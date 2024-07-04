/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Intel Corporation */

#ifndef __IPU3_CIO2_H
#define __IPU3_CIO2_H

#include <linux/bits.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <asm/page.h>

#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>

struct cio2_fbpt_entry;		/* defined here, after the first usage */
struct pci_dev;

#define CIO2_NAME					"ipu3-cio2"
#define CIO2_DEVICE_NAME				"Intel IPU3 CIO2"
#define CIO2_ENTITY_NAME				"ipu3-csi2"
#define CIO2_PCI_ID					0x9d32
#define CIO2_PCI_BAR					0
#define CIO2_DMA_MASK					DMA_BIT_MASK(39)

#define CIO2_IMAGE_MAX_WIDTH				4224U
#define CIO2_IMAGE_MAX_HEIGHT				3136U

/* 32MB = 8xFBPT_entry */
#define CIO2_MAX_LOPS					8
#define CIO2_MAX_BUFFERS			(PAGE_SIZE / 16 / CIO2_MAX_LOPS)
#define CIO2_LOP_ENTRIES			(PAGE_SIZE / sizeof(u32))

#define CIO2_PAD_SINK					0U
#define CIO2_PAD_SOURCE					1U
#define CIO2_PADS					2U

#define CIO2_NUM_DMA_CHAN				20U
#define CIO2_NUM_PORTS					4U /* DPHYs */

/* 1 for each sensor */
#define CIO2_QUEUES					CIO2_NUM_PORTS

/* Register and bit field definitions */
#define CIO2_REG_PIPE_BASE(n)			((n) * 0x0400)	/* n = 0..3 */
#define CIO2_REG_CSIRX_BASE				0x000
#define CIO2_REG_MIPIBE_BASE				0x100
#define CIO2_REG_PIXELGEN_BAS				0x200
#define CIO2_REG_IRQCTRL_BASE				0x300
#define CIO2_REG_GPREG_BASE				0x1000

/* base register: CIO2_REG_PIPE_BASE(pipe) * CIO2_REG_CSIRX_BASE */
#define CIO2_REG_CSIRX_ENABLE			(CIO2_REG_CSIRX_BASE + 0x0)
#define CIO2_REG_CSIRX_NOF_ENABLED_LANES	(CIO2_REG_CSIRX_BASE + 0x4)
#define CIO2_REG_CSIRX_SP_IF_CONFIG		(CIO2_REG_CSIRX_BASE + 0x10)
#define CIO2_REG_CSIRX_LP_IF_CONFIG		(CIO2_REG_CSIRX_BASE + 0x14)
#define CIO2_CSIRX_IF_CONFIG_FILTEROUT			0x00
#define CIO2_CSIRX_IF_CONFIG_FILTEROUT_VC_INACTIVE	0x01
#define CIO2_CSIRX_IF_CONFIG_PASS			0x02
#define CIO2_CSIRX_IF_CONFIG_FLAG_ERROR			BIT(2)
#define CIO2_REG_CSIRX_STATUS			(CIO2_REG_CSIRX_BASE + 0x18)
#define CIO2_REG_CSIRX_STATUS_DLANE_HS		(CIO2_REG_CSIRX_BASE + 0x1c)
#define CIO2_CSIRX_STATUS_DLANE_HS_MASK			0xff
#define CIO2_REG_CSIRX_STATUS_DLANE_LP		(CIO2_REG_CSIRX_BASE + 0x20)
#define CIO2_CSIRX_STATUS_DLANE_LP_MASK			0xffffff
/* Termination enable and settle in 0.0625ns units, lane=0..3 or -1 for clock */
#define CIO2_REG_CSIRX_DLY_CNT_TERMEN(lane) \
				(CIO2_REG_CSIRX_BASE + 0x2c + 8 * (lane))
#define CIO2_REG_CSIRX_DLY_CNT_SETTLE(lane) \
				(CIO2_REG_CSIRX_BASE + 0x30 + 8 * (lane))
/* base register: CIO2_REG_PIPE_BASE(pipe) * CIO2_REG_MIPIBE_BASE */
#define CIO2_REG_MIPIBE_ENABLE		(CIO2_REG_MIPIBE_BASE + 0x0)
#define CIO2_REG_MIPIBE_STATUS		(CIO2_REG_MIPIBE_BASE + 0x4)
#define CIO2_REG_MIPIBE_COMP_FORMAT(vc) \
				(CIO2_REG_MIPIBE_BASE + 0x8 + 0x4 * (vc))
#define CIO2_REG_MIPIBE_FORCE_RAW8	(CIO2_REG_MIPIBE_BASE + 0x20)
#define CIO2_REG_MIPIBE_FORCE_RAW8_ENABLE		BIT(0)
#define CIO2_REG_MIPIBE_FORCE_RAW8_USE_TYPEID		BIT(1)
#define CIO2_REG_MIPIBE_FORCE_RAW8_TYPEID_SHIFT		2U

#define CIO2_REG_MIPIBE_IRQ_STATUS	(CIO2_REG_MIPIBE_BASE + 0x24)
#define CIO2_REG_MIPIBE_IRQ_CLEAR	(CIO2_REG_MIPIBE_BASE + 0x28)
#define CIO2_REG_MIPIBE_GLOBAL_LUT_DISREGARD (CIO2_REG_MIPIBE_BASE + 0x68)
#define CIO2_MIPIBE_GLOBAL_LUT_DISREGARD		1U
#define CIO2_REG_MIPIBE_PKT_STALL_STATUS (CIO2_REG_MIPIBE_BASE + 0x6c)
#define CIO2_REG_MIPIBE_PARSE_GSP_THROUGH_LP_LUT_REG_IDX \
					(CIO2_REG_MIPIBE_BASE + 0x70)
#define CIO2_REG_MIPIBE_SP_LUT_ENTRY(vc) \
				       (CIO2_REG_MIPIBE_BASE + 0x74 + 4 * (vc))
#define CIO2_REG_MIPIBE_LP_LUT_ENTRY(m)	/* m = 0..15 */ \
					(CIO2_REG_MIPIBE_BASE + 0x84 + 4 * (m))
#define CIO2_MIPIBE_LP_LUT_ENTRY_DISREGARD		1U
#define CIO2_MIPIBE_LP_LUT_ENTRY_SID_SHIFT		1U
#define CIO2_MIPIBE_LP_LUT_ENTRY_VC_SHIFT		5U
#define CIO2_MIPIBE_LP_LUT_ENTRY_FORMAT_TYPE_SHIFT	7U

/* base register: CIO2_REG_PIPE_BASE(pipe) * CIO2_REG_IRQCTRL_BASE */
/* IRQ registers are 18-bit wide, see cio2_irq_error for bit definitions */
#define CIO2_REG_IRQCTRL_EDGE		(CIO2_REG_IRQCTRL_BASE + 0x00)
#define CIO2_REG_IRQCTRL_MASK		(CIO2_REG_IRQCTRL_BASE + 0x04)
#define CIO2_REG_IRQCTRL_STATUS		(CIO2_REG_IRQCTRL_BASE + 0x08)
#define CIO2_REG_IRQCTRL_CLEAR		(CIO2_REG_IRQCTRL_BASE + 0x0c)
#define CIO2_REG_IRQCTRL_ENABLE		(CIO2_REG_IRQCTRL_BASE + 0x10)
#define CIO2_REG_IRQCTRL_LEVEL_NOT_PULSE	(CIO2_REG_IRQCTRL_BASE + 0x14)

#define CIO2_REG_GPREG_SRST		(CIO2_REG_GPREG_BASE + 0x0)
#define CIO2_GPREG_SRST_ALL				0xffff	/* Reset all */
#define CIO2_REG_FB_HPLL_FREQ		(CIO2_REG_GPREG_BASE + 0x08)
#define CIO2_REG_ISCLK_RATIO		(CIO2_REG_GPREG_BASE + 0xc)

#define CIO2_REG_CGC					0x1400
#define CIO2_CGC_CSI2_TGE				BIT(0)
#define CIO2_CGC_PRIM_TGE				BIT(1)
#define CIO2_CGC_SIDE_TGE				BIT(2)
#define CIO2_CGC_XOSC_TGE				BIT(3)
#define CIO2_CGC_MPLL_SHUTDOWN_EN			BIT(4)
#define CIO2_CGC_D3I3_TGE				BIT(5)
#define CIO2_CGC_CSI2_INTERFRAME_TGE			BIT(6)
#define CIO2_CGC_CSI2_PORT_DCGE				BIT(8)
#define CIO2_CGC_CSI2_DCGE				BIT(9)
#define CIO2_CGC_SIDE_DCGE				BIT(10)
#define CIO2_CGC_PRIM_DCGE				BIT(11)
#define CIO2_CGC_ROSC_DCGE				BIT(12)
#define CIO2_CGC_XOSC_DCGE				BIT(13)
#define CIO2_CGC_FLIS_DCGE				BIT(14)
#define CIO2_CGC_CLKGATE_HOLDOFF_SHIFT			20U
#define CIO2_CGC_CSI_CLKGATE_HOLDOFF_SHIFT		24U
#define CIO2_REG_D0I3C					0x1408
#define CIO2_D0I3C_I3					BIT(2)	/* Set D0I3 */
#define CIO2_D0I3C_RR					BIT(3)	/* Restore? */
#define CIO2_REG_SWRESET				0x140c
#define CIO2_SWRESET_SWRESET				1U
#define CIO2_REG_SENSOR_ACTIVE				0x1410
#define CIO2_REG_INT_STS				0x1414
#define CIO2_REG_INT_STS_EXT_OE				0x1418
#define CIO2_INT_EXT_OE_DMAOE_SHIFT			0U
#define CIO2_INT_EXT_OE_DMAOE_MASK			0x7ffff
#define CIO2_INT_EXT_OE_OES_SHIFT			24U
#define CIO2_INT_EXT_OE_OES_MASK	(0xf << CIO2_INT_EXT_OE_OES_SHIFT)
#define CIO2_REG_INT_EN					0x1420
#define CIO2_REG_INT_EN_IRQ				(1 << 24)
#define CIO2_REG_INT_EN_IOS(dma)	(1U << (((dma) >> 1U) + 12U))
/*
 * Interrupt on completion bit, Eg. DMA 0-3 maps to bit 0-3,
 * DMA4 & DMA5 map to bit 4 ... DMA18 & DMA19 map to bit 11 Et cetera
 */
#define CIO2_INT_IOC(dma)	(1U << ((dma) < 4U ? (dma) : ((dma) >> 1U) + 2U))
#define CIO2_INT_IOC_SHIFT				0
#define CIO2_INT_IOC_MASK		(0x7ff << CIO2_INT_IOC_SHIFT)
#define CIO2_INT_IOS_IOLN(dma)		(1U << (((dma) >> 1U) + 12U))
#define CIO2_INT_IOS_IOLN_SHIFT				12
#define CIO2_INT_IOS_IOLN_MASK		(0x3ff << CIO2_INT_IOS_IOLN_SHIFT)
#define CIO2_INT_IOIE					BIT(22)
#define CIO2_INT_IOOE					BIT(23)
#define CIO2_INT_IOIRQ					BIT(24)
#define CIO2_REG_INT_EN_EXT_OE				0x1424
#define CIO2_REG_DMA_DBG				0x1448
#define CIO2_REG_DMA_DBG_DMA_INDEX_SHIFT		0U
#define CIO2_REG_PBM_ARB_CTRL				0x1460
#define CIO2_PBM_ARB_CTRL_LANES_DIV			0U /* 4-4-2-2 lanes */
#define CIO2_PBM_ARB_CTRL_LANES_DIV_SHIFT		0U
#define CIO2_PBM_ARB_CTRL_LE_EN				BIT(7)
#define CIO2_PBM_ARB_CTRL_PLL_POST_SHTDN		2U
#define CIO2_PBM_ARB_CTRL_PLL_POST_SHTDN_SHIFT		8U
#define CIO2_PBM_ARB_CTRL_PLL_AHD_WK_UP			480U
#define CIO2_PBM_ARB_CTRL_PLL_AHD_WK_UP_SHIFT		16U
#define CIO2_REG_PBM_WMCTRL1				0x1464
#define CIO2_PBM_WMCTRL1_MIN_2CK_SHIFT			0U
#define CIO2_PBM_WMCTRL1_MID1_2CK_SHIFT			8U
#define CIO2_PBM_WMCTRL1_MID2_2CK_SHIFT			16U
#define CIO2_PBM_WMCTRL1_TS_COUNT_DISABLE		BIT(31)
#define CIO2_PBM_WMCTRL1_MIN_2CK	(4 << CIO2_PBM_WMCTRL1_MIN_2CK_SHIFT)
#define CIO2_PBM_WMCTRL1_MID1_2CK	(16 << CIO2_PBM_WMCTRL1_MID1_2CK_SHIFT)
#define CIO2_PBM_WMCTRL1_MID2_2CK	(21 << CIO2_PBM_WMCTRL1_MID2_2CK_SHIFT)
#define CIO2_REG_PBM_WMCTRL2				0x1468
#define CIO2_PBM_WMCTRL2_HWM_2CK			40U
#define CIO2_PBM_WMCTRL2_HWM_2CK_SHIFT			0U
#define CIO2_PBM_WMCTRL2_LWM_2CK			22U
#define CIO2_PBM_WMCTRL2_LWM_2CK_SHIFT			8U
#define CIO2_PBM_WMCTRL2_OBFFWM_2CK			2U
#define CIO2_PBM_WMCTRL2_OBFFWM_2CK_SHIFT		16U
#define CIO2_PBM_WMCTRL2_TRANSDYN			1U
#define CIO2_PBM_WMCTRL2_TRANSDYN_SHIFT			24U
#define CIO2_PBM_WMCTRL2_DYNWMEN			BIT(28)
#define CIO2_PBM_WMCTRL2_OBFF_MEM_EN			BIT(29)
#define CIO2_PBM_WMCTRL2_OBFF_CPU_EN			BIT(30)
#define CIO2_PBM_WMCTRL2_DRAINNOW			BIT(31)
#define CIO2_REG_PBM_TS_COUNT				0x146c
#define CIO2_REG_PBM_FOPN_ABORT				0x1474
/* below n = 0..3 */
#define CIO2_PBM_FOPN_ABORT(n)				(0x1 << 8U * (n))
#define CIO2_PBM_FOPN_FORCE_ABORT(n)			(0x2 << 8U * (n))
#define CIO2_PBM_FOPN_FRAMEOPEN(n)			(0x8 << 8U * (n))
#define CIO2_REG_LTRCTRL				0x1480
#define CIO2_LTRCTRL_LTRDYNEN				BIT(16)
#define CIO2_LTRCTRL_LTRSTABLETIME_SHIFT		8U
#define CIO2_LTRCTRL_LTRSTABLETIME_MASK			0xff
#define CIO2_LTRCTRL_LTRSEL1S3				BIT(7)
#define CIO2_LTRCTRL_LTRSEL1S2				BIT(6)
#define CIO2_LTRCTRL_LTRSEL1S1				BIT(5)
#define CIO2_LTRCTRL_LTRSEL1S0				BIT(4)
#define CIO2_LTRCTRL_LTRSEL2S3				BIT(3)
#define CIO2_LTRCTRL_LTRSEL2S2				BIT(2)
#define CIO2_LTRCTRL_LTRSEL2S1				BIT(1)
#define CIO2_LTRCTRL_LTRSEL2S0				BIT(0)
#define CIO2_REG_LTRVAL23				0x1484
#define CIO2_REG_LTRVAL01				0x1488
#define CIO2_LTRVAL02_VAL_SHIFT				0U
#define CIO2_LTRVAL02_SCALE_SHIFT			10U
#define CIO2_LTRVAL13_VAL_SHIFT				16U
#define CIO2_LTRVAL13_SCALE_SHIFT			26U

#define CIO2_LTRVAL0_VAL				175U
/* Value times 1024 ns */
#define CIO2_LTRVAL0_SCALE				2U
#define CIO2_LTRVAL1_VAL				90U
#define CIO2_LTRVAL1_SCALE				2U
#define CIO2_LTRVAL2_VAL				90U
#define CIO2_LTRVAL2_SCALE				2U
#define CIO2_LTRVAL3_VAL				90U
#define CIO2_LTRVAL3_SCALE				2U

#define CIO2_REG_CDMABA(n)		(0x1500 + 0x10 * (n))	/* n = 0..19 */
#define CIO2_REG_CDMARI(n)		(0x1504 + 0x10 * (n))
#define CIO2_CDMARI_FBPT_RP_SHIFT			0U
#define CIO2_CDMARI_FBPT_RP_MASK			0xff
#define CIO2_REG_CDMAC0(n)		(0x1508 + 0x10 * (n))
#define CIO2_CDMAC0_FBPT_LEN_SHIFT			0U
#define CIO2_CDMAC0_FBPT_WIDTH_SHIFT			8U
#define CIO2_CDMAC0_FBPT_NS				BIT(25)
#define CIO2_CDMAC0_DMA_INTR_ON_FS			BIT(26)
#define CIO2_CDMAC0_DMA_INTR_ON_FE			BIT(27)
#define CIO2_CDMAC0_FBPT_UPDATE_FIFO_FULL		BIT(28)
#define CIO2_CDMAC0_FBPT_FIFO_FULL_FIX_DIS		BIT(29)
#define CIO2_CDMAC0_DMA_EN				BIT(30)
#define CIO2_CDMAC0_DMA_HALTED				BIT(31)
#define CIO2_REG_CDMAC1(n)		(0x150c + 0x10 * (n))
#define CIO2_CDMAC1_LINENUMINT_SHIFT			0U
#define CIO2_CDMAC1_LINENUMUPDATE_SHIFT			16U
/* n = 0..3 */
#define CIO2_REG_PXM_PXF_FMT_CFG0(n)	(0x1700 + 0x30 * (n))
#define CIO2_PXM_PXF_FMT_CFG_SID0_SHIFT			0U
#define CIO2_PXM_PXF_FMT_CFG_SID1_SHIFT			16U
#define CIO2_PXM_PXF_FMT_CFG_PCK_64B			(0 << 0)
#define CIO2_PXM_PXF_FMT_CFG_PCK_32B			(1 << 0)
#define CIO2_PXM_PXF_FMT_CFG_BPP_08			(0 << 2)
#define CIO2_PXM_PXF_FMT_CFG_BPP_10			(1 << 2)
#define CIO2_PXM_PXF_FMT_CFG_BPP_12			(2 << 2)
#define CIO2_PXM_PXF_FMT_CFG_BPP_14			(3 << 2)
#define CIO2_PXM_PXF_FMT_CFG_SPEC_4PPC			(0 << 4)
#define CIO2_PXM_PXF_FMT_CFG_SPEC_3PPC_RGBA		(1 << 4)
#define CIO2_PXM_PXF_FMT_CFG_SPEC_3PPC_ARGB		(2 << 4)
#define CIO2_PXM_PXF_FMT_CFG_SPEC_PLANAR2		(3 << 4)
#define CIO2_PXM_PXF_FMT_CFG_SPEC_PLANAR3		(4 << 4)
#define CIO2_PXM_PXF_FMT_CFG_SPEC_NV16			(5 << 4)
#define CIO2_PXM_PXF_FMT_CFG_PSWAP4_1ST_AB		(1 << 7)
#define CIO2_PXM_PXF_FMT_CFG_PSWAP4_1ST_CD		(1 << 8)
#define CIO2_PXM_PXF_FMT_CFG_PSWAP4_2ND_AC		(1 << 9)
#define CIO2_PXM_PXF_FMT_CFG_PSWAP4_2ND_BD		(1 << 10)
#define CIO2_REG_INT_STS_EXT_IE				0x17e4
#define CIO2_REG_INT_EN_EXT_IE				0x17e8
#define CIO2_INT_EXT_IE_ECC_RE(n)			(0x01 << (8U * (n)))
#define CIO2_INT_EXT_IE_DPHY_NR(n)			(0x02 << (8U * (n)))
#define CIO2_INT_EXT_IE_ECC_NR(n)			(0x04 << (8U * (n)))
#define CIO2_INT_EXT_IE_CRCERR(n)			(0x08 << (8U * (n)))
#define CIO2_INT_EXT_IE_INTERFRAMEDATA(n)		(0x10 << (8U * (n)))
#define CIO2_INT_EXT_IE_PKT2SHORT(n)			(0x20 << (8U * (n)))
#define CIO2_INT_EXT_IE_PKT2LONG(n)			(0x40 << (8U * (n)))
#define CIO2_INT_EXT_IE_IRQ(n)				(0x80 << (8U * (n)))
#define CIO2_REG_PXM_FRF_CFG(n)				(0x1720 + 0x30 * (n))
#define CIO2_PXM_FRF_CFG_FNSEL				BIT(0)
#define CIO2_PXM_FRF_CFG_FN_RST				BIT(1)
#define CIO2_PXM_FRF_CFG_ABORT				BIT(2)
#define CIO2_PXM_FRF_CFG_CRC_TH_SHIFT			3U
#define CIO2_PXM_FRF_CFG_MSK_ECC_DPHY_NR		BIT(8)
#define CIO2_PXM_FRF_CFG_MSK_ECC_RE			BIT(9)
#define CIO2_PXM_FRF_CFG_MSK_ECC_DPHY_NE		BIT(10)
#define CIO2_PXM_FRF_CFG_EVEN_ODD_MODE_SHIFT		11U
#define CIO2_PXM_FRF_CFG_MASK_CRC_THRES			BIT(13)
#define CIO2_PXM_FRF_CFG_MASK_CSI_ACCEPT		BIT(14)
#define CIO2_PXM_FRF_CFG_CIOHC_FS_MODE			BIT(15)
#define CIO2_PXM_FRF_CFG_CIOHC_FRST_FRM_SHIFT		16U
#define CIO2_REG_PXM_SID2BID0(n)			(0x1724 + 0x30 * (n))
#define CIO2_FB_HPLL_FREQ				0x2
#define CIO2_ISCLK_RATIO				0xc

#define CIO2_IRQCTRL_MASK				0x3ffff

#define CIO2_INT_EN_EXT_OE_MASK				0x8f0fffff

#define CIO2_CGC_CLKGATE_HOLDOFF			3U
#define CIO2_CGC_CSI_CLKGATE_HOLDOFF			5U

#define CIO2_PXM_FRF_CFG_CRC_TH				16

#define CIO2_INT_EN_EXT_IE_MASK				0xffffffff

#define CIO2_DMA_CHAN					0U

#define CIO2_CSIRX_DLY_CNT_CLANE_IDX			-1

#define CIO2_CSIRX_DLY_CNT_TERMEN_CLANE_A		0
#define CIO2_CSIRX_DLY_CNT_TERMEN_CLANE_B		0
#define CIO2_CSIRX_DLY_CNT_SETTLE_CLANE_A		95
#define CIO2_CSIRX_DLY_CNT_SETTLE_CLANE_B		-8

#define CIO2_CSIRX_DLY_CNT_TERMEN_DLANE_A		0
#define CIO2_CSIRX_DLY_CNT_TERMEN_DLANE_B		0
#define CIO2_CSIRX_DLY_CNT_SETTLE_DLANE_A		85
#define CIO2_CSIRX_DLY_CNT_SETTLE_DLANE_B		-2

#define CIO2_CSIRX_DLY_CNT_TERMEN_DEFAULT		0x4
#define CIO2_CSIRX_DLY_CNT_SETTLE_DEFAULT		0x570

struct cio2_csi2_timing {
	s32 clk_termen;
	s32 clk_settle;
	s32 dat_termen;
	s32 dat_settle;
};

struct cio2_buffer {
	struct vb2_v4l2_buffer vbb;
	u32 *lop[CIO2_MAX_LOPS];
	dma_addr_t lop_bus_addr[CIO2_MAX_LOPS];
	unsigned int offset;
};

#define to_cio2_buffer(vb)	container_of(vb, struct cio2_buffer, vbb.vb2_buf)

struct csi2_bus_info {
	u32 port;
	u32 lanes;
};

struct cio2_queue {
	/* mutex to be used by vb2_queue */
	struct mutex lock;
	struct media_pipeline pipe;
	struct csi2_bus_info csi2;
	struct v4l2_subdev *sensor;
	void __iomem *csi_rx_base;

	/* Subdev, /dev/v4l-subdevX */
	struct v4l2_subdev subdev;
	struct mutex subdev_lock; /* Serialise acces to subdev_fmt field */
	struct media_pad subdev_pads[CIO2_PADS];
	struct v4l2_mbus_framefmt subdev_fmt;
	atomic_t frame_sequence;

	/* Video device, /dev/videoX */
	struct video_device vdev;
	struct media_pad vdev_pad;
	struct v4l2_pix_format_mplane format;
	struct vb2_queue vbq;

	/* Buffer queue handling */
	struct cio2_fbpt_entry *fbpt;	/* Frame buffer pointer table */
	dma_addr_t fbpt_bus_addr;
	struct cio2_buffer *bufs[CIO2_MAX_BUFFERS];
	unsigned int bufs_first;	/* Index of the first used entry */
	unsigned int bufs_next;	/* Index of the first unused entry */
	atomic_t bufs_queued;
};

struct cio2_device {
	struct pci_dev *pci_dev;
	void __iomem *base;
	struct v4l2_device v4l2_dev;
	struct cio2_queue queue[CIO2_QUEUES];
	struct cio2_queue *cur_queue;
	/* mutex to be used by video_device */
	struct mutex lock;

	bool streaming;
	struct v4l2_async_notifier notifier;
	struct media_device media_dev;

	/*
	 * Safety net to catch DMA fetch ahead
	 * when reaching the end of LOP
	 */
	void *dummy_page;
	/* DMA handle of dummy_page */
	dma_addr_t dummy_page_bus_addr;
	/* single List of Pointers (LOP) page */
	u32 *dummy_lop;
	/* DMA handle of dummy_lop */
	dma_addr_t dummy_lop_bus_addr;
};

#define to_cio2_device(n)	container_of(n, struct cio2_device, notifier)

/**************** Virtual channel ****************/
/*
 * This should come from sensor driver. No
 * driver interface nor requirement yet.
 */
#define SENSOR_VIR_CH_DFLT		0

/**************** FBPT operations ****************/
#define CIO2_FBPT_SIZE			(CIO2_MAX_BUFFERS * CIO2_MAX_LOPS * \
					 sizeof(struct cio2_fbpt_entry))

#define CIO2_FBPT_SUBENTRY_UNIT		4

/* cio2 fbpt first_entry ctrl status */
#define CIO2_FBPT_CTRL_VALID		BIT(0)
#define CIO2_FBPT_CTRL_IOC		BIT(1)
#define CIO2_FBPT_CTRL_IOS		BIT(2)
#define CIO2_FBPT_CTRL_SUCCXFAIL	BIT(3)
#define CIO2_FBPT_CTRL_CMPLCODE_SHIFT	4

/*
 * Frame Buffer Pointer Table(FBPT) entry
 * each entry describe an output buffer and consists of
 * several sub-entries
 */
struct __packed cio2_fbpt_entry {
	union {
		struct __packed {
			u32 ctrl; /* status ctrl */
			u16 cur_line_num; /* current line # written to DDR */
			u16 frame_num; /* updated by DMA upon FE */
			u32 first_page_offset; /* offset for 1st page in LOP */
		} first_entry;
		/* Second entry per buffer */
		struct __packed {
			u32 timestamp;
			u32 num_of_bytes;
			/* the number of bytes for write on last page */
			u16 last_page_available_bytes;
			/* the number of pages allocated for this buf */
			u16 num_of_pages;
		} second_entry;
	};
	u32 lop_page_addr;	/* Points to list of pointers (LOP) table */
};

static inline struct cio2_queue *file_to_cio2_queue(struct file *file)
{
	return container_of(video_devdata(file), struct cio2_queue, vdev);
}

static inline struct cio2_queue *vb2q_to_cio2_queue(struct vb2_queue *vq)
{
	return container_of(vq, struct cio2_queue, vbq);
}

#endif
