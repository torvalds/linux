/*
 * ntp_calendar.c - calendar and helper functions
 *
 * Written by Juergen Perlinger (perlinger@ntp.org) for the NTP project.
 * The contents of 'html/copyright.html' apply.
 *
 * --------------------------------------------------------------------
 * Some notes on the implementation:
 *
 * Calendar algorithms thrive on the division operation, which is one of
 * the slowest numerical operations in any CPU. What saves us here from
 * abysmal performance is the fact that all divisions are divisions by
 * constant numbers, and most compilers can do this by a multiplication
 * operation.  But this might not work when using the div/ldiv/lldiv
 * function family, because many compilers are not able to do inline
 * expansion of the code with following optimisation for the
 * constant-divider case.
 *
 * Also div/ldiv/lldiv are defined in terms of int/long/longlong, which
 * are inherently target dependent. Nothing that could not be cured with
 * autoconf, but still a mess...
 *
 * Furthermore, we need floor division in many places. C either leaves
 * the division behaviour undefined (< C99) or demands truncation to
 * zero (>= C99), so additional steps are required to make sure the
 * algorithms work. The {l,ll}div function family is requested to
 * truncate towards zero, which is also the wrong direction for our
 * purpose.
 *
 * For all this, all divisions by constant are coded manually, even when
 * there is a joined div/mod operation: The optimiser should sort that
 * out, if possible. Most of the calculations are done with unsigned
 * types, explicitely using two's complement arithmetics where
 * necessary. This minimises the dependecies to compiler and target,
 * while still giving reasonable to good performance.
 *
 * The implementation uses a few tricks that exploit properties of the
 * two's complement: Floor division on negative dividents can be
 * executed by using the one's complement of the divident. One's
 * complement can be easily created using XOR and a mask.
 *
 * Finally, check for overflow conditions is minimal. There are only two
 * calculation steps in the whole calendar that suffer from an internal
 * overflow, and these conditions are checked: errno is set to EDOM and
 * the results are clamped/saturated in this case.  All other functions
 * do not suffer from internal overflow and simply return the result
 * truncated to 32 bits.
 *
 * This is a sacrifice made for execution speed.  Since a 32-bit day
 * counter covers +/- 5,879,610 years and the clamp limits the effective
 * range to +/-2.9 million years, this should not pose a problem here.
 *
 */

#include <config.h>
#include <sys/types.h>

#include "ntp_types.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"
#include "ntp_fp.h"
#include "ntp_unixtime.h"

/* For now, let's take the conservative approach: if the target property
 * macros are not defined, check a few well-known compiler/architecture
 * settings. Default is to assume that the representation of signed
 * integers is unknown and shift-arithmetic-right is not available.
 */
#ifndef TARGET_HAS_2CPL
# if defined(__GNUC__)
#  if defined(__i386__) || defined(__x86_64__) || defined(__arm__)
#   define TARGET_HAS_2CPL 1
#  else
#   define TARGET_HAS_2CPL 0
#  endif
# elif defined(_MSC_VER)
#  if defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM)
#   define TARGET_HAS_2CPL 1
#  else
#   define TARGET_HAS_2CPL 0
#  endif
# else
#  define TARGET_HAS_2CPL 0
# endif
#endif

#ifndef TARGET_HAS_SAR
# define TARGET_HAS_SAR 0
#endif

/*
 *---------------------------------------------------------------------
 * replacing the 'time()' function
 *---------------------------------------------------------------------
 */

static systime_func_ptr systime_func = &time;
static inline time_t now(void);


systime_func_ptr
ntpcal_set_timefunc(
	systime_func_ptr nfunc
	)
{
	systime_func_ptr res;

	res = systime_func;
	if (NULL == nfunc)
		nfunc = &time;
	systime_func = nfunc;

	return res;
}


static inline time_t
now(void)
{
	return (*systime_func)(NULL);
}

/*
 *---------------------------------------------------------------------
 * Get sign extension mask and unsigned 2cpl rep for a signed integer
 *---------------------------------------------------------------------
 */

static inline uint32_t
int32_sflag(
	const int32_t v)
{
#   if TARGET_HAS_2CPL && TARGET_HAS_SAR && SIZEOF_INT >= 4

	/* Let's assume that shift is the fastest way to get the sign
	 * extension of of a signed integer. This might not always be
	 * true, though -- On 8bit CPUs or machines without barrel
	 * shifter this will kill the performance. So we make sure
	 * we do this only if 'int' has at least 4 bytes.
	 */
	return (uint32_t)(v >> 31);
	
#   else

	/* This should be a rather generic approach for getting a sign
	 * extension mask...
	 */
	return UINT32_C(0) - (uint32_t)(v < 0);
	
#   endif
}

static inline uint32_t
int32_to_uint32_2cpl(
	const int32_t v)
{
	uint32_t vu;
	
#   if TARGET_HAS_2CPL

	/* Just copy through the 32 bits from the signed value if we're
	 * on a two's complement target.
	 */
	vu = (uint32_t)v;
	
#   else

	/* Convert from signed int to unsigned int two's complement. Do
	 * not make any assumptions about the representation of signed
	 * integers, but make sure signed integer overflow cannot happen
	 * here. A compiler on a two's complement target *might* find
	 * out that this is just a complicated cast (as above), but your
	 * mileage might vary.
	 */
	if (v < 0)
		vu = ~(uint32_t)(-(v + 1));
	else
		vu = (uint32_t)v;
	
#   endif
	
	return vu;
}

static inline int32_t
uint32_2cpl_to_int32(
	const uint32_t vu)
{
	int32_t v;
	
#   if TARGET_HAS_2CPL

	/* Just copy through the 32 bits from the unsigned value if
	 * we're on a two's complement target.
	 */
	v = (int32_t)vu;

#   else

	/* Convert to signed integer, making sure signed integer
	 * overflow cannot happen. Again, the optimiser might or might
	 * not find out that this is just a copy of 32 bits on a target
	 * with two's complement representation for signed integers.
	 */
	if (vu > INT32_MAX)
		v = -(int32_t)(~vu) - 1;
	else
		v = (int32_t)vu;
	
#   endif
	
	return v;
}

/* Some of the calculations need to multiply the input by 4 before doing
 * a division. This can cause overflow and strange results. Therefore we
 * clamp / saturate the input operand. And since we do the calculations
 * in unsigned int with an extra sign flag/mask, we only loose one bit
 * of the input value range.
 */
static inline uint32_t
uint32_saturate(
	uint32_t vu,
	uint32_t mu)
{
	static const uint32_t limit = UINT32_MAX/4u;
	if ((mu ^ vu) > limit) {
		vu    = mu ^ limit;
		errno = EDOM;
	}
	return vu;
}

/*
 *---------------------------------------------------------------------
 * Convert between 'time_t' and 'vint64'
 *---------------------------------------------------------------------
 */
vint64
time_to_vint64(
	const time_t * ptt
	)
{
	vint64 res;
	time_t tt;

	tt = *ptt;

#   if SIZEOF_TIME_T <= 4

	res.D_s.hi = 0;
	if (tt < 0) {
		res.D_s.lo = (uint32_t)-tt;
		M_NEG(res.D_s.hi, res.D_s.lo);
	} else {
		res.D_s.lo = (uint32_t)tt;
	}

#   elif defined(HAVE_INT64)

	res.q_s = tt;

#   else
	/*
	 * shifting negative signed quantities is compiler-dependent, so
	 * we better avoid it and do it all manually. And shifting more
	 * than the width of a quantity is undefined. Also a don't do!
	 */
	if (tt < 0) {
		tt = -tt;
		res.D_s.lo = (uint32_t)tt;
		res.D_s.hi = (uint32_t)(tt >> 32);
		M_NEG(res.D_s.hi, res.D_s.lo);
	} else {
		res.D_s.lo = (uint32_t)tt;
		res.D_s.hi = (uint32_t)(tt >> 32);
	}

#   endif

	return res;
}


time_t
vint64_to_time(
	const vint64 *tv
	)
{
	time_t res;

#   if SIZEOF_TIME_T <= 4

	res = (time_t)tv->D_s.lo;

#   elif defined(HAVE_INT64)

	res = (time_t)tv->q_s;

#   else

	res = ((time_t)tv->d_s.hi << 32) | tv->D_s.lo;

#   endif

	return res;
}

/*
 *---------------------------------------------------------------------
 * Get the build date & time
 *---------------------------------------------------------------------
 */
int
ntpcal_get_build_date(
	struct calendar * jd
	)
{
	/* The C standard tells us the format of '__DATE__':
	 *
	 * __DATE__ The date of translation of the preprocessing
	 * translation unit: a character string literal of the form "Mmm
	 * dd yyyy", where the names of the months are the same as those
	 * generated by the asctime function, and the first character of
	 * dd is a space character if the value is less than 10. If the
	 * date of translation is not available, an
	 * implementation-defined valid date shall be supplied.
	 *
	 * __TIME__ The time of translation of the preprocessing
	 * translation unit: a character string literal of the form
	 * "hh:mm:ss" as in the time generated by the asctime
	 * function. If the time of translation is not available, an
	 * implementation-defined valid time shall be supplied.
	 *
	 * Note that MSVC declares DATE and TIME to be in the local time
	 * zone, while neither the C standard nor the GCC docs make any
	 * statement about this. As a result, we may be +/-12hrs off
	 * UTC.  But for practical purposes, this should not be a
	 * problem.
	 *
	 */
#   ifdef MKREPRO_DATE
	static const char build[] = MKREPRO_TIME "/" MKREPRO_DATE;
#   else
	static const char build[] = __TIME__ "/" __DATE__;
#   endif
	static const char mlist[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

	char		  monstr[4];
	const char *	  cp;
	unsigned short	  hour, minute, second, day, year;
 	/* Note: The above quantities are used for sscanf 'hu' format,
	 * so using 'uint16_t' is contra-indicated!
	 */

#   ifdef DEBUG
	static int        ignore  = 0;
#   endif

	ZERO(*jd);
	jd->year     = 1970;
	jd->month    = 1;
	jd->monthday = 1;

#   ifdef DEBUG
	/* check environment if build date should be ignored */
	if (0 == ignore) {
	    const char * envstr;
	    envstr = getenv("NTPD_IGNORE_BUILD_DATE");
	    ignore = 1 + (envstr && (!*envstr || !strcasecmp(envstr, "yes")));
	}
	if (ignore > 1)
	    return FALSE;
#   endif

	if (6 == sscanf(build, "%hu:%hu:%hu/%3s %hu %hu",
			&hour, &minute, &second, monstr, &day, &year)) {
		cp = strstr(mlist, monstr);
		if (NULL != cp) {
			jd->year     = year;
			jd->month    = (uint8_t)((cp - mlist) / 3 + 1);
			jd->monthday = (uint8_t)day;
			jd->hour     = (uint8_t)hour;
			jd->minute   = (uint8_t)minute;
			jd->second   = (uint8_t)second;

			return TRUE;
		}
	}

	return FALSE;
}


/*
 *---------------------------------------------------------------------
 * basic calendar stuff
 *---------------------------------------------------------------------
 */

/* month table for a year starting with March,1st */
static const uint16_t shift_month_table[13] = {
	0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306, 337, 366
};

/* month tables for years starting with January,1st; regular & leap */
static const uint16_t real_month_table[2][13] = {
	/* -*- table for regular years -*- */
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	/* -*- table for leap years -*- */
	{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

/*
 * Some notes on the terminology:
 *
 * We use the proleptic Gregorian calendar, which is the Gregorian
 * calendar extended in both directions ad infinitum. This totally
 * disregards the fact that this calendar was invented in 1582, and
 * was adopted at various dates over the world; sometimes even after
 * the start of the NTP epoch.
 *
 * Normally date parts are given as current cycles, while time parts
 * are given as elapsed cycles:
 *
 * 1970-01-01/03:04:05 means 'IN the 1970st. year, IN the first month,
 * ON the first day, with 3hrs, 4minutes and 5 seconds elapsed.
 *
 * The basic calculations for this calendar implementation deal with
 * ELAPSED date units, which is the number of full years, full months
 * and full days before a date: 1970-01-01 would be (1969, 0, 0) in
 * that notation.
 *
 * To ease the numeric computations, month and day values outside the
 * normal range are acceptable: 2001-03-00 will be treated as the day
 * before 2001-03-01, 2000-13-32 will give the same result as
 * 2001-02-01 and so on.
 *
 * 'rd' or 'RD' is used as an abbreviation for the latin 'rata die'
 * (day number).  This is the number of days elapsed since 0000-12-31
 * in the proleptic Gregorian calendar. The begin of the Christian Era
 * (0001-01-01) is RD(1).
 */

/*
 * ====================================================================
 *
 * General algorithmic stuff
 *
 * ====================================================================
 */

/*
 *---------------------------------------------------------------------
 * Do a periodic extension of 'value' around 'pivot' with a period of
 * 'cycle'.
 *
 * The result 'res' is a number that holds to the following properties:
 *
 *   1)	 res MOD cycle == value MOD cycle
 *   2)	 pivot <= res < pivot + cycle
 *	 (replace </<= with >/>= for negative cycles)
 *
 * where 'MOD' denotes the modulo operator for FLOOR DIVISION, which
 * is not the same as the '%' operator in C: C requires division to be
 * a truncated division, where remainder and dividend have the same
 * sign if the remainder is not zero, whereas floor division requires
 * divider and modulus to have the same sign for a non-zero modulus.
 *
 * This function has some useful applications:
 *
 * + let Y be a calendar year and V a truncated 2-digit year: then
 *	periodic_extend(Y-50, V, 100)
 *   is the closest expansion of the truncated year with respect to
 *   the full year, that is a 4-digit year with a difference of less
 *   than 50 years to the year Y. ("century unfolding")
 *
 * + let T be a UN*X time stamp and V be seconds-of-day: then
 *	perodic_extend(T-43200, V, 86400)
 *   is a time stamp that has the same seconds-of-day as the input
 *   value, with an absolute difference to T of <= 12hrs.  ("day
 *   unfolding")
 *
 * + Wherever you have a truncated periodic value and a non-truncated
 *   base value and you want to match them somehow...
 *
 * Basically, the function delivers 'pivot + (value - pivot) % cycle',
 * but the implementation takes some pains to avoid internal signed
 * integer overflows in the '(value - pivot) % cycle' part and adheres
 * to the floor division convention.
 *
 * If 64bit scalars where available on all intended platforms, writing a
 * version that uses 64 bit ops would be easy; writing a general
 * division routine for 64bit ops on a platform that can only do
 * 32/16bit divisions and is still performant is a bit more
 * difficult. Since most usecases can be coded in a way that does only
 * require the 32-bit version a 64bit version is NOT provided here.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_periodic_extend(
	int32_t pivot,
	int32_t value,
	int32_t cycle
	)
{
	uint32_t diff;
	char	 cpl = 0; /* modulo complement flag */
	char	 neg = 0; /* sign change flag	    */

	/* make the cycle positive and adjust the flags */
	if (cycle < 0) {
		cycle = - cycle;
		neg ^= 1;
		cpl ^= 1;
	}
	/* guard against div by zero or one */
	if (cycle > 1) {
		/*
		 * Get absolute difference as unsigned quantity and
		 * the complement flag. This is done by always
		 * subtracting the smaller value from the bigger
		 * one.
		 */
		if (value >= pivot) {
			diff = int32_to_uint32_2cpl(value)
			     - int32_to_uint32_2cpl(pivot);
		} else {
			diff = int32_to_uint32_2cpl(pivot)
			     - int32_to_uint32_2cpl(value);
			cpl ^= 1;
		}
		diff %= (uint32_t)cycle;
		if (diff) {
			if (cpl)
				diff = (uint32_t)cycle - diff;
			if (neg)
				diff = ~diff + 1;
			pivot += uint32_2cpl_to_int32(diff);
		}
	}
	return pivot;
}

/*---------------------------------------------------------------------
 * Note to the casual reader
 *
 * In the next two functions you will find (or would have found...)
 * the expression
 *
 *   res.Q_s -= 0x80000000;
 *
 * There was some ruckus about a possible programming error due to
 * integer overflow and sign propagation.
 *
 * This assumption is based on a lack of understanding of the C
 * standard. (Though this is admittedly not one of the most 'natural'
 * aspects of the 'C' language and easily to get wrong.)
 *
 * see 
 *	http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
 *	"ISO/IEC 9899:201x Committee Draft — April 12, 2011"
 *	6.4.4.1 Integer constants, clause 5
 *
 * why there is no sign extension/overflow problem here.
 *
 * But to ease the minds of the doubtful, I added back the 'u' qualifiers
 * that somehow got lost over the last years. 
 */


/*
 *---------------------------------------------------------------------
 * Convert a timestamp in NTP scale to a 64bit seconds value in the UN*X
 * scale with proper epoch unfolding around a given pivot or the current
 * system time. This function happily accepts negative pivot values as
 * timestamps befor 1970-01-01, so be aware of possible trouble on
 * platforms with 32bit 'time_t'!
 *
 * This is also a periodic extension, but since the cycle is 2^32 and
 * the shift is 2^31, we can do some *very* fast math without explicit
 * divisions.
 *---------------------------------------------------------------------
 */
vint64
ntpcal_ntp_to_time(
	uint32_t	ntp,
	const time_t *	pivot
	)
{
	vint64 res;

#   if defined(HAVE_INT64)

	res.q_s = (pivot != NULL)
		      ? *pivot
		      : now();
	res.Q_s -= 0x80000000u;		/* unshift of half range */
	ntp	-= (uint32_t)JAN_1970;	/* warp into UN*X domain */
	ntp	-= res.D_s.lo;		/* cycle difference	 */
	res.Q_s += (uint64_t)ntp;	/* get expanded time	 */

#   else /* no 64bit scalars */

	time_t tmp;

	tmp = (pivot != NULL)
		  ? *pivot
		  : now();
	res = time_to_vint64(&tmp);
	M_SUB(res.D_s.hi, res.D_s.lo, 0, 0x80000000u);
	ntp -= (uint32_t)JAN_1970;	/* warp into UN*X domain */
	ntp -= res.D_s.lo;		/* cycle difference	 */
	M_ADD(res.D_s.hi, res.D_s.lo, 0, ntp);

#   endif /* no 64bit scalars */

	return res;
}

/*
 *---------------------------------------------------------------------
 * Convert a timestamp in NTP scale to a 64bit seconds value in the NTP
 * scale with proper epoch unfolding around a given pivot or the current
 * system time.
 *
 * Note: The pivot must be given in the UN*X time domain!
 *
 * This is also a periodic extension, but since the cycle is 2^32 and
 * the shift is 2^31, we can do some *very* fast math without explicit
 * divisions.
 *---------------------------------------------------------------------
 */
vint64
ntpcal_ntp_to_ntp(
	uint32_t      ntp,
	const time_t *pivot
	)
{
	vint64 res;

#   if defined(HAVE_INT64)

	res.q_s = (pivot)
		      ? *pivot
		      : now();
	res.Q_s -= 0x80000000u;		/* unshift of half range */
	res.Q_s += (uint32_t)JAN_1970;	/* warp into NTP domain	 */
	ntp	-= res.D_s.lo;		/* cycle difference	 */
	res.Q_s += (uint64_t)ntp;	/* get expanded time	 */

#   else /* no 64bit scalars */

	time_t tmp;

	tmp = (pivot)
		  ? *pivot
		  : now();
	res = time_to_vint64(&tmp);
	M_SUB(res.D_s.hi, res.D_s.lo, 0, 0x80000000u);
	M_ADD(res.D_s.hi, res.D_s.lo, 0, (uint32_t)JAN_1970);/*into NTP */
	ntp -= res.D_s.lo;		/* cycle difference	 */
	M_ADD(res.D_s.hi, res.D_s.lo, 0, ntp);

#   endif /* no 64bit scalars */

	return res;
}


/*
 * ====================================================================
 *
 * Splitting values to composite entities
 *
 * ====================================================================
 */

/*
 *---------------------------------------------------------------------
 * Split a 64bit seconds value into elapsed days in 'res.hi' and
 * elapsed seconds since midnight in 'res.lo' using explicit floor
 * division. This function happily accepts negative time values as
 * timestamps before the respective epoch start.
 *---------------------------------------------------------------------
 */
ntpcal_split
ntpcal_daysplit(
	const vint64 *ts
	)
{
	ntpcal_split res;
	uint32_t Q;

#   if defined(HAVE_INT64)
	
	/* Manual floor division by SECSPERDAY. This uses the one's
	 * complement trick, too, but without an extra flag value: The
	 * flag would be 64bit, and that's a bit of overkill on a 32bit
	 * target that has to use a register pair for a 64bit number.
	 */
	if (ts->q_s < 0)
		Q = ~(uint32_t)(~ts->Q_s / SECSPERDAY);
	else
		Q = (uint32_t)(ts->Q_s / SECSPERDAY);

#   else

	uint32_t ah, al, sflag, A;

	/* get operand into ah/al (either ts or ts' one's complement,
	 * for later floor division)
	 */
	sflag = int32_sflag(ts->d_s.hi);
	ah = sflag ^ ts->D_s.hi;
	al = sflag ^ ts->D_s.lo;

	/* Since 86400 == 128*675 we can drop the least 7 bits and
	 * divide by 675 instead of 86400. Then the maximum remainder
	 * after each devision step is 674, and we need 10 bits for
	 * that. So in the next step we can shift in 22 bits from the
	 * numerator.
	 *
	 * Therefore we load the accu with the top 13 bits (51..63) in
	 * the first shot. We don't have to remember the quotient -- it
	 * would be shifted out anyway.
	 */
	A = ah >> 19;
	if (A >= 675)
		A = (A % 675u);

	/* Now assemble the remainder with bits 29..50 from the
	 * numerator and divide. This creates the upper ten bits of the
	 * quotient. (Well, the top 22 bits of a 44bit result. But that
	 * will be truncated to 32 bits anyway.)
	 */
	A = (A << 19) | (ah & 0x0007FFFFu);
	A = (A <<  3) | (al >> 29);
	Q = A / 675u;
	A = A % 675u;

	/* Now assemble the remainder with bits 7..28 from the numerator
	 * and do a final division step.
	 */
	A = (A << 22) | ((al >> 7) & 0x003FFFFFu);
	Q = (Q << 22) | (A / 675u);

	/* The last 7 bits get simply dropped, as they have no affect on
	 * the quotient when dividing by 86400.
	 */

	/* apply sign correction and calculate the true floor
	 * remainder.
	 */
	Q ^= sflag;
	
#   endif
	
	res.hi = uint32_2cpl_to_int32(Q);
	res.lo = ts->D_s.lo - Q * SECSPERDAY;

	return res;
}

/*
 *---------------------------------------------------------------------
 * Split a 32bit seconds value into h/m/s and excessive days.  This
 * function happily accepts negative time values as timestamps before
 * midnight.
 *---------------------------------------------------------------------
 */
static int32_t
priv_timesplit(
	int32_t split[3],
	int32_t ts
	)
{
	/* Do 3 chained floor divisions by positive constants, using the
	 * one's complement trick and factoring out the intermediate XOR
	 * ops to reduce the number of operations.
	 */
	uint32_t us, um, uh, ud, sflag;

	sflag = int32_sflag(ts);
	us    = int32_to_uint32_2cpl(ts);

	um = (sflag ^ us) / SECSPERMIN;
	uh = um / MINSPERHR;
	ud = uh / HRSPERDAY;

	um ^= sflag;
	uh ^= sflag;
	ud ^= sflag;

	split[0] = (int32_t)(uh - ud * HRSPERDAY );
	split[1] = (int32_t)(um - uh * MINSPERHR );
	split[2] = (int32_t)(us - um * SECSPERMIN);
	
	return uint32_2cpl_to_int32(ud);
}

/*
 *---------------------------------------------------------------------
 * Given the number of elapsed days in the calendar era, split this
 * number into the number of elapsed years in 'res.hi' and the number
 * of elapsed days of that year in 'res.lo'.
 *
 * if 'isleapyear' is not NULL, it will receive an integer that is 0 for
 * regular years and a non-zero value for leap years.
 *---------------------------------------------------------------------
 */
ntpcal_split
ntpcal_split_eradays(
	int32_t days,
	int  *isleapyear
	)
{
	/* Use the fast cyclesplit algorithm here, to calculate the
	 * centuries and years in a century with one division each. This
	 * reduces the number of division operations to two, but is
	 * susceptible to internal range overflow. We make sure the
	 * input operands are in the safe range; this still gives us
	 * approx +/-2.9 million years.
	 */
	ntpcal_split res;
	int32_t	 n100, n001; /* calendar year cycles */
	uint32_t uday, Q, sflag;

	/* split off centuries first */
	sflag = int32_sflag(days);
	uday  = uint32_saturate(int32_to_uint32_2cpl(days), sflag);
	uday  = (4u * uday) | 3u;
	Q    = sflag ^ ((sflag ^ uday) / GREGORIAN_CYCLE_DAYS);
	uday = uday - Q * GREGORIAN_CYCLE_DAYS;
	n100 = uint32_2cpl_to_int32(Q);
	
	/* Split off years in century -- days >= 0 here, and we're far
	 * away from integer overflow trouble now. */
	uday |= 3;
	n001 = uday / GREGORIAN_NORMAL_LEAP_CYCLE_DAYS;
	uday = uday % GREGORIAN_NORMAL_LEAP_CYCLE_DAYS;

	/* Assemble the year and day in year */
	res.hi = n100 * 100 + n001;
	res.lo = uday / 4u;

	/* Eventually set the leap year flag. Note: 0 <= n001 <= 99 and
	 * Q is still the two's complement representation of the
	 * centuries: The modulo 4 ops can be done with masking here.
	 * We also shift the year and the century by one, so the tests
	 * can be done against zero instead of 3.
	 */
	if (isleapyear)
		*isleapyear = !((n001+1) & 3)
		    && ((n001 != 99) || !((Q+1) & 3));
	
	return res;
}

/*
 *---------------------------------------------------------------------
 * Given a number of elapsed days in a year and a leap year indicator,
 * split the number of elapsed days into the number of elapsed months in
 * 'res.hi' and the number of elapsed days of that month in 'res.lo'.
 *
 * This function will fail and return {-1,-1} if the number of elapsed
 * days is not in the valid range!
 *---------------------------------------------------------------------
 */
ntpcal_split
ntpcal_split_yeardays(
	int32_t eyd,
	int     isleapyear
	)
{
	ntpcal_split    res;
	const uint16_t *lt;	/* month length table	*/

	/* check leap year flag and select proper table */
	lt = real_month_table[(isleapyear != 0)];
	if (0 <= eyd && eyd < lt[12]) {
		/* get zero-based month by approximation & correction step */
		res.hi = eyd >> 5;	   /* approx month; might be 1 too low */
		if (lt[res.hi + 1] <= eyd) /* fixup approximative month value  */
			res.hi += 1;
		res.lo = eyd - lt[res.hi];
	} else {
		res.lo = res.hi = -1;
	}

	return res;
}

/*
 *---------------------------------------------------------------------
 * Convert a RD into the date part of a 'struct calendar'.
 *---------------------------------------------------------------------
 */
int
ntpcal_rd_to_date(
	struct calendar *jd,
	int32_t		 rd
	)
{
	ntpcal_split split;
	int	     leapy;
	u_int	     ymask;

	/* Get day-of-week first. Since rd is signed, the remainder can
	 * be in the range [-6..+6], but the assignment to an unsigned
	 * variable maps the negative values to positive values >=7.
	 * This makes the sign correction look strange, but adding 7
	 * causes the needed wrap-around into the desired value range of
	 * zero to six, both inclusive.
	 */
	jd->weekday = rd % DAYSPERWEEK;
	if (jd->weekday >= DAYSPERWEEK)	/* weekday is unsigned! */
		jd->weekday += DAYSPERWEEK;

	split = ntpcal_split_eradays(rd - 1, &leapy);
	/* Get year and day-of-year, with overflow check. If any of the
	 * upper 16 bits is set after shifting to unity-based years, we
	 * will have an overflow when converting to an unsigned 16bit
	 * year. Shifting to the right is OK here, since it does not
	 * matter if the shift is logic or arithmetic.
	 */
	split.hi += 1;
	ymask = 0u - ((split.hi >> 16) == 0);
	jd->year = (uint16_t)(split.hi & ymask);
	jd->yearday = (uint16_t)split.lo + 1;

	/* convert to month and mday */
	split = ntpcal_split_yeardays(split.lo, leapy);
	jd->month    = (uint8_t)split.hi + 1;
	jd->monthday = (uint8_t)split.lo + 1;

	return ymask ? leapy : -1;
}

/*
 *---------------------------------------------------------------------
 * Convert a RD into the date part of a 'struct tm'.
 *---------------------------------------------------------------------
 */
int
ntpcal_rd_to_tm(
	struct tm  *utm,
	int32_t	    rd
	)
{
	ntpcal_split split;
	int	     leapy;

	/* get day-of-week first */
	utm->tm_wday = rd % DAYSPERWEEK;
	if (utm->tm_wday < 0)
		utm->tm_wday += DAYSPERWEEK;

	/* get year and day-of-year */
	split = ntpcal_split_eradays(rd - 1, &leapy);
	utm->tm_year = split.hi - 1899;
	utm->tm_yday = split.lo;	/* 0-based */

	/* convert to month and mday */
	split = ntpcal_split_yeardays(split.lo, leapy);
	utm->tm_mon  = split.hi;	/* 0-based */
	utm->tm_mday = split.lo + 1;	/* 1-based */

	return leapy;
}

/*
 *---------------------------------------------------------------------
 * Take a value of seconds since midnight and split it into hhmmss in a
 * 'struct calendar'.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_daysec_to_date(
	struct calendar *jd,
	int32_t		sec
	)
{
	int32_t days;
	int   ts[3];

	days = priv_timesplit(ts, sec);
	jd->hour   = (uint8_t)ts[0];
	jd->minute = (uint8_t)ts[1];
	jd->second = (uint8_t)ts[2];

	return days;
}

/*
 *---------------------------------------------------------------------
 * Take a value of seconds since midnight and split it into hhmmss in a
 * 'struct tm'.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_daysec_to_tm(
	struct tm *utm,
	int32_t	   sec
	)
{
	int32_t days;
	int32_t ts[3];

	days = priv_timesplit(ts, sec);
	utm->tm_hour = ts[0];
	utm->tm_min  = ts[1];
	utm->tm_sec  = ts[2];

	return days;
}

/*
 *---------------------------------------------------------------------
 * take a split representation for day/second-of-day and day offset
 * and convert it to a 'struct calendar'. The seconds will be normalised
 * into the range of a day, and the day will be adjusted accordingly.
 *
 * returns >0 if the result is in a leap year, 0 if in a regular
 * year and <0 if the result did not fit into the calendar struct.
 *---------------------------------------------------------------------
 */
int
ntpcal_daysplit_to_date(
	struct calendar	   *jd,
	const ntpcal_split *ds,
	int32_t		    dof
	)
{
	dof += ntpcal_daysec_to_date(jd, ds->lo);
	return ntpcal_rd_to_date(jd, ds->hi + dof);
}

/*
 *---------------------------------------------------------------------
 * take a split representation for day/second-of-day and day offset
 * and convert it to a 'struct tm'. The seconds will be normalised
 * into the range of a day, and the day will be adjusted accordingly.
 *
 * returns 1 if the result is in a leap year and zero if in a regular
 * year.
 *---------------------------------------------------------------------
 */
int
ntpcal_daysplit_to_tm(
	struct tm	   *utm,
	const ntpcal_split *ds ,
	int32_t		    dof
	)
{
	dof += ntpcal_daysec_to_tm(utm, ds->lo);

	return ntpcal_rd_to_tm(utm, ds->hi + dof);
}

/*
 *---------------------------------------------------------------------
 * Take a UN*X time and convert to a calendar structure.
 *---------------------------------------------------------------------
 */
int
ntpcal_time_to_date(
	struct calendar	*jd,
	const vint64	*ts
	)
{
	ntpcal_split ds;

	ds = ntpcal_daysplit(ts);
	ds.hi += ntpcal_daysec_to_date(jd, ds.lo);
	ds.hi += DAY_UNIX_STARTS;

	return ntpcal_rd_to_date(jd, ds.hi);
}


/*
 * ====================================================================
 *
 * merging composite entities
 *
 * ====================================================================
 */

/*
 *---------------------------------------------------------------------
 * Merge a number of days and a number of seconds into seconds,
 * expressed in 64 bits to avoid overflow.
 *---------------------------------------------------------------------
 */
vint64
ntpcal_dayjoin(
	int32_t days,
	int32_t secs
	)
{
	vint64 res;

#   if defined(HAVE_INT64)

	res.q_s	 = days;
	res.q_s *= SECSPERDAY;
	res.q_s += secs;

#   else

	uint32_t p1, p2;
	int	 isneg;

	/*
	 * res = days *86400 + secs, using manual 16/32 bit
	 * multiplications and shifts.
	 */
	isneg = (days < 0);
	if (isneg)
		days = -days;

	/* assemble days * 675 */
	res.D_s.lo = (days & 0xFFFF) * 675u;
	res.D_s.hi = 0;
	p1 = (days >> 16) * 675u;
	p2 = p1 >> 16;
	p1 = p1 << 16;
	M_ADD(res.D_s.hi, res.D_s.lo, p2, p1);

	/* mul by 128, using shift */
	res.D_s.hi = (res.D_s.hi << 7) | (res.D_s.lo >> 25);
	res.D_s.lo = (res.D_s.lo << 7);

	/* fix sign */
	if (isneg)
		M_NEG(res.D_s.hi, res.D_s.lo);

	/* properly add seconds */
	p2 = 0;
	if (secs < 0) {
		p1 = (uint32_t)-secs;
		M_NEG(p2, p1);
	} else {
		p1 = (uint32_t)secs;
	}
	M_ADD(res.D_s.hi, res.D_s.lo, p2, p1);

#   endif

	return res;
}

/*
 *---------------------------------------------------------------------
 * get leap years since epoch in elapsed years
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_leapyears_in_years(
	int32_t years
	)
{
	/* We use the in-out-in algorithm here, using the one's
	 * complement division trick for negative numbers. The chained
	 * division sequence by 4/25/4 gives the compiler the chance to
	 * get away with only one true division and doing shifts otherwise.
	 */

	uint32_t sflag, sum, uyear;

	sflag = int32_sflag(years);
	uyear = int32_to_uint32_2cpl(years);
	uyear ^= sflag;

	sum  = (uyear /=  4u);	/*   4yr rule --> IN  */
	sum -= (uyear /= 25u);	/* 100yr rule --> OUT */
	sum += (uyear /=  4u);	/* 400yr rule --> IN  */

	/* Thanks to the alternation of IN/OUT/IN we can do the sum
	 * directly and have a single one's complement operation
	 * here. (Only if the years are negative, of course.) Otherwise
	 * the one's complement would have to be done when
	 * adding/subtracting the terms.
	 */
	return uint32_2cpl_to_int32(sflag ^ sum);
}

/*
 *---------------------------------------------------------------------
 * Convert elapsed years in Era into elapsed days in Era.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_days_in_years(
	int32_t years
	)
{
	return years * DAYSPERYEAR + ntpcal_leapyears_in_years(years);
}

/*
 *---------------------------------------------------------------------
 * Convert a number of elapsed month in a year into elapsed days in year.
 *
 * The month will be normalized, and 'res.hi' will contain the
 * excessive years that must be considered when converting the years,
 * while 'res.lo' will contain the number of elapsed days since start
 * of the year.
 *
 * This code uses the shifted-month-approach to convert month to days,
 * because then there is no need to have explicit leap year
 * information.	 The slight disadvantage is that for most month values
 * the result is a negative value, and the year excess is one; the
 * conversion is then simply based on the start of the following year.
 *---------------------------------------------------------------------
 */
ntpcal_split
ntpcal_days_in_months(
	int32_t m
	)
{
	ntpcal_split res;

	/* Add ten months and correct if needed. (It likely is...) */
	res.lo  = m + 10;
	res.hi  = (res.lo >= 12);
	if (res.hi)
		res.lo -= 12;

	/* if still out of range, normalise by floor division ... */
	if (res.lo < 0 || res.lo >= 12) {
		uint32_t mu, Q, sflag;
		sflag = int32_sflag(res.lo);
		mu    = int32_to_uint32_2cpl(res.lo);
		Q     = sflag ^ ((sflag ^ mu) / 12u);
		res.hi += uint32_2cpl_to_int32(Q);
		res.lo  = mu - Q * 12u;
	}
	
	/* get cummulated days in year with unshift */
	res.lo = shift_month_table[res.lo] - 306;

	return res;
}

/*
 *---------------------------------------------------------------------
 * Convert ELAPSED years/months/days of gregorian calendar to elapsed
 * days in Gregorian epoch.
 *
 * If you want to convert years and days-of-year, just give a month of
 * zero.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_edate_to_eradays(
	int32_t years,
	int32_t mons,
	int32_t mdays
	)
{
	ntpcal_split tmp;
	int32_t	     res;

	if (mons) {
		tmp = ntpcal_days_in_months(mons);
		res = ntpcal_days_in_years(years + tmp.hi) + tmp.lo;
	} else
		res = ntpcal_days_in_years(years);
	res += mdays;

	return res;
}

/*
 *---------------------------------------------------------------------
 * Convert ELAPSED years/months/days of gregorian calendar to elapsed
 * days in year.
 *
 * Note: This will give the true difference to the start of the given
 * year, even if months & days are off-scale.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_edate_to_yeardays(
	int32_t years,
	int32_t mons,
	int32_t mdays
	)
{
	ntpcal_split tmp;

	if (0 <= mons && mons < 12) {
		years += 1;
		mdays += real_month_table[is_leapyear(years)][mons];
	} else {
		tmp = ntpcal_days_in_months(mons);
		mdays += tmp.lo
		       + ntpcal_days_in_years(years + tmp.hi)
		       - ntpcal_days_in_years(years);
	}

	return mdays;
}

/*
 *---------------------------------------------------------------------
 * Convert elapsed days and the hour/minute/second information into
 * total seconds.
 *
 * If 'isvalid' is not NULL, do a range check on the time specification
 * and tell if the time input is in the normal range, permitting for a
 * single leapsecond.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_etime_to_seconds(
	int32_t hours,
	int32_t minutes,
	int32_t seconds
	)
{
	int32_t res;

	res = (hours * MINSPERHR + minutes) * SECSPERMIN + seconds;

	return res;
}

/*
 *---------------------------------------------------------------------
 * Convert the date part of a 'struct tm' (that is, year, month,
 * day-of-month) into the RD of that day.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_tm_to_rd(
	const struct tm *utm
	)
{
	return ntpcal_edate_to_eradays(utm->tm_year + 1899,
				       utm->tm_mon,
				       utm->tm_mday - 1) + 1;
}

/*
 *---------------------------------------------------------------------
 * Convert the date part of a 'struct calendar' (that is, year, month,
 * day-of-month) into the RD of that day.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_date_to_rd(
	const struct calendar *jd
	)
{
	return ntpcal_edate_to_eradays((int32_t)jd->year - 1,
				       (int32_t)jd->month - 1,
				       (int32_t)jd->monthday - 1) + 1;
}

/*
 *---------------------------------------------------------------------
 * convert a year number to rata die of year start
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_year_to_ystart(
	int32_t year
	)
{
	return ntpcal_days_in_years(year - 1) + 1;
}

/*
 *---------------------------------------------------------------------
 * For a given RD, get the RD of the associated year start,
 * that is, the RD of the last January,1st on or before that day.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_rd_to_ystart(
	int32_t rd
	)
{
	/*
	 * Rather simple exercise: split the day number into elapsed
	 * years and elapsed days, then remove the elapsed days from the
	 * input value. Nice'n sweet...
	 */
	return rd - ntpcal_split_eradays(rd - 1, NULL).lo;
}

/*
 *---------------------------------------------------------------------
 * For a given RD, get the RD of the associated month start.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_rd_to_mstart(
	int32_t rd
	)
{
	ntpcal_split split;
	int	     leaps;

	split = ntpcal_split_eradays(rd - 1, &leaps);
	split = ntpcal_split_yeardays(split.lo, leaps);

	return rd - split.lo;
}

/*
 *---------------------------------------------------------------------
 * take a 'struct calendar' and get the seconds-of-day from it.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_date_to_daysec(
	const struct calendar *jd
	)
{
	return ntpcal_etime_to_seconds(jd->hour, jd->minute,
				       jd->second);
}

/*
 *---------------------------------------------------------------------
 * take a 'struct tm' and get the seconds-of-day from it.
 *---------------------------------------------------------------------
 */
int32_t
ntpcal_tm_to_daysec(
	const struct tm *utm
	)
{
	return ntpcal_etime_to_seconds(utm->tm_hour, utm->tm_min,
				       utm->tm_sec);
}

/*
 *---------------------------------------------------------------------
 * take a 'struct calendar' and convert it to a 'time_t'
 *---------------------------------------------------------------------
 */
time_t
ntpcal_date_to_time(
	const struct calendar *jd
	)
{
	vint64  join;
	int32_t days, secs;

	days = ntpcal_date_to_rd(jd) - DAY_UNIX_STARTS;
	secs = ntpcal_date_to_daysec(jd);
	join = ntpcal_dayjoin(days, secs);

	return vint64_to_time(&join);
}


/*
 * ====================================================================
 *
 * extended and unchecked variants of caljulian/caltontp
 *
 * ====================================================================
 */
int
ntpcal_ntp64_to_date(
	struct calendar *jd,
	const vint64    *ntp
	)
{
	ntpcal_split ds;

	ds = ntpcal_daysplit(ntp);
	ds.hi += ntpcal_daysec_to_date(jd, ds.lo);

	return ntpcal_rd_to_date(jd, ds.hi + DAY_NTP_STARTS);
}

int
ntpcal_ntp_to_date(
	struct calendar *jd,
	uint32_t	 ntp,
	const time_t	*piv
	)
{
	vint64	ntp64;

	/*
	 * Unfold ntp time around current time into NTP domain. Split
	 * into days and seconds, shift days into CE domain and
	 * process the parts.
	 */
	ntp64 = ntpcal_ntp_to_ntp(ntp, piv);
	return ntpcal_ntp64_to_date(jd, &ntp64);
}


vint64
ntpcal_date_to_ntp64(
	const struct calendar *jd
	)
{
	/*
	 * Convert date to NTP. Ignore yearday, use d/m/y only.
	 */
	return ntpcal_dayjoin(ntpcal_date_to_rd(jd) - DAY_NTP_STARTS,
			      ntpcal_date_to_daysec(jd));
}


uint32_t
ntpcal_date_to_ntp(
	const struct calendar *jd
	)
{
	/*
	 * Get lower half of 64-bit NTP timestamp from date/time.
	 */
	return ntpcal_date_to_ntp64(jd).d_s.lo;
}



/*
 * ====================================================================
 *
 * day-of-week calculations
 *
 * ====================================================================
 */
/*
 * Given a RataDie and a day-of-week, calculate a RDN that is reater-than,
 * greater-or equal, closest, less-or-equal or less-than the given RDN
 * and denotes the given day-of-week
 */
int32_t
ntpcal_weekday_gt(
	int32_t rdn,
	int32_t dow
	)
{
	return ntpcal_periodic_extend(rdn+1, dow, 7);
}

int32_t
ntpcal_weekday_ge(
	int32_t rdn,
	int32_t dow
	)
{
	return ntpcal_periodic_extend(rdn, dow, 7);
}

int32_t
ntpcal_weekday_close(
	int32_t rdn,
	int32_t dow
	)
{
	return ntpcal_periodic_extend(rdn-3, dow, 7);
}

int32_t
ntpcal_weekday_le(
	int32_t rdn,
	int32_t dow
	)
{
	return ntpcal_periodic_extend(rdn, dow, -7);
}

int32_t
ntpcal_weekday_lt(
	int32_t rdn,
	int32_t dow
	)
{
	return ntpcal_periodic_extend(rdn-1, dow, -7);
}

/*
 * ====================================================================
 *
 * ISO week-calendar conversions
 *
 * The ISO8601 calendar defines a calendar of years, weeks and weekdays.
 * It is related to the Gregorian calendar, and a ISO year starts at the
 * Monday closest to Jan,1st of the corresponding Gregorian year.  A ISO
 * calendar year has always 52 or 53 weeks, and like the Grogrian
 * calendar the ISO8601 calendar repeats itself every 400 years, or
 * 146097 days, or 20871 weeks.
 *
 * While it is possible to write ISO calendar functions based on the
 * Gregorian calendar functions, the following implementation takes a
 * different approach, based directly on years and weeks.
 *
 * Analysis of the tabulated data shows that it is not possible to
 * interpolate from years to weeks over a full 400 year range; cyclic
 * shifts over 400 years do not provide a solution here. But it *is*
 * possible to interpolate over every single century of the 400-year
 * cycle. (The centennial leap year rule seems to be the culprit here.)
 *
 * It can be shown that a conversion from years to weeks can be done
 * using a linear transformation of the form
 *
 *   w = floor( y * a + b )
 *
 * where the slope a must hold to
 *
 *  52.1780821918 <= a < 52.1791044776
 *
 * and b must be chosen according to the selected slope and the number
 * of the century in a 400-year period.
 *
 * The inverse calculation can also be done in this way. Careful scaling
 * provides an unlimited set of integer coefficients a,k,b that enable
 * us to write the calulation in the form
 *
 *   w = (y * a	 + b ) / k
 *   y = (w * a' + b') / k'
 *
 * In this implementation the values of k and k' are chosen to be
 * smallest possible powers of two, so the division can be implemented
 * as shifts if the optimiser chooses to do so.
 *
 * ====================================================================
 */

/*
 * Given a number of elapsed (ISO-)years since the begin of the
 * christian era, return the number of elapsed weeks corresponding to
 * the number of years.
 */
int32_t
isocal_weeks_in_years(
	int32_t years
	)
{	
	/*
	 * use: w = (y * 53431 + b[c]) / 1024 as interpolation
	 */
	static const uint16_t bctab[4] = { 157, 449, 597, 889 };

	int32_t  cs, cw;
	uint32_t cc, ci, yu, sflag;

	sflag = int32_sflag(years);
	yu    = int32_to_uint32_2cpl(years);
	
	/* split off centuries, using floor division */
	cc  = sflag ^ ((sflag ^ yu) / 100u);
	yu -= cc * 100u;

	/* calculate century cycles shift and cycle index:
	 * Assuming a century is 5217 weeks, we have to add a cycle
	 * shift that is 3 for every 4 centuries, because 3 of the four
	 * centuries have 5218 weeks. So '(cc*3 + 1) / 4' is the actual
	 * correction, and the second century is the defective one.
	 *
	 * Needs floor division by 4, which is done with masking and
	 * shifting.
	 */
	ci = cc * 3u + 1;
	cs = uint32_2cpl_to_int32(sflag ^ ((sflag ^ ci) / 4u));
	ci = ci % 4u;
	
	/* Get weeks in century. Can use plain division here as all ops
	 * are >= 0,  and let the compiler sort out the possible
	 * optimisations.
	 */
	cw = (yu * 53431u + bctab[ci]) / 1024u;

	return uint32_2cpl_to_int32(cc) * 5217 + cs + cw;
}

/*
 * Given a number of elapsed weeks since the begin of the christian
 * era, split this number into the number of elapsed years in res.hi
 * and the excessive number of weeks in res.lo. (That is, res.lo is
 * the number of elapsed weeks in the remaining partial year.)
 */
ntpcal_split
isocal_split_eraweeks(
	int32_t weeks
	)
{
	/*
	 * use: y = (w * 157 + b[c]) / 8192 as interpolation
	 */

	static const uint16_t bctab[4] = { 85, 130, 17, 62 };

	ntpcal_split res;
	int32_t  cc, ci;
	uint32_t sw, cy, Q, sflag;

	/* Use two fast cycle-split divisions here. This is again
	 * susceptible to internal overflow, so we check the range. This
	 * still permits more than +/-20 million years, so this is
	 * likely a pure academical problem.
	 *
	 * We want to execute '(weeks * 4 + 2) /% 20871' under floor
	 * division rules in the first step.
	 */
	sflag = int32_sflag(weeks);
	sw  = uint32_saturate(int32_to_uint32_2cpl(weeks), sflag);
	sw  = 4u * sw + 2;
	Q   = sflag ^ ((sflag ^ sw) / GREGORIAN_CYCLE_WEEKS);
	sw -= Q * GREGORIAN_CYCLE_WEEKS;
	ci  = Q % 4u;
	cc  = uint32_2cpl_to_int32(Q);

	/* Split off years; sw >= 0 here! The scaled weeks in the years
	 * are scaled up by 157 afterwards.
	 */ 
	sw  = (sw / 4u) * 157u + bctab[ci];
	cy  = sw / 8192u;	/* ws >> 13 , let the compiler sort it out */
	sw  = sw % 8192u;	/* ws & 8191, let the compiler sort it out */

	/* assemble elapsed years and downscale the elapsed weeks in
	 * the year.
	 */
	res.hi = 100*cc + cy;
	res.lo = sw / 157u;

	return res;
}

/*
 * Given a second in the NTP time scale and a pivot, expand the NTP
 * time stamp around the pivot and convert into an ISO calendar time
 * stamp.
 */
int
isocal_ntp64_to_date(
	struct isodate *id,
	const vint64   *ntp
	)
{
	ntpcal_split ds;
	int32_t      ts[3];
	uint32_t     uw, ud, sflag;

	/*
	 * Split NTP time into days and seconds, shift days into CE
	 * domain and process the parts.
	 */
	ds = ntpcal_daysplit(ntp);

	/* split time part */
	ds.hi += priv_timesplit(ts, ds.lo);
	id->hour   = (uint8_t)ts[0];
	id->minute = (uint8_t)ts[1];
	id->second = (uint8_t)ts[2];

	/* split days into days and weeks, using floor division in unsigned */
	ds.hi += DAY_NTP_STARTS - 1; /* shift from NTP to RDN */
	sflag = int32_sflag(ds.hi);
	ud  = int32_to_uint32_2cpl(ds.hi);
	uw  = sflag ^ ((sflag ^ ud) / DAYSPERWEEK);
	ud -= uw * DAYSPERWEEK;
	ds.hi = uint32_2cpl_to_int32(uw);
	ds.lo = ud;

	id->weekday = (uint8_t)ds.lo + 1;	/* weekday result    */

	/* get year and week in year */
	ds = isocal_split_eraweeks(ds.hi);	/* elapsed years&week*/
	id->year = (uint16_t)ds.hi + 1;		/* shift to current  */
	id->week = (uint8_t )ds.lo + 1;

	return (ds.hi >= 0 && ds.hi < 0x0000FFFF);
}

int
isocal_ntp_to_date(
	struct isodate *id,
	uint32_t	ntp,
	const time_t   *piv
	)
{
	vint64	ntp64;

	/*
	 * Unfold ntp time around current time into NTP domain, then
	 * convert the full time stamp.
	 */
	ntp64 = ntpcal_ntp_to_ntp(ntp, piv);
	return isocal_ntp64_to_date(id, &ntp64);
}

/*
 * Convert a ISO date spec into a second in the NTP time scale,
 * properly truncated to 32 bit.
 */
vint64
isocal_date_to_ntp64(
	const struct isodate *id
	)
{
	int32_t weeks, days, secs;

	weeks = isocal_weeks_in_years((int32_t)id->year - 1)
	      + (int32_t)id->week - 1;
	days = weeks * 7 + (int32_t)id->weekday;
	/* days is RDN of ISO date now */
	secs = ntpcal_etime_to_seconds(id->hour, id->minute, id->second);

	return ntpcal_dayjoin(days - DAY_NTP_STARTS, secs);
}

uint32_t
isocal_date_to_ntp(
	const struct isodate *id
	)
{
	/*
	 * Get lower half of 64-bit NTP timestamp from date/time.
	 */
	return isocal_date_to_ntp64(id).d_s.lo;
}

/*
 * ====================================================================
 * 'basedate' support functions
 * ====================================================================
 */

static int32_t s_baseday = NTP_TO_UNIX_DAYS;
static int32_t s_gpsweek = 0;

int32_t
basedate_eval_buildstamp(void)
{
	struct calendar jd;
	int32_t		ed;
	
	if (!ntpcal_get_build_date(&jd))
		return NTP_TO_UNIX_DAYS;

	/* The time zone of the build stamp is unspecified; we remove
	 * one day to provide a certain slack. And in case somebody
	 * fiddled with the system clock, we make sure we do not go
	 * before the UNIX epoch (1970-01-01). It's probably not possible
	 * to do this to the clock on most systems, but there are other
	 * ways to tweak the build stamp.
	 */
	jd.monthday -= 1;
	ed = ntpcal_date_to_rd(&jd) - DAY_NTP_STARTS;
	return (ed < NTP_TO_UNIX_DAYS) ? NTP_TO_UNIX_DAYS : ed;
}

int32_t
basedate_eval_string(
	const char * str
	)
{
	u_short	y,m,d;
	u_long	ned;
	int	rc, nc;
	size_t	sl;

	sl = strlen(str);	
	rc = sscanf(str, "%4hu-%2hu-%2hu%n", &y, &m, &d, &nc);
	if (rc == 3 && (size_t)nc == sl) {
		if (m >= 1 && m <= 12 && d >= 1 && d <= 31)
			return ntpcal_edate_to_eradays(y-1, m-1, d)
			    - DAY_NTP_STARTS;
		goto buildstamp;
	}

	rc = sscanf(str, "%lu%n", &ned, &nc);
	if (rc == 1 && (size_t)nc == sl) {
		if (ned <= INT32_MAX)
			return (int32_t)ned;
		goto buildstamp;
	}

  buildstamp:
	msyslog(LOG_WARNING,
		"basedate string \"%s\" invalid, build date substituted!",
		str);
	return basedate_eval_buildstamp();
}

uint32_t
basedate_get_day(void)
{
	return s_baseday;
}

int32_t
basedate_set_day(
	int32_t day
	)
{
	struct calendar	jd;
	int32_t		retv;

	/* set NTP base date for NTP era unfolding */
	if (day < NTP_TO_UNIX_DAYS) {
		msyslog(LOG_WARNING,
			"baseday_set_day: invalid day (%lu), UNIX epoch substituted",
			(unsigned long)day);
		day = NTP_TO_UNIX_DAYS;
	}
	retv = s_baseday; 
	s_baseday = day;
	ntpcal_rd_to_date(&jd, day + DAY_NTP_STARTS);
	msyslog(LOG_INFO, "basedate set to %04hu-%02hu-%02hu",
		jd.year, (u_short)jd.month, (u_short)jd.monthday);

	/* set GPS base week for GPS week unfolding */
	day = ntpcal_weekday_ge(day + DAY_NTP_STARTS, CAL_SUNDAY)
	    - DAY_NTP_STARTS;
	if (day < NTP_TO_GPS_DAYS)
	    day = NTP_TO_GPS_DAYS;
	s_gpsweek = (day - NTP_TO_GPS_DAYS) / DAYSPERWEEK;
	ntpcal_rd_to_date(&jd, day + DAY_NTP_STARTS);
	msyslog(LOG_INFO, "gps base set to %04hu-%02hu-%02hu (week %d)",
		jd.year, (u_short)jd.month, (u_short)jd.monthday, s_gpsweek);
	
	return retv;
}

time_t
basedate_get_eracenter(void)
{
	time_t retv;
	retv  = (time_t)(s_baseday - NTP_TO_UNIX_DAYS);
	retv *= SECSPERDAY;
	retv += (UINT32_C(1) << 31);
	return retv;
}

time_t
basedate_get_erabase(void)
{
	time_t retv;
	retv  = (time_t)(s_baseday - NTP_TO_UNIX_DAYS);
	retv *= SECSPERDAY;
	return retv;
}

uint32_t
basedate_get_gpsweek(void)
{
    return s_gpsweek;
}

uint32_t
basedate_expand_gpsweek(
    unsigned short weekno
    )
{
    /* We do a fast modulus expansion here. Since all quantities are
     * unsigned and we cannot go before the start of the GPS epoch
     * anyway, and since the truncated GPS week number is 10 bit, the
     * expansion becomes a simple sub/and/add sequence.
     */
    #if GPSWEEKS != 1024
    # error GPSWEEKS defined wrong -- should be 1024!
    #endif
    
    uint32_t diff;
    diff = ((uint32_t)weekno - s_gpsweek) & (GPSWEEKS - 1);
    return s_gpsweek + diff;
}

/* -*-EOF-*- */
