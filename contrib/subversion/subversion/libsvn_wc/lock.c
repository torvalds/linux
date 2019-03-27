/*
 * lock.c:  routines for locking working copy subdirectories.
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

#define SVN_DEPRECATED

#include <apr_pools.h>
#include <apr_time.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "svn_hash.h"
#include "svn_types.h"

#include "wc.h"
#include "adm_files.h"
#include "lock.h"
#include "props.h"
#include "wc_db.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"




struct svn_wc_adm_access_t
{
  /* PATH to directory which contains the administrative area */
  const char *path;

  /* And the absolute form of the path.  */
  const char *abspath;

  /* Indicates that the baton has been closed. */
  svn_boolean_t closed;

  /* Handle to the administrative database. */
  svn_wc__db_t *db;

  /* Was the DB provided to us? If so, then we'll never close it.  */
  svn_boolean_t db_provided;

  /* ENTRIES_HIDDEN is all cached entries including those in
     state deleted or state absent. It may be NULL. */
  apr_hash_t *entries_all;

  /* POOL is used to allocate cached items, they need to persist for the
     lifetime of this access baton */
  apr_pool_t *pool;

};


/* This is a placeholder used in the set hash to represent missing
   directories.  Only its address is important, it contains no useful
   data. */
static const svn_wc_adm_access_t missing = { 0 };
#define IS_MISSING(lock) ((lock) == &missing)

/* ### hack for now. future functionality coming in a future revision.  */
#define svn_wc__db_is_closed(db) FALSE


svn_error_t *
svn_wc__internal_check_wc(int *wc_format,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          svn_boolean_t check_path,
                          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = svn_wc__db_temp_get_format(wc_format, db, local_abspath, scratch_pool);
  if (err)
    {
      svn_node_kind_t kind;

      if (err->apr_err != SVN_ERR_WC_MISSING &&
          err->apr_err != SVN_ERR_WC_UNSUPPORTED_FORMAT &&
          err->apr_err != SVN_ERR_WC_UPGRADE_REQUIRED)
        return svn_error_trace(err);
      svn_error_clear(err);

      /* ### the stuff below seems to be redundant. get_format() probably
         ### does all this.
         ###
         ### investigate all callers. DEFINITELY keep in mind the
         ### svn_wc_check_wc() entrypoint.
      */

      /* If the format file does not exist or path not directory, then for
         our purposes this is not a working copy, so return 0. */
      *wc_format = 0;

      /* Check path itself exists. */
      SVN_ERR(svn_io_check_path(local_abspath, &kind, scratch_pool));
      if (kind == svn_node_none)
        {
          return svn_error_createf(APR_ENOENT, NULL, _("'%s' does not exist"),
                                   svn_dirent_local_style(local_abspath,
                                                          scratch_pool));
        }
    }

  if (*wc_format >= SVN_WC__WC_NG_VERSION)
    {
      svn_wc__db_status_t db_status;
      svn_node_kind_t db_kind;

      if (check_path)
        {
          /* If a node is not a directory, it is not a working copy
             directory.  This allows creating new working copies as
             a path below an existing working copy. */
          svn_node_kind_t wc_kind;

          SVN_ERR(svn_io_check_path(local_abspath, &wc_kind, scratch_pool));
          if (wc_kind != svn_node_dir)
            {
              *wc_format = 0; /* Not a directory, so not a wc-directory */
              return SVN_NO_ERROR;
            }
        }

      err = svn_wc__db_read_info(&db_status, &db_kind, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL,
                                 db, local_abspath,
                                 scratch_pool, scratch_pool);

      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          *wc_format = 0;
          return SVN_NO_ERROR;
        }
      else
        SVN_ERR(err);

      if (db_kind != svn_node_dir)
        {
          /* The WC thinks there must be a file, so this is not
             a wc-directory */
          *wc_format = 0;
          return SVN_NO_ERROR;
        }

      switch (db_status)
        {
          case svn_wc__db_status_not_present:
          case svn_wc__db_status_server_excluded:
          case svn_wc__db_status_excluded:
            /* If there is a directory here, it is not related to the parent
               working copy: Obstruction */
            *wc_format = 0;
            return SVN_NO_ERROR;
          default:
            break;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_check_wc2(int *wc_format,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 apr_pool_t *scratch_pool)
{
  /* ### Should we pass TRUE for check_path to find obstructions and
         missing directories? */
  return svn_error_trace(
    svn_wc__internal_check_wc(wc_format, wc_ctx->db, local_abspath, FALSE,
                              scratch_pool));
}


/* */
static svn_error_t *
add_to_shared(svn_wc_adm_access_t *lock, apr_pool_t *scratch_pool)
{
  /* ### sometimes we replace &missing with a now-valid lock.  */
  {
    svn_wc_adm_access_t *prior = svn_wc__db_temp_get_access(lock->db,
                                                            lock->abspath,
                                                            scratch_pool);
    if (IS_MISSING(prior))
      SVN_ERR(svn_wc__db_temp_close_access(lock->db, lock->abspath,
                                           prior, scratch_pool));
  }

  svn_wc__db_temp_set_access(lock->db, lock->abspath, lock,
                             scratch_pool);

  return SVN_NO_ERROR;
}


/* */
static svn_wc_adm_access_t *
get_from_shared(const char *abspath,
                svn_wc__db_t *db,
                apr_pool_t *scratch_pool)
{
  /* We closed the DB when it became empty. ABSPATH is not present.  */
  if (db == NULL)
    return NULL;
  return svn_wc__db_temp_get_access(db, abspath, scratch_pool);
}


/* */
static svn_error_t *
close_single(svn_wc_adm_access_t *adm_access,
             svn_boolean_t preserve_lock,
             apr_pool_t *scratch_pool)
{
  svn_boolean_t locked;

  if (adm_access->closed)
    return SVN_NO_ERROR;

  /* Physically unlock if required */
  SVN_ERR(svn_wc__db_wclock_owns_lock(&locked, adm_access->db,
                                      adm_access->abspath, TRUE,
                                      scratch_pool));
  if (locked)
    {
      if (!preserve_lock)
        {
          /* Remove the physical lock in the admin directory for
             PATH. It is acceptable for the administrative area to
             have disappeared, such as when the directory is removed
             from the working copy.  It is an error for the lock to
             have disappeared if the administrative area still exists. */

          svn_error_t *err = svn_wc__db_wclock_release(adm_access->db,
                                                       adm_access->abspath,
                                                       scratch_pool);
          if (err)
            {
              if (svn_wc__adm_area_exists(adm_access->abspath, scratch_pool))
                return err;
              svn_error_clear(err);
            }
        }
    }

  /* Reset to prevent further use of the lock. */
  adm_access->closed = TRUE;

  /* Detach from set */
  SVN_ERR(svn_wc__db_temp_close_access(adm_access->db, adm_access->abspath,
                                       adm_access, scratch_pool));

  /* Possibly close the underlying wc_db. */
  if (!adm_access->db_provided)
    {
      apr_hash_t *opened = svn_wc__db_temp_get_all_access(adm_access->db,
                                                          scratch_pool);
      if (apr_hash_count(opened) == 0)
        {
          SVN_ERR(svn_wc__db_close(adm_access->db));
          adm_access->db = NULL;
        }
    }

  return SVN_NO_ERROR;
}


/* Cleanup for a locked access baton.

   This handles closing access batons when their pool gets destroyed.
   The physical locks associated with such batons remain in the working
   copy if they are protecting work items in the workqueue.  */
static apr_status_t
pool_cleanup_locked(void *p)
{
  svn_wc_adm_access_t *lock = p;
  apr_uint64_t id;
  svn_skel_t *work_item;
  svn_error_t *err;

  if (lock->closed)
    return APR_SUCCESS;

  /* If the DB is closed, then we have a bunch of extra work to do.  */
  if (svn_wc__db_is_closed(lock->db))
    {
      apr_pool_t *scratch_pool;
      svn_wc__db_t *db;

      lock->closed = TRUE;

      /* If there is no ADM area, then we definitely have no work items
         or physical locks to worry about. Bail out.  */
      if (!svn_wc__adm_area_exists(lock->abspath, lock->pool))
        return APR_SUCCESS;

      /* Creating a subpool is safe within a pool cleanup, as long as
         we're absolutely sure to destroy it before we exit this function.

         We avoid using LOCK->POOL to keep the following functions from
         hanging cleanups or subpools from it. (the cleanups *might* get
         run, but the subpools will NOT be destroyed)  */
      scratch_pool = svn_pool_create(lock->pool);

      err = svn_wc__db_open(&db, NULL /* ### config. need! */, FALSE, TRUE,
                            scratch_pool, scratch_pool);
      if (!err)
        {
          err = svn_wc__db_wq_fetch_next(&id, &work_item, db, lock->abspath, 0,
                                         scratch_pool, scratch_pool);
          if (!err && work_item == NULL)
            {
              /* There is no remaining work, so we're good to remove any
                 potential "physical" lock.  */
              err = svn_wc__db_wclock_release(db, lock->abspath, scratch_pool);
            }
        }
      svn_error_clear(err);

      /* Closes the DB, too.  */
      svn_pool_destroy(scratch_pool);

      return APR_SUCCESS;
    }

  /* ### should we create an API that just looks, but doesn't return?  */
  err = svn_wc__db_wq_fetch_next(&id, &work_item, lock->db, lock->abspath, 0,
                                 lock->pool, lock->pool);

  /* Close just this access baton. The pool cleanup will close the rest.  */
  if (!err)
    err = close_single(lock,
                       work_item != NULL /* preserve_lock */,
                       lock->pool);

  if (err)
    {
      apr_status_t apr_err = err->apr_err;
      svn_error_clear(err);
      return apr_err;
    }

  return APR_SUCCESS;
}


/* Cleanup for a readonly access baton.  */
static apr_status_t
pool_cleanup_readonly(void *data)
{
  svn_wc_adm_access_t *lock = data;
  svn_error_t *err;

  if (lock->closed)
    return APR_SUCCESS;

  /* If the DB is closed, then we have nothing to do. There are no
     "physical" locks to remove, and we don't care whether this baton
     is registered with the DB.  */
  if (svn_wc__db_is_closed(lock->db))
    return APR_SUCCESS;

  /* Close this baton. No lock to preserve. Since this is part of the
     pool cleanup, we don't need to close children -- the cleanup process
     will close all children.  */
  err = close_single(lock, FALSE /* preserve_lock */, lock->pool);
  if (err)
    {
      apr_status_t result = err->apr_err;
      svn_error_clear(err);
      return result;
    }

  return APR_SUCCESS;
}


/* An APR pool cleanup handler.  This is a child handler, it removes the
   main pool handler. */
static apr_status_t
pool_cleanup_child(void *p)
{
  svn_wc_adm_access_t *lock = p;

  apr_pool_cleanup_kill(lock->pool, lock, pool_cleanup_locked);
  apr_pool_cleanup_kill(lock->pool, lock, pool_cleanup_readonly);

  return APR_SUCCESS;
}


/* Allocate from POOL, initialise and return an access baton. TYPE and PATH
   are used to initialise the baton.  If STEAL_LOCK, steal the lock if path
   is already locked */
static svn_error_t *
adm_access_alloc(svn_wc_adm_access_t **adm_access,
                 const char *path,
                 svn_wc__db_t *db,
                 svn_boolean_t db_provided,
                 svn_boolean_t write_lock,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *lock = apr_palloc(result_pool, sizeof(*lock));

  lock->closed = FALSE;
  lock->entries_all = NULL;
  lock->db = db;
  lock->db_provided = db_provided;
  lock->path = apr_pstrdup(result_pool, path);
  lock->pool = result_pool;

  SVN_ERR(svn_dirent_get_absolute(&lock->abspath, path, result_pool));

  *adm_access = lock;

  if (write_lock)
    {
      svn_boolean_t owns_lock;

      /* If the db already owns a lock, we can't add an extra lock record */
      SVN_ERR(svn_wc__db_wclock_owns_lock(&owns_lock, db, path, FALSE,
                                          scratch_pool));

      /* If DB owns the lock, but when there is no access baton open for this
         directory, old access baton based code is trying to access data that
         was previously locked by new code. Just hand them the lock, or
         important code paths like svn_wc_add3() will start failing */
      if (!owns_lock
          || svn_wc__adm_retrieve_internal2(db, lock->abspath, scratch_pool))
        {
          SVN_ERR(svn_wc__db_wclock_obtain(db, lock->abspath, 0, FALSE,
                                           scratch_pool));
        }
    }

  err = add_to_shared(lock, scratch_pool);

  if (err)
    return svn_error_compose_create(
                err,
                svn_wc__db_wclock_release(db, lock->abspath, scratch_pool));

  /* ### does this utf8 thing really/still apply??  */
  /* It's important that the cleanup handler is registered *after* at least
     one UTF8 conversion has been done, since such a conversion may create
     the apr_xlate_t object in the pool, and that object must be around
     when the cleanup handler runs.  If the apr_xlate_t cleanup handler
     were to run *before* the access baton cleanup handler, then the access
     baton's handler won't work. */

  /* Register an appropriate cleanup handler, based on the whether this
     access baton is locked or not.  */
  apr_pool_cleanup_register(lock->pool, lock,
                            write_lock
                              ? pool_cleanup_locked
                              : pool_cleanup_readonly,
                            pool_cleanup_child);

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
probe(svn_wc__db_t *db,
      const char **dir,
      const char *path,
      apr_pool_t *pool)
{
  svn_node_kind_t kind;
  int wc_format = 0;

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_dir)
    {
      const char *local_abspath;

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
      SVN_ERR(svn_wc__internal_check_wc(&wc_format, db, local_abspath,
                                        FALSE, pool));
    }

  /* a "version" of 0 means a non-wc directory */
  if (kind != svn_node_dir || wc_format == 0)
    {
      /* Passing a path ending in "." or ".." to svn_dirent_dirname() is
         probably always a bad idea; certainly it is in this case.
         Unfortunately, svn_dirent_dirname()'s current signature can't
         return an error, so we have to insert the protection in this
         caller, ideally the API needs a change.  See issue #1617. */
      const char *base_name = svn_dirent_basename(path, pool);
      if ((strcmp(base_name, "..") == 0)
          || (strcmp(base_name, ".") == 0))
        {
          return svn_error_createf
            (SVN_ERR_WC_BAD_PATH, NULL,
             _("Path '%s' ends in '%s', "
               "which is unsupported for this operation"),
             svn_dirent_local_style(path, pool), base_name);
        }

      *dir = svn_dirent_dirname(path, pool);
    }
  else
    *dir = path;

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
open_single(svn_wc_adm_access_t **adm_access,
            const char *path,
            svn_boolean_t write_lock,
            svn_wc__db_t *db,
            svn_boolean_t db_provided,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  int wc_format = 0;
  svn_error_t *err;
  svn_wc_adm_access_t *lock;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));
  err = svn_wc__internal_check_wc(&wc_format, db, local_abspath, FALSE,
                                  scratch_pool);
  if (wc_format == 0 || (err && APR_STATUS_IS_ENOENT(err->apr_err)))
    {
      return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, err,
                               _("'%s' is not a working copy"),
                               svn_dirent_local_style(path, scratch_pool));
    }
  SVN_ERR(err);

  /* The format version must match exactly. Note that wc_db will perform
     an auto-upgrade if allowed. If it does *not*, then it has decided a
     manual upgrade is required and it should have raised an error.  */
  SVN_ERR_ASSERT(wc_format == SVN_WC__VERSION);

  /* Need to create a new lock */
  SVN_ERR(adm_access_alloc(&lock, path, db, db_provided, write_lock,
                           result_pool, scratch_pool));

  /* ### recurse was here */
  *adm_access = lock;

  return SVN_NO_ERROR;
}


/* Retrieves the KIND of LOCAL_ABSPATH and whether its administrative data is
   available in the working copy.

   *AVAILABLE is set to TRUE when the node and its metadata are available,
   otherwise to FALSE (due to obstruction, missing, absence, exclusion,
   or a "not-present" child).

   KIND can be NULL.

   ### note: this function should go away when we move to a single
   ### adminstrative area.  */
static svn_error_t *
adm_available(svn_boolean_t *available,
              svn_node_kind_t *kind,
              svn_wc__db_t *db,
              const char *local_abspath,
              apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;

  if (kind)
    *kind = svn_node_unknown;

  SVN_ERR(svn_wc__db_read_info(&status, kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               db, local_abspath, scratch_pool, scratch_pool));

  *available = !(status == svn_wc__db_status_server_excluded
                 || status == svn_wc__db_status_excluded
                 || status == svn_wc__db_status_not_present);

  return SVN_NO_ERROR;
}
/* This is essentially the guts of svn_wc_adm_open3.
 *
 * If the working copy is already locked, return SVN_ERR_WC_LOCKED; if
 * it is not a versioned directory, return SVN_ERR_WC_NOT_WORKING_COPY.
 */
static svn_error_t *
do_open(svn_wc_adm_access_t **adm_access,
        const char *path,
        svn_wc__db_t *db,
        svn_boolean_t db_provided,
        apr_array_header_t *rollback,
        svn_boolean_t write_lock,
        int levels_to_lock,
        svn_cancel_func_t cancel_func,
        void *cancel_baton,
        apr_pool_t *result_pool,
        apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *lock;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(open_single(&lock, path, write_lock, db, db_provided,
                      result_pool, iterpool));

  /* Add self to the rollback list in case of error.  */
  APR_ARRAY_PUSH(rollback, svn_wc_adm_access_t *) = lock;

  if (levels_to_lock != 0)
    {
      const apr_array_header_t *children;
      const char *local_abspath = svn_wc__adm_access_abspath(lock);
      int i;

      /* Reduce levels_to_lock since we are about to recurse */
      if (levels_to_lock > 0)
        levels_to_lock--;

      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                       scratch_pool, iterpool));

      /* Open the tree */
      for (i = 0; i < children->nelts; i++)
        {
          const char *node_abspath;
          svn_node_kind_t kind;
          svn_boolean_t available;
          const char *name = APR_ARRAY_IDX(children, i, const char *);

          svn_pool_clear(iterpool);

          /* See if someone wants to cancel this operation. */
          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          node_abspath = svn_dirent_join(local_abspath, name, iterpool);

          SVN_ERR(adm_available(&available,
                                &kind,
                                db,
                                node_abspath,
                                scratch_pool));

          if (kind != svn_node_dir)
            continue;

          if (available)
            {
              const char *node_path = svn_dirent_join(path, name, iterpool);
              svn_wc_adm_access_t *node_access;

              SVN_ERR(do_open(&node_access, node_path, db, db_provided,
                              rollback, write_lock, levels_to_lock,
                              cancel_func, cancel_baton,
                              lock->pool, iterpool));
              /* node_access has been registered in DB, so we don't need
                 to do anything with it.  */
            }
        }
    }
  svn_pool_destroy(iterpool);

  *adm_access = lock;

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
open_all(svn_wc_adm_access_t **adm_access,
         const char *path,
         svn_wc__db_t *db,
         svn_boolean_t db_provided,
         svn_boolean_t write_lock,
         int levels_to_lock,
         svn_cancel_func_t cancel_func,
         void *cancel_baton,
         apr_pool_t *pool)
{
  apr_array_header_t *rollback;
  svn_error_t *err;

  rollback = apr_array_make(pool, 10, sizeof(svn_wc_adm_access_t *));

  err = do_open(adm_access, path, db, db_provided, rollback,
                write_lock, levels_to_lock,
                cancel_func, cancel_baton, pool, pool);
  if (err)
    {
      int i;

      for (i = rollback->nelts; i--; )
        {
          svn_wc_adm_access_t *lock = APR_ARRAY_IDX(rollback, i,
                                                    svn_wc_adm_access_t *);
          SVN_ERR_ASSERT(!IS_MISSING(lock));

          svn_error_clear(close_single(lock, FALSE /* preserve_lock */, pool));
        }
    }

  return svn_error_trace(err);
}


svn_error_t *
svn_wc_adm_open3(svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *associated,
                 const char *path,
                 svn_boolean_t write_lock,
                 int levels_to_lock,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  svn_wc__db_t *db;
  svn_boolean_t db_provided;

  /* Make sure that ASSOCIATED has a set of access batons, so that we can
     glom a reference to self into it. */
  if (associated)
    {
      const char *abspath;
      svn_wc_adm_access_t *lock;

      SVN_ERR(svn_dirent_get_absolute(&abspath, path, pool));
      lock = get_from_shared(abspath, associated->db, pool);
      if (lock && !IS_MISSING(lock))
        /* Already locked.  The reason we don't return the existing baton
           here is that the user is supposed to know whether a directory is
           locked: if it's not locked call svn_wc_adm_open, if it is locked
           call svn_wc_adm_retrieve.  */
        return svn_error_createf(SVN_ERR_WC_LOCKED, NULL,
                                 _("Working copy '%s' locked"),
                                 svn_dirent_local_style(path, pool));
      db = associated->db;
      db_provided = associated->db_provided;
    }
  else
    {
      /* Any baton creation is going to need a shared structure for holding
         data across the entire set. The caller isn't providing one, so we
         do it here.  */
      /* ### we could optimize around levels_to_lock==0, but much of this
         ### is going to be simplified soon anyways.  */
      SVN_ERR(svn_wc__db_open(&db, NULL /* ### config. need! */, FALSE, TRUE,
                              pool, pool));
      db_provided = FALSE;
    }

  return svn_error_trace(open_all(adm_access, path, db, db_provided,
                                  write_lock, levels_to_lock,
                                  cancel_func, cancel_baton, pool));
}


svn_error_t *
svn_wc_adm_probe_open3(svn_wc_adm_access_t **adm_access,
                       svn_wc_adm_access_t *associated,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool)
{
  svn_error_t *err;
  const char *dir;

  if (associated == NULL)
    {
      svn_wc__db_t *db;

      /* Ugh. Too bad about having to open a DB.  */
      SVN_ERR(svn_wc__db_open(&db,
                              NULL /* ### config */, FALSE, TRUE, pool, pool));
      err = probe(db, &dir, path, pool);
      svn_error_clear(svn_wc__db_close(db));
      SVN_ERR(err);
    }
  else
    {
      SVN_ERR(probe(associated->db, &dir, path, pool));
    }

  /* If we moved up a directory, then the path is not a directory, or it
     is not under version control. In either case, the notion of
     levels_to_lock does not apply to the provided path.  Disable it so
     that we don't end up trying to lock more than we need.  */
  if (dir != path)
    levels_to_lock = 0;

  err = svn_wc_adm_open3(adm_access, associated, dir, write_lock,
                         levels_to_lock, cancel_func, cancel_baton, pool);
  if (err)
    {
      svn_error_t *err2;

      /* If we got an error on the parent dir, that means we failed to
         get an access baton for the child in the first place.  And if
         the reason we couldn't get the child access baton is that the
         child is not a versioned directory, then return an error
         about the child, not the parent. */
      svn_node_kind_t child_kind;
      if ((err2 = svn_io_check_path(path, &child_kind, pool)))
        {
          svn_error_compose(err, err2);
          return err;
        }

      if ((dir != path)
          && (child_kind == svn_node_dir)
          && (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY))
        {
          svn_error_clear(err);
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("'%s' is not a working copy"),
                                   svn_dirent_local_style(path, pool));
        }

      return err;
    }

  return SVN_NO_ERROR;
}


svn_wc_adm_access_t *
svn_wc__adm_retrieve_internal2(svn_wc__db_t *db,
                               const char *abspath,
                               apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *adm_access = get_from_shared(abspath, db, scratch_pool);

  /* If the entry is marked as "missing", then return nothing.  */
  if (IS_MISSING(adm_access))
    adm_access = NULL;

  return adm_access;
}


/* SVN_DEPRECATED */
svn_error_t *
svn_wc_adm_retrieve(svn_wc_adm_access_t **adm_access,
                    svn_wc_adm_access_t *associated,
                    const char *path,
                    apr_pool_t *pool)
{
  const char *local_abspath;
  svn_node_kind_t kind = svn_node_unknown;
  svn_node_kind_t wckind;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  if (strcmp(associated->path, path) == 0)
    *adm_access = associated;
  else
    *adm_access = svn_wc__adm_retrieve_internal2(associated->db, local_abspath,
                                                 pool);

  /* We found what we're looking for, so bail. */
  if (*adm_access)
    return SVN_NO_ERROR;

  /* Most of the code expects access batons to exist, so returning an error
     generally makes the calling code simpler as it doesn't need to check
     for NULL batons. */
  /* We are going to send a SVN_ERR_WC_NOT_LOCKED, but let's provide
     a bit more information to our caller */

  err = svn_io_check_path(path, &wckind, pool);

  /* If we can't check the path, we can't make a good error message.  */
  if (err)
    {
      return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, err,
                               _("Unable to check path existence for '%s'"),
                               svn_dirent_local_style(path, pool));
    }

  if (associated)
    {
      err = svn_wc__db_read_kind(&kind, svn_wc__adm_get_db(associated),
                                 local_abspath,
                                 TRUE /* allow_missing */,
                                 TRUE /* show_deleted */,
                                 FALSE /* show_hidden */, pool);

      if (err)
        {
          kind = svn_node_unknown;
          svn_error_clear(err);
        }
    }

  if (kind == svn_node_dir && wckind == svn_node_file)
    {
      err = svn_error_createf(
               SVN_ERR_WC_NOT_WORKING_COPY, NULL,
               _("Expected '%s' to be a directory but found a file"),
               svn_dirent_local_style(path, pool));

      return svn_error_create(SVN_ERR_WC_NOT_LOCKED, err, err->message);
    }

  if (kind != svn_node_dir && kind != svn_node_unknown)
    {
      err = svn_error_createf(
               SVN_ERR_WC_NOT_WORKING_COPY, NULL,
               _("Can't retrieve an access baton for non-directory '%s'"),
               svn_dirent_local_style(path, pool));

      return svn_error_create(SVN_ERR_WC_NOT_LOCKED, err, err->message);
    }

  if (kind == svn_node_unknown || wckind == svn_node_none)
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("Directory '%s' is missing"),
                              svn_dirent_local_style(path, pool));

      return svn_error_create(SVN_ERR_WC_NOT_LOCKED, err, err->message);
    }

  /* If all else fails, return our useless generic error.  */
  return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                           _("Working copy '%s' is not locked"),
                           svn_dirent_local_style(path, pool));
}


/* SVN_DEPRECATED */
svn_error_t *
svn_wc_adm_probe_retrieve(svn_wc_adm_access_t **adm_access,
                          svn_wc_adm_access_t *associated,
                          const char *path,
                          apr_pool_t *pool)
{
  const char *dir;
  const char *local_abspath;
  svn_node_kind_t kind;
  svn_error_t *err;

  SVN_ERR_ASSERT(associated != NULL);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__db_read_kind(&kind, associated->db, local_abspath,
                               TRUE /* allow_missing */,
                               TRUE /* show_deleted */,
                               FALSE /* show_hidden*/,
                               pool));

  if (kind == svn_node_dir)
    dir = path;
  else if (kind != svn_node_unknown)
    dir = svn_dirent_dirname(path, pool);
  else
    /* Not a versioned item, probe it */
    SVN_ERR(probe(associated->db, &dir, path, pool));

  err = svn_wc_adm_retrieve(adm_access, associated, dir, pool);
  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* We'll receive a NOT LOCKED error for various reasons,
         including the reason we'll actually want to test for:
         The path is a versioned directory, but missing, in which case
         we want its parent's adm_access (which holds minimal data
         on the child) */
      svn_error_clear(err);
      SVN_ERR(probe(associated->db, &dir, path, pool));
      SVN_ERR(svn_wc_adm_retrieve(adm_access, associated, dir, pool));
    }
  else
    return svn_error_trace(err);

  return SVN_NO_ERROR;
}


/* SVN_DEPRECATED */
svn_error_t *
svn_wc_adm_probe_try3(svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      int levels_to_lock,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_wc_adm_probe_retrieve(adm_access, associated, path, pool);

  /* SVN_ERR_WC_NOT_LOCKED would mean there was no access baton for
     path in associated, in which case we want to open an access
     baton and add it to associated. */
  if (err && (err->apr_err == SVN_ERR_WC_NOT_LOCKED))
    {
      svn_error_clear(err);
      err = svn_wc_adm_probe_open3(adm_access, associated,
                                   path, write_lock, levels_to_lock,
                                   cancel_func, cancel_baton,
                                   svn_wc_adm_access_pool(associated));

      /* If the path is not a versioned directory, we just return a
         null access baton with no error.  Note that of the errors we
         do report, the most important (and probably most likely) is
         SVN_ERR_WC_LOCKED.  That error would mean that someone else
         has this area locked, and we definitely want to bail in that
         case. */
      if (err && (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY))
        {
          svn_error_clear(err);
          *adm_access = NULL;
          err = NULL;
        }
    }

  return err;
}


/* */
static svn_error_t *
child_is_disjoint(svn_boolean_t *disjoint,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool)
{
  svn_boolean_t is_switched;

  /* Check if the parent directory knows about this node */
  SVN_ERR(svn_wc__db_is_switched(disjoint, &is_switched, NULL,
                                 db, local_abspath, scratch_pool));

  if (*disjoint)
    return SVN_NO_ERROR;

  if (is_switched)
    *disjoint = TRUE;

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
open_anchor(svn_wc_adm_access_t **anchor_access,
            svn_wc_adm_access_t **target_access,
            const char **target,
            svn_wc__db_t *db,
            svn_boolean_t db_provided,
            const char *path,
            svn_boolean_t write_lock,
            int levels_to_lock,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            apr_pool_t *pool)
{
  const char *base_name = svn_dirent_basename(path, pool);

  /* Any baton creation is going to need a shared structure for holding
     data across the entire set. The caller isn't providing one, so we
     do it here.  */
  /* ### we could maybe skip the shared struct for levels_to_lock==0, but
     ### given that we need DB for format detection, may as well keep this.
     ### in any case, much of this is going to be simplified soon anyways.  */
  if (!db_provided)
    SVN_ERR(svn_wc__db_open(&db, NULL, /* ### config. need! */ FALSE, TRUE,
                            pool, pool));

  if (svn_path_is_empty(path)
      || svn_dirent_is_root(path, strlen(path))
      || ! strcmp(base_name, ".."))
    {
      SVN_ERR(open_all(anchor_access, path, db, db_provided,
                       write_lock, levels_to_lock,
                       cancel_func, cancel_baton, pool));
      *target_access = *anchor_access;
      *target = "";
    }
  else
    {
      svn_error_t *err;
      svn_wc_adm_access_t *p_access = NULL;
      svn_wc_adm_access_t *t_access = NULL;
      const char *parent = svn_dirent_dirname(path, pool);
      const char *local_abspath;
      svn_error_t *p_access_err = SVN_NO_ERROR;

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

      /* Try to open parent of PATH to setup P_ACCESS */
      err = open_single(&p_access, parent, write_lock, db, db_provided,
                        pool, pool);
      if (err)
        {
          const char *abspath = svn_dirent_dirname(local_abspath, pool);
          svn_wc_adm_access_t *existing_adm = svn_wc__db_temp_get_access(db, abspath, pool);

          if (IS_MISSING(existing_adm))
            svn_wc__db_temp_clear_access(db, abspath, pool);
          else
            SVN_ERR_ASSERT(existing_adm == NULL);

          if (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY)
            {
              svn_error_clear(err);
              p_access = NULL;
            }
          else if (write_lock && (err->apr_err == SVN_ERR_WC_LOCKED
                                  || APR_STATUS_IS_EACCES(err->apr_err)))
            {
              /* If P_ACCESS isn't to be returned then a read-only baton
                 will do for now, but keep the error in case we need it. */
              svn_error_t *err2 = open_single(&p_access, parent, FALSE,
                                              db, db_provided, pool, pool);
              if (err2)
                {
                  svn_error_clear(err2);
                  return err;
                }
              p_access_err = err;
            }
          else
            return err;
        }

      /* Try to open PATH to setup T_ACCESS */
      err = open_all(&t_access, path, db, db_provided, write_lock,
                     levels_to_lock, cancel_func, cancel_baton, pool);
      if (err)
        {
          if (p_access == NULL)
            {
              /* Couldn't open the parent or the target. Bail out.  */
              svn_error_clear(p_access_err);
              return svn_error_trace(err);
            }

          if (err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            {
              if (p_access)
                svn_error_clear(svn_wc_adm_close2(p_access, pool));
              svn_error_clear(p_access_err);
              return svn_error_trace(err);
            }

          /* This directory is not under version control. Ignore it.  */
          svn_error_clear(err);
          t_access = NULL;
        }

      /* At this stage might have P_ACCESS, T_ACCESS or both */

      /* Check for switched or disjoint P_ACCESS and T_ACCESS */
      if (p_access && t_access)
        {
          svn_boolean_t disjoint;

          err = child_is_disjoint(&disjoint, db, local_abspath, pool);
          if (err)
            {
              svn_error_clear(p_access_err);
              svn_error_clear(svn_wc_adm_close2(p_access, pool));
              svn_error_clear(svn_wc_adm_close2(t_access, pool));
              return svn_error_trace(err);
            }

          if (disjoint)
            {
              /* Switched or disjoint, so drop P_ACCESS. Don't close any
                 descendants, or we might blast the child.  */
              err = close_single(p_access, FALSE /* preserve_lock */, pool);
              if (err)
                {
                  svn_error_clear(p_access_err);
                  svn_error_clear(svn_wc_adm_close2(t_access, pool));
                  return svn_error_trace(err);
                }
              p_access = NULL;
            }
        }

      /* We have a parent baton *and* we have an error related to opening
         the baton. That means we have a readonly baton, but that isn't
         going to work for us. (p_access would have been set to NULL if
         a writable parent baton is not required)  */
      if (p_access && p_access_err)
        {
          if (t_access)
            svn_error_clear(svn_wc_adm_close2(t_access, pool));
          svn_error_clear(svn_wc_adm_close2(p_access, pool));
          return svn_error_trace(p_access_err);
        }
      svn_error_clear(p_access_err);

      if (! t_access)
        {
          svn_boolean_t available;
          svn_node_kind_t kind;

          err = adm_available(&available, &kind, db, local_abspath, pool);

          if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            svn_error_clear(err);
          else if (err)
            {
              svn_error_clear(svn_wc_adm_close2(p_access, pool));
              return svn_error_trace(err);
            }
        }

      *anchor_access = p_access ? p_access : t_access;
      *target_access = t_access ? t_access : p_access;

      if (! p_access)
        *target = "";
      else
        *target = base_name;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_adm_open_anchor(svn_wc_adm_access_t **anchor_access,
                       svn_wc_adm_access_t **target_access,
                       const char **target,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool)
{
  return svn_error_trace(open_anchor(anchor_access, target_access, target,
                                     NULL, FALSE, path, write_lock,
                                     levels_to_lock, cancel_func,
                                     cancel_baton, pool));
}


/* Does the work of closing the access baton ADM_ACCESS.  Any physical
   locks are removed from the working copy if PRESERVE_LOCK is FALSE, or
   are left if PRESERVE_LOCK is TRUE.  Any associated access batons that
   are direct descendants will also be closed.
 */
static svn_error_t *
do_close(svn_wc_adm_access_t *adm_access,
         svn_boolean_t preserve_lock,
         apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *look;

  if (adm_access->closed)
    return SVN_NO_ERROR;

  /* If we are part of the shared set, then close descendant batons.  */
  look = get_from_shared(adm_access->abspath, adm_access->db, scratch_pool);
  if (look != NULL)
    {
      apr_hash_t *opened;
      apr_hash_index_t *hi;

      /* Gather all the opened access batons from the DB.  */
      opened = svn_wc__db_temp_get_all_access(adm_access->db, scratch_pool);

      /* Close any that are descendants of this baton.  */
      for (hi = apr_hash_first(scratch_pool, opened);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *abspath = apr_hash_this_key(hi);
          svn_wc_adm_access_t *child = apr_hash_this_val(hi);
          const char *path = child->path;

          if (IS_MISSING(child))
            {
              /* We don't close the missing entry, but get rid of it from
                 the set. */
              svn_wc__db_temp_clear_access(adm_access->db, abspath,
                                           scratch_pool);
              continue;
            }

          if (! svn_dirent_is_ancestor(adm_access->path, path)
              || strcmp(adm_access->path, path) == 0)
            continue;

          SVN_ERR(close_single(child, preserve_lock, scratch_pool));
        }
    }

  return svn_error_trace(close_single(adm_access, preserve_lock,
                                      scratch_pool));
}


/* SVN_DEPRECATED */
svn_error_t *
svn_wc_adm_close2(svn_wc_adm_access_t *adm_access, apr_pool_t *scratch_pool)
{
  return svn_error_trace(do_close(adm_access, FALSE, scratch_pool));
}


/* SVN_DEPRECATED */
svn_boolean_t
svn_wc_adm_locked(const svn_wc_adm_access_t *adm_access)
{
  svn_boolean_t locked;
  apr_pool_t *subpool = svn_pool_create(adm_access->pool);
  svn_error_t *err = svn_wc__db_wclock_owns_lock(&locked, adm_access->db,
                                                 adm_access->abspath, TRUE,
                                                 subpool);
  svn_pool_destroy(subpool);

  if (err)
    {
      svn_error_clear(err);
      /* ### is this right? */
      return FALSE;
    }

  return locked;
}

svn_error_t *
svn_wc__write_check(svn_wc__db_t *db,
                    const char *local_abspath,
                    apr_pool_t *scratch_pool)
{
  svn_boolean_t locked;

  SVN_ERR(svn_wc__db_wclock_owns_lock(&locked, db, local_abspath, FALSE,
                                      scratch_pool));
  if (!locked)
    return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                             _("No write-lock in '%s'"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_locked2(svn_boolean_t *locked_here,
               svn_boolean_t *locked,
               svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (locked_here != NULL)
    SVN_ERR(svn_wc__db_wclock_owns_lock(locked_here, wc_ctx->db, local_abspath,
                                        FALSE, scratch_pool));
  if (locked != NULL)
    SVN_ERR(svn_wc__db_wclocked(locked, wc_ctx->db, local_abspath,
                                scratch_pool));

  return SVN_NO_ERROR;
}


/* SVN_DEPRECATED */
const char *
svn_wc_adm_access_path(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->path;
}


const char *
svn_wc__adm_access_abspath(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->abspath;
}


/* SVN_DEPRECATED */
apr_pool_t *
svn_wc_adm_access_pool(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->pool;
}

apr_pool_t *
svn_wc__adm_access_pool_internal(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->pool;
}

void
svn_wc__adm_access_set_entries(svn_wc_adm_access_t *adm_access,
                               apr_hash_t *entries)
{
  adm_access->entries_all = entries;
}


apr_hash_t *
svn_wc__adm_access_entries(svn_wc_adm_access_t *adm_access)
{
  /* Compile with -DSVN_DISABLE_ENTRY_CACHE to disable the in-memory
     entry caching. As of 2010-03-18 (r924708) merge_tests 34 and 134
     fail during "make check".  */
#ifdef SVN_DISABLE_ENTRY_CACHE
  return NULL;
#else
  return adm_access->entries_all;
#endif
}


svn_wc__db_t *
svn_wc__adm_get_db(const svn_wc_adm_access_t *adm_access)
{
  return adm_access->db;
}

svn_error_t *
svn_wc__acquire_write_lock(const char **lock_root_abspath,
                           svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           svn_boolean_t lock_anchor,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_boolean_t is_wcroot;
  svn_boolean_t is_switched;
  svn_node_kind_t kind;
  svn_error_t *err;

  err = svn_wc__db_is_switched(&is_wcroot, &is_switched, &kind,
                               db, local_abspath, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);

      kind = svn_node_none;
      is_wcroot = FALSE;
      is_switched = FALSE;
    }

  if (!lock_root_abspath && kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_WC_NOT_DIRECTORY, NULL,
                             _("Can't obtain lock on non-directory '%s'."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (lock_anchor && kind == svn_node_dir)
    {
      if (is_wcroot)
        lock_anchor = FALSE;
    }

  if (lock_anchor)
    {
      const char *parent_abspath;
      SVN_ERR_ASSERT(lock_root_abspath != NULL);

      parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

      if (kind == svn_node_dir)
        {
          if (! is_switched)
            local_abspath = parent_abspath;
        }
      else if (kind != svn_node_none && kind != svn_node_unknown)
        {
          /* In the single-DB world we know parent exists */
          local_abspath = parent_abspath;
        }
      else
        {
          /* Can't lock parents that don't exist */
          svn_node_kind_t parent_kind;
          err = svn_wc__db_read_kind(&parent_kind, db, parent_abspath,
                                     TRUE /* allow_missing */,
                                     TRUE /* show_deleted */,
                                     FALSE /* show_hidden */,
                                     scratch_pool);
          if (err && SVN_WC__ERR_IS_NOT_CURRENT_WC(err))
            {
              svn_error_clear(err);
              parent_kind = svn_node_unknown;
            }
          else
            SVN_ERR(err);

          if (parent_kind != svn_node_dir)
            return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                     _("'%s' is not a working copy"),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));

          local_abspath = parent_abspath;
        }
    }
  else if (kind != svn_node_dir)
    {
      local_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
    }

  if (lock_root_abspath)
    *lock_root_abspath = apr_pstrdup(result_pool, local_abspath);

  SVN_ERR(svn_wc__db_wclock_obtain(wc_ctx->db, local_abspath,
                                   -1 /* levels_to_lock (infinite) */,
                                   FALSE /* steal_lock */,
                                   scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__release_write_lock(svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           apr_pool_t *scratch_pool)
{
  apr_uint64_t id;
  svn_skel_t *work_item;

  SVN_ERR(svn_wc__db_wq_fetch_next(&id, &work_item, wc_ctx->db, local_abspath,
                                   0, scratch_pool, scratch_pool));
  if (work_item)
    {
      /* Do not release locks (here or below) if there is work to do.  */
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__db_wclock_release(wc_ctx->db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__call_with_write_lock(svn_wc__with_write_lock_func_t func,
                             void *baton,
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             svn_boolean_t lock_anchor,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_error_t *err1, *err2;
  const char *lock_root_abspath;

  SVN_ERR(svn_wc__acquire_write_lock(&lock_root_abspath, wc_ctx, local_abspath,
                                     lock_anchor, scratch_pool, scratch_pool));
  err1 = svn_error_trace(func(baton, result_pool, scratch_pool));
  err2 = svn_wc__release_write_lock(wc_ctx, lock_root_abspath, scratch_pool);
  return svn_error_compose_create(err1, err2);
}


svn_error_t *
svn_wc__acquire_write_lock_for_resolve(const char **lock_root_abspath,
                                       svn_wc_context_t *wc_ctx,
                                       const char *local_abspath,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  svn_boolean_t locked = FALSE;
  const char *obtained_abspath;
  const char *requested_abspath = local_abspath;

  while (!locked)
    {
      const char *required_abspath;
      const char *child;

      SVN_ERR(svn_wc__acquire_write_lock(&obtained_abspath, wc_ctx,
                                         requested_abspath, FALSE,
                                         scratch_pool, scratch_pool));
      locked = TRUE;

      SVN_ERR(svn_wc__required_lock_for_resolve(&required_abspath,
                                                wc_ctx->db, local_abspath,
                                                scratch_pool, scratch_pool));

      /* It's possible for the required lock path to be an ancestor
         of, a descendant of, or equal to, the obtained lock path. If
         it's an ancestor we have to try again, otherwise the obtained
         lock will do. */
      child = svn_dirent_skip_ancestor(required_abspath, obtained_abspath);
      if (child && child[0])
        {
          SVN_ERR(svn_wc__release_write_lock(wc_ctx, obtained_abspath,
                                             scratch_pool));
          locked = FALSE;
          requested_abspath = required_abspath;
        }
      else
        {
          /* required should be a descendant of, or equal to, obtained */
          SVN_ERR_ASSERT(!strcmp(required_abspath, obtained_abspath)
                         || svn_dirent_skip_ancestor(obtained_abspath,
                                                     required_abspath));
        }
    }

  *lock_root_abspath = apr_pstrdup(result_pool, obtained_abspath);

  return SVN_NO_ERROR;
}
