/* SPDX-License-Identifier: GPL-2.0 */
/* Release Version: irci_stable_candrpv_0415_20150521_0458 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_HDR_HOST_H
#define __IA_CSS_HDR_HOST_H

#include "ia_css_hdr_param.h"
#include "ia_css_hdr_types.h"

extern const struct ia_css_hdr_config default_hdr_config;

void
ia_css_hdr_init_config(
    struct sh_css_isp_hdr_params *to,
    const struct ia_css_hdr_config *from,
    unsigned int size);

#endif /* __IA_CSS_HDR_HOST_H */
