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

#ifndef APR_ERRNO_H
#define APR_ERRNO_H

/**
 * @file apr_errno.h
 * @brief APR Error Codes
 */

#include "apr.h"

#if APR_HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_errno Error Codes
 * @ingroup APR
 * @{
 */

/**
 * Type for specifying an error or status code.
 */
typedef int apr_status_t;

/**
 * Return a human readable string describing the specified error.
 * @param statcode The error code to get a string for.
 * @param buf A buffer to hold the error string.
 * @param bufsize Size of the buffer to hold the string.
 */
APR_DECLARE(char *) apr_strerror(apr_status_t statcode, char *buf,
                                 apr_size_t bufsize);

#if defined(DOXYGEN)
/**
 * @def APR_FROM_OS_ERROR(os_err_type syserr)
 * Fold a platform specific error into an apr_status_t code.
 * @return apr_status_t
 * @param e The platform os error code.
 * @warning  macro implementation; the syserr argument may be evaluated
 *      multiple times.
 */
#define APR_FROM_OS_ERROR(e) (e == 0 ? APR_SUCCESS : e + APR_OS_START_SYSERR)

/**
 * @def APR_TO_OS_ERROR(apr_status_t statcode)
 * @return os_err_type
 * Fold an apr_status_t code back to the native platform defined error.
 * @param e The apr_status_t folded platform os error code.
 * @warning  macro implementation; the statcode argument may be evaluated
 *      multiple times.  If the statcode was not created by apr_get_os_error
 *      or APR_FROM_OS_ERROR, the results are undefined.
 */
#define APR_TO_OS_ERROR(e) (e == 0 ? APR_SUCCESS : e - APR_OS_START_SYSERR)

/** @def apr_get_os_error()
 * @return apr_status_t the last platform error, folded into apr_status_t, on most platforms
 * @remark This retrieves errno, or calls a GetLastError() style function, and
 *      folds it with APR_FROM_OS_ERROR.  Some platforms (such as OS2) have no
 *      such mechanism, so this call may be unsupported.  Do NOT use this
 *      call for socket errors from socket, send, recv etc!
 */

/** @def apr_set_os_error(e)
 * Reset the last platform error, unfolded from an apr_status_t, on some platforms
 * @param e The OS error folded in a prior call to APR_FROM_OS_ERROR()
 * @warning This is a macro implementation; the statcode argument may be evaluated
 *      multiple times.  If the statcode was not created by apr_get_os_error
 *      or APR_FROM_OS_ERROR, the results are undefined.  This macro sets
 *      errno, or calls a SetLastError() style function, unfolding statcode
 *      with APR_TO_OS_ERROR.  Some platforms (such as OS2) have no such
 *      mechanism, so this call may be unsupported.
 */

/** @def apr_get_netos_error()
 * Return the last socket error, folded into apr_status_t, on all platforms
 * @remark This retrieves errno or calls a GetLastSocketError() style function,
 *      and folds it with APR_FROM_OS_ERROR.
 */

/** @def apr_set_netos_error(e)
 * Reset the last socket error, unfolded from an apr_status_t
 * @param e The socket error folded in a prior call to APR_FROM_OS_ERROR()
 * @warning This is a macro implementation; the statcode argument may be evaluated
 *      multiple times.  If the statcode was not created by apr_get_os_error
 *      or APR_FROM_OS_ERROR, the results are undefined.  This macro sets
 *      errno, or calls a WSASetLastError() style function, unfolding
 *      socketcode with APR_TO_OS_ERROR.
 */

#endif /* defined(DOXYGEN) */

/**
 * APR_OS_START_ERROR is where the APR specific error values start.
 */
#define APR_OS_START_ERROR     20000
/**
 * APR_OS_ERRSPACE_SIZE is the maximum number of errors you can fit
 *    into one of the error/status ranges below -- except for
 *    APR_OS_START_USERERR, which see.
 */
#define APR_OS_ERRSPACE_SIZE 50000
/**
 * APR_UTIL_ERRSPACE_SIZE is the size of the space that is reserved for
 * use within apr-util. This space is reserved above that used by APR
 * internally.
 * @note This number MUST be smaller than APR_OS_ERRSPACE_SIZE by a
 *       large enough amount that APR has sufficient room for its
 *       codes.
 */
#define APR_UTIL_ERRSPACE_SIZE 20000
/**
 * APR_OS_START_STATUS is where the APR specific status codes start.
 */
#define APR_OS_START_STATUS    (APR_OS_START_ERROR + APR_OS_ERRSPACE_SIZE)
/**
 * APR_UTIL_START_STATUS is where APR-Util starts defining its
 * status codes.
 */
#define APR_UTIL_START_STATUS   (APR_OS_START_STATUS + \
                           (APR_OS_ERRSPACE_SIZE - APR_UTIL_ERRSPACE_SIZE))
/**
 * APR_OS_START_USERERR are reserved for applications that use APR that
 *     layer their own error codes along with APR's.  Note that the
 *     error immediately following this one is set ten times farther
 *     away than usual, so that users of apr have a lot of room in
 *     which to declare custom error codes.
 *
 * In general applications should try and create unique error codes. To try
 * and assist in finding suitable ranges of numbers to use, the following
 * ranges are known to be used by the listed applications. If your
 * application defines error codes please advise the range of numbers it
 * uses to dev@apr.apache.org for inclusion in this list.
 *
 * Ranges shown are in relation to APR_OS_START_USERERR
 *
 * Subversion - Defined ranges, of less than 100, at intervals of 5000
 *              starting at an offset of 5000, e.g.
 *               +5000 to 5100,  +10000 to 10100
 *
 * Apache HTTPD - +2000 to 2999
 */
#define APR_OS_START_USERERR    (APR_OS_START_STATUS + APR_OS_ERRSPACE_SIZE)
/**
 * APR_OS_START_USEERR is obsolete, defined for compatibility only.
 * Use APR_OS_START_USERERR instead.
 */
#define APR_OS_START_USEERR     APR_OS_START_USERERR
/**
 * APR_OS_START_CANONERR is where APR versions of errno values are defined
 *     on systems which don't have the corresponding errno.
 */
#define APR_OS_START_CANONERR  (APR_OS_START_USERERR \
                                 + (APR_OS_ERRSPACE_SIZE * 10))
/**
 * APR_OS_START_EAIERR folds EAI_ error codes from getaddrinfo() into
 *     apr_status_t values.
 */
#define APR_OS_START_EAIERR    (APR_OS_START_CANONERR + APR_OS_ERRSPACE_SIZE)
/**
 * APR_OS_START_SYSERR folds platform-specific system error values into
 *     apr_status_t values.
 */
#define APR_OS_START_SYSERR    (APR_OS_START_EAIERR + APR_OS_ERRSPACE_SIZE)

/**
 * @defgroup APR_ERROR_map APR Error Space
 * <PRE>
 * The following attempts to show the relation of the various constants
 * used for mapping APR Status codes.
 *
 *       0
 *
 *  20,000     APR_OS_START_ERROR
 *
 *         + APR_OS_ERRSPACE_SIZE (50,000)
 *
 *  70,000      APR_OS_START_STATUS
 *
 *         + APR_OS_ERRSPACE_SIZE - APR_UTIL_ERRSPACE_SIZE (30,000)
 *
 * 100,000      APR_UTIL_START_STATUS
 *
 *         + APR_UTIL_ERRSPACE_SIZE (20,000)
 *
 * 120,000      APR_OS_START_USERERR
 *
 *         + 10 x APR_OS_ERRSPACE_SIZE (50,000 * 10)
 *
 * 620,000      APR_OS_START_CANONERR
 *
 *         + APR_OS_ERRSPACE_SIZE (50,000)
 *
 * 670,000      APR_OS_START_EAIERR
 *
 *         + APR_OS_ERRSPACE_SIZE (50,000)
 *
 * 720,000      APR_OS_START_SYSERR
 *
 * </PRE>
 */

/** no error. */
#define APR_SUCCESS 0

/**
 * @defgroup APR_Error APR Error Values
 * <PRE>
 * <b>APR ERROR VALUES</b>
 * APR_ENOSTAT      APR was unable to perform a stat on the file
 * APR_ENOPOOL      APR was not provided a pool with which to allocate memory
 * APR_EBADDATE     APR was given an invalid date
 * APR_EINVALSOCK   APR was given an invalid socket
 * APR_ENOPROC      APR was not given a process structure
 * APR_ENOTIME      APR was not given a time structure
 * APR_ENODIR       APR was not given a directory structure
 * APR_ENOLOCK      APR was not given a lock structure
 * APR_ENOPOLL      APR was not given a poll structure
 * APR_ENOSOCKET    APR was not given a socket
 * APR_ENOTHREAD    APR was not given a thread structure
 * APR_ENOTHDKEY    APR was not given a thread key structure
 * APR_ENOSHMAVAIL  There is no more shared memory available
 * APR_EDSOOPEN     APR was unable to open the dso object.  For more
 *                  information call apr_dso_error().
 * APR_EGENERAL     General failure (specific information not available)
 * APR_EBADIP       The specified IP address is invalid
 * APR_EBADMASK     The specified netmask is invalid
 * APR_ESYMNOTFOUND Could not find the requested symbol
 * APR_ENOTENOUGHENTROPY Not enough entropy to continue
 * </PRE>
 *
 * <PRE>
 * <b>APR STATUS VALUES</b>
 * APR_INCHILD        Program is currently executing in the child
 * APR_INPARENT       Program is currently executing in the parent
 * APR_DETACH         The thread is detached
 * APR_NOTDETACH      The thread is not detached
 * APR_CHILD_DONE     The child has finished executing
 * APR_CHILD_NOTDONE  The child has not finished executing
 * APR_TIMEUP         The operation did not finish before the timeout
 * APR_INCOMPLETE     The operation was incomplete although some processing
 *                    was performed and the results are partially valid
 * APR_BADCH          Getopt found an option not in the option string
 * APR_BADARG         Getopt found an option that is missing an argument
 *                    and an argument was specified in the option string
 * APR_EOF            APR has encountered the end of the file
 * APR_NOTFOUND       APR was unable to find the socket in the poll structure
 * APR_ANONYMOUS      APR is using anonymous shared memory
 * APR_FILEBASED      APR is using a file name as the key to the shared memory
 * APR_KEYBASED       APR is using a shared key as the key to the shared memory
 * APR_EINIT          Ininitalizer value.  If no option has been found, but
 *                    the status variable requires a value, this should be used
 * APR_ENOTIMPL       The APR function has not been implemented on this
 *                    platform, either because nobody has gotten to it yet,
 *                    or the function is impossible on this platform.
 * APR_EMISMATCH      Two passwords do not match.
 * APR_EABSOLUTE      The given path was absolute.
 * APR_ERELATIVE      The given path was relative.
 * APR_EINCOMPLETE    The given path was neither relative nor absolute.
 * APR_EABOVEROOT     The given path was above the root path.
 * APR_EBUSY          The given lock was busy.
 * APR_EPROC_UNKNOWN  The given process wasn't recognized by APR
 * </PRE>
 * @{
 */
/** @see APR_STATUS_IS_ENOSTAT */
#define APR_ENOSTAT        (APR_OS_START_ERROR + 1)
/** @see APR_STATUS_IS_ENOPOOL */
#define APR_ENOPOOL        (APR_OS_START_ERROR + 2)
/* empty slot: +3 */
/** @see APR_STATUS_IS_EBADDATE */
#define APR_EBADDATE       (APR_OS_START_ERROR + 4)
/** @see APR_STATUS_IS_EINVALSOCK */
#define APR_EINVALSOCK     (APR_OS_START_ERROR + 5)
/** @see APR_STATUS_IS_ENOPROC */
#define APR_ENOPROC        (APR_OS_START_ERROR + 6)
/** @see APR_STATUS_IS_ENOTIME */
#define APR_ENOTIME        (APR_OS_START_ERROR + 7)
/** @see APR_STATUS_IS_ENODIR */
#define APR_ENODIR         (APR_OS_START_ERROR + 8)
/** @see APR_STATUS_IS_ENOLOCK */
#define APR_ENOLOCK        (APR_OS_START_ERROR + 9)
/** @see APR_STATUS_IS_ENOPOLL */
#define APR_ENOPOLL        (APR_OS_START_ERROR + 10)
/** @see APR_STATUS_IS_ENOSOCKET */
#define APR_ENOSOCKET      (APR_OS_START_ERROR + 11)
/** @see APR_STATUS_IS_ENOTHREAD */
#define APR_ENOTHREAD      (APR_OS_START_ERROR + 12)
/** @see APR_STATUS_IS_ENOTHDKEY */
#define APR_ENOTHDKEY      (APR_OS_START_ERROR + 13)
/** @see APR_STATUS_IS_EGENERAL */
#define APR_EGENERAL       (APR_OS_START_ERROR + 14)
/** @see APR_STATUS_IS_ENOSHMAVAIL */
#define APR_ENOSHMAVAIL    (APR_OS_START_ERROR + 15)
/** @see APR_STATUS_IS_EBADIP */
#define APR_EBADIP         (APR_OS_START_ERROR + 16)
/** @see APR_STATUS_IS_EBADMASK */
#define APR_EBADMASK       (APR_OS_START_ERROR + 17)
/* empty slot: +18 */
/** @see APR_STATUS_IS_EDSOPEN */
#define APR_EDSOOPEN       (APR_OS_START_ERROR + 19)
/** @see APR_STATUS_IS_EABSOLUTE */
#define APR_EABSOLUTE      (APR_OS_START_ERROR + 20)
/** @see APR_STATUS_IS_ERELATIVE */
#define APR_ERELATIVE      (APR_OS_START_ERROR + 21)
/** @see APR_STATUS_IS_EINCOMPLETE */
#define APR_EINCOMPLETE    (APR_OS_START_ERROR + 22)
/** @see APR_STATUS_IS_EABOVEROOT */
#define APR_EABOVEROOT     (APR_OS_START_ERROR + 23)
/** @see APR_STATUS_IS_EBADPATH */
#define APR_EBADPATH       (APR_OS_START_ERROR + 24)
/** @see APR_STATUS_IS_EPATHWILD */
#define APR_EPATHWILD      (APR_OS_START_ERROR + 25)
/** @see APR_STATUS_IS_ESYMNOTFOUND */
#define APR_ESYMNOTFOUND   (APR_OS_START_ERROR + 26)
/** @see APR_STATUS_IS_EPROC_UNKNOWN */
#define APR_EPROC_UNKNOWN  (APR_OS_START_ERROR + 27)
/** @see APR_STATUS_IS_ENOTENOUGHENTROPY */
#define APR_ENOTENOUGHENTROPY (APR_OS_START_ERROR + 28)
/** @} */

/**
 * @defgroup APR_STATUS_IS Status Value Tests
 * @warning For any particular error condition, more than one of these tests
 *      may match. This is because platform-specific error codes may not
 *      always match the semantics of the POSIX codes these tests (and the
 *      corresponding APR error codes) are named after. A notable example
 *      are the APR_STATUS_IS_ENOENT and APR_STATUS_IS_ENOTDIR tests on
 *      Win32 platforms. The programmer should always be aware of this and
 *      adjust the order of the tests accordingly.
 * @{
 */
/**
 * APR was unable to perform a stat on the file
 * @warning always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_ENOSTAT(s)        ((s) == APR_ENOSTAT)
/**
 * APR was not provided a pool with which to allocate memory
 * @warning always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_ENOPOOL(s)        ((s) == APR_ENOPOOL)
/** APR was given an invalid date  */
#define APR_STATUS_IS_EBADDATE(s)       ((s) == APR_EBADDATE)
/** APR was given an invalid socket */
#define APR_STATUS_IS_EINVALSOCK(s)     ((s) == APR_EINVALSOCK)
/** APR was not given a process structure */
#define APR_STATUS_IS_ENOPROC(s)        ((s) == APR_ENOPROC)
/** APR was not given a time structure */
#define APR_STATUS_IS_ENOTIME(s)        ((s) == APR_ENOTIME)
/** APR was not given a directory structure */
#define APR_STATUS_IS_ENODIR(s)         ((s) == APR_ENODIR)
/** APR was not given a lock structure */
#define APR_STATUS_IS_ENOLOCK(s)        ((s) == APR_ENOLOCK)
/** APR was not given a poll structure */
#define APR_STATUS_IS_ENOPOLL(s)        ((s) == APR_ENOPOLL)
/** APR was not given a socket */
#define APR_STATUS_IS_ENOSOCKET(s)      ((s) == APR_ENOSOCKET)
/** APR was not given a thread structure */
#define APR_STATUS_IS_ENOTHREAD(s)      ((s) == APR_ENOTHREAD)
/** APR was not given a thread key structure */
#define APR_STATUS_IS_ENOTHDKEY(s)      ((s) == APR_ENOTHDKEY)
/** Generic Error which can not be put into another spot */
#define APR_STATUS_IS_EGENERAL(s)       ((s) == APR_EGENERAL)
/** There is no more shared memory available */
#define APR_STATUS_IS_ENOSHMAVAIL(s)    ((s) == APR_ENOSHMAVAIL)
/** The specified IP address is invalid */
#define APR_STATUS_IS_EBADIP(s)         ((s) == APR_EBADIP)
/** The specified netmask is invalid */
#define APR_STATUS_IS_EBADMASK(s)       ((s) == APR_EBADMASK)
/* empty slot: +18 */
/**
 * APR was unable to open the dso object.
 * For more information call apr_dso_error().
 */
#if defined(WIN32)
#define APR_STATUS_IS_EDSOOPEN(s)       ((s) == APR_EDSOOPEN \
                       || APR_TO_OS_ERROR(s) == ERROR_MOD_NOT_FOUND)
#else
#define APR_STATUS_IS_EDSOOPEN(s)       ((s) == APR_EDSOOPEN)
#endif
/** The given path was absolute. */
#define APR_STATUS_IS_EABSOLUTE(s)      ((s) == APR_EABSOLUTE)
/** The given path was relative. */
#define APR_STATUS_IS_ERELATIVE(s)      ((s) == APR_ERELATIVE)
/** The given path was neither relative nor absolute. */
#define APR_STATUS_IS_EINCOMPLETE(s)    ((s) == APR_EINCOMPLETE)
/** The given path was above the root path. */
#define APR_STATUS_IS_EABOVEROOT(s)     ((s) == APR_EABOVEROOT)
/** The given path was bad. */
#define APR_STATUS_IS_EBADPATH(s)       ((s) == APR_EBADPATH)
/** The given path contained wildcards. */
#define APR_STATUS_IS_EPATHWILD(s)      ((s) == APR_EPATHWILD)
/** Could not find the requested symbol.
 * For more information call apr_dso_error().
 */
#if defined(WIN32)
#define APR_STATUS_IS_ESYMNOTFOUND(s)   ((s) == APR_ESYMNOTFOUND \
                       || APR_TO_OS_ERROR(s) == ERROR_PROC_NOT_FOUND)
#else
#define APR_STATUS_IS_ESYMNOTFOUND(s)   ((s) == APR_ESYMNOTFOUND)
#endif
/** The given process was not recognized by APR. */
#define APR_STATUS_IS_EPROC_UNKNOWN(s)  ((s) == APR_EPROC_UNKNOWN)
/** APR could not gather enough entropy to continue. */
#define APR_STATUS_IS_ENOTENOUGHENTROPY(s) ((s) == APR_ENOTENOUGHENTROPY)

/** @} */

/**
 * @addtogroup APR_Error
 * @{
 */
/** @see APR_STATUS_IS_INCHILD */
#define APR_INCHILD        (APR_OS_START_STATUS + 1)
/** @see APR_STATUS_IS_INPARENT */
#define APR_INPARENT       (APR_OS_START_STATUS + 2)
/** @see APR_STATUS_IS_DETACH */
#define APR_DETACH         (APR_OS_START_STATUS + 3)
/** @see APR_STATUS_IS_NOTDETACH */
#define APR_NOTDETACH      (APR_OS_START_STATUS + 4)
/** @see APR_STATUS_IS_CHILD_DONE */
#define APR_CHILD_DONE     (APR_OS_START_STATUS + 5)
/** @see APR_STATUS_IS_CHILD_NOTDONE */
#define APR_CHILD_NOTDONE  (APR_OS_START_STATUS + 6)
/** @see APR_STATUS_IS_TIMEUP */
#define APR_TIMEUP         (APR_OS_START_STATUS + 7)
/** @see APR_STATUS_IS_INCOMPLETE */
#define APR_INCOMPLETE     (APR_OS_START_STATUS + 8)
/* empty slot: +9 */
/* empty slot: +10 */
/* empty slot: +11 */
/** @see APR_STATUS_IS_BADCH */
#define APR_BADCH          (APR_OS_START_STATUS + 12)
/** @see APR_STATUS_IS_BADARG */
#define APR_BADARG         (APR_OS_START_STATUS + 13)
/** @see APR_STATUS_IS_EOF */
#define APR_EOF            (APR_OS_START_STATUS + 14)
/** @see APR_STATUS_IS_NOTFOUND */
#define APR_NOTFOUND       (APR_OS_START_STATUS + 15)
/* empty slot: +16 */
/* empty slot: +17 */
/* empty slot: +18 */
/** @see APR_STATUS_IS_ANONYMOUS */
#define APR_ANONYMOUS      (APR_OS_START_STATUS + 19)
/** @see APR_STATUS_IS_FILEBASED */
#define APR_FILEBASED      (APR_OS_START_STATUS + 20)
/** @see APR_STATUS_IS_KEYBASED */
#define APR_KEYBASED       (APR_OS_START_STATUS + 21)
/** @see APR_STATUS_IS_EINIT */
#define APR_EINIT          (APR_OS_START_STATUS + 22)
/** @see APR_STATUS_IS_ENOTIMPL */
#define APR_ENOTIMPL       (APR_OS_START_STATUS + 23)
/** @see APR_STATUS_IS_EMISMATCH */
#define APR_EMISMATCH      (APR_OS_START_STATUS + 24)
/** @see APR_STATUS_IS_EBUSY */
#define APR_EBUSY          (APR_OS_START_STATUS + 25)
/** @} */

/**
 * @addtogroup APR_STATUS_IS
 * @{
 */
/**
 * Program is currently executing in the child
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code */
#define APR_STATUS_IS_INCHILD(s)        ((s) == APR_INCHILD)
/**
 * Program is currently executing in the parent
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_INPARENT(s)       ((s) == APR_INPARENT)
/**
 * The thread is detached
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_DETACH(s)         ((s) == APR_DETACH)
/**
 * The thread is not detached
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_NOTDETACH(s)      ((s) == APR_NOTDETACH)
/**
 * The child has finished executing
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_CHILD_DONE(s)     ((s) == APR_CHILD_DONE)
/**
 * The child has not finished executing
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_CHILD_NOTDONE(s)  ((s) == APR_CHILD_NOTDONE)
/**
 * The operation did not finish before the timeout
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_TIMEUP(s)         ((s) == APR_TIMEUP)
/**
 * The operation was incomplete although some processing was performed
 * and the results are partially valid.
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_INCOMPLETE(s)     ((s) == APR_INCOMPLETE)
/* empty slot: +9 */
/* empty slot: +10 */
/* empty slot: +11 */
/**
 * Getopt found an option not in the option string
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_BADCH(s)          ((s) == APR_BADCH)
/**
 * Getopt found an option not in the option string and an argument was
 * specified in the option string
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_BADARG(s)         ((s) == APR_BADARG)
/**
 * APR has encountered the end of the file
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_EOF(s)            ((s) == APR_EOF)
/**
 * APR was unable to find the socket in the poll structure
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_NOTFOUND(s)       ((s) == APR_NOTFOUND)
/* empty slot: +16 */
/* empty slot: +17 */
/* empty slot: +18 */
/**
 * APR is using anonymous shared memory
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_ANONYMOUS(s)      ((s) == APR_ANONYMOUS)
/**
 * APR is using a file name as the key to the shared memory
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_FILEBASED(s)      ((s) == APR_FILEBASED)
/**
 * APR is using a shared key as the key to the shared memory
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_KEYBASED(s)       ((s) == APR_KEYBASED)
/**
 * Ininitalizer value.  If no option has been found, but
 * the status variable requires a value, this should be used
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_EINIT(s)          ((s) == APR_EINIT)
/**
 * The APR function has not been implemented on this
 * platform, either because nobody has gotten to it yet,
 * or the function is impossible on this platform.
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_ENOTIMPL(s)       ((s) == APR_ENOTIMPL)
/**
 * Two passwords do not match.
 * @warning
 * always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_EMISMATCH(s)      ((s) == APR_EMISMATCH)
/**
 * The given lock was busy
 * @warning always use this test, as platform-specific variances may meet this
 * more than one error code
 */
#define APR_STATUS_IS_EBUSY(s)          ((s) == APR_EBUSY)

/** @} */

/**
 * @addtogroup APR_Error APR Error Values
 * @{
 */
/* APR CANONICAL ERROR VALUES */
/** @see APR_STATUS_IS_EACCES */
#ifdef EACCES
#define APR_EACCES EACCES
#else
#define APR_EACCES         (APR_OS_START_CANONERR + 1)
#endif

/** @see APR_STATUS_IS_EEXIST */
#ifdef EEXIST
#define APR_EEXIST EEXIST
#else
#define APR_EEXIST         (APR_OS_START_CANONERR + 2)
#endif

/** @see APR_STATUS_IS_ENAMETOOLONG */
#ifdef ENAMETOOLONG
#define APR_ENAMETOOLONG ENAMETOOLONG
#else
#define APR_ENAMETOOLONG   (APR_OS_START_CANONERR + 3)
#endif

/** @see APR_STATUS_IS_ENOENT */
#ifdef ENOENT
#define APR_ENOENT ENOENT
#else
#define APR_ENOENT         (APR_OS_START_CANONERR + 4)
#endif

/** @see APR_STATUS_IS_ENOTDIR */
#ifdef ENOTDIR
#define APR_ENOTDIR ENOTDIR
#else
#define APR_ENOTDIR        (APR_OS_START_CANONERR + 5)
#endif

/** @see APR_STATUS_IS_ENOSPC */
#ifdef ENOSPC
#define APR_ENOSPC ENOSPC
#else
#define APR_ENOSPC         (APR_OS_START_CANONERR + 6)
#endif

/** @see APR_STATUS_IS_ENOMEM */
#ifdef ENOMEM
#define APR_ENOMEM ENOMEM
#else
#define APR_ENOMEM         (APR_OS_START_CANONERR + 7)
#endif

/** @see APR_STATUS_IS_EMFILE */
#ifdef EMFILE
#define APR_EMFILE EMFILE
#else
#define APR_EMFILE         (APR_OS_START_CANONERR + 8)
#endif

/** @see APR_STATUS_IS_ENFILE */
#ifdef ENFILE
#define APR_ENFILE ENFILE
#else
#define APR_ENFILE         (APR_OS_START_CANONERR + 9)
#endif

/** @see APR_STATUS_IS_EBADF */
#ifdef EBADF
#define APR_EBADF EBADF
#else
#define APR_EBADF          (APR_OS_START_CANONERR + 10)
#endif

/** @see APR_STATUS_IS_EINVAL */
#ifdef EINVAL
#define APR_EINVAL EINVAL
#else
#define APR_EINVAL         (APR_OS_START_CANONERR + 11)
#endif

/** @see APR_STATUS_IS_ESPIPE */
#ifdef ESPIPE
#define APR_ESPIPE ESPIPE
#else
#define APR_ESPIPE         (APR_OS_START_CANONERR + 12)
#endif

/**
 * @see APR_STATUS_IS_EAGAIN
 * @warning use APR_STATUS_IS_EAGAIN instead of just testing this value
 */
#ifdef EAGAIN
#define APR_EAGAIN EAGAIN
#elif defined(EWOULDBLOCK)
#define APR_EAGAIN EWOULDBLOCK
#else
#define APR_EAGAIN         (APR_OS_START_CANONERR + 13)
#endif

/** @see APR_STATUS_IS_EINTR */
#ifdef EINTR
#define APR_EINTR EINTR
#else
#define APR_EINTR          (APR_OS_START_CANONERR + 14)
#endif

/** @see APR_STATUS_IS_ENOTSOCK */
#ifdef ENOTSOCK
#define APR_ENOTSOCK ENOTSOCK
#else
#define APR_ENOTSOCK       (APR_OS_START_CANONERR + 15)
#endif

/** @see APR_STATUS_IS_ECONNREFUSED */
#ifdef ECONNREFUSED
#define APR_ECONNREFUSED ECONNREFUSED
#else
#define APR_ECONNREFUSED   (APR_OS_START_CANONERR + 16)
#endif

/** @see APR_STATUS_IS_EINPROGRESS */
#ifdef EINPROGRESS
#define APR_EINPROGRESS EINPROGRESS
#else
#define APR_EINPROGRESS    (APR_OS_START_CANONERR + 17)
#endif

/**
 * @see APR_STATUS_IS_ECONNABORTED
 * @warning use APR_STATUS_IS_ECONNABORTED instead of just testing this value
 */

#ifdef ECONNABORTED
#define APR_ECONNABORTED ECONNABORTED
#else
#define APR_ECONNABORTED   (APR_OS_START_CANONERR + 18)
#endif

/** @see APR_STATUS_IS_ECONNRESET */
#ifdef ECONNRESET
#define APR_ECONNRESET ECONNRESET
#else
#define APR_ECONNRESET     (APR_OS_START_CANONERR + 19)
#endif

/** @see APR_STATUS_IS_ETIMEDOUT
 *  @deprecated */
#ifdef ETIMEDOUT
#define APR_ETIMEDOUT ETIMEDOUT
#else
#define APR_ETIMEDOUT      (APR_OS_START_CANONERR + 20)
#endif

/** @see APR_STATUS_IS_EHOSTUNREACH */
#ifdef EHOSTUNREACH
#define APR_EHOSTUNREACH EHOSTUNREACH
#else
#define APR_EHOSTUNREACH   (APR_OS_START_CANONERR + 21)
#endif

/** @see APR_STATUS_IS_ENETUNREACH */
#ifdef ENETUNREACH
#define APR_ENETUNREACH ENETUNREACH
#else
#define APR_ENETUNREACH    (APR_OS_START_CANONERR + 22)
#endif

/** @see APR_STATUS_IS_EFTYPE */
#ifdef EFTYPE
#define APR_EFTYPE EFTYPE
#else
#define APR_EFTYPE        (APR_OS_START_CANONERR + 23)
#endif

/** @see APR_STATUS_IS_EPIPE */
#ifdef EPIPE
#define APR_EPIPE EPIPE
#else
#define APR_EPIPE         (APR_OS_START_CANONERR + 24)
#endif

/** @see APR_STATUS_IS_EXDEV */
#ifdef EXDEV
#define APR_EXDEV EXDEV
#else
#define APR_EXDEV         (APR_OS_START_CANONERR + 25)
#endif

/** @see APR_STATUS_IS_ENOTEMPTY */
#ifdef ENOTEMPTY
#define APR_ENOTEMPTY ENOTEMPTY
#else
#define APR_ENOTEMPTY     (APR_OS_START_CANONERR + 26)
#endif

/** @see APR_STATUS_IS_EAFNOSUPPORT */
#ifdef EAFNOSUPPORT
#define APR_EAFNOSUPPORT EAFNOSUPPORT
#else
#define APR_EAFNOSUPPORT  (APR_OS_START_CANONERR + 27)
#endif

/** @} */

#if defined(OS2) && !defined(DOXYGEN)

#define APR_FROM_OS_ERROR(e) (e == 0 ? APR_SUCCESS : e + APR_OS_START_SYSERR)
#define APR_TO_OS_ERROR(e)   (e == 0 ? APR_SUCCESS : e - APR_OS_START_SYSERR)

#define INCL_DOSERRORS
#define INCL_DOS

/* Leave these undefined.
 * OS2 doesn't rely on the errno concept.
 * The API calls always return a result codes which
 * should be filtered through APR_FROM_OS_ERROR().
 *
 * #define apr_get_os_error()   (APR_FROM_OS_ERROR(GetLastError()))
 * #define apr_set_os_error(e)  (SetLastError(APR_TO_OS_ERROR(e)))
 */

/* A special case, only socket calls require this;
 */
#define apr_get_netos_error()   (APR_FROM_OS_ERROR(errno))
#define apr_set_netos_error(e)  (errno = APR_TO_OS_ERROR(e))

/* And this needs to be greped away for good:
 */
#define APR_OS2_STATUS(e) (APR_FROM_OS_ERROR(e))

/* These can't sit in a private header, so in spite of the extra size,
 * they need to be made available here.
 */
#define SOCBASEERR              10000
#define SOCEPERM                (SOCBASEERR+1)             /* Not owner */
#define SOCESRCH                (SOCBASEERR+3)             /* No such process */
#define SOCEINTR                (SOCBASEERR+4)             /* Interrupted system call */
#define SOCENXIO                (SOCBASEERR+6)             /* No such device or address */
#define SOCEBADF                (SOCBASEERR+9)             /* Bad file number */
#define SOCEACCES               (SOCBASEERR+13)            /* Permission denied */
#define SOCEFAULT               (SOCBASEERR+14)            /* Bad address */
#define SOCEINVAL               (SOCBASEERR+22)            /* Invalid argument */
#define SOCEMFILE               (SOCBASEERR+24)            /* Too many open files */
#define SOCEPIPE                (SOCBASEERR+32)            /* Broken pipe */
#define SOCEOS2ERR              (SOCBASEERR+100)           /* OS/2 Error */
#define SOCEWOULDBLOCK          (SOCBASEERR+35)            /* Operation would block */
#define SOCEINPROGRESS          (SOCBASEERR+36)            /* Operation now in progress */
#define SOCEALREADY             (SOCBASEERR+37)            /* Operation already in progress */
#define SOCENOTSOCK             (SOCBASEERR+38)            /* Socket operation on non-socket */
#define SOCEDESTADDRREQ         (SOCBASEERR+39)            /* Destination address required */
#define SOCEMSGSIZE             (SOCBASEERR+40)            /* Message too long */
#define SOCEPROTOTYPE           (SOCBASEERR+41)            /* Protocol wrong type for socket */
#define SOCENOPROTOOPT          (SOCBASEERR+42)            /* Protocol not available */
#define SOCEPROTONOSUPPORT      (SOCBASEERR+43)            /* Protocol not supported */
#define SOCESOCKTNOSUPPORT      (SOCBASEERR+44)            /* Socket type not supported */
#define SOCEOPNOTSUPP           (SOCBASEERR+45)            /* Operation not supported on socket */
#define SOCEPFNOSUPPORT         (SOCBASEERR+46)            /* Protocol family not supported */
#define SOCEAFNOSUPPORT         (SOCBASEERR+47)            /* Address family not supported by protocol family */
#define SOCEADDRINUSE           (SOCBASEERR+48)            /* Address already in use */
#define SOCEADDRNOTAVAIL        (SOCBASEERR+49)            /* Can't assign requested address */
#define SOCENETDOWN             (SOCBASEERR+50)            /* Network is down */
#define SOCENETUNREACH          (SOCBASEERR+51)            /* Network is unreachable */
#define SOCENETRESET            (SOCBASEERR+52)            /* Network dropped connection on reset */
#define SOCECONNABORTED         (SOCBASEERR+53)            /* Software caused connection abort */
#define SOCECONNRESET           (SOCBASEERR+54)            /* Connection reset by peer */
#define SOCENOBUFS              (SOCBASEERR+55)            /* No buffer space available */
#define SOCEISCONN              (SOCBASEERR+56)            /* Socket is already connected */
#define SOCENOTCONN             (SOCBASEERR+57)            /* Socket is not connected */
#define SOCESHUTDOWN            (SOCBASEERR+58)            /* Can't send after socket shutdown */
#define SOCETOOMANYREFS         (SOCBASEERR+59)            /* Too many references: can't splice */
#define SOCETIMEDOUT            (SOCBASEERR+60)            /* Connection timed out */
#define SOCECONNREFUSED         (SOCBASEERR+61)            /* Connection refused */
#define SOCELOOP                (SOCBASEERR+62)            /* Too many levels of symbolic links */
#define SOCENAMETOOLONG         (SOCBASEERR+63)            /* File name too long */
#define SOCEHOSTDOWN            (SOCBASEERR+64)            /* Host is down */
#define SOCEHOSTUNREACH         (SOCBASEERR+65)            /* No route to host */
#define SOCENOTEMPTY            (SOCBASEERR+66)            /* Directory not empty */

/* APR CANONICAL ERROR TESTS */
#define APR_STATUS_IS_EACCES(s)         ((s) == APR_EACCES \
                || (s) == APR_OS_START_SYSERR + ERROR_ACCESS_DENIED \
                || (s) == APR_OS_START_SYSERR + ERROR_SHARING_VIOLATION)
#define APR_STATUS_IS_EEXIST(s)         ((s) == APR_EEXIST \
                || (s) == APR_OS_START_SYSERR + ERROR_OPEN_FAILED \
                || (s) == APR_OS_START_SYSERR + ERROR_FILE_EXISTS \
                || (s) == APR_OS_START_SYSERR + ERROR_ALREADY_EXISTS \
                || (s) == APR_OS_START_SYSERR + ERROR_ACCESS_DENIED)
#define APR_STATUS_IS_ENAMETOOLONG(s)   ((s) == APR_ENAMETOOLONG \
                || (s) == APR_OS_START_SYSERR + ERROR_FILENAME_EXCED_RANGE \
                || (s) == APR_OS_START_SYSERR + SOCENAMETOOLONG)
#define APR_STATUS_IS_ENOENT(s)         ((s) == APR_ENOENT \
                || (s) == APR_OS_START_SYSERR + ERROR_FILE_NOT_FOUND \
                || (s) == APR_OS_START_SYSERR + ERROR_PATH_NOT_FOUND \
                || (s) == APR_OS_START_SYSERR + ERROR_NO_MORE_FILES \
                || (s) == APR_OS_START_SYSERR + ERROR_OPEN_FAILED)
#define APR_STATUS_IS_ENOTDIR(s)        ((s) == APR_ENOTDIR)
#define APR_STATUS_IS_ENOSPC(s)         ((s) == APR_ENOSPC \
                || (s) == APR_OS_START_SYSERR + ERROR_DISK_FULL)
#define APR_STATUS_IS_ENOMEM(s)         ((s) == APR_ENOMEM)
#define APR_STATUS_IS_EMFILE(s)         ((s) == APR_EMFILE \
                || (s) == APR_OS_START_SYSERR + ERROR_TOO_MANY_OPEN_FILES)
#define APR_STATUS_IS_ENFILE(s)         ((s) == APR_ENFILE)
#define APR_STATUS_IS_EBADF(s)          ((s) == APR_EBADF \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_HANDLE)
#define APR_STATUS_IS_EINVAL(s)         ((s) == APR_EINVAL \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_PARAMETER \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_FUNCTION)
#define APR_STATUS_IS_ESPIPE(s)         ((s) == APR_ESPIPE \
                || (s) == APR_OS_START_SYSERR + ERROR_NEGATIVE_SEEK)
#define APR_STATUS_IS_EAGAIN(s)         ((s) == APR_EAGAIN \
                || (s) == APR_OS_START_SYSERR + ERROR_NO_DATA \
                || (s) == APR_OS_START_SYSERR + SOCEWOULDBLOCK \
                || (s) == APR_OS_START_SYSERR + ERROR_LOCK_VIOLATION)
#define APR_STATUS_IS_EINTR(s)          ((s) == APR_EINTR \
                || (s) == APR_OS_START_SYSERR + SOCEINTR)
#define APR_STATUS_IS_ENOTSOCK(s)       ((s) == APR_ENOTSOCK \
                || (s) == APR_OS_START_SYSERR + SOCENOTSOCK)
#define APR_STATUS_IS_ECONNREFUSED(s)   ((s) == APR_ECONNREFUSED \
                || (s) == APR_OS_START_SYSERR + SOCECONNREFUSED)
#define APR_STATUS_IS_EINPROGRESS(s)    ((s) == APR_EINPROGRESS \
                || (s) == APR_OS_START_SYSERR + SOCEINPROGRESS)
#define APR_STATUS_IS_ECONNABORTED(s)   ((s) == APR_ECONNABORTED \
                || (s) == APR_OS_START_SYSERR + SOCECONNABORTED)
#define APR_STATUS_IS_ECONNRESET(s)     ((s) == APR_ECONNRESET \
                || (s) == APR_OS_START_SYSERR + SOCECONNRESET)
/* XXX deprecated */
#define APR_STATUS_IS_ETIMEDOUT(s)         ((s) == APR_ETIMEDOUT \
                || (s) == APR_OS_START_SYSERR + SOCETIMEDOUT)
#undef APR_STATUS_IS_TIMEUP
#define APR_STATUS_IS_TIMEUP(s)         ((s) == APR_TIMEUP \
                || (s) == APR_OS_START_SYSERR + SOCETIMEDOUT)
#define APR_STATUS_IS_EHOSTUNREACH(s)   ((s) == APR_EHOSTUNREACH \
                || (s) == APR_OS_START_SYSERR + SOCEHOSTUNREACH)
#define APR_STATUS_IS_ENETUNREACH(s)    ((s) == APR_ENETUNREACH \
                || (s) == APR_OS_START_SYSERR + SOCENETUNREACH)
#define APR_STATUS_IS_EFTYPE(s)         ((s) == APR_EFTYPE)
#define APR_STATUS_IS_EPIPE(s)          ((s) == APR_EPIPE \
                || (s) == APR_OS_START_SYSERR + ERROR_BROKEN_PIPE \
                || (s) == APR_OS_START_SYSERR + SOCEPIPE)
#define APR_STATUS_IS_EXDEV(s)          ((s) == APR_EXDEV \
                || (s) == APR_OS_START_SYSERR + ERROR_NOT_SAME_DEVICE)
#define APR_STATUS_IS_ENOTEMPTY(s)      ((s) == APR_ENOTEMPTY \
                || (s) == APR_OS_START_SYSERR + ERROR_DIR_NOT_EMPTY \
                || (s) == APR_OS_START_SYSERR + ERROR_ACCESS_DENIED)
#define APR_STATUS_IS_EAFNOSUPPORT(s)   ((s) == APR_AFNOSUPPORT \
                || (s) == APR_OS_START_SYSERR + SOCEAFNOSUPPORT)

/*
    Sorry, too tired to wrap this up for OS2... feel free to
    fit the following into their best matches.

    { ERROR_NO_SIGNAL_SENT,     ESRCH           },
    { SOCEALREADY,              EALREADY        },
    { SOCEDESTADDRREQ,          EDESTADDRREQ    },
    { SOCEMSGSIZE,              EMSGSIZE        },
    { SOCEPROTOTYPE,            EPROTOTYPE      },
    { SOCENOPROTOOPT,           ENOPROTOOPT     },
    { SOCEPROTONOSUPPORT,       EPROTONOSUPPORT },
    { SOCESOCKTNOSUPPORT,       ESOCKTNOSUPPORT },
    { SOCEOPNOTSUPP,            EOPNOTSUPP      },
    { SOCEPFNOSUPPORT,          EPFNOSUPPORT    },
    { SOCEADDRINUSE,            EADDRINUSE      },
    { SOCEADDRNOTAVAIL,         EADDRNOTAVAIL   },
    { SOCENETDOWN,              ENETDOWN        },
    { SOCENETRESET,             ENETRESET       },
    { SOCENOBUFS,               ENOBUFS         },
    { SOCEISCONN,               EISCONN         },
    { SOCENOTCONN,              ENOTCONN        },
    { SOCESHUTDOWN,             ESHUTDOWN       },
    { SOCETOOMANYREFS,          ETOOMANYREFS    },
    { SOCELOOP,                 ELOOP           },
    { SOCEHOSTDOWN,             EHOSTDOWN       },
    { SOCENOTEMPTY,             ENOTEMPTY       },
    { SOCEPIPE,                 EPIPE           }
*/

#elif defined(WIN32) && !defined(DOXYGEN) /* !defined(OS2) */

#define APR_FROM_OS_ERROR(e) (e == 0 ? APR_SUCCESS : e + APR_OS_START_SYSERR)
#define APR_TO_OS_ERROR(e)   (e == 0 ? APR_SUCCESS : e - APR_OS_START_SYSERR)

#define apr_get_os_error()   (APR_FROM_OS_ERROR(GetLastError()))
#define apr_set_os_error(e)  (SetLastError(APR_TO_OS_ERROR(e)))

/* A special case, only socket calls require this:
 */
#define apr_get_netos_error()   (APR_FROM_OS_ERROR(WSAGetLastError()))
#define apr_set_netos_error(e)   (WSASetLastError(APR_TO_OS_ERROR(e)))

/* APR CANONICAL ERROR TESTS */
#define APR_STATUS_IS_EACCES(s)         ((s) == APR_EACCES \
                || (s) == APR_OS_START_SYSERR + ERROR_ACCESS_DENIED \
                || (s) == APR_OS_START_SYSERR + ERROR_CANNOT_MAKE \
                || (s) == APR_OS_START_SYSERR + ERROR_CURRENT_DIRECTORY \
                || (s) == APR_OS_START_SYSERR + ERROR_DRIVE_LOCKED \
                || (s) == APR_OS_START_SYSERR + ERROR_FAIL_I24 \
                || (s) == APR_OS_START_SYSERR + ERROR_LOCK_VIOLATION \
                || (s) == APR_OS_START_SYSERR + ERROR_LOCK_FAILED \
                || (s) == APR_OS_START_SYSERR + ERROR_NOT_LOCKED \
                || (s) == APR_OS_START_SYSERR + ERROR_NETWORK_ACCESS_DENIED \
                || (s) == APR_OS_START_SYSERR + ERROR_SHARING_VIOLATION)
#define APR_STATUS_IS_EEXIST(s)         ((s) == APR_EEXIST \
                || (s) == APR_OS_START_SYSERR + ERROR_FILE_EXISTS \
                || (s) == APR_OS_START_SYSERR + ERROR_ALREADY_EXISTS)
#define APR_STATUS_IS_ENAMETOOLONG(s)   ((s) == APR_ENAMETOOLONG \
                || (s) == APR_OS_START_SYSERR + ERROR_FILENAME_EXCED_RANGE \
                || (s) == APR_OS_START_SYSERR + WSAENAMETOOLONG)
#define APR_STATUS_IS_ENOENT(s)         ((s) == APR_ENOENT \
                || (s) == APR_OS_START_SYSERR + ERROR_FILE_NOT_FOUND \
                || (s) == APR_OS_START_SYSERR + ERROR_PATH_NOT_FOUND \
                || (s) == APR_OS_START_SYSERR + ERROR_OPEN_FAILED \
                || (s) == APR_OS_START_SYSERR + ERROR_NO_MORE_FILES)
#define APR_STATUS_IS_ENOTDIR(s)        ((s) == APR_ENOTDIR \
                || (s) == APR_OS_START_SYSERR + ERROR_PATH_NOT_FOUND \
                || (s) == APR_OS_START_SYSERR + ERROR_BAD_NETPATH \
                || (s) == APR_OS_START_SYSERR + ERROR_BAD_NET_NAME \
                || (s) == APR_OS_START_SYSERR + ERROR_BAD_PATHNAME \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_DRIVE \
                || (s) == APR_OS_START_SYSERR + ERROR_DIRECTORY)
#define APR_STATUS_IS_ENOSPC(s)         ((s) == APR_ENOSPC \
                || (s) == APR_OS_START_SYSERR + ERROR_DISK_FULL)
#define APR_STATUS_IS_ENOMEM(s)         ((s) == APR_ENOMEM \
                || (s) == APR_OS_START_SYSERR + ERROR_ARENA_TRASHED \
                || (s) == APR_OS_START_SYSERR + ERROR_NOT_ENOUGH_MEMORY \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_BLOCK \
                || (s) == APR_OS_START_SYSERR + ERROR_NOT_ENOUGH_QUOTA \
                || (s) == APR_OS_START_SYSERR + ERROR_OUTOFMEMORY)
#define APR_STATUS_IS_EMFILE(s)         ((s) == APR_EMFILE \
                || (s) == APR_OS_START_SYSERR + ERROR_TOO_MANY_OPEN_FILES)
#define APR_STATUS_IS_ENFILE(s)         ((s) == APR_ENFILE)
#define APR_STATUS_IS_EBADF(s)          ((s) == APR_EBADF \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_HANDLE \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_TARGET_HANDLE)
#define APR_STATUS_IS_EINVAL(s)         ((s) == APR_EINVAL \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_ACCESS \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_DATA \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_FUNCTION \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_HANDLE \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_PARAMETER \
                || (s) == APR_OS_START_SYSERR + ERROR_NEGATIVE_SEEK)
#define APR_STATUS_IS_ESPIPE(s)         ((s) == APR_ESPIPE \
                || (s) == APR_OS_START_SYSERR + ERROR_SEEK_ON_DEVICE \
                || (s) == APR_OS_START_SYSERR + ERROR_NEGATIVE_SEEK)
#define APR_STATUS_IS_EAGAIN(s)         ((s) == APR_EAGAIN \
                || (s) == APR_OS_START_SYSERR + ERROR_NO_DATA \
                || (s) == APR_OS_START_SYSERR + ERROR_NO_PROC_SLOTS \
                || (s) == APR_OS_START_SYSERR + ERROR_NESTING_NOT_ALLOWED \
                || (s) == APR_OS_START_SYSERR + ERROR_MAX_THRDS_REACHED \
                || (s) == APR_OS_START_SYSERR + ERROR_LOCK_VIOLATION \
                || (s) == APR_OS_START_SYSERR + WSAEWOULDBLOCK)
#define APR_STATUS_IS_EINTR(s)          ((s) == APR_EINTR \
                || (s) == APR_OS_START_SYSERR + WSAEINTR)
#define APR_STATUS_IS_ENOTSOCK(s)       ((s) == APR_ENOTSOCK \
                || (s) == APR_OS_START_SYSERR + WSAENOTSOCK)
#define APR_STATUS_IS_ECONNREFUSED(s)   ((s) == APR_ECONNREFUSED \
                || (s) == APR_OS_START_SYSERR + WSAECONNREFUSED)
#define APR_STATUS_IS_EINPROGRESS(s)    ((s) == APR_EINPROGRESS \
                || (s) == APR_OS_START_SYSERR + WSAEINPROGRESS)
#define APR_STATUS_IS_ECONNABORTED(s)   ((s) == APR_ECONNABORTED \
                || (s) == APR_OS_START_SYSERR + WSAECONNABORTED)
#define APR_STATUS_IS_ECONNRESET(s)     ((s) == APR_ECONNRESET \
                || (s) == APR_OS_START_SYSERR + ERROR_NETNAME_DELETED \
                || (s) == APR_OS_START_SYSERR + WSAECONNRESET)
/* XXX deprecated */
#define APR_STATUS_IS_ETIMEDOUT(s)         ((s) == APR_ETIMEDOUT \
                || (s) == APR_OS_START_SYSERR + WSAETIMEDOUT \
                || (s) == APR_OS_START_SYSERR + WAIT_TIMEOUT)
#undef APR_STATUS_IS_TIMEUP
#define APR_STATUS_IS_TIMEUP(s)         ((s) == APR_TIMEUP \
                || (s) == APR_OS_START_SYSERR + WSAETIMEDOUT \
                || (s) == APR_OS_START_SYSERR + WAIT_TIMEOUT)
#define APR_STATUS_IS_EHOSTUNREACH(s)   ((s) == APR_EHOSTUNREACH \
                || (s) == APR_OS_START_SYSERR + WSAEHOSTUNREACH)
#define APR_STATUS_IS_ENETUNREACH(s)    ((s) == APR_ENETUNREACH \
                || (s) == APR_OS_START_SYSERR + WSAENETUNREACH)
#define APR_STATUS_IS_EFTYPE(s)         ((s) == APR_EFTYPE \
                || (s) == APR_OS_START_SYSERR + ERROR_EXE_MACHINE_TYPE_MISMATCH \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_DLL \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_MODULETYPE \
                || (s) == APR_OS_START_SYSERR + ERROR_BAD_EXE_FORMAT \
                || (s) == APR_OS_START_SYSERR + ERROR_INVALID_EXE_SIGNATURE \
                || (s) == APR_OS_START_SYSERR + ERROR_FILE_CORRUPT \
                || (s) == APR_OS_START_SYSERR + ERROR_BAD_FORMAT)
#define APR_STATUS_IS_EPIPE(s)          ((s) == APR_EPIPE \
                || (s) == APR_OS_START_SYSERR + ERROR_BROKEN_PIPE)
#define APR_STATUS_IS_EXDEV(s)          ((s) == APR_EXDEV \
                || (s) == APR_OS_START_SYSERR + ERROR_NOT_SAME_DEVICE)
#define APR_STATUS_IS_ENOTEMPTY(s)      ((s) == APR_ENOTEMPTY \
                || (s) == APR_OS_START_SYSERR + ERROR_DIR_NOT_EMPTY)
#define APR_STATUS_IS_EAFNOSUPPORT(s)   ((s) == APR_EAFNOSUPPORT \
                || (s) == APR_OS_START_SYSERR + WSAEAFNOSUPPORT)

#elif defined(NETWARE) && defined(USE_WINSOCK) && !defined(DOXYGEN) /* !defined(OS2) && !defined(WIN32) */

#define APR_FROM_OS_ERROR(e) (e == 0 ? APR_SUCCESS : e + APR_OS_START_SYSERR)
#define APR_TO_OS_ERROR(e)   (e == 0 ? APR_SUCCESS : e - APR_OS_START_SYSERR)

#define apr_get_os_error()    (errno)
#define apr_set_os_error(e)   (errno = (e))

/* A special case, only socket calls require this: */
#define apr_get_netos_error()   (APR_FROM_OS_ERROR(WSAGetLastError()))
#define apr_set_netos_error(e)  (WSASetLastError(APR_TO_OS_ERROR(e)))

/* APR CANONICAL ERROR TESTS */
#define APR_STATUS_IS_EACCES(s)         ((s) == APR_EACCES)
#define APR_STATUS_IS_EEXIST(s)         ((s) == APR_EEXIST)
#define APR_STATUS_IS_ENAMETOOLONG(s)   ((s) == APR_ENAMETOOLONG)
#define APR_STATUS_IS_ENOENT(s)         ((s) == APR_ENOENT)
#define APR_STATUS_IS_ENOTDIR(s)        ((s) == APR_ENOTDIR)
#define APR_STATUS_IS_ENOSPC(s)         ((s) == APR_ENOSPC)
#define APR_STATUS_IS_ENOMEM(s)         ((s) == APR_ENOMEM)
#define APR_STATUS_IS_EMFILE(s)         ((s) == APR_EMFILE)
#define APR_STATUS_IS_ENFILE(s)         ((s) == APR_ENFILE)
#define APR_STATUS_IS_EBADF(s)          ((s) == APR_EBADF)
#define APR_STATUS_IS_EINVAL(s)         ((s) == APR_EINVAL)
#define APR_STATUS_IS_ESPIPE(s)         ((s) == APR_ESPIPE)

#define APR_STATUS_IS_EAGAIN(s)         ((s) == APR_EAGAIN \
                || (s) ==                       EWOULDBLOCK \
                || (s) == APR_OS_START_SYSERR + WSAEWOULDBLOCK)
#define APR_STATUS_IS_EINTR(s)          ((s) == APR_EINTR \
                || (s) == APR_OS_START_SYSERR + WSAEINTR)
#define APR_STATUS_IS_ENOTSOCK(s)       ((s) == APR_ENOTSOCK \
                || (s) == APR_OS_START_SYSERR + WSAENOTSOCK)
#define APR_STATUS_IS_ECONNREFUSED(s)   ((s) == APR_ECONNREFUSED \
                || (s) == APR_OS_START_SYSERR + WSAECONNREFUSED)
#define APR_STATUS_IS_EINPROGRESS(s)    ((s) == APR_EINPROGRESS \
                || (s) == APR_OS_START_SYSERR + WSAEINPROGRESS)
#define APR_STATUS_IS_ECONNABORTED(s)   ((s) == APR_ECONNABORTED \
                || (s) == APR_OS_START_SYSERR + WSAECONNABORTED)
#define APR_STATUS_IS_ECONNRESET(s)     ((s) == APR_ECONNRESET \
                || (s) == APR_OS_START_SYSERR + WSAECONNRESET)
/* XXX deprecated */
#define APR_STATUS_IS_ETIMEDOUT(s)       ((s) == APR_ETIMEDOUT \
                || (s) == APR_OS_START_SYSERR + WSAETIMEDOUT \
                || (s) == APR_OS_START_SYSERR + WAIT_TIMEOUT)
#undef APR_STATUS_IS_TIMEUP
#define APR_STATUS_IS_TIMEUP(s)         ((s) == APR_TIMEUP \
                || (s) == APR_OS_START_SYSERR + WSAETIMEDOUT \
                || (s) == APR_OS_START_SYSERR + WAIT_TIMEOUT)
#define APR_STATUS_IS_EHOSTUNREACH(s)   ((s) == APR_EHOSTUNREACH \
                || (s) == APR_OS_START_SYSERR + WSAEHOSTUNREACH)
#define APR_STATUS_IS_ENETUNREACH(s)    ((s) == APR_ENETUNREACH \
                || (s) == APR_OS_START_SYSERR + WSAENETUNREACH)
#define APR_STATUS_IS_ENETDOWN(s)       ((s) == APR_OS_START_SYSERR + WSAENETDOWN)
#define APR_STATUS_IS_EFTYPE(s)         ((s) == APR_EFTYPE)
#define APR_STATUS_IS_EPIPE(s)          ((s) == APR_EPIPE)
#define APR_STATUS_IS_EXDEV(s)          ((s) == APR_EXDEV)
#define APR_STATUS_IS_ENOTEMPTY(s)      ((s) == APR_ENOTEMPTY)
#define APR_STATUS_IS_EAFNOSUPPORT(s)   ((s) == APR_EAFNOSUPPORT \
                || (s) == APR_OS_START_SYSERR + WSAEAFNOSUPPORT)

#else /* !defined(NETWARE) && !defined(OS2) && !defined(WIN32) */

/*
 *  os error codes are clib error codes
 */
#define APR_FROM_OS_ERROR(e)  (e)
#define APR_TO_OS_ERROR(e)    (e)

#define apr_get_os_error()    (errno)
#define apr_set_os_error(e)   (errno = (e))

/* A special case, only socket calls require this:
 */
#define apr_get_netos_error() (errno)
#define apr_set_netos_error(e) (errno = (e))

/**
 * @addtogroup APR_STATUS_IS
 * @{
 */

/** permission denied */
#define APR_STATUS_IS_EACCES(s)         ((s) == APR_EACCES)
/** file exists */
#define APR_STATUS_IS_EEXIST(s)         ((s) == APR_EEXIST)
/** path name is too long */
#define APR_STATUS_IS_ENAMETOOLONG(s)   ((s) == APR_ENAMETOOLONG)
/**
 * no such file or directory
 * @remark
 * EMVSCATLG can be returned by the automounter on z/OS for
 * paths which do not exist.
 */
#ifdef EMVSCATLG
#define APR_STATUS_IS_ENOENT(s)         ((s) == APR_ENOENT \
                                      || (s) == EMVSCATLG)
#else
#define APR_STATUS_IS_ENOENT(s)         ((s) == APR_ENOENT)
#endif
/** not a directory */
#define APR_STATUS_IS_ENOTDIR(s)        ((s) == APR_ENOTDIR)
/** no space left on device */
#ifdef EDQUOT
#define APR_STATUS_IS_ENOSPC(s)         ((s) == APR_ENOSPC \
                                      || (s) == EDQUOT)
#else
#define APR_STATUS_IS_ENOSPC(s)         ((s) == APR_ENOSPC)
#endif
/** not enough memory */
#define APR_STATUS_IS_ENOMEM(s)         ((s) == APR_ENOMEM)
/** too many open files */
#define APR_STATUS_IS_EMFILE(s)         ((s) == APR_EMFILE)
/** file table overflow */
#define APR_STATUS_IS_ENFILE(s)         ((s) == APR_ENFILE)
/** bad file # */
#define APR_STATUS_IS_EBADF(s)          ((s) == APR_EBADF)
/** invalid argument */
#define APR_STATUS_IS_EINVAL(s)         ((s) == APR_EINVAL)
/** illegal seek */
#define APR_STATUS_IS_ESPIPE(s)         ((s) == APR_ESPIPE)

/** operation would block */
#if !defined(EWOULDBLOCK) || !defined(EAGAIN)
#define APR_STATUS_IS_EAGAIN(s)         ((s) == APR_EAGAIN)
#elif (EWOULDBLOCK == EAGAIN)
#define APR_STATUS_IS_EAGAIN(s)         ((s) == APR_EAGAIN)
#else
#define APR_STATUS_IS_EAGAIN(s)         ((s) == APR_EAGAIN \
                                      || (s) == EWOULDBLOCK)
#endif

/** interrupted system call */
#define APR_STATUS_IS_EINTR(s)          ((s) == APR_EINTR)
/** socket operation on a non-socket */
#define APR_STATUS_IS_ENOTSOCK(s)       ((s) == APR_ENOTSOCK)
/** Connection Refused */
#define APR_STATUS_IS_ECONNREFUSED(s)   ((s) == APR_ECONNREFUSED)
/** operation now in progress */
#define APR_STATUS_IS_EINPROGRESS(s)    ((s) == APR_EINPROGRESS)

/**
 * Software caused connection abort
 * @remark
 * EPROTO on certain older kernels really means ECONNABORTED, so we need to
 * ignore it for them.  See discussion in new-httpd archives nh.9701 & nh.9603
 *
 * There is potentially a bug in Solaris 2.x x<6, and other boxes that
 * implement tcp sockets in userland (i.e. on top of STREAMS).  On these
 * systems, EPROTO can actually result in a fatal loop.  See PR#981 for
 * example.  It's hard to handle both uses of EPROTO.
 */
#ifdef EPROTO
#define APR_STATUS_IS_ECONNABORTED(s)    ((s) == APR_ECONNABORTED \
                                       || (s) == EPROTO)
#else
#define APR_STATUS_IS_ECONNABORTED(s)    ((s) == APR_ECONNABORTED)
#endif

/** Connection Reset by peer */
#define APR_STATUS_IS_ECONNRESET(s)      ((s) == APR_ECONNRESET)
/** Operation timed out
 *  @deprecated */
#define APR_STATUS_IS_ETIMEDOUT(s)      ((s) == APR_ETIMEDOUT)
/** no route to host */
#define APR_STATUS_IS_EHOSTUNREACH(s)    ((s) == APR_EHOSTUNREACH)
/** network is unreachable */
#define APR_STATUS_IS_ENETUNREACH(s)     ((s) == APR_ENETUNREACH)
/** inappropiate file type or format */
#define APR_STATUS_IS_EFTYPE(s)          ((s) == APR_EFTYPE)
/** broken pipe */
#define APR_STATUS_IS_EPIPE(s)           ((s) == APR_EPIPE)
/** cross device link */
#define APR_STATUS_IS_EXDEV(s)           ((s) == APR_EXDEV)
/** Directory Not Empty */
#define APR_STATUS_IS_ENOTEMPTY(s)       ((s) == APR_ENOTEMPTY || \
                                          (s) == APR_EEXIST)
/** Address Family not supported */
#define APR_STATUS_IS_EAFNOSUPPORT(s)    ((s) == APR_EAFNOSUPPORT)
/** @} */

#endif /* !defined(NETWARE) && !defined(OS2) && !defined(WIN32) */

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* ! APR_ERRNO_H */
