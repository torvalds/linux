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

#include "apr_arch_file_io.h"
#include "apr_arch_networkio.h"
#include "apr_poll.h"
#include "apr_errno.h"
#include "apr_support.h"

/* The only case where we don't use wait_for_io_or_timeout is on
 * pre-BONE BeOS, so this check should be sufficient and simpler */
#if !defined(BEOS_R5) && !defined(OS2) && APR_FILES_AS_SOCKETS
#define USE_WAIT_FOR_IO
#endif

#ifdef USE_WAIT_FOR_IO

#ifdef WAITIO_USES_POLL

#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif

apr_status_t apr_wait_for_io_or_timeout(apr_file_t *f, apr_socket_t *s,
                                        int for_read)
{
    struct pollfd pfd;
    int rc, timeout;

    timeout    = f        ? f->timeout / 1000 : s->timeout / 1000;
    pfd.fd     = f        ? f->filedes        : s->socketdes;
    pfd.events = for_read ? POLLIN            : POLLOUT;

    do {
        rc = poll(&pfd, 1, timeout);
    } while (rc == -1 && errno == EINTR);
    if (rc == 0) {
        return APR_TIMEUP;
    }
    else if (rc > 0) {
        return APR_SUCCESS;
    }
    else {
        return errno;
    }
}

#else /* !WAITIO_USES_POLL */

apr_status_t apr_wait_for_io_or_timeout(apr_file_t *f, apr_socket_t *s,
                                        int for_read)
{
    apr_interval_time_t timeout;
    apr_pollfd_t pfd;
    int type = for_read ? APR_POLLIN : APR_POLLOUT;
    apr_pollset_t *pollset;
    apr_status_t status;

    /* TODO - timeout should be less each time through this loop */
    if (f) {
        pfd.desc_type = APR_POLL_FILE;
        pfd.desc.f = f;

        pollset = f->pollset;
        if (pollset == NULL) {
            status = apr_pollset_create(&(f->pollset), 1, f->pool, 0);
            if (status != APR_SUCCESS) {
                return status;
            }
            pollset = f->pollset;
        }
        timeout = f->timeout;
    }
    else {
        pfd.desc_type = APR_POLL_SOCKET;
        pfd.desc.s = s;

        pollset = s->pollset;
        timeout = s->timeout;
    }
    pfd.reqevents = type;

    /* Remove the object if it was in the pollset, then add in the new
     * object with the correct reqevents value. Ignore the status result
     * on the remove, because it might not be in there (yet).
     */
    (void) apr_pollset_remove(pollset, &pfd);

    /* ### check status code */
    (void) apr_pollset_add(pollset, &pfd);

    do {
        int numdesc;
        const apr_pollfd_t *pdesc;

        status = apr_pollset_poll(pollset, timeout, &numdesc, &pdesc);

        if (numdesc == 1 && (pdesc[0].rtnevents & type) != 0) {
            return APR_SUCCESS;
        }
    } while (APR_STATUS_IS_EINTR(status));

    return status;
}
#endif /* WAITIO_USES_POLL */

#endif /* USE_WAIT_FOR_IO */
