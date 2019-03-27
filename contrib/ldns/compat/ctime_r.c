#ifdef HAVE_CONFIG_H
#include <ldns/config.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif

char *ctime_r(const time_t *timep, char *buf)
{
	/* no thread safety. */
	char* result = ctime(timep);
	if(buf && result)
		strcpy(buf, result);
	return result;
}
