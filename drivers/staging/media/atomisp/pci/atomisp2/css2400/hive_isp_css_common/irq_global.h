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

#include <system_types.h>

#define IS_IRQ_VERSION_2
#define IS_IRQ_MAP_VERSION_2

/* We cannot include the (hrt host ID) file defining the "CSS_RECEIVER" property without side effects */
#ifndef HAS_NO_RX
#if defined(IS_ISP_2400_MAMOIADA_SYSTEM)
/*#define CSS_RECEIVER testbench_isp_inp_sys_csi_receiver*/
#include "hive_isp_css_irq_types_hrt.h"	/* enum	hrt_isp_css_irq */
#elif defined(IS_ISP_2401_MAMOIADA_SYSTEM)
/*#define CSS_RECEIVER testbench_isp_is_2400_inp_sys_csi_receiver*/
#include "hive_isp_css_2401_irq_types_hrt.h"	/* enum	hrt_isp_css_irq */
#else
#error "irq_global.h: 2400_SYSTEM must be one of {2400, 2401 }"
#endif
#endif

/* The IRQ is not mapped uniformly on its related interfaces */
#define	IRQ_SW_CHANNEL_OFFSET	hrt_isp_css_irq_sw_pin_0

typedef enum {
	IRQ_SW_CHANNEL0_ID = hrt_isp_css_irq_sw_pin_0 - IRQ_SW_CHANNEL_OFFSET,
	IRQ_SW_CHANNEL1_ID = hrt_isp_css_irq_sw_pin_1 - IRQ_SW_CHANNEL_OFFSET,
	N_IRQ_SW_CHANNEL_ID
} irq_sw_channel_id_t;

#endif /* __IRQ_GLOBAL_H_INCLUDED__ */
