/* Opcode table for the ARC.
   Copyright 1994, 1995, 1997, 1998, 2000, 2001, 2002, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include <stdio.h>
#include "ansidecl.h"
#include "bfd.h"
#include "opcode/arc.h"
#include "opintl.h"

enum operand {OP_NONE,OP_REG,OP_SHIMM,OP_LIMM};

#define OPERANDS 3

enum operand ls_operand[OPERANDS];

struct arc_opcode *arc_ext_opcodes;
struct arc_ext_operand_value *arc_ext_operands;

#define LS_VALUE  0
#define LS_DEST   0
#define LS_BASE   1
#define LS_OFFSET 2

/* Given a format letter, yields the index into `arc_operands'.
   eg: arc_operand_map['a'] = REGA.  */
unsigned char arc_operand_map[256];

/* Nonzero if we've seen an 'f' suffix (in certain insns).  */
static int flag_p;

/* Nonzero if we've finished processing the 'f' suffix.  */
static int flagshimm_handled_p;

/* Nonzero if we've seen a 'a' suffix (address writeback).  */
static int addrwb_p;

/* Nonzero if we've seen a 'q' suffix (condition code).  */
static int cond_p;

/* Nonzero if we've inserted a nullify condition.  */
static int nullify_p;

/* The value of the a nullify condition we inserted.  */
static int nullify;

/* Nonzero if we've inserted jumpflags.  */
static int jumpflags_p;

/* Nonzero if we've inserted a shimm.  */
static int shimm_p;

/* The value of the shimm we inserted (each insn only gets one but it can
   appear multiple times).  */
static int shimm;

/* Nonzero if we've inserted a limm (during assembly) or seen a limm
   (during disassembly).  */
static int limm_p;

/* The value of the limm we inserted.  Each insn only gets one but it can
   appear multiple times.  */
static long limm;

#define INSERT_FN(fn) \
static arc_insn fn (arc_insn, const struct arc_operand *, \
		    int, const struct arc_operand_value *, long, \
		    const char **)

#define EXTRACT_FN(fn) \
static long fn (arc_insn *, const struct arc_operand *, \
		int, const struct arc_operand_value **, int *)

INSERT_FN (insert_reg);
INSERT_FN (insert_shimmfinish);
INSERT_FN (insert_limmfinish);
INSERT_FN (insert_offset);
INSERT_FN (insert_base);
INSERT_FN (insert_st_syntax);
INSERT_FN (insert_ld_syntax);
INSERT_FN (insert_addr_wb);
INSERT_FN (insert_flag);
INSERT_FN (insert_nullify);
INSERT_FN (insert_flagfinish);
INSERT_FN (insert_cond);
INSERT_FN (insert_forcelimm);
INSERT_FN (insert_reladdr);
INSERT_FN (insert_absaddr);
INSERT_FN (insert_jumpflags);
INSERT_FN (insert_unopmacro);

EXTRACT_FN (extract_reg);
EXTRACT_FN (extract_ld_offset);
EXTRACT_FN (extract_ld_syntax);
EXTRACT_FN (extract_st_offset);
EXTRACT_FN (extract_st_syntax);
EXTRACT_FN (extract_flag);
EXTRACT_FN (extract_cond);
EXTRACT_FN (extract_reladdr);
EXTRACT_FN (extract_jumpflags);
EXTRACT_FN (extract_unopmacro);

/* Various types of ARC operands, including insn suffixes.  */

/* Insn format values:

   'a'	REGA		register A field
   'b'	REGB		register B field
   'c'	REGC		register C field
   'S'	SHIMMFINISH	finish inserting a shimm value
   'L'	LIMMFINISH	finish inserting a limm value
   'o'	OFFSET		offset in st insns
   'O'	OFFSET		offset in ld insns
   '0'	SYNTAX_ST_NE	enforce store insn syntax, no errors
   '1'	SYNTAX_LD_NE	enforce load insn syntax, no errors
   '2'  SYNTAX_ST       enforce store insn syntax, errors, last pattern only
   '3'  SYNTAX_LD       enforce load insn syntax, errors, last pattern only
   's'  BASE            base in st insn
   'f'	FLAG		F flag
   'F'	FLAGFINISH	finish inserting the F flag
   'G'	FLAGINSN	insert F flag in "flag" insn
   'n'	DELAY		N field (nullify field)
   'q'	COND		condition code field
   'Q'	FORCELIMM	set `cond_p' to 1 to ensure a constant is a limm
   'B'	BRANCH		branch address (22 bit pc relative)
   'J'	JUMP		jump address (26 bit absolute)
   'j'  JUMPFLAGS       optional high order bits of 'J'
   'z'	SIZE1		size field in ld a,[b,c]
   'Z'	SIZE10		size field in ld a,[b,shimm]
   'y'	SIZE22		size field in st c,[b,shimm]
   'x'	SIGN0		sign extend field ld a,[b,c]
   'X'	SIGN9		sign extend field ld a,[b,shimm]
   'w'	ADDRESS3	write-back field in ld a,[b,c]
   'W'	ADDRESS12	write-back field in ld a,[b,shimm]
   'v'	ADDRESS24	write-back field in st c,[b,shimm]
   'e'	CACHEBYPASS5	cache bypass in ld a,[b,c]
   'E'	CACHEBYPASS14	cache bypass in ld a,[b,shimm]
   'D'	CACHEBYPASS26	cache bypass in st c,[b,shimm]
   'U'	UNOPMACRO	fake operand to copy REGB to REGC for unop macros

   The following modifiers may appear between the % and char (eg: %.f):

   '.'	MODDOT		'.' prefix must be present
   'r'	REG		generic register value, for register table
   'A'	AUXREG		auxiliary register in lr a,[b], sr c,[b]

   Fields are:

   CHAR BITS SHIFT FLAGS INSERT_FN EXTRACT_FN  */

const struct arc_operand arc_operands[] =
{
/* Place holder (??? not sure if needed).  */
#define UNUSED 0
  { 0, 0, 0, 0, 0, 0 },

/* Register A or shimm/limm indicator.  */
#define REGA (UNUSED + 1)
  { 'a', 6, ARC_SHIFT_REGA, ARC_OPERAND_SIGNED | ARC_OPERAND_ERROR, insert_reg, extract_reg },

/* Register B or shimm/limm indicator.  */
#define REGB (REGA + 1)
  { 'b', 6, ARC_SHIFT_REGB, ARC_OPERAND_SIGNED | ARC_OPERAND_ERROR, insert_reg, extract_reg },

/* Register C or shimm/limm indicator.  */
#define REGC (REGB + 1)
  { 'c', 6, ARC_SHIFT_REGC, ARC_OPERAND_SIGNED | ARC_OPERAND_ERROR, insert_reg, extract_reg },

/* Fake operand used to insert shimm value into most instructions.  */
#define SHIMMFINISH (REGC + 1)
  { 'S', 9, 0, ARC_OPERAND_SIGNED + ARC_OPERAND_FAKE, insert_shimmfinish, 0 },

/* Fake operand used to insert limm value into most instructions.  */
#define LIMMFINISH (SHIMMFINISH + 1)
  { 'L', 32, 32, ARC_OPERAND_ADDRESS + ARC_OPERAND_LIMM + ARC_OPERAND_FAKE, insert_limmfinish, 0 },

/* Shimm operand when there is no reg indicator (st).  */
#define ST_OFFSET (LIMMFINISH + 1)
  { 'o', 9, 0, ARC_OPERAND_LIMM | ARC_OPERAND_SIGNED | ARC_OPERAND_STORE, insert_offset, extract_st_offset },

/* Shimm operand when there is no reg indicator (ld).  */
#define LD_OFFSET (ST_OFFSET + 1)
  { 'O', 9, 0,ARC_OPERAND_LIMM | ARC_OPERAND_SIGNED | ARC_OPERAND_LOAD, insert_offset, extract_ld_offset },

/* Operand for base.  */
#define BASE (LD_OFFSET + 1)
  { 's', 6, ARC_SHIFT_REGB, ARC_OPERAND_LIMM | ARC_OPERAND_SIGNED, insert_base, extract_reg},

/* 0 enforce syntax for st insns.  */
#define SYNTAX_ST_NE (BASE + 1)
  { '0', 9, 0, ARC_OPERAND_FAKE, insert_st_syntax, extract_st_syntax },

/* 1 enforce syntax for ld insns.  */
#define SYNTAX_LD_NE (SYNTAX_ST_NE + 1)
  { '1', 9, 0, ARC_OPERAND_FAKE, insert_ld_syntax, extract_ld_syntax },

/* 0 enforce syntax for st insns.  */
#define SYNTAX_ST (SYNTAX_LD_NE + 1)
  { '2', 9, 0, ARC_OPERAND_FAKE | ARC_OPERAND_ERROR, insert_st_syntax, extract_st_syntax },

/* 0 enforce syntax for ld insns.  */
#define SYNTAX_LD (SYNTAX_ST + 1)
  { '3', 9, 0, ARC_OPERAND_FAKE | ARC_OPERAND_ERROR, insert_ld_syntax, extract_ld_syntax },

/* Flag update bit (insertion is defered until we know how).  */
#define FLAG (SYNTAX_LD + 1)
  { 'f', 1, 8, ARC_OPERAND_SUFFIX, insert_flag, extract_flag },

/* Fake utility operand to finish 'f' suffix handling.  */
#define FLAGFINISH (FLAG + 1)
  { 'F', 1, 8, ARC_OPERAND_FAKE, insert_flagfinish, 0 },

/* Fake utility operand to set the 'f' flag for the "flag" insn.  */
#define FLAGINSN (FLAGFINISH + 1)
  { 'G', 1, 8, ARC_OPERAND_FAKE, insert_flag, 0 },

/* Branch delay types.  */
#define DELAY (FLAGINSN + 1)
  { 'n', 2, 5, ARC_OPERAND_SUFFIX , insert_nullify, 0 },

/* Conditions.  */
#define COND (DELAY + 1)
  { 'q', 5, 0, ARC_OPERAND_SUFFIX, insert_cond, extract_cond },

/* Set `cond_p' to 1 to ensure a constant is treated as a limm.  */
#define FORCELIMM (COND + 1)
  { 'Q', 0, 0, ARC_OPERAND_FAKE, insert_forcelimm, 0 },

/* Branch address; b, bl, and lp insns.  */
#define BRANCH (FORCELIMM + 1)
  { 'B', 20, 7, (ARC_OPERAND_RELATIVE_BRANCH + ARC_OPERAND_SIGNED) | ARC_OPERAND_ERROR, insert_reladdr, extract_reladdr },

/* Jump address; j insn (this is basically the same as 'L' except that the
   value is right shifted by 2).  */
#define JUMP (BRANCH + 1)
  { 'J', 24, 32, ARC_OPERAND_ERROR | (ARC_OPERAND_ABSOLUTE_BRANCH + ARC_OPERAND_LIMM + ARC_OPERAND_FAKE), insert_absaddr, 0 },

/* Jump flags; j{,l} insn value or'ed into 'J' addr for flag values.  */
#define JUMPFLAGS (JUMP + 1)
  { 'j', 6, 26, ARC_OPERAND_JUMPFLAGS | ARC_OPERAND_ERROR, insert_jumpflags, extract_jumpflags },

/* Size field, stored in bit 1,2.  */
#define SIZE1 (JUMPFLAGS + 1)
  { 'z', 2, 1, ARC_OPERAND_SUFFIX, 0, 0 },

/* Size field, stored in bit 10,11.  */
#define SIZE10 (SIZE1 + 1)
  { 'Z', 2, 10, ARC_OPERAND_SUFFIX, 0, 0 },

/* Size field, stored in bit 22,23.  */
#define SIZE22 (SIZE10 + 1)
  { 'y', 2, 22, ARC_OPERAND_SUFFIX, 0, 0 },

/* Sign extend field, stored in bit 0.  */
#define SIGN0 (SIZE22 + 1)
  { 'x', 1, 0, ARC_OPERAND_SUFFIX, 0, 0 },

/* Sign extend field, stored in bit 9.  */
#define SIGN9 (SIGN0 + 1)
  { 'X', 1, 9, ARC_OPERAND_SUFFIX, 0, 0 },

/* Address write back, stored in bit 3.  */
#define ADDRESS3 (SIGN9 + 1)
  { 'w', 1, 3, ARC_OPERAND_SUFFIX, insert_addr_wb, 0},

/* Address write back, stored in bit 12.  */
#define ADDRESS12 (ADDRESS3 + 1)
  { 'W', 1, 12, ARC_OPERAND_SUFFIX, insert_addr_wb, 0},

/* Address write back, stored in bit 24.  */
#define ADDRESS24 (ADDRESS12 + 1)
  { 'v', 1, 24, ARC_OPERAND_SUFFIX, insert_addr_wb, 0},

/* Cache bypass, stored in bit 5.  */
#define CACHEBYPASS5 (ADDRESS24 + 1)
  { 'e', 1, 5, ARC_OPERAND_SUFFIX, 0, 0 },

/* Cache bypass, stored in bit 14.  */
#define CACHEBYPASS14 (CACHEBYPASS5 + 1)
  { 'E', 1, 14, ARC_OPERAND_SUFFIX, 0, 0 },

/* Cache bypass, stored in bit 26.  */
#define CACHEBYPASS26 (CACHEBYPASS14 + 1)
  { 'D', 1, 26, ARC_OPERAND_SUFFIX, 0, 0 },

/* Unop macro, used to copy REGB to REGC.  */
#define UNOPMACRO (CACHEBYPASS26 + 1)
  { 'U', 6, ARC_SHIFT_REGC, ARC_OPERAND_FAKE, insert_unopmacro, extract_unopmacro },

/* '.' modifier ('.' required).  */
#define MODDOT (UNOPMACRO + 1)
  { '.', 1, 0, ARC_MOD_DOT, 0, 0 },

/* Dummy 'r' modifier for the register table.
   It's called a "dummy" because there's no point in inserting an 'r' into all
   the %a/%b/%c occurrences in the insn table.  */
#define REG (MODDOT + 1)
  { 'r', 6, 0, ARC_MOD_REG, 0, 0 },

/* Known auxiliary register modifier (stored in shimm field).  */
#define AUXREG (REG + 1)
  { 'A', 9, 0, ARC_MOD_AUXREG, 0, 0 },

/* End of list place holder.  */
  { 0, 0, 0, 0, 0, 0 }
};

/* Insert a value into a register field.
   If REG is NULL, then this is actually a constant.

   We must also handle auxiliary registers for lr/sr insns.  */

static arc_insn
insert_reg (arc_insn insn,
	    const struct arc_operand *operand,
	    int mods,
	    const struct arc_operand_value *reg,
	    long value,
	    const char **errmsg)
{
  static char buf[100];
  enum operand op_type = OP_NONE;

  if (reg == NULL)
    {
      /* We have a constant that also requires a value stored in a register
	 field.  Handle these by updating the register field and saving the
	 value for later handling by either %S (shimm) or %L (limm).  */

      /* Try to use a shimm value before a limm one.  */
      if (ARC_SHIMM_CONST_P (value)
	  /* If we've seen a conditional suffix we have to use a limm.  */
	  && !cond_p
	  /* If we already have a shimm value that is different than ours
	     we have to use a limm.  */
	  && (!shimm_p || shimm == value))
	{
	  int marker;

	  op_type = OP_SHIMM;
	  /* Forget about shimm as dest mlm.  */

	  if ('a' != operand->fmt)
	    {
	      shimm_p = 1;
	      shimm = value;
	      flagshimm_handled_p = 1;
	      marker = flag_p ? ARC_REG_SHIMM_UPDATE : ARC_REG_SHIMM;
	    }
	  else
	    {
	      /* Don't request flag setting on shimm as dest.  */
	      marker = ARC_REG_SHIMM;
	    }
	  insn |= marker << operand->shift;
	  /* insn |= value & 511; - done later.  */
	}
      /* We have to use a limm.  If we've already seen one they must match.  */
      else if (!limm_p || limm == value)
	{
	  op_type = OP_LIMM;
	  limm_p = 1;
	  limm = value;
	  insn |= ARC_REG_LIMM << operand->shift;
	  /* The constant is stored later.  */
	}
      else
	*errmsg = _("unable to fit different valued constants into instruction");
    }
  else
    {
      /* We have to handle both normal and auxiliary registers.  */

      if (reg->type == AUXREG)
	{
	  if (!(mods & ARC_MOD_AUXREG))
	    *errmsg = _("auxiliary register not allowed here");
	  else
	    {
	      if ((insn & I(-1)) == I(2)) /* Check for use validity.  */
		{
		  if (reg->flags & ARC_REGISTER_READONLY)
		    *errmsg = _("attempt to set readonly register");
		}
	      else
		{
		  if (reg->flags & ARC_REGISTER_WRITEONLY)
		    *errmsg = _("attempt to read writeonly register");
		}
	      insn |= ARC_REG_SHIMM << operand->shift;
	      insn |= reg->value << arc_operands[reg->type].shift;
	    }
	}
      else
	{
	  /* check for use validity.  */
	  if ('a' == operand->fmt || ((insn & I(-1)) < I(2)))
	    {
	      if (reg->flags & ARC_REGISTER_READONLY)
		*errmsg = _("attempt to set readonly register");
	    }
	  if ('a' != operand->fmt)
	    {
	      if (reg->flags & ARC_REGISTER_WRITEONLY)
		*errmsg = _("attempt to read writeonly register");
	    }
	  /* We should never get an invalid register number here.  */
	  if ((unsigned int) reg->value > 60)
	    {
	      sprintf (buf, _("invalid register number `%d'"), reg->value);
	      *errmsg = buf;
	    }
	  insn |= reg->value << operand->shift;
	  op_type = OP_REG;
	}
    }

  switch (operand->fmt)
    {
    case 'a':
      ls_operand[LS_DEST] = op_type;
      break;
    case 's':
      ls_operand[LS_BASE] = op_type;
      break;
    case 'c':
      if ((insn & I(-1)) == I(2))
	ls_operand[LS_VALUE] = op_type;
      else
	ls_operand[LS_OFFSET] = op_type;
      break;
    case 'o': case 'O':
      ls_operand[LS_OFFSET] = op_type;
      break;
    }

  return insn;
}

/* Called when we see an 'f' flag.  */

static arc_insn
insert_flag (arc_insn insn,
	     const struct arc_operand *operand ATTRIBUTE_UNUSED,
	     int mods ATTRIBUTE_UNUSED,
	     const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
	     long value ATTRIBUTE_UNUSED,
	     const char **errmsg ATTRIBUTE_UNUSED)
{
  /* We can't store anything in the insn until we've parsed the registers.
     Just record the fact that we've got this flag.  `insert_reg' will use it
     to store the correct value (ARC_REG_SHIMM_UPDATE or bit 0x100).  */
  flag_p = 1;
  return insn;
}

/* Called when we see an nullify condition.  */

static arc_insn
insert_nullify (arc_insn insn,
		const struct arc_operand *operand,
		int mods ATTRIBUTE_UNUSED,
		const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		long value,
		const char **errmsg ATTRIBUTE_UNUSED)
{
  nullify_p = 1;
  insn |= (value & ((1 << operand->bits) - 1)) << operand->shift;
  nullify = value;
  return insn;
}

/* Called after completely building an insn to ensure the 'f' flag gets set
   properly.  This is needed because we don't know how to set this flag until
   we've parsed the registers.  */

static arc_insn
insert_flagfinish (arc_insn insn,
		   const struct arc_operand *operand,
		   int mods ATTRIBUTE_UNUSED,
		   const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		   long value ATTRIBUTE_UNUSED,
		   const char **errmsg ATTRIBUTE_UNUSED)
{
  if (flag_p && !flagshimm_handled_p)
    {
      if (shimm_p)
	abort ();
      flagshimm_handled_p = 1;
      insn |= (1 << operand->shift);
    }
  return insn;
}

/* Called when we see a conditional flag (eg: .eq).  */

static arc_insn
insert_cond (arc_insn insn,
	     const struct arc_operand *operand,
	     int mods ATTRIBUTE_UNUSED,
	     const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
	     long value,
	     const char **errmsg ATTRIBUTE_UNUSED)
{
  cond_p = 1;
  insn |= (value & ((1 << operand->bits) - 1)) << operand->shift;
  return insn;
}

/* Used in the "j" instruction to prevent constants from being interpreted as
   shimm values (which the jump insn doesn't accept).  This can also be used
   to force the use of limm values in other situations (eg: ld r0,[foo] uses
   this).
   ??? The mechanism is sound.  Access to it is a bit klunky right now.  */

static arc_insn
insert_forcelimm (arc_insn insn,
		  const struct arc_operand *operand ATTRIBUTE_UNUSED,
		  int mods ATTRIBUTE_UNUSED,
		  const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		  long value ATTRIBUTE_UNUSED,
		  const char **errmsg ATTRIBUTE_UNUSED)
{
  cond_p = 1;
  return insn;
}

static arc_insn
insert_addr_wb (arc_insn insn,
		const struct arc_operand *operand,
		int mods ATTRIBUTE_UNUSED,
		const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		long value ATTRIBUTE_UNUSED,
		const char **errmsg ATTRIBUTE_UNUSED)
{
  addrwb_p = 1 << operand->shift;
  return insn;
}

static arc_insn
insert_base (arc_insn insn,
	     const struct arc_operand *operand,
	     int mods,
	     const struct arc_operand_value *reg,
	     long value,
	     const char **errmsg)
{
  if (reg != NULL)
    {
      arc_insn myinsn;
      myinsn = insert_reg (0, operand,mods, reg, value, errmsg) >> operand->shift;
      insn |= B(myinsn);
      ls_operand[LS_BASE] = OP_REG;
    }
  else if (ARC_SHIMM_CONST_P (value) && !cond_p)
    {
      if (shimm_p && value != shimm)
	{
	  /* Convert the previous shimm operand to a limm.  */
	  limm_p = 1;
	  limm = shimm;
	  insn &= ~C(-1); /* We know where the value is in insn.  */
	  insn |= C(ARC_REG_LIMM);
	  ls_operand[LS_VALUE] = OP_LIMM;
	}
      insn |= ARC_REG_SHIMM << operand->shift;
      shimm_p = 1;
      shimm = value;
      ls_operand[LS_BASE] = OP_SHIMM;
      ls_operand[LS_OFFSET] = OP_SHIMM;
    }
  else
    {
      if (limm_p && value != limm)
	{
	  *errmsg = _("too many long constants");
	  return insn;
	}
      limm_p = 1;
      limm = value;
      insn |= B(ARC_REG_LIMM);
      ls_operand[LS_BASE] = OP_LIMM;
    }

  return insn;
}

/* Used in ld/st insns to handle the offset field. We don't try to
   match operand syntax here. we catch bad combinations later.  */

static arc_insn
insert_offset (arc_insn insn,
	       const struct arc_operand *operand,
	       int mods,
	       const struct arc_operand_value *reg,
	       long value,
	       const char **errmsg)
{
  long minval, maxval;

  if (reg != NULL)
    {
      arc_insn myinsn;
      myinsn = insert_reg (0,operand,mods,reg,value,errmsg) >> operand->shift;
      ls_operand[LS_OFFSET] = OP_REG;
      if (operand->flags & ARC_OPERAND_LOAD) /* Not if store, catch it later.  */
	if ((insn & I(-1)) != I(1)) /* Not if opcode == 1, catch it later.  */
	  insn |= C (myinsn);
    }
  else
    {
      /* This is *way* more general than necessary, but maybe some day it'll
	 be useful.  */
      if (operand->flags & ARC_OPERAND_SIGNED)
	{
	  minval = -(1 << (operand->bits - 1));
	  maxval = (1 << (operand->bits - 1)) - 1;
	}
      else
	{
	  minval = 0;
	  maxval = (1 << operand->bits) - 1;
	}
      if ((cond_p && !limm_p) || (value < minval || value > maxval))
	{
	  if (limm_p && value != limm)
	    *errmsg = _("too many long constants");

	  else
	    {
	      limm_p = 1;
	      limm = value;
	      if (operand->flags & ARC_OPERAND_STORE)
		insn |= B(ARC_REG_LIMM);
	      if (operand->flags & ARC_OPERAND_LOAD)
		insn |= C(ARC_REG_LIMM);
	      ls_operand[LS_OFFSET] = OP_LIMM;
	    }
	}
      else
	{
	  if ((value < minval || value > maxval))
	    *errmsg = "need too many limms";
	  else if (shimm_p && value != shimm)
	    {
	      /* Check for bad operand combinations
		 before we lose info about them.  */
	      if ((insn & I(-1)) == I(1))
		{
		  *errmsg = _("to many shimms in load");
		  goto out;
		}
	      if (limm_p && operand->flags & ARC_OPERAND_LOAD)
		{
		  *errmsg = _("too many long constants");
		  goto out;
		}
	      /* Convert what we thought was a shimm to a limm.  */
	      limm_p = 1;
	      limm = shimm;
	      if (ls_operand[LS_VALUE] == OP_SHIMM
		  && operand->flags & ARC_OPERAND_STORE)
		{
		  insn &= ~C(-1);
		  insn |= C(ARC_REG_LIMM);
		  ls_operand[LS_VALUE] = OP_LIMM;
		}
	      if (ls_operand[LS_BASE] == OP_SHIMM
		  && operand->flags & ARC_OPERAND_STORE)
		{
		  insn &= ~B(-1);
		  insn |= B(ARC_REG_LIMM);
		  ls_operand[LS_BASE] = OP_LIMM;
		}
	    }
	  shimm = value;
	  shimm_p = 1;
	  ls_operand[LS_OFFSET] = OP_SHIMM;
	}
    }
 out:
  return insn;
}

/* Used in st insns to do final disasemble syntax check.  */

static long
extract_st_syntax (arc_insn *insn,
		   const struct arc_operand *operand ATTRIBUTE_UNUSED,
		   int mods ATTRIBUTE_UNUSED,
		   const struct arc_operand_value **opval ATTRIBUTE_UNUSED,
		   int *invalid)
{
#define ST_SYNTAX(V,B,O) \
((ls_operand[LS_VALUE]  == (V) && \
  ls_operand[LS_BASE]   == (B) && \
  ls_operand[LS_OFFSET] == (O)))

  if (!((ST_SYNTAX(OP_REG,OP_REG,OP_NONE) && (insn[0] & 511) == 0)
	|| ST_SYNTAX(OP_REG,OP_LIMM,OP_NONE)
	|| (ST_SYNTAX(OP_SHIMM,OP_REG,OP_NONE) && (insn[0] & 511) == 0)
	|| (ST_SYNTAX(OP_SHIMM,OP_SHIMM,OP_NONE) && (insn[0] & 511) == 0)
	|| ST_SYNTAX(OP_SHIMM,OP_LIMM,OP_NONE)
	|| ST_SYNTAX(OP_SHIMM,OP_LIMM,OP_SHIMM)
	|| ST_SYNTAX(OP_SHIMM,OP_SHIMM,OP_SHIMM)
	|| (ST_SYNTAX(OP_LIMM,OP_REG,OP_NONE) && (insn[0] & 511) == 0)
	|| ST_SYNTAX(OP_REG,OP_REG,OP_SHIMM)
	|| ST_SYNTAX(OP_REG,OP_SHIMM,OP_SHIMM)
	|| ST_SYNTAX(OP_SHIMM,OP_REG,OP_SHIMM)
	|| ST_SYNTAX(OP_LIMM,OP_SHIMM,OP_SHIMM)
	|| ST_SYNTAX(OP_LIMM,OP_SHIMM,OP_NONE)
	|| ST_SYNTAX(OP_LIMM,OP_REG,OP_SHIMM)))
    *invalid = 1;
  return 0;
}

int
arc_limm_fixup_adjust (arc_insn insn)
{
  int retval = 0;

  /* Check for st shimm,[limm].  */
  if ((insn & (I(-1) | C(-1) | B(-1))) ==
      (I(2) | C(ARC_REG_SHIMM) | B(ARC_REG_LIMM)))
    {
      retval = insn & 0x1ff;
      if (retval & 0x100) /* Sign extend 9 bit offset.  */
	retval |= ~0x1ff;
    }
  return -retval; /* Negate offset for return.  */
}

/* Used in st insns to do final syntax check.  */

static arc_insn
insert_st_syntax (arc_insn insn,
		  const struct arc_operand *operand ATTRIBUTE_UNUSED,
		  int mods ATTRIBUTE_UNUSED,
		  const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		  long value ATTRIBUTE_UNUSED,
		  const char **errmsg)
{
  if (ST_SYNTAX (OP_SHIMM,OP_REG,OP_NONE) && shimm != 0)
    {
      /* Change an illegal insn into a legal one, it's easier to
	 do it here than to try to handle it during operand scan.  */
      limm_p = 1;
      limm = shimm;
      shimm_p = 0;
      shimm = 0;
      insn = insn & ~(C(-1) | 511);
      insn |= ARC_REG_LIMM << ARC_SHIFT_REGC;
      ls_operand[LS_VALUE] = OP_LIMM;
    }

  if (ST_SYNTAX (OP_REG, OP_SHIMM, OP_NONE)
      || ST_SYNTAX (OP_LIMM, OP_SHIMM, OP_NONE))
    {
      /* Try to salvage this syntax.  */
      if (shimm & 0x1) /* Odd shimms won't work.  */
	{
	  if (limm_p) /* Do we have a limm already?  */
	    *errmsg = _("impossible store");

	  limm_p = 1;
	  limm = shimm;
	  shimm = 0;
	  shimm_p = 0;
	  insn = insn & ~(B(-1) | 511);
	  insn |= B(ARC_REG_LIMM);
	  ls_operand[LS_BASE] = OP_LIMM;
	}
      else
	{
	  shimm >>= 1;
	  insn = insn & ~511;
	  insn |= shimm;
	  ls_operand[LS_OFFSET] = OP_SHIMM;
	}
    }
  if (ST_SYNTAX(OP_SHIMM,OP_LIMM,OP_NONE))
    limm += arc_limm_fixup_adjust(insn);

  if (!   (ST_SYNTAX (OP_REG,OP_REG,OP_NONE)
	|| ST_SYNTAX (OP_REG,OP_LIMM,OP_NONE)
	|| ST_SYNTAX (OP_REG,OP_REG,OP_SHIMM)
	|| ST_SYNTAX (OP_REG,OP_SHIMM,OP_SHIMM)
	|| (ST_SYNTAX (OP_SHIMM,OP_SHIMM,OP_NONE) && (shimm == 0))
	|| ST_SYNTAX (OP_SHIMM,OP_LIMM,OP_NONE)
	|| ST_SYNTAX (OP_SHIMM,OP_REG,OP_NONE)
	|| ST_SYNTAX (OP_SHIMM,OP_REG,OP_SHIMM)
	|| ST_SYNTAX (OP_SHIMM,OP_SHIMM,OP_SHIMM)
	|| ST_SYNTAX (OP_LIMM,OP_SHIMM,OP_SHIMM)
	|| ST_SYNTAX (OP_LIMM,OP_REG,OP_NONE)
	|| ST_SYNTAX (OP_LIMM,OP_REG,OP_SHIMM)))
    *errmsg = _("st operand error");
  if (addrwb_p)
    {
      if (ls_operand[LS_BASE] != OP_REG)
	*errmsg = _("address writeback not allowed");
      insn |= addrwb_p;
    }
  if (ST_SYNTAX(OP_SHIMM,OP_REG,OP_NONE) && shimm)
    *errmsg = _("store value must be zero");
  return insn;
}

/* Used in ld insns to do final syntax check.  */

static arc_insn
insert_ld_syntax (arc_insn insn,
		  const struct arc_operand *operand ATTRIBUTE_UNUSED,
		  int mods ATTRIBUTE_UNUSED,
		  const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		  long value ATTRIBUTE_UNUSED,
		  const char **errmsg)
{
#define LD_SYNTAX(D, B, O) \
  (   (ls_operand[LS_DEST]   == (D) \
    && ls_operand[LS_BASE]   == (B) \
    && ls_operand[LS_OFFSET] == (O)))

  int test = insn & I (-1);

  if (!(test == I (1)))
    {
      if ((ls_operand[LS_DEST] == OP_SHIMM || ls_operand[LS_BASE] == OP_SHIMM
	   || ls_operand[LS_OFFSET] == OP_SHIMM))
	*errmsg = _("invalid load/shimm insn");
    }
  if (!(LD_SYNTAX(OP_REG,OP_REG,OP_NONE)
	|| LD_SYNTAX(OP_REG,OP_REG,OP_REG)
	|| LD_SYNTAX(OP_REG,OP_REG,OP_SHIMM)
	|| (LD_SYNTAX(OP_REG,OP_LIMM,OP_REG) && !(test == I(1)))
	|| (LD_SYNTAX(OP_REG,OP_REG,OP_LIMM) && !(test == I(1)))
	|| LD_SYNTAX(OP_REG,OP_SHIMM,OP_SHIMM)
	|| (LD_SYNTAX(OP_REG,OP_LIMM,OP_NONE) && (test == I(1)))))
    *errmsg = _("ld operand error");
  if (addrwb_p)
    {
      if (ls_operand[LS_BASE] != OP_REG)
	*errmsg = _("address writeback not allowed");
      insn |= addrwb_p;
    }
  return insn;
}

/* Used in ld insns to do final syntax check.  */

static long
extract_ld_syntax (arc_insn *insn,
		   const struct arc_operand *operand ATTRIBUTE_UNUSED,
		   int mods ATTRIBUTE_UNUSED,
		   const struct arc_operand_value **opval ATTRIBUTE_UNUSED,
		   int *invalid)
{
  int test = insn[0] & I(-1);

  if (!(test == I(1)))
    {
      if ((ls_operand[LS_DEST] == OP_SHIMM || ls_operand[LS_BASE] == OP_SHIMM
	   || ls_operand[LS_OFFSET] == OP_SHIMM))
	*invalid = 1;
    }
  if (!(   (LD_SYNTAX (OP_REG, OP_REG, OP_NONE) && (test == I(1)))
	||  LD_SYNTAX (OP_REG, OP_REG, OP_REG)
	||  LD_SYNTAX (OP_REG, OP_REG, OP_SHIMM)
	|| (LD_SYNTAX (OP_REG, OP_REG, OP_LIMM) && !(test == I(1)))
	|| (LD_SYNTAX (OP_REG, OP_LIMM, OP_REG) && !(test == I(1)))
	|| (LD_SYNTAX (OP_REG, OP_SHIMM, OP_NONE) && (shimm == 0))
	||  LD_SYNTAX (OP_REG, OP_SHIMM, OP_SHIMM)
	|| (LD_SYNTAX (OP_REG, OP_LIMM, OP_NONE) && (test == I(1)))))
    *invalid = 1;
  return 0;
}

/* Called at the end of processing normal insns (eg: add) to insert a shimm
   value (if present) into the insn.  */

static arc_insn
insert_shimmfinish (arc_insn insn,
		    const struct arc_operand *operand,
		    int mods ATTRIBUTE_UNUSED,
		    const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		    long value ATTRIBUTE_UNUSED,
		    const char **errmsg ATTRIBUTE_UNUSED)
{
  if (shimm_p)
    insn |= (shimm & ((1 << operand->bits) - 1)) << operand->shift;
  return insn;
}

/* Called at the end of processing normal insns (eg: add) to insert a limm
   value (if present) into the insn.

   Note that this function is only intended to handle instructions (with 4 byte
   immediate operands).  It is not intended to handle data.  */

/* ??? Actually, there's nothing for us to do as we can't call frag_more, the
   caller must do that.  The extract fns take a pointer to two words.  The
   insert fns could be converted and then we could do something useful, but
   then the reloc handlers would have to know to work on the second word of
   a 2 word quantity.  That's too much so we don't handle them.  */

static arc_insn
insert_limmfinish (arc_insn insn,
		   const struct arc_operand *operand ATTRIBUTE_UNUSED,
		   int mods ATTRIBUTE_UNUSED,
		   const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		   long value ATTRIBUTE_UNUSED,
		   const char **errmsg ATTRIBUTE_UNUSED)
{
  return insn;
}

static arc_insn
insert_jumpflags (arc_insn insn,
		  const struct arc_operand *operand,
		  int mods ATTRIBUTE_UNUSED,
		  const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		  long value,
		  const char **errmsg)
{
  if (!flag_p)
    *errmsg = _("jump flags, but no .f seen");

  else if (!limm_p)
    *errmsg = _("jump flags, but no limm addr");

  else if (limm & 0xfc000000)
    *errmsg = _("flag bits of jump address limm lost");

  else if (limm & 0x03000000)
    *errmsg = _("attempt to set HR bits");

  else if ((value & ((1 << operand->bits) - 1)) != value)
    *errmsg = _("bad jump flags value");

  jumpflags_p = 1;
  limm = ((limm & ((1 << operand->shift) - 1))
	  | ((value & ((1 << operand->bits) - 1)) << operand->shift));
  return insn;
}

/* Called at the end of unary operand macros to copy the B field to C.  */

static arc_insn
insert_unopmacro (arc_insn insn,
		  const struct arc_operand *operand,
		  int mods ATTRIBUTE_UNUSED,
		  const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		  long value ATTRIBUTE_UNUSED,
		  const char **errmsg ATTRIBUTE_UNUSED)
{
  insn |= ((insn >> ARC_SHIFT_REGB) & ARC_MASK_REG) << operand->shift;
  return insn;
}

/* Insert a relative address for a branch insn (b, bl, or lp).  */

static arc_insn
insert_reladdr (arc_insn insn,
		const struct arc_operand *operand,
		int mods ATTRIBUTE_UNUSED,
		const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		long value,
		const char **errmsg)
{
  if (value & 3)
    *errmsg = _("branch address not on 4 byte boundary");
  insn |= ((value >> 2) & ((1 << operand->bits) - 1)) << operand->shift;
  return insn;
}

/* Insert a limm value as a 26 bit address right shifted 2 into the insn.

   Note that this function is only intended to handle instructions (with 4 byte
   immediate operands).  It is not intended to handle data.  */

/* ??? Actually, there's little for us to do as we can't call frag_more, the
   caller must do that.  The extract fns take a pointer to two words.  The
   insert fns could be converted and then we could do something useful, but
   then the reloc handlers would have to know to work on the second word of
   a 2 word quantity.  That's too much so we don't handle them.

   We do check for correct usage of the nullify suffix, or we
   set the default correctly, though.  */

static arc_insn
insert_absaddr (arc_insn insn,
		const struct arc_operand *operand ATTRIBUTE_UNUSED,
		int mods ATTRIBUTE_UNUSED,
		const struct arc_operand_value *reg ATTRIBUTE_UNUSED,
		long value ATTRIBUTE_UNUSED,
		const char **errmsg)
{
  if (limm_p)
    {
      /* If it is a jump and link, .jd must be specified.  */
      if (insn & R (-1, 9, 1))
	{
	  if (!nullify_p)
	    insn |=  0x02 << 5;  /* Default nullify to .jd.  */

	  else if (nullify != 0x02)
	    *errmsg = _("must specify .jd or no nullify suffix");
	}
    }
  return insn;
}

/* Extraction functions.

   The suffix extraction functions' return value is redundant since it can be
   obtained from (*OPVAL)->value.  However, the boolean suffixes don't have
   a suffix table entry for the "false" case, so values of zero must be
   obtained from the return value (*OPVAL == NULL).  */

/* Called by the disassembler before printing an instruction.  */

void
arc_opcode_init_extract (void)
{
  arc_opcode_init_insert ();
}

static const struct arc_operand_value *
lookup_register (int type, long regno)
{
  const struct arc_operand_value *r,*end;
  struct arc_ext_operand_value *ext_oper = arc_ext_operands;

  while (ext_oper)
    {
      if (ext_oper->operand.type == type && ext_oper->operand.value == regno)
	return (&ext_oper->operand);
      ext_oper = ext_oper->next;
    }

  if (type == REG)
    return &arc_reg_names[regno];

  /* ??? This is a little slow and can be speeded up.  */
  for (r = arc_reg_names, end = arc_reg_names + arc_reg_names_count;
       r < end; ++r)
    if (type == r->type	&& regno == r->value)
      return r;
  return 0;
}

/* As we're extracting registers, keep an eye out for the 'f' indicator
   (ARC_REG_SHIMM_UPDATE).  If we find a register (not a constant marker,
   like ARC_REG_SHIMM), set OPVAL so our caller will know this is a register.

   We must also handle auxiliary registers for lr/sr insns.  They are just
   constants with special names.  */

static long
extract_reg (arc_insn *insn,
	     const struct arc_operand *operand,
	     int mods,
	     const struct arc_operand_value **opval,
	     int *invalid ATTRIBUTE_UNUSED)
{
  int regno;
  long value;
  enum operand op_type;

  /* Get the register number.  */
  regno = (*insn >> operand->shift) & ((1 << operand->bits) - 1);

  /* Is it a constant marker?  */
  if (regno == ARC_REG_SHIMM)
    {
      op_type = OP_SHIMM;
      /* Always return zero if dest is a shimm  mlm.  */

      if ('a' != operand->fmt)
	{
	  value = *insn & 511;
	  if ((operand->flags & ARC_OPERAND_SIGNED)
	      && (value & 256))
	    value -= 512;
	  if (!flagshimm_handled_p)
	    flag_p = 0;
	  flagshimm_handled_p = 1;
	}
      else
	value = 0;
    }
  else if (regno == ARC_REG_SHIMM_UPDATE)
    {
      op_type = OP_SHIMM;

      /* Always return zero if dest is a shimm  mlm.  */
      if ('a' != operand->fmt)
	{
	  value = *insn & 511;
	  if ((operand->flags & ARC_OPERAND_SIGNED) && (value & 256))
	    value -= 512;
	}
      else
	value = 0;

      flag_p = 1;
      flagshimm_handled_p = 1;
    }
  else if (regno == ARC_REG_LIMM)
    {
      op_type = OP_LIMM;
      value = insn[1];
      limm_p = 1;

      /* If this is a jump instruction (j,jl), show new pc correctly.  */
      if (0x07 == ((*insn & I(-1)) >> 27))
	value = (value & 0xffffff);
    }

  /* It's a register, set OPVAL (that's the only way we distinguish registers
     from constants here).  */
  else
    {
      const struct arc_operand_value *reg = lookup_register (REG, regno);

      op_type = OP_REG;

      if (reg == NULL)
	abort ();
      if (opval != NULL)
	*opval = reg;
      value = regno;
    }

  /* If this field takes an auxiliary register, see if it's a known one.  */
  if ((mods & ARC_MOD_AUXREG)
      && ARC_REG_CONSTANT_P (regno))
    {
      const struct arc_operand_value *reg = lookup_register (AUXREG, value);

      /* This is really a constant, but tell the caller it has a special
	 name.  */
      if (reg != NULL && opval != NULL)
	*opval = reg;
    }

  switch(operand->fmt)
    {
    case 'a':
      ls_operand[LS_DEST] = op_type;
      break;
    case 's':
      ls_operand[LS_BASE] = op_type;
      break;
    case 'c':
      if ((insn[0]& I(-1)) == I(2))
	ls_operand[LS_VALUE] = op_type;
      else
	ls_operand[LS_OFFSET] = op_type;
      break;
    case 'o': case 'O':
      ls_operand[LS_OFFSET] = op_type;
      break;
    }

  return value;
}

/* Return the value of the "flag update" field for shimm insns.
   This value is actually stored in the register field.  */

static long
extract_flag (arc_insn *insn,
	      const struct arc_operand *operand,
	      int mods ATTRIBUTE_UNUSED,
	      const struct arc_operand_value **opval,
	      int *invalid ATTRIBUTE_UNUSED)
{
  int f;
  const struct arc_operand_value *val;

  if (flagshimm_handled_p)
    f = flag_p != 0;
  else
    f = (*insn & (1 << operand->shift)) != 0;

  /* There is no text for zero values.  */
  if (f == 0)
    return 0;
  flag_p = 1;
  val = arc_opcode_lookup_suffix (operand, 1);
  if (opval != NULL && val != NULL)
    *opval = val;
  return val->value;
}

/* Extract the condition code (if it exists).
   If we've seen a shimm value in this insn (meaning that the insn can't have
   a condition code field), then we don't store anything in OPVAL and return
   zero.  */

static long
extract_cond (arc_insn *insn,
	      const struct arc_operand *operand,
	      int mods ATTRIBUTE_UNUSED,
	      const struct arc_operand_value **opval,
	      int *invalid ATTRIBUTE_UNUSED)
{
  long cond;
  const struct arc_operand_value *val;

  if (flagshimm_handled_p)
    return 0;

  cond = (*insn >> operand->shift) & ((1 << operand->bits) - 1);
  val = arc_opcode_lookup_suffix (operand, cond);

  /* Ignore NULL values of `val'.  Several condition code values are
     reserved for extensions.  */
  if (opval != NULL && val != NULL)
    *opval = val;
  return cond;
}

/* Extract a branch address.
   We return the value as a real address (not right shifted by 2).  */

static long
extract_reladdr (arc_insn *insn,
		 const struct arc_operand *operand,
		 int mods ATTRIBUTE_UNUSED,
		 const struct arc_operand_value **opval ATTRIBUTE_UNUSED,
		 int *invalid ATTRIBUTE_UNUSED)
{
  long addr;

  addr = (*insn >> operand->shift) & ((1 << operand->bits) - 1);
  if ((operand->flags & ARC_OPERAND_SIGNED)
      && (addr & (1 << (operand->bits - 1))))
    addr -= 1 << operand->bits;
  return addr << 2;
}

/* Extract the flags bits from a j or jl long immediate.  */

static long
extract_jumpflags (arc_insn *insn,
		   const struct arc_operand *operand,
		   int mods ATTRIBUTE_UNUSED,
		   const struct arc_operand_value **opval ATTRIBUTE_UNUSED,
		   int *invalid)
{
  if (!flag_p || !limm_p)
    *invalid = 1;
  return ((flag_p && limm_p)
	  ? (insn[1] >> operand->shift) & ((1 << operand->bits) -1): 0);
}

/* Extract st insn's offset.  */

static long
extract_st_offset (arc_insn *insn,
		   const struct arc_operand *operand,
		   int mods ATTRIBUTE_UNUSED,
		   const struct arc_operand_value **opval ATTRIBUTE_UNUSED,
		   int *invalid)
{
  int value = 0;

  if (ls_operand[LS_VALUE] != OP_SHIMM || ls_operand[LS_BASE] != OP_LIMM)
    {
      value = insn[0] & 511;
      if ((operand->flags & ARC_OPERAND_SIGNED) && (value & 256))
	value -= 512;
      if (value)
	ls_operand[LS_OFFSET] = OP_SHIMM;
    }
  else
    *invalid = 1;

  return value;
}

/* Extract ld insn's offset.  */

static long
extract_ld_offset (arc_insn *insn,
		   const struct arc_operand *operand,
		   int mods,
		   const struct arc_operand_value **opval,
		   int *invalid)
{
  int test = insn[0] & I(-1);
  int value;

  if (test)
    {
      value = insn[0] & 511;
      if ((operand->flags & ARC_OPERAND_SIGNED) && (value & 256))
	value -= 512;
      if (value)
	ls_operand[LS_OFFSET] = OP_SHIMM;

      return value;
    }
  /* If it isn't in the insn, it's concealed behind reg 'c'.  */
  return extract_reg (insn, &arc_operands[arc_operand_map['c']],
		      mods, opval, invalid);
}

/* The only thing this does is set the `invalid' flag if B != C.
   This is needed because the "mov" macro appears before it's real insn "and"
   and we don't want the disassembler to confuse them.  */

static long
extract_unopmacro (arc_insn *insn,
		   const struct arc_operand *operand ATTRIBUTE_UNUSED,
		   int mods ATTRIBUTE_UNUSED,
		   const struct arc_operand_value **opval ATTRIBUTE_UNUSED,
		   int *invalid)
{
  /* This misses the case where B == ARC_REG_SHIMM_UPDATE &&
     C == ARC_REG_SHIMM (or vice versa).  No big deal.  Those insns will get
     printed as "and"s.  */
  if (((*insn >> ARC_SHIFT_REGB) & ARC_MASK_REG)
      != ((*insn >> ARC_SHIFT_REGC) & ARC_MASK_REG))
    if (invalid != NULL)
      *invalid = 1;
  return 0;
}

/* ARC instructions.

   Longer versions of insns must appear before shorter ones (if gas sees
   "lsr r2,r3,1" when it's parsing "lsr %a,%b" it will think the ",1" is
   junk).  This isn't necessary for `ld' because of the trailing ']'.

   Instructions that are really macros based on other insns must appear
   before the real insn so they're chosen when disassembling.  Eg: The `mov'
   insn is really the `and' insn.  */

struct arc_opcode arc_opcodes[] =
{
  /* Base case instruction set (core versions 5-8).  */

  /* "mov" is really an "and".  */
  { "mov%.q%.f %a,%b%F%S%L%U", I(-1), I(12), ARC_MACH_5, 0, 0 },
  /* "asl" is really an "add".  */
  { "asl%.q%.f %a,%b%F%S%L%U", I(-1), I(8), ARC_MACH_5, 0, 0 },
  /* "lsl" is really an "add".  */
  { "lsl%.q%.f %a,%b%F%S%L%U", I(-1), I(8), ARC_MACH_5, 0, 0 },
  /* "nop" is really an "xor".  */
  { "nop", 0x7fffffff, 0x7fffffff, ARC_MACH_5, 0, 0 },
  /* "rlc" is really an "adc".  */
  { "rlc%.q%.f %a,%b%F%S%L%U", I(-1), I(9), ARC_MACH_5, 0, 0 },
  { "adc%.q%.f %a,%b,%c%F%S%L", I(-1), I(9), ARC_MACH_5, 0, 0 },
  { "add%.q%.f %a,%b,%c%F%S%L", I(-1), I(8), ARC_MACH_5, 0, 0 },
  { "and%.q%.f %a,%b,%c%F%S%L", I(-1), I(12), ARC_MACH_5, 0, 0 },
  { "asr%.q%.f %a,%b%F%S%L", I(-1)|C(-1), I(3)|C(1), ARC_MACH_5, 0, 0 },
  { "bic%.q%.f %a,%b,%c%F%S%L",	I(-1), I(14), ARC_MACH_5, 0, 0 },
  { "b%q%.n %B", I(-1), I(4), ARC_MACH_5 | ARC_OPCODE_COND_BRANCH, 0, 0 },
  { "bl%q%.n %B", I(-1), I(5), ARC_MACH_5 | ARC_OPCODE_COND_BRANCH, 0, 0 },
  { "extb%.q%.f %a,%b%F%S%L", I(-1)|C(-1), I(3)|C(7), ARC_MACH_5, 0, 0 },
  { "extw%.q%.f %a,%b%F%S%L", I(-1)|C(-1), I(3)|C(8), ARC_MACH_5, 0, 0 },
  { "flag%.q %b%G%S%L", I(-1)|A(-1)|C(-1), I(3)|A(ARC_REG_SHIMM_UPDATE)|C(0), ARC_MACH_5, 0, 0 },
  { "brk", 0x1ffffe00, 0x1ffffe00, ARC_MACH_7, 0, 0 },
  { "sleep", 0x1ffffe01, 0x1ffffe01, ARC_MACH_7, 0, 0 },
  { "swi", 0x1ffffe02, 0x1ffffe02, ARC_MACH_8, 0, 0 },
  /* %Q: force cond_p=1 -> no shimm values. This insn allows an
     optional flags spec.  */
  { "j%q%Q%.n%.f %b%F%J,%j", I(-1)|A(-1)|C(-1)|R(-1,7,1), I(7)|A(0)|C(0)|R(0,7,1), ARC_MACH_5 | ARC_OPCODE_COND_BRANCH, 0, 0 },
  { "j%q%Q%.n%.f %b%F%J", I(-1)|A(-1)|C(-1)|R(-1,7,1), I(7)|A(0)|C(0)|R(0,7,1), ARC_MACH_5 | ARC_OPCODE_COND_BRANCH, 0, 0 },
  /* This insn allows an optional flags spec.  */
  { "jl%q%Q%.n%.f %b%F%J,%j", I(-1)|A(-1)|C(-1)|R(-1,7,1)|R(-1,9,1), I(7)|A(0)|C(0)|R(0,7,1)|R(1,9,1), ARC_MACH_6 | ARC_OPCODE_COND_BRANCH, 0, 0 },
  { "jl%q%Q%.n%.f %b%F%J", I(-1)|A(-1)|C(-1)|R(-1,7,1)|R(-1,9,1), I(7)|A(0)|C(0)|R(0,7,1)|R(1,9,1), ARC_MACH_6 | ARC_OPCODE_COND_BRANCH, 0, 0 },
  /* Put opcode 1 ld insns first so shimm gets prefered over limm.
     "[%b]" is before "[%b,%o]" so 0 offsets don't get printed.  */
  { "ld%Z%.X%.W%.E %a,[%s]%S%L%1", I(-1)|R(-1,13,1)|R(-1,0,511), I(1)|R(0,13,1)|R(0,0,511), ARC_MACH_5, 0, 0 },
  { "ld%z%.x%.w%.e %a,[%s]%S%L%1", I(-1)|R(-1,4,1)|R(-1,6,7), I(0)|R(0,4,1)|R(0,6,7), ARC_MACH_5, 0, 0 },
  { "ld%z%.x%.w%.e %a,[%s,%O]%S%L%1", I(-1)|R(-1,4,1)|R(-1,6,7), I(0)|R(0,4,1)|R(0,6,7), ARC_MACH_5, 0, 0 },
  { "ld%Z%.X%.W%.E %a,[%s,%O]%S%L%3", I(-1)|R(-1,13,1),	I(1)|R(0,13,1), ARC_MACH_5, 0, 0 },
  { "lp%q%.n %B", I(-1), I(6), ARC_MACH_5, 0, 0 },
  { "lr %a,[%Ab]%S%L", I(-1)|C(-1), I(1)|C(0x10), ARC_MACH_5, 0, 0 },
  { "lsr%.q%.f %a,%b%F%S%L", I(-1)|C(-1), I(3)|C(2), ARC_MACH_5, 0, 0 },
  { "or%.q%.f %a,%b,%c%F%S%L", I(-1), I(13), ARC_MACH_5, 0, 0 },
  { "ror%.q%.f %a,%b%F%S%L", I(-1)|C(-1), I(3)|C(3), ARC_MACH_5, 0, 0 },
  { "rrc%.q%.f %a,%b%F%S%L", I(-1)|C(-1), I(3)|C(4), ARC_MACH_5, 0, 0 },
  { "sbc%.q%.f %a,%b,%c%F%S%L",	I(-1), I(11), ARC_MACH_5, 0, 0 },
  { "sexb%.q%.f %a,%b%F%S%L", I(-1)|C(-1), I(3)|C(5), ARC_MACH_5, 0, 0 },
  { "sexw%.q%.f %a,%b%F%S%L", I(-1)|C(-1), I(3)|C(6), ARC_MACH_5, 0, 0 },
  { "sr %c,[%Ab]%S%L", I(-1)|A(-1), I(2)|A(0x10), ARC_MACH_5, 0, 0 },
  /* "[%b]" is before "[%b,%o]" so 0 offsets don't get printed.  */
  { "st%y%.v%.D %c,[%s]%L%S%0", I(-1)|R(-1,25,1)|R(-1,21,1), I(2)|R(0,25,1)|R(0,21,1), ARC_MACH_5, 0, 0 },
  { "st%y%.v%.D %c,[%s,%o]%S%L%2", I(-1)|R(-1,25,1)|R(-1,21,1), I(2)|R(0,25,1)|R(0,21,1), ARC_MACH_5, 0, 0 },
  { "sub%.q%.f %a,%b,%c%F%S%L",	I(-1), I(10), ARC_MACH_5, 0, 0 },
  { "xor%.q%.f %a,%b,%c%F%S%L",	I(-1), I(15), ARC_MACH_5, 0, 0 }
};

const int arc_opcodes_count = sizeof (arc_opcodes) / sizeof (arc_opcodes[0]);

const struct arc_operand_value arc_reg_names[] =
{
  /* Core register set r0-r63.  */

  /* r0-r28 - general purpose registers.  */
  { "r0", 0, REG, 0 }, { "r1", 1, REG, 0 }, { "r2", 2, REG, 0 },
  { "r3", 3, REG, 0 }, { "r4", 4, REG, 0 }, { "r5", 5, REG, 0 },
  { "r6", 6, REG, 0 }, { "r7", 7, REG, 0 }, { "r8", 8, REG, 0 },
  { "r9", 9, REG, 0 }, { "r10", 10, REG, 0 }, { "r11", 11, REG, 0 },
  { "r12", 12, REG, 0 }, { "r13", 13, REG, 0 }, { "r14", 14, REG, 0 },
  { "r15", 15, REG, 0 }, { "r16", 16, REG, 0 }, { "r17", 17, REG, 0 },
  { "r18", 18, REG, 0 }, { "r19", 19, REG, 0 }, { "r20", 20, REG, 0 },
  { "r21", 21, REG, 0 }, { "r22", 22, REG, 0 }, { "r23", 23, REG, 0 },
  { "r24", 24, REG, 0 }, { "r25", 25, REG, 0 }, { "r26", 26, REG, 0 },
  { "r27", 27, REG, 0 }, { "r28", 28, REG, 0 },
  /* Maskable interrupt link register.  */
  { "ilink1", 29, REG, 0 },
  /* Maskable interrupt link register.  */
  { "ilink2", 30, REG, 0 },
  /* Branch-link register.  */
  { "blink", 31, REG, 0 },

  /* r32-r59 reserved for extensions.  */
  { "r32", 32, REG, 0 }, { "r33", 33, REG, 0 }, { "r34", 34, REG, 0 },
  { "r35", 35, REG, 0 }, { "r36", 36, REG, 0 }, { "r37", 37, REG, 0 },
  { "r38", 38, REG, 0 }, { "r39", 39, REG, 0 }, { "r40", 40, REG, 0 },
  { "r41", 41, REG, 0 }, { "r42", 42, REG, 0 }, { "r43", 43, REG, 0 },
  { "r44", 44, REG, 0 }, { "r45", 45, REG, 0 }, { "r46", 46, REG, 0 },
  { "r47", 47, REG, 0 }, { "r48", 48, REG, 0 }, { "r49", 49, REG, 0 },
  { "r50", 50, REG, 0 }, { "r51", 51, REG, 0 }, { "r52", 52, REG, 0 },
  { "r53", 53, REG, 0 }, { "r54", 54, REG, 0 }, { "r55", 55, REG, 0 },
  { "r56", 56, REG, 0 }, { "r57", 57, REG, 0 }, { "r58", 58, REG, 0 },
  { "r59", 59, REG, 0 },

  /* Loop count register (24 bits).  */
  { "lp_count", 60, REG, 0 },
  /* Short immediate data indicator setting flags.  */
  { "r61", 61, REG, ARC_REGISTER_READONLY },
  /* Long immediate data indicator setting flags.  */
  { "r62", 62, REG, ARC_REGISTER_READONLY },
  /* Short immediate data indicator not setting flags.  */
  { "r63", 63, REG, ARC_REGISTER_READONLY },

  /* Small-data base register.  */
  { "gp", 26, REG, 0 },
  /* Frame pointer.  */
  { "fp", 27, REG, 0 },
  /* Stack pointer.  */
  { "sp", 28, REG, 0 },

  { "r29", 29, REG, 0 },
  { "r30", 30, REG, 0 },
  { "r31", 31, REG, 0 },
  { "r60", 60, REG, 0 },

  /* Auxiliary register set.  */

  /* Auxiliary register address map:
     0xffffffff-0xffffff00 (-1..-256) - customer shimm allocation
     0xfffffeff-0x80000000 - customer limm allocation
     0x7fffffff-0x00000100 - ARC limm allocation
     0x000000ff-0x00000000 - ARC shimm allocation  */

  /* Base case auxiliary registers (shimm address).  */
  { "status",         0x00, AUXREG, 0 },
  { "semaphore",      0x01, AUXREG, 0 },
  { "lp_start",       0x02, AUXREG, 0 },
  { "lp_end",         0x03, AUXREG, 0 },
  { "identity",       0x04, AUXREG, ARC_REGISTER_READONLY },
  { "debug",          0x05, AUXREG, 0 },
};

const int arc_reg_names_count =
  sizeof (arc_reg_names) / sizeof (arc_reg_names[0]);

/* The suffix table.
   Operands with the same name must be stored together.  */

const struct arc_operand_value arc_suffixes[] =
{
  /* Entry 0 is special, default values aren't printed by the disassembler.  */
  { "", 0, -1, 0 },

  /* Base case condition codes.  */
  { "al", 0, COND, 0 },
  { "ra", 0, COND, 0 },
  { "eq", 1, COND, 0 },
  { "z", 1, COND, 0 },
  { "ne", 2, COND, 0 },
  { "nz", 2, COND, 0 },
  { "pl", 3, COND, 0 },
  { "p", 3, COND, 0 },
  { "mi", 4, COND, 0 },
  { "n", 4, COND, 0 },
  { "cs", 5, COND, 0 },
  { "c", 5, COND, 0 },
  { "lo", 5, COND, 0 },
  { "cc", 6, COND, 0 },
  { "nc", 6, COND, 0 },
  { "hs", 6, COND, 0 },
  { "vs", 7, COND, 0 },
  { "v", 7, COND, 0 },
  { "vc", 8, COND, 0 },
  { "nv", 8, COND, 0 },
  { "gt", 9, COND, 0 },
  { "ge", 10, COND, 0 },
  { "lt", 11, COND, 0 },
  { "le", 12, COND, 0 },
  { "hi", 13, COND, 0 },
  { "ls", 14, COND, 0 },
  { "pnz", 15, COND, 0 },

  /* Condition codes 16-31 reserved for extensions.  */

  { "f", 1, FLAG, 0 },

  { "nd", ARC_DELAY_NONE, DELAY, 0 },
  { "d", ARC_DELAY_NORMAL, DELAY, 0 },
  { "jd", ARC_DELAY_JUMP, DELAY, 0 },

  { "b", 1, SIZE1, 0 },
  { "b", 1, SIZE10, 0 },
  { "b", 1, SIZE22, 0 },
  { "w", 2, SIZE1, 0 },
  { "w", 2, SIZE10, 0 },
  { "w", 2, SIZE22, 0 },
  { "x", 1, SIGN0, 0 },
  { "x", 1, SIGN9, 0 },
  { "a", 1, ADDRESS3, 0 },
  { "a", 1, ADDRESS12, 0 },
  { "a", 1, ADDRESS24, 0 },

  { "di", 1, CACHEBYPASS5, 0 },
  { "di", 1, CACHEBYPASS14, 0 },
  { "di", 1, CACHEBYPASS26, 0 },
};

const int arc_suffixes_count =
  sizeof (arc_suffixes) / sizeof (arc_suffixes[0]);

/* Indexed by first letter of opcode.  Points to chain of opcodes with same
   first letter.  */
static struct arc_opcode *opcode_map[26 + 1];

/* Indexed by insn code.  Points to chain of opcodes with same insn code.  */
static struct arc_opcode *icode_map[32];

/* Configuration flags.  */

/* Various ARC_HAVE_XXX bits.  */
static int cpu_type;

/* Translate a bfd_mach_arc_xxx value to a ARC_MACH_XXX value.  */

int
arc_get_opcode_mach (int bfd_mach, int big_p)
{
  static int mach_type_map[] =
  {
    ARC_MACH_5,
    ARC_MACH_6,
    ARC_MACH_7,
    ARC_MACH_8
  };
  return mach_type_map[bfd_mach - bfd_mach_arc_5] | (big_p ? ARC_MACH_BIG : 0);
}

/* Initialize any tables that need it.
   Must be called once at start up (or when first needed).

   FLAGS is a set of bits that say what version of the cpu we have,
   and in particular at least (one of) ARC_MACH_XXX.  */

void
arc_opcode_init_tables (int flags)
{
  static int init_p = 0;

  cpu_type = flags;

  /* We may be intentionally called more than once (for example gdb will call
     us each time the user switches cpu).  These tables only need to be init'd
     once though.  */
  if (!init_p)
    {
      int i,n;

      memset (arc_operand_map, 0, sizeof (arc_operand_map));
      n = sizeof (arc_operands) / sizeof (arc_operands[0]);
      for (i = 0; i < n; ++i)
	arc_operand_map[arc_operands[i].fmt] = i;

      memset (opcode_map, 0, sizeof (opcode_map));
      memset (icode_map, 0, sizeof (icode_map));
      /* Scan the table backwards so macros appear at the front.  */
      for (i = arc_opcodes_count - 1; i >= 0; --i)
	{
	  int opcode_hash = ARC_HASH_OPCODE (arc_opcodes[i].syntax);
	  int icode_hash = ARC_HASH_ICODE (arc_opcodes[i].value);

	  arc_opcodes[i].next_asm = opcode_map[opcode_hash];
	  opcode_map[opcode_hash] = &arc_opcodes[i];

	  arc_opcodes[i].next_dis = icode_map[icode_hash];
	  icode_map[icode_hash] = &arc_opcodes[i];
	}

      init_p = 1;
    }
}

/* Return non-zero if OPCODE is supported on the specified cpu.
   Cpu selection is made when calling `arc_opcode_init_tables'.  */

int
arc_opcode_supported (const struct arc_opcode *opcode)
{
  if (ARC_OPCODE_CPU (opcode->flags) <= cpu_type)
    return 1;
  return 0;
}

/* Return the first insn in the chain for assembling INSN.  */

const struct arc_opcode *
arc_opcode_lookup_asm (const char *insn)
{
  return opcode_map[ARC_HASH_OPCODE (insn)];
}

/* Return the first insn in the chain for disassembling INSN.  */

const struct arc_opcode *
arc_opcode_lookup_dis (unsigned int insn)
{
  return icode_map[ARC_HASH_ICODE (insn)];
}

/* Called by the assembler before parsing an instruction.  */

void
arc_opcode_init_insert (void)
{
  int i;

  for(i = 0; i < OPERANDS; i++)
    ls_operand[i] = OP_NONE;

  flag_p = 0;
  flagshimm_handled_p = 0;
  cond_p = 0;
  addrwb_p = 0;
  shimm_p = 0;
  limm_p = 0;
  jumpflags_p = 0;
  nullify_p = 0;
  nullify = 0; /* The default is important.  */
}

/* Called by the assembler to see if the insn has a limm operand.
   Also called by the disassembler to see if the insn contains a limm.  */

int
arc_opcode_limm_p (long *limmp)
{
  if (limmp)
    *limmp = limm;
  return limm_p;
}

/* Utility for the extraction functions to return the index into
   `arc_suffixes'.  */

const struct arc_operand_value *
arc_opcode_lookup_suffix (const struct arc_operand *type, int value)
{
  const struct arc_operand_value *v,*end;
  struct arc_ext_operand_value *ext_oper = arc_ext_operands;

  while (ext_oper)
    {
      if (type == &arc_operands[ext_oper->operand.type]
	  && value == ext_oper->operand.value)
	return (&ext_oper->operand);
      ext_oper = ext_oper->next;
    }

  /* ??? This is a little slow and can be speeded up.  */
  for (v = arc_suffixes, end = arc_suffixes + arc_suffixes_count; v < end; ++v)
    if (type == &arc_operands[v->type]
	&& value == v->value)
      return v;
  return 0;
}

int
arc_insn_is_j (arc_insn insn)
{
  return (insn & (I(-1))) == I(0x7);
}

int
arc_insn_not_jl (arc_insn insn)
{
  return ((insn & (I(-1)|A(-1)|C(-1)|R(-1,7,1)|R(-1,9,1)))
	  != (I(0x7) | R(-1,9,1)));
}

int
arc_operand_type (int opertype)
{
  switch (opertype)
    {
    case 0:
      return COND;
      break;
    case 1:
      return REG;
      break;
    case 2:
      return AUXREG;
      break;
    }
  return -1;
}

struct arc_operand_value *
get_ext_suffix (char *s)
{
  struct arc_ext_operand_value *suffix = arc_ext_operands;

  while (suffix)
    {
      if ((COND == suffix->operand.type)
	  && !strcmp(s,suffix->operand.name))
	return(&suffix->operand);
      suffix = suffix->next;
    }
  return NULL;
}

int
arc_get_noshortcut_flag (void)
{
  return ARC_REGISTER_NOSHORT_CUT;
}
