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

#ifndef __IA_CSS_DVS_PARAM_H
#define __IA_CSS_DVS_PARAM_H

#include <type_support.h>
#ifdef ISP2401

#if !defined(ENABLE_TPROXY) && !defined(ENABLE_CRUN_FOR_TD) && !defined(PARAMBIN_GENERATION)
#endif
#include "dma.h"
#ifdef ISP2401
#endif /* !defined(ENABLE_TPROXY) && !defined(ENABLE_CRUN_FOR_TD) */

#endif
#include "uds/uds_1.0/ia_css_uds_param.h"

#ifdef ISP2401

#endif
/* dvserence frame */
struct sh_css_isp_dvs_isp_config {
	uint32_t num_horizontal_blocks;
	uint32_t num_vertical_blocks;
};

#endif /* __IA_CSS_DVS_PARAM_H */
