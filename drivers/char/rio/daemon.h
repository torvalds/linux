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
**	Module		: daemon.h
**	SID		: 1.3
**	Last Modified	: 11/6/98 11:34:09
**	Retrieved	: 11/6/98 11:34:21
**
**  ident @(#)daemon.h	1.3
**
** -----------------------------------------------------------------------------
*/

#ifndef	__rio_daemon_h__
#define	__rio_daemon_h__

#ifdef SCCS_LABELS
#ifndef lint
static char *_daemon_h_sccs_ = "@(#)daemon.h	1.3";
#endif
#endif


/*
** structures used on /dev/rio
*/

struct Error {
	uint Error;
	uint Entry;
	uint Other;
};

struct DownLoad {
	char *DataP;
	uint Count;
	uint ProductCode;
};

/*
** A few constants....
*/
#ifndef MAX_VERSION_LEN
#define	MAX_VERSION_LEN	256
#endif

#ifndef MAX_XP_CTRL_LEN
#define	MAX_XP_CTRL_LEN 16	/* ALSO IN PORT.H */
#endif

struct PortSetup {
	uint From;		/* Set/Clear XP & IXANY Control from this port.... */
	uint To;		/* .... to this port */
	uint XpCps;		/* at this speed */
	char XpOn[MAX_XP_CTRL_LEN];	/* this is the start string */
	char XpOff[MAX_XP_CTRL_LEN];	/* this is the stop string */
	uchar IxAny;		/* enable/disable IXANY */
	uchar IxOn;		/* enable/disable IXON */
	uchar Lock;		/* lock port params */
	uchar Store;		/* store params across closes */
	uchar Drain;		/* close only when drained */
};

struct LpbReq {
	uint Host;
	uint Link;
	struct LPB *LpbP;
};

struct RupReq {
	uint HostNum;
	uint RupNum;
	struct RUP *RupP;
};

struct PortReq {
	uint SysPort;
	struct Port *PortP;
};

struct StreamInfo {
	uint SysPort;
#if 0
	queue_t RQueue;
	queue_t WQueue;
#else
	int RQueue;
	int WQueue;
#endif
};

struct HostReq {
	uint HostNum;
	struct Host *HostP;
};

struct HostDpRam {
	uint HostNum;
	struct DpRam *DpRamP;
};

struct DebugCtrl {
	uint SysPort;
	uint Debug;
	uint Wait;
};

struct MapInfo {
	uint FirstPort;		/* 8 ports, starting from this (tty) number */
	uint RtaUnique;		/* reside on this RTA (unique number) */
};

struct MapIn {
	uint NumEntries;	/* How many port sets are we mapping? */
	struct MapInfo *MapInfoP;	/* Pointer to (user space) info */
};

struct SendPack {
	unsigned int PortNum;
	unsigned char Len;
	unsigned char Data[PKT_MAX_DATA_LEN];
};

struct SpecialRupCmd {
	struct PKT Packet;
	unsigned short Host;
	unsigned short RupNum;
};

struct IdentifyRta {
	ulong RtaUnique;
	uchar ID;
};

struct KillNeighbour {
	ulong UniqueNum;
	uchar Link;
};

struct rioVersion {
	char version[MAX_VERSION_LEN];
	char relid[MAX_VERSION_LEN];
	int buildLevel;
	char buildDate[MAX_VERSION_LEN];
};


/*
**	RIOC commands are for the daemon type operations
**
** 09.12.1998 ARG - ESIL 0776 part fix
** Definition for 'RIOC' also appears in rioioctl.h, so we'd better do a
** #ifndef here first.
** rioioctl.h also now has #define 'RIO_QUICK_CHECK' as this ioctl is now
** allowed to be used by customers.
*/
#ifndef RIOC
#define	RIOC	('R'<<8)|('i'<<16)|('o'<<24)
#endif

/*
** Boot stuff
*/
#define	RIO_GET_TABLE     (RIOC | 100)
#define RIO_PUT_TABLE     (RIOC | 101)
#define RIO_ASSIGN_RTA    (RIOC | 102)
#define RIO_DELETE_RTA    (RIOC | 103)
#define	RIO_HOST_FOAD	  (RIOC | 104)
#define	RIO_QUICK_CHECK	  (RIOC | 105)
#define RIO_SIGNALS_ON    (RIOC | 106)
#define RIO_SIGNALS_OFF   (RIOC | 107)
#define	RIO_CHANGE_NAME   (RIOC | 108)
#define RIO_DOWNLOAD      (RIOC | 109)
#define	RIO_GET_LOG	  (RIOC | 110)
#define	RIO_SETUP_PORTS   (RIOC | 111)
#define RIO_ALL_MODEM     (RIOC | 112)

/*
** card state, debug stuff
*/
#define	RIO_NUM_HOSTS	  (RIOC | 120)
#define	RIO_HOST_LPB	  (RIOC | 121)
#define	RIO_HOST_RUP	  (RIOC | 122)
#define	RIO_HOST_PORT	  (RIOC | 123)
#define	RIO_PARMS 	  (RIOC | 124)
#define RIO_HOST_REQ	  (RIOC | 125)
#define	RIO_READ_CONFIG	  (RIOC | 126)
#define	RIO_SET_CONFIG	  (RIOC | 127)
#define	RIO_VERSID	  (RIOC | 128)
#define	RIO_FLAGS	  (RIOC | 129)
#define	RIO_SETDEBUG	  (RIOC | 130)
#define	RIO_GETDEBUG	  (RIOC | 131)
#define	RIO_READ_LEVELS   (RIOC | 132)
#define	RIO_SET_FAST_BUS  (RIOC | 133)
#define	RIO_SET_SLOW_BUS  (RIOC | 134)
#define	RIO_SET_BYTE_MODE (RIOC | 135)
#define	RIO_SET_WORD_MODE (RIOC | 136)
#define RIO_STREAM_INFO   (RIOC | 137)
#define	RIO_START_POLLER  (RIOC | 138)
#define	RIO_STOP_POLLER   (RIOC | 139)
#define	RIO_LAST_ERROR    (RIOC | 140)
#define	RIO_TICK	  (RIOC | 141)
#define	RIO_TOCK	  (RIOC | 241)	/* I did this on purpose, you know. */
#define	RIO_SEND_PACKET   (RIOC | 142)
#define	RIO_SET_BUSY	  (RIOC | 143)
#define	SPECIAL_RUP_CMD   (RIOC | 144)
#define	RIO_FOAD_RTA      (RIOC | 145)
#define	RIO_ZOMBIE_RTA    (RIOC | 146)
#define RIO_IDENTIFY_RTA  (RIOC | 147)
#define RIO_KILL_NEIGHBOUR (RIOC | 148)
#define RIO_DEBUG_MEM     (RIOC | 149)
/*
** 150 - 167 used.....   See below
*/
#define RIO_GET_PORT_SETUP (RIOC | 168)
#define RIO_RESUME        (RIOC | 169)
#define	RIO_MESG	(RIOC | 170)
#define	RIO_NO_MESG	(RIOC | 171)
#define	RIO_WHAT_MESG	(RIOC | 172)
#define RIO_HOST_DPRAM	(RIOC | 173)
#define RIO_MAP_B50_TO_50	(RIOC | 174)
#define RIO_MAP_B50_TO_57600	(RIOC | 175)
#define RIO_MAP_B110_TO_110	(RIOC | 176)
#define RIO_MAP_B110_TO_115200	(RIOC | 177)
#define RIO_GET_PORT_PARAMS	(RIOC | 178)
#define RIO_SET_PORT_PARAMS	(RIOC | 179)
#define RIO_GET_PORT_TTY	(RIOC | 180)
#define RIO_SET_PORT_TTY	(RIOC | 181)
#define RIO_SYSLOG_ONLY	(RIOC | 182)
#define RIO_SYSLOG_CONS	(RIOC | 183)
#define RIO_CONS_ONLY	(RIOC | 184)
#define RIO_BLOCK_OPENS	(RIOC | 185)

/*
** 02.03.1999 ARG - ESIL 0820 fix :
** RIOBootMode is no longer use by the driver, so these ioctls
** are now obsolete :
**
#define RIO_GET_BOOT_MODE	(RIOC | 186)
#define RIO_SET_BOOT_MODE	(RIOC | 187)
**
*/

#define RIO_MEM_DUMP	(RIOC | 189)
#define RIO_READ_REGISTER	(RIOC | 190)
#define RIO_GET_MODTYPE	(RIOC | 191)
#define RIO_SET_TIMER	(RIOC | 192)
#define RIO_READ_CHECK	(RIOC | 196)
#define RIO_WAITING_FOR_RESTART	(RIOC | 197)
#define RIO_BIND_RTA	(RIOC | 198)
#define RIO_GET_BINDINGS	(RIOC | 199)
#define RIO_PUT_BINDINGS	(RIOC | 200)

#define	RIO_MAKE_DEV		(RIOC | 201)
#define	RIO_MINOR		(RIOC | 202)

#define	RIO_IDENTIFY_DRIVER	(RIOC | 203)
#define	RIO_DISPLAY_HOST_CFG	(RIOC | 204)


/*
** MAKE_DEV / MINOR stuff
*/
#define	RIO_DEV_DIRECT		0x0000
#define	RIO_DEV_MODEM		0x0200
#define	RIO_DEV_XPRINT		0x0400
#define	RIO_DEV_MASK		0x0600

/*
** port management, xprint stuff
*/
#define	rIOCN(N)	(RIOC|(N))
#define	rIOCR(N,T)	(RIOC|(N))
#define	rIOCW(N,T)	(RIOC|(N))

#define	RIO_GET_XP_ON     rIOCR(150,char[16])	/* start xprint string */
#define	RIO_SET_XP_ON     rIOCW(151,char[16])
#define	RIO_GET_XP_OFF    rIOCR(152,char[16])	/* finish xprint string */
#define	RIO_SET_XP_OFF    rIOCW(153,char[16])
#define	RIO_GET_XP_CPS    rIOCR(154,int)	/* xprint CPS */
#define	RIO_SET_XP_CPS    rIOCW(155,int)
#define RIO_GET_IXANY     rIOCR(156,int)	/* ixany allowed? */
#define RIO_SET_IXANY     rIOCW(157,int)
#define RIO_SET_IXANY_ON  rIOCN(158)	/* allow ixany */
#define RIO_SET_IXANY_OFF rIOCN(159)	/* disallow ixany */
#define RIO_GET_MODEM     rIOCR(160,int)	/* port is modem/direct line? */
#define RIO_SET_MODEM     rIOCW(161,int)
#define RIO_SET_MODEM_ON  rIOCN(162)	/* port is a modem */
#define RIO_SET_MODEM_OFF rIOCN(163)	/* port is direct */
#define RIO_GET_IXON      rIOCR(164,int)	/* ixon allowed? */
#define RIO_SET_IXON      rIOCW(165,int)
#define RIO_SET_IXON_ON   rIOCN(166)	/* allow ixon */
#define RIO_SET_IXON_OFF  rIOCN(167)	/* disallow ixon */

#define RIO_GET_SIVIEW	  ((('s')<<8) | 106)	/* backwards compatible with SI */

#define	RIO_IOCTL_UNKNOWN	-2

#endif
