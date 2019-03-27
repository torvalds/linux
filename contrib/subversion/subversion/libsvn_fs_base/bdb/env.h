/* env.h : managing the BDB environment
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

#ifndef SVN_LIBSVN_FS_BDB_ENV_H
#define SVN_LIBSVN_FS_BDB_ENV_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include <apr_pools.h>
#include <apr_file_io.h>

#include "bdb_compat.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The name of the Berkeley DB config file.  */
#define BDB_CONFIG_FILE "DB_CONFIG"

/* Prefix string for BDB errors. */
#define BDB_ERRPFX_STRING "svn (bdb): "


/* Opaque descriptor of an open BDB environment. */
typedef struct bdb_env_t bdb_env_t;


/* Thread-specific error info related to the bdb_env_t. */
typedef struct bdb_error_info_t
{
  /* We hold the extended info here until the Berkeley DB function returns.
     It usually returns an error code, triggering the collection and
     wrapping of the additional errors stored here.

     Note: In some circumstances BDB will call the error function and not
     go on to return an error code, so the caller must always check whether
     pending_errors is non-NULL to avoid leaking errors.  This behaviour
     has been seen when running recovery on a repository upgraded to 4.3
     that still has old 4.2 log files present, a typical error string is
     "Skipping log file db/log.0000000002: historic log version 8" */
  svn_error_t *pending_errors;

  /* We permitted clients of our library to install a Berkeley BDB errcall.
     Since we now use the errcall ourselves, we must store and invoke a user
     errcall, to maintain our API guarantees. */
  void (*user_callback)(const char *errpfx, char *msg);

  /* The reference count.  It counts the number of bdb_env_baton_t
     instances that refer to this object. */
  unsigned refcount;

} bdb_error_info_t;


/* The Berkeley DB environment baton. */
typedef struct bdb_env_baton_t
{
  /* The Berkeley DB environment. This pointer must be identical to
     the one in the bdb_env_t. */
  DB_ENV *env;

  /* The (opaque) cached environment descriptor. */
  bdb_env_t *bdb;

  /* The error info related to this baton. */
  bdb_error_info_t *error_info;
} bdb_env_baton_t;



/* Flag combination for opening a shared BDB environment. */
#define SVN_BDB_STANDARD_ENV_FLAGS (DB_CREATE       \
                                    | DB_INIT_LOCK  \
                                    | DB_INIT_LOG   \
                                    | DB_INIT_MPOOL \
                                    | DB_INIT_TXN   \
                                    | SVN_BDB_AUTO_RECOVER)

/* Flag combination for opening a private BDB environment. */
#define SVN_BDB_PRIVATE_ENV_FLAGS (DB_CREATE       \
                                   | DB_INIT_LOG   \
                                   | DB_INIT_MPOOL \
                                   | DB_INIT_TXN   \
                                   | DB_PRIVATE)


/* Iniitalize the BDB back-end's private stuff. */
svn_error_t *svn_fs_bdb__init(apr_pool_t* pool);


/* Allocate the Berkeley DB descriptor BDB and open the environment.
 *
 * Allocate *BDBP from POOL and open (*BDBP)->env in PATH, using FLAGS
 * and MODE.  If applicable, set the BDB_AUTO_COMMIT flag for this
 * environment.
 *
 * Use POOL for temporary allocation.
 *
 * Note: This function may return a bdb_env_baton_t object that refers
 *       to a previously opened environment.  If FLAGS contains
 *       DB_PRIVATE and the environment is already open, the function
 *       will fail (this isn't a problem in practice, because a caller
 *       should obtain an exclusive lock on the repository before
 *       opening the environment).
 */

svn_error_t *svn_fs_bdb__open(bdb_env_baton_t **bdb_batonp,
                              const char *path,
                              u_int32_t flags, int mode,
                              apr_pool_t *pool);

/* Close the Berkeley DB descriptor BDB.
 *
 * Note: This function might not actually close the environment if it
 *       has been opened more than once.
 */
svn_error_t *svn_fs_bdb__close(bdb_env_baton_t *bdb_baton);


/* Get the panic state of the open BDB environment. */
svn_boolean_t svn_fs_bdb__get_panic(bdb_env_baton_t *bdb_baton);

/* Set the panic flag on the open BDB environment. */
void svn_fs_bdb__set_panic(bdb_env_baton_t *bdb_baton);


/* Remove the Berkeley DB environment at PATH.
 *
 * Use POOL for temporary allocation.
 */
svn_error_t *svn_fs_bdb__remove(const char *path, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_BDB_ENV_H */
