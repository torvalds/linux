/*
 * refclock_jjy - clock driver for JJY receivers
 */

/**********************************************************************/
/*								      */
/*  Copyright (C) 2001-2015, Takao Abe.  All rights reserved.	      */
/*								      */
/*  Permission to use, copy, modify, and distribute this software     */
/*  and its documentation for any purpose is hereby granted	      */
/*  without fee, provided that the following conditions are met:      */
/*								      */
/*  One retains the entire copyright notice properly, and both the    */
/*  copyright notice and this license. in the documentation and/or    */
/*  other materials provided with the distribution.		      */
/*								      */
/*  This software and the name of the author must not be used to      */
/*  endorse or promote products derived from this software without    */
/*  prior written permission.					      */
/*								      */
/*  THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT EXPRESSED OR IMPLIED    */
/*  WARRANTIES OF ANY KIND, INCLUDING, BUT NOT LIMITED TO, THE	      */
/*  IMPLIED WARRANTIES OF MERCHANTABLILITY AND FITNESS FOR A	      */
/*  PARTICULAR PURPOSE.						      */
/*  IN NO EVENT SHALL THE AUTHOR TAKAO ABE BE LIABLE FOR ANY DIRECT,  */
/*  INDIRECT, GENERAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES   */
/*  ( INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE	      */
/*  GOODS OR SERVICES; LOSS OF USE, DATA OR PROFITS; OR BUSINESS      */
/*  INTERRUPTION ) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     */
/*  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT ( INCLUDING	      */
/*  NEGLIGENCE OR OTHERWISE ) ARISING IN ANY WAY OUT OF THE USE OF    */
/*  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */
/*								      */
/*  This driver is developed in my private time, and is opened as     */
/*  voluntary contributions for the NTP.			      */
/*  The manufacturer of the JJY receiver has not participated in      */
/*  a development of this driver.				      */
/*  The manufacturer does not warrant anything about this driver,     */
/*  and is not liable for anything about this driver.		      */
/*								      */
/**********************************************************************/
/*								      */
/*  Author     Takao Abe					      */
/*  Email      takao_abe@xurb.jp				      */
/*  Homepage   http://www.bea.hi-ho.ne.jp/abetakao/		      */
/*								      */
/*  The email address abetakao@bea.hi-ho.ne.jp is never read	      */
/*  from 2010, because a few filtering rule are provided by the	      */
/*  "hi-ho.ne.jp", and lots of spam mail are reached.		      */
/*  New email address for supporting the refclock_jjy is	      */
/*  takao_abe@xurb.jp						      */
/*								      */
/**********************************************************************/
/*								      */
/*  History							      */
/*								      */
/*  2001/07/15							      */
/*    [New]    Support the Tristate Ltd. JJY receiver		      */
/*								      */
/*  2001/08/04							      */
/*    [Change] Log to clockstats even if bad reply		      */
/*    [Fix]    PRECISION = (-3) (about 100 ms)			      */
/*    [Add]    Support the C-DEX Co.Ltd. JJY receiver		      */
/*								      */
/*  2001/12/04							      */
/*    [Fix]    C-DEX JST2000 ( fukusima@goto.info.waseda.ac.jp )      */
/*								      */
/*  2002/07/12							      */
/*    [Fix]    Portability for FreeBSD ( patched by the user )	      */
/*								      */
/*  2004/10/31							      */
/*    [Change] Command send timing for the Tristate Ltd. JJY receiver */
/*	       JJY-01 ( Firmware version 2.01 )			      */
/*	       Thanks to Andy Taki for testing under FreeBSD	      */
/*								      */
/*  2004/11/28							      */
/*    [Add]    Support the Echo Keisokuki LT-2000 receiver	      */
/*								      */
/*  2006/11/04							      */
/*    [Fix]    C-DEX JST2000					      */
/*	       Thanks to Hideo Kuramatsu for the patch		      */
/*								      */
/*  2009/04/05							      */
/*    [Add]    Support the CITIZEN T.I.C JJY-200 receiver	      */
/*								      */
/*  2010/11/20							      */
/*    [Change] Bug 1618 ( Harmless )				      */
/*	       Code clean up ( Remove unreachable codes ) in	      */
/*	       jjy_start()					      */
/*    [Change] Change clockstats format of the Tristate JJY01/02      */
/*	       Issues more command to get the status of the receiver  */
/*	       when "fudge 127.127.40.X flag1 1" is specified	      */
/*	       ( DATE,STIM -> DCST,STUS,DATE,STIM )		      */
/*								      */
/*  2011/04/30							      */
/*    [Add]    Support the Tristate Ltd. TS-GPSclock-01		      */
/*								      */
/*  2015/03/29							      */
/*    [Add]    Support the Telephone JJY			      */
/*    [Change] Split the start up routine into each JJY receivers.    */
/*             Change raw data internal bufferring process            */
/*             Change over midnight handling of TS-JJY01 and TS-GPS01 */
/*             to put DATE command between before and after TIME's.   */
/*             Unify the writing clockstats of all JJY receivers.     */
/*								      */
/*  2015/05/15							      */
/*    [Add]    Support the SEIKO TIME SYSTEMS TDC-300		      */
/*								      */
/*  2016/05/08							      */
/*    [Fix]    C-DEX JST2000                                          */
/*             Thanks to Mr. Kuramatsu for the report and the patch.  */
/*								      */
/*  2017/04/30							      */
/*    [Change] Avoid a wrong report of the coverity static analysis   */
/*             tool. ( The code is harmless and has no bug. )	      */
/*             teljjy_conn_send()				      */
/*								      */
/**********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_JJY)

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_tty.h"
#include "ntp_refclock.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"

/**********************************************************************/

/*
 * Interface definitions
 */
#define	DEVICE  	"/dev/jjy%d"	/* device name and unit */
#define	SPEED232_TRISTATE_JJY01		B9600   /* UART speed (9600 baud) */
#define	SPEED232_CDEX_JST2000		B9600   /* UART speed (9600 baud) */
#define	SPEED232_ECHOKEISOKUKI_LT2000	B9600   /* UART speed (9600 baud) */
#define	SPEED232_CITIZENTIC_JJY200	B4800   /* UART speed (4800 baud) */
#define	SPEED232_TRISTATE_GPSCLOCK01	B38400  /* USB  speed (38400 baud) */
#define	SPEED232_SEIKO_TIMESYS_TDC_300	B2400   /* UART speed (2400 baud) */
#define	SPEED232_TELEPHONE		B2400   /* UART speed (4800 baud) */
#define	REFID   	"JJY"		/* reference ID */
#define	DESCRIPTION	"JJY Receiver"
#define	PRECISION	(-3)		/* precision assumed (about 100 ms) */

/*
 * JJY unit control structure
 */

struct jjyRawDataBreak {
	const char *	pString ;
	int 		iLength ;
} ;

#define	MAX_TIMESTAMP	6
#define	MAX_RAWBUF   	100
#define	MAX_LOOPBACK	5

struct jjyunit {
/* Set up by the function "jjy_start_xxxxxxxx" */
	char	unittype ;	    /* UNITTYPE_XXXXXXXXXX */
	short   operationmode ;	    /* Echo Keisokuki LT-2000 */
	int 	linespeed ;         /* SPEED232_XXXXXXXXXX */
	short	linediscipline ;    /* LDISC_CLK or LDISC_RAW */
/* Receiving data */
	char	bInitError ;        /* Set by jjy_start if any error during initialization */
	short	iProcessState ;     /* JJY_PROCESS_STATE_XXXXXX */
	char	bReceiveFlag ;      /* Set and reset by jjy_receive */
	char	bLineError ;	    /* Reset by jjy_poll / Set by jjy_receive_xxxxxxxx*/
	short	iCommandSeq ;       /* 0:Idle  Non-Zero:Issued */
	short	iReceiveSeq ;
	int 	iLineCount ;
	int 	year, month, day, hour, minute, second, msecond ;
	int 	leapsecond ;
	int 	iTimestampCount ;   /* TS-JJY01, TS-GPS01, Telephone-JJY */
	int 	iTimestamp [ MAX_TIMESTAMP ] ;  /* Serial second ( 0 - 86399 ) */
/* LDISC_RAW only */
	char	sRawBuf [ MAX_RAWBUF ] ;
	int 	iRawBufLen ;
	struct	jjyRawDataBreak *pRawBreak ;
	char	bWaitBreakString ;
	char	sLineBuf [ MAX_RAWBUF ] ;
	int 	iLineBufLen ;
	char	sTextBuf [ MAX_RAWBUF ] ;
	int 	iTextBufLen ;
	char	bSkipCntrlCharOnly ;
/* Telephone JJY auto measurement of the loopback delay */
	char	bLoopbackMode ;
	short	iLoopbackCount ;
	struct	timeval sendTime[MAX_LOOPBACK], delayTime[MAX_LOOPBACK] ;
	char	bLoopbackTimeout[MAX_LOOPBACK] ;
	short	iLoopbackValidCount ;
/* Telephone JJY timer */
	short	iTeljjySilentTimer ;
	short	iTeljjyStateTimer ;
/* Telephone JJY control finite state machine */
	short	iClockState ;
	short	iClockEvent ;
	short	iClockCommandSeq ;
/* Modem timer */
	short	iModemSilentCount ;
	short	iModemSilentTimer ;
	short	iModemStateTimer ;
/* Modem control finite state machine */
	short	iModemState ;
	short	iModemEvent ;
	short	iModemCommandSeq ;
};

#define	UNITTYPE_TRISTATE_JJY01		1
#define	UNITTYPE_CDEX_JST2000		2
#define	UNITTYPE_ECHOKEISOKUKI_LT2000  	3
#define	UNITTYPE_CITIZENTIC_JJY200  	4
#define	UNITTYPE_TRISTATE_GPSCLOCK01	5
#define	UNITTYPE_SEIKO_TIMESYS_TDC_300	6
#define	UNITTYPE_TELEPHONE		100

#define	JJY_PROCESS_STATE_IDLE   	0
#define	JJY_PROCESS_STATE_POLL   	1
#define	JJY_PROCESS_STATE_RECEIVE	2
#define	JJY_PROCESS_STATE_DONE   	3
#define	JJY_PROCESS_STATE_ERROR  	4

/**********************************************************************/

/*
 *  Function calling structure
 *
 *  jjy_start
 *   |--  jjy_start_tristate_jjy01
 *   |--  jjy_start_cdex_jst2000
 *   |--  jjy_start_echokeisokuki_lt2000
 *   |--  jjy_start_citizentic_jjy200
 *   |--  jjy_start_tristate_gpsclock01
 *   |--  jjy_start_seiko_tsys_tdc_300
 *   |--  jjy_start_telephone
 *
 *  jjy_shutdown
 *
 *  jjy_poll
 *   |--  jjy_poll_tristate_jjy01
 *   |--  jjy_poll_cdex_jst2000
 *   |--  jjy_poll_echokeisokuki_lt2000
 *   |--  jjy_poll_citizentic_jjy200
 *   |--  jjy_poll_tristate_gpsclock01
 *   |--  jjy_poll_seiko_tsys_tdc_300
 *   |--  jjy_poll_telephone
 *         |--  teljjy_control
 *               |--  teljjy_XXXX_YYYY ( XXXX_YYYY is an event handler name. )
 *                     |--  modem_connect
 *                           |--  modem_control
 *                                 |--  modem_XXXX_YYYY ( XXXX_YYYY is an event handler name. )
 *
 *  jjy_receive
 *   |
 *   |--  jjy_receive_tristate_jjy01
 *   |     |--  jjy_synctime
 *   |--  jjy_receive_cdex_jst2000
 *   |     |--  jjy_synctime
 *   |--  jjy_receive_echokeisokuki_lt2000
 *   |     |--  jjy_synctime
 *   |--  jjy_receive_citizentic_jjy200
 *   |     |--  jjy_synctime
 *   |--  jjy_receive_tristate_gpsclock01
 *   |     |--  jjy_synctime
 *   |--  jjy_receive_seiko_tsys_tdc_300
 *   |     |--  jjy_synctime
 *   |--  jjy_receive_telephone
 *         |--  modem_receive
 *         |     |--  modem_control
 *         |           |--  modem_XXXX_YYYY ( XXXX_YYYY is an event handler name. )
 *         |--  teljjy_control
 *               |--  teljjy_XXXX_YYYY ( XXXX_YYYY is an event handler name. )
 *                     |--  jjy_synctime
 *                     |--  modem_disconnect
 *                           |--  modem_control
 *                                 |--  modem_XXXX_YYYY ( XXXX_YYYY is an event handler name. )
 *
 *  jjy_timer
 *   |--  jjy_timer_telephone
 *         |--  modem_timer
 *         |     |--  modem_control
 *         |           |--  modem_XXXX_YYYY ( XXXX_YYYY is an event handler name. )
 *         |--  teljjy_control
 *               |--  teljjy_XXXX_YYYY ( XXXX_YYYY is an event handler name. )
 *                     |--  modem_disconnect
 *                           |--  modem_control
 *                                 |--  modem_XXXX_YYYY ( XXXX_YYYY is an event handler name. )
 *
 * Function prototypes
 */

static	int 	jjy_start			(int, struct peer *);
static	int 	jjy_start_tristate_jjy01	(int, struct peer *, struct jjyunit *);
static	int 	jjy_start_cdex_jst2000		(int, struct peer *, struct jjyunit *);
static	int 	jjy_start_echokeisokuki_lt2000	(int, struct peer *, struct jjyunit *);
static	int 	jjy_start_citizentic_jjy200	(int, struct peer *, struct jjyunit *);
static	int 	jjy_start_tristate_gpsclock01	(int, struct peer *, struct jjyunit *);
static	int 	jjy_start_seiko_tsys_tdc_300	(int, struct peer *, struct jjyunit *);
static	int 	jjy_start_telephone		(int, struct peer *, struct jjyunit *);

static	void	jjy_shutdown			(int, struct peer *);

static	void	jjy_poll		    	(int, struct peer *);
static	void	jjy_poll_tristate_jjy01	    	(int, struct peer *);
static	void	jjy_poll_cdex_jst2000	    	(int, struct peer *);
static	void	jjy_poll_echokeisokuki_lt2000	(int, struct peer *);
static	void	jjy_poll_citizentic_jjy200	(int, struct peer *);
static	void	jjy_poll_tristate_gpsclock01	(int, struct peer *);
static	void	jjy_poll_seiko_tsys_tdc_300	(int, struct peer *);
static	void	jjy_poll_telephone		(int, struct peer *);

static	void	jjy_receive			(struct recvbuf *);
static	int 	jjy_receive_tristate_jjy01	(struct recvbuf *);
static	int 	jjy_receive_cdex_jst2000	(struct recvbuf *);
static	int 	jjy_receive_echokeisokuki_lt2000 (struct recvbuf *);
static  int 	jjy_receive_citizentic_jjy200	(struct recvbuf *);
static	int 	jjy_receive_tristate_gpsclock01	(struct recvbuf *);
static	int 	jjy_receive_seiko_tsys_tdc_300	(struct recvbuf *);
static	int 	jjy_receive_telephone		(struct recvbuf *);

static	void	jjy_timer			(int, struct peer *);
static	void	jjy_timer_telephone		(int, struct peer *);

static	void	jjy_synctime			( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	void	jjy_write_clockstats		( struct peer *, int, const char* ) ;

static	int 	getRawDataBreakPosition		( struct jjyunit *, int ) ;

static	short	getModemState			( struct jjyunit * ) ;
static	int 	isModemStateConnect		( short ) ;
static	int 	isModemStateDisconnect		( short ) ;
static	int 	isModemStateTimerOn		( struct jjyunit * ) ;
static	void	modem_connect			( int, struct peer * ) ;
static	void	modem_disconnect		( int, struct peer * ) ;
static	int 	modem_receive			( struct recvbuf * ) ;
static	void	modem_timer			( int, struct peer * );

static	void	printableString ( char*, int, const char*, int ) ;

/*
 * Transfer vector
 */
struct	refclock refclock_jjy = {
	jjy_start,	/* start up driver */
	jjy_shutdown,	/* shutdown driver */
	jjy_poll,	/* transmit poll message */
	noentry,	/* not used */
	noentry,	/* not used */
	noentry,	/* not used */
	jjy_timer	/* 1 second interval timer */
};

/*
 * Start up driver return code
 */
#define	RC_START_SUCCESS	1
#define	RC_START_ERROR		0

/*
 * Local constants definition
 */

#define	MAX_LOGTEXT	100

#ifndef	TRUE
#define	TRUE	(0==0)
#endif
#ifndef	FALSE
#define	FALSE	(!TRUE)
#endif

/* Local constants definition for the return code of the jjy_receive_xxxxxxxx */

#define	JJY_RECEIVE_DONE	0
#define	JJY_RECEIVE_SKIP	1
#define	JJY_RECEIVE_UNPROCESS	2
#define	JJY_RECEIVE_WAIT	3
#define	JJY_RECEIVE_ERROR	4

/* Local constants definition for the 2nd parameter of the jjy_write_clockstats */

#define	JJY_CLOCKSTATS_MARK_NONE	0
#define	JJY_CLOCKSTATS_MARK_JJY 	1
#define	JJY_CLOCKSTATS_MARK_SEND	2
#define	JJY_CLOCKSTATS_MARK_RECEIVE	3
#define	JJY_CLOCKSTATS_MARK_INFORMATION	4
#define	JJY_CLOCKSTATS_MARK_ATTENTION	5
#define	JJY_CLOCKSTATS_MARK_WARNING	6
#define	JJY_CLOCKSTATS_MARK_ERROR	7
#define	JJY_CLOCKSTATS_MARK_BUG 	8

/* Local constants definition for the clockstats messages */

#define	JJY_CLOCKSTATS_MESSAGE_ECHOBACK         	"* Echoback"
#define	JJY_CLOCKSTATS_MESSAGE_IGNORE_REPLY     	"* Ignore replay : [%s]"
#define	JJY_CLOCKSTATS_MESSAGE_OVER_MIDNIGHT_2  	"* Over midnight : timestamp=%d, %d"
#define	JJY_CLOCKSTATS_MESSAGE_OVER_MIDNIGHT_3  	"* Over midnight : timestamp=%d, %d, %d"
#define	JJY_CLOCKSTATS_MESSAGE_TIMESTAMP_UNSURE 	"* Unsure timestamp : %s"
#define	JJY_CLOCKSTATS_MESSAGE_LOOPBACK_DELAY   	"* Loopback delay : %d.%03d mSec."
#define	JJY_CLOCKSTATS_MESSAGE_DELAY_ADJUST     	"* Delay adjustment : %d mSec. ( valid=%hd/%d )"
#define	JJY_CLOCKSTATS_MESSAGE_DELAY_UNADJUST   	"* Delay adjustment : None ( valid=%hd/%d )"

#define	JJY_CLOCKSTATS_MESSAGE_UNEXPECTED_REPLY     	"# Unexpected reply : [%s]"
#define	JJY_CLOCKSTATS_MESSAGE_INVALID_LENGTH     	"# Invalid length : length=%d"
#define	JJY_CLOCKSTATS_MESSAGE_TOO_MANY_REPLY     	"# Too many reply : count=%d"
#define	JJY_CLOCKSTATS_MESSAGE_INVALID_REPLY      	"# Invalid reply : [%s]"
#define	JJY_CLOCKSTATS_MESSAGE_SLOW_REPLY_2       	"# Slow reply : timestamp=%d, %d"
#define	JJY_CLOCKSTATS_MESSAGE_SLOW_REPLY_3       	"# Slow reply : timestamp=%d, %d, %d"
#define	JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATE	"# Invalid date : rc=%d year=%d month=%d day=%d"
#define	JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_TIME	"# Invalid time : rc=%d hour=%d minute=%d second=%d"
#define	JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATETIME	"# Invalid time : rc=%d year=%d month=%d day=%d hour=%d minute=%d second=%d"
#define	JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_LEAP	"# Invalid leap : leapsecond=[%s]"
#define	JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_STATUS	"# Invalid status : status=[%s]"

/* Debug print macro */

#ifdef	DEBUG
#define	DEBUG_PRINTF_JJY_RECEIVE(sFunc,iLen)	{ if ( debug ) { printf ( "refclock_jjy.c : %s : iProcessState=%d bLineError=%d iCommandSeq=%d iLineCount=%d iTimestampCount=%d iLen=%d\n", sFunc, up->iProcessState, up->bLineError, up->iCommandSeq, up->iLineCount, up->iTimestampCount, iLen ) ; } }
#else
#define	DEBUG_PRINTF_JJY_RECEIVE(sFunc,iLen)
#endif

/**************************************************************************************************/
/*  jjy_start - open the devices and initialize data for processing                               */
/**************************************************************************************************/
static int
jjy_start ( int unit, struct peer *peer )
{

	struct	refclockproc *pp ;
	struct	jjyunit      *up ;
	int 	rc ;
	int 	fd ;
	char	sDeviceName [ sizeof(DEVICE) + 10 ], sLog [ 60 ] ;

#ifdef DEBUG
	if ( debug ) {
		printf( "refclock_jjy.c : jjy_start : %s  mode=%d  dev=%s  unit=%d\n",
			 ntoa(&peer->srcadr), peer->ttl, DEVICE, unit ) ;
	}
#endif

	/* Allocate memory for the unit structure */
	up = emalloc( sizeof(*up) ) ;
	if ( up == NULL ) {
		msyslog ( LOG_ERR, "refclock_jjy.c : jjy_start : emalloc" ) ;
		return RC_START_ERROR ;
	}
	memset ( up, 0, sizeof(*up) ) ;

	up->bInitError = FALSE ;
	up->iProcessState = JJY_PROCESS_STATE_IDLE ;
	up->bReceiveFlag = FALSE ;
	up->iCommandSeq = 0 ;
	up->iLineCount = 0 ;
	up->iTimestampCount = 0 ;
	up->bWaitBreakString = FALSE ;
	up->iRawBufLen = up->iLineBufLen = up->iTextBufLen = 0 ;
	up->bSkipCntrlCharOnly = TRUE ;

	/* Set up the device name */
	snprintf( sDeviceName, sizeof(sDeviceName), DEVICE, unit ) ;

	snprintf( sLog, sizeof(sLog), "mode=%d dev=%s", peer->ttl, sDeviceName ) ;
	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, sLog ) ;

	/*
	 * peer->ttl is a mode number specified by "127.127.40.X mode N" in the ntp.conf
	 */
	switch ( peer->ttl ) {
	case 0 :
	case 1 :
		rc = jjy_start_tristate_jjy01 ( unit, peer, up ) ;
		break ;
	case 2 :
		rc = jjy_start_cdex_jst2000 ( unit, peer, up ) ;
		break ;
	case 3 :
		rc = jjy_start_echokeisokuki_lt2000 ( unit, peer, up ) ;
		break ;
	case 4 :
		rc = jjy_start_citizentic_jjy200 ( unit, peer, up ) ;
		break ;
	case 5 :
		rc = jjy_start_tristate_gpsclock01 ( unit, peer, up ) ;
		break ;
	case 6 :
		rc = jjy_start_seiko_tsys_tdc_300 ( unit, peer, up ) ;
		break ;
	case 100 :
		rc = jjy_start_telephone ( unit, peer, up ) ;
		break ;
	default :
		if ( 101 <= peer->ttl && peer->ttl <= 180 ) {
			rc = jjy_start_telephone ( unit, peer, up ) ;
		} else {
			msyslog ( LOG_ERR, "JJY receiver [ %s mode %d ] : Unsupported mode",
				  ntoa(&peer->srcadr), peer->ttl ) ;
			free ( (void*) up ) ;
		return RC_START_ERROR ;
		}
	}

	if ( rc != 0 ) {
		msyslog ( LOG_ERR, "JJY receiver [ %s mode %d ] : Initialize error",
			  ntoa(&peer->srcadr), peer->ttl ) ;
		free ( (void*) up ) ;
		return RC_START_ERROR ;
	}

	/* Open the device */
	fd = refclock_open ( sDeviceName, up->linespeed, up->linediscipline ) ;
	if ( fd <= 0 ) {
		free ( (void*) up ) ;
		return RC_START_ERROR ;
	}

	/*
	 * Initialize variables
	 */
	pp = peer->procptr ;

	pp->clockdesc	= DESCRIPTION ;
	pp->unitptr       = up ;
	pp->io.clock_recv = jjy_receive ;
	pp->io.srcclock   = peer ;
	pp->io.datalen	  = 0 ;
	pp->io.fd	  = fd ;
	if ( ! io_addclock(&pp->io) ) {
		close ( fd ) ;
		pp->io.fd = -1 ;
		free ( up ) ;
		pp->unitptr = NULL ;
		return RC_START_ERROR ;
	}
	memcpy( (char*)&pp->refid, REFID, strlen(REFID) ) ;

	peer->precision = PRECISION ;

	snprintf( sLog, sizeof(sLog), "minpoll=%d maxpoll=%d", peer->minpoll, peer->maxpoll ) ;
	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, sLog ) ;

	return RC_START_SUCCESS ;

}

/**************************************************************************************************/
/*  jjy_shutdown - shutdown the clock                                                             */
/**************************************************************************************************/
static void
jjy_shutdown ( int unit, struct peer *peer )
{

	struct jjyunit	    *up;
	struct refclockproc *pp;

	char	sLog [ 60 ] ;

	pp = peer->procptr ;
	up = pp->unitptr ;
	if ( -1 != pp->io.fd ) {
		io_closeclock ( &pp->io ) ;
	}
	if ( NULL != up ) {
		free ( up ) ;
	}

	snprintf( sLog, sizeof(sLog), "JJY stopped. unit=%d mode=%d", unit, peer->ttl ) ;
	record_clock_stats( &peer->srcadr, sLog ) ;

}

/**************************************************************************************************/
/*  jjy_receive - receive data from the serial interface                                          */
/**************************************************************************************************/
static void
jjy_receive ( struct recvbuf *rbufp )
{
#ifdef DEBUG
	static const char *sFunctionName = "jjy_receive" ;
#endif

	struct jjyunit	    *up ;
	struct refclockproc *pp ;
	struct peer	    *peer;

	l_fp	tRecvTimestamp;		/* arrival timestamp */
	int 	rc ;
	char	*pBuf, sLogText [ MAX_LOGTEXT ] ;
	size_t 	iLen, iCopyLen ;
	int 	i, j, iReadRawBuf, iBreakPosition ;

	/*
	 * Initialize pointers and read the timecode and timestamp
	 */
	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	/*
	 * Get next input line
	 */
	if ( up->linediscipline == LDISC_RAW ) {

		pp->lencode  = refclock_gtraw ( rbufp, pp->a_lastcode, BMAX-1, &tRecvTimestamp ) ;
		/* 3rd argument can be BMAX, but the coverity scan tool claim "Memory - corruptions  (OVERRUN)" */
		/* "a_lastcode" is defined as "char a_lastcode[BMAX]" in the ntp_refclock.h */
		/* To avoid its claim, pass the value BMAX-1. */

		/*
		 * Append received charaters to temporary buffer
		 */
		for ( i = 0 ;
		      i < pp->lencode && up->iRawBufLen < MAX_RAWBUF - 2 ;
		      i ++ , up->iRawBufLen ++ ) {
			up->sRawBuf[up->iRawBufLen] = pp->a_lastcode[i] ;
		}
		up->sRawBuf[up->iRawBufLen] = 0 ;


	} else {

		pp->lencode  = refclock_gtlin ( rbufp, pp->a_lastcode, BMAX, &tRecvTimestamp ) ;

	}
#ifdef DEBUG
	printf( "\nrefclock_jjy.c : %s : Len=%d  ", sFunctionName, pp->lencode ) ;
	for ( i = 0 ; i < pp->lencode ; i ++ ) {
		if ( iscntrl( (u_char)(pp->a_lastcode[i] & 0x7F) ) ) {
			printf( "<x%02X>", pp->a_lastcode[i] & 0xFF ) ;
		} else {
			printf( "%c", pp->a_lastcode[i] ) ;
		}
	}
	printf( "\n" ) ;
#endif

	/*
	 * The reply with <CR><LF> gives a blank line
	 */

	if ( pp->lencode == 0 ) return ;

	/*
	 * Receiving data is not expected
	 */

	if ( up->iProcessState == JJY_PROCESS_STATE_IDLE
	  || up->iProcessState == JJY_PROCESS_STATE_DONE
	  || up->iProcessState == JJY_PROCESS_STATE_ERROR ) {
		/* Discard received data */
		up->iRawBufLen = 0 ;
#ifdef DEBUG
		if ( debug ) {
			printf( "refclock_jjy.c : %s : Discard received data\n", sFunctionName ) ;
		}
#endif
		return ;
	}

	/*
	 * We get down to business
	 */

	pp->lastrec = tRecvTimestamp ;

	up->iLineCount ++ ;

	up->iProcessState = JJY_PROCESS_STATE_RECEIVE ;
	up->bReceiveFlag = TRUE ;

	iReadRawBuf = 0 ;
	iBreakPosition = up->iRawBufLen - 1 ;
	for ( ; up->iProcessState == JJY_PROCESS_STATE_RECEIVE ; ) {

		if ( up->linediscipline == LDISC_RAW ) {

			if ( up->bWaitBreakString ) {
				iBreakPosition = getRawDataBreakPosition( up, iReadRawBuf ) ;
				if ( iBreakPosition == -1 ) {
					/* Break string have not come yet */
					if ( up->iRawBufLen < MAX_RAWBUF - 2
					  || iReadRawBuf > 0 ) {
						/* Temporary buffer is not full */
						break ;
					} else {
						/* Temporary buffer is full */
						iBreakPosition = up->iRawBufLen - 1 ;
					}
				}
			} else {
				iBreakPosition = up->iRawBufLen - 1 ;
			}

			/* Copy charaters from temporary buffer to process buffer */
			up->iLineBufLen = up->iTextBufLen = 0 ;
			for ( i = iReadRawBuf ; i <= iBreakPosition ; i ++ ) {

				/* Copy all characters */
				up->sLineBuf[up->iLineBufLen] = up->sRawBuf[i] ;
				up->iLineBufLen ++ ;

				/* Copy printable characters */
				if ( ! iscntrl( (u_char)up->sRawBuf[i] ) ) {
					up->sTextBuf[up->iTextBufLen] = up->sRawBuf[i] ;
					up->iTextBufLen ++ ;
				}

			}
			up->sLineBuf[up->iLineBufLen] = 0 ;
			up->sTextBuf[up->iTextBufLen] = 0 ;
#ifdef DEBUG
			printf( "refclock_jjy.c : %s : up->iLineBufLen=%d up->iTextBufLen=%d\n",
				 sFunctionName, up->iLineBufLen, up->iTextBufLen ) ;
#endif

			if ( up->bSkipCntrlCharOnly && up->iTextBufLen == 0 ) {
#ifdef DEBUG
				printf( "refclock_jjy.c : %s : Skip cntrl char only : up->iRawBufLen=%d iReadRawBuf=%d iBreakPosition=%d\n",
					 sFunctionName, up->iRawBufLen, iReadRawBuf, iBreakPosition ) ;
#endif
				if ( iBreakPosition + 1 < up->iRawBufLen ) {
					iReadRawBuf = iBreakPosition + 1 ;
					continue ;
				} else {
					break ;
				}

			}

		}

		if ( up->linediscipline == LDISC_RAW ) {
			pBuf = up->sLineBuf ;
			iLen = up->iLineBufLen ;
		} else {
			pBuf = pp->a_lastcode ;
			iLen = pp->lencode ;
		}

		iCopyLen = ( iLen <= sizeof(sLogText)-1 ? iLen : sizeof(sLogText)-1 ) ;
		memcpy( sLogText, pBuf, iCopyLen ) ;
		sLogText[iCopyLen] = '\0' ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_RECEIVE, sLogText ) ;

		switch ( up->unittype ) {

		case UNITTYPE_TRISTATE_JJY01 :
			rc = jjy_receive_tristate_jjy01  ( rbufp ) ;
			break ;

		case UNITTYPE_CDEX_JST2000 :
			rc = jjy_receive_cdex_jst2000 ( rbufp ) ;
			break ;

		case UNITTYPE_ECHOKEISOKUKI_LT2000 :
			rc = jjy_receive_echokeisokuki_lt2000 ( rbufp ) ;
			break ;

		case UNITTYPE_CITIZENTIC_JJY200 :
			rc = jjy_receive_citizentic_jjy200 ( rbufp ) ;
			break ;

		case UNITTYPE_TRISTATE_GPSCLOCK01 :
			rc = jjy_receive_tristate_gpsclock01 ( rbufp ) ;
			break ;

		case UNITTYPE_SEIKO_TIMESYS_TDC_300 :
			rc = jjy_receive_seiko_tsys_tdc_300 ( rbufp ) ;
			break ;

		case UNITTYPE_TELEPHONE :
			rc = jjy_receive_telephone ( rbufp ) ;
			break ;

		default :
			rc = JJY_RECEIVE_ERROR ;
			break ;

		}

		switch ( rc ) {
		case JJY_RECEIVE_DONE :
		case JJY_RECEIVE_SKIP :
			up->iProcessState = JJY_PROCESS_STATE_DONE ;
			break ;
		case JJY_RECEIVE_ERROR :
			up->iProcessState = JJY_PROCESS_STATE_ERROR ;
			break ;
		default :
			break ;
		}

		if ( up->linediscipline == LDISC_RAW ) {
			if ( rc == JJY_RECEIVE_UNPROCESS ) {
				break ;
			}
			iReadRawBuf = iBreakPosition + 1 ;
			if ( iReadRawBuf >= up->iRawBufLen ) {
				/* Processed all received data */
				break ;
			}
		}

		if ( up->linediscipline == LDISC_CLK ) {
			break ;
		}

	}

	if ( up->linediscipline == LDISC_RAW && iReadRawBuf > 0 ) {
		for ( i = 0, j = iReadRawBuf ; j < up->iRawBufLen ; i ++, j++ ) {
			up->sRawBuf[i] = up->sRawBuf[j] ;
		}
		up->iRawBufLen -= iReadRawBuf ;
		if ( up->iRawBufLen < 0 ) {
			up->iRawBufLen = 0 ;
		}
	}

	up->bReceiveFlag = FALSE ;

}

/**************************************************************************************************/

static int
getRawDataBreakPosition ( struct jjyunit *up, int iStart )
{

	int 	i, j ;

	if ( iStart >= up->iRawBufLen ) {
#ifdef DEBUG
		printf( "refclock_jjy.c : getRawDataBreakPosition : iStart=%d return=-1\n", iStart ) ;
#endif
		return -1 ;
	}

	for ( i = iStart ; i < up->iRawBufLen ; i ++ ) {

		for ( j = 0 ; up->pRawBreak[j].pString != NULL ; j ++ ) {

			if ( i + up->pRawBreak[j].iLength <= up->iRawBufLen ) {

				if ( strncmp( up->sRawBuf + i,
					up->pRawBreak[j].pString,
					up->pRawBreak[j].iLength ) == 0 ) {

#ifdef DEBUG
					printf( "refclock_jjy.c : getRawDataBreakPosition : iStart=%d return=%d\n",
						iStart, i + up->pRawBreak[j].iLength - 1 ) ;
#endif
					return i + up->pRawBreak[j].iLength - 1 ;

				}
			}
		}
	}

#ifdef DEBUG
	printf( "refclock_jjy.c : getRawDataBreakPosition : iStart=%d return=-1\n", iStart ) ;
#endif
	return -1 ;

}

/**************************************************************************************************/
/*  jjy_poll - called by the transmit procedure                                                   */
/**************************************************************************************************/
static void
jjy_poll ( int unit, struct peer *peer )
{

	char	sLog [ 40 ], sReach [ 9 ] ;

	struct jjyunit      *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr ;

	if ( up->bInitError ) {
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, "Ignore polling because of error during initializing" ) ;
		return ;
	}

	if ( pp->polls > 0  &&  up->iLineCount == 0 ) {
		/*
		 * No reply for last command
		 */
		refclock_report ( peer, CEVNT_TIMEOUT ) ;
	}

	pp->polls ++ ;

	sReach[0] = peer->reach & 0x80 ? '1' : '0' ;
	sReach[1] = peer->reach & 0x40 ? '1' : '0' ;
	sReach[2] = peer->reach & 0x20 ? '1' : '0' ;
	sReach[3] = peer->reach & 0x10 ? '1' : '0' ;
	sReach[4] = peer->reach & 0x08 ? '1' : '0' ;
	sReach[5] = peer->reach & 0x04 ? '1' : '0' ;
	sReach[6] = peer->reach & 0x02 ? '1' : '0' ;
	sReach[7] = 0 ; /* This poll */
	sReach[8] = 0 ;

	snprintf( sLog, sizeof(sLog), "polls=%ld reach=%s", pp->polls, sReach ) ;
	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ATTENTION, sLog ) ;

	up->iProcessState = JJY_PROCESS_STATE_POLL ;
	up->iCommandSeq = 0 ;
	up->iReceiveSeq = 0 ;
	up->iLineCount = 0 ;
	up->bLineError = FALSE ;
	up->iRawBufLen = 0 ;

	switch ( up->unittype ) {
	
	case UNITTYPE_TRISTATE_JJY01 :
		jjy_poll_tristate_jjy01  ( unit, peer ) ;
		break ;

	case UNITTYPE_CDEX_JST2000 :
		jjy_poll_cdex_jst2000 ( unit, peer ) ;
		break ;

	case UNITTYPE_ECHOKEISOKUKI_LT2000 :
		jjy_poll_echokeisokuki_lt2000 ( unit, peer ) ;
		break ;

	case UNITTYPE_CITIZENTIC_JJY200 :
		jjy_poll_citizentic_jjy200 ( unit, peer ) ;
		break ;

	case UNITTYPE_TRISTATE_GPSCLOCK01 :
		jjy_poll_tristate_gpsclock01 ( unit, peer ) ;
		break ;

	case UNITTYPE_SEIKO_TIMESYS_TDC_300 :
		jjy_poll_seiko_tsys_tdc_300 ( unit, peer ) ;
		break ;

	case UNITTYPE_TELEPHONE :
		jjy_poll_telephone ( unit, peer ) ;
		break ;

	default :
		break ;

	}

}

/**************************************************************************************************/
/*  jjy_timer - called at one-second intervals                                                    */
/**************************************************************************************************/
static void
jjy_timer ( int unit, struct peer *peer )
{

	struct	refclockproc *pp ;
	struct	jjyunit      *up ;

#ifdef DEBUG
	if ( debug ) {
		printf ( "refclock_jjy.c : jjy_timer\n" ) ;
	}
#endif

	pp = peer->procptr ;
	up = pp->unitptr ;

	if ( up->bReceiveFlag ) {
#ifdef DEBUG
		if ( debug ) {
			printf ( "refclock_jjy.c : jjy_timer : up->bReceiveFlag= TRUE : Timer skipped.\n" ) ;
		}
#endif
		return ;
	}

	switch ( up->unittype ) {
	
	case UNITTYPE_TELEPHONE :
		jjy_timer_telephone ( unit, peer ) ;
		break ;

	default :
		break ;

	}

}

/**************************************************************************************************/
/*  jjy_synctime                                                                                  */
/**************************************************************************************************/
static void
jjy_synctime ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	char	sLog [ 80 ], cStatus ;
	const char	*pStatus ;

	pp->year   = up->year ;
	pp->day    = ymd2yd( up->year, up->month, up->day ) ;
	pp->hour   = up->hour ;
	pp->minute = up->minute ;
	pp->second = up->second ;
	pp->nsec   = up->msecond * 1000000 ;

	/* 
	 * JST to UTC 
	 */
	pp->hour -= 9 ;
	if ( pp->hour < 0 ) {
		pp->hour += 24 ;
		pp->day -- ;
		if ( pp->day < 1 ) {
			pp->year -- ;
			pp->day  = ymd2yd( pp->year, 12, 31 ) ;
		}
	}

	/*
	 * Process the new sample in the median filter and determine the
	 * timecode timestamp.
	 */

	if ( ! refclock_process( pp ) ) {
		refclock_report( peer, CEVNT_BADTIME ) ;
		return ;
	}

	pp->lastref = pp->lastrec ;

	refclock_receive( peer ) ;

	/*
	 * Write into the clockstats file
	 */
	snprintf ( sLog, sizeof(sLog),
		   "%04d/%02d/%02d %02d:%02d:%02d.%03d JST   ( %04d/%03d %02d:%02d:%02d.%03d UTC )",
		   up->year, up->month, up->day,
		   up->hour, up->minute, up->second, up->msecond,
		   pp->year, pp->day, pp->hour, pp->minute, pp->second,
		   (int)(pp->nsec/1000000) ) ;
	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ATTENTION, sLog ) ;

	cStatus = ' ' ;
	pStatus = "" ;

	switch ( peer->status ) {
	case 0 : cStatus = ' ' ; pStatus = "Reject"    ; break ;
	case 1 : cStatus = 'x' ; pStatus = "FalseTick" ; break ;
	case 2 : cStatus = '.' ; pStatus = "Excess"    ; break ;
	case 3 : cStatus = '-' ; pStatus = "Outlier"   ; break ;
	case 4 : cStatus = '+' ; pStatus = "Candidate" ; break ;
	case 5 : cStatus = '#' ; pStatus = "Selected"  ; break ;
	case 6 : cStatus = '*' ; pStatus = "Sys.Peer"  ; break ;
	case 7 : cStatus = 'o' ; pStatus = "PPS.Peer"  ; break ;
	default : break ; 
	}

	snprintf ( sLog, sizeof(sLog),
		   "status %d [%c] %s : offset %3.3f mSec. : jitter %3.3f mSec.",
		    peer->status, cStatus, pStatus, peer->offset * 1000, peer->jitter * 1000 ) ;
	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_INFORMATION, sLog ) ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    The Tristate Ltd. JJY receiver TS-JJY01, TS-JJY02					##*/
/*##												##*/
/*##    server  127.127.40.X  mode 1								##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/
/*                                                                                                */
/*  Command               Response                                  Remarks                       */
/*  --------------------  ----------------------------------------  ----------------------------  */
/*  dcst<CR><LF>          VALID<CR><LF> or INVALID<CR><LF>                                        */
/*  stus<CR><LF>          ADJUSTED<CR><LF> or UNADJUSTED<CR><LF>                                  */
/*  date<CR><LF>          YYYY/MM/DD XXX<CR><LF>                    XXX is the day of the week    */
/*  time<CR><LF>          HH:MM:SS<CR><LF>                          Not used by this driver       */
/*  stim<CR><LF>          HH:MM:SS<CR><LF>                          Reply at just second          */
/*                                                                                                */
/*################################################################################################*/

#define	TS_JJY01_COMMAND_NUMBER_DATE	1
#define	TS_JJY01_COMMAND_NUMBER_TIME	2
#define	TS_JJY01_COMMAND_NUMBER_STIM	3
#define	TS_JJY01_COMMAND_NUMBER_STUS	4
#define	TS_JJY01_COMMAND_NUMBER_DCST	5

#define	TS_JJY01_REPLY_DATE     	"yyyy/mm/dd www"
#define	TS_JJY01_REPLY_STIM     	"hh:mm:ss"
#define	TS_JJY01_REPLY_STUS_ADJUSTED	"adjusted"
#define	TS_JJY01_REPLY_STUS_UNADJUSTED	"unadjusted"
#define	TS_JJY01_REPLY_DCST_VALID	"valid"
#define	TS_JJY01_REPLY_DCST_INVALID	"invalid"

#define	TS_JJY01_REPLY_LENGTH_DATE           	14	/* Length without <CR><LF> */
#define	TS_JJY01_REPLY_LENGTH_TIME           	8	/* Length without <CR><LF> */
#define	TS_JJY01_REPLY_LENGTH_STIM           	8	/* Length without <CR><LF> */
#define	TS_JJY01_REPLY_LENGTH_STUS_ADJUSTED  	8	/* Length without <CR><LF> */
#define	TS_JJY01_REPLY_LENGTH_STUS_UNADJUSTED	10	/* Length without <CR><LF> */
#define	TS_JJY01_REPLY_LENGTH_DCST_VALID     	5	/* Length without <CR><LF> */
#define	TS_JJY01_REPLY_LENGTH_DCST_INVALID   	7	/* Length without <CR><LF> */

static  struct
{
	const char	commandNumber ;
	const char	*command ;
	int	commandLength ;
	int	iExpectedReplyLength [ 2 ] ;
} tristate_jjy01_command_sequence[] =
{
	{ 0, NULL, 0, { 0, 0 } }, /* Idle */
	{ TS_JJY01_COMMAND_NUMBER_DCST, "dcst\r\n", 6, { TS_JJY01_REPLY_LENGTH_DCST_VALID   , TS_JJY01_REPLY_LENGTH_DCST_INVALID } },
	{ TS_JJY01_COMMAND_NUMBER_STUS, "stus\r\n", 6, { TS_JJY01_REPLY_LENGTH_STUS_ADJUSTED, TS_JJY01_REPLY_LENGTH_STUS_UNADJUSTED } },
	{ TS_JJY01_COMMAND_NUMBER_TIME, "time\r\n", 6, { TS_JJY01_REPLY_LENGTH_TIME         , TS_JJY01_REPLY_LENGTH_TIME } },
	{ TS_JJY01_COMMAND_NUMBER_DATE, "date\r\n", 6, { TS_JJY01_REPLY_LENGTH_DATE         , TS_JJY01_REPLY_LENGTH_DATE } },
	{ TS_JJY01_COMMAND_NUMBER_STIM, "stim\r\n", 6, { TS_JJY01_REPLY_LENGTH_STIM         , TS_JJY01_REPLY_LENGTH_STIM } },
	/* End of command */
	{ 0, NULL, 0, { 0, 0 } }
} ;

/**************************************************************************************************/

static int
jjy_start_tristate_jjy01 ( int unit, struct peer *peer, struct jjyunit *up )
{

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, "Refclock: Tristate Ltd. TS-JJY01, TS-JJY02" ) ;

	up->unittype  = UNITTYPE_TRISTATE_JJY01 ;
	up->linespeed = SPEED232_TRISTATE_JJY01 ;
	up->linediscipline = LDISC_CLK ;

	return 0 ;

}

/**************************************************************************************************/

static int
jjy_receive_tristate_jjy01 ( struct recvbuf *rbufp )
{
	struct jjyunit	    *up ;
	struct refclockproc *pp ;
	struct peer	    *peer;

	char *		pBuf ;
	char		sLog [ 100 ] ;
	int 		iLen ;
	int 		rc ;

	const char *	pCmd ;
	int 		iCmdLen ;

	/* Initialize pointers  */

	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	DEBUG_PRINTF_JJY_RECEIVE( "jjy_receive_tristate_jjy01", iLen ) ;

	/* Check expected reply */

	if ( tristate_jjy01_command_sequence[up->iCommandSeq].command == NULL ) {
		/* Command sequence has not been started, or has been completed */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_UNEXPECTED_REPLY,
			  pBuf ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	/* Check reply length */

	if ( iLen != tristate_jjy01_command_sequence[up->iCommandSeq].iExpectedReplyLength[0]
	  && iLen != tristate_jjy01_command_sequence[up->iCommandSeq].iExpectedReplyLength[1] ) {
		/* Unexpected reply length */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_LENGTH,
			  iLen ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	/* Parse reply */

	switch ( tristate_jjy01_command_sequence[up->iCommandSeq].commandNumber ) {

	case TS_JJY01_COMMAND_NUMBER_DATE : /* YYYY/MM/DD WWW */

		rc = sscanf ( pBuf, "%4d/%2d/%2d",
			      &up->year, &up->month, &up->day ) ;

		if ( rc != 3 || up->year < 2000 || 2099 <= up->year
		  || up->month < 1 || 12 < up->month
		  || up->day < 1 || 31 < up->day ) {
			/* Invalid date */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATE,
				  rc, up->year, up->month, up->day ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		break ;

	case TS_JJY01_COMMAND_NUMBER_TIME : /* HH:MM:SS */
	case TS_JJY01_COMMAND_NUMBER_STIM : /* HH:MM:SS */

		if ( up->iTimestampCount >= 2 ) {
			/* Too many time reply */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_TOO_MANY_REPLY,
				  up->iTimestampCount ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		rc = sscanf ( pBuf, "%2d:%2d:%2d",
			      &up->hour, &up->minute, &up->second ) ;

		if ( rc != 3 || up->hour > 23 || up->minute > 59 ||
		     up->second > 60 ) {
			/* Invalid time */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_TIME,
				  rc, up->hour, up->minute, up->second ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		up->iTimestamp[up->iTimestampCount] = ( up->hour * 60 + up->minute ) * 60 + up->second ;

		up->iTimestampCount++ ;

		up->msecond = 0 ;

		break ;

	case TS_JJY01_COMMAND_NUMBER_STUS :

		if ( strncmp( pBuf, TS_JJY01_REPLY_STUS_ADJUSTED,
			     TS_JJY01_REPLY_LENGTH_STUS_ADJUSTED ) == 0
		  || strncmp( pBuf, TS_JJY01_REPLY_STUS_UNADJUSTED,
			     TS_JJY01_REPLY_LENGTH_STUS_UNADJUSTED ) == 0 ) {
			/* Good */
		} else {
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_REPLY,
				  pBuf ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		break ;

	case TS_JJY01_COMMAND_NUMBER_DCST :

		if ( strncmp( pBuf, TS_JJY01_REPLY_DCST_VALID,
			     TS_JJY01_REPLY_LENGTH_DCST_VALID ) == 0
		  || strncmp( pBuf, TS_JJY01_REPLY_DCST_INVALID,
			     TS_JJY01_REPLY_LENGTH_DCST_INVALID ) == 0 ) {
			/* Good */
		} else {
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_REPLY,
				  pBuf ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		break ;

	default : /*  Unexpected reply */

		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_REPLY,
			  pBuf ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;

	}

	if ( up->iTimestampCount == 2 ) {
		/* Process date and time */

		if ( up->iTimestamp[1] - 2 <= up->iTimestamp[0]
		  && up->iTimestamp[0]     <= up->iTimestamp[1] ) {
			/* 3 commands (time,date,stim) was excuted in two seconds */
			jjy_synctime( peer, pp, up ) ;
			return JJY_RECEIVE_DONE ;
		} else if ( up->iTimestamp[0] > up->iTimestamp[1] ) {
			/* Over midnight, and date is unsure */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_OVER_MIDNIGHT_2,
				  up->iTimestamp[0], up->iTimestamp[1] ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_INFORMATION, sLog ) ;
			return JJY_RECEIVE_SKIP ;
		} else {
			/* Slow reply */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SLOW_REPLY_2,
				  up->iTimestamp[0], up->iTimestamp[1] ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

	}

	/* Issue next command */

	if ( tristate_jjy01_command_sequence[up->iCommandSeq].command != NULL ) {
		up->iCommandSeq ++ ;
	}

	if ( tristate_jjy01_command_sequence[up->iCommandSeq].command == NULL ) {
		/* Command sequence completed */
		return JJY_RECEIVE_DONE ;
	}

	pCmd =  tristate_jjy01_command_sequence[up->iCommandSeq].command ;
	iCmdLen = tristate_jjy01_command_sequence[up->iCommandSeq].commandLength ;
	if ( write ( pp->io.fd, pCmd, iCmdLen ) != iCmdLen ) {
		refclock_report ( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

	return JJY_RECEIVE_WAIT ;

}

/**************************************************************************************************/

static void
jjy_poll_tristate_jjy01  ( int unit, struct peer *peer )
{
#ifdef DEBUG
	static const char *sFunctionName = "jjy_poll_tristate_jjy01" ;
#endif

	struct refclockproc *pp ;
	struct jjyunit	    *up ;

	const char *	pCmd ;
	int 		iCmdLen ;

	pp = peer->procptr;
	up = pp->unitptr ;

	up->bLineError = FALSE ;
	up->iTimestampCount = 0 ;

	if ( ( pp->sloppyclockflag & CLK_FLAG1 ) == 0 ) {
		/* Skip "dcst" and "stus" commands */
		up->iCommandSeq = 2 ;
		up->iLineCount = 2 ;
	}

#ifdef DEBUG
	if ( debug ) {
		printf ( "%s (refclock_jjy.c) : flag1=%X CLK_FLAG1=%X up->iLineCount=%d\n",
			sFunctionName, pp->sloppyclockflag, CLK_FLAG1,
			up->iLineCount ) ;
	}
#endif

	/*
	 * Send a first command
	 */

	up->iCommandSeq ++ ;

	pCmd =  tristate_jjy01_command_sequence[up->iCommandSeq].command ;
	iCmdLen = tristate_jjy01_command_sequence[up->iCommandSeq].commandLength ;
	if ( write ( pp->io.fd, pCmd, iCmdLen ) != iCmdLen ) {
		refclock_report ( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    The C-DEX Co. Ltd. JJY receiver JST2000							##*/
/*##												##*/
/*##    server  127.127.40.X  mode 2								##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/
/*                                                                                                */
/*  Command               Response                                  Remarks                       */
/*  --------------------  ----------------------------------------  ----------------------------  */
/*  <ENQ>1J<ETX>          <STX>JYYMMDD HHMMSSS<ETX>                 J is a fixed character        */
/*                                                                                                */
/*################################################################################################*/

static struct jjyRawDataBreak cdex_jst2000_raw_break [ ] =
{
	{ "\x03", 1 }, { NULL, 0 }
} ;

/**************************************************************************************************/

static int
jjy_start_cdex_jst2000 ( int unit, struct peer *peer, struct jjyunit *up )
{

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, "Refclock: C-DEX Co. Ltd. JST2000" ) ;

	up->unittype  = UNITTYPE_CDEX_JST2000 ;
	up->linespeed = SPEED232_CDEX_JST2000 ;
	up->linediscipline = LDISC_RAW ;

	up->pRawBreak = cdex_jst2000_raw_break ;
	up->bWaitBreakString = TRUE ;

	up->bSkipCntrlCharOnly = FALSE ;

	return 0 ;

}

/**************************************************************************************************/

static int
jjy_receive_cdex_jst2000 ( struct recvbuf *rbufp )
{

	struct jjyunit      *up ;
	struct refclockproc *pp ;
	struct peer         *peer ;

	char	*pBuf, sLog [ 100 ] ;
	int 	iLen ;
	int 	rc ;

	/* Initialize pointers */

	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	DEBUG_PRINTF_JJY_RECEIVE( "jjy_receive_cdex_jst2000", iLen ) ;

	/* Check expected reply */

	if ( up->iCommandSeq != 1 ) {
		/* Command sequence has not been started, or has been completed */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_UNEXPECTED_REPLY,
			  pBuf ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	/* Wait until ETX comes */

	if ( up->iLineBufLen < 17 || up->sLineBuf[up->iLineBufLen-1] != 0x03 ) {
		return JJY_RECEIVE_UNPROCESS ;
	}

	/* Check reply length */

	if ( iLen != 15 ) {
		/* Unexpected reply length */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_LENGTH,
			  iLen ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	/* JYYMMDDWHHMMSSS */

	rc = sscanf ( pBuf, "J%2d%2d%2d%*1d%2d%2d%2d%1d",
		      &up->year, &up->month, &up->day,
		      &up->hour, &up->minute, &up->second,
		      &up->msecond ) ;

	if ( rc != 7 || up->month < 1 || up->month > 12 ||
	     up->day < 1 || up->day > 31 || up->hour > 23 ||
	     up->minute > 59 || up->second > 60 ) {
		/* Invalid date and time */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATETIME,
			  rc, up->year, up->month, up->day,
			  up->hour, up->minute, up->second ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	up->year    += 2000 ;
	up->msecond *= 100 ;

	jjy_synctime( peer, pp, up ) ;

	return JJY_RECEIVE_DONE ;

}

/**************************************************************************************************/

static void
jjy_poll_cdex_jst2000 ( int unit, struct peer *peer )
{

	struct refclockproc *pp ;
	struct jjyunit      *up ;

	pp = peer->procptr ;
	up = pp->unitptr ;

	up->bLineError = FALSE ;
	up->iRawBufLen = 0 ;
	up->iLineBufLen = 0 ;
	up->iTextBufLen = 0 ;

	/*
	 * Send "<ENQ>1J<ETX>" command
	 */

	up->iCommandSeq ++ ;

	if ( write ( pp->io.fd, "\0051J\003", 4 ) != 4  ) {
		refclock_report ( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, "\0051J\003" ) ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    The Echo Keisokuki Co. Ltd. JJY receiver LT2000						##*/
/*##												##*/
/*##    server  127.127.40.X  mode 3								##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/
/*                                                                                                */
/*  Command               Response                                  Remarks                       */
/*  --------------------  ----------------------------------------  ----------------------------  */
/*  #                                                               Mode 1 ( Request & Send )     */
/*  T                     YYMMDDWHHMMSS<BCC1><BCC2><CR>                                           */
/*  C                                                               Mode 2 ( Continuous )         */
/*                        YYMMDDWHHMMSS<ST1><ST2><ST3><ST4><CR>     0.5 sec before time stamp     */
/*                        <SUB>                                     Second signal                 */
/*                                                                                                */
/*################################################################################################*/

#define	ECHOKEISOKUKI_LT2000_MODE_REQUEST_SEND		1
#define	ECHOKEISOKUKI_LT2000_MODE_CONTINUOUS		2
#define	ECHOKEISOKUKI_LT2000_MODE_SWITCHING_CONTINUOUS	3

#define	ECHOKEISOKUKI_LT2000_COMMAND_REQUEST_SEND 	"#"
#define	ECHOKEISOKUKI_LT2000_COMMAND_REQUEST_TIME 	"T"
#define	ECHOKEISOKUKI_LT2000_COMMAND_CONTINUOUS 	"C"

/**************************************************************************************************/

static int
jjy_start_echokeisokuki_lt2000 ( int unit, struct peer *peer, struct jjyunit *up )
{

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, "Refclock: Echo Keisokuki Co. Ltd. LT2000" ) ;

	up->unittype  = UNITTYPE_ECHOKEISOKUKI_LT2000 ;
	up->linespeed = SPEED232_ECHOKEISOKUKI_LT2000 ;
	up->linediscipline = LDISC_CLK ;

	up->operationmode = ECHOKEISOKUKI_LT2000_MODE_SWITCHING_CONTINUOUS ;

	return 0 ;

}

/**************************************************************************************************/

static int
jjy_receive_echokeisokuki_lt2000 ( struct recvbuf *rbufp )
{

	struct jjyunit      *up ;
	struct refclockproc *pp ;
	struct peer	    *peer;

	char	*pBuf, sLog [ 100 ], sErr [ 60 ] ;
	int 	iLen ;
	int 	rc ;
	int	i, ibcc, ibcc1, ibcc2 ;

	/* Initialize pointers */

	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	DEBUG_PRINTF_JJY_RECEIVE( "jjy_receive_echokeisokuki_lt2000", iLen ) ;

	/* Check reply length */

	if ( ( up->operationmode == ECHOKEISOKUKI_LT2000_MODE_REQUEST_SEND
	       && iLen != 15 )
	  || ( up->operationmode == ECHOKEISOKUKI_LT2000_MODE_CONTINUOUS
	       && iLen != 17 )
	  || ( up->operationmode == ECHOKEISOKUKI_LT2000_MODE_SWITCHING_CONTINUOUS
	       && iLen != 17 ) ) {
		/* Unexpected reply length */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_LENGTH,
			  iLen ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	if ( up->operationmode == ECHOKEISOKUKI_LT2000_MODE_REQUEST_SEND && iLen == 15 ) {
		/* YYMMDDWHHMMSS<BCC1><BCC2> */

		for ( i = ibcc = 0 ; i < 13 ; i ++ ) {
			ibcc ^= pBuf[i] ;
		}

		ibcc1 = 0x30 | ( ( ibcc >> 4 ) & 0xF ) ;
		ibcc2 = 0x30 | ( ( ibcc      ) & 0xF ) ;
		if ( pBuf[13] != ibcc1 || pBuf[14] != ibcc2 ) {
			snprintf( sErr, sizeof(sErr)-1, " BCC error : Recv=%02X,%02X / Calc=%02X,%02X ",
				  pBuf[13] & 0xFF, pBuf[14] & 0xFF,
				  ibcc1, ibcc2 ) ;
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_REPLY,
				  sErr ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

	}

	if ( ( up->operationmode == ECHOKEISOKUKI_LT2000_MODE_REQUEST_SEND
	       && iLen == 15 )
	  || ( up->operationmode == ECHOKEISOKUKI_LT2000_MODE_CONTINUOUS
	       && iLen == 17 )
	  || ( up->operationmode == ECHOKEISOKUKI_LT2000_MODE_SWITCHING_CONTINUOUS
	       && iLen == 17 ) ) {
		/* YYMMDDWHHMMSS<BCC1><BCC2> or YYMMDDWHHMMSS<ST1><ST2><ST3><ST4> */

		rc = sscanf ( pBuf, "%2d%2d%2d%*1d%2d%2d%2d",
			      &up->year, &up->month, &up->day,
			      &up->hour, &up->minute, &up->second ) ;

		if ( rc != 6 || up->month < 1 || up->month > 12
		  || up->day < 1 || up->day > 31
		  || up->hour > 23 || up->minute > 59 || up->second > 60 ) {
			/* Invalid date and time */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATETIME,
				  rc, up->year, up->month, up->day,
				  up->hour, up->minute, up->second ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		up->year += 2000 ;

		if ( up->operationmode == ECHOKEISOKUKI_LT2000_MODE_CONTINUOUS
		  || up->operationmode == ECHOKEISOKUKI_LT2000_MODE_SWITCHING_CONTINUOUS ) {
			/* A time stamp comes on every 0.5 second in the mode 2 of the LT-2000. */

			up->msecond = 500 ;
			up->second -- ;
			if ( up->second < 0 ) {
				up->second = 59 ;
				up->minute -- ;
				if ( up->minute < 0 ) {
					up->minute = 59 ;
					up->hour -- ;
					if ( up->hour < 0 ) {
						up->hour = 23 ;
						up->day -- ;
						if ( up->day < 1 ) {
							up->month -- ;
							if ( up->month < 1 ) {
								up->month = 12 ;
								up->year -- ;
							}
						}
					}
				}
			}

		}

		jjy_synctime( peer, pp, up ) ;


	}

	if (up->operationmode == ECHOKEISOKUKI_LT2000_MODE_SWITCHING_CONTINUOUS ) {
		/* Switch from mode 2 to mode 1 in order to restraint of useless time stamp. */

		iLen = strlen( ECHOKEISOKUKI_LT2000_COMMAND_REQUEST_SEND ) ;
		if ( write ( pp->io.fd, ECHOKEISOKUKI_LT2000_COMMAND_REQUEST_SEND, iLen ) != iLen  ) {
			refclock_report ( peer, CEVNT_FAULT ) ;
		}

		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, ECHOKEISOKUKI_LT2000_COMMAND_REQUEST_SEND ) ;

	}

	return JJY_RECEIVE_DONE ;

}

/**************************************************************************************************/

static void
jjy_poll_echokeisokuki_lt2000 ( int unit, struct peer *peer )
{

	struct refclockproc *pp ;
	struct jjyunit      *up ;

	char	sCmd[2] ;

	pp = peer->procptr ;
	up = pp->unitptr ;

	up->bLineError = FALSE ;

	/*
	 * Send "T" or "C" command
	 */

	switch ( up->operationmode ) {
	case ECHOKEISOKUKI_LT2000_MODE_REQUEST_SEND :
		sCmd[0] = 'T' ;
		break ;
	case ECHOKEISOKUKI_LT2000_MODE_CONTINUOUS :
	case ECHOKEISOKUKI_LT2000_MODE_SWITCHING_CONTINUOUS :
		sCmd[0] = 'C' ;
		break ;
	}
	sCmd[1] = 0 ;

	if ( write ( pp->io.fd, sCmd, 1 ) != 1  ) {
		refclock_report ( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, sCmd ) ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    The CITIZEN T.I.C CO., LTD. JJY receiver JJY200						##*/
/*##												##*/
/*##    server  127.127.40.X  mode 4								##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/
/*                                                                                                */
/*  Command               Response                                  Remarks                       */
/*  --------------------  ----------------------------------------  ----------------------------  */
/*                        'XX YY/MM/DD W HH:MM:SS<CR>               XX:OK|NG|ER  W:0(Mon)-6(Sun)  */
/*                                                                                                */
/*################################################################################################*/

static int
jjy_start_citizentic_jjy200 ( int unit, struct peer *peer, struct jjyunit *up )
{

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, "Refclock: CITIZEN T.I.C CO. LTD. JJY200" ) ;

	up->unittype  = UNITTYPE_CITIZENTIC_JJY200 ;
	up->linespeed = SPEED232_CITIZENTIC_JJY200 ;
	up->linediscipline = LDISC_CLK ;

	return 0 ;

}

/**************************************************************************************************/

static int
jjy_receive_citizentic_jjy200 ( struct recvbuf *rbufp )
{

	struct jjyunit		*up ;
	struct refclockproc	*pp ;
	struct peer		*peer;

	char	*pBuf, sLog [ 100 ], sMsg [ 16 ] ;
	int	iLen ;
	int	rc ;
	char	cApostrophe, sStatus[3] ;
	int	iWeekday ;

	/* Initialize pointers */

	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	DEBUG_PRINTF_JJY_RECEIVE( "jjy_receive_citizentic_jjy200", iLen ) ;

	/*
	 * JJY-200 sends a timestamp every second.
	 * So, a timestamp is ignored unless it is right after polled.
	 */

	if ( up->iProcessState != JJY_PROCESS_STATE_RECEIVE ) {
		return JJY_RECEIVE_SKIP ;
	}

	/* Check reply length */

	if ( iLen != 23 ) {
		/* Unexpected reply length */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_LENGTH,
			  iLen ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	/* 'XX YY/MM/DD W HH:MM:SS<CR> */

	rc = sscanf ( pBuf, "%c%2s %2d/%2d/%2d %1d %2d:%2d:%2d",
		      &cApostrophe, sStatus,
		      &up->year, &up->month, &up->day, &iWeekday,
		      &up->hour, &up->minute, &up->second ) ;
	sStatus[2] = 0 ;

	if ( rc != 9 || cApostrophe != '\''
	  || ( strcmp( sStatus, "OK" ) != 0
	    && strcmp( sStatus, "NG" ) != 0
	    && strcmp( sStatus, "ER" ) != 0 )
	  || up->month < 1 || up->month > 12 || up->day < 1 || up->day > 31
	  || iWeekday > 6
	  || up->hour > 23 || up->minute > 59 || up->second > 60 ) {
		/* Invalid date and time */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATETIME,
			  rc, up->year, up->month, up->day,
			  up->hour, up->minute, up->second ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	} else if ( strcmp( sStatus, "NG" ) == 0
		 || strcmp( sStatus, "ER" ) == 0 ) {
		/* Timestamp is unsure */
		snprintf( sMsg, sizeof(sMsg)-1, "status=%s", sStatus ) ;
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_TIMESTAMP_UNSURE,
			  sMsg ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_WARNING, sLog ) ;
		return JJY_RECEIVE_SKIP ;
	}

	up->year += 2000 ;
	up->msecond = 0 ;

	jjy_synctime( peer, pp, up ) ;

	return JJY_RECEIVE_DONE ;

}

/**************************************************************************************************/

static void
jjy_poll_citizentic_jjy200 ( int unit, struct peer *peer )
{

	struct refclockproc *pp ;
	struct jjyunit	    *up ;

	pp = peer->procptr ;
	up = pp->unitptr ;

	up->bLineError = FALSE ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    The Tristate Ltd. GPS clock TS-GPS01							##*/
/*##												##*/
/*##    server  127.127.40.X  mode 5								##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/
/*                                                                                                */
/*  This clock has NMEA mode and command/respose mode.                                            */
/*  When this jjy driver are used, set to command/respose mode of this clock                      */
/*  by the onboard switch SW4, and make sure the LED-Y is tured on.                               */
/*  Other than this JJY driver, the refclock driver type 20, generic NMEA driver,                 */
/*  works with the NMEA mode of this clock.                                                       */
/*                                                                                                */
/*  Command               Response                                  Remarks                       */
/*  --------------------  ----------------------------------------  ----------------------------  */
/*  stus<CR><LF>          *R|*G|*U|+U<CR><LF>                                                     */
/*  date<CR><LF>          YY/MM/DD<CR><LF>                                                        */
/*  time<CR><LF>          HH:MM:SS<CR><LF>                                                        */
/*                                                                                                */
/*################################################################################################*/

#define	TS_GPS01_COMMAND_NUMBER_DATE	1
#define	TS_GPS01_COMMAND_NUMBER_TIME	2
#define	TS_GPS01_COMMAND_NUMBER_STUS	4

#define	TS_GPS01_REPLY_DATE		"yyyy/mm/dd"
#define	TS_GPS01_REPLY_TIME		"hh:mm:ss"
#define	TS_GPS01_REPLY_STUS_RTC		"*R"
#define	TS_GPS01_REPLY_STUS_GPS		"*G"
#define	TS_GPS01_REPLY_STUS_UTC		"*U"
#define	TS_GPS01_REPLY_STUS_PPS		"+U"

#define	TS_GPS01_REPLY_LENGTH_DATE	    10	/* Length without <CR><LF> */
#define	TS_GPS01_REPLY_LENGTH_TIME	    8	/* Length without <CR><LF> */
#define	TS_GPS01_REPLY_LENGTH_STUS	    2	/* Length without <CR><LF> */

static  struct
{
	char	commandNumber ;
	const char	*command ;
	int	commandLength ;
	int	iExpectedReplyLength ;
} tristate_gps01_command_sequence[] =
{
	{ 0, NULL, 0, 0 }, /* Idle */
	{ TS_GPS01_COMMAND_NUMBER_STUS, "stus\r\n", 6, TS_GPS01_REPLY_LENGTH_STUS },
	{ TS_GPS01_COMMAND_NUMBER_TIME, "time\r\n", 6, TS_GPS01_REPLY_LENGTH_TIME },
	{ TS_GPS01_COMMAND_NUMBER_DATE, "date\r\n", 6, TS_GPS01_REPLY_LENGTH_DATE },
	{ TS_GPS01_COMMAND_NUMBER_TIME, "time\r\n", 6, TS_GPS01_REPLY_LENGTH_TIME },
	/* End of command */
	{ 0, NULL, 0, 0 }
} ;

/**************************************************************************************************/

static int
jjy_start_tristate_gpsclock01 ( int unit, struct peer *peer, struct jjyunit *up )
{

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, "Refclock: Tristate Ltd. TS-GPS01" ) ;

	up->unittype  = UNITTYPE_TRISTATE_GPSCLOCK01 ;
	up->linespeed = SPEED232_TRISTATE_GPSCLOCK01 ;
	up->linediscipline = LDISC_CLK ;

	return 0 ;

}

/**************************************************************************************************/

static int
jjy_receive_tristate_gpsclock01 ( struct recvbuf *rbufp )
{
#ifdef DEBUG
	static	const char	*sFunctionName = "jjy_receive_tristate_gpsclock01" ;
#endif

	struct jjyunit	    *up ;
	struct refclockproc *pp ;
	struct peer	    *peer;

	char *		pBuf ;
	char		sLog [ 100 ] ;
	int 		iLen ;
	int 		rc ;

	const char *	pCmd ;
	int 		iCmdLen ;

	/* Initialize pointers */

	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	DEBUG_PRINTF_JJY_RECEIVE( "jjy_receive_tristate_gpsclock01", iLen ) ;

	/* Ignore NMEA data stream */

	if ( iLen > 5
	  && ( strncmp( pBuf, "$GP", 3 ) == 0 || strncmp( pBuf, "$PFEC", 5 ) == 0 ) ) {
#ifdef DEBUG
		if ( debug ) {
			printf ( "%s (refclock_jjy.c) : Skip NMEA stream [%s]\n",
				sFunctionName, pBuf ) ;
		}
#endif
		return JJY_RECEIVE_WAIT ;
	}

	/*
	 * Skip command prompt '$Cmd>' from the TS-GPSclock-01
	 */
	if ( iLen == 5 && strncmp( pBuf, "$Cmd>", 5 ) == 0 ) {
		return JJY_RECEIVE_WAIT ;
	} else if ( iLen > 5 && strncmp( pBuf, "$Cmd>", 5 ) == 0 ) {
		pBuf += 5 ;
		iLen -= 5 ;
	}

	/*
	 * Ignore NMEA data stream after command prompt
	 */
	if ( iLen > 5
	  && ( strncmp( pBuf, "$GP", 3 ) == 0 || strncmp( pBuf, "$PFEC", 5 ) == 0 ) ) {
#ifdef DEBUG
		if ( debug ) {
			printf ( "%s (refclock_jjy.c) : Skip NMEA stream [%s]\n",
				sFunctionName, pBuf ) ;
		}
#endif
		return JJY_RECEIVE_WAIT ;
	}

	/* Check expected reply */

	if ( tristate_gps01_command_sequence[up->iCommandSeq].command == NULL ) {
		/* Command sequence has not been started, or has been completed */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_UNEXPECTED_REPLY,
			  pBuf ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	/* Check reply length */

	if ( iLen != tristate_gps01_command_sequence[up->iCommandSeq].iExpectedReplyLength ) {
		/* Unexpected reply length */
		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_LENGTH,
			  iLen ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;
	}

	/* Parse reply */

	switch ( tristate_gps01_command_sequence[up->iCommandSeq].commandNumber ) {

	case TS_GPS01_COMMAND_NUMBER_DATE : /* YYYY/MM/DD */

		rc = sscanf ( pBuf, "%4d/%2d/%2d", &up->year, &up->month, &up->day ) ;

		if ( rc != 3 || up->year < 2000 || 2099 <= up->year
		  || up->month < 1 || 12 < up->month
		  || up->day < 1 || 31 < up->day ) {
			/* Invalid date */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATE,
				  rc, up->year, up->month, up->day ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		break ;

	case TS_GPS01_COMMAND_NUMBER_TIME : /* HH:MM:SS */

		if ( up->iTimestampCount >= 2 ) {
			/* Too many time reply */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_TOO_MANY_REPLY,
				  up->iTimestampCount ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		rc = sscanf ( pBuf, "%2d:%2d:%2d",
			      &up->hour, &up->minute, &up->second ) ;

		if ( rc != 3
		  || up->hour > 23 || up->minute > 59 || up->second > 60 ) {
			/* Invalid time */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_TIME,
				  rc, up->hour, up->minute, up->second ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		up->iTimestamp[up->iTimestampCount] = ( up->hour * 60 + up->minute ) * 60 + up->second ;

		up->iTimestampCount++ ;

		up->msecond = 0 ;

		break ;

	case TS_GPS01_COMMAND_NUMBER_STUS :

		if ( strncmp( pBuf, TS_GPS01_REPLY_STUS_RTC, TS_GPS01_REPLY_LENGTH_STUS ) == 0
		  || strncmp( pBuf, TS_GPS01_REPLY_STUS_GPS, TS_GPS01_REPLY_LENGTH_STUS ) == 0
		  || strncmp( pBuf, TS_GPS01_REPLY_STUS_UTC, TS_GPS01_REPLY_LENGTH_STUS ) == 0
		  || strncmp( pBuf, TS_GPS01_REPLY_STUS_PPS, TS_GPS01_REPLY_LENGTH_STUS ) == 0 ) {
			/* Good */
		} else {
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_REPLY,
				  pBuf ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		break ;

	default : /*  Unexpected reply */

		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_REPLY,
			  pBuf ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;

	}

	if ( up->iTimestampCount == 2 ) {
		/* Process date and time */

		if ( up->iTimestamp[1] - 2 <= up->iTimestamp[0]
		  && up->iTimestamp[0]     <= up->iTimestamp[1] ) {
			/* 3 commands (time,date,stim) was excuted in two seconds */
			jjy_synctime( peer, pp, up ) ;
			return JJY_RECEIVE_DONE ;
		} else if ( up->iTimestamp[0] > up->iTimestamp[1] ) {
			/* Over midnight, and date is unsure */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_OVER_MIDNIGHT_2,
				  up->iTimestamp[0], up->iTimestamp[1] ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_INFORMATION, sLog ) ;
			return JJY_RECEIVE_SKIP ;
		} else {
			/* Slow reply */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SLOW_REPLY_2,
				  up->iTimestamp[0], up->iTimestamp[1] ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

	}

	if ( tristate_gps01_command_sequence[up->iCommandSeq].command == NULL ) {
		/* Command sequence completed */
		jjy_synctime( peer, pp, up ) ;
		return JJY_RECEIVE_DONE ;
	}

	/* Issue next command */

	if ( tristate_gps01_command_sequence[up->iCommandSeq].command != NULL ) {
		up->iCommandSeq ++ ;
	}

	if ( tristate_gps01_command_sequence[up->iCommandSeq].command == NULL ) {
		/* Command sequence completed */
		up->iProcessState = JJY_PROCESS_STATE_DONE ;
		return JJY_RECEIVE_DONE ;
	}

	pCmd =  tristate_gps01_command_sequence[up->iCommandSeq].command ;
	iCmdLen = tristate_gps01_command_sequence[up->iCommandSeq].commandLength ;
	if ( write ( pp->io.fd, pCmd, iCmdLen ) != iCmdLen ) {
		refclock_report ( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

	return JJY_RECEIVE_WAIT ;

}

/**************************************************************************************************/

static void
jjy_poll_tristate_gpsclock01 ( int unit, struct peer *peer )
{
#ifdef DEBUG
	static const char *sFunctionName = "jjy_poll_tristate_gpsclock01" ;
#endif

	struct refclockproc *pp ;
	struct jjyunit	    *up ;

	const char *	pCmd ;
	int		iCmdLen ;

	pp = peer->procptr ;
	up = pp->unitptr ;

	up->iTimestampCount = 0 ;

	if ( ( pp->sloppyclockflag & CLK_FLAG1 ) == 0 ) {
		/* Skip "stus" command */
		up->iCommandSeq = 1 ;
		up->iLineCount = 1 ;
	}

#ifdef DEBUG
	if ( debug ) {
		printf ( "%s (refclock_jjy.c) : flag1=%X CLK_FLAG1=%X up->iLineCount=%d\n",
			sFunctionName, pp->sloppyclockflag, CLK_FLAG1,
			up->iLineCount ) ;
	}
#endif

	/*
	 * Send a first command
	 */

	up->iCommandSeq ++ ;

	pCmd =  tristate_gps01_command_sequence[up->iCommandSeq].command ;
	iCmdLen = tristate_gps01_command_sequence[up->iCommandSeq].commandLength ;
	if ( write ( pp->io.fd, pCmd, iCmdLen ) != iCmdLen ) {
		refclock_report ( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    The SEIKO TIME SYSTEMS TDC-300								##*/
/*##												##*/
/*##    server  127.127.40.X  mode 6								##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/
/*                                                                                                */
/*  Type                  Response                                  Remarks                       */
/*  --------------------  ----------------------------------------  ----------------------------  */
/*  Type 1                <STX>HH:MM:SS<ETX>                                                      */
/*  Type 2                <STX>YYMMDDHHMMSSWLSCU<ETX>               W:0(Sun)-6(Sat)               */
/*  Type 3                <STX>YYMMDDWHHMMSS<ETX>                   W:0(Sun)-6(Sat)               */
/*                        <STX><xE5><ETX>                           5 to 10 mSec. before second   */
/*                                                                                                */
/*################################################################################################*/

static struct jjyRawDataBreak seiko_tsys_tdc_300_raw_break [ ] =
{
	{ "\x03", 1 }, { NULL, 0 }
} ;

/**************************************************************************************************/

static int
jjy_start_seiko_tsys_tdc_300 ( int unit, struct peer *peer, struct jjyunit *up )
{

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, "Refclock: SEIKO TIME SYSTEMS TDC-300" ) ;

	up->unittype  = UNITTYPE_SEIKO_TIMESYS_TDC_300 ;
	up->linespeed = SPEED232_SEIKO_TIMESYS_TDC_300 ;
	up->linediscipline = LDISC_RAW ;

	up->pRawBreak = seiko_tsys_tdc_300_raw_break ;
	up->bWaitBreakString = TRUE ;

	up->bSkipCntrlCharOnly = FALSE ;

	return 0 ;

}

/**************************************************************************************************/

static int
jjy_receive_seiko_tsys_tdc_300 ( struct recvbuf *rbufp )
{

	struct peer		*peer;
	struct refclockproc	*pp ;
	struct jjyunit		*up ;

	char	*pBuf, sLog [ 100 ] ;
	int	iLen, i ;
	int	rc, iWeekday ;
	time_t	now ;
	struct	tm	*pTime ;

	/* Initialize pointers */

	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	DEBUG_PRINTF_JJY_RECEIVE( "jjy_receive_seiko_tsys_tdc_300", iLen ) ;

	/*
	 * TDC-300 sends a timestamp every second.
	 * So, a timestamp is ignored unless it is right after polled.
	 */

	if ( up->iProcessState != JJY_PROCESS_STATE_RECEIVE ) {
		return JJY_RECEIVE_SKIP ;
	}

	/* Process timestamp */

	up->iReceiveSeq ++ ;

	switch ( iLen ) {

	case 8 : /* Type 1 : <STX>HH:MM:SS<ETX> */

		for ( i = 0 ; i < iLen ; i ++ ) {
			pBuf[i] &= 0x7F ;
		}

		rc = sscanf ( pBuf+1, "%2d:%2d:%2d",
		      &up->hour, &up->minute, &up->second ) ;

		if ( rc != 3
		  || up->hour > 23 || up->minute > 59 || up->second > 60 ) {
			/* Invalid time */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_TIME,
				  rc, up->hour, up->minute, up->second ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		} else if ( up->hour == 23 && up->minute == 59 && up->second >= 55 ) {
			/* Uncertainty date guard */
			return JJY_RECEIVE_WAIT ;
		}

		time( &now ) ;
		pTime = localtime( &now ) ;
		up->year  = pTime->tm_year ;
		up->month = pTime->tm_mon + 1 ;
		up->day   = pTime->tm_mday ;

		break ;

	case 17 : /* Type 2 : <STX>YYMMDDHHMMSSWLSCU<ETX> */

		for ( i = 0 ; i < iLen ; i ++ ) {
			pBuf[i] &= 0x7F ;
		}

		rc = sscanf ( pBuf+1, "%2d%2d%2d%2d%2d%2d%1d",
		      &up->year, &up->month, &up->day,
		      &up->hour, &up->minute, &up->second, &iWeekday ) ;

		if ( rc != 7
		  || up->month < 1 || up->month > 12 || up->day < 1 || up->day > 31
		  || iWeekday > 6
		  || up->hour > 23 || up->minute > 59 || up->second > 60 ) {
			/* Invalid date and time */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATETIME,
				  rc, up->year, up->month, up->day,
				  up->hour, up->minute, up->second ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		break ;

	case 13 : /* Type 3 : <STX>YYMMDDWHHMMSS<ETX> */

		rc = sscanf ( pBuf, "%2d%2d%2d%1d%2d%2d%2d",
		      &up->year, &up->month, &up->day, &iWeekday,
		      &up->hour, &up->minute, &up->second ) ;

		if ( rc != 7
		  || up->month < 1 || up->month > 12 || up->day < 1 || up->day > 31
		  || iWeekday > 6
		  || up->hour > 23 || up->minute > 59 || up->second > 60 ) {
			/* Invalid date and time */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATETIME,
				  rc, up->year, up->month, up->day,
				  up->hour, up->minute, up->second ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		return JJY_RECEIVE_WAIT ;

	case 1 : /* Type 3 : <STX><xE5><ETX> */

		if ( ( *pBuf & 0xFF ) != 0xE5 ) {
			/* Invalid second signal */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_REPLY,
				  up->sLineBuf ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		} else if ( up->iReceiveSeq == 1 ) {
			/* Wait for next timestamp */
			up->iReceiveSeq -- ;
			return JJY_RECEIVE_WAIT ;
		} else if ( up->iReceiveSeq >= 3 ) {
			/* Unexpected second signal */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_UNEXPECTED_REPLY,
				  up->sLineBuf ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
			return JJY_RECEIVE_ERROR ;
		}

		break ;

	default : /* Unexpected reply length */

		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_INVALID_LENGTH,
			  iLen ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
		up->bLineError = TRUE ;
		return JJY_RECEIVE_ERROR ;

	}

	up->year += 2000 ;
	up->msecond = 0 ;

	jjy_synctime( peer, pp, up ) ;

	return JJY_RECEIVE_DONE ;

}

/**************************************************************************************************/

static void
jjy_poll_seiko_tsys_tdc_300 ( int unit, struct peer *peer )
{

	struct refclockproc *pp ;
	struct jjyunit	    *up ;

	pp = peer->procptr ;
	up = pp->unitptr ;

	up->bLineError = FALSE ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    Telephone JJY										##*/
/*##												##*/
/*##    server  127.127.40.X  mode 100 to 180							##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/
/*                                                                                                */
/*  Prompt                Command               Response              Remarks                     */
/*  --------------------  --------------------  --------------------  --------------------------  */
/*  Name<SP>?<SP>         TJJY<CR>              Welcome messages      TJJY is a guest user ID     */
/*  >                     4DATE<CR>             YYYYMMDD<CR>                                      */
/*  >                     LEAPSEC<CR>           XX<CR>                One of <SP>0, +1, -1        */
/*  >                     TIME<CR>              HHMMSS<CR>            3 times on second           */
/*  >                     BYE<CR>               Sayounara messages                                */
/*                                                                                                */
/*################################################################################################*/

static struct jjyRawDataBreak teljjy_raw_break [ ] =
{
	{ "\r\n", 2 },
	{ "\r"  , 1 },
	{ "\n"  , 1 },
	{ "Name ? ", 7 },
	{ ">"   , 1 },
	{ "+++" , 3 },
	{ NULL  , 0 }
} ;

#define	TELJJY_STATE_IDLE	0
#define	TELJJY_STATE_DAILOUT	1
#define	TELJJY_STATE_LOGIN	2
#define	TELJJY_STATE_CONNECT	3
#define	TELJJY_STATE_BYE	4

#define	TELJJY_EVENT_NULL	0
#define	TELJJY_EVENT_START	1
#define	TELJJY_EVENT_CONNECT	2
#define	TELJJY_EVENT_DISCONNECT	3
#define	TELJJY_EVENT_COMMAND	4
#define	TELJJY_EVENT_LOGIN	5	/* Posted by the jjy_receive_telephone */
#define	TELJJY_EVENT_PROMPT	6	/* Posted by the jjy_receive_telephone */
#define	TELJJY_EVENT_DATA	7	/* Posted by the jjy_receive_telephone */
#define	TELJJY_EVENT_ERROR	8	/* Posted by the jjy_receive_telephone */
#define	TELJJY_EVENT_SILENT	9	/* Posted by the jjy_timer_telephone */
#define	TELJJY_EVENT_TIMEOUT	10	/* Posted by the jjy_timer_telephone */

static	void 	teljjy_control		( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;

static	int 	teljjy_idle_ignore	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_idle_dialout	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_dial_ignore	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_dial_login	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_dial_disc	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_login_ignore	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_login_disc	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_login_conn	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_login_login	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_login_silent	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_login_error	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_conn_ignore	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_conn_disc	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_conn_send	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_conn_data	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_conn_silent	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_conn_error	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_bye_ignore	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_bye_disc 	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;
static	int 	teljjy_bye_modem	( struct peer *peer, struct refclockproc *, struct jjyunit * ) ;

static int ( *pTeljjyHandler [ ] [ 5 ] ) ( struct peer *, struct refclockproc *, struct jjyunit *) =
{               	/*STATE_IDLE           STATE_DAILOUT       STATE_LOGIN           STATE_CONNECT       STATE_BYE        */
/* NULL       */	{ teljjy_idle_ignore , teljjy_dial_ignore, teljjy_login_ignore, teljjy_conn_ignore, teljjy_bye_ignore },
/* START      */	{ teljjy_idle_dialout, teljjy_dial_ignore, teljjy_login_ignore, teljjy_conn_ignore, teljjy_bye_ignore },
/* CONNECT    */	{ teljjy_idle_ignore , teljjy_dial_login , teljjy_login_ignore, teljjy_conn_ignore, teljjy_bye_ignore },
/* DISCONNECT */	{ teljjy_idle_ignore , teljjy_dial_disc  , teljjy_login_disc  , teljjy_conn_disc  , teljjy_bye_disc   },
/* COMMAND    */	{ teljjy_idle_ignore , teljjy_dial_ignore, teljjy_login_ignore, teljjy_conn_ignore, teljjy_bye_modem  },
/* LOGIN      */	{ teljjy_idle_ignore , teljjy_dial_ignore, teljjy_login_login , teljjy_conn_error , teljjy_bye_ignore },
/* PROMPT     */	{ teljjy_idle_ignore , teljjy_dial_ignore, teljjy_login_conn  , teljjy_conn_send  , teljjy_bye_ignore },
/* DATA       */	{ teljjy_idle_ignore , teljjy_dial_ignore, teljjy_login_ignore, teljjy_conn_data  , teljjy_bye_ignore },
/* ERROR      */	{ teljjy_idle_ignore , teljjy_dial_ignore, teljjy_login_error , teljjy_conn_error , teljjy_bye_ignore },
/* SILENT     */	{ teljjy_idle_ignore , teljjy_dial_ignore, teljjy_login_silent, teljjy_conn_silent, teljjy_bye_ignore },
/* TIMEOUT    */	{ teljjy_idle_ignore , teljjy_dial_disc  , teljjy_login_error , teljjy_conn_error , teljjy_bye_modem  }
} ;

static short iTeljjyNextState [ ] [ 5 ] =
{               	/*STATE_IDLE            STATE_DAILOUT         STATE_LOGIN           STATE_CONNECT         STATE_BYE         */
/* NULL       */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_DAILOUT, TELJJY_STATE_LOGIN  , TELJJY_STATE_CONNECT, TELJJY_STATE_BYE  },
/* START      */	{ TELJJY_STATE_DAILOUT, TELJJY_STATE_DAILOUT, TELJJY_STATE_LOGIN  , TELJJY_STATE_CONNECT, TELJJY_STATE_BYE  },
/* CONNECT    */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_LOGIN  , TELJJY_STATE_LOGIN  , TELJJY_STATE_CONNECT, TELJJY_STATE_BYE  },
/* DISCONNECT */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_IDLE   , TELJJY_STATE_IDLE   , TELJJY_STATE_IDLE   , TELJJY_STATE_IDLE },
/* COMMAND    */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_DAILOUT, TELJJY_STATE_LOGIN  , TELJJY_STATE_CONNECT, TELJJY_STATE_BYE  },
/* LOGIN      */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_DAILOUT, TELJJY_STATE_LOGIN  , TELJJY_STATE_BYE    , TELJJY_STATE_BYE  },
/* PROMPT     */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_DAILOUT, TELJJY_STATE_CONNECT, TELJJY_STATE_BYE    , TELJJY_STATE_BYE  },
/* DATA       */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_DAILOUT, TELJJY_STATE_LOGIN  , TELJJY_STATE_CONNECT, TELJJY_STATE_BYE  },
/* ERROR      */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_DAILOUT, TELJJY_STATE_BYE    , TELJJY_STATE_BYE    , TELJJY_STATE_BYE  },
/* SILENT     */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_DAILOUT, TELJJY_STATE_LOGIN  , TELJJY_STATE_CONNECT, TELJJY_STATE_BYE  },
/* TIMEOUT    */	{ TELJJY_STATE_IDLE   , TELJJY_STATE_IDLE   , TELJJY_STATE_BYE    , TELJJY_STATE_BYE    , TELJJY_STATE_BYE  }
} ;

static short iTeljjyPostEvent [ ] [ 5 ] =
{               	/*STATE_IDLE         STATE_DAILOUT      STATE_LOGIN           STATE_CONNECT         STATE_BYE         */
/* NULL       */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL },
/* START      */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL },
/* CONNECT    */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL },
/* DISCONNECT */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL },
/* COMMAND    */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL },
/* LOGIN      */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_NULL   , TELJJY_EVENT_COMMAND, TELJJY_EVENT_NULL },
/* PROMPT     */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_PROMPT , TELJJY_EVENT_COMMAND, TELJJY_EVENT_NULL },
/* DATA       */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL },
/* ERROR      */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_COMMAND, TELJJY_EVENT_COMMAND, TELJJY_EVENT_NULL },
/* SILENT     */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL   , TELJJY_EVENT_NULL },
/* TIMEOUT    */	{ TELJJY_EVENT_NULL, TELJJY_EVENT_NULL, TELJJY_EVENT_COMMAND, TELJJY_EVENT_COMMAND, TELJJY_EVENT_NULL }
} ;

static short iTeljjySilentTimeout [ 5 ] = { 0,   0, 10,  5,  0 } ;
static short iTeljjyStateTimeout  [ 5 ] = { 0, 120, 60, 60, 40 } ;

#define	TELJJY_STAY_CLOCK_STATE  	0
#define	TELJJY_CHANGE_CLOCK_STATE	1

/* Command and replay */

#define	TELJJY_REPLY_NONE	0
#define	TELJJY_REPLY_4DATE	1
#define	TELJJY_REPLY_TIME	2
#define	TELJJY_REPLY_LEAPSEC	3
#define	TELJJY_REPLY_LOOP	4
#define	TELJJY_REPLY_PROMPT	5
#define	TELJJY_REPLY_LOOPBACK	6
#define	TELJJY_REPLY_COM	7

#define	TELJJY_COMMAND_START_SKIP_LOOPBACK	7

static  struct
{
	const char	*command ;
	int	commandLength ;
	int	iEchobackReplyLength ;
	int	iExpectedReplyType   ;
	int	iExpectedReplyLength ;
} teljjy_command_sequence[] =
{
	{ NULL, 0, 0, 0, 0 }, /* Idle */
	{ "LOOP\r"   , 5, 4, TELJJY_REPLY_LOOP    , 0 }, /* Getting into loopback mode */
	{ ">"        , 1, 1, TELJJY_REPLY_LOOPBACK, 0 }, /* Loopback measuring of delay time */
	{ ">"        , 1, 1, TELJJY_REPLY_LOOPBACK, 0 }, /* Loopback measuring of delay time */
	{ ">"        , 1, 1, TELJJY_REPLY_LOOPBACK, 0 }, /* Loopback measuring of delay time */
	{ ">"        , 1, 1, TELJJY_REPLY_LOOPBACK, 0 }, /* Loopback measuring of delay time */
	{ ">"        , 1, 1, TELJJY_REPLY_LOOPBACK, 0 }, /* Loopback measuring of delay time */
	{ "COM\r"    , 4, 3, TELJJY_REPLY_COM     , 0 }, /* Exit from loopback mode */
	/* TELJJY_COMMAND_START_SKIP_LOOPBACK */
	{ "TIME\r"   , 5, 4, TELJJY_REPLY_TIME    , 6 },
	{ "4DATE\r"  , 6, 5, TELJJY_REPLY_4DATE   , 8 },
	{ "LEAPSEC\r", 8, 7, TELJJY_REPLY_LEAPSEC , 2 },
	{ "TIME\r"   , 5, 4, TELJJY_REPLY_TIME    , 6 },
	{ "BYE\r"    , 4, 3, TELJJY_REPLY_NONE    , 0 },
	/* End of command */
	{ NULL, 0, 0, 0, 0 }
} ;

#define	TELJJY_LOOPBACK_DELAY_THRESHOLD		700 /* Milli second */

#ifdef	DEBUG
#define	DEBUG_TELJJY_PRINTF(sFunc)	{ if ( debug ) { printf ( "refclock_jjy.c : %s : iClockState=%d iClockEvent=%d iTeljjySilentTimer=%d iTeljjyStateTimer=%d iClockCommandSeq=%d\n", sFunc, up->iClockState, up->iClockEvent, up->iTeljjySilentTimer, up->iTeljjyStateTimer, up->iClockCommandSeq ) ; } }
#else
#define	DEBUG_TELJJY_PRINTF(sFunc)
#endif

/**************************************************************************************************/

static int
jjy_start_telephone ( int unit, struct peer *peer, struct jjyunit *up )
{

	char	sLog [ 80 ], sFirstThreeDigits [ 4 ] ;
	int	iNumberOfDigitsOfPhoneNumber, iCommaCount, iCommaPosition ;
	size_t  i ;
	size_t	iFirstThreeDigitsCount ;

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, "Refclock: Telephone JJY" ) ;

	up->unittype  = UNITTYPE_TELEPHONE ;
	up->linespeed = SPEED232_TELEPHONE ;
	up->linediscipline = LDISC_RAW ;

	up->pRawBreak = teljjy_raw_break ;
	up->bWaitBreakString = TRUE ;

	up->bSkipCntrlCharOnly = TRUE ;

	up->iClockState = TELJJY_STATE_IDLE ;
	up->iClockEvent = TELJJY_EVENT_NULL ;

	/* Check the telephone number */

	if ( sys_phone[0] == NULL ) {
		msyslog( LOG_ERR, "refclock_jjy.c : jjy_start_telephone : phone in the ntpd.conf must be specified." ) ;
		up->bInitError = TRUE ;
		return 1 ;
	}

	if ( sys_phone[1] != NULL ) {
		msyslog( LOG_ERR, "refclock_jjy.c : jjy_start_telephone : phone in the ntpd.conf should be only one." ) ;
		up->bInitError = TRUE ;
		return 1 ;
	}

	iNumberOfDigitsOfPhoneNumber = iCommaCount = iCommaPosition = iFirstThreeDigitsCount = 0 ;
	for ( i = 0 ; i < strlen( sys_phone[0] ) ; i ++ ) {
		if ( isdigit( (u_char)sys_phone[0][i] ) ) {
			if ( iFirstThreeDigitsCount < sizeof(sFirstThreeDigits)-1 ) {
				sFirstThreeDigits[iFirstThreeDigitsCount++] = sys_phone[0][i] ;
			}
			iNumberOfDigitsOfPhoneNumber ++ ;
		} else if ( sys_phone[0][i] == ',' ) {
			iCommaCount ++ ;
			if ( iCommaCount > 1 ) {
				msyslog( LOG_ERR, "refclock_jjy.c : jjy_start_telephone : phone in the ntpd.conf should be zero or one comma." ) ;
				up->bInitError = TRUE ;
				return 1 ;
			}
			iFirstThreeDigitsCount = 0 ;
			iCommaPosition = i ;
		} else if ( sys_phone[0][i] != '-' ) {
			msyslog( LOG_ERR, "refclock_jjy.c : jjy_start_telephone : phone in the ntpd.conf should be a number or a hyphen." ) ;
			up->bInitError = TRUE ;
			return 1 ;
		}
	}
	sFirstThreeDigits[iFirstThreeDigitsCount] = 0 ;

	if ( iCommaCount == 1 ) {
		if ( iCommaPosition != 1 || *sys_phone[0] != '0' ) {
			msyslog( LOG_ERR, "refclock_jjy.c : jjy_start_telephone : Getting an outside line should be '0,'." ) ;
			up->bInitError = TRUE ;
			return 1 ;
		}
	}

	if ( iNumberOfDigitsOfPhoneNumber - iCommaPosition < 6 || 10 < iNumberOfDigitsOfPhoneNumber - iCommaPosition ) {
		/* Too short or too long */
		msyslog( LOG_ERR, "refclock_jjy.c : jjy_start_telephone : phone=%s : Number of digits should be 6 to 10.", sys_phone[0] ) ;
		up->bInitError = TRUE ;
		return 1 ;
	}

	if ( strncmp( sFirstThreeDigits + iCommaPosition, "00" , 2 ) == 0
	  || strncmp( sFirstThreeDigits + iCommaPosition, "10" , 2 ) == 0
	  || strncmp( sFirstThreeDigits + iCommaPosition, "11" , 2 ) == 0
	  || strncmp( sFirstThreeDigits + iCommaPosition, "12" , 2 ) == 0
	  || strncmp( sFirstThreeDigits + iCommaPosition, "171", 3 ) == 0
	  || strncmp( sFirstThreeDigits + iCommaPosition, "177", 3 ) == 0
	  || ( sFirstThreeDigits[0] == '0' &&  sFirstThreeDigits[2] == '0' ) ) {
		/* Not allowed because of emergency numbers or special service numbers */
		msyslog( LOG_ERR, "refclock_jjy.c : jjy_start_telephone : phone=%s : First 2 or 3 digits are not allowed.", sys_phone[0] ) ;
		up->bInitError = TRUE ;
		return 1 ;
	}

	snprintf( sLog, sizeof(sLog), "phone=%s", sys_phone[0] ) ;
	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, sLog ) ;

	if ( peer->minpoll < 8 ) {
		/* minpoll must be greater or equal to 8 ( 256 seconds = about 4 minutes ) */
		int oldminpoll = peer->minpoll ;
		peer->minpoll = 8 ;
		if ( peer->ppoll < peer->minpoll ) {
			peer->ppoll = peer->minpoll ;
		}
		if ( peer->maxpoll < peer->minpoll ) {
			peer->maxpoll = peer->minpoll ;
		}
		snprintf( sLog, sizeof(sLog), "minpoll=%d -> %d", oldminpoll, peer->minpoll ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_JJY, sLog ) ;
	}

	return 0 ;

}

/**************************************************************************************************/

static int
jjy_receive_telephone ( struct recvbuf *rbufp )
{
#ifdef DEBUG
	static	const char	*sFunctionName = "jjy_receive_telephone" ;
#endif

	struct	peer         *peer;
	struct	refclockproc *pp ;
	struct	jjyunit      *up ;
	char	*pBuf ;
	int	iLen ;
	short	iPreviousModemState ;

	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	DEBUG_TELJJY_PRINTF( sFunctionName ) ;

	if ( up->iClockState == TELJJY_STATE_IDLE
	  || up->iClockState == TELJJY_STATE_DAILOUT
	  || up->iClockState == TELJJY_STATE_BYE ) {

		iPreviousModemState = getModemState( up ) ;

		modem_receive ( rbufp ) ;

		if ( iPreviousModemState != up->iModemState ) {
			/* Modem state is changed just now. */
			if ( isModemStateDisconnect( up->iModemState ) ) {
				up->iClockEvent = TELJJY_EVENT_DISCONNECT ;
				teljjy_control ( peer, pp, up ) ;
			} else if ( isModemStateConnect( up->iModemState ) ) {
				up->iClockEvent = TELJJY_EVENT_CONNECT ;
				teljjy_control ( peer, pp, up ) ;
			}
		}

		return JJY_RECEIVE_WAIT ;

	}

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	up->iTeljjySilentTimer = 0 ;
	if      ( iLen == 7 && strncmp( pBuf, "Name ? ", 7 ) == 0 ) { up->iClockEvent = TELJJY_EVENT_LOGIN  ; }
	else if ( iLen == 1 && strncmp( pBuf, ">"      , 1 ) == 0 ) { up->iClockEvent = TELJJY_EVENT_PROMPT ; }
	else if ( iLen >= 1 && strncmp( pBuf, "?"      , 1 ) == 0 ) { up->iClockEvent = TELJJY_EVENT_ERROR  ; }
	else                                                        { up->iClockEvent = TELJJY_EVENT_DATA   ; }

	teljjy_control ( peer, pp, up ) ;

	return JJY_RECEIVE_WAIT ;

}

/**************************************************************************************************/

static void
jjy_poll_telephone ( int unit, struct peer *peer )
{
#ifdef DEBUG
	static const char *sFunctionName = "jjy_poll_telephone" ;
#endif

	struct	refclockproc *pp ;
	struct	jjyunit      *up ;

	pp = peer->procptr ;
	up = pp->unitptr ;

	DEBUG_TELJJY_PRINTF( sFunctionName ) ;

	if ( up->iClockState == TELJJY_STATE_IDLE ) {
		up->iRawBufLen = 0 ;
		up->iLineBufLen = 0 ;
		up->iTextBufLen = 0 ;
	}

	up->iClockEvent = TELJJY_EVENT_START ;
	teljjy_control ( peer, pp, up ) ;

}

/**************************************************************************************************/

static void
jjy_timer_telephone ( int unit, struct peer *peer )
{
#ifdef DEBUG
	static const char *sFunctionName = "jjy_timer_telephone" ;
#endif

	struct	refclockproc *pp ;
	struct	jjyunit      *up ;
	short	iPreviousModemState ;

	pp = peer->procptr ;
	up = pp->unitptr ;

	DEBUG_TELJJY_PRINTF( sFunctionName ) ;

	if ( iTeljjySilentTimeout[up->iClockState] != 0 ) {
		up->iTeljjySilentTimer++ ;
		if ( iTeljjySilentTimeout[up->iClockState] <= up->iTeljjySilentTimer ) {
			up->iClockEvent = TELJJY_EVENT_SILENT ;
			teljjy_control ( peer, pp, up ) ;
		}
	}

	if ( iTeljjyStateTimeout[up->iClockState] != 0 ) {
		up->iTeljjyStateTimer++ ;
		if ( iTeljjyStateTimeout[up->iClockState] <= up->iTeljjyStateTimer ) {
			up->iClockEvent = TELJJY_EVENT_TIMEOUT ;
			teljjy_control ( peer, pp, up ) ;
		}
	}

	if ( isModemStateTimerOn( up ) ) {

		iPreviousModemState = getModemState( up ) ;

		modem_timer ( unit, peer ) ;

		if ( iPreviousModemState != up->iModemState ) {
			/* Modem state is changed just now. */
			if ( isModemStateDisconnect( up->iModemState ) ) {
				up->iClockEvent = TELJJY_EVENT_DISCONNECT ;
				teljjy_control ( peer, pp, up ) ;
			} else if ( isModemStateConnect( up->iModemState ) ) {
				up->iClockEvent = TELJJY_EVENT_CONNECT ;
				teljjy_control ( peer, pp, up ) ;
			}
		}

	}

}

/**************************************************************************************************/

static void
teljjy_control ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	int	i, rc ;
	short	iPostEvent = TELJJY_EVENT_NULL ;

	DEBUG_TELJJY_PRINTF( "teljjy_control" ) ;

	rc = (*pTeljjyHandler[up->iClockEvent][up->iClockState])( peer, pp, up ) ;

	if ( rc == TELJJY_CHANGE_CLOCK_STATE ) {
		iPostEvent = iTeljjyPostEvent[up->iClockEvent][up->iClockState] ;
#ifdef DEBUG
		if ( debug ) {
			printf( "refclock_jjy.c : teljjy_control : iClockState=%hd -> %hd  iPostEvent=%hd\n",
				up->iClockState, iTeljjyNextState[up->iClockEvent][up->iClockState], iPostEvent ) ;
		}
#endif
		up->iTeljjySilentTimer = 0 ;
		if ( up->iClockState != iTeljjyNextState[up->iClockEvent][up->iClockState] ) {
			/* Telephone JJY state is changing now */
			up->iTeljjyStateTimer = 0 ;
			up->bLineError = FALSE ;
			up->iClockCommandSeq = 0 ;
			up->iTimestampCount = 0 ;
			up->iLoopbackCount = 0 ;
			for ( i = 0 ; i < MAX_LOOPBACK ; i ++ ) {
				up->bLoopbackTimeout[i] = FALSE ;
			}
			if (iTeljjyNextState[up->iClockEvent][up->iClockState] == TELJJY_STATE_IDLE ) {
				/* Telephone JJY state is changing to IDLE just now */
				up->iProcessState = JJY_PROCESS_STATE_DONE ;
			}
		}
		up->iClockState = iTeljjyNextState[up->iClockEvent][up->iClockState] ;

	}

	if ( iPostEvent != TELJJY_EVENT_NULL ) {
		up->iClockEvent = iPostEvent ;
		teljjy_control ( peer, pp, up ) ;
	}

	up->iClockEvent = TELJJY_EVENT_NULL ;

}

/**************************************************************************************************/

static void
teljjy_setDelay ( struct peer *peer, struct jjyunit *up )
{

	char	sLog [ 60 ] ;
	int	milliSecond, microSecond ;

	gettimeofday( &(up->delayTime[up->iLoopbackCount]), NULL ) ;

	up->delayTime[up->iLoopbackCount].tv_sec  -= up->sendTime[up->iLoopbackCount].tv_sec ;
	up->delayTime[up->iLoopbackCount].tv_usec -= up->sendTime[up->iLoopbackCount].tv_usec ;
	if ( up->delayTime[up->iLoopbackCount].tv_usec < 0 ) {
		up->delayTime[up->iLoopbackCount].tv_sec -- ;
		up->delayTime[up->iLoopbackCount].tv_usec += 1000000 ;
	}

	milliSecond  = up->delayTime[up->iLoopbackCount].tv_usec / 1000 ;
	microSecond  = up->delayTime[up->iLoopbackCount].tv_usec - milliSecond * 1000 ;
	milliSecond += up->delayTime[up->iLoopbackCount].tv_sec * 1000 ;

	snprintf( sLog, sizeof(sLog), JJY_CLOCKSTATS_MESSAGE_LOOPBACK_DELAY,
		  milliSecond, microSecond ) ;

	if ( milliSecond > TELJJY_LOOPBACK_DELAY_THRESHOLD ) {
		/* Delay > 700 mS */
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_WARNING, sLog ) ;
	} else {
		/* Delay <= 700 mS */
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_INFORMATION, sLog ) ;
	}

}

/**************************************************************************************************/

static int
teljjy_getDelay ( struct peer *peer, struct jjyunit *up )
{

	struct	timeval maxTime, minTime, averTime ;
	int	i ;
	int	minIndex = 0, maxIndex = 0, iAverCount = 0 ;
	int	iThresholdSecond, iThresholdMicroSecond ;
	int	iPercent ;

	minTime.tv_sec = minTime.tv_usec = 0 ;
	maxTime.tv_sec = maxTime.tv_usec = 0 ;

	iThresholdSecond = TELJJY_LOOPBACK_DELAY_THRESHOLD / 1000 ;
	iThresholdMicroSecond = ( TELJJY_LOOPBACK_DELAY_THRESHOLD - ( TELJJY_LOOPBACK_DELAY_THRESHOLD / 1000 ) * 1000 ) * 1000 ;

	up->iLoopbackValidCount = 0 ;

	for ( i = 0 ; i < MAX_LOOPBACK && i < up->iLoopbackCount ; i ++ ) {
		if ( up->bLoopbackTimeout[i]
		  || up->delayTime[i].tv_sec  > iThresholdSecond
		|| ( up->delayTime[i].tv_sec == iThresholdSecond
		  && up->delayTime[i].tv_usec > iThresholdMicroSecond ) ) {
			continue ;
		}
		if ( up->iLoopbackValidCount == 0 ) {
			minTime.tv_sec  = up->delayTime[i].tv_sec  ;
			minTime.tv_usec = up->delayTime[i].tv_usec ;
			maxTime.tv_sec  = up->delayTime[i].tv_sec  ;
			maxTime.tv_usec = up->delayTime[i].tv_usec ;
			minIndex = maxIndex = i ;
		} else if ( minTime.tv_sec  > up->delayTime[i].tv_sec
		       || ( minTime.tv_sec == up->delayTime[i].tv_sec
		         && minTime.tv_usec > up->delayTime[i].tv_usec ) ) {
			minTime.tv_sec  = up->delayTime[i].tv_sec  ;
			minTime.tv_usec = up->delayTime[i].tv_usec ;
			minIndex = i ;
		} else if ( maxTime.tv_sec  < up->delayTime[i].tv_sec
		       || ( maxTime.tv_sec == up->delayTime[i].tv_sec
		         && maxTime.tv_usec < up->delayTime[i].tv_usec ) ) {
			maxTime.tv_sec  = up->delayTime[i].tv_sec  ;
			maxTime.tv_usec = up->delayTime[i].tv_usec ;
			maxIndex = i ;
		}
		up->iLoopbackValidCount ++ ;
	}

	if ( up->iLoopbackValidCount < 2 ) {
		return -1 ;
	}

	averTime.tv_usec = 0;

	for ( i = 0 ; i < MAX_LOOPBACK && i < up->iLoopbackCount ; i ++ ) {
		if ( up->bLoopbackTimeout[i]
		  || up->delayTime[i].tv_sec  > iThresholdSecond
		|| ( up->delayTime[i].tv_sec == iThresholdSecond
		  && up->delayTime[i].tv_usec > iThresholdMicroSecond ) ) {
			continue ;
		}
		if ( up->iLoopbackValidCount >= 3 && i == maxIndex ) {
			continue ;
		}
		if ( up->iLoopbackValidCount >= 4 && i == minIndex ) {
			continue ;
		}
		averTime.tv_usec += up->delayTime[i].tv_usec ;
		iAverCount ++ ;
	}

	if ( iAverCount == 0 ) {
		/* This is never happened. */
		/* Previous for-if-for blocks assure iAverCount > 0. */
		/* This code avoids a claim by the coverity scan tool. */
		return -1 ;
	}

	/* mode 101 = 1%, mode 150 = 50%, mode 180 = 80% */

	iPercent = ( peer->ttl - 100 ) ;

	/* Average delay time in milli second */

	return ( ( averTime.tv_usec / iAverCount ) * iPercent ) / 100000 ;

}

/******************************/
static int
teljjy_idle_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_idle_ignore" ) ;

	return TELJJY_STAY_CLOCK_STATE ;

}

/******************************/
static int
teljjy_idle_dialout ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_idle_dialout" ) ;

	modem_connect ( peer->refclkunit, peer ) ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_dial_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_dial_ignore" ) ;

	return TELJJY_STAY_CLOCK_STATE ;

}

/******************************/
static int
teljjy_dial_login ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_dial_login" ) ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_dial_disc ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_dial_disc" ) ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_login_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_login_ignore" ) ;

	return TELJJY_STAY_CLOCK_STATE ;

}

/******************************/
static int
teljjy_login_disc ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_login_disc" ) ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_login_conn ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	int	i ;

	DEBUG_TELJJY_PRINTF( "teljjy_login_conn" ) ;

	up->bLineError = FALSE ;
	up->iClockCommandSeq = 0 ;
	up->iTimestampCount = 0 ;
	up->iLoopbackCount = 0 ;
	for ( i = 0 ; i < MAX_LOOPBACK ; i ++ ) {
		up->bLoopbackTimeout[i] = FALSE ;
	}

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_login_login ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	const char *	pCmd ;
	int		iCmdLen ;

	DEBUG_TELJJY_PRINTF( "teljjy_login_login" ) ;

	/* Send a guest user ID */
	pCmd = "TJJY\r" ;

	/* Send login ID */
	iCmdLen = strlen( pCmd ) ;
	if ( write( pp->io.fd, pCmd, iCmdLen ) != iCmdLen ) {
		refclock_report( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

	return TELJJY_STAY_CLOCK_STATE ;

}

/******************************/
static int
teljjy_login_silent ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_login_silent" ) ;

	if ( write( pp->io.fd, "\r", 1 ) != 1 ) {
		refclock_report( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, "\r" ) ;

	up->iTeljjySilentTimer = 0 ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_login_error ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_login_error" ) ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_conn_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_conn_ignore" ) ;

	return TELJJY_STAY_CLOCK_STATE ;

}

/******************************/
static int
teljjy_conn_disc ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_conn_disc" ) ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_conn_send ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	const char *	pCmd ;
	int		i, iLen, iNextClockState ;
	char	sLog [ 120 ] ;

	DEBUG_TELJJY_PRINTF( "teljjy_conn_send" ) ;

	if ( up->iClockCommandSeq > 0
	  && teljjy_command_sequence[up->iClockCommandSeq].command == NULL ) {
		/* Command sequence has been completed */
	  	return TELJJY_CHANGE_CLOCK_STATE ;
	}

	if ( up->iClockCommandSeq == 0 && peer->ttl == 100 ) {
		/* Skip loopback */

		up->iClockCommandSeq = TELJJY_COMMAND_START_SKIP_LOOPBACK ;

	} else if ( up->iClockCommandSeq == 0 && peer->ttl != 100 ) {
		/* Loopback start */

		up->iLoopbackCount = 0 ;
		for ( i = 0 ; i < MAX_LOOPBACK ; i ++ ) {
			up->bLoopbackTimeout[i] = FALSE ;
		}

	} else if ( up->iClockCommandSeq > 0 && peer->ttl != 100
		 && teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyType == TELJJY_REPLY_LOOPBACK
		 && up->iLoopbackCount < MAX_LOOPBACK ) {
		/* Loopback character comes */
#ifdef DEBUG
		if ( debug ) {
			printf( "refclock_jjy.c : teljjy_conn_send : iClockCommandSeq=%d iLoopbackCount=%d\n",
				 up->iClockCommandSeq, up->iLoopbackCount ) ;
		}
#endif

		teljjy_setDelay( peer, up ) ;

		up->iLoopbackCount ++ ;

	}

	up->iClockCommandSeq++ ;

	pCmd = teljjy_command_sequence[up->iClockCommandSeq].command ;
	iLen = teljjy_command_sequence[up->iClockCommandSeq].commandLength ;
	
	if ( pCmd != NULL ) {

		if ( write( pp->io.fd, pCmd, iLen ) != iLen ) {
			refclock_report( peer, CEVNT_FAULT ) ;
		}

		if ( teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyType == TELJJY_REPLY_LOOPBACK ) {
			/* Loopback character and timestamp */
			if ( up->iLoopbackCount < MAX_LOOPBACK ) {
				gettimeofday( &(up->sendTime[up->iLoopbackCount]), NULL ) ;
				up->bLoopbackMode = TRUE ;
			} else {
				/* This else-block is never come. */
				/* This code avoid wrong report of the coverity static analysis scan tool. */
				snprintf( sLog, sizeof(sLog)-1, "refclock_jjy.c ; teljjy_conn_send ; iClockCommandSeq=%d iLoopbackCount=%d MAX_LOOPBACK=%d",
					  up->iClockCommandSeq, up->iLoopbackCount, MAX_LOOPBACK ) ;
				jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_BUG, sLog ) ;
				msyslog ( LOG_ERR, "%s", sLog ) ;
				up->bLoopbackMode = FALSE ;
			}
		} else {
			/* Regular command */
			up->bLoopbackMode = FALSE ;
		}

		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

		if ( teljjy_command_sequence[up->iClockCommandSeq+1].command == NULL ) {
			/* Last command of the command sequence */
			iNextClockState = TELJJY_CHANGE_CLOCK_STATE ;
		} else {
			/* More commands to be issued */
			iNextClockState = TELJJY_STAY_CLOCK_STATE ;
		}

	} else {

		iNextClockState = TELJJY_CHANGE_CLOCK_STATE ;

	}

	return iNextClockState ;

}

/******************************/
static int
teljjy_conn_data ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	char	*pBuf ;
	int	iLen, rc ;
	char	sLog [ 80 ] ;
	char	bAdjustment ;


	DEBUG_TELJJY_PRINTF( "teljjy_conn_data" ) ;

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	if ( teljjy_command_sequence[up->iClockCommandSeq].iEchobackReplyLength == iLen
	  && teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyType == TELJJY_REPLY_LOOPBACK
	  && up->sTextBuf[0] == *(teljjy_command_sequence[up->iClockCommandSeq].command)
	  && up->iLoopbackCount < MAX_LOOPBACK ) {
		/* Loopback */

		teljjy_setDelay( peer, up ) ;

		up->iLoopbackCount ++ ;

	} else if ( teljjy_command_sequence[up->iClockCommandSeq].iEchobackReplyLength == iLen
	    && strncmp( pBuf, teljjy_command_sequence[up->iClockCommandSeq].command, iLen ) == 0 ) {
		/* Maybe echoback */

		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_INFORMATION, JJY_CLOCKSTATS_MESSAGE_ECHOBACK ) ;

	} else if ( teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyLength == iLen
		 && teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyType == TELJJY_REPLY_4DATE ) {
		/* 4DATE<CR> -> YYYYMMDD<CR> */

		rc = sscanf ( pBuf, "%4d%2d%2d", &up->year, &up->month, &up->day ) ;

		if ( rc != 3 || up->year < 2000 || 2099 <= up->year
		  || up->month < 1 || 12 < up->month || up->day < 1 || 31 < up->day ) {
			/* Invalid date */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_DATE,
				  rc, up->year, up->month, up->day ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
		}

	} else if ( teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyLength == iLen
		 && teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyType == TELJJY_REPLY_LEAPSEC
	         && ( strncmp( pBuf, " 0", 2 ) == 0 || strncmp( pBuf, "+1", 2 ) == 0 || strncmp( pBuf, "-1", 2 ) == 0 ) ) {
		/* LEAPSEC<CR> -> XX<CR> ( One of <SP>0, +1, -1 ) */

		rc = sscanf ( pBuf, "%2d", &up->leapsecond ) ;

		if ( rc != 1 || up->leapsecond < -1 || 1 < up->leapsecond ) {
			/* Invalid leap second */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_LEAP,
				  pBuf ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
		}

	} else if ( teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyLength == iLen
		 && teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyType == TELJJY_REPLY_TIME ) {
		/* TIME<CR> -> HHMMSS<CR> ( 3 times on second ) */

		rc = sscanf ( pBuf, "%2d%2d%2d", &up->hour, &up->minute, &up->second ) ;

		if ( rc != 3 || up->hour > 23 || up->minute > 59 || up->second > 60 ) {
			/* Invalid time */
			snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_SSCANF_INVALID_TIME,
				  rc, up->hour, up->minute, up->second ) ;
			jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
			up->bLineError = TRUE ;
		}
		up->iTimestamp[up->iTimestampCount] = ( up->hour * 60 + up->minute ) * 60 + up->second ;

		up->iTimestampCount++ ;

		if ( up->iTimestampCount == 6 && ! up->bLineError ) {
#if DEBUG
			printf( "refclock_jjy.c : teljjy_conn_data : bLineError=%d iTimestamp=%d, %d, %d\n",
				up->bLineError,
				up->iTimestamp[3], up->iTimestamp[4], up->iTimestamp[5] ) ;
#endif
			bAdjustment = TRUE ;

			if ( peer->ttl == 100 ) {
				/* mode=100 */
				up->msecond = 0 ;
			} else {
				/* mode=101 to 110 */
				up->msecond = teljjy_getDelay( peer, up ) ;
				if (up->msecond < 0 ) {
					up->msecond = 0 ;
					bAdjustment = FALSE ;
				}
			}

			if ( ( up->iTimestamp[3] - 15 ) <= up->iTimestamp[2]
			  &&   up->iTimestamp[2]        <= up->iTimestamp[3]
			  && ( up->iTimestamp[3] +  1 ) == up->iTimestamp[4]
			  && ( up->iTimestamp[4] +  1 ) == up->iTimestamp[5] ) {
				/* Non over midnight */

				jjy_synctime( peer, pp, up ) ;

				if ( peer->ttl != 100 ) {
					if ( bAdjustment ) {
						snprintf( sLog, sizeof(sLog),
							  JJY_CLOCKSTATS_MESSAGE_DELAY_ADJUST,
							  up->msecond, up->iLoopbackValidCount, MAX_LOOPBACK ) ;
						jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_INFORMATION, sLog ) ;
					} else {
						snprintf( sLog, sizeof(sLog),
							  JJY_CLOCKSTATS_MESSAGE_DELAY_UNADJUST,
							   up->iLoopbackValidCount, MAX_LOOPBACK ) ;
						jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;
					}
				}

			}
		}

	} else if ( teljjy_command_sequence[up->iClockCommandSeq].iEchobackReplyLength != iLen
		 && teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyType == TELJJY_REPLY_LOOPBACK ) {
		/* Loopback noise ( Unexpected replay ) */

		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_IGNORE_REPLY,
			  pBuf ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_WARNING, sLog ) ;

	} else {

		up->bLineError = TRUE ;

		snprintf( sLog, sizeof(sLog)-1, JJY_CLOCKSTATS_MESSAGE_UNEXPECTED_REPLY,
			  pBuf ) ;
		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_ERROR, sLog ) ;

	}

	return TELJJY_STAY_CLOCK_STATE ;

}

/******************************/
static int
teljjy_conn_silent ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	const char *	pCmd ;

	DEBUG_TELJJY_PRINTF( "teljjy_conn_silent" ) ;

	if ( up->iClockCommandSeq >= 1
	  && up->iClockCommandSeq < TELJJY_COMMAND_START_SKIP_LOOPBACK ) {
		/* Loopback */
#ifdef DEBUG
		if ( debug ) {
			printf( "refclock_jjy.c : teljjy_conn_silent : call teljjy_conn_send\n" ) ;
		}
#endif
		if ( teljjy_command_sequence[up->iClockCommandSeq].iExpectedReplyType == TELJJY_REPLY_LOOPBACK ) {
			up->bLoopbackTimeout[up->iLoopbackCount] = TRUE ;
		}
		up->iTeljjySilentTimer = 0 ;
		return teljjy_conn_send( peer, pp, up ) ;
	} else {
		pCmd = "\r" ;
	}

	if ( write( pp->io.fd, pCmd, 1 ) != 1 ) {
		refclock_report( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

	up->iTeljjySilentTimer = 0 ;

	return TELJJY_STAY_CLOCK_STATE ;

}

/******************************/
static int
teljjy_conn_error ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_conn_error" ) ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_bye_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_bye_ignore" ) ;

	return TELJJY_STAY_CLOCK_STATE ;

}

/******************************/
static int
teljjy_bye_disc ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_bye_disc" ) ;

	return TELJJY_CHANGE_CLOCK_STATE ;

}

/******************************/
static int
teljjy_bye_modem ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_TELJJY_PRINTF( "teljjy_bye_modem" ) ;

	modem_disconnect ( peer->refclkunit, peer ) ;

	return TELJJY_STAY_CLOCK_STATE ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    Modem control finite state machine							##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/

/* struct jjyunit.iModemState */

#define	MODEM_STATE_DISCONNECT		0
#define	MODEM_STATE_INITIALIZE		1
#define	MODEM_STATE_DAILING		2
#define	MODEM_STATE_CONNECT		3
#define	MODEM_STATE_ESCAPE		4

/* struct jjyunit.iModemEvent */

#define MODEM_EVENT_NULL		0
#define	MODEM_EVENT_INITIALIZE		1
#define	MODEM_EVENT_DIALOUT		2
#define	MODEM_EVENT_DISCONNECT		3
#define	MODEM_EVENT_RESP_OK		4
#define	MODEM_EVENT_RESP_CONNECT	5
#define	MODEM_EVENT_RESP_RING		6
#define	MODEM_EVENT_RESP_NO_CARRIER	7
#define	MODEM_EVENT_RESP_ERROR		8
#define	MODEM_EVENT_RESP_CONNECT_X	9
#define	MODEM_EVENT_RESP_NO_DAILTONE	10
#define	MODEM_EVENT_RESP_BUSY		11
#define	MODEM_EVENT_RESP_NO_ANSWER	12
#define	MODEM_EVENT_RESP_UNKNOWN	13
#define	MODEM_EVENT_SILENT		14
#define	MODEM_EVENT_TIMEOUT		15

/* Function prototypes */

static	void	modem_control		( struct peer *, struct refclockproc *, struct jjyunit * ) ;

static	int 	modem_disc_ignore	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_disc_init  	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_init_ignore	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_init_start 	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_init_disc  	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_init_resp00	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_init_resp04	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_dial_ignore	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_dial_dialout	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_dial_escape	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_dial_connect	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_dial_disc  	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_conn_ignore	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_conn_escape	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_esc_ignore	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_esc_escape	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_esc_data  	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_esc_silent	( struct peer *, struct refclockproc *, struct jjyunit * ) ;
static	int 	modem_esc_disc  	( struct peer *, struct refclockproc *, struct jjyunit * ) ;

static int ( *pModemHandler [ ] [ 5 ] ) ( struct peer *, struct refclockproc *, struct jjyunit * ) =
{                         	/*STATE_DISCONNECT   STATE_INITIALIZE   STATE_DAILING       STATE_CONNECT      STATE_ESCAPE     */
/* NULL                 */	{ modem_disc_ignore, modem_init_ignore, modem_dial_ignore , modem_conn_ignore, modem_esc_ignore },
/* INITIALIZE           */	{ modem_disc_init  , modem_init_start , modem_dial_ignore , modem_conn_ignore, modem_esc_ignore },
/* DIALOUT              */	{ modem_disc_ignore, modem_init_ignore, modem_dial_dialout, modem_conn_ignore, modem_esc_ignore },
/* DISCONNECT           */	{ modem_disc_ignore, modem_init_disc  , modem_dial_escape , modem_conn_escape, modem_esc_escape },
/* RESP: 0: OK          */	{ modem_disc_ignore, modem_init_resp00, modem_dial_ignore , modem_conn_ignore, modem_esc_data   },
/* RESP: 1: CONNECT     */	{ modem_disc_ignore, modem_init_ignore, modem_dial_connect, modem_conn_ignore, modem_esc_data   },
/* RESP: 2: RING        */	{ modem_disc_ignore, modem_init_ignore, modem_dial_ignore , modem_conn_ignore, modem_esc_data   },
/* RESP: 3: NO CARRIER  */	{ modem_disc_ignore, modem_init_ignore, modem_dial_disc   , modem_conn_ignore, modem_esc_data   },
/* RESP: 4: ERROR       */	{ modem_disc_ignore, modem_init_resp04, modem_dial_disc   , modem_conn_ignore, modem_esc_data   },
/* RESP: 5: CONNECT     */	{ modem_disc_ignore, modem_init_ignore, modem_dial_connect, modem_conn_ignore, modem_esc_data   },
/* RESP: 6: NO DAILTONE */	{ modem_disc_ignore, modem_init_ignore, modem_dial_disc   , modem_conn_ignore, modem_esc_data   },
/* RESP: 7: BUSY        */	{ modem_disc_ignore, modem_init_ignore, modem_dial_disc   , modem_conn_ignore, modem_esc_data   },
/* RESP: 8: NO ANSWER   */	{ modem_disc_ignore, modem_init_ignore, modem_dial_disc   , modem_conn_ignore, modem_esc_data   },
/* RESP: 9: UNKNOWN     */	{ modem_disc_ignore, modem_init_ignore, modem_dial_ignore , modem_conn_ignore, modem_esc_data   },
/* SILENT               */	{ modem_disc_ignore, modem_init_ignore, modem_dial_ignore , modem_conn_ignore, modem_esc_silent },
/* TIMEOUT              */	{ modem_disc_ignore, modem_init_disc  , modem_dial_escape , modem_conn_escape, modem_esc_disc   }
} ;

static short iModemNextState [ ] [ 5 ] =
{                         	/*STATE_DISCONNECT        STATE_INITIALIZE        STATE_DAILING        STATE_CONNECT        STATE_ESCAPE           */
/* NULL                 */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DAILING   , MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* INITIALIZE           */	{ MODEM_STATE_INITIALIZE, MODEM_STATE_INITIALIZE, MODEM_STATE_DAILING   , MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* DIALOUT              */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DAILING   , MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* DISCONNECT           */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_DISCONNECT, MODEM_STATE_ESCAPE    , MODEM_STATE_ESCAPE , MODEM_STATE_ESCAPE     },
/* RESP: 0: OK          */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_DAILING   , MODEM_STATE_DAILING   , MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 1: CONNECT     */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_CONNECT   , MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 2: RING        */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DAILING   , MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 3: NO CARRIER  */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DISCONNECT, MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 4: ERROR       */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_DAILING   , MODEM_STATE_DISCONNECT, MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 5: CONNECT X   */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_CONNECT   , MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 6: NO DAILTONE */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DISCONNECT, MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 7: BUSY        */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DISCONNECT, MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 8: NO ANSWER   */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DISCONNECT, MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* RESP: 9: UNKNOWN     */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DAILING   , MODEM_STATE_CONNECT, MODEM_STATE_ESCAPE     },
/* SILENT               */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_INITIALIZE, MODEM_STATE_DAILING   , MODEM_STATE_CONNECT, MODEM_STATE_DISCONNECT },
/* TIMEOUT              */	{ MODEM_STATE_DISCONNECT, MODEM_STATE_DISCONNECT, MODEM_STATE_ESCAPE    , MODEM_STATE_ESCAPE , MODEM_STATE_DISCONNECT }
} ;

static short iModemPostEvent [ ] [ 5 ] =
{                         	/*STATE_DISCONNECT        STATE_INITIALIZE     STATE_DAILING           STATE_CONNECT           STATE_ESCAPE     */
/* NULL                 */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* INITIALIZE           */	{ MODEM_EVENT_INITIALIZE, MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* DIALOUT              */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* DISCONNECT           */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_DISCONNECT, MODEM_EVENT_DISCONNECT, MODEM_EVENT_NULL },
/* RESP: 0: OK          */	{ MODEM_EVENT_NULL      , MODEM_EVENT_DIALOUT, MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 1: CONNECT     */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 2: RING        */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 3: NO CARRIER  */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 4: ERROR       */	{ MODEM_EVENT_NULL      , MODEM_EVENT_DIALOUT, MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 5: CONNECT X   */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 6: NO DAILTONE */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 7: BUSY        */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 8: NO ANSWER   */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* RESP: 9: UNKNOWN     */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* SILENT               */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_NULL      , MODEM_EVENT_NULL      , MODEM_EVENT_NULL },
/* TIMEOUT              */	{ MODEM_EVENT_NULL      , MODEM_EVENT_NULL   , MODEM_EVENT_DISCONNECT, MODEM_EVENT_DISCONNECT, MODEM_EVENT_NULL }
} ;

static short iModemSilentTimeout [ 5 ] = { 0,  0,  0, 0,  5 } ;
static short iModemStateTimeout  [ 5 ] = { 0, 20, 90, 0, 20 } ;

#define	STAY_MODEM_STATE	0
#define	CHANGE_MODEM_STATE	1

#ifdef	DEBUG
#define	DEBUG_MODEM_PRINTF(sFunc)	{ if ( debug ) { printf ( "refclock_jjy.c : %s : iModemState=%d iModemEvent=%d iModemSilentTimer=%d iModemStateTimer=%d\n", sFunc, up->iModemState, up->iModemEvent, up->iModemSilentTimer, up->iModemStateTimer ) ; } }
#else
#define	DEBUG_MODEM_PRINTF(sFunc)
#endif

/**************************************************************************************************/

static short
getModemState ( struct jjyunit *up )
{
	return up->iModemState ;
}

/**************************************************************************************************/

static int
isModemStateConnect ( short iCheckState )
{
	return ( iCheckState == MODEM_STATE_CONNECT ) ;
}

/**************************************************************************************************/

static int
isModemStateDisconnect ( short iCheckState )
{
	return ( iCheckState == MODEM_STATE_DISCONNECT ) ;
}

/**************************************************************************************************/

static int
isModemStateTimerOn ( struct jjyunit *up )
{
	return ( iModemSilentTimeout[up->iModemState] != 0 || iModemStateTimeout[up->iModemState] != 0 ) ;
}

/**************************************************************************************************/

static void
modem_connect ( int unit, struct peer *peer )
{
	struct	refclockproc	*pp;
	struct	jjyunit 	*up;

	pp = peer->procptr ;
	up = pp->unitptr ;

	DEBUG_MODEM_PRINTF( "modem_connect" ) ;

	up->iModemEvent = MODEM_EVENT_INITIALIZE ;

	modem_control ( peer, pp, up ) ;

}

/**************************************************************************************************/

static void
modem_disconnect ( int unit, struct peer *peer )
{
	struct	refclockproc	*pp;
	struct	jjyunit 	*up;

	pp = peer->procptr ;
	up = pp->unitptr ;

	DEBUG_MODEM_PRINTF( "modem_disconnect" ) ;

	up->iModemEvent = MODEM_EVENT_DISCONNECT ;

	modem_control ( peer, pp, up ) ;

}

/**************************************************************************************************/

static int
modem_receive ( struct recvbuf *rbufp )
{

	struct	peer		*peer;
	struct	jjyunit		*up;
	struct	refclockproc	*pp;
	char	*pBuf ;
	size_t	iLen ;

#ifdef DEBUG
	static const char *sFunctionName = "modem_receive" ;
#endif

	peer = rbufp->recv_peer ;
	pp = peer->procptr ;
	up = pp->unitptr ;

	DEBUG_MODEM_PRINTF( sFunctionName ) ;

	if ( up->linediscipline == LDISC_RAW ) {
		pBuf = up->sTextBuf ;
		iLen = up->iTextBufLen ;
	} else {
		pBuf = pp->a_lastcode ;
		iLen = pp->lencode ;
	}

	if      ( iLen ==  2 && strncmp( pBuf, "OK"         ,  2 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_OK          ; }
	else if ( iLen ==  7 && strncmp( pBuf, "CONNECT"    ,  7 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_CONNECT     ; }
	else if ( iLen ==  4 && strncmp( pBuf, "RING"       ,  4 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_RING        ; }
	else if ( iLen == 10 && strncmp( pBuf, "NO CARRIER" , 10 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_NO_CARRIER  ; }
	else if ( iLen ==  5 && strncmp( pBuf, "ERROR"      ,  5 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_ERROR       ; }
	else if ( iLen >=  8 && strncmp( pBuf, "CONNECT "   ,  8 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_CONNECT_X   ; }
	else if ( iLen == 11 && strncmp( pBuf, "NO DAILTONE", 11 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_NO_DAILTONE ; }
	else if ( iLen ==  4 && strncmp( pBuf, "BUSY"       ,  4 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_BUSY        ; }
	else if ( iLen ==  9 && strncmp( pBuf, "NO ANSWER"  ,  9 ) == 0 ) { up->iModemEvent = MODEM_EVENT_RESP_NO_ANSWER   ; }
	else                                                              { up->iModemEvent = MODEM_EVENT_RESP_UNKNOWN     ; }

#ifdef DEBUG
	if ( debug ) {
		char	sResp [ 40 ] ;
		size_t	iCopyLen ;
		iCopyLen = ( iLen <= sizeof(sResp)-1 ? iLen : sizeof(sResp)-1 ) ;
		strncpy( sResp, pBuf, iLen <= sizeof(sResp)-1 ? iLen : sizeof(sResp)-1 ) ;
		sResp[iCopyLen] = 0 ;
		printf ( "refclock_jjy.c : modem_receive : iLen=%zu pBuf=[%s] iModemEvent=%d\n", iCopyLen, sResp, up->iModemEvent ) ;
	}
#endif
	modem_control ( peer, pp, up ) ;

	return 0 ;

}

/**************************************************************************************************/

static void
modem_timer ( int unit, struct peer *peer )
{

	struct	refclockproc *pp ;
	struct	jjyunit      *up ;

	pp = peer->procptr ;
	up = pp->unitptr ;

	DEBUG_MODEM_PRINTF( "modem_timer" ) ;

	if ( iModemSilentTimeout[up->iModemState] != 0 ) {
		up->iModemSilentTimer++ ;
		if ( iModemSilentTimeout[up->iModemState] <= up->iModemSilentTimer ) {
			up->iModemEvent = MODEM_EVENT_SILENT ;
			modem_control ( peer, pp, up ) ;
		}
	}

	if ( iModemStateTimeout[up->iModemState] != 0 ) {
		up->iModemStateTimer++ ;
		if ( iModemStateTimeout[up->iModemState] <= up->iModemStateTimer ) {
			up->iModemEvent = MODEM_EVENT_TIMEOUT ;
			modem_control ( peer, pp, up ) ;
		}
	}

}

/**************************************************************************************************/

static void
modem_control ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	int	rc ;
	short	iPostEvent = MODEM_EVENT_NULL ;

	DEBUG_MODEM_PRINTF( "modem_control" ) ;

	rc = (*pModemHandler[up->iModemEvent][up->iModemState])( peer, pp, up ) ;

	if ( rc == CHANGE_MODEM_STATE ) {
		iPostEvent = iModemPostEvent[up->iModemEvent][up->iModemState] ;
#ifdef DEBUG
		if ( debug ) {
			printf( "refclock_jjy.c : modem_control : iModemState=%d -> %d  iPostEvent=%d\n",
				 up->iModemState, iModemNextState[up->iModemEvent][up->iModemState], iPostEvent ) ;
		}
#endif

		if ( up->iModemState != iModemNextState[up->iModemEvent][up->iModemState] ) {
			up->iModemSilentCount = 0 ;
			up->iModemStateTimer = 0 ;
			up->iModemCommandSeq = 0 ;
		}

		up->iModemState = iModemNextState[up->iModemEvent][up->iModemState] ;
	}

	if ( iPostEvent != MODEM_EVENT_NULL ) {
		up->iModemEvent = iPostEvent ;
		modem_control ( peer, pp, up ) ;
	}

	up->iModemEvent = MODEM_EVENT_NULL ;

}

/******************************/
static int
modem_disc_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_disc_ignore" ) ;

	return STAY_MODEM_STATE ;

}

/******************************/
static int
modem_disc_init ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_disc_init" ) ;

	return CHANGE_MODEM_STATE ;

}

/******************************/
static int
modem_init_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_init_ignore" ) ;

	return STAY_MODEM_STATE ;

}

/******************************/
static int
modem_init_start ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_init_start" ) ;

	up->iModemCommandSeq = 0 ;

#ifdef DEBUG
	if ( debug ) {
		printf( "refclock_jjy.c : modem_init_start : call modem_init_resp00\n" ) ;
	}
#endif

	return modem_init_resp00( peer, pp, up ) ;

}

/******************************/
static int
modem_init_resp00 ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	const char *	pCmd ;
	char		cBuf [ 46 ] ;
	int		iCmdLen ;
	int		iErrorCorrection, iSpeakerSwitch, iSpeakerVolume ;
	int		iNextModemState = STAY_MODEM_STATE ;

	DEBUG_MODEM_PRINTF( "modem_init_resp00" ) ;

	up->iModemCommandSeq++ ;

	switch ( up->iModemCommandSeq ) {

	case 1 :
		/* En = Echoback      0:Off      1:On   */
		/* Qn = Result codes  0:On       1:Off  */
		/* Vn = Result codes  0:Numeric  1:Text */
		pCmd = "ATE0Q0V1\r\n" ;
		break ;

	case 2 :
		/* Mn = Speaker switch  0:Off  1:On until remote carrier detected  2:On */
		if ( ( pp->sloppyclockflag & CLK_FLAG3 ) == 0 ) {
			/* fudge 127.127.40.n flag3 0 */
			iSpeakerSwitch = 0 ;
		} else {
			/* fudge 127.127.40.n flag3 1 */
			iSpeakerSwitch = 2 ;
		}

		/* Ln = Speaker volume  0:Very low  1:Low  2:Middle  3:High */
		if ( ( pp->sloppyclockflag & CLK_FLAG4 ) == 0 ) {
			/* fudge 127.127.40.n flag4 0 */
			iSpeakerVolume = 1 ;
		} else {
			/* fudge 127.127.40.n flag4 1 */
			iSpeakerVolume = 2 ;
		}

		pCmd = cBuf ;
		snprintf( cBuf, sizeof(cBuf), "ATM%dL%d\r\n", iSpeakerSwitch, iSpeakerVolume ) ;
		break ;

	case 3 :
		/* &Kn = Flow control  4:XON/XOFF */
		pCmd = "AT&K4\r\n" ;
		break ;

	case 4 :
		/* +MS = Protocol  V22B:1200,2400bpsiV.22bis) */
		pCmd = "AT+MS=V22B\r\n" ;
		break ;

	case 5 :
		/* %Cn = Data compression  0:No data compression */
		pCmd = "AT%C0\r\n" ;
		break ;

	case 6 :
		/* \Nn = Error correction  0:Normal mode  1:Direct mode  2:V42,MNP  3:V42,MNP,Normal */
		if ( ( pp->sloppyclockflag & CLK_FLAG2 ) == 0 ) {
			/* fudge 127.127.40.n flag2 0 */
			iErrorCorrection = 0 ;
		} else {
			/* fudge 127.127.40.n flag2 1 */
			iErrorCorrection = 3 ;
		}

		pCmd = cBuf ;
		snprintf( cBuf, sizeof(cBuf), "AT\\N%d\r\n", iErrorCorrection ) ;
		break ;

	case 7 :
		/* Hn = Hook  0:Hook-On ( Disconnect )  1:Hook-Off ( Connect ) */
		pCmd = "ATH1\r\n" ;
		break ;

	case 8 :
		/* Initialize completion */
		pCmd = NULL ;
		iNextModemState = CHANGE_MODEM_STATE ;
		break ;

	default :
		pCmd = NULL ;
		break ;

	}

	if ( pCmd != NULL ) {

		iCmdLen = strlen( pCmd ) ;
		if ( write( pp->io.fd, pCmd, iCmdLen ) != iCmdLen ) {
			refclock_report( peer, CEVNT_FAULT ) ;
		}

		jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

	}

	return iNextModemState ;

}

/******************************/
static int
modem_init_resp04 ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_init_resp04" ) ;

	return modem_init_resp00( peer, pp, up ) ;

}

/******************************/
static int
modem_init_disc ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_init_disc" ) ;
#ifdef DEBUG
	if ( debug ) {
		printf( "refclock_jjy.c : modem_init_disc : call modem_esc_disc\n" ) ;
	}
#endif

	return CHANGE_MODEM_STATE ;

}

/******************************/
static int
modem_dial_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_dial_ignore" ) ;

	return STAY_MODEM_STATE ;

}

/******************************/
static int
modem_dial_dialout ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	char	sCmd [ 46 ] ;
	int	iCmdLen ;
	char	cToneOrPulse ;

	DEBUG_MODEM_PRINTF( "modem_dial_dialout" ) ;

	/* Tone or Pulse */
	if ( ( pp->sloppyclockflag & CLK_FLAG1 ) == 0 ) {
		/* fudge 127.127.40.n flag1 0 */
		cToneOrPulse = 'T' ;
	} else {
		/* fudge 127.127.40.n flag1 1 */
		cToneOrPulse = 'P' ;
	}

	/* Connect ( Dial number ) */
	snprintf( sCmd, sizeof(sCmd), "ATDW%c%s\r\n", cToneOrPulse, *sys_phone ) ;

	/* Send command */
	iCmdLen = strlen( sCmd ) ;
	if ( write( pp->io.fd, sCmd, iCmdLen ) != iCmdLen ) {
		refclock_report( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, sCmd ) ;

	return STAY_MODEM_STATE ;

}

/******************************/
static int
modem_dial_escape ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_dial_escape" ) ;
#ifdef DEBUG
	if ( debug ) {
		printf( "refclock_jjy.c : modem_dial_escape : call modem_conn_escape\n" ) ;
	}
#endif

	return modem_conn_escape( peer, pp, up ) ;

}

/******************************/
static int
modem_dial_connect ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_dial_connect" ) ;

	return CHANGE_MODEM_STATE ;

}

/******************************/
static int
modem_dial_disc ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_dial_disc" ) ;
#ifdef DEBUG
	if ( debug ) {
		printf( "refclock_jjy.c : modem_dial_disc : call modem_esc_disc\n" ) ;
	}
#endif

	modem_esc_disc( peer, pp, up ) ;

	return CHANGE_MODEM_STATE ;

}

/******************************/
static int
modem_conn_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_conn_ignore" ) ;

	return STAY_MODEM_STATE ;

}

/******************************/
static int
modem_conn_escape ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_conn_escape" ) ;

	return CHANGE_MODEM_STATE ;

}

/******************************/
static int
modem_esc_ignore ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_esc_ignore" ) ;

	return STAY_MODEM_STATE ;

}

/******************************/
static int
modem_esc_escape ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	const char *	pCmd ;
	int		iCmdLen ;

	DEBUG_MODEM_PRINTF( "modem_esc_escape" ) ;

	/* Escape command ( Go to command mode ) */
	pCmd = "+++" ;

	/* Send command */
	iCmdLen = strlen( pCmd ) ;
	if ( write( pp->io.fd, pCmd, iCmdLen ) != iCmdLen ) {
		refclock_report( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

	return STAY_MODEM_STATE ;

}

/******************************/
static int
modem_esc_data ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_esc_data" ) ;

	up->iModemSilentTimer = 0 ;

	return STAY_MODEM_STATE ;

}

/******************************/
static int
modem_esc_silent ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	DEBUG_MODEM_PRINTF( "modem_esc_silent" ) ;

	up->iModemSilentCount ++ ;

	if ( up->iModemSilentCount < iModemStateTimeout[up->iModemState] / iModemSilentTimeout[up->iModemState] ) {
#ifdef DEBUG
		if ( debug ) {
			printf( "refclock_jjy.c : modem_esc_silent : call modem_esc_escape\n" ) ;
		}
#endif
		modem_esc_escape( peer, pp, up ) ;
		up->iModemSilentTimer = 0 ;
		return STAY_MODEM_STATE ;
	}

#ifdef DEBUG
	if ( debug ) {
		printf( "refclock_jjy.c : modem_esc_silent : call modem_esc_disc\n" ) ;
	}
#endif
	return modem_esc_disc( peer, pp, up ) ;

}
/******************************/
static int
modem_esc_disc ( struct peer *peer, struct refclockproc *pp, struct jjyunit *up )
{

	const char *	pCmd ;
	int		iCmdLen ;

	DEBUG_MODEM_PRINTF( "modem_esc_disc" ) ;

	/* Disconnect */
	pCmd = "ATH0\r\n" ;

	/* Send command */
	iCmdLen = strlen( pCmd ) ;
	if ( write( pp->io.fd, pCmd, iCmdLen ) != iCmdLen ) {
		refclock_report( peer, CEVNT_FAULT ) ;
	}

	jjy_write_clockstats( peer, JJY_CLOCKSTATS_MARK_SEND, pCmd ) ;

	return CHANGE_MODEM_STATE ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    jjy_write_clockstats									##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/

static void
jjy_write_clockstats ( struct peer *peer, int iMark, const char *pData )
{

	char		sLog [ 100 ] ;
	const char *	pMark ;
	int 		iMarkLen, iDataLen ;

	switch ( iMark ) {
	case JJY_CLOCKSTATS_MARK_JJY :
		pMark = "JJY " ;
		break ;
	case JJY_CLOCKSTATS_MARK_SEND :
		pMark = "--> " ;
		break ;
	case JJY_CLOCKSTATS_MARK_RECEIVE :
		pMark = "<-- " ;
		break ;
	case JJY_CLOCKSTATS_MARK_INFORMATION :
		pMark = "--- " ;
		break ;
	case JJY_CLOCKSTATS_MARK_ATTENTION :
		pMark = "=== " ;
		break ;
	case JJY_CLOCKSTATS_MARK_WARNING :
		pMark = "-W- " ;
		break ;
	case JJY_CLOCKSTATS_MARK_ERROR :
		pMark = "-X- " ;
		break ;
	case JJY_CLOCKSTATS_MARK_BUG :
		pMark = "!!! " ;
		break ;
	default :
		pMark = "" ;
		break ;
	}

	iDataLen = strlen( pData ) ;
	iMarkLen = strlen( pMark ) ;
	strcpy( sLog, pMark ) ; /* Harmless because of enough length */
	printableString( sLog+iMarkLen, sizeof(sLog)-iMarkLen, pData, iDataLen ) ;

#ifdef DEBUG
	if ( debug ) {
		printf( "refclock_jjy.c : clockstats : %s\n", sLog ) ;
	}
#endif
	record_clock_stats( &peer->srcadr, sLog ) ;

}

/*################################################################################################*/
/*################################################################################################*/
/*##												##*/
/*##    printableString										##*/
/*##												##*/
/*################################################################################################*/
/*################################################################################################*/

static void
printableString ( char *sOutput, int iOutputLen, const char *sInput, int iInputLen )
{
	const char	*printableControlChar[] = {
			"<NUL>", "<SOH>", "<STX>", "<ETX>",
			"<EOT>", "<ENQ>", "<ACK>", "<BEL>",
			"<BS>" , "<HT>" , "<LF>" , "<VT>" ,
			"<FF>" , "<CR>" , "<SO>" , "<SI>" ,
			"<DLE>", "<DC1>", "<DC2>", "<DC3>",
			"<DC4>", "<NAK>", "<SYN>", "<ETB>",
			"<CAN>", "<EM>" , "<SUB>", "<ESC>",
			"<FS>" , "<GS>" , "<RS>" , "<US>" ,
			" " } ;

	size_t	i, j, n ;
	size_t	InputLen;
	size_t	OutputLen;

	InputLen = (size_t)iInputLen;
	OutputLen = (size_t)iOutputLen;
	for ( i = j = 0 ; i < InputLen && j < OutputLen ; i ++ ) {
		if ( isprint( (unsigned char)sInput[i] ) ) {
			n = 1 ;
			if ( j + 1 >= OutputLen )
				break ;
			sOutput[j] = sInput[i] ;
		} else if ( ( sInput[i] & 0xFF ) < 
			    COUNTOF(printableControlChar) ) {
			n = strlen( printableControlChar[sInput[i] & 0xFF] ) ;
			if ( j + n + 1 >= OutputLen )
				break ;
			strlcpy( sOutput + j,
				 printableControlChar[sInput[i] & 0xFF],
				 OutputLen - j ) ;
		} else {
			n = 5 ;
			if ( j + n + 1 >= OutputLen )
				break ;
			snprintf( sOutput + j, OutputLen - j, "<x%X>",
				  sInput[i] & 0xFF ) ;
		}
		j += n ;
	}

	sOutput[min(j, (size_t)iOutputLen - 1)] = '\0' ;

}

/**************************************************************************************************/

#else
int refclock_jjy_bs ;
#endif /* REFCLOCK */
