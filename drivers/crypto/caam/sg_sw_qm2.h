/*
 * Copyright 2015-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the names of the above-listed copyright holders nor the
 *	 names of any contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SG_SW_QM2_H_
#define _SG_SW_QM2_H_

#include "../../../drivers/staging/fsl-mc/include/dpaa2-fd.h"

static inline void dma_to_qm_sg_one(struct dpaa2_sg_entry *qm_sg_ptr,
				    dma_addr_t dma, u32 len, u16 offset)
{
	dpaa2_sg_set_addr(qm_sg_ptr, dma);
	dpaa2_sg_set_format(qm_sg_ptr, dpaa2_sg_single);
	dpaa2_sg_set_final(qm_sg_ptr, false);
	dpaa2_sg_set_len(qm_sg_ptr, len);
	dpaa2_sg_set_bpid(qm_sg_ptr, 0);
	dpaa2_sg_set_offset(qm_sg_ptr, offset);
}

/*
 * convert scatterlist to h/w link table format
 * but does not have final bit; instead, returns last entry
 */
static inline struct dpaa2_sg_entry *
sg_to_qm_sg(struct scatterlist *sg, int sg_count,
	    struct dpaa2_sg_entry *qm_sg_ptr, u16 offset)
{
	while (sg_count && sg) {
		dma_to_qm_sg_one(qm_sg_ptr, sg_dma_address(sg),
				 sg_dma_len(sg), offset);
		qm_sg_ptr++;
		sg = sg_next(sg);
		sg_count--;
	}
	return qm_sg_ptr - 1;
}

/*
 * convert scatterlist to h/w link table format
 * scatterlist must have been previously dma mapped
 */
static inline void sg_to_qm_sg_last(struct scatterlist *sg, int sg_count,
				    struct dpaa2_sg_entry *qm_sg_ptr,
				    u16 offset)
{
	qm_sg_ptr = sg_to_qm_sg(sg, sg_count, qm_sg_ptr, offset);
	dpaa2_sg_set_final(qm_sg_ptr, true);
}

#endif /* _SG_SW_QM2_H_ */
