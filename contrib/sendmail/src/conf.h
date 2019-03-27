/*
 * Copyright (c) 1998-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *
 *	$Id: conf.h,v 8.577 2013-11-22 20:51:55 ca Exp $
 */

/*
**  CONF.H -- All user-configurable parameters for sendmail
**
**	Send updates to sendmail@Sendmail.ORG so they will be
**	included in the next release.
*/

#ifndef CONF_H
#define CONF_H 1

#ifdef __GNUC__
struct rusage;	/* forward declaration to get gcc to shut up in wait.h */
#endif /* __GNUC__ */

# include <sys/param.h>
# include <sys/types.h>
# include <sys/stat.h>
# ifndef __QNX__
/* in QNX this grabs bogus LOCK_* manifests */
#  include <sys/file.h>
# endif /* ! __QNX__ */
# include <sys/wait.h>
# include <limits.h>
# include <fcntl.h>
# include <signal.h>
# include <netdb.h>
# include <pwd.h>
# include <grp.h>

/* make sure TOBUFSIZ isn't larger than system limit for size of exec() args */
#ifdef ARG_MAX
# if ARG_MAX > 4096
#  define SM_ARG_MAX	4096
# else /* ARG_MAX > 4096 */
#  define SM_ARG_MAX	ARG_MAX
# endif /* ARG_MAX > 4096 */
#else /* ARG_MAX */
# define SM_ARG_MAX	4096
#endif /* ARG_MAX */

/**********************************************************************
**  Table sizes, etc....
**	There shouldn't be much need to change these....
**	If you do, be careful, none should be set anywhere near INT_MAX
**********************************************************************/

#define MAXLINE		2048	/* max line length */
#if SASL
# define MAXINPLINE	12288	/* max input line length (for AUTH) */
#else /* SASL */
# define MAXINPLINE	MAXLINE	/* max input line length */
#endif /* SASL */
#define MAXNAME		256	/* max length of a name */
#ifndef MAXAUTHINFO
# define MAXAUTHINFO	100	/* max length of authinfo token */
#endif /* ! MAXAUTHINFO */
#define MAXPV		256	/* max # of parms to mailers */
#define MAXATOM		1000	/* max atoms per address */
#define MAXRWSETS	200	/* max # of sets of rewriting rules */
#define MAXPRIORITIES	25	/* max values for Precedence: field */
#define MAXMXHOSTS	100	/* max # of MX records for one host */
#define SMTPLINELIM	990	/* max SMTP line length */
#define MAXUDBKEY	128	/* max size of a database key (udb only) */
#define MAXKEY		1024	/* max size of a database key */
#define MEMCHUNKSIZE	1024	/* chunk size for memory allocation */
#define MAXUSERENVIRON	100	/* max envars saved, must be >= 3 */
#define MAXMAPSTACK	12	/* max # of stacked or sequenced maps */
#if MILTER
# define MAXFILTERS	25	/* max # of milter filters */
# define MAXFILTERMACROS 50	/* max # of macros per milter cmd */
#endif /* MILTER */
#define MAXSMTPARGS	20	/* max # of ESMTP args for MAIL/RCPT */
#define MAXTOCLASS	8	/* max # of message timeout classes */
#define MAXRESTOTYPES	3	/* max # of resolver timeout types */
#define MAXMIMEARGS	20	/* max args in Content-Type: */
#define MAXMIMENESTING	20	/* max MIME multipart nesting */
#define QUEUESEGSIZE	1000	/* increment for queue size */

#ifndef MAXNOOPCOMMANDS
# define MAXNOOPCOMMANDS 20	/* max "noise" commands before slowdown */
#endif /* ! MAXNOOPCOMMANDS */

/*
**  MAXQFNAME == 2 (size of "qf", "df" prefix)
**	+ 8 (base 60 encoded date, time & sequence number)
**	+ 10 (base 10 encoded 32 bit process id)
**	+ 1 (terminating NUL character).
*/

#define MAXQFNAME	21		/* max qf file name length + 1 */
#define MACBUFSIZE	4096		/* max expanded macro buffer size */
#define TOBUFSIZE	SM_ARG_MAX	/* max buffer to hold address list */
#define MAXSHORTSTR	203		/* max short string length */
#define MAXMACNAMELEN	25		/* max macro name length */
#define MAXMACROID	0377		/* max macro id number */
					/* Must match (BITMAPBITS - 1) */
#ifndef MAXHDRSLEN
# define MAXHDRSLEN	(32 * 1024)	/* max size of message headers */
#endif /* ! MAXHDRSLEN */
#ifndef MAXDAEMONS
# define MAXDAEMONS	10		/* max number of ports to listen to */
#endif /* MAXDAEMONS */
#ifndef MAXINTERFACES
# define MAXINTERFACES	512		/* number of interfaces to probe */
#endif /* MAXINTERFACES */
#ifndef MAXSYMLINKS
# define MAXSYMLINKS	32		/* max number of symlinks in a path */
#endif /* ! MAXSYMLINKS */
#define MAXLINKPATHLEN	(MAXPATHLEN * MAXSYMLINKS) /* max link-expanded file */
#define DATA_PROGRESS_TIMEOUT	300	/* how often to check DATA progress */
#define ENHSCLEN	10		/* max len of enhanced status code */
#define DEFAULT_MAX_RCPT	100	/* max number of RCPTs per envelope */
#ifndef MAXQUEUEGROUPS
# define MAXQUEUEGROUPS	50		/* max # of queue groups */
	/* must be less than BITMAPBITS for DoQueueRun */
#endif /* MAXQUEUEGROUPS */
#if MAXQUEUEGROUPS >= BITMAPBITS
  ERROR _MAXQUEUEGROUPS must be less than _BITMAPBITS
#endif /* MAXQUEUEGROUPS >= BITMAPBITS */

#ifndef MAXWORKGROUPS
# define MAXWORKGROUPS	50		/* max # of work groups */
#endif /* MAXWORKGROUPS */

#define MAXFILESYS	BITMAPBITS	/* max # of queue file systems
					 * must be <= BITMAPBITS */
#ifndef FILESYS_UPDATE_INTERVAL
# define FILESYS_UPDATE_INTERVAL 300	/* how often to update FileSys table */
#endif /* FILESYS_UPDATE_INTERVAL */

#ifndef SM_DEFAULT_TTL
# define SM_DEFAULT_TTL 3600 /* default TTL for services that don't have one */
#endif /* SM_DEFAULT_TTL */

#if SASL
# ifndef AUTH_MECHANISMS
#  if STARTTLS
#   define AUTH_MECHANISMS	"EXTERNAL GSSAPI KERBEROS_V4 DIGEST-MD5 CRAM-MD5"
#  else /* STARTTLS */
#   define AUTH_MECHANISMS	"GSSAPI KERBEROS_V4 DIGEST-MD5 CRAM-MD5"
#  endif /* STARTTLS */
# endif /* ! AUTH_MECHANISMS */
#endif /* SASL */

/*
**  Default database permissions (alias, maps, etc.)
**	Used by sendmail and libsmdb
*/

#ifndef DBMMODE
# define DBMMODE	0640
#endif /* ! DBMMODE */

/*
**  Value which means a uid or gid value should not change
*/

#ifndef NO_UID
# define NO_UID		-1
#endif /* ! NO_UID */
#ifndef NO_GID
# define NO_GID		-1
#endif /* ! NO_GID */

/**********************************************************************
**  Compilation options.
**	#define these to 1 if they are available;
**	#define them to 0 otherwise.
**  All can be overridden from Makefile.
**********************************************************************/

#ifndef NETINET
# define NETINET	1	/* include internet support */
#endif /* ! NETINET */

#ifndef NETINET6
# define NETINET6	0	/* do not include IPv6 support */
#endif /* ! NETINET6 */

#ifndef NETISO
# define NETISO	0		/* do not include ISO socket support */
#endif /* ! NETISO */

#ifndef NAMED_BIND
# define NAMED_BIND	1	/* use Berkeley Internet Domain Server */
#endif /* ! NAMED_BIND */

#ifndef XDEBUG
# define XDEBUG		1	/* enable extended debugging */
#endif /* ! XDEBUG */

#ifndef MATCHGECOS
# define MATCHGECOS	1	/* match user names from gecos field */
#endif /* ! MATCHGECOS */

#ifndef DSN
# define DSN		1	/* include delivery status notification code */
#endif /* ! DSN */

#if !defined(USERDB) && (defined(NEWDB) || defined(HESIOD))
# define USERDB		1	/* look in user database */
#endif /* !defined(USERDB) && (defined(NEWDB) || defined(HESIOD)) */

#ifndef MIME8TO7
# define MIME8TO7	1	/* 8->7 bit MIME conversions */
#endif /* ! MIME8TO7 */

#ifndef MIME7TO8
# define MIME7TO8	1	/* 7->8 bit MIME conversions */
#endif /* ! MIME7TO8 */

#if NAMED_BIND
# ifndef DNSMAP
#  define DNSMAP	1	/* DNS map type */
# endif /* ! DNSMAP */
#endif /* NAMED_BIND */

#ifndef PIPELINING
# define PIPELINING	1	/* SMTP PIPELINING */
#endif /* PIPELINING */

/**********************************************************************
**  End of site-specific configuration.
**********************************************************************/

#include <sm/conf.h>

#endif /* ! CONF_H */
