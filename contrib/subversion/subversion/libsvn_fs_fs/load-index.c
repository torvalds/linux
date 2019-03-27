/* load-index-cmd.c -- implements the dump-index sub-command.
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
#include "private/svn_sorts_private.h"

#include "index.h"
#include "util.h"
#include "transaction.h"

/* From the ENTRIES array of svn_fs_fs__p2l_entry_t*, sorted by offset,
 * return the first offset behind the last item. */
static apr_off_t
get_max_covered(apr_array_header_t *entries)
{
  const svn_fs_fs__p2l_entry_t *entry;
  if (entries->nelts == 0)
    return -1;

  entry = APR_ARRAY_IDX(entries, entries->nelts - 1,
                        const svn_fs_fs__p2l_entry_t *);
  return entry->offset + entry->size;
}

/* Make sure that the svn_fs_fs__p2l_entry_t* in ENTRIES are consecutive
 * and non-overlapping.  Use SCRATCH_POOL for temporaries. */
static svn_error_t *
check_all_covered(apr_array_header_t *entries,
                  apr_pool_t *scratch_pool)
{
  int i;
  apr_off_t expected = 0;
  for (i = 0; i < entries->nelts; ++i)
    {
      const svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, const svn_fs_fs__p2l_entry_t *);

      if (entry->offset < expected)
        return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                                 "Overlapping index data for offset %s",
                                 apr_psprintf(scratch_pool,
                                              "%" APR_UINT64_T_HEX_FMT,
                                              (apr_uint64_t)expected));

      if (entry->offset > expected)
        return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                                 "Missing index data for offset %s",
                                 apr_psprintf(scratch_pool,
                                              "%" APR_UINT64_T_HEX_FMT,
                                              (apr_uint64_t)expected));

      expected = entry->offset + entry->size;
    }

  return SVN_NO_ERROR;
}

/* A svn_sort__array compatible comparator function, sorting the
 * svn_fs_fs__p2l_entry_t** given in LHS, RHS by offset. */
static int
compare_p2l_entry_revision(const void *lhs,
                           const void *rhs)
{
  const svn_fs_fs__p2l_entry_t *lhs_entry
    =*(const svn_fs_fs__p2l_entry_t **)lhs;
  const svn_fs_fs__p2l_entry_t *rhs_entry
    =*(const svn_fs_fs__p2l_entry_t **)rhs;

  if (lhs_entry->offset < rhs_entry->offset)
    return -1;

  return lhs_entry->offset == rhs_entry->offset ? 0 : 1;
}

svn_error_t *
svn_fs_fs__load_index(svn_fs_t *fs,
                      svn_revnum_t revision,
                      apr_array_header_t *entries,
                      apr_pool_t *scratch_pool)
{
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  /* Check the FS format number. */
  if (! svn_fs_fs__use_log_addressing(fs))
    return svn_error_create(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL, NULL);

  /* P2L index must be written in offset order.
   * Sort ENTRIES accordingly. */
  svn_sort__array(entries, compare_p2l_entry_revision);

  /* Treat an empty array as a no-op instead error. */
  if (entries->nelts != 0)
    {
      const char *l2p_proto_index;
      const char *p2l_proto_index;
      svn_fs_fs__revision_file_t *rev_file;
      svn_error_t *err;
      apr_off_t max_covered = get_max_covered(entries);

      /* Ensure that the index data is complete. */
      SVN_ERR(check_all_covered(entries, scratch_pool));

      /* Open rev / pack file & trim indexes + footer off it. */
      SVN_ERR(svn_fs_fs__open_pack_or_rev_file_writable(&rev_file, fs,
                                                        revision, subpool,
                                                        subpool));

      /* Remove the existing index info. */
      err = svn_fs_fs__auto_read_footer(rev_file);
      if (err)
        {
          /* Even the index footer cannot be read, even less be trusted.
           * Take the range of valid data from the new index data. */
          svn_error_clear(err);
          SVN_ERR(svn_io_file_trunc(rev_file->file, max_covered,
                                    subpool));
        }
      else
        {
          /* We assume that the new index data covers all contents.
           * Error out if it doesn't.  The user can always truncate
           * the file themselves. */
          if (max_covered != rev_file->l2p_offset)
            return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                       "New index data ends at %s, old index ended at %s",
                       apr_psprintf(scratch_pool, "%" APR_UINT64_T_HEX_FMT,
                                    (apr_uint64_t)max_covered),
                       apr_psprintf(scratch_pool, "%" APR_UINT64_T_HEX_FMT,
                                    (apr_uint64_t) rev_file->l2p_offset));

          SVN_ERR(svn_io_file_trunc(rev_file->file, rev_file->l2p_offset,
                                    subpool));
        }

      /* Create proto index files for the new index data
       * (will be cleaned up automatically with iterpool). */
      SVN_ERR(svn_fs_fs__p2l_index_from_p2l_entries(&p2l_proto_index, fs,
                                                    rev_file, entries,
                                                    subpool, subpool));
      SVN_ERR(svn_fs_fs__l2p_index_from_p2l_entries(&l2p_proto_index, fs,
                                                    entries, subpool,
                                                    subpool));

      /* Combine rev data with new index data. */
      SVN_ERR(svn_fs_fs__add_index_data(fs, rev_file->file, l2p_proto_index,
                                        p2l_proto_index,
                                        rev_file->start_revision, subpool));
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}
