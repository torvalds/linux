/*
 * timespecops.h -- calculations on 'struct timespec' values
 *
 * Written by Juergen Perlinger (perlinger@ntp.org) for the NTP project.
 * The contents of 'html/copyright.html' apply.
 *
 * Rationale
 * ---------
 *
 * Doing basic arithmetic on a 'struct timespec' is not exceedingly
 * hard, but it requires tedious and repetitive code to keep the result
 * normalised. We consider a timespec normalised when the nanosecond
 * fraction is in the interval [0 .. 10^9[ ; there are multiple value
 * pairs of seconds and nanoseconds that denote the same time interval,
 * but the normalised representation is unique. No two different
 * intervals can have the same normalised representation.
 *
 * Another topic is the representation of negative time intervals.
 * There's more than one way to this, since both the seconds and the
 * nanoseconds of a timespec are signed values. IMHO, the easiest way is
 * to use a complement representation where the nanoseconds are still
 * normalised, no matter what the sign of the seconds value. This makes
 * normalisation easier, since the sign of the integer part is
 * irrelevant, and it removes several sign decision cases during the
 * calculations.
 *
 * As long as no signed integer overflow can occur with the nanosecond
 * part of the operands, all operations work as expected and produce a
 * normalised result.
 *
 * The exception to this are functions fix a '_fast' suffix, which do no
 * normalisation on input data and therefore expect the input data to be
 * normalised.
 *
 * Input and output operands may overlap; all input is consumed before
 * the output is written to.
 */
#ifndef TIMESPECOPS_H
#define TIMESPECOPS_H

#include <sys/types.h>
#include <stdio.h>
#include <math.h>

#include "ntp.h"
#include "timetoa.h"


/* nanoseconds per second */
#define NANOSECONDS 1000000000

/* predicate: returns TRUE if the nanoseconds are in nominal range */
#define timespec_isnormal(x) \
	((x)->tv_nsec >= 0 && (x)->tv_nsec < NANOSECONDS)

/* predicate: returns TRUE if the nanoseconds are out-of-bounds */
#define timespec_isdenormal(x)	(!timespec_isnormal(x))

/* conversion between l_fp fractions and nanoseconds */
#ifdef HAVE_U_INT64
# define FTOTVN(tsf)						\
	((int32)						\
	 (((u_int64)(tsf) * NANOSECONDS + 0x80000000) >> 32))
# define TVNTOF(tvu)						\
	((u_int32)						\
	 ((((u_int64)(tvu) << 32) + NANOSECONDS / 2) /		\
	  NANOSECONDS))
#else
# define NSECFRAC	(FRAC / NANOSECONDS)
# define FTOTVN(tsf)						\
	((int32)((tsf) / NSECFRAC + 0.5))
# define TVNTOF(tvu)						\
	((u_int32)((tvu) * NSECFRAC + 0.5))
#endif



/* make sure nanoseconds are in nominal range */
static inline struct timespec
normalize_tspec(
	struct timespec x
	)
{
#if SIZEOF_LONG > 4
	long	z;

	/* 
	 * tv_nsec is of type 'long', and on a 64-bit machine using only
	 * loops becomes prohibitive once the upper 32 bits get
	 * involved. On the other hand, division by constant should be
	 * fast enough; so we do a division of the nanoseconds in that
	 * case. The floor adjustment step follows with the standard
	 * normalisation loops. And labs() is intentionally not used
	 * here: it has implementation-defined behaviour when applied
	 * to LONG_MIN.
	 */
	if (x.tv_nsec < -3l * NANOSECONDS ||
	    x.tv_nsec > 3l * NANOSECONDS) {
		z = x.tv_nsec / NANOSECONDS;
		x.tv_nsec -= z * NANOSECONDS;
		x.tv_sec += z;
	}
#endif
	/* since 10**9 is close to 2**32, we don't divide but do a
	 * normalisation in a loop; this takes 3 steps max, and should
	 * outperform a division even if the mul-by-inverse trick is
	 * employed. */
	if (x.tv_nsec < 0)
		do {
			x.tv_nsec += NANOSECONDS;
			x.tv_sec--;
		} while (x.tv_nsec < 0);
	else if (x.tv_nsec >= NANOSECONDS)
		do {
			x.tv_nsec -= NANOSECONDS;
			x.tv_sec++;
		} while (x.tv_nsec >= NANOSECONDS);

	return x;
}

/* x = a + b */
static inline struct timespec
add_tspec(
	struct timespec	a,
	struct timespec	b
	)
{
	struct timespec	x;

	x = a;
	x.tv_sec += b.tv_sec;
	x.tv_nsec += b.tv_nsec;

	return normalize_tspec(x);
}

/* x = a + b, b is fraction only */
static inline struct timespec
add_tspec_ns(
	struct timespec	a,
	long		b
	)
{
	struct timespec x;

	x = a;
	x.tv_nsec += b;

	return normalize_tspec(x);
}

/* x = a - b */
static inline struct timespec
sub_tspec(
	struct timespec	a,
	struct timespec	b
	)
{	
	struct timespec x;

	x = a;
	x.tv_sec -= b.tv_sec;
	x.tv_nsec -= b.tv_nsec;

	return normalize_tspec(x);
}

/* x = a - b, b is fraction only */
static inline struct timespec
sub_tspec_ns(
	struct timespec	a,
	long		b
	)
{
	struct timespec	x;

	x = a;
	x.tv_nsec -= b;

	return normalize_tspec(x);
}

/* x = -a */
static inline struct timespec
neg_tspec(
	struct timespec	a
	)
{	
	struct timespec	x;

	x.tv_sec = -a.tv_sec;
	x.tv_nsec = -a.tv_nsec;

	return normalize_tspec(x);
}

/* x = abs(a) */
static inline struct timespec
abs_tspec(
	struct timespec	a
	)
{
	struct timespec	c;

	c = normalize_tspec(a);
	if (c.tv_sec < 0) {
		if (c.tv_nsec != 0) {
			c.tv_sec = -c.tv_sec - 1;
			c.tv_nsec = NANOSECONDS - c.tv_nsec;
		} else {
			c.tv_sec = -c.tv_sec;
		}
	}

	return c;
}

/*
 * compare previously-normalised a and b
 * return 1 / 0 / -1 if a < / == / > b
 */
static inline int
cmp_tspec(
	struct timespec a,
	struct timespec b
	)
{
	int r;

	r = (a.tv_sec > b.tv_sec) - (a.tv_sec < b.tv_sec);
	if (0 == r)
		r = (a.tv_nsec > b.tv_nsec) -
		    (a.tv_nsec < b.tv_nsec);
	
	return r;
}

/*
 * compare possibly-denormal a and b
 * return 1 / 0 / -1 if a < / == / > b
 */
static inline int
cmp_tspec_denorm(
	struct timespec	a,
	struct timespec	b
	)
{
	return cmp_tspec(normalize_tspec(a), normalize_tspec(b));
}

/*
 * test previously-normalised a
 * return 1 / 0 / -1 if a < / == / > 0
 */
static inline int
test_tspec(
	struct timespec	a
	)
{
	int		r;

	r = (a.tv_sec > 0) - (a.tv_sec < 0);
	if (r == 0)
		r = (a.tv_nsec > 0);
	
	return r;
}

/*
 * test possibly-denormal a
 * return 1 / 0 / -1 if a < / == / > 0
 */
static inline int
test_tspec_denorm(
	struct timespec	a
	)
{
	return test_tspec(normalize_tspec(a));
}

/* return LIB buffer ptr to string rep */
static inline const char *
tspectoa(
	struct timespec	x
	)
{
	return format_time_fraction(x.tv_sec, x.tv_nsec, 9);
}

/*
 *  convert to l_fp type, relative and absolute
 */

/* convert from timespec duration to l_fp duration */
static inline l_fp
tspec_intv_to_lfp(
	struct timespec	x
	)
{
	struct timespec	v;
	l_fp		y;
	
	v = normalize_tspec(x);
	y.l_uf = TVNTOF(v.tv_nsec);
	y.l_i = (int32)v.tv_sec;

	return y;
}

/* x must be UN*X epoch, output will be in NTP epoch */
static inline l_fp
tspec_stamp_to_lfp(
	struct timespec	x
	)
{
	l_fp		y;

	y = tspec_intv_to_lfp(x);
	y.l_ui += JAN_1970;

	return y;
}

/* convert from l_fp type, relative signed/unsigned and absolute */
static inline struct timespec
lfp_intv_to_tspec(
	l_fp		x
	)
{
	struct timespec out;
	l_fp		absx;
	int		neg;
	
	neg = L_ISNEG(&x);
	absx = x;
	if (neg) {
		L_NEG(&absx);	
	}
	out.tv_nsec = FTOTVN(absx.l_uf);
	out.tv_sec = absx.l_i;
	if (neg) {
		out.tv_sec = -out.tv_sec;
		out.tv_nsec = -out.tv_nsec;
		out = normalize_tspec(out);
	}

	return out;
}

static inline struct timespec
lfp_uintv_to_tspec(
	l_fp		x
	)
{
	struct timespec	out;
	
	out.tv_nsec = FTOTVN(x.l_uf);
	out.tv_sec = x.l_ui;

	return out;
}

/*
 * absolute (timestamp) conversion. Input is time in NTP epoch, output
 * is in UN*X epoch. The NTP time stamp will be expanded around the
 * pivot time *p or the current time, if p is NULL.
 */
static inline struct timespec
lfp_stamp_to_tspec(
	l_fp		x,
	const time_t *	p
	)
{
	struct timespec	out;
	vint64		sec;

	sec = ntpcal_ntp_to_time(x.l_ui, p);
	out.tv_nsec = FTOTVN(x.l_uf);

	/* copying a vint64 to a time_t needs some care... */
#if SIZEOF_TIME_T <= 4
	out.tv_sec = (time_t)sec.d_s.lo;
#elif defined(HAVE_INT64)
	out.tv_sec = (time_t)sec.q_s;
#else
	out.tv_sec = ((time_t)sec.d_s.hi << 32) | sec.d_s.lo;
#endif
	
	return out;
}

#endif	/* TIMESPECOPS_H */
