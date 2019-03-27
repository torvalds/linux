/*
 * ntp_unixtime.h - much of what was here is now in timevalops.h
 */

#ifndef NTP_UNIXTIME_H
#define NTP_UNIXTIME_H

#include "ntp_types.h"	/* picks up time.h via ntp_machine.h */
#include "ntp_calendar.h"

#ifdef SIM
#   define GETTIMEOFDAY(a, b) (node_gettime(&ntp_node, a))
#   define SETTIMEOFDAY(a, b) (node_settime(&ntp_node, a))
#   define ADJTIMEOFDAY(a, b) (node_adjtime(&ntp_node, a, b))
#else
#   define ADJTIMEOFDAY(a, b) (adjtime(a, b))
/* gettimeofday() takes two args in BSD and only one in SYSV */
# if defined(HAVE_SYS_TIMERS_H) && defined(HAVE_GETCLOCK)
#  include <sys/timers.h>
int getclock (int clock_type, struct timespec *tp);
#   define GETTIMEOFDAY(a, b) (gettimeofday(a, b))
#   define SETTIMEOFDAY(a, b) (settimeofday(a, b))
# else /* not (HAVE_SYS_TIMERS_H && HAVE_GETCLOCK) */
#  ifdef SYSV_TIMEOFDAY
#   define GETTIMEOFDAY(a, b) (gettimeofday(a))
#   define SETTIMEOFDAY(a, b) (settimeofday(a))
#  else /* ! SYSV_TIMEOFDAY */
#if defined SYS_CYGWIN32
#   define GETTIMEOFDAY(a, b) (gettimeofday(a, b))
#   define SETTIMEOFDAY(a, b) (settimeofday_NT(a))
#else
#   define GETTIMEOFDAY(a, b) (gettimeofday(a, b))
#   define SETTIMEOFDAY(a, b) (settimeofday(a, b))
#endif
#  endif /* SYSV_TIMEOFDAY */
# endif /* not (HAVE_SYS_TIMERS_H && HAVE_GETCLOCK) */
#endif /* SIM */

/*
 * Time of day conversion constant.  Ntp's time scale starts in 1900,
 * Unix in 1970.  The value is 1970 - 1900 in seconds, 0x83aa7e80 or
 * 2208988800.  This is larger than 32-bit INT_MAX, so unsigned
 * type is forced.
 */
#define	JAN_1970 ((u_int)NTP_TO_UNIX_DAYS * (u_int)SECSPERDAY)

#endif /* !defined(NTP_UNIXTIME_H) */
