/* frags.h - Header file for the frag concept.
   Copyright 1987, 1992, 1993, 1994, 1995, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

#ifndef FRAGS_H
#define FRAGS_H

struct obstack;

/* A code fragment (frag) is some known number of chars, followed by some
   unknown number of chars. Typically the unknown number of chars is an
   instruction address whose size is yet unknown. We always know the greatest
   possible size the unknown number of chars may become, and reserve that
   much room at the end of the frag.
   Once created, frags do not change address during assembly.
   We chain the frags in (a) forward-linked list(s). The object-file address
   of the 1st char of a frag is generally not known until after relax().
   Many things at assembly time describe an address by {object-file-address
   of a particular frag}+offset.

   BUG: it may be smarter to have a single pointer off to various different
   notes for different frag kinds.  See how code pans.  */

struct frag {
  /* Object file address (as an octet offset).  */
  addressT fr_address;
  /* When relaxing multiple times, remember the address the frag had
     in the last relax pass.  */
  addressT last_fr_address;

  /* (Fixed) number of octets we know we have.  May be 0.  */
  offsetT fr_fix;
  /* May be used for (Variable) number of octets after above.
     The generic frag handling code no longer makes any use of fr_var.  */
  offsetT fr_var;
  /* For variable-length tail.  */
  offsetT fr_offset;
  /* For variable-length tail.  */
  symbolS *fr_symbol;
  /* Points to opcode low addr byte, for relaxation.  */
  char *fr_opcode;

  /* Chain forward; ascending address order.  Rooted in frch_root.  */
  struct frag *fr_next;

  /* Where the frag was created, or where it became a variant frag.  */
  char *fr_file;
  unsigned int fr_line;

#ifndef NO_LISTING
  struct list_info_struct *line;
#endif

  /* Flipped each relax pass so we can easily determine whether
     fr_address has been adjusted.  */
  unsigned int relax_marker:1;

  /* Used to ensure that all insns are emitted on proper address
     boundaries.  */
  unsigned int has_code:1;
  unsigned int insn_addr:6;

  /* What state is my tail in? */
  relax_stateT fr_type;
  relax_substateT fr_subtype;

#ifdef USING_CGEN
  /* Don't include this unless using CGEN to keep frag size down.  */
  struct {
    /* CGEN_INSN entry for this instruction.  */
    const struct cgen_insn *insn;
    /* Index into operand table.  */
    int opindex;
    /* Target specific data, usually reloc number.  */
    int opinfo;
  } fr_cgen;
#endif

#ifdef TC_FRAG_TYPE
  TC_FRAG_TYPE tc_frag_data;
#endif

  /* Data begins here.  */
  char fr_literal[1];
};

#define SIZEOF_STRUCT_FRAG \
((char *) zero_address_frag.fr_literal - (char *) &zero_address_frag)
/* We want to say fr_literal[0] above.  */

/* Current frag we are building.  This frag is incomplete.  It is,
   however, included in frchain_now.  The fr_fix field is bogus;
   instead, use frag_now_fix ().  */
COMMON fragS *frag_now;
extern addressT frag_now_fix (void);
extern addressT frag_now_fix_octets (void);

/* For foreign-segment symbol fixups.  */
COMMON fragS zero_address_frag;
/* For local common (N_BSS segment) fixups.  */
COMMON fragS bss_address_frag;

extern void frag_append_1_char (int);
#define FRAG_APPEND_1_CHAR(X) frag_append_1_char (X)

void frag_init (void);
fragS *frag_alloc (struct obstack *);
void frag_grow (unsigned int nchars);
char *frag_more (int nchars);
void frag_align (int alignment, int fill_character, int max);
void frag_align_pattern (int alignment, const char *fill_pattern,
			 int n_fill, int max);
void frag_align_code (int alignment, int max);
void frag_new (int old_frags_var_max_size);
void frag_wane (fragS * fragP);
int frag_room (void);

char *frag_variant (relax_stateT type,
		    int max_chars,
		    int var,
		    relax_substateT subtype,
		    symbolS * symbol,
		    offsetT offset,
		    char *opcode);

char *frag_var (relax_stateT type,
		int max_chars,
		int var,
		relax_substateT subtype,
		symbolS * symbol,
		offsetT offset,
		char *opcode);

bfd_boolean frag_offset_fixed_p (const fragS *, const fragS *, bfd_vma *);

#endif /* FRAGS_H */
