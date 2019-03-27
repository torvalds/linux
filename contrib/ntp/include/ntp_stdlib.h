/*
 * ntp_stdlib.h - Prototypes for NTP lib.
 */
#ifndef NTP_STDLIB_H
#define NTP_STDLIB_H

#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include "declcond.h"	/* ntpd uses ntpd/declcond.h, others include/ */
#include "l_stdlib.h"
#include "ntp_net.h"
#include "ntp_debug.h"
#include "ntp_malloc.h"
#include "ntp_string.h"
#include "ntp_syslog.h"
#include "ntp_keyacc.h"

#ifdef __GNUC__
#define NTP_PRINTF(fmt, args) __attribute__((__format__(__printf__, fmt, args)))
#else
#define NTP_PRINTF(fmt, args)
#endif

extern	int	mprintf(const char *, ...) NTP_PRINTF(1, 2);
extern	int	mfprintf(FILE *, const char *, ...) NTP_PRINTF(2, 3);
extern	int	mvfprintf(FILE *, const char *, va_list) NTP_PRINTF(2, 0);
extern	int	mvsnprintf(char *, size_t, const char *, va_list)
			NTP_PRINTF(3, 0);
extern	int	msnprintf(char *, size_t, const char *, ...)
			NTP_PRINTF(3, 4);
extern	void	msyslog(int, const char *, ...) NTP_PRINTF(2, 3);
extern	void	mvsyslog(int, const char *, va_list) NTP_PRINTF(2, 0);
extern	void	init_logging	(const char *, u_int32, int);
extern	int	change_logfile	(const char *, int);
extern	void	setup_logfile	(const char *);
#ifndef errno_to_str
extern	void	errno_to_str(int, char *, size_t);
#endif

extern	int	xvsbprintf(char**, char* const, char const*, va_list) NTP_PRINTF(3, 0);
extern	int	xsbprintf(char**, char* const, char const*, ...) NTP_PRINTF(3, 4);

/*
 * When building without OpenSSL, use a few macros of theirs to
 * minimize source differences in NTP.
 */
#ifndef OPENSSL
#define NID_md5	4	/* from openssl/objects.h */
/* from openssl/evp.h */
#define EVP_MAX_MD_SIZE	64	/* longest known is SHA512 */
#endif

#define SAVE_ERRNO(stmt)				\
	{						\
		int preserved_errno;			\
							\
		preserved_errno = socket_errno();	\
		{					\
			stmt				\
		}					\
		errno = preserved_errno;		\
	}

typedef void (*ctrl_c_fn)(void);

/* authkeys.c */
extern	void	auth_delkeys	(void);
extern	int	auth_havekey	(keyid_t);
extern	int	authdecrypt	(keyid_t, u_int32 *, size_t, size_t);
extern	size_t	authencrypt	(keyid_t, u_int32 *, size_t);
extern	int	authhavekey	(keyid_t);
extern	int	authistrusted	(keyid_t);
extern	int	authistrustedip	(keyid_t, sockaddr_u *);
extern	int	authreadkeys	(const char *);
extern	void	authtrust	(keyid_t, u_long);
extern	int	authusekey	(keyid_t, int, const u_char *);

/*
 * Based on the NTP timestamp, calculate the NTP timestamp of
 * the corresponding calendar unit. Use the pivot time to unfold
 * the NTP timestamp properly, or the current system time if the
 * pivot pointer is NULL.
 */
extern	u_int32	calyearstart	(u_int32 ntptime, const time_t *pivot);
extern	u_int32	calmonthstart	(u_int32 ntptime, const time_t *pivot);
extern	u_int32	calweekstart	(u_int32 ntptime, const time_t *pivot);
extern	u_int32	caldaystart	(u_int32 ntptime, const time_t *pivot);

extern	const char *clockname	(int);
extern	int	clocktime	(int, int, int, int, int, u_int32, u_long *, u_int32 *);
extern	int	ntp_getopt	(int, char **, const char *);
extern	void	init_auth	(void);
extern	void	init_lib	(void);
extern	struct savekey *auth_findkey (keyid_t);
extern	void	auth_moremem	(int);
extern	void	auth_prealloc_symkeys(int);
extern	int	ymd2yd		(int, int, int);

/* a_md5encrypt.c */
extern	int	MD5authdecrypt	(int, const u_char *, size_t, u_int32 *, size_t, size_t);
extern	size_t	MD5authencrypt	(int, const u_char *, size_t, u_int32 *, size_t);
extern	void	MD5auth_setkey	(keyid_t, int, const u_char *, size_t, KeyAccT *c);
extern	u_int32	addr2refid	(sockaddr_u *);

/* emalloc.c */
#ifndef EREALLOC_CALLSITE	/* ntp_malloc.h defines */
extern	void *	ereallocz	(void *, size_t, size_t, int);
extern	void *	oreallocarrayxz	(void *optr, size_t nmemb, size_t size, size_t extra);
#define	erealloczsite(p, n, o, z, f, l) ereallocz((p), (n), (o), (z))
#define	emalloc(n)		ereallocz(NULL, (n), 0, FALSE)
#define	emalloc_zero(c)		ereallocz(NULL, (c), 0, TRUE)
#define	erealloc(p, c)		ereallocz((p), (c), 0, FALSE)
#define erealloc_zero(p, n, o)	ereallocz((p), (n), (o), TRUE)
#define ereallocarray(p, n, s)	oreallocarrayxz((p), (n), (s), 0)
#define eallocarray(n, s)	oreallocarrayxz(NULL, (n), (s), 0)
#define ereallocarrayxz(p, n, s, x)	oreallocarrayxz((p), (n), (s), (x))
#define eallocarrayxz(n, s, x)	oreallocarrayxz(NULL, (n), (s), (x))
extern	char *	estrdup_impl(const char *);
#define	estrdup(s)		estrdup_impl(s)
#else
extern	void *	ereallocz	(void *, size_t, size_t, int,
				 const char *, int);
extern	void *	oreallocarrayxz	(void *optr, size_t nmemb, size_t size,
				 size_t extra, const char *, int);
#define erealloczsite		ereallocz
#define	emalloc(c)		ereallocz(NULL, (c), 0, FALSE, \
					  __FILE__, __LINE__)
#define	emalloc_zero(c)		ereallocz(NULL, (c), 0, TRUE, \
					  __FILE__, __LINE__)
#define	erealloc(p, c)		ereallocz((p), (c), 0, FALSE, \
					  __FILE__, __LINE__)
#define	erealloc_zero(p, n, o)	ereallocz((p), (n), (o), TRUE, \
					  __FILE__, __LINE__)
#define ereallocarray(p, n, s)	oreallocarrayxz((p), (n), (s), 0, \
					  __FILE__, __LINE__)
#define eallocarray(n, s)	oreallocarrayxz(NULL, (n), (s), 0, \
					  __FILE__, __LINE__)
#define ereallocarrayxz(p, n, s, x)	oreallocarrayxz((p), (n), (s), (x), \
					  __FILE__, __LINE__)
#define eallocarrayxz(n, s, x)	oreallocarrayxz(NULL, (n), (s), (x), \
					  __FILE__, __LINE__)
extern	char *	estrdup_impl(const char *, const char *, int);
#define	estrdup(s) estrdup_impl((s), __FILE__, __LINE__)
#endif


extern	int	atoint		(const char *, long *);
extern	int	atouint		(const char *, u_long *);
extern	int	hextoint	(const char *, u_long *);
extern	const char *	humanlogtime	(void);
extern	const char *	humantime	(time_t);
extern int	is_ip_address	(const char *, u_short, sockaddr_u *);
extern	char *	mfptoa		(u_int32, u_int32, short);
extern	char *	mfptoms		(u_int32, u_int32, short);
extern	const char * modetoa	(size_t);
extern	const char * eventstr	(int);
extern	const char * ceventstr	(int);
extern	const char * res_match_flags(u_short);
extern	const char * res_access_flags(u_short);
#ifdef KERNEL_PLL
extern	const char * k_st_flags	(u_int32);
#endif
extern	char *	statustoa	(int, int);
extern	sockaddr_u * netof	(sockaddr_u *);
extern	char *	numtoa		(u_int32);
extern	char *	numtohost	(u_int32);
extern	const char * socktoa	(const sockaddr_u *);
extern	const char * sockporttoa(const sockaddr_u *);
extern	u_short	sock_hash	(const sockaddr_u *);
extern	int	sockaddr_masktoprefixlen(const sockaddr_u *);
extern	const char * socktohost	(const sockaddr_u *);
extern	int	octtoint	(const char *, u_long *);
extern	u_long	ranp2		(int);
extern	const char *refnumtoa	(sockaddr_u *);
extern	const char *refid_str	(u_int32, int);

extern	int	decodenetnum	(const char *, sockaddr_u *);

extern	const char * FindConfig	(const char *);

extern	void	signal_no_reset (int, RETSIGTYPE (*func)(int));
extern	void	set_ctrl_c_hook (ctrl_c_fn);

extern	void	getauthkeys 	(const char *);
extern	void	auth_agekeys	(void);
extern	void	rereadkeys	(void);

/*
 * Variable declarations for libntp.
 */

/* authkeys.c */
extern u_long	authkeynotfound;	/* keys not found */
extern u_long	authkeylookups;		/* calls to lookup keys */
extern u_long	authnumkeys;		/* number of active keys */
extern u_long	authkeyexpired;		/* key lifetime expirations */
extern u_long	authkeyuncached;	/* cache misses */
extern u_long	authencryptions;	/* calls to encrypt */
extern u_long	authdecryptions;	/* calls to decrypt */

extern int	authnumfreekeys;

/*
 * The key cache. We cache the last key we looked at here.
 */
extern keyid_t	cache_keyid;		/* key identifier */
extern int	cache_type;		/* key type */
extern u_char *	cache_secret;		/* secret */
extern size_t	cache_secretsize;	/* secret octets */
extern u_short	cache_flags;		/* KEY_ bit flags */

/* getopt.c */
extern char *	ntp_optarg;		/* global argument pointer */
extern int	ntp_optind;		/* global argv index */

/* lib_strbuf.c */
extern int	ipv4_works;
extern int	ipv6_works;

/* machines.c */
typedef void (*pset_tod_using)(const char *);
extern pset_tod_using	set_tod_using;

/* ssl_init.c */
#ifdef OPENSSL
extern	void	ssl_init		(void);
extern	void	ssl_check_version	(void);
extern	int	ssl_init_done;
#define	INIT_SSL()				\
	do {					\
		if (!ssl_init_done)		\
			ssl_init();		\
	} while (0)
#else	/* !OPENSSL follows */
#define	INIT_SSL()		do {} while (0)
#endif
extern	int	keytype_from_text	(const char *,	size_t *);
extern	const char *keytype_name	(int);
extern	char *	getpass_keytype		(int);

/* strl-obsd.c */
#ifndef HAVE_STRLCPY		/* + */
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
extern	size_t	strlcpy(char *dst, const char *src, size_t siz);
#endif
#ifndef HAVE_STRLCAT		/* + */
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
extern	size_t	strlcat(char *dst, const char *src, size_t siz);
#endif



/* lib/isc/win32/strerror.c
 *
 * To minimize Windows-specific changes to the rest of the NTP code,
 * particularly reference clocks, we hijack calls to strerror() to deal
 * with our mixture of error codes from the  C runtime (open, write)
 * and Windows (sockets, serial ports).  This is an ugly hack because
 * both use the lowest values differently, but particularly for ntpd,
 * it's not a problem.
 */
#ifdef NTP_REDEFINE_STRERROR
#define	strerror(e)	ntp_strerror(e)
extern char *	ntp_strerror	(int e);
#endif

/* systime.c */
extern double	sys_tick;		/* tick size or time to read */
extern double	measured_tick;		/* non-overridable sys_tick */
extern double	sys_fuzz;		/* min clock read latency */
extern int	trunc_os_clock;		/* sys_tick > measured_tick */

/* version.c */
extern const char *Version;		/* version declaration */

#endif	/* NTP_STDLIB_H */
