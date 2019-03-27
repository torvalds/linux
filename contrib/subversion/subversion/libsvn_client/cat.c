/*
 * cat.c:  implementation of the 'cat' command
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
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_subst.h"
#include "svn_io.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

svn_error_t *
svn_client__get_normalized_stream(svn_stream_t **normal_stream,
                                  svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  const svn_opt_revision_t *revision,
                                  svn_boolean_t expand_keywords,
                                  svn_boolean_t normalize_eols,
                                  svn_cancel_func_t cancel_func,
                                  void *cancel_baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  apr_hash_t *kw = NULL;
  svn_subst_eol_style_t style;
  apr_hash_t *props;
  svn_string_t *eol_style, *keywords, *special;
  const char *eol = NULL;
  svn_boolean_t local_mod = FALSE;
  svn_stream_t *input;
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind));

  SVN_ERR(svn_wc_read_kind2(&kind, wc_ctx, local_abspath,
                            (revision->kind != svn_opt_revision_working),
                            FALSE, scratch_pool));

  if (kind == svn_node_unknown || kind == svn_node_none)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (kind != svn_node_file)
    return svn_error_createf(SVN_ERR_CLIENT_IS_DIRECTORY, NULL,
                             _("'%s' refers to a directory"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (revision->kind != svn_opt_revision_working)
    {
      SVN_ERR(svn_wc_get_pristine_contents2(&input, wc_ctx, local_abspath,
                                            result_pool, scratch_pool));
      if (input == NULL)
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                 _("'%s' has no pristine version until it is committed"),
                 svn_dirent_local_style(local_abspath, scratch_pool));

      SVN_ERR(svn_wc_get_pristine_props(&props, wc_ctx, local_abspath,
                                        scratch_pool, scratch_pool));
    }
  else
    {
      svn_wc_status3_t *status;

      SVN_ERR(svn_stream_open_readonly(&input, local_abspath, scratch_pool,
                                       result_pool));

      SVN_ERR(svn_wc_prop_list2(&props, wc_ctx, local_abspath, scratch_pool,
                                scratch_pool));
      SVN_ERR(svn_wc_status3(&status, wc_ctx, local_abspath, scratch_pool,
                             scratch_pool));
      if (status->node_status != svn_wc_status_normal)
        local_mod = TRUE;
    }

  eol_style = svn_hash_gets(props, SVN_PROP_EOL_STYLE);
  keywords = svn_hash_gets(props, SVN_PROP_KEYWORDS);
  special = svn_hash_gets(props, SVN_PROP_SPECIAL);

  if (eol_style)
    svn_subst_eol_style_from_value(&style, &eol, eol_style->data);

  if (keywords)
    {
      svn_revnum_t changed_rev;
      const char *rev_str;
      const char *author;
      const char *url;
      apr_time_t tm;
      const char *repos_root_url;
      const char *repos_relpath;

      SVN_ERR(svn_wc__node_get_changed_info(&changed_rev, &tm, &author, wc_ctx,
                                            local_abspath, scratch_pool,
                                            scratch_pool));
      SVN_ERR(svn_wc__node_get_repos_info(NULL, &repos_relpath, &repos_root_url,
                                          NULL,
                                          wc_ctx, local_abspath, scratch_pool,
                                          scratch_pool));
      url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                        scratch_pool);

      if (local_mod)
        {
          /* For locally modified files, we'll append an 'M'
             to the revision number, and set the author to
             "(local)" since we can't always determine the
             current user's username */
          rev_str = apr_psprintf(scratch_pool, "%ldM", changed_rev);
          author = _("(local)");

          if (! special)
            {
              /* Use the modified time from the working copy for files */
              SVN_ERR(svn_io_file_affected_time(&tm, local_abspath,
                                                scratch_pool));
            }
        }
      else
        {
          rev_str = apr_psprintf(scratch_pool, "%ld", changed_rev);
        }

      SVN_ERR(svn_subst_build_keywords3(&kw, keywords->data, rev_str, url,
                                        repos_root_url, tm, author,
                                        scratch_pool));
    }

  /* Wrap the output stream if translation is needed. */
  if (eol != NULL || kw != NULL)
    input = svn_subst_stream_translated(
      input,
      (eol_style && normalize_eols) ? SVN_SUBST_NATIVE_EOL_STR : eol,
      FALSE, kw, expand_keywords, result_pool);

  *normal_stream = input;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_cat3(apr_hash_t **returned_props,
                svn_stream_t *out,
                const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_boolean_t expand_keywords,
                svn_client_ctx_t *ctx,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  svn_client__pathrev_t *loc;
  svn_string_t *eol_style;
  svn_string_t *keywords;
  apr_hash_t *props = NULL;
  const char *repos_root_url;
  svn_stream_t *output = out;
  svn_error_t *err;

  /* ### Inconsistent default revision logic in this command. */
  if (peg_revision->kind == svn_opt_revision_unspecified)
    {
      peg_revision = svn_cl__rev_default_to_head_or_working(peg_revision,
                                                            path_or_url);
      revision = svn_cl__rev_default_to_head_or_base(revision, path_or_url);
    }
  else
    {
      revision = svn_cl__rev_default_to_peg(revision, peg_revision);
    }

  if (! svn_path_is_url(path_or_url)
      && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(peg_revision->kind)
      && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind))
    {
      const char *local_abspath;
      svn_stream_t *normal_stream;

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path_or_url,
                                      scratch_pool));
      SVN_ERR(svn_client__get_normalized_stream(&normal_stream, ctx->wc_ctx,
                                            local_abspath, revision,
                                            expand_keywords, FALSE,
                                            ctx->cancel_func, ctx->cancel_baton,
                                            scratch_pool, scratch_pool));

      /* We don't promise to close output, so disown it to ensure we don't. */
      output = svn_stream_disown(output, scratch_pool);

      if (returned_props)
        SVN_ERR(svn_wc_prop_list2(returned_props, ctx->wc_ctx, local_abspath,
                                  result_pool, scratch_pool));

      return svn_error_trace(svn_stream_copy3(normal_stream, output,
                                              ctx->cancel_func,
                                              ctx->cancel_baton, scratch_pool));
    }

  /* Get an RA plugin for this filesystem object. */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                            path_or_url, NULL,
                                            peg_revision,
                                            revision, ctx, scratch_pool));

  /* Find the repos root URL */
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url, scratch_pool));

  /* Grab some properties we need to know in order to figure out if anything
     special needs to be done with this file. */
  err = svn_ra_get_file(ra_session, "", loc->rev, NULL, NULL, &props,
                        result_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FILE)
        {
          return svn_error_createf(SVN_ERR_CLIENT_IS_DIRECTORY, err,
                                   _("URL '%s' refers to a directory"),
                                   loc->url);
        }
      else
        {
          return svn_error_trace(err);
        }
    }

  eol_style = svn_hash_gets(props, SVN_PROP_EOL_STYLE);
  keywords = svn_hash_gets(props, SVN_PROP_KEYWORDS);

  if (eol_style || keywords)
    {
      /* It's a file with no special eol style or keywords. */
      svn_subst_eol_style_t eol;
      const char *eol_str;
      apr_hash_t *kw;

      if (eol_style)
        svn_subst_eol_style_from_value(&eol, &eol_str, eol_style->data);
      else
        {
          eol = svn_subst_eol_style_none;
          eol_str = NULL;
        }


      if (keywords && expand_keywords)
        {
          svn_string_t *cmt_rev, *cmt_date, *cmt_author;
          apr_time_t when = 0;

          cmt_rev = svn_hash_gets(props, SVN_PROP_ENTRY_COMMITTED_REV);
          cmt_date = svn_hash_gets(props, SVN_PROP_ENTRY_COMMITTED_DATE);
          cmt_author = svn_hash_gets(props, SVN_PROP_ENTRY_LAST_AUTHOR);
          if (cmt_date)
            SVN_ERR(svn_time_from_cstring(&when, cmt_date->data, scratch_pool));

          SVN_ERR(svn_subst_build_keywords3(&kw, keywords->data,
                                            cmt_rev->data, loc->url,
                                            repos_root_url, when,
                                            cmt_author ?
                                              cmt_author->data : NULL,
                                            scratch_pool));
        }
      else
        kw = NULL;

      /* Interject a translating stream */
      output = svn_subst_stream_translated(svn_stream_disown(out,
                                                             scratch_pool),
                                           eol_str, FALSE, kw, TRUE,
                                           scratch_pool);
    }

  if (returned_props)
    {
      /* filter entry and WC props */
      apr_hash_index_t *hi;
      const void *key;
      apr_ssize_t klen;

      for (hi = apr_hash_first(scratch_pool, props);
           hi; hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &key, &klen, NULL);
          if (!svn_wc_is_normal_prop(key))
            apr_hash_set(props, key, klen, NULL);
        }

      *returned_props = props;
    }

  SVN_ERR(svn_ra_get_file(ra_session, "", loc->rev, output, NULL, NULL,
                          scratch_pool));

  if (out != output)
    /* Close the interjected stream */
    SVN_ERR(svn_stream_close(output));

  return SVN_NO_ERROR;
}
