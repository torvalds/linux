/* tc-i386.h -- Header file for tc-i386.c
   Copyright 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

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

#ifndef TC_I386
#define TC_I386 1

#include "opcodes/i386-opc.h"

struct fix;

#define TARGET_BYTES_BIG_ENDIAN	0

#define TARGET_ARCH		bfd_arch_i386
#define TARGET_MACH		(i386_mach ())
extern unsigned long i386_mach (void);

#ifdef TE_FreeBSD
#define AOUT_TARGET_FORMAT	"a.out-i386-freebsd"
#endif
#ifdef TE_NetBSD
#define AOUT_TARGET_FORMAT	"a.out-i386-netbsd"
#endif
#ifdef TE_386BSD
#define AOUT_TARGET_FORMAT	"a.out-i386-bsd"
#endif
#ifdef TE_LINUX
#define AOUT_TARGET_FORMAT	"a.out-i386-linux"
#endif
#ifdef TE_Mach
#define AOUT_TARGET_FORMAT	"a.out-mach3"
#endif
#ifdef TE_DYNIX
#define AOUT_TARGET_FORMAT	"a.out-i386-dynix"
#endif
#ifndef AOUT_TARGET_FORMAT
#define AOUT_TARGET_FORMAT	"a.out-i386"
#endif

#ifdef TE_FreeBSD
#define ELF_TARGET_FORMAT	"elf32-i386-freebsd"
#define ELF_TARGET_FORMAT64	"elf64-x86-64-freebsd"
#elif defined (TE_VXWORKS)
#define ELF_TARGET_FORMAT	"elf32-i386-vxworks"
#endif

#ifndef ELF_TARGET_FORMAT
#define ELF_TARGET_FORMAT	"elf32-i386"
#endif

#ifndef ELF_TARGET_FORMAT64
#define ELF_TARGET_FORMAT64	"elf64-x86-64"
#endif

#if ((defined (OBJ_MAYBE_COFF) && defined (OBJ_MAYBE_AOUT)) \
     || defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF))
extern const char *i386_target_format (void);
#define TARGET_FORMAT i386_target_format ()
#else
#ifdef OBJ_ELF
#define TARGET_FORMAT		ELF_TARGET_FORMAT
#endif
#ifdef OBJ_AOUT
#define TARGET_FORMAT		AOUT_TARGET_FORMAT
#endif
#endif

#if (defined (OBJ_MAYBE_ELF) || defined (OBJ_ELF))
#define md_end i386_elf_emit_arch_note
extern void i386_elf_emit_arch_note (void);
#endif

#define SUB_SEGMENT_ALIGN(SEG, FRCHAIN) 0

#define LOCAL_LABELS_FB 1

extern const char extra_symbol_chars[];
#define tc_symbol_chars extra_symbol_chars

extern const char *i386_comment_chars;
#define tc_comment_chars i386_comment_chars

/* Prefixes will be emitted in the order defined below.
   WAIT_PREFIX must be the first prefix since FWAIT is really is an
   instruction, and so must come before any prefixes.
   The preferred prefix order is SEG_PREFIX, ADDR_PREFIX, DATA_PREFIX,
   LOCKREP_PREFIX.  */
#define WAIT_PREFIX	0
#define SEG_PREFIX	1
#define ADDR_PREFIX	2
#define DATA_PREFIX	3
#define LOCKREP_PREFIX	4
#define REX_PREFIX	5       /* must come last.  */
#define MAX_PREFIXES	6	/* max prefixes per opcode */

/* we define the syntax here (modulo base,index,scale syntax) */
#define REGISTER_PREFIX '%'
#define IMMEDIATE_PREFIX '$'
#define ABSOLUTE_PREFIX '*'

/* these are the instruction mnemonic suffixes.  */
#define WORD_MNEM_SUFFIX  'w'
#define BYTE_MNEM_SUFFIX  'b'
#define SHORT_MNEM_SUFFIX 's'
#define LONG_MNEM_SUFFIX  'l'
#define QWORD_MNEM_SUFFIX  'q'
/* Intel Syntax */
#define LONG_DOUBLE_MNEM_SUFFIX 'x'

#define END_OF_INSN '\0'

/*
  'templates' is for grouping together 'template' structures for opcodes
  of the same name.  This is only used for storing the insns in the grand
  ole hash table of insns.
  The templates themselves start at START and range up to (but not including)
  END.
  */
typedef struct
{
  const template *start;
  const template *end;
}
templates;

/* 386 operand encoding bytes:  see 386 book for details of this.  */
typedef struct
{
  unsigned int regmem;	/* codes register or memory operand */
  unsigned int reg;	/* codes register operand (or extended opcode) */
  unsigned int mode;	/* how to interpret regmem & reg */
}
modrm_byte;

/* x86-64 extension prefix.  */
typedef int rex_byte;

/* 386 opcode byte to code indirect addressing.  */
typedef struct
{
  unsigned base;
  unsigned index;
  unsigned scale;
}
sib_byte;

enum processor_type
{
  PROCESSOR_UNKNOWN,
  PROCESSOR_I486,
  PROCESSOR_PENTIUM,
  PROCESSOR_PENTIUMPRO,
  PROCESSOR_PENTIUM4,
  PROCESSOR_NOCONA,
  PROCESSOR_CORE,
  PROCESSOR_CORE2,
  PROCESSOR_K6,
  PROCESSOR_ATHLON,
  PROCESSOR_K8,
  PROCESSOR_GENERIC32,
  PROCESSOR_GENERIC64,
  PROCESSOR_AMDFAM10
};

/* x86 arch names, types and features */
typedef struct
{
  const char *name;		/* arch name */
  enum processor_type type;	/* arch type */
  unsigned int flags;		/* cpu feature flags */
}
arch_entry;

/* The name of the global offset table generated by the compiler. Allow
   this to be overridden if need be.  */
#ifndef GLOBAL_OFFSET_TABLE_NAME
#define GLOBAL_OFFSET_TABLE_NAME "_GLOBAL_OFFSET_TABLE_"
#endif

#if (defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)) && !defined (LEX_AT)
#define TC_PARSE_CONS_EXPRESSION(EXP, NBYTES) x86_cons (EXP, NBYTES)
extern void x86_cons (expressionS *, int);
#endif

#define TC_CONS_FIX_NEW(FRAG,OFF,LEN,EXP) x86_cons_fix_new(FRAG, OFF, LEN, EXP)
extern void x86_cons_fix_new
  (fragS *, unsigned int, unsigned int, expressionS *);

#define DIFF_EXPR_OK    /* foo-. gets turned into PC relative relocs */

#define NO_RELOC BFD_RELOC_NONE

void i386_validate_fix (struct fix *);
#define TC_VALIDATE_FIX(FIX,SEGTYPE,SKIP) i386_validate_fix(FIX)

#define tc_fix_adjustable(X)  tc_i386_fix_adjustable(X)
extern int tc_i386_fix_adjustable (struct fix *);

/* Values passed to md_apply_fix don't include the symbol value.  */
#define MD_APPLY_SYM_VALUE(FIX) 0

/* ELF wants external syms kept, as does PE COFF.  */
#if defined (TE_PE) && defined (STRICT_PE_FORMAT)
#define EXTERN_FORCE_RELOC				\
  (OUTPUT_FLAVOR == bfd_target_elf_flavour		\
   || OUTPUT_FLAVOR == bfd_target_coff_flavour)
#else
#define EXTERN_FORCE_RELOC				\
  (OUTPUT_FLAVOR == bfd_target_elf_flavour)
#endif

/* This expression evaluates to true if the relocation is for a local
   object for which we still want to do the relocation at runtime.
   False if we are willing to perform this relocation while building
   the .o file.  GOTOFF does not need to be checked here because it is
   not pcrel.  I am not sure if some of the others are ever used with
   pcrel, but it is easier to be safe than sorry.  */

#define TC_FORCE_RELOCATION_LOCAL(FIX)			\
  (!(FIX)->fx_pcrel					\
   || (FIX)->fx_r_type == BFD_RELOC_386_PLT32		\
   || (FIX)->fx_r_type == BFD_RELOC_386_GOT32		\
   || (FIX)->fx_r_type == BFD_RELOC_386_GOTPC		\
   || TC_FORCE_RELOCATION (FIX))

extern int i386_parse_name (char *, expressionS *, char *);
#define md_parse_name(s, e, m, c) i386_parse_name (s, e, c)

extern const struct relax_type md_relax_table[];
#define TC_GENERIC_RELAX_TABLE md_relax_table

extern int optimize_align_code;

#define md_do_align(n, fill, len, max, around)				\
if ((n)									\
    && !need_pass_2							\
    && optimize_align_code						\
    && (!(fill)								\
	|| ((char)*(fill) == (char)0x90 && (len) == 1))			\
    && subseg_text_p (now_seg))						\
  {									\
    frag_align_code ((n), (max));					\
    goto around;							\
  }

#define MAX_MEM_FOR_RS_ALIGN_CODE  15

extern void i386_align_code (fragS *, int);

#define HANDLE_ALIGN(fragP)						\
if (fragP->fr_type == rs_align_code) 					\
  i386_align_code (fragP, (fragP->fr_next->fr_address			\
			   - fragP->fr_address				\
			   - fragP->fr_fix));

void i386_print_statistics (FILE *);
#define tc_print_statistics i386_print_statistics

#define md_number_to_chars number_to_chars_littleendian

#ifdef SCO_ELF
#define tc_init_after_args() sco_id ()
extern void sco_id (void);
#endif

#define WORKING_DOT_WORD 1

/* We want .cfi_* pseudo-ops for generating unwind info.  */
#define TARGET_USE_CFIPOP 1

extern unsigned int x86_dwarf2_return_column;
#define DWARF2_DEFAULT_RETURN_COLUMN x86_dwarf2_return_column

extern int x86_cie_data_alignment;
#define DWARF2_CIE_DATA_ALIGNMENT x86_cie_data_alignment

#define tc_regname_to_dw2regnum tc_x86_regname_to_dw2regnum
extern int tc_x86_regname_to_dw2regnum (char *);

#define tc_cfi_frame_initial_instructions tc_x86_frame_initial_instructions
extern void tc_x86_frame_initial_instructions (void);

#define md_elf_section_type(str,len) i386_elf_section_type (str, len)
extern int i386_elf_section_type (const char *, size_t);

/* Support for SHF_X86_64_LARGE */
extern int x86_64_section_word (char *, size_t);
extern int x86_64_section_letter (int, char **);
#define md_elf_section_letter(LETTER, PTR_MSG)	x86_64_section_letter (LETTER, PTR_MSG)
#define md_elf_section_word(STR, LEN)		x86_64_section_word (STR, LEN)

#ifdef TE_PE

#define O_secrel O_md1

#define TC_DWARF2_EMIT_OFFSET  tc_pe_dwarf2_emit_offset
void tc_pe_dwarf2_emit_offset (symbolS *, unsigned int);

#endif /* TE_PE */

#endif /* TC_I386 */
