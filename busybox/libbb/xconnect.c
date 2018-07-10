/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Connect to host at port using address resolution from getaddrinfo
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include <sys/types.h>
#include <sys/socket.h> /* netinet/in.h needs it */
#include <netinet/in.h>
#include <net/if.h>
#include <sys/un.h>
#include "libbb.h"

int FAST_FUNC setsockopt_int(int fd, int level, int optname, int optval)
{
	return setsockopt(fd, level, optname, &optval, sizeof(int));
}
int FAST_FUNC setsockopt_1(int fd, int level, int optname)
{
	return setsockopt_int(fd, level, optname, 1);
}
int FAST_FUNC setsockopt_SOL_SOCKET_int(int fd, int optname, int optval)
{
	return setsockopt_int(fd, SOL_SOCKET, optname, optval);
}
int FAST_FUNC setsockopt_SOL_SOCKET_1(int fd, int optname)
{
	return setsockopt_SOL_SOCKET_int(fd, optname, 1);
}

void FAST_FUNC setsockopt_reuseaddr(int fd)
{
	setsockopt_SOL_SOCKET_1(fd, SO_REUSEADDR);
}
int FAST_FUNC setsockopt_broadcast(int fd)
{
	return setsockopt_SOL_SOCKET_1(fd, SO_BROADCAST);
}
int FAST_FUNC setsockopt_keepalive(int fd)
{
	return setsockopt_SOL_SOCKET_1(fd, SO_KEEPALIVE);
}

#ifdef SO_BINDTODEVICE
int FAST_FUNC setsockopt_bindtodevice(int fd, const char *iface)
{
	int r;
	struct ifreq ifr;
	strncpy_IFNAMSIZ(ifr.ifr_name, iface);
	/* NB: passing (iface, strlen(iface) + 1) does not work!
	 * (maybe it works on _some_ kernels, but not on 2.6.26)
	 * Actually, ifr_name is at offset 0, and in practice
	 * just giving char[IFNAMSIZ] instead of struct ifreq works too.
	 * But just in case it's not true on some obscure arch... */
	r = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
	if (r)
		bb_perror_msg("can't bind to interface %s", iface);
	return r;
}
#else
int FAST_FUNC setsockopt_bindtodevice(int fd UNUSED_PARAM,
		const char *iface UNUSED_PARAM)
{
	bb_error_msg("SO_BINDTODEVICE is not supported on this system");
	return -1;
}
#endif

static len_and_sockaddr* get_lsa(int fd, int (*get_name)(int fd, struct sockaddr *addr, socklen_t *addrlen))
{
	len_and_sockaddr lsa;
	len_and_sockaddr *lsa_ptr;

	lsa.len = LSA_SIZEOF_SA;
	if (get_name(fd, &lsa.u.sa, &lsa.len) != 0)
		return NULL;

	lsa_ptr = xzalloc(LSA_LEN_SIZE + lsa.len);
	if (lsa.len > LSA_SIZEOF_SA) { /* rarely (if ever) happens */
		lsa_ptr->len = lsa.len;
		get_name(fd, &lsa_ptr->u.sa, &lsa_ptr->len);
	} else {
		memcpy(lsa_ptr, &lsa, LSA_LEN_SIZE + lsa.len);
	}
	return lsa_ptr;
}

len_and_sockaddr* FAST_FUNC get_sock_lsa(int fd)
{
	return get_lsa(fd, getsockname);
}

len_and_sockaddr* FAST_FUNC get_peer_lsa(int fd)
{
	return get_lsa(fd, getpeername);
}

void FAST_FUNC xconnect(int s, const struct sockaddr *s_addr, socklen_t addrlen)
{
	if (connect(s, s_addr, addrlen) < 0) {
		if (ENABLE_FEATURE_CLEAN_UP)
			close(s);
		if (s_addr->sa_family == AF_INET)
			bb_perror_msg_and_die("%s (%s)",
				"can't connect to remote host",
				inet_ntoa(((struct sockaddr_in *)s_addr)->sin_addr));
		bb_perror_msg_and_die("can't connect to remote host");
	}
}

/* Return port number for a service.
 * If "port" is a number use it as the port.
 * If "port" is a name it is looked up in /etc/services,
 * if it isnt found return default_port
 */
unsigned FAST_FUNC bb_lookup_port(const char *port, const char *protocol, unsigned default_port)
{
	unsigned port_nr = default_port;
	if (port) {
		int old_errno;

		/* Since this is a lib function, we're not allowed to reset errno to 0.
		 * Doing so could break an app that is deferring checking of errno. */
		old_errno = errno;
		port_nr = bb_strtou(port, NULL, 10);
		if (errno || port_nr > 65535) {
			struct servent *tserv = getservbyname(port, protocol);
			port_nr = default_port;
			if (tserv)
				port_nr = ntohs(tserv->s_port);
		}
		errno = old_errno;
	}
	return (uint16_t)port_nr;
}


/* "New" networking API */


int FAST_FUNC get_nport(const struct sockaddr *sa)
{
#if ENABLE_FEATURE_IPV6
	if (sa->sa_family == AF_INET6) {
		return ((struct sockaddr_in6*)sa)->sin6_port;
	}
#endif
	if (sa->sa_family == AF_INET) {
		return ((struct sockaddr_in*)sa)->sin_port;
	}
	/* What? UNIX socket? IPX?? :) */
	return -1;
}

void FAST_FUNC set_nport(struct sockaddr *sa, unsigned port)
{
#if ENABLE_FEATURE_IPV6
	if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (void*) sa;
		sin6->sin6_port = port;
		return;
	}
#endif
	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (void*) sa;
		sin->sin_port = port;
		return;
	}
	/* What? UNIX socket? IPX?? :) */
}

/* We hijack this constant to mean something else */
/* It doesn't hurt because we will remove this bit anyway */
#define DIE_ON_ERROR AI_CANONNAME

/* host: "1.2.3.4[:port]", "www.google.com[:port]"
 * port: if neither of above specifies port # */
static len_and_sockaddr* str2sockaddr(
		const char *host, int port,
IF_FEATURE_IPV6(sa_family_t af,)
		int ai_flags)
{
IF_NOT_FEATURE_IPV6(sa_family_t af = AF_INET;)
	int rc;
	len_and_sockaddr *r;
	struct addrinfo *result = NULL;
	struct addrinfo *used_res;
	const char *org_host = host; /* only for error msg */
	const char *cp;
	struct addrinfo hint;

	if (ENABLE_FEATURE_UNIX_LOCAL && is_prefixed_with(host, "local:")) {
		struct sockaddr_un *sun;

		r = xzalloc(LSA_LEN_SIZE + sizeof(struct sockaddr_un));
		r->len = sizeof(struct sockaddr_un);
		r->u.sa.sa_family = AF_UNIX;
		sun = (struct sockaddr_un *)&r->u.sa;
		safe_strncpy(sun->sun_path, host + 6, sizeof(sun->sun_path));
		return r;
	}

	r = NULL;

	/* Ugly parsing of host:addr */
	if (ENABLE_FEATURE_IPV6 && host[0] == '[') {
		/* Even uglier parsing of [xx]:nn */
		host++;
		cp = strchr(host, ']');
		if (!cp || (cp[1] != ':' && cp[1] != '\0')) {
			/* Malformed: must be [xx]:nn or [xx] */
			bb_error_msg("bad address '%s'", org_host);
			if (ai_flags & DIE_ON_ERROR)
				xfunc_die();
			return NULL;
		}
	} else {
		cp = strrchr(host, ':');
		if (ENABLE_FEATURE_IPV6 && cp && strchr(host, ':') != cp) {
			/* There is more than one ':' (e.g. "::1") */
			cp = NULL; /* it's not a port spec */
		}
	}
	if (cp) { /* points to ":" or "]:" */
		int sz = cp - host + 1;

		host = safe_strncpy(alloca(sz), host, sz);
		if (ENABLE_FEATURE_IPV6 && *cp != ':') {
			cp++; /* skip ']' */
			if (*cp == '\0') /* [xx] without port */
				goto skip;
		}
		cp++; /* skip ':' */
		port = bb_strtou(cp, NULL, 10);
		if (errno || (unsigned)port > 0xffff) {
			bb_error_msg("bad port spec '%s'", org_host);
			if (ai_flags & DIE_ON_ERROR)
				xfunc_die();
			return NULL;
		}
 skip: ;
	}

	/* Next two if blocks allow to skip getaddrinfo()
	 * in case host name is a numeric IP(v6) address.
	 * getaddrinfo() initializes DNS resolution machinery,
	 * scans network config and such - tens of syscalls.
	 */
	/* If we were not asked specifically for IPv6,
	 * check whether this is a numeric IPv4 */
	IF_FEATURE_IPV6(if(af != AF_INET6)) {
		struct in_addr in4;
		if (inet_aton(host, &in4) != 0) {
			r = xzalloc(LSA_LEN_SIZE + sizeof(struct sockaddr_in));
			r->len = sizeof(struct sockaddr_in);
			r->u.sa.sa_family = AF_INET;
			r->u.sin.sin_addr = in4;
			goto set_port;
		}
	}
#if ENABLE_FEATURE_IPV6
	/* If we were not asked specifically for IPv4,
	 * check whether this is a numeric IPv6 */
	if (af != AF_INET) {
		struct in6_addr in6;
		if (inet_pton(AF_INET6, host, &in6) > 0) {
			r = xzalloc(LSA_LEN_SIZE + sizeof(struct sockaddr_in6));
			r->len = sizeof(struct sockaddr_in6);
			r->u.sa.sa_family = AF_INET6;
			r->u.sin6.sin6_addr = in6;
			goto set_port;
		}
	}
#endif

	memset(&hint, 0 , sizeof(hint));
	hint.ai_family = af;
	/* Need SOCK_STREAM, or else we get each address thrice (or more)
	 * for each possible socket type (tcp,udp,raw...): */
	hint.ai_socktype = SOCK_STREAM;
	hint.ai_flags = ai_flags & ~DIE_ON_ERROR;
	rc = getaddrinfo(host, NULL, &hint, &result);
	if (rc || !result) {
		bb_error_msg("bad address '%s'", org_host);
		if (ai_flags & DIE_ON_ERROR)
			xfunc_die();
		goto ret;
	}
	used_res = result;
#if ENABLE_FEATURE_PREFER_IPV4_ADDRESS
	while (1) {
		if (used_res->ai_family == AF_INET)
			break;
		used_res = used_res->ai_next;
		if (!used_res) {
			used_res = result;
			break;
		}
	}
#endif
	r = xmalloc(LSA_LEN_SIZE + used_res->ai_addrlen);
	r->len = used_res->ai_addrlen;
	memcpy(&r->u.sa, used_res->ai_addr, used_res->ai_addrlen);

 set_port:
	set_nport(&r->u.sa, htons(port));
 ret:
	if (result)
		freeaddrinfo(result);
	return r;
}
#if !ENABLE_FEATURE_IPV6
#define str2sockaddr(host, port, af, ai_flags) str2sockaddr(host, port, ai_flags)
#endif

#if ENABLE_FEATURE_IPV6
len_and_sockaddr* FAST_FUNC host_and_af2sockaddr(const char *host, int port, sa_family_t af)
{
	return str2sockaddr(host, port, af, 0);
}

len_and_sockaddr* FAST_FUNC xhost_and_af2sockaddr(const char *host, int port, sa_family_t af)
{
	return str2sockaddr(host, port, af, DIE_ON_ERROR);
}
#endif

len_and_sockaddr* FAST_FUNC host2sockaddr(const char *host, int port)
{
	return str2sockaddr(host, port, AF_UNSPEC, 0);
}

len_and_sockaddr* FAST_FUNC xhost2sockaddr(const char *host, int port)
{
	return str2sockaddr(host, port, AF_UNSPEC, DIE_ON_ERROR);
}

len_and_sockaddr* FAST_FUNC xdotted2sockaddr(const char *host, int port)
{
	return str2sockaddr(host, port, AF_UNSPEC, AI_NUMERICHOST | DIE_ON_ERROR);
}

int FAST_FUNC xsocket_type(len_and_sockaddr **lsap, int family, int sock_type)
{
	len_and_sockaddr *lsa;
	int fd;
	int len;

	if (family == AF_UNSPEC) {
#if ENABLE_FEATURE_IPV6
		fd = socket(AF_INET6, sock_type, 0);
		if (fd >= 0) {
			family = AF_INET6;
			goto done;
		}
#endif
		family = AF_INET;
	}

	fd = xsocket(family, sock_type, 0);

	len = sizeof(struct sockaddr_in);
	if (family == AF_UNIX)
		len = sizeof(struct sockaddr_un);
#if ENABLE_FEATURE_IPV6
	if (family == AF_INET6) {
 done:
		len = sizeof(struct sockaddr_in6);
	}
#endif
	lsa = xzalloc(LSA_LEN_SIZE + len);
	lsa->len = len;
	lsa->u.sa.sa_family = family;
	*lsap = lsa;
	return fd;
}

int FAST_FUNC xsocket_stream(len_and_sockaddr **lsap)
{
	return xsocket_type(lsap, AF_UNSPEC, SOCK_STREAM);
}

static int create_and_bind_or_die(const char *bindaddr, int port, int sock_type)
{
	int fd;
	len_and_sockaddr *lsa;

	if (bindaddr && bindaddr[0]) {
		lsa = xdotted2sockaddr(bindaddr, port);
		/* user specified bind addr dictates family */
		fd = xsocket(lsa->u.sa.sa_family, sock_type, 0);
	} else {
		fd = xsocket_type(&lsa, AF_UNSPEC, sock_type);
		set_nport(&lsa->u.sa, htons(port));
	}
	setsockopt_reuseaddr(fd);
	xbind(fd, &lsa->u.sa, lsa->len);
	free(lsa);
	return fd;
}

int FAST_FUNC create_and_bind_stream_or_die(const char *bindaddr, int port)
{
	return create_and_bind_or_die(bindaddr, port, SOCK_STREAM);
}

int FAST_FUNC create_and_bind_dgram_or_die(const char *bindaddr, int port)
{
	return create_and_bind_or_die(bindaddr, port, SOCK_DGRAM);
}


int FAST_FUNC create_and_connect_stream_or_die(const char *peer, int port)
{
	int fd;
	len_and_sockaddr *lsa;

	lsa = xhost2sockaddr(peer, port);
	fd = xsocket(lsa->u.sa.sa_family, SOCK_STREAM, 0);
	setsockopt_reuseaddr(fd);
	xconnect(fd, &lsa->u.sa, lsa->len);
	free(lsa);
	return fd;
}

int FAST_FUNC xconnect_stream(const len_and_sockaddr *lsa)
{
	int fd = xsocket(lsa->u.sa.sa_family, SOCK_STREAM, 0);
	xconnect(fd, &lsa->u.sa, lsa->len);
	return fd;
}

/* We hijack this constant to mean something else */
/* It doesn't hurt because we will add this bit anyway */
#define IGNORE_PORT NI_NUMERICSERV
static char* FAST_FUNC sockaddr2str(const struct sockaddr *sa, int flags)
{
	char host[128];
	char serv[16];
	int rc;
	socklen_t salen;

	if (ENABLE_FEATURE_UNIX_LOCAL && sa->sa_family == AF_UNIX) {
		struct sockaddr_un *sun = (struct sockaddr_un *)sa;
		return xasprintf("local:%.*s",
				(int) sizeof(sun->sun_path),
				sun->sun_path);
	}

	salen = LSA_SIZEOF_SA;
#if ENABLE_FEATURE_IPV6
	if (sa->sa_family == AF_INET)
		salen = sizeof(struct sockaddr_in);
	if (sa->sa_family == AF_INET6)
		salen = sizeof(struct sockaddr_in6);
#endif
	rc = getnameinfo(sa, salen,
			host, sizeof(host),
	/* can do ((flags & IGNORE_PORT) ? NULL : serv) but why bother? */
			serv, sizeof(serv),
			/* do not resolve port# into service _name_ */
			flags | NI_NUMERICSERV
	);
	if (rc)
		return NULL;
	if (flags & IGNORE_PORT)
		return xstrdup(host);
#if ENABLE_FEATURE_IPV6
	if (sa->sa_family == AF_INET6) {
		if (strchr(host, ':')) /* heh, it's not a resolved hostname */
			return xasprintf("[%s]:%s", host, serv);
		/*return xasprintf("%s:%s", host, serv);*/
		/* - fall through instead */
	}
#endif
	/* For now we don't support anything else, so it has to be INET */
	/*if (sa->sa_family == AF_INET)*/
		return xasprintf("%s:%s", host, serv);
	/*return xstrdup(host);*/
}

char* FAST_FUNC xmalloc_sockaddr2host(const struct sockaddr *sa)
{
	return sockaddr2str(sa, 0);
}

char* FAST_FUNC xmalloc_sockaddr2host_noport(const struct sockaddr *sa)
{
	return sockaddr2str(sa, IGNORE_PORT);
}

char* FAST_FUNC xmalloc_sockaddr2hostonly_noport(const struct sockaddr *sa)
{
	return sockaddr2str(sa, NI_NAMEREQD | IGNORE_PORT);
}
#ifndef NI_NUMERICSCOPE
# define NI_NUMERICSCOPE 0
#endif
char* FAST_FUNC xmalloc_sockaddr2dotted(const struct sockaddr *sa)
{
	return sockaddr2str(sa, NI_NUMERICHOST | NI_NUMERICSCOPE);
}

char* FAST_FUNC xmalloc_sockaddr2dotted_noport(const struct sockaddr *sa)
{
	return sockaddr2str(sa, NI_NUMERICHOST | NI_NUMERICSCOPE | IGNORE_PORT);
}
