/*
 * (C) 2000 R.E.Wolff@BitWizard.nl
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/interrupt.h>


#define DEBUG_ALL

struct ttystatics {
	struct termios tm;
};

extern int rio_debug;

#define RIO_DEBUG_INIT         0x000001
#define RIO_DEBUG_BOOT         0x000002
#define RIO_DEBUG_CMD          0x000004
#define RIO_DEBUG_CTRL         0x000008
#define RIO_DEBUG_INTR         0x000010
#define RIO_DEBUG_PARAM        0x000020
#define RIO_DEBUG_ROUTE        0x000040
#define RIO_DEBUG_TABLE        0x000080
#define RIO_DEBUG_TTY          0x000100
#define RIO_DEBUG_FLOW         0x000200
#define RIO_DEBUG_MODEMSIGNALS 0x000400
#define RIO_DEBUG_PROBE        0x000800
#define RIO_DEBUG_CLEANUP      0x001000
#define RIO_DEBUG_IFLOW        0x002000
#define RIO_DEBUG_PFE          0x004000
#define RIO_DEBUG_REC          0x008000
#define RIO_DEBUG_SPINLOCK     0x010000
#define RIO_DEBUG_DELAY        0x020000
#define RIO_DEBUG_MOD_COUNT    0x040000


/* Copied over from riowinif.h . This is ugly. The winif file declares
also much other stuff which is incompatible with the headers from
the older driver. The older driver includes "brates.h" which shadows
the definitions from Linux, and is incompatible... */

/* RxBaud and TxBaud definitions... */
#define	RIO_B0			0x00	/* RTS / DTR signals dropped */
#define	RIO_B50			0x01	/* 50 baud */
#define	RIO_B75			0x02	/* 75 baud */
#define	RIO_B110		0x03	/* 110 baud */
#define	RIO_B134		0x04	/* 134.5 baud */
#define	RIO_B150		0x05	/* 150 baud */
#define	RIO_B200		0x06	/* 200 baud */
#define	RIO_B300		0x07	/* 300 baud */
#define	RIO_B600		0x08	/* 600 baud */
#define	RIO_B1200		0x09	/* 1200 baud */
#define	RIO_B1800		0x0A	/* 1800 baud */
#define	RIO_B2400		0x0B	/* 2400 baud */
#define	RIO_B4800		0x0C	/* 4800 baud */
#define	RIO_B9600		0x0D	/* 9600 baud */
#define	RIO_B19200		0x0E	/* 19200 baud */
#define	RIO_B38400		0x0F	/* 38400 baud */
#define	RIO_B56000		0x10	/* 56000 baud */
#define	RIO_B57600		0x11	/* 57600 baud */
#define	RIO_B64000		0x12	/* 64000 baud */
#define	RIO_B115200		0x13	/* 115200 baud */
#define	RIO_B2000		0x14	/* 2000 baud */
