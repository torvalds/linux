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
 */

#ifndef _DIGI_H
#define _DIGI_H

#ifndef TIOCM_LE
#define		TIOCM_LE	0x01		/* line enable */
#define		TIOCM_DTR	0x02		/* data terminal ready */
#define		TIOCM_RTS	0x04		/* request to send */
#define		TIOCM_ST	0x08		/* secondary transmit */
#define		TIOCM_SR	0x10		/* secondary receive */
#define		TIOCM_CTS	0x20		/* clear to send */
#define		TIOCM_CAR	0x40		/* carrier detect */
#define		TIOCM_RNG	0x80		/* ring	indicator */
#define		TIOCM_DSR	0x100		/* data set ready */
#define		TIOCM_RI	TIOCM_RNG	/* ring (alternate) */
#define		TIOCM_CD	TIOCM_CAR	/* carrier detect (alt)	*/
#endif

#if !defined(TIOCMSET)
#define	TIOCMSET	(('d' << 8) | 252)	/* set modem ctrl state	*/
#define	TIOCMGET	(('d' << 8) | 253)	/* set modem ctrl state	*/
#endif

#if !defined(TIOCMBIC)
#define	TIOCMBIC	(('d' << 8) | 254)	/* set modem ctrl state */
#define	TIOCMBIS	(('d' << 8) | 255)	/* set modem ctrl state */
#endif

#define DIGI_GETA	(('e' << 8) | 94)	/* Read params */
#define DIGI_SETA	(('e' << 8) | 95)	/* Set params */
#define DIGI_SETAW	(('e' << 8) | 96)	/* Drain & set params */
#define DIGI_SETAF	(('e' << 8) | 97)	/* Drain, flush & set params */
#define DIGI_GET_NI_INFO (('d' << 8) | 250)	/* Non-intelligent state info */
#define DIGI_LOOPBACK (('d' << 8) | 252)	/* Enable/disable UART
						 * internal loopback
						 */
#define DIGI_FAST	0x0002		/* Fast baud rates */
#define RTSPACE		0x0004		/* RTS input flow control */
#define CTSPACE		0x0008		/* CTS output flow control */
#define DIGI_COOK	0x0080		/* Cooked processing done in FEP */
#define DIGI_FORCEDCD	0x0100		/* Force carrier */
#define	DIGI_ALTPIN	0x0200		/* Alternate RJ-45 pin config */
#define	DIGI_PRINTER	0x0800		/* Hold port open for flow cntrl*/
#define DIGI_DTR_TOGGLE	0x2000		/* Support DTR Toggle */
#define DIGI_RTS_TOGGLE	0x8000		/* Support RTS Toggle */
#define DIGI_PLEN	28		/* String length */
#define	DIGI_TSIZ	10		/* Terminal string len */

/*
 * Structure used with ioctl commands for DIGI parameters.
 */
/**
 * struct digi_t - Ioctl commands for DIGI parameters.
 * @digi_flags: Flags.
 * @digi_maxcps: Maximum printer CPS.
 * @digi_maxchar: Maximum characters in the print queue.
 * @digi_bufsize: Buffer size.
 * @digi_onlen: Length of ON string.
 * @digi_offlen: Length of OFF string.
 * @digi_onstr: Printer ON string.
 * @digi_offstr: Printer OFF string.
 * @digi_term: Terminal string.
 */
struct digi_t {
	unsigned short	digi_flags;
	unsigned short	digi_maxcps;
	unsigned short	digi_maxchar;
	unsigned short	digi_bufsize;
	unsigned char	digi_onlen;
	unsigned char	digi_offlen;
	char		digi_onstr[DIGI_PLEN];
	char		digi_offstr[DIGI_PLEN];
	char		digi_term[DIGI_TSIZ];
};

/**
 * struct digi_dinfo - Driver status information.
 * @dinfo_nboards: Number of boards configured.
 * @dinfo_reserved: Not used, for future expansion.
 * @dinfio_version: Driver version.
 */
struct digi_dinfo {
	unsigned int	dinfo_nboards;
	char		dinfo_reserved[12];
	char		dinfo_version[16];
};

#define	DIGI_GETDD	(('d' << 8) | 248)	/* get driver info */

/**
 * struct digi_info - Ioctl commands for per board information.
 *
 * Physsize and memsize differ when board has "windowed" memory.
 *
 * @info_bdnum: Board number (0 based).
 * @info_ioport: IO port address.
 * @indo_physaddr: Memory address.
 * @info_physize: Size of host memory window.
 * @info_memsize: Amount of dual-port memory on board.
 * @info_bdtype: Board type.
 * @info_nports: Number of ports.
 * @info_bdstate: Board state.
 * @info_reserved: Not used, for future expansion.
 */
struct digi_info {
	unsigned int	info_bdnum;
	unsigned int	info_ioport;
	unsigned int	info_physaddr;
	unsigned int	info_physsize;
	unsigned int	info_memsize;
	unsigned short	info_bdtype;
	unsigned short	info_nports;
	char		info_bdstate;
	char		info_reserved[7];
};

#define	DIGI_GETBD	(('d' << 8) | 249)	/* get board info */

/**
 * struct digi_getbuffer - Holds buffer use counts.
 */
struct digi_getbuffer {
	unsigned long tx_in;
	unsigned long tx_out;
	unsigned long rxbuf;
	unsigned long txbuf;
	unsigned long txdone;
};

/**
 * struct digi_getcounter
 * @norun: Number of UART overrun errors.
 * @noflow: Number of buffer overflow errors.
 * @nframe: Number of framing errors.
 * @nparity: Number of parity errors.
 * @nbreak: Number of breaks received.
 * @rbytes: Number of received bytes.
 * @tbytes: Number of transmitted bytes.
 */
struct digi_getcounter {
	unsigned long norun;
	unsigned long noflow;
	unsigned long nframe;
	unsigned long nparity;
	unsigned long nbreak;
	unsigned long rbytes;
	unsigned long tbytes;
};

/* Board State Definitions */
#define	BD_RUNNING	0x0
#define	BD_NOFEP	0x5

#define DIGI_SETCUSTOMBAUD _IOW('e', 106, int)	/* Set integer baud rate */
#define DIGI_GETCUSTOMBAUD _IOR('e', 107, int)	/* Get integer baud rate */

#define DIGI_REALPORT_GETBUFFERS (('e' << 8) | 108)
#define DIGI_REALPORT_SENDIMMEDIATE (('e' << 8) | 109)
#define DIGI_REALPORT_GETCOUNTERS (('e' << 8) | 110)
#define DIGI_REALPORT_GETEVENTS (('e' << 8) | 111)

#define EV_OPU 0x0001 /* Output paused by client */
#define EV_OPS 0x0002 /* Output paused by regular sw flowctrl */
#define EV_IPU 0x0010 /* Input paused unconditionally by user */
#define EV_IPS 0x0020 /* Input paused by high/low water marks */
#define EV_TXB 0x0040 /* Transmit break pending */

/**
 * struct ni_info - intelligent <--> non-intelligent DPA translation.
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

#define T_CLASSIC 0002
#define T_PCIBUS 0400
#define T_NEO_EXPRESS 0001
#define T_NEO 0000

#define TTY_FLIPBUF_SIZE 512

#endif	/* _DIGI_H */
