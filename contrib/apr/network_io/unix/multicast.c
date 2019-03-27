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
#include "apr_support.h"
#include "apr_portable.h"
#include "apr_arch_inherit.h"

#ifdef HAVE_GETIFADDRS
#include <net/if.h>
#include <ifaddrs.h>
#endif

#ifdef HAVE_STRUCT_IPMREQ
static void fill_mip_v4(struct ip_mreq *mip, apr_sockaddr_t *mcast,
                        apr_sockaddr_t *iface)
{
    mip->imr_multiaddr = mcast->sa.sin.sin_addr;
    if (iface == NULL) {
        mip->imr_interface.s_addr = INADDR_ANY;
    }
    else {
        mip->imr_interface = iface->sa.sin.sin_addr;
    }
}

/* This function is only interested in AF_INET6 sockets, so a noop
 * "return 0" implementation for the !APR_HAVE_IPV6 build is
 * sufficient. */
static unsigned int find_if_index(const apr_sockaddr_t *iface)
{
    unsigned int index = 0;
#if defined(HAVE_GETIFADDRS) && APR_HAVE_IPV6 
    struct ifaddrs *ifp, *ifs;

    /**
     * TODO: getifaddrs is only portable to *BSD and OS X. Using ioctl 
     *       and SIOCGIFCONF is needed for Linux/Solaris support.
     *       
     *       There is a wrapper that takes the messy ioctl interface into 
     *       getifaddrs. The license is acceptable, but, It is a fairly large 
     *       chunk of code.
     */
    if (getifaddrs(&ifs) != 0) {
        return 0;
    }

    for (ifp = ifs; ifp; ifp = ifp->ifa_next) {
        if (ifp->ifa_addr != NULL && ifp->ifa_addr->sa_family == AF_INET6) {
            if (memcmp(&iface->sa.sin6.sin6_addr,
                       &ifp->ifa_addr->sa_data[0],
                       sizeof(iface->sa.sin6.sin6_addr)) == 0) {
                index = if_nametoindex(ifp->ifa_name);
                break;
            }
        }
    }

    freeifaddrs(ifs);
#endif
    return index;
}

#if APR_HAVE_IPV6
static void fill_mip_v6(struct ipv6_mreq *mip, const apr_sockaddr_t *mcast,
                        const apr_sockaddr_t *iface)
{
    memcpy(&mip->ipv6mr_multiaddr, mcast->ipaddr_ptr,
           sizeof(mip->ipv6mr_multiaddr));

    if (iface == NULL) {
        mip->ipv6mr_interface = 0;
    }
    else {
        mip->ipv6mr_interface = find_if_index(iface);
    }
}

#endif

static int sock_is_ipv4(apr_socket_t *sock)
{
    if (sock->local_addr->family == APR_INET)
        return 1;
    return 0;
}

#if APR_HAVE_IPV6
static int sock_is_ipv6(apr_socket_t *sock)
{
    if (sock->local_addr->family == APR_INET6)
        return 1;
    return 0;
}
#endif

static apr_status_t do_mcast(int type, apr_socket_t *sock,
                             apr_sockaddr_t *mcast, apr_sockaddr_t *iface,
                             apr_sockaddr_t *source)
{
    struct ip_mreq mip4;
    apr_status_t rv = APR_SUCCESS;
#if APR_HAVE_IPV6
    struct ipv6_mreq mip6;
#endif
#ifdef GROUP_FILTER_SIZE
    struct group_source_req mip;
    int ip_proto;
#endif

    if (source != NULL) {
#ifdef GROUP_FILTER_SIZE
        if (sock_is_ipv4(sock)) {
            ip_proto = IPPROTO_IP;
        } 
#if APR_HAVE_IPV6
        else if (sock_is_ipv6(sock)) {
            ip_proto = IPPROTO_IPV6;
        }
#endif
        else {
            return APR_ENOTIMPL;
        }

        if (type == IP_ADD_MEMBERSHIP)
            type = MCAST_JOIN_SOURCE_GROUP;
        else if (type == IP_DROP_MEMBERSHIP)
            type = MCAST_LEAVE_SOURCE_GROUP;
        else
            return APR_ENOTIMPL;

        mip.gsr_interface = find_if_index(iface);
        memcpy(&mip.gsr_group, mcast->ipaddr_ptr, sizeof(mip.gsr_group));
        memcpy(&mip.gsr_source, source->ipaddr_ptr, sizeof(mip.gsr_source));

        if (setsockopt(sock->socketdes, ip_proto, type, (const void *) &mip,
                       sizeof(mip)) == -1) {
            rv = errno;
        }
#else
        /* We do not support Source-Specific Multicast. */
        return APR_ENOTIMPL;
#endif
    }
    else {
        if (sock_is_ipv4(sock)) {

            fill_mip_v4(&mip4, mcast, iface);

            if (setsockopt(sock->socketdes, IPPROTO_IP, type,
                           (const void *) &mip4, sizeof(mip4)) == -1) {
                rv = errno;
            }
        }
#if APR_HAVE_IPV6 && defined(IPV6_JOIN_GROUP) && defined(IPV6_LEAVE_GROUP)
        else if (sock_is_ipv6(sock)) {
            if (type == IP_ADD_MEMBERSHIP) {
                type = IPV6_JOIN_GROUP;
            }
            else if (type == IP_DROP_MEMBERSHIP) {
                type = IPV6_LEAVE_GROUP;
            }
            else {
                return APR_ENOTIMPL;
            }

            fill_mip_v6(&mip6, mcast, iface);

            if (setsockopt(sock->socketdes, IPPROTO_IPV6, type,
                           (const void *) &mip6, sizeof(mip6)) == -1) {
                rv = errno;
            }
        }
#endif
        else {
            rv = APR_ENOTIMPL;
        }
    }
    return rv;
}

/* Set the IP_MULTICAST_TTL or IP_MULTICAST_LOOP option, or IPv6
 * equivalents, for the socket, to the given value.  Note that this
 * function *only works* for those particular option types. */
static apr_status_t do_mcast_opt(int type, apr_socket_t *sock,
                                 apr_byte_t value)
{
    apr_status_t rv = APR_SUCCESS;

    if (sock_is_ipv4(sock)) {
        /* For the IP_MULTICAST_* options, this must be a (char *)
         * pointer. */
        if (setsockopt(sock->socketdes, IPPROTO_IP, type,
                       (const void *) &value, sizeof(value)) == -1) {
            rv = errno;
        }
    }
#if APR_HAVE_IPV6
    else if (sock_is_ipv6(sock)) {
        /* For the IPV6_* options, an (int *) pointer must be used. */
        int ivalue = value;

        if (type == IP_MULTICAST_TTL) {
            type = IPV6_MULTICAST_HOPS;
        }
        else if (type == IP_MULTICAST_LOOP) {
            type = IPV6_MULTICAST_LOOP;
        }
        else {
            return APR_ENOTIMPL;
        }

        if (setsockopt(sock->socketdes, IPPROTO_IPV6, type,
                       (const void *) &ivalue, sizeof(ivalue)) == -1) {
            rv = errno;
        }
    }
#endif
    else {
        rv = APR_ENOTIMPL;
    }

    return rv;
}
#endif

APR_DECLARE(apr_status_t) apr_mcast_join(apr_socket_t *sock,
                                         apr_sockaddr_t *join,
                                         apr_sockaddr_t *iface,
                                         apr_sockaddr_t *source)
{
#if defined(IP_ADD_MEMBERSHIP) && defined(HAVE_STRUCT_IPMREQ)
    return do_mcast(IP_ADD_MEMBERSHIP, sock, join, iface, source);
#else
    return APR_ENOTIMPL;
#endif
}

APR_DECLARE(apr_status_t) apr_mcast_leave(apr_socket_t *sock,
                                          apr_sockaddr_t *addr,
                                          apr_sockaddr_t *iface,
                                          apr_sockaddr_t *source)
{
#if defined(IP_DROP_MEMBERSHIP) && defined(HAVE_STRUCT_IPMREQ)
    return do_mcast(IP_DROP_MEMBERSHIP, sock, addr, iface, source);
#else
    return APR_ENOTIMPL;
#endif
}

APR_DECLARE(apr_status_t) apr_mcast_hops(apr_socket_t *sock, apr_byte_t ttl)
{
#if defined(IP_MULTICAST_TTL) && defined(HAVE_STRUCT_IPMREQ)
    return do_mcast_opt(IP_MULTICAST_TTL, sock, ttl);
#else
    return APR_ENOTIMPL;
#endif
}

APR_DECLARE(apr_status_t) apr_mcast_loopback(apr_socket_t *sock,
                                             apr_byte_t opt)
{
#if defined(IP_MULTICAST_LOOP) && defined(HAVE_STRUCT_IPMREQ)
    return do_mcast_opt(IP_MULTICAST_LOOP, sock, opt);
#else
    return APR_ENOTIMPL;
#endif
}

APR_DECLARE(apr_status_t) apr_mcast_interface(apr_socket_t *sock,
                                              apr_sockaddr_t *iface)
{
#if defined(IP_MULTICAST_IF) && defined(HAVE_STRUCT_IPMREQ)
    apr_status_t rv = APR_SUCCESS;

    if (sock_is_ipv4(sock)) {
        if (setsockopt(sock->socketdes, IPPROTO_IP, IP_MULTICAST_IF,
                       (const void *) &iface->sa.sin.sin_addr,
                       sizeof(iface->sa.sin.sin_addr)) == -1) {
            rv = errno;
        }
    }
#if APR_HAVE_IPV6
    else if (sock_is_ipv6(sock)) {
        unsigned int idx = find_if_index(iface);
        if (setsockopt(sock->socketdes, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                       (const void *) &idx, sizeof(idx)) == -1) {
            rv = errno;
        }
    }
#endif
    else {
        rv = APR_ENOTIMPL;
    }
    return rv;
#else
    return APR_ENOTIMPL;
#endif
}
