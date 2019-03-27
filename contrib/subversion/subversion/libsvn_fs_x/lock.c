/* lock.c :  functions for manipulating filesystem locks.
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

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_hash.h"
#include "svn_time.h"
#include "svn_utf.h"

#include <apr_uuid.h>
#include <apr_file_io.h>
#include <apr_file_info.h>

#include "lock.h"
#include "tree.h"
#include "fs_x.h"
#include "transaction.h"
#include "util.h"
#include "../libsvn_fs/fs-loader.h"

#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "private/svn_sorts_private.h"
#include "svn_private_config.h"

/* Names of hash keys used to store a lock for writing to disk. */
#define PATH_KEY "path"
#define TOKEN_KEY "token"
#define OWNER_KEY "owner"
#define CREATION_DATE_KEY "creation_date"
#define EXPIRATION_DATE_KEY "expiration_date"
#define COMMENT_KEY "comment"
#define IS_DAV_COMMENT_KEY "is_dav_comment"
#define CHILDREN_KEY "children"

/* Number of characters from the head of a digest file name used to
   calculate a subdirectory in which to drop that file. */
#define DIGEST_SUBDIR_LEN 3



/*** Generic helper functions. ***/

/* Set *DIGEST to the MD5 hash of STR. */
static svn_error_t *
make_digest(const char **digest,
            const char *str,
            apr_pool_t *pool)
{
  svn_checksum_t *checksum;

  SVN_ERR(svn_checksum(&checksum, svn_checksum_md5, str, strlen(str), pool));

  *digest = svn_checksum_to_cstring_display(checksum, pool);
  return SVN_NO_ERROR;
}


/* Set the value of KEY (whose size is KEY_LEN, or APR_HASH_KEY_STRING
   if unknown) to an svn_string_t-ized version of VALUE (whose size is
   VALUE_LEN, or APR_HASH_KEY_STRING if unknown) in HASH.  The value
   will be allocated in POOL; KEY will not be duped.  If either KEY or VALUE
   is NULL, this function will do nothing. */
static void
hash_store(apr_hash_t *hash,
           const char *key,
           apr_ssize_t key_len,
           const char *value,
           apr_ssize_t value_len,
           apr_pool_t *pool)
{
  if (! (key && value))
    return;
  if (value_len == APR_HASH_KEY_STRING)
    value_len = strlen(value);
  apr_hash_set(hash, key, key_len,
               svn_string_ncreate(value, value_len, pool));
}


/* Fetch the value of KEY from HASH, returning only the cstring data
   of that value (if it exists). */
static const char *
hash_fetch(apr_hash_t *hash,
           const char *key)
{
  svn_string_t *str = svn_hash_gets(hash, key);
  return str ? str->data : NULL;
}


/* SVN_ERR_FS_CORRUPT: the lockfile for PATH in FS is corrupt.  */
static svn_error_t *
err_corrupt_lockfile(const char *fs_path,
                     const char *path)
{
  return
    svn_error_createf(
     SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt lockfile for path '%s' in filesystem '%s'"),
     path, fs_path);
}


/*** Digest file handling functions. ***/

/* Return the path of the lock/entries file for which DIGEST is the
   hashed repository relative path. */
static const char *
digest_path_from_digest(const char *fs_path,
                        const char *digest,
                        apr_pool_t *pool)
{
  return svn_dirent_join_many(pool, fs_path, PATH_LOCKS_DIR,
                              apr_pstrmemdup(pool, digest, DIGEST_SUBDIR_LEN),
                              digest, SVN_VA_NULL);
}


/* Set *DIGEST_PATH to the path to the lock/entries digest file associate
   with PATH, where PATH is the path to the lock file or lock entries file
   in FS. */
static svn_error_t *
digest_path_from_path(const char **digest_path,
                      const char *fs_path,
                      const char *path,
                      apr_pool_t *pool)
{
  const char *digest;
  SVN_ERR(make_digest(&digest, path, pool));
  *digest_path = svn_dirent_join_many(pool, fs_path, PATH_LOCKS_DIR,
                                      apr_pstrmemdup(pool, digest,
                                                     DIGEST_SUBDIR_LEN),
                                      digest, SVN_VA_NULL);
  return SVN_NO_ERROR;
}


/* Write to DIGEST_PATH a representation of CHILDREN (which may be
   empty, if the versioned path in FS represented by DIGEST_PATH has
   no children) and LOCK (which may be NULL if that versioned path is
   lock itself locked).  Set the permissions of DIGEST_PATH to those of
   PERMS_REFERENCE.  Use POOL for temporary allocations.
 */
static svn_error_t *
write_digest_file(apr_hash_t *children,
                  svn_lock_t *lock,
                  const char *fs_path,
                  const char *digest_path,
                  const char *perms_reference,
                  apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_stream_t *stream;
  apr_hash_index_t *hi;
  apr_hash_t *hash = apr_hash_make(scratch_pool);
  const char *tmp_path;

  SVN_ERR(svn_fs_x__ensure_dir_exists(svn_dirent_join(fs_path, PATH_LOCKS_DIR,
                                                      scratch_pool),
                                      fs_path, scratch_pool));
  SVN_ERR(svn_fs_x__ensure_dir_exists(svn_dirent_dirname(digest_path,
                                                         scratch_pool),
                                      fs_path, scratch_pool));

  if (lock)
    {
      const char *creation_date = NULL, *expiration_date = NULL;
      if (lock->creation_date)
        creation_date = svn_time_to_cstring(lock->creation_date,
                                            scratch_pool);
      if (lock->expiration_date)
        expiration_date = svn_time_to_cstring(lock->expiration_date,
                                              scratch_pool);

      hash_store(hash, PATH_KEY, sizeof(PATH_KEY)-1,
                 lock->path, APR_HASH_KEY_STRING, scratch_pool);
      hash_store(hash, TOKEN_KEY, sizeof(TOKEN_KEY)-1,
                 lock->token, APR_HASH_KEY_STRING, scratch_pool);
      hash_store(hash, OWNER_KEY, sizeof(OWNER_KEY)-1,
                 lock->owner, APR_HASH_KEY_STRING, scratch_pool);
      hash_store(hash, COMMENT_KEY, sizeof(COMMENT_KEY)-1,
                 lock->comment, APR_HASH_KEY_STRING, scratch_pool);
      hash_store(hash, IS_DAV_COMMENT_KEY, sizeof(IS_DAV_COMMENT_KEY)-1,
                 lock->is_dav_comment ? "1" : "0", 1, scratch_pool);
      hash_store(hash, CREATION_DATE_KEY, sizeof(CREATION_DATE_KEY)-1,
                 creation_date, APR_HASH_KEY_STRING, scratch_pool);
      hash_store(hash, EXPIRATION_DATE_KEY, sizeof(EXPIRATION_DATE_KEY)-1,
                 expiration_date, APR_HASH_KEY_STRING, scratch_pool);
    }
  if (apr_hash_count(children))
    {
      svn_stringbuf_t *children_list
        = svn_stringbuf_create_empty(scratch_pool);
      for (hi = apr_hash_first(scratch_pool, children);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_stringbuf_appendbytes(children_list,
                                    apr_hash_this_key(hi),
                                    apr_hash_this_key_len(hi));
          svn_stringbuf_appendbyte(children_list, '\n');
        }
      hash_store(hash, CHILDREN_KEY, sizeof(CHILDREN_KEY)-1,
                 children_list->data, children_list->len, scratch_pool);
    }

  SVN_ERR(svn_stream_open_unique(&stream, &tmp_path,
                                 svn_dirent_dirname(digest_path,
                                                    scratch_pool),
                                 svn_io_file_del_none, scratch_pool,
                                 scratch_pool));
  if ((err = svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR,
                             scratch_pool)))
    {
      err = svn_error_compose_create(err, svn_stream_close(stream));
      return svn_error_createf(err->apr_err,
                               err,
                               _("Cannot write lock/entries hashfile '%s'"),
                               svn_dirent_local_style(tmp_path,
                                                      scratch_pool));
    }

  SVN_ERR(svn_stream_close(stream));
  SVN_ERR(svn_io_file_rename2(tmp_path, digest_path, FALSE, scratch_pool));
  SVN_ERR(svn_io_copy_perms(perms_reference, digest_path, scratch_pool));
  return SVN_NO_ERROR;
}


/* Parse the file at DIGEST_PATH, populating the lock LOCK_P in that
   file (if it exists, and if *LOCK_P is non-NULL) and the hash of
   CHILDREN_P (if any exist, and if *CHILDREN_P is non-NULL).  Use POOL
   for all allocations.  */
static svn_error_t *
read_digest_file(apr_hash_t **children_p,
                 svn_lock_t **lock_p,
                 const char *fs_path,
                 const char *digest_path,
                 apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_lock_t *lock;
  apr_hash_t *hash;
  svn_stream_t *stream;
  const char *val;
  svn_node_kind_t kind;

  if (lock_p)
    *lock_p = NULL;
  if (children_p)
    *children_p = apr_hash_make(pool);

  SVN_ERR(svn_io_check_path(digest_path, &kind, pool));
  if (kind == svn_node_none)
    return SVN_NO_ERROR;

  /* If our caller doesn't care about anything but the presence of the
     file... whatever. */
  if (kind == svn_node_file && !lock_p && !children_p)
    return SVN_NO_ERROR;

  SVN_ERR(svn_stream_open_readonly(&stream, digest_path, pool, pool));

  hash = apr_hash_make(pool);
  if ((err = svn_hash_read2(hash, stream, SVN_HASH_TERMINATOR, pool)))
    {
      err = svn_error_compose_create(err, svn_stream_close(stream));
      return svn_error_createf(err->apr_err,
                               err,
                               _("Can't parse lock/entries hashfile '%s'"),
                               svn_dirent_local_style(digest_path, pool));
    }
  SVN_ERR(svn_stream_close(stream));

  /* If our caller cares, see if we have a lock path in our hash. If
     so, we'll assume we have a lock here. */
  val = hash_fetch(hash, PATH_KEY);
  if (val && lock_p)
    {
      const char *path = val;

      /* Create our lock and load it up. */
      lock = svn_lock_create(pool);
      lock->path = path;

      if (! ((lock->token = hash_fetch(hash, TOKEN_KEY))))
        return svn_error_trace(err_corrupt_lockfile(fs_path, path));

      if (! ((lock->owner = hash_fetch(hash, OWNER_KEY))))
        return svn_error_trace(err_corrupt_lockfile(fs_path, path));

      if (! ((val = hash_fetch(hash, IS_DAV_COMMENT_KEY))))
        return svn_error_trace(err_corrupt_lockfile(fs_path, path));
      lock->is_dav_comment = (val[0] == '1');

      if (! ((val = hash_fetch(hash, CREATION_DATE_KEY))))
        return svn_error_trace(err_corrupt_lockfile(fs_path, path));
      SVN_ERR(svn_time_from_cstring(&(lock->creation_date), val, pool));

      if ((val = hash_fetch(hash, EXPIRATION_DATE_KEY)))
        SVN_ERR(svn_time_from_cstring(&(lock->expiration_date), val, pool));

      lock->comment = hash_fetch(hash, COMMENT_KEY);

      *lock_p = lock;
    }

  /* If our caller cares, see if we have any children for this path. */
  val = hash_fetch(hash, CHILDREN_KEY);
  if (val && children_p)
    {
      apr_array_header_t *kiddos = svn_cstring_split(val, "\n", FALSE, pool);
      int i;

      for (i = 0; i < kiddos->nelts; i++)
        {
          svn_hash_sets(*children_p, APR_ARRAY_IDX(kiddos, i, const char *),
                        (void *)1);
        }
    }
  return SVN_NO_ERROR;
}



/*** Lock helper functions (path here are still FS paths, not on-disk
     schema-supporting paths) ***/


/* Write LOCK in FS to the actual OS filesystem.

   Use PERMS_REFERENCE for the permissions of any digest files.
 */
static svn_error_t *
set_lock(const char *fs_path,
         svn_lock_t *lock,
         const char *perms_reference,
         apr_pool_t *scratch_pool)
{
  const char *digest_path;
  apr_hash_t *children;

  SVN_ERR(digest_path_from_path(&digest_path, fs_path, lock->path,
                                scratch_pool));

  /* We could get away without reading the file as children should
     always come back empty. */
  SVN_ERR(read_digest_file(&children, NULL, fs_path, digest_path,
                           scratch_pool));

  SVN_ERR(write_digest_file(children, lock, fs_path, digest_path,
                            perms_reference, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_lock(const char *fs_path,
            const char *path,
            apr_pool_t *scratch_pool)
{
  const char *digest_path;

  SVN_ERR(digest_path_from_path(&digest_path, fs_path, path, scratch_pool));

  SVN_ERR(svn_io_remove_file2(digest_path, TRUE, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
add_to_digest(const char *fs_path,
              apr_array_header_t *paths,
              const char *index_path,
              const char *perms_reference,
              apr_pool_t *scratch_pool)
{
  const char *index_digest_path;
  apr_hash_t *children;
  svn_lock_t *lock;
  int i;
  unsigned int original_count;

  SVN_ERR(digest_path_from_path(&index_digest_path, fs_path, index_path,
                                scratch_pool));
  SVN_ERR(read_digest_file(&children, &lock, fs_path, index_digest_path,
                           scratch_pool));

  original_count = apr_hash_count(children);

  for (i = 0; i < paths->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      const char *digest_path, *digest_file;

      SVN_ERR(digest_path_from_path(&digest_path, fs_path, path,
                                    scratch_pool));
      digest_file = svn_dirent_basename(digest_path, NULL);
      svn_hash_sets(children, digest_file, (void *)1);
    }

  if (apr_hash_count(children) != original_count)
    SVN_ERR(write_digest_file(children, lock, fs_path, index_digest_path,
                              perms_reference, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_from_digest(const char *fs_path,
                   apr_array_header_t *paths,
                   const char *index_path,
                   const char *perms_reference,
                   apr_pool_t *scratch_pool)
{
  const char *index_digest_path;
  apr_hash_t *children;
  svn_lock_t *lock;
  int i;

  SVN_ERR(digest_path_from_path(&index_digest_path, fs_path, index_path,
                                scratch_pool));
  SVN_ERR(read_digest_file(&children, &lock, fs_path, index_digest_path,
                           scratch_pool));

  for (i = 0; i < paths->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      const char *digest_path, *digest_file;

      SVN_ERR(digest_path_from_path(&digest_path, fs_path, path,
                                    scratch_pool));
      digest_file = svn_dirent_basename(digest_path, NULL);
      svn_hash_sets(children, digest_file, NULL);
    }

  if (apr_hash_count(children) || lock)
    SVN_ERR(write_digest_file(children, lock, fs_path, index_digest_path,
                              perms_reference, scratch_pool));
  else
    SVN_ERR(svn_io_remove_file2(index_digest_path, TRUE, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
unlock_single(svn_fs_t *fs,
              svn_lock_t *lock,
              apr_pool_t *pool);

/* Check if LOCK has been already expired. */
static svn_boolean_t lock_expired(const svn_lock_t *lock)
{
  return lock->expiration_date && (apr_time_now() > lock->expiration_date);
}

/* Set *LOCK_P to the lock for PATH in FS.  HAVE_WRITE_LOCK should be
   TRUE if the caller (or one of its callers) has taken out the
   repository-wide write lock, FALSE otherwise.  If MUST_EXIST is
   not set, the function will simply return NULL in *LOCK_P instead
   of creating an SVN_FS__ERR_NO_SUCH_LOCK error in case the lock
   was not found (much faster).  Use POOL for allocations. */
static svn_error_t *
get_lock(svn_lock_t **lock_p,
         svn_fs_t *fs,
         const char *path,
         svn_boolean_t have_write_lock,
         svn_boolean_t must_exist,
         apr_pool_t *pool)
{
  svn_lock_t *lock = NULL;
  const char *digest_path;
  svn_node_kind_t kind;

  SVN_ERR(digest_path_from_path(&digest_path, fs->path, path, pool));
  SVN_ERR(svn_io_check_path(digest_path, &kind, pool));

  *lock_p = NULL;
  if (kind != svn_node_none)
    SVN_ERR(read_digest_file(NULL, &lock, fs->path, digest_path, pool));

  if (! lock)
    return must_exist ? SVN_FS__ERR_NO_SUCH_LOCK(fs, path) : SVN_NO_ERROR;

  /* Don't return an expired lock. */
  if (lock_expired(lock))
    {
      /* Only remove the lock if we have the write lock.
         Read operations shouldn't change the filesystem. */
      if (have_write_lock)
        SVN_ERR(unlock_single(fs, lock, pool));
      return SVN_FS__ERR_LOCK_EXPIRED(fs, lock->token);
    }

  *lock_p = lock;
  return SVN_NO_ERROR;
}


/* Set *LOCK_P to the lock for PATH in FS.  HAVE_WRITE_LOCK should be
   TRUE if the caller (or one of its callers) has taken out the
   repository-wide write lock, FALSE otherwise.  Use POOL for
   allocations. */
static svn_error_t *
get_lock_helper(svn_fs_t *fs,
                svn_lock_t **lock_p,
                const char *path,
                svn_boolean_t have_write_lock,
                apr_pool_t *pool)
{
  svn_lock_t *lock;
  svn_error_t *err;

  err = get_lock(&lock, fs, path, have_write_lock, FALSE, pool);

  /* We've deliberately decided that this function doesn't tell the
     caller *why* the lock is unavailable.  */
  if (err && ((err->apr_err == SVN_ERR_FS_NO_SUCH_LOCK)
              || (err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)))
    {
      svn_error_clear(err);
      *lock_p = NULL;
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  *lock_p = lock;
  return SVN_NO_ERROR;
}


/* A function that calls GET_LOCKS_FUNC/GET_LOCKS_BATON for
   all locks in and under PATH in FS.
   HAVE_WRITE_LOCK should be true if the caller (directly or indirectly)
   has the FS write lock. */
static svn_error_t *
walk_locks(svn_fs_t *fs,
           const char *digest_path,
           svn_fs_get_locks_callback_t get_locks_func,
           void *get_locks_baton,
           svn_boolean_t have_write_lock,
           apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *children;
  apr_pool_t *subpool;
  svn_lock_t *lock;

  /* First, send up any locks in the current digest file. */
  SVN_ERR(read_digest_file(&children, &lock, fs->path, digest_path, pool));

  if (lock && lock_expired(lock))
    {
      /* Only remove the lock if we have the write lock.
         Read operations shouldn't change the filesystem. */
      if (have_write_lock)
        SVN_ERR(unlock_single(fs, lock, pool));
    }
  else if (lock)
    {
      SVN_ERR(get_locks_func(get_locks_baton, lock, pool));
    }

  /* Now, report all the child entries (if any; bail otherwise). */
  if (! apr_hash_count(children))
    return SVN_NO_ERROR;
  subpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, children); hi; hi = apr_hash_next(hi))
    {
      const char *digest = apr_hash_this_key(hi);
      svn_pool_clear(subpool);

      SVN_ERR(read_digest_file
              (NULL, &lock, fs->path,
               digest_path_from_digest(fs->path, digest, subpool), subpool));

      if (lock && lock_expired(lock))
        {
          /* Only remove the lock if we have the write lock.
             Read operations shouldn't change the filesystem. */
          if (have_write_lock)
            SVN_ERR(unlock_single(fs, lock, pool));
        }
      else if (lock)
        {
          SVN_ERR(get_locks_func(get_locks_baton, lock, pool));
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Utility function:  verify that a lock can be used.  Interesting
   errors returned from this function:

      SVN_ERR_FS_NO_USER: No username attached to FS.
      SVN_ERR_FS_LOCK_OWNER_MISMATCH: FS's username doesn't match LOCK's owner.
      SVN_ERR_FS_BAD_LOCK_TOKEN: FS doesn't hold matching lock-token for LOCK.
 */
static svn_error_t *
verify_lock(svn_fs_t *fs,
            svn_lock_t *lock)
{
  if ((! fs->access_ctx) || (! fs->access_ctx->username))
    return svn_error_createf
      (SVN_ERR_FS_NO_USER, NULL,
       _("Cannot verify lock on path '%s'; no username available"),
       lock->path);

  else if (strcmp(fs->access_ctx->username, lock->owner) != 0)
    return svn_error_createf
      (SVN_ERR_FS_LOCK_OWNER_MISMATCH, NULL,
       _("User '%s' does not own lock on path '%s' (currently locked by '%s')"),
       fs->access_ctx->username, lock->path, lock->owner);

  else if (svn_hash_gets(fs->access_ctx->lock_tokens, lock->token) == NULL)
    return svn_error_createf
      (SVN_ERR_FS_BAD_LOCK_TOKEN, NULL,
       _("Cannot verify lock on path '%s'; no matching lock-token available"),
       lock->path);

  return SVN_NO_ERROR;
}


/* This implements the svn_fs_get_locks_callback_t interface, where
   BATON is just an svn_fs_t object. */
static svn_error_t *
get_locks_callback(void *baton,
                   svn_lock_t *lock,
                   apr_pool_t *pool)
{
  return verify_lock(baton, lock);
}


/* The main routine for lock enforcement, used throughout libsvn_fs_x. */
svn_error_t *
svn_fs_x__allow_locked_operation(const char *path,
                                 svn_fs_t *fs,
                                 svn_boolean_t recurse,
                                 svn_boolean_t have_write_lock,
                                 apr_pool_t *scratch_pool)
{
  path = svn_fs__canonicalize_abspath(path, scratch_pool);
  if (recurse)
    {
      /* Discover all locks at or below the path. */
      const char *digest_path;
      SVN_ERR(digest_path_from_path(&digest_path, fs->path, path,
                                    scratch_pool));
      SVN_ERR(walk_locks(fs, digest_path, get_locks_callback,
                         fs, have_write_lock, scratch_pool));
    }
  else
    {
      /* Discover and verify any lock attached to the path. */
      svn_lock_t *lock;
      SVN_ERR(get_lock_helper(fs, &lock, path, have_write_lock,
                              scratch_pool));
      if (lock)
        SVN_ERR(verify_lock(fs, lock));
    }
  return SVN_NO_ERROR;
}

/* Helper function called from the lock and unlock code.
   UPDATES is a map from "const char *" parent paths to "apr_array_header_t *"
   arrays of child paths.  For all of the parent paths of PATH this function
   adds PATH to the corresponding array of child paths. */
static void
schedule_index_update(apr_hash_t *updates,
                      const char *path,
                      apr_pool_t *scratch_pool)
{
  apr_pool_t *hashpool = apr_hash_pool_get(updates);
  const char *parent_path = path;

  while (! svn_fspath__is_root(parent_path, strlen(parent_path)))
    {
      apr_array_header_t *children;

      parent_path = svn_fspath__dirname(parent_path, scratch_pool);
      children = svn_hash_gets(updates, parent_path);

      if (! children)
        {
          children = apr_array_make(hashpool, 8, sizeof(const char *));
          svn_hash_sets(updates, apr_pstrdup(hashpool, parent_path), children);
        }

      APR_ARRAY_PUSH(children, const char *) = path;
    }
}

/* The effective arguments for lock_body() below. */
typedef struct lock_baton_t {
  svn_fs_t *fs;
  apr_array_header_t *targets;
  apr_array_header_t *infos;
  const char *comment;
  svn_boolean_t is_dav_comment;
  apr_time_t expiration_date;
  svn_boolean_t steal_lock;
  apr_pool_t *result_pool;
} lock_baton_t;

static svn_error_t *
check_lock(svn_error_t **fs_err,
           const char *path,
           const svn_fs_lock_target_t *target,
           lock_baton_t *lb,
           svn_fs_root_t *root,
           svn_revnum_t youngest_rev,
           apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_lock_t *existing_lock;

  *fs_err = SVN_NO_ERROR;

  SVN_ERR(svn_fs_x__check_path(&kind, root, path, pool));
  if (kind == svn_node_dir)
    {
      *fs_err = SVN_FS__ERR_NOT_FILE(lb->fs, path);
      return SVN_NO_ERROR;
    }

  /* While our locking implementation easily supports the locking of
     nonexistent paths, we deliberately choose not to allow such madness. */
  if (kind == svn_node_none)
    {
      if (SVN_IS_VALID_REVNUM(target->current_rev))
        *fs_err = svn_error_createf(
          SVN_ERR_FS_OUT_OF_DATE, NULL,
          _("Path '%s' doesn't exist in HEAD revision"),
          path);
      else
        *fs_err = svn_error_createf(
          SVN_ERR_FS_NOT_FOUND, NULL,
          _("Path '%s' doesn't exist in HEAD revision"),
          path);

      return SVN_NO_ERROR;
    }

  /* Is the caller attempting to lock an out-of-date working file? */
  if (SVN_IS_VALID_REVNUM(target->current_rev))
    {
      svn_revnum_t created_rev;

      if (target->current_rev > youngest_rev)
        {
          *fs_err = svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                      _("No such revision %ld"),
                                      target->current_rev);
          return SVN_NO_ERROR;
        }

      SVN_ERR(svn_fs_x__node_created_rev(&created_rev, root, path,
                                         pool));

      /* SVN_INVALID_REVNUM means the path doesn't exist.  So
         apparently somebody is trying to lock something in their
         working copy, but somebody else has deleted the thing
         from HEAD.  That counts as being 'out of date'. */
      if (! SVN_IS_VALID_REVNUM(created_rev))
        {
          *fs_err = svn_error_createf
            (SVN_ERR_FS_OUT_OF_DATE, NULL,
             _("Path '%s' doesn't exist in HEAD revision"), path);

          return SVN_NO_ERROR;
        }

      if (target->current_rev < created_rev)
        {
          *fs_err = svn_error_createf
            (SVN_ERR_FS_OUT_OF_DATE, NULL,
             _("Lock failed: newer version of '%s' exists"), path);

          return SVN_NO_ERROR;
        }
    }

  /* If the caller provided a TOKEN, we *really* need to see
     if a lock already exists with that token, and if so, verify that
     the lock's path matches PATH.  Otherwise we run the risk of
     breaking the 1-to-1 mapping of lock tokens to locked paths. */
  /* ### TODO:  actually do this check.  This is tough, because the
     schema doesn't supply a lookup-by-token mechanism. */

  /* Is the path already locked?

     Note that this next function call will automatically ignore any
     errors about {the path not existing as a key, the path's token
     not existing as a key, the lock just having been expired}.  And
     that's totally fine.  Any of these three errors are perfectly
     acceptable to ignore; it means that the path is now free and
     clear for locking, because the fsx funcs just cleared out both
     of the tables for us.   */
  SVN_ERR(get_lock_helper(lb->fs, &existing_lock, path, TRUE, pool));
  if (existing_lock)
    {
      if (! lb->steal_lock)
        {
          /* Sorry, the path is already locked. */
          *fs_err = SVN_FS__ERR_PATH_ALREADY_LOCKED(lb->fs, existing_lock);
          return SVN_NO_ERROR;
        }
    }

  return SVN_NO_ERROR;
}

typedef struct lock_info_t {
  const char *path;
  svn_lock_t *lock;
  svn_error_t *fs_err;
} lock_info_t;

/* The body of svn_fs_x__lock(), which see.

   BATON is a 'lock_baton_t *' holding the effective arguments.
   BATON->targets is an array of 'svn_sort__item_t' targets, sorted by
   path, mapping canonical path to 'svn_fs_lock_target_t'.  Set
   BATON->infos to an array of 'lock_info_t' holding the results.  For
   the other arguments, see svn_fs_lock_many().

   This implements the svn_fs_x__with_write_lock() 'body' callback
   type, and assumes that the write lock is held.
 */
static svn_error_t *
lock_body(void *baton,
          apr_pool_t *pool)
{
  lock_baton_t *lb = baton;
  svn_fs_root_t *root;
  svn_revnum_t youngest;
  const char *rev_0_path;
  int i;
  apr_hash_t *index_updates = apr_hash_make(pool);
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Until we implement directory locks someday, we only allow locks
     on files. */
  /* Use fs->vtable->foo instead of svn_fs_foo to avoid circular
     library dependencies, which are not portable. */
  SVN_ERR(lb->fs->vtable->youngest_rev(&youngest, lb->fs, pool));
  SVN_ERR(lb->fs->vtable->revision_root(&root, lb->fs, youngest, pool));

  for (i = 0; i < lb->targets->nelts; ++i)
    {
      const svn_sort__item_t *item = &APR_ARRAY_IDX(lb->targets, i,
                                                    svn_sort__item_t);
      lock_info_t info;

      svn_pool_clear(iterpool);

      info.path = item->key;
      info.lock = NULL;
      info.fs_err = SVN_NO_ERROR;

      SVN_ERR(check_lock(&info.fs_err, info.path, item->value, lb, root,
                         youngest, iterpool));

      /* If no error occurred while pre-checking, schedule the index updates for
         this path. */
      if (!info.fs_err)
        schedule_index_update(index_updates, info.path, iterpool);

      APR_ARRAY_PUSH(lb->infos, lock_info_t) = info;
    }

  rev_0_path = svn_fs_x__path_rev_absolute(lb->fs, 0, pool);

  /* We apply the scheduled index updates before writing the actual locks.

     Writing indices before locks is correct: if interrupted it leaves
     indices without locks rather than locks without indices.  An
     index without a lock is consistent in that it always shows up as
     unlocked in svn_fs_x__allow_locked_operation.  A lock without an
     index is inconsistent, svn_fs_x__allow_locked_operation will
     show locked on the file but unlocked on the parent. */

  for (hi = apr_hash_first(pool, index_updates); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      apr_array_header_t *children = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);
      SVN_ERR(add_to_digest(lb->fs->path, children, path, rev_0_path,
                            iterpool));
    }

  for (i = 0; i < lb->infos->nelts; ++i)
    {
      struct lock_info_t *info = &APR_ARRAY_IDX(lb->infos, i,
                                                struct lock_info_t);
      svn_sort__item_t *item = &APR_ARRAY_IDX(lb->targets, i, svn_sort__item_t);
      svn_fs_lock_target_t *target = item->value;

      svn_pool_clear(iterpool);

      if (! info->fs_err)
        {
          info->lock = svn_lock_create(lb->result_pool);
          if (target->token)
            info->lock->token = apr_pstrdup(lb->result_pool, target->token);
          else
            SVN_ERR(svn_fs_x__generate_lock_token(&(info->lock->token), lb->fs,
                                                  lb->result_pool));

          /* The INFO->PATH is already allocated in LB->RESULT_POOL as a result
             of svn_fspath__canonicalize() (see svn_fs_x__lock()). */
          info->lock->path = info->path;
          info->lock->owner = apr_pstrdup(lb->result_pool,
                                          lb->fs->access_ctx->username);
          info->lock->comment = apr_pstrdup(lb->result_pool, lb->comment);
          info->lock->is_dav_comment = lb->is_dav_comment;
          info->lock->creation_date = apr_time_now();
          info->lock->expiration_date = lb->expiration_date;

          info->fs_err = set_lock(lb->fs->path, info->lock, rev_0_path,
                                  iterpool);
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* The effective arguments for unlock_body() below. */
typedef struct unlock_baton_t {
  svn_fs_t *fs;
  apr_array_header_t *targets;
  apr_array_header_t *infos;
  /* Set skip_check TRUE to prevent the checks that set infos[].fs_err. */
  svn_boolean_t skip_check;
  svn_boolean_t break_lock;
  apr_pool_t *result_pool;
} unlock_baton_t;

static svn_error_t *
check_unlock(svn_error_t **fs_err,
             const char *path,
             const char *token,
             unlock_baton_t *ub,
             svn_fs_root_t *root,
             apr_pool_t *pool)
{
  svn_lock_t *lock;

  *fs_err = get_lock(&lock, ub->fs, path, TRUE, TRUE, pool);
  if (!*fs_err && !ub->break_lock)
    {
      if (strcmp(token, lock->token) != 0)
        *fs_err = SVN_FS__ERR_NO_SUCH_LOCK(ub->fs, path);
      else if (strcmp(ub->fs->access_ctx->username, lock->owner) != 0)
        *fs_err = SVN_FS__ERR_LOCK_OWNER_MISMATCH(ub->fs,
                                                  ub->fs->access_ctx->username,
                                                  lock->owner);
    }

  return SVN_NO_ERROR;
}

typedef struct unlock_info_t {
  const char *path;
  svn_error_t *fs_err;
  svn_boolean_t done;
} unlock_info_t;

/* The body of svn_fs_x__unlock(), which see.

   BATON is a 'unlock_baton_t *' holding the effective arguments.
   BATON->targets is an array of 'svn_sort__item_t' targets, sorted by
   path, mapping canonical path to (const char *) token.  Set
   BATON->infos to an array of 'unlock_info_t' results.  For the other
   arguments, see svn_fs_unlock_many().

   This implements the svn_fs_x__with_write_lock() 'body' callback
   type, and assumes that the write lock is held.
 */
static svn_error_t *
unlock_body(void *baton,
            apr_pool_t *pool)
{
  unlock_baton_t *ub = baton;
  svn_fs_root_t *root;
  svn_revnum_t youngest;
  const char *rev_0_path;
  int i;
  apr_hash_t *indices_updates = apr_hash_make(pool);
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  SVN_ERR(ub->fs->vtable->youngest_rev(&youngest, ub->fs, pool));
  SVN_ERR(ub->fs->vtable->revision_root(&root, ub->fs, youngest, pool));

  for (i = 0; i < ub->targets->nelts; ++i)
    {
      const svn_sort__item_t *item = &APR_ARRAY_IDX(ub->targets, i,
                                                    svn_sort__item_t);
      const char *token = item->value;
      unlock_info_t info;

      svn_pool_clear(iterpool);

      info.path = item->key;
      info.fs_err = SVN_NO_ERROR;
      info.done = FALSE;

      if (!ub->skip_check)
        SVN_ERR(check_unlock(&info.fs_err, info.path, token, ub, root,
                             iterpool));

      /* If no error occurred while pre-checking, schedule the index updates for
         this path. */
      if (!info.fs_err)
        schedule_index_update(indices_updates, info.path, iterpool);

      APR_ARRAY_PUSH(ub->infos, unlock_info_t) = info;
    }

  rev_0_path = svn_fs_x__path_rev_absolute(ub->fs, 0, pool);

  /* Unlike the lock_body(), we need to delete locks *before* we start to
     update indices. */

  for (i = 0; i < ub->infos->nelts; ++i)
    {
      struct unlock_info_t *info = &APR_ARRAY_IDX(ub->infos, i,
                                                  struct unlock_info_t);

      svn_pool_clear(iterpool);

      if (! info->fs_err)
        {
          SVN_ERR(delete_lock(ub->fs->path, info->path, iterpool));
          info->done = TRUE;
        }
    }

  for (hi = apr_hash_first(pool, indices_updates); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      apr_array_header_t *children = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);
      SVN_ERR(delete_from_digest(ub->fs->path, children, path, rev_0_path,
                                 iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Unlock the lock described by LOCK->path and LOCK->token in FS.

   This assumes that the write lock is held.
 */
static svn_error_t *
unlock_single(svn_fs_t *fs,
              svn_lock_t *lock,
              apr_pool_t *scratch_pool)
{
  unlock_baton_t ub;
  svn_sort__item_t item;
  apr_array_header_t *targets = apr_array_make(scratch_pool, 1,
                                               sizeof(svn_sort__item_t));
  item.key = lock->path;
  item.klen = strlen(item.key);
  item.value = (char*)lock->token;
  APR_ARRAY_PUSH(targets, svn_sort__item_t) = item;

  ub.fs = fs;
  ub.targets = targets;
  ub.infos = apr_array_make(scratch_pool, targets->nelts,
                            sizeof(struct unlock_info_t));
  ub.skip_check = TRUE;
  ub.result_pool = scratch_pool;

  /* No ub.infos[].fs_err error because skip_check is TRUE. */
  SVN_ERR(unlock_body(&ub, scratch_pool));

  return SVN_NO_ERROR;
}


/*** Public API implementations ***/

svn_error_t *
svn_fs_x__lock(svn_fs_t *fs,
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
  lock_baton_t lb;
  apr_array_header_t *sorted_targets;
  apr_hash_t *canonical_targets = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  svn_error_t *err, *cb_err = SVN_NO_ERROR;
  int i;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  /* We need to have a username attached to the fs. */
  if (!fs->access_ctx || !fs->access_ctx->username)
    return SVN_FS__ERR_NO_USER(fs);

  /* The FS locking API allows both canonical and non-canonical
     paths which means that the same canonical path could be
     represented more than once in the TARGETS hash.  We just keep
     one, choosing one with a token if possible. */
  for (hi = apr_hash_first(scratch_pool, targets); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      const svn_fs_lock_target_t *target = apr_hash_this_val(hi);
      const svn_fs_lock_target_t *other;

      path = svn_fspath__canonicalize(path, result_pool);
      other = svn_hash_gets(canonical_targets, path);

      if (!other || (!other->token && target->token))
        svn_hash_sets(canonical_targets, path, target);
    }

  sorted_targets = svn_sort__hash(canonical_targets,
                                  svn_sort_compare_items_as_paths,
                                  scratch_pool);

  lb.fs = fs;
  lb.targets = sorted_targets;
  lb.infos = apr_array_make(result_pool, sorted_targets->nelts,
                            sizeof(struct lock_info_t));
  lb.comment = comment;
  lb.is_dav_comment = is_dav_comment;
  lb.expiration_date = expiration_date;
  lb.steal_lock = steal_lock;
  lb.result_pool = result_pool;

  iterpool = svn_pool_create(scratch_pool);
  err = svn_fs_x__with_write_lock(fs, lock_body, &lb, iterpool);
  for (i = 0; i < lb.infos->nelts; ++i)
    {
      struct lock_info_t *info = &APR_ARRAY_IDX(lb.infos, i,
                                                struct lock_info_t);
      svn_pool_clear(iterpool);
      if (!cb_err && lock_callback)
        {
          if (!info->lock && !info->fs_err)
            info->fs_err = svn_error_createf(SVN_ERR_FS_LOCK_OPERATION_FAILED,
                                             0, _("Failed to lock '%s'"),
                                             info->path);

          cb_err = lock_callback(lock_baton, info->path, info->lock,
                                 info->fs_err, iterpool);
        }
      svn_error_clear(info->fs_err);
    }
  svn_pool_destroy(iterpool);

  if (err && cb_err)
    svn_error_compose(err, cb_err);
  else if (!err)
    err = cb_err;

  return svn_error_trace(err);
}


svn_error_t *
svn_fs_x__generate_lock_token(const char **token,
                              svn_fs_t *fs,
                              apr_pool_t *pool)
{
  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  /* Notice that 'fs' is currently unused.  But perhaps someday, we'll
     want to use the fs UUID + some incremented number?  For now, we
     generate a URI that matches the DAV RFC.  We could change this to
     some other URI scheme someday, if we wish. */
  *token = apr_pstrcat(pool, "opaquelocktoken:",
                       svn_uuid_generate(pool), SVN_VA_NULL);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__unlock(svn_fs_t *fs,
                 apr_hash_t *targets,
                 svn_boolean_t break_lock,
                 svn_fs_lock_callback_t lock_callback,
                 void *lock_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  unlock_baton_t ub;
  apr_array_header_t *sorted_targets;
  apr_hash_t *canonical_targets = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  svn_error_t *err, *cb_err = SVN_NO_ERROR;
  int i;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  /* We need to have a username attached to the fs. */
  if (!fs->access_ctx || !fs->access_ctx->username)
    return SVN_FS__ERR_NO_USER(fs);

  for (hi = apr_hash_first(scratch_pool, targets); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      const char *token = apr_hash_this_val(hi);
      const char *other;

      path = svn_fspath__canonicalize(path, result_pool);
      other = svn_hash_gets(canonical_targets, path);

      if (!other)
        svn_hash_sets(canonical_targets, path, token);
    }

  sorted_targets = svn_sort__hash(canonical_targets,
                                  svn_sort_compare_items_as_paths,
                                  scratch_pool);

  ub.fs = fs;
  ub.targets = sorted_targets;
  ub.infos = apr_array_make(result_pool, sorted_targets->nelts,
                            sizeof(struct unlock_info_t));
  ub.skip_check = FALSE;
  ub.break_lock = break_lock;
  ub.result_pool = result_pool;

  iterpool = svn_pool_create(scratch_pool);
  err = svn_fs_x__with_write_lock(fs, unlock_body, &ub, iterpool);
  for (i = 0; i < ub.infos->nelts; ++i)
    {
      unlock_info_t *info = &APR_ARRAY_IDX(ub.infos, i, unlock_info_t);
      svn_pool_clear(iterpool);
      if (!cb_err && lock_callback)
        {
          if (!info->done && !info->fs_err)
            info->fs_err = svn_error_createf(SVN_ERR_FS_LOCK_OPERATION_FAILED,
                                             0, _("Failed to unlock '%s'"),
                                             info->path);
          cb_err = lock_callback(lock_baton, info->path, NULL, info->fs_err,
                                 iterpool);
        }
      svn_error_clear(info->fs_err);
    }
  svn_pool_destroy(iterpool);

  if (err && cb_err)
    svn_error_compose(err, cb_err);
  else if (!err)
    err = cb_err;

  return svn_error_trace(err);
}


svn_error_t *
svn_fs_x__get_lock(svn_lock_t **lock_p,
                   svn_fs_t *fs,
                   const char *path,
                   apr_pool_t *pool)
{
  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  path = svn_fs__canonicalize_abspath(path, pool);
  return get_lock_helper(fs, lock_p, path, FALSE, pool);
}


/* Baton for get_locks_filter_func(). */
typedef struct get_locks_filter_baton_t
{
  const char *path;
  svn_depth_t requested_depth;
  svn_fs_get_locks_callback_t get_locks_func;
  void *get_locks_baton;

} get_locks_filter_baton_t;


/* A wrapper for the GET_LOCKS_FUNC passed to svn_fs_x__get_locks()
   which filters out locks on paths that aren't within
   BATON->requested_depth of BATON->path before called
   BATON->get_locks_func() with BATON->get_locks_baton.

   NOTE: See issue #3660 for details about how the FSX lock
   management code is inconsistent.  Until that inconsistency is
   resolved, we take this filtering approach rather than honoring
   depth requests closer to the crawling code.  In other words, once
   we decide how to resolve issue #3660, there might be a more
   performant way to honor the depth passed to svn_fs_x__get_locks().  */
static svn_error_t *
get_locks_filter_func(void *baton,
                      svn_lock_t *lock,
                      apr_pool_t *pool)
{
  get_locks_filter_baton_t *b = baton;

  /* Filter out unwanted paths.  Since Subversion only allows
     locks on files, we can treat depth=immediates the same as
     depth=files for filtering purposes.  Meaning, we'll keep
     this lock if:

     a) its path is the very path we queried, or
     b) we've asked for a fully recursive answer, or
     c) we've asked for depth=files or depth=immediates, and this
        lock is on an immediate child of our query path.
  */
  if ((strcmp(b->path, lock->path) == 0)
      || (b->requested_depth == svn_depth_infinity))
    {
      SVN_ERR(b->get_locks_func(b->get_locks_baton, lock, pool));
    }
  else if ((b->requested_depth == svn_depth_files) ||
           (b->requested_depth == svn_depth_immediates))
    {
      const char *rel_uri = svn_fspath__skip_ancestor(b->path, lock->path);
      if (rel_uri && (svn_path_component_count(rel_uri) == 1))
        SVN_ERR(b->get_locks_func(b->get_locks_baton, lock, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_locks(svn_fs_t *fs,
                    const char *path,
                    svn_depth_t depth,
                    svn_fs_get_locks_callback_t get_locks_func,
                    void *get_locks_baton,
                    apr_pool_t *scratch_pool)
{
  const char *digest_path;
  get_locks_filter_baton_t glfb;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  path = svn_fs__canonicalize_abspath(path, scratch_pool);

  glfb.path = path;
  glfb.requested_depth = depth;
  glfb.get_locks_func = get_locks_func;
  glfb.get_locks_baton = get_locks_baton;

  /* Get the top digest path in our tree of interest, and then walk it. */
  SVN_ERR(digest_path_from_path(&digest_path, fs->path, path, scratch_pool));
  SVN_ERR(walk_locks(fs, digest_path, get_locks_filter_func, &glfb,
                     FALSE, scratch_pool));
  return SVN_NO_ERROR;
}
