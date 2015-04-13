/*
 * Exynos Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 * Copyright (C) 2012-2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _DW_MMC_EXYNOS_H_
#define _DW_MMC_EXYNOS_H_

/* Extended Register's Offset */
#define SDMMC_CLKSEL			0x09C
#define SDMMC_CLKSEL64			0x0A8

/* CLKSEL register defines */
#define SDMMC_CLKSEL_CCLK_SAMPLE(x)	(((x) & 7) << 0)
#define SDMMC_CLKSEL_CCLK_DRIVE(x)	(((x) & 7) << 16)
#define SDMMC_CLKSEL_CCLK_DIVIDER(x)	(((x) & 7) << 24)
#define SDMMC_CLKSEL_GET_DRV_WD3(x)	(((x) >> 16) & 0x7)
#define SDMMC_CLKSEL_TIMING(x, y, z)	(SDMMC_CLKSEL_CCLK_SAMPLE(x) |	\
					 SDMMC_CLKSEL_CCLK_DRIVE(y) |	\
					 SDMMC_CLKSEL_CCLK_DIVIDER(z))
#define SDMMC_CLKSEL_WAKEUP_INT		BIT(11)

/* Protector Register */
#define SDMMC_EMMCP_BASE	0x1000
#define SDMMC_MPSECURITY	(SDMMC_EMMCP_BASE + 0x0010)
#define SDMMC_MPSBEGIN0		(SDMMC_EMMCP_BASE + 0x0200)
#define SDMMC_MPSEND0		(SDMMC_EMMCP_BASE + 0x0204)
#define SDMMC_MPSCTRL0		(SDMMC_EMMCP_BASE + 0x020C)

/* SMU control defines */
#define SDMMC_MPSCTRL_SECURE_READ_BIT		BIT(7)
#define SDMMC_MPSCTRL_SECURE_WRITE_BIT		BIT(6)
#define SDMMC_MPSCTRL_NON_SECURE_READ_BIT	BIT(5)
#define SDMMC_MPSCTRL_NON_SECURE_WRITE_BIT	BIT(4)
#define SDMMC_MPSCTRL_USE_FUSE_KEY		BIT(3)
#define SDMMC_MPSCTRL_ECB_MODE			BIT(2)
#define SDMMC_MPSCTRL_ENCRYPTION		BIT(1)
#define SDMMC_MPSCTRL_VALID			BIT(0)

/* Maximum number of Ending sector */
#define SDMMC_ENDING_SEC_NR_MAX	0xFFFFFFFF

/* Fixed clock divider */
#define EXYNOS4210_FIXED_CIU_CLK_DIV	2
#define EXYNOS4412_FIXED_CIU_CLK_DIV	4

/* Minimal required clock frequency for cclkin, unit: HZ */
#define EXYNOS_CCLKIN_MIN	50000000

#endif /* _DW_MMC_EXYNOS_H_ */
