/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_QPLANE_HOST_H
#define __IA_CSS_QPLANE_HOST_H

#include <ia_css_frame_public.h>
#include <ia_css_binary.h>

#if 0
/* Cannot be included, since sh_css_internal.h is too generic
 * e.g. for FW generation.
*/
#include "sh_css_internal.h"	/* sh_css_sp_pipeline */
#endif

#include "ia_css_qplane_types.h"
#include "ia_css_qplane_param.h"

int ia_css_qplane_config(struct sh_css_isp_qplane_isp_config      *to,
			 const struct ia_css_qplane_configuration *from,
			 unsigned int size);

int ia_css_qplane_configure(const struct sh_css_sp_pipeline *pipe,
			    const struct ia_css_binary      *binary,
			    const struct ia_css_frame_info  *from);

#endif /* __IA_CSS_QPLANE_HOST_H */
