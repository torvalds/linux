/* vi: set sw=4 ts=4: */
/* Copyright (C) 1996, 1997, 1998, 1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

/* Declaration of types and functions for shadow password suite */

#ifndef BB_SHADOW_H
#define BB_SHADOW_H 1

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

/* Structure of the password file */
struct spwd {
	char *sp_namp;          /* Login name */
	char *sp_pwdp;          /* Encrypted password */
	long sp_lstchg;         /* Date of last change */
	long sp_min;            /* Minimum number of days between changes */
	long sp_max;            /* Maximum number of days between changes */
	long sp_warn;           /* Number of days to warn user to change the password */
	long sp_inact;          /* Number of days the account may be inactive */
	long sp_expire;         /* Number of days since 1970-01-01 until account expires */
	unsigned long sp_flag;  /* Reserved */
};

#define setspent    bb_internal_setspent
#define endspent    bb_internal_endspent
#define getspent    bb_internal_getspent
#define getspnam    bb_internal_getspnam
#define sgetspent   bb_internal_sgetspent
#define fgetspent   bb_internal_fgetspent
#define putspent    bb_internal_putspent
#define getspent_r  bb_internal_getspent_r
#define getspnam_r  bb_internal_getspnam_r
#define sgetspent_r bb_internal_sgetspent_r
#define fgetspent_r bb_internal_fgetspent_r
#define lckpwdf     bb_internal_lckpwdf
#define ulckpwdf    bb_internal_ulckpwdf


/* All function names below should be remapped by #defines above
 * in order to not collide with libc names. */

#ifdef UNUSED_FOR_NOW
/* Open database for reading */
void FAST_FUNC setspent(void);

/* Close database */
void FAST_FUNC endspent(void);

/* Get next entry from database, perhaps after opening the file */
struct spwd* FAST_FUNC getspent(void);

/* Get shadow entry matching NAME */
struct spwd* FAST_FUNC getspnam(const char *__name);

/* Read shadow entry from STRING */
struct spwd* FAST_FUNC sgetspent(const char *__string);

/* Read next shadow entry from STREAM */
struct spwd* FAST_FUNC fgetspent(FILE *__stream);

/* Write line containing shadow password entry to stream */
int FAST_FUNC putspent(const struct spwd *__p, FILE *__stream);

/* Reentrant versions of some of the functions above */
int FAST_FUNC getspent_r(struct spwd *__result_buf, char *__buffer,
		size_t __buflen, struct spwd **__result);
#endif

int FAST_FUNC getspnam_r(const char *__name, struct spwd *__result_buf,
		char *__buffer, size_t __buflen,
		struct spwd **__result);

#ifdef UNUSED_FOR_NOW
int FAST_FUNC sgetspent_r(const char *__string, struct spwd *__result_buf,
		char *__buffer, size_t __buflen,
		struct spwd **__result);

int FAST_FUNC fgetspent_r(FILE *__stream, struct spwd *__result_buf,
		char *__buffer, size_t __buflen,
		struct spwd **__result);
/* Protect password file against multi writers */
int FAST_FUNC lckpwdf(void);

/* Unlock password file */
int FAST_FUNC ulckpwdf(void);
#endif

POP_SAVED_FUNCTION_VISIBILITY

#endif /* shadow.h */
