/* env.h : managing the BDB environment
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

#include <assert.h>

#include <apr.h>
#if APR_HAS_THREADS
#include <apr_thread_proc.h>
#include <apr_time.h>
#endif

#include <apr_strings.h>
#include <apr_hash.h>

#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_utf.h"
#include "private/svn_atomic.h"
#include "private/svn_mutex.h"

#include "bdb-err.h"
#include "bdb_compat.h"

#include "env.h"

/* A note about the BDB environment descriptor cache.

   With the advent of DB_REGISTER in BDB-4.4, a process may only open
   an environment handle once.  This means that we must maintain a
   cache of open environment handles, with reference counts.  We
   allocate each environment descriptor (a bdb_env_t) from its own
   pool.  The cache itself (and the cache pool) are shared between
   threads, so all direct or indirect access to the pool is serialized
   with a global mutex.

   Because several threads can now use the same DB_ENV handle, we must
   use the DB_THREAD flag when opening the environments, otherwise the
   env handles (and all of libsvn_fs_base) won't be thread-safe.

   If we use DB_THREAD, however, all of the code that reads data from
   the database without a cursor must use either DB_DBT_MALLOC,
   DB_DBT_REALLOC, or DB_DBT_USERMEM, as described in the BDB
   documentation.

   (Oh, yes -- using DB_THREAD might not work on some systems. But
   then, it's quite probable that threading is seriously broken on
   those systems anyway, so we'll rely on APR_HAS_THREADS.)
*/


/* The cache key for a Berkeley DB environment descriptor.  This is a
   combination of the device ID and INODE number of the Berkeley DB
   config file.

   XXX FIXME: Although the dev+inode combination is supposed do be
   unique, apparently that's not always the case with some remote
   filesystems.  We /should/ be safe using this as a unique hash key,
   because the database must be on a local filesystem.  We can hope,
   anyway. */
typedef struct bdb_env_key_t
{
  apr_dev_t device;
  apr_ino_t inode;
} bdb_env_key_t;

/* The cached Berkeley DB environment descriptor. */
struct bdb_env_t
{
  /**************************************************************************/
  /* Error Reporting */

  /* A (char *) casted pointer to this structure is passed to BDB's
     set_errpfx(), which treats it as a NUL-terminated character
     string to prefix all BDB error messages.  However, svn also
     registers bdb_error_gatherer() as an error handler with
     set_errcall() which turns off BDB's default printing of errors to
     stderr and anytime thereafter when BDB reports an error and
     before the BDB function returns, it calls bdb_error_gatherer()
     and passes the same error prefix (char *) pointer given to
     set_errpfx().  The bdb_error_gatherer() callback casts the
     (char *) it back to a (bdb_env_t *).

     To avoid problems should BDB ever try to interpret our baton as a
     string, the first field in the structure is a char
     errpfx_string[].  Initializers of this structure must strcpy the
     value of BDB_ERRPFX_STRING into this array.  */
  char errpfx_string[sizeof(BDB_ERRPFX_STRING)];

  /* Extended error information. */
#if APR_HAS_THREADS
  apr_threadkey_t *error_info;   /* Points to a bdb_error_info_t. */
#else
  bdb_error_info_t error_info;
#endif

  /**************************************************************************/
  /* BDB Environment Cache */

  /* The Berkeley DB environment. */
  DB_ENV *env;

  /* The flags with which this environment was opened.  Reopening the
     environment with a different set of flags is not allowed.  Trying
     to change the state of the DB_PRIVATE flag is an especially bad
     idea, so svn_fs_bdb__open() forbids any flag changes. */
  u_int32_t flags;

  /* The home path of this environment; a canonical SVN path encoded in
     UTF-8 and allocated from this decriptor's pool. */
  const char *path;

  /* The home path of this environment, in the form expected by BDB. */
  const char *path_bdb;

  /* The reference count for this environment handle; this is
     essentially the difference between the number of calls to
     svn_fs_bdb__open and svn_fs_bdb__close. */
  unsigned refcount;

  /* If this flag is TRUE, someone has detected that the environment
     descriptor is in a panicked state and should be removed from the
     cache.

     Note 1: Once this flag is set, it must not be cleared again.

     Note 2: Unlike other fields in this structure, this field is not
             protected by the cache mutex on threaded platforms, and
             should only be accesses via the svn_atomic functions. */
  volatile svn_atomic_t panic;

  /* The key for the environment descriptor cache. */
  bdb_env_key_t key;

  /* The handle of the open DB_CONFIG file.

     We keep the DB_CONFIG file open in this process as long as the
     environment handle itself is open.  On Windows, this guarantees
     that the cache key remains unique; here's what the Windows SDK
     docs have to say about the file index (interpreted as the INODE
     number by APR):

        "This value is useful only while the file is open by at least
        one process.  If no processes have it open, the index may
        change the next time the file is opened."

     Now, we certainly don't want a unique key to change while it's
     being used, do we... */
  apr_file_t *dbconfig_file;

  /* The pool associated with this environment descriptor.

     Because the descriptor has a life of its own, the structure and
     any data associated with it are allocated from their own global
     pool. */
  apr_pool_t *pool;

};


#if APR_HAS_THREADS
/* Get the thread-specific error info from a bdb_env_t. */
static bdb_error_info_t *
get_error_info(const bdb_env_t *bdb)
{
  void *priv;
  apr_threadkey_private_get(&priv, bdb->error_info);
  if (!priv)
    {
      priv = calloc(1, sizeof(bdb_error_info_t));
      apr_threadkey_private_set(priv, bdb->error_info);
    }
  return priv;
}
#else
#define get_error_info(bdb) (&(bdb)->error_info)
#endif /* APR_HAS_THREADS */


/* Convert a BDB error to a Subversion error. */
static svn_error_t *
convert_bdb_error(bdb_env_t *bdb, int db_err)
{
  if (db_err)
    {
      bdb_env_baton_t bdb_baton;
      bdb_baton.env = bdb->env;
      bdb_baton.bdb = bdb;
      bdb_baton.error_info = get_error_info(bdb);
      SVN_BDB_ERR(&bdb_baton, db_err);
    }
  return SVN_NO_ERROR;
}


/* Allocating an appropriate Berkeley DB environment object.  */

/* BDB error callback.  See bdb_error_info_t in env.h for more info.
   Note: bdb_error_gatherer is a macro with BDB < 4.3, so be careful how
   you use it! */
static void
bdb_error_gatherer(const DB_ENV *dbenv, const char *baton, const char *msg)
{
  /* See the documentation at bdb_env_t's definition why the
     (bdb_env_t *) cast is safe and why it is done. */
  bdb_error_info_t *error_info = get_error_info((const bdb_env_t *) baton);
  svn_error_t *new_err;

  SVN_BDB_ERROR_GATHERER_IGNORE(dbenv);

  new_err = svn_error_createf(SVN_ERR_FS_BERKELEY_DB, NULL, "bdb: %s", msg);
  if (error_info->pending_errors)
    svn_error_compose(error_info->pending_errors, new_err);
  else
    error_info->pending_errors = new_err;

  if (error_info->user_callback)
    error_info->user_callback(NULL, (char *)msg); /* ### I hate this cast... */
}


/* Pool cleanup for the cached environment descriptor. */
static apr_status_t
cleanup_env(void *data)
{
  bdb_env_t *bdb = data;
  bdb->pool = NULL;
  bdb->dbconfig_file = NULL;   /* will be closed during pool destruction */
#if APR_HAS_THREADS
  apr_threadkey_private_delete(bdb->error_info);
#endif /* APR_HAS_THREADS */

  /* If there are no references to this descriptor, free its memory here,
     so that we don't leak it if create_env returns an error.
     See bdb_close, which takes care of freeing this memory if the
     environment is still open when the cache is destroyed. */
  if (!bdb->refcount)
    free(data);

  return APR_SUCCESS;
}

#if APR_HAS_THREADS
/* This cleanup is the fall back plan.  If the thread exits and the
   environment hasn't been closed it's responsible for cleanup of the
   thread local error info variable, which would otherwise be leaked.
   Normally it will not be called, because svn_fs_bdb__close will
   set the thread's error info to NULL after cleaning it up. */
static void
cleanup_error_info(void *baton)
{
  bdb_error_info_t *error_info = baton;

  if (error_info)
    svn_error_clear(error_info->pending_errors);

  free(error_info);
}
#endif /* APR_HAS_THREADS */

/* Create a Berkeley DB environment. */
static svn_error_t *
create_env(bdb_env_t **bdbp, const char *path, apr_pool_t *pool)
{
  int db_err;
  bdb_env_t *bdb;
  const char *path_bdb;
  char *tmp_path, *tmp_path_bdb;
  apr_size_t path_size, path_bdb_size;

#if SVN_BDB_PATH_UTF8
  path_bdb = svn_dirent_local_style(path, pool);
#else
  SVN_ERR(svn_utf_cstring_from_utf8(&path_bdb,
                                    svn_dirent_local_style(path, pool),
                                    pool));
#endif

  /* Allocate the whole structure, including strings, from the heap,
     because it must survive the cache pool cleanup. */
  path_size = strlen(path) + 1;
  path_bdb_size = strlen(path_bdb) + 1;
  /* Using calloc() to ensure the padding bytes in bdb->key (which is used as
   * a hash key) are zeroed. */
  bdb = calloc(1, sizeof(*bdb) + path_size + path_bdb_size);

  /* We must initialize this now, as our callers may assume their bdb
     pointer is valid when checking for errors.  */
  apr_pool_cleanup_register(pool, bdb, cleanup_env, apr_pool_cleanup_null);
  apr_cpystrn(bdb->errpfx_string, BDB_ERRPFX_STRING,
              sizeof(bdb->errpfx_string));
  bdb->path = tmp_path = (char*)(bdb + 1);
  bdb->path_bdb = tmp_path_bdb = tmp_path + path_size;
  apr_cpystrn(tmp_path, path, path_size);
  apr_cpystrn(tmp_path_bdb, path_bdb, path_bdb_size);
  bdb->pool = pool;
  *bdbp = bdb;

#if APR_HAS_THREADS
  {
    apr_status_t apr_err = apr_threadkey_private_create(&bdb->error_info,
                                                        cleanup_error_info,
                                                        pool);
    if (apr_err)
      return svn_error_create(apr_err, NULL,
                              "Can't allocate thread-specific storage"
                              " for the Berkeley DB environment descriptor");
  }
#endif /* APR_HAS_THREADS */

  db_err = db_env_create(&(bdb->env), 0);
  if (!db_err)
    {
      /* See the documentation at bdb_env_t's definition why the
         (char *) cast is safe and why it is done. */
      bdb->env->set_errpfx(bdb->env, (char *) bdb);

      /* bdb_error_gatherer is in parens to stop macro expansion. */
      bdb->env->set_errcall(bdb->env, (bdb_error_gatherer));

      /* Needed on Windows in case Subversion and Berkeley DB are using
         different C runtime libraries  */
      db_err = bdb->env->set_alloc(bdb->env, malloc, realloc, free);

      /* If we detect a deadlock, select a transaction to abort at
         random from those participating in the deadlock.  */
      if (!db_err)
        db_err = bdb->env->set_lk_detect(bdb->env, DB_LOCK_RANDOM);
    }
  return convert_bdb_error(bdb, db_err);
}



/* The environment descriptor cache. */

/* The global pool used for this cache. */
static apr_pool_t *bdb_cache_pool = NULL;

/* The cache.  The items are bdb_env_t structures. */
static apr_hash_t *bdb_cache = NULL;

/* The mutex that protects bdb_cache. */
static svn_mutex__t *bdb_cache_lock = NULL;

/* Cleanup callback to NULL out the cache, so we don't try to use it after
   the pool has been cleared during global shutdown. */
static apr_status_t
clear_cache(void *data)
{
  bdb_cache = NULL;
  bdb_cache_lock = NULL;
  return APR_SUCCESS;
}

static volatile svn_atomic_t bdb_cache_state = 0;

static svn_error_t *
bdb_init_cb(void *baton, apr_pool_t *pool)
{
  bdb_cache_pool = svn_pool_create(pool);
  bdb_cache = apr_hash_make(bdb_cache_pool);

  SVN_ERR(svn_mutex__init(&bdb_cache_lock, TRUE, bdb_cache_pool));
  apr_pool_cleanup_register(bdb_cache_pool, NULL, clear_cache,
                            apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_bdb__init(apr_pool_t* pool)
{
  return svn_atomic__init_once(&bdb_cache_state, bdb_init_cb, NULL, pool);
}

/* Construct a cache key for the BDB environment at PATH in *KEYP.
   if DBCONFIG_FILE is not NULL, return the opened file handle.
   Allocate from POOL. */
static svn_error_t *
bdb_cache_key(bdb_env_key_t *keyp, apr_file_t **dbconfig_file,
              const char *path, apr_pool_t *pool)
{
  const char *dbcfg_file_name = svn_dirent_join(path, BDB_CONFIG_FILE, pool);
  apr_file_t *dbcfg_file;
  apr_status_t apr_err;
  apr_finfo_t finfo;

  SVN_ERR(svn_io_file_open(&dbcfg_file, dbcfg_file_name,
                           APR_READ, APR_OS_DEFAULT, pool));

  apr_err = apr_file_info_get(&finfo, APR_FINFO_DEV | APR_FINFO_INODE,
                              dbcfg_file);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err, "Can't create BDB environment cache key");

  /* Make sure that any padding in the key is always cleared, so that
     the key's hash deterministic. */
  memset(keyp, 0, sizeof *keyp);
  keyp->device = finfo.device;
  keyp->inode = finfo.inode;

  if (dbconfig_file)
    *dbconfig_file = dbcfg_file;
  else
    apr_file_close(dbcfg_file);

  return SVN_NO_ERROR;
}


/* Find a BDB environment in the cache.
   Return the environment's panic state in *PANICP.

   Note: You MUST acquire the cache mutex before calling this function.
*/
static bdb_env_t *
bdb_cache_get(const bdb_env_key_t *keyp, svn_boolean_t *panicp)
{
  bdb_env_t *bdb = apr_hash_get(bdb_cache, keyp, sizeof *keyp);
  if (bdb && bdb->env)
    {
      *panicp = !!svn_atomic_read(&bdb->panic);
#if SVN_BDB_VERSION_AT_LEAST(4,2)
      if (!*panicp)
        {
          u_int32_t flags;
          if (bdb->env->get_flags(bdb->env, &flags)
              || (flags & DB_PANIC_ENVIRONMENT))
            {
              /* Something is wrong with the environment. */
              svn_atomic_set(&bdb->panic, TRUE);
              *panicp = TRUE;
              bdb = NULL;
            }
        }
#endif /* at least bdb-4.2 */
    }
  else
    {
      *panicp = FALSE;
    }
  return bdb;
}



/* Close and destroy a BDB environment descriptor. */
static svn_error_t *
bdb_close(bdb_env_t *bdb)
{
  svn_error_t *err = SVN_NO_ERROR;

  /* This bit is delcate; we must propagate the error from
     DB_ENV->close to the caller, and always destroy the pool. */
  int db_err = bdb->env->close(bdb->env, 0);

  /* If automatic database recovery is enabled, ignore DB_RUNRECOVERY
     errors, since they're dealt with eventually by BDB itself. */
  if (db_err && (!SVN_BDB_AUTO_RECOVER || db_err != DB_RUNRECOVERY))
    err = convert_bdb_error(bdb, db_err);

  /* Free the environment descriptor. The pool cleanup will do this unless
     the cache has already been destroyed. */
  if (bdb->pool)
    svn_pool_destroy(bdb->pool);
  else
    free(bdb);
  return svn_error_trace(err);
}


static svn_error_t *
svn_fs_bdb__close_internal(bdb_env_t *bdb)
{
  svn_error_t *err = SVN_NO_ERROR;

  if (--bdb->refcount != 0)
    {
      /* If the environment is panicked and automatic recovery is not
         enabled, return an appropriate error. */
#if !SVN_BDB_AUTO_RECOVER
      if (svn_atomic_read(&bdb->panic))
        err = svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                                db_strerror(DB_RUNRECOVERY));
#endif
    }
  else
    {
      /* If the bdb cache has been set to NULL that means we are
         shutting down, and the pool that holds the bdb cache has
         already been destroyed, so accessing it here would be a Bad
         Thing (tm) */
      if (bdb_cache)
        apr_hash_set(bdb_cache, &bdb->key, sizeof bdb->key, NULL);
      err = bdb_close(bdb);
    }
  return svn_error_trace(err);
}

svn_error_t *
svn_fs_bdb__close(bdb_env_baton_t *bdb_baton)
{
  bdb_env_t *bdb = bdb_baton->bdb;

  SVN_ERR_ASSERT(bdb_baton->env == bdb_baton->bdb->env);
  SVN_ERR_ASSERT(bdb_baton->error_info->refcount > 0);

  /* Neutralize bdb_baton's pool cleanup to prevent double-close. See
     cleanup_env_baton(). */
  bdb_baton->bdb = NULL;

  /* Note that we only bother with this cleanup if the pool is non-NULL, to
     guard against potential races between this and the cleanup_env cleanup
     callback.  It's not clear if that can actually happen, but better safe
     than sorry. */
  if (0 == --bdb_baton->error_info->refcount && bdb->pool)
    {
      svn_error_clear(bdb_baton->error_info->pending_errors);
#if APR_HAS_THREADS
      free(bdb_baton->error_info);
      apr_threadkey_private_set(NULL, bdb->error_info);
#endif
    }

  /* This may run during final pool cleanup when the lock is NULL. */
  SVN_MUTEX__WITH_LOCK(bdb_cache_lock, svn_fs_bdb__close_internal(bdb));

  return SVN_NO_ERROR;
}



/* Open and initialize a BDB environment. */
static svn_error_t *
bdb_open(bdb_env_t *bdb, u_int32_t flags, int mode)
{
#if APR_HAS_THREADS
  flags |= DB_THREAD;
#endif
  SVN_ERR(convert_bdb_error
          (bdb, (bdb->env->open)(bdb->env, bdb->path_bdb, flags, mode)));

#if SVN_BDB_AUTO_COMMIT
  /* Assert the BDB_AUTO_COMMIT flag on the opened environment. This
     will force all operations on the environment (and handles that
     are opened within the environment) to be transactional. */

  SVN_ERR(convert_bdb_error
          (bdb, bdb->env->set_flags(bdb->env, SVN_BDB_AUTO_COMMIT, 1)));
#endif

  return bdb_cache_key(&bdb->key, &bdb->dbconfig_file,
                       bdb->path, bdb->pool);
}


/* Pool cleanup for the environment baton. */
static apr_status_t
cleanup_env_baton(void *data)
{
  bdb_env_baton_t *bdb_baton = data;

  if (bdb_baton->bdb)
    svn_error_clear(svn_fs_bdb__close(bdb_baton));

  return APR_SUCCESS;
}


static svn_error_t *
svn_fs_bdb__open_internal(bdb_env_baton_t **bdb_batonp,
                          const char *path,
                          u_int32_t flags, int mode,
                          apr_pool_t *pool)
{
  bdb_env_key_t key;
  bdb_env_t *bdb;
  svn_boolean_t panic;

  /* We can safely discard the open DB_CONFIG file handle.  If the
     environment descriptor is in the cache, the key's immutability is
     guaranteed.  If it's not, we don't care if the key changes,
     between here and the actual insertion of the newly-created
     environment into the cache, because no other thread can touch the
     cache in the meantime. */
  SVN_ERR(bdb_cache_key(&key, NULL, path, pool));

  bdb = bdb_cache_get(&key, &panic);
  if (panic)
    return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                            db_strerror(DB_RUNRECOVERY));

  /* Make sure that the environment's open flags haven't changed. */
  if (bdb && bdb->flags != flags)
    {
      /* Handle changes to the DB_PRIVATE flag specially */
      if ((flags ^ bdb->flags) & DB_PRIVATE)
        {
          if (flags & DB_PRIVATE)
            return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                                    "Reopening a public Berkeley DB"
                                    " environment with private attributes");
          else
            return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                                    "Reopening a private Berkeley DB"
                                    " environment with public attributes");
        }

      /* Otherwise return a generic "flags-mismatch" error. */
      return svn_error_create(SVN_ERR_FS_BERKELEY_DB, NULL,
                              "Reopening a Berkeley DB environment"
                              " with different attributes");
    }

  if (!bdb)
    {
      svn_error_t *err;

      SVN_ERR(create_env(&bdb, path, svn_pool_create(bdb_cache_pool)));
      err = bdb_open(bdb, flags, mode);
      if (err)
        {
          /* Clean up, and we can't do anything about returned errors. */
          svn_error_clear(bdb_close(bdb));
          return svn_error_trace(err);
        }

      apr_hash_set(bdb_cache, &bdb->key, sizeof bdb->key, bdb);
      bdb->flags = flags;
      bdb->refcount = 1;
    }
  else
    {
      ++bdb->refcount;
    }

  *bdb_batonp = apr_palloc(pool, sizeof **bdb_batonp);
  (*bdb_batonp)->env = bdb->env;
  (*bdb_batonp)->bdb = bdb;
  (*bdb_batonp)->error_info = get_error_info(bdb);
  ++(*bdb_batonp)->error_info->refcount;
  apr_pool_cleanup_register(pool, *bdb_batonp, cleanup_env_baton,
                            apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_bdb__open(bdb_env_baton_t **bdb_batonp, const char *path,
                 u_int32_t flags, int mode,
                 apr_pool_t *pool)
{
  SVN_MUTEX__WITH_LOCK(bdb_cache_lock,
                       svn_fs_bdb__open_internal(bdb_batonp,
                                                 path,
                                                 flags,
                                                 mode,
                                                 pool));

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_fs_bdb__get_panic(bdb_env_baton_t *bdb_baton)
{
  /* An invalid baton is equivalent to a panicked environment; in both
     cases, database cleanups should be skipped. */
  if (!bdb_baton->bdb)
    return TRUE;

  assert(bdb_baton->env == bdb_baton->bdb->env);
  return !!svn_atomic_read(&bdb_baton->bdb->panic);
}

void
svn_fs_bdb__set_panic(bdb_env_baton_t *bdb_baton)
{
  if (!bdb_baton->bdb)
    return;

  assert(bdb_baton->env == bdb_baton->bdb->env);
  svn_atomic_set(&bdb_baton->bdb->panic, TRUE);
}


/* This function doesn't actually open the environment, so it doesn't
   have to look in the cache.  Callers are supposed to own an
   exclusive lock on the filesystem anyway. */
svn_error_t *
svn_fs_bdb__remove(const char *path, apr_pool_t *pool)
{
  bdb_env_t *bdb;

  SVN_ERR(create_env(&bdb, path, pool));
  return convert_bdb_error
    (bdb, bdb->env->remove(bdb->env, bdb->path_bdb, DB_FORCE));
}
