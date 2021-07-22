/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _IA_CSS_BUFQ_COMM_H
#define _IA_CSS_BUFQ_COMM_H

#include "system_global.h"

enum sh_css_queue_id {
	SH_CSS_INVALID_QUEUE_ID     = -1,
	SH_CSS_QUEUE_A_ID = 0,
	SH_CSS_QUEUE_B_ID,
	SH_CSS_QUEUE_C_ID,
	SH_CSS_QUEUE_D_ID,
	SH_CSS_QUEUE_E_ID,
	SH_CSS_QUEUE_F_ID,
	SH_CSS_QUEUE_G_ID,
	SH_CSS_QUEUE_H_ID, /* for metadata */

#define SH_CSS_MAX_NUM_QUEUES (SH_CSS_QUEUE_H_ID + 1)

};

#define SH_CSS_MAX_DYNAMIC_BUFFERS_PER_THREAD SH_CSS_MAX_NUM_QUEUES
/* for now we staticaly assign queue 0 & 1 to parameter sets */
#define IA_CSS_PARAMETER_SET_QUEUE_ID SH_CSS_QUEUE_A_ID
#define IA_CSS_PER_FRAME_PARAMETER_SET_QUEUE_ID SH_CSS_QUEUE_B_ID

#endif
