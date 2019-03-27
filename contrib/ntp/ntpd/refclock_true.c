/*
 * refclock_true - clock driver for the Kinemetrics/TrueTime receivers
 *	Receiver Version 3.0C - tested plain, with CLKLDISC
 *	Development work being done:
 *      - Support TL-3 WWV TOD receiver
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_TRUETIME)

#include <stdio.h>
#include <ctype.h>

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"

#ifdef SYS_WINNT
extern int async_write(int, const void *, unsigned int);
#undef write
#define write(fd, data, octets)	async_write(fd, data, octets)
#endif

/* This should be an atom clock but those are very hard to build.
 *
 * The PCL720 from P C Labs has an Intel 8253 lookalike, as well as a bunch
 * of TTL input and output pins, all brought out to the back panel.  If you
 * wire a PPS signal (such as the TTL PPS coming out of a GOES or other
 * Kinemetrics/Truetime clock) to the 8253's GATE0, and then also wire the
 * 8253's OUT0 to the PCL720's INPUT3.BIT0, then we can read CTR0 to get the
 * number of uSecs since the last PPS upward swing, mediated by reading OUT0
 * to find out if the counter has wrapped around (this happens if more than
 * 65535us (65ms) elapses between the PPS event and our being called.)
 */
#ifdef CLOCK_PPS720
# undef min	/* XXX */
# undef max	/* XXX */
# include <machine/inline.h>
# include <sys/pcl720.h>
# include <sys/i8253.h>
# define PCL720_IOB 0x2a0	/* XXX */
# define PCL720_CTR 0		/* XXX */
#endif

/*
 * Support for Kinemetrics Truetime Receivers
 *	GOES:           (468-DC, usable with GPS->GOES converting antenna)
 *	GPS/TM-TMD:	
 *	XL-DC:		(a 151-602-210, reported by the driver as a GPS/TM-TMD)
 *	GPS-800 TCU:	(an 805-957 with the RS232 Talker/Listener module)
 *      TL-3:           3 channel WWV/H receiver w/ IRIG and RS-232 outputs
 *	OM-DC:		getting stale ("OMEGA")
 *
 * Most of this code is originally from refclock_wwvb.c with thanks.
 * It has been so mangled that wwvb is not a recognizable ancestor.
 *
 * Timcode format: ADDD:HH:MM:SSQCL
 *	A - control A		(this is stripped before we see it)
 *	Q - Quality indication	(see below)
 *	C - Carriage return
 *	L - Line feed
 *
 * Quality codes indicate possible error of
 *   468-DC GOES Receiver:
 *   GPS-TM/TMD Receiver: (default quality codes for XL-DC)
 *       ?     +/- 1  milliseconds	#     +/- 100 microseconds
 *       *     +/- 10 microseconds	.     +/- 1   microsecond
 *     space   less than 1 microsecond
 *   TL-3 Receiver: (default quality codes for TL-3)
 *       ?     unknown quality (receiver is unlocked)
 *     space   +/- 5 milliseconds
 *   OM-DC OMEGA Receiver: (default quality codes for OMEGA)
 *   WARNING OMEGA navigation system is no longer existent
 *       >     >+- 5 seconds
 *       ?     >+/- 500 milliseconds    #     >+/- 50 milliseconds
 *       *     >+/- 5 milliseconds      .     >+/- 1 millisecond
 *      A-H    less than 1 millisecond.  Character indicates which station
 *	       is being received as follows:
 *	       A = Norway, B = Liberia, C = Hawaii, D = North Dakota,
 *	       E = La Reunion, F = Argentina, G = Australia, H = Japan.
 *
 * The carriage return start bit begins on 0 seconds and extends to 1 bit time.
 *
 * Notes on 468-DC and OMEGA receiver:
 *
 * Send the clock a 'R' or 'C' and once per second a timestamp will
 * appear.  Send a 'P' to get the satellite position once (GOES only.)
 *
 * Notes on the 468-DC receiver:
 *
 * Since the old east/west satellite locations are only historical, you can't
 * set your clock propagation delay settings correctly and still use
 * automatic mode. The manual says to use a compromise when setting the
 * switches. This results in significant errors. The solution; use fudge
 * time1 and time2 to incorporate corrections. If your clock is set for
 * 50 and it should be 58 for using the west and 46 for using the east,
 * use the line
 *
 * fudge 127.127.5.0 time1 +0.008 time2 -0.004
 *
 * This corrects the 4 milliseconds advance and 8 milliseconds retard
 * needed. The software will ask the clock which satellite it sees.
 *
 * Notes on the TrueTime TimeLink TL-3 WWV TOD receiver:
 * 
 * This clock may be polled, or send one timecode per second.
 * That mode may be toggled via the front panel ("C" mode), or controlled
 * from the RS-232 port.  Send the receiver "ST1" to turn it on, and
 * "ST0" to turn it off.  Send "QV" to get the firmware revision (useful
 * for identifying this model.)
 * 
 * Note that it can take several polling cycles, especially if the receiver
 * was in the continuous timecode mode.  (It can be slow to leave that mode.)
 * 
 * ntp.conf parameters:
 * time1   - offset applied to samples when reading WEST satellite (default = 0)
 * time2   - offset applied to samples when reading EAST satellite (default = 0)
 * stratum - stratum to assign to this clock (default = 0)
 * refid   - refid assigned to this clock (default = "TRUE", see below)
 * flag1   - will silence the clock side of ntpd, just reading the clock
 *	     without trying to write to it.  (default = 0)
 * flag2   - generate a debug file /tmp/true%d.
 * flag3   - enable ppsclock streams module
 * flag4   - use the PCL-720 (BSD/OS only)
 */


/*
 * Definitions
 */
#define	DEVICE		"/dev/true%d"
#define	SPEED232	B9600	/* 9600 baud */

/*
 * Radio interface parameters
 */
#define	PRECISION	(-10)	/* precision assumed (about 1 ms) */
#define	REFID		"TRUE"	/* reference id */
#define	DESCRIPTION	"Kinemetrics/TrueTime Receiver"

/*
 * Tags which station (satellite) we see
 */
#define GOES_WEST	0	/* Default to WEST satellite and apply time1 */
#define GOES_EAST	1	/* until you discover otherwise */

/*
 * used by the state machine
 */
enum true_event	{e_Init, e_Huh, e_F18, e_F50, e_F51, e_Satellite,
		 e_TL3, e_Poll, e_Location, e_TS, e_Max};
const char *events[] = {"Init", "Huh", "F18", "F50", "F51", "Satellite",
			"TL3", "Poll", "Location", "TS"};
#define eventStr(x) (((int)x<(int)e_Max) ? events[(int)x] : "?")

enum true_state	{s_Base, s_InqTM, s_InqTCU, s_InqOmega, s_InqGOES,
		 s_InqTL3, s_Init, s_F18, s_F50, s_Start, s_Auto, s_Max};
const char *states[] = {"Base", "InqTM", "InqTCU", "InqOmega", "InqGOES",
			"InqTL3", "Init", "F18", "F50", "Start", "Auto"};
#define stateStr(x) (((int)x<(int)s_Max) ? states[(int)x] : "?")

enum true_type	{t_unknown, t_goes, t_tm, t_tcu, t_omega, t_tl3, t_Max};
const char *types[] = {"unknown", "goes", "tm", "tcu", "omega", "tl3"};
#define typeStr(x) (((int)x<(int)t_Max) ? types[(int)x] : "?")

/*
 * unit control structure
 */
struct true_unit {
	unsigned int	pollcnt;	/* poll message counter */
	unsigned int	station;	/* which station we are on */
	unsigned int	polled;		/* Hand in a time sample? */
	enum true_state	state;		/* state machine */
	enum true_type	type;		/* what kind of clock is it? */
	int		unit;		/* save an extra copy of this */
	FILE		*debug;		/* debug logging file */
#ifdef CLOCK_PPS720
	int		pcl720init;	/* init flag for PCL 720 */
#endif
};

/*
 * Function prototypes
 */
static	int	true_start	(int, struct peer *);
static	void	true_shutdown	(int, struct peer *);
static	void	true_receive	(struct recvbuf *);
static	void	true_poll	(int, struct peer *);
static	void	true_send	(struct peer *, const char *);
static	void	true_doevent	(struct peer *, enum true_event);

#ifdef CLOCK_PPS720
static	u_long	true_sample720	(void);
#endif

/*
 * Transfer vector
 */
struct	refclock refclock_true = {
	true_start,		/* start up driver */
	true_shutdown,		/* shut down driver */
	true_poll,		/* transmit poll message */
	noentry,		/* not used (old true_control) */
	noentry,		/* initialize driver (not used) */
	noentry,		/* not used (old true_buginfo) */
	NOFLAGS			/* not used */
};


#if !defined(__STDC__)
# define true_debug (void)
#else
NTP_PRINTF(2, 3)
static void
true_debug(struct peer *peer, const char *fmt, ...)
{
	va_list ap;
	int want_debugging, now_debugging;
	struct refclockproc *pp;
	struct true_unit *up;

	va_start(ap, fmt);
	pp = peer->procptr;
	up = pp->unitptr;

	want_debugging = (pp->sloppyclockflag & CLK_FLAG2) != 0;
	now_debugging = (up->debug != NULL);
	if (want_debugging != now_debugging)
	{
		if (want_debugging) {
			char filename[40];
			int fd;

			snprintf(filename, sizeof(filename),
				 "/tmp/true%d.debug", up->unit);
			fd = open(filename, O_CREAT | O_WRONLY | O_EXCL,
				  0600);
			if (fd >= 0 && (up->debug = fdopen(fd, "w"))) {
#ifdef HAVE_SETVBUF
				static char buf[BUFSIZ];

				setvbuf(up->debug, buf, _IOLBF, BUFSIZ);
#else
				setlinebuf(up->debug);
#endif
			}
		} else {
			fclose(up->debug);
			up->debug = NULL;
		}
	}

	if (up->debug) {
		fprintf(up->debug, "true%d: ", up->unit);
		vfprintf(up->debug, fmt, ap);
	}
	va_end(ap);
}
#endif /*STDC*/

/*
 * true_start - open the devices and initialize data for processing
 */
static int
true_start(
	int unit,
	struct peer *peer
	)
{
	register struct true_unit *up;
	struct refclockproc *pp;
	char device[40];
	int fd;

	/*
	 * Open serial port
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	fd = refclock_open(device, SPEED232, LDISC_CLK);
	if (fd <= 0)
		return 0;

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));
	pp = peer->procptr;
	pp->io.clock_recv = true_receive;
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
	memcpy(&pp->refid, REFID, 4);
	up->pollcnt = 2;
	up->type = t_unknown;
	up->state = s_Base;

	/*
	 * Send a CTRL-C character at the start,
	 * just in case the clock is already
	 * sending timecodes
	 */
	true_send(peer, "\03\r");
	
	true_doevent(peer, e_Init);

	return (1);
}


/*
 * true_shutdown - shut down the clock
 */
static void
true_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct true_unit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	if (pp->io.fd != -1)
		io_closeclock(&pp->io);
	if (up != NULL)
		free(up);
}


/*
 * true_receive - receive data from the serial interface on a clock
 */
static void
true_receive(
	struct recvbuf *rbufp
	)
{
	register struct true_unit *up;
	struct refclockproc *pp;
	struct peer *peer;
	u_short new_station;
	char synced;
	int i;
	int lat, lon, off;	/* GOES Satellite position */
	/* These variables hold data until we decide to keep it */
	char	rd_lastcode[BMAX];
	l_fp	rd_tmp;
	u_short	rd_lencode;

	/*
	 * Get the clock this applies to and pointers to the data.
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Read clock output.  Automatically handles STREAMS, CLKLDISC.
	 */
	rd_lencode = refclock_gtlin(rbufp, rd_lastcode, BMAX, &rd_tmp);
	rd_lastcode[rd_lencode] = '\0';

	/*
	 * There is a case where <cr><lf> generates 2 timestamps.
	 */
	if (rd_lencode == 0)
		return;
	pp->lencode = rd_lencode;
	strlcpy(pp->a_lastcode, rd_lastcode, sizeof(pp->a_lastcode));
	pp->lastrec = rd_tmp;
	true_debug(peer, "receive(%s) [%d]\n", pp->a_lastcode,
		   pp->lencode);

	up->pollcnt = 2;
	record_clock_stats(&peer->srcadr, pp->a_lastcode);

	/*
	 * We get down to business, check the timecode format and decode
	 * its contents. This code decodes a multitude of different
	 * clock messages. Timecodes are processed if needed. All replies
	 * will be run through the state machine to tweak driver options
	 * and program the clock.
	 */

	/*
	 * Clock misunderstood our last command?
	 */
	if (pp->a_lastcode[0] == '?' ||
	    strcmp(pp->a_lastcode, "ERROR 05 NO SUCH FUNCTION") == 0) {
		true_doevent(peer, e_Huh);
		return;
	}

	/*
	 * Timecode: "nnnnn+nnn-nnn"
	 * (from GOES clock when asked about satellite position)
	 */
	if ((pp->a_lastcode[5] == '+' || pp->a_lastcode[5] == '-') &&
	    (pp->a_lastcode[9] == '+' || pp->a_lastcode[9] == '-') &&
	    sscanf(pp->a_lastcode, "%5d%*c%3d%*c%3d", &lon, &lat, &off) == 3
	    ) {
		const char *label = "Botch!";

		/*
		 * This is less than perfect.  Call the (satellite)
		 * either EAST or WEST and adjust slop accodingly
		 * Perfectionists would recalculate the exact delay
		 * and adjust accordingly...
		 */
		if (lon > 7000 && lon < 14000) {
			if (lon < 10000) {
				new_station = GOES_EAST;
				label = "EAST";
			} else {
				new_station = GOES_WEST;
				label = "WEST";
			}
				
			if (new_station != up->station) {
				double dtemp;

				dtemp = pp->fudgetime1;
				pp->fudgetime1 = pp->fudgetime2;
				pp->fudgetime2 = dtemp;
				up->station = new_station;
			}
		}
		else {
			/*refclock_report(peer, CEVNT_BADREPLY);*/
			label = "UNKNOWN";
		}
		true_debug(peer, "GOES: station %s\n", label);
		true_doevent(peer, e_Satellite);
		return;
	}

	/*
	 * Timecode: "Fnn"
	 * (from TM/TMD clock when it wants to tell us what it's up to.)
	 */
	if (sscanf(pp->a_lastcode, "F%2d", &i) == 1 && i > 0 && i < 80) {
		switch (i) {
		case 50:
			true_doevent(peer, e_F50);
			break;
		case 51:
			true_doevent(peer, e_F51);
			break;
		default:
			true_debug(peer, "got F%02d - ignoring\n", i);
			break;
		}
		return;
	}

        /*
         * Timecode: "VER xx.xx"
         * (from a TL3 when sent "QV", so id's it during initialization.)
         */
        if (pp->a_lastcode[0] == 'V' && pp->a_lastcode[1] == 'E' &&
            pp->a_lastcode[2] == 'R' && pp->a_lastcode[6] == '.') {
                true_doevent(peer, e_TL3);
                NLOG(NLOG_CLOCKSTATUS) {
                        msyslog(LOG_INFO, "TL3: %s", pp->a_lastcode);
                }
                return;
        }

	/*
	 * Timecode: " TRUETIME Mk III" or " TRUETIME XL"
	 * (from a TM/TMD/XL clock during initialization.)
	 */
	if (strncmp(pp->a_lastcode, " TRUETIME Mk III ", 17) == 0 ||
	    strncmp(pp->a_lastcode, " TRUETIME XL", 12) == 0) {
		true_doevent(peer, e_F18);
		NLOG(NLOG_CLOCKSTATUS) {
			msyslog(LOG_INFO, "TM/TMD/XL: %s", pp->a_lastcode);
		}
		return;
	}

	/*
	 * Timecode: "N03726428W12209421+000033"
	 *			1	   2
	 * index      0123456789012345678901234
	 * (from a TCU during initialization)
	 */
	if ((pp->a_lastcode[0] == 'N' || pp->a_lastcode[0] == 'S') &&
	    (pp->a_lastcode[9] == 'W' || pp->a_lastcode[9] == 'E') &&
	    pp->a_lastcode[18] == '+') {
		true_doevent(peer, e_Location);
		NLOG(NLOG_CLOCKSTATUS) {
			msyslog(LOG_INFO, "TCU-800: %s", pp->a_lastcode);
		}
		return;
	}
	/*
	 * Timecode: "ddd:hh:mm:ssQ"
	 *			1	   2
	 * index      0123456789012345678901234
	 * (from all clocks supported by this driver.)
	 */
	if (pp->a_lastcode[3] == ':' &&
	    pp->a_lastcode[6] == ':' &&
	    pp->a_lastcode[9] == ':' &&
	    sscanf(pp->a_lastcode, "%3d:%2d:%2d:%2d%c",
		   &pp->day, &pp->hour, &pp->minute,
		   &pp->second, &synced) == 5) {

		/*
		 * Adjust the synchronize indicator according to timecode
		 * say were OK, and then say not if we really are not OK
		 */
		if (synced == '>' || synced == '#' || synced == '?'
		    || synced == 'X')
			pp->leap = LEAP_NOTINSYNC;
		else
			pp->leap = LEAP_NOWARNING;

		true_doevent(peer, e_TS);

#ifdef CLOCK_PPS720
		/* If it's taken more than 65ms to get here, we'll lose. */
		if ((pp->sloppyclockflag & CLK_FLAG4) && up->pcl720init) {
			l_fp   off;

#ifdef CLOCK_ATOM
			/*
			 * find out what time it really is. Include
			 * the count from the PCL720
			 */
			if (!clocktime(pp->day, pp->hour, pp->minute, 
				       pp->second, GMT, pp->lastrec.l_ui, 
				       &pp->yearstart, &off.l_ui)) {
				refclock_report(peer, CEVNT_BADTIME);
				return;
			}
			off.l_uf = 0;
#endif

			pp->usec = true_sample720();
#ifdef CLOCK_ATOM
			TVUTOTSF(pp->usec, off.l_uf);
#endif

			/*
			 * Stomp all over the timestamp that was pulled out
			 * of the input stream. It's irrelevant since we've
			 * adjusted the input time to reflect now (via pp->usec)
			 * rather than when the data was collected.
			 */
			get_systime(&pp->lastrec);
#ifdef CLOCK_ATOM
			/*
			 * Create a true offset for feeding to pps_sample()
			 */
			L_SUB(&off, &pp->lastrec);

			pps_sample(peer, &off);
#endif
			true_debug(peer, "true_sample720: %luus\n", pp->usec);
		}
#endif

		/*
		 * The clock will blurt a timecode every second but we only
		 * want one when polled.  If we havn't been polled, bail out.
		 */
		if (!up->polled)
			return;

                /* We only call doevent if additional things need be done
                 * at poll interval.  Currently, its only for GOES.  We also
                 * call it for clock unknown so that it gets logged.
                 */
                if (up->type == t_goes || up->type == t_unknown)
                    true_doevent(peer, e_Poll);

		if (!refclock_process(pp)) {
			refclock_report(peer, CEVNT_BADTIME);
			return;
		}
		/*
		 * If clock is good we send a NOMINAL message so that
		 * any previous BAD messages are nullified
		 */
		pp->lastref = pp->lastrec;
		refclock_receive(peer);
		refclock_report(peer, CEVNT_NOMINAL);

		/*
		 * We have succedded in answering the poll.
		 * Turn off the flag and return
		 */
		up->polled = 0;

		return;
	}

	/*
	 * No match to known timecodes, report failure and return
	 */
	refclock_report(peer, CEVNT_BADREPLY);
	return;
}


/*
 * true_send - time to send the clock a signal to cough up a time sample
 */
static void
true_send(
	struct peer *peer,
	const char *cmd
	)
{
	struct refclockproc *pp;

	pp = peer->procptr;
	if (!(pp->sloppyclockflag & CLK_FLAG1)) {
		size_t len = strlen(cmd);

		true_debug(peer, "Send '%s'\n", cmd);
		if (write(pp->io.fd, cmd, len) != (ssize_t)len)
			refclock_report(peer, CEVNT_FAULT);
		else
			pp->polls++;
	}
}


/*
 * state machine for initializing and controlling a clock
 */
static void
true_doevent(
	struct peer *peer,
	enum true_event event
	)
{
	struct true_unit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	if (event != e_TS) {
		NLOG(NLOG_CLOCKSTATUS) {
			msyslog(LOG_INFO, "TRUE: clock %s, state %s, event %s",
				typeStr(up->type),
				stateStr(up->state),
				eventStr(event));
		}
	}
	true_debug(peer, "clock %s, state %s, event %s\n",
		   typeStr(up->type), stateStr(up->state), eventStr(event));
	switch (up->type) {
	case t_goes:
		switch (event) {
		case e_Init:	/* FALLTHROUGH */
		case e_Satellite:
			/*
			 * Switch back to on-second time codes and return.
			 */
			true_send(peer, "C");
			up->state = s_Start;
			break;
		case e_Poll:
			/*
			 * After each poll, check the station (satellite).
			 */
			true_send(peer, "P");
			/* No state change needed. */
			break;
		default:
			break;
		}
		/* FALLTHROUGH */
	case t_omega:
		switch (event) {
		case e_Init:
			true_send(peer, "C");
			up->state = s_Start;
			break;
		case e_TS:
			if (up->state != s_Start && up->state != s_Auto) {
				true_send(peer, "\03\r");
				break;
			}
			up->state = s_Auto;
			break;
		default:
			break;
		}
		break;
	case t_tm:
		switch (event) {
		case e_Init:
			true_send(peer, "F18\r");
			up->state = s_Init;
			break;
		case e_F18:
			true_send(peer, "F50\r");
                        /*
                         * Timecode: " TRUETIME Mk III" or " TRUETIME XL"
                         * (from a TM/TMD/XL clock during initialization.)
                         */
                        if ( strcmp(pp->a_lastcode, " TRUETIME Mk III") == 0 ||
                            strncmp(pp->a_lastcode, " TRUETIME XL", 12) == 0) {
                                true_doevent(peer, e_F18);
                                NLOG(NLOG_CLOCKSTATUS) {
                                    msyslog(LOG_INFO, "TM/TMD/XL: %s", 
                                            pp->a_lastcode);
                                }
                                return;
                        }
			up->state = s_F18;
			break;
		case e_F50:
			true_send(peer, "F51\r");
			up->state = s_F50;
			break;
		case e_F51:
			true_send(peer, "F08\r");
			up->state = s_Start;
			break;
		case e_TS:
			if (up->state != s_Start && up->state != s_Auto) {
				true_send(peer, "\03\r");
				break;
			}
			up->state = s_Auto;
			break;
		default:
			break;
		}
		break;
	case t_tcu:
		switch (event) {
		case e_Init:
			true_send(peer, "MD3\r");	/* GPS Synch'd Gen. */
			true_send(peer, "TSU\r");	/* UTC, not GPS. */
			true_send(peer, "AU\r");	/* Auto Timestamps. */
			up->state = s_Start;
			break;
		case e_TS:
			if (up->state != s_Start && up->state != s_Auto) {
				true_send(peer, "\03\r");
				break;
			}
			up->state = s_Auto;
			break;
		default:
			break;
		}
		break;
	case t_tl3:
                switch (event) {
                    case e_Init:
                        true_send(peer, "ST1"); /* Turn on continuous stream */
                        break;
                    case e_TS:
                        up->state = s_Auto;
                        break;
                    default:
                        break;
                }
                break;
	case t_unknown:
               if (event == e_Poll)
                   break;
		switch (up->state) {
		case s_Base:
			if (event != e_Init)
			    abort();
			true_send(peer, "P\r");
			up->state = s_InqGOES;
			break;
		case s_InqGOES:
			switch (event) {
			case e_Satellite:
				up->type = t_goes;
				true_doevent(peer, e_Init);
				break;
			case e_Init:	/*FALLTHROUGH*/
			case e_Huh:
			case e_TS:
                                true_send(peer, "ST0"); /* turn off TL3 auto */
                                sleep(1);               /* wait for it */
                                up->state = s_InqTL3;
                                true_send(peer, "QV");  /* see if its a TL3 */
                                break;
                            default:
                                abort();
                        }
                        break;
                    case s_InqTL3:
                        switch (event) {
                            case e_TL3:
                                up->type = t_tl3;
                                up->state = s_Auto;     /* Inq side-effect. */
                                true_send(peer, "ST1"); /* Turn on 1/sec data */
                                break;
                            case e_Init:        /*FALLTHROUGH*/
                            case e_Huh:
				up->state = s_InqOmega;
				true_send(peer, "C\r");
				break;
                            case e_TS:
                                 up->type = t_tl3;    /* Already sending data */
                                 up->state = s_Auto;
                                 break;
			    default:
                                msyslog(LOG_INFO, 
                                        "TRUE: TL3 init fellthrough! (%d)", event);
                                break; 
			}
			break;
		case s_InqOmega:
			switch (event) {
			case e_TS:
				up->type = t_omega;
				up->state = s_Auto;	/* Inq side-effect. */
				break;
			case e_Init:	/*FALLTHROUGH*/
			case e_Huh:
				up->state = s_InqTM;
				true_send(peer, "F18\r");
				break;
			default:
				abort();
			}
			break;
		case s_InqTM:
			switch (event) {
			case e_F18:
				up->type = t_tm;
				true_doevent(peer, e_Init);
				break;
			case e_Init:	/*FALLTHROUGH*/
			case e_Huh:
				true_send(peer, "PO\r");
				up->state = s_InqTCU;
				break;
			default:
                                msyslog(LOG_INFO, 
                                        "TRUE: TM/TMD init fellthrough!");
			        break;
			}
			break;
		case s_InqTCU:
			switch (event) {
			case e_Location:
				up->type = t_tcu;
				true_doevent(peer, e_Init);
				break;
			case e_Init:	/*FALLTHROUGH*/
			case e_Huh:
				up->state = s_Base;
				sleep(1);	/* XXX */
				break;
			default:
                                msyslog(LOG_INFO, 
                                        "TRUE: TCU init fellthrough!");
                                break;
			}
			break;
			/*
			 * An expedient hack to prevent lint complaints,
			 * these don't actually need to be used here...
			 */
		case s_Init:
		case s_F18:
		case s_F50:
		case s_Start:
		case s_Auto:
		case s_Max:
			msyslog(LOG_INFO, "TRUE: state %s is unexpected!",
				stateStr(up->state));
		}
		break;
	default:
                msyslog(LOG_INFO, "TRUE: cannot identify refclock!");
		abort();    
		/* NOTREACHED */
	}

#ifdef CLOCK_PPS720
	if ((pp->sloppyclockflag & CLK_FLAG4) && !up->pcl720init) {
		/* Make counter trigger on gate0, count down from 65535. */
		pcl720_load(PCL720_IOB, PCL720_CTR, i8253_oneshot, 65535);
		/*
		 * (These constants are OK since
		 * they represent hardware maximums.)
		 */
		NLOG(NLOG_CLOCKINFO) {
			msyslog(LOG_NOTICE, "PCL-720 initialized");
		}
		up->pcl720init++;
	}
#endif


}

/*
 * true_poll - called by the transmit procedure
 */
static void
true_poll(
	int unit,
	struct peer *peer
	)
{
	struct true_unit *up;
	struct refclockproc *pp;

	/*
	 * You don't need to poll this clock.  It puts out timecodes
	 * once per second.  If asked for a timestamp, take note.
	 * The next time a timecode comes in, it will be fed back.
	 */
	pp = peer->procptr;
	up = pp->unitptr;
	if (up->pollcnt > 0) {
		up->pollcnt--;
	} else {
		true_doevent(peer, e_Init);
		refclock_report(peer, CEVNT_TIMEOUT);
	}

	/*
	 * polled every 64 seconds. Ask true_receive to hand in a
	 * timestamp.
	 */
	up->polled = 1;
	pp->polls++;
}

#ifdef CLOCK_PPS720
/*
 * true_sample720 - sample the PCL-720
 */
static u_long
true_sample720(void)
{
	unsigned long f;

	/* We wire the PCL-720's 8253.OUT0 to bit 0 of connector 3.
	 * If it is not being held low now, we did not get called
	 * within 65535us.
	 */
	if (inb(pcl720_data_16_23(PCL720_IOB)) & 0x01) {
		NLOG(NLOG_CLOCKINFO) {
			msyslog(LOG_NOTICE, "PCL-720 out of synch");
		}
		return (0);
	}
	f = (65536 - pcl720_read(PCL720_IOB, PCL720_CTR));
#ifdef PPS720_DEBUG
	msyslog(LOG_DEBUG, "PCL-720: %luus", f);
#endif
	return (f);
}
#endif

#else
int refclock_true_bs;
#endif /* REFCLOCK */
