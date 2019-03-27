/* hotcopys.c --- FS hotcopy functionality for FSX
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
#include "svn_path.h"
#include "svn_dirent_uri.h"

#include "fs_x.h"
#include "hotcopy.h"
#include "util.h"
#include "revprops.h"
#include "rep-cache.h"
#include "transaction.h"
#include "recovery.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Like svn_io_dir_file_copy(), but doesn't copy files that exist at
 * the destination and do not differ in terms of kind, size, and mtime.
 * Set *SKIPPED_P to FALSE only if the file was copied, do not change
 * the value in *SKIPPED_P otherwise. SKIPPED_P may be NULL if not
 * required. */
static svn_error_t *
hotcopy_io_dir_file_copy(svn_boolean_t *skipped_p,
                         const char *src_path,
                         const char *dst_path,
                         const char *file,
                         apr_pool_t *scratch_pool)
{
  const svn_io_dirent2_t *src_dirent;
  const svn_io_dirent2_t *dst_dirent;
  const char *src_target;
  const char *dst_target;

  /* Does the destination already exist? If not, we must copy it. */
  dst_target = svn_dirent_join(dst_path, file, scratch_pool);
  SVN_ERR(svn_io_stat_dirent2(&dst_dirent, dst_target, FALSE, TRUE,
                              scratch_pool, scratch_pool));
  if (dst_dirent->kind != svn_node_none)
    {
      /* If the destination's stat information indicates that the file
       * is equal to the source, don't bother copying the file again. */
      src_target = svn_dirent_join(src_path, file, scratch_pool);
      SVN_ERR(svn_io_stat_dirent2(&src_dirent, src_target, FALSE, FALSE,
                                  scratch_pool, scratch_pool));
      if (src_dirent->kind == dst_dirent->kind &&
          src_dirent->special == dst_dirent->special &&
          src_dirent->filesize == dst_dirent->filesize &&
          src_dirent->mtime <= dst_dirent->mtime)
        return SVN_NO_ERROR;
    }

  if (skipped_p)
    *skipped_p = FALSE;

  return svn_error_trace(svn_io_dir_file_copy(src_path, dst_path, file,
                                              scratch_pool));
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
                   apr_pool_t *result_pool)
{
  svn_error_t *err = svn_path_cstring_to_utf8(name_p, name, result_pool);
  if (err && err->apr_err == APR_EINVAL)
    {
      return svn_error_createf(err->apr_err, err,
                               _("Error converting entry "
                                 "in directory '%s' to UTF-8"),
                               svn_dirent_local_style(parent, result_pool));
    }
  return err;
}

/* Like svn_io_copy_dir_recursively() but doesn't copy regular files that
 * exist in the destination and do not differ from the source in terms of
 * kind, size, and mtime. Set *SKIPPED_P to FALSE only if at least one
 * file was copied, do not change the value in *SKIPPED_P otherwise.
 * SKIPPED_P may be NULL if not required. */
static svn_error_t *
hotcopy_io_copy_dir_recursively(svn_boolean_t *skipped_p,
                                const char *src,
                                const char *dst_parent,
                                const char *dst_basename,
                                svn_boolean_t copy_perms,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  apr_status_t status;
  const char *dst_path;
  apr_dir_t *this_dir;
  apr_finfo_t this_entry;
  apr_int32_t flags = APR_FINFO_TYPE | APR_FINFO_NAME;

  /* Make a subpool for recursion */
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  /* The 'dst_path' is simply dst_parent/dst_basename */
  dst_path = svn_dirent_join(dst_parent, dst_basename, scratch_pool);

  /* Sanity checks:  SRC and DST_PARENT are directories, and
     DST_BASENAME doesn't already exist in DST_PARENT. */
  SVN_ERR(svn_io_check_path(src, &kind, subpool));
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("Source '%s' is not a directory"),
                             svn_dirent_local_style(src, scratch_pool));

  SVN_ERR(svn_io_check_path(dst_parent, &kind, subpool));
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("Destination '%s' is not a directory"),
                             svn_dirent_local_style(dst_parent,
                                                    scratch_pool));

  SVN_ERR(svn_io_check_path(dst_path, &kind, subpool));

  /* Create the new directory. */
  /* ### TODO: copy permissions (needs apr_file_attrs_get()) */
  SVN_ERR(svn_io_make_dir_recursively(dst_path, scratch_pool));

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
          const char *entryname_utf8;

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          SVN_ERR(entry_name_to_utf8(&entryname_utf8, this_entry.name,
                                     src, subpool));
          if (this_entry.filetype == APR_REG) /* regular file */
            {
              SVN_ERR(hotcopy_io_dir_file_copy(skipped_p, src, dst_path,
                                               entryname_utf8, subpool));
            }
          else if (this_entry.filetype == APR_LNK) /* symlink */
            {
              const char *src_target = svn_dirent_join(src, entryname_utf8,
                                                       subpool);
              const char *dst_target = svn_dirent_join(dst_path,
                                                       entryname_utf8,
                                                       subpool);
              SVN_ERR(svn_io_copy_link(src_target, dst_target,
                                       subpool));
            }
          else if (this_entry.filetype == APR_DIR) /* recurse */
            {
              const char *src_target;

              /* Prevent infinite recursion by filtering off our
                 newly created destination path. */
              if (strcmp(src, dst_parent) == 0
                  && strcmp(entryname_utf8, dst_basename) == 0)
                continue;

              src_target = svn_dirent_join(src, entryname_utf8, subpool);
              SVN_ERR(hotcopy_io_copy_dir_recursively(skipped_p,
                                                      src_target,
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
                              svn_dirent_local_style(src, scratch_pool));

  status = apr_dir_close(this_dir);
  if (status)
    return svn_error_wrap_apr(status, _("Error closing directory '%s'"),
                              svn_dirent_local_style(src, scratch_pool));

  /* Free any memory used by recursion */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Copy an un-packed revision or revprop file for revision REV from SRC_SUBDIR
 * to DST_SUBDIR. Assume a sharding layout based on MAX_FILES_PER_DIR.
 * Set *SKIPPED_P to FALSE only if the file was copied, do not change the
 * value in *SKIPPED_P otherwise. SKIPPED_P may be NULL if not required.
 * If PROPS is set, copy the revprops file, otherwise copy the rev data file.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
hotcopy_copy_shard_file(svn_boolean_t *skipped_p,
                        const char *src_subdir,
                        const char *dst_subdir,
                        svn_revnum_t rev,
                        int max_files_per_dir,
                        svn_boolean_t props,
                        apr_pool_t *scratch_pool)
{
  const char *src_subdir_shard = src_subdir,
             *dst_subdir_shard = dst_subdir;

  const char *shard = apr_psprintf(scratch_pool, "%ld",
                                   rev / max_files_per_dir);
  src_subdir_shard = svn_dirent_join(src_subdir, shard, scratch_pool);
  dst_subdir_shard = svn_dirent_join(dst_subdir, shard, scratch_pool);

  if (rev % max_files_per_dir == 0)
    {
      SVN_ERR(svn_io_make_dir_recursively(dst_subdir_shard, scratch_pool));
      SVN_ERR(svn_io_copy_perms(dst_subdir, dst_subdir_shard,
                                scratch_pool));
    }

  SVN_ERR(hotcopy_io_dir_file_copy(skipped_p,
                                   src_subdir_shard, dst_subdir_shard,
                                   apr_psprintf(scratch_pool, "%c%ld",
                                                props ? 'p' : 'r',
                                                rev),
                                   scratch_pool));
  return SVN_NO_ERROR;
}


/* Copy a packed shard containing revision REV, and which contains
 * MAX_FILES_PER_DIR revisions, from SRC_FS to DST_FS.
 * Update *DST_MIN_UNPACKED_REV in case the shard is new in DST_FS.
 * Do not re-copy data which already exists in DST_FS.
 * Set *SKIPPED_P to FALSE only if at least one part of the shard
 * was copied, do not change the value in *SKIPPED_P otherwise.
 * SKIPPED_P may be NULL if not required.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
hotcopy_copy_packed_shard(svn_boolean_t *skipped_p,
                          svn_revnum_t *dst_min_unpacked_rev,
                          svn_fs_t *src_fs,
                          svn_fs_t *dst_fs,
                          svn_revnum_t rev,
                          int max_files_per_dir,
                          apr_pool_t *scratch_pool)
{
  const char *src_subdir;
  const char *dst_subdir;
  const char *packed_shard;
  const char *src_subdir_packed_shard;

  /* Copy the packed shard. */
  src_subdir = svn_dirent_join(src_fs->path, PATH_REVS_DIR, scratch_pool);
  dst_subdir = svn_dirent_join(dst_fs->path, PATH_REVS_DIR, scratch_pool);
  packed_shard = apr_psprintf(scratch_pool, "%ld" PATH_EXT_PACKED_SHARD,
                              rev / max_files_per_dir);
  src_subdir_packed_shard = svn_dirent_join(src_subdir, packed_shard,
                                            scratch_pool);
  SVN_ERR(hotcopy_io_copy_dir_recursively(skipped_p, src_subdir_packed_shard,
                                          dst_subdir, packed_shard,
                                          TRUE /* copy_perms */,
                                          NULL /* cancel_func */, NULL,
                                          scratch_pool));

  /* If necessary, update the min-unpacked rev file in the hotcopy. */
  if (*dst_min_unpacked_rev < rev + max_files_per_dir)
    {
      *dst_min_unpacked_rev = rev + max_files_per_dir;
      SVN_ERR(svn_fs_x__write_min_unpacked_rev(dst_fs,
                                               *dst_min_unpacked_rev,
                                               scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Remove file PATH, if it exists - even if it is read-only.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
hotcopy_remove_file(const char *path,
                    apr_pool_t *scratch_pool)
{
  /* Make the rev file writable and remove it. */
  SVN_ERR(svn_io_set_file_read_write(path, TRUE, scratch_pool));
  SVN_ERR(svn_io_remove_file2(path, TRUE, scratch_pool));

  return SVN_NO_ERROR;
}

/* Verify that DST_FS is a suitable destination for an incremental
 * hotcopy from SRC_FS. */
static svn_error_t *
hotcopy_incremental_check_preconditions(svn_fs_t *src_fs,
                                        svn_fs_t *dst_fs)
{
  svn_fs_x__data_t *src_ffd = src_fs->fsap_data;
  svn_fs_x__data_t *dst_ffd = dst_fs->fsap_data;

  /* We only support incremental hotcopy between the same format. */
  if (src_ffd->format != dst_ffd->format)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
      _("The FSX format (%d) of the hotcopy source does not match the "
        "FSX format (%d) of the hotcopy destination; please upgrade "
        "both repositories to the same format"),
      src_ffd->format, dst_ffd->format);

  /* Make sure the UUID of source and destination match up.
   * We don't want to copy over a different repository. */
  if (strcmp(src_fs->uuid, dst_fs->uuid) != 0)
    return svn_error_create(SVN_ERR_RA_UUID_MISMATCH, NULL,
                            _("The UUID of the hotcopy source does "
                              "not match the UUID of the hotcopy "
                              "destination"));

  /* Also require same shard size. */
  if (src_ffd->max_files_per_dir != dst_ffd->max_files_per_dir)
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                            _("The sharding layout configuration "
                              "of the hotcopy source does not match "
                              "the sharding layout configuration of "
                              "the hotcopy destination"));
  return SVN_NO_ERROR;
}

/* Copy the revision and revprop files (possibly sharded / packed) from
 * SRC_FS to DST_FS.  Do not re-copy data which already exists in DST_FS.
 * When copying packed or unpacked shards, checkpoint the result in DST_FS
 * for every shard by updating the 'current' file if necessary.  Assume
 * the >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT filesystem format without
 * global next-ID counters.  Indicate progress via the optional NOTIFY_FUNC
 * callback using NOTIFY_BATON.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
hotcopy_revisions(svn_fs_t *src_fs,
                  svn_fs_t *dst_fs,
                  svn_revnum_t src_youngest,
                  svn_revnum_t dst_youngest,
                  svn_boolean_t incremental,
                  const char *src_revs_dir,
                  const char *dst_revs_dir,
                  svn_fs_hotcopy_notify_t notify_func,
                  void* notify_baton,
                  svn_cancel_func_t cancel_func,
                  void* cancel_baton,
                  apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *src_ffd = src_fs->fsap_data;
  int max_files_per_dir = src_ffd->max_files_per_dir;
  svn_revnum_t src_min_unpacked_rev;
  svn_revnum_t dst_min_unpacked_rev;
  svn_revnum_t rev;
  apr_pool_t *iterpool;

  /* Copy the min unpacked rev, and read its value. */
  SVN_ERR(svn_fs_x__read_min_unpacked_rev(&src_min_unpacked_rev, src_fs,
                                          scratch_pool));
  SVN_ERR(svn_fs_x__read_min_unpacked_rev(&dst_min_unpacked_rev, dst_fs,
                                          scratch_pool));

  /* We only support packs coming from the hotcopy source.
    * The destination should not be packed independently from
    * the source. This also catches the case where users accidentally
    * swap the source and destination arguments. */
  if (src_min_unpacked_rev < dst_min_unpacked_rev)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("The hotcopy destination already contains "
                               "more packed revisions (%lu) than the "
                               "hotcopy source contains (%lu)"),
                             dst_min_unpacked_rev - 1,
                             src_min_unpacked_rev - 1);

  SVN_ERR(svn_io_dir_file_copy(src_fs->path, dst_fs->path,
                               PATH_MIN_UNPACKED_REV, scratch_pool));

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /*
   * Copy the necessary rev files.
   */

  iterpool = svn_pool_create(scratch_pool);
  /* First, copy packed shards. */
  for (rev = 0; rev < src_min_unpacked_rev; rev += max_files_per_dir)
    {
      svn_boolean_t skipped = TRUE;
      svn_revnum_t pack_end_rev;

      svn_pool_clear(iterpool);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* Copy the packed shard. */
      SVN_ERR(hotcopy_copy_packed_shard(&skipped, &dst_min_unpacked_rev,
                                        src_fs, dst_fs,
                                        rev, max_files_per_dir,
                                        iterpool));

      pack_end_rev = rev + max_files_per_dir - 1;

      /* Whenever this pack did not previously exist in the destination,
       * update 'current' to the most recent packed rev (so readers can see
       * new revisions which arrived in this pack). */
      if (pack_end_rev > dst_youngest)
        {
          SVN_ERR(svn_fs_x__write_current(dst_fs, pack_end_rev, iterpool));
        }

      /* When notifying about packed shards, make things simpler by either
       * reporting a full revision range, i.e [pack start, pack end] or
       * reporting nothing. There is one case when this approach might not
       * be exact (incremental hotcopy with a pack replacing last unpacked
       * revisions), but generally this is good enough. */
      if (notify_func && !skipped)
        notify_func(notify_baton, rev, pack_end_rev, iterpool);

      /* Now that all revisions have moved into the pack, the original
       * rev dir can be removed. */
      SVN_ERR(svn_io_remove_dir2(svn_fs_x__path_shard(dst_fs, rev, iterpool),
                                 TRUE, cancel_func, cancel_baton, iterpool));
    }

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR_ASSERT(rev == src_min_unpacked_rev);
  SVN_ERR_ASSERT(src_min_unpacked_rev == dst_min_unpacked_rev);

  /* Now, copy pairs of non-packed revisions and revprop files.
   * If necessary, update 'current' after copying all files from a shard. */
  for (; rev <= src_youngest; rev++)
    {
      svn_boolean_t skipped = TRUE;

      svn_pool_clear(iterpool);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* Copying non-packed revisions is racy in case the source repository is
       * being packed concurrently with this hotcopy operation.  With the pack
       * lock, however, the race is impossible, because hotcopy and pack
       * operations block each other.
       *
       * We assume that all revisions coming after 'min-unpacked-rev' really
       * are unpacked and that's not necessarily true with concurrent packing.
       * Don't try to be smart in this edge case, because handling it properly
       * might require copying *everything* from the start. Just abort the
       * hotcopy with an ENOENT (revision file moved to a pack, so it is no
       * longer where we expect it to be). */

      /* Copy the rev file. */
      SVN_ERR(hotcopy_copy_shard_file(&skipped, src_revs_dir, dst_revs_dir,
                                      rev, max_files_per_dir, FALSE,
                                      iterpool));

      /* Copy the revprop file. */
      SVN_ERR(hotcopy_copy_shard_file(&skipped, src_revs_dir, dst_revs_dir,
                                      rev, max_files_per_dir, TRUE,
                                      iterpool));

      /* Whenever this revision did not previously exist in the destination,
       * checkpoint the progress via 'current' (do that once per full shard
       * in order not to slow things down). */
      if (rev > dst_youngest)
        {
          if (max_files_per_dir && (rev % max_files_per_dir == 0))
            {
              SVN_ERR(svn_fs_x__write_current(dst_fs, rev, iterpool));
            }
        }

      if (notify_func && !skipped)
        notify_func(notify_baton, rev, rev, iterpool);
    }
  svn_pool_destroy(iterpool);

  /* We assume that all revisions were copied now, i.e. we didn't exit the
   * above loop early. 'rev' was last incremented during exit of the loop. */
  SVN_ERR_ASSERT(rev == src_youngest + 1);

  return SVN_NO_ERROR;
}

/* Baton for hotcopy_body(). */
typedef struct hotcopy_body_baton_t {
  svn_fs_t *src_fs;
  svn_fs_t *dst_fs;
  svn_boolean_t incremental;
  svn_fs_hotcopy_notify_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
} hotcopy_body_baton_t;

/* Perform a hotcopy, either normal or incremental.
 *
 * Normal hotcopy assumes that the destination exists as an empty
 * directory. It behaves like an incremental hotcopy except that
 * none of the copied files already exist in the destination.
 *
 * An incremental hotcopy copies only changed or new files to the destination,
 * and removes files from the destination no longer present in the source.
 * While the incremental hotcopy is running, readers should still be able
 * to access the destination repository without error and should not see
 * revisions currently in progress of being copied. Readers are able to see
 * new fully copied revisions even if the entire incremental hotcopy procedure
 * has not yet completed.
 *
 * Writers are blocked out completely during the entire incremental hotcopy
 * process to ensure consistency. This function assumes that the repository
 * write-lock is held.
 */
static svn_error_t *
hotcopy_body(void *baton,
             apr_pool_t *scratch_pool)
{
  hotcopy_body_baton_t *hbb = baton;
  svn_fs_t *src_fs = hbb->src_fs;
  svn_fs_t *dst_fs = hbb->dst_fs;
  svn_boolean_t incremental = hbb->incremental;
  svn_fs_hotcopy_notify_t notify_func = hbb->notify_func;
  void* notify_baton = hbb->notify_baton;
  svn_cancel_func_t cancel_func = hbb->cancel_func;
  void* cancel_baton = hbb->cancel_baton;
  svn_revnum_t src_youngest;
  svn_revnum_t dst_youngest;
  const char *src_revs_dir;
  const char *dst_revs_dir;
  const char *src_subdir;
  const char *dst_subdir;
  svn_node_kind_t kind;

  /* Try to copy the config.
   *
   * ### We try copying the config file before doing anything else,
   * ### because higher layers will abort the hotcopy if we throw
   * ### an error from this function, and that renders the hotcopy
   * ### unusable anyway. */
  SVN_ERR(svn_io_dir_file_copy(src_fs->path, dst_fs->path, PATH_CONFIG,
                               scratch_pool));

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Find the youngest revision in the source and destination.
   * We only support hotcopies from sources with an equal or greater amount
   * of revisions than the destination.
   * This also catches the case where users accidentally swap the
   * source and destination arguments. */
  SVN_ERR(svn_fs_x__read_current(&src_youngest, src_fs, scratch_pool));
  if (incremental)
    {
      SVN_ERR(svn_fs_x__youngest_rev(&dst_youngest, dst_fs, scratch_pool));
      if (src_youngest < dst_youngest)
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                 _("The hotcopy destination already contains more revisions "
                   "(%lu) than the hotcopy source contains (%lu); are source "
                   "and destination swapped?"),
                   dst_youngest, src_youngest);
    }
  else
    dst_youngest = 0;

  src_revs_dir = svn_dirent_join(src_fs->path, PATH_REVS_DIR, scratch_pool);
  dst_revs_dir = svn_dirent_join(dst_fs->path, PATH_REVS_DIR, scratch_pool);

  /* Ensure that the required folders exist in the destination
   * before actually copying the revisions and revprops. */
  SVN_ERR(svn_io_make_dir_recursively(dst_revs_dir, scratch_pool));
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Split the logic for new and old FS formats. The latter is much simpler
   * due to the absense of sharding and packing. However, it requires special
   * care when updating the 'current' file (which contains not just the
   * revision number, but also the next-ID counters). */
  SVN_ERR(hotcopy_revisions(src_fs, dst_fs, src_youngest, dst_youngest,
                            incremental, src_revs_dir, dst_revs_dir,
                            notify_func, notify_baton,
                            cancel_func, cancel_baton, scratch_pool));
  SVN_ERR(svn_fs_x__write_current(dst_fs, src_youngest, scratch_pool));

  /* Replace the locks tree.
   * This is racy in case readers are currently trying to list locks in
   * the destination. However, we need to get rid of stale locks.
   * This is the simplest way of doing this, so we accept this small race. */
  dst_subdir = svn_dirent_join(dst_fs->path, PATH_LOCKS_DIR, scratch_pool);
  SVN_ERR(svn_io_remove_dir2(dst_subdir, TRUE, cancel_func, cancel_baton,
                             scratch_pool));
  src_subdir = svn_dirent_join(src_fs->path, PATH_LOCKS_DIR, scratch_pool);
  SVN_ERR(svn_io_check_path(src_subdir, &kind, scratch_pool));
  if (kind == svn_node_dir)
    SVN_ERR(svn_io_copy_dir_recursively(src_subdir, dst_fs->path,
                                        PATH_LOCKS_DIR, TRUE,
                                        cancel_func, cancel_baton,
                                        scratch_pool));

  /*
   * NB: Data copied below is only read by writers, not readers.
   *     Writers are still locked out at this point.
   */

  /* Copy the rep cache and then remove entries for revisions
   * younger than the destination's youngest revision. */
  src_subdir = svn_dirent_join(src_fs->path, REP_CACHE_DB_NAME, scratch_pool);
  dst_subdir = svn_dirent_join(dst_fs->path, REP_CACHE_DB_NAME, scratch_pool);
  SVN_ERR(svn_io_check_path(src_subdir, &kind, scratch_pool));
  if (kind == svn_node_file)
    {
      /* Copy the rep cache and then remove entries for revisions
       * that did not make it into the destination. */
      SVN_ERR(svn_sqlite__hotcopy(src_subdir, dst_subdir, scratch_pool));

      /* The source might have r/o flags set on it - which would be
         carried over to the copy. */
      SVN_ERR(svn_io_set_file_read_write(dst_subdir, FALSE, scratch_pool));
      SVN_ERR(svn_fs_x__del_rep_reference(dst_fs, src_youngest,
                                          scratch_pool));
    }

  /* Copy the txn-current file. */
  SVN_ERR(svn_io_dir_file_copy(src_fs->path, dst_fs->path,
                                PATH_TXN_CURRENT, scratch_pool));

  /* If a revprop generation file exists in the source filesystem,
   * reset it to zero (since this is on a different path, it will not
   * overlap with data already in cache).  Also, clean up stale files
   * used for the named atomics implementation. */
  SVN_ERR(svn_fs_x__reset_revprop_generation_file(dst_fs, scratch_pool));

  /* Hotcopied FS is complete. Stamp it with a format file. */
  SVN_ERR(svn_fs_x__write_format(dst_fs, TRUE, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__hotcopy(svn_fs_t *src_fs,
                  svn_fs_t *dst_fs,
                  const char *src_path,
                  const char *dst_path,
                  svn_boolean_t incremental,
                  svn_fs_hotcopy_notify_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_mutex__t *common_pool_lock,
                  apr_pool_t *scratch_pool,
                  apr_pool_t *common_pool)
{
  hotcopy_body_baton_t hbb;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR(svn_fs_x__open(src_fs, src_path, scratch_pool));

  if (incremental)
    {
      const char *dst_format_abspath;
      svn_node_kind_t dst_format_kind;

      /* Check destination format to be sure we know how to incrementally
       * hotcopy to the destination FS. */
      dst_format_abspath = svn_dirent_join(dst_path, PATH_FORMAT,
                                           scratch_pool);
      SVN_ERR(svn_io_check_path(dst_format_abspath, &dst_format_kind,
                                scratch_pool));
      if (dst_format_kind == svn_node_none)
        {
          /* No destination?  Fallback to a non-incremental hotcopy. */
          incremental = FALSE;
        }
    }

  if (incremental)
    {
      /* Check the existing repository. */
      SVN_ERR(svn_fs_x__open(dst_fs, dst_path, scratch_pool));
      SVN_ERR(hotcopy_incremental_check_preconditions(src_fs, dst_fs));

      SVN_ERR(svn_fs_x__initialize_shared_data(dst_fs, common_pool_lock,
                                               scratch_pool, common_pool));
      SVN_ERR(svn_fs_x__initialize_caches(dst_fs, scratch_pool));
    }
  else
    {
      /* Start out with an empty destination using the same configuration
       * as the source. */
      svn_fs_x__data_t *src_ffd = src_fs->fsap_data;

      /* Create the DST_FS repository with the same layout as SRC_FS. */
      SVN_ERR(svn_fs_x__create_file_tree(dst_fs, dst_path, src_ffd->format,
                                         src_ffd->max_files_per_dir,
                                         scratch_pool));

      /* Copy the UUID.  Hotcopy destination receives a new instance ID, but
       * has the same filesystem UUID as the source. */
      SVN_ERR(svn_fs_x__set_uuid(dst_fs, src_fs->uuid, NULL, TRUE,
                                 scratch_pool));

      /* Remove revision 0 contents.  Otherwise, it may not get overwritten
       * due to having a newer timestamp. */
      SVN_ERR(hotcopy_remove_file(svn_fs_x__path_rev(dst_fs, 0,
                                                     scratch_pool),
                                  scratch_pool));
      SVN_ERR(hotcopy_remove_file(svn_fs_x__path_revprops(dst_fs, 0,
                                                          scratch_pool),
                                  scratch_pool));

      SVN_ERR(svn_fs_x__initialize_shared_data(dst_fs, common_pool_lock,
                                               scratch_pool, common_pool));
      SVN_ERR(svn_fs_x__initialize_caches(dst_fs, scratch_pool));
    }

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  hbb.src_fs = src_fs;
  hbb.dst_fs = dst_fs;
  hbb.incremental = incremental;
  hbb.notify_func = notify_func;
  hbb.notify_baton = notify_baton;
  hbb.cancel_func = cancel_func;
  hbb.cancel_baton = cancel_baton;

  /* Lock the destination in the incremental mode.  For a non-incremental
   * hotcopy, don't take any locks.  In that case the destination cannot be
   * opened until the hotcopy finishes, and we don't have to worry about
   * concurrency. */
  if (incremental)
    SVN_ERR(svn_fs_x__with_all_locks(dst_fs, hotcopy_body, &hbb,
                                     scratch_pool));
  else
    SVN_ERR(hotcopy_body(&hbb, scratch_pool));

  return SVN_NO_ERROR;
}
