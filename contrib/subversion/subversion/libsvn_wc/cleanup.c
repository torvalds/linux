/*
 * cleanup.c:  handle cleaning up workqueue items
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



#include <string.h>

#include "svn_wc.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"

#include "wc.h"
#include "adm_files.h"
#include "lock.h"
#include "workqueue.h"

#include "private/svn_wc_private.h"
#include "svn_private_config.h"


/*** Recursively do log things. ***/

/* */
static svn_error_t *
can_be_cleaned(int *wc_format,
               svn_wc__db_t *db,
               const char *local_abspath,
               apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__internal_check_wc(wc_format, db,
                                    local_abspath, FALSE, scratch_pool));

  /* a "version" of 0 means a non-wc directory */
  if (*wc_format == 0)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("'%s' is not a working copy directory"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (*wc_format < SVN_WC__WC_NG_VERSION)
    return svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
                            _("Log format too old, please use "
                              "Subversion 1.6 or earlier"));

  return SVN_NO_ERROR;
}

/* Dummy svn_wc_status_func4_t implementation */
static svn_error_t *
status_dummy_callback(void *baton,
                      const char *local_abspath,
                      const svn_wc_status3_t *status,
                      apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
cleanup_internal(svn_wc__db_t *db,
                 const char *dir_abspath,
                 svn_boolean_t break_locks,
                 svn_boolean_t fix_recorded_timestamps,
                 svn_boolean_t vacuum_pristines,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  int wc_format;
  svn_boolean_t is_wcroot;
  const char *lock_abspath;

  /* Can we even work with this directory?  */
  SVN_ERR(can_be_cleaned(&wc_format, db, dir_abspath, scratch_pool));

  /* We cannot obtain a lock on a directory that's within a locked
     subtree, so always run cleanup from the lock owner. */
  SVN_ERR(svn_wc__db_wclock_find_root(&lock_abspath, db, dir_abspath,
                                      scratch_pool, scratch_pool));
  if (lock_abspath)
    dir_abspath = lock_abspath;
  SVN_ERR(svn_wc__db_wclock_obtain(db, dir_abspath, -1, break_locks, scratch_pool));

  /* Run our changes before the subdirectories. We may not have to recurse
     if we blow away a subdir.  */
  if (wc_format >= SVN_WC__HAS_WORK_QUEUE)
    SVN_ERR(svn_wc__wq_run(db, dir_abspath, cancel_func, cancel_baton,
                           scratch_pool));

  SVN_ERR(svn_wc__db_is_wcroot(&is_wcroot, db, dir_abspath, scratch_pool));

#ifdef SVN_DEBUG
  SVN_ERR(svn_wc__db_verify(db, dir_abspath, scratch_pool));
#endif

  /* Perform these operations if we lock the entire working copy.
     Note that we really need to check a wcroot value and not
     svn_wc__check_wcroot() as that function, will just return true
     once we start sharing databases with externals.
   */
  if (is_wcroot && vacuum_pristines)
    {
    /* Cleanup the tmp area of the admin subdir, if running the log has not
       removed it!  The logs have been run, so anything left here has no hope
       of being useful. */
      SVN_ERR(svn_wc__adm_cleanup_tmp_area(db, dir_abspath, scratch_pool));

      /* Remove unreferenced pristine texts */
      SVN_ERR(svn_wc__db_pristine_cleanup(db, dir_abspath, scratch_pool));
    }

  if (fix_recorded_timestamps)
    {
      /* Instead of implementing a separate repair step here, use the standard
         status walker's optimized implementation, which performs repairs when
         there is a lock. */
      SVN_ERR(svn_wc__internal_walk_status(db, dir_abspath, svn_depth_infinity,
                                           FALSE /* get_all */,
                                           FALSE /* no_ignore */,
                                           FALSE /* ignore_text_mods */,
                                           NULL /* ignore patterns */,
                                           status_dummy_callback, NULL,
                                           cancel_func, cancel_baton,
                                           scratch_pool));
    }

  /* All done, toss the lock */
  SVN_ERR(svn_wc__db_wclock_release(db, dir_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_cleanup4(svn_wc_context_t *wc_ctx,
                const char *local_abspath,
                svn_boolean_t break_locks,
                svn_boolean_t fix_recorded_timestamps,
                svn_boolean_t clear_dav_cache,
                svn_boolean_t vacuum_pristines,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                svn_wc_notify_func2_t notify_func,
                void *notify_baton,
                apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(wc_ctx != NULL);

  if (break_locks)
    {
      /* We'll handle everything manually.  */

      /* Close the existing database (if any) to avoid problems with
         exclusive database usage */
      SVN_ERR(svn_wc__db_drop_root(wc_ctx->db, local_abspath,
                                   scratch_pool));

      SVN_ERR(svn_wc__db_open(&db,
                              NULL /* ### config */, FALSE, FALSE,
                              scratch_pool, scratch_pool));
    }
  else
    db = wc_ctx->db;

  SVN_ERR(cleanup_internal(db, local_abspath,
                           break_locks,
                           fix_recorded_timestamps,
                           vacuum_pristines,
                           cancel_func, cancel_baton,
                           scratch_pool));

  /* The DAV cache suffers from flakiness from time to time, and the
     pre-1.7 prescribed workarounds aren't as user-friendly in WC-NG. */
  if (clear_dav_cache)
    SVN_ERR(svn_wc__db_base_clear_dav_cache_recursive(db, local_abspath,
                                                      scratch_pool));

  if (vacuum_pristines)
    SVN_ERR(svn_wc__db_vacuum(db, local_abspath, scratch_pool));

  /* We're done with this DB, so proactively close it.  */
  if (break_locks)
    SVN_ERR(svn_wc__db_close(db));

  return SVN_NO_ERROR;
}
