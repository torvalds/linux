/*
 * Copyright (c) 2005-2014 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <ctype.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

static const char *port = "7471";

static struct rdma_cm_id *listen_id, *id;
static struct ibv_mr *mr;
static struct rdma_addrinfo hints;

static uint8_t recv_msg[16];
static __be32 srqn;

static int create_srq(void)
{
	struct ibv_srq_init_attr attr;
	int ret;
	uint32_t tmp_srqn;

	attr.attr.max_wr = 1;
	attr.attr.max_sge = 1;
	attr.attr.srq_limit = 0;
	attr.srq_context = id;

	ret = rdma_create_srq(id, NULL, &attr);
	if (ret)
		perror("rdma_create_srq:");

	if (id->srq) {
		ibv_get_srq_num(id->srq, &tmp_srqn);
		srqn = htobe32(tmp_srqn);
	}
	return ret;
}

static int test(void)
{
	struct rdma_addrinfo *res;
	struct ibv_qp_init_attr attr;
	struct rdma_conn_param param;
	struct ibv_wc wc;
	int ret;

	ret = rdma_getaddrinfo(NULL, port, &hints, &res);
	if (ret) {
		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	memset(&attr, 0, sizeof attr);
	attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
	attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
	ret = rdma_create_ep(&listen_id, res, NULL, &attr);
	rdma_freeaddrinfo(res);
	if (ret) {
		perror("rdma_create_ep");
		return ret;
	}

	ret = rdma_listen(listen_id, 0);
	if (ret) {
		perror("rdma_listen");
		return ret;
	}

	ret = rdma_get_request(listen_id, &id);
	if (ret) {
		perror("rdma_get_request");
		return ret;
	}

	if (hints.ai_qp_type == IBV_QPT_XRC_RECV) {
		ret = create_srq();
		if (ret)
			return ret;
	}

	mr = rdma_reg_msgs(id, recv_msg, sizeof recv_msg);
	if (!mr) {
		perror("rdma_reg_msgs");
		return ret;
	}

	ret = rdma_post_recv(id, NULL, recv_msg, sizeof recv_msg, mr);
	if (ret) {
		perror("rdma_post_recv");
		return ret;
	}

	memset(&param, 0, sizeof param);
	param.private_data = &srqn;
	param.private_data_len = sizeof srqn;
	ret = rdma_accept(id, &param);
	if (ret) {
		perror("rdma_accept");
		return ret;
	}

	ret = rdma_get_recv_comp(id, &wc);
	if (ret <= 0) {
		perror("rdma_get_recv_comp");
		return ret;
	}

	rdma_disconnect(id);
	rdma_dereg_mr(mr);
	rdma_destroy_ep(id);
	rdma_destroy_ep(listen_id);
	return 0;
}

int main(int argc, char **argv)
{
	int op, ret;

	hints.ai_flags = RAI_PASSIVE;
	hints.ai_port_space = RDMA_PS_TCP;
	hints.ai_qp_type = IBV_QPT_RC;

	while ((op = getopt(argc, argv, "p:c:")) != -1) {
		switch (op) {
		case 'p':
			port = optarg;
			break;
		case 'c':
			switch (tolower(optarg[0])) {
			case 'r':
				break;
			case 'x':
				hints.ai_port_space = RDMA_PS_IB;
				hints.ai_qp_type = IBV_QPT_XRC_RECV;
				break;
			default:
				goto err;
			}
			break;
		default:
			goto err;
		}
	}

	printf("%s: start\n", argv[0]);
	ret = test();
	printf("%s: end %d\n", argv[0], ret);
	return ret;

err:
	printf("usage: %s\n", argv[0]);
	printf("\t[-p port_number]\n");
	printf("\t[-c communication type]\n");
	printf("\t    r - RC: reliable-connected (default)\n");
	printf("\t    x - XRC: extended-reliable-connected\n");
	exit(1);
}
