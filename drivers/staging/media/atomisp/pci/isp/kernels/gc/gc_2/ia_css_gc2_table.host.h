/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_GC2_TABLE_HOST_H
#define __IA_CSS_GC2_TABLE_HOST_H

#include "ia_css_gc2_types.h"

extern struct ia_css_rgb_gamma_table default_r_gamma_table;
extern struct ia_css_rgb_gamma_table default_g_gamma_table;
extern struct ia_css_rgb_gamma_table default_b_gamma_table;

void ia_css_config_rgb_gamma_tables(void);

#endif /* __IA_CSS_GC2_TABLE_HOST_H */
