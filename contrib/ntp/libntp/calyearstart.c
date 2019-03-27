/*
 * calyearstart - determine the NTP time at midnight of January 1 in
 *		  the year of the given date.
 */
#include <config.h>
#include <sys/types.h>

#include "ntp_types.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"
#include "ntp_assert.h"

/*
 * Juergen Perlinger, 2010-05-02
 *
 * Redone in terms of the calendar functions. It's rather simple:
 * - expand the NTP time stamp
 * - split into days and seconds since midnight, dropping the partial day
 * - get full number of days before year start in NTP epoch
 * - convert to seconds, truncated to 32 bits.
 */
u_int32
calyearstart(u_int32 ntptime, const time_t *pivot)
{
	u_int32      ndays; /* elapsed days since NTP starts */
	vint64       vlong;
	ntpcal_split split;

	vlong = ntpcal_ntp_to_ntp(ntptime, pivot);
	split = ntpcal_daysplit(&vlong);
	ndays = ntpcal_rd_to_ystart(split.hi + DAY_NTP_STARTS)
	      - DAY_NTP_STARTS;

	return (u_int32)(ndays * SECSPERDAY);
}

/*
 * calmonthstart - get NTP time at midnight of the first day of the
 * current month.
 */
u_int32
calmonthstart(u_int32 ntptime, const time_t *pivot)
{
	u_int32      ndays; /* elapsed days since NTP starts */
	vint64       vlong;
	ntpcal_split split;

	vlong = ntpcal_ntp_to_ntp(ntptime, pivot);
	split = ntpcal_daysplit(&vlong);
	ndays = ntpcal_rd_to_mstart(split.hi + DAY_NTP_STARTS)
	      - DAY_NTP_STARTS;

	return (u_int32)(ndays * SECSPERDAY);
}

/*
 * calweekstart - get NTP time at midnight of the last Monday on or
 * before the current date.
 */
u_int32
calweekstart(u_int32 ntptime, const time_t *pivot)
{
	u_int32      ndays; /* elapsed days since NTP starts */
	vint64       vlong;
	ntpcal_split split;

	vlong = ntpcal_ntp_to_ntp(ntptime, pivot);
	split = ntpcal_daysplit(&vlong);
	ndays = ntpcal_weekday_le(split.hi + DAY_NTP_STARTS, CAL_MONDAY)
	      - DAY_NTP_STARTS;

	return (u_int32)(ndays * SECSPERDAY);
}

/*
 * caldaystart - get NTP time at midnight of the current day.
 */
u_int32
caldaystart(u_int32 ntptime, const time_t *pivot)
{
	vint64       vlong;
	ntpcal_split split;

	vlong = ntpcal_ntp_to_ntp(ntptime, pivot);
	split = ntpcal_daysplit(&vlong);
	
	return ntptime - split.lo;
}
