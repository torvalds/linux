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

#ifndef __SH_CSS_PARAMS_SHADING_H
#define __SH_CSS_PARAMS_SHADING_H

#include <ia_css_types.h>
#include <ia_css_binary.h>

void
sh_css_params_shading_id_table_generate(
	struct ia_css_shading_table **target_table,
#ifndef ISP2401
	const struct ia_css_binary *binary);
#else
	unsigned int table_width,
	unsigned int table_height);
#endif

void
prepare_shading_table(const struct ia_css_shading_table *in_table,
		      unsigned int sensor_binning,
		      struct ia_css_shading_table **target_table,
		      const struct ia_css_binary *binary,
		      unsigned int bds_factor);

#endif /* __SH_CSS_PARAMS_SHADING_H */

