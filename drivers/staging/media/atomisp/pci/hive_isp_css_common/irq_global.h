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

#ifndef __IRQ_GLOBAL_H_INCLUDED__
#define __IRQ_GLOBAL_H_INCLUDED__

#include <system_local.h>

#define IS_IRQ_VERSION_2
#define IS_IRQ_MAP_VERSION_2

/* We cannot include the (hrt host ID) file defining the "CSS_RECEIVER" property without side effects */
#ifndef HAS_NO_RX
#include "irq_types_hrt.h"
#endif

/* The IRQ is not mapped uniformly on its related interfaces */
#define	IRQ_SW_CHANNEL_OFFSET	hrt_isp_css_irq_sw_pin_0

typedef enum {
	IRQ_SW_CHANNEL0_ID = hrt_isp_css_irq_sw_pin_0 - IRQ_SW_CHANNEL_OFFSET,
	IRQ_SW_CHANNEL1_ID = hrt_isp_css_irq_sw_pin_1 - IRQ_SW_CHANNEL_OFFSET,
	N_IRQ_SW_CHANNEL_ID
} irq_sw_channel_id_t;

#endif /* __IRQ_GLOBAL_H_INCLUDED__ */
