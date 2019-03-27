/*
 * wc_db_wcroot.c :  supporting datastructures for the administrative database
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

#define SVN_WC__I_AM_WC_DB

#include <assert.h>

#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_version.h"

#include "wc.h"
#include "adm_files.h"
#include "wc_db_private.h"
#include "wc-queries.h"

#include "svn_private_config.h"

/* ### Same values as wc_db.c */
#define SDB_FILE  "wc.db"
#define UNKNOWN_WC_ID ((apr_int64_t) -1)
#define FORMAT_FROM_SDB (-1)

/* #define VERIFY_ON_CLOSE */

/* Get the format version from a wc-1 directory. If it is not a working copy
   directory, then it sets VERSION to zero and returns no error.  */
static svn_error_t *
get_old_version(int *version,
                const char *abspath,
                apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *format_file_path;
  svn_node_kind_t kind;

  /* Try reading the format number from the entries file.  */
  format_file_path = svn_wc__adm_child(abspath, SVN_WC__ADM_ENTRIES,
                                       scratch_pool);

  /* Since trying to open a non-existent file is quite expensive, try a
     quick stat call first. In wc-ng w/cs, this will be an early exit. */
  SVN_ERR(svn_io_check_path(format_file_path, &kind, scratch_pool));
  if (kind == svn_node_none)
    {
      *version = 0;
      return SVN_NO_ERROR;
    }

  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;
  if (err->apr_err != SVN_ERR_BAD_VERSION_FILE_FORMAT
      && !APR_STATUS_IS_ENOENT(err->apr_err)
      && !APR_STATUS_IS_ENOTDIR(err->apr_err))
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(abspath, scratch_pool));
  svn_error_clear(err);

  /* This must be a really old working copy!  Fall back to reading the
     format file.

     Note that the format file might not exist in newer working copies
     (format 7 and higher), but in that case, the entries file should
     have contained the format number. */
  format_file_path = svn_wc__adm_child(abspath, SVN_WC__ADM_FORMAT,
                                       scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;

  /* Whatever error may have occurred... we can just ignore. This is not
     a working copy directory. Signal the caller.  */
  svn_error_clear(err);

  *version = 0;
  return SVN_NO_ERROR;
}


/* A helper function to parse_local_abspath() which returns the on-disk KIND
   of LOCAL_ABSPATH, using DB and SCRATCH_POOL as needed.

   This function may do strange things, but at long as it comes up with the
   Right Answer, we should be happy. */
static svn_error_t *
get_path_kind(svn_node_kind_t *kind,
              svn_wc__db_t *db,
              const char *local_abspath,
              apr_pool_t *scratch_pool)
{
  svn_boolean_t special;
  svn_node_kind_t node_kind;

  /* This implements a *really* simple LRU cache, where "simple" is defined
     as "only one element".  In other words, we remember the most recently
     queried path, and nothing else.  This gives >80% cache hits. */

  if (db->parse_cache.abspath
        && strcmp(db->parse_cache.abspath->data, local_abspath) == 0)
    {
      /* Cache hit! */
      *kind = db->parse_cache.kind;
      return SVN_NO_ERROR;
    }

  if (!db->parse_cache.abspath)
    {
      db->parse_cache.abspath = svn_stringbuf_create(local_abspath,
                                                     db->state_pool);
    }
  else
    {
      svn_stringbuf_set(db->parse_cache.abspath, local_abspath);
    }

  SVN_ERR(svn_io_check_special_path(local_abspath, &node_kind,
                                    &special, scratch_pool));

  db->parse_cache.kind = (special ? svn_node_symlink : node_kind);
  *kind = db->parse_cache.kind;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_verify_no_work(svn_sqlite__db_t *sdb)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_LOOK_FOR_WORK));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (have_row)
    return svn_error_create(SVN_ERR_WC_CLEANUP_REQUIRED, NULL,
                            NULL /* nothing to add.  */);

  return SVN_NO_ERROR;
}

#if defined(VERIFY_ON_CLOSE) && defined(SVN_DEBUG)
/* Implements svn_wc__db_verify_cb_t */
static svn_error_t *
verify_db_cb(void *baton,
             const char *wc_abspath,
             const char *local_relpath,
             int op_depth,
             int id,
             const char *msg,
             apr_pool_t *scratch_pool)
{
  if (op_depth >= 0)
    SVN_DBG(("DB-VRFY: %s: %s (%d): SV%04d %s",
              wc_abspath, local_relpath, op_depth, id, msg));
  else
    SVN_DBG(("DB-VRFY: %s: %s: SV%04d %s",
              wc_abspath, local_relpath, id, msg));

  return SVN_NO_ERROR;
}
#endif

/* */
static apr_status_t
close_wcroot(void *data)
{
  svn_wc__db_wcroot_t *wcroot = data;
  svn_error_t *err;

  SVN_ERR_ASSERT_NO_RETURN(wcroot->sdb != NULL);

#if defined(VERIFY_ON_CLOSE) && defined(SVN_DEBUG)
  if (getenv("SVN_CMDLINE_VERIFY_SQL_AT_CLOSE"))
    {
      apr_pool_t *scratch_pool = svn_pool_create(NULL);

      svn_error_clear(svn_wc__db_verify_db_full_internal(
                                    wcroot, verify_db_cb, NULL, scratch_pool));

      svn_pool_destroy(scratch_pool);
    }
#endif

  err = svn_sqlite__close(wcroot->sdb);
  wcroot->sdb = NULL;
  if (err)
    {
      apr_status_t result = err->apr_err;
      svn_error_clear(err);
      return result;
    }

  return APR_SUCCESS;
}


svn_error_t *
svn_wc__db_open(svn_wc__db_t **db,
                svn_config_t *config,
                svn_boolean_t open_without_upgrade,
                svn_boolean_t enforce_empty_wq,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  *db = apr_pcalloc(result_pool, sizeof(**db));
  (*db)->config = config;
  (*db)->verify_format = !open_without_upgrade;
  (*db)->enforce_empty_wq = enforce_empty_wq;
  (*db)->dir_data = apr_hash_make(result_pool);

  (*db)->state_pool = result_pool;

  /* Don't need to initialize (*db)->parse_cache, due to the calloc above */
  if (config)
    {
      svn_error_t *err;
      svn_boolean_t sqlite_exclusive = FALSE;
      apr_int64_t timeout;

      err = svn_config_get_bool(config, &sqlite_exclusive,
                                SVN_CONFIG_SECTION_WORKING_COPY,
                                SVN_CONFIG_OPTION_SQLITE_EXCLUSIVE,
                                FALSE);
      if (err)
        {
          svn_error_clear(err);
        }
      else
        (*db)->exclusive = sqlite_exclusive;

      err = svn_config_get_int64(config, &timeout,
                                 SVN_CONFIG_SECTION_WORKING_COPY,
                                 SVN_CONFIG_OPTION_SQLITE_BUSY_TIMEOUT,
                                 0);
      if (err || timeout < 0 || timeout > APR_INT32_MAX)
        svn_error_clear(err);
      else
        (*db)->timeout = (apr_int32_t)timeout;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_close(svn_wc__db_t *db)
{
  apr_pool_t *scratch_pool = db->state_pool;
  apr_hash_t *roots = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;

  /* Collect all the unique WCROOT structures, and empty out DIR_DATA.  */
  for (hi = apr_hash_first(scratch_pool, db->dir_data);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_wc__db_wcroot_t *wcroot = apr_hash_this_val(hi);
      const char *local_abspath = apr_hash_this_key(hi);

      if (wcroot->sdb)
        svn_hash_sets(roots, wcroot->abspath, wcroot);

      svn_hash_sets(db->dir_data, local_abspath, NULL);
    }

  /* Run the cleanup for each WCROOT.  */
  return svn_error_trace(svn_wc__db_close_many_wcroots(roots, db->state_pool,
                                                       scratch_pool));
}


svn_error_t *
svn_wc__db_pdh_create_wcroot(svn_wc__db_wcroot_t **wcroot,
                             const char *wcroot_abspath,
                             svn_sqlite__db_t *sdb,
                             apr_int64_t wc_id,
                             int format,
                             svn_boolean_t verify_format,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  if (sdb && format == FORMAT_FROM_SDB)
    SVN_ERR(svn_sqlite__read_schema_version(&format, sdb, scratch_pool));

  /* If we construct a wcroot, then we better have a format.  */
  SVN_ERR_ASSERT(format >= 1);

  /* If this working copy is PRE-1.0, then simply bail out.  */
  if (format < 4)
    {
      return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("Working copy format of '%s' is too old (%d); "
          "please check out your working copy again"),
        svn_dirent_local_style(wcroot_abspath, scratch_pool), format);
    }

  /* If this working copy is from a future version, then bail out.  */
  if (format > SVN_WC__VERSION)
    {
      return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("This client is too old to work with the working copy at\n"
          "'%s' (format %d).\n"
          "You need to get a newer Subversion client. For more details, see\n"
          "  http://subversion.apache.org/faq.html#working-copy-format-change\n"
          ),
        svn_dirent_local_style(wcroot_abspath, scratch_pool),
        format);
    }

  /* Verify that no work items exists. If they do, then our integrity is
     suspect and, thus, we cannot upgrade this database.  */
  if (format >= SVN_WC__HAS_WORK_QUEUE &&
      format < SVN_WC__VERSION && verify_format)
    {
      svn_error_t *err = svn_wc__db_verify_no_work(sdb);
      if (err)
        {
          /* Special message for attempts to upgrade a 1.7-dev wc with
             outstanding workqueue items. */
          if (err->apr_err == SVN_ERR_WC_CLEANUP_REQUIRED
              && format < SVN_WC__VERSION && verify_format)
            err = svn_error_quick_wrap(err, _("Cleanup with an older 1.7 "
                                              "client before upgrading with "
                                              "this client"));
          return svn_error_trace(err);
        }
    }

  /* Auto-upgrade the SDB if possible.  */
  if (format < SVN_WC__VERSION && verify_format)
    {
      return svn_error_createf(SVN_ERR_WC_UPGRADE_REQUIRED, NULL,
                               _("The working copy at '%s'\nis too old "
                                 "(format %d) to work with client version "
                                 "'%s' (expects format %d). You need to "
                                 "upgrade the working copy first.\n"),
                               svn_dirent_local_style(wcroot_abspath,
                                                      scratch_pool),
                               format, SVN_VERSION, SVN_WC__VERSION);
    }

  *wcroot = apr_palloc(result_pool, sizeof(**wcroot));

  (*wcroot)->abspath = wcroot_abspath;
  (*wcroot)->sdb = sdb;
  (*wcroot)->wc_id = wc_id;
  (*wcroot)->format = format;
  /* 8 concurrent locks is probably more than a typical wc_ng based svn client
     uses. */
  (*wcroot)->owned_locks = apr_array_make(result_pool, 8,
                                          sizeof(svn_wc__db_wclock_t));
  (*wcroot)->access_cache = apr_hash_make(result_pool);

  /* SDB will be NULL for pre-NG working copies. We only need to run a
     cleanup when the SDB is present.  */
  if (sdb != NULL)
    apr_pool_cleanup_register(result_pool, *wcroot, close_wcroot,
                              apr_pool_cleanup_null);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_close_many_wcroots(apr_hash_t *roots,
                              apr_pool_t *state_pool,
                              apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, roots); hi; hi = apr_hash_next(hi))
    {
      svn_wc__db_wcroot_t *wcroot = apr_hash_this_val(hi);
      apr_status_t result;

      result = apr_pool_cleanup_run(state_pool, wcroot, close_wcroot);
      if (result != APR_SUCCESS)
        return svn_error_wrap_apr(result, NULL);
    }

  return SVN_NO_ERROR;
}


/* POOL may be NULL if the lifetime of LOCAL_ABSPATH is sufficient.  */
static const char *
compute_relpath(const svn_wc__db_wcroot_t *wcroot,
                const char *local_abspath,
                apr_pool_t *result_pool)
{
  const char *relpath = svn_dirent_is_child(wcroot->abspath, local_abspath,
                                            result_pool);
  if (relpath == NULL)
    return "";
  return relpath;
}


/* Return in *LINK_TARGET_ABSPATH the absolute path the symlink at
 * LOCAL_ABSPATH is pointing to. Perform all allocations in POOL. */
static svn_error_t *
read_link_target(const char **link_target_abspath,
                 const char *local_abspath,
                 apr_pool_t *pool)
{
  svn_string_t *link_target;
  const char *canon_link_target;

  SVN_ERR(svn_io_read_link(&link_target, local_abspath, pool));
  if (link_target->len == 0)
    return svn_error_createf(SVN_ERR_WC_NOT_SYMLINK, NULL,
                             _("The symlink at '%s' points nowhere"),
                             svn_dirent_local_style(local_abspath, pool));

  canon_link_target = svn_dirent_canonicalize(link_target->data, pool);

  /* Treat relative symlinks as relative to LOCAL_ABSPATH's parent. */
  if (!svn_dirent_is_absolute(canon_link_target))
    canon_link_target = svn_dirent_join(svn_dirent_dirname(local_abspath,
                                                           pool),
                                        canon_link_target, pool);

  /* Collapse any .. in the symlink part of the path. */
  if (svn_path_is_backpath_present(canon_link_target))
    SVN_ERR(svn_dirent_get_absolute(link_target_abspath, canon_link_target,
                                    pool));
  else
    *link_target_abspath = canon_link_target;

  return SVN_NO_ERROR;
}

/* Verify if the sqlite_stat1 table exists and if not tries to add
   this table (but ignores errors on adding the schema) */
static svn_error_t *
verify_stats_table(svn_sqlite__db_t *sdb,
                   int format,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  if (format != SVN_WC__ENSURE_STAT1_TABLE)
    return SVN_NO_ERROR;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_HAVE_STAT1_TABLE));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (!have_row)
    {
      svn_error_clear(
          svn_wc__db_install_schema_statistics(sdb, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Sqlite transaction helper for opening the db in
   svn_wc__db_wcroot_parse_local_abspath() to avoid multiple
   db operations that each obtain and release a lock */
static svn_error_t *
fetch_sdb_info(apr_int64_t *wc_id,
               int *format,
               svn_sqlite__db_t *sdb,
               apr_pool_t *scratch_pool)
{
  *wc_id = -1;
  *format = -1;

  SVN_SQLITE__WITH_LOCK4(
        svn_wc__db_util_fetch_wc_id(wc_id, sdb, scratch_pool),
        svn_sqlite__read_schema_version(format, sdb, scratch_pool),
        verify_stats_table(sdb, *format, scratch_pool),
        SVN_NO_ERROR,
        sdb);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_wcroot_parse_local_abspath(svn_wc__db_wcroot_t **wcroot,
                                      const char **local_relpath,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  const char *local_dir_abspath;
  const char *original_abspath = local_abspath;
  svn_node_kind_t kind;
  const char *build_relpath;
  svn_wc__db_wcroot_t *probe_wcroot;
  svn_wc__db_wcroot_t *found_wcroot = NULL;
  const char *scan_abspath;
  svn_sqlite__db_t *sdb = NULL;
  svn_boolean_t moved_upwards = FALSE;
  svn_boolean_t always_check = FALSE;
  int wc_format = 0;
  const char *adm_relpath;
  /* Non-NULL if WCROOT is found through a symlink: */
  const char *symlink_wcroot_abspath = NULL;

  /* ### we need more logic for finding the database (if it is located
     ### outside of the wcroot) and then managing all of that within DB.
     ### for now: play quick & dirty. */

  probe_wcroot = svn_hash_gets(db->dir_data, local_abspath);
  if (probe_wcroot != NULL)
    {
      *wcroot = probe_wcroot;

      /* We got lucky. Just return the thing BEFORE performing any I/O.  */
      /* ### validate SMODE against how we opened wcroot->sdb? and against
         ### DB->mode? (will we record per-dir mode?)  */

      /* ### for most callers, we could pass NULL for result_pool.  */
      *local_relpath = compute_relpath(probe_wcroot, local_abspath,
                                       result_pool);

      return SVN_NO_ERROR;
    }

  /* ### at some point in the future, we may need to find a way to get
     ### rid of this stat() call. it is going to happen for EVERY call
     ### into wc_db which references a file. calls for directories could
     ### get an early-exit in the hash lookup just above.  */
  SVN_ERR(get_path_kind(&kind, db, local_abspath, scratch_pool));
  if (kind != svn_node_dir)
    {
      /* If the node specified by the path is NOT present, then it cannot
         possibly be a directory containing ".svn/wc.db".

         If it is a file, then it cannot contain ".svn/wc.db".

         For both of these cases, strip the basename off of the path and
         move up one level. Keep record of what we strip, though, since
         we'll need it later to construct local_relpath.  */
      svn_dirent_split(&local_dir_abspath, &build_relpath, local_abspath,
                       scratch_pool);

      /* Is this directory in our hash?  */
      probe_wcroot = svn_hash_gets(db->dir_data, local_dir_abspath);
      if (probe_wcroot != NULL)
        {
          const char *dir_relpath;

          *wcroot = probe_wcroot;

          /* Stashed directory's local_relpath + basename. */
          dir_relpath = compute_relpath(probe_wcroot, local_dir_abspath,
                                        NULL);
          *local_relpath = svn_relpath_join(dir_relpath,
                                            build_relpath,
                                            result_pool);
          return SVN_NO_ERROR;
        }

      /* If the requested path is not on the disk, then we don't know how
         many ancestors need to be scanned until we start hitting content
         on the disk. Set ALWAYS_CHECK to keep looking for .svn/entries
         rather than bailing out after the first check.  */
      if (kind == svn_node_none)
        always_check = TRUE;

      /* Start the scanning at LOCAL_DIR_ABSPATH.  */
      local_abspath = local_dir_abspath;
    }
  else
    {
      /* Start the local_relpath empty. If *this* directory contains the
         wc.db, then relpath will be the empty string.  */
      build_relpath = "";

      /* Remember the dir containing LOCAL_ABSPATH (they're the same).  */
      local_dir_abspath = local_abspath;
    }

  /* LOCAL_ABSPATH refers to a directory at this point. At this point,
     we've determined that an associated WCROOT is NOT in the DB's hash
     table for this directory. Let's find an existing one in the ancestors,
     or create one when we find the actual wcroot.  */

  /* Assume that LOCAL_ABSPATH is a directory, and look for the SQLite
     database in the right place. If we find it... great! If not, then
     peel off some components, and try again. */

  adm_relpath = svn_wc_get_adm_dir(scratch_pool);
  while (TRUE)
    {
      svn_error_t *err;
      svn_node_kind_t adm_subdir_kind;

      const char *adm_subdir = svn_dirent_join(local_abspath, adm_relpath,
                                               scratch_pool);

      SVN_ERR(svn_io_check_path(adm_subdir, &adm_subdir_kind, scratch_pool));

      if (adm_subdir_kind == svn_node_dir)
        {
          /* We always open the database in read/write mode.  If the database
             isn't writable in the filesystem, SQLite will internally open
             it as read-only, and we'll get an error if we try to do a write
             operation.

             We could decide what to do on a per-operation basis, but since
             we're caching database handles, it make sense to be as permissive
             as the filesystem allows. */
          err = svn_wc__db_util_open_db(&sdb, local_abspath, SDB_FILE,
                                        svn_sqlite__mode_readwrite,
                                        db->exclusive, db->timeout, NULL,
                                        db->state_pool, scratch_pool);
          if (err == NULL)
            {
#ifdef SVN_DEBUG
              /* Install self-verification trigger statements. */
              err = svn_sqlite__exec_statements(sdb,
                                                STMT_VERIFICATION_TRIGGERS);
              if (err && err->apr_err == SVN_ERR_SQLITE_ERROR)
                {
                  /* Verification triggers can fail to install on old 1.7-dev
                   * formats which didn't have a NODES table yet. Ignore sqlite
                   * errors so such working copies can be upgraded. */
                  svn_error_clear(err);
                }
              else
                SVN_ERR(err);
#endif
              break;
            }
          if (err->apr_err != SVN_ERR_SQLITE_ERROR
              && !APR_STATUS_IS_ENOENT(err->apr_err))
            return svn_error_trace(err);
          svn_error_clear(err);

          /* If we have not moved upwards, then check for a wc-1 working copy.
             Since wc-1 has a .svn in every directory, and we didn't find one
             in the original directory, then we aren't looking at a wc-1.

             If the original path is not present, then we have to check on every
             iteration. The content may be the immediate parent, or possibly
             five ancetors higher. We don't test for directory presence (just
             for the presence of subdirs/files), so we don't know when we can
             stop checking ... so just check always.  */
          if (!moved_upwards || always_check)
            {
              SVN_ERR(get_old_version(&wc_format, local_abspath,
                                      scratch_pool));
              if (wc_format != 0)
                break;
            }
        }

      /* We couldn't open the SDB within the specified directory, so
         move up one more directory. */
      if (svn_dirent_is_root(local_abspath, strlen(local_abspath)))
        {
          /* Hit the root without finding a wcroot. */

          /* The wcroot could be a symlink to a directory.
           * (Issue #2557, #3987). If so, try again, this time scanning
           * for a db within the directory the symlink points to,
           * rather than within the symlink's parent directory. */
          if (kind == svn_node_symlink)
            {
              svn_node_kind_t resolved_kind;

              local_abspath = original_abspath;

              SVN_ERR(svn_io_check_resolved_path(local_abspath,
                                                 &resolved_kind,
                                                 scratch_pool));
              if (resolved_kind == svn_node_dir)
                {
                  /* Is this directory recorded in our hash?  */
                  found_wcroot = svn_hash_gets(db->dir_data, local_abspath);
                  if (found_wcroot)
                    break;

                  symlink_wcroot_abspath = local_abspath;
                  SVN_ERR(read_link_target(&local_abspath, local_abspath,
                                           scratch_pool));
try_symlink_as_dir:
                  kind = svn_node_dir;
                  moved_upwards = FALSE;
                  local_dir_abspath = local_abspath;
                  build_relpath = "";

                  continue;
                }
            }

          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("'%s' is not a working copy"),
                                   svn_dirent_local_style(original_abspath,
                                                          scratch_pool));
        }

      local_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

      moved_upwards = TRUE;
      symlink_wcroot_abspath = NULL;

      /* Is the parent directory recorded in our hash?  */
      found_wcroot = svn_hash_gets(db->dir_data, local_abspath);
      if (found_wcroot != NULL)
        break;
    }

  if (found_wcroot != NULL)
    {
      /* We found a hash table entry for an ancestor, so we stopped scanning
         since all subdirectories use the same WCROOT.  */
      *wcroot = found_wcroot;
    }
  else if (wc_format == 0)
    {
      /* We finally found the database. Construct a wcroot_t for it.  */

      apr_int64_t wc_id;
      int format;
      svn_error_t *err;

      err = fetch_sdb_info(&wc_id, &format, sdb, scratch_pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_CORRUPT)
            return svn_error_quick_wrapf(
              err, _("Missing a row in WCROOT for '%s'."),
              svn_dirent_local_style(original_abspath, scratch_pool));
          return svn_error_trace(err);
        }

      /* WCROOT.local_abspath may be NULL when the database is stored
         inside the wcroot, but we know the abspath is this directory
         (ie. where we found it).  */

      err = svn_wc__db_pdh_create_wcroot(wcroot,
                            apr_pstrdup(db->state_pool,
                                        symlink_wcroot_abspath
                                          ? symlink_wcroot_abspath
                                          : local_abspath),
                            sdb, wc_id, format,
                            db->verify_format,
                            db->state_pool, scratch_pool);
      if (err && (err->apr_err == SVN_ERR_WC_UNSUPPORTED_FORMAT ||
                  err->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED) &&
          kind == svn_node_symlink)
        {
          /* We found an unsupported WC after traversing upwards from a
           * symlink. Fall through to code below to check if the symlink
           * points at a supported WC. */
          svn_error_clear(err);
          *wcroot = NULL;
        }
      else if (err)
        {
          /* Close handle if we are not going to use it to support
             upgrading with exclusive wc locking. */
          return svn_error_compose_create(err, svn_sqlite__close(sdb));
        }
    }
  else
    {
      /* We found something that looks like a wc-1 working copy directory.
         However, if the format version is 12 and the .svn/entries file
         is only 3 bytes long, then it's a breadcrumb in a wc-ng working
         copy that's missing an .svn/wc.db, or its .svn/wc.db is corrupt. */
      if (wc_format == SVN_WC__WC_NG_VERSION /* 12 */)
        {
          apr_finfo_t info;

          /* Check attributes of .svn/entries */
          const char *admin_abspath = svn_wc__adm_child(
              local_abspath, SVN_WC__ADM_ENTRIES, scratch_pool);
          svn_error_t *err = svn_io_stat(&info, admin_abspath, APR_FINFO_SIZE,
                                         scratch_pool);

          /* If the former does not succeed, something is seriously wrong. */
          if (err)
            return svn_error_createf(
                SVN_ERR_WC_CORRUPT, err,
                _("The working copy at '%s' is corrupt."),
                svn_dirent_local_style(local_abspath, scratch_pool));
          svn_error_clear(err);

          if (3 == info.size)
            {
              /* Check existence of .svn/wc.db */
              admin_abspath = svn_wc__adm_child(local_abspath, SDB_FILE,
                                                scratch_pool);
              err = svn_io_stat(&info, admin_abspath, APR_FINFO_SIZE,
                                scratch_pool);
              if (err && APR_STATUS_IS_ENOENT(err->apr_err))
                {
                  svn_error_clear(err);
                  return svn_error_createf(
                      SVN_ERR_WC_CORRUPT, NULL,
                      _("The working copy database at '%s' is missing."),
                      svn_dirent_local_style(local_abspath, scratch_pool));
                }
              else
                /* We should never have reached this point in the code
                   if .svn/wc.db exists; therefore it's best to assume
                   it's corrupt. */
                return svn_error_createf(
                    SVN_ERR_WC_CORRUPT, err,
                    _("The working copy database at '%s' is corrupt."),
                    svn_dirent_local_style(local_abspath, scratch_pool));
            }
        }

      SVN_ERR(svn_wc__db_pdh_create_wcroot(wcroot,
                            apr_pstrdup(db->state_pool,
                                        symlink_wcroot_abspath
                                          ? symlink_wcroot_abspath
                                          : local_abspath),
                            NULL, UNKNOWN_WC_ID, wc_format,
                            db->verify_format,
                            db->state_pool, scratch_pool));
    }

  if (*wcroot)
    {
      const char *dir_relpath;

      if (symlink_wcroot_abspath)
        {
          /* The WCROOT was found through a symlink pointing at the root of
           * the WC. Cache the WCROOT under the symlink's path. */
          local_dir_abspath = symlink_wcroot_abspath;
        }

      /* The subdirectory's relpath is easily computed relative to the
         wcroot that we just found.  */
      dir_relpath = compute_relpath(*wcroot, local_dir_abspath, NULL);

      /* And the result local_relpath may include a filename.  */
      *local_relpath = svn_relpath_join(dir_relpath, build_relpath, result_pool);
    }

  if (kind == svn_node_symlink)
    {
      svn_boolean_t retry_if_dir = FALSE;
      svn_wc__db_status_t status;
      svn_boolean_t conflicted;
      svn_error_t *err;

      /* Check if the symlink is versioned or obstructs a versioned node
       * in this DB -- in that case, use this wcroot. Else, if the symlink
       * points to a directory, try to find a wcroot in that directory
       * instead. */

      if (*wcroot)
        {
          err = svn_wc__db_read_info_internal(&status, NULL, NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL, NULL,
                                              NULL, NULL, NULL, &conflicted,
                                              NULL, NULL, NULL, NULL, NULL,
                                              NULL, *wcroot, *local_relpath,
                                              scratch_pool, scratch_pool);
          if (err)
            {
              if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND
                  && !SVN_WC__ERR_IS_NOT_CURRENT_WC(err))
                return svn_error_trace(err);

              svn_error_clear(err);
              retry_if_dir = TRUE; /* The symlink is unversioned. */
            }
          else
            {
              /* The symlink is versioned, or obstructs a versioned node.
               * Ignore non-conflicted not-present/excluded nodes.
               * This allows the symlink to redirect the wcroot query to a
               * directory, regardless of 'invisible' nodes in this WC. */
              retry_if_dir = ((status == svn_wc__db_status_not_present ||
                               status == svn_wc__db_status_excluded ||
                               status == svn_wc__db_status_server_excluded)
                              && !conflicted);
            }
        }
      else
        retry_if_dir = TRUE;

      if (retry_if_dir)
        {
          svn_node_kind_t resolved_kind;

          SVN_ERR(svn_io_check_resolved_path(original_abspath,
                                             &resolved_kind,
                                             scratch_pool));
          if (resolved_kind == svn_node_dir)
            {
              symlink_wcroot_abspath = original_abspath;
              SVN_ERR(read_link_target(&local_abspath, original_abspath,
                                       scratch_pool));
              /* This handle was opened in this function but is not going
                 to be used further so close it. */
              if (sdb)
                SVN_ERR(svn_sqlite__close(sdb));
              goto try_symlink_as_dir;
            }
        }
    }

  /* We've found the appropriate WCROOT for the requested path. Stash
     it into that path's directory.  */
  svn_hash_sets(db->dir_data,
                apr_pstrdup(db->state_pool, local_dir_abspath),
                *wcroot);

  /* Did we traverse up to parent directories?  */
  if (!moved_upwards)
    {
      /* We did NOT move to a parent of the original requested directory.
         We've constructed and filled in a WCROOT for the request, so we
         are done.  */
      return SVN_NO_ERROR;
    }

  /* The WCROOT that we just found/built was for the LOCAL_ABSPATH originally
     passed into this function. We stepped *at least* one directory above that.
     We should now associate the WROOT for each parent directory that does
     not (yet) have one.  */

  scan_abspath = local_dir_abspath;

  do
    {
      const char *parent_dir = svn_dirent_dirname(scan_abspath, scratch_pool);
      svn_wc__db_wcroot_t *parent_wcroot;

      parent_wcroot = svn_hash_gets(db->dir_data, parent_dir);
      if (parent_wcroot == NULL)
        {
          svn_hash_sets(db->dir_data, apr_pstrdup(db->state_pool, parent_dir),
                        *wcroot);
        }

      /* Move up a directory, stopping when we reach the directory where
         we found/built the WCROOT.  */
      scan_abspath = parent_dir;
    }
  while (strcmp(scan_abspath, local_abspath) != 0);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_drop_root(svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *root_wcroot = svn_hash_gets(db->dir_data, local_abspath);
  apr_hash_index_t *hi;
  apr_status_t result;

  if (!root_wcroot)
    return SVN_NO_ERROR;

  if (strcmp(root_wcroot->abspath, local_abspath) != 0)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("'%s' is not a working copy root"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  for (hi = apr_hash_first(scratch_pool, db->dir_data);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_wc__db_wcroot_t *wcroot = apr_hash_this_val(hi);

      if (wcroot == root_wcroot)
        svn_hash_sets(db->dir_data, apr_hash_this_key(hi), NULL);
    }

  result = apr_pool_cleanup_run(db->state_pool, root_wcroot, close_wcroot);
  if (result != APR_SUCCESS)
    return svn_error_wrap_apr(result, NULL);

  return SVN_NO_ERROR;
}
