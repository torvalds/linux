/* util.c --- utility functions for FSFS repo access
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

#include "svn_ctype.h"
#include "svn_dirent_uri.h"
#include "private/svn_string_private.h"

#include "fs_fs.h"
#include "pack.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

svn_boolean_t
svn_fs_fs__is_packed_rev(svn_fs_t *fs,
                         svn_revnum_t rev)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  return (rev < ffd->min_unpacked_rev);
}

svn_boolean_t
svn_fs_fs__is_packed_revprop(svn_fs_t *fs,
                             svn_revnum_t rev)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  /* rev 0 will not be packed */
  return (rev < ffd->min_unpacked_rev)
      && (rev != 0)
      && (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT);
}

svn_revnum_t
svn_fs_fs__packed_base_rev(svn_fs_t *fs,
                           svn_revnum_t revision)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  return (revision < ffd->min_unpacked_rev)
       ? (revision - (revision % ffd->max_files_per_dir))
       : revision;
}

const char *
svn_fs_fs__path_txn_current(svn_fs_t *fs,
                            apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_TXN_CURRENT, pool);
}

const char *
svn_fs_fs__path_txn_current_lock(svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_TXN_CURRENT_LOCK, pool);
}

const char *
svn_fs_fs__path_lock(svn_fs_t *fs,
                     apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_LOCK_FILE, pool);
}

const char *
svn_fs_fs__path_pack_lock(svn_fs_t *fs,
                          apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_PACK_LOCK_FILE, pool);
}

const char *
svn_fs_fs__path_revprop_generation(svn_fs_t *fs,
                                   apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_REVPROP_GENERATION, pool);
}

const char *
svn_fs_fs__path_rev_packed(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *kind,
                           apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  assert(svn_fs_fs__is_packed_rev(fs, rev));

  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool,
                                           "%ld" PATH_EXT_PACKED_SHARD,
                                           rev / ffd->max_files_per_dir),
                              kind, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_rev_shard(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool, "%ld",
                                                 rev / ffd->max_files_per_dir),
                              SVN_VA_NULL);
}

const char *
svn_fs_fs__path_rev(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(! svn_fs_fs__is_packed_rev(fs, rev));

  if (ffd->max_files_per_dir)
    {
      return svn_dirent_join(svn_fs_fs__path_rev_shard(fs, rev, pool),
                             apr_psprintf(pool, "%ld", rev),
                             pool);
    }

  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool, "%ld", rev), SVN_VA_NULL);
}

/* Set *PATH to the path of REV in FS with PACKED selecting whether the
   (potential) pack file or single revision file name is returned.
   Allocate *PATH in POOL.
*/
static const char *
path_rev_absolute_internal(svn_fs_t *fs,
                           svn_revnum_t rev,
                           svn_boolean_t packed,
                           apr_pool_t *pool)
{
  return packed
       ? svn_fs_fs__path_rev_packed(fs, rev, PATH_PACKED, pool)
       : svn_fs_fs__path_rev(fs, rev, pool);
}

const char *
svn_fs_fs__path_rev_absolute(svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t is_packed = ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT
                         && svn_fs_fs__is_packed_rev(fs, rev);

  return path_rev_absolute_internal(fs, rev, is_packed, pool);
}

const char *
svn_fs_fs__path_revprops_shard(svn_fs_t *fs,
                               svn_revnum_t rev,
                               apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld",
                                           rev / ffd->max_files_per_dir),
                              SVN_VA_NULL);
}

const char *
svn_fs_fs__path_revprops_pack_shard(svn_fs_t *fs,
                                    svn_revnum_t rev,
                                    apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld" PATH_EXT_PACKED_SHARD,
                                           rev / ffd->max_files_per_dir),
                              SVN_VA_NULL);
}

const char *
svn_fs_fs__path_revprops(svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  if (ffd->max_files_per_dir)
    {
      return svn_dirent_join(svn_fs_fs__path_revprops_shard(fs, rev, pool),
                             apr_psprintf(pool, "%ld", rev),
                             pool);
    }

  return svn_dirent_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                              apr_psprintf(pool, "%ld", rev), SVN_VA_NULL);
}

/* Return TO_ADD appended to the C string representation of TXN_ID.
 * Allocate the result in POOL.
 */
static const char *
combine_txn_id_string(const svn_fs_fs__id_part_t *txn_id,
                      const char *to_add,
                      apr_pool_t *pool)
{
  return apr_pstrcat(pool, svn_fs_fs__id_txn_unparse(txn_id, pool),
                     to_add, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_txns_dir(svn_fs_t *fs,
                         apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_TXNS_DIR, pool);
}

const char *
svn_fs_fs__path_txn_dir(svn_fs_t *fs,
                        const svn_fs_fs__id_part_t *txn_id,
                        apr_pool_t *pool)
{
  SVN_ERR_ASSERT_NO_RETURN(txn_id != NULL);
  return svn_dirent_join(svn_fs_fs__path_txns_dir(fs, pool),
                         combine_txn_id_string(txn_id, PATH_EXT_TXN, pool),
                         pool);
}

const char*
svn_fs_fs__path_l2p_proto_index(svn_fs_t *fs,
                                const svn_fs_fs__id_part_t *txn_id,
                                apr_pool_t *pool)
{
  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                         PATH_INDEX PATH_EXT_L2P_INDEX, pool);
}

const char*
svn_fs_fs__path_p2l_proto_index(svn_fs_t *fs,
                                const svn_fs_fs__id_part_t *txn_id,
                                apr_pool_t *pool)
{
  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                         PATH_INDEX PATH_EXT_P2L_INDEX, pool);
}

const char *
svn_fs_fs__path_txn_item_index(svn_fs_t *fs,
                               const svn_fs_fs__id_part_t *txn_id,
                               apr_pool_t *pool)
{
  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                         PATH_TXN_ITEM_INDEX, pool);
}

const char *
svn_fs_fs__path_txn_proto_revs(svn_fs_t *fs,
                               apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_TXN_PROTOS_DIR, pool);
}

const char *
svn_fs_fs__path_txn_proto_rev(svn_fs_t *fs,
                              const svn_fs_fs__id_part_t *txn_id,
                              apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    return svn_dirent_join(svn_fs_fs__path_txn_proto_revs(fs, pool),
                           combine_txn_id_string(txn_id, PATH_EXT_REV, pool),
                           pool);
  else
    return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                           PATH_REV, pool);
}


const char *
svn_fs_fs__path_txn_proto_rev_lock(svn_fs_t *fs,
                                   const svn_fs_fs__id_part_t *txn_id,
                                   apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (ffd->format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    return svn_dirent_join(svn_fs_fs__path_txn_proto_revs(fs, pool),
                           combine_txn_id_string(txn_id, PATH_EXT_REV_LOCK,
                                                 pool),
                           pool);
  else
    return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                           PATH_REV_LOCK, pool);
}

const char *
svn_fs_fs__path_txn_node_rev(svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             apr_pool_t *pool)
{
  char *filename = (char *)svn_fs_fs__id_unparse(id, pool)->data;
  *strrchr(filename, '.') = '\0';

  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, svn_fs_fs__id_txn_id(id),
                                                 pool),
                         apr_psprintf(pool, PATH_PREFIX_NODE "%s",
                                      filename),
                         pool);
}

const char *
svn_fs_fs__path_txn_node_props(svn_fs_t *fs,
                               const svn_fs_id_t *id,
                               apr_pool_t *pool)
{
  return apr_pstrcat(pool, svn_fs_fs__path_txn_node_rev(fs, id, pool),
                     PATH_EXT_PROPS, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_txn_node_children(svn_fs_t *fs,
                                  const svn_fs_id_t *id,
                                  apr_pool_t *pool)
{
  return apr_pstrcat(pool, svn_fs_fs__path_txn_node_rev(fs, id, pool),
                     PATH_EXT_CHILDREN, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_node_origin(svn_fs_t *fs,
                            const svn_fs_fs__id_part_t *node_id,
                            apr_pool_t *pool)
{
  char buffer[SVN_INT64_BUFFER_SIZE];
  apr_size_t len = svn__ui64tobase36(buffer, node_id->number);

  if (len > 1)
    buffer[len - 1] = '\0';

  return svn_dirent_join_many(pool, fs->path, PATH_NODE_ORIGINS_DIR,
                              buffer, SVN_VA_NULL);
}

const char *
svn_fs_fs__path_min_unpacked_rev(svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_MIN_UNPACKED_REV, pool);
}

svn_error_t *
svn_fs_fs__check_file_buffer_numeric(const char *buf,
                                     apr_off_t offset,
                                     const char *path,
                                     const char *title,
                                     apr_pool_t *pool)
{
  const char *p;

  for (p = buf + offset; *p; p++)
    if (!svn_ctype_isdigit(*p))
      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
        _("%s file '%s' contains unexpected non-digit '%c' within '%s'"),
        title, svn_dirent_local_style(path, pool), *p, buf);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_min_unpacked_rev(svn_revnum_t *min_unpacked_rev,
                                 svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  char buf[80];
  apr_file_t *file;
  apr_size_t len;

  SVN_ERR(svn_io_file_open(&file,
                           svn_fs_fs__path_min_unpacked_rev(fs, pool),
                           APR_READ | APR_BUFFERED,
                           APR_OS_DEFAULT,
                           pool));
  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &len, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  SVN_ERR(svn_revnum_parse(min_unpacked_rev, buf, NULL));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__update_min_unpacked_rev(svn_fs_t *fs,
                                   apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR_ASSERT(ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT);

  return svn_fs_fs__read_min_unpacked_rev(&ffd->min_unpacked_rev, fs, pool);
}

svn_error_t *
svn_fs_fs__write_min_unpacked_rev(svn_fs_t *fs,
                                  svn_revnum_t revnum,
                                  apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *final_path;
  char buf[SVN_INT64_BUFFER_SIZE];
  apr_size_t len = svn__i64toa(buf, revnum);
  buf[len] = '\n';

  final_path = svn_fs_fs__path_min_unpacked_rev(fs, scratch_pool);

  SVN_ERR(svn_io_write_atomic2(final_path, buf, len + 1,
                               final_path /* copy_perms */,
                               ffd->flush_to_disk, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_current(svn_revnum_t *rev,
                        apr_uint64_t *next_node_id,
                        apr_uint64_t *next_copy_id,
                        svn_fs_t *fs,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stringbuf_t *content;

  SVN_ERR(svn_fs_fs__read_content(&content,
                                  svn_fs_fs__path_current(fs, pool),
                                  pool));

  if (ffd->format >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
    {
      /* When format 1 and 2 filesystems are upgraded, the 'current' file is
         left intact.  As a consequence, there is a window when a filesystem
         has a new format, but this file still contains the IDs left from an
         old format, i.e. looks like "359 j5 v\n".  Do not be too strict here
         and only expect a parseable revision number. */
      SVN_ERR(svn_revnum_parse(rev, content->data, NULL));

      *next_node_id = 0;
      *next_copy_id = 0;
    }
  else
    {
      const char *str;

      SVN_ERR(svn_revnum_parse(rev, content->data, &str));
      if (*str != ' ')
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Corrupt 'current' file"));

      *next_node_id = svn__base36toui64(&str, str + 1);
      if (*str != ' ')
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Corrupt 'current' file"));

      *next_copy_id = svn__base36toui64(&str, str + 1);
      if (*str != '\n')
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Corrupt 'current' file"));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__write_current(svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_uint64_t next_node_id,
                         apr_uint64_t next_copy_id,
                         apr_pool_t *pool)
{
  char *buf;
  const char *name;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* Now we can just write out this line. */
  if (ffd->format >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
    {
      buf = apr_psprintf(pool, "%ld\n", rev);
    }
  else
    {
      char node_id_str[SVN_INT64_BUFFER_SIZE];
      char copy_id_str[SVN_INT64_BUFFER_SIZE];
      svn__ui64tobase36(node_id_str, next_node_id);
      svn__ui64tobase36(copy_id_str, next_copy_id);

      buf = apr_psprintf(pool, "%ld %s %s\n", rev, node_id_str, copy_id_str);
    }

  name = svn_fs_fs__path_current(fs, pool);
  SVN_ERR(svn_io_write_atomic2(name, buf, strlen(buf),
                               name /* copy_perms_path */,
                               ffd->flush_to_disk, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__try_stringbuf_from_file(svn_stringbuf_t **content,
                                   svn_boolean_t *missing,
                                   const char *path,
                                   svn_boolean_t last_attempt,
                                   apr_pool_t *pool)
{
  svn_error_t *err = svn_stringbuf_from_file2(content, path, pool);
  if (missing)
    *missing = FALSE;

  if (err)
    {
      *content = NULL;

      if (APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (!last_attempt)
            {
              svn_error_clear(err);
              if (missing)
                *missing = TRUE;
              return SVN_NO_ERROR;
            }
        }
#ifdef ESTALE
      else if (APR_TO_OS_ERROR(err->apr_err) == ESTALE
                || APR_TO_OS_ERROR(err->apr_err) == EIO)
        {
          if (!last_attempt)
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
        }
#endif
    }

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_fs__read_content(svn_stringbuf_t **content,
                        const char *fname,
                        apr_pool_t *pool)
{
  int i;
  *content = NULL;

  for (i = 0; !*content && (i < SVN_FS_FS__RECOVERABLE_RETRY_COUNT); ++i)
    SVN_ERR(svn_fs_fs__try_stringbuf_from_file(content, NULL,
                        fname, i + 1 < SVN_FS_FS__RECOVERABLE_RETRY_COUNT,
                        pool));

  if (!*content)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't read '%s'"),
                             svn_dirent_local_style(fname, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_number_from_stream(apr_int64_t *result,
                                   svn_boolean_t *hit_eof,
                                   svn_stream_t *stream,
                                   apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *sb;
  svn_boolean_t eof;
  svn_error_t *err;

  SVN_ERR(svn_stream_readline(stream, &sb, "\n", &eof, scratch_pool));
  if (hit_eof)
    *hit_eof = eof;
  else
    if (eof)
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL, _("Unexpected EOF"));

  if (!eof)
    {
      err = svn_cstring_atoi64(result, sb->data);
      if (err)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                                 _("Number '%s' invalid or too large"),
                                 sb->data);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__move_into_place(const char *old_filename,
                           const char *new_filename,
                           const char *perms_reference,
                           svn_boolean_t flush_to_disk,
                           apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *file;

  /* Copying permissions is a no-op on WIN32. */
  SVN_ERR(svn_io_copy_perms(perms_reference, old_filename, pool));

  /* Move the file into place. */
  err = svn_io_file_rename2(old_filename, new_filename, flush_to_disk, pool);
  if (err && APR_STATUS_IS_EXDEV(err->apr_err))
    {
      /* Can't rename across devices; fall back to copying. */
      svn_error_clear(err);
      SVN_ERR(svn_io_copy_file(old_filename, new_filename, TRUE, pool));

      /* Flush the target of the copy to disk.
         ### The code below is duplicates svn_io_file_rename2(), because
             currently we don't have the svn_io_copy_file2() function with
             a flush_to_disk argument. */
      if (flush_to_disk)
        {
          SVN_ERR(svn_io_file_open(&file, new_filename, APR_WRITE,
                                   APR_OS_DEFAULT, pool));
          SVN_ERR(svn_io_file_flush_to_disk(file, pool));
          SVN_ERR(svn_io_file_close(file, pool));
        }

#ifdef SVN_ON_POSIX
      if (flush_to_disk)
        {
          /* On POSIX, the file name is stored in the file's directory entry.
             Hence, we need to fsync() that directory as well.
             On other operating systems, we'd only be asking for trouble
             by trying to open and fsync a directory. */
          const char *dirname;

          dirname = svn_dirent_dirname(new_filename, pool);
          SVN_ERR(svn_io_file_open(&file, dirname, APR_READ, APR_OS_DEFAULT,
                                   pool));
          SVN_ERR(svn_io_file_flush_to_disk(file, pool));
          SVN_ERR(svn_io_file_close(file, pool));
        }
#endif
    }
  else if (err)
    return svn_error_trace(err);

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_fs_fs__use_log_addressing(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  return ffd->use_log_addressing;
}
