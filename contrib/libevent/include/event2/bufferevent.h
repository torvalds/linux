/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EVENT2_BUFFEREVENT_H_INCLUDED_
#define EVENT2_BUFFEREVENT_H_INCLUDED_

/**
   @file event2/bufferevent.h

  Functions for buffering data for network sending or receiving.  Bufferevents
  are higher level than evbuffers: each has an underlying evbuffer for reading
  and one for writing, and callbacks that are invoked under certain
  circumstances.

  A bufferevent provides input and output buffers that get filled and
  drained automatically.  The user of a bufferevent no longer deals
  directly with the I/O, but instead is reading from input and writing
  to output buffers.

  Once initialized, the bufferevent structure can be used repeatedly
  with bufferevent_enable() and bufferevent_disable().

  When reading is enabled, the bufferevent will try to read from the
  file descriptor onto its input buffer, and call the read callback.
  When writing is enabled, the bufferevent will try to write data onto its
  file descriptor when the output buffer has enough data, and call the write
  callback when the output buffer is sufficiently drained.

  Bufferevents come in several flavors, including:

  <dl>
    <dt>Socket-based bufferevents</dt>
      <dd>A bufferevent that reads and writes data onto a network
          socket. Created with bufferevent_socket_new().</dd>

    <dt>Paired bufferevents</dt>
      <dd>A pair of bufferevents that send and receive data to one
          another without touching the network.  Created with
          bufferevent_pair_new().</dd>

    <dt>Filtering bufferevents</dt>
       <dd>A bufferevent that transforms data, and sends or receives it
          over another underlying bufferevent.  Created with
          bufferevent_filter_new().</dd>

    <dt>SSL-backed bufferevents</dt>
      <dd>A bufferevent that uses the openssl library to send and
          receive data over an encrypted connection. Created with
	  bufferevent_openssl_socket_new() or
	  bufferevent_openssl_filter_new().</dd>
  </dl>
 */

#include <event2/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>
#ifdef EVENT__HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/* For int types. */
#include <event2/util.h>

/** @name Bufferevent event codes

    These flags are passed as arguments to a bufferevent's event callback.

    @{
*/
#define BEV_EVENT_READING	0x01	/**< error encountered while reading */
#define BEV_EVENT_WRITING	0x02	/**< error encountered while writing */
#define BEV_EVENT_EOF		0x10	/**< eof file reached */
#define BEV_EVENT_ERROR		0x20	/**< unrecoverable error encountered */
#define BEV_EVENT_TIMEOUT	0x40	/**< user-specified timeout reached */
#define BEV_EVENT_CONNECTED	0x80	/**< connect operation finished. */
/**@}*/

/**
   An opaque type for handling buffered IO

   @see event2/bufferevent.h
 */
struct bufferevent
#ifdef EVENT_IN_DOXYGEN_
{}
#endif
;
struct event_base;
struct evbuffer;
struct sockaddr;

/**
   A read or write callback for a bufferevent.

   The read callback is triggered when new data arrives in the input
   buffer and the amount of readable data exceed the low watermark
   which is 0 by default.

   The write callback is triggered if the write buffer has been
   exhausted or fell below its low watermark.

   @param bev the bufferevent that triggered the callback
   @param ctx the user-specified context for this bufferevent
 */
typedef void (*bufferevent_data_cb)(struct bufferevent *bev, void *ctx);

/**
   An event/error callback for a bufferevent.

   The event callback is triggered if either an EOF condition or another
   unrecoverable error was encountered.

   For bufferevents with deferred callbacks, this is a bitwise OR of all errors
   that have happened on the bufferevent since the last callback invocation.

   @param bev the bufferevent for which the error condition was reached
   @param what a conjunction of flags: BEV_EVENT_READING or BEV_EVENT_WRITING
	  to indicate if the error was encountered on the read or write path,
	  and one of the following flags: BEV_EVENT_EOF, BEV_EVENT_ERROR,
	  BEV_EVENT_TIMEOUT, BEV_EVENT_CONNECTED.

   @param ctx the user-specified context for this bufferevent
*/
typedef void (*bufferevent_event_cb)(struct bufferevent *bev, short what, void *ctx);

/** Options that can be specified when creating a bufferevent */
enum bufferevent_options {
	/** If set, we close the underlying file
	 * descriptor/bufferevent/whatever when this bufferevent is freed. */
	BEV_OPT_CLOSE_ON_FREE = (1<<0),

	/** If set, and threading is enabled, operations on this bufferevent
	 * are protected by a lock */
	BEV_OPT_THREADSAFE = (1<<1),

	/** If set, callbacks are run deferred in the event loop. */
	BEV_OPT_DEFER_CALLBACKS = (1<<2),

	/** If set, callbacks are executed without locks being held on the
	* bufferevent.  This option currently requires that
	* BEV_OPT_DEFER_CALLBACKS also be set; a future version of Libevent
	* might remove the requirement.*/
	BEV_OPT_UNLOCK_CALLBACKS = (1<<3)
};

/**
  Create a new socket bufferevent over an existing socket.

  @param base the event base to associate with the new bufferevent.
  @param fd the file descriptor from which data is read and written to.
	    This file descriptor is not allowed to be a pipe(2).
	    It is safe to set the fd to -1, so long as you later
	    set it with bufferevent_setfd or bufferevent_socket_connect().
  @param options Zero or more BEV_OPT_* flags
  @return a pointer to a newly allocated bufferevent struct, or NULL if an
	  error occurred
  @see bufferevent_free()
  */
EVENT2_EXPORT_SYMBOL
struct bufferevent *bufferevent_socket_new(struct event_base *base, evutil_socket_t fd, int options);

/**
   Launch a connect() attempt with a socket-based bufferevent.

   When the connect succeeds, the eventcb will be invoked with
   BEV_EVENT_CONNECTED set.

   If the bufferevent does not already have a socket set, we allocate a new
   socket here and make it nonblocking before we begin.

   If no address is provided, we assume that the socket is already connecting,
   and configure the bufferevent so that a BEV_EVENT_CONNECTED event will be
   yielded when it is done connecting.

   @param bufev an existing bufferevent allocated with
       bufferevent_socket_new().
   @param addr the address we should connect to
   @param socklen The length of the address
   @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_socket_connect(struct bufferevent *, const struct sockaddr *, int);

struct evdns_base;
/**
   Resolve the hostname 'hostname' and connect to it as with
   bufferevent_socket_connect().

   @param bufev An existing bufferevent allocated with bufferevent_socket_new()
   @param evdns_base Optionally, an evdns_base to use for resolving hostnames
      asynchronously. May be set to NULL for a blocking resolve.
   @param family A preferred address family to resolve addresses to, or
      AF_UNSPEC for no preference.  Only AF_INET, AF_INET6, and AF_UNSPEC are
      supported.
   @param hostname The hostname to resolve; see below for notes on recognized
      formats
   @param port The port to connect to on the resolved address.
   @return 0 if successful, -1 on failure.

   Recognized hostname formats are:

       www.example.com	(hostname)
       1.2.3.4		(ipv4address)
       ::1		(ipv6address)
       [::1]		([ipv6address])

   Performance note: If you do not provide an evdns_base, this function
   may block while it waits for a DNS response.	 This is probably not
   what you want.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_socket_connect_hostname(struct bufferevent *,
    struct evdns_base *, int, const char *, int);

/**
   Return the error code for the last failed DNS lookup attempt made by
   bufferevent_socket_connect_hostname().

   @param bev The bufferevent object.
   @return DNS error code.
   @see evutil_gai_strerror()
*/
EVENT2_EXPORT_SYMBOL
int bufferevent_socket_get_dns_error(struct bufferevent *bev);

/**
  Assign a bufferevent to a specific event_base.

  NOTE that only socket bufferevents support this function.

  @param base an event_base returned by event_init()
  @param bufev a bufferevent struct returned by bufferevent_new()
     or bufferevent_socket_new()
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_new()
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_base_set(struct event_base *base, struct bufferevent *bufev);

/**
   Return the event_base used by a bufferevent
*/
EVENT2_EXPORT_SYMBOL
struct event_base *bufferevent_get_base(struct bufferevent *bev);

/**
  Assign a priority to a bufferevent.

  Only supported for socket bufferevents.

  @param bufev a bufferevent struct
  @param pri the priority to be assigned
  @return 0 if successful, or -1 if an error occurred
  */
EVENT2_EXPORT_SYMBOL
int bufferevent_priority_set(struct bufferevent *bufev, int pri);

/**
   Return the priority of a bufferevent.

   Only supported for socket bufferevents
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_get_priority(const struct bufferevent *bufev);

/**
  Deallocate the storage associated with a bufferevent structure.

  If there is pending data to write on the bufferevent, it probably won't be
  flushed before the bufferevent is freed.

  @param bufev the bufferevent structure to be freed.
  */
EVENT2_EXPORT_SYMBOL
void bufferevent_free(struct bufferevent *bufev);


/**
  Changes the callbacks for a bufferevent.

  @param bufev the bufferevent object for which to change callbacks
  @param readcb callback to invoke when there is data to be read, or NULL if
	 no callback is desired
  @param writecb callback to invoke when the file descriptor is ready for
	 writing, or NULL if no callback is desired
  @param eventcb callback to invoke when there is an event on the file
	 descriptor
  @param cbarg an argument that will be supplied to each of the callbacks
	 (readcb, writecb, and errorcb)
  @see bufferevent_new()
  */
EVENT2_EXPORT_SYMBOL
void bufferevent_setcb(struct bufferevent *bufev,
    bufferevent_data_cb readcb, bufferevent_data_cb writecb,
    bufferevent_event_cb eventcb, void *cbarg);

/**
 Retrieves the callbacks for a bufferevent.

 @param bufev the bufferevent to examine.
 @param readcb_ptr if readcb_ptr is nonnull, *readcb_ptr is set to the current
    read callback for the bufferevent.
 @param writecb_ptr if writecb_ptr is nonnull, *writecb_ptr is set to the
    current write callback for the bufferevent.
 @param eventcb_ptr if eventcb_ptr is nonnull, *eventcb_ptr is set to the
    current event callback for the bufferevent.
 @param cbarg_ptr if cbarg_ptr is nonnull, *cbarg_ptr is set to the current
    callback argument for the bufferevent.
 @see buffervent_setcb()
*/
EVENT2_EXPORT_SYMBOL
void bufferevent_getcb(struct bufferevent *bufev,
    bufferevent_data_cb *readcb_ptr,
    bufferevent_data_cb *writecb_ptr,
    bufferevent_event_cb *eventcb_ptr,
    void **cbarg_ptr);

/**
  Changes the file descriptor on which the bufferevent operates.
  Not supported for all bufferevent types.

  @param bufev the bufferevent object for which to change the file descriptor
  @param fd the file descriptor to operate on
*/
EVENT2_EXPORT_SYMBOL
int bufferevent_setfd(struct bufferevent *bufev, evutil_socket_t fd);

/**
   Returns the file descriptor associated with a bufferevent, or -1 if
   no file descriptor is associated with the bufferevent.
 */
EVENT2_EXPORT_SYMBOL
evutil_socket_t bufferevent_getfd(struct bufferevent *bufev);

/**
   Returns the underlying bufferevent associated with a bufferevent (if
   the bufferevent is a wrapper), or NULL if there is no underlying bufferevent.
 */
EVENT2_EXPORT_SYMBOL
struct bufferevent *bufferevent_get_underlying(struct bufferevent *bufev);

/**
  Write data to a bufferevent buffer.

  The bufferevent_write() function can be used to write data to the file
  descriptor.  The data is appended to the output buffer and written to the
  descriptor automatically as it becomes available for writing.

  @param bufev the bufferevent to be written to
  @param data a pointer to the data to be written
  @param size the length of the data, in bytes
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_write_buffer()
  */
EVENT2_EXPORT_SYMBOL
int bufferevent_write(struct bufferevent *bufev,
    const void *data, size_t size);


/**
  Write data from an evbuffer to a bufferevent buffer.	The evbuffer is
  being drained as a result.

  @param bufev the bufferevent to be written to
  @param buf the evbuffer to be written
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_write()
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_write_buffer(struct bufferevent *bufev, struct evbuffer *buf);


/**
  Read data from a bufferevent buffer.

  The bufferevent_read() function is used to read data from the input buffer.

  @param bufev the bufferevent to be read from
  @param data pointer to a buffer that will store the data
  @param size the size of the data buffer, in bytes
  @return the amount of data read, in bytes.
 */
EVENT2_EXPORT_SYMBOL
size_t bufferevent_read(struct bufferevent *bufev, void *data, size_t size);

/**
  Read data from a bufferevent buffer into an evbuffer.	 This avoids
  memory copies.

  @param bufev the bufferevent to be read from
  @param buf the evbuffer to which to add data
  @return 0 if successful, or -1 if an error occurred.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_read_buffer(struct bufferevent *bufev, struct evbuffer *buf);

/**
   Returns the input buffer.

   The user MUST NOT set the callback on this buffer.

   @param bufev the bufferevent from which to get the evbuffer
   @return the evbuffer object for the input buffer
 */

EVENT2_EXPORT_SYMBOL
struct evbuffer *bufferevent_get_input(struct bufferevent *bufev);

/**
   Returns the output buffer.

   The user MUST NOT set the callback on this buffer.

   When filters are being used, the filters need to be manually
   triggered if the output buffer was manipulated.

   @param bufev the bufferevent from which to get the evbuffer
   @return the evbuffer object for the output buffer
 */

EVENT2_EXPORT_SYMBOL
struct evbuffer *bufferevent_get_output(struct bufferevent *bufev);

/**
  Enable a bufferevent.

  @param bufev the bufferevent to be enabled
  @param event any combination of EV_READ | EV_WRITE.
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_disable()
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_enable(struct bufferevent *bufev, short event);

/**
  Disable a bufferevent.

  @param bufev the bufferevent to be disabled
  @param event any combination of EV_READ | EV_WRITE.
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_enable()
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_disable(struct bufferevent *bufev, short event);

/**
   Return the events that are enabled on a given bufferevent.

   @param bufev the bufferevent to inspect
   @return A combination of EV_READ | EV_WRITE
 */
EVENT2_EXPORT_SYMBOL
short bufferevent_get_enabled(struct bufferevent *bufev);

/**
  Set the read and write timeout for a bufferevent.

  A bufferevent's timeout will fire the first time that the indicated
  amount of time has elapsed since a successful read or write operation,
  during which the bufferevent was trying to read or write.

  (In other words, if reading or writing is disabled, or if the
  bufferevent's read or write operation has been suspended because
  there's no data to write, or not enough banwidth, or so on, the
  timeout isn't active.  The timeout only becomes active when we we're
  willing to actually read or write.)

  Calling bufferevent_enable or setting a timeout for a bufferevent
  whose timeout is already pending resets its timeout.

  If the timeout elapses, the corresponding operation (EV_READ or
  EV_WRITE) becomes disabled until you re-enable it again.  The
  bufferevent's event callback is called with the
  BEV_EVENT_TIMEOUT|BEV_EVENT_READING or
  BEV_EVENT_TIMEOUT|BEV_EVENT_WRITING.

  @param bufev the bufferevent to be modified
  @param timeout_read the read timeout, or NULL
  @param timeout_write the write timeout, or NULL
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_set_timeouts(struct bufferevent *bufev,
    const struct timeval *timeout_read, const struct timeval *timeout_write);

/**
  Sets the watermarks for read and write events.

  On input, a bufferevent does not invoke the user read callback unless
  there is at least low watermark data in the buffer.	If the read buffer
  is beyond the high watermark, the bufferevent stops reading from the network.

  On output, the user write callback is invoked whenever the buffered data
  falls below the low watermark.  Filters that write to this bufev will try
  not to write more bytes to this buffer than the high watermark would allow,
  except when flushing.

  @param bufev the bufferevent to be modified
  @param events EV_READ, EV_WRITE or both
  @param lowmark the lower watermark to set
  @param highmark the high watermark to set
*/

EVENT2_EXPORT_SYMBOL
void bufferevent_setwatermark(struct bufferevent *bufev, short events,
    size_t lowmark, size_t highmark);

/**
  Retrieves the watermarks for read or write events.
  Returns non-zero if events contains not only EV_READ or EV_WRITE.
  Returns zero if events equal EV_READ or EV_WRITE

  @param bufev the bufferevent to be examined
  @param events EV_READ or EV_WRITE
  @param lowmark receives the lower watermark if not NULL
  @param highmark receives the high watermark if not NULL
*/
EVENT2_EXPORT_SYMBOL
int bufferevent_getwatermark(struct bufferevent *bufev, short events,
    size_t *lowmark, size_t *highmark);

/**
   Acquire the lock on a bufferevent.  Has no effect if locking was not
   enabled with BEV_OPT_THREADSAFE.
 */
EVENT2_EXPORT_SYMBOL
void bufferevent_lock(struct bufferevent *bufev);

/**
   Release the lock on a bufferevent.  Has no effect if locking was not
   enabled with BEV_OPT_THREADSAFE.
 */
EVENT2_EXPORT_SYMBOL
void bufferevent_unlock(struct bufferevent *bufev);


/**
 * Public interface to manually increase the reference count of a bufferevent
 * this is useful in situations where a user may reference the bufferevent
 * somewhere eles (unknown to libevent)
 *
 * @param bufev the bufferevent to increase the refcount on
 *
 */
EVENT2_EXPORT_SYMBOL
void bufferevent_incref(struct bufferevent *bufev);

/**
 * Public interface to manually decrement the reference count of a bufferevent
 *
 * Warning: make sure you know what you're doing. This is mainly used in
 * conjunction with bufferevent_incref(). This will free up all data associated
 * with a bufferevent if the reference count hits 0.
 *
 * @param bufev the bufferevent to decrement the refcount on
 *
 * @return 1 if the bufferevent was freed, otherwise 0 (still referenced)
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_decref(struct bufferevent *bufev);

/**
   Flags that can be passed into filters to let them know how to
   deal with the incoming data.
*/
enum bufferevent_flush_mode {
	/** usually set when processing data */
	BEV_NORMAL = 0,

	/** want to checkpoint all data sent. */
	BEV_FLUSH = 1,

	/** encountered EOF on read or done sending data */
	BEV_FINISHED = 2
};

/**
   Triggers the bufferevent to produce more data if possible.

   @param bufev the bufferevent object
   @param iotype either EV_READ or EV_WRITE or both.
   @param mode either BEV_NORMAL or BEV_FLUSH or BEV_FINISHED
   @return -1 on failure, 0 if no data was produces, 1 if data was produced
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_flush(struct bufferevent *bufev,
    short iotype,
    enum bufferevent_flush_mode mode);

/**
   Flags for bufferevent_trigger(_event) that modify when and how to trigger
   the callback.
*/
enum bufferevent_trigger_options {
	/** trigger the callback regardless of the watermarks */
	BEV_TRIG_IGNORE_WATERMARKS = (1<<16),

	/** defer even if the callbacks are not */
	BEV_TRIG_DEFER_CALLBACKS = BEV_OPT_DEFER_CALLBACKS

	/* (Note: for internal reasons, these need to be disjoint from
	 * bufferevent_options, except when they mean the same thing. */
};

/**
   Triggers bufferevent data callbacks.

   The function will honor watermarks unless options contain
   BEV_TRIG_IGNORE_WATERMARKS. If the options contain BEV_OPT_DEFER_CALLBACKS,
   the callbacks are deferred.

   @param bufev the bufferevent object
   @param iotype either EV_READ or EV_WRITE or both.
   @param options
 */
EVENT2_EXPORT_SYMBOL
void bufferevent_trigger(struct bufferevent *bufev, short iotype,
    int options);

/**
   Triggers the bufferevent event callback.

   If the options contain BEV_OPT_DEFER_CALLBACKS, the callbacks are deferred.

   @param bufev the bufferevent object
   @param what the flags to pass onto the event callback
   @param options
 */
EVENT2_EXPORT_SYMBOL
void bufferevent_trigger_event(struct bufferevent *bufev, short what,
    int options);

/**
   @name Filtering support

   @{
*/
/**
   Values that filters can return.
 */
enum bufferevent_filter_result {
	/** everything is okay */
	BEV_OK = 0,

	/** the filter needs to read more data before output */
	BEV_NEED_MORE = 1,

	/** the filter encountered a critical error, no further data
	    can be processed. */
	BEV_ERROR = 2
};

/** A callback function to implement a filter for a bufferevent.

    @param src An evbuffer to drain data from.
    @param dst An evbuffer to add data to.
    @param limit A suggested upper bound of bytes to write to dst.
       The filter may ignore this value, but doing so means that
       it will overflow the high-water mark associated with dst.
       -1 means "no limit".
    @param mode Whether we should write data as may be convenient
       (BEV_NORMAL), or flush as much data as we can (BEV_FLUSH),
       or flush as much as we can, possibly including an end-of-stream
       marker (BEV_FINISH).
    @param ctx A user-supplied pointer.

    @return BEV_OK if we wrote some data; BEV_NEED_MORE if we can't
       produce any more output until we get some input; and BEV_ERROR
       on an error.
 */
typedef enum bufferevent_filter_result (*bufferevent_filter_cb)(
    struct evbuffer *src, struct evbuffer *dst, ev_ssize_t dst_limit,
    enum bufferevent_flush_mode mode, void *ctx);

/**
   Allocate a new filtering bufferevent on top of an existing bufferevent.

   @param underlying the underlying bufferevent.
   @param input_filter The filter to apply to data we read from the underlying
     bufferevent
   @param output_filter The filer to apply to data we write to the underlying
     bufferevent
   @param options A bitfield of bufferevent options.
   @param free_context A function to use to free the filter context when
     this bufferevent is freed.
   @param ctx A context pointer to pass to the filter functions.
 */
EVENT2_EXPORT_SYMBOL
struct bufferevent *
bufferevent_filter_new(struct bufferevent *underlying,
		       bufferevent_filter_cb input_filter,
		       bufferevent_filter_cb output_filter,
		       int options,
		       void (*free_context)(void *),
		       void *ctx);
/**@}*/

/**
   Allocate a pair of linked bufferevents.  The bufferevents behave as would
   two bufferevent_sock instances connected to opposite ends of a
   socketpair(), except that no internal socketpair is allocated.

   @param base The event base to associate with the socketpair.
   @param options A set of options for this bufferevent
   @param pair A pointer to an array to hold the two new bufferevent objects.
   @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_pair_new(struct event_base *base, int options,
    struct bufferevent *pair[2]);

/**
   Given one bufferevent returned by bufferevent_pair_new(), returns the
   other one if it still exists.  Otherwise returns NULL.
 */
EVENT2_EXPORT_SYMBOL
struct bufferevent *bufferevent_pair_get_partner(struct bufferevent *bev);

/**
   Abstract type used to configure rate-limiting on a bufferevent or a group
   of bufferevents.
 */
struct ev_token_bucket_cfg;

/**
   A group of bufferevents which are configured to respect the same rate
   limit.
*/
struct bufferevent_rate_limit_group;

/** Maximum configurable rate- or burst-limit. */
#define EV_RATE_LIMIT_MAX EV_SSIZE_MAX

/**
   Initialize and return a new object to configure the rate-limiting behavior
   of bufferevents.

   @param read_rate The maximum number of bytes to read per tick on
     average.
   @param read_burst The maximum number of bytes to read in any single tick.
   @param write_rate The maximum number of bytes to write per tick on
     average.
   @param write_burst The maximum number of bytes to write in any single tick.
   @param tick_len The length of a single tick.	 Defaults to one second.
     Any fractions of a millisecond are ignored.

   Note that all rate-limits hare are currently best-effort: future versions
   of Libevent may implement them more tightly.
 */
EVENT2_EXPORT_SYMBOL
struct ev_token_bucket_cfg *ev_token_bucket_cfg_new(
	size_t read_rate, size_t read_burst,
	size_t write_rate, size_t write_burst,
	const struct timeval *tick_len);

/** Free all storage held in 'cfg'.

    Note: 'cfg' is not currently reference-counted; it is not safe to free it
    until no bufferevent is using it.
 */
EVENT2_EXPORT_SYMBOL
void ev_token_bucket_cfg_free(struct ev_token_bucket_cfg *cfg);

/**
   Set the rate-limit of a the bufferevent 'bev' to the one specified in
   'cfg'.  If 'cfg' is NULL, disable any per-bufferevent rate-limiting on
   'bev'.

   Note that only some bufferevent types currently respect rate-limiting.
   They are: socket-based bufferevents (normal and IOCP-based), and SSL-based
   bufferevents.

   Return 0 on sucess, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_set_rate_limit(struct bufferevent *bev,
    struct ev_token_bucket_cfg *cfg);

/**
   Create a new rate-limit group for bufferevents.  A rate-limit group
   constrains the maximum number of bytes sent and received, in toto,
   by all of its bufferevents.

   @param base An event_base to run any necessary timeouts for the group.
      Note that all bufferevents in the group do not necessarily need to share
      this event_base.
   @param cfg The rate-limit for this group.

   Note that all rate-limits hare are currently best-effort: future versions
   of Libevent may implement them more tightly.

   Note also that only some bufferevent types currently respect rate-limiting.
   They are: socket-based bufferevents (normal and IOCP-based), and SSL-based
   bufferevents.
 */
EVENT2_EXPORT_SYMBOL
struct bufferevent_rate_limit_group *bufferevent_rate_limit_group_new(
	struct event_base *base,
	const struct ev_token_bucket_cfg *cfg);
/**
   Change the rate-limiting settings for a given rate-limiting group.

   Return 0 on success, -1 on failure.
*/
EVENT2_EXPORT_SYMBOL
int bufferevent_rate_limit_group_set_cfg(
	struct bufferevent_rate_limit_group *,
	const struct ev_token_bucket_cfg *);

/**
   Change the smallest quantum we're willing to allocate to any single
   bufferevent in a group for reading or writing at a time.

   The rationale is that, because of TCP/IP protocol overheads and kernel
   behavior, if a rate-limiting group is so tight on bandwidth that you're
   only willing to send 1 byte per tick per bufferevent, you might instead
   want to batch up the reads and writes so that you send N bytes per
   1/N of the bufferevents (chosen at random) each tick, so you still wind
   up send 1 byte per tick per bufferevent on average, but you don't send
   so many tiny packets.

   The default min-share is currently 64 bytes.

   Returns 0 on success, -1 on faulre.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_rate_limit_group_set_min_share(
	struct bufferevent_rate_limit_group *, size_t);

/**
   Free a rate-limiting group.  The group must have no members when
   this function is called.
*/
EVENT2_EXPORT_SYMBOL
void bufferevent_rate_limit_group_free(struct bufferevent_rate_limit_group *);

/**
   Add 'bev' to the list of bufferevents whose aggregate reading and writing
   is restricted by 'g'.  If 'g' is NULL, remove 'bev' from its current group.

   A bufferevent may belong to no more than one rate-limit group at a time.
   If 'bev' is already a member of a group, it will be removed from its old
   group before being added to 'g'.

   Return 0 on success and -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_add_to_rate_limit_group(struct bufferevent *bev,
    struct bufferevent_rate_limit_group *g);

/** Remove 'bev' from its current rate-limit group (if any). */
EVENT2_EXPORT_SYMBOL
int bufferevent_remove_from_rate_limit_group(struct bufferevent *bev);

/**
   Set the size limit for single read operation.

   Set to 0 for a reasonable default.

   Return 0 on success and -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_set_max_single_read(struct bufferevent *bev, size_t size);

/**
   Set the size limit for single write operation.

   Set to 0 for a reasonable default.

   Return 0 on success and -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_set_max_single_write(struct bufferevent *bev, size_t size);

/** Get the current size limit for single read operation. */
EVENT2_EXPORT_SYMBOL
ev_ssize_t bufferevent_get_max_single_read(struct bufferevent *bev);

/** Get the current size limit for single write operation. */
EVENT2_EXPORT_SYMBOL
ev_ssize_t bufferevent_get_max_single_write(struct bufferevent *bev);

/**
   @name Rate limit inspection

   Return the current read or write bucket size for a bufferevent.
   If it is not configured with a per-bufferevent ratelimit, return
   EV_SSIZE_MAX.  This function does not inspect the group limit, if any.
   Note that it can return a negative value if the bufferevent has been
   made to read or write more than its limit.

   @{
 */
EVENT2_EXPORT_SYMBOL
ev_ssize_t bufferevent_get_read_limit(struct bufferevent *bev);
EVENT2_EXPORT_SYMBOL
ev_ssize_t bufferevent_get_write_limit(struct bufferevent *bev);
/*@}*/

EVENT2_EXPORT_SYMBOL
ev_ssize_t bufferevent_get_max_to_read(struct bufferevent *bev);
EVENT2_EXPORT_SYMBOL
ev_ssize_t bufferevent_get_max_to_write(struct bufferevent *bev);

EVENT2_EXPORT_SYMBOL
const struct ev_token_bucket_cfg *bufferevent_get_token_bucket_cfg(const struct bufferevent * bev);

/**
   @name Group Rate limit inspection

   Return the read or write bucket size for a bufferevent rate limit
   group.  Note that it can return a negative value if bufferevents in
   the group have been made to read or write more than their limits.

   @{
 */
EVENT2_EXPORT_SYMBOL
ev_ssize_t bufferevent_rate_limit_group_get_read_limit(
	struct bufferevent_rate_limit_group *);
EVENT2_EXPORT_SYMBOL
ev_ssize_t bufferevent_rate_limit_group_get_write_limit(
	struct bufferevent_rate_limit_group *);
/*@}*/

/**
   @name Rate limit manipulation

   Subtract a number of bytes from a bufferevent's read or write bucket.
   The decrement value can be negative, if you want to manually refill
   the bucket.	If the change puts the bucket above or below zero, the
   bufferevent will resume or suspend reading writing as appropriate.
   These functions make no change in the buckets for the bufferevent's
   group, if any.

   Returns 0 on success, -1 on internal error.

   @{
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_decrement_read_limit(struct bufferevent *bev, ev_ssize_t decr);
EVENT2_EXPORT_SYMBOL
int bufferevent_decrement_write_limit(struct bufferevent *bev, ev_ssize_t decr);
/*@}*/

/**
   @name Group rate limit manipulation

   Subtract a number of bytes from a bufferevent rate-limiting group's
   read or write bucket.  The decrement value can be negative, if you
   want to manually refill the bucket.	If the change puts the bucket
   above or below zero, the bufferevents in the group will resume or
   suspend reading writing as appropriate.

   Returns 0 on success, -1 on internal error.

   @{
 */
EVENT2_EXPORT_SYMBOL
int bufferevent_rate_limit_group_decrement_read(
	struct bufferevent_rate_limit_group *, ev_ssize_t);
EVENT2_EXPORT_SYMBOL
int bufferevent_rate_limit_group_decrement_write(
	struct bufferevent_rate_limit_group *, ev_ssize_t);
/*@}*/


/**
 * Inspect the total bytes read/written on a group.
 *
 * Set the variable pointed to by total_read_out to the total number of bytes
 * ever read on grp, and the variable pointed to by total_written_out to the
 * total number of bytes ever written on grp. */
EVENT2_EXPORT_SYMBOL
void bufferevent_rate_limit_group_get_totals(
    struct bufferevent_rate_limit_group *grp,
    ev_uint64_t *total_read_out, ev_uint64_t *total_written_out);

/**
 * Reset the total bytes read/written on a group.
 *
 * Reset the number of bytes read or written on grp as given by
 * bufferevent_rate_limit_group_reset_totals(). */
EVENT2_EXPORT_SYMBOL
void
bufferevent_rate_limit_group_reset_totals(
	struct bufferevent_rate_limit_group *grp);

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_BUFFEREVENT_H_INCLUDED_ */
