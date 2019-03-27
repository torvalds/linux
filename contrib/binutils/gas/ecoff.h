/* ecoff.h -- header file for ECOFF debugging support
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2003, 2005
   Free Software Foundation, Inc.
   Contributed by Cygnus Support.
   Put together by Ian Lance Taylor <ian@cygnus.com>.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef GAS_ECOFF_H
#define GAS_ECOFF_H

#ifdef ECOFF_DEBUGGING

#include "coff/sym.h"
#include "coff/ecoff.h"

/* Whether we have seen any ECOFF debugging information.  */
extern int ecoff_debugging_seen;

/* This function should be called at the start of assembly, by
   obj_read_begin_hook.  */
extern void ecoff_read_begin_hook (void);

/* This function should be called when the assembler switches to a new
   file.  */
extern void ecoff_new_file (const char *, int);

/* This function should be called when a new symbol is created, by
   obj_symbol_new_hook.  */
extern void ecoff_symbol_new_hook (symbolS *);

/* This function should be called by the obj_frob_symbol hook.  */
extern void ecoff_frob_symbol (symbolS *);

/* Build the ECOFF debugging information.  This should be called by
   obj_frob_file.  This fills in the counts in *HDR; the offsets are
   filled in relative to the start of the *BUFP.  It sets *BUFP to a
   block of memory holding the debugging information.  It returns the
   length of *BUFP.  */
extern unsigned long ecoff_build_debug
  (HDRR *hdr, char **bufp, const struct ecoff_debug_swap *);

/* Functions to handle the ECOFF debugging directives.  */
extern void ecoff_directive_begin (int);
extern void ecoff_directive_bend (int);
extern void ecoff_directive_end (int);
extern void ecoff_directive_ent (int);
extern void ecoff_directive_fmask (int);
extern void ecoff_directive_frame (int);
extern void ecoff_directive_loc (int);
extern void ecoff_directive_mask (int);

/* Other ECOFF directives.  */
extern void ecoff_directive_extern (int);
extern void ecoff_directive_weakext (int);

/* Functions to handle the COFF debugging directives.  */
extern void ecoff_directive_def (int);
extern void ecoff_directive_dim (int);
extern void ecoff_directive_endef (int);
extern void ecoff_directive_file (int);
extern void ecoff_directive_scl (int);
extern void ecoff_directive_size (int);
extern void ecoff_directive_tag (int);
extern void ecoff_directive_type (int);
extern void ecoff_directive_val (int);

/* Handle stabs.  */
extern void ecoff_stab (segT sec, int what, const char *string,
			int type, int other, int desc);

/* Set the GP prologue size.  */
extern void ecoff_set_gp_prolog_size (int sz);

/* This routine is called from the ECOFF code to set the external
   information for a symbol.  */
#ifndef obj_ecoff_set_ext
extern void obj_ecoff_set_ext (symbolS *, EXTR *);
#endif

/* This routine is used to patch up a line number directive when
   instructions are moved around.  */
extern void ecoff_fix_loc (fragS *, unsigned long);

/* This function is called from read.c to peek at cur_file_ptr.  */
extern int ecoff_no_current_file (void);

/* This function returns the symbol associated with the current proc.  */
extern symbolS *ecoff_get_cur_proc_sym (void);

#endif /* ECOFF_DEBUGGING */

/* This routine is called from read.c to generate line number for .s file.  */
extern void ecoff_generate_asm_lineno (void);

#endif /* ! GAS_ECOFF_H */
