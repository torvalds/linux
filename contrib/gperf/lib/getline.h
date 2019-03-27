/*  Copyright (C) 1995, 2000-2002 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
USA.  */

#ifndef GETLINE_H_
# define GETLINE_H_ 1

# include <stddef.h>
# include <stdio.h>

/* Like the glibc functions get_line and get_delim, except that the result
   must be freed using delete[], not free().  */

/* Reads up to (and including) a newline from STREAM into *LINEPTR
   (and null-terminate it). *LINEPTR is a pointer returned from new [] (or
   NULL), pointing to *N characters of space.  It is realloc'd as
   necessary.  Returns the number of characters read (not including the
   null terminator), or -1 on error or immediate EOF.  */
extern int get_line (char **lineptr, size_t *n, FILE *stream);

/* Reads up to (and including) a DELIMITER from STREAM into *LINEPTR
   (and null-terminate it). *LINEPTR is a pointer returned from new [] (or
   NULL), pointing to *N characters of space.  It is realloc'd as
   necessary.  Returns the number of characters read (not including the
   null terminator), or -1 on error or immediate EOF.  */
extern int get_delim (char **lineptr, size_t *n, int delimiter, FILE *stream);

#endif /* not GETLINE_H_ */
