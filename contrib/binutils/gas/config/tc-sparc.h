/* tc-sparc.h - Macros and type defines for the sparc.
   Copyright 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2005, 2007
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2,
   or (at your option) any later version.

   GAS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with GAS; see the file COPYING.  If not, write
   to the Free Software Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#ifndef TC_SPARC
#define TC_SPARC 1

struct frag;

/* This is used to set the default value for `target_big_endian'.  */
#define TARGET_BYTES_BIG_ENDIAN 1

#define LOCAL_LABELS_FB 1

#define TARGET_ARCH bfd_arch_sparc

#ifdef TE_FreeBSD
#define ELF_TARGET_FORMAT	"elf32-sparc-freebsd"
#define ELF64_TARGET_FORMAT	"elf64-sparc-freebsd"
#endif

#ifndef ELF_TARGET_FORMAT
#define ELF_TARGET_FORMAT	"elf32-sparc"
#endif

#ifndef ELF64_TARGET_FORMAT
#define ELF64_TARGET_FORMAT	"elf64-sparc"
#endif

extern const char *sparc_target_format PARAMS ((void));
#define TARGET_FORMAT sparc_target_format ()

#define RELOC_EXPANSION_POSSIBLE
#define MAX_RELOC_EXPANSION 2

/* Make it unconditional and check if -EL is valid after option parsing */
#define SPARC_BIENDIAN

#define WORKING_DOT_WORD

#define md_convert_frag(b,s,f) \
  as_fatal (_("sparc convert_frag\n"))
#define md_estimate_size_before_relax(f,s) \
  (as_fatal (_("estimate_size_before_relax called")), 1)

#define LISTING_HEADER "SPARC GAS "

extern int sparc_pic_code;

/* We require .word, et. al., to be aligned correctly.  */
#define md_cons_align(nbytes) sparc_cons_align (nbytes)
extern void sparc_cons_align PARAMS ((int));

#define HANDLE_ALIGN(fragp) sparc_handle_align (fragp)
extern void sparc_handle_align PARAMS ((struct frag *));

#define MAX_MEM_FOR_RS_ALIGN_CODE  (3 + 4 + 4)

/* I know that "call 0" fails in sparc-coff if this doesn't return 1.  I
   don't know about other relocation types, or other formats, yet.  */
#ifdef OBJ_COFF
#define TC_FORCE_RELOCATION_ABS(FIX)		\
  ((FIX)->fx_r_type == BFD_RELOC_32_PCREL_S2	\
   || TC_FORCE_RELOCATION (FIX))

#define RELOC_REQUIRES_SYMBOL
#endif

#ifdef OBJ_AOUT
/* This expression evaluates to true if the relocation is for a local
   object for which we still want to do the relocation at runtime.
   False if we are willing to perform this relocation while building
   the .o file.  */

#define TC_FORCE_RELOCATION_LOCAL(FIX)		\
  (!(FIX)->fx_pcrel				\
   || (sparc_pic_code				\
       && S_IS_EXTERNAL ((FIX)->fx_addsy))	\
   || TC_FORCE_RELOCATION (FIX))
#endif

#ifdef OBJ_ELF
/* Don't turn certain relocs into relocations against sections.  This
   is required for the dynamic linker to operate properly.  When
   generating PIC, we need to keep any non PC relative reloc.  The PIC
   part of this test must be parallel to the code in tc_gen_reloc which
   converts relocations to GOT relocations.  */
#define tc_fix_adjustable(FIX)						\
  ((FIX)->fx_r_type != BFD_RELOC_VTABLE_INHERIT				\
   && (FIX)->fx_r_type != BFD_RELOC_VTABLE_ENTRY			\
   && ((FIX)->fx_r_type < BFD_RELOC_SPARC_TLS_GD_HI22			\
       || (FIX)->fx_r_type > BFD_RELOC_SPARC_TLS_TPOFF64)		\
   && (! sparc_pic_code							\
       || ((FIX)->fx_r_type != BFD_RELOC_HI22				\
	   && (FIX)->fx_r_type != BFD_RELOC_LO10			\
	   && (FIX)->fx_r_type != BFD_RELOC_SPARC13			\
	   && ((FIX)->fx_r_type != BFD_RELOC_32_PCREL_S2		\
	       || !generic_force_reloc (FIX))				\
	   && ((FIX)->fx_pcrel						\
	       || ((FIX)->fx_subsy != NULL				\
		   && (S_GET_SEGMENT ((FIX)->fx_subsy)			\
		       == S_GET_SEGMENT ((FIX)->fx_addsy)))		\
	       || S_IS_LOCAL ((FIX)->fx_addsy)))))

/* Values passed to md_apply_fix don't include the symbol value.  */
#define MD_APPLY_SYM_VALUE(FIX) 0

/* Finish up the entire symtab.  */
#define tc_adjust_symtab() sparc_adjust_symtab ()
extern void sparc_adjust_symtab PARAMS ((void));
#endif

#ifdef OBJ_AOUT
/* When generating PIC code, we must not adjust any reloc which will
   turn into a reloc against the global offset table, nor any reloc
   which we will need if a symbol is overridden.  */
#define tc_fix_adjustable(FIX)						\
  (! sparc_pic_code							\
   || ((FIX)->fx_pcrel							\
       && ((FIX)->fx_addsy == NULL					\
	   || (! S_IS_EXTERNAL ((FIX)->fx_addsy)			\
	       && ! S_IS_WEAK ((FIX)->fx_addsy))))			\
   || (FIX)->fx_r_type == BFD_RELOC_16					\
   || (FIX)->fx_r_type == BFD_RELOC_32)
#endif

#define elf_tc_final_processing sparc_elf_final_processing
extern void sparc_elf_final_processing PARAMS ((void));

#define md_operand(x)

extern void sparc_md_end PARAMS ((void));
#define md_end() sparc_md_end ()

#endif

#ifdef OBJ_ELF
#define TC_PARSE_CONS_EXPRESSION(EXP, NBYTES) sparc_cons (EXP, NBYTES)
extern void sparc_cons PARAMS ((expressionS *, int));
#endif

#define TC_CONS_FIX_NEW cons_fix_new_sparc
extern void cons_fix_new_sparc
  PARAMS ((struct frag *, int, unsigned int, struct expressionS *));

#define TC_FIX_TYPE	valueT

#define TC_INIT_FIX_DATA(X)			\
  do						\
     {						\
       (X)->tc_fix_data = 0;			\
     }						\
  while (0)

#define TC_FIX_DATA_PRINT(FILE, FIX)					\
  do									\
    {									\
      fprintf ((FILE), "addend2=%ld\n",   				\
	      (unsigned long) (FIX)->tc_fix_data);			\
    }									\
  while (0)

#define TARGET_USE_CFIPOP 1

#define tc_cfi_frame_initial_instructions sparc_cfi_frame_initial_instructions
extern void sparc_cfi_frame_initial_instructions PARAMS ((void));

#define tc_regname_to_dw2regnum sparc_regname_to_dw2regnum
extern int sparc_regname_to_dw2regnum PARAMS ((char *regname));

#define tc_cfi_emit_pcrel_expr sparc_cfi_emit_pcrel_expr
extern void sparc_cfi_emit_pcrel_expr PARAMS ((expressionS *, unsigned int));

extern int sparc_cie_data_alignment;

#define DWARF2_LINE_MIN_INSN_LENGTH     4
#define DWARF2_DEFAULT_RETURN_COLUMN    15
#define DWARF2_CIE_DATA_ALIGNMENT       sparc_cie_data_alignment

/* end of tc-sparc.h */
