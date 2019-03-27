/* Dump-to-file commands, for GDB, the GNU debugger.

   Copyright 2001 Free Software Foundation, Inc.

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

#ifndef CLI_DUMP_H
#define CLI_DUMP_H

extern void add_dump_command (char *name,
			      void (*func) (char *args, char *mode),
			      char *descr);

/* Utilities for doing the dump.  */
extern char *scan_filename_with_cleanup (char **cmd, const char *defname);

extern char *scan_expression_with_cleanup (char **cmd, const char *defname);

extern FILE *fopen_with_cleanup (char *filename, const char *mode);

extern char *skip_spaces (char *inp);

extern struct value *parse_and_eval_with_error (char *exp, const char *fmt, ...) ATTR_FORMAT (printf, 2, 3);

#endif
