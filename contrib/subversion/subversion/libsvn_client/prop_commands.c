/*
 * prop_commands.c:  Implementation of propset, propget, and proplist.
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

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_error.h"
#include "svn_client.h"
#include "client.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_hash.h"
#include "svn_sorts.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_client_private.h"


/*** Code. ***/

/* Return an SVN_ERR_CLIENT_PROPERTY_NAME error if NAME is a wcprop,
   else return SVN_NO_ERROR. */
static svn_error_t *
error_if_wcprop_name(const char *name)
{
  if (svn_property_kind2(name) == svn_prop_wc_kind)
    {
      return svn_error_createf
        (SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
         _("'%s' is a wcprop, thus not accessible to clients"),
         name);
    }

  return SVN_NO_ERROR;
}


struct getter_baton
{
  svn_ra_session_t *ra_session;
  svn_revnum_t base_revision_for_url;
};


static svn_error_t *
get_file_for_validation(const svn_string_t **mime_type,
                        svn_stream_t *stream,
                        void *baton,
                        apr_pool_t *pool)
{
  struct getter_baton *gb = baton;
  svn_ra_session_t *ra_session = gb->ra_session;
  apr_hash_t *props;

  SVN_ERR(svn_ra_get_file(ra_session, "", gb->base_revision_for_url,
                          stream, NULL,
                          (mime_type ? &props : NULL),
                          pool));

  if (mime_type)
    *mime_type = svn_hash_gets(props, SVN_PROP_MIME_TYPE);

  return SVN_NO_ERROR;
}


static
svn_error_t *
do_url_propset(const char *url,
               const char *propname,
               const svn_string_t *propval,
               const svn_node_kind_t kind,
               const svn_revnum_t base_revision_for_url,
               const svn_delta_editor_t *editor,
               void *edit_baton,
               apr_pool_t *pool)
{
  void *root_baton;

  SVN_ERR(editor->open_root(edit_baton, base_revision_for_url, pool,
                            &root_baton));

  if (kind == svn_node_file)
    {
      void *file_baton;
      const char *uri_basename = svn_uri_basename(url, pool);

      SVN_ERR(editor->open_file(uri_basename, root_baton,
                                base_revision_for_url, pool, &file_baton));
      SVN_ERR(editor->change_file_prop(file_baton, propname, propval, pool));
      SVN_ERR(editor->close_file(file_baton, NULL, pool));
    }
  else
    {
      SVN_ERR(editor->change_dir_prop(root_baton, propname, propval, pool));
    }

  return editor->close_directory(root_baton, pool);
}

static svn_error_t *
propset_on_url(const char *propname,
               const svn_string_t *propval,
               const char *target,
               svn_boolean_t skip_checks,
               svn_revnum_t base_revision_for_url,
               const apr_hash_t *revprop_table,
               svn_commit_callback2_t commit_callback,
               void *commit_baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  enum svn_prop_kind prop_kind = svn_property_kind2(propname);
  svn_ra_session_t *ra_session;
  svn_node_kind_t node_kind;
  const char *message;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  apr_hash_t *commit_revprops;
  svn_error_t *err;

  if (prop_kind != svn_prop_regular_kind)
    return svn_error_createf
      (SVN_ERR_BAD_PROP_KIND, NULL,
       _("Property '%s' is not a regular property"), propname);

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  SVN_ERR(svn_client_open_ra_session2(&ra_session, target, NULL,
                                      ctx, pool, pool));

  SVN_ERR(svn_ra_check_path(ra_session, "", base_revision_for_url,
                            &node_kind, pool));
  if (node_kind == svn_node_none)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       _("Path '%s' does not exist in revision %ld"),
       target, base_revision_for_url);

  if (node_kind == svn_node_file)
    {
      /* We need to reparent our session one directory up, since editor
         semantics require the root is a directory.

         ### How does this interact with authz? */
      const char *parent_url;
      parent_url = svn_uri_dirname(target, pool);

      SVN_ERR(svn_ra_reparent(ra_session, parent_url, pool));
    }

  /* Setting an inappropriate property is not allowed (unless
     overridden by 'skip_checks', in some circumstances).  Deleting an
     inappropriate property is allowed, however, since older clients
     allowed (and other clients possibly still allow) setting it in
     the first place. */
  if (propval && svn_prop_is_svn_prop(propname))
    {
      const svn_string_t *new_value;
      struct getter_baton gb;

      gb.ra_session = ra_session;
      gb.base_revision_for_url = base_revision_for_url;
      SVN_ERR(svn_wc_canonicalize_svn_prop(&new_value, propname, propval,
                                           target, node_kind, skip_checks,
                                           get_file_for_validation, &gb, pool));
      propval = new_value;
    }

  /* Create a new commit item and add it to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items = apr_array_make(pool, 1, sizeof(item));

      item = svn_client_commit_item3_create(pool);
      item->url = target;
      item->kind = node_kind;
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_PROP_MODS;
      APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, pool));
      if (! message)
        return SVN_NO_ERROR;
    }
  else
    message = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           message, ctx, pool));

  /* Fetch RA commit editor. */
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx,
                                                       NULL, pool)));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    commit_callback,
                                    commit_baton,
                                    NULL, TRUE, /* No lock tokens */
                                    pool));

  err = do_url_propset(target, propname, propval, node_kind,
                       base_revision_for_url, editor, edit_baton, pool);

  if (err)
    {
      /* At least try to abort the edit (and fs txn) before throwing err. */
      svn_error_clear(editor->abort_edit(edit_baton, pool));
      return svn_error_trace(err);
    }

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;
      notify = svn_wc_create_notify_url(target,
                                        svn_wc_notify_commit_finalizing,
                                        pool);
      ctx->notify_func2(ctx->notify_baton2, notify, pool);
    }
  /* Close the edit. */
  return editor->close_edit(edit_baton, pool);
}

/* Check that PROPNAME is a valid name for a versioned property.  Return an
 * error if it is not valid, specifically if it is:
 *   - the name of a standard Subversion rev-prop; or
 *   - in the namespace of WC-props; or
 *   - not a well-formed property name (except if PROPVAL is NULL: in other
 *     words we do allow deleting a prop with an ill-formed name).
 *
 * Since Subversion controls the "svn:" property namespace, we don't honor
 * a 'skip_checks' flag here.  Checks for unusual property combinations such
 * as svn:eol-style with a non-text svn:mime-type might understandably be
 * skipped, but things such as using a property name reserved for revprops
 * on a local target are never allowed.
 */
static svn_error_t *
check_prop_name(const char *propname,
                const svn_string_t *propval)
{
  if (svn_prop_is_known_svn_rev_prop(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Revision property '%s' not allowed "
                               "in this context"), propname);

  SVN_ERR(error_if_wcprop_name(propname));

  if (propval && ! svn_prop_name_is_valid(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Bad property name: '%s'"), propname);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_propset_local(const char *propname,
                         const svn_string_t *propval,
                         const apr_array_header_t *targets,
                         svn_depth_t depth,
                         svn_boolean_t skip_checks,
                         const apr_array_header_t *changelists,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t targets_are_urls;
  int i;

  if (targets->nelts == 0)
    return SVN_NO_ERROR;

  /* Check for homogeneity among our targets. */
  targets_are_urls = svn_path_is_url(APR_ARRAY_IDX(targets, 0, const char *));
  SVN_ERR(svn_client__assert_homogeneous_target_type(targets));

  if (targets_are_urls)
    return svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL,
                            _("Targets must be working copy paths"));

  SVN_ERR(check_prop_name(propname, propval));

  for (i = 0; i < targets->nelts; i++)
    {
      svn_node_kind_t kind;
      const char *target_abspath;
      const char *target = APR_ARRAY_IDX(targets, i, const char *);

      svn_pool_clear(iterpool);

      /* Check for cancellation */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      SVN_ERR(svn_dirent_get_absolute(&target_abspath, target, iterpool));

      /* Call prop_set for deleted nodes to have special errors */
      SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, target_abspath,
                                FALSE, FALSE, iterpool));

      if (kind == svn_node_unknown || kind == svn_node_none)
        {
          if (ctx->notify_func2)
            {
              svn_wc_notify_t *notify = svn_wc_create_notify(
                                          target_abspath,
                                          svn_wc_notify_path_nonexistent,
                                          iterpool);

              ctx->notify_func2(ctx->notify_baton2, notify, iterpool);
            }
        }

      SVN_WC__CALL_WITH_WRITE_LOCK(
        svn_wc_prop_set4(ctx->wc_ctx, target_abspath, propname,
                         propval, depth, skip_checks, changelists,
                         ctx->cancel_func, ctx->cancel_baton,
                         ctx->notify_func2, ctx->notify_baton2, iterpool),
        ctx->wc_ctx, target_abspath, FALSE /* lock_anchor */, iterpool);
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_propset_remote(const char *propname,
                          const svn_string_t *propval,
                          const char *url,
                          svn_boolean_t skip_checks,
                          svn_revnum_t base_revision_for_url,
                          const apr_hash_t *revprop_table,
                          svn_commit_callback2_t commit_callback,
                          void *commit_baton,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *scratch_pool)
{
  if (!svn_path_is_url(url))
    return svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL,
                            _("Targets must be URLs"));

  SVN_ERR(check_prop_name(propname, propval));

  /* The rationale for requiring the base_revision_for_url
     argument is that without it, it's too easy to possibly
     overwrite someone else's change without noticing.  (See also
     tools/examples/svnput.c). */
  if (! SVN_IS_VALID_REVNUM(base_revision_for_url))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Setting property on non-local targets "
                              "needs a base revision"));

  /* ### When you set svn:eol-style or svn:keywords on a wc file,
     ### Subversion sends a textdelta at commit time to properly
     ### normalize the file in the repository.  If we want to
     ### support editing these properties on URLs, then we should
     ### generate the same textdelta; for now, we won't support
     ### editing these properties on URLs.  (Admittedly, this
     ### means that all the machinery with get_file_for_validation
     ### is unused.)
   */
  if ((strcmp(propname, SVN_PROP_EOL_STYLE) == 0) ||
      (strcmp(propname, SVN_PROP_KEYWORDS) == 0))
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Setting property '%s' on non-local "
                               "targets is not supported"), propname);

  SVN_ERR(propset_on_url(propname, propval, url, skip_checks,
                         base_revision_for_url, revprop_table,
                         commit_callback, commit_baton, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
check_and_set_revprop(svn_revnum_t *set_rev,
                      svn_ra_session_t *ra_session,
                      const char *propname,
                      const svn_string_t *original_propval,
                      const svn_string_t *propval,
                      apr_pool_t *pool)
{
  if (original_propval)
    {
      /* Ensure old value hasn't changed behind our back. */
      svn_string_t *current;
      SVN_ERR(svn_ra_rev_prop(ra_session, *set_rev, propname, &current, pool));

      if (original_propval->data && (! current))
        {
          return svn_error_createf(
                  SVN_ERR_RA_OUT_OF_DATE, NULL,
                  _("revprop '%s' in r%ld is unexpectedly absent "
                    "in repository (maybe someone else deleted it?)"),
                  propname, *set_rev);
        }
      else if (original_propval->data
               && (! svn_string_compare(original_propval, current)))
        {
          return svn_error_createf(
                  SVN_ERR_RA_OUT_OF_DATE, NULL,
                  _("revprop '%s' in r%ld has unexpected value "
                    "in repository (maybe someone else changed it?)"),
                  propname, *set_rev);
        }
      else if ((! original_propval->data) && current)
        {
          return svn_error_createf(
                  SVN_ERR_RA_OUT_OF_DATE, NULL,
                  _("revprop '%s' in r%ld is unexpectedly present "
                    "in repository (maybe someone else set it?)"),
                  propname, *set_rev);
        }
    }

  SVN_ERR(svn_ra_change_rev_prop2(ra_session, *set_rev, propname,
                                  NULL, propval, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_revprop_set2(const char *propname,
                        const svn_string_t *propval,
                        const svn_string_t *original_propval,
                        const char *URL,
                        const svn_opt_revision_t *revision,
                        svn_revnum_t *set_rev,
                        svn_boolean_t force,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  svn_boolean_t be_atomic;

  if ((strcmp(propname, SVN_PROP_REVISION_AUTHOR) == 0)
      && propval
      && strchr(propval->data, '\n') != NULL
      && (! force))
    return svn_error_create(SVN_ERR_CLIENT_REVISION_AUTHOR_CONTAINS_NEWLINE,
                            NULL, _("Author name should not contain a newline;"
                                    " value will not be set unless forced"));

  if (propval && ! svn_prop_name_is_valid(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Bad property name: '%s'"), propname);

  /* Open an RA session for the URL. */
  SVN_ERR(svn_client_open_ra_session2(&ra_session, URL, NULL,
                                      ctx, pool, pool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR(svn_client__get_revision_number(set_rev, NULL, ctx->wc_ctx, NULL,
                                          ra_session, revision, pool));

  SVN_ERR(svn_ra_has_capability(ra_session, &be_atomic,
                                SVN_RA_CAPABILITY_ATOMIC_REVPROPS, pool));
  if (be_atomic)
    {
      /* Convert ORIGINAL_PROPVAL to an OLD_VALUE_P. */
      const svn_string_t *const *old_value_p;
      const svn_string_t *unset = NULL;

      if (original_propval == NULL)
        old_value_p = NULL;
      else if (original_propval->data == NULL)
        old_value_p = &unset;
      else
        old_value_p = &original_propval;

      /* The actual RA call. */
      SVN_ERR(svn_ra_change_rev_prop2(ra_session, *set_rev, propname,
                                      old_value_p, propval, pool));
    }
  else
    {
      /* The actual RA call. */
      SVN_ERR(check_and_set_revprop(set_rev, ra_session, propname,
                                    original_propval, propval, pool));
    }

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify_url(URL,
                                             propval == NULL
                                               ? svn_wc_notify_revprop_deleted
                                               : svn_wc_notify_revprop_set,
                                             pool);
      notify->prop_name = propname;
      notify->revision = *set_rev;

      ctx->notify_func2(ctx->notify_baton2, notify, pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__remote_propget(apr_hash_t *props,
                           apr_array_header_t **inherited_props,
                           const char *propname,
                           const char *target_prefix,
                           const char *target_relative,
                           svn_node_kind_t kind,
                           svn_revnum_t revnum,
                           svn_ra_session_t *ra_session,
                           svn_depth_t depth,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  apr_hash_t *dirents;
  apr_hash_t *prop_hash = NULL;
  const svn_string_t *val;
  const char *target_full_url =
    svn_path_url_add_component2(target_prefix, target_relative,
                                scratch_pool);

  if (kind == svn_node_dir)
    {
      SVN_ERR(svn_ra_get_dir2(ra_session,
                              (depth >= svn_depth_files ? &dirents : NULL),
                              NULL, &prop_hash, target_relative, revnum,
                              SVN_DIRENT_KIND, scratch_pool));
    }
  else if (kind == svn_node_file)
    {
      SVN_ERR(svn_ra_get_file(ra_session, target_relative, revnum,
                              NULL, NULL, &prop_hash, scratch_pool));
    }
  else if (kind == svn_node_none)
    {
      return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                               _("'%s' does not exist in revision %ld"),
                               target_full_url, revnum);
    }
  else
    {
      return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                               _("Unknown node kind for '%s'"),
                               target_full_url);
    }

  if (inherited_props)
    {
      const char *repos_root_url;
      int i;
      apr_array_header_t *final_iprops =
        apr_array_make(result_pool, 1, sizeof(svn_prop_inherited_item_t *));

      /* We will filter out all but PROPNAME later, making a final copy
         in RESULT_POOL, so pass SCRATCH_POOL for all pools. */
      SVN_ERR(svn_ra_get_inherited_props(ra_session, inherited_props,
                                         target_relative, revnum,
                                         scratch_pool, scratch_pool));
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url,
                                     scratch_pool));
      SVN_ERR(svn_client__iprop_relpaths_to_urls(*inherited_props,
                                                 repos_root_url,
                                                 scratch_pool,
                                                 scratch_pool));

      /* Make a copy of any inherited PROPNAME properties in RESULT_POOL. */
      for (i = 0; i < (*inherited_props)->nelts; i++)
        {
          svn_prop_inherited_item_t *iprop =
            APR_ARRAY_IDX((*inherited_props), i, svn_prop_inherited_item_t *);
          svn_string_t *iprop_val = svn_hash_gets(iprop->prop_hash, propname);

          if (iprop_val)
            {
              svn_prop_inherited_item_t *new_iprop =
                apr_palloc(result_pool, sizeof(*new_iprop));
              new_iprop->path_or_url =
                apr_pstrdup(result_pool, iprop->path_or_url);
              new_iprop->prop_hash = apr_hash_make(result_pool);
              svn_hash_sets(new_iprop->prop_hash,
                            apr_pstrdup(result_pool, propname),
                            svn_string_dup(iprop_val, result_pool));
              APR_ARRAY_PUSH(final_iprops, svn_prop_inherited_item_t *) =
                new_iprop;
            }
        }
      *inherited_props = final_iprops;
    }

  if (prop_hash
      && (val = svn_hash_gets(prop_hash, propname)))
    {
      svn_hash_sets(props,
                    apr_pstrdup(result_pool, target_full_url),
                    svn_string_dup(val, result_pool));
    }

  if (depth >= svn_depth_files
      && kind == svn_node_dir
      && apr_hash_count(dirents) > 0)
    {
      apr_hash_index_t *hi;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      for (hi = apr_hash_first(scratch_pool, dirents);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *this_name = apr_hash_this_key(hi);
          svn_dirent_t *this_ent = apr_hash_this_val(hi);
          const char *new_target_relative;
          svn_depth_t depth_below_here = depth;

          svn_pool_clear(iterpool);

          if (depth == svn_depth_files && this_ent->kind == svn_node_dir)
            continue;

          if (depth == svn_depth_files || depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          new_target_relative = svn_relpath_join(target_relative, this_name,
                                                 iterpool);

          SVN_ERR(svn_client__remote_propget(props, NULL,
                                             propname,
                                             target_prefix,
                                             new_target_relative,
                                             this_ent->kind,
                                             revnum,
                                             ra_session,
                                             depth_below_here,
                                             result_pool, iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

/* Baton for recursive_propget_receiver(). */
struct recursive_propget_receiver_baton
{
  apr_hash_t *props; /* Hash to collect props. */
  apr_pool_t *pool; /* Pool to allocate additions to PROPS. */
  svn_wc_context_t *wc_ctx;  /* Working copy context. */
};

/* An implementation of svn_wc__proplist_receiver_t. */
static svn_error_t *
recursive_propget_receiver(void *baton,
                           const char *local_abspath,
                           apr_hash_t *props,
                           apr_pool_t *scratch_pool)
{
  struct recursive_propget_receiver_baton *b = baton;

  if (apr_hash_count(props))
    {
      apr_hash_index_t *hi = apr_hash_first(scratch_pool, props);
      svn_hash_sets(b->props, apr_pstrdup(b->pool, local_abspath),
                    svn_string_dup(apr_hash_this_val(hi), b->pool));
    }

  return SVN_NO_ERROR;
}

/* Return the property value for any PROPNAME set on TARGET in *PROPS,
   with WC paths of char * for keys and property values of
   svn_string_t * for values.  Assumes that PROPS is non-NULL.  Additions
   to *PROPS are allocated in RESULT_POOL, temporary allocations happen in
   SCRATCH_POOL.

   CHANGELISTS is an array of const char * changelist names, used as a
   restrictive filter on items whose properties are set; that is,
   don't set properties on any item unless it's a member of one of
   those changelists.  If CHANGELISTS is empty (or altogether NULL),
   no changelist filtering occurs.

   Treat DEPTH as in svn_client_propget3().
*/
static svn_error_t *
get_prop_from_wc(apr_hash_t **props,
                 const char *propname,
                 const char *target_abspath,
                 svn_boolean_t pristine,
                 svn_node_kind_t kind,
                 svn_depth_t depth,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct recursive_propget_receiver_baton rb;

  /* Technically, svn_depth_unknown just means use whatever depth(s)
     we find in the working copy.  But this is a walk over extant
     working copy paths: if they're there at all, then by definition
     the local depth reaches them, so let's just use svn_depth_infinity
     to get there. */
  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  if (!pristine && depth == svn_depth_infinity
      && (!changelists || changelists->nelts == 0))
    {
      /* Handle this common svn:mergeinfo case more efficient than the target
         list handling in the recursive retrieval. */
      SVN_ERR(svn_wc__prop_retrieve_recursive(
                            props, ctx->wc_ctx, target_abspath, propname,
                            result_pool, scratch_pool));
      return SVN_NO_ERROR;
    }

  *props = apr_hash_make(result_pool);
  rb.props = *props;
  rb.pool = result_pool;
  rb.wc_ctx = ctx->wc_ctx;

  SVN_ERR(svn_wc__prop_list_recursive(ctx->wc_ctx, target_abspath,
                                      propname, depth, pristine,
                                      changelists,
                                      recursive_propget_receiver, &rb,
                                      ctx->cancel_func, ctx->cancel_baton,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

/* Note: this implementation is very similar to svn_client_proplist. */
svn_error_t *
svn_client_propget5(apr_hash_t **props,
                    apr_array_header_t **inherited_props,
                    const char *propname,
                    const char *target,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_revnum_t *actual_revnum,
                    svn_depth_t depth,
                    const apr_array_header_t *changelists,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_revnum_t revnum;
  svn_boolean_t local_explicit_props;
  svn_boolean_t local_iprops;

  SVN_ERR(error_if_wcprop_name(propname));
  if (!svn_path_is_url(target))
    SVN_ERR_ASSERT(svn_dirent_is_absolute(target));

  peg_revision = svn_cl__rev_default_to_head_or_working(peg_revision,
                                                        target);
  revision = svn_cl__rev_default_to_peg(revision, peg_revision);

  local_explicit_props =
    (! svn_path_is_url(target)
     && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(peg_revision->kind)
     && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind));

  local_iprops =
    (local_explicit_props
     && (peg_revision->kind == svn_opt_revision_working
         || peg_revision->kind == svn_opt_revision_unspecified )
     && (revision->kind == svn_opt_revision_working
         || revision->kind == svn_opt_revision_unspecified ));

  if (local_explicit_props)
    {
      svn_node_kind_t kind;
      svn_boolean_t pristine;
      svn_error_t *err;

      /* If FALSE, we want the working revision. */
      pristine = (revision->kind == svn_opt_revision_committed
                  || revision->kind == svn_opt_revision_base);

      SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, target,
                                pristine, FALSE,
                                scratch_pool));

      if (kind == svn_node_unknown || kind == svn_node_none)
        {
          /* svn uses SVN_ERR_UNVERSIONED_RESOURCE as warning only
             for this function. */
          return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                   _("'%s' is not under version control"),
                                   svn_dirent_local_style(target,
                                                          scratch_pool));
        }

      err = svn_client__get_revision_number(&revnum, NULL, ctx->wc_ctx,
                                            target, NULL, revision,
                                            scratch_pool);
      if (err && err->apr_err == SVN_ERR_CLIENT_BAD_REVISION)
        {
          svn_error_clear(err);
          revnum = SVN_INVALID_REVNUM;
        }
      else if (err)
        return svn_error_trace(err);

      if (inherited_props && local_iprops)
        {
          const char *repos_root_url;

          SVN_ERR(svn_wc__get_iprops(inherited_props, ctx->wc_ctx,
                                     target, propname,
                                     result_pool, scratch_pool));
          SVN_ERR(svn_client_get_repos_root(&repos_root_url, NULL,
                                            target, ctx, scratch_pool,
                                            scratch_pool));
          SVN_ERR(svn_client__iprop_relpaths_to_urls(*inherited_props,
                                                     repos_root_url,
                                                     result_pool,
                                                     scratch_pool));
        }

      SVN_ERR(get_prop_from_wc(props, propname, target,
                               pristine, kind,
                               depth, changelists, ctx, result_pool,
                               scratch_pool));
    }

  if ((inherited_props && !local_iprops)
      || !local_explicit_props)
    {
      svn_ra_session_t *ra_session;
      svn_node_kind_t kind;
      svn_opt_revision_t new_operative_rev;
      svn_opt_revision_t new_peg_rev;

      /* Peg or operative revisions may be WC specific for
         TARGET's explicit props, but still require us to
         contact the repository for the inherited properties. */
      if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind)
          || SVN_CLIENT__REVKIND_NEEDS_WC(revision->kind))
        {
          const char *repos_relpath;
          const char *repos_root_url;
          const char *local_abspath;

          /* Avoid assertion on the next line when somebody accidentally asks for
             a working copy revision on a URL */
          if (svn_path_is_url(target))
            return svn_error_create(SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED,
                                    NULL, NULL);

          SVN_ERR_ASSERT(svn_dirent_is_absolute(target));
          local_abspath = target;

          if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind))
            {
              SVN_ERR(svn_wc__node_get_origin(NULL, NULL,
                                              &repos_relpath,
                                              &repos_root_url,
                                              NULL, NULL, NULL,
                                              ctx->wc_ctx,
                                              local_abspath,
                                              FALSE, /* scan_deleted */
                                              result_pool,
                                              scratch_pool));
              if (repos_relpath)
                {
                  target = svn_path_url_add_component2(repos_root_url,
                                                       repos_relpath,
                                                       scratch_pool);
                  if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind))
                    {
                      svn_revnum_t resolved_peg_rev;

                      SVN_ERR(svn_client__get_revision_number(
                        &resolved_peg_rev, NULL, ctx->wc_ctx,
                        local_abspath, NULL, peg_revision, scratch_pool));
                      new_peg_rev.kind = svn_opt_revision_number;
                      new_peg_rev.value.number = resolved_peg_rev;
                      peg_revision = &new_peg_rev;
                    }

                  if (SVN_CLIENT__REVKIND_NEEDS_WC(revision->kind))
                    {
                      svn_revnum_t resolved_operative_rev;

                      SVN_ERR(svn_client__get_revision_number(
                        &resolved_operative_rev, NULL, ctx->wc_ctx,
                        local_abspath, NULL, revision, scratch_pool));
                      new_operative_rev.kind = svn_opt_revision_number;
                      new_operative_rev.value.number = resolved_operative_rev;
                      revision = &new_operative_rev;
                    }
                }
              else
                {
                  /* TARGET doesn't exist in the repository, so there are
                     obviously not inherited props to be found there. */
                  local_iprops = TRUE;
                  *inherited_props = apr_array_make(
                    result_pool, 0, sizeof(svn_prop_inherited_item_t *));
                }
            }
        }

      /* Do we still have anything to ask the repository about? */
      if (!local_explicit_props || !local_iprops)
        {
          svn_client__pathrev_t *loc;

          /* Get an RA plugin for this filesystem object. */
          SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                                    target, NULL,
                                                    peg_revision,
                                                    revision, ctx,
                                                    scratch_pool));

          SVN_ERR(svn_ra_check_path(ra_session, "", loc->rev, &kind,
                                    scratch_pool));

          if (!local_explicit_props)
            *props = apr_hash_make(result_pool);

          SVN_ERR(svn_client__remote_propget(
                                 !local_explicit_props ? *props : NULL,
                                 !local_iprops ? inherited_props : NULL,
                                 propname, loc->url, "",
                                 kind, loc->rev, ra_session,
                                 depth, result_pool, scratch_pool));
          revnum = loc->rev;
        }
    }

  if (actual_revnum)
    *actual_revnum = revnum;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_revprop_get(const char *propname,
                       svn_string_t **propval,
                       const char *URL,
                       const svn_opt_revision_t *revision,
                       svn_revnum_t *set_rev,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err;

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  SVN_ERR(svn_client_open_ra_session2(&ra_session, URL, NULL,
                                      ctx, subpool, subpool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR(svn_client__get_revision_number(set_rev, NULL, ctx->wc_ctx, NULL,
                                          ra_session, revision, subpool));

  /* The actual RA call. */
  err = svn_ra_rev_prop(ra_session, *set_rev, propname, propval, pool);

  /* Close RA session */
  svn_pool_destroy(subpool);
  return svn_error_trace(err);
}


/* Call RECEIVER for the given PATH and its PROP_HASH and/or
 * INHERITED_PROPERTIES.
 *
 * If PROP_HASH is null or has zero count or INHERITED_PROPERTIES is null,
 * then do nothing.
 */
static svn_error_t*
call_receiver(const char *path,
              apr_hash_t *prop_hash,
              apr_array_header_t *inherited_properties,
              svn_proplist_receiver2_t receiver,
              void *receiver_baton,
              apr_pool_t *scratch_pool)
{
  if ((prop_hash && apr_hash_count(prop_hash))
      || inherited_properties)
    SVN_ERR(receiver(receiver_baton, path, prop_hash, inherited_properties,
                     scratch_pool));

  return SVN_NO_ERROR;
}


/* Helper for the remote case of svn_client_proplist.
 *
 * If GET_EXPLICIT_PROPS is true, then call RECEIVER for paths at or under
 * "TARGET_PREFIX/TARGET_RELATIVE@REVNUM" (obtained using RA_SESSION) which
 * have regular properties.  If GET_TARGET_INHERITED_PROPS is true, then send
 * the target's inherited properties to the callback.
 *
 * The 'path' and keys for 'prop_hash' and 'inherited_prop' arguments to
 * RECEIVER are all URLs.
 *
 * RESULT_POOL is used to allocated the 'path', 'prop_hash', and
 * 'inherited_prop' arguments to RECEIVER.  SCRATCH_POOL is used for all
 * other (temporary) allocations.
 *
 * KIND is the kind of the node at "TARGET_PREFIX/TARGET_RELATIVE".
 *
 * If the target is a directory, only fetch properties for the files
 * and directories at depth DEPTH.  DEPTH has not effect on inherited
 * properties.
 */
static svn_error_t *
remote_proplist(const char *target_prefix,
                const char *target_relative,
                svn_node_kind_t kind,
                svn_revnum_t revnum,
                svn_ra_session_t *ra_session,
                svn_boolean_t get_explicit_props,
                svn_boolean_t get_target_inherited_props,
                svn_depth_t depth,
                svn_proplist_receiver2_t receiver,
                void *receiver_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  apr_hash_t *dirents;
  apr_hash_t *prop_hash = NULL;
  apr_hash_index_t *hi;
  const char *target_full_url =
    svn_path_url_add_component2(target_prefix, target_relative, scratch_pool);
  apr_array_header_t *inherited_props;

  /* Note that we pass only the SCRATCH_POOL to svn_ra_get[dir*|file*] because
     we'll be filtering out non-regular properties from PROP_HASH before we
     return. */
  if (kind == svn_node_dir)
    {
      SVN_ERR(svn_ra_get_dir2(ra_session,
                              (depth > svn_depth_empty) ? &dirents : NULL,
                              NULL, &prop_hash, target_relative, revnum,
                              SVN_DIRENT_KIND, scratch_pool));
    }
  else if (kind == svn_node_file)
    {
      SVN_ERR(svn_ra_get_file(ra_session, target_relative, revnum,
                              NULL, NULL, &prop_hash, scratch_pool));
    }
  else
    {
      return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                               _("Unknown node kind for '%s'"),
                               target_full_url);
    }

  if (get_target_inherited_props)
    {
      const char *repos_root_url;

      SVN_ERR(svn_ra_get_inherited_props(ra_session, &inherited_props,
                                         target_relative, revnum,
                                         scratch_pool, scratch_pool));
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url,
                                     scratch_pool));
      SVN_ERR(svn_client__iprop_relpaths_to_urls(inherited_props,
                                                 repos_root_url,
                                                 scratch_pool,
                                                 scratch_pool));
    }
  else
    {
      inherited_props = NULL;
    }

  if (!get_explicit_props)
    prop_hash = NULL;
  else
    {
      /* Filter out non-regular properties, since the RA layer returns all
         kinds.  Copy regular properties keys/vals from the prop_hash
         allocated in SCRATCH_POOL to the "final" hash allocated in
         RESULT_POOL. */
      for (hi = apr_hash_first(scratch_pool, prop_hash);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *name = apr_hash_this_key(hi);
          apr_ssize_t klen = apr_hash_this_key_len(hi);
          svn_prop_kind_t prop_kind;

          prop_kind = svn_property_kind2(name);

          if (prop_kind != svn_prop_regular_kind)
            {
              apr_hash_set(prop_hash, name, klen, NULL);
            }
        }
    }

  SVN_ERR(call_receiver(target_full_url, prop_hash, inherited_props,
                        receiver, receiver_baton, scratch_pool));

  if (depth > svn_depth_empty
      && get_explicit_props
      && (kind == svn_node_dir) && (apr_hash_count(dirents) > 0))
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      for (hi = apr_hash_first(scratch_pool, dirents);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *this_name = apr_hash_this_key(hi);
          svn_dirent_t *this_ent = apr_hash_this_val(hi);
          const char *new_target_relative;

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          svn_pool_clear(iterpool);

          new_target_relative = svn_relpath_join(target_relative,
                                                 this_name, iterpool);

          if (this_ent->kind == svn_node_file
              || depth > svn_depth_files)
            {
              svn_depth_t depth_below_here = depth;

              if (depth == svn_depth_immediates)
                depth_below_here = svn_depth_empty;

              SVN_ERR(remote_proplist(target_prefix,
                                      new_target_relative,
                                      this_ent->kind,
                                      revnum,
                                      ra_session,
                                      TRUE /* get_explicit_props */,
                                      FALSE /* get_target_inherited_props */,
                                      depth_below_here,
                                      receiver, receiver_baton,
                                      cancel_func, cancel_baton,
                                      iterpool));
            }
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}


/* Baton for recursive_proplist_receiver(). */
struct recursive_proplist_receiver_baton
{
  svn_wc_context_t *wc_ctx;  /* Working copy context. */
  svn_proplist_receiver2_t wrapped_receiver;  /* Proplist receiver to call. */
  void *wrapped_receiver_baton;    /* Baton for the proplist receiver. */
  apr_array_header_t *iprops;

  /* Anchor, anchor_abspath pair for converting to relative paths */
  const char *anchor;
  const char *anchor_abspath;
};

/* An implementation of svn_wc__proplist_receiver_t. */
static svn_error_t *
recursive_proplist_receiver(void *baton,
                            const char *local_abspath,
                            apr_hash_t *props,
                            apr_pool_t *scratch_pool)
{
  struct recursive_proplist_receiver_baton *b = baton;
  const char *path;
  apr_array_header_t *iprops = NULL;

  if (b->iprops
      && ! strcmp(local_abspath, b->anchor_abspath))
    {
      /* Report iprops with the properties for the anchor */
      iprops = b->iprops;
      b->iprops = NULL;
    }
  else if (b->iprops)
    {
      /* No report for the root?
         Report iprops anyway */

      SVN_ERR(b->wrapped_receiver(b->wrapped_receiver_baton,
                                  b->anchor ? b->anchor : b->anchor_abspath,
                                  NULL /* prop_hash */,
                                  b->iprops,
                                  scratch_pool));
      b->iprops = NULL;
    }

  /* Attempt to convert absolute paths to relative paths for
   * presentation purposes, if needed. */
  if (b->anchor && b->anchor_abspath)
    {
      path = svn_dirent_join(b->anchor,
                             svn_dirent_skip_ancestor(b->anchor_abspath,
                                                      local_abspath),
                             scratch_pool);
    }
  else
    path = local_abspath;

  return svn_error_trace(b->wrapped_receiver(b->wrapped_receiver_baton,
                                             path, props, iprops,
                                             scratch_pool));
}

/* Helper for svn_client_proplist4 when retrieving properties and/or
   inherited properties from the repository.  Except as noted below,
   all arguments are as per svn_client_proplist4.

   GET_EXPLICIT_PROPS controls if explicit props are retrieved. */
static svn_error_t *
get_remote_props(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 svn_boolean_t get_explicit_props,
                 svn_boolean_t get_target_inherited_props,
                 svn_proplist_receiver2_t receiver,
                 void *receiver_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  svn_node_kind_t kind;
  svn_opt_revision_t new_operative_rev;
  svn_opt_revision_t new_peg_rev;
  svn_client__pathrev_t *loc;

  /* Peg or operative revisions may be WC specific for
     PATH_OR_URL's explicit props, but still require us to
     contact the repository for the inherited properties. */
  if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind)
      || SVN_CLIENT__REVKIND_NEEDS_WC(revision->kind))
    {
      const char *repos_relpath;
      const char *repos_root_url;
      const char *local_abspath;
      svn_boolean_t is_copy;

      /* Avoid assertion on the next line when somebody accidentally asks for
         a working copy revision on a URL */
      if (svn_path_is_url(path_or_url))
        return svn_error_create(SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED,
                                NULL, NULL);

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path_or_url,
                                      scratch_pool));

      if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind))
        {
          SVN_ERR(svn_wc__node_get_origin(&is_copy,
                                          NULL,
                                          &repos_relpath,
                                          &repos_root_url,
                                          NULL, NULL, NULL,
                                          ctx->wc_ctx,
                                          local_abspath,
                                          FALSE, /* scan_deleted */
                                          scratch_pool,
                                          scratch_pool));
          if (repos_relpath)
            {
              path_or_url =
                svn_path_url_add_component2(repos_root_url,
                                            repos_relpath,
                                            scratch_pool);
              if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind))
                {
                  svn_revnum_t resolved_peg_rev;

                  SVN_ERR(svn_client__get_revision_number(&resolved_peg_rev,
                                                          NULL, ctx->wc_ctx,
                                                          local_abspath, NULL,
                                                          peg_revision,
                                                          scratch_pool));
                  new_peg_rev.kind = svn_opt_revision_number;
                  new_peg_rev.value.number = resolved_peg_rev;
                  peg_revision = &new_peg_rev;
                }

              if (SVN_CLIENT__REVKIND_NEEDS_WC(revision->kind))
                {
                  svn_revnum_t resolved_operative_rev;

                  SVN_ERR(svn_client__get_revision_number(
                    &resolved_operative_rev,
                    NULL, ctx->wc_ctx,
                    local_abspath, NULL,
                    revision,
                    scratch_pool));
                  new_operative_rev.kind = svn_opt_revision_number;
                  new_operative_rev.value.number = resolved_operative_rev;
                  revision = &new_operative_rev;
                }
            }
          else
            {
                  /* PATH_OR_URL doesn't exist in the repository, so there are
                     obviously not inherited props to be found there. If we
                     aren't looking for explicit props then we're done. */
                  if (!get_explicit_props)
                    return SVN_NO_ERROR;
            }
        }
    }

  /* Get an RA session for this URL. */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                            path_or_url, NULL,
                                            peg_revision,
                                            revision, ctx,
                                            scratch_pool));

  SVN_ERR(svn_ra_check_path(ra_session, "", loc->rev, &kind,
                            scratch_pool));

  SVN_ERR(remote_proplist(loc->url, "", kind, loc->rev, ra_session,
                          get_explicit_props,
                          get_target_inherited_props,
                          depth, receiver, receiver_baton,
                          ctx->cancel_func, ctx->cancel_baton,
                          scratch_pool));
  return SVN_NO_ERROR;
}

/* Helper for svn_client_proplist4 when retrieving properties and
   possibly inherited properties from the WC.  All arguments are as
   per svn_client_proplist4. */
static svn_error_t *
get_local_props(const char *path_or_url,
                const svn_opt_revision_t *revision,
                svn_depth_t depth,
                const apr_array_header_t *changelists,
                svn_boolean_t get_target_inherited_props,
                svn_proplist_receiver2_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  svn_boolean_t pristine;
  svn_node_kind_t kind;
  apr_hash_t *changelist_hash = NULL;
  const char *local_abspath;
  apr_array_header_t *iprops = NULL;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path_or_url,
                                  scratch_pool));

  pristine = ((revision->kind == svn_opt_revision_committed)
              || (revision->kind == svn_opt_revision_base));

  SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, local_abspath,
                            pristine, FALSE, scratch_pool));

  if (kind == svn_node_unknown || kind == svn_node_none)
    {
      /* svn uses SVN_ERR_UNVERSIONED_RESOURCE as warning only
         for this function. */
      return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                               _("'%s' is not under version control"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  if (get_target_inherited_props)
    {
      const char *repos_root_url;

      SVN_ERR(svn_wc__get_iprops(&iprops, ctx->wc_ctx, local_abspath,
                                 NULL, scratch_pool, scratch_pool));
      SVN_ERR(svn_client_get_repos_root(&repos_root_url, NULL, local_abspath,
                                        ctx, scratch_pool, scratch_pool));
      SVN_ERR(svn_client__iprop_relpaths_to_urls(iprops, repos_root_url,
                                                 scratch_pool,
                                                 scratch_pool));
    }

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash,
                                       changelists, scratch_pool));

  /* Fetch, recursively or not. */
  if (kind == svn_node_dir)
    {
      struct recursive_proplist_receiver_baton rb;

      rb.wc_ctx = ctx->wc_ctx;
      rb.wrapped_receiver = receiver;
      rb.wrapped_receiver_baton = receiver_baton;
      rb.iprops = iprops;
      rb.anchor_abspath = local_abspath;

      if (strcmp(path_or_url, local_abspath) != 0)
        {
          rb.anchor = path_or_url;
        }
      else
        {
          rb.anchor = NULL;
        }

      SVN_ERR(svn_wc__prop_list_recursive(ctx->wc_ctx, local_abspath, NULL,
                                          depth, pristine, changelists,
                                          recursive_proplist_receiver, &rb,
                                          ctx->cancel_func, ctx->cancel_baton,
                                          scratch_pool));

      if (rb.iprops)
        {
          /* We didn't report for the root. Report iprops anyway */
          SVN_ERR(call_receiver(path_or_url, NULL /* props */, rb.iprops,
                                receiver, receiver_baton, scratch_pool));
        }
    }
  else if (svn_wc__changelist_match(ctx->wc_ctx, local_abspath,
                                    changelist_hash, scratch_pool))
    {
      apr_hash_t *props;

        if (pristine)
          SVN_ERR(svn_wc_get_pristine_props(&props,
                                            ctx->wc_ctx, local_abspath,
                                            scratch_pool, scratch_pool));
        else
          {
            svn_error_t *err;

            err = svn_wc_prop_list2(&props, ctx->wc_ctx, local_abspath,
                                    scratch_pool, scratch_pool);


            if (err)
              {
                if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
                  return svn_error_trace(err);
                /* As svn_wc_prop_list2() doesn't return NULL for locally-deleted
                   let's do that here.  */
                svn_error_clear(err);
                props = apr_hash_make(scratch_pool);
              }
          }

      SVN_ERR(call_receiver(path_or_url, props, iprops,
                            receiver, receiver_baton, scratch_pool));

    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_proplist4(const char *path_or_url,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_depth_t depth,
                     const apr_array_header_t *changelists,
                     svn_boolean_t get_target_inherited_props,
                     svn_proplist_receiver2_t receiver,
                     void *receiver_baton,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  svn_boolean_t local_explicit_props;
  svn_boolean_t local_iprops;

  peg_revision = svn_cl__rev_default_to_head_or_working(peg_revision,
                                                        path_or_url);
  revision = svn_cl__rev_default_to_peg(revision, peg_revision);

  if (depth == svn_depth_unknown)
    depth = svn_depth_empty;

  /* Are explicit props available locally? */
  local_explicit_props =
    (! svn_path_is_url(path_or_url)
     && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(peg_revision->kind)
     && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind));

  /* If we want iprops are they available locally? */
  local_iprops =
    (get_target_inherited_props /* We want iprops */
     && local_explicit_props /* No local explicit props means no local iprops. */
     && (peg_revision->kind == svn_opt_revision_working
         || peg_revision->kind == svn_opt_revision_unspecified )
     && (revision->kind == svn_opt_revision_working
         || revision->kind == svn_opt_revision_unspecified ));

  if ((get_target_inherited_props && !local_iprops)
      || !local_explicit_props)
    {
      SVN_ERR(get_remote_props(path_or_url, peg_revision, revision, depth,
                               !local_explicit_props,
                               (get_target_inherited_props && !local_iprops),
                               receiver, receiver_baton, ctx, scratch_pool));
    }

  if (local_explicit_props)
    {
      SVN_ERR(get_local_props(path_or_url, revision, depth, changelists,
                              local_iprops, receiver, receiver_baton, ctx,
                              scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_revprop_list(apr_hash_t **props,
                        const char *URL,
                        const svn_opt_revision_t *revision,
                        svn_revnum_t *set_rev,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  apr_hash_t *proplist;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err;

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  SVN_ERR(svn_client_open_ra_session2(&ra_session, URL, NULL,
                                      ctx, subpool, subpool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR(svn_client__get_revision_number(set_rev, NULL, ctx->wc_ctx, NULL,
                                          ra_session, revision, subpool));

  /* The actual RA call. */
  err = svn_ra_rev_proplist(ra_session, *set_rev, &proplist, pool);

  *props = proplist;
  svn_pool_destroy(subpool); /* Close RA session */
  return svn_error_trace(err);
}
