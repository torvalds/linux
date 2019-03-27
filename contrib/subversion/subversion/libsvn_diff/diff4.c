/*
 * diff.c :  routines for doing diffs
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
#include "svn_types.h"

#include "diff.h"

/*
 * Variance adjustment rules:
 *
 * See notes/variance-adjusted-patching.html
 *
 * ###: Expand this comment to contain the full set of adjustment
 * ###: rules instead of pointing to a webpage.
 */

/*
 * In the text below consider the following:
 *
 * O     = Original
 * M     = Modified
 * L     = Latest
 * A     = Ancestor
 * X:Y   = diff between X and Y
 * X:Y:Z = 3-way diff between X, Y and Z
 * P     = O:L, possibly adjusted
 *
 * diff4 -- Variance adjusted diff algorithm
 *
 * 1. Create a diff O:L and call that P.
 *
 * 2. Morph P into a 3-way diff by performing the following
 *    transformation: O:L -> O:O:L.
 *
 * 3. Create a diff A:O.
 *
 * 4. Using A:O...
 *
 * #. Using M:A...
 *
 * #. Resolve conflicts...
 *

   1. Out-range added line: decrement the line numbers in every hunk in P
      that comes after the addition. This undoes the effect of the add, since
      the add never happened in D.

   2. Out-range deleted line: increment the line numbers in every hunk in P
      that comes after the deletion. This undoes the effect of the deletion,
      since the deletion never happened in D.

   3. Out-range edited line: do nothing. Out-range edits are irrelevant to P.

   4. Added line in context range in P: remove the corresponding line from
      the context, optionally replacing it with new context based on that
      region in M, and adjust line numbers and mappings appropriately.

   5. Added line in affected text range in P: this is a dependency problem
      -- part of the change T:18-T:19 depends on changes introduced to T after
      B branched. There are several possible behaviors, depending on what the
      user wants. One is to generate an informative error, stating that
      T:18-T:19 depends on some other change (T:N-T:M, where N>=8, M<=18,
      and M-N == 1); the exact revisions can be discovered automatically using
      the same process as "cvs annotate", though it may take some time to do
      so. Another option is to include the change in P, as an insertion of the
      "after" version of the text, and adjust line numbers and mappings
      accordingly. (And if all this isn't sounding a lot like a directory
      merge algorithm, try drinking more of the Kool-Aid.) A third option is
      to include it as an insertion, but with metadata (such as CVS-style
      conflict markers) indicating that the line attempting to be patched
      does not exist in B.

   6. Deleted line that is in-range in P: request another universe -- this
      situation can't happen in ours.

   7. In-range edited line: reverse that edit in the "before" version of the
      corresponding line in the appropriate hunk in P, to obtain the version of
      the line that will be found in B when P is applied.
*/


static void
adjust_diff(svn_diff_t *diff, svn_diff_t *adjust)
{
  svn_diff_t *hunk;
  apr_off_t range_start;
  apr_off_t range_end;
  apr_off_t adjustment;

  for (; adjust; adjust = adjust->next)
    {
      range_start = adjust->modified_start;
      range_end = range_start + adjust->modified_length;
      adjustment = adjust->original_length - adjust->modified_length;

      /* No change in line count, so no modifications. [3, 7] */
      if (adjustment == 0)
        continue;

      for (hunk = diff; hunk; hunk = hunk->next)
        {
          /* Changes are in the range before this hunk.  Adjust the start
           * of the hunk. [1, 2]
           */
          if (hunk->modified_start >= range_end)
            {
              hunk->modified_start += adjustment;
              continue;
            }

          /* Changes are in the range beyond this hunk.  No adjustments
           * needed. [1, 2]
           */
          if (hunk->modified_start + hunk->modified_length <= range_start)
            continue;

          /* From here on changes are in the range of this hunk. */

          /* This is a context hunk.  Adjust the length. [4]
           */
          if (hunk->type == svn_diff__type_diff_modified)
            {
              hunk->modified_length += adjustment;
              continue;
            }

          /* Mark as conflicted. This happens in the reverse case when a line
           * is added in range and in the forward case when a line is deleted
           * in range. [5 (reverse), 6 (forward)]
           */
          if (adjustment < 0)
              hunk->type = svn_diff__type_conflict;

          /* Adjust the length of this hunk (reverse the change). [5, 6] */
          hunk->modified_length -= adjustment;
        }
    }
}

svn_error_t *
svn_diff_diff4_2(svn_diff_t **diff,
                 void *diff_baton,
                 const svn_diff_fns2_t *vtable,
                 apr_pool_t *pool)
{
  svn_diff__tree_t *tree;
  svn_diff__position_t *position_list[4];
  svn_diff__token_index_t num_tokens;
  svn_diff__token_index_t *token_counts[4];
  svn_diff_datasource_e datasource[] = {svn_diff_datasource_original,
                                        svn_diff_datasource_modified,
                                        svn_diff_datasource_latest,
                                        svn_diff_datasource_ancestor};
  svn_diff__lcs_t *lcs_ol;
  svn_diff__lcs_t *lcs_adjust;
  svn_diff_t *diff_ol;
  svn_diff_t *diff_adjust;
  svn_diff_t *hunk;
  apr_pool_t *subpool;
  apr_pool_t *subpool2;
  apr_pool_t *subpool3;
  apr_off_t prefix_lines = 0;
  apr_off_t suffix_lines = 0;

  *diff = NULL;

  subpool = svn_pool_create(pool);
  subpool2 = svn_pool_create(subpool);
  subpool3 = svn_pool_create(subpool2);

  svn_diff__tree_create(&tree, subpool3);

  SVN_ERR(vtable->datasources_open(diff_baton, &prefix_lines, &suffix_lines,
                                   datasource, 4));

  SVN_ERR(svn_diff__get_tokens(&position_list[0],
                               tree,
                               diff_baton, vtable,
                               svn_diff_datasource_original,
                               prefix_lines,
                               subpool2));

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

  SVN_ERR(svn_diff__get_tokens(&position_list[3],
                               tree,
                               diff_baton, vtable,
                               svn_diff_datasource_ancestor,
                               prefix_lines,
                               subpool2));

  num_tokens = svn_diff__get_node_count(tree);

  /* Get rid of the tokens, we don't need them to calc the diff */
  if (vtable->token_discard_all != NULL)
    vtable->token_discard_all(diff_baton);

  /* We don't need the nodes in the tree either anymore, nor the tree itself */
  svn_pool_clear(subpool3);

  token_counts[0] = svn_diff__get_token_counts(position_list[0], num_tokens,
                                               subpool);
  token_counts[1] = svn_diff__get_token_counts(position_list[1], num_tokens,
                                               subpool);
  token_counts[2] = svn_diff__get_token_counts(position_list[2], num_tokens,
                                               subpool);
  token_counts[3] = svn_diff__get_token_counts(position_list[3], num_tokens,
                                               subpool);

  /* Get the lcs for original - latest */
  lcs_ol = svn_diff__lcs(position_list[0], position_list[2],
                         token_counts[0], token_counts[2],
                         num_tokens, prefix_lines,
                         suffix_lines, subpool3);
  diff_ol = svn_diff__diff(lcs_ol, 1, 1, TRUE, pool);

  svn_pool_clear(subpool3);

  for (hunk = diff_ol; hunk; hunk = hunk->next)
    {
      hunk->latest_start = hunk->modified_start;
      hunk->latest_length = hunk->modified_length;
      hunk->modified_start = hunk->original_start;
      hunk->modified_length = hunk->original_length;

      if (hunk->type == svn_diff__type_diff_modified)
          hunk->type = svn_diff__type_diff_latest;
      else
          hunk->type = svn_diff__type_diff_modified;
    }

  /* Get the lcs for common ancestor - original
   * Do reverse adjustments
   */
  lcs_adjust = svn_diff__lcs(position_list[3], position_list[2],
                             token_counts[3], token_counts[2],
                             num_tokens, prefix_lines,
                             suffix_lines, subpool3);
  diff_adjust = svn_diff__diff(lcs_adjust, 1, 1, FALSE, subpool3);
  adjust_diff(diff_ol, diff_adjust);

  svn_pool_clear(subpool3);

  /* Get the lcs for modified - common ancestor
   * Do forward adjustments
   */
  lcs_adjust = svn_diff__lcs(position_list[1], position_list[3],
                             token_counts[1], token_counts[3],
                             num_tokens, prefix_lines,
                             suffix_lines, subpool3);
  diff_adjust = svn_diff__diff(lcs_adjust, 1, 1, FALSE, subpool3);
  adjust_diff(diff_ol, diff_adjust);

  /* Get rid of the position lists for original and ancestor, and delete
   * our scratchpool.
   */
  svn_pool_destroy(subpool2);

  /* Now we try and resolve the conflicts we encountered */
  for (hunk = diff_ol; hunk; hunk = hunk->next)
    {
      if (hunk->type == svn_diff__type_conflict)
        {
          svn_diff__resolve_conflict(hunk, &position_list[1],
                                     &position_list[2], num_tokens, pool);
        }
    }

  svn_pool_destroy(subpool);

  *diff = diff_ol;

  return SVN_NO_ERROR;
}
