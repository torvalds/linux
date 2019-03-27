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

#ifndef APR_FILE_INFO_H
#define APR_FILE_INFO_H

/**
 * @file apr_file_info.h
 * @brief APR File Information
 */

#include "apr.h"
#include "apr_user.h"
#include "apr_pools.h"
#include "apr_tables.h"
#include "apr_time.h"
#include "apr_errno.h"

#if APR_HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_file_info File Information
 * @ingroup APR 
 * @{
 */

/* Many applications use the type member to determine the
 * existance of a file or initialization of the file info,
 * so the APR_NOFILE value must be distinct from APR_UNKFILE.
 */

/** apr_filetype_e values for the filetype member of the 
 * apr_file_info_t structure
 * @warning: Not all of the filetypes below can be determined.
 * For example, a given platform might not correctly report 
 * a socket descriptor as APR_SOCK if that type isn't 
 * well-identified on that platform.  In such cases where
 * a filetype exists but cannot be described by the recognized
 * flags below, the filetype will be APR_UNKFILE.  If the
 * filetype member is not determined, the type will be APR_NOFILE.
 */

typedef enum {
    APR_NOFILE = 0,     /**< no file type determined */
    APR_REG,            /**< a regular file */
    APR_DIR,            /**< a directory */
    APR_CHR,            /**< a character device */
    APR_BLK,            /**< a block device */
    APR_PIPE,           /**< a FIFO / pipe */
    APR_LNK,            /**< a symbolic link */
    APR_SOCK,           /**< a [unix domain] socket */
    APR_UNKFILE = 127   /**< a file of some other unknown type */
} apr_filetype_e; 

/**
 * @defgroup apr_file_permissions File Permissions flags 
 * @{
 */

#define APR_FPROT_USETID      0x8000 /**< Set user id */
#define APR_FPROT_UREAD       0x0400 /**< Read by user */
#define APR_FPROT_UWRITE      0x0200 /**< Write by user */
#define APR_FPROT_UEXECUTE    0x0100 /**< Execute by user */

#define APR_FPROT_GSETID      0x4000 /**< Set group id */
#define APR_FPROT_GREAD       0x0040 /**< Read by group */
#define APR_FPROT_GWRITE      0x0020 /**< Write by group */
#define APR_FPROT_GEXECUTE    0x0010 /**< Execute by group */

#define APR_FPROT_WSTICKY     0x2000 /**< Sticky bit */
#define APR_FPROT_WREAD       0x0004 /**< Read by others */
#define APR_FPROT_WWRITE      0x0002 /**< Write by others */
#define APR_FPROT_WEXECUTE    0x0001 /**< Execute by others */

#define APR_FPROT_OS_DEFAULT  0x0FFF /**< use OS's default permissions */

/* additional permission flags for apr_file_copy  and apr_file_append */
#define APR_FPROT_FILE_SOURCE_PERMS 0x1000 /**< Copy source file's permissions */
    
/* backcompat */
#define APR_USETID     APR_FPROT_USETID     /**< @deprecated @see APR_FPROT_USETID     */
#define APR_UREAD      APR_FPROT_UREAD      /**< @deprecated @see APR_FPROT_UREAD      */
#define APR_UWRITE     APR_FPROT_UWRITE     /**< @deprecated @see APR_FPROT_UWRITE     */
#define APR_UEXECUTE   APR_FPROT_UEXECUTE   /**< @deprecated @see APR_FPROT_UEXECUTE   */
#define APR_GSETID     APR_FPROT_GSETID     /**< @deprecated @see APR_FPROT_GSETID     */
#define APR_GREAD      APR_FPROT_GREAD      /**< @deprecated @see APR_FPROT_GREAD      */
#define APR_GWRITE     APR_FPROT_GWRITE     /**< @deprecated @see APR_FPROT_GWRITE     */
#define APR_GEXECUTE   APR_FPROT_GEXECUTE   /**< @deprecated @see APR_FPROT_GEXECUTE   */
#define APR_WSTICKY    APR_FPROT_WSTICKY    /**< @deprecated @see APR_FPROT_WSTICKY    */
#define APR_WREAD      APR_FPROT_WREAD      /**< @deprecated @see APR_FPROT_WREAD      */
#define APR_WWRITE     APR_FPROT_WWRITE     /**< @deprecated @see APR_FPROT_WWRITE     */
#define APR_WEXECUTE   APR_FPROT_WEXECUTE   /**< @deprecated @see APR_FPROT_WEXECUTE   */
#define APR_OS_DEFAULT APR_FPROT_OS_DEFAULT /**< @deprecated @see APR_FPROT_OS_DEFAULT */
#define APR_FILE_SOURCE_PERMS APR_FPROT_FILE_SOURCE_PERMS /**< @deprecated @see APR_FPROT_FILE_SOURCE_PERMS */
    
/** @} */


/**
 * Structure for referencing directories.
 */
typedef struct apr_dir_t          apr_dir_t;
/**
 * Structure for determining file permissions.
 */
typedef apr_int32_t               apr_fileperms_t;
#if (defined WIN32) || (defined NETWARE)
/**
 * Structure for determining the device the file is on.
 */
typedef apr_uint32_t              apr_dev_t;
#else
/**
 * Structure for determining the device the file is on.
 */
typedef dev_t                     apr_dev_t;
#endif

/**
 * @defgroup apr_file_stat Stat Functions
 * @{
 */
/** file info structure */
typedef struct apr_finfo_t        apr_finfo_t;

#define APR_FINFO_LINK   0x00000001 /**< Stat the link not the file itself if it is a link */
#define APR_FINFO_MTIME  0x00000010 /**< Modification Time */
#define APR_FINFO_CTIME  0x00000020 /**< Creation or inode-changed time */
#define APR_FINFO_ATIME  0x00000040 /**< Access Time */
#define APR_FINFO_SIZE   0x00000100 /**< Size of the file */
#define APR_FINFO_CSIZE  0x00000200 /**< Storage size consumed by the file */
#define APR_FINFO_DEV    0x00001000 /**< Device */
#define APR_FINFO_INODE  0x00002000 /**< Inode */
#define APR_FINFO_NLINK  0x00004000 /**< Number of links */
#define APR_FINFO_TYPE   0x00008000 /**< Type */
#define APR_FINFO_USER   0x00010000 /**< User */
#define APR_FINFO_GROUP  0x00020000 /**< Group */
#define APR_FINFO_UPROT  0x00100000 /**< User protection bits */
#define APR_FINFO_GPROT  0x00200000 /**< Group protection bits */
#define APR_FINFO_WPROT  0x00400000 /**< World protection bits */
#define APR_FINFO_ICASE  0x01000000 /**< if dev is case insensitive */
#define APR_FINFO_NAME   0x02000000 /**< ->name in proper case */

#define APR_FINFO_MIN    0x00008170 /**< type, mtime, ctime, atime, size */
#define APR_FINFO_IDENT  0x00003000 /**< dev and inode */
#define APR_FINFO_OWNER  0x00030000 /**< user and group */
#define APR_FINFO_PROT   0x00700000 /**<  all protections */
#define APR_FINFO_NORM   0x0073b170 /**<  an atomic unix apr_stat() */
#define APR_FINFO_DIRENT 0x02000000 /**<  an atomic unix apr_dir_read() */

/**
 * The file information structure.  This is analogous to the POSIX
 * stat structure.
 */
struct apr_finfo_t {
    /** Allocates memory and closes lingering handles in the specified pool */
    apr_pool_t *pool;
    /** The bitmask describing valid fields of this apr_finfo_t structure 
     *  including all available 'wanted' fields and potentially more */
    apr_int32_t valid;
    /** The access permissions of the file.  Mimics Unix access rights. */
    apr_fileperms_t protection;
    /** The type of file.  One of APR_REG, APR_DIR, APR_CHR, APR_BLK, APR_PIPE, 
     * APR_LNK or APR_SOCK.  If the type is undetermined, the value is APR_NOFILE.
     * If the type cannot be determined, the value is APR_UNKFILE.
     */
    apr_filetype_e filetype;
    /** The user id that owns the file */
    apr_uid_t user;
    /** The group id that owns the file */
    apr_gid_t group;
    /** The inode of the file. */
    apr_ino_t inode;
    /** The id of the device the file is on. */
    apr_dev_t device;
    /** The number of hard links to the file. */
    apr_int32_t nlink;
    /** The size of the file */
    apr_off_t size;
    /** The storage size consumed by the file */
    apr_off_t csize;
    /** The time the file was last accessed */
    apr_time_t atime;
    /** The time the file was last modified */
    apr_time_t mtime;
    /** The time the file was created, or the inode was last changed */
    apr_time_t ctime;
    /** The pathname of the file (possibly unrooted) */
    const char *fname;
    /** The file's name (no path) in filesystem case */
    const char *name;
    /** Unused */
    struct apr_file_t *filehand;
};

/**
 * get the specified file's stats.  The file is specified by filename, 
 * instead of using a pre-opened file.
 * @param finfo Where to store the information about the file, which is
 * never touched if the call fails.
 * @param fname The name of the file to stat.
 * @param wanted The desired apr_finfo_t fields, as a bit flag of APR_FINFO_
                 values 
 * @param pool the pool to use to allocate the new file. 
 *
 * @note If @c APR_INCOMPLETE is returned all the fields in @a finfo may
 *       not be filled in, and you need to check the @c finfo->valid bitmask
 *       to verify that what you're looking for is there.
 */ 
APR_DECLARE(apr_status_t) apr_stat(apr_finfo_t *finfo, const char *fname,
                                   apr_int32_t wanted, apr_pool_t *pool);

/** @} */
/**
 * @defgroup apr_dir Directory Manipulation Functions
 * @{
 */

/**
 * Open the specified directory.
 * @param new_dir The opened directory descriptor.
 * @param dirname The full path to the directory (use / on all systems)
 * @param pool The pool to use.
 */                        
APR_DECLARE(apr_status_t) apr_dir_open(apr_dir_t **new_dir, 
                                       const char *dirname, 
                                       apr_pool_t *pool);

/**
 * close the specified directory. 
 * @param thedir the directory descriptor to close.
 */                        
APR_DECLARE(apr_status_t) apr_dir_close(apr_dir_t *thedir);

/**
 * Read the next entry from the specified directory. 
 * @param finfo the file info structure and filled in by apr_dir_read
 * @param wanted The desired apr_finfo_t fields, as a bit flag of APR_FINFO_
                 values 
 * @param thedir the directory descriptor returned from apr_dir_open
 * @remark No ordering is guaranteed for the entries read.
 *
 * @note If @c APR_INCOMPLETE is returned all the fields in @a finfo may
 *       not be filled in, and you need to check the @c finfo->valid bitmask
 *       to verify that what you're looking for is there. When no more
 *       entries are available, APR_ENOENT is returned.
 */                        
APR_DECLARE(apr_status_t) apr_dir_read(apr_finfo_t *finfo, apr_int32_t wanted,
                                       apr_dir_t *thedir);

/**
 * Rewind the directory to the first entry.
 * @param thedir the directory descriptor to rewind.
 */                        
APR_DECLARE(apr_status_t) apr_dir_rewind(apr_dir_t *thedir);
/** @} */

/**
 * @defgroup apr_filepath Filepath Manipulation Functions
 * @{
 */

/** Cause apr_filepath_merge to fail if addpath is above rootpath 
 * @bug in APR 0.9 and 1.x, this flag's behavior is undefined
 * if the rootpath is NULL or empty.  In APR 2.0 this should be
 * changed to imply NOTABSOLUTE if the rootpath is NULL or empty.
 */
#define APR_FILEPATH_NOTABOVEROOT   0x01

/** internal: Only meaningful with APR_FILEPATH_NOTABOVEROOT */
#define APR_FILEPATH_SECUREROOTTEST 0x02

/** Cause apr_filepath_merge to fail if addpath is above rootpath,
 * even given a rootpath /foo/bar and an addpath ../bar/bash
 */
#define APR_FILEPATH_SECUREROOT     0x03

/** Fail apr_filepath_merge if the merged path is relative */
#define APR_FILEPATH_NOTRELATIVE    0x04

/** Fail apr_filepath_merge if the merged path is absolute */
#define APR_FILEPATH_NOTABSOLUTE    0x08

/** Return the file system's native path format (e.g. path delimiters
 * of ':' on MacOS9, '\' on Win32, etc.) */
#define APR_FILEPATH_NATIVE         0x10

/** Resolve the true case of existing directories and file elements
 * of addpath, (resolving any aliases on Win32) and append a proper 
 * trailing slash if a directory
 */
#define APR_FILEPATH_TRUENAME       0x20

/**
 * Extract the rootpath from the given filepath
 * @param rootpath the root file path returned with APR_SUCCESS or APR_EINCOMPLETE
 * @param filepath the pathname to parse for its root component
 * @param flags the desired rules to apply, from
 * <PRE>
 *      APR_FILEPATH_NATIVE    Use native path separators (e.g. '\' on Win32)
 *      APR_FILEPATH_TRUENAME  Tests that the root exists, and makes it proper
 * </PRE>
 * @param p the pool to allocate the new path string from
 * @remark on return, filepath points to the first non-root character in the
 * given filepath.  In the simplest example, given a filepath of "/foo", 
 * returns the rootpath of "/" and filepath points at "foo".  This is far 
 * more complex on other platforms, which will canonicalize the root form
 * to a consistant format, given the APR_FILEPATH_TRUENAME flag, and also
 * test for the validity of that root (e.g., that a drive d:/ or network 
 * share //machine/foovol/). 
 * The function returns APR_ERELATIVE if filepath isn't rooted (an
 * error), APR_EINCOMPLETE if the root path is ambiguous (but potentially
 * legitimate, e.g. "/" on Windows is incomplete because it doesn't specify
 * the drive letter), or APR_EBADPATH if the root is simply invalid.
 * APR_SUCCESS is returned if filepath is an absolute path.
 */
APR_DECLARE(apr_status_t) apr_filepath_root(const char **rootpath, 
                                            const char **filepath, 
                                            apr_int32_t flags,
                                            apr_pool_t *p);

/**
 * Merge additional file path onto the previously processed rootpath
 * @param newpath the merged paths returned
 * @param rootpath the root file path (NULL uses the current working path)
 * @param addpath the path to add to the root path
 * @param flags the desired APR_FILEPATH_ rules to apply when merging
 * @param p the pool to allocate the new path string from
 * @remark if the flag APR_FILEPATH_TRUENAME is given, and the addpath 
 * contains wildcard characters ('*', '?') on platforms that don't support 
 * such characters within filenames, the paths will be merged, but the 
 * result code will be APR_EPATHWILD, and all further segments will not
 * reflect the true filenames including the wildcard and following segments.
 */                        
APR_DECLARE(apr_status_t) apr_filepath_merge(char **newpath, 
                                             const char *rootpath,
                                             const char *addpath, 
                                             apr_int32_t flags,
                                             apr_pool_t *p);

/**
 * Split a search path into separate components
 * @param pathelts the returned components of the search path
 * @param liststr the search path (e.g., <tt>getenv("PATH")</tt>)
 * @param p the pool to allocate the array and path components from
 * @remark empty path components do not become part of @a pathelts.
 * @remark the path separator in @a liststr is system specific;
 * e.g., ':' on Unix, ';' on Windows, etc.
 */
APR_DECLARE(apr_status_t) apr_filepath_list_split(apr_array_header_t **pathelts,
                                                  const char *liststr,
                                                  apr_pool_t *p);

/**
 * Merge a list of search path components into a single search path
 * @param liststr the returned search path; may be NULL if @a pathelts is empty
 * @param pathelts the components of the search path
 * @param p the pool to allocate the search path from
 * @remark emtpy strings in the source array are ignored.
 * @remark the path separator in @a liststr is system specific;
 * e.g., ':' on Unix, ';' on Windows, etc.
 */
APR_DECLARE(apr_status_t) apr_filepath_list_merge(char **liststr,
                                                  apr_array_header_t *pathelts,
                                                  apr_pool_t *p);

/**
 * Return the default file path (for relative file names)
 * @param path the default path string returned
 * @param flags optional flag APR_FILEPATH_NATIVE to retrieve the
 *              default file path in os-native format.
 * @param p the pool to allocate the default path string from
 */
APR_DECLARE(apr_status_t) apr_filepath_get(char **path, apr_int32_t flags,
                                           apr_pool_t *p);

/**
 * Set the default file path (for relative file names)
 * @param path the default path returned
 * @param p the pool to allocate any working storage
 */
APR_DECLARE(apr_status_t) apr_filepath_set(const char *path, apr_pool_t *p);

/** The FilePath character encoding is unknown */
#define APR_FILEPATH_ENCODING_UNKNOWN  0

/** The FilePath character encoding is locale-dependent */
#define APR_FILEPATH_ENCODING_LOCALE   1

/** The FilePath character encoding is UTF-8 */
#define APR_FILEPATH_ENCODING_UTF8     2

/**
 * Determine the encoding used internally by the FilePath functions
 * @param style points to a variable which receives the encoding style flag
 * @param p the pool to allocate any working storage
 * @remark Use @c apr_os_locale_encoding and/or @c apr_os_default_encoding
 * to get the name of the path encoding if it's not UTF-8.
 */
APR_DECLARE(apr_status_t) apr_filepath_encoding(int *style, apr_pool_t *p);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* ! APR_FILE_INFO_H */
