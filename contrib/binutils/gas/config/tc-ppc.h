/* tc-ppc.h -- Header file for tc-ppc.c.
   Copyright 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Support.

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

#define TC_PPC

struct fix;

/* Set the endianness we are using.  Default to big endian.  */
#ifndef TARGET_BYTES_BIG_ENDIAN
#define TARGET_BYTES_BIG_ENDIAN 1
#endif

/* If OBJ_COFF is defined, and TE_PE is not defined, we are assembling
   XCOFF for AIX or PowerMac.  If TE_PE is defined, we are assembling
   COFF for Windows NT.  */

#ifdef OBJ_COFF
#ifndef TE_PE
#define OBJ_XCOFF
#endif
#endif

/* The target BFD architecture.  */
#define TARGET_ARCH (ppc_arch ())
#define TARGET_MACH (ppc_mach ())
extern enum bfd_architecture ppc_arch (void);
extern unsigned long ppc_mach (void);

/* Whether or not the target is big endian */
extern int target_big_endian;

/* The target BFD format.  */
#define TARGET_FORMAT (ppc_target_format ())
extern char *ppc_target_format (void);

/* Permit temporary numeric labels.  */
#define LOCAL_LABELS_FB 1

/* $ is used to refer to the current location.  */
#define DOLLAR_DOT

/* Strings do not use backslash escapes under COFF.  */
#ifdef OBJ_COFF
#define NO_STRING_ESCAPES
#endif

#ifdef OBJ_ELF
#define DIFF_EXPR_OK		/* foo-. gets turned into PC relative relocs */
#endif

#if TARGET_BYTES_BIG_ENDIAN
#define PPC_BIG_ENDIAN 1
#else
#define PPC_BIG_ENDIAN 0
#endif

/* We don't need to handle .word strangely.  */
#define WORKING_DOT_WORD

#define MAX_MEM_FOR_RS_ALIGN_CODE 4
#define HANDLE_ALIGN(FRAGP)						\
  if ((FRAGP)->fr_type == rs_align_code)				\
    ppc_handle_align (FRAGP);

extern void ppc_handle_align (struct frag *);

#define SUB_SEGMENT_ALIGN(SEG, FRCHAIN) 0

#define md_frag_check(FRAGP) \
  if ((FRAGP)->has_code							\
      && (((FRAGP)->fr_address + (FRAGP)->insn_addr) & 3) != 0)		\
    as_bad_where ((FRAGP)->fr_file, (FRAGP)->fr_line,			\
		  _("instruction address is not a multiple of 4"));

#ifdef TE_PE

/* Question marks are permitted in symbol names.  */
#define LEX_QM 1

/* Don't adjust TOC relocs.  */
#define tc_fix_adjustable(FIX) ppc_pe_fix_adjustable (FIX)
extern int ppc_pe_fix_adjustable (struct fix *);

#endif

#ifdef OBJ_XCOFF

/* Declarations needed when generating XCOFF code.  XCOFF is an
   extension of COFF, used only on the RS/6000.  Rather than create an
   obj-xcoff, we just use obj-coff, and handle the extensions here in
   tc-ppc.  */

/* We need to keep some information for symbols.  */
struct ppc_tc_sy
{
  /* We keep a few linked lists of symbols.  */
  symbolS *next;
  /* Non-zero if the symbol should be output.  The RS/6000 assembler
     only outputs symbols that are external or are mentioned in a
     .globl or .lglobl statement.  */
  int output;
  /* The symbol class.  */
  int class;
  /* The real name, if the symbol was renamed.  */
  char *real_name;
  /* For a csect symbol, the subsegment we are using.  This is zero
     for symbols that are not csects.  */
  subsegT subseg;
  /* For a csect or common symbol, the alignment to use.  */
  int align;
  /* For a function symbol, a symbol whose value is the size.  The
     field is NULL if there is no size.  */
  symbolS *size;
  /* For a csect symbol, the last symbol which has been defined in
     this csect, or NULL if none have been defined so far.  For a .bs
     symbol, the referenced csect symbol.  */
  symbolS *within;
};

#define TC_SYMFIELD_TYPE struct ppc_tc_sy

/* We need an additional auxent for function symbols.  */
#define OBJ_COFF_MAX_AUXENTRIES 2

/* Square and curly brackets are permitted in symbol names.  */
#define LEX_BR 3

/* Canonicalize the symbol name.  */
#define tc_canonicalize_symbol_name(name) ppc_canonicalize_symbol_name (name)
extern char *ppc_canonicalize_symbol_name (char *);

/* Get the symbol class from the name.  */
#define tc_symbol_new_hook(sym) ppc_symbol_new_hook (sym)
extern void ppc_symbol_new_hook (symbolS *);

/* Set the symbol class of a label based on the csect.  */
#define tc_frob_label(sym) ppc_frob_label (sym)
extern void ppc_frob_label (symbolS *);

/* TOC relocs requires special handling.  */
#define tc_fix_adjustable(FIX) ppc_fix_adjustable (FIX)
extern int ppc_fix_adjustable (struct fix *);

/* We need to set the section VMA.  */
#define tc_frob_section(sec) ppc_frob_section (sec)
extern void ppc_frob_section (asection *);

/* Finish up the symbol.  */
#define tc_frob_symbol(sym, punt) punt = ppc_frob_symbol (sym)
extern int ppc_frob_symbol (symbolS *);

/* Finish up the entire symtab.  */
#define tc_adjust_symtab() ppc_adjust_symtab ()
extern void ppc_adjust_symtab (void);

/* We also need to copy, in particular, the class of the symbol,
   over what obj-coff would otherwise have copied.  */
#define OBJ_COPY_SYMBOL_ATTRIBUTES(dest,src)			\
do {								\
  if (SF_GET_GET_SEGMENT (dest))				\
    S_SET_SEGMENT (dest, S_GET_SEGMENT (src));			\
  symbol_get_tc (dest)->size = symbol_get_tc (src)->size;	\
  symbol_get_tc (dest)->align = symbol_get_tc (src)->align;	\
  symbol_get_tc (dest)->class = symbol_get_tc (src)->class;	\
  symbol_get_tc (dest)->within = symbol_get_tc (src)->within;	\
} while (0)

#endif /* OBJ_XCOFF */

extern const char       ppc_symbol_chars[];
#define tc_symbol_chars ppc_symbol_chars

#ifdef OBJ_ELF

/* Support for SHF_EXCLUDE and SHT_ORDERED */
extern int ppc_section_letter (int, char **);
extern int ppc_section_type (char *, size_t);
extern int ppc_section_word (char *, size_t);
extern int ppc_section_flags (int, int, int);

#define md_elf_section_letter(LETTER, PTR_MSG)	ppc_section_letter (LETTER, PTR_MSG)
#define md_elf_section_type(STR, LEN)		ppc_section_type (STR, LEN)
#define md_elf_section_word(STR, LEN)		ppc_section_word (STR, LEN)
#define md_elf_section_flags(FLAGS, ATTR, TYPE)	ppc_section_flags (FLAGS, ATTR, TYPE)

#define tc_comment_chars ppc_comment_chars
extern const char *ppc_comment_chars;

/* Keep relocations relative to the GOT, or non-PC relative.  */
#define tc_fix_adjustable(FIX) ppc_fix_adjustable (FIX)
extern int ppc_fix_adjustable (struct fix *);

/* Values passed to md_apply_fix don't include symbol values.  */
#define MD_APPLY_SYM_VALUE(FIX) 0

#define tc_frob_file_before_adjust ppc_frob_file_before_adjust
extern void ppc_frob_file_before_adjust (void);

#endif /* OBJ_ELF */

#if defined (OBJ_ELF) || defined (OBJ_XCOFF)
#define TC_FORCE_RELOCATION(FIX) ppc_force_relocation (FIX)
extern int ppc_force_relocation (struct fix *);
#endif

/* call md_pcrel_from_section, not md_pcrel_from */
#define MD_PCREL_FROM_SECTION(FIX, SEC) md_pcrel_from_section(FIX, SEC)
extern long md_pcrel_from_section (struct fix *, segT);

#define md_parse_name(name, exp, mode, c) ppc_parse_name (name, exp)
extern int ppc_parse_name (const char *, struct expressionS *);

#define md_operand(x)

#define md_cleanup() ppc_cleanup ()
extern void ppc_cleanup (void);

#define TARGET_USE_CFIPOP 1

#define tc_cfi_frame_initial_instructions ppc_cfi_frame_initial_instructions
extern void ppc_cfi_frame_initial_instructions (void);

#define tc_regname_to_dw2regnum tc_ppc_regname_to_dw2regnum
extern int tc_ppc_regname_to_dw2regnum (char *);

extern int ppc_cie_data_alignment;

#define DWARF2_LINE_MIN_INSN_LENGTH     4
#define DWARF2_DEFAULT_RETURN_COLUMN    0x41
#define DWARF2_CIE_DATA_ALIGNMENT       ppc_cie_data_alignment
