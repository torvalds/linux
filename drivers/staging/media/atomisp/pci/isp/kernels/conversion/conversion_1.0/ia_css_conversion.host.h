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

#ifndef __IA_CSS_CONVERSION_HOST_H
#define __IA_CSS_CONVERSION_HOST_H

#include "ia_css_conversion_types.h"
#include "ia_css_conversion_param.h"

extern const struct ia_css_conversion_config default_conversion_config;

void
ia_css_conversion_encode(
    struct sh_css_isp_conversion_params *to,
    const struct ia_css_conversion_config *from,
    unsigned int size);

#endif /* __IA_CSS_CONVERSION_HOST_H */
