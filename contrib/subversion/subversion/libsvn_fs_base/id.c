/* id.c : operations on node-revision IDs
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

#include <string.h>
#include <stdlib.h>

#include "id.h"
#include "../libsvn_fs/fs-loader.h"



typedef struct id_private_t {
  const char *node_id;
  const char *copy_id;
  const char *txn_id;
} id_private_t;


/* Accessing ID Pieces.  */

const char *
svn_fs_base__id_node_id(const svn_fs_id_t *id)
{
  id_private_t *pvt = id->fsap_data;

  return pvt->node_id;
}


const char *
svn_fs_base__id_copy_id(const svn_fs_id_t *id)
{
  id_private_t *pvt = id->fsap_data;

  return pvt->copy_id;
}


const char *
svn_fs_base__id_txn_id(const svn_fs_id_t *id)
{
  id_private_t *pvt = id->fsap_data;

  return pvt->txn_id;
}


svn_string_t *
svn_fs_base__id_unparse(const svn_fs_id_t *id,
                        apr_pool_t *pool)
{
  id_private_t *pvt = id->fsap_data;

  return svn_string_createf(pool, "%s.%s.%s",
                            pvt->node_id, pvt->copy_id, pvt->txn_id);
}


/*** Comparing node IDs ***/

svn_boolean_t
svn_fs_base__id_eq(const svn_fs_id_t *a,
                   const svn_fs_id_t *b)
{
  id_private_t *pvta = a->fsap_data, *pvtb = b->fsap_data;

  if (a == b)
    return TRUE;
  if (strcmp(pvta->node_id, pvtb->node_id) != 0)
     return FALSE;
  if (strcmp(pvta->copy_id, pvtb->copy_id) != 0)
    return FALSE;
  if (strcmp(pvta->txn_id, pvtb->txn_id) != 0)
    return FALSE;
  return TRUE;
}


svn_boolean_t
svn_fs_base__id_check_related(const svn_fs_id_t *a,
                              const svn_fs_id_t *b)
{
  id_private_t *pvta = a->fsap_data, *pvtb = b->fsap_data;

  if (a == b)
    return TRUE;

  return (strcmp(pvta->node_id, pvtb->node_id) == 0);
}


svn_fs_node_relation_t
svn_fs_base__id_compare(const svn_fs_id_t *a,
                        const svn_fs_id_t *b)
{
  if (svn_fs_base__id_eq(a, b))
    return svn_fs_node_unchanged;
  return (svn_fs_base__id_check_related(a, b) ? svn_fs_node_common_ancestor
                                              : svn_fs_node_unrelated);
}



/* Creating ID's.  */

static id_vtable_t id_vtable = {
  svn_fs_base__id_unparse,
  svn_fs_base__id_compare
};


svn_fs_id_t *
svn_fs_base__id_create(const char *node_id,
                       const char *copy_id,
                       const char *txn_id,
                       apr_pool_t *pool)
{
  svn_fs_id_t *id = apr_palloc(pool, sizeof(*id));
  id_private_t *pvt = apr_palloc(pool, sizeof(*pvt));

  pvt->node_id = apr_pstrdup(pool, node_id);
  pvt->copy_id = apr_pstrdup(pool, copy_id);
  pvt->txn_id = apr_pstrdup(pool, txn_id);
  id->vtable = &id_vtable;
  id->fsap_data = pvt;
  return id;
}


svn_fs_id_t *
svn_fs_base__id_copy(const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_fs_id_t *new_id = apr_palloc(pool, sizeof(*new_id));
  id_private_t *new_pvt = apr_palloc(pool, sizeof(*new_pvt));
  id_private_t *pvt = id->fsap_data;

  new_pvt->node_id = apr_pstrdup(pool, pvt->node_id);
  new_pvt->copy_id = apr_pstrdup(pool, pvt->copy_id);
  new_pvt->txn_id = apr_pstrdup(pool, pvt->txn_id);
  new_id->vtable = &id_vtable;
  new_id->fsap_data = new_pvt;
  return new_id;
}


svn_fs_id_t *
svn_fs_base__id_parse(const char *data,
                      apr_size_t len,
                      apr_pool_t *pool)
{
  svn_fs_id_t *id;
  id_private_t *pvt;
  char *data_copy, *str;

  /* Dup the ID data into POOL.  Our returned ID will have references
     into this memory. */
  data_copy = apr_pstrmemdup(pool, data, len);

  /* Alloc a new svn_fs_id_t structure. */
  id = apr_palloc(pool, sizeof(*id));
  pvt = apr_palloc(pool, sizeof(*pvt));
  id->vtable = &id_vtable;
  id->fsap_data = pvt;

  /* Now, we basically just need to "split" this data on `.'
     characters.  We will use svn_cstring_tokenize, which will put
     terminators where each of the '.'s used to be.  Then our new
     id field will reference string locations inside our duplicate
     string.*/

  /* Node Id */
  str = svn_cstring_tokenize(".", &data_copy);
  if (str == NULL)
    return NULL;
  pvt->node_id = str;

  /* Copy Id */
  str = svn_cstring_tokenize(".", &data_copy);
  if (str == NULL)
    return NULL;
  pvt->copy_id = str;

  /* Txn Id */
  str = svn_cstring_tokenize(".", &data_copy);
  if (str == NULL)
    return NULL;
  pvt->txn_id = str;

  return id;
}
