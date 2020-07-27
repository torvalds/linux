// SPDX-License-Identifier: GPL-2.0
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

#include "system_local.h"

/* ISP */
const hrt_address ISP_CTRL_BASE[N_ISP_ID] = {
	0x0000000000020000ULL
};

const hrt_address ISP_DMEM_BASE[N_ISP_ID] = {
	0x0000000000200000ULL
};

const hrt_address ISP_BAMEM_BASE[N_BAMEM_ID] = {
	0x0000000000100000ULL
};

/* SP */
const hrt_address SP_CTRL_BASE[N_SP_ID] = {
	0x0000000000010000ULL
};

const hrt_address SP_DMEM_BASE[N_SP_ID] = {
	0x0000000000300000ULL
};

/* MMU */
/*
 * MMU0_ID: The data MMU
 * MMU1_ID: The icache MMU
 */
const hrt_address MMU_BASE[N_MMU_ID] = {
	0x0000000000070000ULL,
	0x00000000000A0000ULL
};

/* DMA */
const hrt_address DMA_BASE[N_DMA_ID] = {
	0x0000000000040000ULL
};

const hrt_address ISYS2401_DMA_BASE[N_ISYS2401_DMA_ID] = {
	0x00000000000CA000ULL
};

/* IRQ */
const hrt_address IRQ_BASE[N_IRQ_ID] = {
	0x0000000000000500ULL,
	0x0000000000030A00ULL,
	0x000000000008C000ULL,
	0x0000000000090200ULL
};

/*
	0x0000000000000500ULL};
 */

/* GDC */
const hrt_address GDC_BASE[N_GDC_ID] = {
	0x0000000000050000ULL,
	0x0000000000060000ULL
};

/* FIFO_MONITOR (not a subset of GP_DEVICE) */
const hrt_address FIFO_MONITOR_BASE[N_FIFO_MONITOR_ID] = {
	0x0000000000000000ULL
};

/*
const hrt_address GP_REGS_BASE[N_GP_REGS_ID] = {
	0x0000000000000000ULL};

const hrt_address GP_DEVICE_BASE[N_GP_DEVICE_ID] = {
	0x0000000000090000ULL};
*/

/* GP_DEVICE (single base for all separate GP_REG instances) */
const hrt_address GP_DEVICE_BASE[N_GP_DEVICE_ID] = {
	0x0000000000000000ULL
};

/*GP TIMER , all timer registers are inter-twined,
 * so, having multiple base addresses for
 * different timers does not help*/
const hrt_address GP_TIMER_BASE =
    (hrt_address)0x0000000000000600ULL;

/* GPIO */
const hrt_address GPIO_BASE[N_GPIO_ID] = {
	0x0000000000000400ULL
};

/* TIMED_CTRL */
const hrt_address TIMED_CTRL_BASE[N_TIMED_CTRL_ID] = {
	0x0000000000000100ULL
};

/* INPUT_FORMATTER */
const hrt_address INPUT_FORMATTER_BASE[N_INPUT_FORMATTER_ID] = {
	0x0000000000030000ULL,
	0x0000000000030200ULL,
	0x0000000000030400ULL,
	0x0000000000030600ULL
}; /* memcpy() */

/* INPUT_SYSTEM */
const hrt_address INPUT_SYSTEM_BASE[N_INPUT_SYSTEM_ID] = {
	0x0000000000080000ULL
};

/*	0x0000000000081000ULL, */ /* capture A */
/*	0x0000000000082000ULL, */ /* capture B */
/*	0x0000000000083000ULL, */ /* capture C */
/*	0x0000000000084000ULL, */ /* Acquisition */
/*	0x0000000000085000ULL, */ /* DMA */
/*	0x0000000000089000ULL, */ /* ctrl */
/*	0x000000000008A000ULL, */ /* GP regs */
/*	0x000000000008B000ULL, */ /* FIFO */
/*	0x000000000008C000ULL, */ /* IRQ */

/* RX, the MIPI lane control regs start at offset 0 */
const hrt_address RX_BASE[N_RX_ID] = {
	0x0000000000080100ULL
};

/* IBUF_CTRL, part of the Input System 2401 */
const hrt_address IBUF_CTRL_BASE[N_IBUF_CTRL_ID] = {
	0x00000000000C1800ULL,	/* ibuf controller A */
	0x00000000000C3800ULL,	/* ibuf controller B */
	0x00000000000C5800ULL	/* ibuf controller C */
};

/* ISYS IRQ Controllers, part of the Input System 2401 */
const hrt_address ISYS_IRQ_BASE[N_ISYS_IRQ_ID] = {
	0x00000000000C1400ULL,	/* port a */
	0x00000000000C3400ULL,	/* port b */
	0x00000000000C5400ULL	/* port c */
};

/* CSI FE, part of the Input System 2401 */
const hrt_address CSI_RX_FE_CTRL_BASE[N_CSI_RX_FRONTEND_ID] = {
	0x00000000000C0400ULL,	/* csi fe controller A */
	0x00000000000C2400ULL,	/* csi fe controller B */
	0x00000000000C4400ULL	/* csi fe controller C */
};

/* CSI BE, part of the Input System 2401 */
const hrt_address CSI_RX_BE_CTRL_BASE[N_CSI_RX_BACKEND_ID] = {
	0x00000000000C0800ULL,	/* csi be controller A */
	0x00000000000C2800ULL,	/* csi be controller B */
	0x00000000000C4800ULL	/* csi be controller C */
};

/* PIXEL Generator, part of the Input System 2401 */
const hrt_address PIXELGEN_CTRL_BASE[N_PIXELGEN_ID] = {
	0x00000000000C1000ULL,	/* pixel gen controller A */
	0x00000000000C3000ULL,	/* pixel gen controller B */
	0x00000000000C5000ULL	/* pixel gen controller C */
};

/* Stream2MMIO, part of the Input System 2401 */
const hrt_address STREAM2MMIO_CTRL_BASE[N_STREAM2MMIO_ID] = {
	0x00000000000C0C00ULL,	/* stream2mmio controller A */
	0x00000000000C2C00ULL,	/* stream2mmio controller B */
	0x00000000000C4C00ULL	/* stream2mmio controller C */
};
