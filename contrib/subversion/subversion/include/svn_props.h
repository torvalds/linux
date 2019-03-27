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
 * @file svn_props.h
 * @brief Subversion properties
 */

/* ==================================================================== */

#ifndef SVN_PROPS_H
#define SVN_PROPS_H

#include <apr_pools.h>   /* for apr_pool_t */
#include <apr_tables.h>  /* for apr_array_header_t */
#include <apr_hash.h>    /* for apr_hash_t */

#include "svn_types.h"   /* for svn_boolean_t, svn_error_t */
#include "svn_string.h"  /* for svn_string_t */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup svn_props_support Properties management utilities
 * @{
 */



/** A general in-memory representation of a single property.  Most of
 * the time, property lists will be stored completely in hashes.  But
 * sometimes it's useful to have an "ordered" collection of
 * properties, in which case we use an array of these structures.
 *
 * Also: sometimes we want a list that represents a set of property
 * *changes*, and in this case, an @c apr_hash_t won't work -- there's no
 * way to represent a property deletion, because we can't store a @c NULL
 * value in a hash.  So instead, we use these structures.
 */
typedef struct svn_prop_t
{
  const char *name;           /**< Property name */
  const svn_string_t *value;  /**< Property value */
} svn_prop_t;


/**
 * Return a duplicate of @a prop, allocated in @a pool. No part of the new
 * structure will be shared with @a prop.
 *
 * @since New in 1.3.
 */
svn_prop_t *
svn_prop_dup(const svn_prop_t *prop,
             apr_pool_t *pool);


/**
 * Duplicate an @a array of svn_prop_t items using @a pool.
 *
 * @since New in 1.3.
 */
apr_array_header_t *
svn_prop_array_dup(const apr_array_header_t *array,
                   apr_pool_t *pool);


/** A structure to represent inherited properties.
 *
 * @since New in 1.8.
 */
typedef struct svn_prop_inherited_item_t
{
  /** The absolute working copy path, relative filesystem path, or URL
   * from which the properties in @a prop_hash are inherited.  (For
   * details about which path specification format is in use for a
   * particular instance of this structure, consult the documentation
   * for the API which produced it.) */
  const char *path_or_url;

  /** A hash of (<tt>const char *</tt>) inherited property names, and
   * (<tt>svn_string_t *</tt>) property values. */
  apr_hash_t *prop_hash;

} svn_prop_inherited_item_t;


/**
 * Given a hash (keys <tt>const char *</tt> and values <tt>const
 * svn_string_t</tt>) of properties, returns an array of svn_prop_t
 * items using @a pool.
 *
 * @since New in 1.5.
 */
apr_array_header_t *
svn_prop_hash_to_array(const apr_hash_t *hash,
                       apr_pool_t *pool);

/**
 * Given an array of svn_prop_t items, return a hash mapping const char *
 * property names to const svn_string_t * values.
 *
 * @warning The behaviour on #svn_prop_t objects with a @c NULL @c
 * svn_prop_t.value member is undefined.
 *
 * @since New in 1.7.
 */
apr_hash_t *
svn_prop_array_to_hash(const apr_array_header_t *properties,
                       apr_pool_t *result);

/**
 * Creates a deep copy of @a hash (keys <tt>const char *</tt> and
 * values <tt>const svn_string_t *</tt>) in @a pool.
 *
 * @since New in 1.6.
 */
apr_hash_t *
svn_prop_hash_dup(const apr_hash_t *hash,
                  apr_pool_t *pool);

/**
 * Return the value of property @a prop_name as it is in @a properties,
 * with values <tt>const svn_string_t</tt>. If @a prop_name is not
 * in @a properties or @a properties is NULL, return NULL.
 *
 * @since New in 1.7.
 */
const char *
svn_prop_get_value(const apr_hash_t *properties,
                   const char *prop_name);

/**
 * Subversion distinguishes among several kinds of properties,
 * particularly on the client-side.  There is no "unknown" kind; if
 * there's nothing special about a property name, the default category
 * is @c svn_prop_regular_kind.
 */
typedef enum svn_prop_kind
{
  /** In .svn/entries, i.e., author, date, etc. */
  svn_prop_entry_kind,

  /** Client-side only, stored by specific RA layer. */
  svn_prop_wc_kind,

  /** Seen if user does "svn proplist"; note that this includes some "svn:"
   * props and all user props, i.e. ones stored in the repository fs.
   */
  svn_prop_regular_kind
} svn_prop_kind_t;

/** Return the property kind of a property named @a prop_name.
 *
 * @since New in 1.8.
 */
svn_prop_kind_t
svn_property_kind2(const char *prop_name);

/** Return the prop kind of a property named @a prop_name, and
 * (if @a prefix_len is non-@c NULL) set @a *prefix_len to the length of
 * the prefix of @a prop_name that was sufficient to distinguish its kind.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_prop_kind_t
svn_property_kind(int *prefix_len,
                  const char *prop_name);


/** Return @c TRUE iff @a prop_name represents the name of a Subversion
 * property.  That is, any property name in Subversion's name space for
 * versioned or unversioned properties, regardless whether the particular
 * property name is recognized.
 */
svn_boolean_t
svn_prop_is_svn_prop(const char *prop_name);


/** Return @c TRUE iff @a props has at least one property whose name
 * represents the name of a Subversion property, in the sense of
 * svn_prop_is_svn_prop().
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_prop_has_svn_prop(const apr_hash_t *props,
                      apr_pool_t *pool);

/** Return @c TRUE iff @a prop_name is a Subversion property whose
 * value is interpreted as a boolean.
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_prop_is_boolean(const char *prop_name);

/** Return @c TRUE iff @a prop_name is in the "svn:" name space and is a
 * known revision property ("svn:log" or "svn:date", e.g.).
 *
 * This will return @c FALSE for any property name that is not known by this
 * version of the library, even though the name may be known to other (for
 * example, later) Subversion software.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_prop_is_known_svn_rev_prop(const char *prop_name);

/** Return @c TRUE iff @a prop_name is in the "svn:" name space and is a
 * known versioned property that is allowed on a file and/or on a
 * directory ("svn:eol-style", "svn:ignore", or "svn:mergeinfo", e.g.).
 *
 * This will return @c FALSE for any property name that is not known
 * by this version of the library, even though the name may be known
 * to other (for example, later) Subversion software.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_prop_is_known_svn_node_prop(const char *prop_name);

/** Return @c TRUE iff @a prop_name is in the "svn:" name space and is
 * a known versioned property that is allowed on a file
 * ("svn:eol-style" or "svn:mergeinfo", e.g.).
 *
 * This will return @c FALSE for any property name that is not known
 * by this version of the library, even though the name may be known
 * to other (for example, later) Subversion software.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_prop_is_known_svn_file_prop(const char *prop_name);

/** Return @c TRUE iff @a prop_name is in the "svn:" name space and is
 * a known versioned property that is allowed on a directory
 * ("svn:ignore" or "svn:mergeinfo", e.g.).
 *
 * This will return @c FALSE for any property name that is not known
 * by this version of the library, even though the name may be known
 * to other (for example, later) Subversion software.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_prop_is_known_svn_dir_prop(const char *prop_name);

/** If @a prop_name requires that its value be stored as UTF8/LF in the
 * repository, then return @c TRUE.  Else return @c FALSE.  This is for
 * users of libsvn_client or libsvn_fs, since it their responsibility
 * to do this translation in both directions.  (See
 * svn_subst_translate_string()/svn_subst_detranslate_string() for
 * help with this task.)
 */
svn_boolean_t
svn_prop_needs_translation(const char *prop_name);


/** Given a @a proplist array of @c svn_prop_t structures, allocate
 * three new arrays in @a pool.  Categorize each property and then
 * create new @c svn_prop_t structures in the proper lists.  Each new
 * @c svn_prop_t structure's fields will point to the same data within
 * @a proplist's structures.
 *
 * Callers may pass NULL for each of the property lists in which they
 * are uninterested.  If no props exist in a certain category, and the
 * property list argument for that category is non-NULL, then that
 * array will come back with <tt>->nelts == 0</tt>.
 */
svn_error_t *
svn_categorize_props(const apr_array_header_t *proplist,
                     apr_array_header_t **entry_props,
                     apr_array_header_t **wc_props,
                     apr_array_header_t **regular_props,
                     apr_pool_t *pool);


/** Given two property hashes (<tt>const char *name</tt> -> <tt>const
 * svn_string_t *value</tt>), deduce the differences between them (from
 * @a source_props -> @c target_props).  Set @a propdiffs to a new array of
 * @c svn_prop_t structures, with one entry for each property that differs,
 * including properties that exist in @a source_props or @a target_props but
 * not both. The @c value field of each entry is that property's value from
 * @a target_props or NULL if that property only exists in @a source_props.
 *
 * Allocate the array from @a pool. Allocate the contents of the array from
 * @a pool or by reference to the storage of the input hashes or both.
 *
 * For note, here's a quick little table describing the logic of this
 * routine:
 *
 * @verbatim
   source_props    target_props      event
   ------------    ------------      -----
   value = foo     value = NULL      Deletion occurred.
   value = foo     value = bar       Set occurred (modification)
   value = NULL    value = baz       Set occurred (creation) @endverbatim
 */
svn_error_t *
svn_prop_diffs(apr_array_header_t **propdiffs,
               const apr_hash_t *target_props,
               const apr_hash_t *source_props,
               apr_pool_t *pool);


/**
 * Return @c TRUE iff @a prop_name is a valid property name.
 *
 * For now, "valid" means the ASCII subset of an XML "Name".
 * XML "Name" is defined at http://www.w3.org/TR/REC-xml#sec-common-syn
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_prop_name_is_valid(const char *prop_name);



/* Defines for reserved ("svn:") property names.  */

/** All Subversion property names start with this. */
#define SVN_PROP_PREFIX "svn:"


/** Visible properties
 *
 * These are regular properties that are attached to ordinary files
 * and dirs, and are visible (and tweakable) by svn client programs
 * and users.  Adding these properties causes specific effects.
 *
 * @note the values of these properties are always UTF8-encoded with
 * LF line-endings.  It is the burden of svn library users to enforce
 * this.  Use svn_prop_needs_translation() to discover if a
 * certain property needs translation, and you can use
 * svn_subst_translate_string()/svn_subst_detranslate_string()
 * to do the translation.
 *
 * @defgroup svn_prop_visible_props Visible properties
 * @{
 */

/** Properties whose values are interpreted as booleans (such as
 * svn:executable, svn:needs_lock, and svn:special) always fold their
 * value to this.
 *
 * @since New in 1.5.
 */
#define SVN_PROP_BOOLEAN_TRUE "*"

/** The mime-type of a given file. */
#define SVN_PROP_MIME_TYPE  SVN_PROP_PREFIX "mime-type"

/** The ignore patterns for a given directory. */
#define SVN_PROP_IGNORE  SVN_PROP_PREFIX "ignore"

/** The line ending style for a given file. */
#define SVN_PROP_EOL_STYLE  SVN_PROP_PREFIX "eol-style"

/** The "activated" keywords (for keyword substitution) for a given file. */
#define SVN_PROP_KEYWORDS  SVN_PROP_PREFIX "keywords"

/** Set to either TRUE or FALSE if we want a file to be executable or not. */
#define SVN_PROP_EXECUTABLE  SVN_PROP_PREFIX "executable"

/** The value to force the executable property to when set.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 * Use @c SVN_PROP_BOOLEAN_TRUE instead.
 */
#define SVN_PROP_EXECUTABLE_VALUE SVN_PROP_BOOLEAN_TRUE

/** Set to TRUE ('*') if we want a file to be set to read-only when
 * not locked.  FALSE is indicated by deleting the property. */
#define SVN_PROP_NEEDS_LOCK  SVN_PROP_PREFIX "needs-lock"

/** The value to force the needs-lock property to when set.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 * Use @c SVN_PROP_BOOLEAN_TRUE instead.
 */
#define SVN_PROP_NEEDS_LOCK_VALUE SVN_PROP_BOOLEAN_TRUE

/** Set if the file should be treated as a special file. */
#define SVN_PROP_SPECIAL  SVN_PROP_PREFIX "special"

/** The value to force the special property to when set.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 * Use @c SVN_PROP_BOOLEAN_TRUE instead.
 */
#define SVN_PROP_SPECIAL_VALUE SVN_PROP_BOOLEAN_TRUE

/** Describes external items to check out into this directory.
 *
 * The format is a series of lines, each in the following format:
 *   [-r REV] URL[@PEG] LOCALPATH
 * LOCALPATH is relative to the directory having this property.
 * REV pins the external to revision REV.
 * URL may be a full URL or a relative URL starting with one of:
 *   ../  to the parent directory of the extracted external
 *   ^/   to the repository root
 *   /    to the server root
 *   //   to the URL scheme
 * The following format is supported for interoperability with
 * Subversion 1.4 and earlier clients:
 *   LOCALPATH [-r PEG] URL
 * The ambiguous format 'relative_path relative_path' is taken as
 * 'relative_url relative_path' with peg revision support.
 * Lines starting with a '#' character are ignored.
 */
#define SVN_PROP_EXTERNALS  SVN_PROP_PREFIX "externals"

/** Merge info property used to record a resource's merge history.
 *
 * The format is a series of lines containing merge paths and revision
 * ranges, such as:
 *
 * @verbatim
     /trunk: 1-6,9,37-38
     /trunk/foo: 10 @endverbatim
 * @since New in 1.5.
 */
#define SVN_PROP_MERGEINFO SVN_PROP_PREFIX "mergeinfo"

/** Property used to record inheritable configuration auto-props.
 *
 * @since New in 1.8.
 */
#define SVN_PROP_INHERITABLE_AUTO_PROPS SVN_PROP_PREFIX "auto-props"

/** Property used to record inheritable configuration ignores.
 *
 * @since New in 1.8.
 */
#define SVN_PROP_INHERITABLE_IGNORES SVN_PROP_PREFIX "global-ignores"

/** Meta-data properties.
 *
 * The following properties are used for storing meta-data about
 * individual entries in the meta-data branches of subversion,
 * see issue #1256 or browseable at
 * http://svn.apache.org/viewvc/subversion/branches/meta-data-versioning/ .
 * Furthermore @c svntar (http://svn.borg.ch/svntar/) and @c FSVS
 * (http://fsvs.tigris.org/) use these, too.
 *
 * Please note that these formats are very UNIX-centric currently;
 * a bit of discussion about Windows can be read at
 * http://article.gmane.org/gmane.comp.version-control.subversion.devel/103991
 *
 * @defgroup svn_prop_meta_data Meta-data properties
 * @{ */

/** The files' last modification time.
 * This is stored as string in the form @c "2008-08-07T07:38:51.008782Z", to
 * be converted by the functions @c svn_time_to_cstring() and
 * @c svn_time_from_cstring().
 *
 * @note This property name is reserved for future usage, but currently unused.
 *
 * @since New in 1.6.
 */
#define SVN_PROP_TEXT_TIME  SVN_PROP_PREFIX "text-time"

/** The files' owner.
 * Stored as numeric ID, optionally followed by whitespace and the string:
 * @c "1000 pmarek". Parsers @b should accept any number of whitespace,
 * and writers @b should put exactly a single space.
 *
 * @note This property name is reserved for future usage, but currently unused.
 *
 * @since New in 1.6.
 */
#define SVN_PROP_OWNER SVN_PROP_PREFIX "owner"

/** The files' group.
 * The same format as for @c SVN_PROP_OWNER, the owner-property.
 *
 * @note This property name is reserved for future usage, but currently unused.
 *
 * @since New in 1.6.
 */
#define SVN_PROP_GROUP  SVN_PROP_PREFIX "group"

/** The files' unix-mode.
 * Stored in octal, with a leading @c 0; may have 5 digits if any of @c setuid,
 * @c setgid or @c sticky are set; an example is @c "0644".
 *
 * @note This property name is reserved for future usage, but currently unused.
 *
 * @since New in 1.6.
 */
#define SVN_PROP_UNIX_MODE  SVN_PROP_PREFIX "unix-mode"

/** @} */ /* Meta-data properties */

/**
 * This is a list of all user-visible and -settable versioned node
 * properties.
 *
 * @since New in 1.8.
 */
#define SVN_PROP_NODE_ALL_PROPS SVN_PROP_MIME_TYPE, \
                                SVN_PROP_IGNORE, \
                                SVN_PROP_EOL_STYLE, \
                                SVN_PROP_KEYWORDS, \
                                SVN_PROP_EXECUTABLE, \
                                SVN_PROP_NEEDS_LOCK, \
                                SVN_PROP_SPECIAL, \
                                SVN_PROP_EXTERNALS, \
                                SVN_PROP_MERGEINFO, \
                                SVN_PROP_INHERITABLE_AUTO_PROPS, \
                                SVN_PROP_INHERITABLE_IGNORES, \
                                \
                                SVN_PROP_TEXT_TIME, \
                                SVN_PROP_OWNER, \
                                SVN_PROP_GROUP, \
                                SVN_PROP_UNIX_MODE,

/** @} */

/** WC props are props that are invisible to users:  they're generated
 * by an RA layer, and stored in secret parts of .svn/.
 *
 * @defgroup svn_prop_invisible_props Invisible properties
 * @{
 */

/** The property name *prefix* that makes a property a "WC property".
 *
 * For example, WebDAV RA implementations might store a versioned-resource
 * url as a WC prop like this:
 *
 * <pre reason="Should use 'verbatim' instead, but Doxygen v1.6.1 & v1.7.1
 *              then doesn't recognize the #define; presumably a bug.">
      name = svn:wc:dav_url
      val  = http://www.example.com/repos/452348/e.289 </pre>
 *
 * The client will try to protect WC props by warning users against
 * changing them.  The client will also send them back to the RA layer
 * when committing.
 */
#define SVN_PROP_WC_PREFIX     SVN_PROP_PREFIX "wc:"

/** Another type of non-user-visible property.  "Entry properties" are
 * stored as fields with the administrative 'entries' file.
 */
#define SVN_PROP_ENTRY_PREFIX  SVN_PROP_PREFIX "entry:"

/** The revision this entry was last committed to on. */
#define SVN_PROP_ENTRY_COMMITTED_REV     SVN_PROP_ENTRY_PREFIX "committed-rev"

/** The date this entry was last committed to on. */
#define SVN_PROP_ENTRY_COMMITTED_DATE    SVN_PROP_ENTRY_PREFIX "committed-date"

/** The author who last committed to this entry. */
#define SVN_PROP_ENTRY_LAST_AUTHOR       SVN_PROP_ENTRY_PREFIX "last-author"

/** The UUID of this entry's repository. */
#define SVN_PROP_ENTRY_UUID       SVN_PROP_ENTRY_PREFIX "uuid"

/** The lock token for this entry.
 * @since New in 1.2. */
#define SVN_PROP_ENTRY_LOCK_TOKEN SVN_PROP_ENTRY_PREFIX "lock-token"

/** When custom, user-defined properties are passed over the wire, they will
 * have this prefix added to their name.
 */
#define SVN_PROP_CUSTOM_PREFIX SVN_PROP_PREFIX "custom:"

/** @} */

/**
 * These are reserved properties attached to a "revision" object in
 * the repository filesystem.  They can be queried by using
 * svn_fs_revision_prop().
 *
 * @defgroup svn_props_revision_props Revision properties
 * @{
 */

/** The fs revision property that stores a commit's author. */
#define SVN_PROP_REVISION_AUTHOR  SVN_PROP_PREFIX "author"

/** The fs revision property that stores a commit's log message. */
#define SVN_PROP_REVISION_LOG  SVN_PROP_PREFIX "log"

/** The fs revision property that stores a commit's date. */
#define SVN_PROP_REVISION_DATE  SVN_PROP_PREFIX "date"

/** The fs revision property that stores a commit's "original" date.
 *
 * The svn:date property must be monotonically increasing, along with
 * the revision number. In certain scenarios, this may pose a problem
 * when the revision represents a commit that occurred at a time which
 * does not fit within the sequencing required for svn:date. This can
 * happen, for instance, when the revision represents a commit to a
 * foreign version control system, or possibly when two Subversion
 * repositories are combined. This property can be used to record the
 * TRUE, original date of the commit.
 */
#define SVN_PROP_REVISION_ORIG_DATE  SVN_PROP_PREFIX "original-date"

/** The presence of this fs revision property indicates that the
 * revision was automatically generated by the mod_dav_svn
 * autoversioning feature.  The value is irrelevant.
 */
#define SVN_PROP_REVISION_AUTOVERSIONED  SVN_PROP_PREFIX "autoversioned"


/* More reserved revision props in the 'svn:' namespace, used by the
   svnsync tool:   */

/** Prefix for all svnsync custom properties.
 * @since New in 1.4.
 */
#define SVNSYNC_PROP_PREFIX             SVN_PROP_PREFIX "sync-"

/* The following revision properties are set on revision 0 of
 * destination repositories by svnsync:
 */

/** Used to enforce mutually exclusive destination repository access.
 * @since New in 1.4.
 */
#define SVNSYNC_PROP_LOCK               SVNSYNC_PROP_PREFIX "lock"

/** Identifies the repository's source URL.
 * @since New in 1.4.
 */
#define SVNSYNC_PROP_FROM_URL           SVNSYNC_PROP_PREFIX "from-url"
/** Identifies the repository's source UUID.
 * @since New in 1.4.
 */
#define SVNSYNC_PROP_FROM_UUID          SVNSYNC_PROP_PREFIX "from-uuid"

/** Identifies the last completely mirrored revision.
 * @since New in 1.4.
 */
#define SVNSYNC_PROP_LAST_MERGED_REV    SVNSYNC_PROP_PREFIX "last-merged-rev"

/** Identifies the revision currently being copied.
 * @since New in 1.4.
 */
#define SVNSYNC_PROP_CURRENTLY_COPYING  SVNSYNC_PROP_PREFIX "currently-copying"


/**
 * This is a list of all revision properties.
 */
#define SVN_PROP_REVISION_ALL_PROPS SVN_PROP_REVISION_AUTHOR, \
                                    SVN_PROP_REVISION_LOG, \
                                    SVN_PROP_REVISION_DATE, \
                                    SVN_PROP_REVISION_AUTOVERSIONED, \
                                    SVN_PROP_REVISION_ORIG_DATE, \
                                    SVNSYNC_PROP_LOCK, \
                                    SVNSYNC_PROP_FROM_URL, \
                                    SVNSYNC_PROP_FROM_UUID, \
                                    SVNSYNC_PROP_LAST_MERGED_REV, \
                                    SVNSYNC_PROP_CURRENTLY_COPYING,

/** @} */

/**
 * These are reserved properties attached to a "transaction" object in
 * the repository filesystem in advance of the pre-commit hook script
 * running on the server, but then automatically removed from the
 * transaction before its promotion to a new revision.
 *
 * @defgroup svn_props_ephemeral_txnprops Ephemeral transaction properties
 * @{
 */

/** The prefix used for all (ephemeral) transaction properties.
 *
 * @since New in 1.8.
 */
#define SVN_PROP_TXN_PREFIX  SVN_PROP_PREFIX "txn-"

/** Identifies the client version compatibility level.  For clients
 * compiled against Subversion libraries, this is @c SVN_VER_NUMBER.
 * Third-party implementations are advised to use similar formatting
 * for values of this property.
 *
 * @since New in 1.8.
 */
#define SVN_PROP_TXN_CLIENT_COMPAT_VERSION \
            SVN_PROP_TXN_PREFIX "client-compat-version"

/** Identifies the client's user agent string, if any.
 *
 * @since New in 1.8.
 */
#define SVN_PROP_TXN_USER_AGENT \
            SVN_PROP_TXN_PREFIX "user-agent"

/** The prefix reserved for copies of (ephemeral) transaction
 * properties designed to outlive the transaction.  Administrators may
 * choose to, in their pre-commit hook scripts, copy the values of one
 * or more properties named @c SVN_PROP_TXN_PREFIX + "something"
 * to new properties named @c SVN_PROP_REVISION_PREFIX + "something",
 * allowing that information to survive the commit-time removal of
 * ephemeral transaction properties.
 *
 * @since New in 1.8.
 */
#define SVN_PROP_REVISION_PREFIX  SVN_PROP_PREFIX "revision-"


/** @} */

/** @} */



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_PROPS_H */
