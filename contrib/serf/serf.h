/* ====================================================================
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

#ifndef SERF_H
#define SERF_H

/**
 * @file serf.h
 * @brief Main serf header file
 */

#include <apr.h>
#include <apr_errno.h>
#include <apr_allocator.h>
#include <apr_pools.h>
#include <apr_network_io.h>
#include <apr_time.h>
#include <apr_poll.h>
#include <apr_uri.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declare some structures */
typedef struct serf_context_t serf_context_t;

typedef struct serf_bucket_t serf_bucket_t;
typedef struct serf_bucket_type_t serf_bucket_type_t;
typedef struct serf_bucket_alloc_t serf_bucket_alloc_t;

typedef struct serf_connection_t serf_connection_t;
typedef struct serf_listener_t serf_listener_t;
typedef struct serf_incoming_t serf_incoming_t;
typedef struct serf_incoming_request_t serf_incoming_request_t;

typedef struct serf_request_t serf_request_t;


/**
 * @defgroup serf high-level constructs
 * @ingroup serf
 * @{
 */

/**
 * Serf-specific error codes
 */
#define SERF_ERROR_RANGE 100
#define SERF_ERROR_START (APR_OS_START_USERERR + SERF_ERROR_RANGE)

/* This code is for when this is the last response on this connection:
 * i.e. do not send any more requests on this connection or expect
 * any more responses.
 */
#define SERF_ERROR_CLOSING (SERF_ERROR_START + 1)
/* This code is for when the connection terminated before the request
 * could be processed on the other side.
 */
#define SERF_ERROR_REQUEST_LOST (SERF_ERROR_START + 2)
/* This code is for when the connection is blocked - we can not proceed
 * until something happens - generally due to SSL negotiation-like behavior
 * where a write() is blocked until a read() is processed.
 */
#define SERF_ERROR_WAIT_CONN (SERF_ERROR_START + 3)
/* This code is for when something went wrong during deflating compressed
 * data e.g. a CRC error. */
#define SERF_ERROR_DECOMPRESSION_FAILED (SERF_ERROR_START + 4)
/* This code is for when a response received from a http server is not in
 * http-compliant syntax. */
#define SERF_ERROR_BAD_HTTP_RESPONSE (SERF_ERROR_START + 5)
/* The server sent less data than what was announced. */
#define SERF_ERROR_TRUNCATED_HTTP_RESPONSE (SERF_ERROR_START + 6)
/* The proxy server returned an error while setting up the SSL tunnel. */
#define SERF_ERROR_SSLTUNNEL_SETUP_FAILED (SERF_ERROR_START + 7)
/* The server unexpectedly closed the connection prematurely. */
#define SERF_ERROR_ABORTED_CONNECTION (SERF_ERROR_START + 8)

/* SSL certificates related errors */
#define SERF_ERROR_SSL_CERT_FAILED (SERF_ERROR_START + 70)

/* SSL communications related errors */
#define SERF_ERROR_SSL_COMM_FAILED (SERF_ERROR_START + 71)

/* General authentication related errors */
#define SERF_ERROR_AUTHN_FAILED (SERF_ERROR_START + 90)

/* None of the available authn mechanisms for the request are supported */
#define SERF_ERROR_AUTHN_NOT_SUPPORTED (SERF_ERROR_START + 91)

/* Authn was requested by the server but the header lacked some attribute  */
#define SERF_ERROR_AUTHN_MISSING_ATTRIBUTE (SERF_ERROR_START + 92)

/* Authentication handler initialization related errors */
#define SERF_ERROR_AUTHN_INITALIZATION_FAILED (SERF_ERROR_START + 93)

/* Error code reserved for use in the test suite. */
#define SERF_ERROR_ISSUE_IN_TESTSUITE (SERF_ERROR_START + 99)

/* This macro groups errors potentially raised when reading a http response.  */
#define SERF_BAD_RESPONSE_ERROR(status) ((status) \
    && ((SERF_ERROR_DECOMPRESSION_FAILED == (status)) \
        ||(SERF_ERROR_BAD_HTTP_RESPONSE == (status)) \
        ||(SERF_ERROR_TRUNCATED_HTTP_RESPONSE == (status))))

/**
 * Return a string that describes the specified error code.
 *
 * If the error code is not one of the above Serf error codes, then
 * NULL will be returned.
 *
 * Note regarding lifetime: the string is a statically-allocated constant
 */
const char *serf_error_string(apr_status_t errcode);


/**
 * Create a new context for serf operations.
 *
 * A serf context defines a control loop which processes multiple
 * connections simultaneously.
 *
 * The context will be allocated within @a pool.
 */
serf_context_t *serf_context_create(
    apr_pool_t *pool);

/**
 * Callback function. Add a socket to the externally managed poll set.
 *
 * Both @a pfd and @a serf_baton should be used when calling serf_event_trigger
 * later.
 */
typedef apr_status_t (*serf_socket_add_t)(
    void *user_baton,
    apr_pollfd_t *pfd,
    void *serf_baton);

/**
 * Callback function. Remove the socket, identified by both @a pfd and
 * @a serf_baton from the externally managed poll set.
 */
typedef apr_status_t (*serf_socket_remove_t)(
    void *user_baton,
    apr_pollfd_t *pfd,
    void *serf_baton);

/* Create a new context for serf operations.
 *
 * Use this function to make serf not use its internal control loop, but
 * instead rely on an external event loop. Serf will use the @a addf and @a rmf
 * callbacks to notify of any event on a connection. The @a user_baton will be
 * passed through the addf and rmf callbacks.
 *
 * The context will be allocated within @a pool.
 */
serf_context_t *serf_context_create_ex(
    void *user_baton,
    serf_socket_add_t addf,
    serf_socket_remove_t rmf,
    apr_pool_t *pool);

/**
 * Make serf process events on a connection, identified by both @a pfd and
 * @a serf_baton.
 *
 * Any outbound data is delivered, and incoming data is made available to
 * the associated response handlers and their buckets.
 *
 * If any data is processed (incoming or outgoing), then this function will
 * return with APR_SUCCESS.
 */
apr_status_t serf_event_trigger(
    serf_context_t *s,
    void *serf_baton,
    const apr_pollfd_t *pfd);

/** @see serf_context_run should not block at all. */
#define SERF_DURATION_NOBLOCK 0
/** @see serf_context_run should run for (nearly) "forever". */
#define SERF_DURATION_FOREVER 2000000000        /* approx 1^31 */

/**
 * Run the main networking control loop.
 *
 * The set of connections defined by the serf context @a ctx are processed.
 * Any outbound data is delivered, and incoming data is made available to
 * the associated response handlers and their buckets. This function will
 * block on the network for no longer than @a duration microseconds.
 *
 * If any data is processed (incoming or outgoing), then this function will
 * return with APR_SUCCESS. Typically, the caller will just want to call it
 * again to continue processing data.
 *
 * If no activity occurs within the specified timeout duration, then
 * APR_TIMEUP is returned.
 *
 * All temporary allocations will be made in @a pool.
 */
apr_status_t serf_context_run(
    serf_context_t *ctx,
    apr_short_interval_time_t duration,
    apr_pool_t *pool);


apr_status_t serf_context_prerun(
    serf_context_t *ctx);

/**
 * Callback function for progress information. @a progress indicates cumulative
 * number of bytes read or written, for the whole context.
 */
typedef void (*serf_progress_t)(
    void *progress_baton,
    apr_off_t read,
    apr_off_t write);

/**
 * Sets the progress callback function. @a progress_func will be called every
 * time bytes are read of or written on a socket.
 */
void serf_context_set_progress_cb(
    serf_context_t *ctx,
    const serf_progress_t progress_func,
    void *progress_baton);

/** @} */

/**
 * @defgroup serf connections and requests
 * @ingroup serf
 * @{
 */

/**
 * When a connection is established, the application needs to wrap some
 * buckets around @a skt to enable serf to process incoming responses. This
 * is the control point for assembling connection-level processing logic
 * around the given socket.
 *
 * The @a setup_baton is the baton established at connection creation time.
 *
 * This callback corresponds to reading from the server. Since this is an
 * on-demand activity, we use a callback. The corresponding write operation
 * is based on the @see serf_request_deliver function, where the application
 * can assemble the appropriate bucket(s) before delivery.
 *
 * The returned bucket should live at least as long as the connection itself.
 * It is assumed that an appropriate allocator is passed in @a setup_baton.
 * ### we may want to create a connection-level allocator and pass that
 * ### along. however, that allocator would *only* be used for this
 * ### callback. it may be wasteful to create a per-conn allocator, so this
 * ### baton-based, app-responsible form might be best.
 *
 * Responsibility for the buckets is passed to the serf library. They will be
 * destroyed when the connection is closed.
 *
 * All temporary allocations should be made in @a pool.
 */
typedef apr_status_t (*serf_connection_setup_t)(
    apr_socket_t *skt,
    serf_bucket_t **read_bkt,
    serf_bucket_t **write_bkt,
    void *setup_baton,
    apr_pool_t *pool);

/**
 * ### need to update docco w.r.t socket. became "stream" recently.
 * ### the stream does not have a barrier, this callback should generally
 * ### add a barrier around the stream before incorporating it into a
 * ### response bucket stack.
 * ### should serf add the barrier automatically to protect its data
 * ### structure? i.e. the passed bucket becomes owned rather than
 * ### borrowed. that might suit overall semantics better.
 * Accept an incoming response for @a request, and its @a socket. A bucket
 * for the response should be constructed and returned. This is the control
 * point for assembling the appropriate wrapper buckets around the socket to
 * enable processing of the incoming response.
 *
 * The @a acceptor_baton is the baton provided when the specified request
 * was created.
 *
 * The request's pool and bucket allocator should be used for any allocations
 * that need to live for the duration of the response. Care should be taken
 * to bound the amount of memory stored in this pool -- to ensure that
 * allocations are not proportional to the amount of data in the response.
 *
 * Responsibility for the bucket is passed to the serf library. It will be
 * destroyed when the response has been fully read (the bucket returns an
 * APR_EOF status from its read functions).
 *
 * All temporary allocations should be made in @a pool.
 */
/* ### do we need to return an error? */
typedef serf_bucket_t * (*serf_response_acceptor_t)(
    serf_request_t *request,
    serf_bucket_t *stream,
    void *acceptor_baton,
    apr_pool_t *pool);

/**
 * Notification callback for when a connection closes.
 *
 * This callback is used to inform an application that the @a conn
 * connection has been (abnormally) closed. The @a closed_baton is the
 * baton provided when the connection was first opened. The reason for
 * closure is given in @a why, and will be APR_SUCCESS if the application
 * requested closure (by clearing the pool used to allocate this
 * connection or calling serf_connection_close).
 *
 * All temporary allocations should be made in @a pool.
 */
typedef void (*serf_connection_closed_t)(
    serf_connection_t *conn,
    void *closed_baton,
    apr_status_t why,
    apr_pool_t *pool);

/**
 * Response data has arrived and should be processed.
 *
 * Whenever response data for @a request arrives (initially, or continued data
 * arrival), this handler is invoked. The response data is available in the
 * @a response bucket. The @a handler_baton is passed along from the baton
 * provided by the request setup callback (@see serf_request_setup_t).
 *
 * The handler MUST process data from the @a response bucket until the
 * bucket's read function states it would block (see APR_STATUS_IS_EAGAIN).
 * The handler is invoked only when new data arrives. If no further data
 * arrives, and the handler does not process all available data, then the
 * system can result in a deadlock around the unprocessed, but read, data.
 *
 * The handler should return APR_EOF when the response has been fully read.
 * If calling the handler again would block, APR_EAGAIN should be returned.
 * If the handler should be invoked again, simply return APR_SUCCESS.
 *
 * Note: if the connection closed (at the request of the application, or
 * because of an (abnormal) termination) while a request is being delivered,
 * or before a response arrives, then @a response will be NULL. This is the
 * signal that the request was not delivered properly, and no further
 * response should be expected (this callback will not be invoked again).
 * If a request is injected into the connection (during this callback's
 * execution, or otherwise), then the connection will be reopened.
 *
 * All temporary allocations should be made in @a pool.
 */
typedef apr_status_t (*serf_response_handler_t)(
    serf_request_t *request,
    serf_bucket_t *response,
    void *handler_baton,
    apr_pool_t *pool);

/**
 * Callback function to be implemented by the application, so that serf
 * can handle server and proxy authentication.
 * code = 401 (server) or 407 (proxy).
 * baton = the baton passed to serf_context_run.
 * authn_type = one of "Basic", "Digest".
 */
typedef apr_status_t (*serf_credentials_callback_t)(
    char **username,
    char **password,
    serf_request_t *request, void *baton,
    int code, const char *authn_type,
    const char *realm,
    apr_pool_t *pool);

/**
 * Create a new connection associated with the @a ctx serf context.
 *
 * If no proxy server is configured, a connection will be created to
 * (eventually) connect to the address specified by @a address. The address must
 * live at least as long as @a pool (thus, as long as the connection object).
 * If a proxy server is configured, @address will be ignored.
 *
 * The connection object will be allocated within @a pool. Clearing or
 * destroying this pool will close the connection, and terminate any
 * outstanding requests or responses.
 *
 * When the connection is closed (upon request or because of an error),
 * then the @a closed callback is invoked, and @a closed_baton is passed.
 *
 * ### doc on setup(_baton). tweak below comment re: acceptor.
 * NULL may be passed for @a acceptor and @a closed; default implementations
 * will be used.
 *
 * Note: the connection is not made immediately. It will be opened on
 * the next call to @see serf_context_run.
 */
serf_connection_t *serf_connection_create(
    serf_context_t *ctx,
    apr_sockaddr_t *address,
    serf_connection_setup_t setup,
    void *setup_baton,
    serf_connection_closed_t closed,
    void *closed_baton,
    apr_pool_t *pool);

/**
 * Create a new connection associated with the @a ctx serf context.
 *
 * A connection will be created to (eventually) connect to the address
 * specified by @a address. The address must live at least as long as
 * @a pool (thus, as long as the connection object).
 *
 * The host address will be looked up based on the hostname in @a host_info.
 *
 * The connection object will be allocated within @a pool. Clearing or
 * destroying this pool will close the connection, and terminate any
 * outstanding requests or responses.
 *
 * When the connection is closed (upon request or because of an error),
 * then the @a closed callback is invoked, and @a closed_baton is passed.
 *
 * ### doc on setup(_baton). tweak below comment re: acceptor.
 * NULL may be passed for @a acceptor and @a closed; default implementations
 * will be used.
 *
 * Note: the connection is not made immediately. It will be opened on
 * the next call to @see serf_context_run.
 */
apr_status_t serf_connection_create2(
    serf_connection_t **conn,
    serf_context_t *ctx,
    apr_uri_t host_info,
    serf_connection_setup_t setup,
    void *setup_baton,
    serf_connection_closed_t closed,
    void *closed_baton,
    apr_pool_t *pool);


typedef apr_status_t (*serf_accept_client_t)(
    serf_context_t *ctx,
    serf_listener_t *l,
    void *accept_baton,
    apr_socket_t *insock,
    apr_pool_t *pool);

apr_status_t serf_listener_create(
    serf_listener_t **listener,
    serf_context_t *ctx,
    const char *host,
    apr_uint16_t port,
    void *accept_baton,
    serf_accept_client_t accept_func,
    apr_pool_t *pool);

typedef apr_status_t (*serf_incoming_request_cb_t)(
    serf_context_t *ctx,
    serf_incoming_request_t *req,
    void *request_baton,
    apr_pool_t *pool);

apr_status_t serf_incoming_create(
    serf_incoming_t **client,
    serf_context_t *ctx,
    apr_socket_t *insock,
    void *request_baton,
    serf_incoming_request_cb_t request,
    apr_pool_t *pool);




/**
 * Reset the connection, but re-open the socket again.
 */
apr_status_t serf_connection_reset(
    serf_connection_t *conn);

/**
 * Close the connection associated with @a conn and cancel all pending requests.
 *
 * The closed callback passed to serf_connection_create() will be invoked
 * with APR_SUCCESS.
 */
apr_status_t serf_connection_close(
    serf_connection_t *conn);

/**
 * Sets the maximum number of outstanding requests @a max_requests on the
 * connection @a conn. Setting max_requests to 0 means unlimited (the default).
 * Ex.: setting max_requests to 1 means a request is sent when a response on the
 * previous request was received and handled.
 *
 * In general, serf tends to take around 16KB per outstanding request.
 */
void serf_connection_set_max_outstanding_requests(
    serf_connection_t *conn,
    unsigned int max_requests);

void serf_connection_set_async_responses(
    serf_connection_t *conn,
    serf_response_acceptor_t acceptor,
    void *acceptor_baton,
    serf_response_handler_t handler,
    void *handler_baton);

/**
 * Setup the @a request for delivery on its connection.
 *
 * Right before this is invoked, @a pool will be built within the
 * connection's pool for the request to use.  The associated response will
 * be allocated within that subpool. An associated bucket allocator will
 * be built. These items may be fetched from the request object through
 * @see serf_request_get_pool or @see serf_request_get_alloc.
 *
 * The content of the request is specified by the @a req_bkt bucket. When
 * a response arrives, the @a acceptor callback will be invoked (along with
 * the @a acceptor_baton) to produce a response bucket. That bucket will then
 * be passed to @a handler, along with the @a handler_baton.
 *
 * The responsibility for the request bucket is passed to the request
 * object. When the request is done with the bucket, it will be destroyed.
 */
typedef apr_status_t (*serf_request_setup_t)(
    serf_request_t *request,
    void *setup_baton,
    serf_bucket_t **req_bkt,
    serf_response_acceptor_t *acceptor,
    void **acceptor_baton,
    serf_response_handler_t *handler,
    void **handler_baton,
    apr_pool_t *pool);

/**
 * Construct a request object for the @a conn connection.
 *
 * When it is time to deliver the request, the @a setup callback will
 * be invoked with the @a setup_baton passed into it to complete the
 * construction of the request object.
 *
 * If the request has not (yet) been delivered, then it may be canceled
 * with @see serf_request_cancel.
 *
 * Invoking any calls other than @see serf_request_cancel before the setup
 * callback executes is not supported.
 */
serf_request_t *serf_connection_request_create(
    serf_connection_t *conn,
    serf_request_setup_t setup,
    void *setup_baton);

/**
 * Construct a request object for the @a conn connection, add it in the
 * list as the next to-be-written request before all unwritten requests.
 *
 * When it is time to deliver the request, the @a setup callback will
 * be invoked with the @a setup_baton passed into it to complete the
 * construction of the request object.
 *
 * If the request has not (yet) been delivered, then it may be canceled
 * with @see serf_request_cancel.
 *
 * Invoking any calls other than @see serf_request_cancel before the setup
 * callback executes is not supported.
 */
serf_request_t *serf_connection_priority_request_create(
    serf_connection_t *conn,
    serf_request_setup_t setup,
    void *setup_baton);


/** Returns detected network latency for the @a conn connection. Negative
 *  value means that latency is unknwon.
 */
apr_interval_time_t serf_connection_get_latency(serf_connection_t *conn);

/** Check if a @a request has been completely written.
 *
 * Returns APR_SUCCESS if the request was written completely on the connection.
 * Returns APR_EBUSY if the request is not yet or partially written.
 */
apr_status_t serf_request_is_written(
    serf_request_t *request);

/**
 * Cancel the request specified by the @a request object.
 *
 * If the request has been scheduled for delivery, then its response
 * handler will be run, passing NULL for the response bucket.
 *
 * If the request has already been (partially or fully) delivered, then
 * APR_EBUSY is returned and the request is *NOT* canceled. To properly
 * cancel the request, the connection must be closed (by clearing or
 * destroying its associated pool).
 */
apr_status_t serf_request_cancel(
    serf_request_t *request);

/**
 * Return the pool associated with @a request.
 *
 * WARNING: be very careful about the kinds of things placed into this
 * pool. In particular, all allocation should be bounded in size, rather
 * than proportional to any data stream.
 */
apr_pool_t *serf_request_get_pool(
    const serf_request_t *request);

/**
 * Return the bucket allocator associated with @a request.
 */
serf_bucket_alloc_t *serf_request_get_alloc(
    const serf_request_t *request);

/**
 * Return the connection associated with @a request.
 */
serf_connection_t *serf_request_get_conn(
    const serf_request_t *request);

/**
 * Update the @a handler and @a handler_baton for this @a request.
 *
 * This can be called after the request has started processing -
 * subsequent data will be delivered to this new handler.
 */
void serf_request_set_handler(
    serf_request_t *request,
    const serf_response_handler_t handler,
    const void **handler_baton);

/**
 * Configure proxy server settings, to be used by all connections associated
 * with the @a ctx serf context.
 *
 * The next connection will be created to connect to the proxy server
 * specified by @a address. The address must live at least as long as the
 * serf context.
 */
void serf_config_proxy(
    serf_context_t *ctx,
    apr_sockaddr_t *address);

/* Supported authentication types. */
#define SERF_AUTHN_NONE      0x00
#define SERF_AUTHN_BASIC     0x01
#define SERF_AUTHN_DIGEST    0x02
#define SERF_AUTHN_NTLM      0x04
#define SERF_AUTHN_NEGOTIATE 0x08
#define SERF_AUTHN_ALL       0xFF

/**
 * Define the authentication handlers that serf will try on incoming requests.
 */
void serf_config_authn_types(
    serf_context_t *ctx,
    int authn_types);

/**
 * Set the credentials callback handler.
 */
void serf_config_credentials_callback(
    serf_context_t *ctx,
    serf_credentials_callback_t cred_cb);

/* ### maybe some connection control functions for flood? */

/*** Special bucket creation functions ***/

/**
 * Create a bucket of type 'socket bucket'.
 * This is basically a wrapper around @a serf_bucket_socket_create, which
 * initializes the bucket using connection and/or context specific settings.
 */
serf_bucket_t *serf_context_bucket_socket_create(
    serf_context_t *ctx,
    apr_socket_t *skt,
    serf_bucket_alloc_t *allocator);

/**
 * Create a bucket of type 'request bucket'.
 * This is basically a wrapper around @a serf_bucket_request_create, which
 * initializes the bucket using request, connection and/or context specific
 * settings.
 *
 * This function will set following header(s):
 * - Host: if the connection was created with @a serf_connection_create2.
 */
serf_bucket_t *serf_request_bucket_request_create(
    serf_request_t *request,
    const char *method,
    const char *uri,
    serf_bucket_t *body,
    serf_bucket_alloc_t *allocator);

/** @} */


/**
 * @defgroup serf buckets
 * @ingroup serf
 * @{
 */

/** Pass as REQUESTED to the read function of a bucket to read, consume,
 * and return all available data.
 */
#define SERF_READ_ALL_AVAIL ((apr_size_t)-1)

/** Acceptable newline types for bucket->readline(). */
#define SERF_NEWLINE_CR    0x0001
#define SERF_NEWLINE_CRLF  0x0002
#define SERF_NEWLINE_LF    0x0004
#define SERF_NEWLINE_ANY   0x0007

/** Used to indicate that a newline is not present in the data buffer. */
/* ### should we make this zero? */
#define SERF_NEWLINE_NONE  0x0008

/** Used to indicate that a CR was found at the end of a buffer, and CRLF
 * was acceptable. It may be that the LF is present, but it needs to be
 * read first.
 *
 * Note: an alternative to using this symbol would be for callers to see
 * the SERF_NEWLINE_CR return value, and know that some "end of buffer" was
 * reached. While this works well for @see serf_util_readline, it does not
 * necessary work as well for buckets (there is no obvious "end of buffer",
 * although there is an "end of bucket"). The other problem with that
 * alternative is that developers might miss the condition. This symbol
 * calls out the possibility and ensures that callers will watch for it.
 */
#define SERF_NEWLINE_CRLF_SPLIT 0x0010


struct serf_bucket_type_t {

    /** name of this bucket type */
    const char *name;

    /**
     * Read (and consume) up to @a requested bytes from @a bucket.
     *
     * A pointer to the data will be returned in @a data, and its length
     * is specified by @a len.
     *
     * The data will exist until one of two conditions occur:
     *
     * 1) this bucket is destroyed
     * 2) another call to any read function or to peek()
     *
     * If an application needs the data to exist for a longer duration,
     * then it must make a copy.
     */
    apr_status_t (*read)(serf_bucket_t *bucket, apr_size_t requested,
                         const char **data, apr_size_t *len);

    /**
     * Read (and consume) a line of data from @a bucket.
     *
     * The acceptable forms of a newline are given by @a acceptable, and
     * the type found is returned in @a found. If a newline is not present
     * in the returned data, then SERF_NEWLINE_NONE is stored into @a found.
     *
     * A pointer to the data is returned in @a data, and its length is
     * specified by @a len. The data will include the newline, if present.
     *
     * Note that there is no way to limit the amount of data returned
     * by this function.
     *
     * The lifetime of the data is the same as that of the @see read
     * function above.
     */
    apr_status_t (*readline)(serf_bucket_t *bucket, int acceptable,
                             int *found,
                             const char **data, apr_size_t *len);

    /**
     * Read a set of pointer/length pairs from the bucket.
     *
     * The size of the @a vecs array is specified by @a vecs_size. The
     * bucket should fill in elements of the array, and return the number
     * used in @a vecs_used.
     *
     * Each element of @a vecs should specify a pointer to a block of
     * data and a length of that data.
     *
     * The total length of all data elements should not exceed the
     * amount specified in @a requested.
     *
     * The lifetime of the data is the same as that of the @see read
     * function above.
     */
    apr_status_t (*read_iovec)(serf_bucket_t *bucket, apr_size_t requested,
                               int vecs_size, struct iovec *vecs,
                               int *vecs_used);

    /**
     * Read data from the bucket in a form suitable for apr_socket_sendfile()
     *
     * On input, hdtr->numheaders and hdtr->numtrailers specify the size
     * of the hdtr->headers and hdtr->trailers arrays, respectively. The
     * bucket should fill in the headers and trailers, up to the specified
     * limits, and set numheaders and numtrailers to the number of iovecs
     * filled in for each item.
     *
     * @a file should be filled in with a file that can be read. If a file
     * is not available or appropriate, then NULL should be stored. The
     * file offset for the data should be stored in @a offset, and the
     * length of that data should be stored in @a len. If a file is not
     * returned, then @a offset and @a len should be ignored.
     *
     * The file position is not required to correspond to @a offset, and
     * the caller may manipulate it at will.
     *
     * The total length of all data elements, and the portion of the
     * file should not exceed the amount specified in @a requested.
     *
     * The lifetime of the data is the same as that of the @see read
     * function above.
     */
    apr_status_t (*read_for_sendfile)(serf_bucket_t *bucket,
                                      apr_size_t requested, apr_hdtr_t *hdtr,
                                      apr_file_t **file, apr_off_t *offset,
                                      apr_size_t *len);

    /**
     * Look within @a bucket for a bucket of the given @a type. The bucket
     * must be the "initial" data because it will be consumed by this
     * function. If the given bucket type is available, then read and consume
     * it, and return it to the caller.
     *
     * This function is usually used by readers that have custom handling
     * for specific bucket types (e.g. looking for a file bucket to pass
     * to apr_socket_sendfile).
     *
     * If a bucket of the given type is not found, then NULL is returned.
     *
     * The returned bucket becomes the responsibility of the caller. When
     * the caller is done with the bucket, it should be destroyed.
     */
    serf_bucket_t * (*read_bucket)(serf_bucket_t *bucket,
                                   const serf_bucket_type_t *type);

    /**
     * Peek, but don't consume, the data in @a bucket.
     *
     * Since this function is non-destructive, the implicit read size is
     * SERF_READ_ALL_AVAIL. The caller can then use whatever amount is
     * appropriate.
     *
     * The @a data parameter will point to the data, and @a len will
     * specify how much data is available. The lifetime of the data follows
     * the same rules as the @see read function above.
     *
     * Note: if the peek does not return enough data for your particular
     * use, then you must read/consume some first, then peek again.
     *
     * If the returned data represents all available data, then APR_EOF
     * will be returned. Since this function does not consume data, it
     * can return the same data repeatedly rather than blocking; thus,
     * APR_EAGAIN will never be returned.
     */
    apr_status_t (*peek)(serf_bucket_t *bucket,
                         const char **data, apr_size_t *len);

    /**
     * Destroy @a bucket, along with any associated resources.
     */
    void (*destroy)(serf_bucket_t *bucket);

    /* ### apr buckets have 'copy', 'split', and 'setaside' functions.
       ### not sure whether those will be needed in this bucket model.
    */
};

/**
 * Should the use and lifecycle of buckets be tracked?
 *
 * When tracking, the system will ensure several semantic requirements
 * of bucket use:
 *
 *   - if a bucket returns APR_EAGAIN, one of its read functions should
 *     not be called immediately. the context's run loop should be called.
 *     ### and for APR_EOF, too?
 *   - all buckets must be drained of input before returning to the
 *     context's run loop.
 *   - buckets should not be destroyed before they return APR_EOF unless
 *     the connection is closed for some reason.
 *
 * Undefine this symbol to avoid the tracking (and a performance gain).
 *
 * ### we may want to examine when/how we provide this. should it always
 * ### be compiled in? and apps select it before including this header?
 */
/* #define SERF_DEBUG_BUCKET_USE */


/* Internal macros for tracking bucket use. */
#ifdef SERF_DEBUG_BUCKET_USE
#define SERF__RECREAD(b,s) serf_debug__record_read(b,s)
#else
#define SERF__RECREAD(b,s) (s)
#endif

#define serf_bucket_read(b,r,d,l) SERF__RECREAD(b, (b)->type->read(b,r,d,l))
#define serf_bucket_readline(b,a,f,d,l) \
    SERF__RECREAD(b, (b)->type->readline(b,a,f,d,l))
#define serf_bucket_read_iovec(b,r,s,v,u) \
    SERF__RECREAD(b, (b)->type->read_iovec(b,r,s,v,u))
#define serf_bucket_read_for_sendfile(b,r,h,f,o,l) \
    SERF__RECREAD(b, (b)->type->read_for_sendfile(b,r,h,f,o,l))
#define serf_bucket_read_bucket(b,t) ((b)->type->read_bucket(b,t))
#define serf_bucket_peek(b,d,l) ((b)->type->peek(b,d,l))
#define serf_bucket_destroy(b) ((b)->type->destroy(b))

/**
 * Check whether a real error occurred. Note that bucket read functions
 * can return EOF and EAGAIN as part of their "normal" operation, so they
 * should not be considered an error.
 */
#define SERF_BUCKET_READ_ERROR(status) ((status) \
                                        && !APR_STATUS_IS_EOF(status) \
                                        && !APR_STATUS_IS_EAGAIN(status) \
                                        && (SERF_ERROR_WAIT_CONN != status))


struct serf_bucket_t {

    /** the type of this bucket */
    const serf_bucket_type_t *type;

    /** bucket-private data */
    void *data;

    /** the allocator used for this bucket (needed at destroy time) */
    serf_bucket_alloc_t *allocator;
};


/**
 * Generic macro to construct "is TYPE" macros.
 */
#define SERF_BUCKET_CHECK(b, btype) ((b)->type == &serf_bucket_type_ ## btype)


/**
 * Notification callback for a block that was not returned to the bucket
 * allocator when its pool was destroyed.
 *
 * The block of memory is given by @a block. The baton provided when the
 * allocator was constructed is passed as @a unfreed_baton.
 */
typedef void (*serf_unfreed_func_t)(
    void *unfreed_baton,
    void *block);

/**
 * Create a new allocator for buckets.
 *
 * All buckets are associated with a serf bucket allocator. This allocator
 * will be created within @a pool and will be destroyed when that pool is
 * cleared or destroyed.
 *
 * When the allocator is destroyed, if any allocations were not explicitly
 * returned (by calling serf_bucket_mem_free), then the @a unfreed callback
 * will be invoked for each block. @a unfreed_baton will be passed to the
 * callback.
 *
 * If @a unfreed is NULL, then the library will invoke the abort() stdlib
 * call. Any failure to return memory is a bug in the application, and an
 * abort can assist with determining what kinds of memory were not freed.
 */
serf_bucket_alloc_t *serf_bucket_allocator_create(
    apr_pool_t *pool,
    serf_unfreed_func_t unfreed,
    void *unfreed_baton);

/**
 * Return the pool that was used for this @a allocator.
 *
 * WARNING: the use of this pool for allocations requires a very
 *   detailed understanding of pool behaviors, the bucket system,
 *   and knowledge of the bucket's use within the overall pattern
 *   of request/response behavior.
 *
 * See design-guide.txt for more information about pool usage.
 */
apr_pool_t *serf_bucket_allocator_get_pool(
    const serf_bucket_alloc_t *allocator);


/**
 * Utility structure for reading a complete line of input from a bucket.
 *
 * Since it is entirely possible for a line to be broken by APR_EAGAIN,
 * this structure can be used to accumulate the data until a complete line
 * has been read from a bucket.
 */

/* This limit applies to the line buffer functions. If an application needs
 * longer lines, then they will need to manually handle line buffering.
 */
#define SERF_LINEBUF_LIMIT 8000

typedef struct {

    /* Current state of the buffer. */
    enum {
        SERF_LINEBUF_EMPTY,
        SERF_LINEBUF_READY,
        SERF_LINEBUF_PARTIAL,
        SERF_LINEBUF_CRLF_SPLIT
    } state;

    /* How much of the buffer have we used? */
    apr_size_t used;

    /* The line is read into this buffer, minus CR/LF */
    char line[SERF_LINEBUF_LIMIT];

} serf_linebuf_t;

/**
 * Initialize the @a linebuf structure.
 */
void serf_linebuf_init(serf_linebuf_t *linebuf);

/**
 * Fetch a line of text from @a bucket, accumulating the line into
 * @a linebuf. @a acceptable specifies the types of newlines which are
 * acceptable for this fetch.
 *
 * ### we should return a data/len pair so that we can avoid a copy,
 * ### rather than having callers look into our state and line buffer.
 */
apr_status_t serf_linebuf_fetch(
    serf_linebuf_t *linebuf,
    serf_bucket_t *bucket,
    int acceptable);

/** @} */


/* Internal functions for bucket use and lifecycle tracking */
apr_status_t serf_debug__record_read(
    const serf_bucket_t *bucket,
    apr_status_t status);
void serf_debug__entered_loop(
    serf_bucket_alloc_t *allocator);
void serf_debug__closed_conn(
    serf_bucket_alloc_t *allocator);
void serf_debug__bucket_destroy(
    const serf_bucket_t *bucket);
void serf_debug__bucket_alloc_check(
    serf_bucket_alloc_t *allocator);

/* Version info */
#define SERF_MAJOR_VERSION 1
#define SERF_MINOR_VERSION 3
#define SERF_PATCH_VERSION 9

/* Version number string */
#define SERF_VERSION_STRING APR_STRINGIFY(SERF_MAJOR_VERSION) "." \
                            APR_STRINGIFY(SERF_MINOR_VERSION) "." \
                            APR_STRINGIFY(SERF_PATCH_VERSION)

/**
 * Check at compile time if the Serf version is at least a certain
 * level.
 * @param major The major version component of the version checked
 * for (e.g., the "1" of "1.3.0").
 * @param minor The minor version component of the version checked
 * for (e.g., the "3" of "1.3.0").
 * @param patch The patch level component of the version checked
 * for (e.g., the "0" of "1.3.0").
 */
#define SERF_VERSION_AT_LEAST(major,minor,patch)                         \
(((major) < SERF_MAJOR_VERSION)                                          \
  || ((major) == SERF_MAJOR_VERSION && (minor) < SERF_MINOR_VERSION)     \
   || ((major) == SERF_MAJOR_VERSION && (minor) == SERF_MINOR_VERSION && \
            (patch) <= SERF_PATCH_VERSION))


/**
 * Returns the version of the library the application has linked/loaded.
 * Values are returned in @a major, @a minor, and @a patch.
 *
 * Applications will want to use this function to verify compatibility,
 * expecially while serf has not reached a 1.0 milestone. APIs and
 * semantics may change drastically until the library hits 1.0.
 */
void serf_lib_version(
    int *major,
    int *minor,
    int *patch);


#ifdef __cplusplus
}
#endif


/*
 * Every user of serf will want to deal with our various bucket types.
 * Go ahead and include that header right now.
 *
 * Note: make sure this occurs outside of the C++ namespace block
 */
#include "serf_bucket_types.h"


#endif    /* !SERF_H */
