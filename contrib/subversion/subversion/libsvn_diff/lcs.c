/*
 * lcs.c :  routines for creating an lcs
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

#include "diff.h"


/*
 * Calculate the Longest Common Subsequence (LCS) between two datasources.
 * This function is what makes the diff code tick.
 *
 * The LCS algorithm implemented here is based on the approach described
 * by Sun Wu, Udi Manber and Gene Meyers in "An O(NP) Sequence Comparison
 * Algorithm", but has been modified for better performance.
 *
 * Let M and N be the lengths (number of tokens) of the two sources
 * ('files'). The goal is to reach the end of both sources (files) with the
 * minimum number of insertions + deletions. Since there is a known length
 * difference N-M between the files, that is equivalent to just the minimum
 * number of deletions, or equivalently the minimum number of insertions.
 * For symmetry, we use the lesser number - deletions if M<N, insertions if
 * M>N.
 *
 * Let 'k' be the difference in remaining length between the files, i.e.
 * if we're at the beginning of both files, k=N-M, whereas k=0 for the
 * 'end state', at the end of both files. An insertion will increase k by
 * one, while a deletion decreases k by one. If k<0, then insertions are
 * 'free' - we need those to reach the end state k=0 anyway - but deletions
 * are costly: Adding a deletion means that we will have to add an additional
 * insertion later to reach the end state, so it doesn't matter if we count
 * deletions or insertions. Similarly, deletions are free for k>0.
 *
 * Let a 'state' be a given position in each file {pos1, pos2}. An array
 * 'fp' keeps track of the best possible state (largest values of
 * {pos1, pos2}) that can be achieved for a given cost 'p' (# moves away
 * from k=0), as well as a linked list of what matches were used to reach
 * that state. For each new value of p, we find for each value of k the
 * best achievable state for that k - either by doing a costly operation
 * (deletion if k<0) from a state achieved at a lower p, or doing a free
 * operation (insertion if k<0) from a state achieved at the same p -
 * and in both cases advancing past any matching regions found. This is
 * handled by running loops over k in order of descending absolute value.
 *
 * A recent improvement of the algorithm is to ignore tokens that are unique
 * to one file or the other, as those are known from the start to be
 * impossible to match.
 */

typedef struct svn_diff__snake_t svn_diff__snake_t;

struct svn_diff__snake_t
{
    apr_off_t             y;
    svn_diff__lcs_t      *lcs;
    svn_diff__position_t *position[2];
};

static APR_INLINE void
svn_diff__snake(svn_diff__snake_t *fp_k,
                svn_diff__token_index_t *token_counts[2],
                svn_diff__lcs_t **freelist,
                apr_pool_t *pool)
{
  svn_diff__position_t *start_position[2];
  svn_diff__position_t *position[2];
  svn_diff__lcs_t *lcs;
  svn_diff__lcs_t *previous_lcs;

  /* The previous entry at fp[k] is going to be replaced.  See if we
   * can mark that lcs node for reuse, because the sequence up to this
   * point was a dead end.
   */
  lcs = fp_k[0].lcs;
  while (lcs)
    {
      lcs->refcount--;
      if (lcs->refcount)
        break;

      previous_lcs = lcs->next;
      lcs->next = *freelist;
      *freelist = lcs;
      lcs = previous_lcs;
    }

  if (fp_k[-1].y >= fp_k[1].y)
    {
      start_position[0] = fp_k[-1].position[0];
      start_position[1] = fp_k[-1].position[1]->next;

      previous_lcs = fp_k[-1].lcs;
    }
  else
    {
      start_position[0] = fp_k[1].position[0]->next;
      start_position[1] = fp_k[1].position[1];

      previous_lcs = fp_k[1].lcs;
    }


  if (previous_lcs)
    {
      previous_lcs->refcount++;
    }

  /* ### Optimization, skip all positions that don't have matchpoints
   * ### anyway. Beware of the sentinel, don't skip it!
   */

  position[0] = start_position[0];
  position[1] = start_position[1];

  while (1)
    {
      while (position[0]->token_index == position[1]->token_index)
        {
          position[0] = position[0]->next;
          position[1] = position[1]->next;
        }

      if (position[1] != start_position[1])
        {
          lcs = *freelist;
          if (lcs)
            {
              *freelist = lcs->next;
            }
          else
            {
              lcs = apr_palloc(pool, sizeof(*lcs));
            }

          lcs->position[0] = start_position[0];
          lcs->position[1] = start_position[1];
          lcs->length = position[1]->offset - start_position[1]->offset;
          lcs->next = previous_lcs;
          lcs->refcount = 1;
          previous_lcs = lcs;
          start_position[0] = position[0];
          start_position[1] = position[1];
        }

      /* Skip any and all tokens that only occur in one of the files */
      if (position[0]->token_index >= 0
          && token_counts[1][position[0]->token_index] == 0)
        start_position[0] = position[0] = position[0]->next;
      else if (position[1]->token_index >= 0
               && token_counts[0][position[1]->token_index] == 0)
        start_position[1] = position[1] = position[1]->next;
      else
        break;
    }

  fp_k[0].lcs = previous_lcs;
  fp_k[0].position[0] = position[0];
  fp_k[0].position[1] = position[1];

  fp_k[0].y = position[1]->offset;
}


static svn_diff__lcs_t *
svn_diff__lcs_reverse(svn_diff__lcs_t *lcs)
{
  svn_diff__lcs_t *next;
  svn_diff__lcs_t *prev;

  next = NULL;
  while (lcs != NULL)
    {
      prev = lcs->next;
      lcs->next = next;
      next = lcs;
      lcs = prev;
    }

  return next;
}


/* Prepends a new lcs chunk for the amount of LINES at the given positions
 * POS0_OFFSET and POS1_OFFSET to the given LCS chain, and returns it.
 * This function assumes LINES > 0. */
static svn_diff__lcs_t *
prepend_lcs(svn_diff__lcs_t *lcs, apr_off_t lines,
            apr_off_t pos0_offset, apr_off_t pos1_offset,
            apr_pool_t *pool)
{
  svn_diff__lcs_t *new_lcs;

  SVN_ERR_ASSERT_NO_RETURN(lines > 0);

  new_lcs = apr_palloc(pool, sizeof(*new_lcs));
  new_lcs->position[0] = apr_pcalloc(pool, sizeof(*new_lcs->position[0]));
  new_lcs->position[0]->offset = pos0_offset;
  new_lcs->position[1] = apr_pcalloc(pool, sizeof(*new_lcs->position[1]));
  new_lcs->position[1]->offset = pos1_offset;
  new_lcs->length = lines;
  new_lcs->refcount = 1;
  new_lcs->next = lcs;

  return new_lcs;
}


svn_diff__lcs_t *
svn_diff__lcs(svn_diff__position_t *position_list1, /* pointer to tail (ring) */
              svn_diff__position_t *position_list2, /* pointer to tail (ring) */
              svn_diff__token_index_t *token_counts_list1, /* array of counts */
              svn_diff__token_index_t *token_counts_list2, /* array of counts */
              svn_diff__token_index_t num_tokens,
              apr_off_t prefix_lines,
              apr_off_t suffix_lines,
              apr_pool_t *pool)
{
  apr_off_t length[2];
  svn_diff__token_index_t *token_counts[2];
  svn_diff__token_index_t unique_count[2];
  svn_diff__token_index_t token_index;
  svn_diff__snake_t *fp;
  apr_off_t d;
  apr_off_t k;
  apr_off_t p = 0;
  svn_diff__lcs_t *lcs, *lcs_freelist = NULL;

  svn_diff__position_t sentinel_position[2];

  /* Since EOF is always a sync point we tack on an EOF link
   * with sentinel positions
   */
  lcs = apr_palloc(pool, sizeof(*lcs));
  lcs->position[0] = apr_pcalloc(pool, sizeof(*lcs->position[0]));
  lcs->position[0]->offset = position_list1
                             ? position_list1->offset + suffix_lines + 1
                             : prefix_lines + suffix_lines + 1;
  lcs->position[1] = apr_pcalloc(pool, sizeof(*lcs->position[1]));
  lcs->position[1]->offset = position_list2
                             ? position_list2->offset + suffix_lines + 1
                             : prefix_lines + suffix_lines + 1;
  lcs->length = 0;
  lcs->refcount = 1;
  lcs->next = NULL;

  if (position_list1 == NULL || position_list2 == NULL)
    {
      if (suffix_lines)
        lcs = prepend_lcs(lcs, suffix_lines,
                          lcs->position[0]->offset - suffix_lines,
                          lcs->position[1]->offset - suffix_lines,
                          pool);
      if (prefix_lines)
        lcs = prepend_lcs(lcs, prefix_lines, 1, 1, pool);

      return lcs;
    }

  unique_count[1] = unique_count[0] = 0;
  for (token_index = 0; token_index < num_tokens; token_index++)
    {
      if (token_counts_list1[token_index] == 0)
        unique_count[1] += token_counts_list2[token_index];
      if (token_counts_list2[token_index] == 0)
        unique_count[0] += token_counts_list1[token_index];
    }

  /* Calculate lengths M and N of the sequences to be compared. Do not
   * count tokens unique to one file, as those are ignored in __snake.
   */
  length[0] = position_list1->offset - position_list1->next->offset + 1
              - unique_count[0];
  length[1] = position_list2->offset - position_list2->next->offset + 1
              - unique_count[1];

  /* strikerXXX: here we allocate the furthest point array, which is
   * strikerXXX: sized M + N + 3 (!)
   */
  fp = apr_pcalloc(pool,
                   sizeof(*fp) * (apr_size_t)(length[0] + length[1] + 3));

  /* The origo of fp corresponds to the end state, where we are
   * at the end of both files. The valid states thus span from
   * -N (at end of first file and at the beginning of the second
   * file) to +M (the opposite :). Finally, svn_diff__snake needs
   * 1 extra slot on each side to work.
   */
  fp += length[1] + 1;

  sentinel_position[0].next = position_list1->next;
  position_list1->next = &sentinel_position[0];
  sentinel_position[0].offset = position_list1->offset + 1;
  token_counts[0] = token_counts_list1;

  sentinel_position[1].next = position_list2->next;
  position_list2->next = &sentinel_position[1];
  sentinel_position[1].offset = position_list2->offset + 1;
  token_counts[1] = token_counts_list2;

  /* Negative indices will not be used elsewhere
   */
  sentinel_position[0].token_index = -1;
  sentinel_position[1].token_index = -2;

  /* position d = M - N corresponds to the initial state, where
   * we are at the beginning of both files.
   */
  d = length[0] - length[1];

  /* k = d - 1 will be the first to be used to get previous
   * position information from, make sure it holds sane
   * data
   */
  fp[d - 1].position[0] = sentinel_position[0].next;
  fp[d - 1].position[1] = &sentinel_position[1];

  p = 0;
  do
    {
      /* For k < 0, insertions are free */
      for (k = (d < 0 ? d : 0) - p; k < 0; k++)
        {
          svn_diff__snake(fp + k, token_counts, &lcs_freelist, pool);
        }
      /* for k > 0, deletions are free */
      for (k = (d > 0 ? d : 0) + p; k >= 0; k--)
        {
          svn_diff__snake(fp + k, token_counts, &lcs_freelist, pool);
        }

      p++;
    }
  while (fp[0].position[1] != &sentinel_position[1]);

  if (suffix_lines)
    lcs->next = prepend_lcs(fp[0].lcs, suffix_lines,
                            lcs->position[0]->offset - suffix_lines,
                            lcs->position[1]->offset - suffix_lines,
                            pool);
  else
    lcs->next = fp[0].lcs;

  lcs = svn_diff__lcs_reverse(lcs);

  position_list1->next = sentinel_position[0].next;
  position_list2->next = sentinel_position[1].next;

  if (prefix_lines)
    return prepend_lcs(lcs, prefix_lines, 1, 1, pool);
  else
    return lcs;
}
