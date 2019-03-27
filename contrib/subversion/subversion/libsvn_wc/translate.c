/*
 * translate.c :  wc-specific eol/keyword substitution
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



#include <stdlib.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_strings.h>

#include "svn_private_config.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_subst.h"
#include "svn_io.h"
#include "svn_props.h"

#include "wc.h"
#include "adm_files.h"
#include "translate.h"
#include "props.h"

#include "private/svn_wc_private.h"


svn_error_t *
svn_wc__internal_translated_stream(svn_stream_t **stream,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const char *versioned_abspath,
                                   apr_uint32_t flags,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_boolean_t special;
  svn_boolean_t to_nf = flags & SVN_WC_TRANSLATE_TO_NF;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t repair_forced = flags & SVN_WC_TRANSLATE_FORCE_EOL_REPAIR;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(versioned_abspath));

  SVN_ERR(svn_wc__get_translate_info(&style, &eol,
                                     &keywords,
                                     &special,
                                     db, versioned_abspath, NULL, FALSE,
                                     scratch_pool, scratch_pool));

  if (special)
    {
      if (to_nf)
        return svn_subst_read_specialfile(stream, local_abspath, result_pool,
                                          scratch_pool);

      return svn_subst_create_specialfile(stream, local_abspath, result_pool,
                                          scratch_pool);
    }

  if (to_nf)
    SVN_ERR(svn_stream_open_readonly(stream, local_abspath, result_pool,
                                     scratch_pool));
  else
    {
      apr_file_t *file;

      /* We don't want the "open-exclusively" feature of the normal
         svn_stream_open_writable interface. Do this manually. */
      SVN_ERR(svn_io_file_open(&file, local_abspath,
                               APR_CREATE | APR_WRITE | APR_BUFFERED,
                               APR_OS_DEFAULT, result_pool));
      *stream = svn_stream_from_aprfile2(file, FALSE, result_pool);
    }

  if (svn_subst_translation_required(style, eol, keywords, special, TRUE))
    {
      if (to_nf)
        {
          if (style == svn_subst_eol_style_native)
            eol = SVN_SUBST_NATIVE_EOL_STR;
          else if (style == svn_subst_eol_style_fixed)
            repair_forced = TRUE;
          else if (style != svn_subst_eol_style_none)
            return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);

          /* Wrap the stream to translate to normal form */
          *stream = svn_subst_stream_translated(*stream,
                                                eol,
                                                repair_forced,
                                                keywords,
                                                FALSE /* expand */,
                                                result_pool);

          /* streams enforce our contract that TO_NF streams are read-only
           * by returning SVN_ERR_STREAM_NOT_SUPPORTED when trying to
           * write to them. */
        }
      else
        {
          *stream = svn_subst_stream_translated(*stream, eol, TRUE,
                                                keywords, TRUE, result_pool);

          /* streams enforce our contract that FROM_NF streams are write-only
           * by returning SVN_ERR_STREAM_NOT_SUPPORTED when trying to
           * read them. */
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__internal_translated_file(const char **xlated_abspath,
                                 const char *src_abspath,
                                 svn_wc__db_t *db,
                                 const char *versioned_abspath,
                                 apr_uint32_t flags,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t special;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(versioned_abspath));
  SVN_ERR(svn_wc__get_translate_info(&style, &eol,
                                     &keywords,
                                     &special,
                                     db, versioned_abspath, NULL, FALSE,
                                     scratch_pool, scratch_pool));

  if (! svn_subst_translation_required(style, eol, keywords, special, TRUE)
      && (! (flags & SVN_WC_TRANSLATE_FORCE_COPY)))
    {
      /* Translation would be a no-op, so return the original file. */
      *xlated_abspath = src_abspath;
    }
  else  /* some translation (or copying) is necessary */
    {
      const char *tmp_dir;
      const char *tmp_vfile;
      svn_boolean_t repair_forced
          = (flags & SVN_WC_TRANSLATE_FORCE_EOL_REPAIR) != 0;
      svn_boolean_t expand = (flags & SVN_WC_TRANSLATE_TO_NF) == 0;

      if (flags & SVN_WC_TRANSLATE_USE_GLOBAL_TMP)
        tmp_dir = NULL;
      else
        SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmp_dir, db, versioned_abspath,
                                               scratch_pool, scratch_pool));

      SVN_ERR(svn_io_open_unique_file3(NULL, &tmp_vfile, tmp_dir,
                (flags & SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP)
                  ? svn_io_file_del_none
                  : svn_io_file_del_on_pool_cleanup,
                result_pool, scratch_pool));

      /* ### ugh. the repair behavior does NOT match the docstring. bleah.
         ### all of these translation functions are crap and should go
         ### away anyways. we'll just deprecate most of the functions and
         ### properly document the survivors */

      if (expand)
        {
          /* from normal form */

          repair_forced = TRUE;
        }
      else
        {
          /* to normal form */

          if (style == svn_subst_eol_style_native)
            eol = SVN_SUBST_NATIVE_EOL_STR;
          else if (style == svn_subst_eol_style_fixed)
            repair_forced = TRUE;
          else if (style != svn_subst_eol_style_none)
            return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);
        }

      SVN_ERR(svn_subst_copy_and_translate4(src_abspath, tmp_vfile,
                                            eol, repair_forced,
                                            keywords,
                                            expand,
                                            special,
                                            cancel_func, cancel_baton,
                                            result_pool));

      *xlated_abspath = tmp_vfile;
    }

  return SVN_NO_ERROR;
}

void
svn_wc__eol_value_from_string(const char **value, const char *eol)
{
  if (eol == NULL)
    *value = NULL;
  else if (! strcmp("\n", eol))
    *value = "LF";
  else if (! strcmp("\r", eol))
    *value = "CR";
  else if (! strcmp("\r\n", eol))
    *value = "CRLF";
  else
    *value = NULL;
}

svn_error_t *
svn_wc__get_translate_info(svn_subst_eol_style_t *style,
                           const char **eol,
                           apr_hash_t **keywords,
                           svn_boolean_t *special,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_hash_t *props,
                           svn_boolean_t for_normalization,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *propval;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (props == NULL)
    SVN_ERR(svn_wc__get_actual_props(&props, db, local_abspath,
                                     scratch_pool, scratch_pool));

  if (eol)
    {
      propval = svn_prop_get_value(props, SVN_PROP_EOL_STYLE);

      svn_subst_eol_style_from_value(style, eol, propval);
    }

  if (keywords)
    {
      propval = svn_prop_get_value(props, SVN_PROP_KEYWORDS);

      if (!propval || *propval == '\0')
        *keywords = NULL;
      else
        SVN_ERR(svn_wc__expand_keywords(keywords,
                                        db, local_abspath, NULL,
                                        propval, for_normalization,
                                        result_pool, scratch_pool));
    }
  if (special)
    {
      propval = svn_prop_get_value(props, SVN_PROP_SPECIAL);

      *special = (propval != NULL);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__expand_keywords(apr_hash_t **keywords,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const char *wri_abspath,
                        const char *keyword_list,
                        svn_boolean_t for_normalization,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const char *url;
  const char *repos_root_url;

  if (! for_normalization)
    {
      const char *repos_relpath;

      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, &repos_relpath,
                                   &repos_root_url, NULL, &changed_rev,
                                   &changed_date, &changed_author, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

      /* Handle special statuses (e.g. added) */
      if (!repos_relpath)
         SVN_ERR(svn_wc__db_read_repos_info(NULL, &repos_relpath,
                                            &repos_root_url, NULL,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));

      url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                        scratch_pool);
    }
  else
    {
      url = "";
      changed_rev = SVN_INVALID_REVNUM;
      changed_date = 0;
      changed_author = "";
      repos_root_url = "";
    }

  SVN_ERR(svn_subst_build_keywords3(keywords, keyword_list,
                                    apr_psprintf(scratch_pool, "%ld",
                                                 changed_rev),
                                    url, repos_root_url,
                                    changed_date, changed_author,
                                    result_pool));

  if (apr_hash_count(*keywords) == 0)
    *keywords = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__sync_flags_with_props(svn_boolean_t *did_set,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_wc__db_lock_t *lock;
  apr_hash_t *props = NULL;
  svn_boolean_t had_props;
  svn_boolean_t props_mod;

  if (did_set)
    *did_set = FALSE;

  /* ### We'll consolidate these info gathering statements in a future
         commit. */

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, &lock, NULL, NULL, NULL, NULL, NULL,
                               &had_props, &props_mod, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  /* We actually only care about the following flags on files, so just
     early-out for all other types.

     Also bail if there is no in-wc representation of the file. */
  if (kind != svn_node_file
      || (status != svn_wc__db_status_normal
          && status != svn_wc__db_status_added))
    return SVN_NO_ERROR;

  if (props_mod || had_props)
    SVN_ERR(svn_wc__db_read_props(&props, db, local_abspath, scratch_pool,
                                  scratch_pool));
  else
    props = NULL;

  /* If we get this far, we're going to change *something*, so just set
     the flag appropriately. */
  if (did_set)
    *did_set = TRUE;

  /* Handle the read-write bit. */
  if (status != svn_wc__db_status_normal
      || props == NULL
      || ! svn_hash_gets(props, SVN_PROP_NEEDS_LOCK)
      || lock)
    {
      SVN_ERR(svn_io_set_file_read_write(local_abspath, FALSE, scratch_pool));
    }
  else
    {
      /* Special case: If we have an uncommitted svn:needs-lock, we don't
         set the file read_only just yet.  That happens upon commit. */
      apr_hash_t *pristine_props;

      if (! props_mod)
        pristine_props = props;
      else if (had_props)
        SVN_ERR(svn_wc__db_read_pristine_props(&pristine_props, db, local_abspath,
                                                scratch_pool, scratch_pool));
      else
        pristine_props = NULL;

      if (pristine_props
            && svn_hash_gets(pristine_props, SVN_PROP_NEEDS_LOCK) )
            /*&& props
            && apr_hash_get(props, SVN_PROP_NEEDS_LOCK, APR_HASH_KEY_STRING) )*/
        SVN_ERR(svn_io_set_file_read_only(local_abspath, FALSE, scratch_pool));
    }

/* Windows doesn't care about the execute bit. */
#ifndef WIN32

  if (props == NULL
      || ! svn_hash_gets(props, SVN_PROP_EXECUTABLE))
    {
      /* Turn off the execute bit */
      SVN_ERR(svn_io_set_file_executable(local_abspath, FALSE, FALSE,
                                         scratch_pool));
    }
  else
    SVN_ERR(svn_io_set_file_executable(local_abspath, TRUE, FALSE,
                                       scratch_pool));
#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__translated_stream(svn_stream_t **stream,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          const char *versioned_abspath,
                          apr_uint32_t flags,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(
           svn_wc__internal_translated_stream(stream, wc_ctx->db,
                                              local_abspath,
                                              versioned_abspath,
                                              flags, result_pool,
                                              scratch_pool));
}
