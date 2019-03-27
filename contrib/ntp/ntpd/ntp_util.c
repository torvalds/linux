/*
 * ntp_util.c - stuff I didn't have any other place for
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntpd.h"
#include "ntp_unixtime.h"
#include "ntp_filegen.h"
#include "ntp_if.h"
#include "ntp_stdlib.h"
#include "ntp_assert.h"
#include "ntp_calendar.h"
#include "ntp_leapsec.h"
#include "lib_strbuf.h"

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <sys/stat.h>

#ifdef HAVE_IEEEFP_H
# include <ieeefp.h>
#endif
#ifdef HAVE_MATH_H
# include <math.h>
#endif

#if defined(VMS)
# include <descrip.h>
#endif /* VMS */

/*
 * Defines used by the leapseconds stuff
 */
#define	MAX_TAI	100			/* max TAI offset (s) */
#define	L_DAY	86400UL			/* seconds per day */
#define	L_YEAR	(L_DAY * 365)		/* days per year */
#define	L_LYEAR	(L_YEAR + L_DAY)	/* days per leap year */
#define	L_4YEAR	(L_LYEAR + 3 * L_YEAR)	/* days per leap cycle */
#define	L_CENT	(L_4YEAR * 25)		/* days per century */

/*
 * This contains odds and ends, including the hourly stats, various
 * configuration items, leapseconds stuff, etc.
 */
/*
 * File names
 */
static	char *key_file_name;		/* keys file name */
static char	  *leapfile_name;		/* leapseconds file name */
static struct stat leapfile_stat;	/* leapseconds file stat() buffer */
static int /*BOOL*/have_leapfile = FALSE;
char	*stats_drift_file;		/* frequency file name */
static	char *stats_temp_file;		/* temp frequency file name */
static double wander_resid;		/* last frequency update */
double	wander_threshold = 1e-7;	/* initial frequency threshold */

/*
 * Statistics file stuff
 */
#ifndef NTP_VAR
# ifndef SYS_WINNT
#  define NTP_VAR "/var/NTP/"		/* NOTE the trailing '/' */
# else
#  define NTP_VAR "c:\\var\\ntp\\"	/* NOTE the trailing '\\' */
# endif /* SYS_WINNT */
#endif


char statsdir[MAXFILENAME] = NTP_VAR;
static FILEGEN peerstats;
static FILEGEN loopstats;
static FILEGEN clockstats;
static FILEGEN rawstats;
static FILEGEN sysstats;
static FILEGEN protostats;
static FILEGEN cryptostats;
static FILEGEN timingstats;

/*
 * This controls whether stats are written to the fileset. Provided
 * so that ntpdc can turn off stats when the file system fills up. 
 */
int stats_control;

/*
 * Last frequency written to file.
 */
static double prev_drift_comp;		/* last frequency update */

/*
 * Function prototypes
 */
static	void	record_sys_stats(void);
	void	ntpd_time_stepped(void);
static  void	check_leap_expiration(int, uint32_t, const time_t*);

/* 
 * Prototypes
 */
#ifdef DEBUG
void	uninit_util(void);
#endif

/*
 * uninit_util - free memory allocated by init_util
 */
#ifdef DEBUG
void
uninit_util(void)
{
#if defined(_MSC_VER) && defined (_DEBUG)
	_CrtCheckMemory();
#endif
	if (stats_drift_file) {
		free(stats_drift_file);
		free(stats_temp_file);
		stats_drift_file = NULL;
		stats_temp_file = NULL;
	}
	if (key_file_name) {
		free(key_file_name);
		key_file_name = NULL;
	}
	filegen_unregister("peerstats");
	filegen_unregister("loopstats");
	filegen_unregister("clockstats");
	filegen_unregister("rawstats");
	filegen_unregister("sysstats");
	filegen_unregister("protostats");
#ifdef AUTOKEY
	filegen_unregister("cryptostats");
#endif	/* AUTOKEY */
#ifdef DEBUG_TIMING
	filegen_unregister("timingstats");
#endif	/* DEBUG_TIMING */

#if defined(_MSC_VER) && defined (_DEBUG)
	_CrtCheckMemory();
#endif
}
#endif /* DEBUG */


/*
 * init_util - initialize the util module of ntpd
 */
void
init_util(void)
{
	filegen_register(statsdir, "peerstats",	  &peerstats);
	filegen_register(statsdir, "loopstats",	  &loopstats);
	filegen_register(statsdir, "clockstats",  &clockstats);
	filegen_register(statsdir, "rawstats",	  &rawstats);
	filegen_register(statsdir, "sysstats",	  &sysstats);
	filegen_register(statsdir, "protostats",  &protostats);
	filegen_register(statsdir, "cryptostats", &cryptostats);
	filegen_register(statsdir, "timingstats", &timingstats);
	/*
	 * register with libntp ntp_set_tod() to call us back
	 * when time is stepped.
	 */
	step_callback = &ntpd_time_stepped;
#ifdef DEBUG
	atexit(&uninit_util);
#endif /* DEBUG */
}


/*
 * hourly_stats - print some interesting stats
 */
void
write_stats(void)
{
	FILE	*fp;
#ifdef DOSYNCTODR
	struct timeval tv;
#if !defined(VMS)
	int	prio_set;
#endif
#ifdef HAVE_GETCLOCK
	struct timespec ts;
#endif
	int	o_prio;

	/*
	 * Sometimes having a Sun can be a drag.
	 *
	 * The kernel variable dosynctodr controls whether the system's
	 * soft clock is kept in sync with the battery clock. If it
	 * is zero, then the soft clock is not synced, and the battery
	 * clock is simply left to rot. That means that when the system
	 * reboots, the battery clock (which has probably gone wacky)
	 * sets the soft clock. That means ntpd starts off with a very
	 * confused idea of what time it is. It then takes a large
	 * amount of time to figure out just how wacky the battery clock
	 * has made things drift, etc, etc. The solution is to make the
	 * battery clock sync up to system time. The way to do THAT is
	 * to simply set the time of day to the current time of day, but
	 * as quickly as possible. This may, or may not be a sensible
	 * thing to do.
	 *
	 * CAVEAT: settimeofday() steps the sun clock by about 800 us,
	 *	   so setting DOSYNCTODR seems a bad idea in the
	 *	   case of us resolution
	 */

#if !defined(VMS)
	/*
	 * (prr) getpriority returns -1 on error, but -1 is also a valid
	 * return value (!), so instead we have to zero errno before the
	 * call and check it for non-zero afterwards.
	 */
	errno = 0;
	prio_set = 0;
	o_prio = getpriority(PRIO_PROCESS,0); /* Save setting */

	/*
	 * (prr) if getpriority succeeded, call setpriority to raise
	 * scheduling priority as high as possible.  If that succeeds
	 * as well, set the prio_set flag so we remember to reset
	 * priority to its previous value below.  Note that on Solaris
	 * 2.6 (and beyond?), both getpriority and setpriority will fail
	 * with ESRCH, because sched_setscheduler (called from main) put
	 * us in the real-time scheduling class which setpriority
	 * doesn't know about. Being in the real-time class is better
	 * than anything setpriority can do, anyhow, so this error is
	 * silently ignored.
	 */
	if ((errno == 0) && (setpriority(PRIO_PROCESS,0,-20) == 0))
		prio_set = 1;	/* overdrive */
#endif /* VMS */
#ifdef HAVE_GETCLOCK
	(void) getclock(TIMEOFDAY, &ts);
	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = ts.tv_nsec / 1000;
#else /*  not HAVE_GETCLOCK */
	GETTIMEOFDAY(&tv,(struct timezone *)NULL);
#endif /* not HAVE_GETCLOCK */
	if (ntp_set_tod(&tv,(struct timezone *)NULL) != 0)
		msyslog(LOG_ERR, "can't sync battery time: %m");
#if !defined(VMS)
	if (prio_set)
		setpriority(PRIO_PROCESS, 0, o_prio); /* downshift */
#endif /* VMS */
#endif /* DOSYNCTODR */
	record_sys_stats();
	if (stats_drift_file != 0) {

		/*
		 * When the frequency file is written, initialize the
		 * prev_drift_comp and wander_resid. Thereafter,
		 * reduce the wander_resid by half each hour. When
		 * the difference between the prev_drift_comp and
		 * drift_comp is less than the wander_resid, update
		 * the frequncy file. This minimizes the file writes to
		 * nonvolaile storage.
		 */
#ifdef DEBUG
		if (debug)
			printf("write_stats: frequency %.6lf thresh %.6lf, freq %.6lf\n",
			    (prev_drift_comp - drift_comp) * 1e6, wander_resid *
			    1e6, drift_comp * 1e6);
#endif
		if (fabs(prev_drift_comp - drift_comp) < wander_resid) {
			wander_resid *= 0.5;
			return;
		}
		prev_drift_comp = drift_comp;
		wander_resid = wander_threshold;
		if ((fp = fopen(stats_temp_file, "w")) == NULL) {
			msyslog(LOG_ERR, "frequency file %s: %m",
			    stats_temp_file);
			return;
		}
		fprintf(fp, "%.3f\n", drift_comp * 1e6);
		(void)fclose(fp);
		/* atomic */
#ifdef SYS_WINNT
		if (_unlink(stats_drift_file)) /* rename semantics differ under NT */
			msyslog(LOG_WARNING, 
				"Unable to remove prior drift file %s, %m", 
				stats_drift_file);
#endif /* SYS_WINNT */

#ifndef NO_RENAME
		if (rename(stats_temp_file, stats_drift_file))
			msyslog(LOG_WARNING, 
				"Unable to rename temp drift file %s to %s, %m", 
				stats_temp_file, stats_drift_file);
#else
		/* we have no rename NFS of ftp in use */
		if ((fp = fopen(stats_drift_file, "w")) ==
		    NULL) {
			msyslog(LOG_ERR,
			    "frequency file %s: %m",
			    stats_drift_file);
			return;
		}
#endif

#if defined(VMS)
		/* PURGE */
		{
			$DESCRIPTOR(oldvers,";-1");
			struct dsc$descriptor driftdsc = {
				strlen(stats_drift_file), 0, 0,
				    stats_drift_file };
			while(lib$delete_file(&oldvers,
			    &driftdsc) & 1);
		}
#endif
	}
}


/*
 * stats_config - configure the stats operation
 */
void
stats_config(
	int item,
	const char *invalue	/* only one type so far */
	)
{
	FILE	*fp;
	const char *value;
	size_t	len;
	double	old_drift;
	l_fp	now;
	time_t  ttnow;
#ifndef VMS
	const char temp_ext[] = ".TEMP";
#else
	const char temp_ext[] = "-TEMP";
#endif

	/*
	 * Expand environment strings under Windows NT, since the
	 * command interpreter doesn't do this, the program must.
	 */
#ifdef SYS_WINNT
	char newvalue[MAX_PATH], parameter[MAX_PATH];

	if (!ExpandEnvironmentStrings(invalue, newvalue, MAX_PATH)) {
		switch (item) {
		case STATS_FREQ_FILE:
			strlcpy(parameter, "STATS_FREQ_FILE",
				sizeof(parameter));
			break;

		case STATS_LEAP_FILE:
			strlcpy(parameter, "STATS_LEAP_FILE",
				sizeof(parameter));
			break;

		case STATS_STATSDIR:
			strlcpy(parameter, "STATS_STATSDIR",
				sizeof(parameter));
			break;

		case STATS_PID_FILE:
			strlcpy(parameter, "STATS_PID_FILE",
				sizeof(parameter));
			break;

		default:
			strlcpy(parameter, "UNKNOWN",
				sizeof(parameter));
			break;
		}
		value = invalue;
		msyslog(LOG_ERR,
			"ExpandEnvironmentStrings(%s) failed: %m\n",
			parameter);
	} else {
		value = newvalue;
	}
#else	 
	value = invalue;
#endif /* SYS_WINNT */

	switch (item) {

	/*
	 * Open and read frequency file.
	 */
	case STATS_FREQ_FILE:
		if (!value || (len = strlen(value)) == 0)
			break;

		stats_drift_file = erealloc(stats_drift_file, len + 1);
		stats_temp_file = erealloc(stats_temp_file, 
		    len + sizeof(".TEMP"));
		memcpy(stats_drift_file, value, (size_t)(len+1));
		memcpy(stats_temp_file, value, (size_t)len);
		memcpy(stats_temp_file + len, temp_ext, sizeof(temp_ext));

		/*
		 * Open drift file and read frequency. If the file is
		 * missing or contains errors, tell the loop to reset.
		 */
		if ((fp = fopen(stats_drift_file, "r")) == NULL)
			break;

		if (fscanf(fp, "%lf", &old_drift) != 1) {
			msyslog(LOG_ERR,
				"format error frequency file %s", 
				stats_drift_file);
			fclose(fp);
			break;

		}
		fclose(fp);
		loop_config(LOOP_FREQ, old_drift);
		prev_drift_comp = drift_comp;
		break;

	/*
	 * Specify statistics directory.
	 */
	case STATS_STATSDIR:

		/* - 1 since value may be missing the DIR_SEP. */
		if (strlen(value) >= sizeof(statsdir) - 1) {
			msyslog(LOG_ERR,
			    "statsdir too long (>%d, sigh)",
			    (int)sizeof(statsdir) - 2);
		} else {
			int add_dir_sep;
			size_t value_l;

			/* Add a DIR_SEP unless we already have one. */
			value_l = strlen(value);
			if (0 == value_l)
				add_dir_sep = FALSE;
			else
				add_dir_sep = (DIR_SEP !=
				    value[value_l - 1]);

			if (add_dir_sep)
				snprintf(statsdir, sizeof(statsdir),
				    "%s%c", value, DIR_SEP);
			else
				snprintf(statsdir, sizeof(statsdir),
				    "%s", value);
			filegen_statsdir();
		}
		break;

	/*
	 * Open pid file.
	 */
	case STATS_PID_FILE:
		if ((fp = fopen(value, "w")) == NULL) {
			msyslog(LOG_ERR, "pid file %s: %m",
			    value);
			break;
		}
		fprintf(fp, "%d", (int)getpid());
		fclose(fp);
		break;

	/*
	 * Read leapseconds file.
	 *
	 * Note: Currently a leap file without SHA1 signature is
	 * accepted, but if there is a signature line, the signature
	 * must be valid or the file is rejected.
	 */
	case STATS_LEAP_FILE:
		if (!value || (len = strlen(value)) == 0)
			break;

		leapfile_name = erealloc(leapfile_name, len + 1);
		memcpy(leapfile_name, value, len + 1);

		if (leapsec_load_file(
			    leapfile_name, &leapfile_stat, TRUE, TRUE))
		{
			leap_signature_t lsig;

			get_systime(&now);
			time(&ttnow);
			leapsec_getsig(&lsig);
			mprintf_event(EVNT_TAI, NULL,
				      "%d leap %s %s %s",
				      lsig.taiof,
				      fstostr(lsig.ttime),
				      leapsec_expired(now.l_ui, NULL)
					  ? "expired"
					  : "expires",
				      fstostr(lsig.etime));

			have_leapfile = TRUE;

			/* force an immediate daily expiration check of
			 * the leap seconds table
			 */
			check_leap_expiration(TRUE, now.l_ui, &ttnow);
		}
		break;

	default:
		/* oh well */
		break;
	}
}


/*
 * record_peer_stats - write peer statistics to file
 *
 * file format:
 * day (MJD)
 * time (s past UTC midnight)
 * IP address
 * status word (hex)
 * offset
 * delay
 * dispersion
 * jitter
*/
void
record_peer_stats(
	sockaddr_u *addr,
	int	status,
	double	offset,		/* offset */
	double	delay,		/* delay */
	double	dispersion,	/* dispersion */
	double	jitter		/* jitter */
	)
{
	l_fp	now;
	u_long	day;

	if (!stats_control)
		return;

	get_systime(&now);
	filegen_setup(&peerstats, now.l_ui);
	day = now.l_ui / 86400 + MJD_1900;
	now.l_ui %= 86400;
	if (peerstats.fp != NULL) {
		fprintf(peerstats.fp,
		    "%lu %s %s %x %.9f %.9f %.9f %.9f\n", day,
		    ulfptoa(&now, 3), stoa(addr), status, offset,
		    delay, dispersion, jitter);
		fflush(peerstats.fp);
	}
}


/*
 * record_loop_stats - write loop filter statistics to file
 *
 * file format:
 * day (MJD)
 * time (s past midnight)
 * offset
 * frequency (PPM)
 * jitter
 * wnder (PPM)
 * time constant (log2)
 */
void
record_loop_stats(
	double	offset,		/* offset */
	double	freq,		/* frequency (PPM) */
	double	jitter,		/* jitter */
	double	wander,		/* wander (PPM) */
	int spoll
	)
{
	l_fp	now;
	u_long	day;

	if (!stats_control)
		return;

	get_systime(&now);
	filegen_setup(&loopstats, now.l_ui);
	day = now.l_ui / 86400 + MJD_1900;
	now.l_ui %= 86400;
	if (loopstats.fp != NULL) {
		fprintf(loopstats.fp, "%lu %s %.9f %.3f %.9f %.6f %d\n",
		    day, ulfptoa(&now, 3), offset, freq * 1e6, jitter,
		    wander * 1e6, spoll);
		fflush(loopstats.fp);
	}
}


/*
 * record_clock_stats - write clock statistics to file
 *
 * file format:
 * day (MJD)
 * time (s past midnight)
 * IP address
 * text message
 */
void
record_clock_stats(
	sockaddr_u *addr,
	const char *text	/* timecode string */
	)
{
	l_fp	now;
	u_long	day;

	if (!stats_control)
		return;

	get_systime(&now);
	filegen_setup(&clockstats, now.l_ui);
	day = now.l_ui / 86400 + MJD_1900;
	now.l_ui %= 86400;
	if (clockstats.fp != NULL) {
		fprintf(clockstats.fp, "%lu %s %s %s\n", day,
		    ulfptoa(&now, 3), stoa(addr), text);
		fflush(clockstats.fp);
	}
}


/*
 * mprintf_clock_stats - write clock statistics to file with
 *			msnprintf-style formatting.
 */
int
mprintf_clock_stats(
	sockaddr_u *addr,
	const char *fmt,
	...
	)
{
	va_list	ap;
	int	rc;
	char	msg[512];

	va_start(ap, fmt);
	rc = mvsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (stats_control)
		record_clock_stats(addr, msg);

	return rc;
}

/*
 * record_raw_stats - write raw timestamps to file
 *
 * file format
 * day (MJD)
 * time (s past midnight)
 * peer ip address
 * IP address
 * t1 t2 t3 t4 timestamps
 * leap, version, mode, stratum, ppoll, precision, root delay, root dispersion, REFID
 * length and hex dump of any EFs and any legacy MAC.
 */
void
record_raw_stats(
	sockaddr_u *srcadr,
	sockaddr_u *dstadr,
	l_fp	*t1,		/* originate timestamp */
	l_fp	*t2,		/* receive timestamp */
	l_fp	*t3,		/* transmit timestamp */
	l_fp	*t4,		/* destination timestamp */
	int	leap,
	int	version,
	int	mode,
	int	stratum,
	int	ppoll,
	int	precision,
	double	root_delay,	/* seconds */
	double	root_dispersion,/* seconds */
	u_int32	refid,
	int	len,
	u_char	*extra
	)
{
	l_fp	now;
	u_long	day;

	if (!stats_control)
		return;

	get_systime(&now);
	filegen_setup(&rawstats, now.l_ui);
	day = now.l_ui / 86400 + MJD_1900;
	now.l_ui %= 86400;
	if (rawstats.fp != NULL) {
		fprintf(rawstats.fp, "%lu %s %s %s %s %s %s %s %d %d %d %d %d %d %.6f %.6f %s",
		    day, ulfptoa(&now, 3),
		    srcadr ? stoa(srcadr) : "-",
		    dstadr ? stoa(dstadr) : "-",
		    ulfptoa(t1, 9), ulfptoa(t2, 9),
		    ulfptoa(t3, 9), ulfptoa(t4, 9),
		    leap, version, mode, stratum, ppoll, precision,
		    root_delay, root_dispersion, refid_str(refid, stratum));
		if (len > 0) {
			int i;

			fprintf(rawstats.fp, " %d: ", len);
			for (i = 0; i < len; ++i) {
				fprintf(rawstats.fp, "%02x", extra[i]);
			}
		}
		fprintf(rawstats.fp, "\n");
		fflush(rawstats.fp);
	}
}


/*
 * record_sys_stats - write system statistics to file
 *
 * file format
 * day (MJD)
 * time (s past midnight)
 * time since reset
 * packets recieved
 * packets for this host
 * current version
 * old version
 * access denied
 * bad length or format
 * bad authentication
 * declined
 * rate exceeded
 * KoD sent
 */
void
record_sys_stats(void)
{
	l_fp	now;
	u_long	day;

	if (!stats_control)
		return;

	get_systime(&now);
	filegen_setup(&sysstats, now.l_ui);
	day = now.l_ui / 86400 + MJD_1900;
	now.l_ui %= 86400;
	if (sysstats.fp != NULL) {
		fprintf(sysstats.fp,
		    "%lu %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		    day, ulfptoa(&now, 3), current_time - sys_stattime,
		    sys_received, sys_processed, sys_newversion,
		    sys_oldversion, sys_restricted, sys_badlength,
		    sys_badauth, sys_declined, sys_limitrejected,
		    sys_kodsent);
		fflush(sysstats.fp);
		proto_clr_stats();
	}
}


/*
 * record_proto_stats - write system statistics to file
 *
 * file format
 * day (MJD)
 * time (s past midnight)
 * text message
 */
void
record_proto_stats(
	char	*str		/* text string */
	)
{
	l_fp	now;
	u_long	day;

	if (!stats_control)
		return;

	get_systime(&now);
	filegen_setup(&protostats, now.l_ui);
	day = now.l_ui / 86400 + MJD_1900;
	now.l_ui %= 86400;
	if (protostats.fp != NULL) {
		fprintf(protostats.fp, "%lu %s %s\n", day,
		    ulfptoa(&now, 3), str);
		fflush(protostats.fp);
	}
}


#ifdef AUTOKEY
/*
 * record_crypto_stats - write crypto statistics to file
 *
 * file format:
 * day (mjd)
 * time (s past midnight)
 * peer ip address
 * text message
 */
void
record_crypto_stats(
	sockaddr_u *addr,
	const char *text	/* text message */
	)
{
	l_fp	now;
	u_long	day;

	if (!stats_control)
		return;

	get_systime(&now);
	filegen_setup(&cryptostats, now.l_ui);
	day = now.l_ui / 86400 + MJD_1900;
	now.l_ui %= 86400;
	if (cryptostats.fp != NULL) {
		if (addr == NULL)
			fprintf(cryptostats.fp, "%lu %s 0.0.0.0 %s\n",
			    day, ulfptoa(&now, 3), text);
		else
			fprintf(cryptostats.fp, "%lu %s %s %s\n",
			    day, ulfptoa(&now, 3), stoa(addr), text);
		fflush(cryptostats.fp);
	}
}
#endif	/* AUTOKEY */


#ifdef DEBUG_TIMING
/*
 * record_timing_stats - write timing statistics to file
 *
 * file format:
 * day (mjd)
 * time (s past midnight)
 * text message
 */
void
record_timing_stats(
	const char *text	/* text message */
	)
{
	static unsigned int flshcnt;
	l_fp	now;
	u_long	day;

	if (!stats_control)
		return;

	get_systime(&now);
	filegen_setup(&timingstats, now.l_ui);
	day = now.l_ui / 86400 + MJD_1900;
	now.l_ui %= 86400;
	if (timingstats.fp != NULL) {
		fprintf(timingstats.fp, "%lu %s %s\n", day, lfptoa(&now,
		    3), text);
		if (++flshcnt % 100 == 0)
			fflush(timingstats.fp);
	}
}
#endif


/*
 * check_leap_file - See if the leapseconds file has been updated.
 *
 * Returns: n/a
 *
 * Note: This loads a new leapfile on the fly. Currently a leap file
 * without SHA1 signature is accepted, but if there is a signature line,
 * the signature must be valid or the file is rejected.
 */
void
check_leap_file(
	int           is_daily_check,
	uint32_t      ntptime       ,
	const time_t *systime
	)
{
	/* just do nothing if there is no leap file */
	if ( ! (leapfile_name && *leapfile_name))
		return;
	
	/* try to load leapfile, force it if no leapfile loaded yet */
	if (leapsec_load_file(
		    leapfile_name, &leapfile_stat,
		    !have_leapfile, is_daily_check))
		have_leapfile = TRUE;
	else if (!have_leapfile)
		return;

	check_leap_expiration(is_daily_check, ntptime, systime);
}

/*
 * check expiration of a loaded leap table
 */
static void
check_leap_expiration(
	int           is_daily_check,
	uint32_t      ntptime       ,
	const time_t *systime
	)
{
	static const char * const logPrefix = "leapsecond file";
	int  rc;

	/* test the expiration of the leap data and log with proper
	 * level and frequency (once/hour or once/day, depending on the
	 * state.
	 */
	rc = leapsec_daystolive(ntptime, systime);	
	if (rc == 0) {
		msyslog(LOG_WARNING,
			"%s ('%s'): will expire in less than one day",
			logPrefix, leapfile_name);
	} else if (is_daily_check && rc < 28) {
		if (rc < 0)
			msyslog(LOG_ERR,
				"%s ('%s'): expired less than %d day%s ago",
				logPrefix, leapfile_name, -rc, (rc == -1 ? "" : "s"));
		else
			msyslog(LOG_WARNING,
				"%s ('%s'): will expire in less than %d days",
				logPrefix, leapfile_name, 1+rc);
	}
}


/*
 * getauthkeys - read the authentication keys from the specified file
 */
void
getauthkeys(
	const char *keyfile
	)
{
	size_t len;

	len = strlen(keyfile);
	if (!len)
		return;
	
#ifndef SYS_WINNT
	key_file_name = erealloc(key_file_name, len + 1);
	memcpy(key_file_name, keyfile, len + 1);
#else
	key_file_name = erealloc(key_file_name, _MAX_PATH);
	if (len + 1 > _MAX_PATH)
		return;
	if (!ExpandEnvironmentStrings(keyfile, key_file_name,
				      _MAX_PATH)) {
		msyslog(LOG_ERR,
			"ExpandEnvironmentStrings(KEY_FILE) failed: %m");
		strlcpy(key_file_name, keyfile, _MAX_PATH);
	}
	key_file_name = erealloc(key_file_name,
				 1 + strlen(key_file_name));
#endif /* SYS_WINNT */

	authreadkeys(key_file_name);
}


/*
 * rereadkeys - read the authentication key file over again.
 */
void
rereadkeys(void)
{
	if (NULL != key_file_name)
		authreadkeys(key_file_name);
}


#if notyet
/*
 * ntp_exit - document explicitly that ntpd has exited
 */
void
ntp_exit(int retval)
{
	msyslog(LOG_ERR, "EXITING with return code %d", retval);
	exit(retval);
}
#endif

/*
 * fstostr - prettyprint NTP seconds
 */
char * fstostr(
	time_t	ntp_stamp
	)
{
	char *		buf;
	struct calendar tm;

	LIB_GETBUF(buf);
	if (ntpcal_ntp_to_date(&tm, (u_int32)ntp_stamp, NULL) < 0)
		snprintf(buf, LIB_BUFLENGTH, "ntpcal_ntp_to_date: %ld: range error",
			 (long)ntp_stamp);
	else
		snprintf(buf, LIB_BUFLENGTH, "%04d%02d%02d%02d%02d",
			 tm.year, tm.month, tm.monthday,
			 tm.hour, tm.minute);
	return buf;
}


/*
 * ntpd_time_stepped is called back by step_systime(), allowing ntpd
 * to do any one-time processing necessitated by the step.
 */
void
ntpd_time_stepped(void)
{
	u_int saved_mon_enabled;

	/*
	 * flush the monitor MRU list which contains l_fp timestamps
	 * which should not be compared across the step.
	 */
	if (MON_OFF != mon_enabled) {
		saved_mon_enabled = mon_enabled;
		mon_stop(MON_OFF);
		mon_start(saved_mon_enabled);
	}

	/* inform interpolating Windows code to allow time to go back */
#ifdef SYS_WINNT
	win_time_stepped();
#endif
}
