/*
 * blame.c:  return blame messages
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

#include <apr_pools.h>

#include "client.h"

#include "svn_client.h"
#include "svn_subst.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_diff.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_hash.h"
#include "svn_sorts.h"

#include "private/svn_wc_private.h"

#include "svn_private_config.h"

#include <assert.h>

/* The metadata associated with a particular revision. */
struct rev
{
  svn_revnum_t revision; /* the revision number */
  apr_hash_t *rev_props; /* the revision properties */
  /* Used for merge reporting. */
  const char *path;      /* the absolute repository path */
};

/* One chunk of blame */
struct blame
{
  const struct rev *rev;    /* the responsible revision */
  apr_off_t start;          /* the starting diff-token (line) */
  struct blame *next;       /* the next chunk */
};

/* A chain of blame chunks */
struct blame_chain
{
  struct blame *blame;      /* linked list of blame chunks */
  struct blame *avail;      /* linked list of free blame chunks */
  struct apr_pool_t *pool;  /* Allocate members from this pool. */
};

/* The baton use for the diff output routine. */
struct diff_baton {
  struct blame_chain *chain;
  const struct rev *rev;
};

/* The baton used for a file revision. Lives the entire operation */
struct file_rev_baton {
  svn_revnum_t start_rev, end_rev;
  svn_boolean_t backwards;
  const char *target;
  svn_client_ctx_t *ctx;
  const svn_diff_file_options_t *diff_options;
  /* name of file containing the previous revision of the file */
  const char *last_filename;
  struct rev *last_rev;   /* the rev of the last modification */
  struct blame_chain *chain;      /* the original blame chain. */
  const char *repos_root_url;    /* To construct a url */
  apr_pool_t *mainpool;  /* lives during the whole sequence of calls */
  apr_pool_t *lastpool;  /* pool used during previous call */
  apr_pool_t *currpool;  /* pool used during this call */

  /* These are used for tracking merged revisions. */
  svn_boolean_t include_merged_revisions;
  struct blame_chain *merged_chain;  /* the merged blame chain. */
  /* name of file containing the previous merged revision of the file */
  const char *last_original_filename;
  /* pools for files which may need to persist for more than one rev. */
  apr_pool_t *filepool;
  apr_pool_t *prevfilepool;

  svn_boolean_t check_mime_type;

  /* When blaming backwards we have to use the changes
     on the *next* revision, as the interesting change
     happens when we move to the previous revision */
  svn_revnum_t last_revnum;
  apr_hash_t *last_props;
};

/* The baton used by the txdelta window handler. Allocated per revision */
struct delta_baton {
  /* Our underlying handler/baton that we wrap */
  svn_txdelta_window_handler_t wrapped_handler;
  void *wrapped_baton;
  struct file_rev_baton *file_rev_baton;
  svn_stream_t *source_stream;  /* the delta source */
  const char *filename;
  svn_boolean_t is_merged_revision;
  struct rev *rev;     /* the rev struct for the current revision */
};




/* Return a blame chunk associated with REV for a change starting
   at token START, and allocated in CHAIN->mainpool. */
static struct blame *
blame_create(struct blame_chain *chain,
             const struct rev *rev,
             apr_off_t start)
{
  struct blame *blame;
  if (chain->avail)
    {
      blame = chain->avail;
      chain->avail = blame->next;
    }
  else
    blame = apr_palloc(chain->pool, sizeof(*blame));
  blame->rev = rev;
  blame->start = start;
  blame->next = NULL;
  return blame;
}

/* Destroy a blame chunk. */
static void
blame_destroy(struct blame_chain *chain,
              struct blame *blame)
{
  blame->next = chain->avail;
  chain->avail = blame;
}

/* Return the blame chunk that contains token OFF, starting the search at
   BLAME. */
static struct blame *
blame_find(struct blame *blame, apr_off_t off)
{
  struct blame *prev = NULL;
  while (blame)
    {
      if (blame->start > off) break;
      prev = blame;
      blame = blame->next;
    }
  return prev;
}

/* Shift the start-point of BLAME and all subsequence blame-chunks
   by ADJUST tokens */
static void
blame_adjust(struct blame *blame, apr_off_t adjust)
{
  while (blame)
    {
      blame->start += adjust;
      blame = blame->next;
    }
}

/* Delete the blame associated with the region from token START to
   START + LENGTH */
static svn_error_t *
blame_delete_range(struct blame_chain *chain,
                   apr_off_t start,
                   apr_off_t length)
{
  struct blame *first = blame_find(chain->blame, start);
  struct blame *last = blame_find(chain->blame, start + length);
  struct blame *tail = last->next;

  if (first != last)
    {
      struct blame *walk = first->next;
      while (walk != last)
        {
          struct blame *next = walk->next;
          blame_destroy(chain, walk);
          walk = next;
        }
      first->next = last;
      last->start = start;
      if (first->start == start)
        {
          *first = *last;
          blame_destroy(chain, last);
          last = first;
        }
    }

  if (tail && tail->start == last->start + length)
    {
      *last = *tail;
      blame_destroy(chain, tail);
      tail = last->next;
    }

  blame_adjust(tail, -length);

  return SVN_NO_ERROR;
}

/* Insert a chunk of blame associated with REV starting
   at token START and continuing for LENGTH tokens */
static svn_error_t *
blame_insert_range(struct blame_chain *chain,
                   const struct rev *rev,
                   apr_off_t start,
                   apr_off_t length)
{
  struct blame *head = chain->blame;
  struct blame *point = blame_find(head, start);
  struct blame *insert;

  if (point->start == start)
    {
      insert = blame_create(chain, point->rev, point->start + length);
      point->rev = rev;
      insert->next = point->next;
      point->next = insert;
    }
  else
    {
      struct blame *middle;
      middle = blame_create(chain, rev, start);
      insert = blame_create(chain, point->rev, start + length);
      middle->next = insert;
      insert->next = point->next;
      point->next = middle;
    }
  blame_adjust(insert->next, length);

  return SVN_NO_ERROR;
}

/* Callback for diff between subsequent revisions */
static svn_error_t *
output_diff_modified(void *baton,
                     apr_off_t original_start,
                     apr_off_t original_length,
                     apr_off_t modified_start,
                     apr_off_t modified_length,
                     apr_off_t latest_start,
                     apr_off_t latest_length)
{
  struct diff_baton *db = baton;

  if (original_length)
    SVN_ERR(blame_delete_range(db->chain, modified_start, original_length));

  if (modified_length)
    SVN_ERR(blame_insert_range(db->chain, db->rev, modified_start,
                               modified_length));

  return SVN_NO_ERROR;
}

static const svn_diff_output_fns_t output_fns = {
        NULL,
        output_diff_modified
};

/* Add the blame for the diffs between LAST_FILE and CUR_FILE to CHAIN,
   for revision REV.  LAST_FILE may be NULL in which
   case blame is added for every line of CUR_FILE. */
static svn_error_t *
add_file_blame(const char *last_file,
               const char *cur_file,
               struct blame_chain *chain,
               struct rev *rev,
               const svn_diff_file_options_t *diff_options,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  if (!last_file)
    {
      SVN_ERR_ASSERT(chain->blame == NULL);
      chain->blame = blame_create(chain, rev, 0);
    }
  else
    {
      svn_diff_t *diff;
      struct diff_baton diff_baton;

      diff_baton.chain = chain;
      diff_baton.rev = rev;

      /* We have a previous file.  Get the diff and adjust blame info. */
      SVN_ERR(svn_diff_file_diff_2(&diff, last_file, cur_file,
                                   diff_options, pool));
      SVN_ERR(svn_diff_output2(diff, &diff_baton, &output_fns,
                               cancel_func, cancel_baton));
    }

  return SVN_NO_ERROR;
}

/* Record the blame information for the revision in BATON->file_rev_baton.
 */
static svn_error_t *
update_blame(void *baton)
{
  struct delta_baton *dbaton = baton;
  struct file_rev_baton *frb = dbaton->file_rev_baton;
  struct blame_chain *chain;

  /* Close the source file used for the delta.
     It is important to do this early, since otherwise, they will be deleted
     before all handles are closed, which leads to failures on some platforms
     when new tempfiles are to be created. */
  if (dbaton->source_stream)
    SVN_ERR(svn_stream_close(dbaton->source_stream));

  /* If we are including merged revisions, we need to add each rev to the
     merged chain. */
  if (frb->include_merged_revisions)
    chain = frb->merged_chain;
  else
    chain = frb->chain;

  /* Process this file. */
  SVN_ERR(add_file_blame(frb->last_filename,
                         dbaton->filename, chain, dbaton->rev,
                         frb->diff_options,
                         frb->ctx->cancel_func, frb->ctx->cancel_baton,
                         frb->currpool));

  /* If we are including merged revisions, and the current revision is not a
     merged one, we need to add its blame info to the chain for the original
     line of history. */
  if (frb->include_merged_revisions && ! dbaton->is_merged_revision)
    {
      apr_pool_t *tmppool;

      SVN_ERR(add_file_blame(frb->last_original_filename,
                             dbaton->filename, frb->chain, dbaton->rev,
                             frb->diff_options,
                             frb->ctx->cancel_func, frb->ctx->cancel_baton,
                             frb->currpool));

      /* This filename could be around for a while, potentially, so
         use the longer lifetime pool, and switch it with the previous one*/
      svn_pool_clear(frb->prevfilepool);
      tmppool = frb->filepool;
      frb->filepool = frb->prevfilepool;
      frb->prevfilepool = tmppool;

      frb->last_original_filename = apr_pstrdup(frb->filepool,
                                                dbaton->filename);
    }

  /* Prepare for next revision. */

  /* Remember the file name so we can diff it with the next revision. */
  frb->last_filename = dbaton->filename;

  /* Switch pools. */
  {
    apr_pool_t *tmp_pool = frb->lastpool;
    frb->lastpool = frb->currpool;
    frb->currpool = tmp_pool;
  }

  return SVN_NO_ERROR;
}

/* The delta window handler for the text delta between the previously seen
 * revision and the revision currently being handled.
 *
 * Record the blame information for this revision in BATON->file_rev_baton.
 *
 * Implements svn_txdelta_window_handler_t.
 */
static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct delta_baton *dbaton = baton;

  /* Call the wrapped handler first. */
  if (dbaton->wrapped_handler)
    SVN_ERR(dbaton->wrapped_handler(window, dbaton->wrapped_baton));

  /* We patiently wait for the NULL window marking the end. */
  if (window)
    return SVN_NO_ERROR;

  /* Diff and update blame info. */
  SVN_ERR(update_blame(baton));

  return SVN_NO_ERROR;
}


/* Calculate and record blame information for one revision of the file,
 * by comparing the file content against the previously seen revision.
 *
 * This handler is called once for each interesting revision of the file.
 *
 * Record the blame information for this revision in (file_rev_baton) BATON.
 *
 * Implements svn_file_rev_handler_t.
 */
static svn_error_t *
file_rev_handler(void *baton, const char *path, svn_revnum_t revnum,
                 apr_hash_t *rev_props,
                 svn_boolean_t merged_revision,
                 svn_txdelta_window_handler_t *content_delta_handler,
                 void **content_delta_baton,
                 apr_array_header_t *prop_diffs,
                 apr_pool_t *pool)
{
  struct file_rev_baton *frb = baton;
  svn_stream_t *last_stream;
  svn_stream_t *cur_stream;
  struct delta_baton *delta_baton;
  apr_pool_t *filepool;

  /* Clear the current pool. */
  svn_pool_clear(frb->currpool);

  if (frb->check_mime_type)
    {
      apr_hash_t *props = svn_prop_array_to_hash(prop_diffs, frb->currpool);
      const char *value;

      frb->check_mime_type = FALSE; /* Only check first */

      value = svn_prop_get_value(props, SVN_PROP_MIME_TYPE);

      if (value && svn_mime_type_is_binary(value))
        {
          return svn_error_createf(
              SVN_ERR_CLIENT_IS_BINARY_FILE, NULL,
              _("Cannot calculate blame information for binary file '%s'"),
               (svn_path_is_url(frb->target)
                      ? frb->target 
                      : svn_dirent_local_style(frb->target, pool)));
        }
    }

  if (frb->ctx->notify_func2)
    {
      svn_wc_notify_t *notify
            = svn_wc_create_notify_url(
                            svn_path_url_add_component2(frb->repos_root_url,
                                                        path+1, pool),
                            svn_wc_notify_blame_revision, pool);
      notify->path = path;
      notify->kind = svn_node_none;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      notify->revision = revnum;
      notify->rev_props = rev_props;
      frb->ctx->notify_func2(frb->ctx->notify_baton2, notify, pool);
    }

  if (frb->ctx->cancel_func)
    SVN_ERR(frb->ctx->cancel_func(frb->ctx->cancel_baton));

  /* If there were no content changes and no (potential) merges, we couldn't
     care less about this revision now.  Note that we checked the mime type
     above, so things work if the user just changes the mime type in a commit.
     Also note that we don't switch the pools in this case.  This is important,
     since the tempfile will be removed by the pool and we need the tempfile
     from the last revision with content changes. */
  if (!content_delta_handler
      && (!frb->include_merged_revisions || merged_revision))
    return SVN_NO_ERROR;

  /* Create delta baton. */
  delta_baton = apr_pcalloc(frb->currpool, sizeof(*delta_baton));

  /* Prepare the text delta window handler. */
  if (frb->last_filename)
    SVN_ERR(svn_stream_open_readonly(&delta_baton->source_stream, frb->last_filename,
                                     frb->currpool, pool));
  else
    /* Means empty stream below. */
    delta_baton->source_stream = NULL;
  last_stream = svn_stream_disown(delta_baton->source_stream, pool);

  if (frb->include_merged_revisions && !merged_revision)
    filepool = frb->filepool;
  else
    filepool = frb->currpool;

  SVN_ERR(svn_stream_open_unique(&cur_stream, &delta_baton->filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 filepool, filepool));

  /* Wrap the window handler with our own. */
  delta_baton->file_rev_baton = frb;
  delta_baton->is_merged_revision = merged_revision;

  /* Create the rev structure. */
  delta_baton->rev = apr_pcalloc(frb->mainpool, sizeof(struct rev));

  if (frb->backwards)
    {
      /* Use from last round...
         SVN_INVALID_REVNUM on first, which is exactly
         what we want */
      delta_baton->rev->revision = frb->last_revnum;
      delta_baton->rev->rev_props = frb->last_props;

      /* Store for next delta */
      if (revnum >= MIN(frb->start_rev, frb->end_rev))
        {
          frb->last_revnum = revnum;
          frb->last_props = svn_prop_hash_dup(rev_props, frb->mainpool);
        }
      /* Else: Not needed on last rev */
    }
  else if (merged_revision
           || (revnum >= MIN(frb->start_rev, frb->end_rev)))
    {
      /* 1+ for the "youngest to oldest" blame */
      SVN_ERR_ASSERT(revnum <= 1 + MAX(frb->end_rev, frb->start_rev));

      /* Set values from revision props. */
      delta_baton->rev->revision = revnum;
      delta_baton->rev->rev_props = svn_prop_hash_dup(rev_props, frb->mainpool);
    }
  else
    {
      /* We shouldn't get more than one revision outside the
         specified range (unless we alsoe receive merged revisions) */
      SVN_ERR_ASSERT((frb->last_filename == NULL)
                     || frb->include_merged_revisions);

      /* The file existed before start_rev; generate no blame info for
         lines from this revision (or before). 

         This revision specifies the state as it was at the start revision */

      delta_baton->rev->revision = SVN_INVALID_REVNUM;
    }

  if (frb->include_merged_revisions)
    delta_baton->rev->path = apr_pstrdup(frb->mainpool, path);

  /* Keep last revision for postprocessing after all changes */
  frb->last_rev = delta_baton->rev;

  /* Handle all delta - even if it is empty.
     We must do the latter to "merge" blame info from other branches. */
  if (content_delta_handler)
    {
      /* Proper delta - get window handler for applying delta.
         svn_ra_get_file_revs2 will drive the delta editor. */
      svn_txdelta_apply(last_stream, cur_stream, NULL, NULL,
                        frb->currpool,
                        &delta_baton->wrapped_handler,
                        &delta_baton->wrapped_baton);
      *content_delta_handler = window_handler;
      *content_delta_baton = delta_baton;
    }
  else
    {
      /* Apply an empty delta, i.e. simply copy the old contents.
         We can't simply use the existing file due to the pool rotation logic.
         Trigger the blame update magic. */
      SVN_ERR(svn_stream_copy3(last_stream, cur_stream, NULL, NULL, pool));
      SVN_ERR(update_blame(delta_baton));
    }

  return SVN_NO_ERROR;
}

/* Ensure that CHAIN_ORIG and CHAIN_MERGED have the same number of chunks,
   and that for every chunk C, CHAIN_ORIG[C] and CHAIN_MERGED[C] have the
   same starting value.  Both CHAIN_ORIG and CHAIN_MERGED should not be
   NULL.  */
static void
normalize_blames(struct blame_chain *chain,
                 struct blame_chain *chain_merged,
                 apr_pool_t *pool)
{
  struct blame *walk, *walk_merged;

  /* Walk over the CHAIN's blame chunks and CHAIN_MERGED's blame chunks,
     creating new chunks as needed. */
  for (walk = chain->blame, walk_merged = chain_merged->blame;
       walk->next && walk_merged->next;
       walk = walk->next, walk_merged = walk_merged->next)
    {
      /* The current chunks should always be starting at the same offset. */
      assert(walk->start == walk_merged->start);

      if (walk->next->start < walk_merged->next->start)
        {
          /* insert a new chunk in CHAIN_MERGED. */
          struct blame *tmp = blame_create(chain_merged, walk_merged->rev,
                                           walk->next->start);
          tmp->next = walk_merged->next;
          walk_merged->next = tmp;
        }

      if (walk->next->start > walk_merged->next->start)
        {
          /* insert a new chunk in CHAIN. */
          struct blame *tmp = blame_create(chain, walk->rev,
                                           walk_merged->next->start);
          tmp->next = walk->next;
          walk->next = tmp;
        }
    }

  /* If both NEXT pointers are null, the lists are equally long, otherwise
     we need to extend one of them.  If CHAIN is longer, append new chunks
     to CHAIN_MERGED until its length matches that of CHAIN. */
  while (walk->next != NULL)
    {
      struct blame *tmp = blame_create(chain_merged, walk_merged->rev,
                                       walk->next->start);
      walk_merged->next = tmp;

      walk_merged = walk_merged->next;
      walk = walk->next;
    }

  /* Same as above, only extend CHAIN to match CHAIN_MERGED. */
  while (walk_merged->next != NULL)
    {
      struct blame *tmp = blame_create(chain, walk->rev,
                                       walk_merged->next->start);
      walk->next = tmp;

      walk = walk->next;
      walk_merged = walk_merged->next;
    }
}

svn_error_t *
svn_client_blame5(const char *target,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  const svn_diff_file_options_t *diff_options,
                  svn_boolean_t ignore_mime_type,
                  svn_boolean_t include_merged_revisions,
                  svn_client_blame_receiver3_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  struct file_rev_baton frb;
  svn_ra_session_t *ra_session;
  svn_revnum_t start_revnum, end_revnum;
  struct blame *walk, *walk_merged = NULL;
  apr_pool_t *iterpool;
  svn_stream_t *last_stream;
  svn_stream_t *stream;
  const char *target_abspath_or_url;

  if (start->kind == svn_opt_revision_unspecified
      || end->kind == svn_opt_revision_unspecified)
    return svn_error_create
      (SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  if (svn_path_is_url(target))
    target_abspath_or_url = target;
  else
    SVN_ERR(svn_dirent_get_absolute(&target_abspath_or_url, target, pool));

  /* Get an RA plugin for this filesystem object. */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, NULL,
                                            target, NULL, peg_revision,
                                            peg_revision,
                                            ctx, pool));

  SVN_ERR(svn_client__get_revision_number(&start_revnum, NULL, ctx->wc_ctx,
                                          target_abspath_or_url, ra_session,
                                          start, pool));

  SVN_ERR(svn_client__get_revision_number(&end_revnum, NULL, ctx->wc_ctx,
                                          target_abspath_or_url, ra_session,
                                          end, pool));

  {
    svn_client__pathrev_t *loc;
    svn_opt_revision_t younger_end;
    younger_end.kind = svn_opt_revision_number;
    younger_end.value.number = MAX(start_revnum, end_revnum);

    SVN_ERR(svn_client__resolve_rev_and_url(&loc, ra_session,
                                            target, peg_revision,
                                            &younger_end,
                                            ctx, pool));

    /* Make the session point to the real URL. */
    SVN_ERR(svn_ra_reparent(ra_session, loc->url, pool));
  }

  /* We check the mime-type of the yougest revision before getting all
     the older revisions. */
  if (!ignore_mime_type
      && start_revnum < end_revnum)
    {
      apr_hash_t *props;
      const char *mime_type = NULL;

      if (svn_path_is_url(target)
          || start_revnum > end_revnum
          || (end->kind != svn_opt_revision_working
              && end->kind != svn_opt_revision_base))
        {
          SVN_ERR(svn_ra_get_file(ra_session, "", end_revnum, NULL, NULL,
                                  &props, pool));

          mime_type = svn_prop_get_value(props, SVN_PROP_MIME_TYPE);
        }
      else 
        {
          const svn_string_t *value;

          if (end->kind == svn_opt_revision_working)
            SVN_ERR(svn_wc_prop_get2(&value, ctx->wc_ctx,
                                     target_abspath_or_url,
                                     SVN_PROP_MIME_TYPE,
                                     pool, pool));
          else
            {
              SVN_ERR(svn_wc_get_pristine_props(&props, ctx->wc_ctx,
                                                target_abspath_or_url,
                                                pool, pool));

              value = props ? svn_hash_gets(props, SVN_PROP_MIME_TYPE)
                            : NULL;
            }

          mime_type = value ? value->data : NULL;
        }

      if (mime_type)
        {
          if (svn_mime_type_is_binary(mime_type))
            return svn_error_createf
              (SVN_ERR_CLIENT_IS_BINARY_FILE, 0,
               _("Cannot calculate blame information for binary file '%s'"),
               (svn_path_is_url(target)
                ? target : svn_dirent_local_style(target, pool)));
        }
    }

  frb.start_rev = start_revnum;
  frb.end_rev = end_revnum;
  frb.target = target;
  frb.ctx = ctx;
  frb.diff_options = diff_options;
  frb.include_merged_revisions = include_merged_revisions;
  frb.last_filename = NULL;
  frb.last_rev = NULL;
  frb.last_original_filename = NULL;
  frb.chain = apr_palloc(pool, sizeof(*frb.chain));
  frb.chain->blame = NULL;
  frb.chain->avail = NULL;
  frb.chain->pool = pool;
  if (include_merged_revisions)
    {
      frb.merged_chain = apr_palloc(pool, sizeof(*frb.merged_chain));
      frb.merged_chain->blame = NULL;
      frb.merged_chain->avail = NULL;
      frb.merged_chain->pool = pool;
    }
  frb.backwards = (frb.start_rev > frb.end_rev);
  frb.last_revnum = SVN_INVALID_REVNUM;
  frb.last_props = NULL;
  frb.check_mime_type = (frb.backwards && !ignore_mime_type);

  SVN_ERR(svn_ra_get_repos_root2(ra_session, &frb.repos_root_url, pool));

  frb.mainpool = pool;
  /* The callback will flip the following two pools, because it needs
     information from the previous call.  Obviously, it can't rely on
     the lifetime of the pool provided by get_file_revs. */
  frb.lastpool = svn_pool_create(pool);
  frb.currpool = svn_pool_create(pool);
  if (include_merged_revisions)
    {
      frb.filepool = svn_pool_create(pool);
      frb.prevfilepool = svn_pool_create(pool);
    }

  /* Collect all blame information.
     We need to ensure that we get one revision before the start_rev,
     if available so that we can know what was actually changed in the start
     revision. */
  SVN_ERR(svn_ra_get_file_revs2(ra_session, "",
                                frb.backwards ? start_revnum
                                              : MAX(0, start_revnum-1),
                                end_revnum,
                                include_merged_revisions,
                                file_rev_handler, &frb, pool));

  if (end->kind == svn_opt_revision_working)
    {
      /* If the local file is modified we have to call the handler on the
         working copy file with keywords unexpanded */
      svn_wc_status3_t *status;

      SVN_ERR(svn_wc_status3(&status, ctx->wc_ctx, target_abspath_or_url, pool,
                             pool));

      if (status->text_status != svn_wc_status_normal
          || (status->prop_status != svn_wc_status_normal
              && status->prop_status != svn_wc_status_none))
        {
          svn_stream_t *wcfile;
          svn_stream_t *tempfile;
          svn_opt_revision_t rev;
          svn_boolean_t normalize_eols = FALSE;
          const char *temppath;

          if (status->prop_status != svn_wc_status_none)
            {
              const svn_string_t *eol_style;
              SVN_ERR(svn_wc_prop_get2(&eol_style, ctx->wc_ctx,
                                       target_abspath_or_url,
                                       SVN_PROP_EOL_STYLE,
                                       pool, pool));

              if (eol_style)
                {
                  svn_subst_eol_style_t style;
                  const char *eol;
                  svn_subst_eol_style_from_value(&style, &eol, eol_style->data);

                  normalize_eols = (style == svn_subst_eol_style_native);
                }
            }

          rev.kind = svn_opt_revision_working;
          SVN_ERR(svn_client__get_normalized_stream(&wcfile, ctx->wc_ctx,
                                                    target_abspath_or_url, &rev,
                                                    FALSE, normalize_eols,
                                                    ctx->cancel_func,
                                                    ctx->cancel_baton,
                                                    pool, pool));

          SVN_ERR(svn_stream_open_unique(&tempfile, &temppath, NULL,
                                         svn_io_file_del_on_pool_cleanup,
                                         pool, pool));

          SVN_ERR(svn_stream_copy3(wcfile, tempfile, ctx->cancel_func,
                                   ctx->cancel_baton, pool));

          SVN_ERR(add_file_blame(frb.last_filename, temppath, frb.chain, NULL,
                                 frb.diff_options,
                                 ctx->cancel_func, ctx->cancel_baton, pool));

          frb.last_filename = temppath;
        }
    }

  /* Report the blame to the caller. */

  /* The callback has to have been called at least once. */
  SVN_ERR_ASSERT(frb.last_filename != NULL);

  /* Create a pool for the iteration below. */
  iterpool = svn_pool_create(pool);

  /* Open the last file and get a stream. */
  SVN_ERR(svn_stream_open_readonly(&last_stream, frb.last_filename,
                                   pool, pool));
  stream = svn_subst_stream_translated(last_stream,
                                       "\n", TRUE, NULL, FALSE, pool);

  /* Perform optional merged chain normalization. */
  if (include_merged_revisions)
    {
      /* If we never created any blame for the original chain, create it now,
         with the most recent changed revision.  This could occur if a file
         was created on a branch and them merged to another branch.  This is
         semanticly a copy, and we want to use the revision on the branch as
         the most recently changed revision.  ### Is this really what we want
         to do here?  Do the sematics of copy change? */
      if (!frb.chain->blame)
        frb.chain->blame = blame_create(frb.chain, frb.last_rev, 0);

      normalize_blames(frb.chain, frb.merged_chain, pool);
      walk_merged = frb.merged_chain->blame;
    }

  /* Process each blame item. */
  for (walk = frb.chain->blame; walk; walk = walk->next)
    {
      apr_off_t line_no;
      svn_revnum_t merged_rev;
      const char *merged_path;
      apr_hash_t *merged_rev_props;

      if (walk_merged)
        {
          merged_rev = walk_merged->rev->revision;
          merged_rev_props = walk_merged->rev->rev_props;
          merged_path = walk_merged->rev->path;
        }
      else
        {
          merged_rev = SVN_INVALID_REVNUM;
          merged_rev_props = NULL;
          merged_path = NULL;
        }

      for (line_no = walk->start;
           !walk->next || line_no < walk->next->start;
           ++line_no)
        {
          svn_boolean_t eof;
          svn_stringbuf_t *sb;

          svn_pool_clear(iterpool);
          SVN_ERR(svn_stream_readline(stream, &sb, "\n", &eof, iterpool));
          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));
          if (!eof || sb->len)
            {
              if (walk->rev)
                SVN_ERR(receiver(receiver_baton, start_revnum, end_revnum,
                                 line_no, walk->rev->revision,
                                 walk->rev->rev_props, merged_rev,
                                 merged_rev_props, merged_path,
                                 sb->data, FALSE, iterpool));
              else
                SVN_ERR(receiver(receiver_baton, start_revnum, end_revnum,
                                 line_no, SVN_INVALID_REVNUM,
                                 NULL, SVN_INVALID_REVNUM,
                                 NULL, NULL,
                                 sb->data, TRUE, iterpool));
            }
          if (eof) break;
        }

      if (walk_merged)
        walk_merged = walk_merged->next;
    }

  SVN_ERR(svn_stream_close(stream));

  svn_pool_destroy(frb.lastpool);
  svn_pool_destroy(frb.currpool);
  if (include_merged_revisions)
    {
      svn_pool_destroy(frb.filepool);
      svn_pool_destroy(frb.prevfilepool);
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
