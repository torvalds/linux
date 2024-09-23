/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_UDS_PARAM_H
#define __IA_CSS_UDS_PARAM_H

#include "sh_css_uds.h"

/* uds (Up and Down scaling) */
struct ia_css_uds_config {
	struct sh_css_crop_pos crop_pos;
	struct sh_css_uds_info uds;
};

struct sh_css_sp_uds_params {
	struct sh_css_crop_pos crop_pos;
	struct sh_css_uds_info uds;
};

#endif /* __IA_CSS_UDS_PARAM_H */
