/* machines.c - provide special support for peculiar architectures
 *
 * Real bummers unite !
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ntp.h"
#include "ntp_machine.h"
#include "ntp_syslog.h"
#include "ntp_stdlib.h"
#include "ntp_unixtime.h"
#include "lib_strbuf.h"
#include "ntp_debug.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef SYS_WINNT
int _getch(void);	/* Declare the one function rather than include conio.h */
#else

#ifdef SYS_VXWORKS
#include "taskLib.h"
#include "sysLib.h"
#include "time.h"
#include "ntp_syslog.h"

/*	some translations to the world of vxWorkings -casey */
/* first some netdb type things */
#include "ioLib.h"
#include <socket.h>
int h_errno;

struct hostent *gethostbyname(char *name)
	{
	struct hostent *host1;
	h_errno = 0;					/* we are always successful!!! */
	host1 = (struct hostent *) emalloc (sizeof(struct hostent));
	host1->h_name = name;
	host1->h_addrtype = AF_INET;
	host1->h_aliases = name;
	host1->h_length = 4;
	host1->h_addr_list[0] = (char *)hostGetByName (name);
	host1->h_addr_list[1] = NULL;
	return host1;
	}

struct hostent *gethostbyaddr(char *name, int size, int addr_type)
	{
	struct hostent *host1;
	h_errno = 0;  /* we are always successful!!! */
	host1 = (struct hostent *) emalloc (sizeof(struct hostent));
	host1->h_name = name;
	host1->h_addrtype = AF_INET;
	host1->h_aliases = name;
	host1->h_length = 4;
	host1->h_addr_list = NULL;
	return host1;
	}

struct servent *getservbyname (char *name, char *type)
	{
	struct servent *serv1;
	serv1 = (struct servent *) emalloc (sizeof(struct servent));
	serv1->s_name = "ntp";      /* official service name */
	serv1->s_aliases = NULL;	/* alias list */
	serv1->s_port = 123;		/* port # */
	serv1->s_proto = "udp";     /* protocol to use */
	return serv1;
	}

/* second
 * vxworks thinks it has insomnia
 * we have to sleep for number of seconds
 */

#define CLKRATE 	sysClkRateGet()

/* I am not sure how valid the granularity is - it is from G. Eger's port */
#define CLK_GRANULARITY  1		/* Granularity of system clock in usec	*/
								/* Used to round down # usecs/tick		*/
								/* On a VCOM-100, PIT gets 8 MHz clk,	*/
								/*	& it prescales by 32, thus 4 usec	*/
								/* on mv167, granularity is 1usec anyway*/
								/* To defeat rounding, set to 1 		*/
#define USECS_PER_SEC		MILLION		/* Microseconds per second	*/
#define TICK (((USECS_PER_SEC / CLKRATE) / CLK_GRANULARITY) * CLK_GRANULARITY)

/* emulate unix sleep
 * casey
 */
void sleep(int seconds)
	{
	taskDelay(seconds*TICK);
	}
/* emulate unix alarm
 * that pauses and calls SIGALRM after the seconds are up...
 * so ... taskDelay() fudged for seconds should amount to the same thing.
 * casey
 */
void alarm (int seconds)
	{
	sleep(seconds);
	}

#endif /* SYS_VXWORKS */

#ifdef SYS_PTX			/* Does PTX still need this? */
/*#include <sys/types.h>	*/
#include <sys/procstats.h>

int
gettimeofday(
	struct timeval *tvp
	)
{
	/*
	 * hi, this is Sequents sneak path to get to a clock
	 * this is also the most logical syscall for such a function
	 */
	return (get_process_stats(tvp, PS_SELF, (struct procstats *) 0,
				  (struct procstats *) 0));
}
#endif /* SYS_PTX */

#ifdef MPE
/* This is a substitute for bind() that if called for an AF_INET socket
port less than 1024, GETPRIVMODE() and GETUSERMODE() calls will be done. */

#undef bind
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

extern void GETPRIVMODE(void);
extern void GETUSERMODE(void);

int __ntp_mpe_bind(int s, void *addr, int addrlen);

int __ntp_mpe_bind(int s, void *addr, int addrlen) {
	int priv = 0;
	int result;

if (addrlen == sizeof(struct sockaddr_in)) { /* AF_INET */
	if (((struct sockaddr_in *)addr)->sin_port > 0 &&
	    ((struct sockaddr_in *)addr)->sin_port < 1024) {
		priv = 1;
		GETPRIVMODE();
	}
/*	((struct sockaddr_in *)addr)->sin_addr.s_addr = 0; */
	result = bind(s,addr,addrlen);
	if (priv == 1) GETUSERMODE();
} else /* AF_UNIX */
	result = bind(s,addr,addrlen);

return result;
}

/*
 * MPE stupidly requires sfcntl() to be used on sockets instead of fcntl(),
 * so we define a wrapper to analyze the file descriptor and call the correct
 * function.
 */

#undef fcntl
#include <errno.h>
#include <fcntl.h>

int __ntp_mpe_fcntl(int fd, int cmd, int arg);

int __ntp_mpe_fcntl(int fd, int cmd, int arg) {
	int len;
	struct sockaddr sa;

	extern int sfcntl(int, int, int);

	len = sizeof sa;
	if (getsockname(fd, &sa, &len) == -1) {
		if (errno == EAFNOSUPPORT) /* AF_UNIX socket */
			return sfcntl(fd, cmd, arg);
		if (errno == ENOTSOCK) /* file or pipe */
			return fcntl(fd, cmd, arg);
		return (-1); /* unknown getsockname() failure */
	} else /* AF_INET socket */
		return sfcntl(fd, cmd, arg);
}

/*
 * Setitimer emulation support.  Note that we implement this using alarm(),
 * and since alarm() only delivers one signal, we must re-enable the alarm
 * by enabling our own SIGALRM setitimer_mpe_handler routine to be called
 * before the real handler routine and re-enable the alarm at that time.
 *
 * Note that this solution assumes that sigaction(SIGALRM) is called before
 * calling setitimer().  If it should ever to become necessary to support
 * sigaction(SIGALRM) after calling setitimer(), it will be necessary to trap
 * those sigaction() calls.
 */

#include <limits.h>
#include <signal.h>

/*
 * Some global data that needs to be shared between setitimer() and
 * setitimer_mpe_handler().
 */

struct {
	unsigned long current_msec;	/* current alarm() value in effect */
	unsigned long interval_msec;	/* next alarm() value from setitimer */
	unsigned long value_msec;	/* first alarm() value from setitimer */
	struct itimerval current_itimerval; /* current itimerval in effect */
	struct sigaction oldact;	/* SIGALRM state saved by setitimer */
} setitimer_mpe_ctx = { 0, 0, 0 };

/*
 * Undocumented, unsupported function to do alarm() in milliseconds.
 */

extern unsigned int px_alarm(unsigned long, int *);

/*
 * The SIGALRM handler routine enabled by setitimer().  Re-enable the alarm or
 * restore the original SIGALRM setting if no more alarms are needed.  Then
 * call the original SIGALRM handler (if any).
 */

static RETSIGTYPE setitimer_mpe_handler(int sig)
{
int alarm_hpe_status;

/* Update the new current alarm value */

setitimer_mpe_ctx.current_msec = setitimer_mpe_ctx.interval_msec;

if (setitimer_mpe_ctx.interval_msec > 0) {
  /* Additional intervals needed; re-arm the alarm timer */
  px_alarm(setitimer_mpe_ctx.interval_msec,&alarm_hpe_status);
} else {
  /* No more intervals, so restore previous original SIGALRM handler */
  sigaction(SIGALRM, &setitimer_mpe_ctx.oldact, NULL);
}

/* Call the original SIGALRM handler if it is a function and not just a flag */

if (setitimer_mpe_ctx.oldact.sa_handler != SIG_DFL &&
    setitimer_mpe_ctx.oldact.sa_handler != SIG_ERR &&
    setitimer_mpe_ctx.oldact.sa_handler != SIG_IGN)
  (*setitimer_mpe_ctx.oldact.sa_handler)(SIGALRM);

}

/*
 * Our implementation of setitimer().
 */

int
setitimer(int which, struct itimerval *value,
	    struct itimerval *ovalue)
{

int alarm_hpe_status;
unsigned long remaining_msec, value_msec, interval_msec;
struct sigaction newact;

/* 
 * Convert the initial interval to milliseconds
 */

if (value->it_value.tv_sec > (UINT_MAX / 1000))
  value_msec = UINT_MAX;
else
  value_msec = value->it_value.tv_sec * 1000;

value_msec += value->it_value.tv_usec / 1000;

/*
 * Convert the reset interval to milliseconds
 */

if (value->it_interval.tv_sec > (UINT_MAX / 1000))
  interval_msec = UINT_MAX;
else
  interval_msec = value->it_interval.tv_sec * 1000;

interval_msec += value->it_interval.tv_usec / 1000;

if (value_msec > 0 && interval_msec > 0) {
  /*
   * We'll be starting an interval timer that will be repeating, so we need to
   * insert our own SIGALRM signal handler to schedule the repeats.
   */

  /* Read the current SIGALRM action */

  if (sigaction(SIGALRM, NULL, &setitimer_mpe_ctx.oldact) < 0) {
    fprintf(stderr,"MPE setitimer old handler failed, errno=%d\n",errno);
    return -1;
  }

  /* Initialize the new action to call our SIGALRM handler instead */

  newact.sa_handler = &setitimer_mpe_handler;
  newact.sa_mask = setitimer_mpe_ctx.oldact.sa_mask;
  newact.sa_flags = setitimer_mpe_ctx.oldact.sa_flags;
 
  if (sigaction(SIGALRM, &newact, NULL) < 0) {
    fprintf(stderr,"MPE setitimer new handler failed, errno=%d\n",errno);
    return -1;
  }
}

/*
 * Return previous itimerval if desired
 */

if (ovalue != NULL) *ovalue = setitimer_mpe_ctx.current_itimerval;

/*
 * Save current parameters for later usage
 */

setitimer_mpe_ctx.current_itimerval = *value;
setitimer_mpe_ctx.current_msec = value_msec;
setitimer_mpe_ctx.value_msec = value_msec;
setitimer_mpe_ctx.interval_msec = interval_msec;

/*
 * Schedule the first alarm
 */

remaining_msec = px_alarm(value_msec, &alarm_hpe_status);
if (alarm_hpe_status == 0)
  return (0);
else
  return (-1);
}

/* 
 * MPE lacks gettimeofday(), so we define our own.
 */

int gettimeofday(struct timeval *tvp)

{
/* Documented, supported MPE functions. */
extern void GETPRIVMODE(void);
extern void GETUSERMODE(void);

/* Undocumented, unsupported MPE functions. */
extern long long get_time(void);
extern void get_time_change_info(long long *, char *, char *);
extern long long ticks_to_micro(long long);

char pwf_since_boot, recover_pwf_time;
long long mpetime, offset_ticks, offset_usec;

GETPRIVMODE();
mpetime = get_time(); /* MPE local time usecs since Jan 1 1970 */
get_time_change_info(&offset_ticks, &pwf_since_boot, &recover_pwf_time);
offset_usec = ticks_to_micro(offset_ticks);  /* UTC offset usecs */
GETUSERMODE();

mpetime = mpetime - offset_usec;  /* Convert from local time to UTC */
tvp->tv_sec = mpetime / 1000000LL;
tvp->tv_usec = mpetime % 1000000LL;

return 0;
}

/* 
 * MPE lacks settimeofday(), so we define our own.
 */

#define HAVE_SETTIMEOFDAY

int settimeofday(struct timeval *tvp)

{
/* Documented, supported MPE functions. */
extern void GETPRIVMODE(void);
extern void GETUSERMODE(void);

/* Undocumented, unsupported MPE functions. */
extern void get_time_change_info(long long *, char *, char *);
extern void initialize_system_time(long long, int);
extern void set_time_correction(long long, int, int);
extern long long ticks_to_micro(long long);

char pwf_since_boot, recover_pwf_time;
long long big_sec, big_usec, mpetime, offset_ticks, offset_usec;

big_sec = tvp->tv_sec;
big_usec = tvp->tv_usec;
mpetime = (big_sec * 1000000LL) + big_usec;  /* Desired UTC microseconds */

GETPRIVMODE();
set_time_correction(0LL,0,0); /* Cancel previous time correction, if any */
get_time_change_info(&offset_ticks, &pwf_since_boot, &recover_pwf_time);
offset_usec = ticks_to_micro(offset_ticks); /* UTC offset microseconds */
mpetime = mpetime + offset_usec; /* Convert from UTC to local time */
initialize_system_time(mpetime,1);
GETUSERMODE();

return 0;
}
#endif /* MPE */

#define SET_TOD_UNDETERMINED	0
#define SET_TOD_CLOCK_SETTIME	1
#define SET_TOD_SETTIMEOFDAY	2
#define SET_TOD_STIME		3

const char * const set_tod_used[] = {
	"undetermined",
	"clock_settime",
	"settimeofday",
	"stime"
};

pset_tod_using	set_tod_using = NULL;


int
ntp_set_tod(
	struct timeval *tvp,
	void *tzp
	)
{
	static int	tod;
	int		rc;
	int		saved_errno;

	TRACE(1, ("In ntp_set_tod\n"));
	rc = -1;
	saved_errno = 0;

#ifdef HAVE_CLOCK_SETTIME
	if (rc && (SET_TOD_CLOCK_SETTIME == tod || !tod)) {
		struct timespec ts;

		/* Convert timeval to timespec */
		ts.tv_sec = tvp->tv_sec;
		ts.tv_nsec = 1000 *  tvp->tv_usec;

		errno = 0;
		rc = clock_settime(CLOCK_REALTIME, &ts);
		saved_errno = errno;
		TRACE(1, ("ntp_set_tod: clock_settime: %d %m\n", rc));
		if (!tod && !rc)
			tod = SET_TOD_CLOCK_SETTIME;

	}
#endif /* HAVE_CLOCK_SETTIME */
#ifdef HAVE_SETTIMEOFDAY
	if (rc && (SET_TOD_SETTIMEOFDAY == tod || !tod)) {
		struct timeval adjtv;

		/*
		 * Some broken systems don't reset adjtime() when the
		 * clock is stepped.
		 */
		adjtv.tv_sec = adjtv.tv_usec = 0;
		adjtime(&adjtv, NULL);
		errno = 0;
		rc = SETTIMEOFDAY(tvp, tzp);
		saved_errno = errno;
		TRACE(1, ("ntp_set_tod: settimeofday: %d %m\n", rc));
		if (!tod && !rc)
			tod = SET_TOD_SETTIMEOFDAY;
	}
#endif /* HAVE_SETTIMEOFDAY */
#ifdef HAVE_STIME
	if (rc && (SET_TOD_STIME == tod || !tod)) {
		long tp = tvp->tv_sec;

		errno = 0;
		rc = stime(&tp); /* lie as bad as SysVR4 */
		saved_errno = errno;
		TRACE(1, ("ntp_set_tod: stime: %d %m\n", rc));
		if (!tod && !rc)
			tod = SET_TOD_STIME;
	}
#endif /* HAVE_STIME */

	errno = saved_errno;	/* for %m below */
	TRACE(1, ("ntp_set_tod: Final result: %s: %d %m\n",
		  set_tod_used[tod], rc));
	/*
	 * Say how we're setting the time of day
	 */
	if (!rc && NULL != set_tod_using) {
		(*set_tod_using)(set_tod_used[tod]);
		set_tod_using = NULL;
	}

	if (rc)
		errno = saved_errno;

	return rc;
}

#endif /* not SYS_WINNT */

#if defined (SYS_WINNT) || defined (SYS_VXWORKS) || defined(MPE)
/* getpass is used in ntpq.c and ntpdc.c */

char *
getpass(const char * prompt)
{
	int c, i;
	static char password[32];

	fprintf(stderr, "%s", prompt);
	fflush(stderr);

	for (i=0; i<sizeof(password)-1 && ((c=_getch())!='\n' && c!='\r'); i++) {
		password[i] = (char) c;
	}
	password[i] = '\0';

	fputc('\n', stderr);
	fflush(stderr);

	return password;
}
#endif /* SYS_WINNT */
