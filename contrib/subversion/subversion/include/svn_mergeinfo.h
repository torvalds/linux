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
 * @file svn_mergeinfo.h
 * @brief mergeinfo handling and processing
 */


#ifndef SVN_MERGEINFO_H
#define SVN_MERGEINFO_H

#include <apr_pools.h>
#include <apr_tables.h>  /* for apr_array_header_t */
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_string.h"  /* for svn_string_t */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Overview of the @c SVN_PROP_MERGEINFO property.
 *
 * Merge history is stored in the @c SVN_PROP_MERGEINFO property of files
 * and directories.  The @c SVN_PROP_MERGEINFO property on a path stores the
 * complete list of changes merged to that path, either directly or via the
 * path's parent, grand-parent, etc..  A path may have empty mergeinfo which
 * means that nothing has been merged to that path or all previous merges
 * to the path were reversed.  Note that a path may have no mergeinfo, this
 * is not the same as empty mergeinfo.
 *
 * Every path in a tree may have @c SVN_PROP_MERGEINFO set, but if the
 * @c SVN_PROP_MERGEINFO for a path is equivalent to the
 * @c SVN_PROP_MERGEINFO for its parent, then the @c SVN_PROP_MERGEINFO on
 * the path will 'elide' (be removed) from the path as a post step to any
 * merge.  If a path's parent does not have any @c SVN_PROP_MERGEINFO set,
 * the path's mergeinfo can elide to its nearest grand-parent,
 * great-grand-parent, etc. that has equivalent @c SVN_PROP_MERGEINFO set
 * on it.
 *
 * If a path has no @c SVN_PROP_MERGEINFO of its own, it inherits mergeinfo
 * from its nearest parent that has @c SVN_PROP_MERGEINFO set.  The
 * exception to this is @c SVN_PROP_MERGEINFO with non-inheritable revision
 * ranges.  These non-inheritable ranges apply only to the path which they
 * are set on.
 *
 * Due to Subversion's allowance for mixed revision working copies, both
 * elision and inheritance within the working copy presume the path
 * between a path and its nearest parent with mergeinfo is at the same
 * working revision.  If this is not the case then neither inheritance nor
 * elision can occur.
 *
 * The value of the @c SVN_PROP_MERGEINFO property is either an empty string
 * (representing empty mergeinfo) or a non-empty string consisting of
 * a path, a colon, and comma separated revision list, containing one or more
 * revision or revision ranges. Revision range start and end points are
 * separated by "-".  Revisions and revision ranges may have the optional
 * @c SVN_MERGEINFO_NONINHERITABLE_STR suffix to signify a non-inheritable
 * revision/revision range.
 *
 * @c SVN_PROP_MERGEINFO Value Grammar:
 *
 *   Token             Definition
 *   -----             ----------
 *   revisionrange     REVISION1 "-" REVISION2
 *   revisioneelement  (revisionrange | REVISION)"*"?
 *   rangelist         revisioneelement (COMMA revisioneelement)*
 *   revisionline      PATHNAME COLON rangelist
 *   top               "" | (revisionline (NEWLINE revisionline))*
 *
 * The PATHNAME is the source of a merge and the rangelist the revision(s)
 * merged to the path @c SVN_PROP_MERGEINFO is set on directly or indirectly
 * via inheritance.  PATHNAME must always exist at the specified rangelist
 * and thus a single merge may result in multiple revisionlines if the source
 * was renamed.
 *
 * Rangelists must be sorted from lowest to highest revision and cannot
 * contain overlapping revisionlistelements.  REVISION1 must be less than
 * REVISION2.  Consecutive single revisions that can be represented by a
 * revisionrange are allowed however (e.g. '5,6,7,8,9-12' or '5-12' are
 * both acceptable).
 */

/** Suffix for SVN_PROP_MERGEINFO revision ranges indicating a given
   range is non-inheritable. */
#define SVN_MERGEINFO_NONINHERITABLE_STR "*"

/** Terminology for data structures that contain mergeinfo.
 *
 * Subversion commonly uses several data structures to represent
 * mergeinfo in RAM:
 *
 * (a) Strings (@c svn_string_t *) containing "unparsed mergeinfo".
 *
 * (b) @c svn_rangelist_t, called a "rangelist".
 *
 * (c) @c svn_mergeinfo_t, called "mergeinfo".
 *
 * (d) @c svn_mergeinfo_catalog_t, called a "mergeinfo catalog".
 *
 * Both @c svn_mergeinfo_t and @c svn_mergeinfo_catalog_t are just
 * typedefs for @c apr_hash_t *; there is no static type-checking, and
 * you still use standard @c apr_hash_t functions to interact with
 * them.
 */

/** An array of non-overlapping merge ranges (@c svn_merge_range_t *),
 * sorted as said by @c svn_sort_compare_ranges().  An empty range list is
 * represented by an empty array.
 *
 * Unless specifically noted otherwise, all APIs require rangelists that
 * describe only forward ranges, i.e. the range's start revision is less
 * than its end revision. */
typedef apr_array_header_t svn_rangelist_t;

/** A hash mapping merge source paths to non-empty rangelist arrays.
 *
 * The keys are (@c const char *) absolute paths from the repository root,
 * starting with slashes. A @c NULL hash represents no mergeinfo and an
 * empty hash represents empty mergeinfo. */
typedef apr_hash_t *svn_mergeinfo_t;

/** A hash mapping paths (@c const char *) to @c svn_mergeinfo_t.
 *
 * @note While the keys of #svn_mergeinfo_t are always absolute from the
 * repository root, the keys of a catalog may be relative to something
 * else, such as an RA session root.
 * */
typedef apr_hash_t *svn_mergeinfo_catalog_t;

/** Parse the mergeinfo from @a input into @a *mergeinfo.  If no
 * mergeinfo is available, return an empty mergeinfo (never @c NULL).
 *
 * If @a input is not a grammatically correct @c SVN_PROP_MERGEINFO
 * property, contains overlapping revision ranges of differing
 * inheritability, or revision ranges with a start revision greater
 * than or equal to its end revision, or contains paths mapped to empty
 * revision ranges, then return  @c SVN_ERR_MERGEINFO_PARSE_ERROR.
 * Unordered revision ranges are  allowed, but will be sorted when
 * placed into @a *mergeinfo.  Overlapping revision ranges of the same
 * inheritability are also allowed, but will be combined into a single
 * range when placed into @a *mergeinfo.
 *
 * @a input may contain relative merge source paths, but these are
 * converted to absolute paths in @a *mergeinfo.
 *
 * Allocate the result deeply in @a pool. Also perform temporary
 * allocations in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_mergeinfo_parse(svn_mergeinfo_t *mergeinfo, const char *input,
                    apr_pool_t *pool);

/** Calculate the delta between two mergeinfos, @a mergefrom and @a mergeto
 * (either or both of which may be @c NULL meaning an empty mergeinfo).
 * Place the result in @a *deleted and @a *added (neither output argument
 * may be @c NULL), both allocated in @a result_pool.  The resulting
 * @a *deleted and @a *added will not be null.
 *
 * @a consider_inheritance determines how the rangelists in the two
 * hashes are compared for equality.  If @a consider_inheritance is FALSE,
 * then the start and end revisions of the @c svn_merge_range_t's being
 * compared are the only factors considered when determining equality.
 *
 *  e.g. '/trunk: 1,3-4*,5' == '/trunk: 1,3-5'
 *
 * If @a consider_inheritance is TRUE, then the inheritability of the
 * @c svn_merge_range_t's is also considered and must be the same for two
 * otherwise identical ranges to be judged equal.
 *
 *  e.g. '/trunk: 1,3-4*,5' != '/trunk: 1,3-5'
 *       '/trunk: 1,3-4*,5' == '/trunk: 1,3-4*,5'
 *       '/trunk: 1,3-4,5'  == '/trunk: 1,3-4,5'
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_mergeinfo_diff2(svn_mergeinfo_t *deleted, svn_mergeinfo_t *added,
                    svn_mergeinfo_t mergefrom, svn_mergeinfo_t mergeto,
                    svn_boolean_t consider_inheritance,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

/** Similar to svn_mergeinfo_diff2(), but users only one pool.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_mergeinfo_diff(svn_mergeinfo_t *deleted, svn_mergeinfo_t *added,
                   svn_mergeinfo_t mergefrom, svn_mergeinfo_t mergeto,
                   svn_boolean_t consider_inheritance,
                   apr_pool_t *pool);

/** Merge a shallow copy of one mergeinfo, @a changes, into another mergeinfo
 * @a mergeinfo.
 *
 * Rangelists for merge source paths common to @a changes and @a mergeinfo may
 * result in new rangelists; these are allocated in @a result_pool.
 * Temporary allocations are made in @a scratch_pool.
 *
 * When intersecting rangelists for a path are merged, the inheritability of
 * the resulting svn_merge_range_t depends on the inheritability of the
 * operands.  If two non-inheritable ranges are merged the result is always
 * non-inheritable, in all other cases the resulting range is inheritable.
 *
 *  e.g. '/A: 1,3-4'  merged with '/A: 1,3,4*,5' --> '/A: 1,3-5'
 *       '/A: 1,3-4*' merged with '/A: 1,3,4*,5' --> '/A: 1,3,4*,5'
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_mergeinfo_merge2(svn_mergeinfo_t mergeinfo,
                     svn_mergeinfo_t changes,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);

/** Like svn_mergeinfo_merge2, but uses only one pool.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_mergeinfo_merge(svn_mergeinfo_t mergeinfo,
                    svn_mergeinfo_t changes,
                    apr_pool_t *pool);

/** Combine one mergeinfo catalog, @a changes_catalog, into another mergeinfo
 * catalog @a mergeinfo_catalog.  If both catalogs have mergeinfo for the same
 * key, use svn_mergeinfo_merge() to combine the mergeinfos.
 *
 * Additions to @a mergeinfo_catalog are deep copies allocated in
 * @a result_pool.  Temporary allocations are made in @a scratch_pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_mergeinfo_catalog_merge(svn_mergeinfo_catalog_t mergeinfo_catalog,
                            svn_mergeinfo_catalog_t changes_catalog,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/** Like svn_mergeinfo_remove2, but always considers inheritance.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_mergeinfo_remove(svn_mergeinfo_t *mergeinfo, svn_mergeinfo_t eraser,
                     svn_mergeinfo_t whiteboard, apr_pool_t *pool);

/** Removes @a eraser (the subtrahend) from @a whiteboard (the
 * minuend), and places the resulting difference in @a *mergeinfo.
 * Allocates @a *mergeinfo in @a result_pool.  Temporary allocations
 * will be performed in @a scratch_pool.
 *
 * @a consider_inheritance determines how to account for the inheritability
 * of the two mergeinfo's ranges when calculating the range equivalence,
 * as described for svn_mergeinfo_diff().
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_mergeinfo_remove2(svn_mergeinfo_t *mergeinfo,
                      svn_mergeinfo_t eraser,
                      svn_mergeinfo_t whiteboard,
                      svn_boolean_t consider_inheritance,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

/** Calculate the delta between two rangelists consisting of @c
 * svn_merge_range_t * elements (sorted in ascending order), @a from
 * and @a to, and place the result in @a *deleted and @a *added
 * (neither output argument will ever be @c NULL).
 *
 * @a consider_inheritance determines how to account for the inheritability
 * of the two rangelist's ranges when calculating the diff,
 * as described for svn_mergeinfo_diff().
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_diff(svn_rangelist_t **deleted, svn_rangelist_t **added,
                   const svn_rangelist_t *from, const svn_rangelist_t *to,
                   svn_boolean_t consider_inheritance,
                   apr_pool_t *pool);

/** Merge two rangelists consisting of @c svn_merge_range_t *
 * elements, @a rangelist and @a changes, placing the results in
 * @a rangelist. New elements added to @a rangelist are allocated
 * in @a result_pool. Either rangelist may be empty.
 *
 * When intersecting rangelists are merged, the inheritability of
 * the resulting svn_merge_range_t depends on the inheritability of the
 * operands: see svn_mergeinfo_merge().
 *
 * Note: @a rangelist and @a changes must be sorted as said by @c
 * svn_sort_compare_ranges().  @a rangelist is guaranteed to remain
 * in sorted order and be compacted to the minimal number of ranges
 * needed to represent the merged result.
 *
 * If the original rangelist contains non-collapsed adjacent ranges,
 * the final result is not guaranteed to be compacted either.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_rangelist_merge2(svn_rangelist_t *rangelist,
                     const svn_rangelist_t *changes,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);

/** Like svn_rangelist_merge2(), but with @a rangelist as an input/output
 * argument. This function always allocates a new rangelist in @a pool and
 * returns its result in @a *rangelist. It does not modify @a *rangelist
 * in place. If not used carefully, this function can use up a lot of memory
 * if called in a loop.
 *
 * It performs an extra adjacent range compaction round to make sure non
 * collapsed input ranges are compacted in the result.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_rangelist_merge(svn_rangelist_t **rangelist,
                    const svn_rangelist_t *changes,
                    apr_pool_t *pool);

/** Removes @a eraser (the subtrahend) from @a whiteboard (the
 * minuend), and places the resulting difference in @a output.
 *
 * Note: @a eraser and @a whiteboard must be sorted as said by @c
 * svn_sort_compare_ranges().  @a output is guaranteed to be in sorted
 * order.
 *
 * @a consider_inheritance determines how to account for the
 * @c svn_merge_range_t inheritable field when comparing @a whiteboard's
 * and @a *eraser's rangelists for equality.  @see svn_mergeinfo_diff().
 *
 * Allocate the entire output in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_remove(svn_rangelist_t **output, const svn_rangelist_t *eraser,
                     const svn_rangelist_t *whiteboard,
                     svn_boolean_t consider_inheritance,
                     apr_pool_t *pool);

/** Find the intersection of two mergeinfos, @a mergeinfo1 and @a
 * mergeinfo2, and place the result in @a *mergeinfo, which is (deeply)
 * allocated in @a result_pool.  Temporary allocations will be performed
 * in @a scratch_pool.
 *
 * @a consider_inheritance determines how to account for the inheritability
 * of the two mergeinfo's ranges when calculating the range equivalence,
 * @see svn_rangelist_intersect().
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_mergeinfo_intersect2(svn_mergeinfo_t *mergeinfo,
                         svn_mergeinfo_t mergeinfo1,
                         svn_mergeinfo_t mergeinfo2,
                         svn_boolean_t consider_inheritance,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

/** Like svn_mergeinfo_intersect2, but always considers inheritance.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_mergeinfo_intersect(svn_mergeinfo_t *mergeinfo,
                        svn_mergeinfo_t mergeinfo1,
                        svn_mergeinfo_t mergeinfo2,
                        apr_pool_t *pool);

/** Find the intersection of two rangelists consisting of @c
 * svn_merge_range_t * elements, @a rangelist1 and @a rangelist2, and
 * place the result in @a *rangelist (which is never @c NULL).
 *
 * @a consider_inheritance determines how to account for the inheritability
 * of the two rangelist's ranges when calculating the intersection,
 * @see svn_mergeinfo_diff().  If @a consider_inheritance is FALSE then
 * ranges with different inheritance can intersect, but the resulting
 * @a *rangelist is non-inheritable only if the corresponding ranges from
 * both @a rangelist1 and @a rangelist2 are non-inheritable.
 * If @a consider_inheritance is TRUE, then ranges with different
 * inheritance can never intersect.
 *
 * Note: @a rangelist1 and @a rangelist2 must be sorted as said by @c
 * svn_sort_compare_ranges(). @a *rangelist is guaranteed to be in sorted
 * order.
 *
 * Allocate the entire output in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_intersect(svn_rangelist_t **rangelist,
                        const svn_rangelist_t *rangelist1,
                        const svn_rangelist_t *rangelist2,
                        svn_boolean_t consider_inheritance,
                        apr_pool_t *pool);

/** Reverse @a rangelist, and the @c start and @c end fields of each
 * range in @a rangelist, in place.
 *
 * TODO(miapi): Is this really a valid function?  Rangelists that
 * aren't sorted, or rangelists containing reverse ranges, are
 * generally not valid in mergeinfo code.  Can we rewrite the two
 * places where this is used?
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_reverse(svn_rangelist_t *rangelist, apr_pool_t *pool);

/** Take an array of svn_merge_range_t *'s in @a rangelist, and convert it
 * back to a text format rangelist in @a output.  If @a rangelist contains
 * no elements, sets @a output to the empty string.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_rangelist_to_string(svn_string_t **output,
                        const svn_rangelist_t *rangelist,
                        apr_pool_t *pool);

/** Remove non-inheritable or inheritable revision ranges from a rangelist.
 *
 * Set @a *inheritable_rangelist to a deep copy of @a rangelist, excluding
 * all non-inheritable @c svn_merge_range_t if @a inheritable is TRUE or
 * excluding all inheritable @c svn_merge_range_t otherwise.
 *
 * If @a start and @a end are valid revisions and @a start is less than or
 * equal to @a end, then exclude only the (non-inheritable or inheritable)
 * revision ranges that intersect inclusively with the range defined by
 * @a start and @a end.
 *
 * If there are no remaining ranges, return an empty array.
 *
 * Allocate the copy in @a result_pool, and use @a scratch_pool for
 * temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_rangelist_inheritable2(svn_rangelist_t **inheritable_rangelist,
                           const svn_rangelist_t *rangelist,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t inheritable,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/** Like svn_rangelist_inheritable2, but always finds inheritable ranges.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_rangelist_inheritable(svn_rangelist_t **inheritable_rangelist,
                          const svn_rangelist_t *rangelist,
                          svn_revnum_t start,
                          svn_revnum_t end,
                          apr_pool_t *pool);

/** Remove non-inheritable or inheritable revision ranges from mergeinfo.
 *
 * Set @a *inheritable_mergeinfo to a deep copy of @a mergeinfo, excluding
 * all non-inheritable @c svn_merge_range_t if @a inheritable is TRUE or
 * excluding all inheritable @c svn_merge_range_t otherwise.
 *
 * If @a start and @a end are valid revisions and @a start is less than or
 * equal to @a end, then exclude only the (non-inheritable or inheritable)
 * revisions that intersect inclusively with the range defined by @a start
 * and @a end.
 *
 * If @a path is not NULL remove (non-inheritable or inheritable) ranges
 * only for @a path.
 *
 * If all ranges are removed for a given path then remove that path as well.
 * If @a mergeinfo is initially empty or all paths are removed from it then
 * set @a *inheritable_mergeinfo to an empty mergeinfo.
 *
 * Allocate the copy in @a result_pool, and use @a scratch_pool for
 * temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_mergeinfo_inheritable2(svn_mergeinfo_t *inheritable_mergeinfo,
                           svn_mergeinfo_t mergeinfo,
                           const char *path,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t inheritable,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/** Like svn_mergeinfo_inheritable2, but always finds inheritable mergeinfo.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_mergeinfo_inheritable(svn_mergeinfo_t *inheritable_mergeinfo,
                          svn_mergeinfo_t mergeinfo,
                          const char *path,
                          svn_revnum_t start,
                          svn_revnum_t end,
                          apr_pool_t *pool);

/** Take a mergeinfo in @a mergeinput, and convert it to unparsed
 *  mergeinfo. Set @a *output to the result, allocated in @a pool.
 *  If @a input contains no elements, set @a *output to the empty string.
 *
 * @a mergeinput may contain relative merge source paths, but these are
 * converted to absolute paths in @a *output.
 *
 * @since New in 1.5.
*/
svn_error_t *
svn_mergeinfo_to_string(svn_string_t **output,
                        svn_mergeinfo_t mergeinput,
                        apr_pool_t *pool);

/** Take a hash of mergeinfo in @a mergeinfo, and sort the rangelists
 * associated with each key (in place).
 *
 * TODO(miapi): mergeinfos should *always* be sorted.  This should be
 * a private function.
 *
 * @since New in 1.5
 */
svn_error_t *
svn_mergeinfo_sort(svn_mergeinfo_t mergeinfo, apr_pool_t *pool);

/** Return a deep copy of @a mergeinfo_catalog, allocated in @a pool.
 *
 * @since New in 1.6.
 */
svn_mergeinfo_catalog_t
svn_mergeinfo_catalog_dup(svn_mergeinfo_catalog_t mergeinfo_catalog,
                          apr_pool_t *pool);

/** Return a deep copy of @a mergeinfo, allocated in @a pool.
 *
 * @since New in 1.5.
 */
svn_mergeinfo_t
svn_mergeinfo_dup(svn_mergeinfo_t mergeinfo, apr_pool_t *pool);

/** Return a deep copy of @a rangelist, allocated in @a pool.
 *
 * @since New in 1.5.
 */
svn_rangelist_t *
svn_rangelist_dup(const svn_rangelist_t *rangelist, apr_pool_t *pool);


/**
 * The three ways to request mergeinfo affecting a given path.
 *
 * @since New in 1.5.
 */
typedef enum svn_mergeinfo_inheritance_t
{
  /** Explicit mergeinfo only. */
  svn_mergeinfo_explicit,

  /** Explicit mergeinfo, or if that doesn't exist, the inherited
      mergeinfo from a target's nearest (path-wise, not history-wise)
      ancestor. */
  svn_mergeinfo_inherited,

  /** Mergeinfo inherited from a target's nearest (path-wise, not
      history-wise) ancestor, regardless of whether target has explicit
      mergeinfo. */
  svn_mergeinfo_nearest_ancestor
} svn_mergeinfo_inheritance_t;

/** Return a constant string expressing @a inherit as an English word,
 * i.e., "explicit" (default), "inherited", or "nearest_ancestor".
 * The string is not localized, as it may be used for client<->server
 * communications.
 *
 * @since New in 1.5.
 */
const char *
svn_inheritance_to_word(svn_mergeinfo_inheritance_t inherit);


/** Return the appropriate @c svn_mergeinfo_inheritance_t for @a word.
 * @a word is as returned from svn_inheritance_to_word().  Defaults to
 * @c svn_mergeinfo_explicit.
 *
 * @since New in 1.5.
 */
svn_mergeinfo_inheritance_t
svn_inheritance_from_word(const char *word);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MERGEINFO_H */
