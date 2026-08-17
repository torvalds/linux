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

/**
 * mmu_set_page_table_base_index() - Set the page table base index of MMU[ID]
 * @ID:				MMU identifier
 * @base_index:			page table base index
 *
 * Return: none, MMU[ID].page_table_base_index = base_index
 */
void mmu_set_page_table_base_index(const mmu_ID_t ID, const hrt_data base_index);

/**
 * mmu_get_page_table_base_index() - Get the page table base index of MMU[ID]
 * @ID:				MMU identifier
 *
 * Return: MMU[ID].page_table_base_index
 */
hrt_data mmu_get_page_table_base_index(const mmu_ID_t ID);

/**
 * mmu_invalidate_cache() - nvalidate the page table cache of MMU[ID]
 * @ID:				MMU identifier
 *
 * Return: none
 */
void mmu_invalidate_cache(const mmu_ID_t ID);

/**
 * mmu_invalidate_cache_all() - Invalidate the page table cache of all MMUs
 *
 * Return: none
 */
void mmu_invalidate_cache_all(void);

/**
 * mmu_reg_store() - Write to a control register of MMU[ID]
 * @ID:				MMU identifier
 * @reg:			register index
 * @value:			The data to be written
 *
 * Return: none, MMU[ID].ctrl[reg] = value
 */
static inline void mmu_reg_store(const mmu_ID_t ID, const unsigned int reg, const hrt_data value)
{
	assert(ID < N_MMU_ID);
	assert(MMU_BASE[ID] != (hrt_address) - 1);
	ia_css_device_store_uint32(MMU_BASE[ID] + reg * sizeof(hrt_data), value);
}

/**
 * mmu_reg_load() - Read from a control register of MMU[ID]
 * @ID:				MMU identifier
 * @reg:			register index
 *
 * Return: MMU[ID].ctrl[reg]
 */
static inline hrt_data mmu_reg_load(const mmu_ID_t ID, const unsigned int reg)
{
	assert(ID < N_MMU_ID);
	assert(MMU_BASE[ID] != (hrt_address) - 1);
	return ia_css_device_load_uint32(MMU_BASE[ID] + reg * sizeof(hrt_data));
}

#endif /* __MMU_PUBLIC_H_INCLUDED__ */
