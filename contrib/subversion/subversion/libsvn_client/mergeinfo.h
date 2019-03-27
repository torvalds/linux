/*
 * mergeinfo.h : Client library-internal mergeinfo APIs.
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

#ifndef SVN_LIBSVN_CLIENT_MERGEINFO_H
#define SVN_LIBSVN_CLIENT_MERGEINFO_H

#include "svn_wc.h"
#include "svn_client.h"
#include "private/svn_client_private.h"


/*** Data Structures ***/


/* Structure to store information about working copy paths that need special
   consideration during a mergeinfo aware merge -- See the
   'THE CHILDREN_WITH_MERGEINFO ARRAY' meta comment and the doc string for the
   function get_mergeinfo_paths() in libsvn_client/merge.c.
*/
typedef struct svn_client__merge_path_t
{
  const char *abspath;               /* Absolute working copy path. */
  svn_boolean_t missing_child;       /* ABSPATH has an immediate child which
                                        is missing, but is not switched. */
  svn_boolean_t switched_child;      /* ABSPATH has an immediate child which
                                        is switched. */
  svn_boolean_t switched;            /* ABSPATH is switched. */
  svn_boolean_t has_noninheritable;  /* ABSPATH has svn:mergeinfo set on it
                                        which includes non-inheritable
                                        revision ranges. */
  svn_boolean_t absent;              /* ABSPATH is absent from the WC,
                                        probably due to authz
                                        restrictions. */

  svn_boolean_t child_of_noninheritable; /* ABSPATH has no explicit mergeinfo
                                            itself but is the child of a
                                            path with noniheritable
                                            mergeinfo. */

  /* The remaining ranges to be merged to ABSPATH.  When describing a forward
     merge this rangelist adheres to the rules for rangelists described in
     svn_mergeinfo.h.  However, when describing reverse merges this
     rangelist can contain reverse merge ranges that are not sorted per
     svn_sort_compare_ranges(), but rather are sorted such that the ranges
     with the youngest start revisions come first.  In both the forward and
     reverse merge cases the ranges should never overlap.  This rangelist
     may be empty but should never be NULL unless ABSENT is true. */
  svn_rangelist_t *remaining_ranges;

  svn_mergeinfo_t pre_merge_mergeinfo;  /* Explicit or inherited mergeinfo
                                           on ABSPATH prior to a merge.
                                           May be NULL. */
  svn_mergeinfo_t implicit_mergeinfo;   /* Implicit mergeinfo on ABSPATH
                                           prior to a merge.  May be NULL. */
  svn_boolean_t inherited_mergeinfo;    /* Whether PRE_MERGE_MERGEINFO was
                                           explicit or inherited. */
  svn_boolean_t scheduled_for_deletion; /* ABSPATH is scheduled for
                                           deletion. */
  svn_boolean_t immediate_child_dir;    /* ABSPATH is an immediate child
                                           directory of the merge target,
                                           has no explicit mergeinfo prior
                                           to the merge, and the operational
                                           depth of the merge is
                                           svn_depth_immediates. */
  svn_boolean_t record_mergeinfo;       /* Mergeinfo needs to be recorded
                                           on ABSPATH to describe the
                                           merge. */
  svn_boolean_t record_noninheritable;  /* Non-inheritable mergeinfo needs to
                                           be recorded on ABSPATH to describe
                                           the merge. Implies RECORD_MERGEINFO
                                           is true. */
} svn_client__merge_path_t;

/* Return a deep copy of the merge-path structure OLD, allocated in POOL. */
svn_client__merge_path_t *
svn_client__merge_path_dup(const svn_client__merge_path_t *old,
                           apr_pool_t *pool);

/* Create a new merge path structure, allocated in POOL.  Initialize the
 * 'abspath' member to a deep copy of ABSPATH and all other fields to zero
 * bytes. */
svn_client__merge_path_t *
svn_client__merge_path_create(const char *abspath,
                              apr_pool_t *pool);



/*** Functions ***/

/* Find explicit or inherited WC mergeinfo for LOCAL_ABSPATH, and return it
   in *MERGEINFO (NULL if no mergeinfo is set).  Set *INHERITED to
   whether the mergeinfo was inherited (TRUE or FALSE), if INHERITED is
   non-null.

   This function will search for inherited mergeinfo in the parents of
   LOCAL_ABSPATH only if the base revision of LOCAL_ABSPATH falls within
   the range of the parent's last committed revision to the parent's base
   revision (inclusive) or is LOCAL_ABSPATH is a local addition.  If asking
   for the inherited mergeinfo of an added path (i.e. one with no base
   revision), that path may inherit mergeinfo from its nearest parent
   with a base revision and explicit mergeinfo.

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for LOCAL_ABSPATH is retrieved.

   Don't look for inherited mergeinfo any higher than LIMIT_ABSPATH
   (ignored if NULL) or beyond any switched path.

   Set *WALKED_PATH to the path climbed from LOCAL_ABSPATH to find inherited
   mergeinfo, or "" if none was found. (ignored if NULL).

   If IGNORE_INVALID_MERGEINFO is true, then syntactically invalid explicit
   mergeinfo on found on LOCAL_ABSPATH is ignored and *MERGEINFO is set to an
   empty hash.  If IGNORE_INVALID_MERGEINFO is false, then syntactically
   invalid explicit mergeinfo on found on LOCAL_ABSPATH results in a
   SVN_ERR_MERGEINFO_PARSE_ERROR error.  Regardless of
   IGNORE_INVALID_MERGEINFO, if LOCAL_ABSPATH inherits invalid mergeinfo,
   then *MERGEINFO is always set to an empty hash and no parse error is
   raised. */
svn_error_t *
svn_client__get_wc_mergeinfo(svn_mergeinfo_t *mergeinfo,
                             svn_boolean_t *inherited,
                             svn_mergeinfo_inheritance_t inherit,
                             const char *local_abspath,
                             const char *limit_abspath,
                             const char **walked_path,
                             svn_boolean_t ignore_invalid_mergeinfo,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* If INCLUDE_DESCENDANTS is FALSE, behave exactly like
   svn_client__get_wc_mergeinfo() except the mergeinfo for LOCAL_ABSPATH is
   put in the mergeinfo catalog MERGEINFO_CAT, mapped from LOCAL_ABSPATH's
   repository root-relative path.

   If INCLUDE_DESCENDANTS is true, then any subtrees under LOCAL_ABSPATH with
   explicit mergeinfo are also included in MERGEINFO_CAT and again the
   keys are the repository root-relative paths of the subtrees.  If no
   mergeinfo is found, then *MERGEINFO_CAT is set to NULL. */
svn_error_t *
svn_client__get_wc_mergeinfo_catalog(svn_mergeinfo_catalog_t *mergeinfo_cat,
                                     svn_boolean_t *inherited,
                                     svn_boolean_t include_descendants,
                                     svn_mergeinfo_inheritance_t inherit,
                                     const char *local_abspath,
                                     const char *limit_path,
                                     const char **walked_path,
                                     svn_boolean_t ignore_invalid_mergeinfo,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/* Obtain any mergeinfo for URL from the repository, and set
   it in *TARGET_MERGEINFO.

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for URL is obtained.

   If URL does not exist at REV, SVN_ERR_FS_NOT_FOUND or
   SVN_ERR_RA_DAV_REQUEST_FAILED is returned and *TARGET_MERGEINFO
   is untouched.

   If there is no mergeinfo available for URL, or if the server
   doesn't support a mergeinfo capability and SQUELCH_INCAPABLE is
   TRUE, set *TARGET_MERGEINFO to NULL. If the server doesn't support
   a mergeinfo capability and SQUELCH_INCAPABLE is FALSE, return an
   SVN_ERR_UNSUPPORTED_FEATURE error.

   RA_SESSION is an open RA session to the repository in which URL lives;
   it may be temporarily reparented by this function.
*/
svn_error_t *
svn_client__get_repos_mergeinfo(svn_mergeinfo_t *target_mergeinfo,
                                svn_ra_session_t *ra_session,
                                const char *url,
                                svn_revnum_t rev,
                                svn_mergeinfo_inheritance_t inherit,
                                svn_boolean_t squelch_incapable,
                                apr_pool_t *pool);

/* If INCLUDE_DESCENDANTS is FALSE, behave exactly like
   svn_client__get_repos_mergeinfo() except the mergeinfo for URL
   is put in the mergeinfo catalog MERGEINFO_CAT, with the key being
   the repository root-relative path of URL.

   If INCLUDE_DESCENDANTS is true, then any subtrees under URL
   with explicit mergeinfo are also included in MERGEINFO_CAT.  The
   keys for the subtree mergeinfo are the repository root-relative
   paths of the subtrees.  If no mergeinfo is found, then
   *TARGET_MERGEINFO_CAT is set to NULL. */
svn_error_t *
svn_client__get_repos_mergeinfo_catalog(svn_mergeinfo_catalog_t *mergeinfo_cat,
                                        svn_ra_session_t *ra_session,
                                        const char *url,
                                        svn_revnum_t rev,
                                        svn_mergeinfo_inheritance_t inherit,
                                        svn_boolean_t squelch_incapable,
                                        svn_boolean_t include_descendants,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool);

/* Retrieve the direct mergeinfo for the TARGET_WCPATH from the WC's
   mergeinfo prop, or that inherited from its nearest ancestor if the
   target has no info of its own.

   If no mergeinfo can be obtained from the WC or REPOS_ONLY is TRUE,
   get it from the repository.  If the repository is contacted for mergeinfo
   and RA_SESSION does not point to TARGET_WCPATH's URL, then it is
   temporarily reparented.  If RA_SESSION is NULL, then a temporary session
   is opened as needed.

   Store any mergeinfo obtained for TARGET_WCPATH in
   *TARGET_MERGEINFO, if no mergeinfo is found *TARGET_MERGEINFO is
   NULL.

   Like svn_client__get_wc_mergeinfo(), this function considers no
   inherited mergeinfo to be found in the WC when trying to crawl into
   a parent path with a different working revision.

   INHERIT indicates whether explicit, explicit or inherited, or only
   inherited mergeinfo for TARGET_WCPATH is retrieved.

   If FROM_REPOS is not NULL, then set *FROM_REPOS to true if
   *TARGET_MERGEINFO is inherited and the repository was contacted to
   obtain it.  Set *FROM_REPOS to false otherwise.

   If TARGET_WCPATH inherited its mergeinfo from a working copy ancestor
   or if it was obtained from the repository, set *INHERITED to TRUE, set it
   to FALSE otherwise, if INHERITED is non-null. */
svn_error_t *
svn_client__get_wc_or_repos_mergeinfo(svn_mergeinfo_t *target_mergeinfo,
                                      svn_boolean_t *inherited,
                                      svn_boolean_t *from_repos,
                                      svn_boolean_t repos_only,
                                      svn_mergeinfo_inheritance_t inherit,
                                      svn_ra_session_t *ra_session,
                                      const char *target_wcpath,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *pool);

/* If INCLUDE_DESCENDANTS is false then behaves exactly like
   svn_client__get_wc_or_repos_mergeinfo() except the mergeinfo for
   TARGET_WCPATH is put in the mergeinfo catalog
   TARGET_MERGEINFO_CATALOG, mapped from TARGET_WCPATH's repository
   root-relative path.

   IGNORE_INVALID_MERGEINFO behaves as per the argument of the same
   name to svn_client__get_wc_mergeinfo().  It is applicable only if
   the mergeinfo for TARGET_WCPATH is obtained from the working copy.

   If INCLUDE_DESCENDANTS is true, then any subtrees under
   TARGET_WCPATH with explicit mergeinfo are also included in
   TARGET_MERGEINFO_CATALOG and again the keys are the repository
   root-relative paths of the subtrees.  If no mergeinfo is found,
   then *TARGET_MERGEINFO_CAT is set to NULL. */
svn_error_t *
svn_client__get_wc_or_repos_mergeinfo_catalog(
  svn_mergeinfo_catalog_t *target_mergeinfo_catalog,
  svn_boolean_t *inherited,
  svn_boolean_t *from_repos,
  svn_boolean_t include_descendants,
  svn_boolean_t repos_only,
  svn_boolean_t ignore_invalid_mergeinfo,
  svn_mergeinfo_inheritance_t inherit,
  svn_ra_session_t *ra_session,
  const char *target_wcpath,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/* Set *MERGEINFO_P to a mergeinfo constructed solely from the
   natural history of PATHREV.

   If RANGE_YOUNGEST and RANGE_OLDEST are valid, use them as inclusive
   bounds on the revision ranges of returned mergeinfo.  PATHREV->rev,
   RANGE_YOUNGEST and RANGE_OLDEST are governed by the same rules as the
   PEG_REVISION, START_REV, and END_REV parameters (respectively) of
   svn_ra_get_location_segments().

   If HAS_REV_ZERO_HISTORY is not NULL, then set *HAS_REV_ZERO_HISTORY to
   TRUE if the natural history includes revision 0, else to FALSE.

   RA_SESSION is an open RA session to the repository of PATHREV;
   it may be temporarily reparented by this function.
*/
svn_error_t *
svn_client__get_history_as_mergeinfo(svn_mergeinfo_t *mergeinfo_p,
                                     svn_boolean_t *has_rev_zero_history,
                                     const svn_client__pathrev_t *pathrev,
                                     svn_revnum_t range_youngest,
                                     svn_revnum_t range_oldest,
                                     svn_ra_session_t *ra_session,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);

/* Parse any explicit mergeinfo on LOCAL_ABSPATH and store it in
   *MERGEINFO.  If no record of any mergeinfo exists, set *MERGEINFO to NULL.
   Does not acount for inherited mergeinfo.

   Allocate the result deeply in @a result_pool. */
svn_error_t *
svn_client__parse_mergeinfo(svn_mergeinfo_t *mergeinfo,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Write MERGEINFO into the WC for LOCAL_ABSPATH.  If MERGEINFO is NULL,
   remove any SVN_PROP_MERGEINFO for LOCAL_ABSPATH.  If MERGEINFO is empty,
   record an empty property value (e.g. "").  If CTX->NOTIFY_FUNC2 is
   not null call it with notification type svn_wc_notify_merge_record_info
   if DO_NOTIFICATION is true.

   Use WC_CTX to access the working copy, and SCRATCH_POOL for any temporary
   allocations. */
svn_error_t *
svn_client__record_wc_mergeinfo(const char *local_abspath,
                                svn_mergeinfo_t mergeinfo,
                                svn_boolean_t do_notification,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *scratch_pool);

/* Write mergeinfo into the WC.
 *
 * For each path in RESULT_CATALOG, set the SVN_PROP_MERGEINFO
 * property to represent the given mergeinfo, or remove the property
 * if the given mergeinfo is null, and notify the change.  Leave
 * other paths unchanged.  RESULT_CATALOG maps (const char *) WC paths
 * to (svn_mergeinfo_t) mergeinfo. */
svn_error_t *
svn_client__record_wc_mergeinfo_catalog(apr_hash_t *result_catalog,
                                        svn_client_ctx_t *ctx,
                                        apr_pool_t *scratch_pool);

/* Elide any svn:mergeinfo set on TARGET_ABSPATH to its nearest working
   copy (or possibly repository) ancestor with equivalent mergeinfo.

   If WC_ELISION_LIMIT_ABSPATH is NULL check up to the root of the
   working copy or the nearest switched parent for an elision
   destination, if none is found check the repository, otherwise check
   as far as WC_ELISION_LIMIT_ABSPATH within the working copy.

   Elision occurs if:

     A) TARGET_ABSPATH has empty mergeinfo and no parent path with
        explicit mergeinfo can be found in either the WC or the
        repository (WC_ELISION_LIMIT_PATH must be NULL for this to
        occur).

     B) TARGET_ABSPATH has empty mergeinfo and its nearest parent also
        has empty mergeinfo.

     C) TARGET_ABSPATH has the same mergeinfo as its nearest parent
        when that parent's mergeinfo is adjusted for the path
        difference between the two, e.g.:

           TARGET_ABSPATH                = A_COPY/D/H
           TARGET_ABSPATH's mergeinfo    = '/A/D/H:3'
           TARGET_ABSPATH nearest parent = A_COPY
           Parent's mergeinfo            = '/A:3'
           Path difference               = 'D/H'
           Parent's adjusted mergeinfo   = '/A/D/H:3'

   If Elision occurs remove the svn:mergeinfo property from
   TARGET_ABSPATH. */
svn_error_t *
svn_client__elide_mergeinfo(const char *target_abspath,
                            const char *wc_elision_limit_abspath,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* Simplify a mergeinfo catalog, if possible, via elision.

   For each path in MERGEINFO_CATALOG, check if the path's mergeinfo can
   elide to the path's nearest path-wise parent in MERGEINFO_CATALOG.  If
   so, remove that path from MERGEINFO_CATALOG.  Elidability is determined
   as per svn_client__elide_mergeinfo except that elision to the repository
   is not considered.

   SCRATCH_POOL is used for temporary allocations. */
svn_error_t *
svn_client__elide_mergeinfo_catalog(svn_mergeinfo_catalog_t mergeinfo_catalog,
                                    apr_pool_t *scratch_pool);

/* Set *MERGEINFO_CHANGES to TRUE if LOCAL_ABSPATH has locally modified
   mergeinfo, set *MERGEINFO_CHANGES to FALSE otherwise. */
svn_error_t *
svn_client__mergeinfo_status(svn_boolean_t *mergeinfo_changes,
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             apr_pool_t *scratch_pool);

#endif /* SVN_LIBSVN_CLIENT_MERGEINFO_H */
