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
#ifndef __IA_CSS_BNR2_2_HOST_H
#define __IA_CSS_BNR2_2_HOST_H

#include "ia_css_bnr2_2_types.h"
#include "ia_css_bnr2_2_param.h"

extern const struct ia_css_bnr2_2_config default_bnr2_2_config;

void
ia_css_bnr2_2_encode(
    struct sh_css_isp_bnr2_2_params *to,
    const struct ia_css_bnr2_2_config *from,
    size_t size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_bnr2_2_debug_dtrace(
    const struct ia_css_bnr2_2_config *config,
    unsigned int level);
#endif

#endif /* __IA_CSS_BNR2_2_HOST_H */
