/*
 * This program can be used to calibrate the clock reading jitter of a
 * particular CPU and operating system. It first tickles every element
 * of an array, in order to force pages into memory, then repeatedly
 * reads the system clock and, finally, writes out the time values for
 * later analysis. From this you can determine the jitter and if the
 * clock ever runs backwards.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include "ntp_fp.h"

#define NBUF	800002
#define JAN_1970 2208988800UL		/* Unix base epoch */
#define CLOCK_GETTIME			/* Solaris hires clock */

char progname[10];
double sys_residual;
double average;
void sys_gettime(l_fp *);

int
main(
	int argc,
	char *argv[]
	)
{
	l_fp tr;
	int i, j;
	double dtemp, gtod[NBUF];

	/*
	 * Force pages into memory
	 */
	for (i = 0; i < NBUF; i ++)
	    gtod[i] = 0;

	/*
	 * Construct gtod array
	 */
	for (i = 0; i < NBUF; i ++) {
		get_systime(&tr);
		LFPTOD(&tr, gtod[i]);
	}

	/*
	 * Write out gtod array for later processing with Matlab
	 */
	average = 0;
	for (i = 0; i < NBUF - 2; i++) {
		gtod[i] = gtod[i + 1] - gtod[i];
		printf("%13.9f\n", gtod[i]);
		average += gtod[i];
	}

	/*
	 * Sort the gtod array and display deciles
	 */
	for (i = 0; i < NBUF - 2; i++) {
		for (j = 0; j <= i; j++) {
			if (gtod[j] > gtod[i]) {
				dtemp = gtod[j];
				gtod[j] = gtod[i];
				gtod[i] = dtemp;
			}
		}
	}
	average = average / (NBUF - 2);
	fprintf(stderr, "Average %13.9f\n", average);
	fprintf(stderr, "First rank\n");
	for (i = 0; i < 10; i++)
		fprintf(stderr, "%2d %13.9f\n", i, gtod[i]);
	fprintf(stderr, "Last rank\n");
	for (i = NBUF - 12; i < NBUF - 2; i++)
		fprintf(stderr, "%2d %13.9f\n", i, gtod[i]);
	exit(0);
}


/*
 * get_systime - return system time in NTP timestamp format.
 */
void
get_systime(
	l_fp *now		/* system time */
	)
{
	double dtemp;

#if defined(HAVE_CLOCK_GETTIME) || defined(HAVE_GETCLOCK)
	struct timespec ts;	/* seconds and nanoseconds */

	/*
	 * Convert Unix clock from seconds and nanoseconds to seconds.
	 */
# ifdef HAVE_CLOCK_GETTIME
	clock_gettime(CLOCK_REALTIME, &ts);
# else
	getclock(TIMEOFDAY, &ts);
# endif
	now->l_i = ts.tv_sec + JAN_1970;
	dtemp = ts.tv_nsec / 1e9;

#else /* HAVE_CLOCK_GETTIME || HAVE_GETCLOCK */
	struct timeval tv;	/* seconds and microseconds */

	/*
	 * Convert Unix clock from seconds and microseconds to seconds.
	 */
	gettimeofday(&tv, NULL);
	now->l_i = tv.tv_sec + JAN_1970;
	dtemp = tv.tv_usec / 1e6;

#endif /* HAVE_CLOCK_GETTIME || HAVE_GETCLOCK */

	/*
	 * Renormalize to seconds past 1900 and fraction.
	 */
	dtemp += sys_residual;
	if (dtemp >= 1) {
		dtemp -= 1;
		now->l_i++;
	} else if (dtemp < -1) {
		dtemp += 1;
		now->l_i--;
	}
	dtemp *= FRAC;
	now->l_uf = (u_int32)dtemp;
}
