#ifdef HAVE_CONFIG_H
#include <ldns/config.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	/* no thread safety. */
	*result = *gmtime(timep);
	return result;
}
