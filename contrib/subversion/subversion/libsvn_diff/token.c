/*
 * token.c :  routines for doing diffs
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


#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>

#include "svn_error.h"
#include "svn_diff.h"
#include "svn_types.h"

#include "diff.h"


/*
 * Prime number to use as the size of the hash table.  This number was
 * not selected by testing of any kind and may need tweaking.
 */
#define SVN_DIFF__HASH_SIZE 127

struct svn_diff__node_t
{
  svn_diff__node_t       *parent;
  svn_diff__node_t       *left;
  svn_diff__node_t       *right;

  apr_uint32_t            hash;
  svn_diff__token_index_t index;
  void                   *token;
};

struct svn_diff__tree_t
{
  svn_diff__node_t       *root[SVN_DIFF__HASH_SIZE];
  apr_pool_t             *pool;
  svn_diff__token_index_t node_count;
};


/*
 * Returns number of tokens in a tree
 */
svn_diff__token_index_t
svn_diff__get_node_count(svn_diff__tree_t *tree)
{
  return tree->node_count;
}

/*
 * Support functions to build a tree of token positions
 */

void
svn_diff__tree_create(svn_diff__tree_t **tree, apr_pool_t *pool)
{
  *tree = apr_pcalloc(pool, sizeof(**tree));
  (*tree)->pool = pool;
  (*tree)->node_count = 0;
}


static svn_error_t *
tree_insert_token(svn_diff__node_t **node, svn_diff__tree_t *tree,
                  void *diff_baton,
                  const svn_diff_fns2_t *vtable,
                  apr_uint32_t hash, void *token)
{
  svn_diff__node_t *new_node;
  svn_diff__node_t **node_ref;
  svn_diff__node_t *parent;
  int rv;

  SVN_ERR_ASSERT(token);

  parent = NULL;
  node_ref = &tree->root[hash % SVN_DIFF__HASH_SIZE];

  while (*node_ref != NULL)
    {
      parent = *node_ref;

      rv = hash - parent->hash;
      if (!rv)
        SVN_ERR(vtable->token_compare(diff_baton, parent->token, token, &rv));

      if (rv == 0)
        {
          /* Discard the previous token.  This helps in cases where
           * only recently read tokens are still in memory.
           */
          if (vtable->token_discard != NULL)
            vtable->token_discard(diff_baton, parent->token);

          parent->token = token;
          *node = parent;

          return SVN_NO_ERROR;
        }
      else if (rv > 0)
        {
          node_ref = &parent->left;
        }
      else
        {
          node_ref = &parent->right;
        }
    }

  /* Create a new node */
  new_node = apr_palloc(tree->pool, sizeof(*new_node));
  new_node->parent = parent;
  new_node->left = NULL;
  new_node->right = NULL;
  new_node->hash = hash;
  new_node->token = token;
  new_node->index = tree->node_count++;

  *node = *node_ref = new_node;

  return SVN_NO_ERROR;
}


/*
 * Get all tokens from a datasource.  Return the
 * last item in the (circular) list.
 */
svn_error_t *
svn_diff__get_tokens(svn_diff__position_t **position_list,
                     svn_diff__tree_t *tree,
                     void *diff_baton,
                     const svn_diff_fns2_t *vtable,
                     svn_diff_datasource_e datasource,
                     apr_off_t prefix_lines,
                     apr_pool_t *pool)
{
  svn_diff__position_t *start_position;
  svn_diff__position_t *position = NULL;
  svn_diff__position_t **position_ref;
  svn_diff__node_t *node;
  void *token;
  apr_off_t offset;
  apr_uint32_t hash;

  *position_list = NULL;

  position_ref = &start_position;
  offset = prefix_lines;
  hash = 0; /* The callback fn doesn't need to touch it per se */
  while (1)
    {
      SVN_ERR(vtable->datasource_get_next_token(&hash, &token,
                                                diff_baton, datasource));
      if (token == NULL)
        break;

      offset++;
      SVN_ERR(tree_insert_token(&node, tree, diff_baton, vtable, hash, token));

      /* Create a new position */
      position = apr_palloc(pool, sizeof(*position));
      position->next = NULL;
      position->token_index = node->index;
      position->offset = offset;

      *position_ref = position;
      position_ref = &position->next;
    }

  *position_ref = start_position;

  SVN_ERR(vtable->datasource_close(diff_baton, datasource));

  *position_list = position;

  return SVN_NO_ERROR;
}
