/*
 * Copyright (c) 2010-2012 Intel Corporation.  All rights reserved.
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
 */

#include <config.h>

#include <stdio.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "cma.h"
#include <rdma/rdma_cma.h>
#include <infiniband/ib.h>
#include <infiniband/sa.h>

#define ACM_VERSION             1

#define ACM_OP_RESOLVE          0x01
#define ACM_OP_ACK              0x80

#define ACM_STATUS_SUCCESS      0
#define ACM_STATUS_ENOMEM       1
#define ACM_STATUS_EINVAL       2
#define ACM_STATUS_ENODATA      3
#define ACM_STATUS_ENOTCONN     5
#define ACM_STATUS_ETIMEDOUT    6
#define ACM_STATUS_ESRCADDR     7
#define ACM_STATUS_ESRCTYPE     8
#define ACM_STATUS_EDESTADDR    9
#define ACM_STATUS_EDESTTYPE    10

#define ACM_FLAGS_NODELAY	(1<<30)

#define ACM_MSG_HDR_LENGTH      16
#define ACM_MAX_ADDRESS         64
#define ACM_MSG_EP_LENGTH       72
#define ACM_MSG_DATA_LENGTH     (ACM_MSG_EP_LENGTH * 8)

struct acm_hdr {
	uint8_t                 version;
	uint8_t                 opcode;
	uint8_t                 status;
	uint8_t		        data[3];
	uint16_t                length;
	uint64_t                tid;
};

#define ACM_EP_INFO_NAME        0x0001
#define ACM_EP_INFO_ADDRESS_IP  0x0002
#define ACM_EP_INFO_ADDRESS_IP6 0x0003
#define ACM_EP_INFO_PATH        0x0010

union acm_ep_info {
	uint8_t                 addr[ACM_MAX_ADDRESS];
	uint8_t                 name[ACM_MAX_ADDRESS];
	struct ibv_path_record  path;
};

#define ACM_EP_FLAG_SOURCE      (1<<0)
#define ACM_EP_FLAG_DEST        (1<<1)

struct acm_ep_addr_data {
	uint32_t                flags;
	uint16_t                type;
	uint16_t                reserved;
	union acm_ep_info       info;
};

struct acm_resolve_msg {
	struct acm_hdr          hdr;
	struct acm_ep_addr_data data[0];
};

struct acm_msg {
	struct acm_hdr                  hdr;
	union{
		uint8_t                 data[ACM_MSG_DATA_LENGTH];
		struct acm_ep_addr_data resolve_data[0];
	};
};

static pthread_mutex_t acm_lock = PTHREAD_MUTEX_INITIALIZER;
static int sock = -1;
static uint16_t server_port;

static int ucma_set_server_port(void)
{
	FILE *f;

	if ((f = fopen(IBACM_PORT_FILE, "r" STREAM_CLOEXEC))) {
		if (fscanf(f, "%" SCNu16, &server_port) != 1)
			server_port = 0;
		fclose(f);
	}
	return server_port;
}

void ucma_ib_init(void)
{
	struct sockaddr_in addr;
	static int init;
	int ret;

	if (init)
		return;

	pthread_mutex_lock(&acm_lock);
	if (init)
		goto unlock;

	if (!ucma_set_server_port())
		goto out;

	sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
	if (sock < 0)
		goto out;

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htobe32(INADDR_LOOPBACK);
	addr.sin_port = htobe16(server_port);
	ret = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
	if (ret) {
		close(sock);
		sock = -1;
	}
out:
	init = 1;
unlock:
	pthread_mutex_unlock(&acm_lock);
}

void ucma_ib_cleanup(void)
{
	if (sock >= 0) {
		shutdown(sock, SHUT_RDWR);
		close(sock);
	}
}

static int ucma_ib_set_addr(struct rdma_addrinfo *ib_rai,
			    struct rdma_addrinfo *rai)
{
	struct sockaddr_ib *src, *dst;
	struct ibv_path_record *path;

	src = calloc(1, sizeof(*src));
	if (!src)
		return ERR(ENOMEM);

	dst = calloc(1, sizeof(*dst));
	if (!dst) {
		free(src);
		return ERR(ENOMEM);
	}

	path = &((struct ibv_path_data *) ib_rai->ai_route)->path;

	src->sib_family = AF_IB;
	src->sib_pkey = path->pkey;
	src->sib_flowinfo = htobe32(be32toh(path->flowlabel_hoplimit) >> 8);
	memcpy(&src->sib_addr, &path->sgid, 16);
	ucma_set_sid(ib_rai->ai_port_space, rai->ai_src_addr, src);

	dst->sib_family = AF_IB;
	dst->sib_pkey = path->pkey;
	dst->sib_flowinfo = htobe32(be32toh(path->flowlabel_hoplimit) >> 8);
	memcpy(&dst->sib_addr, &path->dgid, 16);
	ucma_set_sid(ib_rai->ai_port_space, rai->ai_dst_addr, dst);

	ib_rai->ai_src_addr = (struct sockaddr *) src;
	ib_rai->ai_src_len = sizeof(*src);

	ib_rai->ai_dst_addr = (struct sockaddr *) dst;
	ib_rai->ai_dst_len = sizeof(*dst);

	return 0;
}
				 
static int ucma_ib_set_connect(struct rdma_addrinfo *ib_rai,
			       struct rdma_addrinfo *rai)
{
	struct ib_connect_hdr *hdr;

	if (rai->ai_family == AF_IB)
		return 0;

	hdr = calloc(1, sizeof(*hdr));
	if (!hdr)
		return ERR(ENOMEM);

	if (rai->ai_family == AF_INET) {
		hdr->ip_version = 4 << 4;
		memcpy(&hdr->cma_src_ip4,
		       &((struct sockaddr_in *) rai->ai_src_addr)->sin_addr, 4);
		memcpy(&hdr->cma_dst_ip4,
		       &((struct sockaddr_in *) rai->ai_dst_addr)->sin_addr, 4);
	} else {
		hdr->ip_version = 6 << 4;
		memcpy(&hdr->cma_src_ip6,
		       &((struct sockaddr_in6 *) rai->ai_src_addr)->sin6_addr, 16);
		memcpy(&hdr->cma_dst_ip6,
		       &((struct sockaddr_in6 *) rai->ai_dst_addr)->sin6_addr, 16);
	}

	ib_rai->ai_connect = hdr;
	ib_rai->ai_connect_len = sizeof(*hdr);
	return 0;
}

static void ucma_resolve_af_ib(struct rdma_addrinfo **rai)
{
	struct rdma_addrinfo *ib_rai;

	ib_rai = calloc(1, sizeof(*ib_rai));
	if (!ib_rai)
		return;

	ib_rai->ai_flags = (*rai)->ai_flags;
	ib_rai->ai_family = AF_IB;
	ib_rai->ai_qp_type = (*rai)->ai_qp_type;
	ib_rai->ai_port_space = (*rai)->ai_port_space;

	ib_rai->ai_route = calloc(1, (*rai)->ai_route_len);
	if (!ib_rai->ai_route)
		goto err;

	memcpy(ib_rai->ai_route, (*rai)->ai_route, (*rai)->ai_route_len);
	ib_rai->ai_route_len = (*rai)->ai_route_len;

	if ((*rai)->ai_src_canonname) {
		ib_rai->ai_src_canonname = strdup((*rai)->ai_src_canonname);
		if (!ib_rai->ai_src_canonname)
			goto err;
	}

	if ((*rai)->ai_dst_canonname) {
		ib_rai->ai_dst_canonname = strdup((*rai)->ai_dst_canonname);
		if (!ib_rai->ai_dst_canonname)
			goto err;
	}

	if (ucma_ib_set_connect(ib_rai, *rai))
		goto err;

	if (ucma_ib_set_addr(ib_rai, *rai))
		goto err;

	ib_rai->ai_next = *rai;
	*rai = ib_rai;
	return;

err:
	rdma_freeaddrinfo(ib_rai);
}

static void ucma_ib_save_resp(struct rdma_addrinfo *rai, struct acm_msg *msg)
{
	struct acm_ep_addr_data *ep_data;
	struct ibv_path_data *path_data = NULL;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	int i, cnt, path_cnt = 0;

	cnt = (msg->hdr.length - ACM_MSG_HDR_LENGTH) / ACM_MSG_EP_LENGTH;
	for (i = 0; i < cnt; i++) {
		ep_data = &msg->resolve_data[i];
		switch (ep_data->type) {
		case ACM_EP_INFO_PATH:
			ep_data->type = 0;
			if (!path_data)
				path_data = (struct ibv_path_data *) ep_data;
			path_cnt++;
			break;
		case ACM_EP_INFO_ADDRESS_IP:
			if (!(ep_data->flags & ACM_EP_FLAG_SOURCE) || rai->ai_src_len)
				break;

			sin = calloc(1, sizeof(*sin));
			if (!sin)
				break;

			sin->sin_family = AF_INET;
			memcpy(&sin->sin_addr, &ep_data->info.addr, 4);
			rai->ai_src_len = sizeof(*sin);
			rai->ai_src_addr = (struct sockaddr *) sin;
			break;
		case ACM_EP_INFO_ADDRESS_IP6:
			if (!(ep_data->flags & ACM_EP_FLAG_SOURCE) || rai->ai_src_len)
				break;

			sin6 = calloc(1, sizeof(*sin6));
			if (!sin6)
				break;

			sin6->sin6_family = AF_INET6;
			memcpy(&sin6->sin6_addr, &ep_data->info.addr, 16);
			rai->ai_src_len = sizeof(*sin6);
			rai->ai_src_addr = (struct sockaddr *) sin6;
			break;
		default:
			break;
		}
	}

	rai->ai_route = calloc(path_cnt, sizeof(*path_data));
	if (rai->ai_route) {
		memcpy(rai->ai_route, path_data, path_cnt * sizeof(*path_data));
		rai->ai_route_len = path_cnt * sizeof(*path_data);
	}
}

static void ucma_set_ep_addr(struct acm_ep_addr_data *data, struct sockaddr *addr)
{
	if (addr->sa_family == AF_INET) {
		data->type = ACM_EP_INFO_ADDRESS_IP;
		memcpy(data->info.addr, &((struct sockaddr_in *) addr)->sin_addr, 4);
	} else {
		data->type = ACM_EP_INFO_ADDRESS_IP6;
		memcpy(data->info.addr, &((struct sockaddr_in6 *) addr)->sin6_addr, 16);
	}
}

static int ucma_inet_addr(struct sockaddr *addr, socklen_t len)
{
	return len && addr && (addr->sa_family == AF_INET ||
			       addr->sa_family == AF_INET6);
}

static int ucma_ib_addr(struct sockaddr *addr, socklen_t len)
{
	return len && addr && (addr->sa_family == AF_IB);
}

void ucma_ib_resolve(struct rdma_addrinfo **rai,
		     const struct rdma_addrinfo *hints)
{
	struct acm_msg msg;
	struct acm_ep_addr_data *data;
	int ret;

	ucma_ib_init();
	if (sock < 0)
		return;

	memset(&msg, 0, sizeof msg);
	msg.hdr.version = ACM_VERSION;
	msg.hdr.opcode = ACM_OP_RESOLVE;
	msg.hdr.length = ACM_MSG_HDR_LENGTH;

	data = &msg.resolve_data[0];
	if (ucma_inet_addr((*rai)->ai_src_addr, (*rai)->ai_src_len)) {
		data->flags = ACM_EP_FLAG_SOURCE;
		ucma_set_ep_addr(data, (*rai)->ai_src_addr);
		data++;
		msg.hdr.length += ACM_MSG_EP_LENGTH;
	}

	if (ucma_inet_addr((*rai)->ai_dst_addr, (*rai)->ai_dst_len)) {
		data->flags = ACM_EP_FLAG_DEST;
		if (hints->ai_flags & (RAI_NUMERICHOST | RAI_NOROUTE))
			data->flags |= ACM_FLAGS_NODELAY;
		ucma_set_ep_addr(data, (*rai)->ai_dst_addr);
		data++;
		msg.hdr.length += ACM_MSG_EP_LENGTH;
	}

	if (hints->ai_route_len ||
	    ucma_ib_addr((*rai)->ai_src_addr, (*rai)->ai_src_len) ||
	    ucma_ib_addr((*rai)->ai_dst_addr, (*rai)->ai_dst_len)) {
		struct ibv_path_record *path;

		if (hints->ai_route_len == sizeof(struct ibv_path_record))
			path = (struct ibv_path_record *) hints->ai_route;
		else if (hints->ai_route_len == sizeof(struct ibv_path_data))
			path = &((struct ibv_path_data *) hints->ai_route)->path;
		else
			path = NULL;

		if (path)
			memcpy(&data->info.path, path, sizeof(*path));

		if (ucma_ib_addr((*rai)->ai_src_addr, (*rai)->ai_src_len)) {
			memcpy(&data->info.path.sgid,
			       &((struct sockaddr_ib *) (*rai)->ai_src_addr)->sib_addr, 16);
		}
		if (ucma_ib_addr((*rai)->ai_dst_addr, (*rai)->ai_dst_len)) {
			memcpy(&data->info.path.dgid,
			       &((struct sockaddr_ib *) (*rai)->ai_dst_addr)->sib_addr, 16);
		}
		data->type = ACM_EP_INFO_PATH;
		data++;
		msg.hdr.length += ACM_MSG_EP_LENGTH;
	}

	pthread_mutex_lock(&acm_lock);
	ret = send(sock, (char *) &msg, msg.hdr.length, 0);
	if (ret != msg.hdr.length) {
		pthread_mutex_unlock(&acm_lock);
		return;
	}

	ret = recv(sock, (char *) &msg, sizeof msg, 0);
	pthread_mutex_unlock(&acm_lock);
	if (ret < ACM_MSG_HDR_LENGTH || ret != msg.hdr.length || msg.hdr.status)
		return;

	ucma_ib_save_resp(*rai, &msg);

	if (af_ib_support && !(hints->ai_flags & RAI_ROUTEONLY) && (*rai)->ai_route_len)
		ucma_resolve_af_ib(rai);
}
