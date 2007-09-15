/******************************************************************************
 *
 * Name:	skgehw.h
 * Project:	Gigabit Ethernet Adapters, Common Modules
 * Version:	$Revision: 1.56 $
 * Date:	$Date: 2003/09/23 09:01:00 $
 * Purpose:	Defines and Macros for the Gigabit Ethernet Adapter Product Family
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#ifndef __INC_SKGEHW_H
#define __INC_SKGEHW_H

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* defines ********************************************************************/

#define BIT_31		(1UL << 31)
#define BIT_30		(1L << 30)
#define BIT_29		(1L << 29)
#define BIT_28		(1L << 28)
#define BIT_27		(1L << 27)
#define BIT_26		(1L << 26)
#define BIT_25		(1L << 25)
#define BIT_24		(1L << 24)
#define BIT_23		(1L << 23)
#define BIT_22		(1L << 22)
#define BIT_21		(1L << 21)
#define BIT_20		(1L << 20)
#define BIT_19		(1L << 19)
#define BIT_18		(1L << 18)
#define BIT_17		(1L << 17)
#define BIT_16		(1L << 16)
#define BIT_15		(1L << 15)
#define BIT_14		(1L << 14)
#define BIT_13		(1L << 13)
#define BIT_12		(1L << 12)
#define BIT_11		(1L << 11)
#define BIT_10		(1L << 10)
#define BIT_9		(1L << 9)
#define BIT_8		(1L << 8)
#define BIT_7		(1L << 7)
#define BIT_6		(1L << 6)
#define BIT_5		(1L << 5)
#define BIT_4		(1L << 4)
#define BIT_3		(1L << 3)
#define BIT_2		(1L << 2)
#define BIT_1		(1L << 1)
#define BIT_0		1L

#define BIT_15S		(1U << 15)
#define BIT_14S		(1 << 14)
#define BIT_13S		(1 << 13)
#define BIT_12S		(1 << 12)
#define BIT_11S		(1 << 11)
#define BIT_10S		(1 << 10)
#define BIT_9S		(1 << 9)
#define BIT_8S		(1 << 8)
#define BIT_7S 		(1 << 7)
#define BIT_6S		(1 << 6)
#define BIT_5S		(1 << 5)
#define BIT_4S		(1 << 4)
#define BIT_3S		(1 << 3)
#define BIT_2S		(1 << 2)
#define BIT_1S		(1 << 1)
#define BIT_0S		1

#define SHIFT31(x)	((x) << 31)
#define SHIFT30(x)	((x) << 30)
#define SHIFT29(x)	((x) << 29)
#define SHIFT28(x)	((x) << 28)
#define SHIFT27(x)	((x) << 27)
#define SHIFT26(x)	((x) << 26)
#define SHIFT25(x)	((x) << 25)
#define SHIFT24(x)	((x) << 24)
#define SHIFT23(x)	((x) << 23)
#define SHIFT22(x)	((x) << 22)
#define SHIFT21(x)	((x) << 21)
#define SHIFT20(x)	((x) << 20)
#define SHIFT19(x)	((x) << 19)
#define SHIFT18(x)	((x) << 18)
#define SHIFT17(x)	((x) << 17)
#define SHIFT16(x)	((x) << 16)
#define SHIFT15(x)	((x) << 15)
#define SHIFT14(x)	((x) << 14)
#define SHIFT13(x)	((x) << 13)
#define SHIFT12(x)	((x) << 12)
#define SHIFT11(x)	((x) << 11)
#define SHIFT10(x)	((x) << 10)
#define SHIFT9(x)	((x) << 9)
#define SHIFT8(x)	((x) << 8)
#define SHIFT7(x)	((x) << 7)
#define SHIFT6(x)	((x) << 6)
#define SHIFT5(x)	((x) << 5)
#define SHIFT4(x)	((x) << 4)
#define SHIFT3(x)	((x) << 3)
#define SHIFT2(x)	((x) << 2)
#define SHIFT1(x)	((x) << 1)
#define SHIFT0(x)	((x) << 0)

/*
 * Configuration Space header
 * Since this module is used for different OS', those may be
 * duplicate on some of them (e.g. Linux). But to keep the
 * common source, we have to live with this...
 */
#define PCI_VENDOR_ID	0x00	/* 16 bit	Vendor ID */
#define PCI_DEVICE_ID	0x02	/* 16 bit	Device ID */
#define PCI_COMMAND		0x04	/* 16 bit	Command */
#define PCI_STATUS		0x06	/* 16 bit	Status */
#define PCI_REV_ID		0x08	/*  8 bit	Revision ID */
#define PCI_CLASS_CODE	0x09	/* 24 bit	Class Code */
#define PCI_CACHE_LSZ	0x0c	/*  8 bit	Cache Line Size */
#define PCI_LAT_TIM		0x0d	/*  8 bit	Latency Timer */
#define PCI_HEADER_T	0x0e	/*  8 bit	Header Type */
#define PCI_BIST		0x0f	/*  8 bit	Built-in selftest */
#define PCI_BASE_1ST	0x10	/* 32 bit	1st Base address */
#define PCI_BASE_2ND	0x14	/* 32 bit	2nd Base address */
	/* Byte 0x18..0x2b:	reserved */
#define PCI_SUB_VID		0x2c	/* 16 bit	Subsystem Vendor ID */
#define PCI_SUB_ID		0x2e	/* 16 bit	Subsystem ID */
#define PCI_BASE_ROM	0x30	/* 32 bit	Expansion ROM Base Address */
#define PCI_CAP_PTR		0x34	/*  8 bit 	Capabilities Ptr */
	/* Byte 0x35..0x3b:	reserved */
#define PCI_IRQ_LINE	0x3c	/*  8 bit	Interrupt Line */
#define PCI_IRQ_PIN		0x3d	/*  8 bit	Interrupt Pin */
#define PCI_MIN_GNT		0x3e	/*  8 bit	Min_Gnt */
#define PCI_MAX_LAT		0x3f	/*  8 bit	Max_Lat */
	/* Device Dependent Region */
#define PCI_OUR_REG_1	0x40	/* 32 bit 	Our Register 1 */
#define PCI_OUR_REG_2	0x44	/* 32 bit 	Our Register 2 */
	/* Power Management Region */
#define PCI_PM_CAP_ID	0x48	/*  8 bit 	Power Management Cap. ID */
#define PCI_PM_NITEM	0x49	/*  8 bit 	Next Item Ptr */
#define PCI_PM_CAP_REG	0x4a	/* 16 bit 	Power Management Capabilities */
#define PCI_PM_CTL_STS	0x4c	/* 16 bit 	Power Manag. Control/Status */
	/* Byte 0x4e:	reserved */
#define PCI_PM_DAT_REG	0x4f	/*  8 bit 	Power Manag. Data Register */
	/* VPD Region */
#define PCI_VPD_CAP_ID	0x50	/*  8 bit 	VPD Cap. ID */
#define PCI_VPD_NITEM	0x51	/*  8 bit 	Next Item Ptr */
#define PCI_VPD_ADR_REG	0x52	/* 16 bit 	VPD Address Register */
#define PCI_VPD_DAT_REG	0x54	/* 32 bit 	VPD Data Register */
	/* Byte 0x58..0x59:	reserved */
#define PCI_SER_LD_CTRL	0x5a	/* 16 bit 	SEEPROM Loader Ctrl (YUKON only) */
	/* Byte 0x5c..0xff:	reserved */

/*
 * I2C Address (PCI Config)
 *
 * Note: The temperature and voltage sensors are relocated on a different
 *	 I2C bus.
 */
#define I2C_ADDR_VPD	0xa0	/* I2C address for the VPD EEPROM */

/*
 * Define Bits and Values of the registers
 */
/*	PCI_COMMAND	16 bit	Command */
								/* Bit 15..11:	reserved */
#define PCI_INT_DIS		BIT_10S		/* Interrupt INTx# disable (PCI 2.3) */
#define PCI_FBTEN		BIT_9S		/* Fast Back-To-Back enable */
#define PCI_SERREN		BIT_8S		/* SERR enable */
#define PCI_ADSTEP		BIT_7S		/* Address Stepping */
#define PCI_PERREN		BIT_6S		/* Parity Report Response enable */
#define PCI_VGA_SNOOP	BIT_5S		/* VGA palette snoop */
#define PCI_MWIEN		BIT_4S		/* Memory write an inv cycl ena */
#define PCI_SCYCEN		BIT_3S		/* Special Cycle enable */
#define PCI_BMEN		BIT_2S		/* Bus Master enable */
#define PCI_MEMEN		BIT_1S		/* Memory Space Access enable */
#define PCI_IOEN		BIT_0S		/* I/O Space Access enable */

#define PCI_COMMAND_VAL	(PCI_FBTEN | PCI_SERREN | PCI_PERREN | PCI_MWIEN |\
						 PCI_BMEN | PCI_MEMEN | PCI_IOEN)

/*	PCI_STATUS	16 bit	Status */
#define PCI_PERR		BIT_15S		/* Parity Error */
#define PCI_SERR		BIT_14S		/* Signaled SERR */
#define PCI_RMABORT		BIT_13S		/* Received Master Abort */
#define PCI_RTABORT		BIT_12S		/* Received Target Abort */
								/* Bit 11:	reserved */
#define PCI_DEVSEL		(3<<9)		/* Bit 10.. 9:	DEVSEL Timing */
#define PCI_DEV_FAST	(0<<9)		/*		fast */
#define PCI_DEV_MEDIUM	(1<<9)		/*		medium */
#define PCI_DEV_SLOW	(2<<9)		/*		slow */
#define PCI_DATAPERR	BIT_8S		/* DATA Parity error detected */
#define PCI_FB2BCAP		BIT_7S		/* Fast Back-to-Back Capability */
#define PCI_UDF			BIT_6S		/* User Defined Features */
#define PCI_66MHZCAP	BIT_5S		/* 66 MHz PCI bus clock capable */
#define PCI_NEWCAP		BIT_4S		/* New cap. list implemented */
#define PCI_INT_STAT	BIT_3S		/* Interrupt INTx# Status (PCI 2.3) */
								/* Bit  2.. 0:	reserved */

#define PCI_ERRBITS	(PCI_PERR | PCI_SERR | PCI_RMABORT | PCI_RTABORT |\
			PCI_DATAPERR)

/*	PCI_CLASS_CODE	24 bit	Class Code */
/*	Byte 2:		Base Class		(02) */
/*	Byte 1:		SubClass		(00) */
/*	Byte 0:		Programming Interface	(00) */

/*	PCI_CACHE_LSZ	8 bit	Cache Line Size */
/*	Possible values: 0,2,4,8,16,32,64,128	*/

/*	PCI_HEADER_T	8 bit	Header Type */
#define PCI_HD_MF_DEV	BIT_7S	/* 0= single, 1= multi-func dev */
#define PCI_HD_TYPE		0x7f	/* Bit 6..0:	Header Layout 0= normal */

/*	PCI_BIST	8 bit	Built-in selftest */
/*	Built-in Self test not supported (optional) */

/*	PCI_BASE_1ST	32 bit	1st Base address */
#define PCI_MEMSIZE		0x4000L		/* use 16 kB Memory Base */
#define PCI_MEMBASE_MSK 0xffffc000L	/* Bit 31..14:	Memory Base Address */
#define PCI_MEMSIZE_MSK 0x00003ff0L	/* Bit 13.. 4:	Memory Size Req. */
#define PCI_PREFEN		BIT_3		/* Prefetchable */
#define PCI_MEM_TYP		(3L<<2)		/* Bit	2.. 1:	Memory Type */
#define PCI_MEM32BIT	(0L<<1)		/* Base addr anywhere in 32 Bit range */
#define PCI_MEM1M		(1L<<1)		/* Base addr below 1 MegaByte */
#define PCI_MEM64BIT	(2L<<1)		/* Base addr anywhere in 64 Bit range */
#define PCI_MEMSPACE	BIT_0		/* Memory Space Indicator */

/*	PCI_BASE_2ND	32 bit	2nd Base address */
#define PCI_IOBASE		0xffffff00L	/* Bit 31.. 8:	I/O Base address */
#define PCI_IOSIZE		0x000000fcL	/* Bit	7.. 2:	I/O Size Requirements */
									/* Bit	1:	reserved */
#define PCI_IOSPACE		BIT_0		/* I/O Space Indicator */

/*	PCI_BASE_ROM	32 bit	Expansion ROM Base Address */
#define PCI_ROMBASE_MSK	0xfffe0000L	/* Bit 31..17:	ROM Base address */
#define PCI_ROMBASE_SIZ	(0x1cL<<14)	/* Bit 16..14:	Treat as Base or Size */
#define PCI_ROMSIZE		(0x38L<<11)	/* Bit 13..11:	ROM Size Requirements */
									/* Bit 10.. 1:	reserved */
#define PCI_ROMEN		BIT_0		/* Address Decode enable */

/* Device Dependent Region */
/*	PCI_OUR_REG_1		32 bit	Our Register 1 */
									/* Bit 31..29:	reserved */
#define PCI_PHY_COMA	BIT_28		/* Set PHY to Coma Mode (YUKON only) */
#define PCI_TEST_CAL	BIT_27		/* Test PCI buffer calib. (YUKON only) */
#define PCI_EN_CAL		BIT_26		/* Enable PCI buffer calib. (YUKON only) */
#define PCI_VIO			BIT_25		/* PCI I/O Voltage, 0 = 3.3V, 1 = 5V */
#define PCI_DIS_BOOT	BIT_24		/* Disable BOOT via ROM */
#define PCI_EN_IO		BIT_23		/* Mapping to I/O space */
#define PCI_EN_FPROM	BIT_22		/* Enable FLASH mapping to memory */
									/*		1 = Map Flash to memory */
									/*		0 = Disable addr. dec */
#define PCI_PAGESIZE	(3L<<20)	/* Bit 21..20:	FLASH Page Size	*/
#define PCI_PAGE_16		(0L<<20)	/*		16 k pages	*/
#define PCI_PAGE_32K	(1L<<20)	/*		32 k pages	*/
#define PCI_PAGE_64K	(2L<<20)	/*		64 k pages	*/
#define PCI_PAGE_128K	(3L<<20)	/*		128 k pages	*/
									/* Bit 19:	reserved	*/
#define PCI_PAGEREG		(7L<<16)	/* Bit 18..16:	Page Register	*/
#define PCI_NOTAR		BIT_15		/* No turnaround cycle */
#define PCI_FORCE_BE	BIT_14		/* Assert all BEs on MR */
#define PCI_DIS_MRL		BIT_13		/* Disable Mem Read Line */
#define PCI_DIS_MRM		BIT_12		/* Disable Mem Read Multiple */
#define PCI_DIS_MWI		BIT_11		/* Disable Mem Write & Invalidate */
#define PCI_DISC_CLS	BIT_10		/* Disc: cacheLsz bound */
#define PCI_BURST_DIS	BIT_9		/* Burst Disable */
#define PCI_DIS_PCI_CLK	BIT_8		/* Disable PCI clock driving */
#define PCI_SKEW_DAS	(0xfL<<4)	/* Bit	7.. 4:	Skew Ctrl, DAS Ext */
#define PCI_SKEW_BASE	0xfL		/* Bit	3.. 0:	Skew Ctrl, Base	*/


/*	PCI_OUR_REG_2		32 bit	Our Register 2 */
#define PCI_VPD_WR_THR	(0xffL<<24)	/* Bit 31..24:	VPD Write Threshold */
#define PCI_DEV_SEL		(0x7fL<<17)	/* Bit 23..17:	EEPROM Device Select */
#define PCI_VPD_ROM_SZ	(7L<<14)	/* Bit 16..14:	VPD ROM Size	*/
									/* Bit 13..12:	reserved	*/
#define PCI_PATCH_DIR	(0xfL<<8)	/* Bit 11.. 8:	Ext Patches dir 3..0 */
#define PCI_PATCH_DIR_3	BIT_11
#define PCI_PATCH_DIR_2	BIT_10
#define PCI_PATCH_DIR_1	BIT_9
#define PCI_PATCH_DIR_0	BIT_8
#define PCI_EXT_PATCHS	(0xfL<<4)	/* Bit	7.. 4:	Extended Patches 3..0 */
#define PCI_EXT_PATCH_3	BIT_7
#define PCI_EXT_PATCH_2	BIT_6
#define PCI_EXT_PATCH_1	BIT_5
#define PCI_EXT_PATCH_0	BIT_4
#define PCI_EN_DUMMY_RD	BIT_3		/* Enable Dummy Read */
#define PCI_REV_DESC	BIT_2		/* Reverse Desc. Bytes */
									/* Bit	1:	reserved */
#define PCI_USEDATA64	BIT_0		/* Use 64Bit Data bus ext */


/* Power Management Region */
/*	PCI_PM_CAP_REG		16 bit	Power Management Capabilities */
#define PCI_PME_SUP_MSK	(0x1f<<11)	/* Bit 15..11:	PM Event Support Mask */
#define PCI_PME_D3C_SUP	BIT_15S		/* PME from D3cold Support (if Vaux) */
#define PCI_PME_D3H_SUP	BIT_14S		/* PME from D3hot Support */
#define PCI_PME_D2_SUP	BIT_13S		/* PME from D2 Support */
#define PCI_PME_D1_SUP	BIT_12S		/* PME from D1 Support */
#define PCI_PME_D0_SUP	BIT_11S		/* PME from D0 Support */
#define PCI_PM_D2_SUP	BIT_10S		/* D2 Support in 33 MHz mode */
#define PCI_PM_D1_SUP	BIT_9S		/* D1 Support */
									/* Bit	8.. 6:	reserved */
#define PCI_PM_DSI		BIT_5S		/* Device Specific Initialization */
#define PCI_PM_APS		BIT_4S		/* Auxialiary Power Source */
#define PCI_PME_CLOCK	BIT_3S		/* PM Event Clock */
#define PCI_PM_VER_MSK		7		/* Bit	2.. 0:	PM PCI Spec. version */

/*	PCI_PM_CTL_STS		16 bit	Power Management Control/Status */
#define PCI_PME_STATUS	BIT_15S		/* PME Status (YUKON only) */
#define PCI_PM_DAT_SCL	(3<<13)		/* Bit 14..13:	Data Reg. scaling factor */
#define PCI_PM_DAT_SEL	(0xf<<9)	/* Bit 12.. 9:	PM data selector field */
#define PCI_PME_EN		BIT_8S		/* Enable PME# generation (YUKON only) */
									/* Bit	7.. 2:	reserved */
#define PCI_PM_STATE_MSK	3		/* Bit	1.. 0:	Power Management State */

#define PCI_PM_STATE_D0		0		/* D0:	Operational (default) */
#define PCI_PM_STATE_D1		1		/* D1:	(YUKON only) */
#define PCI_PM_STATE_D2		2		/* D2:	(YUKON only) */
#define PCI_PM_STATE_D3 	3		/* D3:	HOT, Power Down and Reset */

/* VPD Region */
/*	PCI_VPD_ADR_REG		16 bit	VPD Address Register */
#define PCI_VPD_FLAG	BIT_15S		/* starts VPD rd/wr cycle */
#define PCI_VPD_ADR_MSK	0x7fffL		/* Bit 14.. 0:	VPD address mask */

/*	Control Register File (Address Map) */

/*
 *	Bank 0
 */
#define B0_RAP			0x0000	/*  8 bit	Register Address Port */
	/* 0x0001 - 0x0003:	reserved */
#define B0_CTST			0x0004	/* 16 bit	Control/Status register */
#define B0_LED			0x0006	/*  8 Bit	LED register */
#define B0_POWER_CTRL	0x0007	/*  8 Bit	Power Control reg (YUKON only) */
#define B0_ISRC			0x0008	/* 32 bit	Interrupt Source Register */
#define B0_IMSK			0x000c	/* 32 bit	Interrupt Mask Register */
#define B0_HWE_ISRC		0x0010	/* 32 bit	HW Error Interrupt Src Reg */
#define B0_HWE_IMSK		0x0014	/* 32 bit	HW Error Interrupt Mask Reg */
#define B0_SP_ISRC		0x0018	/* 32 bit	Special Interrupt Source Reg */
	/* 0x001c:		reserved */

/* B0 XMAC 1 registers (GENESIS only) */
#define B0_XM1_IMSK		0x0020	/* 16 bit r/w	XMAC 1 Interrupt Mask Register*/
	/* 0x0022 - 0x0027:	reserved */
#define B0_XM1_ISRC		0x0028	/* 16 bit ro	XMAC 1 Interrupt Status Reg */
	/* 0x002a - 0x002f:	reserved */
#define B0_XM1_PHY_ADDR 0x0030	/* 16 bit r/w	XMAC 1 PHY Address Register */
	/* 0x0032 - 0x0033:	reserved */
#define B0_XM1_PHY_DATA 0x0034	/* 16 bit r/w	XMAC 1 PHY Data Register */
	/* 0x0036 - 0x003f:	reserved */

/* B0 XMAC 2 registers (GENESIS only) */
#define B0_XM2_IMSK		0x0040	/* 16 bit r/w	XMAC 2 Interrupt Mask Register*/
	/* 0x0042 - 0x0047:	reserved */
#define B0_XM2_ISRC		0x0048	/* 16 bit ro	XMAC 2 Interrupt Status Reg */
	/* 0x004a - 0x004f:	reserved */
#define B0_XM2_PHY_ADDR 0x0050	/* 16 bit r/w	XMAC 2 PHY Address Register */
	/* 0x0052 - 0x0053:	reserved */
#define B0_XM2_PHY_DATA 0x0054	/* 16 bit r/w	XMAC 2 PHY Data Register */
	/* 0x0056 - 0x005f:	reserved */

/* BMU Control Status Registers */
#define B0_R1_CSR		0x0060	/* 32 bit	BMU Ctrl/Stat Rx Queue 1 */
#define B0_R2_CSR		0x0064	/* 32 bit	BMU Ctrl/Stat Rx Queue 2 */
#define B0_XS1_CSR		0x0068	/* 32 bit	BMU Ctrl/Stat Sync Tx Queue 1 */
#define B0_XA1_CSR		0x006c	/* 32 bit	BMU Ctrl/Stat Async Tx Queue 1*/
#define B0_XS2_CSR		0x0070	/* 32 bit	BMU Ctrl/Stat Sync Tx Queue 2 */
#define B0_XA2_CSR		0x0074	/* 32 bit	BMU Ctrl/Stat Async Tx Queue 2*/
	/* 0x0078 - 0x007f:	reserved */

/*
 *	Bank 1
 *	- completely empty (this is the RAP Block window)
 *	Note: if RAP = 1 this page is reserved
 */

/*
 *	Bank 2
 */
/* NA reg = 48 bit Network Address Register, 3x16 or 8x8 bit readable */
#define B2_MAC_1		0x0100	/* NA reg	 MAC Address 1 */
	/* 0x0106 - 0x0107:	reserved */
#define B2_MAC_2		0x0108	/* NA reg	 MAC Address 2 */
	/* 0x010e - 0x010f:	reserved */
#define B2_MAC_3		0x0110	/* NA reg	 MAC Address 3 */
	/* 0x0116 - 0x0117:	reserved */
#define B2_CONN_TYP		0x0118	/*  8 bit	Connector type */
#define B2_PMD_TYP		0x0119	/*  8 bit	PMD type */
#define B2_MAC_CFG		0x011a	/*  8 bit	MAC Configuration / Chip Revision */
#define B2_CHIP_ID		0x011b	/*  8 bit 	Chip Identification Number */
	/* Eprom registers are currently of no use */
#define B2_E_0			0x011c	/*  8 bit	EPROM Byte 0 (ext. SRAM size */
#define B2_E_1			0x011d	/*  8 bit	EPROM Byte 1 (PHY type) */
#define B2_E_2			0x011e	/*  8 bit	EPROM Byte 2 */
#define B2_E_3			0x011f	/*  8 bit	EPROM Byte 3 */
#define B2_FAR			0x0120	/* 32 bit	Flash-Prom Addr Reg/Cnt */
#define B2_FDP			0x0124	/*  8 bit	Flash-Prom Data Port */
	/* 0x0125 - 0x0127:	reserved */
#define B2_LD_CTRL		0x0128	/*  8 bit	EPROM loader control register */
#define B2_LD_TEST		0x0129	/*  8 bit	EPROM loader test register */
	/* 0x012a - 0x012f:	reserved */
#define B2_TI_INI		0x0130	/* 32 bit	Timer Init Value */
#define B2_TI_VAL		0x0134	/* 32 bit	Timer Value */
#define B2_TI_CTRL		0x0138	/*  8 bit	Timer Control */
#define B2_TI_TEST		0x0139	/*  8 Bit	Timer Test */
	/* 0x013a - 0x013f:	reserved */
#define B2_IRQM_INI		0x0140	/* 32 bit	IRQ Moderation Timer Init Reg.*/
#define B2_IRQM_VAL		0x0144	/* 32 bit	IRQ Moderation Timer Value */
#define B2_IRQM_CTRL	0x0148	/*  8 bit	IRQ Moderation Timer Control */
#define B2_IRQM_TEST	0x0149	/*  8 bit	IRQ Moderation Timer Test */
#define B2_IRQM_MSK 	0x014c	/* 32 bit	IRQ Moderation Mask */
#define B2_IRQM_HWE_MSK 0x0150	/* 32 bit	IRQ Moderation HW Error Mask */
	/* 0x0154 - 0x0157:	reserved */
#define B2_TST_CTRL1	0x0158	/*  8 bit	Test Control Register 1 */
#define B2_TST_CTRL2	0x0159	/*  8 bit	Test Control Register 2 */
	/* 0x015a - 0x015b:	reserved */
#define B2_GP_IO		0x015c	/* 32 bit	General Purpose I/O Register */
#define B2_I2C_CTRL		0x0160	/* 32 bit	I2C HW Control Register */
#define B2_I2C_DATA		0x0164	/* 32 bit	I2C HW Data Register */
#define B2_I2C_IRQ		0x0168	/* 32 bit	I2C HW IRQ Register */
#define B2_I2C_SW		0x016c	/* 32 bit	I2C SW Port Register */

/* Blink Source Counter (GENESIS only) */
#define B2_BSC_INI		0x0170	/* 32 bit	Blink Source Counter Init Val */
#define B2_BSC_VAL		0x0174	/* 32 bit	Blink Source Counter Value */
#define B2_BSC_CTRL		0x0178	/*  8 bit	Blink Source Counter Control */
#define B2_BSC_STAT		0x0179	/*  8 bit	Blink Source Counter Status */
#define B2_BSC_TST		0x017a	/* 16 bit	Blink Source Counter Test Reg */
	/* 0x017c - 0x017f:	reserved */

/*
 *	Bank 3
 */
/* RAM Random Registers */
#define B3_RAM_ADDR		0x0180	/* 32 bit	RAM Address, to read or write */
#define B3_RAM_DATA_LO	0x0184	/* 32 bit	RAM Data Word (low dWord) */
#define B3_RAM_DATA_HI	0x0188	/* 32 bit	RAM Data Word (high dWord) */
	/* 0x018c - 0x018f:	reserved */

/* RAM Interface Registers */
/*
 * The HW-Spec. calls this registers Timeout Value 0..11. But this names are
 * not usable in SW. Please notice these are NOT real timeouts, these are
 * the number of qWords transferred continuously.
 */
#define B3_RI_WTO_R1	0x0190	/*  8 bit	WR Timeout Queue R1		(TO0) */
#define B3_RI_WTO_XA1	0x0191	/*  8 bit	WR Timeout Queue XA1	(TO1) */
#define B3_RI_WTO_XS1	0x0192	/*  8 bit	WR Timeout Queue XS1	(TO2) */
#define B3_RI_RTO_R1	0x0193	/*  8 bit	RD Timeout Queue R1		(TO3) */
#define B3_RI_RTO_XA1	0x0194	/*  8 bit	RD Timeout Queue XA1	(TO4) */
#define B3_RI_RTO_XS1	0x0195	/*  8 bit	RD Timeout Queue XS1	(TO5) */
#define B3_RI_WTO_R2	0x0196	/*  8 bit	WR Timeout Queue R2		(TO6) */
#define B3_RI_WTO_XA2	0x0197	/*  8 bit	WR Timeout Queue XA2	(TO7) */
#define B3_RI_WTO_XS2	0x0198	/*  8 bit	WR Timeout Queue XS2	(TO8) */
#define B3_RI_RTO_R2	0x0199	/*  8 bit	RD Timeout Queue R2		(TO9) */
#define B3_RI_RTO_XA2	0x019a	/*  8 bit	RD Timeout Queue XA2	(TO10)*/
#define B3_RI_RTO_XS2	0x019b	/*  8 bit	RD Timeout Queue XS2	(TO11)*/
#define B3_RI_TO_VAL	0x019c	/*  8 bit	Current Timeout Count Val */
	/* 0x019d - 0x019f:	reserved */
#define B3_RI_CTRL		0x01a0	/* 16 bit	RAM Interface Control Register */
#define B3_RI_TEST		0x01a2	/*  8 bit	RAM Interface Test Register */
	/* 0x01a3 - 0x01af:	reserved */

/* MAC Arbiter Registers (GENESIS only) */
/* these are the no. of qWord transferred continuously and NOT real timeouts */
#define B3_MA_TOINI_RX1	0x01b0	/*  8 bit	Timeout Init Val Rx Path MAC 1 */
#define B3_MA_TOINI_RX2	0x01b1	/*  8 bit	Timeout Init Val Rx Path MAC 2 */
#define B3_MA_TOINI_TX1	0x01b2	/*  8 bit	Timeout Init Val Tx Path MAC 1 */
#define B3_MA_TOINI_TX2	0x01b3	/*  8 bit	Timeout Init Val Tx Path MAC 2 */
#define B3_MA_TOVAL_RX1	0x01b4	/*  8 bit	Timeout Value Rx Path MAC 1 */
#define B3_MA_TOVAL_RX2	0x01b5	/*  8 bit	Timeout Value Rx Path MAC 1 */
#define B3_MA_TOVAL_TX1	0x01b6	/*  8 bit	Timeout Value Tx Path MAC 2 */
#define B3_MA_TOVAL_TX2	0x01b7	/*  8 bit	Timeout Value Tx Path MAC 2 */
#define B3_MA_TO_CTRL	0x01b8	/* 16 bit	MAC Arbiter Timeout Ctrl Reg */
#define B3_MA_TO_TEST	0x01ba	/* 16 bit	MAC Arbiter Timeout Test Reg */
	/* 0x01bc - 0x01bf:	reserved */
#define B3_MA_RCINI_RX1	0x01c0	/*  8 bit	Recovery Init Val Rx Path MAC 1 */
#define B3_MA_RCINI_RX2	0x01c1	/*  8 bit	Recovery Init Val Rx Path MAC 2 */
#define B3_MA_RCINI_TX1	0x01c2	/*  8 bit	Recovery Init Val Tx Path MAC 1 */
#define B3_MA_RCINI_TX2	0x01c3	/*  8 bit	Recovery Init Val Tx Path MAC 2 */
#define B3_MA_RCVAL_RX1	0x01c4	/*  8 bit	Recovery Value Rx Path MAC 1 */
#define B3_MA_RCVAL_RX2	0x01c5	/*  8 bit	Recovery Value Rx Path MAC 1 */
#define B3_MA_RCVAL_TX1	0x01c6	/*  8 bit	Recovery Value Tx Path MAC 2 */
#define B3_MA_RCVAL_TX2	0x01c7	/*  8 bit	Recovery Value Tx Path MAC 2 */
#define B3_MA_RC_CTRL	0x01c8	/* 16 bit	MAC Arbiter Recovery Ctrl Reg */
#define B3_MA_RC_TEST	0x01ca	/* 16 bit	MAC Arbiter Recovery Test Reg */
	/* 0x01cc - 0x01cf:	reserved */

/* Packet Arbiter Registers (GENESIS only) */
/* these are real timeouts */
#define B3_PA_TOINI_RX1	0x01d0	/* 16 bit	Timeout Init Val Rx Path MAC 1 */
	/* 0x01d2 - 0x01d3:	reserved */
#define B3_PA_TOINI_RX2	0x01d4	/* 16 bit	Timeout Init Val Rx Path MAC 2 */
	/* 0x01d6 - 0x01d7:	reserved */
#define B3_PA_TOINI_TX1	0x01d8	/* 16 bit	Timeout Init Val Tx Path MAC 1 */
	/* 0x01da - 0x01db:	reserved */
#define B3_PA_TOINI_TX2	0x01dc	/* 16 bit	Timeout Init Val Tx Path MAC 2 */
	/* 0x01de - 0x01df:	reserved */
#define B3_PA_TOVAL_RX1	0x01e0	/* 16 bit	Timeout Val Rx Path MAC 1 */
	/* 0x01e2 - 0x01e3:	reserved */
#define B3_PA_TOVAL_RX2	0x01e4	/* 16 bit	Timeout Val Rx Path MAC 2 */
	/* 0x01e6 - 0x01e7:	reserved */
#define B3_PA_TOVAL_TX1	0x01e8	/* 16 bit	Timeout Val Tx Path MAC 1 */
	/* 0x01ea - 0x01eb:	reserved */
#define B3_PA_TOVAL_TX2	0x01ec	/* 16 bit	Timeout Val Tx Path MAC 2 */
	/* 0x01ee - 0x01ef:	reserved */
#define B3_PA_CTRL	0x01f0	/* 16 bit	Packet Arbiter Ctrl Register */
#define B3_PA_TEST	0x01f2	/* 16 bit	Packet Arbiter Test Register */
	/* 0x01f4 - 0x01ff:	reserved */

/*
 *	Bank 4 - 5
 */
/* Transmit Arbiter Registers MAC 1 and 2, use MR_ADDR() to access */
#define TXA_ITI_INI		0x0200	/* 32 bit	Tx Arb Interval Timer Init Val*/
#define TXA_ITI_VAL		0x0204	/* 32 bit	Tx Arb Interval Timer Value */
#define TXA_LIM_INI		0x0208	/* 32 bit	Tx Arb Limit Counter Init Val */
#define TXA_LIM_VAL		0x020c	/* 32 bit	Tx Arb Limit Counter Value */
#define TXA_CTRL		0x0210	/*  8 bit	Tx Arbiter Control Register */
#define TXA_TEST		0x0211	/*  8 bit	Tx Arbiter Test Register */
#define TXA_STAT		0x0212	/*  8 bit	Tx Arbiter Status Register */
	/* 0x0213 - 0x027f:	reserved */
	/* 0x0280 - 0x0292:	MAC 2 */
	/* 0x0213 - 0x027f:	reserved */

/*
 *	Bank 6
 */
/* External registers (GENESIS only) */
#define B6_EXT_REG		0x0300

/*
 *	Bank 7
 */
/* This is a copy of the Configuration register file (lower half) */
#define B7_CFG_SPC		0x0380

/*
 *	Bank 8 - 15
 */
/* Receive and Transmit Queue Registers, use Q_ADDR() to access */
#define B8_Q_REGS		0x0400

/* Queue Register Offsets, use Q_ADDR() to access */
#define Q_D		0x00	/* 8*32	bit	Current Descriptor */
#define Q_DA_L	0x20	/* 32 bit	Current Descriptor Address Low dWord */
#define Q_DA_H	0x24	/* 32 bit	Current Descriptor Address High dWord */
#define Q_AC_L	0x28	/* 32 bit	Current Address Counter Low dWord */
#define Q_AC_H	0x2c	/* 32 bit	Current Address Counter High dWord */
#define Q_BC	0x30	/* 32 bit	Current Byte Counter */
#define Q_CSR	0x34	/* 32 bit	BMU Control/Status Register */
#define Q_F		0x38	/* 32 bit	Flag Register */
#define Q_T1	0x3c	/* 32 bit	Test Register 1 */
#define Q_T1_TR	0x3c	/*  8 bit	Test Register 1 Transfer SM */
#define Q_T1_WR	0x3d	/*  8 bit	Test Register 1 Write Descriptor SM */
#define Q_T1_RD	0x3e	/*  8 bit	Test Register 1 Read Descriptor SM */
#define Q_T1_SV	0x3f	/*  8 bit	Test Register 1 Supervisor SM */
#define Q_T2	0x40	/* 32 bit	Test Register 2	*/
#define Q_T3	0x44	/* 32 bit	Test Register 3	*/
	/* 0x48 - 0x7f:	reserved */

/*
 *	Bank 16 - 23
 */
/* RAM Buffer Registers */
#define B16_RAM_REGS	0x0800

/* RAM Buffer Register Offsets, use RB_ADDR() to access */
#define RB_START		0x00	/* 32 bit	RAM Buffer Start Address */
#define RB_END			0x04	/* 32 bit	RAM Buffer End Address */
#define RB_WP			0x08	/* 32 bit	RAM Buffer Write Pointer */
#define RB_RP			0x0c	/* 32 bit	RAM Buffer Read Pointer */
#define RB_RX_UTPP		0x10	/* 32 bit	Rx Upper Threshold, Pause Pack */
#define RB_RX_LTPP		0x14	/* 32 bit	Rx Lower Threshold, Pause Pack */
#define RB_RX_UTHP		0x18	/* 32 bit	Rx Upper Threshold, High Prio */
#define RB_RX_LTHP		0x1c	/* 32 bit	Rx Lower Threshold, High Prio */
	/* 0x10 - 0x1f:	reserved at Tx RAM Buffer Registers */
#define RB_PC			0x20	/* 32 bit	RAM Buffer Packet Counter */
#define RB_LEV			0x24	/* 32 bit	RAM Buffer Level Register */
#define RB_CTRL			0x28	/*  8 bit	RAM Buffer Control Register */
#define RB_TST1			0x29	/*  8 bit	RAM Buffer Test Register 1 */
#define RB_TST2			0x2A	/*  8 bit	RAM Buffer Test Register 2 */
	/* 0x2c - 0x7f:	reserved */

/*
 *	Bank 24
 */
/*
 * Receive MAC FIFO, Receive LED, and Link_Sync regs (GENESIS only)
 * use MR_ADDR() to access
 */
#define RX_MFF_EA		0x0c00	/* 32 bit	Receive MAC FIFO End Address */
#define RX_MFF_WP		0x0c04	/* 32 bit 	Receive MAC FIFO Write Pointer */
	/* 0x0c08 - 0x0c0b:	reserved */
#define RX_MFF_RP		0x0c0c	/* 32 bit	Receive MAC FIFO Read Pointer */
#define RX_MFF_PC		0x0c10	/* 32 bit	Receive MAC FIFO Packet Cnt */
#define RX_MFF_LEV		0x0c14	/* 32 bit	Receive MAC FIFO Level */
#define RX_MFF_CTRL1	0x0c18	/* 16 bit	Receive MAC FIFO Control Reg 1*/
#define RX_MFF_STAT_TO	0x0c1a	/*  8 bit	Receive MAC Status Timeout */
#define RX_MFF_TIST_TO	0x0c1b	/*  8 bit	Receive MAC Time Stamp Timeout */
#define RX_MFF_CTRL2	0x0c1c	/*  8 bit	Receive MAC FIFO Control Reg 2*/
#define RX_MFF_TST1		0x0c1d	/*  8 bit	Receive MAC FIFO Test Reg 1 */
#define RX_MFF_TST2		0x0c1e	/*  8 bit	Receive MAC FIFO Test Reg 2 */
	/* 0x0c1f:	reserved */
#define RX_LED_INI		0x0c20	/* 32 bit	Receive LED Cnt Init Value */
#define RX_LED_VAL		0x0c24	/* 32 bit	Receive LED Cnt Current Value */
#define RX_LED_CTRL		0x0c28	/*  8 bit	Receive LED Cnt Control Reg */
#define RX_LED_TST		0x0c29	/*  8 bit	Receive LED Cnt Test Register */
	/* 0x0c2a - 0x0c2f:	reserved */
#define LNK_SYNC_INI	0x0c30	/* 32 bit	Link Sync Cnt Init Value */
#define LNK_SYNC_VAL	0x0c34	/* 32 bit	Link Sync Cnt Current Value */
#define LNK_SYNC_CTRL	0x0c38	/*  8 bit	Link Sync Cnt Control Register */
#define LNK_SYNC_TST	0x0c39	/*  8 bit	Link Sync Cnt Test Register */
	/* 0x0c3a - 0x0c3b:	reserved */
#define LNK_LED_REG		0x0c3c	/*  8 bit	Link LED Register */
	/* 0x0c3d - 0x0c3f:	reserved */

/* Receive GMAC FIFO (YUKON only), use MR_ADDR() to access */
#define RX_GMF_EA		0x0c40	/* 32 bit	Rx GMAC FIFO End Address */
#define RX_GMF_AF_THR	0x0c44	/* 32 bit	Rx GMAC FIFO Almost Full Thresh. */
#define RX_GMF_CTRL_T	0x0c48	/* 32 bit	Rx GMAC FIFO Control/Test */
#define RX_GMF_FL_MSK	0x0c4c	/* 32 bit	Rx GMAC FIFO Flush Mask */
#define RX_GMF_FL_THR	0x0c50	/* 32 bit	Rx GMAC FIFO Flush Threshold */
	/* 0x0c54 - 0x0c5f:	reserved */
#define RX_GMF_WP		0x0c60	/* 32 bit 	Rx GMAC FIFO Write Pointer */
	/* 0x0c64 - 0x0c67:	reserved */
#define RX_GMF_WLEV		0x0c68	/* 32 bit 	Rx GMAC FIFO Write Level */
	/* 0x0c6c - 0x0c6f:	reserved */
#define RX_GMF_RP		0x0c70	/* 32 bit 	Rx GMAC FIFO Read Pointer */
	/* 0x0c74 - 0x0c77:	reserved */
#define RX_GMF_RLEV		0x0c78	/* 32 bit 	Rx GMAC FIFO Read Level */
	/* 0x0c7c - 0x0c7f:	reserved */

/*
 *	Bank 25
 */
	/* 0x0c80 - 0x0cbf:	MAC 2 */
	/* 0x0cc0 - 0x0cff:	reserved */

/*
 *	Bank 26
 */
/*
 * Transmit MAC FIFO and Transmit LED Registers (GENESIS only),
 * use MR_ADDR() to access
 */
#define TX_MFF_EA		0x0d00	/* 32 bit	Transmit MAC FIFO End Address */
#define TX_MFF_WP		0x0d04	/* 32 bit 	Transmit MAC FIFO WR Pointer */
#define TX_MFF_WSP		0x0d08	/* 32 bit	Transmit MAC FIFO WR Shadow Ptr */
#define TX_MFF_RP		0x0d0c	/* 32 bit	Transmit MAC FIFO RD Pointer */
#define TX_MFF_PC		0x0d10	/* 32 bit	Transmit MAC FIFO Packet Cnt */
#define TX_MFF_LEV		0x0d14	/* 32 bit	Transmit MAC FIFO Level */
#define TX_MFF_CTRL1	0x0d18	/* 16 bit	Transmit MAC FIFO Ctrl Reg 1 */
#define TX_MFF_WAF		0x0d1a	/*  8 bit	Transmit MAC Wait after flush */
	/* 0x0c1b:	reserved */
#define TX_MFF_CTRL2	0x0d1c	/*  8 bit	Transmit MAC FIFO Ctrl Reg 2 */
#define TX_MFF_TST1		0x0d1d	/*  8 bit	Transmit MAC FIFO Test Reg 1 */
#define TX_MFF_TST2		0x0d1e	/*  8 bit	Transmit MAC FIFO Test Reg 2 */
	/* 0x0d1f:	reserved */
#define TX_LED_INI		0x0d20	/* 32 bit	Transmit LED Cnt Init Value */
#define TX_LED_VAL		0x0d24	/* 32 bit	Transmit LED Cnt Current Val */
#define TX_LED_CTRL		0x0d28	/*  8 bit	Transmit LED Cnt Control Reg */
#define TX_LED_TST		0x0d29	/*  8 bit	Transmit LED Cnt Test Reg */
	/* 0x0d2a - 0x0d3f:	reserved */

/* Transmit GMAC FIFO (YUKON only), use MR_ADDR() to access */
#define TX_GMF_EA		0x0d40	/* 32 bit	Tx GMAC FIFO End Address */
#define TX_GMF_AE_THR	0x0d44	/* 32 bit	Tx GMAC FIFO Almost Empty Thresh.*/
#define TX_GMF_CTRL_T	0x0d48	/* 32 bit	Tx GMAC FIFO Control/Test */
	/* 0x0d4c - 0x0d5f:	reserved */
#define TX_GMF_WP		0x0d60	/* 32 bit 	Tx GMAC FIFO Write Pointer */
#define TX_GMF_WSP		0x0d64	/* 32 bit 	Tx GMAC FIFO Write Shadow Ptr. */
#define TX_GMF_WLEV		0x0d68	/* 32 bit 	Tx GMAC FIFO Write Level */
	/* 0x0d6c - 0x0d6f:	reserved */
#define TX_GMF_RP		0x0d70	/* 32 bit 	Tx GMAC FIFO Read Pointer */
#define TX_GMF_RSTP		0x0d74	/* 32 bit 	Tx GMAC FIFO Restart Pointer */
#define TX_GMF_RLEV		0x0d78	/* 32 bit 	Tx GMAC FIFO Read Level */
	/* 0x0d7c - 0x0d7f:	reserved */

/*
 *	Bank 27
 */
	/* 0x0d80 - 0x0dbf:	MAC 2 */
	/* 0x0daa - 0x0dff:	reserved */

/*
 *	Bank 28
 */
/* Descriptor Poll Timer Registers */
#define B28_DPT_INI		0x0e00	/* 24 bit	Descriptor Poll Timer Init Val */
#define B28_DPT_VAL		0x0e04	/* 24 bit	Descriptor Poll Timer Curr Val */
#define B28_DPT_CTRL	0x0e08	/*  8 bit	Descriptor Poll Timer Ctrl Reg */
	/* 0x0e09:	reserved */
#define B28_DPT_TST		0x0e0a	/*  8 bit	Descriptor Poll Timer Test Reg */
	/* 0x0e0b:	reserved */

/* Time Stamp Timer Registers (YUKON only) */
	/* 0x0e10:	reserved */
#define GMAC_TI_ST_VAL	0x0e14	/* 32 bit	Time Stamp Timer Curr Val */
#define GMAC_TI_ST_CTRL	0x0e18	/*  8 bit	Time Stamp Timer Ctrl Reg */
	/* 0x0e19:	reserved */
#define GMAC_TI_ST_TST	0x0e1a	/*  8 bit	Time Stamp Timer Test Reg */
	/* 0x0e1b - 0x0e7f:	reserved */

/*
 *	Bank 29
 */
	/* 0x0e80 - 0x0efc:	reserved */

/*
 *	Bank 30
 */
/* GMAC and GPHY Control Registers (YUKON only) */
#define GMAC_CTRL		0x0f00	/* 32 bit	GMAC Control Reg */
#define GPHY_CTRL		0x0f04	/* 32 bit	GPHY Control Reg */
#define GMAC_IRQ_SRC	0x0f08	/*  8 bit	GMAC Interrupt Source Reg */
	/* 0x0f09 - 0x0f0b:	reserved */
#define GMAC_IRQ_MSK	0x0f0c	/*  8 bit	GMAC Interrupt Mask Reg */
	/* 0x0f0d - 0x0f0f:	reserved */
#define GMAC_LINK_CTRL	0x0f10	/* 16 bit	Link Control Reg */
	/* 0x0f14 - 0x0f1f:	reserved */

/* Wake-up Frame Pattern Match Control Registers (YUKON only) */

#define WOL_REG_OFFS	0x20	/* HW-Bug: Address is + 0x20 against spec. */

#define WOL_CTRL_STAT	0x0f20	/* 16 bit	WOL Control/Status Reg */
#define WOL_MATCH_CTL	0x0f22	/*  8 bit	WOL Match Control Reg */
#define WOL_MATCH_RES	0x0f23	/*  8 bit	WOL Match Result Reg */
#define WOL_MAC_ADDR_LO	0x0f24	/* 32 bit	WOL MAC Address Low */
#define WOL_MAC_ADDR_HI	0x0f28	/* 16 bit	WOL MAC Address High */
#define WOL_PATT_RPTR	0x0f2c	/*  8 bit	WOL Pattern Read Ptr */

/* use this macro to access above registers */
#define WOL_REG(Reg)	((Reg) + (pAC->GIni.GIWolOffs))


/* WOL Pattern Length Registers (YUKON only) */

#define WOL_PATT_LEN_LO	0x0f30		/* 32 bit	WOL Pattern Length 3..0 */
#define WOL_PATT_LEN_HI	0x0f34		/* 24 bit	WOL Pattern Length 6..4 */

/* WOL Pattern Counter Registers (YUKON only) */

#define WOL_PATT_CNT_0	0x0f38		/* 32 bit	WOL Pattern Counter 3..0 */
#define WOL_PATT_CNT_4	0x0f3c		/* 24 bit	WOL Pattern Counter 6..4 */
	/* 0x0f40 - 0x0f7f:	reserved */

/*
 *	Bank 31
 */
/* 0x0f80 - 0x0fff:	reserved */

/*
 *	Bank 32	- 33
 */
#define WOL_PATT_RAM_1	0x1000	/*  WOL Pattern RAM Link 1 */

/*
 *	Bank 0x22 - 0x3f
 */
/* 0x1100 - 0x1fff:	reserved */

/*
 *	Bank 0x40 - 0x4f
 */
#define BASE_XMAC_1		0x2000	/* XMAC 1 registers */

/*
 *	Bank 0x50 - 0x5f
 */

#define BASE_GMAC_1		0x2800	/* GMAC 1 registers */

/*
 *	Bank 0x60 - 0x6f
 */
#define BASE_XMAC_2		0x3000	/* XMAC 2 registers */

/*
 *	Bank 0x70 - 0x7f
 */
#define BASE_GMAC_2		0x3800	/* GMAC 2 registers */

/*
 *	Control Register Bit Definitions:
 */
/*	B0_RAP		8 bit	Register Address Port */
								/* Bit 7:	reserved */
#define RAP_RAP			0x3f	/* Bit 6..0:	0 = block 0,..,6f = block 6f */

/*	B0_CTST			16 bit	Control/Status register */
								/* Bit 15..14:	reserved */
#define CS_CLK_RUN_HOT	BIT_13S		/* CLK_RUN hot m. (YUKON-Lite only) */
#define CS_CLK_RUN_RST	BIT_12S		/* CLK_RUN reset  (YUKON-Lite only) */
#define CS_CLK_RUN_ENA	BIT_11S		/* CLK_RUN enable (YUKON-Lite only) */
#define CS_VAUX_AVAIL	BIT_10S		/* VAUX available (YUKON only) */
#define CS_BUS_CLOCK	BIT_9S		/* Bus Clock 0/1 = 33/66 MHz */
#define CS_BUS_SLOT_SZ	BIT_8S		/* Slot Size 0/1 = 32/64 bit slot */
#define CS_ST_SW_IRQ	BIT_7S		/* Set IRQ SW Request */
#define CS_CL_SW_IRQ	BIT_6S		/* Clear IRQ SW Request */
#define CS_STOP_DONE	BIT_5S		/* Stop Master is finished */
#define CS_STOP_MAST	BIT_4S		/* Command Bit to stop the master */
#define CS_MRST_CLR		BIT_3S		/* Clear Master reset	*/
#define CS_MRST_SET		BIT_2S		/* Set Master reset	*/
#define CS_RST_CLR		BIT_1S		/* Clear Software reset	*/
#define CS_RST_SET		BIT_0S		/* Set   Software reset	*/

/*	B0_LED			 8 Bit	LED register */
								/* Bit  7.. 2:	reserved */
#define LED_STAT_ON		BIT_1S		/* Status LED on	*/
#define LED_STAT_OFF	BIT_0S		/* Status LED off	*/

/*	B0_POWER_CTRL	 8 Bit	Power Control reg (YUKON only) */
#define PC_VAUX_ENA		BIT_7		/* Switch VAUX Enable  */
#define PC_VAUX_DIS		BIT_6       /* Switch VAUX Disable */
#define PC_VCC_ENA		BIT_5       /* Switch VCC Enable  */
#define PC_VCC_DIS		BIT_4       /* Switch VCC Disable */
#define PC_VAUX_ON		BIT_3       /* Switch VAUX On  */
#define PC_VAUX_OFF		BIT_2       /* Switch VAUX Off */
#define PC_VCC_ON		BIT_1       /* Switch VCC On  */
#define PC_VCC_OFF		BIT_0       /* Switch VCC Off */

/*	B0_ISRC			32 bit	Interrupt Source Register */
/*	B0_IMSK			32 bit	Interrupt Mask Register */
/*	B0_SP_ISRC		32 bit	Special Interrupt Source Reg */
/*	B2_IRQM_MSK 	32 bit	IRQ Moderation Mask */
#define IS_ALL_MSK		0xbfffffffUL	/* All Interrupt bits */
#define IS_HW_ERR		BIT_31		/* Interrupt HW Error */
								/* Bit 30:	reserved */
#define IS_PA_TO_RX1	BIT_29		/* Packet Arb Timeout Rx1 */
#define IS_PA_TO_RX2	BIT_28		/* Packet Arb Timeout Rx2 */
#define IS_PA_TO_TX1	BIT_27		/* Packet Arb Timeout Tx1 */
#define IS_PA_TO_TX2	BIT_26		/* Packet Arb Timeout Tx2 */
#define IS_I2C_READY	BIT_25		/* IRQ on end of I2C Tx */
#define IS_IRQ_SW		BIT_24		/* SW forced IRQ	*/
#define IS_EXT_REG		BIT_23		/* IRQ from LM80 or PHY (GENESIS only) */
									/* IRQ from PHY (YUKON only) */
#define IS_TIMINT		BIT_22		/* IRQ from Timer	*/
#define IS_MAC1			BIT_21		/* IRQ from MAC 1	*/
#define IS_LNK_SYNC_M1	BIT_20		/* Link Sync Cnt wrap MAC 1 */
#define IS_MAC2			BIT_19		/* IRQ from MAC 2	*/
#define IS_LNK_SYNC_M2	BIT_18		/* Link Sync Cnt wrap MAC 2 */
/* Receive Queue 1 */
#define IS_R1_B			BIT_17		/* Q_R1 End of Buffer */
#define IS_R1_F			BIT_16		/* Q_R1 End of Frame */
#define IS_R1_C			BIT_15		/* Q_R1 Encoding Error */
/* Receive Queue 2 */
#define IS_R2_B			BIT_14		/* Q_R2 End of Buffer */
#define IS_R2_F			BIT_13		/* Q_R2 End of Frame */
#define IS_R2_C			BIT_12		/* Q_R2 Encoding Error */
/* Synchronous Transmit Queue 1 */
#define IS_XS1_B		BIT_11		/* Q_XS1 End of Buffer */
#define IS_XS1_F		BIT_10		/* Q_XS1 End of Frame */
#define IS_XS1_C		BIT_9		/* Q_XS1 Encoding Error */
/* Asynchronous Transmit Queue 1 */
#define IS_XA1_B		BIT_8		/* Q_XA1 End of Buffer */
#define IS_XA1_F		BIT_7		/* Q_XA1 End of Frame */
#define IS_XA1_C		BIT_6		/* Q_XA1 Encoding Error */
/* Synchronous Transmit Queue 2 */
#define IS_XS2_B		BIT_5		/* Q_XS2 End of Buffer */
#define IS_XS2_F		BIT_4		/* Q_XS2 End of Frame */
#define IS_XS2_C		BIT_3		/* Q_XS2 Encoding Error */
/* Asynchronous Transmit Queue 2 */
#define IS_XA2_B		BIT_2		/* Q_XA2 End of Buffer */
#define IS_XA2_F		BIT_1		/* Q_XA2 End of Frame */
#define IS_XA2_C		BIT_0		/* Q_XA2 Encoding Error */


/*	B0_HWE_ISRC		32 bit	HW Error Interrupt Src Reg */
/*	B0_HWE_IMSK		32 bit	HW Error Interrupt Mask Reg */
/*	B2_IRQM_HWE_MSK	32 bit	IRQ Moderation HW Error Mask */
#define IS_ERR_MSK		0x00000fffL	/* 		All Error bits */
								/* Bit 31..14:	reserved */
#define IS_IRQ_TIST_OV	BIT_13	/* Time Stamp Timer Overflow (YUKON only) */
#define IS_IRQ_SENSOR	BIT_12	/* IRQ from Sensor (YUKON only) */
#define IS_IRQ_MST_ERR	BIT_11	/* IRQ master error detected */
#define IS_IRQ_STAT		BIT_10	/* IRQ status exception */
#define IS_NO_STAT_M1	BIT_9	/* No Rx Status from MAC 1 */
#define IS_NO_STAT_M2	BIT_8	/* No Rx Status from MAC 2 */
#define IS_NO_TIST_M1	BIT_7	/* No Time Stamp from MAC 1 */
#define IS_NO_TIST_M2	BIT_6	/* No Time Stamp from MAC 2 */
#define IS_RAM_RD_PAR	BIT_5	/* RAM Read  Parity Error */
#define IS_RAM_WR_PAR	BIT_4	/* RAM Write Parity Error */
#define IS_M1_PAR_ERR	BIT_3	/* MAC 1 Parity Error */
#define IS_M2_PAR_ERR	BIT_2	/* MAC 2 Parity Error */
#define IS_R1_PAR_ERR	BIT_1	/* Queue R1 Parity Error */
#define IS_R2_PAR_ERR	BIT_0	/* Queue R2 Parity Error */

/*	B2_CONN_TYP		 8 bit	Connector type */
/*	B2_PMD_TYP		 8 bit	PMD type */
/*	Values of connector and PMD type comply to SysKonnect internal std */

/*	B2_MAC_CFG		 8 bit	MAC Configuration / Chip Revision */
#define CFG_CHIP_R_MSK	(0xf<<4)	/* Bit 7.. 4: Chip Revision */
									/* Bit 3.. 2:	reserved */
#define CFG_DIS_M2_CLK	BIT_1S		/* Disable Clock for 2nd MAC */
#define CFG_SNG_MAC		BIT_0S		/* MAC Config: 0=2 MACs / 1=1 MAC*/

/*	B2_CHIP_ID		 8 bit 	Chip Identification Number */
#define CHIP_ID_GENESIS		0x0a	/* Chip ID for GENESIS */
#define CHIP_ID_YUKON		0xb0	/* Chip ID for YUKON */
#define CHIP_ID_YUKON_LITE	0xb1	/* Chip ID for YUKON-Lite (Rev. A1-A3) */
#define CHIP_ID_YUKON_LP	0xb2	/* Chip ID for YUKON-LP */

#define CHIP_REV_YU_LITE_A1	3		/* Chip Rev. for YUKON-Lite A1,A2 */
#define CHIP_REV_YU_LITE_A3	7		/* Chip Rev. for YUKON-Lite A3 */

/*	B2_FAR			32 bit	Flash-Prom Addr Reg/Cnt */
#define FAR_ADDR		0x1ffffL	/* Bit 16.. 0:	FPROM Address mask */

/*	B2_LD_CTRL		 8 bit	EPROM loader control register */
/*	Bits are currently reserved */

/*	B2_LD_TEST		 8 bit	EPROM loader test register */
								/* Bit 7.. 4:	reserved */
#define LD_T_ON			BIT_3S	/* Loader Test mode on */
#define LD_T_OFF		BIT_2S	/* Loader Test mode off */
#define LD_T_STEP		BIT_1S	/* Decrement FPROM addr. Counter */
#define LD_START		BIT_0S	/* Start loading FPROM */

/*
 *	Timer Section
 */
/*	B2_TI_CTRL		 8 bit	Timer control */
/*	B2_IRQM_CTRL	 8 bit	IRQ Moderation Timer Control */
								/* Bit 7.. 3:	reserved */
#define TIM_START		BIT_2S	/* Start Timer */
#define TIM_STOP		BIT_1S	/* Stop  Timer */
#define TIM_CLR_IRQ		BIT_0S	/* Clear Timer IRQ (!IRQM) */

/*	B2_TI_TEST		 8 Bit	Timer Test */
/*	B2_IRQM_TEST	 8 bit	IRQ Moderation Timer Test */
/*	B28_DPT_TST		 8 bit	Descriptor Poll Timer Test Reg */
								/* Bit 7.. 3:	reserved */
#define TIM_T_ON		BIT_2S	/* Test mode on */
#define TIM_T_OFF		BIT_1S	/* Test mode off */
#define TIM_T_STEP		BIT_0S	/* Test step */

/*	B28_DPT_INI	32 bit	Descriptor Poll Timer Init Val */
/*	B28_DPT_VAL	32 bit	Descriptor Poll Timer Curr Val */
								/* Bit 31..24:	reserved */
#define DPT_MSK		0x00ffffffL	/* Bit 23.. 0:	Desc Poll Timer Bits */

/*	B28_DPT_CTRL	 8 bit	Descriptor Poll Timer Ctrl Reg */
								/* Bit  7.. 2:	reserved */
#define DPT_START		BIT_1S	/* Start Descriptor Poll Timer */
#define DPT_STOP		BIT_0S	/* Stop  Descriptor Poll Timer */

/*	B2_E_3			 8 bit 	lower 4 bits used for HW self test result */
#define B2_E3_RES_MASK	0x0f

/*	B2_TST_CTRL1	 8 bit	Test Control Register 1 */
#define TST_FRC_DPERR_MR	BIT_7S	/* force DATAPERR on MST RD */
#define TST_FRC_DPERR_MW	BIT_6S	/* force DATAPERR on MST WR */
#define TST_FRC_DPERR_TR	BIT_5S	/* force DATAPERR on TRG RD */
#define TST_FRC_DPERR_TW	BIT_4S	/* force DATAPERR on TRG WR */
#define TST_FRC_APERR_M		BIT_3S	/* force ADDRPERR on MST */
#define TST_FRC_APERR_T		BIT_2S	/* force ADDRPERR on TRG */
#define TST_CFG_WRITE_ON	BIT_1S	/* Enable  Config Reg WR */
#define TST_CFG_WRITE_OFF	BIT_0S	/* Disable Config Reg WR */

/*	B2_TST_CTRL2	 8 bit	Test Control Register 2 */
									/* Bit 7.. 4:	reserved */
			/* force the following error on the next master read/write	*/
#define TST_FRC_DPERR_MR64	BIT_3S	/* DataPERR RD 64	*/
#define TST_FRC_DPERR_MW64	BIT_2S	/* DataPERR WR 64	*/
#define TST_FRC_APERR_1M64	BIT_1S	/* AddrPERR on 1. phase */
#define TST_FRC_APERR_2M64	BIT_0S	/* AddrPERR on 2. phase */

/*	B2_GP_IO		32 bit	General Purpose I/O Register */
							/* Bit 31..26:	reserved */
#define GP_DIR_9	BIT_25	/* IO_9 direct, 0=In/1=Out */
#define GP_DIR_8	BIT_24	/* IO_8 direct, 0=In/1=Out */
#define GP_DIR_7	BIT_23	/* IO_7 direct, 0=In/1=Out */
#define GP_DIR_6	BIT_22	/* IO_6 direct, 0=In/1=Out */
#define GP_DIR_5	BIT_21	/* IO_5 direct, 0=In/1=Out */
#define GP_DIR_4	BIT_20	/* IO_4 direct, 0=In/1=Out */
#define GP_DIR_3	BIT_19	/* IO_3 direct, 0=In/1=Out */
#define GP_DIR_2	BIT_18	/* IO_2 direct, 0=In/1=Out */
#define GP_DIR_1	BIT_17	/* IO_1 direct, 0=In/1=Out */
#define GP_DIR_0	BIT_16	/* IO_0 direct, 0=In/1=Out */
						/* Bit 15..10:	reserved */
#define GP_IO_9		BIT_9	/* IO_9 pin */
#define GP_IO_8		BIT_8	/* IO_8 pin */
#define GP_IO_7		BIT_7	/* IO_7 pin */
#define GP_IO_6		BIT_6	/* IO_6 pin */
#define GP_IO_5		BIT_5	/* IO_5 pin */
#define GP_IO_4		BIT_4	/* IO_4 pin */
#define GP_IO_3		BIT_3	/* IO_3 pin */
#define GP_IO_2		BIT_2	/* IO_2 pin */
#define GP_IO_1		BIT_1	/* IO_1 pin */
#define GP_IO_0		BIT_0	/* IO_0 pin */

/*	B2_I2C_CTRL		32 bit	I2C HW Control Register */
#define I2C_FLAG		BIT_31		/* Start read/write if WR */
#define I2C_ADDR		(0x7fffL<<16)	/* Bit 30..16:	Addr to be RD/WR */
#define I2C_DEV_SEL		(0x7fL<<9)		/* Bit 15.. 9:	I2C Device Select */
								/* Bit	8.. 5:	reserved	*/
#define I2C_BURST_LEN	BIT_4		/* Burst Len, 1/4 bytes */
#define I2C_DEV_SIZE	(7<<1)		/* Bit	3.. 1:	I2C Device Size	*/
#define I2C_025K_DEV	(0<<1)		/*		0: 256 Bytes or smal. */
#define I2C_05K_DEV		(1<<1)		/* 		1: 512	Bytes	*/
#define I2C_1K_DEV		(2<<1)		/*		2: 1024 Bytes	*/
#define I2C_2K_DEV		(3<<1)		/*		3: 2048	Bytes	*/
#define I2C_4K_DEV		(4<<1)		/*		4: 4096 Bytes	*/
#define I2C_8K_DEV		(5<<1)		/*		5: 8192 Bytes	*/
#define I2C_16K_DEV		(6<<1)		/*		6: 16384 Bytes	*/
#define I2C_32K_DEV		(7<<1)		/*		7: 32768 Bytes	*/
#define I2C_STOP		BIT_0		/* Interrupt I2C transfer */

/*	B2_I2C_IRQ		32 bit	I2C HW IRQ Register */
								/* Bit 31.. 1	reserved */
#define I2C_CLR_IRQ		BIT_0	/* Clear I2C IRQ */

/*	B2_I2C_SW		32 bit (8 bit access)	I2C HW SW Port Register */
								/* Bit  7.. 3:	reserved */
#define I2C_DATA_DIR	BIT_2S		/* direction of I2C_DATA */
#define I2C_DATA		BIT_1S		/* I2C Data Port	*/
#define I2C_CLK			BIT_0S		/* I2C Clock Port	*/

/*
 * I2C Address
 */
#define I2C_SENS_ADDR	LM80_ADDR	/* I2C Sensor Address, (Volt and Temp)*/


/*	B2_BSC_CTRL		 8 bit	Blink Source Counter Control */
							/* Bit  7.. 2:	reserved */
#define BSC_START	BIT_1S		/* Start Blink Source Counter */
#define BSC_STOP	BIT_0S		/* Stop  Blink Source Counter */

/*	B2_BSC_STAT		 8 bit	Blink Source Counter Status */
							/* Bit  7.. 1:	reserved */
#define BSC_SRC		BIT_0S		/* Blink Source, 0=Off / 1=On */

/*	B2_BSC_TST		16 bit	Blink Source Counter Test Reg */
#define BSC_T_ON	BIT_2S		/* Test mode on */
#define BSC_T_OFF	BIT_1S		/* Test mode off */
#define BSC_T_STEP	BIT_0S		/* Test step */


/*	B3_RAM_ADDR		32 bit	RAM Address, to read or write */
					/* Bit 31..19:	reserved */
#define RAM_ADR_RAN	0x0007ffffL	/* Bit 18.. 0:	RAM Address Range */

/* RAM Interface Registers */
/*	B3_RI_CTRL		16 bit	RAM Iface Control Register */
								/* Bit 15..10:	reserved */
#define RI_CLR_RD_PERR	BIT_9S	/* Clear IRQ RAM Read Parity Err */
#define RI_CLR_WR_PERR	BIT_8S	/* Clear IRQ RAM Write Parity Err*/
								/* Bit	7.. 2:	reserved */
#define RI_RST_CLR		BIT_1S	/* Clear RAM Interface Reset */
#define RI_RST_SET		BIT_0S	/* Set   RAM Interface Reset */

/*	B3_RI_TEST		 8 bit	RAM Iface Test Register */
								/* Bit 15.. 4:	reserved */
#define RI_T_EV			BIT_3S	/* Timeout Event occured */
#define RI_T_ON			BIT_2S	/* Timeout Timer Test On */
#define RI_T_OFF		BIT_1S	/* Timeout Timer Test Off */
#define RI_T_STEP		BIT_0S	/* Timeout Timer Step */

/* MAC Arbiter Registers */
/*	B3_MA_TO_CTRL	16 bit	MAC Arbiter Timeout Ctrl Reg */
								/* Bit 15.. 4:	reserved */
#define MA_FOE_ON		BIT_3S	/* XMAC Fast Output Enable ON */
#define MA_FOE_OFF		BIT_2S	/* XMAC Fast Output Enable OFF */
#define MA_RST_CLR		BIT_1S	/* Clear MAC Arbiter Reset */
#define MA_RST_SET		BIT_0S	/* Set   MAC Arbiter Reset */

/*	B3_MA_RC_CTRL	16 bit	MAC Arbiter Recovery Ctrl Reg */
								/* Bit 15.. 8:	reserved */
#define MA_ENA_REC_TX2	BIT_7S	/* Enable  Recovery Timer TX2 */
#define MA_DIS_REC_TX2	BIT_6S	/* Disable Recovery Timer TX2 */
#define MA_ENA_REC_TX1	BIT_5S	/* Enable  Recovery Timer TX1 */
#define MA_DIS_REC_TX1	BIT_4S	/* Disable Recovery Timer TX1 */
#define MA_ENA_REC_RX2	BIT_3S	/* Enable  Recovery Timer RX2 */
#define MA_DIS_REC_RX2	BIT_2S	/* Disable Recovery Timer RX2 */
#define MA_ENA_REC_RX1	BIT_1S	/* Enable  Recovery Timer RX1 */
#define MA_DIS_REC_RX1	BIT_0S	/* Disable Recovery Timer RX1 */

/* Packet Arbiter Registers */
/*	B3_PA_CTRL		16 bit	Packet Arbiter Ctrl Register */
								/* Bit 15..14:	reserved */
#define PA_CLR_TO_TX2	BIT_13S	/* Clear IRQ Packet Timeout TX2 */
#define PA_CLR_TO_TX1	BIT_12S	/* Clear IRQ Packet Timeout TX1 */
#define PA_CLR_TO_RX2	BIT_11S	/* Clear IRQ Packet Timeout RX2 */
#define PA_CLR_TO_RX1	BIT_10S	/* Clear IRQ Packet Timeout RX1 */
#define PA_ENA_TO_TX2	BIT_9S	/* Enable  Timeout Timer TX2 */
#define PA_DIS_TO_TX2	BIT_8S	/* Disable Timeout Timer TX2 */
#define PA_ENA_TO_TX1	BIT_7S	/* Enable  Timeout Timer TX1 */
#define PA_DIS_TO_TX1	BIT_6S	/* Disable Timeout Timer TX1 */
#define PA_ENA_TO_RX2	BIT_5S	/* Enable  Timeout Timer RX2 */
#define PA_DIS_TO_RX2	BIT_4S	/* Disable Timeout Timer RX2 */
#define PA_ENA_TO_RX1	BIT_3S	/* Enable  Timeout Timer RX1 */
#define PA_DIS_TO_RX1	BIT_2S	/* Disable Timeout Timer RX1 */
#define PA_RST_CLR		BIT_1S	/* Clear MAC Arbiter Reset */
#define PA_RST_SET		BIT_0S	/* Set   MAC Arbiter Reset */

#define PA_ENA_TO_ALL	(PA_ENA_TO_RX1 | PA_ENA_TO_RX2 |\
						PA_ENA_TO_TX1 | PA_ENA_TO_TX2)

/* Rx/Tx Path related Arbiter Test Registers */
/*	B3_MA_TO_TEST	16 bit	MAC Arbiter Timeout Test Reg */
/*	B3_MA_RC_TEST	16 bit	MAC Arbiter Recovery Test Reg */
/*	B3_PA_TEST		16 bit	Packet Arbiter Test Register */
/*			Bit 15, 11, 7, and 3 are reserved in B3_PA_TEST */
#define TX2_T_EV	BIT_15S		/* TX2 Timeout/Recv Event occured */
#define TX2_T_ON	BIT_14S		/* TX2 Timeout/Recv Timer Test On */
#define TX2_T_OFF	BIT_13S		/* TX2 Timeout/Recv Timer Tst Off */
#define TX2_T_STEP	BIT_12S		/* TX2 Timeout/Recv Timer Step */
#define TX1_T_EV	BIT_11S		/* TX1 Timeout/Recv Event occured */
#define TX1_T_ON	BIT_10S		/* TX1 Timeout/Recv Timer Test On */
#define TX1_T_OFF	BIT_9S		/* TX1 Timeout/Recv Timer Tst Off */
#define TX1_T_STEP	BIT_8S		/* TX1 Timeout/Recv Timer Step */
#define RX2_T_EV	BIT_7S		/* RX2 Timeout/Recv Event occured */
#define RX2_T_ON	BIT_6S		/* RX2 Timeout/Recv Timer Test On */
#define RX2_T_OFF	BIT_5S		/* RX2 Timeout/Recv Timer Tst Off */
#define RX2_T_STEP	BIT_4S		/* RX2 Timeout/Recv Timer Step */
#define RX1_T_EV	BIT_3S		/* RX1 Timeout/Recv Event occured */
#define RX1_T_ON	BIT_2S		/* RX1 Timeout/Recv Timer Test On */
#define RX1_T_OFF	BIT_1S		/* RX1 Timeout/Recv Timer Tst Off */
#define RX1_T_STEP	BIT_0S		/* RX1 Timeout/Recv Timer Step */


/* Transmit Arbiter Registers MAC 1 and 2, use MR_ADDR() to access */
/*	TXA_ITI_INI		32 bit	Tx Arb Interval Timer Init Val */
/*	TXA_ITI_VAL		32 bit	Tx Arb Interval Timer Value */
/*	TXA_LIM_INI		32 bit	Tx Arb Limit Counter Init Val */
/*	TXA_LIM_VAL		32 bit	Tx Arb Limit Counter Value */
								/* Bit 31..24:	reserved */
#define TXA_MAX_VAL	0x00ffffffUL/* Bit 23.. 0:	Max TXA Timer/Cnt Val */

/*	TXA_CTRL		 8 bit	Tx Arbiter Control Register */
#define TXA_ENA_FSYNC	BIT_7S	/* Enable  force of sync Tx queue */
#define TXA_DIS_FSYNC	BIT_6S	/* Disable force of sync Tx queue */
#define TXA_ENA_ALLOC	BIT_5S	/* Enable  alloc of free bandwidth */
#define TXA_DIS_ALLOC	BIT_4S	/* Disable alloc of free bandwidth */
#define TXA_START_RC	BIT_3S	/* Start sync Rate Control */
#define TXA_STOP_RC		BIT_2S	/* Stop  sync Rate Control */
#define TXA_ENA_ARB		BIT_1S	/* Enable  Tx Arbiter */
#define TXA_DIS_ARB		BIT_0S	/* Disable Tx Arbiter */

/*	TXA_TEST		 8 bit	Tx Arbiter Test Register */
								/* Bit 7.. 6:	reserved */
#define TXA_INT_T_ON	BIT_5S	/* Tx Arb Interval Timer Test On */
#define TXA_INT_T_OFF	BIT_4S	/* Tx Arb Interval Timer Test Off */
#define TXA_INT_T_STEP	BIT_3S	/* Tx Arb Interval Timer Step */
#define TXA_LIM_T_ON	BIT_2S	/* Tx Arb Limit Timer Test On */
#define TXA_LIM_T_OFF	BIT_1S	/* Tx Arb Limit Timer Test Off */
#define TXA_LIM_T_STEP	BIT_0S	/* Tx Arb Limit Timer Step */

/*	TXA_STAT		 8 bit	Tx Arbiter Status Register */
								/* Bit 7.. 1:	reserved */
#define TXA_PRIO_XS		BIT_0S	/* sync queue has prio to send */

/*	Q_BC			32 bit	Current Byte Counter */
								/* Bit 31..16:	reserved */
#define BC_MAX			0xffff	/* Bit 15.. 0:	Byte counter */

/* BMU Control Status Registers */
/*	B0_R1_CSR		32 bit	BMU Ctrl/Stat Rx Queue 1 */
/*	B0_R2_CSR		32 bit	BMU Ctrl/Stat Rx Queue 2 */
/*	B0_XA1_CSR		32 bit	BMU Ctrl/Stat Sync Tx Queue 1 */
/*	B0_XS1_CSR		32 bit	BMU Ctrl/Stat Async Tx Queue 1 */
/*	B0_XA2_CSR		32 bit	BMU Ctrl/Stat Sync Tx Queue 2 */
/*	B0_XS2_CSR		32 bit	BMU Ctrl/Stat Async Tx Queue 2 */
/*	Q_CSR			32 bit	BMU Control/Status Register */
								/* Bit 31..25:	reserved */
#define CSR_SV_IDLE		BIT_24		/* BMU SM Idle */
								/* Bit 23..22:	reserved */
#define CSR_DESC_CLR	BIT_21		/* Clear Reset for Descr */
#define CSR_DESC_SET	BIT_20		/* Set   Reset for Descr */
#define CSR_FIFO_CLR	BIT_19		/* Clear Reset for FIFO */
#define CSR_FIFO_SET	BIT_18		/* Set   Reset for FIFO */
#define CSR_HPI_RUN		BIT_17		/* Release HPI SM */
#define CSR_HPI_RST		BIT_16		/* Reset   HPI SM to Idle */
#define CSR_SV_RUN		BIT_15		/* Release Supervisor SM */
#define CSR_SV_RST		BIT_14		/* Reset   Supervisor SM */
#define CSR_DREAD_RUN	BIT_13		/* Release Descr Read SM */
#define CSR_DREAD_RST	BIT_12		/* Reset   Descr Read SM */
#define CSR_DWRITE_RUN	BIT_11		/* Release Descr Write SM */
#define CSR_DWRITE_RST	BIT_10		/* Reset   Descr Write SM */
#define CSR_TRANS_RUN	BIT_9		/* Release Transfer SM */
#define CSR_TRANS_RST	BIT_8		/* Reset   Transfer SM */
#define CSR_ENA_POL		BIT_7		/* Enable  Descr Polling */
#define CSR_DIS_POL		BIT_6		/* Disable Descr Polling */
#define CSR_STOP		BIT_5		/* Stop  Rx/Tx Queue */
#define CSR_START		BIT_4		/* Start Rx/Tx Queue */
#define CSR_IRQ_CL_P	BIT_3		/* (Rx)	Clear Parity IRQ */
#define CSR_IRQ_CL_B	BIT_2		/* Clear EOB IRQ */
#define CSR_IRQ_CL_F	BIT_1		/* Clear EOF IRQ */
#define CSR_IRQ_CL_C	BIT_0		/* Clear ERR IRQ */

#define CSR_SET_RESET	(CSR_DESC_SET | CSR_FIFO_SET | CSR_HPI_RST |\
						CSR_SV_RST | CSR_DREAD_RST | CSR_DWRITE_RST |\
						CSR_TRANS_RST)
#define CSR_CLR_RESET	(CSR_DESC_CLR | CSR_FIFO_CLR | CSR_HPI_RUN |\
						CSR_SV_RUN | CSR_DREAD_RUN | CSR_DWRITE_RUN |\
						CSR_TRANS_RUN)

/*	Q_F				32 bit	Flag Register */
									/* Bit 31..28:	reserved */
#define F_ALM_FULL		BIT_27		/* Rx FIFO: almost full */
#define F_EMPTY			BIT_27		/* Tx FIFO: empty flag */
#define F_FIFO_EOF		BIT_26		/* Tag (EOF Flag) bit in FIFO */
#define F_WM_REACHED	BIT_25		/* Watermark reached */
									/* reserved */
#define F_FIFO_LEVEL	(0x1fL<<16)	/* Bit 23..16:	# of Qwords in FIFO */
									/* Bit 15..11: 	reserved */
#define F_WATER_MARK	0x0007ffL	/* Bit 10.. 0:	Watermark */

/*	Q_T1			32 bit	Test Register 1 */
/*		Holds four State Machine control Bytes */
#define SM_CTRL_SV_MSK	(0xffL<<24)	/* Bit 31..24:	Control Supervisor SM */
#define SM_CTRL_RD_MSK	(0xffL<<16)	/* Bit 23..16:	Control Read Desc SM */
#define SM_CTRL_WR_MSK	(0xffL<<8)	/* Bit 15.. 8:	Control Write Desc SM */
#define SM_CTRL_TR_MSK	0xffL		/* Bit	7.. 0:	Control Transfer SM */

/*	Q_T1_TR			 8 bit	Test Register 1 Transfer SM */
/*	Q_T1_WR			 8 bit	Test Register 1 Write Descriptor SM */
/*	Q_T1_RD			 8 bit	Test Register 1 Read Descriptor SM */
/*	Q_T1_SV			 8 bit	Test Register 1 Supervisor SM */

/* The control status byte of each machine looks like ... */
#define SM_STATE		0xf0	/* Bit 7.. 4:	State which shall be loaded */
#define SM_LOAD			BIT_3S	/* Load the SM with SM_STATE */
#define SM_TEST_ON		BIT_2S	/* Switch on SM Test Mode */
#define SM_TEST_OFF		BIT_1S	/* Go off the Test Mode */
#define SM_STEP			BIT_0S	/* Step the State Machine */
/* The encoding of the states is not supported by the Diagnostics Tool */

/*	Q_T2			32 bit	Test Register 2	*/
								/* Bit 31.. 8:	reserved */
#define T2_AC_T_ON		BIT_7	/* Address Counter Test Mode on */
#define T2_AC_T_OFF		BIT_6	/* Address Counter Test Mode off */
#define T2_BC_T_ON		BIT_5	/* Byte Counter Test Mode on */
#define T2_BC_T_OFF		BIT_4	/* Byte Counter Test Mode off */
#define T2_STEP04		BIT_3	/* Inc AC/Dec BC by 4 */
#define T2_STEP03		BIT_2	/* Inc AC/Dec BC by 3 */
#define T2_STEP02		BIT_1	/* Inc AC/Dec BC by 2 */
#define T2_STEP01		BIT_0	/* Inc AC/Dec BC by 1 */

/*	Q_T3			32 bit	Test Register 3	*/
								/* Bit 31.. 7:	reserved */
#define T3_MUX_MSK		(7<<4)	/* Bit  6.. 4:	Mux Position */
								/* Bit  3:	reserved */
#define T3_VRAM_MSK		7		/* Bit  2.. 0:	Virtual RAM Buffer Address */

/* RAM Buffer Register Offsets, use RB_ADDR(Queue, Offs) to access */
/*	RB_START		32 bit	RAM Buffer Start Address */
/*	RB_END			32 bit	RAM Buffer End Address */
/*	RB_WP			32 bit	RAM Buffer Write Pointer */
/*	RB_RP			32 bit	RAM Buffer Read Pointer */
/*	RB_RX_UTPP		32 bit	Rx Upper Threshold, Pause Pack */
/*	RB_RX_LTPP		32 bit	Rx Lower Threshold, Pause Pack */
/*	RB_RX_UTHP		32 bit	Rx Upper Threshold, High Prio */
/*	RB_RX_LTHP		32 bit	Rx Lower Threshold, High Prio */
/*	RB_PC			32 bit	RAM Buffer Packet Counter */
/*	RB_LEV			32 bit	RAM Buffer Level Register */
				/* Bit 31..19:	reserved */
#define RB_MSK	0x0007ffff	/* Bit 18.. 0:	RAM Buffer Pointer Bits */

/*	RB_TST2			 8 bit	RAM Buffer Test Register 2 */
								/* Bit 7.. 4:	reserved */
#define RB_PC_DEC		BIT_3S	/* Packet Counter Decrem */
#define RB_PC_T_ON		BIT_2S	/* Packet Counter Test On */
#define RB_PC_T_OFF		BIT_1S	/* Packet Counter Tst Off */
#define RB_PC_INC		BIT_0S	/* Packet Counter Increm */

/*	RB_TST1			 8 bit	RAM Buffer Test Register 1 */
							/* Bit 7:	reserved */
#define RB_WP_T_ON		BIT_6S	/* Write Pointer Test On */
#define RB_WP_T_OFF		BIT_5S	/* Write Pointer Test Off */
#define RB_WP_INC		BIT_4S	/* Write Pointer Increm */
								/* Bit 3:	reserved */
#define RB_RP_T_ON		BIT_2S	/* Read Pointer Test On */
#define RB_RP_T_OFF		BIT_1S	/* Read Pointer Test Off */
#define RB_RP_DEC		BIT_0S	/* Read Pointer Decrement */

/*	RB_CTRL			 8 bit	RAM Buffer Control Register */
								/* Bit 7.. 6:	reserved */
#define RB_ENA_STFWD	BIT_5S	/* Enable  Store & Forward */
#define RB_DIS_STFWD	BIT_4S	/* Disable Store & Forward */
#define RB_ENA_OP_MD	BIT_3S	/* Enable  Operation Mode */
#define RB_DIS_OP_MD	BIT_2S	/* Disable Operation Mode */
#define RB_RST_CLR		BIT_1S	/* Clear RAM Buf STM Reset */
#define RB_RST_SET		BIT_0S	/* Set   RAM Buf STM Reset */


/* Receive and Transmit MAC FIFO Registers (GENESIS only) */

/*	RX_MFF_EA		32 bit	Receive MAC FIFO End Address */
/*	RX_MFF_WP		32 bit 	Receive MAC FIFO Write Pointer */
/*	RX_MFF_RP		32 bit	Receive MAC FIFO Read Pointer */
/*	RX_MFF_PC		32 bit	Receive MAC FIFO Packet Counter */
/*	RX_MFF_LEV		32 bit	Receive MAC FIFO Level */
/*	TX_MFF_EA		32 bit	Transmit MAC FIFO End Address */
/*	TX_MFF_WP		32 bit 	Transmit MAC FIFO Write Pointer */
/*	TX_MFF_WSP		32 bit	Transmit MAC FIFO WR Shadow Pointer */
/*	TX_MFF_RP		32 bit	Transmit MAC FIFO Read Pointer */
/*	TX_MFF_PC		32 bit	Transmit MAC FIFO Packet Cnt */
/*	TX_MFF_LEV		32 bit	Transmit MAC FIFO Level */
								/* Bit 31.. 6:	reserved */
#define MFF_MSK			0x007fL	/* Bit	5.. 0:	MAC FIFO Address/Ptr Bits */

/*	RX_MFF_CTRL1	16 bit	Receive MAC FIFO Control Reg 1 */
								/* Bit 15..14:	reserved */
#define MFF_ENA_RDY_PAT	BIT_13S		/* Enable  Ready Patch */
#define MFF_DIS_RDY_PAT	BIT_12S		/* Disable Ready Patch */
#define MFF_ENA_TIM_PAT	BIT_11S		/* Enable  Timing Patch */
#define MFF_DIS_TIM_PAT	BIT_10S		/* Disable Timing Patch */
#define MFF_ENA_ALM_FUL	BIT_9S		/* Enable  AlmostFull Sign */
#define MFF_DIS_ALM_FUL	BIT_8S		/* Disable AlmostFull Sign */
#define MFF_ENA_PAUSE	BIT_7S		/* Enable  Pause Signaling */
#define MFF_DIS_PAUSE	BIT_6S		/* Disable Pause Signaling */
#define MFF_ENA_FLUSH	BIT_5S		/* Enable  Frame Flushing */
#define MFF_DIS_FLUSH	BIT_4S		/* Disable Frame Flushing */
#define MFF_ENA_TIST	BIT_3S		/* Enable  Time Stamp Gener */
#define MFF_DIS_TIST	BIT_2S		/* Disable Time Stamp Gener */
#define MFF_CLR_INTIST	BIT_1S		/* Clear IRQ No Time Stamp */
#define MFF_CLR_INSTAT	BIT_0S		/* Clear IRQ No Status */

#define MFF_RX_CTRL_DEF MFF_ENA_TIM_PAT

/*	TX_MFF_CTRL1	16 bit	Transmit MAC FIFO Control Reg 1 */
#define MFF_CLR_PERR	BIT_15S		/* Clear Parity Error IRQ */
								/* Bit 14:	reserved */
#define MFF_ENA_PKT_REC	BIT_13S		/* Enable  Packet Recovery */
#define MFF_DIS_PKT_REC BIT_12S		/* Disable Packet Recovery */
/*	MFF_ENA_TIM_PAT	 (see RX_MFF_CTRL1) Bit 11:	Enable  Timing Patch */
/*	MFF_DIS_TIM_PAT	 (see RX_MFF_CTRL1) Bit 10:	Disable Timing Patch */
/*	MFF_ENA_ALM_FUL	 (see RX_MFF_CTRL1) Bit	 9:	Enable  Almost Full Sign */
/*	MFF_DIS_ALM_FUL	 (see RX_MFF_CTRL1) Bit	 8:	Disable Almost Full Sign */
#define MFF_ENA_W4E		BIT_7S		/* Enable  Wait for Empty */
#define MFF_DIS_W4E		BIT_6S		/* Disable Wait for Empty */
/*	MFF_ENA_FLUSH	 (see RX_MFF_CTRL1) Bit	 5:	Enable  Frame Flushing */
/*	MFF_DIS_FLUSH	 (see RX_MFF_CTRL1) Bit	 4:	Disable Frame Flushing */
#define MFF_ENA_LOOPB	BIT_3S		/* Enable  Loopback */
#define MFF_DIS_LOOPB	BIT_2S		/* Disable Loopback */
#define MFF_CLR_MAC_RST	BIT_1S		/* Clear XMAC Reset */
#define MFF_SET_MAC_RST	BIT_0S		/* Set   XMAC Reset */

#define MFF_TX_CTRL_DEF	(MFF_ENA_PKT_REC | MFF_ENA_TIM_PAT | MFF_ENA_FLUSH)

/*	RX_MFF_TST2	 	 8 bit	Receive MAC FIFO Test Register 2 */
/*	TX_MFF_TST2	 	 8 bit	Transmit MAC FIFO Test Register 2 */
								/* Bit 7:	reserved */
#define MFF_WSP_T_ON	BIT_6S	/* Tx: Write Shadow Ptr TestOn */
#define MFF_WSP_T_OFF	BIT_5S	/* Tx: Write Shadow Ptr TstOff */
#define MFF_WSP_INC		BIT_4S	/* Tx: Write Shadow Ptr Increment */
#define MFF_PC_DEC		BIT_3S	/* Packet Counter Decrement */
#define MFF_PC_T_ON		BIT_2S	/* Packet Counter Test On */
#define MFF_PC_T_OFF	BIT_1S	/* Packet Counter Test Off */
#define MFF_PC_INC		BIT_0S	/* Packet Counter Increment */

/*	RX_MFF_TST1	 	 8 bit	Receive MAC FIFO Test Register 1 */
/*	TX_MFF_TST1	 	 8 bit	Transmit MAC FIFO Test Register 1 */
					/* Bit 7:	reserved */
#define MFF_WP_T_ON		BIT_6S	/* Write Pointer Test On */
#define MFF_WP_T_OFF	BIT_5S	/* Write Pointer Test Off */
#define MFF_WP_INC		BIT_4S	/* Write Pointer Increm */
							/* Bit 3:	reserved */
#define MFF_RP_T_ON		BIT_2S	/* Read Pointer Test On */
#define MFF_RP_T_OFF	BIT_1S	/* Read Pointer Test Off */
#define MFF_RP_DEC		BIT_0S	/* Read Pointer Decrement */

/*	RX_MFF_CTRL2	 8 bit	Receive MAC FIFO Control Reg 2 */
/*	TX_MFF_CTRL2	 8 bit	Transmit MAC FIFO Control Reg 2 */
								/* Bit 7..4:	reserved */
#define MFF_ENA_OP_MD	BIT_3S	/* Enable  Operation Mode */
#define MFF_DIS_OP_MD	BIT_2S	/* Disable Operation Mode */
#define MFF_RST_CLR		BIT_1S	/* Clear MAC FIFO Reset */
#define MFF_RST_SET		BIT_0S	/* Set   MAC FIFO Reset */


/*	Link LED Counter Registers (GENESIS only) */

/*	RX_LED_CTRL		 8 bit	Receive LED Cnt Control Reg */
/*	TX_LED_CTRL		 8 bit	Transmit LED Cnt Control Reg */
/*	LNK_SYNC_CTRL	 8 bit	Link Sync Cnt Control Register */
							/* Bit 7.. 3:	reserved */
#define LED_START		BIT_2S	/* Start Timer */
#define LED_STOP		BIT_1S	/* Stop Timer */
#define LED_STATE		BIT_0S	/* Rx/Tx: LED State, 1=LED on */
#define LED_CLR_IRQ		BIT_0S	/* Lnk: 	Clear Link IRQ */

/*	RX_LED_TST		 8 bit	Receive LED Cnt Test Register */
/*	TX_LED_TST		 8 bit	Transmit LED Cnt Test Register */
/*	LNK_SYNC_TST	 8 bit	Link Sync Cnt Test Register */
							/* Bit 7.. 3:	reserved */
#define LED_T_ON		BIT_2S	/* LED Counter Test mode On */
#define LED_T_OFF		BIT_1S	/* LED Counter Test mode Off */
#define LED_T_STEP		BIT_0S	/* LED Counter Step */

/*	LNK_LED_REG	 	 8 bit	Link LED Register */
								/* Bit 7.. 6:	reserved */
#define LED_BLK_ON		BIT_5S	/* Link LED Blinking On */
#define LED_BLK_OFF		BIT_4S	/* Link LED Blinking Off */
#define LED_SYNC_ON		BIT_3S	/* Use Sync Wire to switch LED */
#define LED_SYNC_OFF	BIT_2S	/* Disable Sync Wire Input */
#define LED_ON			BIT_1S	/* switch LED on */
#define LED_OFF			BIT_0S	/* switch LED off */

/*	Receive and Transmit GMAC FIFO Registers (YUKON only) */

/*	RX_GMF_EA		32 bit	Rx GMAC FIFO End Address */
/*	RX_GMF_AF_THR	32 bit	Rx GMAC FIFO Almost Full Thresh. */
/*	RX_GMF_WP		32 bit 	Rx GMAC FIFO Write Pointer */
/*	RX_GMF_WLEV		32 bit 	Rx GMAC FIFO Write Level */
/*	RX_GMF_RP		32 bit 	Rx GMAC FIFO Read Pointer */
/*	RX_GMF_RLEV		32 bit 	Rx GMAC FIFO Read Level */
/*	TX_GMF_EA		32 bit	Tx GMAC FIFO End Address */
/*	TX_GMF_AE_THR	32 bit	Tx GMAC FIFO Almost Empty Thresh.*/
/*	TX_GMF_WP		32 bit 	Tx GMAC FIFO Write Pointer */
/*	TX_GMF_WSP		32 bit 	Tx GMAC FIFO Write Shadow Ptr. */
/*	TX_GMF_WLEV		32 bit 	Tx GMAC FIFO Write Level */
/*	TX_GMF_RP		32 bit 	Tx GMAC FIFO Read Pointer */
/*	TX_GMF_RSTP		32 bit 	Tx GMAC FIFO Restart Pointer */
/*	TX_GMF_RLEV		32 bit 	Tx GMAC FIFO Read Level */

/*	RX_GMF_CTRL_T	32 bit	Rx GMAC FIFO Control/Test */
						/* Bits 31..15:	reserved */
#define GMF_WP_TST_ON	BIT_14		/* Write Pointer Test On */
#define GMF_WP_TST_OFF	BIT_13		/* Write Pointer Test Off */
#define GMF_WP_STEP		BIT_12		/* Write Pointer Step/Increment */
						/* Bit 11:	reserved */
#define GMF_RP_TST_ON	BIT_10		/* Read Pointer Test On */
#define GMF_RP_TST_OFF	BIT_9		/* Read Pointer Test Off */
#define GMF_RP_STEP		BIT_8		/* Read Pointer Step/Increment */
#define GMF_RX_F_FL_ON	BIT_7		/* Rx FIFO Flush Mode On */
#define GMF_RX_F_FL_OFF	BIT_6		/* Rx FIFO Flush Mode Off */
#define GMF_CLI_RX_FO	BIT_5		/* Clear IRQ Rx FIFO Overrun */
#define GMF_CLI_RX_FC	BIT_4		/* Clear IRQ Rx Frame Complete */
#define GMF_OPER_ON		BIT_3		/* Operational Mode On */
#define GMF_OPER_OFF	BIT_2		/* Operational Mode Off */
#define GMF_RST_CLR		BIT_1		/* Clear GMAC FIFO Reset */
#define GMF_RST_SET		BIT_0		/* Set   GMAC FIFO Reset */

/*	TX_GMF_CTRL_T	32 bit	Tx GMAC FIFO Control/Test */
						/* Bits 31..19:	reserved */
#define GMF_WSP_TST_ON	BIT_18		/* Write Shadow Pointer Test On */
#define GMF_WSP_TST_OFF	BIT_17		/* Write Shadow Pointer Test Off */
#define GMF_WSP_STEP	BIT_16		/* Write Shadow Pointer Step/Increment */
						/* Bits 15..7: same as for RX_GMF_CTRL_T */
#define GMF_CLI_TX_FU	BIT_6		/* Clear IRQ Tx FIFO Underrun */
#define GMF_CLI_TX_FC	BIT_5		/* Clear IRQ Tx Frame Complete */
#define GMF_CLI_TX_PE	BIT_4		/* Clear IRQ Tx Parity Error */
						/* Bits 3..0: same as for RX_GMF_CTRL_T */

#define GMF_RX_CTRL_DEF		(GMF_OPER_ON | GMF_RX_F_FL_ON)
#define GMF_TX_CTRL_DEF		GMF_OPER_ON

#define RX_GMF_FL_THR_DEF	0x0a	/* Rx GMAC FIFO Flush Threshold default */

/*	GMAC_TI_ST_CTRL	 8 bit	Time Stamp Timer Ctrl Reg (YUKON only) */
								/* Bit 7.. 3:	reserved */
#define GMT_ST_START	BIT_2S		/* Start Time Stamp Timer */
#define GMT_ST_STOP		BIT_1S		/* Stop  Time Stamp Timer */
#define GMT_ST_CLR_IRQ	BIT_0S		/* Clear Time Stamp Timer IRQ */

/*	GMAC_CTRL		32 bit	GMAC Control Reg (YUKON only) */
						/* Bits 31.. 8:	reserved */
#define GMC_H_BURST_ON	BIT_7		/* Half Duplex Burst Mode On */
#define GMC_H_BURST_OFF	BIT_6		/* Half Duplex Burst Mode Off */
#define GMC_F_LOOPB_ON	BIT_5		/* FIFO Loopback On */
#define GMC_F_LOOPB_OFF	BIT_4		/* FIFO Loopback Off */
#define GMC_PAUSE_ON	BIT_3		/* Pause On */
#define GMC_PAUSE_OFF	BIT_2		/* Pause Off */
#define GMC_RST_CLR		BIT_1		/* Clear GMAC Reset */
#define GMC_RST_SET		BIT_0		/* Set   GMAC Reset */

/*	GPHY_CTRL		32 bit	GPHY Control Reg (YUKON only) */
						/* Bits 31..29:	reserved */
#define GPC_SEL_BDT		BIT_28	/* Select Bi-Dir. Transfer for MDC/MDIO */
#define GPC_INT_POL_HI	BIT_27	/* IRQ Polarity is Active HIGH */
#define GPC_75_OHM		BIT_26	/* Use 75 Ohm Termination instead of 50 */
#define GPC_DIS_FC		BIT_25	/* Disable Automatic Fiber/Copper Detection */
#define GPC_DIS_SLEEP	BIT_24	/* Disable Energy Detect */
#define GPC_HWCFG_M_3	BIT_23	/* HWCFG_MODE[3] */
#define GPC_HWCFG_M_2	BIT_22	/* HWCFG_MODE[2] */
#define GPC_HWCFG_M_1	BIT_21	/* HWCFG_MODE[1] */
#define GPC_HWCFG_M_0	BIT_20	/* HWCFG_MODE[0] */
#define GPC_ANEG_0		BIT_19	/* ANEG[0] */
#define GPC_ENA_XC		BIT_18	/* Enable MDI crossover */
#define GPC_DIS_125		BIT_17	/* Disable 125 MHz clock */
#define GPC_ANEG_3		BIT_16	/* ANEG[3] */
#define GPC_ANEG_2		BIT_15	/* ANEG[2] */
#define GPC_ANEG_1		BIT_14	/* ANEG[1] */
#define GPC_ENA_PAUSE	BIT_13	/* Enable Pause (SYM_OR_REM) */
#define GPC_PHYADDR_4	BIT_12	/* Bit 4 of Phy Addr */
#define GPC_PHYADDR_3	BIT_11	/* Bit 3 of Phy Addr */
#define GPC_PHYADDR_2	BIT_10	/* Bit 2 of Phy Addr */
#define GPC_PHYADDR_1	BIT_9	/* Bit 1 of Phy Addr */
#define GPC_PHYADDR_0	BIT_8	/* Bit 0 of Phy Addr */
						/* Bits  7..2:	reserved */
#define GPC_RST_CLR		BIT_1	/* Clear GPHY Reset */
#define GPC_RST_SET		BIT_0	/* Set   GPHY Reset */

#define GPC_HWCFG_GMII_COP	(GPC_HWCFG_M_3 | GPC_HWCFG_M_2 | \
							 GPC_HWCFG_M_1 | GPC_HWCFG_M_0)

#define GPC_HWCFG_GMII_FIB	(				 GPC_HWCFG_M_2 | \
							 GPC_HWCFG_M_1 | GPC_HWCFG_M_0)

#define GPC_ANEG_ADV_ALL_M	(GPC_ANEG_3 | GPC_ANEG_2 | \
							 GPC_ANEG_1 | GPC_ANEG_0)

/* forced speed and duplex mode (don't mix with other ANEG bits) */
#define GPC_FRC10MBIT_HALF	0
#define GPC_FRC10MBIT_FULL	GPC_ANEG_0
#define GPC_FRC100MBIT_HALF	GPC_ANEG_1
#define GPC_FRC100MBIT_FULL	(GPC_ANEG_0 | GPC_ANEG_1)

/* auto-negotiation with limited advertised speeds */
/* mix only with master/slave settings (for copper) */
#define GPC_ADV_1000_HALF	GPC_ANEG_2
#define GPC_ADV_1000_FULL	GPC_ANEG_3
#define GPC_ADV_ALL			(GPC_ANEG_2 | GPC_ANEG_3)

/* master/slave settings */
/* only for copper with 1000 Mbps */
#define GPC_FORCE_MASTER	0
#define GPC_FORCE_SLAVE		GPC_ANEG_0
#define GPC_PREF_MASTER		GPC_ANEG_1
#define GPC_PREF_SLAVE		(GPC_ANEG_1 | GPC_ANEG_0)

/*	GMAC_IRQ_SRC	 8 bit	GMAC Interrupt Source Reg (YUKON only) */
/*	GMAC_IRQ_MSK	 8 bit	GMAC Interrupt Mask   Reg (YUKON only) */
#define GM_IS_TX_CO_OV	BIT_5		/* Transmit Counter Overflow IRQ */
#define GM_IS_RX_CO_OV	BIT_4		/* Receive Counter Overflow IRQ */
#define GM_IS_TX_FF_UR	BIT_3		/* Transmit FIFO Underrun */
#define GM_IS_TX_COMPL	BIT_2		/* Frame Transmission Complete */
#define GM_IS_RX_FF_OR	BIT_1		/* Receive FIFO Overrun */
#define GM_IS_RX_COMPL	BIT_0		/* Frame Reception Complete */

#define GMAC_DEF_MSK	(GM_IS_TX_CO_OV | GM_IS_RX_CO_OV | \
						GM_IS_TX_FF_UR)

/*	GMAC_LINK_CTRL	16 bit	GMAC Link Control Reg (YUKON only) */
						/* Bits 15.. 2:	reserved */
#define GMLC_RST_CLR	BIT_1S		/* Clear GMAC Link Reset */
#define GMLC_RST_SET	BIT_0S		/* Set   GMAC Link Reset */


/*	WOL_CTRL_STAT	16 bit	WOL Control/Status Reg */
#define WOL_CTL_LINK_CHG_OCC			BIT_15S
#define WOL_CTL_MAGIC_PKT_OCC			BIT_14S
#define WOL_CTL_PATTERN_OCC				BIT_13S

#define WOL_CTL_CLEAR_RESULT			BIT_12S

#define WOL_CTL_ENA_PME_ON_LINK_CHG		BIT_11S
#define WOL_CTL_DIS_PME_ON_LINK_CHG		BIT_10S
#define WOL_CTL_ENA_PME_ON_MAGIC_PKT	BIT_9S
#define WOL_CTL_DIS_PME_ON_MAGIC_PKT	BIT_8S
#define WOL_CTL_ENA_PME_ON_PATTERN		BIT_7S
#define WOL_CTL_DIS_PME_ON_PATTERN		BIT_6S

#define WOL_CTL_ENA_LINK_CHG_UNIT		BIT_5S
#define WOL_CTL_DIS_LINK_CHG_UNIT		BIT_4S
#define WOL_CTL_ENA_MAGIC_PKT_UNIT		BIT_3S
#define WOL_CTL_DIS_MAGIC_PKT_UNIT		BIT_2S
#define WOL_CTL_ENA_PATTERN_UNIT		BIT_1S
#define WOL_CTL_DIS_PATTERN_UNIT		BIT_0S

#define WOL_CTL_DEFAULT				\
	(WOL_CTL_DIS_PME_ON_LINK_CHG |	\
	WOL_CTL_DIS_PME_ON_PATTERN |	\
	WOL_CTL_DIS_PME_ON_MAGIC_PKT |	\
	WOL_CTL_DIS_LINK_CHG_UNIT |		\
	WOL_CTL_DIS_PATTERN_UNIT |		\
	WOL_CTL_DIS_MAGIC_PKT_UNIT)

/*	WOL_MATCH_CTL	 8 bit	WOL Match Control Reg */
#define WOL_CTL_PATT_ENA(x)				(BIT_0 << (x))

#define SK_NUM_WOL_PATTERN		7
#define SK_PATTERN_PER_WORD		4
#define SK_BITMASK_PATTERN		7
#define SK_POW_PATTERN_LENGTH	128

#define WOL_LENGTH_MSK		0x7f
#define WOL_LENGTH_SHIFT	8


/* Receive and Transmit Descriptors ******************************************/

/* Transmit Descriptor struct */
typedef	struct s_HwTxd {
	SK_U32 volatile	TxCtrl;	/* Transmit Buffer Control Field */
	SK_U32	TxNext;			/* Physical Address Pointer to the next TxD */
	SK_U32	TxAdrLo;		/* Physical Tx Buffer Address lower dword */
	SK_U32	TxAdrHi;		/* Physical Tx Buffer Address upper dword */
	SK_U32	TxStat;			/* Transmit Frame Status Word */
#ifndef	SK_USE_REV_DESC
	SK_U16	TxTcpOffs;		/* TCP Checksum Calculation Start Value */
	SK_U16	TxRes1;			/* 16 bit reserved field */
	SK_U16	TxTcpWp;		/* TCP Checksum Write Position */
	SK_U16	TxTcpSp;		/* TCP Checksum Calculation Start Position */
#else	/* SK_USE_REV_DESC */
	SK_U16	TxRes1;			/* 16 bit reserved field */
	SK_U16	TxTcpOffs;		/* TCP Checksum Calculation Start Value */
	SK_U16	TxTcpSp;		/* TCP Checksum Calculation Start Position */
	SK_U16	TxTcpWp;		/* TCP Checksum Write Position */
#endif	/* SK_USE_REV_DESC */
	SK_U32  TxRes2;			/* 32 bit reserved field */
} SK_HWTXD;

/* Receive Descriptor struct */
typedef	struct s_HwRxd {
	SK_U32 volatile RxCtrl;	/* Receive Buffer Control Field */
	SK_U32	RxNext;			/* Physical Address Pointer to the next RxD */
	SK_U32	RxAdrLo;		/* Physical Rx Buffer Address lower dword */
	SK_U32	RxAdrHi;		/* Physical Rx Buffer Address upper dword */
	SK_U32	RxStat;			/* Receive Frame Status Word */
	SK_U32	RxTiSt;			/* Receive Time Stamp (from XMAC on GENESIS) */
#ifndef	SK_USE_REV_DESC
	SK_U16	RxTcpSum1;		/* TCP Checksum 1 */
	SK_U16	RxTcpSum2;		/* TCP Checksum 2 */
	SK_U16	RxTcpSp1;		/* TCP Checksum Calculation Start Position 1 */
	SK_U16	RxTcpSp2;		/* TCP Checksum Calculation Start Position 2 */
#else	/* SK_USE_REV_DESC */
	SK_U16	RxTcpSum2;		/* TCP Checksum 2 */
	SK_U16	RxTcpSum1;		/* TCP Checksum 1 */
	SK_U16	RxTcpSp2;		/* TCP Checksum Calculation Start Position 2 */
	SK_U16	RxTcpSp1;		/* TCP Checksum Calculation Start Position 1 */
#endif	/* SK_USE_REV_DESC */
} SK_HWRXD;

/*
 * Drivers which use the reverse descriptor feature (PCI_OUR_REG_2)
 * should set the define SK_USE_REV_DESC.
 * Structures are 'normaly' not endianess dependent. But in
 * this case the SK_U16 fields are bound to bit positions inside the
 * descriptor. RxTcpSum1 e.g. must start at bit 0 within the 6.th DWord.
 * The bit positions inside a DWord are of course endianess dependent and
 * swaps if the DWord is swapped by the hardware.
 */


/* Descriptor Bit Definition */
/*	TxCtrl		Transmit Buffer Control Field */
/*	RxCtrl		Receive  Buffer Control Field */
#define BMU_OWN			BIT_31	/* OWN bit: 0=host/1=BMU */
#define BMU_STF			BIT_30	/* Start of Frame */
#define BMU_EOF			BIT_29	/* End of Frame */
#define BMU_IRQ_EOB		BIT_28	/* Req "End of Buffer" IRQ */
#define BMU_IRQ_EOF		BIT_27	/* Req "End of Frame" IRQ */
/* TxCtrl specific bits */
#define BMU_STFWD		BIT_26	/* (Tx)	Store & Forward Frame */
#define BMU_NO_FCS		BIT_25	/* (Tx) Disable MAC FCS (CRC) generation */
#define BMU_SW			BIT_24	/* (Tx)	1 bit res. for SW use */
/* RxCtrl specific bits */
#define BMU_DEV_0		BIT_26	/* (Rx)	Transfer data to Dev0 */
#define BMU_STAT_VAL	BIT_25	/* (Rx)	Rx Status Valid */
#define BMU_TIST_VAL	BIT_24	/* (Rx)	Rx TimeStamp Valid */
								/* Bit 23..16:	BMU Check Opcodes */
#define BMU_CHECK		(0x55L<<16)	/* Default BMU check */
#define BMU_TCP_CHECK	(0x56L<<16)	/* Descr with TCP ext */
#define BMU_UDP_CHECK	(0x57L<<16)	/* Descr with UDP ext (YUKON only) */
#define BMU_BBC			0xffffL	/* Bit 15.. 0:	Buffer Byte Counter */

/*	TxStat		Transmit Frame Status Word */
/*	RxStat		Receive Frame Status Word */
/*
 *Note: TxStat is reserved for ASIC loopback mode only
 *
 *	The Bits of the Status words are defined in xmac_ii.h
 *	(see XMR_FS bits)
 */

/* macros ********************************************************************/

/* Receive and Transmit Queues */
#define Q_R1	0x0000		/* Receive Queue 1 */
#define Q_R2	0x0080		/* Receive Queue 2 */
#define Q_XS1	0x0200		/* Synchronous Transmit Queue 1 */
#define Q_XA1	0x0280		/* Asynchronous Transmit Queue 1 */
#define Q_XS2	0x0300		/* Synchronous Transmit Queue 2 */
#define Q_XA2	0x0380		/* Asynchronous Transmit Queue 2 */

/*
 *	Macro Q_ADDR()
 *
 *	Use this macro to access the Receive and Transmit Queue Registers.
 *
 * para:
 *	Queue	Queue to access.
 *				Values: Q_R1, Q_R2, Q_XS1, Q_XA1, Q_XS2, and Q_XA2
 *	Offs	Queue register offset.
 *				Values: Q_D, Q_DA_L ... Q_T2, Q_T3
 *
 * usage	SK_IN32(pAC, Q_ADDR(Q_R2, Q_BC), pVal)
 */
#define Q_ADDR(Queue, Offs)	(B8_Q_REGS + (Queue) + (Offs))

/*
 *	Macro RB_ADDR()
 *
 *	Use this macro to access the RAM Buffer Registers.
 *
 * para:
 *	Queue	Queue to access.
 *				Values: Q_R1, Q_R2, Q_XS1, Q_XA1, Q_XS2, and Q_XA2
 *	Offs	Queue register offset.
 *				Values: RB_START, RB_END ... RB_LEV, RB_CTRL
 *
 * usage	SK_IN32(pAC, RB_ADDR(Q_R2, RB_RP), pVal)
 */
#define RB_ADDR(Queue, Offs)	(B16_RAM_REGS + (Queue) + (Offs))


/* MAC Related Registers */
#define MAC_1		0	/* belongs to the port near the slot */
#define MAC_2		1	/* belongs to the port far away from the slot */

/*
 *	Macro MR_ADDR()
 *
 *	Use this macro to access a MAC Related Registers inside the ASIC.
 *
 * para:
 *	Mac		MAC to access.
 *				Values: MAC_1, MAC_2
 *	Offs	MAC register offset.
 *				Values: RX_MFF_EA, RX_MFF_WP ... LNK_LED_REG,
 *						TX_MFF_EA, TX_MFF_WP ... TX_LED_TST
 *
 * usage	SK_IN32(pAC, MR_ADDR(MAC_1, TX_MFF_EA), pVal)
 */
#define MR_ADDR(Mac, Offs)	(((Mac) << 7) + (Offs))

#ifdef	SK_LITTLE_ENDIAN
#define XM_WORD_LO	0
#define XM_WORD_HI	1
#else	/* !SK_LITTLE_ENDIAN */
#define XM_WORD_LO	1
#define XM_WORD_HI	0
#endif	/* !SK_LITTLE_ENDIAN */


/*
 * macros to access the XMAC (GENESIS only)
 *
 * XM_IN16(),		to read a 16 bit register (e.g. XM_MMU_CMD)
 * XM_OUT16(),		to write a 16 bit register (e.g. XM_MMU_CMD)
 * XM_IN32(),		to read a 32 bit register (e.g. XM_TX_EV_CNT)
 * XM_OUT32(),		to write a 32 bit register (e.g. XM_TX_EV_CNT)
 * XM_INADDR(),		to read a network address register (e.g. XM_SRC_CHK)
 * XM_OUTADDR(),	to write a network address register (e.g. XM_SRC_CHK)
 * XM_INHASH(),		to read the XM_HSM_CHK register
 * XM_OUTHASH()		to write the XM_HSM_CHK register
 *
 * para:
 *	Mac		XMAC to access		values: MAC_1 or MAC_2
 *	IoC		I/O context needed for SK I/O macros
 *	Reg		XMAC Register to read or write
 *	(p)Val	Value or pointer to the value which should be read or written
 *
 * usage:	XM_OUT16(IoC, MAC_1, XM_MMU_CMD, Value);
 */

#define XMA(Mac, Reg)									\
	((BASE_XMAC_1 + (Mac) * (BASE_XMAC_2 - BASE_XMAC_1)) | ((Reg) << 1))

#define XM_IN16(IoC, Mac, Reg, pVal)					\
	SK_IN16((IoC), XMA((Mac), (Reg)), (pVal))

#define XM_OUT16(IoC, Mac, Reg, Val)					\
	SK_OUT16((IoC), XMA((Mac), (Reg)), (Val))

#define XM_IN32(IoC, Mac, Reg, pVal) {					\
	SK_IN16((IoC), XMA((Mac), (Reg)),					\
		(SK_U16 SK_FAR*)&((SK_U16 SK_FAR*)(pVal))[XM_WORD_LO]);		\
	SK_IN16((IoC), XMA((Mac), (Reg+2)),					\
		(SK_U16 SK_FAR*)&((SK_U16 SK_FAR*)(pVal))[XM_WORD_HI]);		\
}

#define XM_OUT32(IoC, Mac, Reg, Val) {										\
	SK_OUT16((IoC), XMA((Mac), (Reg)), (SK_U16)((Val) & 0xffffL));			\
	SK_OUT16((IoC), XMA((Mac), (Reg+2)), (SK_U16)(((Val) >> 16) & 0xffffL));\
}

/* Remember: we are always writing to / reading from LITTLE ENDIAN memory */

#define XM_INADDR(IoC, Mac, Reg, pVal) {				\
	SK_U16	Word;										\
	SK_U8	*pByte;										\
	pByte = (SK_U8 *)&((SK_U8 *)(pVal))[0];				\
	SK_IN16((IoC), XMA((Mac), (Reg)), &Word);			\
	pByte[0] = (SK_U8)(Word  & 0x00ff);					\
	pByte[1] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), XMA((Mac), (Reg+2)), &Word);			\
	pByte[2] = (SK_U8)(Word  & 0x00ff);					\
	pByte[3] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), XMA((Mac), (Reg+4)), &Word);			\
	pByte[4] = (SK_U8)(Word  & 0x00ff);					\
	pByte[5] = (SK_U8)((Word >> 8) & 0x00ff);			\
}

#define XM_OUTADDR(IoC, Mac, Reg, pVal) {				\
	SK_U8	SK_FAR *pByte;								\
	pByte = (SK_U8 SK_FAR *)&((SK_U8 SK_FAR *)(pVal))[0];	\
	SK_OUT16((IoC), XMA((Mac), (Reg)), (SK_U16)			\
		(((SK_U16)(pByte[0]) & 0x00ff) |				\
		(((SK_U16)(pByte[1]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), XMA((Mac), (Reg+2)), (SK_U16)		\
		(((SK_U16)(pByte[2]) & 0x00ff) |				\
		(((SK_U16)(pByte[3]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), XMA((Mac), (Reg+4)), (SK_U16)		\
		(((SK_U16)(pByte[4]) & 0x00ff) |				\
		(((SK_U16)(pByte[5]) << 8) & 0xff00)));			\
}

#define XM_INHASH(IoC, Mac, Reg, pVal) {				\
	SK_U16	Word;										\
	SK_U8	SK_FAR *pByte;								\
	pByte = (SK_U8 SK_FAR *)&((SK_U8 SK_FAR *)(pVal))[0];	\
	SK_IN16((IoC), XMA((Mac), (Reg)), &Word);			\
	pByte[0] = (SK_U8)(Word  & 0x00ff);					\
	pByte[1] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), XMA((Mac), (Reg+2)), &Word);			\
	pByte[2] = (SK_U8)(Word  & 0x00ff);					\
	pByte[3] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), XMA((Mac), (Reg+4)), &Word);			\
	pByte[4] = (SK_U8)(Word  & 0x00ff);					\
	pByte[5] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), XMA((Mac), (Reg+6)), &Word);			\
	pByte[6] = (SK_U8)(Word  & 0x00ff);					\
	pByte[7] = (SK_U8)((Word >> 8) & 0x00ff);			\
}

#define XM_OUTHASH(IoC, Mac, Reg, pVal) {				\
	SK_U8	SK_FAR *pByte;								\
	pByte = (SK_U8 SK_FAR *)&((SK_U8 SK_FAR *)(pVal))[0];	\
	SK_OUT16((IoC), XMA((Mac), (Reg)), (SK_U16)			\
		(((SK_U16)(pByte[0]) & 0x00ff)|					\
		(((SK_U16)(pByte[1]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), XMA((Mac), (Reg+2)), (SK_U16)		\
		(((SK_U16)(pByte[2]) & 0x00ff)|					\
		(((SK_U16)(pByte[3]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), XMA((Mac), (Reg+4)), (SK_U16)		\
		(((SK_U16)(pByte[4]) & 0x00ff)|					\
		(((SK_U16)(pByte[5]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), XMA((Mac), (Reg+6)), (SK_U16)		\
		(((SK_U16)(pByte[6]) & 0x00ff)|					\
		(((SK_U16)(pByte[7]) << 8) & 0xff00)));			\
}

/*
 * macros to access the GMAC (YUKON only)
 *
 * GM_IN16(),		to read  a 16 bit register (e.g. GM_GP_STAT)
 * GM_OUT16(),		to write a 16 bit register (e.g. GM_GP_CTRL)
 * GM_IN32(),		to read  a 32 bit register (e.g. GM_)
 * GM_OUT32(),		to write a 32 bit register (e.g. GM_)
 * GM_INADDR(),		to read  a network address register (e.g. GM_SRC_ADDR_1L)
 * GM_OUTADDR(),	to write a network address register (e.g. GM_SRC_ADDR_2L)
 * GM_INHASH(),		to read  the GM_MC_ADDR_H1 register
 * GM_OUTHASH()		to write the GM_MC_ADDR_H1 register
 *
 * para:
 *	Mac		GMAC to access		values: MAC_1 or MAC_2
 *	IoC		I/O context needed for SK I/O macros
 *	Reg		GMAC Register to read or write
 *	(p)Val	Value or pointer to the value which should be read or written
 *
 * usage:	GM_OUT16(IoC, MAC_1, GM_GP_CTRL, Value);
 */

#define GMA(Mac, Reg)									\
	((BASE_GMAC_1 + (Mac) * (BASE_GMAC_2 - BASE_GMAC_1)) | (Reg))

#define GM_IN16(IoC, Mac, Reg, pVal)					\
	SK_IN16((IoC), GMA((Mac), (Reg)), (pVal))

#define GM_OUT16(IoC, Mac, Reg, Val)					\
	SK_OUT16((IoC), GMA((Mac), (Reg)), (Val))

#define GM_IN32(IoC, Mac, Reg, pVal) {					\
	SK_IN16((IoC), GMA((Mac), (Reg)),					\
		(SK_U16 SK_FAR*)&((SK_U16 SK_FAR*)(pVal))[XM_WORD_LO]);		\
	SK_IN16((IoC), GMA((Mac), (Reg+4)),					\
		(SK_U16 SK_FAR*)&((SK_U16 SK_FAR*)(pVal))[XM_WORD_HI]);		\
}

#define GM_OUT32(IoC, Mac, Reg, Val) {										\
	SK_OUT16((IoC), GMA((Mac), (Reg)), (SK_U16)((Val) & 0xffffL));			\
	SK_OUT16((IoC), GMA((Mac), (Reg+4)), (SK_U16)(((Val) >> 16) & 0xffffL));\
}

#define GM_INADDR(IoC, Mac, Reg, pVal) {				\
	SK_U16	Word;										\
	SK_U8	*pByte;										\
	pByte = (SK_U8 *)&((SK_U8 *)(pVal))[0];				\
	SK_IN16((IoC), GMA((Mac), (Reg)), &Word);			\
	pByte[0] = (SK_U8)(Word  & 0x00ff);					\
	pByte[1] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), GMA((Mac), (Reg+4)), &Word);			\
	pByte[2] = (SK_U8)(Word  & 0x00ff);					\
	pByte[3] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), GMA((Mac), (Reg+8)), &Word);			\
	pByte[4] = (SK_U8)(Word  & 0x00ff);					\
	pByte[5] = (SK_U8)((Word >> 8) & 0x00ff);			\
}

#define GM_OUTADDR(IoC, Mac, Reg, pVal) {				\
	SK_U8	SK_FAR *pByte;								\
	pByte = (SK_U8 SK_FAR *)&((SK_U8 SK_FAR *)(pVal))[0];	\
	SK_OUT16((IoC), GMA((Mac), (Reg)), (SK_U16)			\
		(((SK_U16)(pByte[0]) & 0x00ff) |				\
		(((SK_U16)(pByte[1]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), GMA((Mac), (Reg+4)), (SK_U16)		\
		(((SK_U16)(pByte[2]) & 0x00ff) |				\
		(((SK_U16)(pByte[3]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), GMA((Mac), (Reg+8)), (SK_U16)		\
		(((SK_U16)(pByte[4]) & 0x00ff) |				\
		(((SK_U16)(pByte[5]) << 8) & 0xff00)));			\
}

#define GM_INHASH(IoC, Mac, Reg, pVal) {				\
	SK_U16	Word;										\
	SK_U8	*pByte;										\
	pByte = (SK_U8 *)&((SK_U8 *)(pVal))[0];				\
	SK_IN16((IoC), GMA((Mac), (Reg)), &Word);			\
	pByte[0] = (SK_U8)(Word  & 0x00ff);					\
	pByte[1] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), GMA((Mac), (Reg+4)), &Word);			\
	pByte[2] = (SK_U8)(Word  & 0x00ff);					\
	pByte[3] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), GMA((Mac), (Reg+8)), &Word);			\
	pByte[4] = (SK_U8)(Word  & 0x00ff);					\
	pByte[5] = (SK_U8)((Word >> 8) & 0x00ff);			\
	SK_IN16((IoC), GMA((Mac), (Reg+12)), &Word);		\
	pByte[6] = (SK_U8)(Word  & 0x00ff);					\
	pByte[7] = (SK_U8)((Word >> 8) & 0x00ff);			\
}

#define GM_OUTHASH(IoC, Mac, Reg, pVal) {				\
	SK_U8	*pByte;										\
	pByte = (SK_U8 *)&((SK_U8 *)(pVal))[0];				\
	SK_OUT16((IoC), GMA((Mac), (Reg)), (SK_U16)			\
		(((SK_U16)(pByte[0]) & 0x00ff)|					\
		(((SK_U16)(pByte[1]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), GMA((Mac), (Reg+4)), (SK_U16)		\
		(((SK_U16)(pByte[2]) & 0x00ff)|					\
		(((SK_U16)(pByte[3]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), GMA((Mac), (Reg+8)), (SK_U16)		\
		(((SK_U16)(pByte[4]) & 0x00ff)|					\
		(((SK_U16)(pByte[5]) << 8) & 0xff00)));			\
	SK_OUT16((IoC), GMA((Mac), (Reg+12)), (SK_U16)		\
		(((SK_U16)(pByte[6]) & 0x00ff)|					\
		(((SK_U16)(pByte[7]) << 8) & 0xff00)));			\
}

/*
 * Different MAC Types
 */
#define SK_MAC_XMAC		0	/* Xaqti XMAC II */
#define SK_MAC_GMAC		1	/* Marvell GMAC */

/*
 * Different PHY Types
 */
#define SK_PHY_XMAC			0	/* integrated in XMAC II */
#define SK_PHY_BCOM			1	/* Broadcom BCM5400 */
#define SK_PHY_LONE			2	/* Level One LXT1000 */
#define SK_PHY_NAT			3	/* National DP83891 */
#define SK_PHY_MARV_COPPER	4	/* Marvell 88E1011S */
#define SK_PHY_MARV_FIBER	5	/* Marvell 88E1011S working on fiber */

/*
 * PHY addresses (bits 12..8 of PHY address reg)
 */
#define PHY_ADDR_XMAC	(0<<8)
#define PHY_ADDR_BCOM	(1<<8)
#define PHY_ADDR_LONE	(3<<8)
#define PHY_ADDR_NAT	(0<<8)

/* GPHY address (bits 15..11 of SMI control reg) */
#define PHY_ADDR_MARV	0

/*
 * macros to access the PHY
 *
 * PHY_READ()		read a 16 bit value from the PHY
 * PHY_WRITE()		write a 16 bit value to the PHY
 *
 * para:
 * 	IoC		I/O context needed for SK I/O macros
 * 	pPort	Pointer to port struct for PhyAddr
 * 	Mac		XMAC to access		values: MAC_1 or MAC_2
 * 	PhyReg	PHY Register to read or write
 * 	(p)Val	Value or pointer to the value which should be read or
 *			written.
 *
 * usage:	PHY_READ(IoC, pPort, MAC_1, PHY_CTRL, Value);
 * Warning: a PHY_READ on an uninitialized PHY (PHY still in reset) never
 *          comes back. This is checked in DEBUG mode.
 */
#ifndef DEBUG
#define PHY_READ(IoC, pPort, Mac, PhyReg, pVal) {						\
	SK_U16 Mmu;  														\
																		\
	XM_OUT16((IoC), (Mac), XM_PHY_ADDR, (PhyReg) | (pPort)->PhyAddr);	\
	XM_IN16((IoC), (Mac), XM_PHY_DATA, (pVal));							\
	if ((pPort)->PhyType != SK_PHY_XMAC) {								\
		do {  															\
			XM_IN16((IoC), (Mac), XM_MMU_CMD, &Mmu);					\
		} while ((Mmu & XM_MMU_PHY_RDY) == 0);							\
		XM_IN16((IoC), (Mac), XM_PHY_DATA, (pVal));						\
	}  																	\
}
#else
#define PHY_READ(IoC, pPort, Mac, PhyReg, pVal) {						\
	SK_U16 Mmu;  														\
	int __i = 0;														\
																		\
	XM_OUT16((IoC), (Mac), XM_PHY_ADDR, (PhyReg) | (pPort)->PhyAddr);	\
	XM_IN16((IoC), (Mac), XM_PHY_DATA, (pVal));							\
	if ((pPort)->PhyType != SK_PHY_XMAC) {								\
		do {  															\
			XM_IN16((IoC), (Mac), XM_MMU_CMD, &Mmu);					\
			__i++;														\
			if (__i > 100000) {											\
				SK_DBG_PRINTF("*****************************\n");		\
				SK_DBG_PRINTF("PHY_READ on uninitialized PHY\n");		\
				SK_DBG_PRINTF("*****************************\n");		\
				break;													\
			}															\
		} while ((Mmu & XM_MMU_PHY_RDY) == 0);							\
		XM_IN16((IoC), (Mac), XM_PHY_DATA, (pVal));						\
	}  																	\
}
#endif /* DEBUG */

#define PHY_WRITE(IoC, pPort, Mac, PhyReg, Val) {						\
	SK_U16 Mmu;															\
																		\
	if ((pPort)->PhyType != SK_PHY_XMAC) {								\
		do {  															\
			XM_IN16((IoC), (Mac), XM_MMU_CMD, &Mmu);					\
		} while ((Mmu & XM_MMU_PHY_BUSY) != 0);							\
	}  																	\
	XM_OUT16((IoC), (Mac), XM_PHY_ADDR, (PhyReg) | (pPort)->PhyAddr);	\
	XM_OUT16((IoC), (Mac), XM_PHY_DATA, (Val));							\
	if ((pPort)->PhyType != SK_PHY_XMAC) {								\
		do {  															\
			XM_IN16((IoC), (Mac), XM_MMU_CMD, &Mmu);					\
		} while ((Mmu & XM_MMU_PHY_BUSY) != 0);							\
	}  																	\
}

/*
 *	Macro PCI_C()
 *
 *	Use this macro to access PCI config register from the I/O space.
 *
 * para:
 *	Addr	PCI configuration register to access.
 *			Values:	PCI_VENDOR_ID ... PCI_VPD_ADR_REG,
 *
 * usage	SK_IN16(pAC, PCI_C(PCI_VENDOR_ID), pVal);
 */
#define PCI_C(Addr)	(B7_CFG_SPC + (Addr))	/* PCI Config Space */

/*
 *	Macro SK_HW_ADDR(Base, Addr)
 *
 *	Calculates the effective HW address
 *
 * para:
 *	Base	I/O or memory base address
 *	Addr	Address offset
 *
 * usage:	May be used in SK_INxx and SK_OUTxx macros
 *		#define SK_IN8(pAC, Addr, pVal) ...\
 *			*pVal = (SK_U8)inp(SK_HW_ADDR(pAC->Hw.Iop, Addr)))
 */
#ifdef SK_MEM_MAPPED_IO
#define SK_HW_ADDR(Base, Addr)	((Base) + (Addr))
#else  /* SK_MEM_MAPPED_IO */
#define SK_HW_ADDR(Base, Addr)	\
			((Base) + (((Addr) & 0x7f) | (((Addr) >> 7 > 0) ? 0x80 : 0)))
#endif /* SK_MEM_MAPPED_IO */

#define SZ_LONG	(sizeof(SK_U32))

/*
 *	Macro SK_HWAC_LINK_LED()
 *
 *	Use this macro to set the link LED mode.
 * para:
 *	pAC		Pointer to adapter context struct
 *	IoC		I/O context needed for SK I/O macros
 *  Port	Port number
 *	Mode	Mode to set for this LED
 */
#define SK_HWAC_LINK_LED(pAC, IoC, Port, Mode) \
	SK_OUT8(IoC, MR_ADDR(Port, LNK_LED_REG), Mode);


/* typedefs *******************************************************************/


/* function prototypes ********************************************************/

#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif	/* __INC_SKGEHW_H */
