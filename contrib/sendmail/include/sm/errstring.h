/*
 * Copyright (c) 1998-2001, 2003 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: errstring.h,v 1.11 2013-11-22 20:51:31 ca Exp $
 */

/*
**  Error codes.
*/

#ifndef SM_ERRSTRING_H
# define SM_ERRSTRING_H

#if defined(__QNX__)
# define E_PSEUDOBASE	512
#endif /* defined(__QNX__) */

#include <errno.h>
#if NEEDINTERRNO
extern int errno;
#endif /* NEEDINTERRNO */

/*
**  These are used in a few cases where we need some special
**  error codes, but where the system doesn't provide something
**  reasonable.  They are printed in sm_errstring.
*/

#ifndef E_PSEUDOBASE
# define E_PSEUDOBASE	256
#endif /* ! E_PSEUDOBASE */

#define E_SM_OPENTIMEOUT (E_PSEUDOBASE + 0)	/* Timeout on file open */
#define E_SM_NOSLINK	(E_PSEUDOBASE + 1)	/* Symbolic links not allowed */
#define E_SM_NOHLINK	(E_PSEUDOBASE + 2)	/* Hard links not allowed */
#define E_SM_REGONLY	(E_PSEUDOBASE + 3)	/* Regular files only */
#define E_SM_ISEXEC	(E_PSEUDOBASE + 4)	/* Executable files not allowed */
#define E_SM_WWDIR	(E_PSEUDOBASE + 5)	/* World writable directory */
#define E_SM_GWDIR	(E_PSEUDOBASE + 6)	/* Group writable directory */
#define E_SM_FILECHANGE (E_PSEUDOBASE + 7)	/* File changed after open */
#define E_SM_WWFILE	(E_PSEUDOBASE + 8)	/* World writable file */
#define E_SM_GWFILE	(E_PSEUDOBASE + 9)	/* Group writable file */
#define E_SM_GRFILE	(E_PSEUDOBASE + 10)	/* g readable file */
#define E_SM_WRFILE	(E_PSEUDOBASE + 11)	/* o readable file */
#define E_DNSBASE	(E_PSEUDOBASE + 20)	/* base for DNS h_errno */
#define E_SMDBBASE	(E_PSEUDOBASE + 40)	/* base for libsmdb errors */
#define E_LDAPREALBASE	(E_PSEUDOBASE + 70)	/* start of range for LDAP */
#define E_LDAPBASE	(E_LDAPREALBASE + E_LDAP_SHIM)	/* LDAP error zero */
#define E_LDAPURLBASE	(E_PSEUDOBASE + 230)	/* base for LDAP URL errors */

/*
**  OpenLDAP uses small negative errors for internal (non-protocol)
**  errors.  We expect them to be between zero and -E_LDAP_SHIM
**  (and then offset by E_LDAPBASE).
*/

#define E_LDAP_SHIM	30

/* libsmdb */
#define SMDBE_OK			0
#define SMDBE_MALLOC			(E_SMDBBASE + 1)
#define SMDBE_GDBM_IS_BAD		(E_SMDBBASE + 2)
#define SMDBE_UNSUPPORTED		(E_SMDBBASE + 3)
#define SMDBE_DUPLICATE			(E_SMDBBASE + 4)
#define SMDBE_BAD_OPEN			(E_SMDBBASE + 5)
#define SMDBE_NOT_FOUND			(E_SMDBBASE + 6)
#define SMDBE_UNKNOWN_DB_TYPE		(E_SMDBBASE + 7)
#define SMDBE_UNSUPPORTED_DB_TYPE	(E_SMDBBASE + 8)
#define SMDBE_INCOMPLETE		(E_SMDBBASE + 9)
#define SMDBE_KEY_EMPTY			(E_SMDBBASE + 10)
#define SMDBE_KEY_EXIST			(E_SMDBBASE + 11)
#define SMDBE_LOCK_DEADLOCK		(E_SMDBBASE + 12)
#define SMDBE_LOCK_NOT_GRANTED		(E_SMDBBASE + 13)
#define SMDBE_LOCK_NOT_HELD		(E_SMDBBASE + 14)
#define SMDBE_RUN_RECOVERY		(E_SMDBBASE + 15)
#define SMDBE_IO_ERROR			(E_SMDBBASE + 16)
#define SMDBE_READ_ONLY			(E_SMDBBASE + 17)
#define SMDBE_DB_NAME_TOO_LONG		(E_SMDBBASE + 18)
#define SMDBE_INVALID_PARAMETER		(E_SMDBBASE + 19)
#define SMDBE_ONLY_SUPPORTS_ONE_CURSOR	(E_SMDBBASE + 20)
#define SMDBE_NOT_A_VALID_CURSOR	(E_SMDBBASE + 21)
#define SMDBE_LAST_ENTRY		(E_SMDBBASE + 22)
#define SMDBE_OLD_VERSION		(E_SMDBBASE + 23)
#define SMDBE_VERSION_MISMATCH		(E_SMDBBASE + 24)

extern const char *sm_errstring __P((int _errno));


#endif /* SM_ERRSTRING_H */
