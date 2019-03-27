/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2011 Intel Corporation, Inc.  All rights reserved.
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
#define _GNU_SOURCE
#include <config.h>

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>

#include "pingpong.h"

#define MSG_FORMAT "%04x:%06x:%06x:%06x:%06x:%32s"
#define MSG_SIZE   66
#define MSG_SSCAN  "%x:%x:%x:%x:%x:%s"
#define ADDR_FORMAT \
	"%8s: LID %04x, QPN RECV %06x SEND %06x, PSN %06x, SRQN %06x, GID %s\n"
#define TERMINATION_FORMAT "%s"
#define TERMINATION_MSG_SIZE 4
#define TERMINATION_MSG "END"
static int page_size;

struct pingpong_dest {
	union ibv_gid gid;
	int lid;
	int recv_qpn;
	int send_qpn;
	int recv_psn;
	int send_psn;
	int srqn;
	int pp_cnt;
	int sockfd;
};

struct pingpong_context {
	struct ibv_context	*context;
	struct ibv_comp_channel *channel;
	struct ibv_pd		*pd;
	struct ibv_mr		*mr;
	struct ibv_cq		*send_cq;
	struct ibv_cq		*recv_cq;
	struct ibv_srq		*srq;
	struct ibv_xrcd		*xrcd;
	struct ibv_qp		**recv_qp;
	struct ibv_qp		**send_qp;
	struct pingpong_dest	*rem_dest;
	void			*buf;
	int			 lid;
	int			 sl;
	enum ibv_mtu		 mtu;
	int			 ib_port;
	int			 fd;
	int			 size;
	int			 num_clients;
	int			 num_tests;
	int			 use_event;
	int			 gidx;
};

static struct pingpong_context ctx;


static int open_device(char *ib_devname)
{
	struct ibv_device **dev_list;
	int i = 0;

	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		fprintf(stderr, "Failed to get IB devices list");
		return -1;
	}

	if (ib_devname) {
		for (; dev_list[i]; ++i) {
			if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
				break;
		}
	}
	if (!dev_list[i]) {
		fprintf(stderr, "IB device %s not found\n",
			ib_devname ? ib_devname : "");
		return -1;
	}

	ctx.context = ibv_open_device(dev_list[i]);
	if (!ctx.context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			ibv_get_device_name(dev_list[i]));
		return -1;
	}

	ibv_free_device_list(dev_list);
	return 0;
}

static int create_qps(void)
{
	struct ibv_qp_init_attr_ex init;
	struct ibv_qp_attr mod;
	int i;

	for (i = 0; i < ctx.num_clients; ++i) {

		memset(&init, 0, sizeof init);
		init.qp_type = IBV_QPT_XRC_RECV;
		init.comp_mask = IBV_QP_INIT_ATTR_XRCD;
		init.xrcd = ctx.xrcd;

		ctx.recv_qp[i] = ibv_create_qp_ex(ctx.context, &init);
		if (!ctx.recv_qp[i])  {
			fprintf(stderr, "Couldn't create recv QP[%d] errno %d\n",
				i, errno);
			return 1;
		}

		mod.qp_state        = IBV_QPS_INIT;
		mod.pkey_index      = 0;
		mod.port_num        = ctx.ib_port;
		mod.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

		if (ibv_modify_qp(ctx.recv_qp[i], &mod,
				  IBV_QP_STATE | IBV_QP_PKEY_INDEX |
				  IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify recv QP[%d] to INIT\n", i);
			return 1;
		}

		memset(&init, 0, sizeof init);
		init.qp_type	      = IBV_QPT_XRC_SEND;
		init.send_cq	      = ctx.send_cq;
		init.cap.max_send_wr  = ctx.num_clients * ctx.num_tests;
		init.cap.max_send_sge = 1;
		init.comp_mask	      = IBV_QP_INIT_ATTR_PD;
		init.pd		      = ctx.pd;

		ctx.send_qp[i] = ibv_create_qp_ex(ctx.context, &init);
		if (!ctx.send_qp[i])  {
			fprintf(stderr, "Couldn't create send QP[%d] errno %d\n",
				i, errno);
			return 1;
		}

		mod.qp_state        = IBV_QPS_INIT;
		mod.pkey_index      = 0;
		mod.port_num        = ctx.ib_port;
		mod.qp_access_flags = 0;

		if (ibv_modify_qp(ctx.send_qp[i], &mod,
				  IBV_QP_STATE | IBV_QP_PKEY_INDEX |
				  IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify send QP[%d] to INIT\n", i);
			return 1;
		}
	}

	return 0;
}

static int pp_init_ctx(char *ib_devname)
{
	struct ibv_srq_init_attr_ex attr;
	struct ibv_xrcd_init_attr xrcd_attr;
	struct ibv_port_attr port_attr;

	ctx.recv_qp = calloc(ctx.num_clients, sizeof *ctx.recv_qp);
	ctx.send_qp = calloc(ctx.num_clients, sizeof *ctx.send_qp);
	ctx.rem_dest = calloc(ctx.num_clients, sizeof *ctx.rem_dest);
	if (!ctx.recv_qp || !ctx.send_qp || !ctx.rem_dest)
		return 1;

	if (open_device(ib_devname)) {
		fprintf(stderr, "Failed to open device\n");
		return 1;
	}

	if (pp_get_port_info(ctx.context, ctx.ib_port, &port_attr)) {
		fprintf(stderr, "Failed to get port info\n");
		return 1;
	}

	ctx.lid = port_attr.lid;
	if (port_attr.link_layer != IBV_LINK_LAYER_ETHERNET && !ctx.lid) {
		fprintf(stderr, "Couldn't get local LID\n");
		return 1;
	}

	ctx.buf = memalign(page_size, ctx.size);
	if (!ctx.buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		return 1;
	}

	memset(ctx.buf, 0, ctx.size);

	if (ctx.use_event) {
		ctx.channel = ibv_create_comp_channel(ctx.context);
		if (!ctx.channel) {
			fprintf(stderr, "Couldn't create completion channel\n");
			return 1;
		}
	}

	ctx.pd = ibv_alloc_pd(ctx.context);
	if (!ctx.pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		return 1;
	}

	ctx.mr = ibv_reg_mr(ctx.pd, ctx.buf, ctx.size, IBV_ACCESS_LOCAL_WRITE);
	if (!ctx.mr) {
		fprintf(stderr, "Couldn't register MR\n");
		return 1;
	}

	ctx.fd = open("/tmp/xrc_domain", O_RDONLY | O_CREAT, S_IRUSR | S_IRGRP);
	if (ctx.fd < 0) {
		fprintf(stderr,
			"Couldn't create the file for the XRC Domain "
			"but not stopping %d\n", errno);
		ctx.fd = -1;
	}

	memset(&xrcd_attr, 0, sizeof xrcd_attr);
	xrcd_attr.comp_mask = IBV_XRCD_INIT_ATTR_FD | IBV_XRCD_INIT_ATTR_OFLAGS;
	xrcd_attr.fd = ctx.fd;
	xrcd_attr.oflags = O_CREAT;
	ctx.xrcd = ibv_open_xrcd(ctx.context, &xrcd_attr);
	if (!ctx.xrcd) {
		fprintf(stderr, "Couldn't Open the XRC Domain %d\n", errno);
		return 1;
	}

	ctx.recv_cq = ibv_create_cq(ctx.context, ctx.num_clients, &ctx.recv_cq,
				    ctx.channel, 0);
	if (!ctx.recv_cq) {
		fprintf(stderr, "Couldn't create recv CQ\n");
		return 1;
	}

	if (ctx.use_event) {
		if (ibv_req_notify_cq(ctx.recv_cq, 0)) {
			fprintf(stderr, "Couldn't request CQ notification\n");
			return 1;
		}
	}

	ctx.send_cq = ibv_create_cq(ctx.context, ctx.num_clients, NULL, NULL, 0);
	if (!ctx.send_cq) {
		fprintf(stderr, "Couldn't create send CQ\n");
		return 1;
	}

	memset(&attr, 0, sizeof attr);
	attr.attr.max_wr = ctx.num_clients;
	attr.attr.max_sge = 1;
	attr.comp_mask = IBV_SRQ_INIT_ATTR_TYPE | IBV_SRQ_INIT_ATTR_XRCD |
			 IBV_SRQ_INIT_ATTR_CQ | IBV_SRQ_INIT_ATTR_PD;
	attr.srq_type = IBV_SRQT_XRC;
	attr.xrcd = ctx.xrcd;
	attr.cq = ctx.recv_cq;
	attr.pd = ctx.pd;

	ctx.srq = ibv_create_srq_ex(ctx.context, &attr);
	if (!ctx.srq)  {
		fprintf(stderr, "Couldn't create SRQ\n");
		return 1;
	}

	if (create_qps())
		return 1;

	return 0;
}

static int recv_termination_ack(int index)
{
	char msg[TERMINATION_MSG_SIZE];
	int n = 0, r;
	int sockfd = ctx.rem_dest[index].sockfd;

	while (n < TERMINATION_MSG_SIZE) {
		r = read(sockfd, msg + n, TERMINATION_MSG_SIZE - n);
		if (r < 0) {
			perror("client read");
			fprintf(stderr,
				"%d/%d: Couldn't read remote termination ack\n",
				n, TERMINATION_MSG_SIZE);
			return 1;
		}
		n += r;
	}

	if (strcmp(msg, TERMINATION_MSG)) {
		fprintf(stderr, "Invalid termination ack was accepted\n");
		return 1;
	}

	return 0;
}

static int send_termination_ack(int index)
{
	char msg[TERMINATION_MSG_SIZE];
	int sockfd = ctx.rem_dest[index].sockfd;

	sprintf(msg, TERMINATION_FORMAT, TERMINATION_MSG);

	if (write(sockfd, msg, TERMINATION_MSG_SIZE) != TERMINATION_MSG_SIZE) {
		fprintf(stderr, "Couldn't send termination ack\n");
		return 1;
	}

	return 0;
}

static int pp_client_termination(void)
{
	if (send_termination_ack(0))
		return 1;
	if (recv_termination_ack(0))
		return 1;

	return 0;
}

static int pp_server_termination(void)
{
	int i;

	for (i = 0; i < ctx.num_clients; i++) {
		if (recv_termination_ack(i))
			return 1;
	}

	for (i = 0; i < ctx.num_clients; i++) {
		if (send_termination_ack(i))
			return 1;
	}

	return 0;
}

static int send_local_dest(int sockfd, int index)
{
	char msg[MSG_SIZE];
	char gid[33];
	uint32_t srq_num;
	union ibv_gid local_gid;

	if (ctx.gidx >= 0) {
		if (ibv_query_gid(ctx.context, ctx.ib_port, ctx.gidx,
				  &local_gid)) {
			fprintf(stderr, "can't read sgid of index %d\n",
				ctx.gidx);
			return -1;
		}
	} else {
		memset(&local_gid, 0, sizeof(local_gid));
	}

	ctx.rem_dest[index].recv_psn = lrand48() & 0xffffff;
	if (ibv_get_srq_num(ctx.srq, &srq_num)) {
		fprintf(stderr, "Couldn't get SRQ num\n");
		return -1;
	}

	inet_ntop(AF_INET6, &local_gid, gid, sizeof(gid));
	printf(ADDR_FORMAT, "local", ctx.lid, ctx.recv_qp[index]->qp_num,
		ctx.send_qp[index]->qp_num, ctx.rem_dest[index].recv_psn,
		srq_num, gid);

	gid_to_wire_gid(&local_gid, gid);
	sprintf(msg, MSG_FORMAT, ctx.lid, ctx.recv_qp[index]->qp_num,
		ctx.send_qp[index]->qp_num, ctx.rem_dest[index].recv_psn,
		srq_num, gid);

	if (write(sockfd, msg, MSG_SIZE) != MSG_SIZE) {
		fprintf(stderr, "Couldn't send local address\n");
		return -1;
	}

	return 0;
}

static int recv_remote_dest(int sockfd, int index)
{
	struct pingpong_dest *rem_dest;
	char msg[MSG_SIZE];
	char gid[33];
	int n = 0, r;

	while (n < MSG_SIZE) {
		r = read(sockfd, msg + n, MSG_SIZE - n);
		if (r < 0) {
			perror("client read");
			fprintf(stderr,
				"%d/%d: Couldn't read remote address [%d]\n",
				n, MSG_SIZE, index);
			return -1;
		}
		n += r;
	}

	rem_dest = &ctx.rem_dest[index];
	sscanf(msg, MSG_SSCAN, &rem_dest->lid, &rem_dest->recv_qpn,
		&rem_dest->send_qpn, &rem_dest->send_psn, &rem_dest->srqn, gid);

	wire_gid_to_gid(gid, &rem_dest->gid);
	inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof(gid));
	printf(ADDR_FORMAT, "remote", rem_dest->lid, rem_dest->recv_qpn,
		rem_dest->send_qpn, rem_dest->send_psn, rem_dest->srqn,
		gid);

	rem_dest->sockfd = sockfd;
	return 0;
}

static void set_ah_attr(struct ibv_ah_attr *attr, struct pingpong_context *myctx,
			int index)
{
	attr->is_global = 1;
	attr->grh.hop_limit = 5;
	attr->grh.dgid = myctx->rem_dest[index].gid;
	attr->grh.sgid_index = myctx->gidx;
}

static int connect_qps(int index)
{
	struct ibv_qp_attr attr;

	memset(&attr, 0, sizeof attr);
	attr.qp_state	      = IBV_QPS_RTR;
	attr.dest_qp_num      = ctx.rem_dest[index].send_qpn;
	attr.path_mtu	      = ctx.mtu;
	attr.rq_psn	      = ctx.rem_dest[index].send_psn;
	attr.min_rnr_timer    = 12;
	attr.ah_attr.dlid     = ctx.rem_dest[index].lid;
	attr.ah_attr.sl	      = ctx.sl;
	attr.ah_attr.port_num = ctx.ib_port;

	if (ctx.rem_dest[index].gid.global.interface_id)
		set_ah_attr(&attr.ah_attr, &ctx, index);

	if (ibv_modify_qp(ctx.recv_qp[index], &attr,
			  IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
			  IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
			  IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
		fprintf(stderr, "Failed to modify recv QP[%d] to RTR\n", index);
		return 1;
	}

	memset(&attr, 0, sizeof attr);
	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = 14;
	attr.sq_psn = ctx.rem_dest[index].recv_psn;

	if (ibv_modify_qp(ctx.recv_qp[index], &attr,
			  IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_SQ_PSN)) {
		fprintf(stderr, "Failed to modify recv QP[%d] to RTS\n", index);
		return 1;
	}

	memset(&attr, 0, sizeof attr);
	attr.qp_state	      = IBV_QPS_RTR;
	attr.dest_qp_num      = ctx.rem_dest[index].recv_qpn;
	attr.path_mtu	      = ctx.mtu;
	attr.rq_psn	      = ctx.rem_dest[index].send_psn;
	attr.ah_attr.dlid     = ctx.rem_dest[index].lid;
	attr.ah_attr.sl	      = ctx.sl;
	attr.ah_attr.port_num = ctx.ib_port;

	if (ctx.rem_dest[index].gid.global.interface_id)
		set_ah_attr(&attr.ah_attr, &ctx, index);

	if (ibv_modify_qp(ctx.send_qp[index], &attr,
			  IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
			  IBV_QP_DEST_QPN | IBV_QP_RQ_PSN)) {
		fprintf(stderr, "Failed to modify send QP[%d] to RTR\n", index);
		return 1;
	}

	memset(&attr, 0, sizeof attr);
	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = 14;
	attr.retry_cnt = 7;
	attr.rnr_retry = 7;
	attr.sq_psn = ctx.rem_dest[index].recv_psn;

	if (ibv_modify_qp(ctx.send_qp[index], &attr,
			  IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_SQ_PSN |
			  IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC)) {
		fprintf(stderr, "Failed to modify send QP[%d] to RTS\n", index);
		return 1;
	}

	return 0;
}

static int pp_client_connect(const char *servername, int port)
{
	struct addrinfo *res, *t;
	char *service;
	int ret;
	int sockfd = -1;
	struct addrinfo hints = {
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};

	if (asprintf(&service, "%d", port) < 0)
		return 1;

	ret = getaddrinfo(servername, service, &hints, &res);
	if (ret < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(ret), servername, port);
		free(service);
		return 1;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo_null(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		return 1;
	}

	if (send_local_dest(sockfd, 0))
		return 1;

	if (recv_remote_dest(sockfd, 0))
		return 1;

	if (connect_qps(0))
		return 1;

	return 0;
}

static int pp_server_connect(int port)
{
	struct addrinfo *res, *t;
	char *service;
	int ret, i, n;
	int sockfd = -1, connfd;
	struct addrinfo hints = {
		.ai_flags    = AI_PASSIVE,
		.ai_family   = AF_INET,
		.ai_socktype = SOCK_STREAM
	};

	if (asprintf(&service, "%d", port) < 0)
		return 1;

	ret = getaddrinfo(NULL, service, &hints, &res);
	if (ret < 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(ret), port);
		free(service);
		return 1;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			n = 1;
			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);
			if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo_null(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't listen to port %d\n", port);
		return 1;
	}

	listen(sockfd, ctx.num_clients);

	for (i = 0; i < ctx.num_clients; i++) {
		connfd = accept(sockfd, NULL, NULL);
		if (connfd < 0) {
			fprintf(stderr, "accept() failed for client %d\n", i);
			return 1;
		}

		if (recv_remote_dest(connfd, i))
			return 1;

		if (send_local_dest(connfd, i))
			return 1;

		if (connect_qps(i))
			return 1;
	}

	close(sockfd);
	return 0;
}


static int pp_close_ctx(void)
{
	int i;

	for (i = 0; i < ctx.num_clients; ++i) {

		if (ibv_destroy_qp(ctx.send_qp[i])) {
			fprintf(stderr, "Couldn't destroy INI QP[%d]\n", i);
			return 1;
		}

		if (ibv_destroy_qp(ctx.recv_qp[i])) {
			fprintf(stderr, "Couldn't destroy TGT QP[%d]\n", i);
			return 1;
		}

		if (ctx.rem_dest[i].sockfd)
			close(ctx.rem_dest[i].sockfd);
	}

	if (ibv_destroy_srq(ctx.srq)) {
		fprintf(stderr, "Couldn't destroy SRQ\n");
		return 1;
	}

	if (ctx.xrcd && ibv_close_xrcd(ctx.xrcd)) {
		fprintf(stderr, "Couldn't close the XRC Domain\n");
		return 1;
	}
	if (ctx.fd >= 0 && close(ctx.fd)) {
		fprintf(stderr, "Couldn't close the file for the XRC Domain\n");
		return 1;
	}

	if (ibv_destroy_cq(ctx.send_cq)) {
		fprintf(stderr, "Couldn't destroy send CQ\n");
		return 1;
	}

	if (ibv_destroy_cq(ctx.recv_cq)) {
		fprintf(stderr, "Couldn't destroy recv CQ\n");
		return 1;
	}

	if (ibv_dereg_mr(ctx.mr)) {
		fprintf(stderr, "Couldn't deregister MR\n");
		return 1;
	}

	if (ibv_dealloc_pd(ctx.pd)) {
		fprintf(stderr, "Couldn't deallocate PD\n");
		return 1;
	}

	if (ctx.channel) {
		if (ibv_destroy_comp_channel(ctx.channel)) {
			fprintf(stderr,
				"Couldn't destroy completion channel\n");
			return 1;
		}
	}

	if (ibv_close_device(ctx.context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}

	free(ctx.buf);
	free(ctx.rem_dest);
	free(ctx.send_qp);
	free(ctx.recv_qp);
	return 0;
}

static int pp_post_recv(int cnt)
{
	struct ibv_sge sge;
	struct ibv_recv_wr wr, *bad_wr;

	sge.addr = (uintptr_t) ctx.buf;
	sge.length = ctx.size;
	sge.lkey = ctx.mr->lkey;

	wr.next       = NULL;
	wr.wr_id      = (uintptr_t) &ctx;
	wr.sg_list    = &sge;
	wr.num_sge    = 1;

	while (cnt--) {
		if (ibv_post_srq_recv(ctx.srq, &wr, &bad_wr)) {
			fprintf(stderr, "Failed to post receive to SRQ\n");
			return 1;
		}
	}
	return 0;
}

/*
 * Send to each client round robin on each set of xrc send/recv qp.
 * Generate a completion on the last send.
 */
static int pp_post_send(int index)
{
	struct ibv_sge sge;
	struct ibv_send_wr wr, *bad_wr;
	int qpi;

	sge.addr = (uintptr_t) ctx.buf;
	sge.length = ctx.size;
	sge.lkey = ctx.mr->lkey;

	wr.wr_id   = (uintptr_t) index;
	wr.next    = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode  = IBV_WR_SEND;
	wr.qp_type.xrc.remote_srqn = ctx.rem_dest[index].srqn;

	qpi = (index + ctx.rem_dest[index].pp_cnt) % ctx.num_clients;
	wr.send_flags = (++ctx.rem_dest[index].pp_cnt >= ctx.num_tests) ?
			IBV_SEND_SIGNALED : 0;

	return ibv_post_send(ctx.send_qp[qpi], &wr, &bad_wr);
}

static int find_qp(int qpn)
{
	int i;

	if (ctx.num_clients == 1)
		return 0;

	for (i = 0; i < ctx.num_clients; ++i)
		if (ctx.recv_qp[i]->qp_num == qpn)
			return i;

	fprintf(stderr, "Unable to find qp %x\n", qpn);
	return 0;
}

static int get_cq_event(void)
{
	struct ibv_cq *ev_cq;
	void          *ev_ctx;

	if (ibv_get_cq_event(ctx.channel, &ev_cq, &ev_ctx)) {
		fprintf(stderr, "Failed to get cq_event\n");
		return 1;
	}

	if (ev_cq != ctx.recv_cq) {
		fprintf(stderr, "CQ event for unknown CQ %p\n", ev_cq);
		return 1;
	}

	if (ibv_req_notify_cq(ctx.recv_cq, 0)) {
		fprintf(stderr, "Couldn't request CQ notification\n");
		return 1;
	}

	return 0;
}

static void init(void)
{
	srand48(getpid() * time(NULL));

	ctx.size = 4096;
	ctx.ib_port = 1;
	ctx.num_clients  = 1;
	ctx.num_tests = 5;
	ctx.mtu = IBV_MTU_2048;
	ctx.sl = 0;
	ctx.gidx = -1;
}

static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s            start a server and wait for connection\n", argv0);
	printf("  %s <host>     connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -p, --port=<port>      listen on/connect to port <port> (default 18515)\n");
	printf("  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
	printf("  -i, --ib-port=<port>   use port <port> of IB device (default 1)\n");
	printf("  -s, --size=<size>      size of message to exchange (default 4096)\n");
	printf("  -m, --mtu=<size>       path MTU (default 2048)\n");
	printf("  -c, --clients=<n>      number of clients (on server only, default 1)\n");
	printf("  -n, --num_tests=<n>    number of tests per client (default 5)\n");
	printf("  -l, --sl=<sl>          service level value\n");
	printf("  -e, --events           sleep on CQ events (default poll)\n");
	printf("  -g, --gid-idx=<gid index> local port gid index\n");
}

int main(int argc, char *argv[])
{
	char          *ib_devname = NULL;
	char          *servername = NULL;
	int           port = 18515;
	int           i, total, cnt = 0;
	int           ne, qpi, num_cq_events = 0;
	struct ibv_wc wc;

	init();
	while (1) {
		int c;

		static struct option long_options[] = {
			{ .name = "port",      .has_arg = 1, .val = 'p' },
			{ .name = "ib-dev",    .has_arg = 1, .val = 'd' },
			{ .name = "ib-port",   .has_arg = 1, .val = 'i' },
			{ .name = "size",      .has_arg = 1, .val = 's' },
			{ .name = "mtu",       .has_arg = 1, .val = 'm' },
			{ .name = "clients",   .has_arg = 1, .val = 'c' },
			{ .name = "num_tests", .has_arg = 1, .val = 'n' },
			{ .name = "sl",        .has_arg = 1, .val = 'l' },
			{ .name = "events",    .has_arg = 0, .val = 'e' },
			{ .name = "gid-idx",   .has_arg = 1, .val = 'g' },
			{}
		};

		c = getopt_long(argc, argv, "p:d:i:s:m:c:n:l:eg:", long_options,
				NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'p':
			port = strtol(optarg, NULL, 0);
			if (port < 0 || port > 65535) {
				usage(argv[0]);
				return 1;
			}
			break;
		case 'd':
			ib_devname = strdupa(optarg);
			break;
		case 'i':
			ctx.ib_port = strtol(optarg, NULL, 0);
			if (ctx.ib_port < 0) {
				usage(argv[0]);
				return 1;
			}
			break;
		case 's':
			ctx.size = strtol(optarg, NULL, 0);
			break;
		case 'm':
			ctx.mtu = pp_mtu_to_enum(strtol(optarg, NULL, 0));
			if (ctx.mtu == 0) {
				usage(argv[0]);
				return 1;
			}
			break;
		case 'c':
			ctx.num_clients = strtol(optarg, NULL, 0);
			break;
		case 'n':
			ctx.num_tests = strtol(optarg, NULL, 0);
			break;
		case 'l':
			ctx.sl = strtol(optarg, NULL, 0);
			break;
		case 'g':
			ctx.gidx = strtol(optarg, NULL, 0);
			break;
		case 'e':
			ctx.use_event = 1;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (optind == argc - 1) {
		servername = strdupa(argv[optind]);
		ctx.num_clients = 1;
	} else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	page_size = sysconf(_SC_PAGESIZE);

	if (pp_init_ctx(ib_devname))
		return 1;

	if (pp_post_recv(ctx.num_clients)) {
		fprintf(stderr, "Couldn't post receives\n");
		return 1;
	}

	if (servername) {
		if (pp_client_connect(servername, port))
			return 1;
	} else {
		if (pp_server_connect(port))
			return 1;

		for (i = 0; i < ctx.num_clients; i++)
			pp_post_send(i);
	}

	total = ctx.num_clients * ctx.num_tests;
	while (cnt < total) {
		if (ctx.use_event) {
			if (get_cq_event())
				return 1;

			++num_cq_events;
		}

		do {
			ne = ibv_poll_cq(ctx.recv_cq, 1, &wc);
			if (ne < 0) {
				fprintf(stderr, "Error polling cq %d\n", ne);
				return 1;
			} else if (ne == 0) {
				break;
			}

			if (wc.status) {
				fprintf(stderr, "Work completion error %d\n", wc.status);
				return 1;
			}

			pp_post_recv(ne);
			qpi = find_qp(wc.qp_num);
			if (ctx.rem_dest[qpi].pp_cnt < ctx.num_tests)
				pp_post_send(qpi);
			cnt += ne;
		} while (ne > 0);
	}

	for (cnt = 0; cnt < ctx.num_clients; cnt += ne) {
		ne = ibv_poll_cq(ctx.send_cq, 1, &wc);
		if (ne < 0) {
			fprintf(stderr, "Error polling cq %d\n", ne);
			return 1;
		}
	}

	if (ctx.use_event)
		ibv_ack_cq_events(ctx.recv_cq, num_cq_events);

	/* Process should get an ack from the daemon to close its resources to
	  * make sure latest daemon's response sent via its target QP destined
	  * to an XSRQ created by another client won't be lost.
	  * Failure to do so may cause the other client to wait for that sent
	  * message forever. See comment on pp_post_send.
	*/
	if (servername) {
		if (pp_client_termination())
			return 1;
	} else if (pp_server_termination()) {
		return 1;
	}

	if (pp_close_ctx())
		return 1;

	printf("success\n");
	return 0;
}
