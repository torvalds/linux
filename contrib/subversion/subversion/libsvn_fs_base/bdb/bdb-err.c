/*
 * err.c : implementation of fs-private error functions
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



#include <stdlib.h>
#include <stdarg.h>

#include <apr_strings.h>

#include "svn_fs.h"
#include "../fs.h"
#include "../err.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"

#define SVN_WANT_BDB
#include "svn_private_config.h"


/* Return a distinguished error for any db error code we want to detect
 * programatically; otherwise return a generic error.
 */
static int
bdb_err_to_apr_err(int db_err)
{
  if (db_err == DB_LOCK_DEADLOCK)
    return SVN_ERR_FS_BERKELEY_DB_DEADLOCK;
  else
    return SVN_ERR_FS_BERKELEY_DB;
}


svn_error_t *
svn_fs_bdb__dberr(bdb_env_baton_t *bdb_baton, int db_err)
{
  svn_error_t *child_errors;

  child_errors = bdb_baton->error_info->pending_errors;
  bdb_baton->error_info->pending_errors = NULL;

  return svn_error_create(bdb_err_to_apr_err(db_err), child_errors,
                          db_strerror(db_err));
}


svn_error_t *
svn_fs_bdb__dberrf(bdb_env_baton_t *bdb_baton,
                   int db_err, const char *fmt, ...)
{
  va_list ap;
  char *msg;
  svn_error_t *err;
  svn_error_t *child_errors;

  child_errors = bdb_baton->error_info->pending_errors;
  bdb_baton->error_info->pending_errors = NULL;

  err = svn_error_create(bdb_err_to_apr_err(db_err), child_errors, NULL);

  va_start(ap, fmt);
  msg = apr_pvsprintf(err->pool, fmt, ap);
  va_end(ap);
  err->message = apr_psprintf(err->pool, "%s%s", msg, db_strerror(db_err));
  return svn_error_trace(err);
}


svn_error_t *
svn_fs_bdb__wrap_db(svn_fs_t *fs, const char *operation, int db_err)
{
  base_fs_data_t *bfd = fs->fsap_data;

  if (! db_err)
    {
      svn_error_clear(bfd->bdb->error_info->pending_errors);
      bfd->bdb->error_info->pending_errors = NULL;
      return SVN_NO_ERROR;
    }

  bfd = fs->fsap_data;
  return svn_fs_bdb__dberrf
    (bfd->bdb, db_err,
     _("Berkeley DB error for filesystem '%s' while %s:\n"),
     fs->path ? fs->path : "(none)", _(operation));
}
