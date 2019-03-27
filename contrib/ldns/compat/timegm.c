#ifdef HAVE_CONFIG_H
#include <ldns/config.h>
#endif

#include <stdio.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <time.h>

time_t
timegm (struct tm *tm) {
	time_t ret;
	char *tz;
	
	tz = getenv("TZ");
	putenv((char*)"TZ=");
	tzset();
	ret = mktime(tm);
	if (tz) {
		char buf[256];
		snprintf(buf, sizeof(buf), "TZ=%s", tz);
		putenv(tz);
	}
	else
		putenv((char*)"TZ");
	tzset();
	return ret;
}
