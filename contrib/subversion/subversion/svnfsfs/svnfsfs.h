/*
 * svnfsfs.h:  shared stuff in the command line program
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


#ifndef SVNFSFS_H
#define SVNFSFS_H

/*** Includes. ***/

#include "svn_opt.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Command dispatch. ***/

/* Baton for passing option/argument state to a subcommand function. */
typedef struct svnfsfs__opt_state
{
  const char *repository_path;
  svn_opt_revision_t start_revision, end_revision;  /* -r X[:Y] */
  svn_boolean_t help;                               /* --help or -? */
  svn_boolean_t version;                            /* --version */
  svn_boolean_t quiet;                              /* --quiet */
  apr_uint64_t memory_cache_size;                   /* --memory-cache-size M */
} svnfsfs__opt_state;

/* Declare all the command procedures */
svn_opt_subcommand_t
  subcommand__help,
  subcommand__dump_index,
  subcommand__load_index,
  subcommand__stats;


/* Check that the filesystem at PATH is an FSFS repository and then open it.
 * Return the filesystem in *FS, allocated in POOL. */
svn_error_t *
open_fs(svn_fs_t **fs,
        const char *path,
        apr_pool_t *pool);

/* Our cancellation callback. */
extern svn_cancel_func_t check_cancel;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVNFSFS_H */
