/* tc-ppc.c -- Assemble for the PowerPC or POWER (RS/6000)
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

#include "as.h"
#include "safe-ctype.h"
#include "subsegs.h"
#include "dw2gencfi.h"
#include "opcode/ppc.h"

#ifdef OBJ_ELF
#include "elf/ppc.h"
#include "dwarf2dbg.h"
#endif

#ifdef TE_PE
#include "coff/pe.h"
#endif

/* This is the assembler for the PowerPC or POWER (RS/6000) chips.  */

/* Tell the main code what the endianness is.  */
extern int target_big_endian;

/* Whether or not, we've set target_big_endian.  */
static int set_target_endian = 0;

/* Whether to use user friendly register names.  */
#ifndef TARGET_REG_NAMES_P
#ifdef TE_PE
#define TARGET_REG_NAMES_P TRUE
#else
#define TARGET_REG_NAMES_P FALSE
#endif
#endif

/* Macros for calculating LO, HI, HA, HIGHER, HIGHERA, HIGHEST,
   HIGHESTA.  */

/* #lo(value) denotes the least significant 16 bits of the indicated.  */
#define PPC_LO(v) ((v) & 0xffff)

/* #hi(value) denotes bits 16 through 31 of the indicated value.  */
#define PPC_HI(v) (((v) >> 16) & 0xffff)

/* #ha(value) denotes the high adjusted value: bits 16 through 31 of
  the indicated value, compensating for #lo() being treated as a
  signed number.  */
#define PPC_HA(v) PPC_HI ((v) + 0x8000)

/* #higher(value) denotes bits 32 through 47 of the indicated value.  */
#define PPC_HIGHER(v) (((v) >> 16 >> 16) & 0xffff)

/* #highera(value) denotes bits 32 through 47 of the indicated value,
   compensating for #lo() being treated as a signed number.  */
#define PPC_HIGHERA(v) PPC_HIGHER ((v) + 0x8000)

/* #highest(value) denotes bits 48 through 63 of the indicated value.  */
#define PPC_HIGHEST(v) (((v) >> 24 >> 24) & 0xffff)

/* #highesta(value) denotes bits 48 through 63 of the indicated value,
   compensating for #lo being treated as a signed number.  */
#define PPC_HIGHESTA(v) PPC_HIGHEST ((v) + 0x8000)

#define SEX16(val) ((((val) & 0xffff) ^ 0x8000) - 0x8000)

static bfd_boolean reg_names_p = TARGET_REG_NAMES_P;

static void ppc_macro (char *, const struct powerpc_macro *);
static void ppc_byte (int);

#if defined (OBJ_XCOFF) || defined (OBJ_ELF)
static void ppc_tc (int);
static void ppc_machine (int);
#endif

#ifdef OBJ_XCOFF
static void ppc_comm (int);
static void ppc_bb (int);
static void ppc_bc (int);
static void ppc_bf (int);
static void ppc_biei (int);
static void ppc_bs (int);
static void ppc_eb (int);
static void ppc_ec (int);
static void ppc_ef (int);
static void ppc_es (int);
static void ppc_csect (int);
static void ppc_change_csect (symbolS *, offsetT);
static void ppc_function (int);
static void ppc_extern (int);
static void ppc_lglobl (int);
static void ppc_section (int);
static void ppc_named_section (int);
static void ppc_stabx (int);
static void ppc_rename (int);
static void ppc_toc (int);
static void ppc_xcoff_cons (int);
static void ppc_vbyte (int);
#endif

#ifdef OBJ_ELF
static void ppc_elf_cons (int);
static void ppc_elf_rdata (int);
static void ppc_elf_lcomm (int);
#endif

#ifdef TE_PE
static void ppc_previous (int);
static void ppc_pdata (int);
static void ppc_ydata (int);
static void ppc_reldata (int);
static void ppc_rdata (int);
static void ppc_ualong (int);
static void ppc_znop (int);
static void ppc_pe_comm (int);
static void ppc_pe_section (int);
static void ppc_pe_function (int);
static void ppc_pe_tocd (int);
#endif

/* Generic assembler global variables which must be defined by all
   targets.  */

#ifdef OBJ_ELF
/* This string holds the chars that always start a comment.  If the
   pre-processor is disabled, these aren't very useful.  The macro
   tc_comment_chars points to this.  We use this, rather than the
   usual comment_chars, so that we can switch for Solaris conventions.  */
static const char ppc_solaris_comment_chars[] = "#!";
static const char ppc_eabi_comment_chars[] = "#";

#ifdef TARGET_SOLARIS_COMMENT
const char *ppc_comment_chars = ppc_solaris_comment_chars;
#else
const char *ppc_comment_chars = ppc_eabi_comment_chars;
#endif
#else
const char comment_chars[] = "#";
#endif

/* Characters which start a comment at the beginning of a line.  */
const char line_comment_chars[] = "#";

/* Characters which may be used to separate multiple commands on a
   single line.  */
const char line_separator_chars[] = ";";

/* Characters which are used to indicate an exponent in a floating
   point number.  */
const char EXP_CHARS[] = "eE";

/* Characters which mean that a number is a floating point constant,
   as in 0d1.0.  */
const char FLT_CHARS[] = "dD";

/* Anything that can start an operand needs to be mentioned here,
   to stop the input scrubber eating whitespace.  */
const char ppc_symbol_chars[] = "%[";

/* The dwarf2 data alignment, adjusted for 32 or 64 bit.  */
int ppc_cie_data_alignment;

/* The target specific pseudo-ops which we support.  */

const pseudo_typeS md_pseudo_table[] =
{
  /* Pseudo-ops which must be overridden.  */
  { "byte",	ppc_byte,	0 },

#ifdef OBJ_XCOFF
  /* Pseudo-ops specific to the RS/6000 XCOFF format.  Some of these
     legitimately belong in the obj-*.c file.  However, XCOFF is based
     on COFF, and is only implemented for the RS/6000.  We just use
     obj-coff.c, and add what we need here.  */
  { "comm",	ppc_comm,	0 },
  { "lcomm",	ppc_comm,	1 },
  { "bb",	ppc_bb,		0 },
  { "bc",	ppc_bc,		0 },
  { "bf",	ppc_bf,		0 },
  { "bi",	ppc_biei,	0 },
  { "bs",	ppc_bs,		0 },
  { "csect",	ppc_csect,	0 },
  { "data",	ppc_section,	'd' },
  { "eb",	ppc_eb,		0 },
  { "ec",	ppc_ec,		0 },
  { "ef",	ppc_ef,		0 },
  { "ei",	ppc_biei,	1 },
  { "es",	ppc_es,		0 },
  { "extern",	ppc_extern,	0 },
  { "function",	ppc_function,	0 },
  { "lglobl",	ppc_lglobl,	0 },
  { "rename",	ppc_rename,	0 },
  { "section",	ppc_named_section, 0 },
  { "stabx",	ppc_stabx,	0 },
  { "text",	ppc_section,	't' },
  { "toc",	ppc_toc,	0 },
  { "long",	ppc_xcoff_cons,	2 },
  { "llong",	ppc_xcoff_cons,	3 },
  { "word",	ppc_xcoff_cons,	1 },
  { "short",	ppc_xcoff_cons,	1 },
  { "vbyte",    ppc_vbyte,	0 },
#endif

#ifdef OBJ_ELF
  { "llong",	ppc_elf_cons,	8 },
  { "quad",	ppc_elf_cons,	8 },
  { "long",	ppc_elf_cons,	4 },
  { "word",	ppc_elf_cons,	2 },
  { "short",	ppc_elf_cons,	2 },
  { "rdata",	ppc_elf_rdata,	0 },
  { "rodata",	ppc_elf_rdata,	0 },
  { "lcomm",	ppc_elf_lcomm,	0 },
#endif

#ifdef TE_PE
  /* Pseudo-ops specific to the Windows NT PowerPC PE (coff) format.  */
  { "previous", ppc_previous,   0 },
  { "pdata",    ppc_pdata,      0 },
  { "ydata",    ppc_ydata,      0 },
  { "reldata",  ppc_reldata,    0 },
  { "rdata",    ppc_rdata,      0 },
  { "ualong",   ppc_ualong,     0 },
  { "znop",     ppc_znop,       0 },
  { "comm",	ppc_pe_comm,	0 },
  { "lcomm",	ppc_pe_comm,	1 },
  { "section",  ppc_pe_section, 0 },
  { "function",	ppc_pe_function,0 },
  { "tocd",     ppc_pe_tocd,    0 },
#endif

#if defined (OBJ_XCOFF) || defined (OBJ_ELF)
  { "tc",	ppc_tc,		0 },
  { "machine",  ppc_machine,    0 },
#endif

  { NULL,	NULL,		0 }
};


/* Predefined register names if -mregnames (or default for Windows NT).
   In general, there are lots of them, in an attempt to be compatible
   with a number of other Windows NT assemblers.  */

/* Structure to hold information about predefined registers.  */
struct pd_reg
  {
    char *name;
    int value;
  };

/* List of registers that are pre-defined:

   Each general register has predefined names of the form:
   1. r<reg_num> which has the value <reg_num>.
   2. r.<reg_num> which has the value <reg_num>.

   Each floating point register has predefined names of the form:
   1. f<reg_num> which has the value <reg_num>.
   2. f.<reg_num> which has the value <reg_num>.

   Each vector unit register has predefined names of the form:
   1. v<reg_num> which has the value <reg_num>.
   2. v.<reg_num> which has the value <reg_num>.

   Each condition register has predefined names of the form:
   1. cr<reg_num> which has the value <reg_num>.
   2. cr.<reg_num> which has the value <reg_num>.

   There are individual registers as well:
   sp or r.sp     has the value 1
   rtoc or r.toc  has the value 2
   fpscr          has the value 0
   xer            has the value 1
   lr             has the value 8
   ctr            has the value 9
   pmr            has the value 0
   dar            has the value 19
   dsisr          has the value 18
   dec            has the value 22
   sdr1           has the value 25
   srr0           has the value 26
   srr1           has the value 27

   The table is sorted. Suitable for searching by a binary search.  */

static const struct pd_reg pre_defined_registers[] =
{
  { "cr.0", 0 },    /* Condition Registers */
  { "cr.1", 1 },
  { "cr.2", 2 },
  { "cr.3", 3 },
  { "cr.4", 4 },
  { "cr.5", 5 },
  { "cr.6", 6 },
  { "cr.7", 7 },

  { "cr0", 0 },
  { "cr1", 1 },
  { "cr2", 2 },
  { "cr3", 3 },
  { "cr4", 4 },
  { "cr5", 5 },
  { "cr6", 6 },
  { "cr7", 7 },

  { "ctr", 9 },

  { "dar", 19 },    /* Data Access Register */
  { "dec", 22 },    /* Decrementer */
  { "dsisr", 18 },  /* Data Storage Interrupt Status Register */

  { "f.0", 0 },     /* Floating point registers */
  { "f.1", 1 },
  { "f.10", 10 },
  { "f.11", 11 },
  { "f.12", 12 },
  { "f.13", 13 },
  { "f.14", 14 },
  { "f.15", 15 },
  { "f.16", 16 },
  { "f.17", 17 },
  { "f.18", 18 },
  { "f.19", 19 },
  { "f.2", 2 },
  { "f.20", 20 },
  { "f.21", 21 },
  { "f.22", 22 },
  { "f.23", 23 },
  { "f.24", 24 },
  { "f.25", 25 },
  { "f.26", 26 },
  { "f.27", 27 },
  { "f.28", 28 },
  { "f.29", 29 },
  { "f.3", 3 },
  { "f.30", 30 },
  { "f.31", 31 },
  { "f.4", 4 },
  { "f.5", 5 },
  { "f.6", 6 },
  { "f.7", 7 },
  { "f.8", 8 },
  { "f.9", 9 },

  { "f0", 0 },
  { "f1", 1 },
  { "f10", 10 },
  { "f11", 11 },
  { "f12", 12 },
  { "f13", 13 },
  { "f14", 14 },
  { "f15", 15 },
  { "f16", 16 },
  { "f17", 17 },
  { "f18", 18 },
  { "f19", 19 },
  { "f2", 2 },
  { "f20", 20 },
  { "f21", 21 },
  { "f22", 22 },
  { "f23", 23 },
  { "f24", 24 },
  { "f25", 25 },
  { "f26", 26 },
  { "f27", 27 },
  { "f28", 28 },
  { "f29", 29 },
  { "f3", 3 },
  { "f30", 30 },
  { "f31", 31 },
  { "f4", 4 },
  { "f5", 5 },
  { "f6", 6 },
  { "f7", 7 },
  { "f8", 8 },
  { "f9", 9 },

  { "fpscr", 0 },

  { "lr", 8 },     /* Link Register */

  { "pmr", 0 },

  { "r.0", 0 },    /* General Purpose Registers */
  { "r.1", 1 },
  { "r.10", 10 },
  { "r.11", 11 },
  { "r.12", 12 },
  { "r.13", 13 },
  { "r.14", 14 },
  { "r.15", 15 },
  { "r.16", 16 },
  { "r.17", 17 },
  { "r.18", 18 },
  { "r.19", 19 },
  { "r.2", 2 },
  { "r.20", 20 },
  { "r.21", 21 },
  { "r.22", 22 },
  { "r.23", 23 },
  { "r.24", 24 },
  { "r.25", 25 },
  { "r.26", 26 },
  { "r.27", 27 },
  { "r.28", 28 },
  { "r.29", 29 },
  { "r.3", 3 },
  { "r.30", 30 },
  { "r.31", 31 },
  { "r.4", 4 },
  { "r.5", 5 },
  { "r.6", 6 },
  { "r.7", 7 },
  { "r.8", 8 },
  { "r.9", 9 },

  { "r.sp", 1 },   /* Stack Pointer */

  { "r.toc", 2 },  /* Pointer to the table of contents */

  { "r0", 0 },     /* More general purpose registers */
  { "r1", 1 },
  { "r10", 10 },
  { "r11", 11 },
  { "r12", 12 },
  { "r13", 13 },
  { "r14", 14 },
  { "r15", 15 },
  { "r16", 16 },
  { "r17", 17 },
  { "r18", 18 },
  { "r19", 19 },
  { "r2", 2 },
  { "r20", 20 },
  { "r21", 21 },
  { "r22", 22 },
  { "r23", 23 },
  { "r24", 24 },
  { "r25", 25 },
  { "r26", 26 },
  { "r27", 27 },
  { "r28", 28 },
  { "r29", 29 },
  { "r3", 3 },
  { "r30", 30 },
  { "r31", 31 },
  { "r4", 4 },
  { "r5", 5 },
  { "r6", 6 },
  { "r7", 7 },
  { "r8", 8 },
  { "r9", 9 },

  { "rtoc", 2 },  /* Table of contents */

  { "sdr1", 25 }, /* Storage Description Register 1 */

  { "sp", 1 },

  { "srr0", 26 }, /* Machine Status Save/Restore Register 0 */
  { "srr1", 27 }, /* Machine Status Save/Restore Register 1 */

  { "v.0", 0 },     /* Vector registers */
  { "v.1", 1 },
  { "v.10", 10 },
  { "v.11", 11 },
  { "v.12", 12 },
  { "v.13", 13 },
  { "v.14", 14 },
  { "v.15", 15 },
  { "v.16", 16 },
  { "v.17", 17 },
  { "v.18", 18 },
  { "v.19", 19 },
  { "v.2", 2 },
  { "v.20", 20 },
  { "v.21", 21 },
  { "v.22", 22 },
  { "v.23", 23 },
  { "v.24", 24 },
  { "v.25", 25 },
  { "v.26", 26 },
  { "v.27", 27 },
  { "v.28", 28 },
  { "v.29", 29 },
  { "v.3", 3 },
  { "v.30", 30 },
  { "v.31", 31 },
  { "v.4", 4 },
  { "v.5", 5 },
  { "v.6", 6 },
  { "v.7", 7 },
  { "v.8", 8 },
  { "v.9", 9 },

  { "v0", 0 },
  { "v1", 1 },
  { "v10", 10 },
  { "v11", 11 },
  { "v12", 12 },
  { "v13", 13 },
  { "v14", 14 },
  { "v15", 15 },
  { "v16", 16 },
  { "v17", 17 },
  { "v18", 18 },
  { "v19", 19 },
  { "v2", 2 },
  { "v20", 20 },
  { "v21", 21 },
  { "v22", 22 },
  { "v23", 23 },
  { "v24", 24 },
  { "v25", 25 },
  { "v26", 26 },
  { "v27", 27 },
  { "v28", 28 },
  { "v29", 29 },
  { "v3", 3 },
  { "v30", 30 },
  { "v31", 31 },
  { "v4", 4 },
  { "v5", 5 },
  { "v6", 6 },
  { "v7", 7 },
  { "v8", 8 },
  { "v9", 9 },

  { "xer", 1 },

};

#define REG_NAME_CNT	(sizeof (pre_defined_registers) / sizeof (struct pd_reg))

/* Given NAME, find the register number associated with that name, return
   the integer value associated with the given name or -1 on failure.  */

static int
reg_name_search (const struct pd_reg *regs, int regcount, const char *name)
{
  int middle, low, high;
  int cmp;

  low = 0;
  high = regcount - 1;

  do
    {
      middle = (low + high) / 2;
      cmp = strcasecmp (name, regs[middle].name);
      if (cmp < 0)
	high = middle - 1;
      else if (cmp > 0)
	low = middle + 1;
      else
	return regs[middle].value;
    }
  while (low <= high);

  return -1;
}

/*
 * Summary of register_name.
 *
 * in:	Input_line_pointer points to 1st char of operand.
 *
 * out:	A expressionS.
 *      The operand may have been a register: in this case, X_op == O_register,
 *      X_add_number is set to the register number, and truth is returned.
 *	Input_line_pointer->(next non-blank) char after operand, or is in its
 *      original state.
 */

static bfd_boolean
register_name (expressionS *expressionP)
{
  int reg_number;
  char *name;
  char *start;
  char c;

  /* Find the spelling of the operand.  */
  start = name = input_line_pointer;
  if (name[0] == '%' && ISALPHA (name[1]))
    name = ++input_line_pointer;

  else if (!reg_names_p || !ISALPHA (name[0]))
    return FALSE;

  c = get_symbol_end ();
  reg_number = reg_name_search (pre_defined_registers, REG_NAME_CNT, name);

  /* Put back the delimiting char.  */
  *input_line_pointer = c;

  /* Look to see if it's in the register table.  */
  if (reg_number >= 0)
    {
      expressionP->X_op = O_register;
      expressionP->X_add_number = reg_number;

      /* Make the rest nice.  */
      expressionP->X_add_symbol = NULL;
      expressionP->X_op_symbol = NULL;
      return TRUE;
    }

  /* Reset the line as if we had not done anything.  */
  input_line_pointer = start;
  return FALSE;
}

/* This function is called for each symbol seen in an expression.  It
   handles the special parsing which PowerPC assemblers are supposed
   to use for condition codes.  */

/* Whether to do the special parsing.  */
static bfd_boolean cr_operand;

/* Names to recognize in a condition code.  This table is sorted.  */
static const struct pd_reg cr_names[] =
{
  { "cr0", 0 },
  { "cr1", 1 },
  { "cr2", 2 },
  { "cr3", 3 },
  { "cr4", 4 },
  { "cr5", 5 },
  { "cr6", 6 },
  { "cr7", 7 },
  { "eq", 2 },
  { "gt", 1 },
  { "lt", 0 },
  { "so", 3 },
  { "un", 3 }
};

/* Parsing function.  This returns non-zero if it recognized an
   expression.  */

int
ppc_parse_name (const char *name, expressionS *expr)
{
  int val;

  if (! cr_operand)
    return 0;

  val = reg_name_search (cr_names, sizeof cr_names / sizeof cr_names[0],
			 name);
  if (val < 0)
    return 0;

  expr->X_op = O_constant;
  expr->X_add_number = val;

  return 1;
}

/* Local variables.  */

/* The type of processor we are assembling for.  This is one or more
   of the PPC_OPCODE flags defined in opcode/ppc.h.  */
static unsigned long ppc_cpu = PPC_OPCODE_ANY;

/* Whether to target xcoff64/elf64.  */
static unsigned int ppc_obj64 = BFD_DEFAULT_TARGET_SIZE == 64;

/* Opcode hash table.  */
static struct hash_control *ppc_hash;

/* Macro hash table.  */
static struct hash_control *ppc_macro_hash;

#ifdef OBJ_ELF
/* What type of shared library support to use.  */
static enum { SHLIB_NONE, SHLIB_PIC, SHLIB_MRELOCATABLE } shlib = SHLIB_NONE;

/* Flags to set in the elf header.  */
static flagword ppc_flags = 0;

/* Whether this is Solaris or not.  */
#ifdef TARGET_SOLARIS_COMMENT
#define SOLARIS_P TRUE
#else
#define SOLARIS_P FALSE
#endif

static bfd_boolean msolaris = SOLARIS_P;
#endif

#ifdef OBJ_XCOFF

/* The RS/6000 assembler uses the .csect pseudo-op to generate code
   using a bunch of different sections.  These assembler sections,
   however, are all encompassed within the .text or .data sections of
   the final output file.  We handle this by using different
   subsegments within these main segments.  */

/* Next subsegment to allocate within the .text segment.  */
static subsegT ppc_text_subsegment = 2;

/* Linked list of csects in the text section.  */
static symbolS *ppc_text_csects;

/* Next subsegment to allocate within the .data segment.  */
static subsegT ppc_data_subsegment = 2;

/* Linked list of csects in the data section.  */
static symbolS *ppc_data_csects;

/* The current csect.  */
static symbolS *ppc_current_csect;

/* The RS/6000 assembler uses a TOC which holds addresses of functions
   and variables.  Symbols are put in the TOC with the .tc pseudo-op.
   A special relocation is used when accessing TOC entries.  We handle
   the TOC as a subsegment within the .data segment.  We set it up if
   we see a .toc pseudo-op, and save the csect symbol here.  */
static symbolS *ppc_toc_csect;

/* The first frag in the TOC subsegment.  */
static fragS *ppc_toc_frag;

/* The first frag in the first subsegment after the TOC in the .data
   segment.  NULL if there are no subsegments after the TOC.  */
static fragS *ppc_after_toc_frag;

/* The current static block.  */
static symbolS *ppc_current_block;

/* The COFF debugging section; set by md_begin.  This is not the
   .debug section, but is instead the secret BFD section which will
   cause BFD to set the section number of a symbol to N_DEBUG.  */
static asection *ppc_coff_debug_section;

#endif /* OBJ_XCOFF */

#ifdef TE_PE

/* Various sections that we need for PE coff support.  */
static segT ydata_section;
static segT pdata_section;
static segT reldata_section;
static segT rdata_section;
static segT tocdata_section;

/* The current section and the previous section. See ppc_previous.  */
static segT ppc_previous_section;
static segT ppc_current_section;

#endif /* TE_PE */

#ifdef OBJ_ELF
symbolS *GOT_symbol;		/* Pre-defined "_GLOBAL_OFFSET_TABLE" */
#define PPC_APUINFO_ISEL	0x40
#define PPC_APUINFO_PMR		0x41
#define PPC_APUINFO_RFMCI	0x42
#define PPC_APUINFO_CACHELCK	0x43
#define PPC_APUINFO_SPE		0x100
#define PPC_APUINFO_EFS		0x101
#define PPC_APUINFO_BRLOCK	0x102

/*
 * We keep a list of APUinfo
 */
unsigned long *ppc_apuinfo_list;
unsigned int ppc_apuinfo_num;
unsigned int ppc_apuinfo_num_alloc;
#endif /* OBJ_ELF */

#ifdef OBJ_ELF
const char *const md_shortopts = "b:l:usm:K:VQ:";
#else
const char *const md_shortopts = "um:";
#endif
const struct option md_longopts[] = {
  {NULL, no_argument, NULL, 0}
};
const size_t md_longopts_size = sizeof (md_longopts);


/* Handle -m options that set cpu type, and .machine arg.  */

static int
parse_cpu (const char *arg)
{
  /* -mpwrx and -mpwr2 mean to assemble for the IBM POWER/2
     (RIOS2).  */
  if (strcmp (arg, "pwrx") == 0 || strcmp (arg, "pwr2") == 0)
    ppc_cpu = PPC_OPCODE_POWER | PPC_OPCODE_POWER2 | PPC_OPCODE_32;
  /* -mpwr means to assemble for the IBM POWER (RIOS1).  */
  else if (strcmp (arg, "pwr") == 0)
    ppc_cpu = PPC_OPCODE_POWER | PPC_OPCODE_32;
  /* -m601 means to assemble for the PowerPC 601, which includes
     instructions that are holdovers from the Power.  */
  else if (strcmp (arg, "601") == 0)
    ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC
	       | PPC_OPCODE_601 | PPC_OPCODE_32);
  /* -mppc, -mppc32, -m603, and -m604 mean to assemble for the
     PowerPC 603/604.  */
  else if (strcmp (arg, "ppc") == 0
	   || strcmp (arg, "ppc32") == 0
	   || strcmp (arg, "603") == 0
	   || strcmp (arg, "604") == 0)
    ppc_cpu = PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC | PPC_OPCODE_32;
  /* -m403 and -m405 mean to assemble for the PowerPC 403/405.  */
  else if (strcmp (arg, "403") == 0
	   || strcmp (arg, "405") == 0)
    ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC
	       | PPC_OPCODE_403 | PPC_OPCODE_32);
  else if (strcmp (arg, "440") == 0)
    ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_32
	       | PPC_OPCODE_440 | PPC_OPCODE_ISEL | PPC_OPCODE_RFMCI);
  else if (strcmp (arg, "7400") == 0
	   || strcmp (arg, "7410") == 0
	   || strcmp (arg, "7450") == 0
	   || strcmp (arg, "7455") == 0)
    ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC
	       | PPC_OPCODE_ALTIVEC | PPC_OPCODE_32);
  else if (strcmp (arg, "e300") == 0)
    ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC | PPC_OPCODE_32
	       | PPC_OPCODE_E300);
  else if (strcmp (arg, "altivec") == 0)
    {
      if (ppc_cpu == 0)
	ppc_cpu = PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC | PPC_OPCODE_ALTIVEC;
      else
	ppc_cpu |= PPC_OPCODE_ALTIVEC;
    }
  else if (strcmp (arg, "e500") == 0 || strcmp (arg, "e500x2") == 0)
    {
      ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_SPE
		 | PPC_OPCODE_ISEL | PPC_OPCODE_EFS | PPC_OPCODE_BRLOCK
		 | PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK
		 | PPC_OPCODE_RFMCI);
    }
  else if (strcmp (arg, "spe") == 0)
    {
      if (ppc_cpu == 0)
	ppc_cpu = PPC_OPCODE_PPC | PPC_OPCODE_SPE | PPC_OPCODE_EFS;
      else
	ppc_cpu |= PPC_OPCODE_SPE;
    }
  /* -mppc64 and -m620 mean to assemble for the 64-bit PowerPC
     620.  */
  else if (strcmp (arg, "ppc64") == 0 || strcmp (arg, "620") == 0)
    {
      ppc_cpu = PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC | PPC_OPCODE_64;
    }
  else if (strcmp (arg, "ppc64bridge") == 0)
    {
      ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC
		 | PPC_OPCODE_64_BRIDGE | PPC_OPCODE_64);
    }
  /* -mbooke/-mbooke32 mean enable 32-bit BookE support.  */
  else if (strcmp (arg, "booke") == 0 || strcmp (arg, "booke32") == 0)
    {
      ppc_cpu = PPC_OPCODE_PPC | PPC_OPCODE_BOOKE | PPC_OPCODE_32;
    }
  /* -mbooke64 means enable 64-bit BookE support.  */
  else if (strcmp (arg, "booke64") == 0)
    {
      ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_BOOKE
		 | PPC_OPCODE_BOOKE64 | PPC_OPCODE_64);
    }
  else if (strcmp (arg, "power4") == 0)
    {
      ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC
		 | PPC_OPCODE_64 | PPC_OPCODE_POWER4);
    }
  else if (strcmp (arg, "power5") == 0)
    {
      ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC
		 | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		 | PPC_OPCODE_POWER5);
    }
  else if (strcmp (arg, "power6") == 0)
    {
      ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC
		 | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		 | PPC_OPCODE_POWER5 | PPC_OPCODE_POWER6);
    }
  else if (strcmp (arg, "cell") == 0)
    {
      ppc_cpu = (PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC
		 | PPC_OPCODE_64 | PPC_OPCODE_POWER4
		 | PPC_OPCODE_CELL);
    }
  /* -mcom means assemble for the common intersection between Power
     and PowerPC.  At present, we just allow the union, rather
     than the intersection.  */
  else if (strcmp (arg, "com") == 0)
    ppc_cpu = PPC_OPCODE_COMMON | PPC_OPCODE_32;
  /* -many means to assemble for any architecture (PWR/PWRX/PPC).  */
  else if (strcmp (arg, "any") == 0)
    ppc_cpu |= PPC_OPCODE_ANY;
  else
    return 0;

  return 1;
}

int
md_parse_option (int c, char *arg)
{
  switch (c)
    {
    case 'u':
      /* -u means that any undefined symbols should be treated as
	 external, which is the default for gas anyhow.  */
      break;

#ifdef OBJ_ELF
    case 'l':
      /* Solaris as takes -le (presumably for little endian).  For completeness
	 sake, recognize -be also.  */
      if (strcmp (arg, "e") == 0)
	{
	  target_big_endian = 0;
	  set_target_endian = 1;
	}
      else
	return 0;

      break;

    case 'b':
      if (strcmp (arg, "e") == 0)
	{
	  target_big_endian = 1;
	  set_target_endian = 1;
	}
      else
	return 0;

      break;

    case 'K':
      /* Recognize -K PIC.  */
      if (strcmp (arg, "PIC") == 0 || strcmp (arg, "pic") == 0)
	{
	  shlib = SHLIB_PIC;
	  ppc_flags |= EF_PPC_RELOCATABLE_LIB;
	}
      else
	return 0;

      break;
#endif

      /* a64 and a32 determine whether to use XCOFF64 or XCOFF32.  */
    case 'a':
      if (strcmp (arg, "64") == 0)
	{
#ifdef BFD64
	  ppc_obj64 = 1;
#else
	  as_fatal (_("%s unsupported"), "-a64");
#endif
	}
      else if (strcmp (arg, "32") == 0)
	ppc_obj64 = 0;
      else
	return 0;
      break;

    case 'm':
      if (parse_cpu (arg))
	;

      else if (strcmp (arg, "regnames") == 0)
	reg_names_p = TRUE;

      else if (strcmp (arg, "no-regnames") == 0)
	reg_names_p = FALSE;

#ifdef OBJ_ELF
      /* -mrelocatable/-mrelocatable-lib -- warn about initializations
	 that require relocation.  */
      else if (strcmp (arg, "relocatable") == 0)
	{
	  shlib = SHLIB_MRELOCATABLE;
	  ppc_flags |= EF_PPC_RELOCATABLE;
	}

      else if (strcmp (arg, "relocatable-lib") == 0)
	{
	  shlib = SHLIB_MRELOCATABLE;
	  ppc_flags |= EF_PPC_RELOCATABLE_LIB;
	}

      /* -memb, set embedded bit.  */
      else if (strcmp (arg, "emb") == 0)
	ppc_flags |= EF_PPC_EMB;

      /* -mlittle/-mbig set the endianess.  */
      else if (strcmp (arg, "little") == 0
	       || strcmp (arg, "little-endian") == 0)
	{
	  target_big_endian = 0;
	  set_target_endian = 1;
	}

      else if (strcmp (arg, "big") == 0 || strcmp (arg, "big-endian") == 0)
	{
	  target_big_endian = 1;
	  set_target_endian = 1;
	}

      else if (strcmp (arg, "solaris") == 0)
	{
	  msolaris = TRUE;
	  ppc_comment_chars = ppc_solaris_comment_chars;
	}

      else if (strcmp (arg, "no-solaris") == 0)
	{
	  msolaris = FALSE;
	  ppc_comment_chars = ppc_eabi_comment_chars;
	}
#endif
      else
	{
	  as_bad (_("invalid switch -m%s"), arg);
	  return 0;
	}
      break;

#ifdef OBJ_ELF
      /* -V: SVR4 argument to print version ID.  */
    case 'V':
      print_version_id ();
      break;

      /* -Qy, -Qn: SVR4 arguments controlling whether a .comment section
	 should be emitted or not.  FIXME: Not implemented.  */
    case 'Q':
      break;

      /* Solaris takes -s to specify that .stabs go in a .stabs section,
	 rather than .stabs.excl, which is ignored by the linker.
	 FIXME: Not implemented.  */
    case 's':
      if (arg)
	return 0;

      break;
#endif

    default:
      return 0;
    }

  return 1;
}

void
md_show_usage (FILE *stream)
{
  fprintf (stream, _("\
PowerPC options:\n\
-a32			generate ELF32/XCOFF32\n\
-a64			generate ELF64/XCOFF64\n\
-u			ignored\n\
-mpwrx, -mpwr2		generate code for POWER/2 (RIOS2)\n\
-mpwr			generate code for POWER (RIOS1)\n\
-m601			generate code for PowerPC 601\n\
-mppc, -mppc32, -m603, -m604\n\
			generate code for PowerPC 603/604\n\
-m403, -m405		generate code for PowerPC 403/405\n\
-m440			generate code for PowerPC 440\n\
-m7400, -m7410, -m7450, -m7455\n\
			generate code For PowerPC 7400/7410/7450/7455\n"));
  fprintf (stream, _("\
-mppc64, -m620		generate code for PowerPC 620/625/630\n\
-mppc64bridge		generate code for PowerPC 64, including bridge insns\n\
-mbooke64		generate code for 64-bit PowerPC BookE\n\
-mbooke, mbooke32	generate code for 32-bit PowerPC BookE\n\
-mpower4		generate code for Power4 architecture\n\
-mpower5		generate code for Power5 architecture\n\
-mpower6		generate code for Power6 architecture\n\
-mcell			generate code for Cell Broadband Engine architecture\n\
-mcom			generate code Power/PowerPC common instructions\n\
-many			generate code for any architecture (PWR/PWRX/PPC)\n"));
  fprintf (stream, _("\
-maltivec		generate code for AltiVec\n\
-me300			generate code for PowerPC e300 family\n\
-me500, -me500x2	generate code for Motorola e500 core complex\n\
-mspe			generate code for Motorola SPE instructions\n\
-mregnames		Allow symbolic names for registers\n\
-mno-regnames		Do not allow symbolic names for registers\n"));
#ifdef OBJ_ELF
  fprintf (stream, _("\
-mrelocatable		support for GCC's -mrelocatble option\n\
-mrelocatable-lib	support for GCC's -mrelocatble-lib option\n\
-memb			set PPC_EMB bit in ELF flags\n\
-mlittle, -mlittle-endian, -l, -le\n\
			generate code for a little endian machine\n\
-mbig, -mbig-endian, -b, -be\n\
			generate code for a big endian machine\n\
-msolaris		generate code for Solaris\n\
-mno-solaris		do not generate code for Solaris\n\
-V			print assembler version number\n\
-Qy, -Qn		ignored\n"));
#endif
}

/* Set ppc_cpu if it is not already set.  */

static void
ppc_set_cpu (void)
{
  const char *default_os  = TARGET_OS;
  const char *default_cpu = TARGET_CPU;

  if ((ppc_cpu & ~PPC_OPCODE_ANY) == 0)
    {
      if (ppc_obj64)
	ppc_cpu |= PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC | PPC_OPCODE_64;
      else if (strncmp (default_os, "aix", 3) == 0
	       && default_os[3] >= '4' && default_os[3] <= '9')
	ppc_cpu |= PPC_OPCODE_COMMON | PPC_OPCODE_32;
      else if (strncmp (default_os, "aix3", 4) == 0)
	ppc_cpu |= PPC_OPCODE_POWER | PPC_OPCODE_32;
      else if (strcmp (default_cpu, "rs6000") == 0)
	ppc_cpu |= PPC_OPCODE_POWER | PPC_OPCODE_32;
      else if (strncmp (default_cpu, "powerpc", 7) == 0)
	ppc_cpu |= PPC_OPCODE_PPC | PPC_OPCODE_CLASSIC | PPC_OPCODE_32;
      else
	as_fatal (_("Unknown default cpu = %s, os = %s"),
		  default_cpu, default_os);
    }
}

/* Figure out the BFD architecture to use.  This function and ppc_mach
   are called well before md_begin, when the output file is opened.  */

enum bfd_architecture
ppc_arch (void)
{
  const char *default_cpu = TARGET_CPU;
  ppc_set_cpu ();

  if ((ppc_cpu & PPC_OPCODE_PPC) != 0)
    return bfd_arch_powerpc;
  else if ((ppc_cpu & PPC_OPCODE_POWER) != 0)
    return bfd_arch_rs6000;
  else if ((ppc_cpu & (PPC_OPCODE_COMMON | PPC_OPCODE_ANY)) != 0)
    {
      if (strcmp (default_cpu, "rs6000") == 0)
	return bfd_arch_rs6000;
      else if (strncmp (default_cpu, "powerpc", 7) == 0)
	return bfd_arch_powerpc;
    }

  as_fatal (_("Neither Power nor PowerPC opcodes were selected."));
  return bfd_arch_unknown;
}

unsigned long
ppc_mach (void)
{
  if (ppc_obj64)
    return bfd_mach_ppc64;
  else if (ppc_arch () == bfd_arch_rs6000)
    return bfd_mach_rs6k;
  else
    return bfd_mach_ppc;
}

extern char*
ppc_target_format (void)
{
#ifdef OBJ_COFF
#ifdef TE_PE
  return target_big_endian ? "pe-powerpc" : "pe-powerpcle";
#elif TE_POWERMAC
  return "xcoff-powermac";
#else
#  ifdef TE_AIX5
    return (ppc_obj64 ? "aix5coff64-rs6000" : "aixcoff-rs6000");
#  else
    return (ppc_obj64 ? "aixcoff64-rs6000" : "aixcoff-rs6000");
#  endif
#endif
#endif
#ifdef OBJ_ELF
# ifdef TE_VXWORKS
  return "elf32-powerpc-vxworks";
# else
  return (target_big_endian
	  ? (ppc_obj64 ? "elf64-powerpc-freebsd" : "elf32-powerpc-freebsd")
	  : (ppc_obj64 ? "elf64-powerpcle" : "elf32-powerpcle"));
# endif
#endif
}

/* Insert opcodes and macros into hash tables.  Called at startup and
   for .cpu pseudo.  */

static void
ppc_setup_opcodes (void)
{
  const struct powerpc_opcode *op;
  const struct powerpc_opcode *op_end;
  const struct powerpc_macro *macro;
  const struct powerpc_macro *macro_end;
  bfd_boolean bad_insn = FALSE;

  if (ppc_hash != NULL)
    hash_die (ppc_hash);
  if (ppc_macro_hash != NULL)
    hash_die (ppc_macro_hash);

  /* Insert the opcodes into a hash table.  */
  ppc_hash = hash_new ();

  if (ENABLE_CHECKING)
    {
      unsigned int i;

      /* Check operand masks.  Code here and in the disassembler assumes
	 all the 1's in the mask are contiguous.  */
      for (i = 0; i < num_powerpc_operands; ++i)
	{
	  unsigned long mask = powerpc_operands[i].bitm;
	  unsigned long right_bit;
	  unsigned int j;

	  right_bit = mask & -mask;
	  mask += right_bit;
	  right_bit = mask & -mask;
	  if (mask != right_bit)
	    {
	      as_bad (_("powerpc_operands[%d].bitm invalid"), i);
	      bad_insn = TRUE;
	    }
	  for (j = i + 1; j < num_powerpc_operands; ++j)
	    if (memcmp (&powerpc_operands[i], &powerpc_operands[j],
			sizeof (powerpc_operands[0])) == 0)
	      {
		as_bad (_("powerpc_operands[%d] duplicates powerpc_operands[%d]"),
			j, i);
		bad_insn = TRUE;
	      }
	}
    }

  op_end = powerpc_opcodes + powerpc_num_opcodes;
  for (op = powerpc_opcodes; op < op_end; op++)
    {
      if (ENABLE_CHECKING)
	{
	  const unsigned char *o;
	  unsigned long omask = op->mask;

	  /* The mask had better not trim off opcode bits.  */
	  if ((op->opcode & omask) != op->opcode)
	    {
	      as_bad (_("mask trims opcode bits for %s"),
		      op->name);
	      bad_insn = TRUE;
	    }

	  /* The operands must not overlap the opcode or each other.  */
	  for (o = op->operands; *o; ++o)
	    if (*o >= num_powerpc_operands)
	      {
		as_bad (_("operand index error for %s"),
			op->name);
		bad_insn = TRUE;
	      }
	    else
	      {
		const struct powerpc_operand *operand = &powerpc_operands[*o];
		if (operand->shift >= 0)
		  {
		    unsigned long mask = operand->bitm << operand->shift;
		    if (omask & mask)
		      {
			as_bad (_("operand %d overlap in %s"),
				(int) (o - op->operands), op->name);
			bad_insn = TRUE;
		      }
		    omask |= mask;
		  }
	      }
	}

      if ((op->flags & ppc_cpu & ~(PPC_OPCODE_32 | PPC_OPCODE_64)) != 0
	  && ((op->flags & (PPC_OPCODE_32 | PPC_OPCODE_64)) == 0
	      || ((op->flags & (PPC_OPCODE_32 | PPC_OPCODE_64))
		  == (ppc_cpu & (PPC_OPCODE_32 | PPC_OPCODE_64)))
	      || (ppc_cpu & PPC_OPCODE_64_BRIDGE) != 0)
	  /* Certain instructions (eg: extsw) do not exist in the
	     32-bit BookE instruction set, but they do exist in the
	     64-bit BookE instruction set, and other PPC instruction
	     sets.  Check to see if the opcode has the BOOKE64 flag set.
	     If it does make sure that the target CPU is not the BookE32.  */
	  && ((op->flags & PPC_OPCODE_BOOKE64) == 0
	      || (ppc_cpu & PPC_OPCODE_BOOKE64) == PPC_OPCODE_BOOKE64
	      || (ppc_cpu & PPC_OPCODE_BOOKE) == 0)
	  && ((op->flags & (PPC_OPCODE_POWER4 | PPC_OPCODE_NOPOWER4)) == 0
	      || ((op->flags & PPC_OPCODE_POWER4)
		  == (ppc_cpu & PPC_OPCODE_POWER4)))
	  && ((op->flags & PPC_OPCODE_POWER5) == 0
	      || ((op->flags & PPC_OPCODE_POWER5)
		  == (ppc_cpu & PPC_OPCODE_POWER5)))
	  && ((op->flags & PPC_OPCODE_POWER6) == 0
	      || ((op->flags & PPC_OPCODE_POWER6)
		  == (ppc_cpu & PPC_OPCODE_POWER6))))
	{
	  const char *retval;

	  retval = hash_insert (ppc_hash, op->name, (void *) op);
	  if (retval != NULL)
	    {
	      /* Ignore Power duplicates for -m601.  */
	      if ((ppc_cpu & PPC_OPCODE_601) != 0
		  && (op->flags & PPC_OPCODE_POWER) != 0)
		continue;

	      as_bad (_("duplicate instruction %s"),
		      op->name);
	      bad_insn = TRUE;
	    }
	}
    }

  if ((ppc_cpu & PPC_OPCODE_ANY) != 0)
    for (op = powerpc_opcodes; op < op_end; op++)
      hash_insert (ppc_hash, op->name, (void *) op);

  /* Insert the macros into a hash table.  */
  ppc_macro_hash = hash_new ();

  macro_end = powerpc_macros + powerpc_num_macros;
  for (macro = powerpc_macros; macro < macro_end; macro++)
    {
      if ((macro->flags & ppc_cpu) != 0)
	{
	  const char *retval;

	  retval = hash_insert (ppc_macro_hash, macro->name, (void *) macro);
	  if (retval != (const char *) NULL)
	    {
	      as_bad (_("duplicate macro %s"), macro->name);
	      bad_insn = TRUE;
	    }
	}
    }

  if (bad_insn)
    abort ();
}

/* This function is called when the assembler starts up.  It is called
   after the options have been parsed and the output file has been
   opened.  */

void
md_begin (void)
{
  ppc_set_cpu ();

  ppc_cie_data_alignment = ppc_obj64 ? -8 : -4;

#ifdef OBJ_ELF
  /* Set the ELF flags if desired.  */
  if (ppc_flags && !msolaris)
    bfd_set_private_flags (stdoutput, ppc_flags);
#endif

  ppc_setup_opcodes ();

  /* Tell the main code what the endianness is if it is not overridden
     by the user.  */
  if (!set_target_endian)
    {
      set_target_endian = 1;
      target_big_endian = PPC_BIG_ENDIAN;
    }

#ifdef OBJ_XCOFF
  ppc_coff_debug_section = coff_section_from_bfd_index (stdoutput, N_DEBUG);

  /* Create dummy symbols to serve as initial csects.  This forces the
     text csects to precede the data csects.  These symbols will not
     be output.  */
  ppc_text_csects = symbol_make ("dummy\001");
  symbol_get_tc (ppc_text_csects)->within = ppc_text_csects;
  ppc_data_csects = symbol_make ("dummy\001");
  symbol_get_tc (ppc_data_csects)->within = ppc_data_csects;
#endif

#ifdef TE_PE

  ppc_current_section = text_section;
  ppc_previous_section = 0;

#endif
}

void
ppc_cleanup (void)
{
#ifdef OBJ_ELF
  if (ppc_apuinfo_list == NULL)
    return;

  /* Ok, so write the section info out.  We have this layout:

  byte	data		what
  ----	----		----
  0	8		length of "APUinfo\0"
  4	(n*4)		number of APU's (4 bytes each)
  8	2		note type 2
  12	"APUinfo\0"	name
  20	APU#1		first APU's info
  24	APU#2		second APU's info
  ...	...
  */
  {
    char *p;
    asection *seg = now_seg;
    subsegT subseg = now_subseg;
    asection *apuinfo_secp = (asection *) NULL;
    unsigned int i;

    /* Create the .PPC.EMB.apuinfo section.  */
    apuinfo_secp = subseg_new (".PPC.EMB.apuinfo", 0);
    bfd_set_section_flags (stdoutput,
			   apuinfo_secp,
			   SEC_HAS_CONTENTS | SEC_READONLY);

    p = frag_more (4);
    md_number_to_chars (p, (valueT) 8, 4);

    p = frag_more (4);
    md_number_to_chars (p, (valueT) ppc_apuinfo_num * 4, 4);

    p = frag_more (4);
    md_number_to_chars (p, (valueT) 2, 4);

    p = frag_more (8);
    strcpy (p, "APUinfo");

    for (i = 0; i < ppc_apuinfo_num; i++)
      {
	p = frag_more (4);
	md_number_to_chars (p, (valueT) ppc_apuinfo_list[i], 4);
      }

    frag_align (2, 0, 0);

    /* We probably can't restore the current segment, for there likely
       isn't one yet...  */
    if (seg && subseg)
      subseg_set (seg, subseg);
  }
#endif
}

/* Insert an operand value into an instruction.  */

static unsigned long
ppc_insert_operand (unsigned long insn,
		    const struct powerpc_operand *operand,
		    offsetT val,
		    char *file,
		    unsigned int line)
{
  long min, max, right;

  max = operand->bitm;
  right = max & -max;
  min = 0;

  if ((operand->flags & PPC_OPERAND_SIGNED) != 0)
    {
      if ((operand->flags & PPC_OPERAND_SIGNOPT) == 0)
	max = (max >> 1) & -right;
      min = ~max & -right;
    }

  if ((operand->flags & PPC_OPERAND_PLUS1) != 0)
    max++;

  if ((operand->flags & PPC_OPERAND_NEGATIVE) != 0)
    {
      long tmp = min;
      min = -max;
      max = -tmp;
    }

  if (min <= max)
    {
      /* Some people write constants with the sign extension done by
	 hand but only up to 32 bits.  This shouldn't really be valid,
	 but, to permit this code to assemble on a 64-bit host, we
	 sign extend the 32-bit value to 64 bits if so doing makes the
	 value valid.  */
      if (val > max
	  && (offsetT) (val - 0x80000000 - 0x80000000) >= min
	  && (offsetT) (val - 0x80000000 - 0x80000000) <= max
	  && ((val - 0x80000000 - 0x80000000) & (right - 1)) == 0)
	val = val - 0x80000000 - 0x80000000;

      /* Similarly, people write expressions like ~(1<<15), and expect
	 this to be OK for a 32-bit unsigned value.  */
      else if (val < min
	       && (offsetT) (val + 0x80000000 + 0x80000000) >= min
	       && (offsetT) (val + 0x80000000 + 0x80000000) <= max
	       && ((val + 0x80000000 + 0x80000000) & (right - 1)) == 0)
	val = val + 0x80000000 + 0x80000000;

      else if (val < min
	       || val > max
	       || (val & (right - 1)) != 0)
	as_bad_value_out_of_range (_("operand"), val, min, max, file, line);
    }

  if (operand->insert)
    {
      const char *errmsg;

      errmsg = NULL;
      insn = (*operand->insert) (insn, (long) val, ppc_cpu, &errmsg);
      if (errmsg != (const char *) NULL)
	as_bad_where (file, line, "%s", errmsg);
    }
  else
    insn |= ((long) val & operand->bitm) << operand->shift;

  return insn;
}


#ifdef OBJ_ELF
/* Parse @got, etc. and return the desired relocation.  */
static bfd_reloc_code_real_type
ppc_elf_suffix (char **str_p, expressionS *exp_p)
{
  struct map_bfd {
    char *string;
    unsigned int length : 8;
    unsigned int valid32 : 1;
    unsigned int valid64 : 1;
    unsigned int reloc;
  };

  char ident[20];
  char *str = *str_p;
  char *str2;
  int ch;
  int len;
  const struct map_bfd *ptr;

#define MAP(str, reloc)   { str, sizeof (str) - 1, 1, 1, reloc }
#define MAP32(str, reloc) { str, sizeof (str) - 1, 1, 0, reloc }
#define MAP64(str, reloc) { str, sizeof (str) - 1, 0, 1, reloc }

  static const struct map_bfd mapping[] = {
    MAP ("l",			BFD_RELOC_LO16),
    MAP ("h",			BFD_RELOC_HI16),
    MAP ("ha",			BFD_RELOC_HI16_S),
    MAP ("brtaken",		BFD_RELOC_PPC_B16_BRTAKEN),
    MAP ("brntaken",		BFD_RELOC_PPC_B16_BRNTAKEN),
    MAP ("got",			BFD_RELOC_16_GOTOFF),
    MAP ("got@l",		BFD_RELOC_LO16_GOTOFF),
    MAP ("got@h",		BFD_RELOC_HI16_GOTOFF),
    MAP ("got@ha",		BFD_RELOC_HI16_S_GOTOFF),
    MAP ("plt@l",		BFD_RELOC_LO16_PLTOFF),
    MAP ("plt@h",		BFD_RELOC_HI16_PLTOFF),
    MAP ("plt@ha",		BFD_RELOC_HI16_S_PLTOFF),
    MAP ("copy",		BFD_RELOC_PPC_COPY),
    MAP ("globdat",		BFD_RELOC_PPC_GLOB_DAT),
    MAP ("sectoff",		BFD_RELOC_16_BASEREL),
    MAP ("sectoff@l",		BFD_RELOC_LO16_BASEREL),
    MAP ("sectoff@h",		BFD_RELOC_HI16_BASEREL),
    MAP ("sectoff@ha",		BFD_RELOC_HI16_S_BASEREL),
    MAP ("tls",			BFD_RELOC_PPC_TLS),
    MAP ("dtpmod",		BFD_RELOC_PPC_DTPMOD),
    MAP ("dtprel",		BFD_RELOC_PPC_DTPREL),
    MAP ("dtprel@l",		BFD_RELOC_PPC_DTPREL16_LO),
    MAP ("dtprel@h",		BFD_RELOC_PPC_DTPREL16_HI),
    MAP ("dtprel@ha",		BFD_RELOC_PPC_DTPREL16_HA),
    MAP ("tprel",		BFD_RELOC_PPC_TPREL),
    MAP ("tprel@l",		BFD_RELOC_PPC_TPREL16_LO),
    MAP ("tprel@h",		BFD_RELOC_PPC_TPREL16_HI),
    MAP ("tprel@ha",		BFD_RELOC_PPC_TPREL16_HA),
    MAP ("got@tlsgd",		BFD_RELOC_PPC_GOT_TLSGD16),
    MAP ("got@tlsgd@l",		BFD_RELOC_PPC_GOT_TLSGD16_LO),
    MAP ("got@tlsgd@h",		BFD_RELOC_PPC_GOT_TLSGD16_HI),
    MAP ("got@tlsgd@ha",	BFD_RELOC_PPC_GOT_TLSGD16_HA),
    MAP ("got@tlsld",		BFD_RELOC_PPC_GOT_TLSLD16),
    MAP ("got@tlsld@l",		BFD_RELOC_PPC_GOT_TLSLD16_LO),
    MAP ("got@tlsld@h",		BFD_RELOC_PPC_GOT_TLSLD16_HI),
    MAP ("got@tlsld@ha",	BFD_RELOC_PPC_GOT_TLSLD16_HA),
    MAP ("got@dtprel",		BFD_RELOC_PPC_GOT_DTPREL16),
    MAP ("got@dtprel@l",	BFD_RELOC_PPC_GOT_DTPREL16_LO),
    MAP ("got@dtprel@h",	BFD_RELOC_PPC_GOT_DTPREL16_HI),
    MAP ("got@dtprel@ha",	BFD_RELOC_PPC_GOT_DTPREL16_HA),
    MAP ("got@tprel",		BFD_RELOC_PPC_GOT_TPREL16),
    MAP ("got@tprel@l",		BFD_RELOC_PPC_GOT_TPREL16_LO),
    MAP ("got@tprel@h",		BFD_RELOC_PPC_GOT_TPREL16_HI),
    MAP ("got@tprel@ha",	BFD_RELOC_PPC_GOT_TPREL16_HA),
    MAP32 ("fixup",		BFD_RELOC_CTOR),
    MAP32 ("plt",		BFD_RELOC_24_PLT_PCREL),
    MAP32 ("pltrel24",		BFD_RELOC_24_PLT_PCREL),
    MAP32 ("local24pc",		BFD_RELOC_PPC_LOCAL24PC),
    MAP32 ("local",		BFD_RELOC_PPC_LOCAL24PC),
    MAP32 ("pltrel",		BFD_RELOC_32_PLT_PCREL),
    MAP32 ("sdarel",		BFD_RELOC_GPREL16),
    MAP32 ("naddr",		BFD_RELOC_PPC_EMB_NADDR32),
    MAP32 ("naddr16",		BFD_RELOC_PPC_EMB_NADDR16),
    MAP32 ("naddr@l",		BFD_RELOC_PPC_EMB_NADDR16_LO),
    MAP32 ("naddr@h",		BFD_RELOC_PPC_EMB_NADDR16_HI),
    MAP32 ("naddr@ha",		BFD_RELOC_PPC_EMB_NADDR16_HA),
    MAP32 ("sdai16",		BFD_RELOC_PPC_EMB_SDAI16),
    MAP32 ("sda2rel",		BFD_RELOC_PPC_EMB_SDA2REL),
    MAP32 ("sda2i16",		BFD_RELOC_PPC_EMB_SDA2I16),
    MAP32 ("sda21",		BFD_RELOC_PPC_EMB_SDA21),
    MAP32 ("mrkref",		BFD_RELOC_PPC_EMB_MRKREF),
    MAP32 ("relsect",		BFD_RELOC_PPC_EMB_RELSEC16),
    MAP32 ("relsect@l",		BFD_RELOC_PPC_EMB_RELST_LO),
    MAP32 ("relsect@h",		BFD_RELOC_PPC_EMB_RELST_HI),
    MAP32 ("relsect@ha",	BFD_RELOC_PPC_EMB_RELST_HA),
    MAP32 ("bitfld",		BFD_RELOC_PPC_EMB_BIT_FLD),
    MAP32 ("relsda",		BFD_RELOC_PPC_EMB_RELSDA),
    MAP32 ("xgot",		BFD_RELOC_PPC_TOC16),
    MAP64 ("higher",		BFD_RELOC_PPC64_HIGHER),
    MAP64 ("highera",		BFD_RELOC_PPC64_HIGHER_S),
    MAP64 ("highest",		BFD_RELOC_PPC64_HIGHEST),
    MAP64 ("highesta",		BFD_RELOC_PPC64_HIGHEST_S),
    MAP64 ("tocbase",		BFD_RELOC_PPC64_TOC),
    MAP64 ("toc",		BFD_RELOC_PPC_TOC16),
    MAP64 ("toc@l",		BFD_RELOC_PPC64_TOC16_LO),
    MAP64 ("toc@h",		BFD_RELOC_PPC64_TOC16_HI),
    MAP64 ("toc@ha",		BFD_RELOC_PPC64_TOC16_HA),
    MAP64 ("dtprel@higher",	BFD_RELOC_PPC64_DTPREL16_HIGHER),
    MAP64 ("dtprel@highera",	BFD_RELOC_PPC64_DTPREL16_HIGHERA),
    MAP64 ("dtprel@highest",	BFD_RELOC_PPC64_DTPREL16_HIGHEST),
    MAP64 ("dtprel@highesta",	BFD_RELOC_PPC64_DTPREL16_HIGHESTA),
    MAP64 ("tprel@higher",	BFD_RELOC_PPC64_TPREL16_HIGHER),
    MAP64 ("tprel@highera",	BFD_RELOC_PPC64_TPREL16_HIGHERA),
    MAP64 ("tprel@highest",	BFD_RELOC_PPC64_TPREL16_HIGHEST),
    MAP64 ("tprel@highesta",	BFD_RELOC_PPC64_TPREL16_HIGHESTA),
    { (char *) 0, 0, 0, 0,	BFD_RELOC_UNUSED }
  };

  if (*str++ != '@')
    return BFD_RELOC_UNUSED;

  for (ch = *str, str2 = ident;
       (str2 < ident + sizeof (ident) - 1
	&& (ISALNUM (ch) || ch == '@'));
       ch = *++str)
    {
      *str2++ = TOLOWER (ch);
    }

  *str2 = '\0';
  len = str2 - ident;

  ch = ident[0];
  for (ptr = &mapping[0]; ptr->length > 0; ptr++)
    if (ch == ptr->string[0]
	&& len == ptr->length
	&& memcmp (ident, ptr->string, ptr->length) == 0
	&& (ppc_obj64 ? ptr->valid64 : ptr->valid32))
      {
	int reloc = ptr->reloc;

	if (!ppc_obj64)
	  if (exp_p->X_add_number != 0
	      && (reloc == (int) BFD_RELOC_16_GOTOFF
		  || reloc == (int) BFD_RELOC_LO16_GOTOFF
		  || reloc == (int) BFD_RELOC_HI16_GOTOFF
		  || reloc == (int) BFD_RELOC_HI16_S_GOTOFF))
	    as_warn (_("identifier+constant@got means identifier@got+constant"));

	/* Now check for identifier@suffix+constant.  */
	if (*str == '-' || *str == '+')
	  {
	    char *orig_line = input_line_pointer;
	    expressionS new_exp;

	    input_line_pointer = str;
	    expression (&new_exp);
	    if (new_exp.X_op == O_constant)
	      {
		exp_p->X_add_number += new_exp.X_add_number;
		str = input_line_pointer;
	      }

	    if (&input_line_pointer != str_p)
	      input_line_pointer = orig_line;
	  }
	*str_p = str;

	if (reloc == (int) BFD_RELOC_PPC64_TOC
	    && exp_p->X_op == O_symbol
	    && strcmp (S_GET_NAME (exp_p->X_add_symbol), ".TOC.") == 0)
	  {
	    /* Change the symbol so that the dummy .TOC. symbol can be
	       omitted from the object file.  */
	    exp_p->X_add_symbol = &abs_symbol;
	  }

	return (bfd_reloc_code_real_type) reloc;
      }

  return BFD_RELOC_UNUSED;
}

/* Like normal .long/.short/.word, except support @got, etc.
   Clobbers input_line_pointer, checks end-of-line.  */
static void
ppc_elf_cons (int nbytes /* 1=.byte, 2=.word, 4=.long, 8=.llong */)
{
  expressionS exp;
  bfd_reloc_code_real_type reloc;

  if (is_it_end_of_statement ())
    {
      demand_empty_rest_of_line ();
      return;
    }

  do
    {
      expression (&exp);
      if (exp.X_op == O_symbol
	  && *input_line_pointer == '@'
	  && (reloc = ppc_elf_suffix (&input_line_pointer,
				      &exp)) != BFD_RELOC_UNUSED)
	{
	  reloc_howto_type *reloc_howto;
	  int size;

	  reloc_howto = bfd_reloc_type_lookup (stdoutput, reloc);
	  size = bfd_get_reloc_size (reloc_howto);

	  if (size > nbytes)
	    {
	      as_bad (_("%s relocations do not fit in %d bytes\n"),
		      reloc_howto->name, nbytes);
	    }
	  else
	    {
	      char *p;
	      int offset;

	      p = frag_more (nbytes);
	      offset = 0;
	      if (target_big_endian)
		offset = nbytes - size;
	      fix_new_exp (frag_now, p - frag_now->fr_literal + offset, size,
			   &exp, 0, reloc);
	    }
	}
      else
	emit_expr (&exp, (unsigned int) nbytes);
    }
  while (*input_line_pointer++ == ',');

  /* Put terminator back into stream.  */
  input_line_pointer--;
  demand_empty_rest_of_line ();
}

/* Solaris pseduo op to change to the .rodata section.  */
static void
ppc_elf_rdata (int xxx)
{
  char *save_line = input_line_pointer;
  static char section[] = ".rodata\n";

  /* Just pretend this is .section .rodata  */
  input_line_pointer = section;
  obj_elf_section (xxx);

  input_line_pointer = save_line;
}

/* Pseudo op to make file scope bss items.  */
static void
ppc_elf_lcomm (int xxx ATTRIBUTE_UNUSED)
{
  char *name;
  char c;
  char *p;
  offsetT size;
  symbolS *symbolP;
  offsetT align;
  segT old_sec;
  int old_subsec;
  char *pfrag;
  int align2;

  name = input_line_pointer;
  c = get_symbol_end ();

  /* just after name is now '\0'.  */
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad (_("Expected comma after symbol-name: rest of line ignored."));
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ',' */
  if ((size = get_absolute_expression ()) < 0)
    {
      as_warn (_(".COMMon length (%ld.) <0! Ignored."), (long) size);
      ignore_rest_of_line ();
      return;
    }

  /* The third argument to .lcomm is the alignment.  */
  if (*input_line_pointer != ',')
    align = 8;
  else
    {
      ++input_line_pointer;
      align = get_absolute_expression ();
      if (align <= 0)
	{
	  as_warn (_("ignoring bad alignment"));
	  align = 8;
	}
    }

  *p = 0;
  symbolP = symbol_find_or_make (name);
  *p = c;

  if (S_IS_DEFINED (symbolP) && ! S_IS_COMMON (symbolP))
    {
      as_bad (_("Ignoring attempt to re-define symbol `%s'."),
	      S_GET_NAME (symbolP));
      ignore_rest_of_line ();
      return;
    }

  if (S_GET_VALUE (symbolP) && S_GET_VALUE (symbolP) != (valueT) size)
    {
      as_bad (_("Length of .lcomm \"%s\" is already %ld. Not changed to %ld."),
	      S_GET_NAME (symbolP),
	      (long) S_GET_VALUE (symbolP),
	      (long) size);

      ignore_rest_of_line ();
      return;
    }

  /* Allocate_bss.  */
  old_sec = now_seg;
  old_subsec = now_subseg;
  if (align)
    {
      /* Convert to a power of 2 alignment.  */
      for (align2 = 0; (align & 1) == 0; align >>= 1, ++align2);
      if (align != 1)
	{
	  as_bad (_("Common alignment not a power of 2"));
	  ignore_rest_of_line ();
	  return;
	}
    }
  else
    align2 = 0;

  record_alignment (bss_section, align2);
  subseg_set (bss_section, 0);
  if (align2)
    frag_align (align2, 0, 0);
  if (S_GET_SEGMENT (symbolP) == bss_section)
    symbol_get_frag (symbolP)->fr_symbol = 0;
  symbol_set_frag (symbolP, frag_now);
  pfrag = frag_var (rs_org, 1, 1, (relax_substateT) 0, symbolP, size,
		    (char *) 0);
  *pfrag = 0;
  S_SET_SIZE (symbolP, size);
  S_SET_SEGMENT (symbolP, bss_section);
  subseg_set (old_sec, old_subsec);
  demand_empty_rest_of_line ();
}

/* Validate any relocations emitted for -mrelocatable, possibly adding
   fixups for word relocations in writable segments, so we can adjust
   them at runtime.  */
static void
ppc_elf_validate_fix (fixS *fixp, segT seg)
{
  if (fixp->fx_done || fixp->fx_pcrel)
    return;

  switch (shlib)
    {
    case SHLIB_NONE:
    case SHLIB_PIC:
      return;

    case SHLIB_MRELOCATABLE:
      if (fixp->fx_r_type <= BFD_RELOC_UNUSED
	  && fixp->fx_r_type != BFD_RELOC_16_GOTOFF
	  && fixp->fx_r_type != BFD_RELOC_HI16_GOTOFF
	  && fixp->fx_r_type != BFD_RELOC_LO16_GOTOFF
	  && fixp->fx_r_type != BFD_RELOC_HI16_S_GOTOFF
	  && fixp->fx_r_type != BFD_RELOC_16_BASEREL
	  && fixp->fx_r_type != BFD_RELOC_LO16_BASEREL
	  && fixp->fx_r_type != BFD_RELOC_HI16_BASEREL
	  && fixp->fx_r_type != BFD_RELOC_HI16_S_BASEREL
	  && (seg->flags & SEC_LOAD) != 0
	  && strcmp (segment_name (seg), ".got2") != 0
	  && strcmp (segment_name (seg), ".dtors") != 0
	  && strcmp (segment_name (seg), ".ctors") != 0
	  && strcmp (segment_name (seg), ".fixup") != 0
	  && strcmp (segment_name (seg), ".gcc_except_table") != 0
	  && strcmp (segment_name (seg), ".eh_frame") != 0
	  && strcmp (segment_name (seg), ".ex_shared") != 0)
	{
	  if ((seg->flags & (SEC_READONLY | SEC_CODE)) != 0
	      || fixp->fx_r_type != BFD_RELOC_CTOR)
	    {
	      as_bad_where (fixp->fx_file, fixp->fx_line,
			    _("Relocation cannot be done when using -mrelocatable"));
	    }
	}
      return;
    }
}

/* Prevent elf_frob_file_before_adjust removing a weak undefined
   function descriptor sym if the corresponding code sym is used.  */

void
ppc_frob_file_before_adjust (void)
{
  symbolS *symp;
  asection *toc;

  if (!ppc_obj64)
    return;

  for (symp = symbol_rootP; symp; symp = symbol_next (symp))
    {
      const char *name;
      char *dotname;
      symbolS *dotsym;
      size_t len;

      name = S_GET_NAME (symp);
      if (name[0] == '.')
	continue;

      if (! S_IS_WEAK (symp)
	  || S_IS_DEFINED (symp))
	continue;

      len = strlen (name) + 1;
      dotname = xmalloc (len + 1);
      dotname[0] = '.';
      memcpy (dotname + 1, name, len);
      dotsym = symbol_find_noref (dotname, 1);
      free (dotname);
      if (dotsym != NULL && (symbol_used_p (dotsym)
			     || symbol_used_in_reloc_p (dotsym)))
	symbol_mark_used (symp);

    }

  toc = bfd_get_section_by_name (stdoutput, ".toc");
  if (toc != NULL
      && bfd_section_size (stdoutput, toc) > 0x10000)
    as_warn (_("TOC section size exceeds 64k"));

  /* Don't emit .TOC. symbol.  */
  symp = symbol_find (".TOC.");
  if (symp != NULL)
    symbol_remove (symp, &symbol_rootP, &symbol_lastP);
}
#endif /* OBJ_ELF */

#ifdef TE_PE

/*
 * Summary of parse_toc_entry.
 *
 * in:	Input_line_pointer points to the '[' in one of:
 *
 *        [toc] [tocv] [toc32] [toc64]
 *
 *      Anything else is an error of one kind or another.
 *
 * out:
 *   return value: success or failure
 *   toc_kind:     kind of toc reference
 *   input_line_pointer:
 *     success: first char after the ']'
 *     failure: unchanged
 *
 * settings:
 *
 *     [toc]   - rv == success, toc_kind = default_toc
 *     [tocv]  - rv == success, toc_kind = data_in_toc
 *     [toc32] - rv == success, toc_kind = must_be_32
 *     [toc64] - rv == success, toc_kind = must_be_64
 *
 */

enum toc_size_qualifier
{
  default_toc, /* The toc cell constructed should be the system default size */
  data_in_toc, /* This is a direct reference to a toc cell                   */
  must_be_32,  /* The toc cell constructed must be 32 bits wide              */
  must_be_64   /* The toc cell constructed must be 64 bits wide              */
};

static int
parse_toc_entry (enum toc_size_qualifier *toc_kind)
{
  char *start;
  char *toc_spec;
  char c;
  enum toc_size_qualifier t;

  /* Save the input_line_pointer.  */
  start = input_line_pointer;

  /* Skip over the '[' , and whitespace.  */
  ++input_line_pointer;
  SKIP_WHITESPACE ();

  /* Find the spelling of the operand.  */
  toc_spec = input_line_pointer;
  c = get_symbol_end ();

  if (strcmp (toc_spec, "toc") == 0)
    {
      t = default_toc;
    }
  else if (strcmp (toc_spec, "tocv") == 0)
    {
      t = data_in_toc;
    }
  else if (strcmp (toc_spec, "toc32") == 0)
    {
      t = must_be_32;
    }
  else if (strcmp (toc_spec, "toc64") == 0)
    {
      t = must_be_64;
    }
  else
    {
      as_bad (_("syntax error: invalid toc specifier `%s'"), toc_spec);
      *input_line_pointer = c;
      input_line_pointer = start;
      return 0;
    }

  /* Now find the ']'.  */
  *input_line_pointer = c;

  SKIP_WHITESPACE ();	     /* leading whitespace could be there.  */
  c = *input_line_pointer++; /* input_line_pointer->past char in c.  */

  if (c != ']')
    {
      as_bad (_("syntax error: expected `]', found  `%c'"), c);
      input_line_pointer = start;
      return 0;
    }

  *toc_kind = t;
  return 1;
}
#endif


#ifdef OBJ_ELF
#define APUID(a,v)	((((a) & 0xffff) << 16) | ((v) & 0xffff))
static void
ppc_apuinfo_section_add (unsigned int apu, unsigned int version)
{
  unsigned int i;

  /* Check we don't already exist.  */
  for (i = 0; i < ppc_apuinfo_num; i++)
    if (ppc_apuinfo_list[i] == APUID (apu, version))
      return;

  if (ppc_apuinfo_num == ppc_apuinfo_num_alloc)
    {
      if (ppc_apuinfo_num_alloc == 0)
	{
	  ppc_apuinfo_num_alloc = 4;
	  ppc_apuinfo_list = (unsigned long *)
	      xmalloc (sizeof (unsigned long) * ppc_apuinfo_num_alloc);
	}
      else
	{
	  ppc_apuinfo_num_alloc += 4;
	  ppc_apuinfo_list = (unsigned long *) xrealloc (ppc_apuinfo_list,
	      sizeof (unsigned long) * ppc_apuinfo_num_alloc);
	}
    }
  ppc_apuinfo_list[ppc_apuinfo_num++] = APUID (apu, version);
}
#undef APUID
#endif


/* We need to keep a list of fixups.  We can't simply generate them as
   we go, because that would require us to first create the frag, and
   that would screw up references to ``.''.  */

struct ppc_fixup
{
  expressionS exp;
  int opindex;
  bfd_reloc_code_real_type reloc;
};

#define MAX_INSN_FIXUPS (5)

/* This routine is called for each instruction to be assembled.  */

void
md_assemble (char *str)
{
  char *s;
  const struct powerpc_opcode *opcode;
  unsigned long insn;
  const unsigned char *opindex_ptr;
  int skip_optional;
  int need_paren;
  int next_opindex;
  struct ppc_fixup fixups[MAX_INSN_FIXUPS];
  int fc;
  char *f;
  int addr_mod;
  int i;
#ifdef OBJ_ELF
  bfd_reloc_code_real_type reloc;
#endif

  /* Get the opcode.  */
  for (s = str; *s != '\0' && ! ISSPACE (*s); s++)
    ;
  if (*s != '\0')
    *s++ = '\0';

  /* Look up the opcode in the hash table.  */
  opcode = (const struct powerpc_opcode *) hash_find (ppc_hash, str);
  if (opcode == (const struct powerpc_opcode *) NULL)
    {
      const struct powerpc_macro *macro;

      macro = (const struct powerpc_macro *) hash_find (ppc_macro_hash, str);
      if (macro == (const struct powerpc_macro *) NULL)
	as_bad (_("Unrecognized opcode: `%s'"), str);
      else
	ppc_macro (s, macro);

      return;
    }

  insn = opcode->opcode;

  str = s;
  while (ISSPACE (*str))
    ++str;

  /* PowerPC operands are just expressions.  The only real issue is
     that a few operand types are optional.  All cases which might use
     an optional operand separate the operands only with commas (in some
     cases parentheses are used, as in ``lwz 1,0(1)'' but such cases never
     have optional operands).  Most instructions with optional operands
     have only one.  Those that have more than one optional operand can
     take either all their operands or none.  So, before we start seriously
     parsing the operands, we check to see if we have optional operands,
     and if we do, we count the number of commas to see which operands
     have been omitted.  */
  skip_optional = 0;
  for (opindex_ptr = opcode->operands; *opindex_ptr != 0; opindex_ptr++)
    {
      const struct powerpc_operand *operand;

      operand = &powerpc_operands[*opindex_ptr];
      if ((operand->flags & PPC_OPERAND_OPTIONAL) != 0)
	{
	  unsigned int opcount;
	  unsigned int num_operands_expected;
	  unsigned int i;

	  /* There is an optional operand.  Count the number of
	     commas in the input line.  */
	  if (*str == '\0')
	    opcount = 0;
	  else
	    {
	      opcount = 1;
	      s = str;
	      while ((s = strchr (s, ',')) != (char *) NULL)
		{
		  ++opcount;
		  ++s;
		}
	    }

	  /* Compute the number of expected operands.
	     Do not count fake operands.  */
	  for (num_operands_expected = 0, i = 0; opcode->operands[i]; i ++)
	    if ((powerpc_operands [opcode->operands[i]].flags & PPC_OPERAND_FAKE) == 0)
	      ++ num_operands_expected;

	  /* If there are fewer operands in the line then are called
	     for by the instruction, we want to skip the optional
	     operands.  */
	  if (opcount < num_operands_expected)
	    skip_optional = 1;

	  break;
	}
    }

  /* Gather the operands.  */
  need_paren = 0;
  next_opindex = 0;
  fc = 0;
  for (opindex_ptr = opcode->operands; *opindex_ptr != 0; opindex_ptr++)
    {
      const struct powerpc_operand *operand;
      const char *errmsg;
      char *hold;
      expressionS ex;
      char endc;

      if (next_opindex == 0)
	operand = &powerpc_operands[*opindex_ptr];
      else
	{
	  operand = &powerpc_operands[next_opindex];
	  next_opindex = 0;
	}
      errmsg = NULL;

      /* If this is a fake operand, then we do not expect anything
	 from the input.  */
      if ((operand->flags & PPC_OPERAND_FAKE) != 0)
	{
	  insn = (*operand->insert) (insn, 0L, ppc_cpu, &errmsg);
	  if (errmsg != (const char *) NULL)
	    as_bad ("%s", errmsg);
	  continue;
	}

      /* If this is an optional operand, and we are skipping it, just
	 insert a zero.  */
      if ((operand->flags & PPC_OPERAND_OPTIONAL) != 0
	  && skip_optional)
	{
	  if (operand->insert)
	    {
	      insn = (*operand->insert) (insn, 0L, ppc_cpu, &errmsg);
	      if (errmsg != (const char *) NULL)
		as_bad ("%s", errmsg);
	    }
	  if ((operand->flags & PPC_OPERAND_NEXT) != 0)
	    next_opindex = *opindex_ptr + 1;
	  continue;
	}

      /* Gather the operand.  */
      hold = input_line_pointer;
      input_line_pointer = str;

#ifdef TE_PE
      if (*input_line_pointer == '[')
	{
	  /* We are expecting something like the second argument here:
	   *
	   *    lwz r4,[toc].GS.0.static_int(rtoc)
	   *           ^^^^^^^^^^^^^^^^^^^^^^^^^^^
	   * The argument following the `]' must be a symbol name, and the
	   * register must be the toc register: 'rtoc' or '2'
	   *
	   * The effect is to 0 as the displacement field
	   * in the instruction, and issue an IMAGE_REL_PPC_TOCREL16 (or
	   * the appropriate variation) reloc against it based on the symbol.
	   * The linker will build the toc, and insert the resolved toc offset.
	   *
	   * Note:
	   * o The size of the toc entry is currently assumed to be
	   *   32 bits. This should not be assumed to be a hard coded
	   *   number.
	   * o In an effort to cope with a change from 32 to 64 bits,
	   *   there are also toc entries that are specified to be
	   *   either 32 or 64 bits:
	   *     lwz r4,[toc32].GS.0.static_int(rtoc)
	   *     lwz r4,[toc64].GS.0.static_int(rtoc)
	   *   These demand toc entries of the specified size, and the
	   *   instruction probably requires it.
	   */

	  int valid_toc;
	  enum toc_size_qualifier toc_kind;
	  bfd_reloc_code_real_type toc_reloc;

	  /* Go parse off the [tocXX] part.  */
	  valid_toc = parse_toc_entry (&toc_kind);

	  if (!valid_toc)
	    {
	      /* Note: message has already been issued.
		 FIXME: what sort of recovery should we do?
		 demand_rest_of_line (); return; ?  */
	    }

	  /* Now get the symbol following the ']'.  */
	  expression (&ex);

	  switch (toc_kind)
	    {
	    case default_toc:
	      /* In this case, we may not have seen the symbol yet,
		 since  it is allowed to appear on a .extern or .globl
		 or just be a label in the .data section.  */
	      toc_reloc = BFD_RELOC_PPC_TOC16;
	      break;
	    case data_in_toc:
	      /* 1. The symbol must be defined and either in the toc
		 section, or a global.
		 2. The reloc generated must have the TOCDEFN flag set
		 in upper bit mess of the reloc type.
		 FIXME: It's a little confusing what the tocv
		 qualifier can be used for.  At the very least, I've
		 seen three uses, only one of which I'm sure I can
		 explain.  */
	      if (ex.X_op == O_symbol)
		{
		  assert (ex.X_add_symbol != NULL);
		  if (symbol_get_bfdsym (ex.X_add_symbol)->section
		      != tocdata_section)
		    {
		      as_bad (_("[tocv] symbol is not a toc symbol"));
		    }
		}

	      toc_reloc = BFD_RELOC_PPC_TOC16;
	      break;
	    case must_be_32:
	      /* FIXME: these next two specifically specify 32/64 bit
		 toc entries.  We don't support them today.  Is this
		 the right way to say that?  */
	      toc_reloc = BFD_RELOC_UNUSED;
	      as_bad (_("Unimplemented toc32 expression modifier"));
	      break;
	    case must_be_64:
	      /* FIXME: see above.  */
	      toc_reloc = BFD_RELOC_UNUSED;
	      as_bad (_("Unimplemented toc64 expression modifier"));
	      break;
	    default:
	      fprintf (stderr,
		       _("Unexpected return value [%d] from parse_toc_entry!\n"),
		       toc_kind);
	      abort ();
	      break;
	    }

	  /* We need to generate a fixup for this expression.  */
	  if (fc >= MAX_INSN_FIXUPS)
	    as_fatal (_("too many fixups"));

	  fixups[fc].reloc = toc_reloc;
	  fixups[fc].exp = ex;
	  fixups[fc].opindex = *opindex_ptr;
	  ++fc;

	  /* Ok. We've set up the fixup for the instruction. Now make it
	     look like the constant 0 was found here.  */
	  ex.X_unsigned = 1;
	  ex.X_op = O_constant;
	  ex.X_add_number = 0;
	  ex.X_add_symbol = NULL;
	  ex.X_op_symbol = NULL;
	}

      else
#endif		/* TE_PE */
	{
	  if (! register_name (&ex))
	    {
	      if ((operand->flags & PPC_OPERAND_CR) != 0)
		cr_operand = TRUE;
	      expression (&ex);
	      cr_operand = FALSE;
	    }
	}

      str = input_line_pointer;
      input_line_pointer = hold;

      if (ex.X_op == O_illegal)
	as_bad (_("illegal operand"));
      else if (ex.X_op == O_absent)
	as_bad (_("missing operand"));
      else if (ex.X_op == O_register)
	{
	  insn = ppc_insert_operand (insn, operand, ex.X_add_number,
				     (char *) NULL, 0);
	}
      else if (ex.X_op == O_constant)
	{
#ifdef OBJ_ELF
	  /* Allow @HA, @L, @H on constants.  */
	  char *orig_str = str;

	  if ((reloc = ppc_elf_suffix (&str, &ex)) != BFD_RELOC_UNUSED)
	    switch (reloc)
	      {
	      default:
		str = orig_str;
		break;

	      case BFD_RELOC_LO16:
		/* X_unsigned is the default, so if the user has done
		   something which cleared it, we always produce a
		   signed value.  */
		if (ex.X_unsigned && ! (operand->flags & PPC_OPERAND_SIGNED))
		  ex.X_add_number &= 0xffff;
		else
		  ex.X_add_number = SEX16 (ex.X_add_number);
		break;

	      case BFD_RELOC_HI16:
		if (ex.X_unsigned && ! (operand->flags & PPC_OPERAND_SIGNED))
		  ex.X_add_number = PPC_HI (ex.X_add_number);
		else
		  ex.X_add_number = SEX16 (PPC_HI (ex.X_add_number));
		break;

	      case BFD_RELOC_HI16_S:
		if (ex.X_unsigned && ! (operand->flags & PPC_OPERAND_SIGNED))
		  ex.X_add_number = PPC_HA (ex.X_add_number);
		else
		  ex.X_add_number = SEX16 (PPC_HA (ex.X_add_number));
		break;

	      case BFD_RELOC_PPC64_HIGHER:
		if (ex.X_unsigned && ! (operand->flags & PPC_OPERAND_SIGNED))
		  ex.X_add_number = PPC_HIGHER (ex.X_add_number);
		else
		  ex.X_add_number = SEX16 (PPC_HIGHER (ex.X_add_number));
		break;

	      case BFD_RELOC_PPC64_HIGHER_S:
		if (ex.X_unsigned && ! (operand->flags & PPC_OPERAND_SIGNED))
		  ex.X_add_number = PPC_HIGHERA (ex.X_add_number);
		else
		  ex.X_add_number = SEX16 (PPC_HIGHERA (ex.X_add_number));
		break;

	      case BFD_RELOC_PPC64_HIGHEST:
		if (ex.X_unsigned && ! (operand->flags & PPC_OPERAND_SIGNED))
		  ex.X_add_number = PPC_HIGHEST (ex.X_add_number);
		else
		  ex.X_add_number = SEX16 (PPC_HIGHEST (ex.X_add_number));
		break;

	      case BFD_RELOC_PPC64_HIGHEST_S:
		if (ex.X_unsigned && ! (operand->flags & PPC_OPERAND_SIGNED))
		  ex.X_add_number = PPC_HIGHESTA (ex.X_add_number);
		else
		  ex.X_add_number = SEX16 (PPC_HIGHESTA (ex.X_add_number));
		break;
	      }
#endif /* OBJ_ELF */
	  insn = ppc_insert_operand (insn, operand, ex.X_add_number,
				     (char *) NULL, 0);
	}
#ifdef OBJ_ELF
      else
	{
	  if (ex.X_op == O_symbol && str[0] == '(')
	    {
	      const char *sym_name = S_GET_NAME (ex.X_add_symbol);
	      if (sym_name[0] == '.')
		++sym_name;

	      if (strcasecmp (sym_name, "__tls_get_addr") == 0)
		{
		  expressionS tls_exp;

		  hold = input_line_pointer;
		  input_line_pointer = str + 1;
		  expression (&tls_exp);
		  if (tls_exp.X_op == O_symbol)
		    {
		      reloc = BFD_RELOC_UNUSED;
		      if (strncasecmp (input_line_pointer, "@tlsgd)", 7) == 0)
			{
			  reloc = BFD_RELOC_PPC_TLSGD;
			  input_line_pointer += 7;
			}
		      else if (strncasecmp (input_line_pointer, "@tlsld)", 7) == 0)
			{
			  reloc = BFD_RELOC_PPC_TLSLD;
			  input_line_pointer += 7;
			}
		      if (reloc != BFD_RELOC_UNUSED)
			{
			  SKIP_WHITESPACE ();
			  str = input_line_pointer;

			  if (fc >= MAX_INSN_FIXUPS)
			    as_fatal (_("too many fixups"));
			  fixups[fc].exp = tls_exp;
			  fixups[fc].opindex = *opindex_ptr;
			  fixups[fc].reloc = reloc;
			  ++fc;
			}
		    }
		  input_line_pointer = hold;
		}
	    }

	  if ((reloc = ppc_elf_suffix (&str, &ex)) != BFD_RELOC_UNUSED)
	    {
	      /* Some TLS tweaks.  */
	      switch (reloc)
		{
		default:
		  break;

		case BFD_RELOC_PPC_TLS:
		  insn = ppc_insert_operand (insn, operand, ppc_obj64 ? 13 : 2,
					     (char *) NULL, 0);
		  break;

		  /* We'll only use the 32 (or 64) bit form of these relocations
		     in constants.  Instructions get the 16 bit form.  */
		case BFD_RELOC_PPC_DTPREL:
		  reloc = BFD_RELOC_PPC_DTPREL16;
		  break;
		case BFD_RELOC_PPC_TPREL:
		  reloc = BFD_RELOC_PPC_TPREL16;
		  break;
		}

	      /* For the absolute forms of branches, convert the PC
		 relative form back into the absolute.  */
	      if ((operand->flags & PPC_OPERAND_ABSOLUTE) != 0)
		{
		  switch (reloc)
		    {
		    case BFD_RELOC_PPC_B26:
		      reloc = BFD_RELOC_PPC_BA26;
		      break;
		    case BFD_RELOC_PPC_B16:
		      reloc = BFD_RELOC_PPC_BA16;
		      break;
		    case BFD_RELOC_PPC_B16_BRTAKEN:
		      reloc = BFD_RELOC_PPC_BA16_BRTAKEN;
		      break;
		    case BFD_RELOC_PPC_B16_BRNTAKEN:
		      reloc = BFD_RELOC_PPC_BA16_BRNTAKEN;
		      break;
		    default:
		      break;
		    }
		}

	      if (ppc_obj64
		  && (operand->flags & (PPC_OPERAND_DS | PPC_OPERAND_DQ)) != 0)
		{
		  switch (reloc)
		    {
		    case BFD_RELOC_16:
		      reloc = BFD_RELOC_PPC64_ADDR16_DS;
		      break;
		    case BFD_RELOC_LO16:
		      reloc = BFD_RELOC_PPC64_ADDR16_LO_DS;
		      break;
		    case BFD_RELOC_16_GOTOFF:
		      reloc = BFD_RELOC_PPC64_GOT16_DS;
		      break;
		    case BFD_RELOC_LO16_GOTOFF:
		      reloc = BFD_RELOC_PPC64_GOT16_LO_DS;
		      break;
		    case BFD_RELOC_LO16_PLTOFF:
		      reloc = BFD_RELOC_PPC64_PLT16_LO_DS;
		      break;
		    case BFD_RELOC_16_BASEREL:
		      reloc = BFD_RELOC_PPC64_SECTOFF_DS;
		      break;
		    case BFD_RELOC_LO16_BASEREL:
		      reloc = BFD_RELOC_PPC64_SECTOFF_LO_DS;
		      break;
		    case BFD_RELOC_PPC_TOC16:
		      reloc = BFD_RELOC_PPC64_TOC16_DS;
		      break;
		    case BFD_RELOC_PPC64_TOC16_LO:
		      reloc = BFD_RELOC_PPC64_TOC16_LO_DS;
		      break;
		    case BFD_RELOC_PPC64_PLTGOT16:
		      reloc = BFD_RELOC_PPC64_PLTGOT16_DS;
		      break;
		    case BFD_RELOC_PPC64_PLTGOT16_LO:
		      reloc = BFD_RELOC_PPC64_PLTGOT16_LO_DS;
		      break;
		    case BFD_RELOC_PPC_DTPREL16:
		      reloc = BFD_RELOC_PPC64_DTPREL16_DS;
		      break;
		    case BFD_RELOC_PPC_DTPREL16_LO:
		      reloc = BFD_RELOC_PPC64_DTPREL16_LO_DS;
		      break;
		    case BFD_RELOC_PPC_TPREL16:
		      reloc = BFD_RELOC_PPC64_TPREL16_DS;
		      break;
		    case BFD_RELOC_PPC_TPREL16_LO:
		      reloc = BFD_RELOC_PPC64_TPREL16_LO_DS;
		      break;
		    case BFD_RELOC_PPC_GOT_DTPREL16:
		    case BFD_RELOC_PPC_GOT_DTPREL16_LO:
		    case BFD_RELOC_PPC_GOT_TPREL16:
		    case BFD_RELOC_PPC_GOT_TPREL16_LO:
		      break;
		    default:
		      as_bad (_("unsupported relocation for DS offset field"));
		      break;
		    }
		}
	    }

	  /* We need to generate a fixup for this expression.  */
	  if (fc >= MAX_INSN_FIXUPS)
	    as_fatal (_("too many fixups"));
	  fixups[fc].exp = ex;
	  fixups[fc].opindex = *opindex_ptr;
	  fixups[fc].reloc = reloc;
	  ++fc;
	}
#else /* OBJ_ELF */
      else
	{
	  /* We need to generate a fixup for this expression.  */
	  if (fc >= MAX_INSN_FIXUPS)
	    as_fatal (_("too many fixups"));
	  fixups[fc].exp = ex;
	  fixups[fc].opindex = *opindex_ptr;
	  fixups[fc].reloc = BFD_RELOC_UNUSED;
	  ++fc;
	}
#endif /* OBJ_ELF */

      if (need_paren)
	{
	  endc = ')';
	  need_paren = 0;
	}
      else if ((operand->flags & PPC_OPERAND_PARENS) != 0)
	{
	  endc = '(';
	  need_paren = 1;
	}
      else
	endc = ',';

      /* The call to expression should have advanced str past any
	 whitespace.  */
      if (*str != endc
	  && (endc != ',' || *str != '\0'))
	{
	  as_bad (_("syntax error; found `%c' but expected `%c'"), *str, endc);
	  break;
	}

      if (*str != '\0')
	++str;
    }

  while (ISSPACE (*str))
    ++str;

  if (*str != '\0')
    as_bad (_("junk at end of line: `%s'"), str);

#ifdef OBJ_ELF
  /* Do we need/want a APUinfo section? */
  if (ppc_cpu & (PPC_OPCODE_SPE
   	       | PPC_OPCODE_ISEL | PPC_OPCODE_EFS
	       | PPC_OPCODE_BRLOCK | PPC_OPCODE_PMR | PPC_OPCODE_CACHELCK
	       | PPC_OPCODE_RFMCI))
    {
      /* These are all version "1".  */
      if (opcode->flags & PPC_OPCODE_SPE)
	ppc_apuinfo_section_add (PPC_APUINFO_SPE, 1);
      if (opcode->flags & PPC_OPCODE_ISEL)
	ppc_apuinfo_section_add (PPC_APUINFO_ISEL, 1);
      if (opcode->flags & PPC_OPCODE_EFS)
	ppc_apuinfo_section_add (PPC_APUINFO_EFS, 1);
      if (opcode->flags & PPC_OPCODE_BRLOCK)
	ppc_apuinfo_section_add (PPC_APUINFO_BRLOCK, 1);
      if (opcode->flags & PPC_OPCODE_PMR)
	ppc_apuinfo_section_add (PPC_APUINFO_PMR, 1);
      if (opcode->flags & PPC_OPCODE_CACHELCK)
	ppc_apuinfo_section_add (PPC_APUINFO_CACHELCK, 1);
      if (opcode->flags & PPC_OPCODE_RFMCI)
	ppc_apuinfo_section_add (PPC_APUINFO_RFMCI, 1);
    }
#endif

  /* Write out the instruction.  */
  f = frag_more (4);
  addr_mod = frag_now_fix () & 3;
  if (frag_now->has_code && frag_now->insn_addr != addr_mod)
    as_bad (_("instruction address is not a multiple of 4"));
  frag_now->insn_addr = addr_mod;
  frag_now->has_code = 1;
  md_number_to_chars (f, insn, 4);

#ifdef OBJ_ELF
  dwarf2_emit_insn (4);
#endif

  /* Create any fixups.  At this point we do not use a
     bfd_reloc_code_real_type, but instead just use the
     BFD_RELOC_UNUSED plus the operand index.  This lets us easily
     handle fixups for any operand type, although that is admittedly
     not a very exciting feature.  We pick a BFD reloc type in
     md_apply_fix.  */
  for (i = 0; i < fc; i++)
    {
      const struct powerpc_operand *operand;

      operand = &powerpc_operands[fixups[i].opindex];
      if (fixups[i].reloc != BFD_RELOC_UNUSED)
	{
	  reloc_howto_type *reloc_howto;
	  int size;
	  int offset;
	  fixS *fixP;

	  reloc_howto = bfd_reloc_type_lookup (stdoutput, fixups[i].reloc);
	  if (!reloc_howto)
	    abort ();

	  size = bfd_get_reloc_size (reloc_howto);
	  offset = target_big_endian ? (4 - size) : 0;

	  if (size < 1 || size > 4)
	    abort ();

	  fixP = fix_new_exp (frag_now,
			      f - frag_now->fr_literal + offset,
			      size,
			      &fixups[i].exp,
			      reloc_howto->pc_relative,
			      fixups[i].reloc);

	  /* Turn off complaints that the addend is too large for things like
	     foo+100000@ha.  */
	  switch (fixups[i].reloc)
	    {
	    case BFD_RELOC_16_GOTOFF:
	    case BFD_RELOC_PPC_TOC16:
	    case BFD_RELOC_LO16:
	    case BFD_RELOC_HI16:
	    case BFD_RELOC_HI16_S:
#ifdef OBJ_ELF
	    case BFD_RELOC_PPC64_HIGHER:
	    case BFD_RELOC_PPC64_HIGHER_S:
	    case BFD_RELOC_PPC64_HIGHEST:
	    case BFD_RELOC_PPC64_HIGHEST_S:
#endif
	      fixP->fx_no_overflow = 1;
	      break;
	    default:
	      break;
	    }
	}
      else
	fix_new_exp (frag_now,
		     f - frag_now->fr_literal,
		     4,
		     &fixups[i].exp,
		     (operand->flags & PPC_OPERAND_RELATIVE) != 0,
		     ((bfd_reloc_code_real_type)
		      (fixups[i].opindex + (int) BFD_RELOC_UNUSED)));
    }
}

/* Handle a macro.  Gather all the operands, transform them as
   described by the macro, and call md_assemble recursively.  All the
   operands are separated by commas; we don't accept parentheses
   around operands here.  */

static void
ppc_macro (char *str, const struct powerpc_macro *macro)
{
  char *operands[10];
  unsigned int count;
  char *s;
  unsigned int len;
  const char *format;
  unsigned int arg;
  char *send;
  char *complete;

  /* Gather the users operands into the operands array.  */
  count = 0;
  s = str;
  while (1)
    {
      if (count >= sizeof operands / sizeof operands[0])
	break;
      operands[count++] = s;
      s = strchr (s, ',');
      if (s == (char *) NULL)
	break;
      *s++ = '\0';
    }

  if (count != macro->operands)
    {
      as_bad (_("wrong number of operands"));
      return;
    }

  /* Work out how large the string must be (the size is unbounded
     because it includes user input).  */
  len = 0;
  format = macro->format;
  while (*format != '\0')
    {
      if (*format != '%')
	{
	  ++len;
	  ++format;
	}
      else
	{
	  arg = strtol (format + 1, &send, 10);
	  know (send != format && arg < count);
	  len += strlen (operands[arg]);
	  format = send;
	}
    }

  /* Put the string together.  */
  complete = s = (char *) alloca (len + 1);
  format = macro->format;
  while (*format != '\0')
    {
      if (*format != '%')
	*s++ = *format++;
      else
	{
	  arg = strtol (format + 1, &send, 10);
	  strcpy (s, operands[arg]);
	  s += strlen (s);
	  format = send;
	}
    }
  *s = '\0';

  /* Assemble the constructed instruction.  */
  md_assemble (complete);
}

#ifdef OBJ_ELF
/* For ELF, add support for SHF_EXCLUDE and SHT_ORDERED.  */

int
ppc_section_letter (int letter, char **ptr_msg)
{
  if (letter == 'e')
    return SHF_EXCLUDE;

  *ptr_msg = _("Bad .section directive: want a,e,w,x,M,S,G,T in string");
  return -1;
}

int
ppc_section_word (char *str, size_t len)
{
  if (len == 7 && strncmp (str, "exclude", 7) == 0)
    return SHF_EXCLUDE;

  return -1;
}

int
ppc_section_type (char *str, size_t len)
{
  if (len == 7 && strncmp (str, "ordered", 7) == 0)
    return SHT_ORDERED;

  return -1;
}

int
ppc_section_flags (int flags, int attr, int type)
{
  if (type == SHT_ORDERED)
    flags |= SEC_ALLOC | SEC_LOAD | SEC_SORT_ENTRIES;

  if (attr & SHF_EXCLUDE)
    flags |= SEC_EXCLUDE;

  return flags;
}
#endif /* OBJ_ELF */


/* Pseudo-op handling.  */

/* The .byte pseudo-op.  This is similar to the normal .byte
   pseudo-op, but it can also take a single ASCII string.  */

static void
ppc_byte (int ignore ATTRIBUTE_UNUSED)
{
  if (*input_line_pointer != '\"')
    {
      cons (1);
      return;
    }

  /* Gather characters.  A real double quote is doubled.  Unusual
     characters are not permitted.  */
  ++input_line_pointer;
  while (1)
    {
      char c;

      c = *input_line_pointer++;

      if (c == '\"')
	{
	  if (*input_line_pointer != '\"')
	    break;
	  ++input_line_pointer;
	}

      FRAG_APPEND_1_CHAR (c);
    }

  demand_empty_rest_of_line ();
}

#ifdef OBJ_XCOFF

/* XCOFF specific pseudo-op handling.  */

/* This is set if we are creating a .stabx symbol, since we don't want
   to handle symbol suffixes for such symbols.  */
static bfd_boolean ppc_stab_symbol;

/* The .comm and .lcomm pseudo-ops for XCOFF.  XCOFF puts common
   symbols in the .bss segment as though they were local common
   symbols, and uses a different smclas.  The native Aix 4.3.3 assembler
   aligns .comm and .lcomm to 4 bytes.  */

static void
ppc_comm (int lcomm)
{
  asection *current_seg = now_seg;
  subsegT current_subseg = now_subseg;
  char *name;
  char endc;
  char *end_name;
  offsetT size;
  offsetT align;
  symbolS *lcomm_sym = NULL;
  symbolS *sym;
  char *pfrag;

  name = input_line_pointer;
  endc = get_symbol_end ();
  end_name = input_line_pointer;
  *end_name = endc;

  if (*input_line_pointer != ',')
    {
      as_bad (_("missing size"));
      ignore_rest_of_line ();
      return;
    }
  ++input_line_pointer;

  size = get_absolute_expression ();
  if (size < 0)
    {
      as_bad (_("negative size"));
      ignore_rest_of_line ();
      return;
    }

  if (! lcomm)
    {
      /* The third argument to .comm is the alignment.  */
      if (*input_line_pointer != ',')
	align = 2;
      else
	{
	  ++input_line_pointer;
	  align = get_absolute_expression ();
	  if (align <= 0)
	    {
	      as_warn (_("ignoring bad alignment"));
	      align = 2;
	    }
	}
    }
  else
    {
      char *lcomm_name;
      char lcomm_endc;

      if (size <= 4)
	align = 2;
      else
	align = 3;

      /* The third argument to .lcomm appears to be the real local
	 common symbol to create.  References to the symbol named in
	 the first argument are turned into references to the third
	 argument.  */
      if (*input_line_pointer != ',')
	{
	  as_bad (_("missing real symbol name"));
	  ignore_rest_of_line ();
	  return;
	}
      ++input_line_pointer;

      lcomm_name = input_line_pointer;
      lcomm_endc = get_symbol_end ();

      lcomm_sym = symbol_find_or_make (lcomm_name);

      *input_line_pointer = lcomm_endc;
    }

  *end_name = '\0';
  sym = symbol_find_or_make (name);
  *end_name = endc;

  if (S_IS_DEFINED (sym)
      || S_GET_VALUE (sym) != 0)
    {
      as_bad (_("attempt to redefine symbol"));
      ignore_rest_of_line ();
      return;
    }

  record_alignment (bss_section, align);

  if (! lcomm
      || ! S_IS_DEFINED (lcomm_sym))
    {
      symbolS *def_sym;
      offsetT def_size;

      if (! lcomm)
	{
	  def_sym = sym;
	  def_size = size;
	  S_SET_EXTERNAL (sym);
	}
      else
	{
	  symbol_get_tc (lcomm_sym)->output = 1;
	  def_sym = lcomm_sym;
	  def_size = 0;
	}

      subseg_set (bss_section, 1);
      frag_align (align, 0, 0);

      symbol_set_frag (def_sym, frag_now);
      pfrag = frag_var (rs_org, 1, 1, (relax_substateT) 0, def_sym,
			def_size, (char *) NULL);
      *pfrag = 0;
      S_SET_SEGMENT (def_sym, bss_section);
      symbol_get_tc (def_sym)->align = align;
    }
  else if (lcomm)
    {
      /* Align the size of lcomm_sym.  */
      symbol_get_frag (lcomm_sym)->fr_offset =
	((symbol_get_frag (lcomm_sym)->fr_offset + (1 << align) - 1)
	 &~ ((1 << align) - 1));
      if (align > symbol_get_tc (lcomm_sym)->align)
	symbol_get_tc (lcomm_sym)->align = align;
    }

  if (lcomm)
    {
      /* Make sym an offset from lcomm_sym.  */
      S_SET_SEGMENT (sym, bss_section);
      symbol_set_frag (sym, symbol_get_frag (lcomm_sym));
      S_SET_VALUE (sym, symbol_get_frag (lcomm_sym)->fr_offset);
      symbol_get_frag (lcomm_sym)->fr_offset += size;
    }

  subseg_set (current_seg, current_subseg);

  demand_empty_rest_of_line ();
}

/* The .csect pseudo-op.  This switches us into a different
   subsegment.  The first argument is a symbol whose value is the
   start of the .csect.  In COFF, csect symbols get special aux
   entries defined by the x_csect field of union internal_auxent.  The
   optional second argument is the alignment (the default is 2).  */

static void
ppc_csect (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char endc;
  symbolS *sym;
  offsetT align;

  name = input_line_pointer;
  endc = get_symbol_end ();

  sym = symbol_find_or_make (name);

  *input_line_pointer = endc;

  if (S_GET_NAME (sym)[0] == '\0')
    {
      /* An unnamed csect is assumed to be [PR].  */
      symbol_get_tc (sym)->class = XMC_PR;
    }

  align = 2;
  if (*input_line_pointer == ',')
    {
      ++input_line_pointer;
      align = get_absolute_expression ();
    }

  ppc_change_csect (sym, align);

  demand_empty_rest_of_line ();
}

/* Change to a different csect.  */

static void
ppc_change_csect (symbolS *sym, offsetT align)
{
  if (S_IS_DEFINED (sym))
    subseg_set (S_GET_SEGMENT (sym), symbol_get_tc (sym)->subseg);
  else
    {
      symbolS **list_ptr;
      int after_toc;
      int hold_chunksize;
      symbolS *list;
      int is_code;
      segT sec;

      /* This is a new csect.  We need to look at the symbol class to
	 figure out whether it should go in the text section or the
	 data section.  */
      after_toc = 0;
      is_code = 0;
      switch (symbol_get_tc (sym)->class)
	{
	case XMC_PR:
	case XMC_RO:
	case XMC_DB:
	case XMC_GL:
	case XMC_XO:
	case XMC_SV:
	case XMC_TI:
	case XMC_TB:
	  S_SET_SEGMENT (sym, text_section);
	  symbol_get_tc (sym)->subseg = ppc_text_subsegment;
	  ++ppc_text_subsegment;
	  list_ptr = &ppc_text_csects;
	  is_code = 1;
	  break;
	case XMC_RW:
	case XMC_TC0:
	case XMC_TC:
	case XMC_DS:
	case XMC_UA:
	case XMC_BS:
	case XMC_UC:
	  if (ppc_toc_csect != NULL
	      && (symbol_get_tc (ppc_toc_csect)->subseg + 1
		  == ppc_data_subsegment))
	    after_toc = 1;
	  S_SET_SEGMENT (sym, data_section);
	  symbol_get_tc (sym)->subseg = ppc_data_subsegment;
	  ++ppc_data_subsegment;
	  list_ptr = &ppc_data_csects;
	  break;
	default:
	  abort ();
	}

      /* We set the obstack chunk size to a small value before
	 changing subsegments, so that we don't use a lot of memory
	 space for what may be a small section.  */
      hold_chunksize = chunksize;
      chunksize = 64;

      sec = subseg_new (segment_name (S_GET_SEGMENT (sym)),
			symbol_get_tc (sym)->subseg);

      chunksize = hold_chunksize;

      if (after_toc)
	ppc_after_toc_frag = frag_now;

      record_alignment (sec, align);
      if (is_code)
	frag_align_code (align, 0);
      else
	frag_align (align, 0, 0);

      symbol_set_frag (sym, frag_now);
      S_SET_VALUE (sym, (valueT) frag_now_fix ());

      symbol_get_tc (sym)->align = align;
      symbol_get_tc (sym)->output = 1;
      symbol_get_tc (sym)->within = sym;

      for (list = *list_ptr;
	   symbol_get_tc (list)->next != (symbolS *) NULL;
	   list = symbol_get_tc (list)->next)
	;
      symbol_get_tc (list)->next = sym;

      symbol_remove (sym, &symbol_rootP, &symbol_lastP);
      symbol_append (sym, symbol_get_tc (list)->within, &symbol_rootP,
		     &symbol_lastP);
    }

  ppc_current_csect = sym;
}

/* This function handles the .text and .data pseudo-ops.  These
   pseudo-ops aren't really used by XCOFF; we implement them for the
   convenience of people who aren't used to XCOFF.  */

static void
ppc_section (int type)
{
  const char *name;
  symbolS *sym;

  if (type == 't')
    name = ".text[PR]";
  else if (type == 'd')
    name = ".data[RW]";
  else
    abort ();

  sym = symbol_find_or_make (name);

  ppc_change_csect (sym, 2);

  demand_empty_rest_of_line ();
}

/* This function handles the .section pseudo-op.  This is mostly to
   give an error, since XCOFF only supports .text, .data and .bss, but
   we do permit the user to name the text or data section.  */

static void
ppc_named_section (int ignore ATTRIBUTE_UNUSED)
{
  char *user_name;
  const char *real_name;
  char c;
  symbolS *sym;

  user_name = input_line_pointer;
  c = get_symbol_end ();

  if (strcmp (user_name, ".text") == 0)
    real_name = ".text[PR]";
  else if (strcmp (user_name, ".data") == 0)
    real_name = ".data[RW]";
  else
    {
      as_bad (_("The XCOFF file format does not support arbitrary sections"));
      *input_line_pointer = c;
      ignore_rest_of_line ();
      return;
    }

  *input_line_pointer = c;

  sym = symbol_find_or_make (real_name);

  ppc_change_csect (sym, 2);

  demand_empty_rest_of_line ();
}

/* The .extern pseudo-op.  We create an undefined symbol.  */

static void
ppc_extern (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char endc;

  name = input_line_pointer;
  endc = get_symbol_end ();

  (void) symbol_find_or_make (name);

  *input_line_pointer = endc;

  demand_empty_rest_of_line ();
}

/* The .lglobl pseudo-op.  Keep the symbol in the symbol table.  */

static void
ppc_lglobl (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char endc;
  symbolS *sym;

  name = input_line_pointer;
  endc = get_symbol_end ();

  sym = symbol_find_or_make (name);

  *input_line_pointer = endc;

  symbol_get_tc (sym)->output = 1;

  demand_empty_rest_of_line ();
}

/* The .rename pseudo-op.  The RS/6000 assembler can rename symbols,
   although I don't know why it bothers.  */

static void
ppc_rename (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char endc;
  symbolS *sym;
  int len;

  name = input_line_pointer;
  endc = get_symbol_end ();

  sym = symbol_find_or_make (name);

  *input_line_pointer = endc;

  if (*input_line_pointer != ',')
    {
      as_bad (_("missing rename string"));
      ignore_rest_of_line ();
      return;
    }
  ++input_line_pointer;

  symbol_get_tc (sym)->real_name = demand_copy_C_string (&len);

  demand_empty_rest_of_line ();
}

/* The .stabx pseudo-op.  This is similar to a normal .stabs
   pseudo-op, but slightly different.  A sample is
       .stabx "main:F-1",.main,142,0
   The first argument is the symbol name to create.  The second is the
   value, and the third is the storage class.  The fourth seems to be
   always zero, and I am assuming it is the type.  */

static void
ppc_stabx (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int len;
  symbolS *sym;
  expressionS exp;

  name = demand_copy_C_string (&len);

  if (*input_line_pointer != ',')
    {
      as_bad (_("missing value"));
      return;
    }
  ++input_line_pointer;

  ppc_stab_symbol = TRUE;
  sym = symbol_make (name);
  ppc_stab_symbol = FALSE;

  symbol_get_tc (sym)->real_name = name;

  (void) expression (&exp);

  switch (exp.X_op)
    {
    case O_illegal:
    case O_absent:
    case O_big:
      as_bad (_("illegal .stabx expression; zero assumed"));
      exp.X_add_number = 0;
      /* Fall through.  */
    case O_constant:
      S_SET_VALUE (sym, (valueT) exp.X_add_number);
      symbol_set_frag (sym, &zero_address_frag);
      break;

    case O_symbol:
      if (S_GET_SEGMENT (exp.X_add_symbol) == undefined_section)
	symbol_set_value_expression (sym, &exp);
      else
	{
	  S_SET_VALUE (sym,
		       exp.X_add_number + S_GET_VALUE (exp.X_add_symbol));
	  symbol_set_frag (sym, symbol_get_frag (exp.X_add_symbol));
	}
      break;

    default:
      /* The value is some complex expression.  This will probably
	 fail at some later point, but this is probably the right
	 thing to do here.  */
      symbol_set_value_expression (sym, &exp);
      break;
    }

  S_SET_SEGMENT (sym, ppc_coff_debug_section);
  symbol_get_bfdsym (sym)->flags |= BSF_DEBUGGING;

  if (*input_line_pointer != ',')
    {
      as_bad (_("missing class"));
      return;
    }
  ++input_line_pointer;

  S_SET_STORAGE_CLASS (sym, get_absolute_expression ());

  if (*input_line_pointer != ',')
    {
      as_bad (_("missing type"));
      return;
    }
  ++input_line_pointer;

  S_SET_DATA_TYPE (sym, get_absolute_expression ());

  symbol_get_tc (sym)->output = 1;

  if (S_GET_STORAGE_CLASS (sym) == C_STSYM) {

    symbol_get_tc (sym)->within = ppc_current_block;

    /* In this case :

       .bs name
       .stabx	"z",arrays_,133,0
       .es

       .comm arrays_,13768,3

       resolve_symbol_value will copy the exp's "within" into sym's when the
       offset is 0.  Since this seems to be corner case problem,
       only do the correction for storage class C_STSYM.  A better solution
       would be to have the tc field updated in ppc_symbol_new_hook.  */

    if (exp.X_op == O_symbol)
      {
	symbol_get_tc (exp.X_add_symbol)->within = ppc_current_block;
      }
  }

  if (exp.X_op != O_symbol
      || ! S_IS_EXTERNAL (exp.X_add_symbol)
      || S_GET_SEGMENT (exp.X_add_symbol) != bss_section)
    ppc_frob_label (sym);
  else
    {
      symbol_remove (sym, &symbol_rootP, &symbol_lastP);
      symbol_append (sym, exp.X_add_symbol, &symbol_rootP, &symbol_lastP);
      if (symbol_get_tc (ppc_current_csect)->within == exp.X_add_symbol)
	symbol_get_tc (ppc_current_csect)->within = sym;
    }

  demand_empty_rest_of_line ();
}

/* The .function pseudo-op.  This takes several arguments.  The first
   argument seems to be the external name of the symbol.  The second
   argument seems to be the label for the start of the function.  gcc
   uses the same name for both.  I have no idea what the third and
   fourth arguments are meant to be.  The optional fifth argument is
   an expression for the size of the function.  In COFF this symbol
   gets an aux entry like that used for a csect.  */

static void
ppc_function (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char endc;
  char *s;
  symbolS *ext_sym;
  symbolS *lab_sym;

  name = input_line_pointer;
  endc = get_symbol_end ();

  /* Ignore any [PR] suffix.  */
  name = ppc_canonicalize_symbol_name (name);
  s = strchr (name, '[');
  if (s != (char *) NULL
      && strcmp (s + 1, "PR]") == 0)
    *s = '\0';

  ext_sym = symbol_find_or_make (name);

  *input_line_pointer = endc;

  if (*input_line_pointer != ',')
    {
      as_bad (_("missing symbol name"));
      ignore_rest_of_line ();
      return;
    }
  ++input_line_pointer;

  name = input_line_pointer;
  endc = get_symbol_end ();

  lab_sym = symbol_find_or_make (name);

  *input_line_pointer = endc;

  if (ext_sym != lab_sym)
    {
      expressionS exp;

      exp.X_op = O_symbol;
      exp.X_add_symbol = lab_sym;
      exp.X_op_symbol = NULL;
      exp.X_add_number = 0;
      exp.X_unsigned = 0;
      symbol_set_value_expression (ext_sym, &exp);
    }

  if (symbol_get_tc (ext_sym)->class == -1)
    symbol_get_tc (ext_sym)->class = XMC_PR;
  symbol_get_tc (ext_sym)->output = 1;

  if (*input_line_pointer == ',')
    {
      expressionS ignore;

      /* Ignore the third argument.  */
      ++input_line_pointer;
      expression (&ignore);
      if (*input_line_pointer == ',')
	{
	  /* Ignore the fourth argument.  */
	  ++input_line_pointer;
	  expression (&ignore);
	  if (*input_line_pointer == ',')
	    {
	      /* The fifth argument is the function size.  */
	      ++input_line_pointer;
	      symbol_get_tc (ext_sym)->size = symbol_new ("L0\001",
							  absolute_section,
							  (valueT) 0,
							  &zero_address_frag);
	      pseudo_set (symbol_get_tc (ext_sym)->size);
	    }
	}
    }

  S_SET_DATA_TYPE (ext_sym, DT_FCN << N_BTSHFT);
  SF_SET_FUNCTION (ext_sym);
  SF_SET_PROCESS (ext_sym);
  coff_add_linesym (ext_sym);

  demand_empty_rest_of_line ();
}

/* The .bf pseudo-op.  This is just like a COFF C_FCN symbol named
   ".bf".  If the pseudo op .bi was seen before .bf, patch the .bi sym
   with the correct line number */

static symbolS *saved_bi_sym = 0;

static void
ppc_bf (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *sym;

  sym = symbol_make (".bf");
  S_SET_SEGMENT (sym, text_section);
  symbol_set_frag (sym, frag_now);
  S_SET_VALUE (sym, frag_now_fix ());
  S_SET_STORAGE_CLASS (sym, C_FCN);

  coff_line_base = get_absolute_expression ();

  S_SET_NUMBER_AUXILIARY (sym, 1);
  SA_SET_SYM_LNNO (sym, coff_line_base);

  /* Line number for bi.  */
  if (saved_bi_sym)
    {
      S_SET_VALUE (saved_bi_sym, coff_n_line_nos);
      saved_bi_sym = 0;
    }


  symbol_get_tc (sym)->output = 1;

  ppc_frob_label (sym);

  demand_empty_rest_of_line ();
}

/* The .ef pseudo-op.  This is just like a COFF C_FCN symbol named
   ".ef", except that the line number is absolute, not relative to the
   most recent ".bf" symbol.  */

static void
ppc_ef (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *sym;

  sym = symbol_make (".ef");
  S_SET_SEGMENT (sym, text_section);
  symbol_set_frag (sym, frag_now);
  S_SET_VALUE (sym, frag_now_fix ());
  S_SET_STORAGE_CLASS (sym, C_FCN);
  S_SET_NUMBER_AUXILIARY (sym, 1);
  SA_SET_SYM_LNNO (sym, get_absolute_expression ());
  symbol_get_tc (sym)->output = 1;

  ppc_frob_label (sym);

  demand_empty_rest_of_line ();
}

/* The .bi and .ei pseudo-ops.  These take a string argument and
   generates a C_BINCL or C_EINCL symbol, which goes at the start of
   the symbol list.  The value of .bi will be know when the next .bf
   is encountered.  */

static void
ppc_biei (int ei)
{
  static symbolS *last_biei;

  char *name;
  int len;
  symbolS *sym;
  symbolS *look;

  name = demand_copy_C_string (&len);

  /* The value of these symbols is actually file offset.  Here we set
     the value to the index into the line number entries.  In
     ppc_frob_symbols we set the fix_line field, which will cause BFD
     to do the right thing.  */

  sym = symbol_make (name);
  /* obj-coff.c currently only handles line numbers correctly in the
     .text section.  */
  S_SET_SEGMENT (sym, text_section);
  S_SET_VALUE (sym, coff_n_line_nos);
  symbol_get_bfdsym (sym)->flags |= BSF_DEBUGGING;

  S_SET_STORAGE_CLASS (sym, ei ? C_EINCL : C_BINCL);
  symbol_get_tc (sym)->output = 1;

  /* Save bi.  */
  if (ei)
    saved_bi_sym = 0;
  else
    saved_bi_sym = sym;

  for (look = last_biei ? last_biei : symbol_rootP;
       (look != (symbolS *) NULL
	&& (S_GET_STORAGE_CLASS (look) == C_FILE
	    || S_GET_STORAGE_CLASS (look) == C_BINCL
	    || S_GET_STORAGE_CLASS (look) == C_EINCL));
       look = symbol_next (look))
    ;
  if (look != (symbolS *) NULL)
    {
      symbol_remove (sym, &symbol_rootP, &symbol_lastP);
      symbol_insert (sym, look, &symbol_rootP, &symbol_lastP);
      last_biei = sym;
    }

  demand_empty_rest_of_line ();
}

/* The .bs pseudo-op.  This generates a C_BSTAT symbol named ".bs".
   There is one argument, which is a csect symbol.  The value of the
   .bs symbol is the index of this csect symbol.  */

static void
ppc_bs (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char endc;
  symbolS *csect;
  symbolS *sym;

  if (ppc_current_block != NULL)
    as_bad (_("nested .bs blocks"));

  name = input_line_pointer;
  endc = get_symbol_end ();

  csect = symbol_find_or_make (name);

  *input_line_pointer = endc;

  sym = symbol_make (".bs");
  S_SET_SEGMENT (sym, now_seg);
  S_SET_STORAGE_CLASS (sym, C_BSTAT);
  symbol_get_bfdsym (sym)->flags |= BSF_DEBUGGING;
  symbol_get_tc (sym)->output = 1;

  symbol_get_tc (sym)->within = csect;

  ppc_frob_label (sym);

  ppc_current_block = sym;

  demand_empty_rest_of_line ();
}

/* The .es pseudo-op.  Generate a C_ESTART symbol named .es.  */

static void
ppc_es (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *sym;

  if (ppc_current_block == NULL)
    as_bad (_(".es without preceding .bs"));

  sym = symbol_make (".es");
  S_SET_SEGMENT (sym, now_seg);
  S_SET_STORAGE_CLASS (sym, C_ESTAT);
  symbol_get_bfdsym (sym)->flags |= BSF_DEBUGGING;
  symbol_get_tc (sym)->output = 1;

  ppc_frob_label (sym);

  ppc_current_block = NULL;

  demand_empty_rest_of_line ();
}

/* The .bb pseudo-op.  Generate a C_BLOCK symbol named .bb, with a
   line number.  */

static void
ppc_bb (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *sym;

  sym = symbol_make (".bb");
  S_SET_SEGMENT (sym, text_section);
  symbol_set_frag (sym, frag_now);
  S_SET_VALUE (sym, frag_now_fix ());
  S_SET_STORAGE_CLASS (sym, C_BLOCK);

  S_SET_NUMBER_AUXILIARY (sym, 1);
  SA_SET_SYM_LNNO (sym, get_absolute_expression ());

  symbol_get_tc (sym)->output = 1;

  SF_SET_PROCESS (sym);

  ppc_frob_label (sym);

  demand_empty_rest_of_line ();
}

/* The .eb pseudo-op.  Generate a C_BLOCK symbol named .eb, with a
   line number.  */

static void
ppc_eb (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *sym;

  sym = symbol_make (".eb");
  S_SET_SEGMENT (sym, text_section);
  symbol_set_frag (sym, frag_now);
  S_SET_VALUE (sym, frag_now_fix ());
  S_SET_STORAGE_CLASS (sym, C_BLOCK);
  S_SET_NUMBER_AUXILIARY (sym, 1);
  SA_SET_SYM_LNNO (sym, get_absolute_expression ());
  symbol_get_tc (sym)->output = 1;

  SF_SET_PROCESS (sym);

  ppc_frob_label (sym);

  demand_empty_rest_of_line ();
}

/* The .bc pseudo-op.  This just creates a C_BCOMM symbol with a
   specified name.  */

static void
ppc_bc (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int len;
  symbolS *sym;

  name = demand_copy_C_string (&len);
  sym = symbol_make (name);
  S_SET_SEGMENT (sym, ppc_coff_debug_section);
  symbol_get_bfdsym (sym)->flags |= BSF_DEBUGGING;
  S_SET_STORAGE_CLASS (sym, C_BCOMM);
  S_SET_VALUE (sym, 0);
  symbol_get_tc (sym)->output = 1;

  ppc_frob_label (sym);

  demand_empty_rest_of_line ();
}

/* The .ec pseudo-op.  This just creates a C_ECOMM symbol.  */

static void
ppc_ec (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *sym;

  sym = symbol_make (".ec");
  S_SET_SEGMENT (sym, ppc_coff_debug_section);
  symbol_get_bfdsym (sym)->flags |= BSF_DEBUGGING;
  S_SET_STORAGE_CLASS (sym, C_ECOMM);
  S_SET_VALUE (sym, 0);
  symbol_get_tc (sym)->output = 1;

  ppc_frob_label (sym);

  demand_empty_rest_of_line ();
}

/* The .toc pseudo-op.  Switch to the .toc subsegment.  */

static void
ppc_toc (int ignore ATTRIBUTE_UNUSED)
{
  if (ppc_toc_csect != (symbolS *) NULL)
    subseg_set (data_section, symbol_get_tc (ppc_toc_csect)->subseg);
  else
    {
      subsegT subseg;
      symbolS *sym;
      symbolS *list;

      subseg = ppc_data_subsegment;
      ++ppc_data_subsegment;

      subseg_new (segment_name (data_section), subseg);
      ppc_toc_frag = frag_now;

      sym = symbol_find_or_make ("TOC[TC0]");
      symbol_set_frag (sym, frag_now);
      S_SET_SEGMENT (sym, data_section);
      S_SET_VALUE (sym, (valueT) frag_now_fix ());
      symbol_get_tc (sym)->subseg = subseg;
      symbol_get_tc (sym)->output = 1;
      symbol_get_tc (sym)->within = sym;

      ppc_toc_csect = sym;

      for (list = ppc_data_csects;
	   symbol_get_tc (list)->next != (symbolS *) NULL;
	   list = symbol_get_tc (list)->next)
	;
      symbol_get_tc (list)->next = sym;

      symbol_remove (sym, &symbol_rootP, &symbol_lastP);
      symbol_append (sym, symbol_get_tc (list)->within, &symbol_rootP,
		     &symbol_lastP);
    }

  ppc_current_csect = ppc_toc_csect;

  demand_empty_rest_of_line ();
}

/* The AIX assembler automatically aligns the operands of a .long or
   .short pseudo-op, and we want to be compatible.  */

static void
ppc_xcoff_cons (int log_size)
{
  frag_align (log_size, 0, 0);
  record_alignment (now_seg, log_size);
  cons (1 << log_size);
}

static void
ppc_vbyte (int dummy ATTRIBUTE_UNUSED)
{
  expressionS exp;
  int byte_count;

  (void) expression (&exp);

  if (exp.X_op != O_constant)
    {
      as_bad (_("non-constant byte count"));
      return;
    }

  byte_count = exp.X_add_number;

  if (*input_line_pointer != ',')
    {
      as_bad (_("missing value"));
      return;
    }

  ++input_line_pointer;
  cons (byte_count);
}

#endif /* OBJ_XCOFF */
#if defined (OBJ_XCOFF) || defined (OBJ_ELF)

/* The .tc pseudo-op.  This is used when generating either XCOFF or
   ELF.  This takes two or more arguments.

   When generating XCOFF output, the first argument is the name to
   give to this location in the toc; this will be a symbol with class
   TC.  The rest of the arguments are N-byte values to actually put at
   this location in the TOC; often there is just one more argument, a
   relocatable symbol reference.  The size of the value to store
   depends on target word size.  A 32-bit target uses 4-byte values, a
   64-bit target uses 8-byte values.

   When not generating XCOFF output, the arguments are the same, but
   the first argument is simply ignored.  */

static void
ppc_tc (int ignore ATTRIBUTE_UNUSED)
{
#ifdef OBJ_XCOFF

  /* Define the TOC symbol name.  */
  {
    char *name;
    char endc;
    symbolS *sym;

    if (ppc_toc_csect == (symbolS *) NULL
	|| ppc_toc_csect != ppc_current_csect)
      {
	as_bad (_(".tc not in .toc section"));
	ignore_rest_of_line ();
	return;
      }

    name = input_line_pointer;
    endc = get_symbol_end ();

    sym = symbol_find_or_make (name);

    *input_line_pointer = endc;

    if (S_IS_DEFINED (sym))
      {
	symbolS *label;

	label = symbol_get_tc (ppc_current_csect)->within;
	if (symbol_get_tc (label)->class != XMC_TC0)
	  {
	    as_bad (_(".tc with no label"));
	    ignore_rest_of_line ();
	    return;
	  }

	S_SET_SEGMENT (label, S_GET_SEGMENT (sym));
	symbol_set_frag (label, symbol_get_frag (sym));
	S_SET_VALUE (label, S_GET_VALUE (sym));

	while (! is_end_of_line[(unsigned char) *input_line_pointer])
	  ++input_line_pointer;

	return;
      }

    S_SET_SEGMENT (sym, now_seg);
    symbol_set_frag (sym, frag_now);
    S_SET_VALUE (sym, (valueT) frag_now_fix ());
    symbol_get_tc (sym)->class = XMC_TC;
    symbol_get_tc (sym)->output = 1;

    ppc_frob_label (sym);
  }

#endif /* OBJ_XCOFF */
#ifdef OBJ_ELF
  int align;

  /* Skip the TOC symbol name.  */
  while (is_part_of_name (*input_line_pointer)
	 || *input_line_pointer == '['
	 || *input_line_pointer == ']'
	 || *input_line_pointer == '{'
	 || *input_line_pointer == '}')
    ++input_line_pointer;

  /* Align to a four/eight byte boundary.  */
  align = ppc_obj64 ? 3 : 2;
  frag_align (align, 0, 0);
  record_alignment (now_seg, align);
#endif /* OBJ_ELF */

  if (*input_line_pointer != ',')
    demand_empty_rest_of_line ();
  else
    {
      ++input_line_pointer;
      cons (ppc_obj64 ? 8 : 4);
    }
}

/* Pseudo-op .machine.  */

static void
ppc_machine (int ignore ATTRIBUTE_UNUSED)
{
  char *cpu_string;
#define MAX_HISTORY 100
  static unsigned long *cpu_history;
  static int curr_hist;

  SKIP_WHITESPACE ();

  if (*input_line_pointer == '"')
    {
      int len;
      cpu_string = demand_copy_C_string (&len);
    }
  else
    {
      char c;
      cpu_string = input_line_pointer;
      c = get_symbol_end ();
      cpu_string = xstrdup (cpu_string);
      *input_line_pointer = c;
    }

  if (cpu_string != NULL)
    {
      unsigned long old_cpu = ppc_cpu;
      char *p;

      for (p = cpu_string; *p != 0; p++)
	*p = TOLOWER (*p);

      if (strcmp (cpu_string, "push") == 0)
	{
	  if (cpu_history == NULL)
	    cpu_history = xmalloc (MAX_HISTORY * sizeof (*cpu_history));

	  if (curr_hist >= MAX_HISTORY)
	    as_bad (_(".machine stack overflow"));
	  else
	    cpu_history[curr_hist++] = ppc_cpu;
	}
      else if (strcmp (cpu_string, "pop") == 0)
	{
	  if (curr_hist <= 0)
	    as_bad (_(".machine stack underflow"));
	  else
	    ppc_cpu = cpu_history[--curr_hist];
	}
      else if (parse_cpu (cpu_string))
	;
      else
	as_bad (_("invalid machine `%s'"), cpu_string);

      if (ppc_cpu != old_cpu)
	ppc_setup_opcodes ();
    }

  demand_empty_rest_of_line ();
}

/* See whether a symbol is in the TOC section.  */

static int
ppc_is_toc_sym (symbolS *sym)
{
#ifdef OBJ_XCOFF
  return symbol_get_tc (sym)->class == XMC_TC;
#endif
#ifdef OBJ_ELF
  const char *sname = segment_name (S_GET_SEGMENT (sym));
  if (ppc_obj64)
    return strcmp (sname, ".toc") == 0;
  else
    return strcmp (sname, ".got") == 0;
#endif
}
#endif /* defined (OBJ_XCOFF) || defined (OBJ_ELF) */

#ifdef TE_PE

/* Pseudo-ops specific to the Windows NT PowerPC PE (coff) format.  */

/* Set the current section.  */
static void
ppc_set_current_section (segT new)
{
  ppc_previous_section = ppc_current_section;
  ppc_current_section = new;
}

/* pseudo-op: .previous
   behaviour: toggles the current section with the previous section.
   errors:    None
   warnings:  "No previous section"  */

static void
ppc_previous (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *tmp;

  if (ppc_previous_section == NULL)
    {
      as_warn (_("No previous section to return to. Directive ignored."));
      return;
    }

  subseg_set (ppc_previous_section, 0);

  ppc_set_current_section (ppc_previous_section);
}

/* pseudo-op: .pdata
   behaviour: predefined read only data section
	      double word aligned
   errors:    None
   warnings:  None
   initial:   .section .pdata "adr3"
	      a - don't know -- maybe a misprint
	      d - initialized data
	      r - readable
	      3 - double word aligned (that would be 4 byte boundary)

   commentary:
   Tag index tables (also known as the function table) for exception
   handling, debugging, etc.  */

static void
ppc_pdata (int ignore ATTRIBUTE_UNUSED)
{
  if (pdata_section == 0)
    {
      pdata_section = subseg_new (".pdata", 0);

      bfd_set_section_flags (stdoutput, pdata_section,
			     (SEC_ALLOC | SEC_LOAD | SEC_RELOC
			      | SEC_READONLY | SEC_DATA ));

      bfd_set_section_alignment (stdoutput, pdata_section, 2);
    }
  else
    {
      pdata_section = subseg_new (".pdata", 0);
    }
  ppc_set_current_section (pdata_section);
}

/* pseudo-op: .ydata
   behaviour: predefined read only data section
	      double word aligned
   errors:    None
   warnings:  None
   initial:   .section .ydata "drw3"
	      a - don't know -- maybe a misprint
	      d - initialized data
	      r - readable
	      3 - double word aligned (that would be 4 byte boundary)
   commentary:
   Tag tables (also known as the scope table) for exception handling,
   debugging, etc.  */

static void
ppc_ydata (int ignore ATTRIBUTE_UNUSED)
{
  if (ydata_section == 0)
    {
      ydata_section = subseg_new (".ydata", 0);
      bfd_set_section_flags (stdoutput, ydata_section,
			     (SEC_ALLOC | SEC_LOAD | SEC_RELOC
			      | SEC_READONLY | SEC_DATA ));

      bfd_set_section_alignment (stdoutput, ydata_section, 3);
    }
  else
    {
      ydata_section = subseg_new (".ydata", 0);
    }
  ppc_set_current_section (ydata_section);
}

/* pseudo-op: .reldata
   behaviour: predefined read write data section
	      double word aligned (4-byte)
	      FIXME: relocation is applied to it
	      FIXME: what's the difference between this and .data?
   errors:    None
   warnings:  None
   initial:   .section .reldata "drw3"
	      d - initialized data
	      r - readable
	      w - writeable
	      3 - double word aligned (that would be 8 byte boundary)

   commentary:
   Like .data, but intended to hold data subject to relocation, such as
   function descriptors, etc.  */

static void
ppc_reldata (int ignore ATTRIBUTE_UNUSED)
{
  if (reldata_section == 0)
    {
      reldata_section = subseg_new (".reldata", 0);

      bfd_set_section_flags (stdoutput, reldata_section,
			     (SEC_ALLOC | SEC_LOAD | SEC_RELOC
			      | SEC_DATA));

      bfd_set_section_alignment (stdoutput, reldata_section, 2);
    }
  else
    {
      reldata_section = subseg_new (".reldata", 0);
    }
  ppc_set_current_section (reldata_section);
}

/* pseudo-op: .rdata
   behaviour: predefined read only data section
	      double word aligned
   errors:    None
   warnings:  None
   initial:   .section .rdata "dr3"
	      d - initialized data
	      r - readable
	      3 - double word aligned (that would be 4 byte boundary)  */

static void
ppc_rdata (int ignore ATTRIBUTE_UNUSED)
{
  if (rdata_section == 0)
    {
      rdata_section = subseg_new (".rdata", 0);
      bfd_set_section_flags (stdoutput, rdata_section,
			     (SEC_ALLOC | SEC_LOAD | SEC_RELOC
			      | SEC_READONLY | SEC_DATA ));

      bfd_set_section_alignment (stdoutput, rdata_section, 2);
    }
  else
    {
      rdata_section = subseg_new (".rdata", 0);
    }
  ppc_set_current_section (rdata_section);
}

/* pseudo-op: .ualong
   behaviour: much like .int, with the exception that no alignment is
	      performed.
	      FIXME: test the alignment statement
   errors:    None
   warnings:  None  */

static void
ppc_ualong (int ignore ATTRIBUTE_UNUSED)
{
  /* Try for long.  */
  cons (4);
}

/* pseudo-op: .znop  <symbol name>
   behaviour: Issue a nop instruction
	      Issue a IMAGE_REL_PPC_IFGLUE relocation against it, using
	      the supplied symbol name.
   errors:    None
   warnings:  Missing symbol name  */

static void
ppc_znop (int ignore ATTRIBUTE_UNUSED)
{
  unsigned long insn;
  const struct powerpc_opcode *opcode;
  expressionS ex;
  char *f;
  symbolS *sym;
  char *symbol_name;
  char c;
  char *name;
  unsigned int exp;
  flagword flags;
  asection *sec;

  /* Strip out the symbol name.  */
  symbol_name = input_line_pointer;
  c = get_symbol_end ();

  name = xmalloc (input_line_pointer - symbol_name + 1);
  strcpy (name, symbol_name);

  sym = symbol_find_or_make (name);

  *input_line_pointer = c;

  SKIP_WHITESPACE ();

  /* Look up the opcode in the hash table.  */
  opcode = (const struct powerpc_opcode *) hash_find (ppc_hash, "nop");

  /* Stick in the nop.  */
  insn = opcode->opcode;

  /* Write out the instruction.  */
  f = frag_more (4);
  md_number_to_chars (f, insn, 4);
  fix_new (frag_now,
	   f - frag_now->fr_literal,
	   4,
	   sym,
	   0,
	   0,
	   BFD_RELOC_16_GOT_PCREL);

}

/* pseudo-op:
   behaviour:
   errors:
   warnings:  */

static void
ppc_pe_comm (int lcomm)
{
  char *name;
  char c;
  char *p;
  offsetT temp;
  symbolS *symbolP;
  offsetT align;

  name = input_line_pointer;
  c = get_symbol_end ();

  /* just after name is now '\0'.  */
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad (_("Expected comma after symbol-name: rest of line ignored."));
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ',' */
  if ((temp = get_absolute_expression ()) < 0)
    {
      as_warn (_(".COMMon length (%ld.) <0! Ignored."), (long) temp);
      ignore_rest_of_line ();
      return;
    }

  if (! lcomm)
    {
      /* The third argument to .comm is the alignment.  */
      if (*input_line_pointer != ',')
	align = 3;
      else
	{
	  ++input_line_pointer;
	  align = get_absolute_expression ();
	  if (align <= 0)
	    {
	      as_warn (_("ignoring bad alignment"));
	      align = 3;
	    }
	}
    }

  *p = 0;
  symbolP = symbol_find_or_make (name);

  *p = c;
  if (S_IS_DEFINED (symbolP) && ! S_IS_COMMON (symbolP))
    {
      as_bad (_("Ignoring attempt to re-define symbol `%s'."),
	      S_GET_NAME (symbolP));
      ignore_rest_of_line ();
      return;
    }

  if (S_GET_VALUE (symbolP))
    {
      if (S_GET_VALUE (symbolP) != (valueT) temp)
	as_bad (_("Length of .comm \"%s\" is already %ld. Not changed to %ld."),
		S_GET_NAME (symbolP),
		(long) S_GET_VALUE (symbolP),
		(long) temp);
    }
  else
    {
      S_SET_VALUE (symbolP, (valueT) temp);
      S_SET_EXTERNAL (symbolP);
      S_SET_SEGMENT (symbolP, bfd_com_section_ptr);
    }

  demand_empty_rest_of_line ();
}

/*
 * implement the .section pseudo op:
 *	.section name {, "flags"}
 *                ^         ^
 *                |         +--- optional flags: 'b' for bss
 *                |                              'i' for info
 *                +-- section name               'l' for lib
 *                                               'n' for noload
 *                                               'o' for over
 *                                               'w' for data
 *						 'd' (apparently m88k for data)
 *                                               'x' for text
 * But if the argument is not a quoted string, treat it as a
 * subsegment number.
 *
 * FIXME: this is a copy of the section processing from obj-coff.c, with
 * additions/changes for the moto-pas assembler support. There are three
 * categories:
 *
 * FIXME: I just noticed this. This doesn't work at all really. It it
 *        setting bits that bfd probably neither understands or uses. The
 *        correct approach (?) will have to incorporate extra fields attached
 *        to the section to hold the system specific stuff. (krk)
 *
 * Section Contents:
 * 'a' - unknown - referred to in documentation, but no definition supplied
 * 'c' - section has code
 * 'd' - section has initialized data
 * 'u' - section has uninitialized data
 * 'i' - section contains directives (info)
 * 'n' - section can be discarded
 * 'R' - remove section at link time
 *
 * Section Protection:
 * 'r' - section is readable
 * 'w' - section is writeable
 * 'x' - section is executable
 * 's' - section is sharable
 *
 * Section Alignment:
 * '0' - align to byte boundary
 * '1' - align to halfword undary
 * '2' - align to word boundary
 * '3' - align to doubleword boundary
 * '4' - align to quadword boundary
 * '5' - align to 32 byte boundary
 * '6' - align to 64 byte boundary
 *
 */

void
ppc_pe_section (int ignore ATTRIBUTE_UNUSED)
{
  /* Strip out the section name.  */
  char *section_name;
  char c;
  char *name;
  unsigned int exp;
  flagword flags;
  segT sec;
  int align;

  section_name = input_line_pointer;
  c = get_symbol_end ();

  name = xmalloc (input_line_pointer - section_name + 1);
  strcpy (name, section_name);

  *input_line_pointer = c;

  SKIP_WHITESPACE ();

  exp = 0;
  flags = SEC_NO_FLAGS;

  if (strcmp (name, ".idata$2") == 0)
    {
      align = 0;
    }
  else if (strcmp (name, ".idata$3") == 0)
    {
      align = 0;
    }
  else if (strcmp (name, ".idata$4") == 0)
    {
      align = 2;
    }
  else if (strcmp (name, ".idata$5") == 0)
    {
      align = 2;
    }
  else if (strcmp (name, ".idata$6") == 0)
    {
      align = 1;
    }
  else
    /* Default alignment to 16 byte boundary.  */
    align = 4;

  if (*input_line_pointer == ',')
    {
      ++input_line_pointer;
      SKIP_WHITESPACE ();
      if (*input_line_pointer != '"')
	exp = get_absolute_expression ();
      else
	{
	  ++input_line_pointer;
	  while (*input_line_pointer != '"'
		 && ! is_end_of_line[(unsigned char) *input_line_pointer])
	    {
	      switch (*input_line_pointer)
		{
		  /* Section Contents */
		case 'a': /* unknown */
		  as_bad (_("Unsupported section attribute -- 'a'"));
		  break;
		case 'c': /* code section */
		  flags |= SEC_CODE;
		  break;
		case 'd': /* section has initialized data */
		  flags |= SEC_DATA;
		  break;
		case 'u': /* section has uninitialized data */
		  /* FIXME: This is IMAGE_SCN_CNT_UNINITIALIZED_DATA
		     in winnt.h */
		  flags |= SEC_ROM;
		  break;
		case 'i': /* section contains directives (info) */
		  /* FIXME: This is IMAGE_SCN_LNK_INFO
		     in winnt.h */
		  flags |= SEC_HAS_CONTENTS;
		  break;
		case 'n': /* section can be discarded */
		  flags &=~ SEC_LOAD;
		  break;
		case 'R': /* Remove section at link time */
		  flags |= SEC_NEVER_LOAD;
		  break;
#if IFLICT_BRAIN_DAMAGE
		  /* Section Protection */
		case 'r': /* section is readable */
		  flags |= IMAGE_SCN_MEM_READ;
		  break;
		case 'w': /* section is writeable */
		  flags |= IMAGE_SCN_MEM_WRITE;
		  break;
		case 'x': /* section is executable */
		  flags |= IMAGE_SCN_MEM_EXECUTE;
		  break;
		case 's': /* section is sharable */
		  flags |= IMAGE_SCN_MEM_SHARED;
		  break;

		  /* Section Alignment */
		case '0': /* align to byte boundary */
		  flags |= IMAGE_SCN_ALIGN_1BYTES;
		  align = 0;
		  break;
		case '1':  /* align to halfword boundary */
		  flags |= IMAGE_SCN_ALIGN_2BYTES;
		  align = 1;
		  break;
		case '2':  /* align to word boundary */
		  flags |= IMAGE_SCN_ALIGN_4BYTES;
		  align = 2;
		  break;
		case '3':  /* align to doubleword boundary */
		  flags |= IMAGE_SCN_ALIGN_8BYTES;
		  align = 3;
		  break;
		case '4':  /* align to quadword boundary */
		  flags |= IMAGE_SCN_ALIGN_16BYTES;
		  align = 4;
		  break;
		case '5':  /* align to 32 byte boundary */
		  flags |= IMAGE_SCN_ALIGN_32BYTES;
		  align = 5;
		  break;
		case '6':  /* align to 64 byte boundary */
		  flags |= IMAGE_SCN_ALIGN_64BYTES;
		  align = 6;
		  break;
#endif
		default:
		  as_bad (_("unknown section attribute '%c'"),
			  *input_line_pointer);
		  break;
		}
	      ++input_line_pointer;
	    }
	  if (*input_line_pointer == '"')
	    ++input_line_pointer;
	}
    }

  sec = subseg_new (name, (subsegT) exp);

  ppc_set_current_section (sec);

  if (flags != SEC_NO_FLAGS)
    {
      if (! bfd_set_section_flags (stdoutput, sec, flags))
	as_bad (_("error setting flags for \"%s\": %s"),
		bfd_section_name (stdoutput, sec),
		bfd_errmsg (bfd_get_error ()));
    }

  bfd_set_section_alignment (stdoutput, sec, align);
}

static void
ppc_pe_function (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char endc;
  symbolS *ext_sym;

  name = input_line_pointer;
  endc = get_symbol_end ();

  ext_sym = symbol_find_or_make (name);

  *input_line_pointer = endc;

  S_SET_DATA_TYPE (ext_sym, DT_FCN << N_BTSHFT);
  SF_SET_FUNCTION (ext_sym);
  SF_SET_PROCESS (ext_sym);
  coff_add_linesym (ext_sym);

  demand_empty_rest_of_line ();
}

static void
ppc_pe_tocd (int ignore ATTRIBUTE_UNUSED)
{
  if (tocdata_section == 0)
    {
      tocdata_section = subseg_new (".tocd", 0);
      /* FIXME: section flags won't work.  */
      bfd_set_section_flags (stdoutput, tocdata_section,
			     (SEC_ALLOC | SEC_LOAD | SEC_RELOC
			      | SEC_READONLY | SEC_DATA));

      bfd_set_section_alignment (stdoutput, tocdata_section, 2);
    }
  else
    {
      rdata_section = subseg_new (".tocd", 0);
    }

  ppc_set_current_section (tocdata_section);

  demand_empty_rest_of_line ();
}

/* Don't adjust TOC relocs to use the section symbol.  */

int
ppc_pe_fix_adjustable (fixS *fix)
{
  return fix->fx_r_type != BFD_RELOC_PPC_TOC16;
}

#endif

#ifdef OBJ_XCOFF

/* XCOFF specific symbol and file handling.  */

/* Canonicalize the symbol name.  We use the to force the suffix, if
   any, to use square brackets, and to be in upper case.  */

char *
ppc_canonicalize_symbol_name (char *name)
{
  char *s;

  if (ppc_stab_symbol)
    return name;

  for (s = name; *s != '\0' && *s != '{' && *s != '['; s++)
    ;
  if (*s != '\0')
    {
      char brac;

      if (*s == '[')
	brac = ']';
      else
	{
	  *s = '[';
	  brac = '}';
	}

      for (s++; *s != '\0' && *s != brac; s++)
	*s = TOUPPER (*s);

      if (*s == '\0' || s[1] != '\0')
	as_bad (_("bad symbol suffix"));

      *s = ']';
    }

  return name;
}

/* Set the class of a symbol based on the suffix, if any.  This is
   called whenever a new symbol is created.  */

void
ppc_symbol_new_hook (symbolS *sym)
{
  struct ppc_tc_sy *tc;
  const char *s;

  tc = symbol_get_tc (sym);
  tc->next = NULL;
  tc->output = 0;
  tc->class = -1;
  tc->real_name = NULL;
  tc->subseg = 0;
  tc->align = 0;
  tc->size = NULL;
  tc->within = NULL;

  if (ppc_stab_symbol)
    return;

  s = strchr (S_GET_NAME (sym), '[');
  if (s == (const char *) NULL)
    {
      /* There is no suffix.  */
      return;
    }

  ++s;

  switch (s[0])
    {
    case 'B':
      if (strcmp (s, "BS]") == 0)
	tc->class = XMC_BS;
      break;
    case 'D':
      if (strcmp (s, "DB]") == 0)
	tc->class = XMC_DB;
      else if (strcmp (s, "DS]") == 0)
	tc->class = XMC_DS;
      break;
    case 'G':
      if (strcmp (s, "GL]") == 0)
	tc->class = XMC_GL;
      break;
    case 'P':
      if (strcmp (s, "PR]") == 0)
	tc->class = XMC_PR;
      break;
    case 'R':
      if (strcmp (s, "RO]") == 0)
	tc->class = XMC_RO;
      else if (strcmp (s, "RW]") == 0)
	tc->class = XMC_RW;
      break;
    case 'S':
      if (strcmp (s, "SV]") == 0)
	tc->class = XMC_SV;
      break;
    case 'T':
      if (strcmp (s, "TC]") == 0)
	tc->class = XMC_TC;
      else if (strcmp (s, "TI]") == 0)
	tc->class = XMC_TI;
      else if (strcmp (s, "TB]") == 0)
	tc->class = XMC_TB;
      else if (strcmp (s, "TC0]") == 0 || strcmp (s, "T0]") == 0)
	tc->class = XMC_TC0;
      break;
    case 'U':
      if (strcmp (s, "UA]") == 0)
	tc->class = XMC_UA;
      else if (strcmp (s, "UC]") == 0)
	tc->class = XMC_UC;
      break;
    case 'X':
      if (strcmp (s, "XO]") == 0)
	tc->class = XMC_XO;
      break;
    }

  if (tc->class == -1)
    as_bad (_("Unrecognized symbol suffix"));
}

/* Set the class of a label based on where it is defined.  This
   handles symbols without suffixes.  Also, move the symbol so that it
   follows the csect symbol.  */

void
ppc_frob_label (symbolS *sym)
{
  if (ppc_current_csect != (symbolS *) NULL)
    {
      if (symbol_get_tc (sym)->class == -1)
	symbol_get_tc (sym)->class = symbol_get_tc (ppc_current_csect)->class;

      symbol_remove (sym, &symbol_rootP, &symbol_lastP);
      symbol_append (sym, symbol_get_tc (ppc_current_csect)->within,
		     &symbol_rootP, &symbol_lastP);
      symbol_get_tc (ppc_current_csect)->within = sym;
    }

#ifdef OBJ_ELF
  dwarf2_emit_label (sym);
#endif
}

/* This variable is set by ppc_frob_symbol if any absolute symbols are
   seen.  It tells ppc_adjust_symtab whether it needs to look through
   the symbols.  */

static bfd_boolean ppc_saw_abs;

/* Change the name of a symbol just before writing it out.  Set the
   real name if the .rename pseudo-op was used.  Otherwise, remove any
   class suffix.  Return 1 if the symbol should not be included in the
   symbol table.  */

int
ppc_frob_symbol (symbolS *sym)
{
  static symbolS *ppc_last_function;
  static symbolS *set_end;

  /* Discard symbols that should not be included in the output symbol
     table.  */
  if (! symbol_used_in_reloc_p (sym)
      && ((symbol_get_bfdsym (sym)->flags & BSF_SECTION_SYM) != 0
	  || (! (S_IS_EXTERNAL (sym) || S_IS_WEAK (sym))
	      && ! symbol_get_tc (sym)->output
	      && S_GET_STORAGE_CLASS (sym) != C_FILE)))
    return 1;

  /* This one will disappear anyway.  Don't make a csect sym for it.  */
  if (sym == abs_section_sym)
    return 1;

  if (symbol_get_tc (sym)->real_name != (char *) NULL)
    S_SET_NAME (sym, symbol_get_tc (sym)->real_name);
  else
    {
      const char *name;
      const char *s;

      name = S_GET_NAME (sym);
      s = strchr (name, '[');
      if (s != (char *) NULL)
	{
	  unsigned int len;
	  char *snew;

	  len = s - name;
	  snew = xmalloc (len + 1);
	  memcpy (snew, name, len);
	  snew[len] = '\0';

	  S_SET_NAME (sym, snew);
	}
    }

  if (set_end != (symbolS *) NULL)
    {
      SA_SET_SYM_ENDNDX (set_end, sym);
      set_end = NULL;
    }

  if (SF_GET_FUNCTION (sym))
    {
      if (ppc_last_function != (symbolS *) NULL)
	as_bad (_("two .function pseudo-ops with no intervening .ef"));
      ppc_last_function = sym;
      if (symbol_get_tc (sym)->size != (symbolS *) NULL)
	{
	  resolve_symbol_value (symbol_get_tc (sym)->size);
	  SA_SET_SYM_FSIZE (sym,
			    (long) S_GET_VALUE (symbol_get_tc (sym)->size));
	}
    }
  else if (S_GET_STORAGE_CLASS (sym) == C_FCN
	   && strcmp (S_GET_NAME (sym), ".ef") == 0)
    {
      if (ppc_last_function == (symbolS *) NULL)
	as_bad (_(".ef with no preceding .function"));
      else
	{
	  set_end = ppc_last_function;
	  ppc_last_function = NULL;

	  /* We don't have a C_EFCN symbol, but we need to force the
	     COFF backend to believe that it has seen one.  */
	  coff_last_function = NULL;
	}
    }

  if (! (S_IS_EXTERNAL (sym) || S_IS_WEAK (sym))
      && (symbol_get_bfdsym (sym)->flags & BSF_SECTION_SYM) == 0
      && S_GET_STORAGE_CLASS (sym) != C_FILE
      && S_GET_STORAGE_CLASS (sym) != C_FCN
      && S_GET_STORAGE_CLASS (sym) != C_BLOCK
      && S_GET_STORAGE_CLASS (sym) != C_BSTAT
      && S_GET_STORAGE_CLASS (sym) != C_ESTAT
      && S_GET_STORAGE_CLASS (sym) != C_BINCL
      && S_GET_STORAGE_CLASS (sym) != C_EINCL
      && S_GET_SEGMENT (sym) != ppc_coff_debug_section)
    S_SET_STORAGE_CLASS (sym, C_HIDEXT);

  if (S_GET_STORAGE_CLASS (sym) == C_EXT
      || S_GET_STORAGE_CLASS (sym) == C_HIDEXT)
    {
      int i;
      union internal_auxent *a;

      /* Create a csect aux.  */
      i = S_GET_NUMBER_AUXILIARY (sym);
      S_SET_NUMBER_AUXILIARY (sym, i + 1);
      a = &coffsymbol (symbol_get_bfdsym (sym))->native[i + 1].u.auxent;
      if (symbol_get_tc (sym)->class == XMC_TC0)
	{
	  /* This is the TOC table.  */
	  know (strcmp (S_GET_NAME (sym), "TOC") == 0);
	  a->x_csect.x_scnlen.l = 0;
	  a->x_csect.x_smtyp = (2 << 3) | XTY_SD;
	}
      else if (symbol_get_tc (sym)->subseg != 0)
	{
	  /* This is a csect symbol.  x_scnlen is the size of the
	     csect.  */
	  if (symbol_get_tc (sym)->next == (symbolS *) NULL)
	    a->x_csect.x_scnlen.l = (bfd_section_size (stdoutput,
						       S_GET_SEGMENT (sym))
				     - S_GET_VALUE (sym));
	  else
	    {
	      resolve_symbol_value (symbol_get_tc (sym)->next);
	      a->x_csect.x_scnlen.l = (S_GET_VALUE (symbol_get_tc (sym)->next)
				       - S_GET_VALUE (sym));
	    }
	  a->x_csect.x_smtyp = (symbol_get_tc (sym)->align << 3) | XTY_SD;
	}
      else if (S_GET_SEGMENT (sym) == bss_section)
	{
	  /* This is a common symbol.  */
	  a->x_csect.x_scnlen.l = symbol_get_frag (sym)->fr_offset;
	  a->x_csect.x_smtyp = (symbol_get_tc (sym)->align << 3) | XTY_CM;
	  if (S_IS_EXTERNAL (sym))
	    symbol_get_tc (sym)->class = XMC_RW;
	  else
	    symbol_get_tc (sym)->class = XMC_BS;
	}
      else if (S_GET_SEGMENT (sym) == absolute_section)
	{
	  /* This is an absolute symbol.  The csect will be created by
	     ppc_adjust_symtab.  */
	  ppc_saw_abs = TRUE;
	  a->x_csect.x_smtyp = XTY_LD;
	  if (symbol_get_tc (sym)->class == -1)
	    symbol_get_tc (sym)->class = XMC_XO;
	}
      else if (! S_IS_DEFINED (sym))
	{
	  /* This is an external symbol.  */
	  a->x_csect.x_scnlen.l = 0;
	  a->x_csect.x_smtyp = XTY_ER;
	}
      else if (symbol_get_tc (sym)->class == XMC_TC)
	{
	  symbolS *next;

	  /* This is a TOC definition.  x_scnlen is the size of the
	     TOC entry.  */
	  next = symbol_next (sym);
	  while (symbol_get_tc (next)->class == XMC_TC0)
	    next = symbol_next (next);
	  if (next == (symbolS *) NULL
	      || symbol_get_tc (next)->class != XMC_TC)
	    {
	      if (ppc_after_toc_frag == (fragS *) NULL)
		a->x_csect.x_scnlen.l = (bfd_section_size (stdoutput,
							   data_section)
					 - S_GET_VALUE (sym));
	      else
		a->x_csect.x_scnlen.l = (ppc_after_toc_frag->fr_address
					 - S_GET_VALUE (sym));
	    }
	  else
	    {
	      resolve_symbol_value (next);
	      a->x_csect.x_scnlen.l = (S_GET_VALUE (next)
				       - S_GET_VALUE (sym));
	    }
	  a->x_csect.x_smtyp = (2 << 3) | XTY_SD;
	}
      else
	{
	  symbolS *csect;

	  /* This is a normal symbol definition.  x_scnlen is the
	     symbol index of the containing csect.  */
	  if (S_GET_SEGMENT (sym) == text_section)
	    csect = ppc_text_csects;
	  else if (S_GET_SEGMENT (sym) == data_section)
	    csect = ppc_data_csects;
	  else
	    abort ();

	  /* Skip the initial dummy symbol.  */
	  csect = symbol_get_tc (csect)->next;

	  if (csect == (symbolS *) NULL)
	    {
	      as_warn (_("warning: symbol %s has no csect"), S_GET_NAME (sym));
	      a->x_csect.x_scnlen.l = 0;
	    }
	  else
	    {
	      while (symbol_get_tc (csect)->next != (symbolS *) NULL)
		{
		  resolve_symbol_value (symbol_get_tc (csect)->next);
		  if (S_GET_VALUE (symbol_get_tc (csect)->next)
		      > S_GET_VALUE (sym))
		    break;
		  csect = symbol_get_tc (csect)->next;
		}

	      a->x_csect.x_scnlen.p =
		coffsymbol (symbol_get_bfdsym (csect))->native;
	      coffsymbol (symbol_get_bfdsym (sym))->native[i + 1].fix_scnlen =
		1;
	    }
	  a->x_csect.x_smtyp = XTY_LD;
	}

      a->x_csect.x_parmhash = 0;
      a->x_csect.x_snhash = 0;
      if (symbol_get_tc (sym)->class == -1)
	a->x_csect.x_smclas = XMC_PR;
      else
	a->x_csect.x_smclas = symbol_get_tc (sym)->class;
      a->x_csect.x_stab = 0;
      a->x_csect.x_snstab = 0;

      /* Don't let the COFF backend resort these symbols.  */
      symbol_get_bfdsym (sym)->flags |= BSF_NOT_AT_END;
    }
  else if (S_GET_STORAGE_CLASS (sym) == C_BSTAT)
    {
      /* We want the value to be the symbol index of the referenced
	 csect symbol.  BFD will do that for us if we set the right
	 flags.  */
      asymbol *bsym = symbol_get_bfdsym (symbol_get_tc (sym)->within);
      combined_entry_type *c = coffsymbol (bsym)->native;

      S_SET_VALUE (sym, (valueT) (size_t) c);
      coffsymbol (symbol_get_bfdsym (sym))->native->fix_value = 1;
    }
  else if (S_GET_STORAGE_CLASS (sym) == C_STSYM)
    {
      symbolS *block;
      symbolS *csect;

      /* The value is the offset from the enclosing csect.  */
      block = symbol_get_tc (sym)->within;
      csect = symbol_get_tc (block)->within;
      resolve_symbol_value (csect);
      S_SET_VALUE (sym, S_GET_VALUE (sym) - S_GET_VALUE (csect));
    }
  else if (S_GET_STORAGE_CLASS (sym) == C_BINCL
	   || S_GET_STORAGE_CLASS (sym) == C_EINCL)
    {
      /* We want the value to be a file offset into the line numbers.
	 BFD will do that for us if we set the right flags.  We have
	 already set the value correctly.  */
      coffsymbol (symbol_get_bfdsym (sym))->native->fix_line = 1;
    }

  return 0;
}

/* Adjust the symbol table.  This creates csect symbols for all
   absolute symbols.  */

void
ppc_adjust_symtab (void)
{
  symbolS *sym;

  if (! ppc_saw_abs)
    return;

  for (sym = symbol_rootP; sym != NULL; sym = symbol_next (sym))
    {
      symbolS *csect;
      int i;
      union internal_auxent *a;

      if (S_GET_SEGMENT (sym) != absolute_section)
	continue;

      csect = symbol_create (".abs[XO]", absolute_section,
			     S_GET_VALUE (sym), &zero_address_frag);
      symbol_get_bfdsym (csect)->value = S_GET_VALUE (sym);
      S_SET_STORAGE_CLASS (csect, C_HIDEXT);
      i = S_GET_NUMBER_AUXILIARY (csect);
      S_SET_NUMBER_AUXILIARY (csect, i + 1);
      a = &coffsymbol (symbol_get_bfdsym (csect))->native[i + 1].u.auxent;
      a->x_csect.x_scnlen.l = 0;
      a->x_csect.x_smtyp = XTY_SD;
      a->x_csect.x_parmhash = 0;
      a->x_csect.x_snhash = 0;
      a->x_csect.x_smclas = XMC_XO;
      a->x_csect.x_stab = 0;
      a->x_csect.x_snstab = 0;

      symbol_insert (csect, sym, &symbol_rootP, &symbol_lastP);

      i = S_GET_NUMBER_AUXILIARY (sym);
      a = &coffsymbol (symbol_get_bfdsym (sym))->native[i].u.auxent;
      a->x_csect.x_scnlen.p = coffsymbol (symbol_get_bfdsym (csect))->native;
      coffsymbol (symbol_get_bfdsym (sym))->native[i].fix_scnlen = 1;
    }

  ppc_saw_abs = FALSE;
}

/* Set the VMA for a section.  This is called on all the sections in
   turn.  */

void
ppc_frob_section (asection *sec)
{
  static bfd_vma vma = 0;

  vma = md_section_align (sec, vma);
  bfd_set_section_vma (stdoutput, sec, vma);
  vma += bfd_section_size (stdoutput, sec);
}

#endif /* OBJ_XCOFF */

/* Turn a string in input_line_pointer into a floating point constant
   of type TYPE, and store the appropriate bytes in *LITP.  The number
   of LITTLENUMS emitted is stored in *SIZEP.  An error message is
   returned, or NULL on OK.  */

char *
md_atof (int type, char *litp, int *sizep)
{
  int prec;
  LITTLENUM_TYPE words[4];
  char *t;
  int i;

  switch (type)
    {
    case 'f':
      prec = 2;
      break;

    case 'd':
      prec = 4;
      break;

    default:
      *sizep = 0;
      return _("bad call to md_atof");
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;

  *sizep = prec * 2;

  if (target_big_endian)
    {
      for (i = 0; i < prec; i++)
	{
	  md_number_to_chars (litp, (valueT) words[i], 2);
	  litp += 2;
	}
    }
  else
    {
      for (i = prec - 1; i >= 0; i--)
	{
	  md_number_to_chars (litp, (valueT) words[i], 2);
	  litp += 2;
	}
    }

  return NULL;
}

/* Write a value out to the object file, using the appropriate
   endianness.  */

void
md_number_to_chars (char *buf, valueT val, int n)
{
  if (target_big_endian)
    number_to_chars_bigendian (buf, val, n);
  else
    number_to_chars_littleendian (buf, val, n);
}

/* Align a section (I don't know why this is machine dependent).  */

valueT
md_section_align (asection *seg ATTRIBUTE_UNUSED, valueT addr)
{
#ifdef OBJ_ELF
  return addr;
#else
  int align = bfd_get_section_alignment (stdoutput, seg);

  return ((addr + (1 << align) - 1) & (-1 << align));
#endif
}

/* We don't have any form of relaxing.  */

int
md_estimate_size_before_relax (fragS *fragp ATTRIBUTE_UNUSED,
			       asection *seg ATTRIBUTE_UNUSED)
{
  abort ();
  return 0;
}

/* Convert a machine dependent frag.  We never generate these.  */

void
md_convert_frag (bfd *abfd ATTRIBUTE_UNUSED,
		 asection *sec ATTRIBUTE_UNUSED,
		 fragS *fragp ATTRIBUTE_UNUSED)
{
  abort ();
}

/* We have no need to default values of symbols.  */

symbolS *
md_undefined_symbol (char *name ATTRIBUTE_UNUSED)
{
  return 0;
}

/* Functions concerning relocs.  */

/* The location from which a PC relative jump should be calculated,
   given a PC relative reloc.  */

long
md_pcrel_from_section (fixS *fixp, segT sec ATTRIBUTE_UNUSED)
{
  return fixp->fx_frag->fr_address + fixp->fx_where;
}

#ifdef OBJ_XCOFF

/* This is called to see whether a fixup should be adjusted to use a
   section symbol.  We take the opportunity to change a fixup against
   a symbol in the TOC subsegment into a reloc against the
   corresponding .tc symbol.  */

int
ppc_fix_adjustable (fixS *fix)
{
  valueT val = resolve_symbol_value (fix->fx_addsy);
  segT symseg = S_GET_SEGMENT (fix->fx_addsy);
  TC_SYMFIELD_TYPE *tc;

  if (symseg == absolute_section)
    return 0;

  if (ppc_toc_csect != (symbolS *) NULL
      && fix->fx_addsy != ppc_toc_csect
      && symseg == data_section
      && val >= ppc_toc_frag->fr_address
      && (ppc_after_toc_frag == (fragS *) NULL
	  || val < ppc_after_toc_frag->fr_address))
    {
      symbolS *sy;

      for (sy = symbol_next (ppc_toc_csect);
	   sy != (symbolS *) NULL;
	   sy = symbol_next (sy))
	{
	  TC_SYMFIELD_TYPE *sy_tc = symbol_get_tc (sy);

	  if (sy_tc->class == XMC_TC0)
	    continue;
	  if (sy_tc->class != XMC_TC)
	    break;
	  if (val == resolve_symbol_value (sy))
	    {
	      fix->fx_addsy = sy;
	      fix->fx_addnumber = val - ppc_toc_frag->fr_address;
	      return 0;
	    }
	}

      as_bad_where (fix->fx_file, fix->fx_line,
		    _("symbol in .toc does not match any .tc"));
    }

  /* Possibly adjust the reloc to be against the csect.  */
  tc = symbol_get_tc (fix->fx_addsy);
  if (tc->subseg == 0
      && tc->class != XMC_TC0
      && tc->class != XMC_TC
      && symseg != bss_section
      /* Don't adjust if this is a reloc in the toc section.  */
      && (symseg != data_section
	  || ppc_toc_csect == NULL
	  || val < ppc_toc_frag->fr_address
	  || (ppc_after_toc_frag != NULL
	      && val >= ppc_after_toc_frag->fr_address)))
    {
      symbolS *csect;
      symbolS *next_csect;

      if (symseg == text_section)
	csect = ppc_text_csects;
      else if (symseg == data_section)
	csect = ppc_data_csects;
      else
	abort ();

      /* Skip the initial dummy symbol.  */
      csect = symbol_get_tc (csect)->next;

      if (csect != (symbolS *) NULL)
	{
	  while ((next_csect = symbol_get_tc (csect)->next) != (symbolS *) NULL
		 && (symbol_get_frag (next_csect)->fr_address <= val))
	    {
	      /* If the csect address equals the symbol value, then we
		 have to look through the full symbol table to see
		 whether this is the csect we want.  Note that we will
		 only get here if the csect has zero length.  */
	      if (symbol_get_frag (csect)->fr_address == val
		  && S_GET_VALUE (csect) == val)
		{
		  symbolS *scan;

		  for (scan = symbol_next (csect);
		       scan != NULL;
		       scan = symbol_next (scan))
		    {
		      if (symbol_get_tc (scan)->subseg != 0)
			break;
		      if (scan == fix->fx_addsy)
			break;
		    }

		  /* If we found the symbol before the next csect
		     symbol, then this is the csect we want.  */
		  if (scan == fix->fx_addsy)
		    break;
		}

	      csect = next_csect;
	    }

	  fix->fx_offset += val - symbol_get_frag (csect)->fr_address;
	  fix->fx_addsy = csect;
	}
      return 0;
    }

  /* Adjust a reloc against a .lcomm symbol to be against the base
     .lcomm.  */
  if (symseg == bss_section
      && ! S_IS_EXTERNAL (fix->fx_addsy))
    {
      symbolS *sy = symbol_get_frag (fix->fx_addsy)->fr_symbol;

      fix->fx_offset += val - resolve_symbol_value (sy);
      fix->fx_addsy = sy;
    }

  return 0;
}

/* A reloc from one csect to another must be kept.  The assembler
   will, of course, keep relocs between sections, and it will keep
   absolute relocs, but we need to force it to keep PC relative relocs
   between two csects in the same section.  */

int
ppc_force_relocation (fixS *fix)
{
  /* At this point fix->fx_addsy should already have been converted to
     a csect symbol.  If the csect does not include the fragment, then
     we need to force the relocation.  */
  if (fix->fx_pcrel
      && fix->fx_addsy != NULL
      && symbol_get_tc (fix->fx_addsy)->subseg != 0
      && ((symbol_get_frag (fix->fx_addsy)->fr_address
	   > fix->fx_frag->fr_address)
	  || (symbol_get_tc (fix->fx_addsy)->next != NULL
	      && (symbol_get_frag (symbol_get_tc (fix->fx_addsy)->next)->fr_address
		  <= fix->fx_frag->fr_address))))
    return 1;

  return generic_force_reloc (fix);
}

#endif /* OBJ_XCOFF */

#ifdef OBJ_ELF
/* If this function returns non-zero, it guarantees that a relocation
   will be emitted for a fixup.  */

int
ppc_force_relocation (fixS *fix)
{
  /* Branch prediction relocations must force a relocation, as must
     the vtable description relocs.  */
  switch (fix->fx_r_type)
    {
    case BFD_RELOC_PPC_B16_BRTAKEN:
    case BFD_RELOC_PPC_B16_BRNTAKEN:
    case BFD_RELOC_PPC_BA16_BRTAKEN:
    case BFD_RELOC_PPC_BA16_BRNTAKEN:
    case BFD_RELOC_24_PLT_PCREL:
    case BFD_RELOC_PPC64_TOC:
      return 1;
    default:
      break;
    }

  if (fix->fx_r_type >= BFD_RELOC_PPC_TLS
      && fix->fx_r_type <= BFD_RELOC_PPC64_DTPREL16_HIGHESTA)
    return 1;

  return generic_force_reloc (fix);
}

int
ppc_fix_adjustable (fixS *fix)
{
  return (fix->fx_r_type != BFD_RELOC_16_GOTOFF
	  && fix->fx_r_type != BFD_RELOC_LO16_GOTOFF
	  && fix->fx_r_type != BFD_RELOC_HI16_GOTOFF
	  && fix->fx_r_type != BFD_RELOC_HI16_S_GOTOFF
	  && fix->fx_r_type != BFD_RELOC_GPREL16
	  && fix->fx_r_type != BFD_RELOC_VTABLE_INHERIT
	  && fix->fx_r_type != BFD_RELOC_VTABLE_ENTRY
	  && !(fix->fx_r_type >= BFD_RELOC_PPC_TLS
	       && fix->fx_r_type <= BFD_RELOC_PPC64_DTPREL16_HIGHESTA));
}
#endif

/* Implement HANDLE_ALIGN.  This writes the NOP pattern into an
   rs_align_code frag.  */

void
ppc_handle_align (struct frag *fragP)
{
  valueT count = (fragP->fr_next->fr_address
		  - (fragP->fr_address + fragP->fr_fix));

  if (count != 0 && (count & 3) == 0)
    {
      char *dest = fragP->fr_literal + fragP->fr_fix;

      fragP->fr_var = 4;
      md_number_to_chars (dest, 0x60000000, 4);

      if ((ppc_cpu & PPC_OPCODE_POWER6) != 0)
	{
	  /* For power6, we want the last nop to be a group terminating
	     one, "ori 1,1,0".  Do this by inserting an rs_fill frag
	     immediately after this one, with its address set to the last
	     nop location.  This will automatically reduce the number of
	     nops in the current frag by one.  */
	  if (count > 4)
	    {
	      struct frag *group_nop = xmalloc (SIZEOF_STRUCT_FRAG + 4);

	      memcpy (group_nop, fragP, SIZEOF_STRUCT_FRAG);
	      group_nop->fr_address = group_nop->fr_next->fr_address - 4;
	      group_nop->fr_fix = 0;
	      group_nop->fr_offset = 1;
	      group_nop->fr_type = rs_fill;
	      fragP->fr_next = group_nop;
	      dest = group_nop->fr_literal;
	    }

	  md_number_to_chars (dest, 0x60210000, 4);
	}
    }
}

/* Apply a fixup to the object code.  This is called for all the
   fixups we generated by the call to fix_new_exp, above.  In the call
   above we used a reloc code which was the largest legal reloc code
   plus the operand index.  Here we undo that to recover the operand
   index.  At this point all symbol values should be fully resolved,
   and we attempt to completely resolve the reloc.  If we can not do
   that, we determine the correct reloc code and put it back in the
   fixup.  */

void
md_apply_fix (fixS *fixP, valueT *valP, segT seg ATTRIBUTE_UNUSED)
{
  valueT value = * valP;

#ifdef OBJ_ELF
  if (fixP->fx_addsy != NULL)
    {
      /* Hack around bfd_install_relocation brain damage.  */
      if (fixP->fx_pcrel)
	value += fixP->fx_frag->fr_address + fixP->fx_where;
    }
  else
    fixP->fx_done = 1;
#else
  /* FIXME FIXME FIXME: The value we are passed in *valP includes
     the symbol values.  If we are doing this relocation the code in
     write.c is going to call bfd_install_relocation, which is also
     going to use the symbol value.  That means that if the reloc is
     fully resolved we want to use *valP since bfd_install_relocation is
     not being used.
     However, if the reloc is not fully resolved we do not want to use
     *valP, and must use fx_offset instead.  However, if the reloc
     is PC relative, we do want to use *valP since it includes the
     result of md_pcrel_from.  This is confusing.  */
  if (fixP->fx_addsy == (symbolS *) NULL)
    fixP->fx_done = 1;

  else if (fixP->fx_pcrel)
    ;

  else
    value = fixP->fx_offset;
#endif

  if (fixP->fx_subsy != (symbolS *) NULL)
    {
      /* We can't actually support subtracting a symbol.  */
      as_bad_where (fixP->fx_file, fixP->fx_line, _("expression too complex"));
    }

  if ((int) fixP->fx_r_type >= (int) BFD_RELOC_UNUSED)
    {
      int opindex;
      const struct powerpc_operand *operand;
      char *where;
      unsigned long insn;

      opindex = (int) fixP->fx_r_type - (int) BFD_RELOC_UNUSED;

      operand = &powerpc_operands[opindex];

#ifdef OBJ_XCOFF
      /* An instruction like `lwz 9,sym(30)' when `sym' is not a TOC symbol
	 does not generate a reloc.  It uses the offset of `sym' within its
	 csect.  Other usages, such as `.long sym', generate relocs.  This
	 is the documented behaviour of non-TOC symbols.  */
      if ((operand->flags & PPC_OPERAND_PARENS) != 0
	  && (operand->bitm & 0xfff0) == 0xfff0
	  && operand->shift == 0
	  && (operand->insert == NULL || ppc_obj64)
	  && fixP->fx_addsy != NULL
	  && symbol_get_tc (fixP->fx_addsy)->subseg != 0
	  && symbol_get_tc (fixP->fx_addsy)->class != XMC_TC
	  && symbol_get_tc (fixP->fx_addsy)->class != XMC_TC0
	  && S_GET_SEGMENT (fixP->fx_addsy) != bss_section)
	{
	  value = fixP->fx_offset;
	  fixP->fx_done = 1;
	}
#endif

      /* Fetch the instruction, insert the fully resolved operand
	 value, and stuff the instruction back again.  */
      where = fixP->fx_frag->fr_literal + fixP->fx_where;
      if (target_big_endian)
	insn = bfd_getb32 ((unsigned char *) where);
      else
	insn = bfd_getl32 ((unsigned char *) where);
      insn = ppc_insert_operand (insn, operand, (offsetT) value,
				 fixP->fx_file, fixP->fx_line);
      if (target_big_endian)
	bfd_putb32 ((bfd_vma) insn, (unsigned char *) where);
      else
	bfd_putl32 ((bfd_vma) insn, (unsigned char *) where);

      if (fixP->fx_done)
	/* Nothing else to do here.  */
	return;

      assert (fixP->fx_addsy != NULL);

      /* Determine a BFD reloc value based on the operand information.
	 We are only prepared to turn a few of the operands into
	 relocs.  */
      if ((operand->flags & PPC_OPERAND_RELATIVE) != 0
	  && operand->bitm == 0x3fffffc
	  && operand->shift == 0)
	fixP->fx_r_type = BFD_RELOC_PPC_B26;
      else if ((operand->flags & PPC_OPERAND_RELATIVE) != 0
	  && operand->bitm == 0xfffc
	  && operand->shift == 0)
	{
	  fixP->fx_r_type = BFD_RELOC_PPC_B16;
#ifdef OBJ_XCOFF
	  fixP->fx_size = 2;
	  if (target_big_endian)
	    fixP->fx_where += 2;
#endif
	}
      else if ((operand->flags & PPC_OPERAND_ABSOLUTE) != 0
	       && operand->bitm == 0x3fffffc
	       && operand->shift == 0)
	fixP->fx_r_type = BFD_RELOC_PPC_BA26;
      else if ((operand->flags & PPC_OPERAND_ABSOLUTE) != 0
	       && operand->bitm == 0xfffc
	       && operand->shift == 0)
	{
	  fixP->fx_r_type = BFD_RELOC_PPC_BA16;
#ifdef OBJ_XCOFF
	  fixP->fx_size = 2;
	  if (target_big_endian)
	    fixP->fx_where += 2;
#endif
	}
#if defined (OBJ_XCOFF) || defined (OBJ_ELF)
      else if ((operand->flags & PPC_OPERAND_PARENS) != 0
	       && (operand->bitm & 0xfff0) == 0xfff0
	       && operand->shift == 0)
	{
	  if (ppc_is_toc_sym (fixP->fx_addsy))
	    {
	      fixP->fx_r_type = BFD_RELOC_PPC_TOC16;
#ifdef OBJ_ELF
	      if (ppc_obj64
		  && (operand->flags & PPC_OPERAND_DS) != 0)
		fixP->fx_r_type = BFD_RELOC_PPC64_TOC16_DS;
#endif
	    }
	  else
	    {
	      fixP->fx_r_type = BFD_RELOC_16;
#ifdef OBJ_ELF
	      if (ppc_obj64
		  && (operand->flags & PPC_OPERAND_DS) != 0)
		fixP->fx_r_type = BFD_RELOC_PPC64_ADDR16_DS;
#endif
	    }
	  fixP->fx_size = 2;
	  if (target_big_endian)
	    fixP->fx_where += 2;
	}
#endif /* defined (OBJ_XCOFF) || defined (OBJ_ELF) */
      else
	{
	  char *sfile;
	  unsigned int sline;

	  /* Use expr_symbol_where to see if this is an expression
	     symbol.  */
	  if (expr_symbol_where (fixP->fx_addsy, &sfile, &sline))
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("unresolved expression that must be resolved"));
	  else
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("unsupported relocation against %s"),
			  S_GET_NAME (fixP->fx_addsy));
	  fixP->fx_done = 1;
	  return;
	}
    }
  else
    {
#ifdef OBJ_ELF
      ppc_elf_validate_fix (fixP, seg);
#endif
      switch (fixP->fx_r_type)
	{
	case BFD_RELOC_CTOR:
	  if (ppc_obj64)
	    goto ctor64;
	  /* fall through */

	case BFD_RELOC_32:
	  if (fixP->fx_pcrel)
	    fixP->fx_r_type = BFD_RELOC_32_PCREL;
	  /* fall through */

	case BFD_RELOC_RVA:
	case BFD_RELOC_32_PCREL:
	case BFD_RELOC_PPC_EMB_NADDR32:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 4);
	  break;

	case BFD_RELOC_64:
	ctor64:
	  if (fixP->fx_pcrel)
	    fixP->fx_r_type = BFD_RELOC_64_PCREL;
	  /* fall through */

	case BFD_RELOC_64_PCREL:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 8);
	  break;

	case BFD_RELOC_GPREL16:
	case BFD_RELOC_16_GOT_PCREL:
	case BFD_RELOC_16_GOTOFF:
	case BFD_RELOC_LO16_GOTOFF:
	case BFD_RELOC_HI16_GOTOFF:
	case BFD_RELOC_HI16_S_GOTOFF:
	case BFD_RELOC_16_BASEREL:
	case BFD_RELOC_LO16_BASEREL:
	case BFD_RELOC_HI16_BASEREL:
	case BFD_RELOC_HI16_S_BASEREL:
	case BFD_RELOC_PPC_EMB_NADDR16:
	case BFD_RELOC_PPC_EMB_NADDR16_LO:
	case BFD_RELOC_PPC_EMB_NADDR16_HI:
	case BFD_RELOC_PPC_EMB_NADDR16_HA:
	case BFD_RELOC_PPC_EMB_SDAI16:
	case BFD_RELOC_PPC_EMB_SDA2REL:
	case BFD_RELOC_PPC_EMB_SDA2I16:
	case BFD_RELOC_PPC_EMB_RELSEC16:
	case BFD_RELOC_PPC_EMB_RELST_LO:
	case BFD_RELOC_PPC_EMB_RELST_HI:
	case BFD_RELOC_PPC_EMB_RELST_HA:
	case BFD_RELOC_PPC_EMB_RELSDA:
	case BFD_RELOC_PPC_TOC16:
#ifdef OBJ_ELF
	case BFD_RELOC_PPC64_TOC16_LO:
	case BFD_RELOC_PPC64_TOC16_HI:
	case BFD_RELOC_PPC64_TOC16_HA:
#endif
	  if (fixP->fx_pcrel)
	    {
	      if (fixP->fx_addsy != NULL)
		as_bad_where (fixP->fx_file, fixP->fx_line,
			      _("cannot emit PC relative %s relocation against %s"),
			      bfd_get_reloc_code_name (fixP->fx_r_type),
			      S_GET_NAME (fixP->fx_addsy));
	      else
		as_bad_where (fixP->fx_file, fixP->fx_line,
			      _("cannot emit PC relative %s relocation"),
			      bfd_get_reloc_code_name (fixP->fx_r_type));
	    }

	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 2);
	  break;

	case BFD_RELOC_16:
	  if (fixP->fx_pcrel)
	    fixP->fx_r_type = BFD_RELOC_16_PCREL;
	  /* fall through */

	case BFD_RELOC_16_PCREL:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 2);
	  break;

	case BFD_RELOC_LO16:
	  if (fixP->fx_pcrel)
	    fixP->fx_r_type = BFD_RELOC_LO16_PCREL;
	  /* fall through */

	case BFD_RELOC_LO16_PCREL:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 2);
	  break;

	  /* This case happens when you write, for example,
	     lis %r3,(L1-L2)@ha
	     where L1 and L2 are defined later.  */
	case BFD_RELOC_HI16:
	  if (fixP->fx_pcrel)
	    fixP->fx_r_type = BFD_RELOC_HI16_PCREL;
	  /* fall through */

	case BFD_RELOC_HI16_PCREL:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      PPC_HI (value), 2);
	  break;

	case BFD_RELOC_HI16_S:
	  if (fixP->fx_pcrel)
	    fixP->fx_r_type = BFD_RELOC_HI16_S_PCREL;
	  /* fall through */

	case BFD_RELOC_HI16_S_PCREL:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      PPC_HA (value), 2);
	  break;

#ifdef OBJ_ELF
	case BFD_RELOC_PPC64_HIGHER:
	  if (fixP->fx_pcrel)
	    abort ();
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      PPC_HIGHER (value), 2);
	  break;

	case BFD_RELOC_PPC64_HIGHER_S:
	  if (fixP->fx_pcrel)
	    abort ();
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      PPC_HIGHERA (value), 2);
	  break;

	case BFD_RELOC_PPC64_HIGHEST:
	  if (fixP->fx_pcrel)
	    abort ();
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      PPC_HIGHEST (value), 2);
	  break;

	case BFD_RELOC_PPC64_HIGHEST_S:
	  if (fixP->fx_pcrel)
	    abort ();
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      PPC_HIGHESTA (value), 2);
	  break;

	case BFD_RELOC_PPC64_ADDR16_DS:
	case BFD_RELOC_PPC64_ADDR16_LO_DS:
	case BFD_RELOC_PPC64_GOT16_DS:
	case BFD_RELOC_PPC64_GOT16_LO_DS:
	case BFD_RELOC_PPC64_PLT16_LO_DS:
	case BFD_RELOC_PPC64_SECTOFF_DS:
	case BFD_RELOC_PPC64_SECTOFF_LO_DS:
	case BFD_RELOC_PPC64_TOC16_DS:
	case BFD_RELOC_PPC64_TOC16_LO_DS:
	case BFD_RELOC_PPC64_PLTGOT16_DS:
	case BFD_RELOC_PPC64_PLTGOT16_LO_DS:
	  if (fixP->fx_pcrel)
	    abort ();
	  {
	    char *where = fixP->fx_frag->fr_literal + fixP->fx_where;
	    unsigned long val, mask;

	    if (target_big_endian)
	      val = bfd_getb32 (where - 2);
	    else
	      val = bfd_getl32 (where);
	    mask = 0xfffc;
	    /* lq insns reserve the four lsbs.  */
	    if ((ppc_cpu & PPC_OPCODE_POWER4) != 0
		&& (val & (0x3f << 26)) == (56u << 26))
	      mask = 0xfff0;
	    val |= value & mask;
	    if (target_big_endian)
	      bfd_putb16 ((bfd_vma) val, where);
	    else
	      bfd_putl16 ((bfd_vma) val, where);
	  }
	  break;

	case BFD_RELOC_PPC_B16_BRTAKEN:
	case BFD_RELOC_PPC_B16_BRNTAKEN:
	case BFD_RELOC_PPC_BA16_BRTAKEN:
	case BFD_RELOC_PPC_BA16_BRNTAKEN:
	  break;

	case BFD_RELOC_PPC_TLS:
	case BFD_RELOC_PPC_TLSLD:
	case BFD_RELOC_PPC_TLSGD:
	  break;

	case BFD_RELOC_PPC_DTPMOD:
	case BFD_RELOC_PPC_TPREL16:
	case BFD_RELOC_PPC_TPREL16_LO:
	case BFD_RELOC_PPC_TPREL16_HI:
	case BFD_RELOC_PPC_TPREL16_HA:
	case BFD_RELOC_PPC_TPREL:
	case BFD_RELOC_PPC_DTPREL16:
	case BFD_RELOC_PPC_DTPREL16_LO:
	case BFD_RELOC_PPC_DTPREL16_HI:
	case BFD_RELOC_PPC_DTPREL16_HA:
	case BFD_RELOC_PPC_DTPREL:
	case BFD_RELOC_PPC_GOT_TLSGD16:
	case BFD_RELOC_PPC_GOT_TLSGD16_LO:
	case BFD_RELOC_PPC_GOT_TLSGD16_HI:
	case BFD_RELOC_PPC_GOT_TLSGD16_HA:
	case BFD_RELOC_PPC_GOT_TLSLD16:
	case BFD_RELOC_PPC_GOT_TLSLD16_LO:
	case BFD_RELOC_PPC_GOT_TLSLD16_HI:
	case BFD_RELOC_PPC_GOT_TLSLD16_HA:
	case BFD_RELOC_PPC_GOT_TPREL16:
	case BFD_RELOC_PPC_GOT_TPREL16_LO:
	case BFD_RELOC_PPC_GOT_TPREL16_HI:
	case BFD_RELOC_PPC_GOT_TPREL16_HA:
	case BFD_RELOC_PPC_GOT_DTPREL16:
	case BFD_RELOC_PPC_GOT_DTPREL16_LO:
	case BFD_RELOC_PPC_GOT_DTPREL16_HI:
	case BFD_RELOC_PPC_GOT_DTPREL16_HA:
	case BFD_RELOC_PPC64_TPREL16_DS:
	case BFD_RELOC_PPC64_TPREL16_LO_DS:
	case BFD_RELOC_PPC64_TPREL16_HIGHER:
	case BFD_RELOC_PPC64_TPREL16_HIGHERA:
	case BFD_RELOC_PPC64_TPREL16_HIGHEST:
	case BFD_RELOC_PPC64_TPREL16_HIGHESTA:
	case BFD_RELOC_PPC64_DTPREL16_DS:
	case BFD_RELOC_PPC64_DTPREL16_LO_DS:
	case BFD_RELOC_PPC64_DTPREL16_HIGHER:
	case BFD_RELOC_PPC64_DTPREL16_HIGHERA:
	case BFD_RELOC_PPC64_DTPREL16_HIGHEST:
	case BFD_RELOC_PPC64_DTPREL16_HIGHESTA:
	  S_SET_THREAD_LOCAL (fixP->fx_addsy);
	  break;
#endif
	  /* Because SDA21 modifies the register field, the size is set to 4
	     bytes, rather than 2, so offset it here appropriately.  */
	case BFD_RELOC_PPC_EMB_SDA21:
	  if (fixP->fx_pcrel)
	    abort ();

	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where
			      + ((target_big_endian) ? 2 : 0),
			      value, 2);
	  break;

	case BFD_RELOC_8:
	  if (fixP->fx_pcrel)
	    {
	      /* This can occur if there is a bug in the input assembler, eg:
		 ".byte <undefined_symbol> - ."  */
	      if (fixP->fx_addsy)
		as_bad (_("Unable to handle reference to symbol %s"),
			S_GET_NAME (fixP->fx_addsy));
	      else
		as_bad (_("Unable to resolve expression"));
	      fixP->fx_done = 1;
	    }
	  else
	    md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
				value, 1);
	  break;

	case BFD_RELOC_24_PLT_PCREL:
	case BFD_RELOC_PPC_LOCAL24PC:
	  if (!fixP->fx_pcrel && !fixP->fx_done)
	    abort ();

	  if (fixP->fx_done)
	    {
	      char *where;
	      unsigned long insn;

	      /* Fetch the instruction, insert the fully resolved operand
		 value, and stuff the instruction back again.  */
	      where = fixP->fx_frag->fr_literal + fixP->fx_where;
	      if (target_big_endian)
		insn = bfd_getb32 ((unsigned char *) where);
	      else
		insn = bfd_getl32 ((unsigned char *) where);
	      if ((value & 3) != 0)
		as_bad_where (fixP->fx_file, fixP->fx_line,
			      _("must branch to an address a multiple of 4"));
	      if ((offsetT) value < -0x40000000
		  || (offsetT) value >= 0x40000000)
		as_bad_where (fixP->fx_file, fixP->fx_line,
			      _("@local or @plt branch destination is too far away, %ld bytes"),
			      (long) value);
	      insn = insn | (value & 0x03fffffc);
	      if (target_big_endian)
		bfd_putb32 ((bfd_vma) insn, (unsigned char *) where);
	      else
		bfd_putl32 ((bfd_vma) insn, (unsigned char *) where);
	    }
	  break;

	case BFD_RELOC_VTABLE_INHERIT:
	  fixP->fx_done = 0;
	  if (fixP->fx_addsy
	      && !S_IS_DEFINED (fixP->fx_addsy)
	      && !S_IS_WEAK (fixP->fx_addsy))
	    S_SET_WEAK (fixP->fx_addsy);
	  break;

	case BFD_RELOC_VTABLE_ENTRY:
	  fixP->fx_done = 0;
	  break;

#ifdef OBJ_ELF
	  /* Generated by reference to `sym@tocbase'.  The sym is
	     ignored by the linker.  */
	case BFD_RELOC_PPC64_TOC:
	  fixP->fx_done = 0;
	  break;
#endif
	default:
	  fprintf (stderr,
		   _("Gas failure, reloc value %d\n"), fixP->fx_r_type);
	  fflush (stderr);
	  abort ();
	}
    }

#ifdef OBJ_ELF
  fixP->fx_addnumber = value;

  /* PowerPC uses RELA relocs, ie. the reloc addend is stored separately
     from the section contents.  If we are going to be emitting a reloc
     then the section contents are immaterial, so don't warn if they
     happen to overflow.  Leave such warnings to ld.  */
  if (!fixP->fx_done)
    fixP->fx_no_overflow = 1;
#else
  if (fixP->fx_r_type != BFD_RELOC_PPC_TOC16)
    fixP->fx_addnumber = 0;
  else
    {
#ifdef TE_PE
      fixP->fx_addnumber = 0;
#else
      /* We want to use the offset within the data segment of the
	 symbol, not the actual VMA of the symbol.  */
      fixP->fx_addnumber =
	- bfd_get_section_vma (stdoutput, S_GET_SEGMENT (fixP->fx_addsy));
#endif
    }
#endif
}

/* Generate a reloc for a fixup.  */

arelent *
tc_gen_reloc (asection *seg ATTRIBUTE_UNUSED, fixS *fixp)
{
  arelent *reloc;

  reloc = (arelent *) xmalloc (sizeof (arelent));

  reloc->sym_ptr_ptr = (asymbol **) xmalloc (sizeof (asymbol *));
  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);
  reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;
  reloc->howto = bfd_reloc_type_lookup (stdoutput, fixp->fx_r_type);
  if (reloc->howto == (reloc_howto_type *) NULL)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    _("reloc %d not supported by object file format"),
		    (int) fixp->fx_r_type);
      return NULL;
    }
  reloc->addend = fixp->fx_addnumber;

  return reloc;
}

void
ppc_cfi_frame_initial_instructions (void)
{
  cfi_add_CFA_def_cfa (1, 0);
}

int
tc_ppc_regname_to_dw2regnum (char *regname)
{
  unsigned int regnum = -1;
  unsigned int i;
  const char *p;
  char *q;
  static struct { char *name; int dw2regnum; } regnames[] =
    {
      { "sp", 1 }, { "r.sp", 1 }, { "rtoc", 2 }, { "r.toc", 2 },
      { "mq", 64 }, { "lr", 65 }, { "ctr", 66 }, { "ap", 67 },
      { "cr", 70 }, { "xer", 76 }, { "vrsave", 109 }, { "vscr", 110 },
      { "spe_acc", 111 }, { "spefscr", 112 }
    };

  for (i = 0; i < ARRAY_SIZE (regnames); ++i)
    if (strcmp (regnames[i].name, regname) == 0)
      return regnames[i].dw2regnum;

  if (regname[0] == 'r' || regname[0] == 'f' || regname[0] == 'v')
    {
      p = regname + 1 + (regname[1] == '.');
      regnum = strtoul (p, &q, 10);
      if (p == q || *q || regnum >= 32)
	return -1;
      if (regname[0] == 'f')
	regnum += 32;
      else if (regname[0] == 'v')
	regnum += 77;
    }
  else if (regname[0] == 'c' && regname[1] == 'r')
    {
      p = regname + 2 + (regname[2] == '.');
      if (p[0] < '0' || p[0] > '7' || p[1])
	return -1;
      regnum = p[0] - '0' + 68;
    }
  return regnum;
}
