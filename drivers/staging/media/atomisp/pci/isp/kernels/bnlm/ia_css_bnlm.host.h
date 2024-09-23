/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_BNLM_HOST_H
#define __IA_CSS_BNLM_HOST_H

#include "ia_css_bnlm_types.h"
#include "ia_css_bnlm_param.h"

void
ia_css_bnlm_vmem_encode(
    struct bnlm_vmem_params *to,
    const struct ia_css_bnlm_config *from,
    size_t size);

void
ia_css_bnlm_encode(
    struct bnlm_dmem_params *to,
    const struct ia_css_bnlm_config *from,
    size_t size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_bnlm_debug_trace(
    const struct ia_css_bnlm_config *config,
    unsigned int level);
#endif

#endif /* __IA_CSS_BNLM_HOST_H */
