/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __SH_CSS_PARAMS_SHADING_H
#define __SH_CSS_PARAMS_SHADING_H

#include <ia_css_types.h>
#include <ia_css_binary.h>

void
sh_css_params_shading_id_table_generate(
    struct ia_css_shading_table **target_table,
    unsigned int table_width,
    unsigned int table_height);

void
prepare_shading_table(const struct ia_css_shading_table *in_table,
		      unsigned int sensor_binning,
		      struct ia_css_shading_table **target_table,
		      const struct ia_css_binary *binary,
		      unsigned int bds_factor);

#endif /* __SH_CSS_PARAMS_SHADING_H */
