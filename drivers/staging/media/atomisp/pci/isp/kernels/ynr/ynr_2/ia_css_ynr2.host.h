/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_YNR2_HOST_H
#define __IA_CSS_YNR2_HOST_H

#include "ia_css_ynr2_types.h"
#include "ia_css_ynr2_param.h"

extern const struct ia_css_ynr_config default_ynr_config;
extern const struct ia_css_fc_config  default_fc_config;

void
ia_css_ynr_encode(
    struct sh_css_isp_yee2_params *to,
    const struct ia_css_ynr_config *from,
    unsigned int size);

void
ia_css_fc_encode(
    struct sh_css_isp_fc_params *to,
    const struct ia_css_fc_config *from,
    unsigned int size);

void
ia_css_ynr_dump(
    const struct sh_css_isp_yee2_params *yee2,
    unsigned int level);

void
ia_css_fc_dump(
    const struct sh_css_isp_fc_params *fc,
    unsigned int level);

void
ia_css_fc_debug_dtrace(
    const struct ia_css_fc_config *config,
    unsigned int level);

void
ia_css_ynr_debug_dtrace(
    const struct ia_css_ynr_config *config,
    unsigned int level);

#endif /* __IA_CSS_YNR2_HOST_H */
