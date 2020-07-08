/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
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

#ifndef __FIFO_MONITOR_LOCAL_H_INCLUDED__
#define __FIFO_MONITOR_LOCAL_H_INCLUDED__

#include <type_support.h>
#include "fifo_monitor_global.h"

#include "hive_isp_css_defs.h"	/* ISP_STR_MON_PORT_SND_SP, ... */

#define _hive_str_mon_valid_offset   0
#define _hive_str_mon_accept_offset  1

#define	FIFO_CHANNEL_SP_VALID_MASK		0x55555555
#define	FIFO_CHANNEL_SP_VALID_B_MASK	0x00000055
#define	FIFO_CHANNEL_ISP_VALID_MASK		0x15555555
#define	FIFO_CHANNEL_MOD_VALID_MASK		0x55555555

typedef enum fifo_switch {
	FIFO_SWITCH_IF,
	FIFO_SWITCH_GDC0,
	FIFO_SWITCH_GDC1,
	N_FIFO_SWITCH
} fifo_switch_t;

typedef enum fifo_channel {
	FIFO_CHANNEL_ISP0_TO_SP0,
	FIFO_CHANNEL_SP0_TO_ISP0,
	FIFO_CHANNEL_ISP0_TO_IF0,
	FIFO_CHANNEL_IF0_TO_ISP0,
	FIFO_CHANNEL_ISP0_TO_IF1,
	FIFO_CHANNEL_IF1_TO_ISP0,
	FIFO_CHANNEL_ISP0_TO_DMA0,
	FIFO_CHANNEL_DMA0_TO_ISP0,
	FIFO_CHANNEL_ISP0_TO_GDC0,
	FIFO_CHANNEL_GDC0_TO_ISP0,
	FIFO_CHANNEL_ISP0_TO_GDC1,
	FIFO_CHANNEL_GDC1_TO_ISP0,
	FIFO_CHANNEL_ISP0_TO_HOST0,
	FIFO_CHANNEL_HOST0_TO_ISP0,
	FIFO_CHANNEL_SP0_TO_IF0,
	FIFO_CHANNEL_IF0_TO_SP0,
	FIFO_CHANNEL_SP0_TO_IF1,
	FIFO_CHANNEL_IF1_TO_SP0,
	FIFO_CHANNEL_SP0_TO_IF2,
	FIFO_CHANNEL_IF2_TO_SP0,
	FIFO_CHANNEL_SP0_TO_DMA0,
	FIFO_CHANNEL_DMA0_TO_SP0,
	FIFO_CHANNEL_SP0_TO_GDC0,
	FIFO_CHANNEL_GDC0_TO_SP0,
	FIFO_CHANNEL_SP0_TO_GDC1,
	FIFO_CHANNEL_GDC1_TO_SP0,
	FIFO_CHANNEL_SP0_TO_HOST0,
	FIFO_CHANNEL_HOST0_TO_SP0,
	FIFO_CHANNEL_SP0_TO_STREAM2MEM0,
	FIFO_CHANNEL_STREAM2MEM0_TO_SP0,
	FIFO_CHANNEL_SP0_TO_INPUT_SYSTEM0,
	FIFO_CHANNEL_INPUT_SYSTEM0_TO_SP0,
	/*
	 * No clue what this is
	 *
		FIFO_CHANNEL_SP0_TO_IRQ0,
		FIFO_CHANNEL_IRQ0_TO_SP0,
	 */
	N_FIFO_CHANNEL
} fifo_channel_t;

struct fifo_channel_state_s {
	bool	src_valid;
	bool	fifo_accept;
	bool	fifo_valid;
	bool	sink_accept;
};

/* The switch is tri-state */
struct fifo_switch_state_s {
	bool	is_none;
	bool	is_isp;
	bool	is_sp;
};

struct fifo_monitor_state_s {
	struct fifo_channel_state_s	fifo_channels[N_FIFO_CHANNEL];
	struct fifo_switch_state_s	fifo_switches[N_FIFO_SWITCH];
};

#endif /* __FIFO_MONITOR_LOCAL_H_INCLUDED__ */
