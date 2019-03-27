/*
 * ntpd.c - main program for the fixed point NTP daemon
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntp_machine.h"
#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_stdlib.h"
#include <ntp_random.h>

#include "ntp_config.h"
#include "ntp_syslog.h"
#include "ntp_assert.h"
#include "isc/error.h"
#include "isc/strerror.h"
#include "isc/formatcheck.h"
#include "iosignal.h"

#ifdef SIM
# include "ntpsim.h"
#endif

#include "ntp_libopts.h"
#include "ntpd-opts.h"

/* there's a short treatise below what the thread stuff is for.
 * [Bug 2954] enable the threading warm-up only for Linux.
 */
#if defined(HAVE_PTHREADS) && HAVE_PTHREADS && !defined(NO_THREADS)
# ifdef HAVE_PTHREAD_H
#  include <pthread.h>
# endif
# if defined(linux)
#  define NEED_PTHREAD_WARMUP
# endif
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#include <stdio.h>
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifdef HAVE_SYS_SIGNAL_H
# include <sys/signal.h>
#else
# include <signal.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */
#if defined(HAVE_RTPRIO)
# ifdef HAVE_SYS_LOCK_H
#  include <sys/lock.h>
# endif
# include <sys/rtprio.h>
#else
# ifdef HAVE_PLOCK
#  ifdef HAVE_SYS_LOCK_H
#	include <sys/lock.h>
#  endif
# endif
#endif
#if defined(HAVE_SCHED_SETSCHEDULER)
# ifdef HAVE_SCHED_H
#  include <sched.h>
# else
#  ifdef HAVE_SYS_SCHED_H
#   include <sys/sched.h>
#  endif
# endif
#endif
#if defined(HAVE_SYS_MMAN_H)
# include <sys/mman.h>
#endif

#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif

#ifdef SYS_DOMAINOS
# include <apollo/base.h>
#endif /* SYS_DOMAINOS */


#include "recvbuff.h"
#include "ntp_cmdargs.h"

#if 0				/* HMS: I don't think we need this. 961223 */
#ifdef LOCK_PROCESS
# ifdef SYS_SOLARIS
#  include <sys/mman.h>
# else
#  include <sys/lock.h>
# endif
#endif
#endif

#ifdef SYS_WINNT
# include "ntservice.h"
#endif

#ifdef _AIX
# include <ulimit.h>
#endif /* _AIX */

#ifdef SCO5_CLOCK
# include <sys/ci/ciioctl.h>
#endif

#ifdef HAVE_DROPROOT
# include <ctype.h>
# include <grp.h>
# include <pwd.h>
#ifdef HAVE_LINUX_CAPABILITIES
# include <sys/capability.h>
# include <sys/prctl.h>
#endif /* HAVE_LINUX_CAPABILITIES */
#if defined(HAVE_PRIV_H) && defined(HAVE_SOLARIS_PRIVS)
# include <priv.h>
#endif /* HAVE_PRIV_H */
#if defined(HAVE_TRUSTEDBSD_MAC)
# include <sys/mac.h>
#endif /* HAVE_TRUSTEDBSD_MAC */
#endif /* HAVE_DROPROOT */

#if defined (LIBSECCOMP) && (KERN_SECCOMP)
/* # include <sys/types.h> */
# include <sys/resource.h>
# include <seccomp.h>
#endif /* LIBSECCOMP and KERN_SECCOMP */

#ifdef HAVE_DNSREGISTRATION
# include <dns_sd.h>
DNSServiceRef mdns;
#endif

#ifdef HAVE_SETPGRP_0
# define ntp_setpgrp(x, y)	setpgrp()
#else
# define ntp_setpgrp(x, y)	setpgrp(x, y)
#endif

#ifdef HAVE_SOLARIS_PRIVS
# define LOWPRIVS "basic,sys_time,net_privaddr,proc_setid,!proc_info,!proc_session,!proc_exec"
static priv_set_t *lowprivs = NULL;
static priv_set_t *highprivs = NULL;
#endif /* HAVE_SOLARIS_PRIVS */
/*
 * Scheduling priority we run at
 */
#define NTPD_PRIO	(-12)

int priority_done = 2;		/* 0 - Set priority */
				/* 1 - priority is OK where it is */
				/* 2 - Don't set priority */
				/* 1 and 2 are pretty much the same */

int listen_to_virtual_ips = TRUE;

/*
 * No-fork flag.  If set, we do not become a background daemon.
 */
int nofork;			/* Fork by default */

#ifdef HAVE_DNSREGISTRATION
/*
 * mDNS registration flag. If set, we attempt to register with the mDNS system, but only
 * after we have synched the first time. If the attempt fails, then try again once per 
 * minute for up to 5 times. After all, we may be starting before mDNS.
 */
int mdnsreg = FALSE;
int mdnstries = 5;
#endif  /* HAVE_DNSREGISTRATION */

#ifdef HAVE_DROPROOT
int droproot;
int root_dropped;
char *user;		/* User to switch to */
char *group;		/* group to switch to */
const char *chrootdir;	/* directory to chroot to */
uid_t sw_uid;
gid_t sw_gid;
struct group *gr;
struct passwd *pw;
#endif /* HAVE_DROPROOT */

#ifdef HAVE_WORKING_FORK
int	waitsync_fd_to_close = -1;	/* -w/--wait-sync */
#endif

/*
 * Version declaration
 */
extern const char *Version;

char const *progname;

int was_alarmed;

#ifdef DECL_SYSCALL
/*
 * We put this here, since the argument profile is syscall-specific
 */
extern int syscall	(int, ...);
#endif /* DECL_SYSCALL */


#if !defined(SIM) && defined(SIGDIE1)
static volatile int signalled	= 0;
static volatile int signo	= 0;

/* In an ideal world, 'finish_safe()' would declared as noreturn... */
static	void		finish_safe	(int);
static	RETSIGTYPE	finish		(int);
#endif

#if !defined(SIM) && defined(HAVE_WORKING_FORK)
static int	wait_child_sync_if	(int, long);
#endif

#if !defined(SIM) && !defined(SYS_WINNT)
# ifdef	DEBUG
static	RETSIGTYPE	moredebug	(int);
static	RETSIGTYPE	lessdebug	(int);
# else	/* !DEBUG follows */
static	RETSIGTYPE	no_debug	(int);
# endif	/* !DEBUG */
#endif	/* !SIM && !SYS_WINNT */

#ifndef WORK_FORK
int	saved_argc;
char **	saved_argv;
#endif

#ifndef SIM
int		ntpdmain		(int, char **);
static void	set_process_priority	(void);
static void	assertion_failed	(const char *, int,
					 isc_assertiontype_t,
					 const char *)
			__attribute__	((__noreturn__));
static void	library_fatal_error	(const char *, int, 
					 const char *, va_list)
					ISC_FORMAT_PRINTF(3, 0);
static void	library_unexpected_error(const char *, int,
					 const char *, va_list)
					ISC_FORMAT_PRINTF(3, 0);
#endif	/* !SIM */


/* Bug2332 unearthed a problem in the interaction of reduced user
 * privileges, the limits on memory usage and some versions of the
 * pthread library on Linux systems. The 'pthread_cancel()' function and
 * likely some others need to track the stack of the thread involved,
 * and uses a function that comes from GCC (--> libgcc_s.so) to do
 * this. Unfortunately the developers of glibc decided to load the
 * library on demand, which speeds up program start but can cause
 * trouble here: Due to all the things NTPD does to limit its resource
 * usage, this deferred load of libgcc_s does not always work once the
 * restrictions are in effect.
 *
 * One way out of this was attempting a forced link against libgcc_s
 * when possible because it makes the library available immediately
 * without deferred load. (The symbol resolution would still be dynamic
 * and on demand, but the code would already be in the process image.)
 *
 * This is a tricky thing to do, since it's not necessary everywhere,
 * not possible everywhere, has shown to break the build of other
 * programs in the NTP suite and is now generally frowned upon.
 *
 * So we take a different approach here: We creat a worker thread that does
 * actually nothing except waiting for cancellation and cancel it. If
 * this is done before all the limitations are put in place, the
 * machinery is pre-heated and all the runtime stuff should be in place
 * and useable when needed.
 *
 * This uses only the standard pthread API and should work with all
 * implementations of pthreads. It is not necessary everywhere, but it's
 * cheap enough to go on nearly unnoticed.
 *
 * Addendum: Bug 2954 showed that the assumption that this should work
 * with all OS is wrong -- at least FreeBSD bombs heavily.
 */
#ifdef NEED_PTHREAD_WARMUP

/* simple thread function: sleep until cancelled, just to exercise
 * thread cancellation.
 */
static void*
my_pthread_warmup_worker(
	void *thread_args)
{
	(void)thread_args;
	for (;;)
		sleep(10);
	return NULL;
}
	
/* pre-heat threading: create a thread and cancel it, just to exercise
 * thread cancellation.
 */
static void
my_pthread_warmup(void)
{
	pthread_t 	thread;
	pthread_attr_t	thr_attr;
	int       	rc;
	
	pthread_attr_init(&thr_attr);
#if defined(HAVE_PTHREAD_ATTR_GETSTACKSIZE) && \
    defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE) && \
    defined(PTHREAD_STACK_MIN)
	{
		size_t ssmin = 32*1024;	/* 32kB should be minimum */
		if (ssmin < PTHREAD_STACK_MIN)
			ssmin = PTHREAD_STACK_MIN;
		rc = pthread_attr_setstacksize(&thr_attr, ssmin);
		if (0 != rc)
			msyslog(LOG_ERR,
				"my_pthread_warmup: pthread_attr_setstacksize() -> %s",
				strerror(rc));
	}
#endif
	rc = pthread_create(
		&thread, &thr_attr, my_pthread_warmup_worker, NULL);
	pthread_attr_destroy(&thr_attr);
	if (0 != rc) {
		msyslog(LOG_ERR,
			"my_pthread_warmup: pthread_create() -> %s",
			strerror(rc));
	} else {
		pthread_cancel(thread);
		pthread_join(thread, NULL);
	}
}

#endif /*defined(NEED_PTHREAD_WARMUP)*/

#ifdef NEED_EARLY_FORK
static void
dummy_callback(void) { return; }

static void
fork_nonchroot_worker(void) {
	getaddrinfo_sometime("localhost", "ntp", NULL, INITIAL_DNS_RETRY,
			     (gai_sometime_callback)&dummy_callback, NULL);
}
#endif /* NEED_EARLY_FORK */

void
parse_cmdline_opts(
	int *	pargc,
	char ***pargv
	)
{
	static int	parsed;
	static int	optct;

	if (!parsed)
		optct = ntpOptionProcess(&ntpdOptions, *pargc, *pargv);

	parsed = 1;
	
	*pargc -= optct;
	*pargv += optct;
}


#ifdef SIM
int
main(
	int argc,
	char *argv[]
	)
{
	progname = argv[0];
	parse_cmdline_opts(&argc, &argv);
#ifdef DEBUG
	debug = OPT_VALUE_SET_DEBUG_LEVEL;
	DPRINTF(1, ("%s\n", Version));
#endif

	return ntpsim(argc, argv);
}
#else	/* !SIM follows */
#ifdef NO_MAIN_ALLOWED
CALL(ntpd,"ntpd",ntpdmain);
#else	/* !NO_MAIN_ALLOWED follows */
#ifndef SYS_WINNT
int
main(
	int argc,
	char *argv[]
	)
{
	return ntpdmain(argc, argv);
}
#endif /* !SYS_WINNT */
#endif /* !NO_MAIN_ALLOWED */
#endif /* !SIM */

#ifdef _AIX
/*
 * OK. AIX is different than solaris in how it implements plock().
 * If you do NOT adjust the stack limit, you will get the MAXIMUM
 * stack size allocated and PINNED with you program. To check the
 * value, use ulimit -a.
 *
 * To fix this, we create an automatic variable and set our stack limit
 * to that PLUS 32KB of extra space (we need some headroom).
 *
 * This subroutine gets the stack address.
 *
 * Grover Davidson and Matt Ladendorf
 *
 */
static char *
get_aix_stack(void)
{
	char ch;
	return (&ch);
}

/*
 * Signal handler for SIGDANGER.
 */
static void
catch_danger(int signo)
{
	msyslog(LOG_INFO, "ntpd: setpgid(): %m");
	/* Make the system believe we'll free something, but don't do it! */
	return;
}
#endif /* _AIX */

/*
 * Set the process priority
 */
#ifndef SIM
static void
set_process_priority(void)
{

# ifdef DEBUG
	if (debug > 1)
		msyslog(LOG_DEBUG, "set_process_priority: %s: priority_done is <%d>",
			((priority_done)
			 ? "Leave priority alone"
			 : "Attempt to set priority"
				),
			priority_done);
# endif /* DEBUG */

# if defined(HAVE_SCHED_SETSCHEDULER)
	if (!priority_done) {
		extern int config_priority_override, config_priority;
		int pmax, pmin;
		struct sched_param sched;

		pmax = sched_get_priority_max(SCHED_FIFO);
		sched.sched_priority = pmax;
		if ( config_priority_override ) {
			pmin = sched_get_priority_min(SCHED_FIFO);
			if ( config_priority > pmax )
				sched.sched_priority = pmax;
			else if ( config_priority < pmin )
				sched.sched_priority = pmin;
			else
				sched.sched_priority = config_priority;
		}
		if ( sched_setscheduler(0, SCHED_FIFO, &sched) == -1 )
			msyslog(LOG_ERR, "sched_setscheduler(): %m");
		else
			++priority_done;
	}
# endif /* HAVE_SCHED_SETSCHEDULER */
# ifdef HAVE_RTPRIO
#  ifdef RTP_SET
	if (!priority_done) {
		struct rtprio srtp;

		srtp.type = RTP_PRIO_REALTIME;	/* was: RTP_PRIO_NORMAL */
		srtp.prio = 0;		/* 0 (hi) -> RTP_PRIO_MAX (31,lo) */

		if (rtprio(RTP_SET, getpid(), &srtp) < 0)
			msyslog(LOG_ERR, "rtprio() error: %m");
		else
			++priority_done;
	}
#  else	/* !RTP_SET follows */
	if (!priority_done) {
		if (rtprio(0, 120) < 0)
			msyslog(LOG_ERR, "rtprio() error: %m");
		else
			++priority_done;
	}
#  endif	/* !RTP_SET */
# endif	/* HAVE_RTPRIO */
# if defined(NTPD_PRIO) && NTPD_PRIO != 0
#  ifdef HAVE_ATT_NICE
	if (!priority_done) {
		errno = 0;
		if (-1 == nice (NTPD_PRIO) && errno != 0)
			msyslog(LOG_ERR, "nice() error: %m");
		else
			++priority_done;
	}
#  endif	/* HAVE_ATT_NICE */
#  ifdef HAVE_BSD_NICE
	if (!priority_done) {
		if (-1 == setpriority(PRIO_PROCESS, 0, NTPD_PRIO))
			msyslog(LOG_ERR, "setpriority() error: %m");
		else
			++priority_done;
	}
#  endif	/* HAVE_BSD_NICE */
# endif	/* NTPD_PRIO && NTPD_PRIO != 0 */
	if (!priority_done)
		msyslog(LOG_ERR, "set_process_priority: No way found to improve our priority");
}
#endif	/* !SIM */

#if !defined(SIM) && !defined(SYS_WINNT)
/*
 * Detach from terminal (much like daemon())
 * Nothe that this function calls exit()
 */
# ifdef HAVE_WORKING_FORK
static void
detach_from_terminal(
	int pipe_fds[2],
	long wait_sync,
	const char *logfilename
	)
{
	int rc;
	int exit_code;
#  if !defined(HAVE_SETSID) && !defined (HAVE_SETPGID) && defined(TIOCNOTTY)
	int		fid;
#  endif
#  ifdef _AIX
	struct sigaction sa;
#  endif

	rc = fork();
	if (-1 == rc) {
		exit_code = (errno) ? errno : -1;
		msyslog(LOG_ERR, "fork: %m");
		exit(exit_code);
	}
	if (rc > 0) {
		/* parent */
		exit_code = wait_child_sync_if(pipe_fds[0],
					       wait_sync);
		exit(exit_code);
	}

	/*
	 * child/daemon
	 * close all open files excepting waitsync_fd_to_close.
	 * msyslog() unreliable until after init_logging().
	 */
	closelog();
	if (syslog_file != NULL) {
		fclose(syslog_file);
		syslog_file = NULL;
		syslogit = TRUE;
	}
	close_all_except(waitsync_fd_to_close);
	INSIST(0 == open("/dev/null", 0) && 1 == dup2(0, 1) \
		&& 2 == dup2(0, 2));

	init_logging(progname, 0, TRUE);
	/* we lost our logfile (if any) daemonizing */
	setup_logfile(logfilename);

#  ifdef SYS_DOMAINOS
	{
		uid_$t puid;
		status_$t st;

		proc2_$who_am_i(&puid);
		proc2_$make_server(&puid, &st);
	}
#  endif	/* SYS_DOMAINOS */
#  ifdef HAVE_SETSID
	if (setsid() == (pid_t)-1)
		msyslog(LOG_ERR, "setsid(): %m");
#  elif defined(HAVE_SETPGID)
	if (setpgid(0, 0) == -1)
		msyslog(LOG_ERR, "setpgid(): %m");
#  else		/* !HAVE_SETSID && !HAVE_SETPGID follows */
#   ifdef TIOCNOTTY
	fid = open("/dev/tty", 2);
	if (fid >= 0) {
		ioctl(fid, (u_long)TIOCNOTTY, NULL);
		close(fid);
	}
#   endif	/* TIOCNOTTY */
	ntp_setpgrp(0, getpid());
#  endif	/* !HAVE_SETSID && !HAVE_SETPGID */
#  ifdef _AIX
	/* Don't get killed by low-on-memory signal. */
	sa.sa_handler = catch_danger;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGDANGER, &sa, NULL);
#  endif	/* _AIX */

	return;
}
# endif /* HAVE_WORKING_FORK */

#ifdef HAVE_DROPROOT
/*
 * Map user name/number to user ID
*/
static int
map_user(
	)
{
	char *endp;

	if (isdigit((unsigned char)*user)) {
		sw_uid = (uid_t)strtoul(user, &endp, 0);
		if (*endp != '\0')
			goto getuser;

		if ((pw = getpwuid(sw_uid)) != NULL) {
			free(user);
			user = estrdup(pw->pw_name);
			sw_gid = pw->pw_gid;
		} else {
			errno = 0;
			msyslog(LOG_ERR, "Cannot find user ID %s", user);
			return 0;
		}

	} else {
getuser:
		errno = 0;
		if ((pw = getpwnam(user)) != NULL) {
			sw_uid = pw->pw_uid;
			sw_gid = pw->pw_gid;
		} else {
			if (errno)
				msyslog(LOG_ERR, "getpwnam(%s) failed: %m", user);
			else
				msyslog(LOG_ERR, "Cannot find user `%s'", user);
			return 0;
		}
	}

	return 1;
}

/*
 * Map group name/number to group ID
*/
static int
map_group(void)
{
	char *endp;

	if (isdigit((unsigned char)*group)) {
		sw_gid = (gid_t)strtoul(group, &endp, 0);
		if (*endp != '\0')
			goto getgroup;
	} else {
getgroup:
		if ((gr = getgrnam(group)) != NULL) {
			sw_gid = gr->gr_gid;
		} else {
			errno = 0;
			msyslog(LOG_ERR, "Cannot find group `%s'", group);
			return 0;
		}
	}

	return 1;
}

static int
set_group_ids(void)
{
	if (user && initgroups(user, sw_gid)) {
		msyslog(LOG_ERR, "Cannot initgroups() to user `%s': %m", user);
		return 0;
	}
	if (group && setgid(sw_gid)) {
		msyslog(LOG_ERR, "Cannot setgid() to group `%s': %m", group);
		return 0;
	}
	if (group && setegid(sw_gid)) {
		msyslog(LOG_ERR, "Cannot setegid() to group `%s': %m", group);
		return 0;
	}
	if (group) {
		if (0 != setgroups(1, &sw_gid)) {
			msyslog(LOG_ERR, "setgroups(1, %d) failed: %m", sw_gid);
			return 0;
		}
	}
	else if (pw)
		if (0 != initgroups(pw->pw_name, pw->pw_gid)) {
			msyslog(LOG_ERR, "initgroups(<%s>, %d) filed: %m", pw->pw_name, pw->pw_gid);
			return 0;
		}
	return 1;
}

static int
set_user_ids(void)
{
	if (user && setuid(sw_uid)) {
		msyslog(LOG_ERR, "Cannot setuid() to user `%s': %m", user);
		return 0;
	}
	if (user && seteuid(sw_uid)) {
		msyslog(LOG_ERR, "Cannot seteuid() to user `%s': %m", user);
		return 0;
	}
	return 1;
}

/*
 * Change (effective) user and group IDs, also initialize the supplementary group access list
 */
int set_user_group_ids(void);
int
set_user_group_ids(void)
{
	/* If the the user was already mapped, no need to map it again */
	if ((NULL != user) && (0 == sw_uid)) {
		if (0 == map_user())
			exit (-1);
	}
	/* same applies for the group */
	if ((NULL != group) && (0 == sw_gid)) {
		if (0 == map_group())
			exit (-1);
	}

	if (getegid() != sw_gid && 0 == set_group_ids())
		return 0;
	if (geteuid() != sw_uid && 0 == set_user_ids())
		return 0;

	return 1;
}
#endif /* HAVE_DROPROOT */
#endif /* !SIM */

/*
 * Main program.  Initialize us, disconnect us from the tty if necessary,
 * and loop waiting for I/O and/or timer expiries.
 */
#ifndef SIM
int
ntpdmain(
	int argc,
	char *argv[]
	)
{
	l_fp		now;
	struct recvbuf *rbuf;
	const char *	logfilename;
# ifdef HAVE_UMASK
	mode_t		uv;
# endif
# if defined(HAVE_GETUID) && !defined(MPE) /* MPE lacks the concept of root */
	uid_t		uid;
# endif
# if defined(HAVE_WORKING_FORK)
	long		wait_sync = 0;
	int		pipe_fds[2];
	int		rc;
	int		exit_code;
# endif	/* HAVE_WORKING_FORK*/
# ifdef SCO5_CLOCK
	int		fd;
	int		zero;
# endif

# ifdef NEED_PTHREAD_WARMUP
	my_pthread_warmup();
# endif
	
# ifdef HAVE_UMASK
	uv = umask(0);
	if (uv)
		umask(uv);
	else
		umask(022);
# endif
	saved_argc = argc;
	saved_argv = argv;
	progname = argv[0];
	initializing = TRUE;		/* mark that we are initializing */
	parse_cmdline_opts(&argc, &argv);
# ifdef DEBUG
	debug = OPT_VALUE_SET_DEBUG_LEVEL;
#  ifdef HAVE_SETLINEBUF
	setlinebuf(stdout);
#  endif
# endif

	if (HAVE_OPT(NOFORK) || HAVE_OPT(QUIT)
# ifdef DEBUG
	    || debug
# endif
	    || HAVE_OPT(SAVECONFIGQUIT))
		nofork = TRUE;

	init_logging(progname, NLOG_SYNCMASK, TRUE);
	/* honor -l/--logfile option to log to a file */
	if (HAVE_OPT(LOGFILE)) {
		logfilename = OPT_ARG(LOGFILE);
		syslogit = FALSE;
		change_logfile(logfilename, FALSE);
	} else {
		logfilename = NULL;
		if (nofork)
			msyslog_term = TRUE;
		if (HAVE_OPT(SAVECONFIGQUIT))
			syslogit = FALSE;
	}
	msyslog(LOG_NOTICE, "%s: Starting", Version);

	{
		int i;
		char buf[1024];	/* Secret knowledge of msyslog buf length */
		char *cp = buf;

		/* Note that every arg has an initial space character */
		snprintf(cp, sizeof(buf), "Command line:");
		cp += strlen(cp);

		for (i = 0; i < saved_argc ; ++i) {
			snprintf(cp, sizeof(buf) - (cp - buf),
				" %s", saved_argv[i]);
			cp += strlen(cp);
		}
		msyslog(LOG_INFO, "%s", buf);
	}

	/*
	 * Install trap handlers to log errors and assertion failures.
	 * Default handlers print to stderr which doesn't work if detached.
	 */
	isc_assertion_setcallback(assertion_failed);
	isc_error_setfatal(library_fatal_error);
	isc_error_setunexpected(library_unexpected_error);

	/* MPE lacks the concept of root */
# if defined(HAVE_GETUID) && !defined(MPE)
	uid = getuid();
	if (uid && !HAVE_OPT( SAVECONFIGQUIT )
#  if defined(HAVE_TRUSTEDBSD_MAC)
	    /* We can run as non-root if the mac_ntpd policy is enabled. */
	    && mac_is_present("ntpd") != 1
#  endif
	    ) {
		msyslog_term = TRUE;
		msyslog(LOG_ERR,
			"must be run as root, not uid %ld", (long)uid);
		exit(1);
	}
# endif

/*
 * Enable the Multi-Media Timer for Windows?
 */
# ifdef SYS_WINNT
	if (HAVE_OPT( MODIFYMMTIMER ))
		set_mm_timer(MM_TIMER_HIRES);
# endif

#ifdef HAVE_DNSREGISTRATION
/*
 * Enable mDNS registrations?
 */
	if (HAVE_OPT( MDNS )) {
		mdnsreg = TRUE;
	}
#endif  /* HAVE_DNSREGISTRATION */

	if (HAVE_OPT( NOVIRTUALIPS ))
		listen_to_virtual_ips = 0;

	/*
	 * --interface, listen on specified interfaces
	 */
	if (HAVE_OPT( INTERFACE )) {
		int		ifacect = STACKCT_OPT( INTERFACE );
		const char**	ifaces  = STACKLST_OPT( INTERFACE );
		sockaddr_u	addr;

		while (ifacect-- > 0) {
			add_nic_rule(
				is_ip_address(*ifaces, AF_UNSPEC, &addr)
					? MATCH_IFADDR
					: MATCH_IFNAME,
				*ifaces, -1, ACTION_LISTEN);
			ifaces++;
		}
	}

	if (HAVE_OPT( NICE ))
		priority_done = 0;

# ifdef HAVE_SCHED_SETSCHEDULER
	if (HAVE_OPT( PRIORITY )) {
		config_priority = OPT_VALUE_PRIORITY;
		config_priority_override = 1;
		priority_done = 0;
	}
# endif

# ifdef HAVE_WORKING_FORK
	/* make sure the FDs are initialised */
	pipe_fds[0] = -1;
	pipe_fds[1] = -1;
	do {					/* 'loop' once */
		if (!HAVE_OPT( WAIT_SYNC ))
			break;
		wait_sync = OPT_VALUE_WAIT_SYNC;
		if (wait_sync <= 0) {
			wait_sync = 0;
			break;
		}
		/* -w requires a fork() even with debug > 0 */
		nofork = FALSE;
		if (pipe(pipe_fds)) {
			exit_code = (errno) ? errno : -1;
			msyslog(LOG_ERR,
				"Pipe creation failed for --wait-sync: %m");
			exit(exit_code);
		}
		waitsync_fd_to_close = pipe_fds[1];
	} while (0);				/* 'loop' once */
# endif	/* HAVE_WORKING_FORK */

	init_lib();
# ifdef SYS_WINNT
	/*
	 * Make sure the service is initialized before we do anything else
	 */
	ntservice_init();

	/*
	 * Start interpolation thread, must occur before first
	 * get_systime()
	 */
	init_winnt_time();
# endif
	/*
	 * Initialize random generator and public key pair
	 */
	get_systime(&now);

	ntp_srandom((int)(now.l_i * now.l_uf));

	/*
	 * Detach us from the terminal.  May need an #ifndef GIZMO.
	 */
	if (!nofork) {

# ifdef HAVE_WORKING_FORK
		detach_from_terminal(pipe_fds, wait_sync, logfilename);
# endif		/* HAVE_WORKING_FORK */
	}

# ifdef SCO5_CLOCK
	/*
	 * SCO OpenServer's system clock offers much more precise timekeeping
	 * on the base CPU than the other CPUs (for multiprocessor systems),
	 * so we must lock to the base CPU.
	 */
	fd = open("/dev/at1", O_RDONLY);		
	if (fd >= 0) {
		zero = 0;
		if (ioctl(fd, ACPU_LOCK, &zero) < 0)
			msyslog(LOG_ERR, "cannot lock to base CPU: %m");
		close(fd);
	}
# endif

	/* Setup stack size in preparation for locking pages in memory. */
# if defined(HAVE_MLOCKALL)
#  ifdef HAVE_SETRLIMIT
	ntp_rlimit(RLIMIT_STACK, DFLT_RLIMIT_STACK * 4096, 4096, "4k");
#   ifdef RLIMIT_MEMLOCK
	/*
	 * The default RLIMIT_MEMLOCK is very low on Linux systems.
	 * Unless we increase this limit malloc calls are likely to
	 * fail if we drop root privilege.  To be useful the value
	 * has to be larger than the largest ntpd resident set size.
	 */
	ntp_rlimit(RLIMIT_MEMLOCK, DFLT_RLIMIT_MEMLOCK * 1024 * 1024, 1024 * 1024, "MB");
#   endif	/* RLIMIT_MEMLOCK */
#  endif	/* HAVE_SETRLIMIT */
# else	/* !HAVE_MLOCKALL follows */
#  ifdef HAVE_PLOCK
#   ifdef PROCLOCK
#    ifdef _AIX
	/*
	 * set the stack limit for AIX for plock().
	 * see get_aix_stack() for more info.
	 */
	if (ulimit(SET_STACKLIM, (get_aix_stack() - 8 * 4096)) < 0)
		msyslog(LOG_ERR,
			"Cannot adjust stack limit for plock: %m");
#    endif	/* _AIX */
#   endif	/* PROCLOCK */
#  endif	/* HAVE_PLOCK */
# endif	/* !HAVE_MLOCKALL */

	/*
	 * Set up signals we pay attention to locally.
	 */
# ifdef SIGDIE1
	signal_no_reset(SIGDIE1, finish);
	signal_no_reset(SIGDIE2, finish);
	signal_no_reset(SIGDIE3, finish);
	signal_no_reset(SIGDIE4, finish);
# endif
# ifdef SIGBUS
	signal_no_reset(SIGBUS, finish);
# endif

# if !defined(SYS_WINNT) && !defined(VMS)
#  ifdef DEBUG
	(void) signal_no_reset(MOREDEBUGSIG, moredebug);
	(void) signal_no_reset(LESSDEBUGSIG, lessdebug);
#  else
	(void) signal_no_reset(MOREDEBUGSIG, no_debug);
	(void) signal_no_reset(LESSDEBUGSIG, no_debug);
#  endif	/* DEBUG */
# endif	/* !SYS_WINNT && !VMS */

	/*
	 * Set up signals we should never pay attention to.
	 */
# ifdef SIGPIPE
	signal_no_reset(SIGPIPE, SIG_IGN);
# endif

	/*
	 * Call the init_ routines to initialize the data structures.
	 *
	 * Exactly what command-line options are we expecting here?
	 */
	INIT_SSL();
	init_auth();
	init_util();
	init_restrict();
	init_mon();
	init_timer();
	init_request();
	init_control();
	init_peer();
# ifdef REFCLOCK
	init_refclock();
# endif
	set_process_priority();
	init_proto();		/* Call at high priority */
	init_io();
	init_loopfilter();
	mon_start(MON_ON);	/* monitor on by default now	  */
				/* turn off in config if unwanted */

	/*
	 * Get the configuration.  This is done in a separate module
	 * since this will definitely be different for the gizmo board.
	 */
	getconfig(argc, argv);

	if (-1 == cur_memlock) {
# if defined(HAVE_MLOCKALL)
		/*
		 * lock the process into memory
		 */
		if (   !HAVE_OPT(SAVECONFIGQUIT)
#  ifdef RLIMIT_MEMLOCK
		    && -1 != DFLT_RLIMIT_MEMLOCK
#  endif
		    && 0 != mlockall(MCL_CURRENT|MCL_FUTURE))
			msyslog(LOG_ERR, "mlockall(): %m");
# else	/* !HAVE_MLOCKALL follows */
#  ifdef HAVE_PLOCK
#   ifdef PROCLOCK
		/*
		 * lock the process into memory
		 */
		if (!HAVE_OPT(SAVECONFIGQUIT) && 0 != plock(PROCLOCK))
			msyslog(LOG_ERR, "plock(PROCLOCK): %m");
#   else	/* !PROCLOCK follows  */
#    ifdef TXTLOCK
		/*
		 * Lock text into ram
		 */
		if (!HAVE_OPT(SAVECONFIGQUIT) && 0 != plock(TXTLOCK))
			msyslog(LOG_ERR, "plock(TXTLOCK) error: %m");
#    else	/* !TXTLOCK follows */
		msyslog(LOG_ERR, "plock() - don't know what to lock!");
#    endif	/* !TXTLOCK */
#   endif	/* !PROCLOCK */
#  endif	/* HAVE_PLOCK */
# endif	/* !HAVE_MLOCKALL */
	}

	loop_config(LOOP_DRIFTINIT, 0);
	report_event(EVNT_SYSRESTART, NULL, NULL);
	initializing = FALSE;

# ifdef HAVE_DROPROOT
	if (droproot) {

#ifdef NEED_EARLY_FORK
		fork_nonchroot_worker();
#endif

		/* Drop super-user privileges and chroot now if the OS supports this */

#  ifdef HAVE_LINUX_CAPABILITIES
		/* set flag: keep privileges accross setuid() call (we only really need cap_sys_time): */
		if (prctl( PR_SET_KEEPCAPS, 1L, 0L, 0L, 0L ) == -1) {
			msyslog( LOG_ERR, "prctl( PR_SET_KEEPCAPS, 1L ) failed: %m" );
			exit(-1);
		}
#  elif HAVE_SOLARIS_PRIVS
		/* Nothing to do here */
#  else
		/* we need a user to switch to */
		if (user == NULL) {
			msyslog(LOG_ERR, "Need user name to drop root privileges (see -u flag!)" );
			exit(-1);
		}
#  endif	/* HAVE_LINUX_CAPABILITIES || HAVE_SOLARIS_PRIVS */

		if (user != NULL) {
			if (0 == map_user())
				exit (-1);
		}
		if (group != NULL) {
			if (0 == map_group())
				exit (-1);
		}

		if (chrootdir ) {
			/* make sure cwd is inside the jail: */
			if (chdir(chrootdir)) {
				msyslog(LOG_ERR, "Cannot chdir() to `%s': %m", chrootdir);
				exit (-1);
			}
			if (chroot(chrootdir)) {
				msyslog(LOG_ERR, "Cannot chroot() to `%s': %m", chrootdir);
				exit (-1);
			}
			if (chdir("/")) {
				msyslog(LOG_ERR, "Cannot chdir() to`root after chroot(): %m");
				exit (-1);
			}
		}
#  ifdef HAVE_SOLARIS_PRIVS
		if ((lowprivs = priv_str_to_set(LOWPRIVS, ",", NULL)) == NULL) {
			msyslog(LOG_ERR, "priv_str_to_set() failed:%m");
			exit(-1);
		}
		if ((highprivs = priv_allocset()) == NULL) {
			msyslog(LOG_ERR, "priv_allocset() failed:%m");
			exit(-1);
		}
		(void) getppriv(PRIV_PERMITTED, highprivs);
		(void) priv_intersect(highprivs, lowprivs);
		if (setppriv(PRIV_SET, PRIV_PERMITTED, lowprivs) == -1) {
			msyslog(LOG_ERR, "setppriv() failed:%m");
			exit(-1);
		}
#  endif /* HAVE_SOLARIS_PRIVS */
		if (0 == set_user_group_ids())
			exit(-1);

#  if defined(HAVE_TRUSTEDBSD_MAC)
		/*
		 * To manipulate system time and (re-)bind to NTP_PORT as needed
		 * following interface changes, we must either run as uid 0 or
		 * the mac_ntpd policy module must be enabled.
		 */
		if (sw_uid != 0 && mac_is_present("ntpd") != 1) {
			msyslog(LOG_ERR, "Need MAC 'ntpd' policy enabled to drop root privileges");
			exit (-1);
		}
#  elif !defined(HAVE_LINUX_CAPABILITIES) && !defined(HAVE_SOLARIS_PRIVS)
		/*
		 * for now assume that the privilege to bind to privileged ports
		 * is associated with running with uid 0 - should be refined on
		 * ports that allow binding to NTP_PORT with uid != 0
		 */
		disable_dynamic_updates |= (sw_uid != 0);  /* also notifies routing message listener */
#  endif /* !HAVE_LINUX_CAPABILITIES && !HAVE_SOLARIS_PRIVS */

		if (disable_dynamic_updates && interface_interval) {
			interface_interval = 0;
			msyslog(LOG_INFO, "running as non-root disables dynamic interface tracking");
		}

#  ifdef HAVE_LINUX_CAPABILITIES
		{
			/*
			 *  We may be running under non-root uid now, but we still hold full root privileges!
			 *  We drop all of them, except for the crucial one or two: cap_sys_time and
			 *  cap_net_bind_service if doing dynamic interface tracking.
			 */
			cap_t caps;
			char *captext;
			
			captext = (0 != interface_interval)
				      ? "cap_sys_time,cap_net_bind_service=pe"
				      : "cap_sys_time=pe";
			caps = cap_from_text(captext);
			if (!caps) {
				msyslog(LOG_ERR,
					"cap_from_text(%s) failed: %m",
					captext);
				exit(-1);
			}
			if (-1 == cap_set_proc(caps)) {
				msyslog(LOG_ERR,
					"cap_set_proc() failed to drop root privs: %m");
				exit(-1);
			}
			cap_free(caps);
		}
#  endif	/* HAVE_LINUX_CAPABILITIES */
#  ifdef HAVE_SOLARIS_PRIVS
		if (priv_delset(lowprivs, "proc_setid") == -1) {
			msyslog(LOG_ERR, "priv_delset() failed:%m");
			exit(-1);
		}
		if (setppriv(PRIV_SET, PRIV_PERMITTED, lowprivs) == -1) {
			msyslog(LOG_ERR, "setppriv() failed:%m");
			exit(-1);
		}
		priv_freeset(lowprivs);
		priv_freeset(highprivs);
#  endif /* HAVE_SOLARIS_PRIVS */
		root_dropped = TRUE;
		fork_deferred_worker();
	}	/* if (droproot) */
# endif	/* HAVE_DROPROOT */

/* libssecomp sandboxing */
#if defined (LIBSECCOMP) && (KERN_SECCOMP)
	scmp_filter_ctx ctx;

	if ((ctx = seccomp_init(SCMP_ACT_KILL)) < 0)
		msyslog(LOG_ERR, "%s: seccomp_init(SCMP_ACT_KILL) failed: %m", __func__);
	else {
		msyslog(LOG_DEBUG, "%s: seccomp_init(SCMP_ACT_KILL) succeeded", __func__);
	}

#ifdef __x86_64__
int scmp_sc[] = {
	SCMP_SYS(adjtimex),
	SCMP_SYS(bind),
	SCMP_SYS(brk),
	SCMP_SYS(chdir),
	SCMP_SYS(clock_gettime),
	SCMP_SYS(clock_settime),
	SCMP_SYS(close),
	SCMP_SYS(connect),
	SCMP_SYS(exit_group),
	SCMP_SYS(fstat),
	SCMP_SYS(fsync),
	SCMP_SYS(futex),
	SCMP_SYS(getitimer),
	SCMP_SYS(getsockname),
	SCMP_SYS(ioctl),
	SCMP_SYS(lseek),
	SCMP_SYS(madvise),
	SCMP_SYS(mmap),
	SCMP_SYS(munmap),
	SCMP_SYS(open),
	SCMP_SYS(poll),
	SCMP_SYS(read),
	SCMP_SYS(recvmsg),
	SCMP_SYS(rename),
	SCMP_SYS(rt_sigaction),
	SCMP_SYS(rt_sigprocmask),
	SCMP_SYS(rt_sigreturn),
	SCMP_SYS(select),
	SCMP_SYS(sendto),
	SCMP_SYS(setitimer),
	SCMP_SYS(setsid),
	SCMP_SYS(socket),
	SCMP_SYS(stat),
	SCMP_SYS(time),
	SCMP_SYS(write),
};
#endif
#ifdef __i386__
int scmp_sc[] = {
	SCMP_SYS(_newselect),
	SCMP_SYS(adjtimex),
	SCMP_SYS(brk),
	SCMP_SYS(chdir),
	SCMP_SYS(clock_gettime),
	SCMP_SYS(clock_settime),
	SCMP_SYS(close),
	SCMP_SYS(exit_group),
	SCMP_SYS(fsync),
	SCMP_SYS(futex),
	SCMP_SYS(getitimer),
	SCMP_SYS(madvise),
	SCMP_SYS(mmap),
	SCMP_SYS(mmap2),
	SCMP_SYS(munmap),
	SCMP_SYS(open),
	SCMP_SYS(poll),
	SCMP_SYS(read),
	SCMP_SYS(rename),
	SCMP_SYS(rt_sigaction),
	SCMP_SYS(rt_sigprocmask),
	SCMP_SYS(select),
	SCMP_SYS(setitimer),
	SCMP_SYS(setsid),
	SCMP_SYS(sigprocmask),
	SCMP_SYS(sigreturn),
	SCMP_SYS(socketcall),
	SCMP_SYS(stat64),
	SCMP_SYS(time),
	SCMP_SYS(write),
};
#endif
	{
		int i;

		for (i = 0; i < COUNTOF(scmp_sc); i++) {
			if (seccomp_rule_add(ctx,
			    SCMP_ACT_ALLOW, scmp_sc[i], 0) < 0) {
				msyslog(LOG_ERR,
				    "%s: seccomp_rule_add() failed: %m",
				    __func__);
			}
		}
	}

	if (seccomp_load(ctx) < 0)
		msyslog(LOG_ERR, "%s: seccomp_load() failed: %m",
		    __func__);	
	else {
		msyslog(LOG_DEBUG, "%s: seccomp_load() succeeded", __func__);
	}
#endif /* LIBSECCOMP and KERN_SECCOMP */

#ifdef SYS_WINNT
	ntservice_isup();
#endif

# ifdef HAVE_IO_COMPLETION_PORT

	for (;;) {
#if !defined(SIM) && defined(SIGDIE1)
		if (signalled)
			finish_safe(signo);
#endif
		GetReceivedBuffers();
# else /* normal I/O */

	BLOCK_IO_AND_ALARM();
	was_alarmed = FALSE;

	for (;;) {
#if !defined(SIM) && defined(SIGDIE1)
		if (signalled)
			finish_safe(signo);
#endif		
		if (alarm_flag) {	/* alarmed? */
			was_alarmed = TRUE;
			alarm_flag = FALSE;
		}

		/* collect async name/addr results */
		if (!was_alarmed)
		    harvest_blocking_responses();
		
		if (!was_alarmed && !has_full_recv_buffer()) {
			/*
			 * Nothing to do.  Wait for something.
			 */
			io_handler();
		}

		if (alarm_flag) {	/* alarmed? */
			was_alarmed = TRUE;
			alarm_flag = FALSE;
		}

		if (was_alarmed) {
			UNBLOCK_IO_AND_ALARM();
			/*
			 * Out here, signals are unblocked.  Call timer routine
			 * to process expiry.
			 */
			timer();
			was_alarmed = FALSE;
			BLOCK_IO_AND_ALARM();
		}

# endif		/* !HAVE_IO_COMPLETION_PORT */

# ifdef DEBUG_TIMING
		{
			l_fp pts;
			l_fp tsa, tsb;
			int bufcount = 0;

			get_systime(&pts);
			tsa = pts;
# endif
			rbuf = get_full_recv_buffer();
			while (rbuf != NULL) {
				if (alarm_flag) {
					was_alarmed = TRUE;
					alarm_flag = FALSE;
				}
				UNBLOCK_IO_AND_ALARM();

				if (was_alarmed) {
					/* avoid timer starvation during lengthy I/O handling */
					timer();
					was_alarmed = FALSE;
				}

				/*
				 * Call the data procedure to handle each received
				 * packet.
				 */
				if (rbuf->receiver != NULL) {
# ifdef DEBUG_TIMING
					l_fp dts = pts;

					L_SUB(&dts, &rbuf->recv_time);
					DPRINTF(2, ("processing timestamp delta %s (with prec. fuzz)\n", lfptoa(&dts, 9)));
					collect_timing(rbuf, "buffer processing delay", 1, &dts);
					bufcount++;
# endif
					(*rbuf->receiver)(rbuf);
				} else {
					msyslog(LOG_ERR, "fatal: receive buffer callback NULL");
					abort();
				}

				BLOCK_IO_AND_ALARM();
				freerecvbuf(rbuf);
				rbuf = get_full_recv_buffer();
			}
# ifdef DEBUG_TIMING
			get_systime(&tsb);
			L_SUB(&tsb, &tsa);
			if (bufcount) {
				collect_timing(NULL, "processing", bufcount, &tsb);
				DPRINTF(2, ("processing time for %d buffers %s\n", bufcount, lfptoa(&tsb, 9)));
			}
		}
# endif

		/*
		 * Go around again
		 */

# ifdef HAVE_DNSREGISTRATION
		if (mdnsreg && (current_time - mdnsreg ) > 60 && mdnstries && sys_leap != LEAP_NOTINSYNC) {
			mdnsreg = current_time;
			msyslog(LOG_INFO, "Attempting to register mDNS");
			if ( DNSServiceRegister (&mdns, 0, 0, NULL, "_ntp._udp", NULL, NULL, 
			    htons(NTP_PORT), 0, NULL, NULL, NULL) != kDNSServiceErr_NoError ) {
				if (!--mdnstries) {
					msyslog(LOG_ERR, "Unable to register mDNS, giving up.");
				} else {	
					msyslog(LOG_INFO, "Unable to register mDNS, will try later.");
				}
			} else {
				msyslog(LOG_INFO, "mDNS service registered.");
				mdnsreg = FALSE;
			}
		}
# endif /* HAVE_DNSREGISTRATION */

	}
	UNBLOCK_IO_AND_ALARM();
	return 1;
}
#endif	/* !SIM */


#if !defined(SIM) && defined(SIGDIE1)
/*
 * finish - exit gracefully
 */
static void
finish_safe(
	int	sig
	)
{
	const char *sig_desc;

	sig_desc = NULL;
#ifdef HAVE_STRSIGNAL
	sig_desc = strsignal(sig);
#endif
	if (sig_desc == NULL)
		sig_desc = "";
	msyslog(LOG_NOTICE, "%s exiting on signal %d (%s)", progname,
		sig, sig_desc);
	/* See Bug 2513 and Bug 2522 re the unlink of PIDFILE */
# ifdef HAVE_DNSREGISTRATION
	if (mdns != NULL)
		DNSServiceRefDeallocate(mdns);
# endif
	peer_cleanup();
	exit(0);
}

static RETSIGTYPE
finish(
	int	sig
	)
{
	signalled = 1;
	signo = sig;
}

#endif	/* !SIM && SIGDIE1 */


#ifndef SIM
/*
 * wait_child_sync_if - implements parent side of -w/--wait-sync
 */
# ifdef HAVE_WORKING_FORK
static int
wait_child_sync_if(
	int	pipe_read_fd,
	long	wait_sync
	)
{
	int	rc;
	int	exit_code;
	time_t	wait_end_time;
	time_t	cur_time;
	time_t	wait_rem;
	fd_set	readset;
	struct timeval wtimeout;

	if (0 == wait_sync) 
		return 0;

	/* waitsync_fd_to_close used solely by child */
	close(waitsync_fd_to_close);
	wait_end_time = time(NULL) + wait_sync;
	do {
		cur_time = time(NULL);
		wait_rem = (wait_end_time > cur_time)
				? (wait_end_time - cur_time)
				: 0;
		wtimeout.tv_sec = wait_rem;
		wtimeout.tv_usec = 0;
		FD_ZERO(&readset);
		FD_SET(pipe_read_fd, &readset);
		rc = select(pipe_read_fd + 1, &readset, NULL, NULL,
			    &wtimeout);
		if (-1 == rc) {
			if (EINTR == errno)
				continue;
			exit_code = (errno) ? errno : -1;
			msyslog(LOG_ERR,
				"--wait-sync select failed: %m");
			return exit_code;
		}
		if (0 == rc) {
			/*
			 * select() indicated a timeout, but in case
			 * its timeouts are affected by a step of the
			 * system clock, select() again with a zero 
			 * timeout to confirm.
			 */
			FD_ZERO(&readset);
			FD_SET(pipe_read_fd, &readset);
			wtimeout.tv_sec = 0;
			wtimeout.tv_usec = 0;
			rc = select(pipe_read_fd + 1, &readset, NULL,
				    NULL, &wtimeout);
			if (0 == rc)	/* select() timeout */
				break;
			else		/* readable */
				return 0;
		} else			/* readable */
			return 0;
	} while (wait_rem > 0);

	fprintf(stderr, "%s: -w/--wait-sync %ld timed out.\n",
		progname, wait_sync);
	return ETIMEDOUT;
}
# endif	/* HAVE_WORKING_FORK */


/*
 * assertion_failed - Redirect assertion failures to msyslog().
 */
static void
assertion_failed(
	const char *file,
	int line,
	isc_assertiontype_t type,
	const char *cond
	)
{
	isc_assertion_setcallback(NULL);    /* Avoid recursion */

	msyslog(LOG_ERR, "%s:%d: %s(%s) failed",
		file, line, isc_assertion_typetotext(type), cond);
	msyslog(LOG_ERR, "exiting (due to assertion failure)");

#if defined(DEBUG) && defined(SYS_WINNT)
	if (debug)
		DebugBreak();
#endif

	abort();
}


/*
 * library_fatal_error - Handle fatal errors from our libraries.
 */
static void
library_fatal_error(
	const char *file,
	int line,
	const char *format,
	va_list args
	)
{
	char errbuf[256];

	isc_error_setfatal(NULL);  /* Avoid recursion */

	msyslog(LOG_ERR, "%s:%d: fatal error:", file, line);
	vsnprintf(errbuf, sizeof(errbuf), format, args);
	msyslog(LOG_ERR, "%s", errbuf);
	msyslog(LOG_ERR, "exiting (due to fatal error in library)");

#if defined(DEBUG) && defined(SYS_WINNT)
	if (debug)
		DebugBreak();
#endif

	abort();
}


/*
 * library_unexpected_error - Handle non fatal errors from our libraries.
 */
# define MAX_UNEXPECTED_ERRORS 100
int unexpected_error_cnt = 0;
static void
library_unexpected_error(
	const char *file,
	int line,
	const char *format,
	va_list args
	)
{
	char errbuf[256];

	if (unexpected_error_cnt >= MAX_UNEXPECTED_ERRORS)
		return;	/* avoid clutter in log */

	msyslog(LOG_ERR, "%s:%d: unexpected error:", file, line);
	vsnprintf(errbuf, sizeof(errbuf), format, args);
	msyslog(LOG_ERR, "%s", errbuf);

	if (++unexpected_error_cnt == MAX_UNEXPECTED_ERRORS)
		msyslog(LOG_ERR, "Too many errors.  Shutting up.");

}
#endif	/* !SIM */

#if !defined(SIM) && !defined(SYS_WINNT)
# ifdef DEBUG

/*
 * moredebug - increase debugging verbosity
 */
static RETSIGTYPE
moredebug(
	int sig
	)
{
	int saved_errno = errno;

	if (debug < 255)
	{
		debug++;
		msyslog(LOG_DEBUG, "debug raised to %d", debug);
	}
	errno = saved_errno;
}


/*
 * lessdebug - decrease debugging verbosity
 */
static RETSIGTYPE
lessdebug(
	int sig
	)
{
	int saved_errno = errno;

	if (debug > 0)
	{
		debug--;
		msyslog(LOG_DEBUG, "debug lowered to %d", debug);
	}
	errno = saved_errno;
}

# else	/* !DEBUG follows */


/*
 * no_debug - We don't do the debug here.
 */
static RETSIGTYPE
no_debug(
	int sig
	)
{
	int saved_errno = errno;

	msyslog(LOG_DEBUG, "ntpd not compiled for debugging (signal %d)", sig);
	errno = saved_errno;
}
# endif	/* !DEBUG */
#endif	/* !SIM && !SYS_WINNT */
