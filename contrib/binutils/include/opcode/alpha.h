/* alpha.h -- Header file for Alpha opcode table
   Copyright 1996, 1999, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@tamu.edu>,
   patterned after the PPC opcode table written by Ian Lance Taylor.

This file is part of GDB, GAS, and the GNU binutils.

GDB, GAS, and the GNU binutils are free software; you can redistribute
them and/or modify them under the terms of the GNU General Public
License as published by the Free Software Foundation; either version
1, or (at your option) any later version.

GDB, GAS, and the GNU binutils are distributed in the hope that they
will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this file; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef OPCODE_ALPHA_H
#define OPCODE_ALPHA_H

/* The opcode table is an array of struct alpha_opcode.  */

struct alpha_opcode
{
  /* The opcode name.  */
  const char *name;

  /* The opcode itself.  Those bits which will be filled in with
     operands are zeroes.  */
  unsigned opcode;

  /* The opcode mask.  This is used by the disassembler.  This is a
     mask containing ones indicating those bits which must match the
     opcode field, and zeroes indicating those bits which need not
     match (and are presumably filled in by operands).  */
  unsigned mask;

  /* One bit flags for the opcode.  These are primarily used to
     indicate specific processors and environments support the
     instructions.  The defined values are listed below. */
  unsigned flags;

  /* An array of operand codes.  Each code is an index into the
     operand table.  They appear in the order which the operands must
     appear in assembly code, and are terminated by a zero.  */
  unsigned char operands[4];
};

/* The table itself is sorted by major opcode number, and is otherwise
   in the order in which the disassembler should consider
   instructions.  */
extern const struct alpha_opcode alpha_opcodes[];
extern const unsigned alpha_num_opcodes;

/* Values defined for the flags field of a struct alpha_opcode.  */

/* CPU Availability */
#define AXP_OPCODE_BASE  0x0001  /* Base architecture -- all cpus.  */
#define AXP_OPCODE_EV4   0x0002  /* EV4 specific PALcode insns.  */
#define AXP_OPCODE_EV5   0x0004  /* EV5 specific PALcode insns.  */
#define AXP_OPCODE_EV6   0x0008  /* EV6 specific PALcode insns.  */
#define AXP_OPCODE_BWX   0x0100  /* Byte/word extension (amask bit 0).  */
#define AXP_OPCODE_CIX   0x0200  /* "Count" extension (amask bit 1).  */
#define AXP_OPCODE_MAX   0x0400  /* Multimedia extension (amask bit 8).  */

#define AXP_OPCODE_NOPAL (~(AXP_OPCODE_EV4|AXP_OPCODE_EV5|AXP_OPCODE_EV6))

/* A macro to extract the major opcode from an instruction.  */
#define AXP_OP(i)	(((i) >> 26) & 0x3F)

/* The total number of major opcodes. */
#define AXP_NOPS	0x40


/* The operands table is an array of struct alpha_operand.  */

struct alpha_operand
{
  /* The number of bits in the operand.  */
  unsigned int bits : 5;

  /* How far the operand is left shifted in the instruction.  */
  unsigned int shift : 5;

  /* The default relocation type for this operand.  */
  signed int default_reloc : 16;

  /* One bit syntax flags.  */
  unsigned int flags : 16;

  /* Insertion function.  This is used by the assembler.  To insert an
     operand value into an instruction, check this field.

     If it is NULL, execute
         i |= (op & ((1 << o->bits) - 1)) << o->shift;
     (i is the instruction which we are filling in, o is a pointer to
     this structure, and op is the opcode value; this assumes twos
     complement arithmetic).

     If this field is not NULL, then simply call it with the
     instruction and the operand value.  It will return the new value
     of the instruction.  If the ERRMSG argument is not NULL, then if
     the operand value is illegal, *ERRMSG will be set to a warning
     string (the operand will be inserted in any case).  If the
     operand value is legal, *ERRMSG will be unchanged (most operands
     can accept any value).  */
  unsigned (*insert) (unsigned instruction, int op, const char **errmsg);

  /* Extraction function.  This is used by the disassembler.  To
     extract this operand type from an instruction, check this field.

     If it is NULL, compute
         op = ((i) >> o->shift) & ((1 << o->bits) - 1);
	 if ((o->flags & AXP_OPERAND_SIGNED) != 0
	     && (op & (1 << (o->bits - 1))) != 0)
	   op -= 1 << o->bits;
     (i is the instruction, o is a pointer to this structure, and op
     is the result; this assumes twos complement arithmetic).

     If this field is not NULL, then simply call it with the
     instruction value.  It will return the value of the operand.  If
     the INVALID argument is not NULL, *INVALID will be set to
     non-zero if this operand type can not actually be extracted from
     this operand (i.e., the instruction does not match).  If the
     operand is valid, *INVALID will not be changed.  */
  int (*extract) (unsigned instruction, int *invalid);
};

/* Elements in the table are retrieved by indexing with values from
   the operands field of the alpha_opcodes table.  */

extern const struct alpha_operand alpha_operands[];
extern const unsigned alpha_num_operands;

/* Values defined for the flags field of a struct alpha_operand.  */

/* Mask for selecting the type for typecheck purposes */
#define AXP_OPERAND_TYPECHECK_MASK					\
  (AXP_OPERAND_PARENS | AXP_OPERAND_COMMA | AXP_OPERAND_IR |		\
   AXP_OPERAND_FPR | AXP_OPERAND_RELATIVE | AXP_OPERAND_SIGNED | 	\
   AXP_OPERAND_UNSIGNED)

/* This operand does not actually exist in the assembler input.  This
   is used to support extended mnemonics, for which two operands fields
   are identical.  The assembler should call the insert function with
   any op value.  The disassembler should call the extract function,
   ignore the return value, and check the value placed in the invalid
   argument.  */
#define AXP_OPERAND_FAKE	01

/* The operand should be wrapped in parentheses rather than separated
   from the previous by a comma.  This is used for the load and store
   instructions which want their operands to look like "Ra,disp(Rb)".  */
#define AXP_OPERAND_PARENS	02

/* Used in combination with PARENS, this supresses the supression of
   the comma.  This is used for "jmp Ra,(Rb),hint".  */
#define AXP_OPERAND_COMMA	04

/* This operand names an integer register.  */
#define AXP_OPERAND_IR		010

/* This operand names a floating point register.  */
#define AXP_OPERAND_FPR		020

/* This operand is a relative branch displacement.  The disassembler
   prints these symbolically if possible.  */
#define AXP_OPERAND_RELATIVE	040

/* This operand takes signed values.  */
#define AXP_OPERAND_SIGNED	0100

/* This operand takes unsigned values.  This exists primarily so that
   a flags value of 0 can be treated as end-of-arguments.  */
#define AXP_OPERAND_UNSIGNED	0200

/* Supress overflow detection on this field.  This is used for hints. */
#define AXP_OPERAND_NOOVERFLOW	0400

/* Mask for optional argument default value.  */
#define AXP_OPERAND_OPTIONAL_MASK 07000

/* This operand defaults to zero.  This is used for jump hints.  */
#define AXP_OPERAND_DEFAULT_ZERO 01000

/* This operand should default to the first (real) operand and is used
   in conjunction with AXP_OPERAND_OPTIONAL.  This allows
   "and $0,3,$0" to be written as "and $0,3", etc.  I don't like
   it, but it's what DEC does.  */
#define AXP_OPERAND_DEFAULT_FIRST 02000

/* Similarly, this operand should default to the second (real) operand.
   This allows "negl $0" instead of "negl $0,$0".  */
#define AXP_OPERAND_DEFAULT_SECOND 04000


/* Register common names */

#define AXP_REG_V0	0
#define AXP_REG_T0	1
#define AXP_REG_T1	2
#define AXP_REG_T2	3
#define AXP_REG_T3	4
#define AXP_REG_T4	5
#define AXP_REG_T5	6
#define AXP_REG_T6	7
#define AXP_REG_T7	8
#define AXP_REG_S0	9
#define AXP_REG_S1	10
#define AXP_REG_S2	11
#define AXP_REG_S3	12
#define AXP_REG_S4	13
#define AXP_REG_S5	14
#define AXP_REG_FP	15
#define AXP_REG_A0	16
#define AXP_REG_A1	17
#define AXP_REG_A2	18
#define AXP_REG_A3	19
#define AXP_REG_A4	20
#define AXP_REG_A5	21
#define AXP_REG_T8	22
#define AXP_REG_T9	23
#define AXP_REG_T10	24
#define AXP_REG_T11	25
#define AXP_REG_RA	26
#define AXP_REG_PV	27
#define AXP_REG_T12	27
#define AXP_REG_AT	28
#define AXP_REG_GP	29
#define AXP_REG_SP	30
#define AXP_REG_ZERO	31

#endif /* OPCODE_ALPHA_H */
