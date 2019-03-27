/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: ifiter_sysctl.c,v 1.25 2007/06/19 23:47:18 tbox Exp $ */

/*! \file
 * \brief
 * Obtain the list of network interfaces using sysctl.
 * See TCP/IP Illustrated Volume 2, sections 19.8, 19.14,
 * and 19.16.
 */

#include <sys/param.h>
#include <sys/sysctl.h>

#include <net/route.h>
#include <net/if_dl.h>

/* XXX what about Alpha? */
#ifdef sgi
#define ROUNDUP(a) ((a) > 0 ? \
		(1 + (((a) - 1) | (sizeof(__uint64_t) - 1))) : \
		sizeof(__uint64_t))
#else
#define ROUNDUP(a) ((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) \
                    : sizeof(long))
#endif

#define IFITER_MAGIC		ISC_MAGIC('I', 'F', 'I', 'S')
#define VALID_IFITER(t)		ISC_MAGIC_VALID(t, IFITER_MAGIC)

struct isc_interfaceiter {
	unsigned int		magic;		/* Magic number. */
	isc_mem_t		*mctx;
	void			*buf;		/* Buffer for sysctl data. */
	unsigned int		bufsize;	/* Bytes allocated. */
	unsigned int		bufused;	/* Bytes used. */
	unsigned int		pos;		/* Current offset in
						   sysctl data. */
	isc_interface_t		current;	/* Current interface data. */
	isc_result_t		result;		/* Last result code. */
};

static int mib[6] = {
	CTL_NET,
	PF_ROUTE,
        0,
	0, 			/* Any address family. */
        NET_RT_IFLIST,
	0 			/* Flags. */
};

isc_result_t
isc_interfaceiter_create(isc_mem_t *mctx, isc_interfaceiter_t **iterp) {
	isc_interfaceiter_t *iter;
	isc_result_t result;
	size_t bufsize;
	size_t bufused;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(mctx != NULL);
	REQUIRE(iterp != NULL);
	REQUIRE(*iterp == NULL);

	iter = isc_mem_get(mctx, sizeof(*iter));
	if (iter == NULL)
		return (ISC_R_NOMEMORY);

	iter->mctx = mctx;
	iter->buf = 0;

	/*
	 * Determine the amount of memory needed.
	 */
	bufsize = 0;
	if (sysctl(mib, 6, NULL, &bufsize, NULL, (size_t) 0) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_IFITERSYSCTL,
						ISC_MSG_GETIFLISTSIZE,
						"getting interface "
						"list size: sysctl: %s"),
				 strbuf);
		result = ISC_R_UNEXPECTED;
		goto failure;
	}
	iter->bufsize = bufsize;

	iter->buf = isc_mem_get(iter->mctx, iter->bufsize);
	if (iter->buf == NULL) {
		result = ISC_R_NOMEMORY;
		goto failure;
	}

	bufused = bufsize;
	if (sysctl(mib, 6, iter->buf, &bufused, NULL, (size_t) 0) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_IFITERSYSCTL,
						ISC_MSG_GETIFLIST,
						"getting interface list: "
						"sysctl: %s"),
				 strbuf);
		result = ISC_R_UNEXPECTED;
		goto failure;
	}
	iter->bufused = bufused;
	INSIST(iter->bufused <= iter->bufsize);

	/*
	 * A newly created iterator has an undefined position
	 * until isc_interfaceiter_first() is called.
	 */
	iter->pos = (unsigned int) -1;
	iter->result = ISC_R_FAILURE;

	iter->magic = IFITER_MAGIC;
	*iterp = iter;
	return (ISC_R_SUCCESS);

 failure:
	if (iter->buf != NULL)
		isc_mem_put(mctx, iter->buf, iter->bufsize);
	isc_mem_put(mctx, iter, sizeof(*iter));
	return (result);
}

/*
 * Get information about the current interface to iter->current.
 * If successful, return ISC_R_SUCCESS.
 * If the interface has an unsupported address family,
 * return ISC_R_IGNORE.  In case of other failure,
 * return ISC_R_UNEXPECTED.
 */

static isc_result_t
internal_current(isc_interfaceiter_t *iter) {
	struct ifa_msghdr *ifam, *ifam_end;

	REQUIRE(VALID_IFITER(iter));
	REQUIRE (iter->pos < (unsigned int) iter->bufused);

	ifam = (struct ifa_msghdr *) ((char *) iter->buf + iter->pos);
	ifam_end = (struct ifa_msghdr *) ((char *) iter->buf + iter->bufused);

	// Skip wrong RTM version headers
	if (ifam->ifam_version != RTM_VERSION)
		return (ISC_R_IGNORE);

	if (ifam->ifam_type == RTM_IFINFO) {
		struct if_msghdr *ifm = (struct if_msghdr *) ifam;
		struct sockaddr_dl *sdl = (struct sockaddr_dl *) (ifm + 1);
		unsigned int namelen;

		memset(&iter->current, 0, sizeof(iter->current));

		iter->current.ifindex = sdl->sdl_index;
		namelen = sdl->sdl_nlen;
		if (namelen > sizeof(iter->current.name) - 1)
			namelen = sizeof(iter->current.name) - 1;

		memset(iter->current.name, 0, sizeof(iter->current.name));
		memcpy(iter->current.name, sdl->sdl_data, namelen);

		iter->current.flags = 0;

		if ((ifam->ifam_flags & IFF_UP) != 0)
			iter->current.flags |= INTERFACE_F_UP;

		if ((ifam->ifam_flags & IFF_POINTOPOINT) != 0)
			iter->current.flags |= INTERFACE_F_POINTTOPOINT;

		if ((ifam->ifam_flags & IFF_LOOPBACK) != 0)
			iter->current.flags |= INTERFACE_F_LOOPBACK;

		if ((ifam->ifam_flags & IFF_BROADCAST) != 0)
			iter->current.flags |= INTERFACE_F_BROADCAST;

#ifdef IFF_MULTICAST
		if ((ifam->ifam_flags & IFF_MULTICAST) != 0)
			iter->current.flags |= INTERFACE_F_MULTICAST;
#endif

		/*
		 * This is not an interface address.
		 * Force another iteration.
		 */
		return (ISC_R_IGNORE);
	} else if (ifam->ifam_type == RTM_NEWADDR) {
		int i;
		int family;
		struct sockaddr *mask_sa = NULL;
		struct sockaddr *addr_sa = NULL;
		struct sockaddr *dst_sa = NULL;

		struct sockaddr *sa = (struct sockaddr *)(ifam + 1);
		family = sa->sa_family;

		for (i = 0; i < RTAX_MAX; i++)
		{
			if ((ifam->ifam_addrs & (1 << i)) == 0)
				continue;

			INSIST(sa < (struct sockaddr *) ifam_end);

			switch (i) {
			case RTAX_NETMASK: /* Netmask */
				mask_sa = sa;
				break;
			case RTAX_IFA: /* Interface address */
				addr_sa = sa;
				break;
			case RTAX_BRD: /* Broadcast or destination address */
				dst_sa = sa;
				break;
			}
#ifdef ISC_PLATFORM_HAVESALEN
			sa = (struct sockaddr *)((char*)(sa)
					 + ROUNDUP(sa->sa_len));
#else
#ifdef sgi
			/*
			 * Do as the contributed SGI code does.
			 */
			sa = (struct sockaddr *)((char*)(sa)
					 + ROUNDUP(_FAKE_SA_LEN_DST(sa)));
#else
			/* XXX untested. */
			sa = (struct sockaddr *)((char*)(sa)
					 + ROUNDUP(sizeof(struct sockaddr)));
#endif
#endif
		}

		if (addr_sa == NULL)
			return (ISC_R_IGNORE);

		family = addr_sa->sa_family;
		if (family != AF_INET && family != AF_INET6)
			return (ISC_R_IGNORE);

		iter->current.af = family;

		get_addr(family, &iter->current.address, addr_sa,
			 iter->current.name);

		if (mask_sa != NULL)
			get_addr(family, &iter->current.netmask, mask_sa,
				 iter->current.name);

		if (dst_sa != NULL &&
		    (iter->current.flags & INTERFACE_F_POINTTOPOINT) != 0)
			get_addr(family, &iter->current.dstaddress, dst_sa,
				 iter->current.name);

		if (dst_sa != NULL &&
		    (iter->current.flags & INTERFACE_F_BROADCAST) != 0)
			get_addr(family, &iter->current.broadcast, dst_sa,
				 iter->current.name);

		return (ISC_R_SUCCESS);
	} else {
		printf("%s", isc_msgcat_get(isc_msgcat,
					    ISC_MSGSET_IFITERSYSCTL,
					    ISC_MSG_UNEXPECTEDTYPE,
					    "warning: unexpected interface "
					    "list message type\n"));
		return (ISC_R_IGNORE);
	}
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
	struct ifa_msghdr *ifam;
	REQUIRE (iter->pos < (unsigned int) iter->bufused);

	ifam = (struct ifa_msghdr *) ((char *) iter->buf + iter->pos);

	iter->pos += ifam->ifam_msglen;

	if (iter->pos >= iter->bufused)
		return (ISC_R_NOMORE);

	return (ISC_R_SUCCESS);
}

static void
internal_destroy(isc_interfaceiter_t *iter) {
	UNUSED(iter); /* Unused. */
	/*
	 * Do nothing.
	 */
}

static
void internal_first(isc_interfaceiter_t *iter) {
	iter->pos = 0;
}
