/*
 * This program can be used to calibrate the clock reading jitter of a
 * particular CPU and operating system. It first tickles every element
 * of an array, in order to force pages into memory, then repeatedly calls
 * gettimeofday() and, finally, writes out the time values for later
 * analysis. From this you can determine the jitter and if the clock ever
 * runs backwards.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntp_types.h"

#include <stdio.h>
#include <stdlib.h>

#define NBUF 100001		/* size of basic histogram */
#define NSRT 20000		/* size of overflow histogram */
#define NCNT (600 * 1000000)	/* sample interval (us) */

int col (const void *, const void *);

int
main(
	int argc,
	char *argv[]
	)
{
	struct timeval ts, tr, tp;
	struct timezone tzp;
	int i, j, n;
	long t, u, v, w, gtod[NBUF], ovfl[NSRT];

	/*
	 * Force pages into memory
	 */
	for (i = 0; i < NBUF; i++)
		gtod[i] = 0;
	for (i = 0; i < NSRT; i++)
		ovfl[i] = 0;

	/*
	 * Construct histogram
	 */
	n = 0;
	gettimeofday(&ts, &tzp);
	t = ts.tv_sec * 1000000 + ts.tv_usec;
	v = t;
	while (1) {
		gettimeofday(&tr, &tzp);
		u = tr.tv_sec * 1000000 + tr.tv_usec; 
		if (u - v > NCNT)
			break;
		w = u - t;
		if (w <= 0) {
/*
			printf("error <= 0 %ld %d %d, %d %d\n", w, ts.tv_sec,
			       ts.tv_usec, tr.tv_sec, tr.tv_usec);
*/
		} else if (w > NBUF - 1) {
			ovfl[n] = w;
			if (n < NSRT - 1)
				n++;
		} else {
			gtod[w]++;
		}
		ts = tr;
		t = u;
	}

	/*
	 * Write out histogram
	 */
	for (i = 0; i < NBUF - 1; i++) {
		if (gtod[i] > 0)
			printf("%ld %ld\n", i, gtod[i]);
	}
	if (n == 0)
		return;
	qsort(&ovfl, (size_t)n, sizeof(ovfl[0]), col);
	w = 0;
	j = 0;
	for (i = 0; i < n; i++) {
		if (ovfl[i] != w) {
			if (j > 0)
				printf("%ld %ld\n", w, j);
			w = ovfl[i];
			j = 1;
		} else
			j++;
	}
	if (j > 0)
		printf("%ld %ld\n", w, j);
 
	exit(0);
}

int
col(
	const void *vx,
	const void *vy
	)
{
	const long *x = vx;
	const long *y = vy;

	return (*x - *y);
}
