/*
 * ntp_refclock - processing support for reference clocks
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_unixtime.h"
#include "ntp_tty.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"
#include "ntp_assert.h"

#include <stdio.h>

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */

#ifdef REFCLOCK

#ifdef KERNEL_PLL
#include "ntp_syscall.h"
#endif /* KERNEL_PLL */

#ifdef HAVE_PPSAPI
#include "ppsapi_timepps.h"
#include "refclock_atom.h"
#endif /* HAVE_PPSAPI */

/*
 * Reference clock support is provided here by maintaining the fiction
 * that the clock is actually a peer.  As no packets are exchanged with
 * a reference clock, however, we replace the transmit, receive and
 * packet procedures with separate code to simulate them.  Routines
 * refclock_transmit() and refclock_receive() maintain the peer
 * variables in a state analogous to an actual peer and pass reference
 * clock data on through the filters.  Routines refclock_peer() and
 * refclock_unpeer() are called to initialize and terminate reference
 * clock associations.  A set of utility routines is included to open
 * serial devices, process sample data, and to perform various debugging
 * functions.
 *
 * The main interface used by these routines is the refclockproc
 * structure, which contains for most drivers the decimal equivalants
 * of the year, day, month, hour, second and millisecond/microsecond
 * decoded from the ASCII timecode.  Additional information includes
 * the receive timestamp, exception report, statistics tallies, etc. 
 * In addition, there may be a driver-specific unit structure used for
 * local control of the device.
 *
 * The support routines are passed a pointer to the peer structure,
 * which is used for all peer-specific processing and contains a
 * pointer to the refclockproc structure, which in turn contains a
 * pointer to the unit structure, if used.  The peer structure is 
 * identified by an interface address in the dotted quad form 
 * 127.127.t.u, where t is the clock type and u the unit.
 */
#define FUDGEFAC	.1	/* fudge correction factor */
#define LF		0x0a	/* ASCII LF */

int	cal_enable;		/* enable refclock calibrate */

/*
 * Forward declarations
 */
static int refclock_cmpl_fp (const void *, const void *);
static int refclock_sample (struct refclockproc *);
static int refclock_ioctl(int, u_int);


/*
 * refclock_report - note the occurance of an event
 *
 * This routine presently just remembers the report and logs it, but
 * does nothing heroic for the trap handler. It tries to be a good
 * citizen and bothers the system log only if things change.
 */
void
refclock_report(
	struct peer *peer,
	int code
	)
{
	struct refclockproc *pp;

	pp = peer->procptr;
	if (pp == NULL)
		return;

	switch (code) {

	case CEVNT_TIMEOUT:
		pp->noreply++;
		break;

	case CEVNT_BADREPLY:
		pp->badformat++;
		break;

	case CEVNT_FAULT:
		break;

	case CEVNT_BADDATE:
	case CEVNT_BADTIME:
		pp->baddata++;
		break;

	default:
		/* ignore others */
		break;
	}
	if ((code != CEVNT_NOMINAL) && (pp->lastevent < 15))
		pp->lastevent++;
	if (pp->currentstatus != code) {
		pp->currentstatus = (u_char)code;
		report_event(PEVNT_CLOCK, peer, ceventstr(code));
	}
}


/*
 * init_refclock - initialize the reference clock drivers
 *
 * This routine calls each of the drivers in turn to initialize internal
 * variables, if necessary. Most drivers have nothing to say at this
 * point.
 */
void
init_refclock(void)
{
	int i;

	for (i = 0; i < (int)num_refclock_conf; i++)
		if (refclock_conf[i]->clock_init != noentry)
			(refclock_conf[i]->clock_init)();
}


/*
 * refclock_newpeer - initialize and start a reference clock
 *
 * This routine allocates and initializes the interface structure which
 * supports a reference clock in the form of an ordinary NTP peer. A
 * driver-specific support routine completes the initialization, if
 * used. Default peer variables which identify the clock and establish
 * its reference ID and stratum are set here. It returns one if success
 * and zero if the clock address is invalid or already running,
 * insufficient resources are available or the driver declares a bum
 * rap.
 */
int
refclock_newpeer(
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	u_char clktype;
	int unit;

	/*
	 * Check for valid clock address. If already running, shut it
	 * down first.
	 */
	if (!ISREFCLOCKADR(&peer->srcadr)) {
		msyslog(LOG_ERR,
			"refclock_newpeer: clock address %s invalid",
			stoa(&peer->srcadr));
		return (0);
	}
	clktype = (u_char)REFCLOCKTYPE(&peer->srcadr);
	unit = REFCLOCKUNIT(&peer->srcadr);
	if (clktype >= num_refclock_conf ||
		refclock_conf[clktype]->clock_start == noentry) {
		msyslog(LOG_ERR,
			"refclock_newpeer: clock type %d invalid\n",
			clktype);
		return (0);
	}

	/*
	 * Allocate and initialize interface structure
	 */
	pp = emalloc_zero(sizeof(*pp));
	peer->procptr = pp;

	/*
	 * Initialize structures
	 */
	peer->refclktype = clktype;
	peer->refclkunit = (u_char)unit;
	peer->flags |= FLAG_REFCLOCK;
	peer->leap = LEAP_NOTINSYNC;
	peer->stratum = STRATUM_REFCLOCK;
	peer->ppoll = peer->maxpoll;
	pp->type = clktype;
	pp->conf = refclock_conf[clktype];
	pp->timestarted = current_time;
	pp->io.fd = -1;

	/*
	 * Set peer.pmode based on the hmode. For appearances only.
	 */
	switch (peer->hmode) {
	case MODE_ACTIVE:
		peer->pmode = MODE_PASSIVE;
		break;

	default:
		peer->pmode = MODE_SERVER;
		break;
	}

	/*
	 * Do driver dependent initialization. The above defaults
	 * can be wiggled, then finish up for consistency.
	 */
	if (!((refclock_conf[clktype]->clock_start)(unit, peer))) {
		refclock_unpeer(peer);
		return (0);
	}
	peer->refid = pp->refid;
	return (1);
}


/*
 * refclock_unpeer - shut down a clock
 */
void
refclock_unpeer(
	struct peer *peer	/* peer structure pointer */
	)
{
	u_char clktype;
	int unit;

	/*
	 * Wiggle the driver to release its resources, then give back
	 * the interface structure.
	 */
	if (NULL == peer->procptr)
		return;

	clktype = peer->refclktype;
	unit = peer->refclkunit;
	if (refclock_conf[clktype]->clock_shutdown != noentry)
		(refclock_conf[clktype]->clock_shutdown)(unit, peer);
	free(peer->procptr);
	peer->procptr = NULL;
}


/*
 * refclock_timer - called once per second for housekeeping.
 */
void
refclock_timer(
	struct peer *p
	)
{
	struct refclockproc *	pp;
	int			unit;

	unit = p->refclkunit;
	pp = p->procptr;
	if (pp->conf->clock_timer != noentry)
		(*pp->conf->clock_timer)(unit, p);
	if (pp->action != NULL && pp->nextaction <= current_time)
		(*pp->action)(p);
}
	

/*
 * refclock_transmit - simulate the transmit procedure
 *
 * This routine implements the NTP transmit procedure for a reference
 * clock. This provides a mechanism to call the driver at the NTP poll
 * interval, as well as provides a reachability mechanism to detect a
 * broken radio or other madness.
 */
void
refclock_transmit(
	struct peer *peer	/* peer structure pointer */
	)
{
	u_char clktype;
	int unit;

	clktype = peer->refclktype;
	unit = peer->refclkunit;
	peer->sent++;
	get_systime(&peer->xmt);

	/*
	 * This is a ripoff of the peer transmit routine, but
	 * specialized for reference clocks. We do a little less
	 * protocol here and call the driver-specific transmit routine.
	 */
	if (peer->burst == 0) {
		u_char oreach;
#ifdef DEBUG
		if (debug)
			printf("refclock_transmit: at %ld %s\n",
			    current_time, stoa(&(peer->srcadr)));
#endif

		/*
		 * Update reachability and poll variables like the
		 * network code.
		 */
		oreach = peer->reach & 0xfe;
		peer->reach <<= 1;
		if (!(peer->reach & 0x0f))
			clock_filter(peer, 0., 0., MAXDISPERSE);
		peer->outdate = current_time;
		if (!peer->reach) {
			if (oreach) {
				report_event(PEVNT_UNREACH, peer, NULL);
				peer->timereachable = current_time;
			}
		} else {
			if (peer->flags & FLAG_BURST)
				peer->burst = NSTAGE;
		}
	} else {
		peer->burst--;
	}
	if (refclock_conf[clktype]->clock_poll != noentry)
		(refclock_conf[clktype]->clock_poll)(unit, peer);
	poll_update(peer, peer->hpoll);
}


/*
 * Compare two doubles - used with qsort()
 */
static int
refclock_cmpl_fp(
	const void *p1,
	const void *p2
	)
{
	const double *dp1 = (const double *)p1;
	const double *dp2 = (const double *)p2;

	if (*dp1 < *dp2)
		return -1;
	if (*dp1 > *dp2)
		return 1;
	return 0;
}


/*
 * refclock_process_offset - update median filter
 *
 * This routine uses the given offset and timestamps to construct a new
 * entry in the median filter circular buffer. Samples that overflow the
 * filter are quietly discarded.
 */
void
refclock_process_offset(
	struct refclockproc *pp,	/* refclock structure pointer */
	l_fp lasttim,			/* last timecode timestamp */
	l_fp lastrec,			/* last receive timestamp */
	double fudge
	)
{
	l_fp lftemp;
	double doffset;

	pp->lastrec = lastrec;
	lftemp = lasttim;
	L_SUB(&lftemp, &lastrec);
	LFPTOD(&lftemp, doffset);
	SAMPLE(doffset + fudge);
}


/*
 * refclock_process - process a sample from the clock
 * refclock_process_f - refclock_process with other than time1 fudge
 *
 * This routine converts the timecode in the form days, hours, minutes,
 * seconds and milliseconds/microseconds to internal timestamp format,
 * then constructs a new entry in the median filter circular buffer.
 * Return success (1) if the data are correct and consistent with the
 * converntional calendar.
 *
 * Important for PPS users: Normally, the pp->lastrec is set to the
 * system time when the on-time character is received and the pp->year,
 * ..., pp->second decoded and the seconds fraction pp->nsec in
 * nanoseconds). When a PPS offset is available, pp->nsec is forced to
 * zero and the fraction for pp->lastrec is set to the PPS offset.
 */
int
refclock_process_f(
	struct refclockproc *pp,	/* refclock structure pointer */
	double fudge
	)
{
	l_fp offset, ltemp;

	/*
	 * Compute the timecode timestamp from the days, hours, minutes,
	 * seconds and milliseconds/microseconds of the timecode. Use
	 * clocktime() for the aggregate seconds and the msec/usec for
	 * the fraction, when present. Note that this code relies on the
	 * filesystem time for the years and does not use the years of
	 * the timecode.
	 */
	if (!clocktime(pp->day, pp->hour, pp->minute, pp->second, GMT,
		pp->lastrec.l_ui, &pp->yearstart, &offset.l_ui))
		return (0);

	offset.l_uf = 0;
	DTOLFP(pp->nsec / 1e9, &ltemp);
	L_ADD(&offset, &ltemp);
	refclock_process_offset(pp, offset, pp->lastrec, fudge);
	return (1);
}


int
refclock_process(
	struct refclockproc *pp		/* refclock structure pointer */
)
{
	return refclock_process_f(pp, pp->fudgetime1);
}


/*
 * refclock_sample - process a pile of samples from the clock
 *
 * This routine implements a recursive median filter to suppress spikes
 * in the data, as well as determine a performance statistic. It
 * calculates the mean offset and RMS jitter. A time adjustment
 * fudgetime1 can be added to the final offset to compensate for various
 * systematic errors. The routine returns the number of samples
 * processed, which could be zero.
 */
static int
refclock_sample(
	struct refclockproc *pp		/* refclock structure pointer */
	)
{
	size_t	i, j, k, m, n;
	double	off[MAXSTAGE];
	double	offset;

	/*
	 * Copy the raw offsets and sort into ascending order. Don't do
	 * anything if the buffer is empty.
	 */
	n = 0;
	while (pp->codeproc != pp->coderecv) {
		pp->codeproc = (pp->codeproc + 1) % MAXSTAGE;
		off[n] = pp->filter[pp->codeproc];
		n++;
	}
	if (n == 0)
		return (0);

	if (n > 1)
		qsort(off, n, sizeof(off[0]), refclock_cmpl_fp);

	/*
	 * Reject the furthest from the median of the samples until
	 * approximately 60 percent of the samples remain.
	 */
	i = 0; j = n;
	m = n - (n * 4) / 10;
	while ((j - i) > m) {
		offset = off[(j + i) / 2];
		if (off[j - 1] - offset < offset - off[i])
			i++;	/* reject low end */
		else
			j--;	/* reject high end */
	}

	/*
	 * Determine the offset and jitter.
	 */
	pp->offset = 0;
	pp->jitter = 0;
	for (k = i; k < j; k++) {
		pp->offset += off[k];
		if (k > i)
			pp->jitter += SQUARE(off[k] - off[k - 1]);
	}
	pp->offset /= m;
	pp->jitter = max(SQRT(pp->jitter / m), LOGTOD(sys_precision));
#ifdef DEBUG
	if (debug)
		printf(
		    "refclock_sample: n %d offset %.6f disp %.6f jitter %.6f\n",
		    (int)n, pp->offset, pp->disp, pp->jitter);
#endif
	return (int)n;
}


/*
 * refclock_receive - simulate the receive and packet procedures
 *
 * This routine simulates the NTP receive and packet procedures for a
 * reference clock. This provides a mechanism in which the ordinary NTP
 * filter, selection and combining algorithms can be used to suppress
 * misbehaving radios and to mitigate between them when more than one is
 * available for backup.
 */
void
refclock_receive(
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;

#ifdef DEBUG
	if (debug)
		printf("refclock_receive: at %lu %s\n",
		    current_time, stoa(&peer->srcadr));
#endif

	/*
	 * Do a little sanity dance and update the peer structure. Groom
	 * the median filter samples and give the data to the clock
	 * filter.
	 */
	pp = peer->procptr;
	peer->leap = pp->leap;
	if (peer->leap == LEAP_NOTINSYNC)
		return;

	peer->received++;
	peer->timereceived = current_time;
	if (!peer->reach) {
		report_event(PEVNT_REACH, peer, NULL);
		peer->timereachable = current_time;
	}
	peer->reach |= 1;
	peer->reftime = pp->lastref;
	peer->aorg = pp->lastrec;
	peer->rootdisp = pp->disp;
	get_systime(&peer->dst);
	if (!refclock_sample(pp))
		return;

	clock_filter(peer, pp->offset, 0., pp->jitter);
	if (cal_enable && fabs(last_offset) < sys_mindisp && sys_peer !=
	    NULL) {
		if (sys_peer->refclktype == REFCLK_ATOM_PPS &&
		    peer->refclktype != REFCLK_ATOM_PPS)
			pp->fudgetime1 -= pp->offset * FUDGEFAC;
	}
}


/*
 * refclock_gtlin - groom next input line and extract timestamp
 *
 * This routine processes the timecode received from the clock and
 * strips the parity bit and control characters. It returns the number
 * of characters in the line followed by a NULL character ('\0'), which
 * is not included in the count. In case of an empty line, the previous
 * line is preserved.
 */
int
refclock_gtlin(
	struct recvbuf *rbufp,	/* receive buffer pointer */
	char	*lineptr,	/* current line pointer */
	int	bmax,		/* remaining characters in line */
	l_fp	*tsptr		/* pointer to timestamp returned */
	)
{
	const char *sp, *spend;
	char	   *dp, *dpend;
	int         dlen;

	if (bmax <= 0)
		return (0);

	dp    = lineptr;
	dpend = dp + bmax - 1; /* leave room for NUL pad */
	sp    = (const char *)rbufp->recv_buffer;
	spend = sp + rbufp->recv_length;

	while (sp != spend && dp != dpend) {
		char c;
		
		c = *sp++ & 0x7f;
		if (c >= 0x20 && c < 0x7f)
			*dp++ = c;
	}
	/* Get length of data written to the destination buffer. If
	 * zero, do *not* place a NUL byte to preserve the previous
	 * buffer content.
	 */
	dlen = dp - lineptr;
	if (dlen)
	    *dp  = '\0';
	*tsptr = rbufp->recv_time;
	DPRINTF(2, ("refclock_gtlin: fd %d time %s timecode %d %s\n",
		    rbufp->fd, ulfptoa(&rbufp->recv_time, 6), dlen,
		    (dlen != 0)
			? lineptr
			: ""));
	return (dlen);
}


/*
 * refclock_gtraw - get next line/chunk of data
 *
 * This routine returns the raw data received from the clock in both
 * canonical or raw modes. The terminal interface routines map CR to LF.
 * In canonical mode this results in two lines, one containing data
 * followed by LF and another containing only LF. In raw mode the
 * interface routines can deliver arbitraty chunks of data from one
 * character to a maximum specified by the calling routine. In either
 * mode the routine returns the number of characters in the line
 * followed by a NULL character ('\0'), which is not included in the
 * count.
 *
 * *tsptr receives a copy of the buffer timestamp.
 */
int
refclock_gtraw(
	struct recvbuf *rbufp,	/* receive buffer pointer */
	char	*lineptr,	/* current line pointer */
	int	bmax,		/* remaining characters in line */
	l_fp	*tsptr		/* pointer to timestamp returned */
	)
{
	if (bmax <= 0)
		return (0);
	bmax -= 1; /* leave room for trailing NUL */
	if (bmax > rbufp->recv_length)
		bmax = rbufp->recv_length;
	memcpy(lineptr, rbufp->recv_buffer, bmax);
	lineptr[bmax] = '\0';

	*tsptr = rbufp->recv_time;
	DPRINTF(2, ("refclock_gtraw: fd %d time %s timecode %d %s\n",
		    rbufp->fd, ulfptoa(&rbufp->recv_time, 6), bmax,
		    lineptr));
	return (bmax);
}


/*
 * indicate_refclock_packet()
 *
 * Passes a fragment of refclock input read from the device to the
 * driver direct input routine, which may consume it (batch it for
 * queuing once a logical unit is assembled).  If it is not so
 * consumed, queue it for the driver's receive entrypoint.
 *
 * The return value is TRUE if the data has been consumed as a fragment
 * and should not be counted as a received packet.
 */
int
indicate_refclock_packet(
	struct refclockio *	rio,
	struct recvbuf *	rb
	)
{
	/* Does this refclock use direct input routine? */
	if (rio->io_input != NULL && (*rio->io_input)(rb) == 0) {
		/*
		 * data was consumed - nothing to pass up
		 * into block input machine
		 */
		freerecvbuf(rb);

		return TRUE;
	}
	add_full_recv_buffer(rb);

	return FALSE;
}


/*
 * process_refclock_packet()
 *
 * Used for deferred processing of 'io_input' on systems where threading
 * is used (notably Windows). This is acting as a trampoline to make the
 * real calls to the refclock functions.
 */
#ifdef HAVE_IO_COMPLETION_PORT
void
process_refclock_packet(
	struct recvbuf * rb
	)
{
	struct refclockio * rio;

	/* get the refclockio structure from the receive buffer */
	rio  = &rb->recv_peer->procptr->io;

	/* call 'clock_recv' if either there is no input function or the
	 * raw input function tells us to feed the packet to the
	 * receiver.
	 */
	if (rio->io_input == NULL || (*rio->io_input)(rb) != 0) {
		rio->recvcount++;
		packets_received++;
		handler_pkts++;
		(*rio->clock_recv)(rb);
	}
}
#endif	/* HAVE_IO_COMPLETION_PORT */


/*
 * The following code does not apply to WINNT & VMS ...
 */
#if !defined(SYS_VXWORKS) && !defined(SYS_WINNT)
#if defined(HAVE_TERMIOS) || defined(HAVE_SYSV_TTYS) || defined(HAVE_BSD_TTYS)

/*
 * refclock_open - open serial port for reference clock
 *
 * This routine opens a serial port for I/O and sets default options. It
 * returns the file descriptor if successful, or logs an error and
 * returns -1.
 */
int
refclock_open(
	const char	*dev,	/* device name pointer */
	u_int		speed,	/* serial port speed (code) */
	u_int		lflags	/* line discipline flags */
	)
{
	int	fd;
	int	omode;
#ifdef O_NONBLOCK
	char	trash[128];	/* litter bin for old input data */
#endif

	/*
	 * Open serial port and set default options
	 */
	omode = O_RDWR;
#ifdef O_NONBLOCK
	omode |= O_NONBLOCK;
#endif
#ifdef O_NOCTTY
	omode |= O_NOCTTY;
#endif

	fd = open(dev, omode, 0777);
	/* refclock_open() long returned 0 on failure, avoid it. */
	if (0 == fd) {
		fd = dup(0);
		SAVE_ERRNO(
			close(0);
		)
	}
	if (fd < 0) {
		SAVE_ERRNO(
			msyslog(LOG_ERR, "refclock_open %s: %m", dev);
		)
		return -1;
	}
	if (!refclock_setup(fd, speed, lflags)) {
		close(fd);
		return -1;
	}
	if (!refclock_ioctl(fd, lflags)) {
		close(fd);
		return -1;
	}
#ifdef O_NONBLOCK
	/*
	 * We want to make sure there is no pending trash in the input
	 * buffer. Since we have non-blocking IO available, this is a
	 * good moment to read and dump all available outdated stuff
	 * that might have become toxic for the driver.
	 */
	while (read(fd, trash, sizeof(trash)) > 0 || errno == EINTR)
		/*NOP*/;
#endif
	return fd;
}


/*
 * refclock_setup - initialize terminal interface structure
 */
int
refclock_setup(
	int	fd,		/* file descriptor */
	u_int	speed,		/* serial port speed (code) */
	u_int	lflags		/* line discipline flags */
	)
{
	int	i;
	TTY	ttyb, *ttyp;

	/*
	 * By default, the serial line port is initialized in canonical
	 * (line-oriented) mode at specified line speed, 8 bits and no
	 * parity. LF ends the line and CR is mapped to LF. The break,
	 * erase and kill functions are disabled. There is a different
	 * section for each terminal interface, as selected at compile
	 * time. The flag bits can be used to set raw mode and echo.
	 */
	ttyp = &ttyb;
#ifdef HAVE_TERMIOS

	/*
	 * POSIX serial line parameters (termios interface)
	 */
	if (tcgetattr(fd, ttyp) < 0) {
		SAVE_ERRNO(
			msyslog(LOG_ERR,
				"refclock_setup fd %d tcgetattr: %m",
				fd);
		)
		return FALSE;
	}

	/*
	 * Set canonical mode and local connection; set specified speed,
	 * 8 bits and no parity; map CR to NL; ignore break.
	 */
	if (speed) {
		u_int	ltemp = 0;

		ttyp->c_iflag = IGNBRK | IGNPAR | ICRNL;
		ttyp->c_oflag = 0;
		ttyp->c_cflag = CS8 | CLOCAL | CREAD;
		if (lflags & LDISC_7O1) {
			/* HP Z3801A needs 7-bit, odd parity */
			ttyp->c_cflag = CS7 | PARENB | PARODD | CLOCAL | CREAD;
		}
		cfsetispeed(&ttyb, speed);
		cfsetospeed(&ttyb, speed);
		for (i = 0; i < NCCS; ++i)
			ttyp->c_cc[i] = '\0';

#if defined(TIOCMGET) && !defined(SCO5_CLOCK)

		/*
		 * If we have modem control, check to see if modem leads
		 * are active; if so, set remote connection. This is
		 * necessary for the kernel pps mods to work.
		 */
		if (ioctl(fd, TIOCMGET, (char *)&ltemp) < 0)
			msyslog(LOG_ERR,
			    "refclock_setup fd %d TIOCMGET: %m", fd);
#ifdef DEBUG
		if (debug)
			printf("refclock_setup fd %d modem status: 0x%x\n",
			    fd, ltemp);
#endif
		if (ltemp & TIOCM_DSR && lflags & LDISC_REMOTE)
			ttyp->c_cflag &= ~CLOCAL;
#endif /* TIOCMGET */
	}

	/*
	 * Set raw and echo modes. These can be changed on-fly.
	 */
	ttyp->c_lflag = ICANON;
	if (lflags & LDISC_RAW) {
		ttyp->c_lflag = 0;
		ttyp->c_iflag = 0;
		ttyp->c_cc[VMIN] = 1;
	}
	if (lflags & LDISC_ECHO)
		ttyp->c_lflag |= ECHO;
	if (tcsetattr(fd, TCSANOW, ttyp) < 0) {
		SAVE_ERRNO(
			msyslog(LOG_ERR,
				"refclock_setup fd %d TCSANOW: %m",
				fd);
		)
		return FALSE;
	}

	/*
	 * flush input and output buffers to discard any outdated stuff
	 * that might have become toxic for the driver. Failing to do so
	 * is logged, but we keep our fingers crossed otherwise.
	 */
	if (tcflush(fd, TCIOFLUSH) < 0)
		msyslog(LOG_ERR, "refclock_setup fd %d tcflush(): %m",
			fd);
#endif /* HAVE_TERMIOS */

#ifdef HAVE_SYSV_TTYS

	/*
	 * System V serial line parameters (termio interface)
	 *
	 */
	if (ioctl(fd, TCGETA, ttyp) < 0) {
		SAVE_ERRNO(
			msyslog(LOG_ERR,
				"refclock_setup fd %d TCGETA: %m",
				fd);
		)
		return FALSE;
	}

	/*
	 * Set canonical mode and local connection; set specified speed,
	 * 8 bits and no parity; map CR to NL; ignore break.
	 */
	if (speed) {
		u_int	ltemp = 0;

		ttyp->c_iflag = IGNBRK | IGNPAR | ICRNL;
		ttyp->c_oflag = 0;
		ttyp->c_cflag = speed | CS8 | CLOCAL | CREAD;
		for (i = 0; i < NCCS; ++i)
			ttyp->c_cc[i] = '\0';

#if defined(TIOCMGET) && !defined(SCO5_CLOCK)

		/*
		 * If we have modem control, check to see if modem leads
		 * are active; if so, set remote connection. This is
		 * necessary for the kernel pps mods to work.
		 */
		if (ioctl(fd, TIOCMGET, (char *)&ltemp) < 0)
			msyslog(LOG_ERR,
			    "refclock_setup fd %d TIOCMGET: %m", fd);
#ifdef DEBUG
		if (debug)
			printf("refclock_setup fd %d modem status: %x\n",
			    fd, ltemp);
#endif
		if (ltemp & TIOCM_DSR)
			ttyp->c_cflag &= ~CLOCAL;
#endif /* TIOCMGET */
	}

	/*
	 * Set raw and echo modes. These can be changed on-fly.
	 */
	ttyp->c_lflag = ICANON;
	if (lflags & LDISC_RAW) {
		ttyp->c_lflag = 0;
		ttyp->c_iflag = 0;
		ttyp->c_cc[VMIN] = 1;
	}
	if (ioctl(fd, TCSETA, ttyp) < 0) {
		SAVE_ERRNO(
			msyslog(LOG_ERR,
				"refclock_setup fd %d TCSETA: %m", fd);
		)
		return FALSE;
	}
#endif /* HAVE_SYSV_TTYS */

#ifdef HAVE_BSD_TTYS

	/*
	 * 4.3bsd serial line parameters (sgttyb interface)
	 */
	if (ioctl(fd, TIOCGETP, (char *)ttyp) < 0) {
		SAVE_ERRNO(
			msyslog(LOG_ERR,
				"refclock_setup fd %d TIOCGETP: %m",
				fd);
		)
		return FALSE;
	}
	if (speed)
		ttyp->sg_ispeed = ttyp->sg_ospeed = speed;
	ttyp->sg_flags = EVENP | ODDP | CRMOD;
	if (ioctl(fd, TIOCSETP, (char *)ttyp) < 0) {
		SAVE_ERRNO(
			msyslog(LOG_ERR, "refclock_setup TIOCSETP: %m");
		)
		return FALSE;
	}
#endif /* HAVE_BSD_TTYS */
	return(1);
}
#endif /* HAVE_TERMIOS || HAVE_SYSV_TTYS || HAVE_BSD_TTYS */


/*
 * refclock_ioctl - set serial port control functions
 *
 * This routine attempts to hide the internal, system-specific details
 * of serial ports. It can handle POSIX (termios), SYSV (termio) and BSD
 * (sgtty) interfaces with varying degrees of success. The routine sets
 * up optional features such as tty_clk. The routine returns TRUE if
 * successful.
 */
int
refclock_ioctl(
	int	fd, 		/* file descriptor */
	u_int	lflags		/* line discipline flags */
	)
{
	/*
	 * simply return TRUE if no UNIX line discipline is supported
	 */
	DPRINTF(1, ("refclock_ioctl: fd %d flags 0x%x\n", fd, lflags));

	return TRUE;
}
#endif /* !defined(SYS_VXWORKS) && !defined(SYS_WINNT) */


/*
 * refclock_control - set and/or return clock values
 *
 * This routine is used mainly for debugging. It returns designated
 * values from the interface structure that can be displayed using
 * ntpdc and the clockstat command. It can also be used to initialize
 * configuration variables, such as fudgetimes, fudgevalues, reference
 * ID and stratum.
 */
void
refclock_control(
	sockaddr_u *srcadr,
	const struct refclockstat *in,
	struct refclockstat *out
	)
{
	struct peer *peer;
	struct refclockproc *pp;
	u_char clktype;
	int unit;

	/*
	 * Check for valid address and running peer
	 */
	if (!ISREFCLOCKADR(srcadr))
		return;

	clktype = (u_char)REFCLOCKTYPE(srcadr);
	unit = REFCLOCKUNIT(srcadr);

	peer = findexistingpeer(srcadr, NULL, NULL, -1, 0, NULL);

	if (NULL == peer)
		return;

	INSIST(peer->procptr != NULL);
	pp = peer->procptr;

	/*
	 * Initialize requested data
	 */
	if (in != NULL) {
		if (in->haveflags & CLK_HAVETIME1)
			pp->fudgetime1 = in->fudgetime1;
		if (in->haveflags & CLK_HAVETIME2)
			pp->fudgetime2 = in->fudgetime2;
		if (in->haveflags & CLK_HAVEVAL1)
			peer->stratum = pp->stratum = (u_char)in->fudgeval1;
		if (in->haveflags & CLK_HAVEVAL2)
			peer->refid = pp->refid = in->fudgeval2;
		if (in->haveflags & CLK_HAVEFLAG1) {
			pp->sloppyclockflag &= ~CLK_FLAG1;
			pp->sloppyclockflag |= in->flags & CLK_FLAG1;
		}
		if (in->haveflags & CLK_HAVEFLAG2) {
			pp->sloppyclockflag &= ~CLK_FLAG2;
			pp->sloppyclockflag |= in->flags & CLK_FLAG2;
		}
		if (in->haveflags & CLK_HAVEFLAG3) {
			pp->sloppyclockflag &= ~CLK_FLAG3;
			pp->sloppyclockflag |= in->flags & CLK_FLAG3;
		}
		if (in->haveflags & CLK_HAVEFLAG4) {
			pp->sloppyclockflag &= ~CLK_FLAG4;
			pp->sloppyclockflag |= in->flags & CLK_FLAG4;
		}
	}

	/*
	 * Readback requested data
	 */
	if (out != NULL) {
		out->fudgeval1 = pp->stratum;
		out->fudgeval2 = pp->refid;
		out->haveflags = CLK_HAVEVAL1 | CLK_HAVEVAL2;
		out->fudgetime1 = pp->fudgetime1;
		if (0.0 != out->fudgetime1)
			out->haveflags |= CLK_HAVETIME1;
		out->fudgetime2 = pp->fudgetime2;
		if (0.0 != out->fudgetime2)
			out->haveflags |= CLK_HAVETIME2;
		out->flags = (u_char) pp->sloppyclockflag;
		if (CLK_FLAG1 & out->flags)
			out->haveflags |= CLK_HAVEFLAG1;
		if (CLK_FLAG2 & out->flags)
			out->haveflags |= CLK_HAVEFLAG2;
		if (CLK_FLAG3 & out->flags)
			out->haveflags |= CLK_HAVEFLAG3;
		if (CLK_FLAG4 & out->flags)
			out->haveflags |= CLK_HAVEFLAG4;

		out->timereset = current_time - pp->timestarted;
		out->polls = pp->polls;
		out->noresponse = pp->noreply;
		out->badformat = pp->badformat;
		out->baddata = pp->baddata;

		out->lastevent = pp->lastevent;
		out->currentstatus = pp->currentstatus;
		out->type = pp->type;
		out->clockdesc = pp->clockdesc;
		out->lencode = (u_short)pp->lencode;
		out->p_lastcode = pp->a_lastcode;
	}

	/*
	 * Give the stuff to the clock
	 */
	if (refclock_conf[clktype]->clock_control != noentry)
		(refclock_conf[clktype]->clock_control)(unit, in, out, peer);
}


/*
 * refclock_buginfo - return debugging info
 *
 * This routine is used mainly for debugging. It returns designated
 * values from the interface structure that can be displayed using
 * ntpdc and the clkbug command.
 */
void
refclock_buginfo(
	sockaddr_u *srcadr,	/* clock address */
	struct refclockbug *bug /* output structure */
	)
{
	struct peer *peer;
	struct refclockproc *pp;
	int clktype;
	int unit;
	unsigned u;

	/*
	 * Check for valid address and peer structure
	 */
	if (!ISREFCLOCKADR(srcadr))
		return;

	clktype = (u_char) REFCLOCKTYPE(srcadr);
	unit = REFCLOCKUNIT(srcadr);

	peer = findexistingpeer(srcadr, NULL, NULL, -1, 0, NULL);

	if (NULL == peer || NULL == peer->procptr)
		return;

	pp = peer->procptr;

	/*
	 * Copy structure values
	 */
	bug->nvalues = 8;
	bug->svalues = 0x0000003f;
	bug->values[0] = pp->year;
	bug->values[1] = pp->day;
	bug->values[2] = pp->hour;
	bug->values[3] = pp->minute;
	bug->values[4] = pp->second;
	bug->values[5] = pp->nsec;
	bug->values[6] = pp->yearstart;
	bug->values[7] = pp->coderecv;
	bug->stimes = 0xfffffffc;
	bug->times[0] = pp->lastref;
	bug->times[1] = pp->lastrec;
	for (u = 2; u < bug->ntimes; u++)
		DTOLFP(pp->filter[u - 2], &bug->times[u]);

	/*
	 * Give the stuff to the clock
	 */
	if (refclock_conf[clktype]->clock_buginfo != noentry)
		(refclock_conf[clktype]->clock_buginfo)(unit, bug, peer);
}


#ifdef HAVE_PPSAPI
/*
 * refclock_ppsapi - initialize/update ppsapi
 *
 * This routine is called after the fudge command to open the PPSAPI
 * interface for later parameter setting after the fudge command.
 */
int
refclock_ppsapi(
	int	fddev,			/* fd device */
	struct refclock_atom *ap	/* atom structure pointer */
	)
{
	if (ap->handle == 0) {
		if (time_pps_create(fddev, &ap->handle) < 0) {
			msyslog(LOG_ERR,
			    "refclock_ppsapi: time_pps_create: %m");
			return (0);
		}
		ZERO(ap->ts); /* [Bug 2689] defined INIT state */
	}
	return (1);
}


/*
 * refclock_params - set ppsapi parameters
 *
 * This routine is called to set the PPSAPI parameters after the fudge
 * command.
 */
int
refclock_params(
	int	mode,			/* mode bits */
	struct refclock_atom *ap	/* atom structure pointer */
	)
{
	ZERO(ap->pps_params);
	ap->pps_params.api_version = PPS_API_VERS_1;

	/*
	 * Solaris serial ports provide PPS pulse capture only on the
	 * assert edge. FreeBSD serial ports provide capture on the
	 * clear edge, while FreeBSD parallel ports provide capture
	 * on the assert edge. Your mileage may vary.
	 */
	if (mode & CLK_FLAG2)
		ap->pps_params.mode = PPS_TSFMT_TSPEC | PPS_CAPTURECLEAR;
	else
		ap->pps_params.mode = PPS_TSFMT_TSPEC | PPS_CAPTUREASSERT;
	if (time_pps_setparams(ap->handle, &ap->pps_params) < 0) {
		msyslog(LOG_ERR,
		    "refclock_params: time_pps_setparams: %m");
		return (0);
	}

	/*
	 * If flag3 is lit, select the kernel PPS if we can.
	 *
	 * Note: EOPNOTSUPP is the only 'legal' error code we deal with;
	 * it is part of the 'if we can' strategy.  Any other error
	 * indicates something more sinister and makes this function fail.
	 */
	if (mode & CLK_FLAG3) {
		if (time_pps_kcbind(ap->handle, PPS_KC_HARDPPS,
		    ap->pps_params.mode & ~PPS_TSFMT_TSPEC,
		    PPS_TSFMT_TSPEC) < 0)
		{
			if (errno != EOPNOTSUPP) { 
				msyslog(LOG_ERR,
					"refclock_params: time_pps_kcbind: %m");
				return (0);
			}
		} else {
			hardpps_enable = 1;
		}
	}
	return (1);
}


/*
 * refclock_pps - called once per second
 *
 * This routine is called once per second. It snatches the PPS
 * timestamp from the kernel and saves the sign-extended fraction in
 * a circular buffer for processing at the next poll event.
 */
int
refclock_pps(
	struct peer *peer,		/* peer structure pointer */
	struct refclock_atom *ap,	/* atom structure pointer */
	int	mode			/* mode bits */	
	)
{
	struct refclockproc *pp;
	pps_info_t pps_info;
	struct timespec timeout;
	double	dtemp, dcorr, trash;

	/*
	 * We require the clock to be synchronized before setting the
	 * parameters. When the parameters have been set, fetch the
	 * most recent PPS timestamp.
	 */ 
	pp = peer->procptr;
	if (ap->handle == 0)
		return (0);

	if (ap->pps_params.mode == 0 && sys_leap != LEAP_NOTINSYNC) {
		if (refclock_params(pp->sloppyclockflag, ap) < 1)
			return (0);
	}
	ZERO(timeout);
	ZERO(pps_info);
	if (time_pps_fetch(ap->handle, PPS_TSFMT_TSPEC, &pps_info,
	    &timeout) < 0) {
		refclock_report(peer, CEVNT_FAULT);
		return (0);
	}
	timeout = ap->ts;	/* save old timestamp for check */
	if (ap->pps_params.mode & PPS_CAPTUREASSERT)
		ap->ts = pps_info.assert_timestamp;
	else if (ap->pps_params.mode & PPS_CAPTURECLEAR)
		ap->ts = pps_info.clear_timestamp;
	else
		return (0);

	/* [Bug 2689] Discard the first sample we read -- if the PPS
	 * source is currently down / disconnected, we have read a
	 * potentially *very* stale value here. So if our old TS value
	 * is all-zero, we consider this sample unrealiable and drop it.
	 *
	 * Note 1: a better check would compare the PPS time stamp to
	 * the current system time and drop it if it's more than say 3s
	 * away.
	 *
	 * Note 2: If we ever again get an all-zero PPS sample, the next
	 * one will be discarded. This can happen every 136yrs and is
	 * unlikely to be ever observed.
	 */
	if (0 == (timeout.tv_sec | timeout.tv_nsec))
		return (0);

	/* If the PPS source fails to deliver a new sample between
	 * polls, it regurgitates the last sample. We do not want to
	 * process the same sample multiple times.
	 */
	if (0 == memcmp(&timeout, &ap->ts, sizeof(timeout)))
		return (0);

	/*
	 * Convert to signed fraction offset, apply fudge and properly
	 * fold the correction into the [-0.5s,0.5s] range. Handle
	 * excessive fudge times, too.
	 */
	dtemp = ap->ts.tv_nsec / 1e9;
	dcorr = modf((pp->fudgetime1 - dtemp), &trash);
	if (dcorr > 0.5)
		dcorr -= 1.0;
	else if (dcorr < -0.5)
		dcorr += 1.0;

	/* phase gate check: avoid wobbling by +/-1s when too close to
	 * the switch-over point. We allow +/-400ms max phase deviation.
	 * The trade-off is clear: The smaller the limit, the less
	 * sensitive to sampling noise the clock becomes. OTOH the
	 * system must get into phase gate range by other means for the
	 * PPS clock to lock in.
	 */
	if (fabs(dcorr) > 0.4)
		return (0);

	/*
	 * record this time stamp and stuff in median filter
	 */
	pp->lastrec.l_ui = (u_int32)ap->ts.tv_sec + JAN_1970;
	pp->lastrec.l_uf = (u_int32)(dtemp * FRAC);
	SAMPLE(dcorr);
	
#ifdef DEBUG
	if (debug > 1)
		printf("refclock_pps: %lu %f %f\n", current_time,
		    dcorr, pp->fudgetime1);
#endif
	return (1);
}
#endif /* HAVE_PPSAPI */
#endif /* REFCLOCK */
