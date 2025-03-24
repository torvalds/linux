/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_REF_HOST_H
#define __IA_CSS_REF_HOST_H

#include <ia_css_frame_public.h>
#include <ia_css_binary.h>

#include "ia_css_ref_types.h"
#include "ia_css_ref_param.h"
#include "ia_css_ref_state.h"

int ia_css_ref_config(struct sh_css_isp_ref_isp_config      *to,
		      const struct ia_css_ref_configuration *from,
		      unsigned int size);

int ia_css_ref_configure(const struct ia_css_binary        *binary,
			 const struct ia_css_frame * const *ref_frames,
			 const uint32_t                    dvs_frame_delay);

void
ia_css_init_ref_state(
    struct sh_css_isp_ref_dmem_state *state,
    unsigned int size);
#endif /* __IA_CSS_REF_HOST_H */
