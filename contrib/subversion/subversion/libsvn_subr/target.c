/*
 * target.c:  functions which operate on a list of targets supplied to
 *              a subversion subcommand.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"


/*** Code. ***/

svn_error_t *
svn_path_condense_targets(const char **pcommon,
                          apr_array_header_t **pcondensed_targets,
                          const apr_array_header_t *targets,
                          svn_boolean_t remove_redundancies,
                          apr_pool_t *pool)
{
  int i, j, num_condensed = targets->nelts;
  svn_boolean_t *removed;
  apr_array_header_t *abs_targets;
  size_t basedir_len;
  const char *first_target;
  svn_boolean_t first_target_is_url;

  /* Early exit when there's no data to work on. */
  if (targets->nelts <= 0)
    {
      *pcommon = NULL;
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
      return SVN_NO_ERROR;
    }

  /* Get the absolute path of the first target. */
  first_target = APR_ARRAY_IDX(targets, 0, const char *);
  first_target_is_url = svn_path_is_url(first_target);
  if (first_target_is_url)
    {
      first_target = apr_pstrdup(pool, first_target);
      *pcommon = first_target;
    }
  else
    SVN_ERR(svn_dirent_get_absolute(pcommon, first_target, pool));

  /* Early exit when there's only one path to work on. */
  if (targets->nelts == 1)
    {
      if (pcondensed_targets)
        *pcondensed_targets = apr_array_make(pool, 0, sizeof(const char *));
      return SVN_NO_ERROR;
    }

  /* Copy the targets array, but with absolute paths instead of
     relative.  Also, find the pcommon argument by finding what is
     common in all of the absolute paths. NOTE: This is not as
     efficient as it could be.  The calculation of the basedir could
     be done in the loop below, which would save some calls to
     svn_path_get_longest_ancestor.  I decided to do it this way
     because I thought it would be simpler, since this way, we don't
     even do the loop if we don't need to condense the targets. */

  removed = apr_pcalloc(pool, (targets->nelts * sizeof(svn_boolean_t)));
  abs_targets = apr_array_make(pool, targets->nelts, sizeof(const char *));

  APR_ARRAY_PUSH(abs_targets, const char *) = *pcommon;

  for (i = 1; i < targets->nelts; ++i)
    {
      const char *rel = APR_ARRAY_IDX(targets, i, const char *);
      const char *absolute;
      svn_boolean_t is_url = svn_path_is_url(rel);

      if (is_url)
        absolute = apr_pstrdup(pool, rel); /* ### TODO: avoid pool dup? */
      else
        SVN_ERR(svn_dirent_get_absolute(&absolute, rel, pool));

      APR_ARRAY_PUSH(abs_targets, const char *) = absolute;

      /* If we've not already determined that there's no common
         parent, then continue trying to do so. */
      if (*pcommon && **pcommon)
        {
          /* If the is-url-ness of this target doesn't match that of
             the first target, our search for a common ancestor can
             end right here.  Otherwise, use the appropriate
             get-longest-ancestor function per the path type. */
          if (is_url != first_target_is_url)
            *pcommon = "";
          else if (first_target_is_url)
            *pcommon = svn_uri_get_longest_ancestor(*pcommon, absolute, pool);
          else
            *pcommon = svn_dirent_get_longest_ancestor(*pcommon, absolute,
                                                       pool);
        }
    }

  if (pcondensed_targets != NULL)
    {
      if (remove_redundancies)
        {
          /* Find the common part of each pair of targets.  If
             common part is equal to one of the paths, the other
             is a child of it, and can be removed.  If a target is
             equal to *pcommon, it can also be removed. */

          /* First pass: when one non-removed target is a child of
             another non-removed target, remove the child. */
          for (i = 0; i < abs_targets->nelts; ++i)
            {
              if (removed[i])
                continue;

              for (j = i + 1; j < abs_targets->nelts; ++j)
                {
                  const char *abs_targets_i;
                  const char *abs_targets_j;
                  svn_boolean_t i_is_url, j_is_url;
                  const char *ancestor;

                  if (removed[j])
                    continue;

                  abs_targets_i = APR_ARRAY_IDX(abs_targets, i, const char *);
                  abs_targets_j = APR_ARRAY_IDX(abs_targets, j, const char *);
                  i_is_url = svn_path_is_url(abs_targets_i);
                  j_is_url = svn_path_is_url(abs_targets_j);

                  if (i_is_url != j_is_url)
                    continue;

                  if (i_is_url)
                    ancestor = svn_uri_get_longest_ancestor(abs_targets_i,
                                                            abs_targets_j,
                                                            pool);
                  else
                    ancestor = svn_dirent_get_longest_ancestor(abs_targets_i,
                                                               abs_targets_j,
                                                               pool);

                  if (*ancestor == '\0')
                    continue;

                  if (strcmp(ancestor, abs_targets_i) == 0)
                    {
                      removed[j] = TRUE;
                      num_condensed--;
                    }
                  else if (strcmp(ancestor, abs_targets_j) == 0)
                    {
                      removed[i] = TRUE;
                      num_condensed--;
                    }
                }
            }

          /* Second pass: when a target is the same as *pcommon,
             remove the target. */
          for (i = 0; i < abs_targets->nelts; ++i)
            {
              const char *abs_targets_i = APR_ARRAY_IDX(abs_targets, i,
                                                        const char *);

              if ((strcmp(abs_targets_i, *pcommon) == 0) && (! removed[i]))
                {
                  removed[i] = TRUE;
                  num_condensed--;
                }
            }
        }

      /* Now create the return array, and copy the non-removed items */
      basedir_len = strlen(*pcommon);
      *pcondensed_targets = apr_array_make(pool, num_condensed,
                                           sizeof(const char *));

      for (i = 0; i < abs_targets->nelts; ++i)
        {
          const char *rel_item = APR_ARRAY_IDX(abs_targets, i, const char *);

          /* Skip this if it's been removed. */
          if (removed[i])
            continue;

          /* If a common prefix was found, condensed_targets are given
             relative to that prefix.  */
          if (basedir_len > 0)
            {
              /* Only advance our pointer past a path separator if
                 REL_ITEM isn't the same as *PCOMMON.

                 If *PCOMMON is a root path, basedir_len will already
                 include the closing '/', so never advance the pointer
                 here.
                 */
              rel_item += basedir_len;
              if (rel_item[0] &&
                  ! svn_dirent_is_root(*pcommon, basedir_len))
                rel_item++;
            }

          APR_ARRAY_PUSH(*pcondensed_targets, const char *)
            = apr_pstrdup(pool, rel_item);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_path_remove_redundancies(apr_array_header_t **pcondensed_targets,
                             const apr_array_header_t *targets,
                             apr_pool_t *pool)
{
  apr_pool_t *temp_pool;
  apr_array_header_t *abs_targets;
  apr_array_header_t *rel_targets;
  int i;

  if ((targets->nelts <= 0) || (! pcondensed_targets))
    {
      /* No targets or no place to store our work means this function
         really has nothing to do. */
      if (pcondensed_targets)
        *pcondensed_targets = NULL;
      return SVN_NO_ERROR;
    }

  /* Initialize our temporary pool. */
  temp_pool = svn_pool_create(pool);

  /* Create our list of absolute paths for our "keepers" */
  abs_targets = apr_array_make(temp_pool, targets->nelts,
                               sizeof(const char *));

  /* Create our list of untainted paths for our "keepers" */
  rel_targets = apr_array_make(pool, targets->nelts,
                               sizeof(const char *));

  /* For each target in our list we do the following:

     1.  Calculate its absolute path (ABS_PATH).
     2.  See if any of the keepers in ABS_TARGETS is a parent of, or
         is the same path as, ABS_PATH.  If so, we ignore this
         target.  If not, however, add this target's absolute path to
         ABS_TARGETS and its original path to REL_TARGETS.
  */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *rel_path = APR_ARRAY_IDX(targets, i, const char *);
      const char *abs_path;
      int j;
      svn_boolean_t is_url, keep_me;

      /* Get the absolute path for this target. */
      is_url = svn_path_is_url(rel_path);
      if (is_url)
        abs_path = rel_path;
      else
        SVN_ERR(svn_dirent_get_absolute(&abs_path, rel_path, temp_pool));

      /* For each keeper in ABS_TARGETS, see if this target is the
         same as or a child of that keeper. */
      keep_me = TRUE;
      for (j = 0; j < abs_targets->nelts; j++)
        {
          const char *keeper = APR_ARRAY_IDX(abs_targets, j, const char *);
          svn_boolean_t keeper_is_url = svn_path_is_url(keeper);
          const char *child_relpath;

          /* If KEEPER hasn't the same is-url-ness as ABS_PATH, we
             know they aren't equal and that one isn't the child of
             the other. */
          if (is_url != keeper_is_url)
            continue;

          /* Quit here if this path is the same as or a child of one of the
             keepers. */
          if (is_url)
            child_relpath = svn_uri_skip_ancestor(keeper, abs_path, temp_pool);
          else
            child_relpath = svn_dirent_skip_ancestor(keeper, abs_path);
          if (child_relpath)
            {
              keep_me = FALSE;
              break;
            }
        }

      /* If this is a new keeper, add its absolute path to ABS_TARGETS
         and its original path to REL_TARGETS. */
      if (keep_me)
        {
          APR_ARRAY_PUSH(abs_targets, const char *) = abs_path;
          APR_ARRAY_PUSH(rel_targets, const char *) = rel_path;
        }
    }

  /* Destroy our temporary pool. */
  svn_pool_destroy(temp_pool);

  /* Make sure we return the list of untainted keeper paths. */
  *pcondensed_targets = rel_targets;

  return SVN_NO_ERROR;
}
