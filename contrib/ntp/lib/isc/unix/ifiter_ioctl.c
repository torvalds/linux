/*
 * Copyright (C) 2004-2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: ifiter_ioctl.c,v 1.62 2009/01/18 23:48:14 tbox Exp $ */

/*! \file
 * \brief
 * Obtain the list of network interfaces using the SIOCGLIFCONF ioctl.
 * See netintro(4).
 */

#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
#ifdef ISC_PLATFORM_HAVEIF_LADDRCONF
#define lifc_len iflc_len
#define lifc_buf iflc_buf
#define lifc_req iflc_req
#define LIFCONF if_laddrconf
#else
#define ISC_HAVE_LIFC_FAMILY 1
#define ISC_HAVE_LIFC_FLAGS 1
#define LIFCONF lifconf
#endif

#ifdef ISC_PLATFORM_HAVEIF_LADDRREQ
#define lifr_addr iflr_addr
#define lifr_name iflr_name
#define lifr_dstaddr iflr_dstaddr
#define lifr_broadaddr iflr_broadaddr
#define lifr_flags iflr_flags
#define lifr_index iflr_index
#define ss_family sa_family
#define LIFREQ if_laddrreq
#else
#define LIFREQ lifreq
#endif
#endif

#define IFITER_MAGIC		ISC_MAGIC('I', 'F', 'I', 'T')
#define VALID_IFITER(t)		ISC_MAGIC_VALID(t, IFITER_MAGIC)

struct isc_interfaceiter {
	unsigned int		magic;		/* Magic number. */
	isc_mem_t		*mctx;
	int			mode;
	int			socket;
	struct ifconf 		ifc;
	void			*buf;		/* Buffer for sysctl data. */
	unsigned int		bufsize;	/* Bytes allocated. */
	unsigned int		pos;		/* Current offset in
						   SIOCGIFCONF data */
#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
	int			socket6;
	struct LIFCONF 		lifc;
	void			*buf6;		/* Buffer for sysctl data. */
	unsigned int		bufsize6;	/* Bytes allocated. */
	unsigned int		pos6;		/* Current offset in
						   SIOCGLIFCONF data */
	isc_result_t		result6;	/* Last result code. */
	isc_boolean_t		first6;
#endif
#ifdef HAVE_TRUCLUSTER
	int			clua_context;	/* Cluster alias context */
	isc_boolean_t		clua_done;
	struct sockaddr		clua_sa;
#endif
#ifdef	__linux
	FILE *			proc;
	char			entry[ISC_IF_INET6_SZ];
	isc_result_t		valid;
#endif
	isc_interface_t		current;	/* Current interface data. */
	isc_result_t		result;		/* Last result code. */
};

#ifdef HAVE_TRUCLUSTER
#include <clua/clua.h>
#include <sys/socket.h>
#endif


/*%
 * Size of buffer for SIOCGLIFCONF, in bytes.  We assume no sane system
 * will have more than a megabyte of interface configuration data.
 */
#define IFCONF_BUFSIZE_INITIAL	4096
#define IFCONF_BUFSIZE_MAX	1048576

#ifdef __linux
#ifndef IF_NAMESIZE
# ifdef IFNAMSIZ
#  define IF_NAMESIZE  IFNAMSIZ
# else
#  define IF_NAMESIZE 16
# endif
#endif
#endif

/* Silence a warning when this file is #included */
int
isc_ioctl(int fildes, int req, char *arg);

int
isc_ioctl(int fildes, int req, char *arg) {
	int trys;
	int ret;

	for (trys = 0; trys < 3; trys++) {
		if ((ret = ioctl(fildes, req, arg)) < 0) {
			if (errno == EINTR)
				continue;
		}
		break;
	}
	return (ret);
}

static isc_result_t
getbuf4(isc_interfaceiter_t *iter) {
	char strbuf[ISC_STRERRORSIZE];

	iter->bufsize = IFCONF_BUFSIZE_INITIAL;

	for (;;) {
		iter->buf = isc_mem_get(iter->mctx, iter->bufsize);
		if (iter->buf == NULL)
			return (ISC_R_NOMEMORY);

		memset(&iter->ifc.ifc_len, 0, sizeof(iter->ifc.ifc_len));
		iter->ifc.ifc_len = iter->bufsize;
		iter->ifc.ifc_buf = iter->buf;
		/*
		 * Ignore the HP/UX warning about "integer overflow during
		 * conversion".  It comes from its own macro definition,
		 * and is really hard to shut up.
		 */
		if (isc_ioctl(iter->socket, SIOCGIFCONF, (char *)&iter->ifc)
		    == -1) {
			if (errno != EINVAL) {
				isc__strerror(errno, strbuf, sizeof(strbuf));
				UNEXPECTED_ERROR(__FILE__, __LINE__,
						 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_IFITERIOCTL,
							ISC_MSG_GETIFCONFIG,
							"get interface "
							"configuration: %s"),
						 strbuf);
				goto unexpected;
			}
			/*
			 * EINVAL.  Retry with a bigger buffer.
			 */
		} else {
			/*
			 * The ioctl succeeded.
			 * Some OS's just return what will fit rather
			 * than set EINVAL if the buffer is too small
			 * to fit all the interfaces in.  If
			 * ifc.lifc_len is too near to the end of the
			 * buffer we will grow it just in case and
			 * retry.
			 */
			if (iter->ifc.ifc_len + 2 * sizeof(struct ifreq)
			    < iter->bufsize)
				break;
		}
		if (iter->bufsize >= IFCONF_BUFSIZE_MAX) {
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_IFITERIOCTL,
							ISC_MSG_BUFFERMAX,
							"get interface "
							"configuration: "
							"maximum buffer "
							"size exceeded"));
			goto unexpected;
		}
		isc_mem_put(iter->mctx, iter->buf, iter->bufsize);

		iter->bufsize *= 2;
	}
	return (ISC_R_SUCCESS);

 unexpected:
	isc_mem_put(iter->mctx, iter->buf, iter->bufsize);
	iter->buf = NULL;
	return (ISC_R_UNEXPECTED);
}

#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
static isc_result_t
getbuf6(isc_interfaceiter_t *iter) {
	char strbuf[ISC_STRERRORSIZE];
	isc_result_t result;

	iter->bufsize6 = IFCONF_BUFSIZE_INITIAL;

	for (;;) {
		iter->buf6 = isc_mem_get(iter->mctx, iter->bufsize6);
		if (iter->buf6 == NULL)
			return (ISC_R_NOMEMORY);

		memset(&iter->lifc, 0, sizeof(iter->lifc));
#ifdef ISC_HAVE_LIFC_FAMILY
		iter->lifc.lifc_family = AF_INET6;
#endif
#ifdef ISC_HAVE_LIFC_FLAGS
		iter->lifc.lifc_flags = 0;
#endif
		iter->lifc.lifc_len = iter->bufsize6;
		iter->lifc.lifc_buf = iter->buf6;
		/*
		 * Ignore the HP/UX warning about "integer overflow during
		 * conversion".  It comes from its own macro definition,
		 * and is really hard to shut up.
		 */
		if (isc_ioctl(iter->socket6, SIOCGLIFCONF, (char *)&iter->lifc)
		    == -1) {
#ifdef __hpux
			/*
			 * IPv6 interface scanning is not available on all
			 * kernels w/ IPv6 sockets.
			 */
			if (errno == ENOENT) {
				isc__strerror(errno, strbuf, sizeof(strbuf));
				isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
					      ISC_LOGMODULE_INTERFACE,
					      ISC_LOG_DEBUG(1),
					      isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_IFITERIOCTL,
							ISC_MSG_GETIFCONFIG,
							"get interface "
							"configuration: %s"),
					       strbuf);
				result = ISC_R_FAILURE;
				goto cleanup;
			}
#endif
			if (errno != EINVAL) {
				isc__strerror(errno, strbuf, sizeof(strbuf));
				UNEXPECTED_ERROR(__FILE__, __LINE__,
						 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_IFITERIOCTL,
							ISC_MSG_GETIFCONFIG,
							"get interface "
							"configuration: %s"),
						 strbuf);
				result = ISC_R_UNEXPECTED;
				goto cleanup;
			}
			/*
			 * EINVAL.  Retry with a bigger buffer.
			 */
		} else {
			/*
			 * The ioctl succeeded.
			 * Some OS's just return what will fit rather
			 * than set EINVAL if the buffer is too small
			 * to fit all the interfaces in.  If
			 * ifc.ifc_len is too near to the end of the
			 * buffer we will grow it just in case and
			 * retry.
			 */
			if (iter->lifc.lifc_len + 2 * sizeof(struct LIFREQ)
			    < iter->bufsize6)
				break;
		}
		if (iter->bufsize6 >= IFCONF_BUFSIZE_MAX) {
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_IFITERIOCTL,
							ISC_MSG_BUFFERMAX,
							"get interface "
							"configuration: "
							"maximum buffer "
							"size exceeded"));
			result = ISC_R_UNEXPECTED;
			goto cleanup;
		}
		isc_mem_put(iter->mctx, iter->buf6, iter->bufsize6);

		iter->bufsize6 *= 2;
	}

	if (iter->lifc.lifc_len != 0)
		iter->mode = 6;
	return (ISC_R_SUCCESS);

 cleanup:
	isc_mem_put(iter->mctx, iter->buf6, iter->bufsize6);
	iter->buf6 = NULL;
	return (result);
}
#endif

isc_result_t
isc_interfaceiter_create(isc_mem_t *mctx, isc_interfaceiter_t **iterp) {
	isc_interfaceiter_t *iter;
	isc_result_t result;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(mctx != NULL);
	REQUIRE(iterp != NULL);
	REQUIRE(*iterp == NULL);

	iter = isc_mem_get(mctx, sizeof(*iter));
	if (iter == NULL)
		return (ISC_R_NOMEMORY);

	iter->mctx = mctx;
	iter->mode = 4;
	iter->buf = NULL;
	iter->pos = (unsigned int) -1;
#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
	iter->buf6 = NULL;
	iter->pos6 = (unsigned int) -1;
	iter->result6 = ISC_R_NOMORE;
	iter->socket6 = -1;
	iter->first6 = ISC_FALSE;
#endif

	/*
	 * Get the interface configuration, allocating more memory if
	 * necessary.
	 */

#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
	result = isc_net_probeipv6();
	if (result == ISC_R_SUCCESS) {
		/*
		 * Create an unbound datagram socket to do the SIOCGLIFCONF
		 * ioctl on.  HP/UX requires an AF_INET6 socket for
		 * SIOCGLIFCONF to get IPv6 addresses.
		 */
		if ((iter->socket6 = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_IFITERIOCTL,
							ISC_MSG_MAKESCANSOCKET,
							"making interface "
							"scan socket: %s"),
					 strbuf);
			result = ISC_R_UNEXPECTED;
			goto socket6_failure;
		}
		result = iter->result6 = getbuf6(iter);
		if (result != ISC_R_NOTIMPLEMENTED && result != ISC_R_SUCCESS)
			goto ioctl6_failure;
	}
#endif
	if ((iter->socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_IFITERIOCTL,
						ISC_MSG_MAKESCANSOCKET,
						"making interface "
						"scan socket: %s"),
				 strbuf);
		result = ISC_R_UNEXPECTED;
		goto socket_failure;
	}
	result = getbuf4(iter);
	if (result != ISC_R_SUCCESS)
		goto ioctl_failure;

	/*
	 * A newly created iterator has an undefined position
	 * until isc_interfaceiter_first() is called.
	 */
#ifdef HAVE_TRUCLUSTER
	iter->clua_context = -1;
	iter->clua_done = ISC_TRUE;
#endif
#ifdef __linux
	iter->proc = fopen("/proc/net/if_inet6", "r");
	iter->valid = ISC_R_FAILURE;
#endif
	iter->result = ISC_R_FAILURE;

	iter->magic = IFITER_MAGIC;
	*iterp = iter;
	return (ISC_R_SUCCESS);

 ioctl_failure:
	if (iter->buf != NULL)
		isc_mem_put(mctx, iter->buf, iter->bufsize);
	(void) close(iter->socket);

 socket_failure:
#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
	if (iter->buf6 != NULL)
		isc_mem_put(mctx, iter->buf6, iter->bufsize6);
  ioctl6_failure:
	if (iter->socket6 != -1)
		(void) close(iter->socket6);
  socket6_failure:
#endif

	isc_mem_put(mctx, iter, sizeof(*iter));
	return (result);
}

#ifdef HAVE_TRUCLUSTER
static void
get_inaddr(isc_netaddr_t *dst, struct in_addr *src) {
	dst->family = AF_INET;
	memcpy(&dst->type.in, src, sizeof(struct in_addr));
}

static isc_result_t
internal_current_clusteralias(isc_interfaceiter_t *iter) {
	struct clua_info ci;
	if (clua_getaliasinfo(&iter->clua_sa, &ci) != CLUA_SUCCESS)
		return (ISC_R_IGNORE);
	memset(&iter->current, 0, sizeof(iter->current));
	iter->current.af = iter->clua_sa.sa_family;
	memset(iter->current.name, 0, sizeof(iter->current.name));
	sprintf(iter->current.name, "clua%d", ci.aliasid);
	iter->current.flags = INTERFACE_F_UP;
	get_inaddr(&iter->current.address, &ci.addr);
	get_inaddr(&iter->current.netmask, &ci.netmask);
	return (ISC_R_SUCCESS);
}
#endif

/*
 * Get information about the current interface to iter->current.
 * If successful, return ISC_R_SUCCESS.
 * If the interface has an unsupported address family, or if
 * some operation on it fails, return ISC_R_IGNORE to make
 * the higher-level iterator code ignore it.
 */

static isc_result_t
internal_current4(isc_interfaceiter_t *iter) {
	struct ifreq *ifrp;
	struct ifreq ifreq;
	int family;
	char strbuf[ISC_STRERRORSIZE];
#if !defined(ISC_PLATFORM_HAVEIF_LADDRREQ) && defined(SIOCGLIFADDR)
	struct lifreq lifreq;
#else
	char sabuf[256];
#endif
	int i, bits, prefixlen;

	REQUIRE(VALID_IFITER(iter));

	if (iter->ifc.ifc_len == 0 ||
	    iter->pos == (unsigned int)iter->ifc.ifc_len) {
#ifdef __linux
		return (linux_if_inet6_current(iter));
#else
		return (ISC_R_NOMORE);
#endif
	}

	INSIST( iter->pos < (unsigned int) iter->ifc.ifc_len);

	ifrp = (void *)((char *) iter->ifc.ifc_req + iter->pos);

	memset(&ifreq, 0, sizeof(ifreq));
	memcpy(&ifreq, ifrp, sizeof(ifreq));

	family = ifreq.ifr_addr.sa_family;
#if defined(ISC_PLATFORM_HAVEIPV6)
	if (family != AF_INET && family != AF_INET6)
#else
	if (family != AF_INET)
#endif
		return (ISC_R_IGNORE);

	memset(&iter->current, 0, sizeof(iter->current));
	iter->current.af = family;

	INSIST(sizeof(ifreq.ifr_name) <= sizeof(iter->current.name));
	memset(iter->current.name, 0, sizeof(iter->current.name));
	memcpy(iter->current.name, ifreq.ifr_name, sizeof(ifreq.ifr_name));

	get_addr(family, &iter->current.address,
		 (struct sockaddr *)&ifrp->ifr_addr, ifreq.ifr_name);

	/*
	 * If the interface does not have a address ignore it.
	 */
	switch (family) {
	case AF_INET:
		if (iter->current.address.type.in.s_addr == htonl(INADDR_ANY))
			return (ISC_R_IGNORE);
		break;
	case AF_INET6:
		if (memcmp(&iter->current.address.type.in6, &in6addr_any,
			   sizeof(in6addr_any)) == 0)
			return (ISC_R_IGNORE);
		break;
	}

	/*
	 * Get interface flags.
	 */

	iter->current.flags = 0;

	/*
	 * Ignore the HP/UX warning about "integer overflow during
	 * conversion.  It comes from its own macro definition,
	 * and is really hard to shut up.
	 */
	if (isc_ioctl(iter->socket, SIOCGIFFLAGS, (char *) &ifreq) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "%s: getting interface flags: %s",
				 ifreq.ifr_name, strbuf);
		return (ISC_R_IGNORE);
	}

	if ((ifreq.ifr_flags & IFF_UP) != 0)
		iter->current.flags |= INTERFACE_F_UP;

#ifdef IFF_POINTOPOINT
	if ((ifreq.ifr_flags & IFF_POINTOPOINT) != 0)
		iter->current.flags |= INTERFACE_F_POINTTOPOINT;
#endif

	if ((ifreq.ifr_flags & IFF_LOOPBACK) != 0)
		iter->current.flags |= INTERFACE_F_LOOPBACK;

	if ((ifreq.ifr_flags & IFF_BROADCAST) != 0)
		iter->current.flags |= INTERFACE_F_BROADCAST;

#ifdef IFF_MULTICAST
	if ((ifreq.ifr_flags & IFF_MULTICAST) != 0)
		iter->current.flags |= INTERFACE_F_MULTICAST;
#endif

	if (family == AF_INET)
		goto inet;

#if !defined(ISC_PLATFORM_HAVEIF_LADDRREQ) && defined(SIOCGLIFADDR)
	memset(&lifreq, 0, sizeof(lifreq));
	memcpy(lifreq.lifr_name, iter->current.name, sizeof(lifreq.lifr_name));
	memcpy(&lifreq.lifr_addr, &iter->current.address.type.in6,
	       sizeof(iter->current.address.type.in6));

	if (isc_ioctl(iter->socket, SIOCGLIFADDR, &lifreq) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "%s: getting interface address: %s",
				 ifreq.ifr_name, strbuf);
		return (ISC_R_IGNORE);
	}
	prefixlen = lifreq.lifr_addrlen;
#else
	isc_netaddr_format(&iter->current.address, sabuf, sizeof(sabuf));
	isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
		      ISC_LOGMODULE_INTERFACE,
		      ISC_LOG_INFO,
		      isc_msgcat_get(isc_msgcat,
				     ISC_MSGSET_IFITERIOCTL,
				     ISC_MSG_GETIFCONFIG,
				     "prefix length for %s is unknown "
				     "(assume 128)"), sabuf);
	prefixlen = 128;
#endif

	/*
	 * Netmask already zeroed.
	 */
	iter->current.netmask.family = family;
	for (i = 0; i < 16; i++) {
		if (prefixlen > 8) {
			bits = 0;
			prefixlen -= 8;
		} else {
			bits = 8 - prefixlen;
			prefixlen = 0;
		}
		iter->current.netmask.type.in6.s6_addr[i] = (~0 << bits) & 0xff;
	}
#ifdef ISC_PLATFORM_HAVEIFNAMETOINDEX
	iter->current.ifindex = if_nametoindex(iter->current.name);
#endif
	return (ISC_R_SUCCESS);

 inet:
	if (family != AF_INET)
		return (ISC_R_IGNORE);
#ifdef IFF_POINTOPOINT
	/*
	 * If the interface is point-to-point, get the destination address.
	 */
	if ((iter->current.flags & INTERFACE_F_POINTTOPOINT) != 0) {
		/*
		 * Ignore the HP/UX warning about "integer overflow during
		 * conversion.  It comes from its own macro definition,
		 * and is really hard to shut up.
		 */
		if (isc_ioctl(iter->socket, SIOCGIFDSTADDR, (char *)&ifreq)
		    < 0) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat,
					       ISC_MSGSET_IFITERIOCTL,
					       ISC_MSG_GETDESTADDR,
					       "%s: getting "
					       "destination address: %s"),
					 ifreq.ifr_name, strbuf);
			return (ISC_R_IGNORE);
		}
		get_addr(family, &iter->current.dstaddress,
			 (struct sockaddr *)&ifreq.ifr_dstaddr, ifreq.ifr_name);
	}
#endif

	if ((iter->current.flags & INTERFACE_F_BROADCAST) != 0) {
		/*
		 * Ignore the HP/UX warning about "integer overflow during
		 * conversion.  It comes from its own macro definition,
		 * and is really hard to shut up.
		 */
		if (isc_ioctl(iter->socket, SIOCGIFBRDADDR, (char *)&ifreq)
		    < 0) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat,
					       ISC_MSGSET_IFITERIOCTL,
					       ISC_MSG_GETBCSTADDR,
					       "%s: getting "
					       "broadcast address: %s"),
					 ifreq.ifr_name, strbuf);
			return (ISC_R_IGNORE);
		}
		get_addr(family, &iter->current.broadcast,
			 (struct sockaddr *)&ifreq.ifr_broadaddr, ifreq.ifr_name);
	}

	/*
	 * Get the network mask.
	 */
	memset(&ifreq, 0, sizeof(ifreq));
	memcpy(&ifreq, ifrp, sizeof(ifreq));
	/*
	 * Ignore the HP/UX warning about "integer overflow during
	 * conversion.  It comes from its own macro definition,
	 * and is really hard to shut up.
	 */
	if (isc_ioctl(iter->socket, SIOCGIFNETMASK, (char *)&ifreq) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
			isc_msgcat_get(isc_msgcat,
				       ISC_MSGSET_IFITERIOCTL,
				       ISC_MSG_GETNETMASK,
				       "%s: getting netmask: %s"),
				       ifreq.ifr_name, strbuf);
		return (ISC_R_IGNORE);
	}
	get_addr(family, &iter->current.netmask,
		 (struct sockaddr *)&ifreq.ifr_addr, ifreq.ifr_name);
#ifdef ISC_PLATFORM_HAVEIFNAMETOINDEX
	iter->current.ifindex = if_nametoindex(iter->current.name);
#endif
	return (ISC_R_SUCCESS);
}

#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
static isc_result_t
internal_current6(isc_interfaceiter_t *iter) {
	struct LIFREQ *ifrp;
	struct LIFREQ lifreq;
	int family;
	char strbuf[ISC_STRERRORSIZE];
	int fd;

	REQUIRE(VALID_IFITER(iter));
	if (iter->result6 != ISC_R_SUCCESS)
		return (iter->result6);
	REQUIRE(iter->pos6 < (unsigned int) iter->lifc.lifc_len);

	ifrp = (void *)((char *)iter->lifc.lifc_req + iter->pos6);

	memset(&lifreq, 0, sizeof(lifreq));
	memcpy(&lifreq, ifrp, sizeof(lifreq));

	family = lifreq.lifr_addr.ss_family;
#ifdef ISC_PLATFORM_HAVEIPV6
	if (family != AF_INET && family != AF_INET6)
#else
	if (family != AF_INET)
#endif
		return (ISC_R_IGNORE);

	memset(&iter->current, 0, sizeof(iter->current));
	iter->current.af = family;

	INSIST(sizeof(lifreq.lifr_name) <= sizeof(iter->current.name));
	memset(iter->current.name, 0, sizeof(iter->current.name));
	memcpy(iter->current.name, lifreq.lifr_name, sizeof(lifreq.lifr_name));

	get_addr(family, &iter->current.address,
		 (struct sockaddr *)&lifreq.lifr_addr, lifreq.lifr_name);

	if (isc_netaddr_islinklocal(&iter->current.address))
		isc_netaddr_setzone(&iter->current.address, 
				    (isc_uint32_t)lifreq.lifr_index);

	/*
	 * If the interface does not have a address ignore it.
	 */
	switch (family) {
	case AF_INET:
		if (iter->current.address.type.in.s_addr == htonl(INADDR_ANY))
			return (ISC_R_IGNORE);
		break;
	case AF_INET6:
		if (memcmp(&iter->current.address.type.in6, &in6addr_any,
			   sizeof(in6addr_any)) == 0)
			return (ISC_R_IGNORE);
		break;
	}

	/*
	 * Get interface flags.
	 */

	iter->current.flags = 0;

	if (family == AF_INET6)
		fd = iter->socket6;
	else
		fd = iter->socket;

	/*
	 * Ignore the HP/UX warning about "integer overflow during
	 * conversion.  It comes from its own macro definition,
	 * and is really hard to shut up.
	 */
	if (isc_ioctl(fd, SIOCGLIFFLAGS, (char *) &lifreq) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "%s: getting interface flags: %s",
				 lifreq.lifr_name, strbuf);
		return (ISC_R_IGNORE);
	}

	if ((lifreq.lifr_flags & IFF_UP) != 0)
		iter->current.flags |= INTERFACE_F_UP;

#ifdef IFF_POINTOPOINT
	if ((lifreq.lifr_flags & IFF_POINTOPOINT) != 0)
		iter->current.flags |= INTERFACE_F_POINTTOPOINT;
#endif

	if ((lifreq.lifr_flags & IFF_LOOPBACK) != 0)
		iter->current.flags |= INTERFACE_F_LOOPBACK;

	if ((lifreq.lifr_flags & IFF_BROADCAST) != 0) {
		iter->current.flags |= INTERFACE_F_BROADCAST;
	}

#ifdef IFF_MULTICAST
	if ((lifreq.lifr_flags & IFF_MULTICAST) != 0) {
		iter->current.flags |= INTERFACE_F_MULTICAST;
	}
#endif

#ifdef IFF_POINTOPOINT
	/*
	 * If the interface is point-to-point, get the destination address.
	 */
	if ((iter->current.flags & INTERFACE_F_POINTTOPOINT) != 0) {
		/*
		 * Ignore the HP/UX warning about "integer overflow during
		 * conversion.  It comes from its own macro definition,
		 * and is really hard to shut up.
		 */
		if (isc_ioctl(fd, SIOCGLIFDSTADDR, (char *)&lifreq)
		    < 0) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat,
					       ISC_MSGSET_IFITERIOCTL,
					       ISC_MSG_GETDESTADDR,
					       "%s: getting "
					       "destination address: %s"),
					 lifreq.lifr_name, strbuf);
			return (ISC_R_IGNORE);
		}
		get_addr(family, &iter->current.dstaddress,
			 (struct sockaddr *)&lifreq.lifr_dstaddr,
			 lifreq.lifr_name);
	}
#endif

#ifdef SIOCGLIFBRDADDR
	if ((iter->current.flags & INTERFACE_F_BROADCAST) != 0) {
		/*
		 * Ignore the HP/UX warning about "integer overflow during
		 * conversion.  It comes from its own macro definition,
		 * and is really hard to shut up.
		 */
		if (isc_ioctl(iter->socket, SIOCGLIFBRDADDR, (char *)&lifreq)
		    < 0) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat,
					       ISC_MSGSET_IFITERIOCTL,
					       ISC_MSG_GETBCSTADDR,
					       "%s: getting "
					       "broadcast address: %s"),
					 lifreq.lifr_name, strbuf);
			return (ISC_R_IGNORE);
		}
		get_addr(family, &iter->current.broadcast,
			 (struct sockaddr *)&lifreq.lifr_broadaddr,
			 lifreq.lifr_name);
	}
#endif	/* SIOCGLIFBRDADDR */

	/*
	 * Get the network mask.  Netmask already zeroed.
	 */
	memset(&lifreq, 0, sizeof(lifreq));
	memcpy(&lifreq, ifrp, sizeof(lifreq));

#ifdef lifr_addrlen
	/*
	 * Special case: if the system provides lifr_addrlen member, the
	 * netmask of an IPv6 address can be derived from the length, since
	 * an IPv6 address always has a contiguous mask.
	 */
	if (family == AF_INET6) {
		int i, bits;

		iter->current.netmask.family = family;
		for (i = 0; i < lifreq.lifr_addrlen; i += 8) {
			bits = lifreq.lifr_addrlen - i;
			bits = (bits < 8) ? (8 - bits) : 0;
			iter->current.netmask.type.in6.s6_addr[i / 8] =
				(~0 << bits) & 0xff;
		}
#ifdef ISC_PLATFORM_HAVEIFNAMETOINDEX
		iter->current.ifindex = if_nametoindex(iter->current.name);
#endif
		return (ISC_R_SUCCESS);
	}
#endif

	/*
	 * Ignore the HP/UX warning about "integer overflow during
	 * conversion.  It comes from its own macro definition,
	 * and is really hard to shut up.
	 */
	if (isc_ioctl(fd, SIOCGLIFNETMASK, (char *)&lifreq) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_IFITERIOCTL,
						ISC_MSG_GETNETMASK,
						"%s: getting netmask: %s"),
				 lifreq.lifr_name, strbuf);
		return (ISC_R_IGNORE);
	}
	get_addr(family, &iter->current.netmask,
		 (struct sockaddr *)&lifreq.lifr_addr, lifreq.lifr_name);

#ifdef ISC_PLATFORM_HAVEIFNAMETOINDEX
	iter->current.ifindex = if_nametoindex(iter->current.name);
#endif
	return (ISC_R_SUCCESS);
}
#endif

static isc_result_t
internal_current(isc_interfaceiter_t *iter) {
#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
	if (iter->mode == 6) {
		iter->result6 = internal_current6(iter);
		if (iter->result6 != ISC_R_NOMORE)
			return (iter->result6);
	}
#endif
#ifdef HAVE_TRUCLUSTER
	if (!iter->clua_done)
		return(internal_current_clusteralias(iter));
#endif
	return (internal_current4(iter));
}

/*
 * Step the iterator to the next interface.  Unlike
 * isc_interfaceiter_next(), this may leave the iterator
 * positioned on an interface that will ultimately
 * be ignored.  Return ISC_R_NOMORE if there are no more
 * interfaces, otherwise ISC_R_SUCCESS.
 */
static isc_result_t
internal_next4(isc_interfaceiter_t *iter) {
#ifdef ISC_PLATFORM_HAVESALEN
	struct ifreq *ifrp;
#endif

	if (iter->pos < (unsigned int) iter->ifc.ifc_len) {
#ifdef ISC_PLATFORM_HAVESALEN
		ifrp = (struct ifreq *)((char *) iter->ifc.ifc_req + iter->pos);

		if (ifrp->ifr_addr.sa_len > sizeof(struct sockaddr))
			iter->pos += sizeof(ifrp->ifr_name) +
				     ifrp->ifr_addr.sa_len;
		else
#endif
			iter->pos += sizeof(struct ifreq);

	} else {
		INSIST(iter->pos == (unsigned int) iter->ifc.ifc_len);
#ifdef __linux
		return (linux_if_inet6_next(iter));
#else
		return (ISC_R_NOMORE);
#endif
	}
	return (ISC_R_SUCCESS);
}

#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
static isc_result_t
internal_next6(isc_interfaceiter_t *iter) {
#ifdef ISC_PLATFORM_HAVESALEN
	struct LIFREQ *ifrp;
#endif

	if (iter->result6 != ISC_R_SUCCESS && iter->result6 != ISC_R_IGNORE)
		return (iter->result6);

	REQUIRE(iter->pos6 < (unsigned int) iter->lifc.lifc_len);

#ifdef ISC_PLATFORM_HAVESALEN
	ifrp = (struct LIFREQ *)((char *) iter->lifc.lifc_req + iter->pos6);

	if (ifrp->lifr_addr.sa_len > sizeof(struct sockaddr))
		iter->pos6 += sizeof(ifrp->lifr_name) + ifrp->lifr_addr.sa_len;
	else
#endif
		iter->pos6 += sizeof(struct LIFREQ);

	if (iter->pos6 >= (unsigned int) iter->lifc.lifc_len)
		return (ISC_R_NOMORE);

	return (ISC_R_SUCCESS);
}
#endif

static isc_result_t
internal_next(isc_interfaceiter_t *iter) {
#ifdef HAVE_TRUCLUSTER
	int clua_result;
#endif
#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
	if (iter->mode == 6) {
		iter->result6 = internal_next6(iter);
		if (iter->result6 != ISC_R_NOMORE)
			return (iter->result6);
		if (iter->first6) {
			iter->first6 = ISC_FALSE;
			return (ISC_R_SUCCESS);
		}
	}
#endif
#ifdef HAVE_TRUCLUSTER
	if (!iter->clua_done) {
		clua_result = clua_getaliasaddress(&iter->clua_sa,
						   &iter->clua_context);
		if (clua_result != CLUA_SUCCESS)
			iter->clua_done = ISC_TRUE;
		return (ISC_R_SUCCESS);
	}
#endif
	return (internal_next4(iter));
}

static void
internal_destroy(isc_interfaceiter_t *iter) {
	(void) close(iter->socket);
#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
	if (iter->socket6 != -1)
		(void) close(iter->socket6);
	if (iter->buf6 != NULL) {
		isc_mem_put(iter->mctx, iter->buf6, iter->bufsize6);
	}
#endif
#ifdef __linux
	if (iter->proc != NULL)
		fclose(iter->proc);
#endif
}

static
void internal_first(isc_interfaceiter_t *iter) {
#ifdef HAVE_TRUCLUSTER
	int clua_result;
#endif
	iter->pos = 0;
#if defined(SIOCGLIFCONF) && defined(SIOCGLIFADDR)
	iter->pos6 = 0;
	if (iter->result6 == ISC_R_NOMORE)
		iter->result6 = ISC_R_SUCCESS;
	iter->first6 = ISC_TRUE;
#endif
#ifdef HAVE_TRUCLUSTER
	iter->clua_context = 0;
	clua_result = clua_getaliasaddress(&iter->clua_sa,
					   &iter->clua_context);
	iter->clua_done = ISC_TF(clua_result != CLUA_SUCCESS);
#endif
#ifdef __linux
	linux_if_inet6_first(iter);
#endif
}
