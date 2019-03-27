/* List lines of source files for GDB, the GNU debugger.
   Copyright 1999 Free Software Foundation, Inc.

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

#ifndef SOURCE_H
#define SOURCE_H

struct symtab;

/* Open a source file given a symtab S.  Returns a file descriptor or
   negative number for error.  */
extern int open_source_file (struct symtab *s);

/* Create and initialize the table S->line_charpos that records the
   positions of the lines in the source file, which is assumed to be
   open on descriptor DESC.  All set S->nlines to the number of such
   lines.  */
extern void find_source_lines (struct symtab *s, int desc);

/* Return the first line listed by print_source_lines.
   Used by command interpreters to request listing from
   a previous point. */
extern int get_first_line_listed (void);

/* Return the default number of lines to print with commands like the
   cli "list".  The caller of print_source_lines must use this to
   calculate the end line and use it in the call to print_source_lines
   as it does not automatically use this value. */
extern int get_lines_to_list (void);

/* Return the current source file for listing and next line to list.
   NOTE: The returned sal pc and end fields are not valid. */
extern struct symtab_and_line get_current_source_symtab_and_line (void);

/* If the current source file for listing is not set, try and get a default.
   Usually called before get_current_source_symtab_and_line() is called.
   It may err out if a default cannot be determined.
   We must be cautious about where it is called, as it can recurse as the
   process of determining a new default may call the caller!
   Use get_current_source_symtab_and_line only to get whatever
   we have without erroring out or trying to get a default. */
extern void set_default_source_symtab_and_line (void);

/* Return the current default file for listing and next line to list
   (the returned sal pc and end fields are not valid.)
   and set the current default to whatever is in SAL.
   NOTE: The returned sal pc and end fields are not valid. */
extern struct symtab_and_line set_current_source_symtab_and_line (const struct symtab_and_line *);

/* Reset any information stored about a default file and line to print. */
extern void clear_current_source_symtab_and_line (void);
#endif
