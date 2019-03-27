/* fs-wrap.c --- filesystem interface wrappers.
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_repos.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_subst.h"
#include "repos.h"
#include "svn_private_config.h"
#include "private/svn_repos_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_utf_private.h"
#include "private/svn_fspath.h"


/*** Commit wrappers ***/

svn_error_t *
svn_repos_fs_commit_txn(const char **conflict_p,
                        svn_repos_t *repos,
                        svn_revnum_t *new_rev,
                        svn_fs_txn_t *txn,
                        apr_pool_t *pool)
{
  svn_error_t *err, *err2;
  const char *txn_name;
  apr_hash_t *props;
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;
  apr_hash_t *hooks_env;

  *new_rev = SVN_INVALID_REVNUM;
  if (conflict_p)
    *conflict_p = NULL;

  /* Parse the hooks-env file (if any). */
  SVN_ERR(svn_repos__parse_hooks_env(&hooks_env, repos->hooks_env_path,
                                     pool, pool));

  /* Run pre-commit hooks. */
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));
  SVN_ERR(svn_repos__hooks_pre_commit(repos, hooks_env, txn_name, pool));

  /* Remove any ephemeral transaction properties.  If the commit fails
     we will attempt to restore the properties but if that fails, or
     the process is killed, the properties will be lost. */
  SVN_ERR(svn_fs_txn_proplist(&props, txn, pool));
  iterpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
    {
      const char *key = apr_hash_this_key(hi);

      svn_pool_clear(iterpool);

      if (strncmp(key, SVN_PROP_TXN_PREFIX,
                  (sizeof(SVN_PROP_TXN_PREFIX) - 1)) == 0)
        {
          SVN_ERR(svn_fs_change_txn_prop(txn, key, NULL, iterpool));
        }
    }
  svn_pool_destroy(iterpool);

  /* Commit. */
  err = svn_fs_commit_txn(conflict_p, new_rev, txn, pool);
  if (! SVN_IS_VALID_REVNUM(*new_rev))
    {
      /* The commit failed, try to restore the ephemeral properties. */
      iterpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
        {
          const char *key = apr_hash_this_key(hi);
          svn_string_t *val = apr_hash_this_val(hi);

          svn_pool_clear(iterpool);

          if (strncmp(key, SVN_PROP_TXN_PREFIX,
                      (sizeof(SVN_PROP_TXN_PREFIX) - 1)) == 0)
            svn_error_clear(svn_fs_change_txn_prop(txn, key, val, iterpool));
        }
      svn_pool_destroy(iterpool);

      return err;
    }

  /* Run post-commit hooks. */
  if ((err2 = svn_repos__hooks_post_commit(repos, hooks_env,
                                           *new_rev, txn_name, pool)))
    {
      err2 = svn_error_create
               (SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED, err2,
                _("Commit succeeded, but post-commit hook failed"));
    }

  return svn_error_compose_create(err, err2);
}



/*** Transaction creation wrappers. ***/


svn_error_t *
svn_repos_fs_begin_txn_for_commit2(svn_fs_txn_t **txn_p,
                                   svn_repos_t *repos,
                                   svn_revnum_t rev,
                                   apr_hash_t *revprop_table,
                                   apr_pool_t *pool)
{
  apr_array_header_t *revprops;
  const char *txn_name;
  svn_string_t *author = svn_hash_gets(revprop_table, SVN_PROP_REVISION_AUTHOR);
  apr_hash_t *hooks_env;
  svn_error_t *err;
  svn_fs_txn_t *txn;

  /* Parse the hooks-env file (if any). */
  SVN_ERR(svn_repos__parse_hooks_env(&hooks_env, repos->hooks_env_path,
                                     pool, pool));

  /* Begin the transaction, ask for the fs to do on-the-fly lock checks.
     We fetch its name, too, so the start-commit hook can use it.  */
  SVN_ERR(svn_fs_begin_txn2(&txn, repos->fs, rev,
                            SVN_FS_TXN_CHECK_LOCKS, pool));
  err = svn_fs_txn_name(&txn_name, txn, pool);
  if (err)
    return svn_error_compose_create(err, svn_fs_abort_txn(txn, pool));

  /* We pass the revision properties to the filesystem by adding them
     as properties on the txn.  Later, when we commit the txn, these
     properties will be copied into the newly created revision. */
  revprops = svn_prop_hash_to_array(revprop_table, pool);
  err = svn_repos_fs_change_txn_props(txn, revprops, pool);
  if (err)
    return svn_error_compose_create(err, svn_fs_abort_txn(txn, pool));

  /* Run start-commit hooks. */
  err = svn_repos__hooks_start_commit(repos, hooks_env,
                                      author ? author->data : NULL,
                                      repos->client_capabilities, txn_name,
                                      pool);
  if (err)
    return svn_error_compose_create(err, svn_fs_abort_txn(txn, pool));

  /* We have API promise that *TXN_P is unaffected on failure. */
  *txn_p = txn;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_begin_txn_for_commit(svn_fs_txn_t **txn_p,
                                  svn_repos_t *repos,
                                  svn_revnum_t rev,
                                  const char *author,
                                  const char *log_msg,
                                  apr_pool_t *pool)
{
  apr_hash_t *revprop_table = apr_hash_make(pool);
  if (author)
    svn_hash_sets(revprop_table, SVN_PROP_REVISION_AUTHOR,
                  svn_string_create(author, pool));
  if (log_msg)
    svn_hash_sets(revprop_table, SVN_PROP_REVISION_LOG,
                  svn_string_create(log_msg, pool));
  return svn_repos_fs_begin_txn_for_commit2(txn_p, repos, rev, revprop_table,
                                            pool);
}


/*** Property wrappers ***/

svn_error_t *
svn_repos__validate_prop(const char *name,
                         const svn_string_t *value,
                         apr_pool_t *pool)
{
  svn_prop_kind_t kind = svn_property_kind2(name);

  /* Allow deleting any property, even a property we don't allow to set. */
  if (value == NULL)
    return SVN_NO_ERROR;

  /* Disallow setting non-regular properties. */
  if (kind != svn_prop_regular_kind)
    return svn_error_createf
      (SVN_ERR_REPOS_BAD_ARGS, NULL,
       _("Storage of non-regular property '%s' is disallowed through the "
         "repository interface, and could indicate a bug in your client"),
       name);

  /* Validate "svn:" properties. */
  if (svn_prop_is_svn_prop(name) && value != NULL)
    {
      /* Validate that translated props (e.g., svn:log) are UTF-8 with
       * LF line endings. */
      if (svn_prop_needs_translation(name))
        {
          if (!svn_utf__is_valid(value->data, value->len))
            {
              return svn_error_createf
                (SVN_ERR_BAD_PROPERTY_VALUE, NULL,
                 _("Cannot accept '%s' property because it is not encoded in "
                   "UTF-8"), name);
            }

          /* Disallow inconsistent line ending style, by simply looking for
           * carriage return characters ('\r'). */
          if (strchr(value->data, '\r') != NULL)
            {
              svn_error_t *err = svn_error_createf
                (SVN_ERR_BAD_PROPERTY_VALUE_EOL, NULL,
                 _("Cannot accept non-LF line endings in '%s' property"),
                   name);

              return svn_error_create(SVN_ERR_BAD_PROPERTY_VALUE, err,
                                      _("Invalid property value"));
            }
        }

      /* "svn:date" should be a valid date. */
      if (strcmp(name, SVN_PROP_REVISION_DATE) == 0)
        {
          apr_time_t temp;
          svn_error_t *err;

          err = svn_time_from_cstring(&temp, value->data, pool);
          if (err)
            return svn_error_create(SVN_ERR_BAD_PROPERTY_VALUE,
                                    err, NULL);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos__normalize_prop(const svn_string_t **result_p,
                          svn_boolean_t *normalized_p,
                          const char *name,
                          const svn_string_t *value,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  if (svn_prop_needs_translation(name) && value)
    {
      svn_string_t *new_value;

      SVN_ERR(svn_subst_translate_string2(&new_value, NULL, normalized_p,
                                          value, "UTF-8", TRUE,
                                          result_pool, scratch_pool));
      *result_p = new_value;
    }
  else
    {
      *result_p = svn_string_dup(value, result_pool);
      if (normalized_p)
        *normalized_p = FALSE;
    }

  return SVN_NO_ERROR;
}


/* Verify the mergeinfo property value VALUE and return an error if it
 * is invalid. The PATH on which that property is set is used for error
 * messages only.  Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
verify_mergeinfo(const svn_string_t *value,
                 const char *path,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_mergeinfo_t mergeinfo;

  /* It's okay to delete svn:mergeinfo. */
  if (value == NULL)
    return SVN_NO_ERROR;

  /* Mergeinfo is UTF-8 encoded so the number of bytes returned by strlen()
   * should match VALUE->LEN. Prevents trailing garbage in the property. */
  if (strlen(value->data) != value->len)
    return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                             _("Commit rejected because mergeinfo on '%s' "
                               "contains unexpected string terminator"),
                             path);

  err = svn_mergeinfo_parse(&mergeinfo, value->data, scratch_pool);
  if (err)
    return svn_error_createf(err->apr_err, err,
                             _("Commit rejected because mergeinfo on '%s' "
                               "is syntactically invalid"),
                             path);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_change_node_prop(svn_fs_root_t *root,
                              const char *path,
                              const char *name,
                              const svn_string_t *value,
                              apr_pool_t *pool)
{
  if (value && strcmp(name, SVN_PROP_MERGEINFO) == 0)
    SVN_ERR(verify_mergeinfo(value, path, pool));

  /* Validate the property, then call the wrapped function. */
  SVN_ERR(svn_repos__validate_prop(name, value, pool));
  return svn_fs_change_node_prop(root, path, name, value, pool);
}


svn_error_t *
svn_repos_fs_change_txn_props(svn_fs_txn_t *txn,
                              const apr_array_header_t *txnprops,
                              apr_pool_t *pool)
{
  int i;

  for (i = 0; i < txnprops->nelts; i++)
    {
      svn_prop_t *prop = &APR_ARRAY_IDX(txnprops, i, svn_prop_t);
      SVN_ERR(svn_repos__validate_prop(prop->name, prop->value, pool));
    }

  return svn_fs_change_txn_props(txn, txnprops, pool);
}


svn_error_t *
svn_repos_fs_change_txn_prop(svn_fs_txn_t *txn,
                             const char *name,
                             const svn_string_t *value,
                             apr_pool_t *pool)
{
  apr_array_header_t *props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  svn_prop_t prop;

  prop.name = name;
  prop.value = value;
  APR_ARRAY_PUSH(props, svn_prop_t) = prop;

  return svn_repos_fs_change_txn_props(txn, props, pool);
}


svn_error_t *
svn_repos_fs_change_rev_prop4(svn_repos_t *repos,
                              svn_revnum_t rev,
                              const char *author,
                              const char *name,
                              const svn_string_t *const *old_value_p,
                              const svn_string_t *new_value,
                              svn_boolean_t use_pre_revprop_change_hook,
                              svn_boolean_t use_post_revprop_change_hook,
                              svn_repos_authz_func_t authz_read_func,
                              void *authz_read_baton,
                              apr_pool_t *pool)
{
  svn_repos_revision_access_level_t readability;

  SVN_ERR(svn_repos_check_revision_access(&readability, repos, rev,
                                          authz_read_func, authz_read_baton,
                                          pool));

  if (readability == svn_repos_revision_access_full)
    {
      const svn_string_t *old_value;
      char action;
      apr_hash_t *hooks_env;

      SVN_ERR(svn_repos__validate_prop(name, new_value, pool));

      /* Fetch OLD_VALUE for svn_fs_change_rev_prop2(). */
      if (old_value_p)
        {
          old_value = *old_value_p;
        }
      else
        {
          /* Get OLD_VALUE anyway, in order for ACTION and OLD_VALUE arguments
           * to the hooks to be accurate. */
          svn_string_t *old_value2;

          SVN_ERR(svn_fs_revision_prop2(&old_value2, repos->fs, rev, name,
                                        TRUE, pool, pool));
          old_value = old_value2;
        }

      /* Prepare ACTION. */
      if (! new_value)
        action = 'D';
      else if (! old_value)
        action = 'A';
      else
        action = 'M';

      /* Parse the hooks-env file (if any, and if to be used). */
      if (use_pre_revprop_change_hook || use_post_revprop_change_hook)
        SVN_ERR(svn_repos__parse_hooks_env(&hooks_env, repos->hooks_env_path,
                                           pool, pool));

      /* ### currently not passing the old_value to hooks */
      if (use_pre_revprop_change_hook)
        SVN_ERR(svn_repos__hooks_pre_revprop_change(repos, hooks_env, rev,
                                                    author, name, new_value,
                                                    action, pool));

      SVN_ERR(svn_fs_change_rev_prop2(repos->fs, rev, name,
                                      &old_value, new_value, pool));

      if (use_post_revprop_change_hook)
        SVN_ERR(svn_repos__hooks_post_revprop_change(repos, hooks_env, rev,
                                                     author, name, old_value,
                                                     action, pool));
    }
  else  /* rev is either unreadable or only partially readable */
    {
      return svn_error_createf
        (SVN_ERR_AUTHZ_UNREADABLE, NULL,
         _("Write denied:  not authorized to read all of revision %ld"), rev);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_revision_prop(svn_string_t **value_p,
                           svn_repos_t *repos,
                           svn_revnum_t rev,
                           const char *propname,
                           svn_repos_authz_func_t authz_read_func,
                           void *authz_read_baton,
                           apr_pool_t *pool)
{
  svn_repos_revision_access_level_t readability;

  SVN_ERR(svn_repos_check_revision_access(&readability, repos, rev,
                                          authz_read_func, authz_read_baton,
                                          pool));

  if (readability == svn_repos_revision_access_none)
    {
      /* Property?  What property? */
      *value_p = NULL;
    }
  else if (readability == svn_repos_revision_access_partial)
    {
      /* Only svn:author and svn:date are fetchable. */
      if ((strcmp(propname, SVN_PROP_REVISION_AUTHOR) != 0)
          && (strcmp(propname, SVN_PROP_REVISION_DATE) != 0))
        *value_p = NULL;

      else
        SVN_ERR(svn_fs_revision_prop2(value_p, repos->fs,
                                      rev, propname, TRUE, pool, pool));
    }
  else /* wholly readable revision */
    {
      SVN_ERR(svn_fs_revision_prop2(value_p, repos->fs, rev, propname, TRUE,
                                    pool, pool));
    }

  return SVN_NO_ERROR;
}



svn_error_t *
svn_repos_fs_revision_proplist(apr_hash_t **table_p,
                               svn_repos_t *repos,
                               svn_revnum_t rev,
                               svn_repos_authz_func_t authz_read_func,
                               void *authz_read_baton,
                               apr_pool_t *pool)
{
  svn_repos_revision_access_level_t readability;

  SVN_ERR(svn_repos_check_revision_access(&readability, repos, rev,
                                          authz_read_func, authz_read_baton,
                                          pool));

  if (readability == svn_repos_revision_access_none)
    {
      /* Return an empty hash. */
      *table_p = apr_hash_make(pool);
    }
  else if (readability == svn_repos_revision_access_partial)
    {
      apr_hash_t *tmphash;
      svn_string_t *value;

      /* Produce two property hashtables, both in POOL. */
      SVN_ERR(svn_fs_revision_proplist2(&tmphash, repos->fs, rev, TRUE,
                                        pool, pool));
      *table_p = apr_hash_make(pool);

      /* If they exist, we only copy svn:author and svn:date into the
         'real' hashtable being returned. */
      value = svn_hash_gets(tmphash, SVN_PROP_REVISION_AUTHOR);
      if (value)
        svn_hash_sets(*table_p, SVN_PROP_REVISION_AUTHOR, value);

      value = svn_hash_gets(tmphash, SVN_PROP_REVISION_DATE);
      if (value)
        svn_hash_sets(*table_p, SVN_PROP_REVISION_DATE, value);
    }
  else /* wholly readable revision */
    {
      SVN_ERR(svn_fs_revision_proplist2(table_p, repos->fs, rev, TRUE,
                                        pool, pool));
    }

  return SVN_NO_ERROR;
}

struct lock_many_baton_t {
  svn_boolean_t need_lock;
  apr_array_header_t *paths;
  svn_fs_lock_callback_t lock_callback;
  void *lock_baton;
  svn_error_t *cb_err;
  apr_pool_t *pool;
};

/* Implements svn_fs_lock_callback_t.  Used by svn_repos_fs_lock_many
   and svn_repos_fs_unlock_many to record the paths for use by post-
   hooks, forward to the supplied callback and record any callback
   error. */
static svn_error_t *
lock_many_cb(void *lock_baton,
             const char *path,
             const svn_lock_t *lock,
             svn_error_t *fs_err,
             apr_pool_t *pool)
{
  struct lock_many_baton_t *b = lock_baton;

  if (!b->cb_err && b->lock_callback)
    b->cb_err = b->lock_callback(b->lock_baton, path, lock, fs_err, pool);

  if ((b->need_lock && lock) || (!b->need_lock && !fs_err))
    APR_ARRAY_PUSH(b->paths, const char *) = apr_pstrdup(b->pool, path);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_fs_lock_many(svn_repos_t *repos,
                       apr_hash_t *targets,
                       const char *comment,
                       svn_boolean_t is_dav_comment,
                       apr_time_t expiration_date,
                       svn_boolean_t steal_lock,
                       svn_fs_lock_callback_t lock_callback,
                       void *lock_baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_error_t *err, *cb_err = SVN_NO_ERROR;
  svn_fs_access_t *access_ctx = NULL;
  const char *username = NULL;
  apr_hash_t *hooks_env;
  apr_hash_t *pre_targets = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  struct lock_many_baton_t baton;

  if (!apr_hash_count(targets))
    return SVN_NO_ERROR;

  /* Parse the hooks-env file (if any). */
  SVN_ERR(svn_repos__parse_hooks_env(&hooks_env, repos->hooks_env_path,
                                     scratch_pool, scratch_pool));

  SVN_ERR(svn_fs_get_access(&access_ctx, repos->fs));
  if (access_ctx)
    SVN_ERR(svn_fs_access_get_username(&username, access_ctx));

  if (! username)
    return svn_error_create
      (SVN_ERR_FS_NO_USER, NULL,
       "Cannot lock path, no authenticated username available.");

  /* Run pre-lock hook.  This could throw error, preventing
     svn_fs_lock2() from happening for that path. */
  for (hi = apr_hash_first(scratch_pool, targets); hi; hi = apr_hash_next(hi))
    {
      const char *new_token;
      svn_fs_lock_target_t *target;
      const char *path = apr_hash_this_key(hi);

      svn_pool_clear(iterpool);

      err = svn_repos__hooks_pre_lock(repos, hooks_env, &new_token, path,
                                      username, comment, steal_lock, iterpool);
      if (err)
        {
          if (!cb_err && lock_callback)
            cb_err = lock_callback(lock_baton, path, NULL, err, iterpool);
          svn_error_clear(err);

          continue;
        }

      target = apr_hash_this_val(hi);
      if (*new_token)
        svn_fs_lock_target_set_token(target, new_token);
      svn_hash_sets(pre_targets, path, target);
    }

  if (!apr_hash_count(pre_targets))
    return svn_error_trace(cb_err);

  baton.need_lock = TRUE;
  baton.paths = apr_array_make(scratch_pool, apr_hash_count(pre_targets),
                               sizeof(const char *));
  baton.lock_callback = lock_callback;
  baton.lock_baton = lock_baton;
  baton.cb_err = cb_err;
  baton.pool = scratch_pool;

  err = svn_fs_lock_many(repos->fs, pre_targets, comment,
                         is_dav_comment, expiration_date, steal_lock,
                         lock_many_cb, &baton, result_pool, iterpool);

  /* If there are locks run the post-lock even if there is an error. */
  if (baton.paths->nelts)
    {
      svn_error_t *perr = svn_repos__hooks_post_lock(repos, hooks_env,
                                                     baton.paths, username,
                                                     iterpool);
      if (perr)
        {
          perr = svn_error_create(SVN_ERR_REPOS_POST_LOCK_HOOK_FAILED, perr,
                            _("Locking succeeded, but post-lock hook failed"));
          err = svn_error_compose_create(err, perr);
        }
    }

  svn_pool_destroy(iterpool);

  if (err && cb_err)
    svn_error_compose(err, cb_err);
  else if (!err)
    err = cb_err;

  return svn_error_trace(err);
}

struct lock_baton_t {
  const svn_lock_t *lock;
  svn_error_t *fs_err;
};

/* Implements svn_fs_lock_callback_t.  Used by svn_repos_fs_lock and
   svn_repos_fs_unlock to record the lock and error from
   svn_repos_fs_lock_many and svn_repos_fs_unlock_many. */
static svn_error_t *
lock_cb(void *lock_baton,
        const char *path,
        const svn_lock_t *lock,
        svn_error_t *fs_err,
        apr_pool_t *pool)
{
  struct lock_baton_t *b = lock_baton;

  b->lock = lock;
  b->fs_err = svn_error_dup(fs_err);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_fs_lock(svn_lock_t **lock,
                  svn_repos_t *repos,
                  const char *path,
                  const char *token,
                  const char *comment,
                  svn_boolean_t is_dav_comment,
                  apr_time_t expiration_date,
                  svn_revnum_t current_rev,
                  svn_boolean_t steal_lock,
                  apr_pool_t *pool)
{
  apr_hash_t *targets = apr_hash_make(pool);
  svn_fs_lock_target_t *target = svn_fs_lock_target_create(token, current_rev,
                                                           pool);
  svn_error_t *err;
  struct lock_baton_t baton = {0};

  svn_hash_sets(targets, path, target);

  err = svn_repos_fs_lock_many(repos, targets, comment, is_dav_comment,
                               expiration_date, steal_lock, lock_cb, &baton,
                               pool, pool);

  if (baton.lock)
    *lock = (svn_lock_t*)baton.lock;

  if (err && baton.fs_err)
    svn_error_compose(err, baton.fs_err);
  else if (!err)
    err = baton.fs_err;

  return svn_error_trace(err);
}


svn_error_t *
svn_repos_fs_unlock_many(svn_repos_t *repos,
                         apr_hash_t *targets,
                         svn_boolean_t break_lock,
                         svn_fs_lock_callback_t lock_callback,
                         void *lock_baton,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_error_t *err, *cb_err = SVN_NO_ERROR;
  svn_fs_access_t *access_ctx = NULL;
  const char *username = NULL;
  apr_hash_t *hooks_env;
  apr_hash_t *pre_targets = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  struct lock_many_baton_t baton;

  if (!apr_hash_count(targets))
    return SVN_NO_ERROR;

  /* Parse the hooks-env file (if any). */
  SVN_ERR(svn_repos__parse_hooks_env(&hooks_env, repos->hooks_env_path,
                                     scratch_pool, scratch_pool));

  SVN_ERR(svn_fs_get_access(&access_ctx, repos->fs));
  if (access_ctx)
    SVN_ERR(svn_fs_access_get_username(&username, access_ctx));

  if (! break_lock && ! username)
    return svn_error_create
      (SVN_ERR_FS_NO_USER, NULL,
       _("Cannot unlock, no authenticated username available"));

  /* Run pre-unlock hook.  This could throw error, preventing
     svn_fs_unlock_many() from happening for that path. */
  for (hi = apr_hash_first(scratch_pool, targets); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      const char *token = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      err = svn_repos__hooks_pre_unlock(repos, hooks_env, path, username, token,
                                        break_lock, iterpool);
      if (err)
        {
          if (!cb_err && lock_callback)
            cb_err = lock_callback(lock_baton, path, NULL, err, iterpool);
          svn_error_clear(err);

          continue;
        }

      svn_hash_sets(pre_targets, path, token);
    }

  if (!apr_hash_count(pre_targets))
    return svn_error_trace(cb_err);

  baton.need_lock = FALSE;
  baton.paths = apr_array_make(scratch_pool, apr_hash_count(pre_targets),
                               sizeof(const char *));
  baton.lock_callback = lock_callback;
  baton.lock_baton = lock_baton;
  baton.cb_err = cb_err;
  baton.pool = scratch_pool;

  err = svn_fs_unlock_many(repos->fs, pre_targets, break_lock,
                           lock_many_cb, &baton, result_pool, iterpool);

  /* If there are 'unlocks' run the post-unlock even if there is an error. */
  if (baton.paths->nelts)
    {
      svn_error_t *perr = svn_repos__hooks_post_unlock(repos, hooks_env,
                                                       baton.paths,
                                                       username, iterpool);
      if (perr)
        {
          perr = svn_error_create(SVN_ERR_REPOS_POST_UNLOCK_HOOK_FAILED, perr,
                           _("Unlock succeeded, but post-unlock hook failed"));
          err = svn_error_compose_create(err, perr);
        }
    }

  svn_pool_destroy(iterpool);

  if (err && cb_err)
    svn_error_compose(err, cb_err);
  else if (!err)
    err = cb_err;

  return svn_error_trace(err);
}

svn_error_t *
svn_repos_fs_unlock(svn_repos_t *repos,
                    const char *path,
                    const char *token,
                    svn_boolean_t break_lock,
                    apr_pool_t *pool)
{
  apr_hash_t *targets = apr_hash_make(pool);
  svn_error_t *err;
  struct lock_baton_t baton = {0};

  if (!token)
    token = "";

  svn_hash_sets(targets, path, token);

  err = svn_repos_fs_unlock_many(repos, targets, break_lock, lock_cb, &baton,
                                 pool, pool);

  if (err && baton.fs_err)
    svn_error_compose(err, baton.fs_err);
  else if (!err)
    err = baton.fs_err;

  return svn_error_trace(err);
}


struct get_locks_baton_t
{
  svn_fs_t *fs;
  svn_fs_root_t *head_root;
  svn_repos_authz_func_t authz_read_func;
  void *authz_read_baton;
  apr_hash_t *locks;
};


/* This implements the svn_fs_get_locks_callback_t interface. */
static svn_error_t *
get_locks_callback(void *baton,
                   svn_lock_t *lock,
                   apr_pool_t *pool)
{
  struct get_locks_baton_t *b = baton;
  svn_boolean_t readable = TRUE;
  apr_pool_t *hash_pool = apr_hash_pool_get(b->locks);

  /* If there's auth to deal with, deal with it. */
  if (b->authz_read_func)
    SVN_ERR(b->authz_read_func(&readable, b->head_root, lock->path,
                               b->authz_read_baton, pool));

  /* If we can read this lock path, add the lock to the return hash. */
  if (readable)
    svn_hash_sets(b->locks, apr_pstrdup(hash_pool, lock->path),
                  svn_lock_dup(lock, hash_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_get_locks2(apr_hash_t **locks,
                        svn_repos_t *repos,
                        const char *path,
                        svn_depth_t depth,
                        svn_repos_authz_func_t authz_read_func,
                        void *authz_read_baton,
                        apr_pool_t *pool)
{
  apr_hash_t *all_locks = apr_hash_make(pool);
  svn_revnum_t head_rev;
  struct get_locks_baton_t baton;

  SVN_ERR_ASSERT((depth == svn_depth_empty) ||
                 (depth == svn_depth_files) ||
                 (depth == svn_depth_immediates) ||
                 (depth == svn_depth_infinity));

  SVN_ERR(svn_fs_youngest_rev(&head_rev, repos->fs, pool));

  /* Populate our callback baton. */
  baton.fs = repos->fs;
  baton.locks = all_locks;
  baton.authz_read_func = authz_read_func;
  baton.authz_read_baton = authz_read_baton;
  SVN_ERR(svn_fs_revision_root(&(baton.head_root), repos->fs,
                               head_rev, pool));

  /* Get all the locks. */
  SVN_ERR(svn_fs_get_locks2(repos->fs, path, depth,
                            get_locks_callback, &baton, pool));

  *locks = baton.locks;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_get_mergeinfo2(svn_repos_t *repos,
                            const apr_array_header_t *paths,
                            svn_revnum_t rev,
                            svn_mergeinfo_inheritance_t inherit,
                            svn_boolean_t include_descendants,
                            svn_repos_authz_func_t authz_read_func,
                            void *authz_read_baton,
                            svn_repos_mergeinfo_receiver_t receiver,
                            void *receiver_baton,
                            apr_pool_t *scratch_pool)
{
  /* Here we cast away 'const', but won't try to write through this pointer
   * without first allocating a new array. */
  apr_array_header_t *readable_paths = (apr_array_header_t *) paths;
  svn_fs_root_t *root;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  if (!SVN_IS_VALID_REVNUM(rev))
    SVN_ERR(svn_fs_youngest_rev(&rev, repos->fs, scratch_pool));
  SVN_ERR(svn_fs_revision_root(&root, repos->fs, rev, scratch_pool));

  /* Filter out unreadable paths before divining merge tracking info. */
  if (authz_read_func)
    {
      int i;

      for (i = 0; i < paths->nelts; i++)
        {
          svn_boolean_t readable;
          const char *path = APR_ARRAY_IDX(paths, i, char *);
          svn_pool_clear(iterpool);
          SVN_ERR(authz_read_func(&readable, root, path, authz_read_baton,
                                  iterpool));
          if (readable && readable_paths != paths)
            APR_ARRAY_PUSH(readable_paths, const char *) = path;
          else if (!readable && readable_paths == paths)
            {
              /* Requested paths differ from readable paths.  Fork
                 list of readable paths from requested paths. */
              int j;
              readable_paths = apr_array_make(scratch_pool, paths->nelts - 1,
                                              sizeof(char *));
              for (j = 0; j < i; j++)
                {
                  path = APR_ARRAY_IDX(paths, j, char *);
                  APR_ARRAY_PUSH(readable_paths, const char *) = path;
                }
            }
        }
    }

  /* We consciously do not perform authz checks on the paths returned
     in *MERGEINFO, avoiding massive authz overhead which would allow
     us to protect the name of where a change was merged from, but not
     the change itself. */
  /* ### TODO(reint): ... but how about descendant merged-to paths? */
  if (readable_paths->nelts > 0)
    SVN_ERR(svn_fs_get_mergeinfo3(root, readable_paths, inherit,
                                  include_descendants, TRUE,
                                  receiver, receiver_baton,
                                  scratch_pool));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

struct pack_notify_baton
{
  svn_repos_notify_func_t notify_func;
  void *notify_baton;
};

/* Implements svn_fs_pack_notify_t. */
static svn_error_t *
pack_notify_func(void *baton,
                 apr_int64_t shard,
                 svn_fs_pack_notify_action_t pack_action,
                 apr_pool_t *pool)
{
  struct pack_notify_baton *pnb = baton;
  svn_repos_notify_t *notify;
  svn_repos_notify_action_t repos_action;

  /* Simple conversion works for these values. */
  SVN_ERR_ASSERT(pack_action >= svn_fs_pack_notify_start
                 && pack_action <= svn_fs_pack_notify_noop);

  repos_action = pack_action == svn_fs_pack_notify_noop
               ? svn_repos_notify_pack_noop
               : pack_action + svn_repos_notify_pack_shard_start
                             - svn_fs_pack_notify_start;

  notify = svn_repos_notify_create(repos_action, pool);
  notify->shard = shard;
  pnb->notify_func(pnb->notify_baton, notify, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_fs_pack2(svn_repos_t *repos,
                   svn_repos_notify_func_t notify_func,
                   void *notify_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  struct pack_notify_baton pnb;

  pnb.notify_func = notify_func;
  pnb.notify_baton = notify_baton;

  return svn_fs_pack(repos->db_path,
                     notify_func ? pack_notify_func : NULL,
                     notify_func ? &pnb : NULL,
                     cancel_func, cancel_baton, pool);
}

svn_error_t *
svn_repos_fs_get_inherited_props(apr_array_header_t **inherited_props_p,
                                 svn_fs_root_t *root,
                                 const char *path,
                                 const char *propname,
                                 svn_repos_authz_func_t authz_read_func,
                                 void *authz_read_baton,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *inherited_props;
  const char *parent_path = path;

  inherited_props = apr_array_make(result_pool, 1,
                                   sizeof(svn_prop_inherited_item_t *));
  while (!(parent_path[0] == '/' && parent_path[1] == '\0'))
    {
      svn_boolean_t allowed = TRUE;
      apr_hash_t *parent_properties = NULL;

      svn_pool_clear(iterpool);
      parent_path = svn_fspath__dirname(parent_path, scratch_pool);

      if (authz_read_func)
        SVN_ERR(authz_read_func(&allowed, root, parent_path,
                                authz_read_baton, iterpool));
      if (allowed)
        {
          if (propname)
            {
              svn_string_t *propval;

              SVN_ERR(svn_fs_node_prop(&propval, root, parent_path, propname,
                                       result_pool));
              if (propval)
                {
                  parent_properties = apr_hash_make(result_pool);
                  svn_hash_sets(parent_properties, propname, propval);
                }
            }
          else
            {
              SVN_ERR(svn_fs_node_proplist(&parent_properties, root,
                                           parent_path, result_pool));
            }

          if (parent_properties && apr_hash_count(parent_properties))
            {
              svn_prop_inherited_item_t *i_props =
                apr_pcalloc(result_pool, sizeof(*i_props));
              i_props->path_or_url =
                apr_pstrdup(result_pool, parent_path + 1);
              i_props->prop_hash = parent_properties;
              /* Build the output array in depth-first order. */
              svn_sort__array_insert(inherited_props, &i_props, 0);
            }
        }
    }

  svn_pool_destroy(iterpool);

  *inherited_props_p = inherited_props;
  return SVN_NO_ERROR;
}

/*
 * vim:ts=4:sw=2:expandtab:tw=80:fo=tcroq
 * vim:isk=a-z,A-Z,48-57,_,.,-,>
 * vim:cino=>1s,e0,n0,f0,{.5s,}0,^-.5s,=.5s,t0,+1s,c3,(0,u0,\:0
 */
