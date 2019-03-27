/* s390.h -- Header file for S390 opcode table
   Copyright 2000, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Martin Schwidefsky (schwidefsky@de.ibm.com).

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef S390_H
#define S390_H

/* List of instruction sets variations. */

enum s390_opcode_mode_val
  {
    S390_OPCODE_ESA = 0,
    S390_OPCODE_ZARCH
  };

enum s390_opcode_cpu_val
  {
    S390_OPCODE_G5 = 0,
    S390_OPCODE_G6,
    S390_OPCODE_Z900,
    S390_OPCODE_Z990,
    S390_OPCODE_Z9_109,
    S390_OPCODE_Z9_EC
  };

/* The opcode table is an array of struct s390_opcode.  */

struct s390_opcode
  {
    /* The opcode name.  */
    const char * name;

    /* The opcode itself.  Those bits which will be filled in with
       operands are zeroes.  */
    unsigned char opcode[6];

    /* The opcode mask.  This is used by the disassembler.  This is a
       mask containing ones indicating those bits which must match the
       opcode field, and zeroes indicating those bits which need not
       match (and are presumably filled in by operands).  */
    unsigned char mask[6];

    /* The opcode length in bytes. */
    int oplen;

    /* An array of operand codes.  Each code is an index into the
       operand table.  They appear in the order which the operands must
       appear in assembly code, and are terminated by a zero.  */
    unsigned char operands[6];

    /* Bitmask of execution modes this opcode is available for.  */
    unsigned int modes;

    /* First cpu this opcode is available for.  */
    enum s390_opcode_cpu_val min_cpu;
  };

/* The table itself is sorted by major opcode number, and is otherwise
   in the order in which the disassembler should consider
   instructions.  */
extern const struct s390_opcode s390_opcodes[];
extern const int                s390_num_opcodes;

/* A opcode format table for the .insn pseudo mnemonic.  */
extern const struct s390_opcode s390_opformats[];
extern const int                s390_num_opformats;

/* Values defined for the flags field of a struct powerpc_opcode.  */

/* The operands table is an array of struct s390_operand.  */

struct s390_operand
  {
    /* The number of bits in the operand.  */
    int bits;

    /* How far the operand is left shifted in the instruction.  */
    int shift;

    /* One bit syntax flags.  */
    unsigned long flags;
  };

/* Elements in the table are retrieved by indexing with values from
   the operands field of the powerpc_opcodes table.  */

extern const struct s390_operand s390_operands[];

/* Values defined for the flags field of a struct s390_operand.  */

/* This operand names a register.  The disassembler uses this to print
   register names with a leading 'r'.  */
#define S390_OPERAND_GPR 0x1

/* This operand names a floating point register.  The disassembler
   prints these with a leading 'f'. */
#define S390_OPERAND_FPR 0x2

/* This operand names an access register.  The disassembler
   prints these with a leading 'a'.  */
#define S390_OPERAND_AR 0x4

/* This operand names a control register.  The disassembler
   prints these with a leading 'c'.  */
#define S390_OPERAND_CR 0x8

/* This operand is a displacement.  */
#define S390_OPERAND_DISP 0x10

/* This operand names a base register.  */
#define S390_OPERAND_BASE 0x20

/* This operand names an index register, it can be skipped.  */
#define S390_OPERAND_INDEX 0x40

/* This operand is a relative branch displacement.  The disassembler
   prints these symbolically if possible.  */
#define S390_OPERAND_PCREL 0x80

/* This operand takes signed values.  */
#define S390_OPERAND_SIGNED 0x100

/* This operand is a length.  */
#define S390_OPERAND_LENGTH 0x200

/* This operand is optional. Only a single operand at the end of
   the instruction may be optional.  */
#define S390_OPERAND_OPTIONAL 0x400

	#endif /* S390_H */
