/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_DOS_REGS_H_
#define __MESON_VDEC_DOS_REGS_H_

/* DOS registers */
#define VDEC_ASSIST_AMR1_INT8	0x00b4

#define ASSIST_MBOX1_CLR_REG	0x01d4
#define ASSIST_MBOX1_MASK	0x01d8

#define MPSR			0x0c04
#define MCPU_INTR_MSK		0x0c10
#define CPSR			0x0c84

#define IMEM_DMA_CTRL		0x0d00
#define IMEM_DMA_ADR		0x0d04
#define IMEM_DMA_COUNT		0x0d08
#define LMEM_DMA_CTRL		0x0d40

#define MC_STATUS0		0x2424
#define MC_CTRL1		0x242c

#define PSCALE_RST		0x2440
#define PSCALE_CTRL		0x2444
#define PSCALE_BMEM_ADDR	0x247c
#define PSCALE_BMEM_DAT		0x2480

#define DBLK_CTRL		0x2544
#define DBLK_STATUS		0x254c

#define GCLK_EN			0x260c
#define MDEC_PIC_DC_CTRL	0x2638
#define MDEC_PIC_DC_STATUS	0x263c
#define ANC0_CANVAS_ADDR	0x2640
#define MDEC_PIC_DC_THRESH	0x26e0

/* Firmware interface registers */
#define AV_SCRATCH_0		0x2700
#define AV_SCRATCH_1		0x2704
#define AV_SCRATCH_2		0x2708
#define AV_SCRATCH_3		0x270c
#define AV_SCRATCH_4		0x2710
#define AV_SCRATCH_5		0x2714
#define AV_SCRATCH_6		0x2718
#define AV_SCRATCH_7		0x271c
#define AV_SCRATCH_8		0x2720
#define AV_SCRATCH_9		0x2724
#define AV_SCRATCH_A		0x2728
#define AV_SCRATCH_B		0x272c
#define AV_SCRATCH_C		0x2730
#define AV_SCRATCH_D		0x2734
#define AV_SCRATCH_E		0x2738
#define AV_SCRATCH_F		0x273c
#define AV_SCRATCH_G		0x2740
#define AV_SCRATCH_H		0x2744
#define AV_SCRATCH_I		0x2748
#define AV_SCRATCH_J		0x274c
#define AV_SCRATCH_K		0x2750
#define AV_SCRATCH_L		0x2754

#define MPEG1_2_REG		0x3004
#define PIC_HEAD_INFO		0x300c
#define POWER_CTL_VLD		0x3020
#define M4_CONTROL_REG		0x30a4

/* Stream Buffer (stbuf) regs */
#define VLD_MEM_VIFIFO_START_PTR	0x3100
#define VLD_MEM_VIFIFO_CURR_PTR	0x3104
#define VLD_MEM_VIFIFO_END_PTR	0x3108
#define VLD_MEM_VIFIFO_CONTROL	0x3110
	#define MEM_FIFO_CNT_BIT	16
	#define MEM_FILL_ON_LEVEL	BIT(10)
	#define MEM_CTRL_EMPTY_EN	BIT(2)
	#define MEM_CTRL_FILL_EN	BIT(1)
#define VLD_MEM_VIFIFO_WP	0x3114
#define VLD_MEM_VIFIFO_RP	0x3118
#define VLD_MEM_VIFIFO_LEVEL	0x311c
#define VLD_MEM_VIFIFO_BUF_CNTL	0x3120
	#define MEM_BUFCTRL_MANUAL	BIT(1)
#define VLD_MEM_VIFIFO_WRAP_COUNT	0x3144

#define DCAC_DMA_CTRL		0x3848

#define DOS_SW_RESET0		0xfc00
#define DOS_GCLK_EN0		0xfc04
#define DOS_GEN_CTRL0		0xfc08
#define DOS_MEM_PD_VDEC		0xfcc0
#define DOS_MEM_PD_HEVC		0xfccc
#define DOS_SW_RESET3		0xfcd0
#define DOS_GCLK_EN3		0xfcd4
#define DOS_VDEC_MCRCC_STALL_CTRL	0xfd00

#endif
