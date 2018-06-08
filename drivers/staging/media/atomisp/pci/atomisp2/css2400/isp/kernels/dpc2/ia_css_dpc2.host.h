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

#ifndef __IA_CSS_DPC2_HOST_H
#define __IA_CSS_DPC2_HOST_H

#include "ia_css_dpc2_types.h"
#include "ia_css_dpc2_param.h"

void
ia_css_dpc2_encode(
	struct ia_css_isp_dpc2_params *to,
	const struct ia_css_dpc2_config *from,
	size_t size);

void
ia_css_init_dpc2_state(
	void *state,
	size_t size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_dpc2_debug_dtrace(
	const struct ia_css_dpc2_config *config,
	unsigned level);
#endif

#endif /* __IA_CSS_DPC2_HOST_H */
