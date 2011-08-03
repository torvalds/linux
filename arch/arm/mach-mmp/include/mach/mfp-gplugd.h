/*
 * linux/arch/arm/mach-mmp/include/mach/mfp-gplugd.h
 *
 *   MFP definitions used in gplugD
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_MFP_GPLUGD_H
#define __MACH_MFP_GPLUGD_H

#include <plat/mfp.h>
#include <mach/mfp.h>

/* UART3 */
#define GPIO8_UART3_SOUT       MFP_CFG(GPIO8, AF2)
#define GPIO9_UART3_SIN        MFP_CFG(GPIO9, AF2)
#define GPI1O_UART3_CTS        MFP_CFG(GPIO10, AF2)
#define GPI11_UART3_RTS        MFP_CFG(GPIO11, AF2)

/* MMC2 */
#define	GPIO28_MMC2_CMD		MFP_CFG_DRV(GPIO28, AF6, FAST)
#define	GPIO29_MMC2_CLK		MFP_CFG_DRV(GPIO29, AF6, FAST)
#define	GPIO30_MMC2_DAT0	MFP_CFG_DRV(GPIO30, AF6, FAST)
#define	GPIO31_MMC2_DAT1	MFP_CFG_DRV(GPIO31, AF6, FAST)
#define	GPIO32_MMC2_DAT2	MFP_CFG_DRV(GPIO32, AF6, FAST)
#define	GPIO33_MMC2_DAT3	MFP_CFG_DRV(GPIO33, AF6, FAST)

/* I2S */
#undef GPIO114_I2S_FRM
#undef GPIO115_I2S_BCLK

#define GPIO114_I2S_FRM	        MFP_CFG_DRV(GPIO114, AF1, FAST)
#define GPIO115_I2S_BCLK        MFP_CFG_DRV(GPIO115, AF1, FAST)
#define GPIO116_I2S_TXD         MFP_CFG_DRV(GPIO116, AF1, FAST)

/* MMC4 */
#define GPIO125_MMC4_DAT3       MFP_CFG_DRV(GPIO125, AF7, FAST)
#define GPIO126_MMC4_DAT2       MFP_CFG_DRV(GPIO126, AF7, FAST)
#define GPIO127_MMC4_DAT1       MFP_CFG_DRV(GPIO127, AF7, FAST)
#define GPIO0_2_MMC4_DAT0       MFP_CFG_DRV(GPIO0_2, AF7, FAST)
#define GPIO1_2_MMC4_CMD        MFP_CFG_DRV(GPIO1_2, AF7, FAST)
#define GPIO2_2_MMC4_CLK        MFP_CFG_DRV(GPIO2_2, AF7, FAST)

/* OTG GPIO */
#define GPIO_USB_OTG_PEN        18
#define GPIO_USB_OIDIR          20

/* Other GPIOs are 35, 84, 85 */
#endif /* __MACH_MFP_GPLUGD_H */
