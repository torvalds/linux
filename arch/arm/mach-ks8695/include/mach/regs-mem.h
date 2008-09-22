/*
 * arch/arm/mach-ks8695/include/mach/regs-mem.h
 *
 * Copyright (C) 2006 Andrew Victor
 *
 * KS8695 - Memory Controller registers and bit definitions
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef KS8695_MEM_H
#define KS8695_MEM_H

#define KS8695_MEM_OFFSET	(0xF0000 + 0x4000)
#define KS8695_MEM_VA		(KS8695_IO_VA + KS8695_MEM_OFFSET)
#define KS8695_MEM_PA		(KS8695_IO_PA + KS8695_MEM_OFFSET)


/*
 * Memory Controller Registers
 */
#define KS8695_EXTACON0		(0x00)		/* External I/O 0 Access Control */
#define KS8695_EXTACON1		(0x04)		/* External I/O 1 Access Control */
#define KS8695_EXTACON2		(0x08)		/* External I/O 2 Access Control */
#define KS8695_ROMCON0		(0x10)		/* ROM/SRAM/Flash 1 Control Register */
#define KS8695_ROMCON1		(0x14)		/* ROM/SRAM/Flash 2 Control Register */
#define KS8695_ERGCON		(0x20)		/* External I/O and ROM/SRAM/Flash General Register */
#define KS8695_SDCON0		(0x30)		/* SDRAM Control Register 0 */
#define KS8695_SDCON1		(0x34)		/* SDRAM Control Register 1 */
#define KS8695_SDGCON		(0x38)		/* SDRAM General Control */
#define KS8695_SDBCON		(0x3c)		/* SDRAM Buffer Control */
#define KS8695_REFTIM		(0x40)		/* SDRAM Refresh Timer */


/* External I/O Access Control Registers */
#define EXTACON_EBNPTR		(0x3ff << 22)		/* Last Address Pointer */
#define EXTACON_EBBPTR		(0x3ff << 12)		/* Base Pointer */
#define EXTACON_EBTACT		(7     <<  9)		/* Write Enable/Output Enable Active Time */
#define EXTACON_EBTCOH		(7     <<  6)		/* Chip Select Hold Time */
#define EXTACON_EBTACS		(7     <<  3)		/* Address Setup Time before ECSN */
#define EXTACON_EBTCOS		(7     <<  0)		/* Chip Select Time before OEN */

/* ROM/SRAM/Flash Control Register */
#define ROMCON_RBNPTR		(0x3ff << 22)		/* Next Pointer */
#define ROMCON_RBBPTR		(0x3ff << 12)		/* Base Pointer */
#define ROMCON_RBTACC		(7     <<  4)		/* Access Cycle Time */
#define ROMCON_RBTPA		(3     <<  2)		/* Page Address Access Time */
#define ROMCON_PMC		(3     <<  0)		/* Page Mode Configuration */
#define		PMC_NORMAL		(0 << 0)
#define		PMC_4WORD		(1 << 0)
#define		PMC_8WORD		(2 << 0)
#define		PMC_16WORD		(3 << 0)

/* External I/O and ROM/SRAM/Flash General Register */
#define ERGCON_TMULT		(3 << 28)		/* Time Multiplier */
#define ERGCON_DSX2		(3 << 20)		/* Data Width (External I/O Bank 2) */
#define ERGCON_DSX1		(3 << 18)		/* Data Width (External I/O Bank 1) */
#define ERGCON_DSX0		(3 << 16)		/* Data Width (External I/O Bank 0) */
#define ERGCON_DSR1		(3 <<  2)		/* Data Width (ROM/SRAM/Flash Bank 1) */
#define ERGCON_DSR0		(3 <<  0)		/* Data Width (ROM/SRAM/Flash Bank 0) */

/* SDRAM Control Register */
#define SDCON_DBNPTR		(0x3ff << 22)		/* Last Address Pointer */
#define SDCON_DBBPTR		(0x3ff << 12)		/* Base Pointer */
#define SDCON_DBCAB		(3     <<  8)		/* Column Address Bits */
#define SDCON_DBBNUM		(1     <<  3)		/* Number of Banks */
#define SDCON_DBDBW		(3     <<  1)		/* Data Bus Width */

/* SDRAM General Control Register */
#define SDGCON_SDTRC		(3 << 2)		/* RAS to CAS latency */
#define SDGCON_SDCAS		(3 << 0)		/* CAS latency */

/* SDRAM Buffer Control Register */
#define SDBCON_SDESTA		(1 << 31)		/* SDRAM Engine Status */
#define SDBCON_RBUFBDIS		(1 << 24)		/* Read Buffer Burst Enable */
#define SDBCON_WFIFOEN		(1 << 23)		/* Write FIFO Enable */
#define SDBCON_RBUFEN		(1 << 22)		/* Read Buffer Enable */
#define SDBCON_FLUSHWFIFO	(1 << 21)		/* Flush Write FIFO */
#define SDBCON_RBUFINV		(1 << 20)		/* Read Buffer Invalidate */
#define SDBCON_SDINI		(3 << 16)		/* SDRAM Initialization Control */
#define SDBCON_SDMODE		(0x3fff << 0)		/* SDRAM Mode Register Value Program */

/* SDRAM Refresh Timer Register */
#define REFTIM_REFTIM		(0xffff << 0)		/* Refresh Timer Value */


#endif
