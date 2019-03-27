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

#ifndef NETWORK_IO_H
#define NETWORK_IO_H

#include "apr.h"
#include "apr_private.h"
#include "apr_network_io.h"
#include "apr_errno.h"
#include "apr_general.h"
#include "apr_lib.h"
#ifndef WAITIO_USES_POLL
#include "apr_poll.h"
#endif

/* System headers the network I/O library needs */
#if APR_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if APR_HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#if APR_HAVE_ERRNO_H
#include <errno.h>
#endif
#if APR_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif
#if APR_HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#if APR_HAVE_NETINET_SCTP_UIO_H
#include <netinet/sctp_uio.h>
#endif
#if APR_HAVE_NETINET_SCTP_H
#include <netinet/sctp.h>
#endif
#if APR_HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if APR_HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if APR_HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if APR_HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#if APR_HAVE_NETDB_H
#include <netdb.h>
#endif
#if APR_HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if APR_HAVE_SYS_SENDFILE_H
#include <sys/sendfile.h>
#endif
#if APR_HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
/* End System Headers */

#ifndef HAVE_POLLIN
#define POLLIN   1
#define POLLPRI  2
#define POLLOUT  4
#define POLLERR  8
#define POLLHUP  16
#define POLLNVAL 32
#endif

typedef struct sock_userdata_t sock_userdata_t;
struct sock_userdata_t {
    sock_userdata_t *next;
    const char *key;
    void *data;
};

struct apr_socket_t {
    apr_pool_t *pool;
    int socketdes;
    int type;
    int protocol;
    apr_sockaddr_t *local_addr;
    apr_sockaddr_t *remote_addr;
    apr_interval_time_t timeout; 
#ifndef HAVE_POLL
    int connected;
#endif
    int local_port_unknown;
    int local_interface_unknown;
    int remote_addr_unknown;
    apr_int32_t options;
    apr_int32_t inherit;
    sock_userdata_t *userdata;
#ifndef WAITIO_USES_POLL
    /* if there is a timeout set, then this pollset is used */
    apr_pollset_t *pollset;
#endif
};

const char *apr_inet_ntop(int af, const void *src, char *dst, apr_size_t size);
int apr_inet_pton(int af, const char *src, void *dst);
void apr_sockaddr_vars_set(apr_sockaddr_t *, int, apr_port_t);

#define apr_is_option_set(skt, option)  \
    (((skt)->options & (option)) == (option))

#define apr_set_option(skt, option, on) \
    do {                                 \
        if (on)                          \
            (skt)->options |= (option);         \
        else                             \
            (skt)->options &= ~(option);        \
    } while (0)

#endif  /* ! NETWORK_IO_H */

