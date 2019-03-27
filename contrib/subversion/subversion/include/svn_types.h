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
 * @file svn_types.h
 * @brief Subversion's data types
 */

#ifndef SVN_TYPES_H
#define SVN_TYPES_H

/* ### this should go away, but it causes too much breakage right now */
#include <stdlib.h>
#include <limits.h> /* for ULONG_MAX */

#include <apr.h>         /* for apr_size_t, apr_int64_t, ... */
#include <apr_version.h>
#include <apr_errno.h>   /* for apr_status_t */
#include <apr_pools.h>   /* for apr_pool_t */
#include <apr_hash.h>    /* for apr_hash_t */
#include <apr_tables.h>  /* for apr_array_push() */
#include <apr_time.h>    /* for apr_time_t */
#include <apr_strings.h> /* for apr_atoi64() */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** Macro used to mark deprecated functions.
 *
 * @since New in 1.6.
 */
#ifndef SVN_DEPRECATED
# if !defined(SWIGPERL) && !defined(SWIGPYTHON) && !defined(SWIGRUBY)
#  if defined(__GNUC__) && (__GNUC__ >= 4 || (__GNUC__==3 && __GNUC_MINOR__>=1))
#   define SVN_DEPRECATED __attribute__((deprecated))
#  elif defined(_MSC_VER) && _MSC_VER >= 1300
#   define SVN_DEPRECATED __declspec(deprecated)
#  else
#   define SVN_DEPRECATED
#  endif
# else
#  define SVN_DEPRECATED
# endif
#endif


/** Macro used to mark experimental functions.
 *
 * @since New in 1.9.
 */
#ifndef SVN_EXPERIMENTAL
# if !defined(SWIGPERL) && !defined(SWIGPYTHON) && !defined(SWIGRUBY)
#  if defined(__has_attribute)
#    if __has_attribute(__warning__)
#      define SVN_EXPERIMENTAL __attribute__((warning("experimental function used")))
#    else
#      define SVN_EXPERIMENTAL
#    endif
#  elif !defined(__llvm__) && defined(__GNUC__) \
      && (__GNUC__ >= 4 || (__GNUC__==3 && __GNUC_MINOR__>=1))
#   define SVN_EXPERIMENTAL __attribute__((warning("experimental function used")))
#  elif defined(_MSC_VER) && _MSC_VER >= 1300
#   define SVN_EXPERIMENTAL __declspec(deprecated("experimental function used"))
#  else
#   define SVN_EXPERIMENTAL
#  endif
# else
#  define SVN_EXPERIMENTAL
# endif
#endif

/** Macro used to mark functions that require a final null sentinel argument.
 *
 * @since New in 1.9.
 */
#ifndef SVN_NEEDS_SENTINEL_NULL
#  if defined(__has_attribute)
#    if __has_attribute(__sentinel__)
#      define SVN_NEEDS_SENTINEL_NULL __attribute__((sentinel))
#    else
#      define SVN_NEEDS_SENTINEL_NULL
#    endif
#  elif defined(__GNUC__) && (__GNUC__ >= 4)
#    define SVN_NEEDS_SENTINEL_NULL __attribute__((sentinel))
#  else
#    define SVN_NEEDS_SENTINEL_NULL
#  endif
#endif


/** Indicate whether the current platform supports unaligned data access.
 *
 * On the majority of machines running SVN (x86 / x64), unaligned access
 * is much cheaper than repeated aligned access. Define this macro to 1
 * on those machines.
 * Unaligned access on other machines (e.g. IA64) will trigger memory
 * access faults or simply misbehave.
 *
 * Note: Some platforms may only support unaligned access for integers
 * (PowerPC).  As a result this macro should only be used to determine
 * if unaligned access is supported for integers.
 *
 * @since New in 1.7.
 */
#ifndef SVN_UNALIGNED_ACCESS_IS_OK
# if defined(_M_IX86) || defined(i386) \
     || defined(_M_X64) || defined(__x86_64) \
     || defined(__powerpc__) || defined(__ppc__)
#  define SVN_UNALIGNED_ACCESS_IS_OK 1
# else
#  define SVN_UNALIGNED_ACCESS_IS_OK 0
# endif
#endif



/** YABT:  Yet Another Boolean Type */
typedef int svn_boolean_t;

#ifndef TRUE
/** uhh... true */
#define TRUE 1
#endif /* TRUE */

#ifndef FALSE
/** uhh... false */
#define FALSE 0
#endif /* FALSE */



/* Declaration of a unique type, never defined, for the SVN_VA_NULL macro.
 *
 * NOTE: Private. Not for direct use by third-party code.
 */
struct svn__null_pointer_constant_stdarg_sentinel_t;

/** Null pointer constant used as a sentinel in variable argument lists.
 *
 * Use of this macro ensures that the argument is of the correct size when a
 * pointer is expected. (The macro @c NULL is not defined as a pointer on
 * all systems, and the arguments to variadic functions are not converted
 * automatically to the expected type.)
 *
 * @since New in 1.9.
 */
#define SVN_VA_NULL ((struct svn__null_pointer_constant_stdarg_sentinel_t*)0)
/* See? (char*)NULL -- They have the same length, but the cast looks ugly. */



/** Subversion error object.
 *
 * Defined here, rather than in svn_error.h, to avoid a recursive @#include
 * situation.
 */
typedef struct svn_error_t
{
  /** APR error value; possibly an SVN_ custom error code (see
   * `svn_error_codes.h' for a full listing).
   */
  apr_status_t apr_err;

  /** Details from the producer of error.
   *
   * Note that if this error was generated by Subversion's API, you'll
   * probably want to use svn_err_best_message() to get a single
   * descriptive string for this error chain (see the @a child member)
   * or svn_handle_error2() to print the error chain in full.  This is
   * because Subversion's API functions sometimes add many links to
   * the error chain that lack details (used only to produce virtual
   * stack traces).  (Use svn_error_purge_tracing() to remove those
   * trace-only links from the error chain.)
   */
  const char *message;

  /** Pointer to the error we "wrap" (may be @c NULL).  Via this
   * member, individual error object can be strung together into an
   * "error chain".
   */
  struct svn_error_t *child;

  /** The pool in which this error object is allocated.  (If
   * Subversion's APIs are used to manage error chains, then this pool
   * will contain the whole error chain of which this object is a
   * member.) */
  apr_pool_t *pool;

  /** Source file where the error originated (iff @c SVN_DEBUG;
   * undefined otherwise).
   */
  const char *file;

  /** Source line where the error originated (iff @c SVN_DEBUG;
   * undefined otherwise).
   */
  long line;

} svn_error_t;



/* See svn_version.h.
   Defined here to avoid including svn_version.h from all public headers. */
typedef struct svn_version_t svn_version_t;



/** @defgroup APR_ARRAY_compat_macros APR Array Compatibility Helper Macros
 * These macros are provided by APR itself from version 1.3.
 * Definitions are provided here for when using older versions of APR.
 * @{
 */

/** index into an apr_array_header_t */
#ifndef APR_ARRAY_IDX
#define APR_ARRAY_IDX(ary,i,type) (((type *)(ary)->elts)[i])
#endif

/** easier array-pushing syntax */
#ifndef APR_ARRAY_PUSH
#define APR_ARRAY_PUSH(ary,type) (*((type *)apr_array_push(ary)))
#endif

/** @} */



/** @defgroup apr_hash_utilities APR Hash Table Helpers
 * These functions enable the caller to dereference an APR hash table index
 * without type casts or temporary variables.
 *
 * These functions are provided by APR itself from version 1.5.
 * Definitions are provided here for when using older versions of APR.
 * @{
 */

#if !APR_VERSION_AT_LEAST(1, 5, 0)

/** Return the key of the hash table entry indexed by @a hi. */
const void *
apr_hash_this_key(apr_hash_index_t *hi);

/** Return the key length of the hash table entry indexed by @a hi. */
apr_ssize_t
apr_hash_this_key_len(apr_hash_index_t *hi);

/** Return the value of the hash table entry indexed by @a hi. */
void *
apr_hash_this_val(apr_hash_index_t *hi);

#endif

/** @} */



/** On Windows, APR_STATUS_IS_ENOTDIR includes several kinds of
 * invalid-pathname error but not ERROR_INVALID_NAME, so we include it.
 * We also include ERROR_DIRECTORY as that was not included in apr versions
 * before 1.4.0 and this fix is not backported */
/* ### These fixes should go into APR. */
#ifndef WIN32
#define SVN__APR_STATUS_IS_ENOTDIR(s)  APR_STATUS_IS_ENOTDIR(s)
#else
#define SVN__APR_STATUS_IS_ENOTDIR(s)  (APR_STATUS_IS_ENOTDIR(s) \
                      || ((s) == APR_OS_START_SYSERR + ERROR_DIRECTORY) \
                      || ((s) == APR_OS_START_SYSERR + ERROR_INVALID_NAME))
#endif

/** On Windows, APR_STATUS_IS_EPIPE does not include ERROR_NO_DATA error.
 * So we include it.*/
/* ### These fixes should go into APR. */
#ifndef WIN32
#define SVN__APR_STATUS_IS_EPIPE(s)  APR_STATUS_IS_EPIPE(s)
#else
#define SVN__APR_STATUS_IS_EPIPE(s)  (APR_STATUS_IS_EPIPE(s) \
                      || ((s) == APR_OS_START_SYSERR + ERROR_NO_DATA))
#endif

/** @} */



/** The various types of nodes in the Subversion filesystem. */
typedef enum svn_node_kind_t
{
  /** absent */
  svn_node_none,

  /** regular file */
  svn_node_file,

  /** directory */
  svn_node_dir,

  /** something's here, but we don't know what */
  svn_node_unknown,

  /**
   * symbolic link
   * @note This value is not currently used by the public API.
   * @since New in 1.8.
   */
  svn_node_symlink
} svn_node_kind_t;

/** Return a constant string expressing @a kind as an English word, e.g.,
 * "file", "dir", etc.  The string is not localized, as it may be used for
 * client<->server communications.  If the kind is not recognized, return
 * "unknown".
 *
 * @since New in 1.6.
 */
const char *
svn_node_kind_to_word(svn_node_kind_t kind);

/** Return the appropriate node_kind for @a word.  @a word is as
 * returned from svn_node_kind_to_word().  If @a word does not
 * represent a recognized kind or is @c NULL, return #svn_node_unknown.
 *
 * @since New in 1.6.
 */
svn_node_kind_t
svn_node_kind_from_word(const char *word);


/** Generic three-state property to represent an unknown value for values
 * that are just like booleans.  The values have been set deliberately to
 * make tristates disjoint from #svn_boolean_t.
 *
 * @note It is unsafe to use apr_pcalloc() to allocate these, since '0' is
 * not a valid value.
 *
 * @since New in 1.7. */
typedef enum svn_tristate_t
{
  /** state known to be false (the constant does not evaulate to false) */
  svn_tristate_false = 2,
  /** state known to be true */
  svn_tristate_true,
  /** state could be true or false */
  svn_tristate_unknown
} svn_tristate_t;

/** Return a constant string "true", "false" or NULL representing the value of
 * @a tristate.
 *
 * @since New in 1.7.
 */
const char *
svn_tristate__to_word(svn_tristate_t tristate);

/** Return the appropriate tristate for @a word. If @a word is "true", returns
 * #svn_tristate_true; if @a word is "false", returns #svn_tristate_false,
 * for all other values (including NULL) returns #svn_tristate_unknown.
 *
 * @since New in 1.7.
 */
svn_tristate_t
svn_tristate__from_word(const char * word);



/** About Special Files in Subversion
 *
 * Subversion denotes files that cannot be portably created or
 * modified as "special" files (svn_node_special).  It stores these
 * files in the repository as a plain text file with the svn:special
 * property set.  The file contents contain: a platform-specific type
 * string, a space character, then any information necessary to create
 * the file on a supported platform.  For example, if a symbolic link
 * were being represented, the repository file would have the
 * following contents:
 *
 * "link /path/to/link/target"
 *
 * Where 'link' is the identifier string showing that this special
 * file should be a symbolic link and '/path/to/link/target' is the
 * destination of the symbolic link.
 *
 * Special files are stored in the text-base exactly as they are
 * stored in the repository.  The platform specific files are created
 * in the working copy at EOL/keyword translation time using
 * svn_subst_copy_and_translate2().  If the current platform does not
 * support a specific special file type, the file is copied into the
 * working copy as it is seen in the repository.  Because of this,
 * users of other platforms can still view and modify the special
 * files, even if they do not have their unique properties.
 *
 * New types of special files can be added by:
 *  1. Implementing a platform-dependent routine to create a uniquely
 *     named special file and one to read the special file in
 *     libsvn_subr/io.c.
 *  2. Creating a new textual name similar to
 *     SVN_SUBST__SPECIAL_LINK_STR in libsvn_subr/subst.c.
 *  3. Handling the translation/detranslation case for the new type in
 *     create_special_file_from_stream and detranslate_special_file, using the
 *     routines from 1.
 */



/** A revision number. */
typedef long int svn_revnum_t;

/** Valid revision numbers begin at 0 */
#define SVN_IS_VALID_REVNUM(n) ((n) >= 0)

/** The 'official' invalid revision num */
#define SVN_INVALID_REVNUM ((svn_revnum_t) -1)

/** Not really invalid...just unimportant -- one day, this can be its
 * own unique value, for now, just make it the same as
 * #SVN_INVALID_REVNUM.
 */
#define SVN_IGNORED_REVNUM ((svn_revnum_t) -1)

/** Convert NULL-terminated C string @a str to a revision number. */
#define SVN_STR_TO_REV(str) ((svn_revnum_t) atol(str))

/**
 * Parse NULL-terminated C string @a str as a revision number and
 * store its value in @a rev.  If @a endptr is non-NULL, then the
 * address of the first non-numeric character in @a str is stored in
 * it.  If there are no digits in @a str, then @a endptr is set (if
 * non-NULL), and the error #SVN_ERR_REVNUM_PARSE_FAILURE error is
 * returned.  Negative numbers parsed from @a str are considered
 * invalid, and result in the same error.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_revnum_parse(svn_revnum_t *rev,
                 const char *str,
                 const char **endptr);

/** Originally intended to be used in printf()-style functions to format
 * revision numbers.  Deprecated due to incompatibilities with language
 * translation tools (e.g. gettext).
 *
 * New code should use a bare "%ld" format specifier for formatting revision
 * numbers.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
#define SVN_REVNUM_T_FMT "ld"



/** The size of a file in the Subversion FS. */
typedef apr_int64_t svn_filesize_t;

/** The 'official' invalid file size constant. */
#define SVN_INVALID_FILESIZE ((svn_filesize_t) -1)

/** In printf()-style functions, format file sizes using this. */
#define SVN_FILESIZE_T_FMT APR_INT64_T_FMT

#ifndef DOXYGEN_SHOULD_SKIP_THIS
/* Parse a base-10 numeric string into a 64-bit unsigned numeric value. */
/* NOTE: Private. For use by Subversion's own code only. See issue #1644. */
/* FIXME: APR should supply a function to do this, such as "apr_atoui64". */
#define svn__atoui64(X) ((apr_uint64_t) apr_atoi64(X))
#endif



/** An enum to indicate whether recursion is needed. */
enum svn_recurse_kind
{
  svn_nonrecursive = 1,
  svn_recursive
};

/** The concept of depth for directories.
 *
 * @note This is similar to, but not exactly the same as, the WebDAV
 * and LDAP concepts of depth.
 *
 * @since New in 1.5.
 */
typedef enum svn_depth_t
{
  /* The order of these depths is important: the higher the number,
     the deeper it descends.  This allows us to compare two depths
     numerically to decide which should govern. */

  /** Depth undetermined or ignored.  In some contexts, this means the
      client should choose an appropriate default depth.  The server
      will generally treat it as #svn_depth_infinity. */
  svn_depth_unknown    = -2,

  /** Exclude (i.e., don't descend into) directory D.
      @note In Subversion 1.5, svn_depth_exclude is *not* supported
      anywhere in the client-side (libsvn_wc/libsvn_client/etc) code;
      it is only supported as an argument to set_path functions in the
      ra and repos reporters.  (This will enable future versions of
      Subversion to run updates, etc, against 1.5 servers with proper
      svn_depth_exclude behavior, once we get a chance to implement
      client-side support for svn_depth_exclude.)
  */
  svn_depth_exclude    = -1,

  /** Just the named directory D, no entries.  Updates will not pull in
      any files or subdirectories not already present. */
  svn_depth_empty      =  0,

  /** D + its file children, but not subdirs.  Updates will pull in any
      files not already present, but not subdirectories. */
  svn_depth_files      =  1,

  /** D + immediate children (D and its entries).  Updates will pull in
      any files or subdirectories not already present; those
      subdirectories' this_dir entries will have depth-empty. */
  svn_depth_immediates =  2,

  /** D + all descendants (full recursion from D).  Updates will pull
      in any files or subdirectories not already present; those
      subdirectories' this_dir entries will have depth-infinity.
      Equivalent to the pre-1.5 default update behavior. */
  svn_depth_infinity   =  3

} svn_depth_t;

/** Return a constant string expressing @a depth as an English word,
 * e.g., "infinity", "immediates", etc.  The string is not localized,
 * as it may be used for client<->server communications.
 *
 * @since New in 1.5.
 */
const char *
svn_depth_to_word(svn_depth_t depth);

/** Return the appropriate depth for @a depth_str.  @a word is as
 * returned from svn_depth_to_word().  If @a depth_str does not
 * represent a recognized depth, return #svn_depth_unknown.
 *
 * @since New in 1.5.
 */
svn_depth_t
svn_depth_from_word(const char *word);

/** Return #svn_depth_infinity if boolean @a recurse is TRUE, else
 * return #svn_depth_files.
 *
 * @note New code should never need to use this, it is called only
 * from pre-depth APIs, for compatibility.
 *
 * @since New in 1.5.
 */
#define SVN_DEPTH_INFINITY_OR_FILES(recurse) \
  ((recurse) ? svn_depth_infinity : svn_depth_files)

/** Return #svn_depth_infinity if boolean @a recurse is TRUE, else
 * return #svn_depth_immediates.
 *
 * @note New code should never need to use this, it is called only
 * from pre-depth APIs, for compatibility.
 *
 * @since New in 1.5.
 */
#define SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse) \
  ((recurse) ? svn_depth_infinity : svn_depth_immediates)

/** Return #svn_depth_infinity if boolean @a recurse is TRUE, else
 * return #svn_depth_empty.
 *
 * @note New code should never need to use this, it is called only
 * from pre-depth APIs, for compatibility.
 *
 * @since New in 1.5.
 */
#define SVN_DEPTH_INFINITY_OR_EMPTY(recurse) \
  ((recurse) ? svn_depth_infinity : svn_depth_empty)

/** Return a recursion boolean based on @a depth.
 *
 * Although much code has been converted to use depth, some code still
 * takes a recurse boolean.  In most cases, it makes sense to treat
 * unknown or infinite depth as recursive, and any other depth as
 * non-recursive (which in turn usually translates to #svn_depth_files).
 */
#define SVN_DEPTH_IS_RECURSIVE(depth)                              \
  ((depth) == svn_depth_infinity || (depth) == svn_depth_unknown)



/**
 * It is sometimes convenient to indicate which parts of an #svn_dirent_t
 * object you are actually interested in, so that calculating and sending
 * the data corresponding to the other fields can be avoided.  These values
 * can be used for that purpose.
 *
 * @defgroup svn_dirent_fields Dirent fields
 * @{
 */

/** An indication that you are interested in the @c kind field */
#define SVN_DIRENT_KIND        0x00001

/** An indication that you are interested in the @c size field */
#define SVN_DIRENT_SIZE        0x00002

/** An indication that you are interested in the @c has_props field */
#define SVN_DIRENT_HAS_PROPS   0x00004

/** An indication that you are interested in the @c created_rev field */
#define SVN_DIRENT_CREATED_REV 0x00008

/** An indication that you are interested in the @c time field */
#define SVN_DIRENT_TIME        0x00010

/** An indication that you are interested in the @c last_author field */
#define SVN_DIRENT_LAST_AUTHOR 0x00020

/** A combination of all the dirent fields */
#define SVN_DIRENT_ALL ~((apr_uint32_t ) 0)

/** @} */

/** A general subversion directory entry.
 *
 * @note To allow for extending the #svn_dirent_t structure in future
 * releases, always use svn_dirent_create() to allocate the stucture.
 *
 * @since New in 1.6.
 */
typedef struct svn_dirent_t
{
  /** node kind */
  svn_node_kind_t kind;

  /** length of file text, otherwise SVN_INVALID_FILESIZE */
  svn_filesize_t size;

  /** does the node have props? */
  svn_boolean_t has_props;

  /** last rev in which this node changed */
  svn_revnum_t created_rev;

  /** time of created_rev (mod-time) */
  apr_time_t time;

  /** author of created_rev */
  const char *last_author;

  /* IMPORTANT: If you extend this struct, check svn_dirent_dup(). */
} svn_dirent_t;

/** Return a deep copy of @a dirent, allocated in @a pool.
 *
 * @since New in 1.4.
 */
svn_dirent_t *
svn_dirent_dup(const svn_dirent_t *dirent,
               apr_pool_t *pool);

/**
 * Create a new svn_dirent_t instance with all values initialized to their
 * not-available values.
 *
 * @since New in 1.8.
 */
svn_dirent_t *
svn_dirent_create(apr_pool_t *result_pool);


/** Keyword substitution.
 *
 * All the keywords Subversion recognizes.
 *
 * Note that there is a better, more general proposal out there, which
 * would take care of both internationalization issues and custom
 * keywords (e.g., $NetBSD$).  See
 *
 * @verbatim
      http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=8921
      =====
      From: "Jonathan M. Manning" <jmanning@alisa-jon.net>
      To: dev@subversion.tigris.org
      Date: Fri, 14 Dec 2001 11:56:54 -0500
      Message-ID: <87970000.1008349014@bdldevel.bl.bdx.com>
      Subject: Re: keywords @endverbatim
 *
 * and Eric Gillespie's support of same:
 *
 * @verbatim
      http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=8757
      =====
      From: "Eric Gillespie, Jr." <epg@pretzelnet.org>
      To: dev@subversion.tigris.org
      Date: Wed, 12 Dec 2001 09:48:42 -0500
      Message-ID: <87k7vsebp1.fsf@vger.pretzelnet.org>
      Subject: Re: Customizable Keywords @endverbatim
 *
 * However, it is considerably more complex than the scheme below.
 * For now we're going with simplicity, hopefully the more general
 * solution can be done post-1.0.
 *
 * @defgroup svn_types_keywords Keyword definitions
 * @{
 */

/** The maximum size of an expanded or un-expanded keyword. */
#define SVN_KEYWORD_MAX_LEN    255

/** The most recent revision in which this file was changed. */
#define SVN_KEYWORD_REVISION_LONG    "LastChangedRevision"

/** Short version of LastChangedRevision */
#define SVN_KEYWORD_REVISION_SHORT   "Rev"

/** Medium version of LastChangedRevision, matching the one CVS uses.
 * @since New in 1.1. */
#define SVN_KEYWORD_REVISION_MEDIUM  "Revision"

/** The most recent date (repository time) when this file was changed. */
#define SVN_KEYWORD_DATE_LONG        "LastChangedDate"

/** Short version of LastChangedDate */
#define SVN_KEYWORD_DATE_SHORT       "Date"

/** Who most recently committed to this file. */
#define SVN_KEYWORD_AUTHOR_LONG      "LastChangedBy"

/** Short version of LastChangedBy */
#define SVN_KEYWORD_AUTHOR_SHORT     "Author"

/** The URL for the head revision of this file. */
#define SVN_KEYWORD_URL_LONG         "HeadURL"

/** Short version of HeadURL */
#define SVN_KEYWORD_URL_SHORT        "URL"

/** A compressed combination of the other four keywords. */
#define SVN_KEYWORD_ID               "Id"

/** A full combination of the first four keywords.
 * @since New in 1.6. */
#define SVN_KEYWORD_HEADER           "Header"

/** @} */



/** All information about a commit.
 *
 * @note Objects of this type should always be created using the
 * svn_create_commit_info() function.
 *
 * @since New in 1.3.
 */
typedef struct svn_commit_info_t
{
  /** just-committed revision. */
  svn_revnum_t revision;

  /** server-side date of the commit. */
  const char *date;

  /** author of the commit. */
  const char *author;

  /** error message from post-commit hook, or NULL. */
  const char *post_commit_err;

  /** repository root, may be @c NULL if unknown.
      @since New in 1.7. */
  const char *repos_root;

} svn_commit_info_t;

/**
 * Allocate an object of type #svn_commit_info_t in @a pool and
 * return it.
 *
 * The @c revision field of the new struct is set to #SVN_INVALID_REVNUM.
 * All other fields are initialized to @c NULL.
 *
 * @note Any object of the type #svn_commit_info_t should
 * be created using this function.
 * This is to provide for extending the svn_commit_info_t in
 * the future.
 *
 * @since New in 1.3.
 */
svn_commit_info_t *
svn_create_commit_info(apr_pool_t *pool);

/**
 * Return a deep copy @a src_commit_info allocated in @a pool.
 *
 * @since New in 1.4.
 */
svn_commit_info_t *
svn_commit_info_dup(const svn_commit_info_t *src_commit_info,
                    apr_pool_t *pool);



/**
 * A structure to represent a path that changed for a log entry.
 *
 * @note To allow for extending the #svn_log_changed_path2_t structure in
 * future releases, always use svn_log_changed_path2_create() to allocate
 * the structure.
 *
 * @since New in 1.6.
 */
typedef struct svn_log_changed_path2_t
{
  /** 'A'dd, 'D'elete, 'R'eplace, 'M'odify */
  char action;

  /** Source path of copy (if any). */
  const char *copyfrom_path;

  /** Source revision of copy (if any). */
  svn_revnum_t copyfrom_rev;

  /** The type of the node, may be svn_node_unknown. */
  svn_node_kind_t node_kind;

  /** Is the text modified, may be svn_tristate_unknown.
   * @since New in 1.7. */
  svn_tristate_t text_modified;

  /** Are properties modified, may be svn_tristate_unknown.
   * @since New in 1.7. */
  svn_tristate_t props_modified;

  /* NOTE: Add new fields at the end to preserve binary compatibility.
     Also, if you add fields here, you have to update
     svn_log_changed_path2_dup(). */
} svn_log_changed_path2_t;

/**
 * Returns an #svn_log_changed_path2_t, allocated in @a pool with all fields
 * initialized to NULL, None or empty values.
 *
 * @note To allow for extending the #svn_log_changed_path2_t structure in
 * future releases, this function should always be used to allocate the
 * structure.
 *
 * @since New in 1.6.
 */
svn_log_changed_path2_t *
svn_log_changed_path2_create(apr_pool_t *pool);

/**
 * Return a deep copy of @a changed_path, allocated in @a pool.
 *
 * @since New in 1.6.
 */
svn_log_changed_path2_t *
svn_log_changed_path2_dup(const svn_log_changed_path2_t *changed_path,
                          apr_pool_t *pool);

/**
 * A structure to represent a path that changed for a log entry.  Same as
 * the first three fields of #svn_log_changed_path2_t.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
typedef struct svn_log_changed_path_t
{
  /** 'A'dd, 'D'elete, 'R'eplace, 'M'odify */
  char action;

  /** Source path of copy (if any). */
  const char *copyfrom_path;

  /** Source revision of copy (if any). */
  svn_revnum_t copyfrom_rev;

} svn_log_changed_path_t;

/**
 * Return a deep copy of @a changed_path, allocated in @a pool.
 *
 * @since New in 1.3.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_log_changed_path_t *
svn_log_changed_path_dup(const svn_log_changed_path_t *changed_path,
                         apr_pool_t *pool);

/**
 * A structure to represent all the information about a particular log entry.
 *
 * @note To allow for extending the #svn_log_entry_t structure in future
 * releases, always use svn_log_entry_create() to allocate the structure.
 *
 * @since New in 1.5.
 */
typedef struct svn_log_entry_t
{
  /** A hash containing as keys every path committed in @a revision; the
   * values are (#svn_log_changed_path_t *) structures.
   *
   * The subversion core libraries will always set this field to the same
   * value as changed_paths2 for compatibility reasons.
   *
   * @deprecated Provided for backward compatibility with the 1.5 API.
   */
  apr_hash_t *changed_paths;

  /** The revision of the commit. */
  svn_revnum_t revision;

  /** The hash of requested revision properties, which may be NULL if it
   * would contain no revprops.  Maps (const char *) property name to
   * (svn_string_t *) property value. */
  apr_hash_t *revprops;

  /**
   * Whether or not this message has children.
   *
   * When a log operation requests additional merge information, extra log
   * entries may be returned as a result of this entry.  The new entries, are
   * considered children of the original entry, and will follow it.  When
   * the HAS_CHILDREN flag is set, the receiver should increment its stack
   * depth, and wait until an entry is provided with SVN_INVALID_REVNUM which
   * indicates the end of the children.
   *
   * For log operations which do not request additional merge information, the
   * HAS_CHILDREN flag is always FALSE.
   *
   * For more information see:
   * https://svn.apache.org/repos/asf/subversion/trunk/notes/merge-tracking/design.html#commutative-reporting
   */
  svn_boolean_t has_children;

  /** A hash containing as keys every path committed in @a revision; the
   * values are (#svn_log_changed_path2_t *) structures.
   *
   * If this value is not @c NULL, it MUST have the same value as
   * changed_paths or svn_log_entry_dup() will not create an identical copy.
   *
   * The subversion core libraries will always set this field to the same
   * value as changed_paths for compatibility with users assuming an older
   * version.
   *
   * @note See http://svn.haxx.se/dev/archive-2010-08/0362.shtml for
   * further explanation.
   *
   * @since New in 1.6.
   */
  apr_hash_t *changed_paths2;

  /**
   * Whether @a revision should be interpreted as non-inheritable in the
   * same sense of #svn_merge_range_t.
   *
   * Only set when this #svn_log_entry_t instance is returned by the
   * libsvn_client mergeinfo apis. Currently always FALSE when the
   * #svn_log_entry_t instance is reported by the ra layer.
   *
   * @since New in 1.7.
   */
  svn_boolean_t non_inheritable;

  /**
   * Whether @a revision is a merged revision resulting from a reverse merge.
   *
   * @since New in 1.7.
   */
  svn_boolean_t subtractive_merge;

  /* NOTE: Add new fields at the end to preserve binary compatibility.
     Also, if you add fields here, you have to update
     svn_log_entry_dup(). */
} svn_log_entry_t;

/**
 * Returns an #svn_log_entry_t, allocated in @a pool with all fields
 * initialized to NULL values.
 *
 * @note To allow for extending the #svn_log_entry_t structure in future
 * releases, this function should always be used to allocate the structure.
 *
 * @since New in 1.5.
 */
svn_log_entry_t *
svn_log_entry_create(apr_pool_t *pool);

/** Return a deep copy of @a log_entry, allocated in @a pool.
 *
 * The resulting svn_log_entry_t has @c changed_paths set to the same
 * value as @c changed_path2. @c changed_paths will be @c NULL if
 * @c changed_paths2 was @c NULL.
 *
 * @since New in 1.6.
 */
svn_log_entry_t *
svn_log_entry_dup(const svn_log_entry_t *log_entry, apr_pool_t *pool);

/** The callback invoked by log message loopers, such as
 * #svn_ra_plugin_t.get_log() and svn_repos_get_logs().
 *
 * This function is invoked once on each log message, in the order
 * determined by the caller (see above-mentioned functions).
 *
 * @a baton is what you think it is, and @a log_entry contains relevant
 * information for the log message.  Any of @a log_entry->author,
 * @a log_entry->date, or @a log_entry->message may be @c NULL.
 *
 * If @a log_entry->date is neither NULL nor the empty string, it was
 * generated by svn_time_to_cstring() and can be converted to
 * @c apr_time_t with svn_time_from_cstring().
 *
 * If @a log_entry->changed_paths is non-@c NULL, then it contains as keys
 * every path committed in @a log_entry->revision; the values are
 * (#svn_log_changed_path_t *) structures.
 *
 * If @a log_entry->has_children is @c TRUE, the message will be followed
 * immediately by any number of merged revisions (child messages), which are
 * terminated by an invocation with SVN_INVALID_REVNUM.  This usage may
 * be recursive.
 *
 * Use @a pool for temporary allocation.  If the caller is iterating
 * over log messages, invoking this receiver on each, we recommend the
 * standard pool loop recipe: create a subpool, pass it as @a pool to
 * each call, clear it after each iteration, destroy it after the loop
 * is done.  (For allocation that must last beyond the lifetime of a
 * given receiver call, use a pool in @a baton.)
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_log_entry_receiver_t)(
  void *baton,
  svn_log_entry_t *log_entry,
  apr_pool_t *pool);

/**
 * Similar to #svn_log_entry_receiver_t, except this uses separate
 * parameters for each part of the log entry.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
typedef svn_error_t *(*svn_log_message_receiver_t)(
  void *baton,
  apr_hash_t *changed_paths,
  svn_revnum_t revision,
  const char *author,
  const char *date,  /* use svn_time_from_cstring() if need apr_time_t */
  const char *message,
  apr_pool_t *pool);


/** Callback function type for commits.
 *
 * When a commit succeeds, an instance of this is invoked with the
 * @a commit_info, along with the @a baton closure.
 * @a pool can be used for temporary allocations.
 *
 * @note Implementers of this callback that pass this callback to
 * svn_ra_get_commit_editor3() should be careful with returning errors
 * as these might be returned as commit errors. See the documentation
 * of svn_ra_get_commit_editor3() for more details.
 *
 * @since New in 1.4.
 */
typedef svn_error_t *(*svn_commit_callback2_t)(
  const svn_commit_info_t *commit_info,
  void *baton,
  apr_pool_t *pool);

/** Same as #svn_commit_callback2_t, but uses individual
 * data elements instead of the #svn_commit_info_t structure
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
typedef svn_error_t *(*svn_commit_callback_t)(
  svn_revnum_t new_revision,
  const char *date,
  const char *author,
  void *baton);



/** A buffer size that may be used when processing a stream of data.
 *
 * @note We don't use this constant any longer, since it is considered to be
 * unnecessarily large.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API.
 */
#define SVN_STREAM_CHUNK_SIZE 102400

#ifndef DOXYGEN_SHOULD_SKIP_THIS
/*
 * The maximum amount we (ideally) hold in memory at a time when
 * processing a stream of data.
 *
 * For example, when copying data from one stream to another, do it in
 * blocks of this size.
 *
 * NOTE: This is an internal macro, put here for convenience.
 * No public API may depend on the particular value of this macro.
 */
#define SVN__STREAM_CHUNK_SIZE 16384
#endif

/** The maximum amount we can ever hold in memory. */
/* FIXME: Should this be the same as SVN_STREAM_CHUNK_SIZE? */
#define SVN_MAX_OBJECT_SIZE (((apr_size_t) -1) / 2)



/* ### Note: despite being about mime-TYPES, these probably don't
 * ### belong in svn_types.h.  However, no other header is more
 * ### appropriate, and didn't feel like creating svn_validate.h for
 * ### so little.
 */

/** Validate @a mime_type.
 *
 * If @a mime_type does not contain a "/", or ends with non-alphanumeric
 * data, return #SVN_ERR_BAD_MIME_TYPE, else return success.
 *
 * Use @a pool only to find error allocation.
 *
 * Goal: to match both "foo/bar" and "foo/bar; charset=blah", without
 * being too strict about it, but to disallow mime types that have
 * quotes, newlines, or other garbage on the end, such as might be
 * unsafe in an HTTP header.
 */
svn_error_t *
svn_mime_type_validate(const char *mime_type,
                       apr_pool_t *pool);

/** Return FALSE iff @a mime_type is a textual type.
 *
 * All mime types that start with "text/" are textual, plus some special
 * cases (for example, "image/x-xbitmap").
 */
svn_boolean_t
svn_mime_type_is_binary(const char *mime_type);



/** A user defined callback that subversion will call with a user defined
 * baton to see if the current operation should be continued.  If the operation
 * should continue, the function should return #SVN_NO_ERROR, if not, it
 * should return #SVN_ERR_CANCELLED.
 */
typedef svn_error_t *(*svn_cancel_func_t)(void *cancel_baton);



/**
 * A lock object, for client & server to share.
 *
 * A lock represents the exclusive right to add, delete, or modify a
 * path.  A lock is created in a repository, wholly controlled by the
 * repository.  A "lock-token" is the lock's UUID, and can be used to
 * learn more about a lock's fields, and or/make use of the lock.
 * Because a lock is immutable, a client is free to not only cache the
 * lock-token, but the lock's fields too, for convenience.
 *
 * Note that the 'is_dav_comment' field is wholly ignored by every
 * library except for mod_dav_svn.  The field isn't even marshalled
 * over the network to the client.  Assuming lock structures are
 * created with apr_pcalloc(), a default value of 0 is universally safe.
 *
 * @note in the current implementation, only files are lockable.
 *
 * @since New in 1.2.
 */
typedef struct svn_lock_t
{
  const char *path;              /**< the path this lock applies to */
  const char *token;             /**< unique URI representing lock */
  const char *owner;             /**< the username which owns the lock */
  const char *comment;           /**< (optional) description of lock  */
  svn_boolean_t is_dav_comment;  /**< was comment made by generic DAV client? */
  apr_time_t creation_date;      /**< when lock was made */
  apr_time_t expiration_date;    /**< (optional) when lock will expire;
                                      If value is 0, lock will never expire. */
} svn_lock_t;

/**
 * Returns an #svn_lock_t, allocated in @a pool with all fields initialized
 * to NULL values.
 *
 * @note To allow for extending the #svn_lock_t structure in the future
 * releases, this function should always be used to allocate the structure.
 *
 * @since New in 1.2.
 */
svn_lock_t *
svn_lock_create(apr_pool_t *pool);

/**
 * Return a deep copy of @a lock, allocated in @a pool.
 *
 * @since New in 1.2.
 */
svn_lock_t *
svn_lock_dup(const svn_lock_t *lock, apr_pool_t *pool);



/**
 * Return a formatted Universal Unique IDentifier (UUID) string.
 *
 * @since New in 1.4.
 */
const char *
svn_uuid_generate(apr_pool_t *pool);



/**
 * Mergeinfo representing a merge of a range of revisions.
 *
 * @since New in 1.5
 */
typedef struct svn_merge_range_t
{
  /**
   * If the 'start' field is less than the 'end' field then 'start' is
   * exclusive and 'end' inclusive of the range described.  This is termed
   * a forward merge range.  If 'start' is greater than 'end' then the
   * opposite is true.  This is termed a reverse merge range.  If 'start'
   * equals 'end' the meaning of the range is not defined.
   */
  svn_revnum_t start;
  svn_revnum_t end;

  /**
   * Whether this merge range should be inherited by treewise
   * descendants of the path to which the range applies. */
  svn_boolean_t inheritable;
} svn_merge_range_t;

/**
 * Return a copy of @a range, allocated in @a pool.
 *
 * @since New in 1.5.
 */
svn_merge_range_t *
svn_merge_range_dup(const svn_merge_range_t *range, apr_pool_t *pool);

/**
 * Returns true if the changeset committed in revision @a rev is one
 * of the changesets in the range @a range.
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_merge_range_contains_rev(const svn_merge_range_t *range, svn_revnum_t rev);



/** @defgroup node_location_seg_reporting Node location segment reporting.
 *  @{ */

/**
 * A representation of a segment of an object's version history with an
 * emphasis on the object's location in the repository as of various
 * revisions.
 *
 * @since New in 1.5.
 */
typedef struct svn_location_segment_t
{
  /** The beginning (oldest) and ending (youngest) revisions for this
      segment, both inclusive. */
  svn_revnum_t range_start;
  svn_revnum_t range_end;

  /** The absolute (sans leading slash) path for this segment.  May be
      NULL to indicate gaps in an object's history.  */
  const char *path;

} svn_location_segment_t;

/**
 * A callback invoked by generators of #svn_location_segment_t
 * objects, used to report information about a versioned object's
 * history in terms of its location in the repository filesystem over
 * time.
 */
typedef svn_error_t *(*svn_location_segment_receiver_t)(
  svn_location_segment_t *segment,
  void *baton,
  apr_pool_t *pool);

/**
 * Return a deep copy of @a segment, allocated in @a pool.
 *
 * @since New in 1.5.
 */
svn_location_segment_t *
svn_location_segment_dup(const svn_location_segment_t *segment,
                         apr_pool_t *pool);

/** @} */



/** A line number, such as in a file or a stream.
 *
 * @since New in 1.7.
 */
typedef unsigned long svn_linenum_t;

/** The maximum value of an svn_linenum_t.
 *
 * @since New in 1.7.
 */
#define SVN_LINENUM_MAX_VALUE ULONG_MAX



#ifdef __cplusplus
}
#endif /* __cplusplus */


/*
 * Everybody and their brother needs to deal with svn_error_t, the error
 * codes, and whatever else. While they *should* go and include svn_error.h
 * in order to do that... bah. Let's just help everybody out and include
 * that header whenever somebody grabs svn_types.h.
 *
 * Note that we do this at the END of this header so that its contents
 * are available to svn_error.h (our guards will prevent the circular
 * include). We also need to do the include *outside* of the cplusplus
 * guard.
 */
#include "svn_error.h"


/*
 * Subversion developers may want to use some additional debugging facilities
 * while working on the code. We'll pull that in here, so individual source
 * files don't have to include this header manually.
 */
#ifdef SVN_DEBUG
#include "private/svn_debug.h"
#endif


#endif /* SVN_TYPES_H */
