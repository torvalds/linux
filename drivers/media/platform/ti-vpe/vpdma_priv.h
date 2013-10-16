/*
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _TI_VPDMA_PRIV_H_
#define _TI_VPDMA_PRIV_H_

/*
 * VPDMA Register offsets
 */

/* Top level */
#define VPDMA_PID		0x00
#define VPDMA_LIST_ADDR		0x04
#define VPDMA_LIST_ATTR		0x08
#define VPDMA_LIST_STAT_SYNC	0x0c
#define VPDMA_BG_RGB		0x18
#define VPDMA_BG_YUV		0x1c
#define VPDMA_SETUP		0x30
#define VPDMA_MAX_SIZE1		0x34
#define VPDMA_MAX_SIZE2		0x38
#define VPDMA_MAX_SIZE3		0x3c

/* Interrupts */
#define VPDMA_INT_CHAN_STAT(grp)	(0x40 + grp * 8)
#define VPDMA_INT_CHAN_MASK(grp)	(VPDMA_INT_CHAN_STAT(grp) + 4)
#define VPDMA_INT_CLIENT0_STAT		0x78
#define VPDMA_INT_CLIENT0_MASK		0x7c
#define VPDMA_INT_CLIENT1_STAT		0x80
#define VPDMA_INT_CLIENT1_MASK		0x84
#define VPDMA_INT_LIST0_STAT		0x88
#define VPDMA_INT_LIST0_MASK		0x8c

#define VPDMA_PERFMON(i)		(0x200 + i * 4)

/* VPE specific client registers */
#define VPDMA_DEI_CHROMA1_CSTAT		0x0300
#define VPDMA_DEI_LUMA1_CSTAT		0x0304
#define VPDMA_DEI_LUMA2_CSTAT		0x0308
#define VPDMA_DEI_CHROMA2_CSTAT		0x030c
#define VPDMA_DEI_LUMA3_CSTAT		0x0310
#define VPDMA_DEI_CHROMA3_CSTAT		0x0314
#define VPDMA_DEI_MV_IN_CSTAT		0x0330
#define VPDMA_DEI_MV_OUT_CSTAT		0x033c
#define VPDMA_VIP_UP_Y_CSTAT		0x0390
#define VPDMA_VIP_UP_UV_CSTAT		0x0394
#define VPDMA_VPI_CTL_CSTAT		0x03d0

/* Reg field info for VPDMA_CLIENT_CSTAT registers */
#define VPDMA_CSTAT_LINE_MODE_MASK	0x03
#define VPDMA_CSTAT_LINE_MODE_SHIFT	8
#define VPDMA_CSTAT_FRAME_START_MASK	0xf
#define VPDMA_CSTAT_FRAME_START_SHIFT	10

#define VPDMA_LIST_NUM_MASK		0x07
#define VPDMA_LIST_NUM_SHFT		24
#define VPDMA_LIST_STOP_SHFT		20
#define VPDMA_LIST_RDY_MASK		0x01
#define VPDMA_LIST_RDY_SHFT		19
#define VPDMA_LIST_TYPE_MASK		0x03
#define VPDMA_LIST_TYPE_SHFT		16
#define VPDMA_LIST_SIZE_MASK		0xffff

/* VPDMA data type values for data formats */
#define DATA_TYPE_Y444				0x0
#define DATA_TYPE_Y422				0x1
#define DATA_TYPE_Y420				0x2
#define DATA_TYPE_C444				0x4
#define DATA_TYPE_C422				0x5
#define DATA_TYPE_C420				0x6
#define DATA_TYPE_YC422				0x7
#define DATA_TYPE_YC444				0x8
#define DATA_TYPE_CY422				0x23

#define DATA_TYPE_RGB16_565			0x0
#define DATA_TYPE_ARGB_1555			0x1
#define DATA_TYPE_ARGB_4444			0x2
#define DATA_TYPE_RGBA_5551			0x3
#define DATA_TYPE_RGBA_4444			0x4
#define DATA_TYPE_ARGB24_6666			0x5
#define DATA_TYPE_RGB24_888			0x6
#define DATA_TYPE_ARGB32_8888			0x7
#define DATA_TYPE_RGBA24_6666			0x8
#define DATA_TYPE_RGBA32_8888			0x9
#define DATA_TYPE_BGR16_565			0x10
#define DATA_TYPE_ABGR_1555			0x11
#define DATA_TYPE_ABGR_4444			0x12
#define DATA_TYPE_BGRA_5551			0x13
#define DATA_TYPE_BGRA_4444			0x14
#define DATA_TYPE_ABGR24_6666			0x15
#define DATA_TYPE_BGR24_888			0x16
#define DATA_TYPE_ABGR32_8888			0x17
#define DATA_TYPE_BGRA24_6666			0x18
#define DATA_TYPE_BGRA32_8888			0x19

#define DATA_TYPE_MV				0x3

/* VPDMA channel numbers(only VPE channels for now) */
#define	VPE_CHAN_NUM_LUMA1_IN		0
#define	VPE_CHAN_NUM_CHROMA1_IN		1
#define	VPE_CHAN_NUM_LUMA2_IN		2
#define	VPE_CHAN_NUM_CHROMA2_IN		3
#define	VPE_CHAN_NUM_LUMA3_IN		4
#define	VPE_CHAN_NUM_CHROMA3_IN		5
#define	VPE_CHAN_NUM_MV_IN		12
#define	VPE_CHAN_NUM_MV_OUT		15
#define	VPE_CHAN_NUM_LUMA_OUT		102
#define	VPE_CHAN_NUM_CHROMA_OUT		103
#define	VPE_CHAN_NUM_RGB_OUT		106

#endif
