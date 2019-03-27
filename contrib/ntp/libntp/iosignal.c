/*
 * iosignal.c - input/output routines for ntpd.	The socket-opening code
 *		   was shamelessly stolen from ntpd.
 */

/*
 * [Bug 158]
 * Do the #includes differently, as under some versions of Linux
 * sys/param.h has a #undef CONFIG_PHONE line in it.
 *
 * As we have ~40 CONFIG_ variables, I don't feel like renaming them
 * every time somebody adds a new macro to some system header.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <signal.h>
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif /* HAVE_SYS_PARAM_H */
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

#include <arpa/inet.h>

#if _BSDI_VERSION >= 199510
# include <ifaddrs.h>
#endif

# ifdef __QNXNTO__
#  include <fcntl.h>
#  include <unix.h>
#  define FNDELAY O_NDELAY
# endif

#include "ntp_machine.h"
#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_if.h"
#include "ntp_stdlib.h"
#include "iosignal.h"

#if defined(HAVE_SIGNALED_IO)
static RETSIGTYPE sigio_handler	(int);

/* consistency safegurad to catch BLOCK/UNBLOCK oversights */
static int sigio_block_count = 0;

/* main inputhandler to be called on SIGIO */
static input_handler_t *input_handler_callback = NULL;

# if defined(HAVE_SIGACTION)
/*
 * If sigaction() is used for signal handling and a signal is
 * pending then the kernel blocks the signal before it calls
 * the signal handler.
 *
 * The variable below is used to take care that the SIGIO signal
 * is not unintentionally unblocked inside the sigio_handler()
 * if the handler executes a piece of code that is normally
 * bracketed by BLOCKIO()/UNBLOCKIO() calls.
 */
static int sigio_handler_active = 0;
# endif

/*
 * SIGPOLL and SIGIO ROUTINES.
 */



/*
 * TTY initialization routines.
 */
int
init_clock_sig(
	struct refclockio *rio
	)
{
# ifdef USE_TTY_SIGPOLL
	{
		/* DO NOT ATTEMPT TO MAKE CLOCK-FD A CTTY: not portable, unreliable */
		if (ioctl(rio->fd, I_SETSIG, S_INPUT) < 0)
		{
			msyslog(LOG_ERR,
				"init_clock_sig: ioctl(I_SETSIG, S_INPUT) failed: %m");
			return 1;
		}
		return 0;
	}
# else
	/*
	 * Special cases first!
	 */
	/* Was: defined(SYS_HPUX) */
#  if defined(FIOSSAIOOWN) && defined(FIOSNBIO) && defined(FIOSSAIOSTAT)
#define CLOCK_DONE
	{
		int pgrp, on = 1;

		/* DO NOT ATTEMPT TO MAKE CLOCK-FD A CTTY: not portable, unreliable */
		pgrp = getpid();
		if (ioctl(rio->fd, FIOSSAIOOWN, (char *)&pgrp) == -1)
		{
			msyslog(LOG_ERR, "ioctl(FIOSSAIOOWN) fails for clock I/O: %m - EXITING");
			exit(1);
			/*NOTREACHED*/
		}

		/*
		 * set non-blocking, async I/O on the descriptor
		 */
		if (ioctl(rio->fd, FIOSNBIO, (char *)&on) == -1)
		{
			msyslog(LOG_ERR, "ioctl(FIOSNBIO) fails for clock I/O: %m - EXITING");
			exit(1);
			/*NOTREACHED*/
		}

		if (ioctl(rio->fd, FIOSSAIOSTAT, (char *)&on) == -1)
		{
			msyslog(LOG_ERR, "ioctl(FIOSSAIOSTAT) fails for clock I/O: %m - EXITING");
			exit(1);
			/*NOTREACHED*/
		}
		return 0;
	}
#  endif /* SYS_HPUX: FIOSSAIOOWN && FIOSNBIO && FIOSSAIOSTAT */
	/* Was: defined(SYS_AIX) && !defined(_BSD) */
#  if !defined(_BSD) && defined(_AIX) && defined(FIOASYNC) && defined(FIOSETOWN)
	/*
	 * SYSV compatibility mode under AIX.
	 */
#define CLOCK_DONE
	{
		int pgrp, on = 1;

		/* DO NOT ATTEMPT TO MAKE CLOCK-FD A CTTY: not portable, unreliable */
		if (ioctl(rio->fd, FIOASYNC, (char *)&on) == -1)
		{
			msyslog(LOG_ERR, "ioctl(FIOASYNC) fails for clock I/O: %m");
			return 1;
		}
		pgrp = -getpid();
		if (ioctl(rio->fd, FIOSETOWN, (char*)&pgrp) == -1)
		{
			msyslog(LOG_ERR, "ioctl(FIOSETOWN) fails for clock I/O: %m");
			return 1;
		}

		if (fcntl(rio->fd, F_SETFL, FNDELAY|FASYNC) < 0)
		{
			msyslog(LOG_ERR, "fcntl(FNDELAY|FASYNC) fails for clock I/O: %m");
			return 1;
		}
		return 0;
	}
#  endif /* AIX && !BSD: !_BSD && FIOASYNC && FIOSETOWN */
#  ifndef  CLOCK_DONE
	{
		/* DO NOT ATTEMPT TO MAKE CLOCK-FD A CTTY: not portable, unreliable */
#	if defined(TIOCSCTTY) && defined(USE_FSETOWNCTTY)
		/*
		 * there are, however, always exceptions to the rules
		 * one is, that OSF accepts SETOWN on TTY fd's only, iff they are
		 * CTTYs. SunOS and HPUX do not semm to have this restriction.
		 * another question is: how can you do multiple SIGIO from several
		 * ttys (as they all should be CTTYs), wondering...
		 *
		 * kd 95-07-16
		 */
		if (ioctl(rio->fd, TIOCSCTTY, 0) == -1)
		{
			msyslog(LOG_ERR, "ioctl(TIOCSCTTY, 0) fails for clock I/O: %m");
			return 1;
		}
#	endif /* TIOCSCTTY && USE_FSETOWNCTTY */

		if (fcntl(rio->fd, F_SETOWN, getpid()) == -1)
		{
			msyslog(LOG_ERR, "fcntl(F_SETOWN) fails for clock I/O: %m");
			return 1;
		}

		if (fcntl(rio->fd, F_SETFL, FNDELAY|FASYNC) < 0)
		{
			msyslog(LOG_ERR,
				"fcntl(FNDELAY|FASYNC) fails for clock I/O: %m");
			return 1;
		}
		return 0;
	}
#  endif /* CLOCK_DONE */
# endif /* !USE_TTY_SIGPOLL  */
}



void
init_socket_sig(
	int fd
	)
{
# ifdef USE_UDP_SIGPOLL
	{
		if (ioctl(fd, I_SETSIG, S_INPUT) < 0)
		{
			msyslog(LOG_ERR,
				"init_socket_sig: ioctl(I_SETSIG, S_INPUT) failed: %m - EXITING");
			exit(1);
		}
	}
# else /* USE_UDP_SIGPOLL */
	{
		int pgrp;
# ifdef FIOASYNC
		int on = 1;
# endif

#  if defined(FIOASYNC)
		if (ioctl(fd, FIOASYNC, (char *)&on) == -1)
		{
			msyslog(LOG_ERR, "ioctl(FIOASYNC) fails: %m - EXITING");
			exit(1);
			/*NOTREACHED*/
		}
#  elif defined(FASYNC)
		{
			int flags;

			if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
			{
				msyslog(LOG_ERR, "fcntl(F_GETFL) fails: %m - EXITING");
				exit(1);
				/*NOTREACHED*/
			}
			if (fcntl(fd, F_SETFL, flags|FASYNC) < 0)
			{
				msyslog(LOG_ERR, "fcntl(...|FASYNC) fails: %m - EXITING");
				exit(1);
				/*NOTREACHED*/
			}
		}
#  else
#	include "Bletch: Need asynchronous I/O!"
#  endif

#  ifdef UDP_BACKWARDS_SETOWN
		pgrp = -getpid();
#  else
		pgrp = getpid();
#  endif

#  if defined(SIOCSPGRP)
		if (ioctl(fd, SIOCSPGRP, (char *)&pgrp) == -1)
		{
			msyslog(LOG_ERR, "ioctl(SIOCSPGRP) fails: %m - EXITING");
			exit(1);
			/*NOTREACHED*/
		}
#  elif defined(FIOSETOWN)
		if (ioctl(fd, FIOSETOWN, (char*)&pgrp) == -1)
		{
			msyslog(LOG_ERR, "ioctl(FIOSETOWN) fails: %m - EXITING");
			exit(1);
			/*NOTREACHED*/
		}
#  elif defined(F_SETOWN)
		if (fcntl(fd, F_SETOWN, pgrp) == -1)
		{
			msyslog(LOG_ERR, "fcntl(F_SETOWN) fails: %m - EXITING");
			exit(1);
			/*NOTREACHED*/
		}
#  else
#	include "Bletch: Need to set process(group) to receive SIG(IO|POLL)"
#  endif
	}
# endif /* USE_UDP_SIGPOLL */
}

static RETSIGTYPE
sigio_handler(
	int sig
	)
{
	int saved_errno = errno;
	l_fp ts;

	get_systime(&ts);

# if defined(HAVE_SIGACTION)
	sigio_handler_active++;
	if (sigio_handler_active != 1)  /* This should never happen! */
	    msyslog(LOG_ERR, "sigio_handler: sigio_handler_active != 1");
# endif

	INSIST(input_handler_callback != NULL);
	(*input_handler_callback)(&ts);

# if defined(HAVE_SIGACTION)
	sigio_handler_active--;
	if (sigio_handler_active != 0)  /* This should never happen! */
	    msyslog(LOG_ERR, "sigio_handler: sigio_handler_active != 0");
# endif

	errno = saved_errno;
}

/*
 * Signal support routines.
 */
# ifdef HAVE_SIGACTION
void
set_signal(input_handler_t *input)
{
	INSIST(input != NULL);
	
	input_handler_callback = input;

	using_sigio = TRUE;
#  ifdef USE_SIGIO
	(void) signal_no_reset(SIGIO, sigio_handler);
# endif
#  ifdef USE_SIGPOLL
	(void) signal_no_reset(SIGPOLL, sigio_handler);
# endif
}

void
block_io_and_alarm(void)
{
	sigset_t set;

	if (sigemptyset(&set))
	    msyslog(LOG_ERR, "block_io_and_alarm: sigemptyset() failed: %m");
#  if defined(USE_SIGIO)
	if (sigaddset(&set, SIGIO))
	    msyslog(LOG_ERR, "block_io_and_alarm: sigaddset(SIGIO) failed: %m");
#  endif
#  if defined(USE_SIGPOLL)
	if (sigaddset(&set, SIGPOLL))
	    msyslog(LOG_ERR, "block_io_and_alarm: sigaddset(SIGPOLL) failed: %m");
#  endif
	if (sigaddset(&set, SIGALRM))
	    msyslog(LOG_ERR, "block_io_and_alarm: sigaddset(SIGALRM) failed: %m");

	if (sigprocmask(SIG_BLOCK, &set, NULL))
	    msyslog(LOG_ERR, "block_io_and_alarm: sigprocmask() failed: %m");
}

void
block_sigio(void)
{
	if ( sigio_handler_active == 0 )  /* not called from within signal handler */
	{
		sigset_t set;

		++sigio_block_count;
		if (sigio_block_count > 1)
		    msyslog(LOG_INFO, "block_sigio: sigio_block_count > 1");
		if (sigio_block_count < 1)
		    msyslog(LOG_INFO, "block_sigio: sigio_block_count < 1");

		if (sigemptyset(&set))
		    msyslog(LOG_ERR, "block_sigio: sigemptyset() failed: %m");
#	if defined(USE_SIGIO)
		if (sigaddset(&set, SIGIO))
		    msyslog(LOG_ERR, "block_sigio: sigaddset(SIGIO) failed: %m");
#	endif
#	if defined(USE_SIGPOLL)
		if (sigaddset(&set, SIGPOLL))
		    msyslog(LOG_ERR, "block_sigio: sigaddset(SIGPOLL) failed: %m");
#	endif

		if (sigprocmask(SIG_BLOCK, &set, NULL))
		    msyslog(LOG_ERR, "block_sigio: sigprocmask() failed: %m");
	}
}

void
unblock_io_and_alarm(void)
{
	sigset_t unset;

	if (sigemptyset(&unset))
	    msyslog(LOG_ERR, "unblock_io_and_alarm: sigemptyset() failed: %m");

#  if defined(USE_SIGIO)
	if (sigaddset(&unset, SIGIO))
	    msyslog(LOG_ERR, "unblock_io_and_alarm: sigaddset(SIGIO) failed: %m");
#  endif
#  if defined(USE_SIGPOLL)
	if (sigaddset(&unset, SIGPOLL))
	    msyslog(LOG_ERR, "unblock_io_and_alarm: sigaddset(SIGPOLL) failed: %m");
#  endif
	if (sigaddset(&unset, SIGALRM))
	    msyslog(LOG_ERR, "unblock_io_and_alarm: sigaddset(SIGALRM) failed: %m");

	if (sigprocmask(SIG_UNBLOCK, &unset, NULL))
	    msyslog(LOG_ERR, "unblock_io_and_alarm: sigprocmask() failed: %m");
}

void
unblock_sigio(void)
{
	if ( sigio_handler_active == 0 )  /* not called from within signal handler */
	{
		sigset_t unset;

		--sigio_block_count;
		if (sigio_block_count > 0)
		    msyslog(LOG_INFO, "unblock_sigio: sigio_block_count > 0");
		if (sigio_block_count < 0)
		    msyslog(LOG_INFO, "unblock_sigio: sigio_block_count < 0");

		if (sigemptyset(&unset))
		    msyslog(LOG_ERR, "unblock_sigio: sigemptyset() failed: %m");

#	if defined(USE_SIGIO)
		if (sigaddset(&unset, SIGIO))
		    msyslog(LOG_ERR, "unblock_sigio: sigaddset(SIGIO) failed: %m");
#	endif
#	if defined(USE_SIGPOLL)
		if (sigaddset(&unset, SIGPOLL))
		    msyslog(LOG_ERR, "unblock_sigio: sigaddset(SIGPOLL) failed: %m");
#	endif

		if (sigprocmask(SIG_UNBLOCK, &unset, NULL))
		    msyslog(LOG_ERR, "unblock_sigio: sigprocmask() failed: %m");
	}
}

void
wait_for_signal(void)
{
	sigset_t old;

	if (sigprocmask(SIG_UNBLOCK, NULL, &old))
	    msyslog(LOG_ERR, "wait_for_signal: sigprocmask() failed: %m");

#  if defined(USE_SIGIO)
	if (sigdelset(&old, SIGIO))
	    msyslog(LOG_ERR, "wait_for_signal: sigdelset(SIGIO) failed: %m");
#  endif
#  if defined(USE_SIGPOLL)
	if (sigdelset(&old, SIGPOLL))
	    msyslog(LOG_ERR, "wait_for_signal: sigdelset(SIGPOLL) failed: %m");
#  endif
	if (sigdelset(&old, SIGALRM))
	    msyslog(LOG_ERR, "wait_for_signal: sigdelset(SIGALRM) failed: %m");

	if (sigsuspend(&old) && (errno != EINTR))
	    msyslog(LOG_ERR, "wait_for_signal: sigsuspend() failed: %m");
}

# else /* !HAVE_SIGACTION */
/*
 * Must be an old bsd system.
 * We assume there is no SIGPOLL.
 */

void
block_io_and_alarm(void)
{
	int mask;

	mask = sigmask(SIGIO) | sigmask(SIGALRM);
	if (sigblock(mask))
	    msyslog(LOG_ERR, "block_io_and_alarm: sigblock() failed: %m");
}

void
block_sigio(void)
{
	int mask;

	++sigio_block_count;
	if (sigio_block_count > 1)
	    msyslog(LOG_INFO, "block_sigio: sigio_block_count > 1");
	if (sigio_block_count < 1)
	    msyslog(LOG_INFO, "block_sigio: sigio_block_count < 1");

	mask = sigmask(SIGIO);
	if (sigblock(mask))
	    msyslog(LOG_ERR, "block_sigio: sigblock() failed: %m");
}

void
set_signal(input_handler_t *input)
{
	INSIST(input != NULL);

	input_handler_callback = input;

	using_sigio = TRUE;
	(void) signal_no_reset(SIGIO, sigio_handler);
}

void
unblock_io_and_alarm(void)
{
	int mask, omask;

	mask = sigmask(SIGIO) | sigmask(SIGALRM);
	omask = sigblock(0);
	omask &= ~mask;
	(void) sigsetmask(omask);
}

void
unblock_sigio(void)
{
	int mask, omask;

	--sigio_block_count;
	if (sigio_block_count > 0)
	    msyslog(LOG_INFO, "unblock_sigio: sigio_block_count > 0");
	if (sigio_block_count < 0)
	    msyslog(LOG_INFO, "unblock_sigio: sigio_block_count < 0");
	mask = sigmask(SIGIO);
	omask = sigblock(0);
	omask &= ~mask;
	(void) sigsetmask(omask);
}

void
wait_for_signal(void)
{
	int mask, omask;

	mask = sigmask(SIGIO) | sigmask(SIGALRM);
	omask = sigblock(0);
	omask &= ~mask;
	if (sigpause(omask) && (errno != EINTR))
	    msyslog(LOG_ERR, "wait_for_signal: sigspause() failed: %m");
}

# endif /* HAVE_SIGACTION */
#else
int  NotAnEmptyCompilationUnit;
#endif 
