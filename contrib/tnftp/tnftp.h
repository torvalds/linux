/*	$NetBSD: tnftp.h,v 1.33 2009/11/14 08:32:42 lukem Exp $	*/

#define	FTP_PRODUCT	PACKAGE_NAME
#define	FTP_VERSION	PACKAGE_VERSION

#include "tnftp_config.h"

#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#if defined(HAVE_SYS_TYPES_H)
# include <sys/types.h>
#endif
#if defined(STDC_HEADERS)
# include <stdarg.h>
# include <stdlib.h>
# include <string.h>
#endif
#if defined(HAVE_LIBGEN_H)
# include <libgen.h>
#endif
#if defined(HAVE_UNISTD_H)
# include <unistd.h>
#endif
#if defined(HAVE_POLL_H)
# include <poll.h>
#elif defined(HAVE_SYS_POLL_H)
# include <sys/poll.h>
#endif
#if defined(HAVE_SYS_SOCKET_H)
# include <sys/socket.h>
#endif
#if defined(HAVE_NETINET_IN_H)
# include <netinet/in.h>
#endif
#if defined(HAVE_NETINET_IN_SYSTM_H)
# include <netinet/in_systm.h>
#endif
#if defined(HAVE_NETINET_IP_H)
# include <netinet/ip.h>
#endif
#if defined(HAVE_NETDB_H)
# if HAVE_DECL_AI_NUMERICHOST
#  include <netdb.h>
# else	/* !HAVE_DECL_AI_NUMERICHOST */
#  define getaddrinfo non_rfc2553_getaddrinfo
#  include <netdb.h>
#  undef getaddrinfo
# endif	/* !HAVE_DECL_AI_NUMERICHOST */
#endif
#if defined(HAVE_ARPA_INET_H)
# include <arpa/inet.h>
#endif
#if defined(HAVE_DIRENT_H)
# include <dirent.h>
#else
# define dirent direct
# if defined(HAVE_SYS_NDIR_H)
#  include <sys/ndir.h>
# endif
# if defined(HAVE_SYS_DIR_H)
#  include <sys/dir.h>
# endif
# if defined(HAVE_NDIR_H)
#  include <ndir.h>
# endif
#endif

#if defined(HAVE_SYS_IOCTL_H)
# include <sys/ioctl.h>
#endif
#if defined(HAVE_SYS_PARAM_H)
# include <sys/param.h>
#endif
#if defined(HAVE_SYS_STAT_H)
# include <sys/stat.h>
#endif
#if defined(HAVE_SYS_SYSLIMITS_H)
# include <sys/syslimits.h>
#endif
#if defined(HAVE_SYS_WAIT_H)
# include <sys/wait.h>
#endif

#if defined(HAVE_ARPA_FTP_H)
# include <arpa/ftp.h>
#endif

#if defined(HAVE_FCNTL_H)
# include <fcntl.h>
#endif
#if defined(HAVE_LIMITS_H)
# include <limits.h>
#endif
#if defined(HAVE_PWD_H)
# include <pwd.h>
#endif
#if defined(HAVE_SETJMP_H)
# include <setjmp.h>
#endif
#if defined(HAVE_SIGNAL_H)
# include <signal.h>
#endif
#if defined(HAVE_STDDEF_H)
# include <stddef.h>
#endif
#if defined(HAVE_TERMIOS_H)
# include <termios.h>
#endif

#if defined(HAVE_POLL)
/* we use poll */
#elif defined(HAVE_SELECT)
/* we use select */
#else /* !defined(HAVE_POLL) && !defined(HAVE_SELECT) */
# error "no poll() or select() found"
#endif
#if !defined(POLLIN)
# define POLLIN		0x0001
#endif
#if !defined(POLLOUT)
# define POLLOUT	0x0004
#endif
#if !defined(POLLRDNORM)
# define POLLRDNORM	0x0040
#endif
#if !defined(POLLWRNORM)
# define POLLWRNORM	POLLOUT
#endif
#if !defined(POLLRDBAND)
# define POLLRDBAND	0x0080
#endif
#if !defined(INFTIM)
# define INFTIM -1
#endif
#if !defined(HAVE_STRUCT_POLLFD)
struct pollfd {
	int	fd;
	short	events;
	short	revents;
};
#endif

#if defined(TIME_WITH_SYS_TIME)
# include <sys/time.h>
# include <time.h>
#else
# if defined(HAVE_SYS_TIME_H)
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if defined(HAVE_ERR_H)
# include <err.h>
#endif

#if defined(USE_GLOB_H)	/* not set by configure; used by other build systems */
# include <glob.h>
#else
# include "ftpglob.h"
#endif

#if defined(HAVE_PATHS_H)
# include <paths.h>
#endif
#if !defined(_PATH_BSHELL)
# define _PATH_BSHELL	"/bin/sh"
#endif
#if !defined(_PATH_TMP)
# define _PATH_TMP	"/tmp/"
#endif

typedef struct _stringlist {
	char	**sl_str;
	size_t	  sl_max;
	size_t	  sl_cur;
} StringList;

StringList *sl_init(void);
int	 sl_add(StringList *, char *);
void	 sl_free(StringList *, int);
char	*sl_find(StringList *, char *);

#if defined(HAVE_TERMCAP_H)
# include <termcap.h>
#else
int	 tgetent(char *, const char *);
char	*tgetstr(const char *, char **);
int	 tgetflag(const char *);
int	 tgetnum(const char *);
char	*tgoto(const char *, int, int);
void	 tputs(const char *, int, int (*)(int));
#endif /* !HAVE_TERMCAP_H */

#if defined(HAVE_VIS_H) && defined(HAVE_STRVIS) && defined(HAVE_STRUNVIS)
# include <vis.h>
#else
# include "ftpvis.h"
#endif

#if !defined(HAVE_IN_PORT_T)
typedef unsigned short in_port_t;
#endif

#if !defined(HAVE_SA_FAMILY_T)
typedef unsigned short sa_family_t;
#endif

#if !defined(HAVE_SOCKLEN_T)
typedef unsigned int socklen_t;
#endif

#if defined(USE_INET6)
# define INET6
#endif

#if !HAVE_DECL_AI_NUMERICHOST

				/* RFC 2553 */
#undef	EAI_ADDRFAMILY
#define	EAI_ADDRFAMILY	 1	/* address family for hostname not supported */
#undef	EAI_AGAIN
#define	EAI_AGAIN	 2	/* temporary failure in name resolution */
#undef	EAI_BADFLAGS
#define	EAI_BADFLAGS	 3	/* invalid value for ai_flags */
#undef	EAI_FAIL
#define	EAI_FAIL	 4	/* non-recoverable failure in name resolution */
#undef	EAI_FAMILY
#define	EAI_FAMILY	 5	/* ai_family not supported */
#undef	EAI_MEMORY
#define	EAI_MEMORY	 6	/* memory allocation failure */
#undef	EAI_NODATA
#define	EAI_NODATA	 7	/* no address associated with hostname */
#undef	EAI_NONAME
#define	EAI_NONAME	 8	/* hostname nor servname provided, or not known */
#undef	EAI_SERVICE
#define	EAI_SERVICE	 9	/* servname not supported for ai_socktype */
#undef	EAI_SOCKTYPE
#define	EAI_SOCKTYPE	10	/* ai_socktype not supported */
#undef	EAI_SYSTEM
#define	EAI_SYSTEM	11	/* system error returned in errno */

				/* KAME extensions? */
#undef	EAI_BADHINTS
#define	EAI_BADHINTS	12
#undef	EAI_PROTOCOL
#define	EAI_PROTOCOL	13
#undef	EAI_MAX
#define	EAI_MAX		14

				/* RFC 2553 */
#undef	NI_MAXHOST
#define	NI_MAXHOST	1025
#undef	NI_MAXSERV
#define	NI_MAXSERV	32

#undef	NI_NOFQDN
#define	NI_NOFQDN	0x00000001
#undef	NI_NUMERICHOST
#define	NI_NUMERICHOST	0x00000002
#undef	NI_NAMEREQD
#define	NI_NAMEREQD	0x00000004
#undef	NI_NUMERICSERV
#define	NI_NUMERICSERV	0x00000008
#undef	NI_DGRAM
#define	NI_DGRAM	0x00000010

				/* RFC 2553 */
#undef	AI_PASSIVE
#define	AI_PASSIVE	0x00000001 /* get address to use bind() */
#undef	AI_CANONNAME
#define	AI_CANONNAME	0x00000002 /* fill ai_canonname */

				/* KAME extensions ? */
#undef	AI_NUMERICHOST
#define	AI_NUMERICHOST	0x00000004 /* prevent name resolution */
#undef	AI_MASK
#define	AI_MASK		(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST)

				/* RFC 2553 */
#undef	AI_ALL
#define	AI_ALL		0x00000100 /* IPv6 and IPv4-mapped (with AI_V4MAPPED) */
#undef	AI_V4MAPPED_CFG
#define	AI_V4MAPPED_CFG	0x00000200 /* accept IPv4-mapped if kernel supports */
#undef	AI_ADDRCONFIG
#define	AI_ADDRCONFIG	0x00000400 /* only if any address is assigned */
#undef	AI_V4MAPPED
#define	AI_V4MAPPED	0x00000800 /* accept IPv4-mapped IPv6 address */

#endif /* !HAVE_DECL_AI_NUMERICHOST */


#if !HAVE_DECL_AI_NUMERICHOST && !defined(HAVE_STRUCT_ADDRINFO) \
    && !defined(USE_SOCKS)

struct addrinfo {
	int		ai_flags;	/* AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST */
	int		ai_family;	/* PF_xxx */
	int		ai_socktype;	/* SOCK_xxx */
	int		ai_protocol;	/* 0 or IPPROTO_xxx for IPv4 and IPv6 */
	socklen_t	ai_addrlen;	/* length of ai_addr */
	char		*ai_canonname;	/* canonical name for hostname */
	struct sockaddr *ai_addr;	/* binary address */
	struct addrinfo *ai_next;	/* next structure in linked list */
};

int	getaddrinfo(const char *, const char *,
	    const struct addrinfo *, struct addrinfo **);
int	getnameinfo(const struct sockaddr *, socklen_t,
	    char *, size_t, char *, size_t, int);
void	freeaddrinfo(struct addrinfo *);
const char *gai_strerror(int);

#endif /* !HAVE_DECL_AI_NUMERICHOST && !defined(HAVE_STRUCT_ADDRINFO) \
	&& !defined(USE_SOCKS) */

#if !defined(HAVE_STRUCT_DIRENT_D_NAMLEN)
# define DIRENT_MISSING_D_NAMLEN
#endif

#if !HAVE_DECL_H_ERRNO
extern int	h_errno;
#endif
#define HAVE_H_ERRNO	1		/* XXX: an assumption for now... */

#if !HAVE_DECL_FCLOSE
int	fclose(FILE *);
#endif

#if !HAVE_DECL_GETPASS
char	*getpass(const char *);
#endif

#if !HAVE_DECL_OPTARG
extern char    *optarg;
#endif

#if !HAVE_DECL_OPTIND
extern int	optind;
#endif

#if !HAVE_DECL_PCLOSE
int	pclose(FILE *);
#endif

#if !HAVE_DECL_DIRNAME
char	*dirname(char *);
#endif

#if !defined(HAVE_ERR)
void	err(int, const char *, ...);
void	errx(int, const char *, ...);
void	warn(const char *, ...);
void	warnx(const char *, ...);
#endif

#if !defined(HAVE_FGETLN)
char   *fgetln(FILE *, size_t *);
#endif

#if !defined(HAVE_FSEEKO)
int	fseeko(FILE *, off_t, int);
#endif

#if !defined(HAVE_INET_NTOP)
const char *inet_ntop(int, const void *, char *, socklen_t);
#endif

#if !defined(HAVE_INET_PTON)
int inet_pton(int, const char *, void *);
#endif

#if !defined(HAVE_MKSTEMP)
int	mkstemp(char *);
#endif

#if !defined(HAVE_SETPROGNAME)
const char *getprogname(void);
void	setprogname(const char *);
#endif

#if !defined(HAVE_SNPRINTF)
int	snprintf(char *, size_t, const char *, ...);
#endif

#if !defined(HAVE_STRDUP)
char   *strdup(const char *);
#endif

#if !defined(HAVE_STRERROR)
char   *strerror(int);
#endif

#if !defined(HAVE_STRPTIME) || !HAVE_DECL_STRPTIME
char   *strptime(const char *, const char *, struct tm *);
#endif

#if defined(HAVE_PRINTF_LONG_LONG) && defined(HAVE_LONG_LONG_INT)
# if !defined(HAVE_STRTOLL)
long long strtoll(const char *, char **, int);
# endif
# if !defined(LLONG_MIN)
#  define LLONG_MIN	(-0x7fffffffffffffffLL-1)
# endif
# if !defined(LLONG_MAX)
#  define LLONG_MAX	(0x7fffffffffffffffLL)
# endif
#else  /* !(defined(HAVE_PRINTF_LONG_LONG) && defined(HAVE_LONG_LONG_INT)) */
# define NO_LONG_LONG	1
#endif /* !(defined(HAVE_PRINTF_LONG_LONG) && defined(HAVE_LONG_LONG_INT)) */

#if !defined(HAVE_TIMEGM)
time_t	timegm(struct tm *);
#endif

#if !defined(HAVE_STRLCAT)
size_t	strlcat(char *, const char *, size_t);
#endif

#if !defined(HAVE_STRLCPY)
size_t	strlcpy(char *, const char *, size_t);
#endif

#if !defined(HAVE_STRSEP)
char   *strsep(char **stringp, const char *delim);
#endif

#if !defined(HAVE_UTIMES)
int utimes(const char *, const struct timeval *);
#endif

#if !defined(HAVE_MEMMOVE)
# define memmove(a,b,c)	bcopy((b),(a),(c))
	/* XXX: add others #defines for borken systems? */
#endif

#if defined(HAVE_GETPASSPHRASE)
# define getpass getpassphrase
#endif

#if !defined(MIN)
# define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif
#if !defined(MAX)
# define MAX(a, b)	((a) < (b) ? (b) : (a))
#endif

#if !defined(timersub)
# define timersub(tvp, uvp, vvp)					\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_usec += 1000000;			\
		}							\
	} while (0)
#endif

#if !defined(S_ISLNK)
# define S_ISLNK(m)	((m & S_IFMT) == S_IFLNK)
#endif

#define	EPOCH_YEAR	1970
#define	SECSPERHOUR	3600
#define	SECSPERDAY	86400
#define	TM_YEAR_BASE	1900

#if defined(USE_SOCKS)		/* (Dante) SOCKS5 */
#define connect		Rconnect
#define bind		Rbind
#define getsockname	Rgetsockname
#define getpeername	Rgetpeername
#define accept		Raccept
#define rresvport	Rrresvport
#define bindresvport	Rbindresvport
#define gethostbyname	Rgethostbyname
#define gethostbyname2	Rgethostbyname2
#define sendto		Rsendto
#define recvfrom	Rrecvfrom
#define recvfrom	Rrecvfrom
#define write		Rwrite
#define writev		Rwritev
#define send		Rsend
#define sendmsg		Rsendmsg
#define read		Rread
#define readv		Rreadv
#define recv		Rrecv
#define recvmsg		Rrecvmsg
#define getaddrinfo	Rgetaddrinfo
#define getipnodebyname	Rgetipnodebyname
#endif /* defined(USE_SOCKS) */
