/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_DPC2_HOST_H
#define __IA_CSS_DPC2_HOST_H

#include "ia_css_dpc2_types.h"
#include "ia_css_dpc2_param.h"

void
ia_css_dpc2_encode(
    struct ia_css_isp_dpc2_params *to,
    const struct ia_css_dpc2_config *from,
    size_t size);

void
ia_css_init_dpc2_state(
    void *state,
    size_t size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_dpc2_debug_dtrace(
    const struct ia_css_dpc2_config *config,
    unsigned int level);
#endif

#endif /* __IA_CSS_DPC2_HOST_H */
