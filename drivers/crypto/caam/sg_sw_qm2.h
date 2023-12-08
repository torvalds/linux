/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2015-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 */

#ifndef _SG_SW_QM2_H_
#define _SG_SW_QM2_H_

#include <soc/fsl/dpaa2-fd.h>

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
sg_to_qm_sg(struct scatterlist *sg, int len,
	    struct dpaa2_sg_entry *qm_sg_ptr, u16 offset)
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
				    struct dpaa2_sg_entry *qm_sg_ptr,
				    u16 offset)
{
	qm_sg_ptr = sg_to_qm_sg(sg, len, qm_sg_ptr, offset);
	dpaa2_sg_set_final(qm_sg_ptr, true);
}

#endif /* _SG_SW_QM2_H_ */
