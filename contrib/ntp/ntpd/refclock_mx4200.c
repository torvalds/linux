/*
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66.
 *
 * Copyright (c) 1992 The Regents of the University of California.
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
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 * 4. The name of the University may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Modified: Marc Brett <marc.brett@westgeo.com>   Sept, 1999.
 *
 * 1. Added support for alternate PPS schemes, with code mostly
 *    copied from the Oncore driver (Thanks, Poul-Henning Kamp).
 *    This code runs on SunOS 4.1.3 with ppsclock-1.6a1 and Solaris 7.
 */


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_MX4200) && defined(HAVE_PPSAPI)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

#include "mx4200.h"

#ifdef HAVE_SYS_TERMIOS_H
# include <sys/termios.h>
#endif
#ifdef HAVE_SYS_PPSCLOCK_H
# include <sys/ppsclock.h>
#endif

#ifndef HAVE_STRUCT_PPSCLOCKEV
struct ppsclockev {
# ifdef HAVE_STRUCT_TIMESPEC
	struct timespec tv;
# else
	struct timeval tv;
# endif
	u_int serial;
};
#endif /* ! HAVE_STRUCT_PPSCLOCKEV */

#ifdef HAVE_PPSAPI
# include "ppsapi_timepps.h"
#endif /* HAVE_PPSAPI */

/*
 * This driver supports the Magnavox Model MX 4200 GPS Receiver
 * adapted to precision timing applications.  It requires the
 * ppsclock line discipline or streams module described in the
 * Line Disciplines and Streams Drivers page. It also requires a
 * gadget box and 1-PPS level converter, such as described in the
 * Pulse-per-second (PPS) Signal Interfacing page.
 *
 * It's likely that other compatible Magnavox receivers such as the
 * MX 4200D, MX 9212, MX 9012R, MX 9112 will be supported by this code.
 */

/*
 * Check this every time you edit the code!
 */
#define YEAR_LAST_MODIFIED 2000

/*
 * GPS Definitions
 */
#define	DEVICE		"/dev/gps%d"	/* device name and unit */
#define	SPEED232	B4800		/* baud */

/*
 * Radio interface parameters
 */
#define	PRECISION	(-18)	/* precision assumed (about 4 us) */
#define	REFID	"GPS\0"		/* reference id */
#define	DESCRIPTION	"Magnavox MX4200 GPS Receiver" /* who we are */
#define	DEFFUDGETIME	0	/* default fudge time (ms) */

#define	SLEEPTIME	32	/* seconds to wait for reconfig to complete */

/*
 * Position Averaging.
 */
#define INTERVAL	1	/* Interval between position measurements (s) */
#define AVGING_TIME	24	/* Number of hours to average */
#define NOT_INITIALIZED	-9999.	/* initial pivot longitude */

/*
 * MX4200 unit control structure.
 */
struct mx4200unit {
	u_int  pollcnt;			/* poll message counter */
	u_int  polled;			/* Hand in a time sample? */
	u_int  lastserial;		/* last pps serial number */
	struct ppsclockev ppsev;	/* PPS control structure */
	double avg_lat;			/* average latitude */
	double avg_lon;			/* average longitude */
	double avg_alt;			/* average height */
	double central_meridian;	/* central meridian */
	double N_fixes;			/* Number of position measurements */
	int    last_leap;		/* leap second warning */
	u_int  moving;			/* mobile platform? */
	u_long sloppyclockflag;		/* fudge flags */
	u_int  known;			/* position known yet? */
	u_long clamp_time;		/* when to stop postion averaging */
	u_long log_time;		/* when to print receiver status */
	pps_handle_t	pps_h;
	pps_params_t	pps_p;
	pps_info_t	pps_i;
};

static char pmvxg[] = "PMVXG";

/* XXX should be somewhere else */
#ifdef __GNUC__
#if __GNUC__ < 2  || (__GNUC__ == 2 && __GNUC_MINOR__ < 5)
#ifndef __attribute__
#define __attribute__(args)
#endif /* __attribute__ */
#endif /* __GNUC__ < 2  || (__GNUC__ == 2 && __GNUC_MINOR__ < 5) */
#else
#ifndef __attribute__
#define __attribute__(args)
#endif /* __attribute__ */
#endif /* __GNUC__ */
/* XXX end */

/*
 * Function prototypes
 */
static	int	mx4200_start	(int, struct peer *);
static	void	mx4200_shutdown	(int, struct peer *);
static	void	mx4200_receive	(struct recvbuf *);
static	void	mx4200_poll	(int, struct peer *);

static	char *	mx4200_parse_t	(struct peer *);
static	char *	mx4200_parse_p	(struct peer *);
static	char *	mx4200_parse_s	(struct peer *);
int	mx4200_cmpl_fp	(const void *, const void *);
static	int	mx4200_config	(struct peer *);
static	void	mx4200_ref	(struct peer *);
static	void	mx4200_send	(struct peer *, char *, ...)
    __attribute__ ((format (printf, 2, 3)));
static	u_char	mx4200_cksum	(char *, int);
static	int	mx4200_jday	(int, int, int);
static	void	mx4200_debug	(struct peer *, char *, ...)
    __attribute__ ((format (printf, 2, 3)));
static	int	mx4200_pps	(struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_mx4200 = {
	mx4200_start,		/* start up driver */
	mx4200_shutdown,	/* shut down driver */
	mx4200_poll,		/* transmit poll message */
	noentry,		/* not used (old mx4200_control) */
	noentry,		/* initialize driver (not used) */
	noentry,		/* not used (old mx4200_buginfo) */
	NOFLAGS			/* not used */
};



/*
 * mx4200_start - open the devices and initialize data for processing
 */
static int
mx4200_start(
	int unit,
	struct peer *peer
	)
{
	register struct mx4200unit *up;
	struct refclockproc *pp;
	int fd;
	char gpsdev[20];

	/*
	 * Open serial port
	 */
	snprintf(gpsdev, sizeof(gpsdev), DEVICE, unit);
	fd = refclock_open(gpsdev, SPEED232, LDISC_PPS);
	if (fd <= 0)
		return 0;

	/*
	 * Allocate unit structure
	 */
	up = emalloc_zero(sizeof(*up));
	pp = peer->procptr;
	pp->io.clock_recv = mx4200_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	if (!io_addclock(&pp->io)) {
		close(fd);
		pp->io.fd = -1;
		free(up);
		return (0);
	}
	pp->unitptr = up;

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy((char *)&pp->refid, REFID, 4);

	/* Ensure the receiver is properly configured */
	return mx4200_config(peer);
}


/*
 * mx4200_shutdown - shut down the clock
 */
static void
mx4200_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct mx4200unit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	if (NULL != up)
		free(up);
}


/*
 * mx4200_config - Configure the receiver
 */
static int
mx4200_config(
	struct peer *peer
	)
{
	char tr_mode;
	int add_mode;
	register struct mx4200unit *up;
	struct refclockproc *pp;
	int mode;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Initialize the unit variables
	 *
	 * STRANGE BEHAVIOUR WARNING: The fudge flags are not available
	 * at the time mx4200_start is called.  These are set later,
	 * and so the code must be prepared to handle changing flags.
	 */
	up->sloppyclockflag = pp->sloppyclockflag;
	if (pp->sloppyclockflag & CLK_FLAG2) {
		up->moving   = 1;	/* Receiver on mobile platform */
		msyslog(LOG_DEBUG, "mx4200_config: mobile platform");
	} else {
		up->moving   = 0;	/* Static Installation */
	}
	up->pollcnt     	= 2;
	up->polled      	= 0;
	up->known       	= 0;
	up->avg_lat     	= 0.0;
	up->avg_lon     	= 0.0;
	up->avg_alt     	= 0.0;
	up->central_meridian	= NOT_INITIALIZED;
	up->N_fixes    		= 0.0;
	up->last_leap   	= 0;	/* LEAP_NOWARNING */
	up->clamp_time  	= current_time + (AVGING_TIME * 60 * 60);
	up->log_time    	= current_time + SLEEPTIME;

	if (time_pps_create(pp->io.fd, &up->pps_h) < 0) {
		perror("time_pps_create");
		msyslog(LOG_ERR,
			"mx4200_config: time_pps_create failed: %m");
		return (0);
	}
	if (time_pps_getcap(up->pps_h, &mode) < 0) {
		msyslog(LOG_ERR,
			"mx4200_config: time_pps_getcap failed: %m");
		return (0);
	}

	if (time_pps_getparams(up->pps_h, &up->pps_p) < 0) {
		msyslog(LOG_ERR,
			"mx4200_config: time_pps_getparams failed: %m");
		return (0);
	}

	/* nb. only turn things on, if someone else has turned something
	 *      on before we get here, leave it alone!
	 */

	up->pps_p.mode = PPS_CAPTUREASSERT | PPS_TSFMT_TSPEC;
	up->pps_p.mode &= mode;		/* only set what is legal */

	if (time_pps_setparams(up->pps_h, &up->pps_p) < 0) {
		perror("time_pps_setparams");
		msyslog(LOG_ERR,
			"mx4200_config: time_pps_setparams failed: %m");
		exit(1);
	}

	if (time_pps_kcbind(up->pps_h, PPS_KC_HARDPPS, PPS_CAPTUREASSERT,
			PPS_TSFMT_TSPEC) < 0) {
		perror("time_pps_kcbind");
		msyslog(LOG_ERR,
			"mx4200_config: time_pps_kcbind failed: %m");
		exit(1);
	}


	/*
	 * "007" Control Port Configuration
	 * Zero the output list (do it twice to flush possible junk)
	 */
	mx4200_send(peer, "%s,%03d,,%d,,,,,,", pmvxg,
	    PMVXG_S_PORTCONF,
	    /* control port output block Label */
	    1);		/* clear current output control list (1=yes) */
	/* add/delete sentences from list */
	/* must be null */
	/* sentence output rate (sec) */
	/* precision for position output */
	/* nmea version for cga & gll output */
	/* pass-through control */
	mx4200_send(peer, "%s,%03d,,%d,,,,,,", pmvxg,
	    PMVXG_S_PORTCONF, 1);

	/*
	 * Request software configuration so we can syslog the firmware version
	 */
	mx4200_send(peer, "%s,%03d", "CDGPQ", PMVXG_D_SOFTCONF);

	/*
	 * "001" Initialization/Mode Control, Part A
	 * Where ARE we?
	 */
	mx4200_send(peer, "%s,%03d,,,,,,,,,,", pmvxg,
	    PMVXG_S_INITMODEA);
	/* day of month */
	/* month of year */
	/* year */
	/* gmt */
	/* latitude   DDMM.MMMM */
	/* north/south */
	/* longitude DDDMM.MMMM */
	/* east/west */
	/* height */
	/* Altitude Reference 1=MSL */

	/*
	 * "001" Initialization/Mode Control, Part B
	 * Start off in 2d/3d coast mode, holding altitude to last known
	 * value if only 3 satellites available.
	 */
	mx4200_send(peer, "%s,%03d,%d,,%.1f,%.1f,%d,%d,%d,%c,%d",
	    pmvxg, PMVXG_S_INITMODEB,
	    3,		/* 2d/3d coast */
	    /* reserved */
	    0.1,	/* hor accel fact as per Steve (m/s**2) */
	    0.1,	/* ver accel fact as per Steve (m/s**2) */
	    10,		/* vdop */
	    10,		/* hdop limit as per Steve */
	    5,		/* elevation limit as per Steve (deg) */
	    'U',	/* time output mode (UTC) */
	    0);		/* local time offset from gmt (HHHMM) */

	/*
	 * "023" Time Recovery Configuration
	 * Get UTC time from a stationary receiver.
	 * (Set field 1 'D' == dynamic if we are on a moving platform).
	 * (Set field 1 'S' == static  if we are not moving).
	 * (Set field 1 'K' == known position if we can initialize lat/lon/alt).
	 */

	if (pp->sloppyclockflag & CLK_FLAG2)
		up->moving   = 1;	/* Receiver on mobile platform */
	else
		up->moving   = 0;	/* Static Installation */

	up->pollcnt  = 2;
	if (up->moving) {
		/* dynamic: solve for pos, alt, time, while moving */
		tr_mode = 'D';
	} else {
		/* static: solve for pos, alt, time, while stationary */
		tr_mode = 'S';
	}
	mx4200_send(peer, "%s,%03d,%c,%c,%c,%d,%d,%d,", pmvxg,
	    PMVXG_S_TRECOVCONF,
	    tr_mode,	/* time recovery mode (see above ) */
	    'U',	/* synchronize to UTC */
	    'A',	/* always output a time pulse */
	    500,	/* max time error in ns */
	    0,		/* user bias in ns */
	    1);		/* output "830" sentences to control port */
			/* Multi-satellite mode */

	/*
	 * Output position information (to calculate fixed installation
	 * location) only if we are not moving
	 */
	if (up->moving) {
		add_mode = 2;	/* delete from list */
	} else {
		add_mode = 1;	/* add to list */
	}


	/*
	 * "007" Control Port Configuration
	 * Output "021" position, height, velocity reports
	 */
	mx4200_send(peer, "%s,%03d,%03d,%d,%d,,%d,,,", pmvxg,
	    PMVXG_S_PORTCONF,
	    PMVXG_D_PHV, /* control port output block Label */
	    0,		/* clear current output control list (0=no) */
	    add_mode,	/* add/delete sentences from list (1=add, 2=del) */
	    		/* must be null */
	    INTERVAL);	/* sentence output rate (sec) */
			/* precision for position output */
			/* nmea version for cga & gll output */
			/* pass-through control */

	return (1);
}

/*
 * mx4200_ref - Reconfigure unit as a reference station at a known position.
 */
static void
mx4200_ref(
	struct peer *peer
	)
{
	register struct mx4200unit *up;
	struct refclockproc *pp;
	double minute, lat, lon, alt;
	char lats[16], lons[16];
	char nsc, ewc;

	pp = peer->procptr;
	up = pp->unitptr;

	/* Should never happen! */
	if (up->moving) return;

	/*
	 * Set up to output status information in the near future
	 */
	up->log_time    = current_time + SLEEPTIME;

	/*
	 * "007" Control Port Configuration
	 * Stop outputting "021" position, height, velocity reports
	 */
	mx4200_send(peer, "%s,%03d,%03d,%d,%d,,,,,", pmvxg,
	    PMVXG_S_PORTCONF,
	    PMVXG_D_PHV, /* control port output block Label */
	    0,		/* clear current output control list (0=no) */
	    2);		/* add/delete sentences from list (2=delete) */
			/* must be null */
	    		/* sentence output rate (sec) */
			/* precision for position output */
			/* nmea version for cga & gll output */
			/* pass-through control */

	/*
	 * "001" Initialization/Mode Control, Part B
	 * Put receiver in fully-constrained 2d nav mode
	 */
	mx4200_send(peer, "%s,%03d,%d,,%.1f,%.1f,%d,%d,%d,%c,%d",
	    pmvxg, PMVXG_S_INITMODEB,
	    2,		/* 2d nav */
	    /* reserved */
	    0.1,	/* hor accel fact as per Steve (m/s**2) */
	    0.1,	/* ver accel fact as per Steve (m/s**2) */
	    10,		/* vdop */
	    10,		/* hdop limit as per Steve */
	    5,		/* elevation limit as per Steve (deg) */
	    'U',	/* time output mode (UTC) */
	    0);		/* local time offset from gmt (HHHMM) */

	/*
	 * "023" Time Recovery Configuration
	 * Get UTC time from a stationary receiver.  Solve for time only.
	 * This should improve the time resolution dramatically.
	 */
	mx4200_send(peer, "%s,%03d,%c,%c,%c,%d,%d,%d,", pmvxg,
	    PMVXG_S_TRECOVCONF,
	    'K',	/* known position: solve for time only */
	    'U',	/* synchronize to UTC */
	    'A',	/* always output a time pulse */
	    500,	/* max time error in ns */
	    0,		/* user bias in ns */
	    1);		/* output "830" sentences to control port */
	/* Multi-satellite mode */

	/*
	 * "000" Initialization/Mode Control - Part A
	 * Fix to our averaged position.
	 */
	if (up->central_meridian != NOT_INITIALIZED) {
		up->avg_lon += up->central_meridian;
		if (up->avg_lon < -180.0) up->avg_lon += 360.0;
		if (up->avg_lon >  180.0) up->avg_lon -= 360.0;
	}

	if (up->avg_lat >= 0.0) {
		lat = up->avg_lat;
		nsc = 'N';
	} else {
		lat = up->avg_lat * (-1.0);
		nsc = 'S';
	}
	if (up->avg_lon >= 0.0) {
		lon = up->avg_lon;
		ewc = 'E';
	} else {
		lon = up->avg_lon * (-1.0);
		ewc = 'W';
	}
	alt = up->avg_alt;
	minute = (lat - (double)(int)lat) * 60.0;
	snprintf(lats, sizeof(lats), "%02d%02.4f", (int)lat, minute);
	minute = (lon - (double)(int)lon) * 60.0;
	snprintf(lons, sizeof(lons), "%03d%02.4f", (int)lon, minute);

	mx4200_send(peer, "%s,%03d,,,,,%s,%c,%s,%c,%.2f,%d", pmvxg,
	    PMVXG_S_INITMODEA,
	    /* day of month */
	    /* month of year */
	    /* year */
	    /* gmt */
	    lats,	/* latitude   DDMM.MMMM */
	    nsc,	/* north/south */
	    lons,	/* longitude DDDMM.MMMM */
	    ewc,	/* east/west */
	    alt,	/* Altitude */
	    1);		/* Altitude Reference (0=WGS84 ellipsoid, 1=MSL geoid)*/

	msyslog(LOG_DEBUG,
	    "mx4200: reconfig to fixed location: %s %c, %s %c, %.2f m",
		lats, nsc, lons, ewc, alt );

}

/*
 * mx4200_poll - mx4200 watchdog routine
 */
static void
mx4200_poll(
	int unit,
	struct peer *peer
	)
{
	register struct mx4200unit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * You don't need to poll this clock.  It puts out timecodes
	 * once per second.  If asked for a timestamp, take note.
	 * The next time a timecode comes in, it will be fed back.
	 */

	/*
	 * If we haven't had a response in a while, reset the receiver.
	 */
	if (up->pollcnt > 0) {
		up->pollcnt--;
	} else {
		refclock_report(peer, CEVNT_TIMEOUT);

		/*
		 * Request a "000" status message which should trigger a
		 * reconfig
		 */
		mx4200_send(peer, "%s,%03d",
		    "CDGPQ",		/* query from CDU to GPS */
		    PMVXG_D_STATUS);	/* label of desired sentence */
	}

	/*
	 * polled every 64 seconds. Ask mx4200_receive to hand in
	 * a timestamp.
	 */
	up->polled = 1;
	pp->polls++;

	/*
	 * Output receiver status information.
	 */
	if ((up->log_time > 0) && (current_time > up->log_time)) {
		up->log_time = 0;
		/*
		 * Output the following messages once, for debugging.
		 *    "004" Mode Data
		 *    "523" Time Recovery Parameters
		 */
		mx4200_send(peer, "%s,%03d", "CDGPQ", PMVXG_D_MODEDATA);
		mx4200_send(peer, "%s,%03d", "CDGPQ", PMVXG_D_TRECOVUSEAGE);
	}
}

static char char2hex[] = "0123456789ABCDEF";

/*
 * mx4200_receive - receive gps data
 */
static void
mx4200_receive(
	struct recvbuf *rbufp
	)
{
	register struct mx4200unit *up;
	struct refclockproc *pp;
	struct peer *peer;
	char *cp;
	int sentence_type;
	u_char ck;

	/*
	 * Initialize pointers and read the timecode and timestamp.
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * If operating mode has been changed, then reinitialize the receiver
	 * before doing anything else.
	 */
	if ((pp->sloppyclockflag & CLK_FLAG2) !=
	    (up->sloppyclockflag & CLK_FLAG2)) {
		up->sloppyclockflag = pp->sloppyclockflag;
		mx4200_debug(peer,
		    "mx4200_receive: mode switch: reset receiver\n");
		mx4200_config(peer);
		return;
	}
	up->sloppyclockflag = pp->sloppyclockflag;

	/*
	 * Read clock output.  Automatically handles STREAMS, CLKLDISC.
	 */
	pp->lencode = refclock_gtlin(rbufp, pp->a_lastcode, BMAX, &pp->lastrec);

	/*
	 * There is a case where <cr><lf> generates 2 timestamps.
	 */
	if (pp->lencode == 0)
		return;

	up->pollcnt = 2;
	pp->a_lastcode[pp->lencode] = '\0';
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
	mx4200_debug(peer, "mx4200_receive: %d %s\n",
		     pp->lencode, pp->a_lastcode);

	/*
	 * The structure of the control port sentences is based on the
	 * NMEA-0183 Standard for interfacing Marine Electronics
	 * Navigation Devices (Version 1.5)
	 *
	 *	$PMVXG,XXX, ....................*CK<cr><lf>
	 *
	 *		$	Sentence Start Identifier (reserved char)
	 *			   (Start-of-Sentence Identifier)
	 *		P	Special ID (Proprietary)
	 *		MVX	Originator ID (Magnavox)
	 *		G	Interface ID (GPS)
	 *		,	Field Delimiters (reserved char)
	 *		XXX	Sentence Type
	 *		......	Data
	 *		*	Checksum Field Delimiter (reserved char)
	 *		CK	Checksum
	 *		<cr><lf> Carriage-Return/Line Feed (reserved chars)
	 *			   (End-of-Sentence Identifier)
	 *
	 * Reject if any important landmarks are missing.
	 */
	cp = pp->a_lastcode + pp->lencode - 3;
	if (cp < pp->a_lastcode || *pp->a_lastcode != '$' || cp[0] != '*' ) {
		mx4200_debug(peer, "mx4200_receive: bad format\n");
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}

	/*
	 * Check and discard the checksum
	 */
	ck = mx4200_cksum(&pp->a_lastcode[1], pp->lencode - 4);
	if (char2hex[ck >> 4] != cp[1] || char2hex[ck & 0xf] != cp[2]) {
		mx4200_debug(peer, "mx4200_receive: bad checksum\n");
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}
	*cp = '\0';

	/*
	 * Get the sentence type.
	 */
	sentence_type = 0;
	if ((cp = strchr(pp->a_lastcode, ',')) == NULL) {
		mx4200_debug(peer, "mx4200_receive: no sentence\n");
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}
	cp++;
	sentence_type = strtol(cp, &cp, 10);

	/*
	 * Process the sentence according to its type.
	 */
	switch (sentence_type) {

	/*
	 * "000" Status message
	 */
	case PMVXG_D_STATUS:
		/*
		 * XXX
		 * Since we configure the receiver to not give us status
		 * messages and since the receiver outputs status messages by
		 * default after being reset to factory defaults when sent the
		 * "$PMVXG,018,C\r\n" message, any status message we get
		 * indicates the reciever needs to be initialized; thus, it is
		 * not necessary to decode the status message.
		 */
		if ((cp = mx4200_parse_s(peer)) != NULL) {
			mx4200_debug(peer,
				     "mx4200_receive: status: %s\n", cp);
		}
		mx4200_debug(peer, "mx4200_receive: reset receiver\n");
		mx4200_config(peer);
		break;

	/*
	 * "021" Position, Height, Velocity message,
	 *  if we are still averaging our position
	 */
	case PMVXG_D_PHV:
		if (!up->known) {
			/*
			 * Parse the message, calculating our averaged position.
			 */
			if ((cp = mx4200_parse_p(peer)) != NULL) {
				mx4200_debug(peer, "mx4200_receive: pos: %s\n", cp);
				return;
			}
			mx4200_debug(peer,
			    "mx4200_receive: position avg %f %.9f %.9f %.4f\n",
			    up->N_fixes, up->avg_lat, up->avg_lon, up->avg_alt);
			/*
			 * Reinitialize as a reference station
			 * if position is well known.
			 */
			if (current_time > up->clamp_time) {
				up->known++;
				mx4200_debug(peer, "mx4200_receive: reconfiguring!\n");
				mx4200_ref(peer);
			}
		}
		break;

	/*
	 * Print to the syslog:
	 * "004" Mode Data
	 * "030" Software Configuration
	 * "523" Time Recovery Parameters Currently in Use
	 */
	case PMVXG_D_MODEDATA:
	case PMVXG_D_SOFTCONF:
	case PMVXG_D_TRECOVUSEAGE:

		if ((cp = mx4200_parse_s(peer)) != NULL) {
			mx4200_debug(peer,
				     "mx4200_receive: multi-record: %s\n", cp);
		}
		break;

	/*
	 * "830" Time Recovery Results message
	 */
	case PMVXG_D_TRECOVOUT:

		/*
		 * Capture the last PPS signal.
		 * Precision timestamp is returned in pp->lastrec
		 */
		if (0 != mx4200_pps(peer)) {
			mx4200_debug(peer, "mx4200_receive: pps failure\n");
			refclock_report(peer, CEVNT_FAULT);
			return;
		}


		/*
		 * Parse the time recovery message, and keep the info
		 * to print the pretty billboards.
		 */
		if ((cp = mx4200_parse_t(peer)) != NULL) {
			mx4200_debug(peer, "mx4200_receive: time: %s\n", cp);
			refclock_report(peer, CEVNT_BADREPLY);
			return;
		}

		/*
		 * Add the new sample to a median filter.
		 */
		if (!refclock_process(pp)) {
			mx4200_debug(peer,"mx4200_receive: offset: %.6f\n",
			    pp->offset);
			refclock_report(peer, CEVNT_BADTIME);
			return;
		}

		/*
		 * The clock will blurt a timecode every second but we only
		 * want one when polled.  If we havn't been polled, bail out.
		 */
		if (!up->polled)
			return;

		/*
		 * Return offset and dispersion to control module.  We use
		 * lastrec as both the reference time and receive time in
		 * order to avoid being cute, like setting the reference time
		 * later than the receive time, which may cause a paranoid
		 * protocol module to chuck out the data.
		 */
		mx4200_debug(peer, "mx4200_receive: process time: ");
		mx4200_debug(peer, "%4d-%03d %02d:%02d:%02d at %s, %.6f\n",
		    pp->year, pp->day, pp->hour, pp->minute, pp->second,
		    prettydate(&pp->lastrec), pp->offset);
		pp->lastref = pp->lastrec;
		refclock_receive(peer);

		/*
		 * We have succeeded in answering the poll.
		 * Turn off the flag and return
		 */
		up->polled = 0;
		break;

	/*
	 * Ignore all other sentence types
	 */
	default:
		break;

	} /* switch (sentence_type) */

	return;
}


/*
 * Parse a mx4200 time recovery message. Returns a string if error.
 *
 * A typical message looks like this.  Checksum has already been stripped.
 *
 *    $PMVXG,830,T,YYYY,MM,DD,HH:MM:SS,U,S,FFFFFF,PPPPP,BBBBBB,LL
 *
 *	Field	Field Contents
 *	-----	--------------
 *		Block Label: $PMVXG
 *		Sentence Type: 830=Time Recovery Results
 *			This sentence is output approximately 1 second
 *			preceding the 1PPS output.  It indicates the
 *			exact time of the next pulse, whether or not the
 *			time mark will be valid (based on operator-specified
 *			error tolerance), the time to which the pulse is
 *			synchronized, the receiver operating mode,
 *			and the time error of the *last* 1PPS output.
 *	1  char Time Mark Valid: T=Valid, F=Not Valid
 *	2  int  Year: 1993-
 *	3  int  Month of Year: 1-12
 *	4  int  Day of Month: 1-31
 *	5  int  Time of Day: HH:MM:SS
 *	6  char Time Synchronization: U=UTC, G=GPS
 *	7  char Time Recovery Mode: D=Dynamic, S=Static,
 *			K=Known Position, N=No Time Recovery
 *	8  int  Oscillator Offset: The filter's estimate of the oscillator
 *			frequency error, in parts per billion (ppb).
 *	9  int  Time Mark Error: The computed error of the *last* pulse
 *			output, in nanoseconds.
 *	10 int  User Time Bias: Operator specified bias, in nanoseconds
 *	11 int  Leap Second Flag: Indicates that a leap second will
 *			occur.  This value is usually zero, except during
 *			the week prior to the leap second occurrence, when
 *			this value will be set to +1 or -1.  A value of
 *			+1 indicates that GPS time will be 1 second
 *			further ahead of UTC time.
 *
 */
static char *
mx4200_parse_t(
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct mx4200unit *up;
	char   time_mark_valid, time_sync, op_mode;
	int    sentence_type, valid;
	int    year, day_of_year, month, day_of_month;
	int    hour, minute, second, leapsec_warn;
	int    oscillator_offset, time_mark_error, time_bias;

	pp = peer->procptr;
	up = pp->unitptr;

	leapsec_warn = 0;  /* Not all receivers output leap second warnings (!) */
	sscanf(pp->a_lastcode,
		"$PMVXG,%d,%c,%d,%d,%d,%d:%d:%d,%c,%c,%d,%d,%d,%d",
		&sentence_type, &time_mark_valid, &year, &month, &day_of_month,
		&hour, &minute, &second, &time_sync, &op_mode,
		&oscillator_offset, &time_mark_error, &time_bias, &leapsec_warn);

	if (sentence_type != PMVXG_D_TRECOVOUT)
		return ("wrong rec-type");

	switch (time_mark_valid) {
		case 'T':
			valid = 1;
			break;
		case 'F':
			valid = 0;
			break;
		default:
			return ("bad pulse-valid");
	}

	switch (time_sync) {
		case 'G':
			return ("synchronized to GPS; should be UTC");
		case 'U':
			break; /* UTC -> ok */
		default:
			return ("not synchronized to UTC");
	}

	/*
	 * Check for insane time (allow for possible leap seconds)
	 */
	if (second > 60 || minute > 59 || hour > 23 ||
	    second <  0 || minute <  0 || hour <  0) {
		mx4200_debug(peer,
		    "mx4200_parse_t: bad time %02d:%02d:%02d",
		    hour, minute, second);
		if (leapsec_warn != 0)
			mx4200_debug(peer, " (leap %+d\n)", leapsec_warn);
		mx4200_debug(peer, "\n");
		refclock_report(peer, CEVNT_BADTIME);
		return ("bad time");
	}
	if ( second == 60 ) {
		msyslog(LOG_DEBUG,
		    "mx4200: leap second! %02d:%02d:%02d",
		    hour, minute, second);
	}

	/*
	 * Check for insane date
	 * (Certainly can't be any year before this code was last altered!)
	 */
	if (day_of_month > 31 || month > 12 ||
	    day_of_month <  1 || month <  1 || year < YEAR_LAST_MODIFIED) {
		mx4200_debug(peer,
		    "mx4200_parse_t: bad date (%4d-%02d-%02d)\n",
		    year, month, day_of_month);
		refclock_report(peer, CEVNT_BADDATE);
		return ("bad date");
	}

	/*
	 * Silly Hack for MX4200:
	 * ASCII message is for *next* 1PPS signal, but we have the
	 * timestamp for the *last* 1PPS signal.  So we have to subtract
	 * a second.  Discard if we are on a month boundary to avoid
	 * possible leap seconds and leap days.
	 */
	second--;
	if (second < 0) {
		second = 59;
		minute--;
		if (minute < 0) {
			minute = 59;
			hour--;
			if (hour < 0) {
				hour = 23;
				day_of_month--;
				if (day_of_month < 1) {
					return ("sorry, month boundary");
				}
			}
		}
	}

	/*
	 * Calculate Julian date
	 */
	if (!(day_of_year = mx4200_jday(year, month, day_of_month))) {
		mx4200_debug(peer,
		    "mx4200_parse_t: bad julian date %d (%4d-%02d-%02d)\n",
		    day_of_year, year, month, day_of_month);
		refclock_report(peer, CEVNT_BADDATE);
		return("invalid julian date");
	}

	/*
	 * Setup leap second indicator
	 */
	switch (leapsec_warn) {
		case 0:
			pp->leap = LEAP_NOWARNING;
			break;
		case 1:
			pp->leap = LEAP_ADDSECOND;
			break;
		case -1:
			pp->leap = LEAP_DELSECOND;
			break;
		default:
			pp->leap = LEAP_NOTINSYNC;
	}

	/*
	 * Any change to the leap second warning status?
	 */
	if (leapsec_warn != up->last_leap ) {
		msyslog(LOG_DEBUG,
		    "mx4200: leap second warning: %d to %d (%d)",
		    up->last_leap, leapsec_warn, pp->leap);
	}
	up->last_leap = leapsec_warn;

	/*
	 * Copy time data for billboard monitoring.
	 */

	pp->year   = year;
	pp->day    = day_of_year;
	pp->hour   = hour;
	pp->minute = minute;
	pp->second = second;

	/*
	 * Toss if sentence is marked invalid
	 */
	if (!valid || pp->leap == LEAP_NOTINSYNC) {
		mx4200_debug(peer, "mx4200_parse_t: time mark not valid\n");
		refclock_report(peer, CEVNT_BADTIME);
		return ("pulse invalid");
	}

	return (NULL);
}

/*
 * Calculate the checksum
 */
static u_char
mx4200_cksum(
	register char *cp,
	register int n
	)
{
	register u_char ck;

	for (ck = 0; n-- > 0; cp++)
		ck ^= *cp;
	return (ck);
}

/*
 * Tables to compute the day of year.  Viva la leap.
 */
static int day1tab[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static int day2tab[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/*
 * Calculate the the Julian Day
 */
static int
mx4200_jday(
	int year,
	int month,
	int day_of_month
	)
{
	register int day, i;
	int leap_year;

	/*
	 * Is this a leap year ?
	 */
	if (year % 4) {
		leap_year = 0; /* FALSE */
	} else {
		if (year % 100) {
			leap_year = 1; /* TRUE */
		} else {
			if (year % 400) {
				leap_year = 0; /* FALSE */
			} else {
				leap_year = 1; /* TRUE */
			}
		}
	}

	/*
	 * Calculate the Julian Date
	 */
	day = day_of_month;

	if (leap_year) {
		/* a leap year */
		if (day > day2tab[month - 1]) {
			return (0);
		}
		for (i = 0; i < month - 1; i++)
		    day += day2tab[i];
	} else {
		/* not a leap year */
		if (day > day1tab[month - 1]) {
			return (0);
		}
		for (i = 0; i < month - 1; i++)
		    day += day1tab[i];
	}
	return (day);
}

/*
 * Parse a mx4200 position/height/velocity sentence.
 *
 * A typical message looks like this.  Checksum has already been stripped.
 *
 * $PMVXG,021,SSSSSS.SS,DDMM.MMMM,N,DDDMM.MMMM,E,HHHHH.H,GGGG.G,EEEE.E,WWWW.W,MM
 *
 *	Field	Field Contents
 *	-----	--------------
 *		Block Label: $PMVXG
 *		Sentence Type: 021=Position, Height Velocity Data
 *			This sentence gives the receiver position, height,
 *			navigation mode, and velocity north/east.
 *			*This sentence is intended for post-analysis
 *			applications.*
 *	1 float UTC measurement time (seconds into week)
 *	2 float WGS-84 Lattitude (degrees, minutes)
 *	3  char N=North, S=South
 *	4 float WGS-84 Longitude (degrees, minutes)
 *	5  char E=East, W=West
 *	6 float Altitude (meters above mean sea level)
 *	7 float Geoidal height (meters)
 *	8 float East velocity (m/sec)
 *	9 float West Velocity (m/sec)
 *	10  int Navigation Mode
 *		    Mode if navigating:
 *			1 = Position from remote device
 *			2 = 2-D position
 *			3 = 3-D position
 *			4 = 2-D differential position
 *			5 = 3-D differential position
 *			6 = Static
 *			8 = Position known -- reference station
 *			9 = Position known -- Navigator
 *		    Mode if not navigating:
 *			51 = Too few satellites
 *			52 = DOPs too large
 *			53 = Position STD too large
 *			54 = Velocity STD too large
 *			55 = Too many iterations for velocity
 *			56 = Too many iterations for position
 *			57 = 3 sat startup failed
 *			58 = Command abort
 */
static char *
mx4200_parse_p(
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct mx4200unit *up;
	int sentence_type, mode;
	double mtime, lat, lon, alt, geoid, vele, veln;
	char   north_south, east_west;

	pp = peer->procptr;
	up = pp->unitptr;

	/* Should never happen! */
	if (up->moving) return ("mobile platform - no pos!");

	sscanf ( pp->a_lastcode,
		"$PMVXG,%d,%lf,%lf,%c,%lf,%c,%lf,%lf,%lf,%lf,%d",
		&sentence_type, &mtime, &lat, &north_south, &lon, &east_west,
		&alt, &geoid, &vele, &veln, &mode);

	/* Sentence type */
	if (sentence_type != PMVXG_D_PHV)
		return ("wrong rec-type");

	/*
	 * return if not navigating
	 */
	if (mode > 10)
		return ("not navigating");
	if (mode != 3 && mode != 5)
		return ("not navigating in 3D");

	/* Latitude (always +ve) and convert DDMM.MMMM to decimal */
	if (lat <  0.0) return ("negative latitude");
	if (lat > 9000.0) lat = 9000.0;
	lat *= 0.01;
	lat = ((int)lat) + (((lat - (int)lat)) * 1.6666666666666666);

	/* North/South */
	switch (north_south) {
		case 'N':
			break;
		case 'S':
			lat *= -1.0;
			break;
		default:
			return ("invalid north/south indicator");
	}

	/* Longitude (always +ve) and convert DDDMM.MMMM to decimal */
	if (lon <   0.0) return ("negative longitude");
	if (lon > 180.0) lon = 180.0;
	lon *= 0.01;
	lon = ((int)lon) + (((lon - (int)lon)) * 1.6666666666666666);

	/* East/West */
	switch (east_west) {
		case 'E':
			break;
		case 'W':
			lon *= -1.0;
			break;
		default:
			return ("invalid east/west indicator");
	}

	/*
	 * Normalize longitude to near 0 degrees.
	 * Assume all data are clustered around first reading.
	 */
	if (up->central_meridian == NOT_INITIALIZED) {
		up->central_meridian = lon;
		mx4200_debug(peer,
		    "mx4200_receive: central meridian =  %.9f \n",
		    up->central_meridian);
	}
	lon -= up->central_meridian;
	if (lon < -180.0) lon += 360.0;
	if (lon >  180.0) lon -= 360.0;

	/*
	 * Calculate running averages
	 */

	up->avg_lon = (up->N_fixes * up->avg_lon) + lon;
	up->avg_lat = (up->N_fixes * up->avg_lat) + lat;
	up->avg_alt = (up->N_fixes * up->avg_alt) + alt;

	up->N_fixes += 1.0;

	up->avg_lon /= up->N_fixes;
	up->avg_lat /= up->N_fixes;
	up->avg_alt /= up->N_fixes;

	mx4200_debug(peer,
	    "mx4200_receive: position rdg %.0f: %.9f %.9f %.4f (CM=%.9f)\n",
	    up->N_fixes, lat, lon, alt, up->central_meridian);

	return (NULL);
}

/*
 * Parse a mx4200 Status sentence
 * Parse a mx4200 Mode Data sentence
 * Parse a mx4200 Software Configuration sentence
 * Parse a mx4200 Time Recovery Parameters Currently in Use sentence
 * (used only for logging raw strings)
 *
 * A typical message looks like this.  Checksum has already been stripped.
 *
 * $PMVXG,000,XXX,XX,X,HHMM,X
 *
 *	Field	Field Contents
 *	-----	--------------
 *		Block Label: $PMVXG
 *		Sentence Type: 000=Status.
 *			Returns status of the receiver to the controller.
 *	1	Current Receiver Status:
 *		ACQ = Satellite re-acquisition
 *		ALT = Constellation selection
 *		COR = Providing corrections (for reference stations only)
 *		IAC = Initial acquisition
 *		IDL = Idle, no satellites
 *		NAV = Navigation
 *		STS = Search the Sky (no almanac available)
 *		TRK = Tracking
 *	2	Number of satellites that should be visible
 *	3	Number of satellites being tracked
 *	4	Time since last navigation status if not currently navigating
 *		(hours, minutes)
 *	5	Initialization status:
 *		0 = Waiting for initialization parameters
 *		1 = Initialization completed
 *
 * A typical message looks like this.  Checksum has already been stripped.
 *
 * $PMVXG,004,C,R,D,H.HH,V.VV,TT,HHHH,VVVV,T
 *
 *	Field	Field Contents
 *	-----	--------------
 *		Block Label: $PMVXG
 *		Sentence Type: 004=Software Configuration.
 *			Defines the navigation mode and criteria for
 *			acceptable navigation for the receiver.
 *	1	Constrain Altitude Mode:
 *		0 = Auto.  Constrain altitude (2-D solution) and use
 *		    manual altitude input when 3 sats avalable.  Do
 *		    not constrain altitude (3-D solution) when 4 sats
 *		    available.
 *		1 = Always constrain altitude (2-D solution).
 *		2 = Never constrain altitude (3-D solution).
 *		3 = Coast.  Constrain altitude (2-D solution) and use
 *		    last GPS altitude calculation when 3 sats avalable.
 *		    Do not constrain altitude (3-D solution) when 4 sats
 *		    available.
 *	2	Altitude Reference: (always 0 for MX4200)
 *		0 = Ellipsoid
 *		1 = Geoid (MSL)
 *	3	Differential Navigation Control:
 *		0 = Disabled
 *		1 = Enabled
 *	4	Horizontal Acceleration Constant (m/sec**2)
 *	5	Vertical Acceleration Constant (m/sec**2) (0 for MX4200)
 *	6	Tracking Elevation Limit (degrees)
 *	7	HDOP Limit
 *	8	VDOP Limit
 *	9	Time Output Mode:
 *		U = UTC
 *		L = Local time
 *	10	Local Time Offset (minutes) (absent on MX4200)
 *
 * A typical message looks like this.  Checksum has already been stripped.
 *
 * $PMVXG,030,NNNN,FFF
 *
 *	Field	Field Contents
 *	-----	--------------
 *		Block Label: $PMVXG
 *		Sentence Type: 030=Software Configuration.
 *			This sentence contains the navigation processor
 *			and baseband firmware version numbers.
 *	1	Nav Processor Version Number
 *	2	Baseband Firmware Version Number
 *
 * A typical message looks like this.  Checksum has already been stripped.
 *
 * $PMVXG,523,M,S,M,EEEE,BBBBBB,C,R
 *
 *	Field	Field Contents
 *	-----	--------------
 *		Block Label: $PMVXG
 *		Sentence Type: 523=Time Recovery Parameters Currently in Use.
 *			This sentence contains the configuration of the
 *			time recovery feature of the receiver.
 *	1	Time Recovery Mode:
 *		D = Dynamic; solve for position and time while moving
 *		S = Static; solve for position and time while stationary
 *		K = Known position input, solve for time only
 *		N = No time recovery
 *	2	Time Synchronization:
 *		U = UTC time
 *		G = GPS time
 *	3	Time Mark Mode:
 *		A = Always output a time pulse
 *		V = Only output time pulse if time is valid (as determined
 *		    by Maximum Time Error)
 *	4	Maximum Time Error - the maximum error (in nanoseconds) for
 *		which a time mark will be considered valid.
 *	5	User Time Bias - external bias in nanoseconds
 *	6	Time Message Control:
 *		0 = Do not output the time recovery message
 *		1 = Output the time recovery message (record 830) to
 *		    Control port
 *		2 = Output the time recovery message (record 830) to
 *		    Equipment port
 *	7	Reserved
 *	8	Position Known PRN (absent on MX 4200)
 *
 */
static char *
mx4200_parse_s(
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct mx4200unit *up;
	int sentence_type;

	pp = peer->procptr;
	up = pp->unitptr;

        sscanf ( pp->a_lastcode, "$PMVXG,%d", &sentence_type);

	/* Sentence type */
	switch (sentence_type) {

		case PMVXG_D_STATUS:
			msyslog(LOG_DEBUG,
			  "mx4200: status: %s", pp->a_lastcode);
			break;
		case PMVXG_D_MODEDATA:
			msyslog(LOG_DEBUG,
			  "mx4200: mode data: %s", pp->a_lastcode);
			break;
		case PMVXG_D_SOFTCONF:
			msyslog(LOG_DEBUG,
			  "mx4200: firmware configuration: %s", pp->a_lastcode);
			break;
		case PMVXG_D_TRECOVUSEAGE:
			msyslog(LOG_DEBUG,
			  "mx4200: time recovery parms: %s", pp->a_lastcode);
			break;
		default:
			return ("wrong rec-type");
	}

	return (NULL);
}

/*
 * Process a PPS signal, placing a timestamp in pp->lastrec.
 */
static int
mx4200_pps(
	struct peer *peer
	)
{
	int temp_serial;
	struct refclockproc *pp;
	struct mx4200unit *up;

	struct timespec timeout;

	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Grab the timestamp of the PPS signal.
	 */
	temp_serial = up->pps_i.assert_sequence;
	timeout.tv_sec  = 0;
	timeout.tv_nsec = 0;
	if (time_pps_fetch(up->pps_h, PPS_TSFMT_TSPEC, &(up->pps_i),
			&timeout) < 0) {
		mx4200_debug(peer,
		  "mx4200_pps: time_pps_fetch: serial=%lu, %m\n",
		     (unsigned long)up->pps_i.assert_sequence);
		refclock_report(peer, CEVNT_FAULT);
		return(1);
	}
	if (temp_serial == up->pps_i.assert_sequence) {
		mx4200_debug(peer,
		   "mx4200_pps: assert_sequence serial not incrementing: %lu\n",
			(unsigned long)up->pps_i.assert_sequence);
		refclock_report(peer, CEVNT_FAULT);
		return(1);
	}
	/*
	 * Check pps serial number against last one
	 */
	if (up->lastserial + 1 != up->pps_i.assert_sequence &&
	    up->lastserial != 0) {
		if (up->pps_i.assert_sequence == up->lastserial) {
			mx4200_debug(peer, "mx4200_pps: no new pps event\n");
		} else {
			mx4200_debug(peer, "mx4200_pps: missed %lu pps events\n",
			    up->pps_i.assert_sequence - up->lastserial - 1UL);
		}
		refclock_report(peer, CEVNT_FAULT);
	}
	up->lastserial = up->pps_i.assert_sequence;

	/*
	 * Return the timestamp in pp->lastrec
	 */

	pp->lastrec.l_ui = up->pps_i.assert_timestamp.tv_sec +
			   (u_int32) JAN_1970;
	pp->lastrec.l_uf = ((double)(up->pps_i.assert_timestamp.tv_nsec) *
			   4.2949672960) + 0.5;

	return(0);
}

/*
 * mx4200_debug - print debug messages
 */
static void
mx4200_debug(struct peer *peer, char *fmt, ...)
{
#ifdef DEBUG
	va_list ap;
	struct refclockproc *pp;
	struct mx4200unit *up;

	if (debug) {
		va_start(ap, fmt);

		pp = peer->procptr;
		up = pp->unitptr;

		/*
		 * Print debug message to stdout
		 * In the future, we may want to get get more creative...
		 */
		mvprintf(fmt, ap);

		va_end(ap);
	}
#endif
}

/*
 * Send a character string to the receiver.  Checksum is appended here.
 */
#if defined(__STDC__)
static void
mx4200_send(struct peer *peer, char *fmt, ...)
#else
static void
mx4200_send(peer, fmt, va_alist)
     struct peer *peer;
     char *fmt;
     va_dcl
#endif /* __STDC__ */
{
	struct refclockproc *pp;
	struct mx4200unit *up;

	register char *cp, *ep;
	register int n, m;
	va_list ap;
	char buf[1024];
	u_char ck;

	pp = peer->procptr;
	up = pp->unitptr;

	cp = buf;
	ep = cp + sizeof(buf);
	*cp++ = '$';
	
#if defined(__STDC__)
	va_start(ap, fmt);
#else
	va_start(ap);
#endif /* __STDC__ */
	n = VSNPRINTF((cp, (size_t)(ep - cp), fmt, ap));
	va_end(ap);
	if (n < 0 || (size_t)n >= (size_t)(ep - cp))
		goto overflow;

	ck = mx4200_cksum(cp, n);
	cp += n;	    
	n = SNPRINTF((cp, (size_t)(ep - cp), "*%02X\r\n", ck));
	if (n < 0 || (size_t)n >= (size_t)(ep - cp))
		goto overflow;
	cp += n;
	m = write(pp->io.fd, buf, (unsigned)(cp - buf));
	if (m < 0)
		msyslog(LOG_ERR, "mx4200_send: write: %m (%s)", buf);
	mx4200_debug(peer, "mx4200_send: %d %s\n", m, buf);
	
  overflow:
	msyslog(LOG_ERR, "mx4200_send: %s", "data exceeds buffer size");
}

#else
int refclock_mx4200_bs;
#endif /* REFCLOCK */
