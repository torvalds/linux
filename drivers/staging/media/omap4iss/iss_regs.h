/*
 * TI OMAP4 ISS V4L2 Driver - Register defines
 *
 * Copyright (C) 2012 Texas Instruments.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _OMAP4_ISS_REGS_H_
#define _OMAP4_ISS_REGS_H_

/* ISS */
#define ISS_HL_REVISION					0x0

#define ISS_HL_SYSCONFIG				0x10
#define ISS_HL_SYSCONFIG_IDLEMODE_SHIFT			2
#define ISS_HL_SYSCONFIG_IDLEMODE_FORCEIDLE		0x0
#define ISS_HL_SYSCONFIG_IDLEMODE_NOIDLE		0x1
#define ISS_HL_SYSCONFIG_IDLEMODE_SMARTIDLE		0x2
#define ISS_HL_SYSCONFIG_SOFTRESET			(1 << 0)

#define ISS_HL_IRQSTATUS_RAW(i)				(0x20 + (0x10 * (i)))
#define ISS_HL_IRQSTATUS(i)				(0x24 + (0x10 * (i)))
#define ISS_HL_IRQENABLE_SET(i)				(0x28 + (0x10 * (i)))
#define ISS_HL_IRQENABLE_CLR(i)				(0x2c + (0x10 * (i)))

#define ISS_HL_IRQ_HS_VS				(1 << 17)
#define ISS_HL_IRQ_SIMCOP(i)				(1 << (12 + (i)))
#define ISS_HL_IRQ_BTE					(1 << 11)
#define ISS_HL_IRQ_CBUFF				(1 << 10)
#define ISS_HL_IRQ_CCP2(i)				(1 << ((i) > 3 ? 16 : 14 + (i)))
#define ISS_HL_IRQ_CSIB					(1 << 5)
#define ISS_HL_IRQ_CSIA					(1 << 4)
#define ISS_HL_IRQ_ISP(i)				(1 << (i))

#define ISS_CTRL					0x80
#define ISS_CTRL_CLK_DIV_MASK				(3 << 4)
#define ISS_CTRL_INPUT_SEL_MASK				(3 << 2)
#define ISS_CTRL_INPUT_SEL_CSI2A			(0 << 2)
#define ISS_CTRL_INPUT_SEL_CSI2B			(1 << 2)
#define ISS_CTRL_SYNC_DETECT_VS_RAISING			(3 << 0)

#define ISS_CLKCTRL					0x84
#define ISS_CLKCTRL_VPORT2_CLK				(1 << 30)
#define ISS_CLKCTRL_VPORT1_CLK				(1 << 29)
#define ISS_CLKCTRL_VPORT0_CLK				(1 << 28)
#define ISS_CLKCTRL_CCP2				(1 << 4)
#define ISS_CLKCTRL_CSI2_B				(1 << 3)
#define ISS_CLKCTRL_CSI2_A				(1 << 2)
#define ISS_CLKCTRL_ISP					(1 << 1)
#define ISS_CLKCTRL_SIMCOP				(1 << 0)

#define ISS_CLKSTAT					0x88
#define ISS_CLKSTAT_VPORT2_CLK				(1 << 30)
#define ISS_CLKSTAT_VPORT1_CLK				(1 << 29)
#define ISS_CLKSTAT_VPORT0_CLK				(1 << 28)
#define ISS_CLKSTAT_CCP2				(1 << 4)
#define ISS_CLKSTAT_CSI2_B				(1 << 3)
#define ISS_CLKSTAT_CSI2_A				(1 << 2)
#define ISS_CLKSTAT_ISP					(1 << 1)
#define ISS_CLKSTAT_SIMCOP				(1 << 0)

#define ISS_PM_STATUS					0x8c
#define ISS_PM_STATUS_CBUFF_PM_MASK			(3 << 12)
#define ISS_PM_STATUS_BTE_PM_MASK			(3 << 10)
#define ISS_PM_STATUS_SIMCOP_PM_MASK			(3 << 8)
#define ISS_PM_STATUS_ISP_PM_MASK			(3 << 6)
#define ISS_PM_STATUS_CCP2_PM_MASK			(3 << 4)
#define ISS_PM_STATUS_CSI2_B_PM_MASK			(3 << 2)
#define ISS_PM_STATUS_CSI2_A_PM_MASK			(3 << 0)

#define REGISTER0					0x0
#define REGISTER0_HSCLOCKCONFIG				(1 << 24)
#define REGISTER0_THS_TERM_MASK				(0xff << 8)
#define REGISTER0_THS_TERM_SHIFT			8
#define REGISTER0_THS_SETTLE_MASK			(0xff << 0)
#define REGISTER0_THS_SETTLE_SHIFT			0

#define REGISTER1					0x4
#define REGISTER1_RESET_DONE_CTRLCLK			(1 << 29)
#define REGISTER1_CLOCK_MISS_DETECTOR_STATUS		(1 << 25)
#define REGISTER1_TCLK_TERM_MASK			(0x3f << 18)
#define REGISTER1_TCLK_TERM_SHIFT			18
#define REGISTER1_DPHY_HS_SYNC_PATTERN_SHIFT		10
#define REGISTER1_CTRLCLK_DIV_FACTOR_MASK		(0x3 << 8)
#define REGISTER1_CTRLCLK_DIV_FACTOR_SHIFT		8
#define REGISTER1_TCLK_SETTLE_MASK			(0xff << 0)
#define REGISTER1_TCLK_SETTLE_SHIFT			0

#define REGISTER2					0x8

#define CSI2_SYSCONFIG					0x10
#define CSI2_SYSCONFIG_MSTANDBY_MODE_MASK		(3 << 12)
#define CSI2_SYSCONFIG_MSTANDBY_MODE_FORCE		(0 << 12)
#define CSI2_SYSCONFIG_MSTANDBY_MODE_NO			(1 << 12)
#define CSI2_SYSCONFIG_MSTANDBY_MODE_SMART		(2 << 12)
#define CSI2_SYSCONFIG_SOFT_RESET			(1 << 1)
#define CSI2_SYSCONFIG_AUTO_IDLE			(1 << 0)

#define CSI2_SYSSTATUS					0x14
#define CSI2_SYSSTATUS_RESET_DONE			(1 << 0)

#define CSI2_IRQSTATUS					0x18
#define CSI2_IRQENABLE					0x1c

/* Shared bits across CSI2_IRQENABLE and IRQSTATUS */

#define CSI2_IRQ_OCP_ERR				(1 << 14)
#define CSI2_IRQ_SHORT_PACKET				(1 << 13)
#define CSI2_IRQ_ECC_CORRECTION				(1 << 12)
#define CSI2_IRQ_ECC_NO_CORRECTION			(1 << 11)
#define CSI2_IRQ_COMPLEXIO_ERR				(1 << 9)
#define CSI2_IRQ_FIFO_OVF				(1 << 8)
#define CSI2_IRQ_CONTEXT0				(1 << 0)

#define CSI2_CTRL					0x40
#define CSI2_CTRL_MFLAG_LEVH_MASK			(7 << 20)
#define CSI2_CTRL_MFLAG_LEVH_SHIFT			20
#define CSI2_CTRL_MFLAG_LEVL_MASK			(7 << 17)
#define CSI2_CTRL_MFLAG_LEVL_SHIFT			17
#define CSI2_CTRL_BURST_SIZE_EXPAND			(1 << 16)
#define CSI2_CTRL_VP_CLK_EN				(1 << 15)
#define CSI2_CTRL_NON_POSTED_WRITE			(1 << 13)
#define CSI2_CTRL_VP_ONLY_EN				(1 << 11)
#define CSI2_CTRL_VP_OUT_CTRL_MASK			(3 << 8)
#define CSI2_CTRL_VP_OUT_CTRL_SHIFT			8
#define CSI2_CTRL_DBG_EN				(1 << 7)
#define CSI2_CTRL_BURST_SIZE_MASK			(3 << 5)
#define CSI2_CTRL_ENDIANNESS				(1 << 4)
#define CSI2_CTRL_FRAME					(1 << 3)
#define CSI2_CTRL_ECC_EN				(1 << 2)
#define CSI2_CTRL_IF_EN					(1 << 0)

#define CSI2_DBG_H					0x44

#define CSI2_COMPLEXIO_CFG				0x50
#define CSI2_COMPLEXIO_CFG_RESET_CTRL			(1 << 30)
#define CSI2_COMPLEXIO_CFG_RESET_DONE			(1 << 29)
#define CSI2_COMPLEXIO_CFG_PWD_CMD_MASK			(3 << 27)
#define CSI2_COMPLEXIO_CFG_PWD_CMD_OFF			(0 << 27)
#define CSI2_COMPLEXIO_CFG_PWD_CMD_ON			(1 << 27)
#define CSI2_COMPLEXIO_CFG_PWD_CMD_ULP			(2 << 27)
#define CSI2_COMPLEXIO_CFG_PWD_STATUS_MASK		(3 << 25)
#define CSI2_COMPLEXIO_CFG_PWD_STATUS_OFF		(0 << 25)
#define CSI2_COMPLEXIO_CFG_PWD_STATUS_ON		(1 << 25)
#define CSI2_COMPLEXIO_CFG_PWD_STATUS_ULP		(2 << 25)
#define CSI2_COMPLEXIO_CFG_PWR_AUTO			(1 << 24)
#define CSI2_COMPLEXIO_CFG_DATA_POL(i)			(1 << (((i) * 4) + 3))
#define CSI2_COMPLEXIO_CFG_DATA_POSITION_MASK(i)	(7 << ((i) * 4))
#define CSI2_COMPLEXIO_CFG_DATA_POSITION_SHIFT(i)	((i) * 4)
#define CSI2_COMPLEXIO_CFG_CLOCK_POL			(1 << 3)
#define CSI2_COMPLEXIO_CFG_CLOCK_POSITION_MASK		(7 << 0)
#define CSI2_COMPLEXIO_CFG_CLOCK_POSITION_SHIFT		0

#define CSI2_COMPLEXIO_IRQSTATUS			0x54

#define CSI2_SHORT_PACKET				0x5c

#define CSI2_COMPLEXIO_IRQENABLE			0x60

/* Shared bits across CSI2_COMPLEXIO_IRQENABLE and IRQSTATUS */
#define CSI2_COMPLEXIO_IRQ_STATEALLULPMEXIT		(1 << 26)
#define CSI2_COMPLEXIO_IRQ_STATEALLULPMENTER		(1 << 25)
#define CSI2_COMPLEXIO_IRQ_STATEULPM5			(1 << 24)
#define CSI2_COMPLEXIO_IRQ_STATEULPM4			(1 << 23)
#define CSI2_COMPLEXIO_IRQ_STATEULPM3			(1 << 22)
#define CSI2_COMPLEXIO_IRQ_STATEULPM2			(1 << 21)
#define CSI2_COMPLEXIO_IRQ_STATEULPM1			(1 << 20)
#define CSI2_COMPLEXIO_IRQ_ERRCONTROL5			(1 << 19)
#define CSI2_COMPLEXIO_IRQ_ERRCONTROL4			(1 << 18)
#define CSI2_COMPLEXIO_IRQ_ERRCONTROL3			(1 << 17)
#define CSI2_COMPLEXIO_IRQ_ERRCONTROL2			(1 << 16)
#define CSI2_COMPLEXIO_IRQ_ERRCONTROL1			(1 << 15)
#define CSI2_COMPLEXIO_IRQ_ERRESC5			(1 << 14)
#define CSI2_COMPLEXIO_IRQ_ERRESC4			(1 << 13)
#define CSI2_COMPLEXIO_IRQ_ERRESC3			(1 << 12)
#define CSI2_COMPLEXIO_IRQ_ERRESC2			(1 << 11)
#define CSI2_COMPLEXIO_IRQ_ERRESC1			(1 << 10)
#define CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS5		(1 << 9)
#define CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS4		(1 << 8)
#define CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS3		(1 << 7)
#define CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS2		(1 << 6)
#define CSI2_COMPLEXIO_IRQ_ERRSOTSYNCHS1		(1 << 5)
#define CSI2_COMPLEXIO_IRQ_ERRSOTHS5			(1 << 4)
#define CSI2_COMPLEXIO_IRQ_ERRSOTHS4			(1 << 3)
#define CSI2_COMPLEXIO_IRQ_ERRSOTHS3			(1 << 2)
#define CSI2_COMPLEXIO_IRQ_ERRSOTHS2			(1 << 1)
#define CSI2_COMPLEXIO_IRQ_ERRSOTHS1			(1 << 0)

#define CSI2_DBG_P					0x68

#define CSI2_TIMING					0x6c
#define CSI2_TIMING_FORCE_RX_MODE_IO1			(1 << 15)
#define CSI2_TIMING_STOP_STATE_X16_IO1			(1 << 14)
#define CSI2_TIMING_STOP_STATE_X4_IO1			(1 << 13)
#define CSI2_TIMING_STOP_STATE_COUNTER_IO1_MASK		(0x1fff << 0)
#define CSI2_TIMING_STOP_STATE_COUNTER_IO1_SHIFT	0

#define CSI2_CTX_CTRL1(i)				(0x70 + (0x20 * i))
#define CSI2_CTX_CTRL1_GENERIC				(1 << 30)
#define CSI2_CTX_CTRL1_TRANSCODE			(0xf << 24)
#define CSI2_CTX_CTRL1_FEC_NUMBER_MASK			(0xff << 16)
#define CSI2_CTX_CTRL1_COUNT_MASK			(0xff << 8)
#define CSI2_CTX_CTRL1_COUNT_SHIFT			8
#define CSI2_CTX_CTRL1_EOF_EN				(1 << 7)
#define CSI2_CTX_CTRL1_EOL_EN				(1 << 6)
#define CSI2_CTX_CTRL1_CS_EN				(1 << 5)
#define CSI2_CTX_CTRL1_COUNT_UNLOCK			(1 << 4)
#define CSI2_CTX_CTRL1_PING_PONG			(1 << 3)
#define CSI2_CTX_CTRL1_CTX_EN				(1 << 0)

#define CSI2_CTX_CTRL2(i)				(0x74 + (0x20 * i))
#define CSI2_CTX_CTRL2_FRAME_MASK			(0xffff << 16)
#define CSI2_CTX_CTRL2_FRAME_SHIFT			16
#define CSI2_CTX_CTRL2_USER_DEF_MAP_SHIFT		13
#define CSI2_CTX_CTRL2_USER_DEF_MAP_MASK		\
		(0x3 << CSI2_CTX_CTRL2_USER_DEF_MAP_SHIFT)
#define CSI2_CTX_CTRL2_VIRTUAL_ID_MASK			(3 << 11)
#define CSI2_CTX_CTRL2_VIRTUAL_ID_SHIFT			11
#define CSI2_CTX_CTRL2_DPCM_PRED			(1 << 10)
#define CSI2_CTX_CTRL2_FORMAT_MASK			(0x3ff << 0)
#define CSI2_CTX_CTRL2_FORMAT_SHIFT			0

#define CSI2_CTX_DAT_OFST(i)				(0x78 + (0x20 * i))
#define CSI2_CTX_DAT_OFST_MASK				(0xfff << 5)

#define CSI2_CTX_PING_ADDR(i)				(0x7c + (0x20 * i))
#define CSI2_CTX_PING_ADDR_MASK				0xffffffe0

#define CSI2_CTX_PONG_ADDR(i)				(0x80 + (0x20 * i))
#define CSI2_CTX_PONG_ADDR_MASK				CSI2_CTX_PING_ADDR_MASK

#define CSI2_CTX_IRQENABLE(i)				(0x84 + (0x20 * i))
#define CSI2_CTX_IRQSTATUS(i)				(0x88 + (0x20 * i))

#define CSI2_CTX_CTRL3(i)				(0x8c + (0x20 * i))
#define CSI2_CTX_CTRL3_ALPHA_SHIFT			5
#define CSI2_CTX_CTRL3_ALPHA_MASK			\
		(0x3fff << CSI2_CTX_CTRL3_ALPHA_SHIFT)

/* Shared bits across CSI2_CTX_IRQENABLE and IRQSTATUS */
#define CSI2_CTX_IRQ_ECC_CORRECTION			(1 << 8)
#define CSI2_CTX_IRQ_LINE_NUMBER			(1 << 7)
#define CSI2_CTX_IRQ_FRAME_NUMBER			(1 << 6)
#define CSI2_CTX_IRQ_CS					(1 << 5)
#define CSI2_CTX_IRQ_LE					(1 << 3)
#define CSI2_CTX_IRQ_LS					(1 << 2)
#define CSI2_CTX_IRQ_FE					(1 << 1)
#define CSI2_CTX_IRQ_FS					(1 << 0)

/* ISS BTE */
#define BTE_CTRL					(0x0030)
#define BTE_CTRL_BW_LIMITER_MASK			(0x3ff << 22)
#define BTE_CTRL_BW_LIMITER_SHIFT			22

/* ISS ISP_SYS1 */
#define ISP5_REVISION					(0x0000)
#define ISP5_SYSCONFIG					(0x0010)
#define ISP5_SYSCONFIG_STANDBYMODE_MASK			(3 << 4)
#define ISP5_SYSCONFIG_STANDBYMODE_FORCE		(0 << 4)
#define ISP5_SYSCONFIG_STANDBYMODE_NO			(1 << 4)
#define ISP5_SYSCONFIG_STANDBYMODE_SMART		(2 << 4)
#define ISP5_SYSCONFIG_SOFTRESET			(1 << 1)

#define ISP5_IRQSTATUS(i)				(0x0028 + (0x10 * (i)))
#define ISP5_IRQENABLE_SET(i)				(0x002c + (0x10 * (i)))
#define ISP5_IRQENABLE_CLR(i)				(0x0030 + (0x10 * (i)))

/* Bits shared for ISP5_IRQ* registers */
#define ISP5_IRQ_OCP_ERR				(1 << 31)
#define ISP5_IRQ_IPIPE_INT_DPC_RNEW1			(1 << 29)
#define ISP5_IRQ_IPIPE_INT_DPC_RNEW0			(1 << 28)
#define ISP5_IRQ_IPIPE_INT_DPC_INIT			(1 << 27)
#define ISP5_IRQ_IPIPE_INT_EOF				(1 << 25)
#define ISP5_IRQ_H3A_INT_EOF				(1 << 24)
#define ISP5_IRQ_RSZ_INT_EOF1				(1 << 23)
#define ISP5_IRQ_RSZ_INT_EOF0				(1 << 22)
#define ISP5_IRQ_RSZ_FIFO_IN_BLK_ERR			(1 << 19)
#define ISP5_IRQ_RSZ_FIFO_OVF				(1 << 18)
#define ISP5_IRQ_RSZ_INT_CYC_RSZB			(1 << 17)
#define ISP5_IRQ_RSZ_INT_CYC_RSZA			(1 << 16)
#define ISP5_IRQ_RSZ_INT_DMA				(1 << 15)
#define ISP5_IRQ_RSZ_INT_LAST_PIX			(1 << 14)
#define ISP5_IRQ_RSZ_INT_REG				(1 << 13)
#define ISP5_IRQ_H3A_INT				(1 << 12)
#define ISP5_IRQ_AF_INT					(1 << 11)
#define ISP5_IRQ_AEW_INT				(1 << 10)
#define ISP5_IRQ_IPIPEIF_IRQ				(1 << 9)
#define ISP5_IRQ_IPIPE_INT_HST				(1 << 8)
#define ISP5_IRQ_IPIPE_INT_BSC				(1 << 7)
#define ISP5_IRQ_IPIPE_INT_DMA				(1 << 6)
#define ISP5_IRQ_IPIPE_INT_LAST_PIX			(1 << 5)
#define ISP5_IRQ_IPIPE_INT_REG				(1 << 4)
#define ISP5_IRQ_ISIF_INT(i)				(1 << (i))

#define ISP5_CTRL					(0x006c)
#define ISP5_CTRL_MSTANDBY				(1 << 24)
#define ISP5_CTRL_VD_PULSE_EXT				(1 << 23)
#define ISP5_CTRL_MSTANDBY_WAIT				(1 << 20)
#define ISP5_CTRL_BL_CLK_ENABLE				(1 << 15)
#define ISP5_CTRL_ISIF_CLK_ENABLE			(1 << 14)
#define ISP5_CTRL_H3A_CLK_ENABLE			(1 << 13)
#define ISP5_CTRL_RSZ_CLK_ENABLE			(1 << 12)
#define ISP5_CTRL_IPIPE_CLK_ENABLE			(1 << 11)
#define ISP5_CTRL_IPIPEIF_CLK_ENABLE			(1 << 10)
#define ISP5_CTRL_SYNC_ENABLE				(1 << 9)
#define ISP5_CTRL_PSYNC_CLK_SEL				(1 << 8)

/* ISS ISP ISIF register offsets */
#define ISIF_SYNCEN					(0x0000)
#define ISIF_SYNCEN_DWEN				(1 << 1)
#define ISIF_SYNCEN_SYEN				(1 << 0)

#define ISIF_MODESET					(0x0004)
#define ISIF_MODESET_INPMOD_MASK			(3 << 12)
#define ISIF_MODESET_INPMOD_RAW				(0 << 12)
#define ISIF_MODESET_INPMOD_YCBCR16			(1 << 12)
#define ISIF_MODESET_INPMOD_YCBCR8			(2 << 12)
#define ISIF_MODESET_CCDW_MASK				(7 << 8)
#define ISIF_MODESET_CCDW_2BIT				(2 << 8)
#define ISIF_MODESET_CCDMD				(1 << 7)
#define ISIF_MODESET_SWEN				(1 << 5)
#define ISIF_MODESET_HDPOL				(1 << 3)
#define ISIF_MODESET_VDPOL				(1 << 2)

#define ISIF_SPH					(0x0018)
#define ISIF_SPH_MASK					(0x7fff)

#define ISIF_LNH					(0x001c)
#define ISIF_LNH_MASK					(0x7fff)

#define ISIF_LNV					(0x0028)
#define ISIF_LNV_MASK					(0x7fff)

#define ISIF_HSIZE					(0x0034)
#define ISIF_HSIZE_ADCR					(1 << 12)
#define ISIF_HSIZE_HSIZE_MASK				(0xfff)

#define ISIF_CADU					(0x003c)
#define ISIF_CADU_MASK					(0x7ff)

#define ISIF_CADL					(0x0040)
#define ISIF_CADL_MASK					(0xffff)

#define ISIF_CCOLP					(0x004c)
#define ISIF_CCOLP_CP0_F0_R				(0 << 6)
#define ISIF_CCOLP_CP0_F0_GR				(1 << 6)
#define ISIF_CCOLP_CP0_F0_B				(3 << 6)
#define ISIF_CCOLP_CP0_F0_GB				(2 << 6)
#define ISIF_CCOLP_CP1_F0_R				(0 << 4)
#define ISIF_CCOLP_CP1_F0_GR				(1 << 4)
#define ISIF_CCOLP_CP1_F0_B				(3 << 4)
#define ISIF_CCOLP_CP1_F0_GB				(2 << 4)
#define ISIF_CCOLP_CP2_F0_R				(0 << 2)
#define ISIF_CCOLP_CP2_F0_GR				(1 << 2)
#define ISIF_CCOLP_CP2_F0_B				(3 << 2)
#define ISIF_CCOLP_CP2_F0_GB				(2 << 2)
#define ISIF_CCOLP_CP3_F0_R				(0 << 0)
#define ISIF_CCOLP_CP3_F0_GR				(1 << 0)
#define ISIF_CCOLP_CP3_F0_B				(3 << 0)
#define ISIF_CCOLP_CP3_F0_GB				(2 << 0)

#define ISIF_VDINT(i)					(0x0070 + (i) * 4)
#define ISIF_VDINT_MASK					(0x7fff)

#define ISIF_CGAMMAWD					(0x0080)
#define ISIF_CGAMMAWD_GWDI_MASK				(0xf << 1)
#define ISIF_CGAMMAWD_GWDI(bpp)				((16 - (bpp)) << 1)

#define ISIF_CCDCFG					(0x0088)
#define ISIF_CCDCFG_Y8POS				(1 << 11)

/* ISS ISP IPIPEIF register offsets */
#define IPIPEIF_ENABLE					(0x0000)

#define IPIPEIF_CFG1					(0x0004)
#define IPIPEIF_CFG1_INPSRC1_MASK			(3 << 14)
#define IPIPEIF_CFG1_INPSRC1_VPORT_RAW			(0 << 14)
#define IPIPEIF_CFG1_INPSRC1_SDRAM_RAW			(1 << 14)
#define IPIPEIF_CFG1_INPSRC1_ISIF_DARKFM		(2 << 14)
#define IPIPEIF_CFG1_INPSRC1_SDRAM_YUV			(3 << 14)
#define IPIPEIF_CFG1_INPSRC2_MASK			(3 << 2)
#define IPIPEIF_CFG1_INPSRC2_ISIF			(0 << 2)
#define IPIPEIF_CFG1_INPSRC2_SDRAM_RAW			(1 << 2)
#define IPIPEIF_CFG1_INPSRC2_ISIF_DARKFM		(2 << 2)
#define IPIPEIF_CFG1_INPSRC2_SDRAM_YUV			(3 << 2)

#define IPIPEIF_CFG2					(0x0030)
#define IPIPEIF_CFG2_YUV8P				(1 << 7)
#define IPIPEIF_CFG2_YUV8				(1 << 6)
#define IPIPEIF_CFG2_YUV16				(1 << 3)
#define IPIPEIF_CFG2_VDPOL				(1 << 2)
#define IPIPEIF_CFG2_HDPOL				(1 << 1)
#define IPIPEIF_CFG2_INTSW				(1 << 0)

#define IPIPEIF_CLKDIV					(0x0040)

/* ISS ISP IPIPE register offsets */
#define IPIPE_SRC_EN					(0x0000)
#define IPIPE_SRC_EN_EN					(1 << 0)

#define IPIPE_SRC_MODE					(0x0004)
#define IPIPE_SRC_MODE_WRT				(1 << 1)
#define IPIPE_SRC_MODE_OST				(1 << 0)

#define IPIPE_SRC_FMT					(0x0008)
#define IPIPE_SRC_FMT_RAW2YUV				(0 << 0)
#define IPIPE_SRC_FMT_RAW2RAW				(1 << 0)
#define IPIPE_SRC_FMT_RAW2STATS				(2 << 0)
#define IPIPE_SRC_FMT_YUV2YUV				(3 << 0)

#define IPIPE_SRC_COL					(0x000c)
#define IPIPE_SRC_COL_OO_R				(0 << 6)
#define IPIPE_SRC_COL_OO_GR				(1 << 6)
#define IPIPE_SRC_COL_OO_B				(3 << 6)
#define IPIPE_SRC_COL_OO_GB				(2 << 6)
#define IPIPE_SRC_COL_OE_R				(0 << 4)
#define IPIPE_SRC_COL_OE_GR				(1 << 4)
#define IPIPE_SRC_COL_OE_B				(3 << 4)
#define IPIPE_SRC_COL_OE_GB				(2 << 4)
#define IPIPE_SRC_COL_EO_R				(0 << 2)
#define IPIPE_SRC_COL_EO_GR				(1 << 2)
#define IPIPE_SRC_COL_EO_B				(3 << 2)
#define IPIPE_SRC_COL_EO_GB				(2 << 2)
#define IPIPE_SRC_COL_EE_R				(0 << 0)
#define IPIPE_SRC_COL_EE_GR				(1 << 0)
#define IPIPE_SRC_COL_EE_B				(3 << 0)
#define IPIPE_SRC_COL_EE_GB				(2 << 0)

#define IPIPE_SRC_VPS					(0x0010)
#define IPIPE_SRC_VPS_MASK				(0xffff)

#define IPIPE_SRC_VSZ					(0x0014)
#define IPIPE_SRC_VSZ_MASK				(0x1fff)

#define IPIPE_SRC_HPS					(0x0018)
#define IPIPE_SRC_HPS_MASK				(0xffff)

#define IPIPE_SRC_HSZ					(0x001c)
#define IPIPE_SRC_HSZ_MASK				(0x1ffe)

#define IPIPE_SEL_SBU					(0x0020)

#define IPIPE_SRC_STA					(0x0024)

#define IPIPE_GCK_MMR					(0x0028)
#define IPIPE_GCK_MMR_REG				(1 << 0)

#define IPIPE_GCK_PIX					(0x002c)
#define IPIPE_GCK_PIX_G3				(1 << 3)
#define IPIPE_GCK_PIX_G2				(1 << 2)
#define IPIPE_GCK_PIX_G1				(1 << 1)
#define IPIPE_GCK_PIX_G0				(1 << 0)

#define IPIPE_DPC_LUT_EN				(0x0034)
#define IPIPE_DPC_LUT_SEL				(0x0038)
#define IPIPE_DPC_LUT_ADR				(0x003c)
#define IPIPE_DPC_LUT_SIZ				(0x0040)

#define IPIPE_DPC_OTF_EN				(0x0044)
#define IPIPE_DPC_OTF_TYP				(0x0048)
#define IPIPE_DPC_OTF_2_D_THR_R				(0x004c)
#define IPIPE_DPC_OTF_2_D_THR_GR			(0x0050)
#define IPIPE_DPC_OTF_2_D_THR_GB			(0x0054)
#define IPIPE_DPC_OTF_2_D_THR_B				(0x0058)
#define IPIPE_DPC_OTF_2_C_THR_R				(0x005c)
#define IPIPE_DPC_OTF_2_C_THR_GR			(0x0060)
#define IPIPE_DPC_OTF_2_C_THR_GB			(0x0064)
#define IPIPE_DPC_OTF_2_C_THR_B				(0x0068)
#define IPIPE_DPC_OTF_3_SHF				(0x006c)
#define IPIPE_DPC_OTF_3_D_THR				(0x0070)
#define IPIPE_DPC_OTF_3_D_SPL				(0x0074)
#define IPIPE_DPC_OTF_3_D_MIN				(0x0078)
#define IPIPE_DPC_OTF_3_D_MAX				(0x007c)
#define IPIPE_DPC_OTF_3_C_THR				(0x0080)
#define IPIPE_DPC_OTF_3_C_SLP				(0x0084)
#define IPIPE_DPC_OTF_3_C_MIN				(0x0088)
#define IPIPE_DPC_OTF_3_C_MAX				(0x008c)

#define IPIPE_LSC_VOFT					(0x0090)
#define IPIPE_LSC_VA2					(0x0094)
#define IPIPE_LSC_VA1					(0x0098)
#define IPIPE_LSC_VS					(0x009c)
#define IPIPE_LSC_HOFT					(0x00a0)
#define IPIPE_LSC_HA2					(0x00a4)
#define IPIPE_LSC_HA1					(0x00a8)
#define IPIPE_LSC_HS					(0x00ac)
#define IPIPE_LSC_GAN_R					(0x00b0)
#define IPIPE_LSC_GAN_GR				(0x00b4)
#define IPIPE_LSC_GAN_GB				(0x00b8)
#define IPIPE_LSC_GAN_B					(0x00bc)
#define IPIPE_LSC_OFT_R					(0x00c0)
#define IPIPE_LSC_OFT_GR				(0x00c4)
#define IPIPE_LSC_OFT_GB				(0x00c8)
#define IPIPE_LSC_OFT_B					(0x00cc)
#define IPIPE_LSC_SHF					(0x00d0)
#define IPIPE_LSC_MAX					(0x00d4)

#define IPIPE_D2F_1ST_EN				(0x00d8)
#define IPIPE_D2F_1ST_TYP				(0x00dc)
#define IPIPE_D2F_1ST_THR_00				(0x00e0)
#define IPIPE_D2F_1ST_THR_01				(0x00e4)
#define IPIPE_D2F_1ST_THR_02				(0x00e8)
#define IPIPE_D2F_1ST_THR_03				(0x00ec)
#define IPIPE_D2F_1ST_THR_04				(0x00f0)
#define IPIPE_D2F_1ST_THR_05				(0x00f4)
#define IPIPE_D2F_1ST_THR_06				(0x00f8)
#define IPIPE_D2F_1ST_THR_07				(0x00fc)
#define IPIPE_D2F_1ST_STR_00				(0x0100)
#define IPIPE_D2F_1ST_STR_01				(0x0104)
#define IPIPE_D2F_1ST_STR_02				(0x0108)
#define IPIPE_D2F_1ST_STR_03				(0x010c)
#define IPIPE_D2F_1ST_STR_04				(0x0110)
#define IPIPE_D2F_1ST_STR_05				(0x0114)
#define IPIPE_D2F_1ST_STR_06				(0x0118)
#define IPIPE_D2F_1ST_STR_07				(0x011c)
#define IPIPE_D2F_1ST_SPR_00				(0x0120)
#define IPIPE_D2F_1ST_SPR_01				(0x0124)
#define IPIPE_D2F_1ST_SPR_02				(0x0128)
#define IPIPE_D2F_1ST_SPR_03				(0x012c)
#define IPIPE_D2F_1ST_SPR_04				(0x0130)
#define IPIPE_D2F_1ST_SPR_05				(0x0134)
#define IPIPE_D2F_1ST_SPR_06				(0x0138)
#define IPIPE_D2F_1ST_SPR_07				(0x013c)
#define IPIPE_D2F_1ST_EDG_MIN				(0x0140)
#define IPIPE_D2F_1ST_EDG_MAX				(0x0144)
#define IPIPE_D2F_2ND_EN				(0x0148)
#define IPIPE_D2F_2ND_TYP				(0x014c)
#define IPIPE_D2F_2ND_THR00				(0x0150)
#define IPIPE_D2F_2ND_THR01				(0x0154)
#define IPIPE_D2F_2ND_THR02				(0x0158)
#define IPIPE_D2F_2ND_THR03				(0x015c)
#define IPIPE_D2F_2ND_THR04				(0x0160)
#define IPIPE_D2F_2ND_THR05				(0x0164)
#define IPIPE_D2F_2ND_THR06				(0x0168)
#define IPIPE_D2F_2ND_THR07				(0x016c)
#define IPIPE_D2F_2ND_STR_00				(0x0170)
#define IPIPE_D2F_2ND_STR_01				(0x0174)
#define IPIPE_D2F_2ND_STR_02				(0x0178)
#define IPIPE_D2F_2ND_STR_03				(0x017c)
#define IPIPE_D2F_2ND_STR_04				(0x0180)
#define IPIPE_D2F_2ND_STR_05				(0x0184)
#define IPIPE_D2F_2ND_STR_06				(0x0188)
#define IPIPE_D2F_2ND_STR_07				(0x018c)
#define IPIPE_D2F_2ND_SPR_00				(0x0190)
#define IPIPE_D2F_2ND_SPR_01				(0x0194)
#define IPIPE_D2F_2ND_SPR_02				(0x0198)
#define IPIPE_D2F_2ND_SPR_03				(0x019c)
#define IPIPE_D2F_2ND_SPR_04				(0x01a0)
#define IPIPE_D2F_2ND_SPR_05				(0x01a4)
#define IPIPE_D2F_2ND_SPR_06				(0x01a8)
#define IPIPE_D2F_2ND_SPR_07				(0x01ac)
#define IPIPE_D2F_2ND_EDG_MIN				(0x01b0)
#define IPIPE_D2F_2ND_EDG_MAX				(0x01b4)

#define IPIPE_GIC_EN					(0x01b8)
#define IPIPE_GIC_TYP					(0x01bc)
#define IPIPE_GIC_GAN					(0x01c0)
#define IPIPE_GIC_NFGAIN				(0x01c4)
#define IPIPE_GIC_THR					(0x01c8)
#define IPIPE_GIC_SLP					(0x01cc)

#define IPIPE_WB2_OFT_R					(0x01d0)
#define IPIPE_WB2_OFT_GR				(0x01d4)
#define IPIPE_WB2_OFT_GB				(0x01d8)
#define IPIPE_WB2_OFT_B					(0x01dc)

#define IPIPE_WB2_WGN_R					(0x01e0)
#define IPIPE_WB2_WGN_GR				(0x01e4)
#define IPIPE_WB2_WGN_GB				(0x01e8)
#define IPIPE_WB2_WGN_B					(0x01ec)

#define IPIPE_CFA_MODE					(0x01f0)
#define IPIPE_CFA_2DIR_HPF_THR				(0x01f4)
#define IPIPE_CFA_2DIR_HPF_SLP				(0x01f8)
#define IPIPE_CFA_2DIR_MIX_THR				(0x01fc)
#define IPIPE_CFA_2DIR_MIX_SLP				(0x0200)
#define IPIPE_CFA_2DIR_DIR_TRH				(0x0204)
#define IPIPE_CFA_2DIR_DIR_SLP				(0x0208)
#define IPIPE_CFA_2DIR_NDWT				(0x020c)
#define IPIPE_CFA_MONO_HUE_FRA				(0x0210)
#define IPIPE_CFA_MONO_EDG_THR				(0x0214)
#define IPIPE_CFA_MONO_THR_MIN				(0x0218)

#define IPIPE_CFA_MONO_THR_SLP				(0x021c)
#define IPIPE_CFA_MONO_SLP_MIN				(0x0220)
#define IPIPE_CFA_MONO_SLP_SLP				(0x0224)
#define IPIPE_CFA_MONO_LPWT				(0x0228)

#define IPIPE_RGB1_MUL_RR				(0x022c)
#define IPIPE_RGB1_MUL_GR				(0x0230)
#define IPIPE_RGB1_MUL_BR				(0x0234)
#define IPIPE_RGB1_MUL_RG				(0x0238)
#define IPIPE_RGB1_MUL_GG				(0x023c)
#define IPIPE_RGB1_MUL_BG				(0x0240)
#define IPIPE_RGB1_MUL_RB				(0x0244)
#define IPIPE_RGB1_MUL_GB				(0x0248)
#define IPIPE_RGB1_MUL_BB				(0x024c)
#define IPIPE_RGB1_OFT_OR				(0x0250)
#define IPIPE_RGB1_OFT_OG				(0x0254)
#define IPIPE_RGB1_OFT_OB				(0x0258)
#define IPIPE_GMM_CFG					(0x025c)
#define IPIPE_RGB2_MUL_RR				(0x0260)
#define IPIPE_RGB2_MUL_GR				(0x0264)
#define IPIPE_RGB2_MUL_BR				(0x0268)
#define IPIPE_RGB2_MUL_RG				(0x026c)
#define IPIPE_RGB2_MUL_GG				(0x0270)
#define IPIPE_RGB2_MUL_BG				(0x0274)
#define IPIPE_RGB2_MUL_RB				(0x0278)
#define IPIPE_RGB2_MUL_GB				(0x027c)
#define IPIPE_RGB2_MUL_BB				(0x0280)
#define IPIPE_RGB2_OFT_OR				(0x0284)
#define IPIPE_RGB2_OFT_OG				(0x0288)
#define IPIPE_RGB2_OFT_OB				(0x028c)

#define IPIPE_YUV_ADJ					(0x0294)
#define IPIPE_YUV_MUL_RY				(0x0298)
#define IPIPE_YUV_MUL_GY				(0x029c)
#define IPIPE_YUV_MUL_BY				(0x02a0)
#define IPIPE_YUV_MUL_RCB				(0x02a4)
#define IPIPE_YUV_MUL_GCB				(0x02a8)
#define IPIPE_YUV_MUL_BCB				(0x02ac)
#define IPIPE_YUV_MUL_RCR				(0x02b0)
#define IPIPE_YUV_MUL_GCR				(0x02b4)
#define IPIPE_YUV_MUL_BCR				(0x02b8)
#define IPIPE_YUV_OFT_Y					(0x02bc)
#define IPIPE_YUV_OFT_CB				(0x02c0)
#define IPIPE_YUV_OFT_CR				(0x02c4)

#define IPIPE_YUV_PHS					(0x02c8)
#define IPIPE_YUV_PHS_LPF				(1 << 1)
#define IPIPE_YUV_PHS_POS				(1 << 0)

#define IPIPE_YEE_EN					(0x02d4)
#define IPIPE_YEE_TYP					(0x02d8)
#define IPIPE_YEE_SHF					(0x02dc)
#define IPIPE_YEE_MUL_00				(0x02e0)
#define IPIPE_YEE_MUL_01				(0x02e4)
#define IPIPE_YEE_MUL_02				(0x02e8)
#define IPIPE_YEE_MUL_10				(0x02ec)
#define IPIPE_YEE_MUL_11				(0x02f0)
#define IPIPE_YEE_MUL_12				(0x02f4)
#define IPIPE_YEE_MUL_20				(0x02f8)
#define IPIPE_YEE_MUL_21				(0x02fc)
#define IPIPE_YEE_MUL_22				(0x0300)
#define IPIPE_YEE_THR					(0x0304)
#define IPIPE_YEE_E_GAN					(0x0308)
#define IPIPE_YEE_E_THR_1				(0x030c)
#define IPIPE_YEE_E_THR_2				(0x0310)
#define IPIPE_YEE_G_GAN					(0x0314)
#define IPIPE_YEE_G_OFT					(0x0318)

#define IPIPE_CAR_EN					(0x031c)
#define IPIPE_CAR_TYP					(0x0320)
#define IPIPE_CAR_SW					(0x0324)
#define IPIPE_CAR_HPF_TYP				(0x0328)
#define IPIPE_CAR_HPF_SHF				(0x032c)
#define IPIPE_CAR_HPF_THR				(0x0330)
#define IPIPE_CAR_GN1_GAN				(0x0334)
#define IPIPE_CAR_GN1_SHF				(0x0338)
#define IPIPE_CAR_GN1_MIN				(0x033c)
#define IPIPE_CAR_GN2_GAN				(0x0340)
#define IPIPE_CAR_GN2_SHF				(0x0344)
#define IPIPE_CAR_GN2_MIN				(0x0348)
#define IPIPE_CGS_EN					(0x034c)
#define IPIPE_CGS_GN1_L_THR				(0x0350)
#define IPIPE_CGS_GN1_L_GAIN				(0x0354)
#define IPIPE_CGS_GN1_L_SHF				(0x0358)
#define IPIPE_CGS_GN1_L_MIN				(0x035c)
#define IPIPE_CGS_GN1_H_THR				(0x0360)
#define IPIPE_CGS_GN1_H_GAIN				(0x0364)
#define IPIPE_CGS_GN1_H_SHF				(0x0368)
#define IPIPE_CGS_GN1_H_MIN				(0x036c)
#define IPIPE_CGS_GN2_L_THR				(0x0370)
#define IPIPE_CGS_GN2_L_GAIN				(0x0374)
#define IPIPE_CGS_GN2_L_SHF				(0x0378)
#define IPIPE_CGS_GN2_L_MIN				(0x037c)

#define IPIPE_BOX_EN					(0x0380)
#define IPIPE_BOX_MODE					(0x0384)
#define IPIPE_BOX_TYP					(0x0388)
#define IPIPE_BOX_SHF					(0x038c)
#define IPIPE_BOX_SDR_SAD_H				(0x0390)
#define IPIPE_BOX_SDR_SAD_L				(0x0394)

#define IPIPE_HST_EN					(0x039c)
#define IPIPE_HST_MODE					(0x03a0)
#define IPIPE_HST_SEL					(0x03a4)
#define IPIPE_HST_PARA					(0x03a8)
#define IPIPE_HST_0_VPS					(0x03ac)
#define IPIPE_HST_0_VSZ					(0x03b0)
#define IPIPE_HST_0_HPS					(0x03b4)
#define IPIPE_HST_0_HSZ					(0x03b8)
#define IPIPE_HST_1_VPS					(0x03bc)
#define IPIPE_HST_1_VSZ					(0x03c0)
#define IPIPE_HST_1_HPS					(0x03c4)
#define IPIPE_HST_1_HSZ					(0x03c8)
#define IPIPE_HST_2_VPS					(0x03cc)
#define IPIPE_HST_2_VSZ					(0x03d0)
#define IPIPE_HST_2_HPS					(0x03d4)
#define IPIPE_HST_2_HSZ					(0x03d8)
#define IPIPE_HST_3_VPS					(0x03dc)
#define IPIPE_HST_3_VSZ					(0x03e0)
#define IPIPE_HST_3_HPS					(0x03e4)
#define IPIPE_HST_3_HSZ					(0x03e8)
#define IPIPE_HST_TBL					(0x03ec)
#define IPIPE_HST_MUL_R					(0x03f0)
#define IPIPE_HST_MUL_GR				(0x03f4)
#define IPIPE_HST_MUL_GB				(0x03f8)
#define IPIPE_HST_MUL_B					(0x03fc)

#define IPIPE_BSC_EN					(0x0400)
#define IPIPE_BSC_MODE					(0x0404)
#define IPIPE_BSC_TYP					(0x0408)
#define IPIPE_BSC_ROW_VCT				(0x040c)
#define IPIPE_BSC_ROW_SHF				(0x0410)
#define IPIPE_BSC_ROW_VPO				(0x0414)
#define IPIPE_BSC_ROW_VNU				(0x0418)
#define IPIPE_BSC_ROW_VSKIP				(0x041c)
#define IPIPE_BSC_ROW_HPO				(0x0420)
#define IPIPE_BSC_ROW_HNU				(0x0424)
#define IPIPE_BSC_ROW_HSKIP				(0x0428)
#define IPIPE_BSC_COL_VCT				(0x042c)
#define IPIPE_BSC_COL_SHF				(0x0430)
#define IPIPE_BSC_COL_VPO				(0x0434)
#define IPIPE_BSC_COL_VNU				(0x0438)
#define IPIPE_BSC_COL_VSKIP				(0x043c)
#define IPIPE_BSC_COL_HPO				(0x0440)
#define IPIPE_BSC_COL_HNU				(0x0444)
#define IPIPE_BSC_COL_HSKIP				(0x0448)

#define IPIPE_BSC_EN					(0x0400)

/* ISS ISP Resizer register offsets */
#define RSZ_REVISION					(0x0000)
#define RSZ_SYSCONFIG					(0x0004)
#define RSZ_SYSCONFIG_RSZB_CLK_EN			(1 << 9)
#define RSZ_SYSCONFIG_RSZA_CLK_EN			(1 << 8)

#define RSZ_IN_FIFO_CTRL				(0x000c)
#define RSZ_IN_FIFO_CTRL_THRLD_LOW_MASK			(0x1ff << 16)
#define RSZ_IN_FIFO_CTRL_THRLD_LOW_SHIFT		16
#define RSZ_IN_FIFO_CTRL_THRLD_HIGH_MASK		(0x1ff << 0)
#define RSZ_IN_FIFO_CTRL_THRLD_HIGH_SHIFT		0

#define RSZ_FRACDIV					(0x0008)
#define RSZ_FRACDIV_MASK				(0xffff)

#define RSZ_SRC_EN					(0x0020)
#define RSZ_SRC_EN_SRC_EN				(1 << 0)

#define RSZ_SRC_MODE					(0x0024)
#define RSZ_SRC_MODE_OST				(1 << 0)
#define RSZ_SRC_MODE_WRT				(1 << 1)

#define RSZ_SRC_FMT0					(0x0028)
#define RSZ_SRC_FMT0_BYPASS				(1 << 1)
#define RSZ_SRC_FMT0_SEL				(1 << 0)

#define RSZ_SRC_FMT1					(0x002c)
#define RSZ_SRC_FMT1_IN420				(1 << 1)

#define RSZ_SRC_VPS					(0x0030)
#define RSZ_SRC_VSZ					(0x0034)
#define RSZ_SRC_HPS					(0x0038)
#define RSZ_SRC_HSZ					(0x003c)
#define RSZ_DMA_RZA					(0x0040)
#define RSZ_DMA_RZB					(0x0044)
#define RSZ_DMA_STA					(0x0048)
#define RSZ_GCK_MMR					(0x004c)
#define RSZ_GCK_MMR_MMR					(1 << 0)

#define RSZ_GCK_SDR					(0x0054)
#define RSZ_GCK_SDR_CORE				(1 << 0)

#define RSZ_IRQ_RZA					(0x0058)
#define RSZ_IRQ_RZA_MASK				(0x1fff)

#define RSZ_IRQ_RZB					(0x005c)
#define RSZ_IRQ_RZB_MASK				(0x1fff)

#define RSZ_YUV_Y_MIN					(0x0060)
#define RSZ_YUV_Y_MAX					(0x0064)
#define RSZ_YUV_C_MIN					(0x0068)
#define RSZ_YUV_C_MAX					(0x006c)

#define RSZ_SEQ						(0x0074)
#define RSZ_SEQ_HRVB					(1 << 2)
#define RSZ_SEQ_HRVA					(1 << 0)

#define RZA_EN						(0x0078)
#define RZA_MODE					(0x007c)
#define RZA_MODE_ONE_SHOT				(1 << 0)

#define RZA_420						(0x0080)
#define RZA_I_VPS					(0x0084)
#define RZA_I_HPS					(0x0088)
#define RZA_O_VSZ					(0x008c)
#define RZA_O_HSZ					(0x0090)
#define RZA_V_PHS_Y					(0x0094)
#define RZA_V_PHS_C					(0x0098)
#define RZA_V_DIF					(0x009c)
#define RZA_V_TYP					(0x00a0)
#define RZA_V_LPF					(0x00a4)
#define RZA_H_PHS					(0x00a8)
#define RZA_H_DIF					(0x00b0)
#define RZA_H_TYP					(0x00b4)
#define RZA_H_LPF					(0x00b8)
#define RZA_DWN_EN					(0x00bc)
#define RZA_SDR_Y_BAD_H					(0x00d0)
#define RZA_SDR_Y_BAD_L					(0x00d4)
#define RZA_SDR_Y_SAD_H					(0x00d8)
#define RZA_SDR_Y_SAD_L					(0x00dc)
#define RZA_SDR_Y_OFT					(0x00e0)
#define RZA_SDR_Y_PTR_S					(0x00e4)
#define RZA_SDR_Y_PTR_E					(0x00e8)
#define RZA_SDR_C_BAD_H					(0x00ec)
#define RZA_SDR_C_BAD_L					(0x00f0)
#define RZA_SDR_C_SAD_H					(0x00f4)
#define RZA_SDR_C_SAD_L					(0x00f8)
#define RZA_SDR_C_OFT					(0x00fc)
#define RZA_SDR_C_PTR_S					(0x0100)
#define RZA_SDR_C_PTR_E					(0x0104)

#define RZB_EN						(0x0108)
#define RZB_MODE					(0x010c)
#define RZB_420						(0x0110)
#define RZB_I_VPS					(0x0114)
#define RZB_I_HPS					(0x0118)
#define RZB_O_VSZ					(0x011c)
#define RZB_O_HSZ					(0x0120)

#define RZB_V_DIF					(0x012c)
#define RZB_V_TYP					(0x0130)
#define RZB_V_LPF					(0x0134)

#define RZB_H_DIF					(0x0140)
#define RZB_H_TYP					(0x0144)
#define RZB_H_LPF					(0x0148)

#define RZB_SDR_Y_BAD_H					(0x0160)
#define RZB_SDR_Y_BAD_L					(0x0164)
#define RZB_SDR_Y_SAD_H					(0x0168)
#define RZB_SDR_Y_SAD_L					(0x016c)
#define RZB_SDR_Y_OFT					(0x0170)
#define RZB_SDR_Y_PTR_S					(0x0174)
#define RZB_SDR_Y_PTR_E					(0x0178)
#define RZB_SDR_C_BAD_H					(0x017c)
#define RZB_SDR_C_BAD_L					(0x0180)
#define RZB_SDR_C_SAD_H					(0x0184)
#define RZB_SDR_C_SAD_L					(0x0188)

#define RZB_SDR_C_PTR_S					(0x0190)
#define RZB_SDR_C_PTR_E					(0x0194)

/* Shared Bitmasks between RZA & RZB */
#define RSZ_EN_EN					(1 << 0)

#define RSZ_420_CEN					(1 << 1)
#define RSZ_420_YEN					(1 << 0)

#define RSZ_I_VPS_MASK					(0x1fff)

#define RSZ_I_HPS_MASK					(0x1fff)

#define RSZ_O_VSZ_MASK					(0x1fff)

#define RSZ_O_HSZ_MASK					(0x1ffe)

#define RSZ_V_PHS_Y_MASK				(0x3fff)

#define RSZ_V_PHS_C_MASK				(0x3fff)

#define RSZ_V_DIF_MASK					(0x3fff)

#define RSZ_V_TYP_C					(1 << 1)
#define RSZ_V_TYP_Y					(1 << 0)

#define RSZ_V_LPF_C_MASK				(0x3f << 6)
#define RSZ_V_LPF_C_SHIFT				6
#define RSZ_V_LPF_Y_MASK				(0x3f << 0)
#define RSZ_V_LPF_Y_SHIFT				0

#define RSZ_H_PHS_MASK					(0x3fff)

#define RSZ_H_DIF_MASK					(0x3fff)

#define RSZ_H_TYP_C					(1 << 1)
#define RSZ_H_TYP_Y					(1 << 0)

#define RSZ_H_LPF_C_MASK				(0x3f << 6)
#define RSZ_H_LPF_C_SHIFT				6
#define RSZ_H_LPF_Y_MASK				(0x3f << 0)
#define RSZ_H_LPF_Y_SHIFT				0

#define RSZ_DWN_EN_DWN_EN				(1 << 0)

#endif /* _OMAP4_ISS_REGS_H_ */
