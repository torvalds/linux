/*
 * refclock_gpsdjson.c - clock driver as GPSD JSON client
 *	Juergen Perlinger (perlinger@ntp.org)
 *	Feb 11, 2014 for the NTP project.
 *      The contents of 'html/copyright.html' apply.
 *
 *	Heavily inspired by refclock_nmea.c
 *
 * Special thanks to Gary Miller and Hal Murray for their comments and
 * ideas.
 *
 * Note: This will currently NOT work with Windows due to some
 * limitations:
 *
 *  - There is no GPSD for Windows. (There is an unofficial port to
 *    cygwin, but Windows is not officially supported.)
 *
 *  - To work properly, this driver needs PPS and TPV/TOFF sentences
 *    from GPSD. I don't see how the cygwin port should deal with the
 *    PPS signal.
 *
 *  - The device name matching must be done in a different way for
 *    Windows. (Can be done with COMxx matching, as done for NMEA.)
 *
 * Apart from those minor hickups, once GPSD has been fully ported to
 * Windows, there's no reason why this should not work there ;-) If this
 * is ever to happen at all is a different question.
 *
 * ---------------------------------------------------------------------
 *
 * This driver works slightly different from most others, as the PPS
 * information (if available) is also coming from GPSD via the data
 * connection. This makes using both the PPS data and the serial data
 * easier, but OTOH it's not possible to use the ATOM driver to feed a
 * raw PPS stream to the core of NTPD.
 *
 * To go around this, the driver can use a secondary clock unit
 * (units>=128) that operate in tandem with the primary clock unit
 * (unit%128). The primary clock unit does all the IO stuff and data
 * decoding; if a a secondary unit is attached to a primary unit, this
 * secondary unit is feed with the PPS samples only and can act as a PPS
 * source to the clock selection.
 *
 * The drawback is that the primary unit must be present for the
 * secondary unit to work.
 *
 * This design is a compromise to reduce the IO load for both NTPD and
 * GPSD; it also ensures that data is transmitted and evaluated only
 * once on the side of NTPD.
 *
 * ---------------------------------------------------------------------
 *
 * trouble shooting hints:
 *
 *   Enable and check the clock stats. Check if there are bad replies;
 *   there should be none. If there are actually bad replies, then the
 *   driver cannot parse all JSON records from GPSD, and some record
 *   types are vital for the operation of the driver. This indicates a
 *   problem on the protocol level.
 *
 *   When started on the command line with a debug level >= 2, the
 *   driver dumps the raw received data and the parser input to
 *   stdout. Since the debug level is global, NTPD starts to create a
 *   *lot* of output. It makes sense to pipe it through '(f)grep
 *   GPSD_JSON' before writing the result to disk.
 *
 *   A bit less intrusive is using netcat or telnet to connect to GPSD
 *   and snoop what NTPD would get. If you try this, you have to send a
 *   WATCH command to GPSD:
 *
 * ?WATCH={"device":"/dev/gps0","enable":true,"json":true,"pps":true};<CRLF>
 *
 *   should show you what GPSD has to say to NTPD. Replace "/dev/gps0"
 *   with the device link used by GPSD, if necessary.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ntp_types.h"

#if defined(REFCLOCK) && defined(CLOCK_GPSDJSON) && !defined(SYS_WINNT)

/* =====================================================================
 * Get the little JSMN library directly into our guts. Use the 'parent
 * link' feature for maximum speed.
 */
#define JSMN_PARENT_LINKS
#include "../libjsmn/jsmn.c"

/* =====================================================================
 * JSON parsing stuff
 */

#define JSMN_MAXTOK	350
#define INVALID_TOKEN (-1)

typedef struct json_ctx {
	char        * buf;
	int           ntok;
	jsmntok_t     tok[JSMN_MAXTOK];
} json_ctx;

typedef int tok_ref;

/* Not all targets have 'long long', and not all of them have 'strtoll'.
 * Sigh. We roll our own integer number parser.
 */
#ifdef HAVE_LONG_LONG
typedef signed   long long int json_int;
typedef unsigned long long int json_uint;
#define JSON_INT_MAX LLONG_MAX
#define JSON_INT_MIN LLONG_MIN
#else
typedef signed   long int json_int;
typedef unsigned long int json_uint;
#define JSON_INT_MAX LONG_MAX
#define JSON_INT_MIN LONG_MIN
#endif

/* =====================================================================
 * header stuff we need
 */

#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/tcp.h>

#if defined(HAVE_SYS_POLL_H)
# include <sys/poll.h>
#elif defined(HAVE_SYS_SELECT_H)
# include <sys/select.h>
#else
# error need poll() or select()
#endif

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_unixtime.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"
#include "ntp_calendar.h"
#include "timespecops.h"

/* get operation modes from mode word.

 * + SERIAL (default) evaluates only serial time information ('STI') as
 *   provided by TPV and TOFF records. TPV evaluation suffers from a
 *   bigger jitter than TOFF, sine it does not contain the receive time
 *   from GPSD and therefore the receive time of NTPD must be
 *   substituted for it. The network latency makes this a second rate
 *   guess.
 *
 *   If TOFF records are detected in the data stream, the timing
 *   information is gleaned from this record -- it contains the local
 *   receive time stamp from GPSD and therefore eliminates the
 *   transmission latency between GPSD and NTPD. The timing information
 *   from TPV is ignored once a TOFF is detected or expected.
 *
 *   TPV is still used to check the fix status, so the driver can stop
 *   feeding samples when GPSD says that the time information is
 *   effectively unreliable.
 *
 * + STRICT means only feed clock samples when a valid STI/PPS pair is
 *   available. Combines the reference time from STI with the pulse time
 *   from PPS. Masks the serial data jitter as long PPS is available,
 *   but can rapidly deteriorate once PPS drops out.
 *
 * + AUTO tries to use STI/PPS pairs if available for some time, and if
 *   this fails for too long switches back to STI only until the PPS
 *   signal becomes available again. See the HTML docs for this driver
 *   about the gotchas and why this is not the default.
 */
#define MODE_OP_MASK   0x03
#define MODE_OP_STI    0
#define MODE_OP_STRICT 1
#define MODE_OP_AUTO   2
#define MODE_OP_MAXVAL 2
#define MODE_OP_MODE(x)		((x) & MODE_OP_MASK)

#define	PRECISION	(-9)	/* precision assumed (about 2 ms) */
#define	PPS_PRECISION	(-20)	/* precision assumed (about 1 us) */
#define	REFID		"GPSD"	/* reference id */
#define	DESCRIPTION	"GPSD JSON client clock" /* who we are */

#define MAX_PDU_LEN	1600
#define TICKOVER_LOW	10
#define TICKOVER_HIGH	120
#define LOGTHROTTLE	3600

/* Primary channel PPS avilability dance:
 * Every good PPS sample gets us a credit of PPS_INCCOUNT points, every
 * bad/missing PPS sample costs us a debit of PPS_DECCOUNT points. When
 * the account reaches the upper limit we change to a mode where only
 * PPS-augmented samples are fed to the core; when the account drops to
 * zero we switch to a mode where TPV-only timestamps are fed to the
 * core.
 * This reduces the chance of rapid alternation between raw and
 * PPS-augmented time stamps.
 */
#define PPS_MAXCOUNT	60	/* upper limit of account  */
#define PPS_INCCOUNT     3	/* credit for good samples */
#define PPS_DECCOUNT     1	/* debit for bad samples   */

/* The secondary (PPS) channel uses a different strategy to avoid old
 * PPS samples in the median filter.
 */
#define PPS2_MAXCOUNT 10

#ifndef BOOL
# define BOOL int
#endif
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

#define PROTO_VERSION(hi,lo) \
	    ((((uint32_t)(hi) << 16) & 0xFFFF0000u) | \
	     ((uint32_t)(lo) & 0x0FFFFu))

/* some local typedefs: The NTPD formatting style cries for short type
 * names, and we provide them locally. Note:the suffix '_t' is reserved
 * for the standard; I use a capital T instead.
 */
typedef struct peer         peerT;
typedef struct refclockproc clockprocT;
typedef struct addrinfo     addrinfoT;

/* =====================================================================
 * We use the same device name scheme as does the NMEA driver; since
 * GPSD supports the same links, we can select devices by a fixed name.
 */
static const char * s_dev_stem = "/dev/gps";

/* =====================================================================
 * forward declarations for transfer vector and the vector itself
 */

static	void	gpsd_init	(void);
static	int	gpsd_start	(int, peerT *);
static	void	gpsd_shutdown	(int, peerT *);
static	void	gpsd_receive	(struct recvbuf *);
static	void	gpsd_poll	(int, peerT *);
static	void	gpsd_control	(int, const struct refclockstat *,
				 struct refclockstat *, peerT *);
static	void	gpsd_timer	(int, peerT *);

static  int     myasprintf(char**, char const*, ...) NTP_PRINTF(2, 3);

static void     enter_opmode(peerT *peer, int mode);
static void	leave_opmode(peerT *peer, int mode);

struct refclock refclock_gpsdjson = {
	gpsd_start,		/* start up driver */
	gpsd_shutdown,		/* shut down driver */
	gpsd_poll,		/* transmit poll message */
	gpsd_control,		/* fudge control */
	gpsd_init,		/* initialize driver */
	noentry,		/* buginfo */
	gpsd_timer		/* called once per second */
};

/* =====================================================================
 * our local clock unit and data
 */
struct gpsd_unit;
typedef struct gpsd_unit gpsd_unitT;

struct gpsd_unit {
	/* links for sharing between master/slave units */
	gpsd_unitT *next_unit;
	size_t      refcount;

	/* data for the secondary PPS channel */
	peerT      *pps_peer;

	/* unit and operation modes */
	int      unit;
	int      mode;
	char    *logname;	/* cached name for log/print */
	char    * device;	/* device name of unit */

	/* current line protocol version */
	uint32_t proto_version;

	/* PPS time stamps primary + secondary channel */
	l_fp pps_local;	/* when we received the PPS message */
	l_fp pps_stamp;	/* related reference time */
	l_fp pps_recvt;	/* when GPSD detected the pulse */
	l_fp pps_stamp2;/* related reference time (secondary) */
	l_fp pps_recvt2;/* when GPSD detected the pulse (secondary)*/
	int  ppscount;	/* PPS counter (primary unit) */
	int  ppscount2;	/* PPS counter (secondary unit) */

	/* TPV or TOFF serial time information */
	l_fp sti_local;	/* when we received the TPV/TOFF message */
	l_fp sti_stamp;	/* effective GPS time stamp */
	l_fp sti_recvt;	/* when GPSD got the fix */

	/* precision estimates */
	int16_t	    sti_prec;	/* serial precision based on EPT */
	int16_t     pps_prec;	/* PPS precision from GPSD or above */

	/* fudge values for correction, mirrored as 'l_fp' */
	l_fp pps_fudge;		/* PPS fudge primary channel */
	l_fp pps_fudge2;	/* PPS fudge secondary channel */
	l_fp sti_fudge;		/* TPV/TOFF serial data fudge */

	/* Flags to indicate available data */
	int fl_nosync: 1;	/* GPSD signals bad quality */
	int fl_sti   : 1;	/* valid TPV/TOFF seen (have time) */
	int fl_pps   : 1;	/* valid pulse seen */
	int fl_pps2  : 1;	/* valid pulse seen for PPS channel */
	int fl_rawsti: 1;	/* permit raw TPV/TOFF time stamps */
	int fl_vers  : 1;	/* have protocol version */
	int fl_watch : 1;	/* watch reply seen */
	/* protocol flags */
	int pf_nsec  : 1;	/* have nanosec PPS info */
	int pf_toff  : 1;	/* have TOFF record for timing */

	/* admin stuff for sockets and device selection */
	int         fdt;	/* current connecting socket */
	addrinfoT * addr;	/* next address to try */
	u_int       tickover;	/* timeout countdown */
	u_int       tickpres;	/* timeout preset */

	/* tallies for the various events */
	u_int       tc_recv;	/* received known records */
	u_int       tc_breply;	/* bad replies / parsing errors */
	u_int       tc_nosync;	/* TPV / sample cycles w/o fix */
	u_int       tc_sti_recv;/* received serial time info records */
	u_int       tc_sti_used;/* used        --^-- */
	u_int       tc_pps_recv;/* received PPS timing info records */
	u_int       tc_pps_used;/* used        --^-- */

	/* log bloat throttle */
	u_int       logthrottle;/* seconds to next log slot */

	/* The parse context for the current record */
	json_ctx    json_parse;

	/* record assemby buffer and saved length */
	int  buflen;
	char buffer[MAX_PDU_LEN];
};

/* =====================================================================
 * static local helpers forward decls
 */
static void gpsd_init_socket(peerT * const peer);
static void gpsd_test_socket(peerT * const peer);
static void gpsd_stop_socket(peerT * const peer);

static void gpsd_parse(peerT * const peer,
		       const l_fp  * const rtime);
static BOOL convert_ascii_time(l_fp * fp, const char * gps_time);
static void save_ltc(clockprocT * const pp, const char * const tc);
static int  syslogok(clockprocT * const pp, gpsd_unitT * const up);
static void log_data(peerT *peer, const char *what,
		     const char *buf, size_t len);
static int16_t clamped_precision(int rawprec);

/* =====================================================================
 * local / static stuff
 */

static const char * const s_req_version =
    "?VERSION;\r\n";

/* We keep a static list of network addresses for 'localhost:gpsd' or a
 * fallback alias of it, and we try to connect to them in round-robin
 * fashion. The service lookup is done during the driver init
 * function to minmise the impact of 'getaddrinfo()'.
 *
 * Alas, the init function is called even if there are no clocks
 * configured for this driver. So it makes sense to defer the logging of
 * any errors or other notifications until the first clock unit is
 * started -- otherwise there might be syslog entries from a driver that
 * is not used at all.
 */
static addrinfoT  *s_gpsd_addr;
static gpsd_unitT *s_clock_units;

/* list of service/socket names we want to resolve against */
static const char * const s_svctab[][2] = {
	{ "localhost", "gpsd" },
	{ "localhost", "2947" },
	{ "127.0.0.1", "2947" },
	{ NULL, NULL }
};

/* list of address resolution errors and index of service entry that
 * finally worked.
 */
static int s_svcerr[sizeof(s_svctab)/sizeof(s_svctab[0])];
static int s_svcidx;

/* =====================================================================
 * log throttling
 */
static int/*BOOL*/
syslogok(
	clockprocT * const pp,
	gpsd_unitT * const up)
{
	int res = (0 != (pp->sloppyclockflag & CLK_FLAG3))
	       || (0           == up->logthrottle )
	       || (LOGTHROTTLE == up->logthrottle );
	if (res)
		up->logthrottle = LOGTHROTTLE;
	return res;
}

/* =====================================================================
 * the clock functions
 */

/* ---------------------------------------------------------------------
 * Init: This currently just gets the socket address for the GPS daemon
 */
static void
gpsd_init(void)
{
	addrinfoT   hints;
	int         rc, idx;

	memset(s_svcerr, 0, sizeof(s_svcerr));
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_socktype = SOCK_STREAM;

	for (idx = 0; s_svctab[idx][0] && !s_gpsd_addr; idx++) {
		rc = getaddrinfo(s_svctab[idx][0], s_svctab[idx][1],
				 &hints, &s_gpsd_addr);
		s_svcerr[idx] = rc;
		if (0 == rc)
			break;
		s_gpsd_addr = NULL;
	}
	s_svcidx = idx;
}

/* ---------------------------------------------------------------------
 * Init Check: flush pending log messages and check if we can proceed
 */
static int/*BOOL*/
gpsd_init_check(void)
{
	int idx;

	/* Check if there is something to log */
	if (s_svcidx == 0)
		return (s_gpsd_addr != NULL);

	/* spool out the resolver errors */
	for (idx = 0; idx < s_svcidx; ++idx) {
		msyslog(LOG_WARNING,
			"GPSD_JSON: failed to resolve '%s:%s', rc=%d (%s)",
			s_svctab[idx][0], s_svctab[idx][1],
			s_svcerr[idx], gai_strerror(s_svcerr[idx]));
	}

	/* check if it was fatal, or if we can proceed */
	if (s_gpsd_addr == NULL)
		msyslog(LOG_ERR, "%s",
			"GPSD_JSON: failed to get socket address, giving up.");
	else if (idx != 0)
		msyslog(LOG_WARNING,
			"GPSD_JSON: using '%s:%s' instead of '%s:%s'",
			s_svctab[idx][0], s_svctab[idx][1],
			s_svctab[0][0], s_svctab[0][1]);

	/* make sure this gets logged only once and tell if we can
	 * proceed or not
	 */
	s_svcidx = 0;
	return (s_gpsd_addr != NULL);
}

/* ---------------------------------------------------------------------
 * Start: allocate a unit pointer and set up the runtime data
 */
static int
gpsd_start(
	int     unit,
	peerT * peer)
{
	clockprocT  * const pp = peer->procptr;
	gpsd_unitT  * up;
	gpsd_unitT ** uscan    = &s_clock_units;

	struct stat sb;

	/* check if we can proceed at all or if init failed */
	if ( ! gpsd_init_check())
		return FALSE;

	/* search for matching unit */
	while ((up = *uscan) != NULL && up->unit != (unit & 0x7F))
		uscan = &up->next_unit;
	if (up == NULL) {
		/* alloc unit, add to list and increment use count ASAP. */
		up = emalloc_zero(sizeof(*up));
		*uscan = up;
		++up->refcount;

		/* initialize the unit structure */
		up->logname  = estrdup(refnumtoa(&peer->srcadr));
		up->unit     = unit & 0x7F;
		up->fdt      = -1;
		up->addr     = s_gpsd_addr;
		up->tickpres = TICKOVER_LOW;

		/* Create the device name and check for a Character
		 * Device. It's assumed that GPSD was started with the
		 * same link, so the names match. (If this is not
		 * practicable, we will have to read the symlink, if
		 * any, so we can get the true device file.)
		 */
		if (-1 == myasprintf(&up->device, "%s%u",
				     s_dev_stem, up->unit)) {
			msyslog(LOG_ERR, "%s: clock device name too long",
				up->logname);
			goto dev_fail;
		}
		if (-1 == stat(up->device, &sb) || !S_ISCHR(sb.st_mode)) {
			msyslog(LOG_ERR, "%s: '%s' is not a character device",
				up->logname, up->device);
			goto dev_fail;
		}
	} else {
		/* All set up, just increment use count. */
		++up->refcount;
	}
	
	/* setup refclock processing */
	pp->unitptr = (caddr_t)up;
	pp->io.fd         = -1;
	pp->io.clock_recv = gpsd_receive;
	pp->io.srcclock   = peer;
	pp->io.datalen    = 0;
	pp->a_lastcode[0] = '\0';
	pp->lencode       = 0;
	pp->clockdesc     = DESCRIPTION;
	memcpy(&pp->refid, REFID, 4);

	/* Initialize miscellaneous variables */
	if (unit >= 128)
		peer->precision = PPS_PRECISION;
	else
		peer->precision = PRECISION;

	/* If the daemon name lookup failed, just give up now. */
	if (NULL == up->addr) {
		msyslog(LOG_ERR, "%s: no GPSD socket address, giving up",
			up->logname);
		goto dev_fail;
	}

	LOGIF(CLOCKINFO,
	      (LOG_NOTICE, "%s: startup, device is '%s'",
	       refnumtoa(&peer->srcadr), up->device));
	up->mode = MODE_OP_MODE(peer->ttl);
	if (up->mode > MODE_OP_MAXVAL)
		up->mode = 0;
	if (unit >= 128)
		up->pps_peer = peer;
	else
		enter_opmode(peer, up->mode);
	return TRUE;

dev_fail:
	/* On failure, remove all UNIT ressources and declare defeat. */

	INSIST (up);
	if (!--up->refcount) {
		*uscan = up->next_unit;
		free(up->device);
		free(up);
	}

	pp->unitptr = (caddr_t)NULL;
	return FALSE;
}

/* ------------------------------------------------------------------ */

static void
gpsd_shutdown(
	int     unit,
	peerT * peer)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;
	gpsd_unitT ** uscan   = &s_clock_units;

	UNUSED_ARG(unit);

	/* The unit pointer might have been removed already. */
	if (up == NULL)
		return;

	/* now check if we must close IO resources */
	if (peer != up->pps_peer) {
		if (-1 != pp->io.fd) {
			DPRINTF(1, ("%s: closing clock, fd=%d\n",
				    up->logname, pp->io.fd));
			io_closeclock(&pp->io);
			pp->io.fd = -1;
		}
		if (up->fdt != -1)
			close(up->fdt);
	}
	/* decrement use count and eventually remove this unit. */
	if (!--up->refcount) {
		/* unlink this unit */
		while (*uscan != NULL)
			if (*uscan == up)
				*uscan = up->next_unit;
			else
				uscan = &(*uscan)->next_unit;
		free(up->logname);
		free(up->device);
		free(up);
	}
	pp->unitptr = (caddr_t)NULL;
	LOGIF(CLOCKINFO,
	      (LOG_NOTICE, "%s: shutdown", refnumtoa(&peer->srcadr)));
}

/* ------------------------------------------------------------------ */

static void
gpsd_receive(
	struct recvbuf * rbufp)
{
	/* declare & init control structure ptrs */
	peerT	   * const peer = rbufp->recv_peer;
	clockprocT * const pp   = peer->procptr;
	gpsd_unitT * const up   = (gpsd_unitT *)pp->unitptr;

	const char *psrc, *esrc;
	char       *pdst, *edst, ch;

	/* log the data stream, if this is enabled */
	log_data(peer, "recv", (const char*)rbufp->recv_buffer,
		 (size_t)rbufp->recv_length);


	/* Since we're getting a raw stream data, we must assemble lines
	 * in our receive buffer. We can't use neither 'refclock_gtraw'
	 * not 'refclock_gtlin' here...  We process chars until we reach
	 * an EoL (that is, line feed) but we truncate the message if it
	 * does not fit the buffer.  GPSD might truncate messages, too,
	 * so dealing with truncated buffers is necessary anyway.
	 */
	psrc = (const char*)rbufp->recv_buffer;
	esrc = psrc + rbufp->recv_length;

	pdst = up->buffer + up->buflen;
	edst = pdst + sizeof(up->buffer) - 1; /* for trailing NUL */

	while (psrc != esrc) {
		ch = *psrc++;
		if (ch == '\n') {
			/* trim trailing whitespace & terminate buffer */
			while (pdst != up->buffer && pdst[-1] <= ' ')
				--pdst;
			*pdst = '\0';
			/* process data and reset buffer */
			up->buflen = pdst - up->buffer;
			gpsd_parse(peer, &rbufp->recv_time);
			pdst = up->buffer;
		} else if (pdst != edst) {
			/* add next char, ignoring leading whitespace */
			if (ch > ' ' || pdst != up->buffer)
				*pdst++ = ch;
		}
	}
	up->buflen   = pdst - up->buffer;
	up->tickover = TICKOVER_LOW;
}

/* ------------------------------------------------------------------ */

static void
poll_primary(
	peerT      * const peer ,
	clockprocT * const pp   ,
	gpsd_unitT * const up   )
{
	if (pp->coderecv != pp->codeproc) {
		/* all is well */
		pp->lastref = pp->lastrec;
		refclock_report(peer, CEVNT_NOMINAL);
		refclock_receive(peer);
	} else {
		/* Not working properly, admit to it. If we have no
		 * connection to GPSD, declare the clock as faulty. If
		 * there were bad replies, this is handled as the major
		 * cause, and everything else is just a timeout.
		 */
		peer->precision = PRECISION;
		if (-1 == pp->io.fd)
			refclock_report(peer, CEVNT_FAULT);
		else if (0 != up->tc_breply)
			refclock_report(peer, CEVNT_BADREPLY);
		else
			refclock_report(peer, CEVNT_TIMEOUT);
	}

	if (pp->sloppyclockflag & CLK_FLAG4)
		mprintf_clock_stats(
			&peer->srcadr,"%u %u %u %u %u %u %u",
			up->tc_recv,
			up->tc_breply, up->tc_nosync,
			up->tc_sti_recv, up->tc_sti_used,
			up->tc_pps_recv, up->tc_pps_used);

	/* clear tallies for next round */
	up->tc_breply   = 0;
	up->tc_recv     = 0;
	up->tc_nosync   = 0;
	up->tc_sti_recv = 0;
	up->tc_sti_used = 0;
	up->tc_pps_recv = 0;
	up->tc_pps_used = 0;
}

static void
poll_secondary(
	peerT      * const peer ,
	clockprocT * const pp   ,
	gpsd_unitT * const up   )
{
	if (pp->coderecv != pp->codeproc) {
		/* all is well */
		pp->lastref = pp->lastrec;
		refclock_report(peer, CEVNT_NOMINAL);
		refclock_receive(peer);
	} else {
		peer->precision = PPS_PRECISION;
		peer->flags &= ~FLAG_PPS;
		refclock_report(peer, CEVNT_TIMEOUT);
	}
}

static void
gpsd_poll(
	int     unit,
	peerT * peer)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	++pp->polls;
	if (peer == up->pps_peer)
		poll_secondary(peer, pp, up);
	else
		poll_primary(peer, pp, up);
}

/* ------------------------------------------------------------------ */

static void
gpsd_control(
	int                         unit,
	const struct refclockstat * in_st,
	struct refclockstat       * out_st,
	peerT                     * peer  )
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	if (peer == up->pps_peer) {
		DTOLFP(pp->fudgetime1, &up->pps_fudge2);
		if ( ! (pp->sloppyclockflag & CLK_FLAG1))
			peer->flags &= ~FLAG_PPS;
	} else {
		/* save preprocessed fudge times */
		DTOLFP(pp->fudgetime1, &up->pps_fudge);
		DTOLFP(pp->fudgetime2, &up->sti_fudge);

		if (MODE_OP_MODE(up->mode ^ peer->ttl)) {
			leave_opmode(peer, up->mode);
			up->mode = MODE_OP_MODE(peer->ttl);
			enter_opmode(peer, up->mode);
		}
	}
 }

/* ------------------------------------------------------------------ */

static void
timer_primary(
	peerT      * const peer ,
	clockprocT * const pp   ,
	gpsd_unitT * const up   )
{
	int rc;

	/* This is used for timeout handling. Nothing that needs
	 * sub-second precison happens here, so receive/connect/retry
	 * timeouts are simply handled by a count down, and then we
	 * decide what to do by the socket values.
	 *
	 * Note that the timer stays at zero here, unless some of the
	 * functions set it to another value.
	 */
	if (up->logthrottle)
		--up->logthrottle;
	if (up->tickover)
		--up->tickover;
	switch (up->tickover) {
	case 4:
		/* If we are connected to GPSD, try to get a live signal
		 * by querying the version. Otherwise just check the
		 * socket to become ready.
		 */
		if (-1 != pp->io.fd) {
			size_t rlen = strlen(s_req_version);
			DPRINTF(2, ("%s: timer livecheck: '%s'\n",
				    up->logname, s_req_version));
			log_data(peer, "send", s_req_version, rlen);
			rc = write(pp->io.fd, s_req_version, rlen);
			(void)rc;
		} else if (-1 != up->fdt) {
			gpsd_test_socket(peer);
		}
		break;

	case 0:
		if (-1 != pp->io.fd)
			gpsd_stop_socket(peer);
		else if (-1 != up->fdt)
			gpsd_test_socket(peer);
		else if (NULL != s_gpsd_addr)
			gpsd_init_socket(peer);
		break;

	default:
		if (-1 == pp->io.fd && -1 != up->fdt)
			gpsd_test_socket(peer);
	}
}

static void
timer_secondary(
	peerT      * const peer ,
	clockprocT * const pp   ,
	gpsd_unitT * const up   )
{
	/* Reduce the count by one. Flush sample buffer and clear PPS
	 * flag when this happens.
	 */
	up->ppscount2 = max(0, (up->ppscount2 - 1));
	if (0 == up->ppscount2) {
		if (pp->coderecv != pp->codeproc) {
			refclock_report(peer, CEVNT_TIMEOUT);
			pp->coderecv = pp->codeproc;
		}
		peer->flags &= ~FLAG_PPS;
	}
}

static void
gpsd_timer(
	int     unit,
	peerT * peer)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	if (peer == up->pps_peer)
		timer_secondary(peer, pp, up);
	else
		timer_primary(peer, pp, up);
}

/* =====================================================================
 * handle opmode switches
 */

static void
enter_opmode(
	peerT *peer,
	int    mode)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	DPRINTF(1, ("%s: enter operation mode %d\n",
		    up->logname, MODE_OP_MODE(mode)));

	if (MODE_OP_MODE(mode) == MODE_OP_AUTO) {
		up->fl_rawsti = 0;
		up->ppscount  = PPS_MAXCOUNT / 2;
	}
	up->fl_pps = 0;
	up->fl_sti = 0;
}

/* ------------------------------------------------------------------ */

static void
leave_opmode(
	peerT *peer,
	int    mode)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	DPRINTF(1, ("%s: leaving operation mode %d\n",
		    up->logname, MODE_OP_MODE(mode)));

	if (MODE_OP_MODE(mode) == MODE_OP_AUTO) {
		up->fl_rawsti = 0;
		up->ppscount  = 0;
	}
	up->fl_pps = 0;
	up->fl_sti = 0;
}

/* =====================================================================
 * operation mode specific evaluation
 */

static void
add_clock_sample(
	peerT      * const peer ,
	clockprocT * const pp   ,
	l_fp               stamp,
	l_fp               recvt)
{
	pp->lastref = stamp;
	if (pp->coderecv == pp->codeproc)
		refclock_report(peer, CEVNT_NOMINAL);
	refclock_process_offset(pp, stamp, recvt, pp->fudgetime1);
}

/* ------------------------------------------------------------------ */

static void
eval_strict(
	peerT      * const peer ,
	clockprocT * const pp   ,
	gpsd_unitT * const up   )
{
	if (up->fl_sti && up->fl_pps) {
		/* use TPV reference time + PPS receive time */
		add_clock_sample(peer, pp, up->sti_stamp, up->pps_recvt);
		peer->precision = up->pps_prec;
		/* both packets consumed now... */
		up->fl_pps = 0;
		up->fl_sti = 0;
		++up->tc_sti_used;
	}
}

/* ------------------------------------------------------------------ */
/* PPS processing for the secondary channel. GPSD provides us with full
 * timing information, so there's no danger of PLL-locking to the wrong
 * second. The belts and suspenders needed for the raw ATOM clock are
 * unnecessary here.
 */
static void
eval_pps_secondary(
	peerT      * const peer ,
	clockprocT * const pp   ,
	gpsd_unitT * const up   )
{
	if (up->fl_pps2) {
		/* feed data */
		add_clock_sample(peer, pp, up->pps_stamp2, up->pps_recvt2);
		peer->precision = up->pps_prec;
		/* PPS peer flag logic */
		up->ppscount2 = min(PPS2_MAXCOUNT, (up->ppscount2 + 2));
		if ((PPS2_MAXCOUNT == up->ppscount2) &&
		    (pp->sloppyclockflag & CLK_FLAG1) )
			peer->flags |= FLAG_PPS;
		/* mark time stamp as burned... */
		up->fl_pps2 = 0;
		++up->tc_pps_used;
	}
}

/* ------------------------------------------------------------------ */

static void
eval_serial(
	peerT      * const peer ,
	clockprocT * const pp   ,
	gpsd_unitT * const up   )
{
	if (up->fl_sti) {
		add_clock_sample(peer, pp, up->sti_stamp, up->sti_recvt);
		peer->precision = up->sti_prec;
		/* mark time stamp as burned... */
		up->fl_sti = 0;
		++up->tc_sti_used;
	}
}

/* ------------------------------------------------------------------ */
static void
eval_auto(
	peerT      * const peer ,
	clockprocT * const pp   ,
	gpsd_unitT * const up   )
{
	/* If there's no TPV available, stop working here... */
	if (!up->fl_sti)
		return;

	/* check how to handle STI+PPS: Can PPS be used to augment STI
	 * (or vice versae), do we drop the sample because there is a
	 * temporary missing PPS signal, or do we feed on STI time
	 * stamps alone?
	 *
	 * Do a counter/threshold dance to decide how to proceed.
	 */
	if (up->fl_pps) {
		up->ppscount = min(PPS_MAXCOUNT,
				   (up->ppscount + PPS_INCCOUNT));
		if ((PPS_MAXCOUNT == up->ppscount) && up->fl_rawsti) {
			up->fl_rawsti = 0;
			msyslog(LOG_INFO,
				"%s: expect valid PPS from now",
				up->logname);
		}
	} else {
		up->ppscount = max(0, (up->ppscount - PPS_DECCOUNT));
		if ((0 == up->ppscount) && !up->fl_rawsti) {
			up->fl_rawsti = -1;
			msyslog(LOG_WARNING,
				"%s: use TPV alone from now",
				up->logname);
		}
	}

	/* now eventually feed the sample */
	if (up->fl_rawsti)
		eval_serial(peer, pp, up);
	else
		eval_strict(peer, pp, up);
}

/* =====================================================================
 * JSON parsing stuff
 */

/* ------------------------------------------------------------------ */
/* Parse a decimal integer with a possible sign. Works like 'strtoll()'
 * or 'strtol()', but with a fixed base of 10 and without eating away
 * leading whitespace. For the error codes, the handling of the end
 * pointer and the return values see 'strtol()'.
 */
static json_int
strtojint(
	const char *cp, char **ep)
{
	json_uint     accu, limit_lo, limit_hi;
	int           flags; /* bit 0: overflow; bit 1: sign */
	const char  * hold;

	/* pointer union to circumvent a tricky/sticky const issue */
	union {	const char * c; char * v; } vep;

	/* store initial value of 'cp' -- see 'strtol()' */
	vep.c = cp;

	/* Eat away an optional sign and set the limits accordingly: The
	 * high limit is the maximum absolute value that can be returned,
	 * and the low limit is the biggest value that does not cause an
	 * overflow when multiplied with 10. Avoid negation overflows.
	 */
	if (*cp == '-') {
		cp += 1;
		flags    = 2;
		limit_hi = (json_uint)-(JSON_INT_MIN + 1) + 1;
	} else {
		cp += (*cp == '+');
		flags    = 0;
		limit_hi = (json_uint)JSON_INT_MAX;
	}
	limit_lo = limit_hi / 10;

	/* Now try to convert a sequence of digits. */
	hold = cp;
	accu = 0;
	while (isdigit(*(const u_char*)cp)) {
		flags |= (accu > limit_lo);
		accu = accu * 10 + (*(const u_char*)cp++ - '0');
		flags |= (accu > limit_hi);
	}
	/* Check for empty conversion (no digits seen). */
	if (hold != cp)
		vep.c = cp;
	else
		errno = EINVAL;	/* accu is still zero */
	/* Check for range overflow */
	if (flags & 1) {
		errno = ERANGE;
		accu  = limit_hi;
	}
	/* If possible, store back the end-of-conversion pointer */
	if (ep)
		*ep = vep.v;
	/* If negative, return the negated result if the accu is not
	 * zero. Avoid negation overflows.
	 */
	if ((flags & 2) && accu)
		return -(json_int)(accu - 1) - 1;
	else
		return (json_int)accu;
}

/* ------------------------------------------------------------------ */

static tok_ref
json_token_skip(
	const json_ctx * ctx,
	tok_ref          tid)
{
	if (tid >= 0 && tid < ctx->ntok) {
		int len = ctx->tok[tid].size;
		/* For arrays and objects, the size is the number of
		 * ITEMS in the compound. Thats the number of objects in
		 * the array, and the number of key/value pairs for
		 * objects. In theory, the key must be a string, and we
		 * could simply skip one token before skipping the
		 * value, which can be anything. We're a bit paranoid
		 * and lazy at the same time: We simply double the
		 * number of tokens to skip and fall through into the
		 * array processing when encountering an object.
		 */
		switch (ctx->tok[tid].type) {
		case JSMN_OBJECT:
			len *= 2;
			/* FALLTHROUGH */
		case JSMN_ARRAY:
			for (++tid; len; --len)
				tid = json_token_skip(ctx, tid);
			break;
			
		default:
			++tid;
			break;
		}
		/* The next condition should never be true, but paranoia
		 * prevails...
		 */
		if (tid < 0 || tid > ctx->ntok)
			tid = ctx->ntok;
	}
	return tid;
}

/* ------------------------------------------------------------------ */

static int
json_object_lookup(
	const json_ctx * ctx ,
	tok_ref          tid ,
	const char     * key ,
	int              what)
{
	int len;

	if (tid < 0 || tid >= ctx->ntok ||
	    ctx->tok[tid].type != JSMN_OBJECT)
		return INVALID_TOKEN;
	
	len = ctx->tok[tid].size;
	for (++tid; len && tid+1 < ctx->ntok; --len) {
		if (ctx->tok[tid].type != JSMN_STRING) { /* Blooper! */
			tid = json_token_skip(ctx, tid); /* skip key */
			tid = json_token_skip(ctx, tid); /* skip val */
		} else if (strcmp(key, ctx->buf + ctx->tok[tid].start)) {
			tid = json_token_skip(ctx, tid+1); /* skip key+val */
		} else if (what < 0 || (u_int)what == ctx->tok[tid+1].type) {
			return tid + 1;
		} else {
			break;
		}
		/* if skipping ahead returned an error, bail out here. */
		if (tid < 0)
			break;
	}
	return INVALID_TOKEN;
}

/* ------------------------------------------------------------------ */

static const char*
json_object_lookup_primitive(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	tid = json_object_lookup(ctx, tid, key, JSMN_PRIMITIVE);
	if (INVALID_TOKEN  != tid)
		return ctx->buf + ctx->tok[tid].start;
	else
		return NULL;
}
/* ------------------------------------------------------------------ */
/* look up a boolean value. This essentially returns a tribool:
 * 0->false, 1->true, (-1)->error/undefined
 */
static int
json_object_lookup_bool(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	const char *cp;
	cp  = json_object_lookup_primitive(ctx, tid, key);
	switch ( cp ? *cp : '\0') {
	case 't': return  1;
	case 'f': return  0;
	default : return -1;
	}
}

/* ------------------------------------------------------------------ */

static const char*
json_object_lookup_string(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	tid = json_object_lookup(ctx, tid, key, JSMN_STRING);
	if (INVALID_TOKEN != tid)
		return ctx->buf + ctx->tok[tid].start;
	return NULL;
}

static const char*
json_object_lookup_string_default(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key,
	const char     * def)
{
	tid = json_object_lookup(ctx, tid, key, JSMN_STRING);
	if (INVALID_TOKEN != tid)
		return ctx->buf + ctx->tok[tid].start;
	return def;
}

/* ------------------------------------------------------------------ */

static json_int
json_object_lookup_int(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	json_int     ret;
	const char * cp;
	char       * ep;

	cp = json_object_lookup_primitive(ctx, tid, key);
	if (NULL != cp) {
		ret = strtojint(cp, &ep);
		if (cp != ep && '\0' == *ep)
			return ret;
	} else {
		errno = EINVAL;
	}
	return 0;
}

static json_int
json_object_lookup_int_default(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key,
	json_int         def)
{
	json_int     ret;
	const char * cp;
	char       * ep;

	cp = json_object_lookup_primitive(ctx, tid, key);
	if (NULL != cp) {
		ret = strtojint(cp, &ep);
		if (cp != ep && '\0' == *ep)
			return ret;
	}
	return def;
}

/* ------------------------------------------------------------------ */
#if 0 /* currently unused */
static double
json_object_lookup_float(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key)
{
	double       ret;
	const char * cp;
	char       * ep;

	cp = json_object_lookup_primitive(ctx, tid, key);
	if (NULL != cp) {
		ret = strtod(cp, &ep);
		if (cp != ep && '\0' == *ep)
			return ret;
	} else {
		errno = EINVAL;
	}
	return 0.0;
}
#endif

static double
json_object_lookup_float_default(
	const json_ctx * ctx,
	tok_ref          tid,
	const char     * key,
	double           def)
{
	double       ret;
	const char * cp;
	char       * ep;

	cp = json_object_lookup_primitive(ctx, tid, key);
	if (NULL != cp) {
		ret = strtod(cp, &ep);
		if (cp != ep && '\0' == *ep)
			return ret;
	}
	return def;
}

/* ------------------------------------------------------------------ */

static BOOL
json_parse_record(
	json_ctx * ctx,
	char     * buf,
	size_t     len)
{
	jsmn_parser jsm;
	int         idx, rc;

	jsmn_init(&jsm);
	rc = jsmn_parse(&jsm, buf, len, ctx->tok, JSMN_MAXTOK);
	if (rc <= 0)
		return FALSE;
	ctx->buf  = buf;
	ctx->ntok = rc;

	if (JSMN_OBJECT != ctx->tok[0].type)
		return FALSE; /* not object!?! */

	/* Make all tokens NUL terminated by overwriting the
	 * terminator symbol. Makes string compares and number parsing a
	 * lot easier!
	 */
	for (idx = 0; idx < ctx->ntok; ++idx)
		if (ctx->tok[idx].end > ctx->tok[idx].start)
			ctx->buf[ctx->tok[idx].end] = '\0';
	return TRUE;
}


/* =====================================================================
 * static local helpers
 */
static BOOL
get_binary_time(
	l_fp       * const dest     ,
	json_ctx   * const jctx     ,
	const char * const time_name,
	const char * const frac_name,
	long               fscale   )
{
	BOOL            retv = FALSE;
	struct timespec ts;

	errno = 0;
	ts.tv_sec  = (time_t)json_object_lookup_int(jctx, 0, time_name);
	ts.tv_nsec = (long  )json_object_lookup_int(jctx, 0, frac_name);
	if (0 == errno) {
		ts.tv_nsec *= fscale;
		*dest = tspec_stamp_to_lfp(ts);
		retv  = TRUE;
	}
	return retv;
}

/* ------------------------------------------------------------------ */
/* Process a WATCH record
 *
 * Currently this is only used to recognise that the device is present
 * and that we're listed subscribers.
 */
static void
process_watch(
	peerT      * const peer ,
	json_ctx   * const jctx ,
	const l_fp * const rtime)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	const char * path;

	path = json_object_lookup_string(jctx, 0, "device");
	if (NULL == path || strcmp(path, up->device))
		return;

	if (json_object_lookup_bool(jctx, 0, "enable") > 0 &&
	    json_object_lookup_bool(jctx, 0, "json"  ) > 0  )
		up->fl_watch = -1;
	else
		up->fl_watch = 0;
	DPRINTF(2, ("%s: process_watch, enabled=%d\n",
		    up->logname, (up->fl_watch & 1)));
}

/* ------------------------------------------------------------------ */

static void
process_version(
	peerT      * const peer ,
	json_ctx   * const jctx ,
	const l_fp * const rtime)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	int    len;
	char * buf;
	const char *revision;
	const char *release;
	uint16_t    pvhi, pvlo;

	/* get protocol version number */
	revision = json_object_lookup_string_default(
		jctx, 0, "rev", "(unknown)");
	release  = json_object_lookup_string_default(
		jctx, 0, "release", "(unknown)");
	errno = 0;
	pvhi = (uint16_t)json_object_lookup_int(jctx, 0, "proto_major");
	pvlo = (uint16_t)json_object_lookup_int(jctx, 0, "proto_minor");

	if (0 == errno) {
		if ( ! up->fl_vers)
			msyslog(LOG_INFO,
				"%s: GPSD revision=%s release=%s protocol=%u.%u",
				up->logname, revision, release,
				pvhi, pvlo);
		up->proto_version = PROTO_VERSION(pvhi, pvlo);
		up->fl_vers = -1;
	} else {
		if (syslogok(pp, up))
			msyslog(LOG_INFO,
				"%s: could not evaluate version data",
				up->logname);
		return;
	}
	/* With the 3.9 GPSD protocol, '*_musec' vanished from the PPS
	 * record and was replace by '*_nsec'.
	 */
	up->pf_nsec = -(up->proto_version >= PROTO_VERSION(3,9));

	/* With the 3.10 protocol we can get TOFF records for better
	 * timing information.
	 */
	up->pf_toff = -(up->proto_version >= PROTO_VERSION(3,10));

	/* request watch for our GPS device if not yet watched.
	 *
	 * The version string is also sent as a life signal, if we have
	 * seen useable data. So if we're already watching the device,
	 * skip the request.
	 *
	 * Reuse the input buffer, which is no longer needed in the
	 * current cycle. Also assume that we can write the watch
	 * request in one sweep into the socket; since we do not do
	 * output otherwise, this should always work.  (Unless the
	 * TCP/IP window size gets lower than the length of the
	 * request. We handle that when it happens.)
	 */
	if (up->fl_watch)
		return;

	/* The logon string is actually the ?WATCH command of GPSD,
	 * using JSON data and selecting the GPS device name we created
	 * from our unit number. We have an old a newer version that
	 * request PPS (and TOFF) transmission.
	 */
	snprintf(up->buffer, sizeof(up->buffer),
		 "?WATCH={\"device\":\"%s\",\"enable\":true,\"json\":true%s};\r\n",
		 up->device, (up->pf_toff ? ",\"pps\":true" : ""));
	buf = up->buffer;
	len = strlen(buf);
	log_data(peer, "send", buf, len);
	if (len != write(pp->io.fd, buf, len) && (syslogok(pp, up))) {
		/* Note: if the server fails to read our request, the
		 * resulting data timeout will take care of the
		 * connection!
		 */
		msyslog(LOG_ERR, "%s: failed to write watch request (%m)",
			up->logname);
	}
}

/* ------------------------------------------------------------------ */

static void
process_tpv(
	peerT      * const peer ,
	json_ctx   * const jctx ,
	const l_fp * const rtime)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	const char * gps_time;
	int          gps_mode;
	double       ept;
	int          xlog2;

	gps_mode = (int)json_object_lookup_int_default(
		jctx, 0, "mode", 0);

	gps_time = json_object_lookup_string(
		jctx, 0, "time");

	/* accept time stamps only in 2d or 3d fix */
	if (gps_mode < 2 || NULL == gps_time) {
		/* receiver has no fix; tell about and avoid stale data */
		if ( ! up->pf_toff)
			++up->tc_sti_recv;
		++up->tc_nosync;
		up->fl_sti    = 0;
		up->fl_pps    = 0;
		up->fl_nosync = -1;
		return;
	}
	up->fl_nosync = 0;

	/* convert clock and set resulting ref time, but only if the
	 * TOFF sentence is *not* available
	 */
	if ( ! up->pf_toff) {
		++up->tc_sti_recv;
		/* save last time code to clock data */
		save_ltc(pp, gps_time);
		/* now parse the time string */
		if (convert_ascii_time(&up->sti_stamp, gps_time)) {
			DPRINTF(2, ("%s: process_tpv, stamp='%s',"
				    " recvt='%s' mode=%u\n",
				    up->logname,
				    gmprettydate(&up->sti_stamp),
				    gmprettydate(&up->sti_recvt),
				    gps_mode));

			/* have to use local receive time as substitute
			 * for the real receive time: TPV does not tell
			 * us.
			 */
			up->sti_local = *rtime;
			up->sti_recvt = *rtime;
			L_SUB(&up->sti_recvt, &up->sti_fudge);
			up->fl_sti = -1;
		} else {
			++up->tc_breply;
			up->fl_sti = 0;
		}
	}

	/* Set the precision from the GPSD data
	 * Use the ETP field for an estimation of the precision of the
	 * serial data. If ETP is not available, use the default serial
	 * data presion instead. (Note: The PPS branch has a different
	 * precision estimation, since it gets the proper value directly
	 * from GPSD!)
	 */
	ept = json_object_lookup_float_default(jctx, 0, "ept", 2.0e-3);
	ept = frexp(fabs(ept)*0.70710678, &xlog2); /* ~ sqrt(0.5) */
	if (ept < 0.25)
		xlog2 = INT_MIN;
	if (ept > 2.0)
		xlog2 = INT_MAX;
	up->sti_prec = clamped_precision(xlog2);
}

/* ------------------------------------------------------------------ */

static void
process_pps(
	peerT      * const peer ,
	json_ctx   * const jctx ,
	const l_fp * const rtime)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	int xlog2;

	++up->tc_pps_recv;

	/* Bail out if there's indication that time sync is bad or
	 * if we're explicitely requested to ignore PPS data.
	 */
	if (up->fl_nosync)
		return;

	up->pps_local = *rtime;
	/* Now grab the time values. 'clock_*' is the event time of the
	 * pulse measured on the local system clock; 'real_*' is the GPS
	 * reference time GPSD associated with the pulse.
	 */
	if (up->pf_nsec) {
		if ( ! get_binary_time(&up->pps_recvt2, jctx,
				       "clock_sec", "clock_nsec", 1))
			goto fail;
		if ( ! get_binary_time(&up->pps_stamp2, jctx,
				       "real_sec", "real_nsec", 1))
			goto fail;
	} else {
		if ( ! get_binary_time(&up->pps_recvt2, jctx,
				       "clock_sec", "clock_musec", 1000))
			goto fail;
		if ( ! get_binary_time(&up->pps_stamp2, jctx,
				       "real_sec", "real_musec", 1000))
			goto fail;
	}

	/* Try to read the precision field from the PPS record. If it's
	 * not there, take the precision from the serial data.
	 */
	xlog2 = json_object_lookup_int_default(
			jctx, 0, "precision", up->sti_prec);
	up->pps_prec = clamped_precision(xlog2);
	
	/* Get fudged receive times for primary & secondary unit */
	up->pps_recvt = up->pps_recvt2;
	L_SUB(&up->pps_recvt , &up->pps_fudge );
	L_SUB(&up->pps_recvt2, &up->pps_fudge2);
	pp->lastrec = up->pps_recvt;

	/* Map to nearest full second as reference time stamp for the
	 * primary channel. Sanity checks are done in evaluation step.
	 */
	up->pps_stamp = up->pps_recvt;
	L_ADDUF(&up->pps_stamp, 0x80000000u);
	up->pps_stamp.l_uf = 0;

	if (NULL != up->pps_peer)
		save_ltc(up->pps_peer->procptr,
			 gmprettydate(&up->pps_stamp2));
	DPRINTF(2, ("%s: PPS record processed,"
		    " stamp='%s', recvt='%s'\n",
		    up->logname,
		    gmprettydate(&up->pps_stamp2),
		    gmprettydate(&up->pps_recvt2)));
	
	up->fl_pps  = (0 != (pp->sloppyclockflag & CLK_FLAG2)) - 1;
	up->fl_pps2 = -1;
	return;

  fail:
	DPRINTF(1, ("%s: PPS record processing FAILED\n",
		    up->logname));
	++up->tc_breply;
}

/* ------------------------------------------------------------------ */

static void
process_toff(
	peerT      * const peer ,
	json_ctx   * const jctx ,
	const l_fp * const rtime)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	++up->tc_sti_recv;

	/* remember this! */
	up->pf_toff = -1;

	/* bail out if there's indication that time sync is bad */
	if (up->fl_nosync)
		return;

	if ( ! get_binary_time(&up->sti_recvt, jctx,
			       "clock_sec", "clock_nsec", 1))
			goto fail;
	if ( ! get_binary_time(&up->sti_stamp, jctx,
			       "real_sec", "real_nsec", 1))
			goto fail;
	L_SUB(&up->sti_recvt, &up->sti_fudge);
	up->sti_local = *rtime;
	up->fl_sti    = -1;

	save_ltc(pp, gmprettydate(&up->sti_stamp));
	DPRINTF(2, ("%s: TOFF record processed,"
		    " stamp='%s', recvt='%s'\n",
		    up->logname,
		    gmprettydate(&up->sti_stamp),
		    gmprettydate(&up->sti_recvt)));
	return;

  fail:
	DPRINTF(1, ("%s: TOFF record processing FAILED\n",
		    up->logname));
	++up->tc_breply;
}

/* ------------------------------------------------------------------ */

static void
gpsd_parse(
	peerT      * const peer ,
	const l_fp * const rtime)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	const char * clsid;

        DPRINTF(2, ("%s: gpsd_parse: time %s '%.*s'\n",
                    up->logname, ulfptoa(rtime, 6),
		    up->buflen, up->buffer));

	/* See if we can grab anything potentially useful. JSMN does not
	 * need a trailing NUL, but it needs the number of bytes to
	 * process. */
	if (!json_parse_record(&up->json_parse, up->buffer, up->buflen)) {
		++up->tc_breply;
		return;
	}
	
	/* Now dispatch over the objects we know */
	clsid = json_object_lookup_string(&up->json_parse, 0, "class");
	if (NULL == clsid) {
		++up->tc_breply;
		return;
	}

	if      (!strcmp("TPV", clsid))
		process_tpv(peer, &up->json_parse, rtime);
	else if (!strcmp("PPS", clsid))
		process_pps(peer, &up->json_parse, rtime);
	else if (!strcmp("TOFF", clsid))
		process_toff(peer, &up->json_parse, rtime);
	else if (!strcmp("VERSION", clsid))
		process_version(peer, &up->json_parse, rtime);
	else if (!strcmp("WATCH", clsid))
		process_watch(peer, &up->json_parse, rtime);
	else
		return; /* nothing we know about... */
	++up->tc_recv;

	/* if possible, feed the PPS side channel */
	if (up->pps_peer)
		eval_pps_secondary(
			up->pps_peer, up->pps_peer->procptr, up);

	/* check PPS vs. STI receive times:
	 * If STI is before PPS, then clearly the STI is too old. If PPS
	 * is before STI by more than one second, then PPS is too old.
	 * Weed out stale time stamps & flags.
	 */
	if (up->fl_pps && up->fl_sti) {
		l_fp diff;
		diff = up->sti_local;
		L_SUB(&diff, &up->pps_local);
		if (diff.l_i > 0)
			up->fl_pps = 0; /* pps too old */
		else if (diff.l_i < 0)
			up->fl_sti = 0; /* serial data too old */
	}

	/* dispatch to the mode-dependent processing functions */
	switch (up->mode) {
	default:
	case MODE_OP_STI:
		eval_serial(peer, pp, up);
		break;

	case MODE_OP_STRICT:
		eval_strict(peer, pp, up);
		break;

	case MODE_OP_AUTO:
		eval_auto(peer, pp, up);
		break;
	}
}

/* ------------------------------------------------------------------ */

static void
gpsd_stop_socket(
	peerT * const peer)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	if (-1 != pp->io.fd) {
		if (syslogok(pp, up))
			msyslog(LOG_INFO,
				"%s: closing socket to GPSD, fd=%d",
				up->logname, pp->io.fd);
		else
			DPRINTF(1, ("%s: closing socket to GPSD, fd=%d\n",
				    up->logname, pp->io.fd));
		io_closeclock(&pp->io);
		pp->io.fd = -1;
	}
	up->tickover = up->tickpres;
	up->tickpres = min(up->tickpres + 5, TICKOVER_HIGH);
	up->fl_vers  = 0;
	up->fl_sti   = 0;
	up->fl_pps   = 0;
	up->fl_watch = 0;
}

/* ------------------------------------------------------------------ */

static void
gpsd_init_socket(
	peerT * const peer)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;
	addrinfoT  * ai;
	int          rc;
	int          ov;

	/* draw next address to try */
	if (NULL == up->addr)
		up->addr = s_gpsd_addr;
	ai = up->addr;
	up->addr = ai->ai_next;

	/* try to create a matching socket */
	up->fdt = socket(
		ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (-1 == up->fdt) {
		if (syslogok(pp, up))
			msyslog(LOG_ERR,
				"%s: cannot create GPSD socket: %m",
				up->logname);
		goto no_socket;
	}

	/* Make sure the socket is non-blocking. Connect/reconnect and
	 * IO happen in an event-driven environment, and synchronous
	 * operations wreak havoc on that.
	 */
	rc = fcntl(up->fdt, F_SETFL, O_NONBLOCK, 1);
	if (-1 == rc) {
		if (syslogok(pp, up))
			msyslog(LOG_ERR,
				"%s: cannot set GPSD socket to non-blocking: %m",
				up->logname);
		goto no_socket;
	}
	/* Disable nagling. The way both GPSD and NTPD handle the
	 * protocol makes it record-oriented, and in most cases
	 * complete records (JSON serialised objects) will be sent in
	 * one sweep. Nagling gives not much advantage but adds another
	 * delay, which can worsen the situation for some packets.
	 */
	ov = 1;
	rc = setsockopt(up->fdt, IPPROTO_TCP, TCP_NODELAY,
			(void *)&ov, sizeof(ov));
	if (-1 == rc) {
		if (syslogok(pp, up))
			msyslog(LOG_INFO,
				"%s: cannot disable TCP nagle: %m",
				up->logname);
	}

	/* Start a non-blocking connect. There might be a synchronous
	 * connection result we have to handle.
	 */
	rc = connect(up->fdt, ai->ai_addr, ai->ai_addrlen);
	if (-1 == rc) {
		if (errno == EINPROGRESS) {
			DPRINTF(1, ("%s: async connect pending, fd=%d\n",
				    up->logname, up->fdt));
			return;
		}

		if (syslogok(pp, up))
			msyslog(LOG_ERR,
				"%s: cannot connect GPSD socket: %m",
				up->logname);
		goto no_socket;
	}

	/* We had a successful synchronous connect, so we add the
	 * refclock processing ASAP. We still have to wait for the
	 * version string and apply the watch command later on, but we
	 * might as well get the show on the road now.
	 */
	DPRINTF(1, ("%s: new socket connection, fd=%d\n",
		    up->logname, up->fdt));

	pp->io.fd = up->fdt;
	up->fdt   = -1;
	if (0 == io_addclock(&pp->io)) {
		if (syslogok(pp, up))
			msyslog(LOG_ERR,
				"%s: failed to register with I/O engine",
				up->logname);
		goto no_socket;
	}

	return;

  no_socket:
	if (-1 != pp->io.fd)
		close(pp->io.fd);
	if (-1 != up->fdt)
		close(up->fdt);
	pp->io.fd    = -1;
	up->fdt      = -1;
	up->tickover = up->tickpres;
	up->tickpres = min(up->tickpres + 5, TICKOVER_HIGH);
}

/* ------------------------------------------------------------------ */

static void
gpsd_test_socket(
	peerT * const peer)
{
	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	int       ec, rc;
	socklen_t lc;

	/* Check if the non-blocking connect was finished by testing the
	 * socket for writeability. Use the 'poll()' API if available
	 * and 'select()' otherwise.
	 */
	DPRINTF(2, ("%s: check connect, fd=%d\n",
		    up->logname, up->fdt));

#if defined(HAVE_SYS_POLL_H)
	{
		struct pollfd pfd;

		pfd.events = POLLOUT;
		pfd.fd     = up->fdt;
		rc = poll(&pfd, 1, 0);
		if (1 != rc || !(pfd.revents & POLLOUT))
			return;
	}
#elif defined(HAVE_SYS_SELECT_H)
	{
		struct timeval tout;
		fd_set         wset;

		memset(&tout, 0, sizeof(tout));
		FD_ZERO(&wset);
		FD_SET(up->fdt, &wset);
		rc = select(up->fdt+1, NULL, &wset, NULL, &tout);
		if (0 == rc || !(FD_ISSET(up->fdt, &wset)))
			return;
	}
#else
# error Blooper! That should have been found earlier!
#endif

	/* next timeout is a full one... */
	up->tickover = TICKOVER_LOW;

	/* check for socket error */
	ec = 0;
	lc = sizeof(ec);
	rc = getsockopt(up->fdt, SOL_SOCKET, SO_ERROR, (void *)&ec, &lc);
	if (-1 == rc || 0 != ec) {
		const char *errtxt;
		if (0 == ec)
			ec = errno;
		errtxt = strerror(ec);
		if (syslogok(pp, up))
			msyslog(LOG_ERR,
				"%s: async connect to GPSD failed,"
				" fd=%d, ec=%d(%s)",
				up->logname, up->fdt, ec, errtxt);
		else
			DPRINTF(1, ("%s: async connect to GPSD failed,"
				" fd=%d, ec=%d(%s)\n",
				    up->logname, up->fdt, ec, errtxt));
		goto no_socket;
	} else {
		DPRINTF(1, ("%s: async connect to GPSD succeeded, fd=%d\n",
			    up->logname, up->fdt));
	}

	/* swap socket FDs, and make sure the clock was added */
	pp->io.fd = up->fdt;
	up->fdt   = -1;
	if (0 == io_addclock(&pp->io)) {
		if (syslogok(pp, up))
			msyslog(LOG_ERR,
				"%s: failed to register with I/O engine",
				up->logname);
		goto no_socket;
	}
	return;

  no_socket:
	if (-1 != up->fdt) {
		DPRINTF(1, ("%s: closing socket, fd=%d\n",
			    up->logname, up->fdt));
		close(up->fdt);
	}
	up->fdt      = -1;
	up->tickover = up->tickpres;
	up->tickpres = min(up->tickpres + 5, TICKOVER_HIGH);
}

/* =====================================================================
 * helper stuff
 */

/* -------------------------------------------------------------------
 * store a properly clamped precision value
 */
static int16_t
clamped_precision(
	int rawprec)
{
	if (rawprec > 0)
		rawprec = 0;
	if (rawprec < -32)
		rawprec = -32;
	return (int16_t)rawprec;
}

/* -------------------------------------------------------------------
 * Convert a GPSD timestamp (ISO8601 Format) to an l_fp
 */
static BOOL
convert_ascii_time(
	l_fp       * fp      ,
	const char * gps_time)
{
	char           *ep;
	struct tm       gd;
	struct timespec ts;
	uint32_t        dw;

	/* Use 'strptime' to take the brunt of the work, then parse
	 * the fractional part manually, starting with a digit weight of
	 * 10^8 nanoseconds.
	 */
	ts.tv_nsec = 0;
	ep = strptime(gps_time, "%Y-%m-%dT%H:%M:%S", &gd);
	if (NULL == ep)
		return FALSE; /* could not parse the mandatory stuff! */
	if (*ep == '.') {
		dw = 100000000u;
		while (isdigit(*(u_char*)++ep)) {
			ts.tv_nsec += (*(u_char*)ep - '0') * dw;
			dw /= 10u;
		}
	}
	if (ep[0] != 'Z' || ep[1] != '\0')
		return FALSE; /* trailing garbage */

	/* Now convert the whole thing into a 'l_fp'. We do not use
	 * 'mkgmtime()' since its not standard and going through the
	 * calendar routines is not much effort, either.
	 */
	ts.tv_sec = (ntpcal_tm_to_rd(&gd) - DAY_NTP_STARTS) * SECSPERDAY
	          + ntpcal_tm_to_daysec(&gd);
	*fp = tspec_intv_to_lfp(ts);

	return TRUE;
}

/* -------------------------------------------------------------------
 * Save the last timecode string, making sure it's properly truncated
 * if necessary and NUL terminated in any case.
 */
static void
save_ltc(
	clockprocT * const pp,
	const char * const tc)
{
	size_t len = 0;
	
	if (tc) {
		len = strlen(tc);
		if (len >= sizeof(pp->a_lastcode))
			len = sizeof(pp->a_lastcode) - 1;
		memcpy(pp->a_lastcode, tc, len);
	}
	pp->lencode = (u_short)len;
	pp->a_lastcode[len] = '\0';
}

/* -------------------------------------------------------------------
 * asprintf replacement... it's not available everywhere...
 */
static int
myasprintf(
	char      ** spp,
	char const * fmt,
	...             )
{
	size_t alen, plen;

	alen = 32;
	*spp = NULL;
	do {
		va_list va;

		alen += alen;
		free(*spp);
		*spp = (char*)malloc(alen);
		if (NULL == *spp)
			return -1;

		va_start(va, fmt);
		plen = (size_t)vsnprintf(*spp, alen, fmt, va);
		va_end(va);
	} while (plen >= alen);

	return (int)plen;
}

/* -------------------------------------------------------------------
 * dump a raw data buffer
 */

static char *
add_string(
	char *dp,
	char *ep,
	const char *sp)
{
	while (dp != ep && *sp)
		*dp++ = *sp++;
	return dp;
}

static void
log_data(
	peerT      *peer,
	const char *what,
	const char *buf ,
	size_t      len )
{
	/* we're running single threaded with regards to the clocks. */
	static char s_lbuf[2048];

	clockprocT * const pp = peer->procptr;
	gpsd_unitT * const up = (gpsd_unitT *)pp->unitptr;

	if (debug > 1) {
		const char *sptr = buf;
		const char *stop = buf + len;
		char       *dptr = s_lbuf;
		char       *dtop = s_lbuf + sizeof(s_lbuf) - 1; /* for NUL */

		while (sptr != stop && dptr != dtop) {
			u_char uch = (u_char)*sptr++;
			if (uch == '\\') {
				dptr = add_string(dptr, dtop, "\\\\");
			} else if (isprint(uch)) {
				*dptr++ = (char)uch;
			} else {
				char fbuf[6];
				snprintf(fbuf, sizeof(fbuf), "\\%03o", uch);
				dptr = add_string(dptr, dtop, fbuf);
			}
		}
		*dptr = '\0';
		mprintf("%s[%s]: '%s'\n", up->logname, what, s_lbuf);
	}
}

#else
NONEMPTY_TRANSLATION_UNIT
#endif /* REFCLOCK && CLOCK_GPSDJSON */
