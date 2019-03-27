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
 * @file svn_io.h
 * @brief General file I/O for Subversion
 */

/* ==================================================================== */


#ifndef SVN_IO_H
#define SVN_IO_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_time.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_thread_proc.h>  /* for apr_proc_t, apr_exit_why_e */

#include "svn_types.h"
#include "svn_string.h"
#include "svn_checksum.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** Used as an argument when creating temporary files to indicate
 * when a file should be removed.
 *
 * @since New in 1.4.
 *
 * Not specifying any of these means no removal at all. */
typedef enum svn_io_file_del_t
{
  /** No deletion ever */
  svn_io_file_del_none = 0,
  /** Remove when the file is closed */
  svn_io_file_del_on_close,
  /** Remove when the associated pool is cleared */
  svn_io_file_del_on_pool_cleanup
} svn_io_file_del_t;

/** A set of directory entry data elements as returned by svn_io_get_dirents
 *
 * Note that the first two fields are exactly identical to svn_io_dirent_t
 * to allow returning a svn_io_dirent2_t as a svn_io_dirent_t.
 *
 * Use svn_io_dirent2_create() to create new svn_dirent2_t instances or
 * svn_io_dirent2_dup() to duplicate an existing instance.
 *
 * @since New in 1.7.
 */
typedef struct svn_io_dirent2_t {
  /* New fields must be added at the end to preserve binary compatibility */

  /** The kind of this entry. */
  svn_node_kind_t kind;

  /** If @c kind is #svn_node_file, whether this entry is a special file;
   * else FALSE.
   *
   * @see svn_io_check_special_path().
   */
  svn_boolean_t special;

  /** The filesize of this entry or undefined for a directory */
  svn_filesize_t filesize;

  /** The time the file was last modified */
  apr_time_t mtime;

  /* Don't forget to update svn_io_dirent2_dup() when adding new fields */
} svn_io_dirent2_t;


/** Creates a new #svn_io_dirent2_t structure
 *
 * @since New in 1.7.
 */
svn_io_dirent2_t *
svn_io_dirent2_create(apr_pool_t *result_pool);

/** Duplicates a @c svn_io_dirent2_t structure into @a result_pool.
 *
 * @since New in 1.7.
 */
svn_io_dirent2_t *
svn_io_dirent2_dup(const svn_io_dirent2_t *item,
                   apr_pool_t *result_pool);

/** Represents the kind and special status of a directory entry.
 *
 * Note that the first two fields are exactly identical to svn_io_dirent2_t
 * to allow returning a svn_io_dirent2_t as a svn_io_dirent_t.
 *
 * @since New in 1.3.
 */
typedef struct svn_io_dirent_t {
  /** The kind of this entry. */
  svn_node_kind_t kind;
  /** If @c kind is #svn_node_file, whether this entry is a special file;
   * else FALSE.
   *
   * @see svn_io_check_special_path().
   */
  svn_boolean_t special;
} svn_io_dirent_t;

/** Determine the @a kind of @a path.  @a path should be UTF-8 encoded.
 *
 * If @a path is a file, set @a *kind to #svn_node_file.
 *
 * If @a path is a directory, set @a *kind to #svn_node_dir.
 *
 * If @a path does not exist, set @a *kind to #svn_node_none.
 *
 * If @a path exists but is none of the above, set @a *kind to
 * #svn_node_unknown.
 *
 * If @a path is not a valid pathname, set @a *kind to #svn_node_none.  If
 * unable to determine @a path's kind for any other reason, return an error,
 * with @a *kind's value undefined.
 *
 * Use @a pool for temporary allocations.
 *
 * @see svn_node_kind_t
 */
svn_error_t *
svn_io_check_path(const char *path,
                  svn_node_kind_t *kind,
                  apr_pool_t *pool);

/**
 * Like svn_io_check_path(), but also set *is_special to @c TRUE if
 * the path is not a normal file.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_io_check_special_path(const char *path,
                          svn_node_kind_t *kind,
                          svn_boolean_t *is_special,
                          apr_pool_t *pool);

/** Like svn_io_check_path(), but resolve symlinks.  This returns the
    same varieties of @a kind as svn_io_check_path(). */
svn_error_t *
svn_io_check_resolved_path(const char *path,
                           svn_node_kind_t *kind,
                           apr_pool_t *pool);


/** Open a new file (for reading and writing) with a unique name based on
 * utf-8 encoded @a filename, in the directory @a dirpath.  The file handle is
 * returned in @a *file, and the name, which ends with @a suffix, is returned
 * in @a *unique_name, also utf8-encoded.  Either @a file or @a unique_name
 * may be @c NULL.  If @a file is @c NULL, the file will be created but not
 * open.
 *
 * The file will be deleted according to @a delete_when.  If that is
 * #svn_io_file_del_on_pool_cleanup, it refers to @a result_pool.
 *
 * The @c APR_BUFFERED flag will always be used when opening the file.
 *
 * The first attempt will just append @a suffix.  If the result is not
 * a unique name, then subsequent attempts will append a dot,
 * followed by an iteration number ("2", then "3", and so on),
 * followed by the suffix.  For example, successive calls to
 *
 *    svn_io_open_uniquely_named(&f, &u, "tests/t1/A/D/G", "pi", ".tmp", ...)
 *
 * will open
 *
 *    tests/t1/A/D/G/pi.tmp
 *    tests/t1/A/D/G/pi.2.tmp
 *    tests/t1/A/D/G/pi.3.tmp
 *    tests/t1/A/D/G/pi.4.tmp
 *    tests/t1/A/D/G/pi.5.tmp
 *    ...
 *
 * Assuming @a suffix is non-empty, @a *unique_name will never be exactly
 * the same as @a filename, even if @a filename does not exist.
 *
 * If @a dirpath is NULL, then the directory returned by svn_io_temp_dir()
 * will be used.
 *
 * If @a filename is NULL, then "tempfile" will be used.
 *
 * If @a suffix is NULL, then ".tmp" will be used.
 *
 * Allocates @a *file and @a *unique_name in @a result_pool. All
 * intermediate allocations will be performed in @a scratch_pool.
 *
 * If no unique name can be found, #SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED is
 * the error returned.
 *
 * Claim of Historical Inevitability: this function was written
 * because
 *
 *    - tmpnam() is not thread-safe.
 *    - tempname() tries standard system tmp areas first.
 *
 * @since New in 1.6
 */
svn_error_t *
svn_io_open_uniquely_named(apr_file_t **file,
                           const char **unique_name,
                           const char *dirpath,
                           const char *filename,
                           const char *suffix,
                           svn_io_file_del_t delete_when,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);


/** Create a writable file, with an arbitrary and unique name, in the
 * directory @a dirpath.  Set @a *temp_path to its full path, and set
 * @a *file to the file handle, both allocated from @a result_pool.  Either
 * @a file or @a temp_path may be @c NULL.  If @a file is @c NULL, the file
 * will be created but not open.
 *
 * If @a dirpath is @c NULL, use the path returned from svn_io_temp_dir().
 * (Note that when using the system-provided temp directory, it may not
 * be possible to atomically rename the resulting file due to cross-device
 * issues.)
 *
 * The file will be deleted according to @a delete_when.  If that is
 * #svn_io_file_del_on_pool_cleanup, it refers to @a result_pool.  If it
 * is #svn_io_file_del_on_close and @a file is @c NULL, the file will be
 * deleted before this function returns.
 *
 * When passing @c svn_io_file_del_none please don't forget to eventually
 * remove the temporary file to avoid filling up the system temp directory.
 * It is often appropriate to bind the lifetime of the temporary file to
 * the lifetime of a pool by using @c svn_io_file_del_on_pool_cleanup.
 *
 * Temporary allocations will be performed in @a scratch_pool.
 *
 * @since New in 1.6
 * @see svn_stream_open_unique()
 */
svn_error_t *
svn_io_open_unique_file3(apr_file_t **file,
                         const char **temp_path,
                         const char *dirpath,
                         svn_io_file_del_t delete_when,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


/** Like svn_io_open_uniquely_named(), but takes a joined dirpath and
 * filename, and a single pool.
 *
 * @since New in 1.4
 *
 * @deprecated Provided for backward compatibility with the 1.5 API
 */
SVN_DEPRECATED
svn_error_t *
svn_io_open_unique_file2(apr_file_t **f,
                         const char **unique_name_p,
                         const char *path,
                         const char *suffix,
                         svn_io_file_del_t delete_when,
                         apr_pool_t *pool);

/** Like svn_io_open_unique_file2, but can't delete on pool cleanup.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API
 *
 * @note In 1.4 the API was extended to require either @a f or
 *       @a unique_name_p (the other can be NULL).  Before that, both were
 *       required.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_open_unique_file(apr_file_t **f,
                        const char **unique_name_p,
                        const char *path,
                        const char *suffix,
                        svn_boolean_t delete_on_close,
                        apr_pool_t *pool);


/**
 * Like svn_io_open_unique_file(), except that instead of creating a
 * file, a symlink is generated that references the path @a dest.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_io_create_unique_link(const char **unique_name_p,
                          const char *path,
                          const char *dest,
                          const char *suffix,
                          apr_pool_t *pool);


/**
 * Set @a *dest to the path that the symlink at @a path references.
 * Allocate the string from @a pool.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_io_read_link(svn_string_t **dest,
                 const char *path,
                 apr_pool_t *pool);


/** Set @a *dir to a directory path (allocated in @a pool) deemed
 * usable for the creation of temporary files and subdirectories.
 */
svn_error_t *
svn_io_temp_dir(const char **dir,
                apr_pool_t *pool);


/** Copy @a src to @a dst atomically, in a "byte-for-byte" manner.
 * Overwrite @a dst if it exists, else create it.  Both @a src and @a dst
 * are utf8-encoded filenames.  If @a copy_perms is TRUE, set @a dst's
 * permissions to match those of @a src.
 */
svn_error_t *
svn_io_copy_file(const char *src,
                 const char *dst,
                 svn_boolean_t copy_perms,
                 apr_pool_t *pool);


/** Copy permission flags from @a src onto the file at @a dst. Both
 * filenames are utf8-encoded filenames.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_io_copy_perms(const char *src,
                  const char *dst,
                  apr_pool_t *pool);


/**
 * Copy symbolic link @a src to @a dst atomically.  Overwrite @a dst
 * if it exists, else create it.  Both @a src and @a dst are
 * utf8-encoded filenames.  After copying, the @a dst link will point
 * to the same thing @a src does.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_io_copy_link(const char *src,
                 const char *dst,
                 apr_pool_t *pool);


/** Recursively copy directory @a src into @a dst_parent, as a new entry named
 * @a dst_basename.  If @a dst_basename already exists in @a dst_parent,
 * return error.  @a copy_perms will be passed through to svn_io_copy_file()
 * when any files are copied.  @a src, @a dst_parent, and @a dst_basename are
 * all utf8-encoded.
 *
 * If @a cancel_func is non-NULL, invoke it with @a cancel_baton at
 * various points during the operation.  If it returns any error
 * (typically #SVN_ERR_CANCELLED), return that error immediately.
 */
svn_error_t *
svn_io_copy_dir_recursively(const char *src,
                            const char *dst_parent,
                            const char *dst_basename,
                            svn_boolean_t copy_perms,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *pool);


/** Create directory @a path on the file system, creating intermediate
 * directories as required, like <tt>mkdir -p</tt>.  Report no error if @a
 * path already exists.  @a path is utf8-encoded.
 *
 * This is essentially a wrapper for apr_dir_make_recursive(), passing
 * @c APR_OS_DEFAULT as the permissions.
 */
svn_error_t *
svn_io_make_dir_recursively(const char *path,
                            apr_pool_t *pool);


/** Set @a *is_empty_p to @c TRUE if directory @a path is empty, else to
 * @c FALSE if it is not empty.  @a path must be a directory, and is
 * utf8-encoded.  Use @a pool for temporary allocation.
 */
svn_error_t *
svn_io_dir_empty(svn_boolean_t *is_empty_p,
                 const char *path,
                 apr_pool_t *pool);


/** Append @a src to @a dst.  @a dst will be appended to if it exists, else it
 * will be created.  Both @a src and @a dst are utf8-encoded.
 */
svn_error_t *
svn_io_append_file(const char *src,
                   const char *dst,
                   apr_pool_t *pool);


/** Make a file as read-only as the operating system allows.
 * @a path is the utf8-encoded path to the file. If @a ignore_enoent is
 * @c TRUE, don't fail if the target file doesn't exist.
 *
 * If @a path is a symlink, do nothing.
 *
 * @note If @a path is a directory, act on it as though it were a
 * file, as described above, but note that you probably don't want to
 * call this function on directories.  We have left it effective on
 * directories for compatibility reasons, but as its name implies, it
 * should be used only for files.
 */
svn_error_t *
svn_io_set_file_read_only(const char *path,
                          svn_boolean_t ignore_enoent,
                          apr_pool_t *pool);


/** Make a file as writable as the operating system allows.
 * @a path is the utf8-encoded path to the file.  If @a ignore_enoent is
 * @c TRUE, don't fail if the target file doesn't exist.
 * @warning On Unix this function will do the equivalent of chmod a+w path.
 * If this is not what you want you should not use this function, but rather
 * use apr_file_perms_set().
 *
 * If @a path is a symlink, do nothing.
 *
 * @note If @a path is a directory, act on it as though it were a
 * file, as described above, but note that you probably don't want to
 * call this function on directories.  We have left it effective on
 * directories for compatibility reasons, but as its name implies, it
 * should be used only for files.
 */
svn_error_t *
svn_io_set_file_read_write(const char *path,
                           svn_boolean_t ignore_enoent,
                           apr_pool_t *pool);


/** Similar to svn_io_set_file_read_* functions.
 * Change the read-write permissions of a file.
 * @since New in 1.1.
 *
 * When making @a path read-write on operating systems with unix style
 * permissions, set the permissions on @a path to the permissions that
 * are set when a new file is created (effectively honoring the user's
 * umask).
 *
 * When making the file read-only on operating systems with unix style
 * permissions, remove all write permissions.
 *
 * On other operating systems, toggle the file's "writability" as much as
 * the operating system allows.
 *
 * @a path is the utf8-encoded path to the file.  If @a enable_write
 * is @c TRUE, then make the file read-write.  If @c FALSE, make it
 * read-only.  If @a ignore_enoent is @c TRUE, don't fail if the target
 * file doesn't exist.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_set_file_read_write_carefully(const char *path,
                                     svn_boolean_t enable_write,
                                     svn_boolean_t ignore_enoent,
                                     apr_pool_t *pool);

/** Set @a path's "executability" (but do nothing if it is a symlink).
 *
 * @a path is the utf8-encoded path to the file.  If @a executable
 * is @c TRUE, then make the file executable.  If @c FALSE, make it
 * non-executable.  If @a ignore_enoent is @c TRUE, don't fail if the target
 * file doesn't exist.
 *
 * When making the file executable on operating systems with unix style
 * permissions, never add an execute permission where there is not
 * already a read permission: that is, only make the file executable
 * for the user, group or world if the corresponding read permission
 * is already set for user, group or world.
 *
 * When making the file non-executable on operating systems with unix style
 * permissions, remove all execute permissions.
 *
 * On other operating systems, toggle the file's "executability" as much as
 * the operating system allows.
 *
 * @note If @a path is a directory, act on it as though it were a
 * file, as described above, but note that you probably don't want to
 * call this function on directories.  We have left it effective on
 * directories for compatibility reasons, but as its name implies, it
 * should be used only for files.
 */
svn_error_t *
svn_io_set_file_executable(const char *path,
                           svn_boolean_t executable,
                           svn_boolean_t ignore_enoent,
                           apr_pool_t *pool);

/** Determine whether a file is executable by the current user.
 * Set @a *executable to @c TRUE if the file @a path is executable by the
 * current user, otherwise set it to @c FALSE.
 *
 * On Windows and on platforms without userids, always returns @c FALSE.
 */
svn_error_t *
svn_io_is_file_executable(svn_boolean_t *executable,
                          const char *path,
                          apr_pool_t *pool);


/** Read a line from @a file into @a buf, but not exceeding @a *limit bytes.
 * Does not include newline, instead '\\0' is put there.
 * Length (as in strlen) is returned in @a *limit.
 * @a buf should be pre-allocated.
 * @a file should be already opened.
 *
 * When the file is out of lines, @c APR_EOF will be returned.
 */
svn_error_t *
svn_io_read_length_line(apr_file_t *file,
                        char *buf,
                        apr_size_t *limit,
                        apr_pool_t *pool);


/** Set @a *apr_time to the time of last modification of the contents of the
 * file @a path.  @a path is utf8-encoded.
 *
 * @note This is the APR mtime which corresponds to the traditional mtime
 * on Unix, and the last write time on Windows.
 */
svn_error_t *
svn_io_file_affected_time(apr_time_t *apr_time,
                          const char *path,
                          apr_pool_t *pool);

/** Set the timestamp of file @a path to @a apr_time.  @a path is
 *  utf8-encoded.
 *
 * @note This is the APR mtime which corresponds to the traditional mtime
 * on Unix, and the last write time on Windows.
 */
svn_error_t *
svn_io_set_file_affected_time(apr_time_t apr_time,
                              const char *path,
                              apr_pool_t *pool);

/** Sleep to ensure that any files modified after we exit have a different
 * timestamp than the one we recorded. If @a path is not NULL, check if we
 * can determine how long we should wait for a new timestamp on the filesystem
 * containing @a path, an existing file or directory. If @a path is NULL or we
 * can't determine the timestamp resolution, sleep until the next second.
 *
 * Use @a pool for any necessary allocations. @a pool can be null if @a path
 * is NULL.
 *
 * Errors while retrieving the timestamp resolution will result in sleeping
 * to the next second, to keep the working copy stable in error conditions.
 *
 * @since New in 1.6.
 */
void
svn_io_sleep_for_timestamps(const char *path, apr_pool_t *pool);

/** Set @a *different_p to TRUE if @a file1 and @a file2 have different
 * sizes, else set to FALSE.  Both @a file1 and @a file2 are utf8-encoded.
 *
 * Setting @a *different_p to zero does not mean the files definitely
 * have the same size, it merely means that the sizes are not
 * definitely different.  That is, if the size of one or both files
 * cannot be determined, then the sizes are not known to be different,
 * so @a *different_p is set to FALSE.
 */
svn_error_t *
svn_io_filesizes_different_p(svn_boolean_t *different_p,
                             const char *file1,
                             const char *file2,
                             apr_pool_t *pool);

/** Set @a *different_p12 to non-zero if @a file1 and @a file2 have different
 * sizes, else set to zero.  Do the similar for @a *different_p23 with
 * @a file2 and @a file3, and @a *different_p13 for @a file1 and @a file3.
 * The filenames @a file1, @a file2 and @a file3 are utf8-encoded.
 *
 * Setting @a *different_p12 to zero does not mean the files definitely
 * have the same size, it merely means that the sizes are not
 * definitely different.  That is, if the size of one or both files
 * cannot be determined (due to stat() returning an error), then the sizes
 * are not known to be different, so @a *different_p12 is set to 0.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_io_filesizes_three_different_p(svn_boolean_t *different_p12,
                                   svn_boolean_t *different_p23,
                                   svn_boolean_t *different_p13,
                                   const char *file1,
                                   const char *file2,
                                   const char *file3,
                                   apr_pool_t *scratch_pool);

/** Return in @a *checksum the checksum of type @a kind of @a file
 * Use @a pool for temporary allocations and to allocate @a *checksum.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_io_file_checksum2(svn_checksum_t **checksum,
                      const char *file,
                      svn_checksum_kind_t kind,
                      apr_pool_t *pool);


/** Put the md5 checksum of @a file into @a digest.
 * @a digest points to @c APR_MD5_DIGESTSIZE bytes of storage.
 * Use @a pool only for temporary allocations.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_file_checksum(unsigned char digest[],
                     const char *file,
                     apr_pool_t *pool);


/** Set @a *same to TRUE if @a file1 and @a file2 have the same
 * contents, else set it to FALSE.  Use @a pool for temporary allocations.
 */
svn_error_t *
svn_io_files_contents_same_p(svn_boolean_t *same,
                             const char *file1,
                             const char *file2,
                             apr_pool_t *pool);

/** Set @a *same12 to TRUE if @a file1 and @a file2 have the same
 * contents, else set it to FALSE.  Do the similar for @a *same23
 * with @a file2 and @a file3, and @a *same13 for @a file1 and @a
 * file3. The filenames @a file1, @a file2 and @a file3 are
 * utf8-encoded. Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_io_files_contents_three_same_p(svn_boolean_t *same12,
                                   svn_boolean_t *same23,
                                   svn_boolean_t *same13,
                                   const char *file1,
                                   const char *file2,
                                   const char *file3,
                                   apr_pool_t *scratch_pool);

/** Create a file at utf8-encoded path @a file with the contents given
 * by the null-terminated string @a contents.
 *
 * @a file must not already exist. If an error occurs while writing or
 * closing the file, attempt to delete the file before returning the error.
 *
 * Write the data in 'binary' mode (#APR_FOPEN_BINARY). If @a contents
 * is @c NULL, create an empty file.
 *
 * Use @a pool for memory allocations.
 */
svn_error_t *
svn_io_file_create(const char *file,
                   const char *contents,
                   apr_pool_t *pool);

/** Create a file at utf8-encoded path @a file with the contents given
 * by @a contents of @a length bytes.
 *
 * @a file must not already exist. If an error occurs while writing or
 * closing the file, attempt to delete the file before returning the error.
 *
 * Write the data in 'binary' mode (#APR_FOPEN_BINARY). If @a length is
 * zero, create an empty file; in this case @a contents may be @c NULL.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_io_file_create_bytes(const char *file,
                         const void *contents,
                         apr_size_t length,
                         apr_pool_t *scratch_pool);

/** Create an empty file at utf8-encoded path @a file.
 *
 * @a file must not already exist. If an error occurs while
 * closing the file, attempt to delete the file before returning the error.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_io_file_create_empty(const char *file,
                         apr_pool_t *scratch_pool);

/**
 * Lock file at @a lock_file. If @a exclusive is TRUE,
 * obtain exclusive lock, otherwise obtain shared lock.
 * Lock will be automatically released when @a pool is cleared or destroyed.
 * Use @a pool for memory allocations.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_file_lock(const char *lock_file,
                 svn_boolean_t exclusive,
                 apr_pool_t *pool);

/**
 * Lock file at @a lock_file. If @a exclusive is TRUE,
 * obtain exclusive lock, otherwise obtain shared lock.
 *
 * If @a nonblocking is TRUE, do not wait for the lock if it
 * is not available: throw an error instead.
 *
 * Lock will be automatically released when @a pool is cleared or destroyed.
 * Use @a pool for memory allocations.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_io_file_lock2(const char *lock_file,
                  svn_boolean_t exclusive,
                  svn_boolean_t nonblocking,
                  apr_pool_t *pool);

/**
 * Lock the file @a lockfile_handle. If @a exclusive is TRUE,
 * obtain exclusive lock, otherwise obtain shared lock.
 *
 * If @a nonblocking is TRUE, do not wait for the lock if it
 * is not available: throw an error instead.
 *
 * Lock will be automatically released when @a pool is cleared or destroyed.
 * You may also explicitly call svn_io_unlock_open_file().
 * Use @a pool for memory allocations. @a pool must be the pool that
 * @a lockfile_handle has been created in or one of its sub-pools.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_io_lock_open_file(apr_file_t *lockfile_handle,
                      svn_boolean_t exclusive,
                      svn_boolean_t nonblocking,
                      apr_pool_t *pool);

/**
 * Unlock the file @a lockfile_handle.
 *
 * Use @a pool for memory allocations. @a pool must be the pool that
 * was passed to svn_io_lock_open_file().
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_io_unlock_open_file(apr_file_t *lockfile_handle,
                        apr_pool_t *pool);

/**
 * Flush any unwritten data from @a file to disk.  Use @a pool for
 * memory allocations.
 *
 * @note This function uses advanced file control operations to flush buffers
 * to disk that aren't always accessible and can be very expensive on systems
 * that implement flushing on all IO layers, like Windows. Please avoid using
 * this function in cases where the file should just work on any network
 * filesystem. In many cases a normal svn_io_file_flush() will work just fine.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_io_file_flush_to_disk(apr_file_t *file,
                          apr_pool_t *pool);

/** Copy the file whose basename (or relative path) is @a file within
 * directory @a src_path to the same basename (or relative path) within
 * directory @a dest_path.  Overwrite the destination file if it already
 * exists.  The destination directory (including any directory
 * components in @a name) must already exist.  Set the destination
 * file's permissions to match those of the source.  Use @a pool for
 * memory allocations.
 */
svn_error_t *
svn_io_dir_file_copy(const char *src_path,
                     const char *dest_path,
                     const char *file,
                     apr_pool_t *pool);


/** Generic byte-streams
 *
 * @defgroup svn_io_byte_streams Generic byte streams
 * @{
 */

/** An abstract stream of bytes--either incoming or outgoing or both.
 *
 * The creator of a stream sets functions to handle read and write.
 * Both of these handlers accept a baton whose value is determined at
 * stream creation time; this baton can point to a structure
 * containing data associated with the stream.  If a caller attempts
 * to invoke a handler which has not been set, it will generate a
 * runtime assertion failure.  The creator can also set a handler for
 * close requests so that it can flush buffered data or whatever;
 * if a close handler is not specified, a close request on the stream
 * will simply be ignored.  Note that svn_stream_close() does not
 * deallocate the memory used to allocate the stream structure; free
 * the pool you created the stream in to free that memory.
 *
 * The read and write handlers accept length arguments via pointer.
 * On entry to the handler, the pointed-to value should be the amount
 * of data which can be read or the amount of data to write.  When the
 * handler returns, the value is reset to the amount of data actually
 * read or written.  The write and full read handler are obliged to
 * complete a read or write to the maximum extent possible; thus, a
 * short read with no associated error implies the end of the input
 * stream, and a short write should never occur without an associated
 * error. In Subversion 1.9 the stream api was extended to also support
 * limited reads via the new svn_stream_read2() api.
 *
 * In Subversion 1.7 mark, seek and reset support was added as an optional
 * feature of streams. If a stream implements resetting it allows reading
 * the data again after a successful call to svn_stream_reset().
 */
typedef struct svn_stream_t svn_stream_t;



/** Read handler function for a generic stream.  @see svn_stream_t. */
typedef svn_error_t *(*svn_read_fn_t)(void *baton,
                                      char *buffer,
                                      apr_size_t *len);

/** Skip data handler function for a generic stream.  @see svn_stream_t
 * and svn_stream_skip().
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_stream_skip_fn_t)(void *baton,
                                             apr_size_t len);

/** Write handler function for a generic stream.  @see svn_stream_t. */
typedef svn_error_t *(*svn_write_fn_t)(void *baton,
                                       const char *data,
                                       apr_size_t *len);

/** Close handler function for a generic stream.  @see svn_stream_t. */
typedef svn_error_t *(*svn_close_fn_t)(void *baton);

/** An opaque type which represents a mark on a stream.  There is no
 * concrete definition of this type, it is a named type for stream
 * implementation specific baton pointers.
 *
 * @see svn_stream_mark().
 * @since New in 1.7.
 */
typedef struct svn_stream_mark_t svn_stream_mark_t;

/** Mark handler function for a generic stream. @see svn_stream_t and
 * svn_stream_mark().
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_stream_mark_fn_t)(void *baton,
                                         svn_stream_mark_t **mark,
                                         apr_pool_t *pool);

/** Seek handler function for a generic stream. @see svn_stream_t and
 * svn_stream_seek().
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_stream_seek_fn_t)(void *baton,
                                             const svn_stream_mark_t *mark);

/** Poll handler for generic streams that support incomplete reads, @see
 * svn_stream_t and svn_stream_data_available().
 *
 * @since New in 1.9.
 */
typedef svn_error_t *(*svn_stream_data_available_fn_t)(void *baton,
                                              svn_boolean_t *data_available);

/** Readline handler function for a generic stream. @see svn_stream_t and
 * svn_stream_readline().
 *
 * @since New in 1.10.
 */
typedef svn_error_t *(*svn_stream_readline_fn_t)(void *baton,
                                                 svn_stringbuf_t **stringbuf,
                                                 const char *eol,
                                                 svn_boolean_t *eof,
                                                 apr_pool_t *pool);

/** Create a generic stream.  @see svn_stream_t. */
svn_stream_t *
svn_stream_create(void *baton,
                  apr_pool_t *pool);

/** Set @a stream's baton to @a baton */
void
svn_stream_set_baton(svn_stream_t *stream,
                     void *baton);

/** Set @a stream's read functions to @a read_fn and @a read_full_fn. If
 * @a read_full_fn is NULL a default implementation based on multiple calls
 * to @a read_fn will be used.
 *
 * @since New in 1.9.
 */
void
svn_stream_set_read2(svn_stream_t *stream,
                     svn_read_fn_t read_fn,
                     svn_read_fn_t read_full_fn);

/** Set @a stream's read function to @a read_fn.
 *
 * This function sets only the full read function to read_fn.
 *
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
void
svn_stream_set_read(svn_stream_t *stream,
                    svn_read_fn_t read_fn);

/** Set @a stream's skip function to @a skip_fn
 *
 * @since New in 1.7
 */
void
svn_stream_set_skip(svn_stream_t *stream,
                    svn_stream_skip_fn_t skip_fn);

/** Set @a stream's write function to @a write_fn */
void
svn_stream_set_write(svn_stream_t *stream,
                     svn_write_fn_t write_fn);

/** Set @a stream's close function to @a close_fn */
void
svn_stream_set_close(svn_stream_t *stream,
                     svn_close_fn_t close_fn);

/** Set @a stream's mark function to @a mark_fn
 *
 * @since New in 1.7.
 */
void
svn_stream_set_mark(svn_stream_t *stream,
                    svn_stream_mark_fn_t mark_fn);

/** Set @a stream's seek function to @a seek_fn
 *
 * @since New in 1.7.
 */
void
svn_stream_set_seek(svn_stream_t *stream,
                    svn_stream_seek_fn_t seek_fn);

/** Set @a stream's data available function to @a data_available_fn
 *
 * @since New in 1.9.
 */
void
svn_stream_set_data_available(svn_stream_t *stream,
                              svn_stream_data_available_fn_t data_available);

/** Set @a stream's readline function to @a readline_fn
 *
 * @since New in 1.10.
 */
void
svn_stream_set_readline(svn_stream_t *stream,
                        svn_stream_readline_fn_t readline_fn);

/** Create a stream that is empty for reading and infinite for writing. */
svn_stream_t *
svn_stream_empty(apr_pool_t *pool);

/** Return a stream allocated in @a pool which forwards all requests
 * to @a stream.  Destruction is explicitly excluded from forwarding.
 *
 * @see http://subversion.apache.org/docs/community-guide/conventions.html#destruction-of-stacked-resources
 *
 * @since New in 1.4.
 */
svn_stream_t *
svn_stream_disown(svn_stream_t *stream,
                  apr_pool_t *pool);


/** Create a stream to read the file at @a path. It will be opened using
 * the APR_BUFFERED and APR_BINARY flag, and APR_OS_DEFAULT for the perms.
 * If you'd like to use different values, then open the file yourself, and
 * use the svn_stream_from_aprfile2() interface.
 *
 * The stream will be returned in @a stream, and allocated from @a result_pool.
 * Temporary allocations will be performed in @a scratch_pool.
 *
 * @since New in 1.6
 */
svn_error_t *
svn_stream_open_readonly(svn_stream_t **stream,
                         const char *path,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


/** Create a stream to write a file at @a path. The file will be *created*
 * using the APR_BUFFERED and APR_BINARY flag, and APR_OS_DEFAULT for the
 * perms. The file will be created "exclusively", so if it already exists,
 * then an error will be thrown. If you'd like to use different values, or
 * open an existing file, then open the file yourself, and use the
 * svn_stream_from_aprfile2() interface.
 *
 * The stream will be returned in @a stream, and allocated from @a result_pool.
 * Temporary allocations will be performed in @a scratch_pool.
 *
 * @since New in 1.6
 */
svn_error_t *
svn_stream_open_writable(svn_stream_t **stream,
                         const char *path,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


/** Create a writable stream to a file in the directory @a dirpath.
 * The file will have an arbitrary and unique name, and the full path
 * will be returned in @a temp_path. The stream will be returned in
 * @a stream. Both will be allocated from @a result_pool.
 *
 * If @a dirpath is @c NULL, use the path returned from svn_io_temp_dir().
 * (Note that when using the system-provided temp directory, it may not
 * be possible to atomically rename the resulting file due to cross-device
 * issues.)
 *
 * The file will be deleted according to @a delete_when.  If that is
 * #svn_io_file_del_on_pool_cleanup, it refers to @a result_pool.
 *
 * Temporary allocations will be performed in @a scratch_pool.
 *
 * @since New in 1.6
 * @see svn_io_open_unique_file3()
 */
svn_error_t *
svn_stream_open_unique(svn_stream_t **stream,
                       const char **temp_path,
                       const char *dirpath,
                       svn_io_file_del_t delete_when,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);


/** Create a stream from an APR file.  For convenience, if @a file is
 * @c NULL, an empty stream created by svn_stream_empty() is returned.
 *
 * This function should normally be called with @a disown set to FALSE,
 * in which case closing the stream will also close the underlying file.
 *
 * If @a disown is TRUE, the stream will disown the underlying file,
 * meaning that svn_stream_close() will not close the file.
 *
 * @since New in 1.4.
 */
svn_stream_t *
svn_stream_from_aprfile2(apr_file_t *file,
                         svn_boolean_t disown,
                         apr_pool_t *pool);

/** Similar to svn_stream_from_aprfile2(), except that the file will
 * always be disowned.
 *
 * @note The stream returned is not considered to "own" the underlying
 *       file, meaning that svn_stream_close() on the stream will not
 *       close the file.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_stream_t *
svn_stream_from_aprfile(apr_file_t *file,
                        apr_pool_t *pool);

/** Set @a *in to a generic stream connected to stdin, allocated in
 * @a pool.  If @a buffered is set, APR buffering will be enabled.
 * The stream and its underlying APR handle will be closed when @a pool
 * is cleared or destroyed.
 *
 * @note APR buffering will try to fill the whole internal buffer before
 *       serving read requests.  This may be inappropriate for interactive
 *       applications where stdin will not deliver any more data unless
 *       the application processed the data already received.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_stream_for_stdin2(svn_stream_t **in,
                      svn_boolean_t buffered,
                      apr_pool_t *pool);

/** Similar to svn_stream_for_stdin2(), but with buffering being disabled.
 *
 * @since New in 1.7.
 *
 * @deprecated Provided for backward compatibility with the 1.9 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_stream_for_stdin(svn_stream_t **in,
                     apr_pool_t *pool);

/** Set @a *err to a generic stream connected to stderr, allocated in
 * @a pool.  The stream and its underlying APR handle will be closed
 * when @a pool is cleared or destroyed.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_stream_for_stderr(svn_stream_t **err,
                      apr_pool_t *pool);

/** Set @a *out to a generic stream connected to stdout, allocated in
 * @a pool.  The stream and its underlying APR handle will be closed
 * when @a pool is cleared or destroyed.
 */
svn_error_t *
svn_stream_for_stdout(svn_stream_t **out,
                      apr_pool_t *pool);

/** Read the contents of @a stream into memory, from its current position
 * to its end, returning the data in @a *result. This function does not
 * close the @a stream upon completion.
 *
 * @a len_hint gives a hint about the expected length, in bytes, of the
 * actual data that will be read from the stream. It may be 0, meaning no
 * hint is being provided. Efficiency in time and/or in space may be
 * better (and in general will not be worse) when the actual data length
 * is equal or approximately equal to the length hint.
 *
 * The returned memory is allocated in @a result_pool.
 *
 * @note The present implementation is efficient when @a len_hint is big
 * enough (but not vastly bigger than necessary), and also for actual
 * lengths up to 64 bytes when @a len_hint is 0. Otherwise it can incur
 * significant time and space overheads. See source code for details.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_stringbuf_from_stream(svn_stringbuf_t **result,
                          svn_stream_t *stream,
                          apr_size_t len_hint,
                          apr_pool_t *result_pool);

/** Return a generic stream connected to stringbuf @a str.  Allocate the
 * stream in @a pool.
 */
svn_stream_t *
svn_stream_from_stringbuf(svn_stringbuf_t *str,
                          apr_pool_t *pool);

/** Return a generic read-only stream connected to string @a str.
 *  Allocate the stream in @a pool.
 */
svn_stream_t *
svn_stream_from_string(const svn_string_t *str,
                       apr_pool_t *pool);

/** Return a generic stream which implements buffered reads and writes.
 *  The stream will preferentially store data in-memory, but may use
 *  disk storage as backup if the amount of data is large.
 *  Allocate the stream in @a result_pool
 *
 * @since New in 1.8.
 */
svn_stream_t *
svn_stream_buffered(apr_pool_t *result_pool);

/** Return a stream that decompresses all data read and compresses all
 * data written. The stream @a stream is used to read and write all
 * compressed data. All compression data structures are allocated on
 * @a pool. If compression support is not compiled in then
 * svn_stream_compressed() returns @a stream unmodified. Make sure you
 * call svn_stream_close() on the stream returned by this function,
 * so that all data are flushed and cleaned up.
 *
 * @note From 1.4, compression support is always compiled in.
 */
svn_stream_t *
svn_stream_compressed(svn_stream_t *stream,
                      apr_pool_t *pool);

/** Return a stream that calculates checksums for all data read
 * and written.  The stream @a stream is used to read and write all data.
 * The stream and the resulting digests are allocated in @a pool.
 *
 * When the stream is closed, @a *read_checksum and @a *write_checksum
 * are set to point to the resulting checksums, of type @a read_checksum_kind
 * and @a write_checksum_kind, respectively.
 *
 * Both @a read_checksum and @a write_checksum can be @c NULL, in which case
 * the respective checksum isn't calculated.
 *
 * If @a read_all is TRUE, make sure that all data available on @a
 * stream is read (and checksummed) when the stream is closed.
 *
 * Read and write operations can be mixed without interfering.
 *
 * The @a stream passed into this function is closed when the created
 * stream is closed.
 *
 * @since New in 1.6.  Since 1.10, the resulting stream supports reset
 * via stream_stream_reset().
 */
svn_stream_t *
svn_stream_checksummed2(svn_stream_t *stream,
                        svn_checksum_t **read_checksum,
                        svn_checksum_t **write_checksum,
                        svn_checksum_kind_t checksum_kind,
                        svn_boolean_t read_all,
                        apr_pool_t *pool);

/**
 * Similar to svn_stream_checksummed2(), but always returning the MD5
 * checksum in @a read_digest and @a write_digest.
 *
 * @since New in 1.4.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_stream_t *
svn_stream_checksummed(svn_stream_t *stream,
                       const unsigned char **read_digest,
                       const unsigned char **write_digest,
                       svn_boolean_t read_all,
                       apr_pool_t *pool);

/** Read the contents of the readable stream @a stream and return its
 * checksum of type @a kind in @a *checksum.
 *
 * The stream will be closed before this function returns (regardless
 * of the result, or any possible error).
 *
 * Use @a scratch_pool for temporary allocations and @a result_pool
 * to allocate @a *checksum.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_stream_contents_checksum(svn_checksum_t **checksum,
                             svn_stream_t *stream,
                             svn_checksum_kind_t kind,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/** Read from a generic stream until @a buffer is filled upto @a *len or
 * until EOF is reached. @see svn_stream_t
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_stream_read_full(svn_stream_t *stream,
                     char *buffer,
                     apr_size_t *len);


/** Returns @c TRUE if the generic @c stream supports svn_stream_read2().
 *
 * @since New in 1.9.
 */
svn_boolean_t
svn_stream_supports_partial_read(svn_stream_t *stream);

/** Read all currently available upto @a *len into @a buffer. Use
 * svn_stream_read_full() if you want to wait for the buffer to be filled
 * or EOF. If the stream doesn't support limited reads this function will
 * return an #SVN_ERR_STREAM_NOT_SUPPORTED error.
 *
 * A 0 byte read signals the end of the stream.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_stream_read2(svn_stream_t *stream,
                 char *buffer,
                 apr_size_t *len);


/** Read from a generic stream until the buffer is completely filled or EOF.
 * @see svn_stream_t.
 *
 * @note This function is a wrapper of svn_stream_read_full() now, which name
 * better documents the behavior of this function.
 *
 * @deprecated Provided for backward compatibility with the 1.8 API
 */
SVN_DEPRECATED
svn_error_t *
svn_stream_read(svn_stream_t *stream,
                char *buffer,
                apr_size_t *len);

/**
 * Skip @a len bytes from a generic @a stream. If the stream is exhausted
 * before @a len bytes have been read, return an error.
 *
 * @note  No assumption can be made on the semantics of this function
 * other than that the stream read pointer will be advanced by *len
 * bytes. Depending on the capabilities of the underlying stream
 * implementation, this may for instance be translated into a sequence
 * of reads or a simple seek operation. If the stream implementation has
 * not provided a skip function, this will read from the stream and
 * discard the data.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_stream_skip(svn_stream_t *stream,
                apr_size_t len);

/** Write to a generic stream. @see svn_stream_t. */
svn_error_t *
svn_stream_write(svn_stream_t *stream,
                 const char *data,
                 apr_size_t *len);

/** Close a generic stream. @see svn_stream_t. */
svn_error_t *
svn_stream_close(svn_stream_t *stream);

/** Reset a generic stream back to its origin. (E.g. On a file this would be
 * implemented as a seek to position 0).  This function returns a
 * #SVN_ERR_STREAM_SEEK_NOT_SUPPORTED error when the stream doesn't
 * implement resetting.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_stream_reset(svn_stream_t *stream);

/** Returns @c TRUE if the generic @a stream supports svn_stream_mark().
 *
 * @see svn_stream_mark()
 * @since New in 1.7.
 */
svn_boolean_t
svn_stream_supports_mark(svn_stream_t *stream);

/** Returns @c TRUE if the generic @a stream supports svn_stream_reset().
 *
 * @see svn_stream_reset()
 * @since New in 1.10.
 */
svn_boolean_t
svn_stream_supports_reset(svn_stream_t *stream);

/** Set a @a mark at the current position of a generic @a stream,
 * which can later be sought back to using svn_stream_seek().
 * The @a mark is allocated in @a pool.
 *
 * This function returns the #SVN_ERR_STREAM_SEEK_NOT_SUPPORTED error
 * if the stream doesn't implement seeking.
 *
 * @see svn_stream_seek()
 * @since New in 1.7.
 */
svn_error_t *
svn_stream_mark(svn_stream_t *stream,
                svn_stream_mark_t **mark,
                apr_pool_t *pool);

/** Seek to a @a mark in a generic @a stream.
 * This function returns the #SVN_ERR_STREAM_SEEK_NOT_SUPPORTED error
 * if the stream doesn't implement seeking. Passing NULL as @a mark,
 * seeks to the start of the stream.
 *
 * @see svn_stream_mark()
 * @since New in 1.7.
 */
svn_error_t *
svn_stream_seek(svn_stream_t *stream, const svn_stream_mark_t *mark);

/** When a stream supports polling for available data, obtain a boolean
 * indicating whether data is waiting to be read. If the stream doesn't
 * support polling this function returns a #SVN_ERR_STREAM_NOT_SUPPORTED
 * error.
 *
 * If the data_available callback is implemented and the stream is at the end
 * the stream will set @a *data_available to FALSE.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_stream_data_available(svn_stream_t *stream,
                          svn_boolean_t *data_available);

/** Return a writable stream which, when written to, writes to both of the
 * underlying streams.  Both of these streams will be closed upon closure of
 * the returned stream; use svn_stream_disown() if this is not the desired
 * behavior.  One or both of @a out1 and @a out2 may be @c NULL.  If both are
 * @c NULL, @c NULL is returned.
 *
 * @since New in 1.7.
 */
svn_stream_t *
svn_stream_tee(svn_stream_t *out1,
               svn_stream_t *out2,
               apr_pool_t *pool);

/** Write NULL-terminated string @a str to @a stream.
 *
 * @since New in 1.8.
 *
 */
svn_error_t *
svn_stream_puts(svn_stream_t *stream,
                const char *str);

/** Write to @a stream using a printf-style @a fmt specifier, passed through
 * apr_psprintf() using memory from @a pool.
 */
svn_error_t *
svn_stream_printf(svn_stream_t *stream,
                  apr_pool_t *pool,
                  const char *fmt,
                  ...)
       __attribute__((format(printf, 3, 4)));

/** Write to @a stream using a printf-style @a fmt specifier, passed through
 * apr_psprintf() using memory from @a pool.  The resulting string
 * will be translated to @a encoding before it is sent to @a stream.
 *
 * @note Use @c APR_LOCALE_CHARSET to translate to the encoding of the
 * current locale.
 *
 * @since New in 1.3.
 */
svn_error_t *
svn_stream_printf_from_utf8(svn_stream_t *stream,
                            const char *encoding,
                            apr_pool_t *pool,
                            const char *fmt,
                            ...)
       __attribute__((format(printf, 4, 5)));

/** Allocate @a *stringbuf in @a pool, and read into it one line (terminated
 * by @a eol) from @a stream. The line-terminator is read from the stream,
 * but is not added to the end of the stringbuf.  Instead, the stringbuf
 * ends with a usual '\\0'.
 *
 * If @a stream runs out of bytes before encountering a line-terminator,
 * then set @a *eof to @c TRUE, otherwise set @a *eof to FALSE.
 */
svn_error_t *
svn_stream_readline(svn_stream_t *stream,
                    svn_stringbuf_t **stringbuf,
                    const char *eol,
                    svn_boolean_t *eof,
                    apr_pool_t *pool);

/**
 * Read the contents of the readable stream @a from and write them to the
 * writable stream @a to calling @a cancel_func before copying each chunk.
 *
 * @a cancel_func may be @c NULL.
 *
 * @note both @a from and @a to will be closed upon successful completion of
 * the copy (but an error may still be returned, based on trying to close
 * the two streams). If the closure is not desired, then you can use
 * svn_stream_disown() to protect either or both of the streams from
 * being closed.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_stream_copy3(svn_stream_t *from,
                 svn_stream_t *to,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool);

/**
 * Same as svn_stream_copy3() but the streams are not closed.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_stream_copy2(svn_stream_t *from,
                 svn_stream_t *to,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool);

/**
 * Same as svn_stream_copy3(), but without the cancellation function
 * or stream closing.
 *
 * @since New in 1.1.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_stream_copy(svn_stream_t *from,
                svn_stream_t *to,
                apr_pool_t *pool);


/** Set @a *same to TRUE if @a stream1 and @a stream2 have the same
 * contents, else set it to FALSE.
 *
 * Both streams will be closed before this function returns (regardless of
 * the result, or any possible error).
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_stream_contents_same2(svn_boolean_t *same,
                          svn_stream_t *stream1,
                          svn_stream_t *stream2,
                          apr_pool_t *pool);


/**
 * Same as svn_stream_contents_same2(), but the streams will not be closed.
 *
 * @since New in 1.4.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_stream_contents_same(svn_boolean_t *same,
                         svn_stream_t *stream1,
                         svn_stream_t *stream2,
                         apr_pool_t *pool);


/** Read the contents of @a stream into memory, from its current position
 * to its end, returning the data in @a *result. The stream will be closed
 * when it has been successfully and completely read.
 *
 * @a len_hint gives a hint about the expected length, in bytes, of the
 * actual data that will be read from the stream. It may be 0, meaning no
 * hint is being provided. Efficiency in time and/or in space may be
 * better (and in general will not be worse) when the actual data length
 * is equal or approximately equal to the length hint.
 *
 * The returned memory is allocated in @a result_pool, and any temporary
 * allocations may be performed in @a scratch_pool.
 *
 * @note The present implementation is efficient when @a len_hint is big
 * enough (but not vastly bigger than necessary), and also for actual
 * lengths up to 64 bytes when @a len_hint is 0. Otherwise it can incur
 * significant time and space overheads. See source code for details.
 *
 * @since New in 1.10
 */
svn_error_t *
svn_string_from_stream2(svn_string_t **result,
                        svn_stream_t *stream,
                        apr_size_t len_hint,
                        apr_pool_t *result_pool);

/** Similar to svn_string_from_stream2(), but always passes 0 for
 * @a len_hint.
 *
 * @deprecated Provided for backwards compatibility with the 1.9 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_string_from_stream(svn_string_t **result,
                       svn_stream_t *stream,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/** A function type provided for use as a callback from
 * @c svn_stream_lazyopen_create().
 *
 * The callback function shall open a new stream and set @a *stream to
 * the stream object, allocated in @a result_pool.  @a baton is the
 * callback baton that was passed to svn_stream_lazyopen_create().
 *
 * @a result_pool is the result pool that was passed to
 * svn_stream_lazyopen_create().  The callback function may use
 * @a scratch_pool for temporary allocations; the caller may clear or
 * destroy @a scratch_pool any time after the function returns.
 *
 * @since New in 1.8.
 */
typedef svn_error_t *
(*svn_stream_lazyopen_func_t)(svn_stream_t **stream,
                              void *baton,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);


/** Return a generic stream which wraps another primary stream,
 * delaying the "opening" of that stream until the first time the
 * returned stream is accessed.
 *
 * @a open_func and @a open_baton are a callback function/baton pair
 * which will be invoked upon the first access of the returned
 * stream (read, write, mark, seek, skip, or possibly close).  The
 * callback shall open the primary stream.
 *
 * If the only "access" the returned stream gets is to close it
 * then @a open_func will only be called if @a open_on_close is TRUE.
 *
 * Allocate the returned stream in @a result_pool. Also arrange for
 * @a result_pool to be passed as the @c result_pool parameter to
 * @a open_func when it is called.
 *
 * @since New in 1.8.
 */
svn_stream_t *
svn_stream_lazyopen_create(svn_stream_lazyopen_func_t open_func,
                           void *open_baton,
                           svn_boolean_t open_on_close,
                           apr_pool_t *result_pool);

/** @} */

/** Set @a *result to a string containing the contents of @a
 * filename, which is either "-" (indicating that stdin should be
 * read) or the utf8-encoded path of a real file.
 *
 * @warning Callers should be aware of possible unexpected results
 * when using this function to read from stdin where additional
 * stdin-reading processes abound.  For example, if a program tries
 * both to invoke an external editor and to read from stdin, stdin
 * could be trashed and the editor might act funky or die outright.
 *
 * @note due to memory pseudo-reallocation behavior (due to pools), this
 *   can be a memory-intensive operation for large files.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_stringbuf_from_file2(svn_stringbuf_t **result,
                         const char *filename,
                         apr_pool_t *pool);

/** Similar to svn_stringbuf_from_file2(), except that if @a filename
 * is "-", return the error #SVN_ERR_UNSUPPORTED_FEATURE and don't
 * touch @a *result.
 *
 * @deprecated Provided for backwards compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_stringbuf_from_file(svn_stringbuf_t **result,
                        const char *filename,
                        apr_pool_t *pool);

/** Sets @a *result to a string containing the contents of the already opened
 * @a file.  Reads from the current position in file to the end.  Does not
 * close the file or reset the cursor position.
 *
 * @note due to memory pseudo-reallocation behavior (due to pools), this
 *   can be a memory-intensive operation for large files.
 */
svn_error_t *
svn_stringbuf_from_aprfile(svn_stringbuf_t **result,
                           apr_file_t *file,
                           apr_pool_t *pool);

/** Remove file @a path, a utf8-encoded path.  This wraps apr_file_remove(),
 * converting any error to a Subversion error. If @a ignore_enoent is TRUE, and
 * the file is not present (APR_STATUS_IS_ENOENT returns TRUE), then no
 * error will be returned.
 *
 * The file will be removed even if it is not writable.  (On Windows and
 * OS/2, this function first clears the file's read-only bit.)
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_io_remove_file2(const char *path,
                    svn_boolean_t ignore_enoent,
                    apr_pool_t *scratch_pool);

/** Similar to svn_io_remove_file2(), except with @a ignore_enoent set to FALSE.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_remove_file(const char *path,
                   apr_pool_t *pool);

/** Recursively remove directory @a path.  @a path is utf8-encoded.
 * If @a ignore_enoent is @c TRUE, don't fail if the target directory
 * doesn't exist.  Use @a pool for temporary allocations.
 *
 * Because recursive delete of a directory tree can be a lengthy operation,
 * provide @a cancel_func and @a cancel_baton for interruptibility.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_io_remove_dir2(const char *path,
                   svn_boolean_t ignore_enoent,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool);

/** Similar to svn_io_remove_dir2(), but with @a ignore_enoent set to
 * @c FALSE and @a cancel_func and @a cancel_baton set to @c NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API
 */
SVN_DEPRECATED
svn_error_t *
svn_io_remove_dir(const char *path,
                  apr_pool_t *pool);

/** Read all of the disk entries in directory @a path, a utf8-encoded
 * path.  Set @a *dirents to a hash mapping dirent names (<tt>char *</tt>) to
 * undefined non-NULL values, allocated in @a pool.
 *
 * @note The `.' and `..' directories normally returned by
 * apr_dir_read() are NOT returned in the hash.
 *
 * @since New in 1.4.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_get_dir_filenames(apr_hash_t **dirents,
                         const char *path,
                         apr_pool_t *pool);

/** Read all of the disk entries in directory @a path, a utf8-encoded
 * path.  Set @a *dirents to a hash mapping dirent names (<tt>char *</tt>) to
 * #svn_io_dirent2_t structures, allocated in @a pool.
 *
 * If @a only_check_type is set to @c TRUE, only the kind and special
 * fields of the svn_io_dirent2_t are filled.
 *
 * @note The `.' and `..' directories normally returned by
 * apr_dir_read() are NOT returned in the hash.
 *
 * @note The kind field in the @a dirents is set according to the mapping
 *       as documented for svn_io_check_path().
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_io_get_dirents3(apr_hash_t **dirents,
                    const char *path,
                    svn_boolean_t only_check_type,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);


/** Similar to svn_io_get_dirents3, but returns a mapping to svn_io_dirent_t
 * structures instead of svn_io_dirent2_t and with only a single pool.
 *
 * @since New in 1.3.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_get_dirents2(apr_hash_t **dirents,
                    const char *path,
                    apr_pool_t *pool);

/** Similar to svn_io_get_dirents2(), but @a *dirents is a hash table
 * with #svn_node_kind_t values.
 *
 * @deprecated Provided for backwards compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_get_dirents(apr_hash_t **dirents,
                   const char *path,
                   apr_pool_t *pool);

/** Create a svn_io_dirent2_t instance for path. Specialized variant of
 * svn_io_stat() that directly translates node_kind and special.
 *
 * If @a verify_truename is @c TRUE, an additional check is performed to
 * verify the truename of the last path component on case insensitive
 * filesystems. This check is expensive compared to a just a stat,
 * but certainly cheaper than a full truename calculation using
 * apr_filepath_merge() which verifies all path components.
 *
 * If @a ignore_enoent is set to @c TRUE, set *dirent_p->kind to
 * svn_node_none instead of returning an error.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_io_stat_dirent2(const svn_io_dirent2_t **dirent_p,
                    const char *path,
                    svn_boolean_t verify_truename,
                    svn_boolean_t ignore_enoent,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);


/** Similar to svn_io_stat_dirent2(), but always passes FALSE for
 * @a verify_truename.
 *
 * @since New in 1.7.
 * @deprecated Provided for backwards compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_stat_dirent(const svn_io_dirent2_t **dirent_p,
                   const char *path,
                   svn_boolean_t ignore_enoent,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);


/** Callback function type for svn_io_dir_walk() */
typedef svn_error_t * (*svn_io_walk_func_t)(void *baton,
                                            const char *path,
                                            const apr_finfo_t *finfo,
                                            apr_pool_t *pool);

/** Recursively walk the directory rooted at @a dirname, a
 * utf8-encoded path, invoking @a walk_func (with @a walk_baton) for
 * each item in the tree.  For a given directory, invoke @a walk_func
 * on the directory itself before invoking it on any children thereof.
 *
 * Deliver to @a walk_func the information specified by @a wanted,
 * which is a combination of @c APR_FINFO_* flags, plus the
 * information specified by @c APR_FINFO_TYPE and @c APR_FINFO_NAME.
 *
 * Use @a pool for all allocations.
 *
 * @note This function does not currently pass all file types to @a
 * walk_func -- only APR_DIR, APR_REG, and APR_LNK.  We reserve the
 * right to pass additional file types through this interface in the
 * future, though, so implementations of this callback should
 * explicitly test FINFO->filetype.  See the APR library's
 * apr_filetype_e enum for the various filetypes and their meanings.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_io_dir_walk2(const char *dirname,
                 apr_int32_t wanted,
                 svn_io_walk_func_t walk_func,
                 void *walk_baton,
                 apr_pool_t *pool);

/** Similar to svn_io_dir_walk(), but only calls @a walk_func for
 * files of type APR_DIR (directory) and APR_REG (regular file).
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_dir_walk(const char *dirname,
                apr_int32_t wanted,
                svn_io_walk_func_t walk_func,
                void *walk_baton,
                apr_pool_t *pool);

/**
 * Start @a cmd with @a args, using utf8-encoded @a path as working
 * directory.  Return the process handle for the invoked program in @a
 * *cmd_proc.
 *
 * If @a infile_pipe is TRUE, connect @a cmd's stdin to a pipe;
 * otherwise, connect it to @a infile (which may be NULL).  If
 * @a outfile_pipe is TRUE, connect @a cmd's stdout to a pipe; otherwise,
 * connect it to @a outfile (which may be NULL).  If @a errfile_pipe
 * is TRUE, connect @a cmd's stderr to a pipe; otherwise, connect it
 * to @a errfile (which may be NULL).  (Callers must pass FALSE for
 * each of these boolean values for which the corresponding file
 * handle is non-NULL.)
 *
 * @a args is a list of utf8-encoded <tt>const char *</tt> arguments,
 * terminated by @c NULL.  @a args[0] is the name of the program, though it
 * need not be the same as @a cmd.
 *
 * If @a inherit is TRUE, the invoked program inherits its environment from
 * the caller and @a cmd, if not absolute, is searched for in PATH.
 *
 * If @a inherit is FALSE @a cmd must be an absolute path and the invoked
 * program inherits the environment defined by @a env or runs with an empty
 * environment in @a env is NULL.
 *
 * @note On some platforms, failure to execute @a cmd in the child process
 * will result in error output being written to @a errfile, if non-NULL, and
 * a non-zero exit status being returned to the parent process.
 *
 * @note An APR bug affects Windows: passing a NULL @a env does not
 * guarantee the invoked program to run with an empty environment when
 * @a inherit is FALSE, the program may inherit its parent's environment.
 * Explicitly pass an empty @a env to get an empty environment.
 *
 * @since New in 1.8.
 */
svn_error_t *svn_io_start_cmd3(apr_proc_t *cmd_proc,
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
                               apr_pool_t *pool);


/**
 * Similar to svn_io_start_cmd3() but with @a env always set to NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API
 * @since New in 1.7.
 */
SVN_DEPRECATED
svn_error_t *svn_io_start_cmd2(apr_proc_t *cmd_proc,
                               const char *path,
                               const char *cmd,
                               const char *const *args,
                               svn_boolean_t inherit,
                               svn_boolean_t infile_pipe,
                               apr_file_t *infile,
                               svn_boolean_t outfile_pipe,
                               apr_file_t *outfile,
                               svn_boolean_t errfile_pipe,
                               apr_file_t *errfile,
                               apr_pool_t *pool);

/**
 * Similar to svn_io_start_cmd2() but with @a infile_pipe, @a
 * outfile_pipe, and @a errfile_pipe always FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API
 * @since New in 1.3.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_start_cmd(apr_proc_t *cmd_proc,
                 const char *path,
                 const char *cmd,
                 const char *const *args,
                 svn_boolean_t inherit,
                 apr_file_t *infile,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 apr_pool_t *pool);

/**
 * Wait for the process @a *cmd_proc to complete and optionally retrieve
 * its exit code.  @a cmd is used only in error messages.
 *
 * If @a exitcode is not NULL, set @a *exitcode to the exit code of the
 * process and do not consider any exit code to be an error.  If @a exitcode
 * is NULL, then if the exit code of the process is non-zero then return an
 * #SVN_ERR_EXTERNAL_PROGRAM error.
 *
 * If @a exitwhy is not NULL, set @a *exitwhy to indicate why the process
 * terminated and do not consider any reason to be an error.  If @a exitwhy
 * is NULL, then if the termination reason is not @c APR_PROC_CHECK_EXIT()
 * then return an #SVN_ERR_EXTERNAL_PROGRAM error.
 *
 * @since New in 1.3.
 */
svn_error_t *
svn_io_wait_for_cmd(apr_proc_t *cmd_proc,
                    const char *cmd,
                    int *exitcode,
                    apr_exit_why_e *exitwhy,
                    apr_pool_t *pool);

/** Run a command to completion, by first calling svn_io_start_cmd() and
 * then calling svn_io_wait_for_cmd().  The parameters correspond to
 * the same-named parameters of those two functions.
 */
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
               apr_pool_t *pool);

/** Invoke the configured @c diff program, with @a user_args (an array
 * of utf8-encoded @a num_user_args arguments) if they are specified
 * (that is, if @a user_args is non-NULL), or "-u" if they are not.
 * If @a user_args is NULL, the value of @a num_user_args is ignored.
 *
 * Diff runs in utf8-encoded @a dir, and its exit status is stored in
 * @a exitcode, if it is not @c NULL.
 *
 * If @a label1 and/or @a label2 are not NULL they will be passed to the diff
 * process as the arguments of "-L" options.  @a label1 and @a label2 are also
 * in utf8, and will be converted to native charset along with the other args.
 *
 * @a from is the first file passed to diff, and @a to is the second.  The
 * stdout of diff will be sent to @a outfile, and the stderr to @a errfile.
 *
 * @a diff_cmd must be non-NULL.
 *
 * Do all allocation in @a pool.
 * @since New in 1.6.0.
 */
svn_error_t *
svn_io_run_diff2(const char *dir,
                 const char *const *user_args,
                 int num_user_args,
                 const char *label1,
                 const char *label2,
                 const char *from,
                 const char *to,
                 int *exitcode,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 const char *diff_cmd,
                 apr_pool_t *pool);

/** Similar to svn_io_run_diff2() but with @a diff_cmd encoded in internal
 * encoding used by APR.
 *
 * @deprecated Provided for backwards compatibility with the 1.5 API. */
SVN_DEPRECATED
svn_error_t *
svn_io_run_diff(const char *dir,
                const char *const *user_args,
                int num_user_args,
                const char *label1,
                const char *label2,
                const char *from,
                const char *to,
                int *exitcode,
                apr_file_t *outfile,
                apr_file_t *errfile,
                const char *diff_cmd,
                apr_pool_t *pool);



/** Invoke the configured @c diff3 program, in utf8-encoded @a dir
 * like this:
 *
 *          diff3 -E -m @a mine @a older @a yours > @a merged
 *
 * (See the diff3 documentation for details.)
 *
 * If @a user_args is non-NULL, replace "-E" with the <tt>const char*</tt>
 * elements that @a user_args contains.
 *
 * @a mine, @a older and @a yours are utf8-encoded paths (relative to
 * @a dir or absolute) to three files that already exist.
 *
 * @a merged is an open file handle, and is left open after the merge
 * result is written to it. (@a merged should *not* be the same file
 * as @a mine, or nondeterministic things may happen!)
 *
 * @a mine_label, @a older_label, @a yours_label are utf8-encoded label
 * parameters for diff3's -L option.  Any of them may be @c NULL, in
 * which case the corresponding @a mine, @a older, or @a yours parameter is
 * used instead.
 *
 * Set @a *exitcode to diff3's exit status.  If @a *exitcode is anything
 * other than 0 or 1, then return #SVN_ERR_EXTERNAL_PROGRAM.  (Note the
 * following from the diff3 info pages: "An exit status of 0 means
 * `diff3' was successful, 1 means some conflicts were found, and 2
 * means trouble.")
 *
 * @a diff3_cmd must be non-NULL.
 *
 * Do all allocation in @a pool.
 *
 * @since New in 1.4.
 */
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
                   apr_pool_t *pool);

/** Similar to svn_io_run_diff3_3(), but with @a diff3_cmd encoded in
 * internal encoding used by APR.
 *
 * @deprecated Provided for backwards compatibility with the 1.5 API.
 * @since New in 1.4.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_run_diff3_2(int *exitcode,
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
                   apr_pool_t *pool);

/** Similar to svn_io_run_diff3_2(), but with @a user_args set to @c NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_io_run_diff3(const char *dir,
                 const char *mine,
                 const char *older,
                 const char *yours,
                 const char *mine_label,
                 const char *older_label,
                 const char *yours_label,
                 apr_file_t *merged,
                 int *exitcode,
                 const char *diff3_cmd,
                 apr_pool_t *pool);


/** Parse utf8-encoded @a mimetypes_file as a MIME types file (such as
 * is provided with Apache HTTP Server), and set @a *type_map to a
 * hash mapping <tt>const char *</tt> filename extensions to
 * <tt>const char *</tt> MIME types.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_io_parse_mimetypes_file(apr_hash_t **type_map,
                            const char *mimetypes_file,
                            apr_pool_t *pool);


/** Examine utf8-encoded @a file to determine if it can be described by a
 * known (as in, known by this function) Multipurpose Internet Mail
 * Extension (MIME) type.  If so, set @a *mimetype to a character string
 * describing the MIME type, else set it to @c NULL.
 *
 * If not @c NULL, @a mimetype_map is a hash mapping <tt>const char *</tt>
 * filename extensions to <tt>const char *</tt> MIME types, and is the
 * first source consulted regarding @a file's MIME type.
 *
 * Use @a pool for any necessary allocations.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_io_detect_mimetype2(const char **mimetype,
                        const char *file,
                        apr_hash_t *mimetype_map,
                        apr_pool_t *pool);


/** Like svn_io_detect_mimetype2, but with @a mimetypes_map set to
 * @c NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API
 */
SVN_DEPRECATED
svn_error_t *
svn_io_detect_mimetype(const char **mimetype,
                       const char *file,
                       apr_pool_t *pool);


/** Examine up to @a len bytes of data in @a buf to determine if the
 * can be considered binary data, in which case return TRUE.
 * If the data can be considered plain-text data, return FALSE.
 *
 * @since New in 1.7.
 */
svn_boolean_t
svn_io_is_binary_data(const void *buf, apr_size_t len);


/** Wrapper for apr_file_open().  @a fname is utf8-encoded.
    Always passed flag | APR_BINARY to apr. */
svn_error_t *
svn_io_file_open(apr_file_t **new_file,
                 const char *fname,
                 apr_int32_t flag,
                 apr_fileperms_t perm,
                 apr_pool_t *pool);


/** Wrapper for apr_file_close(). */
svn_error_t *
svn_io_file_close(apr_file_t *file,
                  apr_pool_t *pool);


/** Wrapper for apr_file_getc(). */
svn_error_t *
svn_io_file_getc(char *ch,
                 apr_file_t *file,
                 apr_pool_t *pool);


/** Wrapper for apr_file_putc().
  * @since New in 1.7
  */
svn_error_t *
svn_io_file_putc(char ch,
                 apr_file_t *file,
                 apr_pool_t *pool);


/** Wrapper for apr_file_info_get(). */
svn_error_t *
svn_io_file_info_get(apr_finfo_t *finfo,
                     apr_int32_t wanted,
                     apr_file_t *file,
                     apr_pool_t *pool);


/** Set @a *filesize_p to the size of @a file. Use @a pool for temporary
  * allocations.
  *
  * @note Use svn_io_file_info_get() to get more information about
  * apr_file_t.
  *
  * @since New in 1.10
  */
svn_error_t *
svn_io_file_size_get(svn_filesize_t *filesize_p, apr_file_t *file,
                     apr_pool_t *pool);

/** Fetch the current offset of @a file into @a *offset_p. Use @a pool for
  * temporary allocations.
  *
  * @since New in 1.10
  */
svn_error_t *
svn_io_file_get_offset(apr_off_t *offset_p,
                       apr_file_t *file,
                       apr_pool_t *pool);

/** Wrapper for apr_file_read(). */
svn_error_t *
svn_io_file_read(apr_file_t *file,
                 void *buf,
                 apr_size_t *nbytes,
                 apr_pool_t *pool);


/** Wrapper for apr_file_read_full().
 *
 * If @a hit_eof is not NULL, EOF will be indicated there and no
 * svn_error_t error object will be created upon EOF.
 *
 * @since New in 1.7
 */
svn_error_t *
svn_io_file_read_full2(apr_file_t *file,
                       void *buf,
                       apr_size_t nbytes,
                       apr_size_t *bytes_read,
                       svn_boolean_t *hit_eof,
                       apr_pool_t *pool);


/** Similar to svn_io_file_read_full2 with hit_eof being set
 * to @c NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API
 */
SVN_DEPRECATED
svn_error_t *
svn_io_file_read_full(apr_file_t *file,
                      void *buf,
                      apr_size_t nbytes,
                      apr_size_t *bytes_read,
                      apr_pool_t *pool);


/** Wrapper for apr_file_seek(). */
svn_error_t *
svn_io_file_seek(apr_file_t *file,
                 apr_seek_where_t where,
                 apr_off_t *offset,
                 apr_pool_t *pool);

/** Set the file pointer of the #APR_BUFFERED @a file to @a offset.  In
 * contrast to #svn_io_file_seek, this function will attempt to resize the
 * internal data buffer to @a block_size bytes and to read data aligned to
 * multiples of that value.  The beginning of the block will be returned
 * in @a buffer_start, if that is not NULL.
 * Uses @a scratch_pool for temporary allocations.
 *
 * @note Due to limitations of the APR API, the alignment may not be
 * successful.  If you never use any other seek function on @a file,
 * however, you are virtually guaranteed to get at least 4kByte alignment
 * for all reads.
 *
 * @note Calling this for non-buffered files is legal but inefficient.
 *
 * @since New in 1.9
 */
svn_error_t *
svn_io_file_aligned_seek(apr_file_t *file,
                         apr_off_t block_size,
                         apr_off_t *buffer_start,
                         apr_off_t offset,
                         apr_pool_t *scratch_pool);

/** Wrapper for apr_file_write(). */
svn_error_t *
svn_io_file_write(apr_file_t *file,
                  const void *buf,
                  apr_size_t *nbytes,
                  apr_pool_t *pool);

/** Wrapper for apr_file_flush().
 * @since New in 1.9
 */
svn_error_t *
svn_io_file_flush(apr_file_t *file,
                  apr_pool_t *scratch_pool);



/** Wrapper for apr_file_write_full(). */
svn_error_t *
svn_io_file_write_full(apr_file_t *file,
                       const void *buf,
                       apr_size_t nbytes,
                       apr_size_t *bytes_written,
                       apr_pool_t *pool);

/**
 * Writes @a nbytes bytes from @a *buf to a temporary file inside the same
 * directory as @a *final_path. Then syncs the temporary file to disk and
 * closes the file. After this rename the temporary file to @a final_path,
 * possibly replacing an existing file.
 *
 * If @a copy_perms_path is not NULL, copy the permissions applied on @a
 * @a copy_perms_path on the temporary file before renaming.
 *
 * If @a flush_to_disk is non-zero, do not return until the node has
 * actually been written on the disk.
 *
 * @note The flush to disk operation can be very expensive on systems
 * that implement flushing on all IO layers, like Windows. Please use
 * @a flush_to_disk flag only for critical data.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_io_write_atomic2(const char *final_path,
                     const void *buf,
                     apr_size_t nbytes,
                     const char *copy_perms_path,
                     svn_boolean_t flush_to_disk,
                     apr_pool_t *scratch_pool);

/** Similar to svn_io_write_atomic2(), but with @a flush_to_disk set
 * to @c TRUE.
 *
 * @since New in 1.9.
 *
 * @deprecated Provided for backward compatibility with the 1.9 API
 */
SVN_DEPRECATED
svn_error_t *
svn_io_write_atomic(const char *final_path,
                    const void *buf,
                    apr_size_t nbytes,
                    const char* copy_perms_path,
                    apr_pool_t *scratch_pool);

/**
 * Open a unique file in @a dirpath, and write @a nbytes from @a buf to
 * the file before flushing it to disk and closing it.  Return the name
 * of the newly created file in @a *tmp_path, allocated in @a pool.
 *
 * If @a dirpath is @c NULL, use the path returned from svn_io_temp_dir().
 * (Note that when using the system-provided temp directory, it may not
 * be possible to atomically rename the resulting file due to cross-device
 * issues.)
 *
 * The file will be deleted according to @a delete_when.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_io_write_unique(const char **tmp_path,
                    const char *dirpath,
                    const void *buf,
                    apr_size_t nbytes,
                    svn_io_file_del_t delete_when,
                    apr_pool_t *pool);

/** Wrapper for apr_file_trunc().
  * @since New in 1.6. */
svn_error_t *
svn_io_file_trunc(apr_file_t *file,
                  apr_off_t offset,
                  apr_pool_t *pool);


/** Wrapper for apr_stat().  @a fname is utf8-encoded. */
svn_error_t *
svn_io_stat(apr_finfo_t *finfo,
            const char *fname,
            apr_int32_t wanted,
            apr_pool_t *pool);


/** Rename and/or move the node (not necessarily a regular file) at
 * @a from_path to a new path @a to_path within the same filesystem.
 * In some cases, an existing node at @a to_path will be overwritten.
 *
 * @a from_path and @a to_path are utf8-encoded.  If @a flush_to_disk
 * is non-zero, do not return until the node has actually been moved on
 * the disk.
 *
 * @note The flush to disk operation can be very expensive on systems
 * that implement flushing on all IO layers, like Windows. Please use
 * @a flush_to_disk flag only for critical data.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_io_file_rename2(const char *from_path, const char *to_path,
                    svn_boolean_t flush_to_disk, apr_pool_t *pool);

/** Similar to svn_io_file_rename2(), but with @a flush_to_disk set
 * to @c FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.9 API
 */
SVN_DEPRECATED
svn_error_t *
svn_io_file_rename(const char *from_path,
                   const char *to_path,
                   apr_pool_t *pool);


/** Move the file from @a from_path to @a to_path, even across device
 * boundaries. Overwrite @a to_path if it exists.
 *
 * @note This function is different from svn_io_file_rename in that the
 * latter fails in the 'across device boundaries' case.
 *
 * @since New in 1.3.
 */
svn_error_t *
svn_io_file_move(const char *from_path,
                 const char *to_path,
                 apr_pool_t *pool);


/** Wrapper for apr_dir_make().  @a path is utf8-encoded. */
svn_error_t *
svn_io_dir_make(const char *path,
                apr_fileperms_t perm,
                apr_pool_t *pool);

/** Same as svn_io_dir_make(), but sets the hidden attribute on the
    directory on systems that support it. */
svn_error_t *
svn_io_dir_make_hidden(const char *path,
                       apr_fileperms_t perm,
                       apr_pool_t *pool);

/**
 * Same as svn_io_dir_make(), but attempts to set the sgid on the
 * directory on systems that support it.  Does not return an error if
 * the attempt to set the sgid bit fails.  On Unix filesystems,
 * setting the sgid bit on a directory ensures that files and
 * subdirectories created within inherit group ownership from the
 * parent instead of from the primary gid.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_io_dir_make_sgid(const char *path,
                     apr_fileperms_t perm,
                     apr_pool_t *pool);

/** Wrapper for apr_dir_open().  @a dirname is utf8-encoded. */
svn_error_t *
svn_io_dir_open(apr_dir_t **new_dir,
                const char *dirname,
                apr_pool_t *pool);

/** Wrapper for apr_dir_close().
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_io_dir_close(apr_dir_t *thedir);

/** Wrapper for apr_dir_remove().  @a dirname is utf8-encoded.
 * @note This function has this name to avoid confusion with
 * svn_io_remove_dir2(), which is recursive.
 */
svn_error_t *
svn_io_dir_remove_nonrecursive(const char *dirname,
                               apr_pool_t *pool);


/** Wrapper for apr_dir_read().  Ensures that @a finfo->name is
 * utf8-encoded, which means allocating @a finfo->name in @a pool,
 * which may or may not be the same as @a finfo's pool.  Use @a pool
 * for error allocation as well.
 */
svn_error_t *
svn_io_dir_read(apr_finfo_t *finfo,
                apr_int32_t wanted,
                apr_dir_t *thedir,
                apr_pool_t *pool);

/** Wrapper for apr_file_name_get().  @a *filename is utf8-encoded.
 *
 * @note The file name may be NULL.
 *
 * @since New in 1.7. */
svn_error_t *
svn_io_file_name_get(const char **filename,
                     apr_file_t *file,
                     apr_pool_t *pool);



/** Version/format files.
 *
 * @defgroup svn_io_format_files Version/format files
 * @{
 */

/** Set @a *version to the integer that starts the file at @a path.  If the
 * file does not begin with a series of digits followed by a newline,
 * return the error #SVN_ERR_BAD_VERSION_FILE_FORMAT.  Use @a pool for
 * all allocations.
 */
svn_error_t *
svn_io_read_version_file(int *version,
                         const char *path,
                         apr_pool_t *pool);

/** Create (or overwrite) the file at @a path with new contents,
 * formatted as a non-negative integer @a version followed by a single
 * newline.  On successful completion the file will be read-only.  Use
 * @a pool for all allocations.
 */
svn_error_t *
svn_io_write_version_file(const char *path,
                          int version,
                          apr_pool_t *pool);

/** Read a line of text from a file, up to a specified length.
 *
 * Allocate @a *stringbuf in @a result_pool, and read into it one line
 * from @a file. Reading stops either after a line-terminator was found
 * or after @a max_len bytes have been read.
 *
 * If end-of-file is reached or @a max_len bytes have been read, and @a eof
 * is not NULL, then set @a *eof to @c TRUE.
 *
 * The line-terminator is not stored in @a *stringbuf.
 * The line-terminator is detected automatically and stored in @a *eol
 * if @a eol is not NULL. If EOF is reached and @a file does not end
 * with a newline character, and @a eol is not NULL, @ *eol is set to NULL.
 *
 * @a scratch_pool is used for temporary allocations.
 *
 * Hint: To read all data until a line-terminator is hit, pass APR_SIZE_MAX
 * for @a max_len.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_io_file_readline(apr_file_t *file,
                     svn_stringbuf_t **stringbuf,
                     const char **eol,
                     svn_boolean_t *eof,
                     apr_size_t max_len,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_IO_H */
