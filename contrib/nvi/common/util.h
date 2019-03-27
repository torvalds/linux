/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: util.h,v 10.7 2013/02/24 21:00:10 zy Exp $
 */

/* Macros to init/set/clear/test flags. */
#define	FL_INIT(l, f)	(l) = (f)		/* Specific flags location. */
#define	FL_SET(l, f)	((l) |= (f))
#define	FL_CLR(l, f)	((l) &= ~(f))
#define	FL_ISSET(l, f)	((l) & (f))

#define	LF_INIT(f)	FL_INIT(flags, f)	/* Local variable flags. */
#define	LF_SET(f)	FL_SET(flags, f)
#define	LF_CLR(f)	FL_CLR(flags, f)
#define	LF_ISSET(f)	FL_ISSET(flags, f)

#define	F_INIT(p, f)	FL_INIT((p)->flags, f)	/* Structure element flags. */
#define	F_SET(p, f)	FL_SET((p)->flags, f)
#define	F_CLR(p, f)	FL_CLR((p)->flags, f)
#define	F_ISSET(p, f)	FL_ISSET((p)->flags, f)

/* Offset to next column of stop size, e.g. tab offsets. */
#define	COL_OFF(c, stop)	((stop) - ((c) % (stop)))

/* Busy message types. */
typedef enum { B_NONE, B_OFF, B_READ, B_RECOVER, B_SEARCH, B_WRITE } bmsg_t;

/*
 * Number handling defines and protoypes.
 *
 * NNFITS:	test for addition of two negative numbers under a limit
 * NPFITS:	test for addition of two positive numbers under a limit
 * NADD_SLONG:	test for addition of two signed longs
 * NADD_USLONG:	test for addition of two unsigned longs
 */
enum nresult { NUM_ERR, NUM_OK, NUM_OVER, NUM_UNDER };
#define	NNFITS(min, cur, add)						\
	(((long)(min)) - (cur) <= (add))
#define	NPFITS(max, cur, add)						\
	(((unsigned long)(max)) - (cur) >= (add))
#define	NADD_SLONG(sp, v1, v2)						\
	((v1) < 0 ?							\
	    ((v2) < 0 &&						\
	    NNFITS(LONG_MIN, (v1), (v2))) ? NUM_UNDER : NUM_OK :	\
	 (v1) > 0 ?							\
	    (v2) > 0 &&							\
	    NPFITS(LONG_MAX, (v1), (v2)) ? NUM_OK : NUM_OVER :		\
	 NUM_OK)
#define	NADD_USLONG(sp, v1, v2)						\
	(NPFITS(ULONG_MAX, (v1), (v2)) ? NUM_OK : NUM_OVER)

/* Macros for min/max. */
#undef	MIN
#undef	MAX
#define	MIN(_a,_b)	((_a)<(_b)?(_a):(_b))
#define	MAX(_a,_b)	((_a)<(_b)?(_b):(_a))

/* Operations on timespecs */
#undef timespecclear
#undef timespecisset
#undef timespeccmp
#undef timespecadd
#undef timespecsub
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timespecadd(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec += (uvp)->tv_sec;				\
		(vvp)->tv_nsec += (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec >= 1000000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_nsec -= 1000000000;			\
		}							\
	} while (0)
#define timespecsub(vvp, uvp)						\
	do {								\
		(vvp)->tv_sec -= (uvp)->tv_sec;				\
		(vvp)->tv_nsec -= (uvp)->tv_nsec;			\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	} while (0)
