/* This file is tc-arm.h
   Copyright 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   Contributed by Richard Earnshaw (rwe@pegasus.esprit.ec.org)
	Modified by David Taylor (dtaylor@armltd.co.uk)

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

#define TC_ARM 1

#ifndef TARGET_BYTES_BIG_ENDIAN
#define TARGET_BYTES_BIG_ENDIAN 0
#endif

#define WORKING_DOT_WORD

#define COFF_MAGIC 	ARMMAGIC
#define TARGET_ARCH 	bfd_arch_arm

#define DIFF_EXPR_OK

#ifdef  LITTLE_ENDIAN
#undef  LITTLE_ENDIAN
#endif

#ifdef  BIG_ENDIAN
#undef  BIG_ENDIAN
#endif

#define LITTLE_ENDIAN 	1234
#define BIG_ENDIAN 	4321

struct fix;

#if defined OBJ_AOUT
# if defined TE_RISCIX
#  define TARGET_FORMAT "a.out-riscix"
# elif defined TE_LINUX
#  define ARM_BI_ENDIAN
#  define TARGET_FORMAT "a.out-arm-linux"
# elif defined TE_NetBSD
#  define TARGET_FORMAT "a.out-arm-netbsd"
# else
#  define ARM_BI_ENDIAN
#  define TARGET_FORMAT (target_big_endian ? "a.out-arm-big" : "a.out-arm-little")
# endif
#elif defined OBJ_AIF
# define TARGET_FORMAT "aif"
#elif defined OBJ_COFF
# define ARM_BI_ENDIAN
# if defined TE_PE
#  if defined TE_EPOC
#   define TARGET_FORMAT (target_big_endian ? "epoc-pe-arm-big" : "epoc-pe-arm-little")
#  elif defined TE_WINCE
#   define TARGET_FORMAT (target_big_endian ? "pe-arm-wince-big" : "pe-arm-wince-little")
#  else
#   define TARGET_FORMAT (target_big_endian ? "pe-arm-big" : "pe-arm-little")
#  endif
# else
#  define TARGET_FORMAT (target_big_endian ? "coff-arm-big" : "coff-arm-little")
# endif
#elif defined OBJ_ELF
# define ARM_BI_ENDIAN
# define TARGET_FORMAT	elf32_arm_target_format ()
#endif

#define TC_FORCE_RELOCATION(FIX) arm_force_relocation (FIX)

#define md_relax_frag(segment, fragp, stretch) \
  arm_relax_frag(segment, fragp, stretch)
extern int arm_relax_frag (asection *, struct frag *, long);

#define md_optimize_expr(l,o,r)		arm_optimize_expr (l, o, r)
extern int arm_optimize_expr (expressionS *, operatorT, expressionS *);

#define md_cleanup() arm_cleanup ()

#define md_start_line_hook() arm_start_line_hook ()

#define tc_frob_label(S) arm_frob_label (S)

/* We also need to mark assembler created symbols:  */
#define tc_frob_fake_label(S) arm_frob_label (S)

#ifdef OBJ_ELF
#define md_end arm_md_end
extern void arm_md_end (void);
bfd_boolean arm_is_eabi (void);
#endif

/* NOTE: The fake label creation in stabs.c:s_stab_generic() has
   deliberately not been updated to mark assembler created stabs
   symbols as Thumb.  */

#define TC_FIX_TYPE int
#define TC_INIT_FIX_DATA(FIX) ((FIX)->tc_fix_data = 0)

/* We need to keep some local information on symbols.  */

#define TC_SYMFIELD_TYPE 	unsigned int
#define ARM_GET_FLAG(s)   	(*symbol_get_tc (s))
#define ARM_SET_FLAG(s,v) 	(*symbol_get_tc (s) |= (v))
#define ARM_RESET_FLAG(s,v) 	(*symbol_get_tc (s) &= ~(v))

#define ARM_FLAG_THUMB 		(1 << 0)	/* The symbol is a Thumb symbol rather than an Arm symbol.  */
#define ARM_FLAG_INTERWORK 	(1 << 1)	/* The symbol is attached to code that supports interworking.  */
#define THUMB_FLAG_FUNC		(1 << 2)	/* The symbol is attached to the start of a Thumb function.  */

#define ARM_IS_THUMB(s)		(ARM_GET_FLAG (s) & ARM_FLAG_THUMB)
#define ARM_IS_INTERWORK(s)	(ARM_GET_FLAG (s) & ARM_FLAG_INTERWORK)
#ifdef OBJ_ELF

/* For ELF objects THUMB_IS_FUNC is inferred from
   ARM_IS_TUMB and the function type.  */
#define THUMB_IS_FUNC(s) \
  ((arm_is_eabi () \
    && (ARM_IS_THUMB (s)) \
    && (symbol_get_bfdsym (s)->flags & BSF_FUNCTION)) \
   || (ARM_GET_FLAG (s) & THUMB_FLAG_FUNC))

#else
#define THUMB_IS_FUNC(s)	(ARM_GET_FLAG (s) & THUMB_FLAG_FUNC)
#endif

#define ARM_SET_THUMB(s,t)      ((t) ? ARM_SET_FLAG (s, ARM_FLAG_THUMB)     : ARM_RESET_FLAG (s, ARM_FLAG_THUMB))
#define ARM_SET_INTERWORK(s,t)  ((t) ? ARM_SET_FLAG (s, ARM_FLAG_INTERWORK) : ARM_RESET_FLAG (s, ARM_FLAG_INTERWORK))
#define THUMB_SET_FUNC(s,t)     ((t) ? ARM_SET_FLAG (s, THUMB_FLAG_FUNC)    : ARM_RESET_FLAG (s, THUMB_FLAG_FUNC))

void arm_copy_symbol_attributes (symbolS *, symbolS *);
#ifndef TC_COPY_SYMBOL_ATTRIBUTES
#define TC_COPY_SYMBOL_ATTRIBUTES(DEST, SRC) \
  (arm_copy_symbol_attributes (DEST, SRC))
#endif

#define TC_START_LABEL(C,STR)            (c == ':' || (c == '/' && arm_data_in_code ()))
#define tc_canonicalize_symbol_name(str) arm_canonicalize_symbol_name (str);
#define obj_adjust_symtab() 		 arm_adjust_symtab ()

#define LISTING_HEADER "ARM GAS "

#define OPTIONAL_REGISTER_PREFIX '%'

#define LOCAL_LABEL(name)  (name[0] == '.' && name[1] == 'L')
#define LOCAL_LABELS_FB    1

/* This expression evaluates to true if the relocation is for a local
   object for which we still want to do the relocation at runtime.
   False if we are willing to perform this relocation while building
   the .o file.  GOTOFF does not need to be checked here because it is
   not pcrel.  I am not sure if some of the others are ever used with
   pcrel, but it is easier to be safe than sorry.  */

#define TC_FORCE_RELOCATION_LOCAL(FIX)			\
  (!(FIX)->fx_pcrel					\
   || (FIX)->fx_r_type == BFD_RELOC_ARM_GOT32		\
   || (FIX)->fx_r_type == BFD_RELOC_32			\
   || TC_FORCE_RELOCATION (FIX))

/* Force output of R_ARM_REL32 relocations against thumb function symbols.
   This is needed to ensure the low bit is handled correctly.  */
#define TC_FORCE_RELOCATION_SUB_SAME(FIX, SEG)	\
  (THUMB_IS_FUNC ((FIX)->fx_addsy)		\
   || !SEG_NORMAL (SEG))

#define TC_CONS_FIX_NEW cons_fix_new_arm

#define MAX_MEM_FOR_RS_ALIGN_CODE 31

/* For frags in code sections we need to record whether they contain
   ARM code or THUMB code.  This is that if they have to be aligned,
   they can contain the correct type of no-op instruction.  */
#define TC_FRAG_TYPE		int
#define TC_FRAG_INIT(fragp)	arm_init_frag (fragp)
#define HANDLE_ALIGN(fragp)	arm_handle_align (fragp)

#define md_do_align(N, FILL, LEN, MAX, LABEL)					\
  if (FILL == NULL && (N) != 0 && ! need_pass_2 && subseg_text_p (now_seg))	\
    {										\
      arm_frag_align_code (N, MAX);						\
      goto LABEL;								\
    }

#define DWARF2_LINE_MIN_INSN_LENGTH 	2

/* The lr register is r14.  */
#define DWARF2_DEFAULT_RETURN_COLUMN  14

/* Registers are generally saved at negative offsets to the CFA.  */
#define DWARF2_CIE_DATA_ALIGNMENT     (-4)

#ifdef OBJ_ELF
# define obj_frob_symbol(sym, punt)	armelf_frob_symbol ((sym), & (punt))
# define md_elf_section_change_hook()	arm_elf_change_section ()
# define md_elf_section_type(str, len)	arm_elf_section_type (str, len)
# define GLOBAL_OFFSET_TABLE_NAME	"_GLOBAL_OFFSET_TABLE_"
# define TC_SEGMENT_INFO_TYPE 		struct arm_segment_info_type

/* This is not really an alignment operation, but it's something we
   need to do at the same time: whenever we are figuring out the
   alignment for data, we should check whether a $d symbol is
   necessary.  */
# define md_cons_align(nbytes)		mapping_state (MAP_DATA)

enum mstate
{
  MAP_UNDEFINED = 0, /* Must be zero, for seginfo in new sections.  */
  MAP_DATA,
  MAP_ARM,
  MAP_THUMB
};

void mapping_state (enum mstate);

struct arm_segment_info_type
{
  enum mstate mapstate;
  unsigned int marked_pr_dependency;
};

/* We want .cfi_* pseudo-ops for generating unwind info.  */
#define TARGET_USE_CFIPOP              1

/* CFI hooks.  */
#define tc_regname_to_dw2regnum            tc_arm_regname_to_dw2regnum
#define tc_cfi_frame_initial_instructions  tc_arm_frame_initial_instructions

#else /* Not OBJ_ELF.  */
#define GLOBAL_OFFSET_TABLE_NAME "__GLOBAL_OFFSET_TABLE_"
#endif

#if defined OBJ_ELF || defined OBJ_COFF

# define EXTERN_FORCE_RELOC 			1
# define tc_fix_adjustable(FIX) 		arm_fix_adjustable (FIX)
/* Values passed to md_apply_fix don't include the symbol value.  */
# define MD_APPLY_SYM_VALUE(FIX) 		0
# define TC_VALIDATE_FIX(FIX, SEGTYPE, LABEL)	arm_validate_fix (FIX)

#endif

#define MD_PCREL_FROM_SECTION(F,S) md_pcrel_from_section(F,S)

extern long md_pcrel_from_section (struct fix *, segT);
extern void arm_frag_align_code (int, int);
extern void arm_validate_fix (struct fix *);
extern const char * elf32_arm_target_format (void);
extern void arm_elf_change_section (void);
extern int arm_force_relocation (struct fix *);
extern void arm_cleanup (void);
extern void arm_start_line_hook (void);
extern void arm_frob_label (symbolS *);
extern int arm_data_in_code (void);
extern char * arm_canonicalize_symbol_name (char *);
extern void arm_adjust_symtab (void);
extern void armelf_frob_symbol (symbolS *, int *);
extern void cons_fix_new_arm (fragS *, int, int, expressionS *);
extern void arm_init_frag (struct frag *);
extern void arm_handle_align (struct frag *);
extern bfd_boolean arm_fix_adjustable (struct fix *);
extern int arm_elf_section_type (const char *, size_t);
extern int tc_arm_regname_to_dw2regnum (char *regname);
extern void tc_arm_frame_initial_instructions (void);

#ifdef TE_PE

#define O_secrel O_md1

#define TC_DWARF2_EMIT_OFFSET  tc_pe_dwarf2_emit_offset
void tc_pe_dwarf2_emit_offset (symbolS *, unsigned int);

#endif /* TE_PE */
