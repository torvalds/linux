/*
 * Chip-specific header file for the SAMA5D4 family
 *
 *  Copyright (C) 2013 Atmel Corporation,
 *                     Nicolas Ferre <nicolas.ferre@atmel.com>
 *
 * Common definitions.
 * Based on SAMA5D4 datasheet.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef SAMA5D4_H
#define SAMA5D4_H

/*
 * User Peripheral physical base addresses.
 */
#define SAMA5D4_BASE_USART3	0xfc00c000 /* (USART3 non-secure) Base Address */
#define SAMA5D4_BASE_PMC	0xf0018000 /* (PMC) Base Address */
#define SAMA5D4_BASE_MPDDRC	0xf0010000 /* (MPDDRC) Base Address */
#define SAMA5D4_BASE_PIOD	0xfc068000 /* (PIOD) Base Address */

/* Some other peripherals */
#define SAMA5D4_BASE_SYS2	SAMA5D4_BASE_PIOD

/*
 * Internal Memory.
 */
#define SAMA5D4_NS_SRAM_BASE     0x00210000      /* Internal SRAM base address Non-Secure */
#define SAMA5D4_NS_SRAM_SIZE     (64 * SZ_1K)   /* Internal SRAM size Non-Secure part (64Kb) */

#endif
