/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file svn_object_pool.h
 * @brief multithreaded object pool API
 *
 * This is the core data structure behind various object pools.  It
 * provides a thread-safe associative container for object instances of
 * the same type.
 *
 * Memory and lifetime management for the objects are handled by the pool.
 * Reference counting takes care that neither objects nor the object pool
 * get actually destroyed while other parts depend on them.  All objects
 * are thought to be recycle-able and live in their own root memory pools
 * making them (potentially) safe to be used from different threads.
 * Currently unused objects may be kept around for a while and returned
 * by the next lookup.
 *
 * Two modes are supported: shared use and exclusive use.  In shared mode,
 * any object can be handed out to multiple users and in potentially
 * different threads at the same time.  In exclusive mode, the same object
 * will only be referenced at most once.
 *
 * Object creation and access must be provided outside this structure.
 * In particular, the using container will usually wrap the actual object
 * in a meta-data struct containing key information etc and must provide
 * getters and setters for those wrapper structs.
 */



#ifndef SVN_OBJECT_POOL_H
#define SVN_OBJECT_POOL_H

#include <apr.h>        /* for apr_int64_t */
#include <apr_pools.h>  /* for apr_pool_t */
#include <apr_hash.h>   /* for apr_hash_t */

#include "svn_types.h"

#include "private/svn_mutex.h"
#include "private/svn_string_private.h"



/* The opaque object container type. */
typedef struct svn_object_pool__t svn_object_pool__t;

/* Create a new object pool in POOL and return it in *OBJECT_POOL.
 * Objects are reference-counted and stored as opaque pointers.  Each
 * must be allocated in a separate pool ceated by
 * svn_object_pool__new_item_pool.  Unused objects get destroyed at
 * the object pool's discretion.
 *
 * If THREAD_SAFE is not set, neither the object pool nor the object
 * references returned from it may be accessed from multiple threads.
 *
 * It is not legal to call any API on the object pool after POOL got
 * cleared or destroyed nor to use any objects from this object pool.
 */
svn_error_t *
svn_object_pool__create(svn_object_pool__t **object_pool,
                        svn_boolean_t thread_safe,
                        apr_pool_t *pool);

/* Return a pool to allocate the new object.
 */
apr_pool_t *
svn_object_pool__new_item_pool(svn_object_pool__t *object_pool);

/* In OBJECT_POOL, look for an available object by KEY and return a
 * reference to it in *OBJECT.  If none can be found, *OBJECT will be NULL.
 *
 * The reference will be returned when *RESULT_POOL and may be destroyed
 * or recycled by OBJECT_POOL.
 */
svn_error_t *
svn_object_pool__lookup(void **object,
                        svn_object_pool__t *object_pool,
                        svn_membuf_t *key,
                        apr_pool_t *result_pool);

/* Store the object ITEM under KEY in OBJECT_POOL and return a reference
 * to the object in *OBJECT (just like lookup).
 *
 * The object must have been created in ITEM_POOL and the latter must
 * have been created by svn_object_pool__new_item_pool.
 *
 * The reference will be returned when *RESULT_POOL gets cleaned up or
 * destroyed.
 */
svn_error_t *
svn_object_pool__insert(void **object,
                        svn_object_pool__t *object_pool,
                        const svn_membuf_t *key,
                        void *item,
                        apr_pool_t *item_pool,
                        apr_pool_t *result_pool);

#endif /* SVN_OBJECT_POOL_H */
