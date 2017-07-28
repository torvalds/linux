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

#ifndef __IA_CSS_CNR2_HOST_H
#define __IA_CSS_CNR2_HOST_H

#include "ia_css_cnr2_types.h"
#include "ia_css_cnr2_param.h"

extern const struct ia_css_cnr_config default_cnr_config;

void
ia_css_cnr_encode(
	struct sh_css_isp_cnr_params *to,
	const struct ia_css_cnr_config *from,
	unsigned size);

void
ia_css_cnr_dump(
	const struct sh_css_isp_cnr_params *cnr,
	unsigned level);

void
ia_css_cnr_debug_dtrace(
	const struct ia_css_cnr_config *config,
	unsigned level);

void
ia_css_init_cnr2_state(
	void/*struct sh_css_isp_cnr_vmem_state*/ *state,
	size_t size);
#endif /* __IA_CSS_CNR2_HOST_H */
