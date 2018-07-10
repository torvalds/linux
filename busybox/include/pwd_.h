/* vi: set sw=4 ts=4: */
/* Copyright (C) 1991,92,95,96,97,98,99,2001 Free Software Foundation, Inc.
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

/*
 * POSIX Standard: 9.2.2 User Database Access	<pwd.h>
 */

#ifndef BB_PWD_H
#define BB_PWD_H 1

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

/* This file is #included after #include <pwd.h>
 * We will use libc-defined structures, but will #define function names
 * so that function calls are directed to bb_internal_XXX replacements
 */
#undef endpwent
#define setpwent    bb_internal_setpwent
#define endpwent    bb_internal_endpwent
#define getpwent    bb_internal_getpwent
#define getpwuid    bb_internal_getpwuid
#define getpwnam    bb_internal_getpwnam
#define getpwnam_r  bb_internal_getpwnam_r

/* All function names below should be remapped by #defines above
 * in order to not collide with libc names. */

/* Rewind the password-file stream.  */
void FAST_FUNC setpwent(void);

/* Close the password-file stream.  */
void FAST_FUNC endpwent(void);

/* Read an entry from the password-file stream, opening it if necessary.  */
struct passwd* FAST_FUNC getpwent(void);

/* Search for an entry with a matching user ID.  */
struct passwd* FAST_FUNC getpwuid(uid_t __uid);

/* Search for an entry with a matching username.  */
struct passwd* FAST_FUNC getpwnam(const char *__name);

/* Reentrant versions of some of the functions above. */
int FAST_FUNC getpwnam_r(const char *__restrict __name,
		struct passwd *__restrict __resultbuf,
		char *__restrict __buffer, size_t __buflen,
		struct passwd **__restrict __result);

POP_SAVED_FUNCTION_VISIBILITY

#endif
