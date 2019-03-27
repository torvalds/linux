/* Interface to C preprocessor macro expansion for GDB.
   Copyright 2002 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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


#ifndef MACROEXP_H
#define MACROEXP_H

/* A function for looking up preprocessor macro definitions.  Return
   the preprocessor definition of NAME in scope according to BATON, or
   zero if NAME is not defined as a preprocessor macro.

   The caller must not free or modify the definition returned.  It is
   probably unwise for the caller to hold pointers to it for very
   long; it probably lives in some objfile's obstacks.  */
typedef struct macro_definition *(macro_lookup_ftype) (const char *name,
                                                       void *baton);


/* Expand any preprocessor macros in SOURCE, and return the expanded
   text.  Use LOOKUP_FUNC and LOOKUP_FUNC_BATON to find identifiers'
   preprocessor definitions.  SOURCE is a null-terminated string.  The
   result is a null-terminated string, allocated using xmalloc; it is
   the caller's responsibility to free it.  */
char *macro_expand (const char *source,
                    macro_lookup_ftype *lookup_func,
                    void *lookup_func_baton);


/* Expand all preprocessor macro references that appear explicitly in
   SOURCE, but do not expand any new macro references introduced by
   that first level of expansion.  Use LOOKUP_FUNC and
   LOOKUP_FUNC_BATON to find identifiers' preprocessor definitions.
   SOURCE is a null-terminated string.  The result is a
   null-terminated string, allocated using xmalloc; it is the caller's
   responsibility to free it.  */
char *macro_expand_once (const char *source,
                         macro_lookup_ftype *lookup_func,
                         void *lookup_func_baton);


/* If the null-terminated string pointed to by *LEXPTR begins with a
   macro invocation, return the result of expanding that invocation as
   a null-terminated string, and set *LEXPTR to the next character
   after the invocation.  The result is completely expanded; it
   contains no further macro invocations.

   Otherwise, if *LEXPTR does not start with a macro invocation,
   return zero, and leave *LEXPTR unchanged.

   Use LOOKUP_FUNC and LOOKUP_BATON to find macro definitions.

   If this function returns a string, the caller is responsible for
   freeing it, using xfree.

   We need this expand-one-token-at-a-time interface in order to
   accomodate GDB's C expression parser, which may not consume the
   entire string.  When the user enters a command like

      (gdb) break *func+20 if x == 5

   the parser is expected to consume `func+20', and then stop when it
   sees the "if".  But of course, "if" appearing in a character string
   or as part of a larger identifier doesn't count.  So you pretty
   much have to do tokenization to find the end of the string that
   needs to be macro-expanded.  Our C/C++ tokenizer isn't really
   designed to be called by anything but the yacc parser engine.  */
char *macro_expand_next (char **lexptr,
                         macro_lookup_ftype *lookup_func,
                         void *lookup_baton);


#endif /* MACROEXP_H */
