/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	include/asm-mips/dec/kn02ba.h
 *
 *	DECstation 5000/1xx (3min or KN02-BA) definitions.
 *
 *	Copyright (C) 2002, 2003  Maciej W. Rozycki
 */
#ifndef __ASM_MIPS_DEC_KN02BA_H
#define __ASM_MIPS_DEC_KN02BA_H

#include <asm/dec/kn02xa.h>		/* For common definitions. */

/*
 * CPU interrupt bits.
 */
#define KN02BA_CPU_INR_HALT	6	/* HALT button */
#define KN02BA_CPU_INR_CASCADE	5	/* I/O ASIC cascade */
#define KN02BA_CPU_INR_TC2	4	/* TURBOchannel slot #2 */
#define KN02BA_CPU_INR_TC1	3	/* TURBOchannel slot #1 */
#define KN02BA_CPU_INR_TC0	2	/* TURBOchannel slot #0 */

/*
 * I/O ASIC interrupt bits.  Star marks denote non-IRQ status bits.
 */
#define KN02BA_IO_INR_RES_15	15	/* unused */
#define KN02BA_IO_INR_NVRAM	14	/* (*) NVRAM clear jumper */
#define KN02BA_IO_INR_RES_13	13	/* unused */
#define KN02BA_IO_INR_BUS	12	/* memory, I/O bus read/write errors */
#define KN02BA_IO_INR_RES_11	11	/* unused */
#define KN02BA_IO_INR_NRMOD	10	/* (*) NRMOD manufacturing jumper */
#define KN02BA_IO_INR_ASC	9	/* ASC (NCR53C94) SCSI */
#define KN02BA_IO_INR_LANCE	8	/* LANCE (Am7990) Ethernet */
#define KN02BA_IO_INR_SCC1	7	/* SCC (Z85C30) serial #1 */
#define KN02BA_IO_INR_SCC0	6	/* SCC (Z85C30) serial #0 */
#define KN02BA_IO_INR_RTC	5	/* DS1287 RTC */
#define KN02BA_IO_INR_PSU	4	/* power supply unit warning */
#define KN02BA_IO_INR_RES_3	3	/* unused */
#define KN02BA_IO_INR_ASC_DATA	2	/* SCSI data ready (for PIO) */
#define KN02BA_IO_INR_PBNC	1	/* ~HALT button debouncer */
#define KN02BA_IO_INR_PBNO	0	/* HALT button debouncer */


/*
 * Memory Error Register bits.
 */
#define KN02BA_MER_RES_27	(1<<27)		/* unused */

/*
 * Memory Size Register bits.
 */
#define KN02BA_MSR_RES_17	(0x3ff<<17)	/* unused */

/*
 * I/O ASIC System Support Register bits.
 */
#define KN02BA_IO_SSR_TXDIS1	(1<<14)		/* SCC1 transmit disable */
#define KN02BA_IO_SSR_TXDIS0	(1<<13)		/* SCC0 transmit disable */
#define KN02BA_IO_SSR_RES_12	(1<<12)		/* unused */

#define KN02BA_IO_SSR_LEDS	(0xff<<0)	/* ~diagnostic LEDs */

#endif /* __ASM_MIPS_DEC_KN02BA_H */
