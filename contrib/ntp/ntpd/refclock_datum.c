/*
** refclock_datum - clock driver for the Datum Programmable Time Server
**
** Important note: This driver assumes that you have termios. If you have
** a system that does not have termios, you will have to modify this driver.
**
** Sorry, I have only tested this driver on SUN and HP platforms.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntp_types.h"

#if defined(REFCLOCK) && defined(CLOCK_DATUM)

/*
** Include Files
*/

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_tty.h"
#include "ntp_refclock.h"
#include "timevalops.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

#if defined(STREAM)
#include <stropts.h>
#endif /* STREAM */

#include "ntp_stdlib.h"

/*
** This driver supports the Datum Programmable Time System (PTS) clock.
** The clock works in very straight forward manner. When it receives a
** time code request (e.g., the ascii string "//k/mn"), it responds with
** a seven byte BCD time code. This clock only responds with a
** time code after it first receives the "//k/mn" message. It does not
** periodically send time codes back at some rate once it is started.
** the returned time code can be broken down into the following fields.
**
**            _______________________________
** Bit Index | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
**            ===============================
** byte 0:   | -   -   -   - |      H D      |
**            ===============================
** byte 1:   |      T D      |      U D      |
**            ===============================
** byte 2:   | -   - |  T H  |      U H      |
**            ===============================
** byte 3:   | - |    T M    |      U M      |
**            ===============================
** byte 4:   | - |    T S    |      U S      |
**            ===============================
** byte 5:   |      t S      |      h S      |
**            ===============================
** byte 6:   |      m S      | -   -   -   - |
**            ===============================
**
** In the table above:
**
**	"-" means don't care
**	"H D", "T D", and "U D" means Hundreds, Tens, and Units of Days
**	"T H", and "UH" means Tens and Units of Hours
**	"T M", and "U M" means Tens and Units of Minutes
**	"T S", and "U S" means Tens and Units of Seconds
**	"t S", "h S", and "m S" means tenths, hundredths, and thousandths
**				of seconds
**
** The Datum PTS communicates throught the RS232 port on your machine.
** Right now, it assumes that you have termios. This driver has been tested
** on SUN and HP workstations. The Datum PTS supports various IRIG and
** NASA input codes. This driver assumes that the name of the device is
** /dev/datum. You will need to make a soft link to your RS232 device or
** create a new driver to use this refclock.
*/

/*
** Datum PTS defines
*/

/*
** Note that if GMT is defined, then the Datum PTS must use Greenwich
** time. Otherwise, this driver allows the Datum PTS to use the current
** wall clock for its time. It determines the time zone offset by minimizing
** the error after trying several time zone offsets. If the Datum PTS
** time is Greenwich time and GMT is not defined, everything should still
** work since the time zone will be found to be 0. What this really means
** is that your system time (at least to start with) must be within the
** correct time by less than +- 30 minutes. The default is for GMT to not
** defined. If you really want to force GMT without the funny +- 30 minute
** stuff then you must define (uncomment) GMT below.
*/

/*
#define GMT
#define DEBUG_DATUM_PTC
#define LOG_TIME_ERRORS
*/


#define	PRECISION	(-10)		/* precision assumed 1/1024 ms */
#define	REFID "DATM"			/* reference id */
#define DATUM_DISPERSION 0		/* fixed dispersion = 0 ms */
#define DATUM_MAX_ERROR 0.100		/* limits on sigma squared */
#define DATUM_DEV	"/dev/datum"	/* device name */

#define DATUM_MAX_ERROR2 (DATUM_MAX_ERROR*DATUM_MAX_ERROR)

/*
** The Datum PTS structure
*/

/*
** I don't use a fixed array of MAXUNITS like everyone else just because
** I don't like to program that way. Sorry if this bothers anyone. I assume
** that you can use any id for your unit and I will search for it in a
** dynamic array of units until I find it. I was worried that users might
** enter a bad id in their configuration file (larger than MAXUNITS) and
** besides, it is just cleaner not to have to assume that you have a fixed
** number of anything in a program.
*/

struct datum_pts_unit {
	struct peer *peer;		/* peer used by ntp */
	int PTS_fd;			/* file descriptor for PTS */
	u_int unit;			/* id for unit */
	u_long timestarted;		/* time started */
	l_fp lastrec;			/* time tag for the receive time (system) */
	l_fp lastref;			/* reference time (Datum time) */
	u_long yearstart;		/* the year that this clock started */
	int coderecv;			/* number of time codes received */
	int day;			/* day */
	int hour;			/* hour */
	int minute;			/* minutes */
	int second;			/* seconds */
	int msec;			/* miliseconds */
	int usec;			/* miliseconds */
	u_char leap;			/* funny leap character code */
	char retbuf[8];		/* returned time from the datum pts */
	char nbytes;			/* number of bytes received from datum pts */ 
	double sigma2;		/* average squared error (roughly) */
	int tzoff;			/* time zone offest from GMT */
};

/*
** PTS static constant variables for internal use
*/

static char TIME_REQUEST[6];	/* request message sent to datum for time */
static int nunits;		/* number of active units */

/*
** Callback function prototypes that ntpd needs to know about.
*/

static	int	datum_pts_start		(int, struct peer *);
static	void	datum_pts_shutdown	(int, struct peer *);
static	void	datum_pts_poll		(int, struct peer *);
static	void	datum_pts_control	(int, const struct refclockstat *,
					 struct refclockstat *, struct peer *);
static	void	datum_pts_init		(void);
static	void	datum_pts_buginfo	(int, struct refclockbug *, struct peer *);

/*
** This is the call back function structure that ntpd actually uses for
** this refclock.
*/

struct	refclock refclock_datum = {
	datum_pts_start,		/* start up a new Datum refclock */
	datum_pts_shutdown,		/* shutdown a Datum refclock */
	datum_pts_poll,		/* sends out the time request */
	datum_pts_control,		/* not used */
	datum_pts_init,		/* initialization (called first) */
	datum_pts_buginfo,		/* not used */
	NOFLAGS			/* we are not setting any special flags */
};

/*
** The datum_pts_receive callback function is handled differently from the
** rest. It is passed to the ntpd io data structure. Basically, every
** 64 seconds, the datum_pts_poll() routine is called. It sends out the time
** request message to the Datum Programmable Time System. Then, ntpd
** waits on a select() call to receive data back. The datum_pts_receive()
** function is called as data comes back. We expect a seven byte time
** code to be returned but the datum_pts_receive() function may only get
** a few bytes passed to it at a time. In other words, this routine may
** get called by the io stuff in ntpd a few times before we get all seven
** bytes. Once the last byte is received, we process it and then pass the
** new time measurement to ntpd for updating the system time. For now,
** there is no 3 state filtering done on the time measurements. The
** jitter may be a little high but at least for its current use, it is not
** a problem. We have tried to keep things as simple as possible. This
** clock should not jitter more than 1 or 2 mseconds at the most once
** things settle down. It is important to get the right drift calibrated
** in the ntpd.drift file as well as getting the right tick set up right
** using tickadj for SUNs. Tickadj is not used for the HP but you need to
** remember to bring up the adjtime daemon because HP does not support
** the adjtime() call.
*/

static	void	datum_pts_receive	(struct recvbuf *);

/*......................................................................*/
/*	datum_pts_start - start up the datum PTS. This means open the	*/
/*	RS232 device and set up the data structure for my unit.		*/
/*......................................................................*/

static int
datum_pts_start(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct datum_pts_unit *datum_pts;
	int fd;
#ifdef HAVE_TERMIOS
	int rc;
	struct termios arg;
#endif

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("Starting Datum PTS unit %d\n", unit);
#endif

	/*
	** Open the Datum PTS device
	*/
	fd = open(DATUM_DEV, O_RDWR);

	if (fd < 0) {
		msyslog(LOG_ERR, "Datum_PTS: open(\"%s\", O_RDWR) failed: %m", DATUM_DEV);
		return 0;
	}

	/*
	** Create the memory for the new unit
	*/
	datum_pts = emalloc_zero(sizeof(*datum_pts));
	datum_pts->unit = unit;	/* set my unit id */
	datum_pts->yearstart = 0;	/* initialize the yearstart to 0 */
	datum_pts->sigma2 = 0.0;	/* initialize the sigma2 to 0 */

	datum_pts->PTS_fd = fd;

	if (-1 == fcntl(datum_pts->PTS_fd, F_SETFL, 0)) /* clear the descriptor flags */
		msyslog(LOG_ERR, "MSF_ARCRON(%d): fcntl(F_SETFL, 0): %m.",
			unit);

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("Opening RS232 port with file descriptor %d\n",
		   datum_pts->PTS_fd);
#endif

	/*
	** Set up the RS232 terminal device information. Note that we assume that
	** we have termios. This code has only been tested on SUNs and HPs. If your
	** machine does not have termios this driver cannot be initialized. You can change this
	** if you want by editing this source. Please give the changes back to the
	** ntp folks so that it can become part of their regular distribution.
	*/

	memset(&arg, 0, sizeof(arg));

	arg.c_iflag = IGNBRK;
	arg.c_oflag = 0;
	arg.c_cflag = B9600 | CS8 | CREAD | PARENB | CLOCAL;
	arg.c_lflag = 0;
	arg.c_cc[VMIN] = 0;		/* start timeout timer right away (not used) */
	arg.c_cc[VTIME] = 30;		/* 3 second timout on reads (not used) */

	rc = tcsetattr(datum_pts->PTS_fd, TCSANOW, &arg);
	if (rc < 0) {
		msyslog(LOG_ERR, "Datum_PTS: tcsetattr(\"%s\") failed: %m", DATUM_DEV);
		close(datum_pts->PTS_fd);
		free(datum_pts);
		return 0;
	}

	/*
	** Initialize the ntpd IO structure
	*/

	datum_pts->peer = peer;
	pp = peer->procptr;
	pp->io.clock_recv = datum_pts_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = datum_pts->PTS_fd;

	if (!io_addclock(&pp->io)) {
		pp->io.fd = -1;
#ifdef DEBUG_DATUM_PTC
		if (debug)
		    printf("Problem adding clock\n");
#endif

		msyslog(LOG_ERR, "Datum_PTS: Problem adding clock");
		close(datum_pts->PTS_fd);
		free(datum_pts);

		return 0;
	}
	peer->procptr->unitptr = datum_pts;

	/*
	** Now add one to the number of units and return a successful code
	*/

	nunits++;
	return 1;

}


/*......................................................................*/
/*	datum_pts_shutdown - this routine shuts doen the device and	*/
/*	removes the memory for the unit.				*/
/*......................................................................*/

static void
datum_pts_shutdown(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct datum_pts_unit *datum_pts;

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("Shutdown Datum PTS\n");
#endif

	msyslog(LOG_ERR, "Datum_PTS: Shutdown Datum PTS");

	/*
	** We found the unit so close the file descriptor and free up the memory used
	** by the structure.
	*/
	pp = peer->procptr;
	datum_pts = pp->unitptr;
	if (NULL != datum_pts) {
		io_closeclock(&pp->io);
		free(datum_pts);
	}
}


/*......................................................................*/
/*	datum_pts_poll - this routine sends out the time request to the */
/*	Datum PTS device. The time will be passed back in the 		*/
/*	datum_pts_receive() routine.					*/
/*......................................................................*/

static void
datum_pts_poll(
	int unit,
	struct peer *peer
	)
{
	int error_code;
	struct datum_pts_unit *datum_pts;

	datum_pts = peer->procptr->unitptr;

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("Poll Datum PTS\n");
#endif

	/*
	** Find the right unit and send out a time request once it is found.
	*/
	error_code = write(datum_pts->PTS_fd, TIME_REQUEST, 6);
	if (error_code != 6)
		perror("TIME_REQUEST");
	datum_pts->nbytes = 0;
}


/*......................................................................*/
/*	datum_pts_control - not used					*/
/*......................................................................*/

static void
datum_pts_control(
	int unit,
	const struct refclockstat *in,
	struct refclockstat *out,
	struct peer *peer
	)
{

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("Control Datum PTS\n");
#endif

}


/*......................................................................*/
/*	datum_pts_init - initializes things for all possible Datum	*/
/*	time code generators that might be used. In practice, this is	*/
/*	only called once at the beginning before anything else is	*/
/*	called.								*/
/*......................................................................*/

static void
datum_pts_init(void)
{

	/*									*/
	/*...... open up the log file if we are debugging ......................*/
	/*									*/

	/*
	** Open up the log file if we are debugging. For now, send data out to the
	** screen (stdout).
	*/

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("Init Datum PTS\n");
#endif

	/*
	** Initialize the time request command string. This is the only message
	** that we ever have to send to the Datum PTS (although others are defined).
	*/

	memcpy(TIME_REQUEST, "//k/mn",6);

	/*
	** Initialize the number of units to 0 and set the dynamic array of units to
	** NULL since there are no units defined yet.
	*/

	nunits = 0;

}


/*......................................................................*/
/*	datum_pts_buginfo - not used					*/
/*......................................................................*/

static void
datum_pts_buginfo(
	int unit,
	register struct refclockbug *bug,
	register struct peer *peer
	)
{

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("Buginfo Datum PTS\n");
#endif

}


/*......................................................................*/
/*	datum_pts_receive - receive the time buffer that was read in	*/
/*	by the ntpd io handling routines. When 7 bytes have been	*/
/*	received (it may take several tries before all 7 bytes are	*/
/*	received), then the time code must be unpacked and sent to	*/
/*	the ntpd clock_receive() routine which causes the systems	*/
/*	clock to be updated (several layers down).			*/
/*......................................................................*/

static void
datum_pts_receive(
	struct recvbuf *rbufp
	)
{
	int i;
	size_t nb;
	l_fp tstmp;
	struct peer *p;
	struct datum_pts_unit *datum_pts;
	char *dpt;
	int dpend;
	int tzoff;
	int timerr;
	double ftimerr, abserr;
#ifdef DEBUG_DATUM_PTC
	double dispersion;
#endif
	int goodtime;
      /*double doffset;*/

	/*
	** Get the time code (maybe partial) message out of the rbufp buffer.
	*/

	p = rbufp->recv_peer;
	datum_pts = p->procptr->unitptr;
	dpt = (char *)&rbufp->recv_space;
	dpend = rbufp->recv_length;

#ifdef DEBUG_DATUM_PTC
	if (debug)
		printf("Receive Datum PTS: %d bytes\n", dpend);
#endif

	/*									*/
	/*...... save the ntp system time when the first byte is received ......*/
	/*									*/

	/*
	** Save the ntp system time when the first byte is received. Note that
	** because it may take several calls to this routine before all seven
	** bytes of our return message are finally received by the io handlers in
	** ntpd, we really do want to use the time tag when the first byte is
	** received to reduce the jitter.
	*/

	nb = datum_pts->nbytes;
	if (nb == 0) {
		datum_pts->lastrec = rbufp->recv_time;
	}

	/*
	** Increment our count to the number of bytes received so far. Return if we
	** haven't gotten all seven bytes yet.
	** [Sec 3388] make sure we do not overrun the buffer.
	** TODO: what to do with excessive bytes, if we ever get them?
	*/
	for (i=0; (i < dpend) && (nb < sizeof(datum_pts->retbuf)); i++, nb++) {
		datum_pts->retbuf[nb] = dpt[i];
	}
	datum_pts->nbytes = nb;
	
	if (nb < 7) {
		return;
	}

	/*
	** Convert the seven bytes received in our time buffer to day, hour, minute,
	** second, and msecond values. The usec value is not used for anything
	** currently. It is just the fractional part of the time stored in units
	** of microseconds.
	*/

	datum_pts->day =	100*(datum_pts->retbuf[0] & 0x0f) +
		10*((datum_pts->retbuf[1] & 0xf0)>>4) +
		(datum_pts->retbuf[1] & 0x0f);

	datum_pts->hour =	10*((datum_pts->retbuf[2] & 0x30)>>4) +
		(datum_pts->retbuf[2] & 0x0f);

	datum_pts->minute =	10*((datum_pts->retbuf[3] & 0x70)>>4) +
		(datum_pts->retbuf[3] & 0x0f);

	datum_pts->second =	10*((datum_pts->retbuf[4] & 0x70)>>4) +
		(datum_pts->retbuf[4] & 0x0f);

	datum_pts->msec =	100*((datum_pts->retbuf[5] & 0xf0) >> 4) + 
		10*(datum_pts->retbuf[5] & 0x0f) +
		((datum_pts->retbuf[6] & 0xf0)>>4);

	datum_pts->usec =	1000*datum_pts->msec;

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("day %d, hour %d, minute %d, second %d, msec %d\n",
		   datum_pts->day,
		   datum_pts->hour,
		   datum_pts->minute,
		   datum_pts->second,
		   datum_pts->msec);
#endif

	/*
	** Get the GMT time zone offset. Note that GMT should be zero if the Datum
	** reference time is using GMT as its time base. Otherwise we have to
	** determine the offset if the Datum PTS is using time of day as its time
	** base.
	*/

	goodtime = 0;		/* We are not sure about the time and offset yet */

#ifdef GMT

	/*
	** This is the case where the Datum PTS is using GMT so there is no time
	** zone offset.
	*/

	tzoff = 0;		/* set time zone offset to 0 */

#else

	/*
	** This is the case where the Datum PTS is using regular time of day for its
	** time so we must compute the time zone offset. The way we do it is kind of
	** funny but it works. We loop through different time zones (0 to 24) and
	** pick the one that gives the smallest error (+- one half hour). The time
	** zone offset is stored in the datum_pts structure for future use. Normally,
	** the clocktime() routine is only called once (unless the time zone offset
	** changes due to daylight savings) since the goodtime flag is set when a
	** good time is found (with a good offset). Note that even if the Datum
	** PTS is using GMT, this mechanism will still work since it should come up
	** with a value for tzoff = 0 (assuming that your system clock is within
	** a half hour of the Datum time (even with time zone differences).
	*/

	for (tzoff=0; tzoff<24; tzoff++) {
		if (clocktime( datum_pts->day,
			       datum_pts->hour,
			       datum_pts->minute,
			       datum_pts->second,
			       (tzoff + datum_pts->tzoff) % 24,
			       datum_pts->lastrec.l_ui,
			       &datum_pts->yearstart,
			       &datum_pts->lastref.l_ui) ) {

			datum_pts->lastref.l_uf = 0;
			error = datum_pts->lastref.l_ui - datum_pts->lastrec.l_ui;

#ifdef DEBUG_DATUM_PTC
			printf("Time Zone (clocktime method) = %d, error = %d\n", tzoff, error);
#endif

			if ((error < 1799) && (error > -1799)) {
				tzoff = (tzoff + datum_pts->tzoff) % 24;
				datum_pts->tzoff = tzoff;
				goodtime = 1;

#ifdef DEBUG_DATUM_PTC
				printf("Time Zone found (clocktime method) = %d\n",tzoff);
#endif

				break;
			}

		}
	}

#endif

	/*
	** Make sure that we have a good time from the Datum PTS. Clocktime() also
	** sets yearstart and lastref.l_ui. We will have to set astref.l_uf (i.e.,
	** the fraction of a second) stuff later.
	*/

	if (!goodtime) {

		if (!clocktime( datum_pts->day,
				datum_pts->hour,
				datum_pts->minute,
				datum_pts->second,
				tzoff,
				datum_pts->lastrec.l_ui,
				&datum_pts->yearstart,
				&datum_pts->lastref.l_ui) ) {

#ifdef DEBUG_DATUM_PTC
			if (debug)
			{
				printf("Error: bad clocktime\n");
				printf("GMT %d, lastrec %d, yearstart %d, lastref %d\n",
				       tzoff,
				       datum_pts->lastrec.l_ui,
				       datum_pts->yearstart,
				       datum_pts->lastref.l_ui);
			}
#endif

			msyslog(LOG_ERR, "Datum_PTS: Bad clocktime");

			return;

		}else{

#ifdef DEBUG_DATUM_PTC
			if (debug)
			    printf("Good clocktime\n");
#endif

		}

	}

	/*
	** We have datum_pts->lastref.l_ui set (which is the integer part of the
	** time. Now set the microseconds field.
	*/

	TVUTOTSF(datum_pts->usec, datum_pts->lastref.l_uf);

	/*
	** Compute the time correction as the difference between the reference
	** time (i.e., the Datum time) minus the receive time (system time).
	*/

	tstmp = datum_pts->lastref;		/* tstmp is the datum ntp time */
	L_SUB(&tstmp, &datum_pts->lastrec);	/* tstmp is now the correction */
	datum_pts->coderecv++;		/* increment a counter */

#ifdef DEBUG_DATUM_PTC
	dispersion = DATUM_DISPERSION;	/* set the dispersion to 0 */
	ftimerr = dispersion;
	ftimerr /= (1024.0 * 64.0);
	if (debug)
	    printf("dispersion = %d, %f\n", dispersion, ftimerr);
#endif

	/*
	** Pass the new time to ntpd through the refclock_receive function. Note
	** that we are not trying to make any corrections due to the time it takes
	** for the Datum PTS to send the message back. I am (erroneously) assuming
	** that the time for the Datum PTS to send the time back to us is negligable.
	** I suspect that this time delay may be as much as 15 ms or so (but probably
	** less). For our needs at JPL, this kind of error is ok so it is not
	** necessary to use fudge factors in the ntp.conf file. Maybe later we will.
	*/
      /*LFPTOD(&tstmp, doffset);*/
	datum_pts->lastref = datum_pts->lastrec;
	refclock_receive(datum_pts->peer);

	/*
	** Compute sigma squared (not used currently). Maybe later, this could be
	** used for the dispersion estimate. The problem is that ntpd does not link
	** in the math library so sqrt() is not available. Anyway, this is useful
	** for debugging. Maybe later I will just use absolute values for the time
	** error to come up with my dispersion estimate. Anyway, for now my dispersion
	** is set to 0.
	*/

	timerr = tstmp.l_ui<<20;
	timerr |= (tstmp.l_uf>>12) & 0x000fffff;
	ftimerr = timerr;
	ftimerr /= 1024*1024;
	abserr = ftimerr;
	if (ftimerr < 0.0) abserr = -ftimerr;

	if (datum_pts->sigma2 == 0.0) {
		if (abserr < DATUM_MAX_ERROR) {
			datum_pts->sigma2 = abserr*abserr;
		}else{
			datum_pts->sigma2 = DATUM_MAX_ERROR2;
		}
	}else{
		if (abserr < DATUM_MAX_ERROR) {
			datum_pts->sigma2 = 0.95*datum_pts->sigma2 + 0.05*abserr*abserr;
		}else{
			datum_pts->sigma2 = 0.95*datum_pts->sigma2 + 0.05*DATUM_MAX_ERROR2;
		}
	}

#ifdef DEBUG_DATUM_PTC
	if (debug)
	    printf("Time error = %f seconds\n", ftimerr);
#endif

#if defined(DEBUG_DATUM_PTC) || defined(LOG_TIME_ERRORS)
	if (debug)
	    printf("PTS: day %d, hour %d, minute %d, second %d, msec %d, Time Error %f\n",
		   datum_pts->day,
		   datum_pts->hour,
		   datum_pts->minute,
		   datum_pts->second,
		   datum_pts->msec,
		   ftimerr);
#endif

}
#else
NONEMPTY_TRANSLATION_UNIT
#endif /* REFCLOCK */
