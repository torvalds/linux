/*
 * Copyright (c) 2005-2006 Intel Corporation.  All rights reserved.
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
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <getopt.h>

#include <rdma/rdma_cma.h>
#include "common.h"

struct cmatest_node {
	int			id;
	struct rdma_cm_id	*cma_id;
	int			connected;
	struct ibv_pd		*pd;
	struct ibv_cq		*cq;
	struct ibv_mr		*mr;
	struct ibv_ah		*ah;
	uint32_t		remote_qpn;
	uint32_t		remote_qkey;
	void			*mem;
};

struct cmatest {
	struct rdma_event_channel *channel;
	struct cmatest_node	*nodes;
	int			conn_index;
	int			connects_left;

	struct rdma_addrinfo	*rai;
};

static struct cmatest test;
static int connections = 1;
static int message_size = 100;
static int message_count = 10;
static const char *port = "7174";
static uint8_t set_tos = 0;
static uint8_t tos;
static char *dst_addr;
static char *src_addr;
static struct rdma_addrinfo hints;

static int create_message(struct cmatest_node *node)
{
	if (!message_size)
		message_count = 0;

	if (!message_count)
		return 0;

	node->mem = malloc(message_size + sizeof(struct ibv_grh));
	if (!node->mem) {
		printf("failed message allocation\n");
		return -1;
	}
	node->mr = ibv_reg_mr(node->pd, node->mem,
			      message_size + sizeof(struct ibv_grh),
			      IBV_ACCESS_LOCAL_WRITE);
	if (!node->mr) {
		printf("failed to reg MR\n");
		goto err;
	}
	return 0;
err:
	free(node->mem);
	return -1;
}

static int verify_test_params(struct cmatest_node *node)
{
	struct ibv_port_attr port_attr;
	int ret;

	ret = ibv_query_port(node->cma_id->verbs, node->cma_id->port_num,
			     &port_attr);
	if (ret)
		return ret;

	if (message_count && message_size > (1 << (port_attr.active_mtu + 7))) {
		printf("udaddy: message_size %d is larger than active mtu %d\n",
		       message_size, 1 << (port_attr.active_mtu + 7));
		return -EINVAL;
	}

	return 0;
}

static int init_node(struct cmatest_node *node)
{
	struct ibv_qp_init_attr init_qp_attr;
	int cqe, ret;

	node->pd = ibv_alloc_pd(node->cma_id->verbs);
	if (!node->pd) {
		ret = -ENOMEM;
		printf("udaddy: unable to allocate PD\n");
		goto out;
	}

	cqe = message_count ? message_count * 2 : 2;
	node->cq = ibv_create_cq(node->cma_id->verbs, cqe, node, NULL, 0);
	if (!node->cq) {
		ret = -ENOMEM;
		printf("udaddy: unable to create CQ\n");
		goto out;
	}

	memset(&init_qp_attr, 0, sizeof init_qp_attr);
	init_qp_attr.cap.max_send_wr = message_count ? message_count : 1;
	init_qp_attr.cap.max_recv_wr = message_count ? message_count : 1;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.qp_context = node;
	init_qp_attr.sq_sig_all = 0;
	init_qp_attr.qp_type = IBV_QPT_UD;
	init_qp_attr.send_cq = node->cq;
	init_qp_attr.recv_cq = node->cq;
	ret = rdma_create_qp(node->cma_id, node->pd, &init_qp_attr);
	if (ret) {
		perror("udaddy: unable to create QP");
		goto out;
	}

	ret = create_message(node);
	if (ret) {
		printf("udaddy: failed to create messages: %d\n", ret);
		goto out;
	}
out:
	return ret;
}

static int post_recvs(struct cmatest_node *node)
{
	struct ibv_recv_wr recv_wr, *recv_failure;
	struct ibv_sge sge;
	int i, ret = 0;

	if (!message_count)
		return 0;

	recv_wr.next = NULL;
	recv_wr.sg_list = &sge;
	recv_wr.num_sge = 1;
	recv_wr.wr_id = (uintptr_t) node;

	sge.length = message_size + sizeof(struct ibv_grh);
	sge.lkey = node->mr->lkey;
	sge.addr = (uintptr_t) node->mem;

	for (i = 0; i < message_count && !ret; i++ ) {
		ret = ibv_post_recv(node->cma_id->qp, &recv_wr, &recv_failure);
		if (ret) {
			printf("failed to post receives: %d\n", ret);
			break;
		}
	}
	return ret;
}

static int post_sends(struct cmatest_node *node, int signal_flag)
{
	struct ibv_send_wr send_wr, *bad_send_wr;
	struct ibv_sge sge;
	int i, ret = 0;

	if (!node->connected || !message_count)
		return 0;

	send_wr.next = NULL;
	send_wr.sg_list = &sge;
	send_wr.num_sge = 1;
	send_wr.opcode = IBV_WR_SEND_WITH_IMM;
	send_wr.send_flags = signal_flag;
	send_wr.wr_id = (unsigned long)node;
	send_wr.imm_data = htobe32(node->cma_id->qp->qp_num);

	send_wr.wr.ud.ah = node->ah;
	send_wr.wr.ud.remote_qpn = node->remote_qpn;
	send_wr.wr.ud.remote_qkey = node->remote_qkey;

	sge.length = message_size;
	sge.lkey = node->mr->lkey;
	sge.addr = (uintptr_t) node->mem;

	for (i = 0; i < message_count && !ret; i++) {
		ret = ibv_post_send(node->cma_id->qp, &send_wr, &bad_send_wr);
		if (ret) 
			printf("failed to post sends: %d\n", ret);
	}
	return ret;
}

static void connect_error(void)
{
	test.connects_left--;
}

static int addr_handler(struct cmatest_node *node)
{
	int ret;

	if (set_tos) {
		ret = rdma_set_option(node->cma_id, RDMA_OPTION_ID,
				      RDMA_OPTION_ID_TOS, &tos, sizeof tos);
		if (ret)
			perror("udaddy: set TOS option failed");
	}

	ret = rdma_resolve_route(node->cma_id, 2000);
	if (ret) {
		perror("udaddy: resolve route failed");
		connect_error();
	}
	return ret;
}

static int route_handler(struct cmatest_node *node)
{
	struct rdma_conn_param conn_param;
	int ret;

	ret = verify_test_params(node);
	if (ret)
		goto err;

	ret = init_node(node);
	if (ret)
		goto err;

	ret = post_recvs(node);
	if (ret)
		goto err;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.private_data = test.rai->ai_connect;
	conn_param.private_data_len = test.rai->ai_connect_len;
	ret = rdma_connect(node->cma_id, &conn_param);
	if (ret) {
		perror("udaddy: failure connecting");
		goto err;
	}
	return 0;
err:
	connect_error();
	return ret;
}

static int connect_handler(struct rdma_cm_id *cma_id)
{
	struct cmatest_node *node;
	struct rdma_conn_param conn_param;
	int ret;

	if (test.conn_index == connections) {
		ret = -ENOMEM;
		goto err1;
	}
	node = &test.nodes[test.conn_index++];

	node->cma_id = cma_id;
	cma_id->context = node;

	ret = verify_test_params(node);
	if (ret)
		goto err2;

	ret = init_node(node);
	if (ret)
		goto err2;

	ret = post_recvs(node);
	if (ret)
		goto err2;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.qp_num = node->cma_id->qp->qp_num;
	ret = rdma_accept(node->cma_id, &conn_param);
	if (ret) {
		perror("udaddy: failure accepting");
		goto err2;
	}
	node->connected = 1;
	test.connects_left--;
	return 0;

err2:
	node->cma_id = NULL;
	connect_error();
err1:
	printf("udaddy: failing connection request\n");
	rdma_reject(cma_id, NULL, 0);
	return ret;
}

static int resolved_handler(struct cmatest_node *node,
			    struct rdma_cm_event *event)
{
	node->remote_qpn = event->param.ud.qp_num;
	node->remote_qkey = event->param.ud.qkey;
	node->ah = ibv_create_ah(node->pd, &event->param.ud.ah_attr);
	if (!node->ah) {
		printf("udaddy: failure creating address handle\n");
		goto err;
	}

	node->connected = 1;
	test.connects_left--;
	return 0;
err:
	connect_error();
	return -1;
}

static int cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	int ret = 0;

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		ret = addr_handler(cma_id->context);
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ret = route_handler(cma_id->context);
		break;
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = connect_handler(cma_id);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		ret = resolved_handler(cma_id->context, event);
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		printf("udaddy: event: %s, error: %d\n",
		       rdma_event_str(event->event), event->status);
		connect_error();
		ret = event->status;
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		/* Cleanup will occur after test completes. */
		break;
	default:
		break;
	}
	return ret;
}

static void destroy_node(struct cmatest_node *node)
{
	if (!node->cma_id)
		return;

	if (node->ah)
		ibv_destroy_ah(node->ah);

	if (node->cma_id->qp)
		rdma_destroy_qp(node->cma_id);

	if (node->cq)
		ibv_destroy_cq(node->cq);

	if (node->mem) {
		ibv_dereg_mr(node->mr);
		free(node->mem);
	}

	if (node->pd)
		ibv_dealloc_pd(node->pd);

	/* Destroy the RDMA ID after all device resources */
	rdma_destroy_id(node->cma_id);
}

static int alloc_nodes(void)
{
	int ret, i;

	test.nodes = malloc(sizeof *test.nodes * connections);
	if (!test.nodes) {
		printf("udaddy: unable to allocate memory for test nodes\n");
		return -ENOMEM;
	}
	memset(test.nodes, 0, sizeof *test.nodes * connections);

	for (i = 0; i < connections; i++) {
		test.nodes[i].id = i;
		if (dst_addr) {
			ret = rdma_create_id(test.channel,
					     &test.nodes[i].cma_id,
					     &test.nodes[i], hints.ai_port_space);
			if (ret)
				goto err;
		}
	}
	return 0;
err:
	while (--i >= 0)
		rdma_destroy_id(test.nodes[i].cma_id);
	free(test.nodes);
	return ret;
}

static void destroy_nodes(void)
{
	int i;

	for (i = 0; i < connections; i++)
		destroy_node(&test.nodes[i]);
	free(test.nodes);
}

static void create_reply_ah(struct cmatest_node *node, struct ibv_wc *wc)
{
	struct ibv_qp_attr attr;
	struct ibv_qp_init_attr init_attr;

	node->ah = ibv_create_ah_from_wc(node->pd, wc, node->mem,
					 node->cma_id->port_num);
	node->remote_qpn = be32toh(wc->imm_data);

	ibv_query_qp(node->cma_id->qp, &attr, IBV_QP_QKEY, &init_attr);
	node->remote_qkey = attr.qkey;
}

static int poll_cqs(void)
{
	struct ibv_wc wc[8];
	int done, i, ret;

	for (i = 0; i < connections; i++) {
		if (!test.nodes[i].connected)
			continue;

		for (done = 0; done < message_count; done += ret) {
			ret = ibv_poll_cq(test.nodes[i].cq, 8, wc);
			if (ret < 0) {
				printf("udaddy: failed polling CQ: %d\n", ret);
				return ret;
			}

			if (ret && !test.nodes[i].ah)
				create_reply_ah(&test.nodes[i], wc);
		}
	}
	return 0;
}

static int connect_events(void)
{
	struct rdma_cm_event *event;
	int ret = 0;

	while (test.connects_left && !ret) {
		ret = rdma_get_cm_event(test.channel, &event);
		if (!ret) {
			ret = cma_handler(event->id, event);
			rdma_ack_cm_event(event);
		}
	}
	return ret;
}

static int run_server(void)
{
	struct rdma_cm_id *listen_id;
	int i, ret;

	printf("udaddy: starting server\n");
	ret = rdma_create_id(test.channel, &listen_id, &test, hints.ai_port_space);
	if (ret) {
		perror("udaddy: listen request failed");
		return ret;
	}

	ret = get_rdma_addr(src_addr, dst_addr, port, &hints, &test.rai);
	if (ret) {
		printf("udaddy: getrdmaaddr error: %s\n", gai_strerror(ret));
		goto out;
	}

	ret = rdma_bind_addr(listen_id, test.rai->ai_src_addr);
	if (ret) {
		perror("udaddy: bind address failed");
		goto out;
	}

	ret = rdma_listen(listen_id, 0);
	if (ret) {
		perror("udaddy: failure trying to listen");
		goto out;
	}

	connect_events();

	if (message_count) {
		printf("receiving data transfers\n");
		ret = poll_cqs();
		if (ret)
			goto out;

		printf("sending replies\n");
		for (i = 0; i < connections; i++) {
			ret = post_sends(&test.nodes[i], IBV_SEND_SIGNALED);
			if (ret)
				goto out;
		}

		ret = poll_cqs();
		if (ret)
			goto out;
		printf("data transfers complete\n");
	}
out:
	rdma_destroy_id(listen_id);
	return ret;
}

static int run_client(void)
{
	int i, ret;

	printf("udaddy: starting client\n");

	ret = get_rdma_addr(src_addr, dst_addr, port, &hints, &test.rai);
	if (ret) {
		printf("udaddy: getaddrinfo error: %s\n", gai_strerror(ret));
		return ret;
	}

	printf("udaddy: connecting\n");
	for (i = 0; i < connections; i++) {
		ret = rdma_resolve_addr(test.nodes[i].cma_id, test.rai->ai_src_addr,
					test.rai->ai_dst_addr, 2000);
		if (ret) {
			perror("udaddy: failure getting addr");
			connect_error();
			return ret;
		}
	}

	ret = connect_events();
	if (ret)
		goto out;

	if (message_count) {
		printf("initiating data transfers\n");
		for (i = 0; i < connections; i++) {
			ret = post_sends(&test.nodes[i], 0);
			if (ret)
				goto out;
		}
		printf("receiving data transfers\n");
		ret = poll_cqs();
		if (ret)
			goto out;

		printf("data transfers complete\n");
	}
out:
	return ret;
}

int main(int argc, char **argv)
{
	int op, ret;

	hints.ai_port_space = RDMA_PS_UDP;
	while ((op = getopt(argc, argv, "s:b:c:C:S:t:p:P:f:")) != -1) {
		switch (op) {
		case 's':
			dst_addr = optarg;
			break;
		case 'b':
			src_addr = optarg;
			break;
		case 'c':
			connections = atoi(optarg);
			break;
		case 'C':
			message_count = atoi(optarg);
			break;
		case 'S':
			message_size = atoi(optarg);
			break;
		case 't':
			set_tos = 1;
			tos = (uint8_t) strtoul(optarg, NULL, 0);
			break;
		case 'p': /* for backwards compatibility - use -P */
			hints.ai_port_space = strtol(optarg, NULL, 0);
			break;
		case 'f':
			if (!strncasecmp("ip", optarg, 2)) {
				hints.ai_flags = RAI_NUMERICHOST;
			} else if (!strncasecmp("gid", optarg, 3)) {
				hints.ai_flags = RAI_NUMERICHOST | RAI_FAMILY;
				hints.ai_family = AF_IB;
			} else if (strncasecmp("name", optarg, 4)) {
				fprintf(stderr, "Warning: unknown address format\n");
			}
			break;
		case 'P':
			if (!strncasecmp("ipoib", optarg, 5)) {
				hints.ai_port_space = RDMA_PS_IPOIB;
			} else if (strncasecmp("udp", optarg, 3)) {
				fprintf(stderr, "Warning: unknown port space format\n");
			}
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-s server_address]\n");
			printf("\t[-b bind_address]\n");
			printf("\t[-f address_format]\n");
			printf("\t    name, ip, ipv6, or gid\n");
			printf("\t[-P port_space]\n");
			printf("\t    udp or ipoib\n");
			printf("\t[-c connections]\n");
			printf("\t[-C message_count]\n");
			printf("\t[-S message_size]\n");
			printf("\t[-t type_of_service]\n");
			printf("\t[-p port_space - %#x for UDP (default), "
			       "%#x for IPOIB]\n", RDMA_PS_UDP, RDMA_PS_IPOIB);
			exit(1);
		}
	}

	test.connects_left = connections;

	test.channel = rdma_create_event_channel();
	if (!test.channel) {
		perror("failed to create event channel");
		exit(1);
	}

	if (alloc_nodes())
		exit(1);

	if (dst_addr) {
		ret = run_client();
	} else {
		hints.ai_flags |= RAI_PASSIVE;
		ret = run_server();
	}

	printf("test complete\n");
	destroy_nodes();
	rdma_destroy_event_channel(test.channel);
	if (test.rai)
		rdma_freeaddrinfo(test.rai);

	printf("return status %d\n", ret);
	return ret;
}
