/* dump-index.c -- implements the svn_fs_fs__dump_index private API
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
#include "private/svn_fs_fs_private.h"

#include "index.h"
#include "rev_file.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

svn_error_t *
svn_fs_fs__dump_index(svn_fs_t *fs,
                      svn_revnum_t revision,
                      svn_fs_fs__dump_index_func_t callback_func,
                      void *callback_baton,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_fs__revision_file_t *rev_file;
  int i;
  apr_off_t offset, max_offset;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Check the FS format. */
  if (! svn_fs_fs__use_log_addressing(fs))
    return svn_error_create(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL, NULL);

  /* Revision & index file access object. */
  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, fs, revision,
                                           scratch_pool, iterpool));

  /* Offset range to cover. */
  SVN_ERR(svn_fs_fs__p2l_get_max_offset(&max_offset, fs, rev_file, revision,
                                        scratch_pool));

  /* Walk through all P2L index entries in offset order. */
  for (offset = 0; offset < max_offset; )
    {
      apr_array_header_t *entries;

      /* Read entries for the next block.  There will be no overlaps since
       * we start at the first offset not covered. */
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, fs, rev_file, revision,
                                          offset, ffd->p2l_page_size,
                                          iterpool, iterpool));

      /* Print entries for this block, one line per entry. */
      for (i = 0; i < entries->nelts && offset < max_offset; ++i)
        {
          const svn_fs_fs__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, const svn_fs_fs__p2l_entry_t);
          offset = entry->offset + entry->size;

          /* Cancellation support */
          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          /* Invoke processing callback. */
          SVN_ERR(callback_func(entry, callback_baton, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
