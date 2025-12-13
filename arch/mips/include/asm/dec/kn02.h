/*
 * Hardware info about DECstation 5000/200 systems (otherwise known as
 * 3max or KN02).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995,1996 by Paul M. Antoine, some code and definitions
 * are by courtesy of Chris Fraser.
 * Copyright (C) 2002, 2003, 2005  Maciej W. Rozycki
 */
#ifndef __ASM_MIPS_DEC_KN02_H
#define __ASM_MIPS_DEC_KN02_H

#define KN02_SLOT_BASE	0x1fc00000
#define KN02_SLOT_SIZE	0x00080000

/*
 * Address ranges decoded by the "system slot" logic for onboard devices.
 */
#define KN02_SYS_ROM	(0*KN02_SLOT_SIZE)	/* system board ROM */
#define KN02_RES_1	(1*KN02_SLOT_SIZE)	/* unused */
#define KN02_CHKSYN	(2*KN02_SLOT_SIZE)	/* ECC syndrome */
#define KN02_ERRADDR	(3*KN02_SLOT_SIZE)	/* bus error address */
#define KN02_DZ11	(4*KN02_SLOT_SIZE)	/* DZ11 (DC7085) serial */
#define KN02_RTC	(5*KN02_SLOT_SIZE)	/* DS1287 RTC */
#define KN02_CSR	(6*KN02_SLOT_SIZE)	/* system ctrl & status reg */
#define KN02_SYS_ROM_7	(7*KN02_SLOT_SIZE)	/* system board ROM (alias) */


/*
 * System Control & Status Register bits.
 */
#define KN02_CSR_RES_28		(0xf<<28)	/* unused */
#define KN02_CSR_PSU		(1<<27)		/* power supply unit warning */
#define KN02_CSR_NVRAM		(1<<26)		/* ~NVRAM clear jumper */
#define KN02_CSR_REFEVEN	(1<<25)		/* mem refresh bank toggle */
#define KN02_CSR_NRMOD		(1<<24)		/* ~NRMOD manufact. jumper */
#define KN02_CSR_IOINTEN	(0xff<<16)	/* IRQ mask bits */
#define KN02_CSR_DIAGCHK	(1<<15)		/* diagn/norml ECC reads */
#define KN02_CSR_DIAGGEN	(1<<14)		/* diagn/norml ECC writes */
#define KN02_CSR_CORRECT	(1<<13)		/* ECC correct/check */
#define KN02_CSR_LEDIAG		(1<<12)		/* ECC diagn. latch strobe */
#define KN02_CSR_TXDIS		(1<<11)		/* DZ11 transmit disable */
#define KN02_CSR_BNK32M		(1<<10)		/* 32M/8M stride */
#define KN02_CSR_DIAGDN		(1<<9)		/* DIAGDN manufact. jumper */
#define KN02_CSR_BAUD38		(1<<8)		/* DZ11 38/19kbps ext. rate */
#define KN02_CSR_IOINT		(0xff<<0)	/* IRQ status bits (r/o) */
#define KN02_CSR_LEDS		(0xff<<0)	/* ~diagnostic LEDs (w/o) */


/*
 * CPU interrupt bits.
 */
#define KN02_CPU_INR_RES_6	6	/* unused */
#define KN02_CPU_INR_BUS	5	/* memory, I/O bus read/write errors */
#define KN02_CPU_INR_RES_4	4	/* unused */
#define KN02_CPU_INR_RTC	3	/* DS1287 RTC */
#define KN02_CPU_INR_CASCADE	2	/* CSR cascade */

/*
 * CSR interrupt bits.
 */
#define KN02_CSR_INR_DZ11	7	/* DZ11 (DC7085) serial */
#define KN02_CSR_INR_LANCE	6	/* LANCE (Am7990) Ethernet */
#define KN02_CSR_INR_ASC	5	/* ASC (NCR53C94) SCSI */
#define KN02_CSR_INR_RES_4	4	/* unused */
#define KN02_CSR_INR_RES_3	3	/* unused */
#define KN02_CSR_INR_TC2	2	/* TURBOchannel slot #2 */
#define KN02_CSR_INR_TC1	1	/* TURBOchannel slot #1 */
#define KN02_CSR_INR_TC0	0	/* TURBOchannel slot #0 */


#define KN02_IRQ_BASE		8	/* first IRQ assigned to CSR */
#define KN02_IRQ_LINES		8	/* number of CSR interrupts */

#define KN02_IRQ_NR(n)		((n) + KN02_IRQ_BASE)
#define KN02_IRQ_MASK(n)	(1 << (n))
#define KN02_IRQ_ALL		0xff


#ifndef __ASSEMBLER__

#include <linux/types.h>

extern u32 cached_kn02_csr;
extern void init_kn02_irqs(int base);
#endif

#endif /* __ASM_MIPS_DEC_KN02_H */
