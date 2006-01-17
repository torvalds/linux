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
#ifndef lint
/* static char* _cirrus_h_sccs = "@(#)cirrus.h	1.16"; */
#endif
#define _cirrus_h 1

#ifdef RTA
#define	TO_UART	RX
#define TO_DRIVER TX
#endif

#ifdef HOST
#define	TO_UART	TX
#define TO_DRIVER RX
#endif
#ifdef RTA
/* Miscellaneous defines for CIRRUS addresses and related logic for
   interrupts etc.
*/
#define	MAP(a)		((short *)(cirrus_base + (a)))
#define outp(a,b)	(*MAP (a) =(b))
#define inp(a)		((*MAP (a)) & 0xff)
#define	CIRRUS_FIRST	(short*)0x7300
#define	CIRRUS_SECOND	(short*)0x7200
#define	CIRRUS_THIRD	(short*)0x7100
#define	CIRRUS_FOURTH	(short*)0x7000
#define	PORTS_ON_CIRRUS	4
#define	CIRRUS_FIFO_SIZE	12
#define	SPACE		0x20
#define	TAB		0x09
#define	LINE_FEED	0x0a
#define	CARRIAGE_RETURN	0x0d
#define	BACKSPACE	0x08
#define	SPACES_IN_TABS	8
#define	SEND_ESCAPE	0x00
#define START_BREAK	0x81
#define	TIMER_TICK	0x82
#define STOP_BREAK	0x83
#define BASE(a) ((a) < 4 ? (short*)CIRRUS_FIRST : ((a) < 8 ? (short *)CIRRUS_SECOND : ((a) < 12 ? (short*)CIRRUS_THIRD : (short *)CIRRUS_FOURTH)))
#define txack1	((short *)0x7104)
#define rxack1	((short *)0x7102)
#define mdack1  ((short *)0x7106)
#define txack2  ((short *)0x7006)
#define rxack2	((short *)0x7004)
#define mdack2  ((short *)0x7100)
#define int_latch       ((short *) 0x7800)
#define int_status      ((short *) 0x7c00)
#define tx1_pending     0x20
#define rx1_pending     0x10
#define md1_pending     0x40
#define tx2_pending     0x02
#define rx2_pending     0x01
#define md2_pending     0x40
#define module1_bits	0x07
#define module1_modern	0x08
#define module2_bits	0x70
#define module2_modern	0x80
#define module_blank	0xf
#define rs232_d25	0x0
#define	rs232_rj45	0x1
#define rs422_d25	0x3
#define parallel	0x5

#define	CLK0	0x00
#define CLK1	0x01
#define CLK2	0x02
#define CLK3	0x03
#define CLK4	0x04

#define CIRRUS_REVC    0x42
#define CIRRUS_REVE    0x44

#define	TURNON	1
#define TURNOFF 0

/* The list of CIRRUS registers. 
   NB. These registers are relative values on 8 bit boundaries whereas
   on the RTA's the CIRRUS registers are on word boundaries. Use pointer
   arithmetic (short *) to obtain the real addresses required */
#define ccr	0x05		/* Channel Command Register     */
#define ier	0x06		/* Interrupt Enable Register    */
#define cor1	0x08		/* Channel Option Register 1    */
#define cor2	0x09		/* Channel Option Register 2    */
#define cor3	0x0a		/* Channel Option Register 3    */
#define cor4	0x1e		/* Channel Option Register 4    */
#define	cor5	0x1f		/* Channel Option Register 5    */

#define ccsr	0x0b		/* Channel Control Status Register */
#define rdcr	0x0e		/* Receive Data Count Register  */
#define tdcr	0x12		/* Transmit Data Count Register */
#define mcor1	0x15		/* Modem Change Option Register 1 */
#define mcor2	0x16		/* Modem Change Option Regsiter 2 */

#define livr	0x18		/* Local Interrupt Vector Register */
#define schr1	0x1a		/* Special Character Register 1 */
#define schr2	0x1b		/* Special Character Register 2 */
#define schr3	0x1c		/* Special Character Register 3 */
#define schr4	0x1d		/* Special Character Register 4 */

#define rtr	0x20		/* Receive Timer Register */
#define rtpr	0x21		/* Receive Timeout Period Register */
#define lnc	0x24		/* Lnext character */

#define rivr	0x43		/* Receive Interrupt Vector Register    */
#define tivr	0x42		/* Transmit Interrupt Vector Register   */
#define mivr	0x41		/* Modem Interrupt Vector Register      */
#define gfrcr	0x40		/* Global Firmware Revision code Reg    */
#define ricr	0x44		/* Receive Interrupting Channel Reg     */
#define ticr	0x45		/* Transmit Interrupting Channel Reg    */
#define micr	0x46		/* Modem Interrupting Channel Register  */

#define gcr	0x4b		/* Global configuration register */
#define misr    0x4c		/* Modem interrupt status register */

#define rbusr	0x59
#define tbusr	0x5a
#define mbusr	0x5b

#define eoir	0x60		/* End Of Interrupt Register */
#define rdsr	0x62		/* Receive Data / Status Register */
#define tdr	0x63		/* Transmit Data Register */
#define svrr	0x67		/* Service Request Register */

#define car	0x68		/* Channel Access Register */
#define mir	0x69		/* Modem Interrupt Register */
#define tir	0x6a		/* Transmit Interrupt Register */
#define rir	0x6b		/* Receive Interrupt Register */
#define msvr1	0x6c		/* Modem Signal Value Register 1 */
#define msvr2	0x6d		/* Modem Signal Value Register 2 */
#define psvr	0x6f		/* Printer Signal Value Register */

#define tbpr	0x72		/* Transmit Baud Rate Period Register */
#define tcor	0x76		/* Transmit Clock Option Register */

#define rbpr	0x78		/* Receive Baud Rate Period Register */
#define rber	0x7a		/* Receive Baud Rate Extension Register */
#define rcor	0x7c		/* Receive Clock Option Register */
#define ppr	0x7e		/* Prescalar Period Register    */

/* Misc registers used for forcing the 1400 out of its reset woes */
#define airl	0x6d
#define airm	0x6e
#define airh	0x6f
#define btcr	0x66
#define mtcr	0x6c
#define tber	0x74

#endif				/* #ifdef RTA */


/* Bit fields for particular registers */

/* GCR */
#define GCR_SERIAL	0x00	/* Configure as serial channel */
#define GCR_PARALLEL	0x80	/* Configure as parallel channel */

/* RDSR - when status read from FIFO */
#define	RDSR_BREAK		0x08	/* Break received */
#define RDSR_TIMEOUT    	0x80	/* No new data timeout */
#define RDSR_SC1  	  	0x10	/* Special char 1 (tx XON) matched */
#define RDSR_SC2  	  	0x20	/* Special char 2 (tx XOFF) matched */
#define RDSR_SC12_MASK	  	0x30	/* Mask for special chars 1 and 2 */

/* PPR */
#define PPR_DEFAULT	0x31	/* Default value - for a 25Mhz clock gives
				   a timeout period of 1ms */

/* LIVR */
#define	LIVR_EXCEPTION	0x07	/* Receive exception interrupt */

/* CCR */
#define	CCR_RESET	0x80	/* Reset channel */
#define	CCR_CHANGE	0x4e	/* COR's have changed - NB always change all
				   COR's */
#define	CCR_WFLUSH	0x82	/* Flush transmit FIFO and TSR / THR */

#define	CCR_SENDSC1	0x21	/* Send special character one */
#define CCR_SENDSC2	0x22	/* Send special character two */
#define CCR_SENDSC3	0x23	/* Send special character three */
#define CCR_SENDSC4	0x24	/* Send special character four */

#define CCR_TENABLE	0x18	/* Enable transmitter */
#define	CCR_TDISABLE	0x14	/* Disable transmitter */
#define CCR_RENABLE	0x12	/* Enable receiver */
#define CCR_RDISABLE	0x11	/* Disable receiver */

#define	CCR_READY	0x00	/* CCR is ready for another command */

/* CCSR */
#define CCSR_TXENABLE	0x08	/* Transmitter enable */
#define CCSR_RXENABLE	0x80	/* Receiver enable */
#define CCSR_TXFLOWOFF	0x04	/* Transmit flow off */
#define CCSR_TXFLOWON	0x02	/* Transmit flow on */

/* SVRR */
#define	SVRR_RECEIVE	0x01	/* Receive interrupt pending */
#define	SVRR_TRANSMIT	0x02	/* Transmit interrupt pending */
#define	SVRR_MODEM	0x04	/* Modem interrupt pending */

/* CAR */
#define CAR_PORTS	0x03	/* Bit fields for ports */

/* IER */
#define	IER_MODEM	0x80	/* Change in modem status */
#define	IER_RECEIVE	0x10	/* Good data / data exception */
#define IER_TRANSMITR	0x04	/* Transmit ready (FIFO empty) */
#define	IER_TRANSMITE	0x02	/* Transmit empty */
#define IER_TIMEOUT	0x01	/* Timeout on no data */

#define	IER_DEFAULT	0x94	/* Default values */
#define IER_PARALLEL    0x84	/* Default for Parallel */
#define	IER_EMPTY	0x92	/* Transmitter empty rather than ready */

/* COR1 - Driver only */
#define	COR1_INPCK	0x10	/* Check parity of received characters */

/* COR1 - driver and RTA */
#define	COR1_ODD	0x80	/* Odd parity */
#define COR1_EVEN	0x00	/* Even parity */
#define	COR1_NOP	0x00	/* No parity */
#define	COR1_FORCE	0x20	/* Force parity */
#define	COR1_NORMAL	0x40	/* With parity */
#define	COR1_1STOP	0x00	/* 1 stop bit */
#define	COR1_15STOP	0x04	/* 1.5 stop bits */
#define	COR1_2STOP	0x08	/* 2 stop bits */
#define	COR1_5BITS	0x00	/* 5 data bits */
#define	COR1_6BITS	0x01	/* 6 data bits */
#define	COR1_7BITS	0x02	/* 7 data bits */
#define	COR1_8BITS	0x03	/* 8 data bits */

#define COR1_HOST       0xef	/* Safe host bits */

/* RTA only */
#define COR1_CINPCK     0x00	/* Check parity of received characters */
#define COR1_CNINPCK    0x10	/* Don't check parity */

/* COR2 bits for both RTA and driver use */
#define	COR2_IXANY	0x80	/* IXANY - any character is XON */
#define	COR2_IXON	0x40	/* IXON - enable tx soft flowcontrol */
#define	COR2_RTSFLOW	0x02	/* Enable tx hardware flow control */

/* Additional driver bits */
#define	COR2_HUPCL	0x20	/* Hang up on close */
#define	COR2_CTSFLOW	0x04	/* Enable rx hardware flow control */
#define	COR2_IXOFF	0x01	/* Enable rx software flow control */
#define COR2_DTRFLOW	0x08	/* Enable tx hardware flow control */

/* RTA use only */
#define COR2_ETC	0x20	/* Embedded transmit options */
#define	COR2_LOCAL	0x10	/* Local loopback mode */
#define	COR2_REMOTE	0x08	/* Remote loopback mode */
#define	COR2_HOST	0xc2	/* Safe host bits */

/* COR3 - RTA use only */
#define	COR3_SCDRNG	0x80	/* Enable special char detect for range */
#define	COR3_SCD34	0x40	/* Special character detect for SCHR's 3 + 4 */
#define	COR3_FCT	0x20	/* Flow control transparency */
#define	COR3_SCD12	0x10	/* Special character detect for SCHR's 1 + 2 */
#define	COR3_FIFO12	0x0c	/* 12 chars for receive FIFO threshold */
#define COR3_FIFO10     0x0a	/* 10 chars for receive FIFO threshold */
#define COR3_FIFO8      0x08	/* 8 chars for receive FIFO threshold */
#define COR3_FIFO6      0x06	/* 6 chars for receive FIFO threshold */

#define COR3_THRESHOLD  COR3_FIFO8	/* MUST BE LESS THAN MCOR_THRESHOLD */

#define	COR3_DEFAULT	(COR3_FCT | COR3_THRESHOLD)
				/* Default bits for COR3 */

/* COR4 driver and RTA use */
#define	COR4_IGNCR	0x80	/* Throw away CR's on input */
#define	COR4_ICRNL	0x40	/* Map CR -> NL on input */
#define	COR4_INLCR	0x20	/* Map NL -> CR on input */
#define	COR4_IGNBRK	0x10	/* Ignore Break */
#define	COR4_NBRKINT	0x08	/* No interrupt on break (-BRKINT) */
#define COR4_RAISEMOD	0x01	/* Raise modem output lines on non-zero baud */


/* COR4 driver only */
#define COR4_IGNPAR	0x04	/* IGNPAR (ignore characters with errors) */
#define COR4_PARMRK	0x02	/* PARMRK */

#define COR4_HOST	0xf8	/* Safe host bits */

/* COR4 RTA only */
#define COR4_CIGNPAR	0x02	/* Thrown away bad characters */
#define COR4_CPARMRK	0x04	/* PARMRK characters */
#define COR4_CNPARMRK	0x03	/* Don't PARMRK */

/* COR5 driver and RTA use */
#define	COR5_ISTRIP	0x80	/* Strip input chars to 7 bits */
#define	COR5_LNE	0x40	/* Enable LNEXT processing */
#define	COR5_CMOE	0x20	/* Match good and errored characters */
#define	COR5_ONLCR	0x02	/* NL -> CR NL on output */
#define	COR5_OCRNL	0x01	/* CR -> NL on output */

/*
** Spare bits - these are not used in the CIRRUS registers, so we use
** them to set various other features.
*/
/*
** tstop and tbusy indication
*/
#define	COR5_TSTATE_ON	0x08	/* Turn on monitoring of tbusy and tstop */
#define	COR5_TSTATE_OFF	0x04	/* Turn off monitoring of tbusy and tstop */
/*
** TAB3
*/
#define	COR5_TAB3	0x10	/* TAB3 mode */

#define	COR5_HOST	0xc3	/* Safe host bits */

/* CCSR */
#define	CCSR_TXFLOFF	0x04	/* Tx is xoffed */

/* MSVR1 */
/* NB. DTR / CD swapped from Cirrus spec as the pins are also reversed on the
   RTA. This is because otherwise DCD would get lost on the 1 parallel / 3
   serial option.
*/
#define	MSVR1_CD	0x80	/* CD (DSR on Cirrus) */
#define	MSVR1_RTS	0x40	/* RTS (CTS on Cirrus) */
#define	MSVR1_RI	0x20	/* RI */
#define	MSVR1_DTR	0x10	/* DTR (CD on Cirrus) */
#define	MSVR1_CTS	0x01	/* CTS output pin (RTS on Cirrus) */
/* Next two used to indicate state of tbusy and tstop to driver */
#define	MSVR1_TSTOP	0x08	/* Set if port flow controlled */
#define	MSVR1_TEMPTY	0x04	/* Set if port tx buffer empty */

#define	MSVR1_HOST	0xf3	/* The bits the host wants */

/* MSVR2 */
#define	MSVR2_DSR	0x02	/* DSR output pin (DTR on Cirrus) */

/* MCOR */
#define	MCOR_CD	        0x80	/* CD (DSR on Cirrus) */
#define	MCOR_RTS	0x40	/* RTS (CTS on Cirrus) */
#define	MCOR_RI	        0x20	/* RI */
#define	MCOR_DTR	0x10	/* DTR (CD on Cirrus) */

#define MCOR_DEFAULT    (MCOR_CD | MCOR_RTS | MCOR_RI | MCOR_DTR)
#define MCOR_FULLMODEM  MCOR_DEFAULT
#define MCOR_RJ45       (MCOR_CD | MCOR_RTS | MCOR_DTR)
#define MCOR_RESTRICTED (MCOR_CD | MCOR_RTS)

/* More MCOR - H/W Handshake (flowcontrol) stuff */
#define	MCOR_THRESH8	0x08	/* eight characters then we stop */
#define	MCOR_THRESH9	0x09	/* nine characters then we stop */
#define	MCOR_THRESH10	0x0A	/* ten characters then we stop */
#define	MCOR_THRESH11	0x0B	/* eleven characters then we stop */

#define	MCOR_THRESHBITS 0x0F	/* mask for ANDing out the above */

#define	MCOR_THRESHOLD	MCOR_THRESH9	/* MUST BE GREATER THAN COR3_THRESHOLD */


/* RTPR */
#define RTPR_DEFAULT	0x02	/* Default */


/* Defines for the subscripts of a CONFIG packet */
#define	CONFIG_COR1	1	/* Option register 1 */
#define	CONFIG_COR2	2	/* Option register 2 */
#define	CONFIG_COR4	3	/* Option register 4 */
#define	CONFIG_COR5	4	/* Option register 5 */
#define	CONFIG_TXXON	5	/* Tx XON character */
#define	CONFIG_TXXOFF	6	/* Tx XOFF character */
#define	CONFIG_RXXON	7	/* Rx XON character */
#define	CONFIG_RXXOFF	8	/* Rx XOFF character */
#define CONFIG_LNEXT	9	/* LNEXT character */
#define	CONFIG_TXBAUD	10	/* Tx baud rate */
#define	CONFIG_RXBAUD	11	/* Rx baud rate */

/* Port status stuff */
#define	IDLE_CLOSED	0	/* Closed */
#define IDLE_OPEN	1	/* Idle open */
#define IDLE_BREAK	2	/* Idle on break */

/* Subscript of MODEM STATUS packet */
#define	MODEM_VALUE	3	/* Current values of handshake pins */
/* Subscript of SBREAK packet */
#define BREAK_LENGTH	1	/* Length of a break in slices of 0.01 seconds
				   0 = stay on break until an EBREAK command
				   is sent */


#define	PRE_EMPTIVE	0x80	/* Pre-emptive bit in command field */

/* Packet types going from Host to remote - with the exception of OPEN, MOPEN,
   CONFIG, SBREAK and MEMDUMP the remaining bytes of the data array will not
   be used 
*/
#define	OPEN		0x00	/* Open a port */
#define CONFIG		0x01	/* Configure a port */
#define	MOPEN		0x02	/* Modem open (block for DCD) */
#define	CLOSE		0x03	/* Close a port */
#define	WFLUSH		(0x04 | PRE_EMPTIVE)	/* Write flush */
#define	RFLUSH		(0x05 | PRE_EMPTIVE)	/* Read flush */
#define	RESUME		(0x06 | PRE_EMPTIVE)	/* Resume if xoffed */
#define	SBREAK		0x07	/* Start break */
#define	EBREAK		0x08	/* End break */
#define	SUSPEND		(0x09 | PRE_EMPTIVE)	/* Susp op (behave as tho xoffed) */
#define FCLOSE          (0x0a | PRE_EMPTIVE)	/* Force close */
#define XPRINT          0x0b	/* Xprint packet */
#define MBIS		(0x0c | PRE_EMPTIVE)	/* Set modem lines */
#define MBIC		(0x0d | PRE_EMPTIVE)	/* Clear modem lines */
#define MSET		(0x0e | PRE_EMPTIVE)	/* Set modem lines */
#define PCLOSE		0x0f	/* Pseudo close - Leaves rx/tx enabled */
#define MGET		(0x10 | PRE_EMPTIVE)	/* Force update of modem status */
#define MEMDUMP		(0x11 | PRE_EMPTIVE)	/* Send back mem from addr supplied */
#define	READ_REGISTER	(0x12 | PRE_EMPTIVE)	/* Read CD1400 register (debug) */

/* "Command" packets going from remote to host COMPLETE and MODEM_STATUS
   use data[4] / data[3] to indicate current state and modem status respectively
*/

#define	COMPLETE	(0x20 | PRE_EMPTIVE)
				/* Command complete */
#define BREAK_RECEIVED	(0x21 | PRE_EMPTIVE)
				/* Break received */
#define MODEM_STATUS	(0x22 | PRE_EMPTIVE)
				/* Change in modem status */

/* "Command" packet that could go either way - handshake wake-up */
#define HANDSHAKE	(0x23 | PRE_EMPTIVE)
				/* Wake-up to HOST / RTA */

#endif
