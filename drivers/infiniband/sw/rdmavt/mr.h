#ifndef DEF_RVTMR_H
#define DEF_RVTMR_H

/*
 * Copyright(c) 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <rdma/rdma_vt.h>
struct rvt_fmr {
	struct ib_fmr ibfmr;
	struct rvt_mregion mr;        /* must be last */
};

struct rvt_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	struct rvt_mregion mr;  /* must be last */
};

static inline struct rvt_fmr *to_ifmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct rvt_fmr, ibfmr);
}

static inline struct rvt_mr *to_imr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct rvt_mr, ibmr);
}

int rvt_driver_mr_init(struct rvt_dev_info *rdi);
void rvt_mr_exit(struct rvt_dev_info *rdi);

/* Mem Regions */
struct ib_mr *rvt_get_dma_mr(struct ib_pd *pd, int acc);
struct ib_mr *rvt_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			      u64 virt_addr, int mr_access_flags,
			      struct ib_udata *udata);
int rvt_dereg_mr(struct ib_mr *ibmr);
struct ib_mr *rvt_alloc_mr(struct ib_pd *pd,
			   enum ib_mr_type mr_type,
			   u32 max_num_sg);
int rvt_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
		  int sg_nents, unsigned int *sg_offset);
struct ib_fmr *rvt_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
			     struct ib_fmr_attr *fmr_attr);
int rvt_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		     int list_len, u64 iova);
int rvt_unmap_fmr(struct list_head *fmr_list);
int rvt_dealloc_fmr(struct ib_fmr *ibfmr);

#endif          /* DEF_RVTMR_H */
