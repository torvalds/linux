/* ldlex.h -
   Copyright 1991, 1992, 1993, 1994, 1995, 1997, 2000, 2003
   Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 1, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef LDLEX_H
#define LDLEX_H

#include <stdio.h>

/* The initial parser states.  */
typedef enum input_enum {
  input_selected,		/* We've set the initial state.  */
  input_script,
  input_mri_script,
  input_version_script,
  input_dynamic_list,
  input_defsym
} input_type;

extern input_type parser_input;

extern unsigned int lineno;
extern const char *lex_string;

/* In ldlex.l.  */
extern int yylex (void);
extern void lex_push_file (FILE *, const char *);
extern void lex_redirect (const char *);
extern void ldlex_script (void);
extern void ldlex_mri_script (void);
extern void ldlex_version_script (void);
extern void ldlex_version_file (void);
extern void ldlex_defsym (void);
extern void ldlex_expression (void);
extern void ldlex_both (void);
extern void ldlex_command (void);
extern void ldlex_popstate (void);

/* In lexsup.c.  */
extern int lex_input (void);
extern void lex_unput (int);
#ifndef yywrap
extern int yywrap (void);
#endif
extern void parse_args (unsigned, char **);

#endif
