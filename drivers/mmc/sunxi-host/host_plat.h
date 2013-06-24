/*
 * drivers/mmc/sunxi-host/host_plat.h
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Aaron.Maoye <leafy.myeh@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef HOST_PLAT_H
#define HOST_PLAT_H

#include <mach/irqs.h>

#define DRIVER_NAME "sunxi-mmc"
#define DRIVER_RIVISION "V2.0"
#define DRIVER_VERSION "SUNXI MMC Controller, Version: " DRIVER_RIVISION "(Compiled in " __DATE__ " at " __TIME__ ")"

/* SDMMC Control registers definition */
#define  SMC0_BASE              0x01C0f000
#define  SMC1_BASE              0x01C10000
#define  SMC2_BASE              0x01C11000
#define  SMC3_BASE              0x01C12000

#define SMC_BASE_OS             (0x1000)
#define SMC_BASE(x)             (SMC0_BASE + 0x1000 * (x))

/* interrupt number */
#define  INTC_IRQNO_SMC0	SW_INT_IRQNO_SDMC0
#define  INTC_IRQNO_SMC1	SW_INT_IRQNO_SDMC1
#define  INTC_IRQNO_SMC2	SW_INT_IRQNO_SDMC2
#define  INTC_IRQNO_SMC3	SW_INT_IRQNO_SDMC3

#define  SUNXI_MMC_USED_MASK       (0xf)
#define  SUNXI_MMC0_USED           (0x1 << 0)
#define  SUNXI_MMC1_USED           (0x1 << 1)
#define  SUNXI_MMC2_USED           (0x1 << 2)
#define  SUNXI_MMC3_USED           (0x1 << 3)

#if defined(CONFIG_ARCH_SUN4I)
#define  SUNXI_MMC_HOST_NUM        4
#define  SUNXI_MMC_USED_CTRL       (SUNXI_MMC0_USED | SUNXI_MMC1_USED | SUNXI_MMC2_USED | SUNXI_MMC3_USED)
#define  SUNXI_MMC_MAX_DMA_DES_BIT  13
#define  SUNXI_MMC_DMA_DES_BIT_LEFT 6

enum mclk_src {
	SMC_MCLK_SRC_HOSC,
	SMC_MCLK_SRC_SATAPLL,
	SMC_MCLK_SRC_DRAMPLL
};
#define SMC_MAX_MOD_CLOCK(n)    ((n)==3 ? 90000000 : 90000000)
#define SMC_MAX_IO_CLOCK(n)     ((n)==3 ? 45000000 : 45000000)
#define SMC_MOD_CLK_SRC(n)      ((n)==3 ? SMC_MCLK_SRC_DRAMPLL : SMC_MCLK_SRC_DRAMPLL)

#elif defined(CONFIG_ARCH_SUN5I)
#define  SUNXI_MMC_HOST_NUM     3
#define  SUNXI_MMC_USED_CTRL    (SUNXI_MMC0_USED | SUNXI_MMC1_USED | SUNXI_MMC2_USED)
#define  SUNXI_MMC_MAX_DMA_DES_BIT  16
#define  SUNXI_MMC_DMA_DES_BIT_LEFT 0
enum mclk_src {
	SMC_MCLK_SRC_HOSC,
	SMC_MCLK_SRC_SATAPLL,
	SMC_MCLK_SRC_DRAMPLL
};
#define SMC_MAX_MOD_CLOCK(n)    (104000000)
#define SMC_MAX_IO_CLOCK(n)     (52000000)
#define SMC_MOD_CLK_SRC(n)      (SMC_MCLK_SRC_SATAPLL)

#elif defined(CONFIG_ARCH_SUN7I)
#define  SUNXI_MMC_HOST_NUM     4
#define  SUNXI_MMC_USED_CTRL    (SUNXI_MMC0_USED | SUNXI_MMC1_USED | SUNXI_MMC2_USED | SUNXI_MMC3_USED)
#define  SUNXI_MMC_MAX_DMA_DES_BIT  16
#define  SUNXI_MMC_DMA_DES_BIT_LEFT 0
enum mclk_src {
	SMC_MCLK_SRC_HOSC,
	SMC_MCLK_SRC_SATAPLL,
	SMC_MCLK_SRC_DRAMPLL
};
#define SMC_MAX_MOD_CLOCK(n)    (104000000)
#define SMC_MAX_IO_CLOCK(n)     (52000000)
#define SMC_MOD_CLK_SRC(n)      (SMC_MCLK_SRC_DRAMPLL)

#endif

#define SMC_DBG_ERR     (1 << 0)
#define SMC_DBG_MSG     (1 << 1)
#define SMC_DBG_INFO	(1 << 2)

#define SMC_MSG(...)    do { printk("[mmc]: "__VA_ARGS__); } while(0)
#define SMC_ERR(...)    do { printk("[mmc]: %s(L%d): ", __FUNCTION__, __LINE__); printk(__VA_ARGS__);} while(0)

#ifdef CONFIG_MMC_SUNXI_DBG
#define SMC_INFO(...)   do {if (smc_debug) SMC_MSG(__VA_ARGS__); } while(0)
#define SMC_DBG(...)    do {if (smc_debug) SMC_MSG(__VA_ARGS__); } while(0)
#else  //#ifdef CONFIG_MMC_SUNXI_DBG
#define SMC_INFO(...)
#define SMC_DBG(...)
#endif  //#ifdef CONFIG_MMC_SUNXI_DBG

#endif
