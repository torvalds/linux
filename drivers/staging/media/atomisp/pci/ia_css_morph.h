/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_MORPH_H
#define __IA_CSS_MORPH_H

/* @file
 * This file contains supporting for morphing table
 */

#include <ia_css_types.h>

/* @brief Morphing table
 * @param[in]	width Width of the morphing table.
 * @param[in]	height Height of the morphing table.
 * @return		Pointer to the morphing table
*/
struct ia_css_morph_table *
ia_css_morph_table_allocate(unsigned int width, unsigned int height);

/* @brief Free the morph table
 * @param[in]	me Pointer to the morph table.
 * @return		None
*/
void
ia_css_morph_table_free(struct ia_css_morph_table *me);

#endif /* __IA_CSS_MORPH_H */
