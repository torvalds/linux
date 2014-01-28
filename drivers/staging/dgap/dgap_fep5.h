/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
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
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 *
 ************************************************************************ 
 ***	FEP Version 5 dependent definitions
 ************************************************************************/

#ifndef __DGAP_FEP5_H
#define __DGAP_FEP5_H

/************************************************************************
 *      FEP memory offsets
 ************************************************************************/
#define START           0x0004L         /* Execution start address      */

#define CMDBUF          0x0d10L         /* Command (cm_t) structure offset */
#define CMDSTART        0x0400L         /* Start of command buffer      */   
#define CMDMAX          0x0800L         /* End of command buffer        */

#define EVBUF           0x0d18L         /* Event (ev_t) structure       */
#define EVSTART         0x0800L         /* Start of event buffer        */
#define EVMAX           0x0c00L         /* End of event buffer          */
#define FEP5_PLUS       0x0E40          /* ASCII '5' and ASCII 'A' is here  */
#define ECS_SEG         0x0E44          /* Segment of the extended channel structure */
#define LINE_SPEED      0x10            /* Offset into ECS_SEG for line speed   */
                                        /* if the fep has extended capabilities */

/* BIOS MAGIC SPOTS */
#define ERROR           0x0C14L		/* BIOS error code              */
#define SEQUENCE	0x0C12L		/* BIOS sequence indicator      */
#define POSTAREA	0x0C00L		/* POST complete message area   */

/* FEP MAGIC SPOTS */
#define FEPSTAT         POSTAREA        /* OS here when FEP comes up    */
#define NCHAN           0x0C02L         /* number of ports FEP sees     */
#define PANIC           0x0C10L         /* PANIC area for FEP           */
#define KMEMEM          0x0C30L         /* Memory for KME use           */   
#define CONFIG          0x0CD0L         /* Concentrator configuration info */
#define CONFIGSIZE      0x0030          /* configuration info size      */
#define DOWNREQ         0x0D00          /* Download request buffer pointer */

#define CHANBUF         0x1000L         /* Async channel (bs_t) structs */
#define FEPOSSIZE       0x1FFF          /* 8K FEPOS                     */

#define XEMPORTS    0xC02	/*
				 * Offset in board memory where FEP5 stores
				 * how many ports it has detected.
				 * NOTE: FEP5 reports 64 ports when the user
				 * has the cable in EBI OUT instead of EBI IN.
				 */

#define FEPCLR      0x00
#define FEPMEM      0x02 
#define FEPRST      0x04 
#define FEPINT      0x08
#define FEPMASK     0x0e
#define FEPWIN      0x80

#define LOWMEM      0x0100
#define HIGHMEM     0x7f00

#define FEPTIMEOUT 200000

#define ENABLE_INTR		0x0e04		/* Enable interrupts flag */
#define FEPPOLL_MIN		1		/* minimum of 1 millisecond */  
#define FEPPOLL_MAX		20		/* maximum of 20 milliseconds */
#define FEPPOLL			0x0c26		/* Fep event poll interval */   

#define	IALTPIN			0x0080		/* Input flag to swap DSR <-> DCD */

/************************************************************************ 
 * Command structure definition.
 ************************************************************************/
struct cm_t {
	volatile unsigned short cm_head;	/* Command buffer head offset	*/
	volatile unsigned short cm_tail;	/* Command buffer tail offset	*/
	volatile unsigned short cm_start;	/* start offset of buffer	*/
	volatile unsigned short cm_max;		/* last offset of buffer	*/
};

/************************************************************************
 * Event structure definition.
 ************************************************************************/
struct ev_t {
	volatile unsigned short ev_head;	/* Command buffer head offset	*/
	volatile unsigned short ev_tail;	/* Command buffer tail offset	*/
	volatile unsigned short ev_start;	/* start offset of buffer	*/
	volatile unsigned short ev_max;		/* last offset of buffer	*/
};

/************************************************************************
 * Download buffer structure.
 ************************************************************************/
struct downld_t {
	uchar	dl_type;		/* Header                       */
	uchar	dl_seq;			/* Download sequence            */
	ushort	dl_srev;		/* Software revision number     */
	ushort	dl_lrev;		/* Low revision number          */
	ushort	dl_hrev;		/* High revision number         */
	ushort	dl_seg;			/* Start segment address        */
	ushort	dl_size;		/* Number of bytes to download  */
	uchar	dl_data[1024];		/* Download data                */
};

/************************************************************************ 
 * Per channel buffer structure
 ************************************************************************
 *              Base Structure Entries Usage Meanings to Host           * 
 *                                                                      * 
 *        W = read write        R = read only                           * 
 *        C = changed by commands only                                  * 
 *        U = unknown (may be changed w/o notice)                       *
 ************************************************************************/
struct bs_t {
	volatile unsigned short  tp_jmp;	/* Transmit poll jump		 */
	volatile unsigned short  tc_jmp;	/* Cooked procedure jump	 */
	volatile unsigned short  ri_jmp;	/* Not currently used		 */
	volatile unsigned short  rp_jmp;	/* Receive poll jump		 */

	volatile unsigned short  tx_seg;	/* W  Tx segment	 */
	volatile unsigned short  tx_head;	/* W  Tx buffer head offset	*/
	volatile unsigned short  tx_tail;	/* R  Tx buffer tail offset	*/
	volatile unsigned short  tx_max;	/* W  Tx buffer size - 1	 */
 
	volatile unsigned short  rx_seg;	/* W  Rx segment		*/
	volatile unsigned short  rx_head;	/* W  Rx buffer head offset	*/
	volatile unsigned short  rx_tail;	/* R  Rx buffer tail offset	*/
	volatile unsigned short  rx_max;	/* W  Rx buffer size - 1	 */

	volatile unsigned short  tx_lw;		/* W  Tx buffer low water mark  */
	volatile unsigned short  rx_lw;		/* W  Rx buffer low water mark  */
	volatile unsigned short  rx_hw;		/* W  Rx buffer high water mark */
	volatile unsigned short  incr;		/* W  Increment to next channel */

	volatile unsigned short  fepdev;	/* U  SCC device base address    */
	volatile unsigned short  edelay;	/* W  Exception delay            */
	volatile unsigned short  blen;		/* W  Break length              */
	volatile unsigned short  btime;		/* U  Break complete time       */

	volatile unsigned short  iflag;		/* C  UNIX input flags          */
	volatile unsigned short  oflag;		/* C  UNIX output flags         */
	volatile unsigned short  cflag;		/* C  UNIX control flags        */
	volatile unsigned short  wfill[13];	/* U  Reserved for expansion    */

	volatile unsigned char   num;		/* U  Channel number            */
	volatile unsigned char   ract;		/* U  Receiver active counter   */
	volatile unsigned char   bstat;		/* U  Break status bits         */
	volatile unsigned char   tbusy;		/* W  Transmit busy             */
	volatile unsigned char   iempty;	/* W  Transmit empty event enable */
	volatile unsigned char   ilow;		/* W  Transmit low-water event enable */
	volatile unsigned char   idata;		/* W  Receive data interrupt enable */
	volatile unsigned char   eflag;		/* U  Host event flags          */

	volatile unsigned char   tflag;		/* U  Transmit flags            */
	volatile unsigned char   rflag;		/* U  Receive flags             */
	volatile unsigned char   xmask;		/* U  Transmit ready flags      */
	volatile unsigned char   xval;		/* U  Transmit ready value      */
	volatile unsigned char   m_stat;	/* RC Modem status bits          */
	volatile unsigned char   m_change;	/* U  Modem bits which changed  */
	volatile unsigned char   m_int;		/* W  Modem interrupt enable bits */
	volatile unsigned char   m_last;	/* U  Last modem status         */

	volatile unsigned char   mtran;		/* C   Unreported modem trans   */
	volatile unsigned char   orun;		/* C   Buffer overrun occurred  */
	volatile unsigned char   astartc;	/* W   Auxiliary Xon char       */  
	volatile unsigned char   astopc;	/* W   Auxiliary Xoff char      */
	volatile unsigned char   startc;	/* W   Xon character             */
	volatile unsigned char   stopc;		/* W   Xoff character           */
	volatile unsigned char   vnextc;	/* W   Vnext character           */
	volatile unsigned char   hflow;		/* C   Software flow control    */

	volatile unsigned char   fillc;		/* U   Delay Fill character     */
	volatile unsigned char   ochar;		/* U   Saved output character   */
	volatile unsigned char   omask;		/* U   Output character mask    */

	volatile unsigned char   bfill[13];	/* U   Reserved for expansion   */  

	volatile unsigned char   scc[16];	/* U   SCC registers            */
};


/************************************************************************   
 * FEP supported functions
 ************************************************************************/
#define SRLOW		0xe0		/* Set receive low water	*/
#define SRHIGH		0xe1		/* Set receive high water	*/
#define FLUSHTX		0xe2		/* Flush transmit buffer	*/
#define PAUSETX		0xe3		/* Pause data transmission	*/
#define RESUMETX	0xe4		/* Resume data transmission	*/
#define SMINT		0xe5		/* Set Modem Interrupt		*/
#define SAFLOWC		0xe6		/* Set Aux. flow control chars	*/
#define SBREAK		0xe8		/* Send break			*/
#define SMODEM		0xe9		/* Set 8530 modem control lines	*/  
#define SIFLAG		0xea		/* Set UNIX iflags		*/
#define SFLOWC		0xeb		/* Set flow control characters	*/
#define STLOW		0xec		/* Set transmit low water mark	*/
#define RPAUSE		0xee		/* Pause receive		*/
#define RRESUME		0xef		/* Resume receive		*/  
#define CHRESET		0xf0		/* Reset Channel		*/
#define BUFSETALL	0xf2		/* Set Tx & Rx buffer size avail*/
#define SOFLAG		0xf3		/* Set UNIX oflags		*/
#define SHFLOW		0xf4		/* Set hardware handshake	*/
#define SCFLAG		0xf5		/* Set UNIX cflags		*/
#define SVNEXT		0xf6		/* Set VNEXT character		*/
#define SPINTFC		0xfc		/* Reserved			*/
#define SCOMMODE	0xfd		/* Set RS232/422 mode		*/


/************************************************************************ 
 *	Modes for SCOMMODE
 ************************************************************************/
#define MODE_232	0x00
#define MODE_422	0x01


/************************************************************************ 
 *      Event flags.
 ************************************************************************/
#define IFBREAK         0x01            /* Break received               */  
#define IFTLW           0x02            /* Transmit low water           */
#define IFTEM           0x04            /* Transmitter empty            */
#define IFDATA          0x08            /* Receive data present         */
#define IFMODEM         0x20            /* Modem status change          */

/************************************************************************   
 *      Modem flags
 ************************************************************************/
#       define  DM_RTS          0x02    /* Request to send              */
#       define  DM_CD           0x80    /* Carrier detect               */
#       define  DM_DSR          0x20    /* Data set ready               */
#       define  DM_CTS          0x10    /* Clear to send                */
#       define  DM_RI           0x40    /* Ring indicator               */
#       define  DM_DTR          0x01    /* Data terminal ready          */


#endif
