/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_EED1_8_HOST_H
#define __IA_CSS_EED1_8_HOST_H

#include "ia_css_eed1_8_types.h"
#include "ia_css_eed1_8_param.h"

void
ia_css_eed1_8_vmem_encode(
    struct eed1_8_vmem_params *to,
    const struct ia_css_eed1_8_config *from,
    size_t size);

void
ia_css_eed1_8_encode(
    struct eed1_8_dmem_params *to,
    const struct ia_css_eed1_8_config *from,
    size_t size);

void
ia_css_init_eed1_8_state(
    void *state,
    size_t size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_eed1_8_debug_dtrace(
    const struct ia_css_eed1_8_config *config,
    unsigned int level);
#endif

#endif /* __IA_CSS_EED1_8_HOST_H */
