/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.
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
**
**	Module		: port.h
**	SID		: 1.3
**	Last Modified	: 11/6/98 11:34:12
**	Retrieved	: 11/6/98 11:34:21
**
**  ident @(#)port.h	1.3
**
** -----------------------------------------------------------------------------
*/

#ifndef	__rio_port_h__
#define	__rio_port_h__

/*
**	Port data structure
*/
struct Port {
	struct gs_port gs;
	int PortNum;			/* RIO port no., 0-511 */
	struct Host *HostP;
	void __iomem *Caddr;
	unsigned short HostPort;	/* Port number on host card */
	unsigned char RupNum;		/* Number of RUP for port */
	unsigned char ID2;		/* Second ID of RTA for port */
	unsigned long State;		/* FLAGS for open & xopen */
#define	RIO_LOPEN	0x00001		/* Local open */
#define	RIO_MOPEN	0x00002		/* Modem open */
#define	RIO_WOPEN	0x00004		/* Waiting for open */
#define	RIO_CLOSING	0x00008		/* The port is being close */
#define	RIO_XPBUSY	0x00010		/* Transparent printer busy */
#define	RIO_BREAKING	0x00020		/* Break in progress */
#define	RIO_DIRECT	0x00040		/* Doing Direct output */
#define	RIO_EXCLUSIVE	0x00080		/* Stream open for exclusive use */
#define	RIO_NDELAY	0x00100		/* Stream is open FNDELAY */
#define	RIO_CARR_ON	0x00200		/* Stream has carrier present */
#define	RIO_XPWANTR	0x00400		/* Stream wanted by Xprint */
#define	RIO_RBLK	0x00800		/* Stream is read-blocked */
#define	RIO_BUSY	0x01000		/* Stream is BUSY for write */
#define	RIO_TIMEOUT	0x02000		/* Stream timeout in progress */
#define	RIO_TXSTOP	0x04000		/* Stream output is stopped */
#define	RIO_WAITFLUSH	0x08000		/* Stream waiting for flush */
#define	RIO_DYNOROD	0x10000		/* Drain failed */
#define	RIO_DELETED	0x20000		/* RTA has been deleted */
#define RIO_ISSCANCODE	0x40000		/* This line is in scancode mode */
#define	RIO_USING_EUC	0x100000	/* Using extended Unix chars */
#define	RIO_CAN_COOK	0x200000	/* This line can do cooking */
#define RIO_TRIAD_MODE  0x400000	/* Enable TRIAD special ops. */
#define RIO_TRIAD_BLOCK 0x800000	/* Next read will block */
#define RIO_TRIAD_FUNC  0x1000000	/* Seen a function key coming in */
#define RIO_THROTTLE_RX 0x2000000	/* RX needs to be throttled. */

	unsigned long Config;		/* FLAGS for NOREAD.... */
#define	RIO_NOREAD	0x0001		/* Are not allowed to read port */
#define	RIO_NOWRITE	0x0002		/* Are not allowed to write port */
#define	RIO_NOXPRINT	0x0004		/* Are not allowed to xprint port */
#define	RIO_NOMASK	0x0007		/* All not allowed things */
#define RIO_IXANY	0x0008		/* Port is allowed ixany */
#define	RIO_MODEM	0x0010		/* Stream is a modem device */
#define	RIO_IXON	0x0020		/* Port is allowed ixon */
#define RIO_WAITDRAIN	0x0040		/* Wait for port to completely drain */
#define RIO_MAP_50_TO_50	0x0080	/* Map 50 baud to 50 baud */
#define RIO_MAP_110_TO_110	0x0100	/* Map 110 baud to 110 baud */

/*
** 15.10.1998 ARG - ESIL 0761 prt fix
** As LynxOS does not appear to support Hardware Flow Control .....
** Define our own flow control flags in 'Config'.
*/
#define RIO_CTSFLOW	0x0200		/* RIO's own CTSFLOW flag */
#define RIO_RTSFLOW	0x0400		/* RIO's own RTSFLOW flag */


	struct PHB __iomem *PhbP;	/* pointer to PHB for port */
	u16 __iomem *TxAdd;		/* Add packets here */
	u16 __iomem *TxStart;		/* Start of add array */
	u16 __iomem *TxEnd;		/* End of add array */
	u16 __iomem *RxRemove;		/* Remove packets here */
	u16 __iomem *RxStart;		/* Start of remove array */
	u16 __iomem *RxEnd;		/* End of remove array */
	unsigned int RtaUniqueNum;	/* Unique number of RTA */
	unsigned short PortState;	/* status of port */
	unsigned short ModemState;	/* status of modem lines */
	unsigned long ModemLines;	/* Modem bits sent to RTA */
	unsigned char CookMode;		/* who expands CR/LF? */
	unsigned char ParamSem;		/* Prevent write during param */
	unsigned char Mapped;		/* if port mapped onto host */
	unsigned char SecondBlock;	/* if port belongs to 2nd block
				   		of 16 port RTA */
	unsigned char InUse;		/* how many pre-emptive cmds */
	unsigned char Lock;		/* if params locked */
	unsigned char Store;		/* if params stored across closes */
	unsigned char FirstOpen;	/* TRUE if first time port opened */
	unsigned char FlushCmdBodge;	/* if doing a (non)flush */
	unsigned char MagicFlags;	/* require intr processing */
#define	MAGIC_FLUSH	0x01		/* mirror of WflushFlag */
#define	MAGIC_REBOOT	0x02		/* RTA re-booted, re-open ports */
#define	MORE_OUTPUT_EYGOR 0x04		/* riotproc failed to empty clists */
	unsigned char WflushFlag;	/* 1 How many WFLUSHs active */
/*
** Transparent print stuff
*/
	struct Xprint {
#ifndef MAX_XP_CTRL_LEN
#define MAX_XP_CTRL_LEN		16	/* ALSO IN DAEMON.H */
#endif
		unsigned int XpCps;
		char XpOn[MAX_XP_CTRL_LEN];
		char XpOff[MAX_XP_CTRL_LEN];
		unsigned short XpLen;	/* strlen(XpOn)+strlen(XpOff) */
		unsigned char XpActive;
		unsigned char XpLastTickOk;	/* TRUE if we can process */
#define	XP_OPEN		00001
#define	XP_RUNABLE	00002
		struct ttystatics *XttyP;
	} Xprint;
	unsigned char RxDataStart;
	unsigned char Cor2Copy;		/* copy of COR2 */
	char *Name;			/* points to the Rta's name */
	char *TxRingBuffer;
	unsigned short TxBufferIn;	/* New data arrives here */
	unsigned short TxBufferOut;	/* Intr removes data here */
	unsigned short OldTxBufferOut;	/* Indicates if draining */
	int TimeoutId;			/* Timeout ID */
	unsigned int Debug;
	unsigned char WaitUntilBooted;	/* True if open should block */
	unsigned int statsGather;	/* True if gathering stats */
	unsigned long txchars;		/* Chars transmitted */
	unsigned long rxchars;		/* Chars received */
	unsigned long opens;		/* port open count */
	unsigned long closes;		/* port close count */
	unsigned long ioctls;		/* ioctl count */
	unsigned char LastRxTgl;	/* Last state of rx toggle bit */
	spinlock_t portSem;		/* Lock using this sem */
	int MonitorTstate;		/* Monitoring ? */
	int timeout_id;			/* For calling 100 ms delays */
	int timeout_sem;		/* For calling 100 ms delays */
	int firstOpen;			/* First time open ? */
	char *p;			/* save the global struc here .. */
};

struct ModuleInfo {
	char *Name;
	unsigned int Flags[4];		/* one per port on a module */
};

/*
** This struct is required because trying to grab an entire Port structure
** runs into problems with differing struct sizes between driver and config.
*/
struct PortParams {
	unsigned int Port;
	unsigned long Config;
	unsigned long State;
	struct ttystatics *TtyP;
};

#endif
