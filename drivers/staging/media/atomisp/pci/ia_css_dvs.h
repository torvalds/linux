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

#ifndef __IA_CSS_DVS_H
#define __IA_CSS_DVS_H

/* @file
 * This file contains types for DVS statistics
 */

#include <linux/build_bug.h>

#include <type_support.h>
#include "ia_css_types.h"
#include "ia_css_err.h"
#include "ia_css_stream_public.h"

enum dvs_statistics_type {
	DVS_STATISTICS,
	DVS2_STATISTICS,
	SKC_DVS_STATISTICS
};

/* Structure that holds DVS statistics in the ISP internal
 * format. Use ia_css_get_dvs_statistics() to translate
 * this to the format used on the host (DVS engine).
 * */
struct ia_css_isp_dvs_statistics {
	ia_css_ptr hor_proj;
	ia_css_ptr ver_proj;
	u32   hor_size;
	u32   ver_size;
	u32   exp_id;   /** see ia_css_event_public.h for more detail */
	ia_css_ptr data_ptr; /* base pointer containing all memory */
	u32   size;     /* size of allocated memory in data_ptr */
};

/* Structure that holds SKC DVS statistics in the ISP internal
 * format. Use ia_css_dvs_statistics_get() to translate this to
 * the format used on the host.
 * */
struct ia_css_isp_skc_dvs_statistics;

#define SIZE_OF_IA_CSS_ISP_DVS_STATISTICS_STRUCT			\
	((3 * SIZE_OF_IA_CSS_PTR) +					\
	 (4 * sizeof(uint32_t)))

static_assert(sizeof(struct ia_css_isp_dvs_statistics) == SIZE_OF_IA_CSS_ISP_DVS_STATISTICS_STRUCT);

/* Map with host-side pointers to ISP-format statistics.
 * These pointers can either be copies of ISP data or memory mapped
 * ISP pointers.
 * All of the data behind these pointers is allocatd contiguously, the
 * allocated pointer is stored in the data_ptr field. The other fields
 * point into this one block of data.
 */
struct ia_css_isp_dvs_statistics_map {
	void    *data_ptr;
	s32 *hor_proj;
	s32 *ver_proj;
	u32 size;		 /* total size in bytes */
	u32 data_allocated; /* indicate whether data was allocated */
};

union ia_css_dvs_statistics_isp {
	struct ia_css_isp_dvs_statistics *p_dvs_statistics_isp;
	struct ia_css_isp_skc_dvs_statistics *p_skc_dvs_statistics_isp;
};

union ia_css_dvs_statistics_host {
	struct ia_css_dvs_statistics *p_dvs_statistics_host;
	struct ia_css_dvs2_statistics *p_dvs2_statistics_host;
	struct ia_css_skc_dvs_statistics *p_skc_dvs_statistics_host;
};

/* @brief Copy DVS statistics from an ISP buffer to a host buffer.
 * @param[in]	host_stats Host buffer
 * @param[in]	isp_stats ISP buffer
 * @return	error value if temporary memory cannot be allocated
 *
 * This may include a translation step as well depending
 * on the ISP version.
 * Always use this function, never copy the buffer directly.
 * Note that this function uses the mem_load function from the CSS
 * environment struct.
 * In certain environments this may be slow. In those cases it is
 * advised to map the ISP memory into a host-side pointer and use
 * the ia_css_translate_dvs_statistics() function instead.
 */
int
ia_css_get_dvs_statistics(struct ia_css_dvs_statistics *host_stats,
			  const struct ia_css_isp_dvs_statistics *isp_stats);

/* @brief Translate DVS statistics from ISP format to host format
 * @param[in]	host_stats Host buffer
 * @param[in]	isp_stats ISP buffer
 * @return	None
 *
 * This function translates the dvs statistics from the ISP-internal
 * format to the format used by the DVS library on the CPU.
 * This function takes a host-side pointer as input. This can either
 * point to a copy of the data or be a memory mapped pointer to the
 * ISP memory pages.
 */
void
ia_css_translate_dvs_statistics(
    struct ia_css_dvs_statistics *host_stats,
    const struct ia_css_isp_dvs_statistics_map *isp_stats);

/* @brief Copy DVS 2.0 statistics from an ISP buffer to a host buffer.
 * @param[in]	host_stats Host buffer
 * @param[in]	isp_stats ISP buffer
 * @return	error value if temporary memory cannot be allocated
 *
 * This may include a translation step as well depending
 * on the ISP version.
 * Always use this function, never copy the buffer directly.
 * Note that this function uses the mem_load function from the CSS
 * environment struct.
 * In certain environments this may be slow. In those cases it is
 * advised to map the ISP memory into a host-side pointer and use
 * the ia_css_translate_dvs2_statistics() function instead.
 */
int
ia_css_get_dvs2_statistics(struct ia_css_dvs2_statistics *host_stats,
			   const struct ia_css_isp_dvs_statistics *isp_stats);

/* @brief Translate DVS2 statistics from ISP format to host format
 * @param[in]	host_stats Host buffer
 * @param[in]	isp_stats ISP buffer
 * @return		None
 *
 * This function translates the dvs2 statistics from the ISP-internal
 * format to the format used by the DVS2 library on the CPU.
 * This function takes a host-side pointer as input. This can either
 * point to a copy of the data or be a memory mapped pointer to the
 * ISP memory pages.
 */
void
ia_css_translate_dvs2_statistics(
    struct ia_css_dvs2_statistics	   *host_stats,
    const struct ia_css_isp_dvs_statistics_map *isp_stats);

/* @brief Copy DVS statistics from an ISP buffer to a host buffer.
 * @param[in] type - DVS statistics type
 * @param[in] host_stats Host buffer
 * @param[in] isp_stats ISP buffer
 * @return None
 */
void
ia_css_dvs_statistics_get(enum dvs_statistics_type type,
			  union ia_css_dvs_statistics_host  *host_stats,
			  const union ia_css_dvs_statistics_isp *isp_stats);

/* @brief Allocate the DVS statistics memory on the ISP
 * @param[in]	grid The grid.
 * @return	Pointer to the allocated DVS statistics buffer on the ISP
*/
struct ia_css_isp_dvs_statistics *
ia_css_isp_dvs_statistics_allocate(const struct ia_css_dvs_grid_info *grid);

/* @brief Free the DVS statistics memory on the ISP
 * @param[in]	me Pointer to the DVS statistics buffer on the ISP.
 * @return	None
*/
void
ia_css_isp_dvs_statistics_free(struct ia_css_isp_dvs_statistics *me);

/* @brief Allocate the DVS 2.0 statistics memory
 * @param[in]	grid The grid.
 * @return	Pointer to the allocated DVS statistics buffer on the ISP
*/
struct ia_css_isp_dvs_statistics *
ia_css_isp_dvs2_statistics_allocate(const struct ia_css_dvs_grid_info *grid);

/* @brief Free the DVS 2.0 statistics memory
 * @param[in]	me Pointer to the DVS statistics buffer on the ISP.
 * @return	None
*/
void
ia_css_isp_dvs2_statistics_free(struct ia_css_isp_dvs_statistics *me);

/* @brief Allocate the DVS statistics memory on the host
 * @param[in]	grid The grid.
 * @return	Pointer to the allocated DVS statistics buffer on the host
*/
struct ia_css_dvs_statistics *
ia_css_dvs_statistics_allocate(const struct ia_css_dvs_grid_info *grid);

/* @brief Free the DVS statistics memory on the host
 * @param[in]	me Pointer to the DVS statistics buffer on the host.
 * @return	None
*/
void
ia_css_dvs_statistics_free(struct ia_css_dvs_statistics *me);

/* @brief Allocate the DVS coefficients memory
 * @param[in]	grid The grid.
 * @return	Pointer to the allocated DVS coefficients buffer
*/
struct ia_css_dvs_coefficients *
ia_css_dvs_coefficients_allocate(const struct ia_css_dvs_grid_info *grid);

/* @brief Free the DVS coefficients memory
 * @param[in]	me Pointer to the DVS coefficients buffer.
 * @return	None
 */
void
ia_css_dvs_coefficients_free(struct ia_css_dvs_coefficients *me);

/* @brief Allocate the DVS 2.0 statistics memory on the host
 * @param[in]	grid The grid.
 * @return	Pointer to the allocated DVS 2.0 statistics buffer on the host
 */
struct ia_css_dvs2_statistics *
ia_css_dvs2_statistics_allocate(const struct ia_css_dvs_grid_info *grid);

/* @brief Free the DVS 2.0 statistics memory
 * @param[in]	me Pointer to the DVS 2.0 statistics buffer on the host.
 * @return	None
*/
void
ia_css_dvs2_statistics_free(struct ia_css_dvs2_statistics *me);

/* @brief Allocate the DVS 2.0 coefficients memory
 * @param[in]	grid The grid.
 * @return	Pointer to the allocated DVS 2.0 coefficients buffer
*/
struct ia_css_dvs2_coefficients *
ia_css_dvs2_coefficients_allocate(const struct ia_css_dvs_grid_info *grid);

/* @brief Free the DVS 2.0 coefficients memory
 * @param[in]	me Pointer to the DVS 2.0 coefficients buffer.
 * @return	None
*/
void
ia_css_dvs2_coefficients_free(struct ia_css_dvs2_coefficients *me);

/* @brief Allocate the DVS 2.0 6-axis config memory
 * @param[in]	stream The stream.
 * @return	Pointer to the allocated DVS 6axis configuration buffer
*/
struct ia_css_dvs_6axis_config *
ia_css_dvs2_6axis_config_allocate(const struct ia_css_stream *stream);

/* @brief Free the DVS 2.0 6-axis config memory
 * @param[in]	dvs_6axis_config Pointer to the DVS 6axis configuration buffer
 * @return	None
 */
void
ia_css_dvs2_6axis_config_free(struct ia_css_dvs_6axis_config *dvs_6axis_config);

/* @brief Allocate a dvs statistics map structure
 * @param[in]	isp_stats pointer to ISP dvs statistis struct
 * @param[in]	data_ptr  host-side pointer to ISP dvs statistics.
 * @return	Pointer to the allocated dvs statistics map
 *
 * This function allocates the ISP dvs statistics map structure
 * and uses the data_ptr as base pointer to set the appropriate
 * pointers to all relevant subsets of the dvs statistics (dmem,
 * vmem, hmem).
 * If the data_ptr is NULL, this function will allocate the host-side
 * memory. This information is stored in the struct and used in the
 * ia_css_isp_dvs_statistics_map_free() function to determine whether
 * the memory should be freed or not.
 * Note that this function does not allocate or map any ISP
 * memory.
*/
struct ia_css_isp_dvs_statistics_map *
ia_css_isp_dvs_statistics_map_allocate(
    const struct ia_css_isp_dvs_statistics *isp_stats,
    void *data_ptr);

/* @brief Free the dvs statistics map
 * @param[in]	me Pointer to the dvs statistics map
 * @return	None
 *
 * This function frees the map struct. If the data_ptr inside it
 * was allocated inside ia_css_isp_dvs_statistics_map_allocate(), it
 * will be freed in this function. Otherwise it will not be freed.
 */
void
ia_css_isp_dvs_statistics_map_free(struct ia_css_isp_dvs_statistics_map *me);

/* @brief Allocate memory for the SKC DVS statistics on the ISP
 * @return		Pointer to the allocated ACC DVS statistics buffer on the ISP
*/
struct ia_css_isp_skc_dvs_statistics *ia_css_skc_dvs_statistics_allocate(void);

#endif /*  __IA_CSS_DVS_H */
