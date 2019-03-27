/*
 * caljulian - determine the Julian date from an NTP time.
 *
 * (Note: since we use the GREGORIAN calendar, this should be renamed to
 * 'calgregorian' eventually...)
 */
#include <config.h>
#include <sys/types.h>

#include "ntp_types.h"
#include "ntp_calendar.h"

#if !(defined(ISC_CHECK_ALL) || defined(ISC_CHECK_NONE) || \
      defined(ISC_CHECK_ENSURE) || defined(ISC_CHECK_INSIST) || \
      defined(ISC_CHECK_INVARIANT))
# define ISC_CHECK_ALL
#endif

#include "ntp_assert.h"

void
caljulian(
	uint32_t		ntp,
	struct calendar *	jt
	)
{
	vint64		vlong;
	ntpcal_split	split;
	
	
	INSIST(NULL != jt);

	/*
	 * Unfold ntp time around current time into NTP domain. Split
	 * into days and seconds, shift days into CE domain and
	 * process the parts.
	 */
	vlong = ntpcal_ntp_to_ntp(ntp, NULL);
	split = ntpcal_daysplit(&vlong);
	ntpcal_daysplit_to_date(jt, &split, DAY_NTP_STARTS);
}
