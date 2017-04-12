/*
 * CAAM/SEC 4.x functions for using scatterlists in caam driver
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 */

#include "regs.h"

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
	sec4_sg_ptr->ptr = cpu_to_caam_dma64(dma);
	sec4_sg_ptr->len = cpu_to_caam32(len);
	sec4_sg_ptr->bpid_offset = cpu_to_caam32(offset & SEC4_SG_OFFSET_MASK);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "sec4_sg_ptr@: ",
		       DUMP_PREFIX_ADDRESS, 16, 4, sec4_sg_ptr,
		       sizeof(struct sec4_sg_entry), 1);
#endif
}

/*
 * convert scatterlist to h/w link table format
 * but does not have final bit; instead, returns last entry
 */
static inline struct sec4_sg_entry *
sg_to_sec4_sg(struct scatterlist *sg, int sg_count,
	      struct sec4_sg_entry *sec4_sg_ptr, u16 offset)
{
	while (sg_count) {
		dma_to_sec4_sg_one(sec4_sg_ptr, sg_dma_address(sg),
				   sg_dma_len(sg), offset);
		sec4_sg_ptr++;
		sg = sg_next(sg);
		sg_count--;
	}
	return sec4_sg_ptr - 1;
}

/*
 * convert scatterlist to h/w link table format
 * scatterlist must have been previously dma mapped
 */
static inline void sg_to_sec4_sg_last(struct scatterlist *sg, int sg_count,
				      struct sec4_sg_entry *sec4_sg_ptr,
				      u16 offset)
{
	sec4_sg_ptr = sg_to_sec4_sg(sg, sg_count, sec4_sg_ptr, offset);
	sec4_sg_ptr->len |= cpu_to_caam32(SEC4_SG_LEN_FIN);
}

static inline struct sec4_sg_entry *sg_to_sec4_sg_len(
	struct scatterlist *sg, unsigned int total,
	struct sec4_sg_entry *sec4_sg_ptr)
{
	do {
		unsigned int len = min(sg_dma_len(sg), total);

		dma_to_sec4_sg_one(sec4_sg_ptr, sg_dma_address(sg), len, 0);
		sec4_sg_ptr++;
		sg = sg_next(sg);
		total -= len;
	} while (total);
	return sec4_sg_ptr - 1;
}
