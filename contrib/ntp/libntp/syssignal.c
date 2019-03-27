#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <signal.h>

#include "ntp_syslog.h"
#include "ntp_stdlib.h"

static ctrl_c_fn	ctrl_c_hook;
#ifndef SYS_WINNT
RETSIGTYPE sigint_handler(int);
#else
BOOL WINAPI console_event_handler(DWORD);
#endif


#ifdef HAVE_SIGACTION

# ifdef SA_RESTART
#  define Z_SA_RESTART		SA_RESTART
# else
#  define Z_SA_RESTART		0
# endif

void
signal_no_reset(
	int sig,
	void (*func)(int)
	)
{
	int n;
	struct sigaction vec;
	struct sigaction ovec;

	ZERO(vec);
	sigemptyset(&vec.sa_mask);
	vec.sa_handler = func;

	/* Added for PPS clocks on Solaris 7 which get EINTR errors */
# ifdef SIGPOLL
	if (SIGPOLL == sig)
		vec.sa_flags = Z_SA_RESTART;
# endif
# ifdef SIGIO
	if (SIGIO == sig)
		vec.sa_flags = Z_SA_RESTART;
# endif

	do
		n = sigaction(sig, &vec, &ovec);
	while (-1 == n && EINTR == errno);
	if (-1 == n) {
		perror("sigaction");
		exit(1);
	}
}

#elif  HAVE_SIGVEC

void
signal_no_reset(
	int sig,
	RETSIGTYPE (*func)(int)
	)
{
	struct sigvec sv;
	int n;

	ZERO(sv);
	sv.sv_handler = func;
	n = sigvec(sig, &sv, (struct sigvec *)NULL);
	if (-1 == n) {
		perror("sigvec");
		exit(1);
	}
}

#elif  HAVE_SIGSET

void
signal_no_reset(
	int sig,
	RETSIGTYPE (*func)(int)
	)
{
	int n;

	n = sigset(sig, func);
	if (-1 == n) {
		perror("sigset");
		exit(1);
	}
}

#else

/* Beware!	This implementation resets the signal to SIG_DFL */
void
signal_no_reset(
	int sig,
	RETSIGTYPE (*func)(int)
	)
{
#ifndef SIG_ERR
# define SIG_ERR	(-1)
#endif
	if (SIG_ERR == signal(sig, func)) {
		perror("signal");
		exit(1);
	}
}

#endif

#ifndef SYS_WINNT
/*
 * POSIX implementation of set_ctrl_c_hook()
 */
RETSIGTYPE
sigint_handler(
	int	signum
	)
{
	UNUSED_ARG(signum);
	if (ctrl_c_hook != NULL)
		(*ctrl_c_hook)();
}

void
set_ctrl_c_hook(
	ctrl_c_fn	c_hook
	)
{
	RETSIGTYPE (*handler)(int);

	if (NULL == c_hook) {
		handler = SIG_DFL;
		signal_no_reset(SIGINT, handler);
		ctrl_c_hook = c_hook;
	} else {
		ctrl_c_hook = c_hook;
		handler = &sigint_handler;
		signal_no_reset(SIGINT, handler);
	}
}
#else	/* SYS_WINNT follows */
/*
 * Windows implementation of set_ctrl_c_hook()
 */
BOOL WINAPI 
console_event_handler(  
	DWORD	dwCtrlType
	)
{
	BOOL handled;

	if (CTRL_C_EVENT == dwCtrlType && ctrl_c_hook != NULL) {
		(*ctrl_c_hook)();
		handled = TRUE;
	} else {
		handled = FALSE;
	}

	return handled;
}
void
set_ctrl_c_hook(
	ctrl_c_fn	c_hook
	)
{
	BOOL install;

	if (NULL == c_hook) {
		ctrl_c_hook = NULL;
		install = FALSE;
	} else {
		ctrl_c_hook = c_hook;
		install = TRUE;
	}
	if (!SetConsoleCtrlHandler(&console_event_handler, install))
		msyslog(LOG_ERR, "Can't %s console control handler: %m",
			(install)
			    ? "add"
			    : "remove");
}
#endif	/* SYS_WINNT */
