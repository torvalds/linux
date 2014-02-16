/*
 * Copyright 2003 Digi International (www.digi.com)
 *      Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *      NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 */


/*
 * This structure holds data needed for the intelligent <--> nonintelligent
 * DPA translation
 */
 struct ni_info {
	int board;
	int channel;
	int dtr;
	int rts;
	int cts;
	int dsr;
	int ri;
	int dcd;
	int curtx;
	int currx;
	unsigned short iflag;
	unsigned short oflag;
	unsigned short cflag;
	unsigned short lflag;

	unsigned int mstat;
	unsigned char hflow;

	unsigned char xmit_stopped;
	unsigned char recv_stopped;

	unsigned int baud;
};

#define RW_READ		1
#define RW_WRITE        2
#define DIGI_KME        ('e'<<8) | 98           /* Read/Write Host */

#define SUBTYPE         0007
#define T_PCXI          0000
#define T_PCXEM         0001
#define T_PCXE          0002
#define T_PCXR          0003
#define T_SP            0004
#define T_SP_PLUS       0005

#define T_HERC   0000
#define T_HOU    0001
#define T_LON    0002
#define T_CHA    0003

#define T_NEO	 0000
#define T_NEO_EXPRESS  0001
#define T_CLASSIC 0002

#define FAMILY          0070
#define T_COMXI         0000
#define	T_NI		0000
#define T_PCXX          0010
#define T_CX            0020
#define T_EPC           0030
#define T_PCLITE        0040
#define T_SPXX          0050
#define T_AVXX          0060
#define T_DXB           0070
#define T_A2K_4_8       0070

#define BUSTYPE         0700
#define T_ISABUS        0000
#define T_MCBUS         0100
#define T_EISABUS       0200
#define T_PCIBUS        0400

/* Board State Definitions */

#define BD_RUNNING      0x0
#define BD_REASON       0x7f
#define BD_NOTFOUND     0x1
#define BD_NOIOPORT     0x2
#define BD_NOMEM        0x3
#define BD_NOBIOS       0x4
#define BD_NOFEP        0x5
#define BD_FAILED       0x6
#define BD_ALLOCATED    0x7
#define BD_TRIBOOT      0x8
#define BD_BADKME       0x80

#define DIGI_AIXON      0x0400          /* Aux flow control in fep */

/* Ioctls needed for dpa operation */

#define DIGI_GETDD      ('d'<<8) | 248          /* get driver info      */
#define DIGI_GETBD      ('d'<<8) | 249          /* get board info       */
#define DIGI_GET_NI_INFO ('d'<<8) | 250		/* nonintelligent state snfo */

/* Other special ioctls */
#define DIGI_TIMERIRQ ('d'<<8) | 251		/* Enable/disable RS_TIMER use */
#define DIGI_LOOPBACK ('d'<<8) | 252		/* Enable/disable UART internal loopback */
