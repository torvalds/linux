/* drivers/char/ser_a2232.h */

/* $Id: ser_a2232.h,v 0.4 2000/01/25 12:00:00 ehaase Exp $ */

/* Linux serial driver for the Amiga A2232 board */

/* This driver is MAINTAINED. Before applying any changes, please contact
 * the author.
 */
   
/* Copyright (c) 2000-2001 Enver Haase    <ehaase@inf.fu-berlin.de>
 *                   alias The A2232 driver project <A2232@gmx.net>
 * All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *  
 */

#ifndef _SER_A2232_H_
#define _SER_A2232_H_

/*
	How many boards are to be supported at maximum;
	"up to five A2232 Multiport Serial Cards may be installed in a
	single Amiga 2000" states the A2232 User's Guide. If you have
	more slots available, you might want to change the value below.
*/
#define MAX_A2232_BOARDS 5

#ifndef A2232_NORMAL_MAJOR
/* This allows overriding on the compiler commandline, or in a "major.h" 
   include or something like that */
#define A2232_NORMAL_MAJOR  224	/* /dev/ttyY* */
#define A2232_CALLOUT_MAJOR 225	/* /dev/cuy*  */
#endif

/* Some magic is always good - Who knows :) */
#define A2232_MAGIC 0x000a2232

/* A2232 port structure to keep track of the
   status of every single line used */
struct a2232_port{
	struct gs_port gs;
	unsigned int which_a2232;
	unsigned int which_port_on_a2232;
	short disable_rx;
	short throttle_input;
	short cd_status;
};

#define	NUMLINES		7	/* number of lines per board */
#define	A2232_IOBUFLEN		256	/* number of bytes per buffer */
#define	A2232_IOBUFLENMASK	0xff	/* mask for maximum number of bytes */


#define	A2232_UNKNOWN	0	/* crystal not known */
#define	A2232_NORMAL	1	/* normal A2232 (1.8432 MHz oscillator) */
#define	A2232_TURBO	2	/* turbo A2232 (3.6864 MHz oscillator) */


struct a2232common {
	char   Crystal;	/* normal (1) or turbo (2) board? */
	u_char Pad_a;
	u_char TimerH;	/* timer value after speed check */
	u_char TimerL;
	u_char CDHead;	/* head pointer for CD message queue */
	u_char CDTail;	/* tail pointer for CD message queue */
	u_char CDStatus;
	u_char Pad_b;
};

struct a2232status {
	u_char InHead;		/* input queue head */
	u_char InTail;		/* input queue tail */
	u_char OutDisable;	/* disables output */
	u_char OutHead;		/* output queue head */
	u_char OutTail;		/* output queue tail */
	u_char OutCtrl;		/* soft flow control character to send */
	u_char OutFlush;	/* flushes output buffer */
	u_char Setup;		/* causes reconfiguration */
	u_char Param;		/* parameter byte - see A2232PARAM */
	u_char Command;		/* command byte - see A2232CMD */
	u_char SoftFlow;	/* enables xon/xoff flow control */
	/* private 65EC02 fields: */
	u_char XonOff;		/* stores XON/XOFF enable/disable */
};

#define	A2232_MEMPAD1	\
	(0x0200 - NUMLINES * sizeof(struct a2232status)	-	\
	sizeof(struct a2232common))
#define	A2232_MEMPAD2	(0x2000 - NUMLINES * A2232_IOBUFLEN - A2232_IOBUFLEN)

struct a2232memory {
	struct a2232status Status[NUMLINES];	/* 0x0000-0x006f status areas */
	struct a2232common Common;		/* 0x0070-0x0077 common flags */
	u_char Dummy1[A2232_MEMPAD1];		/* 0x00XX-0x01ff */
	u_char OutBuf[NUMLINES][A2232_IOBUFLEN];/* 0x0200-0x08ff output bufs */
	u_char InBuf[NUMLINES][A2232_IOBUFLEN];	/* 0x0900-0x0fff input bufs */
	u_char InCtl[NUMLINES][A2232_IOBUFLEN];	/* 0x1000-0x16ff control data */
	u_char CDBuf[A2232_IOBUFLEN];		/* 0x1700-0x17ff CD event buffer */
	u_char Dummy2[A2232_MEMPAD2];		/* 0x1800-0x2fff */
	u_char Code[0x1000];			/* 0x3000-0x3fff code area */
	u_short InterruptAck;			/* 0x4000        intr ack */
	u_char Dummy3[0x3ffe];			/* 0x4002-0x7fff */
	u_short Enable6502Reset;		/* 0x8000 Stop board, */
						/*  6502 RESET line held low */
	u_char Dummy4[0x3ffe];			/* 0x8002-0xbfff */
	u_short ResetBoard;			/* 0xc000 reset board & run, */
						/*  6502 RESET line held high */
};

#undef A2232_MEMPAD1
#undef A2232_MEMPAD2

#define	A2232INCTL_CHAR		0	/* corresponding byte in InBuf is a character */
#define	A2232INCTL_EVENT	1	/* corresponding byte in InBuf is an event */

#define	A2232EVENT_Break	1	/* break set */
#define	A2232EVENT_CarrierOn	2	/* carrier raised */
#define	A2232EVENT_CarrierOff	3	/* carrier dropped */
#define A2232EVENT_Sync		4	/* don't know, defined in 2232.ax */

#define	A2232CMD_Enable		0x1	/* enable/DTR bit */
#define	A2232CMD_Close		0x2	/* close the device */
#define	A2232CMD_Open		0xb	/* open the device */
#define	A2232CMD_CMask		0xf	/* command mask */
#define	A2232CMD_RTSOff		0x0  	/* turn off RTS */
#define	A2232CMD_RTSOn		0x8	/* turn on RTS */
#define	A2232CMD_Break		0xd	/* transmit a break */
#define	A2232CMD_RTSMask	0xc	/* mask for RTS stuff */
#define	A2232CMD_NoParity	0x00	/* don't use parity */
#define	A2232CMD_OddParity	0x20	/* odd parity */
#define	A2232CMD_EvenParity	0x60	/* even parity */
#define	A2232CMD_ParityMask	0xe0	/* parity mask */

#define	A2232PARAM_B115200	0x0	/* baud rates */
#define	A2232PARAM_B50		0x1
#define	A2232PARAM_B75		0x2
#define	A2232PARAM_B110		0x3
#define	A2232PARAM_B134		0x4
#define	A2232PARAM_B150		0x5
#define	A2232PARAM_B300		0x6
#define	A2232PARAM_B600		0x7
#define	A2232PARAM_B1200	0x8
#define	A2232PARAM_B1800	0x9
#define	A2232PARAM_B2400	0xa
#define	A2232PARAM_B3600	0xb
#define	A2232PARAM_B4800	0xc
#define	A2232PARAM_B7200	0xd
#define	A2232PARAM_B9600	0xe
#define	A2232PARAM_B19200	0xf
#define	A2232PARAM_BaudMask	0xf	/* baud rate mask */
#define	A2232PARAM_RcvBaud	0x10	/* enable receive baud rate */
#define	A2232PARAM_8Bit		0x00	/* numbers of bits */
#define	A2232PARAM_7Bit		0x20
#define	A2232PARAM_6Bit		0x40
#define	A2232PARAM_5Bit		0x60
#define	A2232PARAM_BitMask	0x60	/* numbers of bits mask */


/* Standard speeds tables, -1 means unavailable, -2 means 0 baud: switch off line */
#define A2232_BAUD_TABLE_NOAVAIL -1
#define A2232_BAUD_TABLE_NUM_RATES (18)
static int a2232_baud_table[A2232_BAUD_TABLE_NUM_RATES*3] = {
	//Baud	//Normal			//Turbo
	50,	A2232PARAM_B50,			A2232_BAUD_TABLE_NOAVAIL,
	75,	A2232PARAM_B75,			A2232_BAUD_TABLE_NOAVAIL,
	110,	A2232PARAM_B110,		A2232_BAUD_TABLE_NOAVAIL,
	134,	A2232PARAM_B134,		A2232_BAUD_TABLE_NOAVAIL,
	150,	A2232PARAM_B150,		A2232PARAM_B75,
	200,	A2232_BAUD_TABLE_NOAVAIL,	A2232_BAUD_TABLE_NOAVAIL,
	300,	A2232PARAM_B300,		A2232PARAM_B150,
	600,	A2232PARAM_B600,		A2232PARAM_B300,
	1200,	A2232PARAM_B1200,		A2232PARAM_B600,
	1800,	A2232PARAM_B1800,		A2232_BAUD_TABLE_NOAVAIL,
	2400,	A2232PARAM_B2400,		A2232PARAM_B1200,
	4800,	A2232PARAM_B4800,		A2232PARAM_B2400,
	9600,	A2232PARAM_B9600,		A2232PARAM_B4800,
	19200,	A2232PARAM_B19200,		A2232PARAM_B9600,
	38400,	A2232_BAUD_TABLE_NOAVAIL,	A2232PARAM_B19200,
	57600,	A2232_BAUD_TABLE_NOAVAIL,	A2232_BAUD_TABLE_NOAVAIL,
#ifdef A2232_SPEEDHACK
	115200,	A2232PARAM_B115200,		A2232_BAUD_TABLE_NOAVAIL,
	230400,	A2232_BAUD_TABLE_NOAVAIL,	A2232PARAM_B115200
#else
	115200,	A2232_BAUD_TABLE_NOAVAIL,	A2232_BAUD_TABLE_NOAVAIL,
	230400,	A2232_BAUD_TABLE_NOAVAIL,	A2232_BAUD_TABLE_NOAVAIL
#endif
};
#endif
