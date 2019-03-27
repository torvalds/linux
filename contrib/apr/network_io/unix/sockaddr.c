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
#include "apr.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_private.h"

#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif

#define APR_WANT_STRFUNC
#include "apr_want.h"

struct apr_ipsubnet_t {
    int family;
#if APR_HAVE_IPV6
    apr_uint32_t sub[4]; /* big enough for IPv4 and IPv6 addresses */
    apr_uint32_t mask[4];
#else
    apr_uint32_t sub[1];
    apr_uint32_t mask[1];
#endif
};

#if !defined(NETWARE) && !defined(WIN32)
#ifdef HAVE_SET_H_ERRNO
#define SET_H_ERRNO(newval) set_h_errno(newval)
#else
#define SET_H_ERRNO(newval) h_errno = (newval)
#endif
#else
#define SET_H_ERRNO(newval)
#endif

#if APR_HAS_THREADS && !defined(GETHOSTBYNAME_IS_THREAD_SAFE) && \
    defined(HAVE_GETHOSTBYNAME_R)
/* This is the maximum size that may be returned from the reentrant
 * gethostbyname_r function.  If the system tries to use more, it
 * should return ERANGE.
 */
#define GETHOSTBYNAME_BUFLEN 512
#endif

#ifdef _AIX
/* Some levels of AIX getaddrinfo() don't like servname = "0", so
 * set servname to "1" when port is 0 and fix it up later.
 */
#define AIX_SERVNAME_HACK 1
#else
#define AIX_SERVNAME_HACK 0
#endif

#ifdef _WIN32_WCE
/* XXX: BS solution.  Need an HAVE_GETSERVBYNAME and actually
 * do something here, to provide the obvious proto mappings.
 */
static void *getservbyname(const char *name, const char *proto)
{
    return NULL;
}
#endif

static apr_status_t get_local_addr(apr_socket_t *sock)
{
    sock->local_addr->salen = sizeof(sock->local_addr->sa);
    if (getsockname(sock->socketdes, (struct sockaddr *)&sock->local_addr->sa,
                    &sock->local_addr->salen) < 0) {
        return apr_get_netos_error();
    }
    else {
        sock->local_port_unknown = sock->local_interface_unknown = 0;
        /* XXX assumes sin_port and sin6_port at same offset */
        sock->local_addr->port = ntohs(sock->local_addr->sa.sin.sin_port);
        return APR_SUCCESS;
    }
}

static apr_status_t get_remote_addr(apr_socket_t *sock)
{
    sock->remote_addr->salen = sizeof(sock->remote_addr->sa);
    if (getpeername(sock->socketdes, (struct sockaddr *)&sock->remote_addr->sa,
                    &sock->remote_addr->salen) < 0) {
        return apr_get_netos_error();
    }
    else {
        sock->remote_addr_unknown = 0;
        /* XXX assumes sin_port and sin6_port at same offset */
        sock->remote_addr->port = ntohs(sock->remote_addr->sa.sin.sin_port);
        return APR_SUCCESS;
    }
}

APR_DECLARE(apr_status_t) apr_sockaddr_ip_getbuf(char *buf, apr_size_t buflen,
                                                 apr_sockaddr_t *sockaddr)
{
    if (!apr_inet_ntop(sockaddr->family, sockaddr->ipaddr_ptr, buf, buflen)) {
        return APR_ENOSPC;
    }

#if APR_HAVE_IPV6
    if (sockaddr->family == AF_INET6 
        && IN6_IS_ADDR_V4MAPPED((struct in6_addr *)sockaddr->ipaddr_ptr)
        && buflen > strlen("::ffff:")) {
        /* This is an IPv4-mapped IPv6 address; drop the leading
         * part of the address string so we're left with the familiar
         * IPv4 format.
         */
        memmove(buf, buf + strlen("::ffff:"),
                strlen(buf + strlen("::ffff:"))+1);
    }
#endif
    /* ensure NUL termination if the buffer is too short */
    buf[buflen-1] = '\0';
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_sockaddr_ip_get(char **addr,
                                              apr_sockaddr_t *sockaddr)
{
    *addr = apr_palloc(sockaddr->pool, sockaddr->addr_str_len);
    return apr_sockaddr_ip_getbuf(*addr, sockaddr->addr_str_len, sockaddr);
}

void apr_sockaddr_vars_set(apr_sockaddr_t *addr, int family, apr_port_t port)
{
    addr->family = family;
    addr->sa.sin.sin_family = family;
    if (port) {
        /* XXX IPv6: assumes sin_port and sin6_port at same offset */
        addr->sa.sin.sin_port = htons(port);
        addr->port = port;
    }
#if AIX_SERVNAME_HACK
    else {
        addr->sa.sin.sin_port = htons(port);
    }
#endif

    if (family == APR_INET) {
        addr->salen = sizeof(struct sockaddr_in);
        addr->addr_str_len = 16;
        addr->ipaddr_ptr = &(addr->sa.sin.sin_addr);
        addr->ipaddr_len = sizeof(struct in_addr);
    }
#if APR_HAVE_IPV6
    else if (family == APR_INET6) {
        addr->salen = sizeof(struct sockaddr_in6);
        addr->addr_str_len = 46;
        addr->ipaddr_ptr = &(addr->sa.sin6.sin6_addr);
        addr->ipaddr_len = sizeof(struct in6_addr);
    }
#endif
}

APR_DECLARE(apr_status_t) apr_socket_addr_get(apr_sockaddr_t **sa,
                                           apr_interface_e which,
                                           apr_socket_t *sock)
{
    if (which == APR_LOCAL) {
        if (sock->local_interface_unknown || sock->local_port_unknown) {
            apr_status_t rv = get_local_addr(sock);

            if (rv != APR_SUCCESS) {
                return rv;
            }
        }
        *sa = sock->local_addr;
    }
    else if (which == APR_REMOTE) {
        if (sock->remote_addr_unknown) {
            apr_status_t rv = get_remote_addr(sock);

            if (rv != APR_SUCCESS) {
                return rv;
            }
        }
        *sa = sock->remote_addr;
    }
    else {
        *sa = NULL;
        return APR_EINVAL;
    }
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_parse_addr_port(char **addr,
                                              char **scope_id,
                                              apr_port_t *port,
                                              const char *str,
                                              apr_pool_t *p)
{
    const char *ch, *lastchar;
    int big_port;
    apr_size_t addrlen;

    *addr = NULL;         /* assume not specified */
    *scope_id = NULL;     /* assume not specified */
    *port = 0;            /* assume not specified */

    /* First handle the optional port number.  That may be all that
     * is specified in the string.
     */
    ch = lastchar = str + strlen(str) - 1;
    while (ch >= str && apr_isdigit(*ch)) {
        --ch;
    }

    if (ch < str) {       /* Entire string is the port. */
        big_port = atoi(str);
        if (big_port < 1 || big_port > 65535) {
            return APR_EINVAL;
        }
        *port = big_port;
        return APR_SUCCESS;
    }

    if (*ch == ':' && ch < lastchar) { /* host and port number specified */
        if (ch == str) {               /* string starts with ':' -- bad */
            return APR_EINVAL;
        }
        big_port = atoi(ch + 1);
        if (big_port < 1 || big_port > 65535) {
            return APR_EINVAL;
        }
        *port = big_port;
        lastchar = ch - 1;
    }

    /* now handle the hostname */
    addrlen = lastchar - str + 1;

/* XXX we don't really have to require APR_HAVE_IPV6 for this; 
 * just pass char[] for ipaddr (so we don't depend on struct in6_addr)
 * and always define APR_INET6 
 */
#if APR_HAVE_IPV6
    if (*str == '[') {
        const char *end_bracket = memchr(str, ']', addrlen);
        struct in6_addr ipaddr;
        const char *scope_delim;

        if (!end_bracket || end_bracket != lastchar) {
            *port = 0;
            return APR_EINVAL;
        }

        /* handle scope id; this is the only context where it is allowed */
        scope_delim = memchr(str, '%', addrlen);
        if (scope_delim) {
            if (scope_delim == end_bracket - 1) { /* '%' without scope id */
                *port = 0;
                return APR_EINVAL;
            }
            addrlen = scope_delim - str - 1;
            *scope_id = apr_palloc(p, end_bracket - scope_delim);
            memcpy(*scope_id, scope_delim + 1, end_bracket - scope_delim - 1);
            (*scope_id)[end_bracket - scope_delim - 1] = '\0';
        }
        else {
            addrlen = addrlen - 2; /* minus 2 for '[' and ']' */
        }

        *addr = apr_palloc(p, addrlen + 1);
        memcpy(*addr,
               str + 1,
               addrlen);
        (*addr)[addrlen] = '\0';
        if (apr_inet_pton(AF_INET6, *addr, &ipaddr) != 1) {
            *addr = NULL;
            *scope_id = NULL;
            *port = 0;
            return APR_EINVAL;
        }
    }
    else 
#endif
    {
        /* XXX If '%' is not a valid char in a DNS name, we *could* check 
         *     for bogus scope ids first.
         */
        *addr = apr_palloc(p, addrlen + 1);
        memcpy(*addr, str, addrlen);
        (*addr)[addrlen] = '\0';
    }
    return APR_SUCCESS;
}

#if defined(HAVE_GETADDRINFO)

static apr_status_t call_resolver(apr_sockaddr_t **sa,
                                  const char *hostname, apr_int32_t family,
                                  apr_port_t port, apr_int32_t flags, 
                                  apr_pool_t *p)
{
    struct addrinfo hints, *ai, *ai_list;
    apr_sockaddr_t *prev_sa;
    int error;
    char *servname = NULL; 

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
#ifdef HAVE_GAI_ADDRCONFIG
    if (family == APR_UNSPEC) {
        /* By default, only look up addresses using address types for
         * which a local interface is configured, i.e. no IPv6 if no
         * IPv6 interfaces configured. */
        hints.ai_flags = AI_ADDRCONFIG;
    }
#endif

#ifdef __MVS__
    /* z/OS will not return IPv4 address under AF_UNSPEC if any IPv6 results 
     * are returned, w/o AI_ALL. 
     */
    if (family == APR_UNSPEC) { 
       hints.ai_flags |= AI_ALL;
    }
#endif

    if(hostname == NULL) {
#ifdef AI_PASSIVE 
        /* If hostname is NULL, assume we are trying to bind to all
         * interfaces. */
        hints.ai_flags |= AI_PASSIVE;
#endif
        /* getaddrinfo according to RFC 2553 must have either hostname
         * or servname non-NULL.
         */
#ifdef OSF1
        /* The Tru64 5.0 getaddrinfo() can only resolve services given
         * by the name listed in /etc/services; a numeric or unknown
         * servname gets an EAI_SERVICE error.  So just resolve the
         * appropriate anyaddr and fill in the port later. */
        hostname = family == AF_INET6 ? "::" : "0.0.0.0";
        servname = NULL;
#ifdef AI_NUMERICHOST
        hints.ai_flags |= AI_NUMERICHOST;
#endif
#else
#if AIX_SERVNAME_HACK
        if (!port) {
            servname = "1";
        }
        else
#endif /* AIX_SERVNAME_HACK */
        servname = apr_itoa(p, port);
#endif /* OSF1 */
    }
    error = getaddrinfo(hostname, servname, &hints, &ai_list);
#ifdef HAVE_GAI_ADDRCONFIG
    /*
     * Using AI_ADDRCONFIG involves some unfortunate guesswork because it
     * does not consider loopback addresses when trying to determine if
     * IPv4 or IPv6 is configured on a system (see RFC 3493).
     * This is a problem if one actually wants to listen on or connect to
     * the loopback address of a protocol family that is not otherwise
     * configured on the system. See PR 52709.
     * To work around some of the problems, retry without AI_ADDRCONFIG
     * in case of EAI_ADDRFAMILY.
     * XXX: apr_sockaddr_info_get() should really accept a flag to determine
     * XXX: if AI_ADDRCONFIG's guesswork is wanted and if the address is
     * XXX: to be used for listen() or connect().
     *
     * In case of EAI_BADFLAGS, AI_ADDRCONFIG is not supported.
     */
    if ((family == APR_UNSPEC) && (error == EAI_BADFLAGS
#ifdef EAI_ADDRFAMILY
                                   || error == EAI_ADDRFAMILY
#endif
                                                             )) {
        hints.ai_flags &= ~AI_ADDRCONFIG;
        error = getaddrinfo(hostname, servname, &hints, &ai_list);
    }
#endif
    if (error) {
#if defined(WIN32)
        return apr_get_netos_error();
#else
        if (error == EAI_SYSTEM) {
            return errno ? errno : APR_EGENERAL;
        }
        else 
        {
            /* issues with representing this with APR's error scheme:
             * glibc uses negative values for these numbers, perhaps so 
             * they don't conflict with h_errno values...  Tru64 uses 
             * positive values which conflict with h_errno values
             */
#if defined(NEGATIVE_EAI)
            error = -error;
#endif
            return error + APR_OS_START_EAIERR;
        }
#endif /* WIN32 */
    }

    prev_sa = NULL;
    ai = ai_list;
    while (ai) { /* while more addresses to report */
        apr_sockaddr_t *new_sa;

        /* Ignore anything bogus: getaddrinfo in some old versions of
         * glibc will return AF_UNIX entries for APR_UNSPEC+AI_PASSIVE
         * lookups. */
#if APR_HAVE_IPV6
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
#else
        if (ai->ai_family != AF_INET) {
#endif
            ai = ai->ai_next;
            continue;
        }

        new_sa = apr_pcalloc(p, sizeof(apr_sockaddr_t));

        new_sa->pool = p;
        memcpy(&new_sa->sa, ai->ai_addr, ai->ai_addrlen);
        apr_sockaddr_vars_set(new_sa, ai->ai_family, port);

        if (!prev_sa) { /* first element in new list */
            if (hostname) {
                new_sa->hostname = apr_pstrdup(p, hostname);
            }
            *sa = new_sa;
        }
        else {
            new_sa->hostname = prev_sa->hostname;
            prev_sa->next = new_sa;
        }

        prev_sa = new_sa;
        ai = ai->ai_next;
    }
    freeaddrinfo(ai_list);

    if (prev_sa == NULL) {
        /*
         * getaddrinfo returned only useless entries and *sa is still empty.
         * This should be treated as an error.
         */
        return APR_EGENERAL;
    }

    return APR_SUCCESS;
}

static apr_status_t find_addresses(apr_sockaddr_t **sa, 
                                   const char *hostname, apr_int32_t family,
                                   apr_port_t port, apr_int32_t flags, 
                                   apr_pool_t *p)
{
    if (flags & APR_IPV4_ADDR_OK) {
        apr_status_t error = call_resolver(sa, hostname, AF_INET, port, flags, p);

#if APR_HAVE_IPV6
        if (error) {
            family = AF_INET6; /* try again */
        }
        else
#endif
        return error;
    }
#if APR_HAVE_IPV6
    else if (flags & APR_IPV6_ADDR_OK) {
        apr_status_t error = call_resolver(sa, hostname, AF_INET6, port, flags, p);

        if (error) {
            family = AF_INET; /* try again */
        }
        else {
            return APR_SUCCESS;
        }
    }
#endif

    return call_resolver(sa, hostname, family, port, flags, p);
}

#else /* end of HAVE_GETADDRINFO code */

static apr_status_t find_addresses(apr_sockaddr_t **sa, 
                                   const char *hostname, apr_int32_t family,
                                   apr_port_t port, apr_int32_t flags, 
                                   apr_pool_t *p)
{
    struct hostent *hp;
    apr_sockaddr_t *prev_sa;
    int curaddr;
#if APR_HAS_THREADS && !defined(GETHOSTBYNAME_IS_THREAD_SAFE) && \
    defined(HAVE_GETHOSTBYNAME_R) && !defined(BEOS)
#ifdef GETHOSTBYNAME_R_HOSTENT_DATA
    struct hostent_data hd;
#else
    /* If you see ERANGE, that means GETHOSBYNAME_BUFLEN needs to be
     * bumped. */
    char tmp[GETHOSTBYNAME_BUFLEN];
#endif
    int hosterror;
#endif
    struct hostent hs;
    struct in_addr ipaddr;
    char *addr_list[2];
    const char *orig_hostname = hostname;

    if (hostname == NULL) {
        /* if we are given a NULL hostname, assume '0.0.0.0' */
        hostname = "0.0.0.0";
    }

    if (*hostname >= '0' && *hostname <= '9' &&
        strspn(hostname, "0123456789.") == strlen(hostname)) {

        ipaddr.s_addr = inet_addr(hostname);
        addr_list[0] = (char *)&ipaddr;
        addr_list[1] = NULL; /* just one IP in list */
        hs.h_addr_list = (char **)addr_list;
        hp = &hs;
    }
    else {
#if APR_HAS_THREADS && !defined(GETHOSTBYNAME_IS_THREAD_SAFE) && \
    defined(HAVE_GETHOSTBYNAME_R) && !defined(BEOS)
#if defined(GETHOSTBYNAME_R_HOSTENT_DATA)
        /* AIX, HP/UX, D/UX et alia */
        gethostbyname_r(hostname, &hs, &hd);
        hp = &hs;
#else
#if defined(GETHOSTBYNAME_R_GLIBC2)
        /* Linux glibc2+ */
        gethostbyname_r(hostname, &hs, tmp, GETHOSTBYNAME_BUFLEN - 1, 
                        &hp, &hosterror);
#else
        /* Solaris, Irix et alia */
        hp = gethostbyname_r(hostname, &hs, tmp, GETHOSTBYNAME_BUFLEN - 1,
                             &hosterror);
#endif /* !defined(GETHOSTBYNAME_R_GLIBC2) */
        if (!hp) {
            return (hosterror + APR_OS_START_SYSERR);
        }
#endif /* !defined(GETHOSTBYNAME_R_HOSTENT_DATA) */
#else
        hp = gethostbyname(hostname);
#endif

        if (!hp) {
#ifdef WIN32
            return apr_get_netos_error();
#else
            return (h_errno + APR_OS_START_SYSERR);
#endif
        }
    }

    prev_sa = NULL;
    curaddr = 0;
    while (hp->h_addr_list[curaddr]) {
        apr_sockaddr_t *new_sa = apr_pcalloc(p, sizeof(apr_sockaddr_t));

        new_sa->pool = p;
        new_sa->sa.sin.sin_addr = *(struct in_addr *)hp->h_addr_list[curaddr];
        apr_sockaddr_vars_set(new_sa, AF_INET, port);

        if (!prev_sa) { /* first element in new list */
            if (orig_hostname) {
                new_sa->hostname = apr_pstrdup(p, orig_hostname);
            }
            *sa = new_sa;
        }
        else {
            new_sa->hostname = prev_sa->hostname;
            prev_sa->next = new_sa;
        }

        prev_sa = new_sa;
        ++curaddr;
    }

    if (prev_sa == NULL) {
        /* this should not happen but no result should be treated as error */
        return APR_EGENERAL;
    }

    return APR_SUCCESS;
}

#endif /* end of !HAVE_GETADDRINFO code */

APR_DECLARE(apr_status_t) apr_sockaddr_info_get(apr_sockaddr_t **sa,
                                                const char *hostname, 
                                                apr_int32_t family, apr_port_t port,
                                                apr_int32_t flags, apr_pool_t *p)
{
    apr_int32_t masked;
    *sa = NULL;

    if ((masked = flags & (APR_IPV4_ADDR_OK | APR_IPV6_ADDR_OK))) {
        if (!hostname ||
            family != APR_UNSPEC ||
            masked == (APR_IPV4_ADDR_OK | APR_IPV6_ADDR_OK)) {
            return APR_EINVAL;
        }
#if !APR_HAVE_IPV6
        if (flags & APR_IPV6_ADDR_OK) {
            return APR_ENOTIMPL;
        }
#endif
    }
#if !APR_HAVE_IPV6
    /* What may happen is that APR is not IPv6-enabled, but we're still
     * going to call getaddrinfo(), so we have to tell the OS we only
     * want IPv4 addresses back since we won't know what to do with
     * IPv6 addresses.
     */
    if (family == APR_UNSPEC) {
        family = APR_INET;
    }
#endif

    return find_addresses(sa, hostname, family, port, flags, p);
}

APR_DECLARE(apr_status_t) apr_getnameinfo(char **hostname,
                                          apr_sockaddr_t *sockaddr,
                                          apr_int32_t flags)
{
#if defined(HAVE_GETNAMEINFO)
    int rc;
#if defined(NI_MAXHOST)
    char tmphostname[NI_MAXHOST];
#else
    char tmphostname[256];
#endif

    /* don't know if it is portable for getnameinfo() to set h_errno;
     * clear it then see if it was set */
    SET_H_ERRNO(0);

    /* default flags are NI_NAMREQD; otherwise, getnameinfo() will return
     * a numeric address string if it fails to resolve the host name;
     * that is *not* what we want here
     *
     * For IPv4-mapped IPv6 addresses, drop down to IPv4 before calling
     * getnameinfo() to avoid getnameinfo bugs (MacOS X, glibc).
     */
#if APR_HAVE_IPV6
    if (sockaddr->family == AF_INET6 &&
        IN6_IS_ADDR_V4MAPPED(&sockaddr->sa.sin6.sin6_addr)) {
        struct sockaddr_in tmpsa;
        tmpsa.sin_family = AF_INET;
        tmpsa.sin_port = 0;
        tmpsa.sin_addr.s_addr = ((apr_uint32_t *)sockaddr->ipaddr_ptr)[3];
#ifdef SIN6_LEN
        tmpsa.sin_len = sizeof(tmpsa);
#endif

        rc = getnameinfo((const struct sockaddr *)&tmpsa, sizeof(tmpsa),
                         tmphostname, sizeof(tmphostname), NULL, 0,
                         flags != 0 ? flags : NI_NAMEREQD);
    }
    else
#endif
    rc = getnameinfo((const struct sockaddr *)&sockaddr->sa, sockaddr->salen,
                     tmphostname, sizeof(tmphostname), NULL, 0,
                     flags != 0 ? flags : NI_NAMEREQD);
    if (rc != 0) {
        *hostname = NULL;

#ifndef WIN32
        /* something went wrong. Look at the EAI_ error code */
        if (rc == EAI_SYSTEM) {
            /* EAI_SYSTEM      System error returned in errno. */
            /* IMHO, Implementations that set h_errno a simply broken. */
            if (h_errno) { /* for broken implementations which set h_errno */
                return h_errno + APR_OS_START_SYSERR;
            }
            else { /* "normal" case */
                return errno + APR_OS_START_SYSERR;
            }
        }
        else 
#endif
        {
#if defined(NEGATIVE_EAI)
            if (rc < 0) rc = -rc;
#endif
            return rc + APR_OS_START_EAIERR; /* return the EAI_ error */
        }
    }
    *hostname = sockaddr->hostname = apr_pstrdup(sockaddr->pool, 
                                                 tmphostname);
    return APR_SUCCESS;
#else
#if APR_HAS_THREADS && !defined(GETHOSTBYADDR_IS_THREAD_SAFE) && \
    defined(HAVE_GETHOSTBYADDR_R) && !defined(BEOS)
#ifdef GETHOSTBYNAME_R_HOSTENT_DATA
    struct hostent_data hd;
#else
    char tmp[GETHOSTBYNAME_BUFLEN];
#endif
    int hosterror;
    struct hostent hs, *hptr;

#if defined(GETHOSTBYNAME_R_HOSTENT_DATA)
    /* AIX, HP/UX, D/UX et alia */
    gethostbyaddr_r((char *)&sockaddr->sa.sin.sin_addr, 
                  sizeof(struct in_addr), AF_INET, &hs, &hd);
    hptr = &hs;
#else
#if defined(GETHOSTBYNAME_R_GLIBC2)
    /* Linux glibc2+ */
    gethostbyaddr_r((char *)&sockaddr->sa.sin.sin_addr, 
                    sizeof(struct in_addr), AF_INET,
                    &hs, tmp, GETHOSTBYNAME_BUFLEN - 1, &hptr, &hosterror);
#else
    /* Solaris, Irix et alia */
    hptr = gethostbyaddr_r((char *)&sockaddr->sa.sin.sin_addr, 
                           sizeof(struct in_addr), AF_INET,
                           &hs, tmp, GETHOSTBYNAME_BUFLEN, &hosterror);
#endif /* !defined(GETHOSTBYNAME_R_GLIBC2) */
    if (!hptr) {
        *hostname = NULL;
        return hosterror + APR_OS_START_SYSERR;
    }
#endif /* !defined(GETHOSTBYNAME_R_HOSTENT_DATA) */
#else
    struct hostent *hptr;
    hptr = gethostbyaddr((char *)&sockaddr->sa.sin.sin_addr, 
                         sizeof(struct in_addr), AF_INET);
#endif

    if (hptr) {
        *hostname = sockaddr->hostname = apr_pstrdup(sockaddr->pool, hptr->h_name);
        return APR_SUCCESS;
    }
    *hostname = NULL;
#if defined(WIN32)
    return apr_get_netos_error();
#elif defined(OS2)
    return h_errno;
#else
    return h_errno + APR_OS_START_SYSERR;
#endif
#endif
}

APR_DECLARE(apr_status_t) apr_getservbyname(apr_sockaddr_t *sockaddr,
                                            const char *servname)
{
#if APR_HAS_THREADS && !defined(GETSERVBYNAME_IS_THREAD_SAFE) && \
    defined(HAVE_GETSERVBYNAME_R) && \
    (defined(GETSERVBYNAME_R_GLIBC2) || defined(GETSERVBYNAME_R_SOLARIS) || \
     defined(GETSERVBYNAME_R_OSF1))
    struct servent se;
#if defined(GETSERVBYNAME_R_OSF1)
    struct servent_data sed;

    memset(&sed, 0, sizeof(sed)); /* must zero fill before use */
#else
#if defined(GETSERVBYNAME_R_GLIBC2)
    struct servent *res;
#endif
    char buf[1024];
#endif
#else
    struct servent *se;
#endif

    if (servname == NULL)
        return APR_EINVAL;

#if APR_HAS_THREADS && !defined(GETSERVBYNAME_IS_THREAD_SAFE) && \
    defined(HAVE_GETSERVBYNAME_R) && \
    (defined(GETSERVBYNAME_R_GLIBC2) || defined(GETSERVBYNAME_R_SOLARIS) || \
     defined(GETSERVBYNAME_R_OSF1))
#if defined(GETSERVBYNAME_R_GLIBC2)
    if (getservbyname_r(servname, NULL,
                        &se, buf, sizeof(buf), &res) == 0 && res != NULL) {
        sockaddr->port = ntohs(res->s_port);
        sockaddr->servname = apr_pstrdup(sockaddr->pool, servname);
        sockaddr->sa.sin.sin_port = res->s_port;
        return APR_SUCCESS;
    }
#elif defined(GETSERVBYNAME_R_SOLARIS)
    if (getservbyname_r(servname, NULL, &se, buf, sizeof(buf)) != NULL) {
        sockaddr->port = ntohs(se.s_port);
        sockaddr->servname = apr_pstrdup(sockaddr->pool, servname);
        sockaddr->sa.sin.sin_port = se.s_port;
        return APR_SUCCESS;
    }
#elif defined(GETSERVBYNAME_R_OSF1)
    if (getservbyname_r(servname, NULL, &se, &sed) == 0) {
        sockaddr->port = ntohs(se.s_port);
        sockaddr->servname = apr_pstrdup(sockaddr->pool, servname);
        sockaddr->sa.sin.sin_port = se.s_port;
        return APR_SUCCESS;
    }
#endif
#else
    if ((se = getservbyname(servname, NULL)) != NULL){
        sockaddr->port = ntohs(se->s_port);
        sockaddr->servname = apr_pstrdup(sockaddr->pool, servname);
        sockaddr->sa.sin.sin_port = se->s_port;
        return APR_SUCCESS;
    }
#endif
    return APR_ENOENT;
}

#define V4MAPPED_EQUAL(a,b)                                   \
((a)->sa.sin.sin_family == AF_INET &&                         \
 (b)->sa.sin.sin_family == AF_INET6 &&                        \
 IN6_IS_ADDR_V4MAPPED((struct in6_addr *)(b)->ipaddr_ptr) &&  \
 !memcmp((a)->ipaddr_ptr,                                     \
         &((struct in6_addr *)(b)->ipaddr_ptr)->s6_addr[12],  \
         (a)->ipaddr_len))

APR_DECLARE(int) apr_sockaddr_equal(const apr_sockaddr_t *addr1,
                                    const apr_sockaddr_t *addr2)
{
    if (addr1->ipaddr_len == addr2->ipaddr_len &&
        !memcmp(addr1->ipaddr_ptr, addr2->ipaddr_ptr, addr1->ipaddr_len)) {
        return 1;
    }
#if APR_HAVE_IPV6
    if (V4MAPPED_EQUAL(addr1, addr2)) {
        return 1;
    }
    if (V4MAPPED_EQUAL(addr2, addr1)) {
        return 1;
    }
#endif
    return 0; /* not equal */
}

APR_DECLARE(int) apr_sockaddr_is_wildcard(const apr_sockaddr_t *addr)
{
    static const char inaddr_any[
#if APR_HAVE_IPV6
        sizeof(struct in6_addr)
#else
        sizeof(struct in_addr)
#endif
    ] = {0};

    if (addr->ipaddr_ptr /* IP address initialized */
        && addr->ipaddr_len <= sizeof inaddr_any) { /* else bug elsewhere? */
        if (!memcmp(inaddr_any, addr->ipaddr_ptr, addr->ipaddr_len)) {
            return 1;
        }
#if APR_HAVE_IPV6
    if (addr->family == AF_INET6
        && IN6_IS_ADDR_V4MAPPED((struct in6_addr *)addr->ipaddr_ptr)) {
        struct in_addr *v4 = (struct in_addr *)&((apr_uint32_t *)addr->ipaddr_ptr)[3];

        if (!memcmp(inaddr_any, v4, sizeof *v4)) {
            return 1;
        }
    }
#endif
    }
    return 0;
}

static apr_status_t parse_network(apr_ipsubnet_t *ipsub, const char *network)
{
    /* legacy syntax for ip addrs: a.b.c. ==> a.b.c.0/24 for example */
    int shift;
    char *s, *t;
    int octet;
    char buf[sizeof "255.255.255.255"];

    if (strlen(network) < sizeof buf) {
        strcpy(buf, network);
    }
    else {
        return APR_EBADIP;
    }

    /* parse components */
    s = buf;
    ipsub->sub[0] = 0;
    ipsub->mask[0] = 0;
    shift = 24;
    while (*s) {
        t = s;
        if (!apr_isdigit(*t)) {
            return APR_EBADIP;
        }
        while (apr_isdigit(*t)) {
            ++t;
        }
        if (*t == '.') {
            *t++ = 0;
        }
        else if (*t) {
            return APR_EBADIP;
        }
        if (shift < 0) {
            return APR_EBADIP;
        }
        octet = atoi(s);
        if (octet < 0 || octet > 255) {
            return APR_EBADIP;
        }
        ipsub->sub[0] |= octet << shift;
        ipsub->mask[0] |= 0xFFUL << shift;
        s = t;
        shift -= 8;
    }
    ipsub->sub[0] = ntohl(ipsub->sub[0]);
    ipsub->mask[0] = ntohl(ipsub->mask[0]);
    ipsub->family = AF_INET;
    return APR_SUCCESS;
}

/* return values:
 * APR_EINVAL     not an IP address; caller should see if it is something else
 * APR_BADIP      IP address portion is is not valid
 * APR_BADMASK    mask portion is not valid
 */

static apr_status_t parse_ip(apr_ipsubnet_t *ipsub, const char *ipstr, int network_allowed)
{
    /* supported flavors of IP:
     *
     * . IPv6 numeric address string (e.g., "fe80::1")
     * 
     *   IMPORTANT: Don't store IPv4-mapped IPv6 address as an IPv6 address.
     *
     * . IPv4 numeric address string (e.g., "127.0.0.1")
     *
     * . IPv4 network string (e.g., "9.67")
     *
     *   IMPORTANT: This network form is only allowed if network_allowed is on.
     */
    int rc;

#if APR_HAVE_IPV6
    rc = apr_inet_pton(AF_INET6, ipstr, ipsub->sub);
    if (rc == 1) {
        if (IN6_IS_ADDR_V4MAPPED((struct in6_addr *)ipsub->sub)) {
            /* apr_ipsubnet_test() assumes that we don't create IPv4-mapped IPv6
             * addresses; this of course forces the user to specify IPv4 addresses
             * in a.b.c.d style instead of ::ffff:a.b.c.d style.
             */
            return APR_EBADIP;
        }
        ipsub->family = AF_INET6;
    }
    else
#endif
    {
        rc = apr_inet_pton(AF_INET, ipstr, ipsub->sub);
        if (rc == 1) {
            ipsub->family = AF_INET;
        }
    }
    if (rc != 1) {
        if (network_allowed) {
            return parse_network(ipsub, ipstr);
        }
        else {
            return APR_EBADIP;
        }
    }
    return APR_SUCCESS;
}

static int looks_like_ip(const char *ipstr)
{
    if (strchr(ipstr, ':')) {
        /* definitely not a hostname; assume it is intended to be an IPv6 address */
        return 1;
    }

    /* simple IPv4 address string check */
    while ((*ipstr == '.') || apr_isdigit(*ipstr))
        ipstr++;
    return (*ipstr == '\0');
}

static void fix_subnet(apr_ipsubnet_t *ipsub)
{
    /* in case caller specified more bits in network address than are
     * valid according to the mask, turn off the extra bits
     */
    int i;

    for (i = 0; i < sizeof ipsub->mask / sizeof(apr_int32_t); i++) {
        ipsub->sub[i] &= ipsub->mask[i];
    }
}

/* be sure not to store any IPv4 address as a v4-mapped IPv6 address */
APR_DECLARE(apr_status_t) apr_ipsubnet_create(apr_ipsubnet_t **ipsub, const char *ipstr, 
                                              const char *mask_or_numbits, apr_pool_t *p)
{
    apr_status_t rv;
    char *endptr;
    long bits, maxbits = 32;

    /* filter out stuff which doesn't look remotely like an IP address; this helps 
     * callers like mod_access which have a syntax allowing hostname or IP address;
     * APR_EINVAL tells the caller that it was probably not intended to be an IP
     * address
     */
    if (!looks_like_ip(ipstr)) {
        return APR_EINVAL;
    }

    *ipsub = apr_pcalloc(p, sizeof(apr_ipsubnet_t));

    /* assume ipstr is an individual IP address, not a subnet */
    memset((*ipsub)->mask, 0xFF, sizeof (*ipsub)->mask);

    rv = parse_ip(*ipsub, ipstr, mask_or_numbits == NULL);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    if (mask_or_numbits) {
#if APR_HAVE_IPV6
        if ((*ipsub)->family == AF_INET6) {
            maxbits = 128;
        }
#endif
        bits = strtol(mask_or_numbits, &endptr, 10);
        if (*endptr == '\0' && bits > 0 && bits <= maxbits) {
            /* valid num-bits string; fill in mask appropriately */
            int cur_entry = 0;
            apr_int32_t cur_bit_value;

            memset((*ipsub)->mask, 0, sizeof (*ipsub)->mask);
            while (bits > 32) {
                (*ipsub)->mask[cur_entry] = 0xFFFFFFFF; /* all 32 bits */
                bits -= 32;
                ++cur_entry;
            }
            cur_bit_value = 0x80000000;
            while (bits) {
                (*ipsub)->mask[cur_entry] |= cur_bit_value;
                --bits;
                cur_bit_value /= 2;
            }
            (*ipsub)->mask[cur_entry] = htonl((*ipsub)->mask[cur_entry]);
        }
        else if (apr_inet_pton(AF_INET, mask_or_numbits, (*ipsub)->mask) == 1 &&
            (*ipsub)->family == AF_INET) {
            /* valid IPv4 netmask */
        }
        else {
            return APR_EBADMASK;
        }
    }

    fix_subnet(*ipsub);

    return APR_SUCCESS;
}

APR_DECLARE(int) apr_ipsubnet_test(apr_ipsubnet_t *ipsub, apr_sockaddr_t *sa)
{
#if APR_HAVE_IPV6
    /* XXX This line will segv on Win32 build with APR_HAVE_IPV6,
     * but without the IPV6 drivers installed.
     */
    if (sa->family == AF_INET) {
        if (ipsub->family == AF_INET &&
            ((sa->sa.sin.sin_addr.s_addr & ipsub->mask[0]) == ipsub->sub[0])) {
            return 1;
        }
    }
    else if (IN6_IS_ADDR_V4MAPPED((struct in6_addr *)sa->ipaddr_ptr)) {
        if (ipsub->family == AF_INET &&
            (((apr_uint32_t *)sa->ipaddr_ptr)[3] & ipsub->mask[0]) == ipsub->sub[0]) {
            return 1;
        }
    }
    else if (sa->family == AF_INET6 && ipsub->family == AF_INET6) {
        apr_uint32_t *addr = (apr_uint32_t *)sa->ipaddr_ptr;

        if ((addr[0] & ipsub->mask[0]) == ipsub->sub[0] &&
            (addr[1] & ipsub->mask[1]) == ipsub->sub[1] &&
            (addr[2] & ipsub->mask[2]) == ipsub->sub[2] &&
            (addr[3] & ipsub->mask[3]) == ipsub->sub[3]) {
            return 1;
        }
    }
#else
    if ((sa->sa.sin.sin_addr.s_addr & ipsub->mask[0]) == ipsub->sub[0]) {
        return 1;
    }
#endif /* APR_HAVE_IPV6 */
    return 0; /* no match */
}
