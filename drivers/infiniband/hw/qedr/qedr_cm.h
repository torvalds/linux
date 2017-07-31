/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#ifndef LINUX_QEDR_CM_H_
#define LINUX_QEDR_CM_H_

#define QEDR_GSI_MAX_RECV_WR	(4096)
#define QEDR_GSI_MAX_SEND_WR	(4096)

#define QEDR_GSI_MAX_RECV_SGE	(1)	/* LL2 FW limitation */

#define QEDR_ROCE_V2_UDP_SPORT	(0000)

static inline u32 qedr_get_ipv4_from_gid(u8 *gid)
{
	return *(u32 *)(void *)&gid[12];
}

/* RDMA CM */
int qedr_gsi_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
int qedr_gsi_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		       struct ib_recv_wr **bad_wr);
int qedr_gsi_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		       struct ib_send_wr **bad_wr);
struct ib_qp *qedr_create_gsi_qp(struct qedr_dev *dev,
				 struct ib_qp_init_attr *attrs,
				 struct qedr_qp *qp);
void qedr_store_gsi_qp_cq(struct qedr_dev *dev,
			  struct qedr_qp *qp, struct ib_qp_init_attr *attrs);
int qedr_destroy_gsi_qp(struct qedr_dev *dev);
void qedr_inc_sw_gsi_cons(struct qedr_qp_hwq_info *info);
#endif
