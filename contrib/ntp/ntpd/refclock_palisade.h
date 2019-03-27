/*
 * This software was developed by the Software and Component Technologies
 * group of Trimble Navigation, Ltd.
 *
 * Copyright (c) 1997, 1998, 1999, 2000   Trimble Navigation Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Trimble Navigation, Ltd.
 * 4. The name of Trimble Navigation Ltd. may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TRIMBLE NAVIGATION LTD. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL TRIMBLE NAVIGATION LTD. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * refclock_palisade - clock driver for the Trimble Palisade GPS
 * timing receiver
 *
 * For detailed information on this program, please refer to the html 
 * Refclock 29 page accompanying the NTP distribution.
 *
 * for questions / bugs / comments, contact:
 * sven_dietrich@trimble.com
 *
 * Sven-Thorsten Dietrich
 * 645 North Mary Avenue
 * Post Office Box 3642
 * Sunnyvale, CA 94088-3642
 *
 */

#ifndef REFCLOCK_PALISADE_H
#define REFCLOCK_PALISADE_H

#if defined HAVE_SYS_MODEM_H
#include <sys/modem.h>
#ifndef __QNXNTO__
#define TIOCMSET MCSETA
#define TIOCMGET MCGETA
#define TIOCM_RTS MRTS
#endif
#endif

#ifdef HAVE_TERMIOS_H
# ifdef TERMIOS_NEEDS__SVID3
#  define _SVID3
# endif
# include <termios.h>
# include <sys/stat.h>
# ifdef TERMIOS_NEEDS__SVID3
#  undef _SVID3
# endif
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_control.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"

/*
 * GPS Definitions
 */
#define	DESCRIPTION	"Trimble Palisade GPS" /* Long name */
#define	PRECISION	(-20)	/* precision assumed (about 1 us) */
#define	REFID		"GPS\0"	/* reference ID */
#define TRMB_MINPOLL    4	/* 16 seconds */
#define TRMB_MAXPOLL	5	/* 32 seconds */

/*
 * I/O Definitions
 */
#define	DEVICE		"/dev/palisade%d" 	/* device name and unit */
#define	SPEED232	B9600		  	/* uart speed (9600 baud) */

/*
 * TSIP Report Definitions
 */
#define LENCODE_8F0B	74	/* Length of TSIP 8F-0B Packet & header */
#define LENCODE_NTP     22	/* Length of Palisade NTP Packet */

#define LENCODE_8FAC    68      /* Length of Thunderbolt 8F-AC Position Packet*/
#define LENCODE_8FAB    17      /* Length of Thunderbolt Primary Timing Packet*/

/* Allowed Sub-Packet ID's */
#define PACKET_8F0B	0x0B
#define PACKET_NTP	0xAD

/* Thunderbolt Packets */
#define PACKET_8FAC     0xAC	/* Supplementary Thunderbolt Time Packet */
#define PACKET_8FAB     0xAB	/* Primary Thunderbolt Time Packet */
#define PACKET_6D	0x6D	/* Supplementary Thunderbolt Tracking Stats */
#define PACKET_41	0x41	/* Thunderbolt I dont know what this packet is, it's not documented on my manual*/

/* Acutime Packets */
#define PACKET_41A      0x41    /* GPS time */
#define PACKET_46       0x46    /* Receiver Health */

#define DLE 0x10
#define ETX 0x03

/* parse states */
#define TSIP_PARSED_EMPTY       0	
#define TSIP_PARSED_FULL        1
#define TSIP_PARSED_DLE_1       2
#define TSIP_PARSED_DATA        3
#define TSIP_PARSED_DLE_2       4

/* 
 * Leap-Insert and Leap-Delete are encoded as follows:
 * 	PALISADE_UTC_TIME set   and PALISADE_LEAP_PENDING set: INSERT leap
 */

#define PALISADE_LEAP_INPROGRESS 0x08 /* This is the leap flag			*/
#define PALISADE_LEAP_WARNING    0x04 /* GPS Leap Warning (see ICD-200) */
#define PALISADE_LEAP_PENDING    0x02 /* Leap Pending (24 hours)		*/
#define PALISADE_UTC_TIME        0x01 /* UTC time available				*/

#define mb(_X_) (up->rpt_buf[(_X_ + 1)]) /* shortcut for buffer access	*/

/* Conversion Definitions */
#define GPS_PI 		(3.1415926535898)
#define	R2D		(180.0/GPS_PI)

/*
 * Structure for build data packets for send (thunderbolt uses it only)
 * taken from Markus Prosch
 */
struct packettx
{
	short	size;
	u_char *data;
};

/*
 * Palisade unit control structure.
 */
struct palisade_unit {
	short		unit;		/* NTP refclock unit number */
	int 		polled;		/* flag to detect noreplies */
	char		leap_status;	/* leap second flag */
	char		rpt_status;	/* TSIP Parser State */
	short 		rpt_cnt;	/* TSIP packet length so far */
	char 		rpt_buf[BMAX]; 	/* packet assembly buffer */
	int		type;		/* Clock mode type */
	int		month;		/* for LEAP filter */
};

/*
 * Function prototypes
 */

static	int	palisade_start		(int, struct peer *);
static	void	palisade_shutdown	(int, struct peer *);
static	void	palisade_receive	(struct peer *);
static	void	palisade_poll		(int, struct peer *);
static	void 	palisade_io		(struct recvbuf *);
int 		palisade_configure	(int, struct peer *);
int 		TSIP_decode		(struct peer *);
long		HW_poll			(struct refclockproc *);
static	double	getdbl 			(u_char *);
static	short	getint 			(u_char *);
static	int32	getlong			(u_char *);


#ifdef PALISADE_SENDCMD_RESURRECTED
static  void	sendcmd			(struct packettx *buffer, int c);
#endif
static  void	sendsupercmd		(struct packettx *buffer, int c1, int c2);
static  void	sendbyte		(struct packettx *buffer, int b);
static  void	sendint			(struct packettx *buffer, int a);
static  int	sendetx			(struct packettx *buffer, int fd);
static  void	init_thunderbolt	(int fd);
static  void	init_acutime		(int fd);

#endif /* REFCLOCK_PALISADE_H */
