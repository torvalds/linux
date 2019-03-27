/*
 * editorp.c :  Driving and consuming an editor across an svn connection
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



#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_ra_svn.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_private_config.h"

#include "private/svn_atomic.h"
#include "private/svn_fspath.h"
#include "private/svn_editor.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"

#include "ra_svn.h"

/*
 * Both the client and server in the svn protocol need to drive and
 * consume editors.  For a commit, the client drives and the server
 * consumes; for an update/switch/status/diff, the server drives and
 * the client consumes.  This file provides a generic framework for
 * marshalling and unmarshalling editor operations over an svn
 * connection; both ends are useful for both server and client.
 */

typedef struct ra_svn_edit_baton_t {
  svn_ra_svn_conn_t *conn;
  svn_ra_svn_edit_callback callback;    /* Called on successful completion. */
  void *callback_baton;
  apr_uint64_t next_token;
  svn_boolean_t got_status;
} ra_svn_edit_baton_t;

/* Works for both directories and files. */
typedef struct ra_svn_baton_t {
  svn_ra_svn_conn_t *conn;
  apr_pool_t *pool;
  ra_svn_edit_baton_t *eb;
  svn_string_t *token;
} ra_svn_baton_t;

/* Forward declaration. */
typedef struct ra_svn_token_entry_t ra_svn_token_entry_t;

typedef struct ra_svn_driver_state_t {
  const svn_delta_editor_t *editor;
  void *edit_baton;
  apr_hash_t *tokens;

  /* Entry for the last token seen.  May be NULL. */
  ra_svn_token_entry_t *last_token;
  svn_boolean_t *aborted;
  svn_boolean_t done;
  apr_pool_t *pool;
  apr_pool_t *file_pool;
  int file_refs;
  svn_boolean_t for_replay;
} ra_svn_driver_state_t;

/* Works for both directories and files; however, the pool handling is
   different for files.  To save space during commits (where file
   batons generally last until the end of the commit), token entries
   for files are all created in a single reference-counted pool (the
   file_pool member of the driver state structure), which is cleared
   at close_file time when the reference count hits zero.  So the pool
   field in this structure is vestigial for files, and we use it for a
   different purpose instead: at apply-textdelta time, we set it to a
   subpool of the file pool, which is destroyed in textdelta-end. */
struct ra_svn_token_entry_t {
  svn_string_t *token;
  void *baton;
  svn_boolean_t is_file;
  svn_stream_t *dstream;  /* svndiff stream for apply_textdelta */
  apr_pool_t *pool;
};

/* --- CONSUMING AN EDITOR BY PASSING EDIT OPERATIONS OVER THE NET --- */

static svn_string_t *
make_token(char type,
           ra_svn_edit_baton_t *eb,
           apr_pool_t *pool)
{
  apr_size_t len;
  char buffer[1 + SVN_INT64_BUFFER_SIZE];
  buffer[0] = type;
  len = 1 + svn__ui64toa(&buffer[1], eb->next_token++);
  
  return svn_string_ncreate(buffer, len, pool);
}

static ra_svn_baton_t *ra_svn_make_baton(svn_ra_svn_conn_t *conn,
                                         apr_pool_t *pool,
                                         ra_svn_edit_baton_t *eb,
                                         svn_string_t *token)
{
  ra_svn_baton_t *b;

  b = apr_palloc(pool, sizeof(*b));
  b->conn = conn;
  b->pool = pool;
  b->eb = eb;
  b->token = token;
  return b;
}

/* Check for an early error status report from the consumer.  If we
 * get one, abort the edit and return the error. */
static svn_error_t *
check_for_error_internal(ra_svn_edit_baton_t *eb, apr_pool_t *pool)
{
  svn_boolean_t available;
  SVN_ERR_ASSERT(!eb->got_status);

  /* reset TX counter */
  eb->conn->written_since_error_check = 0;

  /* if we weren't asked to always check, wait for at least the next TX */
  eb->conn->may_check_for_error = eb->conn->error_check_interval == 0;

  /* any incoming data? */
  SVN_ERR(svn_ra_svn__data_available(eb->conn, &available));
  if (available)
    {
      eb->got_status = TRUE;
      SVN_ERR(svn_ra_svn__write_cmd_abort_edit(eb->conn, pool));
      SVN_ERR(svn_ra_svn__read_cmd_response(eb->conn, pool, ""));
      /* We shouldn't get here if the consumer is doing its job. */
      return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                              _("Successful edit status returned too soon"));
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
check_for_error(ra_svn_edit_baton_t *eb, apr_pool_t *pool)
{
  return eb->conn->may_check_for_error
    ? check_for_error_internal(eb, pool)
    : SVN_NO_ERROR;
}

static svn_error_t *ra_svn_target_rev(void *edit_baton, svn_revnum_t rev,
                                      apr_pool_t *pool)
{
  ra_svn_edit_baton_t *eb = edit_baton;

  SVN_ERR(check_for_error(eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_target_rev(eb->conn, pool, rev));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_open_root(void *edit_baton, svn_revnum_t rev,
                                     apr_pool_t *pool, void **root_baton)
{
  ra_svn_edit_baton_t *eb = edit_baton;
  svn_string_t *token = make_token('d', eb, pool);

  SVN_ERR(check_for_error(eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_open_root(eb->conn, pool, rev, token));
  *root_baton = ra_svn_make_baton(eb->conn, pool, eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_delete_entry(const char *path, svn_revnum_t rev,
                                        void *parent_baton, apr_pool_t *pool)
{
  ra_svn_baton_t *b = parent_baton;

  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_delete_entry(b->conn, pool,
                                             path, rev, b->token));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_add_dir(const char *path, void *parent_baton,
                                   const char *copy_path,
                                   svn_revnum_t copy_rev,
                                   apr_pool_t *pool, void **child_baton)
{
  ra_svn_baton_t *b = parent_baton;
  svn_string_t *token = make_token('d', b->eb, pool);

  SVN_ERR_ASSERT((copy_path && SVN_IS_VALID_REVNUM(copy_rev))
                 || (!copy_path && !SVN_IS_VALID_REVNUM(copy_rev)));
  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_add_dir(b->conn, pool, path, b->token,
                                        token, copy_path, copy_rev));
  *child_baton = ra_svn_make_baton(b->conn, pool, b->eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_open_dir(const char *path, void *parent_baton,
                                    svn_revnum_t rev, apr_pool_t *pool,
                                    void **child_baton)
{
  ra_svn_baton_t *b = parent_baton;
  svn_string_t *token = make_token('d', b->eb, pool);

  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_open_dir(b->conn, pool, path, b->token,
                                         token, rev));
  *child_baton = ra_svn_make_baton(b->conn, pool, b->eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_change_dir_prop(void *dir_baton, const char *name,
                                           const svn_string_t *value,
                                           apr_pool_t *pool)
{
  ra_svn_baton_t *b = dir_baton;

  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_change_dir_prop(b->conn, pool, b->token,
                                                name, value));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_close_dir(void *dir_baton, apr_pool_t *pool)
{
  ra_svn_baton_t *b = dir_baton;

  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_close_dir(b->conn, pool, b->token));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_absent_dir(const char *path,
                                      void *parent_baton, apr_pool_t *pool)
{
  ra_svn_baton_t *b = parent_baton;

  /* Avoid sending an unknown command if the other end doesn't support
     absent-dir. */
  if (! svn_ra_svn_has_capability(b->conn, SVN_RA_SVN_CAP_ABSENT_ENTRIES))
    return SVN_NO_ERROR;

  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_absent_dir(b->conn, pool, path, b->token));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_add_file(const char *path,
                                    void *parent_baton,
                                    const char *copy_path,
                                    svn_revnum_t copy_rev,
                                    apr_pool_t *pool,
                                    void **file_baton)
{
  ra_svn_baton_t *b = parent_baton;
  svn_string_t *token = make_token('c', b->eb, pool);

  SVN_ERR_ASSERT((copy_path && SVN_IS_VALID_REVNUM(copy_rev))
                 || (!copy_path && !SVN_IS_VALID_REVNUM(copy_rev)));
  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_add_file(b->conn, pool,  path, b->token,
                                         token, copy_path, copy_rev));
  *file_baton = ra_svn_make_baton(b->conn, pool, b->eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_open_file(const char *path,
                                     void *parent_baton,
                                     svn_revnum_t rev,
                                     apr_pool_t *pool,
                                     void **file_baton)
{
  ra_svn_baton_t *b = parent_baton;
  svn_string_t *token = make_token('c', b->eb, pool);

  SVN_ERR(check_for_error(b->eb, b->pool));
  SVN_ERR(svn_ra_svn__write_cmd_open_file(b->conn, pool, path, b->token,
                                          token, rev));
  *file_baton = ra_svn_make_baton(b->conn, pool, b->eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_svndiff_handler(void *baton, const char *data,
                                           apr_size_t *len)
{
  ra_svn_baton_t *b = baton;
  svn_string_t str;

  SVN_ERR(check_for_error(b->eb, b->pool));
  str.data = data;
  str.len = *len;
  return svn_ra_svn__write_cmd_textdelta_chunk(b->conn, b->pool,
                                               b->token, &str);
}

static svn_error_t *ra_svn_svndiff_close_handler(void *baton)
{
  ra_svn_baton_t *b = baton;

  SVN_ERR(check_for_error(b->eb, b->pool));
  SVN_ERR(svn_ra_svn__write_cmd_textdelta_end(b->conn, b->pool, b->token));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_apply_textdelta(void *file_baton,
                                           const char *base_checksum,
                                           apr_pool_t *pool,
                                           svn_txdelta_window_handler_t *wh,
                                           void **wh_baton)
{
  ra_svn_baton_t *b = file_baton;
  svn_stream_t *diff_stream;

  /* Tell the other side we're starting a text delta. */
  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_apply_textdelta(b->conn, pool, b->token,
                                                base_checksum));

  /* Transform the window stream to an svndiff stream.  Reuse the
   * file baton for the stream handler, since it has all the
   * needed information. */
  diff_stream = svn_stream_create(b, pool);
  svn_stream_set_write(diff_stream, ra_svn_svndiff_handler);
  svn_stream_set_close(diff_stream, ra_svn_svndiff_close_handler);

  svn_txdelta_to_svndiff3(wh, wh_baton, diff_stream,
                          svn_ra_svn__svndiff_version(b->conn),
                          b->conn->compression_level, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_change_file_prop(void *file_baton,
                                            const char *name,
                                            const svn_string_t *value,
                                            apr_pool_t *pool)
{
  ra_svn_baton_t *b = file_baton;

  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_change_file_prop(b->conn, pool,
                                                 b->token, name, value));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_close_file(void *file_baton,
                                      const char *text_checksum,
                                      apr_pool_t *pool)
{
  ra_svn_baton_t *b = file_baton;

  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_close_file(b->conn, pool,
                                           b->token, text_checksum));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_absent_file(const char *path,
                                       void *parent_baton, apr_pool_t *pool)
{
  ra_svn_baton_t *b = parent_baton;

  /* Avoid sending an unknown command if the other end doesn't support
     absent-file. */
  if (! svn_ra_svn_has_capability(b->conn, SVN_RA_SVN_CAP_ABSENT_ENTRIES))
    return SVN_NO_ERROR;

  SVN_ERR(check_for_error(b->eb, pool));
  SVN_ERR(svn_ra_svn__write_cmd_absent_file(b->conn, pool, path, b->token));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_close_edit(void *edit_baton, apr_pool_t *pool)
{
  ra_svn_edit_baton_t *eb = edit_baton;
  svn_error_t *err;

  SVN_ERR_ASSERT(!eb->got_status);
  eb->got_status = TRUE;
  SVN_ERR(svn_ra_svn__write_cmd_close_edit(eb->conn, pool));
  err = svn_error_trace(svn_ra_svn__read_cmd_response(eb->conn, pool, ""));
  if (err)
    {
      return svn_error_compose_create(
                    err,
                    svn_error_trace(
                        svn_ra_svn__write_cmd_abort_edit(eb->conn, pool)));
    }
  if (eb->callback)
    SVN_ERR(eb->callback(eb->callback_baton));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_abort_edit(void *edit_baton, apr_pool_t *pool)
{
  ra_svn_edit_baton_t *eb = edit_baton;

  if (eb->got_status)
    return SVN_NO_ERROR;
  SVN_ERR(svn_ra_svn__write_cmd_abort_edit(eb->conn, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(eb->conn, pool, ""));
  return SVN_NO_ERROR;
}

void svn_ra_svn_get_editor(const svn_delta_editor_t **editor,
                           void **edit_baton, svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           svn_ra_svn_edit_callback callback,
                           void *callback_baton)
{
  svn_delta_editor_t *ra_svn_editor = svn_delta_default_editor(pool);
  ra_svn_edit_baton_t *eb;

  eb = apr_palloc(pool, sizeof(*eb));
  eb->conn = conn;
  eb->callback = callback;
  eb->callback_baton = callback_baton;
  eb->next_token = 0;
  eb->got_status = FALSE;

  ra_svn_editor->set_target_revision = ra_svn_target_rev;
  ra_svn_editor->open_root = ra_svn_open_root;
  ra_svn_editor->delete_entry = ra_svn_delete_entry;
  ra_svn_editor->add_directory = ra_svn_add_dir;
  ra_svn_editor->open_directory = ra_svn_open_dir;
  ra_svn_editor->change_dir_prop = ra_svn_change_dir_prop;
  ra_svn_editor->close_directory = ra_svn_close_dir;
  ra_svn_editor->absent_directory = ra_svn_absent_dir;
  ra_svn_editor->add_file = ra_svn_add_file;
  ra_svn_editor->open_file = ra_svn_open_file;
  ra_svn_editor->apply_textdelta = ra_svn_apply_textdelta;
  ra_svn_editor->change_file_prop = ra_svn_change_file_prop;
  ra_svn_editor->close_file = ra_svn_close_file;
  ra_svn_editor->absent_file = ra_svn_absent_file;
  ra_svn_editor->close_edit = ra_svn_close_edit;
  ra_svn_editor->abort_edit = ra_svn_abort_edit;

  *editor = ra_svn_editor;
  *edit_baton = eb;

  svn_error_clear(svn_editor__insert_shims(editor, edit_baton, *editor,
                                           *edit_baton, NULL, NULL,
                                           conn->shim_callbacks,
                                           pool, pool));
}

/* --- DRIVING AN EDITOR --- */

/* Store a token entry.  The token string will be copied into pool. */
static ra_svn_token_entry_t *store_token(ra_svn_driver_state_t *ds,
                                         void *baton,
                                         svn_string_t *token,
                                         svn_boolean_t is_file,
                                         apr_pool_t *pool)
{
  ra_svn_token_entry_t *entry;

  entry = apr_palloc(pool, sizeof(*entry));
  entry->token = svn_string_dup(token, pool);
  entry->baton = baton;
  entry->is_file = is_file;
  entry->dstream = NULL;
  entry->pool = pool;

  apr_hash_set(ds->tokens, entry->token->data, entry->token->len, entry);
  ds->last_token = entry;

  return entry;
}

static svn_error_t *lookup_token(ra_svn_driver_state_t *ds,
                                 svn_string_t *token,
                                 svn_boolean_t is_file,
                                 ra_svn_token_entry_t **entry)
{
  if (ds->last_token && svn_string_compare(ds->last_token->token, token))
    {
      *entry = ds->last_token;
    }
  else
    {
      *entry = apr_hash_get(ds->tokens, token->data, token->len);
      ds->last_token = *entry;
    }

  if (!*entry || (*entry)->is_file != is_file)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Invalid file or dir token during edit"));
  return SVN_NO_ERROR;
}

/* Remove a TOKEN entry from DS. */
static void remove_token(ra_svn_driver_state_t *ds,
                         svn_string_t *token)
{
  apr_hash_set(ds->tokens, token->data, token->len, NULL);

  /* Reset this unconditionally.  In most cases, LAST_TOKEN->TOKEN will
     match TOKEN anyway and if it doesn't, lookup_token() will suffer only
     a minor performance hit. */
  ds->last_token = NULL;
}

static svn_error_t *
ra_svn_handle_target_rev(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const svn_ra_svn__list_t *params,
                         ra_svn_driver_state_t *ds)
{
  svn_revnum_t rev;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "r", &rev));
  SVN_CMD_ERR(ds->editor->set_target_revision(ds->edit_baton, rev, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_open_root(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const svn_ra_svn__list_t *params,
                        ra_svn_driver_state_t *ds)
{
  svn_revnum_t rev;
  apr_pool_t *subpool;
  svn_string_t *token;
  void *root_baton;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "(?r)s", &rev, &token));
  subpool = svn_pool_create(ds->pool);
  SVN_CMD_ERR(ds->editor->open_root(ds->edit_baton, rev, subpool,
                                    &root_baton));
  store_token(ds, root_baton, token, FALSE, subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_delete_entry(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const svn_ra_svn__list_t *params,
                           ra_svn_driver_state_t *ds)
{
  const char *path;
  svn_string_t *token;
  svn_revnum_t rev;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "c(?r)s",
                                  &path, &rev, &token));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  path = svn_relpath_canonicalize(path, pool);
  SVN_CMD_ERR(ds->editor->delete_entry(path, rev, entry->baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_add_dir(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      const svn_ra_svn__list_t *params,
                      ra_svn_driver_state_t *ds)
{
  const char *path, *copy_path;
  svn_string_t *token, *child_token;
  svn_revnum_t copy_rev;
  ra_svn_token_entry_t *entry;
  apr_pool_t *subpool;
  void *child_baton;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "css(?cr)", &path, &token,
                                  &child_token, &copy_path, &copy_rev));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  subpool = svn_pool_create(entry->pool);
  path = svn_relpath_canonicalize(path, pool);

  /* Some operations pass COPY_PATH as a full URL (commits, etc.).
     Others (replay, e.g.) deliver an fspath.  That's ... annoying. */
  if (copy_path)
    {
      if (svn_path_is_url(copy_path))
        copy_path = svn_uri_canonicalize(copy_path, pool);
      else
        copy_path = svn_fspath__canonicalize(copy_path, pool);
    }

  SVN_CMD_ERR(ds->editor->add_directory(path, entry->baton, copy_path,
                                        copy_rev, subpool, &child_baton));
  store_token(ds, child_baton, child_token, FALSE, subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_open_dir(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const svn_ra_svn__list_t *params,
                       ra_svn_driver_state_t *ds)
{
  const char *path;
  svn_string_t *token, *child_token;
  svn_revnum_t rev;
  ra_svn_token_entry_t *entry;
  apr_pool_t *subpool;
  void *child_baton;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "css(?r)", &path, &token,
                                  &child_token, &rev));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  subpool = svn_pool_create(entry->pool);
  path = svn_relpath_canonicalize(path, pool);
  SVN_CMD_ERR(ds->editor->open_directory(path, entry->baton, rev, subpool,
                                         &child_baton));
  store_token(ds, child_baton, child_token, FALSE, subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_change_dir_prop(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const svn_ra_svn__list_t *params,
                              ra_svn_driver_state_t *ds)
{
  svn_string_t *token;
  const char *name;
  svn_string_t *value;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "sc(?s)", &token, &name,
                                  &value));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  SVN_CMD_ERR(ds->editor->change_dir_prop(entry->baton, name, value,
                                          entry->pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_close_dir(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const svn_ra_svn__list_t *params,
                        ra_svn_driver_state_t *ds)
{
  svn_string_t *token;
  ra_svn_token_entry_t *entry;

  /* Parse and look up the directory token. */
  SVN_ERR(svn_ra_svn__parse_tuple(params, "s", &token));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));

  /* Close the directory and destroy the baton. */
  SVN_CMD_ERR(ds->editor->close_directory(entry->baton, pool));
  remove_token(ds, token);
  svn_pool_destroy(entry->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_absent_dir(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const svn_ra_svn__list_t *params,
                         ra_svn_driver_state_t *ds)
{
  const char *path;
  svn_string_t *token;
  ra_svn_token_entry_t *entry;

  /* Parse parameters and look up the directory token. */
  SVN_ERR(svn_ra_svn__parse_tuple(params, "cs", &path, &token));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));

  /* Call the editor. */
  SVN_CMD_ERR(ds->editor->absent_directory(path, entry->baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_add_file(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const svn_ra_svn__list_t *params,
                       ra_svn_driver_state_t *ds)
{
  const char *path, *copy_path;
  svn_string_t *token, *file_token;
  svn_revnum_t copy_rev;
  ra_svn_token_entry_t *entry, *file_entry;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "css(?cr)", &path, &token,
                                  &file_token, &copy_path, &copy_rev));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  ds->file_refs++;

  /* The PATH should be canonical .. but never trust incoming data. */
  if (!svn_relpath_is_canonical(path))
    path = svn_relpath_canonicalize(path, pool);

  /* Some operations pass COPY_PATH as a full URL (commits, etc.).
     Others (replay, e.g.) deliver an fspath.  That's ... annoying. */
  if (copy_path)
    {
      if (svn_path_is_url(copy_path))
        copy_path = svn_uri_canonicalize(copy_path, pool);
      else
        copy_path = svn_fspath__canonicalize(copy_path, pool);
    }

  file_entry = store_token(ds, NULL, file_token, TRUE, ds->file_pool);
  SVN_CMD_ERR(ds->editor->add_file(path, entry->baton, copy_path, copy_rev,
                                   ds->file_pool, &file_entry->baton));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_open_file(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const svn_ra_svn__list_t *params,
                        ra_svn_driver_state_t *ds)
{
  const char *path;
  svn_string_t *token, *file_token;
  svn_revnum_t rev;
  ra_svn_token_entry_t *entry, *file_entry;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "css(?r)", &path, &token,
                                  &file_token, &rev));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  ds->file_refs++;

  /* The PATH should be canonical .. but never trust incoming data. */
  if (!svn_relpath_is_canonical(path))
    path = svn_relpath_canonicalize(path, pool);

  file_entry = store_token(ds, NULL, file_token, TRUE, ds->file_pool);
  SVN_CMD_ERR(ds->editor->open_file(path, entry->baton, rev, ds->file_pool,
                                    &file_entry->baton));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_apply_textdelta(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const svn_ra_svn__list_t *params,
                              ra_svn_driver_state_t *ds)
{
  svn_string_t *token;
  ra_svn_token_entry_t *entry;
  svn_txdelta_window_handler_t wh;
  void *wh_baton;
  char *base_checksum;

  /* Parse arguments and look up the token. */
  SVN_ERR(svn_ra_svn__parse_tuple(params, "s(?c)",
                                  &token, &base_checksum));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));
  if (entry->dstream)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Apply-textdelta already active"));
  entry->pool = svn_pool_create(ds->file_pool);
  SVN_CMD_ERR(ds->editor->apply_textdelta(entry->baton, base_checksum,
                                          entry->pool, &wh, &wh_baton));
  entry->dstream = svn_txdelta_parse_svndiff(wh, wh_baton, TRUE, entry->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_textdelta_chunk(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const svn_ra_svn__list_t *params,
                              ra_svn_driver_state_t *ds)
{
  svn_string_t *token;
  ra_svn_token_entry_t *entry;
  svn_string_t *str;

  /* Parse arguments and look up the token. */
  SVN_ERR(svn_ra_svn__parse_tuple(params, "ss", &token, &str));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));
  if (!entry->dstream)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Apply-textdelta not active"));
  SVN_CMD_ERR(svn_stream_write(entry->dstream, str->data, &str->len));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_textdelta_end(svn_ra_svn_conn_t *conn,
                            apr_pool_t *pool,
                            const svn_ra_svn__list_t *params,
                            ra_svn_driver_state_t *ds)
{
  svn_string_t *token;
  ra_svn_token_entry_t *entry;

  /* Parse arguments and look up the token. */
  SVN_ERR(svn_ra_svn__parse_tuple(params, "s", &token));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));
  if (!entry->dstream)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Apply-textdelta not active"));
  SVN_CMD_ERR(svn_stream_close(entry->dstream));
  entry->dstream = NULL;
  svn_pool_destroy(entry->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_change_file_prop(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const svn_ra_svn__list_t *params,
                               ra_svn_driver_state_t *ds)
{
  const char *name;
  svn_string_t *token, *value;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_ra_svn__parse_tuple(params, "sc(?s)", &token, &name,
                                  &value));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));
  SVN_CMD_ERR(ds->editor->change_file_prop(entry->baton, name, value, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_close_file(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const svn_ra_svn__list_t *params,
                         ra_svn_driver_state_t *ds)
{
  svn_string_t *token;
  ra_svn_token_entry_t *entry;
  const char *text_checksum;

  /* Parse arguments and look up the file token. */
  SVN_ERR(svn_ra_svn__parse_tuple(params, "s(?c)",
                                  &token, &text_checksum));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));

  /* Close the file and destroy the baton. */
  SVN_CMD_ERR(ds->editor->close_file(entry->baton, text_checksum, pool));
  remove_token(ds, token);
  if (--ds->file_refs == 0)
    svn_pool_clear(ds->file_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_absent_file(svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool,
                          const svn_ra_svn__list_t *params,
                          ra_svn_driver_state_t *ds)
{
  const char *path;
  svn_string_t *token;
  ra_svn_token_entry_t *entry;

  /* Parse parameters and look up the parent directory token. */
  SVN_ERR(svn_ra_svn__parse_tuple(params, "cs", &path, &token));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));

  /* Call the editor. */
  SVN_CMD_ERR(ds->editor->absent_file(path, entry->baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_handle_close_edit(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const svn_ra_svn__list_t *params,
                         ra_svn_driver_state_t *ds)
{
  SVN_CMD_ERR(ds->editor->close_edit(ds->edit_baton, pool));
  ds->done = TRUE;
#ifdef SVN_DEBUG
  /* Before enabling this in non-maintainer mode:
     *  Note that this code is used on both client *and* server */
  if (apr_hash_count(ds->tokens) != 0)
    return svn_error_create(
              SVN_ERR_FS_INCORRECT_EDITOR_COMPLETION, NULL,
              _("Closing editor with directories or files open"));
#endif
  if (ds->aborted)
    *ds->aborted = FALSE;
  return svn_ra_svn__write_cmd_response(conn, pool, "");
}

static svn_error_t *
ra_svn_handle_abort_edit(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const svn_ra_svn__list_t *params,
                         ra_svn_driver_state_t *ds)
{
  ds->done = TRUE;
  if (ds->aborted)
    *ds->aborted = TRUE;
  SVN_CMD_ERR(ds->editor->abort_edit(ds->edit_baton, pool));
  return svn_ra_svn__write_cmd_response(conn, pool, "");
}

static svn_error_t *
ra_svn_handle_finish_replay(svn_ra_svn_conn_t *conn,
                            apr_pool_t *pool,
                            const svn_ra_svn__list_t *params,
                            ra_svn_driver_state_t *ds)
{
  if (!ds->for_replay)
    return svn_error_createf
      (SVN_ERR_RA_SVN_UNKNOWN_CMD, NULL,
       _("Command 'finish-replay' invalid outside of replays"));
  ds->done = TRUE;
  if (ds->aborted)
    *ds->aborted = FALSE;
  return SVN_NO_ERROR;
}

/* Common function signature for all editor command handlers. */
typedef svn_error_t *(*cmd_handler_t)(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_ra_svn__list_t *params,
                                      ra_svn_driver_state_t *ds);

static const struct {
  const char *cmd;
  cmd_handler_t handler;
} ra_svn_edit_cmds[] = {
  { "change-file-prop", ra_svn_handle_change_file_prop },
  { "open-file",        ra_svn_handle_open_file },
  { "apply-textdelta",  ra_svn_handle_apply_textdelta },
  { "textdelta-chunk",  ra_svn_handle_textdelta_chunk },
  { "close-file",       ra_svn_handle_close_file },
  { "add-dir",          ra_svn_handle_add_dir },
  { "open-dir",         ra_svn_handle_open_dir },
  { "change-dir-prop",  ra_svn_handle_change_dir_prop },
  { "delete-entry",     ra_svn_handle_delete_entry },
  { "close-dir",        ra_svn_handle_close_dir },
  { "absent-dir",       ra_svn_handle_absent_dir },
  { "add-file",         ra_svn_handle_add_file },
  { "textdelta-end",    ra_svn_handle_textdelta_end },
  { "absent-file",      ra_svn_handle_absent_file },
  { "abort-edit",       ra_svn_handle_abort_edit },
  { "finish-replay",    ra_svn_handle_finish_replay },
  { "target-rev",       ra_svn_handle_target_rev },
  { "open-root",        ra_svn_handle_open_root },
  { "close-edit",       ra_svn_handle_close_edit },
  { NULL }
};

/* All editor commands are kept in a collision-free hash table. */

/* Hash table entry.
   It is similar to ra_svn_edit_cmds but uses our SVN string type. */
typedef struct cmd_t {
  svn_string_t cmd;
  cmd_handler_t handler;
} cmd_t;

/* The actual hash table.  It will be filled once before first usage.

   If you add more commands, you may have to tweak the table size to
   eliminate collisions.  Alternatively, you may modify the hash function.

   Be sure to initialize all elements with 0 as the has conflict detection
   will rely on it (see init_cmd_hash).
 */
#define CMD_HASH_SIZE 67
static cmd_t cmd_hash[CMD_HASH_SIZE] = { { { NULL } } };

/* Init flag that controls CMD_HASH's atomic initialization. */
static volatile svn_atomic_t cmd_hash_initialized = FALSE;

/* Super-fast hash function that works very well with the structure of our
   command words.  It produces no conflicts for them.

   Return the index within CMD_HASH that a command NAME of LEN chars would
   be found.  LEN > 0.
 */
static apr_size_t
cmd_hash_func(const char *name,
              apr_size_t len)
{
  apr_size_t value =     (apr_byte_t)(name[0] - 'a') % 8
                   + 1 * (apr_byte_t)(name[len - 1] - 'a') % 8
                   + 10 * (len - 7);
  return value % CMD_HASH_SIZE;
}

/* svn_atomic__init_once callback that fills the CMD_HASH table.  It will
   error out on hash collisions.  BATON and POOL are not used. */
static svn_error_t *
init_cmd_hash(void *baton,
              apr_pool_t *pool)
{
  int i;
  for (i = 0; ra_svn_edit_cmds[i].cmd; i++)
    {
      apr_size_t len = strlen(ra_svn_edit_cmds[i].cmd);
      apr_size_t value = cmd_hash_func(ra_svn_edit_cmds[i].cmd, len);
      SVN_ERR_ASSERT(cmd_hash[value].cmd.data == NULL);

      cmd_hash[value].cmd.data = ra_svn_edit_cmds[i].cmd;
      cmd_hash[value].cmd.len = len;
      cmd_hash[value].handler = ra_svn_edit_cmds[i].handler;
    }

  return SVN_NO_ERROR;
}

/* Return the command handler function for the command name CMD.
   Return NULL if no such handler exists */
static cmd_handler_t
cmd_lookup(const char *cmd)
{
  apr_size_t value;
  apr_size_t len = strlen(cmd);

  /* Malicious data that our hash function may not like? */
  if (len == 0)
    return NULL;

  /* Hash lookup. */
  value = cmd_hash_func(cmd, len);

  /* Hit? */
  if (cmd_hash[value].cmd.len != len)
    return NULL;

  if (memcmp(cmd_hash[value].cmd.data, cmd, len))
    return NULL;

  /* Yes! */
  return cmd_hash[value].handler;
}

static svn_error_t *blocked_write(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *cmd;
  svn_ra_svn__list_t *params;

  /* We blocked trying to send an error.  Read and discard an editing
   * command in order to avoid deadlock. */
  SVN_ERR(svn_ra_svn__read_tuple(conn, pool, "wl", &cmd, &params));
  if (strcmp(cmd, "abort-edit") == 0)
    {
      ds->done = TRUE;
      svn_ra_svn__set_block_handler(conn, NULL, NULL);
    }
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn_drive_editor2(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_delta_editor_t *editor,
                                      void *edit_baton,
                                      svn_boolean_t *aborted,
                                      svn_boolean_t for_replay)
{
  ra_svn_driver_state_t state;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *cmd;
  svn_error_t *err, *write_err;
  svn_ra_svn__list_t *params;

  SVN_ERR(svn_atomic__init_once(&cmd_hash_initialized, init_cmd_hash, NULL,
                                pool));

  state.editor = editor;
  state.edit_baton = edit_baton;
  state.tokens = svn_hash__make(pool);
  state.last_token = NULL;
  state.aborted = aborted;
  state.done = FALSE;
  state.pool = pool;
  state.file_pool = svn_pool_create(pool);
  state.file_refs = 0;
  state.for_replay = for_replay;

  while (!state.done)
    {
      svn_pool_clear(subpool);

      /* WRT to applying I/O limits, treat each editor command as a separate
       * protocol command. */
      svn_ra_svn__reset_command_io_counters(conn);
      if (editor)
        {
          cmd_handler_t handler;
          SVN_ERR(svn_ra_svn__read_tuple(conn, subpool, "wl", &cmd, &params));
          handler = cmd_lookup(cmd);

          if (handler)
            err = (*handler)(conn, subpool, params, &state);
          else if (strcmp(cmd, "failure") == 0)
            {
              /* While not really an editor command this can occur when
                reporter->finish_report() fails before the first editor
                command */
              if (aborted)
                *aborted = TRUE;
              err = svn_ra_svn__handle_failure_status(params);
              return svn_error_compose_create(
                                err,
                                editor->abort_edit(edit_baton, subpool));
            }
          else
            {
              err = svn_error_createf(SVN_ERR_RA_SVN_UNKNOWN_CMD, NULL,
                                      _("Unknown editor command '%s'"), cmd);
              err = svn_error_create(SVN_ERR_RA_SVN_CMD_ERR, err, NULL);
            }
        }
      else
        {
          const char* command = NULL;
          SVN_ERR(svn_ra_svn__read_command_only(conn, subpool, &command));
          if (strcmp(command, "close-edit") == 0)
            {
              state.done = TRUE;
              if (aborted)
                *aborted = FALSE;
              err = svn_ra_svn__write_cmd_response(conn, pool, "");
            }
          else
            err = NULL;
        }

      if (err && err->apr_err == SVN_ERR_RA_SVN_CMD_ERR)
        {
          if (aborted)
            *aborted = TRUE;
          if (!state.done)
            {
              /* Abort the edit and use non-blocking I/O to write the error. */
              if (editor)
                {
                  err = svn_error_compose_create(
                          err,
                          svn_error_trace(editor->abort_edit(edit_baton,
                                                             subpool)));
                }
              svn_ra_svn__set_block_handler(conn, blocked_write, &state);
            }
          write_err = svn_ra_svn__write_cmd_failure(
                          conn, subpool,
                          svn_ra_svn__locate_real_error_child(err));
          if (!write_err)
            write_err = svn_ra_svn__flush(conn, subpool);
          svn_ra_svn__set_block_handler(conn, NULL, NULL);
          svn_error_clear(err); /* We just sent this error */
          SVN_ERR(write_err);
          break;
        }
      SVN_ERR(err);
    }

  /* Read and discard editing commands until the edit is complete.
     Hopefully, the other side will call another editor command, run
     check_for_error, notice the error, write "abort-edit" at us, and
     throw the error up a few levels on its side (possibly even
     tossing it right back at us, which is why we can return
     SVN_NO_ERROR below).

     However, if the other side is way ahead of us, it might
     completely finish the edit (or sequence of edit/revprops, for
     "replay-range") before we send over our "failure".  So we should
     also stop if we see "success".  (Then the other side will try to
     interpret our "failure" as a command, which will itself fail...
     The net effect is that whatever error we wrote to the other side
     will be replaced with SVN_ERR_RA_SVN_UNKNOWN_CMD.)
   */
  while (!state.done)
    {
      svn_pool_clear(subpool);
      err = svn_ra_svn__read_tuple(conn, subpool, "wl", &cmd, &params);
      if (err && err->apr_err == SVN_ERR_RA_SVN_CONNECTION_CLOSED)
        {
          /* Other side disconnected; that's no error. */
          svn_error_clear(err);
          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }
      svn_error_clear(err);
      if (strcmp(cmd, "abort-edit") == 0
          || strcmp(cmd, "success") == 0)
        state.done = TRUE;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn_drive_editor(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const svn_delta_editor_t *editor,
                                     void *edit_baton,
                                     svn_boolean_t *aborted)
{
  return svn_ra_svn_drive_editor2(conn,
                                  pool,
                                  editor,
                                  edit_baton,
                                  aborted,
                                  FALSE);
}
