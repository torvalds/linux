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

#ifndef __SYSTEM_LOCAL_H_INCLUDED__
#define __SYSTEM_LOCAL_H_INCLUDED__

#ifdef HRT_ISP_CSS_CUSTOM_HOST
#ifndef HRT_USE_VIR_ADDRS
#define HRT_USE_VIR_ADDRS
#endif
#endif

#include "system_global.h"

/* HRT assumes 32 by default (see Linux/include/hive_types.h), overrule it in case it is different */
#undef HRT_ADDRESS_WIDTH
#define HRT_ADDRESS_WIDTH	64		/* Surprise, this is a local property */

/* This interface is deprecated */
#include "hive_types.h"

/*
 * Cell specific address maps
 */
#if HRT_ADDRESS_WIDTH == 64

#define GP_FIFO_BASE   ((hrt_address)0x0000000000090104)		/* This is NOT a base address */

/* DDR */
static const hrt_address DDR_BASE[N_DDR_ID] = {
	(hrt_address)0x0000000120000000ULL
};

/* ISP */
static const hrt_address ISP_CTRL_BASE[N_ISP_ID] = {
	(hrt_address)0x0000000000020000ULL
};

static const hrt_address ISP_DMEM_BASE[N_ISP_ID] = {
	(hrt_address)0x0000000000200000ULL
};

static const hrt_address ISP_BAMEM_BASE[N_BAMEM_ID] = {
	(hrt_address)0x0000000000100000ULL
};

static const hrt_address ISP_VAMEM_BASE[N_VAMEM_ID] = {
	(hrt_address)0x00000000001C0000ULL,
	(hrt_address)0x00000000001D0000ULL,
	(hrt_address)0x00000000001E0000ULL
};

static const hrt_address ISP_HMEM_BASE[N_HMEM_ID] = {
	(hrt_address)0x00000000001F0000ULL
};

/* SP */
static const hrt_address SP_CTRL_BASE[N_SP_ID] = {
	(hrt_address)0x0000000000010000ULL
};

static const hrt_address SP_DMEM_BASE[N_SP_ID] = {
	(hrt_address)0x0000000000300000ULL
};

static const hrt_address SP_PMEM_BASE[N_SP_ID] = {
	(hrt_address)0x00000000000B0000ULL
};

/* MMU */
/*
 * MMU0_ID: The data MMU
 * MMU1_ID: The icache MMU
 */
static const hrt_address MMU_BASE[N_MMU_ID] = {
	(hrt_address)0x0000000000070000ULL,
	(hrt_address)0x00000000000A0000ULL
};

/* DMA */
static const hrt_address DMA_BASE[N_DMA_ID] = {
	(hrt_address)0x0000000000040000ULL
};

/* IRQ */
static const hrt_address IRQ_BASE[N_IRQ_ID] = {
	(hrt_address)0x0000000000000500ULL,
	(hrt_address)0x0000000000030A00ULL,
	(hrt_address)0x000000000008C000ULL,
	(hrt_address)0x0000000000090200ULL
};

/*
	(hrt_address)0x0000000000000500ULL};
 */

/* GDC */
static const hrt_address GDC_BASE[N_GDC_ID] = {
	(hrt_address)0x0000000000050000ULL,
	(hrt_address)0x0000000000060000ULL
};

/* FIFO_MONITOR (not a subset of GP_DEVICE) */
static const hrt_address FIFO_MONITOR_BASE[N_FIFO_MONITOR_ID] = {
	(hrt_address)0x0000000000000000ULL
};

/*
static const hrt_address GP_REGS_BASE[N_GP_REGS_ID] = {
	(hrt_address)0x0000000000000000ULL};

static const hrt_address GP_DEVICE_BASE[N_GP_DEVICE_ID] = {
	(hrt_address)0x0000000000090000ULL};
*/

/* GP_DEVICE (single base for all separate GP_REG instances) */
static const hrt_address GP_DEVICE_BASE[N_GP_DEVICE_ID] = {
	(hrt_address)0x0000000000000000ULL
};

/*GP TIMER , all timer registers are inter-twined,
 * so, having multiple base addresses for
 * different timers does not help*/
static const hrt_address GP_TIMER_BASE =
    (hrt_address)0x0000000000000600ULL;
/* GPIO */
static const hrt_address GPIO_BASE[N_GPIO_ID] = {
	(hrt_address)0x0000000000000400ULL
};

/* TIMED_CTRL */
static const hrt_address TIMED_CTRL_BASE[N_TIMED_CTRL_ID] = {
	(hrt_address)0x0000000000000100ULL
};

/* INPUT_FORMATTER */
static const hrt_address INPUT_FORMATTER_BASE[N_INPUT_FORMATTER_ID] = {
	(hrt_address)0x0000000000030000ULL,
	(hrt_address)0x0000000000030200ULL,
	(hrt_address)0x0000000000030400ULL,
	(hrt_address)0x0000000000030600ULL
}; /* memcpy() */

/* INPUT_SYSTEM */
static const hrt_address INPUT_SYSTEM_BASE[N_INPUT_SYSTEM_ID] = {
	(hrt_address)0x0000000000080000ULL
};

/*	(hrt_address)0x0000000000081000ULL, */ /* capture A */
/*	(hrt_address)0x0000000000082000ULL, */ /* capture B */
/*	(hrt_address)0x0000000000083000ULL, */ /* capture C */
/*	(hrt_address)0x0000000000084000ULL, */ /* Acquisition */
/*	(hrt_address)0x0000000000085000ULL, */ /* DMA */
/*	(hrt_address)0x0000000000089000ULL, */ /* ctrl */
/*	(hrt_address)0x000000000008A000ULL, */ /* GP regs */
/*	(hrt_address)0x000000000008B000ULL, */ /* FIFO */
/*	(hrt_address)0x000000000008C000ULL, */ /* IRQ */

/* RX, the MIPI lane control regs start at offset 0 */
static const hrt_address RX_BASE[N_RX_ID] = {
	(hrt_address)0x0000000000080100ULL
};

#elif HRT_ADDRESS_WIDTH == 32

#define GP_FIFO_BASE   ((hrt_address)0x00090104)		/* This is NOT a base address */

/* DDR : Attention, this value not defined in 32-bit */
static const hrt_address DDR_BASE[N_DDR_ID] = {
	(hrt_address)0x00000000UL
};

/* ISP */
static const hrt_address ISP_CTRL_BASE[N_ISP_ID] = {
	(hrt_address)0x00020000UL
};

static const hrt_address ISP_DMEM_BASE[N_ISP_ID] = {
	(hrt_address)0x00200000UL
};

static const hrt_address ISP_BAMEM_BASE[N_BAMEM_ID] = {
	(hrt_address)0x100000UL
};

static const hrt_address ISP_VAMEM_BASE[N_VAMEM_ID] = {
	(hrt_address)0xffffffffUL,
	(hrt_address)0xffffffffUL,
	(hrt_address)0xffffffffUL
};

static const hrt_address ISP_HMEM_BASE[N_HMEM_ID] = {
	(hrt_address)0xffffffffUL
};

/* SP */
static const hrt_address SP_CTRL_BASE[N_SP_ID] = {
	(hrt_address)0x00010000UL
};

static const hrt_address SP_DMEM_BASE[N_SP_ID] = {
	(hrt_address)0x00300000UL
};

static const hrt_address SP_PMEM_BASE[N_SP_ID] = {
	(hrt_address)0x000B0000UL
};

/* MMU */
/*
 * MMU0_ID: The data MMU
 * MMU1_ID: The icache MMU
 */
static const hrt_address MMU_BASE[N_MMU_ID] = {
	(hrt_address)0x00070000UL,
	(hrt_address)0x000A0000UL
};

/* DMA */
static const hrt_address DMA_BASE[N_DMA_ID] = {
	(hrt_address)0x00040000UL
};

/* IRQ */
static const hrt_address IRQ_BASE[N_IRQ_ID] = {
	(hrt_address)0x00000500UL,
	(hrt_address)0x00030A00UL,
	(hrt_address)0x0008C000UL,
	(hrt_address)0x00090200UL
};

/*
	(hrt_address)0x00000500UL};
 */

/* GDC */
static const hrt_address GDC_BASE[N_GDC_ID] = {
	(hrt_address)0x00050000UL,
	(hrt_address)0x00060000UL
};

/* FIFO_MONITOR (not a subset of GP_DEVICE) */
static const hrt_address FIFO_MONITOR_BASE[N_FIFO_MONITOR_ID] = {
	(hrt_address)0x00000000UL
};

/*
static const hrt_address GP_REGS_BASE[N_GP_REGS_ID] = {
	(hrt_address)0x00000000UL};

static const hrt_address GP_DEVICE_BASE[N_GP_DEVICE_ID] = {
	(hrt_address)0x00090000UL};
*/

/* GP_DEVICE (single base for all separate GP_REG instances) */
static const hrt_address GP_DEVICE_BASE[N_GP_DEVICE_ID] = {
	(hrt_address)0x00000000UL
};

/*GP TIMER , all timer registers are inter-twined,
 * so, having multiple base addresses for
 * different timers does not help*/
static const hrt_address GP_TIMER_BASE =
    (hrt_address)0x00000600UL;

/* GPIO */
static const hrt_address GPIO_BASE[N_GPIO_ID] = {
	(hrt_address)0x00000400UL
};

/* TIMED_CTRL */
static const hrt_address TIMED_CTRL_BASE[N_TIMED_CTRL_ID] = {
	(hrt_address)0x00000100UL
};

/* INPUT_FORMATTER */
static const hrt_address INPUT_FORMATTER_BASE[N_INPUT_FORMATTER_ID] = {
	(hrt_address)0x00030000UL,
	(hrt_address)0x00030200UL,
	(hrt_address)0x00030400UL
};

/*	(hrt_address)0x00030600UL, */ /* memcpy() */

/* INPUT_SYSTEM */
static const hrt_address INPUT_SYSTEM_BASE[N_INPUT_SYSTEM_ID] = {
	(hrt_address)0x00080000UL
};

/*	(hrt_address)0x00081000UL, */ /* capture A */
/*	(hrt_address)0x00082000UL, */ /* capture B */
/*	(hrt_address)0x00083000UL, */ /* capture C */
/*	(hrt_address)0x00084000UL, */ /* Acquisition */
/*	(hrt_address)0x00085000UL, */ /* DMA */
/*	(hrt_address)0x00089000UL, */ /* ctrl */
/*	(hrt_address)0x0008A000UL, */ /* GP regs */
/*	(hrt_address)0x0008B000UL, */ /* FIFO */
/*	(hrt_address)0x0008C000UL, */ /* IRQ */

/* RX, the MIPI lane control regs start at offset 0 */
static const hrt_address RX_BASE[N_RX_ID] = {
	(hrt_address)0x00080100UL
};

#else
#error "system_local.h: HRT_ADDRESS_WIDTH must be one of {32,64}"
#endif

#endif /* __SYSTEM_LOCAL_H_INCLUDED__ */
