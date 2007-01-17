/* ppc.h -- Header file for PowerPC opcode table
   Copyright 1994, 1995, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Support

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

#ifndef PPC_H
#define PPC_H

/* The opcode table is an array of struct powerpc_opcode.  */

struct powerpc_opcode
{
  /* The opcode name.  */
  const char *name;

  /* The opcode itself.  Those bits which will be filled in with
     operands are zeroes.  */
  unsigned long opcode;

  /* The opcode mask.  This is used by the disassembler.  This is a
     mask containing ones indicating those bits which must match the
     opcode field, and zeroes indicating those bits which need not
     match (and are presumably filled in by operands).  */
  unsigned long mask;

  /* One bit flags for the opcode.  These are used to indicate which
     specific processors support the instructions.  The defined values
     are listed below.  */
  unsigned long flags;

  /* An array of operand codes.  Each code is an index into the
     operand table.  They appear in the order which the operands must
     appear in assembly code, and are terminated by a zero.  */
  unsigned char operands[8];
};

/* The table itself is sorted by major opcode number, and is otherwise
   in the order in which the disassembler should consider
   instructions.  */
extern const struct powerpc_opcode powerpc_opcodes[];
extern const int powerpc_num_opcodes;

/* Values defined for the flags field of a struct powerpc_opcode.  */

/* Opcode is defined for the PowerPC architecture.  */
#define PPC_OPCODE_PPC			 1

/* Opcode is defined for the POWER (RS/6000) architecture.  */
#define PPC_OPCODE_POWER		 2

/* Opcode is defined for the POWER2 (Rios 2) architecture.  */
#define PPC_OPCODE_POWER2		 4

/* Opcode is only defined on 32 bit architectures.  */
#define PPC_OPCODE_32			 8

/* Opcode is only defined on 64 bit architectures.  */
#define PPC_OPCODE_64		      0x10

/* Opcode is supported by the Motorola PowerPC 601 processor.  The 601
   is assumed to support all PowerPC (PPC_OPCODE_PPC) instructions,
   but it also supports many additional POWER instructions.  */
#define PPC_OPCODE_601		      0x20

/* Opcode is supported in both the Power and PowerPC architectures
   (ie, compiler's -mcpu=common or assembler's -mcom).  */
#define PPC_OPCODE_COMMON	      0x40

/* Opcode is supported for any Power or PowerPC platform (this is
   for the assembler's -many option, and it eliminates duplicates).  */
#define PPC_OPCODE_ANY		      0x80

/* Opcode is supported as part of the 64-bit bridge.  */
#define PPC_OPCODE_64_BRIDGE	     0x100

/* Opcode is supported by Altivec Vector Unit */
#define PPC_OPCODE_ALTIVEC	     0x200

/* Opcode is supported by PowerPC 403 processor.  */
#define PPC_OPCODE_403		     0x400

/* Opcode is supported by PowerPC BookE processor.  */
#define PPC_OPCODE_BOOKE	     0x800

/* Opcode is only supported by 64-bit PowerPC BookE processor.  */
#define PPC_OPCODE_BOOKE64	    0x1000

/* Opcode is supported by PowerPC 440 processor.  */
#define PPC_OPCODE_440		    0x2000

/* Opcode is only supported by Power4 architecture.  */
#define PPC_OPCODE_POWER4	    0x4000

/* Opcode isn't supported by Power4 architecture.  */
#define PPC_OPCODE_NOPOWER4	    0x8000

/* Opcode is only supported by POWERPC Classic architecture.  */
#define PPC_OPCODE_CLASSIC	   0x10000

/* Opcode is only supported by e500x2 Core.  */
#define PPC_OPCODE_SPE		   0x20000

/* Opcode is supported by e500x2 Integer select APU.  */
#define PPC_OPCODE_ISEL		   0x40000

/* Opcode is an e500 SPE floating point instruction.  */
#define PPC_OPCODE_EFS		   0x80000

/* Opcode is supported by branch locking APU.  */
#define PPC_OPCODE_BRLOCK	  0x100000

/* Opcode is supported by performance monitor APU.  */
#define PPC_OPCODE_PMR		  0x200000

/* Opcode is supported by cache locking APU.  */
#define PPC_OPCODE_CACHELCK	  0x400000

/* Opcode is supported by machine check APU.  */
#define PPC_OPCODE_RFMCI	  0x800000

/* Opcode is only supported by Power5 architecture.  */
#define PPC_OPCODE_POWER5	 0x1000000

/* Opcode is supported by PowerPC e300 family.  */
#define PPC_OPCODE_E300          0x2000000

/* Opcode is only supported by Power6 architecture.  */
#define PPC_OPCODE_POWER6	 0x4000000

/* Opcode is only supported by PowerPC Cell family.  */
#define PPC_OPCODE_CELL		 0x8000000

/* A macro to extract the major opcode from an instruction.  */
#define PPC_OP(i) (((i) >> 26) & 0x3f)

/* The operands table is an array of struct powerpc_operand.  */

struct powerpc_operand
{
  /* The number of bits in the operand.  */
  int bits;

  /* How far the operand is left shifted in the instruction.  */
  int shift;

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
  unsigned long (*insert)
    (unsigned long instruction, long op, int dialect, const char **errmsg);

  /* Extraction function.  This is used by the disassembler.  To
     extract this operand type from an instruction, check this field.

     If it is NULL, compute
         op = ((i) >> o->shift) & ((1 << o->bits) - 1);
	 if ((o->flags & PPC_OPERAND_SIGNED) != 0
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
  long (*extract) (unsigned long instruction, int dialect, int *invalid);

  /* One bit syntax flags.  */
  unsigned long flags;
};

/* Elements in the table are retrieved by indexing with values from
   the operands field of the powerpc_opcodes table.  */

extern const struct powerpc_operand powerpc_operands[];

/* Values defined for the flags field of a struct powerpc_operand.  */

/* This operand takes signed values.  */
#define PPC_OPERAND_SIGNED (01)

/* This operand takes signed values, but also accepts a full positive
   range of values when running in 32 bit mode.  That is, if bits is
   16, it takes any value from -0x8000 to 0xffff.  In 64 bit mode,
   this flag is ignored.  */
#define PPC_OPERAND_SIGNOPT (02)

/* This operand does not actually exist in the assembler input.  This
   is used to support extended mnemonics such as mr, for which two
   operands fields are identical.  The assembler should call the
   insert function with any op value.  The disassembler should call
   the extract function, ignore the return value, and check the value
   placed in the valid argument.  */
#define PPC_OPERAND_FAKE (04)

/* The next operand should be wrapped in parentheses rather than
   separated from this one by a comma.  This is used for the load and
   store instructions which want their operands to look like
       reg,displacement(reg)
   */
#define PPC_OPERAND_PARENS (010)

/* This operand may use the symbolic names for the CR fields, which
   are
       lt  0	gt  1	eq  2	so  3	un  3
       cr0 0	cr1 1	cr2 2	cr3 3
       cr4 4	cr5 5	cr6 6	cr7 7
   These may be combined arithmetically, as in cr2*4+gt.  These are
   only supported on the PowerPC, not the POWER.  */
#define PPC_OPERAND_CR (020)

/* This operand names a register.  The disassembler uses this to print
   register names with a leading 'r'.  */
#define PPC_OPERAND_GPR (040)

/* Like PPC_OPERAND_GPR, but don't print a leading 'r' for r0.  */
#define PPC_OPERAND_GPR_0 (0100)

/* This operand names a floating point register.  The disassembler
   prints these with a leading 'f'.  */
#define PPC_OPERAND_FPR (0200)

/* This operand is a relative branch displacement.  The disassembler
   prints these symbolically if possible.  */
#define PPC_OPERAND_RELATIVE (0400)

/* This operand is an absolute branch address.  The disassembler
   prints these symbolically if possible.  */
#define PPC_OPERAND_ABSOLUTE (01000)

/* This operand is optional, and is zero if omitted.  This is used for
   example, in the optional BF field in the comparison instructions.  The
   assembler must count the number of operands remaining on the line,
   and the number of operands remaining for the opcode, and decide
   whether this operand is present or not.  The disassembler should
   print this operand out only if it is not zero.  */
#define PPC_OPERAND_OPTIONAL (02000)

/* This flag is only used with PPC_OPERAND_OPTIONAL.  If this operand
   is omitted, then for the next operand use this operand value plus
   1, ignoring the next operand field for the opcode.  This wretched
   hack is needed because the Power rotate instructions can take
   either 4 or 5 operands.  The disassembler should print this operand
   out regardless of the PPC_OPERAND_OPTIONAL field.  */
#define PPC_OPERAND_NEXT (04000)

/* This operand should be regarded as a negative number for the
   purposes of overflow checking (i.e., the normal most negative
   number is disallowed and one more than the normal most positive
   number is allowed).  This flag will only be set for a signed
   operand.  */
#define PPC_OPERAND_NEGATIVE (010000)

/* This operand names a vector unit register.  The disassembler
   prints these with a leading 'v'.  */
#define PPC_OPERAND_VR (020000)

/* This operand is for the DS field in a DS form instruction.  */
#define PPC_OPERAND_DS (040000)

/* This operand is for the DQ field in a DQ form instruction.  */
#define PPC_OPERAND_DQ (0100000)

/* The POWER and PowerPC assemblers use a few macros.  We keep them
   with the operands table for simplicity.  The macro table is an
   array of struct powerpc_macro.  */

struct powerpc_macro
{
  /* The macro name.  */
  const char *name;

  /* The number of operands the macro takes.  */
  unsigned int operands;

  /* One bit flags for the opcode.  These are used to indicate which
     specific processors support the instructions.  The values are the
     same as those for the struct powerpc_opcode flags field.  */
  unsigned long flags;

  /* A format string to turn the macro into a normal instruction.
     Each %N in the string is replaced with operand number N (zero
     based).  */
  const char *format;
};

extern const struct powerpc_macro powerpc_macros[];
extern const int powerpc_num_macros;

#endif /* PPC_H */
