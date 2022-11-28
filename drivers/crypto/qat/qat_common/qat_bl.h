/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2014 - 2022 Intel Corporation */
#ifndef QAT_BL_H
#define QAT_BL_H
#include <linux/scatterlist.h>
#include <linux/types.h>

#define QAT_MAX_BUFF_DESC	4

struct qat_alg_buf {
	u32 len;
	u32 resrvd;
	u64 addr;
} __packed;

struct qat_alg_buf_list {
	u64 resrvd;
	u32 num_bufs;
	u32 num_mapped_bufs;
	struct qat_alg_buf bufers[];
} __packed;

struct qat_alg_fixed_buf_list {
	struct qat_alg_buf_list sgl_hdr;
	struct qat_alg_buf descriptors[QAT_MAX_BUFF_DESC];
} __packed __aligned(64);

struct qat_request_buffs {
	struct qat_alg_buf_list *bl;
	dma_addr_t blp;
	struct qat_alg_buf_list *blout;
	dma_addr_t bloutp;
	size_t sz;
	size_t sz_out;
	bool sgl_src_valid;
	bool sgl_dst_valid;
	struct qat_alg_fixed_buf_list sgl_src;
	struct qat_alg_fixed_buf_list sgl_dst;
};

void qat_bl_free_bufl(struct adf_accel_dev *accel_dev,
		      struct qat_request_buffs *buf);
int qat_bl_sgl_to_bufl(struct adf_accel_dev *accel_dev,
		       struct scatterlist *sgl,
		       struct scatterlist *sglout,
		       struct qat_request_buffs *buf,
		       gfp_t flags);

#endif
