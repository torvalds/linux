/* scan.h - Utility declarations for scan-decls and fix-header programs.
   Copyright (C) 1993, 1998, 1999, 2003, 2004 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include <stdio.h>

typedef struct sstring
{
  char *base;
  char *ptr;
  char *limit;
} sstring;

#define INIT_SSTRING(STR) ((STR)->base = 0, (STR)->ptr = 0, (STR)->limit = 0)
#define FREE_SSTRING(STR) do { if ((STR)->base) free (STR)->base; } while(0)
#define SSTRING_PUT(STR, C) do {\
  if ((STR)->limit <= (STR)->ptr) make_sstring_space (STR, 1); \
  *(STR)->ptr++ = (C); } while (0)
#define SSTRING_LENGTH(STR) ((STR)->ptr - (STR)->base)
#define MAKE_SSTRING_SPACE(STR, COUNT) \
  if ((STR)->limit - (STR)->ptr < (COUNT)) make_sstring_space (STR, COUNT);

struct partial_proto;
struct fn_decl
{
  const char *fname;
  const char *rtype;
  const char *params;
  struct partial_proto *partial;
};

struct cpp_token;

extern void sstring_append (sstring *, sstring *);
extern void make_sstring_space (sstring *, int);
extern int skip_spaces (FILE *, int);
extern int scan_ident (FILE *, sstring *, int);
extern int scan_string (FILE *, sstring *, int);
extern int read_upto (FILE *, sstring *, int);
extern unsigned long hash (const char *);
extern void recognized_function (const struct cpp_token *,
				 unsigned int, int, int);
extern void recognized_extern (const struct cpp_token *);
extern unsigned int hashstr (const char *, unsigned int);

extern int scan_decls (struct cpp_reader *, int, char **);

/* get_token is a simple C lexer.  */
#define IDENTIFIER_TOKEN 300
#define CHAR_TOKEN 301
#define STRING_TOKEN 302
#define INT_TOKEN 303
extern int get_token (FILE *, sstring *);

/* Current file and line numer, taking #-directives into account */
extern int source_lineno;
extern sstring source_filename;
/* Current physical line number */
extern int lineno;

extern struct line_maps line_table;
