/* tc-ia64.h -- Header file for tc-ia64.c.
   Copyright 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2007
   Free Software Foundation, Inc.
   Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

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
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include "opcode/ia64.h"
#include "elf/ia64.h"

#define TC_IA64

/* Linux is little endian by default.  HPUX is big endian by default.  */
#ifdef TE_HPUX
#define TARGET_BYTES_BIG_ENDIAN		1
#define MD_FLAGS_DEFAULT		EF_IA_64_BE
#else
#define TARGET_BYTES_BIG_ENDIAN		0
#define MD_FLAGS_DEFAULT		EF_IA_64_ABI64
#endif /* TE_HPUX */

extern void (*ia64_number_to_chars) PARAMS ((char *, valueT, int));
#define md_number_to_chars		(*ia64_number_to_chars)

extern void ia64_elf_section_change_hook PARAMS ((void));
#define md_elf_section_change_hook	ia64_elf_section_change_hook

/* We record the endian for this section. 0 means default, 1 means
   big endian and 2 means little endian.  */
struct ia64_segment_info_type
{
  unsigned int endian : 2;
};

#define TC_SEGMENT_INFO_TYPE		struct ia64_segment_info_type

extern void ia64_adjust_symtab PARAMS ((void));
#define tc_adjust_symtab()	ia64_adjust_symtab ()

extern void ia64_frob_file PARAMS ((void));
#define tc_frob_file()		ia64_frob_file ()

/* We need to set the default object file format in ia64_init and not in
   md_begin.  This is because parse_args is called before md_begin, and we
   do not want md_begin to wipe out the flag settings set by options parsed in
   md_parse_args.  */

#define HOST_SPECIAL_INIT ia64_init
extern void ia64_init PARAMS ((int, char **));

#define TARGET_FORMAT ia64_target_format()
extern const char *ia64_target_format PARAMS ((void));

#define TARGET_ARCH			bfd_arch_ia64
#define DOUBLESLASH_LINE_COMMENTS	/* allow //-style comments */

#define NEED_LITERAL_POOL		/* need gp literal pool */
#define RELOC_REQUIRES_SYMBOL
#define DIFF_EXPR_OK   /* foo-. gets turned into PC relative relocs */
#define NEED_INDEX_OPERATOR		/* [ ] is index operator */

#define QUOTES_IN_INSN			/* allow `string "foo;bar"' */
#define LEX_AT		(LEX_NAME|LEX_BEGIN_NAME) /* allow `@' inside name */
#define LEX_QM		(LEX_NAME|LEX_BEGIN_NAME) /* allow `?' inside name */
#define LEX_HASH	LEX_END_NAME	/* allow `#' ending a name */

extern const char ia64_symbol_chars[];
#define tc_symbol_chars ia64_symbol_chars

#define SUB_SEGMENT_ALIGN(SEG, FRCHAIN) 0

struct ia64_fix
  {
    int bigendian;			/* byte order at fix location */
    enum ia64_opnd opnd;
  };

extern void ia64_end_of_source PARAMS((void));
extern void ia64_start_line PARAMS((void));
extern int ia64_unrecognized_line PARAMS((int ch));
extern void ia64_frob_label PARAMS((struct symbol *sym));
#ifdef TE_HPUX
extern int ia64_frob_symbol PARAMS((struct symbol *sym));
#endif
extern void ia64_flush_pending_output PARAMS((void));
extern int ia64_parse_name PARAMS((char *name, expressionS *e, char *nextP));
extern int ia64_optimize_expr PARAMS((expressionS *l, operatorT op,
				      expressionS *r));
extern void ia64_cons_align PARAMS((int));
extern void ia64_flush_insns PARAMS((void));
extern int ia64_fix_adjustable PARAMS((struct fix *fix));
extern int ia64_force_relocation PARAMS((struct fix *));
extern void ia64_cons_fix_new PARAMS ((fragS *f, int where, int nbytes,
				       expressionS *exp));
extern void ia64_validate_fix PARAMS ((struct fix *fix));
extern char * ia64_canonicalize_symbol_name PARAMS ((char *));
extern int ia64_elf_section_letter PARAMS ((int, char **));
extern flagword ia64_elf_section_flags PARAMS ((flagword, int, int));
extern int ia64_elf_section_type PARAMS ((const char *, size_t len));
extern long ia64_pcrel_from_section PARAMS ((struct fix *fix, segT sec));
extern void ia64_md_do_align PARAMS ((int, const char *, int, int));
extern void ia64_handle_align PARAMS ((fragS *f));
extern void ia64_after_parse_args PARAMS ((void));
extern void ia64_dwarf2_emit_offset PARAMS ((symbolS *, unsigned int));
extern void ia64_check_label PARAMS ((symbolS *));
extern int ia64_estimate_size_before_relax (fragS *, asection *);
extern void ia64_convert_frag (fragS *);

#define md_end()       			ia64_end_of_source ()
#define md_start_line_hook()		ia64_start_line ()
#define tc_unrecognized_line(ch)	ia64_unrecognized_line (ch)
#define tc_frob_label(s)		ia64_frob_label (s)
#ifdef TE_HPUX
#define tc_frob_symbol(s,p)		p |= ia64_frob_symbol (s)
#endif /* TE_HPUX */
#define md_flush_pending_output()	ia64_flush_pending_output ()
#define md_parse_name(s,e,m,c)		ia64_parse_name (s, e, c)
#define tc_canonicalize_symbol_name(s)	ia64_canonicalize_symbol_name (s)
#define tc_canonicalize_section_name(s)	ia64_canonicalize_symbol_name (s)
#define md_optimize_expr(l,o,r)		ia64_optimize_expr (l, o, r)
#define md_cons_align(n)		ia64_cons_align (n)
#define TC_FORCE_RELOCATION(f)		ia64_force_relocation (f)
#define tc_fix_adjustable(f)		ia64_fix_adjustable (f)
#define MD_APPLY_SYM_VALUE(FIX)		0
#define md_convert_frag(b,s,f)		ia64_convert_frag (f)
#define md_create_long_jump(p,f,t,fr,s)	as_fatal ("ia64_create_long_jump")
#define md_create_short_jump(p,f,t,fr,s) \
					as_fatal ("ia64_create_short_jump")
#define md_estimate_size_before_relax(f,s) \
					ia64_estimate_size_before_relax(f,s)
#define md_elf_section_letter		ia64_elf_section_letter
#define md_elf_section_flags		ia64_elf_section_flags
#define TC_FIX_TYPE			struct ia64_fix
#define TC_INIT_FIX_DATA(f)		{ f->tc_fix_data.opnd = 0; }
#define TC_CONS_FIX_NEW(f,o,l,e)	ia64_cons_fix_new (f, o, l, e)
#define TC_VALIDATE_FIX(fix,seg,skip)	ia64_validate_fix (fix)
#define MD_PCREL_FROM_SECTION(fix,sec)	ia64_pcrel_from_section (fix, sec)
#define md_section_align(seg,size)	(size)
#define md_do_align(n,f,l,m,j)		ia64_md_do_align (n,f,l,m)
#define HANDLE_ALIGN(f)			ia64_handle_align (f)
#define md_elf_section_type(str,len)	ia64_elf_section_type (str, len)
#define md_after_parse_args()		ia64_after_parse_args ()
#define TC_DWARF2_EMIT_OFFSET		ia64_dwarf2_emit_offset
#define tc_check_label(l)		ia64_check_label (l)

/* Record if an alignment frag should end with a stop bit.  */
#define TC_FRAG_TYPE			int
#define TC_FRAG_INIT(FRAGP)		do {(FRAGP)->tc_frag_data = 0;}while (0)

/* Give an error if a frag containing code is not aligned to a 16 byte
   boundary.  */
#define md_frag_check(FRAGP) \
  if ((FRAGP)->has_code							\
      && (((FRAGP)->fr_address + (FRAGP)->insn_addr) & 15) != 0)	\
     as_bad_where ((FRAGP)->fr_file, (FRAGP)->fr_line,			\
		   _("instruction address is not a multiple of 16"));

#define MAX_MEM_FOR_RS_ALIGN_CODE  (15 + 16)

#define WORKING_DOT_WORD	/* don't do broken word processing for now */

#define DWARF2_LINE_MIN_INSN_LENGTH 1	/* so slot-multipliers can be 1 */

/* This is the information required for unwind records in an ia64
   object file. This is required by GAS and the compiler runtime.  */

/* These are the starting point masks for the various types of
   unwind records. To create a record of type R3 for instance, one
   starts by using the value UNW_R3 and or-ing in any other required values.
   These values are also unique (in context), so they can be used to identify
   the various record types as well. UNW_Bx and some UNW_Px do have the
   same value, but Px can only occur in a prologue context, and Bx in
   a body context.  */

#define UNW_R1	0x00
#define UNW_R2	0x40
#define UNW_R3	0x60
#define UNW_P1	0x80
#define UNW_P2	0xA0
#define UNW_P3	0xB0
#define UNW_P4	0xB8
#define UNW_P5	0xB9
#define UNW_P6	0xC0
#define UNW_P7	0xE0
#define UNW_P8	0xF0
#define UNW_P9	0xF1
#define UNW_P10	0xFF
#define UNW_X1	0xF9
#define UNW_X2	0xFA
#define UNW_X3	0xFB
#define UNW_X4	0xFC
#define UNW_B1	0x80
#define UNW_B2	0xC0
#define UNW_B3	0xE0
#define UNW_B4	0xF0

/* These are all the various types of unwind records.  */

typedef enum
{
  prologue, prologue_gr, body, mem_stack_f, mem_stack_v, psp_gr, psp_sprel,
  rp_when, rp_gr, rp_br, rp_psprel, rp_sprel, pfs_when, pfs_gr, pfs_psprel,
  pfs_sprel, preds_when, preds_gr, preds_psprel, preds_sprel,
  fr_mem, frgr_mem, gr_gr, gr_mem, br_mem, br_gr, spill_base, spill_mask,
  unat_when, unat_gr, unat_psprel, unat_sprel, lc_when, lc_gr, lc_psprel,
  lc_sprel, fpsr_when, fpsr_gr, fpsr_psprel, fpsr_sprel,
  priunat_when_gr, priunat_when_mem, priunat_gr, priunat_psprel,
  priunat_sprel, bsp_when, bsp_gr, bsp_psprel, bsp_sprel, bspstore_when,
  bspstore_gr, bspstore_psprel, bspstore_sprel, rnat_when, rnat_gr,
  rnat_psprel, rnat_sprel, epilogue, label_state, copy_state,
  spill_psprel, spill_sprel, spill_reg, spill_psprel_p, spill_sprel_p,
  spill_reg_p, unwabi, endp
} unw_record_type;

/* These structures declare the fields that can be used in each of the
   4 record formats, R, P, B and X.  */

typedef struct unw_r_record
{
  unsigned long rlen;
  unsigned short grmask;
  unsigned short grsave;
  /* masks to represent the union of save.g, save.f, save.b, and
     save.gf: */
  unsigned long imask_size;
  struct
  {
    unsigned char *i;
    unsigned int fr_mem;
    unsigned char gr_mem;
    unsigned char br_mem;
  } mask;
} unw_r_record;

typedef struct unw_p_record
{
  struct unw_rec_list *next;
  unsigned long t;
  unsigned long size;
  union
  {
    unsigned long sp;
    unsigned long psp;
  } off;
  union
  {
    unsigned short gr;
    unsigned short br;
  } r;
  unsigned char grmask;
  unsigned char brmask;
  unsigned int frmask;
  unsigned char abi;
  unsigned char context;
} unw_p_record;

typedef struct unw_b_record
{
  unsigned long t;
  unsigned long label;
  unsigned short ecount;
} unw_b_record;

typedef struct unw_x_record
{
  unsigned long t;
  union
  {
    unsigned long spoff;
    unsigned long pspoff;
    unsigned int reg;
  } where;
  unsigned short reg;
  unsigned short qp;
  unsigned short ab;	/* Value of the AB field..  */
  unsigned short xy;	/* Value of the XY field..  */
} unw_x_record;

/* This structure is used to determine the specific record type and
   its fields.  */
typedef struct unwind_record
{
  unw_record_type type;
  union {
    unw_r_record r;
    unw_p_record p;
    unw_b_record b;
    unw_x_record x;
  } record;
} unwind_record;

/* This expression evaluates to true if the relocation is for a local
   object for which we still want to do the relocation at runtime.
   False if we are willing to perform this relocation while building
   the .o file.  */

/* If the reloc type is BFD_RELOC_UNUSED, then this is for a TAG13/TAG13b field
   which has no external reloc, so we must resolve the value now.  */

#define TC_FORCE_RELOCATION_LOCAL(FIX)			\
  ((FIX)->fx_r_type != BFD_RELOC_UNUSED			\
   && (!(FIX)->fx_pcrel					\
       || (FIX)->fx_r_type == BFD_RELOC_IA64_PLTOFF22	\
       || TC_FORCE_RELOCATION (FIX)))
