/*
 * Taken from FreeBSD src / lib / libc / stdtime / localtime.c 1.43 revision.
 * localtime.c 7.78.
 * tzfile.h 1.8
 * adapted to be replacement gmtime_r.
 */
#include "config.h"

#ifdef HAVE_TIME_H
#include <time.h>
#endif

#define MONSPERYEAR 12
#define DAYSPERNYEAR 365
#define DAYSPERLYEAR 366
#define SECSPERMIN 60
#define SECSPERHOUR (60*60)
#define SECSPERDAY (24*60*60)
#define DAYSPERWEEK 7
#define TM_SUNDAY	0
#define TM_MONDAY	1
#define TM_TUESDAY	2
#define TM_WEDNESDAY	3
#define TM_THURSDAY	4
#define TM_FRIDAY	5
#define TM_SATURDAY	6

#define TM_YEAR_BASE	1900

#define EPOCH_YEAR	1970
#define EPOCH_WDAY	TM_THURSDAY

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

static const int	mon_lengths[2][MONSPERYEAR] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static const int	year_lengths[2] = {
	DAYSPERNYEAR, DAYSPERLYEAR
};

static void
timesub(timep, offset, tmp)
const time_t * const			timep;
const long				offset;
struct tm * const		tmp;
{
	long			days;
	long			rem;
	long			y;
	int			yleap;
	const int *		ip;

	days = *timep / SECSPERDAY;
	rem = *timep % SECSPERDAY;
	rem += (offset);
	while (rem < 0) {
		rem += SECSPERDAY;
		--days;
	}
	while (rem >= SECSPERDAY) {
		rem -= SECSPERDAY;
		++days;
	}
	tmp->tm_hour = (int) (rem / SECSPERHOUR);
	rem = rem % SECSPERHOUR;
	tmp->tm_min = (int) (rem / SECSPERMIN);
	/*
	** A positive leap second requires a special
	** representation.  This uses "... ??:59:60" et seq.
	*/
	tmp->tm_sec = (int) (rem % SECSPERMIN) ;
	tmp->tm_wday = (int) ((EPOCH_WDAY + days) % DAYSPERWEEK);
	if (tmp->tm_wday < 0)
		tmp->tm_wday += DAYSPERWEEK;
	y = EPOCH_YEAR;
#define LEAPS_THRU_END_OF(y)	((y) / 4 - (y) / 100 + (y) / 400)
	while (days < 0 || days >= (long) year_lengths[yleap = isleap(y)]) {
		long	newy;

		newy = y + days / DAYSPERNYEAR;
		if (days < 0)
			--newy;
		days -= (newy - y) * DAYSPERNYEAR +
			LEAPS_THRU_END_OF(newy - 1) -
			LEAPS_THRU_END_OF(y - 1);
		y = newy;
	}
	tmp->tm_year = y - TM_YEAR_BASE;
	tmp->tm_yday = (int) days;
	ip = mon_lengths[yleap];
	for (tmp->tm_mon = 0; days >= (long) ip[tmp->tm_mon]; ++(tmp->tm_mon))
		days = days - (long) ip[tmp->tm_mon];
	tmp->tm_mday = (int) (days + 1);
	tmp->tm_isdst = 0;
}

/*
* Re-entrant version of gmtime.
*/
struct tm * gmtime_r(const time_t* timep, struct tm *tm)
{
	timesub(timep, 0L, tm);
	return tm;
}
