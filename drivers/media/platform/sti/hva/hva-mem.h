/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
 */

#ifndef HVA_MEM_H
#define HVA_MEM_H

/**
 * struct hva_buffer - hva buffer
 *
 * @name:  name of requester
 * @paddr: physical address (for hardware)
 * @vaddr: virtual address (kernel can read/write)
 * @size:  size of buffer
 */
struct hva_buffer {
	const char		*name;
	dma_addr_t		paddr;
	void			*vaddr;
	u32			size;
};

int hva_mem_alloc(struct hva_ctx *ctx,
		  __u32 size,
		  const char *name,
		  struct hva_buffer **buf);

void hva_mem_free(struct hva_ctx *ctx,
		  struct hva_buffer *buf);

#endif /* HVA_MEM_H */
