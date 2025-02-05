/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_ITERATOR_HOST_H
#define __IA_CSS_ITERATOR_HOST_H

#include "ia_css_frame_public.h"
#include "ia_css_binary.h"
#include "ia_css_err.h"
#include "ia_css_iterator_param.h"

void
ia_css_iterator_config(
    struct sh_css_isp_iterator_isp_config *to,
    const struct ia_css_iterator_configuration *from,
    unsigned int size);

int
ia_css_iterator_configure(
    const struct ia_css_binary *binary,
    const struct ia_css_frame_info *in_info);

#endif /* __IA_CSS_ITERATOR_HOST_H */
