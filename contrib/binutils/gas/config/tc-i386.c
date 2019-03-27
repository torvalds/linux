/* tc-i386.c -- Assemble code for the Intel 80386
   Copyright 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
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

/* Intel 80386 machine specific gas.
   Written by Eliot Dresselhaus (eliot@mgm.mit.edu).
   x86_64 support by Jan Hubicka (jh@suse.cz)
   VIA PadLock support by Michal Ludvig (mludvig@suse.cz)
   Bugs & suggestions are completely welcome.  This is free software.
   Please help us make it better.  */

#include "as.h"
#include "safe-ctype.h"
#include "subsegs.h"
#include "dwarf2dbg.h"
#include "dw2gencfi.h"
#include "elf/x86-64.h"

#ifndef REGISTER_WARNINGS
#define REGISTER_WARNINGS 1
#endif

#ifndef INFER_ADDR_PREFIX
#define INFER_ADDR_PREFIX 1
#endif

#ifndef SCALE1_WHEN_NO_INDEX
/* Specifying a scale factor besides 1 when there is no index is
   futile.  eg. `mov (%ebx,2),%al' does exactly the same as
   `mov (%ebx),%al'.  To slavishly follow what the programmer
   specified, set SCALE1_WHEN_NO_INDEX to 0.  */
#define SCALE1_WHEN_NO_INDEX 1
#endif

#ifndef DEFAULT_ARCH
#define DEFAULT_ARCH "i386"
#endif

#ifndef INLINE
#if __GNUC__ >= 2
#define INLINE __inline__
#else
#define INLINE
#endif
#endif

static void set_code_flag (int);
static void set_16bit_gcc_code_flag (int);
static void set_intel_syntax (int);
static void set_cpu_arch (int);
#ifdef TE_PE
static void pe_directive_secrel (int);
#endif
static void signed_cons (int);
static char *output_invalid (int c);
static int i386_operand (char *);
static int i386_intel_operand (char *, int);
static const reg_entry *parse_register (char *, char **);
static char *parse_insn (char *, char *);
static char *parse_operands (char *, const char *);
static void swap_operands (void);
static void swap_2_operands (int, int);
static void optimize_imm (void);
static void optimize_disp (void);
static int match_template (void);
static int check_string (void);
static int process_suffix (void);
static int check_byte_reg (void);
static int check_long_reg (void);
static int check_qword_reg (void);
static int check_word_reg (void);
static int finalize_imm (void);
static int process_operands (void);
static const seg_entry *build_modrm_byte (void);
static void output_insn (void);
static void output_imm (fragS *, offsetT);
static void output_disp (fragS *, offsetT);
#ifndef I386COFF
static void s_bss (int);
#endif
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
static void handle_large_common (int small ATTRIBUTE_UNUSED);
#endif

static const char *default_arch = DEFAULT_ARCH;

/* 'md_assemble ()' gathers together information and puts it into a
   i386_insn.  */

union i386_op
  {
    expressionS *disps;
    expressionS *imms;
    const reg_entry *regs;
  };

struct _i386_insn
  {
    /* TM holds the template for the insn were currently assembling.  */
    template tm;

    /* SUFFIX holds the instruction mnemonic suffix if given.
       (e.g. 'l' for 'movl')  */
    char suffix;

    /* OPERANDS gives the number of given operands.  */
    unsigned int operands;

    /* REG_OPERANDS, DISP_OPERANDS, MEM_OPERANDS, IMM_OPERANDS give the number
       of given register, displacement, memory operands and immediate
       operands.  */
    unsigned int reg_operands, disp_operands, mem_operands, imm_operands;

    /* TYPES [i] is the type (see above #defines) which tells us how to
       use OP[i] for the corresponding operand.  */
    unsigned int types[MAX_OPERANDS];

    /* Displacement expression, immediate expression, or register for each
       operand.  */
    union i386_op op[MAX_OPERANDS];

    /* Flags for operands.  */
    unsigned int flags[MAX_OPERANDS];
#define Operand_PCrel 1

    /* Relocation type for operand */
    enum bfd_reloc_code_real reloc[MAX_OPERANDS];

    /* BASE_REG, INDEX_REG, and LOG2_SCALE_FACTOR are used to encode
       the base index byte below.  */
    const reg_entry *base_reg;
    const reg_entry *index_reg;
    unsigned int log2_scale_factor;

    /* SEG gives the seg_entries of this insn.  They are zero unless
       explicit segment overrides are given.  */
    const seg_entry *seg[2];

    /* PREFIX holds all the given prefix opcodes (usually null).
       PREFIXES is the number of prefix opcodes.  */
    unsigned int prefixes;
    unsigned char prefix[MAX_PREFIXES];

    /* RM and SIB are the modrm byte and the sib byte where the
       addressing modes of this insn are encoded.  */

    modrm_byte rm;
    rex_byte rex;
    sib_byte sib;
  };

typedef struct _i386_insn i386_insn;

/* List of chars besides those in app.c:symbol_chars that can start an
   operand.  Used to prevent the scrubber eating vital white-space.  */
const char extra_symbol_chars[] = "*%-(["
#ifdef LEX_AT
	"@"
#endif
#ifdef LEX_QM
	"?"
#endif
	;

#if (defined (TE_I386AIX)				\
     || ((defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF))	\
	 && !defined (TE_GNU)				\
	 && !defined (TE_LINUX)				\
 	 && !defined (TE_NETWARE)			\
	 && !defined (TE_FreeBSD)			\
	 && !defined (TE_NetBSD)))
/* This array holds the chars that always start a comment.  If the
   pre-processor is disabled, these aren't very useful.  The option
   --divide will remove '/' from this list.  */
const char *i386_comment_chars = "#/";
#define SVR4_COMMENT_CHARS 1
#define PREFIX_SEPARATOR '\\'

#else
const char *i386_comment_chars = "#";
#define PREFIX_SEPARATOR '/'
#endif

/* This array holds the chars that only start a comment at the beginning of
   a line.  If the line seems to have the form '# 123 filename'
   .line and .file directives will appear in the pre-processed output.
   Note that input_file.c hand checks for '#' at the beginning of the
   first line of the input file.  This is because the compiler outputs
   #NO_APP at the beginning of its output.
   Also note that comments started like this one will always work if
   '/' isn't otherwise defined.  */
const char line_comment_chars[] = "#/";

const char line_separator_chars[] = ";";

/* Chars that can be used to separate mant from exp in floating point
   nums.  */
const char EXP_CHARS[] = "eE";

/* Chars that mean this number is a floating point constant
   As in 0f12.456
   or    0d1.2345e12.  */
const char FLT_CHARS[] = "fFdDxX";

/* Tables for lexical analysis.  */
static char mnemonic_chars[256];
static char register_chars[256];
static char operand_chars[256];
static char identifier_chars[256];
static char digit_chars[256];

/* Lexical macros.  */
#define is_mnemonic_char(x) (mnemonic_chars[(unsigned char) x])
#define is_operand_char(x) (operand_chars[(unsigned char) x])
#define is_register_char(x) (register_chars[(unsigned char) x])
#define is_space_char(x) ((x) == ' ')
#define is_identifier_char(x) (identifier_chars[(unsigned char) x])
#define is_digit_char(x) (digit_chars[(unsigned char) x])

/* All non-digit non-letter characters that may occur in an operand.  */
static char operand_special_chars[] = "%$-+(,)*._~/<>|&^!:[@]";

/* md_assemble() always leaves the strings it's passed unaltered.  To
   effect this we maintain a stack of saved characters that we've smashed
   with '\0's (indicating end of strings for various sub-fields of the
   assembler instruction).  */
static char save_stack[32];
static char *save_stack_p;
#define END_STRING_AND_SAVE(s) \
	do { *save_stack_p++ = *(s); *(s) = '\0'; } while (0)
#define RESTORE_END_STRING(s) \
	do { *(s) = *--save_stack_p; } while (0)

/* The instruction we're assembling.  */
static i386_insn i;

/* Possible templates for current insn.  */
static const templates *current_templates;

/* Per instruction expressionS buffers: max displacements & immediates.  */
static expressionS disp_expressions[MAX_MEMORY_OPERANDS];
static expressionS im_expressions[MAX_IMMEDIATE_OPERANDS];

/* Current operand we are working on.  */
static int this_operand;

/* We support four different modes.  FLAG_CODE variable is used to distinguish
   these.  */

enum flag_code {
	CODE_32BIT,
	CODE_16BIT,
	CODE_64BIT };
#define NUM_FLAG_CODE ((int) CODE_64BIT + 1)

static enum flag_code flag_code;
static unsigned int object_64bit;
static int use_rela_relocations = 0;

/* The names used to print error messages.  */
static const char *flag_code_names[] =
  {
    "32",
    "16",
    "64"
  };

/* 1 for intel syntax,
   0 if att syntax.  */
static int intel_syntax = 0;

/* 1 if register prefix % not required.  */
static int allow_naked_reg = 0;

/* Register prefix used for error message.  */
static const char *register_prefix = "%";

/* Used in 16 bit gcc mode to add an l suffix to call, ret, enter,
   leave, push, and pop instructions so that gcc has the same stack
   frame as in 32 bit mode.  */
static char stackop_size = '\0';

/* Non-zero to optimize code alignment.  */
int optimize_align_code = 1;

/* Non-zero to quieten some warnings.  */
static int quiet_warnings = 0;

/* CPU name.  */
static const char *cpu_arch_name = NULL;
static const char *cpu_sub_arch_name = NULL;

/* CPU feature flags.  */
static unsigned int cpu_arch_flags = CpuUnknownFlags | CpuNo64;

/* If we have selected a cpu we are generating instructions for.  */
static int cpu_arch_tune_set = 0;

/* Cpu we are generating instructions for.  */
static enum processor_type cpu_arch_tune = PROCESSOR_UNKNOWN;

/* CPU feature flags of cpu we are generating instructions for.  */
static unsigned int cpu_arch_tune_flags = 0;

/* CPU instruction set architecture used.  */
static enum processor_type cpu_arch_isa = PROCESSOR_UNKNOWN;

/* CPU feature flags of instruction set architecture used.  */
static unsigned int cpu_arch_isa_flags = 0;

/* If set, conditional jumps are not automatically promoted to handle
   larger than a byte offset.  */
static unsigned int no_cond_jump_promotion = 0;

/* Pre-defined "_GLOBAL_OFFSET_TABLE_".  */
static symbolS *GOT_symbol;

/* The dwarf2 return column, adjusted for 32 or 64 bit.  */
unsigned int x86_dwarf2_return_column;

/* The dwarf2 data alignment, adjusted for 32 or 64 bit.  */
int x86_cie_data_alignment;

/* Interface to relax_segment.
   There are 3 major relax states for 386 jump insns because the
   different types of jumps add different sizes to frags when we're
   figuring out what sort of jump to choose to reach a given label.  */

/* Types.  */
#define UNCOND_JUMP 0
#define COND_JUMP 1
#define COND_JUMP86 2

/* Sizes.  */
#define CODE16	1
#define SMALL	0
#define SMALL16 (SMALL | CODE16)
#define BIG	2
#define BIG16	(BIG | CODE16)

#ifndef INLINE
#ifdef __GNUC__
#define INLINE __inline__
#else
#define INLINE
#endif
#endif

#define ENCODE_RELAX_STATE(type, size) \
  ((relax_substateT) (((type) << 2) | (size)))
#define TYPE_FROM_RELAX_STATE(s) \
  ((s) >> 2)
#define DISP_SIZE_FROM_RELAX_STATE(s) \
    ((((s) & 3) == BIG ? 4 : (((s) & 3) == BIG16 ? 2 : 1)))

/* This table is used by relax_frag to promote short jumps to long
   ones where necessary.  SMALL (short) jumps may be promoted to BIG
   (32 bit long) ones, and SMALL16 jumps to BIG16 (16 bit long).  We
   don't allow a short jump in a 32 bit code segment to be promoted to
   a 16 bit offset jump because it's slower (requires data size
   prefix), and doesn't work, unless the destination is in the bottom
   64k of the code segment (The top 16 bits of eip are zeroed).  */

const relax_typeS md_relax_table[] =
{
  /* The fields are:
     1) most positive reach of this state,
     2) most negative reach of this state,
     3) how many bytes this mode will have in the variable part of the frag
     4) which index into the table to try if we can't fit into this one.  */

  /* UNCOND_JUMP states.  */
  {127 + 1, -128 + 1, 1, ENCODE_RELAX_STATE (UNCOND_JUMP, BIG)},
  {127 + 1, -128 + 1, 1, ENCODE_RELAX_STATE (UNCOND_JUMP, BIG16)},
  /* dword jmp adds 4 bytes to frag:
     0 extra opcode bytes, 4 displacement bytes.  */
  {0, 0, 4, 0},
  /* word jmp adds 2 byte2 to frag:
     0 extra opcode bytes, 2 displacement bytes.  */
  {0, 0, 2, 0},

  /* COND_JUMP states.  */
  {127 + 1, -128 + 1, 1, ENCODE_RELAX_STATE (COND_JUMP, BIG)},
  {127 + 1, -128 + 1, 1, ENCODE_RELAX_STATE (COND_JUMP, BIG16)},
  /* dword conditionals adds 5 bytes to frag:
     1 extra opcode byte, 4 displacement bytes.  */
  {0, 0, 5, 0},
  /* word conditionals add 3 bytes to frag:
     1 extra opcode byte, 2 displacement bytes.  */
  {0, 0, 3, 0},

  /* COND_JUMP86 states.  */
  {127 + 1, -128 + 1, 1, ENCODE_RELAX_STATE (COND_JUMP86, BIG)},
  {127 + 1, -128 + 1, 1, ENCODE_RELAX_STATE (COND_JUMP86, BIG16)},
  /* dword conditionals adds 5 bytes to frag:
     1 extra opcode byte, 4 displacement bytes.  */
  {0, 0, 5, 0},
  /* word conditionals add 4 bytes to frag:
     1 displacement byte and a 3 byte long branch insn.  */
  {0, 0, 4, 0}
};

static const arch_entry cpu_arch[] =
{
  {"generic32", PROCESSOR_GENERIC32,
   Cpu186|Cpu286|Cpu386},
  {"generic64", PROCESSOR_GENERIC64,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuP4|CpuMMX
   |CpuMMX2|CpuSSE|CpuSSE2},
  {"i8086", PROCESSOR_UNKNOWN,
   0},
  {"i186", PROCESSOR_UNKNOWN,
   Cpu186},
  {"i286", PROCESSOR_UNKNOWN,
   Cpu186|Cpu286},
  {"i386", PROCESSOR_GENERIC32,
   Cpu186|Cpu286|Cpu386},
  {"i486", PROCESSOR_I486,
   Cpu186|Cpu286|Cpu386|Cpu486},
  {"i586", PROCESSOR_PENTIUM,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586},
  {"i686", PROCESSOR_PENTIUMPRO,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686},
  {"pentium", PROCESSOR_PENTIUM,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586},
  {"pentiumpro",PROCESSOR_PENTIUMPRO,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686},
  {"pentiumii",	PROCESSOR_PENTIUMPRO,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuMMX},
  {"pentiumiii",PROCESSOR_PENTIUMPRO,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuMMX|CpuMMX2|CpuSSE},
  {"pentium4", PROCESSOR_PENTIUM4,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuP4|CpuMMX
   |CpuMMX2|CpuSSE|CpuSSE2},
  {"prescott", PROCESSOR_NOCONA,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuP4|CpuMMX
   |CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3},
  {"nocona", PROCESSOR_NOCONA,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuP4|CpuMMX
   |CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3},
  {"yonah", PROCESSOR_CORE,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuP4|CpuMMX
   |CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3},
  {"core", PROCESSOR_CORE,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuP4|CpuMMX
   |CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3},
  {"merom", PROCESSOR_CORE2,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuP4|CpuMMX
   |CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3|CpuSSSE3},
  {"core2", PROCESSOR_CORE2,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuP4|CpuMMX
   |CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3|CpuSSSE3},
  {"k6", PROCESSOR_K6,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|CpuK6|CpuMMX},
  {"k6_2", PROCESSOR_K6,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|CpuK6|CpuMMX|Cpu3dnow},
  {"athlon", PROCESSOR_ATHLON,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuK6
   |CpuMMX|CpuMMX2|Cpu3dnow|Cpu3dnowA},
  {"sledgehammer", PROCESSOR_K8,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuK6
   |CpuSledgehammer|CpuMMX|CpuMMX2|Cpu3dnow|Cpu3dnowA|CpuSSE|CpuSSE2},
  {"opteron", PROCESSOR_K8,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuK6
   |CpuSledgehammer|CpuMMX|CpuMMX2|Cpu3dnow|Cpu3dnowA|CpuSSE|CpuSSE2},
  {"k8", PROCESSOR_K8,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuK6
   |CpuSledgehammer|CpuMMX|CpuMMX2|Cpu3dnow|Cpu3dnowA|CpuSSE|CpuSSE2},
  {"amdfam10", PROCESSOR_AMDFAM10,
   Cpu186|Cpu286|Cpu386|Cpu486|Cpu586|Cpu686|CpuK6|CpuSledgehammer
   |CpuMMX|CpuMMX2|Cpu3dnow|Cpu3dnowA|CpuSSE|CpuSSE2|CpuSSE3|CpuSSE4a
   |CpuABM},
  {".mmx", PROCESSOR_UNKNOWN,
   CpuMMX},
  {".sse", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|CpuSSE},
  {".sse2", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|CpuSSE|CpuSSE2},
  {".sse3", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3},
  {".ssse3", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3|CpuSSSE3},
  {".sse4.1", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3|CpuSSSE3|CpuSSE4_1},
  {".sse4.2", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3|CpuSSSE3|CpuSSE4},
  {".sse4", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3|CpuSSSE3|CpuSSE4},
  {".3dnow", PROCESSOR_UNKNOWN,
   CpuMMX|Cpu3dnow},
  {".3dnowa", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|Cpu3dnow|Cpu3dnowA},
  {".padlock", PROCESSOR_UNKNOWN,
   CpuPadLock},
  {".pacifica", PROCESSOR_UNKNOWN,
   CpuSVME},
  {".svme", PROCESSOR_UNKNOWN,
   CpuSVME},
  {".sse4a", PROCESSOR_UNKNOWN,
   CpuMMX|CpuMMX2|CpuSSE|CpuSSE2|CpuSSE3|CpuSSE4a},
  {".abm", PROCESSOR_UNKNOWN,
   CpuABM},
  {".xsave", PROCESSOR_UNKNOWN,
   CpuXSAVE}
};

const pseudo_typeS md_pseudo_table[] =
{
#if !defined(OBJ_AOUT) && !defined(USE_ALIGN_PTWO)
  {"align", s_align_bytes, 0},
#else
  {"align", s_align_ptwo, 0},
#endif
  {"arch", set_cpu_arch, 0},
#ifndef I386COFF
  {"bss", s_bss, 0},
#endif
  {"ffloat", float_cons, 'f'},
  {"dfloat", float_cons, 'd'},
  {"tfloat", float_cons, 'x'},
  {"value", cons, 2},
  {"slong", signed_cons, 4},
  {"noopt", s_ignore, 0},
  {"optim", s_ignore, 0},
  {"code16gcc", set_16bit_gcc_code_flag, CODE_16BIT},
  {"code16", set_code_flag, CODE_16BIT},
  {"code32", set_code_flag, CODE_32BIT},
  {"code64", set_code_flag, CODE_64BIT},
  {"intel_syntax", set_intel_syntax, 1},
  {"att_syntax", set_intel_syntax, 0},
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
  {"largecomm", handle_large_common, 0},
#else
  {"file", (void (*) (int)) dwarf2_directive_file, 0},
  {"loc", dwarf2_directive_loc, 0},
  {"loc_mark_labels", dwarf2_directive_loc_mark_labels, 0},
#endif
#ifdef TE_PE
  {"secrel32", pe_directive_secrel, 0},
#endif
  {0, 0, 0}
};

/* For interface with expression ().  */
extern char *input_line_pointer;

/* Hash table for instruction mnemonic lookup.  */
static struct hash_control *op_hash;

/* Hash table for register lookup.  */
static struct hash_control *reg_hash;

void
i386_align_code (fragS *fragP, int count)
{
  /* Various efficient no-op patterns for aligning code labels.
     Note: Don't try to assemble the instructions in the comments.
     0L and 0w are not legal.  */
  static const char f32_1[] =
    {0x90};					/* nop			*/
  static const char f32_2[] =
    {0x66,0x90};				/* xchg %ax,%ax */
  static const char f32_3[] =
    {0x8d,0x76,0x00};				/* leal 0(%esi),%esi	*/
  static const char f32_4[] =
    {0x8d,0x74,0x26,0x00};			/* leal 0(%esi,1),%esi	*/
  static const char f32_5[] =
    {0x90,					/* nop			*/
     0x8d,0x74,0x26,0x00};			/* leal 0(%esi,1),%esi	*/
  static const char f32_6[] =
    {0x8d,0xb6,0x00,0x00,0x00,0x00};		/* leal 0L(%esi),%esi	*/
  static const char f32_7[] =
    {0x8d,0xb4,0x26,0x00,0x00,0x00,0x00};	/* leal 0L(%esi,1),%esi */
  static const char f32_8[] =
    {0x90,					/* nop			*/
     0x8d,0xb4,0x26,0x00,0x00,0x00,0x00};	/* leal 0L(%esi,1),%esi */
  static const char f32_9[] =
    {0x89,0xf6,					/* movl %esi,%esi	*/
     0x8d,0xbc,0x27,0x00,0x00,0x00,0x00};	/* leal 0L(%edi,1),%edi */
  static const char f32_10[] =
    {0x8d,0x76,0x00,				/* leal 0(%esi),%esi	*/
     0x8d,0xbc,0x27,0x00,0x00,0x00,0x00};	/* leal 0L(%edi,1),%edi */
  static const char f32_11[] =
    {0x8d,0x74,0x26,0x00,			/* leal 0(%esi,1),%esi	*/
     0x8d,0xbc,0x27,0x00,0x00,0x00,0x00};	/* leal 0L(%edi,1),%edi */
  static const char f32_12[] =
    {0x8d,0xb6,0x00,0x00,0x00,0x00,		/* leal 0L(%esi),%esi	*/
     0x8d,0xbf,0x00,0x00,0x00,0x00};		/* leal 0L(%edi),%edi	*/
  static const char f32_13[] =
    {0x8d,0xb6,0x00,0x00,0x00,0x00,		/* leal 0L(%esi),%esi	*/
     0x8d,0xbc,0x27,0x00,0x00,0x00,0x00};	/* leal 0L(%edi,1),%edi */
  static const char f32_14[] =
    {0x8d,0xb4,0x26,0x00,0x00,0x00,0x00,	/* leal 0L(%esi,1),%esi */
     0x8d,0xbc,0x27,0x00,0x00,0x00,0x00};	/* leal 0L(%edi,1),%edi */
  static const char f32_15[] =
    {0xeb,0x0d,0x90,0x90,0x90,0x90,0x90,	/* jmp .+15; lotsa nops	*/
     0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
  static const char f16_3[] =
    {0x8d,0x74,0x00};				/* lea 0(%esi),%esi	*/
  static const char f16_4[] =
    {0x8d,0xb4,0x00,0x00};			/* lea 0w(%si),%si	*/
  static const char f16_5[] =
    {0x90,					/* nop			*/
     0x8d,0xb4,0x00,0x00};			/* lea 0w(%si),%si	*/
  static const char f16_6[] =
    {0x89,0xf6,					/* mov %si,%si		*/
     0x8d,0xbd,0x00,0x00};			/* lea 0w(%di),%di	*/
  static const char f16_7[] =
    {0x8d,0x74,0x00,				/* lea 0(%si),%si	*/
     0x8d,0xbd,0x00,0x00};			/* lea 0w(%di),%di	*/
  static const char f16_8[] =
    {0x8d,0xb4,0x00,0x00,			/* lea 0w(%si),%si	*/
     0x8d,0xbd,0x00,0x00};			/* lea 0w(%di),%di	*/
  static const char *const f32_patt[] = {
    f32_1, f32_2, f32_3, f32_4, f32_5, f32_6, f32_7, f32_8,
    f32_9, f32_10, f32_11, f32_12, f32_13, f32_14, f32_15
  };
  static const char *const f16_patt[] = {
    f32_1, f32_2, f16_3, f16_4, f16_5, f16_6, f16_7, f16_8,
    f32_15, f32_15, f32_15, f32_15, f32_15, f32_15, f32_15
  };
  /* nopl (%[re]ax) */
  static const char alt_3[] =
    {0x0f,0x1f,0x00};
  /* nopl 0(%[re]ax) */
  static const char alt_4[] =
    {0x0f,0x1f,0x40,0x00};
  /* nopl 0(%[re]ax,%[re]ax,1) */
  static const char alt_5[] =
    {0x0f,0x1f,0x44,0x00,0x00};
  /* nopw 0(%[re]ax,%[re]ax,1) */
  static const char alt_6[] =
    {0x66,0x0f,0x1f,0x44,0x00,0x00};
  /* nopl 0L(%[re]ax) */
  static const char alt_7[] =
    {0x0f,0x1f,0x80,0x00,0x00,0x00,0x00};
  /* nopl 0L(%[re]ax,%[re]ax,1) */
  static const char alt_8[] =
    {0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  /* nopw 0L(%[re]ax,%[re]ax,1) */
  static const char alt_9[] =
    {0x66,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  /* nopw %cs:0L(%[re]ax,%[re]ax,1) */
  static const char alt_10[] =
    {0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  /* data16
     nopw %cs:0L(%[re]ax,%[re]ax,1) */
  static const char alt_long_11[] =
    {0x66,
     0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  /* data16
     data16
     nopw %cs:0L(%[re]ax,%[re]ax,1) */
  static const char alt_long_12[] =
    {0x66,
     0x66,
     0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  /* data16
     data16
     data16
     nopw %cs:0L(%[re]ax,%[re]ax,1) */
  static const char alt_long_13[] =
    {0x66,
     0x66,
     0x66,
     0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  /* data16
     data16
     data16
     data16
     nopw %cs:0L(%[re]ax,%[re]ax,1) */
  static const char alt_long_14[] =
    {0x66,
     0x66,
     0x66,
     0x66,
     0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  /* data16
     data16
     data16
     data16
     data16
     nopw %cs:0L(%[re]ax,%[re]ax,1) */
  static const char alt_long_15[] =
    {0x66,
     0x66,
     0x66,
     0x66,
     0x66,
     0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  /* nopl 0(%[re]ax,%[re]ax,1)
     nopw 0(%[re]ax,%[re]ax,1) */
  static const char alt_short_11[] =
    {0x0f,0x1f,0x44,0x00,0x00,
     0x66,0x0f,0x1f,0x44,0x00,0x00};
  /* nopw 0(%[re]ax,%[re]ax,1)
     nopw 0(%[re]ax,%[re]ax,1) */
  static const char alt_short_12[] =
    {0x66,0x0f,0x1f,0x44,0x00,0x00,
     0x66,0x0f,0x1f,0x44,0x00,0x00};
  /* nopw 0(%[re]ax,%[re]ax,1)
     nopl 0L(%[re]ax) */
  static const char alt_short_13[] =
    {0x66,0x0f,0x1f,0x44,0x00,0x00,
     0x0f,0x1f,0x80,0x00,0x00,0x00,0x00};
  /* nopl 0L(%[re]ax)
     nopl 0L(%[re]ax) */
  static const char alt_short_14[] =
    {0x0f,0x1f,0x80,0x00,0x00,0x00,0x00,
     0x0f,0x1f,0x80,0x00,0x00,0x00,0x00};
  /* nopl 0L(%[re]ax)
     nopl 0L(%[re]ax,%[re]ax,1) */
  static const char alt_short_15[] =
    {0x0f,0x1f,0x80,0x00,0x00,0x00,0x00,
     0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
  static const char *const alt_short_patt[] = {
    f32_1, f32_2, alt_3, alt_4, alt_5, alt_6, alt_7, alt_8,
    alt_9, alt_10, alt_short_11, alt_short_12, alt_short_13,
    alt_short_14, alt_short_15
  };
  static const char *const alt_long_patt[] = {
    f32_1, f32_2, alt_3, alt_4, alt_5, alt_6, alt_7, alt_8,
    alt_9, alt_10, alt_long_11, alt_long_12, alt_long_13,
    alt_long_14, alt_long_15
  };

  if (count <= 0 || count > 15)
    return;

  /* We need to decide which NOP sequence to use for 32bit and
     64bit. When -mtune= is used:

     1. For PROCESSOR_I486, PROCESSOR_PENTIUM and PROCESSOR_GENERIC32,
     f32_patt will be used.
     2. For PROCESSOR_K8 and PROCESSOR_AMDFAM10 in 64bit, NOPs with
     0x66 prefix will be used.
     3. For PROCESSOR_CORE2, alt_long_patt will be used.
     4. For PROCESSOR_PENTIUMPRO, PROCESSOR_PENTIUM4, PROCESSOR_NOCONA,
     PROCESSOR_CORE, PROCESSOR_CORE2, PROCESSOR_K6, PROCESSOR_ATHLON
     and PROCESSOR_GENERIC64, alt_short_patt will be used.

     When -mtune= isn't used, alt_short_patt will be used if
     cpu_arch_isa_flags has Cpu686. Otherwise, f32_patt will be used.

     When -march= or .arch is used, we can't use anything beyond
     cpu_arch_isa_flags.   */

  if (flag_code == CODE_16BIT)
    {
      memcpy (fragP->fr_literal + fragP->fr_fix,
	      f16_patt[count - 1], count);
      if (count > 8)
	/* Adjust jump offset.  */
	fragP->fr_literal[fragP->fr_fix + 1] = count - 2;
    }
  else if (flag_code == CODE_64BIT && cpu_arch_tune == PROCESSOR_K8)
    {
      int i;
      int nnops = (count + 3) / 4;
      int len = count / nnops;
      int remains = count - nnops * len;
      int pos = 0;

      /* The recommended way to pad 64bit code is to use NOPs preceded
	 by maximally four 0x66 prefixes.  Balance the size of nops.  */
      for (i = 0; i < remains; i++)
	{
	  memset (fragP->fr_literal + fragP->fr_fix + pos, 0x66, len);
	  fragP->fr_literal[fragP->fr_fix + pos + len] = 0x90;
	  pos += len + 1;
	}
      for (; i < nnops; i++)
	{
	  memset (fragP->fr_literal + fragP->fr_fix + pos, 0x66, len - 1);
	  fragP->fr_literal[fragP->fr_fix + pos + len - 1] = 0x90;
	  pos += len;
	}
    }
  else
    {
      const char *const *patt = NULL;

      if (cpu_arch_isa == PROCESSOR_UNKNOWN)
	{
	  /* PROCESSOR_UNKNOWN means that all ISAs may be used.  */
	  switch (cpu_arch_tune)
	    {
	    case PROCESSOR_UNKNOWN:
	      /* We use cpu_arch_isa_flags to check if we SHOULD
		 optimize for Cpu686.  */
	      if ((cpu_arch_isa_flags & Cpu686) != 0)
		patt = alt_short_patt;
	      else
		patt = f32_patt;
	      break;
	    case PROCESSOR_CORE2:
	      patt = alt_long_patt;
	      break;
	    case PROCESSOR_PENTIUMPRO:
	    case PROCESSOR_PENTIUM4:
	    case PROCESSOR_NOCONA:
	    case PROCESSOR_CORE:
	    case PROCESSOR_K6:
	    case PROCESSOR_ATHLON:
	    case PROCESSOR_K8:
	    case PROCESSOR_GENERIC64:
	    case PROCESSOR_AMDFAM10:
	      patt = alt_short_patt;
	      break;
	    case PROCESSOR_I486:
	    case PROCESSOR_PENTIUM:
	    case PROCESSOR_GENERIC32:
	      patt = f32_patt;
	      break;
	    }
	}
      else
	{
	  switch (cpu_arch_tune)
	    {
	    case PROCESSOR_UNKNOWN:
	      /* When cpu_arch_isa is net, cpu_arch_tune shouldn't be
		 PROCESSOR_UNKNOWN.  */
	      abort ();
	      break;

	    case PROCESSOR_I486:
	    case PROCESSOR_PENTIUM:
	    case PROCESSOR_PENTIUMPRO:
	    case PROCESSOR_PENTIUM4:
	    case PROCESSOR_NOCONA:
	    case PROCESSOR_CORE:
	    case PROCESSOR_K6:
	    case PROCESSOR_ATHLON:
	    case PROCESSOR_K8:
	    case PROCESSOR_AMDFAM10:
	    case PROCESSOR_GENERIC32:
	      /* We use cpu_arch_isa_flags to check if we CAN optimize
		 for Cpu686.  */
	      if ((cpu_arch_isa_flags & Cpu686) != 0)
		patt = alt_short_patt;
	      else
		patt = f32_patt;
	      break;
	    case PROCESSOR_CORE2:
	      if ((cpu_arch_isa_flags & Cpu686) != 0)
		patt = alt_long_patt;
	      else
		patt = f32_patt;
	      break;
	    case PROCESSOR_GENERIC64:
	      patt = alt_short_patt;
	      break;
	    }
	}

      memcpy (fragP->fr_literal + fragP->fr_fix,
	      patt[count - 1], count);
    }
  fragP->fr_var = count;
}

static INLINE unsigned int
mode_from_disp_size (unsigned int t)
{
  return (t & Disp8) ? 1 : (t & (Disp16 | Disp32 | Disp32S)) ? 2 : 0;
}

static INLINE int
fits_in_signed_byte (offsetT num)
{
  return (num >= -128) && (num <= 127);
}

static INLINE int
fits_in_unsigned_byte (offsetT num)
{
  return (num & 0xff) == num;
}

static INLINE int
fits_in_unsigned_word (offsetT num)
{
  return (num & 0xffff) == num;
}

static INLINE int
fits_in_signed_word (offsetT num)
{
  return (-32768 <= num) && (num <= 32767);
}

static INLINE int
fits_in_signed_long (offsetT num ATTRIBUTE_UNUSED)
{
#ifndef BFD64
  return 1;
#else
  return (!(-((offsetT) 1 << 31) & num)
	  || (-((offsetT) 1 << 31) & num) == -((offsetT) 1 << 31));
#endif
}				/* fits_in_signed_long() */

static INLINE int
fits_in_unsigned_long (offsetT num ATTRIBUTE_UNUSED)
{
#ifndef BFD64
  return 1;
#else
  return (num & (((offsetT) 2 << 31) - 1)) == num;
#endif
}				/* fits_in_unsigned_long() */

static unsigned int
smallest_imm_type (offsetT num)
{
  if (cpu_arch_flags != (Cpu186 | Cpu286 | Cpu386 | Cpu486 | CpuNo64))
    {
      /* This code is disabled on the 486 because all the Imm1 forms
	 in the opcode table are slower on the i486.  They're the
	 versions with the implicitly specified single-position
	 displacement, which has another syntax if you really want to
	 use that form.  */
      if (num == 1)
	return Imm1 | Imm8 | Imm8S | Imm16 | Imm32 | Imm32S | Imm64;
    }
  return (fits_in_signed_byte (num)
	  ? (Imm8S | Imm8 | Imm16 | Imm32 | Imm32S | Imm64)
	  : fits_in_unsigned_byte (num)
	  ? (Imm8 | Imm16 | Imm32 | Imm32S | Imm64)
	  : (fits_in_signed_word (num) || fits_in_unsigned_word (num))
	  ? (Imm16 | Imm32 | Imm32S | Imm64)
	  : fits_in_signed_long (num)
	  ? (Imm32 | Imm32S | Imm64)
	  : fits_in_unsigned_long (num)
	  ? (Imm32 | Imm64)
	  : Imm64);
}

static offsetT
offset_in_range (offsetT val, int size)
{
  addressT mask;

  switch (size)
    {
    case 1: mask = ((addressT) 1 <<  8) - 1; break;
    case 2: mask = ((addressT) 1 << 16) - 1; break;
    case 4: mask = ((addressT) 2 << 31) - 1; break;
#ifdef BFD64
    case 8: mask = ((addressT) 2 << 63) - 1; break;
#endif
    default: abort ();
    }

  /* If BFD64, sign extend val.  */
  if (!use_rela_relocations)
    if ((val & ~(((addressT) 2 << 31) - 1)) == 0)
      val = (val ^ ((addressT) 1 << 31)) - ((addressT) 1 << 31);

  if ((val & ~mask) != 0 && (val & ~mask) != ~mask)
    {
      char buf1[40], buf2[40];

      sprint_value (buf1, val);
      sprint_value (buf2, val & mask);
      as_warn (_("%s shortened to %s"), buf1, buf2);
    }
  return val & mask;
}

/* Returns 0 if attempting to add a prefix where one from the same
   class already exists, 1 if non rep/repne added, 2 if rep/repne
   added.  */
static int
add_prefix (unsigned int prefix)
{
  int ret = 1;
  unsigned int q;

  if (prefix >= REX_OPCODE && prefix < REX_OPCODE + 16
      && flag_code == CODE_64BIT)
    {
      if ((i.prefix[REX_PREFIX] & prefix & REX_W)
	  || ((i.prefix[REX_PREFIX] & (REX_R | REX_X | REX_B))
	      && (prefix & (REX_R | REX_X | REX_B))))
	ret = 0;
      q = REX_PREFIX;
    }
  else
    {
      switch (prefix)
	{
	default:
	  abort ();

	case CS_PREFIX_OPCODE:
	case DS_PREFIX_OPCODE:
	case ES_PREFIX_OPCODE:
	case FS_PREFIX_OPCODE:
	case GS_PREFIX_OPCODE:
	case SS_PREFIX_OPCODE:
	  q = SEG_PREFIX;
	  break;

	case REPNE_PREFIX_OPCODE:
	case REPE_PREFIX_OPCODE:
	  ret = 2;
	  /* fall thru */
	case LOCK_PREFIX_OPCODE:
	  q = LOCKREP_PREFIX;
	  break;

	case FWAIT_OPCODE:
	  q = WAIT_PREFIX;
	  break;

	case ADDR_PREFIX_OPCODE:
	  q = ADDR_PREFIX;
	  break;

	case DATA_PREFIX_OPCODE:
	  q = DATA_PREFIX;
	  break;
	}
      if (i.prefix[q] != 0)
	ret = 0;
    }

  if (ret)
    {
      if (!i.prefix[q])
	++i.prefixes;
      i.prefix[q] |= prefix;
    }
  else
    as_bad (_("same type of prefix used twice"));

  return ret;
}

static void
set_code_flag (int value)
{
  flag_code = value;
  cpu_arch_flags &= ~(Cpu64 | CpuNo64);
  cpu_arch_flags |= (flag_code == CODE_64BIT ? Cpu64 : CpuNo64);
  if (value == CODE_64BIT && !(cpu_arch_flags & CpuSledgehammer))
    {
      as_bad (_("64bit mode not supported on this CPU."));
    }
  if (value == CODE_32BIT && !(cpu_arch_flags & Cpu386))
    {
      as_bad (_("32bit mode not supported on this CPU."));
    }
  stackop_size = '\0';
}

static void
set_16bit_gcc_code_flag (int new_code_flag)
{
  flag_code = new_code_flag;
  cpu_arch_flags &= ~(Cpu64 | CpuNo64);
  cpu_arch_flags |= (flag_code == CODE_64BIT ? Cpu64 : CpuNo64);
  stackop_size = LONG_MNEM_SUFFIX;
}

static void
set_intel_syntax (int syntax_flag)
{
  /* Find out if register prefixing is specified.  */
  int ask_naked_reg = 0;

  SKIP_WHITESPACE ();
  if (!is_end_of_line[(unsigned char) *input_line_pointer])
    {
      char *string = input_line_pointer;
      int e = get_symbol_end ();

      if (strcmp (string, "prefix") == 0)
	ask_naked_reg = 1;
      else if (strcmp (string, "noprefix") == 0)
	ask_naked_reg = -1;
      else
	as_bad (_("bad argument to syntax directive."));
      *input_line_pointer = e;
    }
  demand_empty_rest_of_line ();

  intel_syntax = syntax_flag;

  if (ask_naked_reg == 0)
    allow_naked_reg = (intel_syntax
		       && (bfd_get_symbol_leading_char (stdoutput) != '\0'));
  else
    allow_naked_reg = (ask_naked_reg < 0);

  identifier_chars['%'] = intel_syntax && allow_naked_reg ? '%' : 0;
  identifier_chars['$'] = intel_syntax ? '$' : 0;
  register_prefix = allow_naked_reg ? "" : "%";
}

static void
set_cpu_arch (int dummy ATTRIBUTE_UNUSED)
{
  SKIP_WHITESPACE ();

  if (!is_end_of_line[(unsigned char) *input_line_pointer])
    {
      char *string = input_line_pointer;
      int e = get_symbol_end ();
      unsigned int i;

      for (i = 0; i < ARRAY_SIZE (cpu_arch); i++)
	{
	  if (strcmp (string, cpu_arch[i].name) == 0)
	    {
	      if (*string != '.')
		{
		  cpu_arch_name = cpu_arch[i].name;
		  cpu_sub_arch_name = NULL;
		  cpu_arch_flags = (cpu_arch[i].flags
				    | (flag_code == CODE_64BIT
				       ? Cpu64 : CpuNo64));
		  cpu_arch_isa = cpu_arch[i].type;
		  cpu_arch_isa_flags = cpu_arch[i].flags;
		  if (!cpu_arch_tune_set)
		    {
		      cpu_arch_tune = cpu_arch_isa;
		      cpu_arch_tune_flags = cpu_arch_isa_flags;
		    }
		  break;
		}
	      if ((cpu_arch_flags | cpu_arch[i].flags) != cpu_arch_flags)
		{
		  cpu_sub_arch_name = cpu_arch[i].name;
		  cpu_arch_flags |= cpu_arch[i].flags;
		}
	      *input_line_pointer = e;
	      demand_empty_rest_of_line ();
	      return;
	    }
	}
      if (i >= ARRAY_SIZE (cpu_arch))
	as_bad (_("no such architecture: `%s'"), string);

      *input_line_pointer = e;
    }
  else
    as_bad (_("missing cpu architecture"));

  no_cond_jump_promotion = 0;
  if (*input_line_pointer == ','
      && !is_end_of_line[(unsigned char) input_line_pointer[1]])
    {
      char *string = ++input_line_pointer;
      int e = get_symbol_end ();

      if (strcmp (string, "nojumps") == 0)
	no_cond_jump_promotion = 1;
      else if (strcmp (string, "jumps") == 0)
	;
      else
	as_bad (_("no such architecture modifier: `%s'"), string);

      *input_line_pointer = e;
    }

  demand_empty_rest_of_line ();
}

unsigned long
i386_mach ()
{
  if (!strcmp (default_arch, "x86_64"))
    return bfd_mach_x86_64;
  else if (!strcmp (default_arch, "i386"))
    return bfd_mach_i386_i386;
  else
    as_fatal (_("Unknown architecture"));
}

void
md_begin ()
{
  const char *hash_err;

  /* Initialize op_hash hash table.  */
  op_hash = hash_new ();

  {
    const template *optab;
    templates *core_optab;

    /* Setup for loop.  */
    optab = i386_optab;
    core_optab = (templates *) xmalloc (sizeof (templates));
    core_optab->start = optab;

    while (1)
      {
	++optab;
	if (optab->name == NULL
	    || strcmp (optab->name, (optab - 1)->name) != 0)
	  {
	    /* different name --> ship out current template list;
	       add to hash table; & begin anew.  */
	    core_optab->end = optab;
	    hash_err = hash_insert (op_hash,
				    (optab - 1)->name,
				    (PTR) core_optab);
	    if (hash_err)
	      {
		as_fatal (_("Internal Error:  Can't hash %s: %s"),
			  (optab - 1)->name,
			  hash_err);
	      }
	    if (optab->name == NULL)
	      break;
	    core_optab = (templates *) xmalloc (sizeof (templates));
	    core_optab->start = optab;
	  }
      }
  }

  /* Initialize reg_hash hash table.  */
  reg_hash = hash_new ();
  {
    const reg_entry *regtab;
    unsigned int regtab_size = i386_regtab_size;

    for (regtab = i386_regtab; regtab_size--; regtab++)
      {
	hash_err = hash_insert (reg_hash, regtab->reg_name, (PTR) regtab);
	if (hash_err)
	  as_fatal (_("Internal Error:  Can't hash %s: %s"),
		    regtab->reg_name,
		    hash_err);
      }
  }

  /* Fill in lexical tables:  mnemonic_chars, operand_chars.  */
  {
    int c;
    char *p;

    for (c = 0; c < 256; c++)
      {
	if (ISDIGIT (c))
	  {
	    digit_chars[c] = c;
	    mnemonic_chars[c] = c;
	    register_chars[c] = c;
	    operand_chars[c] = c;
	  }
	else if (ISLOWER (c))
	  {
	    mnemonic_chars[c] = c;
	    register_chars[c] = c;
	    operand_chars[c] = c;
	  }
	else if (ISUPPER (c))
	  {
	    mnemonic_chars[c] = TOLOWER (c);
	    register_chars[c] = mnemonic_chars[c];
	    operand_chars[c] = c;
	  }

	if (ISALPHA (c) || ISDIGIT (c))
	  identifier_chars[c] = c;
	else if (c >= 128)
	  {
	    identifier_chars[c] = c;
	    operand_chars[c] = c;
	  }
      }

#ifdef LEX_AT
    identifier_chars['@'] = '@';
#endif
#ifdef LEX_QM
    identifier_chars['?'] = '?';
    operand_chars['?'] = '?';
#endif
    digit_chars['-'] = '-';
    mnemonic_chars['-'] = '-';
    mnemonic_chars['.'] = '.';
    identifier_chars['_'] = '_';
    identifier_chars['.'] = '.';

    for (p = operand_special_chars; *p != '\0'; p++)
      operand_chars[(unsigned char) *p] = *p;
  }

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
  if (IS_ELF)
    {
      record_alignment (text_section, 2);
      record_alignment (data_section, 2);
      record_alignment (bss_section, 2);
    }
#endif

  if (flag_code == CODE_64BIT)
    {
      x86_dwarf2_return_column = 16;
      x86_cie_data_alignment = -8;
    }
  else
    {
      x86_dwarf2_return_column = 8;
      x86_cie_data_alignment = -4;
    }
}

void
i386_print_statistics (FILE *file)
{
  hash_print_statistics (file, "i386 opcode", op_hash);
  hash_print_statistics (file, "i386 register", reg_hash);
}

#ifdef DEBUG386

/* Debugging routines for md_assemble.  */
static void pte (template *);
static void pt (unsigned int);
static void pe (expressionS *);
static void ps (symbolS *);

static void
pi (char *line, i386_insn *x)
{
  unsigned int i;

  fprintf (stdout, "%s: template ", line);
  pte (&x->tm);
  fprintf (stdout, "  address: base %s  index %s  scale %x\n",
	   x->base_reg ? x->base_reg->reg_name : "none",
	   x->index_reg ? x->index_reg->reg_name : "none",
	   x->log2_scale_factor);
  fprintf (stdout, "  modrm:  mode %x  reg %x  reg/mem %x\n",
	   x->rm.mode, x->rm.reg, x->rm.regmem);
  fprintf (stdout, "  sib:  base %x  index %x  scale %x\n",
	   x->sib.base, x->sib.index, x->sib.scale);
  fprintf (stdout, "  rex: 64bit %x  extX %x  extY %x  extZ %x\n",
	   (x->rex & REX_W) != 0,
	   (x->rex & REX_R) != 0,
	   (x->rex & REX_X) != 0,
	   (x->rex & REX_B) != 0);
  for (i = 0; i < x->operands; i++)
    {
      fprintf (stdout, "    #%d:  ", i + 1);
      pt (x->types[i]);
      fprintf (stdout, "\n");
      if (x->types[i]
	  & (Reg | SReg2 | SReg3 | Control | Debug | Test | RegMMX | RegXMM))
	fprintf (stdout, "%s\n", x->op[i].regs->reg_name);
      if (x->types[i] & Imm)
	pe (x->op[i].imms);
      if (x->types[i] & Disp)
	pe (x->op[i].disps);
    }
}

static void
pte (template *t)
{
  unsigned int i;
  fprintf (stdout, " %d operands ", t->operands);
  fprintf (stdout, "opcode %x ", t->base_opcode);
  if (t->extension_opcode != None)
    fprintf (stdout, "ext %x ", t->extension_opcode);
  if (t->opcode_modifier & D)
    fprintf (stdout, "D");
  if (t->opcode_modifier & W)
    fprintf (stdout, "W");
  fprintf (stdout, "\n");
  for (i = 0; i < t->operands; i++)
    {
      fprintf (stdout, "    #%d type ", i + 1);
      pt (t->operand_types[i]);
      fprintf (stdout, "\n");
    }
}

static void
pe (expressionS *e)
{
  fprintf (stdout, "    operation     %d\n", e->X_op);
  fprintf (stdout, "    add_number    %ld (%lx)\n",
	   (long) e->X_add_number, (long) e->X_add_number);
  if (e->X_add_symbol)
    {
      fprintf (stdout, "    add_symbol    ");
      ps (e->X_add_symbol);
      fprintf (stdout, "\n");
    }
  if (e->X_op_symbol)
    {
      fprintf (stdout, "    op_symbol    ");
      ps (e->X_op_symbol);
      fprintf (stdout, "\n");
    }
}

static void
ps (symbolS *s)
{
  fprintf (stdout, "%s type %s%s",
	   S_GET_NAME (s),
	   S_IS_EXTERNAL (s) ? "EXTERNAL " : "",
	   segment_name (S_GET_SEGMENT (s)));
}

static struct type_name
  {
    unsigned int mask;
    char *tname;
  }
const type_names[] =
{
  { Reg8, "r8" },
  { Reg16, "r16" },
  { Reg32, "r32" },
  { Reg64, "r64" },
  { Imm8, "i8" },
  { Imm8S, "i8s" },
  { Imm16, "i16" },
  { Imm32, "i32" },
  { Imm32S, "i32s" },
  { Imm64, "i64" },
  { Imm1, "i1" },
  { BaseIndex, "BaseIndex" },
  { Disp8, "d8" },
  { Disp16, "d16" },
  { Disp32, "d32" },
  { Disp32S, "d32s" },
  { Disp64, "d64" },
  { InOutPortReg, "InOutPortReg" },
  { ShiftCount, "ShiftCount" },
  { Control, "control reg" },
  { Test, "test reg" },
  { Debug, "debug reg" },
  { FloatReg, "FReg" },
  { FloatAcc, "FAcc" },
  { SReg2, "SReg2" },
  { SReg3, "SReg3" },
  { Acc, "Acc" },
  { JumpAbsolute, "Jump Absolute" },
  { RegMMX, "rMMX" },
  { RegXMM, "rXMM" },
  { EsSeg, "es" },
  { 0, "" }
};

static void
pt (t)
     unsigned int t;
{
  const struct type_name *ty;

  for (ty = type_names; ty->mask; ty++)
    if (t & ty->mask)
      fprintf (stdout, "%s, ", ty->tname);
  fflush (stdout);
}

#endif /* DEBUG386 */

static bfd_reloc_code_real_type
reloc (unsigned int size,
       int pcrel,
       int sign,
       bfd_reloc_code_real_type other)
{
  if (other != NO_RELOC)
    {
      reloc_howto_type *reloc;

      if (size == 8)
	switch (other)
	  {
	  case BFD_RELOC_X86_64_GOT32:
	    return BFD_RELOC_X86_64_GOT64;
	    break;
	  case BFD_RELOC_X86_64_PLTOFF64:
	    return BFD_RELOC_X86_64_PLTOFF64;
	    break;
	  case BFD_RELOC_X86_64_GOTPC32:
	    other = BFD_RELOC_X86_64_GOTPC64;
	    break;
	  case BFD_RELOC_X86_64_GOTPCREL:
	    other = BFD_RELOC_X86_64_GOTPCREL64;
	    break;
	  case BFD_RELOC_X86_64_TPOFF32:
	    other = BFD_RELOC_X86_64_TPOFF64;
	    break;
	  case BFD_RELOC_X86_64_DTPOFF32:
	    other = BFD_RELOC_X86_64_DTPOFF64;
	    break;
	  default:
	    break;
	  }

      /* Sign-checking 4-byte relocations in 16-/32-bit code is pointless.  */
      if (size == 4 && flag_code != CODE_64BIT)
	sign = -1;

      reloc = bfd_reloc_type_lookup (stdoutput, other);
      if (!reloc)
	as_bad (_("unknown relocation (%u)"), other);
      else if (size != bfd_get_reloc_size (reloc))
	as_bad (_("%u-byte relocation cannot be applied to %u-byte field"),
		bfd_get_reloc_size (reloc),
		size);
      else if (pcrel && !reloc->pc_relative)
	as_bad (_("non-pc-relative relocation for pc-relative field"));
      else if ((reloc->complain_on_overflow == complain_overflow_signed
		&& !sign)
	       || (reloc->complain_on_overflow == complain_overflow_unsigned
		   && sign > 0))
	as_bad (_("relocated field and relocation type differ in signedness"));
      else
	return other;
      return NO_RELOC;
    }

  if (pcrel)
    {
      if (!sign)
	as_bad (_("there are no unsigned pc-relative relocations"));
      switch (size)
	{
	case 1: return BFD_RELOC_8_PCREL;
	case 2: return BFD_RELOC_16_PCREL;
	case 4: return BFD_RELOC_32_PCREL;
	case 8: return BFD_RELOC_64_PCREL;
	}
      as_bad (_("cannot do %u byte pc-relative relocation"), size);
    }
  else
    {
      if (sign > 0)
	switch (size)
	  {
	  case 4: return BFD_RELOC_X86_64_32S;
	  }
      else
	switch (size)
	  {
	  case 1: return BFD_RELOC_8;
	  case 2: return BFD_RELOC_16;
	  case 4: return BFD_RELOC_32;
	  case 8: return BFD_RELOC_64;
	  }
      as_bad (_("cannot do %s %u byte relocation"),
	      sign > 0 ? "signed" : "unsigned", size);
    }

  abort ();
  return BFD_RELOC_NONE;
}

/* Here we decide which fixups can be adjusted to make them relative to
   the beginning of the section instead of the symbol.  Basically we need
   to make sure that the dynamic relocations are done correctly, so in
   some cases we force the original symbol to be used.  */

int
tc_i386_fix_adjustable (fixS *fixP ATTRIBUTE_UNUSED)
{
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
  if (!IS_ELF)
    return 1;

  /* Don't adjust pc-relative references to merge sections in 64-bit
     mode.  */
  if (use_rela_relocations
      && (S_GET_SEGMENT (fixP->fx_addsy)->flags & SEC_MERGE) != 0
      && fixP->fx_pcrel)
    return 0;

  /* The x86_64 GOTPCREL are represented as 32bit PCrel relocations
     and changed later by validate_fix.  */
  if (GOT_symbol && fixP->fx_subsy == GOT_symbol
      && fixP->fx_r_type == BFD_RELOC_32_PCREL)
    return 0;

  /* adjust_reloc_syms doesn't know about the GOT.  */
  if (fixP->fx_r_type == BFD_RELOC_386_GOTOFF
      || fixP->fx_r_type == BFD_RELOC_386_PLT32
      || fixP->fx_r_type == BFD_RELOC_386_GOT32
      || fixP->fx_r_type == BFD_RELOC_386_TLS_GD
      || fixP->fx_r_type == BFD_RELOC_386_TLS_LDM
      || fixP->fx_r_type == BFD_RELOC_386_TLS_LDO_32
      || fixP->fx_r_type == BFD_RELOC_386_TLS_IE_32
      || fixP->fx_r_type == BFD_RELOC_386_TLS_IE
      || fixP->fx_r_type == BFD_RELOC_386_TLS_GOTIE
      || fixP->fx_r_type == BFD_RELOC_386_TLS_LE_32
      || fixP->fx_r_type == BFD_RELOC_386_TLS_LE
      || fixP->fx_r_type == BFD_RELOC_386_TLS_GOTDESC
      || fixP->fx_r_type == BFD_RELOC_386_TLS_DESC_CALL
      || fixP->fx_r_type == BFD_RELOC_X86_64_PLT32
      || fixP->fx_r_type == BFD_RELOC_X86_64_GOT32
      || fixP->fx_r_type == BFD_RELOC_X86_64_GOTPCREL
      || fixP->fx_r_type == BFD_RELOC_X86_64_TLSGD
      || fixP->fx_r_type == BFD_RELOC_X86_64_TLSLD
      || fixP->fx_r_type == BFD_RELOC_X86_64_DTPOFF32
      || fixP->fx_r_type == BFD_RELOC_X86_64_DTPOFF64
      || fixP->fx_r_type == BFD_RELOC_X86_64_GOTTPOFF
      || fixP->fx_r_type == BFD_RELOC_X86_64_TPOFF32
      || fixP->fx_r_type == BFD_RELOC_X86_64_TPOFF64
      || fixP->fx_r_type == BFD_RELOC_X86_64_GOTOFF64
      || fixP->fx_r_type == BFD_RELOC_X86_64_GOTPC32_TLSDESC
      || fixP->fx_r_type == BFD_RELOC_X86_64_TLSDESC_CALL
      || fixP->fx_r_type == BFD_RELOC_VTABLE_INHERIT
      || fixP->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    return 0;
#endif
  return 1;
}

static int
intel_float_operand (const char *mnemonic)
{
  /* Note that the value returned is meaningful only for opcodes with (memory)
     operands, hence the code here is free to improperly handle opcodes that
     have no operands (for better performance and smaller code). */

  if (mnemonic[0] != 'f')
    return 0; /* non-math */

  switch (mnemonic[1])
    {
    /* fclex, fdecstp, fdisi, femms, feni, fincstp, finit, fsetpm, and
       the fs segment override prefix not currently handled because no
       call path can make opcodes without operands get here */
    case 'i':
      return 2 /* integer op */;
    case 'l':
      if (mnemonic[2] == 'd' && (mnemonic[3] == 'c' || mnemonic[3] == 'e'))
	return 3; /* fldcw/fldenv */
      break;
    case 'n':
      if (mnemonic[2] != 'o' /* fnop */)
	return 3; /* non-waiting control op */
      break;
    case 'r':
      if (mnemonic[2] == 's')
	return 3; /* frstor/frstpm */
      break;
    case 's':
      if (mnemonic[2] == 'a')
	return 3; /* fsave */
      if (mnemonic[2] == 't')
	{
	  switch (mnemonic[3])
	    {
	    case 'c': /* fstcw */
	    case 'd': /* fstdw */
	    case 'e': /* fstenv */
	    case 's': /* fsts[gw] */
	      return 3;
	    }
	}
      break;
    case 'x':
      if (mnemonic[2] == 'r' || mnemonic[2] == 's')
	return 0; /* fxsave/fxrstor are not really math ops */
      break;
    }

  return 1;
}

/* This is the guts of the machine-dependent assembler.  LINE points to a
   machine dependent instruction.  This function is supposed to emit
   the frags/bytes it assembles to.  */

void
md_assemble (line)
     char *line;
{
  int j;
  char mnemonic[MAX_MNEM_SIZE];

  /* Initialize globals.  */
  memset (&i, '\0', sizeof (i));
  for (j = 0; j < MAX_OPERANDS; j++)
    i.reloc[j] = NO_RELOC;
  memset (disp_expressions, '\0', sizeof (disp_expressions));
  memset (im_expressions, '\0', sizeof (im_expressions));
  save_stack_p = save_stack;

  /* First parse an instruction mnemonic & call i386_operand for the operands.
     We assume that the scrubber has arranged it so that line[0] is the valid
     start of a (possibly prefixed) mnemonic.  */

  line = parse_insn (line, mnemonic);
  if (line == NULL)
    return;

  line = parse_operands (line, mnemonic);
  if (line == NULL)
    return;

  /* The order of the immediates should be reversed
     for 2 immediates extrq and insertq instructions */
  if ((i.imm_operands == 2)
      && ((strcmp (mnemonic, "extrq") == 0)
	  || (strcmp (mnemonic, "insertq") == 0)))
    {
      swap_2_operands (0, 1);
      /* "extrq" and insertq" are the only two instructions whose operands
	 have to be reversed even though they have two immediate operands.
      */
      if (intel_syntax)
	swap_operands ();
    }

  /* Now we've parsed the mnemonic into a set of templates, and have the
     operands at hand.  */

  /* All intel opcodes have reversed operands except for "bound" and
     "enter".  We also don't reverse intersegment "jmp" and "call"
     instructions with 2 immediate operands so that the immediate segment
     precedes the offset, as it does when in AT&T mode. */
  if (intel_syntax
      && i.operands > 1
      && (strcmp (mnemonic, "bound") != 0)
      && (strcmp (mnemonic, "invlpga") != 0)
      && !((i.types[0] & Imm) && (i.types[1] & Imm)))
    swap_operands ();

  if (i.imm_operands)
    optimize_imm ();

  /* Don't optimize displacement for movabs since it only takes 64bit
     displacement.  */
  if (i.disp_operands
      && (flag_code != CODE_64BIT
	  || strcmp (mnemonic, "movabs") != 0))
    optimize_disp ();

  /* Next, we find a template that matches the given insn,
     making sure the overlap of the given operands types is consistent
     with the template operand types.  */

  if (!match_template ())
    return;

  if (intel_syntax)
    {
      /* Undo SYSV386_COMPAT brokenness when in Intel mode.  See i386.h  */
      if (SYSV386_COMPAT
	  && (i.tm.base_opcode & 0xfffffde0) == 0xdce0)
	i.tm.base_opcode ^= Opcode_FloatR;

      /* Zap movzx and movsx suffix.  The suffix may have been set from
	 "word ptr" or "byte ptr" on the source operand, but we'll use
	 the suffix later to choose the destination register.  */
      if ((i.tm.base_opcode & ~9) == 0x0fb6)
	{
	  if (i.reg_operands < 2
	      && !i.suffix
	      && (~i.tm.opcode_modifier
		  & (No_bSuf
		     | No_wSuf
		     | No_lSuf
		     | No_sSuf
		     | No_xSuf
		     | No_qSuf)))
	    as_bad (_("ambiguous operand size for `%s'"), i.tm.name);

	  i.suffix = 0;
	}
    }

  if (i.tm.opcode_modifier & FWait)
    if (!add_prefix (FWAIT_OPCODE))
      return;

  /* Check string instruction segment overrides.  */
  if ((i.tm.opcode_modifier & IsString) != 0 && i.mem_operands != 0)
    {
      if (!check_string ())
	return;
    }

  if (!process_suffix ())
    return;

  /* Make still unresolved immediate matches conform to size of immediate
     given in i.suffix.  */
  if (!finalize_imm ())
    return;

  if (i.types[0] & Imm1)
    i.imm_operands = 0;	/* kludge for shift insns.  */
  if (i.types[0] & ImplicitRegister)
    i.reg_operands--;
  if (i.types[1] & ImplicitRegister)
    i.reg_operands--;
  if (i.types[2] & ImplicitRegister)
    i.reg_operands--;

  if (i.tm.opcode_modifier & ImmExt)
    {
      expressionS *exp;

      if ((i.tm.cpu_flags & (CpuSSE3|CpuSMAP)) && i.operands > 0)
	{
	  /* Streaming SIMD extensions 3 Instructions have the fixed
	     operands with an opcode suffix which is coded in the same
	     place as an 8-bit immediate field would be. Here we check
	     those operands and remove them afterwards.  */
	  unsigned int x;

	  for (x = 0; x < i.operands; x++)
	    if (i.op[x].regs->reg_num != x)
	      as_bad (_("can't use register '%s%s' as operand %d in '%s'."),
		      register_prefix,
		      i.op[x].regs->reg_name,
		      x + 1,
		      i.tm.name);
	  i.operands = 0;
 	}

      /* These AMD 3DNow! and Intel Katmai New Instructions have an
	 opcode suffix which is coded in the same place as an 8-bit
	 immediate field would be.  Here we fake an 8-bit immediate
	 operand from the opcode suffix stored in tm.extension_opcode.  */

      assert (i.imm_operands == 0 && i.operands <= 2 && 2 < MAX_OPERANDS);

      exp = &im_expressions[i.imm_operands++];
      i.op[i.operands].imms = exp;
      i.types[i.operands++] = Imm8;
      exp->X_op = O_constant;
      exp->X_add_number = i.tm.extension_opcode;
      i.tm.extension_opcode = None;
    }

  /* For insns with operands there are more diddles to do to the opcode.  */
  if (i.operands)
    {
      if (!process_operands ())
	return;
    }
  else if (!quiet_warnings && (i.tm.opcode_modifier & Ugh) != 0)
    {
      /* UnixWare fsub no args is alias for fsubp, fadd -> faddp, etc.  */
      as_warn (_("translating to `%sp'"), i.tm.name);
    }

  /* Handle conversion of 'int $3' --> special int3 insn.  */
  if (i.tm.base_opcode == INT_OPCODE && i.op[0].imms->X_add_number == 3)
    {
      i.tm.base_opcode = INT3_OPCODE;
      i.imm_operands = 0;
    }

  if ((i.tm.opcode_modifier & (Jump | JumpByte | JumpDword))
      && i.op[0].disps->X_op == O_constant)
    {
      /* Convert "jmp constant" (and "call constant") to a jump (call) to
	 the absolute address given by the constant.  Since ix86 jumps and
	 calls are pc relative, we need to generate a reloc.  */
      i.op[0].disps->X_add_symbol = &abs_symbol;
      i.op[0].disps->X_op = O_symbol;
    }

  if ((i.tm.opcode_modifier & Rex64) != 0)
    i.rex |= REX_W;

  /* For 8 bit registers we need an empty rex prefix.  Also if the
     instruction already has a prefix, we need to convert old
     registers to new ones.  */

  if (((i.types[0] & Reg8) != 0
       && (i.op[0].regs->reg_flags & RegRex64) != 0)
      || ((i.types[1] & Reg8) != 0
	  && (i.op[1].regs->reg_flags & RegRex64) != 0)
      || (((i.types[0] & Reg8) != 0 || (i.types[1] & Reg8) != 0)
	  && i.rex != 0))
    {
      int x;

      i.rex |= REX_OPCODE;
      for (x = 0; x < 2; x++)
	{
	  /* Look for 8 bit operand that uses old registers.  */
	  if ((i.types[x] & Reg8) != 0
	      && (i.op[x].regs->reg_flags & RegRex64) == 0)
	    {
	      /* In case it is "hi" register, give up.  */
	      if (i.op[x].regs->reg_num > 3)
		as_bad (_("can't encode register '%s%s' in an "
			  "instruction requiring REX prefix."),
			register_prefix, i.op[x].regs->reg_name);

	      /* Otherwise it is equivalent to the extended register.
		 Since the encoding doesn't change this is merely
		 cosmetic cleanup for debug output.  */

	      i.op[x].regs = i.op[x].regs + 8;
	    }
	}
    }

  if (i.rex != 0)
    add_prefix (REX_OPCODE | i.rex);

  /* We are ready to output the insn.  */
  output_insn ();
}

static char *
parse_insn (char *line, char *mnemonic)
{
  char *l = line;
  char *token_start = l;
  char *mnem_p;
  int supported;
  const template *t;

  /* Non-zero if we found a prefix only acceptable with string insns.  */
  const char *expecting_string_instruction = NULL;

  while (1)
    {
      mnem_p = mnemonic;
      while ((*mnem_p = mnemonic_chars[(unsigned char) *l]) != 0)
	{
	  mnem_p++;
	  if (mnem_p >= mnemonic + MAX_MNEM_SIZE)
	    {
	      as_bad (_("no such instruction: `%s'"), token_start);
	      return NULL;
	    }
	  l++;
	}
      if (!is_space_char (*l)
	  && *l != END_OF_INSN
	  && (intel_syntax
	      || (*l != PREFIX_SEPARATOR
		  && *l != ',')))
	{
	  as_bad (_("invalid character %s in mnemonic"),
		  output_invalid (*l));
	  return NULL;
	}
      if (token_start == l)
	{
	  if (!intel_syntax && *l == PREFIX_SEPARATOR)
	    as_bad (_("expecting prefix; got nothing"));
	  else
	    as_bad (_("expecting mnemonic; got nothing"));
	  return NULL;
	}

      /* Look up instruction (or prefix) via hash table.  */
      current_templates = hash_find (op_hash, mnemonic);

      if (*l != END_OF_INSN
	  && (!is_space_char (*l) || l[1] != END_OF_INSN)
	  && current_templates
	  && (current_templates->start->opcode_modifier & IsPrefix))
	{
	  if (current_templates->start->cpu_flags
	      & (flag_code != CODE_64BIT ? Cpu64 : CpuNo64))
	    {
	      as_bad ((flag_code != CODE_64BIT
		       ? _("`%s' is only supported in 64-bit mode")
		       : _("`%s' is not supported in 64-bit mode")),
		      current_templates->start->name);
	      return NULL;
	    }
	  /* If we are in 16-bit mode, do not allow addr16 or data16.
	     Similarly, in 32-bit mode, do not allow addr32 or data32.  */
	  if ((current_templates->start->opcode_modifier & (Size16 | Size32))
	      && flag_code != CODE_64BIT
	      && (((current_templates->start->opcode_modifier & Size32) != 0)
		  ^ (flag_code == CODE_16BIT)))
	    {
	      as_bad (_("redundant %s prefix"),
		      current_templates->start->name);
	      return NULL;
	    }
	  /* Add prefix, checking for repeated prefixes.  */
	  switch (add_prefix (current_templates->start->base_opcode))
	    {
	    case 0:
	      return NULL;
	    case 2:
	      expecting_string_instruction = current_templates->start->name;
	      break;
	    }
	  /* Skip past PREFIX_SEPARATOR and reset token_start.  */
	  token_start = ++l;
	}
      else
	break;
    }

  if (!current_templates)
    {
      /* See if we can get a match by trimming off a suffix.  */
      switch (mnem_p[-1])
	{
	case WORD_MNEM_SUFFIX:
	  if (intel_syntax && (intel_float_operand (mnemonic) & 2))
	    i.suffix = SHORT_MNEM_SUFFIX;
	  else
	case BYTE_MNEM_SUFFIX:
	case QWORD_MNEM_SUFFIX:
	  i.suffix = mnem_p[-1];
	  mnem_p[-1] = '\0';
	  current_templates = hash_find (op_hash, mnemonic);
	  break;
	case SHORT_MNEM_SUFFIX:
	case LONG_MNEM_SUFFIX:
	  if (!intel_syntax)
	    {
	      i.suffix = mnem_p[-1];
	      mnem_p[-1] = '\0';
	      current_templates = hash_find (op_hash, mnemonic);
	    }
	  break;

	  /* Intel Syntax.  */
	case 'd':
	  if (intel_syntax)
	    {
	      if (intel_float_operand (mnemonic) == 1)
		i.suffix = SHORT_MNEM_SUFFIX;
	      else
		i.suffix = LONG_MNEM_SUFFIX;
	      mnem_p[-1] = '\0';
	      current_templates = hash_find (op_hash, mnemonic);
	    }
	  break;
	}
      if (!current_templates)
	{
	  as_bad (_("no such instruction: `%s'"), token_start);
	  return NULL;
	}
    }

  if (current_templates->start->opcode_modifier & (Jump | JumpByte))
    {
      /* Check for a branch hint.  We allow ",pt" and ",pn" for
	 predict taken and predict not taken respectively.
	 I'm not sure that branch hints actually do anything on loop
	 and jcxz insns (JumpByte) for current Pentium4 chips.  They
	 may work in the future and it doesn't hurt to accept them
	 now.  */
      if (l[0] == ',' && l[1] == 'p')
	{
	  if (l[2] == 't')
	    {
	      if (!add_prefix (DS_PREFIX_OPCODE))
		return NULL;
	      l += 3;
	    }
	  else if (l[2] == 'n')
	    {
	      if (!add_prefix (CS_PREFIX_OPCODE))
		return NULL;
	      l += 3;
	    }
	}
    }
  /* Any other comma loses.  */
  if (*l == ',')
    {
      as_bad (_("invalid character %s in mnemonic"),
	      output_invalid (*l));
      return NULL;
    }

  /* Check if instruction is supported on specified architecture.  */
  supported = 0;
  for (t = current_templates->start; t < current_templates->end; ++t)
    {
      if (!((t->cpu_flags & ~(Cpu64 | CpuNo64))
	    & ~(cpu_arch_flags & ~(Cpu64 | CpuNo64))))
	supported |= 1;
      if (!(t->cpu_flags & (flag_code == CODE_64BIT ? CpuNo64 : Cpu64)))
	supported |= 2;
    }
  if (!(supported & 2))
    {
      as_bad (flag_code == CODE_64BIT
	      ? _("`%s' is not supported in 64-bit mode")
	      : _("`%s' is only supported in 64-bit mode"),
	      current_templates->start->name);
      return NULL;
    }
  if (!(supported & 1))
    {
      as_warn (_("`%s' is not supported on `%s%s'"),
	       current_templates->start->name,
	       cpu_arch_name,
	       cpu_sub_arch_name ? cpu_sub_arch_name : "");
    }
  else if ((Cpu386 & ~cpu_arch_flags) && (flag_code != CODE_16BIT))
    {
      as_warn (_("use .code16 to ensure correct addressing mode"));
    }

  /* Check for rep/repne without a string instruction.  */
  if (expecting_string_instruction)
    {
      static templates override;

      for (t = current_templates->start; t < current_templates->end; ++t)
	if (t->opcode_modifier & IsString)
	  break;
      if (t >= current_templates->end)
	{
	  as_bad (_("expecting string instruction after `%s'"),
		  expecting_string_instruction);
	  return NULL;
	}
      for (override.start = t; t < current_templates->end; ++t)
	if (!(t->opcode_modifier & IsString))
	  break;
      override.end = t;
      current_templates = &override;
    }

  return l;
}

static char *
parse_operands (char *l, const char *mnemonic)
{
  char *token_start;

  /* 1 if operand is pending after ','.  */
  unsigned int expecting_operand = 0;

  /* Non-zero if operand parens not balanced.  */
  unsigned int paren_not_balanced;

  while (*l != END_OF_INSN)
    {
      /* Skip optional white space before operand.  */
      if (is_space_char (*l))
	++l;
      if (!is_operand_char (*l) && *l != END_OF_INSN)
	{
	  as_bad (_("invalid character %s before operand %d"),
		  output_invalid (*l),
		  i.operands + 1);
	  return NULL;
	}
      token_start = l;	/* after white space */
      paren_not_balanced = 0;
      while (paren_not_balanced || *l != ',')
	{
	  if (*l == END_OF_INSN)
	    {
	      if (paren_not_balanced)
		{
		  if (!intel_syntax)
		    as_bad (_("unbalanced parenthesis in operand %d."),
			    i.operands + 1);
		  else
		    as_bad (_("unbalanced brackets in operand %d."),
			    i.operands + 1);
		  return NULL;
		}
	      else
		break;	/* we are done */
	    }
	  else if (!is_operand_char (*l) && !is_space_char (*l))
	    {
	      as_bad (_("invalid character %s in operand %d"),
		      output_invalid (*l),
		      i.operands + 1);
	      return NULL;
	    }
	  if (!intel_syntax)
	    {
	      if (*l == '(')
		++paren_not_balanced;
	      if (*l == ')')
		--paren_not_balanced;
	    }
	  else
	    {
	      if (*l == '[')
		++paren_not_balanced;
	      if (*l == ']')
		--paren_not_balanced;
	    }
	  l++;
	}
      if (l != token_start)
	{			/* Yes, we've read in another operand.  */
	  unsigned int operand_ok;
	  this_operand = i.operands++;
	  if (i.operands > MAX_OPERANDS)
	    {
	      as_bad (_("spurious operands; (%d operands/instruction max)"),
		      MAX_OPERANDS);
	      return NULL;
	    }
	  /* Now parse operand adding info to 'i' as we go along.  */
	  END_STRING_AND_SAVE (l);

	  if (intel_syntax)
	    operand_ok =
	      i386_intel_operand (token_start,
				  intel_float_operand (mnemonic));
	  else
	    operand_ok = i386_operand (token_start);

	  RESTORE_END_STRING (l);
	  if (!operand_ok)
	    return NULL;
	}
      else
	{
	  if (expecting_operand)
	    {
	    expecting_operand_after_comma:
	      as_bad (_("expecting operand after ','; got nothing"));
	      return NULL;
	    }
	  if (*l == ',')
	    {
	      as_bad (_("expecting operand before ','; got nothing"));
	      return NULL;
	    }
	}

      /* Now *l must be either ',' or END_OF_INSN.  */
      if (*l == ',')
	{
	  if (*++l == END_OF_INSN)
	    {
	      /* Just skip it, if it's \n complain.  */
	      goto expecting_operand_after_comma;
	    }
	  expecting_operand = 1;
	}
    }
  return l;
}

static void
swap_2_operands (int xchg1, int xchg2)
{
  union i386_op temp_op;
  unsigned int temp_type;
  enum bfd_reloc_code_real temp_reloc;

  temp_type = i.types[xchg2];
  i.types[xchg2] = i.types[xchg1];
  i.types[xchg1] = temp_type;
  temp_op = i.op[xchg2];
  i.op[xchg2] = i.op[xchg1];
  i.op[xchg1] = temp_op;
  temp_reloc = i.reloc[xchg2];
  i.reloc[xchg2] = i.reloc[xchg1];
  i.reloc[xchg1] = temp_reloc;
}

static void
swap_operands (void)
{
  switch (i.operands)
    {
    case 4:
      swap_2_operands (1, i.operands - 2);
    case 3:
    case 2:
      swap_2_operands (0, i.operands - 1);
      break;
    default:
      abort ();
    }

  if (i.mem_operands == 2)
    {
      const seg_entry *temp_seg;
      temp_seg = i.seg[0];
      i.seg[0] = i.seg[1];
      i.seg[1] = temp_seg;
    }
}

/* Try to ensure constant immediates are represented in the smallest
   opcode possible.  */
static void
optimize_imm (void)
{
  char guess_suffix = 0;
  int op;

  if (i.suffix)
    guess_suffix = i.suffix;
  else if (i.reg_operands)
    {
      /* Figure out a suffix from the last register operand specified.
	 We can't do this properly yet, ie. excluding InOutPortReg,
	 but the following works for instructions with immediates.
	 In any case, we can't set i.suffix yet.  */
      for (op = i.operands; --op >= 0;)
	if (i.types[op] & Reg)
	  {
	    if (i.types[op] & Reg8)
	      guess_suffix = BYTE_MNEM_SUFFIX;
	    else if (i.types[op] & Reg16)
	      guess_suffix = WORD_MNEM_SUFFIX;
	    else if (i.types[op] & Reg32)
	      guess_suffix = LONG_MNEM_SUFFIX;
	    else if (i.types[op] & Reg64)
	      guess_suffix = QWORD_MNEM_SUFFIX;
	    break;
	  }
    }
  else if ((flag_code == CODE_16BIT) ^ (i.prefix[DATA_PREFIX] != 0))
    guess_suffix = WORD_MNEM_SUFFIX;

  for (op = i.operands; --op >= 0;)
    if (i.types[op] & Imm)
      {
	switch (i.op[op].imms->X_op)
	  {
	  case O_constant:
	    /* If a suffix is given, this operand may be shortened.  */
	    switch (guess_suffix)
	      {
	      case LONG_MNEM_SUFFIX:
		i.types[op] |= Imm32 | Imm64;
		break;
	      case WORD_MNEM_SUFFIX:
		i.types[op] |= Imm16 | Imm32S | Imm32 | Imm64;
		break;
	      case BYTE_MNEM_SUFFIX:
		i.types[op] |= Imm16 | Imm8 | Imm8S | Imm32S | Imm32 | Imm64;
		break;
	      }

	    /* If this operand is at most 16 bits, convert it
	       to a signed 16 bit number before trying to see
	       whether it will fit in an even smaller size.
	       This allows a 16-bit operand such as $0xffe0 to
	       be recognised as within Imm8S range.  */
	    if ((i.types[op] & Imm16)
		&& (i.op[op].imms->X_add_number & ~(offsetT) 0xffff) == 0)
	      {
		i.op[op].imms->X_add_number =
		  (((i.op[op].imms->X_add_number & 0xffff) ^ 0x8000) - 0x8000);
	      }
	    if ((i.types[op] & Imm32)
		&& ((i.op[op].imms->X_add_number & ~(((offsetT) 2 << 31) - 1))
		    == 0))
	      {
		i.op[op].imms->X_add_number = ((i.op[op].imms->X_add_number
						^ ((offsetT) 1 << 31))
					       - ((offsetT) 1 << 31));
	      }
	    i.types[op] |= smallest_imm_type (i.op[op].imms->X_add_number);

	    /* We must avoid matching of Imm32 templates when 64bit
	       only immediate is available.  */
	    if (guess_suffix == QWORD_MNEM_SUFFIX)
	      i.types[op] &= ~Imm32;
	    break;

	  case O_absent:
	  case O_register:
	    abort ();

	    /* Symbols and expressions.  */
	  default:
	    /* Convert symbolic operand to proper sizes for matching, but don't
	       prevent matching a set of insns that only supports sizes other
	       than those matching the insn suffix.  */
	    {
	      unsigned int mask, allowed = 0;
	      const template *t;

	      for (t = current_templates->start;
		   t < current_templates->end;
		   ++t)
		allowed |= t->operand_types[op];
	      switch (guess_suffix)
		{
		case QWORD_MNEM_SUFFIX:
		  mask = Imm64 | Imm32S;
		  break;
		case LONG_MNEM_SUFFIX:
		  mask = Imm32;
		  break;
		case WORD_MNEM_SUFFIX:
		  mask = Imm16;
		  break;
		case BYTE_MNEM_SUFFIX:
		  mask = Imm8;
		  break;
		default:
		  mask = 0;
		  break;
		}
	      if (mask & allowed)
		i.types[op] &= mask;
	    }
	    break;
	  }
      }
}

/* Try to use the smallest displacement type too.  */
static void
optimize_disp (void)
{
  int op;

  for (op = i.operands; --op >= 0;)
    if (i.types[op] & Disp)
      {
	if (i.op[op].disps->X_op == O_constant)
	  {
	    offsetT disp = i.op[op].disps->X_add_number;

	    if ((i.types[op] & Disp16)
		&& (disp & ~(offsetT) 0xffff) == 0)
	      {
		/* If this operand is at most 16 bits, convert
		   to a signed 16 bit number and don't use 64bit
		   displacement.  */
		disp = (((disp & 0xffff) ^ 0x8000) - 0x8000);
		i.types[op] &= ~Disp64;
	      }
	    if ((i.types[op] & Disp32)
		&& (disp & ~(((offsetT) 2 << 31) - 1)) == 0)
	      {
		/* If this operand is at most 32 bits, convert
		   to a signed 32 bit number and don't use 64bit
		   displacement.  */
		disp &= (((offsetT) 2 << 31) - 1);
		disp = (disp ^ ((offsetT) 1 << 31)) - ((addressT) 1 << 31);
		i.types[op] &= ~Disp64;
	      }
	    if (!disp && (i.types[op] & BaseIndex))
	      {
		i.types[op] &= ~Disp;
		i.op[op].disps = 0;
		i.disp_operands--;
	      }
	    else if (flag_code == CODE_64BIT)
	      {
		if (fits_in_signed_long (disp))
		  {
		    i.types[op] &= ~Disp64;
		    i.types[op] |= Disp32S;
		  }
		if (fits_in_unsigned_long (disp))
		  i.types[op] |= Disp32;
	      }
	    if ((i.types[op] & (Disp32 | Disp32S | Disp16))
		&& fits_in_signed_byte (disp))
	      i.types[op] |= Disp8;
	  }
	else if (i.reloc[op] == BFD_RELOC_386_TLS_DESC_CALL
		 || i.reloc[op] == BFD_RELOC_X86_64_TLSDESC_CALL)
	  {
	    fix_new_exp (frag_now, frag_more (0) - frag_now->fr_literal, 0,
			 i.op[op].disps, 0, i.reloc[op]);
	    i.types[op] &= ~Disp;
	  }
 	else
	  /* We only support 64bit displacement on constants.  */
	  i.types[op] &= ~Disp64;
      }
}

static int
match_template (void)
{
  /* Points to template once we've found it.  */
  const template *t;
  unsigned int overlap0, overlap1, overlap2, overlap3;
  unsigned int found_reverse_match;
  int suffix_check;
  unsigned int operand_types [MAX_OPERANDS];
  int addr_prefix_disp;
  unsigned int j;

#if MAX_OPERANDS != 4
# error "MAX_OPERANDS must be 4."
#endif

#define MATCH(overlap, given, template)				\
  ((overlap & ~JumpAbsolute)					\
   && (((given) & (BaseIndex | JumpAbsolute))			\
       == ((overlap) & (BaseIndex | JumpAbsolute))))

  /* If given types r0 and r1 are registers they must be of the same type
     unless the expected operand type register overlap is null.
     Note that Acc in a template matches every size of reg.  */
#define CONSISTENT_REGISTER_MATCH(m0, g0, t0, m1, g1, t1)	\
  (((g0) & Reg) == 0 || ((g1) & Reg) == 0			\
   || ((g0) & Reg) == ((g1) & Reg)				\
   || ((((m0) & Acc) ? Reg : (t0)) & (((m1) & Acc) ? Reg : (t1)) & Reg) == 0 )

  overlap0 = 0;
  overlap1 = 0;
  overlap2 = 0;
  overlap3 = 0;
  found_reverse_match = 0;
  for (j = 0; j < MAX_OPERANDS; j++)
    operand_types [j] = 0;
  addr_prefix_disp = -1;
  suffix_check = (i.suffix == BYTE_MNEM_SUFFIX
		  ? No_bSuf
		  : (i.suffix == WORD_MNEM_SUFFIX
		     ? No_wSuf
		     : (i.suffix == SHORT_MNEM_SUFFIX
			? No_sSuf
			: (i.suffix == LONG_MNEM_SUFFIX
			   ? No_lSuf
			   : (i.suffix == QWORD_MNEM_SUFFIX
			      ? No_qSuf
			      : (i.suffix == LONG_DOUBLE_MNEM_SUFFIX
				 ? No_xSuf : 0))))));

  for (t = current_templates->start; t < current_templates->end; t++)
    {
      addr_prefix_disp = -1;

      /* Must have right number of operands.  */
      if (i.operands != t->operands)
	continue;

      /* Check the suffix, except for some instructions in intel mode.  */
      if ((t->opcode_modifier & suffix_check)
	  && !(intel_syntax
	       && (t->opcode_modifier & IgnoreSize)))
	continue;

      for (j = 0; j < MAX_OPERANDS; j++)
	operand_types [j] = t->operand_types [j];

      /* In general, don't allow 64-bit operands in 32-bit mode.  */
      if (i.suffix == QWORD_MNEM_SUFFIX
	  && flag_code != CODE_64BIT
	  && (intel_syntax
	      ? (!(t->opcode_modifier & IgnoreSize)
		 && !intel_float_operand (t->name))
	      : intel_float_operand (t->name) != 2)
	  && (!(operand_types[0] & (RegMMX | RegXMM))
	      || !(operand_types[t->operands > 1] & (RegMMX | RegXMM)))
	  && (t->base_opcode != 0x0fc7
	      || t->extension_opcode != 1 /* cmpxchg8b */))
	continue;

      /* Do not verify operands when there are none.  */
      else if (!t->operands)
	{
	  if (t->cpu_flags & ~cpu_arch_flags)
	    continue;
	  /* We've found a match; break out of loop.  */
	  break;
	}

      /* Address size prefix will turn Disp64/Disp32/Disp16 operand
	 into Disp32/Disp16/Disp32 operand.  */
      if (i.prefix[ADDR_PREFIX] != 0)
	  {
	    unsigned int DispOn = 0, DispOff = 0;

	    switch (flag_code)
	    {
	    case CODE_16BIT:
	      DispOn = Disp32;
	      DispOff = Disp16;
	      break;
	    case CODE_32BIT:
	      DispOn = Disp16;
	      DispOff = Disp32;
	      break;
	    case CODE_64BIT:
	      DispOn = Disp32;
	      DispOff = Disp64;
	      break;
	    }

	    for (j = 0; j < MAX_OPERANDS; j++)
	      {
		/* There should be only one Disp operand.  */
		if ((operand_types[j] & DispOff))
		  {
		    addr_prefix_disp = j;
		    operand_types[j] |= DispOn;
		    operand_types[j] &= ~DispOff;
		    break;
		  }
	      }
	  }

      overlap0 = i.types[0] & operand_types[0];
      switch (t->operands)
	{
	case 1:
	  if (!MATCH (overlap0, i.types[0], operand_types[0]))
	    continue;
	  break;
	case 2:
	  /* xchg %eax, %eax is a special case. It is an aliase for nop
	     only in 32bit mode and we can use opcode 0x90.  In 64bit
	     mode, we can't use 0x90 for xchg %eax, %eax since it should
	     zero-extend %eax to %rax.  */
	  if (flag_code == CODE_64BIT
	      && t->base_opcode == 0x90
	      && i.types [0] == (Acc | Reg32)
	      && i.types [1] == (Acc | Reg32))
	    continue;
	case 3:
	case 4:
	  overlap1 = i.types[1] & operand_types[1];
	  if (!MATCH (overlap0, i.types[0], operand_types[0])
	      || !MATCH (overlap1, i.types[1], operand_types[1])
	      /* monitor in SSE3 is a very special case.  The first
		 register and the second register may have different
		 sizes.  The same applies to crc32 in SSE4.2.  */
	      || !((t->base_opcode == 0x0f01
		    && t->extension_opcode == 0xc8)
		   || t->base_opcode == 0xf20f38f1
		   || CONSISTENT_REGISTER_MATCH (overlap0, i.types[0],
						 operand_types[0],
						 overlap1, i.types[1],
						 operand_types[1])))
	    {
	      /* Check if other direction is valid ...  */
	      if ((t->opcode_modifier & (D | FloatD)) == 0)
		continue;

	      /* Try reversing direction of operands.  */
	      overlap0 = i.types[0] & operand_types[1];
	      overlap1 = i.types[1] & operand_types[0];
	      if (!MATCH (overlap0, i.types[0], operand_types[1])
		  || !MATCH (overlap1, i.types[1], operand_types[0])
		  || !CONSISTENT_REGISTER_MATCH (overlap0, i.types[0],
						 operand_types[1],
						 overlap1, i.types[1],
						 operand_types[0]))
		{
		  /* Does not match either direction.  */
		  continue;
		}
	      /* found_reverse_match holds which of D or FloatDR
		 we've found.  */
	      if ((t->opcode_modifier & D))
		found_reverse_match = Opcode_D;
	      else if ((t->opcode_modifier & FloatD))
		found_reverse_match = Opcode_FloatD;
	      else
		found_reverse_match = 0;
	      if ((t->opcode_modifier & FloatR))
		found_reverse_match |= Opcode_FloatR;
	    }
	  else
	    {
	      /* Found a forward 2 operand match here.  */
	      switch (t->operands)
		{
		case 4:
		  overlap3 = i.types[3] & operand_types[3];
		case 3:
		  overlap2 = i.types[2] & operand_types[2];
		  break;
		}

	      switch (t->operands)
		{
		case 4:
		  if (!MATCH (overlap3, i.types[3], operand_types[3])
		      || !CONSISTENT_REGISTER_MATCH (overlap2,
						     i.types[2],
						     operand_types[2],
						     overlap3,
						     i.types[3],
						     operand_types[3]))
		    continue;
		case 3:
		  /* Here we make use of the fact that there are no
		     reverse match 3 operand instructions, and all 3
		     operand instructions only need to be checked for
		     register consistency between operands 2 and 3.  */
		  if (!MATCH (overlap2, i.types[2], operand_types[2])
		      || !CONSISTENT_REGISTER_MATCH (overlap1,
						     i.types[1],
						     operand_types[1],
						     overlap2,
						     i.types[2],
						     operand_types[2]))
		    continue;
		  break;
		}
	    }
	  /* Found either forward/reverse 2, 3 or 4 operand match here:
	     slip through to break.  */
	}
      if (t->cpu_flags & ~cpu_arch_flags)
	{
	  found_reverse_match = 0;
	  continue;
	}
      /* We've found a match; break out of loop.  */
      break;
    }

  if (t == current_templates->end)
    {
      /* We found no match.  */
      as_bad (_("suffix or operands invalid for `%s'"),
	      current_templates->start->name);
      return 0;
    }

  if (!quiet_warnings)
    {
      if (!intel_syntax
	  && ((i.types[0] & JumpAbsolute)
	      != (operand_types[0] & JumpAbsolute)))
	{
	  as_warn (_("indirect %s without `*'"), t->name);
	}

      if ((t->opcode_modifier & (IsPrefix | IgnoreSize))
	  == (IsPrefix | IgnoreSize))
	{
	  /* Warn them that a data or address size prefix doesn't
	     affect assembly of the next line of code.  */
	  as_warn (_("stand-alone `%s' prefix"), t->name);
	}
    }

  /* Copy the template we found.  */
  i.tm = *t;

  if (addr_prefix_disp != -1)
    i.tm.operand_types[addr_prefix_disp]
      = operand_types[addr_prefix_disp];

  if (found_reverse_match)
    {
      /* If we found a reverse match we must alter the opcode
	 direction bit.  found_reverse_match holds bits to change
	 (different for int & float insns).  */

      i.tm.base_opcode ^= found_reverse_match;

      i.tm.operand_types[0] = operand_types[1];
      i.tm.operand_types[1] = operand_types[0];
    }

  return 1;
}

static int
check_string (void)
{
  int mem_op = (i.types[0] & AnyMem) ? 0 : 1;
  if ((i.tm.operand_types[mem_op] & EsSeg) != 0)
    {
      if (i.seg[0] != NULL && i.seg[0] != &es)
	{
	  as_bad (_("`%s' operand %d must use `%%es' segment"),
		  i.tm.name,
		  mem_op + 1);
	  return 0;
	}
      /* There's only ever one segment override allowed per instruction.
	 This instruction possibly has a legal segment override on the
	 second operand, so copy the segment to where non-string
	 instructions store it, allowing common code.  */
      i.seg[0] = i.seg[1];
    }
  else if ((i.tm.operand_types[mem_op + 1] & EsSeg) != 0)
    {
      if (i.seg[1] != NULL && i.seg[1] != &es)
	{
	  as_bad (_("`%s' operand %d must use `%%es' segment"),
		  i.tm.name,
		  mem_op + 2);
	  return 0;
	}
    }
  return 1;
}

static int
process_suffix (void)
{
  /* If matched instruction specifies an explicit instruction mnemonic
     suffix, use it.  */
  if (i.tm.opcode_modifier & (Size16 | Size32 | Size64))
    {
      if (i.tm.opcode_modifier & Size16)
	i.suffix = WORD_MNEM_SUFFIX;
      else if (i.tm.opcode_modifier & Size64)
	i.suffix = QWORD_MNEM_SUFFIX;
      else
	i.suffix = LONG_MNEM_SUFFIX;
    }
  else if (i.reg_operands)
    {
      /* If there's no instruction mnemonic suffix we try to invent one
	 based on register operands.  */
      if (!i.suffix)
	{
	  /* We take i.suffix from the last register operand specified,
	     Destination register type is more significant than source
	     register type.  crc32 in SSE4.2 prefers source register
	     type. */
	  if (i.tm.base_opcode == 0xf20f38f1)
	    {
	      if ((i.types[0] & Reg))
		i.suffix = ((i.types[0] & Reg16) ? WORD_MNEM_SUFFIX :
			    LONG_MNEM_SUFFIX);
	    }
	  else if (i.tm.base_opcode == 0xf20f38f0)
	    {
	      if ((i.types[0] & Reg8))
		i.suffix = BYTE_MNEM_SUFFIX;
	    }

	  if (!i.suffix)
	    {
	      int op;

	      if (i.tm.base_opcode == 0xf20f38f1
		  || i.tm.base_opcode == 0xf20f38f0)
		{
		  /* We have to know the operand size for crc32.  */
		  as_bad (_("ambiguous memory operand size for `%s`"),
			  i.tm.name);
		  return 0;
		}

	      for (op = i.operands; --op >= 0;)
		if ((i.types[op] & Reg)
		    && !(i.tm.operand_types[op] & InOutPortReg))
		  {
		    i.suffix = ((i.types[op] & Reg8) ? BYTE_MNEM_SUFFIX :
				(i.types[op] & Reg16) ? WORD_MNEM_SUFFIX :
				(i.types[op] & Reg64) ? QWORD_MNEM_SUFFIX :
				LONG_MNEM_SUFFIX);
		    break;
		  }
	    }
	}
      else if (i.suffix == BYTE_MNEM_SUFFIX)
	{
	  if (!check_byte_reg ())
	    return 0;
	}
      else if (i.suffix == LONG_MNEM_SUFFIX)
	{
	  if (!check_long_reg ())
	    return 0;
	}
      else if (i.suffix == QWORD_MNEM_SUFFIX)
	{
	  if (!check_qword_reg ())
	    return 0;
	}
      else if (i.suffix == WORD_MNEM_SUFFIX)
	{
	  if (!check_word_reg ())
	    return 0;
	}
      else if (intel_syntax && (i.tm.opcode_modifier & IgnoreSize))
	/* Do nothing if the instruction is going to ignore the prefix.  */
	;
      else
	abort ();
    }
  else if ((i.tm.opcode_modifier & DefaultSize)
	   && !i.suffix
	   /* exclude fldenv/frstor/fsave/fstenv */
	   && (i.tm.opcode_modifier & No_sSuf))
    {
      i.suffix = stackop_size;
    }
  else if (intel_syntax
	   && !i.suffix
	   && ((i.tm.operand_types[0] & JumpAbsolute)
	       || (i.tm.opcode_modifier & (JumpByte|JumpInterSegment))
	       || (i.tm.base_opcode == 0x0f01 /* [ls][gi]dt */
		   && i.tm.extension_opcode <= 3)))
    {
      switch (flag_code)
	{
	case CODE_64BIT:
	  if (!(i.tm.opcode_modifier & No_qSuf))
	    {
	      i.suffix = QWORD_MNEM_SUFFIX;
	      break;
	    }
	case CODE_32BIT:
	  if (!(i.tm.opcode_modifier & No_lSuf))
	    i.suffix = LONG_MNEM_SUFFIX;
	  break;
	case CODE_16BIT:
	  if (!(i.tm.opcode_modifier & No_wSuf))
	    i.suffix = WORD_MNEM_SUFFIX;
	  break;
	}
    }

  if (!i.suffix)
    {
      if (!intel_syntax)
	{
	  if (i.tm.opcode_modifier & W)
	    {
	      as_bad (_("no instruction mnemonic suffix given and "
			"no register operands; can't size instruction"));
	      return 0;
	    }
	}
      else
	{
	  unsigned int suffixes = (~i.tm.opcode_modifier
				   & (No_bSuf
				      | No_wSuf
				      | No_lSuf
				      | No_sSuf
				      | No_xSuf
				      | No_qSuf));

	  if ((i.tm.opcode_modifier & W)
	      || ((suffixes & (suffixes - 1))
		  && !(i.tm.opcode_modifier & (DefaultSize | IgnoreSize))))
	    {
	      as_bad (_("ambiguous operand size for `%s'"), i.tm.name);
	      return 0;
	    }
	}
    }

  /* Change the opcode based on the operand size given by i.suffix;
     We don't need to change things for byte insns.  */

  if (i.suffix && i.suffix != BYTE_MNEM_SUFFIX)
    {
      /* It's not a byte, select word/dword operation.  */
      if (i.tm.opcode_modifier & W)
	{
	  if (i.tm.opcode_modifier & ShortForm)
	    i.tm.base_opcode |= 8;
	  else
	    i.tm.base_opcode |= 1;
	}

      /* Now select between word & dword operations via the operand
	 size prefix, except for instructions that will ignore this
	 prefix anyway.  */
      if (i.tm.base_opcode == 0x0f01 && i.tm.extension_opcode == 0xc8)
	{
	  /* monitor in SSE3 is a very special case. The default size
	     of AX is the size of mode. The address size override
	     prefix will change the size of AX.  */
	  if (i.op->regs[0].reg_type &
	      (flag_code == CODE_32BIT ? Reg16 : Reg32))
	    if (!add_prefix (ADDR_PREFIX_OPCODE))
	      return 0;
	}
      else if (i.suffix != QWORD_MNEM_SUFFIX
	       && i.suffix != LONG_DOUBLE_MNEM_SUFFIX
	       && !(i.tm.opcode_modifier & (IgnoreSize | FloatMF))
	       && ((i.suffix == LONG_MNEM_SUFFIX) == (flag_code == CODE_16BIT)
		   || (flag_code == CODE_64BIT
		       && (i.tm.opcode_modifier & JumpByte))))
	{
	  unsigned int prefix = DATA_PREFIX_OPCODE;

	  if (i.tm.opcode_modifier & JumpByte) /* jcxz, loop */
	    prefix = ADDR_PREFIX_OPCODE;

	  if (!add_prefix (prefix))
	    return 0;
	}

      /* Set mode64 for an operand.  */
      if (i.suffix == QWORD_MNEM_SUFFIX
	  && flag_code == CODE_64BIT
	  && (i.tm.opcode_modifier & NoRex64) == 0)
	{
	  /* Special case for xchg %rax,%rax.  It is NOP and doesn't
	     need rex64.  */
	  if (i.operands != 2
	      || i.types [0] != (Acc | Reg64)
	      || i.types [1] != (Acc | Reg64)
	      || i.tm.base_opcode != 0x90)
	    i.rex |= REX_W;
	}

      /* Size floating point instruction.  */
      if (i.suffix == LONG_MNEM_SUFFIX)
	if (i.tm.opcode_modifier & FloatMF)
	  i.tm.base_opcode ^= 4;
    }

  return 1;
}

static int
check_byte_reg (void)
{
  int op;

  for (op = i.operands; --op >= 0;)
    {
      /* If this is an eight bit register, it's OK.  If it's the 16 or
	 32 bit version of an eight bit register, we will just use the
	 low portion, and that's OK too.  */
      if (i.types[op] & Reg8)
	continue;

      /* movzx and movsx should not generate this warning.  */
      if (intel_syntax
	  && (i.tm.base_opcode == 0xfb7
	      || i.tm.base_opcode == 0xfb6
	      || i.tm.base_opcode == 0x63
	      || i.tm.base_opcode == 0xfbe
	      || i.tm.base_opcode == 0xfbf))
	continue;

      /* crc32 doesn't generate this warning.  */
      if (i.tm.base_opcode == 0xf20f38f0)
	continue;

      if ((i.types[op] & WordReg) && i.op[op].regs->reg_num < 4)
	{
	  /* Prohibit these changes in the 64bit mode, since the
	     lowering is more complicated.  */
	  if (flag_code == CODE_64BIT
	      && (i.tm.operand_types[op] & InOutPortReg) == 0)
	    {
	      as_bad (_("Incorrect register `%s%s' used with `%c' suffix"),
		      register_prefix, i.op[op].regs->reg_name,
		      i.suffix);
	      return 0;
	    }
#if REGISTER_WARNINGS
	  if (!quiet_warnings
	      && (i.tm.operand_types[op] & InOutPortReg) == 0)
	    as_warn (_("using `%s%s' instead of `%s%s' due to `%c' suffix"),
		     register_prefix,
		     (i.op[op].regs + (i.types[op] & Reg16
				       ? REGNAM_AL - REGNAM_AX
				       : REGNAM_AL - REGNAM_EAX))->reg_name,
		     register_prefix,
		     i.op[op].regs->reg_name,
		     i.suffix);
#endif
	  continue;
	}
      /* Any other register is bad.  */
      if (i.types[op] & (Reg | RegMMX | RegXMM
			 | SReg2 | SReg3
			 | Control | Debug | Test
			 | FloatReg | FloatAcc))
	{
	  as_bad (_("`%s%s' not allowed with `%s%c'"),
		  register_prefix,
		  i.op[op].regs->reg_name,
		  i.tm.name,
		  i.suffix);
	  return 0;
	}
    }
  return 1;
}

static int
check_long_reg (void)
{
  int op;

  for (op = i.operands; --op >= 0;)
    /* Reject eight bit registers, except where the template requires
       them. (eg. movzb)  */
    if ((i.types[op] & Reg8) != 0
	&& (i.tm.operand_types[op] & (Reg16 | Reg32 | Acc)) != 0)
      {
	as_bad (_("`%s%s' not allowed with `%s%c'"),
		register_prefix,
		i.op[op].regs->reg_name,
		i.tm.name,
		i.suffix);
	return 0;
      }
  /* Warn if the e prefix on a general reg is missing.  */
    else if ((!quiet_warnings || flag_code == CODE_64BIT)
	     && (i.types[op] & Reg16) != 0
	     && (i.tm.operand_types[op] & (Reg32 | Acc)) != 0)
      {
	/* Prohibit these changes in the 64bit mode, since the
	   lowering is more complicated.  */
	if (flag_code == CODE_64BIT)
	  {
	    as_bad (_("Incorrect register `%s%s' used with `%c' suffix"),
		    register_prefix, i.op[op].regs->reg_name,
		    i.suffix);
	    return 0;
	  }
#if REGISTER_WARNINGS
	else
	  as_warn (_("using `%s%s' instead of `%s%s' due to `%c' suffix"),
		   register_prefix,
		   (i.op[op].regs + REGNAM_EAX - REGNAM_AX)->reg_name,
		   register_prefix,
		   i.op[op].regs->reg_name,
		   i.suffix);
#endif
      }
  /* Warn if the r prefix on a general reg is missing.  */
    else if ((i.types[op] & Reg64) != 0
	     && (i.tm.operand_types[op] & (Reg32 | Acc)) != 0)
      {
	as_bad (_("Incorrect register `%s%s' used with `%c' suffix"),
		register_prefix, i.op[op].regs->reg_name,
		i.suffix);
	return 0;
      }
  return 1;
}

static int
check_qword_reg (void)
{
  int op;

  for (op = i.operands; --op >= 0; )
    /* Reject eight bit registers, except where the template requires
       them. (eg. movzb)  */
    if ((i.types[op] & Reg8) != 0
	&& (i.tm.operand_types[op] & (Reg16 | Reg32 | Acc)) != 0)
      {
	as_bad (_("`%s%s' not allowed with `%s%c'"),
		register_prefix,
		i.op[op].regs->reg_name,
		i.tm.name,
		i.suffix);
	return 0;
      }
  /* Warn if the e prefix on a general reg is missing.  */
    else if (((i.types[op] & Reg16) != 0
	      || (i.types[op] & Reg32) != 0)
	     && (i.tm.operand_types[op] & (Reg32 | Acc)) != 0)
      {
	/* Prohibit these changes in the 64bit mode, since the
	   lowering is more complicated.  */
	as_bad (_("Incorrect register `%s%s' used with `%c' suffix"),
		register_prefix, i.op[op].regs->reg_name,
		i.suffix);
	return 0;
      }
  return 1;
}

static int
check_word_reg (void)
{
  int op;
  for (op = i.operands; --op >= 0;)
    /* Reject eight bit registers, except where the template requires
       them. (eg. movzb)  */
    if ((i.types[op] & Reg8) != 0
	&& (i.tm.operand_types[op] & (Reg16 | Reg32 | Acc)) != 0)
      {
	as_bad (_("`%s%s' not allowed with `%s%c'"),
		register_prefix,
		i.op[op].regs->reg_name,
		i.tm.name,
		i.suffix);
	return 0;
      }
  /* Warn if the e prefix on a general reg is present.  */
    else if ((!quiet_warnings || flag_code == CODE_64BIT)
	     && (i.types[op] & Reg32) != 0
	     && (i.tm.operand_types[op] & (Reg16 | Acc)) != 0)
      {
	/* Prohibit these changes in the 64bit mode, since the
	   lowering is more complicated.  */
	if (flag_code == CODE_64BIT)
	  {
	    as_bad (_("Incorrect register `%s%s' used with `%c' suffix"),
		    register_prefix, i.op[op].regs->reg_name,
		    i.suffix);
	    return 0;
	  }
	else
#if REGISTER_WARNINGS
	  as_warn (_("using `%s%s' instead of `%s%s' due to `%c' suffix"),
		   register_prefix,
		   (i.op[op].regs + REGNAM_AX - REGNAM_EAX)->reg_name,
		   register_prefix,
		   i.op[op].regs->reg_name,
		   i.suffix);
#endif
      }
  return 1;
}

static int
finalize_imm (void)
{
  unsigned int overlap0, overlap1, overlap2;

  overlap0 = i.types[0] & i.tm.operand_types[0];
  if ((overlap0 & (Imm8 | Imm8S | Imm16 | Imm32 | Imm32S | Imm64))
      && overlap0 != Imm8 && overlap0 != Imm8S
      && overlap0 != Imm16 && overlap0 != Imm32S
      && overlap0 != Imm32 && overlap0 != Imm64)
    {
      if (i.suffix)
	{
	  overlap0 &= (i.suffix == BYTE_MNEM_SUFFIX
		       ? Imm8 | Imm8S
		       : (i.suffix == WORD_MNEM_SUFFIX
			  ? Imm16
			  : (i.suffix == QWORD_MNEM_SUFFIX
			     ? Imm64 | Imm32S
			     : Imm32)));
	}
      else if (overlap0 == (Imm16 | Imm32S | Imm32)
	       || overlap0 == (Imm16 | Imm32)
	       || overlap0 == (Imm16 | Imm32S))
	{
	  overlap0 = ((flag_code == CODE_16BIT) ^ (i.prefix[DATA_PREFIX] != 0)
		      ? Imm16 : Imm32S);
	}
      if (overlap0 != Imm8 && overlap0 != Imm8S
	  && overlap0 != Imm16 && overlap0 != Imm32S
	  && overlap0 != Imm32 && overlap0 != Imm64)
	{
	  as_bad (_("no instruction mnemonic suffix given; "
		    "can't determine immediate size"));
	  return 0;
	}
    }
  i.types[0] = overlap0;

  overlap1 = i.types[1] & i.tm.operand_types[1];
  if ((overlap1 & (Imm8 | Imm8S | Imm16 | Imm32S | Imm32 | Imm64))
      && overlap1 != Imm8 && overlap1 != Imm8S
      && overlap1 != Imm16 && overlap1 != Imm32S
      && overlap1 != Imm32 && overlap1 != Imm64)
    {
      if (i.suffix)
	{
	  overlap1 &= (i.suffix == BYTE_MNEM_SUFFIX
		       ? Imm8 | Imm8S
		       : (i.suffix == WORD_MNEM_SUFFIX
			  ? Imm16
			  : (i.suffix == QWORD_MNEM_SUFFIX
			     ? Imm64 | Imm32S
			     : Imm32)));
	}
      else if (overlap1 == (Imm16 | Imm32 | Imm32S)
	       || overlap1 == (Imm16 | Imm32)
	       || overlap1 == (Imm16 | Imm32S))
	{
	  overlap1 = ((flag_code == CODE_16BIT) ^ (i.prefix[DATA_PREFIX] != 0)
		      ? Imm16 : Imm32S);
	}
      if (overlap1 != Imm8 && overlap1 != Imm8S
	  && overlap1 != Imm16 && overlap1 != Imm32S
	  && overlap1 != Imm32 && overlap1 != Imm64)
	{
	  as_bad (_("no instruction mnemonic suffix given; "
		    "can't determine immediate size %x %c"),
		  overlap1, i.suffix);
	  return 0;
	}
    }
  i.types[1] = overlap1;

  overlap2 = i.types[2] & i.tm.operand_types[2];
  assert ((overlap2 & Imm) == 0);
  i.types[2] = overlap2;

  return 1;
}

static int
process_operands (void)
{
  /* Default segment register this instruction will use for memory
     accesses.  0 means unknown.  This is only for optimizing out
     unnecessary segment overrides.  */
  const seg_entry *default_seg = 0;

  /* The imul $imm, %reg instruction is converted into
     imul $imm, %reg, %reg, and the clr %reg instruction
     is converted into xor %reg, %reg.  */
  if (i.tm.opcode_modifier & RegKludge)
    {
       if ((i.tm.cpu_flags & CpuSSE4_1))
	 {
	   /* The first operand in instruction blendvpd, blendvps and
	      pblendvb in SSE4.1 is implicit and must be xmm0.  */
	   assert (i.operands == 3
		   && i.reg_operands >= 2
		   && i.types[0] == RegXMM);
	   if (i.op[0].regs->reg_num != 0)
	     {
	       if (intel_syntax)
		 as_bad (_("the last operand of `%s' must be `%sxmm0'"),
			 i.tm.name, register_prefix);
	       else
		 as_bad (_("the first operand of `%s' must be `%sxmm0'"),
			 i.tm.name, register_prefix);
	       return 0;
	     }
	   i.op[0] = i.op[1];
	   i.op[1] = i.op[2];
	   i.types[0] = i.types[1];
	   i.types[1] = i.types[2];
	   i.operands--;
	   i.reg_operands--;

	   /* We need to adjust fields in i.tm since they are used by
	      build_modrm_byte.  */
	   i.tm.operand_types [0] = i.tm.operand_types [1];
	   i.tm.operand_types [1] = i.tm.operand_types [2];
	   i.tm.operands--;
	 }
       else
	 {
	   unsigned int first_reg_op = (i.types[0] & Reg) ? 0 : 1;
	   /* Pretend we saw the extra register operand.  */
	   assert (i.reg_operands == 1
		   && i.op[first_reg_op + 1].regs == 0);
	   i.op[first_reg_op + 1].regs = i.op[first_reg_op].regs;
	   i.types[first_reg_op + 1] = i.types[first_reg_op];
	   i.operands++;
	   i.reg_operands++;
	 }
    }

  if (i.tm.opcode_modifier & ShortForm)
    {
      if (i.types[0] & (SReg2 | SReg3))
	{
	  if (i.tm.base_opcode == POP_SEG_SHORT
	      && i.op[0].regs->reg_num == 1)
	    {
	      as_bad (_("you can't `pop %%cs'"));
	      return 0;
	    }
	  i.tm.base_opcode |= (i.op[0].regs->reg_num << 3);
	  if ((i.op[0].regs->reg_flags & RegRex) != 0)
	    i.rex |= REX_B;
	}
      else
	{
	  /* The register or float register operand is in operand 0 or 1.  */
	  unsigned int op = (i.types[0] & (Reg | FloatReg)) ? 0 : 1;
	  /* Register goes in low 3 bits of opcode.  */
	  i.tm.base_opcode |= i.op[op].regs->reg_num;
	  if ((i.op[op].regs->reg_flags & RegRex) != 0)
	    i.rex |= REX_B;
	  if (!quiet_warnings && (i.tm.opcode_modifier & Ugh) != 0)
	    {
	      /* Warn about some common errors, but press on regardless.
		 The first case can be generated by gcc (<= 2.8.1).  */
	      if (i.operands == 2)
		{
		  /* Reversed arguments on faddp, fsubp, etc.  */
		  as_warn (_("translating to `%s %s%s,%s%s'"), i.tm.name,
			   register_prefix, i.op[1].regs->reg_name,
			   register_prefix, i.op[0].regs->reg_name);
		}
	      else
		{
		  /* Extraneous `l' suffix on fp insn.  */
		  as_warn (_("translating to `%s %s%s'"), i.tm.name,
			   register_prefix, i.op[0].regs->reg_name);
		}
	    }
	}
    }
  else if (i.tm.opcode_modifier & Modrm)
    {
      /* The opcode is completed (modulo i.tm.extension_opcode which
	 must be put into the modrm byte).  Now, we make the modrm and
	 index base bytes based on all the info we've collected.  */

      default_seg = build_modrm_byte ();
    }
  else if ((i.tm.base_opcode & ~0x3) == MOV_AX_DISP32)
    {
      default_seg = &ds;
    }
  else if ((i.tm.opcode_modifier & IsString) != 0)
    {
      /* For the string instructions that allow a segment override
	 on one of their operands, the default segment is ds.  */
      default_seg = &ds;
    }

  if ((i.tm.base_opcode == 0x8d /* lea */
       || (i.tm.cpu_flags & CpuSVME))
      && i.seg[0] && !quiet_warnings)
    as_warn (_("segment override on `%s' is ineffectual"), i.tm.name);

  /* If a segment was explicitly specified, and the specified segment
     is not the default, use an opcode prefix to select it.  If we
     never figured out what the default segment is, then default_seg
     will be zero at this point, and the specified segment prefix will
     always be used.  */
  if ((i.seg[0]) && (i.seg[0] != default_seg))
    {
      if (!add_prefix (i.seg[0]->seg_prefix))
	return 0;
    }
  return 1;
}

static const seg_entry *
build_modrm_byte (void)
{
  const seg_entry *default_seg = 0;

  /* i.reg_operands MUST be the number of real register operands;
     implicit registers do not count.  */
  if (i.reg_operands == 2)
    {
      unsigned int source, dest;

      switch (i.operands)
	{
	case 2:
	  source = 0;
	  break;
	case 3:
	  /* When there are 3 operands, one of them may be immediate,
	     which may be the first or the last operand.  Otherwise,
	     the first operand must be shift count register (cl). */
	  assert (i.imm_operands == 1
		  || (i.imm_operands == 0
		      && (i.types[0] & ShiftCount)));
	  source = (i.types[0] & (Imm | ShiftCount)) ? 1 : 0;
	  break;
	case 4:
	  /* When there are 4 operands, the first two must be immediate
	     operands. The source operand will be the 3rd one.  */
	  assert (i.imm_operands == 2
		  && (i.types[0] & Imm)
		  && (i.types[1] & Imm));
	  source = 2;
	  break;
	default:
	  abort ();
	}

      dest = source + 1;

      i.rm.mode = 3;
      /* One of the register operands will be encoded in the i.tm.reg
	 field, the other in the combined i.tm.mode and i.tm.regmem
	 fields.  If no form of this instruction supports a memory
	 destination operand, then we assume the source operand may
	 sometimes be a memory operand and so we need to store the
	 destination in the i.rm.reg field.  */
      if ((i.tm.operand_types[dest] & (AnyMem | RegMem)) == 0)
	{
	  i.rm.reg = i.op[dest].regs->reg_num;
	  i.rm.regmem = i.op[source].regs->reg_num;
	  if ((i.op[dest].regs->reg_flags & RegRex) != 0)
	    i.rex |= REX_R;
	  if ((i.op[source].regs->reg_flags & RegRex) != 0)
	    i.rex |= REX_B;
	}
      else
	{
	  i.rm.reg = i.op[source].regs->reg_num;
	  i.rm.regmem = i.op[dest].regs->reg_num;
	  if ((i.op[dest].regs->reg_flags & RegRex) != 0)
	    i.rex |= REX_B;
	  if ((i.op[source].regs->reg_flags & RegRex) != 0)
	    i.rex |= REX_R;
	}
      if (flag_code != CODE_64BIT && (i.rex & (REX_R | REX_B)))
	{
	  if (!((i.types[0] | i.types[1]) & Control))
	    abort ();
	  i.rex &= ~(REX_R | REX_B);
	  add_prefix (LOCK_PREFIX_OPCODE);
	}
    }
  else
    {			/* If it's not 2 reg operands...  */
      if (i.mem_operands)
	{
	  unsigned int fake_zero_displacement = 0;
	  unsigned int op;

	  for (op = 0; op < i.operands; op++)
	    if ((i.types[op] & AnyMem))
	      break;
	  assert (op < i.operands);

	  default_seg = &ds;

	  if (i.base_reg == 0)
	    {
	      i.rm.mode = 0;
	      if (!i.disp_operands)
		fake_zero_displacement = 1;
	      if (i.index_reg == 0)
		{
		  /* Operand is just <disp>  */
		  if (flag_code == CODE_64BIT)
		    {
		      /* 64bit mode overwrites the 32bit absolute
			 addressing by RIP relative addressing and
			 absolute addressing is encoded by one of the
			 redundant SIB forms.  */
		      i.rm.regmem = ESCAPE_TO_TWO_BYTE_ADDRESSING;
		      i.sib.base = NO_BASE_REGISTER;
		      i.sib.index = NO_INDEX_REGISTER;
		      i.types[op] = ((i.prefix[ADDR_PREFIX] == 0)
				     ? Disp32S : Disp32);
		    }
		  else if ((flag_code == CODE_16BIT)
			   ^ (i.prefix[ADDR_PREFIX] != 0))
		    {
		      i.rm.regmem = NO_BASE_REGISTER_16;
		      i.types[op] = Disp16;
		    }
		  else
		    {
		      i.rm.regmem = NO_BASE_REGISTER;
		      i.types[op] = Disp32;
		    }
		}
	      else /* !i.base_reg && i.index_reg  */
		{
		  i.sib.index = i.index_reg->reg_num;
		  i.sib.base = NO_BASE_REGISTER;
		  i.sib.scale = i.log2_scale_factor;
		  i.rm.regmem = ESCAPE_TO_TWO_BYTE_ADDRESSING;
		  i.types[op] &= ~Disp;
		  if (flag_code != CODE_64BIT)
		    i.types[op] |= Disp32;	/* Must be 32 bit */
		  else
		    i.types[op] |= Disp32S;
		  if ((i.index_reg->reg_flags & RegRex) != 0)
		    i.rex |= REX_X;
		}
	    }
	  /* RIP addressing for 64bit mode.  */
	  else if (i.base_reg->reg_type == BaseIndex)
	    {
	      i.rm.regmem = NO_BASE_REGISTER;
	      i.types[op] &= ~ Disp;
	      i.types[op] |= Disp32S;
	      i.flags[op] |= Operand_PCrel;
	      if (! i.disp_operands)
		fake_zero_displacement = 1;
	    }
	  else if (i.base_reg->reg_type & Reg16)
	    {
	      switch (i.base_reg->reg_num)
		{
		case 3: /* (%bx)  */
		  if (i.index_reg == 0)
		    i.rm.regmem = 7;
		  else /* (%bx,%si) -> 0, or (%bx,%di) -> 1  */
		    i.rm.regmem = i.index_reg->reg_num - 6;
		  break;
		case 5: /* (%bp)  */
		  default_seg = &ss;
		  if (i.index_reg == 0)
		    {
		      i.rm.regmem = 6;
		      if ((i.types[op] & Disp) == 0)
			{
			  /* fake (%bp) into 0(%bp)  */
			  i.types[op] |= Disp8;
			  fake_zero_displacement = 1;
			}
		    }
		  else /* (%bp,%si) -> 2, or (%bp,%di) -> 3  */
		    i.rm.regmem = i.index_reg->reg_num - 6 + 2;
		  break;
		default: /* (%si) -> 4 or (%di) -> 5  */
		  i.rm.regmem = i.base_reg->reg_num - 6 + 4;
		}
	      i.rm.mode = mode_from_disp_size (i.types[op]);
	    }
	  else /* i.base_reg and 32/64 bit mode  */
	    {
	      if (flag_code == CODE_64BIT
		  && (i.types[op] & Disp))
		i.types[op] = ((i.types[op] & Disp8)
			       | (i.prefix[ADDR_PREFIX] == 0
				  ? Disp32S : Disp32));

	      i.rm.regmem = i.base_reg->reg_num;
	      if ((i.base_reg->reg_flags & RegRex) != 0)
		i.rex |= REX_B;
	      i.sib.base = i.base_reg->reg_num;
	      /* x86-64 ignores REX prefix bit here to avoid decoder
		 complications.  */
	      if ((i.base_reg->reg_num & 7) == EBP_REG_NUM)
		{
		  default_seg = &ss;
		  if (i.disp_operands == 0)
		    {
		      fake_zero_displacement = 1;
		      i.types[op] |= Disp8;
		    }
		}
	      else if (i.base_reg->reg_num == ESP_REG_NUM)
		{
		  default_seg = &ss;
		}
	      i.sib.scale = i.log2_scale_factor;
	      if (i.index_reg == 0)
		{
		  /* <disp>(%esp) becomes two byte modrm with no index
		     register.  We've already stored the code for esp
		     in i.rm.regmem ie. ESCAPE_TO_TWO_BYTE_ADDRESSING.
		     Any base register besides %esp will not use the
		     extra modrm byte.  */
		  i.sib.index = NO_INDEX_REGISTER;
#if !SCALE1_WHEN_NO_INDEX
		  /* Another case where we force the second modrm byte.  */
		  if (i.log2_scale_factor)
		    i.rm.regmem = ESCAPE_TO_TWO_BYTE_ADDRESSING;
#endif
		}
	      else
		{
		  i.sib.index = i.index_reg->reg_num;
		  i.rm.regmem = ESCAPE_TO_TWO_BYTE_ADDRESSING;
		  if ((i.index_reg->reg_flags & RegRex) != 0)
		    i.rex |= REX_X;
		}

	      if (i.disp_operands
		  && (i.reloc[op] == BFD_RELOC_386_TLS_DESC_CALL
		      || i.reloc[op] == BFD_RELOC_X86_64_TLSDESC_CALL))
		i.rm.mode = 0;
	      else
		i.rm.mode = mode_from_disp_size (i.types[op]);
	    }

	  if (fake_zero_displacement)
	    {
	      /* Fakes a zero displacement assuming that i.types[op]
		 holds the correct displacement size.  */
	      expressionS *exp;

	      assert (i.op[op].disps == 0);
	      exp = &disp_expressions[i.disp_operands++];
	      i.op[op].disps = exp;
	      exp->X_op = O_constant;
	      exp->X_add_number = 0;
	      exp->X_add_symbol = (symbolS *) 0;
	      exp->X_op_symbol = (symbolS *) 0;
	    }
	}

      /* Fill in i.rm.reg or i.rm.regmem field with register operand
	 (if any) based on i.tm.extension_opcode.  Again, we must be
	 careful to make sure that segment/control/debug/test/MMX
	 registers are coded into the i.rm.reg field.  */
      if (i.reg_operands)
	{
	  unsigned int op;

	  for (op = 0; op < i.operands; op++)
	    if ((i.types[op] & (Reg | RegMMX | RegXMM
				| SReg2 | SReg3
				| Control | Debug | Test)))
	      break;
	  assert (op < i.operands);

	  /* If there is an extension opcode to put here, the register
	     number must be put into the regmem field.  */
	  if (i.tm.extension_opcode != None)
	    {
	      i.rm.regmem = i.op[op].regs->reg_num;
	      if ((i.op[op].regs->reg_flags & RegRex) != 0)
		i.rex |= REX_B;
	    }
	  else
	    {
	      i.rm.reg = i.op[op].regs->reg_num;
	      if ((i.op[op].regs->reg_flags & RegRex) != 0)
		i.rex |= REX_R;
	    }

	  /* Now, if no memory operand has set i.rm.mode = 0, 1, 2 we
	     must set it to 3 to indicate this is a register operand
	     in the regmem field.  */
	  if (!i.mem_operands)
	    i.rm.mode = 3;
	}

      /* Fill in i.rm.reg field with extension opcode (if any).  */
      if (i.tm.extension_opcode != None)
	i.rm.reg = i.tm.extension_opcode;
    }
  return default_seg;
}

static void
output_branch (void)
{
  char *p;
  int code16;
  int prefix;
  relax_substateT subtype;
  symbolS *sym;
  offsetT off;

  code16 = 0;
  if (flag_code == CODE_16BIT)
    code16 = CODE16;

  prefix = 0;
  if (i.prefix[DATA_PREFIX] != 0)
    {
      prefix = 1;
      i.prefixes -= 1;
      code16 ^= CODE16;
    }
  /* Pentium4 branch hints.  */
  if (i.prefix[SEG_PREFIX] == CS_PREFIX_OPCODE /* not taken */
      || i.prefix[SEG_PREFIX] == DS_PREFIX_OPCODE /* taken */)
    {
      prefix++;
      i.prefixes--;
    }
  if (i.prefix[REX_PREFIX] != 0)
    {
      prefix++;
      i.prefixes--;
    }

  if (i.prefixes != 0 && !intel_syntax)
    as_warn (_("skipping prefixes on this instruction"));

  /* It's always a symbol;  End frag & setup for relax.
     Make sure there is enough room in this frag for the largest
     instruction we may generate in md_convert_frag.  This is 2
     bytes for the opcode and room for the prefix and largest
     displacement.  */
  frag_grow (prefix + 2 + 4);
  /* Prefix and 1 opcode byte go in fr_fix.  */
  p = frag_more (prefix + 1);
  if (i.prefix[DATA_PREFIX] != 0)
    *p++ = DATA_PREFIX_OPCODE;
  if (i.prefix[SEG_PREFIX] == CS_PREFIX_OPCODE
      || i.prefix[SEG_PREFIX] == DS_PREFIX_OPCODE)
    *p++ = i.prefix[SEG_PREFIX];
  if (i.prefix[REX_PREFIX] != 0)
    *p++ = i.prefix[REX_PREFIX];
  *p = i.tm.base_opcode;

  if ((unsigned char) *p == JUMP_PC_RELATIVE)
    subtype = ENCODE_RELAX_STATE (UNCOND_JUMP, SMALL);
  else if ((cpu_arch_flags & Cpu386) != 0)
    subtype = ENCODE_RELAX_STATE (COND_JUMP, SMALL);
  else
    subtype = ENCODE_RELAX_STATE (COND_JUMP86, SMALL);
  subtype |= code16;

  sym = i.op[0].disps->X_add_symbol;
  off = i.op[0].disps->X_add_number;

  if (i.op[0].disps->X_op != O_constant
      && i.op[0].disps->X_op != O_symbol)
    {
      /* Handle complex expressions.  */
      sym = make_expr_symbol (i.op[0].disps);
      off = 0;
    }

  /* 1 possible extra opcode + 4 byte displacement go in var part.
     Pass reloc in fr_var.  */
  frag_var (rs_machine_dependent, 5, i.reloc[0], subtype, sym, off, p);
}

static void
output_jump (void)
{
  char *p;
  int size;
  fixS *fixP;

  if (i.tm.opcode_modifier & JumpByte)
    {
      /* This is a loop or jecxz type instruction.  */
      size = 1;
      if (i.prefix[ADDR_PREFIX] != 0)
	{
	  FRAG_APPEND_1_CHAR (ADDR_PREFIX_OPCODE);
	  i.prefixes -= 1;
	}
      /* Pentium4 branch hints.  */
      if (i.prefix[SEG_PREFIX] == CS_PREFIX_OPCODE /* not taken */
	  || i.prefix[SEG_PREFIX] == DS_PREFIX_OPCODE /* taken */)
	{
	  FRAG_APPEND_1_CHAR (i.prefix[SEG_PREFIX]);
	  i.prefixes--;
	}
    }
  else
    {
      int code16;

      code16 = 0;
      if (flag_code == CODE_16BIT)
	code16 = CODE16;

      if (i.prefix[DATA_PREFIX] != 0)
	{
	  FRAG_APPEND_1_CHAR (DATA_PREFIX_OPCODE);
	  i.prefixes -= 1;
	  code16 ^= CODE16;
	}

      size = 4;
      if (code16)
	size = 2;
    }

  if (i.prefix[REX_PREFIX] != 0)
    {
      FRAG_APPEND_1_CHAR (i.prefix[REX_PREFIX]);
      i.prefixes -= 1;
    }

  if (i.prefixes != 0 && !intel_syntax)
    as_warn (_("skipping prefixes on this instruction"));

  p = frag_more (1 + size);
  *p++ = i.tm.base_opcode;

  fixP = fix_new_exp (frag_now, p - frag_now->fr_literal, size,
		      i.op[0].disps, 1, reloc (size, 1, 1, i.reloc[0]));

  /* All jumps handled here are signed, but don't use a signed limit
     check for 32 and 16 bit jumps as we want to allow wrap around at
     4G and 64k respectively.  */
  if (size == 1)
    fixP->fx_signed = 1;
}

static void
output_interseg_jump (void)
{
  char *p;
  int size;
  int prefix;
  int code16;

  code16 = 0;
  if (flag_code == CODE_16BIT)
    code16 = CODE16;

  prefix = 0;
  if (i.prefix[DATA_PREFIX] != 0)
    {
      prefix = 1;
      i.prefixes -= 1;
      code16 ^= CODE16;
    }
  if (i.prefix[REX_PREFIX] != 0)
    {
      prefix++;
      i.prefixes -= 1;
    }

  size = 4;
  if (code16)
    size = 2;

  if (i.prefixes != 0 && !intel_syntax)
    as_warn (_("skipping prefixes on this instruction"));

  /* 1 opcode; 2 segment; offset  */
  p = frag_more (prefix + 1 + 2 + size);

  if (i.prefix[DATA_PREFIX] != 0)
    *p++ = DATA_PREFIX_OPCODE;

  if (i.prefix[REX_PREFIX] != 0)
    *p++ = i.prefix[REX_PREFIX];

  *p++ = i.tm.base_opcode;
  if (i.op[1].imms->X_op == O_constant)
    {
      offsetT n = i.op[1].imms->X_add_number;

      if (size == 2
	  && !fits_in_unsigned_word (n)
	  && !fits_in_signed_word (n))
	{
	  as_bad (_("16-bit jump out of range"));
	  return;
	}
      md_number_to_chars (p, n, size);
    }
  else
    fix_new_exp (frag_now, p - frag_now->fr_literal, size,
		 i.op[1].imms, 0, reloc (size, 0, 0, i.reloc[1]));
  if (i.op[0].imms->X_op != O_constant)
    as_bad (_("can't handle non absolute segment in `%s'"),
	    i.tm.name);
  md_number_to_chars (p + size, (valueT) i.op[0].imms->X_add_number, 2);
}

static void
output_insn (void)
{
  fragS *insn_start_frag;
  offsetT insn_start_off;

  /* Tie dwarf2 debug info to the address at the start of the insn.
     We can't do this after the insn has been output as the current
     frag may have been closed off.  eg. by frag_var.  */
  dwarf2_emit_insn (0);

  insn_start_frag = frag_now;
  insn_start_off = frag_now_fix ();

  /* Output jumps.  */
  if (i.tm.opcode_modifier & Jump)
    output_branch ();
  else if (i.tm.opcode_modifier & (JumpByte | JumpDword))
    output_jump ();
  else if (i.tm.opcode_modifier & JumpInterSegment)
    output_interseg_jump ();
  else
    {
      /* Output normal instructions here.  */
      char *p;
      unsigned char *q;
      unsigned int prefix;

      /* All opcodes on i386 have either 1 or 2 bytes.  SSSE3 and
	 SSE4 instructions have 3 bytes.  We may use one more higher
	 byte to specify a prefix the instruction requires.  Exclude
	 instructions which are in both SSE4 and ABM.  */
      if ((i.tm.cpu_flags & (CpuSSSE3 | CpuSSE4 | CpuAES | CpuPCLMUL)) != 0
	  && (i.tm.cpu_flags & CpuABM) == 0)
	{
	  if (i.tm.base_opcode & 0xff000000)
	    {
	      prefix = (i.tm.base_opcode >> 24) & 0xff;
	      goto check_prefix;
	    }
	}
      else if (i.tm.base_opcode == 0x660f3880 || i.tm.base_opcode == 0x660f3881
	  || i.tm.base_opcode == 0x660f3882)
	{
	  /* invept and invvpid are 3 byte instructions with a
	     mandatory prefix. */
	  if (i.tm.base_opcode & 0xff000000)
	    {
	      prefix = (i.tm.base_opcode >> 24) & 0xff;
	      add_prefix (prefix);
	    }
	}
      else if ((i.tm.base_opcode & 0xff0000) != 0)
	{
	  prefix = (i.tm.base_opcode >> 16) & 0xff;
	  if ((i.tm.cpu_flags & CpuPadLock) != 0)
	    {
	    check_prefix:
	      if (prefix != REPE_PREFIX_OPCODE
		  || i.prefix[LOCKREP_PREFIX] != REPE_PREFIX_OPCODE)
		add_prefix (prefix);
	    }
	  else
	    add_prefix (prefix);
	}

      /* The prefix bytes.  */
      for (q = i.prefix;
	   q < i.prefix + sizeof (i.prefix) / sizeof (i.prefix[0]);
	   q++)
	{
	  if (*q)
	    {
	      p = frag_more (1);
	      md_number_to_chars (p, (valueT) *q, 1);
	    }
	}

      /* Now the opcode; be careful about word order here!  */
      if (fits_in_unsigned_byte (i.tm.base_opcode))
	{
	  FRAG_APPEND_1_CHAR (i.tm.base_opcode);
	}
      else
	{
	  if ((i.tm.cpu_flags & (CpuSSSE3 | CpuSSE4 | CpuAES | CpuPCLMUL)) != 0
	      && (i.tm.cpu_flags & CpuABM) == 0)
	    {
	      p = frag_more (3);
	      *p++ = (i.tm.base_opcode >> 16) & 0xff;
	    }
	  else if (i.tm.base_opcode == 0x660f3880 ||
		   i.tm.base_opcode == 0x660f3881 ||
		   i.tm.base_opcode == 0x660f3882)
	    {
	      p = frag_more (3);
	      *p++ = (i.tm.base_opcode >> 16) & 0xff;
	    }
	  else
	    p = frag_more (2);

	  /* Put out high byte first: can't use md_number_to_chars!  */
	  *p++ = (i.tm.base_opcode >> 8) & 0xff;
	  *p = i.tm.base_opcode & 0xff;
	}

      /* Now the modrm byte and sib byte (if present).  */
      if (i.tm.opcode_modifier & Modrm)
	{
	  p = frag_more (1);
	  md_number_to_chars (p,
			      (valueT) (i.rm.regmem << 0
					| i.rm.reg << 3
					| i.rm.mode << 6),
			      1);
	  /* If i.rm.regmem == ESP (4)
	     && i.rm.mode != (Register mode)
	     && not 16 bit
	     ==> need second modrm byte.  */
	  if (i.rm.regmem == ESCAPE_TO_TWO_BYTE_ADDRESSING
	      && i.rm.mode != 3
	      && !(i.base_reg && (i.base_reg->reg_type & Reg16) != 0))
	    {
	      p = frag_more (1);
	      md_number_to_chars (p,
				  (valueT) (i.sib.base << 0
					    | i.sib.index << 3
					    | i.sib.scale << 6),
				  1);
	    }
	}

      if (i.disp_operands)
	output_disp (insn_start_frag, insn_start_off);

      if (i.imm_operands)
	output_imm (insn_start_frag, insn_start_off);
    }

#ifdef DEBUG386
  if (flag_debug)
    {
      pi ("" /*line*/, &i);
    }
#endif /* DEBUG386  */
}

/* Return the size of the displacement operand N.  */

static int
disp_size (unsigned int n)
{
  int size = 4;
  if (i.types[n] & (Disp8 | Disp16 | Disp64))
    {
      size = 2;
      if (i.types[n] & Disp8)
	size = 1;
      if (i.types[n] & Disp64)
	size = 8;
    }
  return size;
}

/* Return the size of the immediate operand N.  */

static int
imm_size (unsigned int n)
{
  int size = 4;
  if (i.types[n] & (Imm8 | Imm8S | Imm16 | Imm64))
    {
      size = 2;
      if (i.types[n] & (Imm8 | Imm8S))
	size = 1;
      if (i.types[n] & Imm64)
	size = 8;
    }
  return size;
}

static void
output_disp (fragS *insn_start_frag, offsetT insn_start_off)
{
  char *p;
  unsigned int n;

  for (n = 0; n < i.operands; n++)
    {
      if (i.types[n] & Disp)
	{
	  if (i.op[n].disps->X_op == O_constant)
	    {
	      int size = disp_size (n);
	      offsetT val;

	      val = offset_in_range (i.op[n].disps->X_add_number,
				     size);
	      p = frag_more (size);
	      md_number_to_chars (p, val, size);
	    }
	  else
	    {
	      enum bfd_reloc_code_real reloc_type;
	      int size = disp_size (n);
	      int sign = (i.types[n] & Disp32S) != 0;
	      int pcrel = (i.flags[n] & Operand_PCrel) != 0;

	      /* We can't have 8 bit displacement here.  */
	      assert ((i.types[n] & Disp8) == 0);

	      /* The PC relative address is computed relative
		 to the instruction boundary, so in case immediate
		 fields follows, we need to adjust the value.  */
	      if (pcrel && i.imm_operands)
		{
		  unsigned int n1;
		  int sz = 0;

		  for (n1 = 0; n1 < i.operands; n1++)
		    if (i.types[n1] & Imm)
		      {
			/* Only one immediate is allowed for PC
			   relative address.  */
			assert (sz == 0);
			sz = imm_size (n1);
			i.op[n].disps->X_add_number -= sz;
		      }
		  /* We should find the immediate.  */
		  assert (sz != 0);
		}

	      p = frag_more (size);
	      reloc_type = reloc (size, pcrel, sign, i.reloc[n]);
	      if (GOT_symbol
		  && GOT_symbol == i.op[n].disps->X_add_symbol
		  && (((reloc_type == BFD_RELOC_32
			|| reloc_type == BFD_RELOC_X86_64_32S
			|| (reloc_type == BFD_RELOC_64
			    && object_64bit))
		       && (i.op[n].disps->X_op == O_symbol
			   || (i.op[n].disps->X_op == O_add
			       && ((symbol_get_value_expression
				    (i.op[n].disps->X_op_symbol)->X_op)
				   == O_subtract))))
		      || reloc_type == BFD_RELOC_32_PCREL))
		{
		  offsetT add;

		  if (insn_start_frag == frag_now)
		    add = (p - frag_now->fr_literal) - insn_start_off;
		  else
		    {
		      fragS *fr;

		      add = insn_start_frag->fr_fix - insn_start_off;
		      for (fr = insn_start_frag->fr_next;
			   fr && fr != frag_now; fr = fr->fr_next)
			add += fr->fr_fix;
		      add += p - frag_now->fr_literal;
		    }

		  if (!object_64bit)
		    {
		      reloc_type = BFD_RELOC_386_GOTPC;
		      i.op[n].imms->X_add_number += add;
		    }
		  else if (reloc_type == BFD_RELOC_64)
		    reloc_type = BFD_RELOC_X86_64_GOTPC64;
		  else
		    /* Don't do the adjustment for x86-64, as there
		       the pcrel addressing is relative to the _next_
		       insn, and that is taken care of in other code.  */
		    reloc_type = BFD_RELOC_X86_64_GOTPC32;
		}
	      fix_new_exp (frag_now, p - frag_now->fr_literal, size,
			   i.op[n].disps, pcrel, reloc_type);
	    }
	}
    }
}

static void
output_imm (fragS *insn_start_frag, offsetT insn_start_off)
{
  char *p;
  unsigned int n;

  for (n = 0; n < i.operands; n++)
    {
      if (i.types[n] & Imm)
	{
	  if (i.op[n].imms->X_op == O_constant)
	    {
	      int size = imm_size (n);
	      offsetT val;

	      val = offset_in_range (i.op[n].imms->X_add_number,
				     size);
	      p = frag_more (size);
	      md_number_to_chars (p, val, size);
	    }
	  else
	    {
	      /* Not absolute_section.
		 Need a 32-bit fixup (don't support 8bit
		 non-absolute imms).  Try to support other
		 sizes ...  */
	      enum bfd_reloc_code_real reloc_type;
	      int size = imm_size (n);
	      int sign;

	      if ((i.types[n] & (Imm32S))
		  && (i.suffix == QWORD_MNEM_SUFFIX
		      || (!i.suffix && (i.tm.opcode_modifier & No_lSuf))))
		sign = 1;
	      else
		sign = 0;

	      p = frag_more (size);
	      reloc_type = reloc (size, 0, sign, i.reloc[n]);

	      /*   This is tough to explain.  We end up with this one if we
	       * have operands that look like
	       * "_GLOBAL_OFFSET_TABLE_+[.-.L284]".  The goal here is to
	       * obtain the absolute address of the GOT, and it is strongly
	       * preferable from a performance point of view to avoid using
	       * a runtime relocation for this.  The actual sequence of
	       * instructions often look something like:
	       *
	       *	call	.L66
	       * .L66:
	       *	popl	%ebx
	       *	addl	$_GLOBAL_OFFSET_TABLE_+[.-.L66],%ebx
	       *
	       *   The call and pop essentially return the absolute address
	       * of the label .L66 and store it in %ebx.  The linker itself
	       * will ultimately change the first operand of the addl so
	       * that %ebx points to the GOT, but to keep things simple, the
	       * .o file must have this operand set so that it generates not
	       * the absolute address of .L66, but the absolute address of
	       * itself.  This allows the linker itself simply treat a GOTPC
	       * relocation as asking for a pcrel offset to the GOT to be
	       * added in, and the addend of the relocation is stored in the
	       * operand field for the instruction itself.
	       *
	       *   Our job here is to fix the operand so that it would add
	       * the correct offset so that %ebx would point to itself.  The
	       * thing that is tricky is that .-.L66 will point to the
	       * beginning of the instruction, so we need to further modify
	       * the operand so that it will point to itself.  There are
	       * other cases where you have something like:
	       *
	       *	.long	$_GLOBAL_OFFSET_TABLE_+[.-.L66]
	       *
	       * and here no correction would be required.  Internally in
	       * the assembler we treat operands of this form as not being
	       * pcrel since the '.' is explicitly mentioned, and I wonder
	       * whether it would simplify matters to do it this way.  Who
	       * knows.  In earlier versions of the PIC patches, the
	       * pcrel_adjust field was used to store the correction, but
	       * since the expression is not pcrel, I felt it would be
	       * confusing to do it this way.  */

	      if ((reloc_type == BFD_RELOC_32
		   || reloc_type == BFD_RELOC_X86_64_32S
		   || reloc_type == BFD_RELOC_64)
		  && GOT_symbol
		  && GOT_symbol == i.op[n].imms->X_add_symbol
		  && (i.op[n].imms->X_op == O_symbol
		      || (i.op[n].imms->X_op == O_add
			  && ((symbol_get_value_expression
			       (i.op[n].imms->X_op_symbol)->X_op)
			      == O_subtract))))
		{
		  offsetT add;

		  if (insn_start_frag == frag_now)
		    add = (p - frag_now->fr_literal) - insn_start_off;
		  else
		    {
		      fragS *fr;

		      add = insn_start_frag->fr_fix - insn_start_off;
		      for (fr = insn_start_frag->fr_next;
			   fr && fr != frag_now; fr = fr->fr_next)
			add += fr->fr_fix;
		      add += p - frag_now->fr_literal;
		    }

		  if (!object_64bit)
		    reloc_type = BFD_RELOC_386_GOTPC;
		  else if (size == 4)
		    reloc_type = BFD_RELOC_X86_64_GOTPC32;
		  else if (size == 8)
		    reloc_type = BFD_RELOC_X86_64_GOTPC64;
		  i.op[n].imms->X_add_number += add;
		}
	      fix_new_exp (frag_now, p - frag_now->fr_literal, size,
			   i.op[n].imms, 0, reloc_type);
	    }
	}
    }
}

/* x86_cons_fix_new is called via the expression parsing code when a
   reloc is needed.  We use this hook to get the correct .got reloc.  */
static enum bfd_reloc_code_real got_reloc = NO_RELOC;
static int cons_sign = -1;

void
x86_cons_fix_new (fragS *frag, unsigned int off, unsigned int len,
		  expressionS *exp)
{
  enum bfd_reloc_code_real r = reloc (len, 0, cons_sign, got_reloc);

  got_reloc = NO_RELOC;

#ifdef TE_PE
  if (exp->X_op == O_secrel)
    {
      exp->X_op = O_symbol;
      r = BFD_RELOC_32_SECREL;
    }
#endif

  fix_new_exp (frag, off, len, exp, 0, r);
}

#if (!defined (OBJ_ELF) && !defined (OBJ_MAYBE_ELF)) || defined (LEX_AT)
# define lex_got(reloc, adjust, types) NULL
#else
/* Parse operands of the form
   <symbol>@GOTOFF+<nnn>
   and similar .plt or .got references.

   If we find one, set up the correct relocation in RELOC and copy the
   input string, minus the `@GOTOFF' into a malloc'd buffer for
   parsing by the calling routine.  Return this buffer, and if ADJUST
   is non-null set it to the length of the string we removed from the
   input line.  Otherwise return NULL.  */
static char *
lex_got (enum bfd_reloc_code_real *reloc,
	 int *adjust,
	 unsigned int *types)
{
  /* Some of the relocations depend on the size of what field is to
     be relocated.  But in our callers i386_immediate and i386_displacement
     we don't yet know the operand size (this will be set by insn
     matching).  Hence we record the word32 relocation here,
     and adjust the reloc according to the real size in reloc().  */
  static const struct {
    const char *str;
    const enum bfd_reloc_code_real rel[2];
    const unsigned int types64;
  } gotrel[] = {
    { "PLTOFF",   { 0,
		    BFD_RELOC_X86_64_PLTOFF64 },
      Imm64 },
    { "PLT",      { BFD_RELOC_386_PLT32,
		    BFD_RELOC_X86_64_PLT32    },
      Imm32 | Imm32S | Disp32 },
    { "GOTPLT",   { 0,
		    BFD_RELOC_X86_64_GOTPLT64 },
      Imm64 | Disp64 },
    { "GOTOFF",   { BFD_RELOC_386_GOTOFF,
		    BFD_RELOC_X86_64_GOTOFF64 },
      Imm64 | Disp64 },
    { "GOTPCREL", { 0,
		    BFD_RELOC_X86_64_GOTPCREL },
      Imm32 | Imm32S | Disp32 },
    { "TLSGD",    { BFD_RELOC_386_TLS_GD,
		    BFD_RELOC_X86_64_TLSGD    },
      Imm32 | Imm32S | Disp32 },
    { "TLSLDM",   { BFD_RELOC_386_TLS_LDM,
		    0                         },
      0 },
    { "TLSLD",    { 0,
		    BFD_RELOC_X86_64_TLSLD    },
      Imm32 | Imm32S | Disp32 },
    { "GOTTPOFF", { BFD_RELOC_386_TLS_IE_32,
		    BFD_RELOC_X86_64_GOTTPOFF },
      Imm32 | Imm32S | Disp32 },
    { "TPOFF",    { BFD_RELOC_386_TLS_LE_32,
		    BFD_RELOC_X86_64_TPOFF32  },
      Imm32 | Imm32S | Imm64 | Disp32 | Disp64 },
    { "NTPOFF",   { BFD_RELOC_386_TLS_LE,
		    0                         },
      0 },
    { "DTPOFF",   { BFD_RELOC_386_TLS_LDO_32,
		    BFD_RELOC_X86_64_DTPOFF32 },
      Imm32 | Imm32S | Imm64 | Disp32 | Disp64 },
    { "GOTNTPOFF",{ BFD_RELOC_386_TLS_GOTIE,
		    0                         },
      0 },
    { "INDNTPOFF",{ BFD_RELOC_386_TLS_IE,
		    0                         },
      0 },
    { "GOT",      { BFD_RELOC_386_GOT32,
		    BFD_RELOC_X86_64_GOT32    },
      Imm32 | Imm32S | Disp32 | Imm64 },
    { "TLSDESC",  { BFD_RELOC_386_TLS_GOTDESC,
		    BFD_RELOC_X86_64_GOTPC32_TLSDESC },
      Imm32 | Imm32S | Disp32 },
    { "TLSCALL",  { BFD_RELOC_386_TLS_DESC_CALL,
		    BFD_RELOC_X86_64_TLSDESC_CALL },
      Imm32 | Imm32S | Disp32 }
  };
  char *cp;
  unsigned int j;

  if (!IS_ELF)
    return NULL;

  for (cp = input_line_pointer; *cp != '@'; cp++)
    if (is_end_of_line[(unsigned char) *cp])
      return NULL;

  for (j = 0; j < sizeof (gotrel) / sizeof (gotrel[0]); j++)
    {
      int len;

      len = strlen (gotrel[j].str);
      if (strncasecmp (cp + 1, gotrel[j].str, len) == 0)
	{
	  if (gotrel[j].rel[object_64bit] != 0)
	    {
	      int first, second;
	      char *tmpbuf, *past_reloc;

	      *reloc = gotrel[j].rel[object_64bit];
	      if (adjust)
		*adjust = len;

	      if (types)
		{
		  if (flag_code != CODE_64BIT)
		    *types = Imm32 | Disp32;
		  else
		    *types = gotrel[j].types64;
		}

	      if (GOT_symbol == NULL)
		GOT_symbol = symbol_find_or_make (GLOBAL_OFFSET_TABLE_NAME);

	      /* The length of the first part of our input line.  */
	      first = cp - input_line_pointer;

	      /* The second part goes from after the reloc token until
		 (and including) an end_of_line char.  Don't use strlen
		 here as the end_of_line char may not be a NUL.  */
	      past_reloc = cp + 1 + len;
	      for (cp = past_reloc; !is_end_of_line[(unsigned char) *cp++]; )
		;
	      second = cp - past_reloc;

	      /* Allocate and copy string.  The trailing NUL shouldn't
		 be necessary, but be safe.  */
	      tmpbuf = xmalloc (first + second + 2);
	      memcpy (tmpbuf, input_line_pointer, first);
	      if (second != 0 && *past_reloc != ' ')
		/* Replace the relocation token with ' ', so that
		   errors like foo@GOTOFF1 will be detected.  */
		tmpbuf[first++] = ' ';
	      memcpy (tmpbuf + first, past_reloc, second);
	      tmpbuf[first + second] = '\0';
	      return tmpbuf;
	    }

	  as_bad (_("@%s reloc is not supported with %d-bit output format"),
		  gotrel[j].str, 1 << (5 + object_64bit));
	  return NULL;
	}
    }

  /* Might be a symbol version string.  Don't as_bad here.  */
  return NULL;
}

void
x86_cons (expressionS *exp, int size)
{
  if (size == 4 || (object_64bit && size == 8))
    {
      /* Handle @GOTOFF and the like in an expression.  */
      char *save;
      char *gotfree_input_line;
      int adjust;

      save = input_line_pointer;
      gotfree_input_line = lex_got (&got_reloc, &adjust, NULL);
      if (gotfree_input_line)
	input_line_pointer = gotfree_input_line;

      expression (exp);

      if (gotfree_input_line)
	{
	  /* expression () has merrily parsed up to the end of line,
	     or a comma - in the wrong buffer.  Transfer how far
	     input_line_pointer has moved to the right buffer.  */
	  input_line_pointer = (save
				+ (input_line_pointer - gotfree_input_line)
				+ adjust);
	  free (gotfree_input_line);
	}
    }
  else
    expression (exp);
}
#endif

static void signed_cons (int size)
{
  if (flag_code == CODE_64BIT)
    cons_sign = 1;
  cons (size);
  cons_sign = -1;
}

#ifdef TE_PE
static void
pe_directive_secrel (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS exp;

  do
    {
      expression (&exp);
      if (exp.X_op == O_symbol)
	exp.X_op = O_secrel;

      emit_expr (&exp, 4);
    }
  while (*input_line_pointer++ == ',');

  input_line_pointer--;
  demand_empty_rest_of_line ();
}
#endif

static int
i386_immediate (char *imm_start)
{
  char *save_input_line_pointer;
  char *gotfree_input_line;
  segT exp_seg = 0;
  expressionS *exp;
  unsigned int types = ~0U;

  if (i.imm_operands == MAX_IMMEDIATE_OPERANDS)
    {
      as_bad (_("at most %d immediate operands are allowed"),
	      MAX_IMMEDIATE_OPERANDS);
      return 0;
    }

  exp = &im_expressions[i.imm_operands++];
  i.op[this_operand].imms = exp;

  if (is_space_char (*imm_start))
    ++imm_start;

  save_input_line_pointer = input_line_pointer;
  input_line_pointer = imm_start;

  gotfree_input_line = lex_got (&i.reloc[this_operand], NULL, &types);
  if (gotfree_input_line)
    input_line_pointer = gotfree_input_line;

  exp_seg = expression (exp);

  SKIP_WHITESPACE ();
  if (*input_line_pointer)
    as_bad (_("junk `%s' after expression"), input_line_pointer);

  input_line_pointer = save_input_line_pointer;
  if (gotfree_input_line)
    free (gotfree_input_line);

  if (exp->X_op == O_absent || exp->X_op == O_big)
    {
      /* Missing or bad expr becomes absolute 0.  */
      as_bad (_("missing or invalid immediate expression `%s' taken as 0"),
	      imm_start);
      exp->X_op = O_constant;
      exp->X_add_number = 0;
      exp->X_add_symbol = (symbolS *) 0;
      exp->X_op_symbol = (symbolS *) 0;
    }
  else if (exp->X_op == O_constant)
    {
      /* Size it properly later.  */
      i.types[this_operand] |= Imm64;
      /* If BFD64, sign extend val.  */
      if (!use_rela_relocations
	  && (exp->X_add_number & ~(((addressT) 2 << 31) - 1)) == 0)
	exp->X_add_number
	  = (exp->X_add_number ^ ((addressT) 1 << 31)) - ((addressT) 1 << 31);
    }
#if (defined (OBJ_AOUT) || defined (OBJ_MAYBE_AOUT))
  else if (OUTPUT_FLAVOR == bfd_target_aout_flavour
	   && exp_seg != absolute_section
	   && exp_seg != text_section
	   && exp_seg != data_section
	   && exp_seg != bss_section
	   && exp_seg != undefined_section
	   && !bfd_is_com_section (exp_seg))
    {
      as_bad (_("unimplemented segment %s in operand"), exp_seg->name);
      return 0;
    }
#endif
  else if (!intel_syntax && exp->X_op == O_register)
    {
      as_bad (_("illegal immediate register operand %s"), imm_start);
      return 0;
    }
  else
    {
      /* This is an address.  The size of the address will be
	 determined later, depending on destination register,
	 suffix, or the default for the section.  */
      i.types[this_operand] |= Imm8 | Imm16 | Imm32 | Imm32S | Imm64;
      i.types[this_operand] &= types;
    }

  return 1;
}

static char *
i386_scale (char *scale)
{
  offsetT val;
  char *save = input_line_pointer;

  input_line_pointer = scale;
  val = get_absolute_expression ();

  switch (val)
    {
    case 1:
      i.log2_scale_factor = 0;
      break;
    case 2:
      i.log2_scale_factor = 1;
      break;
    case 4:
      i.log2_scale_factor = 2;
      break;
    case 8:
      i.log2_scale_factor = 3;
      break;
    default:
      {
	char sep = *input_line_pointer;

	*input_line_pointer = '\0';
	as_bad (_("expecting scale factor of 1, 2, 4, or 8: got `%s'"),
		scale);
	*input_line_pointer = sep;
	input_line_pointer = save;
	return NULL;
      }
    }
  if (i.log2_scale_factor != 0 && i.index_reg == 0)
    {
      as_warn (_("scale factor of %d without an index register"),
	       1 << i.log2_scale_factor);
#if SCALE1_WHEN_NO_INDEX
      i.log2_scale_factor = 0;
#endif
    }
  scale = input_line_pointer;
  input_line_pointer = save;
  return scale;
}

static int
i386_displacement (char *disp_start, char *disp_end)
{
  expressionS *exp;
  segT exp_seg = 0;
  char *save_input_line_pointer;
  char *gotfree_input_line;
  int bigdisp, override;
  unsigned int types = Disp;

  if (i.disp_operands == MAX_MEMORY_OPERANDS)
    {
      as_bad (_("at most %d displacement operands are allowed"),
	      MAX_MEMORY_OPERANDS);
      return 0;
    }

  if ((i.types[this_operand] & JumpAbsolute)
      || !(current_templates->start->opcode_modifier & (Jump | JumpDword)))
    {
      bigdisp = Disp32;
      override = (i.prefix[ADDR_PREFIX] != 0);
    }
  else
    {
      /* For PC-relative branches, the width of the displacement
	 is dependent upon data size, not address size.  */
      bigdisp = 0;
      override = (i.prefix[DATA_PREFIX] != 0);
    }
  if (flag_code == CODE_64BIT)
    {
      if (!bigdisp)
	bigdisp = ((override || i.suffix == WORD_MNEM_SUFFIX)
		   ? Disp16
		   : Disp32S | Disp32);
      else if (!override)
	bigdisp = Disp64 | Disp32S | Disp32;
    }
  else
    {
      if (!bigdisp)
	{
	  if (!override)
	    override = (i.suffix == (flag_code != CODE_16BIT
				     ? WORD_MNEM_SUFFIX
				     : LONG_MNEM_SUFFIX));
	  bigdisp = Disp32;
	}
      if ((flag_code == CODE_16BIT) ^ override)
	bigdisp = Disp16;
    }
  i.types[this_operand] |= bigdisp;

  exp = &disp_expressions[i.disp_operands];
  i.op[this_operand].disps = exp;
  i.disp_operands++;
  save_input_line_pointer = input_line_pointer;
  input_line_pointer = disp_start;
  END_STRING_AND_SAVE (disp_end);

#ifndef GCC_ASM_O_HACK
#define GCC_ASM_O_HACK 0
#endif
#if GCC_ASM_O_HACK
  END_STRING_AND_SAVE (disp_end + 1);
  if ((i.types[this_operand] & BaseIndex) != 0
      && displacement_string_end[-1] == '+')
    {
      /* This hack is to avoid a warning when using the "o"
	 constraint within gcc asm statements.
	 For instance:

	 #define _set_tssldt_desc(n,addr,limit,type) \
	 __asm__ __volatile__ ( \
	 "movw %w2,%0\n\t" \
	 "movw %w1,2+%0\n\t" \
	 "rorl $16,%1\n\t" \
	 "movb %b1,4+%0\n\t" \
	 "movb %4,5+%0\n\t" \
	 "movb $0,6+%0\n\t" \
	 "movb %h1,7+%0\n\t" \
	 "rorl $16,%1" \
	 : "=o"(*(n)) : "q" (addr), "ri"(limit), "i"(type))

	 This works great except that the output assembler ends
	 up looking a bit weird if it turns out that there is
	 no offset.  You end up producing code that looks like:

	 #APP
	 movw $235,(%eax)
	 movw %dx,2+(%eax)
	 rorl $16,%edx
	 movb %dl,4+(%eax)
	 movb $137,5+(%eax)
	 movb $0,6+(%eax)
	 movb %dh,7+(%eax)
	 rorl $16,%edx
	 #NO_APP

	 So here we provide the missing zero.  */

      *displacement_string_end = '0';
    }
#endif
  gotfree_input_line = lex_got (&i.reloc[this_operand], NULL, &types);
  if (gotfree_input_line)
    input_line_pointer = gotfree_input_line;

  exp_seg = expression (exp);

  SKIP_WHITESPACE ();
  if (*input_line_pointer)
    as_bad (_("junk `%s' after expression"), input_line_pointer);
#if GCC_ASM_O_HACK
  RESTORE_END_STRING (disp_end + 1);
#endif
  RESTORE_END_STRING (disp_end);
  input_line_pointer = save_input_line_pointer;
  if (gotfree_input_line)
    free (gotfree_input_line);

  /* We do this to make sure that the section symbol is in
     the symbol table.  We will ultimately change the relocation
     to be relative to the beginning of the section.  */
  if (i.reloc[this_operand] == BFD_RELOC_386_GOTOFF
      || i.reloc[this_operand] == BFD_RELOC_X86_64_GOTPCREL
      || i.reloc[this_operand] == BFD_RELOC_X86_64_GOTOFF64)
    {
      if (exp->X_op != O_symbol)
	{
	  as_bad (_("bad expression used with @%s"),
		  (i.reloc[this_operand] == BFD_RELOC_X86_64_GOTPCREL
		   ? "GOTPCREL"
		   : "GOTOFF"));
	  return 0;
	}

      if (S_IS_LOCAL (exp->X_add_symbol)
	  && S_GET_SEGMENT (exp->X_add_symbol) != undefined_section)
	section_symbol (S_GET_SEGMENT (exp->X_add_symbol));
      exp->X_op = O_subtract;
      exp->X_op_symbol = GOT_symbol;
      if (i.reloc[this_operand] == BFD_RELOC_X86_64_GOTPCREL)
	i.reloc[this_operand] = BFD_RELOC_32_PCREL;
      else if (i.reloc[this_operand] == BFD_RELOC_X86_64_GOTOFF64)
	i.reloc[this_operand] = BFD_RELOC_64;
      else
	i.reloc[this_operand] = BFD_RELOC_32;
    }

  if (exp->X_op == O_absent || exp->X_op == O_big)
    {
      /* Missing or bad expr becomes absolute 0.  */
      as_bad (_("missing or invalid displacement expression `%s' taken as 0"),
	      disp_start);
      exp->X_op = O_constant;
      exp->X_add_number = 0;
      exp->X_add_symbol = (symbolS *) 0;
      exp->X_op_symbol = (symbolS *) 0;
    }

#if (defined (OBJ_AOUT) || defined (OBJ_MAYBE_AOUT))
  if (exp->X_op != O_constant
      && OUTPUT_FLAVOR == bfd_target_aout_flavour
      && exp_seg != absolute_section
      && exp_seg != text_section
      && exp_seg != data_section
      && exp_seg != bss_section
      && exp_seg != undefined_section
      && !bfd_is_com_section (exp_seg))
    {
      as_bad (_("unimplemented segment %s in operand"), exp_seg->name);
      return 0;
    }
#endif

  if (!(i.types[this_operand] & ~Disp))
    i.types[this_operand] &= types;

  return 1;
}

/* Make sure the memory operand we've been dealt is valid.
   Return 1 on success, 0 on a failure.  */

static int
i386_index_check (const char *operand_string)
{
  int ok;
#if INFER_ADDR_PREFIX
  int fudged = 0;

 tryprefix:
#endif
  ok = 1;
  if ((current_templates->start->cpu_flags & CpuSVME)
      && current_templates->end[-1].operand_types[0] == AnyMem)
    {
      /* Memory operands of SVME insns are special in that they only allow
	 rAX as their memory address and ignore any segment override.  */
      unsigned RegXX;

      /* SKINIT is even more restrictive: it always requires EAX.  */
      if (strcmp (current_templates->start->name, "skinit") == 0)
	RegXX = Reg32;
      else if (flag_code == CODE_64BIT)
	RegXX = i.prefix[ADDR_PREFIX] == 0 ? Reg64 : Reg32;
      else
	RegXX = ((flag_code == CODE_16BIT) ^ (i.prefix[ADDR_PREFIX] != 0)
		 ? Reg16
		 : Reg32);
      if (!i.base_reg
	  || !(i.base_reg->reg_type & Acc)
	  || !(i.base_reg->reg_type & RegXX)
	  || i.index_reg
	  || (i.types[0] & Disp))
	ok = 0;
    }
  else if (flag_code == CODE_64BIT)
    {
      unsigned RegXX = (i.prefix[ADDR_PREFIX] == 0 ? Reg64 : Reg32);

      if ((i.base_reg
	   && ((i.base_reg->reg_type & RegXX) == 0)
	   && (i.base_reg->reg_type != BaseIndex
	       || i.index_reg))
	  || (i.index_reg
	      && ((i.index_reg->reg_type & (RegXX | BaseIndex))
		  != (RegXX | BaseIndex))))
	ok = 0;
    }
  else
    {
      if ((flag_code == CODE_16BIT) ^ (i.prefix[ADDR_PREFIX] != 0))
	{
	  /* 16bit checks.  */
	  if ((i.base_reg
	       && ((i.base_reg->reg_type & (Reg16 | BaseIndex | RegRex))
		   != (Reg16 | BaseIndex)))
	      || (i.index_reg
		  && (((i.index_reg->reg_type & (Reg16 | BaseIndex))
		       != (Reg16 | BaseIndex))
		      || !(i.base_reg
			   && i.base_reg->reg_num < 6
			   && i.index_reg->reg_num >= 6
			   && i.log2_scale_factor == 0))))
	    ok = 0;
	}
      else
	{
	  /* 32bit checks.  */
	  if ((i.base_reg
	       && (i.base_reg->reg_type & (Reg32 | RegRex)) != Reg32)
	      || (i.index_reg
		  && ((i.index_reg->reg_type & (Reg32 | BaseIndex | RegRex))
		      != (Reg32 | BaseIndex))))
	    ok = 0;
	}
    }
  if (!ok)
    {
#if INFER_ADDR_PREFIX
      if (i.prefix[ADDR_PREFIX] == 0)
	{
	  i.prefix[ADDR_PREFIX] = ADDR_PREFIX_OPCODE;
	  i.prefixes += 1;
	  /* Change the size of any displacement too.  At most one of
	     Disp16 or Disp32 is set.
	     FIXME.  There doesn't seem to be any real need for separate
	     Disp16 and Disp32 flags.  The same goes for Imm16 and Imm32.
	     Removing them would probably clean up the code quite a lot.  */
	  if (flag_code != CODE_64BIT
	      && (i.types[this_operand] & (Disp16 | Disp32)))
	    i.types[this_operand] ^= (Disp16 | Disp32);
	  fudged = 1;
	  goto tryprefix;
	}
      if (fudged)
	as_bad (_("`%s' is not a valid base/index expression"),
		operand_string);
      else
#endif
	as_bad (_("`%s' is not a valid %s bit base/index expression"),
		operand_string,
		flag_code_names[flag_code]);
    }
  return ok;
}

/* Parse OPERAND_STRING into the i386_insn structure I.  Returns non-zero
   on error.  */

static int
i386_operand (char *operand_string)
{
  const reg_entry *r;
  char *end_op;
  char *op_string = operand_string;

  if (is_space_char (*op_string))
    ++op_string;

  /* We check for an absolute prefix (differentiating,
     for example, 'jmp pc_relative_label' from 'jmp *absolute_label'.  */
  if (*op_string == ABSOLUTE_PREFIX)
    {
      ++op_string;
      if (is_space_char (*op_string))
	++op_string;
      i.types[this_operand] |= JumpAbsolute;
    }

  /* Check if operand is a register.  */
  if ((r = parse_register (op_string, &end_op)) != NULL)
    {
      /* Check for a segment override by searching for ':' after a
	 segment register.  */
      op_string = end_op;
      if (is_space_char (*op_string))
	++op_string;
      if (*op_string == ':' && (r->reg_type & (SReg2 | SReg3)))
	{
	  switch (r->reg_num)
	    {
	    case 0:
	      i.seg[i.mem_operands] = &es;
	      break;
	    case 1:
	      i.seg[i.mem_operands] = &cs;
	      break;
	    case 2:
	      i.seg[i.mem_operands] = &ss;
	      break;
	    case 3:
	      i.seg[i.mem_operands] = &ds;
	      break;
	    case 4:
	      i.seg[i.mem_operands] = &fs;
	      break;
	    case 5:
	      i.seg[i.mem_operands] = &gs;
	      break;
	    }

	  /* Skip the ':' and whitespace.  */
	  ++op_string;
	  if (is_space_char (*op_string))
	    ++op_string;

	  if (!is_digit_char (*op_string)
	      && !is_identifier_char (*op_string)
	      && *op_string != '('
	      && *op_string != ABSOLUTE_PREFIX)
	    {
	      as_bad (_("bad memory operand `%s'"), op_string);
	      return 0;
	    }
	  /* Handle case of %es:*foo.  */
	  if (*op_string == ABSOLUTE_PREFIX)
	    {
	      ++op_string;
	      if (is_space_char (*op_string))
		++op_string;
	      i.types[this_operand] |= JumpAbsolute;
	    }
	  goto do_memory_reference;
	}
      if (*op_string)
	{
	  as_bad (_("junk `%s' after register"), op_string);
	  return 0;
	}
      i.types[this_operand] |= r->reg_type & ~BaseIndex;
      i.op[this_operand].regs = r;
      i.reg_operands++;
    }
  else if (*op_string == REGISTER_PREFIX)
    {
      as_bad (_("bad register name `%s'"), op_string);
      return 0;
    }
  else if (*op_string == IMMEDIATE_PREFIX)
    {
      ++op_string;
      if (i.types[this_operand] & JumpAbsolute)
	{
	  as_bad (_("immediate operand illegal with absolute jump"));
	  return 0;
	}
      if (!i386_immediate (op_string))
	return 0;
    }
  else if (is_digit_char (*op_string)
	   || is_identifier_char (*op_string)
	   || *op_string == '(')
    {
      /* This is a memory reference of some sort.  */
      char *base_string;

      /* Start and end of displacement string expression (if found).  */
      char *displacement_string_start;
      char *displacement_string_end;

    do_memory_reference:
      if ((i.mem_operands == 1
	   && (current_templates->start->opcode_modifier & IsString) == 0)
	  || i.mem_operands == 2)
	{
	  as_bad (_("too many memory references for `%s'"),
		  current_templates->start->name);
	  return 0;
	}

      /* Check for base index form.  We detect the base index form by
	 looking for an ')' at the end of the operand, searching
	 for the '(' matching it, and finding a REGISTER_PREFIX or ','
	 after the '('.  */
      base_string = op_string + strlen (op_string);

      --base_string;
      if (is_space_char (*base_string))
	--base_string;

      /* If we only have a displacement, set-up for it to be parsed later.  */
      displacement_string_start = op_string;
      displacement_string_end = base_string + 1;

      if (*base_string == ')')
	{
	  char *temp_string;
	  unsigned int parens_balanced = 1;
	  /* We've already checked that the number of left & right ()'s are
	     equal, so this loop will not be infinite.  */
	  do
	    {
	      base_string--;
	      if (*base_string == ')')
		parens_balanced++;
	      if (*base_string == '(')
		parens_balanced--;
	    }
	  while (parens_balanced);

	  temp_string = base_string;

	  /* Skip past '(' and whitespace.  */
	  ++base_string;
	  if (is_space_char (*base_string))
	    ++base_string;

	  if (*base_string == ','
	      || ((i.base_reg = parse_register (base_string, &end_op))
		  != NULL))
	    {
	      displacement_string_end = temp_string;

	      i.types[this_operand] |= BaseIndex;

	      if (i.base_reg)
		{
		  base_string = end_op;
		  if (is_space_char (*base_string))
		    ++base_string;
		}

	      /* There may be an index reg or scale factor here.  */
	      if (*base_string == ',')
		{
		  ++base_string;
		  if (is_space_char (*base_string))
		    ++base_string;

		  if ((i.index_reg = parse_register (base_string, &end_op))
		      != NULL)
		    {
		      base_string = end_op;
		      if (is_space_char (*base_string))
			++base_string;
		      if (*base_string == ',')
			{
			  ++base_string;
			  if (is_space_char (*base_string))
			    ++base_string;
			}
		      else if (*base_string != ')')
			{
			  as_bad (_("expecting `,' or `)' "
				    "after index register in `%s'"),
				  operand_string);
			  return 0;
			}
		    }
		  else if (*base_string == REGISTER_PREFIX)
		    {
		      as_bad (_("bad register name `%s'"), base_string);
		      return 0;
		    }

		  /* Check for scale factor.  */
		  if (*base_string != ')')
		    {
		      char *end_scale = i386_scale (base_string);

		      if (!end_scale)
			return 0;

		      base_string = end_scale;
		      if (is_space_char (*base_string))
			++base_string;
		      if (*base_string != ')')
			{
			  as_bad (_("expecting `)' "
				    "after scale factor in `%s'"),
				  operand_string);
			  return 0;
			}
		    }
		  else if (!i.index_reg)
		    {
		      as_bad (_("expecting index register or scale factor "
				"after `,'; got '%c'"),
			      *base_string);
		      return 0;
		    }
		}
	      else if (*base_string != ')')
		{
		  as_bad (_("expecting `,' or `)' "
			    "after base register in `%s'"),
			  operand_string);
		  return 0;
		}
	    }
	  else if (*base_string == REGISTER_PREFIX)
	    {
	      as_bad (_("bad register name `%s'"), base_string);
	      return 0;
	    }
	}

      /* If there's an expression beginning the operand, parse it,
	 assuming displacement_string_start and
	 displacement_string_end are meaningful.  */
      if (displacement_string_start != displacement_string_end)
	{
	  if (!i386_displacement (displacement_string_start,
				  displacement_string_end))
	    return 0;
	}

      /* Special case for (%dx) while doing input/output op.  */
      if (i.base_reg
	  && i.base_reg->reg_type == (Reg16 | InOutPortReg)
	  && i.index_reg == 0
	  && i.log2_scale_factor == 0
	  && i.seg[i.mem_operands] == 0
	  && (i.types[this_operand] & Disp) == 0)
	{
	  i.types[this_operand] = InOutPortReg;
	  return 1;
	}

      if (i386_index_check (operand_string) == 0)
	return 0;
      i.mem_operands++;
    }
  else
    {
      /* It's not a memory operand; argh!  */
      as_bad (_("invalid char %s beginning operand %d `%s'"),
	      output_invalid (*op_string),
	      this_operand + 1,
	      op_string);
      return 0;
    }
  return 1;			/* Normal return.  */
}

/* md_estimate_size_before_relax()

   Called just before relax() for rs_machine_dependent frags.  The x86
   assembler uses these frags to handle variable size jump
   instructions.

   Any symbol that is now undefined will not become defined.
   Return the correct fr_subtype in the frag.
   Return the initial "guess for variable size of frag" to caller.
   The guess is actually the growth beyond the fixed part.  Whatever
   we do to grow the fixed or variable part contributes to our
   returned value.  */

int
md_estimate_size_before_relax (fragP, segment)
     fragS *fragP;
     segT segment;
{
  /* We've already got fragP->fr_subtype right;  all we have to do is
     check for un-relaxable symbols.  On an ELF system, we can't relax
     an externally visible symbol, because it may be overridden by a
     shared library.  */
  if (S_GET_SEGMENT (fragP->fr_symbol) != segment
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
      || (IS_ELF
	  && (S_IS_EXTERNAL (fragP->fr_symbol)
	      || S_IS_WEAK (fragP->fr_symbol)))
#endif
      )
    {
      /* Symbol is undefined in this segment, or we need to keep a
	 reloc so that weak symbols can be overridden.  */
      int size = (fragP->fr_subtype & CODE16) ? 2 : 4;
      enum bfd_reloc_code_real reloc_type;
      unsigned char *opcode;
      int old_fr_fix;

      if (fragP->fr_var != NO_RELOC)
	reloc_type = fragP->fr_var;
      else if (size == 2)
	reloc_type = BFD_RELOC_16_PCREL;
      else
	reloc_type = BFD_RELOC_32_PCREL;

      old_fr_fix = fragP->fr_fix;
      opcode = (unsigned char *) fragP->fr_opcode;

      switch (TYPE_FROM_RELAX_STATE (fragP->fr_subtype))
	{
	case UNCOND_JUMP:
	  /* Make jmp (0xeb) a (d)word displacement jump.  */
	  opcode[0] = 0xe9;
	  fragP->fr_fix += size;
	  fix_new (fragP, old_fr_fix, size,
		   fragP->fr_symbol,
		   fragP->fr_offset, 1,
		   reloc_type);
	  break;

	case COND_JUMP86:
	  if (size == 2
	      && (!no_cond_jump_promotion || fragP->fr_var != NO_RELOC))
	    {
	      /* Negate the condition, and branch past an
		 unconditional jump.  */
	      opcode[0] ^= 1;
	      opcode[1] = 3;
	      /* Insert an unconditional jump.  */
	      opcode[2] = 0xe9;
	      /* We added two extra opcode bytes, and have a two byte
		 offset.  */
	      fragP->fr_fix += 2 + 2;
	      fix_new (fragP, old_fr_fix + 2, 2,
		       fragP->fr_symbol,
		       fragP->fr_offset, 1,
		       reloc_type);
	      break;
	    }
	  /* Fall through.  */

	case COND_JUMP:
	  if (no_cond_jump_promotion && fragP->fr_var == NO_RELOC)
	    {
	      fixS *fixP;

	      fragP->fr_fix += 1;
	      fixP = fix_new (fragP, old_fr_fix, 1,
			      fragP->fr_symbol,
			      fragP->fr_offset, 1,
			      BFD_RELOC_8_PCREL);
	      fixP->fx_signed = 1;
	      break;
	    }

	  /* This changes the byte-displacement jump 0x7N
	     to the (d)word-displacement jump 0x0f,0x8N.  */
	  opcode[1] = opcode[0] + 0x10;
	  opcode[0] = TWO_BYTE_OPCODE_ESCAPE;
	  /* We've added an opcode byte.  */
	  fragP->fr_fix += 1 + size;
	  fix_new (fragP, old_fr_fix + 1, size,
		   fragP->fr_symbol,
		   fragP->fr_offset, 1,
		   reloc_type);
	  break;

	default:
	  BAD_CASE (fragP->fr_subtype);
	  break;
	}
      frag_wane (fragP);
      return fragP->fr_fix - old_fr_fix;
    }

  /* Guess size depending on current relax state.  Initially the relax
     state will correspond to a short jump and we return 1, because
     the variable part of the frag (the branch offset) is one byte
     long.  However, we can relax a section more than once and in that
     case we must either set fr_subtype back to the unrelaxed state,
     or return the value for the appropriate branch.  */
  return md_relax_table[fragP->fr_subtype].rlx_length;
}

/* Called after relax() is finished.

   In:	Address of frag.
	fr_type == rs_machine_dependent.
	fr_subtype is what the address relaxed to.

   Out:	Any fixSs and constants are set up.
	Caller will turn frag into a ".space 0".  */

void
md_convert_frag (abfd, sec, fragP)
     bfd *abfd ATTRIBUTE_UNUSED;
     segT sec ATTRIBUTE_UNUSED;
     fragS *fragP;
{
  unsigned char *opcode;
  unsigned char *where_to_put_displacement = NULL;
  offsetT target_address;
  offsetT opcode_address;
  unsigned int extension = 0;
  offsetT displacement_from_opcode_start;

  opcode = (unsigned char *) fragP->fr_opcode;

  /* Address we want to reach in file space.  */
  target_address = S_GET_VALUE (fragP->fr_symbol) + fragP->fr_offset;

  /* Address opcode resides at in file space.  */
  opcode_address = fragP->fr_address + fragP->fr_fix;

  /* Displacement from opcode start to fill into instruction.  */
  displacement_from_opcode_start = target_address - opcode_address;

  if ((fragP->fr_subtype & BIG) == 0)
    {
      /* Don't have to change opcode.  */
      extension = 1;		/* 1 opcode + 1 displacement  */
      where_to_put_displacement = &opcode[1];
    }
  else
    {
      if (no_cond_jump_promotion
	  && TYPE_FROM_RELAX_STATE (fragP->fr_subtype) != UNCOND_JUMP)
	as_warn_where (fragP->fr_file, fragP->fr_line,
		       _("long jump required"));

      switch (fragP->fr_subtype)
	{
	case ENCODE_RELAX_STATE (UNCOND_JUMP, BIG):
	  extension = 4;		/* 1 opcode + 4 displacement  */
	  opcode[0] = 0xe9;
	  where_to_put_displacement = &opcode[1];
	  break;

	case ENCODE_RELAX_STATE (UNCOND_JUMP, BIG16):
	  extension = 2;		/* 1 opcode + 2 displacement  */
	  opcode[0] = 0xe9;
	  where_to_put_displacement = &opcode[1];
	  break;

	case ENCODE_RELAX_STATE (COND_JUMP, BIG):
	case ENCODE_RELAX_STATE (COND_JUMP86, BIG):
	  extension = 5;		/* 2 opcode + 4 displacement  */
	  opcode[1] = opcode[0] + 0x10;
	  opcode[0] = TWO_BYTE_OPCODE_ESCAPE;
	  where_to_put_displacement = &opcode[2];
	  break;

	case ENCODE_RELAX_STATE (COND_JUMP, BIG16):
	  extension = 3;		/* 2 opcode + 2 displacement  */
	  opcode[1] = opcode[0] + 0x10;
	  opcode[0] = TWO_BYTE_OPCODE_ESCAPE;
	  where_to_put_displacement = &opcode[2];
	  break;

	case ENCODE_RELAX_STATE (COND_JUMP86, BIG16):
	  extension = 4;
	  opcode[0] ^= 1;
	  opcode[1] = 3;
	  opcode[2] = 0xe9;
	  where_to_put_displacement = &opcode[3];
	  break;

	default:
	  BAD_CASE (fragP->fr_subtype);
	  break;
	}
    }

  /* If size if less then four we are sure that the operand fits,
     but if it's 4, then it could be that the displacement is larger
     then -/+ 2GB.  */
  if (DISP_SIZE_FROM_RELAX_STATE (fragP->fr_subtype) == 4
      && object_64bit
      && ((addressT) (displacement_from_opcode_start - extension
		      + ((addressT) 1 << 31))
	  > (((addressT) 2 << 31) - 1)))
    {
      as_bad_where (fragP->fr_file, fragP->fr_line,
		    _("jump target out of range"));
      /* Make us emit 0.  */
      displacement_from_opcode_start = extension;
    }
  /* Now put displacement after opcode.  */
  md_number_to_chars ((char *) where_to_put_displacement,
		      (valueT) (displacement_from_opcode_start - extension),
		      DISP_SIZE_FROM_RELAX_STATE (fragP->fr_subtype));
  fragP->fr_fix += extension;
}

/* Size of byte displacement jmp.  */
int md_short_jump_size = 2;

/* Size of dword displacement jmp.  */
int md_long_jump_size = 5;

void
md_create_short_jump (ptr, from_addr, to_addr, frag, to_symbol)
     char *ptr;
     addressT from_addr, to_addr;
     fragS *frag ATTRIBUTE_UNUSED;
     symbolS *to_symbol ATTRIBUTE_UNUSED;
{
  offsetT offset;

  offset = to_addr - (from_addr + 2);
  /* Opcode for byte-disp jump.  */
  md_number_to_chars (ptr, (valueT) 0xeb, 1);
  md_number_to_chars (ptr + 1, (valueT) offset, 1);
}

void
md_create_long_jump (ptr, from_addr, to_addr, frag, to_symbol)
     char *ptr;
     addressT from_addr, to_addr;
     fragS *frag ATTRIBUTE_UNUSED;
     symbolS *to_symbol ATTRIBUTE_UNUSED;
{
  offsetT offset;

  offset = to_addr - (from_addr + 5);
  md_number_to_chars (ptr, (valueT) 0xe9, 1);
  md_number_to_chars (ptr + 1, (valueT) offset, 4);
}

/* Apply a fixup (fixS) to segment data, once it has been determined
   by our caller that we have all the info we need to fix it up.

   On the 386, immediates, displacements, and data pointers are all in
   the same (little-endian) format, so we don't need to care about which
   we are handling.  */

void
md_apply_fix (fixP, valP, seg)
     /* The fix we're to put in.  */
     fixS *fixP;
     /* Pointer to the value of the bits.  */
     valueT *valP;
     /* Segment fix is from.  */
     segT seg ATTRIBUTE_UNUSED;
{
  char *p = fixP->fx_where + fixP->fx_frag->fr_literal;
  valueT value = *valP;

#if !defined (TE_Mach)
  if (fixP->fx_pcrel)
    {
      switch (fixP->fx_r_type)
	{
	default:
	  break;

	case BFD_RELOC_64:
	  fixP->fx_r_type = BFD_RELOC_64_PCREL;
	  break;
	case BFD_RELOC_32:
	case BFD_RELOC_X86_64_32S:
	  fixP->fx_r_type = BFD_RELOC_32_PCREL;
	  break;
	case BFD_RELOC_16:
	  fixP->fx_r_type = BFD_RELOC_16_PCREL;
	  break;
	case BFD_RELOC_8:
	  fixP->fx_r_type = BFD_RELOC_8_PCREL;
	  break;
	}
    }

  if (fixP->fx_addsy != NULL
      && (fixP->fx_r_type == BFD_RELOC_32_PCREL
	  || fixP->fx_r_type == BFD_RELOC_64_PCREL
	  || fixP->fx_r_type == BFD_RELOC_16_PCREL
	  || fixP->fx_r_type == BFD_RELOC_8_PCREL)
      && !use_rela_relocations)
    {
      /* This is a hack.  There should be a better way to handle this.
	 This covers for the fact that bfd_install_relocation will
	 subtract the current location (for partial_inplace, PC relative
	 relocations); see more below.  */
#ifndef OBJ_AOUT
      if (IS_ELF
#ifdef TE_PE
	  || OUTPUT_FLAVOR == bfd_target_coff_flavour
#endif
	  )
	value += fixP->fx_where + fixP->fx_frag->fr_address;
#endif
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
      if (IS_ELF)
	{
	  segT sym_seg = S_GET_SEGMENT (fixP->fx_addsy);

	  if ((sym_seg == seg
	       || (symbol_section_p (fixP->fx_addsy)
		   && sym_seg != absolute_section))
	      && !generic_force_reloc (fixP))
	    {
	      /* Yes, we add the values in twice.  This is because
		 bfd_install_relocation subtracts them out again.  I think
		 bfd_install_relocation is broken, but I don't dare change
		 it.  FIXME.  */
	      value += fixP->fx_where + fixP->fx_frag->fr_address;
	    }
	}
#endif
#if defined (OBJ_COFF) && defined (TE_PE)
      /* For some reason, the PE format does not store a
	 section address offset for a PC relative symbol.  */
      if (S_GET_SEGMENT (fixP->fx_addsy) != seg
	  || S_IS_WEAK (fixP->fx_addsy))
	value += md_pcrel_from (fixP);
#endif
    }

  /* Fix a few things - the dynamic linker expects certain values here,
     and we must not disappoint it.  */
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
  if (IS_ELF && fixP->fx_addsy)
    switch (fixP->fx_r_type)
      {
      case BFD_RELOC_386_PLT32:
      case BFD_RELOC_X86_64_PLT32:
	/* Make the jump instruction point to the address of the operand.  At
	   runtime we merely add the offset to the actual PLT entry.  */
	value = -4;
	break;

      case BFD_RELOC_386_TLS_GD:
      case BFD_RELOC_386_TLS_LDM:
      case BFD_RELOC_386_TLS_IE_32:
      case BFD_RELOC_386_TLS_IE:
      case BFD_RELOC_386_TLS_GOTIE:
      case BFD_RELOC_386_TLS_GOTDESC:
      case BFD_RELOC_X86_64_TLSGD:
      case BFD_RELOC_X86_64_TLSLD:
      case BFD_RELOC_X86_64_GOTTPOFF:
      case BFD_RELOC_X86_64_GOTPC32_TLSDESC:
	value = 0; /* Fully resolved at runtime.  No addend.  */
	/* Fallthrough */
      case BFD_RELOC_386_TLS_LE:
      case BFD_RELOC_386_TLS_LDO_32:
      case BFD_RELOC_386_TLS_LE_32:
      case BFD_RELOC_X86_64_DTPOFF32:
      case BFD_RELOC_X86_64_DTPOFF64:
      case BFD_RELOC_X86_64_TPOFF32:
      case BFD_RELOC_X86_64_TPOFF64:
	S_SET_THREAD_LOCAL (fixP->fx_addsy);
	break;

      case BFD_RELOC_386_TLS_DESC_CALL:
      case BFD_RELOC_X86_64_TLSDESC_CALL:
	value = 0; /* Fully resolved at runtime.  No addend.  */
	S_SET_THREAD_LOCAL (fixP->fx_addsy);
	fixP->fx_done = 0;
	return;

      case BFD_RELOC_386_GOT32:
      case BFD_RELOC_X86_64_GOT32:
	value = 0; /* Fully resolved at runtime.  No addend.  */
	break;

      case BFD_RELOC_VTABLE_INHERIT:
      case BFD_RELOC_VTABLE_ENTRY:
	fixP->fx_done = 0;
	return;

      default:
	break;
      }
#endif /* defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)  */
  *valP = value;
#endif /* !defined (TE_Mach)  */

  /* Are we finished with this relocation now?  */
  if (fixP->fx_addsy == NULL)
    fixP->fx_done = 1;
  else if (use_rela_relocations)
    {
      fixP->fx_no_overflow = 1;
      /* Remember value for tc_gen_reloc.  */
      fixP->fx_addnumber = value;
      value = 0;
    }

  md_number_to_chars (p, value, fixP->fx_size);
}

#define MAX_LITTLENUMS 6

/* Turn the string pointed to by litP into a floating point constant
   of type TYPE, and emit the appropriate bytes.  The number of
   LITTLENUMS emitted is stored in *SIZEP.  An error message is
   returned, or NULL on OK.  */

char *
md_atof (type, litP, sizeP)
     int type;
     char *litP;
     int *sizeP;
{
  int prec;
  LITTLENUM_TYPE words[MAX_LITTLENUMS];
  LITTLENUM_TYPE *wordP;
  char *t;

  switch (type)
    {
    case 'f':
    case 'F':
      prec = 2;
      break;

    case 'd':
    case 'D':
      prec = 4;
      break;

    case 'x':
    case 'X':
      prec = 5;
      break;

    default:
      *sizeP = 0;
      return _("Bad call to md_atof ()");
    }
  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;

  *sizeP = prec * sizeof (LITTLENUM_TYPE);
  /* This loops outputs the LITTLENUMs in REVERSE order; in accord with
     the bigendian 386.  */
  for (wordP = words + prec - 1; prec--;)
    {
      md_number_to_chars (litP, (valueT) (*wordP--), sizeof (LITTLENUM_TYPE));
      litP += sizeof (LITTLENUM_TYPE);
    }
  return 0;
}

static char output_invalid_buf[sizeof (unsigned char) * 2 + 6];

static char *
output_invalid (int c)
{
  if (ISPRINT (c))
    snprintf (output_invalid_buf, sizeof (output_invalid_buf),
	      "'%c'", c);
  else
    snprintf (output_invalid_buf, sizeof (output_invalid_buf),
	      "(0x%x)", (unsigned char) c);
  return output_invalid_buf;
}

/* REG_STRING starts *before* REGISTER_PREFIX.  */

static const reg_entry *
parse_real_register (char *reg_string, char **end_op)
{
  char *s = reg_string;
  char *p;
  char reg_name_given[MAX_REG_NAME_SIZE + 1];
  const reg_entry *r;

  /* Skip possible REGISTER_PREFIX and possible whitespace.  */
  if (*s == REGISTER_PREFIX)
    ++s;

  if (is_space_char (*s))
    ++s;

  p = reg_name_given;
  while ((*p++ = register_chars[(unsigned char) *s]) != '\0')
    {
      if (p >= reg_name_given + MAX_REG_NAME_SIZE)
	return (const reg_entry *) NULL;
      s++;
    }

  /* For naked regs, make sure that we are not dealing with an identifier.
     This prevents confusing an identifier like `eax_var' with register
     `eax'.  */
  if (allow_naked_reg && identifier_chars[(unsigned char) *s])
    return (const reg_entry *) NULL;

  *end_op = s;

  r = (const reg_entry *) hash_find (reg_hash, reg_name_given);

  /* Handle floating point regs, allowing spaces in the (i) part.  */
  if (r == i386_regtab /* %st is first entry of table  */)
    {
      if (is_space_char (*s))
	++s;
      if (*s == '(')
	{
	  ++s;
	  if (is_space_char (*s))
	    ++s;
	  if (*s >= '0' && *s <= '7')
	    {
	      int fpr = *s - '0';
	      ++s;
	      if (is_space_char (*s))
		++s;
	      if (*s == ')')
		{
		  *end_op = s + 1;
		  r = hash_find (reg_hash, "st(0)");
		  know (r);
		  return r + fpr;
		}
	    }
	  /* We have "%st(" then garbage.  */
	  return (const reg_entry *) NULL;
	}
    }

  if (r != NULL
      && ((r->reg_flags & (RegRex64 | RegRex)) | (r->reg_type & Reg64)) != 0
      && (r->reg_type != Control || !(cpu_arch_flags & CpuSledgehammer))
      && flag_code != CODE_64BIT)
    return (const reg_entry *) NULL;

  return r;
}

/* REG_STRING starts *before* REGISTER_PREFIX.  */

static const reg_entry *
parse_register (char *reg_string, char **end_op)
{
  const reg_entry *r;

  if (*reg_string == REGISTER_PREFIX || allow_naked_reg)
    r = parse_real_register (reg_string, end_op);
  else
    r = NULL;
  if (!r)
    {
      char *save = input_line_pointer;
      char c;
      symbolS *symbolP;

      input_line_pointer = reg_string;
      c = get_symbol_end ();
      symbolP = symbol_find (reg_string);
      if (symbolP && S_GET_SEGMENT (symbolP) == reg_section)
	{
	  const expressionS *e = symbol_get_value_expression (symbolP);

	  know (e->X_op == O_register);
	  know (e->X_add_number >= 0
		&& (valueT) e->X_add_number < i386_regtab_size);
	  r = i386_regtab + e->X_add_number;
	  *end_op = input_line_pointer;
	}
      *input_line_pointer = c;
      input_line_pointer = save;
    }
  return r;
}

int
i386_parse_name (char *name, expressionS *e, char *nextcharP)
{
  const reg_entry *r;
  char *end = input_line_pointer;

  *end = *nextcharP;
  r = parse_register (name, &input_line_pointer);
  if (r && end <= input_line_pointer)
    {
      *nextcharP = *input_line_pointer;
      *input_line_pointer = 0;
      e->X_op = O_register;
      e->X_add_number = r - i386_regtab;
      return 1;
    }
  input_line_pointer = end;
  *end = 0;
  return 0;
}

void
md_operand (expressionS *e)
{
  if (*input_line_pointer == REGISTER_PREFIX)
    {
      char *end;
      const reg_entry *r = parse_real_register (input_line_pointer, &end);

      if (r)
	{
	  e->X_op = O_register;
	  e->X_add_number = r - i386_regtab;
	  input_line_pointer = end;
	}
    }
}


#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
const char *md_shortopts = "kVQ:sqn";
#else
const char *md_shortopts = "qn";
#endif

#define OPTION_32 (OPTION_MD_BASE + 0)
#define OPTION_64 (OPTION_MD_BASE + 1)
#define OPTION_DIVIDE (OPTION_MD_BASE + 2)
#define OPTION_MARCH (OPTION_MD_BASE + 3)
#define OPTION_MTUNE (OPTION_MD_BASE + 4)

struct option md_longopts[] =
{
  {"32", no_argument, NULL, OPTION_32},
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF) || defined(TE_PEP)
  {"64", no_argument, NULL, OPTION_64},
#endif
  {"divide", no_argument, NULL, OPTION_DIVIDE},
  {"march", required_argument, NULL, OPTION_MARCH},
  {"mtune", required_argument, NULL, OPTION_MTUNE},
  {NULL, no_argument, NULL, 0}
};
size_t md_longopts_size = sizeof (md_longopts);

int
md_parse_option (int c, char *arg)
{
  unsigned int i;

  switch (c)
    {
    case 'n':
      optimize_align_code = 0;
      break;

    case 'q':
      quiet_warnings = 1;
      break;

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
      /* -Qy, -Qn: SVR4 arguments controlling whether a .comment section
	 should be emitted or not.  FIXME: Not implemented.  */
    case 'Q':
      break;

      /* -V: SVR4 argument to print version ID.  */
    case 'V':
      print_version_id ();
      break;

      /* -k: Ignore for FreeBSD compatibility.  */
    case 'k':
      break;

    case 's':
      /* -s: On i386 Solaris, this tells the native assembler to use
	 .stab instead of .stab.excl.  We always use .stab anyhow.  */
      break;
#endif
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF) || defined(TE_PEP)
    case OPTION_64:
      {
	const char **list, **l;

	list = bfd_target_list ();
	for (l = list; *l != NULL; l++)
	  if (CONST_STRNEQ (*l, "elf64-x86-64")
	      || strcmp (*l, "coff-x86-64") == 0
	      || strcmp (*l, "pe-x86-64") == 0
	      || strcmp (*l, "pei-x86-64") == 0)
	    {
	      default_arch = "x86_64";
	      break;
	    }
	if (*l == NULL)
	  as_fatal (_("No compiled in support for x86_64"));
	free (list);
      }
      break;
#endif

    case OPTION_32:
      default_arch = "i386";
      break;

    case OPTION_DIVIDE:
#ifdef SVR4_COMMENT_CHARS
      {
	char *n, *t;
	const char *s;

	n = (char *) xmalloc (strlen (i386_comment_chars) + 1);
	t = n;
	for (s = i386_comment_chars; *s != '\0'; s++)
	  if (*s != '/')
	    *t++ = *s;
	*t = '\0';
	i386_comment_chars = n;
      }
#endif
      break;

    case OPTION_MARCH:
      if (*arg == '.')
	as_fatal (_("Invalid -march= option: `%s'"), arg);
      for (i = 0; i < ARRAY_SIZE (cpu_arch); i++)
	{
	  if (strcmp (arg, cpu_arch [i].name) == 0)
	    {
	      cpu_arch_isa = cpu_arch[i].type;
	      cpu_arch_isa_flags = cpu_arch[i].flags;
	      if (!cpu_arch_tune_set)
		{
		  cpu_arch_tune = cpu_arch_isa;
		  cpu_arch_tune_flags = cpu_arch_isa_flags;
		}
	      break;
	    }
	}
      if (i >= ARRAY_SIZE (cpu_arch))
	as_fatal (_("Invalid -march= option: `%s'"), arg);
      break;

    case OPTION_MTUNE:
      if (*arg == '.')
	as_fatal (_("Invalid -mtune= option: `%s'"), arg);
      for (i = 0; i < ARRAY_SIZE (cpu_arch); i++)
	{
	  if (strcmp (arg, cpu_arch [i].name) == 0)
	    {
	      cpu_arch_tune_set = 1;
	      cpu_arch_tune = cpu_arch [i].type;
	      cpu_arch_tune_flags = cpu_arch[i].flags;
	      break;
	    }
	}
      if (i >= ARRAY_SIZE (cpu_arch))
	as_fatal (_("Invalid -mtune= option: `%s'"), arg);
      break;

    default:
      return 0;
    }
  return 1;
}

void
md_show_usage (stream)
     FILE *stream;
{
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
  fprintf (stream, _("\
  -Q                      ignored\n\
  -V                      print assembler version number\n\
  -k                      ignored\n"));
#endif
  fprintf (stream, _("\
  -n                      Do not optimize code alignment\n\
  -q                      quieten some warnings\n"));
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
  fprintf (stream, _("\
  -s                      ignored\n"));
#endif
#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF) || defined(TE_PEP)
  fprintf (stream, _("\
  --32/--64               generate 32bit/64bit code\n"));
#endif
#ifdef SVR4_COMMENT_CHARS
  fprintf (stream, _("\
  --divide                do not treat `/' as a comment character\n"));
#else
  fprintf (stream, _("\
  --divide                ignored\n"));
#endif
  fprintf (stream, _("\
  -march=CPU/-mtune=CPU   generate code/optimize for CPU, where CPU is one of:\n\
                           i386, i486, pentium, pentiumpro, pentium4, nocona,\n\
                           core, core2, k6, athlon, k8, generic32, generic64\n"));

}

#if defined(TE_PEP)
const char *
x86_64_target_format (void)
{
  if (strcmp (default_arch, "x86_64") == 0)
    {
      set_code_flag (CODE_64BIT);
      return COFF_TARGET_FORMAT;
    }
  else if (strcmp (default_arch, "i386") == 0)
    {
      set_code_flag (CODE_32BIT);
      return "coff-i386";
    }

  as_fatal (_("Unknown architecture"));
  return NULL;
}
#endif

#if ((defined (OBJ_MAYBE_COFF) && defined (OBJ_MAYBE_AOUT)) \
     || defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF))

/* Pick the target format to use.  */

const char *
i386_target_format (void)
{
  if (!strcmp (default_arch, "x86_64"))
    {
      set_code_flag (CODE_64BIT);
      if (cpu_arch_isa_flags == 0)
	cpu_arch_isa_flags = Cpu186|Cpu286|Cpu386|Cpu486
			     |Cpu586|Cpu686|CpuP4|CpuMMX|CpuMMX2
			     |CpuSSE|CpuSSE2;
      if (cpu_arch_tune_flags == 0)
	cpu_arch_tune_flags = Cpu186|Cpu286|Cpu386|Cpu486
			      |Cpu586|Cpu686|CpuP4|CpuMMX|CpuMMX2
			      |CpuSSE|CpuSSE2;
    }
  else if (!strcmp (default_arch, "i386"))
    {
      set_code_flag (CODE_32BIT);
      if (cpu_arch_isa_flags == 0)
	cpu_arch_isa_flags = Cpu186|Cpu286|Cpu386;
      if (cpu_arch_tune_flags == 0)
	cpu_arch_tune_flags = Cpu186|Cpu286|Cpu386;
    }
  else
    as_fatal (_("Unknown architecture"));
  switch (OUTPUT_FLAVOR)
    {
#ifdef OBJ_MAYBE_AOUT
    case bfd_target_aout_flavour:
      return AOUT_TARGET_FORMAT;
#endif
#ifdef OBJ_MAYBE_COFF
    case bfd_target_coff_flavour:
      return "coff-i386";
#endif
#if defined (OBJ_MAYBE_ELF) || defined (OBJ_ELF)
    case bfd_target_elf_flavour:
      {
	if (flag_code == CODE_64BIT)
	  {
	    object_64bit = 1;
	    use_rela_relocations = 1;
	  }
	return flag_code == CODE_64BIT ? ELF_TARGET_FORMAT64 : ELF_TARGET_FORMAT;
      }
#endif
    default:
      abort ();
      return NULL;
    }
}

#endif /* OBJ_MAYBE_ more than one  */

#if (defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF))
void
i386_elf_emit_arch_note (void)
{
  if (IS_ELF && cpu_arch_name != NULL)
    {
      char *p;
      asection *seg = now_seg;
      subsegT subseg = now_subseg;
      Elf_Internal_Note i_note;
      Elf_External_Note e_note;
      asection *note_secp;
      int len;

      /* Create the .note section.  */
      note_secp = subseg_new (".note", 0);
      bfd_set_section_flags (stdoutput,
			     note_secp,
			     SEC_HAS_CONTENTS | SEC_READONLY);

      /* Process the arch string.  */
      len = strlen (cpu_arch_name);

      i_note.namesz = len + 1;
      i_note.descsz = 0;
      i_note.type = NT_ARCH;
      p = frag_more (sizeof (e_note.namesz));
      md_number_to_chars (p, (valueT) i_note.namesz, sizeof (e_note.namesz));
      p = frag_more (sizeof (e_note.descsz));
      md_number_to_chars (p, (valueT) i_note.descsz, sizeof (e_note.descsz));
      p = frag_more (sizeof (e_note.type));
      md_number_to_chars (p, (valueT) i_note.type, sizeof (e_note.type));
      p = frag_more (len + 1);
      strcpy (p, cpu_arch_name);

      frag_align (2, 0, 0);

      subseg_set (seg, subseg);
    }
}
#endif

symbolS *
md_undefined_symbol (name)
     char *name;
{
  if (name[0] == GLOBAL_OFFSET_TABLE_NAME[0]
      && name[1] == GLOBAL_OFFSET_TABLE_NAME[1]
      && name[2] == GLOBAL_OFFSET_TABLE_NAME[2]
      && strcmp (name, GLOBAL_OFFSET_TABLE_NAME) == 0)
    {
      if (!GOT_symbol)
	{
	  if (symbol_find (name))
	    as_bad (_("GOT already in symbol table"));
	  GOT_symbol = symbol_new (name, undefined_section,
				   (valueT) 0, &zero_address_frag);
	};
      return GOT_symbol;
    }
  return 0;
}

/* Round up a section size to the appropriate boundary.  */

valueT
md_section_align (segment, size)
     segT segment ATTRIBUTE_UNUSED;
     valueT size;
{
#if (defined (OBJ_AOUT) || defined (OBJ_MAYBE_AOUT))
  if (OUTPUT_FLAVOR == bfd_target_aout_flavour)
    {
      /* For a.out, force the section size to be aligned.  If we don't do
	 this, BFD will align it for us, but it will not write out the
	 final bytes of the section.  This may be a bug in BFD, but it is
	 easier to fix it here since that is how the other a.out targets
	 work.  */
      int align;

      align = bfd_get_section_alignment (stdoutput, segment);
      size = ((size + (1 << align) - 1) & ((valueT) -1 << align));
    }
#endif

  return size;
}

/* On the i386, PC-relative offsets are relative to the start of the
   next instruction.  That is, the address of the offset, plus its
   size, since the offset is always the last part of the insn.  */

long
md_pcrel_from (fixS *fixP)
{
  return fixP->fx_size + fixP->fx_where + fixP->fx_frag->fr_address;
}

#ifndef I386COFF

static void
s_bss (int ignore ATTRIBUTE_UNUSED)
{
  int temp;

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
  if (IS_ELF)
    obj_elf_section_change_hook ();
#endif
  temp = get_absolute_expression ();
  subseg_set (bss_section, (subsegT) temp);
  demand_empty_rest_of_line ();
}

#endif

void
i386_validate_fix (fixS *fixp)
{
  if (fixp->fx_subsy && fixp->fx_subsy == GOT_symbol)
    {
      if (fixp->fx_r_type == BFD_RELOC_32_PCREL)
	{
	  if (!object_64bit)
	    abort ();
	  fixp->fx_r_type = BFD_RELOC_X86_64_GOTPCREL;
	}
      else
	{
	  if (!object_64bit)
	    fixp->fx_r_type = BFD_RELOC_386_GOTOFF;
	  else
	    fixp->fx_r_type = BFD_RELOC_X86_64_GOTOFF64;
	}
      fixp->fx_subsy = 0;
    }
}

arelent *
tc_gen_reloc (section, fixp)
     asection *section ATTRIBUTE_UNUSED;
     fixS *fixp;
{
  arelent *rel;
  bfd_reloc_code_real_type code;

  switch (fixp->fx_r_type)
    {
    case BFD_RELOC_X86_64_PLT32:
    case BFD_RELOC_X86_64_GOT32:
    case BFD_RELOC_X86_64_GOTPCREL:
    case BFD_RELOC_386_PLT32:
    case BFD_RELOC_386_GOT32:
    case BFD_RELOC_386_GOTOFF:
    case BFD_RELOC_386_GOTPC:
    case BFD_RELOC_386_TLS_GD:
    case BFD_RELOC_386_TLS_LDM:
    case BFD_RELOC_386_TLS_LDO_32:
    case BFD_RELOC_386_TLS_IE_32:
    case BFD_RELOC_386_TLS_IE:
    case BFD_RELOC_386_TLS_GOTIE:
    case BFD_RELOC_386_TLS_LE_32:
    case BFD_RELOC_386_TLS_LE:
    case BFD_RELOC_386_TLS_GOTDESC:
    case BFD_RELOC_386_TLS_DESC_CALL:
    case BFD_RELOC_X86_64_TLSGD:
    case BFD_RELOC_X86_64_TLSLD:
    case BFD_RELOC_X86_64_DTPOFF32:
    case BFD_RELOC_X86_64_DTPOFF64:
    case BFD_RELOC_X86_64_GOTTPOFF:
    case BFD_RELOC_X86_64_TPOFF32:
    case BFD_RELOC_X86_64_TPOFF64:
    case BFD_RELOC_X86_64_GOTOFF64:
    case BFD_RELOC_X86_64_GOTPC32:
    case BFD_RELOC_X86_64_GOT64:
    case BFD_RELOC_X86_64_GOTPCREL64:
    case BFD_RELOC_X86_64_GOTPC64:
    case BFD_RELOC_X86_64_GOTPLT64:
    case BFD_RELOC_X86_64_PLTOFF64:
    case BFD_RELOC_X86_64_GOTPC32_TLSDESC:
    case BFD_RELOC_X86_64_TLSDESC_CALL:
    case BFD_RELOC_RVA:
    case BFD_RELOC_VTABLE_ENTRY:
    case BFD_RELOC_VTABLE_INHERIT:
#ifdef TE_PE
    case BFD_RELOC_32_SECREL:
#endif
      code = fixp->fx_r_type;
      break;
    case BFD_RELOC_X86_64_32S:
      if (!fixp->fx_pcrel)
	{
	  /* Don't turn BFD_RELOC_X86_64_32S into BFD_RELOC_32.  */
	  code = fixp->fx_r_type;
	  break;
	}
    default:
      if (fixp->fx_pcrel)
	{
	  switch (fixp->fx_size)
	    {
	    default:
	      as_bad_where (fixp->fx_file, fixp->fx_line,
			    _("can not do %d byte pc-relative relocation"),
			    fixp->fx_size);
	      code = BFD_RELOC_32_PCREL;
	      break;
	    case 1: code = BFD_RELOC_8_PCREL;  break;
	    case 2: code = BFD_RELOC_16_PCREL; break;
	    case 4: code = BFD_RELOC_32_PCREL; break;
#ifdef BFD64
	    case 8: code = BFD_RELOC_64_PCREL; break;
#endif
	    }
	}
      else
	{
	  switch (fixp->fx_size)
	    {
	    default:
	      as_bad_where (fixp->fx_file, fixp->fx_line,
			    _("can not do %d byte relocation"),
			    fixp->fx_size);
	      code = BFD_RELOC_32;
	      break;
	    case 1: code = BFD_RELOC_8;  break;
	    case 2: code = BFD_RELOC_16; break;
	    case 4: code = BFD_RELOC_32; break;
#ifdef BFD64
	    case 8: code = BFD_RELOC_64; break;
#endif
	    }
	}
      break;
    }

  if ((code == BFD_RELOC_32
       || code == BFD_RELOC_32_PCREL
       || code == BFD_RELOC_X86_64_32S)
      && GOT_symbol
      && fixp->fx_addsy == GOT_symbol)
    {
      if (!object_64bit)
	code = BFD_RELOC_386_GOTPC;
      else
	code = BFD_RELOC_X86_64_GOTPC32;
    }
  if ((code == BFD_RELOC_64 || code == BFD_RELOC_64_PCREL)
      && GOT_symbol
      && fixp->fx_addsy == GOT_symbol)
    {
      code = BFD_RELOC_X86_64_GOTPC64;
    }

  rel = (arelent *) xmalloc (sizeof (arelent));
  rel->sym_ptr_ptr = (asymbol **) xmalloc (sizeof (asymbol *));
  *rel->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);

  rel->address = fixp->fx_frag->fr_address + fixp->fx_where;

  if (!use_rela_relocations)
    {
      /* HACK: Since i386 ELF uses Rel instead of Rela, encode the
	 vtable entry to be used in the relocation's section offset.  */
      if (fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
	rel->address = fixp->fx_offset;

      rel->addend = 0;
    }
  /* Use the rela in 64bit mode.  */
  else
    {
      if (!fixp->fx_pcrel)
	rel->addend = fixp->fx_offset;
      else
	switch (code)
	  {
	  case BFD_RELOC_X86_64_PLT32:
	  case BFD_RELOC_X86_64_GOT32:
	  case BFD_RELOC_X86_64_GOTPCREL:
	  case BFD_RELOC_X86_64_TLSGD:
	  case BFD_RELOC_X86_64_TLSLD:
	  case BFD_RELOC_X86_64_GOTTPOFF:
	  case BFD_RELOC_X86_64_GOTPC32_TLSDESC:
	  case BFD_RELOC_X86_64_TLSDESC_CALL:
	    rel->addend = fixp->fx_offset - fixp->fx_size;
	    break;
	  default:
	    rel->addend = (section->vma
			   - fixp->fx_size
			   + fixp->fx_addnumber
			   + md_pcrel_from (fixp));
	    break;
	  }
    }

  rel->howto = bfd_reloc_type_lookup (stdoutput, code);
  if (rel->howto == NULL)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    _("cannot represent relocation type %s"),
		    bfd_get_reloc_code_name (code));
      /* Set howto to a garbage value so that we can keep going.  */
      rel->howto = bfd_reloc_type_lookup (stdoutput, BFD_RELOC_32);
      assert (rel->howto != NULL);
    }

  return rel;
}


/* Parse operands using Intel syntax. This implements a recursive descent
   parser based on the BNF grammar published in Appendix B of the MASM 6.1
   Programmer's Guide.

   FIXME: We do not recognize the full operand grammar defined in the MASM
	  documentation.  In particular, all the structure/union and
	  high-level macro operands are missing.

   Uppercase words are terminals, lower case words are non-terminals.
   Objects surrounded by double brackets '[[' ']]' are optional. Vertical
   bars '|' denote choices. Most grammar productions are implemented in
   functions called 'intel_<production>'.

   Initial production is 'expr'.

    addOp		+ | -

    alpha		[a-zA-Z]

    binOp		& | AND | \| | OR | ^ | XOR

    byteRegister	AL | AH | BL | BH | CL | CH | DL | DH

    constant		digits [[ radixOverride ]]

    dataType		BYTE | WORD | DWORD | FWORD | QWORD | TBYTE | OWORD | XMMWORD

    digits		decdigit
			| digits decdigit
			| digits hexdigit

    decdigit		[0-9]

    e04			e04 addOp e05
			| e05

    e05			e05 binOp e06
			| e06

    e06			e06 mulOp e09
			| e09

    e09			OFFSET e10
			| SHORT e10
			| + e10
			| - e10
			| ~ e10
			| NOT e10
			| e09 PTR e10
			| e09 : e10
			| e10

    e10			e10 [ expr ]
			| e11

    e11			( expr )
			| [ expr ]
			| constant
			| dataType
			| id
			| $
			| register

 => expr		expr cmpOp e04
			| e04

    gpRegister		AX | EAX | BX | EBX | CX | ECX | DX | EDX
			| BP | EBP | SP | ESP | DI | EDI | SI | ESI

    hexdigit		a | b | c | d | e | f
			| A | B | C | D | E | F

    id			alpha
			| id alpha
			| id decdigit

    mulOp		* | / | % | MOD | << | SHL | >> | SHR

    quote		" | '

    register		specialRegister
			| gpRegister
			| byteRegister

    segmentRegister	CS | DS | ES | FS | GS | SS

    specialRegister	CR0 | CR2 | CR3 | CR4
			| DR0 | DR1 | DR2 | DR3 | DR6 | DR7
			| TR3 | TR4 | TR5 | TR6 | TR7

    We simplify the grammar in obvious places (e.g., register parsing is
    done by calling parse_register) and eliminate immediate left recursion
    to implement a recursive-descent parser.

    expr	e04 expr'

    expr'	cmpOp e04 expr'
		| Empty

    e04		e05 e04'

    e04'	addOp e05 e04'
		| Empty

    e05		e06 e05'

    e05'	binOp e06 e05'
		| Empty

    e06		e09 e06'

    e06'	mulOp e09 e06'
		| Empty

    e09		OFFSET e10 e09'
		| SHORT e10'
		| + e10'
		| - e10'
		| ~ e10'
		| NOT e10'
		| e10 e09'

    e09'	PTR e10 e09'
		| : e10 e09'
		| Empty

    e10		e11 e10'

    e10'	[ expr ] e10'
		| Empty

    e11		( expr )
		| [ expr ]
		| BYTE
		| WORD
		| DWORD
		| FWORD
		| QWORD
		| TBYTE
		| OWORD
		| XMMWORD
		| .
		| $
		| register
		| id
		| constant  */

/* Parsing structure for the intel syntax parser. Used to implement the
   semantic actions for the operand grammar.  */
struct intel_parser_s
  {
    char *op_string;		/* The string being parsed.  */
    int got_a_float;		/* Whether the operand is a float.  */
    int op_modifier;		/* Operand modifier.  */
    int is_mem;			/* 1 if operand is memory reference.  */
    int in_offset;		/* >=1 if parsing operand of offset.  */
    int in_bracket;		/* >=1 if parsing operand in brackets.  */
    const reg_entry *reg;	/* Last register reference found.  */
    char *disp;			/* Displacement string being built.  */
    char *next_operand;		/* Resume point when splitting operands.  */
  };

static struct intel_parser_s intel_parser;

/* Token structure for parsing intel syntax.  */
struct intel_token
  {
    int code;			/* Token code.  */
    const reg_entry *reg;	/* Register entry for register tokens.  */
    char *str;			/* String representation.  */
  };

static struct intel_token cur_token, prev_token;

/* Token codes for the intel parser. Since T_SHORT is already used
   by COFF, undefine it first to prevent a warning.  */
#define T_NIL		-1
#define T_CONST		1
#define T_REG		2
#define T_BYTE		3
#define T_WORD		4
#define T_DWORD		5
#define T_FWORD		6
#define T_QWORD		7
#define T_TBYTE		8
#define T_XMMWORD	9
#undef  T_SHORT
#define T_SHORT		10
#define T_OFFSET	11
#define T_PTR		12
#define T_ID		13
#define T_SHL		14
#define T_SHR		15

/* Prototypes for intel parser functions.  */
static int intel_match_token (int);
static void intel_putback_token	(void);
static void intel_get_token (void);
static int intel_expr (void);
static int intel_e04 (void);
static int intel_e05 (void);
static int intel_e06 (void);
static int intel_e09 (void);
static int intel_e10 (void);
static int intel_e11 (void);

static int
i386_intel_operand (char *operand_string, int got_a_float)
{
  int ret;
  char *p;

  p = intel_parser.op_string = xstrdup (operand_string);
  intel_parser.disp = (char *) xmalloc (strlen (operand_string) + 1);

  for (;;)
    {
      /* Initialize token holders.  */
      cur_token.code = prev_token.code = T_NIL;
      cur_token.reg = prev_token.reg = NULL;
      cur_token.str = prev_token.str = NULL;

      /* Initialize parser structure.  */
      intel_parser.got_a_float = got_a_float;
      intel_parser.op_modifier = 0;
      intel_parser.is_mem = 0;
      intel_parser.in_offset = 0;
      intel_parser.in_bracket = 0;
      intel_parser.reg = NULL;
      intel_parser.disp[0] = '\0';
      intel_parser.next_operand = NULL;

      /* Read the first token and start the parser.  */
      intel_get_token ();
      ret = intel_expr ();

      if (!ret)
	break;

      if (cur_token.code != T_NIL)
	{
	  as_bad (_("invalid operand for '%s' ('%s' unexpected)"),
		  current_templates->start->name, cur_token.str);
	  ret = 0;
	}
      /* If we found a memory reference, hand it over to i386_displacement
	 to fill in the rest of the operand fields.  */
      else if (intel_parser.is_mem)
	{
	  if ((i.mem_operands == 1
	       && (current_templates->start->opcode_modifier & IsString) == 0)
	      || i.mem_operands == 2)
	    {
	      as_bad (_("too many memory references for '%s'"),
		      current_templates->start->name);
	      ret = 0;
	    }
	  else
	    {
	      char *s = intel_parser.disp;
	      i.mem_operands++;

	      if (!quiet_warnings && intel_parser.is_mem < 0)
		/* See the comments in intel_bracket_expr.  */
		as_warn (_("Treating `%s' as memory reference"), operand_string);

	      /* Add the displacement expression.  */
	      if (*s != '\0')
		ret = i386_displacement (s, s + strlen (s));
	      if (ret)
		{
		  /* Swap base and index in 16-bit memory operands like
		     [si+bx]. Since i386_index_check is also used in AT&T
		     mode we have to do that here.  */
		  if (i.base_reg
		      && i.index_reg
		      && (i.base_reg->reg_type & Reg16)
		      && (i.index_reg->reg_type & Reg16)
		      && i.base_reg->reg_num >= 6
		      && i.index_reg->reg_num < 6)
		    {
		      const reg_entry *base = i.index_reg;

		      i.index_reg = i.base_reg;
		      i.base_reg = base;
		    }
		  ret = i386_index_check (operand_string);
		}
	    }
	}

      /* Constant and OFFSET expressions are handled by i386_immediate.  */
      else if ((intel_parser.op_modifier & (1 << T_OFFSET))
	       || intel_parser.reg == NULL)
	ret = i386_immediate (intel_parser.disp);

      if (intel_parser.next_operand && this_operand >= MAX_OPERANDS - 1)
	ret = 0;
      if (!ret || !intel_parser.next_operand)
	break;
      intel_parser.op_string = intel_parser.next_operand;
      this_operand = i.operands++;
    }

  free (p);
  free (intel_parser.disp);

  return ret;
}

#define NUM_ADDRESS_REGS (!!i.base_reg + !!i.index_reg)

/* expr	e04 expr'

   expr'  cmpOp e04 expr'
	| Empty  */
static int
intel_expr (void)
{
  /* XXX Implement the comparison operators.  */
  return intel_e04 ();
}

/* e04	e05 e04'

   e04'	addOp e05 e04'
	| Empty  */
static int
intel_e04 (void)
{
  int nregs = -1;

  for (;;)
    {
      if (!intel_e05())
	return 0;

      if (nregs >= 0 && NUM_ADDRESS_REGS > nregs)
	i.base_reg = i386_regtab + REGNAM_AL; /* al is invalid as base */

      if (cur_token.code == '+')
	nregs = -1;
      else if (cur_token.code == '-')
	nregs = NUM_ADDRESS_REGS;
      else
	return 1;

      strcat (intel_parser.disp, cur_token.str);
      intel_match_token (cur_token.code);
    }
}

/* e05	e06 e05'

   e05'	binOp e06 e05'
	| Empty  */
static int
intel_e05 (void)
{
  int nregs = ~NUM_ADDRESS_REGS;

  for (;;)
    {
      if (!intel_e06())
	return 0;

      if (cur_token.code == '&'
	  || cur_token.code == '|'
	  || cur_token.code == '^')
	{
	  char str[2];

	  str[0] = cur_token.code;
	  str[1] = 0;
	  strcat (intel_parser.disp, str);
	}
      else
	break;

      intel_match_token (cur_token.code);

      if (nregs < 0)
	nregs = ~nregs;
    }
  if (nregs >= 0 && NUM_ADDRESS_REGS > nregs)
    i.base_reg = i386_regtab + REGNAM_AL + 1; /* cl is invalid as base */
  return 1;
}

/* e06	e09 e06'

   e06'	mulOp e09 e06'
	| Empty  */
static int
intel_e06 (void)
{
  int nregs = ~NUM_ADDRESS_REGS;

  for (;;)
    {
      if (!intel_e09())
	return 0;

      if (cur_token.code == '*'
	  || cur_token.code == '/'
	  || cur_token.code == '%')
	{
	  char str[2];

	  str[0] = cur_token.code;
	  str[1] = 0;
	  strcat (intel_parser.disp, str);
	}
      else if (cur_token.code == T_SHL)
	strcat (intel_parser.disp, "<<");
      else if (cur_token.code == T_SHR)
	strcat (intel_parser.disp, ">>");
      else
	break;

      intel_match_token (cur_token.code);

      if (nregs < 0)
	nregs = ~nregs;
    }
  if (nregs >= 0 && NUM_ADDRESS_REGS > nregs)
    i.base_reg = i386_regtab + REGNAM_AL + 2; /* dl is invalid as base */
  return 1;
}

/* e09	OFFSET e09
	| SHORT e09
	| + e09
	| - e09
	| ~ e09
	| NOT e09
	| e10 e09'

   e09'	PTR e10 e09'
	| : e10 e09'
	| Empty */
static int
intel_e09 (void)
{
  int nregs = ~NUM_ADDRESS_REGS;
  int in_offset = 0;

  for (;;)
    {
      /* Don't consume constants here.  */
      if (cur_token.code == '+' || cur_token.code == '-')
	{
	  /* Need to look one token ahead - if the next token
	     is a constant, the current token is its sign.  */
	  int next_code;

	  intel_match_token (cur_token.code);
	  next_code = cur_token.code;
	  intel_putback_token ();
	  if (next_code == T_CONST)
	    break;
	}

      /* e09  OFFSET e09  */
      if (cur_token.code == T_OFFSET)
	{
	  if (!in_offset++)
	    ++intel_parser.in_offset;
	}

      /* e09  SHORT e09  */
      else if (cur_token.code == T_SHORT)
	intel_parser.op_modifier |= 1 << T_SHORT;

      /* e09  + e09  */
      else if (cur_token.code == '+')
	strcat (intel_parser.disp, "+");

      /* e09  - e09
	      | ~ e09
	      | NOT e09  */
      else if (cur_token.code == '-' || cur_token.code == '~')
	{
	  char str[2];

	  if (nregs < 0)
	    nregs = ~nregs;
	  str[0] = cur_token.code;
	  str[1] = 0;
	  strcat (intel_parser.disp, str);
	}

      /* e09  e10 e09'  */
      else
	break;

      intel_match_token (cur_token.code);
    }

  for (;;)
    {
      if (!intel_e10 ())
	return 0;

      /* e09'  PTR e10 e09' */
      if (cur_token.code == T_PTR)
	{
	  char suffix;

	  if (prev_token.code == T_BYTE)
	    suffix = BYTE_MNEM_SUFFIX;

	  else if (prev_token.code == T_WORD)
	    {
	      if (current_templates->start->name[0] == 'l'
		  && current_templates->start->name[2] == 's'
		  && current_templates->start->name[3] == 0)
		suffix = BYTE_MNEM_SUFFIX; /* so it will cause an error */
	      else if (intel_parser.got_a_float == 2)	/* "fi..." */
		suffix = SHORT_MNEM_SUFFIX;
	      else
		suffix = WORD_MNEM_SUFFIX;
	    }

	  else if (prev_token.code == T_DWORD)
	    {
	      if (current_templates->start->name[0] == 'l'
		  && current_templates->start->name[2] == 's'
		  && current_templates->start->name[3] == 0)
		suffix = WORD_MNEM_SUFFIX;
	      else if (flag_code == CODE_16BIT
		       && (current_templates->start->opcode_modifier
			   & (Jump | JumpDword)))
		suffix = LONG_DOUBLE_MNEM_SUFFIX;
	      else if (intel_parser.got_a_float == 1)	/* "f..." */
		suffix = SHORT_MNEM_SUFFIX;
	      else
		suffix = LONG_MNEM_SUFFIX;
	    }

	  else if (prev_token.code == T_FWORD)
	    {
	      if (current_templates->start->name[0] == 'l'
		  && current_templates->start->name[2] == 's'
		  && current_templates->start->name[3] == 0)
		suffix = LONG_MNEM_SUFFIX;
	      else if (!intel_parser.got_a_float)
		{
		  if (flag_code == CODE_16BIT)
		    add_prefix (DATA_PREFIX_OPCODE);
		  suffix = LONG_DOUBLE_MNEM_SUFFIX;
		}
	      else
		suffix = BYTE_MNEM_SUFFIX; /* so it will cause an error */
	    }

	  else if (prev_token.code == T_QWORD)
	    {
	      if (intel_parser.got_a_float == 1)	/* "f..." */
		suffix = LONG_MNEM_SUFFIX;
	      else
		suffix = QWORD_MNEM_SUFFIX;
	    }

	  else if (prev_token.code == T_TBYTE)
	    {
	      if (intel_parser.got_a_float == 1)
		suffix = LONG_DOUBLE_MNEM_SUFFIX;
	      else
		suffix = BYTE_MNEM_SUFFIX; /* so it will cause an error */
	    }

	  else if (prev_token.code == T_XMMWORD)
	    {
	      /* XXX ignored for now, but accepted since gcc uses it */
	      suffix = 0;
	    }

	  else
	    {
	      as_bad (_("Unknown operand modifier `%s'"), prev_token.str);
	      return 0;
	    }

	  /* Operands for jump/call using 'ptr' notation denote absolute
	     addresses.  */
	  if (current_templates->start->opcode_modifier & (Jump | JumpDword))
	    i.types[this_operand] |= JumpAbsolute;

	  if (current_templates->start->base_opcode == 0x8d /* lea */)
	    ;
	  else if (!i.suffix)
	    i.suffix = suffix;
	  else if (i.suffix != suffix)
	    {
	      as_bad (_("Conflicting operand modifiers"));
	      return 0;
	    }

	}

      /* e09'  : e10 e09'  */
      else if (cur_token.code == ':')
	{
	  if (prev_token.code != T_REG)
	    {
	      /* While {call,jmp} SSSS:OOOO is MASM syntax only when SSSS is a
		 segment/group identifier (which we don't have), using comma
		 as the operand separator there is even less consistent, since
		 there all branches only have a single operand.  */
	      if (this_operand != 0
		  || intel_parser.in_offset
		  || intel_parser.in_bracket
		  || (!(current_templates->start->opcode_modifier
			& (Jump|JumpDword|JumpInterSegment))
		      && !(current_templates->start->operand_types[0]
			   & JumpAbsolute)))
		return intel_match_token (T_NIL);
	      /* Remember the start of the 2nd operand and terminate 1st
		 operand here.
		 XXX This isn't right, yet (when SSSS:OOOO is right operand of
		 another expression), but it gets at least the simplest case
		 (a plain number or symbol on the left side) right.  */
	      intel_parser.next_operand = intel_parser.op_string;
	      *--intel_parser.op_string = '\0';
	      return intel_match_token (':');
	    }
	}

      /* e09'  Empty  */
      else
	break;

      intel_match_token (cur_token.code);

    }

  if (in_offset)
    {
      --intel_parser.in_offset;
      if (nregs < 0)
	nregs = ~nregs;
      if (NUM_ADDRESS_REGS > nregs)
	{
	  as_bad (_("Invalid operand to `OFFSET'"));
	  return 0;
	}
      intel_parser.op_modifier |= 1 << T_OFFSET;
    }

  if (nregs >= 0 && NUM_ADDRESS_REGS > nregs)
    i.base_reg = i386_regtab + REGNAM_AL + 3; /* bl is invalid as base */
  return 1;
}

static int
intel_bracket_expr (void)
{
  int was_offset = intel_parser.op_modifier & (1 << T_OFFSET);
  const char *start = intel_parser.op_string;
  int len;

  if (i.op[this_operand].regs)
    return intel_match_token (T_NIL);

  intel_match_token ('[');

  /* Mark as a memory operand only if it's not already known to be an
     offset expression.  If it's an offset expression, we need to keep
     the brace in.  */
  if (!intel_parser.in_offset)
    {
      ++intel_parser.in_bracket;

      /* Operands for jump/call inside brackets denote absolute addresses.  */
      if (current_templates->start->opcode_modifier & (Jump | JumpDword))
	i.types[this_operand] |= JumpAbsolute;

      /* Unfortunately gas always diverged from MASM in a respect that can't
	 be easily fixed without risking to break code sequences likely to be
	 encountered (the testsuite even check for this): MASM doesn't consider
	 an expression inside brackets unconditionally as a memory reference.
	 When that is e.g. a constant, an offset expression, or the sum of the
	 two, this is still taken as a constant load. gas, however, always
	 treated these as memory references. As a compromise, we'll try to make
	 offset expressions inside brackets work the MASM way (since that's
	 less likely to be found in real world code), but make constants alone
	 continue to work the traditional gas way. In either case, issue a
	 warning.  */
      intel_parser.op_modifier &= ~was_offset;
    }
  else
    strcat (intel_parser.disp, "[");

  /* Add a '+' to the displacement string if necessary.  */
  if (*intel_parser.disp != '\0'
      && *(intel_parser.disp + strlen (intel_parser.disp) - 1) != '+')
    strcat (intel_parser.disp, "+");

  if (intel_expr ()
      && (len = intel_parser.op_string - start - 1,
	  intel_match_token (']')))
    {
      /* Preserve brackets when the operand is an offset expression.  */
      if (intel_parser.in_offset)
	strcat (intel_parser.disp, "]");
      else
	{
	  --intel_parser.in_bracket;
	  if (i.base_reg || i.index_reg)
	    intel_parser.is_mem = 1;
	  if (!intel_parser.is_mem)
	    {
	      if (!(intel_parser.op_modifier & (1 << T_OFFSET)))
		/* Defer the warning until all of the operand was parsed.  */
		intel_parser.is_mem = -1;
	      else if (!quiet_warnings)
		as_warn (_("`[%.*s]' taken to mean just `%.*s'"),
			 len, start, len, start);
	    }
	}
      intel_parser.op_modifier |= was_offset;

      return 1;
    }
  return 0;
}

/* e10	e11 e10'

   e10'	[ expr ] e10'
	| Empty  */
static int
intel_e10 (void)
{
  if (!intel_e11 ())
    return 0;

  while (cur_token.code == '[')
    {
      if (!intel_bracket_expr ())
	return 0;
    }

  return 1;
}

/* e11	( expr )
	| [ expr ]
	| BYTE
	| WORD
	| DWORD
	| FWORD
	| QWORD
	| TBYTE
	| OWORD
	| XMMWORD
	| $
	| .
	| register
	| id
	| constant  */
static int
intel_e11 (void)
{
  switch (cur_token.code)
    {
    /* e11  ( expr ) */
    case '(':
      intel_match_token ('(');
      strcat (intel_parser.disp, "(");

      if (intel_expr () && intel_match_token (')'))
	{
	  strcat (intel_parser.disp, ")");
	  return 1;
	}
      return 0;

    /* e11  [ expr ] */
    case '[':
      return intel_bracket_expr ();

    /* e11  $
	    | .  */
    case '.':
      strcat (intel_parser.disp, cur_token.str);
      intel_match_token (cur_token.code);

      /* Mark as a memory operand only if it's not already known to be an
	 offset expression.  */
      if (!intel_parser.in_offset)
	intel_parser.is_mem = 1;

      return 1;

    /* e11  register  */
    case T_REG:
      {
	const reg_entry *reg = intel_parser.reg = cur_token.reg;

	intel_match_token (T_REG);

	/* Check for segment change.  */
	if (cur_token.code == ':')
	  {
	    if (!(reg->reg_type & (SReg2 | SReg3)))
	      {
		as_bad (_("`%s' is not a valid segment register"),
			reg->reg_name);
		return 0;
	      }
	    else if (i.seg[i.mem_operands])
	      as_warn (_("Extra segment override ignored"));
	    else
	      {
		if (!intel_parser.in_offset)
		  intel_parser.is_mem = 1;
		switch (reg->reg_num)
		  {
		  case 0:
		    i.seg[i.mem_operands] = &es;
		    break;
		  case 1:
		    i.seg[i.mem_operands] = &cs;
		    break;
		  case 2:
		    i.seg[i.mem_operands] = &ss;
		    break;
		  case 3:
		    i.seg[i.mem_operands] = &ds;
		    break;
		  case 4:
		    i.seg[i.mem_operands] = &fs;
		    break;
		  case 5:
		    i.seg[i.mem_operands] = &gs;
		    break;
		  }
	      }
	  }

	/* Not a segment register. Check for register scaling.  */
	else if (cur_token.code == '*')
	  {
	    if (!intel_parser.in_bracket)
	      {
		as_bad (_("Register scaling only allowed in memory operands"));
		return 0;
	      }

	    if (reg->reg_type & Reg16) /* Disallow things like [si*1]. */
	      reg = i386_regtab + REGNAM_AX + 4; /* sp is invalid as index */
	    else if (i.index_reg)
	      reg = i386_regtab + REGNAM_EAX + 4; /* esp is invalid as index */

	    /* What follows must be a valid scale.  */
	    intel_match_token ('*');
	    i.index_reg = reg;
	    i.types[this_operand] |= BaseIndex;

	    /* Set the scale after setting the register (otherwise,
	       i386_scale will complain)  */
	    if (cur_token.code == '+' || cur_token.code == '-')
	      {
		char *str, sign = cur_token.code;
		intel_match_token (cur_token.code);
		if (cur_token.code != T_CONST)
		  {
		    as_bad (_("Syntax error: Expecting a constant, got `%s'"),
			    cur_token.str);
		    return 0;
		  }
		str = (char *) xmalloc (strlen (cur_token.str) + 2);
		strcpy (str + 1, cur_token.str);
		*str = sign;
		if (!i386_scale (str))
		  return 0;
		free (str);
	      }
	    else if (!i386_scale (cur_token.str))
	      return 0;
	    intel_match_token (cur_token.code);
	  }

	/* No scaling. If this is a memory operand, the register is either a
	   base register (first occurrence) or an index register (second
	   occurrence).  */
	else if (intel_parser.in_bracket)
	  {

	    if (!i.base_reg)
	      i.base_reg = reg;
	    else if (!i.index_reg)
	      i.index_reg = reg;
	    else
	      {
		as_bad (_("Too many register references in memory operand"));
		return 0;
	      }

	    i.types[this_operand] |= BaseIndex;
	  }

	/* It's neither base nor index.  */
	else if (!intel_parser.in_offset && !intel_parser.is_mem)
	  {
	    i.types[this_operand] |= reg->reg_type & ~BaseIndex;
	    i.op[this_operand].regs = reg;
	    i.reg_operands++;
	  }
	else
	  {
	    as_bad (_("Invalid use of register"));
	    return 0;
	  }

	/* Since registers are not part of the displacement string (except
	   when we're parsing offset operands), we may need to remove any
	   preceding '+' from the displacement string.  */
	if (*intel_parser.disp != '\0'
	    && !intel_parser.in_offset)
	  {
	    char *s = intel_parser.disp;
	    s += strlen (s) - 1;
	    if (*s == '+')
	      *s = '\0';
	  }

	return 1;
      }

    /* e11  BYTE
	    | WORD
	    | DWORD
	    | FWORD
	    | QWORD
	    | TBYTE
	    | OWORD
	    | XMMWORD  */
    case T_BYTE:
    case T_WORD:
    case T_DWORD:
    case T_FWORD:
    case T_QWORD:
    case T_TBYTE:
    case T_XMMWORD:
      intel_match_token (cur_token.code);

      if (cur_token.code == T_PTR)
	return 1;

      /* It must have been an identifier.  */
      intel_putback_token ();
      cur_token.code = T_ID;
      /* FALLTHRU */

    /* e11  id
	    | constant  */
    case T_ID:
      if (!intel_parser.in_offset && intel_parser.is_mem <= 0)
	{
	  symbolS *symbolP;

	  /* The identifier represents a memory reference only if it's not
	     preceded by an offset modifier and if it's not an equate.  */
	  symbolP = symbol_find(cur_token.str);
	  if (!symbolP || S_GET_SEGMENT(symbolP) != absolute_section)
	    intel_parser.is_mem = 1;
	}
	/* FALLTHRU */

    case T_CONST:
    case '-':
    case '+':
      {
	char *save_str, sign = 0;

	/* Allow constants that start with `+' or `-'.  */
	if (cur_token.code == '-' || cur_token.code == '+')
	  {
	    sign = cur_token.code;
	    intel_match_token (cur_token.code);
	    if (cur_token.code != T_CONST)
	      {
		as_bad (_("Syntax error: Expecting a constant, got `%s'"),
			cur_token.str);
		return 0;
	      }
	  }

	save_str = (char *) xmalloc (strlen (cur_token.str) + 2);
	strcpy (save_str + !!sign, cur_token.str);
	if (sign)
	  *save_str = sign;

	/* Get the next token to check for register scaling.  */
	intel_match_token (cur_token.code);

	/* Check if this constant is a scaling factor for an
	   index register.  */
	if (cur_token.code == '*')
	  {
	    if (intel_match_token ('*') && cur_token.code == T_REG)
	      {
		const reg_entry *reg = cur_token.reg;

		if (!intel_parser.in_bracket)
		  {
		    as_bad (_("Register scaling only allowed "
			      "in memory operands"));
		    return 0;
		  }

		 /* Disallow things like [1*si].
		    sp and esp are invalid as index.  */
		if (reg->reg_type & Reg16)
		  reg = i386_regtab + REGNAM_AX + 4;
		else if (i.index_reg)
		  reg = i386_regtab + REGNAM_EAX + 4;

		/* The constant is followed by `* reg', so it must be
		   a valid scale.  */
		i.index_reg = reg;
		i.types[this_operand] |= BaseIndex;

		/* Set the scale after setting the register (otherwise,
		   i386_scale will complain)  */
		if (!i386_scale (save_str))
		  return 0;
		intel_match_token (T_REG);

		/* Since registers are not part of the displacement
		   string, we may need to remove any preceding '+' from
		   the displacement string.  */
		if (*intel_parser.disp != '\0')
		  {
		    char *s = intel_parser.disp;
		    s += strlen (s) - 1;
		    if (*s == '+')
		      *s = '\0';
		  }

		free (save_str);

		return 1;
	      }

	    /* The constant was not used for register scaling. Since we have
	       already consumed the token following `*' we now need to put it
	       back in the stream.  */
	    intel_putback_token ();
	  }

	/* Add the constant to the displacement string.  */
	strcat (intel_parser.disp, save_str);
	free (save_str);

	return 1;
      }
    }

  as_bad (_("Unrecognized token '%s'"), cur_token.str);
  return 0;
}

/* Match the given token against cur_token. If they match, read the next
   token from the operand string.  */
static int
intel_match_token (int code)
{
  if (cur_token.code == code)
    {
      intel_get_token ();
      return 1;
    }
  else
    {
      as_bad (_("Unexpected token `%s'"), cur_token.str);
      return 0;
    }
}

/* Read a new token from intel_parser.op_string and store it in cur_token.  */
static void
intel_get_token (void)
{
  char *end_op;
  const reg_entry *reg;
  struct intel_token new_token;

  new_token.code = T_NIL;
  new_token.reg = NULL;
  new_token.str = NULL;

  /* Free the memory allocated to the previous token and move
     cur_token to prev_token.  */
  if (prev_token.str)
    free (prev_token.str);

  prev_token = cur_token;

  /* Skip whitespace.  */
  while (is_space_char (*intel_parser.op_string))
    intel_parser.op_string++;

  /* Return an empty token if we find nothing else on the line.  */
  if (*intel_parser.op_string == '\0')
    {
      cur_token = new_token;
      return;
    }

  /* The new token cannot be larger than the remainder of the operand
     string.  */
  new_token.str = (char *) xmalloc (strlen (intel_parser.op_string) + 1);
  new_token.str[0] = '\0';

  if (strchr ("0123456789", *intel_parser.op_string))
    {
      char *p = new_token.str;
      char *q = intel_parser.op_string;
      new_token.code = T_CONST;

      /* Allow any kind of identifier char to encompass floating point and
	 hexadecimal numbers.  */
      while (is_identifier_char (*q))
	*p++ = *q++;
      *p = '\0';

      /* Recognize special symbol names [0-9][bf].  */
      if (strlen (intel_parser.op_string) == 2
	  && (intel_parser.op_string[1] == 'b'
	      || intel_parser.op_string[1] == 'f'))
	new_token.code = T_ID;
    }

  else if ((reg = parse_register (intel_parser.op_string, &end_op)) != NULL)
    {
      size_t len = end_op - intel_parser.op_string;

      new_token.code = T_REG;
      new_token.reg = reg;

      memcpy (new_token.str, intel_parser.op_string, len);
      new_token.str[len] = '\0';
    }

  else if (is_identifier_char (*intel_parser.op_string))
    {
      char *p = new_token.str;
      char *q = intel_parser.op_string;

      /* A '.' or '$' followed by an identifier char is an identifier.
	 Otherwise, it's operator '.' followed by an expression.  */
      if ((*q == '.' || *q == '$') && !is_identifier_char (*(q + 1)))
	{
	  new_token.code = '.';
	  new_token.str[0] = '.';
	  new_token.str[1] = '\0';
	}
      else
	{
	  while (is_identifier_char (*q) || *q == '@')
	    *p++ = *q++;
	  *p = '\0';

	  if (strcasecmp (new_token.str, "NOT") == 0)
	    new_token.code = '~';

	  else if (strcasecmp (new_token.str, "MOD") == 0)
	    new_token.code = '%';

	  else if (strcasecmp (new_token.str, "AND") == 0)
	    new_token.code = '&';

	  else if (strcasecmp (new_token.str, "OR") == 0)
	    new_token.code = '|';

	  else if (strcasecmp (new_token.str, "XOR") == 0)
	    new_token.code = '^';

	  else if (strcasecmp (new_token.str, "SHL") == 0)
	    new_token.code = T_SHL;

	  else if (strcasecmp (new_token.str, "SHR") == 0)
	    new_token.code = T_SHR;

	  else if (strcasecmp (new_token.str, "BYTE") == 0)
	    new_token.code = T_BYTE;

	  else if (strcasecmp (new_token.str, "WORD") == 0)
	    new_token.code = T_WORD;

	  else if (strcasecmp (new_token.str, "DWORD") == 0)
	    new_token.code = T_DWORD;

	  else if (strcasecmp (new_token.str, "FWORD") == 0)
	    new_token.code = T_FWORD;

	  else if (strcasecmp (new_token.str, "QWORD") == 0)
	    new_token.code = T_QWORD;

	  else if (strcasecmp (new_token.str, "TBYTE") == 0
		   /* XXX remove (gcc still uses it) */
		   || strcasecmp (new_token.str, "XWORD") == 0)
	    new_token.code = T_TBYTE;

	  else if (strcasecmp (new_token.str, "XMMWORD") == 0
		   || strcasecmp (new_token.str, "OWORD") == 0)
	    new_token.code = T_XMMWORD;

	  else if (strcasecmp (new_token.str, "PTR") == 0)
	    new_token.code = T_PTR;

	  else if (strcasecmp (new_token.str, "SHORT") == 0)
	    new_token.code = T_SHORT;

	  else if (strcasecmp (new_token.str, "OFFSET") == 0)
	    {
	      new_token.code = T_OFFSET;

	      /* ??? This is not mentioned in the MASM grammar but gcc
		     makes use of it with -mintel-syntax.  OFFSET may be
		     followed by FLAT:  */
	      if (strncasecmp (q, " FLAT:", 6) == 0)
		strcat (new_token.str, " FLAT:");
	    }

	  /* ??? This is not mentioned in the MASM grammar.  */
	  else if (strcasecmp (new_token.str, "FLAT") == 0)
	    {
	      new_token.code = T_OFFSET;
	      if (*q == ':')
		strcat (new_token.str, ":");
	      else
		as_bad (_("`:' expected"));
	    }

	  else
	    new_token.code = T_ID;
	}
    }

  else if (strchr ("+-/*%|&^:[]()~", *intel_parser.op_string))
    {
      new_token.code = *intel_parser.op_string;
      new_token.str[0] = *intel_parser.op_string;
      new_token.str[1] = '\0';
    }

  else if (strchr ("<>", *intel_parser.op_string)
	   && *intel_parser.op_string == *(intel_parser.op_string + 1))
    {
      new_token.code = *intel_parser.op_string == '<' ? T_SHL : T_SHR;
      new_token.str[0] = *intel_parser.op_string;
      new_token.str[1] = *intel_parser.op_string;
      new_token.str[2] = '\0';
    }

  else
    as_bad (_("Unrecognized token `%s'"), intel_parser.op_string);

  intel_parser.op_string += strlen (new_token.str);
  cur_token = new_token;
}

/* Put cur_token back into the token stream and make cur_token point to
   prev_token.  */
static void
intel_putback_token (void)
{
  if (cur_token.code != T_NIL)
    {
      intel_parser.op_string -= strlen (cur_token.str);
      free (cur_token.str);
    }
  cur_token = prev_token;

  /* Forget prev_token.  */
  prev_token.code = T_NIL;
  prev_token.reg = NULL;
  prev_token.str = NULL;
}

int
tc_x86_regname_to_dw2regnum (char *regname)
{
  unsigned int regnum;
  unsigned int regnames_count;
  static const char *const regnames_32[] =
    {
      "eax", "ecx", "edx", "ebx",
      "esp", "ebp", "esi", "edi",
      "eip", "eflags", NULL,
      "st0", "st1", "st2", "st3",
      "st4", "st5", "st6", "st7",
      NULL, NULL,
      "xmm0", "xmm1", "xmm2", "xmm3",
      "xmm4", "xmm5", "xmm6", "xmm7",
      "mm0", "mm1", "mm2", "mm3",
      "mm4", "mm5", "mm6", "mm7",
      "fcw", "fsw", "mxcsr",
      "es", "cs", "ss", "ds", "fs", "gs", NULL, NULL,
      "tr", "ldtr"
    };
  static const char *const regnames_64[] =
    {
      "rax", "rdx", "rcx", "rbx",
      "rsi", "rdi", "rbp", "rsp",
      "r8",  "r9",  "r10", "r11",
      "r12", "r13", "r14", "r15",
      "rip",
      "xmm0",  "xmm1",  "xmm2",  "xmm3",
      "xmm4",  "xmm5",  "xmm6",  "xmm7",
      "xmm8",  "xmm9",  "xmm10", "xmm11",
      "xmm12", "xmm13", "xmm14", "xmm15",
      "st0", "st1", "st2", "st3",
      "st4", "st5", "st6", "st7",
      "mm0", "mm1", "mm2", "mm3",
      "mm4", "mm5", "mm6", "mm7",
      "rflags",
      "es", "cs", "ss", "ds", "fs", "gs", NULL, NULL,
      "fs.base", "gs.base", NULL, NULL,
      "tr", "ldtr",
      "mxcsr", "fcw", "fsw"
    };
  const char *const *regnames;

  if (flag_code == CODE_64BIT)
    {
      regnames = regnames_64;
      regnames_count = ARRAY_SIZE (regnames_64);
    }
  else
    {
      regnames = regnames_32;
      regnames_count = ARRAY_SIZE (regnames_32);
    }

  for (regnum = 0; regnum < regnames_count; regnum++)
    if (regnames[regnum] != NULL
	&& strcmp (regname, regnames[regnum]) == 0)
      return regnum;

  return -1;
}

void
tc_x86_frame_initial_instructions (void)
{
  static unsigned int sp_regno;

  if (!sp_regno)
    sp_regno = tc_x86_regname_to_dw2regnum (flag_code == CODE_64BIT
					    ? "rsp" : "esp");

  cfi_add_CFA_def_cfa (sp_regno, -x86_cie_data_alignment);
  cfi_add_CFA_offset (x86_dwarf2_return_column, x86_cie_data_alignment);
}

int
i386_elf_section_type (const char *str, size_t len)
{
  if (flag_code == CODE_64BIT
      && len == sizeof ("unwind") - 1
      && strncmp (str, "unwind", 6) == 0)
    return SHT_X86_64_UNWIND;

  return -1;
}

#ifdef TE_PE
void
tc_pe_dwarf2_emit_offset (symbolS *symbol, unsigned int size)
{
  expressionS expr;

  expr.X_op = O_secrel;
  expr.X_add_symbol = symbol;
  expr.X_add_number = 0;
  emit_expr (&expr, size);
}
#endif

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
/* For ELF on x86-64, add support for SHF_X86_64_LARGE.  */

int
x86_64_section_letter (int letter, char **ptr_msg)
{
  if (flag_code == CODE_64BIT)
    {
      if (letter == 'l')
	return SHF_X86_64_LARGE;

      *ptr_msg = _("Bad .section directive: want a,l,w,x,M,S,G,T in string");
    }
  else
    *ptr_msg = _("Bad .section directive: want a,w,x,M,S,G,T in string");
  return -1;
}

int
x86_64_section_word (char *str, size_t len)
{
  if (len == 5 && flag_code == CODE_64BIT && CONST_STRNEQ (str, "large"))
    return SHF_X86_64_LARGE;

  return -1;
}

static void
handle_large_common (int small ATTRIBUTE_UNUSED)
{
  if (flag_code != CODE_64BIT)
    {
      s_comm_internal (0, elf_common_parse);
      as_warn (_(".largecomm supported only in 64bit mode, producing .comm"));
    }
  else
    {
      static segT lbss_section;
      asection *saved_com_section_ptr = elf_com_section_ptr;
      asection *saved_bss_section = bss_section;

      if (lbss_section == NULL)
	{
	  flagword applicable;
	  segT seg = now_seg;
	  subsegT subseg = now_subseg;

	  /* The .lbss section is for local .largecomm symbols.  */
	  lbss_section = subseg_new (".lbss", 0);
	  applicable = bfd_applicable_section_flags (stdoutput);
	  bfd_set_section_flags (stdoutput, lbss_section,
				 applicable & SEC_ALLOC);
	  seg_info (lbss_section)->bss = 1;

	  subseg_set (seg, subseg);
	}

      elf_com_section_ptr = &_bfd_elf_large_com_section;
      bss_section = lbss_section;

      s_comm_internal (0, elf_common_parse);

      elf_com_section_ptr = saved_com_section_ptr;
      bss_section = saved_bss_section;
    }
}
#endif /* OBJ_ELF || OBJ_MAYBE_ELF */
