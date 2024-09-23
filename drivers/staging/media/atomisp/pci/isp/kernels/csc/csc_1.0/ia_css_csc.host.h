/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_CSC_HOST_H
#define __IA_CSS_CSC_HOST_H

#include "ia_css_csc_types.h"
#include "ia_css_csc_param.h"

extern const struct ia_css_cc_config default_cc_config;

void
ia_css_encode_cc(
    struct sh_css_isp_csc_params *to,
    const struct ia_css_cc_config *from,
    unsigned int size);

void
ia_css_csc_encode(
    struct sh_css_isp_csc_params *to,
    const struct ia_css_cc_config *from,
    unsigned int size);

#ifndef IA_CSS_NO_DEBUG
void
ia_css_cc_dump(
    const struct sh_css_isp_csc_params *csc, unsigned int level,
    const char *name);

void
ia_css_csc_dump(
    const struct sh_css_isp_csc_params *csc,
    unsigned int level);

void
ia_css_cc_config_debug_dtrace(
    const struct ia_css_cc_config *config,
    unsigned int level);

#define ia_css_csc_debug_dtrace ia_css_cc_config_debug_dtrace
#endif

#endif /* __IA_CSS_CSC_HOST_H */
