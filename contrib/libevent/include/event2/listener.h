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
#ifndef EVENT2_LISTENER_H_INCLUDED_
#define EVENT2_LISTENER_H_INCLUDED_

#include <event2/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>

struct sockaddr;
struct evconnlistener;

/**
   A callback that we invoke when a listener has a new connection.

   @param listener The evconnlistener
   @param fd The new file descriptor
   @param addr The source address of the connection
   @param socklen The length of addr
   @param user_arg the pointer passed to evconnlistener_new()
 */
typedef void (*evconnlistener_cb)(struct evconnlistener *, evutil_socket_t, struct sockaddr *, int socklen, void *);

/**
   A callback that we invoke when a listener encounters a non-retriable error.

   @param listener The evconnlistener
   @param user_arg the pointer passed to evconnlistener_new()
 */
typedef void (*evconnlistener_errorcb)(struct evconnlistener *, void *);

/** Flag: Indicates that we should not make incoming sockets nonblocking
 * before passing them to the callback. */
#define LEV_OPT_LEAVE_SOCKETS_BLOCKING	(1u<<0)
/** Flag: Indicates that freeing the listener should close the underlying
 * socket. */
#define LEV_OPT_CLOSE_ON_FREE		(1u<<1)
/** Flag: Indicates that we should set the close-on-exec flag, if possible */
#define LEV_OPT_CLOSE_ON_EXEC		(1u<<2)
/** Flag: Indicates that we should disable the timeout (if any) between when
 * this socket is closed and when we can listen again on the same port. */
#define LEV_OPT_REUSEABLE		(1u<<3)
/** Flag: Indicates that the listener should be locked so it's safe to use
 * from multiple threadcs at once. */
#define LEV_OPT_THREADSAFE		(1u<<4)
/** Flag: Indicates that the listener should be created in disabled
 * state. Use evconnlistener_enable() to enable it later. */
#define LEV_OPT_DISABLED		(1u<<5)
/** Flag: Indicates that the listener should defer accept() until data is
 * available, if possible.  Ignored on platforms that do not support this.
 *
 * This option can help performance for protocols where the client transmits
 * immediately after connecting.  Do not use this option if your protocol
 * _doesn't_ start out with the client transmitting data, since in that case
 * this option will sometimes cause the kernel to never tell you about the
 * connection.
 *
 * This option is only supported by evconnlistener_new_bind(): it can't
 * work with evconnlistener_new_fd(), since the listener needs to be told
 * to use the option before it is actually bound.
 */
#define LEV_OPT_DEFERRED_ACCEPT		(1u<<6)
/** Flag: Indicates that we ask to allow multiple servers (processes or
 * threads) to bind to the same port if they each set the option. 
 * 
 * SO_REUSEPORT is what most people would expect SO_REUSEADDR to be, however
 * SO_REUSEPORT does not imply SO_REUSEADDR.
 *
 * This is only available on Linux and kernel 3.9+
 */
#define LEV_OPT_REUSEABLE_PORT		(1u<<7)

/**
   Allocate a new evconnlistener object to listen for incoming TCP connections
   on a given file descriptor.

   @param base The event base to associate the listener with.
   @param cb A callback to be invoked when a new connection arrives.  If the
      callback is NULL, the listener will be treated as disabled until the
      callback is set.
   @param ptr A user-supplied pointer to give to the callback.
   @param flags Any number of LEV_OPT_* flags
   @param backlog Passed to the listen() call to determine the length of the
      acceptable connection backlog.  Set to -1 for a reasonable default.
      Set to 0 if the socket is already listening.
   @param fd The file descriptor to listen on.  It must be a nonblocking
      file descriptor, and it should already be bound to an appropriate
      port and address.
*/
EVENT2_EXPORT_SYMBOL
struct evconnlistener *evconnlistener_new(struct event_base *base,
    evconnlistener_cb cb, void *ptr, unsigned flags, int backlog,
    evutil_socket_t fd);
/**
   Allocate a new evconnlistener object to listen for incoming TCP connections
   on a given address.

   @param base The event base to associate the listener with.
   @param cb A callback to be invoked when a new connection arrives. If the
      callback is NULL, the listener will be treated as disabled until the
      callback is set.
   @param ptr A user-supplied pointer to give to the callback.
   @param flags Any number of LEV_OPT_* flags
   @param backlog Passed to the listen() call to determine the length of the
      acceptable connection backlog.  Set to -1 for a reasonable default.
   @param addr The address to listen for connections on.
   @param socklen The length of the address.
 */
EVENT2_EXPORT_SYMBOL
struct evconnlistener *evconnlistener_new_bind(struct event_base *base,
    evconnlistener_cb cb, void *ptr, unsigned flags, int backlog,
    const struct sockaddr *sa, int socklen);
/**
   Disable and deallocate an evconnlistener.
 */
EVENT2_EXPORT_SYMBOL
void evconnlistener_free(struct evconnlistener *lev);
/**
   Re-enable an evconnlistener that has been disabled.
 */
EVENT2_EXPORT_SYMBOL
int evconnlistener_enable(struct evconnlistener *lev);
/**
   Stop listening for connections on an evconnlistener.
 */
EVENT2_EXPORT_SYMBOL
int evconnlistener_disable(struct evconnlistener *lev);

/** Return an evconnlistener's associated event_base. */
EVENT2_EXPORT_SYMBOL
struct event_base *evconnlistener_get_base(struct evconnlistener *lev);

/** Return the socket that an evconnlistner is listening on. */
EVENT2_EXPORT_SYMBOL
evutil_socket_t evconnlistener_get_fd(struct evconnlistener *lev);

/** Change the callback on the listener to cb and its user_data to arg.
 */
EVENT2_EXPORT_SYMBOL
void evconnlistener_set_cb(struct evconnlistener *lev,
    evconnlistener_cb cb, void *arg);

/** Set an evconnlistener's error callback. */
EVENT2_EXPORT_SYMBOL
void evconnlistener_set_error_cb(struct evconnlistener *lev,
    evconnlistener_errorcb errorcb);

#ifdef __cplusplus
}
#endif

#endif
