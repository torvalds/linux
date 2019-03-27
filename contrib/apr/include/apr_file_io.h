/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APR_FILE_IO_H
#define APR_FILE_IO_H

/**
 * @file apr_file_io.h
 * @brief APR File I/O Handling
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_time.h"
#include "apr_errno.h"
#include "apr_file_info.h"
#include "apr_inherit.h"

#define APR_WANT_STDIO          /**< for SEEK_* */
#define APR_WANT_IOVEC          /**< for apr_file_writev */
#include "apr_want.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_file_io File I/O Handling Functions
 * @ingroup APR 
 * @{
 */

/**
 * @defgroup apr_file_open_flags File Open Flags/Routines
 * @{
 */

/* Note to implementors: Values in the range 0x00100000--0x80000000
   are reserved for platform-specific values. */

#define APR_FOPEN_READ       0x00001  /**< Open the file for reading */
#define APR_FOPEN_WRITE      0x00002  /**< Open the file for writing */
#define APR_FOPEN_CREATE     0x00004  /**< Create the file if not there */
#define APR_FOPEN_APPEND     0x00008  /**< Append to the end of the file */
#define APR_FOPEN_TRUNCATE   0x00010  /**< Open the file and truncate
                                         to 0 length */
#define APR_FOPEN_BINARY     0x00020  /**< Open the file in binary mode
				         (This flag is ignored on UNIX 
					 because it has no meaning)*/
#define APR_FOPEN_EXCL       0x00040  /**< Open should fail if #APR_FOPEN_CREATE
                                         and file exists. */
#define APR_FOPEN_BUFFERED   0x00080  /**< Open the file for buffered I/O */
#define APR_FOPEN_DELONCLOSE 0x00100  /**< Delete the file after close */
#define APR_FOPEN_XTHREAD    0x00200  /**< Platform dependent tag to open
                                         the file for use across multiple
                                         threads */
#define APR_FOPEN_SHARELOCK  0x00400  /**< Platform dependent support for
                                         higher level locked read/write
                                         access to support writes across
                                         process/machines */
#define APR_FOPEN_NOCLEANUP  0x00800  /**< Do not register a cleanup
                                         when the file is opened. The
					 apr_os_file_t handle in apr_file_t
					 will not be closed when the pool
					 is destroyed. */
#define APR_FOPEN_SENDFILE_ENABLED 0x01000 /**< Advisory flag that this
                                             file should support
                                             apr_socket_sendfile operation */
#define APR_FOPEN_LARGEFILE   0x04000 /**< Platform dependent flag to enable
                                       * large file support, see WARNING below
                                       */
#define APR_FOPEN_SPARSE      0x08000 /**< Platform dependent flag to enable
                                       * sparse file support, see WARNING below
                                       */
#define APR_FOPEN_NONBLOCK    0x40000 /**< Platform dependent flag to enable
                                       * non blocking file io */


/* backcompat */
#define APR_READ             APR_FOPEN_READ       /**< @deprecated @see APR_FOPEN_READ */
#define APR_WRITE            APR_FOPEN_WRITE      /**< @deprecated @see APR_FOPEN_WRITE */   
#define APR_CREATE           APR_FOPEN_CREATE     /**< @deprecated @see APR_FOPEN_CREATE */   
#define APR_APPEND           APR_FOPEN_APPEND     /**< @deprecated @see APR_FOPEN_APPEND */   
#define APR_TRUNCATE         APR_FOPEN_TRUNCATE   /**< @deprecated @see APR_FOPEN_TRUNCATE */   
#define APR_BINARY           APR_FOPEN_BINARY     /**< @deprecated @see APR_FOPEN_BINARY */   
#define APR_EXCL             APR_FOPEN_EXCL       /**< @deprecated @see APR_FOPEN_EXCL */   
#define APR_BUFFERED         APR_FOPEN_BUFFERED   /**< @deprecated @see APR_FOPEN_BUFFERED */   
#define APR_DELONCLOSE       APR_FOPEN_DELONCLOSE /**< @deprecated @see APR_FOPEN_DELONCLOSE */   
#define APR_XTHREAD          APR_FOPEN_XTHREAD    /**< @deprecated @see APR_FOPEN_XTHREAD */   
#define APR_SHARELOCK        APR_FOPEN_SHARELOCK  /**< @deprecated @see APR_FOPEN_SHARELOCK */   
#define APR_FILE_NOCLEANUP   APR_FOPEN_NOCLEANUP  /**< @deprecated @see APR_FOPEN_NOCLEANUP */   
#define APR_SENDFILE_ENABLED APR_FOPEN_SENDFILE_ENABLED /**< @deprecated @see APR_FOPEN_SENDFILE_ENABLED */   
#define APR_LARGEFILE        APR_FOPEN_LARGEFILE  /**< @deprecated @see APR_FOPEN_LARGEFILE */   

/** @def APR_FOPEN_LARGEFILE 
 * @warning APR_FOPEN_LARGEFILE flag only has effect on some
 * platforms where sizeof(apr_off_t) == 4.  Where implemented, it
 * allows opening and writing to a file which exceeds the size which
 * can be represented by apr_off_t (2 gigabytes).  When a file's size
 * does exceed 2Gb, apr_file_info_get() will fail with an error on the
 * descriptor, likewise apr_stat()/apr_lstat() will fail on the
 * filename.  apr_dir_read() will fail with #APR_INCOMPLETE on a
 * directory entry for a large file depending on the particular
 * APR_FINFO_* flags.  Generally, it is not recommended to use this
 * flag.
 *
 * @def APR_FOPEN_SPARSE
 * @warning APR_FOPEN_SPARSE may, depending on platform, convert a
 * normal file to a sparse file.  Some applications may be unable
 * to decipher a sparse file, so it's critical that the sparse file
 * flag should only be used for files accessed only by APR or other
 * applications known to be able to decipher them.  APR does not
 * guarantee that it will compress the file into sparse segments
 * if it was previously created and written without the sparse flag.
 * On platforms which do not understand, or on file systems which
 * cannot handle sparse files, the flag is ignored by apr_file_open().
 *
 * @def APR_FOPEN_NONBLOCK
 * @warning APR_FOPEN_NONBLOCK is not implemented on all platforms.
 * Callers should be prepared for it to fail with #APR_ENOTIMPL.
 */

/** @} */

/**
 * @defgroup apr_file_seek_flags File Seek Flags
 * @{
 */

/* flags for apr_file_seek */
/** Set the file position */
#define APR_SET SEEK_SET
/** Current */
#define APR_CUR SEEK_CUR
/** Go to end of file */
#define APR_END SEEK_END
/** @} */

/**
 * @defgroup apr_file_attrs_set_flags File Attribute Flags
 * @{
 */

/* flags for apr_file_attrs_set */
#define APR_FILE_ATTR_READONLY   0x01          /**< File is read-only */
#define APR_FILE_ATTR_EXECUTABLE 0x02          /**< File is executable */
#define APR_FILE_ATTR_HIDDEN     0x04          /**< File is hidden */
/** @} */

/**
 * @defgroup apr_file_writev{_full} max iovec size
 * @{
 */
#if defined(DOXYGEN)
#define APR_MAX_IOVEC_SIZE 1024                /**< System dependent maximum 
                                                    size of an iovec array */
#elif defined(IOV_MAX)
#define APR_MAX_IOVEC_SIZE IOV_MAX
#elif defined(MAX_IOVEC)
#define APR_MAX_IOVEC_SIZE MAX_IOVEC
#else
#define APR_MAX_IOVEC_SIZE 1024
#endif
/** @} */

/** File attributes */
typedef apr_uint32_t apr_fileattrs_t;

/** Type to pass as whence argument to apr_file_seek. */
typedef int       apr_seek_where_t;

/**
 * Structure for referencing files.
 */
typedef struct apr_file_t         apr_file_t;

/* File lock types/flags */
/**
 * @defgroup apr_file_lock_types File Lock Types
 * @{
 */

#define APR_FLOCK_SHARED        1       /**< Shared lock. More than one process
                                           or thread can hold a shared lock
                                           at any given time. Essentially,
                                           this is a "read lock", preventing
                                           writers from establishing an
                                           exclusive lock. */
#define APR_FLOCK_EXCLUSIVE     2       /**< Exclusive lock. Only one process
                                           may hold an exclusive lock at any
                                           given time. This is analogous to
                                           a "write lock". */

#define APR_FLOCK_TYPEMASK      0x000F  /**< mask to extract lock type */
#define APR_FLOCK_NONBLOCK      0x0010  /**< do not block while acquiring the
                                           file lock */
/** @} */

/**
 * Open the specified file.
 * @param newf The opened file descriptor.
 * @param fname The full path to the file (using / on all systems)
 * @param flag Or'ed value of:
 * @li #APR_FOPEN_READ           open for reading
 * @li #APR_FOPEN_WRITE          open for writing
 * @li #APR_FOPEN_CREATE         create the file if not there
 * @li #APR_FOPEN_APPEND         file ptr is set to end prior to all writes
 * @li #APR_FOPEN_TRUNCATE       set length to zero if file exists
 * @li #APR_FOPEN_BINARY         not a text file
 * @li #APR_FOPEN_BUFFERED       buffer the data.  Default is non-buffered
 * @li #APR_FOPEN_EXCL           return error if #APR_FOPEN_CREATE and file exists
 * @li #APR_FOPEN_DELONCLOSE     delete the file after closing
 * @li #APR_FOPEN_XTHREAD        Platform dependent tag to open the file
 *                               for use across multiple threads
 * @li #APR_FOPEN_SHARELOCK      Platform dependent support for higher
 *                               level locked read/write access to support
 *                               writes across process/machines
 * @li #APR_FOPEN_NOCLEANUP      Do not register a cleanup with the pool 
 *                               passed in on the @a pool argument (see below)
 * @li #APR_FOPEN_SENDFILE_ENABLED  Open with appropriate platform semantics
 *                               for sendfile operations.  Advisory only,
 *                               apr_socket_sendfile does not check this flag
 * @li #APR_FOPEN_LARGEFILE      Platform dependent flag to enable large file
 *                               support, see WARNING below 
 * @li #APR_FOPEN_SPARSE         Platform dependent flag to enable sparse file
 *                               support, see WARNING below
 * @li #APR_FOPEN_NONBLOCK       Platform dependent flag to enable
 *                               non blocking file io
 * @param perm Access permissions for file.
 * @param pool The pool to use.
 * @remark If perm is #APR_FPROT_OS_DEFAULT and the file is being created,
 * appropriate default permissions will be used.
 * @remark By default, the returned file descriptor will not be
 * inherited by child processes created by apr_proc_create().  This
 * can be changed using apr_file_inherit_set().
 */
APR_DECLARE(apr_status_t) apr_file_open(apr_file_t **newf, const char *fname,
                                        apr_int32_t flag, apr_fileperms_t perm,
                                        apr_pool_t *pool);

/**
 * Close the specified file.
 * @param file The file descriptor to close.
 */
APR_DECLARE(apr_status_t) apr_file_close(apr_file_t *file);

/**
 * Delete the specified file.
 * @param path The full path to the file (using / on all systems)
 * @param pool The pool to use.
 * @remark If the file is open, it won't be removed until all
 * instances are closed.
 */
APR_DECLARE(apr_status_t) apr_file_remove(const char *path, apr_pool_t *pool);

/**
 * Rename the specified file.
 * @param from_path The full path to the original file (using / on all systems)
 * @param to_path The full path to the new file (using / on all systems)
 * @param pool The pool to use.
 * @warning If a file exists at the new location, then it will be
 * overwritten.  Moving files or directories across devices may not be
 * possible.
 */
APR_DECLARE(apr_status_t) apr_file_rename(const char *from_path, 
                                          const char *to_path,
                                          apr_pool_t *pool);

/**
 * Create a hard link to the specified file.
 * @param from_path The full path to the original file (using / on all systems)
 * @param to_path The full path to the new file (using / on all systems)
 * @remark Both files must reside on the same device.
 */
APR_DECLARE(apr_status_t) apr_file_link(const char *from_path, 
                                          const char *to_path);

/**
 * Copy the specified file to another file.
 * @param from_path The full path to the original file (using / on all systems)
 * @param to_path The full path to the new file (using / on all systems)
 * @param perms Access permissions for the new file if it is created.
 *     In place of the usual or'd combination of file permissions, the
 *     value #APR_FPROT_FILE_SOURCE_PERMS may be given, in which case the source
 *     file's permissions are copied.
 * @param pool The pool to use.
 * @remark The new file does not need to exist, it will be created if required.
 * @warning If the new file already exists, its contents will be overwritten.
 */
APR_DECLARE(apr_status_t) apr_file_copy(const char *from_path, 
                                        const char *to_path,
                                        apr_fileperms_t perms,
                                        apr_pool_t *pool);

/**
 * Append the specified file to another file.
 * @param from_path The full path to the source file (use / on all systems)
 * @param to_path The full path to the destination file (use / on all systems)
 * @param perms Access permissions for the destination file if it is created.
 *     In place of the usual or'd combination of file permissions, the
 *     value #APR_FPROT_FILE_SOURCE_PERMS may be given, in which case the source
 *     file's permissions are copied.
 * @param pool The pool to use.
 * @remark The new file does not need to exist, it will be created if required.
 */
APR_DECLARE(apr_status_t) apr_file_append(const char *from_path, 
                                          const char *to_path,
                                          apr_fileperms_t perms,
                                          apr_pool_t *pool);

/**
 * Are we at the end of the file
 * @param fptr The apr file we are testing.
 * @remark Returns #APR_EOF if we are at the end of file, #APR_SUCCESS otherwise.
 */
APR_DECLARE(apr_status_t) apr_file_eof(apr_file_t *fptr);

/**
 * Open standard error as an apr file pointer.
 * @param thefile The apr file to use as stderr.
 * @param pool The pool to allocate the file out of.
 * 
 * @remark The only reason that the apr_file_open_std* functions exist
 * is that you may not always have a stderr/out/in on Windows.  This
 * is generally a problem with newer versions of Windows and services.
 * 
 * @remark The other problem is that the C library functions generally work
 * differently on Windows and Unix.  So, by using apr_file_open_std*
 * functions, you can get a handle to an APR struct that works with
 * the APR functions which are supposed to work identically on all
 * platforms.
 */
APR_DECLARE(apr_status_t) apr_file_open_stderr(apr_file_t **thefile,
                                               apr_pool_t *pool);

/**
 * open standard output as an apr file pointer.
 * @param thefile The apr file to use as stdout.
 * @param pool The pool to allocate the file out of.
 * 
 * @remark See remarks for apr_file_open_stderr().
 */
APR_DECLARE(apr_status_t) apr_file_open_stdout(apr_file_t **thefile,
                                               apr_pool_t *pool);

/**
 * open standard input as an apr file pointer.
 * @param thefile The apr file to use as stdin.
 * @param pool The pool to allocate the file out of.
 * 
 * @remark See remarks for apr_file_open_stderr().
 */
APR_DECLARE(apr_status_t) apr_file_open_stdin(apr_file_t **thefile,
                                              apr_pool_t *pool);

/**
 * open standard error as an apr file pointer, with flags.
 * @param thefile The apr file to use as stderr.
 * @param flags The flags to open the file with. Only the 
 *              @li #APR_FOPEN_EXCL
 *              @li #APR_FOPEN_BUFFERED
 *              @li #APR_FOPEN_XTHREAD
 *              @li #APR_FOPEN_SHARELOCK 
 *              @li #APR_FOPEN_SENDFILE_ENABLED
 *              @li #APR_FOPEN_LARGEFILE
 *
 *              flags should be used. The #APR_FOPEN_WRITE flag will
 *              be set unconditionally.
 * @param pool The pool to allocate the file out of.
 * 
 * @remark See remarks for apr_file_open_stderr().
 */
APR_DECLARE(apr_status_t) apr_file_open_flags_stderr(apr_file_t **thefile,
                                                     apr_int32_t flags,
                                                     apr_pool_t *pool);

/**
 * open standard output as an apr file pointer, with flags.
 * @param thefile The apr file to use as stdout.
 * @param flags The flags to open the file with. Only the 
 *              @li #APR_FOPEN_EXCL
 *              @li #APR_FOPEN_BUFFERED
 *              @li #APR_FOPEN_XTHREAD
 *              @li #APR_FOPEN_SHARELOCK 
 *              @li #APR_FOPEN_SENDFILE_ENABLED
 *              @li #APR_FOPEN_LARGEFILE
 *
 *              flags should be used. The #APR_FOPEN_WRITE flag will
 *              be set unconditionally.
 * @param pool The pool to allocate the file out of.
 * 
 * @remark See remarks for apr_file_open_stderr().
 */
APR_DECLARE(apr_status_t) apr_file_open_flags_stdout(apr_file_t **thefile,
                                                     apr_int32_t flags,
                                                     apr_pool_t *pool);

/**
 * open standard input as an apr file pointer, with flags.
 * @param thefile The apr file to use as stdin.
 * @param flags The flags to open the file with. Only the 
 *              @li #APR_FOPEN_EXCL
 *              @li #APR_FOPEN_BUFFERED
 *              @li #APR_FOPEN_XTHREAD
 *              @li #APR_FOPEN_SHARELOCK 
 *              @li #APR_FOPEN_SENDFILE_ENABLED
 *              @li #APR_FOPEN_LARGEFILE
 *
 *              flags should be used. The #APR_FOPEN_WRITE flag will
 *              be set unconditionally.
 * @param pool The pool to allocate the file out of.
 * 
 * @remark See remarks for apr_file_open_stderr().
 */
APR_DECLARE(apr_status_t) apr_file_open_flags_stdin(apr_file_t **thefile,
                                                     apr_int32_t flags,
                                                     apr_pool_t *pool);

/**
 * Read data from the specified file.
 * @param thefile The file descriptor to read from.
 * @param buf The buffer to store the data to.
 * @param nbytes On entry, the number of bytes to read; on exit, the number
 * of bytes read.
 *
 * @remark apr_file_read() will read up to the specified number of
 * bytes, but never more.  If there isn't enough data to fill that
 * number of bytes, all of the available data is read.  The third
 * argument is modified to reflect the number of bytes read.  If a
 * char was put back into the stream via ungetc, it will be the first
 * character returned.
 *
 * @remark It is not possible for both bytes to be read and an #APR_EOF
 * or other error to be returned.  #APR_EINTR is never returned.
 */
APR_DECLARE(apr_status_t) apr_file_read(apr_file_t *thefile, void *buf,
                                        apr_size_t *nbytes);

/**
 * Write data to the specified file.
 * @param thefile The file descriptor to write to.
 * @param buf The buffer which contains the data.
 * @param nbytes On entry, the number of bytes to write; on exit, the number 
 *               of bytes written.
 *
 * @remark apr_file_write() will write up to the specified number of
 * bytes, but never more.  If the OS cannot write that many bytes, it
 * will write as many as it can.  The third argument is modified to
 * reflect the * number of bytes written.
 *
 * @remark It is possible for both bytes to be written and an error to
 * be returned.  #APR_EINTR is never returned.
 */
APR_DECLARE(apr_status_t) apr_file_write(apr_file_t *thefile, const void *buf,
                                         apr_size_t *nbytes);

/**
 * Write data from iovec array to the specified file.
 * @param thefile The file descriptor to write to.
 * @param vec The array from which to get the data to write to the file.
 * @param nvec The number of elements in the struct iovec array. This must 
 *             be smaller than #APR_MAX_IOVEC_SIZE.  If it isn't, the function 
 *             will fail with #APR_EINVAL.
 * @param nbytes The number of bytes written.
 *
 * @remark It is possible for both bytes to be written and an error to
 * be returned.  #APR_EINTR is never returned.
 *
 * @remark apr_file_writev() is available even if the underlying
 * operating system doesn't provide writev().
 */
APR_DECLARE(apr_status_t) apr_file_writev(apr_file_t *thefile,
                                          const struct iovec *vec,
                                          apr_size_t nvec, apr_size_t *nbytes);

/**
 * Read data from the specified file, ensuring that the buffer is filled
 * before returning.
 * @param thefile The file descriptor to read from.
 * @param buf The buffer to store the data to.
 * @param nbytes The number of bytes to read.
 * @param bytes_read If non-NULL, this will contain the number of bytes read.
 *
 * @remark apr_file_read_full() will read up to the specified number of
 * bytes, but never more.  If there isn't enough data to fill that
 * number of bytes, then the process/thread will block until it is
 * available or EOF is reached.  If a char was put back into the
 * stream via ungetc, it will be the first character returned.
 *
 * @remark It is possible for both bytes to be read and an error to be
 * returned.  And if *bytes_read is less than nbytes, an accompanying
 * error is _always_ returned.
 *
 * @remark #APR_EINTR is never returned.
 */
APR_DECLARE(apr_status_t) apr_file_read_full(apr_file_t *thefile, void *buf,
                                             apr_size_t nbytes,
                                             apr_size_t *bytes_read);

/**
 * Write data to the specified file, ensuring that all of the data is
 * written before returning.
 * @param thefile The file descriptor to write to.
 * @param buf The buffer which contains the data.
 * @param nbytes The number of bytes to write.
 * @param bytes_written If non-NULL, set to the number of bytes written.
 * 
 * @remark apr_file_write_full() will write up to the specified number of
 * bytes, but never more.  If the OS cannot write that many bytes, the
 * process/thread will block until they can be written. Exceptional
 * error such as "out of space" or "pipe closed" will terminate with
 * an error.
 *
 * @remark It is possible for both bytes to be written and an error to
 * be returned.  And if *bytes_written is less than nbytes, an
 * accompanying error is _always_ returned.
 *
 * @remark #APR_EINTR is never returned.
 */
APR_DECLARE(apr_status_t) apr_file_write_full(apr_file_t *thefile, 
                                              const void *buf,
                                              apr_size_t nbytes, 
                                              apr_size_t *bytes_written);


/**
 * Write data from iovec array to the specified file, ensuring that all of the
 * data is written before returning.
 * @param thefile The file descriptor to write to.
 * @param vec The array from which to get the data to write to the file.
 * @param nvec The number of elements in the struct iovec array. This must 
 *             be smaller than #APR_MAX_IOVEC_SIZE.  If it isn't, the function 
 *             will fail with #APR_EINVAL.
 * @param nbytes The number of bytes written.
 *
 * @remark apr_file_writev_full() is available even if the underlying
 * operating system doesn't provide writev().
 */
APR_DECLARE(apr_status_t) apr_file_writev_full(apr_file_t *thefile,
                                               const struct iovec *vec,
                                               apr_size_t nvec,
                                               apr_size_t *nbytes);
/**
 * Write a character into the specified file.
 * @param ch The character to write.
 * @param thefile The file descriptor to write to
 */
APR_DECLARE(apr_status_t) apr_file_putc(char ch, apr_file_t *thefile);

/**
 * Read a character from the specified file.
 * @param ch The character to read into
 * @param thefile The file descriptor to read from
 */
APR_DECLARE(apr_status_t) apr_file_getc(char *ch, apr_file_t *thefile);

/**
 * Put a character back onto a specified stream.
 * @param ch The character to write.
 * @param thefile The file descriptor to write to
 */
APR_DECLARE(apr_status_t) apr_file_ungetc(char ch, apr_file_t *thefile);

/**
 * Read a line from the specified file
 * @param str The buffer to store the string in. 
 * @param len The length of the string
 * @param thefile The file descriptor to read from
 * @remark The buffer will be NUL-terminated if any characters are stored.
 *         The newline at the end of the line will not be stripped.
 */
APR_DECLARE(apr_status_t) apr_file_gets(char *str, int len, 
                                        apr_file_t *thefile);

/**
 * Write the string into the specified file.
 * @param str The string to write. 
 * @param thefile The file descriptor to write to
 */
APR_DECLARE(apr_status_t) apr_file_puts(const char *str, apr_file_t *thefile);

/**
 * Flush the file's buffer.
 * @param thefile The file descriptor to flush
 */
APR_DECLARE(apr_status_t) apr_file_flush(apr_file_t *thefile);

/**
 * Transfer all file modified data and metadata to disk.
 * @param thefile The file descriptor to sync
 */
APR_DECLARE(apr_status_t) apr_file_sync(apr_file_t *thefile);

/**
 * Transfer all file modified data to disk.
 * @param thefile The file descriptor to sync
 */
APR_DECLARE(apr_status_t) apr_file_datasync(apr_file_t *thefile);

/**
 * Duplicate the specified file descriptor.
 * @param new_file The structure to duplicate into. 
 * @param old_file The file to duplicate.
 * @param p The pool to use for the new file.
 * @remark *new_file must point to a valid apr_file_t, or point to NULL.
 */         
APR_DECLARE(apr_status_t) apr_file_dup(apr_file_t **new_file,
                                       apr_file_t *old_file,
                                       apr_pool_t *p);

/**
 * Duplicate the specified file descriptor and close the original
 * @param new_file The old file that is to be closed and reused
 * @param old_file The file to duplicate
 * @param p        The pool to use for the new file
 *
 * @remark new_file MUST point at a valid apr_file_t. It cannot be NULL.
 */
APR_DECLARE(apr_status_t) apr_file_dup2(apr_file_t *new_file,
                                        apr_file_t *old_file,
                                        apr_pool_t *p);

/**
 * Move the specified file descriptor to a new pool
 * @param new_file Pointer in which to return the new apr_file_t
 * @param old_file The file to move
 * @param p        The pool to which the descriptor is to be moved
 * @remark Unlike apr_file_dup2(), this function doesn't do an
 *         OS dup() operation on the underlying descriptor; it just
 *         moves the descriptor's apr_file_t wrapper to a new pool.
 * @remark The new pool need not be an ancestor of old_file's pool.
 * @remark After calling this function, old_file may not be used
 */
APR_DECLARE(apr_status_t) apr_file_setaside(apr_file_t **new_file,
                                            apr_file_t *old_file,
                                            apr_pool_t *p);

/**
 * Give the specified apr file handle a new buffer 
 * @param thefile  The file handle that is to be modified
 * @param buffer   The buffer
 * @param bufsize  The size of the buffer
 * @remark It is possible to add a buffer to previously unbuffered
 *         file handles, the #APR_FOPEN_BUFFERED flag will be added to
 *         the file handle's flags. Likewise, with buffer=NULL and
 *         bufsize=0 arguments it is possible to make a previously
 *         buffered file handle unbuffered.
 */
APR_DECLARE(apr_status_t) apr_file_buffer_set(apr_file_t *thefile,
                                              char * buffer,
                                              apr_size_t bufsize);

/**
 * Get the size of any buffer for the specified apr file handle 
 * @param thefile  The file handle 
 */
APR_DECLARE(apr_size_t) apr_file_buffer_size_get(apr_file_t *thefile);

/**
 * Move the read/write file offset to a specified byte within a file.
 * @param thefile The file descriptor
 * @param where How to move the pointer, one of:
 *              @li #APR_SET  --  set the offset to offset
 *              @li #APR_CUR  --  add the offset to the current position 
 *              @li #APR_END  --  add the offset to the current file size 
 * @param offset The offset to move the pointer to.
 * @remark The third argument is modified to be the offset the pointer
          was actually moved to.
 */
APR_DECLARE(apr_status_t) apr_file_seek(apr_file_t *thefile, 
                                   apr_seek_where_t where,
                                   apr_off_t *offset);

/**
 * Create an anonymous pipe.
 * @param in The newly created pipe's file for reading.
 * @param out The newly created pipe's file for writing.
 * @param pool The pool to operate on.
 * @remark By default, the returned file descriptors will be inherited
 * by child processes created using apr_proc_create().  This can be
 * changed using apr_file_inherit_unset().
 * @bug  Some platforms cannot toggle between blocking and nonblocking,
 * and when passing a pipe as a standard handle to an application which
 * does not expect it, a non-blocking stream will fluxor the client app.
 * @deprecated @see apr_file_pipe_create_ex()
 */
APR_DECLARE(apr_status_t) apr_file_pipe_create(apr_file_t **in, 
                                               apr_file_t **out,
                                               apr_pool_t *pool);

/**
 * Create an anonymous pipe which portably supports async timeout options.
 * @param in The newly created pipe's file for reading.
 * @param out The newly created pipe's file for writing.
 * @param blocking one of these values defined in apr_thread_proc.h;
 *                 @li #APR_FULL_BLOCK
 *                 @li #APR_READ_BLOCK
 *                 @li #APR_WRITE_BLOCK
 *                 @li #APR_FULL_NONBLOCK
 * @param pool The pool to operate on.
 * @remark By default, the returned file descriptors will be inherited
 * by child processes created using apr_proc_create().  This can be
 * changed using apr_file_inherit_unset().
 * @remark Some platforms cannot toggle between blocking and nonblocking,
 * and when passing a pipe as a standard handle to an application which
 * does not expect it, a non-blocking stream will fluxor the client app.
 * Use this function rather than apr_file_pipe_create() to create pipes 
 * where one or both ends require non-blocking semantics.
 */
APR_DECLARE(apr_status_t) apr_file_pipe_create_ex(apr_file_t **in, 
                                                  apr_file_t **out, 
                                                  apr_int32_t blocking, 
                                                  apr_pool_t *pool);

/**
 * Create a named pipe.
 * @param filename The filename of the named pipe
 * @param perm The permissions for the newly created pipe.
 * @param pool The pool to operate on.
 */
APR_DECLARE(apr_status_t) apr_file_namedpipe_create(const char *filename, 
                                                    apr_fileperms_t perm, 
                                                    apr_pool_t *pool);

/**
 * Get the timeout value for a pipe or manipulate the blocking state.
 * @param thepipe The pipe we are getting a timeout for.
 * @param timeout The current timeout value in microseconds. 
 */
APR_DECLARE(apr_status_t) apr_file_pipe_timeout_get(apr_file_t *thepipe, 
                                               apr_interval_time_t *timeout);

/**
 * Set the timeout value for a pipe or manipulate the blocking state.
 * @param thepipe The pipe we are setting a timeout on.
 * @param timeout The timeout value in microseconds.  Values < 0 mean wait 
 *        forever, 0 means do not wait at all.
 */
APR_DECLARE(apr_status_t) apr_file_pipe_timeout_set(apr_file_t *thepipe, 
                                                  apr_interval_time_t timeout);

/** file (un)locking functions. */

/**
 * Establish a lock on the specified, open file. The lock may be advisory
 * or mandatory, at the discretion of the platform. The lock applies to
 * the file as a whole, rather than a specific range. Locks are established
 * on a per-thread/process basis; a second lock by the same thread will not
 * block.
 * @param thefile The file to lock.
 * @param type The type of lock to establish on the file.
 */
APR_DECLARE(apr_status_t) apr_file_lock(apr_file_t *thefile, int type);

/**
 * Remove any outstanding locks on the file.
 * @param thefile The file to unlock.
 */
APR_DECLARE(apr_status_t) apr_file_unlock(apr_file_t *thefile);

/**accessor and general file_io functions. */

/**
 * return the file name of the current file.
 * @param new_path The path of the file.  
 * @param thefile The currently open file.
 */                     
APR_DECLARE(apr_status_t) apr_file_name_get(const char **new_path, 
                                            apr_file_t *thefile);
    
/**
 * Return the data associated with the current file.
 * @param data The user data associated with the file.  
 * @param key The key to use for retrieving data associated with this file.
 * @param file The currently open file.
 */                     
APR_DECLARE(apr_status_t) apr_file_data_get(void **data, const char *key, 
                                            apr_file_t *file);

/**
 * Set the data associated with the current file.
 * @param file The currently open file.
 * @param data The user data to associate with the file.  
 * @param key The key to use for associating data with the file.
 * @param cleanup The cleanup routine to use when the file is destroyed.
 */                     
APR_DECLARE(apr_status_t) apr_file_data_set(apr_file_t *file, void *data,
                                            const char *key,
                                            apr_status_t (*cleanup)(void *));

/**
 * Write a string to a file using a printf format.
 * @param fptr The file to write to.
 * @param format The format string
 * @param ... The values to substitute in the format string
 * @return The number of bytes written
 */ 
APR_DECLARE_NONSTD(int) apr_file_printf(apr_file_t *fptr, 
                                        const char *format, ...)
        __attribute__((format(printf,2,3)));

/**
 * set the specified file's permission bits.
 * @param fname The file (name) to apply the permissions to.
 * @param perms The permission bits to apply to the file.
 *
 * @warning Some platforms may not be able to apply all of the
 * available permission bits; #APR_INCOMPLETE will be returned if some
 * permissions are specified which could not be set.
 *
 * @warning Platforms which do not implement this feature will return
 * #APR_ENOTIMPL.
 */
APR_DECLARE(apr_status_t) apr_file_perms_set(const char *fname,
                                             apr_fileperms_t perms);

/**
 * Set attributes of the specified file.
 * @param fname The full path to the file (using / on all systems)
 * @param attributes Or'd combination of
 *            @li #APR_FILE_ATTR_READONLY   - make the file readonly
 *            @li #APR_FILE_ATTR_EXECUTABLE - make the file executable
 *            @li #APR_FILE_ATTR_HIDDEN     - make the file hidden
 * @param attr_mask Mask of valid bits in attributes.
 * @param pool the pool to use.
 * @remark This function should be used in preference to explicit manipulation
 *      of the file permissions, because the operations to provide these
 *      attributes are platform specific and may involve more than simply
 *      setting permission bits.
 * @warning Platforms which do not implement this feature will return
 *      #APR_ENOTIMPL.
 */
APR_DECLARE(apr_status_t) apr_file_attrs_set(const char *fname,
                                             apr_fileattrs_t attributes,
                                             apr_fileattrs_t attr_mask,
                                             apr_pool_t *pool);

/**
 * Set the mtime of the specified file.
 * @param fname The full path to the file (using / on all systems)
 * @param mtime The mtime to apply to the file.
 * @param pool The pool to use.
 * @warning Platforms which do not implement this feature will return
 *      #APR_ENOTIMPL.
 */
APR_DECLARE(apr_status_t) apr_file_mtime_set(const char *fname,
                                             apr_time_t mtime,
                                             apr_pool_t *pool);

/**
 * Create a new directory on the file system.
 * @param path the path for the directory to be created. (use / on all systems)
 * @param perm Permissions for the new directory.
 * @param pool the pool to use.
 */                        
APR_DECLARE(apr_status_t) apr_dir_make(const char *path, apr_fileperms_t perm, 
                                       apr_pool_t *pool);

/** Creates a new directory on the file system, but behaves like
 * 'mkdir -p'. Creates intermediate directories as required. No error
 * will be reported if PATH already exists.
 * @param path the path for the directory to be created. (use / on all systems)
 * @param perm Permissions for the new directory.
 * @param pool the pool to use.
 */
APR_DECLARE(apr_status_t) apr_dir_make_recursive(const char *path,
                                                 apr_fileperms_t perm,
                                                 apr_pool_t *pool);

/**
 * Remove directory from the file system.
 * @param path the path for the directory to be removed. (use / on all systems)
 * @param pool the pool to use.
 * @remark Removing a directory which is in-use (e.g., the current working
 * directory, or during apr_dir_read, or with an open file) is not portable.
 */                        
APR_DECLARE(apr_status_t) apr_dir_remove(const char *path, apr_pool_t *pool);

/**
 * get the specified file's stats.
 * @param finfo Where to store the information about the file.
 * @param wanted The desired apr_finfo_t fields, as a bit flag of APR_FINFO_* values 
 * @param thefile The file to get information about.
 */ 
APR_DECLARE(apr_status_t) apr_file_info_get(apr_finfo_t *finfo, 
                                            apr_int32_t wanted,
                                            apr_file_t *thefile);
    

/**
 * Truncate the file's length to the specified offset
 * @param fp The file to truncate
 * @param offset The offset to truncate to.
 * @remark The read/write file offset is repositioned to offset.
 */
APR_DECLARE(apr_status_t) apr_file_trunc(apr_file_t *fp, apr_off_t offset);

/**
 * Retrieve the flags that were passed into apr_file_open()
 * when the file was opened.
 * @return apr_int32_t the flags
 */
APR_DECLARE(apr_int32_t) apr_file_flags_get(apr_file_t *f);

/**
 * Get the pool used by the file.
 */
APR_POOL_DECLARE_ACCESSOR(file);

/**
 * Set a file to be inherited by child processes.
 *
 */
APR_DECLARE_INHERIT_SET(file);

/**
 * Unset a file from being inherited by child processes.
 */
APR_DECLARE_INHERIT_UNSET(file);

/**
 * Open a temporary file
 * @param fp The apr file to use as a temporary file.
 * @param templ The template to use when creating a temp file.
 * @param flags The flags to open the file with. If this is zero,
 *              the file is opened with 
 *              #APR_FOPEN_CREATE | #APR_FOPEN_READ | #APR_FOPEN_WRITE |
 *              #APR_FOPEN_EXCL | #APR_FOPEN_DELONCLOSE
 * @param p The pool to allocate the file out of.
 * @remark   
 * This function  generates  a unique temporary file name from template.  
 * The last six characters of template must be XXXXXX and these are replaced 
 * with a string that makes the filename unique. Since it will  be  modified,
 * template must not be a string constant, but should be declared as a character
 * array.  
 *
 */
APR_DECLARE(apr_status_t) apr_file_mktemp(apr_file_t **fp, char *templ,
                                          apr_int32_t flags, apr_pool_t *p);


/**
 * Find an existing directory suitable as a temporary storage location.
 * @param temp_dir The temp directory.
 * @param p The pool to use for any necessary allocations.
 * @remark   
 * This function uses an algorithm to search for a directory that an
 * an application can use for temporary storage.
 *
 */
APR_DECLARE(apr_status_t) apr_temp_dir_get(const char **temp_dir, 
                                           apr_pool_t *p);

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* ! APR_FILE_IO_H */
