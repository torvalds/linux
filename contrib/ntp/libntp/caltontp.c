/*
 * caltontp - convert a date to an NTP time
 */
#include <config.h>
#include <sys/types.h>

#include "ntp_types.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"
#include "ntp_assert.h"
#include "ntp_unixtime.h"

/*
 * Juergen Perlinger, 2008-11-12
 * Add support for full calendar calculatios. If the day-of-year is provided
 * (that is, not zero) it will be used instead of month and day-of-month;
 * otherwise a full turn through the calendar calculations will be taken.
 *
 * I know that Harlan Stenn likes to see assertions in production code, and I
 * agree there, but it would be a tricky thing here. The algorithm is quite
 * capable of producing sensible answers even to seemingly weird inputs: the
 * date <any year here>-03-00, the 0.th March of the year, will be automtically
 * treated as the last day of February, no matter whether the year is a leap
 * year or not. So adding constraints is merely for the benefit of the callers,
 * because the only thing we can check for consistency is our input, produced
 * by somebody else.
 *
 * BTW: A total roundtrip using 'caljulian' would be a quite shaky thing:
 * Because of the truncation of the NTP time stamp to 32 bits and the epoch
 * unfolding around the current time done by 'caljulian' the roundtrip does
 * *not* necessarily reproduce the input, especially if the time spec is more
 * than 68 years off from the current time...
 */

uint32_t
caltontp(
	const struct calendar *jt
	)
{
	int32_t eraday;	/* CE Rata Die number	*/
	vint64  ntptime;/* resulting NTP time	*/

	REQUIRE(jt != NULL);

	REQUIRE(jt->month <= 13);	/* permit month 0..13! */
	REQUIRE(jt->monthday <= 32);
	REQUIRE(jt->yearday <= 366);
	REQUIRE(jt->hour <= 24);
	REQUIRE(jt->minute <= MINSPERHR);
	REQUIRE(jt->second <= SECSPERMIN);

	/*
	 * First convert the date to he corresponding RataDie
	 * number. If yearday is not zero, assume that it contains a
	 * useable value and avoid all calculations involving month
	 * and day-of-month. Do a full evaluation otherwise.
	 */
	if (jt->yearday)
		eraday = ntpcal_year_to_ystart(jt->year)
		       + jt->yearday - 1;
	else
		eraday = ntpcal_date_to_rd(jt);

	ntptime = ntpcal_dayjoin(eraday - DAY_NTP_STARTS,
				 ntpcal_etime_to_seconds(jt->hour, jt->minute,
							 jt->second));
	return ntptime.d_s.lo;
}
