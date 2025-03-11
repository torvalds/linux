// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "ia_css_types.h"
#include "sh_css_defs.h"
#include "ia_css_debug.h"

#include "ia_css_cnr.host.h"

/* keep the interface here, it is not enabled yet because host doesn't know the size of individual state */
void
ia_css_init_cnr_state(
    void/*struct sh_css_isp_cnr_vmem_state*/ * state,
    size_t size)
{
	memset(state, 0, size);
}
