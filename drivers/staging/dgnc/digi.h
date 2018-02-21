/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 */

#ifndef _DIGI_H
#define _DIGI_H

#define DIGI_GETA	(('e' << 8) | 94)	/* Read params */
#define DIGI_SETA	(('e' << 8) | 95)	/* Set params */
#define DIGI_SETAW	(('e' << 8) | 96)	/* Drain & set params */
#define DIGI_SETAF	(('e' << 8) | 97)	/* Drain, flush & set params */
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

#define TTY_FLIPBUF_SIZE 512

#endif	/* _DIGI_H */
