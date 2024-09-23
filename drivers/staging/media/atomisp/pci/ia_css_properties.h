/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_PROPERTIES_H
#define __IA_CSS_PROPERTIES_H

/* @file
 * This file contains support for retrieving properties of some hardware the CSS system
 */

#include <type_support.h> /* bool */
#include <ia_css_types.h> /* ia_css_vamem_type */

struct ia_css_properties {
	int  gdc_coord_one;
	bool l1_base_is_index; /** Indicate whether the L1 page base
				    is a page index or a byte address. */
	enum ia_css_vamem_type vamem_type;
};

/* @brief Get hardware properties
 * @param[in,out]	properties The hardware properties
 * @return	None
 *
 * This function returns a number of hardware properties.
 */
void
ia_css_get_properties(struct ia_css_properties *properties);

#endif /* __IA_CSS_PROPERTIES_H */
