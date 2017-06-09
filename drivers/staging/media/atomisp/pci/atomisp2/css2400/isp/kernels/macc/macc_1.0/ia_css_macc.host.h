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

#ifndef __IA_CSS_MACC_HOST_H
#define __IA_CSS_MACC_HOST_H

#include "sh_css_params.h"

#include "ia_css_macc_param.h"
#include "ia_css_macc_table.host.h"

extern const struct ia_css_macc_config default_macc_config;

void
ia_css_macc_encode(
	struct sh_css_isp_macc_params *to,
	const struct ia_css_macc_config *from,
	unsigned size);
	

void
ia_css_macc_dump(
	const struct sh_css_isp_macc_params *macc,
	unsigned level);

void
ia_css_macc_debug_dtrace(
	const struct ia_css_macc_config *config,
	unsigned level);

#endif /* __IA_CSS_MACC_HOST_H */
