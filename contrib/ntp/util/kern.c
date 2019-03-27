/*
 * This program simulates a first-order, type-II phase-lock loop using
 * actual code segments from modified kernel distributions for SunOS,
 * Ultrix and OSF/1 kernels. These segments do not use any licensed code.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <sys/time.h>

#ifdef HAVE_TIMEX_H
# include "timex.h"
#endif

/*
 * Phase-lock loop definitions
 */
#define HZ 100			/* timer interrupt frequency (Hz) */
#define MAXPHASE 512000		/* max phase error (us) */
#define MAXFREQ 200		/* max frequency error (ppm) */
#define TAU 2			/* time constant (shift 0 - 6) */
#define POLL 16			/* interval between updates (s) */
#define MAXSEC 1200		/* max interval between updates (s) */

/*
 * Function declarations
 */
void hardupdate();
void hardclock();
void second_overflow();

/*
 * Kernel variables
 */
int tick;			/* timer interrupt period (us) */
int fixtick;			/* amortization constant (ppm) */
struct timeval timex;		/* ripoff of kernel time variable */

/*
 * Phase-lock loop variables
 */
int time_status = TIME_BAD;	/* clock synchronization status */
long time_offset = 0;		/* time adjustment (us) */
long time_constant = 0;		/* pll time constant */
long time_tolerance = MAXFREQ;	/* frequency tolerance (ppm) */
long time_precision = 1000000 / HZ; /* clock precision (us) */
long time_maxerror = MAXPHASE;	/* maximum error (us) */
long time_esterror = MAXPHASE;	/* estimated error (us) */
long time_phase = 0;		/* phase offset (scaled us) */
long time_freq = 0;		/* frequency offset (scaled ppm) */
long time_adj = 0;		/* tick adjust (scaled 1 / HZ) */
long time_reftime = 0;		/* time at last adjustment (s) */

/*
 * Simulation variables
 */
double timey = 0;		/* simulation time (us) */
long timez = 0;			/* current error (us) */
long poll_interval = 0;		/* poll counter */

/*
 * Simulation test program
 */
int
main(
	int argc,
	char *argv[]
	)
{
	tick = 1000000 / HZ;
	fixtick = 1000000 % HZ;
	timex.tv_sec = 0;
	timex.tv_usec = MAXPHASE;
	time_freq = 0;
	time_constant = TAU;
	printf("tick %d us, fixtick %d us\n", tick, fixtick);
	printf("      time    offset      freq   _offset     _freq      _adj\n");

	/*
	 * Grind the loop until ^C
	 */
	while (1) {
		timey += (double)(1000000) / HZ;
		if (timey >= 1000000)
		    timey -= 1000000;
		hardclock();
		if (timex.tv_usec >= 1000000) {
			timex.tv_usec -= 1000000;
			timex.tv_sec++;
			second_overflow();
			poll_interval++;
			if (!(poll_interval % POLL)) {
				timez = (long)timey - timex.tv_usec;
				if (timez > 500000)
				    timez -= 1000000;
				if (timez < -500000)
				    timez += 1000000;
				hardupdate(timez);
				printf("%10li%10li%10.2f  %08lx  %08lx  %08lx\n",
				       timex.tv_sec, timez,
				       (double)time_freq / (1 << SHIFT_KF),
				       time_offset, time_freq, time_adj);
			}
		}
	}
}

/*
 * This routine simulates the ntp_adjtime() call
 *
 * For default SHIFT_UPDATE = 12, offset is limited to +-512 ms, the
 * maximum interval between updates is 4096 s and the maximum frequency
 * offset is +-31.25 ms/s.
 */
void
hardupdate(
	long offset
	)
{
	long ltemp, mtemp;

	time_offset = offset << SHIFT_UPDATE;
	mtemp = timex.tv_sec - time_reftime;
	time_reftime = timex.tv_sec;
	if (mtemp > MAXSEC)
	    mtemp = 0;

	/* ugly multiply should be replaced */
	if (offset < 0)
	    time_freq -= (-offset * mtemp) >>
		    (time_constant + time_constant);
	else
	    time_freq += (offset * mtemp) >>
		    (time_constant + time_constant);
	ltemp = time_tolerance << SHIFT_KF;
	if (time_freq > ltemp)
	    time_freq = ltemp;
	else if (time_freq < -ltemp)
	    time_freq = -ltemp;
	if (time_status == TIME_BAD)
	    time_status = TIME_OK;
}

/*
 * This routine simulates the timer interrupt
 */
void
hardclock(void)
{
	int ltemp, time_update;

	time_update = tick;	/* computed by adjtime() */
	time_phase += time_adj;
	if (time_phase < -FINEUSEC) {
		ltemp = -time_phase >> SHIFT_SCALE;
		time_phase += ltemp << SHIFT_SCALE;
		time_update -= ltemp;
	}
	else if (time_phase > FINEUSEC) {
		ltemp = time_phase >> SHIFT_SCALE;
		time_phase -= ltemp << SHIFT_SCALE;
		time_update += ltemp;
	}
	timex.tv_usec += time_update;
}

/*
 * This routine simulates the overflow of the microsecond field
 *
 * With SHIFT_SCALE = 23, the maximum frequency adjustment is +-256 us
 * per tick, or 25.6 ms/s at a clock frequency of 100 Hz. The time
 * contribution is shifted right a minimum of two bits, while the frequency
 * contribution is a right shift. Thus, overflow is prevented if the
 * frequency contribution is limited to half the maximum or 15.625 ms/s.
 */
void
second_overflow(void)
{
	int ltemp;

	time_maxerror += time_tolerance;
	if (time_offset < 0) {
		ltemp = -time_offset >>
			(SHIFT_KG + time_constant);
		time_offset += ltemp;
		time_adj = -(ltemp <<
			     (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE));
	} else {
		ltemp = time_offset >>
			(SHIFT_KG + time_constant);
		time_offset -= ltemp;
		time_adj = ltemp <<
			(SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
	}
	if (time_freq < 0)
	    time_adj -= -time_freq >> (SHIFT_KF + SHIFT_HZ - SHIFT_SCALE);
	else
	    time_adj += time_freq >> (SHIFT_KF + SHIFT_HZ - SHIFT_SCALE);
	time_adj += fixtick << (SHIFT_SCALE - SHIFT_HZ);

	/* ugly divide should be replaced */
	if (timex.tv_sec % 86400 == 0) {
		switch (time_status) {

		    case TIME_INS:
			timex.tv_sec--; /* !! */
			time_status = TIME_OOP;
			break;

		    case TIME_DEL:
			timex.tv_sec++;
			time_status = TIME_OK;
			break;

		    case TIME_OOP:
			time_status = TIME_OK;
			break;
		}
	}
}
