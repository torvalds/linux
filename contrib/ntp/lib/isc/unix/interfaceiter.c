/*
 * Copyright (C) 2004, 2005, 2007, 2008  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: interfaceiter.c,v 1.45 2008/12/01 03:51:47 marka Exp $ */

/*! \file */

#include <config.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>		/* Required for ifiter_ioctl.c. */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <isc/interfaceiter.h>
#include <isc/log.h>
#include <isc/magic.h>
#include <isc/mem.h>
#include <isc/msgs.h>
#include <isc/net.h>
#include <isc/print.h>
#include <isc/result.h>
#include <isc/strerror.h>
#include <isc/string.h>
#include <isc/types.h>
#include <isc/util.h>

/* Must follow <isc/net.h>. */
#ifdef HAVE_NET_IF6_H
#include <net/if6.h>
#endif
#include <net/if.h>

#ifdef HAVE_LINUX_IF_ADDR_H
# include <linux/if_addr.h>
#endif

/* Common utility functions */

/*%
 * Extract the network address part from a "struct sockaddr".
 * \brief
 * The address family is given explicitly
 * instead of using src->sa_family, because the latter does not work
 * for copying a network mask obtained by SIOCGIFNETMASK (it does
 * not have a valid address family).
 */

static void
get_addr(unsigned int family, isc_netaddr_t *dst, struct sockaddr *src,
	 char *ifname)
{
	struct sockaddr_in6 *sa6;

#if !defined(ISC_PLATFORM_HAVEIFNAMETOINDEX) || \
    !defined(ISC_PLATFORM_HAVESCOPEID)
	UNUSED(ifname);
#endif

	/* clear any remaining value for safety */
	memset(dst, 0, sizeof(*dst));

	dst->family = family;
	switch (family) {
	case AF_INET:
		memcpy(&dst->type.in,
		       &((struct sockaddr_in *)(void *)src)->sin_addr,
		       sizeof(struct in_addr));
		break;
	case AF_INET6:
		sa6 = (struct sockaddr_in6 *)(void *)src;
		memcpy(&dst->type.in6, &sa6->sin6_addr,
		       sizeof(struct in6_addr));
#ifdef ISC_PLATFORM_HAVESCOPEID
		if (sa6->sin6_scope_id != 0)
			isc_netaddr_setzone(dst, sa6->sin6_scope_id);
		else {
			/*
			 * BSD variants embed scope zone IDs in the 128bit
			 * address as a kernel internal form.  Unfortunately,
			 * the embedded IDs are not hidden from applications
			 * when getting access to them by sysctl or ioctl.
			 * We convert the internal format to the pure address
			 * part and the zone ID part.
			 * Since multicast addresses should not appear here
			 * and they cannot be distinguished from netmasks,
			 * we only consider unicast link-local addresses.
			 */
			if (IN6_IS_ADDR_LINKLOCAL(&sa6->sin6_addr)) {
				isc_uint16_t zone16;

				memcpy(&zone16, &sa6->sin6_addr.s6_addr[2],
				       sizeof(zone16));
				zone16 = ntohs(zone16);
				if (zone16 != 0) {
					/* the zone ID is embedded */
					isc_netaddr_setzone(dst,
							    (isc_uint32_t)zone16);
					dst->type.in6.s6_addr[2] = 0;
					dst->type.in6.s6_addr[3] = 0;
#ifdef ISC_PLATFORM_HAVEIFNAMETOINDEX
				} else if (ifname != NULL) {
					unsigned int zone;

					/*
					 * sin6_scope_id is still not provided,
					 * but the corresponding interface name
					 * is know.  Use the interface ID as
					 * the link ID.
					 */
					zone = if_nametoindex(ifname);
					if (zone != 0) {
						isc_netaddr_setzone(dst,
								    (isc_uint32_t)zone);
					}
#endif
				}
			}
		}
#endif
		break;
	default:
		INSIST(0);
		break;
	}
}

/*
 * Include system-dependent code.
 */

#ifdef __linux
#define ISC_IF_INET6_SZ \
    sizeof("00000000000000000000000000000001 01 80 10 80 XXXXXXloXXXXXXXX\n")
static isc_result_t linux_if_inet6_next(isc_interfaceiter_t *);
static isc_result_t linux_if_inet6_current(isc_interfaceiter_t *);
static void linux_if_inet6_first(isc_interfaceiter_t *iter);
#endif

#if HAVE_GETIFADDRS
#include "ifiter_getifaddrs.c"
#elif HAVE_IFLIST_SYSCTL
#include "ifiter_sysctl.c"
#else
#include "ifiter_ioctl.c"
#endif

#ifdef __linux
static void
linux_if_inet6_first(isc_interfaceiter_t *iter) {
	if (iter->proc != NULL) {
		rewind(iter->proc);
		(void)linux_if_inet6_next(iter);
	} else
		iter->valid = ISC_R_NOMORE;
}

static isc_result_t
linux_if_inet6_next(isc_interfaceiter_t *iter) {
	if (iter->proc != NULL &&
	    fgets(iter->entry, sizeof(iter->entry), iter->proc) != NULL)
		iter->valid = ISC_R_SUCCESS;
	else
		iter->valid = ISC_R_NOMORE;
	return (iter->valid);
}

static isc_result_t
linux_if_inet6_current(isc_interfaceiter_t *iter) {
	char address[33];
	char name[IF_NAMESIZE+1];
	struct in6_addr addr6;
	unsigned int ifindex;
	int prefix, scope, flags;
	int res;
	unsigned int i;

	if (iter->valid != ISC_R_SUCCESS)
		return (iter->valid);
	if (iter->proc == NULL) {
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_INTERFACE, ISC_LOG_ERROR,
			      "/proc/net/if_inet6:iter->proc == NULL");
		return (ISC_R_FAILURE);
	}

	res = sscanf(iter->entry, "%32[a-f0-9] %x %x %x %x %16s\n",
		     address, &ifindex, &prefix, &scope, &flags, name);
	if (res != 6) {
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_INTERFACE, ISC_LOG_ERROR,
			      "/proc/net/if_inet6:sscanf() -> %d (expected 6)",
			      res);
		return (ISC_R_FAILURE);
	}
	if (strlen(address) != 32) {
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_INTERFACE, ISC_LOG_ERROR,
			      "/proc/net/if_inet6:strlen(%s) != 32", address);
		return (ISC_R_FAILURE);
	}
	/*
	** Ignore DAD addresses --
	** we can't bind to them until they are resolved
	*/
#ifdef IFA_F_TENTATIVE
	if (flags & IFA_F_TENTATIVE)
		return (ISC_R_IGNORE);
#endif

	for (i = 0; i < 16; i++) {
		unsigned char byte;
		static const char hex[] = "0123456789abcdef";
		byte = ((strchr(hex, address[i * 2]) - hex) << 4) |
		       (strchr(hex, address[i * 2 + 1]) - hex);
		addr6.s6_addr[i] = byte;
	}
	iter->current.af = AF_INET6;
	iter->current.flags = INTERFACE_F_UP;
	isc_netaddr_fromin6(&iter->current.address, &addr6);
	iter->current.ifindex = ifindex;
	if (isc_netaddr_islinklocal(&iter->current.address)) {
		isc_netaddr_setzone(&iter->current.address,
				    (isc_uint32_t)ifindex);
	}
	for (i = 0; i < 16; i++) {
		if (prefix > 8) {
			addr6.s6_addr[i] = 0xff;
			prefix -= 8;
		} else {
			addr6.s6_addr[i] = (0xff << (8 - prefix)) & 0xff;
			prefix = 0;
		}
	}
	isc_netaddr_fromin6(&iter->current.netmask, &addr6);
	strncpy(iter->current.name, name, sizeof(iter->current.name));
	return (ISC_R_SUCCESS);
}
#endif

/*
 * The remaining code is common to the sysctl and ioctl case.
 */

isc_result_t
isc_interfaceiter_current(isc_interfaceiter_t *iter,
			  isc_interface_t *ifdata)
{
	REQUIRE(iter->result == ISC_R_SUCCESS);
	memcpy(ifdata, &iter->current, sizeof(*ifdata));
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_interfaceiter_first(isc_interfaceiter_t *iter) {
	isc_result_t result;

	REQUIRE(VALID_IFITER(iter));

	internal_first(iter);
	for (;;) {
		result = internal_current(iter);
		if (result != ISC_R_IGNORE)
			break;
		result = internal_next(iter);
		if (result != ISC_R_SUCCESS)
			break;
	}
	iter->result = result;
	return (result);
}

isc_result_t
isc_interfaceiter_next(isc_interfaceiter_t *iter) {
	isc_result_t result;

	REQUIRE(VALID_IFITER(iter));
	REQUIRE(iter->result == ISC_R_SUCCESS);

	for (;;) {
		result = internal_next(iter);
		if (result != ISC_R_SUCCESS)
			break;
		result = internal_current(iter);
		if (result != ISC_R_IGNORE)
			break;
	}
	iter->result = result;
	return (result);
}

void
isc_interfaceiter_destroy(isc_interfaceiter_t **iterp)
{
	isc_interfaceiter_t *iter;
	REQUIRE(iterp != NULL);
	iter = *iterp;
	REQUIRE(VALID_IFITER(iter));

	internal_destroy(iter);
	if (iter->buf != NULL)
		isc_mem_put(iter->mctx, iter->buf, iter->bufsize);

	iter->magic = 0;
	isc_mem_put(iter->mctx, iter, sizeof(*iter));
	*iterp = NULL;
}
