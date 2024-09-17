/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAAM/SEC 4.x functions for using scatterlists in caam driver
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 */

#ifndef _SG_SW_SEC4_H_
#define _SG_SW_SEC4_H_

#include "ctrl.h"
#include "regs.h"
#include "sg_sw_qm2.h"
#include <soc/fsl/dpaa2-fd.h>

struct sec4_sg_entry {
	u64 ptr;
	u32 len;
	u32 bpid_offset;
};

/*
 * convert single dma address to h/w link table format
 */
static inline void dma_to_sec4_sg_one(struct sec4_sg_entry *sec4_sg_ptr,
				      dma_addr_t dma, u32 len, u16 offset)
{
	if (caam_dpaa2) {
		dma_to_qm_sg_one((struct dpaa2_sg_entry *)sec4_sg_ptr, dma, len,
				 offset);
	} else {
		sec4_sg_ptr->ptr = cpu_to_caam_dma64(dma);
		sec4_sg_ptr->len = cpu_to_caam32(len);
		sec4_sg_ptr->bpid_offset = cpu_to_caam32(offset &
							 SEC4_SG_OFFSET_MASK);
	}

	print_hex_dump_debug("sec4_sg_ptr@: ", DUMP_PREFIX_ADDRESS, 16, 4,
			     sec4_sg_ptr, sizeof(struct sec4_sg_entry), 1);
}

/*
 * convert scatterlist to h/w link table format
 * but does not have final bit; instead, returns last entry
 */
static inline struct sec4_sg_entry *
sg_to_sec4_sg(struct scatterlist *sg, int len,
	      struct sec4_sg_entry *sec4_sg_ptr, u16 offset)
{
	int ent_len;

	while (len) {
		ent_len = min_t(int, sg_dma_len(sg), len);

		dma_to_sec4_sg_one(sec4_sg_ptr, sg_dma_address(sg), ent_len,
				   offset);
		sec4_sg_ptr++;
		sg = sg_next(sg);
		len -= ent_len;
	}
	return sec4_sg_ptr - 1;
}

static inline void sg_to_sec4_set_last(struct sec4_sg_entry *sec4_sg_ptr)
{
	if (caam_dpaa2)
		dpaa2_sg_set_final((struct dpaa2_sg_entry *)sec4_sg_ptr, true);
	else
		sec4_sg_ptr->len |= cpu_to_caam32(SEC4_SG_LEN_FIN);
}

/*
 * convert scatterlist to h/w link table format
 * scatterlist must have been previously dma mapped
 */
static inline void sg_to_sec4_sg_last(struct scatterlist *sg, int len,
				      struct sec4_sg_entry *sec4_sg_ptr,
				      u16 offset)
{
	sec4_sg_ptr = sg_to_sec4_sg(sg, len, sec4_sg_ptr, offset);
	sg_to_sec4_set_last(sec4_sg_ptr);
}

#endif /* _SG_SW_SEC4_H_ */
