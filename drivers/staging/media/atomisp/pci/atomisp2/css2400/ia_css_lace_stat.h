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

#ifndef __IA_CSS_LACE_STAT_H
#define __IA_CSS_LACE_STAT_H

/** @file
 * This file contains types used for LACE statistics
 */

struct ia_css_isp_lace_statistics;

/** @brief Allocate mem for the LACE statistics on the ISP
 * @return	Pointer to the allocated LACE statistics
 *         buffer on the ISP
*/
struct ia_css_isp_lace_statistics *ia_css_lace_statistics_allocate(void);

/** @brief Free the ACC LACE statistics memory on the isp
 * @param[in]	me Pointer to the LACE statistics buffer on the
 *       ISP.
 * @return		None
*/
void ia_css_lace_statistics_free(struct ia_css_isp_lace_statistics *me);

#endif /*  __IA_CSS_LACE_STAT_H */
