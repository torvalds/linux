/******************************************************************************
 *                                                                            *
 * Copyright (c) David L. Mills 1993, 1994                                    *
 *                                                                            *
 * Permission to use, copy, modify, and distribute this software and its      *
 * documentation for any purpose and without fee is hereby granted, provided  *
 * that the above copyright notice appears in all copies and that both the    *
 * copyright notice and this permission notice appear in supporting           *
 * documentation, and that the name University of Delaware not be used in     *
 * advertising or publicity pertaining to distribution of the software        *
 * without specific, written prior permission.  The University of Delaware    *
 * makes no representations about the suitability this software for any       *
 * purpose.  It is provided "as is" without express or implied warranty.      *
 *                                                                            *
 ******************************************************************************/

/*
 * Modification history timex.h
 *
 * 26 Sep 94	David L. Mills
 *	Added defines for hybrid phase/frequency-lock loop.
 *
 * 19 Mar 94	David L. Mills
 *	Moved defines from kernel routines to header file and added new
 *	defines for PPS phase-lock loop.
 *
 * 20 Feb 94	David L. Mills
 *	Revised status codes and structures for external clock and PPS
 *	signal discipline.
 *
 * 28 Nov 93	David L. Mills
 *	Adjusted parameters to improve stability and increase poll
 *	interval.
 *
 * 17 Sep 93    David L. Mills
 *      Created file
 */
/*
 * This header file defines the Network Time Protocol (NTP) interfaces
 * for user and daemon application programs. These are implemented using
 * private syscalls and data structures and require specific kernel
 * support.
 *
 * NAME
 *	ntp_gettime - NTP user application interface
 *
 * SYNOPSIS
 *	#include <sys/timex.h>
 *
 *	int syscall(SYS_ntp_gettime, tptr)
 *
 *	int SYS_ntp_gettime		defined in syscall.h header file
 *	struct ntptimeval *tptr		pointer to ntptimeval structure
 *
 * NAME
 *	ntp_adjtime - NTP daemon application interface
 *
 * SYNOPSIS
 *	#include <sys/timex.h>
 *
 *	int syscall(SYS_ntp_adjtime, mode, tptr)
 *
 *	int SYS_ntp_adjtime		defined in syscall.h header file
 *	struct timex *tptr		pointer to timex structure
 *
 */
#ifndef _SYS_TIMEX_H_
#define _SYS_TIMEX_H_ 1

#ifndef MSDOS			/* Microsoft specific */
#include <sys/syscall.h>
#endif /* MSDOS */

/*
 * The following defines establish the engineering parameters of the
 * phase-lock loop (PLL) model used in the kernel implementation. These
 * parameters have been carefully chosen by analysis for good stability
 * and wide dynamic range.
 *
 * The hz variable is defined in the kernel build environment. It
 * establishes the timer interrupt frequency, 100 Hz for the SunOS
 * kernel, 256 Hz for the Ultrix kernel and 1024 Hz for the OSF/1
 * kernel. SHIFT_HZ expresses the same value as the nearest power of two
 * in order to avoid hardware multiply operations.
 *
 * SHIFT_KG and SHIFT_KF establish the damping of the PLL and are chosen
 * for a slightly underdamped convergence characteristic. SHIFT_KH
 * establishes the damping of the FLL and is chosen by wisdom and black
 * art.
 *
 * MAXTC establishes the maximum time constant of the PLL. With the
 * SHIFT_KG and SHIFT_KF values given and a time constant range from
 * zero to MAXTC, the PLL will converge in 15 minutes to 16 hours,
 * respectively.
 */
#define SHIFT_HZ 7		/* log2(hz) */
#define SHIFT_KG 6		/* phase factor (shift) */
#define SHIFT_KF 16		/* PLL frequency factor (shift) */
#define SHIFT_KH 2		/* FLL frequency factor (shift) */
#define MAXTC 6			/* maximum time constant (shift) */

/*
 * The following defines establish the scaling of the various variables
 * used by the PLL. They are chosen to allow the greatest precision
 * possible without overflow of a 32-bit word.
 *
 * SHIFT_SCALE defines the scaling (shift) of the time_phase variable,
 * which serves as a an extension to the low-order bits of the system
 * clock variable time.tv_usec.
 *
 * SHIFT_UPDATE defines the scaling (shift) of the time_offset variable,
 * which represents the current time offset with respect to standard
 * time.
 *
 * SHIFT_USEC defines the scaling (shift) of the time_freq and
 * time_tolerance variables, which represent the current frequency
 * offset and maximum frequency tolerance.
 *
 * FINEUSEC is 1 us in SHIFT_UPDATE units of the time_phase variable.
 */
#define SHIFT_SCALE 22		/* phase scale (shift) */
#define SHIFT_UPDATE (SHIFT_KG + MAXTC) /* time offset scale (shift) */
#define SHIFT_USEC 16		/* frequency offset scale (shift) */
#define FINEUSEC (1L << SHIFT_SCALE) /* 1 us in phase units */

/*
 * The following defines establish the performance envelope of the PLL.
 * They insure it operates within predefined limits, in order to satisfy
 * correctness assertions. An excursion which exceeds these bounds is
 * clamped to the bound and operation proceeds accordingly. In practice,
 * this can occur only if something has failed or is operating out of
 * tolerance, but otherwise the PLL continues to operate in a stable
 * mode.
 *
 * MAXPHASE must be set greater than or equal to CLOCK.MAX (128 ms), as
 * defined in the NTP specification. CLOCK.MAX establishes the maximum
 * time offset allowed before the system time is reset, rather than
 * incrementally adjusted. Here, the maximum offset is clamped to
 * MAXPHASE only in order to prevent overflow errors due to defective
 * protocol implementations.
 *
 * MAXFREQ is the maximum frequency tolerance of the CPU clock
 * oscillator plus the maximum slew rate allowed by the protocol. It
 * should be set to at least the frequency tolerance of the oscillator
 * plus 100 ppm for vernier frequency adjustments. If the kernel
 * PPS discipline code is configured (PPS_SYNC), the oscillator time and
 * frequency are disciplined to an external source, presumably with
 * negligible time and frequency error relative to UTC, and MAXFREQ can
 * be reduced.
 *
 * MAXTIME is the maximum jitter tolerance of the PPS signal if the
 * kernel PPS discipline code is configured (PPS_SYNC).
 *
 * MINSEC and MAXSEC define the lower and upper bounds on the interval
 * between protocol updates.
 */
#define MAXPHASE 512000L	/* max phase error (us) */
#ifdef PPS_SYNC
#define MAXFREQ (512L << SHIFT_USEC) /* max freq error (100 ppm) */
#define MAXTIME (200L << PPS_AVG) /* max PPS error (jitter) (200 us) */
#else
#define MAXFREQ (512L << SHIFT_USEC) /* max freq error (200 ppm) */
#endif /* PPS_SYNC */
#define MINSEC 16L		/* min interval between updates (s) */
#define MAXSEC 1200L		/* max interval between updates (s) */

#ifdef PPS_SYNC
/*
 * The following defines are used only if a pulse-per-second (PPS)
 * signal is available and connected via a modem control lead, such as
 * produced by the optional ppsclock feature incorporated in the Sun
 * asynch driver. They establish the design parameters of the frequency-
 * lock loop used to discipline the CPU clock oscillator to the PPS
 * signal.
 *
 * PPS_AVG is the averaging factor for the frequency loop, as well as
 * the time and frequency dispersion.
 *
 * PPS_SHIFT and PPS_SHIFTMAX specify the minimum and maximum
 * calibration intervals, respectively, in seconds as a power of two.
 *
 * PPS_VALID is the maximum interval before the PPS signal is considered
 * invalid and protocol updates used directly instead.
 *
 * MAXGLITCH is the maximum interval before a time offset of more than
 * MAXTIME is believed.
 */
#define PPS_AVG 2		/* pps averaging constant (shift) */
#define PPS_SHIFT 2		/* min interval duration (s) (shift) */
#define PPS_SHIFTMAX 8		/* max interval duration (s) (shift) */
#define PPS_VALID 120		/* pps signal watchdog max (s) */
#define MAXGLITCH 30		/* pps signal glitch max (s) */
#endif /* PPS_SYNC */

/*
 * The following defines and structures define the user interface for
 * the ntp_gettime() and ntp_adjtime() system calls.
 *
 * Control mode codes (timex.modes)
 */
#define MOD_OFFSET	0x0001	/* set time offset */
#define MOD_FREQUENCY	0x0002	/* set frequency offset */
#define MOD_MAXERROR	0x0004	/* set maximum time error */
#define MOD_ESTERROR	0x0008	/* set estimated time error */
#define MOD_STATUS	0x0010	/* set clock status bits */
#define MOD_TIMECONST	0x0020	/* set pll time constant */
#define MOD_CANSCALE	0x0040	/* kernel can scale offset field */
#define MOD_DOSCALE	0x0080	/* userland wants to scale offset field */

/*
 * Status codes (timex.status)
 */
#define STA_PLL		0x0001	/* enable PLL updates (rw) */
#define STA_PPSFREQ	0x0002	/* enable PPS freq discipline (rw) */
#define STA_PPSTIME	0x0004	/* enable PPS time discipline (rw) */
#define STA_FLL		0x0008	/* select frequency-lock mode (rw) */

#define STA_INS		0x0010	/* insert leap (rw) */
#define STA_DEL		0x0020	/* delete leap (rw) */
#define STA_UNSYNC	0x0040	/* clock unsynchronized (rw) */
#define STA_FREQHOLD	0x0080	/* hold frequency (rw) */

#define STA_PPSSIGNAL	0x0100	/* PPS signal present (ro) */
#define STA_PPSJITTER	0x0200	/* PPS signal jitter exceeded (ro) */
#define STA_PPSWANDER	0x0400	/* PPS signal wander exceeded (ro) */
#define STA_PPSERROR	0x0800	/* PPS signal calibration error (ro) */

#define STA_CLOCKERR	0x1000	/* clock hardware fault (ro) */

#define STA_RONLY (STA_PPSSIGNAL | STA_PPSJITTER | STA_PPSWANDER | \
    STA_PPSERROR | STA_CLOCKERR) /* read-only bits */

/*
 * Clock states (time_state)
 */
#define TIME_OK		0	/* no leap second warning */
#define TIME_INS	1	/* insert leap second warning */
#define TIME_DEL	2	/* delete leap second warning */
#define TIME_OOP	3	/* leap second in progress */
#define TIME_WAIT	4	/* leap second has occurred */
#define TIME_ERROR	5	/* clock not synchronized */

/*
 * NTP user interface (ntp_gettime()) - used to read kernel clock values
 *
 * Note: maximum error = NTP synch distance = dispersion + delay / 2;
 * estimated error = NTP dispersion.
 */
struct ntptimeval {
	struct timeval time;	/* current time (ro) */
	long maxerror;		/* maximum error (us) (ro) */
	long esterror;		/* estimated error (us) (ro) */
};

/*
 * NTP daemon interface - (ntp_adjtime()) used to discipline CPU clock
 * oscillator
 */
struct timex {
	unsigned int modes;	/* clock mode bits (wo) */
	long offset;		/* time offset (us) (rw) */
	long freq;		/* frequency offset (scaled ppm) (rw) */
	long maxerror;		/* maximum error (us) (rw) */
	long esterror;		/* estimated error (us) (rw) */
	int status;		/* clock status bits (rw) */
	long constant;		/* pll time constant (rw) */
	long precision;		/* clock precision (us) (ro) */
	long tolerance;		/* clock frequency tolerance (scaled
				 * ppm) (ro) */
	/*
	 * The following read-only structure members are implemented
	 * only if the PPS signal discipline is configured in the
	 * kernel.
	 */
	long ppsfreq;		/* pps frequency (scaled ppm) (ro) */
	long jitter;		/* pps jitter (us) (ro) */
	int shift;		/* interval duration (s) (shift) (ro) */
	long stabil;		/* pps stability (scaled ppm) (ro) */
	long jitcnt;		/* jitter limit exceeded (ro) */
	long calcnt;		/* calibration intervals (ro) */
	long errcnt;		/* calibration errors (ro) */
	long stbcnt;		/* stability limit exceeded (ro) */

};
#ifdef __FreeBSD__

/*
 * sysctl identifiers underneath kern.ntp_pll
 */
#define NTP_PLL_GETTIME	1	/* used by ntp_gettime() */
#define NTP_PLL_MAXID	2	/* number of valid ids */

#define NTP_PLL_NAMES { \
			  { 0, 0 }, \
			  { "gettime", CTLTYPE_STRUCT }, \
		      }

#ifndef KERNEL
#include <sys/cdefs.h>

__BEGIN_DECLS
extern int ntp_gettime        __P((struct ntptimeval *));
extern int ntp_adjtime        __P((struct timex *));
__END_DECLS

#endif /* not KERNEL */

#endif /* __FreeBSD__ */
#endif /* _SYS_TIMEX_H_ */
