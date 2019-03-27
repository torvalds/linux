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
 * @file svn_path.h
 * @brief A path manipulation library
 *
 * All incoming and outgoing paths are non-NULL and in UTF-8, unless
 * otherwise documented.
 *
 * No result path ever ends with a separator, no matter whether the
 * path is a file or directory, because we always canonicalize() it.
 *
 * Nearly all the @c svn_path_xxx functions expect paths passed into
 * them to be in canonical form as defined by the Subversion path
 * library itself.  The only functions which do *not* have such
 * expectations are:
 *
 *    - @c svn_path_canonicalize()
 *    - @c svn_path_is_canonical()
 *    - @c svn_path_internal_style()
 *    - @c svn_path_uri_encode()
 *
 * For the most part, we mean what most anyone would mean when talking
 * about canonical paths, but to be on the safe side, you must run
 * your paths through @c svn_path_canonicalize() before passing them to
 * other functions in this API.
 */

#ifndef SVN_PATH_H
#define SVN_PATH_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** Convert @a path from the local style to the canonical internal style.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_internal_style().
 */
SVN_DEPRECATED
const char *
svn_path_internal_style(const char *path, apr_pool_t *pool);

/** Convert @a path from the canonical internal style to the local style.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_local_style().
 */
SVN_DEPRECATED
const char *
svn_path_local_style(const char *path, apr_pool_t *pool);


/** Join a base path (@a base) with a component (@a component), allocating
 * the result in @a pool. @a component need not be a single component: it
 * can be any path, absolute or relative to @a base.
 *
 * If either @a base or @a component is the empty path, then the other
 * argument will be copied and returned.  If both are the empty path the
 * empty path is returned.
 *
 * If the @a component is an absolute path, then it is copied and returned.
 * Exactly one slash character ('/') is used to join the components,
 * accounting for any trailing slash in @a base.
 *
 * Note that the contents of @a base are not examined, so it is possible to
 * use this function for constructing URLs, or for relative URLs or
 * repository paths.
 *
 * This function is NOT appropriate for native (local) file
 * paths. Only for "internal" canonicalized paths, since it uses '/'
 * for the separator. Further, an absolute path (for @a component) is
 * based on a leading '/' character.  Thus, an "absolute URI" for the
 * @a component won't be detected. An absolute URI can only be used
 * for the base.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_join(), svn_relpath_join() or
 * svn_fspath__join().
 */
SVN_DEPRECATED
char *
svn_path_join(const char *base, const char *component, apr_pool_t *pool);

/** Join multiple components onto a @a base path, allocated in @a pool. The
 * components are terminated by a @c SVN_VA_NULL.
 *
 * If any component is the empty string, it will be ignored.
 *
 * If any component is an absolute path, then it resets the base and
 * further components will be appended to it.
 *
 * This function does not support URLs.
 *
 * See svn_path_join() for further notes about joining paths.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * For new code, consider using svn_dirent_join_many() or a sequence of
 * calls to one of the *_join() functions.
 */
SVN_DEPRECATED
char *
svn_path_join_many(apr_pool_t *pool,
                   const char *base,
                   ...) SVN_NEEDS_SENTINEL_NULL;


/** Get the basename of the specified canonicalized @a path.  The
 * basename is defined as the last component of the path (ignoring any
 * trailing slashes).  If the @a path is root ("/"), then that is
 * returned.  Otherwise, the returned value will have no slashes in
 * it.
 *
 * Example: svn_path_basename("/foo/bar") -> "bar"
 *
 * The returned basename will be allocated in @a pool.
 *
 * @note If an empty string is passed, then an empty string will be returned.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_basename(), svn_uri_basename(),
 * svn_relpath_basename() or svn_fspath__basename().
 */
SVN_DEPRECATED
char *
svn_path_basename(const char *path, apr_pool_t *pool);

/** Get the dirname of the specified canonicalized @a path, defined as
 * the path with its basename removed.  If @a path is root ("/"), it is
 * returned unchanged.
 *
 * The returned dirname will be allocated in @a pool.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_dirname(), svn_uri_dirname(),
 * svn_relpath_dirname() or svn_fspath__dirname().
 */
SVN_DEPRECATED
char *
svn_path_dirname(const char *path, apr_pool_t *pool);

/** Split @a path into a root portion and an extension such that
 * the root + the extension = the original path, and where the
 * extension contains no period (.) characters.  If not @c NULL, set
 * @a *path_root to the root portion.  If not @c NULL, set
 * @a *path_ext to the extension (or "" if there is no extension
 * found).  Allocate both @a *path_root and @a *path_ext in @a pool.
 *
 * @since New in 1.5.
 */
void
svn_path_splitext(const char **path_root, const char **path_ext,
                  const char *path, apr_pool_t *pool);

/** Return the number of components in the canonicalized @a path.
 *
 * @since New in 1.1.
*/
apr_size_t
svn_path_component_count(const char *path);

/** Add a @a component (a NULL-terminated C-string) to the
 * canonicalized @a path.  @a component is allowed to contain
 * directory separators.
 *
 * If @a path is non-empty, append the appropriate directory separator
 * character, and then @a component.  If @a path is empty, simply set it to
 * @a component; don't add any separator character.
 *
 * If the result ends in a separator character, then remove the separator.
 */
void
svn_path_add_component(svn_stringbuf_t *path, const char *component);

/** Remove one component off the end of the canonicalized @a path. */
void
svn_path_remove_component(svn_stringbuf_t *path);

/** Remove @a n components off the end of the canonicalized @a path.
 * Equivalent to calling svn_path_remove_component() @a n times.
 *
 * @since New in 1.1.
 */
void
svn_path_remove_components(svn_stringbuf_t *path, apr_size_t n);

/** Divide the canonicalized @a path into @a *dirpath and @a
 * *base_name, allocated in @a pool.
 *
 * If @a dirpath or @a base_name is NULL, then don't set that one.
 *
 * Either @a dirpath or @a base_name may be @a path's own address, but they
 * may not both be the same address, or the results are undefined.
 *
 * If @a path has two or more components, the separator between @a dirpath
 * and @a base_name is not included in either of the new names.
 *
 *   examples:
 *             - <pre>"/foo/bar/baz"  ==>  "/foo/bar" and "baz"</pre>
 *             - <pre>"/bar"          ==>  "/"  and "bar"</pre>
 *             - <pre>"/"             ==>  "/"  and "/"</pre>
 *             - <pre>"X:/"           ==>  "X:/" and "X:/"</pre>
 *             - <pre>"bar"           ==>  ""   and "bar"</pre>
 *             - <pre>""              ==>  ""   and ""</pre>
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_split(), svn_uri_split(),
 * svn_relpath_split() or svn_fspath__split().
 */
SVN_DEPRECATED
void
svn_path_split(const char *path,
               const char **dirpath,
               const char **base_name,
               apr_pool_t *pool);


/** Return non-zero iff @a path is empty ("") or represents the current
 * directory -- that is, if prepending it as a component to an existing
 * path would result in no meaningful change.
 */
int
svn_path_is_empty(const char *path);


#ifndef SVN_DIRENT_URI_H
/* This declaration has been moved to svn_dirent_uri.h, and remains
   here only for compatibility reasons. */
svn_boolean_t
svn_dirent_is_root(const char *dirent, apr_size_t len);
#endif /* SVN_DIRENT_URI_H */


/** Return a new path (or URL) like @a path, but transformed such that
 * some types of path specification redundancies are removed.
 *
 * This involves collapsing redundant "/./" elements, removing
 * multiple adjacent separator characters, removing trailing
 * separator characters, and possibly other semantically inoperative
 * transformations.
 *
 * Convert the scheme and hostname to lowercase (see issue #2475)
 *
 * The returned path may be statically allocated, equal to @a path, or
 * allocated from @a pool.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_canonicalize(), svn_uri_canonicalize(),
 * svn_relpath_canonicalize() or svn_fspath__canonicalize().
 */
SVN_DEPRECATED
const char *
svn_path_canonicalize(const char *path, apr_pool_t *pool);

/** Return @c TRUE iff path is canonical. Use @a pool for temporary
 * allocations.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_is_canonical(), svn_uri_is_canonical(),
 * svn_relpath_is_canonical() or svn_fspath__is_canonical().
 */
SVN_DEPRECATED
svn_boolean_t
svn_path_is_canonical(const char *path, apr_pool_t *pool);


/** Return an integer greater than, equal to, or less than 0, according
 * as @a path1 is greater than, equal to, or less than @a path2.
 *
 * This function works like strcmp() except that it orders children in
 * subdirectories directly after their parents. This allows using the
 * given ordering for a depth first walk.
 */
int
svn_path_compare_paths(const char *path1, const char *path2);


/** Return the longest common path shared by two canonicalized paths,
 * @a path1 and @a path2.  If there's no common ancestor, return the
 * empty path.
 *
 * @a path1 and @a path2 may be URLs.  In order for two URLs to have
 * a common ancestor, they must (a) have the same protocol (since two URLs
 * with the same path but different protocols may point at completely
 * different resources), and (b) share a common ancestor in their path
 * component, i.e. 'protocol://' is not a sufficient ancestor.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_get_longest_ancestor(),
 * svn_uri_get_longest_ancestor(), svn_relpath_get_longest_ancestor() or
 * svn_fspath__get_longest_ancestor().
 */
SVN_DEPRECATED
char *
svn_path_get_longest_ancestor(const char *path1,
                              const char *path2,
                              apr_pool_t *pool);

/** Convert @a relative canonicalized path to an absolute path and
 * return the results in @a *pabsolute, allocated in @a pool.
 *
 * @a relative may be a URL, in which case no attempt is made to convert it,
 * and a copy of the URL is returned.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_get_absolute() on a non-URL input.
 */
SVN_DEPRECATED
svn_error_t *
svn_path_get_absolute(const char **pabsolute,
                      const char *relative,
                      apr_pool_t *pool);

/** Return the path part of the canonicalized @a path in @a
 * *pdirectory, and the file part in @a *pfile.  If @a path is a
 * directory, set @a *pdirectory to @a path, and @a *pfile to the
 * empty string.  If @a path does not exist it is treated as if it is
 * a file, since directories do not normally vanish.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should implement the required logic directly; no direct
 * replacement is provided.
 */
SVN_DEPRECATED
svn_error_t *
svn_path_split_if_file(const char *path,
                       const char **pdirectory,
                       const char **pfile,
                       apr_pool_t *pool);

/** Find the common prefix of the canonicalized paths in @a targets
 * (an array of <tt>const char *</tt>'s), and remove redundant paths if @a
 * remove_redundancies is TRUE.
 *
 *   - Set @a *pcommon to the absolute path of the path or URL common to
 *     all of the targets.  If the targets have no common prefix, or
 *     are a mix of URLs and local paths, set @a *pcommon to the
 *     empty string.
 *
 *   - If @a pcondensed_targets is non-NULL, set @a *pcondensed_targets
 *     to an array of targets relative to @a *pcommon, and if
 *     @a remove_redundancies is TRUE, omit any paths/URLs that are
 *     descendants of another path/URL in @a targets.  If *pcommon
 *     is empty, @a *pcondensed_targets will contain full URLs and/or
 *     absolute paths; redundancies can still be removed (from both URLs
 *     and paths).  If @a pcondensed_targets is NULL, leave it alone.
 *
 * Else if there is exactly one target, then
 *
 *   - Set @a *pcommon to that target, and
 *
 *   - If @a pcondensed_targets is non-NULL, set @a *pcondensed_targets
 *     to an array containing zero elements.  Else if
 *     @a pcondensed_targets is NULL, leave it alone.
 *
 * If there are no items in @a targets, set @a *pcommon and (if
 * applicable) @a *pcondensed_targets to @c NULL.
 *
 * @note There is no guarantee that @a *pcommon is within a working
 * copy.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_dirent_condense_targets() or
 * svn_uri_condense_targets().
 */
SVN_DEPRECATED
svn_error_t *
svn_path_condense_targets(const char **pcommon,
                          apr_array_header_t **pcondensed_targets,
                          const apr_array_header_t *targets,
                          svn_boolean_t remove_redundancies,
                          apr_pool_t *pool);


/** Copy a list of canonicalized @a targets, one at a time, into @a
 * pcondensed_targets, omitting any targets that are found earlier in
 * the list, or whose ancestor is found earlier in the list.  Ordering
 * of targets in the original list is preserved in the condensed list
 * of targets.  Use @a pool for any allocations.
 *
 * How does this differ in functionality from svn_path_condense_targets()?
 *
 * Here's the short version:
 *
 * 1.  Disclaimer: if you wish to debate the following, talk to Karl. :-)
 *     Order matters for updates because a multi-arg update is not
 *     atomic, and CVS users are used to, when doing 'cvs up targetA
 *     targetB' seeing targetA get updated, then targetB.  I think the
 *     idea is that if you're in a time-sensitive or flaky-network
 *     situation, a user can say, "I really *need* to update
 *     wc/A/D/G/tau, but I might as well update my whole working copy if
 *     I can."  So that user will do 'svn up wc/A/D/G/tau wc', and if
 *     something dies in the middles of the 'wc' update, at least the
 *     user has 'tau' up-to-date.
 *
 * 2.  Also, we have this notion of an anchor and a target for updates
 *     (the anchor is where the update editor is rooted, the target is
 *     the actual thing we want to update).  I needed a function that
 *     would NOT screw with my input paths so that I could tell the
 *     difference between someone being in A/D and saying 'svn up G' and
 *     being in A/D/G and saying 'svn up .' -- believe it or not, these
 *     two things don't mean the same thing.  svn_path_condense_targets()
 *     plays with absolute paths (which is fine, so does
 *     svn_path_remove_redundancies()), but the difference is that it
 *     actually tweaks those targets to be relative to the "grandfather
 *     path" common to all the targets.  Updates don't require a
 *     "grandfather path" at all, and even if it did, the whole
 *     conversion to an absolute path drops the crucial difference
 *     between saying "i'm in foo, update bar" and "i'm in foo/bar,
 *     update '.'"
 */
svn_error_t *
svn_path_remove_redundancies(apr_array_header_t **pcondensed_targets,
                             const apr_array_header_t *targets,
                             apr_pool_t *pool);


/** Decompose the canonicalized @a path into an array of <tt>const
 * char *</tt> components, allocated in @a pool.  If @a path is
 * absolute, the first component will be a lone dir separator (the
 * root directory).
 */
apr_array_header_t *
svn_path_decompose(const char *path, apr_pool_t *pool);

/** Join an array of <tt>const char *</tt> components into a '/'
 * separated path, allocated in @a pool.  The joined path is absolute if
 * the first component is a lone dir separator.
 *
 * Calling svn_path_compose() on the output of svn_path_decompose()
 * will return the exact same path.
 *
 * @since New in 1.5.
 */
const char *
svn_path_compose(const apr_array_header_t *components, apr_pool_t *pool);

/** Test that @a name is a single path component, that is:
 *   - not @c NULL or empty.
 *   - not a `/'-separated directory path
 *   - not empty or `..'
 */
svn_boolean_t
svn_path_is_single_path_component(const char *name);


/**
 * Test to see if a backpath, i.e. '..', is present in @a path.
 * If not, return @c FALSE.
 * If so, return @c TRUE.
 *
 * @since New in 1.1.
 */
svn_boolean_t
svn_path_is_backpath_present(const char *path);


/**
 * Test to see if a dotpath, i.e. '.', is present in @a path.
 * If not, return @c FALSE.
 * If so, return @c TRUE.
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_path_is_dotpath_present(const char *path);


/** Test if @a path2 is a child of @a path1.
 * If not, return @c NULL.
 * If so, return a copy of the remainder path, allocated in @a pool.
 * (The remainder is the component which, added to @a path1, yields
 * @a path2.  The remainder does not begin with a dir separator.)
 *
 * Both paths must be in canonical form, and must either be absolute,
 * or contain no ".." components.
 *
 * If @a path2 is the same as @a path1, it is not considered a child, so the
 * result is @c NULL; an empty string is never returned.
 *
 * @note In 1.5 this function has been extended to allow a @c NULL @a pool
 *       in which case a pointer into @a path2 will be returned to
 *       identify the remainder path.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * For replacement functionality, see svn_dirent_skip_ancestor(),
 * svn_dirent_is_child(), svn_uri_skip_ancestor(), and
 * svn_relpath_skip_ancestor().
 */
SVN_DEPRECATED
const char *
svn_path_is_child(const char *path1, const char *path2, apr_pool_t *pool);

/** Return TRUE if @a path1 is an ancestor of @a path2 or the paths are equal
 * and FALSE otherwise.
 *
 * @since New in 1.3.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * For replacement functionality, see svn_dirent_skip_ancestor(),
 * svn_uri_skip_ancestor(), and svn_relpath_skip_ancestor().
 */
SVN_DEPRECATED
svn_boolean_t
svn_path_is_ancestor(const char *path1, const char *path2);

/**
 * Check whether @a path is a valid Subversion path.
 *
 * A valid Subversion pathname is a UTF-8 string without control
 * characters.  "Valid" means Subversion can store the pathname in
 * a repository.  There may be other, OS-specific, limitations on
 * what paths can be represented in a working copy.
 *
 * ASSUMPTION: @a path is a valid UTF-8 string.  This function does
 * not check UTF-8 validity.
 *
 * Return @c SVN_NO_ERROR if valid and @c SVN_ERR_FS_PATH_SYNTAX if
 * invalid.
 *
 * @note Despite returning an @c SVN_ERR_FS_* error, this function has
 * nothing to do with the versioned filesystem's concept of validity.
 *
 * @since New in 1.2.
 */
svn_error_t *
svn_path_check_valid(const char *path, apr_pool_t *pool);


/** URI/URL stuff
 *
 * @defgroup svn_path_uri_stuff URI/URL conversion
 * @{
 */

/** Return TRUE iff @a path looks like a valid absolute URL. */
svn_boolean_t
svn_path_is_url(const char *path);

/** Return @c TRUE iff @a path is URI-safe, @c FALSE otherwise. */
svn_boolean_t
svn_path_is_uri_safe(const char *path);

/** Return a URI-encoded copy of @a path, allocated in @a pool.  (@a
    path can be an arbitrary UTF-8 string and does not have to be a
    canonical path.) */
const char *
svn_path_uri_encode(const char *path, apr_pool_t *pool);

/** Return a URI-decoded copy of @a path, allocated in @a pool. */
const char *
svn_path_uri_decode(const char *path, apr_pool_t *pool);

/** Extend @a url by @a component, URI-encoding that @a component
 * before adding it to the @a url; return the new @a url, allocated in
 * @a pool.  If @a component is @c NULL, just return a copy of @a url,
 * allocated in @a pool.
 *
 * @a component need not be a single path segment, but if it contains
 * multiple segments, they must be separated by '/'.  @a component
 * should not begin with '/', however; if it does, the behavior is
 * undefined.
 *
 * @a url must be in canonical format; it may not have a trailing '/'.
 *
 * @note To add a component that is already URI-encoded, use
 *       <tt>svn_path_join(url, component, pool)</tt> instead.
 *
 * @note gstein suggests this for when @a component begins with '/':
 *
 *       "replace the path entirely
 *        https://example.com:4444/base/path joined with /leading/slash,
 *        should return: https://example.com:4444/leading/slash
 *        per the RFCs on combining URIs"
 *
 *       We may implement that someday, which is why leading '/' is
 *       merely undefined right now.
 *
 * @since New in 1.6.
 */
const char *
svn_path_url_add_component2(const char *url,
                            const char *component,
                            apr_pool_t *pool);

/** Like svn_path_url_add_component2(), but allows path components that
 * end with a trailing '/'
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
const char *
svn_path_url_add_component(const char *url,
                           const char *component,
                           apr_pool_t *pool);

/**
 * Convert @a iri (Internationalized URI) to an URI.
 * The return value may be the same as @a iri if it was already
 * a URI.  Else, allocate the return value in @a pool.
 *
 * @since New in 1.1.
 */
const char *
svn_path_uri_from_iri(const char *iri, apr_pool_t *pool);

/**
 * URI-encode certain characters in @a uri that are not valid in an URI, but
 * doesn't have any special meaning in @a uri at their positions.  If no
 * characters need escaping, just return @a uri.
 *
 * @note Currently, this function escapes <, >, ", space, {, }, |, \, ^, and `.
 * This may be extended in the future to do context-dependent escaping.
 *
 * @since New in 1.1.
 */
const char *
svn_path_uri_autoescape(const char *uri, apr_pool_t *pool);

/** @} */

/** Charset conversion stuff
 *
 * @defgroup svn_path_charset_stuff Charset conversion
 * @{
 */

/** Convert @a path_utf8 from UTF-8 to the internal encoding used by APR. */
svn_error_t *
svn_path_cstring_from_utf8(const char **path_apr,
                           const char *path_utf8,
                           apr_pool_t *pool);

/** Convert @a path_apr from the internal encoding used by APR to UTF-8. */
svn_error_t *
svn_path_cstring_to_utf8(const char **path_utf8,
                         const char *path_apr,
                         apr_pool_t *pool);


/** @} */


/** Repository relative URLs
 *
 * @defgroup svn_path_repos_relative_urls Repository relative URLs
 * @{
 */

/**
 * Return @c TRUE iff @a path is a repository-relative URL:  specifically
 * that it starts with the characters "^/"
 *
 * @a path is in UTF-8 encoding.
 *
 * Does not check whether @a path is a properly URI-encoded, canonical, or
 * valid in any other way.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_path_is_repos_relative_url(const char *path);

/**
 * Set @a absolute_url to the absolute URL represented by @a relative_url
 * relative to @a repos_root_url, preserving any peg revision
 * specifier present in @a relative_url.  Allocate @a absolute_url
 * from @a pool.
 *
 * @a relative_url is in repository-relative syntax: "^/[REL-URL][@PEG]"
 *
 * @a repos_root_url is the absolute URL of the repository root.
 *
 * All strings are in UTF-8 encoding.
 *
 * @a repos_root_url and @a relative_url do not have to be properly
 * URI-encoded, canonical, or valid in any other way.  The caller is
 * expected to perform canonicalization on @a absolute_url after the
 * call to the function.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_path_resolve_repos_relative_url(const char **absolute_url,
                                    const char *relative_url,
                                    const char *repos_root_url,
                                    apr_pool_t *pool);

/** Return a copy of @a path, allocated from @a pool, for which control
 * characters have been escaped using the form "\NNN" (where NNN is the
 * octal representation of the byte's ordinal value).
 *
 * @since New in 1.8. */
const char *
svn_path_illegal_path_escape(const char *path, apr_pool_t *pool);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* SVN_PATH_H */
