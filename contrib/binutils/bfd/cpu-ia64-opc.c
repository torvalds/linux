/* Copyright 1998, 1999, 2000, 2001, 2002, 2003, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Logically, this code should be part of libopcode but since some of
   the operand insertion/extraction functions help bfd to implement
   relocations, this code is included as part of cpu-ia64.c.  This
   avoids circular dependencies between libopcode and libbfd and also
   obviates the need for applications to link in libopcode when all
   they really want is libbfd.

   --davidm Mon Apr 13 22:14:02 1998 */

#include "../opcodes/ia64-opc.h"

#define NELEMS(a)  ((int) (sizeof (a) / sizeof ((a)[0])))

static const char*
ins_rsvd (const struct ia64_operand *self ATTRIBUTE_UNUSED,
	  ia64_insn value ATTRIBUTE_UNUSED, ia64_insn *code ATTRIBUTE_UNUSED)
{
  return "internal error---this shouldn't happen";
}

static const char*
ext_rsvd (const struct ia64_operand *self ATTRIBUTE_UNUSED,
	  ia64_insn code ATTRIBUTE_UNUSED, ia64_insn *valuep ATTRIBUTE_UNUSED)
{
  return "internal error---this shouldn't happen";
}

static const char*
ins_const (const struct ia64_operand *self ATTRIBUTE_UNUSED,
	   ia64_insn value ATTRIBUTE_UNUSED, ia64_insn *code ATTRIBUTE_UNUSED)
{
  return 0;
}

static const char*
ext_const (const struct ia64_operand *self ATTRIBUTE_UNUSED,
	   ia64_insn code ATTRIBUTE_UNUSED, ia64_insn *valuep ATTRIBUTE_UNUSED)
{
  return 0;
}

static const char*
ins_reg (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  if (value >= 1u << self->field[0].bits)
    return "register number out of range";

  *code |= value << self->field[0].shift;
  return 0;
}

static const char*
ext_reg (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  *valuep = ((code >> self->field[0].shift)
	     & ((1u << self->field[0].bits) - 1));
  return 0;
}

static const char*
ins_immu (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  ia64_insn new = 0;
  int i;

  for (i = 0; i < NELEMS (self->field) && self->field[i].bits; ++i)
    {
      new |= ((value & ((((ia64_insn) 1) << self->field[i].bits) - 1))
	      << self->field[i].shift);
      value >>= self->field[i].bits;
    }
  if (value)
    return "integer operand out of range";

  *code |= new;
  return 0;
}

static const char*
ext_immu (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  BFD_HOST_U_64_BIT value = 0;
  int i, bits = 0, total = 0;

  for (i = 0; i < NELEMS (self->field) && self->field[i].bits; ++i)
    {
      bits = self->field[i].bits;
      value |= ((code >> self->field[i].shift)
		& ((((BFD_HOST_U_64_BIT) 1) << bits) - 1)) << total;
      total += bits;
    }
  *valuep = value;
  return 0;
}

static const char*
ins_immu5b (const struct ia64_operand *self, ia64_insn value,
	    ia64_insn *code)
{
  if (value < 32 || value > 63)
    return "value must be between 32 and 63";
  return ins_immu (self, value - 32, code);
}

static const char*
ext_immu5b (const struct ia64_operand *self, ia64_insn code,
	    ia64_insn *valuep)
{
  const char *result;

  result = ext_immu (self, code, valuep);
  if (result)
    return result;

  *valuep = *valuep + 32;
  return 0;
}

static const char*
ins_immus8 (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  if (value & 0x7)
    return "value not an integer multiple of 8";
  return ins_immu (self, value >> 3, code);
}

static const char*
ext_immus8 (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  const char *result;

  result = ext_immu (self, code, valuep);
  if (result)
    return result;

  *valuep = *valuep << 3;
  return 0;
}

static const char*
ins_imms_scaled (const struct ia64_operand *self, ia64_insn value,
		 ia64_insn *code, int scale)
{
  BFD_HOST_64_BIT svalue = value, sign_bit = 0;
  ia64_insn new = 0;
  int i;

  svalue >>= scale;

  for (i = 0; i < NELEMS (self->field) && self->field[i].bits; ++i)
    {
      new |= ((svalue & ((((ia64_insn) 1) << self->field[i].bits) - 1))
	      << self->field[i].shift);
      sign_bit = (svalue >> (self->field[i].bits - 1)) & 1;
      svalue >>= self->field[i].bits;
    }
  if ((!sign_bit && svalue != 0) || (sign_bit && svalue != -1))
    return "integer operand out of range";

  *code |= new;
  return 0;
}

static const char*
ext_imms_scaled (const struct ia64_operand *self, ia64_insn code,
		 ia64_insn *valuep, int scale)
{
  int i, bits = 0, total = 0;
  BFD_HOST_64_BIT val = 0, sign;

  for (i = 0; i < NELEMS (self->field) && self->field[i].bits; ++i)
    {
      bits = self->field[i].bits;
      val |= ((code >> self->field[i].shift)
	      & ((((BFD_HOST_U_64_BIT) 1) << bits) - 1)) << total;
      total += bits;
    }
  /* sign extend: */
  sign = (BFD_HOST_64_BIT) 1 << (total - 1);
  val = (val ^ sign) - sign;

  *valuep = (val << scale);
  return 0;
}

static const char*
ins_imms (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  return ins_imms_scaled (self, value, code, 0);
}

static const char*
ins_immsu4 (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  value = ((value & 0xffffffff) ^ 0x80000000) - 0x80000000;

  return ins_imms_scaled (self, value, code, 0);
}

static const char*
ext_imms (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  return ext_imms_scaled (self, code, valuep, 0);
}

static const char*
ins_immsm1 (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  --value;
  return ins_imms_scaled (self, value, code, 0);
}

static const char*
ins_immsm1u4 (const struct ia64_operand *self, ia64_insn value,
	      ia64_insn *code)
{
  value = ((value & 0xffffffff) ^ 0x80000000) - 0x80000000;

  --value;
  return ins_imms_scaled (self, value, code, 0);
}

static const char*
ext_immsm1 (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  const char *res = ext_imms_scaled (self, code, valuep, 0);

  ++*valuep;
  return res;
}

static const char*
ins_imms1 (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  return ins_imms_scaled (self, value, code, 1);
}

static const char*
ext_imms1 (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  return ext_imms_scaled (self, code, valuep, 1);
}

static const char*
ins_imms4 (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  return ins_imms_scaled (self, value, code, 4);
}

static const char*
ext_imms4 (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  return ext_imms_scaled (self, code, valuep, 4);
}

static const char*
ins_imms16 (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  return ins_imms_scaled (self, value, code, 16);
}

static const char*
ext_imms16 (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  return ext_imms_scaled (self, code, valuep, 16);
}

static const char*
ins_cimmu (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  ia64_insn mask = (((ia64_insn) 1) << self->field[0].bits) - 1;
  return ins_immu (self, value ^ mask, code);
}

static const char*
ext_cimmu (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  const char *result;
  ia64_insn mask;

  mask = (((ia64_insn) 1) << self->field[0].bits) - 1;
  result = ext_immu (self, code, valuep);
  if (!result)
    {
      mask = (((ia64_insn) 1) << self->field[0].bits) - 1;
      *valuep ^= mask;
    }
  return result;
}

static const char*
ins_cnt (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  --value;
  if (value >= ((BFD_HOST_U_64_BIT) 1) << self->field[0].bits)
    return "count out of range";

  *code |= value << self->field[0].shift;
  return 0;
}

static const char*
ext_cnt (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  *valuep = ((code >> self->field[0].shift)
	     & ((((BFD_HOST_U_64_BIT) 1) << self->field[0].bits) - 1)) + 1;
  return 0;
}

static const char*
ins_cnt2b (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  --value;

  if (value > 2)
    return "count must be in range 1..3";

  *code |= value << self->field[0].shift;
  return 0;
}

static const char*
ext_cnt2b (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  *valuep = ((code >> self->field[0].shift) & 0x3) + 1;
  return 0;
}

static const char*
ins_cnt2c (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  switch (value)
    {
    case 0:	value = 0; break;
    case 7:	value = 1; break;
    case 15:	value = 2; break;
    case 16:	value = 3; break;
    default:	return "count must be 0, 7, 15, or 16";
    }
  *code |= value << self->field[0].shift;
  return 0;
}

static const char*
ext_cnt2c (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  ia64_insn value;

  value = (code >> self->field[0].shift) & 0x3;
  switch (value)
    {
    case 0: value =  0; break;
    case 1: value =  7; break;
    case 2: value = 15; break;
    case 3: value = 16; break;
    }
  *valuep = value;
  return 0;
}

static const char*
ins_inc3 (const struct ia64_operand *self, ia64_insn value, ia64_insn *code)
{
  BFD_HOST_64_BIT val = value;
  BFD_HOST_U_64_BIT sign = 0;

  if (val < 0)
    {
      sign = 0x4;
      value = -value;
    }
  switch (value)
    {
    case  1:	value = 3; break;
    case  4:	value = 2; break;
    case  8:	value = 1; break;
    case 16:	value = 0; break;
    default:	return "count must be +/- 1, 4, 8, or 16";
    }
  *code |= (sign | value) << self->field[0].shift;
  return 0;
}

static const char*
ext_inc3 (const struct ia64_operand *self, ia64_insn code, ia64_insn *valuep)
{
  BFD_HOST_64_BIT val;
  int negate;

  val = (code >> self->field[0].shift) & 0x7;
  negate = val & 0x4;
  switch (val & 0x3)
    {
    case 0: val = 16; break;
    case 1: val =  8; break;
    case 2: val =  4; break;
    case 3: val =  1; break;
    }
  if (negate)
    val = -val;

  *valuep = val;
  return 0;
}

#define CST	IA64_OPND_CLASS_CST
#define REG	IA64_OPND_CLASS_REG
#define IND	IA64_OPND_CLASS_IND
#define ABS	IA64_OPND_CLASS_ABS
#define REL	IA64_OPND_CLASS_REL

#define SDEC	IA64_OPND_FLAG_DECIMAL_SIGNED
#define UDEC	IA64_OPND_FLAG_DECIMAL_UNSIGNED

const struct ia64_operand elf64_ia64_operands[IA64_OPND_COUNT] =
  {
    /* constants: */
    { CST, ins_const, ext_const, "NIL",		{{ 0, 0}}, 0, "<none>" },
    { CST, ins_const, ext_const, "ar.csd",	{{ 0, 0}}, 0, "ar.csd" },
    { CST, ins_const, ext_const, "ar.ccv",	{{ 0, 0}}, 0, "ar.ccv" },
    { CST, ins_const, ext_const, "ar.pfs",	{{ 0, 0}}, 0, "ar.pfs" },
    { CST, ins_const, ext_const, "1",		{{ 0, 0}}, 0, "1" },
    { CST, ins_const, ext_const, "8",		{{ 0, 0}}, 0, "8" },
    { CST, ins_const, ext_const, "16",		{{ 0, 0}}, 0, "16" },
    { CST, ins_const, ext_const, "r0",		{{ 0, 0}}, 0, "r0" },
    { CST, ins_const, ext_const, "ip",		{{ 0, 0}}, 0, "ip" },
    { CST, ins_const, ext_const, "pr",		{{ 0, 0}}, 0, "pr" },
    { CST, ins_const, ext_const, "pr.rot",	{{ 0, 0}}, 0, "pr.rot" },
    { CST, ins_const, ext_const, "psr",		{{ 0, 0}}, 0, "psr" },
    { CST, ins_const, ext_const, "psr.l",	{{ 0, 0}}, 0, "psr.l" },
    { CST, ins_const, ext_const, "psr.um",	{{ 0, 0}}, 0, "psr.um" },

    /* register operands: */
    { REG, ins_reg,   ext_reg,	"ar", {{ 7, 20}}, 0,		/* AR3 */
      "an application register" },
    { REG, ins_reg,   ext_reg,	 "b", {{ 3,  6}}, 0,		/* B1 */
      "a branch register" },
    { REG, ins_reg,   ext_reg,	 "b", {{ 3, 13}}, 0,		/* B2 */
      "a branch register"},
    { REG, ins_reg,   ext_reg,	"cr", {{ 7, 20}}, 0,		/* CR */
      "a control register"},
    { REG, ins_reg,   ext_reg,	 "f", {{ 7,  6}}, 0,		/* F1 */
      "a floating-point register" },
    { REG, ins_reg,   ext_reg,	 "f", {{ 7, 13}}, 0,		/* F2 */
      "a floating-point register" },
    { REG, ins_reg,   ext_reg,	 "f", {{ 7, 20}}, 0,		/* F3 */
      "a floating-point register" },
    { REG, ins_reg,   ext_reg,	 "f", {{ 7, 27}}, 0,		/* F4 */
      "a floating-point register" },
    { REG, ins_reg,   ext_reg,	 "p", {{ 6,  6}}, 0,		/* P1 */
      "a predicate register" },
    { REG, ins_reg,   ext_reg,	 "p", {{ 6, 27}}, 0,		/* P2 */
      "a predicate register" },
    { REG, ins_reg,   ext_reg,	 "r", {{ 7,  6}}, 0,		/* R1 */
      "a general register" },
    { REG, ins_reg,   ext_reg,	 "r", {{ 7, 13}}, 0,		/* R2 */
      "a general register" },
    { REG, ins_reg,   ext_reg,	 "r", {{ 7, 20}}, 0,		/* R3 */
      "a general register" },
    { REG, ins_reg,   ext_reg,	 "r", {{ 2, 20}}, 0,		/* R3_2 */
      "a general register r0-r3" },

    /* memory operands: */
    { IND, ins_reg,   ext_reg,	"",      {{7, 20}}, 0,		/* MR3 */
      "a memory address" },

    /* indirect operands: */
    { IND, ins_reg,   ext_reg,	"cpuid", {{7, 20}}, 0,		/* CPUID_R3 */
      "a cpuid register" },
    { IND, ins_reg,   ext_reg,	"dbr",   {{7, 20}}, 0,		/* DBR_R3 */
      "a dbr register" },
    { IND, ins_reg,   ext_reg,	"dtr",   {{7, 20}}, 0,		/* DTR_R3 */
      "a dtr register" },
    { IND, ins_reg,   ext_reg,	"itr",   {{7, 20}}, 0,		/* ITR_R3 */
      "an itr register" },
    { IND, ins_reg,   ext_reg,	"ibr",   {{7, 20}}, 0,		/* IBR_R3 */
      "an ibr register" },
    { IND, ins_reg,   ext_reg,	"msr",   {{7, 20}}, 0,		/* MSR_R3 */
      "an msr register" },
    { IND, ins_reg,   ext_reg,	"pkr",   {{7, 20}}, 0,		/* PKR_R3 */
      "a pkr register" },
    { IND, ins_reg,   ext_reg,	"pmc",   {{7, 20}}, 0,		/* PMC_R3 */
      "a pmc register" },
    { IND, ins_reg,   ext_reg,	"pmd",   {{7, 20}}, 0,		/* PMD_R3 */
      "a pmd register" },
    { IND, ins_reg,   ext_reg,	"rr",    {{7, 20}}, 0,		/* RR_R3 */
      "an rr register" },

    /* immediate operands: */
    { ABS, ins_cimmu, ext_cimmu, 0, {{ 5, 20 }}, UDEC,		/* CCNT5 */
      "a 5-bit count (0-31)" },
    { ABS, ins_cnt,   ext_cnt,   0, {{ 2, 27 }}, UDEC,		/* CNT2a */
      "a 2-bit count (1-4)" },
    { ABS, ins_cnt2b, ext_cnt2b, 0, {{ 2, 27 }}, UDEC,		/* CNT2b */
      "a 2-bit count (1-3)" },
    { ABS, ins_cnt2c, ext_cnt2c, 0, {{ 2, 30 }}, UDEC,		/* CNT2c */
      "a count (0, 7, 15, or 16)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 5, 14}}, UDEC,		/* CNT5 */
      "a 5-bit count (0-31)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 6, 27}}, UDEC,		/* CNT6 */
      "a 6-bit count (0-63)" },
    { ABS, ins_cimmu, ext_cimmu, 0, {{ 6, 20}}, UDEC,		/* CPOS6a */
      "a 6-bit bit pos (0-63)" },
    { ABS, ins_cimmu, ext_cimmu, 0, {{ 6, 14}}, UDEC,		/* CPOS6b */
      "a 6-bit bit pos (0-63)" },
    { ABS, ins_cimmu, ext_cimmu, 0, {{ 6, 31}}, UDEC,		/* CPOS6c */
      "a 6-bit bit pos (0-63)" },
    { ABS, ins_imms,  ext_imms,  0, {{ 1, 36}}, SDEC,		/* IMM1 */
      "a 1-bit integer (-1, 0)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 2, 13}}, UDEC,		/* IMMU2 */
      "a 2-bit unsigned (0-3)" },
    { ABS, ins_immu5b,  ext_immu5b,  0, {{ 5, 14}}, UDEC,	/* IMMU5b */
      "a 5-bit unsigned (32 + (0-31))" },
    { ABS, ins_immu,  ext_immu,  0, {{ 7, 13}}, 0,		/* IMMU7a */
      "a 7-bit unsigned (0-127)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 7, 20}}, 0,		/* IMMU7b */
      "a 7-bit unsigned (0-127)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 7, 13}}, UDEC,		/* SOF */
      "a frame size (register count)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 7, 20}}, UDEC,		/* SOL */
      "a local register count" },
    { ABS, ins_immus8,ext_immus8,0, {{ 4, 27}}, UDEC,		/* SOR */
      "a rotating register count (integer multiple of 8)" },
    { ABS, ins_imms,  ext_imms,  0,				/* IMM8 */
      {{ 7, 13}, { 1, 36}}, SDEC,
      "an 8-bit integer (-128-127)" },
    { ABS, ins_immsu4,  ext_imms,  0,				/* IMM8U4 */
      {{ 7, 13}, { 1, 36}}, SDEC,
      "an 8-bit signed integer for 32-bit unsigned compare (-128-127)" },
    { ABS, ins_immsm1,  ext_immsm1,  0,				/* IMM8M1 */
      {{ 7, 13}, { 1, 36}}, SDEC,
      "an 8-bit integer (-127-128)" },
    { ABS, ins_immsm1u4,  ext_immsm1,  0,			/* IMM8M1U4 */
      {{ 7, 13}, { 1, 36}}, SDEC,
      "an 8-bit integer for 32-bit unsigned compare (-127-(-1),1-128,0x100000000)" },
    { ABS, ins_immsm1,  ext_immsm1,  0,				/* IMM8M1U8 */
      {{ 7, 13}, { 1, 36}}, SDEC,
      "an 8-bit integer for 64-bit unsigned compare (-127-(-1),1-128,0x10000000000000000)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 2, 33}, { 7, 20}}, 0,	/* IMMU9 */
      "a 9-bit unsigned (0-511)" },
    { ABS, ins_imms,  ext_imms,  0,				/* IMM9a */
      {{ 7,  6}, { 1, 27}, { 1, 36}}, SDEC,
      "a 9-bit integer (-256-255)" },
    { ABS, ins_imms,  ext_imms, 0,				/* IMM9b */
      {{ 7, 13}, { 1, 27}, { 1, 36}}, SDEC,
      "a 9-bit integer (-256-255)" },
    { ABS, ins_imms,  ext_imms, 0,				/* IMM14 */
      {{ 7, 13}, { 6, 27}, { 1, 36}}, SDEC,
      "a 14-bit integer (-8192-8191)" },
    { ABS, ins_imms1, ext_imms1, 0,				/* IMM17 */
      {{ 7,  6}, { 8, 24}, { 1, 36}}, 0,
      "a 17-bit integer (-65536-65535)" },
    { ABS, ins_immu,  ext_immu,  0, {{20,  6}, { 1, 36}}, 0,	/* IMMU21 */
      "a 21-bit unsigned" },
    { ABS, ins_imms,  ext_imms,  0,				/* IMM22 */
      {{ 7, 13}, { 9, 27}, { 5, 22}, { 1, 36}}, SDEC,
      "a 22-bit signed integer" },
    { ABS, ins_immu,  ext_immu,  0,				/* IMMU24 */
      {{21,  6}, { 2, 31}, { 1, 36}}, 0,
      "a 24-bit unsigned" },
    { ABS, ins_imms16,ext_imms16,0, {{27,  6}, { 1, 36}}, 0,	/* IMM44 */
      "a 44-bit unsigned (least 16 bits ignored/zeroes)" },
    { ABS, ins_rsvd,  ext_rsvd,	0, {{0,  0}}, 0,		/* IMMU62 */
      "a 62-bit unsigned" },
    { ABS, ins_rsvd,  ext_rsvd,	0, {{0,  0}}, 0,		/* IMMU64 */
      "a 64-bit unsigned" },
    { ABS, ins_inc3,  ext_inc3,  0, {{ 3, 13}}, SDEC,		/* INC3 */
      "an increment (+/- 1, 4, 8, or 16)" },
    { ABS, ins_cnt,   ext_cnt,   0, {{ 4, 27}}, UDEC,		/* LEN4 */
      "a 4-bit length (1-16)" },
    { ABS, ins_cnt,   ext_cnt,   0, {{ 6, 27}}, UDEC,		/* LEN6 */
      "a 6-bit length (1-64)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 4, 20}},	0,		/* MBTYPE4 */
      "a mix type (@rev, @mix, @shuf, @alt, or @brcst)" },
    { ABS, ins_immu,  ext_immu,  0, {{ 8, 20}},	0,		/* MBTYPE8 */
      "an 8-bit mix type" },
    { ABS, ins_immu,  ext_immu,  0, {{ 6, 14}}, UDEC,		/* POS6 */
      "a 6-bit bit pos (0-63)" },
    { REL, ins_imms4, ext_imms4, 0, {{ 7,  6}, { 2, 33}}, 0,	/* TAG13 */
      "a branch tag" },
    { REL, ins_imms4, ext_imms4, 0, {{ 9, 24}}, 0,		/* TAG13b */
      "a branch tag" },
    { REL, ins_imms4, ext_imms4, 0, {{20,  6}, { 1, 36}}, 0,	/* TGT25 */
      "a branch target" },
    { REL, ins_imms4, ext_imms4, 0,				/* TGT25b */
      {{ 7,  6}, {13, 20}, { 1, 36}}, 0,
      "a branch target" },
    { REL, ins_imms4, ext_imms4, 0, {{20, 13}, { 1, 36}}, 0,	/* TGT25c */
      "a branch target" },
    { REL, ins_rsvd, ext_rsvd, 0, {{0, 0}}, 0,                  /* TGT64  */
      "a branch target" },

    { ABS, ins_const, ext_const, 0, {{0, 0}}, 0,		/* LDXMOV */
      "ldxmov target" },
  };
