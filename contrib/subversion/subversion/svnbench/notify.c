/*
 * notify.c:  feedback handlers for cmdline client.
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

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "cl.h"

#include "svn_private_config.h"


/* Baton for notify and friends. */
struct notify_baton
{
  svn_boolean_t received_some_change;
  svn_boolean_t is_checkout;
  svn_boolean_t is_export;
  svn_boolean_t is_wc_to_repos_copy;
  svn_boolean_t sent_first_txdelta;
  svn_boolean_t in_external;
  svn_boolean_t had_print_error; /* Used to not keep printing error messages
                                    when we've already had one print error. */

  /* Conflict stats for update and merge. */
  unsigned int text_conflicts;
  unsigned int prop_conflicts;
  unsigned int tree_conflicts;
  unsigned int skipped_paths;
  apr_hash_t *conflicted_paths;

  /* The cwd, for use in decomposing absolute paths. */
  const char *path_prefix;
};


/* Add a conflicted path to the list of conflicted paths stored
 * in the notify baton. */
static void
add_conflicted_path(struct notify_baton *nb, const char *path)
{
  apr_hash_set(nb->conflicted_paths,
               apr_pstrdup(apr_hash_pool_get(nb->conflicted_paths), path),
               APR_HASH_KEY_STRING, "");
}

/* This implements `svn_wc_notify_func2_t'.
 * NOTE: This function can't fail, so we just ignore any print errors. */
static void
notify(void *baton, const svn_wc_notify_t *n, apr_pool_t *pool)
{
  struct notify_baton *nb = baton;
  char statchar_buf[5] = "    ";
  const char *path_local;
  svn_error_t *err;

  if (n->url)
    path_local = n->url;
  else
    {
      if (n->path_prefix)
        path_local = svn_cl__local_style_skip_ancestor(n->path_prefix, n->path,
                                                       pool);
      else /* skip nb->path_prefix, if it's non-null */
        path_local = svn_cl__local_style_skip_ancestor(nb->path_prefix, n->path,
                                                       pool);
    }

  switch (n->action)
    {
    case svn_wc_notify_skip:
      nb->skipped_paths++;
      if (n->content_state == svn_wc_notify_state_missing)
        {
          if ((err = svn_cmdline_printf
               (pool, _("Skipped missing target: '%s'\n"),
                path_local)))
            goto print_error;
        }
      else if (n->content_state == svn_wc_notify_state_source_missing)
        {
          if ((err = svn_cmdline_printf
               (pool, _("Skipped target: '%s' -- copy-source is missing\n"),
                path_local)))
            goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf
               (pool, _("Skipped '%s'\n"), path_local)))
            goto print_error;
        }
      break;
    case svn_wc_notify_update_skip_obstruction:
      nb->skipped_paths++;
      if ((err = svn_cmdline_printf(
            pool, _("Skipped '%s' -- An obstructing working copy was found\n"),
            path_local)))
        goto print_error;
      break;
    case svn_wc_notify_update_skip_working_only:
      nb->skipped_paths++;
      if ((err = svn_cmdline_printf(
            pool, _("Skipped '%s' -- Has no versioned parent\n"),
            path_local)))
        goto print_error;
      break;
    case svn_wc_notify_update_skip_access_denied:
      nb->skipped_paths++;
      if ((err = svn_cmdline_printf(
            pool, _("Skipped '%s' -- Access denied\n"),
            path_local)))
        goto print_error;
      break;
    case svn_wc_notify_skip_conflicted:
      nb->skipped_paths++;
      if ((err = svn_cmdline_printf(
            pool, _("Skipped '%s' -- Node remains in conflict\n"),
            path_local)))
        goto print_error;
      break;
    case svn_wc_notify_update_delete:
    case svn_wc_notify_exclude:
      nb->received_some_change = TRUE;
      if ((err = svn_cmdline_printf(pool, "D    %s\n", path_local)))
        goto print_error;
      break;
    case svn_wc_notify_update_broken_lock:
      if ((err = svn_cmdline_printf(pool, "B    %s\n", path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_external_removed:
      nb->received_some_change = TRUE;
      if (n->err && n->err->message)
        {
          if ((err = svn_cmdline_printf(pool, "Removed external '%s': %s\n",
              path_local, n->err->message)))
            goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf(pool, "Removed external '%s'\n",
                                        path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_left_local_modifications:
      if ((err = svn_cmdline_printf(pool, "Left local modifications as '%s'\n",
                                        path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_replace:
      nb->received_some_change = TRUE;
      if ((err = svn_cmdline_printf(pool, "R    %s\n", path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_add:
      nb->received_some_change = TRUE;
      if (n->content_state == svn_wc_notify_state_conflicted)
        {
          nb->text_conflicts++;
          add_conflicted_path(nb, n->path);
          if ((err = svn_cmdline_printf(pool, "C    %s\n", path_local)))
            goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf(pool, "A    %s\n", path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_exists:
      nb->received_some_change = TRUE;
      if (n->content_state == svn_wc_notify_state_conflicted)
        {
          nb->text_conflicts++;
          add_conflicted_path(nb, n->path);
          statchar_buf[0] = 'C';
        }
      else
        statchar_buf[0] = 'E';

      if (n->prop_state == svn_wc_notify_state_conflicted)
        {
          nb->prop_conflicts++;
          add_conflicted_path(nb, n->path);
          statchar_buf[1] = 'C';
        }
      else if (n->prop_state == svn_wc_notify_state_merged)
        statchar_buf[1] = 'G';

      if ((err = svn_cmdline_printf(pool, "%s %s\n", statchar_buf, path_local)))
        goto print_error;
      break;

    case svn_wc_notify_restore:
      if ((err = svn_cmdline_printf(pool, _("Restored '%s'\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_revert:
      if ((err = svn_cmdline_printf(pool, _("Reverted '%s'\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_failed_revert:
      if (( err = svn_cmdline_printf(pool, _("Failed to revert '%s' -- "
                                             "try updating instead.\n"),
                                     path_local)))
        goto print_error;
      break;

    case svn_wc_notify_resolved:
      if ((err = svn_cmdline_printf(pool,
                                    _("Resolved conflicted state of '%s'\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_add:
      /* We *should* only get the MIME_TYPE if PATH is a file.  If we
         do get it, and the mime-type is not textual, note that this
         is a binary addition. */
      if (n->mime_type && (svn_mime_type_is_binary(n->mime_type)))
        {
          if ((err = svn_cmdline_printf(pool, "A  (bin)  %s\n",
                                        path_local)))
            goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf(pool, "A         %s\n",
                                        path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_delete:
      nb->received_some_change = TRUE;
      if ((err = svn_cmdline_printf(pool, "D         %s\n",
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_patch:
      {
        nb->received_some_change = TRUE;
        if (n->content_state == svn_wc_notify_state_conflicted)
          {
            nb->text_conflicts++;
            add_conflicted_path(nb, n->path);
            statchar_buf[0] = 'C';
          }
        else if (n->kind == svn_node_file)
          {
            if (n->content_state == svn_wc_notify_state_merged)
              statchar_buf[0] = 'G';
            else if (n->content_state == svn_wc_notify_state_changed)
              statchar_buf[0] = 'U';
          }

        if (n->prop_state == svn_wc_notify_state_conflicted)
          {
            nb->prop_conflicts++;
            add_conflicted_path(nb, n->path);
            statchar_buf[1] = 'C';
          }
        else if (n->prop_state == svn_wc_notify_state_changed)
              statchar_buf[1] = 'U';

        if (statchar_buf[0] != ' ' || statchar_buf[1] != ' ')
          {
            if ((err = svn_cmdline_printf(pool, "%s      %s\n",
                                          statchar_buf, path_local)))
              goto print_error;
          }
      }
      break;

    case svn_wc_notify_patch_applied_hunk:
      nb->received_some_change = TRUE;
      if (n->hunk_original_start != n->hunk_matched_line)
        {
          apr_uint64_t off;
          const char *s;
          const char *minus;

          if (n->hunk_matched_line > n->hunk_original_start)
            {
              off = n->hunk_matched_line - n->hunk_original_start;
              minus = "";
            }
          else
            {
              off = n->hunk_original_start - n->hunk_matched_line;
              minus = "-";
            }

          /* ### We're creating the localized strings without
           * ### APR_INT64_T_FMT since it isn't translator-friendly */
          if (n->hunk_fuzz)
            {

              if (n->prop_name)
                {
                  s = _(">         applied hunk ## -%lu,%lu +%lu,%lu ## "
                        "with offset %s");

                  err = svn_cmdline_printf(pool,
                                           apr_pstrcat(pool, s,
                                                       "%"APR_UINT64_T_FMT
                                                       " and fuzz %lu (%s)\n",
                                                       SVN_VA_NULL),
                                           n->hunk_original_start,
                                           n->hunk_original_length,
                                           n->hunk_modified_start,
                                           n->hunk_modified_length,
                                           minus, off, n->hunk_fuzz,
                                           n->prop_name);
                }
              else
                {
                  s = _(">         applied hunk @@ -%lu,%lu +%lu,%lu @@ "
                        "with offset %s");

                  err = svn_cmdline_printf(pool,
                                           apr_pstrcat(pool, s,
                                                       "%"APR_UINT64_T_FMT
                                                       " and fuzz %lu\n",
                                                       SVN_VA_NULL),
                                           n->hunk_original_start,
                                           n->hunk_original_length,
                                           n->hunk_modified_start,
                                           n->hunk_modified_length,
                                           minus, off, n->hunk_fuzz);
                }

              if (err)
                goto print_error;
            }
          else
            {

              if (n->prop_name)
                {
                  s = _(">         applied hunk ## -%lu,%lu +%lu,%lu ## "
                        "with offset %s");
                  err = svn_cmdline_printf(pool,
                                            apr_pstrcat(pool, s,
                                                        "%"APR_UINT64_T_FMT" (%s)\n",
                                                        SVN_VA_NULL),
                                            n->hunk_original_start,
                                            n->hunk_original_length,
                                            n->hunk_modified_start,
                                            n->hunk_modified_length,
                                            minus, off, n->prop_name);
                }
              else
                {
                  s = _(">         applied hunk @@ -%lu,%lu +%lu,%lu @@ "
                        "with offset %s");
                  err = svn_cmdline_printf(pool,
                                           apr_pstrcat(pool, s,
                                                       "%"APR_UINT64_T_FMT"\n",
                                                       SVN_VA_NULL),
                                           n->hunk_original_start,
                                           n->hunk_original_length,
                                           n->hunk_modified_start,
                                           n->hunk_modified_length,
                                           minus, off);
                }

              if (err)
                goto print_error;
            }
        }
      else if (n->hunk_fuzz)
        {
          if (n->prop_name)
            err = svn_cmdline_printf(pool,
                          _(">         applied hunk ## -%lu,%lu +%lu,%lu ## "
                                        "with fuzz %lu (%s)\n"),
                                        n->hunk_original_start,
                                        n->hunk_original_length,
                                        n->hunk_modified_start,
                                        n->hunk_modified_length,
                                        n->hunk_fuzz,
                                        n->prop_name);
          else
            err = svn_cmdline_printf(pool,
                          _(">         applied hunk @@ -%lu,%lu +%lu,%lu @@ "
                                        "with fuzz %lu\n"),
                                        n->hunk_original_start,
                                        n->hunk_original_length,
                                        n->hunk_modified_start,
                                        n->hunk_modified_length,
                                        n->hunk_fuzz);
          if (err)
            goto print_error;

        }
      break;

    case svn_wc_notify_patch_rejected_hunk:
      nb->received_some_change = TRUE;

      if (n->prop_name)
        err = svn_cmdline_printf(pool,
                                 _(">         rejected hunk "
                                   "## -%lu,%lu +%lu,%lu ## (%s)\n"),
                                 n->hunk_original_start,
                                 n->hunk_original_length,
                                 n->hunk_modified_start,
                                 n->hunk_modified_length,
                                 n->prop_name);
      else
        err = svn_cmdline_printf(pool,
                                 _(">         rejected hunk "
                                   "@@ -%lu,%lu +%lu,%lu @@\n"),
                                 n->hunk_original_start,
                                 n->hunk_original_length,
                                 n->hunk_modified_start,
                                 n->hunk_modified_length);
      if (err)
        goto print_error;
      break;

    case svn_wc_notify_patch_hunk_already_applied:
      nb->received_some_change = TRUE;
      if (n->prop_name)
        err = svn_cmdline_printf(pool,
                                 _(">         hunk "
                                   "## -%lu,%lu +%lu,%lu ## "
                                   "already applied (%s)\n"),
                                 n->hunk_original_start,
                                 n->hunk_original_length,
                                 n->hunk_modified_start,
                                 n->hunk_modified_length,
                                 n->prop_name);
      else
        err = svn_cmdline_printf(pool,
                                 _(">         hunk "
                                   "@@ -%lu,%lu +%lu,%lu @@ "
                                   "already applied\n"),
                                 n->hunk_original_start,
                                 n->hunk_original_length,
                                 n->hunk_modified_start,
                                 n->hunk_modified_length);
      if (err)
        goto print_error;
      break;

    case svn_wc_notify_update_update:
    case svn_wc_notify_merge_record_info:
      {
        if (n->content_state == svn_wc_notify_state_conflicted)
          {
            nb->text_conflicts++;
            add_conflicted_path(nb, n->path);
            statchar_buf[0] = 'C';
          }
        else if (n->kind == svn_node_file)
          {
            if (n->content_state == svn_wc_notify_state_merged)
              statchar_buf[0] = 'G';
            else if (n->content_state == svn_wc_notify_state_changed)
              statchar_buf[0] = 'U';
          }

        if (n->prop_state == svn_wc_notify_state_conflicted)
          {
            nb->prop_conflicts++;
            add_conflicted_path(nb, n->path);
            statchar_buf[1] = 'C';
          }
        else if (n->prop_state == svn_wc_notify_state_merged)
          statchar_buf[1] = 'G';
        else if (n->prop_state == svn_wc_notify_state_changed)
          statchar_buf[1] = 'U';

        if (n->lock_state == svn_wc_notify_lock_state_unlocked)
          statchar_buf[2] = 'B';

        if (statchar_buf[0] != ' ' || statchar_buf[1] != ' ')
          nb->received_some_change = TRUE;

        if (statchar_buf[0] != ' ' || statchar_buf[1] != ' '
            || statchar_buf[2] != ' ')
          {
            if ((err = svn_cmdline_printf(pool, "%s %s\n",
                                          statchar_buf, path_local)))
              goto print_error;
          }
      }
      break;

    case svn_wc_notify_update_external:
      /* Remember that we're now "inside" an externals definition. */
      nb->in_external = TRUE;

      /* Currently this is used for checkouts and switches too.  If we
         want different output, we'll have to add new actions. */
      if ((err = svn_cmdline_printf(pool,
                                    _("\nFetching external item into '%s':\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_failed_external:
      /* If we are currently inside the handling of an externals
         definition, then we can simply present n->err as a warning
         and feel confident that after this, we aren't handling that
         externals definition any longer. */
      if (nb->in_external)
        {
          svn_handle_warning2(stderr, n->err, "svn: ");
          nb->in_external = FALSE;
          if ((err = svn_cmdline_printf(pool, "\n")))
            goto print_error;
        }
      /* Otherwise, we'll just print two warnings.  Why?  Because
         svn_handle_warning2() only shows the single "best message",
         but we have two pretty important ones: that the external at
         '/some/path' didn't pan out, and then the more specific
         reason why (from n->err). */
      else
        {
          svn_error_t *warn_err =
            svn_error_createf(SVN_ERR_BASE, NULL,
                              _("Error handling externals definition for '%s':"),
                              path_local);
          svn_handle_warning2(stderr, warn_err, "svn: ");
          svn_error_clear(warn_err);
          svn_handle_warning2(stderr, n->err, "svn: ");
        }
      break;

    case svn_wc_notify_update_started:
      if (! (nb->in_external ||
             nb->is_checkout ||
             nb->is_export))
        {
          if ((err = svn_cmdline_printf(pool, _("Updating '%s':\n"),
                                        path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_update_completed:
      {
        if (SVN_IS_VALID_REVNUM(n->revision))
          {
            if (nb->is_export)
              {
                if ((err = svn_cmdline_printf
                     (pool, nb->in_external
                      ? _("Exported external at revision %ld.\n")
                      : _("Exported revision %ld.\n"),
                      n->revision)))
                  goto print_error;
              }
            else if (nb->is_checkout)
              {
                if ((err = svn_cmdline_printf
                     (pool, nb->in_external
                      ? _("Checked out external at revision %ld.\n")
                      : _("Checked out revision %ld.\n"),
                      n->revision)))
                  goto print_error;
              }
            else
              {
                if (nb->received_some_change)
                  {
                    nb->received_some_change = FALSE;
                    if ((err = svn_cmdline_printf
                         (pool, nb->in_external
                          ? _("Updated external to revision %ld.\n")
                          : _("Updated to revision %ld.\n"),
                          n->revision)))
                      goto print_error;
                  }
                else
                  {
                    if ((err = svn_cmdline_printf
                         (pool, nb->in_external
                          ? _("External at revision %ld.\n")
                          : _("At revision %ld.\n"),
                          n->revision)))
                      goto print_error;
                  }
              }
          }
        else  /* no revision */
          {
            if (nb->is_export)
              {
                if ((err = svn_cmdline_printf
                     (pool, nb->in_external
                      ? _("External export complete.\n")
                      : _("Export complete.\n"))))
                  goto print_error;
              }
            else if (nb->is_checkout)
              {
                if ((err = svn_cmdline_printf
                     (pool, nb->in_external
                      ? _("External checkout complete.\n")
                      : _("Checkout complete.\n"))))
                  goto print_error;
              }
            else
              {
                if ((err = svn_cmdline_printf
                     (pool, nb->in_external
                      ? _("External update complete.\n")
                      : _("Update complete.\n"))))
                  goto print_error;
              }
          }
      }

      if (nb->in_external)
        {
          nb->in_external = FALSE;
          if ((err = svn_cmdline_printf(pool, "\n")))
            goto print_error;
        }
      break;

    case svn_wc_notify_status_external:
      if ((err = svn_cmdline_printf
           (pool, _("\nPerforming status on external item at '%s':\n"),
            path_local)))
        goto print_error;
      break;

    case svn_wc_notify_status_completed:
      if (SVN_IS_VALID_REVNUM(n->revision))
        if ((err = svn_cmdline_printf(pool,
                                      _("Status against revision: %6ld\n"),
                                      n->revision)))
          goto print_error;
      break;

    case svn_wc_notify_commit_modified:
      /* xgettext: Align the %s's on this and the following 4 messages */
      if ((err = svn_cmdline_printf(pool,
                                    nb->is_wc_to_repos_copy
                                      ? _("Sending copy of       %s\n")
                                      : _("Sending        %s\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_commit_added:
    case svn_wc_notify_commit_copied:
      if (n->mime_type && svn_mime_type_is_binary(n->mime_type))
        {
          if ((err = svn_cmdline_printf(pool,
                                        nb->is_wc_to_repos_copy
                                          ? _("Adding copy of (bin)  %s\n")
                                          : _("Adding  (bin)  %s\n"),
                                        path_local)))
          goto print_error;
        }
      else
        {
          if ((err = svn_cmdline_printf(pool,
                                        nb->is_wc_to_repos_copy
                                          ? _("Adding copy of        %s\n")
                                          : _("Adding         %s\n"),
                                        path_local)))
            goto print_error;
        }
      break;

    case svn_wc_notify_commit_deleted:
      if ((err = svn_cmdline_printf(pool,
                                    nb->is_wc_to_repos_copy
                                      ? _("Deleting copy of      %s\n")
                                      : _("Deleting       %s\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_commit_replaced:
    case svn_wc_notify_commit_copied_replaced:
      if ((err = svn_cmdline_printf(pool,
                                    nb->is_wc_to_repos_copy
                                      ? _("Replacing copy of     %s\n")
                                      : _("Replacing      %s\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_commit_postfix_txdelta:
      if (! nb->sent_first_txdelta)
        {
          nb->sent_first_txdelta = TRUE;
          if ((err = svn_cmdline_printf(pool,
                                        _("Transmitting file data "))))
            goto print_error;
        }

      if ((err = svn_cmdline_printf(pool, ".")))
        goto print_error;
      break;

    case svn_wc_notify_locked:
      if ((err = svn_cmdline_printf(pool, _("'%s' locked by user '%s'.\n"),
                                    path_local, n->lock->owner)))
        goto print_error;
      break;

    case svn_wc_notify_unlocked:
      if ((err = svn_cmdline_printf(pool, _("'%s' unlocked.\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_failed_lock:
    case svn_wc_notify_failed_unlock:
      svn_handle_warning2(stderr, n->err, "svn: ");
      break;

    case svn_wc_notify_changelist_set:
      if ((err = svn_cmdline_printf(pool, "A [%s] %s\n",
                                    n->changelist_name, path_local)))
        goto print_error;
      break;

    case svn_wc_notify_changelist_clear:
    case svn_wc_notify_changelist_moved:
      if ((err = svn_cmdline_printf(pool,
                                    "D [%s] %s\n",
                                    n->changelist_name, path_local)))
        goto print_error;
      break;

    case svn_wc_notify_merge_begin:
      if (n->merge_range == NULL)
        err = svn_cmdline_printf(pool,
                                 _("--- Merging differences between "
                                   "repository URLs into '%s':\n"),
                                 path_local);
      else if (n->merge_range->start == n->merge_range->end - 1
          || n->merge_range->start == n->merge_range->end)
        err = svn_cmdline_printf(pool, _("--- Merging r%ld into '%s':\n"),
                                 n->merge_range->end, path_local);
      else if (n->merge_range->start - 1 == n->merge_range->end)
        err = svn_cmdline_printf(pool,
                                 _("--- Reverse-merging r%ld into '%s':\n"),
                                 n->merge_range->start, path_local);
      else if (n->merge_range->start < n->merge_range->end)
        err = svn_cmdline_printf(pool,
                                 _("--- Merging r%ld through r%ld into "
                                   "'%s':\n"),
                                 n->merge_range->start + 1,
                                 n->merge_range->end, path_local);
      else /* n->merge_range->start > n->merge_range->end - 1 */
        err = svn_cmdline_printf(pool,
                                 _("--- Reverse-merging r%ld through r%ld "
                                   "into '%s':\n"),
                                 n->merge_range->start,
                                 n->merge_range->end + 1, path_local);
      if (err)
        goto print_error;
      break;

    case svn_wc_notify_merge_record_info_begin:
      if (!n->merge_range)
        {
          err = svn_cmdline_printf(pool,
                                   _("--- Recording mergeinfo for merge "
                                     "between repository URLs into '%s':\n"),
                                   path_local);
        }
      else
        {
          if (n->merge_range->start == n->merge_range->end - 1
              || n->merge_range->start == n->merge_range->end)
            err = svn_cmdline_printf(
              pool,
              _("--- Recording mergeinfo for merge of r%ld into '%s':\n"),
              n->merge_range->end, path_local);
          else if (n->merge_range->start - 1 == n->merge_range->end)
            err = svn_cmdline_printf(
              pool,
              _("--- Recording mergeinfo for reverse merge of r%ld into '%s':\n"),
              n->merge_range->start, path_local);
           else if (n->merge_range->start < n->merge_range->end)
             err = svn_cmdline_printf(
               pool,
               _("--- Recording mergeinfo for merge of r%ld through r%ld into '%s':\n"),
               n->merge_range->start + 1, n->merge_range->end, path_local);
           else /* n->merge_range->start > n->merge_range->end - 1 */
             err = svn_cmdline_printf(
               pool,
               _("--- Recording mergeinfo for reverse merge of r%ld through r%ld into '%s':\n"),
               n->merge_range->start, n->merge_range->end + 1, path_local);
        }

      if (err)
        goto print_error;
      break;

    case svn_wc_notify_merge_elide_info:
      if ((err = svn_cmdline_printf(pool,
                                    _("--- Eliding mergeinfo from '%s':\n"),
                                    path_local)))
        goto print_error;
      break;

    case svn_wc_notify_foreign_merge_begin:
      if (n->merge_range == NULL)
        err = svn_cmdline_printf(pool,
                                 _("--- Merging differences between "
                                   "foreign repository URLs into '%s':\n"),
                                 path_local);
      else if (n->merge_range->start == n->merge_range->end - 1
          || n->merge_range->start == n->merge_range->end)
        err = svn_cmdline_printf(pool,
                                 _("--- Merging (from foreign repository) "
                                   "r%ld into '%s':\n"),
                                 n->merge_range->end, path_local);
      else if (n->merge_range->start - 1 == n->merge_range->end)
        err = svn_cmdline_printf(pool,
                                 _("--- Reverse-merging (from foreign "
                                   "repository) r%ld into '%s':\n"),
                                 n->merge_range->start, path_local);
      else if (n->merge_range->start < n->merge_range->end)
        err = svn_cmdline_printf(pool,
                                 _("--- Merging (from foreign repository) "
                                   "r%ld through r%ld into '%s':\n"),
                                 n->merge_range->start + 1,
                                 n->merge_range->end, path_local);
      else /* n->merge_range->start > n->merge_range->end - 1 */
        err = svn_cmdline_printf(pool,
                                 _("--- Reverse-merging (from foreign "
                                   "repository) r%ld through r%ld into "
                                   "'%s':\n"),
                                 n->merge_range->start,
                                 n->merge_range->end + 1, path_local);
      if (err)
        goto print_error;
      break;

    case svn_wc_notify_tree_conflict:
      nb->tree_conflicts++;
      add_conflicted_path(nb, n->path);
      if ((err = svn_cmdline_printf(pool, "   C %s\n", path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_shadowed_add:
      nb->received_some_change = TRUE;
      if ((err = svn_cmdline_printf(pool, "   A %s\n", path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_shadowed_update:
      nb->received_some_change = TRUE;
      if ((err = svn_cmdline_printf(pool, "   U %s\n", path_local)))
        goto print_error;
      break;

    case svn_wc_notify_update_shadowed_delete:
      nb->received_some_change = TRUE;
      if ((err = svn_cmdline_printf(pool, "   D %s\n", path_local)))
        goto print_error;
      break;

    case svn_wc_notify_property_modified:
    case svn_wc_notify_property_added:
        err = svn_cmdline_printf(pool,
                                 _("property '%s' set on '%s'\n"),
                                 n->prop_name, path_local);
        if (err)
          goto print_error;
      break;

    case svn_wc_notify_property_deleted:
        err = svn_cmdline_printf(pool,
                                 _("property '%s' deleted from '%s'.\n"),
                                 n->prop_name, path_local);
        if (err)
          goto print_error;
      break;

    case svn_wc_notify_property_deleted_nonexistent:
        err = svn_cmdline_printf(pool,
                                 _("Attempting to delete nonexistent "
                                   "property '%s' on '%s'\n"), n->prop_name,
                                   path_local);
        if (err)
          goto print_error;
      break;

    case svn_wc_notify_revprop_set:
        err = svn_cmdline_printf(pool,
                          _("property '%s' set on repository revision %ld\n"),
                          n->prop_name, n->revision);
        if (err)
          goto print_error;
      break;

    case svn_wc_notify_revprop_deleted:
        err = svn_cmdline_printf(pool,
                     _("property '%s' deleted from repository revision %ld\n"),
                     n->prop_name, n->revision);
        if (err)
          goto print_error;
      break;

    case svn_wc_notify_upgraded_path:
        err = svn_cmdline_printf(pool, _("Upgraded '%s'\n"), path_local);
        if (err)
          goto print_error;
      break;

    case svn_wc_notify_url_redirect:
      err = svn_cmdline_printf(pool, _("Redirecting to URL '%s':\n"),
                               n->url);
      if (err)
        goto print_error;
      break;

    case svn_wc_notify_path_nonexistent:
      err = svn_cmdline_printf(pool, _("'%s' is not under version control"),
                               path_local);
      if (err)
        goto print_error;
      break;

    case svn_wc_notify_conflict_resolver_starting:
      /* Once all operations invoke the interactive conflict resolution after
       * they've completed, we can run svn_cl__print_conflict_stats() here. */
      break;

    case svn_wc_notify_conflict_resolver_done:
      break;

    default:
      break;
    }

  if ((err = svn_cmdline_fflush(stdout)))
    goto print_error;

  return;

 print_error:
  /* If we had no errors before, print this error to stderr. Else, don't print
     anything.  The user already knows there were some output errors,
     so there is no point in flooding her with an error per notification. */
  if (!nb->had_print_error)
    {
      nb->had_print_error = TRUE;
      /* Issue #3014:
       * Don't print anything on broken pipes. The pipe was likely
       * closed by the process at the other end. We expect that
       * process to perform error reporting as necessary.
       *
       * ### This assumes that there is only one error in a chain for
       * ### SVN_ERR_IO_PIPE_WRITE_ERROR. See svn_cmdline_fputs(). */
      if (err->apr_err != SVN_ERR_IO_PIPE_WRITE_ERROR)
        svn_handle_error2(err, stderr, FALSE, "svn: ");
    }
  svn_error_clear(err);
}


svn_error_t *
svn_cl__get_notifier(svn_wc_notify_func2_t *notify_func_p,
                     void **notify_baton_p,
                     apr_pool_t *pool)
{
  struct notify_baton *nb = apr_pcalloc(pool, sizeof(*nb));

  nb->received_some_change = FALSE;
  nb->sent_first_txdelta = FALSE;
  nb->is_checkout = FALSE;
  nb->is_export = FALSE;
  nb->is_wc_to_repos_copy = FALSE;
  nb->in_external = FALSE;
  nb->had_print_error = FALSE;
  nb->text_conflicts = 0;
  nb->prop_conflicts = 0;
  nb->tree_conflicts = 0;
  nb->skipped_paths = 0;
  nb->conflicted_paths = apr_hash_make(pool);
  SVN_ERR(svn_dirent_get_absolute(&nb->path_prefix, "", pool));

  *notify_func_p = notify;
  *notify_baton_p = nb;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__notifier_mark_export(void *baton)
{
  struct notify_baton *nb = baton;

  nb->is_export = TRUE;
  return SVN_NO_ERROR;
}
