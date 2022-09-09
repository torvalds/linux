/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_ANR2_HOST_H
#define __IA_CSS_ANR2_HOST_H

#include "sh_css_params.h"

#include "ia_css_anr2_types.h"
#include "ia_css_anr2_param.h"
#include "ia_css_anr2_table.host.h"

void
ia_css_anr2_vmem_encode(
    struct ia_css_isp_anr2_params *to,
    const struct ia_css_anr_thres *from,
    size_t size);

void
ia_css_anr2_debug_dtrace(
    const struct ia_css_anr_thres *config, unsigned int level)
;

#endif /* __IA_CSS_ANR2_HOST_H */
