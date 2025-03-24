#ifndef _QLA_DSD_H_
#define _QLA_DSD_H_

#include <linux/unaligned.h>

/* 32-bit data segment descriptor (8 bytes) */
struct dsd32 {
	__le32 address;
	__le32 length;
};

static inline void append_dsd32(struct dsd32 **dsd, struct scatterlist *sg)
{
	put_unaligned_le32(sg_dma_address(sg), &(*dsd)->address);
	put_unaligned_le32(sg_dma_len(sg),     &(*dsd)->length);
	(*dsd)++;
}

/* 64-bit data segment descriptor (12 bytes) */
struct dsd64 {
	__le64 address;
	__le32 length;
} __packed;

static inline void append_dsd64(struct dsd64 **dsd, struct scatterlist *sg)
{
	put_unaligned_le64(sg_dma_address(sg), &(*dsd)->address);
	put_unaligned_le32(sg_dma_len(sg),     &(*dsd)->length);
	(*dsd)++;
}

#endif
