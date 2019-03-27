/*
 * io.c:   shared file reading, writing, and probing code.
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



#include <stdio.h>

#ifndef WIN32
#include <unistd.h>
#endif

#ifndef APR_STATUS_IS_EPERM
#include <errno.h>
#ifdef EPERM
#define APR_STATUS_IS_EPERM(s)   ((s) == EPERM)
#else
#define APR_STATUS_IS_EPERM(s)   (0)
#endif
#endif

#include <apr_lib.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_general.h>
#include <apr_strings.h>
#include <apr_portable.h>
#include <apr_md5.h>

#if APR_HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "svn_private_config.h"
#include "svn_ctype.h"

#include "private/svn_atomic.h"
#include "private/svn_io_private.h"
#include "private/svn_utf_private.h"
#include "private/svn_dep_compat.h"

#define SVN_SLEEP_ENV_VAR "SVN_I_LOVE_CORRUPTED_WORKING_COPIES_SO_DISABLE_SLEEP_FOR_TIMESTAMPS"

/*
  Windows is 'aided' by a number of types of applications that
  follow other applications around and open up files they have
  changed for various reasons (the most intrusive are virus
  scanners).  So, if one of these other apps has glommed onto
  our file we may get an 'access denied' error.

  This retry loop does not completely solve the problem (who
  knows how long the other app is going to hold onto it for), but
  goes a long way towards minimizing it.  It is not an infinite
  loop because there might really be an error.

  Another reason for retrying delete operations on Windows
  is that they are asynchronous -- the file or directory is not
  actually deleted until the last handle to it is closed.  The
  retry loop cannot completely solve this problem either, but can
  help mitigate it.
*/
#define RETRY_MAX_ATTEMPTS 100
#define RETRY_INITIAL_SLEEP 1000
#define RETRY_MAX_SLEEP 128000

#define RETRY_LOOP(err, expr, retry_test, sleep_test)                      \
  do                                                                       \
    {                                                                      \
      apr_status_t os_err = APR_TO_OS_ERROR(err);                          \
      int sleep_count = RETRY_INITIAL_SLEEP;                               \
      int retries;                                                         \
      for (retries = 0;                                                    \
           retries < RETRY_MAX_ATTEMPTS && (retry_test);                   \
           os_err = APR_TO_OS_ERROR(err))                                  \
        {                                                                  \
          if (sleep_test)                                                  \
            {                                                              \
              ++retries;                                                   \
              apr_sleep(sleep_count);                                      \
              if (sleep_count < RETRY_MAX_SLEEP)                           \
                sleep_count *= 2;                                          \
            }                                                              \
          (err) = (expr);                                                  \
        }                                                                  \
    }                                                                      \
  while (0)

#if defined(EDEADLK) && APR_HAS_THREADS
#define FILE_LOCK_RETRY_LOOP(err, expr)                                    \
  RETRY_LOOP(err,                                                          \
             expr,                                                         \
             (APR_STATUS_IS_EINTR(err) || os_err == EDEADLK),              \
             (!APR_STATUS_IS_EINTR(err)))
#else
#define FILE_LOCK_RETRY_LOOP(err, expr)                                    \
  RETRY_LOOP(err,                                                          \
             expr,                                                         \
             (APR_STATUS_IS_EINTR(err)),                                   \
             0)
#endif

#ifndef WIN32_RETRY_LOOP
#if defined(WIN32) && !defined(SVN_NO_WIN32_RETRY_LOOP)
#define WIN32_RETRY_LOOP(err, expr)                                        \
  RETRY_LOOP(err, expr, (os_err == ERROR_ACCESS_DENIED                     \
                         || os_err == ERROR_SHARING_VIOLATION              \
                         || os_err == ERROR_DIR_NOT_EMPTY),                \
             1)
#else
#define WIN32_RETRY_LOOP(err, expr) ((void)0)
#endif
#endif

#ifdef WIN32

#if _WIN32_WINNT < 0x600 /* Does the SDK assume Windows Vista+? */
typedef struct _FILE_RENAME_INFO {
  BOOL   ReplaceIfExists;
  HANDLE RootDirectory;
  DWORD  FileNameLength;
  WCHAR  FileName[1];
} FILE_RENAME_INFO, *PFILE_RENAME_INFO;

typedef struct _FILE_DISPOSITION_INFO {
  BOOL DeleteFile;
} FILE_DISPOSITION_INFO, *PFILE_DISPOSITION_INFO;

#define FileRenameInfo 3
#define FileDispositionInfo 4
#endif /* WIN32 < Vista */

/* One-time initialization of the late bound Windows API functions. */
static volatile svn_atomic_t win_dynamic_imports_state = 0;

/* Pointer to GetFinalPathNameByHandleW function from kernel32.dll. */
typedef DWORD (WINAPI *GETFINALPATHNAMEBYHANDLE)(
               HANDLE hFile,
               WCHAR *lpszFilePath,
               DWORD cchFilePath,
               DWORD dwFlags);

typedef BOOL (WINAPI *SetFileInformationByHandle_t)(HANDLE hFile,
                                                    int FileInformationClass,
                                                    LPVOID lpFileInformation,
                                                    DWORD dwBufferSize);

static GETFINALPATHNAMEBYHANDLE get_final_path_name_by_handle_proc = NULL;
static SetFileInformationByHandle_t set_file_information_by_handle_proc = NULL;

/* Forward declaration. */
static svn_error_t * io_win_read_link(svn_string_t **dest,
                                      const char *path,
                                      apr_pool_t *pool);

#endif

/* Forward declaration */
static apr_status_t
dir_is_empty(const char *dir, apr_pool_t *pool);
static APR_INLINE svn_error_t *
do_io_file_wrapper_cleanup(apr_file_t *file, apr_status_t status,
                           const char *msg, const char *msg_no_name,
                           apr_pool_t *pool);

/* Local wrapper of svn_path_cstring_to_utf8() that does no copying on
 * operating systems where APR always uses utf-8 as native path format */
static svn_error_t *
cstring_to_utf8(const char **path_utf8,
                const char *path_apr,
                apr_pool_t *pool)
{
#if defined(WIN32) || defined(DARWIN)
  *path_utf8 = path_apr;
  return SVN_NO_ERROR;
#else
  return svn_path_cstring_to_utf8(path_utf8, path_apr, pool);
#endif
}

/* Local wrapper of svn_path_cstring_from_utf8() that does no copying on
 * operating systems where APR always uses utf-8 as native path format */
static svn_error_t *
cstring_from_utf8(const char **path_apr,
                  const char *path_utf8,
                  apr_pool_t *pool)
{
#if defined(WIN32) || defined(DARWIN)
  *path_apr = path_utf8;
  return SVN_NO_ERROR;
#else
  return svn_path_cstring_from_utf8(path_apr, path_utf8, pool);
#endif
}

/* Helper function that allows to convert an APR-level PATH to something
 * that we can pass the svn_error_wrap_apr. Since we use it in context
 * of error reporting, having *some* path info may be more useful than
 * having none.  Therefore, we use a best effort approach here.
 *
 * This is different from svn_io_file_name_get in that it uses a different
 * signature style and will never fail.
 */
static const char *
try_utf8_from_internal_style(const char *path, apr_pool_t *pool)
{
  svn_error_t *error;
  const char *path_utf8;

  /* Special case. */
  if (path == NULL)
    return "(NULL)";

  /* (try to) convert PATH to UTF-8. If that fails, continue with the plain
   * PATH because it is the best we have. It may actually be UTF-8 already.
   */
  error = cstring_to_utf8(&path_utf8, path, pool);
  if (error)
    {
      /* fallback to best representation we have */

      svn_error_clear(error);
      path_utf8 = path;
    }

  /* Toggle (back-)slashes etc. as necessary.
   */
  return svn_dirent_local_style(path_utf8, pool);
}


/* Set *NAME_P to the UTF-8 representation of directory entry NAME.
 * NAME is in the internal encoding used by APR; PARENT is in
 * UTF-8 and in internal (not local) style.
 *
 * Use PARENT only for generating an error string if the conversion
 * fails because NAME could not be represented in UTF-8.  In that
 * case, return a two-level error in which the outer error's message
 * mentions PARENT, but the inner error's message does not mention
 * NAME (except possibly in hex) since NAME may not be printable.
 * Such a compound error at least allows the user to go looking in the
 * right directory for the problem.
 *
 * If there is any other error, just return that error directly.
 *
 * If there is any error, the effect on *NAME_P is undefined.
 *
 * *NAME_P and NAME may refer to the same storage.
 */
static svn_error_t *
entry_name_to_utf8(const char **name_p,
                   const char *name,
                   const char *parent,
                   apr_pool_t *pool)
{
#if defined(WIN32) || defined(DARWIN)
  *name_p = apr_pstrdup(pool, name);
  return SVN_NO_ERROR;
#else
  svn_error_t *err = svn_path_cstring_to_utf8(name_p, name, pool);
  if (err && err->apr_err == APR_EINVAL)
    {
      return svn_error_createf(err->apr_err, err,
                               _("Error converting entry "
                                 "in directory '%s' to UTF-8"),
                               svn_dirent_local_style(parent, pool));
    }
  return err;
#endif
}



static void
map_apr_finfo_to_node_kind(svn_node_kind_t *kind,
                           svn_boolean_t *is_special,
                           apr_finfo_t *finfo)
{
  *is_special = FALSE;

  if (finfo->filetype == APR_REG)
    *kind = svn_node_file;
  else if (finfo->filetype == APR_DIR)
    *kind = svn_node_dir;
  else if (finfo->filetype == APR_LNK)
    {
      *is_special = TRUE;
      *kind = svn_node_file;
    }
  else
    *kind = svn_node_unknown;
}

/* Helper for svn_io_check_path() and svn_io_check_resolved_path();
   essentially the same semantics as those two, with the obvious
   interpretation for RESOLVE_SYMLINKS. */
static svn_error_t *
io_check_path(const char *path,
              svn_boolean_t resolve_symlinks,
              svn_boolean_t *is_special_p,
              svn_node_kind_t *kind,
              apr_pool_t *pool)
{
  apr_int32_t flags;
  apr_finfo_t finfo;
  apr_status_t apr_err;
  const char *path_apr;
  svn_boolean_t is_special = FALSE;

  if (path[0] == '\0')
    path = ".";

  /* Not using svn_io_stat() here because we want to check the
     apr_err return explicitly. */
  SVN_ERR(cstring_from_utf8(&path_apr, path, pool));

  flags = resolve_symlinks ? APR_FINFO_MIN : (APR_FINFO_MIN | APR_FINFO_LINK);
  apr_err = apr_stat(&finfo, path_apr, flags, pool);

  if (APR_STATUS_IS_ENOENT(apr_err))
    *kind = svn_node_none;
  else if (SVN__APR_STATUS_IS_ENOTDIR(apr_err))
    *kind = svn_node_none;
  else if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't check path '%s'"),
                              svn_dirent_local_style(path, pool));
  else
    map_apr_finfo_to_node_kind(kind, &is_special, &finfo);

  *is_special_p = is_special;

  return SVN_NO_ERROR;
}


/* Wrapper for apr_file_open(), taking an APR-encoded filename. */
static apr_status_t
file_open(apr_file_t **f,
          const char *fname_apr,
          apr_int32_t flag,
          apr_fileperms_t perm,
          svn_boolean_t retry_on_failure,
          apr_pool_t *pool)
{
  apr_status_t status = apr_file_open(f, fname_apr, flag, perm, pool);

  if (retry_on_failure)
    {
#ifdef WIN32
      if (status == APR_FROM_OS_ERROR(ERROR_ACCESS_DENIED))
        {
          if ((flag & (APR_CREATE | APR_EXCL)) == (APR_CREATE | APR_EXCL))
            return status; /* Can't create if there is something */

          if (flag & (APR_WRITE | APR_CREATE))
            {
              apr_finfo_t finfo;

              if (!apr_stat(&finfo, fname_apr, SVN__APR_FINFO_READONLY, pool))
                {
                  if (finfo.protection & APR_FREADONLY)
                    return status; /* Retrying won't fix this */
                }
            }
        }
#endif

      WIN32_RETRY_LOOP(status, apr_file_open(f, fname_apr, flag, perm, pool));
    }
  return status;
}


svn_error_t *
svn_io_check_resolved_path(const char *path,
                           svn_node_kind_t *kind,
                           apr_pool_t *pool)
{
  svn_boolean_t ignored;
  return io_check_path(path, TRUE, &ignored, kind, pool);
}

svn_error_t *
svn_io_check_path(const char *path,
                  svn_node_kind_t *kind,
                  apr_pool_t *pool)
{
  svn_boolean_t ignored;
  return io_check_path(path, FALSE, &ignored, kind, pool);
}

svn_error_t *
svn_io_check_special_path(const char *path,
                          svn_node_kind_t *kind,
                          svn_boolean_t *is_special,
                          apr_pool_t *pool)
{
  return io_check_path(path, FALSE, is_special, kind, pool);
}

struct temp_file_cleanup_s
{
  apr_pool_t *pool;
  /* The (APR-encoded) full path of the file to be removed, or NULL if
   * nothing to do. */
  const char *fname_apr;
};


static apr_status_t
temp_file_plain_cleanup_handler(void *baton)
{
  struct  temp_file_cleanup_s *b = baton;
  apr_status_t apr_err = APR_SUCCESS;

  if (b->fname_apr)
    {
      apr_err = apr_file_remove(b->fname_apr, b->pool);
      WIN32_RETRY_LOOP(apr_err, apr_file_remove(b->fname_apr, b->pool));
    }

  return apr_err;
}


static apr_status_t
temp_file_child_cleanup_handler(void *baton)
{
  struct  temp_file_cleanup_s *b = baton;

  apr_pool_cleanup_kill(b->pool, b,
                        temp_file_plain_cleanup_handler);

  return APR_SUCCESS;
}


svn_error_t *
svn_io_open_uniquely_named(apr_file_t **file,
                           const char **unique_path,
                           const char *dirpath,
                           const char *filename,
                           const char *suffix,
                           svn_io_file_del_t delete_when,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *path;
  unsigned int i;
  struct temp_file_cleanup_s *baton = NULL;

  /* At the beginning, we don't know whether unique_path will need
     UTF8 conversion */
  svn_boolean_t needs_utf8_conversion = TRUE;

  SVN_ERR_ASSERT(file || unique_path);

  if (dirpath == NULL)
    SVN_ERR(svn_io_temp_dir(&dirpath, scratch_pool));
  if (filename == NULL)
    filename = "tempfile";
  if (suffix == NULL)
    suffix = ".tmp";

  path = svn_dirent_join(dirpath, filename, scratch_pool);

  if (delete_when == svn_io_file_del_on_pool_cleanup)
    {
      baton = apr_palloc(result_pool, sizeof(*baton));

      baton->pool = result_pool;
      baton->fname_apr = NULL;

      /* Because cleanups are run LIFO, we need to make sure to register
         our cleanup before the apr_file_close cleanup:

         On Windows, you can't remove an open file.
      */
      apr_pool_cleanup_register(result_pool, baton,
                                temp_file_plain_cleanup_handler,
                                temp_file_child_cleanup_handler);
    }

  for (i = 1; i <= 99999; i++)
    {
      const char *unique_name;
      const char *unique_name_apr;
      apr_file_t *try_file;
      apr_status_t apr_err;
      apr_int32_t flag = (APR_READ | APR_WRITE | APR_CREATE | APR_EXCL
                          | APR_BUFFERED | APR_BINARY);

      if (delete_when == svn_io_file_del_on_close)
        flag |= APR_DELONCLOSE;

      /* Special case the first attempt -- if we can avoid having a
         generated numeric portion at all, that's best.  So first we
         try with just the suffix; then future tries add a number
         before the suffix.  (A do-while loop could avoid the repeated
         conditional, but it's not worth the clarity loss.)

         If the first attempt fails, the first number will be "2".
         This is good, since "1" would misleadingly imply that
         the second attempt was actually the first... and if someone's
         got conflicts on their conflicts, we probably don't want to
         add to their confusion :-). */
      if (i == 1)
        unique_name = apr_psprintf(scratch_pool, "%s%s", path, suffix);
      else
        unique_name = apr_psprintf(scratch_pool, "%s.%u%s", path, i, suffix);

      /* Hmmm.  Ideally, we would append to a native-encoding buf
         before starting iteration, then convert back to UTF-8 for
         return. But I suppose that would make the appending code
         sensitive to i18n in a way it shouldn't be... Oh well. */
      if (needs_utf8_conversion)
        {
          SVN_ERR(cstring_from_utf8(&unique_name_apr, unique_name,
                                    scratch_pool));
          if (i == 1)
            {
              /* The variable parts of unique_name will not require UTF8
                 conversion. Therefore, if UTF8 conversion had no effect
                 on it in the first iteration, it won't require conversion
                 in any future iteration. */
              needs_utf8_conversion = strcmp(unique_name_apr, unique_name);
            }
        }
      else
        unique_name_apr = unique_name;

      apr_err = file_open(&try_file, unique_name_apr, flag,
                          APR_OS_DEFAULT, FALSE, result_pool);

      if (APR_STATUS_IS_EEXIST(apr_err))
        continue;
      else if (apr_err)
        {
          /* On Win32, CreateFile fails with an "Access Denied" error
             code, rather than "File Already Exists", if the colliding
             name belongs to a directory. */
          if (APR_STATUS_IS_EACCES(apr_err))
            {
              apr_finfo_t finfo;
              apr_status_t apr_err_2 = apr_stat(&finfo, unique_name_apr,
                                                APR_FINFO_TYPE, scratch_pool);

              if (!apr_err_2 && finfo.filetype == APR_DIR)
                continue;

#ifdef WIN32
              if (apr_err == APR_FROM_OS_ERROR(ERROR_ACCESS_DENIED) ||
                  apr_err == APR_FROM_OS_ERROR(ERROR_SHARING_VIOLATION))
                {
                  /* The file is in use by another process or is hidden;
                     create a new name, but don't do this 99999 times in
                     case the folder is not writable */
                  i += 797;
                  continue;
                }
#endif

              /* Else fall through and return the original error. */
            }

          if (file)
            *file = NULL;
          if (unique_path)
            *unique_path = NULL;
          return svn_error_wrap_apr(apr_err, _("Can't open '%s'"),
                                    svn_dirent_local_style(unique_name,
                                                         scratch_pool));
        }
      else
        {
          if (delete_when == svn_io_file_del_on_pool_cleanup)
            baton->fname_apr = apr_pstrdup(result_pool, unique_name_apr);

          if (file)
            *file = try_file;
          else
            apr_file_close(try_file);
          if (unique_path)
            *unique_path = apr_pstrdup(result_pool, unique_name);

          return SVN_NO_ERROR;
        }
    }

  if (file)
    *file = NULL;
  if (unique_path)
    *unique_path = NULL;
  return svn_error_createf(SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
                           NULL,
                           _("Unable to make name for '%s'"),
                           svn_dirent_local_style(path, scratch_pool));
}

svn_error_t *
svn_io_create_unique_link(const char **unique_name_p,
                          const char *path,
                          const char *dest,
                          const char *suffix,
                          apr_pool_t *pool)
{
#ifdef HAVE_SYMLINK
  unsigned int i;
  const char *unique_name;
  const char *unique_name_apr;
  const char *dest_apr;
  int rv;

  SVN_ERR(cstring_from_utf8(&dest_apr, dest, pool));
  for (i = 1; i <= 99999; i++)
    {
      apr_status_t apr_err;

      /* Special case the first attempt -- if we can avoid having a
         generated numeric portion at all, that's best.  So first we
         try with just the suffix; then future tries add a number
         before the suffix.  (A do-while loop could avoid the repeated
         conditional, but it's not worth the clarity loss.)

         If the first attempt fails, the first number will be "2".
         This is good, since "1" would misleadingly imply that
         the second attempt was actually the first... and if someone's
         got conflicts on their conflicts, we probably don't want to
         add to their confusion :-). */
      if (i == 1)
        unique_name = apr_psprintf(pool, "%s%s", path, suffix);
      else
        unique_name = apr_psprintf(pool, "%s.%u%s", path, i, suffix);

      /* Hmmm.  Ideally, we would append to a native-encoding buf
         before starting iteration, then convert back to UTF-8 for
         return. But I suppose that would make the appending code
         sensitive to i18n in a way it shouldn't be... Oh well. */
      SVN_ERR(cstring_from_utf8(&unique_name_apr, unique_name, pool));
      do {
        rv = symlink(dest_apr, unique_name_apr);
      } while (rv == -1 && APR_STATUS_IS_EINTR(apr_get_os_error()));

      apr_err = apr_get_os_error();

      if (rv == -1 && APR_STATUS_IS_EEXIST(apr_err))
        continue;
      else if (rv == -1 && apr_err)
        {
          /* On Win32, CreateFile fails with an "Access Denied" error
             code, rather than "File Already Exists", if the colliding
             name belongs to a directory. */
          if (APR_STATUS_IS_EACCES(apr_err))
            {
              apr_finfo_t finfo;
              apr_status_t apr_err_2 = apr_stat(&finfo, unique_name_apr,
                                                APR_FINFO_TYPE, pool);

              if (!apr_err_2
                  && (finfo.filetype == APR_DIR))
                continue;

              /* Else ignore apr_err_2; better to fall through and
                 return the original error. */
            }

          *unique_name_p = NULL;
          return svn_error_wrap_apr(apr_err,
                                    _("Can't create symbolic link '%s'"),
                                    svn_dirent_local_style(unique_name, pool));
        }
      else
        {
          *unique_name_p = unique_name;
          return SVN_NO_ERROR;
        }
    }

  *unique_name_p = NULL;
  return svn_error_createf(SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
                           NULL,
                           _("Unable to make name for '%s'"),
                           svn_dirent_local_style(path, pool));
#else
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          _("Symbolic links are not supported on this "
                            "platform"));
#endif
}

svn_error_t *
svn_io_read_link(svn_string_t **dest,
                 const char *path,
                 apr_pool_t *pool)
{
#if defined(HAVE_READLINK)
  svn_string_t dest_apr;
  const char *path_apr;
  char buf[1025];
  ssize_t rv;

  SVN_ERR(cstring_from_utf8(&path_apr, path, pool));
  do {
    rv = readlink(path_apr, buf, sizeof(buf) - 1);
  } while (rv == -1 && APR_STATUS_IS_EINTR(apr_get_os_error()));

  if (rv == -1)
    return svn_error_wrap_apr(apr_get_os_error(),
                              _("Can't read contents of link"));

  buf[rv] = '\0';
  dest_apr.data = buf;
  dest_apr.len = rv;

  /* ### Cast needed, one of these interfaces is wrong */
  return svn_utf_string_to_utf8((const svn_string_t **)dest, &dest_apr, pool);
#elif defined(WIN32)
  return io_win_read_link(dest, path, pool);
#else
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          _("Symbolic links are not supported on this "
                            "platform"));
#endif
}


svn_error_t *
svn_io_copy_link(const char *src,
                 const char *dst,
                 apr_pool_t *pool)

{
#ifdef HAVE_READLINK
  svn_string_t *link_dest;
  const char *dst_tmp;

  /* Notice what the link is pointing at... */
  SVN_ERR(svn_io_read_link(&link_dest, src, pool));

  /* Make a tmp-link pointing at the same thing. */
  SVN_ERR(svn_io_create_unique_link(&dst_tmp, dst, link_dest->data,
                                    ".tmp", pool));

  /* Move the tmp-link to link. */
  return svn_io_file_rename2(dst_tmp, dst, FALSE, pool);

#else
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          _("Symbolic links are not supported on this "
                            "platform"));
#endif
}

/* Temporary directory name cache for svn_io_temp_dir() */
static volatile svn_atomic_t temp_dir_init_state = 0;
static const char *temp_dir;

/* Helper function to initialize temp dir. Passed to svn_atomic__init_once */
static svn_error_t *
init_temp_dir(void *baton, apr_pool_t *scratch_pool)
{
  /* Global pool for the temp path */
  apr_pool_t *global_pool = svn_pool_create(NULL);
  const char *dir;

  apr_status_t apr_err = apr_temp_dir_get(&dir, scratch_pool);

  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't find a temporary directory"));

  SVN_ERR(cstring_to_utf8(&dir, dir, scratch_pool));

  dir = svn_dirent_internal_style(dir, scratch_pool);

  SVN_ERR(svn_dirent_get_absolute(&temp_dir, dir, global_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_temp_dir(const char **dir,
                apr_pool_t *pool)
{
  SVN_ERR(svn_atomic__init_once(&temp_dir_init_state,
                                init_temp_dir, NULL, pool));

  *dir = apr_pstrdup(pool, temp_dir);

  return SVN_NO_ERROR;
}




/*** Creating, copying and appending files. ***/

/* Transfer the contents of FROM_FILE to TO_FILE, using POOL for temporary
 * allocations.
 *
 * NOTE: We don't use apr_copy_file() for this, since it takes filenames
 * as parameters.  Since we want to copy to a temporary file
 * and rename for atomicity (see below), this would require an extra
 * close/open pair, which can be expensive, especially on
 * remote file systems.
 */
static apr_status_t
copy_contents(apr_file_t *from_file,
              apr_file_t *to_file,
              apr_pool_t *pool)
{
  /* Copy bytes till the cows come home. */
  while (1)
    {
      char buf[SVN__STREAM_CHUNK_SIZE];
      apr_size_t bytes_this_time = sizeof(buf);
      apr_status_t read_err;
      apr_status_t write_err;

      /* Read 'em. */
      read_err = apr_file_read(from_file, buf, &bytes_this_time);
      if (read_err && !APR_STATUS_IS_EOF(read_err))
        {
          return read_err;
        }

      /* Write 'em. */
      write_err = apr_file_write_full(to_file, buf, bytes_this_time, NULL);
      if (write_err)
        {
          return write_err;
        }

      if (read_err && APR_STATUS_IS_EOF(read_err))
        {
          /* Return the results of this close: an error, or success. */
          return APR_SUCCESS;
        }
    }
  /* NOTREACHED */
}


svn_error_t *
svn_io_copy_file(const char *src,
                 const char *dst,
                 svn_boolean_t copy_perms,
                 apr_pool_t *pool)
{
  apr_file_t *from_file, *to_file;
  apr_status_t apr_err;
  const char *dst_tmp;
  svn_error_t *err;

  /* ### NOTE: sometimes src == dst. In this case, because we copy to a
     ###   temporary file, and then rename over the top of the destination,
     ###   the net result is resetting the permissions on src/dst.
     ###
     ### Note: specifically, this can happen during a switch when the desired
     ###   permissions for a file change from one branch to another. See
     ###   switch_tests 17.
     ###
     ### ... yes, we should avoid copying to the same file, and we should
     ###     make the "reset perms" explicit. The switch *happens* to work
     ###     because of this copy-to-temp-then-rename implementation. If it
     ###     weren't for that, the switch would break.
  */
#ifdef CHECK_FOR_SAME_FILE
  if (strcmp(src, dst) == 0)
    return SVN_NO_ERROR;
#endif

  SVN_ERR(svn_io_file_open(&from_file, src, APR_READ,
                           APR_OS_DEFAULT, pool));

  /* For atomicity, we copy to a tmp file and then rename the tmp
     file over the real destination. */

  SVN_ERR(svn_io_open_unique_file3(&to_file, &dst_tmp,
                                   svn_dirent_dirname(dst, pool),
                                   svn_io_file_del_none, pool, pool));

  apr_err = copy_contents(from_file, to_file, pool);

  if (apr_err)
    {
      err = svn_error_wrap_apr(apr_err, _("Can't copy '%s' to '%s'"),
                               svn_dirent_local_style(src, pool),
                               svn_dirent_local_style(dst_tmp, pool));
    }
   else
     err = NULL;

  err = svn_error_compose_create(err,
                                 svn_io_file_close(from_file, pool));

  err = svn_error_compose_create(err,
                                 svn_io_file_close(to_file, pool));

  if (err)
    {
      return svn_error_compose_create(
                                 err,
                                 svn_io_remove_file2(dst_tmp, TRUE, pool));
    }

  /* If copying perms, set the perms on dst_tmp now, so they will be
     atomically inherited in the upcoming rename.  But note that we
     had to wait until now to set perms, because if they say
     read-only, then we'd have failed filling dst_tmp's contents. */
  if (copy_perms)
    SVN_ERR(svn_io_copy_perms(src, dst_tmp, pool));

  return svn_error_trace(svn_io_file_rename2(dst_tmp, dst, FALSE, pool));
}

#if !defined(WIN32) && !defined(__OS2__)
/* Wrapper for apr_file_perms_set(), taking a UTF8-encoded filename. */
static svn_error_t *
file_perms_set(const char *fname, apr_fileperms_t perms,
               apr_pool_t *pool)
{
  const char *fname_apr;
  apr_status_t status;

  SVN_ERR(cstring_from_utf8(&fname_apr, fname, pool));

  status = apr_file_perms_set(fname_apr, perms);
  if (status)
    return svn_error_wrap_apr(status, _("Can't set permissions on '%s'"),
                              fname);
  else
    return SVN_NO_ERROR;
}

/* Set permissions PERMS on the FILE. This is a cheaper variant of the
 * file_perms_set wrapper() function because no locale-dependent string
 * conversion is required. POOL will be used for allocations.
 */
static svn_error_t *
file_perms_set2(apr_file_t* file, apr_fileperms_t perms, apr_pool_t *pool)
{
  const char *fname_apr;
  apr_status_t status;

  status = apr_file_name_get(&fname_apr, file);
  if (status)
    return svn_error_wrap_apr(status, _("Can't get file name"));

  status = apr_file_perms_set(fname_apr, perms);
  if (status)
    return svn_error_wrap_apr(status, _("Can't set permissions on '%s'"),
                              try_utf8_from_internal_style(fname_apr, pool));
  else
    return SVN_NO_ERROR;
}

#endif /* !WIN32 && !__OS2__ */

svn_error_t *
svn_io_copy_perms(const char *src,
                  const char *dst,
                  apr_pool_t *pool)
{
  /* ### On Windows or OS/2, apr_file_perms_set always returns APR_ENOTIMPL,
         and the path passed to apr_file_perms_set must be encoded
         in the platform-specific path encoding; not necessary UTF-8.
         We need a platform-specific implementation to get the
         permissions right. */

#if !defined(WIN32) && !defined(__OS2__)
  {
    apr_finfo_t finfo;
    svn_node_kind_t kind;
    svn_boolean_t is_special;
    svn_error_t *err;

    /* If DST is a symlink, don't bother copying permissions. */
    SVN_ERR(svn_io_check_special_path(dst, &kind, &is_special, pool));
    if (is_special)
      return SVN_NO_ERROR;

    SVN_ERR(svn_io_stat(&finfo, src, APR_FINFO_PROT, pool));
    err = file_perms_set(dst, finfo.protection, pool);
    if (err)
      {
        /* We shouldn't be able to get APR_INCOMPLETE or APR_ENOTIMPL
           here under normal circumstances, because the perms themselves
           came from a call to apr_file_info_get(), and we already know
           this is the non-Win32 case.  But if it does happen, it's not
           an error. */
        if (APR_STATUS_IS_INCOMPLETE(err->apr_err) ||
            APR_STATUS_IS_ENOTIMPL(err->apr_err))
          svn_error_clear(err);
        else
          {
            return svn_error_quick_wrapf(
                     err, _("Can't set permissions on '%s'"),
                     svn_dirent_local_style(dst, pool));
          }
      }
  }
#endif /* !WIN32 && !__OS2__ */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_append_file(const char *src, const char *dst, apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char *src_apr, *dst_apr;

  SVN_ERR(cstring_from_utf8(&src_apr, src, pool));
  SVN_ERR(cstring_from_utf8(&dst_apr, dst, pool));

  apr_err = apr_file_append(src_apr, dst_apr, APR_OS_DEFAULT, pool);

  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't append '%s' to '%s'"),
                              svn_dirent_local_style(src, pool),
                              svn_dirent_local_style(dst, pool));

  return SVN_NO_ERROR;
}


svn_error_t *svn_io_copy_dir_recursively(const char *src,
                                         const char *dst_parent,
                                         const char *dst_basename,
                                         svn_boolean_t copy_perms,
                                         svn_cancel_func_t cancel_func,
                                         void *cancel_baton,
                                         apr_pool_t *pool)
{
  svn_node_kind_t kind;
  apr_status_t status;
  const char *dst_path;
  apr_dir_t *this_dir;
  apr_finfo_t this_entry;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;

  /* Make a subpool for recursion */
  apr_pool_t *subpool = svn_pool_create(pool);

  /* The 'dst_path' is simply dst_parent/dst_basename */
  dst_path = svn_dirent_join(dst_parent, dst_basename, pool);

  /* Sanity checks:  SRC and DST_PARENT are directories, and
     DST_BASENAME doesn't already exist in DST_PARENT. */
  SVN_ERR(svn_io_check_path(src, &kind, subpool));
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("Source '%s' is not a directory"),
                             svn_dirent_local_style(src, pool));

  SVN_ERR(svn_io_check_path(dst_parent, &kind, subpool));
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("Destination '%s' is not a directory"),
                             svn_dirent_local_style(dst_parent, pool));

  SVN_ERR(svn_io_check_path(dst_path, &kind, subpool));
  if (kind != svn_node_none)
    return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                             _("Destination '%s' already exists"),
                             svn_dirent_local_style(dst_path, pool));

  /* Create the new directory. */
  /* ### TODO: copy permissions (needs apr_file_attrs_get()) */
  SVN_ERR(svn_io_dir_make(dst_path, APR_OS_DEFAULT, pool));

  /* Loop over the dirents in SRC.  ('.' and '..' are auto-excluded) */
  SVN_ERR(svn_io_dir_open(&this_dir, src, subpool));

  for (status = apr_dir_read(&this_entry, flags, this_dir);
       status == APR_SUCCESS;
       status = apr_dir_read(&this_entry, flags, this_dir))
    {
      if ((this_entry.name[0] == '.')
          && ((this_entry.name[1] == '\0')
              || ((this_entry.name[1] == '.')
                  && (this_entry.name[2] == '\0'))))
        {
          continue;
        }
      else
        {
          const char *src_target, *entryname_utf8;

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          SVN_ERR(entry_name_to_utf8(&entryname_utf8, this_entry.name,
                                     src, subpool));
          src_target = svn_dirent_join(src, entryname_utf8, subpool);

          if (this_entry.filetype == APR_REG) /* regular file */
            {
              const char *dst_target = svn_dirent_join(dst_path,
                                                       entryname_utf8,
                                                       subpool);
              SVN_ERR(svn_io_copy_file(src_target, dst_target,
                                       copy_perms, subpool));
            }
          else if (this_entry.filetype == APR_LNK) /* symlink */
            {
              const char *dst_target = svn_dirent_join(dst_path,
                                                       entryname_utf8,
                                                       subpool);
              SVN_ERR(svn_io_copy_link(src_target, dst_target,
                                       subpool));
            }
          else if (this_entry.filetype == APR_DIR) /* recurse */
            {
              /* Prevent infinite recursion by filtering off our
                 newly created destination path. */
              if (strcmp(src, dst_parent) == 0
                  && strcmp(entryname_utf8, dst_basename) == 0)
                continue;

              SVN_ERR(svn_io_copy_dir_recursively
                      (src_target,
                       dst_path,
                       entryname_utf8,
                       copy_perms,
                       cancel_func,
                       cancel_baton,
                       subpool));
            }
          /* ### support other APR node types someday?? */

        }
    }

  if (! (APR_STATUS_IS_ENOENT(status)))
    return svn_error_wrap_apr(status, _("Can't read directory '%s'"),
                              svn_dirent_local_style(src, pool));

  status = apr_dir_close(this_dir);
  if (status)
    return svn_error_wrap_apr(status, _("Error closing directory '%s'"),
                              svn_dirent_local_style(src, pool));

  /* Free any memory used by recursion */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_make_dir_recursively(const char *path, apr_pool_t *pool)
{
  const char *path_apr;
  apr_status_t apr_err;

  if (svn_path_is_empty(path))
    /* Empty path (current dir) is assumed to always exist,
       so we do nothing, per docs. */
    return SVN_NO_ERROR;

  SVN_ERR(cstring_from_utf8(&path_apr, path, pool));

  apr_err = apr_dir_make_recursive(path_apr, APR_OS_DEFAULT, pool);
#ifdef WIN32
  /* Don't retry on ERROR_ACCESS_DENIED, as that typically signals a
     permanent error */
  if (apr_err == APR_FROM_OS_ERROR(ERROR_SHARING_VIOLATION))
    WIN32_RETRY_LOOP(apr_err, apr_dir_make_recursive(path_apr,
                                                     APR_OS_DEFAULT, pool));
#endif

  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't make directory '%s'"),
                              svn_dirent_local_style(path, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_file_create_bytes(const char *file,
                         const void *contents,
                         apr_size_t length,
                         apr_pool_t *scratch_pool)
{
  apr_file_t *f;
  apr_size_t written;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR(svn_io_file_open(&f, file,
                           (APR_WRITE | APR_CREATE | APR_EXCL),
                           APR_OS_DEFAULT,
                           scratch_pool));
  if (length)
    err = svn_io_file_write_full(f, contents, length, &written,
                                 scratch_pool);

  err = svn_error_compose_create(
                    err,
                    svn_io_file_close(f, scratch_pool));

  if (err)
    {
      /* Our caller doesn't know if we left a file or not if we return
         an error. Better to cleanup after ourselves if we created the
         file. */
      return svn_error_trace(
                svn_error_compose_create(
                    err,
                    svn_io_remove_file2(file, TRUE, scratch_pool)));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_file_create(const char *file,
                   const char *contents,
                   apr_pool_t *pool)
{
  return svn_error_trace(svn_io_file_create_bytes(file, contents,
                                                  contents ? strlen(contents)
                                                           : 0,
                                                  pool));
}

svn_error_t *
svn_io_file_create_empty(const char *file,
                         apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_io_file_create_bytes(file, NULL, 0,
                                                  scratch_pool));
}

svn_error_t *
svn_io_dir_file_copy(const char *src_path,
                     const char *dest_path,
                     const char *file,
                     apr_pool_t *pool)
{
  const char *file_dest_path = svn_dirent_join(dest_path, file, pool);
  const char *file_src_path = svn_dirent_join(src_path, file, pool);

  return svn_error_trace(
            svn_io_copy_file(file_src_path, file_dest_path, TRUE, pool));
}


/*** Modtime checking. ***/

svn_error_t *
svn_io_file_affected_time(apr_time_t *apr_time,
                          const char *path,
                          apr_pool_t *pool)
{
  apr_finfo_t finfo;

  SVN_ERR(svn_io_stat(&finfo, path, APR_FINFO_MIN | APR_FINFO_LINK, pool));

  *apr_time = finfo.mtime;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_set_file_affected_time(apr_time_t apr_time,
                              const char *path,
                              apr_pool_t *pool)
{
  apr_status_t status;
  const char *native_path;

  SVN_ERR(cstring_from_utf8(&native_path, path, pool));
  status = apr_file_mtime_set(native_path, apr_time, pool);

  if (status)
    return svn_error_wrap_apr(status, _("Can't set access time of '%s'"),
                              svn_dirent_local_style(path, pool));

  return SVN_NO_ERROR;
}


void
svn_io_sleep_for_timestamps(const char *path, apr_pool_t *pool)
{
  apr_time_t now, then;
  svn_error_t *err;
  char *sleep_env_var;

  sleep_env_var = getenv(SVN_SLEEP_ENV_VAR);

  if (sleep_env_var && apr_strnatcasecmp(sleep_env_var, "yes") == 0)
    return; /* Allow skipping for testing */

  now = apr_time_now();

  /* Calculate 0.02 seconds after the next second wallclock tick. */
  then = apr_time_make(apr_time_sec(now) + 1, APR_USEC_PER_SEC / 50);

  /* Worst case is waiting one second, so we can use that time to determine
     if we can sleep shorter than that */
  if (path)
    {
      apr_finfo_t finfo;

      err = svn_io_stat(&finfo, path, APR_FINFO_MTIME | APR_FINFO_LINK, pool);

      if (err)
        {
          svn_error_clear(err); /* Fall back on original behavior */
        }
      else if (finfo.mtime % APR_USEC_PER_SEC)
        {
          /* Very simplistic but safe approach:
              If the filesystem has < sec mtime we can be reasonably sure
              that the filesystem has some sub-second resolution.  On Windows
              it is likely to be sub-millisecond; on Linux systems it depends
              on the filesystem, ext4 is typically 1ms, 4ms or 10ms resolution.

             ## Perhaps find a better algorithm here. This will fail once
                in every 1000 cases on a millisecond precision filesystem
                if the mtime happens to be an exact second.

                But better to fail once in every thousand cases than every
                time, like we did before.

             Note for further research on algorithm:
               FAT32 has < 1 sec precision on ctime, but 2 sec on mtime.

               Linux/ext4 with CONFIG_HZ=250 has high resolution
               apr_time_now and although the filesystem timestamps
               have similar high precision they are only updated with
               a coarser 4ms resolution. */

          /* 10 milliseconds after now. */
#ifndef SVN_HI_RES_SLEEP_MS
#define SVN_HI_RES_SLEEP_MS 10
#endif
          then = now + apr_time_from_msec(SVN_HI_RES_SLEEP_MS);
        }

      /* Remove time taken to do stat() from sleep. */
      now = apr_time_now();
    }

  if (now >= then)
    return; /* Passing negative values may suspend indefinitely (Windows) */

  /* (t < 1000 will be round to 0 in apr) */
  if (then - now < 1000)
    apr_sleep(1000);
  else
    apr_sleep(then - now);
}


svn_error_t *
svn_io_filesizes_different_p(svn_boolean_t *different_p,
                             const char *file1,
                             const char *file2,
                             apr_pool_t *pool)
{
  apr_finfo_t finfo1;
  apr_finfo_t finfo2;
  apr_status_t status;
  const char *file1_apr, *file2_apr;

  /* Not using svn_io_stat() because don't want to generate
     svn_error_t objects for non-error conditions. */

  SVN_ERR(cstring_from_utf8(&file1_apr, file1, pool));
  SVN_ERR(cstring_from_utf8(&file2_apr, file2, pool));

  /* Stat both files */
  status = apr_stat(&finfo1, file1_apr, APR_FINFO_MIN, pool);
  if (status)
    {
      /* If we got an error stat'ing a file, it could be because the
         file was removed... or who knows.  Whatever the case, we
         don't know if the filesizes are definitely different, so
         assume that they're not. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }

  status = apr_stat(&finfo2, file2_apr, APR_FINFO_MIN, pool);
  if (status)
    {
      /* See previous comment. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }

  /* Examine file sizes */
  if (finfo1.size == finfo2.size)
    *different_p = FALSE;
  else
    *different_p = TRUE;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_filesizes_three_different_p(svn_boolean_t *different_p12,
                                   svn_boolean_t *different_p23,
                                   svn_boolean_t *different_p13,
                                   const char *file1,
                                   const char *file2,
                                   const char *file3,
                                   apr_pool_t *scratch_pool)
{
  apr_finfo_t finfo1, finfo2, finfo3;
  apr_status_t status1, status2, status3;
  const char *file1_apr, *file2_apr, *file3_apr;

  /* Not using svn_io_stat() because don't want to generate
     svn_error_t objects for non-error conditions. */

  SVN_ERR(cstring_from_utf8(&file1_apr, file1, scratch_pool));
  SVN_ERR(cstring_from_utf8(&file2_apr, file2, scratch_pool));
  SVN_ERR(cstring_from_utf8(&file3_apr, file3, scratch_pool));

  /* Stat all three files */
  status1 = apr_stat(&finfo1, file1_apr, APR_FINFO_MIN, scratch_pool);
  status2 = apr_stat(&finfo2, file2_apr, APR_FINFO_MIN, scratch_pool);
  status3 = apr_stat(&finfo3, file3_apr, APR_FINFO_MIN, scratch_pool);

  /* If we got an error stat'ing a file, it could be because the
     file was removed... or who knows.  Whatever the case, we
     don't know if the filesizes are definitely different, so
     assume that they're not. */
  *different_p12 = !status1 && !status2 && finfo1.size != finfo2.size;
  *different_p23 = !status2 && !status3 && finfo2.size != finfo3.size;
  *different_p13 = !status1 && !status3 && finfo1.size != finfo3.size;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_file_checksum2(svn_checksum_t **checksum,
                      const char *file,
                      svn_checksum_kind_t kind,
                      apr_pool_t *pool)
{
  svn_stream_t *file_stream;
  apr_file_t* f;

  SVN_ERR(svn_io_file_open(&f, file, APR_READ, APR_OS_DEFAULT, pool));
  file_stream = svn_stream_from_aprfile2(f, FALSE, pool);
  SVN_ERR(svn_stream_contents_checksum(checksum, file_stream, kind,
                                       pool, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_file_checksum(unsigned char digest[],
                     const char *file,
                     apr_pool_t *pool)
{
  svn_checksum_t *checksum;

  SVN_ERR(svn_io_file_checksum2(&checksum, file, svn_checksum_md5, pool));
  memcpy(digest, checksum->digest, APR_MD5_DIGESTSIZE);

  return SVN_NO_ERROR;
}



/*** Permissions and modes. ***/

#if !defined(WIN32) && !defined(__OS2__)
/* Given the file specified by PATH, attempt to create an
   identical version of it owned by the current user.  This is done by
   moving it to a temporary location, copying the file back to its old
   path, then deleting the temporarily moved version.  All temporary
   allocations are done in POOL. */
static svn_error_t *
reown_file(const char *path,
           apr_pool_t *pool)
{
  const char *unique_name;

  SVN_ERR(svn_io_open_unique_file3(NULL, &unique_name,
                                   svn_dirent_dirname(path, pool),
                                   svn_io_file_del_none, pool, pool));
  SVN_ERR(svn_io_file_rename2(path, unique_name, FALSE, pool));
  SVN_ERR(svn_io_copy_file(unique_name, path, TRUE, pool));
  return svn_error_trace(svn_io_remove_file2(unique_name, FALSE, pool));
}

/* Determine what the PERMS for a new file should be by looking at the
   permissions of a temporary file that we create in DIRECTORY.  
   DIRECTORY can be NULL in which case the system temporary dir is used.
   Unfortunately, umask() as defined in POSIX provides no thread-safe way
   to get at the current value of the umask, so what we're doing here is
   the only way we have to determine which combination of write bits
   (User/Group/World) should be set by default.
   Make temporary allocations in SCRATCH_POOL.  */
static svn_error_t *
get_default_file_perms(apr_fileperms_t *perms,
                       const char *directory,
                       apr_pool_t *scratch_pool)
{
  /* the default permissions as read from the temp folder */
  static apr_fileperms_t default_perms = 0;

  /* Technically, this "racy": Multiple threads may use enter here and
     try to figure out the default permission concurrently. That's fine
     since they will end up with the same results. Even more technical,
     apr_fileperms_t is an atomic type on 32+ bit machines.
   */
  if (default_perms == 0)
    {
      apr_finfo_t finfo;
      apr_file_t *fd;
      const char *fname_base, *fname;
      apr_uint32_t randomish;
      svn_error_t *err;

      /* Get the perms for a newly created file to find out what bits
        should be set.

        Explicitly delete the file because we want this file to be as
        short-lived as possible since its presence means other
        processes may have to try multiple names.

        Using svn_io_open_uniquely_named() here because other tempfile
        creation functions tweak the permission bits of files they create.

        Note that APR pool structures are allocated as the first item
        in their first memory page (with e.g. 4kB granularity), i.e. the
        lower bits tend to be identical between pool instances.  That is
        particularly true for the MMAPed allocator.
      */
      randomish = ((apr_uint32_t)(apr_uintptr_t)scratch_pool
                   + (apr_uint32_t)((apr_uintptr_t)scratch_pool / 4096)
                   + (apr_uint32_t)apr_time_now());
      fname_base = apr_psprintf(scratch_pool, "svn-%08x", randomish);

      SVN_ERR(svn_io_open_uniquely_named(&fd, &fname, directory, fname_base,
                                         NULL, svn_io_file_del_none,
                                         scratch_pool, scratch_pool));
      err = svn_io_file_info_get(&finfo, APR_FINFO_PROT, fd, scratch_pool);
      err = svn_error_compose_create(err, svn_io_file_close(fd, scratch_pool));
      err = svn_error_compose_create(err, svn_io_remove_file2(fname, TRUE,
                                                              scratch_pool));
      SVN_ERR(err);
      *perms = finfo.protection;
      default_perms = finfo.protection;
    }
  else
    *perms = default_perms;

  return SVN_NO_ERROR;
}

/* OR together permission bits of the file FD and the default permissions
   of a file as determined by get_default_file_perms().  DIRECTORY is used
   to create temporary files, DIRECTORY can be NULL.  Do temporary
   allocations in SCRATCH_POOL. */
static svn_error_t *
merge_default_file_perms(apr_file_t *fd,
                         apr_fileperms_t *perms,
                         const char *directory,
                         apr_pool_t *scratch_pool)
{
  apr_finfo_t finfo;
  apr_fileperms_t default_perms;

  SVN_ERR(get_default_file_perms(&default_perms, directory, scratch_pool));
  SVN_ERR(svn_io_file_info_get(&finfo, APR_FINFO_PROT, fd, scratch_pool));

  /* Glom the perms together. */
  *perms = default_perms | finfo.protection;
  return SVN_NO_ERROR;
}

/* This is a helper function for the svn_io_set_file_read* functions
   that attempts to honor the users umask when dealing with
   permission changes.  It is a no-op when invoked on a symlink. */
static svn_error_t *
io_set_file_perms(const char *path,
                  svn_boolean_t change_readwrite,
                  svn_boolean_t enable_write,
                  svn_boolean_t change_executable,
                  svn_boolean_t executable,
                  svn_boolean_t ignore_enoent,
                  apr_pool_t *pool)
{
  apr_status_t status;
  const char *path_apr;
  apr_finfo_t finfo;
  apr_fileperms_t perms_to_set;

  SVN_ERR(cstring_from_utf8(&path_apr, path, pool));

  /* Try to change only a minimal amount of the perms first
     by getting the current perms and adding bits
     only on where read perms are granted.  If this fails
     fall through to just setting file attributes. */
  status = apr_stat(&finfo, path_apr, APR_FINFO_PROT | APR_FINFO_LINK, pool);
  if (status)
    {
      if (ignore_enoent && (APR_STATUS_IS_ENOENT(status)
                            || SVN__APR_STATUS_IS_ENOTDIR(status)))
        return SVN_NO_ERROR;
      else if (status != APR_ENOTIMPL)
        return svn_error_wrap_apr(status,
                                  _("Can't change perms of file '%s'"),
                                  svn_dirent_local_style(path, pool));
      return SVN_NO_ERROR;
    }

  if (finfo.filetype == APR_LNK)
    return SVN_NO_ERROR;

  perms_to_set = finfo.protection;
  if (change_readwrite)
    {
      if (enable_write) /* Make read-write. */
        {
          /* Tweak the owner bits only. The group/other bits aren't safe to
           * touch because we may end up setting them in undesired ways. */
          perms_to_set |= (APR_UREAD|APR_UWRITE);
        }
      else
        {
          if (finfo.protection & APR_UREAD)
            perms_to_set &= ~APR_UWRITE;
          if (finfo.protection & APR_GREAD)
            perms_to_set &= ~APR_GWRITE;
          if (finfo.protection & APR_WREAD)
            perms_to_set &= ~APR_WWRITE;
        }
    }

  if (change_executable)
    {
      if (executable)
        {
          if (finfo.protection & APR_UREAD)
            perms_to_set |= APR_UEXECUTE;
          if (finfo.protection & APR_GREAD)
            perms_to_set |= APR_GEXECUTE;
          if (finfo.protection & APR_WREAD)
            perms_to_set |= APR_WEXECUTE;
        }
      else
        {
          if (finfo.protection & APR_UREAD)
            perms_to_set &= ~APR_UEXECUTE;
          if (finfo.protection & APR_GREAD)
            perms_to_set &= ~APR_GEXECUTE;
          if (finfo.protection & APR_WREAD)
            perms_to_set &= ~APR_WEXECUTE;
        }
    }

  /* If we aren't changing anything then just return, this saves
     some system calls and helps with shared working copies */
  if (perms_to_set == finfo.protection)
    return SVN_NO_ERROR;

  status = apr_file_perms_set(path_apr, perms_to_set);
  if (!status)
    return SVN_NO_ERROR;

  if (APR_STATUS_IS_EPERM(status))
    {
      /* We don't have permissions to change the
         permissions!  Try a move, copy, and delete
         workaround to see if we can get the file owned by
         us.  If these succeed, try the permissions set
         again.

         Note that we only attempt this in the
         stat-available path.  This assumes that the
         move-copy workaround will only be helpful on
         platforms that implement apr_stat. */
      SVN_ERR(reown_file(path, pool));
      status = apr_file_perms_set(path_apr, perms_to_set);
    }

  if (!status)
    return SVN_NO_ERROR;

  if (ignore_enoent && APR_STATUS_IS_ENOENT(status))
    return SVN_NO_ERROR;
  else if (status == APR_ENOTIMPL)
    {
      /* At least try to set the attributes. */
      apr_fileattrs_t attrs = 0;
      apr_fileattrs_t attrs_values = 0;

      if (change_readwrite)
        {
          attrs = APR_FILE_ATTR_READONLY;
          if (!enable_write)
            attrs_values = APR_FILE_ATTR_READONLY;
        }
      if (change_executable)
        {
          attrs = APR_FILE_ATTR_EXECUTABLE;
          if (executable)
            attrs_values = APR_FILE_ATTR_EXECUTABLE;
        }
      status = apr_file_attrs_set(path_apr, attrs, attrs_values, pool);
    }

  return svn_error_wrap_apr(status,
                            _("Can't change perms of file '%s'"),
                            svn_dirent_local_style(path, pool));
}
#endif /* !WIN32 && !__OS2__ */

#ifdef WIN32
/* This is semantically the same as the APR utf8_to_unicode_path
   function, but reimplemented here because APR does not export it. */
svn_error_t*
svn_io__utf8_to_unicode_longpath(const WCHAR **result,
                                 const char *source,
                                 apr_pool_t *result_pool)
{
    /* This is correct, we don't twist the filename if it will
     * definitely be shorter than 248 characters.  It merits some
     * performance testing to see if this has any effect, but there
     * seem to be applications that get confused by the resulting
     * Unicode \\?\ style file names, especially if they use argv[0]
     * or call the Win32 API functions such as GetModuleName, etc.
     * Not every application is prepared to handle such names.
     *
     * Note also this is shorter than MAX_PATH, as directory paths
     * are actually limited to 248 characters.
     *
     * Note that a utf-8 name can never result in more wide chars
     * than the original number of utf-8 narrow chars.
     */
    const WCHAR *prefix = NULL;
    const size_t srclen = strlen(source);
    WCHAR *buffer;

    if (srclen > 248)
    {
        if (svn_ctype_isalpha(source[0]) && source[1] == ':'
            && (source[2] == '/' || source[2] == '\\'))
        {
            /* This is an ordinary absolute path. */
            prefix = L"\\\\?\\";
        }
        else if ((source[0] == '/' || source[0] == '\\')
                 && (source[1] == '/' || source[1] == '\\')
                 && source[2] != '?')
        {
            /* This is a UNC path */
            source += 2;        /* Skip the leading slashes */
            prefix = L"\\\\?\\UNC\\";
        }
    }

    SVN_ERR(svn_utf__win32_utf8_to_utf16(&(const WCHAR*)buffer, source,
                                         prefix, result_pool));

    /* Convert slashes to backslashes because the \\?\ path format
       does not allow backslashes as path separators. */
    *result = buffer;
    for (; *buffer; ++buffer)
    {
        if (*buffer == '/')
            *buffer = '\\';
    }
    return SVN_NO_ERROR;
}

/* This is semantically the same as the APR unicode_to_utf8_path
   function, but reimplemented here because APR does not export it. */
static svn_error_t *
io_unicode_to_utf8_path(const char **result,
                        const WCHAR *source,
                        apr_pool_t *result_pool)
{
    const char *utf8_buffer;
    char *buffer;

    SVN_ERR(svn_utf__win32_utf16_to_utf8(&utf8_buffer, source,
                                         NULL, result_pool));
    if (!*utf8_buffer)
      {
        *result = utf8_buffer;
        return SVN_NO_ERROR;
      }

    /* We know that the non-empty buffer returned from the UTF-16 to
       UTF-8 conversion function is in fact writable. */
    buffer = (char*)utf8_buffer;

    /* Skip the leading 4 characters if the path begins \\?\, or substitute
     * // for the \\?\UNC\ path prefix, allocating the maximum string
     * length based on the remaining string, plus the trailing null.
     * then transform \\'s back into /'s since the \\?\ form never
     * allows '/' path separators, and APR always uses '/'s.
     */
    if (0 == strncmp(buffer, "\\\\?\\", 4))
    {
        buffer += 4;
        if (0 == strncmp(buffer, "UNC\\", 4))
        {
            buffer += 2;
            *buffer = '/';
        }
    }

    *result = buffer;
    for (; *buffer; ++buffer)
    {
        if (*buffer == '\\')
            *buffer = '/';
    }
    return SVN_NO_ERROR;
}

static svn_error_t *
io_win_file_attrs_set(const char *fname,
                      DWORD attributes,
                      DWORD attr_mask,
                      apr_pool_t *pool)
{
    /* this is an implementation of apr_file_attrs_set() but one
       that uses the proper Windows attributes instead of the apr
       attributes. This way, we can apply any Windows file and
       folder attributes even if apr doesn't implement them */
    DWORD flags;
    const WCHAR *wfname;

    SVN_ERR(svn_io__utf8_to_unicode_longpath(&wfname, fname, pool));

    flags = GetFileAttributesW(wfname);
    if (flags == 0xFFFFFFFF)
        return svn_error_wrap_apr(apr_get_os_error(),
                                  _("Can't get attributes of file '%s'"),
                                  svn_dirent_local_style(fname, pool));

    flags &= ~attr_mask;
    flags |= (attributes & attr_mask);

    if (!SetFileAttributesW(wfname, flags))
        return svn_error_wrap_apr(apr_get_os_error(),
                                  _("Can't set attributes of file '%s'"),
                                  svn_dirent_local_style(fname, pool));

    return SVN_NO_ERROR;
}

static svn_error_t *win_init_dynamic_imports(void *baton, apr_pool_t *pool)
{
  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");

  if (kernel32)
    {
      get_final_path_name_by_handle_proc = (GETFINALPATHNAMEBYHANDLE)
        GetProcAddress(kernel32, "GetFinalPathNameByHandleW");

      set_file_information_by_handle_proc = (SetFileInformationByHandle_t)
        GetProcAddress(kernel32, "SetFileInformationByHandle");
    }

  return SVN_NO_ERROR;
}

static svn_error_t * io_win_read_link(svn_string_t **dest,
                                      const char *path,
                                      apr_pool_t *pool)
{
    SVN_ERR(svn_atomic__init_once(&win_dynamic_imports_state,
                                  win_init_dynamic_imports, NULL, pool));

    if (get_final_path_name_by_handle_proc)
      {
        DWORD rv;
        apr_status_t status;
        apr_file_t *file;
        apr_os_file_t filehand;
        WCHAR wdest[APR_PATH_MAX];
        const char *data;

        /* reserve one char for terminating zero. */
        DWORD wdest_len = sizeof(wdest)/sizeof(wdest[0]) - 1;

        status = apr_file_open(&file, path, APR_OPENINFO, APR_OS_DEFAULT, pool);

        if (status)
          return svn_error_wrap_apr(status,
                                    _("Can't read contents of link"));

        apr_os_file_get(&filehand, file);

        rv = get_final_path_name_by_handle_proc(
               filehand, wdest, wdest_len,
               FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);

        /* Save error code. */
        status = apr_get_os_error();

        /* Close file/directory handle in any case. */
        apr_file_close(file);

        /* GetFinaPathNameByHandleW returns number of characters copied to
         * output buffer. Returns zero on error. Returns required buffer size
         * if supplied buffer is not enough. */
        if (rv > wdest_len || rv == 0)
          {
            return svn_error_wrap_apr(status,
                                      _("Can't read contents of link"));
          }

        /* GetFinaPathNameByHandleW doesn't add terminating NUL. */
        wdest[rv] = 0;
        SVN_ERR(io_unicode_to_utf8_path(&data, wdest, pool));

        /* The result is already in the correct pool, so avoid copying
           it to create the string. */
        *dest = svn_string_create_empty(pool);
        if (*data)
          {
            (*dest)->data = data;
            (*dest)->len = strlen(data);
          }

        return SVN_NO_ERROR;
      }
    else
      {
        return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                _("Symbolic links are not supported on this "
                                "platform"));
      }
}

/* Wrapper around Windows API function SetFileInformationByHandle() that
 * returns APR status instead of boolean flag. */
static apr_status_t
win32_set_file_information_by_handle(HANDLE hFile,
                                     int FileInformationClass,
                                     LPVOID lpFileInformation,
                                     DWORD dwBufferSize)
{
  svn_error_clear(svn_atomic__init_once(&win_dynamic_imports_state,
                                        win_init_dynamic_imports,
                                        NULL, NULL));

  if (!set_file_information_by_handle_proc)
    {
      return SVN_ERR_UNSUPPORTED_FEATURE;
    }

  if (!set_file_information_by_handle_proc(hFile, FileInformationClass,
                                           lpFileInformation,
                                           dwBufferSize))
    {
      return apr_get_os_error();
    }

  return APR_SUCCESS;
}

svn_error_t *
svn_io__win_delete_file_on_close(apr_file_t *file,
                                 const char *path,
                                 apr_pool_t *pool)
{
  FILE_DISPOSITION_INFO disposition_info;
  HANDLE hFile;
  apr_status_t status;

  apr_os_file_get(&hFile, file);

  disposition_info.DeleteFile = TRUE;

  status = win32_set_file_information_by_handle(hFile, FileDispositionInfo,
                                                &disposition_info,
                                                sizeof(disposition_info));

  if (status)
    {
      return svn_error_wrap_apr(status, _("Can't remove file '%s'"),
                                svn_dirent_local_style(path, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io__win_rename_open_file(apr_file_t *file,
                             const char *from_path,
                             const char *to_path,
                             apr_pool_t *pool)
{
  WCHAR *w_final_abspath;
  size_t path_len;
  size_t rename_size;
  FILE_RENAME_INFO *rename_info;
  HANDLE hFile;
  apr_status_t status;

  apr_os_file_get(&hFile, file);

  SVN_ERR(svn_io__utf8_to_unicode_longpath(
            &w_final_abspath, svn_dirent_local_style(to_path,pool),
            pool));

  path_len = wcslen(w_final_abspath);
  rename_size = sizeof(*rename_info) + sizeof(WCHAR) * path_len;

  /* The rename info struct doesn't need hacks for long paths,
     so no ugly escaping calls here */
  rename_info = apr_pcalloc(pool, rename_size);
  rename_info->ReplaceIfExists = TRUE;
  rename_info->FileNameLength = path_len;
  memcpy(rename_info->FileName, w_final_abspath, path_len * sizeof(WCHAR));

  status = win32_set_file_information_by_handle(hFile, FileRenameInfo,
                                                rename_info,
                                                rename_size);

  if (APR_STATUS_IS_EACCES(status) || APR_STATUS_IS_EEXIST(status))
    {
      /* Set the destination file writable because Windows will not allow
         us to rename when final_abspath is read-only. */
      SVN_ERR(svn_io_set_file_read_write(to_path, TRUE, pool));

      status = win32_set_file_information_by_handle(hFile,
                                                    FileRenameInfo,
                                                    rename_info,
                                                    rename_size);
   }

  /* Windows returns Vista+ client accessing network share stored on Windows
     Server 2003 returns ERROR_ACCESS_DENIED. The same happens when Vista+
     client access Windows Server 2008 with disabled SMBv2 protocol.

     So return SVN_ERR_UNSUPPORTED_FEATURE in this case like we do when
     SetFileInformationByHandle() is not available and let caller to
     handle it.

     See "Access denied error on checkout-commit after updating to 1.9.X"
     discussion on dev@s.a.o:
     http://svn.haxx.se/dev/archive-2015-09/0054.shtml */
  if (status == APR_FROM_OS_ERROR(ERROR_ACCESS_DENIED))
    {
      status = SVN_ERR_UNSUPPORTED_FEATURE;
    }

  if (status)
    {
      return svn_error_wrap_apr(status, _("Can't move '%s' to '%s'"),
                                svn_dirent_local_style(from_path, pool),
                                svn_dirent_local_style(to_path, pool));
    }

  return SVN_NO_ERROR;
}

#endif /* WIN32 */

svn_error_t *
svn_io_set_file_read_write_carefully(const char *path,
                                     svn_boolean_t enable_write,
                                     svn_boolean_t ignore_enoent,
                                     apr_pool_t *pool)
{
  if (enable_write)
    return svn_io_set_file_read_write(path, ignore_enoent, pool);
  return svn_io_set_file_read_only(path, ignore_enoent, pool);
}

svn_error_t *
svn_io_set_file_read_only(const char *path,
                          svn_boolean_t ignore_enoent,
                          apr_pool_t *pool)
{
  /* On Windows and OS/2, just set the file attributes -- on unix call
     our internal function which attempts to honor the umask. */
#if !defined(WIN32) && !defined(__OS2__)
  return io_set_file_perms(path, TRUE, FALSE, FALSE, FALSE,
                           ignore_enoent, pool);
#else
  apr_status_t status;
  const char *path_apr;

  SVN_ERR(cstring_from_utf8(&path_apr, path, pool));

  status = apr_file_attrs_set(path_apr,
                              APR_FILE_ATTR_READONLY,
                              APR_FILE_ATTR_READONLY,
                              pool);

  if (status && status != APR_ENOTIMPL)
    if (!(ignore_enoent && (APR_STATUS_IS_ENOENT(status)
                            || SVN__APR_STATUS_IS_ENOTDIR(status))))
      return svn_error_wrap_apr(status,
                                _("Can't set file '%s' read-only"),
                                svn_dirent_local_style(path, pool));

  return SVN_NO_ERROR;
#endif
}


svn_error_t *
svn_io_set_file_read_write(const char *path,
                           svn_boolean_t ignore_enoent,
                           apr_pool_t *pool)
{
  /* On Windows and OS/2, just set the file attributes -- on unix call
     our internal function which attempts to honor the umask. */
#if !defined(WIN32) && !defined(__OS2__)
  return io_set_file_perms(path, TRUE, TRUE, FALSE, FALSE,
                           ignore_enoent, pool);
#else
  apr_status_t status;
  const char *path_apr;

  SVN_ERR(cstring_from_utf8(&path_apr, path, pool));

  status = apr_file_attrs_set(path_apr,
                              0,
                              APR_FILE_ATTR_READONLY,
                              pool);

  if (status && status != APR_ENOTIMPL)
    if (!ignore_enoent || !APR_STATUS_IS_ENOENT(status))
      return svn_error_wrap_apr(status,
                                _("Can't set file '%s' read-write"),
                                svn_dirent_local_style(path, pool));

  return SVN_NO_ERROR;
#endif
}

svn_error_t *
svn_io_set_file_executable(const char *path,
                           svn_boolean_t executable,
                           svn_boolean_t ignore_enoent,
                           apr_pool_t *pool)
{
  /* On Windows and OS/2, just exit -- on unix call our internal function
  which attempts to honor the umask. */
#if (!defined(WIN32) && !defined(__OS2__))
  return io_set_file_perms(path, FALSE, FALSE, TRUE, executable,
                           ignore_enoent, pool);
#else
  return SVN_NO_ERROR;
#endif
}


svn_error_t *
svn_io__is_finfo_read_only(svn_boolean_t *read_only,
                           apr_finfo_t *file_info,
                           apr_pool_t *pool)
{
#if defined(APR_HAS_USER) && !defined(WIN32) &&!defined(__OS2__)
  apr_status_t apr_err;
  apr_uid_t uid;
  apr_gid_t gid;

  *read_only = FALSE;

  apr_err = apr_uid_current(&uid, &gid, pool);

  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Error getting UID of process"));

  /* Check write bit for current user. */
  if (apr_uid_compare(uid, file_info->user) == APR_SUCCESS)
    *read_only = !(file_info->protection & APR_UWRITE);

  else if (apr_gid_compare(gid, file_info->group) == APR_SUCCESS)
    *read_only = !(file_info->protection & APR_GWRITE);

  else
    *read_only = !(file_info->protection & APR_WWRITE);

#else  /* WIN32 || __OS2__ || !APR_HAS_USER */
  *read_only = (file_info->protection & APR_FREADONLY);
#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io__is_finfo_executable(svn_boolean_t *executable,
                            apr_finfo_t *file_info,
                            apr_pool_t *pool)
{
#if defined(APR_HAS_USER) && !defined(WIN32) &&!defined(__OS2__)
  apr_status_t apr_err;
  apr_uid_t uid;
  apr_gid_t gid;

  *executable = FALSE;

  apr_err = apr_uid_current(&uid, &gid, pool);

  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Error getting UID of process"));

  /* Check executable bit for current user. */
  if (apr_uid_compare(uid, file_info->user) == APR_SUCCESS)
    *executable = (file_info->protection & APR_UEXECUTE);

  else if (apr_gid_compare(gid, file_info->group) == APR_SUCCESS)
    *executable = (file_info->protection & APR_GEXECUTE);

  else
    *executable = (file_info->protection & APR_WEXECUTE);

#else  /* WIN32 || __OS2__ || !APR_HAS_USER */
  *executable = FALSE;
#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_is_file_executable(svn_boolean_t *executable,
                          const char *path,
                          apr_pool_t *pool)
{
#if defined(APR_HAS_USER) && !defined(WIN32) &&!defined(__OS2__)
  apr_finfo_t file_info;

  SVN_ERR(svn_io_stat(&file_info, path, APR_FINFO_PROT | APR_FINFO_OWNER,
                      pool));
  SVN_ERR(svn_io__is_finfo_executable(executable, &file_info, pool));

#else  /* WIN32 || __OS2__ || !APR_HAS_USER */
  *executable = FALSE;
#endif

  return SVN_NO_ERROR;
}


/*** File locking. ***/
/* Clear all outstanding locks on ARG, an open apr_file_t *. */
static apr_status_t
file_clear_locks(void *arg)
{
  apr_status_t apr_err;
  apr_file_t *f = arg;

  /* Remove locks. */
  apr_err = apr_file_unlock(f);
  if (apr_err)
    return apr_err;

  return 0;
}

svn_error_t *
svn_io_lock_open_file(apr_file_t *lockfile_handle,
                      svn_boolean_t exclusive,
                      svn_boolean_t nonblocking,
                      apr_pool_t *pool)
{
  int locktype = APR_FLOCK_SHARED;
  apr_status_t apr_err;
  const char *fname;

  if (exclusive)
    locktype = APR_FLOCK_EXCLUSIVE;
  if (nonblocking)
    locktype |= APR_FLOCK_NONBLOCK;

  /* We need this only in case of an error but this is cheap to get -
   * so we do it here for clarity. */
  apr_err = apr_file_name_get(&fname, lockfile_handle);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get file name"));

  /* Get lock on the filehandle. */
  apr_err = apr_file_lock(lockfile_handle, locktype);

  /* In deployments with two or more multithreaded servers running on
     the same system serving two or more fsfs repositories it is
     possible for a deadlock to occur when getting a write lock on
     db/txn-current-lock:

     Process 1                         Process 2
     ---------                         ---------
     thread 1: get lock in repos A
                                       thread 1: get lock in repos B
                                       thread 2: block getting lock in repos A
     thread 2: try to get lock in B *** deadlock ***

     Retry for a while for the deadlock to clear. */
  FILE_LOCK_RETRY_LOOP(apr_err, apr_file_lock(lockfile_handle, locktype));

  if (apr_err)
    {
      switch (locktype & APR_FLOCK_TYPEMASK)
        {
        case APR_FLOCK_SHARED:
          return svn_error_wrap_apr(apr_err,
                                    _("Can't get shared lock on file '%s'"),
                                    try_utf8_from_internal_style(fname, pool));
        case APR_FLOCK_EXCLUSIVE:
          return svn_error_wrap_apr(apr_err,
                                    _("Can't get exclusive lock on file '%s'"),
                                    try_utf8_from_internal_style(fname, pool));
        default:
          SVN_ERR_MALFUNCTION();
        }
    }

  /* On Windows, a process may not release file locks before closing the
     handle, and in this case the outstanding locks are unlocked by the OS.
     However, this is not recommended, because the actual unlocking may be
     postponed depending on available system resources.  We explicitly unlock
     the file as a part of the pool cleanup handler. */
  apr_pool_cleanup_register(pool, lockfile_handle,
                            file_clear_locks,
                            apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_unlock_open_file(apr_file_t *lockfile_handle,
                        apr_pool_t *pool)
{
  const char *fname;
  apr_status_t apr_err;

  /* We need this only in case of an error but this is cheap to get -
   * so we do it here for clarity. */
  apr_err = apr_file_name_get(&fname, lockfile_handle);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get file name"));

  /* The actual unlock attempt. */
  apr_err = apr_file_unlock(lockfile_handle);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't unlock file '%s'"),
                              try_utf8_from_internal_style(fname, pool));

  apr_pool_cleanup_kill(pool, lockfile_handle, file_clear_locks);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_file_lock2(const char *lock_file,
                  svn_boolean_t exclusive,
                  svn_boolean_t nonblocking,
                  apr_pool_t *pool)
{
  int locktype = APR_FLOCK_SHARED;
  apr_file_t *lockfile_handle;
  apr_int32_t flags;

  if (exclusive)
    locktype = APR_FLOCK_EXCLUSIVE;

  flags = APR_READ;
  if (locktype == APR_FLOCK_EXCLUSIVE)
    flags |= APR_WRITE;

  /* locktype is never read after this block, so we don't need to bother
     setting it.  If that were to ever change, uncomment the following
     block.
  if (nonblocking)
    locktype |= APR_FLOCK_NONBLOCK;
  */

  SVN_ERR(svn_io_file_open(&lockfile_handle, lock_file, flags,
                           APR_OS_DEFAULT,
                           pool));

  /* Get lock on the filehandle. */
  return svn_io_lock_open_file(lockfile_handle, exclusive, nonblocking, pool);
}

svn_error_t *
svn_io__file_lock_autocreate(const char *lock_file,
                             apr_pool_t *pool)
{
  svn_error_t *err
    = svn_io_file_lock2(lock_file, TRUE, FALSE, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* No lock file?  No big deal; these are just empty files anyway.
         Create it and try again. */
      svn_error_clear(err);

      /* This file creation is racy.
         We don't care as long as file gets created at all. */
      err = svn_io_file_create_empty(lock_file, pool);
      if (err && APR_STATUS_IS_EEXIST(err->apr_err))
        {
          svn_error_clear(err);
          err = NULL;
        }

      /* Finally, lock the file - if it exists */
      if (!err)
        err = svn_io_file_lock2(lock_file, TRUE, FALSE, pool);
    }

  return svn_error_trace(err);
}



/* Data consistency/coherency operations. */

svn_error_t *svn_io_file_flush_to_disk(apr_file_t *file,
                                       apr_pool_t *pool)
{
  apr_os_file_t filehand;
  const char *fname;
  apr_status_t apr_err;

  /* We need this only in case of an error but this is cheap to get -
   * so we do it here for clarity. */
  apr_err = apr_file_name_get(&fname, file);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get file name"));

  /* ### In apr 1.4+ we could delegate most of this function to
         apr_file_sync(). The only major difference is that this doesn't
         contain the retry loop for EINTR on linux. */

  /* First make sure that any user-space buffered data is flushed. */
  SVN_ERR(svn_io_file_flush(file, pool));

  apr_os_file_get(&filehand, file);

  /* Call the operating system specific function to actually force the
     data to disk. */
  {
#ifdef WIN32

    if (! FlushFileBuffers(filehand))
        return svn_error_wrap_apr(apr_get_os_error(),
                                  _("Can't flush file '%s' to disk"),
                                  try_utf8_from_internal_style(fname, pool));

#else
      int rv;

      do {
#ifdef F_FULLFSYNC
        rv = fcntl(filehand, F_FULLFSYNC, 0);
#else
        rv = fsync(filehand);
#endif
      } while (rv == -1 && APR_STATUS_IS_EINTR(apr_get_os_error()));

      /* If the file is in a memory filesystem, fsync() may return
         EINVAL.  Presumably the user knows the risks, and we can just
         ignore the error. */
      if (rv == -1 && APR_STATUS_IS_EINVAL(apr_get_os_error()))
        return SVN_NO_ERROR;

      if (rv == -1)
        return svn_error_wrap_apr(apr_get_os_error(),
                                  _("Can't flush file '%s' to disk"),
                                  try_utf8_from_internal_style(fname, pool));

#endif
  }
  return SVN_NO_ERROR;
}



/* TODO write test for these two functions, then refactor. */

/* Set RESULT to an svn_stringbuf_t containing the contents of FILE.
   FILENAME is the FILE's on-disk APR-safe name, or NULL if that name
   isn't known.  If CHECK_SIZE is TRUE, the function will attempt to
   first stat() the file to determine it's size before sucking its
   contents into the stringbuf.  (Doing so can prevent unnecessary
   memory usage, an unwanted side effect of the stringbuf growth and
   reallocation mechanism.)  */
static svn_error_t *
stringbuf_from_aprfile(svn_stringbuf_t **result,
                       const char *filename,
                       apr_file_t *file,
                       svn_boolean_t check_size,
                       apr_pool_t *pool)
{
  apr_size_t len;
  svn_error_t *err;
  svn_stringbuf_t *res = NULL;
  apr_size_t res_initial_len = SVN__STREAM_CHUNK_SIZE;
  char *buf;

  /* If our caller wants us to check the size of the file for
     efficient memory handling, we'll try to do so. */
  if (check_size)
    {
      apr_finfo_t finfo = { 0 };

      /* In some cases we get size 0 and no error for non files,
          so we also check for the name. (= cached in apr_file_t) */
      if (! apr_file_info_get(&finfo, APR_FINFO_SIZE, file) && finfo.fname)
        {
          /* we've got the file length. Now, read it in one go. */
          svn_boolean_t eof;
          res_initial_len = (apr_size_t)finfo.size;
          res = svn_stringbuf_create_ensure(res_initial_len, pool);
          SVN_ERR(svn_io_file_read_full2(file, res->data,
                                         res_initial_len, &res->len,
                                         &eof, pool));
          res->data[res->len] = 0;

          *result = res;
          return SVN_NO_ERROR;
        }
    }

  /* XXX: We should check the incoming data for being of type binary. */
  buf = apr_palloc(pool, SVN__STREAM_CHUNK_SIZE);
  res = svn_stringbuf_create_ensure(res_initial_len, pool);

  /* apr_file_read will not return data and eof in the same call. So this loop
   * is safe from missing read data.  */
  len = SVN__STREAM_CHUNK_SIZE;
  err = svn_io_file_read(file, buf, &len, pool);
  while (! err)
    {
      svn_stringbuf_appendbytes(res, buf, len);
      len = SVN__STREAM_CHUNK_SIZE;
      err = svn_io_file_read(file, buf, &len, pool);
    }

  /* Having read all the data we *expect* EOF */
  if (err && !APR_STATUS_IS_EOF(err->apr_err))
    return svn_error_trace(err);
  svn_error_clear(err);

  *result = res;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_stringbuf_from_file2(svn_stringbuf_t **result,
                         const char *filename,
                         apr_pool_t *pool)
{
  apr_file_t *f;

  if (filename[0] == '-' && filename[1] == '\0')
    {
      apr_status_t apr_err;
      if ((apr_err = apr_file_open_stdin(&f, pool)))
        return svn_error_wrap_apr(apr_err, _("Can't open stdin"));
      SVN_ERR(stringbuf_from_aprfile(result, NULL, f, FALSE, pool));
    }
  else
    {
      SVN_ERR(svn_io_file_open(&f, filename, APR_READ, APR_OS_DEFAULT, pool));
      SVN_ERR(stringbuf_from_aprfile(result, filename, f, TRUE, pool));
    }
  return svn_io_file_close(f, pool);
}


svn_error_t *
svn_stringbuf_from_file(svn_stringbuf_t **result,
                        const char *filename,
                        apr_pool_t *pool)
{
  if (filename[0] == '-' && filename[1] == '\0')
    return svn_error_create
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Reading from stdin is disallowed"));
  return svn_stringbuf_from_file2(result, filename, pool);
}

svn_error_t *
svn_stringbuf_from_aprfile(svn_stringbuf_t **result,
                           apr_file_t *file,
                           apr_pool_t *pool)
{
  return stringbuf_from_aprfile(result, NULL, file, TRUE, pool);
}



/* Deletion. */

svn_error_t *
svn_io_remove_file2(const char *path,
                    svn_boolean_t ignore_enoent,
                    apr_pool_t *scratch_pool)
{
  apr_status_t apr_err;
  const char *path_apr;

  SVN_ERR(cstring_from_utf8(&path_apr, path, scratch_pool));

  apr_err = apr_file_remove(path_apr, scratch_pool);

#ifdef WIN32
  /* If the target is read only NTFS reports EACCESS and FAT/FAT32
     reports EEXIST */
  if (APR_STATUS_IS_EACCES(apr_err) || APR_STATUS_IS_EEXIST(apr_err))
    {
      /* Set the destination file writable because Windows will not
         allow us to delete when path is read-only */
      SVN_ERR(svn_io_set_file_read_write(path, ignore_enoent, scratch_pool));
      apr_err = apr_file_remove(path_apr, scratch_pool);
    }

  /* Check to make sure we aren't trying to delete a directory */
  if (apr_err == APR_FROM_OS_ERROR(ERROR_ACCESS_DENIED)
      || apr_err == APR_FROM_OS_ERROR(ERROR_SHARING_VIOLATION))
    {
      apr_finfo_t finfo;

      if (!apr_stat(&finfo, path_apr, APR_FINFO_TYPE, scratch_pool)
          && finfo.filetype == APR_REG)
        {
          WIN32_RETRY_LOOP(apr_err, apr_file_remove(path_apr, scratch_pool));
        }
    }

  /* Just return the delete error */
#endif

  if (!apr_err)
    {
      return SVN_NO_ERROR;
    }
  else if (ignore_enoent && (APR_STATUS_IS_ENOENT(apr_err)
                             || SVN__APR_STATUS_IS_ENOTDIR(apr_err)))
    {
      return SVN_NO_ERROR;
    }
  else
    {
      return svn_error_wrap_apr(apr_err, _("Can't remove file '%s'"),
                                svn_dirent_local_style(path, scratch_pool));
    }
}


svn_error_t *
svn_io_remove_dir(const char *path, apr_pool_t *pool)
{
  return svn_io_remove_dir2(path, FALSE, NULL, NULL, pool);
}

/*
 Mac OS X has a bug where if you're reading the contents of a
 directory via readdir in a loop, and you remove one of the entries in
 the directory and the directory has 338 or more files in it you will
 skip over some of the entries in the directory.  Needless to say,
 this causes problems if you are using this kind of loop inside a
 function that is recursively deleting a directory, because when you
 get around to removing the directory it will still have something in
 it. A similar problem has been observed in other BSDs. This bug has
 since been fixed. See http://www.vnode.ch/fixing_seekdir for details.

 The workaround is to delete the files only _after_ the initial
 directory scan.  A previous workaround involving rewinddir is
 problematic on Win32 and some NFS clients, notably NetBSD.

 See http://subversion.tigris.org/issues/show_bug.cgi?id=1896 and
 http://subversion.tigris.org/issues/show_bug.cgi?id=3501.
*/

/* Neither windows nor unix allows us to delete a non-empty
   directory.

   This is a function to perform the equivalent of 'rm -rf'. */
svn_error_t *
svn_io_remove_dir2(const char *path, svn_boolean_t ignore_enoent,
                   svn_cancel_func_t cancel_func, void *cancel_baton,
                   apr_pool_t *pool)
{
  svn_error_t *err;
  apr_pool_t *subpool;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  /* Check for pending cancellation request.
     If we need to bail out, do so early. */

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  subpool = svn_pool_create(pool);

  err = svn_io_get_dirents3(&dirents, path, TRUE, subpool, subpool);
  if (err)
    {
      /* if the directory doesn't exist, our mission is accomplished */
      if (ignore_enoent && (APR_STATUS_IS_ENOENT(err->apr_err)
                            || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      return svn_error_trace(err);
    }

  for (hi = apr_hash_first(subpool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      const svn_io_dirent2_t *dirent = apr_hash_this_val(hi);
      const char *fullpath;

      fullpath = svn_dirent_join(path, name, subpool);
      if (dirent->kind == svn_node_dir)
        {
          /* Don't check for cancellation, the callee will immediately do so */
          SVN_ERR(svn_io_remove_dir2(fullpath, FALSE, cancel_func,
                                     cancel_baton, subpool));
        }
      else
        {
          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          err = svn_io_remove_file2(fullpath, FALSE, subpool);
          if (err)
            return svn_error_createf
              (err->apr_err, err, _("Can't remove '%s'"),
               svn_dirent_local_style(fullpath, subpool));
        }
    }

  svn_pool_destroy(subpool);

  return svn_io_dir_remove_nonrecursive(path, pool);
}

svn_error_t *
svn_io_get_dir_filenames(apr_hash_t **dirents,
                         const char *path,
                         apr_pool_t *pool)
{
  return svn_error_trace(svn_io_get_dirents3(dirents, path, TRUE,
                                             pool, pool));
}

svn_io_dirent2_t *
svn_io_dirent2_create(apr_pool_t *result_pool)
{
  svn_io_dirent2_t *dirent = apr_pcalloc(result_pool, sizeof(*dirent));

  /*dirent->kind = svn_node_none;
  dirent->special = FALSE;*/
  dirent->filesize = SVN_INVALID_FILESIZE;
  /*dirent->mtime = 0;*/

  return dirent;
}

svn_io_dirent2_t *
svn_io_dirent2_dup(const svn_io_dirent2_t *item,
                   apr_pool_t *result_pool)
{
  return apr_pmemdup(result_pool,
                     item,
                     sizeof(*item));
}

svn_error_t *
svn_io_get_dirents3(apr_hash_t **dirents,
                    const char *path,
                    svn_boolean_t only_check_type,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_status_t status;
  apr_dir_t *this_dir;
  apr_finfo_t this_entry;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;

  if (!only_check_type)
    flags |= APR_FINFO_SIZE | APR_FINFO_MTIME;

  *dirents = apr_hash_make(result_pool);

  SVN_ERR(svn_io_dir_open(&this_dir, path, scratch_pool));

  for (status = apr_dir_read(&this_entry, flags, this_dir);
       status == APR_SUCCESS;
       status = apr_dir_read(&this_entry, flags, this_dir))
    {
      if ((this_entry.name[0] == '.')
          && ((this_entry.name[1] == '\0')
              || ((this_entry.name[1] == '.')
                  && (this_entry.name[2] == '\0'))))
        {
          continue;
        }
      else
        {
          const char *name;
          svn_io_dirent2_t *dirent = svn_io_dirent2_create(result_pool);

          SVN_ERR(entry_name_to_utf8(&name, this_entry.name, path, result_pool));

          map_apr_finfo_to_node_kind(&(dirent->kind),
                                     &(dirent->special),
                                     &this_entry);

          if (!only_check_type)
            {
              dirent->filesize = this_entry.size;
              dirent->mtime = this_entry.mtime;
            }

          svn_hash_sets(*dirents, name, dirent);
        }
    }

  if (! (APR_STATUS_IS_ENOENT(status)))
    return svn_error_wrap_apr(status, _("Can't read directory '%s'"),
                              svn_dirent_local_style(path, scratch_pool));

  status = apr_dir_close(this_dir);
  if (status)
    return svn_error_wrap_apr(status, _("Error closing directory '%s'"),
                              svn_dirent_local_style(path, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_stat_dirent2(const svn_io_dirent2_t **dirent_p,
                    const char *path,
                    svn_boolean_t verify_truename,
                    svn_boolean_t ignore_enoent,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_finfo_t finfo;
  svn_io_dirent2_t *dirent;
  svn_error_t *err;
  apr_int32_t wanted = APR_FINFO_TYPE | APR_FINFO_LINK
                       | APR_FINFO_SIZE | APR_FINFO_MTIME;

#if defined(WIN32) || defined(__OS2__)
  if (verify_truename)
    wanted |= APR_FINFO_NAME;
#endif

  err = svn_io_stat(&finfo, path, wanted, scratch_pool);

  if (err && ignore_enoent &&
      (APR_STATUS_IS_ENOENT(err->apr_err)
       || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
    {
      svn_error_clear(err);
      dirent = svn_io_dirent2_create(result_pool);
      SVN_ERR_ASSERT(dirent->kind == svn_node_none);

      *dirent_p = dirent;
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

#if defined(WIN32) || defined(__OS2__) || defined(DARWIN)
  if (verify_truename)
    {
      const char *requested_name = svn_dirent_basename(path, NULL);

      if (requested_name[0] == '\0')
        {
          /* No parent directory. No need to stat/verify */
        }
#if defined(WIN32) || defined(__OS2__)
      else if (finfo.name)
        {
          const char *name_on_disk;
          SVN_ERR(entry_name_to_utf8(&name_on_disk, finfo.name, path,
                                     scratch_pool));

          if (strcmp(name_on_disk, requested_name) /* != 0 */)
            {
              if (ignore_enoent)
                {
                  *dirent_p = svn_io_dirent2_create(result_pool);
                  return SVN_NO_ERROR;
                }
              else
                return svn_error_createf(APR_ENOENT, NULL,
                          _("Path '%s' not found, case obstructed by '%s'"),
                          svn_dirent_local_style(path, scratch_pool),
                          name_on_disk);
            }
        }
#elif defined(DARWIN)
      /* Currently apr doesn't set finfo.name on DARWIN, returning
                   APR_INCOMPLETE.
         ### Can we optimize this in another way? */
      else
        {
          apr_hash_t *dirents;

          err = svn_io_get_dirents3(&dirents,
                                    svn_dirent_dirname(path, scratch_pool),
                                    TRUE /* only_check_type */,
                                    scratch_pool, scratch_pool);

          if (err && ignore_enoent
              && (APR_STATUS_IS_ENOENT(err->apr_err)
                  || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
            {
              svn_error_clear(err);

              *dirent_p = svn_io_dirent2_create(result_pool);
              return SVN_NO_ERROR;
            }
          else
            SVN_ERR(err);

          if (! svn_hash_gets(dirents, requested_name))
            {
              if (ignore_enoent)
                {
                  *dirent_p = svn_io_dirent2_create(result_pool);
                  return SVN_NO_ERROR;
                }
              else
                return svn_error_createf(APR_ENOENT, NULL,
                          _("Path '%s' not found"),
                          svn_dirent_local_style(path, scratch_pool));
            }
        }
#endif
    }
#endif

  dirent = svn_io_dirent2_create(result_pool);
  map_apr_finfo_to_node_kind(&(dirent->kind), &(dirent->special), &finfo);

  dirent->filesize = finfo.size;
  dirent->mtime = finfo.mtime;

  *dirent_p = dirent;

  return SVN_NO_ERROR;
}

/* Pool userdata key for the error file passed to svn_io_start_cmd(). */
#define ERRFILE_KEY "svn-io-start-cmd-errfile"

/* Handle an error from the child process (before command execution) by
   printing DESC and the error string corresponding to STATUS to stderr. */
static void
handle_child_process_error(apr_pool_t *pool, apr_status_t status,
                           const char *desc)
{
  char errbuf[256];
  apr_file_t *errfile;
  void *p;

  /* We can't do anything if we get an error here, so just return. */
  if (apr_pool_userdata_get(&p, ERRFILE_KEY, pool))
    return;
  errfile = p;

  if (errfile)
    /* What we get from APR is in native encoding. */
    apr_file_printf(errfile, "%s: %s",
                    desc, apr_strerror(status, errbuf,
                                       sizeof(errbuf)));
}


svn_error_t *
svn_io_start_cmd3(apr_proc_t *cmd_proc,
                  const char *path,
                  const char *cmd,
                  const char *const *args,
                  const char *const *env,
                  svn_boolean_t inherit,
                  svn_boolean_t infile_pipe,
                  apr_file_t *infile,
                  svn_boolean_t outfile_pipe,
                  apr_file_t *outfile,
                  svn_boolean_t errfile_pipe,
                  apr_file_t *errfile,
                  apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_procattr_t *cmdproc_attr;
  int num_args;
  const char **args_native;
  const char *cmd_apr;

  SVN_ERR_ASSERT(!((infile != NULL) && infile_pipe));
  SVN_ERR_ASSERT(!((outfile != NULL) && outfile_pipe));
  SVN_ERR_ASSERT(!((errfile != NULL) && errfile_pipe));

  /* Create the process attributes. */
  apr_err = apr_procattr_create(&cmdproc_attr, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              _("Can't create process '%s' attributes"),
                              cmd);

  /* Make sure we invoke cmd directly, not through a shell. */
  apr_err = apr_procattr_cmdtype_set(cmdproc_attr,
                                     inherit ? APR_PROGRAM_PATH : APR_PROGRAM);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't set process '%s' cmdtype"),
                              cmd);

  /* Set the process's working directory. */
  if (path)
    {
      const char *path_apr;

      /* APR doesn't like our canonical path format for current directory */
      if (path[0] == '\0')
        path = ".";

      SVN_ERR(cstring_from_utf8(&path_apr, path, pool));
      apr_err = apr_procattr_dir_set(cmdproc_attr, path_apr);
      if (apr_err)
        return svn_error_wrap_apr(apr_err,
                                  _("Can't set process '%s' directory"),
                                  cmd);
    }

  /* Use requested inputs and outputs.

     ### Unfortunately each of these apr functions creates a pipe and then
     overwrites the pipe file descriptor with the descriptor we pass
     in. The pipes can then never be closed. This is an APR bug. */
  if (infile)
    {
      apr_err = apr_procattr_child_in_set(cmdproc_attr, infile, NULL);
      if (apr_err)
        return svn_error_wrap_apr(apr_err,
                                  _("Can't set process '%s' child input"),
                                  cmd);
    }
  if (outfile)
    {
      apr_err = apr_procattr_child_out_set(cmdproc_attr, outfile, NULL);
      if (apr_err)
        return svn_error_wrap_apr(apr_err,
                                  _("Can't set process '%s' child outfile"),
                                  cmd);
    }
  if (errfile)
    {
      apr_err = apr_procattr_child_err_set(cmdproc_attr, errfile, NULL);
      if (apr_err)
        return svn_error_wrap_apr(apr_err,
                                  _("Can't set process '%s' child errfile"),
                                  cmd);
    }

  /* Forward request for pipes to APR. */
  if (infile_pipe || outfile_pipe || errfile_pipe)
    {
      apr_err = apr_procattr_io_set(cmdproc_attr,
                                    infile_pipe ? APR_FULL_BLOCK : APR_NO_PIPE,
                                    outfile_pipe ? APR_FULL_BLOCK : APR_NO_PIPE,
                                    errfile_pipe ? APR_FULL_BLOCK : APR_NO_PIPE);

      if (apr_err)
        return svn_error_wrap_apr(apr_err,
                                  _("Can't set process '%s' stdio pipes"),
                                  cmd);
    }

  /* Have the child print any problems executing its program to errfile. */
  apr_err = apr_pool_userdata_set(errfile, ERRFILE_KEY, NULL, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              _("Can't set process '%s' child errfile for "
                                "error handler"),
                              cmd);
  apr_err = apr_procattr_child_errfn_set(cmdproc_attr,
                                         handle_child_process_error);
  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              _("Can't set process '%s' error handler"),
                              cmd);

  /* Convert cmd and args from UTF-8 */
  SVN_ERR(cstring_from_utf8(&cmd_apr, cmd, pool));
  for (num_args = 0; args[num_args]; num_args++)
    ;
  args_native = apr_palloc(pool, (num_args + 1) * sizeof(char *));
  args_native[num_args] = NULL;
  while (num_args--)
    {
      /* ### Well, it turns out that on APR on Windows expects all
             program args to be in UTF-8. Callers of svn_io_run_cmd
             should be aware of that. */
      SVN_ERR(cstring_from_utf8(&args_native[num_args],
                                args[num_args], pool));
    }


  /* Start the cmd command. */
  apr_err = apr_proc_create(cmd_proc, cmd_apr, args_native,
                            inherit ? NULL : env, cmdproc_attr, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't start process '%s'"), cmd);

  return SVN_NO_ERROR;
}

#undef ERRFILE_KEY

svn_error_t *
svn_io_wait_for_cmd(apr_proc_t *cmd_proc,
                    const char *cmd,
                    int *exitcode,
                    apr_exit_why_e *exitwhy,
                    apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_exit_why_e exitwhy_val;
  int exitcode_val;

  /* The Win32 apr_proc_wait doesn't set this... */
  exitwhy_val = APR_PROC_EXIT;

  /* Wait for the cmd command to finish. */
  apr_err = apr_proc_wait(cmd_proc, &exitcode_val, &exitwhy_val, APR_WAIT);
  if (!APR_STATUS_IS_CHILD_DONE(apr_err))
    return svn_error_wrap_apr(apr_err, _("Error waiting for process '%s'"),
                              cmd);

  if (exitwhy)
    *exitwhy = exitwhy_val;
  else if (APR_PROC_CHECK_SIGNALED(exitwhy_val)
           && APR_PROC_CHECK_CORE_DUMP(exitwhy_val))
    return svn_error_createf
      (SVN_ERR_EXTERNAL_PROGRAM, NULL,
       _("Process '%s' failed (signal %d, core dumped)"),
       cmd, exitcode_val);
  else if (APR_PROC_CHECK_SIGNALED(exitwhy_val))
    return svn_error_createf
      (SVN_ERR_EXTERNAL_PROGRAM, NULL,
       _("Process '%s' failed (signal %d)"),
       cmd, exitcode_val);
  else if (! APR_PROC_CHECK_EXIT(exitwhy_val))
    /* Don't really know what happened here. */
    return svn_error_createf
      (SVN_ERR_EXTERNAL_PROGRAM, NULL,
       _("Process '%s' failed (exitwhy %d, exitcode %d)"),
       cmd, exitwhy_val, exitcode_val);

  if (exitcode)
    *exitcode = exitcode_val;
  else if (exitcode_val != 0)
    return svn_error_createf
      (SVN_ERR_EXTERNAL_PROGRAM, NULL,
       _("Process '%s' returned error exitcode %d"), cmd, exitcode_val);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_run_cmd(const char *path,
               const char *cmd,
               const char *const *args,
               int *exitcode,
               apr_exit_why_e *exitwhy,
               svn_boolean_t inherit,
               apr_file_t *infile,
               apr_file_t *outfile,
               apr_file_t *errfile,
               apr_pool_t *pool)
{
  apr_proc_t cmd_proc;

  SVN_ERR(svn_io_start_cmd3(&cmd_proc, path, cmd, args, NULL, inherit,
                            FALSE, infile, FALSE, outfile, FALSE, errfile,
                            pool));

  return svn_io_wait_for_cmd(&cmd_proc, cmd, exitcode, exitwhy, pool);
}


svn_error_t *
svn_io_run_diff2(const char *dir,
                 const char *const *user_args,
                 int num_user_args,
                 const char *label1,
                 const char *label2,
                 const char *from,
                 const char *to,
                 int *pexitcode,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 const char *diff_cmd,
                 apr_pool_t *pool)
{
  const char **args;
  int i;
  int exitcode;
  int nargs = 4; /* the diff command itself, two paths, plus a trailing NULL */
  apr_pool_t *subpool = svn_pool_create(pool);

  if (pexitcode == NULL)
    pexitcode = &exitcode;

  if (user_args != NULL)
    nargs += num_user_args;
  else
    nargs += 1; /* -u */

  if (label1 != NULL)
    nargs += 2; /* the -L and the label itself */
  if (label2 != NULL)
    nargs += 2; /* the -L and the label itself */

  args = apr_palloc(subpool, nargs * sizeof(char *));

  i = 0;
  args[i++] = diff_cmd;

  if (user_args != NULL)
    {
      int j;
      for (j = 0; j < num_user_args; ++j)
        args[i++] = user_args[j];
    }
  else
    args[i++] = "-u"; /* assume -u if the user didn't give us any args */

  if (label1 != NULL)
    {
      args[i++] = "-L";
      args[i++] = label1;
    }
  if (label2 != NULL)
    {
      args[i++] = "-L";
      args[i++] = label2;
    }

  args[i++] = svn_dirent_local_style(from, subpool);
  args[i++] = svn_dirent_local_style(to, subpool);
  args[i++] = NULL;

  SVN_ERR_ASSERT(i == nargs);

  SVN_ERR(svn_io_run_cmd(dir, diff_cmd, args, pexitcode, NULL, TRUE,
                         NULL, outfile, errfile, subpool));

  /* The man page for (GNU) diff describes the return value as:

       "An exit status of 0 means no differences were found, 1 means
        some differences were found, and 2 means trouble."

     A return value of 2 typically occurs when diff cannot read its input
     or write to its output, but in any case we probably ought to return an
     error for anything other than 0 or 1 as the output is likely to be
     corrupt.
   */
  if (*pexitcode != 0 && *pexitcode != 1)
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                             _("'%s' returned %d"),
                             svn_dirent_local_style(diff_cmd, pool),
                             *pexitcode);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_run_diff3_3(int *exitcode,
                   const char *dir,
                   const char *mine,
                   const char *older,
                   const char *yours,
                   const char *mine_label,
                   const char *older_label,
                   const char *yours_label,
                   apr_file_t *merged,
                   const char *diff3_cmd,
                   const apr_array_header_t *user_args,
                   apr_pool_t *pool)
{
  const char **args = apr_palloc(pool,
                                 sizeof(char*) * (13
                                                  + (user_args
                                                     ? user_args->nelts
                                                     : 1)));
#ifndef NDEBUG
  int nargs = 12;
#endif
  int i = 0;

  /* Labels fall back to sensible defaults if not specified. */
  if (mine_label == NULL)
    mine_label = ".working";
  if (older_label == NULL)
    older_label = ".old";
  if (yours_label == NULL)
    yours_label = ".new";

  /* Set up diff3 command line. */
  args[i++] = diff3_cmd;
  if (user_args)
    {
      int j;
      for (j = 0; j < user_args->nelts; ++j)
        args[i++] = APR_ARRAY_IDX(user_args, j, const char *);
#ifndef NDEBUG
      nargs += user_args->nelts;
#endif
    }
  else
    {
      args[i++] = "-E";             /* We tried "-A" here, but that caused
                                       overlapping identical changes to
                                       conflict.  See issue #682. */
#ifndef NDEBUG
      ++nargs;
#endif
    }
  args[i++] = "-m";
  args[i++] = "-L";
  args[i++] = mine_label;
  args[i++] = "-L";
  args[i++] = older_label;      /* note:  this label is ignored if
                                   using 2-part markers, which is the
                                   case with "-E". */
  args[i++] = "-L";
  args[i++] = yours_label;
#ifdef SVN_DIFF3_HAS_DIFF_PROGRAM_ARG
  {
    svn_boolean_t has_arg;

    /* ### FIXME: we really shouldn't be reading the config here;
       instead, the necessary bits should be passed in by the caller.
       But should we add another parameter to this function, when the
       whole external diff3 thing might eventually go away?  */
    apr_hash_t *config;
    svn_config_t *cfg;

    SVN_ERR(svn_config_get_config(&config, pool));
    cfg = config ? svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG) : NULL;
    SVN_ERR(svn_config_get_bool(cfg, &has_arg, SVN_CONFIG_SECTION_HELPERS,
                                SVN_CONFIG_OPTION_DIFF3_HAS_PROGRAM_ARG,
                                TRUE));
    if (has_arg)
      {
        const char *diff_cmd, *diff_utf8;
        svn_config_get(cfg, &diff_cmd, SVN_CONFIG_SECTION_HELPERS,
                       SVN_CONFIG_OPTION_DIFF_CMD, SVN_CLIENT_DIFF);
        SVN_ERR(cstring_to_utf8(&diff_utf8, diff_cmd, pool));
        args[i++] = apr_pstrcat(pool, "--diff-program=", diff_utf8,
                                SVN_VA_NULL);
#ifndef NDEBUG
        ++nargs;
#endif
      }
  }
#endif
  args[i++] = svn_dirent_local_style(mine, pool);
  args[i++] = svn_dirent_local_style(older, pool);
  args[i++] = svn_dirent_local_style(yours, pool);
  args[i++] = NULL;
#ifndef NDEBUG
  SVN_ERR_ASSERT(i == nargs);
#endif

  /* Run diff3, output the merged text into the scratch file. */
  SVN_ERR(svn_io_run_cmd(dir, diff3_cmd, args,
                         exitcode, NULL,
                         TRUE, /* keep environment */
                         NULL, merged, NULL,
                         pool));

  /* According to the diff3 docs, a '0' means the merge was clean, and
     '1' means conflict markers were found.  Anything else is real
     error. */
  if ((*exitcode != 0) && (*exitcode != 1))
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                             _("Error running '%s':  exitcode was %d, "
                               "args were:"
                               "\nin directory '%s', basenames:\n%s\n%s\n%s"),
                             svn_dirent_local_style(diff3_cmd, pool),
                             *exitcode,
                             svn_dirent_local_style(dir, pool),
                             /* Don't call svn_path_local_style() on
                                the basenames.  We don't want them to
                                be absolute, and we don't need the
                                separator conversion. */
                             mine, older, yours);

  return SVN_NO_ERROR;
}


/* Canonicalize a string for hashing.  Modifies KEY in place. */
static APR_INLINE char *
fileext_tolower(char *key)
{
  register char *p;
  for (p = key; *p != 0; ++p)
    *p = (char)apr_tolower(*p);
  return key;
}


svn_error_t *
svn_io_parse_mimetypes_file(apr_hash_t **type_map,
                            const char *mimetypes_file,
                            apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_hash_t *types = apr_hash_make(pool);
  svn_boolean_t eof = FALSE;
  svn_stringbuf_t *buf;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_file_t *types_file;
  svn_stream_t *mimetypes_stream;

  SVN_ERR(svn_io_file_open(&types_file, mimetypes_file,
                           APR_READ, APR_OS_DEFAULT, pool));
  mimetypes_stream = svn_stream_from_aprfile2(types_file, FALSE, pool);

  while (1)
    {
      apr_array_header_t *tokens;
      const char *type;

      svn_pool_clear(subpool);

      /* Read a line. */
      if ((err = svn_stream_readline(mimetypes_stream, &buf,
                                     APR_EOL_STR, &eof, subpool)))
        break;

      /* Only pay attention to non-empty, non-comment lines. */
      if (buf->len)
        {
          int i;

          if (buf->data[0] == '#')
            continue;

          /* Tokenize (into our return pool). */
          tokens = svn_cstring_split(buf->data, " \t", TRUE, pool);
          if (tokens->nelts < 2)
            continue;

          /* The first token in a multi-token line is the media type.
             Subsequent tokens are filename extensions associated with
             that media type. */
          type = APR_ARRAY_IDX(tokens, 0, const char *);
          for (i = 1; i < tokens->nelts; i++)
            {
              /* We can safely address 'ext' as a non-const string because
               * we know svn_cstring_split() allocated it in 'pool' for us. */
              char *ext = APR_ARRAY_IDX(tokens, i, char *);
              fileext_tolower(ext);
              svn_hash_sets(types, ext, type);
            }
        }
      if (eof)
        break;
    }
  svn_pool_destroy(subpool);

  /* If there was an error above, close the file (ignoring any error
     from *that*) and return the originally error. */
  if (err)
    {
      svn_error_clear(svn_stream_close(mimetypes_stream));
      return err;
    }

  /* Close the stream (which closes the underlying file, too). */
  SVN_ERR(svn_stream_close(mimetypes_stream));

  *type_map = types;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_detect_mimetype2(const char **mimetype,
                        const char *file,
                        apr_hash_t *mimetype_map,
                        apr_pool_t *pool)
{
  static const char * const generic_binary = "application/octet-stream";

  svn_node_kind_t kind;
  apr_file_t *fh;
  svn_error_t *err;
  unsigned char block[1024];
  apr_size_t amt_read = sizeof(block);

  /* Default return value is NULL. */
  *mimetype = NULL;

  /* If there is a mimetype_map provided, we'll first try to look up
     our file's extension in the map.  Failing that, we'll run the
     heuristic. */
  if (mimetype_map)
    {
      const char *type_from_map;
      char *path_ext; /* Can point to physical const memory but only when
                         svn_path_splitext sets it to "". */
      svn_path_splitext(NULL, (const char **)&path_ext, file, pool);
      fileext_tolower(path_ext);
      if ((type_from_map = svn_hash_gets(mimetype_map, path_ext)))
        {
          *mimetype = type_from_map;
          return SVN_NO_ERROR;
        }
    }

  /* See if this file even exists, and make sure it really is a file. */
  SVN_ERR(svn_io_check_path(file, &kind, pool));
  if (kind != svn_node_file)
    return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                             _("Can't detect MIME type of non-file '%s'"),
                             svn_dirent_local_style(file, pool));

  SVN_ERR(svn_io_file_open(&fh, file, APR_READ, 0, pool));

  /* Read a block of data from FILE. */
  err = svn_io_file_read(fh, block, &amt_read, pool);
  if (err && ! APR_STATUS_IS_EOF(err->apr_err))
    return err;
  svn_error_clear(err);

  /* Now close the file.  No use keeping it open any more.  */
  SVN_ERR(svn_io_file_close(fh, pool));

  if (svn_io_is_binary_data(block, amt_read))
    *mimetype = generic_binary;

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_io_is_binary_data(const void *data, apr_size_t len)
{
  const unsigned char *buf = data;

  if (len == 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
    {
      /* This is an empty UTF-8 file which only contains the UTF-8 BOM.
       * Treat it as plain text. */
      return FALSE;
    }

  /* Right now, this function is going to be really stupid.  It's
     going to examine the block of data, and make sure that 15%
     of the bytes are such that their value is in the ranges 0x07-0x0D
     or 0x20-0x7F, and that none of those bytes is 0x00.  If those
     criteria are not met, we're calling it binary.

     NOTE:  Originally, I intended to target 85% of the bytes being in
     the specified ranges, but I flubbed the condition.  At any rate,
     folks aren't complaining, so I'm not sure that it's worth
     adjusting this retroactively now.  --cmpilato  */
  if (len > 0)
    {
      apr_size_t i;
      apr_size_t binary_count = 0;

      /* Run through the data we've read, counting the 'binary-ish'
         bytes.  HINT: If we see a 0x00 byte, we'll set our count to its
         max and stop reading the file. */
      for (i = 0; i < len; i++)
        {
          if (buf[i] == 0)
            {
              binary_count = len;
              break;
            }
          if ((buf[i] < 0x07)
              || ((buf[i] > 0x0D) && (buf[i] < 0x20))
              || (buf[i] > 0x7F))
            {
              binary_count++;
            }
        }

      return (((binary_count * 1000) / len) > 850);
    }

  return FALSE;
}


svn_error_t *
svn_io_detect_mimetype(const char **mimetype,
                       const char *file,
                       apr_pool_t *pool)
{
  return svn_io_detect_mimetype2(mimetype, file, NULL, pool);
}


svn_error_t *
svn_io_file_open(apr_file_t **new_file, const char *fname,
                 apr_int32_t flag, apr_fileperms_t perm,
                 apr_pool_t *pool)
{
  const char *fname_apr;
  apr_status_t status;

  SVN_ERR(cstring_from_utf8(&fname_apr, fname, pool));
  status = file_open(new_file, fname_apr, flag | APR_BINARY, perm, TRUE,
                     pool);

  if (status)
    return svn_error_wrap_apr(status, _("Can't open file '%s'"),
                              svn_dirent_local_style(fname, pool));
  else
    return SVN_NO_ERROR;
}


static APR_INLINE svn_error_t *
do_io_file_wrapper_cleanup(apr_file_t *file, apr_status_t status,
                           const char *msg, const char *msg_no_name,
                           apr_pool_t *pool)
{
  const char *name;
  svn_error_t *err;

  if (! status)
    return SVN_NO_ERROR;

  err = svn_io_file_name_get(&name, file, pool);
  if (err)
    name = NULL;
  svn_error_clear(err);

  /* ### Issue #3014: Return a specific error for broken pipes,
   * ### with a single element in the error chain. */
  if (SVN__APR_STATUS_IS_EPIPE(status))
    return svn_error_create(SVN_ERR_IO_PIPE_WRITE_ERROR, NULL, NULL);

  if (name)
    return svn_error_wrap_apr(status, _(msg),
                              try_utf8_from_internal_style(name, pool));
  else
    return svn_error_wrap_apr(status, "%s", _(msg_no_name));
}


svn_error_t *
svn_io_file_close(apr_file_t *file, apr_pool_t *pool)
{
  return do_io_file_wrapper_cleanup(file, apr_file_close(file),
                                    N_("Can't close file '%s'"),
                                    N_("Can't close stream"),
                                    pool);
}


svn_error_t *
svn_io_file_getc(char *ch, apr_file_t *file, apr_pool_t *pool)
{
  return do_io_file_wrapper_cleanup(file, apr_file_getc(ch, file),
                                    N_("Can't read file '%s'"),
                                    N_("Can't read stream"),
                                    pool);
}


svn_error_t *
svn_io_file_putc(char ch, apr_file_t *file, apr_pool_t *pool)
{
  return do_io_file_wrapper_cleanup(file, apr_file_putc(ch, file),
                                    N_("Can't write file '%s'"),
                                    N_("Can't write stream"),
                                    pool);
}


svn_error_t *
svn_io_file_info_get(apr_finfo_t *finfo, apr_int32_t wanted,
                     apr_file_t *file, apr_pool_t *pool)
{
  /* Quoting APR: On NT this request is incredibly expensive, but accurate. */
  wanted &= ~SVN__APR_FINFO_MASK_OUT;

  return do_io_file_wrapper_cleanup(
             file, apr_file_info_get(finfo, wanted, file),
             N_("Can't get attribute information from file '%s'"),
             N_("Can't get attribute information from stream"),
             pool);
}

svn_error_t *
svn_io_file_size_get(svn_filesize_t *filesize_p, apr_file_t *file,
                     apr_pool_t *pool)
{
  apr_finfo_t finfo;
  SVN_ERR(svn_io_file_info_get(&finfo, APR_FINFO_SIZE, file, pool));

  *filesize_p = finfo.size;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_file_get_offset(apr_off_t *offset_p,
                       apr_file_t *file,
                       apr_pool_t *pool)
{
  apr_off_t offset;

  /* Note that, for buffered files, one (possibly surprising) side-effect
     of this call is to flush any unwritten data to disk. */
  offset = 0;
  SVN_ERR(svn_io_file_seek(file, APR_CUR, &offset, pool));
  *offset_p = offset;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_file_read(apr_file_t *file, void *buf,
                 apr_size_t *nbytes, apr_pool_t *pool)
{
  return do_io_file_wrapper_cleanup(file, apr_file_read(file, buf, nbytes),
                                    N_("Can't read file '%s'"),
                                    N_("Can't read stream"),
                                    pool);
}


svn_error_t *
svn_io_file_read_full2(apr_file_t *file, void *buf,
                       apr_size_t nbytes, apr_size_t *bytes_read,
                       svn_boolean_t *hit_eof,
                       apr_pool_t *pool)
{
  apr_status_t status = apr_file_read_full(file, buf, nbytes, bytes_read);
  if (hit_eof)
    {
      if (APR_STATUS_IS_EOF(status))
        {
          *hit_eof = TRUE;
          return SVN_NO_ERROR;
        }
      else
        *hit_eof = FALSE;
    }

  return do_io_file_wrapper_cleanup(file, status,
                                    N_("Can't read file '%s'"),
                                    N_("Can't read stream"),
                                    pool);
}


svn_error_t *
svn_io_file_seek(apr_file_t *file, apr_seek_where_t where,
                 apr_off_t *offset, apr_pool_t *pool)
{
  return do_io_file_wrapper_cleanup(
             file, apr_file_seek(file, where, offset),
             N_("Can't set position pointer in file '%s'"),
             N_("Can't set position pointer in stream"),
             pool);
}

svn_error_t *
svn_io_file_aligned_seek(apr_file_t *file,
                         apr_off_t block_size,
                         apr_off_t *buffer_start,
                         apr_off_t offset,
                         apr_pool_t *scratch_pool)
{
  const apr_size_t apr_default_buffer_size = 4096;
  apr_size_t file_buffer_size = apr_default_buffer_size;
  apr_off_t desired_offset = 0;
  apr_off_t current = 0;
  apr_off_t aligned_offset = 0;
  svn_boolean_t fill_buffer = FALSE;

  /* paranoia check: huge blocks on 32 bit machines may cause overflows */
  SVN_ERR_ASSERT(block_size == (apr_size_t)block_size);

  /* default for invalid block sizes */
  if (block_size == 0)
    block_size = apr_default_buffer_size;

  file_buffer_size = apr_file_buffer_size_get(file);

  /* don't try to set a buffer size for non-buffered files! */
  if (file_buffer_size == 0)
    {
      aligned_offset = offset;
    }
  else if (file_buffer_size != (apr_size_t)block_size)
    {
      /* FILE has the wrong buffer size. correct it */
      char *buffer;
      file_buffer_size = (apr_size_t)block_size;
      buffer = apr_palloc(apr_file_pool_get(file), file_buffer_size);
      apr_file_buffer_set(file, buffer, file_buffer_size);

      /* seek to the start of the block and cause APR to read 1 block */
      aligned_offset = offset - (offset % block_size);
      fill_buffer = TRUE;
    }
  else
    {
      aligned_offset = offset - (offset % file_buffer_size);

      /* We have no way to determine the block start of an APR file.
         Furthermore, we don't want to throw away the current buffer
         contents.  Thus, we re-align the buffer only if the CURRENT
         offset definitely lies outside the desired, aligned buffer.
         This covers the typical case of linear reads getting very
         close to OFFSET but reading the previous / following block.

         Note that ALIGNED_OFFSET may still be within the current
         buffer and no I/O will actually happen in the FILL_BUFFER
         section below.
       */
      SVN_ERR(svn_io_file_seek(file, APR_CUR, &current, scratch_pool));
      fill_buffer = aligned_offset + file_buffer_size <= current
                 || current <= aligned_offset;
    }

  if (fill_buffer)
    {
      char dummy;
      apr_status_t status;

      /* seek to the start of the block and cause APR to read 1 block */
      SVN_ERR(svn_io_file_seek(file, APR_SET, &aligned_offset,
                               scratch_pool));
      status = apr_file_getc(&dummy, file);

      /* read may fail if we seek to or behind EOF.  That's ok then. */
      if (status != APR_SUCCESS && !APR_STATUS_IS_EOF(status))
        return do_io_file_wrapper_cleanup(file, status,
                                          N_("Can't read file '%s'"),
                                          N_("Can't read stream"),
                                          scratch_pool);
    }

  /* finally, seek to the OFFSET the caller wants */
  desired_offset = offset;
  SVN_ERR(svn_io_file_seek(file, APR_SET, &offset, scratch_pool));
  if (desired_offset != offset)
    return do_io_file_wrapper_cleanup(file, APR_EOF,
                                      N_("Can't seek in file '%s'"),
                                      N_("Can't seek in stream"),
                                      scratch_pool);

  /* return the buffer start that we (probably) enforced */
  if (buffer_start)
    *buffer_start = aligned_offset;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_file_write(apr_file_t *file, const void *buf,
                  apr_size_t *nbytes, apr_pool_t *pool)
{
  return svn_error_trace(do_io_file_wrapper_cleanup(
     file, apr_file_write(file, buf, nbytes),
     N_("Can't write to file '%s'"),
     N_("Can't write to stream"),
     pool));
}

svn_error_t *
svn_io_file_flush(apr_file_t *file,
                  apr_pool_t *scratch_pool)
{
  return svn_error_trace(do_io_file_wrapper_cleanup(
     file, apr_file_flush(file),
     N_("Can't flush file '%s'"),
     N_("Can't flush stream"),
     scratch_pool));
}

svn_error_t *
svn_io_file_write_full(apr_file_t *file, const void *buf,
                       apr_size_t nbytes, apr_size_t *bytes_written,
                       apr_pool_t *pool)
{
#ifdef WIN32
#define MAXBUFSIZE 30*1024
  apr_size_t bw = nbytes;
  apr_size_t to_write = nbytes;
  apr_status_t rv;

  rv = apr_file_write_full(file, buf, nbytes, &bw);
  buf = (char *)buf + bw;
  to_write -= bw;

  /* Issue #1789: On Windows, writing may fail for large values of NBYTES.
     If that is the case, keep track of how many bytes have been written
     by the apr_file_write_full() call, and attempt to write the remaining
     part in smaller chunks. */
  if (rv == APR_FROM_OS_ERROR(ERROR_NOT_ENOUGH_MEMORY)
      && nbytes > MAXBUFSIZE)
    {
      do {
        bw = to_write > MAXBUFSIZE ? MAXBUFSIZE : to_write;
        rv = apr_file_write(file, buf, &bw);
        buf = (char *)buf + bw;
        to_write -= bw;
      } while (rv == APR_SUCCESS && to_write > 0);
    }

  /* bytes_written may actually be NULL */
  if (bytes_written)
    *bytes_written = nbytes - to_write;
#undef MAXBUFSIZE
#else
  apr_status_t rv = apr_file_write_full(file, buf, nbytes, bytes_written);
#endif

  return svn_error_trace(do_io_file_wrapper_cleanup(
     file, rv,
     N_("Can't write to file '%s'"),
     N_("Can't write to stream"),
     pool));
}


svn_error_t *
svn_io_write_unique(const char **tmp_path,
                    const char *dirpath,
                    const void *buf,
                    apr_size_t nbytes,
                    svn_io_file_del_t delete_when,
                    apr_pool_t *pool)
{
  apr_file_t *new_file;
  svn_error_t *err;

  SVN_ERR(svn_io_open_unique_file3(&new_file, tmp_path, dirpath,
                                   delete_when, pool, pool));

  err = svn_io_file_write_full(new_file, buf, nbytes, NULL, pool);

  if (!err)
    {
      /* svn_io_file_flush_to_disk() can be very expensive, so use the
         cheaper standard flush if the file is created as temporary file
         anyway */
      if (delete_when == svn_io_file_del_none)
        err = svn_io_file_flush_to_disk(new_file, pool);
      else
        err = svn_io_file_flush(new_file, pool);
    }

  return svn_error_trace(
                  svn_error_compose_create(err,
                                           svn_io_file_close(new_file, pool)));
}

svn_error_t *
svn_io_write_atomic2(const char *final_path,
                     const void *buf,
                     apr_size_t nbytes,
                     const char *copy_perms_path,
                     svn_boolean_t flush_to_disk,
                     apr_pool_t *scratch_pool)
{
  apr_file_t *tmp_file;
  const char *tmp_path;
  svn_error_t *err;
  const char *dirname = svn_dirent_dirname(final_path, scratch_pool);

  SVN_ERR(svn_io_open_unique_file3(&tmp_file, &tmp_path, dirname,
                                   svn_io_file_del_none,
                                   scratch_pool, scratch_pool));

  err = svn_io_file_write_full(tmp_file, buf, nbytes, NULL, scratch_pool);

  if (!err && flush_to_disk)
    err = svn_io_file_flush_to_disk(tmp_file, scratch_pool);

  err = svn_error_compose_create(err,
                                 svn_io_file_close(tmp_file, scratch_pool));

  if (!err && copy_perms_path)
    err = svn_io_copy_perms(copy_perms_path, tmp_path, scratch_pool);

  if (!err)
    err = svn_io_file_rename2(tmp_path, final_path, flush_to_disk,
                              scratch_pool);

  if (err)
    {
      err = svn_error_compose_create(err,
                                     svn_io_remove_file2(tmp_path, TRUE,
                                                         scratch_pool));

      return svn_error_createf(err->apr_err, err,
                               _("Can't write '%s' atomically"),
                               svn_dirent_local_style(final_path,
                                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_file_trunc(apr_file_t *file, apr_off_t offset, apr_pool_t *pool)
{
  /* Workaround for yet another APR issue with trunc.

     If the APR file internally is in read mode, the current buffer pointer
     will not be clipped to the valid data range. get_file_offset may then
     return an invalid position *after* new data was written to it.

     To prevent this, write 1 dummy byte just after the OFFSET at which we
     will trunc it.  That will force the APR file into write mode
     internally and the flush() work-around below becomes effective. */
  apr_off_t position = 0;

  /* A frequent usage is OFFSET==0, in which case we don't need to preserve
     any file content or file pointer. */
  if (offset)
    {
      SVN_ERR(svn_io_file_seek(file, APR_CUR, &position, pool));
      SVN_ERR(svn_io_file_seek(file, APR_SET, &offset, pool));
    }
  SVN_ERR(svn_io_file_putc(0, file, pool));

  /* This is a work-around. APR would flush the write buffer
     _after_ truncating the file causing now invalid buffered
     data to be written behind OFFSET. */
  SVN_ERR(do_io_file_wrapper_cleanup(file, apr_file_flush(file),
                                     N_("Can't flush file '%s'"),
                                     N_("Can't flush stream"),
                                     pool));

  SVN_ERR(do_io_file_wrapper_cleanup(file, apr_file_trunc(file, offset),
                                     N_("Can't truncate file '%s'"),
                                     N_("Can't truncate stream"),
                                     pool));

  /* Restore original file pointer, if necessary.
     It's currently at OFFSET. */
  if (position < offset)
    SVN_ERR(svn_io_file_seek(file, APR_SET, &position, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_read_length_line(apr_file_t *file, char *buf, apr_size_t *limit,
                        apr_pool_t *pool)
{
  /* variables */
  apr_size_t total_read = 0;
  svn_boolean_t eof = FALSE;
  const char *name;
  svn_error_t *err;
  apr_size_t buf_size = *limit;

  while (buf_size > 0)
    {
      /* read a fair chunk of data at once. But don't get too ambitious
       * as that would result in too much waste. Also make sure we can
       * put a NUL after the last byte read.
       */
      apr_size_t to_read = buf_size < 129 ? buf_size - 1 : 128;
      apr_size_t bytes_read = 0;
      char *eol;

      if (to_read == 0)
        break;

      /* read data block (or just a part of it) */
      SVN_ERR(svn_io_file_read_full2(file, buf, to_read,
                                     &bytes_read, &eof, pool));

      /* look or a newline char */
      buf[bytes_read] = 0;
      eol = strchr(buf, '\n');
      if (eol)
        {
          apr_off_t offset = (eol + 1 - buf) - (apr_off_t)bytes_read;

          *eol = 0;
          *limit = total_read + (eol - buf);

          /* correct the file pointer:
           * appear as though we just had read the newline char
           */
          SVN_ERR(svn_io_file_seek(file, APR_CUR, &offset, pool));

          return SVN_NO_ERROR;
        }
      else if (eof)
        {
          /* no EOL found but we hit the end of the file.
           * Generate a nice EOF error object and return it.
           */
          char dummy;
          SVN_ERR(svn_io_file_getc(&dummy, file, pool));
        }

      /* next data chunk */
      buf_size -= bytes_read;
      buf += bytes_read;
      total_read += bytes_read;
    }

  /* buffer limit has been exceeded without finding the EOL */
  err = svn_io_file_name_get(&name, file, pool);
  if (err)
    name = NULL;
  svn_error_clear(err);

  if (name)
    return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                             _("Can't read length line in file '%s'"),
                             svn_dirent_local_style(name, pool));
  else
    return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                            _("Can't read length line in stream"));
}


svn_error_t *
svn_io_stat(apr_finfo_t *finfo, const char *fname,
            apr_int32_t wanted, apr_pool_t *pool)
{
  apr_status_t status;
  const char *fname_apr;

  /* APR doesn't like "" directories */
  if (fname[0] == '\0')
    fname = ".";

  SVN_ERR(cstring_from_utf8(&fname_apr, fname, pool));

  /* Quoting APR: On NT this request is incredibly expensive, but accurate. */
  wanted &= ~SVN__APR_FINFO_MASK_OUT;

  status = apr_stat(finfo, fname_apr, wanted, pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't stat '%s'"),
                              svn_dirent_local_style(fname, pool));

  return SVN_NO_ERROR;
}

#if defined(WIN32)
/* Platform specific implementation of apr_file_rename() to workaround
   APR problems on Windows. */
static apr_status_t
win32_file_rename(const WCHAR *from_path_w,
                  const WCHAR *to_path_w,
                  svn_boolean_t flush_to_disk)
{
  /* APR calls MoveFileExW() with MOVEFILE_COPY_ALLOWED, while we rely
   * that rename is atomic operation. Call MoveFileEx directly on Windows
   * without MOVEFILE_COPY_ALLOWED flag to workaround it.
   */

  DWORD flags = MOVEFILE_REPLACE_EXISTING;

  if (flush_to_disk)
    {
      /* Do not return until the file has actually been moved on the disk. */
      flags |= MOVEFILE_WRITE_THROUGH;
    }

  if (!MoveFileExW(from_path_w, to_path_w, flags))
      return apr_get_os_error();

  return APR_SUCCESS;
}
#endif

svn_error_t *
svn_io_file_rename2(const char *from_path, const char *to_path,
                    svn_boolean_t flush_to_disk, apr_pool_t *pool)
{
  apr_status_t status = APR_SUCCESS;
  const char *from_path_apr, *to_path_apr;
#if defined(WIN32)
  WCHAR *from_path_w;
  WCHAR *to_path_w;
#endif

  SVN_ERR(cstring_from_utf8(&from_path_apr, from_path, pool));
  SVN_ERR(cstring_from_utf8(&to_path_apr, to_path, pool));

#if defined(WIN32)
  SVN_ERR(svn_io__utf8_to_unicode_longpath(&from_path_w, from_path_apr, pool));
  SVN_ERR(svn_io__utf8_to_unicode_longpath(&to_path_w, to_path_apr, pool));
  status = win32_file_rename(from_path_w, to_path_w, flush_to_disk);

  /* If the target file is read only NTFS reports EACCESS and
     FAT/FAT32 reports EEXIST */
  if (APR_STATUS_IS_EACCES(status) || APR_STATUS_IS_EEXIST(status))
    {
      /* Set the destination file writable because Windows will not
         allow us to rename when to_path is read-only, but will
         allow renaming when from_path is read only. */
      SVN_ERR(svn_io_set_file_read_write(to_path, TRUE, pool));

      status = win32_file_rename(from_path_w, to_path_w, flush_to_disk);
    }
  WIN32_RETRY_LOOP(status, win32_file_rename(from_path_w, to_path_w,
                                             flush_to_disk));
#elif defined(__OS2__)
  status = apr_file_rename(from_path_apr, to_path_apr, pool);
  /* If the target file is read only NTFS reports EACCESS and
     FAT/FAT32 reports EEXIST */
  if (APR_STATUS_IS_EACCES(status) || APR_STATUS_IS_EEXIST(status))
    {
      /* Set the destination file writable because OS/2 will not
         allow us to rename when to_path is read-only, but will
         allow renaming when from_path is read only. */
      SVN_ERR(svn_io_set_file_read_write(to_path, TRUE, pool));

      status = apr_file_rename(from_path_apr, to_path_apr, pool);
    }
#else
  status = apr_file_rename(from_path_apr, to_path_apr, pool);
#endif /* WIN32 || __OS2__ */

  if (status)
    return svn_error_wrap_apr(status, _("Can't move '%s' to '%s'"),
                              svn_dirent_local_style(from_path, pool),
                              svn_dirent_local_style(to_path, pool));

#if defined(SVN_ON_POSIX)
  if (flush_to_disk)
    {
      /* On POSIX, the file name is stored in the file's directory entry.
         Hence, we need to fsync() that directory as well.
         On other operating systems, we'd only be asking for trouble
         by trying to open and fsync a directory. */
      const char *dirname;
      apr_file_t *file;

      dirname = svn_dirent_dirname(to_path, pool);
      SVN_ERR(svn_io_file_open(&file, dirname, APR_READ, APR_OS_DEFAULT,
                               pool));
      SVN_ERR(svn_io_file_flush_to_disk(file, pool));
      SVN_ERR(svn_io_file_close(file, pool));
    }
#elif !defined(WIN32)
  /* Flush the target of the rename to disk. */
  if (flush_to_disk)
    {
      apr_file_t *file;
      SVN_ERR(svn_io_file_open(&file, to_path, APR_WRITE,
                               APR_OS_DEFAULT, pool));
      SVN_ERR(svn_io_file_flush_to_disk(file, pool));
      SVN_ERR(svn_io_file_close(file, pool));
    }
#endif

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_file_move(const char *from_path, const char *to_path,
                 apr_pool_t *pool)
{
  svn_error_t *err = svn_error_trace(svn_io_file_rename2(from_path, to_path,
                                                         FALSE, pool));

  if (err && APR_STATUS_IS_EXDEV(err->apr_err))
    {
      svn_error_clear(err);

      /* svn_io_copy_file() performs atomic copy via temporary file. */
      err = svn_error_trace(svn_io_copy_file(from_path, to_path, TRUE,
                                             pool));
    }

  return err;
}

/* Common implementation of svn_io_dir_make and svn_io_dir_make_hidden.
   HIDDEN determines if the hidden attribute
   should be set on the newly created directory. */
static svn_error_t *
dir_make(const char *path, apr_fileperms_t perm,
         svn_boolean_t hidden, svn_boolean_t sgid, apr_pool_t *pool)
{
  apr_status_t status;
  const char *path_apr;

  SVN_ERR(cstring_from_utf8(&path_apr, path, pool));

  /* APR doesn't like "" directories */
  if (path_apr[0] == '\0')
    path_apr = ".";

#if (APR_OS_DEFAULT & APR_WSTICKY)
  /* The APR shipped with httpd 2.0.50 contains a bug where
     APR_OS_DEFAULT encompasses the setuid, setgid, and sticky bits.
     There is a special case for file creation, but not directory
     creation, so directories wind up getting created with the sticky
     bit set.  (There is no such thing as a setuid directory, and the
     setgid bit is apparently ignored at mkdir() time.)  If we detect
     this problem, work around it by unsetting those bits if we are
     passed APR_OS_DEFAULT. */
  if (perm == APR_OS_DEFAULT)
    perm &= ~(APR_USETID | APR_GSETID | APR_WSTICKY);
#endif

  status = apr_dir_make(path_apr, perm, pool);

#ifdef WIN32
  /* Don't retry on ERROR_ACCESS_DENIED, as that typically signals a
     permanent error */
  if (status == APR_FROM_OS_ERROR(ERROR_SHARING_VIOLATION))
    WIN32_RETRY_LOOP(status, apr_dir_make(path_apr, perm, pool));
#endif

  if (status)
    return svn_error_wrap_apr(status, _("Can't create directory '%s'"),
                              svn_dirent_local_style(path, pool));

#ifdef APR_FILE_ATTR_HIDDEN
  if (hidden)
    {
#ifndef WIN32
      status = apr_file_attrs_set(path_apr,
                                  APR_FILE_ATTR_HIDDEN,
                                  APR_FILE_ATTR_HIDDEN,
                                  pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't hide directory '%s'"),
                                  svn_dirent_local_style(path, pool));
#else
    /* on Windows, use our wrapper so we can also set the
       FILE_ATTRIBUTE_NOT_CONTENT_INDEXED attribute */
      svn_error_t *err =
          io_win_file_attrs_set(path_apr,
                                FILE_ATTRIBUTE_HIDDEN |
                                FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                                FILE_ATTRIBUTE_HIDDEN |
                                FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                                pool);
      if (err)
        return svn_error_createf(err->apr_err, err,
                                 _("Can't hide directory '%s'"),
                                 svn_dirent_local_style(path, pool));
#endif /* WIN32 */
    }
#endif /* APR_FILE_ATTR_HIDDEN */

/* Windows does not implement sgid. Skip here because retrieving
   the file permissions via APR_FINFO_PROT | APR_FINFO_OWNER is documented
   to be 'incredibly expensive'. */
#ifndef WIN32
  if (sgid)
    {
      apr_finfo_t finfo;

      /* Per our contract, don't do error-checking.  Some filesystems
       * don't support the sgid bit, and that's okay. */
      status = apr_stat(&finfo, path_apr, APR_FINFO_PROT, pool);

      if (!status)
        apr_file_perms_set(path_apr, finfo.protection | APR_GSETID);
    }
#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_dir_make(const char *path, apr_fileperms_t perm, apr_pool_t *pool)
{
  return dir_make(path, perm, FALSE, FALSE, pool);
}

svn_error_t *
svn_io_dir_make_hidden(const char *path, apr_fileperms_t perm,
                       apr_pool_t *pool)
{
  return dir_make(path, perm, TRUE, FALSE, pool);
}

svn_error_t *
svn_io_dir_make_sgid(const char *path, apr_fileperms_t perm,
                     apr_pool_t *pool)
{
  return dir_make(path, perm, FALSE, TRUE, pool);
}


svn_error_t *
svn_io_dir_open(apr_dir_t **new_dir, const char *dirname, apr_pool_t *pool)
{
  apr_status_t status;
  const char *dirname_apr;

  /* APR doesn't like "" directories */
  if (dirname[0] == '\0')
    dirname = ".";

  SVN_ERR(cstring_from_utf8(&dirname_apr, dirname, pool));

  status = apr_dir_open(new_dir, dirname_apr, pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't open directory '%s'"),
                              svn_dirent_local_style(dirname, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_dir_remove_nonrecursive(const char *dirname, apr_pool_t *pool)
{
  apr_status_t status;
  const char *dirname_apr;

  SVN_ERR(cstring_from_utf8(&dirname_apr, dirname, pool));

  status = apr_dir_remove(dirname_apr, pool);

#ifdef WIN32
  {
    svn_boolean_t retry = TRUE;

    if (status == APR_FROM_OS_ERROR(ERROR_DIR_NOT_EMPTY))
    {
        apr_status_t empty_status = dir_is_empty(dirname_apr, pool);

        if (APR_STATUS_IS_ENOTEMPTY(empty_status))
          retry = FALSE;
      }

    if (retry)
      {
        WIN32_RETRY_LOOP(status, apr_dir_remove(dirname_apr, pool));
      }
  }
#endif
  if (status)
    return svn_error_wrap_apr(status, _("Can't remove directory '%s'"),
                              svn_dirent_local_style(dirname, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_dir_read(apr_finfo_t *finfo,
                apr_int32_t wanted,
                apr_dir_t *thedir,
                apr_pool_t *pool)
{
  apr_status_t status;

  status = apr_dir_read(finfo, wanted, thedir);

  if (status)
    return svn_error_wrap_apr(status, _("Can't read directory"));

  /* It would be nice to use entry_name_to_utf8() below, but can we
     get the dir's path out of an apr_dir_t?  I don't see a reliable
     way to do it. */

  if (finfo->fname)
    SVN_ERR(svn_path_cstring_to_utf8(&finfo->fname, finfo->fname, pool));

  if (finfo->name)
    SVN_ERR(svn_path_cstring_to_utf8(&finfo->name, finfo->name, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_dir_close(apr_dir_t *thedir)
{
  apr_status_t apr_err = apr_dir_close(thedir);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Error closing directory"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_dir_walk2(const char *dirname,
                 apr_int32_t wanted,
                 svn_io_walk_func_t walk_func,
                 void *walk_baton,
                 apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_dir_t *handle;
  apr_pool_t *subpool;
  const char *dirname_apr;
  apr_finfo_t finfo;

  wanted |= APR_FINFO_TYPE | APR_FINFO_NAME;

  /* Quoting APR: On NT this request is incredibly expensive, but accurate. */
  wanted &= ~SVN__APR_FINFO_MASK_OUT;

  /* The documentation for apr_dir_read used to state that "." and ".."
     will be returned as the first two files, but it doesn't
     work that way in practice, in particular ext3 on Linux-2.6 doesn't
     follow the rules.  For details see
     http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=56666

     If APR ever does implement "dot-first" then it would be possible to
     remove the svn_io_stat and walk_func calls and use the walk_func
     inside the loop.

     Note: apr_stat doesn't handle FINFO_NAME but svn_io_dir_walk is
     documented to provide it, so we have to do a bit extra. */
  SVN_ERR(svn_io_stat(&finfo, dirname, wanted & ~APR_FINFO_NAME, pool));
  SVN_ERR(cstring_from_utf8(&finfo.name,
                            svn_dirent_basename(dirname, pool),
                            pool));
  finfo.valid |= APR_FINFO_NAME;
  SVN_ERR((*walk_func)(walk_baton, dirname, &finfo, pool));

  SVN_ERR(cstring_from_utf8(&dirname_apr, dirname, pool));

  /* APR doesn't like "" directories */
  if (dirname_apr[0] == '\0')
    dirname_apr = ".";

  apr_err = apr_dir_open(&handle, dirname_apr, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't open directory '%s'"),
                              svn_dirent_local_style(dirname, pool));

  /* iteration subpool */
  subpool = svn_pool_create(pool);

  while (1)
    {
      const char *name_utf8;
      const char *full_path;

      svn_pool_clear(subpool);

      apr_err = apr_dir_read(&finfo, wanted, handle);
      if (APR_STATUS_IS_ENOENT(apr_err))
        break;
      else if (apr_err)
        {
          return svn_error_wrap_apr(apr_err,
                                    _("Can't read directory entry in '%s'"),
                                    svn_dirent_local_style(dirname, pool));
        }

      if (finfo.filetype == APR_DIR)
        {
          if (finfo.name[0] == '.'
              && (finfo.name[1] == '\0'
                  || (finfo.name[1] == '.' && finfo.name[2] == '\0')))
            /* skip "." and ".." */
            continue;

          /* some other directory. recurse. it will be passed to the
             callback inside the recursion. */
          SVN_ERR(entry_name_to_utf8(&name_utf8, finfo.name, dirname,
                                     subpool));
          full_path = svn_dirent_join(dirname, name_utf8, subpool);
          SVN_ERR(svn_io_dir_walk2(full_path,
                                   wanted,
                                   walk_func,
                                   walk_baton,
                                   subpool));
        }
      else if (finfo.filetype == APR_REG || finfo.filetype == APR_LNK)
        {
          /* some other directory. pass it to the callback. */
          SVN_ERR(entry_name_to_utf8(&name_utf8, finfo.name, dirname,
                                     subpool));
          full_path = svn_dirent_join(dirname, name_utf8, subpool);
          SVN_ERR((*walk_func)(walk_baton,
                               full_path,
                               &finfo,
                               subpool));
        }
      /* else:
         Some other type of file; skip it for now.  We've reserved the
         right to expand our coverage here in the future, though,
         without revving this API.
      */
    }

  svn_pool_destroy(subpool);

  apr_err = apr_dir_close(handle);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Error closing directory '%s'"),
                              svn_dirent_local_style(dirname, pool));

  return SVN_NO_ERROR;
}



/**
 * Determine if a directory is empty or not.
 * @param Return APR_SUCCESS if the dir is empty, else APR_ENOTEMPTY if not.
 * @param path The directory.
 * @param pool Used for temporary allocation.
 * @remark If path is not a directory, or some other error occurs,
 * then return the appropriate apr status code.
 *
 * (This function is written in APR style, in anticipation of
 * perhaps someday being moved to APR as 'apr_dir_is_empty'.)
 */
static apr_status_t
dir_is_empty(const char *dir, apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_dir_t *dir_handle;
  apr_finfo_t finfo;
  apr_status_t retval = APR_SUCCESS;

  /* APR doesn't like "" directories */
  if (dir[0] == '\0')
    dir = ".";

  apr_err = apr_dir_open(&dir_handle, dir, pool);
  if (apr_err != APR_SUCCESS)
    return apr_err;

  for (apr_err = apr_dir_read(&finfo, APR_FINFO_NAME, dir_handle);
       apr_err == APR_SUCCESS;
       apr_err = apr_dir_read(&finfo, APR_FINFO_NAME, dir_handle))
    {
      /* Ignore entries for this dir and its parent, robustly.
         (APR promises that they'll come first, so technically
         this guard could be moved outside the loop.  But Ryan Bloom
         says he doesn't believe it, and I believe him. */
      if (! (finfo.name[0] == '.'
             && (finfo.name[1] == '\0'
                 || (finfo.name[1] == '.' && finfo.name[2] == '\0'))))
        {
          retval = APR_ENOTEMPTY;
          break;
        }
    }

  /* Make sure we broke out of the loop for the right reason. */
  if (apr_err && ! APR_STATUS_IS_ENOENT(apr_err))
    return apr_err;

  apr_err = apr_dir_close(dir_handle);
  if (apr_err != APR_SUCCESS)
    return apr_err;

  return retval;
}


svn_error_t *
svn_io_dir_empty(svn_boolean_t *is_empty_p,
                 const char *path,
                 apr_pool_t *pool)
{
  apr_status_t status;
  const char *path_apr;

  SVN_ERR(cstring_from_utf8(&path_apr, path, pool));

  status = dir_is_empty(path_apr, pool);

  if (!status)
    *is_empty_p = TRUE;
  else if (APR_STATUS_IS_ENOTEMPTY(status))
    *is_empty_p = FALSE;
  else
    return svn_error_wrap_apr(status, _("Can't check directory '%s'"),
                              svn_dirent_local_style(path, pool));

  return SVN_NO_ERROR;
}



/*** Version/format files ***/

svn_error_t *
svn_io_write_version_file(const char *path,
                          int version,
                          apr_pool_t *pool)
{
  const char *path_tmp;
  const char *format_contents = apr_psprintf(pool, "%d\n", version);

  SVN_ERR_ASSERT(version >= 0);

  SVN_ERR(svn_io_write_unique(&path_tmp,
                              svn_dirent_dirname(path, pool),
                              format_contents, strlen(format_contents),
                              svn_io_file_del_none, pool));

#if defined(WIN32) || defined(__OS2__)
  /* make the destination writable, but only on Windows, because
     Windows does not let us replace read-only files. */
  SVN_ERR(svn_io_set_file_read_write(path, TRUE, pool));
#endif /* WIN32 || __OS2__ */

  /* rename the temp file as the real destination */
  SVN_ERR(svn_io_file_rename2(path_tmp, path, FALSE, pool));

  /* And finally remove the perms to make it read only */
  return svn_io_set_file_read_only(path, FALSE, pool);
}


svn_error_t *
svn_io_read_version_file(int *version,
                         const char *path,
                         apr_pool_t *pool)
{
  apr_file_t *format_file;
  char buf[80];
  apr_size_t len;
  svn_error_t *err;

  /* Read a chunk of data from PATH */
  SVN_ERR(svn_io_file_open(&format_file, path, APR_READ,
                           APR_OS_DEFAULT, pool));
  len = sizeof(buf);
  err = svn_io_file_read(format_file, buf, &len, pool);

  /* Close the file. */
  SVN_ERR(svn_error_compose_create(err,
                                   svn_io_file_close(format_file, pool)));

  /* If there was no data in PATH, return an error. */
  if (len == 0)
    return svn_error_createf(SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                             _("Reading '%s'"),
                             svn_dirent_local_style(path, pool));

  /* Check that the first line contains only digits. */
  {
    apr_size_t i;

    for (i = 0; i < len; ++i)
      {
        char c = buf[i];

        if (i > 0 && (c == '\r' || c == '\n'))
          {
            buf[i] = '\0';
            break;
          }
        if (! svn_ctype_isdigit(c))
          return svn_error_createf
            (SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
             _("First line of '%s' contains non-digit"),
             svn_dirent_local_style(path, pool));
      }
  }

  /* Convert to integer. */
  SVN_ERR(svn_cstring_atoi(version, buf));

  return SVN_NO_ERROR;
}


/* Do a byte-for-byte comparison of FILE1 and FILE2. */
static svn_error_t *
contents_identical_p(svn_boolean_t *identical_p,
                     const char *file1,
                     const char *file2,
                     apr_pool_t *pool)
{
  svn_error_t *err;
  apr_size_t bytes_read1, bytes_read2;
  char *buf1 = apr_palloc(pool, SVN__STREAM_CHUNK_SIZE);
  char *buf2 = apr_palloc(pool, SVN__STREAM_CHUNK_SIZE);
  apr_file_t *file1_h;
  apr_file_t *file2_h;
  svn_boolean_t eof1 = FALSE;
  svn_boolean_t eof2 = FALSE;

  SVN_ERR(svn_io_file_open(&file1_h, file1, APR_READ, APR_OS_DEFAULT,
                           pool));

  err = svn_io_file_open(&file2_h, file2, APR_READ, APR_OS_DEFAULT,
                         pool);

  if (err)
    return svn_error_trace(
               svn_error_compose_create(err,
                                        svn_io_file_close(file1_h, pool)));

  *identical_p = TRUE;  /* assume TRUE, until disproved below */
  while (!err && !eof1 && !eof2)
    {
      err = svn_io_file_read_full2(file1_h, buf1,
                                   SVN__STREAM_CHUNK_SIZE, &bytes_read1,
                                   &eof1, pool);
      if (err)
          break;

      err = svn_io_file_read_full2(file2_h, buf2,
                                   SVN__STREAM_CHUNK_SIZE, &bytes_read2,
                                   &eof2, pool);
      if (err)
          break;

      if ((bytes_read1 != bytes_read2) || memcmp(buf1, buf2, bytes_read1))
        {
          *identical_p = FALSE;
          break;
        }
    }

  /* Special case: one file being a prefix of the other and the shorter
   * file's size is a multiple of SVN__STREAM_CHUNK_SIZE. */
  if (!err && (eof1 != eof2))
    *identical_p = FALSE;

  return svn_error_trace(
           svn_error_compose_create(
                err,
                svn_error_compose_create(svn_io_file_close(file1_h, pool),
                                         svn_io_file_close(file2_h, pool))));
}



/* Do a byte-for-byte comparison of FILE1, FILE2 and FILE3. */
static svn_error_t *
contents_three_identical_p(svn_boolean_t *identical_p12,
                           svn_boolean_t *identical_p23,
                           svn_boolean_t *identical_p13,
                           const char *file1,
                           const char *file2,
                           const char *file3,
                           apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  char *buf1 = apr_palloc(scratch_pool, SVN__STREAM_CHUNK_SIZE);
  char *buf2 = apr_palloc(scratch_pool, SVN__STREAM_CHUNK_SIZE);
  char *buf3 = apr_palloc(scratch_pool, SVN__STREAM_CHUNK_SIZE);
  apr_file_t *file1_h;
  apr_file_t *file2_h;
  apr_file_t *file3_h;
  svn_boolean_t eof1 = FALSE;
  svn_boolean_t eof2 = FALSE;
  svn_boolean_t eof3 = FALSE;

  SVN_ERR(svn_io_file_open(&file1_h, file1, APR_READ, APR_OS_DEFAULT,
                           scratch_pool));

  err = svn_io_file_open(&file2_h, file2, APR_READ, APR_OS_DEFAULT,
                         scratch_pool);

  if (err)
    return svn_error_trace(
               svn_error_compose_create(err,
                                        svn_io_file_close(file1_h, scratch_pool)));

  err = svn_io_file_open(&file3_h, file3, APR_READ, APR_OS_DEFAULT,
                         scratch_pool);

  if (err)
      return svn_error_trace(
               svn_error_compose_create(
                    err,
                    svn_error_compose_create(svn_io_file_close(file1_h,
                                                          scratch_pool),
                                             svn_io_file_close(file2_h,
                                                          scratch_pool))));

  /* assume TRUE, until disproved below */
  *identical_p12 = *identical_p23 = *identical_p13 = TRUE;
  /* We need to read as long as no error occurs, and as long as one of the
   * flags could still change due to a read operation */
  while (!err
        && ((*identical_p12 && !eof1 && !eof2)
            || (*identical_p23 && !eof2 && !eof3)
            || (*identical_p13 && !eof1 && !eof3)))
    {
      apr_size_t bytes_read1, bytes_read2, bytes_read3;
      svn_boolean_t read_1, read_2, read_3;

      read_1 = read_2 = read_3 = FALSE;

      /* As long as a file is not at the end yet, and it is still
       * potentially identical to another file, we read the next chunk.*/
      if (!eof1 && (*identical_p12 || *identical_p13))
        {
          err = svn_io_file_read_full2(file1_h, buf1,
                                   SVN__STREAM_CHUNK_SIZE, &bytes_read1,
                                   &eof1, scratch_pool);
          if (err)
              break;
          read_1 = TRUE;
        }

      if (!eof2 && (*identical_p12 || *identical_p23))
        {
          err = svn_io_file_read_full2(file2_h, buf2,
                                   SVN__STREAM_CHUNK_SIZE, &bytes_read2,
                                   &eof2, scratch_pool);
          if (err)
              break;
          read_2 = TRUE;
        }

      if (!eof3 && (*identical_p13 || *identical_p23))
        {
          err = svn_io_file_read_full2(file3_h, buf3,
                                   SVN__STREAM_CHUNK_SIZE, &bytes_read3,
                                   &eof3, scratch_pool);
          if (err)
              break;
          read_3 = TRUE;
        }

      /* If the files are still marked identical, and at least one of them
       * is not at the end of file, we check whether they differ, and set
       * their flag to false then. */
      if (*identical_p12
          && (read_1 || read_2)
          && ((eof1 != eof2)
              || (bytes_read1 != bytes_read2)
              || memcmp(buf1, buf2, bytes_read1)))
        {
          *identical_p12 = FALSE;
        }

      if (*identical_p23
          && (read_2 || read_3)
          && ((eof2 != eof3)
              || (bytes_read2 != bytes_read3)
              || memcmp(buf2, buf3, bytes_read2)))
        {
          *identical_p23 = FALSE;
        }

      if (*identical_p13
          && (read_1 || read_3)
          && ((eof1 != eof3)
              || (bytes_read1 != bytes_read3)
              || memcmp(buf1, buf3, bytes_read3)))
        {
          *identical_p13 = FALSE;
        }
    }

  return svn_error_trace(
           svn_error_compose_create(
                err,
                svn_error_compose_create(
                    svn_io_file_close(file1_h, scratch_pool),
                    svn_error_compose_create(
                        svn_io_file_close(file2_h, scratch_pool),
                        svn_io_file_close(file3_h, scratch_pool)))));
}



svn_error_t *
svn_io_files_contents_same_p(svn_boolean_t *same,
                             const char *file1,
                             const char *file2,
                             apr_pool_t *pool)
{
  svn_boolean_t q;

  SVN_ERR(svn_io_filesizes_different_p(&q, file1, file2, pool));

  if (q)
    {
      *same = FALSE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(contents_identical_p(&q, file1, file2, pool));

  if (q)
    *same = TRUE;
  else
    *same = FALSE;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_files_contents_three_same_p(svn_boolean_t *same12,
                                   svn_boolean_t *same23,
                                   svn_boolean_t *same13,
                                   const char *file1,
                                   const char *file2,
                                   const char *file3,
                                   apr_pool_t *scratch_pool)
{
  svn_boolean_t diff_size12, diff_size23, diff_size13;

  SVN_ERR(svn_io_filesizes_three_different_p(&diff_size12,
                                             &diff_size23,
                                             &diff_size13,
                                             file1,
                                             file2,
                                             file3,
                                             scratch_pool));

  if (diff_size12 && diff_size23 && diff_size13)
    {
      *same12 = *same23 = *same13 = FALSE;
    }
  else if (diff_size12 && diff_size23)
    {
      *same12 = *same23 = FALSE;
      SVN_ERR(contents_identical_p(same13, file1, file3, scratch_pool));
    }
  else if (diff_size23 && diff_size13)
    {
      *same23 = *same13 = FALSE;
      SVN_ERR(contents_identical_p(same12, file1, file2, scratch_pool));
    }
  else if (diff_size12 && diff_size13)
    {
      *same12 = *same13 = FALSE;
      SVN_ERR(contents_identical_p(same23, file2, file3, scratch_pool));
    }
  else
    {
      SVN_ERR_ASSERT(!diff_size12 && !diff_size23 && !diff_size13);
      SVN_ERR(contents_three_identical_p(same12, same23, same13,
                                         file1, file2, file3,
                                         scratch_pool));
    }

  return SVN_NO_ERROR;
}

#ifdef WIN32
/* Counter value of file_mktemp request (used in a threadsafe way), to make
   sure that a single process normally never generates the same tempname
   twice */
static volatile apr_uint32_t tempname_counter = 0;
#endif

/* Creates a new temporary file in DIRECTORY with apr flags FLAGS.
   Set *NEW_FILE to the file handle and *NEW_FILE_NAME to its name.
   Perform temporary allocations in SCRATCH_POOL and the result in
   RESULT_POOL. */
static svn_error_t *
temp_file_create(apr_file_t **new_file,
                 const char **new_file_name,
                 const char *directory,
                 apr_int32_t flags,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
#ifndef WIN32
  const char *templ = svn_dirent_join(directory, "svn-XXXXXX", scratch_pool);
  const char *templ_apr;
  apr_status_t status;

  SVN_ERR(svn_path_cstring_from_utf8(&templ_apr, templ, scratch_pool));

  /* ### svn_path_cstring_from_utf8() guarantees to make a copy of the
         data available in POOL and we need a non-const pointer here,
         as apr changes the template to return the new filename. */
  status = apr_file_mktemp(new_file, (char *)templ_apr, flags, result_pool);

  if (status)
    return svn_error_wrap_apr(status, _("Can't create temporary file from "
                              "template '%s'"), templ);

  /* Translate the returned path back to utf-8 before returning it */
  return svn_error_trace(svn_path_cstring_to_utf8(new_file_name,
                                                  templ_apr,
                                                  result_pool));
#else
  /* The Windows implementation of apr_file_mktemp doesn't handle access
     denied errors correctly. Therefore we implement our own temp file
     creation function here. */

  /* ### Most of this is borrowed from the svn_io_open_uniquely_named(),
     ### the function we used before. But we try to guess a more unique
     ### name before trying if it exists. */

  /* Offset by some time value and a unique request nr to make the number
     +- unique for both this process and on the computer */
  int baseNr = (GetTickCount() << 11) + 7 * svn_atomic_inc(&tempname_counter)
               + GetCurrentProcessId();
  int i;

  /* ### Maybe use an iterpool? */
  for (i = 0; i <= 99999; i++)
    {
      apr_uint32_t unique_nr;
      const char *unique_name;
      const char *unique_name_apr;
      apr_file_t *try_file;
      apr_status_t apr_err;

      /* Generate a number that should be unique for this application and
         usually for the entire computer to reduce the number of cycles
         through this loop. (A bit of calculation is much cheaper than
         disk io) */
      unique_nr = baseNr + 3 * i;

      unique_name = svn_dirent_join(directory,
                                    apr_psprintf(scratch_pool, "svn-%X",
                                                 unique_nr),
                                    scratch_pool);

      SVN_ERR(cstring_from_utf8(&unique_name_apr, unique_name, scratch_pool));

      apr_err = file_open(&try_file, unique_name_apr, flags,
                          APR_OS_DEFAULT, FALSE, scratch_pool);

      if (APR_STATUS_IS_EEXIST(apr_err))
          continue;
      else if (apr_err)
        {
          /* On Win32, CreateFile fails with an "Access Denied" error
             code, rather than "File Already Exists", if the colliding
             name belongs to a directory. */

          if (APR_STATUS_IS_EACCES(apr_err))
            {
              apr_finfo_t finfo;
              apr_status_t apr_err_2 = apr_stat(&finfo, unique_name_apr,
                                                APR_FINFO_TYPE, scratch_pool);

              if (!apr_err_2 && finfo.filetype == APR_DIR)
                continue;

              if (apr_err == APR_FROM_OS_ERROR(ERROR_ACCESS_DENIED) ||
                  apr_err == APR_FROM_OS_ERROR(ERROR_SHARING_VIOLATION))
                {
                  /* The file is in use by another process or is hidden;
                     create a new name, but don't do this 99999 times in
                     case the folder is not writable */
                  i += 797;
                  continue;
                }

              /* Else fall through and return the original error. */
            }

          return svn_error_wrap_apr(apr_err, _("Can't open '%s'"),
                                    svn_dirent_local_style(unique_name,
                                                           scratch_pool));
        }
      else
        {
          /* Move file to the right pool */
          apr_err = apr_file_setaside(new_file, try_file, result_pool);

          if (apr_err)
            return svn_error_wrap_apr(apr_err, _("Can't set aside '%s'"),
                                      svn_dirent_local_style(unique_name,
                                                             scratch_pool));

          *new_file_name = apr_pstrdup(result_pool, unique_name);

          return SVN_NO_ERROR;
        }
    }

  return svn_error_createf(SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
                           NULL,
                           _("Unable to make name in '%s'"),
                           svn_dirent_local_style(directory, scratch_pool));
#endif
}

/* Wrapper for apr_file_name_get(), passing out a UTF8-encoded filename. */
svn_error_t *
svn_io_file_name_get(const char **filename,
                     apr_file_t *file,
                     apr_pool_t *pool)
{
  const char *fname_apr;
  apr_status_t status;

  status = apr_file_name_get(&fname_apr, file);
  if (status)
    return svn_error_wrap_apr(status, _("Can't get file name"));

  if (fname_apr)
    SVN_ERR(svn_path_cstring_to_utf8(filename, fname_apr, pool));
  else
    *filename = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_open_unique_file3(apr_file_t **file,
                         const char **unique_path,
                         const char *dirpath,
                         svn_io_file_del_t delete_when,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_file_t *tempfile;
  const char *tempname;
  struct temp_file_cleanup_s *baton = NULL;
  apr_int32_t flags = (APR_READ | APR_WRITE | APR_CREATE | APR_EXCL |
                       APR_BUFFERED | APR_BINARY);
#if !defined(WIN32) && !defined(__OS2__)
  apr_fileperms_t perms;
  svn_boolean_t using_system_temp_dir = FALSE;
#endif

  SVN_ERR_ASSERT(file || unique_path);
  if (file)
    *file = NULL;
  if (unique_path)
    *unique_path = NULL;

  if (dirpath == NULL)
    {
#if !defined(WIN32) && !defined(__OS2__)
      using_system_temp_dir = TRUE;
#endif
      SVN_ERR(svn_io_temp_dir(&dirpath, scratch_pool));
    }

  switch (delete_when)
    {
      case svn_io_file_del_on_pool_cleanup:
        baton = apr_palloc(result_pool, sizeof(*baton));
        baton->pool = result_pool;
        baton->fname_apr = NULL;

        /* Because cleanups are run LIFO, we need to make sure to register
           our cleanup before the apr_file_close cleanup:

           On Windows, you can't remove an open file.
        */
        apr_pool_cleanup_register(result_pool, baton,
                                  temp_file_plain_cleanup_handler,
                                  temp_file_child_cleanup_handler);

        break;
      case svn_io_file_del_on_close:
        flags |= APR_DELONCLOSE;
        break;
      default:
        break;
    }

  SVN_ERR(temp_file_create(&tempfile, &tempname, dirpath, flags,
                           result_pool, scratch_pool));

#if !defined(WIN32) && !defined(__OS2__)
  /* apr_file_mktemp() creates files with mode 0600.
   * This is appropriate if we're using a system temp dir since we don't
   * want to leak sensitive data into temp files other users can read.
   * If we're not using a system temp dir we're probably using the
   * .svn/tmp area and it's likely that the tempfile will end up being
   * copied or renamed into the working copy.
   * This would cause working files having mode 0600 while users might
   * expect to see 0644 or 0664. So we tweak perms of the tempfile in this
   * case, but only if the umask allows it. */
  if (!using_system_temp_dir)
    {
      svn_error_t *err;

      SVN_ERR(merge_default_file_perms(tempfile, &perms, dirpath,
                                       scratch_pool));
      err = file_perms_set2(tempfile, perms, scratch_pool);
      if (err)
        {
          if (APR_STATUS_IS_INCOMPLETE(err->apr_err) ||
              APR_STATUS_IS_ENOTIMPL(err->apr_err))
            svn_error_clear(err);
          else
            {
              return svn_error_quick_wrapf(
                       err, _("Can't set permissions on '%s'"),
                       svn_dirent_local_style(tempname, scratch_pool));
            }
        }
    }
#endif

  if (file)
    *file = tempfile;
  else
    SVN_ERR(svn_io_file_close(tempfile, scratch_pool));

  if (unique_path)
    *unique_path = tempname; /* Was allocated in result_pool */

  if (baton)
    SVN_ERR(cstring_from_utf8(&baton->fname_apr, tempname, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_io_file_readline(apr_file_t *file,
                     svn_stringbuf_t **stringbuf,
                     const char **eol,
                     svn_boolean_t *eof,
                     apr_size_t max_len,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *str;
  const char *eol_str;
  apr_size_t numbytes;
  char c;
  apr_size_t len;
  svn_boolean_t found_eof;

  str = svn_stringbuf_create_ensure(80, result_pool);

  /* Read bytes into STR up to and including, but not storing,
   * the next EOL sequence. */
  eol_str = NULL;
  numbytes = 1;
  len = 0;
  found_eof = FALSE;
  while (!found_eof)
    {
      if (len < max_len)
        SVN_ERR(svn_io_file_read_full2(file, &c, sizeof(c), &numbytes,
                                       &found_eof, scratch_pool));
      len++;
      if (numbytes != 1 || len > max_len)
        {
          found_eof = TRUE;
          break;
        }

      if (c == '\n')
        {
          eol_str = "\n";
        }
      else if (c == '\r')
        {
          eol_str = "\r";

          if (!found_eof && len < max_len)
            {
              apr_off_t pos;

              /* Check for "\r\n" by peeking at the next byte. */
              SVN_ERR(svn_io_file_get_offset(&pos, file, scratch_pool));
              SVN_ERR(svn_io_file_read_full2(file, &c, sizeof(c), &numbytes,
                                             &found_eof, scratch_pool));
              if (numbytes == 1 && c == '\n')
                {
                  eol_str = "\r\n";
                  len++;
                }
              else
                {
                  /* Pretend we never peeked. */
                  SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, scratch_pool));
                  found_eof = FALSE;
                  numbytes = 1;
                }
            }
        }
      else
        svn_stringbuf_appendbyte(str, c);

      if (eol_str)
        break;
    }

  if (eol)
    *eol = eol_str;
  if (eof)
    *eof = found_eof;
  *stringbuf = str;

  return SVN_NO_ERROR;
}
