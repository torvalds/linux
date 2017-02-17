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

#ifndef __IA_CSS_XNR3_0_11_HOST_H
#define __IA_CSS_XNR3_0_11_HOST_H

#include "ia_css_xnr3_0_11_param.h"
#include "ia_css_xnr3_0_11_types.h"

/*
 * Default kernel parameters (weights). In general, default is bypass mode or as close
 * to the ineffective values as possible. Due to the chroma down+upsampling,
 * perfect bypass mode is not possible for xnr3.
 */
extern const struct ia_css_xnr3_0_11_config default_xnr3_0_11_config;


/* (void) = ia_css_xnr3_0_11_vmem_encode(*to, *from)
 * -----------------------------------------------
 * VMEM Encode Function to translate UV parameters from userspace into ISP space
*/
void
ia_css_xnr3_0_11_vmem_encode(
	struct sh_css_isp_xnr3_0_11_vmem_params *to,
	const struct ia_css_xnr3_0_11_config *from,
	unsigned size);

/* (void) = ia_css_xnr3_0_11_encode(*to, *from)
 * -----------------------------------------------
 * DMEM Encode Function to translate UV parameters from userspace into ISP space
 */
void
ia_css_xnr3_0_11_encode(
	struct sh_css_isp_xnr3_0_11_params *to,
	const struct ia_css_xnr3_0_11_config *from,
	unsigned size);

/* (void) = ia_css_xnr3_0_11_debug_dtrace(*config, level)
 * -----------------------------------------------
 * Dummy Function added as the tool expects it
 */
void
ia_css_xnr3_0_11_debug_dtrace(
	const struct ia_css_xnr3_0_11_config *config,
	unsigned level);

#endif /* __IA_CSS_XNR3_0_11_HOST_H */
