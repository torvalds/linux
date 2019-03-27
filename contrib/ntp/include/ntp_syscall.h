/*
 * ntp_syscall.h - various ways to perform the ntp_adjtime() and ntp_gettime()
 * 		   system calls.
 */

#ifndef NTP_SYSCALL_H
#define NTP_SYSCALL_H

#ifdef HAVE_SYS_TIMEX_H
# include <sys/timex.h>
#endif

#ifndef NTP_SYSCALLS_LIBC
# ifdef NTP_SYSCALLS_STD
#  define ntp_adjtime(t)	syscall(SYS_ntp_adjtime, (t))
#  define ntp_gettime(t)	syscall(SYS_ntp_gettime, (t))
# else /* !NTP_SYSCALLS_STD */
#  ifdef HAVE_NTP_ADJTIME
extern	int	ntp_adjtime	(struct timex *);

#   ifndef HAVE_STRUCT_NTPTIMEVAL
struct ntptimeval
{
	struct timeval	time;		/* current time (ro) */
	long int	maxerror;	/* maximum error (us) (ro) */
	long int	esterror;	/* estimated error (us) (ro) */
};
#   endif

#   ifndef HAVE_NTP_GETTIME
static inline int
ntp_gettime(
	struct ntptimeval *ntv
	)
{
	struct timex tntx;
	int result;

	ZERO(tntx);
	result = ntp_adjtime(&tntx);
	ntv->time = tntx.time;
	ntv->maxerror = tntx.maxerror;
	ntv->esterror = tntx.esterror;
#    ifdef NTP_API
#     if NTP_API > 3
	ntv->tai = tntx.tai;
#     endif
#    endif
	return result;
}
#   endif	/* !HAVE_NTP_GETTIME */
#  endif	/* !HAVE_NTP_ADJTIME */
# endif	/* !NTP_SYSCALLS_STD */
#endif	/* !NTP_SYSCALLS_LIBC */

#endif	/* NTP_SYSCALL_H */
