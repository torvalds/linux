/*
 * refclock_nmea.c - clock driver for an NMEA GPS CLOCK
 *		Michael Petry Jun 20, 1994
 *		 based on refclock_heathn.c
 *
 * Updated to add support for Accord GPS Clock
 *		Venu Gopal Dec 05, 2007
 *		neo.venu@gmail.com, venugopal_d@pgad.gov.in
 *
 * Updated to process 'time1' fudge factor
 *		Venu Gopal May 05, 2008
 *
 * Converted to common PPSAPI code, separate PPS fudge time1
 * from serial timecode fudge time2.
 *		Dave Hart July 1, 2009
 *		hart@ntp.org, davehart@davehart.com
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ntp_types.h"

#if defined(REFCLOCK) && defined(CLOCK_NMEA)

#define NMEA_WRITE_SUPPORT 0 /* no write support at the moment */

#include <sys/stat.h>
#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_unixtime.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"
#include "ntp_calendar.h"
#include "timespecops.h"

#ifdef HAVE_PPSAPI
# include "ppsapi_timepps.h"
# include "refclock_atom.h"
#endif /* HAVE_PPSAPI */


/*
 * This driver supports NMEA-compatible GPS receivers
 *
 * Prototype was refclock_trak.c, Thanks a lot.
 *
 * The receiver used spits out the NMEA sentences for boat navigation.
 * And you thought it was an information superhighway.	Try a raging river
 * filled with rapids and whirlpools that rip away your data and warp time.
 *
 * If HAVE_PPSAPI is defined code to use the PPSAPI will be compiled in.
 * On startup if initialization of the PPSAPI fails, it will fall back
 * to the "normal" timestamps.
 *
 * The PPSAPI part of the driver understands fudge flag2 and flag3. If
 * flag2 is set, it will use the clear edge of the pulse. If flag3 is
 * set, kernel hardpps is enabled.
 *
 * GPS sentences other than RMC (the default) may be enabled by setting
 * the relevent bits of 'mode' in the server configuration line
 * server 127.127.20.x mode X
 * 
 * bit 0 - enables RMC (1)
 * bit 1 - enables GGA (2)
 * bit 2 - enables GLL (4)
 * bit 3 - enables ZDA (8) - Standard Time & Date
 * bit 3 - enables ZDG (8) - Accord GPS Clock's custom sentence with GPS time 
 *			     very close to standard ZDA
 * 
 * Multiple sentences may be selected except when ZDG/ZDA is selected.
 *
 * bit 4/5/6 - selects the baudrate for serial port :
 *		0 for 4800 (default) 
 *		1 for 9600 
 *		2 for 19200 
 *		3 for 38400 
 *		4 for 57600 
 *		5 for 115200 
 */
#define NMEA_MESSAGE_MASK	0x0000FF0FU
#define NMEA_BAUDRATE_MASK	0x00000070U
#define NMEA_BAUDRATE_SHIFT	4

#define NMEA_DELAYMEAS_MASK	0x80
#define NMEA_EXTLOG_MASK	0x00010000U
#define NMEA_DATETRUST_MASK	0x02000000U

#define NMEA_PROTO_IDLEN	5	/* tag name must be at least 5 chars */
#define NMEA_PROTO_MINLEN	6	/* min chars in sentence, excluding CS */
#define NMEA_PROTO_MAXLEN	80	/* max chars in sentence, excluding CS */
#define NMEA_PROTO_FIELDS	32	/* not official; limit on fields per record */

/*
 * We check the timecode format and decode its contents.  We only care
 * about a few of them, the most important being the $GPRMC format:
 *
 * $GPRMC,hhmmss,a,fddmm.xx,n,dddmmm.xx,w,zz.z,yyy.,ddmmyy,dd,v*CC
 *
 * mode (0,1,2,3) selects sentence ANY/ALL, RMC, GGA, GLL, ZDA
 * $GPGLL,3513.8385,S,14900.7851,E,232420.594,A*21
 * $GPGGA,232420.59,3513.8385,S,14900.7851,E,1,05,3.4,00519,M,,,,*3F
 * $GPRMC,232418.19,A,3513.8386,S,14900.7853,E,00.0,000.0,121199,12.,E*77
 *
 * Defining GPZDA to support Standard Time & Date
 * sentence. The sentence has the following format 
 *  
 *  $--ZDA,HHMMSS.SS,DD,MM,YYYY,TH,TM,*CS<CR><LF>
 *
 *  Apart from the familiar fields, 
 *  'TH'    Time zone Hours
 *  'TM'    Time zone Minutes
 *
 * Defining GPZDG to support Accord GPS Clock's custom NMEA 
 * sentence. The sentence has the following format 
 *  
 *  $GPZDG,HHMMSS.S,DD,MM,YYYY,AA.BB,V*CS<CR><LF>
 *
 *  It contains the GPS timestamp valid for next PPS pulse.
 *  Apart from the familiar fields, 
 *  'AA.BB' denotes the signal strength( should be < 05.00 ) 
 *  'V'	    denotes the GPS sync status : 
 *	   '0' indicates INVALID time, 
 *	   '1' indicates accuracy of +/-20 ms
 *	   '2' indicates accuracy of +/-100 ns
 *
 * Defining PGRMF for Garmin GPS Fix Data
 * $PGRMF,WN,WS,DATE,TIME,LS,LAT,LAT_DIR,LON,LON_DIR,MODE,FIX,SPD,DIR,PDOP,TDOP
 * WN  -- GPS week number (weeks since 1980-01-06, mod 1024)
 * WS  -- GPS seconds in week
 * LS  -- GPS leap seconds, accumulated ( UTC + LS == GPS )
 * FIX -- Fix type: 0=nofix, 1=2D, 2=3D
 * DATE/TIME are standard date/time strings in UTC time scale
 *
 * The GPS time can be used to get the full century for the truncated
 * date spec.
 */

/*
 * Definitions
 */
#define	DEVICE		"/dev/gps%d"	/* GPS serial device */
#define	PPSDEV		"/dev/gpspps%d"	/* PPSAPI device override */
#define	SPEED232	B4800	/* uart speed (4800 bps) */
#define	PRECISION	(-9)	/* precision assumed (about 2 ms) */
#define	PPS_PRECISION	(-20)	/* precision assumed (about 1 us) */
#define	REFID		"GPS\0"	/* reference id */
#define	DESCRIPTION	"NMEA GPS Clock" /* who we are */
#ifndef O_NOCTTY
#define M_NOCTTY	0
#else
#define M_NOCTTY	O_NOCTTY
#endif
#ifndef O_NONBLOCK
#define M_NONBLOCK	0
#else
#define M_NONBLOCK	O_NONBLOCK
#endif
#define PPSOPENMODE	(O_RDWR | M_NOCTTY | M_NONBLOCK)

/* NMEA sentence array indexes for those we use */
#define NMEA_GPRMC	0	/* recommended min. nav. */
#define NMEA_GPGGA	1	/* fix and quality */
#define NMEA_GPGLL	2	/* geo. lat/long */
#define NMEA_GPZDA	3	/* date/time */
/*
 * $GPZDG is a proprietary sentence that violates the spec, by not
 * using $P and an assigned company identifier to prefix the sentence
 * identifier.	When used with this driver, the system needs to be
 * isolated from other NTP networks, as it operates in GPS time, not
 * UTC as is much more common.	GPS time is >15 seconds different from
 * UTC due to not respecting leap seconds since 1970 or so.  Other
 * than the different timebase, $GPZDG is similar to $GPZDA.
 */
#define NMEA_GPZDG	4
#define NMEA_PGRMF	5
#define NMEA_ARRAY_SIZE (NMEA_PGRMF + 1)

/*
 * Sentence selection mode bits
 */
#define USE_GPRMC		0x00000001u
#define USE_GPGGA		0x00000002u
#define USE_GPGLL		0x00000004u
#define USE_GPZDA		0x00000008u
#define USE_PGRMF		0x00000100u

/* mapping from sentence index to controlling mode bit */
static const u_int32 sentence_mode[NMEA_ARRAY_SIZE] =
{
	USE_GPRMC,
	USE_GPGGA,
	USE_GPGLL,
	USE_GPZDA,
	USE_GPZDA,
	USE_PGRMF
};

/* date formats we support */
enum date_fmt {
	DATE_1_DDMMYY,	/* use 1 field	with 2-digit year */
	DATE_3_DDMMYYYY	/* use 3 fields with 4-digit year */
};

/* results for 'field_init()'
 *
 * Note: If a checksum is present, the checksum test must pass OK or the
 * sentence is tagged invalid.
 */
#define CHECK_EMPTY  -1	/* no data			*/
#define CHECK_INVALID 0	/* not a valid NMEA sentence	*/
#define CHECK_VALID   1	/* valid but without checksum	*/
#define CHECK_CSVALID 2	/* valid with checksum OK	*/

/*
 * Unit control structure
 */
typedef struct {
#ifdef HAVE_PPSAPI
	struct refclock_atom atom; /* PPSAPI structure */
	int	ppsapi_fd;	/* fd used with PPSAPI */
	u_char	ppsapi_tried;	/* attempt PPSAPI once */
	u_char	ppsapi_lit;	/* time_pps_create() worked */
	u_char	ppsapi_gate;	/* system is on PPS */
#endif /* HAVE_PPSAPI */
	u_char  gps_time;	/* use GPS time, not UTC */
	u_short century_cache;	/* cached current century */
	l_fp	last_reftime;	/* last processed reference stamp */
	short 	epoch_warp;	/* last epoch warp, for logging */
	/* tally stats, reset each poll cycle */
	struct
	{
		u_int total;
		u_int accepted;
		u_int rejected;   /* GPS said not enough signal */
		u_int malformed;  /* Bad checksum, invalid date or time */
		u_int filtered;   /* mode bits, not GPZDG, same second */
		u_int pps_used;
	}	
		tally;
	/* per sentence checksum seen flag */
	u_char	cksum_type[NMEA_ARRAY_SIZE];
} nmea_unit;

/*
 * helper for faster field access
 */
typedef struct {
	char  *base;	/* buffer base		*/
	char  *cptr;	/* current field ptr	*/
	int    blen;	/* buffer length	*/
	int    cidx;	/* current field index	*/
} nmea_data;

/*
 * NMEA gps week/time information
 * This record contains the number of weeks since 1980-01-06 modulo
 * 1024, the seconds elapsed since start of the week, and the number of
 * leap seconds that are the difference between GPS and UTC time scale.
 */
typedef struct {
	u_int32 wt_time;	/* seconds since weekstart */
	u_short wt_week;	/* week number */
	short	wt_leap;	/* leap seconds */
} gps_weektm;

/*
 * The GPS week time scale starts on Sunday, 1980-01-06. We need the
 * rata die number of this day.
 */
#ifndef DAY_GPS_STARTS
#define DAY_GPS_STARTS 722820
#endif

/*
 * Function prototypes
 */
static	void	nmea_init	(void);
static	int	nmea_start	(int, struct peer *);
static	void	nmea_shutdown	(int, struct peer *);
static	void	nmea_receive	(struct recvbuf *);
static	void	nmea_poll	(int, struct peer *);
#ifdef HAVE_PPSAPI
static	void	nmea_control	(int, const struct refclockstat *,
				 struct refclockstat *, struct peer *);
#define		NMEA_CONTROL	nmea_control
#else
#define		NMEA_CONTROL	noentry
#endif /* HAVE_PPSAPI */
static	void	nmea_timer	(int, struct peer *);

/* parsing helpers */
static int	field_init	(nmea_data * data, char * cp, int len);
static char *	field_parse	(nmea_data * data, int fn);
static void	field_wipe	(nmea_data * data, ...);
static u_char	parse_qual	(nmea_data * data, int idx,
				 char tag, int inv);
static int	parse_time	(struct calendar * jd, long * nsec,
				 nmea_data *, int idx);
static int	parse_date	(struct calendar *jd, nmea_data*,
				 int idx, enum date_fmt fmt);
static int	parse_weekdata	(gps_weektm *, nmea_data *,
				 int weekidx, int timeidx, int leapidx);
/* calendar / date helpers */
static int	unfold_day	(struct calendar * jd, u_int32 rec_ui);
static int	unfold_century	(struct calendar * jd, u_int32 rec_ui);
static int	gpsfix_century	(struct calendar * jd, const gps_weektm * wd,
				 u_short * ccentury);
static l_fp     eval_gps_time	(struct peer * peer, const struct calendar * gpst,
				 const struct timespec * gpso, const l_fp * xrecv);

static int	nmead_open	(const char * device);
static void     save_ltc        (struct refclockproc * const, const char * const,
				 size_t);

/*
 * If we want the driver to ouput sentences, too: re-enable the send
 * support functions by defining NMEA_WRITE_SUPPORT to non-zero...
 */
#if NMEA_WRITE_SUPPORT

static	void gps_send(int, const char *, struct peer *);
# ifdef SYS_WINNT
#  undef write	/* ports/winnt/include/config.h: #define write _write */
extern int async_write(int, const void *, unsigned int);
#  define write(fd, data, octets)	async_write(fd, data, octets)
# endif /* SYS_WINNT */

#endif /* NMEA_WRITE_SUPPORT */

static int32_t g_gpsMinBase;
static int32_t g_gpsMinYear;

/*
 * -------------------------------------------------------------------
 * Transfer vector
 * -------------------------------------------------------------------
 */
struct refclock refclock_nmea = {
	nmea_start,		/* start up driver */
	nmea_shutdown,		/* shut down driver */
	nmea_poll,		/* transmit poll message */
	NMEA_CONTROL,		/* fudge control */
	nmea_init,		/* initialize driver */
	noentry,		/* buginfo */
	nmea_timer		/* called once per second */
};

/*
 * -------------------------------------------------------------------
 * nmea_init - initialise data
 *
 * calculates a few runtime constants that cannot be made compile time
 * constants.
 * -------------------------------------------------------------------
 */
static void
nmea_init(void)
{
	struct calendar date;

	/* - calculate min. base value for GPS epoch & century unfolding 
	 * This assumes that the build system was roughly in sync with
	 * the world, and that really synchronising to a time before the
	 * program was created would be unsafe or insane. If the build
	 * date cannot be stablished, at least use the start of GPS
	 * (1980-01-06) as minimum, because GPS can surely NOT
	 * synchronise beyond it's own big bang. We add a little safety
	 * margin for the fuzziness of the build date, which is in an
	 * undefined time zone. */
	if (ntpcal_get_build_date(&date))
		g_gpsMinBase = ntpcal_date_to_rd(&date) - 2;
	else
		g_gpsMinBase = 0;

	if (g_gpsMinBase < DAY_GPS_STARTS)
		g_gpsMinBase = DAY_GPS_STARTS;

	ntpcal_rd_to_date(&date, g_gpsMinBase);
	g_gpsMinYear  = date.year;
	g_gpsMinBase -= DAY_NTP_STARTS;
}

/*
 * -------------------------------------------------------------------
 * nmea_start - open the GPS devices and initialize data for processing
 *
 * return 0 on error, 1 on success. Even on error the peer structures
 * must be in a state that permits 'nmea_shutdown()' to clean up all
 * resources, because it will be called immediately to do so.
 * -------------------------------------------------------------------
 */
static int
nmea_start(
	int		unit,
	struct peer *	peer
	)
{
	struct refclockproc * const	pp = peer->procptr;
	nmea_unit * const		up = emalloc_zero(sizeof(*up));
	char				device[20];
	size_t				devlen;
	u_int32				rate;
	int				baudrate;
	const char *			baudtext;


	/* Get baudrate choice from mode byte bits 4/5/6 */
	rate = (peer->ttl & NMEA_BAUDRATE_MASK) >> NMEA_BAUDRATE_SHIFT;

	switch (rate) {
	case 0:
		baudrate = SPEED232;
		baudtext = "4800";
		break;
	case 1:
		baudrate = B9600;
		baudtext = "9600";
		break;
	case 2:
		baudrate = B19200;
		baudtext = "19200";
		break;
	case 3:
		baudrate = B38400;
		baudtext = "38400";
		break;
#ifdef B57600
	case 4:
		baudrate = B57600;
		baudtext = "57600";
		break;
#endif
#ifdef B115200
	case 5:
		baudrate = B115200;
		baudtext = "115200";
		break;
#endif
	default:
		baudrate = SPEED232;
		baudtext = "4800 (fallback)";
		break;
	}

	/* Allocate and initialize unit structure */
	pp->unitptr = (caddr_t)up;
	pp->io.fd = -1;
	pp->io.clock_recv = nmea_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	/* force change detection on first valid message */
	memset(&up->last_reftime, 0xFF, sizeof(up->last_reftime));
	/* force checksum on GPRMC, see below */
	up->cksum_type[NMEA_GPRMC] = CHECK_CSVALID;
#ifdef HAVE_PPSAPI
	up->ppsapi_fd = -1;
#endif
	ZERO(up->tally);

	/* Initialize miscellaneous variables */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy(&pp->refid, REFID, 4);

	/* Open serial port. Use CLK line discipline, if available. */
	devlen = snprintf(device, sizeof(device), DEVICE, unit);
	if (devlen >= sizeof(device)) {
		msyslog(LOG_ERR, "%s clock device name too long",
			refnumtoa(&peer->srcadr));
		return FALSE; /* buffer overflow */
	}
	pp->io.fd = refclock_open(device, baudrate, LDISC_CLK);
	if (0 >= pp->io.fd) {
		pp->io.fd = nmead_open(device);
		if (-1 == pp->io.fd)
			return FALSE;
	}
	LOGIF(CLOCKINFO, (LOG_NOTICE, "%s serial %s open at %s bps",
	      refnumtoa(&peer->srcadr), device, baudtext));

	/* succeed if this clock can be added */
	return io_addclock(&pp->io) != 0;
}


/*
 * -------------------------------------------------------------------
 * nmea_shutdown - shut down a GPS clock
 * 
 * NOTE this routine is called after nmea_start() returns failure,
 * as well as during a normal shutdown due to ntpq :config unpeer.
 * -------------------------------------------------------------------
 */
static void
nmea_shutdown(
	int           unit,
	struct peer * peer
	)
{
	struct refclockproc * const pp = peer->procptr;
	nmea_unit	    * const up = (nmea_unit *)pp->unitptr;

	UNUSED_ARG(unit);

	if (up != NULL) {
#ifdef HAVE_PPSAPI
		if (up->ppsapi_lit)
			time_pps_destroy(up->atom.handle);
		if (up->ppsapi_tried && up->ppsapi_fd != pp->io.fd)
			close(up->ppsapi_fd);
#endif
		free(up);
	}
	pp->unitptr = (caddr_t)NULL;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	pp->io.fd = -1;
}

/*
 * -------------------------------------------------------------------
 * nmea_control - configure fudge params
 * -------------------------------------------------------------------
 */
#ifdef HAVE_PPSAPI
static void
nmea_control(
	int                         unit,
	const struct refclockstat * in_st,
	struct refclockstat       * out_st,
	struct peer               * peer
	)
{
	struct refclockproc * const pp = peer->procptr;
	nmea_unit	    * const up = (nmea_unit *)pp->unitptr;

	char   device[32];
	size_t devlen;
	
	UNUSED_ARG(in_st);
	UNUSED_ARG(out_st);

	/*
	 * PPS control
	 *
	 * If /dev/gpspps$UNIT can be opened that will be used for
	 * PPSAPI.  Otherwise, the GPS serial device /dev/gps$UNIT
	 * already opened is used for PPSAPI as well. (This might not
	 * work, in which case the PPS API remains unavailable...)
	 */

	/* Light up the PPSAPI interface if not yet attempted. */
	if ((CLK_FLAG1 & pp->sloppyclockflag) && !up->ppsapi_tried) {
		up->ppsapi_tried = TRUE;
		devlen = snprintf(device, sizeof(device), PPSDEV, unit);
		if (devlen < sizeof(device)) {
			up->ppsapi_fd = open(device, PPSOPENMODE,
					     S_IRUSR | S_IWUSR);
		} else {
			up->ppsapi_fd = -1;
			msyslog(LOG_ERR, "%s PPS device name too long",
				refnumtoa(&peer->srcadr));
		}
		if (-1 == up->ppsapi_fd)
			up->ppsapi_fd = pp->io.fd;	
		if (refclock_ppsapi(up->ppsapi_fd, &up->atom)) {
			/* use the PPS API for our own purposes now. */
			up->ppsapi_lit = refclock_params(
				pp->sloppyclockflag, &up->atom);
			if (!up->ppsapi_lit) {
				/* failed to configure, drop PPS unit */
				time_pps_destroy(up->atom.handle);
				msyslog(LOG_WARNING,
					"%s set PPSAPI params fails",
					refnumtoa(&peer->srcadr));				
			}
			/* note: the PPS I/O handle remains valid until
			 * flag1 is cleared or the clock is shut down. 
			 */
		} else {
			msyslog(LOG_WARNING,
				"%s flag1 1 but PPSAPI fails",
				refnumtoa(&peer->srcadr));
		}
	}

	/* shut down PPS API if activated */
	if (!(CLK_FLAG1 & pp->sloppyclockflag) && up->ppsapi_tried) {
		/* shutdown PPS API */
		if (up->ppsapi_lit)
			time_pps_destroy(up->atom.handle);
		up->atom.handle = 0;
		/* close/drop PPS fd */
		if (up->ppsapi_fd != pp->io.fd)
			close(up->ppsapi_fd);
		up->ppsapi_fd = -1;

		/* clear markers and peer items */
		up->ppsapi_gate  = FALSE;
		up->ppsapi_lit   = FALSE;
		up->ppsapi_tried = FALSE;

		peer->flags &= ~FLAG_PPS;
		peer->precision = PRECISION;
	}
}
#endif	/* HAVE_PPSAPI */

/*
 * -------------------------------------------------------------------
 * nmea_timer - called once per second
 *		this only polls (older?) Oncore devices now
 *
 * Usually 'nmea_receive()' can get a timestamp every second, but at
 * least one Motorola unit needs prompting each time. Doing so in
 * 'nmea_poll()' gives only one sample per poll cycle, which actually
 * defeats the purpose of the median filter. Polling once per second
 * seems a much better idea.
 * -------------------------------------------------------------------
 */
static void
nmea_timer(
	int	      unit,
	struct peer * peer
	)
{
#if NMEA_WRITE_SUPPORT
    
	struct refclockproc * const pp = peer->procptr;

	UNUSED_ARG(unit);

	if (-1 != pp->io.fd) /* any mode bits to evaluate here? */
		gps_send(pp->io.fd, "$PMOTG,RMC,0000*1D\r\n", peer);
#else
	
	UNUSED_ARG(unit);
	UNUSED_ARG(peer);
	
#endif /* NMEA_WRITE_SUPPORT */
}

#ifdef HAVE_PPSAPI
/*
 * -------------------------------------------------------------------
 * refclock_ppsrelate(...) -- correlate with PPS edge
 *
 * This function is used to correlate a receive time stamp and a
 * reference time with a PPS edge time stamp. It applies the necessary
 * fudges (fudge1 for PPS, fudge2 for receive time) and then tries to
 * move the receive time stamp to the corresponding edge. This can warp
 * into future, if a transmission delay of more than 500ms is not
 * compensated with a corresponding fudge time2 value, because then the
 * next PPS edge is nearer than the last. (Similiar to what the PPS ATOM
 * driver does, but we deal with full time stamps here, not just phase
 * shift information.) Likewise, a negative fudge time2 value must be
 * used if the reference time stamp correlates with the *following* PPS
 * pulse.
 *
 * Note that the receive time fudge value only needs to move the receive
 * stamp near a PPS edge but that close proximity is not required;
 * +/-100ms precision should be enough. But since the fudge value will
 * probably also be used to compensate the transmission delay when no
 * PPS edge can be related to the time stamp, it's best to get it as
 * close as possible.
 *
 * It should also be noted that the typical use case is matching to the
 * preceeding edge, as most units relate their sentences to the current
 * second.
 *
 * The function returns PPS_RELATE_NONE (0) if no PPS edge correlation
 * can be fixed; PPS_RELATE_EDGE (1) when a PPS edge could be fixed, but
 * the distance to the reference time stamp is too big (exceeds
 * +/-400ms) and the ATOM driver PLL cannot be used to fix the phase;
 * and PPS_RELATE_PHASE (2) when the ATOM driver PLL code can be used.
 *
 * On output, the receive time stamp is replaced with the corresponding
 * PPS edge time if a fix could be made; the PPS fudge is updated to
 * reflect the proper fudge time to apply. (This implies that
 * 'refclock_process_offset()' must be used!)
 * -------------------------------------------------------------------
 */
#define PPS_RELATE_NONE	 0	/* no pps correlation possible	  */
#define PPS_RELATE_EDGE	 1	/* recv time fixed, no phase lock */
#define PPS_RELATE_PHASE 2	/* recv time fixed, phase lock ok */

static int
refclock_ppsrelate(
	const struct refclockproc  * pp	    ,	/* for sanity	  */
	const struct refclock_atom * ap	    ,	/* for PPS io	  */
	const l_fp		   * reftime ,
	l_fp			   * rd_stamp,	/* i/o read stamp */
	double			     pp_fudge,	/* pps fudge	  */
	double			   * rd_fudge	/* i/o read fudge */
	)
{
	pps_info_t	pps_info;
	struct timespec timeout;
	l_fp		pp_stamp, pp_delta;
	double		delta, idelta;

	if (pp->leap == LEAP_NOTINSYNC)
		return PPS_RELATE_NONE; /* clock is insane, no chance */

	ZERO(timeout);
	ZERO(pps_info);
	if (time_pps_fetch(ap->handle, PPS_TSFMT_TSPEC,
			   &pps_info, &timeout) < 0)
		return PPS_RELATE_NONE; /* can't get time stamps */

	/* get last active PPS edge before receive */
	if (ap->pps_params.mode & PPS_CAPTUREASSERT)
		timeout = pps_info.assert_timestamp;
	else if (ap->pps_params.mode & PPS_CAPTURECLEAR)
		timeout = pps_info.clear_timestamp;
	else
		return PPS_RELATE_NONE; /* WHICH edge, please?!? */

	/* get delta between receive time and PPS time */
	pp_stamp = tspec_stamp_to_lfp(timeout);
	pp_delta = *rd_stamp;
	L_SUB(&pp_delta, &pp_stamp);
	LFPTOD(&pp_delta, delta);
	delta += pp_fudge - *rd_fudge;
	if (fabs(delta) > 1.5)
		return PPS_RELATE_NONE; /* PPS timeout control */
	
	/* eventually warp edges, check phase */
	idelta	  = floor(delta + 0.5);
	pp_fudge -= idelta;
	delta	 -= idelta;
	if (fabs(delta) > 0.45)
		return PPS_RELATE_NONE; /* dead band control */

	/* we actually have a PPS edge to relate with! */
	*rd_stamp = pp_stamp;
	*rd_fudge = pp_fudge;

	/* if whole system out-of-sync, do not try to PLL */
	if (sys_leap == LEAP_NOTINSYNC)
		return PPS_RELATE_EDGE; /* cannot PLL with atom code */

	/* check against reftime if ATOM PLL can be used */
	pp_delta = *reftime;
	L_SUB(&pp_delta, &pp_stamp);
	LFPTOD(&pp_delta, delta);
	delta += pp_fudge;
	if (fabs(delta) > 0.45)
		return PPS_RELATE_EDGE; /* cannot PLL with atom code */

	/* all checks passed, gets an AAA rating here! */
	return PPS_RELATE_PHASE; /* can PLL with atom code */
}
#endif	/* HAVE_PPSAPI */

/*
 * -------------------------------------------------------------------
 * nmea_receive - receive data from the serial interface
 *
 * This is the workhorse for NMEA data evaluation:
 *
 * + it checks all NMEA data, and rejects sentences that are not valid
 *   NMEA sentences
 * + it checks whether a sentence is known and to be used
 * + it parses the time and date data from the NMEA data string and
 *   augments the missing bits. (century in dat, whole date, ...)
 * + it rejects data that is not from the first accepted sentence in a
 *   burst
 * + it eventually replaces the receive time with the PPS edge time.
 * + it feeds the data to the internal processing stages.
 * -------------------------------------------------------------------
 */
static void
nmea_receive(
	struct recvbuf * rbufp
	)
{
	/* declare & init control structure ptrs */
	struct peer	    * const peer = rbufp->recv_peer;
	struct refclockproc * const pp = peer->procptr;
	nmea_unit	    * const up = (nmea_unit*)pp->unitptr;

	/* Use these variables to hold data until we decide its worth keeping */
	nmea_data rdata;
	char 	  rd_lastcode[BMAX];
	l_fp 	  rd_timestamp, rd_reftime;
	int	  rd_lencode;
	double	  rd_fudge;

	/* working stuff */
	struct calendar date;	/* to keep & convert the time stamp */
	struct timespec tofs;	/* offset to full-second reftime */
	gps_weektm      gpsw;	/* week time storage */
	/* results of sentence/date/time parsing */
	u_char		sentence;	/* sentence tag */
	int		checkres;
	char *		cp;
	int		rc_date;
	int		rc_time;

	/* make sure data has defined pristine state */
	ZERO(tofs);
	ZERO(date);
	ZERO(gpsw);

	/* 
	 * Read the timecode and timestamp, then initialise field
	 * processing. The <CR><LF> at the NMEA line end is translated
	 * to <LF><LF> by the terminal input routines on most systems,
	 * and this gives us one spurious empty read per record which we
	 * better ignore silently.
	 */
	rd_lencode = refclock_gtlin(rbufp, rd_lastcode,
				    sizeof(rd_lastcode), &rd_timestamp);
	checkres = field_init(&rdata, rd_lastcode, rd_lencode);
	switch (checkres) {

	case CHECK_INVALID:
		DPRINTF(1, ("%s invalid data: '%s'\n",
			refnumtoa(&peer->srcadr), rd_lastcode));
		refclock_report(peer, CEVNT_BADREPLY);
		return;

	case CHECK_EMPTY:
		return;

	default:
		DPRINTF(1, ("%s gpsread: %d '%s'\n",
			refnumtoa(&peer->srcadr), rd_lencode,
			rd_lastcode));
		break;
	}
	up->tally.total++;

	/* 
	 * --> below this point we have a valid NMEA sentence <--
	 *
	 * Check sentence name. Skip first 2 chars (talker ID) in most
	 * cases, to allow for $GLGGA and $GPGGA etc. Since the name
	 * field has at least 5 chars we can simply shift the field
	 * start.
	 */
	cp = field_parse(&rdata, 0);
	if      (strncmp(cp + 2, "RMC,", 4) == 0)
		sentence = NMEA_GPRMC;
	else if (strncmp(cp + 2, "GGA,", 4) == 0)
		sentence = NMEA_GPGGA;
	else if (strncmp(cp + 2, "GLL,", 4) == 0)
		sentence = NMEA_GPGLL;
	else if (strncmp(cp + 2, "ZDA,", 4) == 0)
		sentence = NMEA_GPZDA;
	else if (strncmp(cp + 2, "ZDG,", 4) == 0)
		sentence = NMEA_GPZDG;
	else if (strncmp(cp,   "PGRMF,", 6) == 0) 
		sentence = NMEA_PGRMF;
	else
		return;	/* not something we know about */

	/* Eventually output delay measurement now. */
	if (peer->ttl & NMEA_DELAYMEAS_MASK) {
		mprintf_clock_stats(&peer->srcadr, "delay %0.6f %.*s",
			 ldexp(rd_timestamp.l_uf, -32),
			 (int)(strchr(rd_lastcode, ',') - rd_lastcode),
			 rd_lastcode);
	}
	
	/* See if I want to process this message type */
	if ((peer->ttl & NMEA_MESSAGE_MASK) &&
	    !(peer->ttl & sentence_mode[sentence])) {
		up->tally.filtered++;
		return;
	}

	/* 
	 * make sure it came in clean
	 *
	 * Apparently, older NMEA specifications (which are expensive)
	 * did not require the checksum for all sentences.  $GPMRC is
	 * the only one so far identified which has always been required
	 * to include a checksum.
	 *
	 * Today, most NMEA GPS receivers checksum every sentence.  To
	 * preserve its error-detection capabilities with modern GPSes
	 * while allowing operation without checksums on all but $GPMRC,
	 * we keep track of whether we've ever seen a valid checksum on
	 * a given sentence, and if so, reject future instances without
	 * checksum.  ('up->cksum_type[NMEA_GPRMC]' is set in
	 * 'nmea_start()' to enforce checksums for $GPRMC right from the
	 * start.)
	 */
	if (up->cksum_type[sentence] <= (u_char)checkres) {
		up->cksum_type[sentence] = (u_char)checkres;
	} else {
		DPRINTF(1, ("%s checksum missing: '%s'\n",
			refnumtoa(&peer->srcadr), rd_lastcode));
		refclock_report(peer, CEVNT_BADREPLY);
		up->tally.malformed++;
		return;
	}

	/*
	 * $GPZDG provides GPS time not UTC, and the two mix poorly.
	 * Once have processed a $GPZDG, do not process any further UTC
	 * sentences (all but $GPZDG currently).
	 */ 
	if (up->gps_time && NMEA_GPZDG != sentence) {
		up->tally.filtered++;
		return;
	}

	DPRINTF(1, ("%s processing %d bytes, timecode '%s'\n",
		refnumtoa(&peer->srcadr), rd_lencode, rd_lastcode));

	/*
	 * Grab fields depending on clock string type and possibly wipe
	 * sensitive data from the last timecode.
	 */
	switch (sentence) {

	case NMEA_GPRMC:
		/* Check quality byte, fetch data & time */
		rc_time	 = parse_time(&date, &tofs.tv_nsec, &rdata, 1);
		pp->leap = parse_qual(&rdata, 2, 'A', 0);
		rc_date	 = parse_date(&date, &rdata, 9, DATE_1_DDMMYY)
			&& unfold_century(&date, rd_timestamp.l_ui);
		if (CLK_FLAG4 & pp->sloppyclockflag)
			field_wipe(&rdata, 3, 4, 5, 6, -1);
		break;

	case NMEA_GPGGA:
		/* Check quality byte, fetch time only */
		rc_time	 = parse_time(&date, &tofs.tv_nsec, &rdata, 1);
		pp->leap = parse_qual(&rdata, 6, '0', 1);
		rc_date	 = unfold_day(&date, rd_timestamp.l_ui);
		if (CLK_FLAG4 & pp->sloppyclockflag)
			field_wipe(&rdata, 2, 4, -1);
		break;

	case NMEA_GPGLL:
		/* Check quality byte, fetch time only */
		rc_time	 = parse_time(&date, &tofs.tv_nsec, &rdata, 5);
		pp->leap = parse_qual(&rdata, 6, 'A', 0);
		rc_date	 = unfold_day(&date, rd_timestamp.l_ui);
		if (CLK_FLAG4 & pp->sloppyclockflag)
			field_wipe(&rdata, 1, 3, -1);
		break;
	
	case NMEA_GPZDA:
		/* No quality.	Assume best, fetch time & full date */
		pp->leap = LEAP_NOWARNING;
		rc_time	 = parse_time(&date, &tofs.tv_nsec, &rdata, 1);
		rc_date	 = parse_date(&date, &rdata, 2, DATE_3_DDMMYYYY);
		break;

	case NMEA_GPZDG:
		/* Check quality byte, fetch time & full date */
		rc_time	 = parse_time(&date, &tofs.tv_nsec, &rdata, 1);
		rc_date	 = parse_date(&date, &rdata, 2, DATE_3_DDMMYYYY);
		pp->leap = parse_qual(&rdata, 4, '0', 1);
		tofs.tv_sec = -1; /* GPZDG is following second */
		break;

	case NMEA_PGRMF:
		/* get date, time, qualifier and GPS weektime. We need
		 * date and time-of-day for the century fix, so we read
		 * them first.
		 */
		rc_date  = parse_weekdata(&gpsw, &rdata, 1, 2, 5)
		        && parse_date(&date, &rdata, 3, DATE_1_DDMMYY);
		rc_time  = parse_time(&date, &tofs.tv_nsec, &rdata, 4);
		pp->leap = parse_qual(&rdata, 11, '0', 1);		
		rc_date  = rc_date
		        && gpsfix_century(&date, &gpsw, &up->century_cache);
		if (CLK_FLAG4 & pp->sloppyclockflag)
			field_wipe(&rdata, 6, 8, -1);
		break;
		
	default:
		INVARIANT(0);	/* Coverity 97123 */
		return;
	}

	/* Check sanity of time-of-day. */
	if (rc_time == 0) {	/* no time or conversion error? */
		checkres = CEVNT_BADTIME;
		up->tally.malformed++;
	}
	/* Check sanity of date. */
	else if (rc_date == 0) {/* no date or conversion error? */
		checkres = CEVNT_BADDATE;
		up->tally.malformed++;
	}
	/* check clock sanity; [bug 2143] */
	else if (pp->leap == LEAP_NOTINSYNC) { /* no good status? */
		checkres = CEVNT_BADREPLY;
		up->tally.rejected++;
	}
	else
		checkres = -1;

	if (checkres != -1) {
		save_ltc(pp, rd_lastcode, rd_lencode);
		refclock_report(peer, checkres);
		return;
	}

	DPRINTF(1, ("%s effective timecode: %04u-%02u-%02u %02d:%02d:%02d\n",
		refnumtoa(&peer->srcadr),
		date.year, date.month, date.monthday,
		date.hour, date.minute, date.second));

	/* Check if we must enter GPS time mode; log so if we do */
	if (!up->gps_time && (sentence == NMEA_GPZDG)) {
		msyslog(LOG_INFO, "%s using GPS time as if it were UTC",
			refnumtoa(&peer->srcadr));
		up->gps_time = 1;
	}
	
	/*
	 * Get the reference time stamp from the calendar buffer.
	 * Process the new sample in the median filter and determine the
	 * timecode timestamp, but only if the PPS is not in control.
	 * Discard sentence if reference time did not change.
	 */
	rd_reftime = eval_gps_time(peer, &date, &tofs, &rd_timestamp);
	if (L_ISEQU(&up->last_reftime, &rd_reftime)) {
		/* Do not touch pp->a_lastcode on purpose! */
		up->tally.filtered++;
		return;
	}
	up->last_reftime = rd_reftime;
	rd_fudge = pp->fudgetime2;

	DPRINTF(1, ("%s using '%s'\n",
		    refnumtoa(&peer->srcadr), rd_lastcode));

	/* Data will be accepted. Update stats & log data. */
	up->tally.accepted++;
	save_ltc(pp, rd_lastcode, rd_lencode);
	pp->lastrec = rd_timestamp;

#ifdef HAVE_PPSAPI
	/*
	 * If we have PPS running, we try to associate the sentence
	 * with the last active edge of the PPS signal.
	 */
	if (up->ppsapi_lit)
		switch (refclock_ppsrelate(
				pp, &up->atom, &rd_reftime, &rd_timestamp,
				pp->fudgetime1,	&rd_fudge))
		{
		case PPS_RELATE_PHASE:
			up->ppsapi_gate = TRUE;
			peer->precision = PPS_PRECISION;
			peer->flags |= FLAG_PPS;
			DPRINTF(2, ("%s PPS_RELATE_PHASE\n",
				    refnumtoa(&peer->srcadr)));
			up->tally.pps_used++;
			break;
			
		case PPS_RELATE_EDGE:
			up->ppsapi_gate = TRUE;
			peer->precision = PPS_PRECISION;
			DPRINTF(2, ("%s PPS_RELATE_EDGE\n",
				    refnumtoa(&peer->srcadr)));
			break;
			
		case PPS_RELATE_NONE:
		default:
			/*
			 * Resetting precision and PPS flag is done in
			 * 'nmea_poll', since it might be a glitch. But
			 * at the end of the poll cycle we know...
			 */
			DPRINTF(2, ("%s PPS_RELATE_NONE\n",
				    refnumtoa(&peer->srcadr)));
			break;
		}
#endif /* HAVE_PPSAPI */

	refclock_process_offset(pp, rd_reftime, rd_timestamp, rd_fudge);
}


/*
 * -------------------------------------------------------------------
 * nmea_poll - called by the transmit procedure
 *
 * Does the necessary bookkeeping stuff to keep the reported state of
 * the clock in sync with reality.
 *
 * We go to great pains to avoid changing state here, since there may
 * be more than one eavesdropper receiving the same timecode.
 * -------------------------------------------------------------------
 */
static void
nmea_poll(
	int           unit,
	struct peer * peer
	)
{
	struct refclockproc * const pp = peer->procptr;
	nmea_unit	    * const up = (nmea_unit *)pp->unitptr;
	
	/*
	 * Process median filter samples. If none received, declare a
	 * timeout and keep going.
	 */
#ifdef HAVE_PPSAPI
	/*
	 * If we don't have PPS pulses and time stamps, turn PPS down
	 * for now.
	 */
	if (!up->ppsapi_gate) {
		peer->flags &= ~FLAG_PPS;
		peer->precision = PRECISION;
	} else {
		up->ppsapi_gate = FALSE;
	}
#endif /* HAVE_PPSAPI */

	/*
	 * If the median filter is empty, claim a timeout. Else process
	 * the input data and keep the stats going.
	 */
	if (pp->coderecv == pp->codeproc) {
		refclock_report(peer, CEVNT_TIMEOUT);
	} else {
		pp->polls++;
		pp->lastref = pp->lastrec;
		refclock_receive(peer);
	}
	
	/*
	 * If extended logging is required, write the tally stats to the
	 * clockstats file; otherwise just do a normal clock stats
	 * record. Clear the tally stats anyway.
	*/
	if (peer->ttl & NMEA_EXTLOG_MASK) {
		/* Log & reset counters with extended logging */
		const char *nmea = pp->a_lastcode;
		if (*nmea == '\0') nmea = "(none)";
		mprintf_clock_stats(
		  &peer->srcadr, "%s  %u %u %u %u %u %u",
		  nmea,
		  up->tally.total, up->tally.accepted,
		  up->tally.rejected, up->tally.malformed,
		  up->tally.filtered, up->tally.pps_used);
	} else {
		record_clock_stats(&peer->srcadr, pp->a_lastcode);
	}
	ZERO(up->tally);
}

/*
 * -------------------------------------------------------------------
 * Save the last timecode string, making sure it's properly truncated
 * if necessary and NUL terminated in any case.
 */
static void
save_ltc(
	struct refclockproc * const pp,
	const char * const          tc,
	size_t                      len
	)
{
	if (len >= sizeof(pp->a_lastcode))
		len = sizeof(pp->a_lastcode) - 1;
	pp->lencode = (u_short)len;
	memcpy(pp->a_lastcode, tc, len);
	pp->a_lastcode[len] = '\0';
}


#if NMEA_WRITE_SUPPORT
/*
 * -------------------------------------------------------------------
 *  gps_send(fd, cmd, peer)	Sends a command to the GPS receiver.
 *   as in gps_send(fd, "rqts,u", peer);
 *
 * If 'cmd' starts with a '$' it is assumed that this command is in raw
 * format, that is, starts with '$', ends with '<cr><lf>' and that any
 * checksum is correctly provided; the command will be send 'as is' in
 * that case. Otherwise the function will create the necessary frame
 * (start char, chksum, final CRLF) on the fly.
 *
 * We don't currently send any data, but would like to send RTCM SC104
 * messages for differential positioning. It should also give us better
 * time. Without a PPS output, we're Just fooling ourselves because of
 * the serial code paths
 * -------------------------------------------------------------------
 */
static void
gps_send(
	int           fd,
	const char  * cmd,
	struct peer * peer
	)
{
	/* $...*xy<CR><LF><NUL> add 7 */
	char	      buf[NMEA_PROTO_MAXLEN + 7];
	int	      len;
	u_char	      dcs;
	const u_char *beg, *end;

	if (*cmd != '$') {
		/* get checksum and length */
		beg = end = (const u_char*)cmd;
		dcs = 0;
		while (*end >= ' ' && *end != '*')
			dcs ^= *end++;
		len = end - beg;
		/* format into output buffer with overflow check */
		len = snprintf(buf, sizeof(buf), "$%.*s*%02X\r\n",
			       len, beg, dcs);
		if ((size_t)len >= sizeof(buf)) {
			DPRINTF(1, ("%s gps_send: buffer overflow for command '%s'\n",
				    refnumtoa(&peer->srcadr), cmd));
			return;	/* game over player 1 */
		}
		cmd = buf;
	} else {
		len = strlen(cmd);
	}

	DPRINTF(1, ("%s gps_send: '%.*s'\n", refnumtoa(&peer->srcadr),
		len - 2, cmd));

	/* send out the whole stuff */
	if (write(fd, cmd, len) == -1)
		refclock_report(peer, CEVNT_FAULT);
}
#endif /* NMEA_WRITE_SUPPORT */

/*
 * -------------------------------------------------------------------
 * helpers for faster field splitting
 * -------------------------------------------------------------------
 *
 * set up a field record, check syntax and verify checksum
 *
 * format is $XXXXX,1,2,3,4*ML
 *
 * 8-bit XOR of characters between $ and * noninclusive is transmitted
 * in last two chars M and L holding most and least significant nibbles
 * in hex representation such as:
 *
 *   $GPGLL,5057.970,N,00146.110,E,142451,A*27
 *   $GPVTG,089.0,T,,,15.2,N,,*7F
 *
 * Some other constraints:
 * + The field name must at least 5 upcase characters or digits and must
 *   start with a character.
 * + The checksum (if present) must be uppercase hex digits.
 * + The length of a sentence is limited to 80 characters (not including
 *   the final CR/LF nor the checksum, but including the leading '$')
 *
 * Return values:
 *  + CHECK_INVALID
 *	The data does not form a valid NMEA sentence or a checksum error
 *	occurred.
 *  + CHECK_VALID
 *	The data is a valid NMEA sentence but contains no checksum.
 *  + CHECK_CSVALID
 *	The data is a valid NMEA sentence and passed the checksum test.
 * -------------------------------------------------------------------
 */
static int
field_init(
	nmea_data * data,	/* context structure		       */
	char 	  * cptr,	/* start of raw data		       */
	int	    dlen	/* data len, not counting trailing NUL */
	)
{
	u_char cs_l;	/* checksum local computed	*/
	u_char cs_r;	/* checksum remote given	*/
	char * eptr;	/* buffer end end pointer	*/
	char   tmp;	/* char buffer 			*/
	
	cs_l = 0;
	cs_r = 0;
	/* some basic input constraints */
	if (dlen < 0)
		dlen = 0;
	eptr = cptr + dlen;
	*eptr = '\0';
	
	/* load data context */	
	data->base = cptr;
	data->cptr = cptr;
	data->cidx = 0;
	data->blen = dlen;

	/* syntax check follows here. check allowed character
	 * sequences, updating the local computed checksum as we go.
	 *
	 * regex equiv: '^\$[A-Z][A-Z0-9]{4,}[^*]*(\*[0-9A-F]{2})?$'
	 */

	/* -*- start character: '^\$' */
	if (*cptr == '\0')
		return CHECK_EMPTY;
	if (*cptr++ != '$')
		return CHECK_INVALID;

	/* -*- advance context beyond start character */
	data->base++;
	data->cptr++;
	data->blen--;
	
	/* -*- field name: '[A-Z][A-Z0-9]{4,},' */
	if (*cptr < 'A' || *cptr > 'Z')
		return CHECK_INVALID;
	cs_l ^= *cptr++;
	while ((*cptr >= 'A' && *cptr <= 'Z') ||
	       (*cptr >= '0' && *cptr <= '9')  )
		cs_l ^= *cptr++;
	if (*cptr != ',' || (cptr - data->base) < NMEA_PROTO_IDLEN)
		return CHECK_INVALID;
	cs_l ^= *cptr++;

	/* -*- data: '[^*]*' */
	while (*cptr && *cptr != '*')
		cs_l ^= *cptr++;
	
	/* -*- checksum field: (\*[0-9A-F]{2})?$ */
	if (*cptr == '\0')
		return CHECK_VALID;
	if (*cptr != '*' || cptr != eptr - 3 ||
	    (cptr - data->base) >= NMEA_PROTO_MAXLEN)
		return CHECK_INVALID;

	for (cptr++; (tmp = *cptr) != '\0'; cptr++) {
		if (tmp >= '0' && tmp <= '9')
			cs_r = (cs_r << 4) + (tmp - '0');
		else if (tmp >= 'A' && tmp <= 'F')
			cs_r = (cs_r << 4) + (tmp - 'A' + 10);
		else
			break;
	}

	/* -*- make sure we are at end of string and csum matches */
	if (cptr != eptr || cs_l != cs_r)
		return CHECK_INVALID;

	return CHECK_CSVALID;
}

/*
 * -------------------------------------------------------------------
 * fetch a data field by index, zero being the name field. If this
 * function is called repeatedly with increasing indices, the total load
 * is O(n), n being the length of the string; if it is called with
 * decreasing indices, the total load is O(n^2). Try not to go backwards
 * too often.
 * -------------------------------------------------------------------
 */
static char *
field_parse(
	nmea_data * data,
	int 	    fn
	)
{
	char tmp;

	if (fn < data->cidx) {
		data->cidx = 0;
		data->cptr = data->base;
	}
	while ((fn > data->cidx) && (tmp = *data->cptr) != '\0') {
		data->cidx += (tmp == ',');
		data->cptr++;
	}
	return data->cptr;
}

/*
 * -------------------------------------------------------------------
 * Wipe (that is, overwrite with '_') data fields and the checksum in
 * the last timecode.  The list of field indices is given as integers
 * in a varargs list, preferrably in ascending order, in any case
 * terminated by a negative field index.
 *
 * A maximum number of 8 fields can be overwritten at once to guard
 * against runaway (that is, unterminated) argument lists.
 *
 * This function affects what a remote user can see with
 *
 * ntpq -c clockvar <server>
 *
 * Note that this also removes the wiped fields from any clockstats
 * log.	 Some NTP operators monitor their NMEA GPS using the change in
 * location in clockstats over time as as a proxy for the quality of
 * GPS reception and thereby time reported.
 * -------------------------------------------------------------------
 */
static void
field_wipe(
	nmea_data * data,
	...
	)
{
	va_list	va;		/* vararg index list */
	int	fcnt;		/* safeguard against runaway arglist */
	int	fidx;		/* field to nuke, or -1 for checksum */
	char  * cp;		/* overwrite destination */
	
	fcnt = 8;
	cp = NULL;
	va_start(va, data);
	do {
		fidx = va_arg(va, int);
		if (fidx >= 0 && fidx <= NMEA_PROTO_FIELDS) {
			cp = field_parse(data, fidx);
		} else {
			cp = data->base + data->blen;
			if (data->blen >= 3 && cp[-3] == '*')
				cp -= 2;
		}
		for ( ; '\0' != *cp && '*' != *cp && ',' != *cp; cp++)
			if ('.' != *cp)
				*cp = '_';
	} while (fcnt-- && fidx >= 0);
	va_end(va);	
}

/*
 * -------------------------------------------------------------------
 * PARSING HELPERS
 * -------------------------------------------------------------------
 *
 * Check sync status
 *
 * If the character at the data field start matches the tag value,
 * return LEAP_NOWARNING and LEAP_NOTINSYNC otherwise. If the 'inverted'
 * flag is given, just the opposite value is returned. If there is no
 * data field (*cp points to the NUL byte) the result is LEAP_NOTINSYNC.
 * -------------------------------------------------------------------
 */
static u_char
parse_qual(
	nmea_data * rd,
	int         idx,
	char        tag,
	int         inv
	)
{
	static const u_char table[2] =
				{ LEAP_NOTINSYNC, LEAP_NOWARNING };
	char * dp;

	dp = field_parse(rd, idx);
	
	return table[ *dp && ((*dp == tag) == !inv) ];
}

/*
 * -------------------------------------------------------------------
 * Parse a time stamp in HHMMSS[.sss] format with error checking.
 *
 * returns 1 on success, 0 on failure
 * -------------------------------------------------------------------
 */
static int
parse_time(
	struct calendar * jd,	/* result calendar pointer */
	long		* ns,	/* storage for nsec fraction */
	nmea_data       * rd,
	int		  idx
	)
{
	static const unsigned long weight[4] = {
		0, 100000000, 10000000, 1000000
	};

	int	rc;
	u_int	h;
	u_int	m;
	u_int	s;
	int	p1;
	int	p2;
	u_long	f;
	char  * dp;

	dp = field_parse(rd, idx);
	rc = sscanf(dp, "%2u%2u%2u%n.%3lu%n", &h, &m, &s, &p1, &f, &p2);
	if (rc < 3 || p1 != 6) {
		DPRINTF(1, ("nmea: invalid time code: '%.6s'\n", dp));
		return FALSE;
	}
	
	/* value sanity check */
	if (h > 23 || m > 59 || s > 60) {
		DPRINTF(1, ("nmea: invalid time spec %02u:%02u:%02u\n",
			    h, m, s));
		return FALSE;
	}

	jd->hour   = (u_char)h;
	jd->minute = (u_char)m;
	jd->second = (u_char)s;
	/* if we have a fraction, scale it up to nanoseconds. */
	if (rc == 4)
		*ns = f * weight[p2 - p1 - 1];
	else
		*ns = 0;

	return TRUE;
}

/*
 * -------------------------------------------------------------------
 * Parse a date string from an NMEA sentence. This could either be a
 * partial date in DDMMYY format in one field, or DD,MM,YYYY full date
 * spec spanning three fields. This function does some extensive error
 * checking to make sure the date string was consistent.
 *
 * returns 1 on success, 0 on failure
 * -------------------------------------------------------------------
 */
static int
parse_date(
	struct calendar * jd,	/* result pointer */
	nmea_data       * rd,
	int		  idx,
	enum date_fmt	  fmt
	)
{
	int	rc;
	u_int	y;
	u_int	m;
	u_int	d;
	int	p;
	char  * dp;
	
	dp = field_parse(rd, idx);
	switch (fmt) {

	case DATE_1_DDMMYY:
		rc = sscanf(dp, "%2u%2u%2u%n", &d, &m, &y, &p);
		if (rc != 3 || p != 6) {
			DPRINTF(1, ("nmea: invalid date code: '%.6s'\n",
				    dp));
			return FALSE;
		}
		break;

	case DATE_3_DDMMYYYY:
		rc = sscanf(dp, "%2u,%2u,%4u%n", &d, &m, &y, &p);
		if (rc != 3 || p != 10) {
			DPRINTF(1, ("nmea: invalid date code: '%.10s'\n",
				    dp));
			return FALSE;
		}
		break;

	default:
		DPRINTF(1, ("nmea: invalid parse format: %d\n", fmt));
		return FALSE;
	}

	/* value sanity check */
	if (d < 1 || d > 31 || m < 1 || m > 12) {
		DPRINTF(1, ("nmea: invalid date spec (YMD) %04u:%02u:%02u\n",
			    y, m, d));
		return FALSE;
	}
	
	/* store results */
	jd->monthday = (u_char)d;
	jd->month    = (u_char)m;
	jd->year     = (u_short)y;

	return TRUE;
}

/*
 * -------------------------------------------------------------------
 * Parse GPS week time info from an NMEA sentence. This info contains
 * the GPS week number, the GPS time-of-week and the leap seconds GPS
 * to UTC.
 *
 * returns 1 on success, 0 on failure
 * -------------------------------------------------------------------
 */
static int
parse_weekdata(
	gps_weektm * wd,
	nmea_data  * rd,
	int          weekidx,
	int          timeidx,
	int          leapidx
	)
{
	u_long secs;
	int    fcnt;

	/* parse fields and count success */
	fcnt  = sscanf(field_parse(rd, weekidx), "%hu", &wd->wt_week);
	fcnt += sscanf(field_parse(rd, timeidx), "%lu", &secs);
	fcnt += sscanf(field_parse(rd, leapidx), "%hd", &wd->wt_leap);
	if (fcnt != 3 || wd->wt_week >= 1024 || secs >= 7*SECSPERDAY) {
		DPRINTF(1, ("nmea: parse_weekdata: invalid weektime spec\n"));
		return FALSE;
	}
	wd->wt_time = (u_int32)secs;

	return TRUE;
}

/*
 * -------------------------------------------------------------------
 * funny calendar-oriented stuff -- perhaps a bit hard to grok.
 * -------------------------------------------------------------------
 *
 * Unfold a time-of-day (seconds since midnight) around the current
 * system time in a manner that guarantees an absolute difference of
 * less than 12hrs.
 *
 * This function is used for NMEA sentences that contain no date
 * information. This requires the system clock to be in +/-12hrs
 * around the true time, or the clock will synchronize the system 1day
 * off if not augmented with a time sources that also provide the
 * necessary date information.
 *
 * The function updates the calendar structure it also uses as
 * input to fetch the time from.
 *
 * returns 1 on success, 0 on failure
 * -------------------------------------------------------------------
 */
static int
unfold_day(
	struct calendar * jd,
	u_int32		  rec_ui
	)
{
	vint64	     rec_qw;
	ntpcal_split rec_ds;

	/*
	 * basically this is the peridiodic extension of the receive
	 * time - 12hrs to the time-of-day with a period of 1 day.
	 * But we would have to execute this in 64bit arithmetic, and we
	 * cannot assume we can do this; therefore this is done
	 * in split representation.
	 */
	rec_qw = ntpcal_ntp_to_ntp(rec_ui - SECSPERDAY/2, NULL);
	rec_ds = ntpcal_daysplit(&rec_qw);
	rec_ds.lo = ntpcal_periodic_extend(rec_ds.lo,
					   ntpcal_date_to_daysec(jd),
					   SECSPERDAY);
	rec_ds.hi += ntpcal_daysec_to_date(jd, rec_ds.lo);
	return (ntpcal_rd_to_date(jd, rec_ds.hi + DAY_NTP_STARTS) >= 0);
}

/*
 * -------------------------------------------------------------------
 * A 2-digit year is expanded into full year spec around the year found
 * in 'jd->year'. This should be in +79/-19 years around the system time,
 * or the result will be off by 100 years.  The assymetric behaviour was
 * chosen to enable inital sync for systems that do not have a
 * battery-backup clock and start with a date that is typically years in
 * the past.
 *
 * Since the GPS epoch starts at 1980-01-06, the resulting year will be
 * not be before 1980 in any case.
 *
 * returns 1 on success, 0 on failure
 * -------------------------------------------------------------------
 */
static int
unfold_century(
	struct calendar * jd,
	u_int32		  rec_ui
	)
{
	struct calendar rec;
	int32		baseyear;

	ntpcal_ntp_to_date(&rec, rec_ui, NULL);
	baseyear = rec.year - 20;
	if (baseyear < g_gpsMinYear)
		baseyear = g_gpsMinYear;
	jd->year = (u_short)ntpcal_periodic_extend(baseyear, jd->year,
						   100);

	return ((baseyear <= jd->year) && (baseyear + 100 > jd->year));
}

/*
 * -------------------------------------------------------------------
 * A 2-digit year is expanded into a full year spec by correlation with
 * a GPS week number and the current leap second count.
 *
 * The GPS week time scale counts weeks since Sunday, 1980-01-06, modulo
 * 1024 and seconds since start of the week. The GPS time scale is based
 * on international atomic time (TAI), so the leap second difference to
 * UTC is also needed for a proper conversion.
 *
 * A brute-force analysis (that is, test for every date) shows that a
 * wrong assignment of the century can not happen between the years 1900
 * to 2399 when comparing the week signatures for different
 * centuries. (I *think* that will not happen for 400*1024 years, but I
 * have no valid proof. -*-perlinger@ntp.org-*-)
 *
 * This function is bound to to work between years 1980 and 2399
 * (inclusive), which should suffice for now ;-)
 *
 * Note: This function needs a full date&time spec on input due to the
 * necessary leap second corrections!
 *
 * returns 1 on success, 0 on failure
 * -------------------------------------------------------------------
 */
static int
gpsfix_century(
	struct calendar  * jd,
	const gps_weektm * wd,
	u_short          * century
	) 
{
	int32	days;
	int32	doff;
	u_short week;
	u_short year;
	int     loop;

	/* Get day offset. Assumes that the input time is in range and
	 * that the leap seconds do not shift more than +/-1 day.
	 */
	doff = ntpcal_date_to_daysec(jd) + wd->wt_leap;
	doff = (doff >= SECSPERDAY) - (doff < 0);

	/*
	 * Loop over centuries to get a match, starting with the last
	 * successful one. (Or with the 19th century if the cached value
	 * is out of range...)
	 */
	year = jd->year % 100;
	for (loop = 5; loop > 0; loop--,(*century)++) {
		if (*century < 19 || *century >= 24)
			*century = 19;
		/* Get days and week in GPS epoch */
		jd->year = year + *century * 100;
		days = ntpcal_date_to_rd(jd) - DAY_GPS_STARTS + doff;
		week = (days / 7) % 1024;
		if (days >= 0 && wd->wt_week == week)
			return TRUE; /* matched... */
	}

	jd->year = year;
	return FALSE; /* match failed... */
}

/*
 * -------------------------------------------------------------------
 * And now the final execise: Considering the fact that many (most?)
 * GPS receivers cannot handle a GPS epoch wrap well, we try to
 * compensate for that problem by unwrapping a GPS epoch around the
 * receive stamp. Another execise in periodic unfolding, of course,
 * but with enough points to take care of.
 *
 * Note: The integral part of 'tofs' is intended to handle small(!)
 * systematic offsets, as -1 for handling $GPZDG, which gives the
 * following second. (sigh...) The absolute value shall be less than a
 * day (86400 seconds).
 * -------------------------------------------------------------------
 */
static l_fp
eval_gps_time(
	struct peer           * peer, /* for logging etc */
	const struct calendar * gpst, /* GPS time stamp  */
	const struct timespec * tofs, /* GPS frac second & offset */
	const l_fp            * xrecv /* receive time stamp */
	)
{
	struct refclockproc * const pp = peer->procptr;
	nmea_unit	    * const up = (nmea_unit *)pp->unitptr;

	l_fp    retv;

	/* components of calculation */
	int32_t rcv_sec, rcv_day; /* receive ToD and day */
	int32_t gps_sec, gps_day; /* GPS ToD and day in NTP epoch */
	int32_t adj_day, weeks;   /* adjusted GPS day and week shift */

	/* some temporaries to shuffle data */
	vint64       vi64;
	ntpcal_split rs64;

	/* evaluate time stamp from receiver. */
	gps_sec = ntpcal_date_to_daysec(gpst);
	gps_day = ntpcal_date_to_rd(gpst) - DAY_NTP_STARTS;

	/* merge in fractional offset */
	retv = tspec_intv_to_lfp(*tofs);
	gps_sec += retv.l_i;

	/* If we fully trust the GPS receiver, just combine days and
	 * seconds and be done. */
	if (peer->ttl & NMEA_DATETRUST_MASK) {
		retv.l_ui = ntpcal_dayjoin(gps_day, gps_sec).D_s.lo;
		return retv;
	}

	/* So we do not trust the GPS receiver to deliver a correct date
	 * due to the GPS epoch changes. We map the date from the
	 * receiver into the +/-512 week interval around the receive
	 * time in that case. This would be a tad easier with 64bit
	 * calculations, but again, we restrict the code to 32bit ops
	 * when possible. */

	/* - make sure the GPS fractional day is normalised
	 * Applying the offset value might have put us slightly over the
	 * edge of the allowed range for seconds-of-day. Doing a full
	 * division with floor correction is overkill here; a simple
	 * addition or subtraction step is sufficient. Using WHILE loops
	 * gives the right result even if the offset exceeds one day,
	 * which is NOT what it's intented for! */
	while (gps_sec >= SECSPERDAY) {
		gps_sec -= SECSPERDAY;
		gps_day += 1;
	}
	while (gps_sec < 0) {
		gps_sec += SECSPERDAY;
		gps_day -= 1;
	}

	/* - get unfold base: day of full recv time - 512 weeks */
	vi64 = ntpcal_ntp_to_ntp(xrecv->l_ui, NULL);
	rs64 = ntpcal_daysplit(&vi64);
	rcv_sec = rs64.lo;
	rcv_day = rs64.hi - 512 * 7;

	/* - take the fractional days into account
	 * If the fractional day of the GPS time is smaller than the
	 * fractional day of the receive time, we shift the base day for
	 * the unfold by 1. */
	if (   gps_sec  < rcv_sec
	   || (gps_sec == rcv_sec && retv.l_uf < xrecv->l_uf))
		rcv_day += 1;

	/* - don't warp ahead of GPS invention! */
	if (rcv_day < g_gpsMinBase)
		rcv_day = g_gpsMinBase;

	/* - let the magic happen: */
	adj_day = ntpcal_periodic_extend(rcv_day, gps_day, 1024*7);

	/* - check if we should log a GPS epoch warp */
	weeks = (adj_day - gps_day) / 7;
	if (weeks != up->epoch_warp) {
		up->epoch_warp = weeks;
		LOGIF(CLOCKINFO, (LOG_INFO,
				  "%s Changed GPS epoch warp to %d weeks",
				  refnumtoa(&peer->srcadr), weeks));
	}

	/* - build result and be done */
	retv.l_ui = ntpcal_dayjoin(adj_day, gps_sec).D_s.lo;
	return retv;
}

/*
 * ===================================================================
 *
 * NMEAD support
 *
 * original nmead support added by Jon Miner (cp_n18@yahoo.com)
 *
 * See http://home.hiwaay.net/~taylorc/gps/nmea-server/
 * for information about nmead
 *
 * To use this, you need to create a link from /dev/gpsX to
 * the server:port where nmead is running.  Something like this:
 *
 * ln -s server:port /dev/gps1
 *
 * Split into separate function by Juergen Perlinger
 * (perlinger-at-ntp-dot-org)
 *
 * ===================================================================
 */
static int
nmead_open(
	const char * device
	)
{
	int	fd = -1;		/* result file descriptor */
	
#ifdef HAVE_READLINK
	char	host[80];		/* link target buffer	*/
	char  * port;			/* port name or number	*/
	int	rc;			/* result code (several)*/
	int     sh;			/* socket handle	*/
	struct addrinfo	 ai_hint;	/* resolution hint	*/
	struct addrinfo	*ai_list;	/* resolution result	*/
	struct addrinfo *ai;		/* result scan ptr	*/

	fd = -1;
	
	/* try to read as link, make sure no overflow occurs */
	rc = readlink(device, host, sizeof(host));
	if ((size_t)rc >= sizeof(host))
		return fd;	/* error / overflow / truncation */
	host[rc] = '\0';	/* readlink does not place NUL	*/

	/* get port */
	port = strchr(host, ':');
	if (!port)
		return fd; /* not 'host:port' syntax ? */
	*port++ = '\0';	/* put in separator */
	
	/* get address infos and try to open socket
	 *
	 * This getaddrinfo() is naughty in ntpd's nonblocking main
	 * thread, but you have to go out of your wary to use this code
	 * and typically the blocking is at startup where its impact is
	 * reduced. The same holds for the 'connect()', as it is
	 * blocking, too...
	 */
	ZERO(ai_hint);
	ai_hint.ai_protocol = IPPROTO_TCP;
	ai_hint.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &ai_hint, &ai_list))
		return fd;
	
	for (ai = ai_list; ai && (fd == -1); ai = ai->ai_next) {
		sh = socket(ai->ai_family, ai->ai_socktype,
			    ai->ai_protocol);
		if (INVALID_SOCKET == sh)
			continue;
		rc = connect(sh, ai->ai_addr, ai->ai_addrlen);
		if (-1 != rc)
			fd = sh;
		else
			close(sh);
	}
	freeaddrinfo(ai_list);
#else
	fd = -1;
#endif

	return fd;
}
#else
NONEMPTY_TRANSLATION_UNIT
#endif /* REFCLOCK && CLOCK_NMEA */
