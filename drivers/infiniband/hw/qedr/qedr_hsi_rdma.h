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
#ifndef __QED_HSI_RDMA__
#define __QED_HSI_RDMA__

#include <linux/qed/rdma_common.h>

/* rdma completion notification queue element */
struct rdma_cnqe {
	struct regpair	cq_handle;
};

struct rdma_cqe_responder {
	struct regpair srq_wr_id;
	struct regpair qp_handle;
	__le32 imm_data_or_inv_r_Key;
	__le32 length;
	__le32 imm_data_hi;
	__le16 rq_cons;
	u8 flags;
};

struct rdma_cqe_requester {
	__le16 sq_cons;
	__le16 reserved0;
	__le32 reserved1;
	struct regpair qp_handle;
	struct regpair reserved2;
	__le32 reserved3;
	__le16 reserved4;
	u8 flags;
	u8 status;
};

struct rdma_cqe_common {
	struct regpair reserved0;
	struct regpair qp_handle;
	__le16 reserved1[7];
	u8 flags;
	u8 status;
};

/* rdma completion queue element */
union rdma_cqe {
	struct rdma_cqe_responder resp;
	struct rdma_cqe_requester req;
	struct rdma_cqe_common cmn;
};

struct rdma_sq_sge {
	__le32 length;
	struct regpair	addr;
	__le32 l_key;
};

struct rdma_rq_sge {
	struct regpair addr;
	__le32 length;
	__le32 flags;
};

struct rdma_srq_sge {
	struct regpair addr;
	__le32 length;
	__le32 l_key;
};
#endif /* __QED_HSI_RDMA__ */
