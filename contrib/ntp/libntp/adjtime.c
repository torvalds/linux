#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef MPE 
/*
 * MPE lacks adjtime(), so we define our own.  But note that time slewing has
 * a sub-second accuracy bug documented in SR 5003462838 which prevents ntpd
 * from being able to maintain clock synch.  Because of the bug, this adjtime()
 * implementation as used by ntpd has a side-effect of screwing up the hardware
 * PDC clock, which will need to be reset with a reboot.
 *
 * This problem affects all versions of MPE at the time of this writing (when
 * MPE/iX 7.0 is the most current).  It only causes bad things to happen when
 * doing continuous clock synchronization with ntpd; note that you CAN run ntpd
 * with "disable ntp" in ntp.conf if you wish to provide a time server.
 *
 * The one-time clock adjustment functionality of ntpdate and ntp_timeset can
 * be used without screwing up the PDC clock.
 * 
 */
#include <time.h>

int adjtime(struct timeval *delta, struct timeval *olddelta);

int adjtime(struct timeval *delta, struct timeval *olddelta)

{
/* Documented, supported MPE system intrinsics. */

extern void GETPRIVMODE(void);
extern void GETUSERMODE(void);

/* Undocumented, unsupported MPE internal functions. */

extern long long current_correction_usecs(void);
extern long long get_time(void);
extern void get_time_change_info(long long *, char *, char *);
extern long long pdc_time(int *);
extern void set_time_correction(long long, int, int);
extern long long ticks_to_micro(long long);

long long big_sec, big_usec, new_correction = 0LL;
long long prev_correction;

if (delta != NULL) {
  /* Adjustment required.  Convert delta to 64-bit microseconds. */
  big_sec = (long)delta->tv_sec;
  big_usec = delta->tv_usec;
  new_correction = (big_sec * 1000000LL) + big_usec;
}

GETPRIVMODE();

/* Determine how much of a previous correction (if any) we're interrupting. */
prev_correction = current_correction_usecs();

if (delta != NULL) {
  /* Adjustment required. */

#if 0
  /* Speculative code disabled until bug SR 5003462838 is fixed.  This bug
     prevents accurate time slewing, and indeed renders ntpd inoperable. */

  if (prev_correction != 0LL) {
    /* A previous adjustment did not complete.  Since the PDC UTC clock was
    immediately jumped at the start of the previous adjustment, we must
    explicitly reset it to the value of the MPE local time clock minus the
    time zone offset. */

    char pwf_since_boot, recover_pwf_time;
    long long offset_ticks, offset_usecs, pdc_usecs_current, pdc_usecs_wanted;
    int hpe_status;

    get_time_change_info(&offset_ticks, &pwf_since_boot, &recover_pwf_time);
    offset_usecs = ticks_to_micro(offset_ticks);
    pdc_usecs_wanted = get_time() - offset_usecs;
    pdc_usecs_current = pdc_time(&hpe_status);
    if (hpe_status == 0) 
      /* Force new PDC time by starting an extra correction. */
      set_time_correction(pdc_usecs_wanted - pdc_usecs_current,0,1);
  }
#endif /* 0 */
    
  /* Immediately jump the PDC time to the new value, and then initiate a 
     gradual MPE time correction slew. */
  set_time_correction(new_correction,0,1);
}

GETUSERMODE();

if (olddelta != NULL) {
  /* Caller wants to know remaining amount of previous correction. */
  (long)olddelta->tv_sec = prev_correction / 1000000LL;
  olddelta->tv_usec = prev_correction % 1000000LL;
}

return 0;
}
#endif /* MPE */

#ifdef NEED_HPUX_ADJTIME
/*************************************************************************/
/* (c) Copyright Tai Jin, 1988.  All Rights Reserved.                    */
/*     Hewlett-Packard Laboratories.                                     */
/*                                                                       */
/* Permission is hereby granted for unlimited modification, use, and     */
/* distribution.  This software is made available with no warranty of    */
/* any kind, express or implied.  This copyright notice must remain      */
/* intact in all versions of this software.                              */
/*                                                                       */
/* The author would appreciate it if any bug fixes and enhancements were */
/* to be sent back to him for incorporation into future versions of this */
/* software.  Please send changes to tai@iag.hp.com or ken@sdd.hp.com.   */
/*************************************************************************/

/*
 * Revision history
 *
 * 9 Jul 94	David L. Mills, Unibergity of Delabunch
 *		Implemented variable threshold to limit age of
 *		corrections; reformatted code for readability.
 */

#ifndef lint
static char RCSid[] = "adjtime.c,v 3.1 1993/07/06 01:04:42 jbj Exp";
#endif

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include <signal.h>
#include "adjtime.h"

#define abs(x)  ((x) < 0 ? -(x) : (x))

/*
 * The following paramters are appropriate for an NTP adjustment
 * interval of one second.
 */
#define ADJ_THRESH 200		/* initial threshold */
#define ADJ_DELTA 4		/* threshold decrement */

static long adjthresh;		/* adjustment threshold */
static long saveup;		/* corrections accumulator */

/*
 * clear_adjtime - reset accumulator and threshold variables
 */
void
_clear_adjtime(void)
{
	saveup = 0;
	adjthresh = ADJ_THRESH;
}

/*
 * adjtime - hp-ux copout of the standard Unix adjtime() system call
 */
int
adjtime(
	register struct timeval *delta,
	register struct timeval *olddelta
	)
{
	struct timeval newdelta;

	/*
	 * Corrections greater than one second are done immediately.
	 */
	if (delta->tv_sec) {
		adjthresh = ADJ_THRESH;
		saveup = 0;
		return(_adjtime(delta, olddelta));
	}

	/*
	 * Corrections less than one second are accumulated until
	 * tripping a threshold, which is initially set at ADJ_THESH and
	 * reduced in ADJ_DELTA steps to zero. The idea here is to
	 * introduce large corrections quickly, while making sure that
	 * small corrections are introduced without excessive delay. The
	 * idea comes from the ARPAnet routing update algorithm.
	 */
	saveup += delta->tv_usec;
	if (abs(saveup) >= adjthresh) {
		adjthresh = ADJ_THRESH;
		newdelta.tv_sec = 0;
		newdelta.tv_usec = saveup;
		saveup = 0;
		return(_adjtime(&newdelta, olddelta));
	} else {
		adjthresh -= ADJ_DELTA;
	}

	/*
	 * While nobody uses it, return the residual before correction,
	 * as per Unix convention.
	 */
	if (olddelta)
	    olddelta->tv_sec = olddelta->tv_usec = 0;
	return(0);
}

/*
 * _adjtime - does the actual work
 */
int
_adjtime(
	register struct timeval *delta,
	register struct timeval *olddelta
	)
{
	register int mqid;
	MsgBuf msg;
	register MsgBuf *msgp = &msg;

	/*
	 * Get the key to the adjtime message queue (note that we must
	 * get it every time because the queue might have been removed
	 * and recreated)
	 */
	if ((mqid = msgget(KEY, 0)) == -1)
	    return (-1);
	msgp->msgb.mtype = CLIENT;
	msgp->msgb.tv = *delta;
	if (olddelta)
	    msgp->msgb.code = DELTA2;
	else
	    msgp->msgb.code = DELTA1;

	/*
	 * Tickle adjtimed and snatch residual, if indicated. Lots of
	 * fanatic error checking here.
	 */
	if (msgsnd(mqid, &msgp->msgp, MSGSIZE, 0) == -1)
	    return (-1);
	if (olddelta) {
		if (msgrcv(mqid, &msgp->msgp, MSGSIZE, SERVER, 0) == -1)
		    return (-1);
		*olddelta = msgp->msgb.tv;
	}
	return (0);
}

#else
# if NEED_QNX_ADJTIME
/*
 * Emulate adjtime() using QNX ClockAdjust().
 * Chris Burghart <burghart@atd.ucar.edu>, 11/2001
 * Miroslaw Pabich <miroslaw_pabich@o2.pl>, 09/2005
 *
 * This is an implementation of adjtime() for QNX.  
 * ClockAdjust() is used to tweak the system clock for about
 * 1 second period until the desired delta is achieved.
 * Time correction slew is limited to reasonable value.
 * Internal rounding and relative errors are reduced.
 */
# include <sys/neutrino.h>
# include <sys/time.h>

# include <ntp_stdlib.h>

/*
 * Time correction slew limit. QNX is a hard real-time system,
 * so don't adjust system clock too fast.
 */
#define CORR_SLEW_LIMIT     0.02  /* [s/s] */

/*
 * Period of system clock adjustment. It should be equal to adjtime
 * execution period (1s). If slightly less than 1s (0.95-0.99), then olddelta
 * residual error (introduced by execution period jitter) will be reduced.
 */
#define ADJUST_PERIOD       0.97  /* [s] */

int 
adjtime (struct timeval *delta, struct timeval *olddelta)
{
    double delta_nsec;
    double delta_nsec_old;
    struct _clockadjust adj;
    struct _clockadjust oldadj;

    /*
     * How many nanoseconds are we adjusting?
     */
    if (delta != NULL)
	delta_nsec = 1e9 * (long)delta->tv_sec + 1e3 * delta->tv_usec;
    else
	delta_nsec = 0;

    /*
     * Build the adjust structure and call ClockAdjust()
     */
    if (delta_nsec != 0)
    {
	struct _clockperiod period;
	long count;
	long increment;
	long increment_limit;
	int isneg = 0;

	/*
	 * Convert to absolute value for future processing
	 */
	if (delta_nsec < 0)
	{
	    isneg = 1;
	    delta_nsec = -delta_nsec;
	}

	/*
	 * Get the current clock period (nanoseconds)
	 */
	if (ClockPeriod (CLOCK_REALTIME, 0, &period, 0) == -1)
	    return -1;

	/*
	 * Compute count and nanoseconds increment
	 */
	count = 1e9 * ADJUST_PERIOD / period.nsec;
	increment = delta_nsec / count + .5;
	/* Reduce relative error */
	if (count > increment + 1)
	{
	    increment = 1 + (long)((delta_nsec - 1) / count);
	    count = delta_nsec / increment + .5;
	}

	/*
	 * Limit the adjust increment to appropriate value
	 */
	increment_limit = CORR_SLEW_LIMIT * period.nsec;
	if (increment > increment_limit)
	{
	    increment = increment_limit;
	    count = delta_nsec / increment + .5;
	    /* Reduce relative error */
	    if (increment > count + 1)
	    {
		count =  1 + (long)((delta_nsec - 1) / increment);
		increment = delta_nsec / count + .5;
	    }
	}

	adj.tick_nsec_inc = isneg ? -increment : increment;
	adj.tick_count = count;
    }
    else
    {
	adj.tick_nsec_inc = 0;
	adj.tick_count = 0;
    }

    if (ClockAdjust (CLOCK_REALTIME, &adj, &oldadj) == -1)
	return -1;

    /*
     * Build olddelta
     */
    delta_nsec_old = (double)oldadj.tick_count * oldadj.tick_nsec_inc;
    if (olddelta != NULL)
    {
	if (delta_nsec_old != 0)
	{
	    /* Reduce rounding error */
	    delta_nsec_old += (delta_nsec_old < 0) ? -500 : 500;
	    olddelta->tv_sec = delta_nsec_old / 1e9;
	    olddelta->tv_usec = (long)(delta_nsec_old - 1e9
				 * (long)olddelta->tv_sec) / 1000;
	}
	else
	{
	    olddelta->tv_sec = 0;
	    olddelta->tv_usec = 0;
	}
    }

    return 0;
}
# else /* no special adjtime() needed */
int adjtime_bs;
# endif
#endif
