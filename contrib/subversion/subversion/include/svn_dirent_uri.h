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
 * @file svn_dirent_uri.h
 * @brief A library to manipulate URIs, relative paths and directory entries.
 *
 * This library makes a clear distinction between several path formats:
 *
 *  - a dirent is a path on (local) disc or a UNC path (Windows) in
 *    either relative or absolute format.
 *    Examples:
 *       "/foo/bar", "X:/temp", "//server/share", "A:/" (Windows only), ""
 *    But not:
 *       "http://server"
 *
 *  - a uri, for our purposes, is a percent-encoded, absolute path
 *    (URI) that starts with a schema definition.  In practice, these
 *    tend to look like URLs, but never carry query strings.
 *    Examples:
 *       "http://server", "file:///path/to/repos",
 *       "svn+ssh://user@host:123/My%20Stuff/file.doc"
 *    But not:
 *       "file", "dir/file", "A:/dir", "/My%20Stuff/file.doc", ""
 *
 *  - a relative path (relpath) is an unrooted path that can be joined
 *    to any other relative path, uri or dirent. A relative path is
 *    never rooted/prefixed by a '/'.
 *    Examples:
 *       "file", "dir/file", "dir/subdir/../file", ""
 *    But not:
 *       "/file", "http://server/file"
 *
 * This distinction is needed because on Windows we have to handle some
 * dirents and URIs differently. Since it's not possible to determine from
 * the path string if it's a dirent or a URI, it's up to the API user to
 * make this choice. See also issue #2028.
 *
 * All incoming and outgoing paths are non-NULL unless otherwise documented.
 *
 * All of these functions expect paths passed into them to be in canonical
 * form, except:
 *
 *    - @c svn_dirent_canonicalize()
 *    - @c svn_dirent_is_canonical()
 *    - @c svn_dirent_internal_style()
 *    - @c svn_relpath_canonicalize()
 *    - @c svn_relpath_is_canonical()
 *    - @c svn_relpath__internal_style()
 *    - @c svn_uri_canonicalize()
 *    - @c svn_uri_is_canonical()
 *
 * The Subversion codebase also recognizes some other classes of path:
 *
 *  - A Subversion filesystem path (fspath) -- otherwise known as a
 *    path within a repository -- is a path relative to the root of
 *    the repository filesystem, that starts with a slash ("/").  The
 *    rules for a fspath are the same as for a relpath except for the
 *    leading '/'.  A fspath never ends with '/' except when the whole
 *    path is just '/'.  The fspath API is private (see
 *    private/svn_fspath.h).
 *
 *  - A URL path (urlpath) is just the path part of a URL (the part
 *    that follows the schema, username, hostname, and port).  These
 *    are also like relpaths, except that they have a leading slash
 *    (like fspaths) and are URI-encoded.  The urlpath API is also
 *    private (see private/svn_fspath.h)
 *    Example:
 *       "/svn/repos/trunk/README",
 *       "/svn/repos/!svn/bc/45/file%20with%20spaces.txt"
 *
 * So, which path API is appropriate for your use-case?
 *
 *  - If your path refers to a local file, directory, symlink, etc. of
 *    the sort that you can examine and operate on with other software
 *    on your computer, it's a dirent.
 *
 *  - If your path is a full URL -- with a schema, hostname (maybe),
 *    and path portion -- it's a uri.
 *
 *  - If your path is relative, and is somewhat ambiguous unless it's
 *    joined to some other more explicit (possible absolute) base
 *    (such as a dirent or URL), it's a relpath.
 *
 *  - If your path is the virtual path of a versioned object inside a
 *    Subversion repository, it could be one of two different types of
 *    paths.  We'd prefer to use relpaths (relative to the root
 *    directory of the virtual repository filesystem) for that stuff,
 *    but some legacy code uses fspaths.  You'll need to figure out if
 *    your code expects repository paths to have a leading '/' or not.
 *    If so, they are fspaths; otherwise they are relpaths.
 *
 *  - If your path refers only to the path part of URL -- as if
 *    someone hacked off the initial schema and hostname portion --
 *    it's a urlpath.  To date, the ra_dav modules are the only ones
 *    within Subversion that make use of urlpaths, and this is because
 *    WebDAV makes heavy use of that form of path specification.
 *
 * When translating between local paths (dirents) and uris code should
 * always go via the relative path format, perhaps by truncating a
 * parent portion from a path with svn_*_skip_ancestor(), or by
 * converting portions to basenames and then joining to existing
 * paths.
 *
 * SECURITY WARNING: If a path that is received from an untrusted
 * source -- such as from the network -- is converted to a dirent it
 * should be tested with svn_dirent_is_under_root() before you can
 * assume the path to be a safe local path.
 *
 * MEMORY ALLOCATION: A function documented as allocating the result
 * in a pool may instead return a static string such as "." or "". If
 * the result is equal to an input, it will duplicate the input.
 */

#ifndef SVN_DIRENT_URI_H
#define SVN_DIRENT_URI_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Convert @a dirent from the local style to the canonical internal style.
 * "Local style" means native path separators and "." for the empty path.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.6.
 */
const char *
svn_dirent_internal_style(const char *dirent,
                          apr_pool_t *result_pool);

/** Convert @a dirent from the internal style to the local style.
 * "Local style" means native path separators and "." for the empty path.
 * If the input is not canonical, the output may not be canonical.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.6.
 */
const char *
svn_dirent_local_style(const char *dirent,
                       apr_pool_t *result_pool);

/** Convert @a relpath from the local style to the canonical internal style.
 * "Local style" means native path separators and "." for the empty path.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
const char *
svn_relpath__internal_style(const char *relpath,
                            apr_pool_t *result_pool);


/** Join a base dirent (@a base) with a component (@a component).
 *
 * If either @a base or @a component is the empty string, then the other
 * argument will be copied and returned.  If both are the empty string then
 * empty string is returned.
 *
 * If the @a component is an absolute dirent, then it is copied and returned.
 * The platform specific rules for joining paths are used to join the components.
 *
 * This function is NOT appropriate for native (local) file
 * dirents. Only for "internal" canonicalized dirents, since it uses '/'
 * for the separator.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.6.
 */
char *
svn_dirent_join(const char *base,
                const char *component,
                apr_pool_t *result_pool);

/** Join multiple components onto a @a base dirent. The components are
 * terminated by a @c SVN_VA_NULL.
 *
 * If any component is the empty string, it will be ignored.
 *
 * If any component is an absolute dirent, then it resets the base and
 * further components will be appended to it.
 *
 * See svn_dirent_join() for further notes about joining dirents.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.6.
 */
char *
svn_dirent_join_many(apr_pool_t *result_pool,
                     const char *base,
                     ...) SVN_NEEDS_SENTINEL_NULL;

/** Join a base relpath (@a base) with a component (@a component).
 * @a component need not be a single component.
 *
 * If either @a base or @a component is the empty path, then the other
 * argument will be copied and returned.  If both are the empty path the
 * empty path is returned.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
char *
svn_relpath_join(const char *base,
                 const char *component,
                 apr_pool_t *result_pool);

/** Gets the name of the specified canonicalized @a dirent as it is known
 * within its parent directory. If the @a dirent is root, return "". The
 * returned value will not have slashes in it.
 *
 * Example: svn_dirent_basename("/foo/bar") -> "bar"
 *
 * If @a result_pool is NULL, return a pointer to the basename in @a dirent,
 * otherwise allocate the result in @a result_pool.
 *
 * @note If an empty string is passed, then an empty string will be returned.
 *
 * @since New in 1.7.
 */
const char *
svn_dirent_basename(const char *dirent,
                    apr_pool_t *result_pool);

/** Get the dirname of the specified canonicalized @a dirent, defined as
 * the dirent with its basename removed.
 *
 * If @a dirent is root  ("/", "X:/", "//server/share/") or "", it is returned
 * unchanged.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.6.
 */
char *
svn_dirent_dirname(const char *dirent,
                   apr_pool_t *result_pool);

/** Divide the canonicalized @a dirent into @a *dirpath and @a *base_name.
 *
 * If @a dirpath or @a base_name is NULL, then don't set that one.
 *
 * Either @a dirpath or @a base_name may be @a dirent's own address, but they
 * may not both be the same address, or the results are undefined.
 *
 * If @a dirent has two or more components, the separator between @a dirpath
 * and @a base_name is not included in either of the new names.
 *
 * Examples:
 *             - <pre>"/foo/bar/baz"  ==>  "/foo/bar" and "baz"</pre>
 *             - <pre>"/bar"          ==>  "/"  and "bar"</pre>
 *             - <pre>"/"             ==>  "/"  and ""</pre>
 *             - <pre>"bar"           ==>  ""   and "bar"</pre>
 *             - <pre>""              ==>  ""   and ""</pre>
 *  Windows:   - <pre>"X:/"           ==>  "X:/" and ""</pre>
 *             - <pre>"X:/foo"        ==>  "X:/" and "foo"</pre>
 *             - <pre>"X:foo"         ==>  "X:" and "foo"</pre>
 *  Posix:     - <pre>"X:foo"         ==>  ""   and "X:foo"</pre>
 *
 * Allocate the results in @a result_pool.
 *
 * @since New in 1.7.
 */
void
svn_dirent_split(const char **dirpath,
                 const char **base_name,
                 const char *dirent,
                 apr_pool_t *result_pool);

/** Divide the canonicalized @a relpath into @a *dirpath and @a *base_name.
 *
 * If @a dirpath or @a base_name is NULL, then don't set that one.
 *
 * Either @a dirpath or @a base_name may be @a relpaths's own address, but
 * they may not both be the same address, or the results are undefined.
 *
 * If @a relpath has two or more components, the separator between @a dirpath
 * and @a base_name is not included in either of the new names.
 *
 *   examples:
 *             - <pre>"foo/bar/baz"  ==>  "foo/bar" and "baz"</pre>
 *             - <pre>"bar"          ==>  ""  and "bar"</pre>
 *             - <pre>""              ==>  ""   and ""</pre>
 *
 * Allocate the results in @a result_pool.
 *
 * @since New in 1.7.
 */
void
svn_relpath_split(const char **dirpath,
                  const char **base_name,
                  const char *relpath,
                  apr_pool_t *result_pool);

/** Get the basename of the specified canonicalized @a relpath.  The
 * basename is defined as the last component of the relpath.  If the @a
 * relpath has only one component then that is returned. The returned
 * value will have no slashes in it.
 *
 * Example: svn_relpath_basename("/trunk/foo/bar") -> "bar"
 *
 * If @a result_pool is NULL, return a pointer to the basename in @a relpath,
 * otherwise allocate the result in @a result_pool.
 *
 * @note If an empty string is passed, then an empty string will be returned.
 *
 * @since New in 1.7.
 */
const char *
svn_relpath_basename(const char *relpath,
                     apr_pool_t *result_pool);

/** Get the dirname of the specified canonicalized @a relpath, defined as
 * the relpath with its basename removed.
 *
 * If @a relpath is empty, "" is returned.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
char *
svn_relpath_dirname(const char *relpath,
                    apr_pool_t *result_pool);

/** Return a maximum of @a max_components components of @a relpath. This is
 * an efficient way of calling svn_relpath_dirname() multiple times until only
 * a specific number of components is left.
 *
 * Allocate the result in @a result_pool (or statically in case of 0)
 *
 * @since New in 1.9.
 */
const char *
svn_relpath_prefix(const char *relpath,
                   int max_components,
                   apr_pool_t *result_pool);


/** Divide the canonicalized @a uri into a uri @a *dirpath and a
 * (URI-decoded) relpath @a *base_name.
 *
 * If @a dirpath or @a base_name is NULL, then don't set that one.
 *
 * Either @a dirpath or @a base_name may be @a uri's own address, but they
 * may not both be the same address, or the results are undefined.
 *
 * If @a uri has two or more components, the separator between @a dirpath
 * and @a base_name is not included in either of the new names.
 *
 * Examples:
 *   - <pre>"http://server/foo/bar"  ==>  "http://server/foo" and "bar"</pre>
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
void
svn_uri_split(const char **dirpath,
              const char **base_name,
              const char *uri,
              apr_pool_t *result_pool);

/** Get the (URI-decoded) basename of the specified canonicalized @a
 * uri.  The basename is defined as the last component of the uri.  If
 * the @a uri is root, return "".  The returned value will have no
 * slashes in it.
 *
 * Example: svn_uri_basename("http://server/foo/bar") -> "bar"
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
const char *
svn_uri_basename(const char *uri,
                 apr_pool_t *result_pool);

/** Get the dirname of the specified canonicalized @a uri, defined as
 * the uri with its basename removed.
 *
 * If @a uri is root (e.g. "http://server"), it is returned
 * unchanged.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
char *
svn_uri_dirname(const char *uri,
                apr_pool_t *result_pool);

/** Return TRUE if @a dirent is considered absolute on the platform at
 * hand. E.g. '/foo' on Posix platforms or 'X:/foo', '//server/share/foo'
 * on Windows.
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_dirent_is_absolute(const char *dirent);

/** Return TRUE if @a dirent is considered a root directory on the platform
 * at hand.
 * E.g.:
 *  On Posix:   '/'
 *  On Windows: '/', 'X:/', '//server/share', 'X:'
 *
 * Note that on Windows '/' and 'X:' are roots, but paths starting with this
 * root are not absolute.
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_dirent_is_root(const char *dirent,
                   apr_size_t len);

/** Return TRUE if @a uri is a root URL (e.g., "http://server").
 *
 * @since New in 1.7
 */
svn_boolean_t
svn_uri_is_root(const char *uri,
                apr_size_t len);

/** Return a new dirent like @a dirent, but transformed such that some types
 * of dirent specification redundancies are removed.
 *
 * This involves:
 *   - collapsing redundant "/./" elements
 *   - removing multiple adjacent separator characters
 *   - removing trailing separator characters
 *   - converting the server name of a UNC path to lower case (on Windows)
 *   - converting a drive letter to upper case (on Windows)
 *
 * and possibly other semantically inoperative transformations.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.6.
 */
const char *
svn_dirent_canonicalize(const char *dirent,
                        apr_pool_t *result_pool);


/** Return a new relpath like @a relpath, but transformed such that some types
 * of relpath specification redundancies are removed.
 *
 * This involves:
 *   - collapsing redundant "/./" elements
 *   - removing multiple adjacent separator characters
 *   - removing trailing separator characters
 *
 * and possibly other semantically inoperative transformations.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
const char *
svn_relpath_canonicalize(const char *relpath,
                         apr_pool_t *result_pool);


/** Return a new uri like @a uri, but transformed such that some types
 * of uri specification redundancies are removed.
 *
 * This involves:
 *   - collapsing redundant "/./" elements
 *   - removing multiple adjacent separator characters
 *   - removing trailing separator characters
 *   - normalizing the escaping of the path component by unescaping
 *     characters that don't need escaping and escaping characters that do
 *     need escaping but weren't
 *   - removing the port number if it is the default port number (80 for
 *     http, 443 for https, 3690 for svn)
 *
 * and possibly other semantically inoperative transformations.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
const char *
svn_uri_canonicalize(const char *uri,
                     apr_pool_t *result_pool);

/** Return @c TRUE iff @a dirent is canonical.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @note The test for canonicalization is currently defined as
 * "looks exactly the same as @c svn_dirent_canonicalize() would make
 * it look".
 *
 * @see svn_dirent_canonicalize()
 * @since New in 1.6.
 */
svn_boolean_t
svn_dirent_is_canonical(const char *dirent,
                        apr_pool_t *scratch_pool);

/** Return @c TRUE iff @a relpath is canonical.
 *
 * @see svn_relpath_canonicalize()
 * @since New in 1.7.
 */
svn_boolean_t
svn_relpath_is_canonical(const char *relpath);

/** Return @c TRUE iff @a uri is canonical.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @see svn_uri_canonicalize()
 * @since New in 1.7.
 */
svn_boolean_t
svn_uri_is_canonical(const char *uri,
                     apr_pool_t *scratch_pool);

/** Return the longest common dirent shared by two canonicalized dirents,
 * @a dirent1 and @a dirent2.  If there's no common ancestor, return the
 * empty path.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.6.
 */
char *
svn_dirent_get_longest_ancestor(const char *dirent1,
                                const char *dirent2,
                                apr_pool_t *result_pool);

/** Return the longest common path shared by two relative paths,
 * @a relpath1 and @a relpath2.  If there's no common ancestor, return the
 * empty path.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
char *
svn_relpath_get_longest_ancestor(const char *relpath1,
                                 const char *relpath2,
                                 apr_pool_t *result_pool);

/** Return the longest common path shared by two canonicalized uris,
 * @a uri1 and @a uri2.  If there's no common ancestor, return the
 * empty path.  In order for two URLs to have a common ancestor, they
 * must (a) have the same protocol (since two URLs with the same path
 * but different protocols may point at completely different
 * resources), and (b) share a common ancestor in their path
 * component, i.e. 'protocol://' is not a sufficient ancestor.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
char *
svn_uri_get_longest_ancestor(const char *uri1,
                             const char *uri2,
                             apr_pool_t *result_pool);

/** Convert @a relative canonicalized dirent to an absolute dirent and
 * return the results in @a *pabsolute.
 * Raise SVN_ERR_BAD_FILENAME if the absolute dirent cannot be determined.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_dirent_get_absolute(const char **pabsolute,
                        const char *relative,
                        apr_pool_t *result_pool);

/** Similar to svn_dirent_skip_ancestor(), except that if @a child_dirent is
 * the same as @a parent_dirent, it is not considered a child, so the result
 * is @c NULL; an empty string is never returned.
 *
 * If @a result_pool is NULL, return a pointer into @a child_dirent, otherwise
 * allocate the result in @a result_pool.
 *
 * ### TODO: Deprecate, as the semantics are trivially
 * obtainable from *_skip_ancestor().
 *
 * @since New in 1.6.
 */
const char *
svn_dirent_is_child(const char *parent_dirent,
                    const char *child_dirent,
                    apr_pool_t *result_pool);

/** Return TRUE if @a parent_dirent is an ancestor of @a child_dirent or
 * the dirents are equal, and FALSE otherwise.
 *
 * ### TODO: Deprecate, as the semantics are trivially
 * obtainable from *_skip_ancestor().
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_dirent_is_ancestor(const char *parent_dirent,
                       const char *child_dirent);

/** Return TRUE if @a parent_uri is an ancestor of @a child_uri or
 * the uris are equal, and FALSE otherwise.
 */
svn_boolean_t
svn_uri__is_ancestor(const char *parent_uri,
                     const char *child_uri);


/** Return the relative path part of @a child_dirent that is below
 * @a parent_dirent, or just "" if @a parent_dirent is equal to
 * @a child_dirent. If @a child_dirent is not below or equal to
 * @a parent_dirent, return NULL.
 *
 * If one of @a parent_dirent and @a child_dirent is absolute and
 * the other relative, return NULL.
 *
 * @since New in 1.7.
 */
const char *
svn_dirent_skip_ancestor(const char *parent_dirent,
                         const char *child_dirent);

/** Return the relative path part of @a child_relpath that is below
 * @a parent_relpath, or just "" if @a parent_relpath is equal to
 * @a child_relpath. If @a child_relpath is not below @a parent_relpath,
 * return NULL.
 *
 * @since New in 1.7.
 */
const char *
svn_relpath_skip_ancestor(const char *parent_relpath,
                          const char *child_relpath);

/** Return the URI-decoded relative path of @a child_uri that is below
 * @a parent_uri, or just "" if @a parent_uri is equal to @a child_uri. If
 * @a child_uri is not below @a parent_uri, return NULL.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
const char *
svn_uri_skip_ancestor(const char *parent_uri,
                      const char *child_uri,
                      apr_pool_t *result_pool);

/** Find the common prefix of the canonicalized dirents in @a targets
 * (an array of <tt>const char *</tt>'s), and remove redundant dirents if @a
 * remove_redundancies is TRUE.
 *
 *   - Set @a *pcommon to the absolute dirent of the dirent common to
 *     all of the targets.  If the targets have no common prefix (e.g.
 *     "C:/file" and "D:/file" on Windows), set @a *pcommon to the empty
 *     string.
 *
 *   - If @a pcondensed_targets is non-NULL, set @a *pcondensed_targets
 *     to an array of targets relative to @a *pcommon, and if
 *     @a remove_redundancies is TRUE, omit any dirents that are
 *     descendants of another dirent in @a targets.  If *pcommon
 *     is empty, @a *pcondensed_targets will contain absolute dirents;
 *     redundancies can still be removed.  If @a pcondensed_targets is NULL,
 *     leave it alone.
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
 * Allocate the results in @a result_pool. Use @a scratch_pool for
 * temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_dirent_condense_targets(const char **pcommon,
                            apr_array_header_t **pcondensed_targets,
                            const apr_array_header_t *targets,
                            svn_boolean_t remove_redundancies,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/** Find the common prefix of the canonicalized uris in @a targets
 * (an array of <tt>const char *</tt>'s), and remove redundant uris if @a
 * remove_redundancies is TRUE.
 *
 *   - Set @a *pcommon to the common base uri of all of the targets.
 *     If the targets have no common prefix (e.g. "http://srv1/file"
 *     and "http://srv2/file"), set @a *pcommon to the empty
 *     string.
 *
 *   - If @a pcondensed_targets is non-NULL, set @a *pcondensed_targets
 *     to an array of URI-decoded targets relative to @a *pcommon, and
 *     if @a remove_redundancies is TRUE, omit any uris that are
 *     descendants of another uri in @a targets.  If *pcommon is
 *     empty, @a *pcondensed_targets will contain absolute uris;
 *     redundancies can still be removed.  If @a pcondensed_targets is
 *     NULL, leave it alone.
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
 * Allocate the results in @a result_pool. Use @a scratch_pool for
 * temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_uri_condense_targets(const char **pcommon,
                         apr_array_header_t **pcondensed_targets,
                         const apr_array_header_t *targets,
                         svn_boolean_t remove_redundancies,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

/** Join @a path onto @a base_path, checking that @a path does not attempt
 * to traverse above @a base_path. If @a path or any ".." component within
 * it resolves to a path above @a base_path, or if @a path is an absolute
 * path, then set @a *under_root to @c FALSE. Otherwise, set @a *under_root
 * to @c TRUE and, if @a result_path is not @c NULL, set @a *result_path to
 * the resulting path.
 *
 * @a path need not be canonical. @a base_path must be canonical and
 * @a *result_path will be canonical.
 *
 * Allocate the result in @a result_pool.
 *
 * @note Use of this function is strongly encouraged. Do not roll your own.
 * (http://cve.mitre.org/cgi-bin/cvename.cgi?name=2007-3846)
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_dirent_is_under_root(svn_boolean_t *under_root,
                         const char **result_path,
                         const char *base_path,
                         const char *path,
                         apr_pool_t *result_pool);

/** Set @a *dirent to the path corresponding to the file:// URL @a url, using
 * the platform-specific file:// rules.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_uri_get_dirent_from_file_url(const char **dirent,
                                 const char *url,
                                 apr_pool_t *result_pool);

/** Set @a *url to a file:// URL, corresponding to @a dirent using the
 * platform specific dirent and file:// rules.
 *
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_uri_get_file_url_from_dirent(const char **url,
                                 const char *dirent,
                                 apr_pool_t *result_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DIRENT_URI_H */
