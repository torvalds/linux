/* SPDX-License-Identifier: GPL-2.0 */
/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

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
i40e_status i40e_allocate_dma_mem(struct i40e_hw *hw,
					    struct i40e_dma_mem *mem,
					    enum i40e_memory_type type,
					    u64 size, u32 alignment);
i40e_status i40e_free_dma_mem(struct i40e_hw *hw,
					struct i40e_dma_mem *mem);
i40e_status i40e_allocate_virt_mem(struct i40e_hw *hw,
					     struct i40e_virt_mem *mem,
					     u32 size);
i40e_status i40e_free_virt_mem(struct i40e_hw *hw,
					 struct i40e_virt_mem *mem);

#endif /* _I40E_ALLOC_H_ */
