/*
 * Copyright (c) 2010-2014 Intel Corporation.  All rights reserved.
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
#include <netdb.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

static const char *server = "127.0.0.1";
static char port[6] = "7471";

static struct rdma_cm_id *id;
static struct ibv_mr *mr;
static struct rdma_addrinfo hints;

static uint8_t send_msg[16];
static uint32_t srqn;

static int post_send(void)
{
	struct ibv_send_wr wr, *bad;
	struct ibv_sge sge;
	int ret;

	sge.addr = (uint64_t) (uintptr_t) send_msg;
	sge.length = (uint32_t) sizeof send_msg;
	sge.lkey = 0;
	wr.wr_id = (uintptr_t) NULL;
	wr.next = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_INLINE;
	if (hints.ai_qp_type == IBV_QPT_XRC_SEND)
		wr.qp_type.xrc.remote_srqn = srqn;

	ret = ibv_post_send(id->qp, &wr, &bad);
	if (ret)
		perror("rdma_post_send");

	return ret;
}

static int test(void)
{
	struct rdma_addrinfo *res;
	struct ibv_qp_init_attr attr;
	struct ibv_wc wc;
	int ret;

	ret = rdma_getaddrinfo(server, port, &hints, &res);
	if (ret) {
		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	memset(&attr, 0, sizeof attr);
	attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
	attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
	attr.sq_sig_all = 1;
	ret = rdma_create_ep(&id, res, NULL, &attr);
	rdma_freeaddrinfo(res);
	if (ret) {
		perror("rdma_create_ep");
		return ret;
	}

	mr = rdma_reg_msgs(id, send_msg, sizeof send_msg);
	if (!mr) {
		perror("rdma_reg_msgs");
		return ret;
	}

	ret = rdma_connect(id, NULL);
	if (ret) {
		perror("rdma_connect");
		return ret;
	}

	if (hints.ai_qp_type == IBV_QPT_XRC_SEND)
		srqn = be32toh(*(__be32 *) id->event->param.conn.private_data);

	ret = post_send();
	if (ret) {
		perror("post_send");
		return ret;
	}

	ret = rdma_get_send_comp(id, &wc);
	if (ret <= 0) {
		perror("rdma_get_recv_comp");
		return ret;
	}

	rdma_disconnect(id);
	rdma_dereg_mr(mr);
	rdma_destroy_ep(id);
	return 0;
}

int main(int argc, char **argv)
{
	int op, ret;

	hints.ai_port_space = RDMA_PS_TCP;
	hints.ai_qp_type = IBV_QPT_RC;

	while ((op = getopt(argc, argv, "s:p:c:")) != -1) {
		switch (op) {
		case 's':
			server = optarg;
			break;
		case 'p':
			strncpy(port, optarg, sizeof port - 1);
			break;
		case 'c':
			switch (tolower(optarg[0])) {
			case 'r':
				break;
			case 'x':
				hints.ai_port_space = RDMA_PS_IB;
				hints.ai_qp_type = IBV_QPT_XRC_SEND;
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
	printf("\t[-s server]\n");
	printf("\t[-p port_number]\n");
	printf("\t[-c communication type]\n");
	printf("\t    r - RC: reliable-connected (default)\n");
	printf("\t    x - XRC: extended-reliable-connected\n");
	exit(1);
}
