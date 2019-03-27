/*
 * Copyright (c) 2010-2014 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: cm.c 3453 2005-09-15 21:43:21Z sean.hefty $
 */

#include <config.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "cma.h"
#include <rdma/rdma_cma.h>
#include <infiniband/ib.h>

static struct rdma_addrinfo nohints;

static void ucma_convert_to_ai(struct addrinfo *ai,
			       const struct rdma_addrinfo *rai)
{
	memset(ai, 0, sizeof(*ai));
	if (rai->ai_flags & RAI_PASSIVE)
		ai->ai_flags = AI_PASSIVE;
	if (rai->ai_flags & RAI_NUMERICHOST)
		ai->ai_flags |= AI_NUMERICHOST;
	if (rai->ai_family != AF_IB)
		ai->ai_family = rai->ai_family;

	switch (rai->ai_qp_type) {
	case IBV_QPT_RC:
	case IBV_QPT_UC:
	case IBV_QPT_XRC_SEND:
	case IBV_QPT_XRC_RECV:
		ai->ai_socktype = SOCK_STREAM;
		break;
	case IBV_QPT_UD:
		ai->ai_socktype = SOCK_DGRAM;
		break;
	}

	switch (rai->ai_port_space) {
	case RDMA_PS_TCP:
		ai->ai_protocol = IPPROTO_TCP;
		break;
	case RDMA_PS_IPOIB:
	case RDMA_PS_UDP:
		ai->ai_protocol = IPPROTO_UDP;
		break;
	case RDMA_PS_IB:
		if (ai->ai_socktype == SOCK_STREAM)
			ai->ai_protocol = IPPROTO_TCP;
		else if (ai->ai_socktype == SOCK_DGRAM)
			ai->ai_protocol = IPPROTO_UDP;
		break;
	}

	if (rai->ai_flags & RAI_PASSIVE) {
		ai->ai_addrlen = rai->ai_src_len;
		ai->ai_addr = rai->ai_src_addr;
	} else {
		ai->ai_addrlen = rai->ai_dst_len;
		ai->ai_addr = rai->ai_dst_addr;
	}
	ai->ai_canonname = rai->ai_dst_canonname;
	ai->ai_next = NULL;
}

static int ucma_copy_addr(struct sockaddr **dst, socklen_t *dst_len,
			  struct sockaddr *src, socklen_t src_len)
{
	*dst = malloc(src_len);
	if (!(*dst))
		return ERR(ENOMEM);

	memcpy(*dst, src, src_len);
	*dst_len = src_len;
	return 0;
}

void ucma_set_sid(enum rdma_port_space ps, struct sockaddr *addr,
		  struct sockaddr_ib *sib)
{
	__be16 port;

	port = addr ? ucma_get_port(addr) : 0;
	sib->sib_sid = htobe64(((uint64_t) ps << 16) + be16toh(port));

	if (ps)
		sib->sib_sid_mask = htobe64(RDMA_IB_IP_PS_MASK);
	if (port)
		sib->sib_sid_mask |= htobe64(RDMA_IB_IP_PORT_MASK);
}

static int ucma_convert_in6(int ps, struct sockaddr_ib **dst, socklen_t *dst_len,
			    struct sockaddr_in6 *src, socklen_t src_len)
{
	*dst = calloc(1, sizeof(struct sockaddr_ib));
	if (!(*dst))
		return ERR(ENOMEM);

	(*dst)->sib_family = AF_IB;
	(*dst)->sib_pkey = htobe16(0xFFFF);
	(*dst)->sib_flowinfo = src->sin6_flowinfo;
	ib_addr_set(&(*dst)->sib_addr, src->sin6_addr.s6_addr32[0],
		    src->sin6_addr.s6_addr32[1], src->sin6_addr.s6_addr32[2],
		    src->sin6_addr.s6_addr32[3]);
	ucma_set_sid(ps, (struct sockaddr *) src, *dst);
	(*dst)->sib_scope_id = src->sin6_scope_id;

	*dst_len = sizeof(struct sockaddr_ib);
	return 0;
}

static int ucma_convert_to_rai(struct rdma_addrinfo *rai,
			       const struct rdma_addrinfo *hints,
			       const struct addrinfo *ai)
{
	int ret;

	if (hints->ai_qp_type) {
		rai->ai_qp_type = hints->ai_qp_type;
	} else {
		switch (ai->ai_socktype) {
		case SOCK_STREAM:
			rai->ai_qp_type = IBV_QPT_RC;
			break;
		case SOCK_DGRAM:
			rai->ai_qp_type = IBV_QPT_UD;
			break;
		}
	}

	if (hints->ai_port_space) {
		rai->ai_port_space = hints->ai_port_space;
	} else {
		switch (ai->ai_protocol) {
		case IPPROTO_TCP:
			rai->ai_port_space = RDMA_PS_TCP;
			break;
		case IPPROTO_UDP:
			rai->ai_port_space = RDMA_PS_UDP;
			break;
		}
	}

	if (ai->ai_flags & AI_PASSIVE) {
		rai->ai_flags = RAI_PASSIVE;
		if (ai->ai_canonname)
			rai->ai_src_canonname = strdup(ai->ai_canonname);

		if ((hints->ai_flags & RAI_FAMILY) && (hints->ai_family == AF_IB) &&
		    (hints->ai_flags & RAI_NUMERICHOST)) {
			rai->ai_family = AF_IB;
			ret = ucma_convert_in6(rai->ai_port_space,
					       (struct sockaddr_ib **) &rai->ai_src_addr,
					       &rai->ai_src_len,
					       (struct sockaddr_in6 *) ai->ai_addr,
					       ai->ai_addrlen);
		} else {
			rai->ai_family = ai->ai_family;
			ret = ucma_copy_addr(&rai->ai_src_addr, &rai->ai_src_len,
					     ai->ai_addr, ai->ai_addrlen);
		}
	} else {
		if (ai->ai_canonname)
			rai->ai_dst_canonname = strdup(ai->ai_canonname);

		if ((hints->ai_flags & RAI_FAMILY) && (hints->ai_family == AF_IB) &&
		    (hints->ai_flags & RAI_NUMERICHOST)) {
			rai->ai_family = AF_IB;
			ret = ucma_convert_in6(rai->ai_port_space,
					       (struct sockaddr_ib **) &rai->ai_dst_addr,
					       &rai->ai_dst_len,
					       (struct sockaddr_in6 *) ai->ai_addr,
					       ai->ai_addrlen);
		} else {
			rai->ai_family = ai->ai_family;
			ret = ucma_copy_addr(&rai->ai_dst_addr, &rai->ai_dst_len,
					     ai->ai_addr, ai->ai_addrlen);
		}
	}
	return ret;
}

static int ucma_getaddrinfo(const char *node, const char *service,
			    const struct rdma_addrinfo *hints,
			    struct rdma_addrinfo *rai)
{
	struct addrinfo ai_hints;
	struct addrinfo *ai;
	int ret;

	if (hints != &nohints) {
		ucma_convert_to_ai(&ai_hints, hints);
		ret = getaddrinfo(node, service, &ai_hints, &ai);
	} else {
		ret = getaddrinfo(node, service, NULL, &ai);
	}
	if (ret)
		return ret;

	ret = ucma_convert_to_rai(rai, hints, ai);
	freeaddrinfo(ai);
	return ret;
}

int rdma_getaddrinfo(const char *node, const char *service,
		     const struct rdma_addrinfo *hints,
		     struct rdma_addrinfo **res)
{
	struct rdma_addrinfo *rai;
	int ret;

	if (!service && !node && !hints)
		return ERR(EINVAL);

	ret = ucma_init();
	if (ret)
		return ret;

	rai = calloc(1, sizeof(*rai));
	if (!rai)
		return ERR(ENOMEM);

	if (!hints)
		hints = &nohints;

	if (node || service) {
		ret = ucma_getaddrinfo(node, service, hints, rai);
	} else {
		rai->ai_flags = hints->ai_flags;
		rai->ai_family = hints->ai_family;
		rai->ai_qp_type = hints->ai_qp_type;
		rai->ai_port_space = hints->ai_port_space;
		if (hints->ai_dst_len) {
			ret = ucma_copy_addr(&rai->ai_dst_addr, &rai->ai_dst_len,
					     hints->ai_dst_addr, hints->ai_dst_len);
		}
	}
	if (ret)
		goto err;

	if (!rai->ai_src_len && hints->ai_src_len) {
		ret = ucma_copy_addr(&rai->ai_src_addr, &rai->ai_src_len,
				     hints->ai_src_addr, hints->ai_src_len);
		if (ret)
			goto err;
	}

	if (!(rai->ai_flags & RAI_PASSIVE))
		ucma_ib_resolve(&rai, hints);

	*res = rai;
	return 0;

err:
	rdma_freeaddrinfo(rai);
	return ret;
}

void rdma_freeaddrinfo(struct rdma_addrinfo *res)
{
	struct rdma_addrinfo *rai;

	while (res) {
		rai = res;
		res = res->ai_next;

		if (rai->ai_connect)
			free(rai->ai_connect);

		if (rai->ai_route)
			free(rai->ai_route);

		if (rai->ai_src_canonname)
			free(rai->ai_src_canonname);

		if (rai->ai_dst_canonname)
			free(rai->ai_dst_canonname);

		if (rai->ai_src_addr)
			free(rai->ai_src_addr);

		if (rai->ai_dst_addr)
			free(rai->ai_dst_addr);

		free(rai);
	}
}
