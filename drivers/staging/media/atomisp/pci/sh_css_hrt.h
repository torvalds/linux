/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _SH_CSS_HRT_H_
#define _SH_CSS_HRT_H_

#include <sp.h>
#include <isp.h>

#include <ia_css_err.h>

/* SP access */
void sh_css_hrt_sp_start_si(void);

void sh_css_hrt_sp_start_copy_frame(void);

void sh_css_hrt_sp_start_isp(void);

int sh_css_hrt_sp_wait(void);

bool sh_css_hrt_system_is_idle(void);

#endif /* _SH_CSS_HRT_H_ */
