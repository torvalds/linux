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

#ifndef __IA_CSS_CTC_HOST_H
#define __IA_CSS_CTC_HOST_H

#include "sh_css_params.h"

#include "ia_css_ctc_param.h"
#include "ia_css_ctc_table.host.h"

extern const struct ia_css_ctc_config default_ctc_config;

void
ia_css_ctc_vamem_encode(
	struct sh_css_isp_ctc_vamem_params *to,
	const struct ia_css_ctc_table *from,
	unsigned size);

void
ia_css_ctc_debug_dtrace(
	const struct ia_css_ctc_config *config, unsigned level)
;

#endif /* __IA_CSS_CTC_HOST_H */
