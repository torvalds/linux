/****************************************************************************
 *******                                                              *******
 *******		CIRRUS.H				      *******
 *******                                                              *******
 ****************************************************************************

 Author  : Jeremy Rolls
 Date    : 3 Aug 1990

 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
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

 Version : 0.01


                            Mods
 ----------------------------------------------------------------------------
  Date     By                Description
 ----------------------------------------------------------------------------

 ***************************************************************************/

#ifndef _cirrus_h
#define _cirrus_h 1

/* Bit fields for particular registers shared with driver */

/* COR1 - driver and RTA */
#define RIOC_COR1_ODD		0x80	/* Odd parity */
#define RIOC_COR1_EVEN		0x00	/* Even parity */
#define RIOC_COR1_NOP		0x00	/* No parity */
#define RIOC_COR1_FORCE		0x20	/* Force parity */
#define RIOC_COR1_NORMAL	0x40	/* With parity */
#define RIOC_COR1_1STOP		0x00	/* 1 stop bit */
#define RIOC_COR1_15STOP	0x04	/* 1.5 stop bits */
#define RIOC_COR1_2STOP		0x08	/* 2 stop bits */
#define RIOC_COR1_5BITS		0x00	/* 5 data bits */
#define RIOC_COR1_6BITS		0x01	/* 6 data bits */
#define RIOC_COR1_7BITS		0x02	/* 7 data bits */
#define RIOC_COR1_8BITS		0x03	/* 8 data bits */

#define RIOC_COR1_HOST		0xef	/* Safe host bits */

/* RTA only */
#define RIOC_COR1_CINPCK	0x00	/* Check parity of received characters */
#define RIOC_COR1_CNINPCK	0x10	/* Don't check parity */

/* COR2 bits for both RTA and driver use */
#define RIOC_COR2_IXANY		0x80	/* IXANY - any character is XON */
#define RIOC_COR2_IXON		0x40	/* IXON - enable tx soft flowcontrol */
#define RIOC_COR2_RTSFLOW	0x02	/* Enable tx hardware flow control */

/* Additional driver bits */
#define RIOC_COR2_HUPCL		0x20	/* Hang up on close */
#define RIOC_COR2_CTSFLOW	0x04	/* Enable rx hardware flow control */
#define RIOC_COR2_IXOFF		0x01	/* Enable rx software flow control */
#define RIOC_COR2_DTRFLOW	0x08	/* Enable tx hardware flow control */

/* RTA use only */
#define RIOC_COR2_ETC		0x20	/* Embedded transmit options */
#define RIOC_COR2_LOCAL		0x10	/* Local loopback mode */
#define RIOC_COR2_REMOTE	0x08	/* Remote loopback mode */
#define RIOC_COR2_HOST		0xc2	/* Safe host bits */

/* COR3 - RTA use only */
#define RIOC_COR3_SCDRNG	0x80	/* Enable special char detect for range */
#define RIOC_COR3_SCD34		0x40	/* Special character detect for SCHR's 3 + 4 */
#define RIOC_COR3_FCT		0x20	/* Flow control transparency */
#define RIOC_COR3_SCD12		0x10	/* Special character detect for SCHR's 1 + 2 */
#define RIOC_COR3_FIFO12	0x0c	/* 12 chars for receive FIFO threshold */
#define RIOC_COR3_FIFO10	0x0a	/* 10 chars for receive FIFO threshold */
#define RIOC_COR3_FIFO8		0x08	/* 8 chars for receive FIFO threshold */
#define RIOC_COR3_FIFO6		0x06	/* 6 chars for receive FIFO threshold */

#define RIOC_COR3_THRESHOLD	RIOC_COR3_FIFO8	/* MUST BE LESS THAN MCOR_THRESHOLD */

#define RIOC_COR3_DEFAULT	(RIOC_COR3_FCT | RIOC_COR3_THRESHOLD)
				/* Default bits for COR3 */

/* COR4 driver and RTA use */
#define RIOC_COR4_IGNCR		0x80	/* Throw away CR's on input */
#define RIOC_COR4_ICRNL		0x40	/* Map CR -> NL on input */
#define RIOC_COR4_INLCR		0x20	/* Map NL -> CR on input */
#define RIOC_COR4_IGNBRK	0x10	/* Ignore Break */
#define RIOC_COR4_NBRKINT	0x08	/* No interrupt on break (-BRKINT) */
#define RIOC_COR4_RAISEMOD	0x01	/* Raise modem output lines on non-zero baud */


/* COR4 driver only */
#define RIOC_COR4_IGNPAR	0x04	/* IGNPAR (ignore characters with errors) */
#define RIOC_COR4_PARMRK	0x02	/* PARMRK */

#define RIOC_COR4_HOST		0xf8	/* Safe host bits */

/* COR4 RTA only */
#define RIOC_COR4_CIGNPAR	0x02	/* Thrown away bad characters */
#define RIOC_COR4_CPARMRK	0x04	/* PARMRK characters */
#define RIOC_COR4_CNPARMRK	0x03	/* Don't PARMRK */

/* COR5 driver and RTA use */
#define RIOC_COR5_ISTRIP	0x80	/* Strip input chars to 7 bits */
#define RIOC_COR5_LNE		0x40	/* Enable LNEXT processing */
#define RIOC_COR5_CMOE		0x20	/* Match good and errored characters */
#define RIOC_COR5_ONLCR		0x02	/* NL -> CR NL on output */
#define RIOC_COR5_OCRNL		0x01	/* CR -> NL on output */

/*
** Spare bits - these are not used in the CIRRUS registers, so we use
** them to set various other features.
*/
/*
** tstop and tbusy indication
*/
#define RIOC_COR5_TSTATE_ON	0x08	/* Turn on monitoring of tbusy and tstop */
#define RIOC_COR5_TSTATE_OFF	0x04	/* Turn off monitoring of tbusy and tstop */
/*
** TAB3
*/
#define RIOC_COR5_TAB3		0x10	/* TAB3 mode */

#define RIOC_COR5_HOST		0xc3	/* Safe host bits */

/* CCSR */
#define RIOC_CCSR_TXFLOFF	0x04	/* Tx is xoffed */

/* MSVR1 */
/* NB. DTR / CD swapped from Cirrus spec as the pins are also reversed on the
   RTA. This is because otherwise DCD would get lost on the 1 parallel / 3
   serial option.
*/
#define RIOC_MSVR1_CD		0x80	/* CD (DSR on Cirrus) */
#define RIOC_MSVR1_RTS		0x40	/* RTS (CTS on Cirrus) */
#define RIOC_MSVR1_RI		0x20	/* RI */
#define RIOC_MSVR1_DTR		0x10	/* DTR (CD on Cirrus) */
#define RIOC_MSVR1_CTS		0x01	/* CTS output pin (RTS on Cirrus) */
/* Next two used to indicate state of tbusy and tstop to driver */
#define RIOC_MSVR1_TSTOP	0x08	/* Set if port flow controlled */
#define RIOC_MSVR1_TEMPTY	0x04	/* Set if port tx buffer empty */

#define RIOC_MSVR1_HOST		0xf3	/* The bits the host wants */

/* Defines for the subscripts of a CONFIG packet */
#define RIOC_CONFIG_COR1	1	/* Option register 1 */
#define RIOC_CONFIG_COR2	2	/* Option register 2 */
#define RIOC_CONFIG_COR4	3	/* Option register 4 */
#define RIOC_CONFIG_COR5	4	/* Option register 5 */
#define RIOC_CONFIG_TXXON	5	/* Tx XON character */
#define RIOC_CONFIG_TXXOFF	6	/* Tx XOFF character */
#define RIOC_CONFIG_RXXON	7	/* Rx XON character */
#define RIOC_CONFIG_RXXOFF	8	/* Rx XOFF character */
#define RIOC_CONFIG_LNEXT	9	/* LNEXT character */
#define RIOC_CONFIG_TXBAUD	10	/* Tx baud rate */
#define RIOC_CONFIG_RXBAUD	11	/* Rx baud rate */

#define RIOC_PRE_EMPTIVE	0x80	/* Pre-emptive bit in command field */

/* Packet types going from Host to remote - with the exception of OPEN, MOPEN,
   CONFIG, SBREAK and MEMDUMP the remaining bytes of the data array will not
   be used 
*/
#define RIOC_OPEN		0x00	/* Open a port */
#define RIOC_CONFIG		0x01	/* Configure a port */
#define RIOC_MOPEN		0x02	/* Modem open (block for DCD) */
#define RIOC_CLOSE		0x03	/* Close a port */
#define RIOC_WFLUSH		(0x04 | RIOC_PRE_EMPTIVE)	/* Write flush */
#define RIOC_RFLUSH		(0x05 | RIOC_PRE_EMPTIVE)	/* Read flush */
#define RIOC_RESUME		(0x06 | RIOC_PRE_EMPTIVE)	/* Resume if xoffed */
#define RIOC_SBREAK		0x07	/* Start break */
#define RIOC_EBREAK		0x08	/* End break */
#define RIOC_SUSPEND		(0x09 | RIOC_PRE_EMPTIVE)	/* Susp op (behave as tho xoffed) */
#define RIOC_FCLOSE		(0x0a | RIOC_PRE_EMPTIVE)	/* Force close */
#define RIOC_XPRINT		0x0b	/* Xprint packet */
#define RIOC_MBIS		(0x0c | RIOC_PRE_EMPTIVE)	/* Set modem lines */
#define RIOC_MBIC		(0x0d | RIOC_PRE_EMPTIVE)	/* Clear modem lines */
#define RIOC_MSET		(0x0e | RIOC_PRE_EMPTIVE)	/* Set modem lines */
#define RIOC_PCLOSE		0x0f	/* Pseudo close - Leaves rx/tx enabled */
#define RIOC_MGET		(0x10 | RIOC_PRE_EMPTIVE)	/* Force update of modem status */
#define RIOC_MEMDUMP		(0x11 | RIOC_PRE_EMPTIVE)	/* Send back mem from addr supplied */
#define RIOC_READ_REGISTER	(0x12 | RIOC_PRE_EMPTIVE)	/* Read CD1400 register (debug) */

/* "Command" packets going from remote to host COMPLETE and MODEM_STATUS
   use data[4] / data[3] to indicate current state and modem status respectively
*/

#define RIOC_COMPLETE		(0x20 | RIOC_PRE_EMPTIVE)
				/* Command complete */
#define RIOC_BREAK_RECEIVED	(0x21 | RIOC_PRE_EMPTIVE)
				/* Break received */
#define RIOC_MODEM_STATUS	(0x22 | RIOC_PRE_EMPTIVE)
				/* Change in modem status */

/* "Command" packet that could go either way - handshake wake-up */
#define RIOC_HANDSHAKE		(0x23 | RIOC_PRE_EMPTIVE)
				/* Wake-up to HOST / RTA */

#endif
