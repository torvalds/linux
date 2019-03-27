/*
 * element.c :  editing trees of versioned resources
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

#include <assert.h>
#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_props.h"
#include "svn_dirent_uri.h"
#include "svn_iter.h"
#include "private/svn_sorts_private.h"

#include "private/svn_element.h"
#include "svn_private_config.h"


void *
svn_eid__hash_get(apr_hash_t *ht,
                  int key)
{
  return apr_hash_get(ht, &key, sizeof(key));
}

void
svn_eid__hash_set(apr_hash_t *ht,
                  int key,
                  const void *val)
{
  int *id_p = apr_pmemdup(apr_hash_pool_get(ht), &key, sizeof(key));

  apr_hash_set(ht, id_p, sizeof(key), val);
}

int
svn_eid__hash_this_key(apr_hash_index_t *hi)
{
  return *(const int *)apr_hash_this_key(hi);
}

svn_eid__hash_iter_t *
svn_eid__hash_sorted_first(apr_pool_t *pool,
                           apr_hash_t *ht,
                           int (*comparison_func)(const svn_sort__item_t *,
                                                  const svn_sort__item_t *))
{
  svn_eid__hash_iter_t *hi = apr_palloc(pool, sizeof(*hi));

  if (apr_hash_count(ht) == 0)
    return NULL;

  hi->array = svn_sort__hash(ht, comparison_func, pool);
  hi->i = 0;
  hi->eid = *(const int *)(APR_ARRAY_IDX(hi->array, hi->i,
                                         svn_sort__item_t).key);
  hi->val = APR_ARRAY_IDX(hi->array, hi->i, svn_sort__item_t).value;
  return hi;
}

svn_eid__hash_iter_t *
svn_eid__hash_sorted_next(svn_eid__hash_iter_t *hi)
{
  hi->i++;
  if (hi->i >= hi->array->nelts)
    {
      return NULL;
    }
  hi->eid = *(const int *)(APR_ARRAY_IDX(hi->array, hi->i,
                                         svn_sort__item_t).key);
  hi->val = APR_ARRAY_IDX(hi->array, hi->i, svn_sort__item_t).value;
  return hi;
}

int
svn_eid__hash_sort_compare_items_by_eid(const svn_sort__item_t *a,
                                        const svn_sort__item_t *b)
{
  int eid_a = *(const int *)a->key;
  int eid_b = *(const int *)b->key;

  return eid_a - eid_b;
}


/*
 * ===================================================================
 * Element payload
 * ===================================================================
 */

svn_boolean_t
svn_element__payload_invariants(const svn_element__payload_t *payload)
{
  if (payload->is_subbranch_root)
    return TRUE;

  /* If kind is unknown, it's a reference; otherwise it has content
     specified and may also have a reference. */
  if (payload->kind == svn_node_unknown)
    if (SVN_IS_VALID_REVNUM(payload->branch_ref.rev)
        && payload->branch_ref.branch_id
        && payload->branch_ref.eid != -1)
      return TRUE;
  if ((payload->kind == svn_node_dir
       || payload->kind == svn_node_file
       || payload->kind == svn_node_symlink)
      && (payload->props
          && ((payload->kind == svn_node_file) == !!payload->text)
          && ((payload->kind == svn_node_symlink) == !!payload->target)))
    return TRUE;
  return FALSE;
}

svn_element__payload_t *
svn_element__payload_dup(const svn_element__payload_t *old,
                         apr_pool_t *result_pool)
{
  svn_element__payload_t *new_payload;

  assert(! old || svn_element__payload_invariants(old));

  if (old == NULL)
    return NULL;

  new_payload = apr_pmemdup(result_pool, old, sizeof(*new_payload));
  if (old->branch_ref.branch_id)
    new_payload->branch_ref.branch_id
      = apr_pstrdup(result_pool, old->branch_ref.branch_id);
  if (old->props)
    new_payload->props = svn_prop_hash_dup(old->props, result_pool);
  if (old->kind == svn_node_file && old->text)
    new_payload->text = svn_stringbuf_dup(old->text, result_pool);
  if (old->kind == svn_node_symlink && old->target)
    new_payload->target = apr_pstrdup(result_pool, old->target);
  return new_payload;
}

svn_boolean_t
svn_element__payload_equal(const svn_element__payload_t *left,
                           const svn_element__payload_t *right,
                           apr_pool_t *scratch_pool)
{
  apr_array_header_t *prop_diffs;

  assert(svn_element__payload_invariants(left));
  assert(svn_element__payload_invariants(right));

  /* any two subbranch-root elements compare equal */
  if (left->is_subbranch_root && right->is_subbranch_root)
    {
      return TRUE;
    }
  else if (left->is_subbranch_root || right->is_subbranch_root)
    {
      return FALSE;
    }

  /* content defined only by reference is not supported */
  SVN_ERR_ASSERT_NO_RETURN(left->kind != svn_node_unknown
                           && right->kind != svn_node_unknown);

  if (left->kind != right->kind)
    {
      return FALSE;
    }

  svn_error_clear(svn_prop_diffs(&prop_diffs,
                                 left->props, right->props,
                                 scratch_pool));

  if (prop_diffs->nelts != 0)
    {
      return FALSE;
    }
  switch (left->kind)
    {
    case svn_node_dir:
      break;
    case svn_node_file:
      if (! svn_stringbuf_compare(left->text, right->text))
        {
          return FALSE;
        }
      break;
    case svn_node_symlink:
      if (strcmp(left->target, right->target) != 0)
        {
          return FALSE;
        }
      break;
    default:
      break;
    }

  return TRUE;
}

svn_element__payload_t *
svn_element__payload_create_subbranch(apr_pool_t *result_pool)
{
  svn_element__payload_t *new_payload
    = apr_pcalloc(result_pool, sizeof(*new_payload));

  new_payload->pool = result_pool;
  new_payload->is_subbranch_root = TRUE;
  assert(svn_element__payload_invariants(new_payload));
  return new_payload;
}

svn_element__payload_t *
svn_element__payload_create_ref(svn_revnum_t rev,
                                const char *branch_id,
                                int eid,
                                apr_pool_t *result_pool)
{
  svn_element__payload_t *new_payload
    = apr_pcalloc(result_pool, sizeof(*new_payload));

  new_payload->pool = result_pool;
  new_payload->kind = svn_node_unknown;
  new_payload->branch_ref.rev = rev;
  new_payload->branch_ref.branch_id = apr_pstrdup(result_pool, branch_id);
  new_payload->branch_ref.eid = eid;
  assert(svn_element__payload_invariants(new_payload));
  return new_payload;
}

svn_element__payload_t *
svn_element__payload_create_dir(apr_hash_t *props,
                                apr_pool_t *result_pool)
{
  svn_element__payload_t *new_payload
    = apr_pcalloc(result_pool, sizeof(*new_payload));

  new_payload->pool = result_pool;
  new_payload->kind = svn_node_dir;
  new_payload->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  assert(svn_element__payload_invariants(new_payload));
  return new_payload;
}

svn_element__payload_t *
svn_element__payload_create_file(apr_hash_t *props,
                                 svn_stringbuf_t *text,
                                 apr_pool_t *result_pool)
{
  svn_element__payload_t *new_payload
    = apr_pcalloc(result_pool, sizeof(*new_payload));

  SVN_ERR_ASSERT_NO_RETURN(text);

  new_payload->pool = result_pool;
  new_payload->kind = svn_node_file;
  new_payload->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  new_payload->text = svn_stringbuf_dup(text, result_pool);
  assert(svn_element__payload_invariants(new_payload));
  return new_payload;
}

svn_element__payload_t *
svn_element__payload_create_symlink(apr_hash_t *props,
                                    const char *target,
                                    apr_pool_t *result_pool)
{
  svn_element__payload_t *new_payload
    = apr_pcalloc(result_pool, sizeof(*new_payload));

  SVN_ERR_ASSERT_NO_RETURN(target);

  new_payload->pool = result_pool;
  new_payload->kind = svn_node_symlink;
  new_payload->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  new_payload->target = apr_pstrdup(result_pool, target);
  assert(svn_element__payload_invariants(new_payload));
  return new_payload;
}

svn_element__content_t *
svn_element__content_create(int parent_eid,
                            const char *name,
                            const svn_element__payload_t *payload,
                            apr_pool_t *result_pool)
{
  svn_element__content_t *content
     = apr_palloc(result_pool, sizeof(*content));

  content->parent_eid = parent_eid;
  content->name = apr_pstrdup(result_pool, name);
  content->payload = svn_element__payload_dup(payload, result_pool);
  return content;
}

svn_element__content_t *
svn_element__content_dup(const svn_element__content_t *old,
                         apr_pool_t *result_pool)
{
  svn_element__content_t *content
     = apr_pmemdup(result_pool, old, sizeof(*content));

  content->name = apr_pstrdup(result_pool, old->name);
  content->payload = svn_element__payload_dup(old->payload, result_pool);
  return content;
}

svn_boolean_t
svn_element__content_equal(const svn_element__content_t *content_left,
                           const svn_element__content_t *content_right,
                           apr_pool_t *scratch_pool)
{
  if (!content_left && !content_right)
    {
      return TRUE;
    }
  else if (!content_left || !content_right)
    {
      return FALSE;
    }

  if (content_left->parent_eid != content_right->parent_eid)
    {
      return FALSE;
    }
  if (strcmp(content_left->name, content_right->name) != 0)
    {
      return FALSE;
    }
  if (! svn_element__payload_equal(content_left->payload, content_right->payload,
                                   scratch_pool))
    {
      return FALSE;
    }

  return TRUE;
}

svn_element__tree_t *
svn_element__tree_create(apr_hash_t *e_map,
                         int root_eid,
                         apr_pool_t *result_pool)
{
  svn_element__tree_t *element_tree
    = apr_pcalloc(result_pool, sizeof(*element_tree));

  element_tree->e_map = e_map ? apr_hash_copy(result_pool, e_map)
                              : apr_hash_make(result_pool);
  element_tree->root_eid = root_eid;
  return element_tree;
}

svn_element__content_t *
svn_element__tree_get(const svn_element__tree_t *tree,
                      int eid)
{
  return svn_eid__hash_get(tree->e_map, eid);
}

svn_error_t *
svn_element__tree_set(svn_element__tree_t *tree,
                      int eid,
                      const svn_element__content_t *element)
{
  svn_eid__hash_set(tree->e_map, eid, element);

  return SVN_NO_ERROR;
}

void
svn_element__tree_purge_orphans(apr_hash_t *e_map,
                                int root_eid,
                                apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_boolean_t changed;

  SVN_ERR_ASSERT_NO_RETURN(svn_eid__hash_get(e_map, root_eid));

  do
    {
      changed = FALSE;

      for (hi = apr_hash_first(scratch_pool, e_map);
           hi; hi = apr_hash_next(hi))
        {
          int this_eid = svn_eid__hash_this_key(hi);
          svn_element__content_t *this_element = apr_hash_this_val(hi);

          if (this_eid != root_eid)
            {
              svn_element__content_t *parent_element
                = svn_eid__hash_get(e_map, this_element->parent_eid);

              /* Purge if parent is deleted */
              if (! parent_element)
                {
                  svn_eid__hash_set(e_map, this_eid, NULL);
                  changed = TRUE;
                }
              else
                SVN_ERR_ASSERT_NO_RETURN(
                  ! parent_element->payload->is_subbranch_root);
            }
        }
    }
  while (changed);
}

const char *
svn_element__tree_get_path_by_eid(const svn_element__tree_t *tree,
                                  int eid,
                                  apr_pool_t *result_pool)
{
  const char *path = "";
  svn_element__content_t *element;

  for (; eid != tree->root_eid; eid = element->parent_eid)
    {
      element = svn_element__tree_get(tree, eid);
      if (! element)
        return NULL;
      path = svn_relpath_join(element->name, path, result_pool);
    }
  SVN_ERR_ASSERT_NO_RETURN(eid == tree->root_eid);
  return path;
}

svn_element__tree_t *
svn_element__tree_get_subtree_at_eid(svn_element__tree_t *element_tree,
                                     int eid,
                                     apr_pool_t *result_pool)
{
  svn_element__tree_t *new_subtree;
  svn_element__content_t *subtree_root_element;

  new_subtree = svn_element__tree_create(element_tree->e_map, eid,
                                         result_pool);

  /* Purge orphans */
  svn_element__tree_purge_orphans(new_subtree->e_map,
                                  new_subtree->root_eid, result_pool);

  /* Remove 'parent' and 'name' attributes from subtree root element */
  subtree_root_element
    = svn_element__tree_get(new_subtree, new_subtree->root_eid);
  svn_element__tree_set(new_subtree, new_subtree->root_eid,
                        svn_element__content_create(
                          -1, "", subtree_root_element->payload, result_pool));

  return new_subtree;
}

