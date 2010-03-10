/*
 * arch/arm/mach-w90x900/include/mach/regs-serial.h
 *
 * Copyright (c) 2009 Nuvoton technology corporation
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  Description:
 *     Nuvoton Display, LCM Register list
 *  Author:  Wang Qiang (rurality.linux@gmail.com) 2009/12/11
 *
 */


#ifndef __ASM_ARM_W90X900_REGS_LDM_H
#define __ASM_ARM_W90X900_REGS_LDM_H

#include <mach/map.h>

/* Display Controller Control/Status Register */
#define REG_LCM_DCCS			(0x00)

#define LCM_DCCS_ENG_RST		(1 << 0)
#define LCM_DCCS_VA_EN			(1 << 1)
#define LCM_DCCS_OSD_EN			(1 << 2)
#define LCM_DCCS_DISP_OUT_EN		(1 << 3)
#define LCM_DCCS_DISP_INT_EN		(1 << 4)
#define LCM_DCCS_CMD_ON			(1 << 5)
#define LCM_DCCS_FIELD_INTR		(1 << 6)
#define LCM_DCCS_SINGLE			(1 << 7)

enum LCM_DCCS_VA_SRC {
	LCM_DCCS_VA_SRC_YUV422		= (0 << 8),
	LCM_DCCS_VA_SRC_YCBCR422	= (1 << 8),
	LCM_DCCS_VA_SRC_RGB888		= (2 << 8),
	LCM_DCCS_VA_SRC_RGB666		= (3 << 8),
	LCM_DCCS_VA_SRC_RGB565		= (4 << 8),
	LCM_DCCS_VA_SRC_RGB444LOW	= (5 << 8),
	LCM_DCCS_VA_SRC_RGB444HIGH 	= (7 << 8)
};


/* Display Device Control Register */
#define REG_LCM_DEV_CTRL		(0x04)

enum LCM_DEV_CTRL_SWAP_YCbCr {
	LCM_DEV_CTRL_SWAP_UYVY		= (0 << 1),
	LCM_DEV_CTRL_SWAP_YUYV		= (1 << 1),
	LCM_DEV_CTRL_SWAP_VYUY		= (2 << 1),
	LCM_DEV_CTRL_SWAP_YVYU		= (3 << 1)
};

enum LCM_DEV_CTRL_RGB_SHIFT {
	LCM_DEV_CTRL_RGB_SHIFT_NOT 	= (0 << 3),
	LCM_DEV_CTRL_RGB_SHIFT_ONECYCLE = (1 << 3),
	LCM_DEV_CTRL_RGB_SHIFT_TWOCYCLE = (2 << 3),
	LCM_DEV_CTRL_RGB_SHIFT_NOT_DEF	= (3 << 3)
};

enum LCM_DEV_CTRL_DEVICE {
	LCM_DEV_CTRL_DEVICE_YUV422	= (0 << 5),
	LCM_DEV_CTRL_DEVICE_YUV444	= (1 << 5),
	LCM_DEV_CTRL_DEVICE_UNIPAC	= (4 << 5),
	LCM_DEV_CTRL_DEVICE_SEIKO_EPSON	= (5 << 5),
	LCM_DEV_CTRL_DEVICE_HIGH_COLOR	= (6 << 5),
	LCM_DEV_CTRL_DEVICE_MPU		= (7 << 5)
};

#define LCM_DEV_CTRL_LCD_DDA		(8)
#define LCM_DEV_CTRL_YUV2CCIR		(16)

enum LCM_DEV_CTRL_LCD_SEL {
	LCM_DEV_CTRL_LCD_SEL_RGB_GBR	= (0 << 17),
	LCM_DEV_CTRL_LCD_SEL_BGR_RBG	= (1 << 17),
	LCM_DEV_CTRL_LCD_SEL_GBR_RGB	= (2 << 17),
	LCM_DEV_CTRL_LCD_SEL_RBG_BGR	= (3 << 17)
};

enum LCM_DEV_CTRL_FAL_D {
	LCM_DEV_CTRL_FAL_D_FALLING	= (0 << 19),
	LCM_DEV_CTRL_FAL_D_RISING	= (1 << 19),
};

enum LCM_DEV_CTRL_H_POL {
	LCM_DEV_CTRL_H_POL_LOW		= (0 << 20),
	LCM_DEV_CTRL_H_POL_HIGH		= (1 << 20),
};

enum LCM_DEV_CTRL_V_POL {
	LCM_DEV_CTRL_V_POL_LOW		= (0 << 21),
	LCM_DEV_CTRL_V_POL_HIGH		= (1 << 21),
};

enum LCM_DEV_CTRL_VR_LACE {
	LCM_DEV_CTRL_VR_LACE_NINTERLACE	= (0 << 22),
	LCM_DEV_CTRL_VR_LACE_INTERLACE	= (1 << 22),
};

enum LCM_DEV_CTRL_LACE {
	LCM_DEV_CTRL_LACE_NINTERLACE	= (0 << 23),
	LCM_DEV_CTRL_LACE_INTERLACE	= (1 << 23),
};

enum LCM_DEV_CTRL_RGB_SCALE {
	LCM_DEV_CTRL_RGB_SCALE_4096 	= (0 << 24),
	LCM_DEV_CTRL_RGB_SCALE_65536 	= (1 << 24),
	LCM_DEV_CTRL_RGB_SCALE_262144 	= (2 << 24),
	LCM_DEV_CTRL_RGB_SCALE_16777216 = (3 << 24),
};

enum LCM_DEV_CTRL_DBWORD {
	LCM_DEV_CTRL_DBWORD_HALFWORD	= (0 << 26),
	LCM_DEV_CTRL_DBWORD_FULLWORD	= (1 << 26),
};

enum LCM_DEV_CTRL_MPU68 {
	LCM_DEV_CTRL_MPU68_80_SERIES	= (0 << 27),
	LCM_DEV_CTRL_MPU68_68_SERIES	= (1 << 27),
};

enum LCM_DEV_CTRL_DE_POL {
	LCM_DEV_CTRL_DE_POL_HIGH	= (0 << 28),
	LCM_DEV_CTRL_DE_POL_LOW		= (1 << 28),
};

#define LCM_DEV_CTRL_CMD16		(29)
#define LCM_DEV_CTRL_CM16t18		(30)
#define LCM_DEV_CTRL_CMD_LOW		(31)

/* MPU-Interface LCD Write Command */
#define REG_LCM_MPU_CMD			(0x08)

/* Interrupt Control/Status Register */
#define REG_LCM_INT_CS			(0x0c)
#define LCM_INT_CS_DISP_F_EN		(1 << 0)
#define LCM_INT_CS_UNDERRUN_EN   	(1 << 1)
#define LCM_INT_CS_BUS_ERROR_INT 	(1 << 28)
#define LCM_INT_CS_UNDERRUN_INT  	(1 << 29)
#define LCM_INT_CS_DISP_F_STATUS 	(1 << 30)
#define LCM_INT_CS_DISP_F_INT		(1 << 31)

/* CRTC Display Size Control Register */
#define REG_LCM_CRTC_SIZE		(0x10)
#define LCM_CRTC_SIZE_VTTVAL(x)		((x) << 16)
#define LCM_CRTC_SIZE_HTTVAL(x)		((x) << 0)

/* CRTC Display Enable End */
#define REG_LCM_CRTC_DEND		(0x14)
#define LCM_CRTC_DEND_VDENDVAL(x)	((x) << 16)
#define LCM_CRTC_DEND_HDENDVAL(x)	((x) << 0)

/* CRTC Internal Horizontal Retrace Control Register */
#define REG_LCM_CRTC_HR			(0x18)
#define LCM_CRTC_HR_EVAL(x)		((x) << 16)
#define LCM_CRTC_HR_SVAL(x)		((x) << 0)

/* CRTC Horizontal Sync Control Register */
#define REG_LCM_CRTC_HSYNC		(0x1C)
#define LCM_CRTC_HSYNC_SHIFTVAL(x)	((x) << 30)
#define LCM_CRTC_HSYNC_EVAL(x)		((x) << 16)
#define LCM_CRTC_HSYNC_SVAL(x)		((x) << 0)

/* CRTC Internal Vertical Retrace Control Register */
#define REG_LCM_CRTC_VR			(0x20)
#define LCM_CRTC_VR_EVAL(x)		((x) << 16)
#define LCM_CRTC_VR_SVAL(x)		((x) << 0)

/* Video Stream Frame Buffer-0 Starting Address */
#define REG_LCM_VA_BADDR0		(0x24)

/* Video Stream Frame Buffer-1 Starting Address */
#define REG_LCM_VA_BADDR1		(0x28)

/* Video Stream Frame Buffer Control Register */
#define REG_LCM_VA_FBCTRL		(0x2C)
#define LCM_VA_FBCTRL_IO_REGION_HALF	(1 << 28)
#define LCM_VA_FBCTRL_FIELD_DUAL  	(1 << 29)
#define LCM_VA_FBCTRL_START_BUF 	(1 << 30)
#define LCM_VA_FBCTRL_DB_EN		(1 << 31)

/* Video Stream Scaling Control Register */
#define REG_LCM_VA_SCALE		(0x30)
#define LCM_VA_SCALE_XCOPY_INTERPOLATION (0 << 15)
#define LCM_VA_SCALE_XCOPY_DUPLICATION	 (1 << 15)

/* Image Stream Active Window Coordinates */
#define REG_LCM_VA_WIN			(0x38)

/* Image Stream Stuff Pixel */
#define REG_LCM_VA_STUFF		(0x3C)

/* OSD Window Starting Coordinates */
#define REG_LCM_OSD_WINS		(0x40)

/* OSD Window Ending Coordinates */
#define REG_LCM_OSD_WINE		(0x44)

/* OSD Stream Frame Buffer Starting Address */
#define REG_LCM_OSD_BADDR		(0x48)

/* OSD Stream Frame Buffer Control Register */
#define REG_LCM_OSD_FBCTRL		(0x4c)

/* OSD Overlay Control Register */
#define REG_LCM_OSD_OVERLAY		(0x50)

/* OSD Overlay Color-Key Pattern Register */
#define REG_LCM_OSD_CKEY		(0x54)

/* OSD Overlay Color-Key Mask Register */
#define REG_LCM_OSD_CMASK		(0x58)

/* OSD Window Skip1 Register */
#define REG_LCM_OSD_SKIP1		(0x5C)

/* OSD Window Skip2 Register */
#define REG_LCM_OSD_SKIP2		(0x60)

/* OSD horizontal up scaling control register */
#define REG_LCM_OSD_SCALE		(0x64)

/* MPU Vsync control register */
#define REG_LCM_MPU_VSYNC		(0x68)

/* Hardware cursor control Register */
#define REG_LCM_HC_CTRL			(0x6C)

/* Hardware cursot tip point potison on va picture */
#define REG_LCM_HC_POS			(0x70)

/* Hardware Cursor Window Buffer Control Register */
#define REG_LCM_HC_WBCTRL		(0x74)

/* Hardware cursor memory base address register */
#define REG_LCM_HC_BADDR		(0x78)

/* Hardware cursor color ram register mapped to bpp = 0 */
#define REG_LCM_HC_COLOR0		(0x7C)

/* Hardware cursor color ram register mapped to bpp = 1 */
#define REG_LCM_HC_COLOR1		(0x80)

/* Hardware cursor color ram register mapped to bpp = 2 */
#define REG_LCM_HC_COLOR2		(0x84)

/* Hardware cursor color ram register mapped to bpp = 3 */
#define REG_LCM_HC_COLOR3		(0x88)

#endif /* __ASM_ARM_W90X900_REGS_LDM_H */
