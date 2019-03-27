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
 * @file svn_delta.h
 * @brief Delta-parsing
 */

/* ==================================================================== */



#ifndef SVN_DELTA_H
#define SVN_DELTA_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>  /* for apr_file_t */

#include "svn_types.h"
#include "svn_string.h"
#include "svn_io.h"
#include "svn_checksum.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** This compression level effectively disables data compression.
 * However, the data pre-processing costs may still not be zero.
 *
 * @since New in 1.7.
 */
#define SVN_DELTA_COMPRESSION_LEVEL_NONE 0

/** This is the maximum compression level we can pass to zlib.
 *
 * @since New in 1.7.
 */
#define SVN_DELTA_COMPRESSION_LEVEL_MAX 9

/** This is the default compression level we pass to zlib.  It
 * should be between 0 and 9, with higher numbers resulting in
 * better compression rates but slower operation.
 *
 * @since New in 1.7.
 */
#define SVN_DELTA_COMPRESSION_LEVEL_DEFAULT 5

/**
 * Get libsvn_delta version information.
 *
 * @since New in 1.1.
 */
const svn_version_t *
svn_delta_version(void);

/**
 * @defgroup delta_support Delta generation and handling
 *
 * @{
 */

/**  Text deltas.
 *
 * A text delta represents the difference between two strings of
 * bytes, the `source' string and the `target' string.  Given a source
 * string and a target string, we can compute a text delta; given a
 * source string and a delta, we can reconstruct the target string.
 * However, note that deltas are not reversible: you cannot always
 * reconstruct the source string given the target string and delta.
 *
 * Since text deltas can be very large, the interface here allows us
 * to produce and consume them in pieces.  Each piece, represented by
 * an #svn_txdelta_window_t structure, describes how to produce the
 * next section of the target string.
 *
 * To compute a new text delta:
 *
 * - We call svn_txdelta() on the streams we want to compare.  That
 *   returns us an #svn_txdelta_stream_t object.
 *
 * - We then call svn_txdelta_next_window() on the stream object
 *   repeatedly.  Each call returns a new #svn_txdelta_window_t
 *   object, which describes the next portion of the target string.
 *   When svn_txdelta_next_window() returns zero, we are done building
 *   the target string.
 *
 * @defgroup svn_delta_txt_delta Text deltas
 * @{
 */

/** Action codes for text delta instructions. */
enum svn_delta_action {
    /* Note: The svndiff implementation relies on the values assigned in
     * this enumeration matching the instruction encoding values. */

    /** Append the @a length bytes at @a offset in the source view to the
     * target.
     *
     * It must be the case that 0 <= @a offset < @a offset +
     * @a length <= size of source view.
     */
    svn_txdelta_source,

    /** Append the @a length bytes at @a offset in the target view, to the
     * target.
     *
     * It must be the case that 0 <= @a offset < current position in the
     * target view.
     *
     * However!  @a offset + @a length may be *beyond* the end of the existing
     * target data.  "Where the heck does the text come from, then?"
     * If you start at @a offset, and append @a length bytes one at a time,
     * it'll work out --- you're adding new bytes to the end at the
     * same rate you're reading them from the middle.  Thus, if your
     * current target text is "abcdefgh", and you get an #svn_txdelta_target
     * instruction whose @a offset is 6 and whose @a length is 7,
     * the resulting string is "abcdefghghghghg".  This trick is actually
     * useful in encoding long runs of consecutive characters, long runs
     * of CR/LF pairs, etc.
     */
    svn_txdelta_target,

    /** Append the @a length bytes at @a offset in the window's @a new string
     * to the target.
     *
     * It must be the case that 0 <= @a offset < @a offset +
     * @a length <= length of @a new.  Windows MUST use new data in ascending
     * order with no overlap at the moment; svn_txdelta_to_svndiff()
     * depends on this.
     */
    svn_txdelta_new
};

/** A single text delta instruction.  */
typedef struct svn_txdelta_op_t
{
  /** Action code of delta instruction */
  enum svn_delta_action action_code;
  /** Offset of delta, see #svn_delta_action for more details. */
  apr_size_t offset;
   /** Number of bytes of delta, see #svn_delta_action for more details. */
  apr_size_t length;
} svn_txdelta_op_t;


/** An #svn_txdelta_window_t object describes how to reconstruct a
 * contiguous section of the target string (the "target view") using a
 * specified contiguous region of the source string (the "source
 * view").  It contains a series of instructions which assemble the
 * new target string text by pulling together substrings from:
 *
 *   - the source view,
 *
 *   - the previously constructed portion of the target view,
 *
 *   - a string of new data contained within the window structure
 *
 * The source view must always slide forward from one window to the
 * next; that is, neither the beginning nor the end of the source view
 * may move to the left as we read from a window stream.  This
 * property allows us to apply deltas to non-seekable source streams
 * without making a full copy of the source stream.
 */
typedef struct svn_txdelta_window_t
{

  /** The offset of the source view for this window.  */
  svn_filesize_t sview_offset;

  /** The length of the source view for this window.  */
  apr_size_t sview_len;

  /** The length of the target view for this window, i.e. the number of
   * bytes which will be reconstructed by the instruction stream.  */
  apr_size_t tview_len;

  /** The number of instructions in this window.  */
  int num_ops;

  /** The number of svn_txdelta_source instructions in this window. If
   * this number is 0, we don't need to read the source in order to
   * reconstruct the target view.
   */
  int src_ops;

  /** The instructions for this window.  */
  const svn_txdelta_op_t *ops;

  /** New data, for use by any `svn_txdelta_new' instructions.  */
  const svn_string_t *new_data;

} svn_txdelta_window_t;

/**
 * Return a deep copy of @a window, allocated in @a pool.
 *
 * @since New in 1.3.
 */
svn_txdelta_window_t *
svn_txdelta_window_dup(const svn_txdelta_window_t *window,
                       apr_pool_t *pool);

/**
 * Compose two delta windows, yielding a third, allocated in @a pool.
 *
 * @since New in 1.4
 *
 */
svn_txdelta_window_t *
svn_txdelta_compose_windows(const svn_txdelta_window_t *window_A,
                            const svn_txdelta_window_t *window_B,
                            apr_pool_t *pool);

/**
 * Apply the instructions from @a window to a source view @a sbuf to
 *  produce a target view @a tbuf.
 *
 * @a sbuf is assumed to have @a window->sview_len bytes of data and
 * @a tbuf is assumed to have room for @a tlen bytes of output.  @a
 * tlen may be more than @a window->tview_len, so return the actual
 * number of bytes written.  @a sbuf is not touched and may be NULL if
 * @a window contains no source-copy operations. This is purely a
 * memory operation; nothing can go wrong as long as we have a valid
 * window.
 *
 * @since New in 1.4
 *
 * @since Since 1.9, @a tbuf may be NULL if @a *tlen is 0.
 */
void
svn_txdelta_apply_instructions(svn_txdelta_window_t *window,
                               const char *sbuf, char *tbuf,
                               apr_size_t *tlen);

/** A typedef for functions that consume a series of delta windows, for
 * use in caller-pushes interfaces.  Such functions will typically
 * apply the delta windows to produce some file, or save the windows
 * somewhere.  At the end of the delta window stream, you must call
 * this function passing zero for the @a window argument.
 */
typedef svn_error_t *(*svn_txdelta_window_handler_t)(
  svn_txdelta_window_t *window, void *baton);


/** This function will generate delta windows that turn @a source into
 * @a target, and pushing these windows into the @a handler window handler
 * callback (passing @a handler_baton to each invocation).
 *
 * If @a checksum is not NULL, then a checksum (of kind @a checksum_kind)
 * will be computed for the target stream, and placed into *checksum.
 *
 * If @a cancel_func is not NULL, then it should refer to a cancellation
 * function (along with @a cancel_baton).
 *
 * Results (the checksum) will be allocated from @a result_pool, and all
 * temporary allocations will be performed in @a scratch_pool.
 *
 * Note: this function replaces the combination of svn_txdelta() and
 *   svn_txdelta_send_txstream().
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_txdelta_run(svn_stream_t *source,
                svn_stream_t *target,
                svn_txdelta_window_handler_t handler,
                void *handler_baton,
                svn_checksum_kind_t checksum_kind,
                svn_checksum_t **checksum,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool);


/** A delta stream --- this is the hat from which we pull a series of
 * svn_txdelta_window_t objects, which, taken in order, describe the
 * entire target string.  This type is defined within libsvn_delta, and
 * opaque outside that library.
 */
typedef struct svn_txdelta_stream_t svn_txdelta_stream_t;


/** A typedef for a function that will set @a *window to the next
 * window from a #svn_txdelta_stream_t object.  If there are no more
 * delta windows, NULL will be used.  The returned window, if any,
 * will be allocated in @a pool.  @a baton is the baton specified
 * when the stream was created.
 *
 * @since New in 1.4.
 */
typedef svn_error_t *
(*svn_txdelta_next_window_fn_t)(svn_txdelta_window_t **window,
                                void *baton,
                                apr_pool_t *pool);

/** A typedef for a function that will return the md5 checksum of the
 * fulltext deltified by a #svn_txdelta_stream_t object.  Will
 * return NULL if the final null window hasn't yet been returned by
 * the stream.  The returned value will be allocated in the same pool
 * as the stream.  @a baton is the baton specified when the stream was
 * created.
 *
 * @since New in 1.4.
 */
typedef const unsigned char *
(*svn_txdelta_md5_digest_fn_t)(void *baton);

/** A typedef for a function that opens an #svn_txdelta_stream_t object,
 * allocated in @a result_pool.  @a baton is provided by the caller.
 * Any temporary allocations may be performed in @a scratch_pool.
 *
 * @since New in 1.10.
 */
typedef svn_error_t *
(*svn_txdelta_stream_open_func_t)(svn_txdelta_stream_t **txdelta_stream,
                                  void *baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

/** Create and return a generic text delta stream with @a baton, @a
 * next_window and @a md5_digest.  Allocate the new stream in @a
 * pool.
 *
 * @since New in 1.4.
 */
svn_txdelta_stream_t *
svn_txdelta_stream_create(void *baton,
                          svn_txdelta_next_window_fn_t next_window,
                          svn_txdelta_md5_digest_fn_t md5_digest,
                          apr_pool_t *pool);

/** Set @a *window to a pointer to the next window from the delta stream
 * @a stream.  When we have completely reconstructed the target string,
 * set @a *window to zero.
 *
 * The window will be allocated in @a pool.
 */
svn_error_t *
svn_txdelta_next_window(svn_txdelta_window_t **window,
                        svn_txdelta_stream_t *stream,
                        apr_pool_t *pool);


/** Return the md5 digest for the complete fulltext deltified by
 * @a stream, or @c NULL if @a stream has not yet returned its final
 * @c NULL window.  The digest is allocated in the same memory as @a
 * STREAM.
 */
const unsigned char *
svn_txdelta_md5_digest(svn_txdelta_stream_t *stream);

/** Set @a *stream to a pointer to a delta stream that will turn the byte
 * string from @a source into the byte stream from @a target.
 *
 * @a source and @a target are both readable generic streams.  When we call
 * svn_txdelta_next_window() on @a *stream, it will read from @a source and
 * @a target to gather as much data as it needs.  If @a calculate_checksum
 * is set, you may call svn_txdelta_md5_digest() to get an MD5 checksum
 * for @a target.
 *
 * Do any necessary allocation in a sub-pool of @a pool.
 *
 * @since New in 1.8.
 */
void
svn_txdelta2(svn_txdelta_stream_t **stream,
             svn_stream_t *source,
             svn_stream_t *target,
             svn_boolean_t calculate_checksum,
             apr_pool_t *pool);

/** Similar to svn_txdelta2 but always calculating the target checksum.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
void
svn_txdelta(svn_txdelta_stream_t **stream,
            svn_stream_t *source,
            svn_stream_t *target,
            apr_pool_t *pool);


/**
 * Return a writable stream which, when fed target data, will send
 * delta windows to @a handler/@a handler_baton which transform the
 * data in @a source to the target data.  As usual, the window handler
 * will receive a NULL window to signify the end of the window stream.
 * The stream handler functions will read data from @a source as
 * necessary.
 *
 * @since New in 1.1.
 */
svn_stream_t *
svn_txdelta_target_push(svn_txdelta_window_handler_t handler,
                        void *handler_baton,
                        svn_stream_t *source,
                        apr_pool_t *pool);


/** Send the contents of @a string to window-handler @a handler/@a baton.
 * This is effectively a 'copy' operation, resulting in delta windows that
 * make the target equivalent to the value of @a string.
 *
 * All temporary allocation is performed in @a pool.
 */
svn_error_t *
svn_txdelta_send_string(const svn_string_t *string,
                        svn_txdelta_window_handler_t handler,
                        void *handler_baton,
                        apr_pool_t *pool);

/** Send the contents of @a stream to window-handler @a handler/@a baton.
 * This is effectively a 'copy' operation, resulting in delta windows that
 * make the target equivalent to the stream.
 *
 * If @a digest is non-NULL, populate it with the md5 checksum for the
 * fulltext that was deltified (@a digest must be at least
 * @c APR_MD5_DIGESTSIZE bytes long).
 *
 * All temporary allocation is performed in @a pool.
 */
svn_error_t *
svn_txdelta_send_stream(svn_stream_t *stream,
                        svn_txdelta_window_handler_t handler,
                        void *handler_baton,
                        unsigned char *digest,
                        apr_pool_t *pool);

/** Send the contents of @a txstream to window-handler @a handler/@a baton.
 * Windows will be extracted from the stream and delivered to the handler.
 *
 * All temporary allocation is performed in @a pool.
 */
svn_error_t *
svn_txdelta_send_txstream(svn_txdelta_stream_t *txstream,
                          svn_txdelta_window_handler_t handler,
                          void *handler_baton,
                          apr_pool_t *pool);


/** Send the @a contents of length @a len as a txdelta against an empty
 * source directly to window-handler @a handler/@a handler_baton.
 *
 * All temporary allocation is performed in @a pool.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_txdelta_send_contents(const unsigned char *contents,
                          apr_size_t len,
                          svn_txdelta_window_handler_t handler,
                          void *handler_baton,
                          apr_pool_t *pool);

/** Prepare to apply a text delta.  @a source is a readable generic stream
 * yielding the source data, @a target is a writable generic stream to
 * write target data to, and allocation takes place in a sub-pool of
 * @a pool.  On return, @a *handler is set to a window handler function and
 * @a *handler_baton is set to the value to pass as the @a baton argument to
 * @a *handler.
 *
 * If @a result_digest is non-NULL, it points to APR_MD5_DIGESTSIZE bytes
 * of storage, and the final call to @a handler populates it with the
 * MD5 digest of the resulting fulltext.
 *
 * If @a error_info is non-NULL, it is inserted parenthetically into
 * the error string for any error returned by svn_txdelta_apply() or
 * @a *handler.  (It is normally used to provide path information,
 * since there's nothing else in the delta application's context to
 * supply a path for error messages.)
 *
 * @note To avoid lifetime issues, @a error_info is copied into
 * @a pool or a subpool thereof.
 */
void
svn_txdelta_apply(svn_stream_t *source,
                  svn_stream_t *target,
                  unsigned char *result_digest,
                  const char *error_info,
                  apr_pool_t *pool,
                  svn_txdelta_window_handler_t *handler,
                  void **handler_baton);




/*** Producing and consuming svndiff-format text deltas.  ***/

/** Prepare to produce an svndiff-format diff from text delta windows.
 * @a output is a writable generic stream to write the svndiff data to.
 * Allocation takes place in a sub-pool of @a pool.  On return, @a *handler
 * is set to a window handler function and @a *handler_baton is set to
 * the value to pass as the @a baton argument to @a *handler. The svndiff
 * version is @a svndiff_version. @a compression_level is the zlib
 * compression level from 0 (no compression) and 9 (maximum compression).
 *
 * @since New in 1.7.  Since 1.10, @a svndiff_version can be 2 for the
 * svndiff2 format.  @a compression_level is currently ignored if
 * @a svndiff_version is set to 2.
 */
void
svn_txdelta_to_svndiff3(svn_txdelta_window_handler_t *handler,
                        void **handler_baton,
                        svn_stream_t *output,
                        int svndiff_version,
                        int compression_level,
                        apr_pool_t *pool);

/** Similar to svn_txdelta_to_svndiff3(), but always using the SVN default
 * compression level (#SVN_DELTA_COMPRESSION_LEVEL_DEFAULT).
 *
 * @since New in 1.4.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
void
svn_txdelta_to_svndiff2(svn_txdelta_window_handler_t *handler,
                        void **handler_baton,
                        svn_stream_t *output,
                        int svndiff_version,
                        apr_pool_t *pool);

/** Similar to svn_txdelta_to_svndiff2, but always using svndiff
 * version 0.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
void
svn_txdelta_to_svndiff(svn_stream_t *output,
                       apr_pool_t *pool,
                       svn_txdelta_window_handler_t *handler,
                       void **handler_baton);

/** Return a readable generic stream which will produce svndiff-encoded
 * text delta from the delta stream @a txstream.  @a svndiff_version and
 * @a compression_level are same as in svn_txdelta_to_svndiff3().
 *
 * Allocate the stream in @a pool.
 *
 * @since New in 1.10.
 */
svn_stream_t *
svn_txdelta_to_svndiff_stream(svn_txdelta_stream_t *txstream,
                              int svndiff_version,
                              int compression_level,
                              apr_pool_t *pool);

/** Return a writable generic stream which will parse svndiff-format
 * data into a text delta, invoking @a handler with @a handler_baton
 * whenever a new window is ready.
 *
 * When the caller closes this stream, this will signal completion to
 * the window handler by invoking @a handler once more, passing zero for
 * the @c window argument.
 *
 * If @a error_on_early_close is @c TRUE, then attempt to avoid
 * signaling completion to the window handler if the delta was
 * incomplete. Specifically, attempting to close the stream will be
 * successful only if the data written to the stream consisted of one or
 * more complete windows of svndiff data and no extra bytes. Otherwise,
 * closing the stream will not signal completion to the window handler,
 * and will return a #SVN_ERR_SVNDIFF_UNEXPECTED_END error. Note that if
 * no data at all was written, the delta is considered incomplete.
 *
 * If @a error_on_early_close is @c FALSE, closing the stream will
 * signal completion to the window handler, regardless of how much data
 * was written, and discard any pending incomplete data.
 *
 * Allocate the stream in @a pool.
 */
svn_stream_t *
svn_txdelta_parse_svndiff(svn_txdelta_window_handler_t handler,
                          void *handler_baton,
                          svn_boolean_t error_on_early_close,
                          apr_pool_t *pool);

/**
 * Read and parse one delta window in svndiff format from the
 * readable stream @a stream and place it in @a *window, allocating
 * the result in @a pool.  The caller must take responsibility for
 * stripping off the four-byte 'SVN@<ver@>' header at the beginning of
 * the svndiff document before reading the first window, and must
 * provide the version number (the value of the fourth byte) to each
 * invocation of this routine with the @a svndiff_version argument.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_txdelta_read_svndiff_window(svn_txdelta_window_t **window,
                                svn_stream_t *stream,
                                int svndiff_version,
                                apr_pool_t *pool);

/**
 * Read and skip one delta window in svndiff format from the
 * file @a file.  @a pool is used for temporary allocations.  The
 * caller must take responsibility for stripping off the four-byte
 * 'SVN@<ver@>' header at the beginning of the svndiff document before
 * reading or skipping the first window, and must provide the version
 * number (the value of the fourth byte) to each invocation of this
 * routine with the @a svndiff_version argument.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_txdelta_skip_svndiff_window(apr_file_t *file,
                                int svndiff_version,
                                apr_pool_t *pool);

/** @} */


/** Traversing tree deltas.
 *
 * In Subversion, we've got various producers and consumers of tree
 * deltas.
 *
 * In processing a `commit' command:
 * - The client examines its working copy data, and produces a tree
 *   delta describing the changes to be committed.
 * - The client networking library consumes that delta, and sends them
 *   across the wire as an equivalent series of network requests (for
 *   example, to svnserve as an ra_svn protocol stream, or to an
 *   Apache httpd server as WebDAV commands)
 * - The server receives those requests and produces a tree delta ---
 *   hopefully equivalent to the one the client produced above.
 * - The Subversion server module consumes that delta and commits an
 *   appropriate transaction to the filesystem.
 *
 * In processing an `update' command, the process is reversed:
 * - The Subversion server module talks to the filesystem and produces
 *   a tree delta describing the changes necessary to bring the
 *   client's working copy up to date.
 * - The server consumes this delta, and assembles a reply
 *   representing the appropriate changes.
 * - The client networking library receives that reply, and produces a
 *   tree delta --- hopefully equivalent to the one the Subversion
 *   server produced above.
 * - The working copy library consumes that delta, and makes the
 *   appropriate changes to the working copy.
 *
 * The simplest approach would be to represent tree deltas using the
 * obvious data structure.  To do an update, the server would
 * construct a delta structure, and the working copy library would
 * apply that structure to the working copy; the network layer's job
 * would simply be to get the structure across the net intact.
 *
 * However, we expect that these deltas will occasionally be too large
 * to fit in a typical workstation's swap area.  For example, in
 * checking out a 200Mb source tree, the entire source tree is
 * represented by a single tree delta.  So it's important to handle
 * deltas that are too large to fit in swap all at once.
 *
 * So instead of representing the tree delta explicitly, we define a
 * standard way for a consumer to process each piece of a tree delta
 * as soon as the producer creates it.  The #svn_delta_editor_t
 * structure is a set of callback functions to be defined by a delta
 * consumer, and invoked by a delta producer.  Each invocation of a
 * callback function describes a piece of the delta --- a file's
 * contents changing, something being renamed, etc.
 *
 * @defgroup svn_delta_tree_deltas Tree deltas
 * @{
 */

/** A structure full of callback functions the delta source will invoke
 * as it produces the delta.
 *
 * @note Don't try to allocate one of these yourself.  Instead, always
 * use svn_delta_default_editor() or some other constructor, to avoid
 * backwards compatibility problems if the structure is extended in
 * future releases and to ensure that unused slots are filled in with
 * no-op functions.
 *
 * <h3>Function Usage</h3>
 *
 * Here's how to use these functions to express a tree delta.
 *
 * The delta consumer implements the callback functions described in
 * this structure, and the delta producer invokes them.  So the
 * caller (producer) is pushing tree delta data at the callee
 * (consumer).
 *
 * At the start of traversal, the consumer provides @a edit_baton, a
 * baton global to the entire delta edit.  If there is a target
 * revision that needs to be set for this operation, the producer
 * should call the @c set_target_revision function at this point.
 *
 * Next, if there are any tree deltas to express, the producer should
 * pass the @a edit_baton to the @c open_root function, to get a baton
 * representing root of the tree being edited.
 *
 * Most of the callbacks work in the obvious way:
 *
 *     @c delete_entry
 *     @c add_file
 *     @c add_directory
 *     @c open_file
 *     @c open_directory
 *
 * Each of these takes a directory baton, indicating the directory
 * in which the change takes place, and a @a path argument, giving the
 * path of the file, subdirectory, or directory entry to change.
 *
 * The @a path argument to each of the callbacks is relative to the
 * root of the edit.  Editors will usually want to join this relative
 * path with some base stored in the edit baton (e.g. a URL, or a
 * location in the OS filesystem).
 *
 * Since every call requires a parent directory baton, including
 * @c add_directory and @c open_directory, where do we ever get our
 * initial directory baton, to get things started?  The @c open_root
 * function returns a baton for the top directory of the change.  In
 * general, the producer needs to invoke the editor's @c open_root
 * function before it can get anything of interest done.
 *
 * While @c open_root provides a directory baton for the root of
 * the tree being changed, the @c add_directory and @c open_directory
 * callbacks provide batons for other directories.  Like the
 * callbacks above, they take a @a parent_baton and a relative path
 * @a path, and then return a new baton for the subdirectory being
 * created / modified --- @a child_baton.  The producer can then use
 * @a child_baton to make further changes in that subdirectory.
 *
 * So, if we already have subdirectories named `foo' and `foo/bar',
 * then the producer can create a new file named `foo/bar/baz.c' by
 * calling:
 *
 *    - @c open_root () --- yielding a baton @a root for the top directory
 *
 *    - @c open_directory (@a root, "foo") --- yielding a baton @a f for `foo'
 *
 *    - @c open_directory (@a f, "foo/bar") --- yielding a baton @a b for
 *      `foo/bar'
 *
 *    - @c add_file (@a b, "foo/bar/baz.c")
 *
 * When the producer is finished making changes to a directory, it
 * should call @c close_directory.  This lets the consumer do any
 * necessary cleanup, and free the baton's storage.
 *
 * The @c add_file and @c open_file callbacks each return a baton
 * for the file being created or changed.  This baton can then be
 * passed to @c apply_textdelta or @c apply_textdelta_stream to change
 * the file's contents, or @c change_file_prop to change the file's
 * properties.  When the producer is finished making changes to a
 * file, it should call @c close_file, to let the consumer clean up
 * and free the baton.
 *
 * The @c add_file and @c add_directory functions each take arguments
 * @a copyfrom_path and @a copyfrom_revision.  If @a copyfrom_path is
 * non-@c NULL, then @a copyfrom_path and @a copyfrom_revision indicate where
 * the file or directory should be copied from (to create the file
 * or directory being added).  In that case, @a copyfrom_path must be
 * either a path relative to the root of the edit, or a URI from the
 * repository being edited.  If @a copyfrom_path is @c NULL, then @a
 * copyfrom_revision must be #SVN_INVALID_REVNUM; it is invalid to
 * pass a mix of valid and invalid copyfrom arguments.
 *
 *
 * <h3>Function Call Ordering</h3>
 *
 * There are six restrictions on the order in which the producer
 * may use the batons:
 *
 * 1. The producer may call @c open_directory, @c add_directory,
 *    @c open_file, @c add_file at most once on any given directory
 *    entry.  @c delete_entry may be called at most once on any given
 *    directory entry and may later be followed by @c add_directory or
 *    @c add_file on the same directory entry.  @c delete_entry may
 *    not be called on any directory entry after @c open_directory,
 *    @c add_directory, @c open_file or @c add_file has been called on
 *    that directory entry.
 *
 * 2. The producer may not close a directory baton until it has
 *    closed all batons for its subdirectories.
 *
 * 3. When a producer calls @c open_directory or @c add_directory,
 *    it must specify the most recently opened of the currently open
 *    directory batons.  Put another way, the producer cannot have
 *    two sibling directory batons open at the same time.
 *
 * 4. A producer must call @c change_dir_prop on a directory either
 *    before opening any of the directory's subdirs or after closing
 *    them, but not in the middle.
 *
 * 5. When the producer calls @c open_file or @c add_file, either:
 *
 *    (a) The producer must follow with any changes to the file
 *    (@c change_file_prop and/or @c apply_textdelta /
 *    @c apply_textdelta_stream, as applicable), followed by
 *    a @c close_file call, before issuing any other file or
 *    directory calls, or
 *
 *    (b) The producer must follow with a @c change_file_prop call if
 *    it is applicable, before issuing any other file or directory
 *    calls; later, after all directory batons including the root
 *    have been closed, the producer must issue @c apply_textdelta /
 *    @c apply_textdelta_stream and @c close_file calls.
 *
 * 6. When the producer calls @c apply_textdelta, it must make all of
 *    the window handler calls (including the @c NULL window at the
 *    end) before issuing any other #svn_delta_editor_t calls.
 *
 * So, the producer needs to use directory and file batons as if it
 * is doing a single depth-first traversal of the tree, with the
 * exception that the producer may keep file batons open in order to
 * make @c apply_textdelta / @c apply_textdelta_stream calls at the end.
 *
 *
 * <h3>Pool Usage</h3>
 *
 * Many editor functions are invoked multiple times, in a sequence
 * determined by the editor "driver". The driver is responsible for
 * creating a pool for use on each iteration of the editor function,
 * and clearing that pool between each iteration. The driver passes
 * the appropriate pool on each function invocation.
 *
 * Based on the requirement of calling the editor functions in a
 * depth-first style, it is usually customary for the driver to similarly
 * nest the pools. However, this is only a safety feature to ensure
 * that pools associated with deeper items are always cleared when the
 * top-level items are also cleared. The interface does not assume, nor
 * require, any particular organization of the pools passed to these
 * functions. In fact, if "postfix deltas" are used for files, the file
 * pools definitely need to live outside the scope of their parent
 * directories' pools.
 *
 * Note that close_directory can be called *before* a file in that
 * directory has been closed. That is, the directory's baton is
 * closed before the file's baton. The implication is that
 * @c apply_textdelta / @c apply_textdelta_stream and @c close_file
 * should not refer to a parent directory baton UNLESS the editor has
 * taken precautions to allocate it in a pool of the appropriate
 * lifetime (the @a dir_pool passed to @c open_directory and
 * @c add_directory definitely does not have the proper lifetime).
 * In general, it is recommended to simply avoid keeping a parent
 * directory baton in a file baton.
 *
 *
 * <h3>Errors</h3>
 *
 * At least one implementation of the editor interface is
 * asynchronous; an error from one operation may be detected some
 * number of operations later.  As a result, an editor driver must not
 * assume that an error from an editing function resulted from the
 * particular operation being detected.  Moreover, once an editing
 * function (including @c close_edit) returns an error, the edit is
 * dead; the only further operation which may be called on the editor
 * is @c abort_edit.
 */
typedef struct svn_delta_editor_t
{
  /** Set the target revision for this edit to @a target_revision.  This
   * call, if used, should precede all other editor calls.
   *
   * @note This is typically used only for server->client update-type
   * operations.  It doesn't really make much sense for commit-type
   * operations, because the revision of a commit isn't known until
   * the commit is finalized.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*set_target_revision)(void *edit_baton,
                                      svn_revnum_t target_revision,
                                      apr_pool_t *scratch_pool);

  /** Set @a *root_baton to a baton for the top directory of the change.
   * (This is the top of the subtree being changed, not necessarily
   * the root of the filesystem.)  As with any other directory baton, the
   * producer should call @c close_directory on @a root_baton when done.
   * And as with other @c open_* calls, the @a base_revision here is the
   * current revision of the directory (before getting bumped up to the
   * new target revision set with @c set_target_revision).
   *
   * Allocations for the returned @a root_baton should be performed in
   * @a result_pool. It is also typical to (possibly) save this pool for
   * later usage by @c close_directory.
   */
  svn_error_t *(*open_root)(void *edit_baton,
                            svn_revnum_t base_revision,
                            apr_pool_t *result_pool,
                            void **root_baton);


  /** Remove the directory entry at @a path, a child of the directory
   * represented by @a parent_baton.  If @a revision is a valid
   * revision number, it is used as a sanity check to ensure that you
   * are really removing the revision of @a path that you think you are.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   *
   * @note The @a revision parameter is typically used only for
   * client->server commit-type operations, allowing the server to
   * verify that it is deleting what the client thinks it should be
   * deleting.  It only really makes sense in the opposite direction
   * (during server->client update-type operations) when the trees
   * whose delta is being described are ancestrally related (that is,
   * one tree is an ancestor of the other).
   */
  svn_error_t *(*delete_entry)(const char *path,
                               svn_revnum_t revision,
                               void *parent_baton,
                               apr_pool_t *scratch_pool);


  /** We are going to add a new subdirectory at @a path, a child of
   * the directory represented by @a parent_baton.  We will use
   * the value this callback stores in @a *child_baton as the
   * parent baton for further changes in the new subdirectory.
   *
   * If @a copyfrom_path is non-@c NULL, this add has history (i.e., is a
   * copy), and the origin of the copy may be recorded as
   * @a copyfrom_path under @a copyfrom_revision.
   *
   * Allocations for the returned @a child_baton should be performed in
   * @a result_pool. It is also typical to (possibly) save this pool for
   * later usage by @c close_directory.
   */
  svn_error_t *(*add_directory)(const char *path,
                                void *parent_baton,
                                const char *copyfrom_path,
                                svn_revnum_t copyfrom_revision,
                                apr_pool_t *result_pool,
                                void **child_baton);

  /** We are going to make changes in the subdirectory at @a path, a
   * child of the directory represented by @a parent_baton.
   * The callback must store a value in @a *child_baton that
   * should be used as the parent baton for subsequent changes in this
   * subdirectory.  If a valid revnum, @a base_revision is the current
   * revision of the subdirectory.
   *
   * Allocations for the returned @a child_baton should be performed in
   * @a result_pool. It is also typical to (possibly) save this pool for
   * later usage by @c close_directory.
   */
  svn_error_t *(*open_directory)(const char *path,
                                 void *parent_baton,
                                 svn_revnum_t base_revision,
                                 apr_pool_t *result_pool,
                                 void **child_baton);

  /** Change the value of a directory's property.
   * - @a dir_baton specifies the directory whose property should change.
   * - @a name is the name of the property to change.
   * - @a value is the new (final) value of the property, or @c NULL if the
   *   property should be removed altogether.
   *
   * The callback is guaranteed to be called exactly once for each property
   * whose value differs between the start and the end of the edit.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*change_dir_prop)(void *dir_baton,
                                  const char *name,
                                  const svn_string_t *value,
                                  apr_pool_t *scratch_pool);

  /** We are done processing a subdirectory, whose baton is @a dir_baton
   * (set by @c add_directory or @c open_directory).  We won't be using
   * the baton any more, so whatever resources it refers to may now be
   * freed.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*close_directory)(void *dir_baton,
                                  apr_pool_t *scratch_pool);


  /** In the directory represented by @a parent_baton, indicate that
   * @a path is present as a subdirectory in the edit source, but
   * cannot be conveyed to the edit consumer.  Currently, this would
   * only occur because of authorization restrictions, but may change
   * in the future.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*absent_directory)(const char *path,
                                   void *parent_baton,
                                   apr_pool_t *scratch_pool);

  /** We are going to add a new file at @a path, a child of the
   * directory represented by @a parent_baton.  The callback can
   * store a baton for this new file in @a **file_baton; whatever value
   * it stores there should be passed through to @c apply_textdelta or
   * @c apply_textdelta_stream.
   *
   * If @a copyfrom_path is non-@c NULL, this add has history (i.e., is a
   * copy), and the origin of the copy may be recorded as
   * @a copyfrom_path under @a copyfrom_revision.
   *
   * Allocations for the returned @a file_baton should be performed in
   * @a result_pool. It is also typical to save this pool for later usage
   * by @c apply_textdelta and possibly @c close_file.
   *
   * @note Because the editor driver could be employing the "postfix
   * deltas" paradigm, @a result_pool could potentially be relatively
   * long-lived.  Every file baton created by the editor for a given
   * editor drive might be resident in memory similtaneously.  Editor
   * implementations should ideally keep their file batons as
   * conservative (memory-usage-wise) as possible, and use @a result_pool
   * only for those batons.  (Consider using a subpool of @a result_pool
   * for scratch work, destroying the subpool before exiting this
   * function's implementation.)
   */
  svn_error_t *(*add_file)(const char *path,
                           void *parent_baton,
                           const char *copyfrom_path,
                           svn_revnum_t copyfrom_revision,
                           apr_pool_t *result_pool,
                           void **file_baton);

  /** We are going to make changes to a file at @a path, a child of the
   * directory represented by @a parent_baton.
   *
   * The callback can store a baton for this new file in @a **file_baton;
   * whatever value it stores there should be passed through to
   * @c apply_textdelta or @c apply_textdelta_stream.  If a valid revnum,
   * @a base_revision is the current revision of the file.
   *
   * Allocations for the returned @a file_baton should be performed in
   * @a result_pool. It is also typical to save this pool for later usage
   * by @c apply_textdelta and possibly @c close_file.
   *
   * @note See note about memory usage on @a add_file, which also
   * applies here.
   */
  svn_error_t *(*open_file)(const char *path,
                            void *parent_baton,
                            svn_revnum_t base_revision,
                            apr_pool_t *result_pool,
                            void **file_baton);

  /** Apply a text delta, yielding the new revision of a file.
   *
   * @a file_baton indicates the file we're creating or updating, and the
   * ancestor file on which it is based; it is the baton set by some
   * prior @c add_file or @c open_file callback.
   *
   * The callback should set @a *handler to a text delta window
   * handler; we will then call @a *handler on successive text
   * delta windows as we receive them.  The callback should set
   * @a *handler_baton to the value we should pass as the @a baton
   * argument to @a *handler. These values should be allocated within
   * @a result_pool.
   *
   * @a base_checksum is the hex MD5 digest for the base text against
   * which the delta is being applied; it is ignored if NULL, and may
   * be ignored even if not NULL.  If it is not ignored, it must match
   * the checksum of the base text against which svndiff data is being
   * applied; if it does not, @c apply_textdelta or the @a *handler call
   * which detects the mismatch will return the error
   * SVN_ERR_CHECKSUM_MISMATCH (if there is no base text, there may
   * still be an error if @a base_checksum is neither NULL nor the hex
   * MD5 checksum of the empty string).
   */
  svn_error_t *(*apply_textdelta)(void *file_baton,
                                  const char *base_checksum,
                                  apr_pool_t *result_pool,
                                  svn_txdelta_window_handler_t *handler,
                                  void **handler_baton);

  /** Change the value of a file's property.
   * - @a file_baton specifies the file whose property should change.
   * - @a name is the name of the property to change.
   * - @a value is the new (final) value of the property, or @c NULL if the
   *   property should be removed altogether.
   *
   * The callback is guaranteed to be called exactly once for each property
   * whose value differs between the start and the end of the edit.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*change_file_prop)(void *file_baton,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *scratch_pool);

  /** We are done processing a file, whose baton is @a file_baton (set by
   * @c add_file or @c open_file).  We won't be using the baton any
   * more, so whatever resources it refers to may now be freed.
   *
   * @a text_checksum is the hex MD5 digest for the fulltext that
   * resulted from a delta application, see @c apply_textdelta and
   * @c apply_textdelta_stream.  The checksum is ignored if NULL.
   * If not null, it is compared to the checksum of the new fulltext,
   * and the error SVN_ERR_CHECKSUM_MISMATCH is returned if they do
   * not match.  If there is no new fulltext, @a text_checksum is ignored.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*close_file)(void *file_baton,
                             const char *text_checksum,
                             apr_pool_t *scratch_pool);

  /** In the directory represented by @a parent_baton, indicate that
   * @a path is present as a file in the edit source, but cannot be
   * cannot be conveyed to the edit consumer.  Currently, this would
   * only occur because of authorization restrictions, but may change
   * in the future.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*absent_file)(const char *path,
                              void *parent_baton,
                              apr_pool_t *scratch_pool);

  /** All delta processing is done.  Call this, with the @a edit_baton for
   * the entire edit.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*close_edit)(void *edit_baton,
                             apr_pool_t *scratch_pool);

  /** The editor-driver has decided to bail out.  Allow the editor to
   * gracefully clean up things if it needs to.
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   */
  svn_error_t *(*abort_edit)(void *edit_baton,
                             apr_pool_t *scratch_pool);

  /** Apply a text delta stream, yielding the new revision of a file.
   * This callback operates on the passed-in @a editor instance.
   *
   * @a file_baton indicates the file we're creating or updating, and the
   * ancestor file on which it is based; it is the baton set by some
   * prior @c add_file or @c open_file callback.
   *
   * @a open_func is a function that opens a #svn_txdelta_stream_t object.
   * @a open_baton is provided by the caller.
   *
   * @a base_checksum is the hex MD5 digest for the base text against
   * which the delta is being applied; it is ignored if NULL, and may
   * be ignored even if not NULL.  If it is not ignored, it must match
   * the checksum of the base text against which svndiff data is being
   * applied; if it does not, @c apply_textdelta_stream call which detects
   * the mismatch will return the error #SVN_ERR_CHECKSUM_MISMATCH
   * (if there is no base text, there may still be an error if
   * @a base_checksum is neither NULL nor the hex MD5 checksum of the
   * empty string).
   *
   * Any temporary allocations may be performed in @a scratch_pool.
   *
   * @since New in 1.10.
   */
  svn_error_t *(*apply_textdelta_stream)(
    const struct svn_delta_editor_t *editor,
    void *file_baton,
    const char *base_checksum,
    svn_txdelta_stream_open_func_t open_func,
    void *open_baton,
    apr_pool_t *scratch_pool);

  /* Be sure to update svn_delta_get_cancellation_editor() and
   * svn_delta_default_editor() if you add a new callback here. */
} svn_delta_editor_t;


/** Return a default delta editor template, allocated in @a pool.
 *
 * The editor functions in the template do only the most basic
 * baton-swapping: each editor function that produces a baton does so
 * by copying its incoming baton into the outgoing baton reference.
 *
 * This editor is not intended to be useful by itself, but is meant to
 * be the basis for a useful editor.  After getting a default editor,
 * you substitute in your own implementations for the editor functions
 * you care about.  The ones you don't care about, you don't have to
 * implement -- you can rely on the template's implementation to
 * safely do nothing of consequence.
 */
svn_delta_editor_t *
svn_delta_default_editor(apr_pool_t *pool);

/** A text-delta window handler which does nothing.
 *
 * Editors can return this handler from @c apply_textdelta if they don't
 * care about text delta windows.
 */
svn_error_t *
svn_delta_noop_window_handler(svn_txdelta_window_t *window,
                              void *baton);

/** Set @a *editor and @a *edit_baton to a cancellation editor that
 * wraps @a wrapped_editor and @a wrapped_baton.
 *
 * The @a editor will call @a cancel_func with @a cancel_baton when each of
 * its functions is called, continuing on to call the corresponding wrapped
 * function if @a cancel_func returns #SVN_NO_ERROR.
 *
 * If @a cancel_func is @c NULL, set @a *editor to @a wrapped_editor and
 * @a *edit_baton to @a wrapped_baton.
 */
svn_error_t *
svn_delta_get_cancellation_editor(svn_cancel_func_t cancel_func,
                                  void *cancel_baton,
                                  const svn_delta_editor_t *wrapped_editor,
                                  void *wrapped_baton,
                                  const svn_delta_editor_t **editor,
                                  void **edit_baton,
                                  apr_pool_t *pool);

/** Set @a *editor and @a *edit_baton to an depth-based filtering
 * editor that wraps @a wrapped_editor and @a wrapped_baton.
 *
 * The @a editor will track the depth of this drive against the @a
 * requested_depth, taking into account whether not the edit drive is
 * making use of a target (via @a has_target), and forward editor
 * calls which operate "within" the request depth range through to @a
 * wrapped_editor.
 *
 * @a requested_depth must be one of the following depth values:
 * #svn_depth_infinity, #svn_depth_empty, #svn_depth_files,
 * #svn_depth_immediates, or #svn_depth_unknown.
 *
 * If filtering is deemed unnecessary (or if @a requested_depth is
 * #svn_depth_unknown), @a *editor and @a *edit_baton will be set to @a
 * wrapped_editor and @a wrapped_baton, respectively; otherwise,
 * they'll be set to new objects allocated from @a pool.
 *
 * @note Because the svn_delta_editor_t interface's @c delete_entry()
 * function doesn't carry node kind information, a depth-based
 * filtering editor being asked to filter for #svn_depth_files but
 * receiving a @c delete_entry() call on an immediate child of the
 * editor's target is unable to know if that deletion should be
 * allowed or filtered out -- a delete of a top-level file is okay in
 * this case, a delete of a top-level subdirectory is not.  As such,
 * this filtering editor takes a conservative approach, and ignores
 * top-level deletion requests when filtering for #svn_depth_files.
 * Fortunately, most non-depth-aware (pre-1.5) Subversion editor
 * drivers can be told to drive non-recursively (where non-recursive
 * means essentially #svn_depth_files), which means they won't
 * transmit out-of-scope editor commands anyway.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_delta_depth_filter_editor(const svn_delta_editor_t **editor,
                              void **edit_baton,
                              const svn_delta_editor_t *wrapped_editor,
                              void *wrapped_edit_baton,
                              svn_depth_t requested_depth,
                              svn_boolean_t has_target,
                              apr_pool_t *pool);

/** @} */


/** Path-based editor drives.
 *
 * @defgroup svn_delta_path_delta_drivers Path-based delta drivers
 * @{
 */

/** Callback function type for svn_delta_path_driver().
 *
 * The handler of this callback is given the callback baton @a
 * callback_baton, @a path which is a relpath relative to the
 * root of the edit, and the @a parent_baton which represents
 * path's parent directory as created by the editor passed to
 * svn_delta_path_driver().
 *
 * If @a path represents a directory, the handler must return a @a
 * *dir_baton for @a path, generated from the same editor (so that the
 * driver can later close that directory).
 *
 * If, however, @a path represents a file, the handler should NOT
 * return any file batons.  It can close any opened or added files
 * immediately, or delay that close until the end of the edit when
 * svn_delta_path_driver() returns.
 *
 * Finally, if @a parent_baton is @c NULL, then the root of the edit
 * is also one of the paths passed to svn_delta_path_driver().  The
 * handler of this callback must call the editor's open_root()
 * function and return the top-level root dir baton in @a *dir_baton.
 */
typedef svn_error_t *(*svn_delta_path_driver_cb_func_t)(
  void **dir_baton,
  void *parent_baton,
  void *callback_baton,
  const char *path,
  apr_pool_t *pool);


/** Drive @a editor (with its @a edit_baton) to visit each path in @a paths.
 * As each path is hit as part of the editor drive, use
 * @a callback_func and @a callback_baton to allow the caller to handle
 * the portion of the editor drive related to that path.
 *
 * Each path in @a paths is a (const char *) relpath, relative
 * to the root path of the @a edit. The editor drive will be
 * performed in the same order as @a paths. The paths should be sorted
 * using something like svn_sort_compare_paths to ensure that a depth-first
 * pattern is observed for directory/file baton creation. If @a sort_paths
 * is set, the function will sort the paths for you. Some callers may need
 * further customization of the order (ie. libsvn_delta/compat.c).
 *
 * Use @a scratch_pool for all necessary allocations.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_delta_path_driver2(const svn_delta_editor_t *editor,
                       void *edit_baton,
                       const apr_array_header_t *paths,
                       svn_boolean_t sort_paths,
                       svn_delta_path_driver_cb_func_t callback_func,
                       void *callback_baton,
                       apr_pool_t *scratch_pool);


/** Similar to svn_delta_path_driver2, but takes an (unused) revision,
 * and will sort the provided @a paths using svn_sort_compare_paths.
 *
 * @note In versions prior to 1.8, this function would modify the order
 * of elements in @a paths, despite the 'const' marker on the parameter.
 * This has been fixed in 1.8.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_delta_path_driver(const svn_delta_editor_t *editor,
                      void *edit_baton,
                      svn_revnum_t revision,
                      const apr_array_header_t *paths,
                      svn_delta_path_driver_cb_func_t callback_func,
                      void *callback_baton,
                      apr_pool_t *scratch_pool);

/** @} */


/*** File revision iterator types ***/

/**
 * The callback invoked by file rev loopers, such as
 * svn_ra_plugin_t.get_file_revs2() and svn_repos_get_file_revs2().
 *
 * @a baton is provided by the caller, @a path is the pathname of the file
 * in revision @a rev and @a rev_props are the revision properties.
 *
 * If @a delta_handler and @a delta_baton are non-NULL, they may be set to a
 * handler/baton which will be called with the delta between the previous
 * revision and this one after the return of this callback.  They may be
 * left as NULL/NULL.
 *
 * @a result_of_merge will be @c TRUE if the revision being returned was
 * included as the result of a merge.
 *
 * @a prop_diffs is an array of svn_prop_t elements indicating the property
 * delta for this and the previous revision.
 *
 * @a pool may be used for temporary allocations, but you can't rely
 * on objects allocated to live outside of this particular call and
 * the immediately following calls to @a *delta_handler if any.  (Pass
 * in a pool via @a baton if need be.)
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_file_rev_handler_t)(
  void *baton,
  const char *path,
  svn_revnum_t rev,
  apr_hash_t *rev_props,
  svn_boolean_t result_of_merge,
  svn_txdelta_window_handler_t *delta_handler,
  void **delta_baton,
  apr_array_header_t *prop_diffs,
  apr_pool_t *pool);

/**
 * The old file rev handler interface.
 *
 * @note #svn_file_rev_handler_old_t is a placeholder type for both
 * #svn_repos_file_rev_handler_t and #svn_ra_file_rev_handler_t.  It is
 * reproduced here for dependency reasons.
 *
 * @deprecated This type is provided for the svn_compat_wrap_file_rev_handler()
 * compatibility wrapper, and should not be used for new development.
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_file_rev_handler_old_t)(
  void *baton,
  const char *path,
  svn_revnum_t rev,
  apr_hash_t *rev_props,
  svn_txdelta_window_handler_t *delta_handler,
  void **delta_baton,
  apr_array_header_t *prop_diffs,
  apr_pool_t *pool);

/** Return, in @a *handler2 and @a *handler2_baton a function/baton that
 * will call @a handler/@a handler_baton, allocating the @a *handler2_baton
 * in @a pool.
 *
 * @note This is used by compatibility wrappers, which exist in more than
 * Subversion core library.
 *
 * @note #svn_file_rev_handler_old_t is a placeholder type for both
 * #svn_repos_file_rev_handler_t and #svn_ra_file_rev_handler_t.  It is
 * reproduced here for dependency reasons.
 *
 * @since New in 1.5.
 */
void
svn_compat_wrap_file_rev_handler(svn_file_rev_handler_t *handler2,
                                 void **handler2_baton,
                                 svn_file_rev_handler_old_t handler,
                                 void *handler_baton,
                                 apr_pool_t *pool);

/** @} end group: delta_support */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DELTA_H */
