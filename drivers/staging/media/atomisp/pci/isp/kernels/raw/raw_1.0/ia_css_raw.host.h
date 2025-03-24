/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_RAW_HOST_H
#define __IA_CSS_RAW_HOST_H

#include "ia_css_binary.h"

#include "ia_css_raw_types.h"
#include "ia_css_raw_param.h"

int ia_css_raw_config(struct sh_css_isp_raw_isp_config      *to,
		      const struct ia_css_raw_configuration *from,
		      unsigned int size);

int ia_css_raw_configure(const struct sh_css_sp_pipeline *pipe,
			 const struct ia_css_binary     *binary,
			 const struct ia_css_frame_info *in_info,
			 const struct ia_css_frame_info *internal_info,
			 bool two_ppc,
			 bool deinterleaved);

#endif /* __IA_CSS_RAW_HOST_H */
