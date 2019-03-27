/*
 * dirent_uri.c:   a library to manipulate URIs and directory entries.
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
#include <ctype.h>

#include <apr_uri.h>
#include <apr_lib.h>

#include "svn_private_config.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_ctype.h"

#include "dirent_uri.h"
#include "private/svn_fspath.h"
#include "private/svn_cert.h"

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

/* This check must match the check on top of dirent_uri-tests.c and
   path-tests.c */
#if defined(WIN32) || defined(__CYGWIN__) || defined(__OS2__)
#define SVN_USE_DOS_PATHS
#endif

/* Path type definition. Used only by internal functions. */
typedef enum path_type_t {
  type_uri,
  type_dirent,
  type_relpath
} path_type_t;


/**** Forward declarations *****/

static svn_boolean_t
relpath_is_canonical(const char *relpath);


/**** Internal implementation functions *****/

/* Return an internal-style new path based on PATH, allocated in POOL.
 *
 * "Internal-style" means that separators are all '/'.
 */
static const char *
internal_style(const char *path, apr_pool_t *pool)
{
#if '/' != SVN_PATH_LOCAL_SEPARATOR
    {
      char *p = apr_pstrdup(pool, path);
      path = p;

      /* Convert all local-style separators to the canonical ones. */
      for (; *p != '\0'; ++p)
        if (*p == SVN_PATH_LOCAL_SEPARATOR)
          *p = '/';
    }
#endif

  return path;
}

/* Locale insensitive tolower() for converting parts of dirents and urls
   while canonicalizing */
static char
canonicalize_to_lower(char c)
{
  if (c < 'A' || c > 'Z')
    return c;
  else
    return (char)(c - 'A' + 'a');
}

/* Locale insensitive toupper() for converting parts of dirents and urls
   while canonicalizing */
static char
canonicalize_to_upper(char c)
{
  if (c < 'a' || c > 'z')
    return c;
  else
    return (char)(c - 'a' + 'A');
}

/* Calculates the length of the dirent absolute or non absolute root in
   DIRENT, return 0 if dirent is not rooted  */
static apr_size_t
dirent_root_length(const char *dirent, apr_size_t len)
{
#ifdef SVN_USE_DOS_PATHS
  if (len >= 2 && dirent[1] == ':' &&
      ((dirent[0] >= 'A' && dirent[0] <= 'Z') ||
       (dirent[0] >= 'a' && dirent[0] <= 'z')))
    {
      return (len > 2 && dirent[2] == '/') ? 3 : 2;
    }

  if (len > 2 && dirent[0] == '/' && dirent[1] == '/')
    {
      apr_size_t i = 2;

      while (i < len && dirent[i] != '/')
        i++;

      if (i == len)
        return len; /* Cygwin drive alias, invalid path on WIN32 */

      i++; /* Skip '/' */

      while (i < len && dirent[i] != '/')
        i++;

      return i;
    }
#endif /* SVN_USE_DOS_PATHS */
  if (len >= 1 && dirent[0] == '/')
    return 1;

  return 0;
}


/* Return the length of substring necessary to encompass the entire
 * previous dirent segment in DIRENT, which should be a LEN byte string.
 *
 * A trailing slash will not be included in the returned length except
 * in the case in which DIRENT is absolute and there are no more
 * previous segments.
 */
static apr_size_t
dirent_previous_segment(const char *dirent,
                        apr_size_t len)
{
  if (len == 0)
    return 0;

  --len;
  while (len > 0 && dirent[len] != '/'
#ifdef SVN_USE_DOS_PATHS
                 && (dirent[len] != ':' || len != 1)
#endif /* SVN_USE_DOS_PATHS */
        )
    --len;

  /* check if the remaining segment including trailing '/' is a root dirent */
  if (dirent_root_length(dirent, len+1) == len + 1)
    return len + 1;
  else
    return len;
}

/* Calculates the length occupied by the schema defined root of URI */
static apr_size_t
uri_schema_root_length(const char *uri, apr_size_t len)
{
  apr_size_t i;

  for (i = 0; i < len; i++)
    {
      if (uri[i] == '/')
        {
          if (i > 0 && uri[i-1] == ':' && i < len-1 && uri[i+1] == '/')
            {
              /* We have an absolute uri */
              if (i == 5 && strncmp("file", uri, 4) == 0)
                return 7; /* file:// */
              else
                {
                  for (i += 2; i < len; i++)
                    if (uri[i] == '/')
                      return i;

                  return len; /* Only a hostname is found */
                }
            }
          else
            return 0;
        }
    }

  return 0;
}

/* Returns TRUE if svn_dirent_is_absolute(dirent) or when dirent has
   a non absolute root. (E.g. '/' or 'F:' on Windows) */
static svn_boolean_t
dirent_is_rooted(const char *dirent)
{
  if (! dirent)
    return FALSE;

  /* Root on all systems */
  if (dirent[0] == '/')
    return TRUE;

  /* On Windows, dirent is also absolute when it starts with 'H:' or 'H:/'
     where 'H' is any letter. */
#ifdef SVN_USE_DOS_PATHS
  if (((dirent[0] >= 'A' && dirent[0] <= 'Z') ||
       (dirent[0] >= 'a' && dirent[0] <= 'z')) &&
      (dirent[1] == ':'))
     return TRUE;
#endif /* SVN_USE_DOS_PATHS */

  return FALSE;
}

/* Return the length of substring necessary to encompass the entire
 * previous relpath segment in RELPATH, which should be a LEN byte string.
 *
 * A trailing slash will not be included in the returned length.
 */
static apr_size_t
relpath_previous_segment(const char *relpath,
                         apr_size_t len)
{
  if (len == 0)
    return 0;

  --len;
  while (len > 0 && relpath[len] != '/')
    --len;

  return len;
}

/* Return the length of substring necessary to encompass the entire
 * previous uri segment in URI, which should be a LEN byte string.
 *
 * A trailing slash will not be included in the returned length except
 * in the case in which URI is absolute and there are no more
 * previous segments.
 */
static apr_size_t
uri_previous_segment(const char *uri,
                     apr_size_t len)
{
  apr_size_t root_length;
  apr_size_t i = len;
  if (len == 0)
    return 0;

  root_length = uri_schema_root_length(uri, len);

  --i;
  while (len > root_length && uri[i] != '/')
    --i;

  if (i == 0 && len > 1 && *uri == '/')
    return 1;

  return i;
}

/* Return the canonicalized version of PATH, of type TYPE, allocated in
 * POOL.
 */
static const char *
canonicalize(path_type_t type, const char *path, apr_pool_t *pool)
{
  char *canon, *dst;
  const char *src;
  apr_size_t seglen;
  apr_size_t schemelen = 0;
  apr_size_t canon_segments = 0;
  svn_boolean_t url = FALSE;
  char *schema_data = NULL;

  /* "" is already canonical, so just return it; note that later code
     depends on path not being zero-length.  */
  if (SVN_PATH_IS_EMPTY(path))
    {
      assert(type != type_uri);
      return "";
    }

  dst = canon = apr_pcalloc(pool, strlen(path) + 1);

  /* If this is supposed to be an URI, it should start with
     "scheme://".  We'll copy the scheme, host name, etc. to DST and
     set URL = TRUE. */
  src = path;
  if (type == type_uri)
    {
      assert(*src != '/');

      while (*src && (*src != '/') && (*src != ':'))
        src++;

      if (*src == ':' && *(src+1) == '/' && *(src+2) == '/')
        {
          const char *seg;

          url = TRUE;

          /* Found a scheme, convert to lowercase and copy to dst. */
          src = path;
          while (*src != ':')
            {
              *(dst++) = canonicalize_to_lower((*src++));
              schemelen++;
            }
          *(dst++) = ':';
          *(dst++) = '/';
          *(dst++) = '/';
          src += 3;
          schemelen += 3;

          /* This might be the hostname */
          seg = src;
          while (*src && (*src != '/') && (*src != '@'))
            src++;

          if (*src == '@')
            {
              /* Copy the username & password. */
              seglen = src - seg + 1;
              memcpy(dst, seg, seglen);
              dst += seglen;
              src++;
            }
          else
            src = seg;

          /* Found a hostname, convert to lowercase and copy to dst. */
          if (*src == '[')
            {
             *(dst++) = *(src++); /* Copy '[' */

              while (*src == ':'
                     || (*src >= '0' && (*src <= '9'))
                     || (*src >= 'a' && (*src <= 'f'))
                     || (*src >= 'A' && (*src <= 'F')))
                {
                  *(dst++) = canonicalize_to_lower((*src++));
                }

              if (*src == ']')
                *(dst++) = *(src++); /* Copy ']' */
            }
          else
            while (*src && (*src != '/') && (*src != ':'))
              *(dst++) = canonicalize_to_lower((*src++));

          if (*src == ':')
            {
              /* We probably have a port number: Is it a default portnumber
                 which doesn't belong in a canonical url? */
              if (src[1] == '8' && src[2] == '0'
                  && (src[3]== '/'|| !src[3])
                  && !strncmp(canon, "http:", 5))
                {
                  src += 3;
                }
              else if (src[1] == '4' && src[2] == '4' && src[3] == '3'
                       && (src[4]== '/'|| !src[4])
                       && !strncmp(canon, "https:", 6))
                {
                  src += 4;
                }
              else if (src[1] == '3' && src[2] == '6'
                       && src[3] == '9' && src[4] == '0'
                       && (src[5]== '/'|| !src[5])
                       && !strncmp(canon, "svn:", 4))
                {
                  src += 5;
                }
              else if (src[1] == '/' || !src[1])
                {
                  src += 1;
                }

              while (*src && (*src != '/'))
                *(dst++) = canonicalize_to_lower((*src++));
            }

          /* Copy trailing slash, or null-terminator. */
          *(dst) = *(src);

          /* Move src and dst forward only if we are not
           * at null-terminator yet. */
          if (*src)
            {
              src++;
              dst++;
              schema_data = dst;
            }

          canon_segments = 1;
        }
    }

  /* Copy to DST any separator or drive letter that must come before the
     first regular path segment. */
  if (! url && type != type_relpath)
    {
      src = path;
      /* If this is an absolute path, then just copy over the initial
         separator character. */
      if (*src == '/')
        {
          *(dst++) = *(src++);

#ifdef SVN_USE_DOS_PATHS
          /* On Windows permit two leading separator characters which means an
           * UNC path. */
          if ((type == type_dirent) && *src == '/')
            *(dst++) = *(src++);
#endif /* SVN_USE_DOS_PATHS */
        }
#ifdef SVN_USE_DOS_PATHS
      /* On Windows the first segment can be a drive letter, which we normalize
         to upper case. */
      else if (type == type_dirent &&
               ((*src >= 'a' && *src <= 'z') ||
                (*src >= 'A' && *src <= 'Z')) &&
               (src[1] == ':'))
        {
          *(dst++) = canonicalize_to_upper(*(src++));
          /* Leave the ':' to be processed as (or as part of) a path segment
             by the following code block, so we need not care whether it has
             a slash after it. */
        }
#endif /* SVN_USE_DOS_PATHS */
    }

  while (*src)
    {
      /* Parse each segment, finding the closing '/' (which might look
         like '%2F' for URIs).  */
      const char *next = src;
      apr_size_t slash_len = 0;

      while (*next
             && (next[0] != '/')
             && (! (type == type_uri && next[0] == '%' && next[1] == '2' &&
                    canonicalize_to_upper(next[2]) == 'F')))
        {
          ++next;
        }

      /* Record how long our "slash" is. */
      if (next[0] == '/')
        slash_len = 1;
      else if (type == type_uri && next[0] == '%')
        slash_len = 3;

      seglen = next - src;

      if (seglen == 0
          || (seglen == 1 && src[0] == '.')
          || (type == type_uri && seglen == 3 && src[0] == '%' && src[1] == '2'
              && canonicalize_to_upper(src[2]) == 'E'))
        {
          /* Empty or noop segment, so do nothing.  (For URIs, '%2E'
             is equivalent to '.').  */
        }
#ifdef SVN_USE_DOS_PATHS
      /* If this is the first path segment of a file:// URI and it contains a
         windows drive letter, convert the drive letter to upper case. */
      else if (url && canon_segments == 1 && seglen >= 2 &&
               (strncmp(canon, "file:", 5) == 0) &&
               src[0] >= 'a' && src[0] <= 'z' && src[1] == ':')
        {
          *(dst++) = canonicalize_to_upper(src[0]);
          *(dst++) = ':';
          if (seglen > 2) /* drive relative path */
            {
              memcpy(dst, src + 2, seglen - 2);
              dst += seglen - 2;
            }

          if (slash_len)
            *(dst++) = '/';
          canon_segments++;
        }
#endif /* SVN_USE_DOS_PATHS */
      else
        {
          /* An actual segment, append it to the destination path */
          memcpy(dst, src, seglen);
          dst += seglen;
          if (slash_len)
            *(dst++) = '/';
          canon_segments++;
        }

      /* Skip over trailing slash to the next segment. */
      src = next + slash_len;
    }

  /* Remove the trailing slash if there was at least one
   * canonical segment and the last segment ends with a slash.
   *
   * But keep in mind that, for URLs, the scheme counts as a
   * canonical segment -- so if path is ONLY a scheme (such
   * as "https://") we should NOT remove the trailing slash. */
  if ((canon_segments > 0 && *(dst - 1) == '/')
      && ! (url && path[schemelen] == '\0'))
    {
      dst --;
    }

  *dst = '\0';

#ifdef SVN_USE_DOS_PATHS
  /* Skip leading double slashes when there are less than 2
   * canon segments. UNC paths *MUST* have two segments. */
  if ((type == type_dirent) && canon[0] == '/' && canon[1] == '/')
    {
      if (canon_segments < 2)
        return canon + 1;
      else
        {
          /* Now we're sure this is a valid UNC path, convert the server name
             (the first path segment) to lowercase as Windows treats it as case
             insensitive.
             Note: normally the share name is treated as case insensitive too,
             but it seems to be possible to configure Samba to treat those as
             case sensitive, so better leave that alone. */
          for (dst = canon + 2; *dst && *dst != '/'; dst++)
            *dst = canonicalize_to_lower(*dst);
        }
    }
#endif /* SVN_USE_DOS_PATHS */

  /* Check the normalization of characters in a uri */
  if (schema_data)
    {
      int need_extra = 0;
      src = schema_data;

      while (*src)
        {
          switch (*src)
            {
              case '/':
                break;
              case '%':
                if (!svn_ctype_isxdigit(*(src+1)) ||
                    !svn_ctype_isxdigit(*(src+2)))
                  need_extra += 2;
                else
                  src += 2;
                break;
              default:
                if (!svn_uri__char_validity[(unsigned char)*src])
                  need_extra += 2;
                break;
            }
          src++;
        }

      if (need_extra > 0)
        {
          apr_size_t pre_schema_size = (apr_size_t)(schema_data - canon);

          dst = apr_palloc(pool, (apr_size_t)(src - canon) + need_extra + 1);
          memcpy(dst, canon, pre_schema_size);
          canon = dst;

          dst += pre_schema_size;
        }
      else
        dst = schema_data;

      src = schema_data;

      while (*src)
        {
          switch (*src)
            {
              case '/':
                *(dst++) = '/';
                break;
              case '%':
                if (!svn_ctype_isxdigit(*(src+1)) ||
                    !svn_ctype_isxdigit(*(src+2)))
                  {
                    *(dst++) = '%';
                    *(dst++) = '2';
                    *(dst++) = '5';
                  }
                else
                  {
                    char digitz[3];
                    int val;

                    digitz[0] = *(++src);
                    digitz[1] = *(++src);
                    digitz[2] = 0;

                    val = (int)strtol(digitz, NULL, 16);

                    if (svn_uri__char_validity[(unsigned char)val])
                      *(dst++) = (char)val;
                    else
                      {
                        *(dst++) = '%';
                        *(dst++) = canonicalize_to_upper(digitz[0]);
                        *(dst++) = canonicalize_to_upper(digitz[1]);
                      }
                  }
                break;
              default:
                if (!svn_uri__char_validity[(unsigned char)*src])
                  {
                    apr_snprintf(dst, 4, "%%%02X", (unsigned char)*src);
                    dst += 3;
                  }
                else
                  *(dst++) = *src;
                break;
            }
          src++;
        }
      *dst = '\0';
    }

  return canon;
}

/* Return the string length of the longest common ancestor of PATH1 and PATH2.
 * Pass type_uri for TYPE if PATH1 and PATH2 are URIs, and type_dirent if
 * PATH1 and PATH2 are regular paths.
 *
 * If the two paths do not share a common ancestor, return 0.
 *
 * New strings are allocated in POOL.
 */
static apr_size_t
get_longest_ancestor_length(path_type_t types,
                            const char *path1,
                            const char *path2,
                            apr_pool_t *pool)
{
  apr_size_t path1_len, path2_len;
  apr_size_t i = 0;
  apr_size_t last_dirsep = 0;
#ifdef SVN_USE_DOS_PATHS
  svn_boolean_t unc = FALSE;
#endif

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
     1. '/' is the longest common ancestor of '/' and '/foo' */
  if (i == 1 && path1[0] == '/' && path2[0] == '/')
    return 1;
  /* 2. '' is the longest common ancestor of any non-matching
   * strings 'foo' and 'bar' */
  if (types == type_dirent && i == 0)
    return 0;

  /* Handle some windows specific cases */
#ifdef SVN_USE_DOS_PATHS
  if (types == type_dirent)
    {
      /* don't count the '//' from UNC paths */
      if (last_dirsep == 1 && path1[0] == '/' && path1[1] == '/')
        {
          last_dirsep = 0;
          unc = TRUE;
        }

      /* X:/ and X:/foo */
      if (i == 3 && path1[2] == '/' && path1[1] == ':')
        return i;

      /* Cannot use SVN_ERR_ASSERT here, so we'll have to crash, sorry.
       * Note that this assertion triggers only if the code above has
       * been broken. The code below relies on this assertion, because
       * it uses [i - 1] as index. */
      assert(i > 0);

      /* X: and X:/ */
      if ((path1[i - 1] == ':' && path2[i] == '/') ||
          (path2[i - 1] == ':' && path1[i] == '/'))
          return 0;
      /* X: and X:foo */
      if (path1[i - 1] == ':' || path2[i - 1] == ':')
          return i;
    }
#endif /* SVN_USE_DOS_PATHS */

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte, and is guaranteed to be <= the length of
     whichever path is shorter.
     If one of the paths is the common part return that. */
  if (((i == path1_len) && (path2[i] == '/'))
           || ((i == path2_len) && (path1[i] == '/'))
           || ((i == path1_len) && (i == path2_len)))
    return i;
  else
    {
      /* Nothing in common but the root folder '/' or 'X:/' for Windows
         dirents. */
#ifdef SVN_USE_DOS_PATHS
      if (! unc)
        {
          /* X:/foo and X:/bar returns X:/ */
          if ((types == type_dirent) &&
              last_dirsep == 2 && path1[1] == ':' && path1[2] == '/'
                               && path2[1] == ':' && path2[2] == '/')
            return 3;
#endif /* SVN_USE_DOS_PATHS */
          if (last_dirsep == 0 && path1[0] == '/' && path2[0] == '/')
            return 1;
#ifdef SVN_USE_DOS_PATHS
        }
#endif
    }

  return last_dirsep;
}

/* Determine whether PATH2 is a child of PATH1.
 *
 * PATH2 is a child of PATH1 if
 * 1) PATH1 is empty, and PATH2 is not empty and not an absolute path.
 * or
 * 2) PATH2 is has n components, PATH1 has x < n components,
 *    and PATH1 matches PATH2 in all its x components.
 *    Components are separated by a slash, '/'.
 *
 * Pass type_uri for TYPE if PATH1 and PATH2 are URIs, and type_dirent if
 * PATH1 and PATH2 are regular paths.
 *
 * If PATH2 is not a child of PATH1, return NULL.
 *
 * If PATH2 is a child of PATH1, and POOL is not NULL, allocate a copy
 * of the child part of PATH2 in POOL and return a pointer to the
 * newly allocated child part.
 *
 * If PATH2 is a child of PATH1, and POOL is NULL, return a pointer
 * pointing to the child part of PATH2.
 * */
static const char *
is_child(path_type_t type, const char *path1, const char *path2,
         apr_pool_t *pool)
{
  apr_size_t i;

  /* Allow "" and "foo" or "H:foo" to be parent/child */
  if (SVN_PATH_IS_EMPTY(path1))               /* "" is the parent  */
    {
      if (SVN_PATH_IS_EMPTY(path2))            /* "" not a child    */
        return NULL;

      /* check if this is an absolute path */
      if ((type == type_uri) ||
          (type == type_dirent && dirent_is_rooted(path2)))
        return NULL;
      else
        /* everything else is child */
        return pool ? apr_pstrdup(pool, path2) : path2;
    }

  /* Reach the end of at least one of the paths.  How should we handle
     things like path1:"foo///bar" and path2:"foo/bar/baz"?  It doesn't
     appear to arise in the current Subversion code, it's not clear to me
     if they should be parent/child or not. */
  /* Hmmm... aren't paths assumed to be canonical in this function?
   * How can "foo///bar" even happen if the paths are canonical? */
  for (i = 0; path1[i] && path2[i]; i++)
    if (path1[i] != path2[i])
      return NULL;

  /* FIXME: This comment does not really match
   * the checks made in the code it refers to: */
  /* There are two cases that are parent/child
          ...      path1[i] == '\0'
          .../foo  path2[i] == '/'
      or
          /        path1[i] == '\0'
          /foo     path2[i] != '/'

     Other root paths (like X:/) fall under the former case:
          X:/        path1[i] == '\0'
          X:/foo     path2[i] != '/'

     Check for '//' to avoid matching '/' and '//srv'.
  */
  if (path1[i] == '\0' && path2[i])
    {
      if (path1[i - 1] == '/'
#ifdef SVN_USE_DOS_PATHS
          || ((type == type_dirent) && path1[i - 1] == ':')
#endif
           )
        {
          if (path2[i] == '/')
            /* .../
             * ..../
             *     i   */
            return NULL;
          else
            /* .../
             * .../foo
             *     i    */
            return pool ? apr_pstrdup(pool, path2 + i) : path2 + i;
        }
      else if (path2[i] == '/')
        {
          if (path2[i + 1])
            /* ...
             * .../foo
             *    i    */
            return pool ? apr_pstrdup(pool, path2 + i + 1) : path2 + i + 1;
          else
            /* ...
             * .../
             *    i    */
            return NULL;
        }
    }

  /* Otherwise, path2 isn't a child. */
  return NULL;
}


/**** Public API functions ****/

const char *
svn_dirent_internal_style(const char *dirent, apr_pool_t *pool)
{
  return svn_dirent_canonicalize(internal_style(dirent, pool), pool);
}

const char *
svn_dirent_local_style(const char *dirent, apr_pool_t *pool)
{
  /* Internally, Subversion represents the current directory with the
     empty string.  But users like to see "." . */
  if (SVN_PATH_IS_EMPTY(dirent))
    return ".";

#if '/' != SVN_PATH_LOCAL_SEPARATOR
    {
      char *p = apr_pstrdup(pool, dirent);
      dirent = p;

      /* Convert all canonical separators to the local-style ones. */
      for (; *p != '\0'; ++p)
        if (*p == '/')
          *p = SVN_PATH_LOCAL_SEPARATOR;
    }
#endif

  return dirent;
}

const char *
svn_relpath__internal_style(const char *relpath,
                            apr_pool_t *pool)
{
  return svn_relpath_canonicalize(internal_style(relpath, pool), pool);
}


/* We decided against using apr_filepath_root here because of the negative
   performance impact (creating a pool and converting strings ). */
svn_boolean_t
svn_dirent_is_root(const char *dirent, apr_size_t len)
{
#ifdef SVN_USE_DOS_PATHS
  /* On Windows and Cygwin, 'H:' or 'H:/' (where 'H' is any letter)
     are also root directories */
  if ((len == 2 || ((len == 3) && (dirent[2] == '/'))) &&
      (dirent[1] == ':') &&
      ((dirent[0] >= 'A' && dirent[0] <= 'Z') ||
       (dirent[0] >= 'a' && dirent[0] <= 'z')))
    return TRUE;

  /* On Windows and Cygwin //server/share is a root directory,
     and on Cygwin //drive is a drive alias */
  if (len >= 2 && dirent[0] == '/' && dirent[1] == '/'
      && dirent[len - 1] != '/')
    {
      int segments = 0;
      apr_size_t i;
      for (i = len; i >= 2; i--)
        {
          if (dirent[i] == '/')
            {
              segments ++;
              if (segments > 1)
                return FALSE;
            }
        }
#ifdef __CYGWIN__
      return (segments <= 1);
#else
      return (segments == 1); /* //drive is invalid on plain Windows */
#endif
    }
#endif

  /* directory is root if it's equal to '/' */
  if (len == 1 && dirent[0] == '/')
    return TRUE;

  return FALSE;
}

svn_boolean_t
svn_uri_is_root(const char *uri, apr_size_t len)
{
  assert(svn_uri_is_canonical(uri, NULL));
  return (len == uri_schema_root_length(uri, len));
}

char *svn_dirent_join(const char *base,
                      const char *component,
                      apr_pool_t *pool)
{
  apr_size_t blen = strlen(base);
  apr_size_t clen = strlen(component);
  char *dirent;
  int add_separator;

  assert(svn_dirent_is_canonical(base, pool));
  assert(svn_dirent_is_canonical(component, pool));

  /* If the component is absolute, then return it.  */
  if (svn_dirent_is_absolute(component))
    return apr_pmemdup(pool, component, clen + 1);

  /* If either is empty return the other */
  if (SVN_PATH_IS_EMPTY(base))
    return apr_pmemdup(pool, component, clen + 1);
  if (SVN_PATH_IS_EMPTY(component))
    return apr_pmemdup(pool, base, blen + 1);

#ifdef SVN_USE_DOS_PATHS
  if (component[0] == '/')
    {
      /* '/' is drive relative on Windows, not absolute like on Posix */
      if (dirent_is_rooted(base))
        {
          /* Join component without '/' to root-of(base) */
          blen = dirent_root_length(base, blen);
          component++;
          clen--;

          if (blen == 2 && base[1] == ':') /* "C:" case */
            {
              char *root = apr_pmemdup(pool, base, 3);
              root[2] = '/'; /* We don't need the final '\0' */

              base = root;
              blen = 3;
            }

          if (clen == 0)
            return apr_pstrndup(pool, base, blen);
        }
      else
        return apr_pmemdup(pool, component, clen + 1);
    }
  else if (dirent_is_rooted(component))
    return apr_pmemdup(pool, component, clen + 1);
#endif /* SVN_USE_DOS_PATHS */

  /* if last character of base is already a separator, don't add a '/' */
  add_separator = 1;
  if (base[blen - 1] == '/'
#ifdef SVN_USE_DOS_PATHS
       || base[blen - 1] == ':'
#endif
        )
          add_separator = 0;

  /* Construct the new, combined dirent. */
  dirent = apr_palloc(pool, blen + add_separator + clen + 1);
  memcpy(dirent, base, blen);
  if (add_separator)
    dirent[blen] = '/';
  memcpy(dirent + blen + add_separator, component, clen + 1);

  return dirent;
}

char *svn_dirent_join_many(apr_pool_t *pool, const char *base, ...)
{
#define MAX_SAVED_LENGTHS 10
  apr_size_t saved_lengths[MAX_SAVED_LENGTHS];
  apr_size_t total_len;
  int nargs;
  va_list va;
  const char *s;
  apr_size_t len;
  char *dirent;
  char *p;
  int add_separator;
  int base_arg = 0;

  total_len = strlen(base);

  assert(svn_dirent_is_canonical(base, pool));

  /* if last character of base is already a separator, don't add a '/' */
  add_separator = 1;
  if (total_len == 0
       || base[total_len - 1] == '/'
#ifdef SVN_USE_DOS_PATHS
       || base[total_len - 1] == ':'
#endif
        )
          add_separator = 0;

  saved_lengths[0] = total_len;

  /* Compute the length of the resulting string. */

  nargs = 0;
  va_start(va, base);
  while ((s = va_arg(va, const char *)) != NULL)
    {
      len = strlen(s);

      assert(svn_dirent_is_canonical(s, pool));

      if (SVN_PATH_IS_EMPTY(s))
        continue;

      if (nargs++ < MAX_SAVED_LENGTHS)
        saved_lengths[nargs] = len;

      if (dirent_is_rooted(s))
        {
          total_len = len;
          base_arg = nargs;

#ifdef SVN_USE_DOS_PATHS
          if (!svn_dirent_is_absolute(s)) /* Handle non absolute roots */
            {
              /* Set new base and skip the current argument */
              base = s = svn_dirent_join(base, s, pool);
              base_arg++;
              saved_lengths[0] = total_len = len = strlen(s);
            }
          else
#endif /* SVN_USE_DOS_PATHS */
            {
              base = ""; /* Don't add base */
              saved_lengths[0] = 0;
            }

          add_separator = 1;
          if (s[len - 1] == '/'
#ifdef SVN_USE_DOS_PATHS
             || s[len - 1] == ':'
#endif
              )
             add_separator = 0;
        }
      else if (nargs <= base_arg + 1)
        {
          total_len += add_separator + len;
        }
      else
        {
          total_len += 1 + len;
        }
    }
  va_end(va);

  /* base == "/" and no further components. just return that. */
  if (add_separator == 0 && total_len == 1)
    return apr_pmemdup(pool, "/", 2);

  /* we got the total size. allocate it, with room for a NULL character. */
  dirent = p = apr_palloc(pool, total_len + 1);

  /* if we aren't supposed to skip forward to an absolute component, and if
     this is not an empty base that we are skipping, then copy the base
     into the output. */
  if (! SVN_PATH_IS_EMPTY(base))
    {
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
      if (p != dirent &&
          ( ! (nargs - 1 <= base_arg) || add_separator))
        *p++ = '/';

      /* copy the new component and advance the pointer */
      memcpy(p, s, len);
      p += len;
    }
  va_end(va);

  *p = '\0';
  assert((apr_size_t)(p - dirent) == total_len);

  return dirent;
}

char *
svn_relpath_join(const char *base,
                 const char *component,
                 apr_pool_t *pool)
{
  apr_size_t blen = strlen(base);
  apr_size_t clen = strlen(component);
  char *path;

  assert(relpath_is_canonical(base));
  assert(relpath_is_canonical(component));

  /* If either is empty return the other */
  if (blen == 0)
    return apr_pmemdup(pool, component, clen + 1);
  if (clen == 0)
    return apr_pmemdup(pool, base, blen + 1);

  path = apr_palloc(pool, blen + 1 + clen + 1);
  memcpy(path, base, blen);
  path[blen] = '/';
  memcpy(path + blen + 1, component, clen + 1);

  return path;
}

char *
svn_dirent_dirname(const char *dirent, apr_pool_t *pool)
{
  apr_size_t len = strlen(dirent);

  assert(svn_dirent_is_canonical(dirent, pool));

  if (len == dirent_root_length(dirent, len))
    return apr_pstrmemdup(pool, dirent, len);
  else
    return apr_pstrmemdup(pool, dirent, dirent_previous_segment(dirent, len));
}

const char *
svn_dirent_basename(const char *dirent, apr_pool_t *pool)
{
  apr_size_t len = strlen(dirent);
  apr_size_t start;

  assert(!pool || svn_dirent_is_canonical(dirent, pool));

  if (svn_dirent_is_root(dirent, len))
    return "";
  else
    {
      start = len;
      while (start > 0 && dirent[start - 1] != '/'
#ifdef SVN_USE_DOS_PATHS
             && dirent[start - 1] != ':'
#endif
            )
        --start;
    }

  if (pool)
    return apr_pstrmemdup(pool, dirent + start, len - start);
  else
    return dirent + start;
}

void
svn_dirent_split(const char **dirpath,
                 const char **base_name,
                 const char *dirent,
                 apr_pool_t *pool)
{
  assert(dirpath != base_name);

  if (dirpath)
    *dirpath = svn_dirent_dirname(dirent, pool);

  if (base_name)
    *base_name = svn_dirent_basename(dirent, pool);
}

char *
svn_relpath_dirname(const char *relpath,
                    apr_pool_t *pool)
{
  apr_size_t len = strlen(relpath);

  assert(relpath_is_canonical(relpath));

  return apr_pstrmemdup(pool, relpath,
                        relpath_previous_segment(relpath, len));
}

const char *
svn_relpath_basename(const char *relpath,
                     apr_pool_t *pool)
{
  apr_size_t len = strlen(relpath);
  apr_size_t start;

  assert(relpath_is_canonical(relpath));

  start = len;
  while (start > 0 && relpath[start - 1] != '/')
    --start;

  if (pool)
    return apr_pstrmemdup(pool, relpath + start, len - start);
  else
    return relpath + start;
}

void
svn_relpath_split(const char **dirpath,
                  const char **base_name,
                  const char *relpath,
                  apr_pool_t *pool)
{
  assert(dirpath != base_name);

  if (dirpath)
    *dirpath = svn_relpath_dirname(relpath, pool);

  if (base_name)
    *base_name = svn_relpath_basename(relpath, pool);
}

const char *
svn_relpath_prefix(const char *relpath,
                   int max_components,
                   apr_pool_t *result_pool)
{
  const char *end;
  assert(relpath_is_canonical(relpath));

  if (max_components <= 0)
    return "";

  for (end = relpath; *end; end++)
    {
      if (*end == '/')
        {
          if (!--max_components)
            break;
        }
    }

  return apr_pstrmemdup(result_pool, relpath, end-relpath);
}

char *
svn_uri_dirname(const char *uri, apr_pool_t *pool)
{
  apr_size_t len = strlen(uri);

  assert(svn_uri_is_canonical(uri, pool));

  if (svn_uri_is_root(uri, len))
    return apr_pstrmemdup(pool, uri, len);
  else
    return apr_pstrmemdup(pool, uri, uri_previous_segment(uri, len));
}

const char *
svn_uri_basename(const char *uri, apr_pool_t *pool)
{
  apr_size_t len = strlen(uri);
  apr_size_t start;

  assert(svn_uri_is_canonical(uri, NULL));

  if (svn_uri_is_root(uri, len))
    return "";

  start = len;
  while (start > 0 && uri[start - 1] != '/')
    --start;

  return svn_path_uri_decode(uri + start, pool);
}

void
svn_uri_split(const char **dirpath,
              const char **base_name,
              const char *uri,
              apr_pool_t *pool)
{
  assert(dirpath != base_name);

  if (dirpath)
    *dirpath = svn_uri_dirname(uri, pool);

  if (base_name)
    *base_name = svn_uri_basename(uri, pool);
}

char *
svn_dirent_get_longest_ancestor(const char *dirent1,
                                const char *dirent2,
                                apr_pool_t *pool)
{
  return apr_pstrndup(pool, dirent1,
                      get_longest_ancestor_length(type_dirent, dirent1,
                                                  dirent2, pool));
}

char *
svn_relpath_get_longest_ancestor(const char *relpath1,
                                 const char *relpath2,
                                 apr_pool_t *pool)
{
  assert(relpath_is_canonical(relpath1));
  assert(relpath_is_canonical(relpath2));

  return apr_pstrndup(pool, relpath1,
                      get_longest_ancestor_length(type_relpath, relpath1,
                                                  relpath2, pool));
}

char *
svn_uri_get_longest_ancestor(const char *uri1,
                             const char *uri2,
                             apr_pool_t *pool)
{
  apr_size_t uri_ancestor_len;
  apr_size_t i = 0;

  assert(svn_uri_is_canonical(uri1, NULL));
  assert(svn_uri_is_canonical(uri2, NULL));

  /* Find ':' */
  while (1)
    {
      /* No shared protocol => no common prefix */
      if (uri1[i] != uri2[i])
        return apr_pmemdup(pool, SVN_EMPTY_PATH,
                           sizeof(SVN_EMPTY_PATH));

      if (uri1[i] == ':')
        break;

      /* They're both URLs, so EOS can't come before ':' */
      assert((uri1[i] != '\0') && (uri2[i] != '\0'));

      i++;
    }

  i += 3;  /* Advance past '://' */

  uri_ancestor_len = get_longest_ancestor_length(type_uri, uri1 + i,
                                                 uri2 + i, pool);

  if (uri_ancestor_len == 0 ||
      (uri_ancestor_len == 1 && (uri1 + i)[0] == '/'))
    return apr_pmemdup(pool, SVN_EMPTY_PATH, sizeof(SVN_EMPTY_PATH));
  else
    return apr_pstrndup(pool, uri1, uri_ancestor_len + i);
}

const char *
svn_dirent_is_child(const char *parent_dirent,
                    const char *child_dirent,
                    apr_pool_t *pool)
{
  return is_child(type_dirent, parent_dirent, child_dirent, pool);
}

const char *
svn_dirent_skip_ancestor(const char *parent_dirent,
                         const char *child_dirent)
{
  apr_size_t len = strlen(parent_dirent);
  apr_size_t root_len;

  if (0 != strncmp(parent_dirent, child_dirent, len))
    return NULL; /* parent_dirent is no ancestor of child_dirent */

  if (child_dirent[len] == 0)
    return ""; /* parent_dirent == child_dirent */

  /* Child == parent + more-characters */

  root_len = dirent_root_length(child_dirent, strlen(child_dirent));
  if (root_len > len)
    /* Different root, e.g. ("" "/...") or ("//z" "//z/share") */
    return NULL;

  /* Now, child == [root-of-parent] + [rest-of-parent] + more-characters.
   * It must be one of the following forms.
   *
   * rlen parent    child       bad?  rlen=len? c[len]=/?
   *  0   ""        "foo"               *
   *  0   "b"       "bad"         !
   *  0   "b"       "b/foo"                       *
   *  1   "/"       "/foo"              *
   *  1   "/b"      "/bad"        !
   *  1   "/b"      "/b/foo"                      *
   *  2   "a:"      "a:foo"             *
   *  2   "a:b"     "a:bad"       !
   *  2   "a:b"     "a:b/foo"                     *
   *  3   "a:/"     "a:/foo"            *
   *  3   "a:/b"    "a:/bad"      !
   *  3   "a:/b"    "a:/b/foo"                    *
   *  5   "//s/s"   "//s/s/foo"         *         *
   *  5   "//s/s/b" "//s/s/bad"   !
   *  5   "//s/s/b" "//s/s/b/foo"                 *
   */

  if (child_dirent[len] == '/')
    /* "parent|child" is one of:
     * "[a:]b|/foo" "[a:]/b|/foo" "//s/s|/foo" "//s/s/b|/foo" */
    return child_dirent + len + 1;

  if (root_len == len)
    /* "parent|child" is "|foo" "/|foo" "a:|foo" "a:/|foo" "//s/s|/foo" */
    return child_dirent + len;

  return NULL;
}

const char *
svn_relpath_skip_ancestor(const char *parent_relpath,
                          const char *child_relpath)
{
  apr_size_t len = strlen(parent_relpath);

  assert(relpath_is_canonical(parent_relpath));
  assert(relpath_is_canonical(child_relpath));

  if (len == 0)
    return child_relpath;

  if (0 != strncmp(parent_relpath, child_relpath, len))
    return NULL; /* parent_relpath is no ancestor of child_relpath */

  if (child_relpath[len] == 0)
    return ""; /* parent_relpath == child_relpath */

  if (child_relpath[len] == '/')
    return child_relpath + len + 1;

  return NULL;
}


/* */
static const char *
uri_skip_ancestor(const char *parent_uri,
                  const char *child_uri)
{
  apr_size_t len = strlen(parent_uri);

  assert(svn_uri_is_canonical(parent_uri, NULL));
  assert(svn_uri_is_canonical(child_uri, NULL));

  if (0 != strncmp(parent_uri, child_uri, len))
    return NULL; /* parent_uri is no ancestor of child_uri */

  if (child_uri[len] == 0)
    return ""; /* parent_uri == child_uri */

  if (child_uri[len] == '/')
    return child_uri + len + 1;

  return NULL;
}

const char *
svn_uri_skip_ancestor(const char *parent_uri,
                      const char *child_uri,
                      apr_pool_t *result_pool)
{
  const char *result = uri_skip_ancestor(parent_uri, child_uri);

  return result ? svn_path_uri_decode(result, result_pool) : NULL;
}

svn_boolean_t
svn_dirent_is_ancestor(const char *parent_dirent, const char *child_dirent)
{
  return svn_dirent_skip_ancestor(parent_dirent, child_dirent) != NULL;
}

svn_boolean_t
svn_uri__is_ancestor(const char *parent_uri, const char *child_uri)
{
  return uri_skip_ancestor(parent_uri, child_uri) != NULL;
}


svn_boolean_t
svn_dirent_is_absolute(const char *dirent)
{
  if (! dirent)
    return FALSE;

  /* dirent is absolute if it starts with '/' on non-Windows platforms
     or with '//' on Windows platforms */
  if (dirent[0] == '/'
#ifdef SVN_USE_DOS_PATHS
      && dirent[1] == '/' /* Single '/' depends on current drive */
#endif
      )
    return TRUE;

  /* On Windows, dirent is also absolute when it starts with 'H:/'
     where 'H' is any letter. */
#ifdef SVN_USE_DOS_PATHS
  if (((dirent[0] >= 'A' && dirent[0] <= 'Z')) &&
      (dirent[1] == ':') && (dirent[2] == '/'))
     return TRUE;
#endif /* SVN_USE_DOS_PATHS */

  return FALSE;
}

svn_error_t *
svn_dirent_get_absolute(const char **pabsolute,
                        const char *relative,
                        apr_pool_t *pool)
{
  char *buffer;
  apr_status_t apr_err;
  const char *path_apr;

  SVN_ERR_ASSERT(! svn_path_is_url(relative));

  /* Merge the current working directory with the relative dirent. */
  SVN_ERR(svn_path_cstring_from_utf8(&path_apr, relative, pool));

  apr_err = apr_filepath_merge(&buffer, NULL,
                               path_apr,
                               APR_FILEPATH_NOTRELATIVE,
                               pool);
  if (apr_err)
    {
      /* In some cases when the passed path or its ancestor(s) do not exist
         or no longer exist apr returns an error.

         In many of these cases we would like to return a path anyway, when the
         passed path was already a safe absolute path. So check for that now to
         avoid an error.

         svn_dirent_is_absolute() doesn't perform the necessary checks to see
         if the path doesn't need post processing to be in the canonical absolute
         format.
         */

      if (svn_dirent_is_absolute(relative)
          && svn_dirent_is_canonical(relative, pool)
          && !svn_path_is_backpath_present(relative))
        {
          *pabsolute = apr_pstrdup(pool, relative);
          return SVN_NO_ERROR;
        }

      return svn_error_createf(SVN_ERR_BAD_FILENAME,
                               svn_error_create(apr_err, NULL, NULL),
                               _("Couldn't determine absolute path of '%s'"),
                               svn_dirent_local_style(relative, pool));
    }

  SVN_ERR(svn_path_cstring_to_utf8(pabsolute, buffer, pool));
  *pabsolute = svn_dirent_canonicalize(*pabsolute, pool);
  return SVN_NO_ERROR;
}

const char *
svn_uri_canonicalize(const char *uri, apr_pool_t *pool)
{
  return canonicalize(type_uri, uri, pool);
}

const char *
svn_relpath_canonicalize(const char *relpath, apr_pool_t *pool)
{
  return canonicalize(type_relpath, relpath, pool);
}

const char *
svn_dirent_canonicalize(const char *dirent, apr_pool_t *pool)
{
  const char *dst = canonicalize(type_dirent, dirent, pool);

#ifdef SVN_USE_DOS_PATHS
  /* Handle a specific case on Windows where path == "X:/". Here we have to
     append the final '/', as svn_path_canonicalize will chop this of. */
  if (((dirent[0] >= 'A' && dirent[0] <= 'Z') ||
        (dirent[0] >= 'a' && dirent[0] <= 'z')) &&
        dirent[1] == ':' && dirent[2] == '/' &&
        dst[3] == '\0')
    {
      char *dst_slash = apr_pcalloc(pool, 4);
      dst_slash[0] = canonicalize_to_upper(dirent[0]);
      dst_slash[1] = ':';
      dst_slash[2] = '/';
      dst_slash[3] = '\0';

      return dst_slash;
    }
#endif /* SVN_USE_DOS_PATHS */

  return dst;
}

svn_boolean_t
svn_dirent_is_canonical(const char *dirent, apr_pool_t *scratch_pool)
{
  const char *ptr = dirent;
  if (*ptr == '/')
    {
      ptr++;
#ifdef SVN_USE_DOS_PATHS
      /* Check for UNC paths */
      if (*ptr == '/')
        {
          /* TODO: Scan hostname and sharename and fall back to part code */

          /* ### Fall back to old implementation */
          return (strcmp(dirent, svn_dirent_canonicalize(dirent, scratch_pool))
                  == 0);
        }
#endif /* SVN_USE_DOS_PATHS */
    }
#ifdef SVN_USE_DOS_PATHS
  else if (((*ptr >= 'a' && *ptr <= 'z') || (*ptr >= 'A' && *ptr <= 'Z')) &&
           (ptr[1] == ':'))
    {
      /* The only canonical drive names are "A:"..."Z:", no lower case */
      if (*ptr < 'A' || *ptr > 'Z')
        return FALSE;

      ptr += 2;

      if (*ptr == '/')
        ptr++;
    }
#endif /* SVN_USE_DOS_PATHS */

  return relpath_is_canonical(ptr);
}

static svn_boolean_t
relpath_is_canonical(const char *relpath)
{
  const char *dot_pos, *ptr = relpath;
  apr_size_t i, len;
  unsigned pattern = 0;

  /* RELPATH is canonical if it has:
   *  - no '.' segments
   *  - no start and closing '/'
   *  - no '//'
   */

  /* invalid beginnings */
  if (*ptr == '/')
    return FALSE;

  if (ptr[0] == '.' && (ptr[1] == '/' || ptr[1] == '\0'))
    return FALSE;

  /* valid special cases */
  len = strlen(ptr);
  if (len < 2)
    return TRUE;

  /* invalid endings */
  if (ptr[len-1] == '/' || (ptr[len-1] == '.' && ptr[len-2] == '/'))
    return FALSE;

  /* '.' are rare. So, search for them globally. There will often be no
   * more than one hit.  Also note that we already checked for invalid
   * starts and endings, i.e. we only need to check for "/./"
   */
  for (dot_pos = memchr(ptr, '.', len);
       dot_pos;
       dot_pos = strchr(dot_pos+1, '.'))
    if (dot_pos > ptr && dot_pos[-1] == '/' && dot_pos[1] == '/')
      return FALSE;

  /* Now validate the rest of the path. */
  for (i = 0; i < len - 1; ++i)
    {
      pattern = ((pattern & 0xff) << 8) + (unsigned char)ptr[i];
      if (pattern == 0x101 * (unsigned char)('/'))
        return FALSE;
    }

  return TRUE;
}

svn_boolean_t
svn_relpath_is_canonical(const char *relpath)
{
  return relpath_is_canonical(relpath);
}

svn_boolean_t
svn_uri_is_canonical(const char *uri, apr_pool_t *scratch_pool)
{
  const char *ptr = uri, *seg = uri;
  const char *schema_data = NULL;

  /* URI is canonical if it has:
   *  - lowercase URL scheme
   *  - lowercase URL hostname
   *  - no '.' segments
   *  - no closing '/'
   *  - no '//'
   *  - uppercase hex-encoded pair digits ("%AB", not "%ab")
   */

  if (*uri == '\0')
    return FALSE;

  if (! svn_path_is_url(uri))
    return FALSE;

  /* Skip the scheme. */
  while (*ptr && (*ptr != '/') && (*ptr != ':'))
    ptr++;

  /* No scheme?  No good. */
  if (! (*ptr == ':' && *(ptr+1) == '/' && *(ptr+2) == '/'))
    return FALSE;

  /* Found a scheme, check that it's all lowercase. */
  ptr = uri;
  while (*ptr != ':')
    {
      if (*ptr >= 'A' && *ptr <= 'Z')
        return FALSE;
      ptr++;
    }
  /* Skip :// */
  ptr += 3;

  /* Scheme only?  That works. */
  if (! *ptr)
    return TRUE;

  /* This might be the hostname */
  seg = ptr;
  while (*ptr && (*ptr != '/') && (*ptr != '@'))
    ptr++;

  if (*ptr == '@')
    seg = ptr + 1;

  /* Found a hostname, check that it's all lowercase. */
  ptr = seg;

  if (*ptr == '[')
    {
      ptr++;
      while (*ptr == ':'
             || (*ptr >= '0' && *ptr <= '9')
             || (*ptr >= 'a' && *ptr <= 'f'))
        {
          ptr++;
        }

      if (*ptr != ']')
        return FALSE;
      ptr++;
    }
  else
    while (*ptr && *ptr != '/' && *ptr != ':')
      {
        if (*ptr >= 'A' && *ptr <= 'Z')
          return FALSE;
        ptr++;
      }

  /* Found a portnumber */
  if (*ptr == ':')
    {
      apr_int64_t port = 0;

      ptr++;
      schema_data = ptr;

      while (*ptr >= '0' && *ptr <= '9')
        {
          port = 10 * port + (*ptr - '0');
          ptr++;
        }

      if (ptr == schema_data && (*ptr == '/' || *ptr == '\0'))
        return FALSE; /* Fail on "http://host:" */

      if (port == 80 && strncmp(uri, "http:", 5) == 0)
        return FALSE;
      else if (port == 443 && strncmp(uri, "https:", 6) == 0)
        return FALSE;
      else if (port == 3690 && strncmp(uri, "svn:", 4) == 0)
        return FALSE;

      while (*ptr && *ptr != '/')
        ++ptr; /* Allow "http://host:stuff" */
    }

  schema_data = ptr;

#ifdef SVN_USE_DOS_PATHS
  if (schema_data && *ptr == '/')
    {
      /* If this is a file url, ptr now points to the third '/' in
         file:///C:/path. Check that if we have such a URL the drive
         letter is in uppercase. */
      if (strncmp(uri, "file:", 5) == 0 &&
          ! (*(ptr+1) >= 'A' && *(ptr+1) <= 'Z') &&
          *(ptr+2) == ':')
        return FALSE;
    }
#endif /* SVN_USE_DOS_PATHS */

  /* Now validate the rest of the URI. */
  seg = ptr;
  while (*ptr && (*ptr != '/'))
    ptr++;
  while(1)
    {
      apr_size_t seglen = ptr - seg;

      if (seglen == 1 && *seg == '.')
        return FALSE;  /*  /./   */

      if (*ptr == '/' && *(ptr+1) == '/')
        return FALSE;  /*  //    */

      if (! *ptr && *(ptr - 1) == '/' && ptr - 1 != uri)
        return FALSE;  /* foo/  */

      if (! *ptr)
        break;

      if (*ptr == '/')
        ptr++;

      seg = ptr;
      while (*ptr && (*ptr != '/'))
        ptr++;
    }

  ptr = schema_data;

  while (*ptr)
    {
      if (*ptr == '%')
        {
          char digitz[3];
          int val;

          /* Can't usesvn_ctype_isxdigit() because lower case letters are
             not in our canonical format */
          if (((*(ptr+1) < '0' || *(ptr+1) > '9'))
              && (*(ptr+1) < 'A' || *(ptr+1) > 'F'))
            return FALSE;
          else if (((*(ptr+2) < '0' || *(ptr+2) > '9'))
                   && (*(ptr+2) < 'A' || *(ptr+2) > 'F'))
            return FALSE;

          digitz[0] = *(++ptr);
          digitz[1] = *(++ptr);
          digitz[2] = '\0';
          val = (int)strtol(digitz, NULL, 16);

          if (svn_uri__char_validity[val])
            return FALSE; /* Should not have been escaped */
        }
      else if (*ptr != '/' && !svn_uri__char_validity[(unsigned char)*ptr])
        return FALSE; /* Character should have been escaped */
      ptr++;
    }

  return TRUE;
}

svn_error_t *
svn_dirent_condense_targets(const char **pcommon,
                            apr_array_header_t **pcondensed_targets,
                            const apr_array_header_t *targets,
                            svn_boolean_t remove_redundancies,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  int i, num_condensed = targets->nelts;
  svn_boolean_t *removed;
  apr_array_header_t *abs_targets;

  /* Early exit when there's no data to work on. */
  if (targets->nelts <= 0)
    {
      *pcommon = NULL;
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
      return SVN_NO_ERROR;
    }

  /* Get the absolute path of the first target. */
  SVN_ERR(svn_dirent_get_absolute(pcommon,
                                  APR_ARRAY_IDX(targets, 0, const char *),
                                  scratch_pool));

  /* Early exit when there's only one dirent to work on. */
  if (targets->nelts == 1)
    {
      *pcommon = apr_pstrdup(result_pool, *pcommon);
      if (pcondensed_targets)
        *pcondensed_targets = apr_array_make(result_pool, 0,
                                             sizeof(const char *));
      return SVN_NO_ERROR;
    }

  /* Copy the targets array, but with absolute dirents instead of
     relative.  Also, find the pcommon argument by finding what is
     common in all of the absolute dirents. NOTE: This is not as
     efficient as it could be.  The calculation of the basedir could
     be done in the loop below, which would save some calls to
     svn_dirent_get_longest_ancestor.  I decided to do it this way
     because I thought it would be simpler, since this way, we don't
     even do the loop if we don't need to condense the targets. */

  removed = apr_pcalloc(scratch_pool, (targets->nelts *
                                          sizeof(svn_boolean_t)));
  abs_targets = apr_array_make(scratch_pool, targets->nelts,
                               sizeof(const char *));

  APR_ARRAY_PUSH(abs_targets, const char *) = *pcommon;

  for (i = 1; i < targets->nelts; ++i)
    {
      const char *rel = APR_ARRAY_IDX(targets, i, const char *);
      const char *absolute;
      SVN_ERR(svn_dirent_get_absolute(&absolute, rel, scratch_pool));
      APR_ARRAY_PUSH(abs_targets, const char *) = absolute;
      *pcommon = svn_dirent_get_longest_ancestor(*pcommon, absolute,
                                                 scratch_pool);
    }

  *pcommon = apr_pstrdup(result_pool, *pcommon);

  if (pcondensed_targets != NULL)
    {
      size_t basedir_len;

      if (remove_redundancies)
        {
          /* Find the common part of each pair of targets.  If
             common part is equal to one of the dirents, the other
             is a child of it, and can be removed.  If a target is
             equal to *pcommon, it can also be removed. */

          /* First pass: when one non-removed target is a child of
             another non-removed target, remove the child. */
          for (i = 0; i < abs_targets->nelts; ++i)
            {
              int j;

              if (removed[i])
                continue;

              for (j = i + 1; j < abs_targets->nelts; ++j)
                {
                  const char *abs_targets_i;
                  const char *abs_targets_j;
                  const char *ancestor;

                  if (removed[j])
                    continue;

                  abs_targets_i = APR_ARRAY_IDX(abs_targets, i, const char *);
                  abs_targets_j = APR_ARRAY_IDX(abs_targets, j, const char *);

                  ancestor = svn_dirent_get_longest_ancestor
                    (abs_targets_i, abs_targets_j, scratch_pool);

                  if (*ancestor == '\0')
                    continue;

                  if (strcmp(ancestor, abs_targets_i) == 0)
                    {
                      removed[j] = TRUE;
                      num_condensed--;
                    }
                  else if (strcmp(ancestor, abs_targets_j) == 0)
                    {
                      removed[i] = TRUE;
                      num_condensed--;
                    }
                }
            }

          /* Second pass: when a target is the same as *pcommon,
             remove the target. */
          for (i = 0; i < abs_targets->nelts; ++i)
            {
              const char *abs_targets_i = APR_ARRAY_IDX(abs_targets, i,
                                                        const char *);

              if ((strcmp(abs_targets_i, *pcommon) == 0) && (! removed[i]))
                {
                  removed[i] = TRUE;
                  num_condensed--;
                }
            }
        }

      /* Now create the return array, and copy the non-removed items */
      basedir_len = strlen(*pcommon);
      *pcondensed_targets = apr_array_make(result_pool, num_condensed,
                                           sizeof(const char *));

      for (i = 0; i < abs_targets->nelts; ++i)
        {
          const char *rel_item = APR_ARRAY_IDX(abs_targets, i, const char *);

          /* Skip this if it's been removed. */
          if (removed[i])
            continue;

          /* If a common prefix was found, condensed_targets are given
             relative to that prefix.  */
          if (basedir_len > 0)
            {
              /* Only advance our pointer past a dirent separator if
                 REL_ITEM isn't the same as *PCOMMON.

                 If *PCOMMON is a root dirent, basedir_len will already
                 include the closing '/', so never advance the pointer
                 here.
                 */
              rel_item += basedir_len;
              if (rel_item[0] &&
                  ! svn_dirent_is_root(*pcommon, basedir_len))
                rel_item++;
            }

          APR_ARRAY_PUSH(*pcondensed_targets, const char *)
            = apr_pstrdup(result_pool, rel_item);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_uri_condense_targets(const char **pcommon,
                         apr_array_header_t **pcondensed_targets,
                         const apr_array_header_t *targets,
                         svn_boolean_t remove_redundancies,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  int i, num_condensed = targets->nelts;
  apr_array_header_t *uri_targets;
  svn_boolean_t *removed;

  /* Early exit when there's no data to work on. */
  if (targets->nelts <= 0)
    {
      *pcommon = NULL;
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
      return SVN_NO_ERROR;
    }

  *pcommon = svn_uri_canonicalize(APR_ARRAY_IDX(targets, 0, const char *),
                                  scratch_pool);

  /* Early exit when there's only one uri to work on. */
  if (targets->nelts == 1)
    {
      *pcommon = apr_pstrdup(result_pool, *pcommon);
      if (pcondensed_targets)
        *pcondensed_targets = apr_array_make(result_pool, 0,
                                             sizeof(const char *));
      return SVN_NO_ERROR;
    }

  /* Find the pcommon argument by finding what is common in all of the
     uris. NOTE: This is not as efficient as it could be.  The calculation
     of the basedir could be done in the loop below, which would
     save some calls to svn_uri_get_longest_ancestor.  I decided to do it
     this way because I thought it would be simpler, since this way, we don't
     even do the loop if we don't need to condense the targets. */

  removed = apr_pcalloc(scratch_pool, (targets->nelts *
                                          sizeof(svn_boolean_t)));
  uri_targets = apr_array_make(scratch_pool, targets->nelts,
                               sizeof(const char *));

  APR_ARRAY_PUSH(uri_targets, const char *) = *pcommon;

  for (i = 1; i < targets->nelts; ++i)
    {
      const char *uri = svn_uri_canonicalize(
                           APR_ARRAY_IDX(targets, i, const char *),
                           scratch_pool);
      APR_ARRAY_PUSH(uri_targets, const char *) = uri;

      /* If the commonmost ancestor so far is empty, there's no point
         in continuing to search for a common ancestor at all.  But
         we'll keep looping for the sake of canonicalizing the
         targets, I suppose.  */
      if (**pcommon != '\0')
        *pcommon = svn_uri_get_longest_ancestor(*pcommon, uri,
                                                scratch_pool);
    }

  *pcommon = apr_pstrdup(result_pool, *pcommon);

  if (pcondensed_targets != NULL)
    {
      size_t basedir_len;

      if (remove_redundancies)
        {
          /* Find the common part of each pair of targets.  If
             common part is equal to one of the dirents, the other
             is a child of it, and can be removed.  If a target is
             equal to *pcommon, it can also be removed. */

          /* First pass: when one non-removed target is a child of
             another non-removed target, remove the child. */
          for (i = 0; i < uri_targets->nelts; ++i)
            {
              int j;

              if (removed[i])
                continue;

              for (j = i + 1; j < uri_targets->nelts; ++j)
                {
                  const char *uri_i;
                  const char *uri_j;
                  const char *ancestor;

                  if (removed[j])
                    continue;

                  uri_i = APR_ARRAY_IDX(uri_targets, i, const char *);
                  uri_j = APR_ARRAY_IDX(uri_targets, j, const char *);

                  ancestor = svn_uri_get_longest_ancestor(uri_i,
                                                          uri_j,
                                                          scratch_pool);

                  if (*ancestor == '\0')
                    continue;

                  if (strcmp(ancestor, uri_i) == 0)
                    {
                      removed[j] = TRUE;
                      num_condensed--;
                    }
                  else if (strcmp(ancestor, uri_j) == 0)
                    {
                      removed[i] = TRUE;
                      num_condensed--;
                    }
                }
            }

          /* Second pass: when a target is the same as *pcommon,
             remove the target. */
          for (i = 0; i < uri_targets->nelts; ++i)
            {
              const char *uri_targets_i = APR_ARRAY_IDX(uri_targets, i,
                                                        const char *);

              if ((strcmp(uri_targets_i, *pcommon) == 0) && (! removed[i]))
                {
                  removed[i] = TRUE;
                  num_condensed--;
                }
            }
        }

      /* Now create the return array, and copy the non-removed items */
      basedir_len = strlen(*pcommon);
      *pcondensed_targets = apr_array_make(result_pool, num_condensed,
                                           sizeof(const char *));

      for (i = 0; i < uri_targets->nelts; ++i)
        {
          const char *rel_item = APR_ARRAY_IDX(uri_targets, i, const char *);

          /* Skip this if it's been removed. */
          if (removed[i])
            continue;

          /* If a common prefix was found, condensed_targets are given
             relative to that prefix.  */
          if (basedir_len > 0)
            {
              /* Only advance our pointer past a dirent separator if
                 REL_ITEM isn't the same as *PCOMMON.

                 If *PCOMMON is a root dirent, basedir_len will already
                 include the closing '/', so never advance the pointer
                 here.
                 */
              rel_item += basedir_len;
              if ((rel_item[0] == '/') ||
                  (rel_item[0] && !svn_uri_is_root(*pcommon, basedir_len)))
                {
                  rel_item++;
                }
            }

          APR_ARRAY_PUSH(*pcondensed_targets, const char *)
            = svn_path_uri_decode(rel_item, result_pool);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_dirent_is_under_root(svn_boolean_t *under_root,
                         const char **result_path,
                         const char *base_path,
                         const char *path,
                         apr_pool_t *result_pool)
{
  apr_status_t status;
  char *full_path;

  *under_root = FALSE;
  if (result_path)
    *result_path = NULL;

  status = apr_filepath_merge(&full_path,
                              base_path,
                              path,
                              APR_FILEPATH_NOTABOVEROOT
                              | APR_FILEPATH_SECUREROOTTEST,
                              result_pool);

  if (status == APR_SUCCESS)
    {
      if (result_path)
        *result_path = svn_dirent_canonicalize(full_path, result_pool);
      *under_root = TRUE;
      return SVN_NO_ERROR;
    }
  else if (status == APR_EABOVEROOT)
    {
      *under_root = FALSE;
      return SVN_NO_ERROR;
    }

  return svn_error_wrap_apr(status, NULL);
}

svn_error_t *
svn_uri_get_dirent_from_file_url(const char **dirent,
                                 const char *url,
                                 apr_pool_t *pool)
{
  const char *hostname, *path;

  SVN_ERR_ASSERT(svn_uri_is_canonical(url, pool));

  /* Verify that the URL is well-formed (loosely) */

  /* First, check for the "file://" prefix. */
  if (strncmp(url, "file://", 7) != 0)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("Local URL '%s' does not contain 'file://' "
                               "prefix"), url);

  /* Find the HOSTNAME portion and the PATH portion of the URL.  The host
     name is between the "file://" prefix and the next occurrence of '/'.  We
     are considering everything from that '/' until the end of the URL to be
     the absolute path portion of the URL.
     If we got just "file://", treat it the same as "file:///". */
  hostname = url + 7;
  path = strchr(hostname, '/');
  if (path)
    hostname = apr_pstrmemdup(pool, hostname, path - hostname);
  else
    path = "/";

  /* URI-decode HOSTNAME, and set it to NULL if it is "" or "localhost". */
  if (*hostname == '\0')
    hostname = NULL;
  else
    {
      hostname = svn_path_uri_decode(hostname, pool);
      if (strcmp(hostname, "localhost") == 0)
        hostname = NULL;
    }

  /* Duplicate the URL, starting at the top of the path.
     At the same time, we URI-decode the path. */
#ifdef SVN_USE_DOS_PATHS
  /* On Windows, we'll typically have to skip the leading / if the
     path starts with a drive letter.  Like most Web browsers, We
     support two variants of this scheme:

         file:///X:/path    and
         file:///X|/path

    Note that, at least on WinNT and above,  file:////./X:/path  will
    also work, so we must make sure the transformation doesn't break
    that, and  file:///path  (that looks within the current drive
    only) should also keep working.
    If we got a non-empty hostname other than localhost, we convert this
    into an UNC path.  In this case, we obviously don't strip the slash
    even if the path looks like it starts with a drive letter.
  */
  {
    static const char valid_drive_letters[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    /* Casting away const! */
    char *dup_path = (char *)svn_path_uri_decode(path, pool);

    /* This check assumes ':' and '|' are already decoded! */
    if (!hostname && dup_path[1] && strchr(valid_drive_letters, dup_path[1])
        && (dup_path[2] == ':' || dup_path[2] == '|'))
      {
        /* Skip the leading slash. */
        ++dup_path;

        if (dup_path[1] == '|')
          dup_path[1] = ':';

        if (dup_path[2] == '/' || dup_path[2] == '\\' || dup_path[2] == '\0')
          {
            /* Dirents have upper case drive letters in their canonical form */
            dup_path[0] = canonicalize_to_upper(dup_path[0]);

            if (dup_path[2] == '\0')
              {
                /* A valid dirent for the driveroot must be like "C:/" instead of
                   just "C:" or svn_dirent_join() will use the current directory
                   on the drive instead */
                char *new_path = apr_pcalloc(pool, 4);
                new_path[0] = dup_path[0];
                new_path[1] = ':';
                new_path[2] = '/';
                new_path[3] = '\0';
                dup_path = new_path;
              }
            else
              dup_path[2] = '/'; /* Ensure not relative for '\' after drive! */
          }
      }
    if (hostname)
      {
        if (dup_path[0] == '/' && dup_path[1] == '\0')
          return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                   _("Local URL '%s' contains only a hostname, "
                                     "no path"), url);

        /* We still know that the path starts with a slash. */
        *dirent = apr_pstrcat(pool, "//", hostname, dup_path, SVN_VA_NULL);
      }
    else
      *dirent = dup_path;
  }
#else /* !SVN_USE_DOS_PATHS */
  /* Currently, the only hostnames we are allowing on non-Win32 platforms
     are the empty string and 'localhost'. */
  if (hostname)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("Local URL '%s' contains unsupported hostname"),
                             url);

  *dirent = svn_path_uri_decode(path, pool);
#endif /* SVN_USE_DOS_PATHS */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_uri_get_file_url_from_dirent(const char **url,
                                 const char *dirent,
                                 apr_pool_t *pool)
{
  assert(svn_dirent_is_canonical(dirent, pool));

  SVN_ERR(svn_dirent_get_absolute(&dirent, dirent, pool));

  dirent = svn_path_uri_encode(dirent, pool);

#ifndef SVN_USE_DOS_PATHS
  if (dirent[0] == '/' && dirent[1] == '\0')
    dirent = NULL; /* "file://" is the canonical form of "file:///" */

  *url = apr_pstrcat(pool, "file://", dirent, SVN_VA_NULL);
#else
  if (dirent[0] == '/')
    {
      /* Handle UNC paths //server/share -> file://server/share */
      assert(dirent[1] == '/'); /* Expect UNC, not non-absolute */

      *url = apr_pstrcat(pool, "file:", dirent, SVN_VA_NULL);
    }
  else
    {
      char *uri = apr_pstrcat(pool, "file:///", dirent, SVN_VA_NULL);
      apr_size_t len = 8 /* strlen("file:///") */ + strlen(dirent);

      /* "C:/" is a canonical dirent on Windows,
         but "file:///C:/" is not a canonical uri */
      if (uri[len-1] == '/')
        uri[len-1] = '\0';

      *url = uri;
    }
#endif

  return SVN_NO_ERROR;
}



/* -------------- The fspath API (see private/svn_fspath.h) -------------- */

svn_boolean_t
svn_fspath__is_canonical(const char *fspath)
{
  return fspath[0] == '/' && relpath_is_canonical(fspath + 1);
}


const char *
svn_fspath__canonicalize(const char *fspath,
                         apr_pool_t *pool)
{
  if ((fspath[0] == '/') && (fspath[1] == '\0'))
    return "/";

  return apr_pstrcat(pool, "/", svn_relpath_canonicalize(fspath, pool),
                     SVN_VA_NULL);
}


svn_boolean_t
svn_fspath__is_root(const char *fspath, apr_size_t len)
{
  /* directory is root if it's equal to '/' */
  return (len == 1 && fspath[0] == '/');
}


const char *
svn_fspath__skip_ancestor(const char *parent_fspath,
                          const char *child_fspath)
{
  assert(svn_fspath__is_canonical(parent_fspath));
  assert(svn_fspath__is_canonical(child_fspath));

  return svn_relpath_skip_ancestor(parent_fspath + 1, child_fspath + 1);
}


const char *
svn_fspath__dirname(const char *fspath,
                    apr_pool_t *pool)
{
  assert(svn_fspath__is_canonical(fspath));

  if (fspath[0] == '/' && fspath[1] == '\0')
    return apr_pstrdup(pool, fspath);
  else
    return apr_pstrcat(pool, "/", svn_relpath_dirname(fspath + 1, pool),
                       SVN_VA_NULL);
}


const char *
svn_fspath__basename(const char *fspath,
                     apr_pool_t *pool)
{
  const char *result;
  assert(svn_fspath__is_canonical(fspath));

  result = svn_relpath_basename(fspath + 1, pool);

  assert(strchr(result, '/') == NULL);
  return result;
}

void
svn_fspath__split(const char **dirpath,
                  const char **base_name,
                  const char *fspath,
                  apr_pool_t *result_pool)
{
  assert(dirpath != base_name);

  if (dirpath)
    *dirpath = svn_fspath__dirname(fspath, result_pool);

  if (base_name)
    *base_name = svn_fspath__basename(fspath, result_pool);
}

char *
svn_fspath__join(const char *fspath,
                 const char *relpath,
                 apr_pool_t *result_pool)
{
  char *result;
  assert(svn_fspath__is_canonical(fspath));
  assert(svn_relpath_is_canonical(relpath));

  if (relpath[0] == '\0')
    result = apr_pstrdup(result_pool, fspath);
  else if (fspath[1] == '\0')
    result = apr_pstrcat(result_pool, "/", relpath, SVN_VA_NULL);
  else
    result = apr_pstrcat(result_pool, fspath, "/", relpath, SVN_VA_NULL);

  assert(svn_fspath__is_canonical(result));
  return result;
}

char *
svn_fspath__get_longest_ancestor(const char *fspath1,
                                 const char *fspath2,
                                 apr_pool_t *result_pool)
{
  char *result;
  assert(svn_fspath__is_canonical(fspath1));
  assert(svn_fspath__is_canonical(fspath2));

  result = apr_pstrcat(result_pool, "/",
                       svn_relpath_get_longest_ancestor(fspath1 + 1,
                                                        fspath2 + 1,
                                                        result_pool),
                       SVN_VA_NULL);

  assert(svn_fspath__is_canonical(result));
  return result;
}




/* -------------- The urlpath API (see private/svn_fspath.h) ------------- */

const char *
svn_urlpath__canonicalize(const char *uri,
                          apr_pool_t *pool)
{
  if (svn_path_is_url(uri))
    {
      uri = svn_uri_canonicalize(uri, pool);
    }
  else
    {
      uri = svn_fspath__canonicalize(uri, pool);
      /* Do a little dance to normalize hex encoding. */
      uri = svn_path_uri_decode(uri, pool);
      uri = svn_path_uri_encode(uri, pool);
    }
  return uri;
}


/* -------------- The cert API (see private/svn_cert.h) ------------- */

svn_boolean_t
svn_cert__match_dns_identity(svn_string_t *pattern, svn_string_t *hostname)
{
  apr_size_t pattern_pos = 0, hostname_pos = 0;

  /* support leading wildcards that composed of the only character in the
   * left-most label. */
  if (pattern->len >= 2 &&
      pattern->data[pattern_pos] == '*' &&
      pattern->data[pattern_pos + 1] == '.')
    {
      while (hostname_pos < hostname->len &&
             hostname->data[hostname_pos] != '.')
        {
          hostname_pos++;
        }
      /* Assume that the wildcard must match something.  Rule 2 says
       * that *.example.com should not match example.com.  If the wildcard
       * ends up not matching anything then it matches .example.com which
       * seems to be essentially the same as just example.com */
      if (hostname_pos == 0)
        return FALSE;

      pattern_pos++;
    }

  while (pattern_pos < pattern->len && hostname_pos < hostname->len)
    {
      char pattern_c = pattern->data[pattern_pos];
      char hostname_c = hostname->data[hostname_pos];

      /* fold case as described in RFC 4343.
       * Note: We actually convert to lowercase, since our URI
       * canonicalization code converts to lowercase and generally
       * most certs are issued with lowercase DNS names, meaning
       * this avoids the fold operation in most cases.  The RFC
       * suggests the opposite transformation, but doesn't require
       * any specific implementation in any case.  It is critical
       * that this folding be locale independent so you can't use
       * tolower(). */
      pattern_c = canonicalize_to_lower(pattern_c);
      hostname_c = canonicalize_to_lower(hostname_c);

      if (pattern_c != hostname_c)
        {
          /* doesn't match */
          return FALSE;
        }
      else
        {
          /* characters match so skip both */
          pattern_pos++;
          hostname_pos++;
        }
    }

  /* ignore a trailing period on the hostname since this has no effect on the
   * security of the matching.  See the following for the long explanation as
   * to why:
   * https://bugzilla.mozilla.org/show_bug.cgi?id=134402#c28
   */
  if (pattern_pos == pattern->len &&
      hostname_pos == hostname->len - 1 &&
      hostname->data[hostname_pos] == '.')
    hostname_pos++;

  if (pattern_pos != pattern->len || hostname_pos != hostname->len)
    {
      /* end didn't match */
      return FALSE;
    }

  return TRUE;
}
