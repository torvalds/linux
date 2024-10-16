/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright		   : (C) 2002, 2004 by Frank Mori Hess
 ***************************************************************************/

#ifndef _TNT4882_REGISTERS_H
#define _TNT4882_REGISTERS_H

// tnt4882 register offsets
enum {
	ACCWR = 0x5,
	// offset of auxiliary command register in 9914 mode
	AUXCR = 0x6,
	INTRT = 0x7,
	// register number for auxiliary command register when swap bit is set (9914 mode)
	SWAPPED_AUXCR = 0xa,
	HSSEL = 0xd,	// handshake select register
	CNT2 = 0x9,
	CNT3 = 0xb,
	CFG = 0x10,
	SASR = 0x1b,
	IMR0 = 0x1d,
	IMR3 = 0x12,
	CNT0 = 0x14,
	CNT1 = 0x16,
	KEYREG = 0x17,	// key control register (7210 mode only)
	CSR = KEYREG,
	FIFOB = 0x18,
	FIFOA = 0x19,
	CCR = 0x1a,	// carry cycle register
	CMDR = 0x1c,	// command register
	TIMER = 0x1e,	// timer register

	STS1 = 0x10,		/* T488 Status Register 1 */
	STS2 = 0x1c,		/* T488 Status Register 2 */
	ISR0 = IMR0,
	ISR3 = 0x1a,		/* T488 Interrupt Status Register 3 */
	BCR = 0x1f,		/* bus control/status register */
	BSR = BCR,
};

enum {
	tnt_pagein_offset = 0x11,
};

/*============================================================*/

/* TURBO-488 registers bit definitions */

enum bus_control_status_bits {
	BCSR_REN_BIT = 0x1,
	BCSR_IFC_BIT = 0x2,
	BCSR_SRQ_BIT = 0x4,
	BCSR_EOI_BIT = 0x8,
	BCSR_NRFD_BIT = 0x10,
	BCSR_NDAC_BIT = 0x20,
	BCSR_DAV_BIT = 0x40,
	BCSR_ATN_BIT = 0x80,
};

/* CFG -- Configuration Register (write only) */
enum cfg_bits {
	TNT_COMMAND = 0x80,	/* bytes are command bytes instead of data bytes
				 * (tnt4882 one-chip and newer only?)
				 */
	TNT_TLCHE = (1 << 6),	/* halt transfer on imr0, imr1, or imr2 interrupt */
	TNT_IN = (1 << 5),	/* transfer is GPIB read		 */
	TNT_A_B = (1 << 4),	/* order to use fifos 1=fifo A first(big endian),
				 * 0=fifo b first(little endian)
				 */
	TNT_CCEN = (1 << 3),	/* enable carry cycle		      */
	TNT_TMOE = (1 << 2),	/* enable CPU bus time limit	      */
	TNT_TIM_BYTN = (1 << 1),	/* tmot reg is: 1=125ns clocks, 0=num bytes */
	TNT_B_16BIT = (1 << 0),	/* 1=FIFO is 16-bit register, 0=8-bit */
};

/* CMDR -- Command Register */
enum cmdr_bits {
	CLRSC = 0x2,	/* clear the system controller bit */
	SETSC = 0x3,	/* set the system controller bit */
	GO = 0x4,	/* start fifos */
	STOP = 0x8,	/* stop fifos */
	RESET_FIFO = 0x10,	/* reset the FIFOs		*/
	SOFT_RESET = 0x22,	/* issue a software reset	*/
	HARD_RESET = 0x40	/* 500x only? */
};

/* HSSEL -- handshake select register (write only) */
enum hssel_bits {
	TNT_ONE_CHIP_BIT = 0x1,
	NODMA = 0x10,
	TNT_GO2SIDS_BIT = 0x20,
};

/* IMR0 -- Interrupt Mode Register 0 */
enum imr0_bits {
	TNT_SYNCIE_BIT = 0x1, /* handshake sync */
	TNT_TOIE_BIT = 0x2, /* timeout */
	TNT_ATNIE_BIT = 0x4, /* ATN interrupt */
	TNT_IFCIE_BIT = 0x8,	/* interface clear interrupt */
	TNT_BTO_BIT = 0x10, /* byte timeout */
	TNT_NLEN_BIT = 0x20,	/* treat new line as EOS char */
	TNT_STBOIE_BIT = 0x40,	/* status byte out  */
	TNT_IMR0_ALWAYS_BITS = 0x80,	/* always set this bit on write */
};

/* ISR0 -- Interrupt Status Register 0 */
enum isr0_bits {
	TNT_SYNC_BIT = 0x1, /* handshake sync */
	TNT_TO_BIT = 0x2, /* timeout */
	TNT_ATNI_BIT = 0x4, /* ATN interrupt */
	TNT_IFCI_BIT = 0x8,	/* interface clear interrupt */
	TNT_EOS_BIT = 0x10, /* end of string */
	TNT_NL_BIT = 0x20,	/* new line receive */
	TNT_STBO_BIT = 0x40,	/* status byte out  */
	TNT_NBA_BIT = 0x80,	/* new byte available */
};

/* ISR3 -- Interrupt Status Register 3 (read only) */
enum isr3_bits {
	HR_DONE = (1 << 0),	/* transfer done */
	HR_TLCI = (1 << 1),	/* isr0, isr1, or isr2 interrupt asserted */
	HR_NEF = (1 << 2),	/* NOT empty fifo */
	HR_NFF = (1 << 3),	/* NOT full fifo */
	HR_STOP = (1 << 4),	/* fifo empty or STOP command issued */
	HR_SRQI_CIC = (1 << 5),	/* SRQ asserted and we are CIC (500x only?)*/
	HR_INTR = (1 << 7),	/* isr3 interrupt active */
};

enum keyreg_bits {
	MSTD = 0x20,	// enable 350ns T1 delay
};

/* STS1 -- Status Register 1 (read only) */
enum sts1_bits {
	S_DONE = 0x80,	/* DMA done			      */
	S_SC = 0x40,	/* is system controller		      */
	S_IN = 0x20,	/* DMA in (to memory)		      */
	S_DRQ = 0x10,	/* DRQ line (for diagnostics)	      */
	S_STOP = 0x08,	/* DMA stopped			      */
	S_NDAV = 0x04,	/* inverse of DAV		      */
	S_HALT = 0x02,	/* status of transfer machine	      */
	S_GSYNC = 0x01,	/* indicates if GPIB is in sync w I/O */
};

/* STS2 -- Status Register 2 */
enum sts2_bits {
	AFFN = (1 << 3),	/* "A full FIFO NOT"  (0=FIFO full)  */
	AEFN = (1 << 2),	/* "A empty FIFO NOT" (0=FIFO empty) */
	BFFN = (1 << 1),	/* "B full FIFO NOT"  (0=FIFO full)  */
	BEFN = (1 << 0),	/* "B empty FIFO NOT" (0=FIFO empty) */
};

// Auxiliary commands
enum tnt4882_aux_cmds {
	AUX_9914 = 0x15,	// switch to 9914 mode
	AUX_REQT = 0x18,
	AUX_REQF = 0x19,
	AUX_PAGEIN = 0x50,	/* page in alternate registers */
	AUX_HLDI = 0x51,	// rfd holdoff immediately
	AUX_CLEAR_END = 0x55,
	AUX_7210 = 0x99,	// switch to 7210 mode
};

enum tnt4882_aux_regs {
	AUXRG = 0x40,
	AUXRI = 0xe0,
};

enum auxg_bits {
 /* no talking when no listeners bit (prevents bus errors when data written at wrong time) */
	NTNL_BIT = 0x8,
	RPP2_BIT = 0x4,	/* set/clear local rpp message */
	CHES_BIT = 0x1, /*clear holdoff on end select bit*/
};

enum auxi_bits {
	SISB = 0x1,	// static interrupt bits (don't clear isr1, isr2 on read)
	PP2 = 0x4,	// ignore remote parallel poll configuration
	USTD = 0x8,	// ultra short (1100 nanosec) T1 delay
};

enum sasr_bits {
	ACRDY_BIT = 0x4,	/* acceptor ready state */
	ADHS_BIT = 0x8,	/* acceptor data holdoff state */
	ANHS2_BIT = 0x10,	/* acceptor not ready holdoff immediately state */
	ANHS1_BIT = 0x20,	/* acceptor not ready holdoff state */
	AEHS_BIT = 0x40,	/* acceptor end holdoff state */
};

#endif	// _TNT4882_REGISTERS_H
