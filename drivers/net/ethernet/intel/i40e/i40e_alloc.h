/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _I40E_ALLOC_H_
#define _I40E_ALLOC_H_

struct i40e_hw;

/* Memory allocation types */
enum i40e_memory_type {
	i40e_mem_arq_buf = 0,		/* ARQ indirect command buffer */
	i40e_mem_asq_buf = 1,
	i40e_mem_atq_buf = 2,		/* ATQ indirect command buffer */
	i40e_mem_arq_ring = 3,		/* ARQ descriptor ring */
	i40e_mem_atq_ring = 4,		/* ATQ descriptor ring */
	i40e_mem_pd = 5,		/* Page Descriptor */
	i40e_mem_bp = 6,		/* Backing Page - 4KB */
	i40e_mem_bp_jumbo = 7,		/* Backing Page - > 4KB */
	i40e_mem_reserved
};

/* prototype for functions used for dynamic memory allocation */
int i40e_allocate_dma_mem(struct i40e_hw *hw, struct i40e_dma_mem *mem,
			  enum i40e_memory_type type, u64 size, u32 alignment);
int i40e_free_dma_mem(struct i40e_hw *hw, struct i40e_dma_mem *mem);
int i40e_allocate_virt_mem(struct i40e_hw *hw, struct i40e_virt_mem *mem,
			   u32 size);
int i40e_free_virt_mem(struct i40e_hw *hw, struct i40e_virt_mem *mem);

#endif /* _I40E_ALLOC_H_ */
