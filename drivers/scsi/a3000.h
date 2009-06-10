#ifndef A3000_H
#define A3000_H

/* $Id: a3000.h,v 1.4 1997/01/19 23:07:10 davem Exp $
 *
 * Header file for the Amiga 3000 built-in SCSI controller for Linux
 *
 * Written and (C) 1993, Hamish Macdonald, see a3000.c for more info
 *
 */

#include <linux/types.h>

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

/*
 * if the transfer address ANDed with this results in a non-zero
 * result, then we can't use DMA.
 */
#define A3000_XFER_MASK  (0x00000003)

typedef struct {
             unsigned char      pad1[2];
    volatile unsigned short     DAWR;
    volatile unsigned int       WTC;
             unsigned char      pad2[2];
    volatile unsigned short     CNTR;
    volatile unsigned long      ACR;
             unsigned char      pad3[2];
    volatile unsigned short     ST_DMA;
             unsigned char      pad4[2];
    volatile unsigned short     FLUSH;
             unsigned char      pad5[2];
    volatile unsigned short     CINT;
             unsigned char      pad6[2];
    volatile unsigned short     ISTR;
	     unsigned char      pad7[30];
    volatile unsigned short     SP_DMA;
             unsigned char      pad8;
    volatile unsigned char      SASR;
             unsigned char      pad9;
    volatile unsigned char      SCMD;
} a3000_scsiregs;

#define DAWR_A3000		(3)

/* CNTR bits. */
#define CNTR_TCEN		(1<<5)
#define CNTR_PREST		(1<<4)
#define CNTR_PDMD		(1<<3)
#define CNTR_INTEN		(1<<2)
#define CNTR_DDIR		(1<<1)
#define CNTR_IO_DX		(1<<0)

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

#endif /* A3000_H */
