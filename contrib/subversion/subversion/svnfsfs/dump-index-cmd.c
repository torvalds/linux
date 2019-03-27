/* dump-index-cmd.c -- implements the dump-index sub-command.
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

#define APR_WANT_BYTEFUNC

#include "svn_dirent_uri.h"
#include "svn_pools.h"
#include "private/svn_fs_fs_private.h"

#include "svnfsfs.h"

/* Return the 8 digit hex string for FNVV1, allocated in POOL.
 */
static const char *
fnv1_to_string(apr_uint32_t fnv1,
               apr_pool_t *pool)
{
  /* Construct a checksum object containing FNV1. */
  svn_checksum_t checksum = { NULL, svn_checksum_fnv1a_32 };
  apr_uint32_t digest = htonl(fnv1);
  checksum.digest = (const unsigned char *)&digest;

  /* Convert the digest to hex. */
  return svn_checksum_to_cstring_display(&checksum, pool);
}

/* Map svn_fs_fs__p2l_entry_t.type to C string. */
static const char *item_type_str[]
  = {"none ", "frep ", "drep ", "fprop", "dprop", "node ", "chgs ", "rep  "};

/* Implements svn_fs_fs__dump_index_func_t as printing one table row
 * containing the fields of ENTRY to the console.
 */
static svn_error_t *
dump_index_entry(const svn_fs_fs__p2l_entry_t *entry,
                 void *baton,
                 apr_pool_t *scratch_pool)
{
  const char *type_str
    = entry->type < (sizeof(item_type_str) / sizeof(item_type_str[0]))
    ? item_type_str[entry->type]
    : "???";

  printf("%12" APR_UINT64_T_HEX_FMT " %12" APR_UINT64_T_HEX_FMT
         " %s %9ld %8" APR_UINT64_T_FMT " %s\n",
         (apr_uint64_t)entry->offset, (apr_uint64_t)entry->size,
         type_str, entry->item.revision, entry->item.number,
         fnv1_to_string(entry->fnv1_checksum, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the repository at PATH beginning with revision START_REVISION and
 * return the result in *FS.  Allocate caches with MEMSIZE bytes total
 * capacity.  Use POOL for non-cache allocations.
 */
static svn_error_t *
dump_index(const char *path,
           svn_revnum_t revision,
           apr_pool_t *pool)
{
  svn_fs_t *fs;

  /* Check repository type and open it. */
  SVN_ERR(open_fs(&fs, path, pool));

  /* Write header line. */
  printf("       Start       Length Type   Revision     Item Checksum\n");

  /* Dump the whole index contents */
  SVN_ERR(svn_fs_fs__dump_index(fs, revision, dump_index_entry, NULL,
                                check_cancel, NULL, pool));

  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
svn_error_t *
subcommand__dump_index(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnfsfs__opt_state *opt_state = baton;

  SVN_ERR(dump_index(opt_state->repository_path,
                     opt_state->start_revision.value.number, pool));

  return SVN_NO_ERROR;
}
