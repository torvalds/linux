/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_ra_svn_private.h
 * @brief Functions used by the server - Internal routines
 */

#ifndef SVN_RA_SVN_PRIVATE_H
#define SVN_RA_SVN_PRIVATE_H

#include "svn_ra_svn.h"
#include "svn_editor.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Memory representation of an on-the-wire data item. */
typedef struct svn_ra_svn__item_t svn_ra_svn__item_t;

/* A list of svn_ra_svn__item_t objects. */
typedef struct svn_ra_svn__list_t
{
  /* List contents (array).  May be NULL if NELTS is 0. */
  struct svn_ra_svn__item_t *items;

  /* Number of elements in ITEMS. */
  int nelts;
} svn_ra_svn__list_t;

/* List element access macro. */
#define SVN_RA_SVN__LIST_ITEM(list, idx) (list)->items[idx]

/** Memory representation of an on-the-wire data item. */
struct svn_ra_svn__item_t
{
  /** Variant indicator. */
  svn_ra_svn_item_kind_t kind;

  /** Variant data. */
  union {
    apr_uint64_t number;
    svn_string_t string;
    svn_string_t word;
    svn_ra_svn__list_t list;
  } u;
};

/** Command handler, used by svn_ra_svn__handle_commands(). */
typedef svn_error_t *(*svn_ra_svn__command_handler)(svn_ra_svn_conn_t *conn,
                                                    apr_pool_t *pool,
                                                    svn_ra_svn__list_t *params,
                                                    void *baton);

/** Command table, used by svn_ra_svn_handle_commands().
 */
typedef struct svn_ra_svn__cmd_entry_t
{
  /** Name of the command */
  const char *cmdname;

  /** Handler for the command */
  svn_ra_svn__command_handler handler;

  /** Only set when used through a deprecated API.
   * HANDLER is NULL in that case. */
  svn_ra_svn_command_handler deprecated_handler;

  /** Termination flag.  If set, command-handling will cease after
   * command is processed. */
  svn_boolean_t terminate;
} svn_ra_svn__cmd_entry_t;


/* Return a deep copy of the SOURCE array containing private API
 * svn_ra_svn__item_t SOURCE to public API *TARGET, allocating
 * sub-structures in RESULT_POOL. */
apr_array_header_t *
svn_ra_svn__to_public_array(const svn_ra_svn__list_t *source,
                            apr_pool_t *result_pool);

/* Deep copy contents from private API *SOURCE to public API *TARGET,
 * allocating sub-structures in RESULT_POOL. */
void
svn_ra_svn__to_public_item(svn_ra_svn_item_t *target,
                           const svn_ra_svn__item_t *source,
                           apr_pool_t *result_pool);

svn_ra_svn__list_t *
svn_ra_svn__to_private_array(const apr_array_header_t *source,
                             apr_pool_t *result_pool);

/* Deep copy contents from public API *SOURCE to private API *TARGET,
 * allocating sub-structures in RESULT_POOL. */
void
svn_ra_svn__to_private_item(svn_ra_svn__item_t *target,
                            const svn_ra_svn_item_t *source,
                            apr_pool_t *result_pool);

/** Add the capabilities in @a list to @a conn's capabilities.
 * @a list contains svn_ra_svn__item_t entries (which should be of type
 * SVN_RA_SVN_WORD; a malformed data error will result if any are not).
 *
 * This is idempotent: if a given capability was already set for
 * @a conn, it remains set.
 */
svn_error_t *
svn_ra_svn__set_capabilities(svn_ra_svn_conn_t *conn,
                             const svn_ra_svn__list_t *list);

/** Returns the preferred svndiff version to be used with connection @a conn.
 */
int
svn_ra_svn__svndiff_version(svn_ra_svn_conn_t *conn);


/**
 * Set the shim callbacks to be used by @a conn to @a shim_callbacks.
 */
svn_error_t *
svn_ra_svn__set_shim_callbacks(svn_ra_svn_conn_t *conn,
                               svn_delta_shim_callbacks_t *shim_callbacks);

/**
 * Return the memory pool used to allocate @a conn.
 */
apr_pool_t *
svn_ra_svn__get_pool(svn_ra_svn_conn_t *conn);

/**
 * @defgroup ra_svn_deprecated ra_svn low-level functions
 * @{
 */

/** Write a number over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *
svn_ra_svn__write_number(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         apr_uint64_t number);

/** Write a string over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *
svn_ra_svn__write_string(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const svn_string_t *str);

/** Write a cstring over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *
svn_ra_svn__write_cstring(svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool,
                          const char *s);

/** Write a word over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *
svn_ra_svn__write_word(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const char *word);

/** Write a boolean over the net.
 *
 * Writes will be buffered until the next read or flush.
 */
svn_error_t *
svn_ra_svn__write_boolean(svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool,
                          svn_boolean_t value);

/** Write a list of properties over the net.  @a props is allowed to be NULL,
 * in which case an empty list will be written out.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_ra_svn__write_proplist(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           apr_hash_t *props);

/** Begin a list.  Writes will be buffered until the next read or flush. */
svn_error_t *
svn_ra_svn__start_list(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool);

/** End a list.  Writes will be buffered until the next read or flush. */
svn_error_t *
svn_ra_svn__end_list(svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool);

/** Flush the write buffer.
 *
 * Normally this shouldn't be necessary, since the write buffer is flushed
 * when a read is attempted.
 */
svn_error_t *
svn_ra_svn__flush(svn_ra_svn_conn_t *conn,
                  apr_pool_t *pool);

/** Write a tuple, using a printf-like interface.
 *
 * The format string @a fmt may contain:
 *
 *@verbatim
     Spec  Argument type         Item type
     ----  --------------------  ---------
     n     apr_uint64_t          Number
     r     svn_revnum_t          Number
     s     const svn_string_t *  String
     c     const char *          String
     w     const char *          Word
     b     svn_boolean_t         Word ("true" or "false")
     (                           Begin tuple
     )                           End tuple
     ?                           Remaining elements optional
     ! (at beginning or end)     Suppress opening or closing of tuple
  @endverbatim
 *
 * Inside the optional part of a tuple, 'r' values may be @c
 * SVN_INVALID_REVNUM, 'n' values may be
 * SVN_RA_SVN_UNSPECIFIED_NUMBER, and 's', 'c', and 'w' values may be
 * @c NULL; in these cases no data will be written.  'b' and '(' may
 * not appear in the optional part of a tuple.  Either all or none of
 * the optional values should be valid.
 *
 * (If we ever have a need for an optional boolean value, we should
 * invent a 'B' specifier which stores a boolean into an int, using -1
 * for unspecified.  Right now there is no need for such a thing.)
 *
 * Use the '!' format specifier to write partial tuples when you have
 * to transmit an array or other unusual data.  For example, to write
 * a tuple containing a revision, an array of words, and a boolean:
 * @code
     SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "r(!", rev));
     for (i = 0; i < n; i++)
       SVN_ERR(svn_ra_svn__write_word(conn, pool, words[i]));
     SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!)b", flag)); @endcode
 */
svn_error_t *
svn_ra_svn__write_tuple(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const char *fmt, ...);

/** Read an item from the network into @a *item. */
svn_error_t *
svn_ra_svn__read_item(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      svn_ra_svn__item_t **item);

/** Scan data on @a conn until we find something which looks like the
 * beginning of an svn server greeting (an open paren followed by a
 * whitespace character).  This function is appropriate for beginning
 * a client connection opened in tunnel mode, since people's dotfiles
 * sometimes write output to stdout.  It may only be called at the
 * beginning of a client connection.
 */
svn_error_t *
svn_ra_svn__skip_leading_garbage(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool);

/** Parse an array of @c svn_sort__item_t structures as a tuple, using a
 * printf-like interface.  The format string @a fmt may contain:
 *
 *@verbatim
     Spec  Argument type          Item type
     ----  --------------------   ---------
     n     apr_uint64_t *         Number
     r     svn_revnum_t *         Number
     s     svn_string_t **        String
     c     const char **          String
     w     const char **          Word
     b     svn_boolean_t *        Word ("true" or "false")
     B     apr_uint64_t *         Word ("true" or "false")
     3     svn_tristate_t *       Word ("true" or "false")
     l     apr_array_header_t **  List
     (                            Begin tuple
     )                            End tuple
     ?                            Tuple is allowed to end here
  @endverbatim
 *
 * Note that a tuple is only allowed to end precisely at a '?', or at
 * the end of the specification.  So if @a fmt is "c?cc" and @a list
 * contains two elements, an error will result.
 *
 * '3' is similar to 'b', but may be used in the optional tuple specification.
 * It returns #svn_tristate_true, #svn_tristate_false or #svn_tristate_unknown.
 *
 * 'B' is similar to '3', but it returns @c TRUE, @c FALSE, or
 * #SVN_RA_SVN_UNSPECIFIED_NUMBER.  'B' is deprecated; new code should
 * use '3' instead.
 *
 * If an optional part of a tuple contains no data, 'r' values will be
 * set to @c SVN_INVALID_REVNUM; 'n' and 'B' values will be set to
 * #SVN_RA_SVN_UNSPECIFIED_NUMBER; 's', 'c', 'w', and 'l' values will
 * be set to @c NULL; '3' values will be set to #svn_tristate_unknown;
 * and 'b' values will be set to @c FALSE.
 */
svn_error_t *
svn_ra_svn__parse_tuple(const svn_ra_svn__list_t *list,
                        const char *fmt, ...);

/** Read a tuple from the network and parse it as a tuple, using the
 * format string notation from svn_ra_svn_parse_tuple().
 */
svn_error_t *
svn_ra_svn__read_tuple(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const char *fmt, ...);

/** Parse an array of @c svn_ra_svn__item_t structures as a list of
 * properties, storing the properties in a hash table.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_ra_svn__parse_proplist(const svn_ra_svn__list_t *list,
                           apr_pool_t *pool,
                           apr_hash_t **props);

/** Read a command response from the network and parse it as a tuple, using
 * the format string notation from svn_ra_svn_parse_tuple().
 */
svn_error_t *
svn_ra_svn__read_cmd_response(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const char *fmt, ...);

/** Check the receive buffer and socket of @a conn whether there is some
 * unprocessed incoming data without waiting for new data to come in.
 * If data is found, set @a *has_command to TRUE.  If the connection does
 * not contain any more data and has been closed, set @a *terminated to
 * TRUE.
 */
svn_error_t *
svn_ra_svn__has_command(svn_boolean_t *has_command,
                        svn_boolean_t *terminated,
                        svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool);

/** Accept a single command from @a conn and handle them according
 * to @a cmd_hash.  Command handlers will be passed @a conn, @a pool,
 * the parameters of the command, and @a baton.  @a *terminate will be
 * set if either @a error_on_disconnect is FALSE and the connection got
 * closed, or if the command being handled has the "terminate" flag set
 * in the command table.
 */
svn_error_t *
svn_ra_svn__handle_command(svn_boolean_t *terminate,
                           apr_hash_t *cmd_hash,
                           void *baton,
                           svn_ra_svn_conn_t *conn,
                           svn_boolean_t error_on_disconnect,
                           apr_pool_t *pool);

/** Accept commands over the network and handle them according to @a
 * commands.  Command handlers will be passed @a conn, a subpool of @a
 * pool (cleared after each command is handled), the parameters of the
 * command, and @a baton.  Commands will be accepted until a
 * terminating command is received (a command with "terminate" set in
 * the command table).  If a command handler returns an error wrapped
 * in SVN_RA_SVN_CMD_ERR (see the @c SVN_CMD_ERR macro), the error
 * will be reported to the other side of the connection and the
 * command loop will continue; any other kind of error (typically a
 * network or protocol error) is passed through to the caller.
 *
 * @since New in 1.6.
 *
 */
svn_error_t *
svn_ra_svn__handle_commands2(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             const svn_ra_svn__cmd_entry_t *commands,
                             void *baton,
                             svn_boolean_t error_on_disconnect);

/** Write a successful command response over the network, using the
 * same format string notation as svn_ra_svn_write_tuple().  Do not use
 * partial tuples with this function; if you need to use partial
 * tuples, just write out the "success" and argument tuple by hand.
 */
svn_error_t *
svn_ra_svn__write_cmd_response(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *fmt, ...);

/** Write an unsuccessful command response over the network.
 *
 * @note This does not clear @a err. */
svn_error_t *
svn_ra_svn__write_cmd_failure(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const svn_error_t *err);

/**
 * @}
 */

/**
 * @defgroup svn_commands sending ra_svn commands
 * @{
 */

/** Sets the target revision of connection @a conn to @a rev.  Use @a pool
 * for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_target_rev(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 svn_revnum_t rev);

/** Send a "open-root" command over connection @a conn.  Open the
 * repository root at revision @a rev and associate it with @a token.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_open_root(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                svn_revnum_t rev,
                                const svn_string_t *token);

/** Send a "delete-entry" command over connection @a conn.  Delete the
 * @a path at optional revision @a rev below @a parent_token.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_delete_entry(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool,
                                   const char *path,
                                   svn_revnum_t rev,
                                   const svn_string_t *parent_token);

/** Send a "add-dir" command over connection @a conn.  Add a new directory
 * node named @a path under the directory identified by @a parent_token.
 * Associate the new directory with the given @a token.  * @a copy_path
 * and @a copy_rev are optional and describe the copy source.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_add_dir(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const char *path,
                              const svn_string_t *parent_token,
                              const svn_string_t *token,
                              const char *copy_path,
                              svn_revnum_t copy_rev);

/** Send a "open-dir" command over connection @a conn.  Associate to
 * @a token the directory node named @a path under the directory
 * identified by @a parent_token in revision @a rev.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_open_dir(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path,
                               const svn_string_t *parent_token,
                               const svn_string_t *token,
                               svn_revnum_t rev);

/** Send a "change-dir-prop" command over connection @a conn.  Set the
 * property @a name to the optional @a value on the directory identified
 * to @a token.  Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_change_dir_prop(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_string_t *token,
                                      const char *name,
                                      const svn_string_t *value);

/** Send a "close-dir" command over connection @a conn.  Identify the node
 * to close with @a token.  The latter will then no longer be associated
 * with that node.  Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_close_dir(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                const svn_string_t *token);

/** Send a "absent-dir" command over connection @a conn.  Directory node
 * named @a path under the directory identified by @a parent_token is
 * absent.  Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_absent_dir(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 const char *path,
                                 const svn_string_t *parent_token);

/** Send a "add-file" command over connection @a conn.  Add a new file
 * node named @a path under the directory identified by @a parent_token.
 * Associate the new file with the given @a token.  * @a copy_path and
 * @a copy_rev are optional and describe the copy source.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_add_file(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path,
                               const svn_string_t *parent_token,
                               const svn_string_t *token,
                               const char *copy_path,
                               svn_revnum_t copy_rev);

/** Send a "open-file" command over connection @a conn.  Associate to
 * @a token the file node named @a path under the directory identified by
 * @a parent_token in revision @a rev.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_open_file(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                const char *path,
                                const svn_string_t *parent_token,
                                const svn_string_t *token,
                                svn_revnum_t rev);

/** Send a "change-file-prop" command over connection @a conn.  Set the
 * property @a name to the optional @a value on the file identified to
 * @a token.  Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_change_file_prop(svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool,
                                       const svn_string_t *token,
                                       const char *name,
                                       const svn_string_t *value);

/** Send a "close-dir" command over connection @a conn.  Identify the node
 * to close with @a token and provide an optional @a check_sum.  The token
 * will then no longer be associated with that node.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_close_file(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 const svn_string_t *token,
                                 const char *text_checksum);

/** Send a "absent-file" command over connection @a conn.  File node
 * named @a path in the directory identified by @a parent_token is
 * absent.  Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_absent_file(svn_ra_svn_conn_t *conn,
                                  apr_pool_t *pool,
                                  const char *path,
                                  const svn_string_t *parent_token);

/** Send a "apply-textdelta" command over connection @a conn.  Starts a
 * series of text deltas to be applied to the file identified by @a token.
 * Optionally, specify the file's current checksum in @a base_checksum.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_apply_textdelta(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_string_t *token,
                                      const char *base_checksum);

/** Send a "textdelta-chunk" command over connection @a conn.  Apply
 * textdelta @a chunk to the file identified by @a token.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_textdelta_chunk(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_string_t *token,
                                      const svn_string_t *chunk);

/** Send a "textdelta-end" command over connection @a conn.  Ends the
 * series of text deltas to be applied to the file identified by @a token.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_textdelta_end(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool,
                                    const svn_string_t *token);

/** Send a "close-edit" command over connection @a conn.  Ends the editor
 * drive (successfully).  Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_close_edit(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool);

/** Send a "abort-edit" command over connection @a conn.  Prematurely ends
 * the editor drive, e.g. due to some problem on the other side.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_abort_edit(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool);

/** Send a "set-path" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see set_path() in #svn_ra_reporter3_t for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_set_path(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path,
                               svn_revnum_t rev,
                               svn_boolean_t start_empty,
                               const char *lock_token,
                               svn_depth_t depth);

/** Send a "delete-path" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see delete_path() in #svn_ra_reporter3_t for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_delete_path(svn_ra_svn_conn_t *conn,
                                  apr_pool_t *pool,
                                  const char *path);

/** Send a "link-path" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see link_path() in #svn_ra_reporter3_t for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_link_path(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                const char *path,
                                const char *url,
                                svn_revnum_t rev,
                                svn_boolean_t start_empty,
                                const char *lock_token,
                                svn_depth_t depth);

/** Send a "finish-report" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see finish_report() in #svn_ra_reporter3_t for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_finish_report(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool);

/** Send a "abort-report" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see abort_report() in #svn_ra_reporter3_t for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_abort_report(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool);

/** Send a "reparent" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_reparent for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_reparent(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *url);

/** Send a "get-latest-rev" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_get_latest_revnum for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_get_latest_rev(svn_ra_svn_conn_t *conn,
                                     apr_pool_t *pool);

/** Send a "get-dated-rev" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_get_dated_revision for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_get_dated_rev(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool,
                                    apr_time_t tm);

/** Send a "change-rev-prop2" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * If @a dont_care is false then check that the old value matches
 * @a old_value. If @a dont_care is true then do not check the old
 * value; in this case @a old_value must be NULL.
 *
 * @see #svn_ra_change_rev_prop2 for the rest of the description.
 */
svn_error_t *
svn_ra_svn__write_cmd_change_rev_prop2(svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool,
                                       svn_revnum_t rev,
                                       const char *name,
                                       const svn_string_t *value,
                                       svn_boolean_t dont_care,
                                       const svn_string_t *old_value);

/** Send a "change-rev-prop" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_change_rev_prop for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_change_rev_prop(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      svn_revnum_t rev,
                                      const char *name,
                                      const svn_string_t *value);

/** Send a "rev-proplist" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_rev_proplist for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_rev_proplist(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool,
                                   svn_revnum_t rev);

/** Send a "rev-prop" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_rev_prop for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_rev_prop(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               svn_revnum_t rev,
                               const char *name);

/** Send a "get-file" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_get_file for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_get_file(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path,
                               svn_revnum_t rev,
                               svn_boolean_t props,
                               svn_boolean_t stream);

/** Send a "update" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_do_update3 for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_update(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_revnum_t rev,
                             const char *target,
                             svn_boolean_t recurse,
                             svn_depth_t depth,
                             svn_boolean_t send_copyfrom_args,
                             svn_boolean_t ignore_ancestry);

/** Send a "switch" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_do_switch3 for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_switch(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_revnum_t rev,
                             const char *target,
                             svn_boolean_t recurse,
                             const char *switch_url,
                             svn_depth_t depth,
                             svn_boolean_t send_copyfrom_args,
                             svn_boolean_t ignore_ancestry);

/** Send a "status" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_do_status2 for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_status(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             const char *target,
                             svn_boolean_t recurse,
                             svn_revnum_t rev,
                             svn_depth_t depth);

/** Send a "diff" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_do_diff3 for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_diff(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           svn_revnum_t rev,
                           const char *target,
                           svn_boolean_t recurse,
                           svn_boolean_t ignore_ancestry,
                           const char *versus_url,
                           svn_boolean_t text_deltas,
                           svn_depth_t depth);

/** Send a "check-path" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_check_path for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_check_path(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 const char *path,
                                 svn_revnum_t rev);

/** Send a "stat" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_stat for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_stat(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const char *path,
                           svn_revnum_t rev);

/** Send a "get-file-revs" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_get_file_revs2 for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_get_file_revs(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool,
                                    const char *path,
                                    svn_revnum_t start,
                                    svn_revnum_t end,
                                    svn_boolean_t include_merged_revisions);

/** Send a "lock" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_lock for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_lock(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const char *path,
                           const char *comment,
                           svn_boolean_t steal_lock,
                           svn_revnum_t revnum);

/** Send a "unlock" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_unlock for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_unlock(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             const char *path,
                             const svn_string_t *token,
                             svn_boolean_t break_lock);

/** Send a "get-lock" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_get_lock for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_get_lock(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path);

/** Send a "get-locks" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_get_locks2 for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_get_locks(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                const char *path,
                                svn_depth_t depth);

/** Send a "replay" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_replay for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_replay(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_revnum_t rev,
                             svn_revnum_t low_water_mark,
                             svn_boolean_t send_deltas);

/** Send a "replay-range" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_replay_range for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_replay_range(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool,
                                   svn_revnum_t start_revision,
                                   svn_revnum_t end_revision,
                                   svn_revnum_t low_water_mark,
                                   svn_boolean_t send_deltas);

/** Send a "get-deleted-rev" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_get_deleted_rev for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_get_deleted_rev(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const char *path,
                                      svn_revnum_t peg_revision,
                                      svn_revnum_t end_revision);

/** Send a "get-iprops" command over connection @a conn.
 * Use @a pool for allocations.
 *
 * @see #svn_ra_get_inherited_props for a description.
 */
svn_error_t *
svn_ra_svn__write_cmd_get_iprops(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 const char *path,
                                 svn_revnum_t revision);

/** Send a "finish-replay" command over connection @a conn.
 * Use @a pool for allocations.
 */
svn_error_t *
svn_ra_svn__write_cmd_finish_replay(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool);

/**
 * @}
 */

/**
 * @defgroup svn_send_data sending data structures over ra_svn
 * @{
 */

/** Send a changed path (as part of transmitting a log entry) over connection
 * @a conn.  Use @a pool for allocations.
 *
 * @see svn_log_changed_path2_t for a description of the other parameters.
 */
svn_error_t *
svn_ra_svn__write_data_log_changed_path(svn_ra_svn_conn_t *conn,
                                        apr_pool_t *pool,
                                        const svn_string_t *path,
                                        char action,
                                        const char *copyfrom_path,
                                        svn_revnum_t copyfrom_rev,
                                        svn_node_kind_t node_kind,
                                        svn_boolean_t text_modified,
                                        svn_boolean_t props_modified);

/** Send a the details of a log entry (as part of transmitting a log entry
 * and without revprops and changed paths) over connection @a conn.
 * Use @a pool for allocations.
 *
 * @a author, @a date and @a message have been extracted and removed from
 * the revprops to follow.  @a has_children is taken directly from the
 * #svn_log_entry_t struct.  @a revision is too, except when it equals
 * #SVN_INVALID_REVNUM.  In that case, @a revision must be 0 and
 * @a invalid_revnum be set to TRUE.  @a revprop_count is the number of
 * revprops that will follow in the revprops list.
 */
svn_error_t *
svn_ra_svn__write_data_log_entry(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 svn_revnum_t revision,
                                 const svn_string_t *author,
                                 const svn_string_t *date,
                                 const svn_string_t *message,
                                 svn_boolean_t has_children,
                                 svn_boolean_t invalid_revnum,
                                 unsigned revprop_count);

/** Send a directory entry @a dirent for @a path over connection @a conn.
 * Use @a pool for allocations.
 *
 * Depending on the flags in @a dirent_fields, only selected elements will
 * be transmitted.
 */
svn_error_t *
svn_ra_svn__write_dirent(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const char *path,
                         svn_dirent_t *dirent,
                         apr_uint32_t dirent_fields);

/**
 * @}
 */

/**
 * @defgroup svn_read_data reading data structures from ra_svn
 * @{
 */

/** Take the data tuple ITEMS received over ra_svn and convert it to the
 * a changed path (as part of receiving a log entry).
 *
 * @see svn_log_changed_path2_t for a description of the output parameters.
 */
svn_error_t *
svn_ra_svn__read_data_log_changed_entry(const svn_ra_svn__list_t *items,
                                        svn_string_t **cpath,
                                        const char **action,
                                        const char **copy_path,
                                        svn_revnum_t *copy_rev,
                                        const char **kind_str,
                                        apr_uint64_t *text_mods,
                                        apr_uint64_t *prop_mods);
/**
 * @}
 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RA_SVN_PRIVATE_H */
