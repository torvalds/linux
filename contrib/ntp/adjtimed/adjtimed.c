/*************************************************************************/
/* (c) Copyright Tai Jin, 1988.  All Rights Reserved.                    */
/*     Hewlett-Packard Laboratories.                                     */
/*                                                                       */
/* Permission is hereby granted for unlimited modification, use, and     */
/* distribution.  This software is made available with no warranty of    */
/* any kind, express or implied.  This copyright notice must remain      */
/* intact in all versions of this software.                              */
/*                                                                       */
/* The author would appreciate it if any bug fixes and enhancements were */
/* to be sent back to him for incorporation into future versions of this */
/* software.  Please send changes to tai@iag.hp.com or ken@sdd.hp.com.   */
/*************************************************************************/

#ifndef lint
static char RCSid[] = "adjtimed.c,v 3.1 1993/07/06 01:04:45 jbj Exp";
#endif

/*
 * Adjust time daemon.
 * This daemon adjusts the rate of the system clock a la BSD's adjtime().
 * The adjtime() routine uses SYSV messages to communicate with this daemon.
 *
 * Caveat: This emulation uses an undocumented kernel variable.  As such, it
 * cannot be guaranteed to work in future HP-UX releases.  Fortunately,
 * it will no longer be needed in HPUX 10.01 and later.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/lock.h>
#include <time.h>
#include <signal.h>
#include <nlist.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "ntp_syslog.h"
#include "ntp_stdlib.h"

#include "adjtime.h"

double atof (const char *);

int InitClockRate (void);
int AdjustClockRate (register struct timeval *delta, register struct timeval *olddelta);
long GetClockRate (void);
int SetClockRate (long);
void ResetClockRate (void);
void Cleanup (void);
void Exit (int);

#define MILLION		1000000L

/* emacs cc-mode goes nuts if we split the next line... */
#define tvtod(tv)	((double)tv.tv_sec + ((double)tv.tv_usec / (double)MILLION))

char const *progname = NULL;
int verbose = 0;
int sysdebug = 0;
static int mqid;
static double oldrate = 0.0;

int
main(
	int argc,
	char *argv[]
	)
{
	struct timeval remains;
	struct sigvec vec;
	MsgBuf msg;
	char ch;
	int nofork = 0;
	int fd;

	progname = argv[0];

#ifdef LOG_LOCAL6
	openlog("adjtimed", LOG_PID, LOG_LOCAL6);
#else
	openlog("adjtimed", LOG_PID);
#endif

	while ((ch = ntp_getopt(argc, argv, "hkrvdfp:")) != EOF) {
		switch (ch) {
		    case 'k':
		    case 'r':
			if ((mqid = msgget(KEY, 0)) != -1) {
				if (msgctl(mqid, IPC_RMID, (struct msqid_ds *)0) == -1) {
					msyslog(LOG_ERR, "remove old message queue: %m");
					perror("adjtimed: remove old message queue");
					exit(1);
				}
			}

			if (ch == 'k')
			    exit(0);

			break;

		    case 'v':
			++verbose, nofork = 1;
			break;

		    case 'd':
			++sysdebug;
			break;

		    case 'f':
			nofork = 1;
			break;

		    case 'p':
			fputs("adjtimed: -p option ignored\n", stderr);
			break;

		    default:
			puts("usage: adjtimed -hkrvdf");
			puts("-h\thelp");
			puts("-k\tkill existing adjtimed, if any");
			puts("-r\trestart (kills existing adjtimed, if any)");
			puts("-v\tdebug output (repeat for more output)");
			puts("-d\tsyslog output (repeat for more output)");
			puts("-f\tno fork");
			msyslog(LOG_ERR, "usage error");
			exit(1);
		} /* switch */
	} /* while */

	if (!nofork) {
		switch (fork()) {
		    case 0:
			close(fileno(stdin));
			close(fileno(stdout));
			close(fileno(stderr));

#ifdef TIOCNOTTY
			if ((fd = open("/dev/tty")) != -1) {
				ioctl(fd, TIOCNOTTY, 0);
				close(fd);
			}
#else
			setpgrp();
#endif
			break;

		    case -1:
			msyslog(LOG_ERR, "fork: %m");
			perror("adjtimed: fork");
			exit(1);

		    default:
			exit(0);
		} /* switch */
	} /* if */

	if (nofork) {
		setvbuf(stdout, NULL, _IONBF, BUFSIZ);
		setvbuf(stderr, NULL, _IONBF, BUFSIZ);
	}

	msyslog(LOG_INFO, "started");
	if (verbose) printf("adjtimed: started\n");

	if (InitClockRate() == -1)
	    Exit(2);

	(void)signal(SIGHUP, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGTERM, Cleanup);

	vec.sv_handler = ResetClockRate;
	vec.sv_flags = 0;
	vec.sv_mask = ~0;
	sigvector(SIGALRM, &vec, (struct sigvec *)0);

	if (msgget(KEY, IPC_CREAT|IPC_EXCL) == -1) {
		if (errno == EEXIST) {
			msyslog(LOG_ERR, "message queue already exists, use -r to remove it");
			fputs("adjtimed: message queue already exists, use -r to remove it\n",
			      stderr);
			Exit(1);
		}

		msyslog(LOG_ERR, "create message queue: %m");
		perror("adjtimed: create message queue");
		Exit(1);
	}

	if ((mqid = msgget(KEY, 0)) == -1) {
		msyslog(LOG_ERR, "get message queue id: %m");
		perror("adjtimed: get message queue id");
		Exit(1);
	}
  
	/* Lock process in memory to improve response time */
	if (plock(PROCLOCK)) {
		msyslog(LOG_ERR, "plock: %m");
		perror("adjtimed: plock");
		Cleanup();
	}

	/* Also raise process priority.
	 * If we do not get run when we want, this leads to bad timekeeping
	 * and "Previous time adjustment didn't complete" gripes from xntpd.
	 */
	if (nice(-10) == -1) {
		msyslog(LOG_ERR, "nice: %m");
		perror("adjtimed: nice");
		Cleanup();
	}

	for (;;) {
		if (msgrcv(mqid, &msg.msgp, MSGSIZE, CLIENT, 0) == -1) {
			if (errno == EINTR) continue;
			msyslog(LOG_ERR, "read message: %m");
			perror("adjtimed: read message");
			Cleanup();
		}

		switch (msg.msgb.code) {
		    case DELTA1:
		    case DELTA2:
			AdjustClockRate(&msg.msgb.tv, &remains);

			if (msg.msgb.code == DELTA2) {
				msg.msgb.tv = remains;
				msg.msgb.mtype = SERVER;

				while (msgsnd(mqid, &msg.msgp, MSGSIZE, 0) == -1) {
					if (errno == EINTR) continue;
					msyslog(LOG_ERR, "send message: %m");
					perror("adjtimed: send message");
					Cleanup();
				}
			}

			if (remains.tv_sec + remains.tv_usec != 0L) {
				if (verbose) {
					printf("adjtimed: previous correction remaining %.6fs\n",
					       tvtod(remains));
				}
				if (sysdebug) {
					msyslog(LOG_INFO, "previous correction remaining %.6fs",
						tvtod(remains));
				}
			}
			break;

		    default:
			fprintf(stderr, "adjtimed: unknown message code %d\n", msg.msgb.code);
			msyslog(LOG_ERR, "unknown message code %d", msg.msgb.code);
		} /* switch */
	} /* loop */
} /* main */

/*
 * Default clock rate (old_tick).
 */
#define DEFAULT_RATE	(MILLION / HZ)
#define UNKNOWN_RATE	0L
#define TICK_ADJ	5	/* standard adjustment rate, microsec/tick */

static long default_rate = DEFAULT_RATE;
static long tick_rate = HZ;	/* ticks per sec */
static long slew_rate = TICK_ADJ * HZ; /* in microsec/sec */

int
AdjustClockRate(
	register struct timeval *delta,
	register struct timeval *olddelta
	)
{
	register long rate, dt, leftover;
	struct itimerval period, remains;
 
	dt = (delta->tv_sec * MILLION) + delta->tv_usec;

	if (verbose)
	    printf("adjtimed: new correction %.6fs\n", (double)dt / (double)MILLION);
	if (sysdebug)
	    msyslog(LOG_INFO, "new correction %.6fs", (double)dt / (double)MILLION);
	if (verbose > 2) printf("adjtimed: leftover %ldus\n", leftover);
	if (sysdebug > 2) msyslog(LOG_INFO, "leftover %ldus", leftover);
	rate = dt;

	/*
	 * Apply a slew rate of slew_rate over a period of dt/slew_rate seconds.
	 */
	if (dt > 0) {
		rate = slew_rate;
	} else {
		rate = -slew_rate;
		dt = -dt;
	}
	period.it_value.tv_sec = dt / slew_rate;
	period.it_value.tv_usec = (dt % slew_rate) * (MILLION / slew_rate);
	/*
	 * Note: we assume the kernel will convert the specified period into ticks
	 * using the modified clock rate rather than an assumed nominal clock rate,
	 * and therefore will generate the timer interrupt after the specified
	 * number of true seconds, not skewed seconds.
	 */

	if (verbose > 1)
	    printf("adjtimed: will be complete in %lds %ldus\n",
		   period.it_value.tv_sec, period.it_value.tv_usec);
	if (sysdebug > 1)
	    msyslog(LOG_INFO, "will be complete in %lds %ldus",
		    period.it_value.tv_sec, period.it_value.tv_usec);
	/*
	 * adjust the clock rate
	 */
	if (dt) {
		if (SetClockRate((rate / tick_rate) + default_rate) == -1) {
			msyslog(LOG_ERR, "set clock rate: %m");
			perror("adjtimed: set clock rate");
		}
	}
	/*
	 * start the timer
	 * (do this after changing the rate because the period has been rounded down)
	 */
	period.it_interval.tv_sec = period.it_interval.tv_usec = 0L;
	setitimer(ITIMER_REAL, &period, &remains);
	/*
	 * return old delta
	 */
	if (olddelta) {
		dt = ((remains.it_value.tv_sec * MILLION) + remains.it_value.tv_usec) *
			oldrate;
		olddelta->tv_sec = dt / MILLION;
		olddelta->tv_usec = dt - (olddelta->tv_sec * MILLION); 
	}

	oldrate = (double)rate / (double)MILLION;
	return(0);
} /* AdjustClockRate */

static struct nlist nl[] = {
#ifdef __hp9000s800
#ifdef PRE7_0
	{ "tick" },
#else
	{ "old_tick" },
#endif
#else
	{ "_old_tick" },
#endif
	{ "" }
};

static int kmem;

/*
 * The return value is the clock rate in old_tick units or -1 if error.
 */
long
GetClockRate(void)
{
	long rate, mask;

	if (lseek(kmem, (off_t)nl[0].n_value, 0) == -1L)
	    return (-1L);

	mask = sigblock(sigmask(SIGALRM));

	if (read(kmem, (caddr_t)&rate, sizeof(rate)) != sizeof(rate))
	    rate = UNKNOWN_RATE;

	sigsetmask(mask);
	return (rate);
} /* GetClockRate */

/*
 * The argument is the new rate in old_tick units.
 */
int
SetClockRate(
	long rate
	)
{
	long mask;

	if (lseek(kmem, (off_t)nl[0].n_value, 0) == -1L)
	    return (-1);

	mask = sigblock(sigmask(SIGALRM));

	if (write(kmem, (caddr_t)&rate, sizeof(rate)) != sizeof(rate)) {
		sigsetmask(mask);
		return (-1);
	}

	sigsetmask(mask);

	if (rate != default_rate) {
		if (verbose > 3) {
			printf("adjtimed: clock rate (%lu) %ldus/s\n", rate,
			       (rate - default_rate) * tick_rate);
		}
		if (sysdebug > 3) {
			msyslog(LOG_INFO, "clock rate (%lu) %ldus/s", rate,
				(rate - default_rate) * tick_rate);
		}
	}

	return (0);
} /* SetClockRate */

int
InitClockRate(void)
{
	if ((kmem = open("/dev/kmem", O_RDWR)) == -1) {
		msyslog(LOG_ERR, "open(/dev/kmem): %m");
		perror("adjtimed: open(/dev/kmem)");
		return (-1);
	}

	nlist("/hp-ux", nl);

	if (nl[0].n_type == 0) {
		fputs("adjtimed: /hp-ux has no symbol table\n", stderr);
		msyslog(LOG_ERR, "/hp-ux has no symbol table");
		return (-1);
	}
	/*
	 * Set the default to the system's original value
	 */
	default_rate = GetClockRate();
	if (default_rate == UNKNOWN_RATE) default_rate = DEFAULT_RATE;
	tick_rate = (MILLION / default_rate);
	slew_rate = TICK_ADJ * tick_rate;
	fprintf(stderr,"default_rate=%ld, tick_rate=%ld, slew_rate=%ld\n",default_rate,tick_rate,slew_rate);

	return (0);
} /* InitClockRate */

/*
 * Reset the clock rate to the default value.
 */
void
ResetClockRate(void)
{
	struct itimerval it;

	it.it_value.tv_sec = it.it_value.tv_usec = 0L;
	setitimer(ITIMER_REAL, &it, (struct itimerval *)0);

	if (verbose > 2) puts("adjtimed: resetting the clock");
	if (sysdebug > 2) msyslog(LOG_INFO, "resetting the clock");

	if (GetClockRate() != default_rate) {
		if (SetClockRate(default_rate) == -1) {
			msyslog(LOG_ERR, "set clock rate: %m");
			perror("adjtimed: set clock rate");
		}
	}

	oldrate = 0.0;
} /* ResetClockRate */

void
Cleanup(void)
{
	ResetClockRate();

	if (msgctl(mqid, IPC_RMID, (struct msqid_ds *)0) == -1) {
		if (errno != EINVAL) {
			msyslog(LOG_ERR, "remove message queue: %m");
			perror("adjtimed: remove message queue");
		}
	}

	Exit(2);
} /* Cleanup */

void
Exit(status)
     int status;
{
	msyslog(LOG_ERR, "terminated");
	closelog();
	if (kmem != -1) close(kmem);
	exit(status);
} /* Exit */
