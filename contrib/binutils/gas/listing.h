/* This file is listing.h
   Copyright 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1995, 1997, 1998,
   2003 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef __listing_h__
#define __listing_h__

#define LISTING_LISTING    1
#define LISTING_SYMBOLS    2
#define LISTING_NOFORM     4
#define LISTING_HLL        8
#define LISTING_NODEBUG   16
#define LISTING_NOCOND	  32
#define LISTING_MACEXP	  64

#define LISTING_DEFAULT    (LISTING_LISTING | LISTING_HLL | LISTING_SYMBOLS)

#ifndef NO_LISTING
#define LISTING_NEWLINE() { if (listing) listing_newline(NULL); }
#else
#define LISTING_NEWLINE() {;}
#endif
#define LISTING_EOF()     LISTING_NEWLINE()

#define LISTING_SKIP_COND() ((listing & LISTING_NOCOND) != 0)

void listing_eject (int);
void listing_error (const char *message);
void listing_file (const char *name);
void listing_flags (int);
void listing_list (int on);
void listing_newline (char *ps);
void listing_prev_line (void);
void listing_print (char *name);
void listing_psize (int);
void listing_nopage (int);
void listing_source_file (const char *);
void listing_source_line (unsigned int);
void listing_title (int depth);
void listing_warning (const char *message);
void listing_width (unsigned int x);

extern int listing_lhs_width;
extern int listing_lhs_width_second;
extern int listing_lhs_cont_lines;
extern int listing_rhs_width;

#endif /* __listing_h__ */

/* end of listing.h */
