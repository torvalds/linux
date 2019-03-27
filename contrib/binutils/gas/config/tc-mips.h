/* tc-mips.h -- header file for tc-mips.c.
   Copyright 1993, 1994, 1995, 1996, 1997, 2000, 2001, 2002, 2003, 2004,
   2005, 2006 Free Software Foundation, Inc.
   Contributed by the OSF and Ralph Campbell.
   Written by Keith Knowles and Ralph Campbell, working independently.
   Modified for ECOFF support by Ian Lance Taylor of Cygnus Support.

   This file is part of GAS.

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

#ifndef TC_MIPS
#define TC_MIPS

struct frag;
struct expressionS;

/* Default to big endian.  */
#ifndef TARGET_BYTES_BIG_ENDIAN
#define TARGET_BYTES_BIG_ENDIAN		1
#endif

#define TARGET_ARCH bfd_arch_mips

#define WORKING_DOT_WORD	1
#define OLD_FLOAT_READS
#define REPEAT_CONS_EXPRESSIONS
#define RELOC_EXPANSION_POSSIBLE
#define MAX_RELOC_EXPANSION 3
#define LOCAL_LABELS_FB 1

/* Maximum symbol offset that can be encoded in a BFD_RELOC_GPREL16
   relocation.  */
#define MAX_GPREL_OFFSET (0x7FF0)

#define md_relax_frag(segment, fragp, stretch) \
  mips_relax_frag(segment, fragp, stretch)
extern int mips_relax_frag (asection *, struct frag *, long);

#define md_undefined_symbol(name)	(0)
#define md_operand(x)

extern void mips_handle_align (struct frag *);
#define HANDLE_ALIGN(fragp)  mips_handle_align (fragp)

#define MAX_MEM_FOR_RS_ALIGN_CODE  (1 + 2)

struct insn_label_list;
#define TC_SEGMENT_INFO_TYPE struct insn_label_list *

/* This field is nonzero if the symbol is the target of a MIPS16 jump.  */
#define TC_SYMFIELD_TYPE int

/* Tell assembler that we have an itbl_mips.h header file to include.  */
#define HAVE_ITBL_CPU

/* The endianness of the target format may change based on command
   line arguments.  */
#define TARGET_FORMAT mips_target_format()
extern const char *mips_target_format (void);

/* MIPS PIC level.  */

enum mips_pic_level
{
  /* Do not generate PIC code.  */
  NO_PIC,

  /* Generate PIC code as in the SVR4 MIPS ABI.  */
  SVR4_PIC,

  /* VxWorks's PIC model.  */
  VXWORKS_PIC
};

extern enum mips_pic_level mips_pic;

extern int tc_get_register (int frame);

#define md_after_parse_args() mips_after_parse_args()
extern void mips_after_parse_args (void);

#define tc_init_after_args() mips_init_after_args()
extern void mips_init_after_args (void);

#define md_parse_long_option(arg) mips_parse_long_option (arg)
extern int mips_parse_long_option (const char *);

#define tc_frob_label(sym) mips_define_label (sym)
extern void mips_define_label (symbolS *);

#define tc_frob_file_before_adjust() mips_frob_file_before_adjust ()
extern void mips_frob_file_before_adjust (void);

#define tc_frob_file_before_fix() mips_frob_file ()
extern void mips_frob_file (void);

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
#define tc_frob_file_after_relocs mips_frob_file_after_relocs
extern void mips_frob_file_after_relocs (void);
#endif

#define tc_fix_adjustable(fixp) mips_fix_adjustable (fixp)
extern int mips_fix_adjustable (struct fix *);

/* Values passed to md_apply_fix don't include symbol values.  */
#define MD_APPLY_SYM_VALUE(FIX) 0

/* Global syms must not be resolved, to support ELF shared libraries.  */
#define EXTERN_FORCE_RELOC			\
  (OUTPUT_FLAVOR == bfd_target_elf_flavour)

/* When generating NEWABI code, we may need to have to keep combined
   relocations which don't have symbols.  */
#define TC_FORCE_RELOCATION(FIX) mips_force_relocation (FIX)
extern int mips_force_relocation (struct fix *);

#define TC_FORCE_RELOCATION_SUB_SAME(FIX, SEG) \
  (! SEG_NORMAL (SEG) || mips_force_relocation (FIX))

/* Register mask variables.  These are set by the MIPS assembly code
   and used by ECOFF and possibly other object file formats.  */
extern unsigned long mips_gprmask;
extern unsigned long mips_cprmask[4];

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)

#define elf_tc_final_processing mips_elf_final_processing
extern void mips_elf_final_processing (void);

#endif

extern void md_mips_end (void);
#define md_end()	md_mips_end()

extern void mips_pop_insert (void);
#define md_pop_insert()		mips_pop_insert()

extern void mips_emit_delays (void);
#define md_flush_pending_output mips_emit_delays

extern void mips_enable_auto_align (void);
#define md_elf_section_change_hook()	mips_enable_auto_align()

extern int mips_dwarf2_addr_size (void);
#define DWARF2_ADDR_SIZE(bfd) mips_dwarf2_addr_size ()

#define TARGET_USE_CFIPOP 1

#define tc_cfi_frame_initial_instructions mips_cfi_frame_initial_instructions
extern void mips_cfi_frame_initial_instructions (void);

#define tc_regname_to_dw2regnum tc_mips_regname_to_dw2regnum
extern int tc_mips_regname_to_dw2regnum (char *regname);

#define DWARF2_DEFAULT_RETURN_COLUMN 31
#define DWARF2_CIE_DATA_ALIGNMENT (-4)

#endif /* TC_MIPS */
