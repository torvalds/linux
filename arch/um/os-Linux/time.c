#include <stdlib.h>
#include <sys/time.h>

unsigned long long os_usecs(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return((unsigned long long) tv.tv_sec * 1000000 + tv.tv_usec);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
