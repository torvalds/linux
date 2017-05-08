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

#include <type_support.h>
#include "system_global.h"

const uint32_t N_IBUF_CTRL_PROCS[N_IBUF_CTRL_ID] = {
	8,	/* IBUF_CTRL0_ID supports at most 8 processes */
	4,	/* IBUF_CTRL1_ID supports at most 4 processes */
	4	/* IBUF_CTRL2_ID supports at most 4 processes */
};
