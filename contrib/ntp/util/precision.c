#include "ntp_unixtime.h"

#include <stdio.h>

#define	DEFAULT_SYS_PRECISION	-99

int default_get_resolution();
int default_get_precision();

int
main(
	int argc,
	char *argv[]
	)
{
	printf("log2(resolution) = %d, log2(precision) = %d\n",
	       default_get_resolution(),
	       default_get_precision());
	return 0;
}

/* Find the resolution of the system clock by watching how the current time
 * changes as we read it repeatedly.
 *
 * struct timeval is only good to 1us, which may cause problems as machines
 * get faster, but until then the logic goes:
 *
 * If a machine has resolution (i.e. accurate timing info) > 1us, then it will
 * probably use the "unused" low order bits as a counter (to force time to be
 * a strictly increaing variable), incrementing it each time any process
 * requests the time [[ or maybe time will stand still ? ]].
 *
 * SO: the logic goes:
 *
 *      IF      the difference from the last time is "small" (< MINSTEP)
 *      THEN    this machine is "counting" with the low order bits
 *      ELIF    this is not the first time round the loop
 *      THEN    this machine *WAS* counting, and has now stepped
 *      ELSE    this machine has resolution < time to read clock
 *
 * SO: if it exits on the first loop, assume "full accuracy" (1us)
 *     otherwise, take the log2(observered difference, rounded UP)
 *
 * MINLOOPS > 1 ensures that even if there is a STEP between the initial call
 * and the first loop, it doesn't stop too early.
 * Making it even greater allows MINSTEP to be reduced, assuming that the
 * chance of MINSTEP-1 other processes getting in and calling gettimeofday
 * between this processes's calls.
 * Reducing MINSTEP may be necessary as this sets an upper bound for the time
 * to actually call gettimeofday.
 */

#define	DUSECS	1000000
#define	HUSECS	(1024 * 1024)
#define	MINSTEP	5	/* some systems increment uS on each call */
/* Don't use "1" as some *other* process may read too*/
/*We assume no system actually *ANSWERS* in this time*/
#define MAXSTEP 20000   /* maximum clock increment (us) */
#define MINLOOPS 5      /* minimum number of step samples */
#define	MAXLOOPS HUSECS	/* Assume precision < .1s ! */

int
default_get_resolution(void)
{
	struct timeval tp;
	struct timezone tzp;
	long last;
	int i;
	long diff;
	long val;
	int minsteps = MINLOOPS;	/* need at least this many steps */

	gettimeofday(&tp, &tzp);
	last = tp.tv_usec;
	for (i = - --minsteps; i< MAXLOOPS; i++) {
		gettimeofday(&tp, &tzp);
		diff = tp.tv_usec - last;
		if (diff < 0) diff += DUSECS;
		if (diff > MINSTEP) if (minsteps-- <= 0) break;
		last = tp.tv_usec;
	}

	printf("resolution = %ld usec after %d loop%s\n",
	       diff, i, (i==1) ? "" : "s");

	diff = (diff *3)/2;
	if (i >= MAXLOOPS) {
		printf(
			"     (Boy this machine is fast ! %d loops without a step)\n",
			MAXLOOPS);
		diff = 1; /* No STEP, so FAST machine */
	}
	if (i == 0) {
		printf(
			"     (The resolution is less than the time to read the clock -- Assume 1us)\n");
		diff = 1; /* time to read clock >= resolution */
	}
	for (i=0, val=HUSECS; val>0; i--, val >>= 1) if (diff >= val) return i;
	printf("     (Oh dear -- that wasn't expected ! I'll guess !)\n");
	return DEFAULT_SYS_PRECISION /* Something's BUST, so lie ! */;
}

/* ===== Rest of this code lifted straight from xntpd/ntp_proto.c ! ===== */

/*
 * This routine calculates the differences between successive calls to
 * gettimeofday(). If a difference is less than zero, the us field
 * has rolled over to the next second, so we add a second in us. If
 * the difference is greater than zero and less than MINSTEP, the
 * clock has been advanced by a small amount to avoid standing still.
 * If the clock has advanced by a greater amount, then a timer interrupt
 * has occurred and this amount represents the precision of the clock.
 * In order to guard against spurious values, which could occur if we
 * happen to hit a fat interrupt, we do this for MINLOOPS times and
 * keep the minimum value obtained.
 */  
int
default_get_precision(void)
{
	struct timeval tp;
	struct timezone tzp;
#ifdef HAVE_GETCLOCK
	struct timespec ts;
#endif
	long last;
	int i;
	long diff;
	long val;
	long usec;

	usec = 0;
	val = MAXSTEP;
#ifdef HAVE_GETCLOCK
	(void) getclock(TIMEOFDAY, &ts);
	tp.tv_sec = ts.tv_sec;
	tp.tv_usec = ts.tv_nsec / 1000;
#else /*  not HAVE_GETCLOCK */
	GETTIMEOFDAY(&tp, &tzp);
#endif /* not HAVE_GETCLOCK */
	last = tp.tv_usec;
	for (i = 0; i < MINLOOPS && usec < HUSECS;) {
#ifdef HAVE_GETCLOCK
		(void) getclock(TIMEOFDAY, &ts);
		tp.tv_sec = ts.tv_sec;
		tp.tv_usec = ts.tv_nsec / 1000;
#else /*  not HAVE_GETCLOCK */
		GETTIMEOFDAY(&tp, &tzp);
#endif /* not HAVE_GETCLOCK */
		diff = tp.tv_usec - last;
		last = tp.tv_usec;
		if (diff < 0)
		    diff += DUSECS;
		usec += diff;
		if (diff > MINSTEP) {
			i++;
			if (diff < val)
			    val = diff;
		}
	}
	printf("precision  = %ld usec after %d loop%s\n",
	       val, i, (i == 1) ? "" : "s");
	if (usec >= HUSECS) {
		printf("     (Boy this machine is fast ! usec was %ld)\n",
		       usec);
		val = MINSTEP;	/* val <= MINSTEP; fast machine */
	}
	diff = HUSECS;
	for (i = 0; diff > val; i--)
	    diff >>= 1;
	return (i);
}
