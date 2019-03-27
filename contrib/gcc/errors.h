/* Basic error reporting routines.
   Copyright (C) 1999, 2000, 2001, 2003, 2004, 2005
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* warning, error, and fatal.  These definitions are suitable for use
   in the generator programs; eventually we would like to use them in
   cc1 too, but that's a longer term project.

   N.B. We cannot presently use ATTRIBUTE_PRINTF with these functions,
   because they can be extended with additional format specifiers which
   GCC does not know about.  */

#ifndef GCC_ERRORS_H
#define GCC_ERRORS_H

/* The first parameter is for compatibility with the non-generator
   version of warning().  For those, you'd pass an OPT_W* value from
   options.h, but in generator programs it has no effect, so it's OK
   to just pass zero for calls from generator-only files.  */
extern void warning (int, const char *, ...) ATTRIBUTE_PRINTF_2;
extern void error (const char *, ...) ATTRIBUTE_PRINTF_1;
extern void fatal (const char *, ...) ATTRIBUTE_NORETURN ATTRIBUTE_PRINTF_1;
extern void internal_error (const char *, ...) ATTRIBUTE_NORETURN ATTRIBUTE_PRINTF_1;
extern const char *trim_filename (const char *);

extern int have_error;
extern const char *progname;

#endif /* ! GCC_ERRORS_H */
