/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	include/asm-mips/dec/kn05.h
 *
 *	DECstation/DECsystem 5000/260 (4max+ or KN05), 5000/150 (4min
 *	or KN04-BA), Personal DECstation/DECsystem 5000/50 (4maxine or
 *	KN04-CA) and DECsystem 5900/260 (KN05) R4k CPU card MB ASIC
 *	definitions.
 *
 *	Copyright (C) 2002, 2003, 2005, 2008  Maciej W. Rozycki
 *
 *	WARNING!  All this information is pure guesswork based on the
 *	ROM.  It is provided here in hope it will give someone some
 *	food for thought.  No documentation for the KN05 nor the KN04
 *	module has been located so far.
 */
#ifndef __ASM_MIPS_DEC_KN05_H
#define __ASM_MIPS_DEC_KN05_H

#include <asm/dec/ioasic_addrs.h>

/*
 * The oncard MB (Memory Buffer) ASIC provides an additional address
 * decoder.  Certain address ranges within the "high" 16 slots are
 * passed to the I/O ASIC's decoder like with the KN03 or KN02-BA/CA.
 * Others are handled locally.  "Low" slots are always passed.
 */
#define KN4K_SLOT_BASE	0x1fc00000

#define KN4K_MB_ROM	(0*IOASIC_SLOT_SIZE)	/* KN05/KN04 card ROM */
#define KN4K_IOCTL	(1*IOASIC_SLOT_SIZE)	/* I/O ASIC */
#define KN4K_ESAR	(2*IOASIC_SLOT_SIZE)	/* LANCE MAC address chip */
#define KN4K_LANCE	(3*IOASIC_SLOT_SIZE)	/* LANCE Ethernet */
#define KN4K_MB_INT	(4*IOASIC_SLOT_SIZE)	/* MB interrupt register */
#define KN4K_MB_EA	(5*IOASIC_SLOT_SIZE)	/* MB error address? */
#define KN4K_MB_EC	(6*IOASIC_SLOT_SIZE)	/* MB error ??? */
#define KN4K_MB_CSR	(7*IOASIC_SLOT_SIZE)	/* MB control & status */
#define KN4K_RES_08	(8*IOASIC_SLOT_SIZE)	/* unused? */
#define KN4K_RES_09	(9*IOASIC_SLOT_SIZE)	/* unused? */
#define KN4K_RES_10	(10*IOASIC_SLOT_SIZE)	/* unused? */
#define KN4K_RES_11	(11*IOASIC_SLOT_SIZE)	/* unused? */
#define KN4K_SCSI	(12*IOASIC_SLOT_SIZE)	/* ASC SCSI */
#define KN4K_RES_13	(13*IOASIC_SLOT_SIZE)	/* unused? */
#define KN4K_RES_14	(14*IOASIC_SLOT_SIZE)	/* unused? */
#define KN4K_RES_15	(15*IOASIC_SLOT_SIZE)	/* unused? */

/*
 * MB ASIC interrupt bits.
 */
#define KN4K_MB_INR_MB		4	/* ??? */
#define KN4K_MB_INR_MT		3	/* memory, I/O bus read/write errors */
#define KN4K_MB_INR_RES_2	2	/* unused */
#define KN4K_MB_INR_RTC		1	/* RTC */
#define KN4K_MB_INR_TC		0	/* I/O ASIC cascade */

/*
 * Bits for the MB interrupt register.
 * The register appears read-only.
 */
#define KN4K_MB_INT_IRQ		(0x1f<<0)	/* CPU Int[4:0] status. */
#define KN4K_MB_INT_IRQ_N(n)	(1<<(n))	/* Individual status bits. */

/*
 * Bits for the MB control & status register.
 * Set to 0x00bf8001 for KN05 and to 0x003f8000 for KN04 by the firmware.
 */
#define KN4K_MB_CSR_PF		(1<<0)		/* PreFetching enable? */
#define KN4K_MB_CSR_F		(1<<1)		/* ??? */
#define KN4K_MB_CSR_ECC		(0xff<<2)	/* ??? */
#define KN4K_MB_CSR_OD		(1<<10)		/* ??? */
#define KN4K_MB_CSR_CP		(1<<11)		/* ??? */
#define KN4K_MB_CSR_UNC		(1<<12)		/* ??? */
#define KN4K_MB_CSR_IM		(1<<13)		/* ??? */
#define KN4K_MB_CSR_NC		(1<<14)		/* ??? */
#define KN4K_MB_CSR_EE		(1<<15)		/* (bus) Exception Enable? */
#define KN4K_MB_CSR_MSK		(0x1f<<16)	/* CPU Int[4:0] mask */
#define KN4K_MB_CSR_MSK_N(n)	(1<<((n)+16))	/* Individual mask bits. */
#define KN4K_MB_CSR_FW		(1<<21)		/* ??? */
#define KN4K_MB_CSR_W		(1<<31)		/* ??? */

#endif /* __ASM_MIPS_DEC_KN05_H */
