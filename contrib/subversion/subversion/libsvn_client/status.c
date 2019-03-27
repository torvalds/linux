/*
 * status.c:  return the status of a working copy dirent
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
#include <apr_strings.h>
#include <apr_pools.h>

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "client.h"

#include "svn_path.h"
#include "svn_dirent_uri.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_hash.h"

#include "private/svn_client_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_wc_private.h"


/*** Getting update information ***/

/* Baton for tweak_status.  It wraps a bit of extra functionality
   around the received status func/baton, so we can remember if the
   target was deleted in HEAD and tweak incoming status structures
   accordingly. */
struct status_baton
{
  svn_boolean_t deleted_in_repos;             /* target is deleted in repos */
  apr_hash_t *changelist_hash;                /* keys are changelist names */
  svn_client_status_func_t real_status_func;  /* real status function */
  void *real_status_baton;                    /* real status baton */
  const char *anchor_abspath;                 /* Absolute path of anchor */
  const char *anchor_relpath;                 /* Relative path of anchor */
  svn_wc_context_t *wc_ctx;                   /* A working copy context. */
};

/* A status callback function which wraps the *real* status
   function/baton.   This sucker takes care of any status tweaks we
   need to make (such as noting that the target of the status is
   missing from HEAD in the repository).

   This implements the 'svn_wc_status_func4_t' function type.  */
static svn_error_t *
tweak_status(void *baton,
             const char *local_abspath,
             const svn_wc_status3_t *status,
             apr_pool_t *scratch_pool)
{
  struct status_baton *sb = baton;
  const char *path = local_abspath;
  svn_client_status_t *cst;

  if (sb->anchor_abspath)
    path = svn_dirent_join(sb->anchor_relpath,
                           svn_dirent_skip_ancestor(sb->anchor_abspath, path),
                           scratch_pool);

  /* If the status item has an entry, but doesn't belong to one of the
     changelists our caller is interested in, we filter out this status
     transmission.  */
  if (sb->changelist_hash
      && (! status->changelist
          || ! svn_hash_gets(sb->changelist_hash, status->changelist)))
    {
      return SVN_NO_ERROR;
    }

  /* If we know that the target was deleted in HEAD of the repository,
     we need to note that fact in all the status structures that come
     through here. */
  if (sb->deleted_in_repos)
    {
      svn_wc_status3_t *new_status = svn_wc_dup_status3(status, scratch_pool);
      new_status->repos_node_status = svn_wc_status_deleted;
      status = new_status;
    }

  SVN_ERR(svn_client__create_status(&cst, sb->wc_ctx, local_abspath, status,
                                    scratch_pool, scratch_pool));

  /* Call the real status function/baton. */
  return sb->real_status_func(sb->real_status_baton, path, cst,
                              scratch_pool);
}

/* A baton for our reporter that is used to collect locks. */
typedef struct report_baton_t {
  const svn_ra_reporter3_t* wrapped_reporter;
  void *wrapped_report_baton;
  /* The common ancestor URL of all paths included in the report. */
  char *ancestor;
  void *set_locks_baton;
  svn_depth_t depth;
  svn_client_ctx_t *ctx;
  /* Pool to store locks in. */
  apr_pool_t *pool;
} report_baton_t;

/* Implements svn_ra_reporter3_t->set_path. */
static svn_error_t *
reporter_set_path(void *report_baton, const char *path,
                  svn_revnum_t revision, svn_depth_t depth,
                  svn_boolean_t start_empty, const char *lock_token,
                  apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;

  return rb->wrapped_reporter->set_path(rb->wrapped_report_baton, path,
                                        revision, depth, start_empty,
                                        lock_token, pool);
}

/* Implements svn_ra_reporter3_t->delete_path. */
static svn_error_t *
reporter_delete_path(void *report_baton, const char *path, apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;

  return rb->wrapped_reporter->delete_path(rb->wrapped_report_baton, path,
                                           pool);
}

/* Implements svn_ra_reporter3_t->link_path. */
static svn_error_t *
reporter_link_path(void *report_baton, const char *path, const char *url,
                   svn_revnum_t revision, svn_depth_t depth,
                   svn_boolean_t start_empty,
                   const char *lock_token, apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;

  if (!svn_uri__is_ancestor(rb->ancestor, url))
    {
      const char *ancestor;

      ancestor = svn_uri_get_longest_ancestor(url, rb->ancestor, pool);

      /* If we got a shorter ancestor, truncate our current ancestor.
         Note that svn_uri_get_longest_ancestor will allocate its return
         value even if it identical to one of its arguments. */

      rb->ancestor[strlen(ancestor)] = '\0';
      rb->depth = svn_depth_infinity;
    }

  return rb->wrapped_reporter->link_path(rb->wrapped_report_baton, path, url,
                                         revision, depth, start_empty,
                                         lock_token, pool);
}

/* Implements svn_ra_reporter3_t->finish_report. */
static svn_error_t *
reporter_finish_report(void *report_baton, apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;
  svn_ra_session_t *ras;
  apr_hash_t *locks;
  const char *repos_root;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err = SVN_NO_ERROR;

  /* Open an RA session to our common ancestor and grab the locks under it.
   */
  SVN_ERR(svn_client_open_ra_session2(&ras, rb->ancestor, NULL,
                                      rb->ctx, subpool, subpool));

  /* The locks need to live throughout the edit.  Note that if the
     server doesn't support lock discovery, we'll just not do locky
     stuff. */
  err = svn_ra_get_locks2(ras, &locks, "", rb->depth, rb->pool);
  if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      locks = apr_hash_make(rb->pool);
    }
  SVN_ERR(err);

  SVN_ERR(svn_ra_get_repos_root2(ras, &repos_root, rb->pool));

  /* Close the RA session. */
  svn_pool_destroy(subpool);

  SVN_ERR(svn_wc_status_set_repos_locks(rb->set_locks_baton, locks,
                                        repos_root, rb->pool));

  return rb->wrapped_reporter->finish_report(rb->wrapped_report_baton, pool);
}

/* Implements svn_ra_reporter3_t->abort_report. */
static svn_error_t *
reporter_abort_report(void *report_baton, apr_pool_t *pool)
{
  report_baton_t *rb = report_baton;

  return rb->wrapped_reporter->abort_report(rb->wrapped_report_baton, pool);
}

/* A reporter that keeps track of the common URL ancestor of all paths in
   the WC and fetches repository locks for all paths under this ancestor. */
static svn_ra_reporter3_t lock_fetch_reporter = {
  reporter_set_path,
  reporter_delete_path,
  reporter_link_path,
  reporter_finish_report,
  reporter_abort_report
};

/* Perform status operations on each external in EXTERNAL_MAP, a const char *
   local_abspath of all externals mapping to the const char* defining_abspath.
   All other options are the same as those passed to svn_client_status().

   If ANCHOR_ABSPATH and ANCHOR-RELPATH are not null, use them to provide
   properly formatted relative paths */
static svn_error_t *
do_external_status(svn_client_ctx_t *ctx,
                   apr_hash_t *external_map,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t check_out_of_date,
                   svn_boolean_t check_working_copy,
                   svn_boolean_t no_ignore,
                   const apr_array_header_t *changelists,
                   const char *anchor_abspath,
                   const char *anchor_relpath,
                   svn_client_status_func_t status_func,
                   void *status_baton,
                   apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *externals;
  int i;

  externals = svn_sort__hash(external_map, svn_sort_compare_items_lexically,
                             scratch_pool);

  /* Loop over the hash of new values (we don't care about the old
     ones).  This is a mapping of versioned directories to property
     values. */
  for (i = 0; i < externals->nelts; i++)
    {
      svn_node_kind_t external_kind;
      svn_sort__item_t item = APR_ARRAY_IDX(externals, i, svn_sort__item_t);
      const char *local_abspath = item.key;
      const char *defining_abspath = item.value;
      svn_node_kind_t kind;
      svn_opt_revision_t opt_rev;
      const char *status_path;

      svn_pool_clear(iterpool);

      /* Obtain information on the expected external. */
      SVN_ERR(svn_wc__read_external_info(&external_kind, NULL, NULL, NULL,
                                         &opt_rev.value.number,
                                         ctx->wc_ctx, defining_abspath,
                                         local_abspath, FALSE,
                                         iterpool, iterpool));

      if (external_kind != svn_node_dir)
        continue;

      SVN_ERR(svn_io_check_path(local_abspath, &kind, iterpool));
      if (kind != svn_node_dir)
        continue;

      if (SVN_IS_VALID_REVNUM(opt_rev.value.number))
        opt_rev.kind = svn_opt_revision_number;
      else
        opt_rev.kind = svn_opt_revision_unspecified;

      /* Tell the client we're starting an external status set. */
      if (ctx->notify_func2)
        ctx->notify_func2(
               ctx->notify_baton2,
               svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_status_external,
                                    iterpool), iterpool);

      status_path = local_abspath;
      if (anchor_abspath)
        {
          status_path = svn_dirent_join(anchor_relpath,
                           svn_dirent_skip_ancestor(anchor_abspath,
                                                    status_path),
                           iterpool);
        }

      /* And then do the status. */
      SVN_ERR(svn_client_status6(NULL, ctx, status_path, &opt_rev, depth,
                                 get_all, check_out_of_date,
                                 check_working_copy, no_ignore,
                                 FALSE /* ignore_exernals */,
                                 FALSE /* depth_as_sticky */,
                                 changelists, status_func, status_baton,
                                 iterpool));
    }

  /* Destroy SUBPOOL and (implicitly) ITERPOOL. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/*** Public Interface. ***/


svn_error_t *
svn_client_status6(svn_revnum_t *result_rev,
                   svn_client_ctx_t *ctx,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t check_out_of_date,
                   svn_boolean_t check_working_copy,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t depth_as_sticky,
                   const apr_array_header_t *changelists,
                   svn_client_status_func_t status_func,
                   void *status_baton,
                   apr_pool_t *pool)  /* ### aka scratch_pool */
{
  struct status_baton sb;
  const char *dir, *dir_abspath;
  const char *target_abspath;
  const char *target_basename;
  apr_array_header_t *ignores;
  svn_error_t *err;
  apr_hash_t *changelist_hash = NULL;

  /* Override invalid combinations of the check_out_of_date and
     check_working_copy flags. */
  if (!check_out_of_date)
    check_working_copy = TRUE;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  if (result_rev)
    *result_rev = SVN_INVALID_REVNUM;

  sb.real_status_func = status_func;
  sb.real_status_baton = status_baton;
  sb.deleted_in_repos = FALSE;
  sb.changelist_hash = changelist_hash;
  sb.wc_ctx = ctx->wc_ctx;

  SVN_ERR(svn_dirent_get_absolute(&target_abspath, path, pool));

  if (check_out_of_date)
    {
      /* The status editor only works on directories, so get the ancestor
         if necessary */

      svn_node_kind_t kind;

      SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, target_abspath,
                                TRUE, FALSE, pool));

      /* Dir must be a working copy directory or the status editor fails */
      if (kind == svn_node_dir)
        {
          dir_abspath = target_abspath;
          target_basename = "";
          dir = path;
        }
      else
        {
          dir_abspath = svn_dirent_dirname(target_abspath, pool);
          target_basename = svn_dirent_basename(target_abspath, NULL);
          dir = svn_dirent_dirname(path, pool);

          if (kind == svn_node_file)
            {
              if (depth == svn_depth_empty)
                depth = svn_depth_files;
            }
          else
            {
              err = svn_wc_read_kind2(&kind, ctx->wc_ctx, dir_abspath,
                                      FALSE, FALSE, pool);

              svn_error_clear(err);

              if (err || kind != svn_node_dir)
                {
                  return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                           _("'%s' is not a working copy"),
                                           svn_dirent_local_style(path, pool));
                }
            }
        }
    }
  else
    {
      dir = path;
      dir_abspath = target_abspath;
    }

  if (svn_dirent_is_absolute(dir))
    {
      sb.anchor_abspath = NULL;
      sb.anchor_relpath = NULL;
    }
  else
    {
      sb.anchor_abspath = dir_abspath;
      sb.anchor_relpath = dir;
    }

  /* Get the status edit, and use our wrapping status function/baton
     as the callback pair. */
  SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, pool));

  /* If we want to know about out-of-dateness, we crawl the working copy and
     let the RA layer drive the editor for real.  Otherwise, we just close the
     edit.  :-) */
  if (check_out_of_date)
    {
      svn_ra_session_t *ra_session;
      const char *URL;
      svn_node_kind_t kind;
      svn_boolean_t server_supports_depth;
      const svn_delta_editor_t *editor;
      void *edit_baton, *set_locks_baton;
      svn_revnum_t edit_revision = SVN_INVALID_REVNUM;

      /* Get full URL from the ANCHOR. */
      SVN_ERR(svn_client_url_from_path2(&URL, dir_abspath, ctx,
                                        pool, pool));

      if (!URL)
        return svn_error_createf
          (SVN_ERR_ENTRY_MISSING_URL, NULL,
           _("Entry '%s' has no URL"),
           svn_dirent_local_style(dir, pool));

      /* Open a repository session to the URL. */
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, URL,
                                                   dir_abspath, NULL,
                                                   FALSE, TRUE,
                                                   ctx, pool, pool));

      SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                    SVN_RA_CAPABILITY_DEPTH, pool));

      SVN_ERR(svn_wc__get_status_editor(&editor, &edit_baton, &set_locks_baton,
                                        &edit_revision, ctx->wc_ctx,
                                        dir_abspath, target_basename,
                                        depth, get_all, check_working_copy,
                                        no_ignore, depth_as_sticky,
                                        server_supports_depth,
                                        ignores, tweak_status, &sb,
                                        ctx->cancel_func, ctx->cancel_baton,
                                        pool, pool));


      /* Verify that URL exists in HEAD.  If it doesn't, this can save
         us a whole lot of hassle; if it does, the cost of this
         request should be minimal compared to the size of getting
         back the average amount of "out-of-date" information. */
      SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM,
                                &kind, pool));
      if (kind == svn_node_none)
        {
          svn_boolean_t added;

          /* Our status target does not exist in HEAD.  If we've got
             it locally added, that's okay.  But if it was previously
             versioned, then it must have since been deleted from the
             repository.  (Note that "locally replaced" doesn't count
             as "added" in this case.)  */
          SVN_ERR(svn_wc__node_is_added(&added, ctx->wc_ctx,
                                        dir_abspath, pool));
          if (! added)
            sb.deleted_in_repos = TRUE;

          /* And now close the edit. */
          SVN_ERR(editor->close_edit(edit_baton, pool));
        }
      else
        {
          svn_revnum_t revnum;
          report_baton_t rb;
          svn_depth_t status_depth;

          if (revision->kind == svn_opt_revision_head)
            {
              /* Cause the revision number to be omitted from the request,
                 which implies HEAD. */
              revnum = SVN_INVALID_REVNUM;
            }
          else
            {
              /* Get a revision number for our status operation. */
              SVN_ERR(svn_client__get_revision_number(&revnum, NULL,
                                                      ctx->wc_ctx,
                                                      target_abspath,
                                                      ra_session, revision,
                                                      pool));
            }

          if (depth_as_sticky || !server_supports_depth)
            status_depth = depth;
          else
            status_depth = svn_depth_unknown; /* Use depth from WC */

          /* Do the deed.  Let the RA layer drive the status editor. */
          SVN_ERR(svn_ra_do_status2(ra_session, &rb.wrapped_reporter,
                                    &rb.wrapped_report_baton,
                                    target_basename, revnum, status_depth,
                                    editor, edit_baton, pool));

          /* Init the report baton. */
          rb.ancestor = apr_pstrdup(pool, URL); /* Edited later */
          rb.set_locks_baton = set_locks_baton;
          rb.ctx = ctx;
          rb.pool = pool;

          if (depth == svn_depth_unknown)
            rb.depth = svn_depth_infinity;
          else
            rb.depth = depth;

          /* Drive the reporter structure, describing the revisions
             within PATH.  When we call reporter->finish_report,
             EDITOR will be driven to describe differences between our
             working copy and HEAD. */
          SVN_ERR(svn_wc_crawl_revisions5(ctx->wc_ctx,
                                          target_abspath,
                                          &lock_fetch_reporter, &rb,
                                          FALSE /* restore_files */,
                                          depth, (! depth_as_sticky),
                                          (! server_supports_depth),
                                          FALSE /* use_commit_times */,
                                          ctx->cancel_func, ctx->cancel_baton,
                                          NULL, NULL, pool));
        }

      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notify
            = svn_wc_create_notify(target_abspath,
                                   svn_wc_notify_status_completed, pool);
          notify->revision = edit_revision;
          ctx->notify_func2(ctx->notify_baton2, notify, pool);
        }

      /* If the caller wants the result revision, give it to them. */
      if (result_rev)
        *result_rev = edit_revision;
    }
  else
    {
      err = svn_wc_walk_status(ctx->wc_ctx, target_abspath,
                               depth, get_all, no_ignore, FALSE, ignores,
                               tweak_status, &sb,
                               ctx->cancel_func, ctx->cancel_baton,
                               pool);

      if (err && err->apr_err == SVN_ERR_WC_MISSING)
        {
          /* This error code is checked for in svn to continue after
             this error */
          svn_error_clear(err);
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                               _("'%s' is not a working copy"),
                               svn_dirent_local_style(path, pool));
        }

      SVN_ERR(err);
    }

  /* We only descend into an external if depth is svn_depth_infinity or
     svn_depth_unknown.  However, there are conceivable behaviors that
     would involve descending under other circumstances; thus, we pass
     depth anyway, so the code will DTRT if we change the conditional
     in the future.
  */
  if (SVN_DEPTH_IS_RECURSIVE(depth) && (! ignore_externals))
    {
      apr_hash_t *external_map;
      SVN_ERR(svn_wc__externals_defined_below(&external_map,
                                              ctx->wc_ctx, target_abspath,
                                              pool, pool));


      SVN_ERR(do_external_status(ctx, external_map,
                                 depth, get_all,
                                 check_out_of_date, check_working_copy,
                                 no_ignore, changelists,
                                 sb.anchor_abspath, sb.anchor_relpath,
                                 status_func, status_baton, pool));
    }

  return SVN_NO_ERROR;
}

svn_client_status_t *
svn_client_status_dup(const svn_client_status_t *status,
                      apr_pool_t *result_pool)
{
  svn_client_status_t *st = apr_palloc(result_pool, sizeof(*st));

  *st = *status;

  if (status->local_abspath)
    st->local_abspath = apr_pstrdup(result_pool, status->local_abspath);

  if (status->repos_root_url)
    st->repos_root_url = apr_pstrdup(result_pool, status->repos_root_url);

  if (status->repos_uuid)
    st->repos_uuid = apr_pstrdup(result_pool, status->repos_uuid);

  if (status->repos_relpath)
    st->repos_relpath = apr_pstrdup(result_pool, status->repos_relpath);

  if (status->changed_author)
    st->changed_author = apr_pstrdup(result_pool, status->changed_author);

  if (status->lock)
    st->lock = svn_lock_dup(status->lock, result_pool);

  if (status->changelist)
    st->changelist = apr_pstrdup(result_pool, status->changelist);

  if (status->ood_changed_author)
    st->ood_changed_author = apr_pstrdup(result_pool, status->ood_changed_author);

  if (status->repos_lock)
    st->repos_lock = svn_lock_dup(status->repos_lock, result_pool);

  if (status->backwards_compatibility_baton)
    {
      const svn_wc_status3_t *wc_st = status->backwards_compatibility_baton;

      st->backwards_compatibility_baton = svn_wc_dup_status3(wc_st,
                                                             result_pool);
    }

  if (status->moved_from_abspath)
    st->moved_from_abspath =
      apr_pstrdup(result_pool, status->moved_from_abspath);

  if (status->moved_to_abspath)
    st->moved_to_abspath = apr_pstrdup(result_pool, status->moved_to_abspath);

  return st;
}

svn_error_t *
svn_client__create_status(svn_client_status_t **cst,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          const svn_wc_status3_t *status,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  *cst = apr_pcalloc(result_pool, sizeof(**cst));

  (*cst)->kind = status->kind;
  (*cst)->local_abspath = local_abspath;
  (*cst)->filesize = status->filesize;
  (*cst)->versioned = status->versioned;

  (*cst)->conflicted = status->conflicted;

  (*cst)->node_status = status->node_status;
  (*cst)->text_status = status->text_status;
  (*cst)->prop_status = status->prop_status;

  if (status->kind == svn_node_dir)
    (*cst)->wc_is_locked = status->locked;

  (*cst)->copied = status->copied;
  (*cst)->revision = status->revision;

  (*cst)->changed_rev = status->changed_rev;
  (*cst)->changed_date = status->changed_date;
  (*cst)->changed_author = status->changed_author;

  (*cst)->repos_root_url = status->repos_root_url;
  (*cst)->repos_uuid = status->repos_uuid;
  (*cst)->repos_relpath = status->repos_relpath;

  (*cst)->switched = status->switched;

  (*cst)->file_external = status->file_external;
  if (status->file_external)
    {
      (*cst)->switched = FALSE;
    }

  (*cst)->lock = status->lock;

  (*cst)->changelist = status->changelist;
  (*cst)->depth = status->depth;

  /* Out of date information */
  (*cst)->ood_kind = status->ood_kind;
  (*cst)->repos_node_status = status->repos_node_status;
  (*cst)->repos_text_status = status->repos_text_status;
  (*cst)->repos_prop_status = status->repos_prop_status;
  (*cst)->repos_lock = status->repos_lock;

  (*cst)->ood_changed_rev = status->ood_changed_rev;
  (*cst)->ood_changed_date = status->ood_changed_date;
  (*cst)->ood_changed_author = status->ood_changed_author;

  /* When changing the value of backwards_compatibility_baton, also
     change its use in status4_wrapper_func in deprecated.c */
  (*cst)->backwards_compatibility_baton = status;

  if (status->versioned && status->conflicted)
    {
      svn_boolean_t text_conflicted, prop_conflicted, tree_conflicted;

      /* Note: This checks the on disk markers to automatically hide
               text/property conflicts that are hidden by removing their
               markers */
      SVN_ERR(svn_wc_conflicted_p3(&text_conflicted, &prop_conflicted,
                                   &tree_conflicted, wc_ctx, local_abspath,
                                   scratch_pool));

      if (text_conflicted)
        (*cst)->text_status = svn_wc_status_conflicted;

      if (prop_conflicted)
        (*cst)->prop_status = svn_wc_status_conflicted;

      /* ### Also set this for tree_conflicts? */
      if (text_conflicted || prop_conflicted)
        (*cst)->node_status = svn_wc_status_conflicted;
    }

  (*cst)->moved_from_abspath = status->moved_from_abspath;
  (*cst)->moved_to_abspath = status->moved_to_abspath;

  return SVN_NO_ERROR;
}

