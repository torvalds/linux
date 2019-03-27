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
 * @file svn_error_codes.h
 * @brief Subversion error codes.
 */

/* What's going on here?

   In order to define error codes and their associated description
   strings in the same place, we overload the SVN_ERRDEF() macro with
   two definitions below.  Both take two arguments, an error code name
   and a description string.  One definition of the macro just throws
   away the string and defines enumeration constants using the error
   code names -- that definition is used by the header file that
   exports error codes to the rest of Subversion.  The other
   definition creates a static table mapping the enum codes to their
   corresponding strings -- that definition is used by the C file that
   implements svn_strerror().

   The header and C files both include this file, using #defines to
   control which version of the macro they get.
*/


/* Process this file if we're building an error array, or if we have
   not defined the enumerated constants yet.  */
#if defined(SVN_ERROR_BUILD_ARRAY) || !defined(SVN_ERROR_ENUM_DEFINED)

/* Note: despite lacking double underscores in its name, the macro
   SVN_ERROR_BUILD_ARRAY is an implementation detail of Subversion and not
   a public API. */


#include <apr_errno.h>     /* APR's error system */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#if defined(SVN_ERROR_BUILD_ARRAY)

#define SVN_ERROR_START \
        static const err_defn error_table[] = { \
          { SVN_WARNING, "SVN_WARNING", "Warning" },
#define SVN_ERRDEF(num, offset, str) { num, #num, str },
#define SVN_ERROR_END { 0, NULL, NULL } };

#elif !defined(SVN_ERROR_ENUM_DEFINED)

#define SVN_ERROR_START \
        typedef enum svn_errno_t { \
          SVN_WARNING = APR_OS_START_USERERR + 1,
#define SVN_ERRDEF(num, offset, str) /** str */ num = offset,
#define SVN_ERROR_END SVN_ERR_LAST } svn_errno_t;

#define SVN_ERROR_ENUM_DEFINED

#endif

/* Define custom Subversion error numbers, in the range reserved for
   that in APR: from APR_OS_START_USERERR to APR_OS_START_SYSERR (see
   apr_errno.h).

   Error numbers are divided into categories of up to 5000 errors
   each.  Since we're dividing up the APR user error space, which has
   room for 500,000 errors, we can have up to 100 categories.
   Categories are fixed-size; if a category has fewer than 5000
   errors, then it just ends with a range of unused numbers.

   To maintain binary compatibility, please observe these guidelines:

      - When adding a new error, always add on the end of the
        appropriate category, so that the real values of existing
        errors are not changed.

      - When deleting an error, leave a placeholder comment indicating
        the offset, again so that the values of other errors are not
        perturbed.
*/

#define SVN_ERR_CATEGORY_SIZE 5000

/* Leave one category of room at the beginning, for SVN_WARNING and
   any other such beasts we might create in the future. */
#define SVN_ERR_BAD_CATEGORY_START      (APR_OS_START_USERERR \
                                         + ( 1 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_XML_CATEGORY_START      (APR_OS_START_USERERR \
                                         + ( 2 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_IO_CATEGORY_START       (APR_OS_START_USERERR \
                                         + ( 3 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_STREAM_CATEGORY_START   (APR_OS_START_USERERR \
                                         + ( 4 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_NODE_CATEGORY_START     (APR_OS_START_USERERR \
                                         + ( 5 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_ENTRY_CATEGORY_START    (APR_OS_START_USERERR \
                                         + ( 6 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_WC_CATEGORY_START       (APR_OS_START_USERERR \
                                         + ( 7 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_FS_CATEGORY_START       (APR_OS_START_USERERR \
                                         + ( 8 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_REPOS_CATEGORY_START    (APR_OS_START_USERERR \
                                         + ( 9 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_RA_CATEGORY_START       (APR_OS_START_USERERR \
                                         + (10 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_RA_DAV_CATEGORY_START   (APR_OS_START_USERERR \
                                         + (11 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_RA_LOCAL_CATEGORY_START (APR_OS_START_USERERR \
                                         + (12 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_SVNDIFF_CATEGORY_START  (APR_OS_START_USERERR \
                                         + (13 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_APMOD_CATEGORY_START    (APR_OS_START_USERERR \
                                         + (14 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_CLIENT_CATEGORY_START   (APR_OS_START_USERERR \
                                         + (15 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_MISC_CATEGORY_START     (APR_OS_START_USERERR \
                                         + (16 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_CL_CATEGORY_START       (APR_OS_START_USERERR \
                                         + (17 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_RA_SVN_CATEGORY_START   (APR_OS_START_USERERR \
                                         + (18 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_AUTHN_CATEGORY_START    (APR_OS_START_USERERR \
                                         + (19 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_AUTHZ_CATEGORY_START    (APR_OS_START_USERERR \
                                         + (20 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_DIFF_CATEGORY_START     (APR_OS_START_USERERR \
                                         + (21 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_RA_SERF_CATEGORY_START  (APR_OS_START_USERERR \
                                         + (22 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_MALFUNC_CATEGORY_START  (APR_OS_START_USERERR \
                                         + (23 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_X509_CATEGORY_START     (APR_OS_START_USERERR \
                                         + (24 * SVN_ERR_CATEGORY_SIZE))

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/** Collection of Subversion error code values, located within the
 * APR user error space. */
SVN_ERROR_START

  /* validation ("BAD_FOO") errors */

  SVN_ERRDEF(SVN_ERR_BAD_CONTAINING_POOL,
             SVN_ERR_BAD_CATEGORY_START + 0,
             "Bad parent pool passed to svn_make_pool()")

  SVN_ERRDEF(SVN_ERR_BAD_FILENAME,
             SVN_ERR_BAD_CATEGORY_START + 1,
             "Bogus filename")

  SVN_ERRDEF(SVN_ERR_BAD_URL,
             SVN_ERR_BAD_CATEGORY_START + 2,
             "Bogus URL")

  SVN_ERRDEF(SVN_ERR_BAD_DATE,
             SVN_ERR_BAD_CATEGORY_START + 3,
             "Bogus date")

  SVN_ERRDEF(SVN_ERR_BAD_MIME_TYPE,
             SVN_ERR_BAD_CATEGORY_START + 4,
             "Bogus mime-type")

  /** @since New in 1.5.
   *
   * Note that there was an unused slot sitting here at
   * SVN_ERR_BAD_CATEGORY_START + 5, so error codes after this aren't
   * necessarily "New in 1.5" just because they come later.
   */
  SVN_ERRDEF(SVN_ERR_BAD_PROPERTY_VALUE,
             SVN_ERR_BAD_CATEGORY_START + 5,
             "Wrong or unexpected property value")

  SVN_ERRDEF(SVN_ERR_BAD_VERSION_FILE_FORMAT,
             SVN_ERR_BAD_CATEGORY_START + 6,
             "Version file format not correct")

  SVN_ERRDEF(SVN_ERR_BAD_RELATIVE_PATH,
             SVN_ERR_BAD_CATEGORY_START + 7,
             "Path is not an immediate child of the specified directory")

  SVN_ERRDEF(SVN_ERR_BAD_UUID,
             SVN_ERR_BAD_CATEGORY_START + 8,
             "Bogus UUID")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_BAD_CONFIG_VALUE,
             SVN_ERR_BAD_CATEGORY_START + 9,
             "Invalid configuration value")

  SVN_ERRDEF(SVN_ERR_BAD_SERVER_SPECIFICATION,
             SVN_ERR_BAD_CATEGORY_START + 10,
             "Bogus server specification")

  SVN_ERRDEF(SVN_ERR_BAD_CHECKSUM_KIND,
             SVN_ERR_BAD_CATEGORY_START + 11,
             "Unsupported checksum type")

  SVN_ERRDEF(SVN_ERR_BAD_CHECKSUM_PARSE,
             SVN_ERR_BAD_CATEGORY_START + 12,
             "Invalid character in hex checksum")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_BAD_TOKEN,
             SVN_ERR_BAD_CATEGORY_START + 13,
             "Unknown string value of token")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_BAD_CHANGELIST_NAME,
             SVN_ERR_BAD_CATEGORY_START + 14,
             "Invalid changelist name")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_BAD_ATOMIC,
             SVN_ERR_BAD_CATEGORY_START + 15,
             "Invalid atomic")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_BAD_COMPRESSION_METHOD,
             SVN_ERR_BAD_CATEGORY_START + 16,
             "Invalid compression method")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_BAD_PROPERTY_VALUE_EOL,
             SVN_ERR_BAD_CATEGORY_START + 17,
             "Unexpected line ending in the property value")

  /* xml errors */

  SVN_ERRDEF(SVN_ERR_XML_ATTRIB_NOT_FOUND,
             SVN_ERR_XML_CATEGORY_START + 0,
             "No such XML tag attribute")

  SVN_ERRDEF(SVN_ERR_XML_MISSING_ANCESTRY,
             SVN_ERR_XML_CATEGORY_START + 1,
             "<delta-pkg> is missing ancestry")

  SVN_ERRDEF(SVN_ERR_XML_UNKNOWN_ENCODING,
             SVN_ERR_XML_CATEGORY_START + 2,
             "Unrecognized binary data encoding; can't decode")

  SVN_ERRDEF(SVN_ERR_XML_MALFORMED,
             SVN_ERR_XML_CATEGORY_START + 3,
             "XML data was not well-formed")

  SVN_ERRDEF(SVN_ERR_XML_UNESCAPABLE_DATA,
             SVN_ERR_XML_CATEGORY_START + 4,
             "Data cannot be safely XML-escaped")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_XML_UNEXPECTED_ELEMENT,
             SVN_ERR_XML_CATEGORY_START + 5,
             "Unexpected XML element found")

  /* io errors */

  SVN_ERRDEF(SVN_ERR_IO_INCONSISTENT_EOL,
             SVN_ERR_IO_CATEGORY_START + 0,
             "Inconsistent line ending style")

  SVN_ERRDEF(SVN_ERR_IO_UNKNOWN_EOL,
             SVN_ERR_IO_CATEGORY_START + 1,
             "Unrecognized line ending style")

  /** @deprecated Unused, slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_IO_CORRUPT_EOL,
             SVN_ERR_IO_CATEGORY_START + 2,
             "Line endings other than expected")

  SVN_ERRDEF(SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
             SVN_ERR_IO_CATEGORY_START + 3,
             "Ran out of unique names")

  /** @deprecated Unused, slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_IO_PIPE_FRAME_ERROR,
             SVN_ERR_IO_CATEGORY_START + 4,
             "Framing error in pipe protocol")

  /** @deprecated Unused, slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_IO_PIPE_READ_ERROR,
             SVN_ERR_IO_CATEGORY_START + 5,
             "Read error in pipe")

  SVN_ERRDEF(SVN_ERR_IO_WRITE_ERROR,
             SVN_ERR_IO_CATEGORY_START + 6,
             "Write error")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_IO_PIPE_WRITE_ERROR,
             SVN_ERR_IO_CATEGORY_START + 7,
             "Write error in pipe")

  /* stream errors */

  SVN_ERRDEF(SVN_ERR_STREAM_UNEXPECTED_EOF,
             SVN_ERR_STREAM_CATEGORY_START + 0,
             "Unexpected EOF on stream")

  SVN_ERRDEF(SVN_ERR_STREAM_MALFORMED_DATA,
             SVN_ERR_STREAM_CATEGORY_START + 1,
             "Malformed stream data")

  SVN_ERRDEF(SVN_ERR_STREAM_UNRECOGNIZED_DATA,
             SVN_ERR_STREAM_CATEGORY_START + 2,
             "Unrecognized stream data")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_STREAM_SEEK_NOT_SUPPORTED,
             SVN_ERR_STREAM_CATEGORY_START + 3,
             "Stream doesn't support seeking")

  /** Since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_STREAM_NOT_SUPPORTED,
             SVN_ERR_STREAM_CATEGORY_START + 4,
             "Stream doesn't support this capability")

  /* node errors */

  SVN_ERRDEF(SVN_ERR_NODE_UNKNOWN_KIND,
             SVN_ERR_NODE_CATEGORY_START + 0,
             "Unknown svn_node_kind")

  SVN_ERRDEF(SVN_ERR_NODE_UNEXPECTED_KIND,
             SVN_ERR_NODE_CATEGORY_START + 1,
             "Unexpected node kind found")

  /* entry errors */

  SVN_ERRDEF(SVN_ERR_ENTRY_NOT_FOUND,
             SVN_ERR_ENTRY_CATEGORY_START + 0,
             "Can't find an entry")

  /* UNUSED error slot:                    + 1 */

  SVN_ERRDEF(SVN_ERR_ENTRY_EXISTS,
             SVN_ERR_ENTRY_CATEGORY_START + 2,
             "Entry already exists")

  SVN_ERRDEF(SVN_ERR_ENTRY_MISSING_REVISION,
             SVN_ERR_ENTRY_CATEGORY_START + 3,
             "Entry has no revision")

  SVN_ERRDEF(SVN_ERR_ENTRY_MISSING_URL,
             SVN_ERR_ENTRY_CATEGORY_START + 4,
             "Entry has no URL")

  SVN_ERRDEF(SVN_ERR_ENTRY_ATTRIBUTE_INVALID,
             SVN_ERR_ENTRY_CATEGORY_START + 5,
             "Entry has an invalid attribute")

  SVN_ERRDEF(SVN_ERR_ENTRY_FORBIDDEN,
             SVN_ERR_ENTRY_CATEGORY_START + 6,
             "Can't create an entry for a forbidden name")

  /* wc errors */

  SVN_ERRDEF(SVN_ERR_WC_OBSTRUCTED_UPDATE,
             SVN_ERR_WC_CATEGORY_START + 0,
             "Obstructed update")

  /** @deprecated Unused, slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_WC_UNWIND_MISMATCH,
             SVN_ERR_WC_CATEGORY_START + 1,
             "Mismatch popping the WC unwind stack")

  /** @deprecated Unused, slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_WC_UNWIND_EMPTY,
             SVN_ERR_WC_CATEGORY_START + 2,
             "Attempt to pop empty WC unwind stack")

  /** @deprecated Unused, slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_WC_UNWIND_NOT_EMPTY,
             SVN_ERR_WC_CATEGORY_START + 3,
             "Attempt to unlock with non-empty unwind stack")

  SVN_ERRDEF(SVN_ERR_WC_LOCKED,
             SVN_ERR_WC_CATEGORY_START + 4,
             "Attempted to lock an already-locked dir")

  SVN_ERRDEF(SVN_ERR_WC_NOT_LOCKED,
             SVN_ERR_WC_CATEGORY_START + 5,
             "Working copy not locked; this is probably a bug, please report")

  /** @deprecated Unused, slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_WC_INVALID_LOCK,
             SVN_ERR_WC_CATEGORY_START + 6,
             "Invalid lock")

  /** @since New in 1.7. Previously this error number was used by
   * #SVN_ERR_WC_NOT_DIRECTORY, which is now an alias for this error. */
  SVN_ERRDEF(SVN_ERR_WC_NOT_WORKING_COPY,
             SVN_ERR_WC_CATEGORY_START + 7,
             "Path is not a working copy directory")

  /** @deprecated Provided for backward compatibility with the 1.6 API.
   * Use #SVN_ERR_WC_NOT_WORKING_COPY. */
  SVN_ERRDEF(SVN_ERR_WC_NOT_DIRECTORY,
             SVN_ERR_WC_NOT_WORKING_COPY,
             "Path is not a working copy directory")

  SVN_ERRDEF(SVN_ERR_WC_NOT_FILE,
             SVN_ERR_WC_CATEGORY_START + 8,
             "Path is not a working copy file")

  SVN_ERRDEF(SVN_ERR_WC_BAD_ADM_LOG,
             SVN_ERR_WC_CATEGORY_START + 9,
             "Problem running log")

  SVN_ERRDEF(SVN_ERR_WC_PATH_NOT_FOUND,
             SVN_ERR_WC_CATEGORY_START + 10,
             "Can't find a working copy path")

  SVN_ERRDEF(SVN_ERR_WC_NOT_UP_TO_DATE,
             SVN_ERR_WC_CATEGORY_START + 11,
             "Working copy is not up-to-date")

  SVN_ERRDEF(SVN_ERR_WC_LEFT_LOCAL_MOD,
             SVN_ERR_WC_CATEGORY_START + 12,
             "Left locally modified or unversioned files")

  SVN_ERRDEF(SVN_ERR_WC_SCHEDULE_CONFLICT,
             SVN_ERR_WC_CATEGORY_START + 13,
             "Unmergeable scheduling requested on an entry")

  SVN_ERRDEF(SVN_ERR_WC_PATH_FOUND,
             SVN_ERR_WC_CATEGORY_START + 14,
             "Found a working copy path")

  SVN_ERRDEF(SVN_ERR_WC_FOUND_CONFLICT,
             SVN_ERR_WC_CATEGORY_START + 15,
             "A conflict in the working copy obstructs the current operation")

  SVN_ERRDEF(SVN_ERR_WC_CORRUPT,
             SVN_ERR_WC_CATEGORY_START + 16,
             "Working copy is corrupt")

  SVN_ERRDEF(SVN_ERR_WC_CORRUPT_TEXT_BASE,
             SVN_ERR_WC_CATEGORY_START + 17,
             "Working copy text base is corrupt")

  SVN_ERRDEF(SVN_ERR_WC_NODE_KIND_CHANGE,
             SVN_ERR_WC_CATEGORY_START + 18,
             "Cannot change node kind")

  SVN_ERRDEF(SVN_ERR_WC_INVALID_OP_ON_CWD,
             SVN_ERR_WC_CATEGORY_START + 19,
             "Invalid operation on the current working directory")

  SVN_ERRDEF(SVN_ERR_WC_BAD_ADM_LOG_START,
             SVN_ERR_WC_CATEGORY_START + 20,
             "Problem on first log entry in a working copy")

  SVN_ERRDEF(SVN_ERR_WC_UNSUPPORTED_FORMAT,
             SVN_ERR_WC_CATEGORY_START + 21,
             "Unsupported working copy format")

  SVN_ERRDEF(SVN_ERR_WC_BAD_PATH,
             SVN_ERR_WC_CATEGORY_START + 22,
             "Path syntax not supported in this context")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_WC_INVALID_SCHEDULE,
             SVN_ERR_WC_CATEGORY_START + 23,
             "Invalid schedule")

  /** @since New in 1.3. */
  SVN_ERRDEF(SVN_ERR_WC_INVALID_RELOCATION,
             SVN_ERR_WC_CATEGORY_START + 24,
             "Invalid relocation")

  /** @since New in 1.3. */
  SVN_ERRDEF(SVN_ERR_WC_INVALID_SWITCH,
             SVN_ERR_WC_CATEGORY_START + 25,
             "Invalid switch")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_WC_MISMATCHED_CHANGELIST,
             SVN_ERR_WC_CATEGORY_START + 26,
             "Changelist doesn't match")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
             SVN_ERR_WC_CATEGORY_START + 27,
             "Conflict resolution failed")

  SVN_ERRDEF(SVN_ERR_WC_COPYFROM_PATH_NOT_FOUND,
             SVN_ERR_WC_CATEGORY_START + 28,
             "Failed to locate 'copyfrom' path in working copy")

  /** @since New in 1.5.
   * @deprecated Provided for backward compatibility with the 1.6 API.
   * This event is not an error, and is now reported
   * via the standard notification mechanism instead. */
  SVN_ERRDEF(SVN_ERR_WC_CHANGELIST_MOVE,
             SVN_ERR_WC_CATEGORY_START + 29,
             "Moving a path from one changelist to another")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_WC_CANNOT_DELETE_FILE_EXTERNAL,
             SVN_ERR_WC_CATEGORY_START + 30,
             "Cannot delete a file external")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_WC_CANNOT_MOVE_FILE_EXTERNAL,
             SVN_ERR_WC_CATEGORY_START + 31,
             "Cannot move a file external")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_WC_DB_ERROR,
             SVN_ERR_WC_CATEGORY_START + 32,
             "Something's amiss with the wc sqlite database")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_WC_MISSING,
             SVN_ERR_WC_CATEGORY_START + 33,
             "The working copy is missing")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_WC_NOT_SYMLINK,
             SVN_ERR_WC_CATEGORY_START + 34,
             "The specified node is not a symlink")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
             SVN_ERR_WC_CATEGORY_START + 35,
             "The specified path has an unexpected status")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_WC_UPGRADE_REQUIRED,
             SVN_ERR_WC_CATEGORY_START + 36,
             "The working copy needs to be upgraded")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_WC_CLEANUP_REQUIRED,
             SVN_ERR_WC_CATEGORY_START + 37,
             "Previous operation has not finished; "
             "run 'cleanup' if it was interrupted")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_WC_INVALID_OPERATION_DEPTH,
             SVN_ERR_WC_CATEGORY_START + 38,
             "The operation cannot be performed with the specified depth")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_WC_PATH_ACCESS_DENIED,
             SVN_ERR_WC_CATEGORY_START + 39,
             "Couldn't open a working copy file because access was denied")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_WC_MIXED_REVISIONS,
             SVN_ERR_WC_CATEGORY_START + 40,
             "Mixed-revision working copy was found but not expected")

  /** @since New in 1.8 */
  SVN_ERRDEF(SVN_ERR_WC_DUPLICATE_EXTERNALS_TARGET,
             SVN_ERR_WC_CATEGORY_START + 41,
             "Duplicate targets in svn:externals property")

  /* fs errors */

  SVN_ERRDEF(SVN_ERR_FS_GENERAL,
             SVN_ERR_FS_CATEGORY_START + 0,
             "General filesystem error")

  SVN_ERRDEF(SVN_ERR_FS_CLEANUP,
             SVN_ERR_FS_CATEGORY_START + 1,
             "Error closing filesystem")

  SVN_ERRDEF(SVN_ERR_FS_ALREADY_OPEN,
             SVN_ERR_FS_CATEGORY_START + 2,
             "Filesystem is already open")

  SVN_ERRDEF(SVN_ERR_FS_NOT_OPEN,
             SVN_ERR_FS_CATEGORY_START + 3,
             "Filesystem is not open")

  SVN_ERRDEF(SVN_ERR_FS_CORRUPT,
             SVN_ERR_FS_CATEGORY_START + 4,
             "Filesystem is corrupt")

  SVN_ERRDEF(SVN_ERR_FS_PATH_SYNTAX,
             SVN_ERR_FS_CATEGORY_START + 5,
             "Invalid filesystem path syntax")

  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_REVISION,
             SVN_ERR_FS_CATEGORY_START + 6,
             "Invalid filesystem revision number")

  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_TRANSACTION,
             SVN_ERR_FS_CATEGORY_START + 7,
             "Invalid filesystem transaction name")

  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_ENTRY,
             SVN_ERR_FS_CATEGORY_START + 8,
             "Filesystem directory has no such entry")

  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_REPRESENTATION,
             SVN_ERR_FS_CATEGORY_START + 9,
             "Filesystem has no such representation")

  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_STRING,
             SVN_ERR_FS_CATEGORY_START + 10,
             "Filesystem has no such string")

  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_COPY,
             SVN_ERR_FS_CATEGORY_START + 11,
             "Filesystem has no such copy")

  SVN_ERRDEF(SVN_ERR_FS_TRANSACTION_NOT_MUTABLE,
             SVN_ERR_FS_CATEGORY_START + 12,
             "The specified transaction is not mutable")

  SVN_ERRDEF(SVN_ERR_FS_NOT_FOUND,
             SVN_ERR_FS_CATEGORY_START + 13,
             "Filesystem has no item")

  SVN_ERRDEF(SVN_ERR_FS_ID_NOT_FOUND,
             SVN_ERR_FS_CATEGORY_START + 14,
             "Filesystem has no such node-rev-id")

  SVN_ERRDEF(SVN_ERR_FS_NOT_ID,
             SVN_ERR_FS_CATEGORY_START + 15,
             "String does not represent a node or node-rev-id")

  SVN_ERRDEF(SVN_ERR_FS_NOT_DIRECTORY,
             SVN_ERR_FS_CATEGORY_START + 16,
             "Name does not refer to a filesystem directory")

  SVN_ERRDEF(SVN_ERR_FS_NOT_FILE,
             SVN_ERR_FS_CATEGORY_START + 17,
             "Name does not refer to a filesystem file")

  SVN_ERRDEF(SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT,
             SVN_ERR_FS_CATEGORY_START + 18,
             "Name is not a single path component")

  SVN_ERRDEF(SVN_ERR_FS_NOT_MUTABLE,
             SVN_ERR_FS_CATEGORY_START + 19,
             "Attempt to change immutable filesystem node")

  SVN_ERRDEF(SVN_ERR_FS_ALREADY_EXISTS,
             SVN_ERR_FS_CATEGORY_START + 20,
             "Item already exists in filesystem")

  SVN_ERRDEF(SVN_ERR_FS_ROOT_DIR,
             SVN_ERR_FS_CATEGORY_START + 21,
             "Attempt to remove or recreate fs root dir")

  SVN_ERRDEF(SVN_ERR_FS_NOT_TXN_ROOT,
             SVN_ERR_FS_CATEGORY_START + 22,
             "Object is not a transaction root")

  SVN_ERRDEF(SVN_ERR_FS_NOT_REVISION_ROOT,
             SVN_ERR_FS_CATEGORY_START + 23,
             "Object is not a revision root")

  SVN_ERRDEF(SVN_ERR_FS_CONFLICT,
             SVN_ERR_FS_CATEGORY_START + 24,
             "Merge conflict during commit")

  SVN_ERRDEF(SVN_ERR_FS_REP_CHANGED,
             SVN_ERR_FS_CATEGORY_START + 25,
             "A representation vanished or changed between reads")

  SVN_ERRDEF(SVN_ERR_FS_REP_NOT_MUTABLE,
             SVN_ERR_FS_CATEGORY_START + 26,
             "Tried to change an immutable representation")

  SVN_ERRDEF(SVN_ERR_FS_MALFORMED_SKEL,
             SVN_ERR_FS_CATEGORY_START + 27,
             "Malformed skeleton data")

  SVN_ERRDEF(SVN_ERR_FS_TXN_OUT_OF_DATE,
             SVN_ERR_FS_CATEGORY_START + 28,
             "Transaction is out of date")

  SVN_ERRDEF(SVN_ERR_FS_BERKELEY_DB,
             SVN_ERR_FS_CATEGORY_START + 29,
             "Berkeley DB error")

  SVN_ERRDEF(SVN_ERR_FS_BERKELEY_DB_DEADLOCK,
             SVN_ERR_FS_CATEGORY_START + 30,
             "Berkeley DB deadlock error")

  SVN_ERRDEF(SVN_ERR_FS_TRANSACTION_DEAD,
             SVN_ERR_FS_CATEGORY_START + 31,
             "Transaction is dead")

  SVN_ERRDEF(SVN_ERR_FS_TRANSACTION_NOT_DEAD,
             SVN_ERR_FS_CATEGORY_START + 32,
             "Transaction is not dead")

  /** @since New in 1.1. */
  SVN_ERRDEF(SVN_ERR_FS_UNKNOWN_FS_TYPE,
             SVN_ERR_FS_CATEGORY_START + 33,
             "Unknown FS type")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_NO_USER,
             SVN_ERR_FS_CATEGORY_START + 34,
             "No user associated with filesystem")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_PATH_ALREADY_LOCKED,
             SVN_ERR_FS_CATEGORY_START + 35,
             "Path is already locked")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_PATH_NOT_LOCKED,
             SVN_ERR_FS_CATEGORY_START + 36,
             "Path is not locked")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_BAD_LOCK_TOKEN,
             SVN_ERR_FS_CATEGORY_START + 37,
             "Lock token is incorrect")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_NO_LOCK_TOKEN,
             SVN_ERR_FS_CATEGORY_START + 38,
             "No lock token provided")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_LOCK_OWNER_MISMATCH,
             SVN_ERR_FS_CATEGORY_START + 39,
             "Username does not match lock owner")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_LOCK,
             SVN_ERR_FS_CATEGORY_START + 40,
             "Filesystem has no such lock")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_LOCK_EXPIRED,
             SVN_ERR_FS_CATEGORY_START + 41,
             "Lock has expired")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_FS_OUT_OF_DATE,
             SVN_ERR_FS_CATEGORY_START + 42,
             "Item is out of date")

  /**@since New in 1.2.
   *
   * This is analogous to SVN_ERR_REPOS_UNSUPPORTED_VERSION.  To avoid
   * confusion with "versions" (i.e., releases) of Subversion, we've
   * started calling this the "format" number instead.  The old
   * SVN_ERR_REPOS_UNSUPPORTED_VERSION error predates this and so
   * retains its name.
   */
  SVN_ERRDEF(SVN_ERR_FS_UNSUPPORTED_FORMAT,
             SVN_ERR_FS_CATEGORY_START + 43,
             "Unsupported FS format")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_FS_REP_BEING_WRITTEN,
             SVN_ERR_FS_CATEGORY_START + 44,
             "Representation is being written")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_FS_TXN_NAME_TOO_LONG,
             SVN_ERR_FS_CATEGORY_START + 45,
             "The generated transaction name is too long")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_NODE_ORIGIN,
             SVN_ERR_FS_CATEGORY_START + 46,
             "Filesystem has no such node origin record")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_FS_UNSUPPORTED_UPGRADE,
             SVN_ERR_FS_CATEGORY_START + 47,
             "Filesystem upgrade is not supported")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_CHECKSUM_REP,
             SVN_ERR_FS_CATEGORY_START + 48,
             "Filesystem has no such checksum-representation index record")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_FS_PROP_BASEVALUE_MISMATCH,
             SVN_ERR_FS_CATEGORY_START + 49,
             "Property value in filesystem differs from the provided "
             "base value")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_FS_INCORRECT_EDITOR_COMPLETION,
             SVN_ERR_FS_CATEGORY_START + 50,
             "The filesystem editor completion process was not followed")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_FS_PACKED_REVPROP_READ_FAILURE,
             SVN_ERR_FS_CATEGORY_START + 51,
             "A packed revprop could not be read")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_FS_REVPROP_CACHE_INIT_FAILURE,
             SVN_ERR_FS_CATEGORY_START + 52,
             "Could not initialize the revprop caching infrastructure.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_MALFORMED_TXN_ID,
             SVN_ERR_FS_CATEGORY_START + 53,
             "Malformed transaction ID string.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_INDEX_CORRUPTION,
             SVN_ERR_FS_CATEGORY_START + 54,
             "Corrupt index file.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_INDEX_REVISION,
             SVN_ERR_FS_CATEGORY_START + 55,
             "Revision not covered by index.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_INDEX_OVERFLOW,
             SVN_ERR_FS_CATEGORY_START + 56,
             "Item index too large for this revision.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_CONTAINER_INDEX,
             SVN_ERR_FS_CATEGORY_START + 57,
             "Container index out of range.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_INDEX_INCONSISTENT,
             SVN_ERR_FS_CATEGORY_START + 58,
             "Index files are inconsistent.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_LOCK_OPERATION_FAILED,
             SVN_ERR_FS_CATEGORY_START + 59,
             "Lock operation failed")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_UNSUPPORTED_TYPE,
             SVN_ERR_FS_CATEGORY_START + 60,
             "Unsupported FS type")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_CONTAINER_SIZE,
             SVN_ERR_FS_CATEGORY_START + 61,
             "Container capacity exceeded.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_MALFORMED_NODEREV_ID,
             SVN_ERR_FS_CATEGORY_START + 62,
             "Malformed node revision ID string.")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_FS_INVALID_GENERATION,
             SVN_ERR_FS_CATEGORY_START + 63,
             "Invalid generation number data.")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_FS_CORRUPT_REVPROP_MANIFEST,
             SVN_ERR_FS_CATEGORY_START + 64,
             "Revprop manifest corrupt.")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_FS_CORRUPT_PROPLIST,
             SVN_ERR_FS_CATEGORY_START + 65,
             "Property list is corrupt.")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_FS_AMBIGUOUS_CHECKSUM_REP,
             SVN_ERR_FS_CATEGORY_START + 67,
             "Content checksums supposedly match but content does not.")

  /* repos errors */

  SVN_ERRDEF(SVN_ERR_REPOS_LOCKED,
             SVN_ERR_REPOS_CATEGORY_START + 0,
             "The repository is locked, perhaps for db recovery")

  SVN_ERRDEF(SVN_ERR_REPOS_HOOK_FAILURE,
             SVN_ERR_REPOS_CATEGORY_START + 1,
             "A repository hook failed")

  SVN_ERRDEF(SVN_ERR_REPOS_BAD_ARGS,
             SVN_ERR_REPOS_CATEGORY_START + 2,
             "Incorrect arguments supplied")

  SVN_ERRDEF(SVN_ERR_REPOS_NO_DATA_FOR_REPORT,
             SVN_ERR_REPOS_CATEGORY_START + 3,
             "A report cannot be generated because no data was supplied")

  SVN_ERRDEF(SVN_ERR_REPOS_BAD_REVISION_REPORT,
             SVN_ERR_REPOS_CATEGORY_START + 4,
             "Bogus revision report")

  /* This is analogous to SVN_ERR_FS_UNSUPPORTED_FORMAT.  To avoid
   * confusion with "versions" (i.e., releases) of Subversion, we
   * started using the word "format" instead of "version".  However,
   * this error code's name predates that decision.
   */
  SVN_ERRDEF(SVN_ERR_REPOS_UNSUPPORTED_VERSION,
             SVN_ERR_REPOS_CATEGORY_START + 5,
             "Unsupported repository version")

  SVN_ERRDEF(SVN_ERR_REPOS_DISABLED_FEATURE,
             SVN_ERR_REPOS_CATEGORY_START + 6,
             "Disabled repository feature")

  SVN_ERRDEF(SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED,
             SVN_ERR_REPOS_CATEGORY_START + 7,
             "Error running post-commit hook")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_REPOS_POST_LOCK_HOOK_FAILED,
             SVN_ERR_REPOS_CATEGORY_START + 8,
             "Error running post-lock hook")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_REPOS_POST_UNLOCK_HOOK_FAILED,
             SVN_ERR_REPOS_CATEGORY_START + 9,
             "Error running post-unlock hook")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_REPOS_UNSUPPORTED_UPGRADE,
             SVN_ERR_REPOS_CATEGORY_START + 10,
             "Repository upgrade is not supported")

  /* generic RA errors */

  SVN_ERRDEF(SVN_ERR_RA_ILLEGAL_URL,
             SVN_ERR_RA_CATEGORY_START + 0,
             "Bad URL passed to RA layer")

  SVN_ERRDEF(SVN_ERR_RA_NOT_AUTHORIZED,
             SVN_ERR_RA_CATEGORY_START + 1,
             "Authorization failed")

  SVN_ERRDEF(SVN_ERR_RA_UNKNOWN_AUTH,
             SVN_ERR_RA_CATEGORY_START + 2,
             "Unknown authorization method")

  SVN_ERRDEF(SVN_ERR_RA_NOT_IMPLEMENTED,
             SVN_ERR_RA_CATEGORY_START + 3,
             "Repository access method not implemented")

  SVN_ERRDEF(SVN_ERR_RA_OUT_OF_DATE,
             SVN_ERR_RA_CATEGORY_START + 4,
             "Item is out of date")

  SVN_ERRDEF(SVN_ERR_RA_NO_REPOS_UUID,
             SVN_ERR_RA_CATEGORY_START + 5,
             "Repository has no UUID")

  SVN_ERRDEF(SVN_ERR_RA_UNSUPPORTED_ABI_VERSION,
             SVN_ERR_RA_CATEGORY_START + 6,
             "Unsupported RA plugin ABI version")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_RA_NOT_LOCKED,
             SVN_ERR_RA_CATEGORY_START + 7,
             "Path is not locked")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_RA_PARTIAL_REPLAY_NOT_SUPPORTED,
             SVN_ERR_RA_CATEGORY_START + 8,
             "Server can only replay from the root of a repository")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_RA_UUID_MISMATCH,
             SVN_ERR_RA_CATEGORY_START + 9,
             "Repository UUID does not match expected UUID")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_RA_REPOS_ROOT_URL_MISMATCH,
             SVN_ERR_RA_CATEGORY_START + 10,
             "Repository root URL does not match expected root URL")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_RA_SESSION_URL_MISMATCH,
             SVN_ERR_RA_CATEGORY_START + 11,
             "Session URL does not match expected session URL")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_RA_CANNOT_CREATE_TUNNEL,
             SVN_ERR_RA_CATEGORY_START + 12,
             "Can't create tunnel")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_RA_CANNOT_CREATE_SESSION,
             SVN_ERR_RA_CATEGORY_START + 13,
             "Can't create session")

  /* ra_dav errors */

  SVN_ERRDEF(SVN_ERR_RA_DAV_SOCK_INIT,
             SVN_ERR_RA_DAV_CATEGORY_START + 0,
             "RA layer failed to init socket layer")

  SVN_ERRDEF(SVN_ERR_RA_DAV_CREATING_REQUEST,
             SVN_ERR_RA_DAV_CATEGORY_START + 1,
             "RA layer failed to create HTTP request")

  SVN_ERRDEF(SVN_ERR_RA_DAV_REQUEST_FAILED,
             SVN_ERR_RA_DAV_CATEGORY_START + 2,
             "RA layer request failed")

  SVN_ERRDEF(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED,
             SVN_ERR_RA_DAV_CATEGORY_START + 3,
             "RA layer didn't receive requested OPTIONS info")

  SVN_ERRDEF(SVN_ERR_RA_DAV_PROPS_NOT_FOUND,
             SVN_ERR_RA_DAV_CATEGORY_START + 4,
             "RA layer failed to fetch properties")

  SVN_ERRDEF(SVN_ERR_RA_DAV_ALREADY_EXISTS,
             SVN_ERR_RA_DAV_CATEGORY_START + 5,
             "RA layer file already exists")

  /** @deprecated To improve consistency between ra layers, this error code
      is replaced by SVN_ERR_BAD_CONFIG_VALUE.
      Slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE,
             SVN_ERR_RA_DAV_CATEGORY_START + 6,
             "Invalid configuration value")

  /** @deprecated To improve consistency between ra layers, this error code
      is replaced in ra_serf by SVN_ERR_FS_NOT_FOUND.
      Slated for removal in the next major release. */
  SVN_ERRDEF(SVN_ERR_RA_DAV_PATH_NOT_FOUND,
             SVN_ERR_RA_DAV_CATEGORY_START + 7,
             "HTTP Path Not Found")

  SVN_ERRDEF(SVN_ERR_RA_DAV_PROPPATCH_FAILED,
             SVN_ERR_RA_DAV_CATEGORY_START + 8,
             "Failed to execute WebDAV PROPPATCH")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_RA_DAV_MALFORMED_DATA,
             SVN_ERR_RA_DAV_CATEGORY_START + 9,
             "Malformed network data")

  /** @since New in 1.3 */
  SVN_ERRDEF(SVN_ERR_RA_DAV_RESPONSE_HEADER_BADNESS,
             SVN_ERR_RA_DAV_CATEGORY_START + 10,
             "Unable to extract data from response header")

  /** @since New in 1.5 */
  SVN_ERRDEF(SVN_ERR_RA_DAV_RELOCATED,
             SVN_ERR_RA_DAV_CATEGORY_START + 11,
             "Repository has been moved")

  /** @since New in 1.7 */
  SVN_ERRDEF(SVN_ERR_RA_DAV_CONN_TIMEOUT,
             SVN_ERR_RA_DAV_CATEGORY_START + 12,
             "Connection timed out")

  /** @since New in 1.6 */
  SVN_ERRDEF(SVN_ERR_RA_DAV_FORBIDDEN,
             SVN_ERR_RA_DAV_CATEGORY_START + 13,
             "URL access forbidden for unknown reason")

  /** @since New in 1.9 */
  SVN_ERRDEF(SVN_ERR_RA_DAV_PRECONDITION_FAILED,
             SVN_ERR_RA_DAV_CATEGORY_START + 14,
             "The server state conflicts with the requested preconditions")

  /** @since New in 1.9 */
  SVN_ERRDEF(SVN_ERR_RA_DAV_METHOD_NOT_ALLOWED,
             SVN_ERR_RA_DAV_CATEGORY_START + 15,
             "The URL doesn't allow the requested method")

  /* ra_local errors */

  SVN_ERRDEF(SVN_ERR_RA_LOCAL_REPOS_NOT_FOUND,
             SVN_ERR_RA_LOCAL_CATEGORY_START + 0,
             "Couldn't find a repository")

  SVN_ERRDEF(SVN_ERR_RA_LOCAL_REPOS_OPEN_FAILED,
             SVN_ERR_RA_LOCAL_CATEGORY_START + 1,
             "Couldn't open a repository")

  /* svndiff errors */

  SVN_ERRDEF(SVN_ERR_SVNDIFF_INVALID_HEADER,
             SVN_ERR_SVNDIFF_CATEGORY_START + 0,
             "Svndiff data has invalid header")

  SVN_ERRDEF(SVN_ERR_SVNDIFF_CORRUPT_WINDOW,
             SVN_ERR_SVNDIFF_CATEGORY_START + 1,
             "Svndiff data contains corrupt window")

  SVN_ERRDEF(SVN_ERR_SVNDIFF_BACKWARD_VIEW,
             SVN_ERR_SVNDIFF_CATEGORY_START + 2,
             "Svndiff data contains backward-sliding source view")

  SVN_ERRDEF(SVN_ERR_SVNDIFF_INVALID_OPS,
             SVN_ERR_SVNDIFF_CATEGORY_START + 3,
             "Svndiff data contains invalid instruction")

  SVN_ERRDEF(SVN_ERR_SVNDIFF_UNEXPECTED_END,
             SVN_ERR_SVNDIFF_CATEGORY_START + 4,
             "Svndiff data ends unexpectedly")

  SVN_ERRDEF(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA,
             SVN_ERR_SVNDIFF_CATEGORY_START + 5,
             "Svndiff compressed data is invalid")

  /* mod_dav_svn errors */

  SVN_ERRDEF(SVN_ERR_APMOD_MISSING_PATH_TO_FS,
             SVN_ERR_APMOD_CATEGORY_START + 0,
             "Apache has no path to an SVN filesystem")

  SVN_ERRDEF(SVN_ERR_APMOD_MALFORMED_URI,
             SVN_ERR_APMOD_CATEGORY_START + 1,
             "Apache got a malformed URI")

  SVN_ERRDEF(SVN_ERR_APMOD_ACTIVITY_NOT_FOUND,
             SVN_ERR_APMOD_CATEGORY_START + 2,
             "Activity not found")

  SVN_ERRDEF(SVN_ERR_APMOD_BAD_BASELINE,
             SVN_ERR_APMOD_CATEGORY_START + 3,
             "Baseline incorrect")

  SVN_ERRDEF(SVN_ERR_APMOD_CONNECTION_ABORTED,
             SVN_ERR_APMOD_CATEGORY_START + 4,
             "Input/output error")

  /* libsvn_client errors */

  SVN_ERRDEF(SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED,
             SVN_ERR_CLIENT_CATEGORY_START + 0,
             "A path under version control is needed for this operation")

  SVN_ERRDEF(SVN_ERR_CLIENT_RA_ACCESS_REQUIRED,
             SVN_ERR_CLIENT_CATEGORY_START + 1,
             "Repository access is needed for this operation")

  SVN_ERRDEF(SVN_ERR_CLIENT_BAD_REVISION,
             SVN_ERR_CLIENT_CATEGORY_START + 2,
             "Bogus revision information given")

  SVN_ERRDEF(SVN_ERR_CLIENT_DUPLICATE_COMMIT_URL,
             SVN_ERR_CLIENT_CATEGORY_START + 3,
             "Attempting to commit to a URL more than once")

  SVN_ERRDEF(SVN_ERR_CLIENT_IS_BINARY_FILE,
             SVN_ERR_CLIENT_CATEGORY_START + 4,
             "Operation does not apply to binary file")

       /*### SVN_PROP_EXTERNALS needed to be replaced with "svn:externals"
         in order to get gettext translatable strings */
  SVN_ERRDEF(SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION,
             SVN_ERR_CLIENT_CATEGORY_START + 5,
             "Format of an svn:externals property was invalid")

  SVN_ERRDEF(SVN_ERR_CLIENT_MODIFIED,
             SVN_ERR_CLIENT_CATEGORY_START + 6,
             "Attempting restricted operation for modified resource")

  SVN_ERRDEF(SVN_ERR_CLIENT_IS_DIRECTORY,
             SVN_ERR_CLIENT_CATEGORY_START + 7,
             "Operation does not apply to directory")

  SVN_ERRDEF(SVN_ERR_CLIENT_REVISION_RANGE,
             SVN_ERR_CLIENT_CATEGORY_START + 8,
             "Revision range is not allowed")

  SVN_ERRDEF(SVN_ERR_CLIENT_INVALID_RELOCATION,
             SVN_ERR_CLIENT_CATEGORY_START + 9,
             "Inter-repository relocation not allowed")

  SVN_ERRDEF(SVN_ERR_CLIENT_REVISION_AUTHOR_CONTAINS_NEWLINE,
             SVN_ERR_CLIENT_CATEGORY_START + 10,
             "Author name cannot contain a newline")

  SVN_ERRDEF(SVN_ERR_CLIENT_PROPERTY_NAME,
             SVN_ERR_CLIENT_CATEGORY_START + 11,
             "Bad property name")

  /** @since New in 1.1. */
  SVN_ERRDEF(SVN_ERR_CLIENT_UNRELATED_RESOURCES,
             SVN_ERR_CLIENT_CATEGORY_START + 12,
             "Two versioned resources are unrelated")

  /** @since New in 1.2. */
  SVN_ERRDEF(SVN_ERR_CLIENT_MISSING_LOCK_TOKEN,
             SVN_ERR_CLIENT_CATEGORY_START + 13,
             "Path has no lock token")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_CLIENT_MULTIPLE_SOURCES_DISALLOWED,
             SVN_ERR_CLIENT_CATEGORY_START + 14,
             "Operation does not support multiple sources")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_CLIENT_NO_VERSIONED_PARENT,
             SVN_ERR_CLIENT_CATEGORY_START + 15,
             "No versioned parent directories")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_CLIENT_NOT_READY_TO_MERGE,
             SVN_ERR_CLIENT_CATEGORY_START + 16,
             "Working copy and merge source not ready for reintegration")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_CLIENT_FILE_EXTERNAL_OVERWRITE_VERSIONED,
             SVN_ERR_CLIENT_CATEGORY_START + 17,
             "A file external cannot overwrite an existing versioned item")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_CLIENT_PATCH_BAD_STRIP_COUNT,
             SVN_ERR_CLIENT_CATEGORY_START + 18,
             "Invalid path component strip count specified")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_CLIENT_CYCLE_DETECTED,
             SVN_ERR_CLIENT_CATEGORY_START + 19,
             "Detected a cycle while processing the operation")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_CLIENT_MERGE_UPDATE_REQUIRED,
             SVN_ERR_CLIENT_CATEGORY_START + 20,
             "Working copy and merge source not ready for reintegration")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_CLIENT_INVALID_MERGEINFO_NO_MERGETRACKING,
             SVN_ERR_CLIENT_CATEGORY_START + 21,
             "Invalid mergeinfo detected in merge target")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_CLIENT_NO_LOCK_TOKEN,
             SVN_ERR_CLIENT_CATEGORY_START + 22,
             "Can't perform this operation without a valid lock token")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_CLIENT_FORBIDDEN_BY_SERVER,
             SVN_ERR_CLIENT_CATEGORY_START + 23,
             "The operation is forbidden by the server")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_CLIENT_CONFLICT_OPTION_NOT_APPLICABLE,
             SVN_ERR_CLIENT_CATEGORY_START + 24,
             "The conflict resolution option is not applicable")

  /* misc errors */

  SVN_ERRDEF(SVN_ERR_BASE,
             SVN_ERR_MISC_CATEGORY_START + 0,
             "A problem occurred; see other errors for details")

  SVN_ERRDEF(SVN_ERR_PLUGIN_LOAD_FAILURE,
             SVN_ERR_MISC_CATEGORY_START + 1,
             "Failure loading plugin")

  SVN_ERRDEF(SVN_ERR_MALFORMED_FILE,
             SVN_ERR_MISC_CATEGORY_START + 2,
             "Malformed file")

  SVN_ERRDEF(SVN_ERR_INCOMPLETE_DATA,
             SVN_ERR_MISC_CATEGORY_START + 3,
             "Incomplete data")

  SVN_ERRDEF(SVN_ERR_INCORRECT_PARAMS,
             SVN_ERR_MISC_CATEGORY_START + 4,
             "Incorrect parameters given")

  SVN_ERRDEF(SVN_ERR_UNVERSIONED_RESOURCE,
             SVN_ERR_MISC_CATEGORY_START + 5,
             "Tried a versioning operation on an unversioned resource")

  SVN_ERRDEF(SVN_ERR_TEST_FAILED,
             SVN_ERR_MISC_CATEGORY_START + 6,
             "Test failed")

  SVN_ERRDEF(SVN_ERR_UNSUPPORTED_FEATURE,
             SVN_ERR_MISC_CATEGORY_START + 7,
             "Trying to use an unsupported feature")

  SVN_ERRDEF(SVN_ERR_BAD_PROP_KIND,
             SVN_ERR_MISC_CATEGORY_START + 8,
             "Unexpected or unknown property kind")

  SVN_ERRDEF(SVN_ERR_ILLEGAL_TARGET,
             SVN_ERR_MISC_CATEGORY_START + 9,
             "Illegal target for the requested operation")

  SVN_ERRDEF(SVN_ERR_DELTA_MD5_CHECKSUM_ABSENT,
             SVN_ERR_MISC_CATEGORY_START + 10,
             "MD5 checksum is missing")

  SVN_ERRDEF(SVN_ERR_DIR_NOT_EMPTY,
             SVN_ERR_MISC_CATEGORY_START + 11,
             "Directory needs to be empty but is not")

  SVN_ERRDEF(SVN_ERR_EXTERNAL_PROGRAM,
             SVN_ERR_MISC_CATEGORY_START + 12,
             "Error calling external program")

  SVN_ERRDEF(SVN_ERR_SWIG_PY_EXCEPTION_SET,
             SVN_ERR_MISC_CATEGORY_START + 13,
             "Python exception has been set with the error")

  SVN_ERRDEF(SVN_ERR_CHECKSUM_MISMATCH,
             SVN_ERR_MISC_CATEGORY_START + 14,
             "A checksum mismatch occurred")

  SVN_ERRDEF(SVN_ERR_CANCELLED,
             SVN_ERR_MISC_CATEGORY_START + 15,
             "The operation was interrupted")

  SVN_ERRDEF(SVN_ERR_INVALID_DIFF_OPTION,
             SVN_ERR_MISC_CATEGORY_START + 16,
             "The specified diff option is not supported")

  SVN_ERRDEF(SVN_ERR_PROPERTY_NOT_FOUND,
             SVN_ERR_MISC_CATEGORY_START + 17,
             "Property not found")

  SVN_ERRDEF(SVN_ERR_NO_AUTH_FILE_PATH,
             SVN_ERR_MISC_CATEGORY_START + 18,
             "No auth file path available")

  /** @since New in 1.1. */
  SVN_ERRDEF(SVN_ERR_VERSION_MISMATCH,
             SVN_ERR_MISC_CATEGORY_START + 19,
             "Incompatible library version")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_MERGEINFO_PARSE_ERROR,
             SVN_ERR_MISC_CATEGORY_START + 20,
             "Mergeinfo parse error")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_CEASE_INVOCATION,
             SVN_ERR_MISC_CATEGORY_START + 21,
             "Cease invocation of this API")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_REVNUM_PARSE_FAILURE,
             SVN_ERR_MISC_CATEGORY_START + 22,
             "Error parsing revision number")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_ITER_BREAK,
             SVN_ERR_MISC_CATEGORY_START + 23,
             "Iteration terminated before completion")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_UNKNOWN_CHANGELIST,
             SVN_ERR_MISC_CATEGORY_START + 24,
             "Unknown changelist")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_RESERVED_FILENAME_SPECIFIED,
             SVN_ERR_MISC_CATEGORY_START + 25,
             "Reserved directory name in command line arguments")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_UNKNOWN_CAPABILITY,
             SVN_ERR_MISC_CATEGORY_START + 26,
             "Inquiry about unknown capability")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_TEST_SKIPPED,
             SVN_ERR_MISC_CATEGORY_START + 27,
             "Test skipped")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_NO_APR_MEMCACHE,
             SVN_ERR_MISC_CATEGORY_START + 28,
             "APR memcache library not available")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_ATOMIC_INIT_FAILURE,
             SVN_ERR_MISC_CATEGORY_START + 29,
             "Couldn't perform atomic initialization")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_SQLITE_ERROR,
             SVN_ERR_MISC_CATEGORY_START + 30,
             "SQLite error")

  /** @since New in 1.6. */
  SVN_ERRDEF(SVN_ERR_SQLITE_READONLY,
             SVN_ERR_MISC_CATEGORY_START + 31,
             "Attempted to write to readonly SQLite db")

  /** @since New in 1.6.
   * @deprecated the internal sqlite support code does not manage schemas
   * any longer.  */
  SVN_ERRDEF(SVN_ERR_SQLITE_UNSUPPORTED_SCHEMA,
             SVN_ERR_MISC_CATEGORY_START + 32,
             "Unsupported schema found in SQLite db")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_SQLITE_BUSY,
             SVN_ERR_MISC_CATEGORY_START + 33,
             "The SQLite db is busy")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_SQLITE_RESETTING_FOR_ROLLBACK,
             SVN_ERR_MISC_CATEGORY_START + 34,
             "SQLite busy at transaction rollback; "
             "resetting all busy SQLite statements to allow rollback")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_SQLITE_CONSTRAINT,
             SVN_ERR_MISC_CATEGORY_START + 35,
             "Constraint error in SQLite db")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_TOO_MANY_MEMCACHED_SERVERS,
             SVN_ERR_MISC_CATEGORY_START + 36,
             "Too many memcached servers configured")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_MALFORMED_VERSION_STRING,
             SVN_ERR_MISC_CATEGORY_START + 37,
             "Failed to parse version number string")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_CORRUPTED_ATOMIC_STORAGE,
             SVN_ERR_MISC_CATEGORY_START + 38,
             "Atomic data storage is corrupt")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_UTF8PROC_ERROR,
             SVN_ERR_MISC_CATEGORY_START + 39,
             "utf8proc library error")

  /** @since New in 1.8. */
  SVN_ERRDEF(SVN_ERR_UTF8_GLOB,
             SVN_ERR_MISC_CATEGORY_START + 40,
             "Bad arguments to SQL operators GLOB or LIKE")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_CORRUPT_PACKED_DATA,
             SVN_ERR_MISC_CATEGORY_START + 41,
             "Packed data stream is corrupt")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_COMPOSED_ERROR,
             SVN_ERR_MISC_CATEGORY_START + 42,
             "Additional errors:")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_INVALID_INPUT,
             SVN_ERR_MISC_CATEGORY_START + 43,
             "Parser error: invalid input")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_SQLITE_ROLLBACK_FAILED,
             SVN_ERR_MISC_CATEGORY_START + 44,
             "SQLite transaction rollback failed")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_LZ4_COMPRESSION_FAILED,
             SVN_ERR_MISC_CATEGORY_START + 45,
             "LZ4 compression failed")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_LZ4_DECOMPRESSION_FAILED,
             SVN_ERR_MISC_CATEGORY_START + 46,
             "LZ4 decompression failed")

  /* command-line client errors */

  SVN_ERRDEF(SVN_ERR_CL_ARG_PARSING_ERROR,
             SVN_ERR_CL_CATEGORY_START + 0,
             "Error parsing arguments")

  SVN_ERRDEF(SVN_ERR_CL_INSUFFICIENT_ARGS,
             SVN_ERR_CL_CATEGORY_START + 1,
             "Not enough arguments provided")

  SVN_ERRDEF(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS,
             SVN_ERR_CL_CATEGORY_START + 2,
             "Mutually exclusive arguments specified")

  SVN_ERRDEF(SVN_ERR_CL_ADM_DIR_RESERVED,
             SVN_ERR_CL_CATEGORY_START + 3,
             "Attempted command in administrative dir")

  SVN_ERRDEF(SVN_ERR_CL_LOG_MESSAGE_IS_VERSIONED_FILE,
             SVN_ERR_CL_CATEGORY_START + 4,
             "The log message file is under version control")

  SVN_ERRDEF(SVN_ERR_CL_LOG_MESSAGE_IS_PATHNAME,
             SVN_ERR_CL_CATEGORY_START + 5,
             "The log message is a pathname")

  SVN_ERRDEF(SVN_ERR_CL_COMMIT_IN_ADDED_DIR,
             SVN_ERR_CL_CATEGORY_START + 6,
             "Committing in directory scheduled for addition")

  SVN_ERRDEF(SVN_ERR_CL_NO_EXTERNAL_EDITOR,
             SVN_ERR_CL_CATEGORY_START + 7,
             "No external editor available")

  SVN_ERRDEF(SVN_ERR_CL_BAD_LOG_MESSAGE,
             SVN_ERR_CL_CATEGORY_START + 8,
             "Something is wrong with the log message's contents")

  SVN_ERRDEF(SVN_ERR_CL_UNNECESSARY_LOG_MESSAGE,
             SVN_ERR_CL_CATEGORY_START + 9,
             "A log message was given where none was necessary")

  SVN_ERRDEF(SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL,
             SVN_ERR_CL_CATEGORY_START + 10,
             "No external merge tool available")

  SVN_ERRDEF(SVN_ERR_CL_ERROR_PROCESSING_EXTERNALS,
             SVN_ERR_CL_CATEGORY_START + 11,
             "Failed processing one or more externals definitions")

  /** @since New in 1.9. */
  SVN_ERRDEF(SVN_ERR_CL_REPOS_VERIFY_FAILED,
             SVN_ERR_CL_CATEGORY_START + 12,
             "Repository verification failed")

  /* ra_svn errors */

  SVN_ERRDEF(SVN_ERR_RA_SVN_CMD_ERR,
             SVN_ERR_RA_SVN_CATEGORY_START + 0,
             "Special code for wrapping server errors to report to client")

  SVN_ERRDEF(SVN_ERR_RA_SVN_UNKNOWN_CMD,
             SVN_ERR_RA_SVN_CATEGORY_START + 1,
             "Unknown svn protocol command")

  SVN_ERRDEF(SVN_ERR_RA_SVN_CONNECTION_CLOSED,
             SVN_ERR_RA_SVN_CATEGORY_START + 2,
             "Network connection closed unexpectedly")

  SVN_ERRDEF(SVN_ERR_RA_SVN_IO_ERROR,
             SVN_ERR_RA_SVN_CATEGORY_START + 3,
             "Network read/write error")

  SVN_ERRDEF(SVN_ERR_RA_SVN_MALFORMED_DATA,
             SVN_ERR_RA_SVN_CATEGORY_START + 4,
             "Malformed network data")

  SVN_ERRDEF(SVN_ERR_RA_SVN_REPOS_NOT_FOUND,
             SVN_ERR_RA_SVN_CATEGORY_START + 5,
             "Couldn't find a repository")

  SVN_ERRDEF(SVN_ERR_RA_SVN_BAD_VERSION,
             SVN_ERR_RA_SVN_CATEGORY_START + 6,
             "Client/server version mismatch")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_RA_SVN_NO_MECHANISMS,
             SVN_ERR_RA_SVN_CATEGORY_START + 7,
             "Cannot negotiate authentication mechanism")

  /** @since New in 1.7  */
  SVN_ERRDEF(SVN_ERR_RA_SVN_EDIT_ABORTED,
             SVN_ERR_RA_SVN_CATEGORY_START + 8,
             "Editor drive was aborted")

  /** @since New in 1.10  */
  SVN_ERRDEF(SVN_ERR_RA_SVN_REQUEST_SIZE,
             SVN_ERR_RA_SVN_CATEGORY_START + 9,
             "Client request too long")

  /** @since New in 1.10  */
  SVN_ERRDEF(SVN_ERR_RA_SVN_RESPONSE_SIZE,
             SVN_ERR_RA_SVN_CATEGORY_START + 10,
             "Server response too long")

  /* libsvn_auth errors */

       /* this error can be used when an auth provider doesn't have
          the creds, but no other "real" error occurred. */
  SVN_ERRDEF(SVN_ERR_AUTHN_CREDS_UNAVAILABLE,
             SVN_ERR_AUTHN_CATEGORY_START + 0,
             "Credential data unavailable")

  SVN_ERRDEF(SVN_ERR_AUTHN_NO_PROVIDER,
             SVN_ERR_AUTHN_CATEGORY_START + 1,
             "No authentication provider available")

  SVN_ERRDEF(SVN_ERR_AUTHN_PROVIDERS_EXHAUSTED,
             SVN_ERR_AUTHN_CATEGORY_START + 2,
             "All authentication providers exhausted")

  SVN_ERRDEF(SVN_ERR_AUTHN_CREDS_NOT_SAVED,
             SVN_ERR_AUTHN_CATEGORY_START + 3,
             "Credentials not saved")

  /** @since New in 1.5. */
  SVN_ERRDEF(SVN_ERR_AUTHN_FAILED,
             SVN_ERR_AUTHN_CATEGORY_START + 4,
             "Authentication failed")

  /* authorization errors */

  SVN_ERRDEF(SVN_ERR_AUTHZ_ROOT_UNREADABLE,
             SVN_ERR_AUTHZ_CATEGORY_START + 0,
             "Read access denied for root of edit")

  /** @since New in 1.1. */
  SVN_ERRDEF(SVN_ERR_AUTHZ_UNREADABLE,
             SVN_ERR_AUTHZ_CATEGORY_START + 1,
             "Item is not readable")

  /** @since New in 1.1. */
  SVN_ERRDEF(SVN_ERR_AUTHZ_PARTIALLY_READABLE,
             SVN_ERR_AUTHZ_CATEGORY_START + 2,
             "Item is partially readable")

  SVN_ERRDEF(SVN_ERR_AUTHZ_INVALID_CONFIG,
             SVN_ERR_AUTHZ_CATEGORY_START + 3,
             "Invalid authz configuration")

  /** @since New in 1.3 */
  SVN_ERRDEF(SVN_ERR_AUTHZ_UNWRITABLE,
             SVN_ERR_AUTHZ_CATEGORY_START + 4,
             "Item is not writable")


  /* libsvn_diff errors */

  SVN_ERRDEF(SVN_ERR_DIFF_DATASOURCE_MODIFIED,
             SVN_ERR_DIFF_CATEGORY_START + 0,
             "Diff data source modified unexpectedly")

  /** @since New in 1.10 */
  SVN_ERRDEF(SVN_ERR_DIFF_UNEXPECTED_DATA,
             SVN_ERR_DIFF_CATEGORY_START + 1,
             "Diff data unexpected")

  /* libsvn_ra_serf errors */
  /** @since New in 1.5.
      @deprecated SSPI now handled by serf rather than libsvn_ra_serf. */
  SVN_ERRDEF(SVN_ERR_RA_SERF_SSPI_INITIALISATION_FAILED,
             SVN_ERR_RA_SERF_CATEGORY_START + 0,
             "Initialization of SSPI library failed")
  /** @since New in 1.5.
      @deprecated Certificate verification now handled by serf rather
                  than libsvn_ra_serf. */
  SVN_ERRDEF(SVN_ERR_RA_SERF_SSL_CERT_UNTRUSTED,
             SVN_ERR_RA_SERF_CATEGORY_START + 1,
             "Server SSL certificate untrusted")
  /** @since New in 1.7.
      @deprecated GSSAPI now handled by serf rather than libsvn_ra_serf. */
  SVN_ERRDEF(SVN_ERR_RA_SERF_GSSAPI_INITIALISATION_FAILED,
             SVN_ERR_RA_SERF_CATEGORY_START + 2,
             "Initialization of the GSSAPI context failed")

  /** @since New in 1.7. */
  SVN_ERRDEF(SVN_ERR_RA_SERF_WRAPPED_ERROR,
             SVN_ERR_RA_SERF_CATEGORY_START + 3,
             "While handling serf response:")

  /** @since New in 1.10. */
  SVN_ERRDEF(SVN_ERR_RA_SERF_STREAM_BUCKET_READ_ERROR,
             SVN_ERR_RA_SERF_CATEGORY_START + 4,
             "Can't read from stream")

  /* malfunctions such as assertion failures */

  SVN_ERRDEF(SVN_ERR_ASSERTION_FAIL,
             SVN_ERR_MALFUNC_CATEGORY_START + 0,
             "Assertion failure")

  SVN_ERRDEF(SVN_ERR_ASSERTION_ONLY_TRACING_LINKS,
             SVN_ERR_MALFUNC_CATEGORY_START + 1,
             "No non-tracing links found in the error chain")

  /* X509 parser errors.
   * Names of these error codes are based on tropicssl error codes.
   * @since New in 1.9 */

  SVN_ERRDEF(SVN_ERR_ASN1_OUT_OF_DATA,
             SVN_ERR_X509_CATEGORY_START + 0,
             "Unexpected end of ASN1 data")

  SVN_ERRDEF(SVN_ERR_ASN1_UNEXPECTED_TAG,
             SVN_ERR_X509_CATEGORY_START + 1,
             "Unexpected ASN1 tag")

  SVN_ERRDEF(SVN_ERR_ASN1_INVALID_LENGTH,
             SVN_ERR_X509_CATEGORY_START + 2,
             "Invalid ASN1 length")

  SVN_ERRDEF(SVN_ERR_ASN1_LENGTH_MISMATCH,
             SVN_ERR_X509_CATEGORY_START + 3,
             "ASN1 length mismatch")

  SVN_ERRDEF(SVN_ERR_ASN1_INVALID_DATA,
             SVN_ERR_X509_CATEGORY_START + 4,
             "Invalid ASN1 data")

  SVN_ERRDEF(SVN_ERR_X509_FEATURE_UNAVAILABLE,
             SVN_ERR_X509_CATEGORY_START + 5,
             "Unavailable X509 feature")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_PEM,
             SVN_ERR_X509_CATEGORY_START + 6,
             "Invalid PEM certificate")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_FORMAT,
             SVN_ERR_X509_CATEGORY_START + 7,
             "Invalid certificate format")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_VERSION,
             SVN_ERR_X509_CATEGORY_START + 8,
             "Invalid certificate version")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_SERIAL,
             SVN_ERR_X509_CATEGORY_START + 9,
             "Invalid certificate serial number")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_ALG,
             SVN_ERR_X509_CATEGORY_START + 10,
             "Found invalid algorithm in certificate")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_NAME,
             SVN_ERR_X509_CATEGORY_START + 11,
             "Found invalid name in certificate")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_DATE,
             SVN_ERR_X509_CATEGORY_START + 12,
             "Found invalid date in certificate")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_PUBKEY,
             SVN_ERR_X509_CATEGORY_START + 13,
             "Found invalid public key in certificate")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_SIGNATURE,
             SVN_ERR_X509_CATEGORY_START + 14,
             "Found invalid signature in certificate")

  SVN_ERRDEF(SVN_ERR_X509_CERT_INVALID_EXTENSIONS,
             SVN_ERR_X509_CATEGORY_START + 15,
             "Found invalid extensions in certificate")

  SVN_ERRDEF(SVN_ERR_X509_CERT_UNKNOWN_VERSION,
             SVN_ERR_X509_CATEGORY_START + 16,
             "Unknown certificate version")

  SVN_ERRDEF(SVN_ERR_X509_CERT_UNKNOWN_PK_ALG,
             SVN_ERR_X509_CATEGORY_START + 17,
             "Certificate uses unknown public key algorithm")

  SVN_ERRDEF(SVN_ERR_X509_CERT_SIG_MISMATCH,
             SVN_ERR_X509_CATEGORY_START + 18,
             "Certificate signature mismatch")

  SVN_ERRDEF(SVN_ERR_X509_CERT_VERIFY_FAILED,
             SVN_ERR_X509_CATEGORY_START + 19,
             "Certficate verification failed")

SVN_ERROR_END


#undef SVN_ERROR_START
#undef SVN_ERRDEF
#undef SVN_ERROR_END

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* defined(SVN_ERROR_BUILD_ARRAY) || !defined(SVN_ERROR_ENUM_DEFINED) */
