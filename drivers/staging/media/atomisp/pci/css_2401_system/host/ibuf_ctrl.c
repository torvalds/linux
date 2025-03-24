// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include <type_support.h>
#include "system_global.h"
#include "ibuf_ctrl_global.h"

const u32 N_IBUF_CTRL_PROCS[N_IBUF_CTRL_ID] = {
	8,	/* IBUF_CTRL0_ID supports at most 8 processes */
	4,	/* IBUF_CTRL1_ID supports at most 4 processes */
	4	/* IBUF_CTRL2_ID supports at most 4 processes */
};
