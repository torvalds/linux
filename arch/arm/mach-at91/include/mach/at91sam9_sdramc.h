/*
 * arch/arm/mach-at91/include/mach/at91sam9_sdramc.h
 *
 * SDRAM Controllers (SDRAMC) - System peripherals registers.
 * Based on AT91SAM9261 datasheet revision D.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91SAM9_SDRAMC_H
#define AT91SAM9_SDRAMC_H

/* SDRAM Controller (SDRAMC) registers */
#define AT91_SDRAMC_MR		(AT91_SDRAMC + 0x00)	/* SDRAM Controller Mode Register */
#define		AT91_SDRAMC_MODE	(0xf << 0)		/* Command Mode */
#define			AT91_SDRAMC_MODE_NORMAL		0
#define			AT91_SDRAMC_MODE_NOP		1
#define			AT91_SDRAMC_MODE_PRECHARGE	2
#define			AT91_SDRAMC_MODE_LMR		3
#define			AT91_SDRAMC_MODE_REFRESH	4
#define			AT91_SDRAMC_MODE_EXT_LMR	5
#define			AT91_SDRAMC_MODE_DEEP		6

#define AT91_SDRAMC_TR		(AT91_SDRAMC + 0x04)	/* SDRAM Controller Refresh Timer Register */
#define		AT91_SDRAMC_COUNT	(0xfff << 0)		/* Refresh Timer Counter */

#define AT91_SDRAMC_CR		(AT91_SDRAMC + 0x08)	/* SDRAM Controller Configuration Register */
#define		AT91_SDRAMC_NC		(3 << 0)		/* Number of Column Bits */
#define			AT91_SDRAMC_NC_8	(0 << 0)
#define			AT91_SDRAMC_NC_9	(1 << 0)
#define			AT91_SDRAMC_NC_10	(2 << 0)
#define			AT91_SDRAMC_NC_11	(3 << 0)
#define		AT91_SDRAMC_NR		(3 << 2)		/* Number of Row Bits */
#define			AT91_SDRAMC_NR_11	(0 << 2)
#define			AT91_SDRAMC_NR_12	(1 << 2)
#define			AT91_SDRAMC_NR_13	(2 << 2)
#define		AT91_SDRAMC_NB		(1 << 4)		/* Number of Banks */
#define			AT91_SDRAMC_NB_2	(0 << 4)
#define			AT91_SDRAMC_NB_4	(1 << 4)
#define		AT91_SDRAMC_CAS		(3 << 5)		/* CAS Latency */
#define			AT91_SDRAMC_CAS_1	(1 << 5)
#define			AT91_SDRAMC_CAS_2	(2 << 5)
#define			AT91_SDRAMC_CAS_3	(3 << 5)
#define		AT91_SDRAMC_DBW		(1 << 7)		/* Data Bus Width */
#define			AT91_SDRAMC_DBW_32	(0 << 7)
#define			AT91_SDRAMC_DBW_16	(1 << 7)
#define		AT91_SDRAMC_TWR		(0xf <<  8)		/* Write Recovery Delay */
#define		AT91_SDRAMC_TRC		(0xf << 12)		/* Row Cycle Delay */
#define		AT91_SDRAMC_TRP		(0xf << 16)		/* Row Precharge Delay */
#define		AT91_SDRAMC_TRCD	(0xf << 20)		/* Row to Column Delay */
#define		AT91_SDRAMC_TRAS	(0xf << 24)		/* Active to Precharge Delay */
#define		AT91_SDRAMC_TXSR	(0xf << 28)		/* Exit Self Refresh to Active Delay */

#define AT91_SDRAMC_LPR		(AT91_SDRAMC + 0x10)	/* SDRAM Controller Low Power Register */
#define		AT91_SDRAMC_LPCB		(3 << 0)	/* Low-power Configurations */
#define			AT91_SDRAMC_LPCB_DISABLE		0
#define			AT91_SDRAMC_LPCB_SELF_REFRESH		1
#define			AT91_SDRAMC_LPCB_POWER_DOWN		2
#define			AT91_SDRAMC_LPCB_DEEP_POWER_DOWN	3
#define		AT91_SDRAMC_PASR		(7 << 4)	/* Partial Array Self Refresh */
#define		AT91_SDRAMC_TCSR		(3 << 8)	/* Temperature Compensated Self Refresh */
#define		AT91_SDRAMC_DS			(3 << 10)	/* Drive Strength */
#define		AT91_SDRAMC_TIMEOUT		(3 << 12)	/* Time to define when Low Power Mode is enabled */
#define			AT91_SDRAMC_TIMEOUT_0_CLK_CYCLES	(0 << 12)
#define			AT91_SDRAMC_TIMEOUT_64_CLK_CYCLES	(1 << 12)
#define			AT91_SDRAMC_TIMEOUT_128_CLK_CYCLES	(2 << 12)

#define AT91_SDRAMC_IER		(AT91_SDRAMC + 0x14)	/* SDRAM Controller Interrupt Enable Register */
#define AT91_SDRAMC_IDR		(AT91_SDRAMC + 0x18)	/* SDRAM Controller Interrupt Disable Register */
#define AT91_SDRAMC_IMR		(AT91_SDRAMC + 0x1C)	/* SDRAM Controller Interrupt Mask Register */
#define AT91_SDRAMC_ISR		(AT91_SDRAMC + 0x20)	/* SDRAM Controller Interrupt Status Register */
#define		AT91_SDRAMC_RES		(1 << 0)		/* Refresh Error Status */

#define AT91_SDRAMC_MDR		(AT91_SDRAMC + 0x24)	/* SDRAM Memory Device Register */
#define		AT91_SDRAMC_MD		(3 << 0)		/* Memory Device Type */
#define			AT91_SDRAMC_MD_SDRAM		0
#define			AT91_SDRAMC_MD_LOW_POWER_SDRAM	1


#endif
