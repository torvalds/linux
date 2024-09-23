/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_CTC_PARAM_H
#define __IA_CSS_CTC_PARAM_H

#include "type_support.h"
#include <system_global.h>

#include "ia_css_ctc_types.h"

#ifndef PIPE_GENERATION
#define SH_CSS_ISP_CTC_TABLE_SIZE_LOG2       IA_CSS_VAMEM_2_CTC_TABLE_SIZE_LOG2
#define SH_CSS_ISP_CTC_TABLE_SIZE            IA_CSS_VAMEM_2_CTC_TABLE_SIZE

#else
/* For pipe generation, the size is not relevant */
#define SH_CSS_ISP_CTC_TABLE_SIZE 0
#endif

/* This should be vamem_data_t, but that breaks the pipe generator */
struct sh_css_isp_ctc_vamem_params {
	u16 ctc[SH_CSS_ISP_CTC_TABLE_SIZE];
};

#endif /* __IA_CSS_CTC_PARAM_H */
