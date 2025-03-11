/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef __IA_CSS_IFMTR_H__
#define __IA_CSS_IFMTR_H__

#include <type_support.h>
#include <ia_css_stream_public.h>
#include <ia_css_binary.h>

extern bool ifmtr_set_if_blocking_mode_reset;

unsigned int ia_css_ifmtr_lines_needed_for_bayer_order(
    const struct ia_css_stream_config *config);

unsigned int ia_css_ifmtr_columns_needed_for_bayer_order(
    const struct ia_css_stream_config *config);

int ia_css_ifmtr_configure(struct ia_css_stream_config *config,
				       struct ia_css_binary *binary);

#endif /* __IA_CSS_IFMTR_H__ */
