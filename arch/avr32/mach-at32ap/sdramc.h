/*
 * Register definitions for the AT32AP SDRAM Controller
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

/* Register offsets */
#define SDRAMC_MR			0x0000
#define SDRAMC_TR			0x0004
#define SDRAMC_CR			0x0008
#define SDRAMC_HSR			0x000c
#define SDRAMC_LPR			0x0010
#define SDRAMC_IER			0x0014
#define SDRAMC_IDR			0x0018
#define SDRAMC_IMR			0x001c
#define SDRAMC_ISR			0x0020
#define SDRAMC_MDR			0x0024

/* MR - Mode Register */
#define SDRAMC_MR_MODE_NORMAL		(  0 <<  0)
#define SDRAMC_MR_MODE_NOP		(  1 <<  0)
#define SDRAMC_MR_MODE_BANKS_PRECHARGE	(  2 <<  0)
#define SDRAMC_MR_MODE_LOAD_MODE	(  3 <<  0)
#define SDRAMC_MR_MODE_AUTO_REFRESH	(  4 <<  0)
#define SDRAMC_MR_MODE_EXT_LOAD_MODE	(  5 <<  0)
#define SDRAMC_MR_MODE_POWER_DOWN	(  6 <<  0)

/* CR - Configuration Register */
#define SDRAMC_CR_NC_8_BITS		(  0 <<  0)
#define SDRAMC_CR_NC_9_BITS		(  1 <<  0)
#define SDRAMC_CR_NC_10_BITS		(  2 <<  0)
#define SDRAMC_CR_NC_11_BITS		(  3 <<  0)
#define SDRAMC_CR_NR_11_BITS		(  0 <<  2)
#define SDRAMC_CR_NR_12_BITS		(  1 <<  2)
#define SDRAMC_CR_NR_13_BITS		(  2 <<  2)
#define SDRAMC_CR_NB_2_BANKS		(  0 <<  4)
#define SDRAMC_CR_NB_4_BANKS		(  1 <<  4)
#define SDRAMC_CR_CAS(x)		((x) <<  5)
#define SDRAMC_CR_DBW_32_BITS		(  0 <<  7)
#define SDRAMC_CR_DBW_16_BITS		(  1 <<  7)
#define SDRAMC_CR_TWR(x)		((x) <<  8)
#define SDRAMC_CR_TRC(x)		((x) << 12)
#define SDRAMC_CR_TRP(x)		((x) << 16)
#define SDRAMC_CR_TRCD(x)		((x) << 20)
#define SDRAMC_CR_TRAS(x)		((x) << 24)
#define SDRAMC_CR_TXSR(x)		((x) << 28)

/* HSR - High Speed Register */
#define SDRAMC_HSR_DA			(  1 <<  0)

/* LPR - Low Power Register */
#define SDRAMC_LPR_LPCB_INHIBIT		(  0 <<  0)
#define SDRAMC_LPR_LPCB_SELF_RFR	(  1 <<  0)
#define SDRAMC_LPR_LPCB_PDOWN		(  2 <<  0)
#define SDRAMC_LPR_LPCB_DEEP_PDOWN	(  3 <<  0)
#define SDRAMC_LPR_PASR(x)		((x) <<  4)
#define SDRAMC_LPR_TCSR(x)		((x) <<  8)
#define SDRAMC_LPR_DS(x)		((x) << 10)
#define SDRAMC_LPR_TIMEOUT(x)		((x) << 12)

/* IER/IDR/IMR/ISR - Interrupt Enable/Disable/Mask/Status Register */
#define SDRAMC_ISR_RES			(  1 <<  0)

/* MDR - Memory Device Register */
#define SDRAMC_MDR_MD_SDRAM		(  0 <<  0)
#define SDRAMC_MDR_MD_LOW_PWR_SDRAM	(  1 <<  0)

/* Register access macros */
#define sdramc_readl(reg) \
	__raw_readl((void __iomem __force *)SDRAMC_BASE + SDRAMC_##reg)
#define sdramc_writel(reg, value) \
	__raw_writel(value, (void __iomem __force *)SDRAMC_BASE + SDRAMC_##reg)
