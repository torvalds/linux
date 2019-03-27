/*
 * util.c: Subversion command line client utility functions. Any
 * functions that need to be shared across subcommands should be put
 * in here.
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

#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <apr_env.h>
#include <apr_errno.h>
#include <apr_file_info.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_lib.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_ctype.h"
#include "svn_client.h"
#include "svn_cmdline.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_config.h"
#include "svn_wc.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_private_config.h"
#include "cl.h"

#include "private/svn_token.h"
#include "private/svn_opt_private.h"
#include "private/svn_client_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_string_private.h"
#ifdef HAS_ORGANIZATION_NAME
#include "freebsd-organization.h"
#endif




svn_error_t *
svn_cl__print_commit_info(const svn_commit_info_t *commit_info,
                          void *baton,
                          apr_pool_t *pool)
{
  /* Be very careful with returning errors from this callback as those
     will be returned as errors from editor->close_edit(...), which may
     cause callers to assume that the commit itself failed.

     See log message of r1659867 and the svn_ra_get_commit_editor3
     documentation for details on error scenarios. */

  if (SVN_IS_VALID_REVNUM(commit_info->revision))
    SVN_ERR(svn_cmdline_printf(pool, _("Committed revision %ld%s.\n"),
                               commit_info->revision,
                               commit_info->revision == 42 &&
                               getenv("SVN_I_LOVE_PANGALACTIC_GARGLE_BLASTERS")
                                 ?  _(" (the answer to life, the universe, "
                                      "and everything)")
                                 : ""));

  /* Writing to stdout, as there maybe systems that consider the
   * presence of stderr as an indication of commit failure.
   * OTOH, this is only of informational nature to the user as
   * the commit has succeeded. */
  if (commit_info->post_commit_err)
    SVN_ERR(svn_cmdline_printf(pool, _("\nWarning: %s\n"),
                               commit_info->post_commit_err));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__merge_file_externally(const char *base_path,
                              const char *their_path,
                              const char *my_path,
                              const char *merged_path,
                              const char *wc_path,
                              apr_hash_t *config,
                              svn_boolean_t *remains_in_conflict,
                              apr_pool_t *pool)
{
  char *merge_tool;
  /* Error if there is no editor specified */
  if (apr_env_get(&merge_tool, "SVN_MERGE", pool) != APR_SUCCESS)
    {
      struct svn_config_t *cfg;
      merge_tool = NULL;
      cfg = config ? svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG) : NULL;
      /* apr_env_get wants char **, this wants const char ** */
      svn_config_get(cfg, (const char **)&merge_tool,
                     SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_MERGE_TOOL_CMD, NULL);
    }

  if (merge_tool)
    {
      const char *c;

      for (c = merge_tool; *c; c++)
        if (!svn_ctype_isspace(*c))
          break;

      if (! *c)
        return svn_error_create
          (SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL, NULL,
           _("The SVN_MERGE environment variable is empty or "
             "consists solely of whitespace. Expected a shell command.\n"));
    }
  else
      return svn_error_create
        (SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL, NULL,
         _("The environment variable SVN_MERGE and the merge-tool-cmd run-time "
           "configuration option were not set.\n"));

  {
    const char *arguments[7] = { 0 };
    char *cwd;
    int exitcode;

    apr_status_t status = apr_filepath_get(&cwd, APR_FILEPATH_NATIVE, pool);
    if (status != 0)
      return svn_error_wrap_apr(status, NULL);

    arguments[0] = merge_tool;
    arguments[1] = base_path;
    arguments[2] = their_path;
    arguments[3] = my_path;
    arguments[4] = merged_path;
    arguments[5] = wc_path;
    arguments[6] = NULL;

    SVN_ERR(svn_io_run_cmd(svn_dirent_internal_style(cwd, pool), merge_tool,
                           arguments, &exitcode, NULL, TRUE, NULL, NULL, NULL,
                           pool));
    /* Exit code 0 means the merge was successful.
     * Exit code 1 means the file was left in conflict but it
     * is OK to continue with the merge.
     * Any other exit code means there was a real problem. */
    if (exitcode != 0 && exitcode != 1)
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
        _("The external merge tool '%s' exited with exit code %d."),
        merge_tool, exitcode);
    else if (remains_in_conflict)
      *remains_in_conflict = exitcode == 1;
  }
  return SVN_NO_ERROR;
}


/* A svn_client_ctx_t's log_msg_baton3, for use with
   svn_cl__make_log_msg_baton(). */
struct log_msg_baton
{
  const char *editor_cmd;  /* editor specified via --editor-cmd, else NULL */
  const char *message;  /* the message. */
  const char *message_encoding; /* the locale/encoding of the message. */
  const char *base_dir; /* the base directory for an external edit. UTF-8! */
  const char *tmpfile_left; /* the tmpfile left by an external edit. UTF-8! */
  svn_boolean_t non_interactive; /* if true, don't pop up an editor */
  apr_hash_t *config; /* client configuration hash */
  svn_boolean_t keep_locks; /* Keep repository locks? */
  apr_pool_t *pool; /* a pool. */
};


svn_error_t *
svn_cl__make_log_msg_baton(void **baton,
                           svn_cl__opt_state_t *opt_state,
                           const char *base_dir /* UTF-8! */,
                           apr_hash_t *config,
                           apr_pool_t *pool)
{
  struct log_msg_baton *lmb = apr_pcalloc(pool, sizeof(*lmb));

  if (opt_state->filedata)
    {
      if (strlen(opt_state->filedata->data) < opt_state->filedata->len)
        {
          /* The data contains a zero byte, and therefore can't be
             represented as a C string.  Punt now; it's probably not
             a deliberate encoding, and even if it is, we still
             can't handle it. */
          return svn_error_create(SVN_ERR_CL_BAD_LOG_MESSAGE, NULL,
                                  _("Log message contains a zero byte"));
        }
      lmb->message = opt_state->filedata->data;
    }
  else
    {
      lmb->message = opt_state->message;
    }

  lmb->editor_cmd = opt_state->editor_cmd;
  if (opt_state->encoding)
    {
      lmb->message_encoding = opt_state->encoding;
    }
  else if (config)
    {
      svn_config_t *cfg = svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG);
      svn_config_get(cfg, &(lmb->message_encoding),
                     SVN_CONFIG_SECTION_MISCELLANY,
                     SVN_CONFIG_OPTION_LOG_ENCODING,
                     NULL);
    }
  else
    lmb->message_encoding = NULL;

  lmb->base_dir = base_dir;
  lmb->tmpfile_left = NULL;
  lmb->config = config;
  lmb->keep_locks = opt_state->no_unlock;
  lmb->non_interactive = opt_state->non_interactive;
  lmb->pool = pool;
  *baton = lmb;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__cleanup_log_msg(void *log_msg_baton,
                        svn_error_t *commit_err,
                        apr_pool_t *pool)
{
  struct log_msg_baton *lmb = log_msg_baton;
  svn_error_t *err;

  /* If there was no tmpfile left, or there is no log message baton,
     return COMMIT_ERR. */
  if ((! lmb) || (! lmb->tmpfile_left))
    return commit_err;

  /* If there was no commit error, cleanup the tmpfile and return. */
  if (! commit_err)
    return svn_io_remove_file2(lmb->tmpfile_left, FALSE, lmb->pool);

  /* There was a commit error; there is a tmpfile.  Leave the tmpfile
     around, and add message about its presence to the commit error
     chain.  Then return COMMIT_ERR.  If the conversion from UTF-8 to
     native encoding fails, we have to compose that error with the
     commit error chain, too. */

  err = svn_error_createf(commit_err->apr_err, NULL,
                          _("   '%s'"),
                          svn_dirent_local_style(lmb->tmpfile_left, pool));
  svn_error_compose(commit_err,
                    svn_error_create(commit_err->apr_err, err,
                      _("Your commit message was left in "
                        "a temporary file:")));
  return commit_err;
}


/* Remove line-starting PREFIX and everything after it from BUFFER.
   If NEW_LEN is non-NULL, return the new length of BUFFER in
   *NEW_LEN.  */
static void
truncate_buffer_at_prefix(apr_size_t *new_len,
                          char *buffer,
                          const char *prefix)
{
  char *substring = buffer;

  assert(buffer && prefix);

  /* Initialize *NEW_LEN. */
  if (new_len)
    *new_len = strlen(buffer);

  while (1)
    {
      /* Find PREFIX in BUFFER. */
      substring = strstr(substring, prefix);
      if (! substring)
        return;

      /* We found PREFIX.  Is it really a PREFIX?  Well, if it's the first
         thing in the file, or if the character before it is a
         line-terminator character, it sure is. */
      if ((substring == buffer)
          || (*(substring - 1) == '\r')
          || (*(substring - 1) == '\n'))
        {
          *substring = '\0';
          if (new_len)
            *new_len = substring - buffer;
        }
      else if (substring)
        {
          /* Well, it wasn't really a prefix, so just advance by 1
             character and continue. */
          substring++;
        }
    }

  /* NOTREACHED */
}


/*
 * Since we're adding freebsd-specific tokens to the log message,
 * clean out any leftovers to avoid accidently sending them to other
 * projects that won't be expecting them.
 */

static const char *prefixes[] = {
  "PR:",
  "Submitted by:",
  "Reported by:",
  "Reviewed by:",
  "Approved by:",
  "Obtained from:",
  "MFC after:",
  "MFH:",
  "Relnotes:",
  "Security:",
  "Sponsored by:",
  "Pull Request:",
  "Differential Revision:",
};

void
cleanmsg(apr_size_t *l, char *s)
{
  int i;
  char *pos;
  char *kw;
  char *p;
  int empty;

  for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
    pos = s;
    while ((kw = strstr(pos, prefixes[i])) != NULL) {
      /* Check to see if keyword is at start of line (or buffer) */
      if (!(kw == s || kw[-1] == '\r' || kw[-1] == '\n')) {
	pos = kw + 1;
	continue;
      }
      p = kw + strlen(prefixes[i]);
      empty = 1;
      while (1) {
	if (*p == ' ' || *p == '\t') {
	  p++;
	  continue;
	}
	if (*p == '\0' || *p == '\r' || *p == '\n')
	  break;
	empty = 0;
	break;
      }
      if (empty && (*p == '\r' || *p == '\n')) {
	memmove(kw, p + 1, strlen(p + 1) + 1);
	if (l)
	  *l -= (p + 1 - kw);
      } else if (empty) {
	*kw = '\0';
	if (l)
	  *l -= (p - kw);
      } else {
	pos = p;
      }
    }
  }
}

#define EDITOR_EOF_PREFIX  _("--This line, and those below, will be ignored--")

svn_error_t *
svn_cl__get_log_message(const char **log_msg,
                        const char **tmp_file,
                        const apr_array_header_t *commit_items,
                        void *baton,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *default_msg = NULL;
  struct log_msg_baton *lmb = baton;
  svn_stringbuf_t *message = NULL;
  svn_config_t *cfg;
  const char *mfc_after, *sponsored_by;

  cfg = lmb->config ? svn_hash_gets(lmb->config, SVN_CONFIG_CATEGORY_CONFIG) : NULL;

  /* Set default message.  */
  default_msg = svn_stringbuf_create(APR_EOL_STR, pool);
  svn_stringbuf_appendcstr(default_msg, APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "PR:\t\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Submitted by:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Reported by:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Reviewed by:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Approved by:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Obtained from:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "MFC after:\t");
  svn_config_get(cfg, &mfc_after, SVN_CONFIG_SECTION_MISCELLANY, "freebsd-mfc-after", NULL);
  if (mfc_after != NULL)
	  svn_stringbuf_appendcstr(default_msg, mfc_after);
  svn_stringbuf_appendcstr(default_msg, APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "MFH:\t\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Relnotes:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Security:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Sponsored by:\t");
  svn_config_get(cfg, &sponsored_by, SVN_CONFIG_SECTION_MISCELLANY, "freebsd-sponsored-by",
#ifdef HAS_ORGANIZATION_NAME
  	ORGANIZATION_NAME);
#else
	NULL);
#endif
  if (sponsored_by != NULL)
	  svn_stringbuf_appendcstr(default_msg, sponsored_by);
  svn_stringbuf_appendcstr(default_msg, APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Pull Request:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "Differential Revision:\t" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, EDITOR_EOF_PREFIX);
  svn_stringbuf_appendcstr(default_msg, APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Description of fields to fill in above:                     76 columns --|" APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> PR:                       If and which Problem Report is related." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Submitted by:             If someone else sent in the change." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Reported by:              If someone else reported the issue." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Reviewed by:              If someone else reviewed your modification." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Approved by:              If you needed approval for this commit." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Obtained from:            If the change is from a third party." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> MFC after:                N [day[s]|week[s]|month[s]].  Request a reminder email." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> MFH:                      Ports tree branch name.  Request approval for merge." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Relnotes:                 Set to 'yes' for mention in release notes." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Security:                 Vulnerability reference (one per line) or description." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Sponsored by:             If the change was sponsored by an organization." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Pull Request:             https://github.com/freebsd/freebsd/pull/### (*full* GitHub URL needed)." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Differential Revision:    https://reviews.freebsd.org/D### (*full* phabric URL needed)." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, "> Empty fields above will be automatically removed." APR_EOL_STR);
  svn_stringbuf_appendcstr(default_msg, APR_EOL_STR);

  *tmp_file = NULL;
  if (lmb->message)
    {
      svn_string_t *log_msg_str = svn_string_create(lmb->message, pool);

      SVN_ERR_W(svn_subst_translate_string2(&log_msg_str, NULL, NULL,
                                            log_msg_str, lmb->message_encoding,
                                            FALSE, pool, pool),
                _("Error normalizing log message to internal format"));

      /* Strip off the EOF marker text and the junk that follows it. */
      truncate_buffer_at_prefix(&(log_msg_str->len), (char *)log_msg_str->data,
                                EDITOR_EOF_PREFIX);

      cleanmsg(&(log_msg_str->len), (char*)log_msg_str->data);

      *log_msg = log_msg_str->data;
      return SVN_NO_ERROR;
    }

  if (! commit_items->nelts)
    {
      *log_msg = "";
      return SVN_NO_ERROR;
    }

  while (! message)
    {
      /* We still don't have a valid commit message.  Use $EDITOR to
         get one.  Note that svn_cl__edit_string_externally will still
         return a UTF-8'ized log message. */
      int i;
      svn_stringbuf_t *tmp_message = svn_stringbuf_dup(default_msg, pool);
      svn_error_t *err = SVN_NO_ERROR;
      svn_string_t *msg_string = svn_string_create_empty(pool);

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item
            = APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);
          const char *path = item->path;
          char text_mod = '_', prop_mod = ' ', unlock = ' ';

          if (! path)
            path = item->url;
          else if (lmb->base_dir)
            path = svn_dirent_is_child(lmb->base_dir, path, pool);

          /* If still no path, then just use current directory. */
          if (! path || !*path)
            path = ".";

          if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
              && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD))
            text_mod = 'R';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
            text_mod = 'A';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
            text_mod = 'D';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
            text_mod = 'M';

          if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
            prop_mod = 'M';

          if (! lmb->keep_locks
              && item->state_flags & SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN)
            unlock = 'U';

          svn_stringbuf_appendbyte(tmp_message, text_mod);
          svn_stringbuf_appendbyte(tmp_message, prop_mod);
          svn_stringbuf_appendbyte(tmp_message, unlock);
          if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
            /* History included via copy/move. */
            svn_stringbuf_appendcstr(tmp_message, "+ ");
          else
            svn_stringbuf_appendcstr(tmp_message, "  ");
          svn_stringbuf_appendcstr(tmp_message, path);
          svn_stringbuf_appendcstr(tmp_message, APR_EOL_STR);
        }

      msg_string->data = tmp_message->data;
      msg_string->len = tmp_message->len;

      /* Use the external edit to get a log message. */
      if (! lmb->non_interactive)
        {
          err = svn_cmdline__edit_string_externally(&msg_string, &lmb->tmpfile_left,
                                                    lmb->editor_cmd,
                                                    lmb->base_dir ? lmb->base_dir : "",
                                                    msg_string, "svn-commit",
                                                    lmb->config, TRUE,
                                                    lmb->message_encoding,
                                                    pool);
        }
      else /* non_interactive flag says we can't pop up an editor, so error */
        {
          return svn_error_create
            (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
             _("Cannot invoke editor to get log message "
               "when non-interactive"));
        }

      /* Dup the tmpfile path into its baton's pool. */
      *tmp_file = lmb->tmpfile_left = apr_pstrdup(lmb->pool,
                                                  lmb->tmpfile_left);

      /* If the edit returned an error, handle it. */
      if (err)
        {
          if (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR)
            err = svn_error_quick_wrap
              (err, _("Could not use external editor to fetch log message; "
                      "consider setting the $SVN_EDITOR environment variable "
                      "or using the --message (-m) or --file (-F) options"));
          return svn_error_trace(err);
        }

      if (msg_string)
        message = svn_stringbuf_create_from_string(msg_string, pool);

      /* Strip off the EOF marker text and the junk that follows it. */
      if (message)
        truncate_buffer_at_prefix(&message->len, message->data,
                                  EDITOR_EOF_PREFIX);
      /*
       * Since we're adding freebsd-specific tokens to the log message,
       * clean out any leftovers to avoid accidently sending them to other
       * projects that won't be expecting them.
       */
      if (message)
	cleanmsg(&message->len, message->data);

      if (message)
        {
          /* We did get message, now check if it is anything more than just
             white space as we will consider white space only as empty */
          apr_size_t len;

          for (len = 0; len < message->len; len++)
            {
              /* FIXME: should really use an UTF-8 whitespace test
                 rather than svn_ctype_isspace, which is ASCII only */
              if (! svn_ctype_isspace(message->data[len]))
                break;
            }
          if (len == message->len)
            message = NULL;
        }

      if (! message)
        {
          const char *reply;
          SVN_ERR(svn_cmdline_prompt_user2
                  (&reply,
                   _("\nLog message unchanged or not specified\n"
                     "(a)bort, (c)ontinue, (e)dit:\n"), NULL, pool));
          if (reply)
            {
              int letter = apr_tolower(reply[0]);

              /* If the user chooses to abort, we cleanup the
                 temporary file and exit the loop with a NULL
                 message. */
              if ('a' == letter)
                {
                  SVN_ERR(svn_io_remove_file2(lmb->tmpfile_left, FALSE, pool));
                  *tmp_file = lmb->tmpfile_left = NULL;
                  break;
                }

              /* If the user chooses to continue, we make an empty
                 message, which will cause us to exit the loop.  We
                 also cleanup the temporary file. */
              if ('c' == letter)
                {
                  SVN_ERR(svn_io_remove_file2(lmb->tmpfile_left, FALSE, pool));
                  *tmp_file = lmb->tmpfile_left = NULL;
                  message = svn_stringbuf_create_empty(pool);
                }

              /* If the user chooses anything else, the loop will
                 continue on the NULL message. */
            }
        }
    }

  *log_msg = message ? message->data : NULL;
  return SVN_NO_ERROR;
}


/* ### The way our error wrapping currently works, the error returned
 * from here will look as though it originates in this source file,
 * instead of in the caller's source file.  This can be a bit
 * misleading, until one starts debugging.  Ideally, there'd be a way
 * to wrap an error while preserving its FILE/LINE info.
 */
svn_error_t *
svn_cl__may_need_force(svn_error_t *err)
{
  if (err
      && (err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE ||
          err->apr_err == SVN_ERR_CLIENT_MODIFIED))
    {
      /* Should this svn_error_compose a new error number? Probably not,
         the error hasn't changed. */
      err = svn_error_quick_wrap
        (err, _("Use --force to override this restriction (local modifications "
         "may be lost)"));
    }

  return svn_error_trace(err);
}


svn_error_t *
svn_cl__error_checked_fputs(const char *string, FILE* stream)
{
  /* On POSIX systems, errno will be set on an error in fputs, but this might
     not be the case on other platforms.  We reset errno and only
     use it if it was set by the below fputs call.  Else, we just return
     a generic error. */
  errno = 0;

  if (fputs(string, stream) == EOF)
    {
      if (apr_get_os_error()) /* is errno on POSIX */
        return svn_error_wrap_apr(apr_get_os_error(), _("Write error"));
      else
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL, NULL);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__try(svn_error_t *err,
            apr_array_header_t *errors_seen,
            svn_boolean_t quiet,
            ...)
{
  if (err)
    {
      apr_status_t apr_err;
      va_list ap;

      va_start(ap, quiet);
      while ((apr_err = va_arg(ap, apr_status_t)) != APR_SUCCESS)
        {
          if (errors_seen)
            {
              int i;
              svn_boolean_t add = TRUE;

              /* Don't report duplicate error codes. */
              for (i = 0; i < errors_seen->nelts; i++)
                {
                  if (APR_ARRAY_IDX(errors_seen, i,
                                    apr_status_t) == err->apr_err)
                    {
                      add = FALSE;
                      break;
                    }
                }
              if (add)
                APR_ARRAY_PUSH(errors_seen, apr_status_t) = err->apr_err;
            }
          if (err->apr_err == apr_err)
            {
              if (! quiet)
                svn_handle_warning2(stderr, err, "svn: ");
              svn_error_clear(err);
              va_end(ap);
              return SVN_NO_ERROR;
            }
        }
      va_end(ap);
    }

  return svn_error_trace(err);
}


void
svn_cl__xml_tagged_cdata(svn_stringbuf_t **sb,
                         apr_pool_t *pool,
                         const char *tagname,
                         const char *string)
{
  if (string)
    {
      svn_xml_make_open_tag(sb, pool, svn_xml_protect_pcdata,
                            tagname, SVN_VA_NULL);
      svn_xml_escape_cdata_cstring(sb, string, pool);
      svn_xml_make_close_tag(sb, pool, tagname);
    }
}


void
svn_cl__print_xml_commit(svn_stringbuf_t **sb,
                         svn_revnum_t revision,
                         const char *author,
                         const char *date,
                         apr_pool_t *pool)
{
  /* "<commit ...>" */
  svn_xml_make_open_tag(sb, pool, svn_xml_normal, "commit",
                        "revision",
                        apr_psprintf(pool, "%ld", revision), SVN_VA_NULL);

  /* "<author>xx</author>" */
  if (author)
    svn_cl__xml_tagged_cdata(sb, pool, "author", author);

  /* "<date>xx</date>" */
  if (date)
    svn_cl__xml_tagged_cdata(sb, pool, "date", date);

  /* "</commit>" */
  svn_xml_make_close_tag(sb, pool, "commit");
}


void
svn_cl__print_xml_lock(svn_stringbuf_t **sb,
                       const svn_lock_t *lock,
                       apr_pool_t *pool)
{
  /* "<lock>" */
  svn_xml_make_open_tag(sb, pool, svn_xml_normal, "lock", SVN_VA_NULL);

  /* "<token>xx</token>" */
  svn_cl__xml_tagged_cdata(sb, pool, "token", lock->token);

  /* "<owner>xx</owner>" */
  svn_cl__xml_tagged_cdata(sb, pool, "owner", lock->owner);

  /* "<comment>xx</comment>" */
  svn_cl__xml_tagged_cdata(sb, pool, "comment", lock->comment);

  /* "<created>xx</created>" */
  svn_cl__xml_tagged_cdata(sb, pool, "created",
                           svn_time_to_cstring(lock->creation_date, pool));

  /* "<expires>xx</expires>" */
  if (lock->expiration_date != 0)
    svn_cl__xml_tagged_cdata(sb, pool, "expires",
                             svn_time_to_cstring(lock->expiration_date, pool));

  /* "</lock>" */
  svn_xml_make_close_tag(sb, pool, "lock");
}


svn_error_t *
svn_cl__xml_print_header(const char *tagname,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);

  /* <?xml version="1.0" encoding="UTF-8"?> */
  svn_xml_make_header2(&sb, "UTF-8", pool);

  /* "<TAGNAME>" */
  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, tagname, SVN_VA_NULL);

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


svn_error_t *
svn_cl__xml_print_footer(const char *tagname,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);

  /* "</TAGNAME>" */
  svn_xml_make_close_tag(&sb, pool, tagname);
  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* A map for svn_node_kind_t values to XML strings */
static const svn_token_map_t map_node_kind_xml[] =
{
  { "none", svn_node_none },
  { "file", svn_node_file },
  { "dir",  svn_node_dir },
  { "",     svn_node_unknown },
  { NULL,   0 }
};

/* A map for svn_node_kind_t values to human-readable strings */
static const svn_token_map_t map_node_kind_human[] =
{
  { N_("none"), svn_node_none },
  { N_("file"), svn_node_file },
  { N_("dir"),  svn_node_dir },
  { "",         svn_node_unknown },
  { NULL,       0 }
};

const char *
svn_cl__node_kind_str_xml(svn_node_kind_t kind)
{
  return svn_token__to_word(map_node_kind_xml, kind);
}

const char *
svn_cl__node_kind_str_human_readable(svn_node_kind_t kind)
{
  return _(svn_token__to_word(map_node_kind_human, kind));
}


/* A map for svn_wc_operation_t values to XML strings */
static const svn_token_map_t map_wc_operation_xml[] =
{
  { "none",   svn_wc_operation_none },
  { "update", svn_wc_operation_update },
  { "switch", svn_wc_operation_switch },
  { "merge",  svn_wc_operation_merge },
  { NULL,     0 }
};

/* A map for svn_wc_operation_t values to human-readable strings */
static const svn_token_map_t map_wc_operation_human[] =
{
  { N_("none"),   svn_wc_operation_none },
  { N_("update"), svn_wc_operation_update },
  { N_("switch"), svn_wc_operation_switch },
  { N_("merge"),  svn_wc_operation_merge },
  { NULL,         0 }
};

const char *
svn_cl__operation_str_xml(svn_wc_operation_t operation, apr_pool_t *pool)
{
  return svn_token__to_word(map_wc_operation_xml, operation);
}

const char *
svn_cl__operation_str_human_readable(svn_wc_operation_t operation,
                                     apr_pool_t *pool)
{
  return _(svn_token__to_word(map_wc_operation_human, operation));
}


svn_error_t *
svn_cl__args_to_target_array_print_reserved(apr_array_header_t **targets,
                                            apr_getopt_t *os,
                                            const apr_array_header_t *known_targets,
                                            svn_client_ctx_t *ctx,
                                            svn_boolean_t keep_last_origpath_on_truepath_collision,
                                            apr_pool_t *pool)
{
  svn_error_t *err = svn_client_args_to_target_array2(targets,
                                                      os,
                                                      known_targets,
                                                      ctx,
                                                      keep_last_origpath_on_truepath_collision,
                                                      pool);
  if (err)
    {
      if (err->apr_err ==  SVN_ERR_RESERVED_FILENAME_SPECIFIED)
        {
          svn_handle_error2(err, stderr, FALSE, "svn: Skipping argument: ");
          svn_error_clear(err);
        }
      else
        return svn_error_trace(err);
    }
  return SVN_NO_ERROR;
}


/* Helper for svn_cl__get_changelist(); implements
   svn_changelist_receiver_t. */
static svn_error_t *
changelist_receiver(void *baton,
                    const char *path,
                    const char *changelist,
                    apr_pool_t *pool)
{
  /* No need to check CHANGELIST; our caller only asked about one of them. */
  apr_array_header_t *paths = baton;
  APR_ARRAY_PUSH(paths, const char *) = apr_pstrdup(paths->pool, path);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__changelist_paths(apr_array_header_t **paths,
                         const apr_array_header_t *changelists,
                         const apr_array_header_t *targets,
                         svn_depth_t depth,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *found;
  apr_hash_t *paths_hash;
  apr_pool_t *iterpool;
  int i;

  if (! (changelists && changelists->nelts))
    {
      *paths = (apr_array_header_t *)targets;
      return SVN_NO_ERROR;
    }

  found = apr_array_make(scratch_pool, 8, sizeof(const char *));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      svn_pool_clear(iterpool);
      SVN_ERR(svn_client_get_changelists(target, changelists, depth,
                                         changelist_receiver, found,
                                         ctx, iterpool));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_hash_from_cstring_keys(&paths_hash, found, result_pool));
  return svn_error_trace(svn_hash_keys(paths, paths_hash, result_pool));
}

svn_cl__show_revs_t
svn_cl__show_revs_from_word(const char *word)
{
  if (strcmp(word, SVN_CL__SHOW_REVS_MERGED) == 0)
    return svn_cl__show_revs_merged;
  if (strcmp(word, SVN_CL__SHOW_REVS_ELIGIBLE) == 0)
    return svn_cl__show_revs_eligible;
  /* word is an invalid flavor. */
  return svn_cl__show_revs_invalid;
}


svn_error_t *
svn_cl__time_cstring_to_human_cstring(const char **human_cstring,
                                      const char *data,
                                      apr_pool_t *pool)
{
  svn_error_t *err;
  apr_time_t when;

  err = svn_time_from_cstring(&when, data, pool);
  if (err && err->apr_err == SVN_ERR_BAD_DATE)
    {
      svn_error_clear(err);

      *human_cstring = _("(invalid date)");
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  *human_cstring = svn_time_to_human_cstring(when, pool);

  return SVN_NO_ERROR;
}

const char *
svn_cl__node_description(const char *repos_root_url,
                         const char *repos_relpath,
                         svn_revnum_t peg_rev,
                         svn_node_kind_t node_kind,
                         const char *wc_repos_root_URL,
                         apr_pool_t *pool)
{
  const char *root_str = "^";
  const char *path_str = "...";

  if (!repos_root_url || !repos_relpath || !SVN_IS_VALID_REVNUM(peg_rev))
    /* Printing "(none)" the harder way to ensure conformity (mostly with
     * translations). */
    return apr_psprintf(pool, "(%s)",
                        svn_cl__node_kind_str_human_readable(svn_node_none));

  /* Construct a "caret notation" ^/URL if NODE matches WC_REPOS_ROOT_URL.
   * Otherwise show the complete URL, and if we can't, show dots. */

  if (repos_root_url &&
      (wc_repos_root_URL == NULL ||
       strcmp(repos_root_url, wc_repos_root_URL) != 0))
    root_str = repos_root_url;

  if (repos_relpath)
    path_str = repos_relpath;

  return apr_psprintf(pool, "(%s) %s@%ld",
                      svn_cl__node_kind_str_human_readable(node_kind),
                      svn_path_url_add_component2(root_str, path_str, pool),
                      peg_rev);
}

svn_error_t *
svn_cl__eat_peg_revisions(apr_array_header_t **true_targets_p,
                          const apr_array_header_t *targets,
                          apr_pool_t *pool)
{
  int i;
  apr_array_header_t *true_targets;

  true_targets = apr_array_make(pool, targets->nelts, sizeof(const char *));

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *true_target, *peg;

      SVN_ERR(svn_opt__split_arg_at_peg_revision(&true_target, &peg,
                                                 target, pool));
      if (peg[0] && peg[1])
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s': a peg revision is not allowed here"),
                                 target);
      APR_ARRAY_PUSH(true_targets, const char *) = true_target;
    }

  SVN_ERR_ASSERT(true_targets_p);
  *true_targets_p = true_targets;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__assert_homogeneous_target_type(const apr_array_header_t *targets)
{
  svn_error_t *err;

  err = svn_client__assert_homogeneous_target_type(targets);
  if (err && err->apr_err == SVN_ERR_ILLEGAL_TARGET)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, err, NULL);
  return err;
}

svn_error_t *
svn_cl__check_target_is_local_path(const char *target)
{
  if (svn_path_is_url(target))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("'%s' is not a local path"), target);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__check_targets_are_local_paths(const apr_array_header_t *targets)
{
  int i;

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);

      SVN_ERR(svn_cl__check_target_is_local_path(target));
    }
  return SVN_NO_ERROR;
}

const char *
svn_cl__local_style_skip_ancestor(const char *parent_path,
                                  const char *path,
                                  apr_pool_t *pool)
{
  const char *relpath = NULL;

  if (parent_path)
    relpath = svn_dirent_skip_ancestor(parent_path, path);

  return svn_dirent_local_style(relpath ? relpath : path, pool);
}

svn_error_t *
svn_cl__propset_print_binary_mime_type_warning(apr_array_header_t *targets,
                                               const char *propname,
                                               const svn_string_t *propval,
                                               apr_pool_t *scratch_pool)
{
  if (strcmp(propname, SVN_PROP_MIME_TYPE) == 0)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      for (i = 0; i < targets->nelts; i++)
        {
          const char *detected_mimetype;
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          const char *local_abspath;
          const svn_string_t *canon_propval;
          svn_node_kind_t node_kind;

          svn_pool_clear(iterpool);

          SVN_ERR(svn_dirent_get_absolute(&local_abspath, target, iterpool));
          SVN_ERR(svn_io_check_path(local_abspath, &node_kind, iterpool));
          if (node_kind != svn_node_file)
            continue;

          SVN_ERR(svn_wc_canonicalize_svn_prop(&canon_propval,
                                               propname, propval,
                                               local_abspath,
                                               svn_node_file,
                                               FALSE, NULL, NULL,
                                               iterpool));

          if (svn_mime_type_is_binary(canon_propval->data))
            {
              SVN_ERR(svn_io_detect_mimetype2(&detected_mimetype,
                                              local_abspath, NULL,
                                              iterpool));
              if (detected_mimetype == NULL ||
                  !svn_mime_type_is_binary(detected_mimetype))
                svn_error_clear(svn_cmdline_fprintf(stderr, iterpool,
                  _("svn: warning: '%s' is a binary mime-type but file '%s' "
                    "looks like text; diff, merge, blame, and other "
                    "operations will stop working on this file\n"),
                    canon_propval->data,
                    svn_dirent_local_style(local_abspath, iterpool)));

            }
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

