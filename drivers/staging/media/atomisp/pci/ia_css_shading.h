/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_SHADING_H
#define __IA_CSS_SHADING_H

/* @file
 * This file contains support for setting the shading table for CSS
 */

#include <ia_css_types.h>

/* @brief Shading table
 * @param[in]	width Width of the shading table.
 * @param[in]	height Height of the shading table.
 * @return		Pointer to the shading table
*/
struct ia_css_shading_table *
ia_css_shading_table_alloc(unsigned int width,
			   unsigned int height);

/* @brief Free shading table
 * @param[in]	table Pointer to the shading table.
 * @return		None
*/
void
ia_css_shading_table_free(struct ia_css_shading_table *table);

#endif /* __IA_CSS_SHADING_H */
