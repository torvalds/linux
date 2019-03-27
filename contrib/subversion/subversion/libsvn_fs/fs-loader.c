/*
 * fs_loader.c:  Front-end to the various FS back ends
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
#include <apr.h>
#include <apr_atomic.h>
#include <apr_hash.h>
#include <apr_uuid.h>
#include <apr_strings.h>

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_ctype.h"
#include "svn_types.h"
#include "svn_dso.h"
#include "svn_version.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_sorts.h"

#include "private/svn_atomic.h"
#include "private/svn_fs_private.h"
#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "private/svn_utf_private.h"
#include "private/svn_mutex.h"
#include "private/svn_subr_private.h"

#include "fs-loader.h"

/* This is defined by configure on platforms which use configure, but
   we need to define a fallback for Windows. */
#ifndef DEFAULT_FS_TYPE
#define DEFAULT_FS_TYPE "fsfs"
#endif

#define FS_TYPE_FILENAME "fs-type"

/* If a FS backend does not implement the PATHS_CHANGED vtable function,
   it will get emulated.  However, if this macro is defined to non-null
   then the API will always be emulated when feasible, i.e. the calls
   get "re-directed" to the new API implementation. */
#ifndef SVN_FS_EMULATE_PATHS_CHANGED
#define SVN_FS_EMULATE_PATHS_CHANGED FALSE
#endif

/* If a FS backend does not implement the REPORT_CHANGES vtable function,
   it will get emulated.  However, if this macro is defined to non-null
   then the API will always be emulated when feasible, i.e. the calls
   get "re-directed" to the old API implementation. */
#ifndef SVN_FS_EMULATE_REPORT_CHANGES
#define SVN_FS_EMULATE_REPORT_CHANGES FALSE
#endif

/* A pool common to all FS objects.  See the documentation on the
   open/create functions in fs-loader.h and for svn_fs_initialize(). */
static apr_pool_t *common_pool = NULL;
static svn_mutex__t *common_pool_lock = NULL;
static svn_atomic_t common_pool_initialized = FALSE;


/* --- Utility functions for the loader --- */

struct fs_type_defn {
  const char *fs_type;
  const char *fsap_name;
  fs_init_func_t initfunc;
  void * volatile vtable; /* fs_library_vtable_t */
  struct fs_type_defn *next;
};

static struct fs_type_defn base_defn =
  {
    SVN_FS_TYPE_BDB, "base",
#ifdef SVN_LIBSVN_FS_LINKS_FS_BASE
    svn_fs_base__init,
#else
    NULL,
#endif
    NULL,
    NULL /* End of static list: this needs to be reset to NULL if the
            common_pool used when setting it has been cleared. */
  };

static struct fs_type_defn fsx_defn =
  {
    SVN_FS_TYPE_FSX, "x",
#ifdef SVN_LIBSVN_FS_LINKS_FS_X
    svn_fs_x__init,
#else
    NULL,
#endif
    NULL,
    &base_defn
  };

static struct fs_type_defn fsfs_defn =
  {
    SVN_FS_TYPE_FSFS, "fs",
#ifdef SVN_LIBSVN_FS_LINKS_FS_FS
    svn_fs_fs__init,
#else
    NULL,
#endif
    NULL,
    &fsx_defn
  };

static struct fs_type_defn *fs_modules = &fsfs_defn;


static svn_error_t *
load_module(fs_init_func_t *initfunc, const char *name, apr_pool_t *pool)
{
  *initfunc = NULL;

#if defined(SVN_USE_DSO) && APR_HAS_DSO
  {
    apr_dso_handle_t *dso;
    apr_dso_handle_sym_t symbol;
    const char *libname;
    const char *funcname;
    apr_status_t status;
    const char *p;

    /* Demand a simple alphanumeric name so that the generated DSO
       name is sensible. */
    for (p = name; *p; ++p)
      if (!svn_ctype_isalnum(*p))
        return svn_error_createf(SVN_ERR_FS_UNKNOWN_FS_TYPE, NULL,
                                 _("Invalid name for FS type '%s'"),
                                 name);

    libname = apr_psprintf(pool, "libsvn_fs_%s-" SVN_DSO_SUFFIX_FMT,
                           name, SVN_VER_MAJOR, SVN_SOVERSION);
    funcname = apr_psprintf(pool, "svn_fs_%s__init", name);

    /* Find/load the specified library.  If we get an error, assume
       the library doesn't exist.  The library will be unloaded when
       pool is destroyed. */
    SVN_ERR(svn_dso_load(&dso, libname));
    if (! dso)
      return SVN_NO_ERROR;

    /* find the initialization routine */
    status = apr_dso_sym(&symbol, dso, funcname);
    if (status)
      return svn_error_wrap_apr(status, _("'%s' does not define '%s()'"),
                                libname, funcname);

    *initfunc = (fs_init_func_t) symbol;
  }
#endif /* APR_HAS_DSO */

  return SVN_NO_ERROR;
}

/* Fetch a library vtable by a pointer into the library definitions array. */
static svn_error_t *
get_library_vtable_direct(fs_library_vtable_t **vtable,
                          struct fs_type_defn *fst,
                          apr_pool_t *pool)
{
  fs_init_func_t initfunc = NULL;
  const svn_version_t *my_version = svn_fs_version();
  const svn_version_t *fs_version;

  /* most times, we get lucky */
  *vtable = svn_atomic_casptr(&fst->vtable, NULL, NULL);
  if (*vtable)
    return SVN_NO_ERROR;

  /* o.k. the first access needs to actually load the module, find the
     vtable and check for version compatibility. */
  initfunc = fst->initfunc;
  if (! initfunc)
    SVN_ERR(load_module(&initfunc, fst->fsap_name, pool));

  if (! initfunc)
    return svn_error_createf(SVN_ERR_FS_UNKNOWN_FS_TYPE, NULL,
                             _("Failed to load module for FS type '%s'"),
                             fst->fs_type);

  {
    /* Per our API compatibility rules, we cannot ensure that
       svn_fs_initialize is called by the application.  If not, we
       cannot create the common pool and lock in a thread-safe fashion,
       nor can we clean up the common pool if libsvn_fs is dynamically
       unloaded.  This function makes a best effort by creating the
       common pool as a child of the global pool; the window of failure
       due to thread collision is small. */
    SVN_ERR(svn_fs_initialize(NULL));

    /* Invoke the FS module's initfunc function with the common
       pool protected by a lock. */
    SVN_MUTEX__WITH_LOCK(common_pool_lock,
                         initfunc(my_version, vtable, common_pool));
  }
  fs_version = (*vtable)->get_version();
  if (!svn_ver_equal(my_version, fs_version))
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Mismatched FS module version for '%s':"
                               " found %d.%d.%d%s,"
                               " expected %d.%d.%d%s"),
                             fst->fs_type,
                             my_version->major, my_version->minor,
                             my_version->patch, my_version->tag,
                             fs_version->major, fs_version->minor,
                             fs_version->patch, fs_version->tag);

  /* the vtable will not change.  Remember it */
  svn_atomic_casptr(&fst->vtable, *vtable, NULL);

  return SVN_NO_ERROR;
}

#if defined(SVN_USE_DSO) && APR_HAS_DSO
/* Return *FST for the third party FS_TYPE */
static svn_error_t *
get_or_allocate_third(struct fs_type_defn **fst,
                      const char *fs_type)
{
  while (*fst)
    {
      if (strcmp(fs_type, (*fst)->fs_type) == 0)
        return SVN_NO_ERROR;
      fst = &(*fst)->next;
    }

  *fst = apr_palloc(common_pool, sizeof(struct fs_type_defn));
  (*fst)->fs_type = apr_pstrdup(common_pool, fs_type);
  (*fst)->fsap_name = (*fst)->fs_type;
  (*fst)->initfunc = NULL;
  (*fst)->vtable = NULL;
  (*fst)->next = NULL;

  return SVN_NO_ERROR;
}
#endif

/* Fetch a library *VTABLE by FS_TYPE.
   Use POOL for temporary allocations. */
static svn_error_t *
get_library_vtable(fs_library_vtable_t **vtable, const char *fs_type,
                   apr_pool_t *pool)
{
  struct fs_type_defn **fst;
  svn_boolean_t known = FALSE;

  /* There are three FS module definitions known at compile time.  We
     want to check these without any locking overhead even when
     dynamic third party modules are enabled.  The third party modules
     cannot be checked until the lock is held.  */
  for (fst = &fs_modules; *fst; fst = &(*fst)->next)
    {
      if (strcmp(fs_type, (*fst)->fs_type) == 0)
        {
          known = TRUE;
          break;
        }
      else if (!(*fst)->next)
        {
          break;
        }
    }

#if defined(SVN_USE_DSO) && APR_HAS_DSO
  /* Third party FS modules that are unknown at compile time.

     A third party FS is identified by the file fs-type containing a
     third party name, say "foo".  The loader will load the DSO with
     the name "libsvn_fs_foo" and use the entry point with the name
     "svn_fs_foo__init".

     Note: the BDB and FSFS modules don't follow this naming scheme
     and this allows them to be used to test the third party loader.
     Change the content of fs-type to "base" in a BDB filesystem or to
     "fs" in an FSFS filesystem and they will be loaded as third party
     modules. */
  if (!known)
    {
      fst = &(*fst)->next;
      /* Best-effort init, see get_library_vtable_direct. */
      SVN_ERR(svn_fs_initialize(NULL));
      SVN_MUTEX__WITH_LOCK(common_pool_lock,
                           get_or_allocate_third(fst, fs_type));
      known = TRUE;
    }
#endif
  if (!known)
    return svn_error_createf(SVN_ERR_FS_UNKNOWN_FS_TYPE, NULL,
                             _("Unknown FS type '%s'"), fs_type);
  return get_library_vtable_direct(vtable, *fst, pool);
}

svn_error_t *
svn_fs_type(const char **fs_type, const char *path, apr_pool_t *pool)
{
  const char *filename;
  char buf[128];
  svn_error_t *err;
  apr_file_t *file;
  apr_size_t len;

  /* Read the fsap-name file to get the FSAP name, or assume the (old)
     default.  For old repositories I suppose we could check some
     other file, DB_CONFIG or strings say, but for now just check the
     directory exists. */
  filename = svn_dirent_join(path, FS_TYPE_FILENAME, pool);
  err = svn_io_file_open(&file, filename, APR_READ|APR_BUFFERED, 0, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_node_kind_t kind;
      svn_error_t *err2 = svn_io_check_path(path, &kind, pool);
      if (err2)
        {
          svn_error_clear(err2);
          return err;
        }
      if (kind == svn_node_dir)
        {
          svn_error_clear(err);
          *fs_type = SVN_FS_TYPE_BDB;
          return SVN_NO_ERROR;
        }
      return err;
    }
  else if (err)
    return err;

  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &len, pool));
  SVN_ERR(svn_io_file_close(file, pool));
  *fs_type = apr_pstrdup(pool, buf);

  return SVN_NO_ERROR;
}

/* Fetch the library vtable for an existing FS. */
static svn_error_t *
fs_library_vtable(fs_library_vtable_t **vtable, const char *path,
                  apr_pool_t *pool)
{
  const char *fs_type;

  SVN_ERR(svn_fs_type(&fs_type, path, pool));

  /* Fetch the library vtable by name, now that we've chosen one. */
  SVN_ERR(get_library_vtable(vtable, fs_type, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_fs_type(const char *path, const char *fs_type, apr_pool_t *pool)
{
  const char *filename;
  apr_file_t *file;

  filename = svn_dirent_join(path, FS_TYPE_FILENAME, pool);
  SVN_ERR(svn_io_file_open(&file, filename,
                           APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                           APR_OS_DEFAULT, pool));
  SVN_ERR(svn_io_file_write_full(file, fs_type, strlen(fs_type), NULL,
                                 pool));
  SVN_ERR(svn_io_file_write_full(file, "\n", 1, NULL, pool));
  return svn_error_trace(svn_io_file_close(file, pool));
}


/* --- Functions for operating on filesystems by pathname --- */

static apr_status_t uninit(void *data)
{
  common_pool = NULL;
  common_pool_lock = NULL;
  common_pool_initialized = 0;

  return APR_SUCCESS;
}

static svn_error_t *
synchronized_initialize(void *baton, apr_pool_t *pool)
{
  common_pool = svn_pool_create(pool);
  base_defn.next = NULL;
  SVN_ERR(svn_mutex__init(&common_pool_lock, TRUE, common_pool));

  /* ### This won't work if POOL is NULL and libsvn_fs is loaded as a DSO
     ### (via libsvn_ra_local say) since the global common_pool will live
     ### longer than the DSO, which gets unloaded when the pool used to
     ### load it is cleared, and so when the handler runs it will refer to
     ### a function that no longer exists.  libsvn_ra_local attempts to
     ### work around this by explicitly calling svn_fs_initialize. */
  apr_pool_cleanup_register(common_pool, NULL, uninit, apr_pool_cleanup_null);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_initialize(apr_pool_t *pool)
{
#if defined(SVN_USE_DSO) && APR_HAS_DSO
  /* Ensure that DSO subsystem is initialized early as possible if
     we're going to use it. */
  SVN_ERR(svn_dso_initialize2());
#endif
  /* Protect against multiple calls. */
  return svn_error_trace(svn_atomic__init_once(&common_pool_initialized,
                                               synchronized_initialize,
                                               NULL, pool));
}

/* A default warning handling function.  */
static void
default_warning_func(void *baton, svn_error_t *err)
{
  /* The one unforgiveable sin is to fail silently.  Dumping to stderr
     or /dev/tty is not acceptable default behavior for server
     processes, since those may both be equivalent to /dev/null.

     That said, be a good citizen and print something anyway, in case it goes
     somewhere, and our caller hasn't overridden the abort() call.
   */
  if (svn_error_get_malfunction_handler()
      == svn_error_abort_on_malfunction)
    /* ### TODO: extend the malfunction API such that non-abort()ing consumers
       ### also get the information on ERR. */
    svn_handle_error2(err, stderr, FALSE /* fatal */, "svn: fs-loader: ");
  SVN_ERR_MALFUNCTION_NO_RETURN();
}

svn_error_t *
svn_fs__path_valid(const char *path, apr_pool_t *pool)
{
  char *c;

  /* UTF-8 encoded string without NULs. */
  if (! svn_utf__cstring_is_valid(path))
    {
      return svn_error_createf(SVN_ERR_FS_PATH_SYNTAX, NULL,
                               _("Path '%s' is not in UTF-8"), path);
    }

  /* No "." or ".." elements. */
  if (svn_path_is_backpath_present(path)
      || svn_path_is_dotpath_present(path))
    {
      return svn_error_createf(SVN_ERR_FS_PATH_SYNTAX, NULL,
                               _("Path '%s' contains '.' or '..' element"),
                               path);
    }

  /* Raise an error if PATH contains a newline because svn:mergeinfo and
     friends can't handle them.  Issue #4340 describes a similar problem
     in the FSFS code itself.
   */
  c = strchr(path, '\n');
  if (c)
    {
      return svn_error_createf(SVN_ERR_FS_PATH_SYNTAX, NULL,
               _("Invalid control character '0x%02x' in path '%s'"),
               (unsigned char)*c, svn_path_illegal_path_escape(path, pool));
    }

  /* That's good enough. */
  return SVN_NO_ERROR;
}

/* Allocate svn_fs_t structure. */
static svn_fs_t *
fs_new(apr_hash_t *fs_config, apr_pool_t *pool)
{
  svn_fs_t *fs = apr_palloc(pool, sizeof(*fs));
  fs->pool = pool;
  fs->path = NULL;
  fs->warning = default_warning_func;
  fs->warning_baton = NULL;
  fs->config = fs_config;
  fs->access_ctx = NULL;
  fs->vtable = NULL;
  fs->fsap_data = NULL;
  fs->uuid = NULL;
  return fs;
}

svn_fs_t *
svn_fs_new(apr_hash_t *fs_config, apr_pool_t *pool)
{
  return fs_new(fs_config, pool);
}

void
svn_fs_set_warning_func(svn_fs_t *fs, svn_fs_warning_callback_t warning,
                        void *warning_baton)
{
  fs->warning = warning;
  fs->warning_baton = warning_baton;
}

svn_error_t *
svn_fs_create2(svn_fs_t **fs_p,
               const char *path,
               apr_hash_t *fs_config,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  fs_library_vtable_t *vtable;

  const char *fs_type = svn_hash__get_cstring(fs_config,
                                              SVN_FS_CONFIG_FS_TYPE,
                                              DEFAULT_FS_TYPE);
  SVN_ERR(get_library_vtable(&vtable, fs_type, scratch_pool));

  /* Create the FS directory and write out the fsap-name file. */
  SVN_ERR(svn_io_dir_make_sgid(path, APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(write_fs_type(path, fs_type, scratch_pool));

  /* Perform the actual creation. */
  *fs_p = fs_new(fs_config, result_pool);

  SVN_ERR(vtable->create(*fs_p, path, common_pool_lock, scratch_pool,
                         common_pool));
  SVN_ERR(vtable->set_svn_fs_open(*fs_p, svn_fs_open2));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_open2(svn_fs_t **fs_p, const char *path, apr_hash_t *fs_config,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR(fs_library_vtable(&vtable, path, scratch_pool));
  *fs_p = fs_new(fs_config, result_pool);
  SVN_ERR(vtable->open_fs(*fs_p, path, common_pool_lock, scratch_pool,
                          common_pool));
  SVN_ERR(vtable->set_svn_fs_open(*fs_p, svn_fs_open2));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_upgrade2(const char *path,
                svn_fs_upgrade_notify_t notify_func,
                void *notify_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  fs_library_vtable_t *vtable;
  svn_fs_t *fs;

  SVN_ERR(fs_library_vtable(&vtable, path, scratch_pool));
  fs = fs_new(NULL, scratch_pool);

  SVN_ERR(vtable->upgrade_fs(fs, path,
                             notify_func, notify_baton,
                             cancel_func, cancel_baton,
                             common_pool_lock,
                             scratch_pool, common_pool));
  return SVN_NO_ERROR;
}

/* A warning handling function that does not abort on errors,
   but just lets them be returned normally.  */
static void
verify_fs_warning_func(void *baton, svn_error_t *err)
{
}

svn_error_t *
svn_fs_verify(const char *path,
              apr_hash_t *fs_config,
              svn_revnum_t start,
              svn_revnum_t end,
              svn_fs_progress_notify_func_t notify_func,
              void *notify_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;
  svn_fs_t *fs;

  SVN_ERR(fs_library_vtable(&vtable, path, pool));
  fs = fs_new(fs_config, pool);
  svn_fs_set_warning_func(fs, verify_fs_warning_func, NULL);

  SVN_ERR(vtable->verify_fs(fs, path, start, end,
                            notify_func, notify_baton,
                            cancel_func, cancel_baton,
                            common_pool_lock,
                            pool, common_pool));
  return SVN_NO_ERROR;
}

const char *
svn_fs_path(svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup(pool, fs->path);
}

apr_hash_t *
svn_fs_config(svn_fs_t *fs, apr_pool_t *pool)
{
  if (fs->config)
    return apr_hash_copy(pool, fs->config);

  return NULL;
}

svn_error_t *
svn_fs_delete_fs(const char *path, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR(fs_library_vtable(&vtable, path, pool));
  return svn_error_trace(vtable->delete_fs(path, pool));
}

svn_error_t *
svn_fs_hotcopy3(const char *src_path, const char *dst_path,
                svn_boolean_t clean, svn_boolean_t incremental,
                svn_fs_hotcopy_notify_t notify_func,
                void *notify_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  fs_library_vtable_t *vtable;
  const char *src_fs_type;
  svn_fs_t *src_fs;
  svn_fs_t *dst_fs;
  const char *dst_fs_type;
  svn_node_kind_t dst_kind;

  if (strcmp(src_path, dst_path) == 0)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                             _("Hotcopy source and destination are equal"));

  SVN_ERR(svn_fs_type(&src_fs_type, src_path, scratch_pool));
  SVN_ERR(get_library_vtable(&vtable, src_fs_type, scratch_pool));
  src_fs = fs_new(NULL, scratch_pool);
  dst_fs = fs_new(NULL, scratch_pool);

  SVN_ERR(svn_io_check_path(dst_path, &dst_kind, scratch_pool));
  if (dst_kind == svn_node_file)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("'%s' already exists and is a file"),
                             svn_dirent_local_style(dst_path,
                                                    scratch_pool));
  if (dst_kind == svn_node_unknown)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("'%s' already exists and has an unknown "
                               "node kind"),
                             svn_dirent_local_style(dst_path,
                                                    scratch_pool));
  if (dst_kind == svn_node_dir)
    {
      svn_node_kind_t type_file_kind;

      SVN_ERR(svn_io_check_path(svn_dirent_join(dst_path,
                                                FS_TYPE_FILENAME,
                                                scratch_pool),
                                &type_file_kind, scratch_pool));
      if (type_file_kind != svn_node_none)
        {
          SVN_ERR(svn_fs_type(&dst_fs_type, dst_path, scratch_pool));
          if (strcmp(src_fs_type, dst_fs_type) != 0)
            return svn_error_createf(
                     SVN_ERR_ILLEGAL_TARGET, NULL,
                     _("The filesystem type of the hotcopy source "
                       "('%s') does not match the filesystem "
                       "type of the hotcopy destination ('%s')"),
                     src_fs_type, dst_fs_type);
        }
    }

  SVN_ERR(vtable->hotcopy(src_fs, dst_fs, src_path, dst_path, clean,
                          incremental, notify_func, notify_baton,
                          cancel_func, cancel_baton, common_pool_lock,
                          scratch_pool, common_pool));
  return svn_error_trace(write_fs_type(dst_path, src_fs_type, scratch_pool));
}

svn_error_t *
svn_fs_pack(const char *path,
            svn_fs_pack_notify_t notify_func,
            void *notify_baton,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;
  svn_fs_t *fs;

  SVN_ERR(fs_library_vtable(&vtable, path, pool));
  fs = fs_new(NULL, pool);

  SVN_ERR(vtable->pack_fs(fs, path, notify_func, notify_baton,
                          cancel_func, cancel_baton, common_pool_lock,
                          pool, common_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_recover(const char *path,
               svn_cancel_func_t cancel_func, void *cancel_baton,
               apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;
  svn_fs_t *fs;

  SVN_ERR(fs_library_vtable(&vtable, path, pool));
  fs = fs_new(NULL, pool);

  SVN_ERR(vtable->open_fs_for_recovery(fs, path, common_pool_lock,
                                       pool, common_pool));
  return svn_error_trace(vtable->recover(fs, cancel_func, cancel_baton,
                                         pool));
}

svn_error_t *
svn_fs_verify_root(svn_fs_root_t *root,
                   apr_pool_t *scratch_pool)
{
  svn_fs_t *fs = root->fs;
  SVN_ERR(fs->vtable->verify_root(root, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_freeze(svn_fs_t *fs,
              svn_fs_freeze_func_t freeze_func,
              void *freeze_baton,
              apr_pool_t *pool)
{
  SVN_ERR(fs->vtable->freeze(fs, freeze_func, freeze_baton, pool));

  return SVN_NO_ERROR;
}


/* --- Berkeley-specific functions --- */

svn_error_t *
svn_fs_create_berkeley(svn_fs_t *fs, const char *path)
{
  fs_library_vtable_t *vtable;

  SVN_ERR(get_library_vtable(&vtable, SVN_FS_TYPE_BDB, fs->pool));

  /* Create the FS directory and write out the fsap-name file. */
  SVN_ERR(svn_io_dir_make_sgid(path, APR_OS_DEFAULT, fs->pool));
  SVN_ERR(write_fs_type(path, SVN_FS_TYPE_BDB, fs->pool));

  /* Perform the actual creation. */
  SVN_ERR(vtable->create(fs, path, common_pool_lock, fs->pool, common_pool));
  SVN_ERR(vtable->set_svn_fs_open(fs, svn_fs_open2));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_open_berkeley(svn_fs_t *fs, const char *path)
{
  fs_library_vtable_t *vtable;

  SVN_ERR(fs_library_vtable(&vtable, path, fs->pool));
  SVN_ERR(vtable->open_fs(fs, path, common_pool_lock, fs->pool, common_pool));
  SVN_ERR(vtable->set_svn_fs_open(fs, svn_fs_open2));

  return SVN_NO_ERROR;
}

const char *
svn_fs_berkeley_path(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_fs_path(fs, pool);
}

svn_error_t *
svn_fs_delete_berkeley(const char *path, apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_delete_fs(path, pool));
}

svn_error_t *
svn_fs_hotcopy_berkeley(const char *src_path, const char *dest_path,
                        svn_boolean_t clean_logs, apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_hotcopy3(src_path, dest_path, clean_logs,
                                         FALSE, NULL, NULL, NULL, NULL,
                                         pool));
}

svn_error_t *
svn_fs_berkeley_recover(const char *path, apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_recover(path, NULL, NULL, pool));
}

svn_error_t *
svn_fs_set_berkeley_errcall(svn_fs_t *fs,
                            void (*handler)(const char *errpfx, char *msg))
{
  return svn_error_trace(fs->vtable->bdb_set_errcall(fs, handler));
}

svn_error_t *
svn_fs_berkeley_logfiles(apr_array_header_t **logfiles,
                         const char *path,
                         svn_boolean_t only_unused,
                         apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR(fs_library_vtable(&vtable, path, pool));
  return svn_error_trace(vtable->bdb_logfiles(logfiles, path, only_unused,
                                              pool));
}


/* --- Transaction functions --- */

svn_error_t *
svn_fs_begin_txn2(svn_fs_txn_t **txn_p, svn_fs_t *fs, svn_revnum_t rev,
                  apr_uint32_t flags, apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->begin_txn(txn_p, fs, rev, flags, pool));
}


svn_error_t *
svn_fs_commit_txn(const char **conflict_p, svn_revnum_t *new_rev,
                   svn_fs_txn_t *txn, apr_pool_t *pool)
{
  svn_error_t *err;

  *new_rev = SVN_INVALID_REVNUM;
  if (conflict_p)
    *conflict_p = NULL;

  err = txn->vtable->commit(conflict_p, new_rev, txn, pool);

#ifdef SVN_DEBUG
  /* Check postconditions. */
  if (conflict_p)
    {
      SVN_ERR_ASSERT_E(! (SVN_IS_VALID_REVNUM(*new_rev) && *conflict_p != NULL),
                       err);
      SVN_ERR_ASSERT_E((*conflict_p != NULL)
                       == (err && err->apr_err == SVN_ERR_FS_CONFLICT),
                       err);
    }
#endif

  SVN_ERR(err);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_abort_txn(svn_fs_txn_t *txn, apr_pool_t *pool)
{
  return svn_error_trace(txn->vtable->abort(txn, pool));
}

svn_error_t *
svn_fs_purge_txn(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->purge_txn(fs, txn_id, pool));
}

svn_error_t *
svn_fs_txn_name(const char **name_p, svn_fs_txn_t *txn, apr_pool_t *pool)
{
  *name_p = apr_pstrdup(pool, txn->id);
  return SVN_NO_ERROR;
}

svn_revnum_t
svn_fs_txn_base_revision(svn_fs_txn_t *txn)
{
  return txn->base_rev;
}

svn_error_t *
svn_fs_open_txn(svn_fs_txn_t **txn, svn_fs_t *fs, const char *name,
                apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->open_txn(txn, fs, name, pool));
}

svn_error_t *
svn_fs_list_transactions(apr_array_header_t **names_p, svn_fs_t *fs,
                         apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->list_transactions(names_p, fs, pool));
}

static svn_boolean_t
is_internal_txn_prop(const char *name)
{
  return strcmp(name, SVN_FS__PROP_TXN_CHECK_LOCKS) == 0 ||
         strcmp(name, SVN_FS__PROP_TXN_CHECK_OOD) == 0 ||
         strcmp(name, SVN_FS__PROP_TXN_CLIENT_DATE) == 0;
}

svn_error_t *
svn_fs_txn_prop(svn_string_t **value_p, svn_fs_txn_t *txn,
                const char *propname, apr_pool_t *pool)
{
  if (is_internal_txn_prop(propname))
    {
      *value_p = NULL;
      return SVN_NO_ERROR;
    }

  return svn_error_trace(txn->vtable->get_prop(value_p, txn, propname, pool));
}

svn_error_t *
svn_fs_txn_proplist(apr_hash_t **table_p, svn_fs_txn_t *txn, apr_pool_t *pool)
{
  SVN_ERR(txn->vtable->get_proplist(table_p, txn, pool));

  /* Don't give away internal transaction properties. */
  svn_hash_sets(*table_p, SVN_FS__PROP_TXN_CHECK_LOCKS, NULL);
  svn_hash_sets(*table_p, SVN_FS__PROP_TXN_CHECK_OOD, NULL);
  svn_hash_sets(*table_p, SVN_FS__PROP_TXN_CLIENT_DATE, NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_change_txn_prop(svn_fs_txn_t *txn, const char *name,
                       const svn_string_t *value, apr_pool_t *pool)
{
  if (is_internal_txn_prop(name))
    return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                             _("Attempt to modify internal transaction "
                               "property '%s'"), name);

  return svn_error_trace(txn->vtable->change_prop(txn, name, value, pool));
}

svn_error_t *
svn_fs_change_txn_props(svn_fs_txn_t *txn, const apr_array_header_t *props,
                        apr_pool_t *pool)
{
  int i;

  for (i = 0; i < props->nelts; ++i)
    {
      svn_prop_t *prop = &APR_ARRAY_IDX(props, i, svn_prop_t);

      if (is_internal_txn_prop(prop->name))
        return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                 _("Attempt to modify internal transaction "
                                   "property '%s'"), prop->name);
    }

  return svn_error_trace(txn->vtable->change_props(txn, props, pool));
}


/* --- Root functions --- */

svn_error_t *
svn_fs_revision_root(svn_fs_root_t **root_p, svn_fs_t *fs, svn_revnum_t rev,
                     apr_pool_t *pool)
{
  /* We create a subpool for each root object to allow us to implement
     svn_fs_close_root.  */
  apr_pool_t *subpool = svn_pool_create(pool);
  return svn_error_trace(fs->vtable->revision_root(root_p, fs, rev, subpool));
}

svn_error_t *
svn_fs_txn_root(svn_fs_root_t **root_p, svn_fs_txn_t *txn, apr_pool_t *pool)
{
  /* We create a subpool for each root object to allow us to implement
     svn_fs_close_root.  */
  apr_pool_t *subpool = svn_pool_create(pool);
  return svn_error_trace(txn->vtable->root(root_p, txn, subpool));
}

void
svn_fs_close_root(svn_fs_root_t *root)
{
  svn_pool_destroy(root->pool);
}

svn_fs_t *
svn_fs_root_fs(svn_fs_root_t *root)
{
  return root->fs;
}

svn_boolean_t
svn_fs_is_txn_root(svn_fs_root_t *root)
{
  return root->is_txn_root;
}

svn_boolean_t
svn_fs_is_revision_root(svn_fs_root_t *root)
{
  return !root->is_txn_root;
}

const char *
svn_fs_txn_root_name(svn_fs_root_t *root, apr_pool_t *pool)
{
  return root->is_txn_root ? apr_pstrdup(pool, root->txn) : NULL;
}

svn_revnum_t
svn_fs_txn_root_base_revision(svn_fs_root_t *root)
{
  return root->is_txn_root ? root->rev : SVN_INVALID_REVNUM;
}

svn_revnum_t
svn_fs_revision_root_revision(svn_fs_root_t *root)
{
  return root->is_txn_root ? SVN_INVALID_REVNUM : root->rev;
}

svn_error_t *
svn_fs_path_change_get(svn_fs_path_change3_t **change,
                       svn_fs_path_change_iterator_t *iterator)
{
  return iterator->vtable->get(change, iterator);
}

svn_error_t *
svn_fs_paths_changed2(apr_hash_t **changed_paths_p,
                      svn_fs_root_t *root,
                      apr_pool_t *pool)
{
  svn_boolean_t emulate =    !root->vtable->paths_changed
                          || SVN_FS_EMULATE_PATHS_CHANGED;

  if (emulate)
    {
      apr_pool_t *scratch_pool = svn_pool_create(pool);
      apr_hash_t *changes = svn_hash__make(pool);

      svn_fs_path_change_iterator_t *iterator;
      svn_fs_path_change3_t *change;

      SVN_ERR(svn_fs_paths_changed3(&iterator, root, scratch_pool,
                                    scratch_pool));

      SVN_ERR(svn_fs_path_change_get(&change, iterator));
      while (change)
        {
          svn_fs_path_change2_t *copy;
          const svn_fs_id_t *id_copy;
          const char *change_path = change->path.data;
          svn_fs_root_t *change_root = root;

          /* Copy CHANGE to old API struct. */
          if (change->change_kind == svn_fs_path_change_delete)
            SVN_ERR(svn_fs__get_deleted_node(&change_root, &change_path,
                                             change_root, change_path,
                                             scratch_pool, scratch_pool));

          SVN_ERR(svn_fs_node_id(&id_copy, change_root, change_path, pool));

          copy = svn_fs_path_change2_create(id_copy, change->change_kind,
                                            pool);
          copy->copyfrom_known = change->copyfrom_known;
          if (   copy->copyfrom_known
              && SVN_IS_VALID_REVNUM(change->copyfrom_rev))
            {
              copy->copyfrom_rev = change->copyfrom_rev;
              copy->copyfrom_path = apr_pstrdup(pool, change->copyfrom_path);
            }
          copy->mergeinfo_mod = change->mergeinfo_mod;
          copy->node_kind = change->node_kind;
          copy->prop_mod = change->prop_mod;
          copy->text_mod = change->text_mod;

          svn_hash_sets(changes, apr_pstrmemdup(pool, change->path.data,
                                                change->path.len), copy);

          /* Next change. */
          SVN_ERR(svn_fs_path_change_get(&change, iterator));
        }
      svn_pool_destroy(scratch_pool);

      *changed_paths_p = changes;
    }
  else
    {
      SVN_ERR(root->vtable->paths_changed(changed_paths_p, root, pool));
    }

  return SVN_NO_ERROR;
}

/* Implement svn_fs_path_change_iterator_t on top of svn_fs_paths_changed2. */

/* The iterator builds upon a hash iterator, which in turn operates on the
   full prefetched changes list. */
typedef struct fsap_iterator_data_t
{
  apr_hash_index_t *hi;

  /* For efficicency such that we don't need to dynamically allocate
     yet another copy of that data. */
  svn_fs_path_change3_t change;
} fsap_iterator_data_t;

static svn_error_t *
changes_iterator_get(svn_fs_path_change3_t **change,
                     svn_fs_path_change_iterator_t *iterator)
{
  fsap_iterator_data_t *data = iterator->fsap_data;

  if (data->hi)
    {
      const char *path = apr_hash_this_key(data->hi);
      svn_fs_path_change2_t *entry = apr_hash_this_val(data->hi);

      data->change.path.data = path;
      data->change.path.len = apr_hash_this_key_len(data->hi);
      data->change.change_kind = entry->change_kind;
      data->change.node_kind = entry->node_kind;
      data->change.text_mod = entry->text_mod;
      data->change.prop_mod = entry->prop_mod;
      data->change.mergeinfo_mod = entry->mergeinfo_mod;
      data->change.copyfrom_known = entry->copyfrom_known;
      data->change.copyfrom_rev = entry->copyfrom_rev;
      data->change.copyfrom_path = entry->copyfrom_path;

      *change = &data->change;
      data->hi = apr_hash_next(data->hi);
    }
  else
    {
      *change = NULL;
    }

  return SVN_NO_ERROR;
}

static changes_iterator_vtable_t iterator_vtable =
{
  changes_iterator_get
};

svn_error_t *
svn_fs_paths_changed3(svn_fs_path_change_iterator_t **iterator,
                      svn_fs_root_t *root,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_boolean_t emulate =    !root->vtable->report_changes
                          || (   SVN_FS_EMULATE_REPORT_CHANGES
                              && root->vtable->paths_changed);

  if (emulate)
    {
      svn_fs_path_change_iterator_t *result;
      fsap_iterator_data_t *data;

      apr_hash_t *changes;
      SVN_ERR(root->vtable->paths_changed(&changes, root, result_pool));

      data = apr_pcalloc(result_pool, sizeof(*data));
      data->hi = apr_hash_first(result_pool, changes);

      result = apr_pcalloc(result_pool, sizeof(*result));
      result->fsap_data = data;
      result->vtable = &iterator_vtable;

      *iterator = result;
    }
  else
    {
      SVN_ERR(root->vtable->report_changes(iterator, root, result_pool,
                                           scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_check_path(svn_node_kind_t *kind_p, svn_fs_root_t *root,
                  const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->check_path(kind_p, root, path, pool));
}

svn_error_t *
svn_fs_node_history2(svn_fs_history_t **history_p, svn_fs_root_t *root,
                     const char *path, apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  return svn_error_trace(root->vtable->node_history(history_p, root, path,
                                                    result_pool,
                                                    scratch_pool));
}

svn_error_t *
svn_fs_is_dir(svn_boolean_t *is_dir, svn_fs_root_t *root, const char *path,
              apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR(root->vtable->check_path(&kind, root, path, pool));
  *is_dir = (kind == svn_node_dir);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_is_file(svn_boolean_t *is_file, svn_fs_root_t *root, const char *path,
               apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR(root->vtable->check_path(&kind, root, path, pool));
  *is_file = (kind == svn_node_file);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_node_id(const svn_fs_id_t **id_p, svn_fs_root_t *root,
               const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->node_id(id_p, root, path, pool));
}

svn_error_t *
svn_fs_node_relation(svn_fs_node_relation_t *relation,
                     svn_fs_root_t *root_a, const char *path_a,
                     svn_fs_root_t *root_b, const char *path_b,
                     apr_pool_t *scratch_pool)
{
  /* Different repository types? */
  if (root_a->fs != root_b->fs)
    {
      *relation = svn_fs_node_unrelated;
      return SVN_NO_ERROR;
    }

  return svn_error_trace(root_a->vtable->node_relation(relation,
                                                       root_a, path_a,
                                                       root_b, path_b,
                                                       scratch_pool));
}

svn_error_t *
svn_fs_node_created_rev(svn_revnum_t *revision, svn_fs_root_t *root,
                        const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->node_created_rev(revision, root, path,
                                                        pool));
}

svn_error_t *
svn_fs_node_origin_rev(svn_revnum_t *revision, svn_fs_root_t *root,
                       const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->node_origin_rev(revision, root, path,
                                                       pool));
}

svn_error_t *
svn_fs_node_created_path(const char **created_path, svn_fs_root_t *root,
                         const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->node_created_path(created_path, root,
                                                         path, pool));
}

svn_error_t *
svn_fs_node_prop(svn_string_t **value_p, svn_fs_root_t *root,
                 const char *path, const char *propname, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->node_prop(value_p, root, path,
                                                 propname, pool));
}

svn_error_t *
svn_fs_node_proplist(apr_hash_t **table_p, svn_fs_root_t *root,
                     const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->node_proplist(table_p, root, path,
                                                     pool));
}

svn_error_t *
svn_fs_node_has_props(svn_boolean_t *has_props,
                      svn_fs_root_t *root,
                      const char *path,
                      apr_pool_t *scratch_pool)
{
  return svn_error_trace(root->vtable->node_has_props(has_props, root, path,
                                                      scratch_pool));
}

svn_error_t *
svn_fs_change_node_prop(svn_fs_root_t *root, const char *path,
                        const char *name, const svn_string_t *value,
                        apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->change_node_prop(root, path, name,
                                                        value, pool));
}

svn_error_t *
svn_fs_props_different(svn_boolean_t *changed_p, svn_fs_root_t *root1,
                       const char *path1, svn_fs_root_t *root2,
                       const char *path2, apr_pool_t *scratch_pool)
{
  return svn_error_trace(root1->vtable->props_changed(changed_p,
                                                      root1, path1,
                                                      root2, path2,
                                                      TRUE, scratch_pool));
}

svn_error_t *
svn_fs_props_changed(svn_boolean_t *changed_p, svn_fs_root_t *root1,
                     const char *path1, svn_fs_root_t *root2,
                     const char *path2, apr_pool_t *pool)
{
  return svn_error_trace(root1->vtable->props_changed(changed_p,
                                                      root1, path1,
                                                      root2, path2,
                                                      FALSE, pool));
}

svn_error_t *
svn_fs_copied_from(svn_revnum_t *rev_p, const char **path_p,
                   svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->copied_from(rev_p, path_p, root, path,
                                                   pool));
}

svn_error_t *
svn_fs_closest_copy(svn_fs_root_t **root_p, const char **path_p,
                    svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->closest_copy(root_p, path_p,
                                                    root, path, pool));
}

svn_error_t *
svn_fs_get_mergeinfo3(svn_fs_root_t *root,
                      const apr_array_header_t *paths,
                      svn_mergeinfo_inheritance_t inherit,
                      svn_boolean_t include_descendants,
                      svn_boolean_t adjust_inherited_mergeinfo,
                      svn_fs_mergeinfo_receiver_t receiver,
                      void *baton,
                      apr_pool_t *scratch_pool)
{
  return svn_error_trace(root->vtable->get_mergeinfo(
    root, paths, inherit, include_descendants, adjust_inherited_mergeinfo,
    receiver, baton, scratch_pool));
}

/* Baton type to be used with mergeinfo_receiver().  It provides some of
 * the parameters passed to svn_fs__get_mergeinfo_for_path. */
typedef struct mergeinfo_receiver_baton_t
{
  svn_mergeinfo_t *mergeinfo;
  apr_pool_t *result_pool;
} mergeinfo_receiver_baton_t;

static svn_error_t *
mergeinfo_receiver(const char *path,
                   svn_mergeinfo_t mergeinfo,
                   void *baton,
                   apr_pool_t *scratch_pool)
{
  mergeinfo_receiver_baton_t *b = baton;
  *b->mergeinfo = svn_mergeinfo_dup(mergeinfo, b->result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__get_mergeinfo_for_path(svn_mergeinfo_t *mergeinfo,
                               svn_fs_root_t *root,
                               const char *path,
                               svn_mergeinfo_inheritance_t inherit,
                               svn_boolean_t adjust_inherited_mergeinfo,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  apr_array_header_t *paths
    = apr_array_make(scratch_pool, 1, sizeof(const char *));

  mergeinfo_receiver_baton_t baton;
  baton.mergeinfo = mergeinfo;
  baton.result_pool = result_pool;

  APR_ARRAY_PUSH(paths, const char *) = path;

  *mergeinfo = NULL;
  SVN_ERR(svn_fs_get_mergeinfo3(root, paths,
                                inherit, FALSE /*include_descendants*/,
                                adjust_inherited_mergeinfo,
                                mergeinfo_receiver, &baton,
                                scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_merge(const char **conflict_p, svn_fs_root_t *source_root,
             const char *source_path, svn_fs_root_t *target_root,
             const char *target_path, svn_fs_root_t *ancestor_root,
             const char *ancestor_path, apr_pool_t *pool)
{
  return svn_error_trace(target_root->vtable->merge(conflict_p,
                                                    source_root, source_path,
                                                    target_root, target_path,
                                                    ancestor_root,
                                                    ancestor_path, pool));
}

svn_error_t *
svn_fs_dir_entries(apr_hash_t **entries_p, svn_fs_root_t *root,
                   const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->dir_entries(entries_p, root, path,
                                                   pool));
}

svn_error_t *
svn_fs_dir_optimal_order(apr_array_header_t **ordered_p,
                         svn_fs_root_t *root,
                         apr_hash_t *entries,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  return svn_error_trace(root->vtable->dir_optimal_order(ordered_p, root,
                                                         entries,
                                                         result_pool,
                                                         scratch_pool));
}

svn_error_t *
svn_fs_make_dir(svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  SVN_ERR(svn_fs__path_valid(path, pool));
  return svn_error_trace(root->vtable->make_dir(root, path, pool));
}

svn_error_t *
svn_fs_delete(svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->delete_node(root, path, pool));
}

svn_error_t *
svn_fs_copy(svn_fs_root_t *from_root, const char *from_path,
            svn_fs_root_t *to_root, const char *to_path, apr_pool_t *pool)
{
  SVN_ERR(svn_fs__path_valid(to_path, pool));
  return svn_error_trace(to_root->vtable->copy(from_root, from_path,
                                               to_root, to_path, pool));
}

svn_error_t *
svn_fs_revision_link(svn_fs_root_t *from_root, svn_fs_root_t *to_root,
                     const char *path, apr_pool_t *pool)
{
  return svn_error_trace(to_root->vtable->revision_link(from_root, to_root,
                                                        path, pool));
}

svn_error_t *
svn_fs_file_length(svn_filesize_t *length_p, svn_fs_root_t *root,
                   const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->file_length(length_p, root, path,
                                                   pool));
}

svn_error_t *
svn_fs_file_checksum(svn_checksum_t **checksum,
                     svn_checksum_kind_t kind,
                     svn_fs_root_t *root,
                     const char *path,
                     svn_boolean_t force,
                     apr_pool_t *pool)
{
  SVN_ERR(root->vtable->file_checksum(checksum, kind, root, path, pool));

  if (force && (*checksum == NULL || (*checksum)->kind != kind))
    {
      svn_stream_t *contents;

      SVN_ERR(svn_fs_file_contents(&contents, root, path, pool));
      SVN_ERR(svn_stream_contents_checksum(checksum, contents, kind,
                                           pool, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_file_contents(svn_stream_t **contents, svn_fs_root_t *root,
                     const char *path, apr_pool_t *pool)
{
  return svn_error_trace(root->vtable->file_contents(contents, root, path,
                                                     pool));
}

svn_error_t *
svn_fs_try_process_file_contents(svn_boolean_t *success,
                                 svn_fs_root_t *root,
                                 const char *path,
                                 svn_fs_process_contents_func_t processor,
                                 void* baton,
                                 apr_pool_t *pool)
{
  /* if the FS doesn't implement this function, report a "failed" attempt */
  if (root->vtable->try_process_file_contents == NULL)
    {
      *success = FALSE;
      return SVN_NO_ERROR;
    }

  return svn_error_trace(root->vtable->try_process_file_contents(
                         success,
                         root, path,
                         processor, baton, pool));
}

svn_error_t *
svn_fs_make_file(svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  SVN_ERR(svn_fs__path_valid(path, pool));
  return svn_error_trace(root->vtable->make_file(root, path, pool));
}

svn_error_t *
svn_fs_apply_textdelta(svn_txdelta_window_handler_t *contents_p,
                       void **contents_baton_p, svn_fs_root_t *root,
                       const char *path, const char *base_checksum,
                       const char *result_checksum, apr_pool_t *pool)
{
  svn_checksum_t *base, *result;

  /* TODO: If we ever rev this API, we should make the supplied checksums
     svn_checksum_t structs. */
  SVN_ERR(svn_checksum_parse_hex(&base, svn_checksum_md5, base_checksum,
                                 pool));
  SVN_ERR(svn_checksum_parse_hex(&result, svn_checksum_md5, result_checksum,
                                 pool));

  return svn_error_trace(root->vtable->apply_textdelta(contents_p,
                                                       contents_baton_p,
                                                       root,
                                                       path,
                                                       base,
                                                       result,
                                                       pool));
}

svn_error_t *
svn_fs_apply_text(svn_stream_t **contents_p, svn_fs_root_t *root,
                  const char *path, const char *result_checksum,
                  apr_pool_t *pool)
{
  svn_checksum_t *result;

  /* TODO: If we ever rev this API, we should make the supplied checksum an
     svn_checksum_t struct. */
  SVN_ERR(svn_checksum_parse_hex(&result, svn_checksum_md5, result_checksum,
                                 pool));

  return svn_error_trace(root->vtable->apply_text(contents_p, root, path,
                                                  result, pool));
}

svn_error_t *
svn_fs_contents_different(svn_boolean_t *changed_p, svn_fs_root_t *root1,
                          const char *path1, svn_fs_root_t *root2,
                          const char *path2, apr_pool_t *scratch_pool)
{
  return svn_error_trace(root1->vtable->contents_changed(changed_p,
                                                         root1, path1,
                                                         root2, path2,
                                                         TRUE,
                                                         scratch_pool));
}

svn_error_t *
svn_fs_contents_changed(svn_boolean_t *changed_p, svn_fs_root_t *root1,
                        const char *path1, svn_fs_root_t *root2,
                        const char *path2, apr_pool_t *pool)
{
  return svn_error_trace(root1->vtable->contents_changed(changed_p,
                                                         root1, path1,
                                                         root2, path2,
                                                         FALSE, pool));
}

svn_error_t *
svn_fs_youngest_rev(svn_revnum_t *youngest_p, svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->youngest_rev(youngest_p, fs, pool));
}

svn_error_t *
svn_fs_info_format(int *fs_format,
                   svn_version_t **supports_version,
                   svn_fs_t *fs,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  return svn_error_trace(fs->vtable->info_format(fs_format, supports_version,
                                                 fs,
                                                 result_pool, scratch_pool));
}

svn_error_t *
svn_fs_info_config_files(apr_array_header_t **files,
                         svn_fs_t *fs,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  return svn_error_trace(fs->vtable->info_config_files(files, fs,
                                                       result_pool,
                                                       scratch_pool));
}

svn_error_t *
svn_fs_deltify_revision(svn_fs_t *fs, svn_revnum_t revision, apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->deltify(fs, revision, pool));
}

svn_error_t *
svn_fs_refresh_revision_props(svn_fs_t *fs,
                              apr_pool_t *scratch_pool)
{
  return svn_error_trace(fs->vtable->refresh_revprops(fs, scratch_pool));
}

svn_error_t *
svn_fs_revision_prop2(svn_string_t **value_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      const char *propname,
                      svn_boolean_t refresh,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  return svn_error_trace(fs->vtable->revision_prop(value_p, fs, rev,
                                                   propname, refresh,
                                                   result_pool,
                                                   scratch_pool));
}

svn_error_t *
svn_fs_revision_proplist2(apr_hash_t **table_p,
                          svn_fs_t *fs,
                          svn_revnum_t rev,
                          svn_boolean_t refresh,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(fs->vtable->revision_proplist(table_p, fs, rev,
                                                       refresh,
                                                       result_pool,
                                                       scratch_pool));
}

svn_error_t *
svn_fs_change_rev_prop2(svn_fs_t *fs, svn_revnum_t rev, const char *name,
                        const svn_string_t *const *old_value_p,
                        const svn_string_t *value, apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->change_rev_prop(fs, rev, name,
                                                     old_value_p,
                                                     value, pool));
}

svn_error_t *
svn_fs_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                             svn_fs_root_t *source_root,
                             const char *source_path,
                             svn_fs_root_t *target_root,
                             const char *target_path, apr_pool_t *pool)
{
  return svn_error_trace(target_root->vtable->get_file_delta_stream(
                           stream_p,
                           source_root, source_path,
                           target_root, target_path, pool));
}

svn_error_t *
svn_fs__get_deleted_node(svn_fs_root_t **node_root,
                         const char **node_path,
                         svn_fs_root_t *root,
                         const char *path,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  const char *parent_path, *name;
  svn_fs_root_t *copy_root;
  const char *copy_path;

  /* History traversal does not work with transaction roots.
   * Therefore, do it "by hand". */

  /* If the parent got copied in ROOT, PATH got copied with it.
   * Otherwise, we will find the node at PATH in the revision prior to ROOT.
   */
  svn_fspath__split(&parent_path, &name, path, scratch_pool);
  SVN_ERR(svn_fs_closest_copy(&copy_root, &copy_path, root, parent_path,
                              scratch_pool));

  /* Copied in ROOT? */
  if (   copy_root
      && (   svn_fs_revision_root_revision(copy_root)
          == svn_fs_revision_root_revision(root)))
    {
      svn_revnum_t copyfrom_rev;
      const char *copyfrom_path;
      const char *rel_path;
      SVN_ERR(svn_fs_copied_from(&copyfrom_rev, &copyfrom_path,
                                 copy_root, copy_path, scratch_pool));

      SVN_ERR(svn_fs_revision_root(node_root, svn_fs_root_fs(root),
                                   copyfrom_rev, result_pool));
      rel_path = svn_fspath__skip_ancestor(copy_path, path);
      *node_path = svn_fspath__join(copyfrom_path, rel_path, result_pool);
    }
  else
    {
      svn_revnum_t revision;
      svn_revnum_t previous_rev;

      /* Determine the latest revision before ROOT. */
      revision = svn_fs_revision_root_revision(root);
      if (SVN_IS_VALID_REVNUM(revision))
        previous_rev = revision - 1;
      else
        previous_rev = svn_fs_txn_root_base_revision(root);

      SVN_ERR(svn_fs_revision_root(node_root, svn_fs_root_fs(root),
                                   previous_rev, result_pool));
      *node_path = apr_pstrdup(result_pool, path);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_get_uuid(svn_fs_t *fs, const char **uuid, apr_pool_t *pool)
{
  /* If you change this, consider changing svn_fs__identifier(). */
  *uuid = apr_pstrdup(pool, fs->uuid);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_set_uuid(svn_fs_t *fs, const char *uuid, apr_pool_t *pool)
{
  if (! uuid)
    {
      uuid = svn_uuid_generate(pool);
    }
  else
    {
      apr_uuid_t parsed_uuid;
      apr_status_t apr_err = apr_uuid_parse(&parsed_uuid, uuid);
      if (apr_err)
        return svn_error_createf(SVN_ERR_BAD_UUID, NULL,
                                 _("Malformed UUID '%s'"), uuid);
    }
  return svn_error_trace(fs->vtable->set_uuid(fs, uuid, pool));
}

svn_error_t *
svn_fs_lock_many(svn_fs_t *fs,
                 apr_hash_t *targets,
                 const char *comment,
                 svn_boolean_t is_dav_comment,
                 apr_time_t expiration_date,
                 svn_boolean_t steal_lock,
                 svn_fs_lock_callback_t lock_callback,
                 void *lock_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *ok_targets = apr_hash_make(scratch_pool);
  svn_error_t *err, *cb_err = SVN_NO_ERROR;

  /* Enforce that the comment be xml-escapable. */
  if (comment)
    if (! svn_xml_is_xml_safe(comment, strlen(comment)))
      return svn_error_create(SVN_ERR_XML_UNESCAPABLE_DATA, NULL,
                              _("Lock comment contains illegal characters"));

  if (expiration_date < 0)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
              _("Negative expiration date passed to svn_fs_lock"));

  /* Enforce that the token be an XML-safe URI. */
  for (hi = apr_hash_first(scratch_pool, targets); hi; hi = apr_hash_next(hi))
    {
      const svn_fs_lock_target_t *target = apr_hash_this_val(hi);

      err = SVN_NO_ERROR;
      if (target->token)
        {
          const char *c;


          if (strncmp(target->token, "opaquelocktoken:", 16))
            err = svn_error_createf(SVN_ERR_FS_BAD_LOCK_TOKEN, NULL,
                                    _("Lock token URI '%s' has bad scheme; "
                                      "expected '%s'"),
                                    target->token, "opaquelocktoken");

          if (!err)
            {
              for (c = target->token; *c && !err; c++)
                if (! svn_ctype_isascii(*c) || svn_ctype_iscntrl(*c))
                  err = svn_error_createf(
                          SVN_ERR_FS_BAD_LOCK_TOKEN, NULL,
                          _("Lock token '%s' is not ASCII or is a "
                            "control character at byte %u"),
                          target->token,
                          (unsigned)(c - target->token));

              /* strlen(token) == c - token. */
              if (!err && !svn_xml_is_xml_safe(target->token,
                                               c - target->token))
                err = svn_error_createf(
                            SVN_ERR_FS_BAD_LOCK_TOKEN, NULL,
                            _("Lock token URI '%s' is not XML-safe"),
                            target->token);
            }
        }

      if (err)
        {
          if (!cb_err && lock_callback)
            cb_err = lock_callback(lock_baton, apr_hash_this_key(hi),
                                   NULL, err, scratch_pool);
          svn_error_clear(err);
        }
      else
        svn_hash_sets(ok_targets, apr_hash_this_key(hi), target);
    }

  if (!apr_hash_count(ok_targets))
    return svn_error_trace(cb_err);

  err = fs->vtable->lock(fs, ok_targets, comment, is_dav_comment,
                         expiration_date, steal_lock,
                         lock_callback, lock_baton,
                         result_pool, scratch_pool);

  if (err && cb_err)
    svn_error_compose(err, cb_err);
  else if (!err)
    err = cb_err;

  return svn_error_trace(err);
}

struct lock_baton_t {
  const svn_lock_t *lock;
  svn_error_t *fs_err;
};

/* Implements svn_fs_lock_callback_t. Used by svn_fs_lock and
   svn_fs_unlock to record the lock and error from svn_fs_lock_many
   and svn_fs_unlock_many. */
static svn_error_t *
lock_cb(void *lock_baton,
        const char *path,
        const svn_lock_t *lock,
        svn_error_t *fs_err,
        apr_pool_t *pool)
{
  struct lock_baton_t *b = lock_baton;

  b->lock = lock;
  b->fs_err = svn_error_dup(fs_err);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_lock(svn_lock_t **lock, svn_fs_t *fs, const char *path,
            const char *token, const char *comment,
            svn_boolean_t is_dav_comment, apr_time_t expiration_date,
            svn_revnum_t current_rev, svn_boolean_t steal_lock,
            apr_pool_t *pool)
{
  apr_hash_t *targets = apr_hash_make(pool);
  svn_fs_lock_target_t target;
  svn_error_t *err;
  struct lock_baton_t baton = {0};

  target.token = token;
  target.current_rev = current_rev;
  svn_hash_sets(targets, path, &target);

  err = svn_fs_lock_many(fs, targets, comment, is_dav_comment,
                         expiration_date, steal_lock, lock_cb, &baton,
                         pool, pool);

  if (baton.lock)
    *lock = (svn_lock_t*)baton.lock;

  if (err && baton.fs_err)
    svn_error_compose(err, baton.fs_err);
  else if (!err)
    err = baton.fs_err;

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_generate_lock_token(const char **token, svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->generate_lock_token(token, fs, pool));
}

svn_fs_lock_target_t *
svn_fs_lock_target_create(const char *token,
                          svn_revnum_t current_rev,
                          apr_pool_t *result_pool)
{
  svn_fs_lock_target_t *target = apr_palloc(result_pool, sizeof(*target));

  target->token = token;
  target->current_rev = current_rev;

  return target;
}

void
svn_fs_lock_target_set_token(svn_fs_lock_target_t *target,
                             const char *token)
{
  target->token = token;
}

svn_error_t *
svn_fs_unlock_many(svn_fs_t *fs,
                   apr_hash_t *targets,
                   svn_boolean_t break_lock,
                   svn_fs_lock_callback_t lock_callback,
                   void *lock_baton,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  return svn_error_trace(fs->vtable->unlock(fs, targets, break_lock,
                                            lock_callback, lock_baton,
                                            result_pool, scratch_pool));
}

svn_error_t *
svn_fs_unlock(svn_fs_t *fs, const char *path, const char *token,
              svn_boolean_t break_lock, apr_pool_t *pool)
{
  apr_hash_t *targets = apr_hash_make(pool);
  svn_error_t *err;
  struct lock_baton_t baton = {0};

  if (!token)
    token = "";
  svn_hash_sets(targets, path, token);

  err = svn_fs_unlock_many(fs, targets, break_lock, lock_cb, &baton,
                           pool, pool);

  if (err && baton.fs_err)
    svn_error_compose(err, baton.fs_err);
  else if (!err)
    err = baton.fs_err;

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_get_lock(svn_lock_t **lock, svn_fs_t *fs, const char *path,
                apr_pool_t *pool)
{
  return svn_error_trace(fs->vtable->get_lock(lock, fs, path, pool));
}

svn_error_t *
svn_fs_get_locks2(svn_fs_t *fs, const char *path, svn_depth_t depth,
                  svn_fs_get_locks_callback_t get_locks_func,
                  void *get_locks_baton, apr_pool_t *pool)
{
  SVN_ERR_ASSERT((depth == svn_depth_empty) ||
                 (depth == svn_depth_files) ||
                 (depth == svn_depth_immediates) ||
                 (depth == svn_depth_infinity));
  return svn_error_trace(fs->vtable->get_locks(fs, path, depth,
                                               get_locks_func,
                                               get_locks_baton, pool));
}


/* --- History functions --- */

svn_error_t *
svn_fs_history_prev2(svn_fs_history_t **prev_history_p,
                     svn_fs_history_t *history, svn_boolean_t cross_copies,
                     apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  return svn_error_trace(history->vtable->prev(prev_history_p, history,
                                               cross_copies, result_pool,
                                               scratch_pool));
}

svn_error_t *
svn_fs_history_location(const char **path, svn_revnum_t *revision,
                        svn_fs_history_t *history, apr_pool_t *pool)
{
  return svn_error_trace(history->vtable->location(path, revision, history,
                                                   pool));
}


/* --- Node-ID functions --- */

svn_fs_id_t *
svn_fs_parse_id(const char *data, apr_size_t len, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;
  svn_error_t *err;

  err = get_library_vtable(&vtable, SVN_FS_TYPE_BDB, pool);
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }
  return vtable->parse_id(data, len, pool);
}

svn_string_t *
svn_fs_unparse_id(const svn_fs_id_t *id, apr_pool_t *pool)
{
  return id->vtable->unparse(id, pool);
}

svn_boolean_t
svn_fs_check_related(const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  return (a->vtable->compare(a, b) != svn_fs_node_unrelated);
}

int
svn_fs_compare_ids(const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  switch (a->vtable->compare(a, b))
    {
    case svn_fs_node_unchanged:
      return 0;
    case svn_fs_node_common_ancestor:
      return 1;
    default:
      return -1;
    }
}

svn_error_t *
svn_fs_print_modules(svn_stringbuf_t *output,
                     apr_pool_t *pool)
{
  struct fs_type_defn *defn = fs_modules;
  fs_library_vtable_t *vtable;
  apr_pool_t *iterpool = svn_pool_create(pool);

  while (defn)
    {
      char *line;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      err = get_library_vtable_direct(&vtable, defn, iterpool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_FS_UNKNOWN_FS_TYPE)
            {
              svn_error_clear(err);
              defn = defn->next;
              continue;
            }
          else
            return err;
        }

      line = apr_psprintf(iterpool, "* fs_%s : %s\n",
                          defn->fsap_name, vtable->get_description());
      svn_stringbuf_appendcstr(output, line);
      defn = defn->next;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_fs_path_change2_t *
svn_fs_path_change2_create(const svn_fs_id_t *node_rev_id,
                           svn_fs_path_change_kind_t change_kind,
                           apr_pool_t *pool)
{
  return svn_fs__path_change_create_internal(node_rev_id, change_kind, pool);
}

svn_fs_path_change3_t *
svn_fs_path_change3_create(svn_fs_path_change_kind_t change_kind,
                           apr_pool_t *result_pool)
{
  return svn_fs__path_change_create_internal2(change_kind, result_pool);
}

svn_fs_path_change3_t *
svn_fs_path_change3_dup(svn_fs_path_change3_t *change,
                        apr_pool_t *result_pool)
{
  svn_fs_path_change3_t *copy = apr_pmemdup(result_pool, change,
                                            sizeof(*copy));

  copy->path.data = apr_pstrmemdup(result_pool, copy->path.data,
                                   copy->path.len);
  if (copy->copyfrom_path)
    copy->copyfrom_path = apr_pstrdup(result_pool, change->copyfrom_path);

  return copy;
}

/* Return the library version number. */
const svn_version_t *
svn_fs_version(void)
{
  SVN_VERSION_BODY;
}


/** info **/
svn_error_t *
svn_fs_info(const svn_fs_info_placeholder_t **info_p,
            svn_fs_t *fs,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  if (fs->vtable->info_fsap)
    {
      SVN_ERR(fs->vtable->info_fsap((const void **)info_p, fs,
                                    result_pool, scratch_pool));
    }
  else
    {
      svn_fs_info_placeholder_t *info = apr_palloc(result_pool, sizeof(*info));
      /* ### Ask the disk(!), since svn_fs_t doesn't cache the answer. */
      SVN_ERR(svn_fs_type(&info->fs_type, fs->path, result_pool));
      *info_p = info;
    }
  return SVN_NO_ERROR;
}

void *
svn_fs_info_dup(const void *info_void,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  const svn_fs_info_placeholder_t *info = info_void;
  fs_library_vtable_t *vtable;

  SVN_ERR(get_library_vtable(&vtable, info->fs_type, scratch_pool));

  if (vtable->info_fsap_dup)
    return vtable->info_fsap_dup(info_void, result_pool);
  else
    return apr_pmemdup(result_pool, info, sizeof(*info));
}

