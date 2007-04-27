/* RTNETLINK client
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_addr.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <net/netlink.h>
#include "internal.h"

struct afs_rtm_desc {
	struct socket		*nlsock;
	struct afs_interface	*bufs;
	u8			*mac;
	size_t			nbufs;
	size_t			maxbufs;
	void			*data;
	ssize_t			datalen;
	size_t			datamax;
	int			msg_seq;
	unsigned		mac_index;
	bool			wantloopback;
	int (*parse)(struct afs_rtm_desc *, struct nlmsghdr *);
};

/*
 * parse an RTM_GETADDR response
 */
static int afs_rtm_getaddr_parse(struct afs_rtm_desc *desc,
				 struct nlmsghdr *nlhdr)
{
	struct afs_interface *this;
	struct ifaddrmsg *ifa;
	struct rtattr *rtattr;
	const char *name;
	size_t len;

	ifa = (struct ifaddrmsg *) NLMSG_DATA(nlhdr);

	_enter("{ix=%d,af=%d}", ifa->ifa_index, ifa->ifa_family);

	if (ifa->ifa_family != AF_INET) {
		_leave(" = 0 [family %d]", ifa->ifa_family);
		return 0;
	}
	if (desc->nbufs >= desc->maxbufs) {
		_leave(" = 0 [max %zu/%zu]", desc->nbufs, desc->maxbufs);
		return 0;
	}

	this = &desc->bufs[desc->nbufs];

	this->index = ifa->ifa_index;
	this->netmask.s_addr = inet_make_mask(ifa->ifa_prefixlen);
	this->mtu = 0;

	rtattr = NLMSG_DATA(nlhdr) + NLMSG_ALIGN(sizeof(struct ifaddrmsg));
	len = NLMSG_PAYLOAD(nlhdr, sizeof(struct ifaddrmsg));

	name = "unknown";
	for (; RTA_OK(rtattr, len); rtattr = RTA_NEXT(rtattr, len)) {
		switch (rtattr->rta_type) {
		case IFA_ADDRESS:
			memcpy(&this->address, RTA_DATA(rtattr), 4);
			break;
		case IFA_LABEL:
			name = RTA_DATA(rtattr);
			break;
		}
	}

	_debug("%s: "NIPQUAD_FMT"/"NIPQUAD_FMT,
	       name, NIPQUAD(this->address), NIPQUAD(this->netmask));

	desc->nbufs++;
	_leave(" = 0");
	return 0;
}

/*
 * parse an RTM_GETLINK response for MTUs
 */
static int afs_rtm_getlink_if_parse(struct afs_rtm_desc *desc,
				    struct nlmsghdr *nlhdr)
{
	struct afs_interface *this;
	struct ifinfomsg *ifi;
	struct rtattr *rtattr;
	const char *name;
	size_t len, loop;

	ifi = (struct ifinfomsg *) NLMSG_DATA(nlhdr);

	_enter("{ix=%d}", ifi->ifi_index);

	for (loop = 0; loop < desc->nbufs; loop++) {
		this = &desc->bufs[loop];
		if (this->index == ifi->ifi_index)
			goto found;
	}

	_leave(" = 0 [no match]");
	return 0;

found:
	if (ifi->ifi_type == ARPHRD_LOOPBACK && !desc->wantloopback) {
		_leave(" = 0 [loopback]");
		return 0;
	}

	rtattr = NLMSG_DATA(nlhdr) + NLMSG_ALIGN(sizeof(struct ifinfomsg));
	len = NLMSG_PAYLOAD(nlhdr, sizeof(struct ifinfomsg));

	name = "unknown";
	for (; RTA_OK(rtattr, len); rtattr = RTA_NEXT(rtattr, len)) {
		switch (rtattr->rta_type) {
		case IFLA_MTU:
			memcpy(&this->mtu, RTA_DATA(rtattr), 4);
			break;
		case IFLA_IFNAME:
			name = RTA_DATA(rtattr);
			break;
		}
	}

	_debug("%s: "NIPQUAD_FMT"/"NIPQUAD_FMT" mtu %u",
	       name, NIPQUAD(this->address), NIPQUAD(this->netmask),
	       this->mtu);

	_leave(" = 0");
	return 0;
}

/*
 * parse an RTM_GETLINK response for the MAC address belonging to the lowest
 * non-internal interface
 */
static int afs_rtm_getlink_mac_parse(struct afs_rtm_desc *desc,
				     struct nlmsghdr *nlhdr)
{
	struct ifinfomsg *ifi;
	struct rtattr *rtattr;
	const char *name;
	size_t remain, len;
	bool set;

	ifi = (struct ifinfomsg *) NLMSG_DATA(nlhdr);

	_enter("{ix=%d}", ifi->ifi_index);

	if (ifi->ifi_index >= desc->mac_index) {
		_leave(" = 0 [high]");
		return 0;
	}
	if (ifi->ifi_type == ARPHRD_LOOPBACK) {
		_leave(" = 0 [loopback]");
		return 0;
	}

	rtattr = NLMSG_DATA(nlhdr) + NLMSG_ALIGN(sizeof(struct ifinfomsg));
	remain = NLMSG_PAYLOAD(nlhdr, sizeof(struct ifinfomsg));

	name = "unknown";
	set = false;
	for (; RTA_OK(rtattr, remain); rtattr = RTA_NEXT(rtattr, remain)) {
		switch (rtattr->rta_type) {
		case IFLA_ADDRESS:
			len = RTA_PAYLOAD(rtattr);
			memcpy(desc->mac, RTA_DATA(rtattr),
			       min_t(size_t, len, 6));
			desc->mac_index = ifi->ifi_index;
			set = true;
			break;
		case IFLA_IFNAME:
			name = RTA_DATA(rtattr);
			break;
		}
	}

	if (set)
		_debug("%s: %02x:%02x:%02x:%02x:%02x:%02x",
		       name,
		       desc->mac[0], desc->mac[1], desc->mac[2],
		       desc->mac[3], desc->mac[4], desc->mac[5]);

	_leave(" = 0");
	return 0;
}

/*
 * read the rtnetlink response and pass to parsing routine
 */
static int afs_read_rtm(struct afs_rtm_desc *desc)
{
	struct nlmsghdr *nlhdr, tmphdr;
	struct msghdr msg;
	struct kvec iov[1];
	void *data;
	bool last = false;
	int len, ret, remain;

	_enter("");

	do {
		/* first of all peek to see how big the packet is */
		memset(&msg, 0, sizeof(msg));
		iov[0].iov_base = &tmphdr;
		iov[0].iov_len = sizeof(tmphdr);
		len = kernel_recvmsg(desc->nlsock, &msg, iov, 1,
				     sizeof(tmphdr), MSG_PEEK | MSG_TRUNC);
		if (len < 0) {
			_leave(" = %d [peek]", len);
			return len;
		}
		if (len == 0)
			continue;
		if (len < sizeof(tmphdr) || len < NLMSG_PAYLOAD(&tmphdr, 0)) {
			_leave(" = -EMSGSIZE");
			return -EMSGSIZE;
		}

		if (desc->datamax < len) {
			kfree(desc->data);
			desc->data = NULL;
			data = kmalloc(len, GFP_KERNEL);
			if (!data)
				return -ENOMEM;
			desc->data = data;
		}
		desc->datamax = len;

		/* read all the data from this packet */
		iov[0].iov_base = desc->data;
		iov[0].iov_len = desc->datamax;
		desc->datalen = kernel_recvmsg(desc->nlsock, &msg, iov, 1,
					       desc->datamax, 0);
		if (desc->datalen < 0) {
			_leave(" = %zd [recv]", desc->datalen);
			return desc->datalen;
		}

		nlhdr = desc->data;

		/* check if the header is valid */
		if (!NLMSG_OK(nlhdr, desc->datalen) ||
		    nlhdr->nlmsg_type == NLMSG_ERROR) {
			_leave(" = -EIO");
			return -EIO;
		}

		/* see if this is the last message */
		if (nlhdr->nlmsg_type == NLMSG_DONE ||
		    !(nlhdr->nlmsg_flags & NLM_F_MULTI))
			last = true;

		/* parse the bits we got this time */
		nlmsg_for_each_msg(nlhdr, desc->data, desc->datalen, remain) {
			ret = desc->parse(desc, nlhdr);
			if (ret < 0) {
				_leave(" = %d [parse]", ret);
				return ret;
			}
		}

	} while (!last);

	_leave(" = 0");
	return 0;
}

/*
 * list the interface bound addresses to get the address and netmask
 */
static int afs_rtm_getaddr(struct afs_rtm_desc *desc)
{
	struct msghdr msg;
	struct kvec iov[1];
	int ret;

	struct {
		struct nlmsghdr nl_msg __attribute__((aligned(NLMSG_ALIGNTO)));
		struct ifaddrmsg addr_msg __attribute__((aligned(NLMSG_ALIGNTO)));
	} request;

	_enter("");

	memset(&request, 0, sizeof(request));

	request.nl_msg.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	request.nl_msg.nlmsg_type = RTM_GETADDR;
	request.nl_msg.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	request.nl_msg.nlmsg_seq = desc->msg_seq++;
	request.nl_msg.nlmsg_pid = 0;

	memset(&msg, 0, sizeof(msg));
	iov[0].iov_base = &request;
	iov[0].iov_len = sizeof(request);

	ret = kernel_sendmsg(desc->nlsock, &msg, iov, 1, iov[0].iov_len);
	_leave(" = %d", ret);
	return ret;
}

/*
 * list the interface link statuses to get the MTUs
 */
static int afs_rtm_getlink(struct afs_rtm_desc *desc)
{
	struct msghdr msg;
	struct kvec iov[1];
	int ret;

	struct {
		struct nlmsghdr nl_msg __attribute__((aligned(NLMSG_ALIGNTO)));
		struct ifinfomsg link_msg __attribute__((aligned(NLMSG_ALIGNTO)));
	} request;

	_enter("");

	memset(&request, 0, sizeof(request));

	request.nl_msg.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	request.nl_msg.nlmsg_type = RTM_GETLINK;
	request.nl_msg.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT;
	request.nl_msg.nlmsg_seq = desc->msg_seq++;
	request.nl_msg.nlmsg_pid = 0;

	memset(&msg, 0, sizeof(msg));
	iov[0].iov_base = &request;
	iov[0].iov_len = sizeof(request);

	ret = kernel_sendmsg(desc->nlsock, &msg, iov, 1, iov[0].iov_len);
	_leave(" = %d", ret);
	return ret;
}

/*
 * cull any interface records for which there isn't an MTU value
 */
static void afs_cull_interfaces(struct afs_rtm_desc *desc)
{
	struct afs_interface *bufs = desc->bufs;
	size_t nbufs = desc->nbufs;
	int loop, point = 0;

	_enter("{%zu}", nbufs);

	for (loop = 0; loop < nbufs; loop++) {
		if (desc->bufs[loop].mtu != 0) {
			if (loop != point) {
				ASSERTCMP(loop, >, point);
				bufs[point] = bufs[loop];
			}
			point++;
		}
	}

	desc->nbufs = point;
	_leave(" [%zu/%zu]", desc->nbufs, nbufs);
}

/*
 * get a list of this system's interface IPv4 addresses, netmasks and MTUs
 * - returns the number of interface records in the buffer
 */
int afs_get_ipv4_interfaces(struct afs_interface *bufs, size_t maxbufs,
			    bool wantloopback)
{
	struct afs_rtm_desc desc;
	int ret, loop;

	_enter("");

	memset(&desc, 0, sizeof(desc));
	desc.bufs = bufs;
	desc.maxbufs = maxbufs;
	desc.wantloopback = wantloopback;

	ret = sock_create_kern(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE,
			       &desc.nlsock);
	if (ret < 0) {
		_leave(" = %d [sock]", ret);
		return ret;
	}

	/* issue RTM_GETADDR */
	desc.parse = afs_rtm_getaddr_parse;
	ret = afs_rtm_getaddr(&desc);
	if (ret < 0)
		goto error;
	ret = afs_read_rtm(&desc);
	if (ret < 0)
		goto error;

	/* issue RTM_GETLINK */
	desc.parse = afs_rtm_getlink_if_parse;
	ret = afs_rtm_getlink(&desc);
	if (ret < 0)
		goto error;
	ret = afs_read_rtm(&desc);
	if (ret < 0)
		goto error;

	afs_cull_interfaces(&desc);
	ret = desc.nbufs;

	for (loop = 0; loop < ret; loop++)
		_debug("[%d] "NIPQUAD_FMT"/"NIPQUAD_FMT" mtu %u",
		       bufs[loop].index,
		       NIPQUAD(bufs[loop].address),
		       NIPQUAD(bufs[loop].netmask),
		       bufs[loop].mtu);

error:
	kfree(desc.data);
	sock_release(desc.nlsock);
	_leave(" = %d", ret);
	return ret;
}

/*
 * get a MAC address from a random ethernet interface that has a real one
 * - the buffer should be 6 bytes in size
 */
int afs_get_MAC_address(u8 mac[6])
{
	struct afs_rtm_desc desc;
	int ret;

	_enter("");

	memset(&desc, 0, sizeof(desc));
	desc.mac = mac;
	desc.mac_index = UINT_MAX;

	ret = sock_create_kern(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE,
			       &desc.nlsock);
	if (ret < 0) {
		_leave(" = %d [sock]", ret);
		return ret;
	}

	/* issue RTM_GETLINK */
	desc.parse = afs_rtm_getlink_mac_parse;
	ret = afs_rtm_getlink(&desc);
	if (ret < 0)
		goto error;
	ret = afs_read_rtm(&desc);
	if (ret < 0)
		goto error;

	if (desc.mac_index < UINT_MAX) {
		/* got a MAC address */
		_debug("[%d] %02x:%02x:%02x:%02x:%02x:%02x",
		       desc.mac_index,
		       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		ret = -ENONET;
	}

error:
	sock_release(desc.nlsock);
	_leave(" = %d", ret);
	return ret;
}
