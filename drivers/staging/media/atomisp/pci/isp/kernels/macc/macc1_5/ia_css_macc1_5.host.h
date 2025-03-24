/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_MACC1_5_HOST_H
#define __IA_CSS_MACC1_5_HOST_H

#include "ia_css_macc1_5_param.h"
#include "ia_css_macc1_5_table.host.h"

extern const struct ia_css_macc1_5_config default_macc1_5_config;

void
ia_css_macc1_5_encode(
    struct sh_css_isp_macc1_5_params *to,
    const struct ia_css_macc1_5_config *from,
    unsigned int size);

void
ia_css_macc1_5_vmem_encode(
    struct sh_css_isp_macc1_5_vmem_params *params,
    const struct ia_css_macc1_5_table *from,
    unsigned int size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_macc1_5_debug_dtrace(
    const struct ia_css_macc1_5_config *config,
    unsigned int level);
#endif
#endif /* __IA_CSS_MACC1_5_HOST_H */
