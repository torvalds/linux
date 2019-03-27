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

#include "apr_arch_networkio.h"
#include "apr_network_io.h"
#include "apr_strings.h"
#include "apr_support.h"
#include "apr_portable.h"
#include "apr_arch_inherit.h"

#ifdef BEOS_R5
#undef close
#define close closesocket
#endif /* BEOS_R5 */

static char generic_inaddr_any[16] = {0}; /* big enough for IPv4 or IPv6 */

static apr_status_t socket_cleanup(void *sock)
{
    apr_socket_t *thesocket = sock;
    int sd = thesocket->socketdes;

    /* Set socket descriptor to -1 before close(), so that there is no
     * chance of returning an already closed FD from apr_os_sock_get().
     */
    thesocket->socketdes = -1;

    if (close(sd) == 0) {
        return APR_SUCCESS;
    }
    else {
        /* Restore, close() was not successful. */
        thesocket->socketdes = sd;

        return errno;
    }
}

static void set_socket_vars(apr_socket_t *sock, int family, int type, int protocol)
{
    sock->type = type;
    sock->protocol = protocol;
    apr_sockaddr_vars_set(sock->local_addr, family, 0);
    apr_sockaddr_vars_set(sock->remote_addr, family, 0);
    sock->options = 0;
#if defined(BEOS) && !defined(BEOS_BONE)
    /* BeOS pre-BONE has TCP_NODELAY on by default and it can't be
     * switched off!
     */
    sock->options |= APR_TCP_NODELAY;
#endif
}

static void alloc_socket(apr_socket_t **new, apr_pool_t *p)
{
    *new = (apr_socket_t *)apr_pcalloc(p, sizeof(apr_socket_t));
    (*new)->pool = p;
    (*new)->local_addr = (apr_sockaddr_t *)apr_pcalloc((*new)->pool,
                                                       sizeof(apr_sockaddr_t));
    (*new)->local_addr->pool = p;
    (*new)->remote_addr = (apr_sockaddr_t *)apr_pcalloc((*new)->pool,
                                                        sizeof(apr_sockaddr_t));
    (*new)->remote_addr->pool = p;
    (*new)->remote_addr_unknown = 1;
#ifndef WAITIO_USES_POLL
    /* Create a pollset with room for one descriptor. */
    /* ### check return codes */
    (void) apr_pollset_create(&(*new)->pollset, 1, p, 0);
#endif
}

apr_status_t apr_socket_protocol_get(apr_socket_t *sock, int *protocol)
{
    *protocol = sock->protocol;
    return APR_SUCCESS;
}

apr_status_t apr_socket_create(apr_socket_t **new, int ofamily, int type,
                               int protocol, apr_pool_t *cont)
{
    int family = ofamily, flags = 0;

#ifdef HAVE_SOCK_CLOEXEC
    flags |= SOCK_CLOEXEC;
#endif

    if (family == APR_UNSPEC) {
#if APR_HAVE_IPV6
        family = APR_INET6;
#else
        family = APR_INET;
#endif
    }

    alloc_socket(new, cont);

#ifndef BEOS_R5
    (*new)->socketdes = socket(family, type|flags, protocol);
#else
    /* For some reason BeOS R5 has an unconventional protocol numbering,
     * so we need to translate here. */
    switch (protocol) {
    case 0:
        (*new)->socketdes = socket(family, type|flags, 0);
        break;
    case APR_PROTO_TCP:
        (*new)->socketdes = socket(family, type|flags, IPPROTO_TCP);
        break;
    case APR_PROTO_UDP:
        (*new)->socketdes = socket(family, type|flags, IPPROTO_UDP);
        break;
    case APR_PROTO_SCTP:
    default:
        errno = EPROTONOSUPPORT;
        (*new)->socketdes = -1;
        break;
    }
#endif /* BEOS_R5 */

#if APR_HAVE_IPV6
    if ((*new)->socketdes < 0 && ofamily == APR_UNSPEC) {
        family = APR_INET;
        (*new)->socketdes = socket(family, type|flags, protocol);
    }
#endif

    if ((*new)->socketdes < 0) {
        return errno;
    }
    set_socket_vars(*new, family, type, protocol);

#ifndef HAVE_SOCK_CLOEXEC
    {
        int flags;
        apr_status_t rv;

        if ((flags = fcntl((*new)->socketdes, F_GETFD)) == -1) {
            rv = errno;
            close((*new)->socketdes);
            (*new)->socketdes = -1;
            return rv;
        }

        flags |= FD_CLOEXEC;
        if (fcntl((*new)->socketdes, F_SETFD, flags) == -1) {
            rv = errno;
            close((*new)->socketdes);
            (*new)->socketdes = -1;
            return rv;
        }
    }
#endif

    (*new)->timeout = -1;
    (*new)->inherit = 0;
    apr_pool_cleanup_register((*new)->pool, (void *)(*new), socket_cleanup,
                              socket_cleanup);

    return APR_SUCCESS;
} 

apr_status_t apr_socket_shutdown(apr_socket_t *thesocket, 
                                 apr_shutdown_how_e how)
{
    return (shutdown(thesocket->socketdes, how) == -1) ? errno : APR_SUCCESS;
}

apr_status_t apr_socket_close(apr_socket_t *thesocket)
{
    return apr_pool_cleanup_run(thesocket->pool, thesocket, socket_cleanup);
}

apr_status_t apr_socket_bind(apr_socket_t *sock, apr_sockaddr_t *sa)
{
    if (bind(sock->socketdes, 
             (struct sockaddr *)&sa->sa, sa->salen) == -1) {
        return errno;
    }
    else {
        sock->local_addr = sa;
        /* XXX IPv6 - this assumes sin_port and sin6_port at same offset */
        if (sock->local_addr->sa.sin.sin_port == 0) { /* no need for ntohs() when comparing w/ 0 */
            sock->local_port_unknown = 1; /* kernel got us an ephemeral port */
        }
        return APR_SUCCESS;
    }
}

apr_status_t apr_socket_listen(apr_socket_t *sock, apr_int32_t backlog)
{
    if (listen(sock->socketdes, backlog) == -1)
        return errno;
    else
        return APR_SUCCESS;
}

apr_status_t apr_socket_accept(apr_socket_t **new, apr_socket_t *sock,
                               apr_pool_t *connection_context)
{
    int s;
    apr_sockaddr_t sa;

    sa.salen = sizeof(sa.sa);

#ifdef HAVE_ACCEPT4
    {
        int flags = SOCK_CLOEXEC;

#if defined(SOCK_NONBLOCK) && APR_O_NONBLOCK_INHERITED
        /* With FreeBSD accept4() (avail in 10+), O_NONBLOCK is not inherited
         * (unlike Linux).  Mimic the accept() behavior here in a way that
         * may help other platforms.
         */
        if (apr_is_option_set(sock, APR_SO_NONBLOCK) == 1) {
            flags |= SOCK_NONBLOCK;
        }
#endif
        s = accept4(sock->socketdes, (struct sockaddr *)&sa.sa, &sa.salen, flags);
    }
#else
    s = accept(sock->socketdes, (struct sockaddr *)&sa.sa, &sa.salen);
#endif

    if (s < 0) {
        return errno;
    }
#ifdef TPF
    if (s == 0) { 
        /* 0 is an invalid socket for TPF */
        return APR_EINTR;
    }
#endif
    alloc_socket(new, connection_context);

    /* Set up socket variables -- note that it may be possible for
     * *new to be an AF_INET socket when sock is AF_INET6 in some
     * dual-stack configurations, so ensure that the remote_/local_addr
     * structures are adjusted for the family of the accepted
     * socket: */
    set_socket_vars(*new, sa.sa.sin.sin_family, SOCK_STREAM, sock->protocol);

#ifndef HAVE_POLL
    (*new)->connected = 1;
#endif
    (*new)->timeout = -1;

    (*new)->remote_addr_unknown = 0;

    (*new)->socketdes = s;

    /* Copy in peer's address. */
    (*new)->remote_addr->sa = sa.sa;
    (*new)->remote_addr->salen = sa.salen;

    *(*new)->local_addr = *sock->local_addr;

    /* The above assignment just overwrote the pool entry. Setting the local_addr 
       pool for the accepted socket back to what it should be.  Otherwise all 
       allocations for this socket will come from a server pool that is not
       freed until the process goes down.*/
    (*new)->local_addr->pool = connection_context;

    /* fix up any pointers which are no longer valid */
    if (sock->local_addr->sa.sin.sin_family == AF_INET) {
        (*new)->local_addr->ipaddr_ptr = &(*new)->local_addr->sa.sin.sin_addr;
    }
#if APR_HAVE_IPV6
    else if (sock->local_addr->sa.sin.sin_family == AF_INET6) {
        (*new)->local_addr->ipaddr_ptr = &(*new)->local_addr->sa.sin6.sin6_addr;
    }
#endif
    (*new)->remote_addr->port = ntohs((*new)->remote_addr->sa.sin.sin_port);
    if (sock->local_port_unknown) {
        /* not likely for a listening socket, but theoretically possible :) */
        (*new)->local_port_unknown = 1;
    }

#if APR_TCP_NODELAY_INHERITED
    if (apr_is_option_set(sock, APR_TCP_NODELAY) == 1) {
        apr_set_option(*new, APR_TCP_NODELAY, 1);
    }
#endif /* TCP_NODELAY_INHERITED */
#if APR_O_NONBLOCK_INHERITED
    if (apr_is_option_set(sock, APR_SO_NONBLOCK) == 1) {
        apr_set_option(*new, APR_SO_NONBLOCK, 1);
    }
#endif /* APR_O_NONBLOCK_INHERITED */

    if (sock->local_interface_unknown ||
        !memcmp(sock->local_addr->ipaddr_ptr,
                generic_inaddr_any,
                sock->local_addr->ipaddr_len)) {
        /* If the interface address inside the listening socket's local_addr wasn't 
         * up-to-date, we don't know local interface of the connected socket either.
         *
         * If the listening socket was not bound to a specific interface, we
         * don't know the local_addr of the connected socket.
         */
        (*new)->local_interface_unknown = 1;
    }

#ifndef HAVE_ACCEPT4
    {
        int flags;
        apr_status_t rv;

        if ((flags = fcntl((*new)->socketdes, F_GETFD)) == -1) {
            rv = errno;
            close((*new)->socketdes);
            (*new)->socketdes = -1;
            return rv;
        }

        flags |= FD_CLOEXEC;
        if (fcntl((*new)->socketdes, F_SETFD, flags) == -1) {
            rv = errno;
            close((*new)->socketdes);
            (*new)->socketdes = -1;
            return rv;
        }
    }
#endif

    (*new)->inherit = 0;
    apr_pool_cleanup_register((*new)->pool, (void *)(*new), socket_cleanup,
                              socket_cleanup);
    return APR_SUCCESS;
}

apr_status_t apr_socket_connect(apr_socket_t *sock, apr_sockaddr_t *sa)
{
    int rc;        

    do {
        rc = connect(sock->socketdes,
                     (const struct sockaddr *)&sa->sa.sin,
                     sa->salen);
    } while (rc == -1 && errno == EINTR);

    /* we can see EINPROGRESS the first time connect is called on a non-blocking
     * socket; if called again, we can see EALREADY
     */
    if ((rc == -1) && (errno == EINPROGRESS || errno == EALREADY)
                   && (sock->timeout > 0)) {
        rc = apr_wait_for_io_or_timeout(NULL, sock, 0);
        if (rc != APR_SUCCESS) {
            return rc;
        }

#ifdef SO_ERROR
        {
            int error;
            apr_socklen_t len = sizeof(error);
            if ((rc = getsockopt(sock->socketdes, SOL_SOCKET, SO_ERROR, 
                                 (char *)&error, &len)) < 0) {
                return errno;
            }
            if (error) {
                return error;
            }
        }
#endif /* SO_ERROR */
    }


    if (memcmp(sa->ipaddr_ptr, generic_inaddr_any, sa->ipaddr_len)) {
        /* A real remote address was passed in.  If the unspecified
         * address was used, the actual remote addr will have to be
         * determined using getpeername() if required. */
        sock->remote_addr_unknown = 0;

        /* Copy the address structure details in. */
        sock->remote_addr->sa = sa->sa;
        sock->remote_addr->salen = sa->salen;
        /* Adjust ipaddr_ptr et al. */
        apr_sockaddr_vars_set(sock->remote_addr, sa->family, sa->port);
    }

    if (sock->local_addr->port == 0) {
        /* connect() got us an ephemeral port */
        sock->local_port_unknown = 1;
    }
    if (!memcmp(sock->local_addr->ipaddr_ptr,
                generic_inaddr_any,
                sock->local_addr->ipaddr_len)) {
        /* not bound to specific local interface; connect() had to assign
         * one for the socket
         */
        sock->local_interface_unknown = 1;
    }

    if (rc == -1 && errno != EISCONN) {
        return errno;
    }

#ifndef HAVE_POLL
    sock->connected=1;
#endif
    return APR_SUCCESS;
}

apr_status_t apr_socket_type_get(apr_socket_t *sock, int *type)
{
    *type = sock->type;
    return APR_SUCCESS;
}

apr_status_t apr_socket_data_get(void **data, const char *key, apr_socket_t *sock)
{
    sock_userdata_t *cur = sock->userdata;

    *data = NULL;

    while (cur) {
        if (!strcmp(cur->key, key)) {
            *data = cur->data;
            break;
        }
        cur = cur->next;
    }

    return APR_SUCCESS;
}

apr_status_t apr_socket_data_set(apr_socket_t *sock, void *data, const char *key,
                                 apr_status_t (*cleanup) (void *))
{
    sock_userdata_t *new = apr_palloc(sock->pool, sizeof(sock_userdata_t));

    new->key = apr_pstrdup(sock->pool, key);
    new->data = data;
    new->next = sock->userdata;
    sock->userdata = new;

    if (cleanup) {
        apr_pool_cleanup_register(sock->pool, data, cleanup, cleanup);
    }

    return APR_SUCCESS;
}

apr_status_t apr_os_sock_get(apr_os_sock_t *thesock, apr_socket_t *sock)
{
    *thesock = sock->socketdes;
    return APR_SUCCESS;
}

apr_status_t apr_os_sock_make(apr_socket_t **apr_sock, 
                              apr_os_sock_info_t *os_sock_info, 
                              apr_pool_t *cont)
{
    alloc_socket(apr_sock, cont);
    set_socket_vars(*apr_sock, os_sock_info->family, os_sock_info->type, os_sock_info->protocol);
    (*apr_sock)->timeout = -1;
    (*apr_sock)->socketdes = *os_sock_info->os_sock;
    if (os_sock_info->local) {
        memcpy(&(*apr_sock)->local_addr->sa.sin, 
               os_sock_info->local, 
               (*apr_sock)->local_addr->salen);
        /* XXX IPv6 - this assumes sin_port and sin6_port at same offset */
        (*apr_sock)->local_addr->port = ntohs((*apr_sock)->local_addr->sa.sin.sin_port);
    }
    else {
        (*apr_sock)->local_port_unknown = (*apr_sock)->local_interface_unknown = 1;
    }
    if (os_sock_info->remote) {
#ifndef HAVE_POLL
        (*apr_sock)->connected = 1;
#endif
        memcpy(&(*apr_sock)->remote_addr->sa.sin, 
               os_sock_info->remote,
               (*apr_sock)->remote_addr->salen);
        /* XXX IPv6 - this assumes sin_port and sin6_port at same offset */
        (*apr_sock)->remote_addr->port = ntohs((*apr_sock)->remote_addr->sa.sin.sin_port);
    }
    else {
        (*apr_sock)->remote_addr_unknown = 1;
    }
        
    (*apr_sock)->inherit = 0;
    apr_pool_cleanup_register((*apr_sock)->pool, (void *)(*apr_sock), 
                              socket_cleanup, socket_cleanup);
    return APR_SUCCESS;
}

apr_status_t apr_os_sock_put(apr_socket_t **sock, apr_os_sock_t *thesock, 
                           apr_pool_t *cont)
{
    /* XXX Bogus assumption that *sock points at anything legit */
    if ((*sock) == NULL) {
        alloc_socket(sock, cont);
        /* XXX IPv6 figure out the family here! */
        /* XXX figure out the actual socket type here */
        /* *or* just decide that apr_os_sock_put() has to be told the family and type */
        set_socket_vars(*sock, APR_INET, SOCK_STREAM, 0);
        (*sock)->timeout = -1;
    }
    (*sock)->local_port_unknown = (*sock)->local_interface_unknown = 1;
    (*sock)->remote_addr_unknown = 1;
    (*sock)->socketdes = *thesock;
    return APR_SUCCESS;
}

APR_POOL_IMPLEMENT_ACCESSOR(socket)

APR_IMPLEMENT_INHERIT_SET(socket, inherit, pool, socket_cleanup)

APR_IMPLEMENT_INHERIT_UNSET(socket, inherit, pool, socket_cleanup)
