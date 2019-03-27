/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_mergeinfo_private.h
 * @brief Subversion-internal mergeinfo APIs.
 */

#ifndef SVN_MERGEINFO_PRIVATE_H
#define SVN_MERGEINFO_PRIVATE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_mergeinfo.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Set inheritability of all ranges in RANGELIST to INHERITABLE.
   If RANGELIST is NULL do nothing. */
void
svn_rangelist__set_inheritance(svn_rangelist_t *rangelist,
                               svn_boolean_t inheritable);

/* Parse a rangelist from the string STR. Set *RANGELIST to the result,
 * allocated in RESULT_POOL. Return an error if the rangelist is not
 * well-formed (for example, if it contains invalid characters or if
 * R1 >= R2 in a "R1-R2" range element).
 *
 * Unlike svn_mergeinfo_parse(), this does not sort the ranges into order
 * or combine adjacent and overlapping ranges.
 *
 * The compaction can be done with svn_rangelist__canonicalize().
 */
svn_error_t *
svn_rangelist__parse(svn_rangelist_t **rangelist,
                     const char *str,
                     apr_pool_t *result_pool);

/* Return TRUE, if all ranges in RANGELIST are in ascending order and do
* not overlap and are not adjacent.
*
* If this returns FALSE, you probaly want to call
* svn_rangelist__canonicalize().
*/
svn_boolean_t
svn_rangelist__is_canonical(const svn_rangelist_t *rangelist);

/** Canonicalize the @a rangelist: sort the ranges, and combine adjacent or
 * overlapping ranges into single ranges where possible.
 *
 * If overlapping ranges have different inheritability, return an error.
 *
 * Modify @a rangelist in place. Use @a scratch_pool for temporary
 * allocations.
 */
svn_error_t *
svn_rangelist__canonicalize(svn_rangelist_t *rangelist,
                            apr_pool_t *scratch_pool);

/** Canonicalize the revision range lists in the @a mergeinfo.
 *
 * Modify @a mergeinfo in place. Use @a scratch_pool for temporary
 * allocations.
 */
svn_error_t *
svn_mergeinfo__canonicalize_ranges(svn_mergeinfo_t mergeinfo,
                                   apr_pool_t *scratch_pool);

/* Set inheritability of all rangelists in MERGEINFO to INHERITABLE.
   If MERGEINFO is NULL do nothing.  If a rangelist in MERGEINFO is
   NULL leave it alone. */
void
svn_mergeinfo__set_inheritance(svn_mergeinfo_t mergeinfo,
                               svn_boolean_t inheritable,
                               apr_pool_t *scratch_pool);

/* Return whether INFO1 and INFO2 are equal in *IS_EQUAL.

   CONSIDER_INHERITANCE determines how the rangelists in the two
   hashes are compared for equality.  If CONSIDER_INHERITANCE is FALSE,
   then the start and end revisions of the svn_merge_range_t's being
   compared are the only factors considered when determining equality.

     e.g. '/trunk: 1,3-4*,5' == '/trunk: 1,3-5'

   If CONSIDER_INHERITANCE is TRUE, then the inheritability of the
   svn_merge_range_t's is also considered and must be the same for two
   otherwise identical ranges to be judged equal.

     e.g. '/trunk: 1,3-4*,5' != '/trunk: 1,3-5'
          '/trunk: 1,3-4*,5' == '/trunk: 1,3-4*,5'
          '/trunk: 1,3-4,5'  == '/trunk: 1,3-4,5'

   Use POOL for temporary allocations. */
svn_error_t *
svn_mergeinfo__equals(svn_boolean_t *is_equal,
                      svn_mergeinfo_t info1,
                      svn_mergeinfo_t info2,
                      svn_boolean_t consider_inheritance,
                      apr_pool_t *pool);

/* Remove all paths from MERGEINFO which map to empty rangelists.

   Return TRUE if any paths were removed and FALSE if none were
   removed or MERGEINFO is NULL. */
svn_boolean_t
svn_mergeinfo__remove_empty_rangelists(svn_mergeinfo_t mergeinfo,
                                       apr_pool_t *scratch_pool);

/* Make a shallow (ie, mergeinfos are not duped, or altered at all;
   keys share storage) copy of IN_CATALOG in *OUT_CATALOG, removing
   PREFIX_PATH from the beginning of each key in the catalog.
   PREFIX_PATH and the keys of IN_CATALOG are absolute 'fspaths',
   starting with '/'.  It is illegal for any key to not start with
   PREFIX_PATH.  The keys of *OUT_CATALOG are relpaths.  The new hash
   and temporary values are allocated in POOL.  (This is useful for
   making the return value from svn_ra_get_mergeinfo relative to the
   session root, say.) */
svn_error_t *
svn_mergeinfo__remove_prefix_from_catalog(svn_mergeinfo_catalog_t *out_catalog,
                                          svn_mergeinfo_catalog_t in_catalog,
                                          const char *prefix_path,
                                          apr_pool_t *pool);

/* Make a shallow (ie, mergeinfos are not duped, or altered at all;
   though keys are reallocated) copy of IN_CATALOG in *OUT_CATALOG,
   adding PREFIX_PATH to the beginning of each key in the catalog.

   The new hash keys are allocated in RESULT_POOL.  SCRATCH_POOL
   is used for any temporary allocations.*/
svn_error_t *
svn_mergeinfo__add_prefix_to_catalog(svn_mergeinfo_catalog_t *out_catalog,
                                     svn_mergeinfo_catalog_t in_catalog,
                                     const char *prefix_path,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/* Set *OUT_MERGEINFO to a shallow copy of MERGEINFO with the relpath
   SUFFIX_RELPATH added to the end of each key path.

   Allocate *OUT_MERGEINFO and the new keys in RESULT_POOL.  Use
   SCRATCH_POOL for any temporary allocations. */
svn_error_t *
svn_mergeinfo__add_suffix_to_mergeinfo(svn_mergeinfo_t *out_mergeinfo,
                                       svn_mergeinfo_t mergeinfo,
                                       const char *suffix_relpath,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool);

/* Create a string representation of CATALOG in *OUTPUT, allocated in POOL.
   The hash keys of CATALOG and the merge source paths of each key's mergeinfo
   are represented in sorted order as per svn_sort_compare_items_as_paths.
   If CATALOG is empty or NULL then *OUTPUT->DATA is set to "\n".  If SVN_DEBUG
   is true, then a NULL or empty CATALOG causes *OUTPUT to be set to an
   appropriate newline terminated string.  If KEY_PREFIX is not NULL then
   prepend KEY_PREFIX to each key (path) in *OUTPUT.  if VAL_PREFIX is not
   NULL then prepend VAL_PREFIX to each merge source:rangelist line in
   *OUTPUT.

   Any relative merge source paths in the mergeinfo in CATALOG are converted
   to absolute paths in *OUTPUT. */
svn_error_t *
svn_mergeinfo__catalog_to_formatted_string(svn_string_t **output,
                                           svn_mergeinfo_catalog_t catalog,
                                           const char *key_prefix,
                                           const char *val_prefix,
                                           apr_pool_t *pool);

/* Set *YOUNGEST_REV and *OLDEST_REV to the youngest and oldest revisions
   found in the rangelists within MERGEINFO.  Note that *OLDEST_REV is
   exclusive and *YOUNGEST_REV is inclusive.  If MERGEINFO is NULL or empty
   set *YOUNGEST_REV and *OLDEST_REV to SVN_INVALID_REVNUM. */
svn_error_t *
svn_mergeinfo__get_range_endpoints(svn_revnum_t *youngest_rev,
                                   svn_revnum_t *oldest_rev,
                                   svn_mergeinfo_t mergeinfo,
                                   apr_pool_t *pool);

/* Set *FILTERED_MERGEINFO to a deep copy of MERGEINFO, allocated in
   RESULT_POOL, less any revision ranges that fall outside of the range
   OLDEST_REV:YOUNGEST_REV (exclusive:inclusive) if INCLUDE_RANGE is true,
   or less any ranges within OLDEST_REV:YOUNGEST_REV if INCLUDE_RANGE
   is false.  If all the rangelists mapped to a given path are filtered
   then filter that path as well.  If all paths are filtered or MERGEINFO is
   empty or NULL then *FILTERED_MERGEINFO is set to an empty hash.

   Use SCRATCH_POOL for any temporary allocations. */
svn_error_t *
svn_mergeinfo__filter_mergeinfo_by_ranges(svn_mergeinfo_t *filtered_mergeinfo,
                                          svn_mergeinfo_t mergeinfo,
                                          svn_revnum_t youngest_rev,
                                          svn_revnum_t oldest_rev,
                                          svn_boolean_t include_range,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool);

/* Filter each mergeinfo in CATALOG as per
   svn_mergeinfo__filter_mergeinfo_by_ranges() and put a deep copy of the
   result in *FILTERED_CATALOG, allocated in RESULT_POOL.  If any mergeinfo
   is filtered to an empty hash then filter that path/mergeinfo as well.
   If all mergeinfo is filtered or CATALOG is NULL then set *FILTERED_CATALOG
   to an empty hash.

   Use SCRATCH_POOL for any temporary allocations. */
svn_error_t*
svn_mergeinfo__filter_catalog_by_ranges(
  svn_mergeinfo_catalog_t *filtered_catalog,
  svn_mergeinfo_catalog_t catalog,
  svn_revnum_t youngest_rev,
  svn_revnum_t oldest_rev,
  svn_boolean_t include_range,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/* If MERGEINFO is non-inheritable return TRUE, return FALSE otherwise.
   MERGEINFO may be NULL or empty. */
svn_boolean_t
svn_mergeinfo__is_noninheritable(svn_mergeinfo_t mergeinfo,
                                 apr_pool_t *scratch_pool);

/* Return a rangelist with one svn_merge_range_t * element defined by START,
   END, and INHERITABLE.  The rangelist and its contents are allocated in
   RESULT_POOL. */
svn_rangelist_t *
svn_rangelist__initialize(svn_revnum_t start,
                          svn_revnum_t end,
                          svn_boolean_t inheritable,
                          apr_pool_t *result_pool);

/* Adjust in-place MERGEINFO's rangelists by OFFSET.  If OFFSET is negative
   and would adjust any part of MERGEINFO's source revisions to 0 or less,
   then those revisions are dropped.  If all the source revisions for a merge
   source path are dropped, then the path itself is dropped.  If all merge
   source paths are dropped, then *ADJUSTED_MERGEINFO is set to an empty
   hash.  *ADJUSTED_MERGEINFO is allocated in RESULT_POOL.  SCRATCH_POOL is
   used for any temporary allocations. */
svn_error_t *
svn_mergeinfo__adjust_mergeinfo_rangelists(svn_mergeinfo_t *adjusted_mergeinfo,
                                           svn_mergeinfo_t mergeinfo,
                                           svn_revnum_t offset,
                                           apr_pool_t *result_pool,
                                           apr_pool_t *scratch_pool);

/* Translates an array SEGMENTS (of svn_location_segment_t *), like the one
   returned from svn_client__repos_location_segments, into a mergeinfo
   *MERGEINFO_P, allocated in POOL.

   Note: A svn_location_segment_t segment may legitimately describe only revision 0,
   but there is no way to describe that using svn_mergeinfo_t.  Any such
   segment in SEGMENTS are ignored. */
svn_error_t *
svn_mergeinfo__mergeinfo_from_segments(svn_mergeinfo_t *mergeinfo_p,
                                       const apr_array_header_t *segments,
                                       apr_pool_t *pool);

/* Merge every rangelist in MERGEINFO into the given MERGED_RANGELIST,
 * ignoring the source paths of MERGEINFO. MERGED_RANGELIST may
 * initially be empty. New elements added to RANGELIST are allocated in
 * RESULT_POOL. See svn_rangelist_merge2() for details of inheritability
 * etc. */
svn_error_t *
svn_rangelist__merge_many(svn_rangelist_t *merged_rangelist,
                          svn_mergeinfo_t mergeinfo,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_PRIVATE_H */
