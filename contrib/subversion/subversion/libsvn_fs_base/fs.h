/* fs.h : interface to Subversion filesystem, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_BASE_H
#define SVN_LIBSVN_FS_BASE_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_fs.h"

#include "bdb/env.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Filesystem schema versions ***/

/* The format number of this filesystem.  This is independent of the
   repository format number, and independent of any other FS back
   ends.  See the SVN_FS_BASE__MIN_*_FORMAT defines to get a sense of
   what changes and features were added in which versions of this
   back-end's format.

   Note: If you bump this, please update the switch statement in
         base_create() as well.
 */
#define SVN_FS_BASE__FORMAT_NUMBER                4

/* Minimum format number that supports representation sharing.  This
   also brings in the support for storing SHA1 checksums.   */
#define SVN_FS_BASE__MIN_REP_SHARING_FORMAT       4

/* Minimum format number that supports the 'miscellaneous' table */
#define SVN_FS_BASE__MIN_MISCELLANY_FORMAT        4

/* Minimum format number that supports forward deltas */
#define SVN_FS_BASE__MIN_FORWARD_DELTAS_FORMAT    4

/* Minimum format number that supports node-origins tracking */
#define SVN_FS_BASE__MIN_NODE_ORIGINS_FORMAT      3

/* Minimum format number that supports mergeinfo */
#define SVN_FS_BASE__MIN_MERGEINFO_FORMAT         3

/* Minimum format number that supports svndiff version 1.  */
#define SVN_FS_BASE__MIN_SVNDIFF1_FORMAT          2

/* Return SVN_ERR_UNSUPPORTED_FEATURE if the version of filesystem FS does
   not indicate support for FEATURE (which REQUIRES a newer version). */
svn_error_t *
svn_fs_base__test_required_feature_format(svn_fs_t *fs,
                                          const char *feature,
                                          int requires);



/*** Miscellany keys. ***/

/* Revision at which the repo started using forward deltas. */
#define SVN_FS_BASE__MISC_FORWARD_DELTA_UPGRADE  "forward-delta-rev"



/*** The filesystem structure.  ***/

typedef struct base_fs_data_t
{
  /* A Berkeley DB environment for all the filesystem's databases.
     This establishes the scope of the filesystem's transactions.  */
  bdb_env_baton_t *bdb;

  /* The filesystem's various tables.  See `structure' for details.  */
  DB *changes;
  DB *copies;
  DB *nodes;
  DB *representations;
  DB *revisions;
  DB *strings;
  DB *transactions;
  DB *uuids;
  DB *locks;
  DB *lock_tokens;
  DB *node_origins;
  DB *miscellaneous;
  DB *checksum_reps;

  /* A boolean for tracking when we have a live Berkeley DB
     transaction trail alive. */
  svn_boolean_t in_txn_trail;

  /* The format number of this FS. */
  int format;

} base_fs_data_t;


/*** Filesystem Revision ***/
typedef struct revision_t
{
  /* id of the transaction that was committed to create this
     revision. */
  const char *txn_id;

} revision_t;


/*** Transaction Kind ***/
typedef enum transaction_kind_t
{
  transaction_kind_normal = 1,  /* normal, uncommitted */
  transaction_kind_committed,   /* committed */
  transaction_kind_dead         /* uncommitted and dead */

} transaction_kind_t;


/*** Filesystem Transaction ***/
typedef struct transaction_t
{
  /* kind of transaction. */
  transaction_kind_t kind;

  /* revision which this transaction was committed to create, or an
     invalid revision number if this transaction was never committed. */
  svn_revnum_t revision;

  /* property list (const char * name, svn_string_t * value).
     may be NULL if there are no properties.  */
  apr_hash_t *proplist;

  /* node revision id of the root node.  */
  const svn_fs_id_t *root_id;

  /* node revision id of the node which is the root of the revision
     upon which this txn is base.  (unfinished only) */
  const svn_fs_id_t *base_id;

  /* copies list (const char * copy_ids), or NULL if there have been
     no copies in this transaction.  */
  apr_array_header_t *copies;

} transaction_t;


/*** Node-Revision ***/
typedef struct node_revision_t
{
  /* node kind */
  svn_node_kind_t kind;

  /* predecessor node revision id, or NULL if there is no predecessor
     for this node revision */
  const svn_fs_id_t *predecessor_id;

  /* number of predecessors this node revision has (recursively), or
     -1 if not known (for backward compatibility). */
  int predecessor_count;

  /* representation key for this node's properties.  may be NULL if
     there are no properties.  */
  const char *prop_key;

  /* representation key for this node's text data (files) or entries
     list (dirs).  may be NULL if there are no contents.  */
  const char *data_key;

  /* data representation instance identifier.  Sounds fancy, but is
     really just a way to distinguish between "I use the same rep key
     as another node because we share ancestry and haven't had our
     text touched at all" and "I use the same rep key as another node
     only because one or both of us decided to pick up a shared
     representation after-the-fact."  May be NULL (if this node
     revision isn't using a shared rep, or isn't the original
     "assignee" of a shared rep). */
  const char *data_key_uniquifier;

  /* representation key for this node's text-data-in-progess (files
     only).  NULL if no edits are currently in-progress.  This field
     is always NULL for kinds other than "file".  */
  const char *edit_key;

  /* path at which this node first came into existence.  */
  const char *created_path;

  /* does this node revision have the mergeinfo tracking property set
     on it?  (only valid for FS schema 3 and newer) */
  svn_boolean_t has_mergeinfo;

  /* number of children of this node which have the mergeinfo tracking
     property set  (0 for files; valid only for FS schema 3 and newer). */
  apr_int64_t mergeinfo_count;

} node_revision_t;


/*** Representation Kind ***/
typedef enum rep_kind_t
{
  rep_kind_fulltext = 1, /* fulltext */
  rep_kind_delta         /* delta */

} rep_kind_t;


/*** "Delta" Offset/Window Chunk ***/
typedef struct rep_delta_chunk_t
{
  /* diff format version number ### at this point, "svndiff" is the
     only format used. */
  apr_byte_t version;

  /* starting offset of the data represented by this chunk */
  svn_filesize_t offset;

  /* string-key to which this representation points. */
  const char *string_key;

  /* size of the fulltext data represented by this delta window. */
  apr_size_t size;

  /* representation-key to use when needed source data for
     undeltification. */
  const char *rep_key;

  /* apr_off_t rep_offset;  ### not implemented */

} rep_delta_chunk_t;


/*** Representation ***/
typedef struct representation_t
{
  /* representation kind */
  rep_kind_t kind;

  /* transaction ID under which representation was created (used as a
     mutability flag when compared with a current editing
     transaction). */
  const char *txn_id;

  /* Checksums for the contents produced by this representation.
     These checksum is for the contents the rep shows to consumers,
     regardless of how the rep stores the data under the hood.  It is
     independent of the storage (fulltext, delta, whatever).

     If this is NULL, then for compatibility behave as though
     this checksum matches the expected checksum. */
  svn_checksum_t *md5_checksum;
  svn_checksum_t *sha1_checksum;

  /* kind-specific stuff */
  union
  {
    /* fulltext stuff */
    struct
    {
      /* string-key which holds the fulltext data */
      const char *string_key;

    } fulltext;

    /* delta stuff */
    struct
    {
      /* an array of rep_delta_chunk_t * chunks of delta
         information */
      apr_array_header_t *chunks;

    } delta;
  } contents;
} representation_t;


/*** Copy Kind ***/
typedef enum copy_kind_t
{
  copy_kind_real = 1, /* real copy */
  copy_kind_soft      /* soft copy */

} copy_kind_t;


/*** Copy ***/
typedef struct copy_t
{
  /* What kind of copy occurred. */
  copy_kind_t kind;

  /* Path of copy source. */
  const char *src_path;

  /* Transaction id of copy source. */
  const char *src_txn_id;

  /* Node-revision of copy destination. */
  const svn_fs_id_t *dst_noderev_id;

} copy_t;


/*** Change ***/
typedef struct change_t
{
  /* Path of the change. */
  const char *path;

  /* Node revision ID of the change. */
  const svn_fs_id_t *noderev_id;

  /* The kind of change. */
  svn_fs_path_change_kind_t kind;

  /* Text or property mods? */
  svn_boolean_t text_mod;
  svn_boolean_t prop_mod;

} change_t;


/*** Lock node ***/
typedef struct lock_node_t
{
  /* entries list, maps (const char *) name --> (const char *) lock-node-id */
  apr_hash_t *entries;

  /* optional lock-token, might be NULL. */
  const char *lock_token;

} lock_node_t;



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_BASE_H */
