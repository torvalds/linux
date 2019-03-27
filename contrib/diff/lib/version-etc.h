/* Utility to help print --version output in a consistent format.
   Copyright (C) 1999, 2003 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Jim Meyering. */

#ifndef VERSION_ETC_H
# define VERSION_ETC_H 1

# include <stdarg.h>
# include <stdio.h>

extern const char *version_etc_copyright;

extern void version_etc_va (FILE *stream,
			    const char *command_name, const char *package,
			    const char *version, va_list authors);

extern void version_etc (FILE *stream,
			 const char *command_name, const char *package,
			 const char *version,
		         /* const char *author1, ...*/ ...);

#endif /* VERSION_ETC_H */
