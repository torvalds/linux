/*
 * Copyright (c) 1997, 1998, 2003
 *	The Regents of the University of California.  All rights reserved.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_JUPITER) && defined(HAVE_PPSAPI)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

#include "jupiter.h"

#ifdef HAVE_PPSAPI
# include "ppsapi_timepps.h"
#endif

#ifdef WORDS_BIGENDIAN
#define getshort(s) ((((s) & 0xff) << 8) | (((s) >> 8) & 0xff))
#define putshort(s) ((((s) & 0xff) << 8) | (((s) >> 8) & 0xff))
#else
#define getshort(s) ((u_short)(s))
#define putshort(s) ((u_short)(s))
#endif

/*
 * This driver supports the Rockwell Jupiter GPS Receiver board
 * adapted to precision timing applications.  It requires the
 * ppsclock line discipline or streams module described in the
 * Line Disciplines and Streams Drivers page. It also requires a
 * gadget box and 1-PPS level converter, such as described in the
 * Pulse-per-second (PPS) Signal Interfacing page.
 *
 * It may work (with minor modifications) with other Rockwell GPS
 * receivers such as the CityTracker.
 */

/*
 * GPS Definitions
 */
#define	DEVICE		"/dev/gps%d"	/* device name and unit */
#define	SPEED232	B9600		/* baud */

/*
 * Radio interface parameters
 */
#define	PRECISION	(-18)	/* precision assumed (about 4 us) */
#define	REFID	"GPS\0"		/* reference id */
#define	DESCRIPTION	"Rockwell Jupiter GPS Receiver" /* who we are */
#define	DEFFUDGETIME	0	/* default fudge time (ms) */

/* Unix timestamp for the GPS epoch: January 6, 1980 */
#define GPS_EPOCH 315964800

/* Rata Die Number of first day of GPS epoch. This is the number of days
 * since 0000-12-31 to 1980-01-06 in the proleptic Gregorian Calendar.
 */
#define RDN_GPS_EPOCH (4*146097 + 138431 + 1)

/* Double short to unsigned int */
#define DS2UI(p) ((getshort((p)[1]) << 16) | getshort((p)[0]))

/* Double short to signed int */
#define DS2I(p) ((getshort((p)[1]) << 16) | getshort((p)[0]))

/* One week's worth of seconds */
#define WEEKSECS (7 * 24 * 60 * 60)

/*
 * Jupiter unit control structure.
 */
struct instance {
	struct peer *peer;		/* peer */
	u_int  pollcnt;			/* poll message counter */
	u_int  polled;			/* Hand in a time sample? */
#ifdef HAVE_PPSAPI
	pps_params_t pps_params;	/* pps parameters */
	pps_info_t pps_info;		/* last pps data */
	pps_handle_t pps_handle;	/* pps handle */
	u_int assert;			/* pps edge to use */
	u_int hardpps;			/* enable kernel mode */
	struct timespec ts;		/* last timestamp */
#endif
	l_fp limit;
	u_int gpos_gweek;		/* Current GPOS GPS week number */
	u_int gpos_sweek;		/* Current GPOS GPS seconds into week */
	u_int gweek;			/* current GPS week number */
	u_int32 lastsweek;		/* last seconds into GPS week */
	time_t timecode;		/* current ntp timecode */
	u_int32 stime;			/* used to detect firmware bug */
	int wantid;			/* don't reconfig on channel id msg */
	u_int  moving;			/* mobile platform? */
	u_char sloppyclockflag;		/* fudge flags */
	u_short sbuf[512];		/* local input buffer */
	int ssize;			/* space used in sbuf */
};

/*
 * Function prototypes
 */
static	void	jupiter_canmsg	(struct instance *, u_int);
static	u_short	jupiter_cksum	(u_short *, u_int);
static	int	jupiter_config	(struct instance *);
static	void	jupiter_debug	(struct peer *, const char *,
				 const char *, ...) NTP_PRINTF(3, 4);
static	const char *	jupiter_parse_t	(struct instance *, u_short *);
static	const char *	jupiter_parse_gpos	(struct instance *, u_short *);
static	void	jupiter_platform	(struct instance *, u_int);
static	void	jupiter_poll	(int, struct peer *);
static	void	jupiter_control	(int, const struct refclockstat *,
				 struct refclockstat *, struct peer *);
#ifdef HAVE_PPSAPI
static	int	jupiter_ppsapi	(struct instance *);
static	int	jupiter_pps	(struct instance *);
#endif /* HAVE_PPSAPI */
static	int	jupiter_recv	(struct instance *);
static	void	jupiter_receive (struct recvbuf *rbufp);
static	void	jupiter_reqmsg	(struct instance *, u_int, u_int);
static	void	jupiter_reqonemsg(struct instance *, u_int);
static	char *	jupiter_send	(struct instance *, struct jheader *);
static	void	jupiter_shutdown(int, struct peer *);
static	int	jupiter_start	(int, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_jupiter = {
	jupiter_start,		/* start up driver */
	jupiter_shutdown,	/* shut down driver */
	jupiter_poll,		/* transmit poll message */
	jupiter_control,	/* (clock control) */
	noentry,		/* (clock init) */
	noentry,		/* (clock buginfo) */
	NOFLAGS			/* not used */
};

/*
 * jupiter_start - open the devices and initialize data for processing
 */
static int
jupiter_start(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct instance *instance;
	int fd;
	char gpsdev[20];

	/*
	 * Open serial port
	 */
	snprintf(gpsdev, sizeof(gpsdev), DEVICE, unit);
	fd = refclock_open(gpsdev, SPEED232, LDISC_RAW);
	if (fd <= 0) {
		jupiter_debug(peer, "jupiter_start", "open %s: %m",
			      gpsdev);
		return (0);
	}

	/* Allocate unit structure */
	instance = emalloc_zero(sizeof(*instance));
	instance->peer = peer;
	pp = peer->procptr;
	pp->io.clock_recv = jupiter_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	if (!io_addclock(&pp->io)) {
		close(fd);
		pp->io.fd = -1;
		free(instance);
		return (0);
	}
	pp->unitptr = instance;

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy((char *)&pp->refid, REFID, 4);

#ifdef HAVE_PPSAPI
	instance->assert = 1;
	instance->hardpps = 0;
	/*
	 * Start the PPSAPI interface if it is there. Default to use
	 * the assert edge and do not enable the kernel hardpps.
	 */
	if (time_pps_create(fd, &instance->pps_handle) < 0) {
		instance->pps_handle = 0;
		msyslog(LOG_ERR,
			"refclock_jupiter: time_pps_create failed: %m");
	}
	else if (!jupiter_ppsapi(instance))
		goto clean_up;
#endif /* HAVE_PPSAPI */

	/* Ensure the receiver is properly configured */
	if (!jupiter_config(instance))
		goto clean_up;

	return (1);

clean_up:
	jupiter_shutdown(unit, peer);
	pp->unitptr = 0;
	return (0);
}

/*
 * jupiter_shutdown - shut down the clock
 */
static void
jupiter_shutdown(int unit, struct peer *peer)
{
	struct instance *instance;
	struct refclockproc *pp;

	pp = peer->procptr;
	instance = pp->unitptr;
	if (!instance)
		return;

#ifdef HAVE_PPSAPI
	if (instance->pps_handle) {
		time_pps_destroy(instance->pps_handle);
		instance->pps_handle = 0;
	}
#endif /* HAVE_PPSAPI */

	if (pp->io.fd != -1)
		io_closeclock(&pp->io);
	free(instance);
}

/*
 * jupiter_config - Configure the receiver
 */
static int
jupiter_config(struct instance *instance)
{
	jupiter_debug(instance->peer, __func__, "init receiver");

	/*
	 * Initialize the unit variables
	 */
	instance->sloppyclockflag = instance->peer->procptr->sloppyclockflag;
	instance->moving = !!(instance->sloppyclockflag & CLK_FLAG2);
	if (instance->moving)
		jupiter_debug(instance->peer, __func__, "mobile platform");

	instance->pollcnt     = 2;
	instance->polled      = 0;
	instance->gpos_gweek = 0;
	instance->gpos_sweek = 0;
	instance->gweek = 0;
	instance->lastsweek = 2 * WEEKSECS;
	instance->timecode = 0;
	instance->stime = 0;
	instance->ssize = 0;

	/* Stop outputting all messages */
	jupiter_canmsg(instance, JUPITER_ALL);

	/* Request the receiver id so we can syslog the firmware version */
	jupiter_reqonemsg(instance, JUPITER_O_ID);

	/* Flag that this the id was requested (so we don't get called again) */
	instance->wantid = 1;

	/* Request perodic time mark pulse messages */
	jupiter_reqmsg(instance, JUPITER_O_PULSE, 1);

	/* Request perodic geodetic position status */
	jupiter_reqmsg(instance, JUPITER_O_GPOS, 1);

	/* Set application platform type */
	if (instance->moving)
		jupiter_platform(instance, JUPITER_I_PLAT_MED);
	else
		jupiter_platform(instance, JUPITER_I_PLAT_LOW);

	return (1);
}

#ifdef HAVE_PPSAPI
/*
 * Initialize PPSAPI
 */
int
jupiter_ppsapi(
	struct instance *instance	/* unit structure pointer */
	)
{
	int capability;

	if (time_pps_getcap(instance->pps_handle, &capability) < 0) {
		msyslog(LOG_ERR,
		    "refclock_jupiter: time_pps_getcap failed: %m");
		return (0);
	}
	memset(&instance->pps_params, 0, sizeof(pps_params_t));
	if (!instance->assert)
		instance->pps_params.mode = capability & PPS_CAPTURECLEAR;
	else
		instance->pps_params.mode = capability & PPS_CAPTUREASSERT;
	if (!(instance->pps_params.mode & (PPS_CAPTUREASSERT | PPS_CAPTURECLEAR))) {
		msyslog(LOG_ERR,
		    "refclock_jupiter: invalid capture edge %d",
		    instance->assert);
		return (0);
	}
	instance->pps_params.mode |= PPS_TSFMT_TSPEC;
	if (time_pps_setparams(instance->pps_handle, &instance->pps_params) < 0) {
		msyslog(LOG_ERR,
		    "refclock_jupiter: time_pps_setparams failed: %m");
		return (0);
	}
	if (instance->hardpps) {
		if (time_pps_kcbind(instance->pps_handle, PPS_KC_HARDPPS,
				    instance->pps_params.mode & ~PPS_TSFMT_TSPEC,
				    PPS_TSFMT_TSPEC) < 0) {
			msyslog(LOG_ERR,
			    "refclock_jupiter: time_pps_kcbind failed: %m");
			return (0);
		}
		hardpps_enable = 1;
	}
/*	instance->peer->precision = PPS_PRECISION; */

#if DEBUG
	if (debug) {
		time_pps_getparams(instance->pps_handle, &instance->pps_params);
		jupiter_debug(instance->peer, __func__,
			"pps capability 0x%x version %d mode 0x%x kern %d",
			capability, instance->pps_params.api_version,
			instance->pps_params.mode, instance->hardpps);
	}
#endif

	return (1);
}

/*
 * Get PPSAPI timestamps.
 *
 * Return 0 on failure and 1 on success.
 */
static int
jupiter_pps(struct instance *instance)
{
	pps_info_t pps_info;
	struct timespec timeout, ts;
	double dtemp;
	l_fp tstmp;

	/*
	 * Convert the timespec nanoseconds field to ntp l_fp units.
	 */ 
	if (instance->pps_handle == 0)
		return 1;
	timeout.tv_sec = 0;
	timeout.tv_nsec = 0;
	memcpy(&pps_info, &instance->pps_info, sizeof(pps_info_t));
	if (time_pps_fetch(instance->pps_handle, PPS_TSFMT_TSPEC, &instance->pps_info,
	    &timeout) < 0)
		return 1;
	if (instance->pps_params.mode & PPS_CAPTUREASSERT) {
		if (pps_info.assert_sequence ==
		    instance->pps_info.assert_sequence)
			return 1;
		ts = instance->pps_info.assert_timestamp;
	} else if (instance->pps_params.mode & PPS_CAPTURECLEAR) {
		if (pps_info.clear_sequence ==
		    instance->pps_info.clear_sequence)
			return 1;
		ts = instance->pps_info.clear_timestamp;
	} else {
		return 1;
	}
	if ((instance->ts.tv_sec == ts.tv_sec) && (instance->ts.tv_nsec == ts.tv_nsec))
		return 1;
	instance->ts = ts;

	tstmp.l_ui = (u_int32)ts.tv_sec + JAN_1970;
	dtemp = ts.tv_nsec * FRAC / 1e9;
	tstmp.l_uf = (u_int32)dtemp;
	instance->peer->procptr->lastrec = tstmp;
	return 0;
}
#endif /* HAVE_PPSAPI */

/*
 * jupiter_poll - jupiter watchdog routine
 */
static void
jupiter_poll(int unit, struct peer *peer)
{
	struct instance *instance;
	struct refclockproc *pp;

	pp = peer->procptr;
	instance = pp->unitptr;

	/*
	 * You don't need to poll this clock.  It puts out timecodes
	 * once per second.  If asked for a timestamp, take note.
	 * The next time a timecode comes in, it will be fed back.
	 */

	/*
	 * If we haven't had a response in a while, reset the receiver.
	 */
	if (instance->pollcnt > 0) {
		instance->pollcnt--;
	} else {
		refclock_report(peer, CEVNT_TIMEOUT);

		/* Request the receiver id to trigger a reconfig */
		jupiter_reqonemsg(instance, JUPITER_O_ID);
		instance->wantid = 0;
	}

	/*
	 * polled every 64 seconds. Ask jupiter_receive to hand in
	 * a timestamp.
	 */
	instance->polled = 1;
	pp->polls++;
}

/*
 * jupiter_control - fudge control
 */
static void
jupiter_control(
	int unit,		/* unit (not used) */
	const struct refclockstat *in, /* input parameters (not used) */
	struct refclockstat *out, /* output parameters (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct instance *instance;
	u_char sloppyclockflag;

	pp = peer->procptr;
	instance = pp->unitptr;

	DTOLFP(pp->fudgetime2, &instance->limit);
	/* Force positive value. */
	if (L_ISNEG(&instance->limit))
		L_NEG(&instance->limit);

#ifdef HAVE_PPSAPI
	instance->assert = !(pp->sloppyclockflag & CLK_FLAG3);
	jupiter_ppsapi(instance);
#endif /* HAVE_PPSAPI */

	sloppyclockflag = instance->sloppyclockflag;
	instance->sloppyclockflag = pp->sloppyclockflag;
	if ((instance->sloppyclockflag & CLK_FLAG2) !=
	    (sloppyclockflag & CLK_FLAG2)) {
		jupiter_debug(peer, __func__,
		    "mode switch: reset receiver");
		jupiter_config(instance);
		return;
	}
}

/*
 * jupiter_receive - receive gps data
 * Gag me!
 */
static void
jupiter_receive(struct recvbuf *rbufp)
{
	size_t bpcnt;
	int cc, size, ppsret;
	time_t last_timecode;
	u_int32 laststime;
	const char *cp;
	u_char *bp;
	u_short *sp;
	struct jid *ip;
	struct jheader *hp;
	struct peer *peer;
	struct refclockproc *pp;
	struct instance *instance;
	l_fp tstamp;

	/* Initialize pointers and read the timecode and timestamp */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	instance = pp->unitptr;

	bp = (u_char *)rbufp->recv_buffer;
	bpcnt = rbufp->recv_length;

	/* This shouldn't happen */
	if (bpcnt > sizeof(instance->sbuf) - instance->ssize)
		bpcnt = sizeof(instance->sbuf) - instance->ssize;

	/* Append to input buffer */
	memcpy((u_char *)instance->sbuf + instance->ssize, bp, bpcnt);
	instance->ssize += bpcnt;

	/* While there's at least a header and we parse an intact message */
	while (instance->ssize > (int)sizeof(*hp) && (cc = jupiter_recv(instance)) > 0) {
		instance->pollcnt = 2;

		tstamp = rbufp->recv_time;
		hp = (struct jheader *)instance->sbuf;
		sp = (u_short *)(hp + 1);
		size = cc - sizeof(*hp);
		switch (getshort(hp->id)) {

		case JUPITER_O_PULSE:
			if (size != sizeof(struct jpulse)) {
				jupiter_debug(peer, __func__,
				    "pulse: len %d != %u",
				    size, (int)sizeof(struct jpulse));
				refclock_report(peer, CEVNT_BADREPLY);
				break;
			}

			/*
			 * There appears to be a firmware bug related
			 * to the pulse message; in addition to the one
			 * per second messages, we get an extra pulse
			 * message once an hour (on the anniversary of
			 * the cold start). It seems to come 200 ms
			 * after the one requested. So if we've seen a
			 * pulse message in the last 210 ms, we skip
			 * this one.
			 */
			laststime = instance->stime;
			instance->stime = DS2UI(((struct jpulse *)sp)->stime);
			if (laststime != 0 && instance->stime - laststime <= 21) {
				jupiter_debug(peer, __func__,
				"avoided firmware bug (stime %.2f, laststime %.2f)",
				(double)instance->stime * 0.01, (double)laststime * 0.01);
				break;
			}

			/* Retrieve pps timestamp */
			ppsret = jupiter_pps(instance);

			/*
			 * Add one second if msg received early
			 * (i.e. before limit, a.k.a. fudgetime2) in
			 * the second.
			 */
			L_SUB(&tstamp, &pp->lastrec);
			if (!L_ISGEQ(&tstamp, &instance->limit))
				++pp->lastrec.l_ui;

			/* Parse timecode (even when there's no pps) */
			last_timecode = instance->timecode;
			if ((cp = jupiter_parse_t(instance, sp)) != NULL) {
				jupiter_debug(peer, __func__,
				    "pulse: %s", cp);
				break;
			}

			/* Bail if we didn't get a pps timestamp */
			if (ppsret)
				break;

			/* Bail if we don't have the last timecode yet */
			if (last_timecode == 0)
				break;

			/* Add the new sample to a median filter */
			tstamp.l_ui = JAN_1970 + (u_int32)last_timecode;
			tstamp.l_uf = 0;

			refclock_process_offset(pp, tstamp, pp->lastrec, pp->fudgetime1);

			/*
			 * The clock will blurt a timecode every second
			 * but we only want one when polled.  If we
			 * havn't been polled, bail out.
			 */
			if (!instance->polled)
				break;
			instance->polled = 0;

			/*
			 * It's a live one!  Remember this time.
			 */

			pp->lastref = pp->lastrec;
			refclock_receive(peer);

			/*
			 * If we get here - what we got from the clock is
			 * OK, so say so
			 */
			refclock_report(peer, CEVNT_NOMINAL);

			/*
			 * We have succeeded in answering the poll.
			 * Turn off the flag and return
			 */
			instance->polled = 0;
			break;

		case JUPITER_O_GPOS:
			if (size != sizeof(struct jgpos)) {
				jupiter_debug(peer, __func__,
				    "gpos: len %d != %u",
				    size, (int)sizeof(struct jgpos));
				refclock_report(peer, CEVNT_BADREPLY);
				break;
			}

			if ((cp = jupiter_parse_gpos(instance, sp)) != NULL) {
				jupiter_debug(peer, __func__,
				    "gpos: %s", cp);
				break;
			}
			break;

		case JUPITER_O_ID:
			if (size != sizeof(struct jid)) {
				jupiter_debug(peer, __func__,
				    "id: len %d != %u",
				    size, (int)sizeof(struct jid));
				refclock_report(peer, CEVNT_BADREPLY);
				break;
			}
			/*
			 * If we got this message because the Jupiter
			 * just powered instance, it needs to be reconfigured.
			 */
			ip = (struct jid *)sp;
			jupiter_debug(peer, __func__,
			    "%s chan ver %s, %s (%s)",
			    ip->chans, ip->vers, ip->date, ip->opts);
			msyslog(LOG_DEBUG,
			    "jupiter_receive: %s chan ver %s, %s (%s)",
			    ip->chans, ip->vers, ip->date, ip->opts);
			if (instance->wantid)
				instance->wantid = 0;
			else {
				jupiter_debug(peer, __func__, "reset receiver");
				jupiter_config(instance);
				/*
				 * Restore since jupiter_config() just
				 * zeroed it
				 */
				instance->ssize = cc;
			}
			break;

		default:
			jupiter_debug(peer, __func__, "unknown message id %d",
			    getshort(hp->id));
			break;
		}
		instance->ssize -= cc;
		if (instance->ssize < 0) {
			fprintf(stderr, "jupiter_recv: negative ssize!\n");
			abort();
		} else if (instance->ssize > 0)
			memcpy(instance->sbuf, (u_char *)instance->sbuf + cc, instance->ssize);
	}
}

static const char *
jupiter_parse_t(struct instance *instance, u_short *sp)
{
	struct tm *tm;
	char *cp;
	struct jpulse *jp;
	u_int32 sweek;
	time_t last_timecode;
	u_short flags;

	jp = (struct jpulse *)sp;

	/* The timecode is presented as seconds into the current GPS week */
	sweek = DS2UI(jp->sweek) % WEEKSECS;

	/*
	 * If we don't know the current GPS week, calculate it from the
	 * current time. (It's too bad they didn't include this
	 * important value in the pulse message). We'd like to pick it
	 * up from one of the other messages like gpos or chan but they
	 * don't appear to be synchronous with time keeping and changes
	 * too soon (something like 10 seconds before the new GPS
	 * week).
	 *
	 * If we already know the current GPS week, increment it when
	 * we wrap into a new week.
	 */
	if (instance->gweek == 0) {
		if (!instance->gpos_gweek) {
			return ("jupiter_parse_t: Unknown gweek");
		}

		instance->gweek = instance->gpos_gweek;

		/*
		 * Fix warps. GPOS has GPS time and PULSE has UTC.
		 * Plus, GPOS need not be completely in synch with
		 * the PPS signal.
		 */
		if (instance->gpos_sweek >= sweek) {
			if ((instance->gpos_sweek - sweek) > WEEKSECS / 2)
				++instance->gweek;
		}
		else {
			if ((sweek - instance->gpos_sweek) > WEEKSECS / 2)
				--instance->gweek;
		}
	}
	else if (sweek == 0 && instance->lastsweek == WEEKSECS - 1) {
		++instance->gweek;
		jupiter_debug(instance->peer, __func__,
		    "NEW gps week %u", instance->gweek);
	}

	/*
	 * See if the sweek stayed the same (this happens when there is
	 * no pps pulse).
	 *
	 * Otherwise, look for time warps:
	 *
	 *   - we have stored at least one lastsweek and
	 *   - the sweek didn't increase by one and
	 *   - we didn't wrap to a new GPS week
	 *
	 * Then we warped.
	 */
	if (instance->lastsweek == sweek)
		jupiter_debug(instance->peer, __func__,
		    "gps sweek not incrementing (%d)",
		    sweek);
	else if (instance->lastsweek != 2 * WEEKSECS &&
	    instance->lastsweek + 1 != sweek &&
	    !(sweek == 0 && instance->lastsweek == WEEKSECS - 1))
		jupiter_debug(instance->peer, __func__,
		    "gps sweek jumped (was %d, now %d)",
		    instance->lastsweek, sweek);
	instance->lastsweek = sweek;

	/* This timecode describes next pulse */
	last_timecode = instance->timecode;
	instance->timecode =
	    GPS_EPOCH + (instance->gweek * WEEKSECS) + sweek;

	if (last_timecode == 0)
		/* XXX debugging */
		jupiter_debug(instance->peer, __func__,
		    "UTC <none> (gweek/sweek %u/%u)",
		    instance->gweek, sweek);
	else {
		/* XXX debugging */
		tm = gmtime(&last_timecode);
		cp = asctime(tm);

		jupiter_debug(instance->peer, __func__,
		    "UTC %.24s (gweek/sweek %u/%u)",
		    cp, instance->gweek, sweek);

		/* Billboard last_timecode (which is now the current time) */
		instance->peer->procptr->year   = tm->tm_year + 1900;
		instance->peer->procptr->day    = tm->tm_yday + 1;
		instance->peer->procptr->hour   = tm->tm_hour;
		instance->peer->procptr->minute = tm->tm_min;
		instance->peer->procptr->second = tm->tm_sec;
	}

	flags = getshort(jp->flags);

	/* Toss if not designated "valid" by the gps */
	if ((flags & JUPITER_O_PULSE_VALID) == 0) {
		refclock_report(instance->peer, CEVNT_BADTIME);
		return ("time mark not valid");
	}

	/* We better be sync'ed to UTC... */
	if ((flags & JUPITER_O_PULSE_UTC) == 0) {
		refclock_report(instance->peer, CEVNT_BADTIME);
		return ("time mark not sync'ed to UTC");
	}

	return (NULL);
}

static const char *
jupiter_parse_gpos(struct instance *instance, u_short *sp)
{
	struct jgpos *jg;
	time_t t;
	struct tm *tm;
	char *cp;

	jg = (struct jgpos *)sp;

	if (jg->navval != 0) {
		/*
		 * Solution not valid. Use caution and refuse
		 * to determine GPS week from this message.
		 */
		instance->gpos_gweek = 0;
		instance->gpos_sweek = 0;
		return ("Navigation solution not valid");
	}

	instance->gpos_sweek = DS2UI(jg->sweek);
	instance->gpos_gweek = basedate_expand_gpsweek(getshort(jg->gweek));

	/* according to the protocol spec, the seconds-in-week cannot
	 * exceed the nominal value: Is it really necessary to normalise
	 * the seconds???
	 */
	while(instance->gpos_sweek >= WEEKSECS) {
		instance->gpos_sweek -= WEEKSECS;
		++instance->gpos_gweek;
	}
	instance->gweek = 0;

	t = GPS_EPOCH + (instance->gpos_gweek * WEEKSECS) + instance->gpos_sweek;
	tm = gmtime(&t);
	cp = asctime(tm);

	jupiter_debug(instance->peer, __func__,
		"GPS %.24s (gweek/sweek %u/%u)",
		cp, instance->gpos_gweek, instance->gpos_sweek);
	return (NULL);
}

/*
 * jupiter_debug - print debug messages
 */
static void
jupiter_debug(
	struct peer *	peer,
	const char *	function,
	const char *	fmt,
	...
	)
{
	char	buffer[200];
	va_list	ap;

	va_start(ap, fmt);
	/*
	 * Print debug message to stdout
	 * In the future, we may want to get get more creative...
	 */
	mvsnprintf(buffer, sizeof(buffer), fmt, ap);
	record_clock_stats(&peer->srcadr, buffer);
#ifdef DEBUG
	if (debug) {
		printf("%s: %s\n", function, buffer);
		fflush(stdout);
	}
#endif

	va_end(ap);
}

/* Checksum and transmit a message to the Jupiter */
static char *
jupiter_send(struct instance *instance, struct jheader *hp)
{
	u_int len, size;
	ssize_t cc;
	u_short *sp;
	static char errstr[132];

	size = sizeof(*hp);
	hp->hsum = putshort(jupiter_cksum((u_short *)hp,
	    (size / sizeof(u_short)) - 1));
	len = getshort(hp->len);
	if (len > 0) {
		sp = (u_short *)(hp + 1);
		sp[len] = putshort(jupiter_cksum(sp, len));
		size += (len + 1) * sizeof(u_short);
	}

	if ((cc = write(instance->peer->procptr->io.fd, (char *)hp, size)) < 0) {
		msnprintf(errstr, sizeof(errstr), "write: %m");
		return (errstr);
	} else if (cc != (int)size) {
		snprintf(errstr, sizeof(errstr), "short write (%zd != %u)", cc, size);
		return (errstr);
	}
	return (NULL);
}

/* Request periodic message output */
static struct {
	struct jheader jheader;
	struct jrequest jrequest;
} reqmsg = {
	{ putshort(JUPITER_SYNC), 0,
	    putshort((sizeof(struct jrequest) / sizeof(u_short)) - 1),
	    0, JUPITER_FLAG_REQUEST | JUPITER_FLAG_NAK |
	    JUPITER_FLAG_CONN | JUPITER_FLAG_LOG, 0 },
	{ 0, 0, 0, 0 }
};

/* An interval of zero means to output on trigger */
static void
jupiter_reqmsg(struct instance *instance, u_int id,
    u_int interval)
{
	struct jheader *hp;
	struct jrequest *rp;
	char *cp;

	hp = &reqmsg.jheader;
	hp->id = putshort(id);
	rp = &reqmsg.jrequest;
	rp->trigger = putshort(interval == 0);
	rp->interval = putshort(interval);
	if ((cp = jupiter_send(instance, hp)) != NULL)
		jupiter_debug(instance->peer, __func__, "%u: %s", id, cp);
}

/* Cancel periodic message output */
static struct jheader canmsg = {
	putshort(JUPITER_SYNC), 0, 0, 0,
	JUPITER_FLAG_REQUEST | JUPITER_FLAG_NAK | JUPITER_FLAG_DISC,
	0
};

static void
jupiter_canmsg(struct instance *instance, u_int id)
{
	struct jheader *hp;
	char *cp;

	hp = &canmsg;
	hp->id = putshort(id);
	if ((cp = jupiter_send(instance, hp)) != NULL)
		jupiter_debug(instance->peer, __func__, "%u: %s", id, cp);
}

/* Request a single message output */
static struct jheader reqonemsg = {
	putshort(JUPITER_SYNC), 0, 0, 0,
	JUPITER_FLAG_REQUEST | JUPITER_FLAG_NAK | JUPITER_FLAG_QUERY,
	0
};

static void
jupiter_reqonemsg(struct instance *instance, u_int id)
{
	struct jheader *hp;
	char *cp;

	hp = &reqonemsg;
	hp->id = putshort(id);
	if ((cp = jupiter_send(instance, hp)) != NULL)
		jupiter_debug(instance->peer, __func__, "%u: %s", id, cp);
}

/* Set the platform dynamics */
static struct {
	struct jheader jheader;
	struct jplat jplat;
} platmsg = {
	{ putshort(JUPITER_SYNC), putshort(JUPITER_I_PLAT),
	    putshort((sizeof(struct jplat) / sizeof(u_short)) - 1), 0,
	    JUPITER_FLAG_REQUEST | JUPITER_FLAG_NAK, 0 },
	{ 0, 0, 0 }
};

static void
jupiter_platform(struct instance *instance, u_int platform)
{
	struct jheader *hp;
	struct jplat *pp;
	char *cp;

	hp = &platmsg.jheader;
	pp = &platmsg.jplat;
	pp->platform = putshort(platform);
	if ((cp = jupiter_send(instance, hp)) != NULL)
		jupiter_debug(instance->peer, __func__, "%u: %s", platform, cp);
}

/* Checksum "len" shorts */
static u_short
jupiter_cksum(u_short *sp, u_int len)
{
	u_short sum, x;

	sum = 0;
	while (len-- > 0) {
		x = *sp++;
		sum += getshort(x);
	}
	return (~sum + 1);
}

/* Return the size of the next message (or zero if we don't have it all yet) */
static int
jupiter_recv(struct instance *instance)
{
	int n, len, size, cc;
	struct jheader *hp;
	u_char *bp;
	u_short *sp;

	/* Must have at least a header's worth */
	cc = sizeof(*hp);
	size = instance->ssize;
	if (size < cc)
		return (0);

	/* Search for the sync short if missing */
	sp = instance->sbuf;
	hp = (struct jheader *)sp;
	if (getshort(hp->sync) != JUPITER_SYNC) {
		/* Wasn't at the front, sync up */
		jupiter_debug(instance->peer, __func__, "syncing");
		bp = (u_char *)sp;
		n = size;
		while (n >= 2) {
			if (bp[0] != (JUPITER_SYNC & 0xff)) {
				/*
				jupiter_debug(instance->peer, __func__,
				    "{0x%x}", bp[0]);
				*/
				++bp;
				--n;
				continue;
			}
			if (bp[1] == ((JUPITER_SYNC >> 8) & 0xff))
				break;
			/*
			jupiter_debug(instance->peer, __func__,
			    "{0x%x 0x%x}", bp[0], bp[1]);
			*/
			bp += 2;
			n -= 2;
		}
		/*
		jupiter_debug(instance->peer, __func__, "\n");
		*/
		/* Shuffle data to front of input buffer */
		if (n > 0)
			memcpy(sp, bp, n);
		size = n;
		instance->ssize = size;
		if (size < cc || hp->sync != JUPITER_SYNC)
			return (0);
	}

	if (jupiter_cksum(sp, (cc / sizeof(u_short) - 1)) !=
	    getshort(hp->hsum)) {
	    jupiter_debug(instance->peer, __func__, "bad header checksum!");
		/* This is drastic but checksum errors should be rare */
		instance->ssize = 0;
		return (0);
	}

	/* Check for a payload */
	len = getshort(hp->len);
	if (len > 0) {
		n = (len + 1) * sizeof(u_short);
		/* Not enough data yet */
		if (size < cc + n)
			return (0);

		/* Check payload checksum */
		sp = (u_short *)(hp + 1);
		if (jupiter_cksum(sp, len) != getshort(sp[len])) {
			jupiter_debug(instance->peer,
			    __func__, "bad payload checksum!");
			/* This is drastic but checksum errors should be rare */
			instance->ssize = 0;
			return (0);
		}
		cc += n;
	}
	return (cc);
}

#else /* not (REFCLOCK && CLOCK_JUPITER && HAVE_PPSAPI) */
int refclock_jupiter_bs;
#endif /* not (REFCLOCK && CLOCK_JUPITER && HAVE_PPSAPI) */
