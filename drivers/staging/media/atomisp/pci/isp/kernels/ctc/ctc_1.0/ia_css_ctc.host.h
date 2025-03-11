/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_CTC_HOST_H
#define __IA_CSS_CTC_HOST_H

#include "sh_css_params.h"

#include "ia_css_ctc_param.h"
#include "ia_css_ctc_table.host.h"

extern const struct ia_css_ctc_config default_ctc_config;

void
ia_css_ctc_vamem_encode(
    struct sh_css_isp_ctc_vamem_params *to,
    const struct ia_css_ctc_table *from,
    unsigned int size);

void
ia_css_ctc_debug_dtrace(
    const struct ia_css_ctc_config *config, unsigned int level)
;

#endif /* __IA_CSS_CTC_HOST_H */
