/* Header for GDB line completion.
   Copyright 2000 Free Software Foundation, Inc.

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

#if !defined (COMPLETER_H)
#define COMPLETER_H 1

extern char **complete_line (const char *text, char *line_buffer, int point);

extern char *readline_line_completion_function (const char *text, int matches);

extern char **noop_completer (char *, char *);

extern char **filename_completer (char *, char *);

extern char **location_completer (char *, char *);

extern char **command_completer (char *, char *);

extern char *get_gdb_completer_quote_characters (void);

/* Exported to linespec.c */

extern char *skip_quoted_chars (char *, char *, char *);

extern char *skip_quoted (char *);

#endif /* defined (COMPLETER_H) */
