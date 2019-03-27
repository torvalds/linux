/*-
 * Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *
 *	$Id: pathnames.h,v 8.37 2013-11-22 20:51:30 ca Exp $
 */

#ifndef SM_PATHNAMES_H
# define SM_PATHNAMES_H


#  ifndef _PATH_SENDMAILCF
#   if defined(USE_VENDOR_CF_PATH) && defined(_PATH_VENDOR_CF)
#    define _PATH_SENDMAILCF	_PATH_VENDOR_CF
#   else /* defined(USE_VENDOR_CF_PATH) && defined(_PATH_VENDOR_CF) */
#    define _PATH_SENDMAILCF	"/etc/mail/sendmail.cf"
#   endif /* defined(USE_VENDOR_CF_PATH) && defined(_PATH_VENDOR_CF) */
#  endif /* ! _PATH_SENDMAILCF */

#  ifndef _PATH_SENDMAILPID
#   ifdef BSD4_4
#    define _PATH_SENDMAILPID	"/var/run/sendmail.pid"
#   else /* BSD4_4 */
#    define _PATH_SENDMAILPID	"/etc/mail/sendmail.pid"
#   endif /* BSD4_4 */
#  endif /* ! _PATH_SENDMAILPID */

#  ifndef _PATH_SENDMAIL
#   define _PATH_SENDMAIL 	"/usr/lib/sendmail"
#  endif /* ! _PATH_SENDMAIL */

#  ifndef _PATH_MAILDIR
#   define _PATH_MAILDIR	"/var/spool/mail"
#  endif /* ! _PATH_MAILDIR */

#  ifndef _PATH_LOCTMP
#   define _PATH_LOCTMP		"/tmp/local.XXXXXX"
#  endif /* ! _PATH_LOCTMP */

#  ifndef _PATH_HOSTS
#   define _PATH_HOSTS		"/etc/hosts"
#  endif /* ! _PATH_HOSTS */



#  ifndef _DIR_SENDMAILCF
#   define _DIR_SENDMAILCF	"/etc/mail/"
#  endif /* ! _DIR_SENDMAILCF */

# define SM_GET_RIGHT_CF	0	/* get "right" .cf */
# define SM_GET_SENDMAIL_CF	1	/* always use sendmail.cf */
# define SM_GET_SUBMIT_CF	2	/* always use submit.cf */

extern char	*getcfname __P((int, int, int, char *));
#endif /* ! SM_PATHNAMES_H */
