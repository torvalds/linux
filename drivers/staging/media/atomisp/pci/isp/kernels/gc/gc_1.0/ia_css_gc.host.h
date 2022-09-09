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

#ifndef __IA_CSS_GC_HOST_H
#define __IA_CSS_GC_HOST_H

#include "ia_css_gc_param.h"
#include "ia_css_gc_table.host.h"

extern const struct ia_css_gc_config default_gc_config;
extern const struct ia_css_ce_config default_ce_config;

void
ia_css_gc_encode(
    struct sh_css_isp_gc_params *to,
    const struct ia_css_gc_config *from,
    unsigned int size);

void
ia_css_gc_vamem_encode(
    struct sh_css_isp_gc_vamem_params *to,
    const struct ia_css_gamma_table *from,
    unsigned int size);

void
ia_css_ce_encode(
    struct sh_css_isp_ce_params *to,
    const struct ia_css_ce_config *from,
    unsigned int size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_gc_dump(
    const struct sh_css_isp_gc_params *gc,
    unsigned int level);

void
ia_css_ce_dump(
    const struct sh_css_isp_ce_params *ce,
    unsigned int level);

void
ia_css_gc_debug_dtrace(
    const struct ia_css_gc_config *config,
    unsigned int level);

void
ia_css_ce_debug_dtrace(
    const struct ia_css_ce_config *config,
    unsigned int level);

#endif

#endif /* __IA_CSS_GC_HOST_H */
