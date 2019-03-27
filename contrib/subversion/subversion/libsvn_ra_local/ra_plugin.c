/*
 * ra_plugin.c : the main RA module for local repository access
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

#include "ra_local.h"
#include "svn_hash.h"
#include "svn_ra.h"
#include "svn_fs.h"
#include "svn_delta.h"
#include "svn_repos.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_path.h"
#include "svn_version.h"
#include "svn_cache_config.h"

#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_repos_private.h"
#include "private/svn_fspath.h"
#include "private/svn_atomic.h"
#include "private/svn_subr_private.h"

#define APR_WANT_STRFUNC
#include <apr_want.h>

/*----------------------------------------------------------------*/

/*** Miscellaneous helper functions ***/


/* Pool cleanup handler: ensure that the access descriptor of the
   filesystem (svn_fs_t *) DATA is set to NULL. */
static apr_status_t
cleanup_access(void *data)
{
  svn_error_t *serr;
  svn_fs_t *fs = data;

  serr = svn_fs_set_access(fs, NULL);

  if (serr)
    {
      apr_status_t apr_err = serr->apr_err;
      svn_error_clear(serr);
      return apr_err;
    }

  return APR_SUCCESS;
}


/* Fetch a username for use with SESSION, and store it in SESSION->username.
 *
 * Allocate the username in SESSION->pool.  Use SCRATCH_POOL for temporary
 * allocations. */
static svn_error_t *
get_username(svn_ra_session_t *session,
             apr_pool_t *scratch_pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;

  /* If we've already found the username don't ask for it again. */
  if (! sess->username)
    {
      /* Get a username somehow, so we have some svn:author property to
         attach to a commit. */
      if (sess->auth_baton)
        {
          void *creds;
          svn_auth_cred_username_t *username_creds;
          svn_auth_iterstate_t *iterstate;

          SVN_ERR(svn_auth_first_credentials(&creds, &iterstate,
                                             SVN_AUTH_CRED_USERNAME,
                                             sess->uuid, /* realmstring */
                                             sess->auth_baton,
                                             scratch_pool));

          /* No point in calling next_creds(), since that assumes that the
             first_creds() somehow failed to authenticate.  But there's no
             challenge going on, so we use whatever creds we get back on
             the first try. */
          username_creds = creds;
          if (username_creds && username_creds->username)
            {
              sess->username = apr_pstrdup(session->pool,
                                           username_creds->username);
              svn_error_clear(svn_auth_save_credentials(iterstate,
                                                        scratch_pool));
            }
          else
            sess->username = "";
        }
      else
        sess->username = "";
    }

  /* If we have a real username, attach it to the filesystem so that it can
     be used to validate locks.  Even if there already is a user context
     associated, it may contain irrelevant lock tokens, so always create a new.
  */
  if (*sess->username)
    {
      svn_fs_access_t *access_ctx;

      SVN_ERR(svn_fs_create_access(&access_ctx, sess->username,
                                   session->pool));
      SVN_ERR(svn_fs_set_access(sess->fs, access_ctx));

      /* Make sure this context is disassociated when the pool gets
         destroyed. */
      apr_pool_cleanup_register(session->pool, sess->fs, cleanup_access,
                                apr_pool_cleanup_null);
    }

  return SVN_NO_ERROR;
}

/* Implements an svn_atomic__init_once callback.  Sets the FSFS memory
   cache size. */
static svn_error_t *
cache_init(void *baton, apr_pool_t *pool)
{
  apr_hash_t *config_hash = baton;
  svn_config_t *config = NULL;
  const char *memory_cache_size_str;

  if (config_hash)
    config = svn_hash_gets(config_hash, SVN_CONFIG_CATEGORY_CONFIG);
  svn_config_get(config, &memory_cache_size_str, SVN_CONFIG_SECTION_MISCELLANY,
                 SVN_CONFIG_OPTION_MEMORY_CACHE_SIZE, NULL);
  if (memory_cache_size_str)
    {
      apr_uint64_t memory_cache_size;
      svn_cache_config_t settings = *svn_cache_config_get();

      SVN_ERR(svn_error_quick_wrap(svn_cstring_atoui64(&memory_cache_size,
                                                       memory_cache_size_str),
                                   _("memory-cache-size invalid")));
      settings.cache_size = 1024 * 1024 * memory_cache_size;
      svn_cache_config_set(&settings);
    }

  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------*/

/*** The reporter vtable needed by do_update() and friends ***/

typedef struct reporter_baton_t
{
  svn_ra_local__session_baton_t *sess;
  void *report_baton;

} reporter_baton_t;


static void *
make_reporter_baton(svn_ra_local__session_baton_t *sess,
                    void *report_baton,
                    apr_pool_t *pool)
{
  reporter_baton_t *rbaton = apr_palloc(pool, sizeof(*rbaton));
  rbaton->sess = sess;
  rbaton->report_baton = report_baton;
  return rbaton;
}


static svn_error_t *
reporter_set_path(void *reporter_baton,
                  const char *path,
                  svn_revnum_t revision,
                  svn_depth_t depth,
                  svn_boolean_t start_empty,
                  const char *lock_token,
                  apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_repos_set_path3(rbaton->report_baton, path,
                             revision, depth, start_empty, lock_token, pool);
}


static svn_error_t *
reporter_delete_path(void *reporter_baton,
                     const char *path,
                     apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_repos_delete_path(rbaton->report_baton, path, pool);
}


static svn_error_t *
reporter_link_path(void *reporter_baton,
                   const char *path,
                   const char *url,
                   svn_revnum_t revision,
                   svn_depth_t depth,
                   svn_boolean_t start_empty,
                   const char *lock_token,
                   apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  const char *repos_url = rbaton->sess->repos_url;
  const char *relpath = svn_uri_skip_ancestor(repos_url, url, pool);
  const char *fs_path;

  if (!relpath)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("'%s'\n"
                               "is not the same repository as\n"
                               "'%s'"), url, rbaton->sess->repos_url);

  /* Convert the relpath to an fspath */
  if (relpath[0] == '\0')
    fs_path = "/";
  else
    fs_path = apr_pstrcat(pool, "/", relpath, SVN_VA_NULL);

  return svn_repos_link_path3(rbaton->report_baton, path, fs_path, revision,
                              depth, start_empty, lock_token, pool);
}


static svn_error_t *
reporter_finish_report(void *reporter_baton,
                       apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_repos_finish_report(rbaton->report_baton, pool);
}


static svn_error_t *
reporter_abort_report(void *reporter_baton,
                      apr_pool_t *pool)
{
  reporter_baton_t *rbaton = reporter_baton;
  return svn_repos_abort_report(rbaton->report_baton, pool);
}


static const svn_ra_reporter3_t ra_local_reporter =
{
  reporter_set_path,
  reporter_delete_path,
  reporter_link_path,
  reporter_finish_report,
  reporter_abort_report
};


/* ...
 *
 * Wrap a cancellation editor using SESSION's cancellation function around
 * the supplied EDITOR.  ### Some callers (via svn_ra_do_update2() etc.)
 * don't appear to know that we do this, and are supplying an editor that
 * they have already wrapped with the same cancellation editor, so it ends
 * up double-wrapped.
 *
 * Allocate @a *reporter and @a *report_baton in @a result_pool.  Use
 * @a scratch_pool for temporary allocations.
 */
static svn_error_t *
make_reporter(svn_ra_session_t *session,
              const svn_ra_reporter3_t **reporter,
              void **report_baton,
              svn_revnum_t revision,
              const char *target,
              const char *other_url,
              svn_boolean_t text_deltas,
              svn_depth_t depth,
              svn_boolean_t send_copyfrom_args,
              svn_boolean_t ignore_ancestry,
              const svn_delta_editor_t *editor,
              void *edit_baton,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  void *rbaton;
  const char *other_fs_path = NULL;

  /* Get the HEAD revision if one is not supplied. */
  if (! SVN_IS_VALID_REVNUM(revision))
    SVN_ERR(svn_fs_youngest_rev(&revision, sess->fs, scratch_pool));

  /* If OTHER_URL was provided, validate it and convert it into a
     regular filesystem path. */
  if (other_url)
    {
      const char *other_relpath
        = svn_uri_skip_ancestor(sess->repos_url, other_url, scratch_pool);

      /* Sanity check:  the other_url better be in the same repository as
         the original session url! */
      if (! other_relpath)
        return svn_error_createf
          (SVN_ERR_RA_ILLEGAL_URL, NULL,
           _("'%s'\n"
             "is not the same repository as\n"
             "'%s'"), other_url, sess->repos_url);

      other_fs_path = apr_pstrcat(scratch_pool, "/", other_relpath,
                                  SVN_VA_NULL);
    }

  /* Pass back our reporter */
  *reporter = &ra_local_reporter;

  SVN_ERR(get_username(session, scratch_pool));

  if (sess->callbacks)
    SVN_ERR(svn_delta_get_cancellation_editor(sess->callbacks->cancel_func,
                                              sess->callback_baton,
                                              editor,
                                              edit_baton,
                                              &editor,
                                              &edit_baton,
                                              result_pool));

  /* Build a reporter baton. */
  SVN_ERR(svn_repos_begin_report3(&rbaton,
                                  revision,
                                  sess->repos,
                                  sess->fs_path->data,
                                  target,
                                  other_fs_path,
                                  text_deltas,
                                  depth,
                                  ignore_ancestry,
                                  send_copyfrom_args,
                                  editor,
                                  edit_baton,
                                  NULL,
                                  NULL,
                                  0, /* Disable zero-copy codepath, because
                                        RA API users are unaware about the
                                        zero-copy code path limitation (do
                                        not access FSFS data structures
                                        and, hence, caches).  See notes
                                        to svn_repos_begin_report3() for
                                        additional details. */
                                  result_pool));

  /* Wrap the report baton given us by the repos layer with our own
     reporter baton. */
  *report_baton = make_reporter_baton(sess, rbaton, result_pool);

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------*/

/*** Deltification stuff for get_commit_editor() ***/

struct deltify_etc_baton
{
  svn_fs_t *fs;                     /* the fs to deltify in */
  svn_repos_t *repos;               /* repos for unlocking */
  const char *fspath_base;          /* fs-path part of split session URL */

  apr_hash_t *lock_tokens;          /* tokens to unlock, if any */

  svn_commit_callback2_t commit_cb; /* the original callback */
  void *commit_baton;               /* the original callback's baton */
};

/* This implements 'svn_commit_callback_t'.  Its invokes the original
   (wrapped) callback, but also does deltification on the new revision and
   possibly unlocks committed paths.
   BATON is 'struct deltify_etc_baton *'. */
static svn_error_t *
deltify_etc(const svn_commit_info_t *commit_info,
            void *baton,
            apr_pool_t *scratch_pool)
{
  struct deltify_etc_baton *deb = baton;
  svn_error_t *err1 = SVN_NO_ERROR;
  svn_error_t *err2;

  /* Invoke the original callback first, in case someone's waiting to
     know the revision number so they can go off and annotate an
     issue or something. */
  if (deb->commit_cb)
    err1 = deb->commit_cb(commit_info, deb->commit_baton, scratch_pool);

  /* Maybe unlock the paths. */
  if (deb->lock_tokens)
    {
      apr_pool_t *subpool = svn_pool_create(scratch_pool);
      apr_hash_t *targets = apr_hash_make(subpool);
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(subpool, deb->lock_tokens); hi;
           hi = apr_hash_next(hi))
        {
          const void *relpath = apr_hash_this_key(hi);
          const char *token = apr_hash_this_val(hi);
          const char *fspath;

          fspath = svn_fspath__join(deb->fspath_base, relpath, subpool);
          svn_hash_sets(targets, fspath, token);
        }

      /* We may get errors here if the lock was broken or stolen
         after the commit succeeded.  This is fine and should be
         ignored. */
      svn_error_clear(svn_repos_fs_unlock_many(deb->repos, targets, FALSE,
                                               NULL, NULL,
                                               subpool, subpool));

      svn_pool_destroy(subpool);
    }

  /* But, deltification shouldn't be stopped just because someone's
     random callback failed, so proceed unconditionally on to
     deltification. */
  err2 = svn_fs_deltify_revision(deb->fs, commit_info->revision, scratch_pool);

  return svn_error_compose_create(err1, err2);
}


/* If LOCK_TOKENS is not NULL, then copy all tokens into the access context
   of FS. The tokens' paths will be prepended with FSPATH_BASE.

   ACCESS_POOL must match (or exceed) the lifetime of the access context
   that was associated with FS. Typically, this is the session pool.

   Temporary allocations are made in SCRATCH_POOL.  */
static svn_error_t *
apply_lock_tokens(svn_fs_t *fs,
                  const char *fspath_base,
                  apr_hash_t *lock_tokens,
                  apr_pool_t *access_pool,
                  apr_pool_t *scratch_pool)
{
  if (lock_tokens)
    {
      svn_fs_access_t *access_ctx;

      SVN_ERR(svn_fs_get_access(&access_ctx, fs));

      /* If there is no access context, the filesystem will scream if a
         lock is needed.  */
      if (access_ctx)
        {
          apr_hash_index_t *hi;

          /* Note: we have no use for an iterpool here since the data
             within the loop is copied into ACCESS_POOL.  */

          for (hi = apr_hash_first(scratch_pool, lock_tokens); hi;
               hi = apr_hash_next(hi))
            {
              const void *relpath = apr_hash_this_key(hi);
              const char *token = apr_hash_this_val(hi);
              const char *fspath;

              /* The path needs to live as long as ACCESS_CTX.  */
              fspath = svn_fspath__join(fspath_base, relpath, access_pool);

              /* The token must live as long as ACCESS_CTX.  */
              token = apr_pstrdup(access_pool, token);

              SVN_ERR(svn_fs_access_add_lock_token2(access_ctx, fspath,
                                                    token));
            }
        }
    }

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------*/

/*** The RA vtable routines ***/

#define RA_LOCAL_DESCRIPTION \
        N_("Module for accessing a repository on local disk.")

static const char *
svn_ra_local__get_description(apr_pool_t *pool)
{
  return _(RA_LOCAL_DESCRIPTION);
}

static const char * const *
svn_ra_local__get_schemes(apr_pool_t *pool)
{
  static const char *schemes[] = { "file", NULL };

  return schemes;
}

/* Do nothing.
 *
 * Why is this acceptable?  FS warnings used to be used for only
 * two things: failures to close BDB repositories and failures to
 * interact with memcached in FSFS (new in 1.6).  In 1.5 and earlier,
 * we did not call svn_fs_set_warning_func in ra_local, which means
 * that any BDB-closing failure would have led to abort()s; the fact
 * that this hasn't led to huge hues and cries makes it seem likely
 * that this just doesn't happen that often, at least not through
 * ra_local.  And as far as memcached goes, it seems unlikely that
 * somebody is going to go through the trouble of setting up and
 * running memcached servers but then use ra_local access.  So we
 * ignore errors here, so that memcached can use the FS warnings API
 * without crashing ra_local.
 */
static void
ignore_warnings(void *baton,
                svn_error_t *err)
{
#ifdef SVN_DEBUG
  SVN_DBG(("Ignoring FS warning %s\n",
           svn_error_symbolic_name(err ? err->apr_err : 0)));
#endif
  return;
}

#define USER_AGENT "SVN/" SVN_VER_NUMBER " (" SVN_BUILD_TARGET ")" \
                   " ra_local"

static svn_error_t *
svn_ra_local__open(svn_ra_session_t *session,
                   const char **corrected_url,
                   const char *repos_URL,
                   const svn_ra_callbacks2_t *callbacks,
                   void *callback_baton,
                   svn_auth_baton_t *auth_baton,
                   apr_hash_t *config,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *client_string;
  svn_ra_local__session_baton_t *sess;
  const char *fs_path;
  static volatile svn_atomic_t cache_init_state = 0;
  apr_pool_t *pool = result_pool;

  /* Initialise the FSFS memory cache size.  We can only do this once
     so one CONFIG will win the race and all others will be ignored
     silently.  */
  SVN_ERR(svn_atomic__init_once(&cache_init_state, cache_init, config, pool));

  /* We don't support redirections in ra-local. */
  if (corrected_url)
    *corrected_url = NULL;

  /* Allocate and stash the session_sess args we have already. */
  sess = apr_pcalloc(pool, sizeof(*sess));
  sess->callbacks = callbacks;
  sess->callback_baton = callback_baton;
  sess->auth_baton = auth_baton;

  /* Look through the URL, figure out which part points to the
     repository, and which part is the path *within* the
     repository. */
  SVN_ERR(svn_ra_local__split_URL(&(sess->repos),
                                  &(sess->repos_url),
                                  &fs_path,
                                  repos_URL,
                                  session->pool));
  sess->fs_path = svn_stringbuf_create(fs_path, session->pool);

  /* Cache the filesystem object from the repos here for
     convenience. */
  sess->fs = svn_repos_fs(sess->repos);

  /* Ignore FS warnings. */
  svn_fs_set_warning_func(sess->fs, ignore_warnings, NULL);

  /* Cache the repository UUID as well */
  SVN_ERR(svn_fs_get_uuid(sess->fs, &sess->uuid, session->pool));

  /* Be sure username is NULL so we know to look it up / ask for it */
  sess->username = NULL;

  if (sess->callbacks->get_client_string != NULL)
    SVN_ERR(sess->callbacks->get_client_string(sess->callback_baton,
                                               &client_string, pool));
  else
    client_string = NULL;

  if (client_string)
    sess->useragent = apr_pstrcat(pool, USER_AGENT " ",
                                  client_string, SVN_VA_NULL);
  else
    sess->useragent = USER_AGENT;

  session->priv = sess;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__dup_session(svn_ra_session_t *new_session,
                          svn_ra_session_t *session,
                          const char *new_session_url,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_ra_local__session_baton_t *old_sess = session->priv;
  svn_ra_local__session_baton_t *new_sess;
  const char *fs_path;

  /* Allocate and stash the session_sess args we have already. */
  new_sess = apr_pcalloc(result_pool, sizeof(*new_sess));
  new_sess->callbacks = old_sess->callbacks;
  new_sess->callback_baton = old_sess->callback_baton;

  /* ### Re-use existing FS handle? */

  /* Reuse existing code */
  SVN_ERR(svn_ra_local__split_URL(&(new_sess->repos),
                                  &(new_sess->repos_url),
                                  &fs_path,
                                  new_session_url,
                                  result_pool));

  new_sess->fs_path = svn_stringbuf_create(fs_path, result_pool);

  /* Cache the filesystem object from the repos here for
     convenience. */
  new_sess->fs = svn_repos_fs(new_sess->repos);

  /* Ignore FS warnings. */
  svn_fs_set_warning_func(new_sess->fs, ignore_warnings, NULL);

  /* Cache the repository UUID as well */
  new_sess->uuid = apr_pstrdup(result_pool, old_sess->uuid);

  new_sess->username = old_sess->username
                            ? apr_pstrdup(result_pool, old_sess->username)
                            : NULL;

  new_sess->useragent = apr_pstrdup(result_pool, old_sess->useragent);
  new_session->priv = new_sess;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__reparent(svn_ra_session_t *session,
                       const char *url,
                       apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *relpath = svn_uri_skip_ancestor(sess->repos_url, url, pool);

  /* If the new URL isn't the same as our repository root URL, then
     let's ensure that it's some child of it. */
  if (! relpath)
    return svn_error_createf
      (SVN_ERR_RA_ILLEGAL_URL, NULL,
       _("URL '%s' is not a child of the session's repository root "
         "URL '%s'"), url, sess->repos_url);

  /* Update our FS_PATH sess member to point to our new
     relative-URL-turned-absolute-filesystem-path. */
  svn_stringbuf_set(sess->fs_path,
                    svn_fspath__canonicalize(relpath, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__get_session_url(svn_ra_session_t *session,
                              const char **url,
                              apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  *url = svn_path_url_add_component2(sess->repos_url,
                                     sess->fs_path->data + 1,
                                     pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__get_latest_revnum(svn_ra_session_t *session,
                                svn_revnum_t *latest_revnum,
                                apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  return svn_fs_youngest_rev(latest_revnum, sess->fs, pool);
}

static svn_error_t *
svn_ra_local__get_file_revs(svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_boolean_t include_merged_revisions,
                            svn_file_rev_handler_t handler,
                            void *handler_baton,
                            apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);
  return svn_repos_get_file_revs2(sess->repos, abs_path, start, end,
                                  include_merged_revisions, NULL, NULL,
                                  handler, handler_baton, pool);
}

static svn_error_t *
svn_ra_local__get_dated_revision(svn_ra_session_t *session,
                                 svn_revnum_t *revision,
                                 apr_time_t tm,
                                 apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  return svn_repos_dated_revision(revision, sess->repos, tm, pool);
}


static svn_error_t *
svn_ra_local__change_rev_prop(svn_ra_session_t *session,
                              svn_revnum_t rev,
                              const char *name,
                              const svn_string_t *const *old_value_p,
                              const svn_string_t *value,
                              apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;

  SVN_ERR(get_username(session, pool));
  return svn_repos_fs_change_rev_prop4(sess->repos, rev, sess->username,
                                       name, old_value_p, value, TRUE, TRUE,
                                       NULL, NULL, pool);
}

static svn_error_t *
svn_ra_local__get_uuid(svn_ra_session_t *session,
                       const char **uuid,
                       apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  *uuid = sess->uuid;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__get_repos_root(svn_ra_session_t *session,
                             const char **url,
                             apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  *url = sess->repos_url;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__rev_proplist(svn_ra_session_t *session,
                           svn_revnum_t rev,
                           apr_hash_t **props,
                           apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  return svn_repos_fs_revision_proplist(props, sess->repos, rev,
                                        NULL, NULL, pool);
}

static svn_error_t *
svn_ra_local__rev_prop(svn_ra_session_t *session,
                       svn_revnum_t rev,
                       const char *name,
                       svn_string_t **value,
                       apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  return svn_repos_fs_revision_prop(value, sess->repos, rev, name,
                                    NULL, NULL, pool);
}

struct ccw_baton
{
  svn_commit_callback2_t original_callback;
  void *original_baton;

  svn_ra_session_t *session;
};

/* Wrapper which populates the repos_root field of the commit_info struct */
static svn_error_t *
commit_callback_wrapper(const svn_commit_info_t *commit_info,
                        void *baton,
                        apr_pool_t *scratch_pool)
{
  struct ccw_baton *ccwb = baton;
  svn_commit_info_t *ci = svn_commit_info_dup(commit_info, scratch_pool);

  SVN_ERR(svn_ra_local__get_repos_root(ccwb->session, &ci->repos_root,
                                       scratch_pool));

  return svn_error_trace(ccwb->original_callback(ci, ccwb->original_baton,
                                                 scratch_pool));
}


/* The repository layer does not correctly fill in REPOS_ROOT in
   commit_info, as it doesn't know the url that is used to access
   it. This hooks the callback to fill in the missing pieces. */
static void
remap_commit_callback(svn_commit_callback2_t *callback,
                      void **callback_baton,
                      svn_ra_session_t *session,
                      svn_commit_callback2_t original_callback,
                      void *original_baton,
                      apr_pool_t *result_pool)
{
  if (original_callback == NULL)
    {
      *callback = NULL;
      *callback_baton = NULL;
    }
  else
    {
      /* Allocate this in RESULT_POOL, since the callback will be called
         long after this function has returned. */
      struct ccw_baton *ccwb = apr_palloc(result_pool, sizeof(*ccwb));

      ccwb->session = session;
      ccwb->original_callback = original_callback;
      ccwb->original_baton = original_baton;

      *callback = commit_callback_wrapper;
      *callback_baton = ccwb;
    }
}

static svn_error_t *
svn_ra_local__get_commit_editor(svn_ra_session_t *session,
                                const svn_delta_editor_t **editor,
                                void **edit_baton,
                                apr_hash_t *revprop_table,
                                svn_commit_callback2_t callback,
                                void *callback_baton,
                                apr_hash_t *lock_tokens,
                                svn_boolean_t keep_locks,
                                apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  struct deltify_etc_baton *deb = apr_palloc(pool, sizeof(*deb));

  /* Set repos_root_url in commit info */
  remap_commit_callback(&callback, &callback_baton, session,
                        callback, callback_baton, pool);

  /* Prepare the baton for deltify_etc()  */
  deb->fs = sess->fs;
  deb->repos = sess->repos;
  deb->fspath_base = sess->fs_path->data;
  if (! keep_locks)
    deb->lock_tokens = lock_tokens;
  else
    deb->lock_tokens = NULL;
  deb->commit_cb = callback;
  deb->commit_baton = callback_baton;

  SVN_ERR(get_username(session, pool));

  /* If there are lock tokens to add, do so. */
  SVN_ERR(apply_lock_tokens(sess->fs, sess->fs_path->data, lock_tokens,
                            session->pool, pool));

  /* Copy the revprops table so we can add the username. */
  revprop_table = apr_hash_copy(pool, revprop_table);
  svn_hash_sets(revprop_table, SVN_PROP_REVISION_AUTHOR,
                svn_string_create(sess->username, pool));
  svn_hash_sets(revprop_table, SVN_PROP_TXN_CLIENT_COMPAT_VERSION,
                svn_string_create(SVN_VER_NUMBER, pool));
  svn_hash_sets(revprop_table, SVN_PROP_TXN_USER_AGENT,
                svn_string_create(sess->useragent, pool));

  /* Get the repos commit-editor */
  return svn_repos_get_commit_editor5
         (editor, edit_baton, sess->repos, NULL,
          svn_path_uri_decode(sess->repos_url, pool), sess->fs_path->data,
          revprop_table, deltify_etc, deb, NULL, NULL, pool);
}


/* Implements svn_repos_mergeinfo_receiver_t.
 * It add MERGEINFO for PATH to the svn_mergeinfo_catalog_t BATON.
 */
static svn_error_t *
mergeinfo_receiver(const char *path,
                   svn_mergeinfo_t mergeinfo,
                   void *baton,
                   apr_pool_t *scratch_pool)
{
  svn_mergeinfo_catalog_t catalog = baton;
  apr_pool_t *result_pool = apr_hash_pool_get(catalog);
  apr_size_t len = strlen(path);

  apr_hash_set(catalog,
               apr_pstrmemdup(result_pool, path, len),
               len,
               svn_mergeinfo_dup(mergeinfo, result_pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__get_mergeinfo(svn_ra_session_t *session,
                            svn_mergeinfo_catalog_t *catalog,
                            const apr_array_header_t *paths,
                            svn_revnum_t revision,
                            svn_mergeinfo_inheritance_t inherit,
                            svn_boolean_t include_descendants,
                            apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  svn_mergeinfo_catalog_t tmp_catalog = svn_hash__make(pool);
  int i;
  apr_array_header_t *abs_paths =
    apr_array_make(pool, 0, sizeof(const char *));

  for (i = 0; i < paths->nelts; i++)
    {
      const char *relative_path = APR_ARRAY_IDX(paths, i, const char *);
      APR_ARRAY_PUSH(abs_paths, const char *) =
        svn_fspath__join(sess->fs_path->data, relative_path, pool);
    }

  SVN_ERR(svn_repos_fs_get_mergeinfo2(sess->repos, abs_paths, revision,
                                      inherit, include_descendants,
                                      NULL, NULL,
                                      mergeinfo_receiver, tmp_catalog,
                                      pool));
  if (apr_hash_count(tmp_catalog) > 0)
    SVN_ERR(svn_mergeinfo__remove_prefix_from_catalog(catalog,
                                                      tmp_catalog,
                                                      sess->fs_path->data,
                                                      pool));
  else
    *catalog = NULL;

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__do_update(svn_ra_session_t *session,
                        const svn_ra_reporter3_t **reporter,
                        void **report_baton,
                        svn_revnum_t update_revision,
                        const char *update_target,
                        svn_depth_t depth,
                        svn_boolean_t send_copyfrom_args,
                        svn_boolean_t ignore_ancestry,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       NULL,
                       TRUE,
                       depth,
                       send_copyfrom_args,
                       ignore_ancestry,
                       update_editor,
                       update_baton,
                       result_pool, scratch_pool);
}


static svn_error_t *
svn_ra_local__do_switch(svn_ra_session_t *session,
                        const svn_ra_reporter3_t **reporter,
                        void **report_baton,
                        svn_revnum_t update_revision,
                        const char *update_target,
                        svn_depth_t depth,
                        const char *switch_url,
                        svn_boolean_t send_copyfrom_args,
                        svn_boolean_t ignore_ancestry,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       switch_url,
                       TRUE /* text_deltas */,
                       depth,
                       send_copyfrom_args,
                       ignore_ancestry,
                       update_editor,
                       update_baton,
                       result_pool, scratch_pool);
}


static svn_error_t *
svn_ra_local__do_status(svn_ra_session_t *session,
                        const svn_ra_reporter3_t **reporter,
                        void **report_baton,
                        const char *status_target,
                        svn_revnum_t revision,
                        svn_depth_t depth,
                        const svn_delta_editor_t *status_editor,
                        void *status_baton,
                        apr_pool_t *pool)
{
  return make_reporter(session,
                       reporter,
                       report_baton,
                       revision,
                       status_target,
                       NULL,
                       FALSE,
                       depth,
                       FALSE,
                       FALSE,
                       status_editor,
                       status_baton,
                       pool, pool);
}


static svn_error_t *
svn_ra_local__do_diff(svn_ra_session_t *session,
                      const svn_ra_reporter3_t **reporter,
                      void **report_baton,
                      svn_revnum_t update_revision,
                      const char *update_target,
                      svn_depth_t depth,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t text_deltas,
                      const char *switch_url,
                      const svn_delta_editor_t *update_editor,
                      void *update_baton,
                      apr_pool_t *pool)
{
  return make_reporter(session,
                       reporter,
                       report_baton,
                       update_revision,
                       update_target,
                       switch_url,
                       text_deltas,
                       depth,
                       FALSE,
                       ignore_ancestry,
                       update_editor,
                       update_baton,
                       pool, pool);
}


struct log_baton
{
  svn_ra_local__session_baton_t *sess;
  svn_log_entry_receiver_t real_cb;
  void *real_baton;
};

static svn_error_t *
log_receiver_wrapper(void *baton,
                     svn_log_entry_t *log_entry,
                     apr_pool_t *pool)
{
  struct log_baton *b = baton;
  svn_ra_local__session_baton_t *sess = b->sess;

  if (sess->callbacks->cancel_func)
    SVN_ERR((sess->callbacks->cancel_func)(sess->callback_baton));

  /* For consistency with the other RA layers, replace an empty
     changed-paths hash with a NULL one.

     ### Should this be done by svn_ra_get_log2() instead, then? */
  if ((log_entry->changed_paths2)
      && (apr_hash_count(log_entry->changed_paths2) == 0))
    {
      log_entry->changed_paths = NULL;
      log_entry->changed_paths2 = NULL;
    }

  return b->real_cb(b->real_baton, log_entry, pool);
}


static svn_error_t *
svn_ra_local__get_log(svn_ra_session_t *session,
                      const apr_array_header_t *paths,
                      svn_revnum_t start,
                      svn_revnum_t end,
                      int limit,
                      svn_boolean_t discover_changed_paths,
                      svn_boolean_t strict_node_history,
                      svn_boolean_t include_merged_revisions,
                      const apr_array_header_t *revprops,
                      svn_log_entry_receiver_t receiver,
                      void *receiver_baton,
                      apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  struct log_baton lb;
  apr_array_header_t *abs_paths =
    apr_array_make(pool, 0, sizeof(const char *));

  if (paths)
    {
      int i;

      for (i = 0; i < paths->nelts; i++)
        {
          const char *relative_path = APR_ARRAY_IDX(paths, i, const char *);
          APR_ARRAY_PUSH(abs_paths, const char *) =
            svn_fspath__join(sess->fs_path->data, relative_path, pool);
        }
    }

  lb.real_cb = receiver;
  lb.real_baton = receiver_baton;
  lb.sess = sess;
  receiver = log_receiver_wrapper;
  receiver_baton = &lb;

  return svn_repos__get_logs_compat(sess->repos,
                                    abs_paths,
                                    start,
                                    end,
                                    limit,
                                    discover_changed_paths,
                                    strict_node_history,
                                    include_merged_revisions,
                                    revprops,
                                    NULL, NULL,
                                    receiver,
                                    receiver_baton,
                                    pool);
}


static svn_error_t *
svn_ra_local__do_check_path(svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t revision,
                            svn_node_kind_t *kind,
                            apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  svn_fs_root_t *root;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);

  if (! SVN_IS_VALID_REVNUM(revision))
    SVN_ERR(svn_fs_youngest_rev(&revision, sess->fs, pool));
  SVN_ERR(svn_fs_revision_root(&root, sess->fs, revision, pool));
  return svn_fs_check_path(kind, root, abs_path, pool);
}


static svn_error_t *
svn_ra_local__stat(svn_ra_session_t *session,
                   const char *path,
                   svn_revnum_t revision,
                   svn_dirent_t **dirent,
                   apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  svn_fs_root_t *root;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);

  if (! SVN_IS_VALID_REVNUM(revision))
    SVN_ERR(svn_fs_youngest_rev(&revision, sess->fs, pool));
  SVN_ERR(svn_fs_revision_root(&root, sess->fs, revision, pool));

  return svn_repos_stat(dirent, root, abs_path, pool);
}




/* Obtain the properties for a node, including its 'entry props */
static svn_error_t *
get_node_props(apr_hash_t **props,
               svn_fs_root_t *root,
               const char *path,
               const char *uuid,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  svn_revnum_t cmt_rev;
  const char *cmt_date, *cmt_author;

  /* Create a hash with props attached to the fs node. */
  SVN_ERR(svn_fs_node_proplist(props, root, path, result_pool));

  /* Now add some non-tweakable metadata to the hash as well... */

  /* The so-called 'entryprops' with info about CR & friends. */
  SVN_ERR(svn_repos_get_committed_info(&cmt_rev, &cmt_date,
                                       &cmt_author, root, path,
                                       scratch_pool));

  svn_hash_sets(*props, SVN_PROP_ENTRY_COMMITTED_REV,
                svn_string_createf(result_pool, "%ld", cmt_rev));
  svn_hash_sets(*props, SVN_PROP_ENTRY_COMMITTED_DATE, cmt_date ?
                svn_string_create(cmt_date, result_pool) : NULL);
  svn_hash_sets(*props, SVN_PROP_ENTRY_LAST_AUTHOR, cmt_author ?
                svn_string_create(cmt_author, result_pool) : NULL);
  svn_hash_sets(*props, SVN_PROP_ENTRY_UUID,
                svn_string_create(uuid, result_pool));

  /* We have no 'wcprops' in ra_local, but might someday. */

  return SVN_NO_ERROR;
}


/* Getting just one file. */
static svn_error_t *
svn_ra_local__get_file(svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t revision,
                       svn_stream_t *stream,
                       svn_revnum_t *fetched_rev,
                       apr_hash_t **props,
                       apr_pool_t *pool)
{
  svn_fs_root_t *root;
  svn_stream_t *contents;
  svn_revnum_t youngest_rev;
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);
  svn_node_kind_t node_kind;

  /* Open the revision's root. */
  if (! SVN_IS_VALID_REVNUM(revision))
    {
      SVN_ERR(svn_fs_youngest_rev(&youngest_rev, sess->fs, pool));
      SVN_ERR(svn_fs_revision_root(&root, sess->fs, youngest_rev, pool));
      if (fetched_rev != NULL)
        *fetched_rev = youngest_rev;
    }
  else
    SVN_ERR(svn_fs_revision_root(&root, sess->fs, revision, pool));

  SVN_ERR(svn_fs_check_path(&node_kind, root, abs_path, pool));
  if (node_kind == svn_node_none)
    {
      return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                               _("'%s' path not found"), abs_path);
    }
  else if (node_kind != svn_node_file)
    {
      return svn_error_createf(SVN_ERR_FS_NOT_FILE, NULL,
                               _("'%s' is not a file"), abs_path);
    }

  if (stream)
    {
      /* Get a stream representing the file's contents. */
      SVN_ERR(svn_fs_file_contents(&contents, root, abs_path, pool));

      /* Now push data from the fs stream back at the caller's stream.
         Note that this particular RA layer does not computing a
         checksum as we go, and confirming it against the repository's
         checksum when done.  That's because it calls
         svn_fs_file_contents() directly, which already checks the
         stored checksum, and all we're doing here is writing bytes in
         a loop.  Truly, Nothing Can Go Wrong :-).  But RA layers that
         go over a network should confirm the checksum.

         Note: we are not supposed to close the passed-in stream, so
         disown the thing.
      */
      SVN_ERR(svn_stream_copy3(contents, svn_stream_disown(stream, pool),
                               sess->callbacks
                                 ? sess->callbacks->cancel_func : NULL,
                               sess->callback_baton,
                               pool));
    }

  /* Handle props if requested. */
  if (props)
    SVN_ERR(get_node_props(props, root, abs_path, sess->uuid, pool, pool));

  return SVN_NO_ERROR;
}



/* Getting a directory's entries */
static svn_error_t *
svn_ra_local__get_dir(svn_ra_session_t *session,
                      apr_hash_t **dirents,
                      svn_revnum_t *fetched_rev,
                      apr_hash_t **props,
                      const char *path,
                      svn_revnum_t revision,
                      apr_uint32_t dirent_fields,
                      apr_pool_t *pool)
{
  svn_fs_root_t *root;
  svn_revnum_t youngest_rev;
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);

  /* Open the revision's root. */
  if (! SVN_IS_VALID_REVNUM(revision))
    {
      SVN_ERR(svn_fs_youngest_rev(&youngest_rev, sess->fs, pool));
      SVN_ERR(svn_fs_revision_root(&root, sess->fs, youngest_rev, pool));
      if (fetched_rev != NULL)
        *fetched_rev = youngest_rev;
    }
  else
    SVN_ERR(svn_fs_revision_root(&root, sess->fs, revision, pool));

  if (dirents)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);
      /* Get the dir's entries. */
      SVN_ERR(svn_fs_dir_entries(&entries, root, abs_path, pool));

      /* Loop over the fs dirents, and build a hash of general
         svn_dirent_t's. */
      *dirents = apr_hash_make(pool);
      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *datestring, *entryname, *fullpath;
          svn_fs_dirent_t *fs_entry;
          svn_dirent_t *entry = svn_dirent_create(pool);

          svn_pool_clear(iterpool);

          apr_hash_this(hi, &key, NULL, &val);
          entryname = (const char *) key;
          fs_entry = (svn_fs_dirent_t *) val;

          fullpath = svn_dirent_join(abs_path, entryname, iterpool);

          if (dirent_fields & SVN_DIRENT_KIND)
            {
              /* node kind */
              entry->kind = fs_entry->kind;
            }

          if (dirent_fields & SVN_DIRENT_SIZE)
            {
              /* size  */
              if (fs_entry->kind == svn_node_dir)
                entry->size = SVN_INVALID_FILESIZE;
              else
                SVN_ERR(svn_fs_file_length(&(entry->size), root,
                                           fullpath, iterpool));
            }

          if (dirent_fields & SVN_DIRENT_HAS_PROPS)
            {
              /* has_props? */
              SVN_ERR(svn_fs_node_has_props(&entry->has_props,
                                            root, fullpath,
                                            iterpool));
            }

          if ((dirent_fields & SVN_DIRENT_TIME)
              || (dirent_fields & SVN_DIRENT_LAST_AUTHOR)
              || (dirent_fields & SVN_DIRENT_CREATED_REV))
            {
              /* created_rev & friends */
              SVN_ERR(svn_repos_get_committed_info(&(entry->created_rev),
                                                   &datestring,
                                                   &(entry->last_author),
                                                   root, fullpath, iterpool));
              if (datestring)
                SVN_ERR(svn_time_from_cstring(&(entry->time), datestring,
                                              pool));
              if (entry->last_author)
                entry->last_author = apr_pstrdup(pool, entry->last_author);
            }

          /* Store. */
          svn_hash_sets(*dirents, entryname, entry);
        }
      svn_pool_destroy(iterpool);
    }

  /* Handle props if requested. */
  if (props)
    SVN_ERR(get_node_props(props, root, abs_path, sess->uuid, pool, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__get_locations(svn_ra_session_t *session,
                            apr_hash_t **locations,
                            const char *path,
                            svn_revnum_t peg_revision,
                            const apr_array_header_t *location_revisions,
                            apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);
  return svn_repos_trace_node_locations(sess->fs, locations, abs_path,
                                        peg_revision, location_revisions,
                                        NULL, NULL, pool);
}


static svn_error_t *
svn_ra_local__get_location_segments(svn_ra_session_t *session,
                                    const char *path,
                                    svn_revnum_t peg_revision,
                                    svn_revnum_t start_rev,
                                    svn_revnum_t end_rev,
                                    svn_location_segment_receiver_t receiver,
                                    void *receiver_baton,
                                    apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);
  return svn_repos_node_location_segments(sess->repos, abs_path,
                                          peg_revision, start_rev, end_rev,
                                          receiver, receiver_baton,
                                          NULL, NULL, pool);
}

struct lock_baton_t {
  svn_ra_lock_callback_t lock_func;
  void *lock_baton;
  const char *fs_path;
  svn_boolean_t is_lock;
  svn_error_t *cb_err;
};

/* Implements svn_fs_lock_callback_t.  Used by svn_ra_local__lock and
   svn_ra_local__unlock to forward to supplied callback and record any
   callback error. */
static svn_error_t *
lock_cb(void *lock_baton,
        const char *path,
        const svn_lock_t *lock,
        svn_error_t *fs_err,
        apr_pool_t *pool)
{
  struct lock_baton_t *b = lock_baton;

  if (b && !b->cb_err && b->lock_func)
    {
      path = svn_fspath__skip_ancestor(b->fs_path, path);
      b->cb_err = b->lock_func(b->lock_baton, path, b->is_lock, lock, fs_err,
                               pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__lock(svn_ra_session_t *session,
                   apr_hash_t *path_revs,
                   const char *comment,
                   svn_boolean_t force,
                   svn_ra_lock_callback_t lock_func,
                   void *lock_baton,
                   apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  apr_hash_t *targets = apr_hash_make(pool);
  apr_hash_index_t *hi;
  svn_error_t *err;
  struct lock_baton_t baton = {0};

  /* A username is absolutely required to lock a path. */
  SVN_ERR(get_username(session, pool));

  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      const char *abs_path = svn_fspath__join(sess->fs_path->data,
                                              apr_hash_this_key(hi), pool);
      svn_revnum_t current_rev = *(svn_revnum_t *)apr_hash_this_val(hi);
      svn_fs_lock_target_t *target = svn_fs_lock_target_create(NULL,
                                                               current_rev,
                                                               pool);

      svn_hash_sets(targets, abs_path, target);
    }

  baton.lock_func = lock_func;
  baton.lock_baton = lock_baton;
  baton.fs_path = sess->fs_path->data;
  baton.is_lock = TRUE;
  baton.cb_err = SVN_NO_ERROR;

  err = svn_repos_fs_lock_many(sess->repos, targets, comment,
                               FALSE /* not DAV comment */,
                               0 /* no expiration */, force,
                               lock_cb, &baton,
                               pool, pool);

  if (err && baton.cb_err)
    svn_error_compose(err, baton.cb_err);
  else if (!err)
    err = baton.cb_err;

  return svn_error_trace(err);
}


static svn_error_t *
svn_ra_local__unlock(svn_ra_session_t *session,
                     apr_hash_t *path_tokens,
                     svn_boolean_t force,
                     svn_ra_lock_callback_t lock_func,
                     void *lock_baton,
                     apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  apr_hash_t *targets = apr_hash_make(pool);
  apr_hash_index_t *hi;
  svn_error_t *err;
  struct lock_baton_t baton = {0};

  /* A username is absolutely required to unlock a path. */
  SVN_ERR(get_username(session, pool));

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      const char *abs_path = svn_fspath__join(sess->fs_path->data,
                                              apr_hash_this_key(hi), pool);
      const char *token = apr_hash_this_val(hi);

      svn_hash_sets(targets, abs_path, token);
    }

  baton.lock_func = lock_func;
  baton.lock_baton = lock_baton;
  baton.fs_path = sess->fs_path->data;
  baton.is_lock = FALSE;
  baton.cb_err = SVN_NO_ERROR;

  err = svn_repos_fs_unlock_many(sess->repos, targets, force, lock_cb, &baton,
                                 pool, pool);

  if (err && baton.cb_err)
    svn_error_compose(err, baton.cb_err);
  else if (!err)
    err = baton.cb_err;

  return svn_error_trace(err);
}



static svn_error_t *
svn_ra_local__get_lock(svn_ra_session_t *session,
                       svn_lock_t **lock,
                       const char *path,
                       apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);
  return svn_fs_get_lock(lock, sess->fs, abs_path, pool);
}



static svn_error_t *
svn_ra_local__get_locks(svn_ra_session_t *session,
                        apr_hash_t **locks,
                        const char *path,
                        svn_depth_t depth,
                        apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);

  /* Kinda silly to call the repos wrapper, since we have no authz
     func to give it.  But heck, why not. */
  return svn_repos_fs_get_locks2(locks, sess->repos, abs_path, depth,
                                 NULL, NULL, pool);
}


static svn_error_t *
svn_ra_local__replay(svn_ra_session_t *session,
                     svn_revnum_t revision,
                     svn_revnum_t low_water_mark,
                     svn_boolean_t send_deltas,
                     const svn_delta_editor_t *editor,
                     void *edit_baton,
                     apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  svn_fs_root_t *root;

  SVN_ERR(svn_fs_revision_root(&root, svn_repos_fs(sess->repos),
                               revision, pool));
  return svn_repos_replay2(root, sess->fs_path->data, low_water_mark,
                           send_deltas, editor, edit_baton, NULL, NULL,
                           pool);
}


static svn_error_t *
svn_ra_local__replay_range(svn_ra_session_t *session,
                           svn_revnum_t start_revision,
                           svn_revnum_t end_revision,
                           svn_revnum_t low_water_mark,
                           svn_boolean_t send_deltas,
                           svn_ra_replay_revstart_callback_t revstart_func,
                           svn_ra_replay_revfinish_callback_t revfinish_func,
                           void *replay_baton,
                           apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}


static svn_error_t *
svn_ra_local__has_capability(svn_ra_session_t *session,
                             svn_boolean_t *has,
                             const char *capability,
                             apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;

  if (strcmp(capability, SVN_RA_CAPABILITY_DEPTH) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_LOG_REVPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_PARTIAL_REPLAY) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_COMMIT_REVPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_ATOMIC_REVPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_INHERITED_PROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_EPHEMERAL_TXNPROPS) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_GET_FILE_REVS_REVERSE) == 0
      || strcmp(capability, SVN_RA_CAPABILITY_LIST) == 0
      )
    {
      *has = TRUE;
    }
  else if (strcmp(capability, SVN_RA_CAPABILITY_MERGEINFO) == 0)
    {
      /* With mergeinfo, the code's capabilities may not reflect the
         repository's, so inquire further. */
      SVN_ERR(svn_repos_has_capability(sess->repos, has,
                                       SVN_REPOS_CAPABILITY_MERGEINFO,
                                       pool));
    }
  else  /* Don't know any other capabilities, so error. */
    {
      return svn_error_createf
        (SVN_ERR_UNKNOWN_CAPABILITY, NULL,
         _("Don't know anything about capability '%s'"), capability);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__get_deleted_rev(svn_ra_session_t *session,
                              const char *path,
                              svn_revnum_t peg_revision,
                              svn_revnum_t end_revision,
                              svn_revnum_t *revision_deleted,
                              apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path, pool);

  SVN_ERR(svn_repos_deleted_rev(sess->fs,
                                abs_path,
                                peg_revision,
                                end_revision,
                                revision_deleted,
                                pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_local__get_inherited_props(svn_ra_session_t *session,
                                  apr_array_header_t **iprops,
                                  const char *path,
                                  svn_revnum_t revision,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_fs_root_t *root;
  svn_ra_local__session_baton_t *sess = session->priv;
  const char *abs_path = svn_fspath__join(sess->fs_path->data, path,
                                          scratch_pool);
  svn_node_kind_t node_kind;

  /* Open the revision's root. */
  SVN_ERR(svn_fs_revision_root(&root, sess->fs, revision, scratch_pool));

  SVN_ERR(svn_fs_check_path(&node_kind, root, abs_path, scratch_pool));
  if (node_kind == svn_node_none)
    {
      return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                               _("'%s' path not found"), abs_path);
    }

  return svn_error_trace(
                svn_repos_fs_get_inherited_props(iprops, root, abs_path,
                                                 NULL /* propname */,
                                                 NULL, NULL /* auth */,
                                                 result_pool, scratch_pool));
}

static svn_error_t *
svn_ra_local__register_editor_shim_callbacks(svn_ra_session_t *session,
                                    svn_delta_shim_callbacks_t *callbacks)
{
  /* This is currenly a no-op, since we don't provide our own editor, just
     use the one the libsvn_repos hands back to us. */
  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_local__get_commit_ev2(svn_editor_t **editor,
                             svn_ra_session_t *session,
                             apr_hash_t *revprops,
                             svn_commit_callback2_t commit_cb,
                             void *commit_baton,
                             apr_hash_t *lock_tokens,
                             svn_boolean_t keep_locks,
                             svn_ra__provide_base_cb_t provide_base_cb,
                             svn_ra__provide_props_cb_t provide_props_cb,
                             svn_ra__get_copysrc_kind_cb_t get_copysrc_kind_cb,
                             void *cb_baton,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  struct deltify_etc_baton *deb = apr_palloc(result_pool, sizeof(*deb));

  remap_commit_callback(&commit_cb, &commit_baton, session,
                        commit_cb, commit_baton, result_pool);

  /* NOTE: the RA callbacks are ignored. We pass everything directly to
     the REPOS editor.  */

  /* Prepare the baton for deltify_etc()  */
  deb->fs = sess->fs;
  deb->repos = sess->repos;
  deb->fspath_base = sess->fs_path->data;
  if (! keep_locks)
    deb->lock_tokens = lock_tokens;
  else
    deb->lock_tokens = NULL;
  deb->commit_cb = commit_cb;
  deb->commit_baton = commit_baton;

  /* Ensure there is a username (and an FS access context) associated with
     the session and its FS handle.  */
  SVN_ERR(get_username(session, scratch_pool));

  /* If there are lock tokens to add, do so.  */
  SVN_ERR(apply_lock_tokens(sess->fs, sess->fs_path->data, lock_tokens,
                            session->pool, scratch_pool));

  /* Copy the REVPROPS and insert the author/username.  */
  revprops = apr_hash_copy(scratch_pool, revprops);
  svn_hash_sets(revprops, SVN_PROP_REVISION_AUTHOR,
                svn_string_create(sess->username, scratch_pool));

  return svn_error_trace(svn_repos__get_commit_ev2(
                           editor, sess->repos, NULL /* authz */,
                           NULL /* authz_repos_name */, NULL /* authz_user */,
                           revprops,
                           deltify_etc, deb, cancel_func, cancel_baton,
                           result_pool, scratch_pool));
}

/* Trivially forward repos-layer callbacks to RA-layer callbacks.
 * Their signatures are the same. */
typedef struct dirent_receiver_baton_t
{
  svn_ra_dirent_receiver_t receiver;
  void *receiver_baton;
} dirent_receiver_baton_t;

static svn_error_t *
dirent_receiver(const char *rel_path,
                svn_dirent_t *dirent,
                void *baton,
                apr_pool_t *pool)
{
  dirent_receiver_baton_t *b = baton;
  return b->receiver(rel_path, dirent, b->receiver_baton, pool);
}

static svn_error_t *
svn_ra_local__list(svn_ra_session_t *session,
                   const char *path,
                   svn_revnum_t revision,
                   const apr_array_header_t *patterns,
                   svn_depth_t depth,
                   apr_uint32_t dirent_fields,
                   svn_ra_dirent_receiver_t receiver,
                   void *receiver_baton,
                   apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *sess = session->priv;
  svn_fs_root_t *root;
  svn_boolean_t path_info_only = (dirent_fields & ~SVN_DIRENT_KIND) == 0;

  dirent_receiver_baton_t baton;
  baton.receiver = receiver;
  baton.receiver_baton = receiver_baton;

  SVN_ERR(svn_fs_revision_root(&root, sess->fs, revision, pool));
  path = svn_dirent_join(sess->fs_path->data, path, pool);
  return svn_error_trace(svn_repos_list(root, path, patterns, depth,
                                        path_info_only, NULL, NULL,
                                        dirent_receiver, &baton,
                                        sess->callbacks
                                          ? sess->callbacks->cancel_func
                                          : NULL,
                                        sess->callback_baton, pool));
}

/*----------------------------------------------------------------*/

static const svn_version_t *
ra_local_version(void)
{
  SVN_VERSION_BODY;
}

/** The ra_vtable **/

static const svn_ra__vtable_t ra_local_vtable =
{
  ra_local_version,
  svn_ra_local__get_description,
  svn_ra_local__get_schemes,
  svn_ra_local__open,
  svn_ra_local__dup_session,
  svn_ra_local__reparent,
  svn_ra_local__get_session_url,
  svn_ra_local__get_latest_revnum,
  svn_ra_local__get_dated_revision,
  svn_ra_local__change_rev_prop,
  svn_ra_local__rev_proplist,
  svn_ra_local__rev_prop,
  svn_ra_local__get_commit_editor,
  svn_ra_local__get_file,
  svn_ra_local__get_dir,
  svn_ra_local__get_mergeinfo,
  svn_ra_local__do_update,
  svn_ra_local__do_switch,
  svn_ra_local__do_status,
  svn_ra_local__do_diff,
  svn_ra_local__get_log,
  svn_ra_local__do_check_path,
  svn_ra_local__stat,
  svn_ra_local__get_uuid,
  svn_ra_local__get_repos_root,
  svn_ra_local__get_locations,
  svn_ra_local__get_location_segments,
  svn_ra_local__get_file_revs,
  svn_ra_local__lock,
  svn_ra_local__unlock,
  svn_ra_local__get_lock,
  svn_ra_local__get_locks,
  svn_ra_local__replay,
  svn_ra_local__has_capability,
  svn_ra_local__replay_range,
  svn_ra_local__get_deleted_rev,
  svn_ra_local__get_inherited_props,
  NULL /* set_svn_ra_open */,
  svn_ra_local__list ,
  svn_ra_local__register_editor_shim_callbacks,
  svn_ra_local__get_commit_ev2,
  NULL /* replay_range_ev2 */
};


/*----------------------------------------------------------------*/

/** The One Public Routine, called by libsvn_ra **/

svn_error_t *
svn_ra_local__init(const svn_version_t *loader_version,
                   const svn_ra__vtable_t **vtable,
                   apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { "svn_repos", svn_repos_version },
      { "svn_fs",    svn_fs_version },
      { NULL, NULL }
    };


  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Unsupported RA loader version (%d) for "
                               "ra_local"),
                             loader_version->major);

  SVN_ERR(svn_ver_check_list2(ra_local_version(), checklist, svn_ver_equal));

#ifndef SVN_LIBSVN_RA_LINKS_RA_LOCAL
  /* This means the library was loaded as a DSO, so use the DSO pool. */
  SVN_ERR(svn_fs_initialize(svn_dso__pool()));
#endif

  *vtable = &ra_local_vtable;

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for the 1.1 and before API. */
#define NAME "ra_local"
#define DESCRIPTION RA_LOCAL_DESCRIPTION
#define VTBL ra_local_vtable
#define INITFUNC svn_ra_local__init
#define COMPAT_INITFUNC svn_ra_local_init
#include "../libsvn_ra/wrapper_template.h"
