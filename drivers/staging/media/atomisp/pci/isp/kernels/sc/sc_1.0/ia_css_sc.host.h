/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_SC_HOST_H
#define __IA_CSS_SC_HOST_H

#include "sh_css_params.h"

#include "ia_css_sc_types.h"
#include "ia_css_sc_param.h"

void
ia_css_sc_encode(
    struct sh_css_isp_sc_params *to,
    struct ia_css_shading_table **from,
    unsigned int size);

void
ia_css_sc_dump(
    const struct sh_css_isp_sc_params *sc,
    unsigned int level);

/* ------ deprecated(bz675) : from ------ */
void
sh_css_get_shading_settings(const struct ia_css_isp_parameters *params,
			    struct ia_css_shading_settings *settings);

void
sh_css_set_shading_settings(struct ia_css_isp_parameters *params,
			    const struct ia_css_shading_settings *settings);
/* ------ deprecated(bz675) : to ------ */

#endif /* __IA_CSS_SC_HOST_H */
