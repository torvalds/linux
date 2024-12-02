/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_OB_HOST_H
#define __IA_CSS_OB_HOST_H

#include "ia_css_ob_types.h"
#include "ia_css_ob_param.h"

extern const struct ia_css_ob_config default_ob_config;

void
ia_css_ob_configure(
    struct sh_css_isp_ob_stream_config *config,
    unsigned int isp_pipe_version,
    unsigned int raw_bit_depth);

void
ia_css_ob_encode(
    struct sh_css_isp_ob_params *to,
    const struct ia_css_ob_config *from,
    const struct sh_css_isp_ob_stream_config *config,
    unsigned int size);

void
ia_css_ob_vmem_encode(
    struct sh_css_isp_ob_vmem_params *to,
    const struct ia_css_ob_config *from,
    const struct sh_css_isp_ob_stream_config *config,
    unsigned int size);

void
ia_css_ob_dump(
    const struct sh_css_isp_ob_params *ob,
    unsigned int level);

void
ia_css_ob_debug_dtrace(
    const struct ia_css_ob_config *config, unsigned int level)
;

#endif /* __IA_CSS_OB_HOST_H */
