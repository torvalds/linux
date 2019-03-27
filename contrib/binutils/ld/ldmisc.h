/* ldmisc.h -
   Copyright 1991, 1992, 1993, 1994, 1996, 1997, 2001, 2003, 2004, 2007
   Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef LDMISC_H
#define LDMISC_H

extern void einfo (const char *, ...);
extern void minfo (const char *, ...);
extern void info_msg (const char *, ...);
extern void lfinfo (FILE *, const char *, ...);
extern void info_assert (const char *, unsigned int);
extern void yyerror (const char *);
extern void *xmalloc (size_t);
extern void *xrealloc (void *, size_t);
extern void xexit (int);

#define ASSERT(x) \
do { if (!(x)) info_assert(__FILE__,__LINE__); } while (0)

#define FAIL() \
do { info_assert(__FILE__,__LINE__); } while (0)

extern void print_space (void);
extern void print_nl (void);

#endif
