/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_OB2_HOST_H
#define __IA_CSS_OB2_HOST_H

#include "ia_css_ob2_types.h"
#include "ia_css_ob2_param.h"

extern const struct ia_css_ob2_config default_ob2_config;

void
ia_css_ob2_encode(
    struct sh_css_isp_ob2_params *to,
    const struct ia_css_ob2_config *from,
    unsigned int size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_ob2_dump(
    const struct sh_css_isp_ob2_params *ob2,
    unsigned int level);

void
ia_css_ob2_debug_dtrace(
    const struct ia_css_ob2_config *config, unsigned int level);
#endif

#endif /* __IA_CSS_OB2_HOST_H */
