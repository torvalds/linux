/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IA_CSS_YNR_HOST_H
#define __IA_CSS_YNR_HOST_H

#include "ia_css_ynr_types.h"
#include "ia_css_ynr_param.h"

extern const struct ia_css_nr_config default_nr_config;
extern const struct ia_css_ee_config default_ee_config;

void
ia_css_nr_encode(
    struct sh_css_isp_ynr_params *to,
    const struct ia_css_nr_config *from,
    unsigned int size);

void
ia_css_yee_encode(
    struct sh_css_isp_yee_params *to,
    const struct ia_css_yee_config *from,
    unsigned int size);

void
ia_css_nr_dump(
    const struct sh_css_isp_ynr_params *ynr,
    unsigned int level);

void
ia_css_yee_dump(
    const struct sh_css_isp_yee_params *yee,
    unsigned int level);

void
ia_css_nr_debug_dtrace(
    const struct ia_css_nr_config *config,
    unsigned int level);

void
ia_css_ee_debug_dtrace(
    const struct ia_css_ee_config *config,
    unsigned int level);

void
ia_css_init_ynr_state(
    void/*struct sh_css_isp_ynr_vmem_state*/ * state,
    size_t size);
#endif /* __IA_CSS_YNR_HOST_H */
