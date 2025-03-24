/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_DE2_HOST_H
#define __IA_CSS_DE2_HOST_H

#include "ia_css_de2_types.h"
#include "ia_css_de2_param.h"

extern const struct ia_css_ecd_config default_ecd_config;

void
ia_css_ecd_encode(
    struct sh_css_isp_ecd_params *to,
    const struct ia_css_ecd_config *from,
    unsigned int size);

void
ia_css_ecd_dump(
    const struct sh_css_isp_ecd_params *ecd,
    unsigned int level);

void
ia_css_ecd_debug_dtrace(
    const struct ia_css_ecd_config *config, unsigned int level);

#endif /* __IA_CSS_DE2_HOST_H */
