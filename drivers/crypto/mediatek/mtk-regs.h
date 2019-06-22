/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Support for MediaTek cryptographic accelerator.
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 */

#ifndef __MTK_REGS_H__
#define __MTK_REGS_H__

/* HIA, Command Descriptor Ring Manager */
#define CDR_BASE_ADDR_LO(x)		(0x0 + ((x) << 12))
#define CDR_BASE_ADDR_HI(x)		(0x4 + ((x) << 12))
#define CDR_DATA_BASE_ADDR_LO(x)	(0x8 + ((x) << 12))
#define CDR_DATA_BASE_ADDR_HI(x)	(0xC + ((x) << 12))
#define CDR_ACD_BASE_ADDR_LO(x)		(0x10 + ((x) << 12))
#define CDR_ACD_BASE_ADDR_HI(x)		(0x14 + ((x) << 12))
#define CDR_RING_SIZE(x)		(0x18 + ((x) << 12))
#define CDR_DESC_SIZE(x)		(0x1C + ((x) << 12))
#define CDR_CFG(x)			(0x20 + ((x) << 12))
#define CDR_DMA_CFG(x)			(0x24 + ((x) << 12))
#define CDR_THRESH(x)			(0x28 + ((x) << 12))
#define CDR_PREP_COUNT(x)		(0x2C + ((x) << 12))
#define CDR_PROC_COUNT(x)		(0x30 + ((x) << 12))
#define CDR_PREP_PNTR(x)		(0x34 + ((x) << 12))
#define CDR_PROC_PNTR(x)		(0x38 + ((x) << 12))
#define CDR_STAT(x)			(0x3C + ((x) << 12))

/* HIA, Result Descriptor Ring Manager */
#define RDR_BASE_ADDR_LO(x)		(0x800 + ((x) << 12))
#define RDR_BASE_ADDR_HI(x)		(0x804 + ((x) << 12))
#define RDR_DATA_BASE_ADDR_LO(x)	(0x808 + ((x) << 12))
#define RDR_DATA_BASE_ADDR_HI(x)	(0x80C + ((x) << 12))
#define RDR_ACD_BASE_ADDR_LO(x)		(0x810 + ((x) << 12))
#define RDR_ACD_BASE_ADDR_HI(x)		(0x814 + ((x) << 12))
#define RDR_RING_SIZE(x)		(0x818 + ((x) << 12))
#define RDR_DESC_SIZE(x)		(0x81C + ((x) << 12))
#define RDR_CFG(x)			(0x820 + ((x) << 12))
#define RDR_DMA_CFG(x)			(0x824 + ((x) << 12))
#define RDR_THRESH(x)			(0x828 + ((x) << 12))
#define RDR_PREP_COUNT(x)		(0x82C + ((x) << 12))
#define RDR_PROC_COUNT(x)		(0x830 + ((x) << 12))
#define RDR_PREP_PNTR(x)		(0x834 + ((x) << 12))
#define RDR_PROC_PNTR(x)		(0x838 + ((x) << 12))
#define RDR_STAT(x)			(0x83C + ((x) << 12))

/* HIA, Ring AIC */
#define AIC_POL_CTRL(x)			(0xE000 - ((x) << 12))
#define	AIC_TYPE_CTRL(x)		(0xE004 - ((x) << 12))
#define	AIC_ENABLE_CTRL(x)		(0xE008 - ((x) << 12))
#define	AIC_RAW_STAL(x)			(0xE00C - ((x) << 12))
#define	AIC_ENABLE_SET(x)		(0xE00C - ((x) << 12))
#define	AIC_ENABLED_STAT(x)		(0xE010 - ((x) << 12))
#define	AIC_ACK(x)			(0xE010 - ((x) << 12))
#define	AIC_ENABLE_CLR(x)		(0xE014 - ((x) << 12))
#define	AIC_OPTIONS(x)			(0xE018 - ((x) << 12))
#define	AIC_VERSION(x)			(0xE01C - ((x) << 12))

/* HIA, Global AIC */
#define AIC_G_POL_CTRL			0xF800
#define AIC_G_TYPE_CTRL			0xF804
#define AIC_G_ENABLE_CTRL		0xF808
#define AIC_G_RAW_STAT			0xF80C
#define AIC_G_ENABLE_SET		0xF80C
#define AIC_G_ENABLED_STAT		0xF810
#define AIC_G_ACK			0xF810
#define AIC_G_ENABLE_CLR		0xF814
#define AIC_G_OPTIONS			0xF818
#define AIC_G_VERSION			0xF81C

/* HIA, Data Fetch Engine */
#define DFE_CFG				0xF000
#define DFE_PRIO_0			0xF010
#define DFE_PRIO_1			0xF014
#define DFE_PRIO_2			0xF018
#define DFE_PRIO_3			0xF01C

/* HIA, Data Fetch Engine access monitoring for CDR */
#define DFE_RING_REGION_LO(x)		(0xF080 + ((x) << 3))
#define DFE_RING_REGION_HI(x)		(0xF084 + ((x) << 3))

/* HIA, Data Fetch Engine thread control and status for thread */
#define DFE_THR_CTRL			0xF200
#define DFE_THR_STAT			0xF204
#define DFE_THR_DESC_CTRL		0xF208
#define DFE_THR_DESC_DPTR_LO		0xF210
#define DFE_THR_DESC_DPTR_HI		0xF214
#define DFE_THR_DESC_ACDPTR_LO		0xF218
#define DFE_THR_DESC_ACDPTR_HI		0xF21C

/* HIA, Data Store Engine */
#define DSE_CFG				0xF400
#define DSE_PRIO_0			0xF410
#define DSE_PRIO_1			0xF414
#define DSE_PRIO_2			0xF418
#define DSE_PRIO_3			0xF41C

/* HIA, Data Store Engine access monitoring for RDR */
#define DSE_RING_REGION_LO(x)		(0xF480 + ((x) << 3))
#define DSE_RING_REGION_HI(x)		(0xF484 + ((x) << 3))

/* HIA, Data Store Engine thread control and status for thread */
#define DSE_THR_CTRL			0xF600
#define DSE_THR_STAT			0xF604
#define DSE_THR_DESC_CTRL		0xF608
#define DSE_THR_DESC_DPTR_LO		0xF610
#define DSE_THR_DESC_DPTR_HI		0xF614
#define DSE_THR_DESC_S_DPTR_LO		0xF618
#define DSE_THR_DESC_S_DPTR_HI		0xF61C
#define DSE_THR_ERROR_STAT		0xF620

/* HIA Global */
#define HIA_MST_CTRL			0xFFF4
#define HIA_OPTIONS			0xFFF8
#define HIA_VERSION			0xFFFC

/* Processing Engine Input Side, Processing Engine */
#define PE_IN_DBUF_THRESH		0x10000
#define PE_IN_TBUF_THRESH		0x10100

/* Packet Engine Configuration / Status Registers */
#define PE_TOKEN_CTRL_STAT		0x11000
#define PE_FUNCTION_EN			0x11004
#define PE_CONTEXT_CTRL			0x11008
#define PE_INTERRUPT_CTRL_STAT		0x11010
#define PE_CONTEXT_STAT			0x1100C
#define PE_OUT_TRANS_CTRL_STAT		0x11018
#define PE_OUT_BUF_CTRL			0x1101C

/* Packet Engine PRNG Registers */
#define PE_PRNG_STAT			0x11040
#define PE_PRNG_CTRL			0x11044
#define PE_PRNG_SEED_L			0x11048
#define PE_PRNG_SEED_H			0x1104C
#define PE_PRNG_KEY_0_L			0x11050
#define PE_PRNG_KEY_0_H			0x11054
#define PE_PRNG_KEY_1_L			0x11058
#define PE_PRNG_KEY_1_H			0x1105C
#define PE_PRNG_RES_0			0x11060
#define PE_PRNG_RES_1			0x11064
#define PE_PRNG_RES_2			0x11068
#define PE_PRNG_RES_3			0x1106C
#define PE_PRNG_LFSR_L			0x11070
#define PE_PRNG_LFSR_H			0x11074

/* Packet Engine AIC */
#define PE_EIP96_AIC_POL_CTRL		0x113C0
#define PE_EIP96_AIC_TYPE_CTRL		0x113C4
#define PE_EIP96_AIC_ENABLE_CTRL	0x113C8
#define PE_EIP96_AIC_RAW_STAT		0x113CC
#define PE_EIP96_AIC_ENABLE_SET		0x113CC
#define PE_EIP96_AIC_ENABLED_STAT	0x113D0
#define PE_EIP96_AIC_ACK		0x113D0
#define PE_EIP96_AIC_ENABLE_CLR		0x113D4
#define PE_EIP96_AIC_OPTIONS		0x113D8
#define PE_EIP96_AIC_VERSION		0x113DC

/* Packet Engine Options & Version Registers */
#define PE_EIP96_OPTIONS		0x113F8
#define PE_EIP96_VERSION		0x113FC

/* Processing Engine Output Side */
#define PE_OUT_DBUF_THRESH		0x11C00
#define PE_OUT_TBUF_THRESH		0x11D00

/* Processing Engine Local AIC */
#define PE_AIC_POL_CTRL			0x11F00
#define PE_AIC_TYPE_CTRL		0x11F04
#define PE_AIC_ENABLE_CTRL		0x11F08
#define PE_AIC_RAW_STAT			0x11F0C
#define PE_AIC_ENABLE_SET		0x11F0C
#define PE_AIC_ENABLED_STAT		0x11F10
#define PE_AIC_ENABLE_CLR		0x11F14
#define PE_AIC_OPTIONS			0x11F18
#define PE_AIC_VERSION			0x11F1C

/* Processing Engine General Configuration and Version */
#define PE_IN_FLIGHT			0x11FF0
#define PE_OPTIONS			0x11FF8
#define PE_VERSION			0x11FFC

/* EIP-97 - Global */
#define EIP97_CLOCK_STATE		0x1FFE4
#define EIP97_FORCE_CLOCK_ON		0x1FFE8
#define EIP97_FORCE_CLOCK_OFF		0x1FFEC
#define EIP97_MST_CTRL			0x1FFF4
#define EIP97_OPTIONS			0x1FFF8
#define EIP97_VERSION			0x1FFFC
#endif /* __MTK_REGS_H__ */
