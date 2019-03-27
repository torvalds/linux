/*
 * conflict-callbacks.c: conflict resolution callbacks specific to the
 * commandline client.
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

#include <apr_xlate.h>  /* for APR_LOCALE_CHARSET */

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_hash.h"
#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_utf.h"

#include "cl.h"
#include "cl-conflicts.h"

#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"

#define ARRAY_LEN(ary) ((sizeof (ary)) / (sizeof ((ary)[0])))



svn_cl__accept_t
svn_cl__accept_from_word(const char *word)
{
  /* Shorthand options are consistent with  svn_cl__conflict_handler(). */
  if (strcmp(word, SVN_CL__ACCEPT_POSTPONE) == 0
      || strcmp(word, "p") == 0 || strcmp(word, ":-P") == 0)
    return svn_cl__accept_postpone;
  if (strcmp(word, SVN_CL__ACCEPT_BASE) == 0)
    /* ### shorthand? */
    return svn_cl__accept_base;
  if (strcmp(word, SVN_CL__ACCEPT_WORKING) == 0)
    /* ### shorthand? */
    return svn_cl__accept_working;
  if (strcmp(word, SVN_CL__ACCEPT_MINE_CONFLICT) == 0
      || strcmp(word, "mc") == 0 || strcmp(word, "X-)") == 0)
    return svn_cl__accept_mine_conflict;
  if (strcmp(word, SVN_CL__ACCEPT_THEIRS_CONFLICT) == 0
      || strcmp(word, "tc") == 0 || strcmp(word, "X-(") == 0)
    return svn_cl__accept_theirs_conflict;
  if (strcmp(word, SVN_CL__ACCEPT_MINE_FULL) == 0
      || strcmp(word, "mf") == 0 || strcmp(word, ":-)") == 0)
    return svn_cl__accept_mine_full;
  if (strcmp(word, SVN_CL__ACCEPT_THEIRS_FULL) == 0
      || strcmp(word, "tf") == 0 || strcmp(word, ":-(") == 0)
    return svn_cl__accept_theirs_full;
  if (strcmp(word, SVN_CL__ACCEPT_EDIT) == 0
      || strcmp(word, "e") == 0 || strcmp(word, ":-E") == 0)
    return svn_cl__accept_edit;
  if (strcmp(word, SVN_CL__ACCEPT_LAUNCH) == 0
      || strcmp(word, "l") == 0 || strcmp(word, ":-l") == 0)
    return svn_cl__accept_launch;
  if (strcmp(word, SVN_CL__ACCEPT_RECOMMENDED) == 0
      || strcmp(word, "r") == 0)
    return svn_cl__accept_recommended;
  /* word is an invalid action. */
  return svn_cl__accept_invalid;
}


/* Print on stdout a diff that shows incoming conflicting changes
 * corresponding to the conflict described in CONFLICT. */
static svn_error_t *
show_diff(svn_client_conflict_t *conflict,
          const char *merged_abspath,
          const char *path_prefix,
          svn_cancel_func_t cancel_func,
          void *cancel_baton,
          apr_pool_t *pool)
{
  const char *path1, *path2;
  const char *label1, *label2;
  svn_diff_t *diff;
  svn_stream_t *output;
  svn_diff_file_options_t *options;
  const char *my_abspath;
  const char *their_abspath;

  SVN_ERR(svn_client_conflict_text_get_contents(NULL, &my_abspath, NULL,
                                                &their_abspath,
                                                conflict, pool, pool));
  if (merged_abspath)
    {
      /* For conflicts recorded by the 'merge' operation, show a diff between
       * 'mine' (the working version of the file as it appeared before the
       * 'merge' operation was run) and 'merged' (the version of the file
       * as it appears after the merge operation).
       *
       * For conflicts recorded by the 'update' and 'switch' operations,
       * show a diff between 'theirs' (the new pristine version of the
       * file) and 'merged' (the version of the file as it appears with
       * local changes merged with the new pristine version).
       *
       * This way, the diff is always minimal and clearly identifies changes
       * brought into the working copy by the update/switch/merge operation. */
      if (svn_client_conflict_get_operation(conflict) == svn_wc_operation_merge)
        {
          path1 = my_abspath;
          label1 = _("MINE");
        }
      else
        {
          path1 = their_abspath;
          label1 = _("THEIRS");
        }
      path2 = merged_abspath;
      label2 = _("MERGED");
    }
  else
    {
      /* There's no merged file, but we can show the
         difference between mine and theirs. */
      path1 = their_abspath;
      label1 = _("THEIRS");
      path2 = my_abspath;
      label2 = _("MINE");
    }

  label1 = apr_psprintf(pool, "%s\t- %s",
                        svn_cl__local_style_skip_ancestor(
                          path_prefix, path1, pool), label1);
  label2 = apr_psprintf(pool, "%s\t- %s",
                        svn_cl__local_style_skip_ancestor(
                          path_prefix, path2, pool), label2);

  options = svn_diff_file_options_create(pool);
  options->ignore_eol_style = TRUE;
  SVN_ERR(svn_stream_for_stdout(&output, pool));
  SVN_ERR(svn_diff_file_diff_2(&diff, path1, path2,
                               options, pool));
  return svn_diff_file_output_unified4(output, diff,
                                       path1, path2,
                                       label1, label2,
                                       APR_LOCALE_CHARSET,
                                       NULL,
                                       options->show_c_function,
                                       options->context_size,
                                       cancel_func, cancel_baton,
                                       pool);
}


/* Print on stdout just the conflict hunks of a diff among the 'base', 'their'
 * and 'my' files of CONFLICT. */
static svn_error_t *
show_conflicts(svn_client_conflict_t *conflict,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  svn_diff_t *diff;
  svn_stream_t *output;
  svn_diff_file_options_t *options;
  const char *base_abspath;
  const char *my_abspath;
  const char *their_abspath;

  SVN_ERR(svn_client_conflict_text_get_contents(NULL, &my_abspath,
                                                &base_abspath, &their_abspath,
                                                conflict, pool, pool));
  options = svn_diff_file_options_create(pool);
  options->ignore_eol_style = TRUE;
  SVN_ERR(svn_stream_for_stdout(&output, pool));
  SVN_ERR(svn_diff_file_diff3_2(&diff, base_abspath, my_abspath, their_abspath,
                                options, pool));
  /* ### Consider putting the markers/labels from
     ### svn_wc__merge_internal in the conflict description. */
  return svn_diff_file_output_merge3(
           output, diff, base_abspath, my_abspath, their_abspath,
           _("||||||| ORIGINAL"),
           _("<<<<<<< MINE (select with 'mc')"),
           _(">>>>>>> THEIRS (select with 'tc')"),
           "=======",
           svn_diff_conflict_display_only_conflicts,
           cancel_func,
           cancel_baton,
           pool);
}

/* Perform a 3-way merge of the conflicting values of a property,
 * and write the result to the OUTPUT stream.
 *
 * If MERGED_PROPVAL is non-NULL, use it as 'my' version instead of
 * MY_ABSPATH.
 *
 * Assume the values are printable UTF-8 text.
 */
static svn_error_t *
merge_prop_conflict(svn_stream_t *output,
                    const svn_string_t *base_propval,
                    const svn_string_t *my_propval,
                    const svn_string_t *their_propval,
                    const svn_string_t *merged_propval,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *pool)
{
  svn_diff_file_options_t *options = svn_diff_file_options_create(pool);
  svn_diff_t *diff;

  /* If any of the property values is missing, use an empty value instead
   * for the purpose of showing a diff. */
  if (base_propval == NULL)
    base_propval = svn_string_create_empty(pool);
  if (my_propval == NULL)
    my_propval = svn_string_create_empty(pool);
  if (their_propval == NULL)
    their_propval = svn_string_create_empty(pool);
    
  options->ignore_eol_style = TRUE;
  SVN_ERR(svn_diff_mem_string_diff3(&diff, base_propval,
                                    merged_propval ?
                                      merged_propval : my_propval,
                                    their_propval, options, pool));
  SVN_ERR(svn_diff_mem_string_output_merge3(
            output, diff, base_propval,
            merged_propval ? merged_propval : my_propval, their_propval,
            _("||||||| ORIGINAL"),
            _("<<<<<<< MINE"),
            _(">>>>>>> THEIRS"),
            "=======",
            svn_diff_conflict_display_modified_original_latest,
            cancel_func,
            cancel_baton,
            pool));

  return SVN_NO_ERROR;
}

/* Display the conflicting values of a property as a 3-way diff.
 *
 * If MERGED_ABSPATH is non-NULL, show it as 'my' version instead of
 * DESC->MY_ABSPATH.
 *
 * Assume the values are printable UTF-8 text.
 */
static svn_error_t *
show_prop_conflict(const svn_string_t *base_propval,
                   const svn_string_t *my_propval,
                   const svn_string_t *their_propval,
                   const svn_string_t *merged_propval,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  svn_stream_t *output;

  SVN_ERR(svn_stream_for_stdout(&output, pool));
  SVN_ERR(merge_prop_conflict(output, base_propval, my_propval, their_propval,
                              merged_propval, cancel_func, cancel_baton, pool));

  return SVN_NO_ERROR;
}

/* Run an external editor, passing it the MERGED_ABSPATH, or, if the
 * 'merged' file is null, return an error. The tool to use is determined by
 * B->editor_cmd, B->config and environment variables; see
 * svn_cl__edit_file_externally() for details.
 *
 * If the tool runs, set *PERFORMED_EDIT to true; if a tool is not
 * configured or cannot run, do not touch *PERFORMED_EDIT, report the error
 * on stderr, and return SVN_NO_ERROR; if any other error is encountered,
 * return that error. */
static svn_error_t *
open_editor(svn_boolean_t *performed_edit,
            const char *merged_abspath,
            const char *editor_cmd,
            apr_hash_t *config,
            apr_pool_t *pool)
{
  svn_error_t *err;

  if (merged_abspath)
    {
      err = svn_cmdline__edit_file_externally(merged_abspath, editor_cmd,
                                              config, pool);
      if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR ||
                  err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
        {
          char buf[1024];
          const char *message;

          message = svn_err_best_message(err, buf, sizeof(buf));
          SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n", message));
          svn_error_clear(err);
        }
      else if (err)
        return svn_error_trace(err);
      else
        *performed_edit = TRUE;
    }
  else
    SVN_ERR(svn_cmdline_fprintf(stderr, pool,
                                _("Invalid option; there's no "
                                  "merged version to edit.\n\n")));

  return SVN_NO_ERROR;
}

/* Run an external editor on the merged property value with conflict markers.
 * Return the edited result in *MERGED_PROPVAL.
 * If the edit is aborted, set *MERGED_ABSPATH and *MERGED_PROPVAL to NULL.
 * The tool to use is determined by B->editor_cmd, B->config and
 * environment variables; see svn_cl__edit_file_externally() for details. */
static svn_error_t *
edit_prop_conflict(const svn_string_t **merged_propval,
                   const svn_string_t *base_propval,
                   const svn_string_t *my_propval,
                   const svn_string_t *their_propval,
                   const char *editor_cmd,
                   apr_hash_t *config,
                   svn_cmdline_prompt_baton_t *pb,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *file_path;
  svn_boolean_t performed_edit = FALSE;
  svn_stream_t *merged_prop;

  SVN_ERR(svn_stream_open_unique(&merged_prop, &file_path, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  SVN_ERR(merge_prop_conflict(merged_prop, base_propval, my_propval,
                              their_propval, NULL,
                              pb->cancel_func,
                              pb->cancel_baton,
                              scratch_pool));
  SVN_ERR(svn_stream_close(merged_prop));
  SVN_ERR(open_editor(&performed_edit, file_path, editor_cmd,
                      config, scratch_pool));
  if (performed_edit && merged_propval)
    {
      svn_stringbuf_t *buf;

      SVN_ERR(svn_stringbuf_from_file2(&buf, file_path, scratch_pool));
      *merged_propval = svn_string_create_from_buf(buf, result_pool); 
    }

  return SVN_NO_ERROR;
}

/* Maximum line length for the prompt string. */
#define MAX_PROMPT_WIDTH 70

/* Description of a resolver option.
 * Resolver options are used to build the resolver's conflict prompt.
 * The user types a code to select the corresponding conflict resolution option.
 * Some resolver options have a corresponding --accept argument. */
typedef struct resolver_option_t
{
  const char *code;        /* one or two characters */
  svn_client_conflict_option_id_t choice;
                           /* or ..._undefined if not from libsvn_client */
  const char *accept_arg;  /* --accept option argument (NOT localized) */
} resolver_option_t;

typedef struct client_option_t
{
  const char *code;        /* one or two characters */
  const char *label;       /* label in prompt (localized) */
  const char *long_desc;   /* longer description (localized) */
  svn_client_conflict_option_id_t choice;
                           /* or ..._undefined if not from libsvn_client */
  const char *accept_arg;  /* --accept option argument (NOT localized) */
  svn_boolean_t is_recommended; /* if TRUE, try this option before prompting */
} client_option_t;

/* Resolver options for conflict options offered by libsvn_client.  */
static const resolver_option_t builtin_resolver_options[] =
{
  { "r",  svn_client_conflict_option_merged_text,
          SVN_CL__ACCEPT_WORKING },
  { "mc", svn_client_conflict_option_working_text_where_conflicted,
          SVN_CL__ACCEPT_MINE_CONFLICT },
  { "tc", svn_client_conflict_option_incoming_text_where_conflicted,
          SVN_CL__ACCEPT_THEIRS_CONFLICT },
  { "mf", svn_client_conflict_option_working_text,
          SVN_CL__ACCEPT_MINE_FULL},
  { "tf", svn_client_conflict_option_incoming_text,
          SVN_CL__ACCEPT_THEIRS_FULL },
  { "p",  svn_client_conflict_option_postpone,
          SVN_CL__ACCEPT_POSTPONE },

  /* This option resolves a tree conflict to the current working copy state. */
  { "r", svn_client_conflict_option_accept_current_wc_state,
         SVN_CL__ACCEPT_WORKING },

  /* These options use the same code since they only occur in
   * distinct conflict scenarios. */
  { "u", svn_client_conflict_option_update_move_destination },
  { "u", svn_client_conflict_option_update_any_moved_away_children },

  /* Options for incoming add vs local add. */
  { "i", svn_client_conflict_option_incoming_add_ignore },

  /* Options for incoming file add vs local file add upon merge. */
  { "m", svn_client_conflict_option_incoming_added_file_text_merge },
  { "M", svn_client_conflict_option_incoming_added_file_replace_and_merge },

  /* Options for incoming dir add vs local dir add upon merge. */
  { "m", svn_client_conflict_option_incoming_added_dir_merge },
  { "R", svn_client_conflict_option_incoming_added_dir_replace },
  { "M", svn_client_conflict_option_incoming_added_dir_replace_and_merge },

  /* Options for incoming delete vs any. */
  { "i", svn_client_conflict_option_incoming_delete_ignore },
  { "a", svn_client_conflict_option_incoming_delete_accept },

  /* Options for incoming move vs local edit. */
  { "m", svn_client_conflict_option_incoming_move_file_text_merge },
  { "m", svn_client_conflict_option_incoming_move_dir_merge },

  /* Options for local move vs incoming edit. */
  { "m", svn_client_conflict_option_local_move_file_text_merge },

  { NULL }
};

/* Extra resolver options offered by 'svn' for any conflict. */
static const client_option_t extra_resolver_options[] =
{
  /* Translators: keep long_desc below 70 characters (wrap with a left
     margin of 9 spaces if needed) */
  { "q",  N_("Quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_client_conflict_option_postpone },
  { NULL }
};


/* Additional resolver options offered by 'svn' for a text conflict. */
static const client_option_t extra_resolver_options_text[] =
{
  /* Translators: keep long_desc below 70 characters (wrap with a left
     margin of 9 spaces if needed) */
  { "e",  N_("Edit file"),        N_("change merged file in an editor"),
                                  svn_client_conflict_option_undefined,
                                  SVN_CL__ACCEPT_EDIT },
  { "df", N_("Show diff"),        N_("show all changes made to merged file"),
                                  svn_client_conflict_option_undefined},
  { "dc", N_("Display conflict"), N_("show all conflicts "
                                     "(ignoring merged version)"),
                                  svn_client_conflict_option_undefined },
  { "m",  N_("Merge"),            N_("use merge tool to resolve conflict"),
                                  svn_client_conflict_option_undefined },
  { "l",  N_("Launch tool"),      N_("launch external merge tool to resolve "
                                     "conflict"),
                                  svn_client_conflict_option_undefined,
                                  SVN_CL__ACCEPT_LAUNCH },
  { "i",  N_("Internal merge tool"), N_("use built-in merge tool to "
                                     "resolve conflict"),
                                  svn_client_conflict_option_undefined },
  { "s",  N_("Show all options"), N_("show this list (also 'h', '?')"),
                                  svn_client_conflict_option_undefined },
  { NULL }
};

/* Additional resolver options offered by 'svn' for a property conflict. */
static const client_option_t extra_resolver_options_prop[] =
{
  /* Translators: keep long_desc below 70 characters (wrap with a left
     margin of 9 spaces if needed) */
  { "dc", N_("Display conflict"), N_("show conflicts in this property"),
                                  svn_client_conflict_option_undefined },
  { "e",  N_("Edit property"),    N_("change merged property value in an "
                                     "editor"),
                                  svn_client_conflict_option_undefined,
                                  SVN_CL__ACCEPT_EDIT },
  { "h",  N_("Help"),             N_("show this help (also '?')"),
                                  svn_client_conflict_option_undefined },
  { NULL }
};

/* Additional resolver options offered by 'svn' for a tree conflict. */
static const client_option_t extra_resolver_options_tree[] =
{
  /* Translators: keep long_desc below 70 characters (wrap with a left
     margin of 9 spaces if needed) */
  { "d",  N_("Set repository move destination path"),
          N_("pick repository move target from list of possible targets"),
                                  svn_client_conflict_option_undefined },

  { "w",  N_("Set working copy move destination path"),
          N_("pick working copy move target from list of possible targets"),
                                  svn_client_conflict_option_undefined },

  { "h",  N_("Help"),             N_("show this help (also '?')"),
                                  svn_client_conflict_option_undefined },

  { NULL }
};


/* Return a pointer to the option description in OPTIONS matching the
 * one- or two-character OPTION_CODE.  Return NULL if not found. */
static const client_option_t *
find_option(const apr_array_header_t *options,
            const char *option_code)
{
  int i;

  for (i = 0; i < options->nelts; i++)
    {
      const client_option_t *opt = APR_ARRAY_IDX(options, i, client_option_t *);

      /* Ignore code "" (blank lines) which is not a valid answer. */
      if (opt->code[0] && strcmp(opt->code, option_code) == 0)
        return opt;
    }
  return NULL;
}

/* Find the first recommended option in OPTIONS. */
static const client_option_t *
find_recommended_option(const apr_array_header_t *options)
{
  int i;

  for (i = 0; i < options->nelts; i++)
    {
      const client_option_t *opt = APR_ARRAY_IDX(options, i, client_option_t *);

      /* Ignore code "" (blank lines) which is not a valid answer. */
      if (opt->code[0] && opt->is_recommended)
        return opt;
    }
  return NULL;
}

/* Return a pointer to the client_option_t in OPTIONS matching the ID of
 * conflict option BUILTIN_OPTION. @a out will be set to NULL if the
 * option was not found. */
static svn_error_t *
find_option_by_builtin(client_option_t **out,
                       svn_client_conflict_t *conflict,
                       const resolver_option_t *options,
                       svn_client_conflict_option_t *builtin_option,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const resolver_option_t *opt;
  svn_client_conflict_option_id_t id;
  svn_client_conflict_option_id_t recommended_id;

  id = svn_client_conflict_option_get_id(builtin_option);
  recommended_id = svn_client_conflict_get_recommended_option_id(conflict);

  for (opt = options; opt->code; opt++)
    {
      if (opt->choice == id)
        {
          client_option_t *client_opt;

          client_opt = apr_pcalloc(result_pool, sizeof(*client_opt));
          client_opt->choice = id;
          client_opt->code = opt->code;
          client_opt->label = svn_client_conflict_option_get_label(
              builtin_option,
              result_pool);
          client_opt->long_desc = svn_client_conflict_option_get_description(
                                    builtin_option,
                                    result_pool);
          client_opt->accept_arg = opt->accept_arg;
          client_opt->is_recommended =
            (recommended_id != svn_client_conflict_option_unspecified &&
             id == recommended_id);

          *out = client_opt;

          return SVN_NO_ERROR;
        }
    }

  *out = NULL;

  return SVN_NO_ERROR;
}

/* Return a prompt string listing the options OPTIONS. If OPTION_CODES is
 * non-null, select only the options whose codes are mentioned in it. */
static const char *
prompt_string(const apr_array_header_t *options,
              const char *const *option_codes,
              apr_pool_t *pool)
{
  const char *result = _("Select:");
  int left_margin = svn_utf_cstring_utf8_width(result);
  const char *line_sep = apr_psprintf(pool, "\n%*s", left_margin, "");
  int this_line_len = left_margin;
  svn_boolean_t first = TRUE;
  int i = 0;

  while (1)
    {
      const client_option_t *opt;
      const char *s;
      int slen;

      if (option_codes)
        {
          if (! *option_codes)
            break;
          opt = find_option(options, *option_codes++);
          if (opt == NULL)
            continue;
        }
      else
        {
          if (i >= options->nelts)
            break;
          opt = APR_ARRAY_IDX(options, i, client_option_t *);
          i++;
        }

      if (! first)
        result = apr_pstrcat(pool, result, ",", SVN_VA_NULL);
      s = apr_psprintf(pool, " (%s) %s", opt->code,
                       opt->label ? opt->label : opt->long_desc);
      slen = svn_utf_cstring_utf8_width(s);
      /* Break the line if adding the next option would make it too long */
      if (this_line_len + slen > MAX_PROMPT_WIDTH)
        {
          result = apr_pstrcat(pool, result, line_sep, SVN_VA_NULL);
          this_line_len = left_margin;
        }
      result = apr_pstrcat(pool, result, s, SVN_VA_NULL);
      this_line_len += slen;
      first = FALSE;
    }
  return apr_pstrcat(pool, result, ": ", SVN_VA_NULL);
}

/* Return a help string listing the OPTIONS. */
static svn_error_t *
help_string(const char **result,
            const apr_array_header_t *options,
            apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  int i;

  *result = "";
  iterpool = svn_pool_create(pool);
  for (i = 0; i < options->nelts; i++)
    {
      const client_option_t *opt;
      svn_pool_clear(iterpool);

      opt = APR_ARRAY_IDX(options, i,
                          client_option_t *);

      /* Append a line describing OPT, or a blank line if its code is "". */
      if (opt->code[0])
        {
          const char *s = apr_psprintf(pool, "  (%s)", opt->code);

          if (opt->accept_arg)
            *result = apr_psprintf(pool, "%s%-6s - %s  [%s]\n",
                                   *result, s, opt->long_desc,
                                   opt->accept_arg);
          else
            *result = apr_psprintf(pool, "%s%-6s - %s\n", *result, s,
                                   opt->long_desc);
        }
      else
        {
          *result = apr_pstrcat(pool, *result, "\n", SVN_VA_NULL);
        }
    }
  svn_pool_destroy(iterpool);
  *result = apr_pstrcat(pool, *result,
                       _("Words in square brackets are the corresponding "
                         "--accept option arguments.\n"),
                       SVN_VA_NULL);
  return SVN_NO_ERROR;
}

/* Prompt the user with CONFLICT_OPTIONS, restricted to the options listed
 * in OPTIONS_TO_SHOW if that is non-null.  Set *OPT to point to the chosen
 * one of CONFLICT_OPTIONS (not necessarily one of OPTIONS_TO_SHOW), or to
 * NULL if the answer was not one of them.
 *
 * If the answer is the (globally recognized) 'help' option, then display
 * CONFLICT_DESCRIPTION (if not NULL) and help (on stderr) and return with
 * *OPT == NULL.
 */
static svn_error_t *
prompt_user(const client_option_t **opt,
            const apr_array_header_t *conflict_options,
            const char *const *options_to_show,
            const char *conflict_description,
            void *prompt_baton,
            apr_pool_t *scratch_pool)
{
  const char *prompt
    = prompt_string(conflict_options, options_to_show, scratch_pool);
  const char *answer;

  SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, prompt_baton, scratch_pool));
  if (strcmp(answer, "h") == 0 || strcmp(answer, "?") == 0)
    {
      const char *helpstr;

      if (conflict_description)
        SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "\n%s\n",
                                    conflict_description));
      SVN_ERR(help_string(&helpstr, conflict_options, scratch_pool));
      SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "\n%s\n", helpstr));
      *opt = NULL;
    }
  else
    {
      *opt = find_option(conflict_options, answer);
      if (! *opt)
        {
          SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                      _("Unrecognized option.\n\n")));
        }
    }
  return SVN_NO_ERROR;
}

/* Set *OPTIONS to an array of resolution options for CONFLICT. */
static svn_error_t *
build_text_conflict_options(apr_array_header_t **options,
                            svn_client_conflict_t *conflict,
                            svn_client_ctx_t *ctx,
                            svn_boolean_t is_binary,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const client_option_t *o;
  apr_array_header_t *builtin_options;
  int nopt;
  int i;
  apr_pool_t *iterpool;

  SVN_ERR(svn_client_conflict_text_get_resolution_options(&builtin_options,
                                                          conflict, ctx,
                                                          scratch_pool,
                                                          scratch_pool));
  nopt = builtin_options->nelts + ARRAY_LEN(extra_resolver_options);
  if (!is_binary)
    nopt += ARRAY_LEN(extra_resolver_options_text);
  *options = apr_array_make(result_pool, nopt, sizeof(client_option_t *));

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < builtin_options->nelts; i++)
    {
      client_option_t *opt;
      svn_client_conflict_option_t *builtin_option;

      svn_pool_clear(iterpool);
      builtin_option = APR_ARRAY_IDX(builtin_options, i,
                                     svn_client_conflict_option_t *);
      SVN_ERR(find_option_by_builtin(&opt, conflict,
                                     builtin_resolver_options,
                                     builtin_option,
                                     result_pool,
                                     iterpool));
      if (opt == NULL)
        continue; /* ### unknown option -- assign a code dynamically? */

      APR_ARRAY_PUSH(*options, client_option_t *) = opt;
    }

  for (o = extra_resolver_options; o->code; o++)
    APR_ARRAY_PUSH(*options, const client_option_t *) = o;
  if (!is_binary)
    {
      for (o = extra_resolver_options_text; o->code; o++)
        APR_ARRAY_PUSH(*options, const client_option_t *) = o;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Mark CONFLICT as resolved to resolution option with ID OPTION_ID.
 * If TEXT_CONFLICTED is true, resolve text conflicts described by CONFLICT.
 * IF PROPNAME is not NULL, mark the conflict in the specified property as
 * resolved. If PROPNAME is "", mark all property conflicts described by
 * CONFLICT as resolved.
 * If TREE_CONFLICTED is true, resolve tree conflicts described by CONFLICT.
 * Adjust CONFLICT_STATS as necessary (PATH_PREFIX is needed for this step). */
static svn_error_t *
mark_conflict_resolved(svn_client_conflict_t *conflict,
                       svn_client_conflict_option_id_t option_id,
                       svn_boolean_t text_conflicted,
                       const char *propname,
                       svn_boolean_t tree_conflicted,
                       const char *path_prefix,
                       svn_cl__conflict_stats_t *conflict_stats,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool)
{
  const char *local_relpath
    = svn_cl__local_style_skip_ancestor(
        path_prefix, svn_client_conflict_get_local_abspath(conflict),
        scratch_pool);

  if (text_conflicted)
    {
      SVN_ERR(svn_client_conflict_text_resolve_by_id(conflict, option_id,
                                                     ctx, scratch_pool));
      svn_cl__conflict_stats_resolved(conflict_stats, local_relpath,
                                      svn_wc_conflict_kind_text);
    }

  if (propname)
    {
      SVN_ERR(svn_client_conflict_prop_resolve_by_id(conflict, propname,
                                                     option_id, ctx,
                                                     scratch_pool));
      svn_cl__conflict_stats_resolved(conflict_stats, local_relpath,
                                      svn_wc_conflict_kind_property);
    }

  if (tree_conflicted)
    {
      SVN_ERR(svn_client_conflict_tree_resolve_by_id(conflict, option_id,
                                                     ctx, scratch_pool));
      svn_cl__conflict_stats_resolved(conflict_stats, local_relpath,
                                      svn_wc_conflict_kind_tree);
    }

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the text conflict described by CONFLICT
 * and either resolve the conflict accordingly or postpone resolution.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_text_conflict(svn_boolean_t *resolved,
                     svn_boolean_t *postponed,
                     svn_boolean_t *quit,
                     svn_boolean_t *printed_description,
                     svn_client_conflict_t *conflict,
                     const char *path_prefix,
                     svn_cmdline_prompt_baton_t *pb,
                     const char *editor_cmd,
                     apr_hash_t *config,
                     svn_cl__conflict_stats_t *conflict_stats,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t diff_allowed = FALSE;
  /* Have they done something that might have affected the merged file? */
  svn_boolean_t performed_edit = FALSE;
  /* Have they done *something* (edit, look at diff, etc) to
     give them a rational basis for choosing (r)esolved? */
  svn_boolean_t knows_something = FALSE;
  const char *local_relpath;
  const char *local_abspath = svn_client_conflict_get_local_abspath(conflict);
  const char *mime_type = svn_client_conflict_text_get_mime_type(conflict);
  svn_boolean_t is_binary = mime_type ? svn_mime_type_is_binary(mime_type)
                                      : FALSE;
  const char *base_abspath;
  const char *my_abspath;
  const char *their_abspath;
  const char *merged_abspath = svn_client_conflict_get_local_abspath(conflict);
  apr_array_header_t *text_conflict_options;
  svn_client_conflict_option_id_t option_id; 

  option_id = svn_client_conflict_option_unspecified;

  SVN_ERR(svn_client_conflict_text_get_contents(NULL, &my_abspath,
                                                &base_abspath, &their_abspath,
                                                conflict, scratch_pool,
                                                scratch_pool));

  local_relpath = svn_cl__local_style_skip_ancestor(path_prefix,
                                                    local_abspath,
                                                    scratch_pool);

  if (!*printed_description)
    {
      if (is_binary)
        SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                    _("Merge conflict discovered in binary "
                                      "file '%s'.\n"),
                                    local_relpath));
      else
        SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                    _("Merge conflict discovered in file '%s'.\n"),
                                    local_relpath));
      *printed_description = TRUE;
    }

  /* ### TODO This whole feature availability check is grossly outdated.
     DIFF_ALLOWED needs either to be redefined or to go away.
   */

  /* Diffing can happen between base and merged, to show conflict
     markers to the user (this is the typical 3-way merge
     scenario), or if no base is available, we can show a diff
     between mine and theirs. */
  if (!is_binary &&
      ((merged_abspath && base_abspath)
      || (!base_abspath && my_abspath && their_abspath)))
    diff_allowed = TRUE;

  SVN_ERR(build_text_conflict_options(&text_conflict_options, conflict, ctx,
                                      is_binary, scratch_pool, scratch_pool));
  while (TRUE)
    {
      const char *suggested_options[9]; /* filled statically below */
      const char **next_option = suggested_options;
      const client_option_t *opt;

      svn_pool_clear(iterpool);

      *next_option++ = "p";
      if (diff_allowed)
        {
          /* We need one more path for this feature. */
          if (my_abspath)
            *next_option++ = "df";

          *next_option++ = "e";

          /* We need one more path for this feature. */
          if (my_abspath)
            *next_option++ = "m";

          if (knows_something)
            *next_option++ = "r";
        }
      else
        {
          if (knows_something || is_binary)
            *next_option++ = "r";

          /* The 'mine-full' option selects the ".mine" file for texts or
           * the current working directory file for binary files. */
          if (my_abspath || is_binary)
            *next_option++ = "mf";

          *next_option++ = "tf";
        }
      *next_option++ = "s";
      *next_option++ = NULL;

      SVN_ERR(prompt_user(&opt, text_conflict_options, suggested_options,
                          NULL, pb, iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          option_id = opt->choice;
          *quit = TRUE;
          break;
        }
      else if (strcmp(opt->code, "s") == 0)
        {
          const char *helpstr;

          SVN_ERR(help_string(&helpstr, text_conflict_options, iterpool));
          SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "\n%s\n",
                                      helpstr));
        }
      else if (strcmp(opt->code, "dc") == 0)
        {
          if (is_binary)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                          _("Invalid option; cannot "
                                            "display conflicts for a "
                                            "binary file.\n\n")));
              continue;
            }
          else if (! (my_abspath && base_abspath && their_abspath))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                          _("Invalid option; original "
                                            "files not available.\n\n")));
              continue;
            }
          SVN_ERR(show_conflicts(conflict,
                                 pb->cancel_func,
                                 pb->cancel_baton,
                                 iterpool));
          knows_something = TRUE;
        }
      else if (strcmp(opt->code, "df") == 0)
        {
          /* Re-check preconditions. */
          if (! diff_allowed || ! my_abspath)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                             _("Invalid option; there's no "
                                "merged version to diff.\n\n")));
              continue;
            }

          SVN_ERR(show_diff(conflict, merged_abspath, path_prefix,
                            pb->cancel_func, pb->cancel_baton,
                            iterpool));
          knows_something = TRUE;
        }
      else if (strcmp(opt->code, "e") == 0 || strcmp(opt->code, ":-E") == 0)
        {
          SVN_ERR(open_editor(&performed_edit, merged_abspath, editor_cmd,
                              config, iterpool));
          if (performed_edit)
            knows_something = TRUE;
        }
      else if (strcmp(opt->code, "m") == 0 || strcmp(opt->code, ":-g") == 0 ||
               strcmp(opt->code, "=>-") == 0 || strcmp(opt->code, ":>.") == 0)
        {
          svn_error_t *err;

          /* Re-check preconditions. */
          if (! my_abspath)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                             _("Invalid option; there's no "
                                "base path to merge.\n\n")));
              continue;
            }

          err = svn_cl__merge_file_externally(base_abspath,
                                              their_abspath,
                                              my_abspath,
                                              merged_abspath,
                                              local_abspath, config,
                                              NULL, iterpool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL)
                {
                  svn_boolean_t remains_in_conflict = TRUE;

                  /* Try the internal merge tool. */
                  svn_error_clear(err);
                  SVN_ERR(svn_cl__merge_file(&remains_in_conflict,
                                             base_abspath,
                                             their_abspath,
                                             my_abspath,
                                             merged_abspath,
                                             local_abspath,
                                             path_prefix,
                                             editor_cmd,
                                             config,
                                             pb->cancel_func,
                                             pb->cancel_baton,
                                             iterpool));
                  knows_something = !remains_in_conflict;
                }
              else if (err->apr_err == SVN_ERR_EXTERNAL_PROGRAM)
                {
                  char buf[1024];
                  const char *message;

                  message = svn_err_best_message(err, buf, sizeof(buf));
                  SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                              "%s\n", message));
                  svn_error_clear(err);
                  continue;
                }
              else
                return svn_error_trace(err);
            }
          else
            {
              /* The external merge tool's exit code was either 0 or 1.
               * The tool may leave the file conflicted by exiting with
               * exit code 1, and we allow the user to mark the conflict
               * resolved in this case. */
              performed_edit = TRUE;
              knows_something = TRUE;
            }
        }
      else if (strcmp(opt->code, "l") == 0 || strcmp(opt->code, ":-l") == 0)
        {
          /* ### This check should be earlier as it's nasty to offer an option
           *     and then when the user chooses it say 'Invalid option'. */
          /* ### 'merged_abspath' shouldn't be necessary *before* we launch the
           *     resolver: it should be the *result* of doing so. */
          if (base_abspath && their_abspath && my_abspath && merged_abspath)
            {
              svn_error_t *err;
              char buf[1024];
              const char *message;

              err = svn_cl__merge_file_externally(base_abspath,
                                                  their_abspath,
                                                  my_abspath,
                                                  merged_abspath,
                                                  local_abspath,
                                                  config, NULL, iterpool);
              if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL ||
                          err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
                {
                  message = svn_err_best_message(err, buf, sizeof(buf));
                  SVN_ERR(svn_cmdline_fprintf(stderr, iterpool, "%s\n",
                                              message));
                  svn_error_clear(err);
                }
              else if (err)
                return svn_error_trace(err);
              else
                performed_edit = TRUE;

              if (performed_edit)
                knows_something = TRUE;
            }
          else
            SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                        _("Invalid option.\n\n")));
        }
      else if (strcmp(opt->code, "i") == 0)
        {
          svn_boolean_t remains_in_conflict = TRUE;

          SVN_ERR(svn_cl__merge_file(&remains_in_conflict,
                                     base_abspath,
                                     their_abspath,
                                     my_abspath,
                                     merged_abspath,
                                     local_abspath,
                                     path_prefix,
                                     editor_cmd,
                                     config,
                                     pb->cancel_func,
                                     pb->cancel_baton,
                                     iterpool));

          if (!remains_in_conflict)
            knows_something = TRUE;
        }
      else if (opt->choice != svn_client_conflict_option_undefined)
        {
          if ((opt->choice == svn_client_conflict_option_working_text_where_conflicted
               || opt->choice == svn_client_conflict_option_incoming_text_where_conflicted)
              && is_binary)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                          _("Invalid option; cannot choose "
                                            "based on conflicts in a "
                                            "binary file.\n\n")));
              continue;
            }

          /* We only allow the user accept the merged version of
             the file if they've edited it, or at least looked at
             the diff. */
          if (opt->choice == svn_client_conflict_option_merged_text
              && ! knows_something && diff_allowed)
            {
              SVN_ERR(svn_cmdline_fprintf(
                        stderr, iterpool,
                        _("Invalid option; use diff/edit/merge/launch "
                          "before choosing 'mark resolved'.\n\n")));
              continue;
            }

          option_id = opt->choice;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  if (option_id != svn_client_conflict_option_unspecified &&
      option_id != svn_client_conflict_option_postpone)
    {
      SVN_ERR(mark_conflict_resolved(conflict, option_id,
                                     TRUE, NULL, FALSE,
                                     path_prefix, conflict_stats,
                                     ctx, scratch_pool));
      *resolved = TRUE;
    }
  else
    {
      *resolved = FALSE;
      *postponed = (option_id == svn_client_conflict_option_postpone);
    }

  return SVN_NO_ERROR;
}

/* Set *OPTIONS to an array of resolution options for CONFLICT. */
static svn_error_t *
build_prop_conflict_options(apr_array_header_t **options,
                            svn_client_conflict_t *conflict,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const client_option_t *o;
  apr_array_header_t *builtin_options;
  int nopt;
  int i;
  apr_pool_t *iterpool;

  SVN_ERR(svn_client_conflict_prop_get_resolution_options(&builtin_options,
                                                          conflict, ctx,
                                                          scratch_pool,
                                                          scratch_pool));
  nopt = builtin_options->nelts + ARRAY_LEN(extra_resolver_options) +
           ARRAY_LEN(extra_resolver_options_prop);
  *options = apr_array_make(result_pool, nopt, sizeof(client_option_t *));

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < builtin_options->nelts; i++)
    {
      client_option_t *opt;
      svn_client_conflict_option_t *builtin_option;

      svn_pool_clear(iterpool);
      builtin_option = APR_ARRAY_IDX(builtin_options, i,
                                     svn_client_conflict_option_t *);
      SVN_ERR(find_option_by_builtin(&opt, conflict,
                                     builtin_resolver_options,
                                     builtin_option,
                                     result_pool,
                                     iterpool));
      if (opt == NULL)
        continue; /* ### unknown option -- assign a code dynamically? */

      APR_ARRAY_PUSH(*options, client_option_t *) = opt;
    }

  svn_pool_destroy(iterpool);

  for (o = extra_resolver_options; o->code; o++)
    APR_ARRAY_PUSH(*options, const client_option_t *) = o;
  for (o = extra_resolver_options_prop; o->code; o++)
    APR_ARRAY_PUSH(*options, const client_option_t *) = o;

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the conflicted property PROPNAME described
 * by CONFLICT and return the corresponding resolution option in *OPTION.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_one_prop_conflict(svn_client_conflict_option_t **option,
                         svn_boolean_t *quit,
                         const char *path_prefix,
                         svn_cmdline_prompt_baton_t *pb,
                         const char *editor_cmd,
                         apr_hash_t *config,
                         svn_client_conflict_t *conflict,
                         const char *propname,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  const char *description;
  const svn_string_t *merged_propval = NULL;
  svn_boolean_t resolved_allowed = FALSE;
  const svn_string_t *base_propval;
  const svn_string_t *my_propval;
  const svn_string_t *their_propval;
  apr_array_header_t *resolution_options;
  apr_array_header_t *prop_conflict_options;

  SVN_ERR(svn_client_conflict_prop_get_propvals(NULL, &my_propval,
                                                &base_propval, &their_propval,
                                                conflict, propname,
                                                scratch_pool));

  SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                              _("Conflict for property '%s' discovered"
                                " on '%s'.\n"),
                              propname,
                              svn_cl__local_style_skip_ancestor(
                                path_prefix,
                                svn_client_conflict_get_local_abspath(conflict),
                                scratch_pool)));
  SVN_ERR(svn_client_conflict_prop_get_description(&description, conflict,
                                                   scratch_pool, scratch_pool));
  SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n", description));

  SVN_ERR(svn_client_conflict_prop_get_resolution_options(&resolution_options,
                                                          conflict, ctx,
                                                          result_pool,
                                                          scratch_pool));
  SVN_ERR(build_prop_conflict_options(&prop_conflict_options, conflict, ctx,
                                      scratch_pool, scratch_pool));
  iterpool = svn_pool_create(scratch_pool);
  while (TRUE)
    {
      const client_option_t *opt;
      const char *suggested_options[9]; /* filled statically below */
      const char **next_option = suggested_options;

      *next_option++ = "p";
      *next_option++ = "mf";
      *next_option++ = "tf";
      *next_option++ = "dc";
      *next_option++ = "e";
      if (resolved_allowed)
        *next_option++ = "r";
      *next_option++ = "q";
      *next_option++ = "h";
      *next_option++ = NULL;

      svn_pool_clear(iterpool);

      SVN_ERR(prompt_user(&opt, prop_conflict_options, suggested_options,
                          NULL, pb, iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          *option = svn_client_conflict_option_find_by_id(resolution_options,
                                                          opt->choice);
          *quit = TRUE;
          break;
        }
      else if (strcmp(opt->code, "dc") == 0)
        {
          SVN_ERR(show_prop_conflict(base_propval, my_propval, their_propval,
                                     merged_propval,
                                     pb->cancel_func, pb->cancel_baton,
                                     scratch_pool));
        }
      else if (strcmp(opt->code, "e") == 0)
        {
          SVN_ERR(edit_prop_conflict(&merged_propval,
                                     base_propval, my_propval, their_propval,
                                     editor_cmd, config, pb,
                                     result_pool, scratch_pool));
          resolved_allowed = (merged_propval != NULL);
        }
      else if (strcmp(opt->code, "r") == 0)
        {
          if (! resolved_allowed)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                             _("Invalid option; please edit the property "
                               "first.\n\n")));
              continue;
            }

          *option = svn_client_conflict_option_find_by_id(
                      resolution_options,
                      svn_client_conflict_option_merged_text);
          svn_client_conflict_option_set_merged_propval(*option,
                                                        merged_propval);
          break;
        }
      else if (opt->choice != svn_client_conflict_option_undefined)
        {
          *option = svn_client_conflict_option_find_by_id(resolution_options,
                                                          opt->choice);
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the property conflicts described by CONFLICT
 * and either resolve them accordingly or postpone resolution.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_prop_conflicts(svn_boolean_t *resolved,
                      svn_boolean_t *postponed,
                      svn_boolean_t *quit,
                      const svn_string_t **merged_value,
                      const char *path_prefix,
                      svn_cmdline_prompt_baton_t *pb,
                      const char *editor_cmd,
                      apr_hash_t *config,
                      svn_client_conflict_t *conflict,
                      svn_cl__conflict_stats_t *conflict_stats,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *props_conflicted;
  apr_pool_t *iterpool;
  int i;
  int nresolved = 0;

  SVN_ERR(svn_client_conflict_get_conflicted(NULL, &props_conflicted, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < props_conflicted->nelts; i++)
    {
      const char *propname = APR_ARRAY_IDX(props_conflicted, i, const char *);
      svn_client_conflict_option_t *option;
      svn_client_conflict_option_id_t option_id;

      svn_pool_clear(iterpool);

      SVN_ERR(handle_one_prop_conflict(&option, quit, path_prefix, pb,
                                       editor_cmd, config, conflict, propname,
                                       ctx,
                                       iterpool, iterpool));
      option_id = svn_client_conflict_option_get_id(option);

      if (option_id != svn_client_conflict_option_unspecified &&
          option_id != svn_client_conflict_option_postpone)
        {
          const char *local_relpath =
            svn_cl__local_style_skip_ancestor(
              path_prefix, svn_client_conflict_get_local_abspath(conflict),
              iterpool);

          SVN_ERR(svn_client_conflict_prop_resolve(conflict, propname, option,
                                                   ctx, iterpool));
          svn_cl__conflict_stats_resolved(conflict_stats, local_relpath,
                                          svn_wc_conflict_kind_property);
          nresolved++;
          *postponed = FALSE;
        }
      else
        *postponed = (option_id == svn_client_conflict_option_postpone);

      if (*quit)
        break;
    }
  svn_pool_destroy(iterpool);

  /* Indicate success if no property conflicts remain. */
  *resolved = (nresolved == props_conflicted->nelts);

  return SVN_NO_ERROR;
}

/* Set *OPTIONS to an array of resolution options for CONFLICT. */
static svn_error_t *
build_tree_conflict_options(
  apr_array_header_t **options,
  apr_array_header_t **possible_moved_to_repos_relpaths,
  apr_array_header_t **possible_moved_to_abspaths,
  svn_boolean_t *all_options_are_dumb,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  const client_option_t *o;
  apr_array_header_t *builtin_options;
  int nopt;
  int i;
  int next_unknown_option_code = 1;
  apr_pool_t *iterpool;

  if (all_options_are_dumb != NULL)
    *all_options_are_dumb = TRUE;

  SVN_ERR(svn_client_conflict_tree_get_resolution_options(&builtin_options,
                                                          conflict, ctx,
                                                          scratch_pool,
                                                          scratch_pool));
  nopt = builtin_options->nelts + ARRAY_LEN(extra_resolver_options_tree) +
           ARRAY_LEN(extra_resolver_options);
  *options = apr_array_make(result_pool, nopt, sizeof(client_option_t *));
  *possible_moved_to_abspaths = NULL;
  *possible_moved_to_repos_relpaths = NULL;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < builtin_options->nelts; i++)
    {
      client_option_t *opt;
      svn_client_conflict_option_t *builtin_option;
      svn_client_conflict_option_id_t id;

      svn_pool_clear(iterpool);
      builtin_option = APR_ARRAY_IDX(builtin_options, i,
                                     svn_client_conflict_option_t *);
      SVN_ERR(find_option_by_builtin(&opt, conflict,
                                     builtin_resolver_options,
                                     builtin_option,
                                     result_pool,
                                     iterpool));
      if (opt == NULL)
        {
          /* Unkown option. Assign a dynamic option code. */
          opt = apr_pcalloc(result_pool, sizeof(*opt));
          opt->code = apr_psprintf(result_pool, "%d", next_unknown_option_code);
          next_unknown_option_code++;
          opt->label = svn_client_conflict_option_get_label(builtin_option,
                                                            result_pool);
          opt->long_desc = svn_client_conflict_option_get_description(
                             builtin_option, result_pool);
          opt->choice = svn_client_conflict_option_get_id(builtin_option);
          opt->accept_arg = NULL;
        }

      APR_ARRAY_PUSH(*options, client_option_t *) = opt;

      id = svn_client_conflict_option_get_id(builtin_option);

      /* Check if we got a "smart" tree conflict option. */
      if (all_options_are_dumb != NULL &&
          *all_options_are_dumb &&
          id != svn_client_conflict_option_postpone &&
          id != svn_client_conflict_option_accept_current_wc_state)
        *all_options_are_dumb = FALSE;

      if (id == svn_client_conflict_option_incoming_move_file_text_merge ||
          id == svn_client_conflict_option_incoming_move_dir_merge)
        {
          SVN_ERR(
            svn_client_conflict_option_get_moved_to_repos_relpath_candidates(
              possible_moved_to_repos_relpaths, builtin_option,
              result_pool, iterpool));
          SVN_ERR(svn_client_conflict_option_get_moved_to_abspath_candidates(
                    possible_moved_to_abspaths, builtin_option,
                    result_pool, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  for (o = extra_resolver_options_tree; o->code; o++)
    {
      /* Add move target choice options only if there are multiple
       * move targets to choose from. */
      if (strcmp(o->code, "d") == 0 &&
          (*possible_moved_to_repos_relpaths == NULL || 
           (*possible_moved_to_repos_relpaths)->nelts <= 1))
        continue;
      if (strcmp(o->code, "w") == 0 &&
          (*possible_moved_to_abspaths == NULL || 
           (*possible_moved_to_abspaths)->nelts <= 1))
        continue;

      APR_ARRAY_PUSH(*options, const client_option_t *) = o;
    }
  for (o = extra_resolver_options; o->code; o++)
    APR_ARRAY_PUSH(*options, const client_option_t *) = o;

  return SVN_NO_ERROR;
}

/* Make the user select a move target path for the moved-away VICTIM_ABSPATH. */
static svn_error_t *
prompt_move_target_path(int *preferred_move_target_idx,
                        apr_array_header_t *possible_moved_to_paths,
                        svn_boolean_t paths_are_local,
                        svn_cmdline_prompt_baton_t *pb,
                        const char *victim_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  const char *move_targets_prompt = "";
  const char *move_targets_list = "";
  const char *wcroot_abspath;
  const char *victim_relpath;
  int i;
  apr_int64_t idx;
  apr_pool_t *iterpool;

  SVN_ERR(svn_client_get_wc_root(&wcroot_abspath, victim_abspath,
                                 ctx, scratch_pool, scratch_pool));
  victim_relpath = svn_cl__local_style_skip_ancestor(wcroot_abspath,
                                                     victim_abspath,
                                                     scratch_pool),
  iterpool = svn_pool_create(scratch_pool);

  /* Build the prompt. */
  for (i = 0; i < possible_moved_to_paths->nelts; i++)
    {
      svn_pool_clear(iterpool);

      if (paths_are_local)
        {
          const char *moved_to_abspath;
          const char *moved_to_relpath;

          moved_to_abspath = APR_ARRAY_IDX(possible_moved_to_paths, i,
                                           const char *);
          moved_to_relpath = svn_cl__local_style_skip_ancestor(
                               wcroot_abspath, moved_to_abspath, iterpool),
          move_targets_list = apr_psprintf(scratch_pool, "%s (%d): '%s'\n",
                                           move_targets_list, i + 1,
                                           moved_to_relpath);
        }
      else
        {
          const char *moved_to_repos_relpath;

          moved_to_repos_relpath = APR_ARRAY_IDX(possible_moved_to_paths, i,
                                                 const char *);
          move_targets_list = apr_psprintf(scratch_pool, "%s (%d): '^/%s'\n",
                                           move_targets_list, i + 1,
                                           moved_to_repos_relpath);
        }
    }
  if (paths_are_local)
    move_targets_prompt =
      apr_psprintf(scratch_pool,
                   _("Possible working copy destinations for moved-away '%s' "
                     "are:\n%s"
                     "Only one destination can be a move; the others are "
                     "copies.\n"
                     "Specify the correct move target path by number: "),
                   victim_relpath, move_targets_list);
  else
    move_targets_prompt =
      apr_psprintf(scratch_pool,
                   _("Possible repository destinations for moved-away '%s' "
                     "are:\n%s"
                     "Only one destination can be a move; the others are "
                     "copies.\n"
                     "Specify the correct move target path by number: "),
                   victim_relpath, move_targets_list);

  /* Keep asking the user until we got a valid choice. */
  while (1)
    {
      const char *answer;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_cmdline_prompt_user2(&answer, move_targets_prompt,
                                       pb, iterpool));
      err = svn_cstring_strtoi64(&idx, answer, 1,
                                 possible_moved_to_paths->nelts, 10);
      if (err)
        {
          char buf[1024];

          svn_cmdline_fprintf(stderr, iterpool, "%s\n",
                              svn_err_best_message(err, buf, sizeof(buf)));
          svn_error_clear(err);
          continue;
        }

      break;
    }

  svn_pool_destroy(iterpool);

  SVN_ERR_ASSERT((idx - 1) == (int)(idx - 1));
  *preferred_move_target_idx = (int)(idx - 1);
  return SVN_NO_ERROR;
}

/* Ask the user what to do about the tree conflict described by CONFLICT
 * and either resolve the conflict accordingly or postpone resolution.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_tree_conflict(svn_boolean_t *resolved,
                     svn_boolean_t *postponed,
                     svn_boolean_t *quit,
                     svn_boolean_t *printed_description,
                     svn_client_conflict_t *conflict,
                     const char *path_prefix,
                     svn_cmdline_prompt_baton_t *pb,
                     svn_cl__conflict_stats_t *conflict_stats,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  apr_array_header_t *tree_conflict_options;
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;
  const char *conflict_description;
  const char *local_change_description;
  const char *incoming_change_description;
  apr_array_header_t *possible_moved_to_repos_relpaths;
  apr_array_header_t *possible_moved_to_abspaths;
  svn_boolean_t all_options_are_dumb;
  const struct client_option_t *recommended_option;
  svn_boolean_t repos_move_target_chosen = FALSE;
  svn_boolean_t wc_move_target_chosen = FALSE;

  option_id = svn_client_conflict_option_unspecified;
  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  /* Always show the best possible conflict description and options. */
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, scratch_pool));

  SVN_ERR(svn_client_conflict_tree_get_description(
           &incoming_change_description, &local_change_description,
           conflict, ctx, scratch_pool, scratch_pool));
  conflict_description = apr_psprintf(scratch_pool, "%s\n%s",
                                      incoming_change_description,
                                      local_change_description);
  if (!*printed_description)
    SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                _("Tree conflict on '%s':\n%s\n"),
                                svn_cl__local_style_skip_ancestor(
                                  path_prefix, local_abspath, scratch_pool),
                                conflict_description));

  SVN_ERR(build_tree_conflict_options(&tree_conflict_options,
                                      &possible_moved_to_repos_relpaths,
                                      &possible_moved_to_abspaths,
                                      &all_options_are_dumb,
                                      conflict, ctx,
                                      scratch_pool, scratch_pool));

  /* Try a recommended resolution option before prompting. */
  recommended_option = find_recommended_option(tree_conflict_options);
  if (recommended_option)
    {
      svn_error_t *err;
      apr_status_t root_cause;

      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("Applying recommended resolution '%s':\n"),
                                 recommended_option->label));

      err = mark_conflict_resolved(conflict, recommended_option->choice,
                                   FALSE, NULL, TRUE,
                                   path_prefix, conflict_stats,
                                   ctx, scratch_pool);
      if (!err)
        {
          *resolved = TRUE;
          return SVN_NO_ERROR;
        }

      root_cause = svn_error_root_cause(err)->apr_err;
      if (root_cause != SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE &&
          root_cause != SVN_ERR_WC_OBSTRUCTED_UPDATE &&
          root_cause != SVN_ERR_WC_FOUND_CONFLICT)
        return svn_error_trace(err);

      /* Fall back to interactive prompting. */
      svn_error_clear(err);
    }

  if (all_options_are_dumb)
    SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                _("\nSubversion is not smart enough to resolve "
                                  "this tree conflict automatically!\nSee 'svn "
                                  "help resolve' for more information.\n\n")));

  iterpool = svn_pool_create(scratch_pool);
  while (1)
    {
      const client_option_t *opt;

      svn_pool_clear(iterpool);

      if (!repos_move_target_chosen &&
          possible_moved_to_repos_relpaths &&
          possible_moved_to_repos_relpaths->nelts > 1)
        SVN_ERR(svn_cmdline_printf(scratch_pool,
                  _("Ambiguous move destinations exist in the repository; "
                    "try the 'd' option\n")));
      if (!wc_move_target_chosen && possible_moved_to_abspaths &&
          possible_moved_to_abspaths->nelts > 1)
        SVN_ERR(svn_cmdline_printf(scratch_pool,
                  _("Ambiguous move destinations exist in the working copy; "
                    "try the 'w' option\n")));

      SVN_ERR(prompt_user(&opt, tree_conflict_options, NULL,
                          conflict_description, pb, iterpool));
      *printed_description = TRUE;
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          option_id = opt->choice;
          *quit = TRUE;
          break;
        }
      else if (strcmp(opt->code, "d") == 0)
        {
          int preferred_move_target_idx;
          apr_array_header_t *options;
          svn_client_conflict_option_t *conflict_option;

          SVN_ERR(prompt_move_target_path(&preferred_move_target_idx,
                                          possible_moved_to_repos_relpaths,
                                          FALSE,
                                          pb, local_abspath, ctx, iterpool));

          /* Update preferred move target path. */
          SVN_ERR(svn_client_conflict_tree_get_resolution_options(&options,
                                                                  conflict,
                                                                  ctx,
                                                                  iterpool,
                                                                  iterpool));
          conflict_option =
            svn_client_conflict_option_find_by_id( 
              options,
              svn_client_conflict_option_incoming_move_file_text_merge);
          if (conflict_option == NULL)
            {
              conflict_option =
                svn_client_conflict_option_find_by_id( 
                  options, svn_client_conflict_option_incoming_move_dir_merge);
            }

          if (conflict_option)
            {
              SVN_ERR(svn_client_conflict_option_set_moved_to_repos_relpath(
                        conflict_option, preferred_move_target_idx,
                        ctx, iterpool));
              repos_move_target_chosen = TRUE;
              wc_move_target_chosen = FALSE;

              /* Update option description. */
              SVN_ERR(build_tree_conflict_options(
                        &tree_conflict_options,
                        &possible_moved_to_repos_relpaths,
                        &possible_moved_to_abspaths,
                        NULL, conflict, ctx,
                        scratch_pool, scratch_pool));

              /* Update conflict description. */
              SVN_ERR(svn_client_conflict_tree_get_description(
                       &incoming_change_description, &local_change_description,
                       conflict, ctx, scratch_pool, scratch_pool));
              conflict_description = apr_psprintf(scratch_pool, "%s\n%s",
                                                  incoming_change_description,
                                                  local_change_description);
            }
          continue;
        }
      else if (strcmp(opt->code, "w") == 0)
        {
          int preferred_move_target_idx;
          apr_array_header_t *options;
          svn_client_conflict_option_t *conflict_option;

          SVN_ERR(prompt_move_target_path(&preferred_move_target_idx,
                                           possible_moved_to_abspaths, TRUE,
                                           pb, local_abspath, ctx, iterpool));

          /* Update preferred move target path. */
          SVN_ERR(svn_client_conflict_tree_get_resolution_options(&options,
                                                                  conflict,
                                                                  ctx,
                                                                  iterpool,
                                                                  iterpool));
          conflict_option =
            svn_client_conflict_option_find_by_id( 
              options,
              svn_client_conflict_option_incoming_move_file_text_merge);
          if (conflict_option == NULL)
            {
              conflict_option =
                svn_client_conflict_option_find_by_id( 
                  options, svn_client_conflict_option_incoming_move_dir_merge);
            }

          if (conflict_option)
            {
              SVN_ERR(svn_client_conflict_option_set_moved_to_abspath(
                        conflict_option, preferred_move_target_idx, ctx,
                        iterpool));
              wc_move_target_chosen = TRUE;

              /* Update option description. */
              SVN_ERR(build_tree_conflict_options(
                        &tree_conflict_options,
                        &possible_moved_to_repos_relpaths,
                        &possible_moved_to_abspaths,
                        NULL, conflict, ctx,
                        scratch_pool, scratch_pool));
            }
          continue;
        }
      else if (opt->choice != svn_client_conflict_option_undefined)
        {
          option_id = opt->choice;
          break;
        }
    }
  svn_pool_destroy(iterpool);
  if (option_id != svn_client_conflict_option_unspecified &&
      option_id != svn_client_conflict_option_postpone)
    {
      SVN_ERR(mark_conflict_resolved(conflict, option_id,
                                     FALSE, NULL, TRUE,
                                     path_prefix, conflict_stats,
                                     ctx, scratch_pool));
      *resolved = TRUE;
    }
  else
    {
      *resolved = FALSE;
      *postponed = (option_id == svn_client_conflict_option_postpone);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
resolve_conflict_interactively(svn_boolean_t *resolved,
                               svn_boolean_t *postponed,
                               svn_boolean_t *quit,
                               svn_boolean_t *external_failed,
                               svn_boolean_t *printed_summary,
                               svn_boolean_t *printed_description,
                               svn_client_conflict_t *conflict,
                               const char *editor_cmd,
                               apr_hash_t *config,
                               const char *path_prefix,
                               svn_cmdline_prompt_baton_t *pb,
                               svn_cl__conflict_stats_t *conflict_stats,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  const svn_string_t *merged_propval;

  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict,
                                             scratch_pool,
                                             scratch_pool));

  /* Print a summary of conflicts before starting interactive resolution */
  if (! *printed_summary)
    {
      SVN_ERR(svn_cl__print_conflict_stats(conflict_stats, scratch_pool));
      *printed_summary = TRUE;
    }

  *resolved = FALSE;
  if (text_conflicted
       && (svn_client_conflict_get_incoming_change(conflict) ==
           svn_wc_conflict_action_edit)
       && (svn_client_conflict_get_local_change(conflict) ==
           svn_wc_conflict_reason_edited))
    SVN_ERR(handle_text_conflict(resolved, postponed, quit, printed_description,
                                 conflict, path_prefix, pb, editor_cmd, config,
                                 conflict_stats, ctx, scratch_pool));
  if (props_conflicted->nelts > 0)
    SVN_ERR(handle_prop_conflicts(resolved, postponed, quit, &merged_propval,
                                  path_prefix, pb, editor_cmd, config, conflict,
                                  conflict_stats, ctx, result_pool, scratch_pool));
  if (tree_conflicted)
    SVN_ERR(handle_tree_conflict(resolved, postponed, quit, printed_description,
                                 conflict, path_prefix, pb, conflict_stats, ctx,
                                 scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__resolve_conflict(svn_boolean_t *quit,
                         svn_boolean_t *external_failed,
                         svn_boolean_t *printed_summary,
                         svn_client_conflict_t *conflict,
                         svn_cl__accept_t accept_which,
                         const char *editor_cmd,
                         const char *path_prefix,
                         svn_cmdline_prompt_baton_t *pb,
                         svn_cl__conflict_stats_t *conflict_stats,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  const char *local_abspath;
  svn_client_conflict_option_id_t option_id;

  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict,
                                             scratch_pool,
                                             scratch_pool));
  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  if (accept_which == svn_cl__accept_unspecified)
    {
      option_id = svn_client_conflict_option_unspecified;
    }
  else if (accept_which == svn_cl__accept_postpone)
    {
      option_id = svn_client_conflict_option_postpone;
    }
  else if (accept_which == svn_cl__accept_base)
    {
      option_id = svn_client_conflict_option_base_text;
    }
  else if (accept_which == svn_cl__accept_working)
    {
      option_id = svn_client_conflict_option_merged_text;

      if (text_conflicted)
        {
          const char *mime_type =
            svn_client_conflict_text_get_mime_type(conflict);

          /* There is no merged text for binary conflicts, behave as
           * if 'mine-full' was chosen. */
          if (mime_type && svn_mime_type_is_binary(mime_type))
            option_id = svn_client_conflict_option_working_text;
        }
      else if (tree_conflicted)
        {
          /* For tree conflicts, map 'working' to 'accept current working
           * copy state'. */
          option_id = svn_client_conflict_option_accept_current_wc_state;
        }
    }
  else if (accept_which == svn_cl__accept_theirs_conflict)
    {
      option_id = svn_client_conflict_option_incoming_text_where_conflicted;
    }
  else if (accept_which == svn_cl__accept_mine_conflict)
    {
      option_id = svn_client_conflict_option_working_text_where_conflicted;

      if (tree_conflicted)
        {
          svn_wc_operation_t operation;

          operation = svn_client_conflict_get_operation(conflict);
          if (operation == svn_wc_operation_update ||
              operation == svn_wc_operation_switch)
            {
              svn_wc_conflict_reason_t reason;

              reason = svn_client_conflict_get_local_change(conflict);
              if (reason == svn_wc_conflict_reason_moved_away)
                {
                  /* Map 'mine-conflict' to 'update move destination'. */
                  option_id =
                    svn_client_conflict_option_update_move_destination;
                }
              else if (reason == svn_wc_conflict_reason_deleted ||
                       reason == svn_wc_conflict_reason_replaced)
                {
                  svn_wc_conflict_action_t action;
                  svn_node_kind_t node_kind;

                  action = svn_client_conflict_get_incoming_change(conflict);
                  node_kind =
                    svn_client_conflict_tree_get_victim_node_kind(conflict);

                  if (action == svn_wc_conflict_action_edit &&
                      node_kind == svn_node_dir)
                    {
                      /* Map 'mine-conflict' to 'update any moved away
                       * children'. */
                      option_id =
                        svn_client_conflict_option_update_any_moved_away_children;
                    }
                }
            }
        }
    }
  else if (accept_which == svn_cl__accept_theirs_full)
    {
      option_id = svn_client_conflict_option_incoming_text;
    }
  else if (accept_which == svn_cl__accept_mine_full)
    {
      option_id = svn_client_conflict_option_working_text;
    }
  else if (accept_which == svn_cl__accept_edit)
    {
      option_id = svn_client_conflict_option_unspecified;

      if (local_abspath)
        {
          if (*external_failed)
            {
              option_id = svn_client_conflict_option_postpone;
            }
          else
            {
              svn_error_t *err;

              err = svn_cmdline__edit_file_externally(local_abspath,
                                                      editor_cmd,
                                                      ctx->config,
                                                      scratch_pool);
              if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR ||
                          err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
                {
                  char buf[1024];
                  const char *message;

                  message = svn_err_best_message(err, buf, sizeof(buf));
                  SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                              message));
                  svn_error_clear(err);
                  *external_failed = TRUE;
                }
              else if (err)
                return svn_error_trace(err);
              option_id = svn_client_conflict_option_merged_text;
            }
        }
    }
  else if (accept_which == svn_cl__accept_launch)
    {
      const char *base_abspath = NULL;
      const char *my_abspath = NULL;
      const char *their_abspath = NULL;

      option_id = svn_client_conflict_option_unspecified;

      if (text_conflicted)
        SVN_ERR(svn_client_conflict_text_get_contents(NULL, &my_abspath,
                                                      &base_abspath,
                                                      &their_abspath,
                                                      conflict, scratch_pool,
                                                      scratch_pool));

      if (base_abspath && their_abspath && my_abspath && local_abspath)
        {
          if (*external_failed)
            {
              option_id = svn_client_conflict_option_postpone;
            }
          else
            {
              svn_boolean_t remains_in_conflict;
              svn_error_t *err;

              err = svn_cl__merge_file_externally(base_abspath, their_abspath,
                                                  my_abspath, local_abspath,
                                                  local_abspath, ctx->config,
                                                  &remains_in_conflict,
                                                  scratch_pool);
              if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL ||
                          err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
                {
                  char buf[1024];
                  const char *message;

                  message = svn_err_best_message(err, buf, sizeof(buf));
                  SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                              message));
                  *external_failed = TRUE;
                  return svn_error_trace(err);
                }
              else if (err)
                return svn_error_trace(err);

              if (remains_in_conflict)
                option_id = svn_client_conflict_option_postpone;
              else
                option_id = svn_client_conflict_option_merged_text;
            }
        }
    }
  else if (accept_which == svn_cl__accept_recommended)
    {
      svn_client_conflict_option_id_t recommended_id;

      if (tree_conflicted)
        SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx,
                                                     scratch_pool));
      recommended_id = svn_client_conflict_get_recommended_option_id(conflict);
      if (recommended_id != svn_client_conflict_option_unspecified)
        option_id = recommended_id;
      else
        option_id = svn_client_conflict_option_postpone;
    }
  else
    SVN_ERR_MALFUNCTION();

  /* If we are in interactive mode and either the user gave no --accept
   * option or the option did not apply, then prompt. */
  if (option_id == svn_client_conflict_option_unspecified)
    {
      svn_boolean_t resolved = FALSE;
      svn_boolean_t postponed = FALSE;
      svn_boolean_t printed_description = FALSE;
      svn_error_t *err;

      *quit = FALSE;

      while (!resolved && !postponed && !*quit)
        {
          err = resolve_conflict_interactively(&resolved, &postponed, quit,
                                               external_failed,
                                               printed_summary,
                                               &printed_description,
                                               conflict,
                                               editor_cmd, ctx->config,
                                               path_prefix, pb,
                                               conflict_stats, ctx,
                                               scratch_pool, scratch_pool);
          if (err && err->apr_err == SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE)
            {
              /* Conflict resolution has failed. Let the user try again.
               * It is always possible to break out of this loop with
               * the 'quit' or 'postpone' options. */
              svn_handle_warning2(stderr, err, "svn: ");
              svn_error_clear(err);
              err = SVN_NO_ERROR;
            }
          SVN_ERR(err);
        }
    }
  else if (option_id != svn_client_conflict_option_postpone)
    SVN_ERR(mark_conflict_resolved(conflict, option_id,
                                   text_conflicted,
                                   props_conflicted->nelts > 0 ? "" : NULL,
                                   tree_conflicted,
                                   path_prefix, conflict_stats,
                                   ctx, scratch_pool));

  return SVN_NO_ERROR;
}
