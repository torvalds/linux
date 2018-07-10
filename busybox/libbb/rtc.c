/*
 * Common RTC functions
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

#include "libbb.h"
#include "rtc_.h"

#if ENABLE_FEATURE_HWCLOCK_ADJTIME_FHS
# define ADJTIME_PATH "/var/lib/hwclock/adjtime"
#else
# define ADJTIME_PATH "/etc/adjtime"
#endif

int FAST_FUNC rtc_adjtime_is_utc(void)
{
	int utc = 0;
	FILE *f = fopen_for_read(ADJTIME_PATH);

	if (f) {
		char buffer[128];

		while (fgets(buffer, sizeof(buffer), f)) {
			if (is_prefixed_with(buffer, "UTC")) {
				utc = 1;
				break;
			}
		}
		fclose(f);
	}

	return utc;
}

/* rtc opens are exclusive.
 * Try to run two "hwclock -w" at the same time to see it.
 * Users wouldn't expect that to fail merely because /dev/rtc
 * was momentarily busy, let's try a bit harder on errno == EBUSY.
 */
static int open_loop_on_busy(const char *name, int flags)
{
	int rtc;
	/*
	 * Tested with two parallel "hwclock -w" loops.
	 * With try = 10, no failures with 2x1000000 loop iterations.
	 */
	int try = 1000 / 20;
 again:
	errno = 0;
	rtc = open(name, flags);
	if (errno == EBUSY) {
		usleep(20 * 1000);
		if (--try != 0)
			goto again;
		/* EBUSY. Last try, exit on error instead of returning -1 */
		return xopen(name, flags);
	}
	return rtc;
}

/* Never fails */
int FAST_FUNC rtc_xopen(const char **default_rtc, int flags)
{
	int rtc;
	const char *name =
		"/dev/rtc""\0"
		"/dev/rtc0""\0"
		"/dev/misc/rtc""\0";

	if (!*default_rtc)
		goto try_name;
	name = ""; /*else: we have rtc name, don't try other names */

	for (;;) {
		rtc = open_loop_on_busy(*default_rtc, flags);
		if (rtc >= 0)
			return rtc;
		if (!name[0])
			return xopen(*default_rtc, flags);
 try_name:
		*default_rtc = name;
		name += strlen(name) + 1;
	}
}

void FAST_FUNC rtc_read_tm(struct tm *ptm, int fd)
{
	memset(ptm, 0, sizeof(*ptm));
	xioctl(fd, RTC_RD_TIME, ptm);
	ptm->tm_isdst = -1; /* "not known" */
}

time_t FAST_FUNC rtc_tm2time(struct tm *ptm, int utc)
{
	char *oldtz = oldtz; /* for compiler */
	time_t t;

	if (utc) {
		oldtz = getenv("TZ");
		putenv((char*)"TZ=UTC0");
		tzset();
	}

	t = mktime(ptm);

	if (utc) {
		unsetenv("TZ");
		if (oldtz)
			putenv(oldtz - 3);
		tzset();
	}

	return t;
}
