/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register definitions for Samsung LTV350QV Quarter VGA LCD Panel
 *
 * Copyright (C) 2006, 2007 Atmel Corporation
 */
#ifndef __LTV350QV_H
#define __LTV350QV_H

#define LTV_OPC_INDEX	0x74
#define LTV_OPC_DATA	0x76

#define LTV_ID		0x00		/* ID Read */
#define LTV_IFCTL	0x01		/* Display Interface Control */
#define LTV_DATACTL	0x02		/* Display Data Control */
#define LTV_ENTRY_MODE	0x03		/* Entry Mode */
#define LTV_GATECTL1	0x04		/* Gate Control 1 */
#define LTV_GATECTL2	0x05		/* Gate Control 2 */
#define LTV_VBP		0x06		/* Vertical Back Porch */
#define LTV_HBP		0x07		/* Horizontal Back Porch */
#define LTV_SOTCTL	0x08		/* Source Output Timing Control */
#define LTV_PWRCTL1	0x09		/* Power Control 1 */
#define LTV_PWRCTL2	0x0a		/* Power Control 2 */
#define LTV_GAMMA(x)	(0x10 + (x))	/* Gamma control */

/* Bit definitions for LTV_IFCTL */
#define LTV_IM			(1 << 15)
#define LTV_NMD			(1 << 14)
#define LTV_SSMD		(1 << 13)
#define LTV_REV			(1 <<  7)
#define LTV_NL(x)		(((x) & 0x001f) << 0)

/* Bit definitions for LTV_DATACTL */
#define LTV_DS_SAME		(0 << 12)
#define LTV_DS_D_TO_S		(1 << 12)
#define LTV_DS_S_TO_D		(2 << 12)
#define LTV_CHS_384		(0 <<  9)
#define LTV_CHS_480		(1 <<  9)
#define LTV_CHS_492		(2 <<  9)
#define LTV_DF_RGB		(0 <<  6)
#define LTV_DF_RGBX		(1 <<  6)
#define LTV_DF_XRGB		(2 <<  6)
#define LTV_RGB_RGB		(0 <<  2)
#define LTV_RGB_BGR		(1 <<  2)
#define LTV_RGB_GRB		(2 <<  2)
#define LTV_RGB_RBG		(3 <<  2)

/* Bit definitions for LTV_ENTRY_MODE */
#define LTV_VSPL_ACTIVE_LOW	(0 << 15)
#define LTV_VSPL_ACTIVE_HIGH	(1 << 15)
#define LTV_HSPL_ACTIVE_LOW	(0 << 14)
#define LTV_HSPL_ACTIVE_HIGH	(1 << 14)
#define LTV_DPL_SAMPLE_RISING	(0 << 13)
#define LTV_DPL_SAMPLE_FALLING	(1 << 13)
#define LTV_EPL_ACTIVE_LOW	(0 << 12)
#define LTV_EPL_ACTIVE_HIGH	(1 << 12)
#define LTV_SS_LEFT_TO_RIGHT	(0 <<  8)
#define LTV_SS_RIGHT_TO_LEFT	(1 <<  8)
#define LTV_STB			(1 <<  1)

/* Bit definitions for LTV_GATECTL1 */
#define LTV_CLW(x)		(((x) & 0x0007) << 12)
#define LTV_GAON		(1 <<  5)
#define LTV_SDR			(1 <<  3)

/* Bit definitions for LTV_GATECTL2 */
#define LTV_NW_INV_FRAME	(0 << 14)
#define LTV_NW_INV_1LINE	(1 << 14)
#define LTV_NW_INV_2LINE	(2 << 14)
#define LTV_DSC			(1 << 12)
#define LTV_GIF			(1 <<  8)
#define LTV_FHN			(1 <<  7)
#define LTV_FTI(x)		(((x) & 0x0003) << 4)
#define LTV_FWI(x)		(((x) & 0x0003) << 0)

/* Bit definitions for LTV_SOTCTL */
#define LTV_SDT(x)		(((x) & 0x0007) << 10)
#define LTV_EQ(x)		(((x) & 0x0007) <<  2)

/* Bit definitions for LTV_PWRCTL1 */
#define LTV_VCOM_DISABLE	(1 << 14)
#define LTV_VCOMOUT_ENABLE	(1 << 11)
#define LTV_POWER_ON		(1 <<  9)
#define LTV_DRIVE_CURRENT(x)	(((x) & 0x0007) << 4)	/* 0=off, 5=max */
#define LTV_SUPPLY_CURRENT(x)	(((x) & 0x0007) << 0)	/* 0=off, 5=max */

/* Bit definitions for LTV_PWRCTL2 */
#define LTV_VCOML_ENABLE	(1 << 13)
#define LTV_VCOML_VOLTAGE(x)	(((x) & 0x001f) << 8)	/* 0=1V, 31=-1V */
#define LTV_VCOMH_VOLTAGE(x)	(((x) & 0x001f) << 0)	/* 0=3V, 31=4.5V */

#endif /* __LTV350QV_H */
