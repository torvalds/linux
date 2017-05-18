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

#ifndef __IA_CSS_CSC_HOST_H
#define __IA_CSS_CSC_HOST_H

#include "ia_css_csc_types.h"
#include "ia_css_csc_param.h"

extern const struct ia_css_cc_config default_cc_config;

void
ia_css_encode_cc(
	struct sh_css_isp_csc_params *to,
	const struct ia_css_cc_config *from,
	unsigned size);

void
ia_css_csc_encode(
	struct sh_css_isp_csc_params *to,
	const struct ia_css_cc_config *from,
	unsigned size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_cc_dump(
	const struct sh_css_isp_csc_params *csc, unsigned level,
	const char *name);

void
ia_css_csc_dump(
	const struct sh_css_isp_csc_params *csc,
	unsigned level);

void
ia_css_cc_config_debug_dtrace(
	const struct ia_css_cc_config *config,
	unsigned level);

#define ia_css_csc_debug_dtrace ia_css_cc_config_debug_dtrace
#endif

#endif /* __IA_CSS_CSC_HOST_H */
