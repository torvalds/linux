/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_CNR_HOST_H
#define __IA_CSS_CNR_HOST_H

#include "ia_css_cnr_param.h"

void
ia_css_init_cnr_state(
    void/*struct sh_css_isp_cnr_vmem_state*/ * state,
    size_t size);

#endif /* __IA_CSS_CNR_HOST_H */
