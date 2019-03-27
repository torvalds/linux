/*
 * buftvtots - pull a Unix-format (struct timeval) time stamp out of
 *	       an octet stream and convert it to a l_fp time stamp.
 *	       This is useful when using the clock line discipline.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ntp_fp.h"
#include "ntp_string.h"
#include "timevalops.h"

#ifndef SYS_WINNT
int
buftvtots(
	const char *bufp,
	l_fp *ts
	)
{
	struct timeval tv;

	/*
	 * copy to adhere to alignment restrictions
	 */
	memcpy(&tv, bufp, sizeof(tv));

	/*
	 * and use it
	 */
	if (tv.tv_usec > MICROSECONDS - 1)
		return FALSE;

	*ts = tval_stamp_to_lfp(tv);

	return TRUE;
}
#endif	/* !SYS_WINNT */
