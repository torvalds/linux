/*
 * compose_delta.c:  Delta window composition.
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

#include <apr_general.h>        /* For APR_INLINE */

#include "svn_delta.h"
#include "svn_pools.h"
#include "delta.h"

/* Define a MIN macro if this platform doesn't already have one. */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif


/* ==================================================================== */
/* Support for efficient small-block allocation from pools. */

/* The following structs will be allocated and freed often: */

/* A node in the range index tree. */
typedef struct range_index_node_t range_index_node_t;
struct range_index_node_t
{
  /* 'offset' and 'limit' define the range in the source window. */
  apr_size_t offset;
  apr_size_t limit;

  /* 'target_offset' is where that range is represented in the target. */
  apr_size_t target_offset;

  /* 'left' and 'right' link the node into a splay tree. */
  range_index_node_t *left, *right;

  /* 'prev' and 'next' link it into an ordered, doubly-linked list. */
  range_index_node_t *prev, *next;
};

/* A node in a list of ranges for source and target op copies. */
enum range_kind
  {
    range_from_source,
    range_from_target
  };

typedef struct range_list_node_t range_list_node_t;
struct range_list_node_t
{
  /* Where does the range come from?
     'offset' and 'limit' always refer to the "virtual" source data
     for the second delta window. For a target range, the actual
     offset to use for generating the target op is 'target_offset';
     that field isn't used by source ranges. */
  enum range_kind kind;

  /* 'offset' and 'limit' define the range. */
  apr_size_t offset;
  apr_size_t limit;

  /* 'target_offset' is the start of the range in the target. */
  apr_size_t target_offset;

  /* 'prev' and 'next' link the node into an ordered, doubly-linked list. */
  range_list_node_t *prev, *next;
};


/* This is what will be allocated: */
typedef union alloc_block_t alloc_block_t;
union alloc_block_t
{
  range_index_node_t index_node;
  range_list_node_t list_node;

  /* Links free blocks into a freelist. */
  alloc_block_t *next_free;
};


/* Allocate a block. */
static APR_INLINE void *
alloc_block(apr_pool_t *pool, alloc_block_t **free_list)
{
  alloc_block_t *block;
  if (*free_list == NULL)
    block = apr_palloc(pool, sizeof(*block));
  else
    {
      block = *free_list;
      *free_list = block->next_free;
    }
  return block;
}

/* Return the block back to the free list. */
static APR_INLINE void
free_block(void *ptr, alloc_block_t **free_list)
{
  /* Wrapper functions take care of type safety. */
  alloc_block_t *const block = ptr;
  block->next_free = *free_list;
  *free_list = block;
}



/* ==================================================================== */
/* Mapping offsets in the target streem to txdelta ops. */

typedef struct offset_index_t
{
  int length;
  apr_size_t *offs;
} offset_index_t;

/* Create an index mapping target stream offsets to delta ops in
   WINDOW. Allocate from POOL. */

static offset_index_t *
create_offset_index(const svn_txdelta_window_t *window, apr_pool_t *pool)
{
  offset_index_t *ndx = apr_palloc(pool, sizeof(*ndx));
  apr_size_t offset = 0;
  int i;

  ndx->length = window->num_ops;
  ndx->offs = apr_palloc(pool, (ndx->length + 1) * sizeof(*ndx->offs));

  for (i = 0; i < ndx->length; ++i)
    {
      ndx->offs[i] = offset;
      offset += window->ops[i].length;
    }
  ndx->offs[ndx->length] = offset;

  return ndx;
}

/* Find the index of the delta op thet defines that data at OFFSET in
   NDX. HINT is an arbitrary positin within NDX and doesn't even need
   to be valid. To effectively speed up the search, use the last result
   as hint because most lookups come as a sequence of decreasing values
   for OFFSET and they concentrate on the lower end of the array. */

static apr_size_t
search_offset_index(const offset_index_t *ndx,
                    apr_size_t offset,
                    apr_size_t hint)
{
  apr_size_t lo, hi, op;

  assert(offset < ndx->offs[ndx->length]);

  lo = 0;
  hi = ndx->length;

  /* If we got a valid hint, use it to reduce the range to cover.
     Note that this will only be useful if either the hint is a
     hit (i.e. equals the desired result) or narrows the range
     length by a factor larger than 2. */

  if (hint < hi)
    {
      if (offset < ndx->offs[hint])
        hi = hint;
      else if (offset < ndx->offs[hint+1])
        return hint;
      else
        lo = hint+1;
    }

  /* ordinary binary search */

  for (op = (lo + hi)/2; lo != hi; op = (lo + hi)/2)
    {
      if (offset < ndx->offs[op])
        hi = op;
      else
        lo = ++op;
    }

  --lo;
  assert(ndx->offs[lo] <= offset && offset < ndx->offs[lo + 1]);
  return lo;
}



/* ==================================================================== */
/* Mapping ranges in the source stream to ranges in the composed delta. */

/* The range index tree. */
typedef struct range_index_t
{
  range_index_node_t *tree;
  alloc_block_t *free_list;
  apr_pool_t *pool;
} range_index_t;

/* Create a range index tree. Allocate from POOL. */
static range_index_t *
create_range_index(apr_pool_t *pool)
{
  range_index_t *ndx = apr_palloc(pool, sizeof(*ndx));
  ndx->tree = NULL;
  ndx->pool = pool;
  ndx->free_list = NULL;
  return ndx;
}

/* Allocate a node for the range index tree. */
static range_index_node_t *
alloc_range_index_node(range_index_t *ndx,
                       apr_size_t offset,
                       apr_size_t limit,
                       apr_size_t target_offset)
{
  range_index_node_t *const node = alloc_block(ndx->pool, &ndx->free_list);
  node->offset = offset;
  node->limit = limit;
  node->target_offset = target_offset;
  node->left = node->right = NULL;
  node->prev = node->next = NULL;
  return node;
}

/* Free a node from the range index tree. */
static void
free_range_index_node(range_index_t *ndx, range_index_node_t *node)
{
  if (node->next)
    node->next->prev = node->prev;
  if (node->prev)
    node->prev->next = node->next;
  free_block(node, &ndx->free_list);
}


/* Splay the index tree, using OFFSET as the key. */

static void
splay_range_index(apr_size_t offset, range_index_t *ndx)
{
  range_index_node_t *tree = ndx->tree;
  range_index_node_t scratch_node;
  range_index_node_t *left, *right;

  if (tree == NULL)
    return;

  scratch_node.left = scratch_node.right = NULL;
  left = right = &scratch_node;

  for (;;)
    {
      if (offset < tree->offset)
        {
          if (tree->left != NULL
              && offset < tree->left->offset)
            {
              /* Right rotation */
              range_index_node_t *const node = tree->left;
              tree->left = node->right;
              node->right = tree;
              tree = node;
            }
          if (tree->left == NULL)
            break;

          /* Remember the right subtree */
          right->left = tree;
          right = tree;
          tree = tree->left;
        }
      else if (offset > tree->offset)
        {
          if (tree->right != NULL
              && offset > tree->right->offset)
            {
              /* Left rotation */
              range_index_node_t *const node = tree->right;
              tree->right = node->left;
              node->left = tree;
              tree = node;
            }
          if (tree->right == NULL)
            break;

          /* Remember the left subtree */
          left->right = tree;
          left = tree;
          tree = tree->right;
        }
      else
        break;
    }

  /* Link in the left and right subtrees */
  left->right = tree->left;
  right->left = tree->right;
  tree->left  = scratch_node.right;
  tree->right = scratch_node.left;

  /* The basic top-down splay is finished, but we may still need to
     turn the tree around. What we want is to put the node with the
     largest offset where node->offset <= offset at the top of the
     tree, so that we can insert the new data (or search for existing
     ranges) to the right of the root. This makes cleaning up the
     tree after an insert much simpler, and -- incidentally -- makes
     the whole range index magic work. */
  if (offset < tree->offset && tree->left != NULL)
    {
      if (tree->left->right == NULL)
        {
          /* A single right rotation is enough. */
          range_index_node_t *const node = tree->left;
          tree->left = node->right; /* Which is always NULL. */
          node->right = tree;
          tree = node;
        }
      else
        {
          /* Slide down to the rightmost node in the left subtree. */
          range_index_node_t **nodep = &tree->left;
          while ((*nodep)->right != NULL)
            nodep = &(*nodep)->right;

          /* Now move this node to root in one giant promotion. */
          right = tree;
          left = tree->left;
          tree = *nodep;
          *nodep = tree->left;
          right->left = tree->right; /* Which is always NULL, too. */
          tree->left = left;
          tree->right = right;
        }
    }

  /* Sanity check ... */
  assert((offset >= tree->offset)
         || ((tree->left == NULL)
             && (tree->prev == NULL)));
  ndx->tree = tree;
}


/* Remove all ranges from NDX that fall into the root's range.  To
   keep the range index as small as possible, we must also remove
   nodes that don't fall into the new range, but have become redundant
   because the new range overlaps the beginning of the next range.
   Like this:

       new-range: |-----------------|
         range-1:         |-----------------|
         range-2:                |--------------------|

   Before new-range was inserted, range-1 and range-2 were both
   necessary. Now the union of new-range and range-2 completely covers
   range-1, which has become redundant now.

   FIXME: But, of course, there's a catch. range-1 must still remain
   in the tree if we want to optimize the number of target copy ops in
   the case were a copy falls within range-1, but starts before
   range-2 and ends after new-range. */

static void
delete_subtree(range_index_t *ndx, range_index_node_t *node)
{
  if (node != NULL)
    {
      delete_subtree(ndx, node->left);
      delete_subtree(ndx, node->right);
      free_range_index_node(ndx, node);
    }
}

static void
clean_tree(range_index_t *ndx, apr_size_t limit)
{
  apr_size_t top_offset = limit + 1;
  range_index_node_t **nodep = &ndx->tree->right;
  while (*nodep != NULL)
    {
      range_index_node_t *const node = *nodep;
      apr_size_t const offset =
        (node->right != NULL && node->right->offset < top_offset
         ? node->right->offset
         : top_offset);

      if (node->limit <= limit
          || (node->offset < limit && offset < limit))
        {
          *nodep = node->right;
          node->right = NULL;
          delete_subtree(ndx, node);
        }
      else
        {
          top_offset = node->offset;
          nodep = &node->left;
        }
    }
}


/* Add a range [OFFSET, LIMIT) into NDX. If NDX already contains a
   range that encloses [OFFSET, LIMIT), do nothing. Otherwise, remove
   all ranges from NDX that are superseded by the new range.
   NOTE: The range index must be splayed to OFFSET! */

static void
insert_range(apr_size_t offset, apr_size_t limit, apr_size_t target_offset,
             range_index_t *ndx)
{
  range_index_node_t *node = NULL;

  if (ndx->tree == NULL)
    {
      node = alloc_range_index_node(ndx, offset, limit, target_offset);
      ndx->tree = node;
    }
  else
    {
      if (offset == ndx->tree->offset
          && limit > ndx->tree->limit)
        {
          ndx->tree->limit = limit;
          ndx->tree->target_offset = target_offset;
          clean_tree(ndx, limit);
        }
      else if (offset > ndx->tree->offset
               && limit > ndx->tree->limit)
        {
          /* We have to make the same sort of checks as clean_tree()
             does for superseded ranges. Have to merge these someday. */

          const svn_boolean_t insert_range_p =
            (!ndx->tree->next
             || ndx->tree->limit < ndx->tree->next->offset
             || limit > ndx->tree->next->limit);

          if (insert_range_p)
            {
              /* Again, we have to check if the new node and the one
                 to the left of the root override root's range. */
              if (ndx->tree->prev && ndx->tree->prev->limit > offset)
                {
                  /* Replace the data in the splayed node. */
                  ndx->tree->offset = offset;
                  ndx->tree->limit = limit;
                  ndx->tree->target_offset = target_offset;
                }
              else
                {
                  /* Insert the range to the right of the splayed node. */
                  node = alloc_range_index_node(ndx, offset, limit,
                                                target_offset);
                  if ((node->next = ndx->tree->next) != NULL)
                    node->next->prev = node;
                  ndx->tree->next = node;
                  node->prev = ndx->tree;

                  node->right = ndx->tree->right;
                  ndx->tree->right = NULL;
                  node->left = ndx->tree;
                  ndx->tree = node;
                }
              clean_tree(ndx, limit);
            }
          else
            /* Ignore the range */;
        }
      else if (offset < ndx->tree->offset)
        {
          assert(ndx->tree->left == NULL);

          /* Insert the range left of the splayed node */
          node = alloc_range_index_node(ndx, offset, limit, target_offset);
          node->left = node->prev = NULL;
          node->right = node->next = ndx->tree;
          ndx->tree = node->next->prev = node;
          clean_tree(ndx, limit);
        }
      else
        /* Ignore the range */;
    }
}



/* ==================================================================== */
/* Juggling with lists of ranges. */

/* Allocate a node and add it to the range list. LIST is the head of
   the range list, TAIL is the last node in the list. NDX holds the
   freelist; OFFSET, LIMIT and KIND are node data. */
static range_list_node_t *
alloc_range_list(range_list_node_t **list,
                 range_list_node_t **tail,
                 range_index_t *ndx,
                 enum range_kind kind,
                 apr_size_t offset,
                 apr_size_t limit,
                 apr_size_t target_offset)
{
  range_list_node_t *const node = alloc_block(ndx->pool, &ndx->free_list);
  node->kind = kind;
  node->offset = offset;
  node->limit = limit;
  node->target_offset = target_offset;
  if (*list == NULL)
    {
      node->prev = node->next = NULL;
      *list = *tail = node;
    }
  else
    {
      node->prev = *tail;
      node->next = NULL;
      (*tail)->next = node;
      *tail = node;
    }
  return *list;
}

/* Free a range list. LIST is the head of the list, NDX holds the freelist. */
static void
free_range_list(range_list_node_t *list, range_index_t *ndx)
{
  while (list)
    {
      range_list_node_t *const node = list;
      list = node->next;
      free_block(node, &ndx->free_list);
    }
}


/* Based on the data in NDX, build a list of ranges that cover
   [OFFSET, LIMIT) in the "virtual" source data.
   NOTE: The range index must be splayed to OFFSET! */

static range_list_node_t *
build_range_list(apr_size_t offset, apr_size_t limit, range_index_t *ndx)
{
  range_list_node_t *range_list = NULL;
  range_list_node_t *last_range = NULL;
  range_index_node_t *node = ndx->tree;

  while (offset < limit)
    {
      if (node == NULL)
        return alloc_range_list(&range_list, &last_range, ndx,
                                range_from_source,
                                offset, limit, 0);

      if (offset < node->offset)
        {
          if (limit <= node->offset)
            return alloc_range_list(&range_list, &last_range, ndx,
                                    range_from_source,
                                    offset, limit, 0);
          else
            {
              alloc_range_list(&range_list, &last_range, ndx,
                               range_from_source,
                               offset, node->offset, 0);
              offset = node->offset;
            }
        }
      else
        {
          /* TODO: (Potential optimization) Investigate if it would
             make sense to forbid short range_from_target lengths
             (this comment originally said "shorter than, say,
             VD_KEY_SIZE (see vdelta.c)", but Subversion no longer
             uses vdelta). */

          if (offset >= node->limit)
            node = node->next;
          else
            {
              const apr_size_t target_offset =
                offset - node->offset + node->target_offset;

              if (limit <= node->limit)
                return alloc_range_list(&range_list, &last_range, ndx,
                                        range_from_target,
                                        offset, limit, target_offset);
              else
                {
                  alloc_range_list(&range_list, &last_range, ndx,
                                   range_from_target,
                                   offset, node->limit, target_offset);
                  offset = node->limit;
                  node = node->next;
                }
            }
        }
    }

  /* A range's offset isn't smaller than its limit? Impossible! */
  SVN_ERR_MALFUNCTION_NO_RETURN();
}


/* Copy the instructions from WINDOW that define the range [OFFSET,
   LIMIT) in WINDOW's target stream to TARGET_OFFSET in the window
   represented by BUILD_BATON. HINT is a position in the instructions
   array that helps finding the position for OFFSET. A safe default
   is 0. Use NDX to find the instructions in WINDOW. Allocate space
   in BUILD_BATON from POOL. */

static void
copy_source_ops(apr_size_t offset, apr_size_t limit,
                apr_size_t target_offset,
                apr_size_t hint,
                svn_txdelta__ops_baton_t *build_baton,
                const svn_txdelta_window_t *window,
                const offset_index_t *ndx,
                apr_pool_t *pool)
{
  apr_size_t op_ndx = search_offset_index(ndx, offset, hint);
  for (;; ++op_ndx)
    {
      const svn_txdelta_op_t *const op = &window->ops[op_ndx];
      const apr_size_t *const off = &ndx->offs[op_ndx];
      const apr_size_t fix_offset = (offset > off[0] ? offset - off[0] : 0);
      const apr_size_t fix_limit = (off[0] >= limit ? 0
                                    : (off[1] > limit ? off[1] - limit : 0));

      /* Ideally, we'd do this check before assigning fix_offset and
         fix_limit; but then we couldn't make them const whilst still
         adhering to C90 rules. Instead, we're going to assume that a
         smart optimizing compiler will reorder this check before the
         local variable initialization. */
      if (off[0] >= limit)
          break;

      /* It would be extremely weird if the fixed-up op had zero length. */
      assert(fix_offset + fix_limit < op->length);

      if (op->action_code != svn_txdelta_target)
        {
          /* Delta ops that don't depend on the virtual target can be
             copied to the composite unchanged. */
          const char *const new_data = (op->action_code == svn_txdelta_new
                                        ? (window->new_data->data
                                           + op->offset + fix_offset)
                                        : NULL);

          svn_txdelta__insert_op(build_baton, op->action_code,
                                 op->offset + fix_offset,
                                 op->length - fix_offset - fix_limit,
                                 new_data, pool);
        }
      else
        {
          /* The source of a target copy must start before the current
             offset in the (virtual) target stream. */
          assert(op->offset < off[0]);

          if (op->offset + op->length - fix_limit <= off[0])
            {
              /* The recursion _must_ end, otherwise the delta has
                 circular references, and that is not possible. */
              copy_source_ops(op->offset + fix_offset,
                              op->offset + op->length - fix_limit,
                              target_offset,
                              op_ndx,
                              build_baton, window, ndx, pool);
            }
          else
            {
              /* This is an overlapping target copy.
                 The idea here is to transpose the pattern, then generate
                 another overlapping copy. */
              const apr_size_t ptn_length = off[0] - op->offset;
              const apr_size_t ptn_overlap = fix_offset % ptn_length;
              apr_size_t fix_off = fix_offset;
              apr_size_t tgt_off = target_offset;
              assert(ptn_length > ptn_overlap);

              /* Unconditionally issue the second subrange of the
                 pattern. This is always correct, since the outer
                 condition already verifies that there is an overlap
                 in the target copy. */
              {
                const apr_size_t length =
                  MIN(op->length - fix_off - fix_limit,
                      ptn_length - ptn_overlap);
                copy_source_ops(op->offset + ptn_overlap,
                                op->offset + ptn_overlap + length,
                                tgt_off,
                                op_ndx,
                                build_baton, window, ndx, pool);
                fix_off += length;
                tgt_off += length;
              }

              assert(fix_off + fix_limit <= op->length);
              if (ptn_overlap > 0
                  && fix_off + fix_limit < op->length)
                {
                  /* Issue the first subrange in the pattern. */
                  const apr_size_t length =
                    MIN(op->length - fix_off - fix_limit, ptn_overlap);
                  copy_source_ops(op->offset,
                                  op->offset + length,
                                  tgt_off,
                                  op_ndx,
                                  build_baton, window, ndx, pool);
                  fix_off += length;
                  tgt_off += length;
                }

              assert(fix_off + fix_limit <= op->length);
              if (fix_off + fix_limit < op->length)
                {
                  /* Now multiply the pattern */
                  svn_txdelta__insert_op(build_baton, svn_txdelta_target,
                                         tgt_off - ptn_length,
                                         op->length - fix_off - fix_limit,
                                         NULL, pool);
                }
            }
        }

      /* Adjust the target offset for the next op in the list. */
      target_offset += op->length - fix_offset - fix_limit;
    }
}



/* ==================================================================== */
/* Bringing it all together. */


svn_txdelta_window_t *
svn_txdelta_compose_windows(const svn_txdelta_window_t *window_A,
                            const svn_txdelta_window_t *window_B,
                            apr_pool_t *pool)
{
  svn_txdelta__ops_baton_t build_baton = { 0 };
  svn_txdelta_window_t *composite;
  apr_pool_t *subpool = svn_pool_create(pool);
  offset_index_t *offset_index = create_offset_index(window_A, subpool);
  range_index_t *range_index = create_range_index(subpool);
  apr_size_t target_offset = 0;
  int i;

  /* Read the description of the delta composition algorithm in
     notes/fs-improvements.txt before going any further.
     You have been warned. */
  build_baton.new_data = svn_stringbuf_create_empty(pool);
  for (i = 0; i < window_B->num_ops; ++i)
    {
      const svn_txdelta_op_t *const op = &window_B->ops[i];
      if (op->action_code != svn_txdelta_source)
        {
          /* Delta ops that don't depend on the source can be copied
             to the composite unchanged. */
          const char *const new_data =
            (op->action_code == svn_txdelta_new
             ? window_B->new_data->data + op->offset
             : NULL);
          svn_txdelta__insert_op(&build_baton, op->action_code,
                                 op->offset, op->length,
                                 new_data, pool);
        }
      else
        {
          /* NOTE: Remember that `offset' and `limit' refer to
             positions in window_B's _source_ stream, which is the
             same as window_A's _target_ stream! */
          const apr_size_t offset = op->offset;
          const apr_size_t limit = op->offset + op->length;
          range_list_node_t *range_list, *range;
          apr_size_t tgt_off = target_offset;

          splay_range_index(offset, range_index);
          range_list = build_range_list(offset, limit, range_index);

          for (range = range_list; range; range = range->next)
            {
              if (range->kind == range_from_target)
                svn_txdelta__insert_op(&build_baton, svn_txdelta_target,
                                       range->target_offset,
                                       range->limit - range->offset,
                                       NULL, pool);
              else
                copy_source_ops(range->offset, range->limit, tgt_off, 0,
                                &build_baton, window_A, offset_index,
                                pool);

              tgt_off += range->limit - range->offset;
            }
          assert(tgt_off == target_offset + op->length);

          free_range_list(range_list, range_index);
          insert_range(offset, limit, target_offset, range_index);
        }

      /* Remember the new offset in the would-be target stream. */
      target_offset += op->length;
    }

  svn_pool_destroy(subpool);

  composite = svn_txdelta__make_window(&build_baton, pool);
  composite->sview_offset = window_A->sview_offset;
  composite->sview_len = window_A->sview_len;
  composite->tview_len = window_B->tview_len;
  return composite;
}
