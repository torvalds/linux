/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _IAVF_ALLOC_H_
#define _IAVF_ALLOC_H_

struct iavf_hw;

/* Memory allocation types */
enum iavf_memory_type {
	iavf_mem_arq_buf = 0,		/* ARQ indirect command buffer */
	iavf_mem_asq_buf = 1,
	iavf_mem_atq_buf = 2,		/* ATQ indirect command buffer */
	iavf_mem_arq_ring = 3,		/* ARQ descriptor ring */
	iavf_mem_atq_ring = 4,		/* ATQ descriptor ring */
	iavf_mem_pd = 5,		/* Page Descriptor */
	iavf_mem_bp = 6,		/* Backing Page - 4KB */
	iavf_mem_bp_jumbo = 7,		/* Backing Page - > 4KB */
	iavf_mem_reserved
};

/* prototype for functions used for dynamic memory allocation */
enum iavf_status iavf_allocate_dma_mem(struct iavf_hw *hw,
				       struct iavf_dma_mem *mem,
				       enum iavf_memory_type type,
				       u64 size, u32 alignment);
enum iavf_status iavf_free_dma_mem(struct iavf_hw *hw,
				   struct iavf_dma_mem *mem);
enum iavf_status iavf_allocate_virt_mem(struct iavf_hw *hw,
					struct iavf_virt_mem *mem, u32 size);
void iavf_free_virt_mem(struct iavf_hw *hw, struct iavf_virt_mem *mem);

#endif /* _IAVF_ALLOC_H_ */
