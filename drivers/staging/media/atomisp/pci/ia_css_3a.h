/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_3A_H
#define __IA_CSS_3A_H

/* @file
 * This file contains types used for 3A statistics
 */

#include <type_support.h>
#include "ia_css_types.h"
#include "ia_css_err.h"
#include "system_global.h"

enum ia_css_3a_tables {
	IA_CSS_S3A_TBL_HI,
	IA_CSS_S3A_TBL_LO,
	IA_CSS_RGBY_TBL,
	IA_CSS_NUM_3A_TABLES
};

/* Structure that holds 3A statistics in the ISP internal
 * format. Use ia_css_get_3a_statistics() to translate
 * this to the format used on the host (3A library).
 * */
struct ia_css_isp_3a_statistics {
	union {
		struct {
			ia_css_ptr s3a_tbl;
		} dmem;
		struct {
			ia_css_ptr s3a_tbl_hi;
			ia_css_ptr s3a_tbl_lo;
		} vmem;
	} data;
	struct {
		ia_css_ptr rgby_tbl;
	} data_hmem;
	u32 exp_id;     /** exposure id, to match statistics to a frame,
				  see ia_css_event_public.h for more detail. */
	u32 isp_config_id;/** Unique ID to track which config was actually applied to a particular frame */
	ia_css_ptr data_ptr; /** pointer to base of all data */
	u32   size;     /** total size of all data */
	u32   dmem_size;
	u32   vmem_size; /** both lo and hi have this size */
	u32   hmem_size;
};

#define SIZE_OF_DMEM_STRUCT						\
	(SIZE_OF_IA_CSS_PTR)

#define SIZE_OF_VMEM_STRUCT						\
	(2 * SIZE_OF_IA_CSS_PTR)

#define SIZE_OF_DATA_UNION						\
	(MAX(SIZE_OF_DMEM_STRUCT, SIZE_OF_VMEM_STRUCT))

#define SIZE_OF_DATA_HMEM_STRUCT					\
	(SIZE_OF_IA_CSS_PTR)

#define SIZE_OF_IA_CSS_ISP_3A_STATISTICS_STRUCT				\
	(SIZE_OF_DATA_UNION +						\
	 SIZE_OF_DATA_HMEM_STRUCT +					\
	 sizeof(uint32_t) +						\
	 sizeof(uint32_t) +						\
	 SIZE_OF_IA_CSS_PTR +						\
	 4 * sizeof(uint32_t))

/* Map with host-side pointers to ISP-format statistics.
 * These pointers can either be copies of ISP data or memory mapped
 * ISP pointers.
 * All of the data behind these pointers is allocated contiguously, the
 * allocated pointer is stored in the data_ptr field. The other fields
 * point into this one block of data.
 */
struct ia_css_isp_3a_statistics_map {
	void                    *data_ptr; /** Pointer to start of memory */
	struct ia_css_3a_output *dmem_stats;
	u16                *vmem_stats_hi;
	u16                *vmem_stats_lo;
	struct ia_css_bh_table  *hmem_stats;
	u32                 size; /** total size in bytes of data_ptr */
	u32                 data_allocated; /** indicate whether data_ptr
						    was allocated or not. */
};

/* @brief Copy and translate 3A statistics from an ISP buffer to a host buffer
 * @param[out]	host_stats Host buffer.
 * @param[in]	isp_stats ISP buffer.
 * @return	error value if temporary memory cannot be allocated
 *
 * This copies 3a statistics from an ISP pointer to a host pointer and then
 * translates some of the statistics, details depend on which ISP binary is
 * used.
 * Always use this function, never copy the buffer directly.
 */
int
ia_css_get_3a_statistics(struct ia_css_3a_statistics           *host_stats,
			 const struct ia_css_isp_3a_statistics *isp_stats);

/* @brief Translate 3A statistics from ISP format to host format.
 * @param[out]	host_stats host-format statistics
 * @param[in]	isp_stats  ISP-format statistics
 * @return	None
 *
 * This function translates statistics from the internal ISP-format to
 * the host-format. This function does not include an additional copy
 * step.
 * */
void
ia_css_translate_3a_statistics(
    struct ia_css_3a_statistics               *host_stats,
    const struct ia_css_isp_3a_statistics_map *isp_stats);

/* Convenience functions for alloc/free of certain datatypes */

/* @brief Allocate memory for the 3a statistics on the ISP
 * @param[in]	grid The grid.
 * @return		Pointer to the allocated 3a statistics buffer on the ISP
*/
struct ia_css_isp_3a_statistics *
ia_css_isp_3a_statistics_allocate(const struct ia_css_3a_grid_info *grid);

/* @brief Free the 3a statistics memory on the isp
 * @param[in]	me Pointer to the 3a statistics buffer on the ISP.
 * @return		None
*/
void
ia_css_isp_3a_statistics_free(struct ia_css_isp_3a_statistics *me);

/* @brief Allocate memory for the 3a statistics on the host
 * @param[in]	grid The grid.
 * @return		Pointer to the allocated 3a statistics buffer on the host
*/
struct ia_css_3a_statistics *
ia_css_3a_statistics_allocate(const struct ia_css_3a_grid_info *grid);

/* @brief Free the 3a statistics memory on the host
 * @param[in]	me Pointer to the 3a statistics buffer on the host.
 * @return		None
 */
void
ia_css_3a_statistics_free(struct ia_css_3a_statistics *me);

/* @brief Allocate a 3a statistics map structure
 * @param[in]	isp_stats pointer to ISP 3a statistis struct
 * @param[in]	data_ptr  host-side pointer to ISP 3a statistics.
 * @return		Pointer to the allocated 3a statistics map
 *
 * This function allocates the ISP 3a statistics map structure
 * and uses the data_ptr as base pointer to set the appropriate
 * pointers to all relevant subsets of the 3a statistics (dmem,
 * vmem, hmem).
 * If the data_ptr is NULL, this function will allocate the host-side
 * memory. This information is stored in the struct and used in the
 * ia_css_isp_3a_statistics_map_free() function to determine whether
 * the memory should be freed or not.
 * Note that this function does not allocate or map any ISP
 * memory.
*/
struct ia_css_isp_3a_statistics_map *
ia_css_isp_3a_statistics_map_allocate(
    const struct ia_css_isp_3a_statistics *isp_stats,
    void *data_ptr);

/* @brief Free the 3a statistics map
 * @param[in]	me Pointer to the 3a statistics map
 * @return		None
 *
 * This function frees the map struct. If the data_ptr inside it
 * was allocated inside ia_css_isp_3a_statistics_map_allocate(), it
 * will be freed in this function. Otherwise it will not be freed.
 */
void
ia_css_isp_3a_statistics_map_free(struct ia_css_isp_3a_statistics_map *me);

#endif /* __IA_CSS_3A_H */
