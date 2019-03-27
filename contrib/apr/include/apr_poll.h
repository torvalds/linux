/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APR_POLL_H
#define APR_POLL_H
/**
 * @file apr_poll.h
 * @brief APR Poll interface
 */
#include "apr.h"
#include "apr_pools.h"
#include "apr_errno.h"
#include "apr_inherit.h" 
#include "apr_file_io.h" 
#include "apr_network_io.h" 

#if APR_HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_poll Poll Routines
 * @ingroup APR 
 * @{
 */

/**
 * @defgroup pollopts Poll options
 * @ingroup apr_poll
 * @{
 */
#define APR_POLLIN    0x001     /**< Can read without blocking */
#define APR_POLLPRI   0x002     /**< Priority data available */
#define APR_POLLOUT   0x004     /**< Can write without blocking */
#define APR_POLLERR   0x010     /**< Pending error */
#define APR_POLLHUP   0x020     /**< Hangup occurred */
#define APR_POLLNVAL  0x040     /**< Descriptor invalid */
/** @} */

/**
 * @defgroup pollflags Pollset Flags
 * @ingroup apr_poll
 * @{
 */
#define APR_POLLSET_THREADSAFE 0x001 /**< Adding or removing a descriptor is
                                      * thread-safe
                                      */
#define APR_POLLSET_NOCOPY     0x002 /**< Descriptors passed to apr_pollset_add()
                                      * are not copied
                                      */
#define APR_POLLSET_WAKEABLE   0x004 /**< Poll operations are interruptable by
                                      * apr_pollset_wakeup()
                                      */
#define APR_POLLSET_NODEFAULT  0x010 /**< Do not try to use the default method if
                                      * the specified non-default method cannot be
                                      * used
                                      */
/** @} */

/**
 * Pollset Methods
 */
typedef enum {
    APR_POLLSET_DEFAULT,        /**< Platform default poll method */
    APR_POLLSET_SELECT,         /**< Poll uses select method */
    APR_POLLSET_KQUEUE,         /**< Poll uses kqueue method */
    APR_POLLSET_PORT,           /**< Poll uses Solaris event port method */
    APR_POLLSET_EPOLL,          /**< Poll uses epoll method */
    APR_POLLSET_POLL,           /**< Poll uses poll method */
    APR_POLLSET_AIO_MSGQ        /**< Poll uses z/OS asio method */
} apr_pollset_method_e;

/** Used in apr_pollfd_t to determine what the apr_descriptor is */
typedef enum { 
    APR_NO_DESC,                /**< nothing here */
    APR_POLL_SOCKET,            /**< descriptor refers to a socket */
    APR_POLL_FILE,              /**< descriptor refers to a file */
    APR_POLL_LASTDESC           /**< @deprecated descriptor is the last one in the list */
} apr_datatype_e ;

/** Union of either an APR file or socket. */
typedef union {
    apr_file_t *f;              /**< file */
    apr_socket_t *s;            /**< socket */
} apr_descriptor;

/** @see apr_pollfd_t */
typedef struct apr_pollfd_t apr_pollfd_t;

/** Poll descriptor set. */
struct apr_pollfd_t {
    apr_pool_t *p;              /**< associated pool */
    apr_datatype_e desc_type;   /**< descriptor type */
    apr_int16_t reqevents;      /**< requested events */
    apr_int16_t rtnevents;      /**< returned events */
    apr_descriptor desc;        /**< @see apr_descriptor */
    void *client_data;          /**< allows app to associate context */
};


/* General-purpose poll API for arbitrarily large numbers of
 * file descriptors
 */

/** Opaque structure used for pollset API */
typedef struct apr_pollset_t apr_pollset_t;

/**
 * Set up a pollset object
 * @param pollset  The pointer in which to return the newly created object 
 * @param size The maximum number of descriptors that this pollset can hold
 * @param p The pool from which to allocate the pollset
 * @param flags Optional flags to modify the operation of the pollset.
 *
 * @remark If flags contains APR_POLLSET_THREADSAFE, then a pollset is
 *         created on which it is safe to make concurrent calls to
 *         apr_pollset_add(), apr_pollset_remove() and apr_pollset_poll()
 *         from separate threads.  This feature is only supported on some
 *         platforms; the apr_pollset_create() call will fail with
 *         APR_ENOTIMPL on platforms where it is not supported.
 * @remark If flags contains APR_POLLSET_WAKEABLE, then a pollset is
 *         created with an additional internal pipe object used for the
 *         apr_pollset_wakeup() call. The actual size of pollset is
 *         in that case @a size + 1. This feature is only supported on some
 *         platforms; the apr_pollset_create() call will fail with
 *         APR_ENOTIMPL on platforms where it is not supported.
 * @remark If flags contains APR_POLLSET_NOCOPY, then the apr_pollfd_t
 *         structures passed to apr_pollset_add() are not copied and
 *         must have a lifetime at least as long as the pollset.
 * @remark Some poll methods (including APR_POLLSET_KQUEUE,
 *         APR_POLLSET_PORT, and APR_POLLSET_EPOLL) do not have a
 *         fixed limit on the size of the pollset. For these methods,
 *         the size parameter controls the maximum number of
 *         descriptors that will be returned by a single call to
 *         apr_pollset_poll().
 */
APR_DECLARE(apr_status_t) apr_pollset_create(apr_pollset_t **pollset,
                                             apr_uint32_t size,
                                             apr_pool_t *p,
                                             apr_uint32_t flags);

/**
 * Set up a pollset object
 * @param pollset  The pointer in which to return the newly created object 
 * @param size The maximum number of descriptors that this pollset can hold
 * @param p The pool from which to allocate the pollset
 * @param flags Optional flags to modify the operation of the pollset.
 * @param method Poll method to use. See #apr_pollset_method_e.  If this
 *         method cannot be used, the default method will be used unless the
 *         APR_POLLSET_NODEFAULT flag has been specified.
 *
 * @remark If flags contains APR_POLLSET_THREADSAFE, then a pollset is
 *         created on which it is safe to make concurrent calls to
 *         apr_pollset_add(), apr_pollset_remove() and apr_pollset_poll()
 *         from separate threads.  This feature is only supported on some
 *         platforms; the apr_pollset_create_ex() call will fail with
 *         APR_ENOTIMPL on platforms where it is not supported.
 * @remark If flags contains APR_POLLSET_WAKEABLE, then a pollset is
 *         created with additional internal pipe object used for the
 *         apr_pollset_wakeup() call. The actual size of pollset is
 *         in that case size + 1. This feature is only supported on some
 *         platforms; the apr_pollset_create_ex() call will fail with
 *         APR_ENOTIMPL on platforms where it is not supported.
 * @remark If flags contains APR_POLLSET_NOCOPY, then the apr_pollfd_t
 *         structures passed to apr_pollset_add() are not copied and
 *         must have a lifetime at least as long as the pollset.
 * @remark Some poll methods (including APR_POLLSET_KQUEUE,
 *         APR_POLLSET_PORT, and APR_POLLSET_EPOLL) do not have a
 *         fixed limit on the size of the pollset. For these methods,
 *         the size parameter controls the maximum number of
 *         descriptors that will be returned by a single call to
 *         apr_pollset_poll().
 */
APR_DECLARE(apr_status_t) apr_pollset_create_ex(apr_pollset_t **pollset,
                                                apr_uint32_t size,
                                                apr_pool_t *p,
                                                apr_uint32_t flags,
                                                apr_pollset_method_e method);

/**
 * Destroy a pollset object
 * @param pollset The pollset to destroy
 */
APR_DECLARE(apr_status_t) apr_pollset_destroy(apr_pollset_t *pollset);

/**
 * Add a socket or file descriptor to a pollset
 * @param pollset The pollset to which to add the descriptor
 * @param descriptor The descriptor to add
 * @remark If you set client_data in the descriptor, that value
 *         will be returned in the client_data field whenever this
 *         descriptor is signalled in apr_pollset_poll().
 * @remark If the pollset has been created with APR_POLLSET_THREADSAFE
 *         and thread T1 is blocked in a call to apr_pollset_poll() for
 *         this same pollset that is being modified via apr_pollset_add()
 *         in thread T2, the currently executing apr_pollset_poll() call in
 *         T1 will either: (1) automatically include the newly added descriptor
 *         in the set of descriptors it is watching or (2) return immediately
 *         with APR_EINTR.  Option (1) is recommended, but option (2) is
 *         allowed for implementations where option (1) is impossible
 *         or impractical.
 * @remark If the pollset has been created with APR_POLLSET_NOCOPY, the 
 *         apr_pollfd_t structure referenced by descriptor will not be copied
 *         and must have a lifetime at least as long as the pollset.
 * @remark Do not add the same socket or file descriptor to the same pollset
 *         multiple times, even if the requested events differ for the 
 *         different calls to apr_pollset_add().  If the events of interest
 *         for a descriptor change, you must first remove the descriptor 
 *         from the pollset with apr_pollset_remove(), then add it again 
 *         specifying all requested events.
 */
APR_DECLARE(apr_status_t) apr_pollset_add(apr_pollset_t *pollset,
                                          const apr_pollfd_t *descriptor);

/**
 * Remove a descriptor from a pollset
 * @param pollset The pollset from which to remove the descriptor
 * @param descriptor The descriptor to remove
 * @remark If the descriptor is not found, APR_NOTFOUND is returned.
 * @remark If the pollset has been created with APR_POLLSET_THREADSAFE
 *         and thread T1 is blocked in a call to apr_pollset_poll() for
 *         this same pollset that is being modified via apr_pollset_remove()
 *         in thread T2, the currently executing apr_pollset_poll() call in
 *         T1 will either: (1) automatically exclude the newly added descriptor
 *         in the set of descriptors it is watching or (2) return immediately
 *         with APR_EINTR.  Option (1) is recommended, but option (2) is
 *         allowed for implementations where option (1) is impossible
 *         or impractical.
 * @remark apr_pollset_remove() cannot be used to remove a subset of requested
 *         events for a descriptor.  The reqevents field in the apr_pollfd_t
 *         parameter must contain the same value when removing as when adding.
 */
APR_DECLARE(apr_status_t) apr_pollset_remove(apr_pollset_t *pollset,
                                             const apr_pollfd_t *descriptor);

/**
 * Block for activity on the descriptor(s) in a pollset
 * @param pollset The pollset to use
 * @param timeout The amount of time in microseconds to wait.  This is a
 *                maximum, not a minimum.  If a descriptor is signalled, the
 *                function will return before this time.  If timeout is
 *                negative, the function will block until a descriptor is
 *                signalled or until apr_pollset_wakeup() has been called.
 * @param num Number of signalled descriptors (output parameter)
 * @param descriptors Array of signalled descriptors (output parameter)
 * @remark APR_EINTR will be returned if the pollset has been created with
 *         APR_POLLSET_WAKEABLE, apr_pollset_wakeup() has been called while
 *         waiting for activity, and there were no signalled descriptors at the
 *         time of the wakeup call.
 * @remark Multiple signalled conditions for the same descriptor may be reported
 *         in one or more returned apr_pollfd_t structures, depending on the
 *         implementation.
 */
APR_DECLARE(apr_status_t) apr_pollset_poll(apr_pollset_t *pollset,
                                           apr_interval_time_t timeout,
                                           apr_int32_t *num,
                                           const apr_pollfd_t **descriptors);

/**
 * Interrupt the blocked apr_pollset_poll() call.
 * @param pollset The pollset to use
 * @remark If the pollset was not created with APR_POLLSET_WAKEABLE the
 *         return value is APR_EINIT.
 */
APR_DECLARE(apr_status_t) apr_pollset_wakeup(apr_pollset_t *pollset);

/**
 * Poll the descriptors in the poll structure
 * @param aprset The poll structure we will be using. 
 * @param numsock The number of descriptors we are polling
 * @param nsds The number of descriptors signalled (output parameter)
 * @param timeout The amount of time in microseconds to wait.  This is a
 *                maximum, not a minimum.  If a descriptor is signalled, the
 *                function will return before this time.  If timeout is
 *                negative, the function will block until a descriptor is
 *                signalled or until apr_pollset_wakeup() has been called.
 * @remark The number of descriptors signalled is returned in the third argument. 
 *         This is a blocking call, and it will not return until either a 
 *         descriptor has been signalled or the timeout has expired. 
 * @remark The rtnevents field in the apr_pollfd_t array will only be filled-
 *         in if the return value is APR_SUCCESS.
 */
APR_DECLARE(apr_status_t) apr_poll(apr_pollfd_t *aprset, apr_int32_t numsock,
                                   apr_int32_t *nsds, 
                                   apr_interval_time_t timeout);

/**
 * Return a printable representation of the pollset method.
 * @param pollset The pollset to use
 */
APR_DECLARE(const char *) apr_pollset_method_name(apr_pollset_t *pollset);

/**
 * Return a printable representation of the default pollset method
 * (APR_POLLSET_DEFAULT).
 */
APR_DECLARE(const char *) apr_poll_method_defname(void);

/** Opaque structure used for pollcb API */
typedef struct apr_pollcb_t apr_pollcb_t;

/**
 * Set up a pollcb object
 * @param pollcb  The pointer in which to return the newly created object 
 * @param size The maximum number of descriptors that a single _poll can return.
 * @param p The pool from which to allocate the pollcb
 * @param flags Optional flags to modify the operation of the pollcb.
 *
 * @remark Pollcb is only supported on some platforms; the apr_pollcb_create()
 * call will fail with APR_ENOTIMPL on platforms where it is not supported.
 */
APR_DECLARE(apr_status_t) apr_pollcb_create(apr_pollcb_t **pollcb,
                                            apr_uint32_t size,
                                            apr_pool_t *p,
                                            apr_uint32_t flags);

/**
 * Set up a pollcb object
 * @param pollcb  The pointer in which to return the newly created object 
 * @param size The maximum number of descriptors that a single _poll can return.
 * @param p The pool from which to allocate the pollcb
 * @param flags Optional flags to modify the operation of the pollcb.
 * @param method Poll method to use. See #apr_pollset_method_e.  If this
 *         method cannot be used, the default method will be used unless the
 *         APR_POLLSET_NODEFAULT flag has been specified.
 *
 * @remark Pollcb is only supported on some platforms; the apr_pollcb_create_ex()
 * call will fail with APR_ENOTIMPL on platforms where it is not supported.
 */
APR_DECLARE(apr_status_t) apr_pollcb_create_ex(apr_pollcb_t **pollcb,
                                               apr_uint32_t size,
                                               apr_pool_t *p,
                                               apr_uint32_t flags,
                                               apr_pollset_method_e method);

/**
 * Add a socket or file descriptor to a pollcb
 * @param pollcb The pollcb to which to add the descriptor
 * @param descriptor The descriptor to add
 * @remark If you set client_data in the descriptor, that value will be
 *         returned in the client_data field whenever this descriptor is
 *         signalled in apr_pollcb_poll().
 * @remark Unlike the apr_pollset API, the descriptor is not copied, and users 
 *         must retain the memory used by descriptor, as the same pointer will
 *         be returned to them from apr_pollcb_poll.
 * @remark Do not add the same socket or file descriptor to the same pollcb
 *         multiple times, even if the requested events differ for the 
 *         different calls to apr_pollcb_add().  If the events of interest
 *         for a descriptor change, you must first remove the descriptor 
 *         from the pollcb with apr_pollcb_remove(), then add it again 
 *         specifying all requested events.
 */
APR_DECLARE(apr_status_t) apr_pollcb_add(apr_pollcb_t *pollcb,
                                         apr_pollfd_t *descriptor);
/**
 * Remove a descriptor from a pollcb
 * @param pollcb The pollcb from which to remove the descriptor
 * @param descriptor The descriptor to remove
 * @remark apr_pollcb_remove() cannot be used to remove a subset of requested
 *         events for a descriptor.  The reqevents field in the apr_pollfd_t
 *         parameter must contain the same value when removing as when adding.
 */
APR_DECLARE(apr_status_t) apr_pollcb_remove(apr_pollcb_t *pollcb,
                                            apr_pollfd_t *descriptor);

/** Function prototype for pollcb handlers 
 * @param baton Opaque baton passed into apr_pollcb_poll()
 * @param descriptor Contains the notification for an active descriptor, 
 *                   the rtnevents member contains what events were triggered
 *                   for this descriptor.
 */
typedef apr_status_t (*apr_pollcb_cb_t)(void *baton, apr_pollfd_t *descriptor);

/**
 * Block for activity on the descriptor(s) in a pollcb
 * @param pollcb The pollcb to use
 * @param timeout The amount of time in microseconds to wait.  This is a
 *                maximum, not a minimum.  If a descriptor is signalled, the
 *                function will return before this time.  If timeout is
 *                negative, the function will block until a descriptor is
 *                signalled.
 * @param func Callback function to call for each active descriptor.
 * @param baton Opaque baton passed to the callback function.
 * @remark Multiple signalled conditions for the same descriptor may be reported
 *         in one or more calls to the callback function, depending on the
 *         implementation.
 */
APR_DECLARE(apr_status_t) apr_pollcb_poll(apr_pollcb_t *pollcb,
                                          apr_interval_time_t timeout,
                                          apr_pollcb_cb_t func,
                                          void *baton); 

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* ! APR_POLL_H */

