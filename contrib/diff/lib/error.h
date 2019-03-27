/* Declaration for error-reporting function
   Copyright (C) 1995, 1996, 1997, 2003 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ifndef _ERROR_H
#define _ERROR_H 1

#ifndef __attribute__
/* This feature is available in gcc versions 2.5 and later.  */
# if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5)
#  define __attribute__(Spec) /* empty */
# endif
/* The __-protected variants of `format' and `printf' attributes
   are accepted by gcc versions 2.6.4 (effectively 2.7) and later.  */
# if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 7)
#  define __format__ format
#  define __printf__ printf
# endif
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* Print a message with `fprintf (stderr, FORMAT, ...)';
   if ERRNUM is nonzero, follow it with ": " and strerror (ERRNUM).
   If STATUS is nonzero, terminate the program with `exit (STATUS)'.  */

extern void error (int __status, int __errnum, const char *__format, ...)
     __attribute__ ((__format__ (__printf__, 3, 4)));

extern void error_at_line (int __status, int __errnum, const char *__fname,
			   unsigned int __lineno, const char *__format, ...)
     __attribute__ ((__format__ (__printf__, 5, 6)));

/* If NULL, error will flush stdout, then print on stderr the program
   name, a colon and a space.  Otherwise, error will call this
   function without parameters instead.  */
extern void (*error_print_progname) (void);

/* This variable is incremented each time `error' is called.  */
extern unsigned int error_message_count;

/* Sometimes we want to have at most one error per line.  This
   variable controls whether this mode is selected or not.  */
extern int error_one_per_line;

#ifdef	__cplusplus
}
#endif

#endif /* error.h */
