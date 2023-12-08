/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Exynos Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 * Copyright (C) 2012-2014 Samsung Electronics Co., Ltd.
 */

#ifndef _DW_MMC_EXYNOS_H_
#define _DW_MMC_EXYNOS_H_

#define SDMMC_CLKSEL			0x09C
#define SDMMC_CLKSEL64			0x0A8

/* Extended Register's Offset */
#define SDMMC_HS400_DQS_EN		0x180
#define SDMMC_HS400_ASYNC_FIFO_CTRL	0x184
#define SDMMC_HS400_DLINE_CTRL		0x188

/* CLKSEL register defines */
#define SDMMC_CLKSEL_CCLK_SAMPLE(x)	(((x) & 7) << 0)
#define SDMMC_CLKSEL_CCLK_DRIVE(x)	(((x) & 7) << 16)
#define SDMMC_CLKSEL_CCLK_DIVIDER(x)	(((x) & 7) << 24)
#define SDMMC_CLKSEL_GET_DRV_WD3(x)	(((x) >> 16) & 0x7)
#define SDMMC_CLKSEL_GET_DIV(x)		(((x) >> 24) & 0x7)
#define SDMMC_CLKSEL_UP_SAMPLE(x, y)	(((x) & ~SDMMC_CLKSEL_CCLK_SAMPLE(7)) |\
					 SDMMC_CLKSEL_CCLK_SAMPLE(y))
#define SDMMC_CLKSEL_TIMING(x, y, z)	(SDMMC_CLKSEL_CCLK_SAMPLE(x) |	\
					 SDMMC_CLKSEL_CCLK_DRIVE(y) |	\
					 SDMMC_CLKSEL_CCLK_DIVIDER(z))
#define SDMMC_CLKSEL_TIMING_MASK	SDMMC_CLKSEL_TIMING(0x7, 0x7, 0x7)
#define SDMMC_CLKSEL_WAKEUP_INT		BIT(11)

/* RCLK_EN register defines */
#define DATA_STROBE_EN			BIT(0)
#define AXI_NON_BLOCKING_WR	BIT(7)

/* DLINE_CTRL register defines */
#define DQS_CTRL_RD_DELAY(x, y)		(((x) & ~0x3FF) | ((y) & 0x3FF))
#define DQS_CTRL_GET_RD_DELAY(x)	((x) & 0x3FF)

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
#define HS400_FIXED_CIU_CLK_DIV		1

/* Minimal required clock frequency for cclkin, unit: HZ */
#define EXYNOS_CCLKIN_MIN	50000000

#endif /* _DW_MMC_EXYNOS_H_ */
