/*
 * Copyright (c) 1998-2013 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

/*
**  SENDMAIL.H -- MTA-specific definitions for sendmail.
*/

#ifndef _SENDMAIL_H
# define _SENDMAIL_H 1

#ifndef MILTER
# define MILTER	1	/* turn on MILTER by default */
#endif /* MILTER */

#ifdef _DEFINE
# define EXTERN
#else /* _DEFINE */
# define EXTERN extern
#endif /* _DEFINE */


#include <unistd.h>

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <setjmp.h>
#include <string.h>
#include <time.h>
# ifdef EX_OK
#  undef EX_OK			/* for SVr4.2 SMP */
# endif /* EX_OK */

#include "sendmail/sendmail.h"

/* profiling? */
#if MONCONTROL
# define SM_PROF(x)	moncontrol(x)
#else /* MONCONTROL */
# define SM_PROF(x)
#endif /* MONCONTROL */

#ifdef _DEFINE
# ifndef lint
SM_UNUSED(static char SmailId[]) = "@(#)$Id: sendmail.h,v 8.1104 2013-11-22 20:51:56 ca Exp $";
# endif /* ! lint */
#endif /* _DEFINE */

#include "bf.h"
#include "timers.h"
#include <sm/exc.h>
#include <sm/heap.h>
#include <sm/debug.h>
#include <sm/rpool.h>
#include <sm/io.h>
#include <sm/path.h>
#include <sm/signal.h>
#include <sm/clock.h>
#include <sm/mbdb.h>
#include <sm/errstring.h>
#include <sm/sysexits.h>
#include <sm/shm.h>
#include <sm/misc.h>

#ifdef LOG
# include <syslog.h>
#endif /* LOG */



# if NETINET || NETINET6 || NETUNIX || NETISO || NETNS || NETX25
#  include <sys/socket.h>
# endif /* NETINET || NETINET6 || NETUNIX || NETISO || NETNS || NETX25 */
# if NETUNIX
#  include <sys/un.h>
# endif /* NETUNIX */
# if NETINET || NETINET6
#  include <netinet/in.h>
# endif /* NETINET || NETINET6 */
# if NETINET6
/*
**  There is no standard yet for IPv6 includes.
**  Specify OS specific implementation in conf.h
*/
# endif /* NETINET6 */
# if NETISO
#  include <netiso/iso.h>
# endif /* NETISO */
# if NETNS
#  include <netns/ns.h>
# endif /* NETNS */
# if NETX25
#  include <netccitt/x25.h>
# endif /* NETX25 */

# if NAMED_BIND
#  include <arpa/nameser.h>
#  ifdef NOERROR
#   undef NOERROR		/* avoid <sys/streams.h> conflict */
#  endif /* NOERROR */
#  include <resolv.h>
# else /* NAMED_BIND */
#   undef SM_SET_H_ERRNO
#   define SM_SET_H_ERRNO(err)
# endif /* NAMED_BIND */

# if HESIOD
#  include <hesiod.h>
#  if !defined(HES_ER_OK) || defined(HESIOD_INTERFACES)
#   define HESIOD_INIT		/* support for the new interface */
#  endif /* !defined(HES_ER_OK) || defined(HESIOD_INTERFACES) */
# endif /* HESIOD */

#if STARTTLS
# include <openssl/ssl.h>
# if !TLS_NO_RSA
#  if _FFR_FIPSMODE
#   define RSA_KEYLENGTH	1024
#  else /* _FFR_FIPSMODE  */
#   define RSA_KEYLENGTH	512
#  endif /* _FFR_FIPSMODE  */
# endif /* !TLS_NO_RSA */
#endif /* STARTTLS */

#if SASL  /* include the sasl include files if we have them */


# if SASL == 2 || SASL >= 20000
#  include <sasl/sasl.h>
#  include <sasl/saslplug.h>
#  include <sasl/saslutil.h>
#  if SASL_VERSION_FULL < 0x020119
typedef int (*sasl_callback_ft)(void);
#  endif /* SASL_VERSION_FULL < 0x020119 */
# else /* SASL == 2 || SASL >= 20000 */
#  include <sasl.h>
#  include <saslutil.h>
typedef int (*sasl_callback_ft)(void);
# endif /* SASL == 2 || SASL >= 20000 */
# if defined(SASL_VERSION_MAJOR) && defined(SASL_VERSION_MINOR) && defined(SASL_VERSION_STEP)
#  define SASL_VERSION (SASL_VERSION_MAJOR * 10000)  + (SASL_VERSION_MINOR * 100) + SASL_VERSION_STEP
#  if SASL == 1 || SASL == 2
#   undef SASL
#   define SASL SASL_VERSION
#  else /* SASL == 1 || SASL == 2 */
#   if SASL != SASL_VERSION
  ERROR README: -DSASL (SASL) does not agree with the version of the CYRUS_SASL library (SASL_VERSION)
  ERROR README: see README!
#   endif /* SASL != SASL_VERSION */
#  endif /* SASL == 1 || SASL == 2 */
# else /* defined(SASL_VERSION_MAJOR) && defined(SASL_VERSION_MINOR) && defined(SASL_VERSION_STEP) */
#  if SASL == 1
  ERROR README: please set -DSASL to the version of the CYRUS_SASL library
  ERROR README: see README!
#  endif /* SASL == 1 */
# endif /* defined(SASL_VERSION_MAJOR) && defined(SASL_VERSION_MINOR) && defined(SASL_VERSION_STEP) */
#endif /* SASL */

/*
**  Following are "sort of" configuration constants, but they should
**  be pretty solid on most architectures today.  They have to be
**  defined after <arpa/nameser.h> because some versions of that
**  file also define them.  In all cases, we can't use sizeof because
**  some systems (e.g., Crays) always treat everything as being at
**  least 64 bits.
*/

#ifndef INADDRSZ
# define INADDRSZ	4		/* size of an IPv4 address in bytes */
#endif /* ! INADDRSZ */
#ifndef IN6ADDRSZ
# define IN6ADDRSZ	16		/* size of an IPv6 address in bytes */
#endif /* ! IN6ADDRSZ */
#ifndef INT16SZ
# define INT16SZ	2		/* size of a 16 bit integer in bytes */
#endif /* ! INT16SZ */
#ifndef INT32SZ
# define INT32SZ	4		/* size of a 32 bit integer in bytes */
#endif /* ! INT32SZ */
#ifndef INADDR_LOOPBACK
# define INADDR_LOOPBACK	0x7f000001	/* loopback address */
#endif /* ! INADDR_LOOPBACK */

/*
**  Error return from inet_addr(3), in case not defined in /usr/include.
*/

#ifndef INADDR_NONE
# define INADDR_NONE	0xffffffff
#endif /* ! INADDR_NONE */

/* By default use uncompressed IPv6 address format (no "::") */
#ifndef IPV6_FULL
# define IPV6_FULL	1
#endif

/* (f)open() modes for queue files */
#define QF_O_EXTRA	0

#if _FFR_PROXY || _FFR_LOGREPLY
# define _FFR_ERRCODE	1
#endif

#define SM_ARRAY_SIZE(array)   (sizeof(array) / sizeof((array)[0]))

/*
**  An 'argument class' describes the storage allocation status
**  of an object pointed to by an argument to a function.
*/

typedef enum
{
	A_HEAP,	/* the storage was allocated by malloc, and the
		 * ownership of the storage is ceded by the caller
		 * to the called function. */
	A_TEMP, /* The storage is temporary, and is only guaranteed
		 * to be valid for the duration of the function call. */
	A_PERM	/* The storage is 'permanent': this might mean static
		 * storage, or rpool storage. */
} ARGCLASS_T;

/* forward references for prototypes */
typedef struct envelope	ENVELOPE;
typedef struct mailer	MAILER;
typedef struct queuegrp	QUEUEGRP;

/*
**  Address structure.
**	Addresses are stored internally in this structure.
*/

struct address
{
	char		*q_paddr;	/* the printname for the address */
	char		*q_user;	/* user name */
	char		*q_ruser;	/* real user name, or NULL if q_user */
	char		*q_host;	/* host name */
	struct mailer	*q_mailer;	/* mailer to use */
	unsigned long	q_flags;	/* status flags, see below */
	uid_t		q_uid;		/* user-id of receiver (if known) */
	gid_t		q_gid;		/* group-id of receiver (if known) */
	char		*q_home;	/* home dir (local mailer only) */
	char		*q_fullname;	/* full name if known */
	struct address	*q_next;	/* chain */
	struct address	*q_alias;	/* address this results from */
	char		*q_owner;	/* owner of q_alias */
	struct address	*q_tchain;	/* temporary use chain */
#if PIPELINING
	struct address	*q_pchain;	/* chain for pipelining */
#endif /* PIPELINING */
	char		*q_finalrcpt;	/* Final-Recipient: DSN header */
	char		*q_orcpt;	/* ORCPT parameter from RCPT TO: line */
	char		*q_status;	/* status code for DSNs */
	char		*q_rstatus;	/* remote status message for DSNs */
	time_t		q_statdate;	/* date of status messages */
	char		*q_statmta;	/* MTA generating q_rstatus */
	short		q_state;	/* address state, see below */
	char		*q_signature;	/* MX-based sorting value */
	int		q_qgrp;		/* index into queue groups */
	int		q_qdir;		/* queue directory inside group */
	char		*q_message;	/* error message */
};

typedef struct address ADDRESS;

/* bit values for q_flags */
#define QGOODUID	0x00000001	/* the q_uid q_gid fields are good */
#define QPRIMARY	0x00000002	/* set from RCPT or argv */
#define QNOTREMOTE	0x00000004	/* address not for remote forwarding */
#define QSELFREF	0x00000008	/* this address references itself */
#define QBOGUSSHELL	0x00000010	/* user has no valid shell listed */
#define QUNSAFEADDR	0x00000020	/* address acquired via unsafe path */
#define QPINGONSUCCESS	0x00000040	/* give return on successful delivery */
#define QPINGONFAILURE	0x00000080	/* give return on failure */
#define QPINGONDELAY	0x00000100	/* give return on message delay */
#define QHASNOTIFY	0x00000200	/* propagate notify parameter */
#define QRELAYED	0x00000400	/* DSN: relayed to non-DSN aware sys */
#define QEXPANDED	0x00000800	/* DSN: undergone list expansion */
#define QDELIVERED	0x00001000	/* DSN: successful final delivery */
#define QDELAYED	0x00002000	/* DSN: message delayed */
#define QALIAS		0x00004000	/* expanded alias */
#define QBYTRACE	0x00008000	/* DeliverBy: trace */
#define QBYNDELAY	0x00010000	/* DeliverBy: notify, delay */
#define QBYNRELAY	0x00020000	/* DeliverBy: notify, relayed */
#define QINTBCC		0x00040000	/* internal Bcc */
#define QDYNMAILER	0x00080000	/* "dynamic mailer" */
#define QTHISPASS	0x40000000	/* temp: address set this pass */
#define QRCPTOK		0x80000000	/* recipient() processed address */

#define QDYNMAILFLG	'Y'

#define Q_PINGFLAGS	(QPINGONSUCCESS|QPINGONFAILURE|QPINGONDELAY)

#if _FFR_RCPTFLAGS
# define QMATCHFLAGS (QINTBCC|QDYNMAILER)
# define QMATCH_FLAG(a) ((a)->q_flags & QMATCHFLAGS)
# define ADDR_FLAGS_MATCH(a, b)	(QMATCH_FLAG(a) == QMATCH_FLAG(b))
#else
# define ADDR_FLAGS_MATCH(a, b)	true
#endif

/* values for q_state */
#define QS_OK		0		/* address ok (for now)/not yet tried */
#define QS_SENT		1		/* good address, delivery complete */
#define QS_BADADDR	2		/* illegal address */
#define QS_QUEUEUP	3		/* save address in queue */
#define QS_RETRY	4		/* retry delivery for next MX */
#define QS_VERIFIED	5		/* verified, but not expanded */

/*
**  Notice: all of the following values are variations of QS_DONTSEND.
**	If new states are added, they must be inserted in the proper place!
**	See the macro definition of QS_IS_DEAD() down below.
*/

#define QS_DONTSEND	6		/* don't send to this address */
#define QS_EXPANDED	7		/* expanded */
#define QS_SENDER	8		/* message sender (MeToo) */
#define QS_CLONED	9		/* addr cloned to split envelope */
#define QS_DISCARDED	10		/* rcpt discarded (EF_DISCARD) */
#define QS_REPLACED	11		/* maplocaluser()/UserDB replaced */
#define QS_REMOVED	12		/* removed (removefromlist()) */
#define QS_DUPLICATE	13		/* duplicate suppressed */
#define QS_INCLUDED	14		/* :include: delivery */
#define QS_FATALERR	15		/* fatal error, don't deliver */

/* address state testing primitives */
#define QS_IS_OK(s)		((s) == QS_OK)
#define QS_IS_SENT(s)		((s) == QS_SENT)
#define QS_IS_BADADDR(s)	((s) == QS_BADADDR)
#define QS_IS_QUEUEUP(s)	((s) == QS_QUEUEUP)
#define QS_IS_RETRY(s)		((s) == QS_RETRY)
#define QS_IS_VERIFIED(s)	((s) == QS_VERIFIED)
#define QS_IS_EXPANDED(s)	((s) == QS_EXPANDED)
#define QS_IS_REMOVED(s)	((s) == QS_REMOVED)
#define QS_IS_UNDELIVERED(s)	((s) == QS_OK || \
				 (s) == QS_QUEUEUP || \
				 (s) == QS_RETRY || \
				 (s) == QS_VERIFIED)
#define QS_IS_UNMARKED(s)	((s) == QS_OK || \
				 (s) == QS_RETRY)
#define QS_IS_SENDABLE(s)	((s) == QS_OK || \
				 (s) == QS_QUEUEUP || \
				 (s) == QS_RETRY)
#define QS_IS_ATTEMPTED(s)	((s) == QS_QUEUEUP || \
				 (s) == QS_RETRY || \
				 (s) == QS_SENT || \
				 (s) == QS_DISCARDED)
#define QS_IS_DEAD(s)		((s) >= QS_DONTSEND)
#define QS_IS_TEMPFAIL(s)	((s) == QS_QUEUEUP || (s) == QS_RETRY)

#define NULLADDR	((ADDRESS *) NULL)

extern ADDRESS	NullAddress;	/* a null (template) address [main.c] */

/* for cataddr() */
#define NOSPACESEP	256

/* functions */
extern void	cataddr __P((char **, char **, char *, int, int, bool));
extern char	*crackaddr __P((char *, ENVELOPE *));
extern bool	emptyaddr __P((ADDRESS *));
extern ADDRESS	*getctladdr __P((ADDRESS *));
extern int	include __P((char *, bool, ADDRESS *, ADDRESS **, int, ENVELOPE *));
extern bool	invalidaddr __P((char *, char *, bool));
extern ADDRESS	*parseaddr __P((char *, ADDRESS *, int, int, char **,
				ENVELOPE *, bool));
extern char	**prescan __P((char *, int, char[], int, char **, unsigned char *, bool));
extern void	printaddr __P((SM_FILE_T *, ADDRESS *, bool));
extern ADDRESS	*recipient __P((ADDRESS *, ADDRESS **, int, ENVELOPE *));
extern char	*remotename __P((char *, MAILER *, int, int *, ENVELOPE *));
extern int	rewrite __P((char **, int, int, ENVELOPE *, int));
extern bool	sameaddr __P((ADDRESS *, ADDRESS *));
extern int	sendtolist __P((char *, ADDRESS *, ADDRESS **, int, ENVELOPE *));
#if MILTER
extern int	removefromlist __P((char *, ADDRESS **, ENVELOPE *));
#endif /* MILTER */
extern void	setsender __P((char *, ENVELOPE *, char **, int, bool));
typedef void esmtp_args_F __P((ADDRESS *, char *, char *, ENVELOPE *));
extern void	parse_esmtp_args __P((ENVELOPE *, ADDRESS *, char *, char *,
			char *, char *args[], esmtp_args_F));
extern esmtp_args_F mail_esmtp_args;
extern esmtp_args_F rcpt_esmtp_args;
extern void	reset_mail_esmtp_args __P((ENVELOPE *));

/* macro to simplify the common call to rewrite() */
#define REWRITE(pvp, rs, env)	rewrite(pvp, rs, 0, env, MAXATOM)

/*
**  Token Tables for prescan
*/

extern unsigned char	ExtTokenTab[256];	/* external strings */
extern unsigned char	IntTokenTab[256];	/* internal strings */


/*
**  Mailer definition structure.
**	Every mailer known to the system is declared in this
**	structure.  It defines the pathname of the mailer, some
**	flags associated with it, and the argument vector to
**	pass to it.  The flags are defined in conf.c
**
**	The argument vector is expanded before actual use.  All
**	words except the first are passed through the macro
**	processor.
*/

struct mailer
{
	char	*m_name;	/* symbolic name of this mailer */
	char	*m_mailer;	/* pathname of the mailer to use */
	char	*m_mtatype;	/* type of this MTA */
	char	*m_addrtype;	/* type for addresses */
	char	*m_diagtype;	/* type for diagnostics */
	BITMAP256 m_flags;	/* status flags, see below */
	short	m_mno;		/* mailer number internally */
	short	m_nice;		/* niceness to run at (mostly for prog) */
	char	**m_argv;	/* template argument vector */
	short	m_sh_rwset;	/* rewrite set: sender header addresses */
	short	m_se_rwset;	/* rewrite set: sender envelope addresses */
	short	m_rh_rwset;	/* rewrite set: recipient header addresses */
	short	m_re_rwset;	/* rewrite set: recipient envelope addresses */
	char	*m_eol;		/* end of line string */
	long	m_maxsize;	/* size limit on message to this mailer */
	int	m_linelimit;	/* max # characters per line */
	int	m_maxdeliveries; /* max deliveries per mailer connection */
	char	*m_execdir;	/* directory to chdir to before execv */
	char	*m_rootdir;	/* directory to chroot to before execv */
	uid_t	m_uid;		/* UID to run as */
	gid_t	m_gid;		/* GID to run as */
	char	*m_defcharset;	/* default character set */
	time_t	m_wait;		/* timeout to wait for end */
	int	m_maxrcpt;	/* max recipients per envelope client-side */
	short	m_qgrp;		/* queue group for this mailer */
};

/* bits for m_flags */
#define M_xSMTP		0x01	/* internal: {ES,S,L}MTP */
#define M_ESMTP		'a'	/* run Extended SMTP */
#define M_ALIASABLE	'A'	/* user can be LHS of an alias */
#define M_BLANKEND	'b'	/* ensure blank line at end of message */
#define M_STRIPBACKSL	'B'	/* strip all leading backslashes from user */
#define M_NOCOMMENT	'c'	/* don't include comment part of address */
#define M_CANONICAL	'C'	/* make addresses canonical "u@dom" */
#define M_NOBRACKET	'd'	/* never angle bracket envelope route-addrs */
		/*	'D'	   CF: include Date: */
#define M_EXPENSIVE	'e'	/* it costs to use this mailer.... */
#define M_ESCFROM	'E'	/* escape From lines to >From */
#define M_FOPT		'f'	/* mailer takes picky -f flag */
		/*	'F'	   CF: include From: or Resent-From: */
#define M_NO_NULL_FROM	'g'	/* sender of errors should be $g */
#define M_HST_UPPER	'h'	/* preserve host case distinction */
#define M_PREHEAD	'H'	/* MAIL11V3: preview headers */
#define M_UDBENVELOPE	'i'	/* do udbsender rewriting on envelope */
#define M_INTERNAL	'I'	/* SMTP to another sendmail site */
#define M_UDBRECIPIENT	'j'	/* do udbsender rewriting on recipient lines */
#define M_NOLOOPCHECK	'k'	/* don't check for loops in HELO command */
#define M_CHUNKING	'K'	/* CHUNKING: reserved for future use */
#define M_LOCALMAILER	'l'	/* delivery is to this host */
#define M_LIMITS	'L'	/* must enforce SMTP line limits */
#define M_MUSER		'm'	/* can handle multiple users at once */
		/*	'M'	   CF: include Message-Id: */
#define M_NHDR		'n'	/* don't insert From line */
#define M_MANYSTATUS	'N'	/* MAIL11V3: DATA returns multi-status */
#define M_RUNASRCPT	'o'	/* always run mailer as recipient */
		/*	'O'	   free? */
#define M_FROMPATH	'p'	/* use reverse-path in MAIL FROM: */
		/*	'P'	   CF: include Return-Path: */
#define M_VRFY250	'q'	/* VRFY command returns 250 instead of 252 */
#define M_ROPT		'r'	/* mailer takes picky -r flag */
#define M_SECURE_PORT	'R'	/* try to send on a reserved TCP port */
#define M_STRIPQ	's'	/* strip quote chars from user/host */
#define M_SPECIFIC_UID	'S'	/* run as specific uid/gid */
#define M_USR_UPPER	'u'	/* preserve user case distinction */
#define M_UGLYUUCP	'U'	/* this wants an ugly UUCP from line */
#define M_CONTENT_LEN	'v'	/* add Content-Length: header (SVr4) */
		/*	'V'	   UIUC: !-relativize all addresses */
#define M_HASPWENT	'w'	/* check for /etc/passwd entry */
#define M_NOHOSTSTAT	'W'	/* ignore long term host status information */
		/*	'x'	   CF: include Full-Name: */
#define M_XDOT		'X'	/* use hidden-dot algorithm */
		/*	'y'	   free? */
		/*	'Y'	   free? */
#define M_LMTP		'z'	/* run Local Mail Transport Protocol */
#define M_DIALDELAY	'Z'	/* apply dial delay sleeptime */
#define M_NOMX		'0'	/* turn off MX lookups */
#define M_NONULLS	'1'	/* don't send null bytes */
#define M_FSMTP		'2'	/* force SMTP (no ESMTP even if offered) */
		/*	'4'	   free? */
#define M_EBCDIC	'3'	/* extend Q-P encoding for EBCDIC */
#define M_TRYRULESET5	'5'	/* use ruleset 5 after local aliasing */
#define M_7BITHDRS	'6'	/* strip headers to 7 bits even in 8 bit path */
#define M_7BITS		'7'	/* use 7-bit path */
#define M_8BITS		'8'	/* force "just send 8" behaviour */
#define M_MAKE8BIT	'9'	/* convert 7 -> 8 bit if appropriate */
#define M_CHECKINCLUDE	':'	/* check for :include: files */
#define M_CHECKPROG	'|'	/* check for |program addresses */
#define M_CHECKFILE	'/'	/* check for /file addresses */
#define M_CHECKUDB	'@'	/* user can be user database key */
#define M_CHECKHDIR	'~'	/* SGI: check for valid home directory */
#define M_HOLD		'%'	/* Hold delivery until ETRN/-qI/-qR/-qS */
#define M_PLUS		'+'	/* Reserved: Used in mc for adding new flags */
#define M_MINUS		'-'	/* Reserved: Used in mc for removing flags */
#define M_NOMHHACK	'!'	/* Don't perform HM hack dropping explicit from */

/* functions */
extern void	initerrmailers __P((void));
extern void	makemailer __P((char *));
extern void	makequeue __P((char *, bool));
extern void	runqueueevent __P((int));
#if _FFR_QUEUE_RUN_PARANOIA
extern bool	checkqueuerunner __P((void));
#endif /* _FFR_QUEUE_RUN_PARANOIA */

EXTERN MAILER	*FileMailer;	/* ptr to *file* mailer */
EXTERN MAILER	*InclMailer;	/* ptr to *include* mailer */
EXTERN MAILER	*LocalMailer;	/* ptr to local mailer */
EXTERN MAILER	*ProgMailer;	/* ptr to program mailer */
#if _FFR_RCPTFLAGS
EXTERN MAILER	*Mailer[MAXMAILERS * 2 + 1];
#else
EXTERN MAILER	*Mailer[MAXMAILERS + 1];
#endif

/*
**  Queue group definition structure.
**	Every queue group known to the system is declared in this structure.
**	It defines the basic pathname of the queue group, some flags
**	associated with it, and the argument vector to pass to it.
*/

struct qpaths_s
{
	char	*qp_name;	/* name of queue dir, relative path */
	short	qp_subdirs;	/* use subdirs? */
	short	qp_fsysidx;	/* file system index of this directory */
# if SM_CONF_SHM
	int	qp_idx;		/* index into array for queue information */
# endif /* SM_CONF_SHM */
};

typedef struct qpaths_s QPATHS;

struct queuegrp
{
	char	*qg_name;	/* symbolic name of this queue group */

	/*
	**  For now this is the same across all queue groups.
	**  Otherwise we have to play around with chdir().
	*/

	char	*qg_qdir;	/* common component of queue directory */
	short	qg_index;	/* queue number internally, index in Queue[] */
	int	qg_maxqrun;	/* max # of jobs in 1 queuerun */
	int	qg_numqueues;	/* number of queues in this queue */

	/*
	**  qg_queueintvl == 0 denotes that no individual value is used.
	**  Whatever accesses this must deal with "<= 0" as
	**  "not set, use appropriate default".
	*/

	time_t	qg_queueintvl;	/* interval for queue runs */
	QPATHS	*qg_qpaths;	/* list of queue directories */
	BITMAP256 qg_flags;	/* status flags, see below */
	short	qg_nice;	/* niceness for queue run */
	int	qg_wgrp;	/* Assigned to this work group */
	int     qg_maxlist;	/* max items in work queue for this group */
	int     qg_curnum;	/* current number of queue for queue runs */
	int	qg_maxrcpt;	/* max recipients per envelope, 0==no limit */

	time_t	qg_nextrun;	/* time for next queue runs */
#if _FFR_QUEUE_GROUP_SORTORDER
	short	qg_sortorder;	/* how do we sort this queuerun */
#endif /* _FFR_QUEUE_GROUP_SORTORDER */
#if 0
	long	qg_wkrcptfact;	/* multiplier for # recipients -> priority */
	long	qg_qfactor;	/* slope of queue function */
	bool	qg_doqueuerun;	/* XXX flag is it time to do a queuerun */
#endif /* 0 */
};

/* bits for qg_flags (XXX: unused as of now) */
#define QD_DEFINED	((char) 1)	/* queue group has been defined */
#define QD_FORK		'f'	/* fork queue runs */

extern void	filesys_update __P((void));
#if _FFR_ANY_FREE_FS
extern bool	filesys_free __P((long));
#endif /* _FFR_ANY_FREE_FS */

#if SASL
/*
**  SASL
*/

/* lines in authinfo file or index into SASL_AI_T */
# define SASL_WRONG	(-1)
# define SASL_USER	0	/* authorization id (user) */
# define SASL_AUTHID	1	/* authentication id */
# define SASL_PASSWORD	2	/* password fuer authid */
# define SASL_DEFREALM	3	/* realm to use */
# define SASL_MECHLIST	4	/* list of mechanisms to try */
# define SASL_ID_REALM	5	/* authid@defrealm */

/*
**  Current mechanism; this is just used to convey information between
**  invocation of SASL callback functions.
**  It must be last in the list, because it's not allocated by us
**  and hence we don't free() it.
*/
# define SASL_MECH	6
# define SASL_ENTRIES	7	/* number of entries in array */

# define SASL_USER_BIT		(1 << SASL_USER)
# define SASL_AUTHID_BIT	(1 << SASL_AUTHID)
# define SASL_PASSWORD_BIT	(1 << SASL_PASSWORD)
# define SASL_DEFREALM_BIT	(1 << SASL_DEFREALM)
# define SASL_MECHLIST_BIT	(1 << SASL_MECHLIST)

/* authenticated? */
# define SASL_NOT_AUTH	0		/* not authenticated */
# define SASL_PROC_AUTH	1		/* in process of authenticating */
# define SASL_IS_AUTH	2		/* authenticated */

/* SASL options */
# define SASL_AUTH_AUTH	0x1000		/* use auth= only if authenticated */
# if SASL >= 20101
#  define SASL_SEC_MASK	SASL_SEC_MAXIMUM /* mask for SASL_SEC_* values: sasl.h */
# else /* SASL >= 20101 */
#  define SASL_SEC_MASK	0x0fff		/* mask for SASL_SEC_* values: sasl.h */
#  if (SASL_SEC_NOPLAINTEXT & SASL_SEC_MASK) == 0 || \
	(SASL_SEC_NOACTIVE & SASL_SEC_MASK) == 0 || \
	(SASL_SEC_NODICTIONARY & SASL_SEC_MASK) == 0 || \
	(SASL_SEC_FORWARD_SECRECY & SASL_SEC_MASK) == 0 || \
	(SASL_SEC_NOANONYMOUS & SASL_SEC_MASK) == 0 || \
	(SASL_SEC_PASS_CREDENTIALS & SASL_SEC_MASK) == 0
ERROR: change SASL_SEC_MASK_ notify sendmail.org!
#  endif /* SASL_SEC_NOPLAINTEXT & SASL_SEC_MASK) == 0 ... */
# endif /* SASL >= 20101 */
# define MAXOUTLEN 8192	/* length of output buffer, should be 2^n */

/* functions */
extern char	*intersect __P((char *, char *, SM_RPOOL_T *));
extern char	*iteminlist __P((char *, char *, char *));
# if SASL >= 20000
extern int	proxy_policy __P((sasl_conn_t *, void *, const char *, unsigned, const char *, unsigned, const char *, unsigned, struct propctx *));
extern int	safesaslfile __P((void *, const char *, sasl_verify_type_t));
# else /* SASL >= 20000 */
extern int	proxy_policy __P((void *, const char *, const char *, const char **, const char **));
#  if SASL > 10515
extern int	safesaslfile __P((void *, char *, int));
#  else /* SASL > 10515 */
extern int	safesaslfile __P((void *, char *));
#  endif /* SASL > 10515 */
# endif /* SASL >= 20000 */
extern void	stop_sasl_client __P((void));

/* structure to store authinfo */
typedef char *SASL_AI_T[SASL_ENTRIES];

EXTERN char	*AuthMechanisms;	/* AUTH mechanisms */
EXTERN char	*AuthRealm;	/* AUTH realm */
EXTERN char	*SASLInfo;	/* file with AUTH info */
EXTERN int	SASLOpts;	/* options for SASL */
EXTERN int	MaxSLBits;	/* max. encryption bits for SASL */
#endif /* SASL */

/*
**  Structure to store macros.
*/
typedef struct
{
	SM_RPOOL_T	*mac_rpool;		/* resource pool */
	BITMAP256	mac_allocated;		/* storage has been alloc()? */
	char		*mac_table[MAXMACROID + 1];	/* macros */
} MACROS_T;

EXTERN MACROS_T		GlobalMacros;

/*
**  Information about currently open connections to mailers, or to
**  hosts that we have looked up recently.
*/

#define MCI		struct mailer_con_info

MCI
{
	unsigned long	mci_flags;	/* flag bits, see below */
	short		mci_errno;	/* error number on last connection */
	short		mci_herrno;	/* h_errno from last DNS lookup */
	short		mci_exitstat;	/* exit status from last connection */
	short		mci_state;	/* SMTP state */
	int		mci_deliveries;	/* delivery attempts for connection */
	long		mci_maxsize;	/* max size this server will accept */
	SM_FILE_T	*mci_in;	/* input side of connection */
	SM_FILE_T	*mci_out;	/* output side of connection */
	pid_t		mci_pid;	/* process id of subordinate proc */
	char		*mci_phase;	/* SMTP phase string */
	struct mailer	*mci_mailer;	/* ptr to the mailer for this conn */
	char		*mci_host;	/* host name */
	char		*mci_status;	/* DSN status to be copied to addrs */
	char		*mci_rstatus;	/* SMTP status to be copied to addrs */
	time_t		mci_lastuse;	/* last usage time */
	SM_FILE_T	*mci_statfile;	/* long term status file */
	char		*mci_heloname;	/* name to use as HELO arg */
	long		mci_min_by;	/* minimum DELIVERBY */
	bool		mci_retryrcpt;	/* tempfail for at least one rcpt */
	char		*mci_tolist;	/* list of valid recipients */
	SM_RPOOL_T	*mci_rpool;	/* resource pool */
#if PIPELINING
	int		mci_okrcpts;	/* number of valid recipients */
	ADDRESS		*mci_nextaddr;	/* next address for pipelined status */
#endif /* PIPELINING */
#if SASL
	SASL_AI_T	mci_sai;	/* authentication info */
	bool		mci_sasl_auth;	/* authenticated? */
	int		mci_sasl_string_len;
	char		*mci_sasl_string;	/* sasl reply string */
	char		*mci_saslcap;	/* SASL list of mechanisms */
	sasl_conn_t	*mci_conn;	/* SASL connection */
#endif /* SASL */
#if STARTTLS
	SSL		*mci_ssl;	/* SSL connection */
#endif /* STARTTLS */
	MACROS_T	mci_macro;	/* macro definitions */
};


/* flag bits */
#define MCIF_VALID	0x00000001	/* this entry is valid */
/* 0x00000002 unused, was MCIF_TEMP */
#define MCIF_CACHED	0x00000004	/* currently in open cache */
#define MCIF_ESMTP	0x00000008	/* this host speaks ESMTP */
#define MCIF_EXPN	0x00000010	/* EXPN command supported */
#define MCIF_SIZE	0x00000020	/* SIZE option supported */
#define MCIF_8BITMIME	0x00000040	/* BODY=8BITMIME supported */
#define MCIF_7BIT	0x00000080	/* strip this message to 7 bits */
/* 0x00000100 unused, was MCIF_MULTSTAT: MAIL11V3: handles MULT status */
#define MCIF_INHEADER	0x00000200	/* currently outputing header */
#define MCIF_CVT8TO7	0x00000400	/* convert from 8 to 7 bits */
#define MCIF_DSN	0x00000800	/* DSN extension supported */
#define MCIF_8BITOK	0x00001000	/* OK to send 8 bit characters */
#define MCIF_CVT7TO8	0x00002000	/* convert from 7 to 8 bits */
#define MCIF_INMIME	0x00004000	/* currently reading MIME header */
#define MCIF_AUTH	0x00008000	/* AUTH= supported */
#define MCIF_AUTHACT	0x00010000	/* SASL (AUTH) active */
#define MCIF_ENHSTAT	0x00020000	/* ENHANCEDSTATUSCODES supported */
#define MCIF_PIPELINED	0x00040000	/* PIPELINING supported */
#define MCIF_VERB	0x00080000	/* VERB supported */
#if STARTTLS
#define MCIF_TLS	0x00100000	/* STARTTLS supported */
#define MCIF_TLSACT	0x00200000	/* STARTTLS active */
#else /* STARTTLS */
#define MCIF_TLS	0
#define MCIF_TLSACT	0
#endif /* STARTTLS */
#define MCIF_DLVR_BY	0x00400000	/* DELIVERBY */
#if _FFR_IGNORE_EXT_ON_HELO
# define MCIF_HELO	0x00800000	/* we used HELO: ignore extensions */
#endif /* _FFR_IGNORE_EXT_ON_HELO */
#define MCIF_INLONGLINE 0x01000000	/* in the middle of a long line */
#define MCIF_AUTH2	0x02000000	/* got 2 AUTH lines */
#define MCIF_ONLY_EHLO	0x10000000	/* use only EHLO in smtpinit */
#if _FFR_HANDLE_HDR_RW_TEMPFAIL
/* an error is not sticky (if put{header,body}() etc fail) */
# define MCIF_NOTSTICKY	0x20000000
#else
# define MCIF_NOTSTICKY	0
#endif

#define MCIF_EXTENS	(MCIF_EXPN | MCIF_SIZE | MCIF_8BITMIME | MCIF_DSN | MCIF_8BITOK | MCIF_AUTH | MCIF_ENHSTAT | MCIF_TLS | MCIF_AUTH2)

/* states */
#define MCIS_CLOSED	0		/* no traffic on this connection */
#define MCIS_OPENING	1		/* sending initial protocol */
#define MCIS_OPEN	2		/* open, initial protocol sent */
#define MCIS_MAIL	3		/* MAIL command sent */
#define MCIS_RCPT	4		/* RCPT commands being sent */
#define MCIS_DATA	5		/* DATA command sent */
#define MCIS_QUITING	6		/* running quit protocol */
#define MCIS_SSD	7		/* service shutting down */
#define MCIS_ERROR	8		/* I/O error on connection */

/* functions */
extern void	mci_cache __P((MCI *));
extern void	mci_close __P((MCI *, char *where));
extern void	mci_dump __P((SM_FILE_T *, MCI *, bool));
extern void	mci_dump_all __P((SM_FILE_T *, bool));
extern void	mci_flush __P((bool, MCI *));
extern void	mci_clr_extensions __P((MCI *));
extern MCI	*mci_get __P((char *, MAILER *));
extern int	mci_lock_host __P((MCI *));
extern bool	mci_match __P((char *, MAILER *));
extern int	mci_print_persistent __P((char *, char *));
extern int	mci_purge_persistent __P((char *, char *));
extern MCI	**mci_scan __P((MCI *));
extern void	mci_setstat __P((MCI *, int, char *, char *));
extern void	mci_store_persistent __P((MCI *));
extern int	mci_traverse_persistent __P((int (*)(char *, char *), char *));
extern void	mci_unlock_host __P((MCI *));

EXTERN int	MaxMciCache;		/* maximum entries in MCI cache */
EXTERN time_t	MciCacheTimeout;	/* maximum idle time on connections */
EXTERN time_t	MciInfoTimeout;		/* how long 'til we retry down hosts */

/*
**  Header structure.
**	This structure is used internally to store header items.
*/

struct header
{
	char		*h_field;	/* the name of the field */
	char		*h_value;	/* the value of that field */
	struct header	*h_link;	/* the next header */
	unsigned char	h_macro;	/* include header if macro defined */
	unsigned long	h_flags;	/* status bits, see below */
	BITMAP256	h_mflags;	/* m_flags bits needed */
};

typedef struct header	HDR;

/*
**  Header information structure.
**	Defined in conf.c, this struct declares the header fields
**	that have some magic meaning.
*/

struct hdrinfo
{
	char		*hi_field;	/* the name of the field */
	unsigned long	hi_flags;	/* status bits, see below */
	char		*hi_ruleset;	/* validity check ruleset */
};

extern struct hdrinfo	HdrInfo[];

/* bits for h_flags and hi_flags */
#define H_EOH		0x00000001	/* field terminates header */
#define H_RCPT		0x00000002	/* contains recipient addresses */
#define H_DEFAULT	0x00000004	/* if another value is found, drop this */
#define H_RESENT	0x00000008	/* this address is a "Resent-..." address */
#define H_CHECK		0x00000010	/* check h_mflags against m_flags */
#define H_ACHECK	0x00000020	/* ditto, but always (not just default) */
#define H_FORCE		0x00000040	/* force this field, even if default */
#define H_TRACE		0x00000080	/* this field contains trace information */
#define H_FROM		0x00000100	/* this is a from-type field */
#define H_VALID		0x00000200	/* this field has a validated value */
#define H_RECEIPTTO	0x00000400	/* field has return receipt info */
#define H_ERRORSTO	0x00000800	/* field has error address info */
#define H_CTE		0x00001000	/* field is a content-transfer-encoding */
#define H_CTYPE		0x00002000	/* this is a content-type field */
#define H_BCC		0x00004000	/* Bcc: header: strip value or delete */
#define H_ENCODABLE	0x00008000	/* field can be RFC 1522 encoded */
#define H_STRIPCOMM	0x00010000	/* header check: strip comments */
#define H_BINDLATE	0x00020000	/* only expand macros at deliver */
#define H_USER		0x00040000	/* header came from the user/SMTP */

/* bits for chompheader() */
#define CHHDR_DEF	0x0001	/* default header */
#define CHHDR_CHECK	0x0002	/* call ruleset for header */
#define CHHDR_USER	0x0004	/* header from user */
#define CHHDR_QUEUE	0x0008	/* header from queue file */

/* functions */
extern void	addheader __P((char *, char *, int, ENVELOPE *, bool));
extern unsigned long	chompheader __P((char *, int, HDR **, ENVELOPE *));
extern bool	commaize __P((HDR *, char *, bool, MCI *, ENVELOPE *, int));
extern HDR	*copyheader __P((HDR *, SM_RPOOL_T *));
extern void	eatheader __P((ENVELOPE *, bool, bool));
extern char	*hvalue __P((char *, HDR *));
extern void	insheader __P((int, char *, char *, int, ENVELOPE *, bool));
extern bool	isheader __P((char *));
extern bool	putfromline __P((MCI *, ENVELOPE *));
extern void	setupheaders __P((void));

/*
**  Performance monitoring
*/

#define TIMERS		struct sm_timers

TIMERS
{
	TIMER	ti_overall;	/* the whole process */
};


#define PUSHTIMER(l, t)	{ if (tTd(98, l)) pushtimer(&t); }
#define POPTIMER(l, t)	{ if (tTd(98, l)) poptimer(&t); }

/*
**  Envelope structure.
**	This structure defines the message itself.  There is usually
**	only one of these -- for the message that we originally read
**	and which is our primary interest -- but other envelopes can
**	be generated during processing.  For example, error messages
**	will have their own envelope.
*/

struct envelope
{
	HDR		*e_header;	/* head of header list */
	long		e_msgpriority;	/* adjusted priority of this message */
	time_t		e_ctime;	/* time message appeared in the queue */
	char		*e_to;		/* (list of) target person(s) */
	ADDRESS		e_from;		/* the person it is from */
	char		*e_sender;	/* e_from.q_paddr w comments stripped */
	char		**e_fromdomain;	/* the domain part of the sender */
	ADDRESS		*e_sendqueue;	/* list of message recipients */
	ADDRESS		*e_errorqueue;	/* the queue for error responses */

	/*
	**  Overflow detection is based on < 0, so don't change this
	**  to unsigned.  We don't use unsigned and == ULONG_MAX because
	**  some libc's don't have strtoul(), see mail_esmtp_args().
	*/

	long		e_msgsize;	/* size of the message in bytes */
	char		*e_msgid;	/* message id (for logging) */
	unsigned long	e_flags;	/* flags, see below */
	int		e_nrcpts;	/* number of recipients */
	short		e_class;	/* msg class (priority, junk, etc.) */
	short		e_hopcount;	/* number of times processed */
	short		e_nsent;	/* number of sends since checkpoint */
	short		e_sendmode;	/* message send mode */
	short		e_errormode;	/* error return mode */
	short		e_timeoutclass;	/* message timeout class */
	bool		(*e_puthdr)__P((MCI *, HDR *, ENVELOPE *, int));
					/* function to put header of message */
	bool		(*e_putbody)__P((MCI *, ENVELOPE *, char *));
					/* function to put body of message */
	ENVELOPE	*e_parent;	/* the message this one encloses */
	ENVELOPE	*e_sibling;	/* the next envelope of interest */
	char		*e_bodytype;	/* type of message body */
	SM_FILE_T	*e_dfp;		/* data file */
	char		*e_id;		/* code for this entry in queue */
#if _FFR_SESSID
	char		*e_sessid;	/* session ID for this envelope */
#endif /* _FFR_SESSID */
	int		e_qgrp;		/* queue group (index into queues) */
	int		e_qdir;		/* index into queue directories */
	int		e_dfqgrp;	/* data file queue group index */
	int		e_dfqdir;	/* data file queue directory index */
	int		e_xfqgrp;	/* queue group (index into queues) */
	int		e_xfqdir;	/* index into queue directories (xf) */
	SM_FILE_T	*e_xfp;		/* transcript file */
	SM_FILE_T	*e_lockfp;	/* the lock file for this message */
	char		*e_message;	/* error message; readonly; NULL,
					 * or allocated from e_rpool */
	char		*e_statmsg;	/* stat msg (changes per delivery).
					 * readonly. NULL or allocated from
					 * e_rpool. */
	char		*e_quarmsg;	/* why envelope is quarantined */
	char		e_qfletter;	/* queue file letter on disk */
	char		*e_msgboundary;	/* MIME-style message part boundary */
	char		*e_origrcpt;	/* original recipient (one only) */
	char		*e_envid;	/* envelope id from MAIL FROM: line */
	char		*e_status;	/* DSN status for this message */
	time_t		e_dtime;	/* time of last delivery attempt */
	int		e_ntries;	/* number of delivery attempts */
	dev_t		e_dfdev;	/* data file device (crash recovery) */
	ino_t		e_dfino;	/* data file inode (crash recovery) */
	MACROS_T	e_macro;	/* macro definitions */
	MCI		*e_mci;		/* connection info */
	char		*e_auth_param;	/* readonly; NULL or static storage or
					 * allocated from e_rpool */
	TIMERS		e_timers;	/* per job timers */
	long		e_deliver_by;	/* deliver by */
	int		e_dlvr_flag;	/* deliver by flag */
	SM_RPOOL_T	*e_rpool;	/* resource pool for this envelope */
	unsigned int	e_features;	/* server features */
#define ENHSC_LEN	11
#if _FFR_MILTER_ENHSC
	char		e_enhsc[ENHSC_LEN];	/* enhanced status code */
#endif /* _FFR_MILTER_ENHSC */
#if _FFR_ERRCODE
	/* smtp error codes during delivery */
	int		e_rcode;	/* reply code */
	char		e_renhsc[ENHSC_LEN];	/* enhanced status code */
	char		*e_text;	/* reply text */
#endif /* _FFR_ERRCODE */
};

#define PRT_NONNEGL(v)	((v) < 0 ? LONG_MAX : (v))

/* values for e_flags */
#define EF_OLDSTYLE	0x00000001L	/* use spaces (not commas) in hdrs */
#define EF_INQUEUE	0x00000002L	/* this message is fully queued */
#define EF_NO_BODY_RETN	0x00000004L	/* omit message body on error */
#define EF_CLRQUEUE	0x00000008L	/* disk copy is no longer needed */
#define EF_SENDRECEIPT	0x00000010L	/* send a return receipt */
#define EF_FATALERRS	0x00000020L	/* fatal errors occurred */
#define EF_DELETE_BCC	0x00000040L	/* delete Bcc: headers entirely */
#define EF_RESPONSE	0x00000080L	/* this is an error or return receipt */
#define EF_RESENT	0x00000100L	/* this message is being forwarded */
#define EF_VRFYONLY	0x00000200L	/* verify only (don't expand aliases) */
#define EF_WARNING	0x00000400L	/* warning message has been sent */
#define EF_QUEUERUN	0x00000800L	/* this envelope is from queue */
#define EF_GLOBALERRS	0x00001000L	/* treat errors as global */
#define EF_PM_NOTIFY	0x00002000L	/* send return mail to postmaster */
#define EF_METOO	0x00004000L	/* send to me too */
#define EF_LOGSENDER	0x00008000L	/* need to log the sender */
#define EF_NORECEIPT	0x00010000L	/* suppress all return-receipts */
#define EF_HAS8BIT	0x00020000L	/* at least one 8-bit char in body */
#define EF_NL_NOT_EOL	0x00040000L	/* don't accept raw NL as EOLine */
#define EF_CRLF_NOT_EOL	0x00080000L	/* don't accept CR-LF as EOLine */
#define EF_RET_PARAM	0x00100000L	/* RCPT command had RET argument */
#define EF_HAS_DF	0x00200000L	/* set when data file is instantiated */
#define EF_IS_MIME	0x00400000L	/* really is a MIME message */
#define EF_DONT_MIME	0x00800000L	/* never MIME this message */
#define EF_DISCARD	0x01000000L	/* discard the message */
#define EF_TOOBIG	0x02000000L	/* message is too big */
#define EF_SPLIT	0x04000000L	/* envelope has been split */
#define EF_UNSAFE	0x08000000L	/* unsafe: read from untrusted source */
#define EF_TOODEEP	0x10000000L	/* message is nested too deep */

#define DLVR_NOTIFY	0x01
#define DLVR_RETURN	0x02
#define DLVR_TRACE	0x10
#define IS_DLVR_NOTIFY(e)	(((e)->e_dlvr_flag & DLVR_NOTIFY) != 0)
#define IS_DLVR_RETURN(e)	(((e)->e_dlvr_flag & DLVR_RETURN) != 0)
#define IS_DLVR_TRACE(e)	(((e)->e_dlvr_flag & DLVR_TRACE) != 0)
#define IS_DLVR_BY(e)		((e)->e_dlvr_flag != 0)

#define BODYTYPE_NONE	(0)
#define BODYTYPE_7BIT	(1)
#define BODYTYPE_8BITMIME	(2)
#define BODYTYPE_ILLEGAL	(-1)
#define BODYTYPE_VALID(b) ((b) == BODYTYPE_7BIT || (b) == BODYTYPE_8BITMIME)

extern ENVELOPE	BlankEnvelope;

/* functions */
extern void	clearenvelope __P((ENVELOPE *, bool, SM_RPOOL_T *));
extern int	dropenvelope __P((ENVELOPE *, bool, bool));
extern ENVELOPE	*newenvelope __P((ENVELOPE *, ENVELOPE *, SM_RPOOL_T *));
extern void	clrsessenvelope __P((ENVELOPE *));
extern void	printenvflags __P((ENVELOPE *));
extern bool	putbody __P((MCI *, ENVELOPE *, char *));
extern bool	putheader __P((MCI *, HDR *, ENVELOPE *, int));

/*
**  Message priority classes.
**
**	The message class is read directly from the Priority: header
**	field in the message.
**
**	CurEnv->e_msgpriority is the number of bytes in the message plus
**	the creation time (so that jobs ``tend'' to be ordered correctly),
**	adjusted by the message class, the number of recipients, and the
**	amount of time the message has been sitting around.  This number
**	is used to order the queue.  Higher values mean LOWER priority.
**
**	Each priority class point is worth WkClassFact priority points;
**	each recipient is worth WkRecipFact priority points.  Each time
**	we reprocess a message the priority is adjusted by WkTimeFact.
**	WkTimeFact should normally decrease the priority so that jobs
**	that have historically failed will be run later; thanks go to
**	Jay Lepreau at Utah for pointing out the error in my thinking.
**
**	The "class" is this number, unadjusted by the age or size of
**	this message.  Classes with negative representations will have
**	error messages thrown away if they are not local.
*/

struct priority
{
	char	*pri_name;	/* external name of priority */
	int	pri_val;	/* internal value for same */
};

EXTERN int	NumPriorities;	/* pointer into Priorities */
EXTERN struct priority	Priorities[MAXPRIORITIES];

/*
**  Rewrite rules.
*/

struct rewrite
{
	char	**r_lhs;	/* pattern match */
	char	**r_rhs;	/* substitution value */
	struct rewrite	*r_next;/* next in chain */
	int	r_line;		/* rule line in sendmail.cf */
};

/*
**  Special characters in rewriting rules.
**	These are used internally only.
**	The COND* rules are actually used in macros rather than in
**		rewriting rules, but are given here because they
**		cannot conflict.
*/

/* "out of band" indicator */
/* sm/sendmail.h #define METAQUOTE ((unsigned char)0377) quotes the next octet */

/* left hand side items */
#define MATCHZANY	((unsigned char)0220)	/* match zero or more tokens */
#define MATCHANY	((unsigned char)0221)	/* match one or more tokens */
#define MATCHONE	((unsigned char)0222)	/* match exactly one token */
#define MATCHCLASS	((unsigned char)0223)	/* match one token in a class */
#define MATCHNCLASS	((unsigned char)0224)	/* match tokens not in class */

/* right hand side items */
#define MATCHREPL	((unsigned char)0225)	/* RHS replacement for above */
#define CANONNET	((unsigned char)0226)	/* canonical net, next token */
#define CANONHOST	((unsigned char)0227)	/* canonical host, next token */
#define CANONUSER	((unsigned char)0230)	/* canonical user, next N tokens */
#define CALLSUBR	((unsigned char)0231)	/* call another rewriting set */

/* conditionals in macros (anywhere) */
#define CONDIF		((unsigned char)0232)	/* conditional if-then */
#define CONDELSE	((unsigned char)0233)	/* conditional else */
#define CONDFI		((unsigned char)0234)	/* conditional fi */

/* bracket characters for RHS host name lookup */
#define HOSTBEGIN	((unsigned char)0235)	/* hostname lookup begin */
#define HOSTEND		((unsigned char)0236)	/* hostname lookup end */

/* bracket characters for RHS generalized lookup */
#define LOOKUPBEGIN	((unsigned char)0205)	/* generalized lookup begin */
#define LOOKUPEND	((unsigned char)0206)	/* generalized lookup end */

/* macro substitution characters (anywhere) */
#define MACROEXPAND	((unsigned char)0201)	/* macro expansion */
#define MACRODEXPAND	((unsigned char)0202)	/* deferred macro expansion */

/* to make the code clearer */
#define MATCHZERO	CANONHOST

#define MAXMATCH	9	/* max params per rewrite */
#define MAX_MAP_ARGS	10	/* max arguments for map */

/* external <==> internal mapping table */
struct metamac
{
	char		metaname;	/* external code (after $) */
	unsigned char	metaval;	/* internal code (as above) */
};

/* values for macros with external names only */
#define MID_OPMODE	0202	/* operation mode */

/* functions */
#if SM_HEAP_CHECK
extern void
macdefine_tagged __P((
	MACROS_T *_mac,
	ARGCLASS_T _vclass,
	int _id,
	char *_value,
	char *_file,
	int _line,
	int _group));
# define macdefine(mac,c,id,v) \
	macdefine_tagged(mac,c,id,v,__FILE__,__LINE__,sm_heap_group())
#else /* SM_HEAP_CHECK */
extern void
macdefine __P((
	MACROS_T *_mac,
	ARGCLASS_T _vclass,
	int _id,
	char *_value));
# define macdefine_tagged(mac,c,id,v,file,line,grp) macdefine(mac,c,id,v)
#endif /* SM_HEAP_CHECK */
extern void	macset __P((MACROS_T *, int, char *));
#define macget(mac, i) (mac)->mac_table[i]
extern void	expand __P((char *, char *, size_t, ENVELOPE *));
extern int	macid_parse __P((char *, char **));
#define macid(name)  macid_parse(name, NULL)
extern char	*macname __P((int));
extern char	*macvalue __P((int, ENVELOPE *));
extern int	rscheck __P((char *, char *, char *, ENVELOPE *, int, int, char *, char *, ADDRESS *, char **));
extern int	rscap __P((char *, char *, char *, ENVELOPE *, char ***, char *, int));
extern void	setclass __P((int, char *));
extern int	strtorwset __P((char *, char **, int));
extern char	*translate_dollars __P((char *, char *, int *));
extern bool	wordinclass __P((char *, int));

/*
**  Name canonification short circuit.
**
**	If the name server for a host is down, the process of trying to
**	canonify the name can hang.  This is similar to (but alas, not
**	identical to) looking up the name for delivery.  This stab type
**	caches the result of the name server lookup so we don't hang
**	multiple times.
*/

#define NAMECANON	struct _namecanon

NAMECANON
{
	short		nc_errno;	/* cached errno */
	short		nc_herrno;	/* cached h_errno */
	short		nc_stat;	/* cached exit status code */
	short		nc_flags;	/* flag bits */
	char		*nc_cname;	/* the canonical name */
	time_t		nc_exp;		/* entry expires at */
};

/* values for nc_flags */
#define NCF_VALID	0x0001	/* entry valid */

/* hostsignature structure */

struct hostsig_t
{
	char		*hs_sig;	/* hostsignature */
	time_t		hs_exp;		/* entry expires at */
};

typedef struct hostsig_t HOSTSIG_T;

/*
**  The standard udp packet size PACKETSZ (512) is not sufficient for some
**  nameserver answers containing very many resource records. The resolver
**  may switch to tcp and retry if it detects udp packet overflow.
**  Also note that the resolver routines res_query and res_search return
**  the size of the *un*truncated answer in case the supplied answer buffer
**  it not big enough to accommodate the entire answer.
*/

# ifndef MAXPACKET
#  define MAXPACKET 8192	/* max packet size used internally by BIND */
# endif /* ! MAXPACKET */

/*
**  The resolver functions res_{send,query,querydomain} expect the
**  answer buffer to be aligned, but some versions of gcc4 reverse
**  25 years of history and no longer align char buffers on the
**  stack, resulting in crashes on strict-alignment platforms.  Use
**  this union when putting the buffer on the stack to force the
**  alignment, then cast to (HEADER *) or (unsigned char *) as needed.
*/
typedef union
{
	HEADER		qb1;
	unsigned char	qb2[MAXPACKET];
} querybuf;

/* functions */
extern bool	getcanonname __P((char *, int, bool, int *));
extern int	getmxrr __P((char *, char **, unsigned short *, bool, int *, bool, int *));
extern char	*hostsignature __P((MAILER *, char *));
extern int	getfallbackmxrr __P((char *));

/*
**  Mapping functions
**
**	These allow arbitrary mappings in the config file.  The idea
**	(albeit not the implementation) comes from IDA sendmail.
*/

#define MAPCLASS	struct _mapclass
#define MAP		struct _map
#define MAXMAPACTIONS	5		/* size of map_actions array */


/*
**  An actual map.
*/

MAP
{
	MAPCLASS	*map_class;	/* the class of this map */
	MAPCLASS	*map_orgclass;	/* the original class of this map */
	char		*map_mname;	/* name of this map */
	long		map_mflags;	/* flags, see below */
	char		*map_file;	/* the (nominal) filename */
	ARBPTR_T	map_db1;	/* the open database ptr */
	ARBPTR_T	map_db2;	/* an "extra" database pointer */
	char		*map_keycolnm;	/* key column name */
	char		*map_valcolnm;	/* value column name */
	unsigned char	map_keycolno;	/* key column number */
	unsigned char	map_valcolno;	/* value column number */
	char		map_coldelim;	/* column delimiter */
	char		map_spacesub;	/* spacesub */
	char		*map_app;	/* to append to successful matches */
	char		*map_tapp;	/* to append to "tempfail" matches */
	char		*map_domain;	/* the (nominal) NIS domain */
	char		*map_rebuild;	/* program to run to do auto-rebuild */
	time_t		map_mtime;	/* last database modification time */
	time_t		map_timeout;	/* timeout for map accesses */
	int		map_retry;	/* # of retries for map accesses */
	pid_t		map_pid;	/* PID of process which opened map */
	int		map_lockfd;	/* auxiliary lock file descriptor */
	short		map_specificity;	/* specificity of aliases */
	MAP		*map_stack[MAXMAPSTACK];   /* list for stacked maps */
	short		map_return[MAXMAPACTIONS]; /* return bitmaps for stacked maps */
};


/* bit values for map_mflags */
#define MF_VALID	0x00000001	/* this entry is valid */
#define MF_INCLNULL	0x00000002	/* include null byte in key */
#define MF_OPTIONAL	0x00000004	/* don't complain if map not found */
#define MF_NOFOLDCASE	0x00000008	/* don't fold case in keys */
#define MF_MATCHONLY	0x00000010	/* don't use the map value */
#define MF_OPEN		0x00000020	/* this entry is open */
#define MF_WRITABLE	0x00000040	/* open for writing */
#define MF_ALIAS	0x00000080	/* this is an alias file */
#define MF_TRY0NULL	0x00000100	/* try with no null byte */
#define MF_TRY1NULL	0x00000200	/* try with the null byte */
#define MF_LOCKED	0x00000400	/* this map is currently locked */
#define MF_ALIASWAIT	0x00000800	/* alias map in aliaswait state */
#define MF_IMPL_HASH	0x00001000	/* implicit: underlying hash database */
#define MF_IMPL_NDBM	0x00002000	/* implicit: underlying NDBM database */
/* 0x00004000	*/
#define MF_APPEND	0x00008000	/* append new entry on rebuild */
#define MF_KEEPQUOTES	0x00010000	/* don't dequote key before lookup */
#define MF_NODEFER	0x00020000	/* don't defer if map lookup fails */
#define MF_REGEX_NOT	0x00040000	/* regular expression negation */
#define MF_DEFER	0x00080000	/* don't lookup map in defer mode */
#define MF_SINGLEMATCH	0x00100000	/* successful only if match one key */
#define MF_SINGLEDN	0x00200000	/* only one match, but multi values */
#define MF_FILECLASS	0x00400000	/* this is a file class map */
#define MF_OPENBOGUS	0x00800000	/* open failed, don't call map_close */
#define MF_CLOSING	0x01000000	/* map is being closed */

#define DYNOPENMAP(map) \
	do		\
	{		\
		if (!bitset(MF_OPEN, (map)->map_mflags)) \
		{	\
			if (!openmap(map))	\
				return NULL;	\
		}	\
	} while (0)


/* indices for map_actions */
#define MA_NOTFOUND	0		/* member map returned "not found" */
#define MA_UNAVAIL	1		/* member map is not available */
#define MA_TRYAGAIN	2		/* member map returns temp failure */

/*
**  The class of a map -- essentially the functions to call
*/

MAPCLASS
{
	char	*map_cname;		/* name of this map class */
	char	*map_ext;		/* extension for database file */
	short	map_cflags;		/* flag bits, see below */
	bool	(*map_parse)__P((MAP *, char *));
					/* argument parsing function */
	char	*(*map_lookup)__P((MAP *, char *, char **, int *));
					/* lookup function */
	void	(*map_store)__P((MAP *, char *, char *));
					/* store function */
	bool	(*map_open)__P((MAP *, int));
					/* open function */
	void	(*map_close)__P((MAP *));
					/* close function */
};

/* bit values for map_cflags */
#define MCF_ALIASOK	0x0001		/* can be used for aliases */
#define MCF_ALIASONLY	0x0002		/* usable only for aliases */
#define MCF_REBUILDABLE	0x0004		/* can rebuild alias files */
#define MCF_OPTFILE	0x0008		/* file name is optional */
#define MCF_NOTPERSIST	0x0010		/* don't keep map open all the time */

/* functions */
extern void	closemaps __P((bool));
extern bool	impl_map_open __P((MAP *, int));
extern void	initmaps __P((void));
extern MAP	*makemapentry __P((char *));
extern void	maplocaluser __P((ADDRESS *, ADDRESS **, int, ENVELOPE *));
extern char	*map_rewrite __P((MAP *, const char *, size_t, char **));
#if NETINFO
extern char	*ni_propval __P((char *, char *, char *, char *, int));
#endif /* NETINFO */
extern bool	openmap __P((MAP *));
extern int	udbexpand __P((ADDRESS *, ADDRESS **, int, ENVELOPE *));
#if USERDB
extern void	_udbx_close __P((void));
extern char	*udbsender __P((char *, SM_RPOOL_T *));
#endif /* USERDB */

/*
**  LDAP related items
*/
#if LDAPMAP
/* struct defining LDAP Auth Methods */
struct lamvalues
{
	char	*lam_name;	/* name of LDAP auth method */
	int	lam_code;	/* numeric code */
};

/* struct defining LDAP Alias Dereferencing */
struct ladvalues
{
	char	*lad_name;	/* name of LDAP alias dereferencing method */
	int	lad_code;	/* numeric code */
};

/* struct defining LDAP Search Scope */
struct lssvalues
{
	char	*lss_name;	/* name of LDAP search scope */
	int	lss_code;	/* numeric code */
};

/* functions */
extern bool	ldapmap_parseargs __P((MAP *, char *));
extern void	ldapmap_set_defaults __P((char *));
#endif /* LDAPMAP */

/*
**  PH related items
*/

#if PH_MAP

# include <phclient.h>

struct ph_map_struct
{
	char	*ph_servers;	 /* list of ph servers */
	char	*ph_field_list;	 /* list of fields to search for match */
	PH	*ph;		 /* PH server handle */
	int	ph_fastclose;	 /* send "quit" command on close */
	time_t	ph_timeout;	 /* timeout interval */
};
typedef struct ph_map_struct	PH_MAP_STRUCT;

#endif /* PH_MAP */

/*
**  Regular UNIX sockaddrs are too small to handle ISO addresses, so
**  we are forced to declare a supertype here.
*/

#if NETINET || NETINET6 || NETUNIX || NETISO || NETNS || NETX25
union bigsockaddr
{
	struct sockaddr		sa;	/* general version */
# if NETUNIX
	struct sockaddr_un	sunix;	/* UNIX family */
# endif /* NETUNIX */
# if NETINET
	struct sockaddr_in	sin;	/* INET family */
# endif /* NETINET */
# if NETINET6
	struct sockaddr_in6	sin6;	/* INET/IPv6 */
# endif /* NETINET6 */
# if NETISO
	struct sockaddr_iso	siso;	/* ISO family */
# endif /* NETISO */
# if NETNS
	struct sockaddr_ns	sns;	/* XNS family */
# endif /* NETNS */
# if NETX25
	struct sockaddr_x25	sx25;	/* X.25 family */
# endif /* NETX25 */
};

# define SOCKADDR	union bigsockaddr

/* functions */
extern char	*anynet_ntoa __P((SOCKADDR *));
# if NETINET6
extern char	*anynet_ntop __P((struct in6_addr *, char *, size_t));
extern int	anynet_pton __P((int, const char *, void *));
# endif /* NETINET6 */
extern char	*hostnamebyanyaddr __P((SOCKADDR *));
extern char	*validate_connection __P((SOCKADDR *, char *, ENVELOPE *));
# if SASL >= 20000
extern bool	iptostring __P((SOCKADDR *, SOCKADDR_LEN_T, char *, unsigned));
# endif /* SASL >= 20000 */

#endif /* NETINET || NETINET6 || NETUNIX || NETISO || NETNS || NETX25 */

/*
**  Process List (proclist)
*/

#define NO_PID		((pid_t) 0)
#ifndef PROC_LIST_SEG
# define PROC_LIST_SEG	32		/* number of pids to alloc at a time */
#endif /* ! PROC_LIST_SEG */

/* process types */
#define PROC_NONE		0
#define PROC_DAEMON		1
#define PROC_DAEMON_CHILD	2
#define PROC_QUEUE		3
#define PROC_QUEUE_CHILD	3
#define PROC_CONTROL		4
#define PROC_CONTROL_CHILD	5

/* functions */
extern void	proc_list_add __P((pid_t, char *, int, int, int, SOCKADDR *));
extern void	proc_list_clear __P((void));
extern void	proc_list_display __P((SM_FILE_T *, char *));
extern void	proc_list_drop __P((pid_t, int, int *));
extern void	proc_list_probe __P((void));
extern void	proc_list_set __P((pid_t, char *));
extern void	proc_list_signal __P((int, int));

/*
**  Symbol table definitions
*/

struct symtab
{
	char		*s_name;	/* name to be entered */
	short		s_symtype;	/* general type (see below) */
	struct symtab	*s_next;	/* pointer to next in chain */
	union
	{
		BITMAP256	sv_class;	/* bit-map of word classes */
		MAILER		*sv_mailer;	/* pointer to mailer */
		char		*sv_alias;	/* alias */
		MAPCLASS	sv_mapclass;	/* mapping function class */
		MAP		sv_map;		/* mapping function */
		HOSTSIG_T	sv_hostsig;	/* host signature */
		MCI		sv_mci;		/* mailer connection info */
		NAMECANON	sv_namecanon;	/* canonical name cache */
		int		sv_macro;	/* macro name => id mapping */
		int		sv_ruleset;	/* ruleset index */
		struct hdrinfo	sv_header;	/* header metainfo */
		char		*sv_service[MAXMAPSTACK]; /* service switch */
#if LDAPMAP
		MAP		*sv_lmap;	/* Maps for LDAP connection */
#endif /* LDAPMAP */
#if SOCKETMAP
		MAP		*sv_socketmap;	/* Maps for SOCKET connection */
#endif /* SOCKETMAP */
#if MILTER
		struct milter	*sv_milter;	/* milter filter name */
#endif /* MILTER */
		QUEUEGRP	*sv_queue;	/* pointer to queue */
	}	s_value;
};

typedef struct symtab	STAB;

/* symbol types */
#define ST_UNDEF	0	/* undefined type */
#define ST_CLASS	1	/* class map */
/* #define ST_unused	2	UNUSED */
#define ST_MAILER	3	/* a mailer header */
#define ST_ALIAS	4	/* an alias */
#define ST_MAPCLASS	5	/* mapping function class */
#define ST_MAP		6	/* mapping function */
#define ST_HOSTSIG	7	/* host signature */
#define ST_NAMECANON	8	/* cached canonical name */
#define ST_MACRO	9	/* macro name to id mapping */
#define ST_RULESET	10	/* ruleset index */
#define ST_SERVICE	11	/* service switch entry */
#define ST_HEADER	12	/* special header flags */
#if LDAPMAP
# define ST_LMAP	13	/* List head of maps for LDAP connection */
#endif /* LDAPMAP */
#if MILTER
# define ST_MILTER	14	/* milter filter */
#endif /* MILTER */
#define ST_QUEUE	15	/* a queue entry */

#if SOCKETMAP
# define ST_SOCKETMAP   16      /* List head of maps for SOCKET connection */
#endif /* SOCKETMAP */

/* This entry must be last */
#define ST_MCI		17	/* mailer connection info (offset) */

#define s_class		s_value.sv_class
#define s_mailer	s_value.sv_mailer
#define s_alias		s_value.sv_alias
#define s_mci		s_value.sv_mci
#define s_mapclass	s_value.sv_mapclass
#define s_hostsig	s_value.sv_hostsig
#define s_map		s_value.sv_map
#define s_namecanon	s_value.sv_namecanon
#define s_macro		s_value.sv_macro
#define s_ruleset	s_value.sv_ruleset
#define s_service	s_value.sv_service
#define s_header	s_value.sv_header
#if LDAPMAP
# define s_lmap		s_value.sv_lmap
#endif /* LDAPMAP */
#if SOCKETMAP
# define s_socketmap    s_value.sv_socketmap
#endif /* SOCKETMAP */
#if MILTER
# define s_milter	s_value.sv_milter
#endif /* MILTER */
#define s_quegrp	s_value.sv_queue

/* opcodes to stab */
#define ST_FIND		0	/* find entry */
#define ST_ENTER	1	/* enter if not there */

/* functions */
extern STAB	*stab __P((char *, int, int));
extern void	stabapply __P((void (*)(STAB *, int), int));

/*
**  Operation, send, error, and MIME modes
**
**	The operation mode describes the basic operation of sendmail.
**	This can be set from the command line, and is "send mail" by
**	default.
**
**	The send mode tells how to send mail.  It can be set in the
**	configuration file.  Its setting determines how quickly the
**	mail will be delivered versus the load on your system.  If the
**	-v (verbose) flag is given, it will be forced to SM_DELIVER
**	mode.
**
**	The error mode tells how to return errors.
*/

#define MD_DELIVER	'm'		/* be a mail sender */
#define MD_SMTP		's'		/* run SMTP on standard input */
#define MD_ARPAFTP	'a'		/* obsolete ARPANET mode (Grey Book) */
#define MD_DAEMON	'd'		/* run as a daemon */
#define MD_FGDAEMON	'D'		/* run daemon in foreground */
#define MD_LOCAL	'l'		/* like daemon, but localhost only */
#define MD_VERIFY	'v'		/* verify: don't collect or deliver */
#define MD_TEST		't'		/* test mode: resolve addrs only */
#define MD_INITALIAS	'i'		/* initialize alias database */
#define MD_PRINT	'p'		/* print the queue */
#define MD_PRINTNQE	'P'		/* print number of entries in queue */
#define MD_FREEZE	'z'		/* freeze the configuration file */
#define MD_HOSTSTAT	'h'		/* print persistent host stat info */
#define MD_PURGESTAT	'H'		/* purge persistent host stat info */
#define MD_QUEUERUN	'q'		/* queue run */
#define MD_CHECKCONFIG	'C'		/* check configuration file */

#if _FFR_LOCAL_DAEMON
EXTERN bool	LocalDaemon;
# if NETINET6
EXTERN bool	V6LoopbackAddrFound;	/* found an IPv6 loopback address */
#  define SETV6LOOPBACKADDRFOUND(sa)	\
	do	\
	{	\
		if (isloopback(sa))	\
			V6LoopbackAddrFound = true;	\
	} while (0)
# endif /* NETINET6 */
#else /* _FFR_LOCAL_DAEMON */
# define LocalDaemon	false
# define V6LoopbackAddrFound	false
# define SETV6LOOPBACKADDRFOUND(sa)
#endif /* _FFR_LOCAL_DAEMON */

/* Note: see also include/sendmail/pathnames.h: GET_CLIENT_CF */

/* values for e_sendmode -- send modes */
#define SM_DELIVER	'i'		/* interactive delivery */
#if _FFR_PROXY
#define SM_PROXY_REQ	's'		/* synchronous mode requested */
#define SM_PROXY	'S'		/* synchronous mode activated */
#endif /* _FFR_PROXY */
#define SM_FORK		'b'		/* deliver in background */
#if _FFR_DM_ONE
#define SM_DM_ONE	'o' /* deliver first TA in background, then queue */
#endif /* _FFR_DM_ONE */
#define SM_QUEUE	'q'		/* queue, don't deliver */
#define SM_DEFER	'd'		/* defer map lookups as well as queue */
#define SM_VERIFY	'v'		/* verify only (used internally) */
#define DM_NOTSET	(-1)	/* DeliveryMode (per daemon) option not set */
#if _FFR_PROXY
# define SM_IS_INTERACTIVE(m)	((m) == SM_DELIVER || (m) == SM_PROXY_REQ || (m) == SM_PROXY)
#else /* _FFR_PROXY */
# define SM_IS_INTERACTIVE(m)	((m) == SM_DELIVER)
#endif /* _FFR_PROXY */

#define WILL_BE_QUEUED(m)	((m) == SM_QUEUE || (m) == SM_DEFER)

/* used only as a parameter to sendall */
#define SM_DEFAULT	'\0'		/* unspecified, use SendMode */

/* functions */
extern void	set_delivery_mode __P((int, ENVELOPE *));

/* values for e_errormode -- error handling modes */
#define EM_PRINT	'p'		/* print errors */
#define EM_MAIL		'm'		/* mail back errors */
#define EM_WRITE	'w'		/* write back errors */
#define EM_BERKNET	'e'		/* special berknet processing */
#define EM_QUIET	'q'		/* don't print messages (stat only) */


/* bit values for MimeMode */
#define MM_CVTMIME	0x0001		/* convert 8 to 7 bit MIME */
#define MM_PASS8BIT	0x0002		/* just send 8 bit data blind */
#define MM_MIME8BIT	0x0004		/* convert 8-bit data to MIME */


/* how to handle messages without any recipient addresses */
#define NRA_NO_ACTION		0	/* just leave it as is */
#define NRA_ADD_TO		1	/* add To: header */
#define NRA_ADD_APPARENTLY_TO	2	/* add Apparently-To: header */
#define NRA_ADD_BCC		3	/* add empty Bcc: header */
#define NRA_ADD_TO_UNDISCLOSED	4	/* add To: undisclosed:; header */


/* flags to putxline */
#define PXLF_NOTHINGSPECIAL	0	/* no special mapping */
#define PXLF_MAPFROM		0x0001	/* map From_ to >From_ */
#define PXLF_STRIP8BIT		0x0002	/* strip 8th bit */
#define PXLF_HEADER		0x0004	/* map newlines in headers */
#define PXLF_NOADDEOL		0x0008	/* if EOL not present, don't add one */
#define PXLF_STRIPMQUOTE	0x0010	/* strip METAQUOTEs */

/*
**  Privacy flags
**	These are bit values for the PrivacyFlags word.
*/

#define PRIV_PUBLIC		0		/* what have I got to hide? */
#define PRIV_NEEDMAILHELO	0x00000001	/* insist on HELO for MAIL */
#define PRIV_NEEDEXPNHELO	0x00000002	/* insist on HELO for EXPN */
#define PRIV_NEEDVRFYHELO	0x00000004	/* insist on HELO for VRFY */
#define PRIV_NOEXPN		0x00000008	/* disallow EXPN command */
#define PRIV_NOVRFY		0x00000010	/* disallow VRFY command */
#define PRIV_AUTHWARNINGS	0x00000020	/* flag possible auth probs */
#define PRIV_NOVERB		0x00000040	/* disallow VERB command */
#define PRIV_RESTRICTMAILQ	0x00010000	/* restrict mailq command */
#define PRIV_RESTRICTQRUN	0x00020000	/* restrict queue run */
#define PRIV_RESTRICTEXPAND	0x00040000	/* restrict alias/forward expansion */
#define PRIV_NOETRN		0x00080000	/* disallow ETRN command */
#define PRIV_NOBODYRETN		0x00100000	/* do not return bodies on bounces */
#define PRIV_NORECEIPTS		0x00200000	/* disallow return receipts */
#define PRIV_NOACTUALRECIPIENT	0x00400000 /* no X-Actual-Recipient in DSNs */

/* don't give no info, anyway, anyhow (in the main SMTP transaction) */
#define PRIV_GOAWAY		0x0000ffff

/* struct defining such things */
struct prival
{
	char		*pv_name;	/* name of privacy flag */
	unsigned long	pv_flag;	/* numeric level */
};

EXTERN unsigned long	PrivacyFlags;	/* privacy flags */


/*
**  Flags passed to remotename, parseaddr, allocaddr, and buildaddr.
*/

#define RF_SENDERADDR		0x001	/* this is a sender address */
#define RF_HEADERADDR		0x002	/* this is a header address */
#define RF_CANONICAL		0x004	/* strip comment information */
#define RF_ADDDOMAIN		0x008	/* OK to do domain extension */
#define RF_COPYPARSE		0x010	/* copy parsed user & host */
#define RF_COPYPADDR		0x020	/* copy print address */
#define RF_COPYALL		(RF_COPYPARSE|RF_COPYPADDR)
#define RF_COPYNONE		0
#define RF_RM_ADDR		0x040	/* address to be removed */

/*
**  Flags passed to rscheck
*/

#define RSF_RMCOMM		0x0001	/* strip comments */
#define RSF_UNSTRUCTURED	0x0002	/* unstructured, ignore syntax errors */
#define RSF_COUNT		0x0004	/* count rejections (statistics)? */
#define RSF_ADDR		0x0008	/* reassemble address */
#define RSF_STRING		0x0010	/* reassemble address as string */

/*
**  Flags passed to mime8to7 and putheader.
*/

#define M87F_OUTER		0	/* outer context */
#define M87F_NO8BIT		0x0001	/* can't have 8-bit in this section */
#define M87F_DIGEST		0x0002	/* processing multipart/digest */
#define M87F_NO8TO7		0x0004	/* don't do 8->7 bit conversions */

/* functions */
extern bool	mime7to8 __P((MCI *, HDR *, ENVELOPE *));
extern int	mime8to7 __P((MCI *, HDR *, ENVELOPE *, char **, int, int));

/*
**  Flags passed to returntosender.
*/

#define RTSF_NO_BODY		0	/* send headers only */
#define RTSF_SEND_BODY		0x0001	/* include body of message in return */
#define RTSF_PM_BOUNCE		0x0002	/* this is a postmaster bounce */

/* functions */
extern int	returntosender __P((char *, ADDRESS *, int, ENVELOPE *));

/*
**  Mail Filters (milter)
*/

/*
**  32-bit type used by milter
**  (needed by libmilter even if MILTER isn't defined)
*/

typedef SM_INT32	mi_int32;

#if MILTER
# define SMFTO_WRITE	0		/* Timeout for sending information */
# define SMFTO_READ	1		/* Timeout waiting for a response */
# define SMFTO_EOM	2		/* Timeout for ACK/NAK to EOM */
# define SMFTO_CONNECT	3		/* Timeout for connect() */

# define SMFTO_NUM_TO	4		/* Total number of timeouts */

struct milter
{
	char		*mf_name;	/* filter name */
	BITMAP256	mf_flags;	/* MTA flags */
	mi_int32	mf_fvers;	/* filter version */
	mi_int32	mf_fflags;	/* filter flags */
	mi_int32	mf_pflags;	/* protocol flags */
	char		*mf_conn;	/* connection info */
	int		mf_sock;	/* connected socket */
	char		mf_state;	/* state of filter */
	char		mf_lflags;	/* "local" flags */
	int		mf_idx;		/* milter number (index) */
	time_t		mf_timeout[SMFTO_NUM_TO]; /* timeouts */
#if _FFR_MILTER_CHECK
	/* for testing only */
	mi_int32	mf_mta_prot_version;
	mi_int32	mf_mta_prot_flags;
	mi_int32	mf_mta_actions;
#endif /* _FFR_MILTER_CHECK */
};

#define MI_LFL_NONE	0x00000000
#define MI_LFLAGS_SYM(st) (1 << (st))	/* has its own symlist for stage st */

struct milters
{
	mi_int32	mis_flags;	/* filter flags */
};
typedef struct milters	milters_T;

#define MIS_FL_NONE	0x00000000	/* no requirements... */
#define MIS_FL_DEL_RCPT	0x00000001	/* can delete rcpt */
#define MIS_FL_REJ_RCPT	0x00000002	/* can reject rcpt */


/* MTA flags */
# define SMF_REJECT		'R'	/* Reject connection on filter fail */
# define SMF_TEMPFAIL		'T'	/* tempfail connection on failure */
# define SMF_TEMPDROP		'4'	/* 421 connection on failure */

EXTERN struct milter	*InputFilters[MAXFILTERS];
EXTERN char		*InputFilterList;
EXTERN int		MilterLogLevel;

/* functions */
extern void	setup_daemon_milters __P((void));
#endif /* MILTER */

/*
**  Vendor codes
**
**	Vendors can customize sendmail to add special behaviour,
**	generally for back compatibility.  Ideally, this should
**	be set up in the .cf file using the "V" command.  However,
**	it's quite reasonable for some vendors to want the default
**	be their old version; this can be set using
**		-DVENDOR_DEFAULT=VENDOR_xxx
**	in the Makefile.
**
**	Vendors should apply to sendmail@sendmail.org for
**	unique vendor codes.
*/

#define VENDOR_BERKELEY	1	/* Berkeley-native configuration file */
#define VENDOR_SUN	2	/* Sun-native configuration file */
#define VENDOR_HP	3	/* Hewlett-Packard specific config syntax */
#define VENDOR_IBM	4	/* IBM specific config syntax */
#define VENDOR_SENDMAIL	5	/* Proofpoint, Inc. specific config syntax */
#define VENDOR_DEC	6	/* Compaq, DEC, Digital */

/* prototypes for vendor-specific hook routines */
extern void	vendor_daemon_setup __P((ENVELOPE *));
extern void	vendor_set_uid __P((UID_T));


/*
**  Terminal escape codes.
**
**	To make debugging output clearer.
*/

struct termescape
{
	char	*te_rv_on;	/* turn reverse-video on */
	char	*te_under_on;	/* turn underlining on */
	char	*te_normal;	/* revert to normal output */
};

/*
**  Additional definitions
*/

/*
**  d_flags, see daemon.c
**  general rule: lower case: required, upper case: No
*/

#define D_AUTHREQ	'a'	/* authentication required */
#define D_BINDIF	'b'	/* use if_addr for outgoing connection */
#define D_CANONREQ	'c'	/* canonification required (cf) */
#define D_IFNHELO	'h'	/* use if name for HELO */
#define D_FQMAIL	'f'	/* fq sender address required (cf) */
#define D_FQRCPT	'r'	/* fq recipient address required (cf) */
#define D_SMTPS		's'	/* SMTP over SSL (smtps) */
#define D_UNQUALOK	'u'	/* unqualified address is ok (cf) */
#define D_NOAUTH	'A'	/* no AUTH */
#define D_NOCANON	'C'	/* no canonification (cf) */
#define D_NOETRN	'E'	/* no ETRN (MSA) */
#define D_NOTLS		'S'	/* don't use STARTTLS */
#define D_ETRNONLY	((char)0x01)	/* allow only ETRN (disk low) */
#define D_OPTIONAL	'O'	/* optional socket */
#define D_DISABLE	((char)0x02)	/* optional socket disabled */
#define D_ISSET		((char)0x03)	/* this client struct is set */
#if _FFR_XCNCT
#define D_XCNCT	((char)0x04)	/* X-Connect was used */
#define D_XCNCT_M	((char)0x05)	/* X-Connect was used + "forged" */
#endif /* _FFR_XCNCT */

#if STARTTLS
/*
**  TLS
*/

/* what to do in the TLS initialization */
#define TLS_I_NONE	0x00000000	/* no requirements... */
#define TLS_I_CERT_EX	0x00000001	/* cert must exist */
#define TLS_I_CERT_UNR	0x00000002	/* cert must be g/o unreadable */
#define TLS_I_KEY_EX	0x00000004	/* key must exist */
#define TLS_I_KEY_UNR	0x00000008	/* key must be g/o unreadable */
#define TLS_I_CERTP_EX	0x00000010	/* CA cert path must exist */
#define TLS_I_CERTP_UNR	0x00000020	/* CA cert path must be g/o unreadable */
#define TLS_I_CERTF_EX	0x00000040	/* CA cert file must exist */
#define TLS_I_CERTF_UNR	0x00000080	/* CA cert file must be g/o unreadable */
#define TLS_I_RSA_TMP	0x00000100	/* RSA TMP must be generated */
#define TLS_I_USE_KEY	0x00000200	/* private key must usable */
#define TLS_I_USE_CERT	0x00000400	/* certificate must be usable */
#define TLS_I_VRFY_PATH	0x00000800	/* load verify path must succeed */
#define TLS_I_VRFY_LOC	0x00001000	/* load verify default must succeed */
#define TLS_I_CACHE	0x00002000	/* require cache */
#define TLS_I_TRY_DH	0x00004000	/* try DH certificate */
#define TLS_I_REQ_DH	0x00008000	/* require DH certificate */
#define TLS_I_DHPAR_EX	0x00010000	/* require DH parameters */
#define TLS_I_DHPAR_UNR	0x00020000	/* DH param. must be g/o unreadable */
#define TLS_I_DH512	0x00040000	/* generate 512bit DH param */
#define TLS_I_DH1024	0x00080000	/* generate 1024bit DH param */
#define TLS_I_DH2048	0x00100000	/* generate 2048bit DH param */
#define TLS_I_NO_VRFY	0x00200000	/* do not require authentication */
#define TLS_I_KEY_OUNR	0x00400000	/* Key must be other unreadable */
#define TLS_I_CRLF_EX	0x00800000	/* CRL file must exist */
#define TLS_I_CRLF_UNR	0x01000000	/* CRL file must be g/o unreadable */
#define TLS_I_DHFIXED	0x02000000	/* use fixed DH param */

/* require server cert */
#define TLS_I_SRV_CERT	 (TLS_I_CERT_EX | TLS_I_KEY_EX | \
			  TLS_I_KEY_UNR | TLS_I_KEY_OUNR | \
			  TLS_I_CERTP_EX | TLS_I_CERTF_EX | \
			  TLS_I_USE_KEY | TLS_I_USE_CERT | TLS_I_CACHE)

/* server requirements */
#define TLS_I_SRV	(TLS_I_SRV_CERT | TLS_I_RSA_TMP | TLS_I_VRFY_PATH | \
			 TLS_I_VRFY_LOC | TLS_I_TRY_DH | TLS_I_CACHE)

/* client requirements */
#define TLS_I_CLT	(TLS_I_KEY_UNR | TLS_I_KEY_OUNR)

#define TLS_AUTH_OK	0
#define TLS_AUTH_NO	1
#define TLS_AUTH_FAIL	(-1)

/* functions */
extern bool	init_tls_library __P((bool _fipsmode));
extern bool	inittls __P((SSL_CTX **, unsigned long, unsigned long, bool, char *, char *, char *, char *, char *));
extern bool	initclttls __P((bool));
extern void	setclttls __P((bool));
extern bool	initsrvtls __P((bool));
extern int	tls_get_info __P((SSL *, bool, char *, MACROS_T *, bool));
extern int	endtls __P((SSL *, char *));
extern void	tlslogerr __P((int, const char *));


EXTERN char	*CACertPath;	/* path to CA certificates (dir. with hashes) */
EXTERN char	*CACertFile;	/* file with CA certificate */
EXTERN char	*CltCertFile;	/* file with client certificate */
EXTERN char	*CltKeyFile;	/* file with client private key */
EXTERN char	*CipherList;	/* list of ciphers */
EXTERN char	*CertFingerprintAlgorithm;	/* name of fingerprint alg */
EXTERN const EVP_MD	*EVP_digest;	/* digest for cert fp */
EXTERN char	*DHParams;	/* file with DH parameters */
EXTERN char	*RandFile;	/* source of random data */
EXTERN char	*SrvCertFile;	/* file with server certificate */
EXTERN char	*SrvKeyFile;	/* file with server private key */
EXTERN char	*CRLFile;	/* file CRLs */
#if _FFR_CRLPATH
EXTERN char	*CRLPath;	/* path to CRLs (dir. with hashes) */
#endif /* _FFR_CRLPATH */
EXTERN unsigned long	TLS_Srv_Opts;	/* TLS server options */
EXTERN unsigned long	Srv_SSL_Options, Clt_SSL_Options; /* SSL options */
#endif /* STARTTLS */

/*
**  Queue related items
*/

/* queue file names */
#define ANYQFL_LETTER '?'
#define QUARQF_LETTER 'h'
#define DATAFL_LETTER 'd'
#define XSCRPT_LETTER 'x'
#define NORMQF_LETTER 'q'
#define NEWQFL_LETTER 't'

# define TEMPQF_LETTER 'T'
# define LOSEQF_LETTER 'Q'

/* queue sort order */
#define QSO_BYPRIORITY	0		/* sort by message priority */
#define QSO_BYHOST	1		/* sort by first host name */
#define QSO_BYTIME	2		/* sort by submission time */
#define QSO_BYFILENAME	3		/* sort by file name only */
#define QSO_RANDOM	4		/* sort in random order */
#define QSO_BYMODTIME	5		/* sort by modification time */
#define QSO_NONE	6		/* do not sort */
#if _FFR_RHS
# define QSO_BYSHUFFLE	7		/* sort by shuffled host name */
#endif /* _FFR_RHS */

#define NOQGRP	(-1)		/* no queue group (yet) */
#define ENVQGRP	(-2)		/* use queue group of envelope */
#define NOAQGRP	(-3)		/* no queue group in addr (yet) */
#define ISVALIDQGRP(x)	((x) >= 0)	/* valid queue group? */
#define NOQDIR	(-1)		/* no queue directory (yet) */
#define ENVQDIR	(-2)		/* use queue directory of envelope */
#define NOAQDIR	(-3)		/* no queue directory in addr (yet) */
#define ISVALIDQDIR(x)	((x) >= 0)	/* valid queue directory? */
#define RS_QUEUEGROUP	"queuegroup"	/* ruleset for queue group selection */

#define NOW	((time_t) (-1))		/* queue return: now */

/* SuperSafe values */
#define SAFE_NO			0	/* no fsync(): don't use... */
#define SAFE_INTERACTIVE	1	/* limit fsync() in -odi */
#define SAFE_REALLY		2	/* always fsync() */
#define SAFE_REALLY_POSTMILTER	3	/* fsync() if milter says OK */

/* QueueMode bits */
#define QM_NORMAL		' '
#define QM_QUARANTINE		'Q'
#define QM_LOST			'L'

/* Queue Run Limitations */
struct queue_char
{
	char			*queue_match;	/* string to match */
	bool			queue_negate;	/* or not match, if set */
	struct queue_char	*queue_next;
};

/* run_work_group() flags */
#define RWG_NONE		0x0000
#define RWG_FORK		0x0001
#define RWG_VERBOSE		0x0002
#define RWG_PERSISTENT		0x0004
#define RWG_FORCE		0x0008
#define RWG_RUNALL		0x0010

typedef struct queue_char	QUEUE_CHAR;

EXTERN int	volatile CurRunners;	/* current number of runner children */
EXTERN int	MaxQueueRun;	/* maximum number of jobs in one queue run */
EXTERN int	MaxQueueChildren;	/* max # of forked queue children */
EXTERN int	MaxRunnersPerQueue;	/* max # proc's active in queue group */
EXTERN int	NiceQueueRun;	/* nice queue runs to this value */
EXTERN int	NumQueue;	/* number of queue groups */
EXTERN int	QueueFileMode;	/* mode on files in mail queue */
EXTERN int	QueueMode;	/* which queue items to act upon */
EXTERN int	QueueSortOrder;	/* queue sorting order algorithm */
EXTERN time_t	MinQueueAge;	/* min delivery interval */
EXTERN time_t	MaxQueueAge;	/* max delivery interval */
EXTERN time_t	QueueIntvl;	/* intervals between running the queue */
EXTERN char	*QueueDir;	/* location of queue directory */
EXTERN QUEUE_CHAR	*QueueLimitId;		/* limit queue run to id */
EXTERN QUEUE_CHAR	*QueueLimitQuarantine;	/* limit queue run to quarantine reason */
EXTERN QUEUE_CHAR	*QueueLimitRecipient;	/* limit queue run to rcpt */
EXTERN QUEUE_CHAR	*QueueLimitSender;	/* limit queue run to sender */
EXTERN QUEUEGRP	*Queue[MAXQUEUEGROUPS + 1];	/* queue groups */
#if _FFR_BOUNCE_QUEUE
EXTERN int	BounceQueue;
#endif

/* functions */
extern void	assign_queueid __P((ENVELOPE *));
extern ADDRESS	*copyqueue __P((ADDRESS *, SM_RPOOL_T *));
extern void	cleanup_queues __P((void));
extern bool	doqueuerun __P((void));
extern void	initsys __P((ENVELOPE *));
extern void	loseqfile __P((ENVELOPE *, char *));
extern int	name2qid __P((char *));
extern char	*qid_printname __P((ENVELOPE *));
extern char	*qid_printqueue __P((int, int));
extern void	quarantine_queue __P((char *, int));
extern char	*queuename __P((ENVELOPE *, int));
extern void	queueup __P((ENVELOPE *, bool, bool));
extern bool	runqueue __P((bool, bool, bool, bool));
extern bool	run_work_group __P((int, int));
extern void	set_def_queueval __P((QUEUEGRP *, bool));
extern void	setup_queues __P((bool));
extern bool	setnewqueue __P((ENVELOPE *));
extern bool	shouldqueue __P((long, time_t));
extern void	sync_queue_time __P((void));
extern void	init_qid_alg __P((void));
extern int	print_single_queue __P((int, int));
#if REQUIRES_DIR_FSYNC
# define SYNC_DIR(path, panic) sync_dir(path, panic)
extern void	sync_dir __P((char *, bool));
#else /* REQUIRES_DIR_FSYNC */
# define SYNC_DIR(path, panic) ((void) 0)
#endif /* REQUIRES_DIR_FSYNC */

/*
**  Timeouts
**
**	Indicated values are the MINIMUM per RFC 1123 section 5.3.2.
*/

EXTERN struct
{
			/* RFC 1123-specified timeouts [minimum value] */
	time_t	to_initial;	/* initial greeting timeout [5m] */
	time_t	to_mail;	/* MAIL command [5m] */
	time_t	to_rcpt;	/* RCPT command [5m] */
	time_t	to_datainit;	/* DATA initiation [2m] */
	time_t	to_datablock;	/* DATA block [3m] */
	time_t	to_datafinal;	/* DATA completion [10m] */
	time_t	to_nextcommand;	/* next command [5m] */
			/* following timeouts are not mentioned in RFC 1123 */
	time_t	to_iconnect;	/* initial connection timeout (first try) */
	time_t	to_connect;	/* initial connection timeout (later tries) */
	time_t	to_aconnect;	/* all connections timeout (MX and A records) */
	time_t	to_rset;	/* RSET command */
	time_t	to_helo;	/* HELO command */
	time_t	to_quit;	/* QUIT command */
	time_t	to_miscshort;	/* misc short commands (NOOP, VERB, etc) */
	time_t	to_ident;	/* IDENT protocol requests */
	time_t	to_fileopen;	/* opening :include: and .forward files */
	time_t	to_control;	/* process a control socket command */
	time_t	to_lhlo;	/* LMTP: LHLO command */
#if SASL
	time_t	to_auth;	/* AUTH dialogue [10m] */
#endif /* SASL */
#if STARTTLS
	time_t	to_starttls;	/* STARTTLS dialogue [10m] */
#endif /* STARTTLS */
			/* following are per message */
	time_t	to_q_return[MAXTOCLASS];	/* queue return timeouts */
	time_t	to_q_warning[MAXTOCLASS];	/* queue warning timeouts */
	time_t	res_retrans[MAXRESTOTYPES];	/* resolver retransmit */
	int	res_retry[MAXRESTOTYPES];	/* resolver retry */
} TimeOuts;

/* timeout classes for return and warning timeouts */
#define TOC_NORMAL	0	/* normal delivery */
#define TOC_URGENT	1	/* urgent delivery */
#define TOC_NONURGENT	2	/* non-urgent delivery */
#define TOC_DSN		3	/* DSN delivery */

/* resolver timeout specifiers */
#define RES_TO_FIRST	0	/* first attempt */
#define RES_TO_NORMAL	1	/* subsequent attempts */
#define RES_TO_DEFAULT	2	/* default value */

/* functions */
extern void	inittimeouts __P((char *, bool));

/*
**  Interface probing
*/

#define DPI_PROBENONE		0	/* Don't probe any interfaces */
#define DPI_PROBEALL		1	/* Probe all interfaces */
#define DPI_SKIPLOOPBACK	2	/* Don't probe loopback interfaces */

/*
**  Trace information
*/

/* macros for debugging flags */
#if NOT_SENDMAIL
# define tTd(flag, level)	(tTdvect[flag] >= (unsigned char)level)
#else
# define tTd(flag, level)	(tTdvect[flag] >= (unsigned char)level && !IntSig)
#endif
#define tTdlevel(flag)		(tTdvect[flag])

/* variables */
extern unsigned char	tTdvect[100];	/* trace vector */

/*
**  Miscellaneous information.
*/

/*
**  The "no queue id" queue id for sm_syslog
*/

#define NOQID		""

#define CURHOSTNAME	(CurHostName == NULL ? "local" : CurHostName)

/*
**  Some in-line functions
*/

/* set exit status */
#define setstat(s)	\
	do		\
	{		\
		if (ExitStat == EX_OK || ExitStat == EX_TEMPFAIL) \
			ExitStat = s; \
	} while (0)


#define STRUCTCOPY(s, d)	d = s

/* free a pointer if it isn't NULL and set it to NULL */
#define SM_FREE_CLR(p)	\
	do		\
	{		\
		if ((p) != NULL) \
		{ \
			sm_free(p); \
			(p) = NULL; \
		} \
	} while (0)

/*
**  Update a permanent string variable with a new value.
**  The old value is freed, the new value is strdup'ed.
**
**  We use sm_pstrdup_x to duplicate the string because it raises
**  an exception on error, and because it allocates "permanent storage"
**  which is not expected to be freed before process exit.
**  The latter is important for memory leak analysis.
**
**  If an exception occurs while strdup'ing the new value,
**  then the variable remains set to the old value.
**  That's why the strdup must occur before we free the old value.
**
**  The macro uses a do loop so that this idiom will work:
**	if (...)
**		PSTRSET(var, val1);
**	else
**		PSTRSET(var, val2);
*/
#define PSTRSET(var, val) \
	do \
	{ \
		char *_newval = sm_pstrdup_x(val); \
		if (var != NULL) \
			sm_free(var); \
		var = _newval; \
	} while (0)

#define _CHECK_RESTART \
	do \
	{ \
		if (ShutdownRequest != NULL) \
			shutdown_daemon(); \
		else if (RestartRequest != NULL) \
			restart_daemon(); \
		else if (RestartWorkGroup) \
			restart_marked_work_groups(); \
	} while (0)

# define CHECK_RESTART _CHECK_RESTART

#define CHK_CUR_RUNNERS(fct, idx, count)	\
	do	\
	{	\
		if (CurRunners < 0)	\
		{	\
			if (LogLevel > 3)	\
				sm_syslog(LOG_ERR, NOQID,	\
					"%s: CurRunners=%d, i=%d, count=%d, status=should not happen",	\
					fct, CurRunners, idx, count);	\
			CurRunners = 0;	\
		}	\
	} while (0)

/* reply types (text in SmtpMsgBuffer) */
#define XS_DEFAULT	0	/* other commands, e.g., RSET */
#define XS_STARTTLS	1
#define XS_AUTH		2
#define XS_GREET	3
#define XS_EHLO		4
#define XS_MAIL		5
#define XS_RCPT		6
#define XS_DATA		7
#define XS_EOM		8
#define XS_DATA2	9	/* LMTP */
#define XS_QUIT		10

/*
**  Global variables.
*/

#if _FFR_ADD_BCC
EXTERN bool AddBcc;
#endif
#if _FFR_ADDR_TYPE_MODES
EXTERN bool	AddrTypeModes;	/* addr_type: extra "mode" information */
#endif /* _FFR_ADDR_TYPE_MODES */
EXTERN bool	AllowBogusHELO;	/* allow syntax errors on HELO command */
EXTERN bool	CheckAliases;	/* parse addresses during newaliases */
#if _FFR_QUEUE_RUN_PARANOIA
EXTERN int	CheckQueueRunners; /* check whether queue runners are OK */
#endif /* _FFR_QUEUE_RUN_PARANOIA */
EXTERN bool	ColonOkInAddr;	/* single colon legal in address */
#if !defined(_USE_SUN_NSSWITCH_) && !defined(_USE_DEC_SVC_CONF_)
EXTERN bool	ConfigFileRead;	/* configuration file has been read */
#endif /* !defined(_USE_SUN_NSSWITCH_) && !defined(_USE_DEC_SVC_CONF_) */
EXTERN bool	DisConnected;	/* running with OutChannel redirect to transcript file */
EXTERN bool	DontExpandCnames;	/* do not $[...$] expand CNAMEs */
EXTERN bool	DontInitGroups;	/* avoid initgroups() because of NIS cost */
EXTERN bool	DontLockReadFiles;	/* don't read lock support files */
EXTERN bool	DontPruneRoutes;	/* don't prune source routes */
EXTERN bool	ForkQueueRuns;	/* fork for each job when running the queue */
EXTERN bool	FromFlag;	/* if set, "From" person is explicit */
EXTERN bool	FipsMode;
EXTERN bool	GrabTo;		/* if set, get recipients from msg */
EXTERN bool	EightBitAddrOK;	/* we'll let 8-bit addresses through */
EXTERN bool	HasEightBits;	/* has at least one eight bit input byte */
EXTERN bool	HasWildcardMX;	/* don't use MX records when canonifying */
EXTERN bool	HoldErrs;	/* only output errors to transcript */
EXTERN bool	IgnoreHostStatus;	/* ignore long term host status files */
EXTERN bool	IgnrDot;	/* don't let dot end messages */
EXTERN bool	LogUsrErrs;	/* syslog user errors (e.g., SMTP RCPT cmd) */
EXTERN bool	MatchGecos;	/* look for user names in gecos field */
EXTERN bool	MeToo;		/* send to the sender also */
EXTERN bool	NoAlias;	/* suppress aliasing */
EXTERN bool	NoConnect;	/* don't connect to non-local mailers */
EXTERN bool	OnlyOneError;	/*  .... or only want to give one SMTP reply */
EXTERN bool	QuickAbort;	/*  .... but only if we want a quick abort */
#if _FFR_REJECT_NUL_BYTE
EXTERN bool	RejectNUL;	/* reject NUL input byte? */
#endif /* _FFR_REJECT_NUL_BYTE */
#if REQUIRES_DIR_FSYNC
EXTERN bool	RequiresDirfsync;	/* requires fsync() for directory */
#endif /* REQUIRES_DIR_FSYNC */
EXTERN bool	volatile RestartWorkGroup; /* daemon needs to restart some work groups */
EXTERN bool	RrtImpliesDsn;	/* turn Return-Receipt-To: into DSN */
EXTERN bool	SaveFrom;	/* save leading "From" lines */
EXTERN bool	SendMIMEErrors;	/* send error messages in MIME format */
EXTERN bool	SevenBitInput;	/* force 7-bit data on input */
EXTERN bool	SingleLineFromHeader;	/* force From: header to be one line */
EXTERN bool	SingleThreadDelivery;	/* single thread hosts on delivery */
EXTERN bool	SoftBounce;	/* replace 5xy by 4xy (for testing) */
EXTERN bool	volatile StopRequest;	/* stop sending output */
EXTERN bool	SuprErrs;	/* set if we are suppressing errors */
EXTERN bool	TryNullMXList;	/* if we are the best MX, try host directly */
EXTERN bool	UseMSP;		/* mail submission: group writable queue ok? */
EXTERN bool	WorkAroundBrokenAAAA;	/* some nameservers return SERVFAIL on AAAA queries */
EXTERN bool	UseErrorsTo;	/* use Errors-To: header (back compat) */
EXTERN bool	UseNameServer;	/* using DNS -- interpret h_errno & MX RRs */
EXTERN bool	UseCompressedIPv6Addresses;	/* for more specific zero-subnet matches */
EXTERN char	InetMode;		/* default network for daemon mode */
EXTERN char	OpMode;		/* operation mode, see below */
EXTERN char	SpaceSub;	/* substitution for <lwsp> */
#if _FFR_BADRCPT_SHUTDOWN
EXTERN int	BadRcptShutdown; /* Shutdown connection for rejected RCPTs */
EXTERN int	BadRcptShutdownGood; /* above even when there are good RCPTs */
#endif /* _FFR_BADRCPT_SHUTDOWN */
EXTERN int	BadRcptThrottle; /* Throttle rejected RCPTs per SMTP message */
#if _FFR_RCPTTHROTDELAY
EXTERN unsigned int BadRcptThrottleDelay; /* delay for BadRcptThrottle */
#else
# define BadRcptThrottleDelay	1
#endif /* _FFR_RCPTTHROTDELAY */
EXTERN int	CheckpointInterval;	/* queue file checkpoint interval */
EXTERN int	ConfigLevel;	/* config file level */
EXTERN int	ConnRateThrottle;	/* throttle for SMTP connection rate */
EXTERN int	volatile CurChildren;	/* current number of daemonic children */
EXTERN int	CurrentLA;	/* current load average */
EXTERN int	DefaultNotify;	/* default DSN notification flags */
EXTERN int	DelayLA;	/* load average to delay connections */
EXTERN int	DontProbeInterfaces;	/* don't probe interfaces for names */
EXTERN int	Errors;		/* set if errors (local to single pass) */
EXTERN int	ExitStat;	/* exit status code */
EXTERN int	FastSplit;	/* fast initial splitting of envelopes */
EXTERN int	FileMode;	/* mode on files */
EXTERN int	LineNumber;	/* line number in current input */
EXTERN int	LogLevel;	/* level of logging to perform */
EXTERN int	MaxAliasRecursion;	/* maximum depth of alias recursion */
EXTERN int	MaxChildren;	/* maximum number of daemonic children */
EXTERN int	MaxForwardEntries;	/* maximum number of forward entries */
EXTERN int	MaxHeadersLength;	/* max length of headers */
EXTERN int	MaxHopCount;	/* max # of hops until bounce */
EXTERN int	MaxMacroRecursion;	/* maximum depth of macro recursion */
EXTERN int	MaxMimeFieldLength;	/* maximum MIME field length */
EXTERN int	MaxMimeHeaderLength;	/* maximum MIME header length */
EXTERN int	MaxNOOPCommands; /* max "noise" commands before slowdown */

EXTERN int	MaxRcptPerMsg;	/* max recipients per SMTP message */
EXTERN int	MaxRuleRecursion;	/* maximum depth of ruleset recursion */
#if _FFR_MSG_ACCEPT
EXTERN char	*MessageAccept; /* "Message accepted for delivery" reply text */
#endif /* _FFR_MSG_ACCEPT */

EXTERN int	MimeMode;	/* MIME processing mode */
EXTERN int	NoRecipientAction;

#if SM_CONF_SHM
EXTERN int	Numfilesys;	/* number of queue file systems */
EXTERN int	*PNumFileSys;
# define NumFileSys	(*PNumFileSys)
# else /* SM_CONF_SHM */
EXTERN int	NumFileSys;	/* number of queue file systems */
# endif /* SM_CONF_SHM */

EXTERN int	QueueLA;	/* load average starting forced queueing */
EXTERN int	RefuseLA;	/* load average refusing connections */
EXTERN time_t	RejectLogInterval;	/* time btwn log msgs while refusing */
#if _FFR_MEMSTAT
EXTERN long	QueueLowMem;	/* low memory starting forced queueing */
EXTERN long	RefuseLowMem;	/* low memory refusing connections */
EXTERN char	*MemoryResource;/* memory resource to look up */
#endif /* _FFR_MEMSTAT */
EXTERN int	SuperSafe;	/* be extra careful, even if expensive */
EXTERN int	VendorCode;	/* vendor-specific operation enhancements */
EXTERN int	Verbose;	/* set if blow-by-blow desired */
EXTERN gid_t	DefGid;		/* default gid to run as */
EXTERN gid_t	RealGid;	/* real gid of caller */
EXTERN gid_t	RunAsGid;	/* GID to become for bulk of run */
EXTERN gid_t	EffGid;		/* effective gid */
#if SM_CONF_SHM
EXTERN key_t	ShmKey;		/* shared memory key */
EXTERN char	*ShmKeyFile;	/* shared memory key file */
#endif /* SM_CONF_SHM */
EXTERN pid_t	CurrentPid;	/* current process id */
EXTERN pid_t	DaemonPid;	/* process id of daemon */
EXTERN pid_t	PidFilePid;	/* daemon/queue runner who wrote pid file */
EXTERN uid_t	DefUid;		/* default uid to run as */
EXTERN uid_t	RealUid;	/* real uid of caller */
EXTERN uid_t	RunAsUid;	/* UID to become for bulk of run */
EXTERN uid_t	TrustedUid;	/* uid of trusted user for files and startup */
EXTERN size_t	DataFileBufferSize;	/* size of buf for in-core data file */
EXTERN time_t	DeliverByMin;	/* deliver by minimum time */
EXTERN time_t	DialDelay;	/* delay between dial-on-demand tries */
EXTERN time_t	SafeAlias;	/* interval to wait until @:@ in alias file */
EXTERN time_t	ServiceCacheMaxAge;	/* refresh interval for cache */
EXTERN size_t	XscriptFileBufferSize;	/* size of buf for in-core transcript file */
EXTERN MODE_T	OldUmask;	/* umask when sendmail starts up */
EXTERN long	MaxMessageSize;	/* advertised max size we will accept */
EXTERN long	MinBlocksFree;	/* min # of blocks free on queue fs */
EXTERN long	QueueFactor;	/* slope of queue function */
EXTERN long	WkClassFact;	/* multiplier for message class -> priority */
EXTERN long	WkRecipFact;	/* multiplier for # of recipients -> priority */
EXTERN long	WkTimeFact;	/* priority offset each time this job is run */
EXTERN char	*ControlSocketName; /* control socket filename [control.c] */
EXTERN char	*CurHostName;	/* current host we are dealing with */
EXTERN char	*DeadLetterDrop;	/* path to dead letter office */
EXTERN char	*DefUser;	/* default user to run as (from DefUid) */
EXTERN char	*DefaultCharSet;	/* default character set for MIME */
EXTERN char	*DoubleBounceAddr;	/* where to send double bounces */
EXTERN char	*ErrMsgFile;	/* file to prepend to all error messages */
EXTERN char	*FallbackMX;	/* fall back MX host */
EXTERN char	*FallbackSmartHost;	/* fall back smart host */
EXTERN char	*FileName;	/* name to print on error messages */
EXTERN char	*ForwardPath;	/* path to search for .forward files */
EXTERN char	*HeloName;	/* hostname to announce in HELO */
EXTERN char	*HelpFile;	/* location of SMTP help file */
EXTERN char	*HostStatDir;	/* location of host status information */
EXTERN char	*HostsFile;	/* path to /etc/hosts file */
extern char	*Mbdb;		/* mailbox database type */
EXTERN char	*MustQuoteChars;	/* quote these characters in phrases */
EXTERN char	*MyHostName;	/* name of this host for SMTP messages */
EXTERN char	*OperatorChars;	/* operators (old $o macro) */
EXTERN char	*PidFile;	/* location of proc id file [conf.c] */
EXTERN char	*PostMasterCopy;	/* address to get errs cc's */
EXTERN char	*ProcTitlePrefix; /* process title prefix */
EXTERN char	*RealHostName;	/* name of host we are talking to */
EXTERN char	*RealUserName;	/* real user name of caller */
EXTERN char	*volatile RestartRequest;/* a sendmail restart has been requested */
EXTERN char	*RunAsUserName;	/* user to become for bulk of run */
EXTERN char	*SafeFileEnv;	/* chroot location for file delivery */
EXTERN char	*ServiceSwitchFile;	/* backup service switch */
EXTERN char	*volatile ShutdownRequest;/* a sendmail shutdown has been requested */
EXTERN bool	volatile IntSig;
EXTERN char	*SmtpGreeting;	/* SMTP greeting message (old $e macro) */
EXTERN char	*SmtpPhase;	/* current phase in SMTP processing */
EXTERN char	SmtpError[MAXLINE];	/* save failure error messages */
EXTERN char	*StatFile;	/* location of statistics summary */
EXTERN char	*TimeZoneSpec;	/* override time zone specification */
EXTERN char	*UdbSpec;	/* user database source spec */
EXTERN char	*UnixFromLine;	/* UNIX From_ line (old $l macro) */
EXTERN char	**ExternalEnviron;	/* saved user (input) environment */
EXTERN char	**SaveArgv;	/* argument vector for re-execing */
EXTERN BITMAP256	DontBlameSendmail;	/* DontBlameSendmail bits */
EXTERN SM_FILE_T	*InChannel;	/* input connection */
EXTERN SM_FILE_T	*OutChannel;	/* output connection */
EXTERN SM_FILE_T	*TrafficLogFile; /* file in which to log all traffic */
#if HESIOD
EXTERN void	*HesiodContext;
#endif /* HESIOD */
EXTERN ENVELOPE	*CurEnv;	/* envelope currently being processed */
EXTERN char	*RuleSetNames[MAXRWSETS];	/* ruleset number to name */
EXTERN char	*UserEnviron[MAXUSERENVIRON + 1];
EXTERN struct rewrite	*RewriteRules[MAXRWSETS];
EXTERN struct termescape	TermEscape;	/* terminal escape codes */
EXTERN SOCKADDR	ConnectOnlyTo;	/* override connection address (for testing) */
EXTERN SOCKADDR RealHostAddr;	/* address of host we are talking to */
extern const SM_EXC_TYPE_T EtypeQuickAbort; /* type of a QuickAbort exception */


EXTERN int ConnectionRateWindowSize;
#if STARTTLS && USE_OPENSSL_ENGINE
EXTERN bool	SSLEngineInitialized;
#endif /* STARTTLS && USE_OPENSSL_ENGINE */

/*
**  Declarations of useful functions
*/

/* Transcript file */
extern void	closexscript __P((ENVELOPE *));
extern void	openxscript __P((ENVELOPE *));

/* error related */
extern void	buffer_errors __P((void));
extern void	flush_errors __P((bool));
extern void PRINTFLIKE(1, 2)	message __P((const char *, ...));
extern void PRINTFLIKE(1, 2)	nmessage __P((const char *, ...));
#if _FFR_PROXY
extern void PRINTFLIKE(3, 4)	emessage __P((const char *, const char *, const char *, ...));
extern int extsc __P((const char *, int, char *, char *));
#endif /* _FFR_PROXY */
extern void PRINTFLIKE(1, 2)	syserr __P((const char *, ...));
extern void PRINTFLIKE(2, 3)	usrerrenh __P((char *, const char *, ...));
extern void PRINTFLIKE(1, 2)	usrerr __P((const char *, ...));
extern int	isenhsc __P((const char *, int));
extern int	extenhsc __P((const char *, int, char *));

/* alias file */
extern void	alias __P((ADDRESS *, ADDRESS **, int, ENVELOPE *));
extern bool	aliaswait __P((MAP *, char *, bool));
extern void	forward __P((ADDRESS *, ADDRESS **, int, ENVELOPE *));
extern void	readaliases __P((MAP *, SM_FILE_T *, bool, bool));
extern bool	rebuildaliases __P((MAP *, bool));
extern void	setalias __P((char *));

/* logging */
extern void	logdelivery __P((MAILER *, MCI *, char *, const char *, ADDRESS *, time_t, ENVELOPE *, ADDRESS *, int));
extern void	logsender __P((ENVELOPE *, char *));
extern void PRINTFLIKE(3, 4) sm_syslog __P((int, const char *, const char *, ...));

/* SMTP */
extern void	giveresponse __P((int, char *, MAILER *, MCI *, ADDRESS *, time_t, ENVELOPE *, ADDRESS *));
extern int	reply __P((MAILER *, MCI *, ENVELOPE *, time_t, void (*)__P((char *, bool, MAILER *, MCI *, ENVELOPE *)), char **, int));
extern void	smtp __P((char *volatile, BITMAP256, ENVELOPE *volatile));
#if SASL
extern int	smtpauth __P((MAILER *, MCI *, ENVELOPE *));
#endif /* SASL */
extern int	smtpdata __P((MAILER *, MCI *, ENVELOPE *, ADDRESS *, time_t));
extern int	smtpgetstat __P((MAILER *, MCI *, ENVELOPE *));
extern int	smtpmailfrom __P((MAILER *, MCI *, ENVELOPE *));
extern void	smtpmessage __P((char *, MAILER *, MCI *, ...));
extern void	smtpinit __P((MAILER *, MCI *, ENVELOPE *, bool));
extern char	*smtptodsn __P((int));
extern int	smtpprobe __P((MCI *));
extern void	smtpquit __P((MAILER *, MCI *, ENVELOPE *));
extern int	smtprcpt __P((ADDRESS *, MAILER *, MCI *, ENVELOPE *, ADDRESS *, time_t));
extern void	smtprset __P((MAILER *, MCI *, ENVELOPE *));

#define REPLYTYPE(r)	((r) / 100)		/* first digit of reply code */
#define REPLYCLASS(r)	(((r) / 10) % 10)	/* second digit of reply code */
#define REPLYMINOR(r)	((r) % 10)	/* last digit of reply code */
#define ISSMTPCODE(c)	(isascii(c[0]) && isdigit(c[0]) && \
		    isascii(c[1]) && isdigit(c[1]) && \
		    isascii(c[2]) && isdigit(c[2]))
#define ISSMTPREPLY(c)	(ISSMTPCODE(c) && \
		    (c[3] == ' ' || c[3] == '-' || c[3] == '\0'))

/* delivery */
extern pid_t	dowork __P((int, int, char *, bool, bool, ENVELOPE *));
extern pid_t	doworklist __P((ENVELOPE *, bool, bool));
extern int	endmailer __P((MCI *, ENVELOPE *, char **));
extern int	mailfile __P((char *volatile, MAILER *volatile, ADDRESS *, volatile long, ENVELOPE *));
extern void	sendall __P((ENVELOPE *, int));

/* stats */
#define STATS_NORMAL		'n'
#define STATS_QUARANTINE	'q'
#define STATS_REJECT		'r'
#define STATS_CONNECT		'c'

extern void	markstats __P((ENVELOPE *, ADDRESS *, int));
extern void	clearstats __P((void));
extern void	poststats __P((char *));

/* control socket */
extern void	closecontrolsocket  __P((bool));
extern void	clrcontrol  __P((void));
extern void	control_command __P((int, ENVELOPE *));
extern int	opencontrolsocket __P((void));

#if MILTER
/* milter functions */
extern void	milter_config __P((char *, struct milter **, int));
extern void	milter_setup __P((char *));
extern void	milter_set_option __P((char *, char *, bool));
extern bool	milter_init __P((ENVELOPE *, char *, milters_T *));
extern void	milter_quit __P((ENVELOPE *));
extern void	milter_abort __P((ENVELOPE *));
extern char	*milter_connect __P((char *, SOCKADDR, ENVELOPE *, char *));
extern char	*milter_helo __P((char *, ENVELOPE *, char *));
extern char	*milter_envfrom __P((char **, ENVELOPE *, char *));
extern char	*milter_data_cmd __P((ENVELOPE *, char *));
extern char	*milter_envrcpt __P((char **, ENVELOPE *, char *, bool));
extern char	*milter_data __P((ENVELOPE *, char *));
extern char	*milter_unknown __P((char *, ENVELOPE *, char *));
#endif /* MILTER */

extern char	*addquotes __P((char *, SM_RPOOL_T *));
extern char	*arpadate __P((char *));
extern bool	atobool __P((char *));
extern int	atooct __P((char *));
extern void	auth_warning __P((ENVELOPE *, const char *, ...));
extern int	blocksignal __P((int));
extern bool	bitintersect __P((BITMAP256, BITMAP256));
extern bool	bitzerop __P((BITMAP256));
extern int	check_bodytype __P((char *));
extern void	buildfname __P((char *, char *, char *, int));
extern bool	chkclientmodifiers __P((int));
extern bool	chkdaemonmodifiers __P((int));
extern int	checkcompat __P((ADDRESS *, ENVELOPE *));
#ifdef XDEBUG
extern void	checkfd012 __P((char *));
extern void	checkfdopen __P((int, char *));
#endif /* XDEBUG */
extern void	checkfds __P((char *));
extern bool	chownsafe __P((int, bool));
extern void	cleanstrcpy __P((char *, char *, int));
#if SM_CONF_SHM
extern void	cleanup_shm __P((bool));
#endif /* SM_CONF_SHM */
extern void	close_sendmail_pid __P((void));
extern void	clrdaemon __P((void));
extern void	collect __P((SM_FILE_T *, bool, HDR **, ENVELOPE *, bool));
extern bool	connection_rate_check __P((SOCKADDR *, ENVELOPE *));
extern time_t	convtime __P((char *, int));
extern char	**copyplist __P((char **, bool, SM_RPOOL_T *));
extern void	copy_class __P((int, int));
extern int	count_open_connections __P((SOCKADDR *));
extern time_t	curtime __P((void));
extern char	*defcharset __P((ENVELOPE *));
extern char	*denlstring __P((char *, bool, bool));
extern void	dferror __P((SM_FILE_T *volatile, char *, ENVELOPE *));
extern void	disconnect __P((int, ENVELOPE *));
extern void	disk_status __P((SM_FILE_T *, char *));
extern bool	dns_getcanonname __P((char *, int, bool, int *, int *));
extern pid_t	dofork __P((void));
extern int	drop_privileges __P((bool));
extern int	dsntoexitstat __P((char *));
extern void	dumpfd __P((int, bool, bool));
#if SM_HEAP_CHECK
extern void	dumpstab __P((void));
#endif /* SM_HEAP_CHECK */
extern void	dumpstate __P((char *));
extern bool	enoughdiskspace __P((long, ENVELOPE *));
extern char	*exitstat __P((char *));
extern void	fatal_error __P((SM_EXC_T *));
extern char	*fgetfolded __P((char *, int *, SM_FILE_T *));
extern void	fill_fd __P((int, char *));
extern char	*find_character __P((char *, int));
extern int	finduser __P((char *, bool *, SM_MBDB_T *));
extern void	finis __P((bool, bool, volatile int));
extern void	fixcrlf __P((char *, bool));
extern long	freediskspace __P((const char *, long *));
#if NETINET6 && NEEDSGETIPNODE
extern void	freehostent __P((struct hostent *));
#endif /* NETINET6 && NEEDSGETIPNODE */
extern char	*get_column __P((char *, int, int, char *, int));
extern char	*getauthinfo __P((int, bool *));
extern int	getdtsize __P((void));
extern int	getla __P((void));
extern char	*getmodifiers __P((char *, BITMAP256));
extern BITMAP256	*getrequests __P((ENVELOPE *));
extern char	*getvendor __P((int));
#if _FFR_TLS_SE_OPTS && STARTTLS
# ifndef TLS_VRFY_PER_CTX
#  define TLS_VRFY_PER_CTX 1
# endif
extern int	get_tls_se_options __P((ENVELOPE *, SSL *, bool));
#else
# define get_tls_se_options(e, s, w)	0
#endif
extern void	help __P((char *, ENVELOPE *));
extern void	init_md __P((int, char **));
extern void	initdaemon __P((void));
extern void	inithostmaps __P((void));
extern void	initmacros __P((ENVELOPE *));
extern void	initsetproctitle __P((int, char **, char **));
extern void	init_vendor_macros __P((ENVELOPE *));
extern SIGFUNC_DECL	intsig __P((int));
extern bool	isatom __P((const char *));
extern bool	isloopback __P((SOCKADDR sa));
#if _FFR_TLS_SE_OPTS && STARTTLS
extern bool	load_certkey __P((SSL *, bool, char *, char *));
#endif
extern void	load_if_names __P((void));
extern bool	lockfile __P((int, char *, char *, int));
extern void	log_sendmail_pid __P((ENVELOPE *));
extern void	logundelrcpts __P((ENVELOPE *, char *, int, bool));
extern char	lower __P((int));
extern void	makelower __P((char *));
extern int	makeconnection_ds __P((char *, MCI *));
extern int	makeconnection __P((char *, volatile unsigned int, MCI *, ENVELOPE *, time_t));
extern void	makeworkgroups __P((void));
extern void	markfailure __P((ENVELOPE *, ADDRESS *, MCI *, int, bool));
extern void	mark_work_group_restart __P((int, int));
extern MCI	*mci_new __P((SM_RPOOL_T *));
extern char	*munchstring __P((char *, char **, int));
extern struct hostent	*myhostname __P((char *, int));
extern char	*newstr __P((const char *));
#if NISPLUS
extern char	*nisplus_default_domain __P((void));	/* extern for Sun */
#endif /* NISPLUS */
extern bool	path_is_dir __P((char *, bool));
extern int	pickqdir __P((QUEUEGRP *qg, long fsize, ENVELOPE *e));
extern char	*pintvl __P((time_t, bool));
extern void	printav __P((SM_FILE_T *, char **));
extern void	printmailer __P((SM_FILE_T *, MAILER *));
extern void	printnqe __P((SM_FILE_T *, char *));
extern void	printopenfds __P((bool));
extern void	printqueue __P((void));
extern void	printrules __P((void));
extern pid_t	prog_open __P((char **, int *, ENVELOPE *));
extern bool	putline __P((char *, MCI *));
extern bool	putxline __P((char *, size_t, MCI *, int));
extern void	queueup_macros __P((int, SM_FILE_T *, ENVELOPE *));
extern void	readcf __P((char *, bool, ENVELOPE *));
extern SIGFUNC_DECL	reapchild __P((int));
extern int	releasesignal __P((int));
extern void	resetlimits __P((void));
extern void	restart_daemon __P((void));
extern void	restart_marked_work_groups __P((void));
extern bool	rfc822_string __P((char *));
extern void	rmexpstab __P((void));
extern bool	savemail __P((ENVELOPE *, bool));
extern void	seed_random __P((void));
extern void	sendtoargv __P((char **, ENVELOPE *));
extern void	setclientoptions __P((char *));
extern bool	setdaemonoptions __P((char *));
extern void	setdefaults __P((ENVELOPE *));
extern void	setdefuser __P((void));
extern bool	setvendor __P((char *));
extern void	set_op_mode __P((int));
extern void	setoption __P((int, char *, bool, bool, ENVELOPE *));
extern sigfunc_t	setsignal __P((int, sigfunc_t));
extern void	sm_setuserenv __P((const char *, const char *));
extern void	settime __P((ENVELOPE *));
#if STARTTLS
extern int	set_tls_rd_tmo __P((int));
#else
# define set_tls_rd_tmo(rd_tmo)	0
#endif
extern char	*sfgets __P((char *, int, SM_FILE_T *, time_t, char *));
extern char	*shortenstring __P((const char *, size_t));
extern char	*shorten_hostname __P((char []));
extern bool	shorten_rfc822_string __P((char *, size_t));
extern void	shutdown_daemon __P((void));
extern void	sm_closefrom __P((int lowest, int highest));
extern void	sm_close_on_exec __P((int lowest, int highest));
extern struct hostent	*sm_gethostbyname __P((char *, int));
extern struct hostent	*sm_gethostbyaddr __P((char *, int, int));
extern void	sm_getla __P((void));
extern struct passwd	*sm_getpwnam __P((char *));
extern struct passwd	*sm_getpwuid __P((UID_T));
extern void	sm_setproctitle __P((bool, ENVELOPE *, const char *, ...));
extern pid_t	sm_wait __P((int *));
extern bool	split_by_recipient __P((ENVELOPE *e));
extern void	stop_sendmail __P((void));
extern void	stripbackslash __P((char *));
extern bool	strreplnonprt __P((char *, int));
extern bool	strcontainedin __P((bool, char *, char *));
extern int	switch_map_find __P((char *, char *[], short []));
#if STARTTLS
extern void	tls_set_verify __P((SSL_CTX *, SSL *, bool));
#endif /* STARTTLS */
extern bool	transienterror __P((int));
extern void	truncate_at_delim __P((char *, size_t, int));
extern void	tTflag __P((char *));
extern void	tTsetup __P((unsigned char *, unsigned int, char *));
extern SIGFUNC_DECL	tick __P((int));
extern char	*ttypath __P((void));
extern void	unlockqueue __P((ENVELOPE *));
#if !HASUNSETENV
extern void	unsetenv __P((char *));
#endif /* !HASUNSETENV */

/* update file system information: +/- some blocks */
#if SM_CONF_SHM
extern void	upd_qs __P((ENVELOPE *, int, int, char *));
# define updfs(e, count, space, where) upd_qs(e, count, space, where)
#else /* SM_CONF_SHM */
# define updfs(e, count, space, where)
# define upd_qs(e, count, space, where)
#endif /* SM_CONF_SHM */

extern char	*username __P((void));
extern bool	usershellok __P((char *, char *));
extern void	vendor_post_defaults __P((ENVELOPE *));
extern void	vendor_pre_defaults __P((ENVELOPE *));
extern int	waitfor __P((pid_t));
extern bool	writable __P((char *, ADDRESS *, long));
#if SM_HEAP_CHECK
# define xalloc(size)	xalloc_tagged(size, __FILE__, __LINE__)
extern char *xalloc_tagged __P((int, char *, int));
#else /* SM_HEAP_CHECK */
extern char *xalloc __P((int));
#endif /* SM_HEAP_CHECK */
#if _FFR_XCNCT
extern int xconnect __P((SM_FILE_T *));
#endif /* _FFR_XCNCT */
extern void	xputs __P((SM_FILE_T *, const char *));
extern char	*xtextify __P((char *, char *));
extern bool	xtextok __P((char *));
extern int	xunlink __P((char *));
extern char	*xuntextify __P((char *));

#if _FFR_RCPTFLAGS
extern bool	newmodmailer __P((ADDRESS *, int));
#endif

#undef EXTERN
#endif /* ! _SENDMAIL_H */
