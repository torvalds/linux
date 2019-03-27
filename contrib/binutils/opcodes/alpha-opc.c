/* alpha-opc.c -- Alpha AXP opcode list
   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005
   Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@cygnus.com>,
   patterned after the PPC opcode handling written by Ian Lance Taylor.

   This file is part of GDB, GAS, and the GNU binutils.

   GDB, GAS, and the GNU binutils are free software; you can redistribute
   them and/or modify them under the terms of the GNU General Public
   License as published by the Free Software Foundation; either version
   2, or (at your option) any later version.

   GDB, GAS, and the GNU binutils are distributed in the hope that they
   will be useful, but WITHOUT ANY WARRANTY; without even the implied
   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this file; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include <stdio.h>
#include "sysdep.h"
#include "opcode/alpha.h"
#include "bfd.h"
#include "opintl.h"

/* This file holds the Alpha AXP opcode table.  The opcode table includes
   almost all of the extended instruction mnemonics.  This permits the
   disassembler to use them, and simplifies the assembler logic, at the
   cost of increasing the table size.  The table is strictly constant
   data, so the compiler should be able to put it in the text segment.

   This file also holds the operand table.  All knowledge about inserting
   and extracting operands from instructions is kept in this file.

   The information for the base instruction set was compiled from the
   _Alpha Architecture Handbook_, Digital Order Number EC-QD2KB-TE,
   version 2.

   The information for the post-ev5 architecture extensions BWX, CIX and
   MAX came from version 3 of this same document, which is also available
   on-line at http://ftp.digital.com/pub/Digital/info/semiconductor
   /literature/alphahb2.pdf

   The information for the EV4 PALcode instructions was compiled from
   _DECchip 21064 and DECchip 21064A Alpha AXP Microprocessors Hardware
   Reference Manual_, Digital Order Number EC-Q9ZUA-TE, preliminary
   revision dated June 1994.

   The information for the EV5 PALcode instructions was compiled from
   _Alpha 21164 Microprocessor Hardware Reference Manual_, Digital
   Order Number EC-QAEQB-TE, preliminary revision dated April 1995.  */

/* The RB field when it is the same as the RA field in the same insn.
   This operand is marked fake.  The insertion function just copies
   the RA field into the RB field, and the extraction function just
   checks that the fields are the same. */

static unsigned
insert_rba (unsigned insn,
	    int value ATTRIBUTE_UNUSED,
	    const char **errmsg ATTRIBUTE_UNUSED)
{
  return insn | (((insn >> 21) & 0x1f) << 16);
}

static int
extract_rba (unsigned insn, int *invalid)
{
  if (invalid != (int *) NULL
      && ((insn >> 21) & 0x1f) != ((insn >> 16) & 0x1f))
    *invalid = 1;
  return 0;
}

/* The same for the RC field.  */

static unsigned
insert_rca (unsigned insn,
	    int value ATTRIBUTE_UNUSED,
	    const char **errmsg ATTRIBUTE_UNUSED)
{
  return insn | ((insn >> 21) & 0x1f);
}

static int
extract_rca (unsigned insn, int *invalid)
{
  if (invalid != (int *) NULL
      && ((insn >> 21) & 0x1f) != (insn & 0x1f))
    *invalid = 1;
  return 0;
}

/* Fake arguments in which the registers must be set to ZERO.  */

static unsigned
insert_za (unsigned insn,
	   int value ATTRIBUTE_UNUSED,
	   const char **errmsg ATTRIBUTE_UNUSED)
{
  return insn | (31 << 21);
}

static int
extract_za (unsigned insn, int *invalid)
{
  if (invalid != (int *) NULL && ((insn >> 21) & 0x1f) != 31)
    *invalid = 1;
  return 0;
}

static unsigned
insert_zb (unsigned insn,
	   int value ATTRIBUTE_UNUSED,
	   const char **errmsg ATTRIBUTE_UNUSED)
{
  return insn | (31 << 16);
}

static int
extract_zb (unsigned insn, int *invalid)
{
  if (invalid != (int *) NULL && ((insn >> 16) & 0x1f) != 31)
    *invalid = 1;
  return 0;
}

static unsigned
insert_zc (unsigned insn,
	   int value ATTRIBUTE_UNUSED,
	   const char **errmsg ATTRIBUTE_UNUSED)
{
  return insn | 31;
}

static int
extract_zc (unsigned insn, int *invalid)
{
  if (invalid != (int *) NULL && (insn & 0x1f) != 31)
    *invalid = 1;
  return 0;
}


/* The displacement field of a Branch format insn.  */

static unsigned
insert_bdisp (unsigned insn, int value, const char **errmsg)
{
  if (errmsg != (const char **)NULL && (value & 3))
    *errmsg = _("branch operand unaligned");
  return insn | ((value / 4) & 0x1FFFFF);
}

static int
extract_bdisp (unsigned insn, int *invalid ATTRIBUTE_UNUSED)
{
  return 4 * (((insn & 0x1FFFFF) ^ 0x100000) - 0x100000);
}

/* The hint field of a JMP/JSR insn.  */

static unsigned
insert_jhint (unsigned insn, int value, const char **errmsg)
{
  if (errmsg != (const char **)NULL && (value & 3))
    *errmsg = _("jump hint unaligned");
  return insn | ((value / 4) & 0x3FFF);
}

static int
extract_jhint (unsigned insn, int *invalid ATTRIBUTE_UNUSED)
{
  return 4 * (((insn & 0x3FFF) ^ 0x2000) - 0x2000);
}

/* The hint field of an EV6 HW_JMP/JSR insn.  */

static unsigned
insert_ev6hwjhint (unsigned insn, int value, const char **errmsg)
{
  if (errmsg != (const char **)NULL && (value & 3))
    *errmsg = _("jump hint unaligned");
  return insn | ((value / 4) & 0x1FFF);
}

static int
extract_ev6hwjhint (unsigned insn, int *invalid ATTRIBUTE_UNUSED)
{
  return 4 * (((insn & 0x1FFF) ^ 0x1000) - 0x1000);
}

/* The operands table.   */

const struct alpha_operand alpha_operands[] =
{
  /* The fields are bits, shift, insert, extract, flags */
  /* The zero index is used to indicate end-of-list */
#define UNUSED		0
  { 0, 0, 0, 0, 0, 0 },

  /* The plain integer register fields.  */
#define RA		(UNUSED + 1)
  { 5, 21, 0, AXP_OPERAND_IR, 0, 0 },
#define RB		(RA + 1)
  { 5, 16, 0, AXP_OPERAND_IR, 0, 0 },
#define RC		(RB + 1)
  { 5, 0, 0, AXP_OPERAND_IR, 0, 0 },

  /* The plain fp register fields.  */
#define FA		(RC + 1)
  { 5, 21, 0, AXP_OPERAND_FPR, 0, 0 },
#define FB		(FA + 1)
  { 5, 16, 0, AXP_OPERAND_FPR, 0, 0 },
#define FC		(FB + 1)
  { 5, 0, 0, AXP_OPERAND_FPR, 0, 0 },

  /* The integer registers when they are ZERO.  */
#define ZA		(FC + 1)
  { 5, 21, 0, AXP_OPERAND_FAKE, insert_za, extract_za },
#define ZB		(ZA + 1)
  { 5, 16, 0, AXP_OPERAND_FAKE, insert_zb, extract_zb },
#define ZC		(ZB + 1)
  { 5, 0, 0, AXP_OPERAND_FAKE, insert_zc, extract_zc },

  /* The RB field when it needs parentheses.  */
#define PRB		(ZC + 1)
  { 5, 16, 0, AXP_OPERAND_IR|AXP_OPERAND_PARENS, 0, 0 },

  /* The RB field when it needs parentheses _and_ a preceding comma.  */
#define CPRB		(PRB + 1)
  { 5, 16, 0,
    AXP_OPERAND_IR|AXP_OPERAND_PARENS|AXP_OPERAND_COMMA, 0, 0 },

  /* The RB field when it must be the same as the RA field.  */
#define RBA		(CPRB + 1)
  { 5, 16, 0, AXP_OPERAND_FAKE, insert_rba, extract_rba },

  /* The RC field when it must be the same as the RB field.  */
#define RCA		(RBA + 1)
  { 5, 0, 0, AXP_OPERAND_FAKE, insert_rca, extract_rca },

  /* The RC field when it can *default* to RA.  */
#define DRC1		(RCA + 1)
  { 5, 0, 0,
    AXP_OPERAND_IR|AXP_OPERAND_DEFAULT_FIRST, 0, 0 },

  /* The RC field when it can *default* to RB.  */
#define DRC2		(DRC1 + 1)
  { 5, 0, 0,
    AXP_OPERAND_IR|AXP_OPERAND_DEFAULT_SECOND, 0, 0 },

  /* The FC field when it can *default* to RA.  */
#define DFC1		(DRC2 + 1)
  { 5, 0, 0,
    AXP_OPERAND_FPR|AXP_OPERAND_DEFAULT_FIRST, 0, 0 },

  /* The FC field when it can *default* to RB.  */
#define DFC2		(DFC1 + 1)
  { 5, 0, 0,
    AXP_OPERAND_FPR|AXP_OPERAND_DEFAULT_SECOND, 0, 0 },

  /* The unsigned 8-bit literal of Operate format insns.  */
#define LIT		(DFC2 + 1)
  { 8, 13, -LIT, AXP_OPERAND_UNSIGNED, 0, 0 },

  /* The signed 16-bit displacement of Memory format insns.  From here
     we can't tell what relocation should be used, so don't use a default.  */
#define MDISP		(LIT + 1)
  { 16, 0, -MDISP, AXP_OPERAND_SIGNED, 0, 0 },

  /* The signed "23-bit" aligned displacement of Branch format insns.  */
#define BDISP		(MDISP + 1)
  { 21, 0, BFD_RELOC_23_PCREL_S2, 
    AXP_OPERAND_RELATIVE, insert_bdisp, extract_bdisp },

  /* The 26-bit PALcode function */
#define PALFN		(BDISP + 1)
  { 26, 0, -PALFN, AXP_OPERAND_UNSIGNED, 0, 0 },

  /* The optional signed "16-bit" aligned displacement of the JMP/JSR hint.  */
#define JMPHINT		(PALFN + 1)
  { 14, 0, BFD_RELOC_ALPHA_HINT,
    AXP_OPERAND_RELATIVE|AXP_OPERAND_DEFAULT_ZERO|AXP_OPERAND_NOOVERFLOW,
    insert_jhint, extract_jhint },

  /* The optional hint to RET/JSR_COROUTINE.  */
#define RETHINT		(JMPHINT + 1)
  { 14, 0, -RETHINT,
    AXP_OPERAND_UNSIGNED|AXP_OPERAND_DEFAULT_ZERO, 0, 0 },

  /* The 12-bit displacement for the ev[46] hw_{ld,st} (pal1b/pal1f) insns.  */
#define EV4HWDISP	(RETHINT + 1)
#define EV6HWDISP	(EV4HWDISP)
  { 12, 0, -EV4HWDISP, AXP_OPERAND_SIGNED, 0, 0 },

  /* The 5-bit index for the ev4 hw_m[ft]pr (pal19/pal1d) insns.  */
#define EV4HWINDEX	(EV4HWDISP + 1)
  { 5, 0, -EV4HWINDEX, AXP_OPERAND_UNSIGNED, 0, 0 },

  /* The 8-bit index for the oddly unqualified hw_m[tf]pr insns
     that occur in DEC PALcode.  */
#define EV4EXTHWINDEX	(EV4HWINDEX + 1)
  { 8, 0, -EV4EXTHWINDEX, AXP_OPERAND_UNSIGNED, 0, 0 },

  /* The 10-bit displacement for the ev5 hw_{ld,st} (pal1b/pal1f) insns.  */
#define EV5HWDISP	(EV4EXTHWINDEX + 1)
  { 10, 0, -EV5HWDISP, AXP_OPERAND_SIGNED, 0, 0 },

  /* The 16-bit index for the ev5 hw_m[ft]pr (pal19/pal1d) insns.  */
#define EV5HWINDEX	(EV5HWDISP + 1)
  { 16, 0, -EV5HWINDEX, AXP_OPERAND_UNSIGNED, 0, 0 },

  /* The 16-bit combined index/scoreboard mask for the ev6
     hw_m[ft]pr (pal19/pal1d) insns.  */
#define EV6HWINDEX	(EV5HWINDEX + 1)
  { 16, 0, -EV6HWINDEX, AXP_OPERAND_UNSIGNED, 0, 0 },

  /* The 13-bit branch hint for the ev6 hw_jmp/jsr (pal1e) insn.  */
#define EV6HWJMPHINT	(EV6HWINDEX+ 1)
  { 8, 0, -EV6HWJMPHINT,
    AXP_OPERAND_RELATIVE|AXP_OPERAND_DEFAULT_ZERO|AXP_OPERAND_NOOVERFLOW,
    insert_ev6hwjhint, extract_ev6hwjhint }
};

const unsigned alpha_num_operands = sizeof(alpha_operands)/sizeof(*alpha_operands);


/* Macros used to form opcodes.  */

/* The main opcode.  */
#define OP(x)		(((x) & 0x3F) << 26)
#define OP_MASK		0xFC000000

/* Branch format instructions.  */
#define BRA_(oo)	OP(oo)
#define BRA_MASK	OP_MASK
#define BRA(oo)		BRA_(oo), BRA_MASK

/* Floating point format instructions.  */
#define FP_(oo,fff)	(OP(oo) | (((fff) & 0x7FF) << 5))
#define FP_MASK		(OP_MASK | 0xFFE0)
#define FP(oo,fff)	FP_(oo,fff), FP_MASK

/* Memory format instructions.  */
#define MEM_(oo)	OP(oo)
#define MEM_MASK	OP_MASK
#define MEM(oo)		MEM_(oo), MEM_MASK

/* Memory/Func Code format instructions.  */
#define MFC_(oo,ffff)	(OP(oo) | ((ffff) & 0xFFFF))
#define MFC_MASK	(OP_MASK | 0xFFFF)
#define MFC(oo,ffff)	MFC_(oo,ffff), MFC_MASK

/* Memory/Branch format instructions.  */
#define MBR_(oo,h)	(OP(oo) | (((h) & 3) << 14))
#define MBR_MASK	(OP_MASK | 0xC000)
#define MBR(oo,h)	MBR_(oo,h), MBR_MASK

/* Operate format instructions.  The OPRL variant specifies a
   literal second argument.  */
#define OPR_(oo,ff)	(OP(oo) | (((ff) & 0x7F) << 5))
#define OPRL_(oo,ff)	(OPR_((oo),(ff)) | 0x1000)
#define OPR_MASK	(OP_MASK | 0x1FE0)
#define OPR(oo,ff)	OPR_(oo,ff), OPR_MASK
#define OPRL(oo,ff)	OPRL_(oo,ff), OPR_MASK

/* Generic PALcode format instructions.  */
#define PCD_(oo)	OP(oo)
#define PCD_MASK	OP_MASK
#define PCD(oo)		PCD_(oo), PCD_MASK

/* Specific PALcode instructions.  */
#define SPCD_(oo,ffff)	(OP(oo) | ((ffff) & 0x3FFFFFF))
#define SPCD_MASK	0xFFFFFFFF
#define SPCD(oo,ffff)	SPCD_(oo,ffff), SPCD_MASK

/* Hardware memory (hw_{ld,st}) instructions.  */
#define EV4HWMEM_(oo,f)	(OP(oo) | (((f) & 0xF) << 12))
#define EV4HWMEM_MASK	(OP_MASK | 0xF000)
#define EV4HWMEM(oo,f)	EV4HWMEM_(oo,f), EV4HWMEM_MASK

#define EV5HWMEM_(oo,f)	(OP(oo) | (((f) & 0x3F) << 10))
#define EV5HWMEM_MASK	(OP_MASK | 0xF800)
#define EV5HWMEM(oo,f)	EV5HWMEM_(oo,f), EV5HWMEM_MASK

#define EV6HWMEM_(oo,f)	(OP(oo) | (((f) & 0xF) << 12))
#define EV6HWMEM_MASK	(OP_MASK | 0xF000)
#define EV6HWMEM(oo,f)	EV6HWMEM_(oo,f), EV6HWMEM_MASK

#define EV6HWMBR_(oo,h)	(OP(oo) | (((h) & 7) << 13))
#define EV6HWMBR_MASK	(OP_MASK | 0xE000)
#define EV6HWMBR(oo,h)	EV6HWMBR_(oo,h), EV6HWMBR_MASK

/* Abbreviations for instruction subsets.  */
#define BASE			AXP_OPCODE_BASE
#define EV4			AXP_OPCODE_EV4
#define EV5			AXP_OPCODE_EV5
#define EV6			AXP_OPCODE_EV6
#define BWX			AXP_OPCODE_BWX
#define CIX			AXP_OPCODE_CIX
#define MAX			AXP_OPCODE_MAX

/* Common combinations of arguments.  */
#define ARG_NONE		{ 0 }
#define ARG_BRA			{ RA, BDISP }
#define ARG_FBRA		{ FA, BDISP }
#define ARG_FP			{ FA, FB, DFC1 }
#define ARG_FPZ1		{ ZA, FB, DFC1 }
#define ARG_MEM			{ RA, MDISP, PRB }
#define ARG_FMEM		{ FA, MDISP, PRB }
#define ARG_OPR			{ RA, RB, DRC1 }
#define ARG_OPRL		{ RA, LIT, DRC1 }
#define ARG_OPRZ1		{ ZA, RB, DRC1 }
#define ARG_OPRLZ1		{ ZA, LIT, RC }
#define ARG_PCD			{ PALFN }
#define ARG_EV4HWMEM		{ RA, EV4HWDISP, PRB }
#define ARG_EV4HWMPR		{ RA, RBA, EV4HWINDEX }
#define ARG_EV5HWMEM		{ RA, EV5HWDISP, PRB }
#define ARG_EV6HWMEM		{ RA, EV6HWDISP, PRB }

/* The opcode table.

   The format of the opcode table is:

   NAME OPCODE MASK { OPERANDS }

   NAME		is the name of the instruction.

   OPCODE	is the instruction opcode.

   MASK		is the opcode mask; this is used to tell the disassembler
            	which bits in the actual opcode must match OPCODE.

   OPERANDS	is the list of operands.

   The preceding macros merge the text of the OPCODE and MASK fields.

   The disassembler reads the table in order and prints the first
   instruction which matches, so this table is sorted to put more
   specific instructions before more general instructions.

   Otherwise, it is sorted by major opcode and minor function code.

   There are three classes of not-really-instructions in this table:

   ALIAS	is another name for another instruction.  Some of
		these come from the Architecture Handbook, some
		come from the original gas opcode tables.  In all
		cases, the functionality of the opcode is unchanged.

   PSEUDO	a stylized code form endorsed by Chapter A.4 of the
		Architecture Handbook.

   EXTRA	a stylized code form found in the original gas tables.

   And two annotations:

   EV56 BUT	opcodes that are officially introduced as of the ev56,
   		but with defined results on previous implementations.

   EV56 UNA	opcodes that were introduced as of the ev56 with
   		presumably undefined results on previous implementations
		that were not assigned to a particular extension.  */

const struct alpha_opcode alpha_opcodes[] =
{
  { "halt",		SPCD(0x00,0x0000), BASE, ARG_NONE },
  { "draina",		SPCD(0x00,0x0002), BASE, ARG_NONE },
  { "bpt",		SPCD(0x00,0x0080), BASE, ARG_NONE },
  { "bugchk",		SPCD(0x00,0x0081), BASE, ARG_NONE },
  { "callsys",		SPCD(0x00,0x0083), BASE, ARG_NONE },
  { "chmk", 		SPCD(0x00,0x0083), BASE, ARG_NONE },
  { "imb",		SPCD(0x00,0x0086), BASE, ARG_NONE },
  { "rduniq",		SPCD(0x00,0x009e), BASE, ARG_NONE },
  { "wruniq",		SPCD(0x00,0x009f), BASE, ARG_NONE },
  { "gentrap",		SPCD(0x00,0x00aa), BASE, ARG_NONE },
  { "call_pal",		PCD(0x00), BASE, ARG_PCD },
  { "pal",		PCD(0x00), BASE, ARG_PCD },		/* alias */

  { "lda",		MEM(0x08), BASE, { RA, MDISP, ZB } },	/* pseudo */
  { "lda",		MEM(0x08), BASE, ARG_MEM },
  { "ldah",		MEM(0x09), BASE, { RA, MDISP, ZB } },	/* pseudo */
  { "ldah",		MEM(0x09), BASE, ARG_MEM },
  { "ldbu",		MEM(0x0A), BWX, ARG_MEM },
  { "unop",		MEM_(0x0B) | (30 << 16),
			MEM_MASK, BASE, { ZA } },		/* pseudo */
  { "ldq_u",		MEM(0x0B), BASE, ARG_MEM },
  { "ldwu",		MEM(0x0C), BWX, ARG_MEM },
  { "stw",		MEM(0x0D), BWX, ARG_MEM },
  { "stb",		MEM(0x0E), BWX, ARG_MEM },
  { "stq_u",		MEM(0x0F), BASE, ARG_MEM },

  { "sextl",		OPR(0x10,0x00), BASE, ARG_OPRZ1 },	/* pseudo */
  { "sextl",		OPRL(0x10,0x00), BASE, ARG_OPRLZ1 },	/* pseudo */
  { "addl",		OPR(0x10,0x00), BASE, ARG_OPR },
  { "addl",		OPRL(0x10,0x00), BASE, ARG_OPRL },
  { "s4addl",		OPR(0x10,0x02), BASE, ARG_OPR },
  { "s4addl",		OPRL(0x10,0x02), BASE, ARG_OPRL },
  { "negl",		OPR(0x10,0x09), BASE, ARG_OPRZ1 },	/* pseudo */
  { "negl",		OPRL(0x10,0x09), BASE, ARG_OPRLZ1 },	/* pseudo */
  { "subl",		OPR(0x10,0x09), BASE, ARG_OPR },
  { "subl",		OPRL(0x10,0x09), BASE, ARG_OPRL },
  { "s4subl",		OPR(0x10,0x0B), BASE, ARG_OPR },
  { "s4subl",		OPRL(0x10,0x0B), BASE, ARG_OPRL },
  { "cmpbge",		OPR(0x10,0x0F), BASE, ARG_OPR },
  { "cmpbge",		OPRL(0x10,0x0F), BASE, ARG_OPRL },
  { "s8addl",		OPR(0x10,0x12), BASE, ARG_OPR },
  { "s8addl",		OPRL(0x10,0x12), BASE, ARG_OPRL },
  { "s8subl",		OPR(0x10,0x1B), BASE, ARG_OPR },
  { "s8subl",		OPRL(0x10,0x1B), BASE, ARG_OPRL },
  { "cmpult",		OPR(0x10,0x1D), BASE, ARG_OPR },
  { "cmpult",		OPRL(0x10,0x1D), BASE, ARG_OPRL },
  { "addq",		OPR(0x10,0x20), BASE, ARG_OPR },
  { "addq",		OPRL(0x10,0x20), BASE, ARG_OPRL },
  { "s4addq",		OPR(0x10,0x22), BASE, ARG_OPR },
  { "s4addq",		OPRL(0x10,0x22), BASE, ARG_OPRL },
  { "negq", 		OPR(0x10,0x29), BASE, ARG_OPRZ1 },	/* pseudo */
  { "negq", 		OPRL(0x10,0x29), BASE, ARG_OPRLZ1 },	/* pseudo */
  { "subq",		OPR(0x10,0x29), BASE, ARG_OPR },
  { "subq",		OPRL(0x10,0x29), BASE, ARG_OPRL },
  { "s4subq",		OPR(0x10,0x2B), BASE, ARG_OPR },
  { "s4subq",		OPRL(0x10,0x2B), BASE, ARG_OPRL },
  { "cmpeq",		OPR(0x10,0x2D), BASE, ARG_OPR },
  { "cmpeq",		OPRL(0x10,0x2D), BASE, ARG_OPRL },
  { "s8addq",		OPR(0x10,0x32), BASE, ARG_OPR },
  { "s8addq",		OPRL(0x10,0x32), BASE, ARG_OPRL },
  { "s8subq",		OPR(0x10,0x3B), BASE, ARG_OPR },
  { "s8subq",		OPRL(0x10,0x3B), BASE, ARG_OPRL },
  { "cmpule",		OPR(0x10,0x3D), BASE, ARG_OPR },
  { "cmpule",		OPRL(0x10,0x3D), BASE, ARG_OPRL },
  { "addl/v",		OPR(0x10,0x40), BASE, ARG_OPR },
  { "addl/v",		OPRL(0x10,0x40), BASE, ARG_OPRL },
  { "negl/v",		OPR(0x10,0x49), BASE, ARG_OPRZ1 },	/* pseudo */
  { "negl/v",		OPRL(0x10,0x49), BASE, ARG_OPRLZ1 },	/* pseudo */
  { "subl/v",		OPR(0x10,0x49), BASE, ARG_OPR },
  { "subl/v",		OPRL(0x10,0x49), BASE, ARG_OPRL },
  { "cmplt",		OPR(0x10,0x4D), BASE, ARG_OPR },
  { "cmplt",		OPRL(0x10,0x4D), BASE, ARG_OPRL },
  { "addq/v",		OPR(0x10,0x60), BASE, ARG_OPR },
  { "addq/v",		OPRL(0x10,0x60), BASE, ARG_OPRL },
  { "negq/v",		OPR(0x10,0x69), BASE, ARG_OPRZ1 },	/* pseudo */
  { "negq/v",		OPRL(0x10,0x69), BASE, ARG_OPRLZ1 },	/* pseudo */
  { "subq/v",		OPR(0x10,0x69), BASE, ARG_OPR },
  { "subq/v",		OPRL(0x10,0x69), BASE, ARG_OPRL },
  { "cmple",		OPR(0x10,0x6D), BASE, ARG_OPR },
  { "cmple",		OPRL(0x10,0x6D), BASE, ARG_OPRL },

  { "and",		OPR(0x11,0x00), BASE, ARG_OPR },
  { "and",		OPRL(0x11,0x00), BASE, ARG_OPRL },
  { "andnot",		OPR(0x11,0x08), BASE, ARG_OPR },	/* alias */
  { "andnot",		OPRL(0x11,0x08), BASE, ARG_OPRL },	/* alias */
  { "bic",		OPR(0x11,0x08), BASE, ARG_OPR },
  { "bic",		OPRL(0x11,0x08), BASE, ARG_OPRL },
  { "cmovlbs",		OPR(0x11,0x14), BASE, ARG_OPR },
  { "cmovlbs",		OPRL(0x11,0x14), BASE, ARG_OPRL },
  { "cmovlbc",		OPR(0x11,0x16), BASE, ARG_OPR },
  { "cmovlbc",		OPRL(0x11,0x16), BASE, ARG_OPRL },
  { "nop",		OPR(0x11,0x20), BASE, { ZA, ZB, ZC } }, /* pseudo */
  { "clr",		OPR(0x11,0x20), BASE, { ZA, ZB, RC } }, /* pseudo */
  { "mov",		OPR(0x11,0x20), BASE, { ZA, RB, RC } }, /* pseudo */
  { "mov",		OPR(0x11,0x20), BASE, { RA, RBA, RC } }, /* pseudo */
  { "mov",		OPRL(0x11,0x20), BASE, { ZA, LIT, RC } }, /* pseudo */
  { "or",		OPR(0x11,0x20), BASE, ARG_OPR },	/* alias */
  { "or",		OPRL(0x11,0x20), BASE, ARG_OPRL },	/* alias */
  { "bis",		OPR(0x11,0x20), BASE, ARG_OPR },
  { "bis",		OPRL(0x11,0x20), BASE, ARG_OPRL },
  { "cmoveq",		OPR(0x11,0x24), BASE, ARG_OPR },
  { "cmoveq",		OPRL(0x11,0x24), BASE, ARG_OPRL },
  { "cmovne",		OPR(0x11,0x26), BASE, ARG_OPR },
  { "cmovne",		OPRL(0x11,0x26), BASE, ARG_OPRL },
  { "not",		OPR(0x11,0x28), BASE, ARG_OPRZ1 },	/* pseudo */
  { "not",		OPRL(0x11,0x28), BASE, ARG_OPRLZ1 },	/* pseudo */
  { "ornot",		OPR(0x11,0x28), BASE, ARG_OPR },
  { "ornot",		OPRL(0x11,0x28), BASE, ARG_OPRL },
  { "xor",		OPR(0x11,0x40), BASE, ARG_OPR },
  { "xor",		OPRL(0x11,0x40), BASE, ARG_OPRL },
  { "cmovlt",		OPR(0x11,0x44), BASE, ARG_OPR },
  { "cmovlt",		OPRL(0x11,0x44), BASE, ARG_OPRL },
  { "cmovge",		OPR(0x11,0x46), BASE, ARG_OPR },
  { "cmovge",		OPRL(0x11,0x46), BASE, ARG_OPRL },
  { "eqv",		OPR(0x11,0x48), BASE, ARG_OPR },
  { "eqv",		OPRL(0x11,0x48), BASE, ARG_OPRL },
  { "xornot",		OPR(0x11,0x48), BASE, ARG_OPR },	/* alias */
  { "xornot",		OPRL(0x11,0x48), BASE, ARG_OPRL },	/* alias */
  { "amask",		OPR(0x11,0x61), BASE, ARG_OPRZ1 },	/* ev56 but */
  { "amask",		OPRL(0x11,0x61), BASE, ARG_OPRLZ1 },	/* ev56 but */
  { "cmovle",		OPR(0x11,0x64), BASE, ARG_OPR },
  { "cmovle",		OPRL(0x11,0x64), BASE, ARG_OPRL },
  { "cmovgt",		OPR(0x11,0x66), BASE, ARG_OPR },
  { "cmovgt",		OPRL(0x11,0x66), BASE, ARG_OPRL },
  { "implver",		OPRL_(0x11,0x6C)|(31<<21)|(1<<13),
    			0xFFFFFFE0, BASE, { RC } },		/* ev56 but */

  { "mskbl",		OPR(0x12,0x02), BASE, ARG_OPR },
  { "mskbl",		OPRL(0x12,0x02), BASE, ARG_OPRL },
  { "extbl",		OPR(0x12,0x06), BASE, ARG_OPR },
  { "extbl",		OPRL(0x12,0x06), BASE, ARG_OPRL },
  { "insbl",		OPR(0x12,0x0B), BASE, ARG_OPR },
  { "insbl",		OPRL(0x12,0x0B), BASE, ARG_OPRL },
  { "mskwl",		OPR(0x12,0x12), BASE, ARG_OPR },
  { "mskwl",		OPRL(0x12,0x12), BASE, ARG_OPRL },
  { "extwl",		OPR(0x12,0x16), BASE, ARG_OPR },
  { "extwl",		OPRL(0x12,0x16), BASE, ARG_OPRL },
  { "inswl",		OPR(0x12,0x1B), BASE, ARG_OPR },
  { "inswl",		OPRL(0x12,0x1B), BASE, ARG_OPRL },
  { "mskll",		OPR(0x12,0x22), BASE, ARG_OPR },
  { "mskll",		OPRL(0x12,0x22), BASE, ARG_OPRL },
  { "extll",		OPR(0x12,0x26), BASE, ARG_OPR },
  { "extll",		OPRL(0x12,0x26), BASE, ARG_OPRL },
  { "insll",		OPR(0x12,0x2B), BASE, ARG_OPR },
  { "insll",		OPRL(0x12,0x2B), BASE, ARG_OPRL },
  { "zap",		OPR(0x12,0x30), BASE, ARG_OPR },
  { "zap",		OPRL(0x12,0x30), BASE, ARG_OPRL },
  { "zapnot",		OPR(0x12,0x31), BASE, ARG_OPR },
  { "zapnot",		OPRL(0x12,0x31), BASE, ARG_OPRL },
  { "mskql",		OPR(0x12,0x32), BASE, ARG_OPR },
  { "mskql",		OPRL(0x12,0x32), BASE, ARG_OPRL },
  { "srl",		OPR(0x12,0x34), BASE, ARG_OPR },
  { "srl",		OPRL(0x12,0x34), BASE, ARG_OPRL },
  { "extql",		OPR(0x12,0x36), BASE, ARG_OPR },
  { "extql",		OPRL(0x12,0x36), BASE, ARG_OPRL },
  { "sll",		OPR(0x12,0x39), BASE, ARG_OPR },
  { "sll",		OPRL(0x12,0x39), BASE, ARG_OPRL },
  { "insql",		OPR(0x12,0x3B), BASE, ARG_OPR },
  { "insql",		OPRL(0x12,0x3B), BASE, ARG_OPRL },
  { "sra",		OPR(0x12,0x3C), BASE, ARG_OPR },
  { "sra",		OPRL(0x12,0x3C), BASE, ARG_OPRL },
  { "mskwh",		OPR(0x12,0x52), BASE, ARG_OPR },
  { "mskwh",		OPRL(0x12,0x52), BASE, ARG_OPRL },
  { "inswh",		OPR(0x12,0x57), BASE, ARG_OPR },
  { "inswh",		OPRL(0x12,0x57), BASE, ARG_OPRL },
  { "extwh",		OPR(0x12,0x5A), BASE, ARG_OPR },
  { "extwh",		OPRL(0x12,0x5A), BASE, ARG_OPRL },
  { "msklh",		OPR(0x12,0x62), BASE, ARG_OPR },
  { "msklh",		OPRL(0x12,0x62), BASE, ARG_OPRL },
  { "inslh",		OPR(0x12,0x67), BASE, ARG_OPR },
  { "inslh",		OPRL(0x12,0x67), BASE, ARG_OPRL },
  { "extlh",		OPR(0x12,0x6A), BASE, ARG_OPR },
  { "extlh",		OPRL(0x12,0x6A), BASE, ARG_OPRL },
  { "mskqh",		OPR(0x12,0x72), BASE, ARG_OPR },
  { "mskqh",		OPRL(0x12,0x72), BASE, ARG_OPRL },
  { "insqh",		OPR(0x12,0x77), BASE, ARG_OPR },
  { "insqh",		OPRL(0x12,0x77), BASE, ARG_OPRL },
  { "extqh",		OPR(0x12,0x7A), BASE, ARG_OPR },
  { "extqh",		OPRL(0x12,0x7A), BASE, ARG_OPRL },

  { "mull",		OPR(0x13,0x00), BASE, ARG_OPR },
  { "mull",		OPRL(0x13,0x00), BASE, ARG_OPRL },
  { "mulq",		OPR(0x13,0x20), BASE, ARG_OPR },
  { "mulq",		OPRL(0x13,0x20), BASE, ARG_OPRL },
  { "umulh",		OPR(0x13,0x30), BASE, ARG_OPR },
  { "umulh",		OPRL(0x13,0x30), BASE, ARG_OPRL },
  { "mull/v",		OPR(0x13,0x40), BASE, ARG_OPR },
  { "mull/v",		OPRL(0x13,0x40), BASE, ARG_OPRL },
  { "mulq/v",		OPR(0x13,0x60), BASE, ARG_OPR },
  { "mulq/v",		OPRL(0x13,0x60), BASE, ARG_OPRL },

  { "itofs",		FP(0x14,0x004), CIX, { RA, ZB, FC } },
  { "sqrtf/c",		FP(0x14,0x00A), CIX, ARG_FPZ1 },
  { "sqrts/c",		FP(0x14,0x00B), CIX, ARG_FPZ1 },
  { "itoff",		FP(0x14,0x014), CIX, { RA, ZB, FC } },
  { "itoft",		FP(0x14,0x024), CIX, { RA, ZB, FC } },
  { "sqrtg/c",		FP(0x14,0x02A), CIX, ARG_FPZ1 },
  { "sqrtt/c",		FP(0x14,0x02B), CIX, ARG_FPZ1 },
  { "sqrts/m",		FP(0x14,0x04B), CIX, ARG_FPZ1 },
  { "sqrtt/m",		FP(0x14,0x06B), CIX, ARG_FPZ1 },
  { "sqrtf",		FP(0x14,0x08A), CIX, ARG_FPZ1 },
  { "sqrts",		FP(0x14,0x08B), CIX, ARG_FPZ1 },
  { "sqrtg",		FP(0x14,0x0AA), CIX, ARG_FPZ1 },
  { "sqrtt",		FP(0x14,0x0AB), CIX, ARG_FPZ1 },
  { "sqrts/d",		FP(0x14,0x0CB), CIX, ARG_FPZ1 },
  { "sqrtt/d",		FP(0x14,0x0EB), CIX, ARG_FPZ1 },
  { "sqrtf/uc",		FP(0x14,0x10A), CIX, ARG_FPZ1 },
  { "sqrts/uc",		FP(0x14,0x10B), CIX, ARG_FPZ1 },
  { "sqrtg/uc",		FP(0x14,0x12A), CIX, ARG_FPZ1 },
  { "sqrtt/uc",		FP(0x14,0x12B), CIX, ARG_FPZ1 },
  { "sqrts/um",		FP(0x14,0x14B), CIX, ARG_FPZ1 },
  { "sqrtt/um",		FP(0x14,0x16B), CIX, ARG_FPZ1 },
  { "sqrtf/u",		FP(0x14,0x18A), CIX, ARG_FPZ1 },
  { "sqrts/u",		FP(0x14,0x18B), CIX, ARG_FPZ1 },
  { "sqrtg/u",		FP(0x14,0x1AA), CIX, ARG_FPZ1 },
  { "sqrtt/u",		FP(0x14,0x1AB), CIX, ARG_FPZ1 },
  { "sqrts/ud",		FP(0x14,0x1CB), CIX, ARG_FPZ1 },
  { "sqrtt/ud",		FP(0x14,0x1EB), CIX, ARG_FPZ1 },
  { "sqrtf/sc",		FP(0x14,0x40A), CIX, ARG_FPZ1 },
  { "sqrtg/sc",		FP(0x14,0x42A), CIX, ARG_FPZ1 },
  { "sqrtf/s",		FP(0x14,0x48A), CIX, ARG_FPZ1 },
  { "sqrtg/s",		FP(0x14,0x4AA), CIX, ARG_FPZ1 },
  { "sqrtf/suc",	FP(0x14,0x50A), CIX, ARG_FPZ1 },
  { "sqrts/suc",	FP(0x14,0x50B), CIX, ARG_FPZ1 },
  { "sqrtg/suc",	FP(0x14,0x52A), CIX, ARG_FPZ1 },
  { "sqrtt/suc",	FP(0x14,0x52B), CIX, ARG_FPZ1 },
  { "sqrts/sum",	FP(0x14,0x54B), CIX, ARG_FPZ1 },
  { "sqrtt/sum",	FP(0x14,0x56B), CIX, ARG_FPZ1 },
  { "sqrtf/su",		FP(0x14,0x58A), CIX, ARG_FPZ1 },
  { "sqrts/su",		FP(0x14,0x58B), CIX, ARG_FPZ1 },
  { "sqrtg/su",		FP(0x14,0x5AA), CIX, ARG_FPZ1 },
  { "sqrtt/su",		FP(0x14,0x5AB), CIX, ARG_FPZ1 },
  { "sqrts/sud",	FP(0x14,0x5CB), CIX, ARG_FPZ1 },
  { "sqrtt/sud",	FP(0x14,0x5EB), CIX, ARG_FPZ1 },
  { "sqrts/suic",	FP(0x14,0x70B), CIX, ARG_FPZ1 },
  { "sqrtt/suic",	FP(0x14,0x72B), CIX, ARG_FPZ1 },
  { "sqrts/suim",	FP(0x14,0x74B), CIX, ARG_FPZ1 },
  { "sqrtt/suim",	FP(0x14,0x76B), CIX, ARG_FPZ1 },
  { "sqrts/sui",	FP(0x14,0x78B), CIX, ARG_FPZ1 },
  { "sqrtt/sui",	FP(0x14,0x7AB), CIX, ARG_FPZ1 },
  { "sqrts/suid",	FP(0x14,0x7CB), CIX, ARG_FPZ1 },
  { "sqrtt/suid",	FP(0x14,0x7EB), CIX, ARG_FPZ1 },

  { "addf/c",		FP(0x15,0x000), BASE, ARG_FP },
  { "subf/c",		FP(0x15,0x001), BASE, ARG_FP },
  { "mulf/c",		FP(0x15,0x002), BASE, ARG_FP },
  { "divf/c",		FP(0x15,0x003), BASE, ARG_FP },
  { "cvtdg/c",		FP(0x15,0x01E), BASE, ARG_FPZ1 },
  { "addg/c",		FP(0x15,0x020), BASE, ARG_FP },
  { "subg/c",		FP(0x15,0x021), BASE, ARG_FP },
  { "mulg/c",		FP(0x15,0x022), BASE, ARG_FP },
  { "divg/c",		FP(0x15,0x023), BASE, ARG_FP },
  { "cvtgf/c",		FP(0x15,0x02C), BASE, ARG_FPZ1 },
  { "cvtgd/c",		FP(0x15,0x02D), BASE, ARG_FPZ1 },
  { "cvtgq/c",		FP(0x15,0x02F), BASE, ARG_FPZ1 },
  { "cvtqf/c",		FP(0x15,0x03C), BASE, ARG_FPZ1 },
  { "cvtqg/c",		FP(0x15,0x03E), BASE, ARG_FPZ1 },
  { "addf",		FP(0x15,0x080), BASE, ARG_FP },
  { "negf",		FP(0x15,0x081), BASE, ARG_FPZ1 },	/* pseudo */
  { "subf",		FP(0x15,0x081), BASE, ARG_FP },
  { "mulf",		FP(0x15,0x082), BASE, ARG_FP },
  { "divf",		FP(0x15,0x083), BASE, ARG_FP },
  { "cvtdg",		FP(0x15,0x09E), BASE, ARG_FPZ1 },
  { "addg",		FP(0x15,0x0A0), BASE, ARG_FP },
  { "negg",		FP(0x15,0x0A1), BASE, ARG_FPZ1 },	/* pseudo */
  { "subg",		FP(0x15,0x0A1), BASE, ARG_FP },
  { "mulg",		FP(0x15,0x0A2), BASE, ARG_FP },
  { "divg",		FP(0x15,0x0A3), BASE, ARG_FP },
  { "cmpgeq",		FP(0x15,0x0A5), BASE, ARG_FP },
  { "cmpglt",		FP(0x15,0x0A6), BASE, ARG_FP },
  { "cmpgle",		FP(0x15,0x0A7), BASE, ARG_FP },
  { "cvtgf",		FP(0x15,0x0AC), BASE, ARG_FPZ1 },
  { "cvtgd",		FP(0x15,0x0AD), BASE, ARG_FPZ1 },
  { "cvtgq",		FP(0x15,0x0AF), BASE, ARG_FPZ1 },
  { "cvtqf",		FP(0x15,0x0BC), BASE, ARG_FPZ1 },
  { "cvtqg",		FP(0x15,0x0BE), BASE, ARG_FPZ1 },
  { "addf/uc",		FP(0x15,0x100), BASE, ARG_FP },
  { "subf/uc",		FP(0x15,0x101), BASE, ARG_FP },
  { "mulf/uc",		FP(0x15,0x102), BASE, ARG_FP },
  { "divf/uc",		FP(0x15,0x103), BASE, ARG_FP },
  { "cvtdg/uc",		FP(0x15,0x11E), BASE, ARG_FPZ1 },
  { "addg/uc",		FP(0x15,0x120), BASE, ARG_FP },
  { "subg/uc",		FP(0x15,0x121), BASE, ARG_FP },
  { "mulg/uc",		FP(0x15,0x122), BASE, ARG_FP },
  { "divg/uc",		FP(0x15,0x123), BASE, ARG_FP },
  { "cvtgf/uc",		FP(0x15,0x12C), BASE, ARG_FPZ1 },
  { "cvtgd/uc",		FP(0x15,0x12D), BASE, ARG_FPZ1 },
  { "cvtgq/vc",		FP(0x15,0x12F), BASE, ARG_FPZ1 },
  { "addf/u",		FP(0x15,0x180), BASE, ARG_FP },
  { "subf/u",		FP(0x15,0x181), BASE, ARG_FP },
  { "mulf/u",		FP(0x15,0x182), BASE, ARG_FP },
  { "divf/u",		FP(0x15,0x183), BASE, ARG_FP },
  { "cvtdg/u",		FP(0x15,0x19E), BASE, ARG_FPZ1 },
  { "addg/u",		FP(0x15,0x1A0), BASE, ARG_FP },
  { "subg/u",		FP(0x15,0x1A1), BASE, ARG_FP },
  { "mulg/u",		FP(0x15,0x1A2), BASE, ARG_FP },
  { "divg/u",		FP(0x15,0x1A3), BASE, ARG_FP },
  { "cvtgf/u",		FP(0x15,0x1AC), BASE, ARG_FPZ1 },
  { "cvtgd/u",		FP(0x15,0x1AD), BASE, ARG_FPZ1 },
  { "cvtgq/v",		FP(0x15,0x1AF), BASE, ARG_FPZ1 },
  { "addf/sc",		FP(0x15,0x400), BASE, ARG_FP },
  { "subf/sc",		FP(0x15,0x401), BASE, ARG_FP },
  { "mulf/sc",		FP(0x15,0x402), BASE, ARG_FP },
  { "divf/sc",		FP(0x15,0x403), BASE, ARG_FP },
  { "cvtdg/sc",		FP(0x15,0x41E), BASE, ARG_FPZ1 },
  { "addg/sc",		FP(0x15,0x420), BASE, ARG_FP },
  { "subg/sc",		FP(0x15,0x421), BASE, ARG_FP },
  { "mulg/sc",		FP(0x15,0x422), BASE, ARG_FP },
  { "divg/sc",		FP(0x15,0x423), BASE, ARG_FP },
  { "cvtgf/sc",		FP(0x15,0x42C), BASE, ARG_FPZ1 },
  { "cvtgd/sc",		FP(0x15,0x42D), BASE, ARG_FPZ1 },
  { "cvtgq/sc",		FP(0x15,0x42F), BASE, ARG_FPZ1 },
  { "addf/s",		FP(0x15,0x480), BASE, ARG_FP },
  { "negf/s",		FP(0x15,0x481), BASE, ARG_FPZ1 },	/* pseudo */
  { "subf/s",		FP(0x15,0x481), BASE, ARG_FP },
  { "mulf/s",		FP(0x15,0x482), BASE, ARG_FP },
  { "divf/s",		FP(0x15,0x483), BASE, ARG_FP },
  { "cvtdg/s",		FP(0x15,0x49E), BASE, ARG_FPZ1 },
  { "addg/s",		FP(0x15,0x4A0), BASE, ARG_FP },
  { "negg/s",		FP(0x15,0x4A1), BASE, ARG_FPZ1 },	/* pseudo */
  { "subg/s",		FP(0x15,0x4A1), BASE, ARG_FP },
  { "mulg/s",		FP(0x15,0x4A2), BASE, ARG_FP },
  { "divg/s",		FP(0x15,0x4A3), BASE, ARG_FP },
  { "cmpgeq/s",		FP(0x15,0x4A5), BASE, ARG_FP },
  { "cmpglt/s",		FP(0x15,0x4A6), BASE, ARG_FP },
  { "cmpgle/s",		FP(0x15,0x4A7), BASE, ARG_FP },
  { "cvtgf/s",		FP(0x15,0x4AC), BASE, ARG_FPZ1 },
  { "cvtgd/s",		FP(0x15,0x4AD), BASE, ARG_FPZ1 },
  { "cvtgq/s",		FP(0x15,0x4AF), BASE, ARG_FPZ1 },
  { "addf/suc",		FP(0x15,0x500), BASE, ARG_FP },
  { "subf/suc",		FP(0x15,0x501), BASE, ARG_FP },
  { "mulf/suc",		FP(0x15,0x502), BASE, ARG_FP },
  { "divf/suc",		FP(0x15,0x503), BASE, ARG_FP },
  { "cvtdg/suc",	FP(0x15,0x51E), BASE, ARG_FPZ1 },
  { "addg/suc",		FP(0x15,0x520), BASE, ARG_FP },
  { "subg/suc",		FP(0x15,0x521), BASE, ARG_FP },
  { "mulg/suc",		FP(0x15,0x522), BASE, ARG_FP },
  { "divg/suc",		FP(0x15,0x523), BASE, ARG_FP },
  { "cvtgf/suc",	FP(0x15,0x52C), BASE, ARG_FPZ1 },
  { "cvtgd/suc",	FP(0x15,0x52D), BASE, ARG_FPZ1 },
  { "cvtgq/svc",	FP(0x15,0x52F), BASE, ARG_FPZ1 },
  { "addf/su",		FP(0x15,0x580), BASE, ARG_FP },
  { "subf/su",		FP(0x15,0x581), BASE, ARG_FP },
  { "mulf/su",		FP(0x15,0x582), BASE, ARG_FP },
  { "divf/su",		FP(0x15,0x583), BASE, ARG_FP },
  { "cvtdg/su",		FP(0x15,0x59E), BASE, ARG_FPZ1 },
  { "addg/su",		FP(0x15,0x5A0), BASE, ARG_FP },
  { "subg/su",		FP(0x15,0x5A1), BASE, ARG_FP },
  { "mulg/su",		FP(0x15,0x5A2), BASE, ARG_FP },
  { "divg/su",		FP(0x15,0x5A3), BASE, ARG_FP },
  { "cvtgf/su",		FP(0x15,0x5AC), BASE, ARG_FPZ1 },
  { "cvtgd/su",		FP(0x15,0x5AD), BASE, ARG_FPZ1 },
  { "cvtgq/sv",		FP(0x15,0x5AF), BASE, ARG_FPZ1 },

  { "adds/c",		FP(0x16,0x000), BASE, ARG_FP },
  { "subs/c",		FP(0x16,0x001), BASE, ARG_FP },
  { "muls/c",		FP(0x16,0x002), BASE, ARG_FP },
  { "divs/c",		FP(0x16,0x003), BASE, ARG_FP },
  { "addt/c",		FP(0x16,0x020), BASE, ARG_FP },
  { "subt/c",		FP(0x16,0x021), BASE, ARG_FP },
  { "mult/c",		FP(0x16,0x022), BASE, ARG_FP },
  { "divt/c",		FP(0x16,0x023), BASE, ARG_FP },
  { "cvtts/c",		FP(0x16,0x02C), BASE, ARG_FPZ1 },
  { "cvttq/c",		FP(0x16,0x02F), BASE, ARG_FPZ1 },
  { "cvtqs/c",		FP(0x16,0x03C), BASE, ARG_FPZ1 },
  { "cvtqt/c",		FP(0x16,0x03E), BASE, ARG_FPZ1 },
  { "adds/m",		FP(0x16,0x040), BASE, ARG_FP },
  { "subs/m",		FP(0x16,0x041), BASE, ARG_FP },
  { "muls/m",		FP(0x16,0x042), BASE, ARG_FP },
  { "divs/m",		FP(0x16,0x043), BASE, ARG_FP },
  { "addt/m",		FP(0x16,0x060), BASE, ARG_FP },
  { "subt/m",		FP(0x16,0x061), BASE, ARG_FP },
  { "mult/m",		FP(0x16,0x062), BASE, ARG_FP },
  { "divt/m",		FP(0x16,0x063), BASE, ARG_FP },
  { "cvtts/m",		FP(0x16,0x06C), BASE, ARG_FPZ1 },
  { "cvttq/m",		FP(0x16,0x06F), BASE, ARG_FPZ1 },
  { "cvtqs/m",		FP(0x16,0x07C), BASE, ARG_FPZ1 },
  { "cvtqt/m",		FP(0x16,0x07E), BASE, ARG_FPZ1 },
  { "adds",		FP(0x16,0x080), BASE, ARG_FP },
  { "negs", 		FP(0x16,0x081), BASE, ARG_FPZ1 },	/* pseudo */
  { "subs",		FP(0x16,0x081), BASE, ARG_FP },
  { "muls",		FP(0x16,0x082), BASE, ARG_FP },
  { "divs",		FP(0x16,0x083), BASE, ARG_FP },
  { "addt",		FP(0x16,0x0A0), BASE, ARG_FP },
  { "negt", 		FP(0x16,0x0A1), BASE, ARG_FPZ1 },	/* pseudo */
  { "subt",		FP(0x16,0x0A1), BASE, ARG_FP },
  { "mult",		FP(0x16,0x0A2), BASE, ARG_FP },
  { "divt",		FP(0x16,0x0A3), BASE, ARG_FP },
  { "cmptun",		FP(0x16,0x0A4), BASE, ARG_FP },
  { "cmpteq",		FP(0x16,0x0A5), BASE, ARG_FP },
  { "cmptlt",		FP(0x16,0x0A6), BASE, ARG_FP },
  { "cmptle",		FP(0x16,0x0A7), BASE, ARG_FP },
  { "cvtts",		FP(0x16,0x0AC), BASE, ARG_FPZ1 },
  { "cvttq",		FP(0x16,0x0AF), BASE, ARG_FPZ1 },
  { "cvtqs",		FP(0x16,0x0BC), BASE, ARG_FPZ1 },
  { "cvtqt",		FP(0x16,0x0BE), BASE, ARG_FPZ1 },
  { "adds/d",		FP(0x16,0x0C0), BASE, ARG_FP },
  { "subs/d",		FP(0x16,0x0C1), BASE, ARG_FP },
  { "muls/d",		FP(0x16,0x0C2), BASE, ARG_FP },
  { "divs/d",		FP(0x16,0x0C3), BASE, ARG_FP },
  { "addt/d",		FP(0x16,0x0E0), BASE, ARG_FP },
  { "subt/d",		FP(0x16,0x0E1), BASE, ARG_FP },
  { "mult/d",		FP(0x16,0x0E2), BASE, ARG_FP },
  { "divt/d",		FP(0x16,0x0E3), BASE, ARG_FP },
  { "cvtts/d",		FP(0x16,0x0EC), BASE, ARG_FPZ1 },
  { "cvttq/d",		FP(0x16,0x0EF), BASE, ARG_FPZ1 },
  { "cvtqs/d",		FP(0x16,0x0FC), BASE, ARG_FPZ1 },
  { "cvtqt/d",		FP(0x16,0x0FE), BASE, ARG_FPZ1 },
  { "adds/uc",		FP(0x16,0x100), BASE, ARG_FP },
  { "subs/uc",		FP(0x16,0x101), BASE, ARG_FP },
  { "muls/uc",		FP(0x16,0x102), BASE, ARG_FP },
  { "divs/uc",		FP(0x16,0x103), BASE, ARG_FP },
  { "addt/uc",		FP(0x16,0x120), BASE, ARG_FP },
  { "subt/uc",		FP(0x16,0x121), BASE, ARG_FP },
  { "mult/uc",		FP(0x16,0x122), BASE, ARG_FP },
  { "divt/uc",		FP(0x16,0x123), BASE, ARG_FP },
  { "cvtts/uc",		FP(0x16,0x12C), BASE, ARG_FPZ1 },
  { "cvttq/vc",		FP(0x16,0x12F), BASE, ARG_FPZ1 },
  { "adds/um",		FP(0x16,0x140), BASE, ARG_FP },
  { "subs/um",		FP(0x16,0x141), BASE, ARG_FP },
  { "muls/um",		FP(0x16,0x142), BASE, ARG_FP },
  { "divs/um",		FP(0x16,0x143), BASE, ARG_FP },
  { "addt/um",		FP(0x16,0x160), BASE, ARG_FP },
  { "subt/um",		FP(0x16,0x161), BASE, ARG_FP },
  { "mult/um",		FP(0x16,0x162), BASE, ARG_FP },
  { "divt/um",		FP(0x16,0x163), BASE, ARG_FP },
  { "cvtts/um",		FP(0x16,0x16C), BASE, ARG_FPZ1 },
  { "cvttq/vm",		FP(0x16,0x16F), BASE, ARG_FPZ1 },
  { "adds/u",		FP(0x16,0x180), BASE, ARG_FP },
  { "subs/u",		FP(0x16,0x181), BASE, ARG_FP },
  { "muls/u",		FP(0x16,0x182), BASE, ARG_FP },
  { "divs/u",		FP(0x16,0x183), BASE, ARG_FP },
  { "addt/u",		FP(0x16,0x1A0), BASE, ARG_FP },
  { "subt/u",		FP(0x16,0x1A1), BASE, ARG_FP },
  { "mult/u",		FP(0x16,0x1A2), BASE, ARG_FP },
  { "divt/u",		FP(0x16,0x1A3), BASE, ARG_FP },
  { "cvtts/u",		FP(0x16,0x1AC), BASE, ARG_FPZ1 },
  { "cvttq/v",		FP(0x16,0x1AF), BASE, ARG_FPZ1 },
  { "adds/ud",		FP(0x16,0x1C0), BASE, ARG_FP },
  { "subs/ud",		FP(0x16,0x1C1), BASE, ARG_FP },
  { "muls/ud",		FP(0x16,0x1C2), BASE, ARG_FP },
  { "divs/ud",		FP(0x16,0x1C3), BASE, ARG_FP },
  { "addt/ud",		FP(0x16,0x1E0), BASE, ARG_FP },
  { "subt/ud",		FP(0x16,0x1E1), BASE, ARG_FP },
  { "mult/ud",		FP(0x16,0x1E2), BASE, ARG_FP },
  { "divt/ud",		FP(0x16,0x1E3), BASE, ARG_FP },
  { "cvtts/ud",		FP(0x16,0x1EC), BASE, ARG_FPZ1 },
  { "cvttq/vd",		FP(0x16,0x1EF), BASE, ARG_FPZ1 },
  { "cvtst",		FP(0x16,0x2AC), BASE, ARG_FPZ1 },
  { "adds/suc",		FP(0x16,0x500), BASE, ARG_FP },
  { "subs/suc",		FP(0x16,0x501), BASE, ARG_FP },
  { "muls/suc",		FP(0x16,0x502), BASE, ARG_FP },
  { "divs/suc",		FP(0x16,0x503), BASE, ARG_FP },
  { "addt/suc",		FP(0x16,0x520), BASE, ARG_FP },
  { "subt/suc",		FP(0x16,0x521), BASE, ARG_FP },
  { "mult/suc",		FP(0x16,0x522), BASE, ARG_FP },
  { "divt/suc",		FP(0x16,0x523), BASE, ARG_FP },
  { "cvtts/suc",	FP(0x16,0x52C), BASE, ARG_FPZ1 },
  { "cvttq/svc",	FP(0x16,0x52F), BASE, ARG_FPZ1 },
  { "adds/sum",		FP(0x16,0x540), BASE, ARG_FP },
  { "subs/sum",		FP(0x16,0x541), BASE, ARG_FP },
  { "muls/sum",		FP(0x16,0x542), BASE, ARG_FP },
  { "divs/sum",		FP(0x16,0x543), BASE, ARG_FP },
  { "addt/sum",		FP(0x16,0x560), BASE, ARG_FP },
  { "subt/sum",		FP(0x16,0x561), BASE, ARG_FP },
  { "mult/sum",		FP(0x16,0x562), BASE, ARG_FP },
  { "divt/sum",		FP(0x16,0x563), BASE, ARG_FP },
  { "cvtts/sum",	FP(0x16,0x56C), BASE, ARG_FPZ1 },
  { "cvttq/svm",	FP(0x16,0x56F), BASE, ARG_FPZ1 },
  { "adds/su",		FP(0x16,0x580), BASE, ARG_FP },
  { "negs/su",		FP(0x16,0x581), BASE, ARG_FPZ1 },	/* pseudo */
  { "subs/su",		FP(0x16,0x581), BASE, ARG_FP },
  { "muls/su",		FP(0x16,0x582), BASE, ARG_FP },
  { "divs/su",		FP(0x16,0x583), BASE, ARG_FP },
  { "addt/su",		FP(0x16,0x5A0), BASE, ARG_FP },
  { "negt/su",		FP(0x16,0x5A1), BASE, ARG_FPZ1 },	/* pseudo */
  { "subt/su",		FP(0x16,0x5A1), BASE, ARG_FP },
  { "mult/su",		FP(0x16,0x5A2), BASE, ARG_FP },
  { "divt/su",		FP(0x16,0x5A3), BASE, ARG_FP },
  { "cmptun/su",	FP(0x16,0x5A4), BASE, ARG_FP },
  { "cmpteq/su",	FP(0x16,0x5A5), BASE, ARG_FP },
  { "cmptlt/su",	FP(0x16,0x5A6), BASE, ARG_FP },
  { "cmptle/su",	FP(0x16,0x5A7), BASE, ARG_FP },
  { "cvtts/su",		FP(0x16,0x5AC), BASE, ARG_FPZ1 },
  { "cvttq/sv",		FP(0x16,0x5AF), BASE, ARG_FPZ1 },
  { "adds/sud",		FP(0x16,0x5C0), BASE, ARG_FP },
  { "subs/sud",		FP(0x16,0x5C1), BASE, ARG_FP },
  { "muls/sud",		FP(0x16,0x5C2), BASE, ARG_FP },
  { "divs/sud",		FP(0x16,0x5C3), BASE, ARG_FP },
  { "addt/sud",		FP(0x16,0x5E0), BASE, ARG_FP },
  { "subt/sud",		FP(0x16,0x5E1), BASE, ARG_FP },
  { "mult/sud",		FP(0x16,0x5E2), BASE, ARG_FP },
  { "divt/sud",		FP(0x16,0x5E3), BASE, ARG_FP },
  { "cvtts/sud",	FP(0x16,0x5EC), BASE, ARG_FPZ1 },
  { "cvttq/svd",	FP(0x16,0x5EF), BASE, ARG_FPZ1 },
  { "cvtst/s",		FP(0x16,0x6AC), BASE, ARG_FPZ1 },
  { "adds/suic",	FP(0x16,0x700), BASE, ARG_FP },
  { "subs/suic",	FP(0x16,0x701), BASE, ARG_FP },
  { "muls/suic",	FP(0x16,0x702), BASE, ARG_FP },
  { "divs/suic",	FP(0x16,0x703), BASE, ARG_FP },
  { "addt/suic",	FP(0x16,0x720), BASE, ARG_FP },
  { "subt/suic",	FP(0x16,0x721), BASE, ARG_FP },
  { "mult/suic",	FP(0x16,0x722), BASE, ARG_FP },
  { "divt/suic",	FP(0x16,0x723), BASE, ARG_FP },
  { "cvtts/suic",	FP(0x16,0x72C), BASE, ARG_FPZ1 },
  { "cvttq/svic",	FP(0x16,0x72F), BASE, ARG_FPZ1 },
  { "cvtqs/suic",	FP(0x16,0x73C), BASE, ARG_FPZ1 },
  { "cvtqt/suic",	FP(0x16,0x73E), BASE, ARG_FPZ1 },
  { "adds/suim",	FP(0x16,0x740), BASE, ARG_FP },
  { "subs/suim",	FP(0x16,0x741), BASE, ARG_FP },
  { "muls/suim",	FP(0x16,0x742), BASE, ARG_FP },
  { "divs/suim",	FP(0x16,0x743), BASE, ARG_FP },
  { "addt/suim",	FP(0x16,0x760), BASE, ARG_FP },
  { "subt/suim",	FP(0x16,0x761), BASE, ARG_FP },
  { "mult/suim",	FP(0x16,0x762), BASE, ARG_FP },
  { "divt/suim",	FP(0x16,0x763), BASE, ARG_FP },
  { "cvtts/suim",	FP(0x16,0x76C), BASE, ARG_FPZ1 },
  { "cvttq/svim",	FP(0x16,0x76F), BASE, ARG_FPZ1 },
  { "cvtqs/suim",	FP(0x16,0x77C), BASE, ARG_FPZ1 },
  { "cvtqt/suim",	FP(0x16,0x77E), BASE, ARG_FPZ1 },
  { "adds/sui",		FP(0x16,0x780), BASE, ARG_FP },
  { "negs/sui", 	FP(0x16,0x781), BASE, ARG_FPZ1 },	/* pseudo */
  { "subs/sui",		FP(0x16,0x781), BASE, ARG_FP },
  { "muls/sui",		FP(0x16,0x782), BASE, ARG_FP },
  { "divs/sui",		FP(0x16,0x783), BASE, ARG_FP },
  { "addt/sui",		FP(0x16,0x7A0), BASE, ARG_FP },
  { "negt/sui", 	FP(0x16,0x7A1), BASE, ARG_FPZ1 },	/* pseudo */
  { "subt/sui",		FP(0x16,0x7A1), BASE, ARG_FP },
  { "mult/sui",		FP(0x16,0x7A2), BASE, ARG_FP },
  { "divt/sui",		FP(0x16,0x7A3), BASE, ARG_FP },
  { "cvtts/sui",	FP(0x16,0x7AC), BASE, ARG_FPZ1 },
  { "cvttq/svi",	FP(0x16,0x7AF), BASE, ARG_FPZ1 },
  { "cvtqs/sui",	FP(0x16,0x7BC), BASE, ARG_FPZ1 },
  { "cvtqt/sui",	FP(0x16,0x7BE), BASE, ARG_FPZ1 },
  { "adds/suid",	FP(0x16,0x7C0), BASE, ARG_FP },
  { "subs/suid",	FP(0x16,0x7C1), BASE, ARG_FP },
  { "muls/suid",	FP(0x16,0x7C2), BASE, ARG_FP },
  { "divs/suid",	FP(0x16,0x7C3), BASE, ARG_FP },
  { "addt/suid",	FP(0x16,0x7E0), BASE, ARG_FP },
  { "subt/suid",	FP(0x16,0x7E1), BASE, ARG_FP },
  { "mult/suid",	FP(0x16,0x7E2), BASE, ARG_FP },
  { "divt/suid",	FP(0x16,0x7E3), BASE, ARG_FP },
  { "cvtts/suid",	FP(0x16,0x7EC), BASE, ARG_FPZ1 },
  { "cvttq/svid",	FP(0x16,0x7EF), BASE, ARG_FPZ1 },
  { "cvtqs/suid",	FP(0x16,0x7FC), BASE, ARG_FPZ1 },
  { "cvtqt/suid",	FP(0x16,0x7FE), BASE, ARG_FPZ1 },

  { "cvtlq",		FP(0x17,0x010), BASE, ARG_FPZ1 },
  { "fnop",		FP(0x17,0x020), BASE, { ZA, ZB, ZC } },	/* pseudo */
  { "fclr",		FP(0x17,0x020), BASE, { ZA, ZB, FC } },	/* pseudo */
  { "fabs",		FP(0x17,0x020), BASE, ARG_FPZ1 },	/* pseudo */
  { "fmov",		FP(0x17,0x020), BASE, { FA, RBA, FC } }, /* pseudo */
  { "cpys",		FP(0x17,0x020), BASE, ARG_FP },
  { "fneg",		FP(0x17,0x021), BASE, { FA, RBA, FC } }, /* pseudo */
  { "cpysn",		FP(0x17,0x021), BASE, ARG_FP },
  { "cpyse",		FP(0x17,0x022), BASE, ARG_FP },
  { "mt_fpcr",		FP(0x17,0x024), BASE, { FA, RBA, RCA } },
  { "mf_fpcr",		FP(0x17,0x025), BASE, { FA, RBA, RCA } },
  { "fcmoveq",		FP(0x17,0x02A), BASE, ARG_FP },
  { "fcmovne",		FP(0x17,0x02B), BASE, ARG_FP },
  { "fcmovlt",		FP(0x17,0x02C), BASE, ARG_FP },
  { "fcmovge",		FP(0x17,0x02D), BASE, ARG_FP },
  { "fcmovle",		FP(0x17,0x02E), BASE, ARG_FP },
  { "fcmovgt",		FP(0x17,0x02F), BASE, ARG_FP },
  { "cvtql",		FP(0x17,0x030), BASE, ARG_FPZ1 },
  { "cvtql/v",		FP(0x17,0x130), BASE, ARG_FPZ1 },
  { "cvtql/sv",		FP(0x17,0x530), BASE, ARG_FPZ1 },

  { "trapb",		MFC(0x18,0x0000), BASE, ARG_NONE },
  { "draint",		MFC(0x18,0x0000), BASE, ARG_NONE },	/* alias */
  { "excb",		MFC(0x18,0x0400), BASE, ARG_NONE },
  { "mb",		MFC(0x18,0x4000), BASE, ARG_NONE },
  { "wmb",		MFC(0x18,0x4400), BASE, ARG_NONE },
  { "fetch",		MFC(0x18,0x8000), BASE, { ZA, PRB } },
  { "fetch_m",		MFC(0x18,0xA000), BASE, { ZA, PRB } },
  { "rpcc",		MFC(0x18,0xC000), BASE, { RA, ZB } },
  { "rpcc",		MFC(0x18,0xC000), BASE, { RA, RB } },	/* ev6 una */
  { "rc",		MFC(0x18,0xE000), BASE, { RA } },
  { "ecb",		MFC(0x18,0xE800), BASE, { ZA, PRB } },	/* ev56 una */
  { "rs",		MFC(0x18,0xF000), BASE, { RA } },
  { "wh64",		MFC(0x18,0xF800), BASE, { ZA, PRB } },	/* ev56 una */
  { "wh64en",		MFC(0x18,0xFC00), BASE, { ZA, PRB } },	/* ev7 una */

  { "hw_mfpr",		OPR(0x19,0x00), EV4, { RA, RBA, EV4EXTHWINDEX } },
  { "hw_mfpr",		OP(0x19), OP_MASK, EV5, { RA, RBA, EV5HWINDEX } },
  { "hw_mfpr",		OP(0x19), OP_MASK, EV6, { RA, ZB, EV6HWINDEX } },
  { "hw_mfpr/i",	OPR(0x19,0x01), EV4, ARG_EV4HWMPR },
  { "hw_mfpr/a",	OPR(0x19,0x02), EV4, ARG_EV4HWMPR },
  { "hw_mfpr/ai",	OPR(0x19,0x03), EV4, ARG_EV4HWMPR },
  { "hw_mfpr/p",	OPR(0x19,0x04), EV4, ARG_EV4HWMPR },
  { "hw_mfpr/pi",	OPR(0x19,0x05), EV4, ARG_EV4HWMPR },
  { "hw_mfpr/pa",	OPR(0x19,0x06), EV4, ARG_EV4HWMPR },
  { "hw_mfpr/pai",	OPR(0x19,0x07), EV4, ARG_EV4HWMPR },
  { "pal19",		PCD(0x19), BASE, ARG_PCD },

  { "jmp",		MBR_(0x1A,0), MBR_MASK | 0x3FFF,	/* pseudo */
			BASE, { ZA, CPRB } },
  { "jmp",		MBR(0x1A,0), BASE, { RA, CPRB, JMPHINT } },
  { "jsr",		MBR(0x1A,1), BASE, { RA, CPRB, JMPHINT } },
  { "ret",		MBR_(0x1A,2) | (31 << 21) | (26 << 16) | 1,/* pseudo */
			0xFFFFFFFF, BASE, { 0 } },
  { "ret",		MBR(0x1A,2), BASE, { RA, CPRB, RETHINT } },
  { "jcr",		MBR(0x1A,3), BASE, { RA, CPRB, RETHINT } }, /* alias */
  { "jsr_coroutine",	MBR(0x1A,3), BASE, { RA, CPRB, RETHINT } },

  { "hw_ldl",		EV4HWMEM(0x1B,0x0), EV4, ARG_EV4HWMEM },
  { "hw_ldl",		EV5HWMEM(0x1B,0x00), EV5, ARG_EV5HWMEM },
  { "hw_ldl",		EV6HWMEM(0x1B,0x8), EV6, ARG_EV6HWMEM },
  { "hw_ldl/a",		EV4HWMEM(0x1B,0x4), EV4, ARG_EV4HWMEM },
  { "hw_ldl/a",		EV5HWMEM(0x1B,0x10), EV5, ARG_EV5HWMEM },
  { "hw_ldl/a",		EV6HWMEM(0x1B,0xC), EV6, ARG_EV6HWMEM },
  { "hw_ldl/al",	EV5HWMEM(0x1B,0x11), EV5, ARG_EV5HWMEM },
  { "hw_ldl/ar",	EV4HWMEM(0x1B,0x6), EV4, ARG_EV4HWMEM },
  { "hw_ldl/av",	EV5HWMEM(0x1B,0x12), EV5, ARG_EV5HWMEM },
  { "hw_ldl/avl",	EV5HWMEM(0x1B,0x13), EV5, ARG_EV5HWMEM },
  { "hw_ldl/aw",	EV5HWMEM(0x1B,0x18), EV5, ARG_EV5HWMEM },
  { "hw_ldl/awl",	EV5HWMEM(0x1B,0x19), EV5, ARG_EV5HWMEM },
  { "hw_ldl/awv",	EV5HWMEM(0x1B,0x1a), EV5, ARG_EV5HWMEM },
  { "hw_ldl/awvl",	EV5HWMEM(0x1B,0x1b), EV5, ARG_EV5HWMEM },
  { "hw_ldl/l",		EV5HWMEM(0x1B,0x01), EV5, ARG_EV5HWMEM },
  { "hw_ldl/p",		EV4HWMEM(0x1B,0x8), EV4, ARG_EV4HWMEM },
  { "hw_ldl/p",		EV5HWMEM(0x1B,0x20), EV5, ARG_EV5HWMEM },
  { "hw_ldl/p",		EV6HWMEM(0x1B,0x0), EV6, ARG_EV6HWMEM },
  { "hw_ldl/pa",	EV4HWMEM(0x1B,0xC), EV4, ARG_EV4HWMEM },
  { "hw_ldl/pa",	EV5HWMEM(0x1B,0x30), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pal",	EV5HWMEM(0x1B,0x31), EV5, ARG_EV5HWMEM },
  { "hw_ldl/par",	EV4HWMEM(0x1B,0xE), EV4, ARG_EV4HWMEM },
  { "hw_ldl/pav",	EV5HWMEM(0x1B,0x32), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pavl",	EV5HWMEM(0x1B,0x33), EV5, ARG_EV5HWMEM },
  { "hw_ldl/paw",	EV5HWMEM(0x1B,0x38), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pawl",	EV5HWMEM(0x1B,0x39), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pawv",	EV5HWMEM(0x1B,0x3a), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pawvl",	EV5HWMEM(0x1B,0x3b), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pl",	EV5HWMEM(0x1B,0x21), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pr",	EV4HWMEM(0x1B,0xA), EV4, ARG_EV4HWMEM },
  { "hw_ldl/pv",	EV5HWMEM(0x1B,0x22), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pvl",	EV5HWMEM(0x1B,0x23), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pw",	EV5HWMEM(0x1B,0x28), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pwl",	EV5HWMEM(0x1B,0x29), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pwv",	EV5HWMEM(0x1B,0x2a), EV5, ARG_EV5HWMEM },
  { "hw_ldl/pwvl",	EV5HWMEM(0x1B,0x2b), EV5, ARG_EV5HWMEM },
  { "hw_ldl/r",		EV4HWMEM(0x1B,0x2), EV4, ARG_EV4HWMEM },
  { "hw_ldl/v",		EV5HWMEM(0x1B,0x02), EV5, ARG_EV5HWMEM },
  { "hw_ldl/v",		EV6HWMEM(0x1B,0x4), EV6, ARG_EV6HWMEM },
  { "hw_ldl/vl",	EV5HWMEM(0x1B,0x03), EV5, ARG_EV5HWMEM },
  { "hw_ldl/w",		EV5HWMEM(0x1B,0x08), EV5, ARG_EV5HWMEM },
  { "hw_ldl/w",		EV6HWMEM(0x1B,0xA), EV6, ARG_EV6HWMEM },
  { "hw_ldl/wa",	EV6HWMEM(0x1B,0xE), EV6, ARG_EV6HWMEM },
  { "hw_ldl/wl",	EV5HWMEM(0x1B,0x09), EV5, ARG_EV5HWMEM },
  { "hw_ldl/wv",	EV5HWMEM(0x1B,0x0a), EV5, ARG_EV5HWMEM },
  { "hw_ldl/wvl",	EV5HWMEM(0x1B,0x0b), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l",		EV5HWMEM(0x1B,0x01), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/a",	EV5HWMEM(0x1B,0x11), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/av",	EV5HWMEM(0x1B,0x13), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/aw",	EV5HWMEM(0x1B,0x19), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/awv",	EV5HWMEM(0x1B,0x1b), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/p",	EV5HWMEM(0x1B,0x21), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/p",	EV6HWMEM(0x1B,0x2), EV6, ARG_EV6HWMEM },
  { "hw_ldl_l/pa",	EV5HWMEM(0x1B,0x31), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/pav",	EV5HWMEM(0x1B,0x33), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/paw",	EV5HWMEM(0x1B,0x39), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/pawv",	EV5HWMEM(0x1B,0x3b), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/pv",	EV5HWMEM(0x1B,0x23), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/pw",	EV5HWMEM(0x1B,0x29), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/pwv",	EV5HWMEM(0x1B,0x2b), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/v",	EV5HWMEM(0x1B,0x03), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/w",	EV5HWMEM(0x1B,0x09), EV5, ARG_EV5HWMEM },
  { "hw_ldl_l/wv",	EV5HWMEM(0x1B,0x0b), EV5, ARG_EV5HWMEM },
  { "hw_ldq",		EV4HWMEM(0x1B,0x1), EV4, ARG_EV4HWMEM },
  { "hw_ldq",		EV5HWMEM(0x1B,0x04), EV5, ARG_EV5HWMEM },
  { "hw_ldq",		EV6HWMEM(0x1B,0x9), EV6, ARG_EV6HWMEM },
  { "hw_ldq/a",		EV4HWMEM(0x1B,0x5), EV4, ARG_EV4HWMEM },
  { "hw_ldq/a",		EV5HWMEM(0x1B,0x14), EV5, ARG_EV5HWMEM },
  { "hw_ldq/a",		EV6HWMEM(0x1B,0xD), EV6, ARG_EV6HWMEM },
  { "hw_ldq/al",	EV5HWMEM(0x1B,0x15), EV5, ARG_EV5HWMEM },
  { "hw_ldq/ar",	EV4HWMEM(0x1B,0x7), EV4, ARG_EV4HWMEM },
  { "hw_ldq/av",	EV5HWMEM(0x1B,0x16), EV5, ARG_EV5HWMEM },
  { "hw_ldq/avl",	EV5HWMEM(0x1B,0x17), EV5, ARG_EV5HWMEM },
  { "hw_ldq/aw",	EV5HWMEM(0x1B,0x1c), EV5, ARG_EV5HWMEM },
  { "hw_ldq/awl",	EV5HWMEM(0x1B,0x1d), EV5, ARG_EV5HWMEM },
  { "hw_ldq/awv",	EV5HWMEM(0x1B,0x1e), EV5, ARG_EV5HWMEM },
  { "hw_ldq/awvl",	EV5HWMEM(0x1B,0x1f), EV5, ARG_EV5HWMEM },
  { "hw_ldq/l",		EV5HWMEM(0x1B,0x05), EV5, ARG_EV5HWMEM },
  { "hw_ldq/p",		EV4HWMEM(0x1B,0x9), EV4, ARG_EV4HWMEM },
  { "hw_ldq/p",		EV5HWMEM(0x1B,0x24), EV5, ARG_EV5HWMEM },
  { "hw_ldq/p",		EV6HWMEM(0x1B,0x1), EV6, ARG_EV6HWMEM },
  { "hw_ldq/pa",	EV4HWMEM(0x1B,0xD), EV4, ARG_EV4HWMEM },
  { "hw_ldq/pa",	EV5HWMEM(0x1B,0x34), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pal",	EV5HWMEM(0x1B,0x35), EV5, ARG_EV5HWMEM },
  { "hw_ldq/par",	EV4HWMEM(0x1B,0xF), EV4, ARG_EV4HWMEM },
  { "hw_ldq/pav",	EV5HWMEM(0x1B,0x36), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pavl",	EV5HWMEM(0x1B,0x37), EV5, ARG_EV5HWMEM },
  { "hw_ldq/paw",	EV5HWMEM(0x1B,0x3c), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pawl",	EV5HWMEM(0x1B,0x3d), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pawv",	EV5HWMEM(0x1B,0x3e), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pawvl",	EV5HWMEM(0x1B,0x3f), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pl",	EV5HWMEM(0x1B,0x25), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pr",	EV4HWMEM(0x1B,0xB), EV4, ARG_EV4HWMEM },
  { "hw_ldq/pv",	EV5HWMEM(0x1B,0x26), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pvl",	EV5HWMEM(0x1B,0x27), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pw",	EV5HWMEM(0x1B,0x2c), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pwl",	EV5HWMEM(0x1B,0x2d), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pwv",	EV5HWMEM(0x1B,0x2e), EV5, ARG_EV5HWMEM },
  { "hw_ldq/pwvl",	EV5HWMEM(0x1B,0x2f), EV5, ARG_EV5HWMEM },
  { "hw_ldq/r",		EV4HWMEM(0x1B,0x3), EV4, ARG_EV4HWMEM },
  { "hw_ldq/v",		EV5HWMEM(0x1B,0x06), EV5, ARG_EV5HWMEM },
  { "hw_ldq/v",		EV6HWMEM(0x1B,0x5), EV6, ARG_EV6HWMEM },
  { "hw_ldq/vl",	EV5HWMEM(0x1B,0x07), EV5, ARG_EV5HWMEM },
  { "hw_ldq/w",		EV5HWMEM(0x1B,0x0c), EV5, ARG_EV5HWMEM },
  { "hw_ldq/w",		EV6HWMEM(0x1B,0xB), EV6, ARG_EV6HWMEM },
  { "hw_ldq/wa",	EV6HWMEM(0x1B,0xF), EV6, ARG_EV6HWMEM },
  { "hw_ldq/wl",	EV5HWMEM(0x1B,0x0d), EV5, ARG_EV5HWMEM },
  { "hw_ldq/wv",	EV5HWMEM(0x1B,0x0e), EV5, ARG_EV5HWMEM },
  { "hw_ldq/wvl",	EV5HWMEM(0x1B,0x0f), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l",		EV5HWMEM(0x1B,0x05), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/a",	EV5HWMEM(0x1B,0x15), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/av",	EV5HWMEM(0x1B,0x17), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/aw",	EV5HWMEM(0x1B,0x1d), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/awv",	EV5HWMEM(0x1B,0x1f), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/p",	EV5HWMEM(0x1B,0x25), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/p",	EV6HWMEM(0x1B,0x3), EV6, ARG_EV6HWMEM },
  { "hw_ldq_l/pa",	EV5HWMEM(0x1B,0x35), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/pav",	EV5HWMEM(0x1B,0x37), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/paw",	EV5HWMEM(0x1B,0x3d), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/pawv",	EV5HWMEM(0x1B,0x3f), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/pv",	EV5HWMEM(0x1B,0x27), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/pw",	EV5HWMEM(0x1B,0x2d), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/pwv",	EV5HWMEM(0x1B,0x2f), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/v",	EV5HWMEM(0x1B,0x07), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/w",	EV5HWMEM(0x1B,0x0d), EV5, ARG_EV5HWMEM },
  { "hw_ldq_l/wv",	EV5HWMEM(0x1B,0x0f), EV5, ARG_EV5HWMEM },
  { "hw_ld",		EV4HWMEM(0x1B,0x0), EV4, ARG_EV4HWMEM },
  { "hw_ld",		EV5HWMEM(0x1B,0x00), EV5, ARG_EV5HWMEM },
  { "hw_ld/a",		EV4HWMEM(0x1B,0x4), EV4, ARG_EV4HWMEM },
  { "hw_ld/a",		EV5HWMEM(0x1B,0x10), EV5, ARG_EV5HWMEM },
  { "hw_ld/al",		EV5HWMEM(0x1B,0x11), EV5, ARG_EV5HWMEM },
  { "hw_ld/aq",		EV4HWMEM(0x1B,0x5), EV4, ARG_EV4HWMEM },
  { "hw_ld/aq",		EV5HWMEM(0x1B,0x14), EV5, ARG_EV5HWMEM },
  { "hw_ld/aql",	EV5HWMEM(0x1B,0x15), EV5, ARG_EV5HWMEM },
  { "hw_ld/aqv",	EV5HWMEM(0x1B,0x16), EV5, ARG_EV5HWMEM },
  { "hw_ld/aqvl",	EV5HWMEM(0x1B,0x17), EV5, ARG_EV5HWMEM },
  { "hw_ld/ar",		EV4HWMEM(0x1B,0x6), EV4, ARG_EV4HWMEM },
  { "hw_ld/arq",	EV4HWMEM(0x1B,0x7), EV4, ARG_EV4HWMEM },
  { "hw_ld/av",		EV5HWMEM(0x1B,0x12), EV5, ARG_EV5HWMEM },
  { "hw_ld/avl",	EV5HWMEM(0x1B,0x13), EV5, ARG_EV5HWMEM },
  { "hw_ld/aw",		EV5HWMEM(0x1B,0x18), EV5, ARG_EV5HWMEM },
  { "hw_ld/awl",	EV5HWMEM(0x1B,0x19), EV5, ARG_EV5HWMEM },
  { "hw_ld/awq",	EV5HWMEM(0x1B,0x1c), EV5, ARG_EV5HWMEM },
  { "hw_ld/awql",	EV5HWMEM(0x1B,0x1d), EV5, ARG_EV5HWMEM },
  { "hw_ld/awqv",	EV5HWMEM(0x1B,0x1e), EV5, ARG_EV5HWMEM },
  { "hw_ld/awqvl",	EV5HWMEM(0x1B,0x1f), EV5, ARG_EV5HWMEM },
  { "hw_ld/awv",	EV5HWMEM(0x1B,0x1a), EV5, ARG_EV5HWMEM },
  { "hw_ld/awvl",	EV5HWMEM(0x1B,0x1b), EV5, ARG_EV5HWMEM },
  { "hw_ld/l",		EV5HWMEM(0x1B,0x01), EV5, ARG_EV5HWMEM },
  { "hw_ld/p",		EV4HWMEM(0x1B,0x8), EV4, ARG_EV4HWMEM },
  { "hw_ld/p",		EV5HWMEM(0x1B,0x20), EV5, ARG_EV5HWMEM },
  { "hw_ld/pa",		EV4HWMEM(0x1B,0xC), EV4, ARG_EV4HWMEM },
  { "hw_ld/pa",		EV5HWMEM(0x1B,0x30), EV5, ARG_EV5HWMEM },
  { "hw_ld/pal",	EV5HWMEM(0x1B,0x31), EV5, ARG_EV5HWMEM },
  { "hw_ld/paq",	EV4HWMEM(0x1B,0xD), EV4, ARG_EV4HWMEM },
  { "hw_ld/paq",	EV5HWMEM(0x1B,0x34), EV5, ARG_EV5HWMEM },
  { "hw_ld/paql",	EV5HWMEM(0x1B,0x35), EV5, ARG_EV5HWMEM },
  { "hw_ld/paqv",	EV5HWMEM(0x1B,0x36), EV5, ARG_EV5HWMEM },
  { "hw_ld/paqvl",	EV5HWMEM(0x1B,0x37), EV5, ARG_EV5HWMEM },
  { "hw_ld/par",	EV4HWMEM(0x1B,0xE), EV4, ARG_EV4HWMEM },
  { "hw_ld/parq",	EV4HWMEM(0x1B,0xF), EV4, ARG_EV4HWMEM },
  { "hw_ld/pav",	EV5HWMEM(0x1B,0x32), EV5, ARG_EV5HWMEM },
  { "hw_ld/pavl",	EV5HWMEM(0x1B,0x33), EV5, ARG_EV5HWMEM },
  { "hw_ld/paw",	EV5HWMEM(0x1B,0x38), EV5, ARG_EV5HWMEM },
  { "hw_ld/pawl",	EV5HWMEM(0x1B,0x39), EV5, ARG_EV5HWMEM },
  { "hw_ld/pawq",	EV5HWMEM(0x1B,0x3c), EV5, ARG_EV5HWMEM },
  { "hw_ld/pawql",	EV5HWMEM(0x1B,0x3d), EV5, ARG_EV5HWMEM },
  { "hw_ld/pawqv",	EV5HWMEM(0x1B,0x3e), EV5, ARG_EV5HWMEM },
  { "hw_ld/pawqvl",	EV5HWMEM(0x1B,0x3f), EV5, ARG_EV5HWMEM },
  { "hw_ld/pawv",	EV5HWMEM(0x1B,0x3a), EV5, ARG_EV5HWMEM },
  { "hw_ld/pawvl",	EV5HWMEM(0x1B,0x3b), EV5, ARG_EV5HWMEM },
  { "hw_ld/pl",		EV5HWMEM(0x1B,0x21), EV5, ARG_EV5HWMEM },
  { "hw_ld/pq",		EV4HWMEM(0x1B,0x9), EV4, ARG_EV4HWMEM },
  { "hw_ld/pq",		EV5HWMEM(0x1B,0x24), EV5, ARG_EV5HWMEM },
  { "hw_ld/pql",	EV5HWMEM(0x1B,0x25), EV5, ARG_EV5HWMEM },
  { "hw_ld/pqv",	EV5HWMEM(0x1B,0x26), EV5, ARG_EV5HWMEM },
  { "hw_ld/pqvl",	EV5HWMEM(0x1B,0x27), EV5, ARG_EV5HWMEM },
  { "hw_ld/pr",		EV4HWMEM(0x1B,0xA), EV4, ARG_EV4HWMEM },
  { "hw_ld/prq",	EV4HWMEM(0x1B,0xB), EV4, ARG_EV4HWMEM },
  { "hw_ld/pv",		EV5HWMEM(0x1B,0x22), EV5, ARG_EV5HWMEM },
  { "hw_ld/pvl",	EV5HWMEM(0x1B,0x23), EV5, ARG_EV5HWMEM },
  { "hw_ld/pw",		EV5HWMEM(0x1B,0x28), EV5, ARG_EV5HWMEM },
  { "hw_ld/pwl",	EV5HWMEM(0x1B,0x29), EV5, ARG_EV5HWMEM },
  { "hw_ld/pwq",	EV5HWMEM(0x1B,0x2c), EV5, ARG_EV5HWMEM },
  { "hw_ld/pwql",	EV5HWMEM(0x1B,0x2d), EV5, ARG_EV5HWMEM },
  { "hw_ld/pwqv",	EV5HWMEM(0x1B,0x2e), EV5, ARG_EV5HWMEM },
  { "hw_ld/pwqvl",	EV5HWMEM(0x1B,0x2f), EV5, ARG_EV5HWMEM },
  { "hw_ld/pwv",	EV5HWMEM(0x1B,0x2a), EV5, ARG_EV5HWMEM },
  { "hw_ld/pwvl",	EV5HWMEM(0x1B,0x2b), EV5, ARG_EV5HWMEM },
  { "hw_ld/q",		EV4HWMEM(0x1B,0x1), EV4, ARG_EV4HWMEM },
  { "hw_ld/q",		EV5HWMEM(0x1B,0x04), EV5, ARG_EV5HWMEM },
  { "hw_ld/ql",		EV5HWMEM(0x1B,0x05), EV5, ARG_EV5HWMEM },
  { "hw_ld/qv",		EV5HWMEM(0x1B,0x06), EV5, ARG_EV5HWMEM },
  { "hw_ld/qvl",	EV5HWMEM(0x1B,0x07), EV5, ARG_EV5HWMEM },
  { "hw_ld/r",		EV4HWMEM(0x1B,0x2), EV4, ARG_EV4HWMEM },
  { "hw_ld/rq",		EV4HWMEM(0x1B,0x3), EV4, ARG_EV4HWMEM },
  { "hw_ld/v",		EV5HWMEM(0x1B,0x02), EV5, ARG_EV5HWMEM },
  { "hw_ld/vl",		EV5HWMEM(0x1B,0x03), EV5, ARG_EV5HWMEM },
  { "hw_ld/w",		EV5HWMEM(0x1B,0x08), EV5, ARG_EV5HWMEM },
  { "hw_ld/wl",		EV5HWMEM(0x1B,0x09), EV5, ARG_EV5HWMEM },
  { "hw_ld/wq",		EV5HWMEM(0x1B,0x0c), EV5, ARG_EV5HWMEM },
  { "hw_ld/wql",	EV5HWMEM(0x1B,0x0d), EV5, ARG_EV5HWMEM },
  { "hw_ld/wqv",	EV5HWMEM(0x1B,0x0e), EV5, ARG_EV5HWMEM },
  { "hw_ld/wqvl",	EV5HWMEM(0x1B,0x0f), EV5, ARG_EV5HWMEM },
  { "hw_ld/wv",		EV5HWMEM(0x1B,0x0a), EV5, ARG_EV5HWMEM },
  { "hw_ld/wvl",	EV5HWMEM(0x1B,0x0b), EV5, ARG_EV5HWMEM },
  { "pal1b",		PCD(0x1B), BASE, ARG_PCD },

  { "sextb",		OPR(0x1C, 0x00), BWX, ARG_OPRZ1 },
  { "sextw",		OPR(0x1C, 0x01), BWX, ARG_OPRZ1 },
  { "ctpop",		OPR(0x1C, 0x30), CIX, ARG_OPRZ1 },
  { "perr",		OPR(0x1C, 0x31), MAX, ARG_OPR },
  { "ctlz",		OPR(0x1C, 0x32), CIX, ARG_OPRZ1 },
  { "cttz",		OPR(0x1C, 0x33), CIX, ARG_OPRZ1 },
  { "unpkbw",		OPR(0x1C, 0x34), MAX, ARG_OPRZ1 },
  { "unpkbl",		OPR(0x1C, 0x35), MAX, ARG_OPRZ1 },
  { "pkwb",		OPR(0x1C, 0x36), MAX, ARG_OPRZ1 },
  { "pklb",		OPR(0x1C, 0x37), MAX, ARG_OPRZ1 },
  { "minsb8", 		OPR(0x1C, 0x38), MAX, ARG_OPR },
  { "minsb8", 		OPRL(0x1C, 0x38), MAX, ARG_OPRL },
  { "minsw4", 		OPR(0x1C, 0x39), MAX, ARG_OPR },
  { "minsw4", 		OPRL(0x1C, 0x39), MAX, ARG_OPRL },
  { "minub8", 		OPR(0x1C, 0x3A), MAX, ARG_OPR },
  { "minub8", 		OPRL(0x1C, 0x3A), MAX, ARG_OPRL },
  { "minuw4", 		OPR(0x1C, 0x3B), MAX, ARG_OPR },
  { "minuw4", 		OPRL(0x1C, 0x3B), MAX, ARG_OPRL },
  { "maxub8",		OPR(0x1C, 0x3C), MAX, ARG_OPR },
  { "maxub8",		OPRL(0x1C, 0x3C), MAX, ARG_OPRL },
  { "maxuw4",		OPR(0x1C, 0x3D), MAX, ARG_OPR },
  { "maxuw4",		OPRL(0x1C, 0x3D), MAX, ARG_OPRL },
  { "maxsb8",		OPR(0x1C, 0x3E), MAX, ARG_OPR },
  { "maxsb8",		OPRL(0x1C, 0x3E), MAX, ARG_OPRL },
  { "maxsw4",		OPR(0x1C, 0x3F), MAX, ARG_OPR },
  { "maxsw4",		OPRL(0x1C, 0x3F), MAX, ARG_OPRL },
  { "ftoit",		FP(0x1C, 0x70), CIX, { FA, ZB, RC } },
  { "ftois",		FP(0x1C, 0x78), CIX, { FA, ZB, RC } },

  { "hw_mtpr",		OPR(0x1D,0x00), EV4, { RA, RBA, EV4EXTHWINDEX } },
  { "hw_mtpr",		OP(0x1D), OP_MASK, EV5, { RA, RBA, EV5HWINDEX } },
  { "hw_mtpr",		OP(0x1D), OP_MASK, EV6, { ZA, RB, EV6HWINDEX } },
  { "hw_mtpr/i", 	OPR(0x1D,0x01), EV4, ARG_EV4HWMPR },
  { "hw_mtpr/a", 	OPR(0x1D,0x02), EV4, ARG_EV4HWMPR },
  { "hw_mtpr/ai",	OPR(0x1D,0x03), EV4, ARG_EV4HWMPR },
  { "hw_mtpr/p", 	OPR(0x1D,0x04), EV4, ARG_EV4HWMPR },
  { "hw_mtpr/pi",	OPR(0x1D,0x05), EV4, ARG_EV4HWMPR },
  { "hw_mtpr/pa",	OPR(0x1D,0x06), EV4, ARG_EV4HWMPR },
  { "hw_mtpr/pai",	OPR(0x1D,0x07), EV4, ARG_EV4HWMPR },
  { "pal1d",		PCD(0x1D), BASE, ARG_PCD },

  { "hw_rei",		SPCD(0x1E,0x3FF8000), EV4|EV5, ARG_NONE },
  { "hw_rei_stall",	SPCD(0x1E,0x3FFC000), EV5, ARG_NONE },
  { "hw_jmp", 		EV6HWMBR(0x1E,0x0), EV6, { ZA, PRB, EV6HWJMPHINT } },
  { "hw_jsr", 		EV6HWMBR(0x1E,0x2), EV6, { ZA, PRB, EV6HWJMPHINT } },
  { "hw_ret", 		EV6HWMBR(0x1E,0x4), EV6, { ZA, PRB } },
  { "hw_jcr", 		EV6HWMBR(0x1E,0x6), EV6, { ZA, PRB } },
  { "hw_coroutine",	EV6HWMBR(0x1E,0x6), EV6, { ZA, PRB } }, /* alias */
  { "hw_jmp/stall",	EV6HWMBR(0x1E,0x1), EV6, { ZA, PRB, EV6HWJMPHINT } },
  { "hw_jsr/stall", 	EV6HWMBR(0x1E,0x3), EV6, { ZA, PRB, EV6HWJMPHINT } },
  { "hw_ret/stall",	EV6HWMBR(0x1E,0x5), EV6, { ZA, PRB } },
  { "hw_jcr/stall", 	EV6HWMBR(0x1E,0x7), EV6, { ZA, PRB } },
  { "hw_coroutine/stall", EV6HWMBR(0x1E,0x7), EV6, { ZA, PRB } }, /* alias */
  { "pal1e",		PCD(0x1E), BASE, ARG_PCD },

  { "hw_stl",		EV4HWMEM(0x1F,0x0), EV4, ARG_EV4HWMEM },
  { "hw_stl",		EV5HWMEM(0x1F,0x00), EV5, ARG_EV5HWMEM },
  { "hw_stl",		EV6HWMEM(0x1F,0x4), EV6, ARG_EV6HWMEM }, /* ??? 8 */
  { "hw_stl/a",		EV4HWMEM(0x1F,0x4), EV4, ARG_EV4HWMEM },
  { "hw_stl/a",		EV5HWMEM(0x1F,0x10), EV5, ARG_EV5HWMEM },
  { "hw_stl/a",		EV6HWMEM(0x1F,0xC), EV6, ARG_EV6HWMEM },
  { "hw_stl/ac",	EV5HWMEM(0x1F,0x11), EV5, ARG_EV5HWMEM },
  { "hw_stl/ar",	EV4HWMEM(0x1F,0x6), EV4, ARG_EV4HWMEM },
  { "hw_stl/av",	EV5HWMEM(0x1F,0x12), EV5, ARG_EV5HWMEM },
  { "hw_stl/avc",	EV5HWMEM(0x1F,0x13), EV5, ARG_EV5HWMEM },
  { "hw_stl/c",		EV5HWMEM(0x1F,0x01), EV5, ARG_EV5HWMEM },
  { "hw_stl/p",		EV4HWMEM(0x1F,0x8), EV4, ARG_EV4HWMEM },
  { "hw_stl/p",		EV5HWMEM(0x1F,0x20), EV5, ARG_EV5HWMEM },
  { "hw_stl/p",		EV6HWMEM(0x1F,0x0), EV6, ARG_EV6HWMEM },
  { "hw_stl/pa",	EV4HWMEM(0x1F,0xC), EV4, ARG_EV4HWMEM },
  { "hw_stl/pa",	EV5HWMEM(0x1F,0x30), EV5, ARG_EV5HWMEM },
  { "hw_stl/pac",	EV5HWMEM(0x1F,0x31), EV5, ARG_EV5HWMEM },
  { "hw_stl/pav",	EV5HWMEM(0x1F,0x32), EV5, ARG_EV5HWMEM },
  { "hw_stl/pavc",	EV5HWMEM(0x1F,0x33), EV5, ARG_EV5HWMEM },
  { "hw_stl/pc",	EV5HWMEM(0x1F,0x21), EV5, ARG_EV5HWMEM },
  { "hw_stl/pr",	EV4HWMEM(0x1F,0xA), EV4, ARG_EV4HWMEM },
  { "hw_stl/pv",	EV5HWMEM(0x1F,0x22), EV5, ARG_EV5HWMEM },
  { "hw_stl/pvc",	EV5HWMEM(0x1F,0x23), EV5, ARG_EV5HWMEM },
  { "hw_stl/r",		EV4HWMEM(0x1F,0x2), EV4, ARG_EV4HWMEM },
  { "hw_stl/v",		EV5HWMEM(0x1F,0x02), EV5, ARG_EV5HWMEM },
  { "hw_stl/vc",	EV5HWMEM(0x1F,0x03), EV5, ARG_EV5HWMEM },
  { "hw_stl_c",		EV5HWMEM(0x1F,0x01), EV5, ARG_EV5HWMEM },
  { "hw_stl_c/a",	EV5HWMEM(0x1F,0x11), EV5, ARG_EV5HWMEM },
  { "hw_stl_c/av",	EV5HWMEM(0x1F,0x13), EV5, ARG_EV5HWMEM },
  { "hw_stl_c/p",	EV5HWMEM(0x1F,0x21), EV5, ARG_EV5HWMEM },
  { "hw_stl_c/p",	EV6HWMEM(0x1F,0x2), EV6, ARG_EV6HWMEM },
  { "hw_stl_c/pa",	EV5HWMEM(0x1F,0x31), EV5, ARG_EV5HWMEM },
  { "hw_stl_c/pav",	EV5HWMEM(0x1F,0x33), EV5, ARG_EV5HWMEM },
  { "hw_stl_c/pv",	EV5HWMEM(0x1F,0x23), EV5, ARG_EV5HWMEM },
  { "hw_stl_c/v",	EV5HWMEM(0x1F,0x03), EV5, ARG_EV5HWMEM },
  { "hw_stq",		EV4HWMEM(0x1F,0x1), EV4, ARG_EV4HWMEM },
  { "hw_stq",		EV5HWMEM(0x1F,0x04), EV5, ARG_EV5HWMEM },
  { "hw_stq",		EV6HWMEM(0x1F,0x5), EV6, ARG_EV6HWMEM }, /* ??? 9 */
  { "hw_stq/a",		EV4HWMEM(0x1F,0x5), EV4, ARG_EV4HWMEM },
  { "hw_stq/a",		EV5HWMEM(0x1F,0x14), EV5, ARG_EV5HWMEM },
  { "hw_stq/a",		EV6HWMEM(0x1F,0xD), EV6, ARG_EV6HWMEM },
  { "hw_stq/ac",	EV5HWMEM(0x1F,0x15), EV5, ARG_EV5HWMEM },
  { "hw_stq/ar",	EV4HWMEM(0x1F,0x7), EV4, ARG_EV4HWMEM },
  { "hw_stq/av",	EV5HWMEM(0x1F,0x16), EV5, ARG_EV5HWMEM },
  { "hw_stq/avc",	EV5HWMEM(0x1F,0x17), EV5, ARG_EV5HWMEM },
  { "hw_stq/c",		EV5HWMEM(0x1F,0x05), EV5, ARG_EV5HWMEM },
  { "hw_stq/p",		EV4HWMEM(0x1F,0x9), EV4, ARG_EV4HWMEM },
  { "hw_stq/p",		EV5HWMEM(0x1F,0x24), EV5, ARG_EV5HWMEM },
  { "hw_stq/p",		EV6HWMEM(0x1F,0x1), EV6, ARG_EV6HWMEM },
  { "hw_stq/pa",	EV4HWMEM(0x1F,0xD), EV4, ARG_EV4HWMEM },
  { "hw_stq/pa",	EV5HWMEM(0x1F,0x34), EV5, ARG_EV5HWMEM },
  { "hw_stq/pac",	EV5HWMEM(0x1F,0x35), EV5, ARG_EV5HWMEM },
  { "hw_stq/par",	EV4HWMEM(0x1F,0xE), EV4, ARG_EV4HWMEM },
  { "hw_stq/par",	EV4HWMEM(0x1F,0xF), EV4, ARG_EV4HWMEM },
  { "hw_stq/pav",	EV5HWMEM(0x1F,0x36), EV5, ARG_EV5HWMEM },
  { "hw_stq/pavc",	EV5HWMEM(0x1F,0x37), EV5, ARG_EV5HWMEM },
  { "hw_stq/pc",	EV5HWMEM(0x1F,0x25), EV5, ARG_EV5HWMEM },
  { "hw_stq/pr",	EV4HWMEM(0x1F,0xB), EV4, ARG_EV4HWMEM },
  { "hw_stq/pv",	EV5HWMEM(0x1F,0x26), EV5, ARG_EV5HWMEM },
  { "hw_stq/pvc",	EV5HWMEM(0x1F,0x27), EV5, ARG_EV5HWMEM },
  { "hw_stq/r",		EV4HWMEM(0x1F,0x3), EV4, ARG_EV4HWMEM },
  { "hw_stq/v",		EV5HWMEM(0x1F,0x06), EV5, ARG_EV5HWMEM },
  { "hw_stq/vc",	EV5HWMEM(0x1F,0x07), EV5, ARG_EV5HWMEM },
  { "hw_stq_c",		EV5HWMEM(0x1F,0x05), EV5, ARG_EV5HWMEM },
  { "hw_stq_c/a",	EV5HWMEM(0x1F,0x15), EV5, ARG_EV5HWMEM },
  { "hw_stq_c/av",	EV5HWMEM(0x1F,0x17), EV5, ARG_EV5HWMEM },
  { "hw_stq_c/p",	EV5HWMEM(0x1F,0x25), EV5, ARG_EV5HWMEM },
  { "hw_stq_c/p",	EV6HWMEM(0x1F,0x3), EV6, ARG_EV6HWMEM },
  { "hw_stq_c/pa",	EV5HWMEM(0x1F,0x35), EV5, ARG_EV5HWMEM },
  { "hw_stq_c/pav",	EV5HWMEM(0x1F,0x37), EV5, ARG_EV5HWMEM },
  { "hw_stq_c/pv",	EV5HWMEM(0x1F,0x27), EV5, ARG_EV5HWMEM },
  { "hw_stq_c/v",	EV5HWMEM(0x1F,0x07), EV5, ARG_EV5HWMEM },
  { "hw_st",		EV4HWMEM(0x1F,0x0), EV4, ARG_EV4HWMEM },
  { "hw_st",		EV5HWMEM(0x1F,0x00), EV5, ARG_EV5HWMEM },
  { "hw_st/a",		EV4HWMEM(0x1F,0x4), EV4, ARG_EV4HWMEM },
  { "hw_st/a",		EV5HWMEM(0x1F,0x10), EV5, ARG_EV5HWMEM },
  { "hw_st/ac",		EV5HWMEM(0x1F,0x11), EV5, ARG_EV5HWMEM },
  { "hw_st/aq",		EV4HWMEM(0x1F,0x5), EV4, ARG_EV4HWMEM },
  { "hw_st/aq",		EV5HWMEM(0x1F,0x14), EV5, ARG_EV5HWMEM },
  { "hw_st/aqc",	EV5HWMEM(0x1F,0x15), EV5, ARG_EV5HWMEM },
  { "hw_st/aqv",	EV5HWMEM(0x1F,0x16), EV5, ARG_EV5HWMEM },
  { "hw_st/aqvc",	EV5HWMEM(0x1F,0x17), EV5, ARG_EV5HWMEM },
  { "hw_st/ar",		EV4HWMEM(0x1F,0x6), EV4, ARG_EV4HWMEM },
  { "hw_st/arq",	EV4HWMEM(0x1F,0x7), EV4, ARG_EV4HWMEM },
  { "hw_st/av",		EV5HWMEM(0x1F,0x12), EV5, ARG_EV5HWMEM },
  { "hw_st/avc",	EV5HWMEM(0x1F,0x13), EV5, ARG_EV5HWMEM },
  { "hw_st/c",		EV5HWMEM(0x1F,0x01), EV5, ARG_EV5HWMEM },
  { "hw_st/p",		EV4HWMEM(0x1F,0x8), EV4, ARG_EV4HWMEM },
  { "hw_st/p",		EV5HWMEM(0x1F,0x20), EV5, ARG_EV5HWMEM },
  { "hw_st/pa",		EV4HWMEM(0x1F,0xC), EV4, ARG_EV4HWMEM },
  { "hw_st/pa",		EV5HWMEM(0x1F,0x30), EV5, ARG_EV5HWMEM },
  { "hw_st/pac",	EV5HWMEM(0x1F,0x31), EV5, ARG_EV5HWMEM },
  { "hw_st/paq",	EV4HWMEM(0x1F,0xD), EV4, ARG_EV4HWMEM },
  { "hw_st/paq",	EV5HWMEM(0x1F,0x34), EV5, ARG_EV5HWMEM },
  { "hw_st/paqc",	EV5HWMEM(0x1F,0x35), EV5, ARG_EV5HWMEM },
  { "hw_st/paqv",	EV5HWMEM(0x1F,0x36), EV5, ARG_EV5HWMEM },
  { "hw_st/paqvc",	EV5HWMEM(0x1F,0x37), EV5, ARG_EV5HWMEM },
  { "hw_st/par",	EV4HWMEM(0x1F,0xE), EV4, ARG_EV4HWMEM },
  { "hw_st/parq",	EV4HWMEM(0x1F,0xF), EV4, ARG_EV4HWMEM },
  { "hw_st/pav",	EV5HWMEM(0x1F,0x32), EV5, ARG_EV5HWMEM },
  { "hw_st/pavc",	EV5HWMEM(0x1F,0x33), EV5, ARG_EV5HWMEM },
  { "hw_st/pc",		EV5HWMEM(0x1F,0x21), EV5, ARG_EV5HWMEM },
  { "hw_st/pq",		EV4HWMEM(0x1F,0x9), EV4, ARG_EV4HWMEM },
  { "hw_st/pq",		EV5HWMEM(0x1F,0x24), EV5, ARG_EV5HWMEM },
  { "hw_st/pqc",	EV5HWMEM(0x1F,0x25), EV5, ARG_EV5HWMEM },
  { "hw_st/pqv",	EV5HWMEM(0x1F,0x26), EV5, ARG_EV5HWMEM },
  { "hw_st/pqvc",	EV5HWMEM(0x1F,0x27), EV5, ARG_EV5HWMEM },
  { "hw_st/pr",		EV4HWMEM(0x1F,0xA), EV4, ARG_EV4HWMEM },
  { "hw_st/prq",	EV4HWMEM(0x1F,0xB), EV4, ARG_EV4HWMEM },
  { "hw_st/pv",		EV5HWMEM(0x1F,0x22), EV5, ARG_EV5HWMEM },
  { "hw_st/pvc",	EV5HWMEM(0x1F,0x23), EV5, ARG_EV5HWMEM },
  { "hw_st/q",		EV4HWMEM(0x1F,0x1), EV4, ARG_EV4HWMEM },
  { "hw_st/q",		EV5HWMEM(0x1F,0x04), EV5, ARG_EV5HWMEM },
  { "hw_st/qc",		EV5HWMEM(0x1F,0x05), EV5, ARG_EV5HWMEM },
  { "hw_st/qv",		EV5HWMEM(0x1F,0x06), EV5, ARG_EV5HWMEM },
  { "hw_st/qvc",	EV5HWMEM(0x1F,0x07), EV5, ARG_EV5HWMEM },
  { "hw_st/r",		EV4HWMEM(0x1F,0x2), EV4, ARG_EV4HWMEM },
  { "hw_st/v",		EV5HWMEM(0x1F,0x02), EV5, ARG_EV5HWMEM },
  { "hw_st/vc",		EV5HWMEM(0x1F,0x03), EV5, ARG_EV5HWMEM },
  { "pal1f",		PCD(0x1F), BASE, ARG_PCD },

  { "ldf",		MEM(0x20), BASE, ARG_FMEM },
  { "ldg",		MEM(0x21), BASE, ARG_FMEM },
  { "lds",		MEM(0x22), BASE, ARG_FMEM },
  { "ldt",		MEM(0x23), BASE, ARG_FMEM },
  { "stf",		MEM(0x24), BASE, ARG_FMEM },
  { "stg",		MEM(0x25), BASE, ARG_FMEM },
  { "sts",		MEM(0x26), BASE, ARG_FMEM },
  { "stt",		MEM(0x27), BASE, ARG_FMEM },

  { "ldl",		MEM(0x28), BASE, ARG_MEM },
  { "ldq",		MEM(0x29), BASE, ARG_MEM },
  { "ldl_l",		MEM(0x2A), BASE, ARG_MEM },
  { "ldq_l",		MEM(0x2B), BASE, ARG_MEM },
  { "stl",		MEM(0x2C), BASE, ARG_MEM },
  { "stq",		MEM(0x2D), BASE, ARG_MEM },
  { "stl_c",		MEM(0x2E), BASE, ARG_MEM },
  { "stq_c",		MEM(0x2F), BASE, ARG_MEM },

  { "br",		BRA(0x30), BASE, { ZA, BDISP } },	/* pseudo */
  { "br",		BRA(0x30), BASE, ARG_BRA },
  { "fbeq",		BRA(0x31), BASE, ARG_FBRA },
  { "fblt",		BRA(0x32), BASE, ARG_FBRA },
  { "fble",		BRA(0x33), BASE, ARG_FBRA },
  { "bsr",		BRA(0x34), BASE, ARG_BRA },
  { "fbne",		BRA(0x35), BASE, ARG_FBRA },
  { "fbge",		BRA(0x36), BASE, ARG_FBRA },
  { "fbgt",		BRA(0x37), BASE, ARG_FBRA },
  { "blbc",		BRA(0x38), BASE, ARG_BRA },
  { "beq",		BRA(0x39), BASE, ARG_BRA },
  { "blt",		BRA(0x3A), BASE, ARG_BRA },
  { "ble",		BRA(0x3B), BASE, ARG_BRA },
  { "blbs",		BRA(0x3C), BASE, ARG_BRA },
  { "bne",		BRA(0x3D), BASE, ARG_BRA },
  { "bge",		BRA(0x3E), BASE, ARG_BRA },
  { "bgt",		BRA(0x3F), BASE, ARG_BRA },
};

const unsigned alpha_num_opcodes = sizeof(alpha_opcodes)/sizeof(*alpha_opcodes);
