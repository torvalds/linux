/*
 * Hardware info about DECstation DS2100/3100 systems (otherwise known as
 * pmin/pmax or KN01).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995,1996 by Paul M. Antoine, some code and definitions
 * are by courtesy of Chris Fraser.
 * Copyright (C) 2002, 2003, 2005  Maciej W. Rozycki
 */
#ifndef __ASM_MIPS_DEC_KN01_H
#define __ASM_MIPS_DEC_KN01_H

#define KN01_SLOT_BASE	0x10000000
#define KN01_SLOT_SIZE	0x01000000

/*
 * Address ranges for devices.
 */
#define KN01_PMASK	(0*KN01_SLOT_SIZE)	/* color plane mask */
#define KN01_PCC	(1*KN01_SLOT_SIZE)	/* PCC (DC503) cursor */
#define KN01_VDAC	(2*KN01_SLOT_SIZE)	/* color map */
#define KN01_RES_3	(3*KN01_SLOT_SIZE)	/* unused */
#define KN01_RES_4	(4*KN01_SLOT_SIZE)	/* unused */
#define KN01_RES_5	(5*KN01_SLOT_SIZE)	/* unused */
#define KN01_RES_6	(6*KN01_SLOT_SIZE)	/* unused */
#define KN01_ERRADDR	(7*KN01_SLOT_SIZE)	/* write error address */
#define KN01_LANCE	(8*KN01_SLOT_SIZE)	/* LANCE (Am7990) Ethernet */
#define KN01_LANCE_MEM	(9*KN01_SLOT_SIZE)	/* LANCE buffer memory */
#define KN01_SII	(10*KN01_SLOT_SIZE)	/* SII (DC7061) SCSI */
#define KN01_SII_MEM	(11*KN01_SLOT_SIZE)	/* SII buffer memory */
#define KN01_DZ11	(12*KN01_SLOT_SIZE)	/* DZ11 (DC7085) serial */
#define KN01_RTC	(13*KN01_SLOT_SIZE)	/* DS1287 RTC (bytes #0) */
#define KN01_ESAR	(13*KN01_SLOT_SIZE)	/* MAC address (bytes #1) */
#define KN01_CSR	(14*KN01_SLOT_SIZE)	/* system ctrl & status reg */
#define KN01_SYS_ROM	(15*KN01_SLOT_SIZE)	/* system board ROM */


/*
 * Frame buffer memory address.
 */
#define KN01_VFB_MEM	0x0fc00000

/*
 * CPU interrupt bits.
 */
#define KN01_CPU_INR_BUS	6	/* memory, I/O bus read/write errors */
#define KN01_CPU_INR_VIDEO	6	/* PCC area detect #2 */
#define KN01_CPU_INR_RTC	5	/* DS1287 RTC */
#define KN01_CPU_INR_DZ11	4	/* DZ11 (DC7085) serial */
#define KN01_CPU_INR_LANCE	3	/* LANCE (Am7990) Ethernet */
#define KN01_CPU_INR_SII	2	/* SII (DC7061) SCSI */


/*
 * System Control & Status Register bits.
 */
#define KN01_CSR_MNFMOD		(1<<15)	/* MNFMOD manufacturing jumper */
#define KN01_CSR_STATUS		(1<<14)	/* self-test result status output */
#define KN01_CSR_PARDIS		(1<<13)	/* parity error disable */
#define KN01_CSR_CRSRTST	(1<<12)	/* PCC test output */
#define KN01_CSR_MONO		(1<<11)	/* mono/color fb SIMM installed */
#define KN01_CSR_MEMERR		(1<<10)	/* write timeout error status & ack*/
#define KN01_CSR_VINT		(1<<9)	/* PCC area detect #2 status & ack */
#define KN01_CSR_TXDIS		(1<<8)	/* DZ11 transmit disable */
#define KN01_CSR_VBGTRG		(1<<2)	/* blue DAC voltage over green (r/o) */
#define KN01_CSR_VRGTRG		(1<<1)	/* red DAC voltage over green (r/o) */
#define KN01_CSR_VRGTRB		(1<<0)	/* red DAC voltage over blue (r/o) */
#define KN01_CSR_LEDS		(0xff<<0) /* ~diagnostic LEDs (w/o) */


#ifndef __ASSEMBLY__

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct pt_regs;

extern u16 cached_kn01_csr;

extern void dec_kn01_be_init(void);
extern int dec_kn01_be_handler(struct pt_regs *regs, int is_fixup);
extern irqreturn_t dec_kn01_be_interrupt(int irq, void *dev_id);
#endif

#endif /* __ASM_MIPS_DEC_KN01_H */
