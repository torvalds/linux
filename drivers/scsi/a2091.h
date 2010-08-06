#ifndef A2091_H
#define A2091_H

/* $Id: a2091.h,v 1.4 1997/01/19 23:07:09 davem Exp $
 *
 * Header file for the Commodore A2091 Zorro II SCSI controller for Linux
 *
 * Written and (C) 1993, Hamish Macdonald, see a2091.c for more info
 *
 */

#include <linux/types.h>

#ifndef CMD_PER_LUN
#define CMD_PER_LUN		2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE		16
#endif

/*
 * if the transfer address ANDed with this results in a non-zero
 * result, then we can't use DMA.
 */
#define A2091_XFER_MASK		(0xff000001)

struct a2091_scsiregs {
		 unsigned char	pad1[64];
	volatile unsigned short	ISTR;
	volatile unsigned short	CNTR;
		 unsigned char	pad2[60];
	volatile unsigned int	WTC;
	volatile unsigned long	ACR;
		 unsigned char	pad3[6];
	volatile unsigned short	DAWR;
		 unsigned char	pad4;
	volatile unsigned char	SASR;
		 unsigned char	pad5;
	volatile unsigned char	SCMD;
		 unsigned char	pad6[76];
	volatile unsigned short	ST_DMA;
	volatile unsigned short	SP_DMA;
	volatile unsigned short	CINT;
		 unsigned char	pad7[2];
	volatile unsigned short	FLUSH;
};

#define DAWR_A2091		(3)

/* CNTR bits. */
#define CNTR_TCEN		(1<<7)
#define CNTR_PREST		(1<<6)
#define CNTR_PDMD		(1<<5)
#define CNTR_INTEN		(1<<4)
#define CNTR_DDIR		(1<<3)

/* ISTR bits. */
#define ISTR_INTX		(1<<8)
#define ISTR_INT_F		(1<<7)
#define ISTR_INTS		(1<<6)
#define ISTR_E_INT		(1<<5)
#define ISTR_INT_P		(1<<4)
#define ISTR_UE_INT		(1<<3)
#define ISTR_OE_INT		(1<<2)
#define ISTR_FF_FLG		(1<<1)
#define ISTR_FE_FLG		(1<<0)

#endif /* A2091_H */
