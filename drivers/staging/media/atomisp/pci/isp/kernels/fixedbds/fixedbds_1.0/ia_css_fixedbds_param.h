/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_FIXEDBDS_PARAM_H
#define __IA_CSS_FIXEDBDS_PARAM_H

#include "type_support.h"

/* ISP2401 */
#define BDS_UNIT 8
#define FRAC_LOG 3
#define FRAC_ACC BIT(FRAC_LOG)
#if FRAC_ACC != BDS_UNIT
#error "FRAC_ACC and BDS_UNIT need to be merged into one define"
#endif

struct sh_css_isp_bds_params {
	int baf_strength;
};

#endif /* __IA_CSS_FIXEDBDS_PARAM_H */
