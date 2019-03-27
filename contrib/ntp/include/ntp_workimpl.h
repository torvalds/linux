/*
 * ntp_workimpl.h - selects worker child implementation
 */
#ifndef NTP_WORKIMPL_H
#define NTP_WORKIMPL_H

/*
 * Some systems do not support fork() and don't have an alternate
 * threads implementation of ntp_intres.  Such systems are limited
 * to using numeric IP addresses.
 */
#if defined(SYS_WINNT)
# define WORK_THREAD
#elif defined(ISC_PLATFORM_USETHREADS) && \
      defined(HAVE_SEM_TIMEDWAIT) && \
      (defined(HAVE_GETCLOCK) || defined(HAVE_CLOCK_GETTIME))
# define WORK_THREAD
# define WORK_PIPE
#elif defined(VMS) || defined(SYS_VXWORKS)
  /* empty */
#elif defined(HAVE_WORKING_FORK)
# define WORK_FORK
# define WORK_PIPE
#endif

#if defined(WORK_FORK) || defined(WORK_THREAD)
# define WORKER
#endif

#endif	/* !NTP_WORKIMPL_H */
