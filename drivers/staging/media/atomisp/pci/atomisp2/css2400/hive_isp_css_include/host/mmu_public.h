/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __MMU_PUBLIC_H_INCLUDED__
#define __MMU_PUBLIC_H_INCLUDED__

#include "system_types.h"

/*! Set the page table base index of MMU[ID]

 \param	ID[in]				MMU identifier
 \param	base_index[in]		page table base index

 \return none, MMU[ID].page_table_base_index = base_index
 */
extern void mmu_set_page_table_base_index(
	const mmu_ID_t		ID,
	const hrt_data		base_index);

/*! Get the page table base index of MMU[ID]

 \param	ID[in]				MMU identifier
 \param	base_index[in]		page table base index

 \return MMU[ID].page_table_base_index
 */
extern hrt_data mmu_get_page_table_base_index(
	const mmu_ID_t		ID);

/*! Invalidate the page table cache of MMU[ID]

 \param	ID[in]				MMU identifier

 \return none
 */
extern void mmu_invalidate_cache(
	const mmu_ID_t		ID);


/*! Invalidate the page table cache of all MMUs

 \return none
 */
extern void mmu_invalidate_cache_all(void);

/*! Write to a control register of MMU[ID]

 \param	ID[in]				MMU identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, MMU[ID].ctrl[reg] = value
 */
STORAGE_CLASS_MMU_H void mmu_reg_store(
	const mmu_ID_t		ID,
	const unsigned int	reg,
	const hrt_data		value);

/*! Read from a control register of MMU[ID]

 \param	ID[in]				MMU identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return MMU[ID].ctrl[reg]
 */
STORAGE_CLASS_MMU_H hrt_data mmu_reg_load(
	const mmu_ID_t		ID,
	const unsigned int	reg);

#endif /* __MMU_PUBLIC_H_INCLUDED__ */
