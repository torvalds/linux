/* id.h : interface to node ID functions, private to libsvn_fs_fs
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

#ifndef SVN_LIBSVN_FS_FS_ID_H
#define SVN_LIBSVN_FS_FS_ID_H

#include "svn_fs.h"
#include "private/svn_fs_fs_private.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Operations on ID parts. ***/

/* Return TRUE, if both elements of the PART is 0, i.e. this is the default
 * value if e.g. no copies were made of this node. */
svn_boolean_t svn_fs_fs__id_part_is_root(const svn_fs_fs__id_part_t *part);

/* Return TRUE, if all element values of *LHS and *RHS match. */
svn_boolean_t svn_fs_fs__id_part_eq(const svn_fs_fs__id_part_t *lhs,
                                    const svn_fs_fs__id_part_t *rhs);

/* Return TRUE, if TXN_ID is used, i.e. doesn't contain just the defaults. */
svn_boolean_t svn_fs_fs__id_txn_used(const svn_fs_fs__id_part_t *txn_id);

/* Reset TXN_ID to the defaults. */
void svn_fs_fs__id_txn_reset(svn_fs_fs__id_part_t *txn_id);

/* Parse the transaction id in DATA and store the result in *TXN_ID */
svn_error_t *svn_fs_fs__id_txn_parse(svn_fs_fs__id_part_t *txn_id,
                                     const char *data);

/* Convert the transaction id in *TXN_ID into a textual representation
 * allocated in POOL. */
const char *svn_fs_fs__id_txn_unparse(const svn_fs_fs__id_part_t *txn_id,
                                      apr_pool_t *pool);


/*** ID accessor functions. ***/

/* Get the "node id" portion of ID. */
const svn_fs_fs__id_part_t *svn_fs_fs__id_node_id(const svn_fs_id_t *id);

/* Get the "copy id" portion of ID. */
const svn_fs_fs__id_part_t *svn_fs_fs__id_copy_id(const svn_fs_id_t *id);

/* Get the "txn id" portion of ID, or NULL if it is a permanent ID. */
const svn_fs_fs__id_part_t *svn_fs_fs__id_txn_id(const svn_fs_id_t *id);

/* Get the "rev,item" portion of ID. */
const svn_fs_fs__id_part_t *svn_fs_fs__id_rev_item(const svn_fs_id_t *id);

/* Get the "rev" portion of ID, or SVN_INVALID_REVNUM if it is a
   transaction ID. */
svn_revnum_t svn_fs_fs__id_rev(const svn_fs_id_t *id);

/* Access the "item" portion of the ID, or 0 if it is a transaction
   ID. */
apr_uint64_t svn_fs_fs__id_item(const svn_fs_id_t *id);

/* Return TRUE, if this is a transaction ID. */
svn_boolean_t svn_fs_fs__id_is_txn(const svn_fs_id_t *id);

/* Convert ID into string form, allocated in POOL. */
svn_string_t *svn_fs_fs__id_unparse(const svn_fs_id_t *id,
                                    apr_pool_t *pool);

/* Return true if A and B are equal. */
svn_boolean_t svn_fs_fs__id_eq(const svn_fs_id_t *a,
                               const svn_fs_id_t *b);

/* Return true if A and B are related. */
svn_boolean_t svn_fs_fs__id_check_related(const svn_fs_id_t *a,
                                          const svn_fs_id_t *b);

/* Return the noderev relationship between A and B. */
svn_fs_node_relation_t svn_fs_fs__id_compare(const svn_fs_id_t *a,
                                             const svn_fs_id_t *b);

/* Return 0 if A and B are equal, 1 if A is "greater than" B, -1 otherwise. */
int svn_fs_fs__id_part_compare(const svn_fs_fs__id_part_t *a,
                               const svn_fs_fs__id_part_t *b);

/* Create the txn root ID for transaction TXN_ID.  Allocate it in POOL. */
svn_fs_id_t *svn_fs_fs__id_txn_create_root(const svn_fs_fs__id_part_t *txn_id,
                                           apr_pool_t *pool);

/* Create the root ID for REVISION (logical addressing node only).
   Allocate it in POOL. */
svn_fs_id_t *svn_fs_fs__id_create_root(const svn_revnum_t revision,
                                       apr_pool_t *pool);

/* Create an ID within a transaction based on NODE_ID, COPY_ID, and
   TXN_ID, allocated in POOL. */
svn_fs_id_t *svn_fs_fs__id_txn_create(const svn_fs_fs__id_part_t *node_id,
                                      const svn_fs_fs__id_part_t *copy_id,
                                      const svn_fs_fs__id_part_t *txn_id,
                                      apr_pool_t *pool);

/* Create a permanent ID based on NODE_ID, COPY_ID and REV_ITEM,
   allocated in POOL. */
svn_fs_id_t *svn_fs_fs__id_rev_create(const svn_fs_fs__id_part_t *node_id,
                                      const svn_fs_fs__id_part_t *copy_id,
                                      const svn_fs_fs__id_part_t *rev_item,
                                      apr_pool_t *pool);

/* Return a copy of ID, allocated from POOL. */
svn_fs_id_t *svn_fs_fs__id_copy(const svn_fs_id_t *id,
                                apr_pool_t *pool);

/* Return an ID in *ID_P resulting from parsing the string DATA, or an error
   if DATA is an invalid ID string. *DATA will be modified / invalidated by
   this call. */
svn_error_t *
svn_fs_fs__id_parse(const svn_fs_id_t **id_p,
                    char *data,
                    apr_pool_t *pool);


/* (de-)serialization support*/

struct svn_temp_serializer__context_t;

/**
 * Serialize an @a id within the serialization @a context.
 */
void
svn_fs_fs__id_serialize(struct svn_temp_serializer__context_t *context,
                        const svn_fs_id_t * const *id);

/**
 * Deserialize an @a id within the @a buffer.
 */
void
svn_fs_fs__id_deserialize(void *buffer,
                          svn_fs_id_t **id);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_FS_ID_H */
