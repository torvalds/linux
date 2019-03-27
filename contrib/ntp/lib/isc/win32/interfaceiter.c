/*
 * Copyright (C) 2004, 2007-2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001  Internet Software Consortium.
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

/* $Id: interfaceiter.c,v 1.15 2009/01/18 23:48:14 tbox Exp $ */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <gaa_compat.h>

#include <isc/interfaceiter.h>
#include <isc/mem.h>
#include <isc/result.h>
#include <isc/string.h>
#include <isc/strerror.h>
#include <isc/types.h>
#include <isc/util.h>
#include <isc/win32os.h>

void InitSockets(void);


#define IFITER_MAGIC		0x49464954U	/* IFIT. */
#define VALID_IFITER(t)		((t) != NULL && (t)->magic == IFITER_MAGIC)

struct isc_interfaceiter {
	unsigned int		magic;		/* Magic number. */
	/* common fields */
	isc_mem_t		*mctx;
	isc_interface_t		current;	/* Current interface data. */
	isc_result_t		result;		/* Last result code. */
	/* fields used if GetAdaptersAddresses is available at runtime */
	IP_ADAPTER_ADDRESSES *	ipaa;		/* GAA() result buffer */
	ULONG			ipaasize;	/* Bytes allocated */
	IP_ADAPTER_ADDRESSES *	ipaaCur;	/* enumeration position */
	IP_ADAPTER_UNICAST_ADDRESS *ipuaCur;	/* enumeration subposition */
	/* fields used for the older address enumeration ioctls */
	SOCKET			socket;
	INTERFACE_INFO		IFData;		/* Current Interface Info */
	int			numIF;		/* Current Interface count */
	int			v4IF;		/* Number of IPv4 Interfaces */
	INTERFACE_INFO		*buf4;		/* Buffer for WSAIoctl data. */
	unsigned int		buf4size;	/* Bytes allocated. */
	INTERFACE_INFO		*pos4;		/* Current offset in IF List */
	SOCKET_ADDRESS_LIST	*buf6;
	unsigned int		buf6size;	/* Bytes allocated. */
	unsigned int		pos6;		/* buf6 index, counts down */
	struct in6_addr		loop__1;	/* ::1 node-scope localhost */
	struct in6_addr		loopfe80__1;	/* fe80::1 link-scope localhost */
};

typedef ULONG (WINAPI *PGETADAPTERSADDRESSES)(
    ULONG Family,
    ULONG Flags,
    PVOID Reserved,
    PIP_ADAPTER_ADDRESSES AdapterAddresses,
    PULONG SizePointer
);

static	isc_boolean_t		use_GAA;
static	isc_boolean_t		use_GAA_determined;
static	HMODULE			hmod_iphlpapi;
static	PGETADAPTERSADDRESSES	pGAA;


/*
 * Size of buffer for SIO_GET_INTERFACE_LIST, in number of interfaces.
 * We assume no sane system will have more than than 1K of IP addresses on
 * all of its adapters.
 */
#define IFCONF_SIZE_INITIAL	  16
#define IFCONF_SIZE_INCREMENT	  64
#define IFCONF_SIZE_MAX		1040


/* Common utility functions */

/*
 * Windows always provides 255.255.255.255 as the the broadcast
 * address.  ntpd needs to know the broadcast address which will target
 * only that network interface, not all.  Reconstruct it from the
 * address and mask.
 */
static void
get_broadcastaddr(isc_netaddr_t *bcastaddr, isc_netaddr_t *addr, isc_netaddr_t *netmask) {

	isc_uint32_t *	b;
	isc_uint32_t	a, n;

	b = (isc_uint32_t *)&bcastaddr->type.in;
	a = *(isc_uint32_t *)&addr->type.in;
	n = *(isc_uint32_t *)&netmask->type.in;

	*b = a | ~n;
}

isc_result_t
isc_interfaceiter_create(isc_mem_t *mctx, isc_interfaceiter_t **iterp) {
	char strbuf[ISC_STRERRORSIZE];
	isc_interfaceiter_t *iter;
	isc_result_t result;
	unsigned int major;
	unsigned int minor;
	unsigned int spmajor;
	ULONG err;
	int tries;
	int error;
	unsigned long bytesReturned = 0;

	REQUIRE(mctx != NULL);
	REQUIRE(iterp != NULL);
	REQUIRE(*iterp == NULL);

	iter = isc_mem_get(mctx, sizeof(*iter));
	if (iter == NULL)
		return (ISC_R_NOMEMORY);

	InitSockets();

	iter->mctx = mctx;
	iter->ipaa = NULL;
	iter->buf4 = NULL;
	iter->buf6 = NULL;
	iter->pos4 = NULL;
	iter->ipaaCur = NULL;
	iter->ipuaCur = NULL;
	iter->ipaasize = 0;
	iter->pos6 = 0;
	iter->buf6size = 0;
	iter->buf4size = 0;
	iter->result = ISC_R_FAILURE;
	iter->numIF = 0;
	iter->v4IF = 0;

	/*
	 * Use GetAdaptersAddresses in preference to ioctls when running
	 * on Windows XP SP1 or later.  Earlier GetAdaptersAddresses do
	 * not appear to provide enough information to associate unicast
	 * addresses with their prefixes.
	 */
	if (!use_GAA_determined) {
		major = isc_win32os_majorversion();
		minor = isc_win32os_minorversion();
		spmajor = isc_win32os_servicepackmajor();
		if (major > 5 || (5 == major &&
		    (minor > 1 || (1 == minor && spmajor >= 1)))) {
			if (NULL == hmod_iphlpapi)
				hmod_iphlpapi = LoadLibrary("iphlpapi");
			if (NULL != hmod_iphlpapi)
				pGAA = (PGETADAPTERSADDRESSES)
				    GetProcAddress(
					hmod_iphlpapi,
					"GetAdaptersAddresses");
			if (NULL != pGAA)
				use_GAA = ISC_TRUE;
		}
		use_GAA_determined = ISC_TRUE;
	}

	if (!use_GAA)
		goto use_ioctls;

	iter->ipaasize = 16 * 1024;

	for (tries = 0; tries < 5; tries++) {
		iter->ipaa = isc_mem_reallocate(mctx, iter->ipaa,
						 iter->ipaasize);
		if (NULL == iter->ipaa) {
			result = ISC_R_NOMEMORY;
			goto put_iter;
		}
		err = (*pGAA)(
			AF_UNSPEC,
			GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST,
			NULL,
			iter->ipaa,
			&iter->ipaasize);
		if (NO_ERROR == err || ERROR_BUFFER_OVERFLOW != err)
			break;
	}

	if (NO_ERROR != err) {
		isc__strerror(err, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				"GetAdaptersAddresses: %s",
				strbuf);
		result = ISC_R_UNEXPECTED;
		goto gaa_failure;
	}
	
	iter->ipaaCur = iter->ipaa;
	goto success;

 use_ioctls:
	/*
	 * Create an unbound datagram socket to do the
	 * SIO_GET_INTERFACE_LIST WSAIoctl on.
	 */
	if ((iter->socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		error = WSAGetLastError();
		if (error == WSAEAFNOSUPPORT)
			goto inet6_only;
		isc__strerror(error, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				"making interface scan socket: %s",
				strbuf);
		result = ISC_R_UNEXPECTED;
		goto put_iter;
	}

	/*
	 * Get the interface configuration, allocating more memory if
	 * necessary.
	 */
	iter->buf4size = IFCONF_SIZE_INITIAL*sizeof(INTERFACE_INFO);

	for (;;) {
		iter->buf4 = isc_mem_get(mctx, iter->buf4size);
		if (iter->buf4 == NULL) {
			result = ISC_R_NOMEMORY;
			goto alloc_failure;
		}

		if (WSAIoctl(iter->socket, SIO_GET_INTERFACE_LIST,
			     0, 0, iter->buf4, iter->buf4size,
			     &bytesReturned, 0, 0) == SOCKET_ERROR)
		{
			error = WSAGetLastError();
			if (error != WSAEFAULT && error != WSAENOBUFS) {
				errno = error;
				isc__strerror(error, strbuf, sizeof(strbuf));
				UNEXPECTED_ERROR(__FILE__, __LINE__,
						"get interface configuration: %s",
						strbuf);
				result = ISC_R_UNEXPECTED;
				goto ioctl_failure;
			}
			/*
			 * EINVAL.  Retry with a bigger buffer.
			 */
		} else {
			/*
			 * The WSAIoctl succeeded.
			 * If the number of the returned bytes is the same
			 * as the buffer size, we will grow it just in
			 * case and retry.
			 */
			if (bytesReturned > 0 &&
			    (bytesReturned < iter->buf4size))
				break;
		}
		if (iter->buf4size >= IFCONF_SIZE_MAX*sizeof(INTERFACE_INFO)) {
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "get interface configuration: "
					 "maximum buffer size exceeded");
			result = ISC_R_UNEXPECTED;
			goto ioctl_failure;
		}
		isc_mem_put(mctx, iter->buf4, iter->buf4size);

		iter->buf4size += IFCONF_SIZE_INCREMENT *
			sizeof(INTERFACE_INFO);
	}

	/*
	 * A newly created iterator has an undefined position
	 * until isc_interfaceiter_first() is called.
	 */
	iter->v4IF = bytesReturned/sizeof(INTERFACE_INFO);

	/* We don't need the socket any more, so close it */
	closesocket(iter->socket);

 inet6_only:
	/*
	 * Create an unbound datagram socket to do the
	 * SIO_ADDRESS_LIST_QUERY WSAIoctl on.
	 */
	if ((iter->socket = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		error = WSAGetLastError();
		if (error == WSAEAFNOSUPPORT)
			goto success;
		isc__strerror(error, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				"making interface scan socket: %s",
				strbuf);
		result = ISC_R_UNEXPECTED;
		goto put_iter;
	}

	/*
	 * Get the interface configuration, allocating more memory if
	 * necessary.
	 */
	iter->buf6size = sizeof(SOCKET_ADDRESS_LIST) +
			 IFCONF_SIZE_INITIAL*sizeof(SOCKET_ADDRESS);

	for (;;) {
		iter->buf6 = isc_mem_get(mctx, iter->buf6size);
		if (iter->buf6 == NULL) {
			result = ISC_R_NOMEMORY;
			goto ioctl_failure;
		}

		if (WSAIoctl(iter->socket, SIO_ADDRESS_LIST_QUERY,
			     0, 0, iter->buf6, iter->buf6size,
			     &bytesReturned, 0, 0) == SOCKET_ERROR)
		{
			error = WSAGetLastError();
			if (error != WSAEFAULT && error != WSAENOBUFS) {
				errno = error;
				isc__strerror(error, strbuf, sizeof(strbuf));
				UNEXPECTED_ERROR(__FILE__, __LINE__,
						 "sio address list query: %s",
						 strbuf);
				result = ISC_R_UNEXPECTED;
				goto ioctl6_failure;
			}
			/*
			 * EINVAL.  Retry with a bigger buffer.
			 */
		} else
			break;

		if (iter->buf6size >= IFCONF_SIZE_MAX*sizeof(SOCKET_ADDRESS)) {
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "get interface configuration: "
					 "maximum buffer size exceeded");
			result = ISC_R_UNEXPECTED;
			goto ioctl6_failure;
		}
		isc_mem_put(mctx, iter->buf6, iter->buf6size);

		iter->buf6size += IFCONF_SIZE_INCREMENT *
			sizeof(SOCKET_ADDRESS);
	}

	/*
	 * initialize loop__1 to [::1] and loopfe80__1 to [fe80::1].
	 * used by internal_current6().
	 */
	memset(&iter->loop__1, 0, sizeof(iter->loop__1));
	memset(&iter->loopfe80__1, 0, sizeof(iter->loopfe80__1));
	iter->loop__1.s6_addr[15] = 1;
	iter->loopfe80__1.s6_addr[15] = 1;
	iter->loopfe80__1.s6_addr[0] = 0xfe;
	iter->loopfe80__1.s6_addr[1] = 0x80;

	closesocket(iter->socket);

 success:
	iter->magic = IFITER_MAGIC;
	*iterp = iter;
	return (ISC_R_SUCCESS);

 gaa_failure:
	isc_mem_put(mctx, iter->ipaa, iter->ipaasize);
	goto put_iter;

 ioctl6_failure:
	isc_mem_put(mctx, iter->buf6, iter->buf6size);

 ioctl_failure:
	if (iter->buf4 != NULL)
		isc_mem_put(mctx, iter->buf4, iter->buf4size);

 alloc_failure:
	if (iter->socket >= 0)
		(void) closesocket(iter->socket);

 put_iter:
	isc_mem_put(mctx, iter, sizeof(*iter));
	return (result);
}

static unsigned char
GAA_find_prefix(isc_interfaceiter_t *iter) {
	IP_ADAPTER_PREFIX *	ipap;
	IP_ADAPTER_PREFIX *	ipap_match;
	int			match_len;
	int			max_len;
	isc_netaddr_t		target;
	u_short			af;
	isc_netaddr_t		pfx;
	int			pfx_len;
	size_t			nbytes;
	unsigned char		nbits;
	unsigned char *		pbits;
	unsigned int		octets;

	match_len = 0;
	ipap_match = NULL;
	isc_netaddr_fromsockaddr(&target,
	    (isc_sockaddr_t *)iter->ipuaCur->Address.lpSockaddr);
	af = (u_short)target.family;
	INSIST(AF_INET == af || AF_INET6 == af);
	max_len = (AF_INET6 == af) ? 128 : 32;
	iter->current.netmask.family = af;
	for (ipap = iter->ipaaCur->FirstPrefix;
	     ipap != NULL;
	     ipap = ipap->Next) {
		if (ipap->Address.lpSockaddr->sa_family != af)
			continue;
		isc_netaddr_fromsockaddr(&pfx,
		    (isc_sockaddr_t *)ipap->Address.lpSockaddr);
		pfx_len = ipap->PrefixLength;
		INSIST(0 <= pfx_len && pfx_len <= max_len);
		if (pfx_len > match_len && pfx_len < max_len &&
		    isc_netaddr_eqprefix(&target, &pfx, pfx_len)) {
			ipap_match = ipap;
			match_len = pfx_len;
		}
	}
	if (NULL == ipap_match) {
		/* presume all-ones mask */
		if (AF_INET6 == af)
			octets = sizeof(iter->current.netmask.type.in6);
		else
			octets = sizeof(iter->current.netmask.type.in);
		memset(&iter->current.netmask.type, 0xFF, octets);
		return (8 * (unsigned char)octets);
	}
	nbytes = match_len / 8;
	nbits = match_len % 8;
	memset(&iter->current.netmask.type.in6, 0xFF, nbytes);
	pbits = (void *)&iter->current.netmask.type.in6;
	pbits += nbytes;
	*pbits |= 0xFF << (8 - nbits);
	return ((unsigned char)match_len);
}

static isc_result_t
internal_current_GAA(isc_interfaceiter_t *iter) {
	IP_ADAPTER_ADDRESSES *adap;
	IP_ADAPTER_UNICAST_ADDRESS *addr;
	unsigned char prefix_len;

	REQUIRE(iter->ipaaCur != NULL);
	REQUIRE(iter->ipuaCur != NULL);
	adap = iter->ipaaCur;
	addr = iter->ipuaCur;
	if (IpDadStatePreferred != addr->DadState)
		return (ISC_R_IGNORE);
	memset(&iter->current, 0, sizeof(iter->current));
	iter->current.af = addr->Address.lpSockaddr->sa_family;
	isc_netaddr_fromsockaddr(&iter->current.address,
	    (isc_sockaddr_t *)addr->Address.lpSockaddr);
	if (AF_INET6 == iter->current.af)
		iter->current.ifindex = adap->Ipv6IfIndex;
	iter->current.name[0] = '\0';
	WideCharToMultiByte(
		CP_ACP, 
		0, 
		adap->FriendlyName,
		-1,
		iter->current.name,
		sizeof(iter->current.name),
		NULL,
		NULL);
	iter->current.name[sizeof(iter->current.name) - 1] = '\0';
	if (IfOperStatusUp == adap->OperStatus)
		iter->current.flags |= INTERFACE_F_UP;
	if (IF_TYPE_PPP == adap->IfType)
		iter->current.flags |= INTERFACE_F_POINTTOPOINT;
	else if (IF_TYPE_SOFTWARE_LOOPBACK == adap->IfType)
		iter->current.flags |= INTERFACE_F_LOOPBACK;
	if ((IP_ADAPTER_NO_MULTICAST & adap->Flags) == 0)
		iter->current.flags |= INTERFACE_F_MULTICAST;
	if (IpSuffixOriginRandom == addr->SuffixOrigin)
		iter->current.flags |= INTERFACE_F_PRIVACY;

	prefix_len = GAA_find_prefix(iter);
	/* I'm failing to see a broadcast flag via GAA */
	if (AF_INET == iter->current.af && prefix_len < 32 &&
	    (INTERFACE_F_LOOPBACK & iter->current.flags) == 0) {
		iter->current.flags |= INTERFACE_F_BROADCAST;
		get_broadcastaddr(&iter->current.broadcast,
				  &iter->current.address,
				  &iter->current.netmask);
	}
	return (ISC_R_SUCCESS);
}

/*
 * Get information about the current interface to iter->current.
 * If successful, return ISC_R_SUCCESS.
 * If the interface has an unsupported address family, or if
 * some operation on it fails, return ISC_R_IGNORE to make
 * the higher-level iterator code ignore it.
 */

static isc_result_t
internal_current(isc_interfaceiter_t *iter) {
	BOOL ifNamed = FALSE;
	unsigned long flags;

	REQUIRE(VALID_IFITER(iter));
	REQUIRE(iter->numIF >= 0);

	memset(&iter->current, 0, sizeof(iter->current));
	iter->current.af = AF_INET;

	isc_netaddr_fromsockaddr(&iter->current.address,
	    (isc_sockaddr_t *)&(iter->IFData.iiAddress));

	/*
	 * Get interface flags.
	 */

	iter->current.flags = 0;
	flags = iter->IFData.iiFlags;

	if ((flags & IFF_UP) != 0)
		iter->current.flags |= INTERFACE_F_UP;

	if ((flags & IFF_BROADCAST) != 0)
		iter->current.flags |= INTERFACE_F_BROADCAST;

	if ((flags & IFF_MULTICAST) != 0)
		iter->current.flags |= INTERFACE_F_MULTICAST;

	if ((flags & IFF_POINTTOPOINT) != 0) {
		iter->current.flags |= INTERFACE_F_POINTTOPOINT;
		snprintf(iter->current.name, sizeof(iter->current.name),
			 "PPP %d", iter->numIF);
		ifNamed = TRUE;
	}

	if ((flags & IFF_LOOPBACK) != 0) {
		iter->current.flags |= INTERFACE_F_LOOPBACK;
		snprintf(iter->current.name, sizeof(iter->current.name),
			"v4loop %d", iter->numIF);
		ifNamed = TRUE;
	}

	/*
	 * If the interface is point-to-point, get the destination address.
	 */
	if ((iter->current.flags & INTERFACE_F_POINTTOPOINT) != 0)
		isc_netaddr_fromsockaddr(&iter->current.dstaddress,
		    (isc_sockaddr_t *)&(iter->IFData.iiBroadcastAddress));

	/*
	 * Get the network mask.
	 */
	isc_netaddr_fromsockaddr(&iter->current.netmask,
	    (isc_sockaddr_t *)&(iter->IFData.iiNetmask));

	/*
	 * If the interface is broadcast, get the broadcast address,
	 * based on the unicast address and network mask.
	 */
	if ((iter->current.flags & INTERFACE_F_BROADCAST) != 0)
		get_broadcastaddr(&iter->current.broadcast,
				  &iter->current.address,
				  &iter->current.netmask);

	if (ifNamed == FALSE)
		snprintf(iter->current.name, sizeof(iter->current.name),
			"IPv4 %d", iter->numIF);

	return (ISC_R_SUCCESS);
}

static isc_result_t
internal_current6(isc_interfaceiter_t *iter) {
	BOOL ifNamed = FALSE;
	struct sockaddr_in6 *psa6;
	BOOL localhostSeen;
	int i;

	REQUIRE(VALID_IFITER(iter));
	REQUIRE(iter->pos6 >= 0);
	REQUIRE(iter->buf6 != 0);

	memset(&iter->current, 0, sizeof(iter->current));
	iter->current.af = AF_INET6;

	/*
	 * synthesize localhost ::1 before returning the rest, if ::1
	 * is not on the list.
	 */
	if (iter->pos6 >= (unsigned)iter->buf6->iAddressCount) {
		localhostSeen = FALSE;
		for (i = 0; i < iter->buf6->iAddressCount; i++) {
			psa6 = (struct sockaddr_in6 *)
			       iter->buf6->Address[i].lpSockaddr;
			if (!memcmp(&iter->loop__1, &psa6->sin6_addr,
				    sizeof(iter->loop__1))) {
				localhostSeen = TRUE;
				break;
			}
		}
		if (localhostSeen)
			iter->pos6 = iter->buf6->iAddressCount - 1;
	}

	if (iter->pos6 < (unsigned)iter->buf6->iAddressCount) {
		isc_netaddr_fromsockaddr(&iter->current.address,
		    (isc_sockaddr_t *)iter->buf6->Address[iter->pos6].lpSockaddr);
	} else {
		iter->current.address.family = AF_INET6;
		memcpy(&iter->current.address.type.in6, &iter->loop__1,
		       sizeof(iter->current.address.type.in6));
	}

	/*
	 * Get interface flags.
	 */

	iter->current.flags = INTERFACE_F_UP | INTERFACE_F_MULTICAST;

	if (!memcmp(&iter->current.address.type.in6, &iter->loop__1,
		    sizeof(iter->current.address.type.in6)) ||
	    !memcmp(&iter->current.address.type.in6, &iter->loopfe80__1,
	            sizeof(iter->current.address.type.in6))) {

		iter->current.flags |= INTERFACE_F_LOOPBACK;
		snprintf(iter->current.name, sizeof(iter->current.name),
			 "v6loop %d",
			 iter->buf6->iAddressCount - iter->pos6);
		ifNamed = TRUE;
	}

	if (ifNamed == FALSE)
		snprintf(iter->current.name, sizeof(iter->current.name),
			 "IPv6 %d",
			 iter->buf6->iAddressCount - iter->pos6);

	memset(iter->current.netmask.type.in6.s6_addr, 0xff,
	       sizeof(iter->current.netmask.type.in6.s6_addr));
	iter->current.netmask.family = AF_INET6;
	return (ISC_R_SUCCESS);
}

static isc_result_t
internal_next_GAA(isc_interfaceiter_t *iter) {
	REQUIRE(use_GAA);
	if (NULL == iter->ipaaCur)
		return (ISC_R_NOMORE);
	if (NULL == iter->ipuaCur)
		iter->ipuaCur = iter->ipaaCur->FirstUnicastAddress;
	else
		iter->ipuaCur = iter->ipuaCur->Next;
	while (NULL == iter->ipuaCur) {
		iter->ipaaCur = iter->ipaaCur->Next;
		if (NULL == iter->ipaaCur)
			return (ISC_R_NOMORE);
		iter->ipuaCur = iter->ipaaCur->FirstUnicastAddress;
	}
	return (ISC_R_SUCCESS);
}

/*
 * Step the iterator to the next interface.  Unlike
 * isc_interfaceiter_next(), this may leave the iterator
 * positioned on an interface that will ultimately
 * be ignored.  Return ISC_R_NOMORE if there are no more
 * interfaces, otherwise ISC_R_SUCCESS.
 */
static isc_result_t
internal_next(isc_interfaceiter_t *iter) {
	if (iter->numIF >= iter->v4IF)
		return (ISC_R_NOMORE);

	/*
	 * The first one needs to be set up to point to the last
	 * Element of the array.  Go to the end and back up
	 * Microsoft's implementation is peculiar for returning
	 * the list in reverse order
	 */

	if (iter->numIF == 0)
		iter->pos4 = (INTERFACE_INFO *)(iter->buf4 + (iter->v4IF));

	iter->pos4--;
	if (&(iter->pos4) < &(iter->buf4))
		return (ISC_R_NOMORE);

	memset(&(iter->IFData), 0, sizeof(INTERFACE_INFO));
	memcpy(&(iter->IFData), iter->pos4, sizeof(INTERFACE_INFO));
	iter->numIF++;

	return (ISC_R_SUCCESS);
}

static isc_result_t
internal_next6(isc_interfaceiter_t *iter) {
	if (iter->pos6 == 0)
		return (ISC_R_NOMORE);
	iter->pos6--;
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_interfaceiter_current(isc_interfaceiter_t *iter,
			  isc_interface_t *ifdata) {
	REQUIRE(iter->result == ISC_R_SUCCESS);
	memcpy(ifdata, &iter->current, sizeof(*ifdata));
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_interfaceiter_first(isc_interfaceiter_t *iter) {
	REQUIRE(VALID_IFITER(iter));
	REQUIRE(use_GAA_determined);
	/*
	 * SIO_ADDRESS_LIST_QUERY (used to query IPv6 addresses)
	 * intentionally omits localhost addresses [::1] and [::fe80] in
	 * some cases.  ntpd depends on enumerating [::1] to listen on
	 * it, and ntpq and ntpdc default to "localhost" as the target,
	 * so they will attempt to talk to [::1]:123 and fail. This
	 * means we need to synthesize ::1, which we will do first,
	 * hence iAddressCount + 1.  internal_next6() will decrement
	 * it before the first use as an index, and internal_current6()
	 * will treat pos6 == iAddressCount as a sign to synthesize
	 * [::1] if needed.
	 */
	if (!use_GAA && iter->buf6 != NULL)
		iter->pos6 = iter->buf6->iAddressCount + 1;
	iter->result = ISC_R_SUCCESS;
	return (isc_interfaceiter_next(iter));
}

isc_result_t
isc_interfaceiter_next(isc_interfaceiter_t *iter) {
	isc_result_t result;

	REQUIRE(VALID_IFITER(iter));
	REQUIRE(iter->result == ISC_R_SUCCESS);
	REQUIRE(use_GAA_determined);

	if (use_GAA) {
		do {
			result = internal_next_GAA(iter);
			if (ISC_R_NOMORE == result)
				goto set_result;
			result = internal_current_GAA(iter);
		} while (ISC_R_IGNORE == result);
		goto set_result;
	}

	for (;;) {
		result = internal_next(iter);
		if (result == ISC_R_NOMORE) {
			result = internal_next6(iter);
			if (result != ISC_R_SUCCESS)
				break;
			result = internal_current6(iter);
			if (result != ISC_R_IGNORE)
				break;
		} else if (result != ISC_R_SUCCESS)
			break;
		result = internal_current(iter);
		if (result != ISC_R_IGNORE)
			break;
	}
 set_result:
	iter->result = result;
	return (result);
}

void
isc_interfaceiter_destroy(isc_interfaceiter_t **iterp) {
	isc_interfaceiter_t *iter;

	REQUIRE(iterp != NULL);
	iter = *iterp;
	REQUIRE(VALID_IFITER(iter));
	REQUIRE(use_GAA_determined);

	if (use_GAA) {
		REQUIRE(NULL == iter->buf4);
		REQUIRE(NULL == iter->buf4);
		if (iter->ipaa != NULL)
			isc_mem_put(iter->mctx, iter->ipaa, iter->ipaasize);
	} else {
		REQUIRE(NULL == iter->ipaa);
		if (iter->buf4 != NULL)
			isc_mem_put(iter->mctx, iter->buf4, iter->buf4size);
		if (iter->buf6 != NULL)
			isc_mem_put(iter->mctx, iter->buf6, iter->buf6size);
	}

	iter->magic = 0;
	isc_mem_put(iter->mctx, iter, sizeof(*iter));
	*iterp = NULL;
}
