/*
 *
 * Copyright 1999 Digi International (www.digi.com)
 *     Gene Olson  <gene at digi dot com>
 *     James Puzzo <jamesp at digi dot com>
 *     Scott Kilau <scottk at digi dot com>
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
 */

/************************************************************************
 * Master include file for Linux Realport Driver.
 ************************************************************************/

#ifndef __DRP_H
#define __DRP_H

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/tty.h>


#include "digirp.h"

/************************************************************************
 * Tuning parameters.
 ************************************************************************/

#define CHAN_MAX	64		/* Max # ports per server */

#define SEQ_MAX		128		/* Max # transmit sequences (2^n) */
#define SEQ_MASK	(SEQ_MAX-1)	/* Sequence buffer modulus mask */

#define TBUF_MAX	4096		/* Size of transmit buffer (2^n) */
#define RBUF_MAX	4096		/* Size of receive buffer (2^n) */

#define TBUF_MASK	(TBUF_MAX-1)	/* Transmit buffer modulus mask */
#define RBUF_MASK	(RBUF_MAX-1)	/* Receive buffer modulus mask */

#define TBUF_LOW	1000		/* Transmit low water mark */

#define UIO_BASE	1000		/* Base for write operations */
#define UIO_MIN		2000		/* Minimum size application buffer */
#define UIO_MAX		8100		/* Unix I/O buffer size */

#define MON_MAX		65536		/* Monitor buffer size (2^n) */
#define MON_MASK	(MON_MAX-1)	/* Monitor wrap mask */

#define DPA_MAX		65536		/* DPA buffer size (2^n) */
#define DPA_MASK	(DPA_MAX-1)	/* DPA wrap mask */
#define DPA_HIGH_WATER	58000		/* Enforce flow control when
					 * over this amount
					 */

#define IDLE_MAX	(20 * HZ)	/* Max TCP link idle time */

#define MAX_DESC_LEN	100		/* Maximum length of stored PS
					 * description
					 */

#define WRITEBUFLEN	((4096) + 4)    /* 4 extra for alignment play space */

#define VPDSIZE		512

/************************************************************************
 * Minor device decoding conventions.
 ************************************************************************
 *
 * For Linux, the net and mon devices are handled via "proc", so we
 * only have to mux the "tty" devices.  Since every PortServer will
 * have an individual major number, the PortServer number does not
 * need to be encoded, and in fact, does not need to exist.
 *
 */

/*
 * Port device decoding conventions:
 *
 *	Device 00 - 3f        64 dial-in modem devices. (tty)
 *	Device 40 - 7f        64 dial-out tty devices.  (cu)
 *	Device 80 - bf        64 dial-out printer devices.
 *
 *  IS_PRINT(dev)		This is a printer device.
 *
 *  OPEN_CATEGORY(dev)		Specifies the device category.  No two
 *				devices of different categories may be open
 *				at the same time.
 *
 * The following require the category returned by OPEN_CATEGORY().
 *
 *  OPEN_WAIT_AVAIL(cat)	Waits on open until the device becomes
 *				available.  Fails if NDELAY specified.
 *
 *  OPEN_WAIT_CARRIER(cat)	Waits on open if carrier is not present.
 *				Succeeds if NDELAY is given.
 *
 *  OPEN_FORCES_CARRIER(cat)	Carrier is forced high on open.
 *
 */

#define PORT_NUM(dev)			((dev) & 0x3f)

#define OPEN_CATEGORY(dev)		((((dev) & 0x80) & 0x40))
#define IS_PRINT(dev)			(((dev) & 0xff) >= 0x80)

#define OPEN_WAIT_AVAIL(cat)		(((cat) & 0x40) == 0x000)
#define OPEN_WAIT_CARRIER(cat)		(((cat) & 0x40) == 0x000)
#define OPEN_FORCES_CARRIER(cat)	(((cat) & 0x40) != 0x000)


/************************************************************************
 * Modem signal defines for 16450/16550 compatible FEP.
 * set in ch_mout, ch_mflow, ch_mlast etc
 ************************************************************************/

/* TODO : Re-verify that these modem signal definitions are correct */

#define DM_DTR		0x01
#define DM_RTS		0x02
#define DM_RTS_TOGGLE	0x04

#define DM_OUT1		0x04
#define DM_OUT2		0x08

#define DM_CTS		0x10
#define DM_DSR		0x20
#define DM_RI		0x40
#define DM_CD		0x80		/* This is the DCD flag */


/************************************************************************
 * Realport Event Flags.
 ************************************************************************/

#define EV_OPU		0x0001		/* Ouput paused by client */
#define EV_OPS		0x0002		/* Output paused by XOFF */
#define EV_OPX		0x0004		/* Output paused by XXOFF */
#define EV_OPH		0x0008		/* Output paused by MFLOW */
#define EV_IPU		0x0010		/* Input paused by client */
#define EV_IPS		0x0020		/* Input paused by hi/low water */
#define EV_TXB		0x0040		/* Transmit break pending */
#define EV_TXI		0x0080		/* Transmit immediate pending */
#define EV_TXF		0x0100		/* Transmit flow control pending */
#define EV_RXB		0x0200		/* Break received */


/************************************************************************
 * Realport CFLAGS.
 ************************************************************************/

#define CF_CS5		0x0000		/* 5 bit characters */
#define CF_CS6		0x0010		/* 6 bit characters */
#define CF_CS7		0x0020		/* 7 bit characters */
#define CF_CS8		0x0030		/* 8 bit characters */
#define CF_CSIZE	0x0030		/* Character size */
#define CF_CSTOPB	0x0040		/* Two stop bits */
#define CF_CREAD	0x0080		/* Enable receiver */
#define CF_PARENB	0x0100		/* Enable parity */
#define CF_PARODD	0x0200		/* Odd parity */
#define CF_HUPCL	0x0400		/* Drop DTR on close */


/************************************************************************
 * Realport XFLAGS.
 ************************************************************************/

#define XF_XPAR		0x0001		/* Enable Mark/Space Parity */
#define XF_XMODEM	0x0002		/* Enable in-band modem signalling */
#define XF_XCASE	0x0004		/* Convert special characters */
#define XF_XEDATA	0x0008		/* Error data in stream */
#define XF_XTOSS	0x0010		/* Toss IXANY characters */
#define XF_XIXON	0x0020		/* xxon/xxoff enable */


/************************************************************************
 * Realport IFLAGS.
 ************************************************************************/

#define IF_IGNBRK	0x0001		/* Ignore input break */
#define IF_BRKINT	0x0002		/* Break interrupt */
#define IF_IGNPAR	0x0004		/* Ignore error characters */
#define IF_PARMRK	0x0008		/* Error chars marked with 0xff */
#define IF_INPCK	0x0010		/* Input parity checking enabled */
#define IF_ISTRIP	0x0020		/* Input chars masked with 0x7F */
#define IF_IXON		0x0400		/* Output software flow control */
#define IF_IXANY	0x0800		/* Restart output on any char */
#define	IF_IXOFF	0x1000		/* Input software flow control */
#define IF_DOSMODE	0x8000		/* 16450-compatible errors */


/************************************************************************
 * Realport OFLAGS.
 ************************************************************************/

#define OF_OLCUC	0x0002		/* Map lower to upper case */
#define OF_ONLCR	0x0004		/* Map NL to CR-NL */
#define OF_OCRNL	0x0008		/* Map CR to NL */
#define OF_ONOCR	0x0010		/* No CR output at column 0 */
#define OF_ONLRET	0x0020		/* Assume NL does NL/CR */
#define OF_TAB3		0x1800		/* Tabs expand to 8 spaces */
#define OF_TABDLY	0x1800		/* Tab delay */

/************************************************************************
 * Unit flag definitions for un_flag.
 ************************************************************************/

/* These are the DIGI unit flags */
#define UN_EXCL		0x00010000	/* Exclusive open */
#define UN_STICKY	0x00020000	/* TTY Settings are now sticky */
#define UN_BUSY		0x00040000	/* Some work this channel */
#define UN_PWAIT	0x00080000	/* Printer waiting for terminal */
#define UN_TIME		0x00100000	/* Waiting on time */
#define UN_EMPTY	0x00200000	/* Waiting output queue empty */
#define UN_LOW		0x00400000	/* Waiting output low water */
#define UN_DIGI_MASK	0x00FF0000	/* Waiting output low water */

/*
 * Definitions for async_struct (and serial_struct) flags field
 *
 * these are the ASYNC flags copied from serial.h
 *
 */
#define UN_HUP_NOTIFY	0x0001 /* Notify getty on hangups and
				* closes on the callout port
				*/
#define UN_FOURPORT	0x0002	/* Set OU1, OUT2 per AST Fourport settings */
#define UN_SAK		0x0004	/* Secure Attention Key (Orange book) */
#define UN_SPLIT_TERMIOS 0x0008 /* Separate termios for dialin/callout */

#define UN_SPD_MASK	0x0030
#define UN_SPD_HI	0x0010	/* Use 56000 instead of 38400 bps */
#define UN_SPD_VHI	0x0020	/* Use 115200 instead of 38400 bps */
#define UN_SPD_CUST	0x0030	/* Use user-specified divisor */

#define UN_SKIP_TEST	0x0040 /* Skip UART test during autoconfiguration */
#define UN_AUTO_IRQ	0x0080 /* Do automatic IRQ during autoconfiguration */

#define UN_SESSION_LOCKOUT 0x0100 /* Lock out cua opens based on session */
#define UN_PGRP_LOCKOUT	   0x0200 /* Lock out cua opens based on pgrp */
#define UN_CALLOUT_NOHUP   0x0400 /* Don't do hangups for cua device */

#define UN_FLAGS	0x0FFF	/* Possible legal async flags */
#define UN_USR_MASK	0x0430	/* Legal flags that non-privileged
				 * users can set or reset
				 */

#define UN_INITIALIZED		0x80000000 /* Serial port was initialized */
#define UN_CALLOUT_ACTIVE	0x40000000 /* Call out device is active */
#define UN_NORMAL_ACTIVE	0x20000000 /* Normal device is active */
#define UN_BOOT_AUTOCONF	0x10000000 /* Autoconfigure port on bootup */
#define UN_CLOSING		0x08000000 /* Serial port is closing */
#define UN_CTS_FLOW		0x04000000 /* Do CTS flow control */
#define UN_CHECK_CD		0x02000000 /* i.e., CLOCAL */
#define UN_SHARE_IRQ		0x01000000 /* for multifunction cards */


/************************************************************************
 * Structure for terminal or printer unit.  struct un_struct
 *
 * Note that in some places the code assumes the "tty_t" is placed
 * first in the structure.
 ************************************************************************/

struct un_struct {
	struct tty_struct *un_tty;		/* System TTY struct */
	struct ch_struct *un_ch;		/* Associated channel */

	ushort     un_open_count;		/* Successful open count */
	int		un_flag;		/* Unit flags */
	ushort     un_tbusy;		/* Busy transmit count */

	wait_queue_head_t  un_open_wait;
	wait_queue_head_t  un_close_wait;
	ushort	un_type;
	struct device *un_sysfs;
};


/************************************************************************
 * Channel State Numbers for ch_state.
 ************************************************************************/

/*
 * The ordering is important.
 *
 *    state <= CS_WAIT_CANCEL implies the channel is definitely closed.
 *
 *    state >= CS_WAIT_FAIL  implies the channel is definitely open.
 *
 *    state >= CS_READY implies data is allowed on the channel.
 */

enum dgrp_ch_state_t {
	CS_IDLE = 0,	    /* Channel is idle */
	CS_WAIT_OPEN = 1,   /* Waiting for Immediate Open Resp */
	CS_WAIT_CANCEL = 2, /* Waiting for Per/Incom Cancel Resp */
	CS_WAIT_FAIL = 3,   /* Waiting for Immed Open Failure */
	CS_SEND_QUERY = 4,  /* Ready to send Port Query */
	CS_WAIT_QUERY = 5,  /* Waiting for Port Query Response */
	CS_READY = 6,	    /* Ready to accept commands and data */
	CS_SEND_CLOSE =	7,  /* Ready to send Close Request */
	CS_WAIT_CLOSE =	8   /* Waiting for Close Response */
};

/************************************************************************
 * Device flag definitions for ch_flag.
 ************************************************************************/

/*
 *  Note that the state of the two carrier based flags is key.	When
 *  we check for carrier state transitions, we look at the current
 *  physical state of the DCD line and compare it with PHYS_CD (which
 *  was the state the last time we checked), and we also determine
 *  a new virtual state (composite of the physical state, FORCEDCD,
 *  CLOCAL, etc.) and compare it with VIRT_CD.
 *
 *  VIRTUAL transitions high will have the side effect of waking blocked
 *  opens.
 *
 *  PHYSICAL transitions low will cause hangups to occur _IF_ the virtual
 *  state is also low.	We DON'T want to hangup on a PURE virtual drop.
 */

#define CH_HANGUP	0x00002		/* Server port ready to close */

#define CH_VIRT_CD	0x00004		/* Carrier was virtually present */
#define CH_PHYS_CD	0x00008		/* Carrier was physically present */

#define CH_CLOCAL	0x00010		/* CLOCAL set in cflags */
#define CH_BAUD0	0x00020		/* Baud rate zero hangup */

#define CH_FAST_READ	0x00040		/* Fast reads are enabled */
#define CH_FAST_WRITE	0x00080		/* Fast writes are enabled */

#define CH_PRON		0x00100		/* Printer on string active */
#define CH_RX_FLUSH	0x00200		/* Flushing receive data */
#define CH_LOW		0x00400		/* Thread waiting for LOW water */
#define CH_EMPTY	0x00800		/* Thread waiting for EMPTY */
#define CH_DRAIN	0x01000		/* Close is waiting to drain */
#define CH_INPUT	0x02000		/* Thread waiting for INPUT */
#define CH_RXSTOP	0x04000		/* Stop output to ldisc */
#define CH_PARAM	0x08000		/* A parameter was updated */
#define CH_WAITING_SYNC 0x10000		/* A pending sync was assigned
					 * to this port.
					 */
#define CH_PORT_GONE	0x20000		/* Port has disappeared */
#define CH_TX_BREAK	0x40000		/* TX Break to be sent,
					 * but has not yet.
					 */

/************************************************************************
 * Types of Open Requests for ch_otype.
 ************************************************************************/

#define OTYPE_IMMEDIATE	  0		/* Immediate Open */
#define OTYPE_PERSISTENT  1		/* Persistent Open */
#define OTYPE_INCOMING	  2		/* Incoming Open */


/************************************************************************
 * Request/Response flags.
 ************************************************************************/

#define RR_SEQUENCE	0x0001		/* Get server RLAST, TIN */
#define RR_STATUS	0x0002		/* Get server MINT, EINT */
#define RR_BUFFER	0x0004		/* Get server RSIZE, TSIZE */
#define RR_CAPABILITY	0x0008		/* Get server port capabilities */

#define RR_TX_FLUSH	0x0040		/* Flush output buffers */
#define RR_RX_FLUSH	0x0080		/* Flush input buffers */

#define RR_TX_STOP	0x0100		/* Pause output */
#define RR_RX_STOP	0x0200		/* Pause input */
#define RR_TX_START	0x0400		/* Start output */
#define RR_RX_START	0x0800		/* Start input */

#define RR_TX_BREAK	0x1000		/* Send BREAK */
#define RR_TX_ICHAR	0x2000		/* Send character immediate */


/************************************************************************
 * Channel information structure.   struct ch_struct
 ************************************************************************/

struct ch_struct {
	struct digi_struct ch_digi;		/* Digi variables */
	int	ch_edelay;		/* Digi edelay */

	struct tty_port port;
	struct un_struct ch_tun;	/* Terminal unit info */
	struct un_struct ch_pun;	/* Printer unit info */

	struct nd_struct *ch_nd;	/* Node pointer */
	u8  *ch_tbuf;		/* Local Transmit Buffer */
	u8  *ch_rbuf;		/* Local Receive Buffer */
	ulong	ch_cpstime;		/* Printer CPS time */
	ulong	ch_waketime;		/* Printer wake time */

	ulong	ch_flag;		/* CH_* flags */

	enum dgrp_ch_state_t ch_state;		/* CS_* Protocol state */
	ushort	ch_send;		/* Bit vector of RR_* requests */
	ushort	ch_expect;		/* Bit vector of RR_* responses */
	ushort	ch_wait_carrier;	/* Thread count waiting for carrier */
	ushort	ch_wait_count[3];	/* Thread count waiting by otype */

	ushort	ch_portnum;		/* Port number */
	ushort	ch_open_count;		/* Successful open count */
	ushort	ch_category;		/* Device category */
	ushort	ch_open_error;		/* Last open error number */
	ushort	ch_break_time;		/* Pending break request time */
	ushort	ch_cpsrem;		/* Printer CPS remainder */
	ushort	ch_ocook;		/* Realport fastcook oflags */
	ushort	ch_inwait;		/* Thread count in CLIST input */

	ushort	ch_tin;			/* Local transmit buffer in ptr */
	ushort	ch_tout;		/* Local transmit buffer out ptr */
	ushort	ch_s_tin;		/* Realport TIN */
	ushort	ch_s_tpos;		/* Realport TPOS */
	ushort	ch_s_tsize;		/* Realport TSIZE */
	ushort	ch_s_treq;		/* Realport TREQ */
	ushort	ch_s_elast;		/* Realport ELAST */

	ushort	ch_rin;			/* Local receive buffer in ptr */
	ushort	ch_rout;		/* Local receive buffer out ptr */
	ushort	ch_s_rin;		/* Realport RIN */
	/* David Fries 7-13-2001, ch_s_rin should be renamed ch_s_rout because
	 * the variable we want to represent is the PortServer's ROUT, which is
	 * the sequence number for the next byte the PortServer will send us.
	 * RIN is the sequence number for the next byte the PortServer will
	 * receive from the uart.  The port server will send data as long as
	 * ROUT is less than RWIN.  What would happen is the port is opened, it
	 * receives data, it gives the value of RIN, we set the RWIN to
	 * RIN+RBUF_MAX-1, it sends us RWIN-ROUT bytes which overflows.	 ROUT
	 * is set to zero when the port is opened, so we start at zero and
	 * count up as data is received.
	 */
	ushort	ch_s_rwin;		/* Realport RWIN */
	ushort	ch_s_rsize;		/* Realport RSIZE */

	ushort	ch_tmax;		/* Local TMAX */
	ushort	ch_ttime;		/* Local TTIME */
	ushort	ch_rmax;		/* Local RMAX */
	ushort	ch_rtime;		/* Local RTIME */
	ushort	ch_rlow;		/* Local RLOW */
	ushort	ch_rhigh;		/* Local RHIGH */

	ushort	ch_s_tmax;		/* Realport TMAX */
	ushort	ch_s_ttime;		/* Realport TTIME */
	ushort	ch_s_rmax;		/* Realport RMAX */
	ushort	ch_s_rtime;		/* Realport RTIME */
	ushort	ch_s_rlow;		/* Realport RLOW */
	ushort	ch_s_rhigh;		/* Realport RHIGH */

	ushort	ch_brate;		/* Local baud rate */
	ushort	ch_cflag;		/* Local tty cflags */
	ushort	ch_iflag;		/* Local tty iflags */
	ushort	ch_oflag;		/* Local tty oflags */
	ushort	ch_xflag;		/* Local tty xflags */

	ushort	ch_s_brate;		/* Realport BRATE */
	ushort	ch_s_cflag;		/* Realport CFLAG */
	ushort	ch_s_iflag;		/* Realport IFLAG */
	ushort	ch_s_oflag;		/* Realport OFLAG */
	ushort	ch_s_xflag;		/* Realport XFLAG */

	u8	ch_otype;		/* Open request type */
	u8	ch_pscan_savechar;	/* Last character read by parity scan */
	u8	ch_pscan_state;		/* PScan State based on last 2 chars */
	u8	ch_otype_waiting;	/* Type of open pending in server */
	u8	ch_flush_seq;		/* Receive flush end sequence */
	u8	ch_s_mlast;		/* Realport MLAST */

	u8	ch_mout;		/* Local MOUT */
	u8	ch_mflow;		/* Local MFLOW */
	u8	ch_mctrl;		/* Local MCTRL */
	u8	ch_xon;			/* Local XON */
	u8	ch_xoff;		/* Local XOFF */
	u8	ch_lnext;		/* Local LNEXT */
	u8	ch_xxon;		/* Local XXON */
	u8	ch_xxoff;		/* Local XXOFF */

	u8	ch_s_mout;		/* Realport MOUT */
	u8	ch_s_mflow;		/* Realport MFLOW */
	u8	ch_s_mctrl;		/* Realport MCTRL */
	u8	ch_s_xon;		/* Realport XON */
	u8	ch_s_xoff;		/* Realport XOFF */
	u8	ch_s_lnext;		/* Realport LNEXT */
	u8	ch_s_xxon;		/* Realport XXON */
	u8	ch_s_xxoff;		/* Realport XXOFF */

	wait_queue_head_t ch_flag_wait;	/* Wait queue for ch_flag changes */
	wait_queue_head_t ch_sleep;	/* Wait queue for my_sleep() */

	int	ch_custom_speed;	/* Realport custom speed */
	int	ch_txcount;		/* Running TX count */
	int	ch_rxcount;		/* Running RX count */
};


/************************************************************************
 * Node State definitions.
 ************************************************************************/

enum dgrp_nd_state_t {
	NS_CLOSED = 0,	   /* Network device is closed */
	NS_IDLE = 1,	   /* Network connection inactive */
	NS_SEND_QUERY =	2, /* Send server query */
	NS_WAIT_QUERY =	3, /* Wait for query response */
	NS_READY = 4,	   /* Network ready */
	NS_SEND_ERROR =	5  /* Must send error hangup */
};

#define ND_STATE_STR(x) \
	((x) == NS_CLOSED     ? "CLOSED"     : \
	((x) == NS_IDLE	      ? "IDLE"	     : \
	((x) == NS_SEND_QUERY ? "SEND_QUERY" : \
	((x) == NS_WAIT_QUERY ? "WAIT_QUERY" : \
	((x) == NS_READY      ? "READY"	     : \
	((x) == NS_SEND_ERROR ? "SEND_ERROR" : "UNKNOWN"))))))

/************************************************************************
 * Node Flag definitions.
 ************************************************************************/

#define ND_SELECT	0x0001		/* Multiple net read selects */
#define ND_DEB_WAIT	0x0002		/* Debug Device waiting */


/************************************************************************
 * Monitoring flag definitions.
 ************************************************************************/

#define MON_WAIT_DATA	0x0001		/* Waiting for buffer data */
#define MON_WAIT_SPACE	0x0002		/* Waiting for buffer space */

/************************************************************************
 * DPA flag definitions.
 ************************************************************************/

#define DPA_WAIT_DATA	0x0001		/* Waiting for buffer data */
#define DPA_WAIT_SPACE	0x0002		/* Waiting for buffer space */


/************************************************************************
 * Definitions taken from Realport Dump.
 ************************************************************************/

#define RPDUMP_MAGIC	"Digi-RealPort-1.0"

#define RPDUMP_MESSAGE	0xE2		/* Descriptive message */
#define RPDUMP_RESET	0xE7		/* Connection reset */
#define RPDUMP_CLIENT	0xE8		/* Client data */
#define RPDUMP_SERVER	0xE9		/* Server data */


/************************************************************************
 * Node request/response definitions.
 ************************************************************************/

#define NR_ECHO		0x0001		/* Server echo packet */
#define NR_IDENT	0x0002		/* Server Product ID */
#define NR_CAPABILITY	0x0004		/* Server Capabilties */
#define NR_VPD		0x0008		/* Server VPD, if any */
#define NR_PASSWORD	0x0010		/* Server Password */

/************************************************************************
 * Registration status of the node's Linux struct tty_driver structures.
 ************************************************************************/
#define SERIAL_TTDRV_REG   0x0001     /* nd_serial_ttdriver registered	*/
#define CALLOUT_TTDRV_REG  0x0002     /* nd_callout_ttdriver registered */
#define XPRINT_TTDRV_REG   0x0004     /* nd_xprint_ttdriver registered	*/


/************************************************************************
 * Node structure.  There exists one of these for each associated
 * realport server.
 ************************************************************************/

struct nd_struct {
	struct list_head	list;
	long	      nd_major;		   /* Node's major number	    */
	long	      nd_ID;		   /* Node's ID code		    */

	char	      nd_serial_name[50];   /* "tty_dgrp_<id>_" + null	    */
	char	      nd_callout_name[50];  /* "cu_dgrp_<id>_" + null	    */
	char	      nd_xprint_name[50];   /* "pr_dgrp_<id>_" + null	    */

	char	     password[16];	  /* Password for server, if needed */
	int	     nd_tty_ref_cnt;	  /* Linux tty reference count	   */

	struct proc_dir_entry *nd_net_de; /* Dir entry for /proc/dgrp/net  */
	struct proc_dir_entry *nd_mon_de; /* Dir entry for /proc/dgrp/mon  */
	struct proc_dir_entry *nd_ports_de; /* Dir entry for /proc/dgrp/ports*/
	struct proc_dir_entry *nd_dpa_de; /* Dir entry for /proc/dgrp/dpa  */

	spinlock_t nd_lock;		  /* General node lock		   */

	struct semaphore nd_net_semaphore; /* Net read/write lock	    */
	struct semaphore nd_mon_semaphore; /* Monitor buffer lock	    */
	spinlock_t nd_dpa_lock;		/* DPA buffer lock	     */

	enum dgrp_nd_state_t nd_state;	  /* NS_* network state */
	int	      nd_chan_count;	   /* # active channels		    */
	int	      nd_flag;		   /* Node flags		    */
	int	      nd_send;		   /* Responses to send		    */
	int	      nd_expect;	   /* Responses we expect	    */

	u8	 *nd_iobuf;	       /* Network R/W Buffer		*/
	wait_queue_head_t nd_tx_waitq;	  /* Network select wait queue	   */

	u8	 *nd_inputbuf;	       /* Input Buffer			*/
	u8	 *nd_inputflagbuf;     /* Input Flags Buffer		*/

	int	      nd_tx_deposit;	   /* Accumulated transmit deposits */
	int	      nd_tx_charge;	   /* Accumulated transmit charges  */
	int	      nd_tx_credit;	   /* Current TX credit		    */
	int	      nd_tx_ready;	   /* Ready to transmit		    */
	int	      nd_tx_work;	   /* TX work waiting		    */
	ulong	     nd_tx_time;	  /* Last transmit time		   */
	ulong	     nd_poll_time;	  /* Next scheduled poll time	   */

	int	      nd_delay;		   /* Current TX delay		    */
	int	      nd_rate;		   /* Current TX rate		    */
	struct link_struct nd_link;		/* Link speed params.		 */

	int	      nd_seq_in;	   /* TX seq in ptr		    */
	int	      nd_seq_out;	   /* TX seq out ptr		    */
	int	      nd_unack;		   /* Unacknowledged byte count	    */
	int	      nd_remain;	   /* Remaining receive bytes	    */
	int	      nd_tx_module;	   /* Current TX module #	    */
	int	      nd_rx_module;	   /* Current RX module #	    */
	char	     *nd_error;		   /* Protocol error message	    */

	int	      nd_write_count;	   /* drp_write() call count	    */
	int	      nd_read_count;	   /* drp_read() count		    */
	int	      nd_send_count;	   /* TCP message sent		    */
	int	      nd_tx_byte;	   /* Transmit byte count	    */
	int	      nd_rx_byte;	   /* Receive byte count	    */

	ulong	     nd_mon_lbolt;	 /* Monitor start time		   */
	int	      nd_mon_flag;	  /* Monitor flags		    */
	int	      nd_mon_in;	  /* Monitor in pointer		    */
	int	      nd_mon_out;	  /* Monitor out pointer	    */
	wait_queue_head_t nd_mon_wqueue;  /* Monitor wait queue (on flags)  */
	u8	 *nd_mon_buf;	      /* Monitor buffer			*/

	ulong	     nd_dpa_lbolt;	/* DPA start time	      */
	int	     nd_dpa_flag;	/* DPA flags		      */
	int	     nd_dpa_in;		/* DPA in pointer	      */
	int	     nd_dpa_out;	/* DPA out pointer	      */
	wait_queue_head_t nd_dpa_wqueue; /* DPA wait queue (on flags)  */
	u8	  *nd_dpa_buf;	/* DPA buffer		      */

	uint	     nd_dpa_debug;
	uint	     nd_dpa_port;

	wait_queue_head_t nd_seq_wque[SEQ_MAX];	  /* TX thread wait queues */
	u8	  nd_seq_wait[SEQ_MAX];	  /* Transmit thread wait count */

	ushort	     nd_seq_size[SEQ_MAX];   /* Transmit seq packet size   */
	ulong	     nd_seq_time[SEQ_MAX];   /* Transmit seq packet time   */

	ushort	     nd_hw_ver;		  /* HW version returned from PS   */
	ushort	     nd_sw_ver;		  /* SW version returned from PS   */
	uint	     nd_hw_id;		  /* HW ID returned from PS	   */
	u8	  nd_ps_desc[MAX_DESC_LEN+1];  /* Description from PS	*/
	uint	     nd_vpd_len;		/* VPD len, if any */
	u8	     nd_vpd[VPDSIZE];		/* VPD, if any */

	ulong	     nd_ttdriver_flags;	  /* Registration status	    */
	struct tty_driver *nd_serial_ttdriver;	/* Linux TTYDRIVER structure */
	struct tty_driver *nd_callout_ttdriver; /* Linux TTYDRIVER structure */
	struct tty_driver *nd_xprint_ttdriver;	/* Linux TTYDRIVER structure */

	u8	     *nd_writebuf;		/* Used to cache data read
						 * from user
						 */
	struct ch_struct nd_chan[CHAN_MAX];  /* Channel array		    */
	struct device *nd_class_dev;	/* Hang our sysfs stuff off of here */
};

#endif /* __DRP_H */
