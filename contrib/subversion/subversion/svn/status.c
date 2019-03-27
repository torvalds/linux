/*
 * status.c:  the command-line's portion of the "svn status" command
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
#include "svn_hash.h"
#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_dirent_uri.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "cl.h"
#include "svn_private_config.h"
#include "cl-conflicts.h"
#include "private/svn_wc_private.h"

/* Return the single character representation of STATUS */
static char
generate_status_code(enum svn_wc_status_kind status)
{
  switch (status)
    {
    case svn_wc_status_none:        return ' ';
    case svn_wc_status_normal:      return ' ';
    case svn_wc_status_added:       return 'A';
    case svn_wc_status_missing:     return '!';
    case svn_wc_status_incomplete:  return '!';
    case svn_wc_status_deleted:     return 'D';
    case svn_wc_status_replaced:    return 'R';
    case svn_wc_status_modified:    return 'M';
    case svn_wc_status_conflicted:  return 'C';
    case svn_wc_status_obstructed:  return '~';
    case svn_wc_status_ignored:     return 'I';
    case svn_wc_status_external:    return 'X';
    case svn_wc_status_unversioned: return '?';
    default:                        return '?';
    }
}

/* Return the combined STATUS as shown in 'svn status' based
   on the node status and text status */
static enum svn_wc_status_kind
combined_status(const svn_client_status_t *status)
{
  enum svn_wc_status_kind new_status = status->node_status;

  switch (status->node_status)
    {
      case svn_wc_status_conflicted:
        if (!status->versioned && status->conflicted)
          {
            /* Report unversioned tree conflict victims as missing: '!' */
            new_status = svn_wc_status_missing;
            break;
          }
        /* fall through */
      case svn_wc_status_modified:
        /* This value might be the property status */
        new_status = status->text_status;
        break;
      default:
        break;
    }

  return new_status;
}

/* Return the combined repository STATUS as shown in 'svn status' based
   on the repository node status and repository text status */
static enum svn_wc_status_kind
combined_repos_status(const svn_client_status_t *status)
{
  if (status->repos_node_status == svn_wc_status_modified)
    return status->repos_text_status;

  return status->repos_node_status;
}

/* Return the single character representation of the switched column
   status. */
static char
generate_switch_column_code(const svn_client_status_t *status)
{
  if (status->switched)
    return 'S';
  else if (status->file_external)
    return 'X';
  else
    return ' ';
}

/* Return the detailed string representation of STATUS */
static const char *
generate_status_desc(enum svn_wc_status_kind status)
{
  switch (status)
    {
    case svn_wc_status_none:        return "none";
    case svn_wc_status_normal:      return "normal";
    case svn_wc_status_added:       return "added";
    case svn_wc_status_missing:     return "missing";
    case svn_wc_status_incomplete:  return "incomplete";
    case svn_wc_status_deleted:     return "deleted";
    case svn_wc_status_replaced:    return "replaced";
    case svn_wc_status_modified:    return "modified";
    case svn_wc_status_conflicted:  return "conflicted";
    case svn_wc_status_obstructed:  return "obstructed";
    case svn_wc_status_ignored:     return "ignored";
    case svn_wc_status_external:    return "external";
    case svn_wc_status_unversioned: return "unversioned";
    default:
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}

/* Make a relative path containing '..' elements as needed.
   TARGET_ABSPATH shall be the absolute version of TARGET_PATH.
   TARGET_ABSPATH, TARGET_PATH and LOCAL_ABSPATH shall be canonical

   If above conditions are met, a relative path that leads to PATH
   from TARGET_PATH is returned, but there is no error checking involved.

   The returned path is allocated from RESULT_POOL, all other
   allocations are made in SCRATCH_POOL.

   Note that it is not possible to just join the resulting path to
   reconstruct the real path as the "../" paths are relative from
   a different base than the normal relative paths!
 */
static const char *
make_relpath(const char *target_abspath,
             const char *target_path,
             const char *local_abspath,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  const char *la;
  const char *parent_dir_els = "";
  const char *t_relpath;
  const char *p_relpath;

#ifdef SVN_DEBUG
  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_abspath));
#endif

  t_relpath = svn_dirent_skip_ancestor(target_abspath, local_abspath);

  if (t_relpath)
    return svn_dirent_join(target_path, t_relpath, result_pool);

  /* An example:
   *  relative_to_path = /a/b/c
   *  path             = /a/x/y/z
   *  result           = ../../x/y/z
   *
   * Another example (Windows specific):
   *  relative_to_path = F:/wc
   *  path             = C:/wc
   *  result           = C:/wc
   */
  /* Skip the common ancestor of both paths, here '/a'. */
  la = svn_dirent_get_longest_ancestor(target_abspath, local_abspath,
                                       scratch_pool);
  if (*la == '\0')
    {
      /* Nothing in common: E.g. C:/ vs F:/ on Windows */
      return apr_pstrdup(result_pool, local_abspath);
    }
  t_relpath = svn_dirent_skip_ancestor(la, target_abspath);
  p_relpath = svn_dirent_skip_ancestor(la, local_abspath);

  /* In above example, we'd now have:
   *  relative_to_path = b/c
   *  path             = x/y/z */

  /* Count the elements of relative_to_path and prepend as many '..' elements
   * to path. */
  while (*t_relpath)
    {
      t_relpath = svn_dirent_dirname(t_relpath, scratch_pool);
      parent_dir_els = svn_dirent_join(parent_dir_els, "..", scratch_pool);
    }

  /* This returns a ../ style path relative from the status target */
  return svn_dirent_join(parent_dir_els, p_relpath, result_pool);
}


/* Print STATUS and PATH in a format determined by DETAILED and
   SHOW_LAST_COMMITTED. */
static svn_error_t *
print_status(const char *target_abspath,
             const char *target_path,
             const char *path,
             svn_boolean_t detailed,
             svn_boolean_t show_last_committed,
             svn_boolean_t repos_locks,
             const svn_client_status_t *status,
             unsigned int *text_conflicts,
             unsigned int *prop_conflicts,
             unsigned int *tree_conflicts,
             svn_client_ctx_t *ctx,
             apr_pool_t *pool)
{
  enum svn_wc_status_kind node_status = status->node_status;
  enum svn_wc_status_kind prop_status = status->prop_status;
  char tree_status_code = ' ';
  const char *tree_desc_line = "";
  const char *moved_from_line = "";
  const char *moved_to_line = "";

  /* For historic reasons svn ignores the property status for added nodes, even
     if these nodes were copied and have local property changes.

     Note that it doesn't do this on replacements, or children of copies.

     ### Our test suite would catch more errors if we reported property
         changes on copies. */
  if (node_status == svn_wc_status_added)
      prop_status = svn_wc_status_none;

  /* To indicate this node is the victim of a tree conflict, we show
     'C' in the tree-conflict column, overriding any other status.
     We also print a separate line describing the nature of the tree
     conflict. */
  if (status->conflicted)
    {
      const char *desc;
      const char *local_abspath = status->local_abspath;
      svn_boolean_t text_conflicted;
      svn_boolean_t prop_conflicted;
      svn_boolean_t tree_conflicted;

      if (status->versioned)
        {
          svn_error_t *err;

          err = svn_wc_conflicted_p3(&text_conflicted,
                                     &prop_conflicted,
                                     &tree_conflicted, ctx->wc_ctx,
                                     local_abspath, pool);

          if (err && err->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED)
            {
              svn_error_clear(err);
              text_conflicted = FALSE;
              prop_conflicted = FALSE;
              tree_conflicted = FALSE;
            }
          else
            SVN_ERR(err);
        }
      else
        {
          text_conflicted = FALSE;
          prop_conflicted = FALSE;
          tree_conflicted = TRUE;
        }

      if (tree_conflicted)
        {
          svn_client_conflict_t *tree_conflict;

          SVN_ERR(svn_client_conflict_get(&tree_conflict, local_abspath,
                                          ctx, pool, pool));
          tree_status_code = 'C';
          SVN_ERR(svn_cl__get_human_readable_tree_conflict_description(
                            &desc, tree_conflict, pool));
          tree_desc_line = apr_psprintf(pool, "\n      >   %s", desc);
          (*tree_conflicts)++;
        }
      else if (text_conflicted)
        (*text_conflicts)++;
      else if (prop_conflicted)
        (*prop_conflicts)++;
    }

  /* Note that moved-from and moved-to information is only available in STATUS
   * for (op-)roots of a move. Those are exactly the nodes we want to show
   * move info for in 'svn status'. See also comments in svn_wc_status3_t. */
  if (status->moved_from_abspath && status->moved_to_abspath &&
      strcmp(status->moved_from_abspath, status->moved_to_abspath) == 0)
    {
      const char *relpath;

      relpath = make_relpath(target_abspath, target_path,
                             status->moved_from_abspath,
                             pool, pool);
      relpath = svn_dirent_local_style(relpath, pool);
      moved_from_line = apr_pstrcat(pool, "\n        > ",
                                    apr_psprintf(pool,
                                                 _("swapped places with %s"),
                                                 relpath),
                                    SVN_VA_NULL);
    }
  else if (status->moved_from_abspath || status->moved_to_abspath)
    {
      const char *relpath;

      if (status->moved_from_abspath)
        {
          relpath = make_relpath(target_abspath, target_path,
                                 status->moved_from_abspath,
                                 pool, pool);
          relpath = svn_dirent_local_style(relpath, pool);
          moved_from_line = apr_pstrcat(pool, "\n        > ",
                                        apr_psprintf(pool, _("moved from %s"),
                                                     relpath),
                                        SVN_VA_NULL);
        }

      if (status->moved_to_abspath)
        {
          relpath = make_relpath(target_abspath, target_path,
                                 status->moved_to_abspath,
                                 pool, pool);
          relpath = svn_dirent_local_style(relpath, pool);
          moved_to_line = apr_pstrcat(pool, "\n        > ",
                                      apr_psprintf(pool, _("moved to %s"),
                                                   relpath),
                                      SVN_VA_NULL);
        }
    }

  path = svn_dirent_local_style(path, pool);

  if (detailed)
    {
      char ood_status, lock_status;
      const char *working_rev;

      if (! status->versioned)
        working_rev = "";
      else if (status->copied
               || ! SVN_IS_VALID_REVNUM(status->revision))
        working_rev = "-";
      else
        working_rev = apr_psprintf(pool, "%ld", status->revision);

      if (status->repos_node_status != svn_wc_status_none)
        ood_status = '*';
      else
        ood_status = ' ';

      if (repos_locks)
        {
          if (status->repos_lock)
            {
              if (status->lock)
                {
                  if (strcmp(status->repos_lock->token, status->lock->token)
                      == 0)
                    lock_status = 'K';
                  else
                    lock_status = 'T';
                }
              else
                lock_status = 'O';
            }
          else if (status->lock)
            lock_status = 'B';
          else
            lock_status = ' ';
        }
      else
        lock_status = (status->lock) ? 'K' : ' ';

      if (show_last_committed)
        {
          const char *commit_rev;
          const char *commit_author;

          if (SVN_IS_VALID_REVNUM(status->changed_rev))
            commit_rev = apr_psprintf(pool, "%ld", status->changed_rev);
          else if (status->versioned)
            commit_rev = " ? ";
          else
            commit_rev = "";

          if (status->changed_author)
            commit_author = status->changed_author;
          else if (status->versioned)
            commit_author = " ? ";
          else
            commit_author = "";

          SVN_ERR
            (svn_cmdline_printf(pool,
                                "%c%c%c%c%c%c%c %c %8s %8s %-12s %s%s%s%s\n",
                                generate_status_code(combined_status(status)),
                                generate_status_code(prop_status),
                                status->wc_is_locked ? 'L' : ' ',
                                status->copied ? '+' : ' ',
                                generate_switch_column_code(status),
                                lock_status,
                                tree_status_code,
                                ood_status,
                                working_rev,
                                commit_rev,
                                commit_author,
                                path,
                                moved_to_line,
                                moved_from_line,
                                tree_desc_line));
        }
      else
        SVN_ERR(
           svn_cmdline_printf(pool, "%c%c%c%c%c%c%c %c %8s   %s%s%s%s\n",
                              generate_status_code(combined_status(status)),
                              generate_status_code(prop_status),
                              status->wc_is_locked ? 'L' : ' ',
                              status->copied ? '+' : ' ',
                              generate_switch_column_code(status),
                              lock_status,
                              tree_status_code,
                              ood_status,
                              working_rev,
                              path,
                              moved_to_line,
                              moved_from_line,
                              tree_desc_line));
    }
  else
    SVN_ERR(
       svn_cmdline_printf(pool, "%c%c%c%c%c%c%c %s%s%s%s\n",
                          generate_status_code(combined_status(status)),
                          generate_status_code(prop_status),
                          status->wc_is_locked ? 'L' : ' ',
                          status->copied ? '+' : ' ',
                          generate_switch_column_code(status),
                          ((status->lock)
                           ? 'K' : ' '),
                          tree_status_code,
                          path,
                          moved_to_line,
                          moved_from_line,
                          tree_desc_line));

  return svn_cmdline_fflush(stdout);
}


svn_error_t *
svn_cl__print_status_xml(const char *target_abspath,
                         const char *target_path,
                         const char *path,
                         const svn_client_status_t *status,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);
  apr_hash_t *att_hash;
  const char *local_abspath = status->local_abspath;
  svn_boolean_t tree_conflicted = FALSE;

  if (status->node_status == svn_wc_status_none
      && status->repos_node_status == svn_wc_status_none)
    return SVN_NO_ERROR;

  if (status->conflicted)
    SVN_ERR(svn_wc_conflicted_p3(NULL, NULL, &tree_conflicted,
                                 ctx->wc_ctx, local_abspath, pool));

  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "entry",
                        "path", svn_dirent_local_style(path, pool),
                        SVN_VA_NULL);

  att_hash = apr_hash_make(pool);
  svn_hash_sets(att_hash, "item",
                generate_status_desc(combined_status(status)));

  svn_hash_sets(att_hash, "props",
                generate_status_desc(
                   (status->node_status != svn_wc_status_deleted)
                   ? status->prop_status
                   : svn_wc_status_none));
  if (status->wc_is_locked)
    svn_hash_sets(att_hash, "wc-locked", "true");
  if (status->copied)
    svn_hash_sets(att_hash, "copied", "true");
  if (status->switched)
    svn_hash_sets(att_hash, "switched", "true");
  if (status->file_external)
    svn_hash_sets(att_hash, "file-external", "true");
  if (status->versioned && ! status->copied)
    svn_hash_sets(att_hash, "revision",
                  apr_psprintf(pool, "%ld", status->revision));
  if (tree_conflicted)
    svn_hash_sets(att_hash, "tree-conflicted", "true");
  if (status->moved_from_abspath || status->moved_to_abspath)
    {
      const char *relpath;

      if (status->moved_from_abspath)
        {
          relpath = make_relpath(target_abspath, target_path,
                                 status->moved_from_abspath,
                                 pool, pool);
          relpath = svn_dirent_local_style(relpath, pool);
          svn_hash_sets(att_hash, "moved-from", relpath);
        }
      if (status->moved_to_abspath)
        {
          relpath = make_relpath(target_abspath, target_path,
                                 status->moved_to_abspath,
                                 pool, pool);
          relpath = svn_dirent_local_style(relpath, pool);
          svn_hash_sets(att_hash, "moved-to", relpath);
        }
    }
  svn_xml_make_open_tag_hash(&sb, pool, svn_xml_normal, "wc-status",
                             att_hash);

  if (SVN_IS_VALID_REVNUM(status->changed_rev))
    {
      svn_cl__print_xml_commit(&sb, status->changed_rev,
                               status->changed_author,
                               svn_time_to_cstring(status->changed_date,
                                                   pool),
                               pool);
    }

  if (status->lock)
    svn_cl__print_xml_lock(&sb, status->lock, pool);

  svn_xml_make_close_tag(&sb, pool, "wc-status");

  if (status->repos_node_status != svn_wc_status_none
      || status->repos_lock)
    {
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "repos-status",
                            "item",
                            generate_status_desc(combined_repos_status(status)),
                            "props",
                            generate_status_desc(status->repos_prop_status),
                            SVN_VA_NULL);
      if (status->repos_lock)
        svn_cl__print_xml_lock(&sb, status->repos_lock, pool);

      svn_xml_make_close_tag(&sb, pool, "repos-status");
    }

  svn_xml_make_close_tag(&sb, pool, "entry");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}

/* Called by status-cmd.c */
svn_error_t *
svn_cl__print_status(const char *target_abspath,
                     const char *target_path,
                     const char *path,
                     const svn_client_status_t *status,
                     svn_boolean_t suppress_externals_placeholders,
                     svn_boolean_t detailed,
                     svn_boolean_t show_last_committed,
                     svn_boolean_t skip_unrecognized,
                     svn_boolean_t repos_locks,
                     unsigned int *text_conflicts,
                     unsigned int *prop_conflicts,
                     unsigned int *tree_conflicts,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  if (! status
      || (skip_unrecognized
          && !(status->versioned
               || status->conflicted
               || status->node_status == svn_wc_status_external))
      || (status->node_status == svn_wc_status_none
          && status->repos_node_status == svn_wc_status_none))
    return SVN_NO_ERROR;

  /* If we're trying not to print boring "X  /path/to/external"
     lines..." */
  if (suppress_externals_placeholders)
    {
      /* ... skip regular externals unmodified in the repository. */
      if ((status->node_status == svn_wc_status_external)
          && (status->repos_node_status == svn_wc_status_none)
          && (! status->conflicted))
        return SVN_NO_ERROR;

      /* ... skip file externals that aren't modified locally or
         remotely, changelisted, or locked (in either sense of the
         word). */
      if ((status->file_external)
          && (status->repos_node_status == svn_wc_status_none)
          && ((status->node_status == svn_wc_status_normal)
              || (status->node_status == svn_wc_status_none))
          && ((status->prop_status == svn_wc_status_normal)
              || (status->prop_status == svn_wc_status_none))
          && (! status->changelist)
          && (! status->lock)
          && (! status->wc_is_locked)
          && (! status->conflicted))
        return SVN_NO_ERROR;
    }

  return print_status(target_abspath, target_path, path,
                      detailed, show_last_committed, repos_locks, status,
                      text_conflicts, prop_conflicts, tree_conflicts,
                      ctx, pool);
}
