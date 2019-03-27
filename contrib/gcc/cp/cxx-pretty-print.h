/* Interface for the GNU C++ pretty-printer.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@integrable-solutions.net>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_CXX_PRETTY_PRINT_H
#define GCC_CXX_PRETTY_PRINT_H

#include "c-pretty-print.h"

#undef pp_c_base
#define pp_c_base(PP) (&(PP)->c_base)

typedef enum
{
  /* Ask for a qualified-id.  */
  pp_cxx_flag_default_argument = 1 << pp_c_flag_last_bit

} cxx_pretty_printer_flags;

typedef struct
{
  c_pretty_printer c_base;
  /* This is the enclosing scope of the entity being pretty-printed.  */
  tree enclosing_scope;
} cxx_pretty_printer;

#define pp_cxx_cv_qualifier_seq(PP, T)   \
   pp_c_type_qualifier_list (pp_c_base (PP), T)

#define pp_cxx_whitespace(PP)		pp_c_whitespace (pp_c_base (PP))
#define pp_cxx_left_paren(PP)		pp_c_left_paren (pp_c_base (PP))
#define pp_cxx_right_paren(PP)		pp_c_right_paren (pp_c_base (PP))
#define pp_cxx_left_brace(PP)		pp_c_left_brace (pp_c_base (PP))
#define pp_cxx_right_brace(PP)		pp_c_right_brace (pp_c_base (PP))
#define pp_cxx_left_bracket(PP)		pp_c_left_bracket (pp_c_base (PP))
#define pp_cxx_right_bracket(PP)	pp_c_right_bracket (pp_c_base (PP))
#define pp_cxx_dot(PP)			pp_c_dot (pp_c_base (PP))
#define pp_cxx_ampersand(PP)		pp_c_ampersand (pp_c_base (PP))
#define pp_cxx_star(PP)			pp_c_star (pp_c_base (PP))
#define pp_cxx_arrow(PP)		pp_c_arrow (pp_c_base (PP))
#define pp_cxx_semicolon(PP)		pp_c_semicolon (pp_c_base (PP))
#define pp_cxx_complement(PP)		pp_c_complement (pp_c_base (PP))

#define pp_cxx_identifier(PP, I)	pp_c_identifier (pp_c_base (PP), I)
#define pp_cxx_tree_identifier(PP, T) \
  pp_c_tree_identifier (pp_c_base (PP), T)

void pp_cxx_pretty_printer_init (cxx_pretty_printer *);
void pp_cxx_begin_template_argument_list (cxx_pretty_printer *);
void pp_cxx_end_template_argument_list (cxx_pretty_printer *);
void pp_cxx_colon_colon (cxx_pretty_printer *);
void pp_cxx_separate_with (cxx_pretty_printer *, int);

void pp_cxx_declaration (cxx_pretty_printer *, tree);
void pp_cxx_canonical_template_parameter (cxx_pretty_printer *, tree);


#endif /* GCC_CXX_PRETTY_PRINT_H */
