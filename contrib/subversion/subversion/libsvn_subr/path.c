/*
 * paths.c:   a path manipulation library using svn_stringbuf_t
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



#include <string.h>
#include <assert.h>

#include <apr_file_info.h>
#include <apr_lib.h>
#include <apr_uri.h>

#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_private_config.h"         /* for SVN_PATH_LOCAL_SEPARATOR */
#include "svn_utf.h"
#include "svn_io.h"                     /* for svn_io_stat() */
#include "svn_ctype.h"

#include "dirent_uri.h"


/* The canonical empty path.  Can this be changed?  Well, change the empty
   test below and the path library will work, not so sure about the fs/wc
   libraries. */
#define SVN_EMPTY_PATH ""

/* TRUE if s is the canonical empty path, FALSE otherwise */
#define SVN_PATH_IS_EMPTY(s) ((s)[0] == '\0')

/* TRUE if s,n is the platform's empty path ("."), FALSE otherwise. Can
   this be changed?  Well, the path library will work, not so sure about
   the OS! */
#define SVN_PATH_IS_PLATFORM_EMPTY(s,n) ((n) == 1 && (s)[0] == '.')




#ifndef NDEBUG
/* This function is an approximation of svn_path_is_canonical.
 * It is supposed to be used in functions that do not have access
 * to a pool, but still want to assert that a path is canonical.
 *
 * PATH with length LEN is assumed to be canonical if it isn't
 * the platform's empty path (see definition of SVN_PATH_IS_PLATFORM_EMPTY),
 * and does not contain "/./", and any one of the following
 * conditions is also met:
 *
 *  1. PATH has zero length
 *  2. PATH is the root directory (what exactly a root directory is
 *                                depends on the platform)
 *  3. PATH is not a root directory and does not end with '/'
 *
 * If possible, please use svn_path_is_canonical instead.
 */
static svn_boolean_t
is_canonical(const char *path,
             apr_size_t len)
{
  return (! SVN_PATH_IS_PLATFORM_EMPTY(path, len)
          && strstr(path, "/./") == NULL
          && (len == 0
              || (len == 1 && path[0] == '/')
              || (path[len-1] != '/')
#if defined(WIN32) || defined(__CYGWIN__)
              || svn_dirent_is_root(path, len)
#endif
              ));
}
#endif


/* functionality of svn_path_is_canonical but without the deprecation */
static svn_boolean_t
svn_path_is_canonical_internal(const char *path, apr_pool_t *pool)
{
  return svn_uri_is_canonical(path, pool) ||
      svn_dirent_is_canonical(path, pool) ||
      svn_relpath_is_canonical(path);
}

svn_boolean_t
svn_path_is_canonical(const char *path, apr_pool_t *pool)
{
  return svn_path_is_canonical_internal(path, pool);
}

/* functionality of svn_path_join but without the deprecation */
static char *
svn_path_join_internal(const char *base,
                       const char *component,
                       apr_pool_t *pool)
{
  apr_size_t blen = strlen(base);
  apr_size_t clen = strlen(component);
  char *path;

  assert(svn_path_is_canonical_internal(base, pool));
  assert(svn_path_is_canonical_internal(component, pool));

  /* If the component is absolute, then return it.  */
  if (*component == '/')
    return apr_pmemdup(pool, component, clen + 1);

  /* If either is empty return the other */
  if (SVN_PATH_IS_EMPTY(base))
    return apr_pmemdup(pool, component, clen + 1);
  if (SVN_PATH_IS_EMPTY(component))
    return apr_pmemdup(pool, base, blen + 1);

  if (blen == 1 && base[0] == '/')
    blen = 0; /* Ignore base, just return separator + component */

  /* Construct the new, combined path. */
  path = apr_palloc(pool, blen + 1 + clen + 1);
  memcpy(path, base, blen);
  path[blen] = '/';
  memcpy(path + blen + 1, component, clen + 1);

  return path;
}

char *svn_path_join(const char *base,
                    const char *component,
                    apr_pool_t *pool)
{
  return svn_path_join_internal(base, component, pool);
}

char *svn_path_join_many(apr_pool_t *pool, const char *base, ...)
{
#define MAX_SAVED_LENGTHS 10
  apr_size_t saved_lengths[MAX_SAVED_LENGTHS];
  apr_size_t total_len;
  int nargs;
  va_list va;
  const char *s;
  apr_size_t len;
  char *path;
  char *p;
  svn_boolean_t base_is_empty = FALSE, base_is_root = FALSE;
  int base_arg = 0;

  total_len = strlen(base);

  assert(svn_path_is_canonical_internal(base, pool));

  if (total_len == 1 && *base == '/')
    base_is_root = TRUE;
  else if (SVN_PATH_IS_EMPTY(base))
    {
      total_len = sizeof(SVN_EMPTY_PATH) - 1;
      base_is_empty = TRUE;
    }

  saved_lengths[0] = total_len;

  /* Compute the length of the resulting string. */

  nargs = 0;
  va_start(va, base);
  while ((s = va_arg(va, const char *)) != NULL)
    {
      len = strlen(s);

      assert(svn_path_is_canonical_internal(s, pool));

      if (SVN_PATH_IS_EMPTY(s))
        continue;

      if (nargs++ < MAX_SAVED_LENGTHS)
        saved_lengths[nargs] = len;

      if (*s == '/')
        {
          /* an absolute path. skip all components to this point and reset
             the total length. */
          total_len = len;
          base_arg = nargs;
          base_is_root = len == 1;
          base_is_empty = FALSE;
        }
      else if (nargs == base_arg
               || (nargs == base_arg + 1 && base_is_root)
               || base_is_empty)
        {
          /* if we have skipped everything up to this arg, then the base
             and all prior components are empty. just set the length to
             this component; do not add a separator.  If the base is empty
             we can now ignore it. */
          if (base_is_empty)
            {
              base_is_empty = FALSE;
              total_len = 0;
            }
          total_len += len;
        }
      else
        {
          total_len += 1 + len;
        }
    }
  va_end(va);

  /* base == "/" and no further components. just return that. */
  if (base_is_root && total_len == 1)
    return apr_pmemdup(pool, "/", 2);

  /* we got the total size. allocate it, with room for a NULL character. */
  path = p = apr_palloc(pool, total_len + 1);

  /* if we aren't supposed to skip forward to an absolute component, and if
     this is not an empty base that we are skipping, then copy the base
     into the output. */
  if (base_arg == 0 && ! (SVN_PATH_IS_EMPTY(base) && ! base_is_empty))
    {
      if (SVN_PATH_IS_EMPTY(base))
        memcpy(p, SVN_EMPTY_PATH, len = saved_lengths[0]);
      else
        memcpy(p, base, len = saved_lengths[0]);
      p += len;
    }

  nargs = 0;
  va_start(va, base);
  while ((s = va_arg(va, const char *)) != NULL)
    {
      if (SVN_PATH_IS_EMPTY(s))
        continue;

      if (++nargs < base_arg)
        continue;

      if (nargs < MAX_SAVED_LENGTHS)
        len = saved_lengths[nargs];
      else
        len = strlen(s);

      /* insert a separator if we aren't copying in the first component
         (which can happen when base_arg is set). also, don't put in a slash
         if the prior character is a slash (occurs when prior component
         is "/"). */
      if (p != path && p[-1] != '/')
        *p++ = '/';

      /* copy the new component and advance the pointer */
      memcpy(p, s, len);
      p += len;
    }
  va_end(va);

  *p = '\0';
  assert((apr_size_t)(p - path) == total_len);

  return path;
}



apr_size_t
svn_path_component_count(const char *path)
{
  apr_size_t count = 0;

  assert(is_canonical(path, strlen(path)));

  while (*path)
    {
      const char *start;

      while (*path == '/')
        ++path;

      start = path;

      while (*path && *path != '/')
        ++path;

      if (path != start)
        ++count;
    }

  return count;
}


/* Return the length of substring necessary to encompass the entire
 * previous path segment in PATH, which should be a LEN byte string.
 *
 * A trailing slash will not be included in the returned length except
 * in the case in which PATH is absolute and there are no more
 * previous segments.
 */
static apr_size_t
previous_segment(const char *path,
                 apr_size_t len)
{
  if (len == 0)
    return 0;

  while (len > 0 && path[--len] != '/')
    ;

  if (len == 0 && path[0] == '/')
    return 1;
  else
    return len;
}


void
svn_path_add_component(svn_stringbuf_t *path,
                       const char *component)
{
  apr_size_t len = strlen(component);

  assert(is_canonical(path->data, path->len));
  assert(is_canonical(component, strlen(component)));

  /* Append a dir separator, but only if this path is neither empty
     nor consists of a single dir separator already. */
  if ((! SVN_PATH_IS_EMPTY(path->data))
      && (! ((path->len == 1) && (*(path->data) == '/'))))
    {
      char dirsep = '/';
      svn_stringbuf_appendbytes(path, &dirsep, sizeof(dirsep));
    }

  svn_stringbuf_appendbytes(path, component, len);
}


void
svn_path_remove_component(svn_stringbuf_t *path)
{
  assert(is_canonical(path->data, path->len));

  path->len = previous_segment(path->data, path->len);
  path->data[path->len] = '\0';
}


void
svn_path_remove_components(svn_stringbuf_t *path, apr_size_t n)
{
  while (n > 0)
    {
      svn_path_remove_component(path);
      n--;
    }
}


char *
svn_path_dirname(const char *path, apr_pool_t *pool)
{
  apr_size_t len = strlen(path);

  assert(svn_path_is_canonical_internal(path, pool));

  return apr_pstrmemdup(pool, path, previous_segment(path, len));
}


char *
svn_path_basename(const char *path, apr_pool_t *pool)
{
  apr_size_t len = strlen(path);
  apr_size_t start;

  assert(svn_path_is_canonical_internal(path, pool));

  if (len == 1 && path[0] == '/')
    start = 0;
  else
    {
      start = len;
      while (start > 0 && path[start - 1] != '/')
        --start;
    }

  return apr_pstrmemdup(pool, path + start, len - start);
}

int
svn_path_is_empty(const char *path)
{
  assert(is_canonical(path, strlen(path)));

  if (SVN_PATH_IS_EMPTY(path))
    return 1;

  return 0;
}

int
svn_path_compare_paths(const char *path1,
                       const char *path2)
{
  apr_size_t path1_len = strlen(path1);
  apr_size_t path2_len = strlen(path2);
  apr_size_t min_len = ((path1_len < path2_len) ? path1_len : path2_len);
  apr_size_t i = 0;

  assert(is_canonical(path1, path1_len));
  assert(is_canonical(path2, path2_len));

  /* Skip past common prefix. */
  while (i < min_len && path1[i] == path2[i])
    ++i;

  /* Are the paths exactly the same? */
  if ((path1_len == path2_len) && (i >= min_len))
    return 0;

  /* Children of paths are greater than their parents, but less than
     greater siblings of their parents. */
  if ((path1[i] == '/') && (path2[i] == 0))
    return 1;
  if ((path2[i] == '/') && (path1[i] == 0))
    return -1;
  if (path1[i] == '/')
    return -1;
  if (path2[i] == '/')
    return 1;

  /* Common prefix was skipped above, next character is compared to
     determine order.  We need to use an unsigned comparison, though,
     so a "next character" of NULL (0x00) sorts numerically
     smallest. */
  return (unsigned char)(path1[i]) < (unsigned char)(path2[i]) ? -1 : 1;
}

/* Return the string length of the longest common ancestor of PATH1 and PATH2.
 *
 * This function handles everything except the URL-handling logic
 * of svn_path_get_longest_ancestor, and assumes that PATH1 and
 * PATH2 are *not* URLs.
 *
 * If the two paths do not share a common ancestor, return 0.
 *
 * New strings are allocated in POOL.
 */
static apr_size_t
get_path_ancestor_length(const char *path1,
                         const char *path2,
                         apr_pool_t *pool)
{
  apr_size_t path1_len, path2_len;
  apr_size_t i = 0;
  apr_size_t last_dirsep = 0;

  path1_len = strlen(path1);
  path2_len = strlen(path2);

  if (SVN_PATH_IS_EMPTY(path1) || SVN_PATH_IS_EMPTY(path2))
    return 0;

  while (path1[i] == path2[i])
    {
      /* Keep track of the last directory separator we hit. */
      if (path1[i] == '/')
        last_dirsep = i;

      i++;

      /* If we get to the end of either path, break out. */
      if ((i == path1_len) || (i == path2_len))
        break;
    }

  /* two special cases:
     1. '/' is the longest common ancestor of '/' and '/foo'
     2. '/' is the longest common ancestor of '/rif' and '/raf' */
  if (i == 1 && path1[0] == '/' && path2[0] == '/')
    return 1;

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte. */
  if (((i == path1_len) && (path2[i] == '/'))
           || ((i == path2_len) && (path1[i] == '/'))
           || ((i == path1_len) && (i == path2_len)))
    return i;
  else
    if (last_dirsep == 0 && path1[0] == '/' && path2[0] == '/')
      return 1;
  return last_dirsep;
}


char *
svn_path_get_longest_ancestor(const char *path1,
                              const char *path2,
                              apr_pool_t *pool)
{
  svn_boolean_t path1_is_url = svn_path_is_url(path1);
  svn_boolean_t path2_is_url = svn_path_is_url(path2);

  /* Are we messing with URLs?  If we have a mix of URLs and non-URLs,
     there's nothing common between them.  */
  if (path1_is_url && path2_is_url)
    {
      return svn_uri_get_longest_ancestor(path1, path2, pool);
    }
  else if ((! path1_is_url) && (! path2_is_url))
    {
      return apr_pstrndup(pool, path1,
                          get_path_ancestor_length(path1, path2, pool));
    }
  else
    {
      /* A URL and a non-URL => no common prefix */
      return apr_pmemdup(pool, SVN_EMPTY_PATH, sizeof(SVN_EMPTY_PATH));
    }
}

const char *
svn_path_is_child(const char *path1,
                  const char *path2,
                  apr_pool_t *pool)
{
  apr_size_t i;

  /* assert (is_canonical (path1, strlen (path1)));  ### Expensive strlen */
  /* assert (is_canonical (path2, strlen (path2)));  ### Expensive strlen */

  /* Allow "" and "foo" to be parent/child */
  if (SVN_PATH_IS_EMPTY(path1))               /* "" is the parent  */
    {
      if (SVN_PATH_IS_EMPTY(path2)            /* "" not a child    */
          || path2[0] == '/')                  /* "/foo" not a child */
        return NULL;
      else
        /* everything else is child */
        return pool ? apr_pstrdup(pool, path2) : path2;
    }

  /* Reach the end of at least one of the paths.  How should we handle
     things like path1:"foo///bar" and path2:"foo/bar/baz"?  It doesn't
     appear to arise in the current Subversion code, it's not clear to me
     if they should be parent/child or not. */
  for (i = 0; path1[i] && path2[i]; i++)
    if (path1[i] != path2[i])
      return NULL;

  /* There are two cases that are parent/child
          ...      path1[i] == '\0'
          .../foo  path2[i] == '/'
      or
          /        path1[i] == '\0'
          /foo     path2[i] != '/'
  */
  if (path1[i] == '\0' && path2[i])
    {
      if (path2[i] == '/')
        return pool ? apr_pstrdup(pool, path2 + i + 1) : path2 + i + 1;
      else if (i == 1 && path1[0] == '/')
        return pool ? apr_pstrdup(pool, path2 + 1) : path2 + 1;
    }

  /* Otherwise, path2 isn't a child. */
  return NULL;
}


svn_boolean_t
svn_path_is_ancestor(const char *path1, const char *path2)
{
  apr_size_t path1_len = strlen(path1);

  /* If path1 is empty and path2 is not absoulte, then path1 is an ancestor. */
  if (SVN_PATH_IS_EMPTY(path1))
    return *path2 != '/';

  /* If path1 is a prefix of path2, then:
     - If path1 ends in a path separator,
     - If the paths are of the same length
     OR
     - path2 starts a new path component after the common prefix,
     then path1 is an ancestor. */
  if (strncmp(path1, path2, path1_len) == 0)
    return path1[path1_len - 1] == '/'
      || (path2[path1_len] == '/' || path2[path1_len] == '\0');

  return FALSE;
}


apr_array_header_t *
svn_path_decompose(const char *path,
                   apr_pool_t *pool)
{
  apr_size_t i, oldi;

  apr_array_header_t *components =
    apr_array_make(pool, 1, sizeof(const char *));

  assert(svn_path_is_canonical_internal(path, pool));

  if (SVN_PATH_IS_EMPTY(path))
    return components;  /* ### Should we return a "" component? */

  /* If PATH is absolute, store the '/' as the first component. */
  i = oldi = 0;
  if (path[i] == '/')
    {
      char dirsep = '/';

      APR_ARRAY_PUSH(components, const char *)
        = apr_pstrmemdup(pool, &dirsep, sizeof(dirsep));

      i++;
      oldi++;
      if (path[i] == '\0') /* path is a single '/' */
        return components;
    }

  do
    {
      if ((path[i] == '/') || (path[i] == '\0'))
        {
          if (SVN_PATH_IS_PLATFORM_EMPTY(path + oldi, i - oldi))
            APR_ARRAY_PUSH(components, const char *) = SVN_EMPTY_PATH;
          else
            APR_ARRAY_PUSH(components, const char *)
              = apr_pstrmemdup(pool, path + oldi, i - oldi);

          i++;
          oldi = i;  /* skipping past the dirsep */
          continue;
        }
      i++;
    }
  while (path[i-1]);

  return components;
}


const char *
svn_path_compose(const apr_array_header_t *components,
                 apr_pool_t *pool)
{
  apr_size_t *lengths = apr_palloc(pool, components->nelts*sizeof(*lengths));
  apr_size_t max_length = components->nelts;
  char *path;
  char *p;
  int i;

  /* Get the length of each component so a total length can be
     calculated. */
  for (i = 0; i < components->nelts; ++i)
    {
      apr_size_t l = strlen(APR_ARRAY_IDX(components, i, const char *));
      lengths[i] = l;
      max_length += l;
    }

  path = apr_palloc(pool, max_length + 1);
  p = path;

  for (i = 0; i < components->nelts; ++i)
    {
      /* Append a '/' to the path.  Handle the case with an absolute
         path where a '/' appears in the first component.  Only append
         a '/' if the component is the second component that does not
         follow a "/" first component; or it is the third or later
         component. */
      if (i > 1 ||
          (i == 1 && strcmp("/", APR_ARRAY_IDX(components,
                                               0,
                                               const char *)) != 0))
        {
          *p++ = '/';
        }

      memcpy(p, APR_ARRAY_IDX(components, i, const char *), lengths[i]);
      p += lengths[i];
    }

  *p = '\0';

  return path;
}


svn_boolean_t
svn_path_is_single_path_component(const char *name)
{
  assert(is_canonical(name, strlen(name)));

  /* Can't be empty or `..'  */
  if (SVN_PATH_IS_EMPTY(name)
      || (name[0] == '.' && name[1] == '.' && name[2] == '\0'))
    return FALSE;

  /* Slashes are bad, m'kay... */
  if (strchr(name, '/') != NULL)
    return FALSE;

  /* It is valid.  */
  return TRUE;
}


svn_boolean_t
svn_path_is_dotpath_present(const char *path)
{
  size_t len;

  /* The empty string does not have a dotpath */
  if (path[0] == '\0')
    return FALSE;

  /* Handle "." or a leading "./" */
  if (path[0] == '.' && (path[1] == '\0' || path[1] == '/'))
    return TRUE;

  /* Paths of length 1 (at this point) have no dotpath present. */
  if (path[1] == '\0')
    return FALSE;

  /* If any segment is "/./", then a dotpath is present. */
  if (strstr(path, "/./") != NULL)
    return TRUE;

  /* Does the path end in "/." ? */
  len = strlen(path);
  return path[len - 2] == '/' && path[len - 1] == '.';
}

svn_boolean_t
svn_path_is_backpath_present(const char *path)
{
  size_t len;

  /* 0 and 1-length paths do not have a backpath */
  if (path[0] == '\0' || path[1] == '\0')
    return FALSE;

  /* Handle ".." or a leading "../" */
  if (path[0] == '.' && path[1] == '.' && (path[2] == '\0' || path[2] == '/'))
    return TRUE;

  /* Paths of length 2 (at this point) have no backpath present. */
  if (path[2] == '\0')
    return FALSE;

  /* If any segment is "..", then a backpath is present. */
  if (strstr(path, "/../") != NULL)
    return TRUE;

  /* Does the path end in "/.." ? */
  len = strlen(path);
  return path[len - 3] == '/' && path[len - 2] == '.' && path[len - 1] == '.';
}


/*** URI Stuff ***/

/* Examine PATH as a potential URI, and return a substring of PATH
   that immediately follows the (scheme):// portion of the URI, or
   NULL if PATH doesn't appear to be a valid URI.  The returned value
   is not alloced -- it shares memory with PATH. */
static const char *
skip_uri_scheme(const char *path)
{
  apr_size_t j;

  /* A scheme is terminated by a : and cannot contain any /'s. */
  for (j = 0; path[j] && path[j] != ':'; ++j)
    if (path[j] == '/')
      return NULL;

  if (j > 0 && path[j] == ':' && path[j+1] == '/' && path[j+2] == '/')
    return path + j + 3;

  return NULL;
}


svn_boolean_t
svn_path_is_url(const char *path)
{
  /* ### This function is reaaaaaaaaaaaaaally stupid right now.
     We're just going to look for:

        (scheme)://(optional_stuff)

     Where (scheme) has no ':' or '/' characters.

     Someday it might be nice to have an actual URI parser here.
  */
  return skip_uri_scheme(path) != NULL;
}



/* Here is the BNF for path components in a URI. "pchar" is a
   character in a path component.

      pchar       = unreserved | escaped |
                    ":" | "@" | "&" | "=" | "+" | "$" | ","
      unreserved  = alphanum | mark
      mark        = "-" | "_" | "." | "!" | "~" | "*" | "'" | "(" | ")"

   Note that "escaped" doesn't really apply to what users can put in
   their paths, so that really means the set of characters is:

      alphanum | mark | ":" | "@" | "&" | "=" | "+" | "$" | ","
*/
const char svn_uri__char_validity[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 1, 0, 0, 1, 0, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 1, 0, 0,

  /* 64 */
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,

  /* 128 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,

  /* 192 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};


svn_boolean_t
svn_path_is_uri_safe(const char *path)
{
  apr_size_t i;

  /* Skip the URI scheme. */
  path = skip_uri_scheme(path);

  /* No scheme?  Get outta here. */
  if (! path)
    return FALSE;

  /* Skip to the first slash that's after the URI scheme. */
  path = strchr(path, '/');

  /* If there's no first slash, then there's only a host portion;
     therefore there couldn't be any uri-unsafe characters after the
     host... so return true. */
  if (path == NULL)
    return TRUE;

  for (i = 0; path[i]; i++)
    {
      /* Allow '%XX' (where each X is a hex digit) */
      if (path[i] == '%')
        {
          if (svn_ctype_isxdigit(path[i + 1]) &&
              svn_ctype_isxdigit(path[i + 2]))
            {
              i += 2;
              continue;
            }
          return FALSE;
        }
      else if (! svn_uri__char_validity[((unsigned char)path[i])])
        {
          return FALSE;
        }
    }

  return TRUE;
}


/* URI-encode each character c in PATH for which TABLE[c] is 0.
   If no encoding was needed, return PATH, else return a new string allocated
   in POOL. */
static const char *
uri_escape(const char *path, const char table[], apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i, copied = 0;
  int c;
  apr_size_t len;
  const char *p;

  /* To terminate our scanning loop, table[NUL] must report "invalid". */
  assert(table[0] == 0);

  /* Quick check: Does any character need escaping? */
  for (p = path; table[(unsigned char)*p]; ++p)
    {}

  /* No char to escape before EOS? */
  if (*p == '\0')
    return path;

  /* We need to escape at least one character. */
  len = strlen(p) + (p - path);
  retstr = svn_stringbuf_create_ensure(len, pool);
  for (i = p - path; i < len; i++)
    {
      c = (unsigned char)path[i];
      if (table[c])
        continue;

      /* If we got here, we're looking at a character that isn't
         supported by the (or at least, our) URI encoding scheme.  We
         need to escape this character.  */

      /* First things first, copy all the good stuff that we haven't
         yet copied into our output buffer. */
      if (i - copied)
        svn_stringbuf_appendbytes(retstr, path + copied,
                                  i - copied);

      /* Now, write in our escaped character, consisting of the
         '%' and two digits.  We cast the C to unsigned char here because
         the 'X' format character will be tempted to treat it as an unsigned
         int...which causes problem when messing with 0x80-0xFF chars.
         We also need space for a null as apr_snprintf will write one. */
      svn_stringbuf_ensure(retstr, retstr->len + 4);
      apr_snprintf(retstr->data + retstr->len, 4, "%%%02X", (unsigned char)c);
      retstr->len += 3;

      /* Finally, update our copy counter. */
      copied = i + 1;
    }

  /* Anything left to copy? */
  if (i - copied)
    svn_stringbuf_appendbytes(retstr, path + copied, i - copied);

  /* retstr is null-terminated either by apr_snprintf or the svn_stringbuf
     functions. */

  return retstr->data;
}


const char *
svn_path_uri_encode(const char *path, apr_pool_t *pool)
{
  const char *ret;

  ret = uri_escape(path, svn_uri__char_validity, pool);

  /* Our interface guarantees a copy. */
  if (ret == path)
    return apr_pstrdup(pool, path);
  else
    return ret;
}

static const char iri_escape_chars[256] = {
  0, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,

  /* 128 */
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0
};

const char *
svn_path_uri_from_iri(const char *iri, apr_pool_t *pool)
{
  return uri_escape(iri, iri_escape_chars, pool);
}

static const char uri_autoescape_chars[256] = {
  0, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  0, 1, 0, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 0, 1, 0, 1,

  /* 64 */
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 0, 1, 0, 1,
  0, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 0, 0, 0, 1, 1,

  /* 128 */
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,

  /* 192 */
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
};

const char *
svn_path_uri_autoescape(const char *uri, apr_pool_t *pool)
{
  return uri_escape(uri, uri_autoescape_chars, pool);
}

const char *
svn_path_uri_decode(const char *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i;
  svn_boolean_t query_start = FALSE;

  /* avoid repeated realloc */
  retstr = svn_stringbuf_create_ensure(strlen(path) + 1, pool);

  retstr->len = 0;
  for (i = 0; path[i]; i++)
    {
      char c = path[i];

      if (c == '?')
        {
          /* Mark the start of the query string, if it exists. */
          query_start = TRUE;
        }
      else if (c == '+' && query_start)
        {
          /* Only do this if we are into the query string.
           * RFC 2396, section 3.3  */
          c = ' ';
        }
      else if (c == '%' && svn_ctype_isxdigit(path[i + 1])
               && svn_ctype_isxdigit(path[i+2]))
        {
          char digitz[3];
          digitz[0] = path[++i];
          digitz[1] = path[++i];
          digitz[2] = '\0';
          c = (char)(strtol(digitz, NULL, 16));
        }

      retstr->data[retstr->len++] = c;
    }

  /* Null-terminate this bad-boy. */
  retstr->data[retstr->len] = 0;

  return retstr->data;
}


const char *
svn_path_url_add_component2(const char *url,
                            const char *component,
                            apr_pool_t *pool)
{
  /* = svn_path_uri_encode() but without always copying */
  component = uri_escape(component, svn_uri__char_validity, pool);

  return svn_path_join_internal(url, component, pool);
}

svn_error_t *
svn_path_get_absolute(const char **pabsolute,
                      const char *relative,
                      apr_pool_t *pool)
{
  if (svn_path_is_url(relative))
    {
      *pabsolute = apr_pstrdup(pool, relative);
      return SVN_NO_ERROR;
    }

  return svn_dirent_get_absolute(pabsolute, relative, pool);
}


#if !defined(WIN32) && !defined(DARWIN)
/** Get APR's internal path encoding. */
static svn_error_t *
get_path_encoding(svn_boolean_t *path_is_utf8, apr_pool_t *pool)
{
  apr_status_t apr_err;
  int encoding_style;

  apr_err = apr_filepath_encoding(&encoding_style, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              _("Can't determine the native path encoding"));

  /* ### What to do about APR_FILEPATH_ENCODING_UNKNOWN?
     Well, for now we'll just punt to the svn_utf_ functions;
     those will at least do the ASCII-subset check. */
  *path_is_utf8 = (encoding_style == APR_FILEPATH_ENCODING_UTF8);
  return SVN_NO_ERROR;
}
#endif


svn_error_t *
svn_path_cstring_from_utf8(const char **path_apr,
                           const char *path_utf8,
                           apr_pool_t *pool)
{
#if !defined(WIN32) && !defined(DARWIN)
  svn_boolean_t path_is_utf8;
  SVN_ERR(get_path_encoding(&path_is_utf8, pool));
  if (path_is_utf8)
#endif
    {
      *path_apr = apr_pstrdup(pool, path_utf8);
      return SVN_NO_ERROR;
    }
#if !defined(WIN32) && !defined(DARWIN)
  else
    return svn_utf_cstring_from_utf8(path_apr, path_utf8, pool);
#endif
}


svn_error_t *
svn_path_cstring_to_utf8(const char **path_utf8,
                         const char *path_apr,
                         apr_pool_t *pool)
{
#if !defined(WIN32) && !defined(DARWIN)
  svn_boolean_t path_is_utf8;
  SVN_ERR(get_path_encoding(&path_is_utf8, pool));
  if (path_is_utf8)
#endif
    {
      *path_utf8 = apr_pstrdup(pool, path_apr);
      return SVN_NO_ERROR;
    }
#if !defined(WIN32) && !defined(DARWIN)
  else
    return svn_utf_cstring_to_utf8(path_utf8, path_apr, pool);
#endif
}


const char *
svn_path_illegal_path_escape(const char *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i, copied = 0;
  int c;

  /* At least one control character:
      strlen - 1 (control) + \ + N + N + N + null . */
  retstr = svn_stringbuf_create_ensure(strlen(path) + 4, pool);
  for (i = 0; path[i]; i++)
    {
      c = (unsigned char)path[i];
      if (! svn_ctype_iscntrl(c))
        continue;

      /* If we got here, we're looking at a character that isn't
         supported by the (or at least, our) URI encoding scheme.  We
         need to escape this character.  */

      /* First things first, copy all the good stuff that we haven't
         yet copied into our output buffer. */
      if (i - copied)
        svn_stringbuf_appendbytes(retstr, path + copied,
                                  i - copied);

      /* Make sure buffer is big enough for '\' 'N' 'N' 'N' (and NUL) */
      svn_stringbuf_ensure(retstr, retstr->len + 5);
      /*### The backslash separator doesn't work too great with Windows,
         but it's what we'll use for consistency with invalid utf8
         formatting (until someone has a better idea) */
      apr_snprintf(retstr->data + retstr->len, 5, "\\%03o", (unsigned char)c);
      retstr->len += 4;

      /* Finally, update our copy counter. */
      copied = i + 1;
    }

  /* If we didn't encode anything, we don't need to duplicate the string. */
  if (retstr->len == 0)
    return path;

  /* Anything left to copy? */
  if (i - copied)
    svn_stringbuf_appendbytes(retstr, path + copied, i - copied);

  /* retstr is null-terminated either by apr_snprintf or the svn_stringbuf
     functions. */

  return retstr->data;
}

svn_error_t *
svn_path_check_valid(const char *path, apr_pool_t *pool)
{
  const char *c;

  for (c = path; *c; c++)
    {
      if (svn_ctype_iscntrl(*c))
        {
          return svn_error_createf(SVN_ERR_FS_PATH_SYNTAX, NULL,
             _("Invalid control character '0x%02x' in path '%s'"),
             (unsigned char)*c,
             svn_path_illegal_path_escape(svn_dirent_local_style(path, pool),
                                          pool));
        }
    }

  return SVN_NO_ERROR;
}

void
svn_path_splitext(const char **path_root,
                  const char **path_ext,
                  const char *path,
                  apr_pool_t *pool)
{
  const char *last_dot, *last_slash;

  /* Easy out -- why do all the work when there's no way to report it? */
  if (! (path_root || path_ext))
    return;

  /* Do we even have a period in this thing?  And if so, is there
     anything after it?  We look for the "rightmost" period in the
     string. */
  last_dot = strrchr(path, '.');
  if (last_dot && (*(last_dot + 1) != '\0'))
    {
      /* If we have a period, we need to make sure it occurs in the
         final path component -- that there's no path separator
         between the last period and the end of the PATH -- otherwise,
         it doesn't count.  Also, we want to make sure that our period
         isn't the first character of the last component. */
      last_slash = strrchr(path, '/');
      if ((last_slash && (last_dot > (last_slash + 1)))
          || ((! last_slash) && (last_dot > path)))
        {
          if (path_root)
            *path_root = apr_pstrmemdup(pool, path,
                                        (last_dot - path + 1) * sizeof(*path));
          if (path_ext)
            *path_ext = apr_pstrdup(pool, last_dot + 1);
          return;
        }
    }
  /* If we get here, we never found a suitable separator character, so
     there's no split. */
  if (path_root)
    *path_root = apr_pstrdup(pool, path);
  if (path_ext)
    *path_ext = "";
}


/* Repository relative URLs (^/). */

svn_boolean_t
svn_path_is_repos_relative_url(const char *path)
{
  return (0 == strncmp("^/", path, 2));
}

svn_error_t *
svn_path_resolve_repos_relative_url(const char **absolute_url,
                                    const char *relative_url,
                                    const char *repos_root_url,
                                    apr_pool_t *pool)
{
  if (! svn_path_is_repos_relative_url(relative_url))
    return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                             _("Improper relative URL '%s'"),
                             relative_url);

  /* No assumptions are made about the canonicalization of the input
   * arguments, it is presumed that the output will be canonicalized after
   * this function, which will remove any duplicate path separator.
   */
  *absolute_url = apr_pstrcat(pool, repos_root_url, relative_url + 1,
                              SVN_VA_NULL);

  return SVN_NO_ERROR;
}

