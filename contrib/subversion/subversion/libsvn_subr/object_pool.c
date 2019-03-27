/*
 * object_pool.c :  generic pool of reference-counted objects
 *
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
 */




#include <assert.h>

#include "svn_error.h"
#include "svn_hash.h"
#include "svn_pools.h"

#include "private/svn_atomic.h"
#include "private/svn_object_pool.h"
#include "private/svn_subr_private.h"
#include "private/svn_dep_compat.h"



/* A reference counting wrapper around the user-provided object.
 */
typedef struct object_ref_t
{
  /* reference to the parent container */
  svn_object_pool__t *object_pool;

  /* identifies the bucket in OBJECT_POOL->OBJECTS in which this entry
   * belongs. */
  svn_membuf_t key;

  /* User provided object. Usually a wrapper. */
  void *object;

  /* private pool. This instance and its other members got allocated in it.
   * Will be destroyed when this instance is cleaned up. */
  apr_pool_t *pool;

  /* Number of references to this data struct */
  volatile svn_atomic_t ref_count;
} object_ref_t;


/* Core data structure.  All access to it must be serialized using MUTEX.
 */
struct svn_object_pool__t
{
  /* serialization object for all non-atomic data in this struct */
  svn_mutex__t *mutex;

  /* object_ref_t.KEY -> object_ref_t* mapping.
   *
   * In shared object mode, there is at most one such entry per key and it
   * may or may not be in use.  In exclusive mode, only unused references
   * will be put here and they form chains if there are multiple unused
   * instances for the key. */
  apr_hash_t *objects;

  /* same as objects->count but allows for non-sync'ed access */
  volatile svn_atomic_t object_count;

  /* Number of entries in OBJECTS with a reference count 0.
     Due to races, this may be *temporarily* off by one or more.
     Hence we must not strictly depend on it. */
  volatile svn_atomic_t unused_count;

  /* the root pool owning this structure */
  apr_pool_t *pool;
};


/* Pool cleanup function for the whole object pool.
 */
static apr_status_t
object_pool_cleanup(void *baton)
{
  svn_object_pool__t *object_pool = baton;

  /* all entries must have been released up by now */
  SVN_ERR_ASSERT_NO_RETURN(   object_pool->object_count
                           == object_pool->unused_count);

  return APR_SUCCESS;
}

/* Remove entries from OBJECTS in OBJECT_POOL that have a ref-count of 0.
 *
 * Requires external serialization on OBJECT_POOL.
 */
static void
remove_unused_objects(svn_object_pool__t *object_pool)
{
  apr_pool_t *subpool = svn_pool_create(object_pool->pool);

  /* process all hash buckets */
  apr_hash_index_t *hi;
  for (hi = apr_hash_first(subpool, object_pool->objects);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      object_ref_t *object_ref = apr_hash_this_val(hi);

      /* note that we won't hand out new references while access
         to the hash is serialized */
      if (svn_atomic_read(&object_ref->ref_count) == 0)
        {
          apr_hash_set(object_pool->objects, object_ref->key.data,
                       object_ref->key.size, NULL);
          svn_atomic_dec(&object_pool->object_count);
          svn_atomic_dec(&object_pool->unused_count);

          svn_pool_destroy(object_ref->pool);
        }
    }

  svn_pool_destroy(subpool);
}

/* Cleanup function called when an object_ref_t gets released.
 */
static apr_status_t
object_ref_cleanup(void *baton)
{
  object_ref_t *object = baton;
  svn_object_pool__t *object_pool = object->object_pool;

  /* If we released the last reference to object, there is one more
     unused entry.

     Note that unused_count does not need to be always exact but only
     needs to become exact *eventually* (we use it to check whether we
     should remove unused objects every now and then).  I.e. it must
     never drift off / get stuck but always reflect the true value once
     all threads left the racy sections.
   */
  if (svn_atomic_dec(&object->ref_count) == 0)
    svn_atomic_inc(&object_pool->unused_count);

  return APR_SUCCESS;
}

/* Handle reference counting for the OBJECT_REF that the caller is about
 * to return.  The reference will be released when POOL gets cleaned up.
 *
 * Requires external serialization on OBJECT_REF->OBJECT_POOL.
 */
static void
add_object_ref(object_ref_t *object_ref,
              apr_pool_t *pool)
{
  /* Update ref counter.
     Note that this is racy with object_ref_cleanup; see comment there. */
  if (svn_atomic_inc(&object_ref->ref_count) == 0)
    svn_atomic_dec(&object_ref->object_pool->unused_count);

  /* Make sure the reference gets released automatically.
     Since POOL might be a parent pool of OBJECT_REF->OBJECT_POOL,
     to the reference counting update before destroing any of the
     pool hierarchy. */
  apr_pool_pre_cleanup_register(pool, object_ref, object_ref_cleanup);
}

/* Actual implementation of svn_object_pool__lookup.
 *
 * Requires external serialization on OBJECT_POOL.
 */
static svn_error_t *
lookup(void **object,
       svn_object_pool__t *object_pool,
       svn_membuf_t *key,
       apr_pool_t *result_pool)
{
  object_ref_t *object_ref
    = apr_hash_get(object_pool->objects, key->data, key->size);

  if (object_ref)
    {
      *object = object_ref->object;
      add_object_ref(object_ref, result_pool);
    }
  else
    {
      *object = NULL;
    }

  return SVN_NO_ERROR;
}

/* Actual implementation of svn_object_pool__insert.
 *
 * Requires external serialization on OBJECT_POOL.
 */
static svn_error_t *
insert(void **object,
       svn_object_pool__t *object_pool,
       const svn_membuf_t *key,
       void *item,
       apr_pool_t *item_pool,
       apr_pool_t *result_pool)
{
  object_ref_t *object_ref
    = apr_hash_get(object_pool->objects, key->data, key->size);
  if (object_ref)
    {
      /* Destroy the new one and return a reference to the existing one
       * because the existing one may already have references on it.
       */
      svn_pool_destroy(item_pool);
    }
  else
    {
      /* add new index entry */
      object_ref = apr_pcalloc(item_pool, sizeof(*object_ref));
      object_ref->object_pool = object_pool;
      object_ref->object = item;
      object_ref->pool = item_pool;

      svn_membuf__create(&object_ref->key, key->size, item_pool);
      object_ref->key.size = key->size;
      memcpy(object_ref->key.data, key->data, key->size);

      apr_hash_set(object_pool->objects, object_ref->key.data,
                   object_ref->key.size, object_ref);
      svn_atomic_inc(&object_pool->object_count);

      /* the new entry is *not* in use yet.
       * add_object_ref will update counters again.
       */
      svn_atomic_inc(&object_ref->object_pool->unused_count);
    }

  /* return a reference to the object we just added */
  *object = object_ref->object;
  add_object_ref(object_ref, result_pool);

  /* limit memory usage */
  if (svn_atomic_read(&object_pool->unused_count) * 2
      > apr_hash_count(object_pool->objects) + 2)
    remove_unused_objects(object_pool);

  return SVN_NO_ERROR;
}


/* API implementation */

svn_error_t *
svn_object_pool__create(svn_object_pool__t **object_pool,
                        svn_boolean_t thread_safe,
                        apr_pool_t *pool)
{
  svn_object_pool__t *result;

  /* construct the object pool in our private ROOT_POOL to survive POOL
   * cleanup and to prevent threading issues with the allocator
   */
  result = apr_pcalloc(pool, sizeof(*result));
  SVN_ERR(svn_mutex__init(&result->mutex, thread_safe, pool));

  result->pool = pool;
  result->objects = svn_hash__make(result->pool);

  /* make sure we clean up nicely.
   * We need two cleanup functions of which exactly one will be run
   * (disabling the respective other as the first step).  If the owning
   * pool does not cleaned up / destroyed explicitly, it may live longer
   * than our allocator.  So, we need do act upon cleanup requests from
   * either side - owning_pool and root_pool.
   */
  apr_pool_cleanup_register(pool, result, object_pool_cleanup,
                            apr_pool_cleanup_null);

  *object_pool = result;
  return SVN_NO_ERROR;
}

apr_pool_t *
svn_object_pool__new_item_pool(svn_object_pool__t *object_pool)
{
  return svn_pool_create(object_pool->pool);
}

svn_error_t *
svn_object_pool__lookup(void **object,
                        svn_object_pool__t *object_pool,
                        svn_membuf_t *key,
                        apr_pool_t *result_pool)
{
  *object = NULL;
  SVN_MUTEX__WITH_LOCK(object_pool->mutex,
                       lookup(object, object_pool, key, result_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_object_pool__insert(void **object,
                        svn_object_pool__t *object_pool,
                        const svn_membuf_t *key,
                        void *item,
                        apr_pool_t *item_pool,
                        apr_pool_t *result_pool)
{
  *object = NULL;
  SVN_MUTEX__WITH_LOCK(object_pool->mutex,
                       insert(object, object_pool, key, item, 
                              item_pool, result_pool));
  return SVN_NO_ERROR;
}
