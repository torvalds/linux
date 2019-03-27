/*
 * ntp_machine.h
 *
 * Collect all machine dependent idiosyncrasies in one place.
 *
 * The functionality formerly in this file is mostly handled by
 * Autoconf these days.
 */

#ifndef NTP_MACHINE_H
#define NTP_MACHINE_H

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <time.h>

#include "ntp_proto.h"

/*

			 HEY!  CHECK THIS OUT!

  The per-system SYS_* #defins ARE NO LONGER USED, with the temporary
  exception of SYS_WINNT.

  If you find a hunk of code that is bracketed by a SYS_* macro and you
  *know* that it is still needed, please let us know.  In many cases the
  code fragment is now handled somewhere else by autoconf choices.

*/

/*

HOW TO GET IP INTERFACE INFORMATION

  Some UNIX V.4 machines implement a sockets library on top of
  streams. For these systems, you must use send the SIOCGIFCONF down
  the stream in an I_STR ioctl. This ususally also implies
  USE_STREAMS_DEVICE FOR IF_CONFIG. Dell UNIX is a notable exception.

WHAT DOES IOCTL(SIOCGIFCONF) RETURN IN THE BUFFER

  UNIX V.4 machines implement a sockets library on top of streams.
  When requesting the IP interface configuration with an ioctl(2) calll,
  an array of ifreq structures are placed in the provided buffer.  Some
  implementations also place the length of the buffer information in
  the first integer position of the buffer.

  SIZE_RETURNED_IN_BUFFER - size integer is in the buffer

WILL IOCTL(SIOCGIFCONF) WORK ON A SOCKET

  Some UNIX V.4 machines do not appear to support ioctl() requests for the
  IP interface configuration on a socket.  They appear to require the use
  of the streams device instead.

  USE_STREAMS_DEVICE_FOR_IF_CONFIG - use the /dev/ip device for configuration

MISC

  DOSYNCTODR		- Resync TODR clock  every hour.
  RETSIGTYPE		- Define signal function type.
  NO_SIGNED_CHAR_DECL - No "signed char" see include/ntp.h
  LOCK_PROCESS		- Have plock.
*/

int ntp_set_tod (struct timeval *tvp, void *tzp);

/*casey Tue May 27 15:45:25 SAT 1997*/
#ifdef SYS_VXWORKS

/* casey's new defines */
#define NO_MAIN_ALLOWED 	1
#define NO_NETDB			1
#define NO_RENAME			1

/* in vxWorks we use FIONBIO, but the others are defined for old systems, so
 * all hell breaks loose if we leave them defined we define USE_FIONBIO to
 * undefine O_NONBLOCK FNDELAY O_NDELAY where necessary.
 */
#define USE_FIONBIO 		1
/* end my new defines */

#define TIMEOFDAY		0x0 	/* system wide realtime clock */
#define HAVE_GETCLOCK		1	/* configure does not set this ... */
#define HAVE_NO_NICE		1	/* configure does not set this ... */
#define HAVE_RANDOM		1	/* configure does not set this ...  */
#define HAVE_SRANDOM		1	/* configure does not set this ... */

/* vxWorks specific additions to take care of its
 * unix (non)complicance
 */

#include "vxWorks.h"
#include "ioLib.h"
#include "taskLib.h"
#include "time.h"

extern int sysClkRateGet ();

/* usrtime.h
 * Bob Herlien's excellent time code find it at:
 * ftp://ftp.atd.ucar.edu/pub/vxworks/vx/usrTime.shar
 * I would recommend this instead of clock_[g|s]ettime() plus you get
 * adjtime() too ... casey
 */
/*
extern int	  gettimeofday ( struct timeval *tp, struct timezone *tzp );
extern int	  settimeofday (struct timeval *, struct timezone *);
extern int	  adjtime ( struct timeval *delta, struct timeval *olddelta );
 */

/* in  machines.c */
extern void sleep (int seconds);
extern void alarm (int seconds);
/* machines.c */


/*		this is really this 	*/
#define getpid		taskIdSelf
#define getclock	clock_gettime
#define fcntl		ioctl
#define _getch		getchar

/* define this away for vxWorks */
#define openlog(x,y)
/* use local defines for these */
#undef min
#undef max

#endif /* SYS_VXWORKS */

#ifdef NO_NETDB
/* These structures are needed for gethostbyname() etc... */
/* structures used by netdb.h */
struct	hostent {
	char	*h_name;				/* official name of host */
	char	**h_aliases;			/* alias list */
	int h_addrtype; 				/* host address type */
	int h_length;					/* length of address */
	char	**h_addr_list;			/* list of addresses from name server */
#define 	h_addr h_addr_list[0]	/* address, for backward compatibility */
};

struct	servent {
	char	*s_name;				/* official service name */
	char	**s_aliases;			/* alias list */
	int s_port; 					/* port # */
	char	*s_proto;				/* protocol to use */
};
extern int h_errno;

#define TRY_AGAIN	2

struct hostent *gethostbyname (char * netnum);
struct hostent *gethostbyaddr (char * netnum, int size, int addr_type);
/* type is the protocol */
struct servent *getservbyname (char *name, char *type);
#endif	/* NO_NETDB */

#ifdef NO_MAIN_ALLOWED
/* we have no main routines so lets make a plan */
#define CALL(callname, progname, callmain) \
	extern int callmain (int,char**); \
	void callname (a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) \
		char *a0;  \
		char *a1;  \
		char *a2;  \
		char *a3;  \
		char *a4;  \
		char *a5;  \
		char *a6;  \
		char *a7;  \
		char *a8;  \
		char *a9;  \
		char *a10; \
	{ \
	  char *x[11]; \
	  int argc; \
	  char *argv[] = {progname,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL}; \
	  int i; \
	  for (i=0;i<11;i++) \
	   x[i] = NULL; \
	  x[0] = a0; \
	  x[1] = a1; \
	  x[2] = a2; \
	  x[3] = a3; \
	  x[4] = a4; \
	  x[5] = a5; \
	  x[6] = a6; \
	  x[7] = a7; \
	  x[8] = a8; \
	  x[9] = a9; \
	  x[10] = a10; \
	  argc=1; \
	  for (i=0; i<11;i++) \
		if (x[i]) \
		{ \
		  argv[argc++] = x[i];	\
		} \
	 callmain(argc,argv);  \
	}
#endif /* NO_MAIN_ALLOWED */
/*casey Tue May 27 15:45:25 SAT 1997*/

/*
 * Here's where autoconfig starts to take over
 */
#ifdef HAVE_SYS_STROPTS_H
# ifdef HAVE_SYS_STREAM_H
#  define STREAM
# endif
#endif

#ifndef RETSIGTYPE
# if defined(NTP_POSIX_SOURCE)
#  define	RETSIGTYPE	void
# else
#  define	RETSIGTYPE	int
# endif
#endif

#ifdef	NTP_SYSCALLS_STD
# ifndef	NTP_SYSCALL_GET
#  define	NTP_SYSCALL_GET 235
# endif
# ifndef	NTP_SYSCALL_ADJ
#  define	NTP_SYSCALL_ADJ 236
# endif
#endif	/* NTP_SYSCALLS_STD */

#ifdef MPE
# include <sys/types.h>
# include <netinet/in.h>
# include <stdio.h>
# include <time.h>

/* missing functions that are easily renamed */

# define _getch getchar

/* special functions that require MPE-specific wrappers */

# define bind	__ntp_mpe_bind
# define fcntl	__ntp_mpe_fcntl

/* standard macros missing from MPE include files */

# define IN_CLASSD(i)	((((long)(i))&0xf0000000)==0xe0000000)
# define IN_MULTICAST IN_CLASSD
# define ITIMER_REAL 0

/* standard structures missing from MPE include files */

struct itimerval { 
        struct timeval it_interval;    /* timer interval */
        struct timeval it_value;       /* current value */
};

/* various declarations to make gcc stop complaining */

extern int __filbuf(FILE *);
extern int __flsbuf(int, FILE *);
extern int gethostname(char *, int);
extern unsigned long inet_addr(char *);
extern char *strdup(const char *);

/* miscellaneous NTP macros */

# define HAVE_NO_NICE
#endif /* MPE */

#ifdef HAVE_RTPRIO
# define HAVE_NO_NICE
#else
# ifdef HAVE_SETPRIORITY
#  define HAVE_BSD_NICE
# else
#  ifdef HAVE_NICE
#	define HAVE_ATT_NICE
#  endif
# endif
#endif

#if !defined(HAVE_ATT_NICE) \
	&& !defined(HAVE_BSD_NICE) \
	&& !defined(HAVE_NO_NICE)
#include "ERROR: You must define one of the HAVE_xx_NICE defines!"
#endif

#ifndef HAVE_TIMEGM
extern time_t	timegm		(struct tm *);
#endif


#endif	/* NTP_MACHINE_H */
