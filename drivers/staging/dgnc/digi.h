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
 */

#ifndef __DIGI_H
#define __DIGI_H

/************************************************************************
 ***	Definitions for Digi ditty(1) command.
 ************************************************************************/


/*
 * Copyright (c) 1988-96 Digi International Inc., All Rights Reserved.
 */

/************************************************************************
 * This module provides application access to special Digi
 * serial line enhancements which are not standard UNIX(tm) features.
 ************************************************************************/

#if !defined(TIOCMODG)

#define	TIOCMODG	('d'<<8) | 250		/* get modem ctrl state	*/
#define	TIOCMODS	('d'<<8) | 251		/* set modem ctrl state	*/

#ifndef TIOCM_LE
#define		TIOCM_LE	0x01		/* line enable		*/
#define		TIOCM_DTR	0x02		/* data terminal ready	*/
#define		TIOCM_RTS	0x04		/* request to send	*/
#define		TIOCM_ST	0x08		/* secondary transmit	*/
#define		TIOCM_SR	0x10		/* secondary receive	*/
#define		TIOCM_CTS	0x20		/* clear to send	*/
#define		TIOCM_CAR	0x40		/* carrier detect	*/
#define		TIOCM_RNG	0x80		/* ring	indicator	*/
#define		TIOCM_DSR	0x100		/* data set ready	*/
#define		TIOCM_RI	TIOCM_RNG	/* ring (alternate)	*/
#define		TIOCM_CD	TIOCM_CAR	/* carrier detect (alt)	*/
#endif

#endif

#if !defined(TIOCMSET)
#define	TIOCMSET	('d'<<8) | 252		/* set modem ctrl state	*/
#define	TIOCMGET	('d'<<8) | 253		/* set modem ctrl state	*/
#endif

#if !defined(TIOCMBIC)
#define	TIOCMBIC	('d'<<8) | 254		/* set modem ctrl state */
#define	TIOCMBIS	('d'<<8) | 255		/* set modem ctrl state */
#endif


#if !defined(TIOCSDTR)
#define	TIOCSDTR	('e'<<8) | 0		/* set DTR		*/
#define	TIOCCDTR	('e'<<8) | 1		/* clear DTR		*/
#endif

/************************************************************************
 * Ioctl command arguments for DIGI parameters.
 ************************************************************************/
#define DIGI_GETA	('e'<<8) | 94		/* Read params		*/

#define DIGI_SETA	('e'<<8) | 95		/* Set params		*/
#define DIGI_SETAW	('e'<<8) | 96		/* Drain & set params	*/
#define DIGI_SETAF	('e'<<8) | 97		/* Drain, flush & set params */

#define DIGI_KME	('e'<<8) | 98		/* Read/Write Host	*/
						/* Adapter Memory	*/

#define	DIGI_GETFLOW	('e'<<8) | 99		/* Get startc/stopc flow */
						/* control characters 	 */
#define	DIGI_SETFLOW	('e'<<8) | 100		/* Set startc/stopc flow */
						/* control characters	 */
#define	DIGI_GETAFLOW	('e'<<8) | 101		/* Get Aux. startc/stopc */
						/* flow control chars 	 */
#define	DIGI_SETAFLOW	('e'<<8) | 102		/* Set Aux. startc/stopc */
						/* flow control chars	 */

#define DIGI_GEDELAY	('d'<<8) | 246		/* Get edelay */
#define DIGI_SEDELAY	('d'<<8) | 247		/* Set edelay */

struct	digiflow_t {
	unsigned char	startc;				/* flow cntl start char	*/
	unsigned char	stopc;				/* flow cntl stop char	*/
};


#ifdef	FLOW_2200
#define	F2200_GETA	('e'<<8) | 104		/* Get 2x36 flow cntl flags */
#define	F2200_SETAW	('e'<<8) | 105		/* Set 2x36 flow cntl flags */
#define		F2200_MASK	0x03		/* 2200 flow cntl bit mask  */
#define		FCNTL_2200	0x01		/* 2x36 terminal flow cntl  */
#define		PCNTL_2200	0x02		/* 2x36 printer flow cntl   */
#define	F2200_XON	0xf8
#define	P2200_XON	0xf9
#define	F2200_XOFF	0xfa
#define	P2200_XOFF	0xfb

#define	FXOFF_MASK	0x03			/* 2200 flow status mask    */
#define	RCVD_FXOFF	0x01			/* 2x36 Terminal XOFF rcvd  */
#define	RCVD_PXOFF	0x02			/* 2x36 Printer XOFF rcvd   */
#endif

/************************************************************************
 * Values for digi_flags
 ************************************************************************/
#define DIGI_IXON	0x0001		/* Handle IXON in the FEP	*/
#define DIGI_FAST	0x0002		/* Fast baud rates		*/
#define RTSPACE		0x0004		/* RTS input flow control	*/
#define CTSPACE		0x0008		/* CTS output flow control	*/
#define DSRPACE		0x0010		/* DSR output flow control	*/
#define DCDPACE		0x0020		/* DCD output flow control	*/
#define DTRPACE		0x0040		/* DTR input flow control	*/
#define DIGI_COOK	0x0080		/* Cooked processing done in FEP */
#define DIGI_FORCEDCD	0x0100		/* Force carrier		*/
#define	DIGI_ALTPIN	0x0200		/* Alternate RJ-45 pin config	*/
#define	DIGI_AIXON	0x0400		/* Aux flow control in fep	*/
#define	DIGI_PRINTER	0x0800		/* Hold port open for flow cntrl*/
#define DIGI_PP_INPUT	0x1000		/* Change parallel port to input*/
#define DIGI_DTR_TOGGLE	0x2000		/* Support DTR Toggle           */
#define DIGI_422	0x4000		/* for 422/232 selectable panel */
#define DIGI_RTS_TOGGLE	0x8000		/* Support RTS Toggle		*/

/************************************************************************
 * These options are not supported on the comxi.
 ************************************************************************/
#define	DIGI_COMXI	(DIGI_FAST|DIGI_COOK|DSRPACE|DCDPACE|DTRPACE)

#define DIGI_PLEN	28		/* String length		*/
#define	DIGI_TSIZ	10		/* Terminal string len		*/

/************************************************************************
 * Structure used with ioctl commands for DIGI parameters.
 ************************************************************************/
struct digi_t {
	unsigned short	digi_flags;		/* Flags (see above)	*/
	unsigned short	digi_maxcps;		/* Max printer CPS	*/
	unsigned short	digi_maxchar;		/* Max chars in print queue */
	unsigned short	digi_bufsize;		/* Buffer size		*/
	unsigned char	digi_onlen;		/* Length of ON string	*/
	unsigned char	digi_offlen;		/* Length of OFF string	*/
	char		digi_onstr[DIGI_PLEN];	/* Printer on string	*/
	char		digi_offstr[DIGI_PLEN];	/* Printer off string	*/
	char		digi_term[DIGI_TSIZ];	/* terminal string	*/
};

/************************************************************************
 * KME definitions and structures.
 ************************************************************************/
#define	RW_IDLE		0	/* Operation complete			*/
#define	RW_READ		1	/* Read Concentrator Memory		*/
#define	RW_WRITE	2	/* Write Concentrator Memory		*/

struct rw_t {
	unsigned char	rw_req;		/* Request type			*/
	unsigned char	rw_board;	/* Host Adapter board number	*/
	unsigned char	rw_conc;	/* Concentrator number		*/
	unsigned char	rw_reserved;	/* Reserved for expansion	*/
	unsigned int	rw_addr;	/* Address in concentrator	*/
	unsigned short	rw_size;	/* Read/write request length	*/
	unsigned char	rw_data[128];	/* Data to read/write		*/
};

/***********************************************************************
 * Shrink Buffer and Board Information definitions and structures.

 ************************************************************************/
			/* Board type return codes */
#define	PCXI_TYPE 1     /* Board type at the designated port is a PC/Xi */
#define PCXM_TYPE 2     /* Board type at the designated port is a PC/Xm */
#define	PCXE_TYPE 3     /* Board type at the designated port is a PC/Xe */
#define	MCXI_TYPE 4     /* Board type at the designated port is a MC/Xi */
#define COMXI_TYPE 5     /* Board type at the designated port is a COM/Xi */

			 /* Non-Zero Result codes. */
#define RESULT_NOBDFND 1 /* A Digi product at that port is not config installed */
#define RESULT_NODESCT 2 /* A memory descriptor was not obtainable */
#define RESULT_NOOSSIG 3 /* FEP/OS signature was not detected on the board */
#define RESULT_TOOSML  4 /* Too small an area to shrink.  */
#define RESULT_NOCHAN  5 /* Channel structure for the board was not found */

struct shrink_buf_struct {
	unsigned int	shrink_buf_vaddr;	/* Virtual address of board */
	unsigned int	shrink_buf_phys;	/* Physical address of board */
	unsigned int	shrink_buf_bseg;	/* Amount of board memory */
	unsigned int	shrink_buf_hseg;	/* '186 Beginning of Dual-Port */

	unsigned int	shrink_buf_lseg;	/* '186 Beginning of freed memory */
	unsigned int	shrink_buf_mseg;	/* Linear address from start of
						   dual-port were freed memory
						   begins, host viewpoint. */

	unsigned int	shrink_buf_bdparam;	/* Parameter for xxmemon and
						   xxmemoff */

	unsigned int	shrink_buf_reserva;	/* Reserved */
	unsigned int	shrink_buf_reservb;	/* Reserved */
	unsigned int	shrink_buf_reservc;	/* Reserved */
	unsigned int	shrink_buf_reservd;	/* Reserved */

	unsigned char	shrink_buf_result;	/* Reason for call failing
						   Zero is Good return */
	unsigned char	shrink_buf_init;	/* Non-Zero if it caused an
						   xxinit call. */

	unsigned char	shrink_buf_anports;	/* Number of async ports  */
	unsigned char	shrink_buf_snports; 	/* Number of sync  ports */
	unsigned char	shrink_buf_type;	/* Board type 1 = PC/Xi,
							      2 = PC/Xm,
							      3 = PC/Xe
							      4 = MC/Xi
							      5 = COMX/i */
	unsigned char	shrink_buf_card;	/* Card number */

};

/************************************************************************
 * Structure to get driver status information
 ************************************************************************/
struct digi_dinfo {
	unsigned int	dinfo_nboards;		/* # boards configured	*/
	char		dinfo_reserved[12];	/* for future expansion */
	char		dinfo_version[16];	/* driver version       */
};

#define	DIGI_GETDD	('d'<<8) | 248		/* get driver info      */

/************************************************************************
 * Structure used with ioctl commands for per-board information
 *
 * physsize and memsize differ when board has "windowed" memory
 ************************************************************************/
struct digi_info {
	unsigned int	info_bdnum;		/* Board number (0 based)  */
	unsigned int	info_ioport;		/* io port address         */
	unsigned int	info_physaddr;		/* memory address          */
	unsigned int	info_physsize;		/* Size of host mem window */
	unsigned int	info_memsize;		/* Amount of dual-port mem */
						/* on board                */
	unsigned short	info_bdtype;		/* Board type              */
	unsigned short	info_nports;		/* number of ports         */
	char		info_bdstate;		/* board state             */
	char		info_reserved[7];	/* for future expansion    */
};

#define	DIGI_GETBD	('d'<<8) | 249		/* get board info          */

struct digi_stat {
	unsigned int	info_chan;		/* Channel number (0 based)  */
	unsigned int	info_brd;		/* Board number (0 based)  */
	unsigned int	info_cflag;		/* cflag for channel       */
	unsigned int	info_iflag;		/* iflag for channel       */
	unsigned int	info_oflag;		/* oflag for channel       */
	unsigned int	info_mstat;		/* mstat for channel       */
	unsigned int	info_tx_data;		/* tx_data for channel       */
	unsigned int	info_rx_data;		/* rx_data for channel       */
	unsigned int	info_hflow;		/* hflow for channel       */
	unsigned int	info_reserved[8];	/* for future expansion    */
};

#define	DIGI_GETSTAT	('d'<<8) | 244		/* get board info          */
/************************************************************************
 *
 * Structure used with ioctl commands for per-channel information
 *
 ************************************************************************/
struct digi_ch {
	unsigned int	info_bdnum;		/* Board number (0 based)  */
	unsigned int	info_channel;		/* Channel index number    */
	unsigned int	info_ch_cflag;		/* Channel cflag   	   */
	unsigned int	info_ch_iflag;		/* Channel iflag   	   */
	unsigned int	info_ch_oflag;		/* Channel oflag   	   */
	unsigned int	info_chsize;		/* Channel structure size  */
	unsigned int	info_sleep_stat;	/* sleep status		   */
	dev_t		info_dev;		/* device number	   */
	unsigned char	info_initstate;		/* Channel init state	   */
	unsigned char	info_running;		/* Channel running state   */
	int		reserved[8];		/* reserved for future use */
};

/*
* This structure is used with the DIGI_FEPCMD ioctl to
* tell the driver which port to send the command for.
*/
struct digi_cmd {
	int	cmd;
	int	word;
	int	ncmds;
	int	chan; /* channel index (zero based) */
	int	bdid; /* board index (zero based) */
};


struct digi_getbuffer /* Struct for holding buffer use counts */
{
	unsigned long tIn;
	unsigned long tOut;
	unsigned long rxbuf;
	unsigned long txbuf;
	unsigned long txdone;
};

struct digi_getcounter {
	unsigned long norun;		/* number of UART overrun errors */
	unsigned long noflow;		/* number of buffer overflow errors */
	unsigned long nframe;		/* number of framing errors */
	unsigned long nparity;		/* number of parity errors */
	unsigned long nbreak;		/* number of breaks received */
	unsigned long rbytes;		/* number of received bytes */
	unsigned long tbytes;		/* number of bytes transmitted fully */
};

/*
*  info_sleep_stat defines
*/
#define INFO_RUNWAIT	0x0001
#define INFO_WOPEN	0x0002
#define INFO_TTIOW	0x0004
#define INFO_CH_RWAIT	0x0008
#define INFO_CH_WEMPTY	0x0010
#define INFO_CH_WLOW	0x0020
#define INFO_XXBUF_BUSY 0x0040

#define	DIGI_GETCH	('d'<<8) | 245		/* get board info          */

/* Board type definitions */

#define	SUBTYPE		0007
#define	T_PCXI		0000
#define T_PCXM		0001
#define T_PCXE		0002
#define T_PCXR		0003
#define T_SP		0004
#define T_SP_PLUS	0005
#	define T_HERC	0000
#	define T_HOU	0001
#	define T_LON	0002
#	define T_CHA	0003
#define FAMILY		0070
#define T_COMXI		0000
#define T_PCXX		0010
#define T_CX		0020
#define T_EPC		0030
#define	T_PCLITE	0040
#define	T_SPXX		0050
#define	T_AVXX		0060
#define T_DXB		0070
#define T_A2K_4_8	0070
#define BUSTYPE		0700
#define T_ISABUS	0000
#define T_MCBUS		0100
#define	T_EISABUS	0200
#define	T_PCIBUS	0400

/* Board State Definitions */

#define	BD_RUNNING	0x0
#define	BD_REASON	0x7f
#define	BD_NOTFOUND	0x1
#define	BD_NOIOPORT	0x2
#define	BD_NOMEM	0x3
#define	BD_NOBIOS	0x4
#define	BD_NOFEP	0x5
#define	BD_FAILED	0x6
#define BD_ALLOCATED	0x7
#define BD_TRIBOOT	0x8
#define	BD_BADKME	0x80

#define DIGI_SPOLL            ('d'<<8) | 254  /* change poller rate   */

#define DIGI_SETCUSTOMBAUD	_IOW('e', 106, int)	/* Set integer baud rate */
#define DIGI_GETCUSTOMBAUD	_IOR('e', 107, int)	/* Get integer baud rate */

#define DIGI_REALPORT_GETBUFFERS ('e'<<8 ) | 108
#define DIGI_REALPORT_SENDIMMEDIATE ('e'<<8 ) | 109
#define DIGI_REALPORT_GETCOUNTERS ('e'<<8 ) | 110
#define DIGI_REALPORT_GETEVENTS ('e'<<8 ) | 111

#define EV_OPU		0x0001		/* !<Output paused by client */
#define EV_OPS		0x0002		/* !<Output paused by reqular sw flowctrl */
#define EV_OPX		0x0004		/* !<Output paused by extra sw flowctrl */
#define EV_OPH		0x0008		/* !<Output paused by hw flowctrl */
#define EV_OPT		0x0800		/* !<Output paused for RTS Toggle predelay */

#define EV_IPU		0x0010		/* !<Input paused unconditionally by user */
#define EV_IPS		0x0020		/* !<Input paused by high/low water marks */
#define EV_IPA		0x0400		/* !<Input paused by pattern alarm module */

#define EV_TXB		0x0040		/* !<Transmit break pending */
#define EV_TXI		0x0080		/* !<Transmit immediate pending */
#define EV_TXF		0x0100		/* !<Transmit flowctrl char pending */
#define EV_RXB		0x0200		/* !<Break received */

#define EV_OPALL	0x080f		/* !<Output pause flags */
#define EV_IPALL	0x0430		/* !<Input pause flags */

#endif /* DIGI_H */
