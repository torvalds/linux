/*
 * wc_db_util.c :  Various util functions for wc_db(_pdh)
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

/* About this file:
   This file is meant to be a stash of fairly low-level functions used by both
   wc_db.c and wc_db_pdh.c.  In breaking stuff out of the monolithic wc_db.c,
   I have discovered that some utility functions are used by bits in both
   files.  Rather than shoehorn those functions into one file or the other, or
   create circular dependencies between the files, I felt a third file, with
   a well-defined scope, would be sensible.  History will judge its effect.

   The goal of it file is simple: just execute SQLite statements.  That is,
   functions in this file should have no knowledge of pdh's or db's, and
   should just operate on the raw sdb object.  If a function requires more
   information than that, it shouldn't be in here.  -hkw
 */

#define SVN_WC__I_AM_WC_DB

#include "svn_dirent_uri.h"
#include "private/svn_sqlite.h"

#include "wc.h"
#include "adm_files.h"
#include "wc_db_private.h"
#include "wc-queries.h"

#include "svn_private_config.h"

WC_QUERIES_SQL_DECLARE_STATEMENTS(statements);



/* */
svn_error_t *
svn_wc__db_util_fetch_wc_id(apr_int64_t *wc_id,
                            svn_sqlite__db_t *sdb,
                            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  /* ### cheat. we know there is just one WORKING_COPY row, and it has a
     ### NULL value for local_abspath. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_WCROOT_NULL));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                             _("Missing a row in WCROOT."));

  SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 0));
  *wc_id = svn_sqlite__column_int64(stmt, 0);

  return svn_error_trace(svn_sqlite__reset(stmt));
}




/* An SQLite application defined function that allows SQL queries to
   use "relpath_depth(local_relpath)".  */
static svn_error_t *
relpath_depth_sqlite(svn_sqlite__context_t *sctx,
                     int argc,
                     svn_sqlite__value_t *values[],
                     void *baton)
{
  const char *path = NULL;
  apr_int64_t depth;

  if (argc == 1 && svn_sqlite__value_type(values[0]) == SVN_SQLITE__TEXT)
    path = svn_sqlite__value_text(values[0]);
  if (!path)
    {
      svn_sqlite__result_null(sctx);
      return SVN_NO_ERROR;
    }

  depth = *path ? 1 : 0;
  while (*path)
    {
      if (*path == '/')
        ++depth;
      ++path;
    }
  svn_sqlite__result_int64(sctx, depth);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_util_open_db(svn_sqlite__db_t **sdb,
                        const char *dir_abspath,
                        const char *sdb_fname,
                        svn_sqlite__mode_t smode,
                        svn_boolean_t exclusive,
                        apr_int32_t timeout,
                        const char *const *my_statements,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *sdb_abspath = svn_wc__adm_child(dir_abspath, sdb_fname,
                                              scratch_pool);

  if (smode != svn_sqlite__mode_rwcreate)
    {
      svn_node_kind_t kind;

      /* A file stat is much cheaper than a failed database open handled
         by SQLite. */
      SVN_ERR(svn_io_check_path(sdb_abspath, &kind, scratch_pool));

      if (kind != svn_node_file)
        return svn_error_createf(APR_ENOENT, NULL,
                                 _("Working copy database '%s' not found"),
                                 svn_dirent_local_style(sdb_abspath,
                                                        scratch_pool));
    }

  SVN_ERR(svn_sqlite__open(sdb, sdb_abspath, smode,
                           my_statements ? my_statements : statements,
                           0, NULL, timeout, result_pool, scratch_pool));

  if (exclusive)
    SVN_ERR(svn_sqlite__exec_statements(*sdb, STMT_PRAGMA_LOCKING_MODE));

  SVN_ERR(svn_sqlite__create_scalar_function(*sdb, "relpath_depth", 1,
                                             TRUE /* deterministic */,
                                             relpath_depth_sqlite, NULL));

  return SVN_NO_ERROR;
}

