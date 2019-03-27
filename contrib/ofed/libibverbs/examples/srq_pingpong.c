/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <stdlib.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>

#include "pingpong.h"

enum {
	PINGPONG_RECV_WRID = 1,
	PINGPONG_SEND_WRID = 2,

	MAX_QP             = 256,
};

static int page_size;

struct pingpong_context {
	struct ibv_context	*context;
	struct ibv_comp_channel *channel;
	struct ibv_pd		*pd;
	struct ibv_mr		*mr;
	struct ibv_cq		*cq;
	struct ibv_srq		*srq;
	struct ibv_qp		*qp[MAX_QP];
	void			*buf;
	int			 size;
	int			 send_flags;
	int			 num_qp;
	int			 rx_depth;
	int			 pending[MAX_QP];
	struct ibv_port_attr	 portinfo;
};

struct pingpong_dest {
	int lid;
	int qpn;
	int psn;
	union ibv_gid gid;
};

static int pp_connect_ctx(struct pingpong_context *ctx, int port, enum ibv_mtu mtu,
			  int sl, const struct pingpong_dest *my_dest,
			  const struct pingpong_dest *dest, int sgid_idx)
{
	int i;

	for (i = 0; i < ctx->num_qp; ++i) {
		struct ibv_qp_attr attr = {
			.qp_state		= IBV_QPS_RTR,
			.path_mtu		= mtu,
			.dest_qp_num		= dest[i].qpn,
			.rq_psn			= dest[i].psn,
			.max_dest_rd_atomic	= 1,
			.min_rnr_timer		= 12,
			.ah_attr		= {
				.is_global	= 0,
				.dlid		= dest[i].lid,
				.sl		= sl,
				.src_path_bits	= 0,
				.port_num	= port
			}
		};

		if (dest->gid.global.interface_id) {
			attr.ah_attr.is_global = 1;
			attr.ah_attr.grh.hop_limit = 1;
			attr.ah_attr.grh.dgid = dest->gid;
			attr.ah_attr.grh.sgid_index = sgid_idx;
		}
		if (ibv_modify_qp(ctx->qp[i], &attr,
				  IBV_QP_STATE              |
				  IBV_QP_AV                 |
				  IBV_QP_PATH_MTU           |
				  IBV_QP_DEST_QPN           |
				  IBV_QP_RQ_PSN             |
				  IBV_QP_MAX_DEST_RD_ATOMIC |
				  IBV_QP_MIN_RNR_TIMER)) {
			fprintf(stderr, "Failed to modify QP[%d] to RTR\n", i);
			return 1;
		}

		attr.qp_state	    = IBV_QPS_RTS;
		attr.timeout	    = 14;
		attr.retry_cnt	    = 7;
		attr.rnr_retry	    = 7;
		attr.sq_psn	    = my_dest[i].psn;
		attr.max_rd_atomic  = 1;
		if (ibv_modify_qp(ctx->qp[i], &attr,
				  IBV_QP_STATE              |
				  IBV_QP_TIMEOUT            |
				  IBV_QP_RETRY_CNT          |
				  IBV_QP_RNR_RETRY          |
				  IBV_QP_SQ_PSN             |
				  IBV_QP_MAX_QP_RD_ATOMIC)) {
			fprintf(stderr, "Failed to modify QP[%d] to RTS\n", i);
			return 1;
		}
	}

	return 0;
}

static struct pingpong_dest *pp_client_exch_dest(const char *servername, int port,
						 const struct pingpong_dest *my_dest)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int n;
	int r;
	int i;
	int sockfd = -1;
	struct pingpong_dest *rem_dest = NULL;
	char gid[33];

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(servername, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
		free(service);
		return NULL;
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
		return NULL;
	}

	for (i = 0; i < MAX_QP; ++i) {
		gid_to_wire_gid(&my_dest[i].gid, gid);
		sprintf(msg, "%04x:%06x:%06x:%s", my_dest[i].lid,
					my_dest[i].qpn, my_dest[i].psn, gid);
		if (write(sockfd, msg, sizeof msg) != sizeof msg) {
			fprintf(stderr, "Couldn't send local address\n");
			goto out;
		}
	}

	rem_dest = malloc(MAX_QP * sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	for (i = 0; i < MAX_QP; ++i) {
		n = 0;
		while (n < sizeof msg) {
			r = read(sockfd, msg + n, sizeof msg - n);
			if (r < 0) {
				perror("client read");
				fprintf(stderr, "%d/%d: Couldn't read remote address [%d]\n",
					n, (int) sizeof msg, i);
				goto out;
			}
			n += r;
		}

		sscanf(msg, "%x:%x:%x:%s", &rem_dest[i].lid, &rem_dest[i].qpn,
							&rem_dest[i].psn, gid);
		wire_gid_to_gid(gid, &rem_dest[i].gid);
	}

	if (write(sockfd, "done", sizeof "done") != sizeof "done") {
		perror("client write");
		goto out;
	}
out:
	close(sockfd);
	return rem_dest;
}

static struct pingpong_dest *pp_server_exch_dest(struct pingpong_context *ctx,
						 int ib_port, enum ibv_mtu mtu,
						 int port, int sl,
						 const struct pingpong_dest *my_dest,
						 int sgid_idx)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_flags    = AI_PASSIVE,
		.ai_family   = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int n;
	int r;
	int i;
	int sockfd = -1, connfd;
	struct pingpong_dest *rem_dest = NULL;
	char gid[33];

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(NULL, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
		free(service);
		return NULL;
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
		return NULL;
	}

	listen(sockfd, 1);
	connfd = accept(sockfd, NULL, NULL);
	close(sockfd);
	if (connfd < 0) {
		fprintf(stderr, "accept() failed\n");
		return NULL;
	}

	rem_dest = malloc(MAX_QP * sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	for (i = 0; i < MAX_QP; ++i) {
		n = 0;
		while (n < sizeof msg) {
			r = read(connfd, msg + n, sizeof msg - n);
			if (r < 0) {
				perror("server read");
				fprintf(stderr, "%d/%d: Couldn't read remote address [%d]\n",
					n, (int) sizeof msg, i);
				goto out;
			}
			n += r;
		}

		sscanf(msg, "%x:%x:%x:%s", &rem_dest[i].lid, &rem_dest[i].qpn,
							&rem_dest[i].psn, gid);
		wire_gid_to_gid(gid, &rem_dest[i].gid);
	}

	if (pp_connect_ctx(ctx, ib_port, mtu, sl, my_dest, rem_dest,
								sgid_idx)) {
		fprintf(stderr, "Couldn't connect to remote QP\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}

	for (i = 0; i < MAX_QP; ++i) {
		gid_to_wire_gid(&my_dest[i].gid, gid);
		sprintf(msg, "%04x:%06x:%06x:%s", my_dest[i].lid,
					my_dest[i].qpn, my_dest[i].psn, gid);
		if (write(connfd, msg, sizeof msg) != sizeof msg) {
			fprintf(stderr, "Couldn't send local address\n");
			free(rem_dest);
			rem_dest = NULL;
			goto out;
		}
	}

	if (read(connfd, msg, sizeof msg) != sizeof "done") {
		perror("client write");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}

out:
	close(connfd);
	return rem_dest;
}

static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, int size,
					    int num_qp, int rx_depth, int port,
					    int use_event)
{
	struct pingpong_context *ctx;
	int i;

	ctx = calloc(1, sizeof *ctx);
	if (!ctx)
		return NULL;

	ctx->size       = size;
	ctx->send_flags = IBV_SEND_SIGNALED;
	ctx->num_qp     = num_qp;
	ctx->rx_depth   = rx_depth;

	ctx->buf = memalign(page_size, size);
	if (!ctx->buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		goto clean_ctx;
	}

	memset(ctx->buf, 0, size);

	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			ibv_get_device_name(ib_dev));
		goto clean_buffer;
	}

	if (use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			fprintf(stderr, "Couldn't create completion channel\n");
			goto clean_device;
		}
	} else
		ctx->channel = NULL;

	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		goto clean_comp_channel;
	}

	ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, size, IBV_ACCESS_LOCAL_WRITE);
	if (!ctx->mr) {
		fprintf(stderr, "Couldn't register MR\n");
		goto clean_pd;
	}

	ctx->cq = ibv_create_cq(ctx->context, rx_depth + num_qp, NULL,
				ctx->channel, 0);
	if (!ctx->cq) {
		fprintf(stderr, "Couldn't create CQ\n");
		goto clean_mr;
	}

	{
		struct ibv_srq_init_attr attr = {
			.attr = {
				.max_wr  = rx_depth,
				.max_sge = 1
			}
		};

		ctx->srq = ibv_create_srq(ctx->pd, &attr);
		if (!ctx->srq)  {
			fprintf(stderr, "Couldn't create SRQ\n");
			goto clean_cq;
		}
	}

	for (i = 0; i < num_qp; ++i) {
		struct ibv_qp_attr attr;
		struct ibv_qp_init_attr init_attr = {
			.send_cq = ctx->cq,
			.recv_cq = ctx->cq,
			.srq     = ctx->srq,
			.cap     = {
				.max_send_wr  = 1,
				.max_send_sge = 1,
			},
			.qp_type = IBV_QPT_RC
		};

		ctx->qp[i] = ibv_create_qp(ctx->pd, &init_attr);
		if (!ctx->qp[i])  {
			fprintf(stderr, "Couldn't create QP[%d]\n", i);
			goto clean_qps;
		}
		ibv_query_qp(ctx->qp[i], &attr, IBV_QP_CAP, &init_attr);
		if (init_attr.cap.max_inline_data >= size) {
			ctx->send_flags |= IBV_SEND_INLINE;
		}
	}

	for (i = 0; i < num_qp; ++i) {
		struct ibv_qp_attr attr = {
			.qp_state        = IBV_QPS_INIT,
			.pkey_index      = 0,
			.port_num        = port,
			.qp_access_flags = 0
		};

		if (ibv_modify_qp(ctx->qp[i], &attr,
				  IBV_QP_STATE              |
				  IBV_QP_PKEY_INDEX         |
				  IBV_QP_PORT               |
				  IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify QP[%d] to INIT\n", i);
			goto clean_qps_full;
		}
	}

	return ctx;

clean_qps_full:
	i = num_qp;

clean_qps:
	for (--i; i >= 0; --i)
		ibv_destroy_qp(ctx->qp[i]);

	ibv_destroy_srq(ctx->srq);

clean_cq:
	ibv_destroy_cq(ctx->cq);

clean_mr:
	ibv_dereg_mr(ctx->mr);

clean_pd:
	ibv_dealloc_pd(ctx->pd);

clean_comp_channel:
	if (ctx->channel)
		ibv_destroy_comp_channel(ctx->channel);

clean_device:
	ibv_close_device(ctx->context);

clean_buffer:
	free(ctx->buf);

clean_ctx:
	free(ctx);

	return NULL;
}

static int pp_close_ctx(struct pingpong_context *ctx, int num_qp)
{
	int i;

	for (i = 0; i < num_qp; ++i) {
		if (ibv_destroy_qp(ctx->qp[i])) {
			fprintf(stderr, "Couldn't destroy QP[%d]\n", i);
			return 1;
		}
	}

	if (ibv_destroy_srq(ctx->srq)) {
		fprintf(stderr, "Couldn't destroy SRQ\n");
		return 1;
	}

	if (ibv_destroy_cq(ctx->cq)) {
		fprintf(stderr, "Couldn't destroy CQ\n");
		return 1;
	}

	if (ibv_dereg_mr(ctx->mr)) {
		fprintf(stderr, "Couldn't deregister MR\n");
		return 1;
	}

	if (ibv_dealloc_pd(ctx->pd)) {
		fprintf(stderr, "Couldn't deallocate PD\n");
		return 1;
	}

	if (ctx->channel) {
		if (ibv_destroy_comp_channel(ctx->channel)) {
			fprintf(stderr, "Couldn't destroy completion channel\n");
			return 1;
		}
	}

	if (ibv_close_device(ctx->context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}

	free(ctx->buf);
	free(ctx);

	return 0;
}

static int pp_post_recv(struct pingpong_context *ctx, int n)
{
	struct ibv_sge list = {
		.addr	= (uintptr_t) ctx->buf,
		.length = ctx->size,
		.lkey	= ctx->mr->lkey
	};
	struct ibv_recv_wr wr = {
		.wr_id	    = PINGPONG_RECV_WRID,
		.sg_list    = &list,
		.num_sge    = 1,
	};
	struct ibv_recv_wr *bad_wr;
	int i;

	for (i = 0; i < n; ++i)
		if (ibv_post_srq_recv(ctx->srq, &wr, &bad_wr))
			break;

	return i;
}

static int pp_post_send(struct pingpong_context *ctx, int qp_index)
{
	struct ibv_sge list = {
		.addr	= (uintptr_t) ctx->buf,
		.length = ctx->size,
		.lkey	= ctx->mr->lkey
	};
	struct ibv_send_wr wr = {
		.wr_id	    = PINGPONG_SEND_WRID,
		.sg_list    = &list,
		.num_sge    = 1,
		.opcode     = IBV_WR_SEND,
		.send_flags = ctx->send_flags,
	};
	struct ibv_send_wr *bad_wr;

	return ibv_post_send(ctx->qp[qp_index], &wr, &bad_wr);
}

static int find_qp(int qpn, struct pingpong_context *ctx, int num_qp)
{
	int i;

	for (i = 0; i < num_qp; ++i)
		if (ctx->qp[i]->qp_num == qpn)
			return i;

	return -1;
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
	printf("  -m, --mtu=<size>       path MTU (default 1024)\n");
	printf("  -q, --num-qp=<num>     number of QPs to use (default 16)\n");
	printf("  -r, --rx-depth=<dep>   number of receives to post at a time (default 500)\n");
	printf("  -n, --iters=<iters>    number of exchanges per QP(default 1000)\n");
	printf("  -l, --sl=<sl>          service level value\n");
	printf("  -e, --events           sleep on CQ events (default poll)\n");
	printf("  -g, --gid-idx=<gid index> local port gid index\n");
}

int main(int argc, char *argv[])
{
	struct ibv_device      **dev_list;
	struct ibv_device	*ib_dev;
	struct ibv_wc		*wc;
	struct pingpong_context *ctx;
	struct pingpong_dest     my_dest[MAX_QP];
	struct pingpong_dest    *rem_dest;
	struct timeval           start, end;
	char                    *ib_devname = NULL;
	char                    *servername = NULL;
	unsigned int             port = 18515;
	int                      ib_port = 1;
	unsigned int             size = 4096;
	enum ibv_mtu		 mtu = IBV_MTU_1024;
	unsigned int             num_qp = 16;
	unsigned int             rx_depth = 500;
	unsigned int             iters = 1000;
	int                      use_event = 0;
	int                      routs;
	int                      rcnt, scnt;
	int			 num_wc;
	int                      i;
	int                      num_cq_events = 0;
	int                      sl = 0;
	int			 gidx = -1;
	char			 gid[33];

	srand48(getpid() * time(NULL));

	while (1) {
		int c;

		static struct option long_options[] = {
			{ .name = "port",     .has_arg = 1, .val = 'p' },
			{ .name = "ib-dev",   .has_arg = 1, .val = 'd' },
			{ .name = "ib-port",  .has_arg = 1, .val = 'i' },
			{ .name = "size",     .has_arg = 1, .val = 's' },
			{ .name = "mtu",      .has_arg = 1, .val = 'm' },
			{ .name = "num-qp",   .has_arg = 1, .val = 'q' },
			{ .name = "rx-depth", .has_arg = 1, .val = 'r' },
			{ .name = "iters",    .has_arg = 1, .val = 'n' },
			{ .name = "sl",       .has_arg = 1, .val = 'l' },
			{ .name = "events",   .has_arg = 0, .val = 'e' },
			{ .name = "gid-idx",  .has_arg = 1, .val = 'g' },
			{}
		};

		c = getopt_long(argc, argv, "p:d:i:s:m:q:r:n:l:eg:",
							long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'p':
			port = strtoul(optarg, NULL, 0);
			if (port > 65535) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'd':
			ib_devname = strdupa(optarg);
			break;

		case 'i':
			ib_port = strtol(optarg, NULL, 0);
			if (ib_port < 1) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 's':
			size = strtoul(optarg, NULL, 0);
			if (size < 1) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'm':
			mtu = pp_mtu_to_enum(strtol(optarg, NULL, 0));
			if (mtu == 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'q':
			num_qp = strtoul(optarg, NULL, 0);
			break;

		case 'r':
			rx_depth = strtoul(optarg, NULL, 0);
			break;

		case 'n':
			iters = strtoul(optarg, NULL, 0);
			break;

		case 'l':
			sl = strtol(optarg, NULL, 0);
			break;

		case 'e':
			++use_event;
			break;

		case 'g':
			gidx = strtol(optarg, NULL, 0);
			break;

		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (optind == argc - 1)
		servername = strdupa(argv[optind]);
	else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	if (num_qp > rx_depth) {
		fprintf(stderr, "rx_depth %d is too small for %d QPs -- "
			"must have at least one receive per QP.\n",
			rx_depth, num_qp);
		return 1;
	}

	num_wc = num_qp + rx_depth;
	wc     = alloca(num_wc * sizeof *wc);

	page_size = sysconf(_SC_PAGESIZE);

	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		return 1;
	}

	if (!ib_devname) {
		ib_dev = *dev_list;
		if (!ib_dev) {
			fprintf(stderr, "No IB devices found\n");
			return 1;
		}
	} else {
		for (i = 0; dev_list[i]; ++i)
			if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
				break;
		ib_dev = dev_list[i];
		if (!ib_dev) {
			fprintf(stderr, "IB device %s not found\n", ib_devname);
			return 1;
		}
	}

	ctx = pp_init_ctx(ib_dev, size, num_qp, rx_depth, ib_port, use_event);
	if (!ctx)
		return 1;

	routs = pp_post_recv(ctx, ctx->rx_depth);
	if (routs < ctx->rx_depth) {
		fprintf(stderr, "Couldn't post receive (%d)\n", routs);
		return 1;
	}

	if (use_event)
		if (ibv_req_notify_cq(ctx->cq, 0)) {
			fprintf(stderr, "Couldn't request CQ notification\n");
			return 1;
		}

	memset(my_dest, 0, sizeof my_dest);

	if (pp_get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
		fprintf(stderr, "Couldn't get port info\n");
		return 1;
	}
	for (i = 0; i < num_qp; ++i) {
		my_dest[i].qpn = ctx->qp[i]->qp_num;
		my_dest[i].psn = lrand48() & 0xffffff;
		my_dest[i].lid = ctx->portinfo.lid;
		if (ctx->portinfo.link_layer != IBV_LINK_LAYER_ETHERNET
							&& !my_dest[i].lid) {
			fprintf(stderr, "Couldn't get local LID\n");
			return 1;
		}

		if (gidx >= 0) {
			if (ibv_query_gid(ctx->context, ib_port, gidx,
							&my_dest[i].gid)) {
				fprintf(stderr, "Could not get local gid for "
							"gid index %d\n", gidx);
				return 1;
			}
		} else
			memset(&my_dest[i].gid, 0, sizeof my_dest[i].gid);

		inet_ntop(AF_INET6, &my_dest[i].gid, gid, sizeof gid);
		printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, "
			"GID %s\n", my_dest[i].lid, my_dest[i].qpn,
			my_dest[i].psn, gid);
	}

	if (servername)
		rem_dest = pp_client_exch_dest(servername, port, my_dest);
	else
		rem_dest = pp_server_exch_dest(ctx, ib_port, mtu, port, sl,
								my_dest, gidx);

	if (!rem_dest)
		return 1;

	inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof gid);

	for (i = 0; i < num_qp; ++i) {
		inet_ntop(AF_INET6, &rem_dest[i].gid, gid, sizeof gid);
		printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, "
			"GID %s\n", rem_dest[i].lid, rem_dest[i].qpn,
			rem_dest[i].psn, gid);
	}

	if (servername)
		if (pp_connect_ctx(ctx, ib_port, mtu, sl, my_dest, rem_dest,
									gidx))
			return 1;

	if (servername)
		for (i = 0; i < num_qp; ++i) {
			if (pp_post_send(ctx, i)) {
				fprintf(stderr, "Couldn't post send\n");
				return 1;
			}
			ctx->pending[i] = PINGPONG_SEND_WRID | PINGPONG_RECV_WRID;
		}
	else
		for (i = 0; i < num_qp; ++i)
			ctx->pending[i] = PINGPONG_RECV_WRID;

	if (gettimeofday(&start, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	rcnt = scnt = 0;
	while (rcnt < iters || scnt < iters) {
		if (use_event) {
			struct ibv_cq *ev_cq;
			void          *ev_ctx;

			if (ibv_get_cq_event(ctx->channel, &ev_cq, &ev_ctx)) {
				fprintf(stderr, "Failed to get cq_event\n");
				return 1;
			}

			++num_cq_events;

			if (ev_cq != ctx->cq) {
				fprintf(stderr, "CQ event for unknown CQ %p\n", ev_cq);
				return 1;
			}

			if (ibv_req_notify_cq(ctx->cq, 0)) {
				fprintf(stderr, "Couldn't request CQ notification\n");
				return 1;
			}
		}

		{
			int ne, qp_ind;

			do {
				ne = ibv_poll_cq(ctx->cq, num_wc, wc);
				if (ne < 0) {
					fprintf(stderr, "poll CQ failed %d\n", ne);
					return 1;
				}
			} while (!use_event && ne < 1);

			for (i = 0; i < ne; ++i) {
				if (wc[i].status != IBV_WC_SUCCESS) {
					fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
						ibv_wc_status_str(wc[i].status),
						wc[i].status, (int) wc[i].wr_id);
					return 1;
				}

				qp_ind = find_qp(wc[i].qp_num, ctx, num_qp);
				if (qp_ind < 0) {
					fprintf(stderr, "Couldn't find QPN %06x\n",
						wc[i].qp_num);
					return 1;
				}

				switch ((int) wc[i].wr_id) {
				case PINGPONG_SEND_WRID:
					++scnt;
					break;

				case PINGPONG_RECV_WRID:
					if (--routs <= num_qp) {
						routs += pp_post_recv(ctx, ctx->rx_depth - routs);
						if (routs < ctx->rx_depth) {
							fprintf(stderr,
								"Couldn't post receive (%d)\n",
								routs);
							return 1;
						}
					}

					++rcnt;
					break;

				default:
					fprintf(stderr, "Completion for unknown wr_id %d\n",
						(int) wc[i].wr_id);
					return 1;
				}

				ctx->pending[qp_ind] &= ~(int) wc[i].wr_id;
				if (scnt < iters && !ctx->pending[qp_ind]) {
					if (pp_post_send(ctx, qp_ind)) {
						fprintf(stderr, "Couldn't post send\n");
						return 1;
					}
					ctx->pending[qp_ind] = PINGPONG_RECV_WRID |
							       PINGPONG_SEND_WRID;
				}

			}
		}
	}

	if (gettimeofday(&end, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	{
		float usec = (end.tv_sec - start.tv_sec) * 1000000 +
			(end.tv_usec - start.tv_usec);
		long long bytes = (long long) size * iters * 2;

		printf("%lld bytes in %.2f seconds = %.2f Mbit/sec\n",
		       bytes, usec / 1000000., bytes * 8. / usec);
		printf("%d iters in %.2f seconds = %.2f usec/iter\n",
		       iters, usec / 1000000., usec / iters);
	}

	ibv_ack_cq_events(ctx->cq, num_cq_events);

	if (pp_close_ctx(ctx, num_qp))
		return 1;

	ibv_free_device_list(dev_list);
	free(rem_dest);

	return 0;
}
