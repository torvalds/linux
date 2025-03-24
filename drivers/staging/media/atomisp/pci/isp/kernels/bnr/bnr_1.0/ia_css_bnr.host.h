/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_BNR_HOST_H
#define __IA_CSS_BNR_HOST_H

#include "sh_css_params.h"

#include "ynr/ynr_1.0/ia_css_ynr_types.h"
#include "ia_css_bnr_param.h"

void
ia_css_bnr_encode(
    struct sh_css_isp_bnr_params *to,
    const struct ia_css_nr_config *from,
    unsigned int size);

void
ia_css_bnr_dump(
    const struct sh_css_isp_bnr_params *bnr,
    unsigned int level);

#endif /* __IA_CSS_DP_HOST_H */
