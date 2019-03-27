/*
 * diff3.c :  routines for doing diffs
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

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_diff.h"
#include "svn_sorts.h"
#include "svn_types.h"

#include "diff.h"


void
svn_diff__resolve_conflict(svn_diff_t *hunk,
                           svn_diff__position_t **position_list1,
                           svn_diff__position_t **position_list2,
                           svn_diff__token_index_t num_tokens,
                           apr_pool_t *pool)
{
  apr_off_t modified_start = hunk->modified_start + 1;
  apr_off_t latest_start = hunk->latest_start + 1;
  apr_off_t common_length;
  apr_off_t modified_length = hunk->modified_length;
  apr_off_t latest_length = hunk->latest_length;
  svn_diff__position_t *start_position[2];
  svn_diff__position_t *position[2];
  svn_diff__token_index_t *token_counts[2];
  svn_diff__lcs_t *lcs = NULL;
  svn_diff__lcs_t **lcs_ref = &lcs;
  svn_diff_t **diff_ref = &hunk->resolved_diff;
  apr_pool_t *subpool;

  /* First find the starting positions for the
   * comparison
   */

  start_position[0] = *position_list1;
  start_position[1] = *position_list2;

  while (start_position[0]->offset < modified_start)
    start_position[0] = start_position[0]->next;

  while (start_position[1]->offset < latest_start)
    start_position[1] = start_position[1]->next;

  position[0] = start_position[0];
  position[1] = start_position[1];

  common_length = modified_length < latest_length
                ? modified_length : latest_length;

  while (common_length > 0
         && position[0]->token_index == position[1]->token_index)
    {
      position[0] = position[0]->next;
      position[1] = position[1]->next;

      common_length--;
    }

  if (common_length == 0
      && modified_length == latest_length)
    {
      hunk->type = svn_diff__type_diff_common;
      hunk->resolved_diff = NULL;

      *position_list1 = position[0];
      *position_list2 = position[1];

      return;
    }

  hunk->type = svn_diff__type_conflict;

  /* ### If we have a conflict we can try to find the
   * ### common parts in it by getting an lcs between
   * ### modified (start to start + length) and
   * ### latest (start to start + length).
   * ### We use this lcs to create a simple diff.  Only
   * ### where there is a diff between the two, we have
   * ### a conflict.
   * ### This raises a problem; several common diffs and
   * ### conflicts can occur within the same original
   * ### block.  This needs some thought.
   * ###
   * ### NB: We can use the node _pointers_ to identify
   * ###     different tokens
   */

  subpool = svn_pool_create(pool);

  /* Calculate how much of the two sequences was
   * actually the same.
   */
  common_length = (modified_length < latest_length
                  ? modified_length : latest_length)
                - common_length;

  /* If there were matching symbols at the start of
   * both sequences, record that fact.
   */
  if (common_length > 0)
    {
      lcs = apr_palloc(subpool, sizeof(*lcs));
      lcs->next = NULL;
      lcs->position[0] = start_position[0];
      lcs->position[1] = start_position[1];
      lcs->length = common_length;

      lcs_ref = &lcs->next;
    }

  modified_length -= common_length;
  latest_length -= common_length;

  modified_start = start_position[0]->offset;
  latest_start = start_position[1]->offset;

  start_position[0] = position[0];
  start_position[1] = position[1];

  /* Create a new ring for svn_diff__lcs to grok.
   * We can safely do this given we don't need the
   * positions we processed anymore.
   */
  if (modified_length == 0)
    {
      *position_list1 = position[0];
      position[0] = NULL;
    }
  else
    {
      while (--modified_length)
        position[0] = position[0]->next;

      *position_list1 = position[0]->next;
      position[0]->next = start_position[0];
    }

  if (latest_length == 0)
    {
      *position_list2 = position[1];
      position[1] = NULL;
    }
  else
    {
      while (--latest_length)
        position[1] = position[1]->next;

      *position_list2 = position[1]->next;
      position[1]->next = start_position[1];
    }

  token_counts[0] = svn_diff__get_token_counts(position[0], num_tokens,
                                               subpool);
  token_counts[1] = svn_diff__get_token_counts(position[1], num_tokens,
                                               subpool);

  *lcs_ref = svn_diff__lcs(position[0], position[1], token_counts[0],
                           token_counts[1], num_tokens, 0, 0, subpool);

  /* Fix up the EOF lcs element in case one of
   * the two sequences was NULL.
   */
  if ((*lcs_ref)->position[0]->offset == 1)
    (*lcs_ref)->position[0] = *position_list1;

  if ((*lcs_ref)->position[1]->offset == 1)
    (*lcs_ref)->position[1] = *position_list2;

  /* Produce the resolved diff */
  while (1)
    {
      if (modified_start < lcs->position[0]->offset
          || latest_start < lcs->position[1]->offset)
        {
          (*diff_ref) = apr_palloc(pool, sizeof(**diff_ref));

          (*diff_ref)->type = svn_diff__type_conflict;
          (*diff_ref)->original_start = hunk->original_start;
          (*diff_ref)->original_length = hunk->original_length;
          (*diff_ref)->modified_start = modified_start - 1;
          (*diff_ref)->modified_length = lcs->position[0]->offset
                                         - modified_start;
          (*diff_ref)->latest_start = latest_start - 1;
          (*diff_ref)->latest_length = lcs->position[1]->offset
                                       - latest_start;
          (*diff_ref)->resolved_diff = NULL;

          diff_ref = &(*diff_ref)->next;
        }

      /* Detect the EOF */
      if (lcs->length == 0)
        break;

      modified_start = lcs->position[0]->offset;
      latest_start = lcs->position[1]->offset;

      (*diff_ref) = apr_palloc(pool, sizeof(**diff_ref));

      (*diff_ref)->type = svn_diff__type_diff_common;
      (*diff_ref)->original_start = hunk->original_start;
      (*diff_ref)->original_length = hunk->original_length;
      (*diff_ref)->modified_start = modified_start - 1;
      (*diff_ref)->modified_length = lcs->length;
      (*diff_ref)->latest_start = latest_start - 1;
      (*diff_ref)->latest_length = lcs->length;
      (*diff_ref)->resolved_diff = NULL;

      diff_ref = &(*diff_ref)->next;

      modified_start += lcs->length;
      latest_start += lcs->length;

      lcs = lcs->next;
    }

  *diff_ref = NULL;

  svn_pool_destroy(subpool);
}


svn_error_t *
svn_diff_diff3_2(svn_diff_t **diff,
                 void *diff_baton,
                 const svn_diff_fns2_t *vtable,
                 apr_pool_t *pool)
{
  svn_diff__tree_t *tree;
  svn_diff__position_t *position_list[3];
  svn_diff__token_index_t num_tokens;
  svn_diff__token_index_t *token_counts[3];
  svn_diff_datasource_e datasource[] = {svn_diff_datasource_original,
                                        svn_diff_datasource_modified,
                                        svn_diff_datasource_latest};
  svn_diff__lcs_t *lcs_om;
  svn_diff__lcs_t *lcs_ol;
  apr_pool_t *subpool;
  apr_pool_t *treepool;
  apr_off_t prefix_lines = 0;
  apr_off_t suffix_lines = 0;

  *diff = NULL;

  subpool = svn_pool_create(pool);
  treepool = svn_pool_create(pool);

  svn_diff__tree_create(&tree, treepool);

  SVN_ERR(vtable->datasources_open(diff_baton, &prefix_lines, &suffix_lines,
                                   datasource, 3));

  SVN_ERR(svn_diff__get_tokens(&position_list[0],
                               tree,
                               diff_baton, vtable,
                               svn_diff_datasource_original,
                               prefix_lines,
                               subpool));

  SVN_ERR(svn_diff__get_tokens(&position_list[1],
                               tree,
                               diff_baton, vtable,
                               svn_diff_datasource_modified,
                               prefix_lines,
                               subpool));

  SVN_ERR(svn_diff__get_tokens(&position_list[2],
                               tree,
                               diff_baton, vtable,
                               svn_diff_datasource_latest,
                               prefix_lines,
                               subpool));

  num_tokens = svn_diff__get_node_count(tree);

  /* Get rid of the tokens, we don't need them to calc the diff */
  if (vtable->token_discard_all != NULL)
    vtable->token_discard_all(diff_baton);

  /* We don't need the nodes in the tree either anymore, nor the tree itself */
  svn_pool_destroy(treepool);

  token_counts[0] = svn_diff__get_token_counts(position_list[0], num_tokens,
                                               subpool);
  token_counts[1] = svn_diff__get_token_counts(position_list[1], num_tokens,
                                               subpool);
  token_counts[2] = svn_diff__get_token_counts(position_list[2], num_tokens,
                                               subpool);

  /* Get the lcs for original-modified and original-latest */
  lcs_om = svn_diff__lcs(position_list[0], position_list[1], token_counts[0],
                         token_counts[1], num_tokens, prefix_lines,
                         suffix_lines, subpool);
  lcs_ol = svn_diff__lcs(position_list[0], position_list[2], token_counts[0],
                         token_counts[2], num_tokens, prefix_lines,
                         suffix_lines, subpool);

  /* Produce a merged diff */
  {
    svn_diff_t **diff_ref = diff;

    apr_off_t original_start = 1;
    apr_off_t modified_start = 1;
    apr_off_t latest_start = 1;
    apr_off_t original_sync;
    apr_off_t modified_sync;
    apr_off_t latest_sync;
    apr_off_t common_length;
    apr_off_t modified_length;
    apr_off_t latest_length;
    svn_boolean_t is_modified;
    svn_boolean_t is_latest;
    svn_diff__position_t sentinel_position[2];

    /* Point the position lists to the start of the list
     * so that common_diff/conflict detection actually is
     * able to work.
     */
    if (position_list[1])
      {
        sentinel_position[0].next = position_list[1]->next;
        sentinel_position[0].offset = position_list[1]->offset + 1;
        position_list[1]->next = &sentinel_position[0];
        position_list[1] = sentinel_position[0].next;
      }
    else
      {
        sentinel_position[0].offset = prefix_lines + 1;
        sentinel_position[0].next = NULL;
        position_list[1] = &sentinel_position[0];
      }

    if (position_list[2])
      {
        sentinel_position[1].next = position_list[2]->next;
        sentinel_position[1].offset = position_list[2]->offset + 1;
        position_list[2]->next = &sentinel_position[1];
        position_list[2] = sentinel_position[1].next;
      }
    else
      {
        sentinel_position[1].offset = prefix_lines + 1;
        sentinel_position[1].next = NULL;
        position_list[2] = &sentinel_position[1];
      }

    while (1)
      {
        /* Find the sync points */
        while (1)
          {
            if (lcs_om->position[0]->offset > lcs_ol->position[0]->offset)
              {
                original_sync = lcs_om->position[0]->offset;

                while (lcs_ol->position[0]->offset + lcs_ol->length
                       < original_sync)
                  lcs_ol = lcs_ol->next;

                /* If the sync point is the EOF, and our current lcs segment
                 * doesn't reach as far as EOF, we need to skip this segment.
                 */
                if (lcs_om->length == 0 && lcs_ol->length > 0
                    && lcs_ol->position[0]->offset + lcs_ol->length
                       == original_sync
                    && lcs_ol->position[1]->offset + lcs_ol->length
                       != lcs_ol->next->position[1]->offset)
                  lcs_ol = lcs_ol->next;

                if (lcs_ol->position[0]->offset <= original_sync)
                    break;
              }
            else
              {
                original_sync = lcs_ol->position[0]->offset;

                while (lcs_om->position[0]->offset + lcs_om->length
                       < original_sync)
                  lcs_om = lcs_om->next;

                /* If the sync point is the EOF, and our current lcs segment
                 * doesn't reach as far as EOF, we need to skip this segment.
                 */
                if (lcs_ol->length == 0 && lcs_om->length > 0
                    && lcs_om->position[0]->offset + lcs_om->length
                       == original_sync
                    && lcs_om->position[1]->offset + lcs_om->length
                       != lcs_om->next->position[1]->offset)
                  lcs_om = lcs_om->next;

                if (lcs_om->position[0]->offset <= original_sync)
                    break;
              }
          }

        modified_sync = lcs_om->position[1]->offset
                      + (original_sync - lcs_om->position[0]->offset);
        latest_sync = lcs_ol->position[1]->offset
                    + (original_sync - lcs_ol->position[0]->offset);

        /* Determine what is modified, if anything */
        is_modified = lcs_om->position[0]->offset - original_start > 0
                      || lcs_om->position[1]->offset - modified_start > 0;

        is_latest = lcs_ol->position[0]->offset - original_start > 0
                    || lcs_ol->position[1]->offset - latest_start > 0;

        if (is_modified || is_latest)
          {
            modified_length = modified_sync - modified_start;
            latest_length = latest_sync - latest_start;

            (*diff_ref) = apr_palloc(pool, sizeof(**diff_ref));

            (*diff_ref)->original_start = original_start - 1;
            (*diff_ref)->original_length = original_sync - original_start;
            (*diff_ref)->modified_start = modified_start - 1;
            (*diff_ref)->modified_length = modified_length;
            (*diff_ref)->latest_start = latest_start - 1;
            (*diff_ref)->latest_length = latest_length;
            (*diff_ref)->resolved_diff = NULL;

            if (is_modified && is_latest)
              {
                svn_diff__resolve_conflict(*diff_ref,
                                           &position_list[1],
                                           &position_list[2],
                                           num_tokens,
                                           pool);
              }
            else if (is_modified)
              {
                (*diff_ref)->type = svn_diff__type_diff_modified;
              }
            else
              {
                (*diff_ref)->type = svn_diff__type_diff_latest;
              }

            diff_ref = &(*diff_ref)->next;
          }

        /* Detect EOF */
        if (lcs_om->length == 0 || lcs_ol->length == 0)
            break;

        modified_length = lcs_om->length
                          - (original_sync - lcs_om->position[0]->offset);
        latest_length = lcs_ol->length
                        - (original_sync - lcs_ol->position[0]->offset);
        common_length = MIN(modified_length, latest_length);

        if (common_length > 0)
          {
            (*diff_ref) = apr_palloc(pool, sizeof(**diff_ref));

            (*diff_ref)->type = svn_diff__type_common;
            (*diff_ref)->original_start = original_sync - 1;
            (*diff_ref)->original_length = common_length;
            (*diff_ref)->modified_start = modified_sync - 1;
            (*diff_ref)->modified_length = common_length;
            (*diff_ref)->latest_start = latest_sync - 1;
            (*diff_ref)->latest_length = common_length;
            (*diff_ref)->resolved_diff = NULL;

            diff_ref = &(*diff_ref)->next;
          }

        /* Set the new offsets */
        original_start = original_sync + common_length;
        modified_start = modified_sync + common_length;
        latest_start = latest_sync + common_length;

        /* Make it easier for diff_common/conflict detection
           by recording last lcs start positions
         */
        if (position_list[1]->offset < lcs_om->position[1]->offset)
          position_list[1] = lcs_om->position[1];

        if (position_list[2]->offset < lcs_ol->position[1]->offset)
          position_list[2] = lcs_ol->position[1];

        /* Make sure we are pointing to lcs entries beyond
         * the range we just processed
         */
        while (original_start >= lcs_om->position[0]->offset + lcs_om->length
               && lcs_om->length > 0)
          {
            lcs_om = lcs_om->next;
          }

        while (original_start >= lcs_ol->position[0]->offset + lcs_ol->length
               && lcs_ol->length > 0)
          {
            lcs_ol = lcs_ol->next;
          }
      }

    *diff_ref = NULL;
  }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}
