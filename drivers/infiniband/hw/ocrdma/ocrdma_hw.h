/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#ifndef __OCRDMA_HW_H__
#define __OCRDMA_HW_H__

#include "ocrdma_sli.h"

static inline void ocrdma_cpu_to_le32(void *dst, u32 len)
{
#ifdef __BIG_ENDIAN
	int i = 0;
	u32 *src_ptr = dst;
	u32 *dst_ptr = dst;
	for (; i < (len / 4); i++)
		*(dst_ptr + i) = cpu_to_le32p(src_ptr + i);
#endif
}

static inline void ocrdma_le32_to_cpu(void *dst, u32 len)
{
#ifdef __BIG_ENDIAN
	int i = 0;
	u32 *src_ptr = dst;
	u32 *dst_ptr = dst;
	for (; i < (len / sizeof(u32)); i++)
		*(dst_ptr + i) = le32_to_cpu(*(src_ptr + i));
#endif
}

static inline void ocrdma_copy_cpu_to_le32(void *dst, void *src, u32 len)
{
#ifdef __BIG_ENDIAN
	int i = 0;
	u32 *src_ptr = src;
	u32 *dst_ptr = dst;
	for (; i < (len / sizeof(u32)); i++)
		*(dst_ptr + i) = cpu_to_le32p(src_ptr + i);
#else
	memcpy(dst, src, len);
#endif
}

static inline void ocrdma_copy_le32_to_cpu(void *dst, void *src, u32 len)
{
#ifdef __BIG_ENDIAN
	int i = 0;
	u32 *src_ptr = src;
	u32 *dst_ptr = dst;
	for (; i < len / sizeof(u32); i++)
		*(dst_ptr + i) = le32_to_cpu(*(src_ptr + i));
#else
	memcpy(dst, src, len);
#endif
}

static inline u64 ocrdma_get_db_addr(struct ocrdma_dev *dev, u32 pdid)
{
	return dev->nic_info.unmapped_db + (pdid * dev->nic_info.db_page_size);
}

int ocrdma_init_hw(struct ocrdma_dev *);
void ocrdma_cleanup_hw(struct ocrdma_dev *);

enum ib_qp_state get_ibqp_state(enum ocrdma_qp_state qps);
void ocrdma_ring_cq_db(struct ocrdma_dev *, u16 cq_id, bool armed,
		       bool solicited, u16 cqe_popped);

/* verbs specific mailbox commands */
int ocrdma_mbx_get_link_speed(struct ocrdma_dev *dev, u8 *lnk_speed,
			      u8 *lnk_st);
int ocrdma_query_config(struct ocrdma_dev *,
			struct ocrdma_mbx_query_config *config);

int ocrdma_mbx_alloc_pd(struct ocrdma_dev *, struct ocrdma_pd *);
int ocrdma_mbx_dealloc_pd(struct ocrdma_dev *, struct ocrdma_pd *);

int ocrdma_mbx_alloc_lkey(struct ocrdma_dev *, struct ocrdma_hw_mr *hwmr,
			  u32 pd_id, int addr_check);
int ocrdma_mbx_dealloc_lkey(struct ocrdma_dev *, int fmr, u32 lkey);

int ocrdma_reg_mr(struct ocrdma_dev *, struct ocrdma_hw_mr *hwmr,
			u32 pd_id, int acc);
int ocrdma_mbx_create_cq(struct ocrdma_dev *, struct ocrdma_cq *,
				int entries, int dpp_cq, u16 pd_id);
int ocrdma_mbx_destroy_cq(struct ocrdma_dev *, struct ocrdma_cq *);

int ocrdma_mbx_create_qp(struct ocrdma_qp *, struct ib_qp_init_attr *attrs,
			 u8 enable_dpp_cq, u16 dpp_cq_id, u16 *dpp_offset,
			 u16 *dpp_credit_lmt);
int ocrdma_mbx_modify_qp(struct ocrdma_dev *, struct ocrdma_qp *,
			 struct ib_qp_attr *attrs, int attr_mask);
int ocrdma_mbx_query_qp(struct ocrdma_dev *, struct ocrdma_qp *,
			struct ocrdma_qp_params *param);
int ocrdma_mbx_destroy_qp(struct ocrdma_dev *, struct ocrdma_qp *);
int ocrdma_mbx_create_srq(struct ocrdma_dev *, struct ocrdma_srq *,
			  struct ib_srq_init_attr *,
			  struct ocrdma_pd *);
int ocrdma_mbx_modify_srq(struct ocrdma_srq *, struct ib_srq_attr *);
int ocrdma_mbx_query_srq(struct ocrdma_srq *, struct ib_srq_attr *);
int ocrdma_mbx_destroy_srq(struct ocrdma_dev *, struct ocrdma_srq *);

int ocrdma_alloc_av(struct ocrdma_dev *, struct ocrdma_ah *);
int ocrdma_free_av(struct ocrdma_dev *, struct ocrdma_ah *);

int ocrdma_qp_state_change(struct ocrdma_qp *, enum ib_qp_state new_state,
			    enum ib_qp_state *old_ib_state);
bool ocrdma_is_qp_in_sq_flushlist(struct ocrdma_cq *, struct ocrdma_qp *);
bool ocrdma_is_qp_in_rq_flushlist(struct ocrdma_cq *, struct ocrdma_qp *);
void ocrdma_flush_qp(struct ocrdma_qp *);
int ocrdma_get_irq(struct ocrdma_dev *dev, struct ocrdma_eq *eq);

int ocrdma_mbx_rdma_stats(struct ocrdma_dev *, bool reset);
char *port_speed_string(struct ocrdma_dev *dev);
void ocrdma_init_service_level(struct ocrdma_dev *);
void ocrdma_alloc_pd_pool(struct ocrdma_dev *dev);
void ocrdma_free_pd_range(struct ocrdma_dev *dev);
void ocrdma_update_link_state(struct ocrdma_dev *dev, u8 lstate);

#endif				/* __OCRDMA_HW_H__ */
