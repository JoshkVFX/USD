//
// Copyright 2020 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef PXR_USD_USD_PRIM_TYPE_INFO_CACHE_H
#define PXR_USD_USD_PRIM_TYPE_INFO_CACHE_H

#include "pxr/pxr.h"
#include "pxr/usd/usd/api.h"
#include "pxr/usd/usd/primTypeInfo.h"
#include "pxr/base/tf/token.h"

#include <tbb/concurrent_hash_map.h>

PXR_NAMESPACE_OPEN_SCOPE

// Class to be used as a static private singleton to cache all distinct prim 
// types used by the prim data.
class Usd_PrimTypeInfoCache {
public:
    Usd_PrimTypeInfoCache() 
        : _emptyPrimTypeInfo(&Usd_PrimTypeInfo::GetEmptyPrimType()) {}
    Usd_PrimTypeInfoCache(const Usd_PrimTypeInfoCache&) = delete;

    // Finds the cached prim type info for the given prim type and list of 
    // applied schemas, creating and caching a new one if it doesn't exist.
    const Usd_PrimTypeInfo *FindOrCreatePrimTypeInfo(
        const TfToken &primType, TfTokenVector &&appliedSchemas)
    {
        TfToken key = _CreatePrimTypeInfoKey(primType, appliedSchemas);
        if (key.IsEmpty()) {
            return GetEmptyPrimTypeInfo();
        }

        // Find try to find the prim type in the type info map
        if (auto primTypeInfo = _primTypeInfoMap.Find(key)) {
            return primTypeInfo;
        }

        // If it's not, create the new type info first and then try to insert 
        // it. We always return the value found in the cache which may not be
        // the type info we created if another thread happened to create the
        // same type info and managed to insert it first. In that case ours just
        // gets deleted since the hash map didn't take ownership.
        std::unique_ptr<Usd_PrimTypeInfo> newPrimTypeInfo(
            new Usd_PrimTypeInfo(primType, std::move(appliedSchemas)));
        return _primTypeInfoMap.Insert(key, std::move(newPrimTypeInfo));
    }

    // Return the single empty prim type info
    const Usd_PrimTypeInfo *GetEmptyPrimTypeInfo() const 
    {
        return _emptyPrimTypeInfo;
    }

private:
    // Creates the unique prim type token key for the given prim type and 
    // ordered list of applied API schemas.
    static TfToken _CreatePrimTypeInfoKey(
        const TfToken &primType,
        const TfTokenVector &appliedSchemaTypes)
    {
        // In the common case where there are no applied schemas, we just use
        // the prim type token itself.
        if (appliedSchemaTypes.empty()) {
            return primType;
        }

        // We generate a full type string that is a comma separated list of 
        // the prim type and then each the applied schema type in order. Note
        // that it's completely valid for there to be applied schemas when the 
        // prim type is empty; they key just starts with an empty prim type.
        size_t tokenSize = appliedSchemaTypes.size() + primType.size();
        for (const TfToken &schemaType : appliedSchemaTypes) {
            tokenSize += schemaType.size();
        }
        std::string fullTypeString;
        fullTypeString.reserve(tokenSize);
        fullTypeString += primType;
        for (const TfToken &schemaType : appliedSchemaTypes) {
            fullTypeString += ",";
            fullTypeString += schemaType;
        }
        return TfToken(fullTypeString);
    }

    // Wrapper around the thread safe hash map implementation used by the 
    // Usd_PrimTypeInfoCache to cache prim type info
    class _ThreadSafeHashMapImpl {
    public:
        _ThreadSafeHashMapImpl() = default;
        _ThreadSafeHashMapImpl(const _ThreadSafeHashMapImpl&) = delete;

        // Find and return a pointer to the prim type info if it already exists.
        const Usd_PrimTypeInfo *Find(const TfToken &key) const 
        {
            _HashMap::const_accessor accessor;
            if (_hashMap.find(accessor, key)) {
                return accessor->second.get();
            }
            return nullptr;
        }

        // Inserts and takes ownership of the prim type info only if it isn't 
        // already in the hash map. Returns the pointer to the value in the map
        // after insertion regardless.
        const Usd_PrimTypeInfo *Insert(
            const TfToken &key, std::unique_ptr<Usd_PrimTypeInfo> valuePtr)
        {
            _HashMap::accessor accessor;
            if (_hashMap.insert(accessor, key)) {
                accessor->second = std::move(valuePtr);
            }
            return accessor->second.get();
        }
    private:
        // Tokens hash to their pointer values but tbb::concurrent_hash_map is
        // more efficient when there is more randomness in the lower order bits of
        // the hash. Thus the shifted hash function.
        struct _TbbHashFunc {
            inline bool equal(const TfToken &l, const TfToken &r) const {
                return l == r;
            }
            inline size_t hash(const TfToken &t) const {
                return t.Hash();
            }
        };

        using _HashMap = tbb::concurrent_hash_map<
            TfToken, std::unique_ptr<Usd_PrimTypeInfo>, _TbbHashFunc>;
        _HashMap _hashMap;
    };

    _ThreadSafeHashMapImpl _primTypeInfoMap;
    const Usd_PrimTypeInfo *_emptyPrimTypeInfo;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif //PXR_USD_USD_PRIM_TYPE_INFO_CACHE_H