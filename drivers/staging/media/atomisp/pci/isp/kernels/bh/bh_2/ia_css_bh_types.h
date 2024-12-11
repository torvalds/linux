/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_BH_TYPES_H
#define __IA_CSS_BH_TYPES_H

/* Number of elements in the BH table.
  * Should be consistent with hmem.h
  */
#define IA_CSS_HMEM_BH_TABLE_SIZE	ISP_HIST_DEPTH
#define IA_CSS_HMEM_BH_UNIT_SIZE	(ISP_HIST_DEPTH / ISP_HIST_COMPONENTS)

#define BH_COLOR_R	(0)
#define BH_COLOR_G	(1)
#define BH_COLOR_B	(2)
#define BH_COLOR_Y	(3)
#define BH_COLOR_NUM	(4)

/* BH table */
struct ia_css_bh_table {
	u32 hmem[ISP_HIST_COMPONENTS][IA_CSS_HMEM_BH_UNIT_SIZE];
};

#endif /* __IA_CSS_BH_TYPES_H */
