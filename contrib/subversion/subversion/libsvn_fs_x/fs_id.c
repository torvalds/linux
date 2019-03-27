/* fs_id.c : FSX's implementation of svn_fs_id_t
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

#include "svn_pools.h"

#include "cached_data.h"
#include "fs_id.h"

#include "../libsvn_fs/fs-loader.h"
#include "private/svn_string_private.h"



/* Structure holding everything needed to implement svn_fs_id_t for FSX.
 */
typedef struct fs_x__id_t
{
  /* API visible part.
     The fsap_data member points to our svn_fs_x__id_context_t object. */
  svn_fs_id_t generic_id;

  /* Private members.
     This addresses the DAG node identified by this ID object.
     If it refers to a TXN, it may become . */
  svn_fs_x__id_t noderev_id;

} fs_x__id_t;



/* The state machine behind this is as follows:

   (A) FS passed in during context construction still open and uses a
       different pool as the context (Usually the initial state).  In that
       case, FS_PATH is NULL and we watch for either pool's cleanup.

       Next states:
       (B). Transition triggered by FS->POOL cleanup.
       (D). Transition triggered by OWNER cleanup.

   (B) FS has been closed but not the OWNER pool, i.e. the context is valid.
       FS is NULL, FS_NAME has been set. No cleanup functions are registered.

       Next states:
       (C). Transition triggered by successful access to the file system.
       (D). Transition triggered by OWNER cleanup.

   (C) FS is open, allocated in the context's OWNER pool (maybe the initial
       state but that is atypical). No cleanup functions are registered.

       Next states:
       (D). Transition triggered by OWNER cleanup.

   (D) Destroyed.  No access nor notification is allowed.
       Final state.

 */
struct svn_fs_x__id_context_t
{
  /* If this is NULL, FS_PATH points to the on-disk path to the file system
     we need to re-open the FS. */
  svn_fs_t *fs;

  /* If FS is NULL, this points to the on-disk path to pass into svn_fs_open2
     to reopen the filesystem.  Allocated in OWNER.  May only be NULL if FS
     is not.*/
  const char *fs_path;

  /* If FS is NULL, this points to svn_fs_open() as passed to the library. */
  svn_error_t *(*svn_fs_open_)(svn_fs_t **,
      const char *,
      apr_hash_t *,
      apr_pool_t *,
      apr_pool_t *);

  /* Pool that this context struct got allocated in. */
  apr_pool_t *owner;

  /* A sub-pool of ONWER.  We use this when querying data from FS.  Gets
     cleanup up immediately after usage.  NULL until needed for the first
     time. */
  apr_pool_t *aux_pool;
};

/* Forward declaration. */
static apr_status_t
fs_cleanup(void *baton);

/* APR pool cleanup notification for the svn_fs_x__id_context_t given as
   BATON.  Sent at state (A)->(D) transition. */
static apr_status_t
owner_cleanup(void *baton)
{
  svn_fs_x__id_context_t *context = baton;

  /* Everything in CONTEXT gets cleaned up automatically.
     However, we must prevent notifications from other pools. */
  apr_pool_cleanup_kill(context->fs->pool, context, fs_cleanup);

  return  APR_SUCCESS;
}

/* APR pool cleanup notification for the svn_fs_x__id_context_t given as
   BATON.  Sent at state (A)->(B) transition. */
static apr_status_t
fs_cleanup(void *baton)
{
  svn_fs_x__id_context_t *context = baton;
  svn_fs_x__data_t *ffd = context->fs->fsap_data;

  /* Remember the FS_PATH to potentially reopen and mark the FS as n/a. */
  context->fs_path = apr_pstrdup(context->owner, context->fs->path);
  context->svn_fs_open_ = ffd->svn_fs_open_;
  context->fs = NULL;


  /* No need for further notifications because from now on, everything is
     allocated in OWNER. */
  apr_pool_cleanup_kill(context->owner, context, owner_cleanup);

  return  APR_SUCCESS;
}

/* Return the filesystem provided by CONTEXT.  Re-open it if necessary.
   Returns NULL if the FS could not be opened. */
static svn_fs_t *
get_fs(svn_fs_x__id_context_t *context)
{
  if (!context->fs)
    {
      svn_error_t *err;

      SVN_ERR_ASSERT_NO_RETURN(context->svn_fs_open_);

      err = context->svn_fs_open_(&context->fs, context->fs_path, NULL,
                                  context->owner, context->owner);
      if (err)
        {
          svn_error_clear(err);
          context->fs = NULL;
        }
    }

  return context->fs;
}

/* Provide the auto-created auxiliary pool from ID's context object. */
static apr_pool_t *
get_aux_pool(const fs_x__id_t *id)
{
  svn_fs_x__id_context_t *context = id->generic_id.fsap_data;
  if (!context->aux_pool)
    context->aux_pool = svn_pool_create(context->owner);

  return context->aux_pool;
}

/* Return the noderev structure identified by ID.  Returns NULL for invalid
   IDs or inaccessible repositories.  The caller should clear the auxiliary
   pool before returning to its respective caller. */
static svn_fs_x__noderev_t *
get_noderev(const fs_x__id_t *id)
{
  svn_fs_x__noderev_t *result = NULL;

  svn_fs_x__id_context_t *context = id->generic_id.fsap_data;
  svn_fs_t *fs = get_fs(context);
  apr_pool_t *pool = get_aux_pool(id);

  if (fs)
    {
      svn_error_t *err = svn_fs_x__get_node_revision(&result, fs,
                                                     &id->noderev_id,
                                                     pool, pool);
      if (err)
        {
          svn_error_clear(err);
          result = NULL;
        }
    }

  return result;
}



/*** Implement v-table functions ***/

/* Implement id_vtable_t.unparse */
static svn_string_t *
id_unparse(const svn_fs_id_t *fs_id,
           apr_pool_t *result_pool)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;
  return svn_fs_x__id_unparse(&id->noderev_id, result_pool);
}

/* Implement id_vtable_t.compare.

   The result is efficiently computed for matching IDs.  The far less
   meaningful "common ancestor" relationship has a larger latency when
   evaluated first for a given context object.  Subsequent calls are
   moderately fast. */
static svn_fs_node_relation_t
id_compare(const svn_fs_id_t *a,
           const svn_fs_id_t *b)
{
  const fs_x__id_t *id_a = (const fs_x__id_t *)a;
  const fs_x__id_t *id_b = (const fs_x__id_t *)b;
  svn_fs_x__noderev_t *noderev_a, *noderev_b;
  svn_boolean_t same_node;

  /* Quick check: same IDs? */
  if (svn_fs_x__id_eq(&id_a->noderev_id, &id_b->noderev_id))
    return svn_fs_node_unchanged;

  /* Fetch the nodesrevs, compare the IDs of the nodes they belong to and
     clean up any temporaries.  If we can't find one of the noderevs, don't
     get access to the FS etc., report the IDs as "unrelated" as only
     valid / existing things may be related. */
  noderev_a = get_noderev(id_a);
  noderev_b = get_noderev(id_b);

  if (noderev_a && noderev_b)
    same_node = svn_fs_x__id_eq(&noderev_a->node_id, &noderev_b->node_id);
  else
    same_node = FALSE;

  svn_pool_clear(get_aux_pool(id_a));
  svn_pool_clear(get_aux_pool(id_b));

  /* Return result. */
  return same_node ? svn_fs_node_common_ancestor : svn_fs_node_unrelated;
}


/* Creating ID's.  */

static id_vtable_t id_vtable = {
  id_unparse,
  id_compare
};

svn_fs_x__id_context_t *
svn_fs_x__id_create_context(svn_fs_t *fs,
                            apr_pool_t *result_pool)
{
  svn_fs_x__id_context_t *result = apr_pcalloc(result_pool, sizeof(*result));
  result->fs = fs;
  result->owner = result_pool;

  /* Check for a special case:
     If the owner of the context also owns the FS, there will be no reason
     to notify them of the respective other's cleanup. */
  if (result_pool != fs->pool)
    {
      /* If the context's owner gets cleaned up before FS, we must disconnect
         from the FS. */
      apr_pool_cleanup_register(result_pool,
                                result,
                                owner_cleanup,
                                apr_pool_cleanup_null);

      /* If the FS gets cleaned up before the context's owner, disconnect
         from the FS and remember its path on disk to be able to re-open it
         later if necessary. */
      apr_pool_cleanup_register(fs->pool,
                                result,
                                fs_cleanup,
                                apr_pool_cleanup_null);
    }

  return result;
}

svn_fs_id_t *
svn_fs_x__id_create(svn_fs_x__id_context_t *context,
                    const svn_fs_x__id_t *noderev_id,
                    apr_pool_t *result_pool)
{
  fs_x__id_t *id;

  /* Special case: NULL IDs */
  if (!svn_fs_x__id_used(noderev_id))
    return NULL;

  /* In theory, the CONTEXT might not be owned by POOL.  It's FS might even
     have been closed.  Make sure we have a context owned by POOL. */
  if (context->owner != result_pool)
    context = svn_fs_x__id_create_context(get_fs(context), result_pool);

  /* Finally, construct the ID object. */
  id = apr_pcalloc(result_pool, sizeof(*id));
  id->noderev_id = *noderev_id;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = context;

  return (svn_fs_id_t *)id;
}
