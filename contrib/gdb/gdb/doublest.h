/* Floating point definitions for GDB.

   Copyright 1986, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2003 Free Software Foundation,
   Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef DOUBLEST_H
#define DOUBLEST_H

struct type;

/* Setup definitions for host and target floating point formats.  We need to
   consider the format for `float', `double', and `long double' for both target
   and host.  We need to do this so that we know what kind of conversions need
   to be done when converting target numbers to and from the hosts DOUBLEST
   data type.  */

/* This is used to indicate that we don't know the format of the floating point
   number.  Typically, this is useful for native ports, where the actual format
   is irrelevant, since no conversions will be taking place.  */

#include "floatformat.h"	/* For struct floatformat */

/* Use `long double' if the host compiler supports it.  (Note that this is not
   necessarily any longer than `double'.  On SunOS/gcc, it's the same as
   double.)  This is necessary because GDB internally converts all floating
   point values to the widest type supported by the host.

   There are problems however, when the target `long double' is longer than the
   host's `long double'.  In general, we'll probably reduce the precision of
   any such values and print a warning.  */

#ifdef HAVE_LONG_DOUBLE
typedef long double DOUBLEST;
#else
typedef double DOUBLEST;
#endif

extern void floatformat_to_doublest (const struct floatformat *,
				     const void *in, DOUBLEST *out);
extern void floatformat_from_doublest (const struct floatformat *,
				       const DOUBLEST *in, void *out);

extern int floatformat_is_negative (const struct floatformat *, char *);
extern int floatformat_is_nan (const struct floatformat *, char *);
extern char *floatformat_mantissa (const struct floatformat *, char *);

/* These functions have been replaced by extract_typed_floating and
   store_typed_floating.

   Most calls are passing in TYPE_LENGTH (TYPE) so can be changed to
   just pass the TYPE.  The remainder pass in the length of a
   register, those calls should instead pass in the floating point
   type that corresponds to that length.  */

extern DOUBLEST deprecated_extract_floating (const void *addr, int len);
extern void deprecated_store_floating (void *addr, int len, DOUBLEST val);

/* Given TYPE, return its floatformat.  TYPE_FLOATFORMAT() may return
   NULL.  type_floatformat() detects that and returns a floatformat
   based on the type size when FLOATFORMAT is NULL.  */

const struct floatformat *floatformat_from_type (const struct type *type);

extern DOUBLEST extract_typed_floating (const void *addr,
					const struct type *type);
extern void store_typed_floating (void *addr, const struct type *type,
				  DOUBLEST val);
extern void convert_typed_floating (const void *from,
				    const struct type *from_type,
                                    void *to, const struct type *to_type);

#endif
