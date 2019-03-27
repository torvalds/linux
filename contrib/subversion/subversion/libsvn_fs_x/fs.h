/* fs.h : interface to Subversion filesystem
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

#ifndef SVN_LIBSVN_FS_X_FS_H
#define SVN_LIBSVN_FS_X_FS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_network_io.h>
#include <apr_md5.h>
#include <apr_sha1.h>

#include "svn_fs.h"
#include "svn_config.h"
#include "private/svn_atomic.h"
#include "private/svn_cache.h"
#include "private/svn_fs_private.h"
#include "private/svn_sqlite.h"
#include "private/svn_mutex.h"

#include "rev_file.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** The filesystem structure.  ***/

/* Following are defines that specify the textual elements of the
   native filesystem directories and revision files. */

/* Names of special files in the fs_x filesystem. */
#define PATH_FORMAT           "format"           /* Contains format number */
#define PATH_UUID             "uuid"             /* Contains UUID */
#define PATH_CURRENT          "current"          /* Youngest revision */
#define PATH_NEXT             "next"             /* Revision begin written. */
#define PATH_LOCK_FILE        "write-lock"       /* Revision lock file */
#define PATH_PACK_LOCK_FILE   "pack-lock"        /* Pack lock file */
#define PATH_REVS_DIR         "revs"             /* Directory of revisions */
#define PATH_TXNS_DIR         "transactions"     /* Directory of transactions */
#define PATH_TXN_PROTOS_DIR   "txn-protorevs"    /* Directory of proto-revs */
#define PATH_TXN_CURRENT      "txn-current"      /* File with next txn key */
#define PATH_TXN_CURRENT_LOCK "txn-current-lock" /* Lock for txn-current */
#define PATH_LOCKS_DIR        "locks"            /* Directory of locks */
#define PATH_MIN_UNPACKED_REV "min-unpacked-rev" /* Oldest revision which
                                                    has not been packed. */
#define PATH_REVPROP_GENERATION "revprop-generation"
                                                 /* Current revprop generation*/
#define PATH_MANIFEST         "manifest"         /* Manifest file name */
#define PATH_PACKED           "pack"             /* Packed revision data file */
#define PATH_EXT_PACKED_SHARD ".pack"            /* Extension for packed
                                                    shards */
#define PATH_EXT_L2P_INDEX    ".l2p"             /* extension of the log-
                                                    to-phys index */
#define PATH_EXT_P2L_INDEX    ".p2l"             /* extension of the phys-
                                                    to-log index */
/* If you change this, look at tests/svn_test_fs.c(maybe_install_fsx_conf) */
#define PATH_CONFIG           "fsx.conf"         /* Configuration */

/* Names of special files and file extensions for transactions */
#define PATH_CHANGES       "changes"       /* Records changes made so far */
#define PATH_TXN_PROPS     "props"         /* Transaction properties */
#define PATH_NEXT_IDS      "next-ids"      /* Next temporary ID assignments */
#define PATH_PREFIX_NODE   "node."         /* Prefix for node filename */
#define PATH_EXT_TXN       ".txn"          /* Extension of txn dir */
#define PATH_EXT_CHILDREN  ".children"     /* Extension for dir contents */
#define PATH_EXT_PROPS     ".props"        /* Extension for node props */
#define PATH_EXT_REV       ".rev"          /* Extension of protorev file */
#define PATH_EXT_REV_LOCK  ".rev-lock"     /* Extension of protorev lock file */
#define PATH_TXN_ITEM_INDEX "itemidx"      /* File containing the current item
                                             index number */
#define PATH_INDEX          "index"        /* name of index files w/o ext */

/* Names of files in legacy FS formats */
#define PATH_REV           "rev"           /* Proto rev file */
#define PATH_REV_LOCK      "rev-lock"      /* Proto rev (write) lock file */

/* Names of sections and options in fsx.conf. */
#define CONFIG_SECTION_CACHES            "caches"
#define CONFIG_OPTION_FAIL_STOP          "fail-stop"
#define CONFIG_SECTION_REP_SHARING       "rep-sharing"
#define CONFIG_OPTION_ENABLE_REP_SHARING "enable-rep-sharing"
#define CONFIG_SECTION_DELTIFICATION     "deltification"
#define CONFIG_OPTION_MAX_DELTIFICATION_WALK     "max-deltification-walk"
#define CONFIG_OPTION_MAX_LINEAR_DELTIFICATION   "max-linear-deltification"
#define CONFIG_OPTION_COMPRESSION_LEVEL  "compression-level"
#define CONFIG_SECTION_PACKED_REVPROPS   "packed-revprops"
#define CONFIG_OPTION_REVPROP_PACK_SIZE  "revprop-pack-size"
#define CONFIG_OPTION_COMPRESS_PACKED_REVPROPS  "compress-packed-revprops"
#define CONFIG_SECTION_IO                "io"
#define CONFIG_OPTION_BLOCK_SIZE         "block-size"
#define CONFIG_OPTION_L2P_PAGE_SIZE      "l2p-page-size"
#define CONFIG_OPTION_P2L_PAGE_SIZE      "p2l-page-size"
#define CONFIG_SECTION_DEBUG             "debug"
#define CONFIG_OPTION_PACK_AFTER_COMMIT  "pack-after-commit"

/* The format number of this filesystem.
   This is independent of the repository format number, and
   independent of any other FS back ends.

   Note: If you bump this, please update the switch statement in
         svn_fs_x__create() as well.
 */
#define SVN_FS_X__FORMAT_NUMBER   2

/* Latest experimental format number.  Experimental formats are only
   compatible with themselves. */
#define SVN_FS_X__EXPERIMENTAL_FORMAT_NUMBER   2

/* On most operating systems apr implements file locks per process, not
   per file.  On Windows apr implements the locking as per file handle
   locks, so we don't have to add our own mutex for just in-process
   synchronization. */
#if APR_HAS_THREADS && !defined(WIN32)
#define SVN_FS_X__USE_LOCK_MUTEX 1
#else
#define SVN_FS_X__USE_LOCK_MUTEX 0
#endif

/* Maximum number of changes we deliver per request when listing the
   changed paths for a given revision.   Anything > 0 will do.
   At 100..300 bytes per entry, this limits the allocation to ~30kB. */
#define SVN_FS_X__CHANGES_BLOCK_SIZE 100

/* Private FSX-specific data shared between all svn_txn_t objects that
   relate to a particular transaction in a filesystem (as identified
   by transaction id and filesystem UUID).  Objects of this type are
   allocated in their own subpool of the common pool. */
typedef struct svn_fs_x__shared_txn_data_t
{
  /* The next transaction in the list, or NULL if there is no following
     transaction. */
  struct svn_fs_x__shared_txn_data_t *next;

  /* ID of this transaction. */
  svn_fs_x__txn_id_t txn_id;

  /* Whether the transaction's prototype revision file is locked for
     writing by any thread in this process (including the current
     thread; recursive locks are not permitted).  This is effectively
     a non-recursive mutex. */
  svn_boolean_t being_written;

  /* The pool in which this object has been allocated; a subpool of the
     common pool. */
  apr_pool_t *pool;
} svn_fs_x__shared_txn_data_t;

/* Private FSX-specific data shared between all svn_fs_t objects that
   relate to a particular filesystem, as identified by filesystem UUID.
   Objects of this type are allocated in the common pool. */
typedef struct svn_fs_x__shared_data_t
{
  /* A list of shared transaction objects for each transaction that is
     currently active, or NULL if none are.  All access to this list,
     including the contents of the objects stored in it, is synchronised
     under TXN_LIST_LOCK. */
  svn_fs_x__shared_txn_data_t *txns;

  /* A free transaction object, or NULL if there is no free object.
     Access to this object is synchronised under TXN_LIST_LOCK. */
  svn_fs_x__shared_txn_data_t *free_txn;

  /* The following lock must be taken out in reverse order of their
     declaration here.  Any subset may be acquired and held at any given
     time but their relative acquisition order must not change.

     (lock 'pack' before 'write' before 'txn-current' before 'txn-list') */

  /* A lock for intra-process synchronization when accessing the TXNS list. */
  svn_mutex__t *txn_list_lock;

  /* A lock for intra-process synchronization when locking the
     txn-current file. */
  svn_mutex__t *txn_current_lock;

  /* A lock for intra-process synchronization when grabbing the
     repository write lock. */
  svn_mutex__t *fs_write_lock;

  /* A lock for intra-process synchronization when grabbing the
     repository pack operation lock. */
  svn_mutex__t *fs_pack_lock;

  /* The common pool, under which this object is allocated, subpools
     of which are used to allocate the transaction objects. */
  apr_pool_t *common_pool;
} svn_fs_x__shared_data_t;

/* Data structure for the 1st level DAG node cache. */
typedef struct svn_fs_x__dag_cache_t svn_fs_x__dag_cache_t;

/* Key type for all caches that use revision + offset / counter as key.

   Note: Cache keys should be 16 bytes for best performance and there
         should be no padding. */
typedef struct svn_fs_x__pair_cache_key_t
{
  /* The object's revision.  Use the 64 data type to prevent padding. */
  apr_int64_t revision;

  /* Sub-address: item index, revprop generation, packed flag, etc. */
  apr_int64_t second;
} svn_fs_x__pair_cache_key_t;

/* Key type that identifies a representation / rep header.

   Note: Cache keys should require no padding. */
typedef struct svn_fs_x__representation_cache_key_t
{
  /* Revision that contains the representation */
  apr_int64_t revision;

  /* Packed or non-packed representation (boolean)? */
  apr_int64_t is_packed;

  /* Item index of the representation */
  apr_uint64_t item_index;
} svn_fs_x__representation_cache_key_t;

/* Key type that identifies a txdelta window.

   Note: Cache keys should require no padding. */
typedef struct svn_fs_x__window_cache_key_t
{
  /* The object's revision.  Use the 64 data type to prevent padding. */
  apr_int64_t revision;

  /* Window number within that representation. */
  apr_int64_t chunk_index;

  /* Item index of the representation */
  apr_uint64_t item_index;
} svn_fs_x__window_cache_key_t;

/* Private (non-shared) FSX-specific data for each svn_fs_t object.
   Any caches in here may be NULL. */
typedef struct svn_fs_x__data_t
{
  /* The format number of this FS. */
  int format;

  /* The maximum number of files to store per directory. */
  int max_files_per_dir;

  /* Rev / pack file read granularity in bytes. */
  apr_int64_t block_size;

  /* Rev / pack file granularity (in bytes) covered by a single phys-to-log
   * index page. */
  /* Capacity in entries of log-to-phys index pages */
  apr_int64_t l2p_page_size;

  /* Rev / pack file granularity covered by phys-to-log index pages */
  apr_int64_t p2l_page_size;

  /* The revision that was youngest, last time we checked. */
  svn_revnum_t youngest_rev_cache;

  /* Caches of immutable data.  (Note that these may be shared between
     multiple svn_fs_t's for the same filesystem.) */

  /* Access to the configured memcached instances.  May be NULL. */
  svn_memcache_t *memcache;

  /* If TRUE, don't ignore any cache-related errors.  If FALSE, errors from
     e.g. memcached may be ignored as caching is an optional feature. */
  svn_boolean_t fail_stop;

  /* Caches native dag_node_t* instances */
  svn_fs_x__dag_cache_t *dag_node_cache;

  /* A cache of the contents of immutable directories; maps from
     unparsed FS ID to a apr_hash_t * mapping (const char *) dirent
     names to (svn_fs_x__dirent_t *). */
  svn_cache__t *dir_cache;

  /* Fulltext cache; currently only used with memcached.  Maps from
     rep key (revision/offset) to svn_stringbuf_t. */
  svn_cache__t *fulltext_cache;

  /* Revprop generation number.  Will be -1 if it has to reread from disk. */
  apr_int64_t revprop_generation;

  /* Revision property cache.  Maps from (rev,generation) to apr_hash_t. */
  svn_cache__t *revprop_cache;

  /* Node properties cache.  Maps from rep key to apr_hash_t. */
  svn_cache__t *properties_cache;

  /* Cache for txdelta_window_t objects;
   * the key is svn_fs_x__window_cache_key_t */
  svn_cache__t *txdelta_window_cache;

  /* Cache for combined windows as svn_stringbuf_t objects;
     the key is svn_fs_x__window_cache_key_t */
  svn_cache__t *combined_window_cache;

  /* Cache for svn_fs_x__rep_header_t objects;
   * the key is (revision, item index) */
  svn_cache__t *node_revision_cache;

  /* Cache for noderevs_t containers;
     the key is a (pack file revision, file offset) pair */
  svn_cache__t *noderevs_container_cache;

  /* Cache for change lists n blocks as svn_fs_x__changes_list_t * objects;
     the key is the (revision, first-element-in-block) pair. */
  svn_cache__t *changes_cache;

  /* Cache for change_list_t containers;
     the key is a (pack file revision, file offset) pair */
  svn_cache__t *changes_container_cache;

  /* Cache for star-delta / representation containers;
     the key is a (pack file revision, file offset) pair */
  svn_cache__t *reps_container_cache;

  /* Cache for svn_fs_x__rep_header_t objects; the key is a
     (revision, item index) pair */
  svn_cache__t *rep_header_cache;

  /* Cache for l2p_header_t objects; the key is (revision, is-packed).
     Will be NULL for pre-format7 repos */
  svn_cache__t *l2p_header_cache;

  /* Cache for l2p_page_t objects; the key is svn_fs_x__page_cache_key_t.
     Will be NULL for pre-format7 repos */
  svn_cache__t *l2p_page_cache;

  /* Cache for p2l_header_t objects; the key is (revision, is-packed).
     Will be NULL for pre-format7 repos */
  svn_cache__t *p2l_header_cache;

  /* Cache for apr_array_header_t objects containing svn_fs_x__p2l_entry_t
     elements; the key is svn_fs_x__page_cache_key_t.
     Will be NULL for pre-format7 repos */
  svn_cache__t *p2l_page_cache;

  /* TRUE while the we hold a lock on the write lock file. */
  svn_boolean_t has_write_lock;

  /* Data shared between all svn_fs_t objects for a given filesystem. */
  svn_fs_x__shared_data_t *shared;

  /* The sqlite database used for rep caching. */
  svn_sqlite__db_t *rep_cache_db;

  /* Thread-safe boolean */
  svn_atomic_t rep_cache_db_opened;

  /* The oldest revision not in a pack file.  It also applies to revprops
   * if revprop packing has been enabled by the FSX format version. */
  svn_revnum_t min_unpacked_rev;

  /* Whether rep-sharing is supported by the filesystem
   * and allowed by the configuration. */
  svn_boolean_t rep_sharing_allowed;

  /* File size limit in bytes up to which multiple revprops shall be packed
   * into a single file. */
  apr_int64_t revprop_pack_size;

  /* Whether packed revprop files shall be compressed. */
  svn_boolean_t compress_packed_revprops;

  /* Restart deltification histories after each multiple of this value */
  apr_int64_t max_deltification_walk;

  /* Maximum number of length of the linear part at the top of the
   * deltification history after which skip deltas will be used. */
  apr_int64_t max_linear_deltification;

  /* Compression level to use with txdelta storage format in new revs. */
  int delta_compression_level;

  /* Pack after every commit. */
  svn_boolean_t pack_after_commit;

  /* Per-instance filesystem ID, which provides an additional level of
     uniqueness for filesystems that share the same UUID, but should
     still be distinguishable (e.g. backups produced by svn_fs_hotcopy()
     or dump / load cycles). */
  const char *instance_id;

  /* Ensure that all filesystem changes are written to disk. */
  svn_boolean_t flush_to_disk;

  /* Pointer to svn_fs_open. */
  svn_error_t *(*svn_fs_open_)(svn_fs_t **, const char *, apr_hash_t *,
                               apr_pool_t *, apr_pool_t *);

} svn_fs_x__data_t;


/*** Filesystem Transaction ***/
typedef struct svn_fs_x__transaction_t
{
  /* revision upon which this txn is base.  (unfinished only) */
  svn_revnum_t base_rev;

  /* copies list (const char * copy_ids), or NULL if there have been
     no copies in this transaction.  */
  apr_array_header_t *copies;

} svn_fs_x__transaction_t;


/*** Representation ***/
/* If you add fields to this, check to see if you need to change
 * svn_fs_x__rep_copy. */
typedef struct svn_fs_x__representation_t
{
  /* Checksums digests for the contents produced by this representation.
     This checksum is for the contents the rep shows to consumers,
     regardless of how the rep stores the data under the hood.  It is
     independent of the storage (fulltext, delta, whatever).

     If has_sha1 is FALSE, then for compatibility behave as though this
     checksum matches the expected checksum.

     The md5 checksum is always filled, unless this is rep which was
     retrieved from the rep-cache.  The sha1 checksum is only computed on
     a write, for use with rep-sharing. */
  svn_boolean_t has_sha1;
  unsigned char sha1_digest[APR_SHA1_DIGESTSIZE];
  unsigned char md5_digest[APR_MD5_DIGESTSIZE];

  /* Change set and item number where this representation is located. */
  svn_fs_x__id_t id;

  /* The size of the representation in bytes as seen in the revision
     file. */
  svn_filesize_t size;

  /* The size of the fulltext of the representation. */
  svn_filesize_t expanded_size;

} svn_fs_x__representation_t;


/*** Node-Revision ***/
/* If you add fields to this, check to see if you need to change
 * copy_node_revision in dag.c. */
typedef struct svn_fs_x__noderev_t
{
  /* Predecessor node revision id.  Will be "unused" if there is no
     predecessor for this node revision. */
  svn_fs_x__id_t predecessor_id;

  /* The ID of this noderev */
  svn_fs_x__id_t noderev_id;

  /* Identifier of the node that this noderev belongs to. */
  svn_fs_x__id_t node_id;

  /* Copy identifier of this line of history. */
  svn_fs_x__id_t copy_id;

  /* If this node-rev is a copy, where was it copied from? */
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;

  /* Helper for history tracing, root of the parent tree from whence
     this node-rev was copied. */
  svn_revnum_t copyroot_rev;
  const char *copyroot_path;

  /* node kind */
  svn_node_kind_t kind;

  /* Number of predecessors this node revision has (recursively).
     A difference from the BDB backend is that it cannot be -1. */
  int predecessor_count;

  /* representation key for this node's properties.  may be NULL if
     there are no properties.  */
  svn_fs_x__representation_t *prop_rep;

  /* representation for this node's data.  may be NULL if there is
     no data. */
  svn_fs_x__representation_t *data_rep;

  /* path at which this node first came into existence.  */
  const char *created_path;

  /* Does this node itself have svn:mergeinfo? */
  svn_boolean_t has_mergeinfo;

  /* Number of nodes with svn:mergeinfo properties that are
     descendants of this node (including it itself) */
  apr_int64_t mergeinfo_count;

} svn_fs_x__noderev_t;


/** The type of a directory entry.  */
typedef struct svn_fs_x__dirent_t
{

  /** The name of this directory entry.  */
  const char *name;

  /** The node revision ID it names.  */
  svn_fs_x__id_t id;

  /** The node kind. */
  svn_node_kind_t kind;
} svn_fs_x__dirent_t;


/*** Change ***/
typedef svn_fs_path_change3_t svn_fs_x__change_t;

/*** Context for reading changed paths lists iteratively. */
typedef struct svn_fs_x__changes_context_t
{
  /* Repository to fetch from. */
  svn_fs_t *fs;

  /* Revision that we read from. */
  svn_revnum_t revision;

  /* Revision file object to use when needed. */
  svn_fs_x__revision_file_t *revision_file;

  /* Index of the next change to fetch. */
  int next;

  /* Offset, within the changed paths list on disk, of the next change to
     fetch. */
  apr_off_t next_offset;

  /* Has the end of the list been reached? */
  svn_boolean_t eol;

} svn_fs_x__changes_context_t;

/*** Directory (only used at the cache interface) ***/
typedef struct svn_fs_x__dir_data_t
{
  /* Contents, i.e. all directory entries, sorted by name. */
  apr_array_header_t *entries;

  /* SVN_INVALID_FILESIZE for committed data, otherwise the length of the
   * in-txn on-disk representation of that directory. */
  svn_filesize_t txn_filesize;
} svn_fs_x__dir_data_t;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_FS_H */
