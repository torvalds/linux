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
 * @file svn_ra_svn.h
 * @brief libsvn_ra_svn functions used by the server
 */

#ifndef SVN_RA_SVN_H
#define SVN_RA_SVN_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>     /* for apr_file_t */
#include <apr_network_io.h>  /* for apr_socket_t */

#include "svn_types.h"
#include "svn_string.h"
#include "svn_config.h"
#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** The well-known svn port number. */
#define SVN_RA_SVN_PORT 3690

/** Currently-defined capabilities. */
#define SVN_RA_SVN_CAP_EDIT_PIPELINE "edit-pipeline"
#define SVN_RA_SVN_CAP_SVNDIFF1 "svndiff1"
#define SVN_RA_SVN_CAP_SVNDIFF2_ACCEPTED "accepts-svndiff2"
#define SVN_RA_SVN_CAP_ABSENT_ENTRIES "absent-entries"
/* maps to SVN_RA_CAPABILITY_COMMIT_REVPROPS: */
#define SVN_RA_SVN_CAP_COMMIT_REVPROPS "commit-revprops"
/* maps to SVN_RA_CAPABILITY_MERGEINFO: */
#define SVN_RA_SVN_CAP_MERGEINFO "mergeinfo"
/* maps to SVN_RA_CAPABILITY_DEPTH: */
#define SVN_RA_SVN_CAP_DEPTH "depth"
/* maps to SVN_RA_CAPABILITY_LOG_REVPROPS */
#define SVN_RA_SVN_CAP_LOG_REVPROPS "log-revprops"
/* maps to SVN_RA_CAPABILITY_PARTIAL_REPLAY */
#define SVN_RA_SVN_CAP_PARTIAL_REPLAY "partial-replay"
/* maps to SVN_RA_CAPABILITY_ATOMIC_REVPROPS */
#define SVN_RA_SVN_CAP_ATOMIC_REVPROPS "atomic-revprops"
/* maps to SVN_RA_CAPABILITY_INHERITED_PROPERTIES: */
#define SVN_RA_SVN_CAP_INHERITED_PROPS "inherited-props"
/* maps to SVN_RA_CAPABILITY_EPHEMERAL_TXNPROPS */
#define SVN_RA_SVN_CAP_EPHEMERAL_TXNPROPS "ephemeral-txnprops"
/* maps to SVN_RA_CAPABILITY_GET_FILE_REVS_REVERSE */
#define SVN_RA_SVN_CAP_GET_FILE_REVS_REVERSE "file-revs-reverse"
/* maps to SVN_RA_CAPABILITY_LIST */
#define SVN_RA_SVN_CAP_LIST "list"


/** ra_svn passes @c svn_dirent_t fields over the wire as a list of
 * words, these are the values used to represent each field.
 *
 * @defgroup ra_svn_dirent_fields Definitions of ra_svn dirent fields
 * @{
 */

/** The ra_svn way of saying @c SVN_DIRENT_KIND. */
#define SVN_RA_SVN_DIRENT_KIND "kind"

/** The ra_svn way of saying @c SVN_DIRENT_SIZE. */
#define SVN_RA_SVN_DIRENT_SIZE "size"

/** The ra_svn way of saying @c SVN_DIRENT_HAS_PROPS. */
#define SVN_RA_SVN_DIRENT_HAS_PROPS "has-props"

/** The ra_svn way of saying @c SVN_DIRENT_CREATED_REV. */
#define SVN_RA_SVN_DIRENT_CREATED_REV "created-rev"

/** The ra_svn way of saying @c SVN_DIRENT_TIME. */
#define SVN_RA_SVN_DIRENT_TIME "time"

/** The ra_svn way of saying @c SVN_DIRENT_LAST_AUTHOR. */
#define SVN_RA_SVN_DIRENT_LAST_AUTHOR "last-author"

/** @} */

/** A value used to indicate an optional number element in a tuple that was
 * not received.
 */
#define SVN_RA_SVN_UNSPECIFIED_NUMBER ~((apr_uint64_t) 0)

/** A specialized form of @c SVN_ERR to deal with errors which occur in an
 * svn_ra_svn_command_handler().
 *
 * An error returned with this macro will be passed back to the other side
 * of the connection.  Use this macro when performing the requested operation;
 * use the regular @c SVN_ERR when performing I/O with the client.
 */
#define SVN_CMD_ERR(expr)                                     \
  do {                                                        \
    svn_error_t *svn_err__temp = (expr);                      \
    if (svn_err__temp)                                        \
      return svn_error_create(SVN_ERR_RA_SVN_CMD_ERR,         \
                              svn_err__temp, NULL);           \
  } while (0)

/** an ra_svn connection. */
typedef struct svn_ra_svn_conn_st svn_ra_svn_conn_t;

/** Command handler, used by svn_ra_svn_handle_commands(). */
typedef svn_error_t *(*svn_ra_svn_command_handler)(svn_ra_svn_conn_t *conn,
                                                   apr_pool_t *pool,
                                                   apr_array_header_t *params,
                                                   void *baton);

/** Command table, used by svn_ra_svn_handle_commands().
 */
typedef struct svn_ra_svn_cmd_entry_t
{
  /** Name of the command */
  const char *cmdname;

  /** Handler for the command */
  svn_ra_svn_command_handler handler;

  /** Termination flag.  If set, command-handling will cease after
   * command is processed. */
  svn_boolean_t terminate;
} svn_ra_svn_cmd_entry_t;

/** Data types defined by the svn:// protocol.
 *
 * @since The typedef name is new in 1.10; the enumerators are not. */
typedef enum
{
  SVN_RA_SVN_NUMBER,
  SVN_RA_SVN_STRING,
  SVN_RA_SVN_WORD,
  SVN_RA_SVN_LIST
} svn_ra_svn_item_kind_t;

/** Memory representation of an on-the-wire data item. */
typedef struct svn_ra_svn_item_t
{
  /** Variant indicator. */
  svn_ra_svn_item_kind_t kind;

  /** Variant data. */
  union {
    apr_uint64_t number;
    svn_string_t *string;
    const char *word;

    /** Contains @c svn_ra_svn_item_t's. */
    apr_array_header_t *list;
  } u;
} svn_ra_svn_item_t;

typedef svn_error_t *(*svn_ra_svn_edit_callback)(void *baton);

/** Initialize a connection structure for the given socket or
 * input/output streams.
 *
 * Either @a sock or @a in_stream/@a out_stream must be set, not both.
 * @a compression_level specifies the desired network data compression
 * level from 0 (no compression) to 9 (best but slowest). The effect
 * of the parameter depends on the compression algorithm; for example,
 * it is used verbatim by zlib/deflate but ignored by LZ4.
 *
 * If @a zero_copy_limit is not 0, cached file contents smaller than the
 * given limit may be sent directly to the network socket.  Otherwise,
 * it will be copied into a temporary buffer before being forwarded to
 * the network stack.  Since the zero-copy code path has to enforce strict
 * time-outs, the receiver must be able to process @a zero_copy_limit
 * bytes within one second.  Even temporary failure to do so may cause
 * the server to cancel the respective operation with a time-out error.
 *
 * To reduce the overhead of checking for cancellation requests from the
 * data receiver, set @a error_check_interval to some non-zero value.
 * It defines the number of bytes that must have been sent since the last
 * check before the next check will be made.
 *
 * If @a max_in is not 0, error out and close the connection whenever more
 * than @a max_in bytes are received for a command (e.g. a client request).
 * If @a max_out is not 0, error out and close the connection whenever more
 * than @a max_out bytes have been send as response to some command.
 *
 * @note The limits enforced may vary slightly by +/- the I/O buffer size. 
 *
 * @note If @a out_stream is an wrapped apr_file_t* the backing file will be
 * used for some operations.
 *
 * Allocate the result in @a pool.
 *
 * @since New in 1.10
 */
svn_ra_svn_conn_t *svn_ra_svn_create_conn5(apr_socket_t *sock,
                                           svn_stream_t *in_stream,
                                           svn_stream_t *out_stream,
                                           int compression_level,
                                           apr_size_t zero_copy_limit,
                                           apr_size_t error_check_interval,
                                           apr_uint64_t max_in,
                                           apr_uint64_t max_out,
                                           apr_pool_t *result_pool);


/** Similar to svn_ra_svn_create_conn5() but with @a max_in and @a max_out
 * set to 0.
 *
 * @since New in 1.9
 * @deprecated Provided for backward compatibility with the 1.9 API.
 */
SVN_DEPRECATED
svn_ra_svn_conn_t *svn_ra_svn_create_conn4(apr_socket_t *sock,
                                           svn_stream_t *in_stream,
                                           svn_stream_t *out_stream,
                                           int compression_level,
                                           apr_size_t zero_copy_limit,
                                           apr_size_t error_check_interval,
                                           apr_pool_t *result_pool);


/** Similar to svn_ra_svn_create_conn4() but only supports apr_file_t handles
 * instead of the more generic streams.
 *
 * @since New in 1.8
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_ra_svn_conn_t *svn_ra_svn_create_conn3(apr_socket_t *sock,
                                           apr_file_t *in_file,
                                           apr_file_t *out_file,
                                           int compression_level,
                                           apr_size_t zero_copy_limit,
                                           apr_size_t error_check_interval,
                                           apr_pool_t *pool);

/** Similar to svn_ra_svn_create_conn3() but disables the zero copy code
 * path and sets the error checking interval to 0.
 *
 * @since New in 1.7.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_ra_svn_conn_t *
svn_ra_svn_create_conn2(apr_socket_t *sock,
                        apr_file_t *in_file,
                        apr_file_t *out_file,
                        int compression_level,
                        apr_pool_t *pool);

/** Similar to svn_ra_svn_create_conn2() but uses the default
 * compression level (#SVN_DELTA_COMPRESSION_LEVEL_DEFAULT) for network
 * transmissions.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_ra_svn_conn_t *
svn_ra_svn_create_conn(apr_socket_t *sock,
                       apr_file_t *in_file,
                       apr_file_t *out_file,
                       apr_pool_t *pool);

/** Add the capabilities in @a list to @a conn's capabilities.
 * @a list contains svn_ra_svn_item_t entries (which should be of type
 * SVN_RA_SVN_WORD; a malformed data error will result if any are not).
 *
 * This is idempotent: if a given capability was already set for
 * @a conn, it remains set.
 */
svn_error_t *
svn_ra_svn_set_capabilities(svn_ra_svn_conn_t *conn,
                            const apr_array_header_t *list);

/** Return @c TRUE if @a conn has the capability @a capability, or
 * @c FALSE if it does not. */
svn_boolean_t
svn_ra_svn_has_capability(svn_ra_svn_conn_t *conn,
                          const char *capability);

/** Return the data compression level to use for network transmissions.
 *
 * @since New in 1.7.
 */
int
svn_ra_svn_compression_level(svn_ra_svn_conn_t *conn);

/** Return the zero-copy data block limit to use for network
 * transmissions.
 *
 * @see http://en.wikipedia.org/wiki/Zero-copy
 *
 * @since New in 1.8.
 */
apr_size_t
svn_ra_svn_zero_copy_limit(svn_ra_svn_conn_t *conn);

/** Returns the remote address of the connection as a string, if known,
 *  or NULL if inapplicable. */
const char *
svn_ra_svn_conn_remote_host(svn_ra_svn_conn_t *conn);

/** Set @a *editor and @a *edit_baton to an editor which will pass editing
 * operations over the network, using @a conn and @a pool.
 *
 * Upon successful completion of the edit, the editor will invoke @a callback
 * with @a callback_baton as an argument.
 *
 * @note The @c copyfrom_path parameter passed to the @c add_file and
 * @c add_directory methods of the returned editor may be either a URL or a
 * relative path, and is transferred verbatim to the receiving end of the
 * connection. See svn_ra_svn_drive_editor2() for information on the
 * receiving end of the connection.
 */
void
svn_ra_svn_get_editor(const svn_delta_editor_t **editor,
                      void **edit_baton,
                      svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      svn_ra_svn_edit_callback callback,
                      void *callback_baton);

/** Receive edit commands over the network and use them to drive @a editor
 * with @a edit_baton.  On return, @a *aborted will be set if the edit was
 * aborted.  The drive can be terminated with a finish-replay command only
 * if @a for_replay is TRUE.
 *
 * @since New in 1.4.
 *
 * @note The @c copyfrom_path parameter passed to the @c add_file and
 * @c add_directory methods of the receiving editor will be canonicalized
 * either as a URL or as a relative path (starting with a slash) according
 * to which kind was sent by the driving end of the connection. See
 * svn_ra_svn_get_editor() for information on the driving end of the
 * connection.
 */
svn_error_t *
svn_ra_svn_drive_editor2(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const svn_delta_editor_t *editor,
                         void *edit_baton,
                         svn_boolean_t *aborted,
                         svn_boolean_t for_replay);

/** Like svn_ra_svn_drive_editor2, but with @a for_replay always FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_drive_editor(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const svn_delta_editor_t *editor,
                        void *edit_baton,
                        svn_boolean_t *aborted);

/** This function is only intended for use by svnserve.
 *
 * Perform CRAM-MD5 password authentication.  On success, return
 * SVN_NO_ERROR with *user set to the username and *success set to
 * TRUE.  On an error which can be reported to the client, report the
 * error and return SVN_NO_ERROR with *success set to FALSE.  On
 * communications failure, return an error.
 */
svn_error_t *
svn_ra_svn_cram_server(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       svn_config_t *pwdb,
                       const char **user,
                       svn_boolean_t *success);

/**
 * Get libsvn_ra_svn version information.
 * @since New in 1.1.
 */
const svn_version_t *
svn_ra_svn_version(void);

/**
 * @defgroup ra_svn_deprecated ra_svn low-level functions
 * @{
 */

/** Write a number over the net.
 *
 * Writes will be buffered until the next read or flush.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_number(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        apr_uint64_t number);

/** Write a string over the net.
 *
 * Writes will be buffered until the next read or flush.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_string(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const svn_string_t *str);

/** Write a cstring over the net.
 *
 * Writes will be buffered until the next read or flush.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_cstring(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const char *s);

/** Write a word over the net.
 *
 * Writes will be buffered until the next read or flush.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_word(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      const char *word);

/** Write a list of properties over the net.  @a props is allowed to be NULL,
 * in which case an empty list will be written out.
 *
 * @since New in 1.5.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_proplist(svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool,
                          apr_hash_t *props);

/** Begin a list.  Writes will be buffered until the next read or flush.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_start_list(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool);

/** End a list.  Writes will be buffered until the next read or flush.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_end_list(svn_ra_svn_conn_t *conn,
                    apr_pool_t *pool);

/** Flush the write buffer.
 *
 * Normally this shouldn't be necessary, since the write buffer is flushed
 * when a read is attempted.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_flush(svn_ra_svn_conn_t *conn,
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
     SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "r(!", rev));
     for (i = 0; i < n; i++)
       SVN_ERR(svn_ra_svn_write_word(conn, pool, words[i]));
     SVN_ERR(svn_ra_svn_write_tuple(conn, pool, "!)b", flag)); @endcode
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_tuple(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const char *fmt, ...);

/** Read an item from the network into @a *item.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_read_item(svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool,
                     svn_ra_svn_item_t **item);

/** Scan data on @a conn until we find something which looks like the
 * beginning of an svn server greeting (an open paren followed by a
 * whitespace character).  This function is appropriate for beginning
 * a client connection opened in tunnel mode, since people's dotfiles
 * sometimes write output to stdout.  It may only be called at the
 * beginning of a client connection.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_skip_leading_garbage(svn_ra_svn_conn_t *conn,
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
 * 'B' is similar to 'b', but may be used in the optional tuple specification.
 * It returns TRUE, FALSE, or SVN_RA_SVN_UNSPECIFIED_NUMBER.
 *
 * If an optional part of a tuple contains no data, 'r' values will be
 * set to @c SVN_INVALID_REVNUM, 'n' and 'B' values will be set to
 * SVN_RA_SVN_UNSPECIFIED_NUMBER, and 's', 'c', 'w', and 'l' values
 * will be set to @c NULL.  'b' may not appear inside an optional
 * tuple specification; use 'B' instead.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_parse_tuple(const apr_array_header_t *list,
                       apr_pool_t *pool,
                       const char *fmt, ...);

/** Read a tuple from the network and parse it as a tuple, using the
 * format string notation from svn_ra_svn_parse_tuple().
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_read_tuple(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      const char *fmt, ...);

/** Parse an array of @c svn_ra_svn_item_t structures as a list of
 * properties, storing the properties in a hash table.
 *
 * @since New in 1.5.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_parse_proplist(const apr_array_header_t *list,
                          apr_pool_t *pool,
                          apr_hash_t **props);

/** Read a command response from the network and parse it as a tuple, using
 * the format string notation from svn_ra_svn_parse_tuple().
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_read_cmd_response(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             const char *fmt, ...);

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
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_handle_commands2(svn_ra_svn_conn_t *conn,
                            apr_pool_t *pool,
                            const svn_ra_svn_cmd_entry_t *commands,
                            void *baton,
                            svn_boolean_t error_on_disconnect);

/** Similar to svn_ra_svn_handle_commands2 but @a error_on_disconnect
 * is always @c FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_handle_commands(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const svn_ra_svn_cmd_entry_t *commands,
                           void *baton);

/** Write a command over the network, using the same format string notation
 * as svn_ra_svn_write_tuple().
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_cmd(svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool,
                     const char *cmdname,
                     const char *fmt, ...);

/** Write a successful command response over the network, using the
 * same format string notation as svn_ra_svn_write_tuple().  Do not use
 * partial tuples with this function; if you need to use partial
 * tuples, just write out the "success" and argument tuple by hand.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_cmd_response(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const char *fmt, ...);

/** Write an unsuccessful command response over the network.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *             RA_SVN low-level functions are no longer considered public.
 */
SVN_DEPRECATED
svn_error_t *
svn_ra_svn_write_cmd_failure(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_error_t *err);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_RA_SVN_H */
