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
#include "apr_strings.h"


static apr_status_t soblock(int sd)
{
/* BeOS uses setsockopt at present for non blocking... */
#ifndef BEOS
    int fd_flags;

    fd_flags = fcntl(sd, F_GETFL, 0);
#if defined(O_NONBLOCK)
    fd_flags &= ~O_NONBLOCK;
#elif defined(O_NDELAY)
    fd_flags &= ~O_NDELAY;
#elif defined(FNDELAY)
    fd_flags &= ~FNDELAY;
#else
#error Please teach APR how to make sockets blocking on your platform.
#endif
    if (fcntl(sd, F_SETFL, fd_flags) == -1) {
        return errno;
    }
#else
    int on = 0;
    if (setsockopt(sd, SOL_SOCKET, SO_NONBLOCK, &on, sizeof(int)) < 0)
        return errno;
#endif /* BEOS */
    return APR_SUCCESS;
}

static apr_status_t sononblock(int sd)
{
#ifndef BEOS
    int fd_flags;

    fd_flags = fcntl(sd, F_GETFL, 0);
#if defined(O_NONBLOCK)
    fd_flags |= O_NONBLOCK;
#elif defined(O_NDELAY)
    fd_flags |= O_NDELAY;
#elif defined(FNDELAY)
    fd_flags |= FNDELAY;
#else
#error Please teach APR how to make sockets non-blocking on your platform.
#endif
    if (fcntl(sd, F_SETFL, fd_flags) == -1) {
        return errno;
    }
#else
    int on = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_NONBLOCK, &on, sizeof(int)) < 0)
        return errno;
#endif /* BEOS */
    return APR_SUCCESS;
}


apr_status_t apr_socket_timeout_set(apr_socket_t *sock, apr_interval_time_t t)
{
    apr_status_t stat;

    /* If our new timeout is non-negative and our old timeout was
     * negative, then we need to ensure that we are non-blocking.
     * Conversely, if our new timeout is negative and we had
     * non-negative timeout, we must make sure our socket is blocking.
     * We want to avoid calling fcntl more than necessary on the
     * socket.
     */
    if (t >= 0 && sock->timeout < 0) {
        if (apr_is_option_set(sock, APR_SO_NONBLOCK) != 1) {
            if ((stat = sononblock(sock->socketdes)) != APR_SUCCESS) {
                return stat;
            }
            apr_set_option(sock, APR_SO_NONBLOCK, 1);
        }
    } 
    else if (t < 0 && sock->timeout >= 0) {
        if (apr_is_option_set(sock, APR_SO_NONBLOCK) != 0) { 
            if ((stat = soblock(sock->socketdes)) != APR_SUCCESS) { 
                return stat; 
            }
            apr_set_option(sock, APR_SO_NONBLOCK, 0);
        } 
    }
    /* must disable the incomplete read support if we disable
     * a timeout
     */
    if (t <= 0) {
        sock->options &= ~APR_INCOMPLETE_READ;
    }
    sock->timeout = t;
    return APR_SUCCESS;
}


apr_status_t apr_socket_opt_set(apr_socket_t *sock, 
                                apr_int32_t opt, apr_int32_t on)
{
    int one;
    apr_status_t rv;

    if (on)
        one = 1;
    else
        one = 0;
    switch(opt) {
    case APR_SO_KEEPALIVE:
#ifdef SO_KEEPALIVE
        if (on != apr_is_option_set(sock, APR_SO_KEEPALIVE)) {
            if (setsockopt(sock->socketdes, SOL_SOCKET, SO_KEEPALIVE, (void *)&one, sizeof(int)) == -1) {
                return errno;
            }
            apr_set_option(sock, APR_SO_KEEPALIVE, on);
        }
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_SO_DEBUG:
        if (on != apr_is_option_set(sock, APR_SO_DEBUG)) {
            if (setsockopt(sock->socketdes, SOL_SOCKET, SO_DEBUG, (void *)&one, sizeof(int)) == -1) {
                return errno;
            }
            apr_set_option(sock, APR_SO_DEBUG, on);
        }
        break;
    case APR_SO_BROADCAST:
#ifdef SO_BROADCAST
        if (on != apr_is_option_set(sock, APR_SO_BROADCAST)) {
            if (setsockopt(sock->socketdes, SOL_SOCKET, SO_BROADCAST, (void *)&one, sizeof(int)) == -1) {
                return errno;
            }
            apr_set_option(sock, APR_SO_BROADCAST, on);
        }
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_SO_REUSEADDR:
        if (on != apr_is_option_set(sock, APR_SO_REUSEADDR)) {
            if (setsockopt(sock->socketdes, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(int)) == -1) {
                return errno;
            }
            apr_set_option(sock, APR_SO_REUSEADDR, on);
        }
        break;
    case APR_SO_SNDBUF:
#ifdef SO_SNDBUF
        if (setsockopt(sock->socketdes, SOL_SOCKET, SO_SNDBUF, (void *)&on, sizeof(int)) == -1) {
            return errno;
        }
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_SO_RCVBUF:
#ifdef SO_RCVBUF
        if (setsockopt(sock->socketdes, SOL_SOCKET, SO_RCVBUF, (void *)&on, sizeof(int)) == -1) {
            return errno;
        }
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_SO_NONBLOCK:
        if (apr_is_option_set(sock, APR_SO_NONBLOCK) != on) {
            if (on) {
                if ((rv = sononblock(sock->socketdes)) != APR_SUCCESS) 
                    return rv;
            }
            else {
                if ((rv = soblock(sock->socketdes)) != APR_SUCCESS)
                    return rv;
            }
            apr_set_option(sock, APR_SO_NONBLOCK, on);
        }
        break;
    case APR_SO_LINGER:
#ifdef SO_LINGER
        if (apr_is_option_set(sock, APR_SO_LINGER) != on) {
            struct linger li;
            li.l_onoff = on;
            li.l_linger = APR_MAX_SECS_TO_LINGER;
            if (setsockopt(sock->socketdes, SOL_SOCKET, SO_LINGER, (char *) &li, sizeof(struct linger)) == -1) {
                return errno;
            }
            apr_set_option(sock, APR_SO_LINGER, on);
        }
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_TCP_DEFER_ACCEPT:
#if defined(TCP_DEFER_ACCEPT)
        if (apr_is_option_set(sock, APR_TCP_DEFER_ACCEPT) != on) {
            int optlevel = IPPROTO_TCP;
            int optname = TCP_DEFER_ACCEPT;

            if (setsockopt(sock->socketdes, optlevel, optname, 
                           (void *)&on, sizeof(int)) == -1) {
                return errno;
            }
            apr_set_option(sock, APR_TCP_DEFER_ACCEPT, on);
        }
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_TCP_NODELAY:
#if defined(TCP_NODELAY)
        if (apr_is_option_set(sock, APR_TCP_NODELAY) != on) {
            int optlevel = IPPROTO_TCP;
            int optname = TCP_NODELAY;

#if APR_HAVE_SCTP
            if (sock->protocol == IPPROTO_SCTP) {
                optlevel = IPPROTO_SCTP;
                optname = SCTP_NODELAY;
            }
#endif
            if (setsockopt(sock->socketdes, optlevel, optname, (void *)&on, sizeof(int)) == -1) {
                return errno;
            }
            apr_set_option(sock, APR_TCP_NODELAY, on);
        }
#else
        /* BeOS pre-BONE has TCP_NODELAY set by default.
         * As it can't be turned off we might as well check if they're asking
         * for it to be turned on!
         */
#ifdef BEOS
        if (on == 1)
            return APR_SUCCESS;
        else
#endif
        return APR_ENOTIMPL;
#endif
        break;
    case APR_TCP_NOPUSH:
#if APR_TCP_NOPUSH_FLAG
        /* TCP_NODELAY and TCP_CORK are mutually exclusive on Linux
         * kernels < 2.6; on newer kernels they can be used together
         * and TCP_CORK takes preference, which is the desired
         * behaviour.  On older kernels, TCP_NODELAY must be toggled
         * to "off" whilst TCP_CORK is in effect. */
        if (apr_is_option_set(sock, APR_TCP_NOPUSH) != on) {
#ifndef HAVE_TCP_NODELAY_WITH_CORK
            int optlevel = IPPROTO_TCP;
            int optname = TCP_NODELAY;

#if APR_HAVE_SCTP
            if (sock->protocol == IPPROTO_SCTP) {
                optlevel = IPPROTO_SCTP;
                optname = SCTP_NODELAY;
            }
#endif
            /* OK we're going to change some settings here... */
            if (apr_is_option_set(sock, APR_TCP_NODELAY) == 1 && on) {
                /* Now toggle TCP_NODELAY to off, if TCP_CORK is being
                 * turned on: */
                int tmpflag = 0;
                if (setsockopt(sock->socketdes, optlevel, optname,
                               (void*)&tmpflag, sizeof(int)) == -1) {
                    return errno;
                }
                apr_set_option(sock, APR_RESET_NODELAY, 1);
                apr_set_option(sock, APR_TCP_NODELAY, 0);
            } else if (on) {
                apr_set_option(sock, APR_RESET_NODELAY, 0);
            }
#endif /* HAVE_TCP_NODELAY_WITH_CORK */

            /* OK, now we can just set the TCP_NOPUSH flag accordingly...*/
            if (setsockopt(sock->socketdes, IPPROTO_TCP, APR_TCP_NOPUSH_FLAG,
                           (void*)&on, sizeof(int)) == -1) {
                return errno;
            }
            apr_set_option(sock, APR_TCP_NOPUSH, on);
#ifndef HAVE_TCP_NODELAY_WITH_CORK
            if (!on && apr_is_option_set(sock, APR_RESET_NODELAY)) {
                /* Now, if TCP_CORK was just turned off, turn
                 * TCP_NODELAY back on again if it was earlier toggled
                 * to off: */
                int tmpflag = 1;
                if (setsockopt(sock->socketdes, optlevel, optname,
                               (void*)&tmpflag, sizeof(int)) == -1) {
                    return errno;
                }
                apr_set_option(sock, APR_RESET_NODELAY,0);
                apr_set_option(sock, APR_TCP_NODELAY, 1);
            }
#endif /* HAVE_TCP_NODELAY_WITH_CORK */
        }
#else
        return APR_ENOTIMPL;
#endif
        break;
    case APR_INCOMPLETE_READ:
        apr_set_option(sock, APR_INCOMPLETE_READ, on);
        break;
    case APR_IPV6_V6ONLY:
#if APR_HAVE_IPV6 && defined(IPV6_V6ONLY)
        /* we don't know the initial setting of this option,
         * so don't check sock->options since that optimization
         * won't work
         */
        if (setsockopt(sock->socketdes, IPPROTO_IPV6, IPV6_V6ONLY,
                       (void *)&on, sizeof(int)) == -1) {
            return errno;
        }
        apr_set_option(sock, APR_IPV6_V6ONLY, on);
#else
        return APR_ENOTIMPL;
#endif
        break;
    default:
        return APR_EINVAL;
    }

    return APR_SUCCESS; 
}         


apr_status_t apr_socket_timeout_get(apr_socket_t *sock, apr_interval_time_t *t)
{
    *t = sock->timeout;
    return APR_SUCCESS;
}


apr_status_t apr_socket_opt_get(apr_socket_t *sock, 
                                apr_int32_t opt, apr_int32_t *on)
{
    switch(opt) {
        default:
            *on = apr_is_option_set(sock, opt);
    }
    return APR_SUCCESS;
}


apr_status_t apr_socket_atmark(apr_socket_t *sock, int *atmark)
{
#ifndef BEOS_R5
    int oobmark;

    if (ioctl(sock->socketdes, SIOCATMARK, (void*) &oobmark) < 0)
        return apr_get_netos_error();

    *atmark = (oobmark != 0);

    return APR_SUCCESS;
#else /* BEOS_R5 */
    return APR_ENOTIMPL;
#endif
}

apr_status_t apr_gethostname(char *buf, apr_int32_t len, apr_pool_t *cont)
{
#ifdef BEOS_R5
    if (gethostname(buf, len) == 0) {
#else
    if (gethostname(buf, len) != 0) {
#endif  
        buf[0] = '\0';
        return errno;
    }
    else if (!memchr(buf, '\0', len)) { /* buffer too small */
        /* note... most platforms just truncate in this condition
         *         linux+glibc return an error
         */
        buf[0] = '\0';
        return APR_ENAMETOOLONG;
    }
    return APR_SUCCESS;
}

#if APR_HAS_SO_ACCEPTFILTER
apr_status_t apr_socket_accept_filter(apr_socket_t *sock, char *nonconst_name, 
                                      char *nonconst_args)
{
    /* these should have been const; act like they are */
    const char *name = nonconst_name;
    const char *args = nonconst_args;

    struct accept_filter_arg af;
    socklen_t optlen = sizeof(af);

    /* FreeBSD returns an error if the filter is already set; ignore
     * this call if we previously set it to the same value.
     */
    if ((getsockopt(sock->socketdes, SOL_SOCKET, SO_ACCEPTFILTER,
                    &af, &optlen)) == 0) {
        if (!strcmp(name, af.af_name) && !strcmp(args, af.af_arg)) {
            return APR_SUCCESS;
        }
    }

    /* Uhh, at least in FreeBSD 9 the fields are declared as arrays of
     * these lengths; did sizeof not work in some ancient release?
     *
     * FreeBSD kernel sets the last byte to a '\0'.
     */
    apr_cpystrn(af.af_name, name, 16);
    apr_cpystrn(af.af_arg, args, 256 - 16);

    if ((setsockopt(sock->socketdes, SOL_SOCKET, SO_ACCEPTFILTER,
          &af, sizeof(af))) < 0) {
        return errno;
    }
    return APR_SUCCESS;
}
#endif
