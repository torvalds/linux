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
 * @file svn_opt_private.h
 * @brief Subversion-internal option parsing APIs.
 */

#ifndef SVN_OPT_PRIVATE_H
#define SVN_OPT_PRIVATE_H

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_getopt.h>

#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Extract the peg revision, if any, from UTF8_TARGET.
 *
 * If PEG_REVISION is not NULL, return the peg revision in *PEG_REVISION.
 * *PEG_REVISION will be an empty string if no peg revision is found.
 * Return the true target portion in *TRUE_TARGET.
 *
 * UTF8_TARGET need not be canonical. *TRUE_TARGET will not be canonical
 * unless UTF8_TARGET is.
 *
 * Note that *PEG_REVISION will still contain the '@' symbol as the first
 * character if a peg revision was found. If a trailing '@' symbol was
 * used to escape other '@' characters in UTF8_TARGET, *PEG_REVISION will
 * point to the string "@", containing only a single character.
 *
 * All allocations are done in POOL.
 */
svn_error_t *
svn_opt__split_arg_at_peg_revision(const char **true_target,
                                   const char **peg_revision,
                                   const char *utf8_target,
                                   apr_pool_t *pool);

/* Attempt to transform URL_IN, which is a URL-like user input, into a
 * valid URL:
 *   - escape IRI characters and some other non-URI characters
 *   - check that no back-path ("..") components are present
 *   - call svn_uri_canonicalize()
 * URL_IN is in UTF-8 encoding and has no peg revision specifier.
 * Set *URL_OUT to the result, allocated from POOL.
 */
svn_error_t *
svn_opt__arg_canonicalize_url(const char **url_out,
                              const char *url_in,
                              apr_pool_t *pool);

/*
 * Attempt to transform PATH_IN, which is a local path-like user input, into a
 * valid local path:
 *   - Attempt to get the correct capitalization by trying to actually find
 *     the path specified.
 *   - If the path does not exist (which is valid) the given capitalization
 *     is used.
 *   - canonicalize the separator ("/") characters
 *   - call svn_dirent_canonicalize()
 * PATH_IN is in UTF-8 encoding and has no peg revision specifier.
 * Set *PATH_OUT to the result, allocated from POOL.
 */
svn_error_t *
svn_opt__arg_canonicalize_path(const char **path_out,
                               const char *path_in,
                               apr_pool_t *pool);

/*
 * Pull remaining target arguments from OS into *TARGETS_P,
 * converting them to UTF-8, followed by targets from KNOWN_TARGETS
 * (which might come from, for example, the "--targets" command line
 * option), which are already in UTF-8.
 *
 * On each URL target, do some IRI-to-URI encoding and some
 * auto-escaping.  On each local path, canonicalize case and path
 * separators.
 *
 * Allocate *TARGETS_P and its elements in POOL.
 *
 * If a path has the same name as a Subversion working copy
 * administrative directory, return SVN_ERR_RESERVED_FILENAME_SPECIFIED;
 * if multiple reserved paths are encountered, return a chain of
 * errors, all of which are SVN_ERR_RESERVED_FILENAME_SPECIFIED.  Do
 * not return this type of error in a chain with any other type of
 * error, and if this is the only type of error encountered, complete
 * the operation before returning the error(s).
 */
svn_error_t *
svn_opt__args_to_target_array(apr_array_header_t **targets_p,
                              apr_getopt_t *os,
                              const apr_array_header_t *known_targets,
                              apr_pool_t *pool);

/**
 * Return a human-readable description of @a revision.  The result
 * will be allocated statically or from @a result_pool.
 *
 * @since New in 1.7.
 */
const char *
svn_opt__revision_to_string(const svn_opt_revision_t *revision,
                            apr_pool_t *result_pool);

/**
 * Create a revision range structure from two revisions.  Return a new range
 * allocated in @a result_pool with the start and end initialized to
 * (deep copies of) @a *start_revision and @a *end_revision.
 */
svn_opt_revision_range_t *
svn_opt__revision_range_create(const svn_opt_revision_t *start_revision,
                               const svn_opt_revision_t *end_revision,
                               apr_pool_t *result_pool);

/**
 * Create a revision range structure from two revnums.  Return a new range
 * allocated in @a result_pool with the start and end kinds initialized to
 * #svn_opt_revision_number and values @a start_revnum and @a end_revnum.
 */
svn_opt_revision_range_t *
svn_opt__revision_range_from_revnums(svn_revnum_t start_revnum,
                                     svn_revnum_t end_revnum,
                                     apr_pool_t *result_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_OPT_PRIVATE_H */
