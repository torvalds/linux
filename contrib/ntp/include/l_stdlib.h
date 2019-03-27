/*
 * Proto types for machines that are not ANSI and POSIX	 compliant.
 * This is optional
 */

#ifndef L_STDLIB_H
#define L_STDLIB_H

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#include <stdarg.h>
#include <sys/types.h>

/* Needed for speed_t. */
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif

#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif

#include "ntp_types.h"
#include "ntp_proto.h"

/* Let's try to keep this more or less alphabetized... */

#ifdef DECL_ADJTIME_0
struct timeval;
extern	int	adjtime		(struct timeval *, struct timeval *);
#endif

#ifdef DECL_BCOPY_0
#ifndef bcopy
extern	void	bcopy		(const char *, char *, int);
#endif
#endif

#ifdef DECL_BZERO_0
#ifndef bzero
extern	void	bzero		(char *, int);
#endif
#endif

#ifdef DECL_CFSETISPEED_0
struct termios;
extern	int	cfsetispeed	(struct termios *, speed_t);
extern	int	cfsetospeed	(struct termios *, speed_t);
#endif

extern	char *	getpass		(const char *);

#ifdef DECL_HSTRERROR_0
extern	const char * hstrerror	(int);
#endif

#ifdef DECL_INET_NTOA_0
struct in_addr;
extern	char *	inet_ntoa	(struct in_addr);
#endif

#ifdef DECL_IOCTL_0
extern	int	ioctl		(int, u_long, char *);
#endif

#ifdef DECL_IPC_0
struct sockaddr;
extern	int	bind		(int, struct sockaddr *, int);
extern	int	connect		(int, struct sockaddr *, int);
extern	int	recv		(int, char *, int, int);
extern	int	recvfrom	(int, char *, int, int, struct sockaddr *, int *);
extern	int	send		(int, char *, int, int);
extern	int	sendto		(int, char *, int, int, struct sockaddr *, int);
extern	int	setsockopt	(int, int, int, char *, int);
extern	int	socket		(int, int, int);
#endif

#ifdef DECL_MEMMOVE_0
extern	void *	memmove		(void *, const void *, size_t);
#endif

#ifdef DECL_MEMSET_0
extern	char *	memset		(char *, int, int);
#endif

#ifdef DECL_MKSTEMP_0
extern	int	mkstemp		(char *);
#endif

#ifdef DECL_MKTEMP_0
extern	char   *mktemp		(char *);	
#endif

#ifdef DECL_NLIST_0
struct nlist;
extern int	nlist		(const char *, struct nlist *);
#endif

#ifdef DECL_PLOCK_0
extern	int	plock		(int);
#endif

#ifdef DECL_RENAME_0
extern	int	rename		(const char *, const char *);
#endif

#ifdef DECL_SELECT_0
#ifdef NTP_SELECT_H
extern	int	select		(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif
#endif

#ifdef DECL_SETITIMER_0
struct itimerval;
extern	int	setitimer	(int , struct itimerval *, struct itimerval *);
#endif

#ifdef PRIO_PROCESS
#ifdef DECL_SETPRIORITY_0
extern	int	setpriority	(int, int, int);
#endif
#ifdef DECL_SETPRIORITY_1
extern	int	setpriority	(int, id_t, int);
#endif
#endif

#ifdef DECL_SIGVEC_0
struct sigvec;
extern	int	sigvec		(int, struct sigvec *, struct sigvec *);
#endif

#ifdef DECL_STDIO_0
#if defined(FILE) || defined(BUFSIZ)
extern	int	_flsbuf		(int, FILE *);
extern	int	_filbuf		(FILE *);
extern	int	fclose		(FILE *);
extern	int	fflush		(FILE *);
extern	int	fprintf		(FILE *, const char *, ...);
extern	int	fscanf		(FILE *, const char *, ...);
extern	int	fputs		(const char *, FILE *);
extern	int	fputc		(int, FILE *);
extern	int	fread		(char *, int, int, FILE *);
extern	void	perror		(const char *);
extern	int	printf		(const char *, ...);
extern	int	setbuf		(FILE *, char *);
# ifdef HAVE_SETLINEBUF
extern	int	setlinebuf	(FILE *);
# endif
extern	int	setvbuf		(FILE *, char *, int, int);
extern	int	scanf		(const char *, ...);
extern	int	sscanf		(const char *, const char *, ...);
extern	int	vfprintf	(FILE *, const char *, ...);
extern	int	vsprintf	(char *, const char *, ...);
#endif
#endif

#ifdef DECL_STIME_0
extern	int	stime		(const time_t *);
#endif

#ifdef DECL_STIME_1
extern	int	stime		(long *);
#endif

#ifdef DECL_STRERROR_0
extern	char *	strerror		(int errnum);
#endif

#ifdef DECL_STRTOL_0
extern	long	strtol		(const char *, char **, int);
#endif

#ifdef DECL_SYSCALL
extern	int	syscall		(int, ...);
#endif

#ifdef DECL_SYSLOG_0
extern	void	closelog	(void);
#ifndef LOG_DAEMON
extern	void	openlog		(const char *, int);
#else
extern	void	openlog		(const char *, int, int);
#endif
extern	int	setlogmask	(int);
extern	void	syslog		(int, const char *, ...);
#endif

#ifdef DECL_TIME_0
extern	time_t	time		(time_t *);
#endif

#ifdef DECL_TIMEOFDAY_0
#ifdef SYSV_TIMEOFDAY
extern	int	gettimeofday	(struct timeval *);
extern	int	settimeofday	(struct timeval *);
#else /* not SYSV_TIMEOFDAY */
struct timezone;
extern	int	gettimeofday	(struct timeval *, struct timezone *);
extern	int	settimeofday	(struct timeval *, void *);
#endif /* not SYSV_TIMEOFDAY */
#endif

#ifdef DECL_TOLOWER_0
extern	int	tolower		(int);
#endif

#ifdef DECL_TOUPPER_0
extern	int	toupper		(int);
#endif

/*
 * Necessary variable declarations.
 */
#ifdef DECL_ERRNO
extern	int	errno;
#endif

#if defined(DECL_H_ERRNO) && !defined(h_errno)
extern	int	h_errno;
#endif

#endif	/* L_STDLIB_H */
