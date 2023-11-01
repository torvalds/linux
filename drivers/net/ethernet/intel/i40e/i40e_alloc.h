/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _I40E_ALLOC_H_
#define _I40E_ALLOC_H_

#include <linux/types.h>

struct i40e_hw;

/* memory allocation tracking */
struct i40e_dma_mem {
	void *va;
	dma_addr_t pa;
	u32 size;
};

struct i40e_virt_mem {
	void *va;
	u32 size;
};

/* prototype for functions used for dynamic memory allocation */
int i40e_allocate_dma_mem(struct i40e_hw *hw,
			  struct i40e_dma_mem *mem,
			  u64 size, u32 alignment);
int i40e_free_dma_mem(struct i40e_hw *hw,
		      struct i40e_dma_mem *mem);
int i40e_allocate_virt_mem(struct i40e_hw *hw,
			   struct i40e_virt_mem *mem,
			   u32 size);
int i40e_free_virt_mem(struct i40e_hw *hw,
		       struct i40e_virt_mem *mem);

#endif /* _I40E_ALLOC_H_ */
