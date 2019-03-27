/* taken from ldns 1.6.1 */
#include "config.h"
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include "util/locks.h"

/** the lock for ctime buffer */
static lock_basic_type ctime_lock;
/** has it been inited */
static int ctime_r_init = 0;

/** cleanup ctime_r on exit */
static void
ctime_r_cleanup(void)
{
	if(ctime_r_init) {
		ctime_r_init = 0;
		lock_basic_destroy(&ctime_lock);
	}
}

char *ctime_r(const time_t *timep, char *buf)
{
	char* result;
	if(!ctime_r_init) {
		/* still small race where this init can be done twice,
		 * which is mostly harmless */
		ctime_r_init = 1;
		lock_basic_init(&ctime_lock);
		atexit(&ctime_r_cleanup);
	}
	lock_basic_lock(&ctime_lock);
	result = ctime(timep);
	if(buf && result) {
		if(strlen(result) > 10 && result[7]==' ' && result[8]=='0')
			result[8]=' '; /* fix error in windows ctime */
		strcpy(buf, result);
	}
	lock_basic_unlock(&ctime_lock);
	return result;
}
