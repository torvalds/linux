/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __MMU_PUBLIC_H_INCLUDED__
#define __MMU_PUBLIC_H_INCLUDED__

#include "system_local.h"
#include "device_access.h"
#include "assert_support.h"

/*! Set the page table base index of MMU[ID]

 \param	ID[in]				MMU identifier
 \param	base_index[in]		page table base index

 \return none, MMU[ID].page_table_base_index = base_index
 */
void mmu_set_page_table_base_index(
    const mmu_ID_t		ID,
    const hrt_data		base_index);

/*! Get the page table base index of MMU[ID]

 \param	ID[in]				MMU identifier
 \param	base_index[in]		page table base index

 \return MMU[ID].page_table_base_index
 */
hrt_data mmu_get_page_table_base_index(
    const mmu_ID_t		ID);

/*! Invalidate the page table cache of MMU[ID]

 \param	ID[in]				MMU identifier

 \return none
 */
void mmu_invalidate_cache(
    const mmu_ID_t		ID);

/*! Invalidate the page table cache of all MMUs

 \return none
 */
void mmu_invalidate_cache_all(void);

/*! Write to a control register of MMU[ID]

 \param	ID[in]				MMU identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return none, MMU[ID].ctrl[reg] = value
 */
static inline void mmu_reg_store(
    const mmu_ID_t		ID,
    const unsigned int	reg,
    const hrt_data		value)
{
	assert(ID < N_MMU_ID);
	assert(MMU_BASE[ID] != (hrt_address) - 1);
	ia_css_device_store_uint32(MMU_BASE[ID] + reg * sizeof(hrt_data), value);
	return;
}

/*! Read from a control register of MMU[ID]

 \param	ID[in]				MMU identifier
 \param	reg[in]				register index
 \param value[in]			The data to be written

 \return MMU[ID].ctrl[reg]
 */
static inline hrt_data mmu_reg_load(
    const mmu_ID_t		ID,
    const unsigned int	reg)
{
	assert(ID < N_MMU_ID);
	assert(MMU_BASE[ID] != (hrt_address) - 1);
	return ia_css_device_load_uint32(MMU_BASE[ID] + reg * sizeof(hrt_data));
}

#endif /* __MMU_PUBLIC_H_INCLUDED__ */
