/*
 * OMAP3XXX L3 Interconnect Driver header
 *
 * Copyright (C) 2011 Texas Corporation
 *	Felipe Balbi <balbi@ti.com>
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *	sricharan <r.sricharan@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#ifndef __ARCH_ARM_MACH_OMAP2_L3_INTERCONNECT_3XXX_H
#define __ARCH_ARM_MACH_OMAP2_L3_INTERCONNECT_3XXX_H

/* Register definitions. All 64-bit wide */
#define L3_COMPONENT			0x000
#define L3_CORE				0x018
#define L3_AGENT_CONTROL		0x020
#define L3_AGENT_STATUS			0x028
#define L3_ERROR_LOG			0x058

#define L3_ERROR_LOG_MULTI		(1 << 31)
#define L3_ERROR_LOG_SECONDARY		(1 << 30)

#define L3_ERROR_LOG_ADDR		0x060

/* Register definitions for Sideband Interconnect */
#define L3_SI_CONTROL			0x020
#define L3_SI_FLAG_STATUS_0		0x510

static const u64 shift = 1;

#define L3_STATUS_0_MPUIA_BRST		(shift << 0)
#define L3_STATUS_0_MPUIA_RSP		(shift << 1)
#define L3_STATUS_0_MPUIA_INBAND	(shift << 2)
#define L3_STATUS_0_IVAIA_BRST		(shift << 6)
#define L3_STATUS_0_IVAIA_RSP		(shift << 7)
#define L3_STATUS_0_IVAIA_INBAND	(shift << 8)
#define L3_STATUS_0_SGXIA_BRST		(shift << 9)
#define L3_STATUS_0_SGXIA_RSP		(shift << 10)
#define L3_STATUS_0_SGXIA_MERROR	(shift << 11)
#define L3_STATUS_0_CAMIA_BRST		(shift << 12)
#define L3_STATUS_0_CAMIA_RSP		(shift << 13)
#define L3_STATUS_0_CAMIA_INBAND	(shift << 14)
#define L3_STATUS_0_DISPIA_BRST		(shift << 15)
#define L3_STATUS_0_DISPIA_RSP		(shift << 16)
#define L3_STATUS_0_DMARDIA_BRST	(shift << 18)
#define L3_STATUS_0_DMARDIA_RSP		(shift << 19)
#define L3_STATUS_0_DMAWRIA_BRST	(shift << 21)
#define L3_STATUS_0_DMAWRIA_RSP		(shift << 22)
#define L3_STATUS_0_USBOTGIA_BRST	(shift << 24)
#define L3_STATUS_0_USBOTGIA_RSP	(shift << 25)
#define L3_STATUS_0_USBOTGIA_INBAND	(shift << 26)
#define L3_STATUS_0_USBHOSTIA_BRST	(shift << 27)
#define L3_STATUS_0_USBHOSTIA_INBAND	(shift << 28)
#define L3_STATUS_0_SMSTA_REQ		(shift << 48)
#define L3_STATUS_0_GPMCTA_REQ		(shift << 49)
#define L3_STATUS_0_OCMRAMTA_REQ	(shift << 50)
#define L3_STATUS_0_OCMROMTA_REQ	(shift << 51)
#define L3_STATUS_0_IVATA_REQ		(shift << 54)
#define L3_STATUS_0_SGXTA_REQ		(shift << 55)
#define L3_STATUS_0_SGXTA_SERROR	(shift << 56)
#define L3_STATUS_0_GPMCTA_SERROR	(shift << 57)
#define L3_STATUS_0_L4CORETA_REQ	(shift << 58)
#define L3_STATUS_0_L4PERTA_REQ		(shift << 59)
#define L3_STATUS_0_L4EMUTA_REQ		(shift << 60)
#define L3_STATUS_0_MAD2DTA_REQ		(shift << 61)

#define L3_STATUS_0_TIMEOUT_MASK	(L3_STATUS_0_MPUIA_BRST		\
					| L3_STATUS_0_MPUIA_RSP		\
					| L3_STATUS_0_IVAIA_BRST	\
					| L3_STATUS_0_IVAIA_RSP		\
					| L3_STATUS_0_SGXIA_BRST	\
					| L3_STATUS_0_SGXIA_RSP		\
					| L3_STATUS_0_CAMIA_BRST	\
					| L3_STATUS_0_CAMIA_RSP		\
					| L3_STATUS_0_DISPIA_BRST	\
					| L3_STATUS_0_DISPIA_RSP	\
					| L3_STATUS_0_DMARDIA_BRST	\
					| L3_STATUS_0_DMARDIA_RSP	\
					| L3_STATUS_0_DMAWRIA_BRST	\
					| L3_STATUS_0_DMAWRIA_RSP	\
					| L3_STATUS_0_USBOTGIA_BRST	\
					| L3_STATUS_0_USBOTGIA_RSP	\
					| L3_STATUS_0_USBHOSTIA_BRST	\
					| L3_STATUS_0_SMSTA_REQ		\
					| L3_STATUS_0_GPMCTA_REQ	\
					| L3_STATUS_0_OCMRAMTA_REQ	\
					| L3_STATUS_0_OCMROMTA_REQ	\
					| L3_STATUS_0_IVATA_REQ		\
					| L3_STATUS_0_SGXTA_REQ		\
					| L3_STATUS_0_L4CORETA_REQ	\
					| L3_STATUS_0_L4PERTA_REQ	\
					| L3_STATUS_0_L4EMUTA_REQ	\
					| L3_STATUS_0_MAD2DTA_REQ)

#define L3_SI_FLAG_STATUS_1		0x530

#define L3_STATUS_1_MPU_DATAIA		(1 << 0)
#define L3_STATUS_1_DAPIA0		(1 << 3)
#define L3_STATUS_1_DAPIA1		(1 << 4)
#define L3_STATUS_1_IVAIA		(1 << 6)

#define L3_PM_ERROR_LOG			0x020
#define L3_PM_CONTROL			0x028
#define L3_PM_ERROR_CLEAR_SINGLE	0x030
#define L3_PM_ERROR_CLEAR_MULTI		0x038
#define L3_PM_REQ_INFO_PERMISSION(n)	(0x048 + (0x020 * n))
#define L3_PM_READ_PERMISSION(n)	(0x050 + (0x020 * n))
#define L3_PM_WRITE_PERMISSION(n)	(0x058 + (0x020 * n))
#define L3_PM_ADDR_MATCH(n)		(0x060 + (0x020 * n))

/* L3 error log bit fields. Common for IA and TA */
#define L3_ERROR_LOG_CODE		24
#define L3_ERROR_LOG_INITID		8
#define L3_ERROR_LOG_CMD		0

/* L3 agent status bit fields. */
#define L3_AGENT_STATUS_CLEAR_IA	0x10000000
#define L3_AGENT_STATUS_CLEAR_TA	0x01000000

#define OMAP34xx_IRQ_L3_APP		10
#define L3_APPLICATION_ERROR		0x0
#define L3_DEBUG_ERROR			0x1

enum omap3_l3_initiator_id {
	/* LCD has 1 ID */
	OMAP_L3_LCD = 29,
	/* SAD2D has 1 ID */
	OMAP_L3_SAD2D = 28,
	/* MPU has 5 IDs */
	OMAP_L3_IA_MPU_SS_1 = 27,
	OMAP_L3_IA_MPU_SS_2 = 26,
	OMAP_L3_IA_MPU_SS_3 = 25,
	OMAP_L3_IA_MPU_SS_4 = 24,
	OMAP_L3_IA_MPU_SS_5 = 23,
	/* IVA2.2 SS has 3 IDs*/
	OMAP_L3_IA_IVA_SS_1 = 22,
	OMAP_L3_IA_IVA_SS_2 = 21,
	OMAP_L3_IA_IVA_SS_3 = 20,
	/* IVA 2.2 SS DMA has 6 IDS */
	OMAP_L3_IA_IVA_SS_DMA_1 = 19,
	OMAP_L3_IA_IVA_SS_DMA_2 = 18,
	OMAP_L3_IA_IVA_SS_DMA_3 = 17,
	OMAP_L3_IA_IVA_SS_DMA_4 = 16,
	OMAP_L3_IA_IVA_SS_DMA_5 = 15,
	OMAP_L3_IA_IVA_SS_DMA_6 = 14,
	/* SGX has 1 ID */
	OMAP_L3_IA_SGX = 13,
	/* CAM has 3 ID */
	OMAP_L3_IA_CAM_1 = 12,
	OMAP_L3_IA_CAM_2 = 11,
	OMAP_L3_IA_CAM_3 = 10,
	/* DAP has 1 ID */
	OMAP_L3_IA_DAP = 9,
	/* SDMA WR has 2 IDs */
	OMAP_L3_SDMA_WR_1 = 8,
	OMAP_L3_SDMA_WR_2 = 7,
	/* SDMA RD has 4 IDs */
	OMAP_L3_SDMA_RD_1 = 6,
	OMAP_L3_SDMA_RD_2 = 5,
	OMAP_L3_SDMA_RD_3 = 4,
	OMAP_L3_SDMA_RD_4 = 3,
	/* HSUSB OTG has 1 ID */
	OMAP_L3_USBOTG = 2,
	/* HSUSB HOST has 1 ID */
	OMAP_L3_USBHOST = 1,
};

enum omap3_l3_code {
	OMAP_L3_CODE_NOERROR = 0,
	OMAP_L3_CODE_UNSUP_CMD = 1,
	OMAP_L3_CODE_ADDR_HOLE = 2,
	OMAP_L3_CODE_PROTECT_VIOLATION = 3,
	OMAP_L3_CODE_IN_BAND_ERR = 4,
	/* codes 5 and 6 are reserved */
	OMAP_L3_CODE_REQ_TOUT_NOT_ACCEPT = 7,
	OMAP_L3_CODE_REQ_TOUT_NO_RESP = 8,
	/* codes 9 - 15 are also reserved */
};

struct omap3_l3 {
	struct device *dev;
	struct clk *ick;

	/* memory base*/
	void __iomem *rt;

	int debug_irq;
	int app_irq;

	/* true when and inband functional error occurs */
	unsigned inband:1;
};

/* offsets for l3 agents in order with the Flag status register */
static unsigned int omap3_l3_app_bases[] = {
	/* MPU IA */
	0x1400,
	0x1400,
	0x1400,
	/* RESERVED */
	0,
	0,
	0,
	/* IVA 2.2 IA */
	0x1800,
	0x1800,
	0x1800,
	/* SGX IA */
	0x1c00,
	0x1c00,
	/* RESERVED */
	0,
	/* CAMERA IA */
	0x5800,
	0x5800,
	0x5800,
	/* DISPLAY IA */
	0x5400,
	0x5400,
	/* RESERVED */
	0,
	/*SDMA RD IA */
	0x4c00,
	0x4c00,
	/* RESERVED */
	0,
	/* SDMA WR IA */
	0x5000,
	0x5000,
	/* RESERVED */
	0,
	/* USB OTG IA */
	0x4400,
	0x4400,
	0x4400,
	/* USB HOST IA */
	0x4000,
	0x4000,
	/* RESERVED */
	0,
	0,
	0,
	0,
	/* SAD2D IA */
	0x3000,
	0x3000,
	0x3000,
	/* RESERVED */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* SMA TA */
	0x2000,
	/* GPMC TA */
	0x2400,
	/* OCM RAM TA */
	0x2800,
	/* OCM ROM TA */
	0x2C00,
	/* L4 CORE TA */
	0x6800,
	/* L4 PER TA */
	0x6c00,
	/* IVA 2.2 TA */
	0x6000,
	/* SGX TA */
	0x6400,
	/* L4 EMU TA */
	0x7000,
	/* GPMC TA */
	0x2400,
	/* L4 CORE TA */
	0x6800,
	/* L4 PER TA */
	0x6c00,
	/* L4 EMU TA */
	0x7000,
	/* MAD2D TA */
	0x3400,
	/* RESERVED */
	0,
	0,
};

static unsigned int omap3_l3_debug_bases[] = {
	/* MPU DATA IA */
	0x1400,
	/* RESERVED */
	0,
	0,
	/* DAP IA */
	0x5c00,
	0x5c00,
	/* RESERVED */
	0,
	/* IVA 2.2 IA */
	0x1800,
	/* REST RESERVED */
};

static u32 *omap3_l3_bases[] = {
	omap3_l3_app_bases,
	omap3_l3_debug_bases,
};

/*
 * REVISIT define __raw_readll/__raw_writell here, but move them to
 * <asm/io.h> at some point
 */
#define __raw_writell(v, a)	(__chk_io_ptr(a), \
				*(volatile u64 __force *)(a) = (v))
#define __raw_readll(a)		(__chk_io_ptr(a), \
				*(volatile u64 __force *)(a))

#endif
