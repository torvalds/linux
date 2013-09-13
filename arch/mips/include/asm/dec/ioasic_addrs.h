/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Definitions for the address map in the JUNKIO Asic
 *
 * Created with Information from:
 *
 * "DEC 3000 300/400/500/600/700/800/900 AXP Models System Programmer's Manual"
 *
 * and the Mach Sources
 *
 * Copyright (C) 199x  the Anonymous
 * Copyright (C) 2002, 2003  Maciej W. Rozycki
 */

#ifndef __ASM_MIPS_DEC_IOASIC_ADDRS_H
#define __ASM_MIPS_DEC_IOASIC_ADDRS_H

#define IOASIC_SLOT_SIZE 0x00040000

/*
 * Address ranges decoded by the I/O ASIC for onboard devices.
 */
#define IOASIC_SYS_ROM	(0*IOASIC_SLOT_SIZE)	/* system board ROM */
#define IOASIC_IOCTL	(1*IOASIC_SLOT_SIZE)	/* I/O ASIC */
#define IOASIC_ESAR	(2*IOASIC_SLOT_SIZE)	/* LANCE MAC address chip */
#define IOASIC_LANCE	(3*IOASIC_SLOT_SIZE)	/* LANCE Ethernet */
#define IOASIC_SCC0	(4*IOASIC_SLOT_SIZE)	/* SCC #0 */
#define IOASIC_VDAC_HI	(5*IOASIC_SLOT_SIZE)	/* VDAC (maxine) */
#define IOASIC_SCC1	(6*IOASIC_SLOT_SIZE)	/* SCC #1 (3min, 3max+) */
#define IOASIC_VDAC_LO	(7*IOASIC_SLOT_SIZE)	/* VDAC (maxine) */
#define IOASIC_TOY	(8*IOASIC_SLOT_SIZE)	/* RTC */
#define IOASIC_ISDN	(9*IOASIC_SLOT_SIZE)	/* ISDN (maxine) */
#define IOASIC_ERRADDR	(9*IOASIC_SLOT_SIZE)	/* bus error address (3max+) */
#define IOASIC_CHKSYN	(10*IOASIC_SLOT_SIZE)	/* ECC syndrome (3max+) */
#define IOASIC_ACC_BUS	(10*IOASIC_SLOT_SIZE)	/* ACCESS.bus (maxine) */
#define IOASIC_MCR	(11*IOASIC_SLOT_SIZE)	/* memory control (3max+) */
#define IOASIC_FLOPPY	(11*IOASIC_SLOT_SIZE)	/* FDC (maxine) */
#define IOASIC_SCSI	(12*IOASIC_SLOT_SIZE)	/* ASC SCSI */
#define IOASIC_FDC_DMA	(13*IOASIC_SLOT_SIZE)	/* FDC DMA (maxine) */
#define IOASIC_SCSI_DMA (14*IOASIC_SLOT_SIZE)	/* ??? */
#define IOASIC_RES_15	(15*IOASIC_SLOT_SIZE)	/* unused? */


/*
 * Offsets for I/O ASIC registers
 * (relative to (dec_kn_slot_base + IOASIC_IOCTL)).
 */
					/* all systems */
#define IO_REG_SCSI_DMA_P	0x00	/* SCSI DMA Pointer */
#define IO_REG_SCSI_DMA_BP	0x10	/* SCSI DMA Buffer Pointer */
#define IO_REG_LANCE_DMA_P	0x20	/* LANCE DMA Pointer */
#define IO_REG_SCC0A_T_DMA_P	0x30	/* SCC0A Transmit DMA Pointer */
#define IO_REG_SCC0A_R_DMA_P	0x40	/* SCC0A Receive DMA Pointer */

					/* except Maxine */
#define IO_REG_SCC1A_T_DMA_P	0x50	/* SCC1A Transmit DMA Pointer */
#define IO_REG_SCC1A_R_DMA_P	0x60	/* SCC1A Receive DMA Pointer */

					/* Maxine */
#define IO_REG_AB_T_DMA_P	0x50	/* ACCESS.bus Transmit DMA Pointer */
#define IO_REG_AB_R_DMA_P	0x60	/* ACCESS.bus Receive DMA Pointer */
#define IO_REG_FLOPPY_DMA_P	0x70	/* Floppy DMA Pointer */
#define IO_REG_ISDN_T_DMA_P	0x80	/* ISDN Transmit DMA Pointer */
#define IO_REG_ISDN_T_DMA_BP	0x90	/* ISDN Transmit DMA Buffer Pointer */
#define IO_REG_ISDN_R_DMA_P	0xa0	/* ISDN Receive DMA Pointer */
#define IO_REG_ISDN_R_DMA_BP	0xb0	/* ISDN Receive DMA Buffer Pointer */

					/* all systems */
#define IO_REG_DATA_0		0xc0	/* System Data Buffer 0 */
#define IO_REG_DATA_1		0xd0	/* System Data Buffer 1 */
#define IO_REG_DATA_2		0xe0	/* System Data Buffer 2 */
#define IO_REG_DATA_3		0xf0	/* System Data Buffer 3 */

					/* all systems */
#define IO_REG_SSR		0x100	/* System Support Register */
#define IO_REG_SIR		0x110	/* System Interrupt Register */
#define IO_REG_SIMR		0x120	/* System Interrupt Mask Reg. */
#define IO_REG_SAR		0x130	/* System Address Register */

					/* Maxine */
#define IO_REG_ISDN_T_DATA	0x140	/* ISDN Xmit Data Register */
#define IO_REG_ISDN_R_DATA	0x150	/* ISDN Receive Data Register */

					/* all systems */
#define IO_REG_LANCE_SLOT	0x160	/* LANCE I/O Slot Register */
#define IO_REG_SCSI_SLOT	0x170	/* SCSI Slot Register */
#define IO_REG_SCC0A_SLOT	0x180	/* SCC0A DMA Slot Register */

					/* except Maxine */
#define IO_REG_SCC1A_SLOT	0x190	/* SCC1A DMA Slot Register */

					/* Maxine */
#define IO_REG_AB_SLOT		0x190	/* ACCESS.bus DMA Slot Register */
#define IO_REG_FLOPPY_SLOT	0x1a0	/* Floppy Slot Register */

					/* all systems */
#define IO_REG_SCSI_SCR		0x1b0	/* SCSI Partial-Word DMA Control */
#define IO_REG_SCSI_SDR0	0x1c0	/* SCSI DMA Partial Word 0 */
#define IO_REG_SCSI_SDR1	0x1d0	/* SCSI DMA Partial Word 1 */
#define IO_REG_FCTR		0x1e0	/* Free-Running Counter */
#define IO_REG_RES_31		0x1f0	/* unused */


/*
 * The upper 16 bits of the System Support Register are a part of the
 * I/O ASIC's internal DMA engine and thus are common to all I/O ASIC
 * machines.  The exception is the Maxine, which makes use of the
 * FLOPPY and ISDN bits (otherwise unused) and has a different SCC
 * wiring.
 */
						/* all systems */
#define IO_SSR_SCC0A_TX_DMA_EN	(1<<31)		/* SCC0A transmit DMA enable */
#define IO_SSR_SCC0A_RX_DMA_EN	(1<<30)		/* SCC0A receive DMA enable */
#define IO_SSR_RES_27		(1<<27)		/* unused */
#define IO_SSR_RES_26		(1<<26)		/* unused */
#define IO_SSR_RES_25		(1<<25)		/* unused */
#define IO_SSR_RES_24		(1<<24)		/* unused */
#define IO_SSR_RES_23		(1<<23)		/* unused */
#define IO_SSR_SCSI_DMA_DIR	(1<<18)		/* SCSI DMA direction */
#define IO_SSR_SCSI_DMA_EN	(1<<17)		/* SCSI DMA enable */
#define IO_SSR_LANCE_DMA_EN	(1<<16)		/* LANCE DMA enable */

						/* except Maxine */
#define IO_SSR_SCC1A_TX_DMA_EN	(1<<29)		/* SCC1A transmit DMA enable */
#define IO_SSR_SCC1A_RX_DMA_EN	(1<<28)		/* SCC1A receive DMA enable */
#define IO_SSR_RES_22		(1<<22)		/* unused */
#define IO_SSR_RES_21		(1<<21)		/* unused */
#define IO_SSR_RES_20		(1<<20)		/* unused */
#define IO_SSR_RES_19		(1<<19)		/* unused */

						/* Maxine */
#define IO_SSR_AB_TX_DMA_EN	(1<<29)		/* ACCESS.bus xmit DMA enable */
#define IO_SSR_AB_RX_DMA_EN	(1<<28)		/* ACCESS.bus recv DMA enable */
#define IO_SSR_FLOPPY_DMA_DIR	(1<<22)		/* Floppy DMA direction */
#define IO_SSR_FLOPPY_DMA_EN	(1<<21)		/* Floppy DMA enable */
#define IO_SSR_ISDN_TX_DMA_EN	(1<<20)		/* ISDN transmit DMA enable */
#define IO_SSR_ISDN_RX_DMA_EN	(1<<19)		/* ISDN receive DMA enable */

/*
 * The lower 16 bits are system-specific.  Bits 15,11:8 are common and
 * defined here.  The rest is defined in system-specific headers.
 */
#define KN0X_IO_SSR_DIAGDN	(1<<15)		/* diagnostic jumper */
#define KN0X_IO_SSR_SCC_RST	(1<<11)		/* ~SCC0,1 (Z85C30) reset */
#define KN0X_IO_SSR_RTC_RST	(1<<10)		/* ~RTC (DS1287) reset */
#define KN0X_IO_SSR_ASC_RST	(1<<9)		/* ~ASC (NCR53C94) reset */
#define KN0X_IO_SSR_LANCE_RST	(1<<8)		/* ~LANCE (Am7990) reset */

#endif /* __ASM_MIPS_DEC_IOASIC_ADDRS_H */
