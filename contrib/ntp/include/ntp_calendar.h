/*
 * ntp_calendar.h - definitions for the calendar time-of-day routine
 */
#ifndef NTP_CALENDAR_H
#define NTP_CALENDAR_H

#include <time.h>

#include "ntp_types.h"

/* gregorian calendar date */
struct calendar {
	uint16_t year;		/* year (A.D.) */
	uint16_t yearday;	/* day of year, 1 = January 1 */
	uint8_t  month;		/* month, 1 = January */
	uint8_t  monthday;	/* day of month */
	uint8_t  hour;		/* hour of day, midnight = 0 */
	uint8_t  minute;	/* minute of hour */
	uint8_t  second;	/* second of minute */
	uint8_t  weekday;	/* 0..7, 0=Sunday */
};

/* ISO week calendar date */
struct isodate {
	uint16_t year;		/* year (A.D.) */
	uint8_t	 week;		/* 1..53, week in year */
	uint8_t	 weekday;	/* 1..7, 1=Monday */
	uint8_t	 hour;		/* hour of day, midnight = 0 */
	uint8_t	 minute;	/* minute of hour */
	uint8_t	 second;	/* second of minute */
};

/* general split representation */
typedef struct {
	int32_t hi;
	int32_t lo;
} ntpcal_split;

typedef time_t (*systime_func_ptr)(time_t *);

/*
 * set the function for getting the system time. This is mostly used for
 * unit testing to provide a fixed / shifted time stamp. Setting the
 * value to NULL restores the original function, that is, 'time()',
 * which is also the automatic default.
 */
extern systime_func_ptr ntpcal_set_timefunc(systime_func_ptr);

/*
 * days-of-week
 */
#define CAL_SUNDAY	0
#define CAL_MONDAY	1
#define CAL_TUESDAY	2
#define CAL_WEDNESDAY	3
#define CAL_THURSDAY	4
#define CAL_FRIDAY	5
#define CAL_SATURDAY	6
#define CAL_SUNDAY7	7	/* also sunday */

/*
 * Days in each month.	30 days hath September...
 */
#define	JAN	31
#define	FEB	28
#define	FEBLEAP	29
#define	MAR	31
#define	APR	30
#define	MAY	31
#define	JUN	30
#define	JUL	31
#define	AUG	31
#define	SEP	30
#define	OCT	31
#define	NOV	30
#define	DEC	31

/*
 * We deal in a 4 year cycle starting at March 1, 1900.	 We assume
 * we will only want to deal with dates since then, and not to exceed
 * the rollover day in 2036.
 */
#define	SECSPERMIN	(60)			/* seconds per minute */
#define	MINSPERHR	(60)			/* minutes per hour */
#define	HRSPERDAY	(24)			/* hours per day */
#define	DAYSPERWEEK	(7)			/* days per week */
#define	DAYSPERYEAR	(365)			/* days per year */

#define	SECSPERHR	(SECSPERMIN * MINSPERHR)
#define	SECSPERDAY	(SECSPERHR * HRSPERDAY)
#define	SECSPERWEEK	(DAYSPERWEEK * SECSPERDAY)
#define	SECSPERYEAR	(365 * SECSPERDAY)	/* regular year */
#define	SECSPERLEAPYEAR	(366 * SECSPERDAY)	/* leap year */
#define	SECSPERAVGYEAR	31556952		/* mean year length over 400yrs */

#define GPSWEEKS	1024			/* GPS week cycle */
/*
 * Gross hacks.	 I have illicit knowlege that there won't be overflows
 * here, the compiler often can't tell this.
 */
#define	TIMES60(val)	((((val)<<4) - (val))<<2)	/* *(16 - 1) * 4 */
#define	TIMES24(val)	(((val)<<4) + ((val)<<3))	/* *16 + *8 */
#define	TIMES7(val)	(((val)<<3) - (val))		/* *8  - *1 */
#define	TIMESDPERC(val)	(((val)<<10) + ((val)<<8) \
			+ ((val)<<7) + ((val)<<5) \
			+ ((val)<<4) + ((val)<<2) + (val))	/* *big* hack */


extern	const char * const months[12];
extern	const char * const daynames[7];

extern	void	 caljulian	(uint32_t, struct calendar *);
extern	uint32_t caltontp	(const struct calendar *);

/*
 * Convert between 'time_t' and 'vint64'
 */
extern vint64 time_to_vint64(const time_t *);
extern time_t vint64_to_time(const vint64 *);

/*
 * Get the build date & time. ATTENTION: The time zone is not specified!
 * This depends entirely on the C compilers' capabilities to properly
 * expand the '__TIME__' and '__DATE__' macros, as required by the C
 * standard.
 */
extern int
ntpcal_get_build_date(struct calendar * /* jd */);

/*
 * Convert a timestamp in NTP scale to a time_t value in the UN*X
 * scale with proper epoch unfolding around a given pivot or the
 * current system time.
 */
extern vint64
ntpcal_ntp_to_time(uint32_t /* ntp */, const time_t * /* pivot */);

/*
 * Convert a timestamp in NTP scale to a 64bit seconds value in the NTP
 * scale with proper epoch unfolding around a given pivot or the current
 * system time.
 * Note: The pivot must be given in UN*X time scale!
 */
extern vint64
ntpcal_ntp_to_ntp(uint32_t /* ntp */, const time_t * /* pivot */);

/*
 * Split a time stamp in seconds into elapsed days and elapsed seconds
 * since midnight.
 */
extern ntpcal_split
ntpcal_daysplit(const vint64 *);

/*
 * Merge a number of days and a number of seconds into seconds,
 * expressed in 64 bits to avoid overflow.
 */
extern vint64
ntpcal_dayjoin(int32_t /* days */, int32_t /* seconds */);

/* Get the number of leap years since epoch for the number of elapsed
 * full years
 */
extern int32_t
ntpcal_leapyears_in_years(int32_t /* years */);

/*
 * Convert elapsed years in Era into elapsed days in Era.
 */
extern int32_t
ntpcal_days_in_years(int32_t /* years */);

/*
 * Convert a number of elapsed month in a year into elapsed days
 * in year.
 *
 * The month will be normalized, and 'res.hi' will contain the
 * excessive years that must be considered when converting the years,
 * while 'res.lo' will contain the days since start of the
 * year. (Expect the resulting days to be negative, with a positive
 * excess! But then, we need no leap year flag, either...)
 */
extern ntpcal_split
ntpcal_days_in_months(int32_t /* months */);

/*
 * Convert ELAPSED years/months/days of gregorian calendar to elapsed
 * days in Gregorian epoch. No range checks done here!
 */
extern int32_t
ntpcal_edate_to_eradays(int32_t /* years */, int32_t /* months */, int32_t /* mdays */);

/*
 * Convert a time spec to seconds. No range checks done here!
 */
extern int32_t
ntpcal_etime_to_seconds(int32_t /* hours */, int32_t /* minutes */, int32_t /* seconds */);

/*
 * Convert ELAPSED years/months/days of gregorian calendar to elapsed
 * days in year.
 *
 * Note: This will give the true difference to the start of the given year,
 * even if months & days are off-scale.
 */
extern int32_t
ntpcal_edate_to_yeardays(int32_t /* years */, int32_t /* months */, int32_t /* mdays */);

/*
 * Convert the date part of a 'struct tm' (that is, year, month,
 * day-of-month) into the RataDie of that day.
 */
extern int32_t
ntpcal_tm_to_rd(const struct tm * /* utm */);

/*
 * Convert the date part of a 'struct calendar' (that is, year, month,
 * day-of-month) into the RataDie of that day.
 */
extern int32_t
ntpcal_date_to_rd(const struct calendar * /* jt */);

/*
 * Given the number of elapsed days in the calendar era, split this
 * number into the number of elapsed years in 'res.quot' and the
 * number of elapsed days of that year in 'res.rem'.
 *
 * if 'isleapyear' is not NULL, it will receive an integer that is 0
 * for regular years and a non-zero value for leap years.
 *
 * The input is limited to [-2^30, 2^30-1]. If the days exceed this
 * range, errno is set to EDOM and the result is saturated.
 */
extern ntpcal_split
ntpcal_split_eradays(int32_t /* days */, int/*BOOL*/ * /* isleapyear */);

/*
 * Given a number of elapsed days in a year and a leap year indicator,
 * split the number of elapsed days into the number of elapsed months
 * in 'res.quot' and the number of elapsed days of that month in
 * 'res.rem'.
 */
extern ntpcal_split
ntpcal_split_yeardays(int32_t /* eyd */, int/*BOOL*/ /* isleapyear */);

/*
 * Convert a RataDie number into the date part of a 'struct
 * calendar'. Return 0 if the year is regular year, !0 if the year is
 * a leap year.
 */
extern int/*BOOL*/
ntpcal_rd_to_date(struct calendar * /* jt */, int32_t /* rd */);

/*
 * Convert a RataDie number into the date part of a 'struct
 * tm'. Return 0 if the year is regular year, !0 if the year is a leap
 * year.
 */
extern int/*BOOL*/
ntpcal_rd_to_tm(struct tm * /* utm */, int32_t /* rd */);

/*
 * Take a value of seconds since midnight and split it into hhmmss in
 * a 'struct calendar'. Return excessive days.
 */
extern int32_t
ntpcal_daysec_to_date(struct calendar * /* jt */, int32_t /* secs */);

/*
 * Take the time part of a 'struct calendar' and return the seconds
 * since midnight.
 */
extern int32_t
ntpcal_date_to_daysec(const struct calendar *);

/*
 * Take a value of seconds since midnight and split it into hhmmss in
 * a 'struct tm'. Return excessive days.
 */
extern int32_t
ntpcal_daysec_to_tm(struct tm * /* utm */, int32_t /* secs */);

extern int32_t
ntpcal_tm_to_daysec(const struct tm * /* utm */);

/*
 * convert a year number to rata die of year start
 */
extern int32_t
ntpcal_year_to_ystart(int32_t /* year */);

/*
 * For a given RataDie, get the RataDie of the associated year start,
 * that is, the RataDie of the last January,1st on or before that day.
 */
extern int32_t
ntpcal_rd_to_ystart(int32_t /* rd */);

/*
 * convert a RataDie to the RataDie of start of the calendar month.
 */
extern int32_t
ntpcal_rd_to_mstart(int32_t /* year */);


extern int
ntpcal_daysplit_to_date(struct calendar * /* jt */,
			const ntpcal_split * /* ds */, int32_t /* dof */);

extern int
ntpcal_daysplit_to_tm(struct tm * /* utm */, const ntpcal_split * /* ds */,
		      int32_t /* dof */);

extern int
ntpcal_time_to_date(struct calendar * /* jd */, const vint64 * /* ts */);

extern int32_t
ntpcal_periodic_extend(int32_t /* pivot */, int32_t /* value */,
		       int32_t /* cycle */);

extern int
ntpcal_ntp64_to_date(struct calendar * /* jd */, const vint64 * /* ntp */);

extern int
ntpcal_ntp_to_date(struct calendar * /* jd */,	uint32_t /* ntp */,
		   const time_t * /* pivot */);

extern vint64
ntpcal_date_to_ntp64(const struct calendar * /* jd */);

extern uint32_t
ntpcal_date_to_ntp(const struct calendar * /* jd */);

extern time_t
ntpcal_date_to_time(const struct calendar * /* jd */);

/*
 * ISO week-calendar conversions
 */
extern int32_t
isocal_weeks_in_years(int32_t  /* years */);

/*
 * The input is limited to [-2^30, 2^30-1]. If the weeks exceed this
 * range, errno is set to EDOM and the result is saturated.
 */
extern ntpcal_split
isocal_split_eraweeks(int32_t /* weeks */);

extern int
isocal_ntp64_to_date(struct isodate * /* id */, const vint64 * /* ntp */);

extern int
isocal_ntp_to_date(struct isodate * /* id */, uint32_t /* ntp */,
		   const time_t * /* pivot */);

extern vint64
isocal_date_to_ntp64(const struct isodate * /* id */);

extern uint32_t
isocal_date_to_ntp(const struct isodate * /* id */);


/*
 * day-of-week calculations
 *
 * Given a RataDie and a day-of-week, calculate a RDN that is reater-than,
 * greater-or equal, closest, less-or-equal or less-than the given RDN
 * and denotes the given day-of-week
 */
extern int32_t
ntpcal_weekday_gt(int32_t  /* rdn */, int32_t /* dow */);

extern int32_t
ntpcal_weekday_ge(int32_t /* rdn */, int32_t /* dow */);

extern int32_t
ntpcal_weekday_close(int32_t /* rdn */, int32_t  /* dow */);

extern int32_t
ntpcal_weekday_le(int32_t /* rdn */, int32_t /* dow */);

extern int32_t
ntpcal_weekday_lt(int32_t /* rdn */, int32_t /* dow */);


/*
 * handling of base date spec
 */
extern int32_t
basedate_eval_buildstamp(void);

extern int32_t
basedate_eval_string(const char *str);

extern int32_t
basedate_set_day(int32_t dayno);

extern uint32_t
basedate_get_day(void);

extern time_t
basedate_get_eracenter(void);

extern time_t
basedate_get_erabase(void);

extern uint32_t
basedate_get_gpsweek(void);

extern uint32_t
basedate_expand_gpsweek(unsigned short weekno);

/*
 * Additional support stuff for Ed Rheingold's calendrical calculations
 */

/*
 * Start day of NTP time as days past 0000-12-31 in the proleptic
 * Gregorian calendar. (So 0001-01-01 is day number 1; this is the Rata
 * Die counting scheme used by Ed Rheingold in his book "Calendrical
 * Calculations".)
 */
#define	DAY_NTP_STARTS 693596

/*
 * Start day of the UNIX epoch. This is the Rata Die of 1970-01-01.
 */
#define DAY_UNIX_STARTS 719163

/*
 * Start day of the GPS epoch. This is the Rata Die of 1980-01-06
 */
#define DAY_GPS_STARTS 722819

/*
 * Difference between UN*X and NTP epoch (25567).
 */
#define NTP_TO_UNIX_DAYS (DAY_UNIX_STARTS - DAY_NTP_STARTS)

/*
 * Difference between GPS and NTP epoch (29224)
 */
#define NTP_TO_GPS_DAYS (DAY_GPS_STARTS - DAY_NTP_STARTS)

/*
 * Days in a normal 4 year leap year calendar cycle (1461).
 */
#define	GREGORIAN_NORMAL_LEAP_CYCLE_DAYS	(4 * 365 + 1)

/*
 * Days in a normal 100 year leap year calendar (36524).  We lose a
 * leap day in years evenly divisible by 100 but not by 400.
 */
#define	GREGORIAN_NORMAL_CENTURY_DAYS	\
			(25 * GREGORIAN_NORMAL_LEAP_CYCLE_DAYS - 1)

/*
 * The Gregorian calendar is based on a 400 year cycle. This is the
 * number of days in each cycle (146097).  We gain a leap day in years
 * divisible by 400 relative to the "normal" century.
 */
#define	GREGORIAN_CYCLE_DAYS (4 * GREGORIAN_NORMAL_CENTURY_DAYS + 1)

/*
 * Number of weeks in 400 years (20871).
 */
#define	GREGORIAN_CYCLE_WEEKS (GREGORIAN_CYCLE_DAYS / 7)

#define	is_leapyear(y)	(!((y) % 4) && !(!((y) % 100) && (y) % 400))

#endif
