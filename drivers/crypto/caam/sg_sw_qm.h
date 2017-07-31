/*
 * Copyright 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SG_SW_QM_H
#define __SG_SW_QM_H

#include <soc/fsl/qman.h>
#include "regs.h"

static inline void __dma_to_qm_sg(struct qm_sg_entry *qm_sg_ptr, dma_addr_t dma,
				  u16 offset)
{
	qm_sg_entry_set64(qm_sg_ptr, dma);
	qm_sg_ptr->__reserved2 = 0;
	qm_sg_ptr->bpid = 0;
	qm_sg_ptr->offset = cpu_to_be16(offset & QM_SG_OFF_MASK);
}

static inline void dma_to_qm_sg_one(struct qm_sg_entry *qm_sg_ptr,
				    dma_addr_t dma, u32 len, u16 offset)
{
	__dma_to_qm_sg(qm_sg_ptr, dma, offset);
	qm_sg_entry_set_len(qm_sg_ptr, len);
}

static inline void dma_to_qm_sg_one_last(struct qm_sg_entry *qm_sg_ptr,
					 dma_addr_t dma, u32 len, u16 offset)
{
	__dma_to_qm_sg(qm_sg_ptr, dma, offset);
	qm_sg_entry_set_f(qm_sg_ptr, len);
}

static inline void dma_to_qm_sg_one_ext(struct qm_sg_entry *qm_sg_ptr,
					dma_addr_t dma, u32 len, u16 offset)
{
	__dma_to_qm_sg(qm_sg_ptr, dma, offset);
	qm_sg_ptr->cfg = cpu_to_be32(QM_SG_EXT | (len & QM_SG_LEN_MASK));
}

static inline void dma_to_qm_sg_one_last_ext(struct qm_sg_entry *qm_sg_ptr,
					     dma_addr_t dma, u32 len,
					     u16 offset)
{
	__dma_to_qm_sg(qm_sg_ptr, dma, offset);
	qm_sg_ptr->cfg = cpu_to_be32(QM_SG_EXT | QM_SG_FIN |
				     (len & QM_SG_LEN_MASK));
}

/*
 * convert scatterlist to h/w link table format
 * but does not have final bit; instead, returns last entry
 */
static inline struct qm_sg_entry *
sg_to_qm_sg(struct scatterlist *sg, int sg_count,
	    struct qm_sg_entry *qm_sg_ptr, u16 offset)
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
				    struct qm_sg_entry *qm_sg_ptr, u16 offset)
{
	qm_sg_ptr = sg_to_qm_sg(sg, sg_count, qm_sg_ptr, offset);
	qm_sg_entry_set_f(qm_sg_ptr, qm_sg_entry_get_len(qm_sg_ptr));
}

#endif /* __SG_SW_QM_H */
