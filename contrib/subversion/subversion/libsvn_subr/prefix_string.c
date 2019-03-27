/* prefix_string.c --- implement strings based on a prefix tree
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
#include "private/svn_string_private.h"

/* A node in the tree represents a common prefix.  The root node is the
 * empty prefix.  Nodes may have up to 256 sub-nodes, each starting with
 * a different character (possibly '\0').
 *
 * The nodes in the tree store only up to 8 chars of the respective common
 * prefix, i.e. longer common prefixes must be drawn out over multiple
 * hierarchy levels.  This is a space <-> efficiency trade-off.
 *
 * Strings are the leaf nodes in the tree and use a specialized, smaller
 * data structure.  They may add 0 to 7 extra chars to the prefix.  Both
 * data types can be discerned by the last char in the data buffer.  This
 * must be 0 for strings (leaves) and non-0 otherwise.  Please note that
 * ordinary nodes have a length information so that no terminating 0 is
 * required for them.
 */

/* forward declaration */
typedef struct node_t node_t;

/* String type and tree leaf.
 */
struct svn_prefix_string__t
{
  /* mandatory prefix */
  node_t *prefix;

  /* 0 ..7 chars to add the prefix.
   *
   * NUL-terminated, if this is indeed a tree leaf.  We use the same struct
   * within node_t for inner tree nodes, too.  There, DATA[7] is not NUL,
   * meaning DATA may or may not be NUL terminated.  The actual length is
   * provided by the node_t.length field (minus parent node length). */
  char data[8];
};

/* A node inside the tree, i.e. not a string and not a leaf (unless this is
 * the root node).
 *
 * Note: keep the ordering to minimize size / alignment overhead on 64 bit
 * machines.
 */
struct node_t
{
  /* pointer to the parent prefix plus the 1 .. 8 extra chars.
   * Only the root will provide 0 extra chars. */
  svn_prefix_string__t key;

  /* Length of the prefix from the root down to and including this one.
   * 0 for the root node.  Only then will key.prefix be NULL. */
  apr_uint32_t length;

  /* Number of entries used in SUB_NODES. */
  apr_uint32_t sub_node_count;

  /* The sub-nodes, ordered by first char.  node_t and svn_prefix_string__t
   * may be mixed here.  May be NULL.
   * The number of allocated entries is always a power-of-two and only
   * given implicitly by SUB_NODE_COUNT. */
  struct node_t **sub_nodes;
};

/* The actual tree structure. */
struct svn_prefix_tree__t
{
  /* the common tree root (represents the empty prefix). */
  node_t *root;

  /* all sub-nodes & strings will be allocated from this pool */
  apr_pool_t *pool;
};

/* Return TRUE, iff NODE is a leaf node.
 */
static svn_boolean_t
is_leaf(node_t *node)
{
  /* If this NOT a leaf node and this node has ...
   *    ... 8 chars, data[7] will not be NUL because we only support
   *        NUL-*terminated* strings.
   *    ... less than 8 chars, this will be set to 0xff
   *        (any other non-NUL would do as well but this is not valid UTF8
   *         making it easy to recognize during debugging etc.) */
  return node->key.data[7] == 0;
}

/* Ensure that the sub-nodes array of NODE within TREE has at least one
 * unused entry.  Re-allocate as necessary.
 */
static void
auto_realloc_sub_nodes(svn_prefix_tree__t *tree,
                       node_t *node)
{
  if (node->sub_node_count & (node->sub_node_count - 1))
    return;

  if (node->sub_node_count == 0)
    {
      node->sub_nodes = apr_pcalloc(tree->pool, sizeof(*node->sub_nodes));
    }
  else
    {
      node_t **sub_nodes
        = apr_pcalloc(tree->pool,
                      2 * node->sub_node_count * sizeof(*sub_nodes));
      memcpy(sub_nodes, node->sub_nodes,
             node->sub_node_count * sizeof(*sub_nodes));
      node->sub_nodes = sub_nodes;
    }
}

/* Given the COUNT pointers in the SUB_NODES array, return the location at
 * which KEY is either located or would be inserted.
 */
static int
search_lower_bound(node_t **sub_nodes,
                   unsigned char key,
                   int count)
{
  int lower = 0;
  int upper = count - 1;

  /* Binary search for the lowest position at which to insert KEY. */
  while (lower <= upper)
    {
      int current = lower + (upper - lower) / 2;

      if ((unsigned char)sub_nodes[current]->key.data[0] < key)
        lower = current + 1;
      else
        upper = current - 1;
    }

  return lower;
}

svn_prefix_tree__t *
svn_prefix_tree__create(apr_pool_t *pool)
{
  svn_prefix_tree__t *tree = apr_pcalloc(pool, sizeof(*tree));
  tree->pool = pool;

  tree->root = apr_pcalloc(pool, sizeof(*tree->root));
  tree->root->key.data[7] = '\xff'; /* This is not a leaf. See is_leaf(). */

  return tree;
}

svn_prefix_string__t *
svn_prefix_string__create(svn_prefix_tree__t *tree,
                          const char *s)
{
  svn_prefix_string__t *new_string;
  apr_size_t len = strlen(s);
  node_t *node = tree->root;
  node_t *new_node;
  int idx;

  /* walk the existing tree until we either find S or the node at which S
   * has to be inserted */
  while (TRUE)
    {
      node_t *sub_node;
      int match = 1;

      /* index of the matching sub-node */
      idx = node->sub_node_count
          ? search_lower_bound(node->sub_nodes,
                               (unsigned char)s[node->length],
                               node->sub_node_count)
          : 0;

      /* any (partially) matching sub-nodes? */
      if (idx == (int)node->sub_node_count
          || node->sub_nodes[idx]->key.data[0] != s[node->length])
        break;

      /* Yes, it matches - at least the first character does. */
      sub_node = node->sub_nodes[idx];

      /* fully matching sub-node? */
      if (is_leaf(sub_node))
        {
          if (strcmp(sub_node->key.data, s + node->length) == 0)
            return &sub_node->key;
        }
      else
        {
          /* The string formed by the path from the root down to
           * SUB_NODE differs from S.
           *
           * Is it a prefix?  In that case, the chars added by SUB_NODE
           * must fully match the respective chars in S. */
          apr_size_t sub_node_len = sub_node->length - node->length;
          if (strncmp(sub_node->key.data, s + node->length,
                      sub_node_len) == 0)
            {
              node = sub_node;
              continue;
            }
        }

      /* partial match -> split
       *
       * At this point, S may either be a prefix to the string represented
       * by SUB_NODE, or they may diverge at some offset with
       * SUB_NODE->KEY.DATA .
       *
       * MATCH starts with 1 here b/c we already know that at least one
       * char matches.  Also, the loop will terminate because the strings
       * differ before SUB_NODE->KEY.DATA - either at the NUL terminator
       * of S or some char before that.
       */
      while (sub_node->key.data[match] == s[node->length + match])
        ++match;

      new_node = apr_pcalloc(tree->pool, sizeof(*new_node));
      new_node->key = sub_node->key;
      new_node->length = node->length + match;
      new_node->key.data[7] = '\xff';  /* This is not a leaf. See is_leaf(). */
      new_node->sub_node_count = 1;
      new_node->sub_nodes = apr_palloc(tree->pool, sizeof(node_t *));
      new_node->sub_nodes[0] = sub_node;

      memmove(sub_node->key.data, sub_node->key.data + match, 8 - match);

      /* replace old sub-node with new one and continue lookup */
      sub_node->key.prefix = new_node;
      node->sub_nodes[idx] = new_node;
      node = new_node;
    }

  /* add sub-node(s) and final string */
  while (len - node->length > 7)
    {
      new_node = apr_pcalloc(tree->pool, sizeof(*new_node));
      new_node->key.prefix = node;
      new_node->length = node->length + 8;
      memcpy(new_node->key.data, s + node->length, 8);

      auto_realloc_sub_nodes(tree, node);
      memmove(node->sub_nodes + idx + 1, node->sub_nodes + idx,
              (node->sub_node_count - idx) * sizeof(node_t *));

      /* replace old sub-node with new one and continue lookup */
      node->sub_nodes[idx] = new_node;
      node->sub_node_count++;
      node = new_node;
      idx = 0;
    }

  new_string = apr_pcalloc(tree->pool, sizeof(*new_string));
  new_string->prefix = node;
  memcpy(new_string->data, s + node->length, len - node->length);

  auto_realloc_sub_nodes(tree, node);
  memmove(node->sub_nodes + idx + 1, node->sub_nodes + idx,
          (node->sub_node_count - idx) * sizeof(node_t *));

  node->sub_nodes[idx] = (node_t *)new_string;
  node->sub_node_count++;
  return new_string;
}

svn_string_t *
svn_prefix_string__expand(const svn_prefix_string__t *s,
                          apr_pool_t *pool)
{
  apr_size_t s_len = strlen(s->data);
  apr_size_t len = s->prefix->length + s_len;
  char *buffer = apr_palloc(pool, len + 1);

  svn_string_t *result = apr_pcalloc(pool, sizeof(*result));
  result->data = buffer;
  result->len = len;
  buffer[len] = '\0';

  while (s->prefix)
    {
      memcpy(buffer + s->prefix->length, s->data, len - s->prefix->length);
      len = s->prefix->length;
      s = &s->prefix->key;
    }

  return result;
}

int
svn_prefix_string__compare(const svn_prefix_string__t *lhs,
                           const svn_prefix_string__t *rhs)
{
  const node_t *lhs_parent = lhs->prefix;
  const node_t *rhs_parent = rhs->prefix;

  if (lhs == rhs)
    return 0;

  /* find the common root */
  while (lhs_parent != rhs_parent)
    {
      if (lhs_parent->length <= rhs_parent->length)
        {
          rhs = &rhs_parent->key;
          rhs_parent = rhs_parent->key.prefix;
        }
      else if (rhs_parent->length <= lhs_parent->length)
        {
          lhs = &lhs_parent->key;
          lhs_parent = lhs_parent->key.prefix;
        }

      /* same tree? */
      assert(lhs_parent && rhs_parent);
    }

  /* at the common root, strings will differ in the first follow-up char */
  return (int)(unsigned char)lhs->data[0] - (int)(unsigned char)rhs->data[0];
}
