/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __SYSTEM_LOCAL_H_INCLUDED__
#define __SYSTEM_LOCAL_H_INCLUDED__

#ifdef HRT_ISP_CSS_CUSTOM_HOST
#ifndef HRT_USE_VIR_ADDRS
#define HRT_USE_VIR_ADDRS
#endif
#endif

#include "system_global.h"

/* This interface is deprecated */
#include "hive_types.h"

/*
 * Cell specific address maps
 */

#define GP_FIFO_BASE   ((hrt_address)0x0000000000090104)		/* This is NOT a base address */

/* ISP */
extern const hrt_address ISP_CTRL_BASE[N_ISP_ID];
extern const hrt_address ISP_DMEM_BASE[N_ISP_ID];
extern const hrt_address ISP_BAMEM_BASE[N_BAMEM_ID];

/* SP */
extern const hrt_address SP_CTRL_BASE[N_SP_ID];
extern const hrt_address SP_DMEM_BASE[N_SP_ID];

/* MMU */

extern const hrt_address MMU_BASE[N_MMU_ID];

/* DMA */
extern const hrt_address DMA_BASE[N_DMA_ID];
extern const hrt_address ISYS2401_DMA_BASE[N_ISYS2401_DMA_ID];

/* IRQ */
extern const hrt_address IRQ_BASE[N_IRQ_ID];

/* GDC */
extern const hrt_address GDC_BASE[N_GDC_ID];

/* FIFO_MONITOR (not a subset of GP_DEVICE) */
extern const hrt_address FIFO_MONITOR_BASE[N_FIFO_MONITOR_ID];

/* GP_DEVICE (single base for all separate GP_REG instances) */
extern const hrt_address GP_DEVICE_BASE[N_GP_DEVICE_ID];

/*GP TIMER , all timer registers are inter-twined,
 * so, having multiple base addresses for
 * different timers does not help*/
extern const hrt_address GP_TIMER_BASE;

/* GPIO */
extern const hrt_address GPIO_BASE[N_GPIO_ID];

/* TIMED_CTRL */
extern const hrt_address TIMED_CTRL_BASE[N_TIMED_CTRL_ID];

/* INPUT_FORMATTER */
extern const hrt_address INPUT_FORMATTER_BASE[N_INPUT_FORMATTER_ID];

/* INPUT_SYSTEM */
extern const hrt_address INPUT_SYSTEM_BASE[N_INPUT_SYSTEM_ID];

/* RX, the MIPI lane control regs start at offset 0 */
extern const hrt_address RX_BASE[N_RX_ID];

/* IBUF_CTRL, part of the Input System 2401 */
extern const hrt_address IBUF_CTRL_BASE[N_IBUF_CTRL_ID];

/* ISYS IRQ Controllers, part of the Input System 2401 */
extern const hrt_address ISYS_IRQ_BASE[N_ISYS_IRQ_ID];

/* CSI FE, part of the Input System 2401 */
extern const hrt_address CSI_RX_FE_CTRL_BASE[N_CSI_RX_FRONTEND_ID];

/* CSI BE, part of the Input System 2401 */
extern const hrt_address CSI_RX_BE_CTRL_BASE[N_CSI_RX_BACKEND_ID];

/* PIXEL Generator, part of the Input System 2401 */
extern const hrt_address PIXELGEN_CTRL_BASE[N_PIXELGEN_ID];

/* Stream2MMIO, part of the Input System 2401 */
extern const hrt_address STREAM2MMIO_CTRL_BASE[N_STREAM2MMIO_ID];

#endif /* __SYSTEM_LOCAL_H_INCLUDED__ */
