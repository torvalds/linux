/*
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
#ifndef EVENT2_BUFFER_H_INCLUDED_
#define EVENT2_BUFFER_H_INCLUDED_

/** @file event2/buffer.h

  Functions for buffering data for network sending or receiving.

  An evbuffer can be used for preparing data before sending it to
  the network or conversely for reading data from the network.
  Evbuffers try to avoid memory copies as much as possible.  As a
  result, evbuffers can be used to pass data around without actually
  incurring the overhead of copying the data.

  A new evbuffer can be allocated with evbuffer_new(), and can be
  freed with evbuffer_free().  Most users will be using evbuffers via
  the bufferevent interface.  To access a bufferevent's evbuffers, use
  bufferevent_get_input() and bufferevent_get_output().

  There are several guidelines for using evbuffers.

  - if you already know how much data you are going to add as a result
    of calling evbuffer_add() multiple times, it makes sense to use
    evbuffer_expand() first to make sure that enough memory is allocated
    before hand.

  - evbuffer_add_buffer() adds the contents of one buffer to the other
    without incurring any unnecessary memory copies.

  - evbuffer_add() and evbuffer_add_buffer() do not mix very well:
    if you use them, you will wind up with fragmented memory in your
	buffer.

  - For high-performance code, you may want to avoid copying data into and out
    of buffers.  You can skip the copy step by using
    evbuffer_reserve_space()/evbuffer_commit_space() when writing into a
    buffer, and evbuffer_peek() when reading.

  In Libevent 2.0 and later, evbuffers are represented using a linked
  list of memory chunks, with pointers to the first and last chunk in
  the chain.

  As the contents of an evbuffer can be stored in multiple different
  memory blocks, it cannot be accessed directly.  Instead, evbuffer_pullup()
  can be used to force a specified number of bytes to be contiguous. This
  will cause memory reallocation and memory copies if the data is split
  across multiple blocks.  It is more efficient, however, to use
  evbuffer_peek() if you don't require that the memory to be contiguous.
 */

#include <event2/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>
#include <stdarg.h>
#ifdef EVENT__HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef EVENT__HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#include <event2/util.h>

/**
   An evbuffer is an opaque data type for efficiently buffering data to be
   sent or received on the network.

   @see event2/event.h for more information
*/
struct evbuffer
#ifdef EVENT_IN_DOXYGEN_
{}
#endif
;

/**
    Pointer to a position within an evbuffer.

    Used when repeatedly searching through a buffer.  Calling any function
    that modifies or re-packs the buffer contents may invalidate all
    evbuffer_ptrs for that buffer.  Do not modify or contruct these values
    except with evbuffer_ptr_set.

    An evbuffer_ptr can represent any position from the start of a buffer up
    to a position immediately after the end of a buffer.

    @see evbuffer_ptr_set()
 */
struct evbuffer_ptr {
	ev_ssize_t pos;

	/* Do not alter or rely on the values of fields: they are for internal
	 * use */
	struct {
		void *chain;
		size_t pos_in_chain;
	} internal_;
};

/** Describes a single extent of memory inside an evbuffer.  Used for
    direct-access functions.

    @see evbuffer_reserve_space, evbuffer_commit_space, evbuffer_peek
 */
#ifdef EVENT__HAVE_SYS_UIO_H
#define evbuffer_iovec iovec
/* Internal use -- defined only if we are using the native struct iovec */
#define EVBUFFER_IOVEC_IS_NATIVE_
#else
struct evbuffer_iovec {
	/** The start of the extent of memory. */
	void *iov_base;
	/** The length of the extent of memory. */
	size_t iov_len;
};
#endif

/**
  Allocate storage for a new evbuffer.

  @return a pointer to a newly allocated evbuffer struct, or NULL if an error
	occurred
 */
EVENT2_EXPORT_SYMBOL
struct evbuffer *evbuffer_new(void);
/**
  Deallocate storage for an evbuffer.

  @param buf pointer to the evbuffer to be freed
 */
EVENT2_EXPORT_SYMBOL
void evbuffer_free(struct evbuffer *buf);

/**
   Enable locking on an evbuffer so that it can safely be used by multiple
   threads at the same time.

   NOTE: when locking is enabled, the lock will be held when callbacks are
   invoked.  This could result in deadlock if you aren't careful.  Plan
   accordingly!

   @param buf An evbuffer to make lockable.
   @param lock A lock object, or NULL if we should allocate our own.
   @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_enable_locking(struct evbuffer *buf, void *lock);

/**
   Acquire the lock on an evbuffer.  Has no effect if locking was not enabled
   with evbuffer_enable_locking.
*/
EVENT2_EXPORT_SYMBOL
void evbuffer_lock(struct evbuffer *buf);

/**
   Release the lock on an evbuffer.  Has no effect if locking was not enabled
   with evbuffer_enable_locking.
*/
EVENT2_EXPORT_SYMBOL
void evbuffer_unlock(struct evbuffer *buf);


/** If this flag is set, then we will not use evbuffer_peek(),
 * evbuffer_remove(), evbuffer_remove_buffer(), and so on to read bytes
 * from this buffer: we'll only take bytes out of this buffer by
 * writing them to the network (as with evbuffer_write_atmost), by
 * removing them without observing them (as with evbuffer_drain),
 * or by copying them all out at once (as with evbuffer_add_buffer).
 *
 * Using this option allows the implementation to use sendfile-based
 * operations for evbuffer_add_file(); see that function for more
 * information.
 *
 * This flag is on by default for bufferevents that can take advantage
 * of it; you should never actually need to set it on a bufferevent's
 * output buffer.
 */
#define EVBUFFER_FLAG_DRAINS_TO_FD 1

/** Change the flags that are set for an evbuffer by adding more.
 *
 * @param buffer the evbuffer that the callback is watching.
 * @param cb the callback whose status we want to change.
 * @param flags One or more EVBUFFER_FLAG_* options
 * @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_set_flags(struct evbuffer *buf, ev_uint64_t flags);
/** Change the flags that are set for an evbuffer by removing some.
 *
 * @param buffer the evbuffer that the callback is watching.
 * @param cb the callback whose status we want to change.
 * @param flags One or more EVBUFFER_FLAG_* options
 * @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_clear_flags(struct evbuffer *buf, ev_uint64_t flags);

/**
  Returns the total number of bytes stored in the evbuffer

  @param buf pointer to the evbuffer
  @return the number of bytes stored in the evbuffer
*/
EVENT2_EXPORT_SYMBOL
size_t evbuffer_get_length(const struct evbuffer *buf);

/**
   Returns the number of contiguous available bytes in the first buffer chain.

   This is useful when processing data that might be split into multiple
   chains, or that might all be in the first chain.  Calls to
   evbuffer_pullup() that cause reallocation and copying of data can thus be
   avoided.

   @param buf pointer to the evbuffer
   @return 0 if no data is available, otherwise the number of available bytes
     in the first buffer chain.
*/
EVENT2_EXPORT_SYMBOL
size_t evbuffer_get_contiguous_space(const struct evbuffer *buf);

/**
  Expands the available space in an evbuffer.

  Expands the available space in the evbuffer to at least datlen, so that
  appending datlen additional bytes will not require any new allocations.

  @param buf the evbuffer to be expanded
  @param datlen the new minimum length requirement
  @return 0 if successful, or -1 if an error occurred
*/
EVENT2_EXPORT_SYMBOL
int evbuffer_expand(struct evbuffer *buf, size_t datlen);

/**
   Reserves space in the last chain or chains of an evbuffer.

   Makes space available in the last chain or chains of an evbuffer that can
   be arbitrarily written to by a user.  The space does not become
   available for reading until it has been committed with
   evbuffer_commit_space().

   The space is made available as one or more extents, represented by
   an initial pointer and a length.  You can force the memory to be
   available as only one extent.  Allowing more extents, however, makes the
   function more efficient.

   Multiple subsequent calls to this function will make the same space
   available until evbuffer_commit_space() has been called.

   It is an error to do anything that moves around the buffer's internal
   memory structures before committing the space.

   NOTE: The code currently does not ever use more than two extents.
   This may change in future versions.

   @param buf the evbuffer in which to reserve space.
   @param size how much space to make available, at minimum.  The
      total length of the extents may be greater than the requested
      length.
   @param vec an array of one or more evbuffer_iovec structures to
      hold pointers to the reserved extents of memory.
   @param n_vec The length of the vec array.  Must be at least 1;
       2 is more efficient.
   @return the number of provided extents, or -1 on error.
   @see evbuffer_commit_space()
*/
EVENT2_EXPORT_SYMBOL
int
evbuffer_reserve_space(struct evbuffer *buf, ev_ssize_t size,
    struct evbuffer_iovec *vec, int n_vec);

/**
   Commits previously reserved space.

   Commits some of the space previously reserved with
   evbuffer_reserve_space().  It then becomes available for reading.

   This function may return an error if the pointer in the extents do
   not match those returned from evbuffer_reserve_space, or if data
   has been added to the buffer since the space was reserved.

   If you want to commit less data than you got reserved space for,
   modify the iov_len pointer of the appropriate extent to a smaller
   value.  Note that you may have received more space than you
   requested if it was available!

   @param buf the evbuffer in which to reserve space.
   @param vec one or two extents returned by evbuffer_reserve_space.
   @param n_vecs the number of extents.
   @return 0 on success, -1 on error
   @see evbuffer_reserve_space()
*/
EVENT2_EXPORT_SYMBOL
int evbuffer_commit_space(struct evbuffer *buf,
    struct evbuffer_iovec *vec, int n_vecs);

/**
  Append data to the end of an evbuffer.

  @param buf the evbuffer to be appended to
  @param data pointer to the beginning of the data buffer
  @param datlen the number of bytes to be copied from the data buffer
  @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_add(struct evbuffer *buf, const void *data, size_t datlen);


/**
  Read data from an evbuffer and drain the bytes read.

  If more bytes are requested than are available in the evbuffer, we
  only extract as many bytes as were available.

  @param buf the evbuffer to be read from
  @param data the destination buffer to store the result
  @param datlen the maximum size of the destination buffer
  @return the number of bytes read, or -1 if we can't drain the buffer.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_remove(struct evbuffer *buf, void *data, size_t datlen);

/**
  Read data from an evbuffer, and leave the buffer unchanged.

  If more bytes are requested than are available in the evbuffer, we
  only extract as many bytes as were available.

  @param buf the evbuffer to be read from
  @param data_out the destination buffer to store the result
  @param datlen the maximum size of the destination buffer
  @return the number of bytes read, or -1 if we can't drain the buffer.
 */
EVENT2_EXPORT_SYMBOL
ev_ssize_t evbuffer_copyout(struct evbuffer *buf, void *data_out, size_t datlen);

/**
  Read data from the middle of an evbuffer, and leave the buffer unchanged.

  If more bytes are requested than are available in the evbuffer, we
  only extract as many bytes as were available.

  @param buf the evbuffer to be read from
  @param pos the position to start reading from
  @param data_out the destination buffer to store the result
  @param datlen the maximum size of the destination buffer
  @return the number of bytes read, or -1 if we can't drain the buffer.
 */
EVENT2_EXPORT_SYMBOL
ev_ssize_t evbuffer_copyout_from(struct evbuffer *buf, const struct evbuffer_ptr *pos, void *data_out, size_t datlen);

/**
  Read data from an evbuffer into another evbuffer, draining
  the bytes from the source buffer.  This function avoids copy
  operations to the extent possible.

  If more bytes are requested than are available in src, the src
  buffer is drained completely.

  @param src the evbuffer to be read from
  @param dst the destination evbuffer to store the result into
  @param datlen the maximum numbers of bytes to transfer
  @return the number of bytes read
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_remove_buffer(struct evbuffer *src, struct evbuffer *dst,
    size_t datlen);

/** Used to tell evbuffer_readln what kind of line-ending to look for.
 */
enum evbuffer_eol_style {
	/** Any sequence of CR and LF characters is acceptable as an
	 * EOL.
	 *
	 * Note that this style can produce ambiguous results: the
	 * sequence "CRLF" will be treated as a single EOL if it is
	 * all in the buffer at once, but if you first read a CR from
	 * the network and later read an LF from the network, it will
	 * be treated as two EOLs.
	 */
	EVBUFFER_EOL_ANY,
	/** An EOL is an LF, optionally preceded by a CR.  This style is
	 * most useful for implementing text-based internet protocols. */
	EVBUFFER_EOL_CRLF,
	/** An EOL is a CR followed by an LF. */
	EVBUFFER_EOL_CRLF_STRICT,
	/** An EOL is a LF. */
	EVBUFFER_EOL_LF,
	/** An EOL is a NUL character (that is, a single byte with value 0) */
	EVBUFFER_EOL_NUL
};

/**
 * Read a single line from an evbuffer.
 *
 * Reads a line terminated by an EOL as determined by the evbuffer_eol_style
 * argument.  Returns a newly allocated nul-terminated string; the caller must
 * free the returned value.  The EOL is not included in the returned string.
 *
 * @param buffer the evbuffer to read from
 * @param n_read_out if non-NULL, points to a size_t that is set to the
 *       number of characters in the returned string.  This is useful for
 *       strings that can contain NUL characters.
 * @param eol_style the style of line-ending to use.
 * @return pointer to a single line, or NULL if an error occurred
 */
EVENT2_EXPORT_SYMBOL
char *evbuffer_readln(struct evbuffer *buffer, size_t *n_read_out,
    enum evbuffer_eol_style eol_style);

/**
  Move all data from one evbuffer into another evbuffer.

  This is a destructive add.  The data from one buffer moves into
  the other buffer.  However, no unnecessary memory copies occur.

  @param outbuf the output buffer
  @param inbuf the input buffer
  @return 0 if successful, or -1 if an error occurred

  @see evbuffer_remove_buffer()
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_add_buffer(struct evbuffer *outbuf, struct evbuffer *inbuf);

/**
  Copy data from one evbuffer into another evbuffer.

  This is a non-destructive add.  The data from one buffer is copied
  into the other buffer.  However, no unnecessary memory copies occur.

  Note that buffers already containing buffer references can't be added
  to other buffers.

  @param outbuf the output buffer
  @param inbuf the input buffer
  @return 0 if successful, or -1 if an error occurred
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_add_buffer_reference(struct evbuffer *outbuf,
    struct evbuffer *inbuf);

/**
   A cleanup function for a piece of memory added to an evbuffer by
   reference.

   @see evbuffer_add_reference()
 */
typedef void (*evbuffer_ref_cleanup_cb)(const void *data,
    size_t datalen, void *extra);

/**
  Reference memory into an evbuffer without copying.

  The memory needs to remain valid until all the added data has been
  read.  This function keeps just a reference to the memory without
  actually incurring the overhead of a copy.

  @param outbuf the output buffer
  @param data the memory to reference
  @param datlen how memory to reference
  @param cleanupfn callback to be invoked when the memory is no longer
	referenced by this evbuffer.
  @param cleanupfn_arg optional argument to the cleanup callback
  @return 0 if successful, or -1 if an error occurred
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_add_reference(struct evbuffer *outbuf,
    const void *data, size_t datlen,
    evbuffer_ref_cleanup_cb cleanupfn, void *cleanupfn_arg);

/**
  Copy data from a file into the evbuffer for writing to a socket.

  This function avoids unnecessary data copies between userland and
  kernel.  If sendfile is available and the EVBUFFER_FLAG_DRAINS_TO_FD
  flag is set, it uses those functions.  Otherwise, it tries to use
  mmap (or CreateFileMapping on Windows).

  The function owns the resulting file descriptor and will close it
  when finished transferring data.

  The results of using evbuffer_remove() or evbuffer_pullup() on
  evbuffers whose data was added using this function are undefined.

  For more fine-grained control, use evbuffer_add_file_segment.

  @param outbuf the output buffer
  @param fd the file descriptor
  @param offset the offset from which to read data
  @param length how much data to read, or -1 to read as much as possible.
    (-1 requires that 'fd' support fstat.)
  @return 0 if successful, or -1 if an error occurred
*/

EVENT2_EXPORT_SYMBOL
int evbuffer_add_file(struct evbuffer *outbuf, int fd, ev_off_t offset,
    ev_off_t length);

/**
  An evbuffer_file_segment holds a reference to a range of a file --
  possibly the whole file! -- for use in writing from an evbuffer to a
  socket.  It could be implemented with mmap, sendfile, splice, or (if all
  else fails) by just pulling all the data into RAM.  A single
  evbuffer_file_segment can be added more than once, and to more than one
  evbuffer.
 */
struct evbuffer_file_segment;

/**
    Flag for creating evbuffer_file_segment: If this flag is set, then when
    the evbuffer_file_segment is freed and no longer in use by any
    evbuffer, the underlying fd is closed.
 */
#define EVBUF_FS_CLOSE_ON_FREE    0x01
/**
   Flag for creating evbuffer_file_segment: Disable memory-map based
   implementations.
 */
#define EVBUF_FS_DISABLE_MMAP     0x02
/**
   Flag for creating evbuffer_file_segment: Disable direct fd-to-fd
   implementations (including sendfile and splice).

   You might want to use this option if data needs to be taken from the
   evbuffer by any means other than writing it to the network: the sendfile
   backend is fast, but it only works for sending files directly to the
   network.
 */
#define EVBUF_FS_DISABLE_SENDFILE 0x04
/**
   Flag for creating evbuffer_file_segment: Do not allocate a lock for this
   segment.  If this option is set, then neither the segment nor any
   evbuffer it is added to may ever be accessed from more than one thread
   at a time.
 */
#define EVBUF_FS_DISABLE_LOCKING  0x08

/**
   A cleanup function for a evbuffer_file_segment added to an evbuffer
   for reference.
 */
typedef void (*evbuffer_file_segment_cleanup_cb)(
    struct evbuffer_file_segment const* seg, int flags, void* arg);

/**
   Create and return a new evbuffer_file_segment for reading data from a
   file and sending it out via an evbuffer.

   This function avoids unnecessary data copies between userland and
   kernel.  Where available, it uses sendfile or splice.

   The file descriptor must not be closed so long as any evbuffer is using
   this segment.

   The results of using evbuffer_remove() or evbuffer_pullup() or any other
   function that reads bytes from an evbuffer on any evbuffer containing
   the newly returned segment are undefined, unless you pass the
   EVBUF_FS_DISABLE_SENDFILE flag to this function.

   @param fd an open file to read from.
   @param offset an index within the file at which to start reading
   @param length how much data to read, or -1 to read as much as possible.
      (-1 requires that 'fd' support fstat.)
   @param flags any number of the EVBUF_FS_* flags
   @return a new evbuffer_file_segment, or NULL on failure.
 **/
EVENT2_EXPORT_SYMBOL
struct evbuffer_file_segment *evbuffer_file_segment_new(
	int fd, ev_off_t offset, ev_off_t length, unsigned flags);

/**
   Free an evbuffer_file_segment

   It is safe to call this function even if the segment has been added to
   one or more evbuffers.  The evbuffer_file_segment will not be freed
   until no more references to it exist.
 */
EVENT2_EXPORT_SYMBOL
void evbuffer_file_segment_free(struct evbuffer_file_segment *seg);

/**
   Add cleanup callback and argument for the callback to an
   evbuffer_file_segment.

   The cleanup callback will be invoked when no more references to the
   evbuffer_file_segment exist.
 **/
EVENT2_EXPORT_SYMBOL
void evbuffer_file_segment_add_cleanup_cb(struct evbuffer_file_segment *seg,
	evbuffer_file_segment_cleanup_cb cb, void* arg);

/**
   Insert some or all of an evbuffer_file_segment at the end of an evbuffer

   Note that the offset and length parameters of this function have a
   different meaning from those provided to evbuffer_file_segment_new: When
   you create the segment, the offset is the offset _within the file_, and
   the length is the length _of the segment_, whereas when you add a
   segment to an evbuffer, the offset is _within the segment_ and the
   length is the length of the _part of the segment you want to use.

   In other words, if you have a 10 KiB file, and you create an
   evbuffer_file_segment for it with offset 20 and length 1000, it will
   refer to bytes 20..1019 inclusive.  If you then pass this segment to
   evbuffer_add_file_segment and specify an offset of 20 and a length of
   50, you will be adding bytes 40..99 inclusive.

   @param buf the evbuffer to append to
   @param seg the segment to add
   @param offset the offset within the segment to start from
   @param length the amount of data to add, or -1 to add it all.
   @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_add_file_segment(struct evbuffer *buf,
    struct evbuffer_file_segment *seg, ev_off_t offset, ev_off_t length);

/**
  Append a formatted string to the end of an evbuffer.

  The string is formated as printf.

  @param buf the evbuffer that will be appended to
  @param fmt a format string
  @param ... arguments that will be passed to printf(3)
  @return The number of bytes added if successful, or -1 if an error occurred.

  @see evutil_printf(), evbuffer_add_vprintf()
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_add_printf(struct evbuffer *buf, const char *fmt, ...)
#ifdef __GNUC__
  __attribute__((format(printf, 2, 3)))
#endif
;

/**
  Append a va_list formatted string to the end of an evbuffer.

  @param buf the evbuffer that will be appended to
  @param fmt a format string
  @param ap a varargs va_list argument array that will be passed to vprintf(3)
  @return The number of bytes added if successful, or -1 if an error occurred.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_add_vprintf(struct evbuffer *buf, const char *fmt, va_list ap)
#ifdef __GNUC__
	__attribute__((format(printf, 2, 0)))
#endif
;


/**
  Remove a specified number of bytes data from the beginning of an evbuffer.

  @param buf the evbuffer to be drained
  @param len the number of bytes to drain from the beginning of the buffer
  @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_drain(struct evbuffer *buf, size_t len);


/**
  Write the contents of an evbuffer to a file descriptor.

  The evbuffer will be drained after the bytes have been successfully written.

  @param buffer the evbuffer to be written and drained
  @param fd the file descriptor to be written to
  @return the number of bytes written, or -1 if an error occurred
  @see evbuffer_read()
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_write(struct evbuffer *buffer, evutil_socket_t fd);

/**
  Write some of the contents of an evbuffer to a file descriptor.

  The evbuffer will be drained after the bytes have been successfully written.

  @param buffer the evbuffer to be written and drained
  @param fd the file descriptor to be written to
  @param howmuch the largest allowable number of bytes to write, or -1
	to write as many bytes as we can.
  @return the number of bytes written, or -1 if an error occurred
  @see evbuffer_read()
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_write_atmost(struct evbuffer *buffer, evutil_socket_t fd,
						  ev_ssize_t howmuch);

/**
  Read from a file descriptor and store the result in an evbuffer.

  @param buffer the evbuffer to store the result
  @param fd the file descriptor to read from
  @param howmuch the number of bytes to be read
  @return the number of bytes read, or -1 if an error occurred
  @see evbuffer_write()
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_read(struct evbuffer *buffer, evutil_socket_t fd, int howmuch);

/**
   Search for a string within an evbuffer.

   @param buffer the evbuffer to be searched
   @param what the string to be searched for
   @param len the length of the search string
   @param start NULL or a pointer to a valid struct evbuffer_ptr.
   @return a struct evbuffer_ptr whose 'pos' field has the offset of the
     first occurrence of the string in the buffer after 'start'.  The 'pos'
     field of the result is -1 if the string was not found.
 */
EVENT2_EXPORT_SYMBOL
struct evbuffer_ptr evbuffer_search(struct evbuffer *buffer, const char *what, size_t len, const struct evbuffer_ptr *start);

/**
   Search for a string within part of an evbuffer.

   @param buffer the evbuffer to be searched
   @param what the string to be searched for
   @param len the length of the search string
   @param start NULL or a pointer to a valid struct evbuffer_ptr that
     indicates where we should start searching.
   @param end NULL or a pointer to a valid struct evbuffer_ptr that
     indicates where we should stop searching.
   @return a struct evbuffer_ptr whose 'pos' field has the offset of the
     first occurrence of the string in the buffer after 'start'.  The 'pos'
     field of the result is -1 if the string was not found.
 */
EVENT2_EXPORT_SYMBOL
struct evbuffer_ptr evbuffer_search_range(struct evbuffer *buffer, const char *what, size_t len, const struct evbuffer_ptr *start, const struct evbuffer_ptr *end);

/**
   Defines how to adjust an evbuffer_ptr by evbuffer_ptr_set()

   @see evbuffer_ptr_set() */
enum evbuffer_ptr_how {
	/** Sets the pointer to the position; can be called on with an
	    uninitialized evbuffer_ptr. */
	EVBUFFER_PTR_SET,
	/** Advances the pointer by adding to the current position. */
	EVBUFFER_PTR_ADD
};

/**
   Sets the search pointer in the buffer to position.

   There are two ways to use this function: you can call
      evbuffer_ptr_set(buf, &pos, N, EVBUFFER_PTR_SET)
   to move 'pos' to a position 'N' bytes after the start of the buffer, or
      evbuffer_ptr_set(buf, &pos, N, EVBUFFER_PTR_ADD)
   to move 'pos' forward by 'N' bytes.

   If evbuffer_ptr is not initialized, this function can only be called
   with EVBUFFER_PTR_SET.

   An evbuffer_ptr can represent any position from the start of the buffer to
   a position immediately after the end of the buffer.

   @param buffer the evbuffer to be search
   @param ptr a pointer to a struct evbuffer_ptr
   @param position the position at which to start the next search
   @param how determines how the pointer should be manipulated.
   @returns 0 on success or -1 otherwise
*/
EVENT2_EXPORT_SYMBOL
int
evbuffer_ptr_set(struct evbuffer *buffer, struct evbuffer_ptr *ptr,
    size_t position, enum evbuffer_ptr_how how);

/**
   Search for an end-of-line string within an evbuffer.

   @param buffer the evbuffer to be searched
   @param start NULL or a pointer to a valid struct evbuffer_ptr to start
      searching at.
   @param eol_len_out If non-NULL, the pointed-to value will be set to
      the length of the end-of-line string.
   @param eol_style The kind of EOL to look for; see evbuffer_readln() for
      more information
   @return a struct evbuffer_ptr whose 'pos' field has the offset of the
     first occurrence EOL in the buffer after 'start'.  The 'pos'
     field of the result is -1 if the string was not found.
 */
EVENT2_EXPORT_SYMBOL
struct evbuffer_ptr evbuffer_search_eol(struct evbuffer *buffer,
    struct evbuffer_ptr *start, size_t *eol_len_out,
    enum evbuffer_eol_style eol_style);

/** Function to peek at data inside an evbuffer without removing it or
    copying it out.

    Pointers to the data are returned by filling the 'vec_out' array
    with pointers to one or more extents of data inside the buffer.

    The total data in the extents that you get back may be more than
    you requested (if there is more data last extent than you asked
    for), or less (if you do not provide enough evbuffer_iovecs, or if
    the buffer does not have as much data as you asked to see).

    @param buffer the evbuffer to peek into,
    @param len the number of bytes to try to peek.  If len is negative, we
       will try to fill as much of vec_out as we can.  If len is negative
       and vec_out is not provided, we return the number of evbuffer_iovecs
       that would be needed to get all the data in the buffer.
    @param start_at an evbuffer_ptr indicating the point at which we
       should start looking for data.  NULL means, "At the start of the
       buffer."
    @param vec_out an array of evbuffer_iovec
    @param n_vec the length of vec_out.  If 0, we only count how many
       extents would be necessary to point to the requested amount of
       data.
    @return The number of extents needed.  This may be less than n_vec
       if we didn't need all the evbuffer_iovecs we were given, or more
       than n_vec if we would need more to return all the data that was
       requested.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_peek(struct evbuffer *buffer, ev_ssize_t len,
    struct evbuffer_ptr *start_at,
    struct evbuffer_iovec *vec_out, int n_vec);


/** Structure passed to an evbuffer_cb_func evbuffer callback

    @see evbuffer_cb_func, evbuffer_add_cb()
 */
struct evbuffer_cb_info {
	/** The number of bytes in this evbuffer when callbacks were last
	 * invoked. */
	size_t orig_size;
	/** The number of bytes added since callbacks were last invoked. */
	size_t n_added;
	/** The number of bytes removed since callbacks were last invoked. */
	size_t n_deleted;
};

/** Type definition for a callback that is invoked whenever data is added or
    removed from an evbuffer.

    An evbuffer may have one or more callbacks set at a time.  The order
    in which they are executed is undefined.

    A callback function may add more callbacks, or remove itself from the
    list of callbacks, or add or remove data from the buffer.  It may not
    remove another callback from the list.

    If a callback adds or removes data from the buffer or from another
    buffer, this can cause a recursive invocation of your callback or
    other callbacks.  If you ask for an infinite loop, you might just get
    one: watch out!

    @param buffer the buffer whose size has changed
    @param info a structure describing how the buffer changed.
    @param arg a pointer to user data
*/
typedef void (*evbuffer_cb_func)(struct evbuffer *buffer, const struct evbuffer_cb_info *info, void *arg);

struct evbuffer_cb_entry;
/** Add a new callback to an evbuffer.

  Subsequent calls to evbuffer_add_cb() add new callbacks.  To remove this
  callback, call evbuffer_remove_cb or evbuffer_remove_cb_entry.

  @param buffer the evbuffer to be monitored
  @param cb the callback function to invoke when the evbuffer is modified,
	or NULL to remove all callbacks.
  @param cbarg an argument to be provided to the callback function
  @return a handle to the callback on success, or NULL on failure.
 */
EVENT2_EXPORT_SYMBOL
struct evbuffer_cb_entry *evbuffer_add_cb(struct evbuffer *buffer, evbuffer_cb_func cb, void *cbarg);

/** Remove a callback from an evbuffer, given a handle returned from
    evbuffer_add_cb.

    Calling this function invalidates the handle.

    @return 0 if a callback was removed, or -1 if no matching callback was
    found.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_remove_cb_entry(struct evbuffer *buffer,
			     struct evbuffer_cb_entry *ent);

/** Remove a callback from an evbuffer, given the function and argument
    used to add it.

    @return 0 if a callback was removed, or -1 if no matching callback was
    found.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_remove_cb(struct evbuffer *buffer, evbuffer_cb_func cb, void *cbarg);

/** If this flag is not set, then a callback is temporarily disabled, and
 * should not be invoked.
 *
 * @see evbuffer_cb_set_flags(), evbuffer_cb_clear_flags()
 */
#define EVBUFFER_CB_ENABLED 1

/** Change the flags that are set for a callback on a buffer by adding more.

    @param buffer the evbuffer that the callback is watching.
    @param cb the callback whose status we want to change.
    @param flags EVBUFFER_CB_ENABLED to re-enable the callback.
    @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_cb_set_flags(struct evbuffer *buffer,
			  struct evbuffer_cb_entry *cb, ev_uint32_t flags);

/** Change the flags that are set for a callback on a buffer by removing some

    @param buffer the evbuffer that the callback is watching.
    @param cb the callback whose status we want to change.
    @param flags EVBUFFER_CB_ENABLED to disable the callback.
    @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_cb_clear_flags(struct evbuffer *buffer,
			  struct evbuffer_cb_entry *cb, ev_uint32_t flags);

#if 0
/** Postpone calling a given callback until unsuspend is called later.

    This is different from disabling the callback, since the callback will get
	invoked later if the buffer size changes between now and when we unsuspend
	it.

	@param the buffer that the callback is watching.
	@param cb the callback we want to suspend.
 */
EVENT2_EXPORT_SYMBOL
void evbuffer_cb_suspend(struct evbuffer *buffer, struct evbuffer_cb_entry *cb);
/** Stop postponing a callback that we postponed with evbuffer_cb_suspend.

	If data was added to or removed from the buffer while the callback was
	suspended, the callback will get called once now.

	@param the buffer that the callback is watching.
	@param cb the callback we want to stop suspending.
 */
EVENT2_EXPORT_SYMBOL
void evbuffer_cb_unsuspend(struct evbuffer *buffer, struct evbuffer_cb_entry *cb);
#endif

/**
  Makes the data at the beginning of an evbuffer contiguous.

  @param buf the evbuffer to make contiguous
  @param size the number of bytes to make contiguous, or -1 to make the
	entire buffer contiguous.
  @return a pointer to the contiguous memory array, or NULL if param size
	requested more data than is present in the buffer.
*/

EVENT2_EXPORT_SYMBOL
unsigned char *evbuffer_pullup(struct evbuffer *buf, ev_ssize_t size);

/**
  Prepends data to the beginning of the evbuffer

  @param buf the evbuffer to which to prepend data
  @param data a pointer to the memory to prepend
  @param size the number of bytes to prepend
  @return 0 if successful, or -1 otherwise
*/

EVENT2_EXPORT_SYMBOL
int evbuffer_prepend(struct evbuffer *buf, const void *data, size_t size);

/**
  Prepends all data from the src evbuffer to the beginning of the dst
  evbuffer.

  @param dst the evbuffer to which to prepend data
  @param src the evbuffer to prepend; it will be emptied as a result
  @return 0 if successful, or -1 otherwise
*/
EVENT2_EXPORT_SYMBOL
int evbuffer_prepend_buffer(struct evbuffer *dst, struct evbuffer* src);

/**
   Prevent calls that modify an evbuffer from succeeding. A buffer may
   frozen at the front, at the back, or at both the front and the back.

   If the front of a buffer is frozen, operations that drain data from
   the front of the buffer, or that prepend data to the buffer, will
   fail until it is unfrozen.   If the back a buffer is frozen, operations
   that append data from the buffer will fail until it is unfrozen.

   @param buf The buffer to freeze
   @param at_front If true, we freeze the front of the buffer.  If false,
      we freeze the back.
   @return 0 on success, -1 on failure.
*/
EVENT2_EXPORT_SYMBOL
int evbuffer_freeze(struct evbuffer *buf, int at_front);
/**
   Re-enable calls that modify an evbuffer.

   @param buf The buffer to un-freeze
   @param at_front If true, we unfreeze the front of the buffer.  If false,
      we unfreeze the back.
   @return 0 on success, -1 on failure.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_unfreeze(struct evbuffer *buf, int at_front);

struct event_base;
/**
   Force all the callbacks on an evbuffer to be run, not immediately after
   the evbuffer is altered, but instead from inside the event loop.

   This can be used to serialize all the callbacks to a single thread
   of execution.
 */
EVENT2_EXPORT_SYMBOL
int evbuffer_defer_callbacks(struct evbuffer *buffer, struct event_base *base);

/**
  Append data from 1 or more iovec's to an evbuffer

  Calculates the number of bytes needed for an iovec structure and guarantees
  all data will fit into a single chain. Can be used in lieu of functionality
  which calls evbuffer_add() constantly before being used to increase
  performance.

  @param buffer the destination buffer
  @param vec the source iovec
  @param n_vec the number of iovec structures.
  @return the number of bytes successfully written to the output buffer.
*/
EVENT2_EXPORT_SYMBOL
size_t evbuffer_add_iovec(struct evbuffer * buffer, struct evbuffer_iovec * vec, int n_vec);

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_BUFFER_H_INCLUDED_ */
