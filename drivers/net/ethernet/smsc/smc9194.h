/*------------------------------------------------------------------------
 . smc9194.h
 . Copyright (C) 1996 by Erik Stahlman
 .
 . This software may be used and distributed according to the terms
 . of the GNU General Public License, incorporated herein by reference.
 .
 . This file contains register information and access macros for
 . the SMC91xxx chipset.
 .
 . Information contained in this file was obtained from the SMC91C94
 . manual from SMC.  To get a copy, if you really want one, you can find
 . information under www.smc.com in the components division.
 . ( this thanks to advice from Donald Becker ).
 .
 . Authors
 . 	Erik Stahlman				( erik@vt.edu )
 .
 . History
 . 01/06/96		 Erik Stahlman   moved definitions here from main .c file
 . 01/19/96		 Erik Stahlman	  polished this up some, and added better
 .										  error handling
 .
 ---------------------------------------------------------------------------*/
#ifndef _SMC9194_H_
#define _SMC9194_H_

/* I want some simple types */

typedef unsigned char			byte;
typedef unsigned short			word;
typedef unsigned long int 		dword;


/* Because of bank switching, the SMC91xxx uses only 16 I/O ports */

#define SMC_IO_EXTENT	16


/*---------------------------------------------------------------
 .
 . A description of the SMC registers is probably in order here,
 . although for details, the SMC datasheet is invaluable.
 .
 . Basically, the chip has 4 banks of registers ( 0 to 3 ), which
 . are accessed by writing a number into the BANK_SELECT register
 . ( I also use a SMC_SELECT_BANK macro for this ).
 .
 . The banks are configured so that for most purposes, bank 2 is all
 . that is needed for simple run time tasks.
 -----------------------------------------------------------------------*/

/*
 . Bank Select Register:
 .
 .		yyyy yyyy 0000 00xx
 .		xx 		= bank number
 .		yyyy yyyy	= 0x33, for identification purposes.
*/
#define	BANK_SELECT		14

/* BANK 0  */

#define	TCR 		0    	/* transmit control register */
#define TCR_ENABLE	0x0001	/* if this is 1, we can transmit */
#define TCR_FDUPLX    	0x0800  /* receive packets sent out */
#define TCR_STP_SQET	0x1000	/* stop transmitting if Signal quality error */
#define	TCR_MON_CNS	0x0400	/* monitors the carrier status */
#define	TCR_PAD_ENABLE	0x0080	/* pads short packets to 64 bytes */

#define	TCR_CLEAR	0	/* do NOTHING */
/* the normal settings for the TCR register : */
/* QUESTION: do I want to enable padding of short packets ? */
#define	TCR_NORMAL  	TCR_ENABLE


#define EPH_STATUS	2
#define ES_LINK_OK	0x4000	/* is the link integrity ok ? */

#define	RCR		4
#define RCR_SOFTRESET	0x8000 	/* resets the chip */
#define	RCR_STRIP_CRC	0x200	/* strips CRC */
#define RCR_ENABLE	0x100	/* IFF this is set, we can receive packets */
#define RCR_ALMUL	0x4 	/* receive all multicast packets */
#define	RCR_PROMISC	0x2	/* enable promiscuous mode */

/* the normal settings for the RCR register : */
#define	RCR_NORMAL	(RCR_STRIP_CRC | RCR_ENABLE)
#define RCR_CLEAR	0x0		/* set it to a base state */

#define	COUNTER		6
#define	MIR		8
#define	MCR		10
/* 12 is reserved */

/* BANK 1 */
#define CONFIG			0
#define CFG_AUI_SELECT	 	0x100
#define	BASE			2
#define	ADDR0			4
#define	ADDR1			6
#define	ADDR2			8
#define	GENERAL			10
#define	CONTROL			12
#define	CTL_POWERDOWN		0x2000
#define	CTL_LE_ENABLE		0x80
#define	CTL_CR_ENABLE		0x40
#define	CTL_TE_ENABLE		0x0020
#define CTL_AUTO_RELEASE	0x0800
#define	CTL_EPROM_ACCESS	0x0003 /* high if Eprom is being read */

/* BANK 2 */
#define MMU_CMD		0
#define MC_BUSY		1	/* only readable bit in the register */
#define MC_NOP		0
#define	MC_ALLOC	0x20  	/* or with number of 256 byte packets */
#define	MC_RESET	0x40
#define	MC_REMOVE	0x60  	/* remove the current rx packet */
#define MC_RELEASE  	0x80  	/* remove and release the current rx packet */
#define MC_FREEPKT  	0xA0  	/* Release packet in PNR register */
#define MC_ENQUEUE	0xC0 	/* Enqueue the packet for transmit */

#define	PNR_ARR		2
#define FIFO_PORTS	4

#define FP_RXEMPTY  0x8000
#define FP_TXEMPTY  0x80

#define	POINTER		6
#define PTR_READ	0x2000
#define	PTR_RCV		0x8000
#define	PTR_AUTOINC 	0x4000
#define PTR_AUTO_INC	0x0040

#define	DATA_1		8
#define	DATA_2		10
#define	INTERRUPT	12

#define INT_MASK	13
#define IM_RCV_INT	0x1
#define	IM_TX_INT	0x2
#define	IM_TX_EMPTY_INT	0x4
#define	IM_ALLOC_INT	0x8
#define	IM_RX_OVRN_INT	0x10
#define	IM_EPH_INT	0x20
#define	IM_ERCV_INT	0x40 /* not on SMC9192 */

/* BANK 3 */
#define	MULTICAST1	0
#define	MULTICAST2	2
#define	MULTICAST3	4
#define	MULTICAST4	6
#define	MGMT		8
#define	REVISION	10 /* ( hi: chip id   low: rev # ) */


/* this is NOT on SMC9192 */
#define	ERCV		12

#define CHIP_9190	3
#define CHIP_9194	4
#define CHIP_9195	5
#define CHIP_91100	7

static const char * chip_ids[ 15 ] =  {
	NULL, NULL, NULL,
	/* 3 */ "SMC91C90/91C92",
	/* 4 */ "SMC91C94",
	/* 5 */ "SMC91C95",
	NULL,
	/* 7 */ "SMC91C100",
	/* 8 */ "SMC91C100FD",
	NULL, NULL, NULL,
	NULL, NULL, NULL};

/*
 . Transmit status bits
*/
#define TS_SUCCESS 0x0001
#define TS_LOSTCAR 0x0400
#define TS_LATCOL  0x0200
#define TS_16COL   0x0010

/*
 . Receive status bits
*/
#define RS_ALGNERR	0x8000
#define RS_BADCRC	0x2000
#define RS_ODDFRAME	0x1000
#define RS_TOOLONG	0x0800
#define RS_TOOSHORT	0x0400
#define RS_MULTICAST	0x0001
#define RS_ERRORS	(RS_ALGNERR | RS_BADCRC | RS_TOOLONG | RS_TOOSHORT)

static const char * interfaces[ 2 ] = { "TP", "AUI" };

/*-------------------------------------------------------------------------
 .  I define some macros to make it easier to do somewhat common
 . or slightly complicated, repeated tasks.
 --------------------------------------------------------------------------*/

/* select a register bank, 0 to 3  */

#define SMC_SELECT_BANK(x)  { outw( x, ioaddr + BANK_SELECT ); }

/* define a small delay for the reset */
#define SMC_DELAY() { inw( ioaddr + RCR );\
			inw( ioaddr + RCR );\
			inw( ioaddr + RCR );  }

/* this enables an interrupt in the interrupt mask register */
#define SMC_ENABLE_INT(x) {\
		unsigned char mask;\
		SMC_SELECT_BANK(2);\
		mask = inb( ioaddr + INT_MASK );\
		mask |= (x);\
		outb( mask, ioaddr + INT_MASK ); \
}

/* this disables an interrupt from the interrupt mask register */

#define SMC_DISABLE_INT(x) {\
		unsigned char mask;\
		SMC_SELECT_BANK(2);\
		mask = inb( ioaddr + INT_MASK );\
		mask &= ~(x);\
		outb( mask, ioaddr + INT_MASK ); \
}

/*----------------------------------------------------------------------
 . Define the interrupts that I want to receive from the card
 .
 . I want:
 .  IM_EPH_INT, for nasty errors
 .  IM_RCV_INT, for happy received packets
 .  IM_RX_OVRN_INT, because I have to kick the receiver
 --------------------------------------------------------------------------*/
#define SMC_INTERRUPT_MASK   (IM_EPH_INT | IM_RX_OVRN_INT | IM_RCV_INT)

#endif  /* _SMC_9194_H_ */

