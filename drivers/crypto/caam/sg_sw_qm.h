/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
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
sg_to_qm_sg(struct scatterlist *sg, int len,
	    struct qm_sg_entry *qm_sg_ptr, u16 offset)
{
	int ent_len;

	while (len) {
		ent_len = min_t(int, sg_dma_len(sg), len);

		dma_to_qm_sg_one(qm_sg_ptr, sg_dma_address(sg), ent_len,
				 offset);
		qm_sg_ptr++;
		sg = sg_next(sg);
		len -= ent_len;
	}
	return qm_sg_ptr - 1;
}

/*
 * convert scatterlist to h/w link table format
 * scatterlist must have been previously dma mapped
 */
static inline void sg_to_qm_sg_last(struct scatterlist *sg, int len,
				    struct qm_sg_entry *qm_sg_ptr, u16 offset)
{
	qm_sg_ptr = sg_to_qm_sg(sg, len, qm_sg_ptr, offset);
	qm_sg_entry_set_f(qm_sg_ptr, qm_sg_entry_get_len(qm_sg_ptr));
}

#endif /* __SG_SW_QM_H */
