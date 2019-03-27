/* Opcode table for the ARC.
   Copyright 1994, 1995, 1997, 2001, 2002, 2003
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

   This file is part of GAS, the GNU Assembler, GDB, the GNU debugger, and
   the GNU Binutils.

   GAS/GDB is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS/GDB is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS or GDB; see the file COPYING.	If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */


/* List of the various cpu types.
   The tables currently use bit masks to say whether the instruction or
   whatever is supported by a particular cpu.  This lets us have one entry
   apply to several cpus.

   The `base' cpu must be 0. The cpu type is treated independently of
   endianness. The complete `mach' number includes endianness.
   These values are internal to opcodes/bfd/binutils/gas.  */
#define ARC_MACH_5 0
#define ARC_MACH_6 1
#define ARC_MACH_7 2
#define ARC_MACH_8 4

/* Additional cpu values can be inserted here and ARC_MACH_BIG moved down.  */
#define ARC_MACH_BIG 16

/* Mask of number of bits necessary to record cpu type.  */
#define ARC_MACH_CPU_MASK (ARC_MACH_BIG - 1)

/* Mask of number of bits necessary to record cpu type + endianness.  */
#define ARC_MACH_MASK ((ARC_MACH_BIG << 1) - 1)

/* Type to denote an ARC instruction (at least a 32 bit unsigned int).  */

typedef unsigned int arc_insn;

struct arc_opcode {
  char *syntax;              /* syntax of insn  */
  unsigned long mask, value; /* recognize insn if (op&mask) == value  */
  int flags;                 /* various flag bits  */

/* Values for `flags'.  */

/* Return CPU number, given flag bits.  */
#define ARC_OPCODE_CPU(bits) ((bits) & ARC_MACH_CPU_MASK)

/* Return MACH number, given flag bits.  */
#define ARC_OPCODE_MACH(bits) ((bits) & ARC_MACH_MASK)

/* First opcode flag bit available after machine mask.  */
#define ARC_OPCODE_FLAG_START (ARC_MACH_MASK + 1)

/* This insn is a conditional branch.  */
#define ARC_OPCODE_COND_BRANCH (ARC_OPCODE_FLAG_START)
#define SYNTAX_3OP             (ARC_OPCODE_COND_BRANCH << 1)
#define SYNTAX_LENGTH          (SYNTAX_3OP                 )
#define SYNTAX_2OP             (SYNTAX_3OP             << 1)
#define OP1_MUST_BE_IMM        (SYNTAX_2OP             << 1)
#define OP1_IMM_IMPLIED        (OP1_MUST_BE_IMM        << 1)
#define SYNTAX_VALID           (OP1_IMM_IMPLIED        << 1)

#define I(x) (((x) & 31) << 27)
#define A(x) (((x) & ARC_MASK_REG) << ARC_SHIFT_REGA)
#define B(x) (((x) & ARC_MASK_REG) << ARC_SHIFT_REGB)
#define C(x) (((x) & ARC_MASK_REG) << ARC_SHIFT_REGC)
#define R(x,b,m) (((x) & (m)) << (b)) /* value X, mask M, at bit B */

/* These values are used to optimize assembly and disassembly.  Each insn
   is on a list of related insns (same first letter for assembly, same
   insn code for disassembly).  */

  struct arc_opcode *next_asm;	/* Next instr to try during assembly.  */
  struct arc_opcode *next_dis;	/* Next instr to try during disassembly.  */

/* Macros to create the hash values for the lists.  */
#define ARC_HASH_OPCODE(string) \
  ((string)[0] >= 'a' && (string)[0] <= 'z' ? (string)[0] - 'a' : 26)
#define ARC_HASH_ICODE(insn) \
  ((unsigned int) (insn) >> 27)

 /* Macros to access `next_asm', `next_dis' so users needn't care about the
    underlying mechanism.  */
#define ARC_OPCODE_NEXT_ASM(op) ((op)->next_asm)
#define ARC_OPCODE_NEXT_DIS(op) ((op)->next_dis)
};

/* this is an "insert at front" linked list per Metaware spec
   that new definitions override older ones.  */
extern struct arc_opcode *arc_ext_opcodes;

struct arc_operand_value {
  char *name;          /* eg: "eq"  */
  short value;         /* eg: 1  */
  unsigned char type;  /* index into `arc_operands'  */
  unsigned char flags; /* various flag bits  */

/* Values for `flags'.  */

/* Return CPU number, given flag bits.  */
#define ARC_OPVAL_CPU(bits) ((bits) & ARC_MACH_CPU_MASK)
/* Return MACH number, given flag bits.  */
#define ARC_OPVAL_MACH(bits) ((bits) & ARC_MACH_MASK)
};

struct arc_ext_operand_value {
  struct arc_ext_operand_value *next;
  struct arc_operand_value operand;
};

extern struct arc_ext_operand_value *arc_ext_operands;

struct arc_operand {
/* One of the insn format chars.  */
  unsigned char fmt;

/* The number of bits in the operand (may be unused for a modifier).  */
  unsigned char bits;

/* How far the operand is left shifted in the instruction, or
   the modifier's flag bit (may be unused for a modifier.  */
  unsigned char shift;

/* Various flag bits.  */
  int flags;

/* Values for `flags'.  */

/* This operand is a suffix to the opcode.  */
#define ARC_OPERAND_SUFFIX 1

/* This operand is a relative branch displacement.  The disassembler
   prints these symbolically if possible.  */
#define ARC_OPERAND_RELATIVE_BRANCH 2

/* This operand is an absolute branch address.  The disassembler
   prints these symbolically if possible.  */
#define ARC_OPERAND_ABSOLUTE_BRANCH 4

/* This operand is an address.  The disassembler
   prints these symbolically if possible.  */
#define ARC_OPERAND_ADDRESS 8

/* This operand is a long immediate value.  */
#define ARC_OPERAND_LIMM 0x10

/* This operand takes signed values.  */
#define ARC_OPERAND_SIGNED 0x20

/* This operand takes signed values, but also accepts a full positive
   range of values.  That is, if bits is 16, it takes any value from
   -0x8000 to 0xffff.  */
#define ARC_OPERAND_SIGNOPT 0x40

/* This operand should be regarded as a negative number for the
   purposes of overflow checking (i.e., the normal most negative
   number is disallowed and one more than the normal most positive
   number is allowed).  This flag will only be set for a signed
   operand.  */
#define ARC_OPERAND_NEGATIVE 0x80

/* This operand doesn't really exist.  The program uses these operands
   in special ways.  */
#define ARC_OPERAND_FAKE 0x100

/* separate flags operand for j and jl instructions  */
#define ARC_OPERAND_JUMPFLAGS 0x200

/* allow warnings and errors to be issued after call to insert_xxxxxx  */
#define ARC_OPERAND_WARN  0x400
#define ARC_OPERAND_ERROR 0x800

/* this is a load operand */
#define ARC_OPERAND_LOAD  0x8000

/* this is a store operand */
#define ARC_OPERAND_STORE 0x10000

/* Modifier values.  */
/* A dot is required before a suffix.  Eg: .le  */
#define ARC_MOD_DOT 0x1000

/* A normal register is allowed (not used, but here for completeness).  */
#define ARC_MOD_REG 0x2000

/* An auxiliary register name is expected.  */
#define ARC_MOD_AUXREG 0x4000

/* Sum of all ARC_MOD_XXX bits.  */
#define ARC_MOD_BITS 0x7000

/* Non-zero if the operand type is really a modifier.  */
#define ARC_MOD_P(X) ((X) & ARC_MOD_BITS)

/* enforce read/write only register restrictions  */
#define ARC_REGISTER_READONLY    0x01
#define ARC_REGISTER_WRITEONLY   0x02
#define ARC_REGISTER_NOSHORT_CUT 0x04

/* Insertion function.  This is used by the assembler.  To insert an
   operand value into an instruction, check this field.

   If it is NULL, execute
   i |= (p & ((1 << o->bits) - 1)) << o->shift;
   (I is the instruction which we are filling in, O is a pointer to
   this structure, and OP is the opcode value; this assumes twos
   complement arithmetic).
   
   If this field is not NULL, then simply call it with the
   instruction and the operand value.  It will return the new value
   of the instruction.  If the ERRMSG argument is not NULL, then if
   the operand value is illegal, *ERRMSG will be set to a warning
   string (the operand will be inserted in any case).  If the
   operand value is legal, *ERRMSG will be unchanged.

   REG is non-NULL when inserting a register value.  */

  arc_insn (*insert)
    (arc_insn insn, const struct arc_operand *operand, int mods,
     const struct arc_operand_value *reg, long value, const char **errmsg);

/* Extraction function.  This is used by the disassembler.  To
   extract this operand type from an instruction, check this field.
   
   If it is NULL, compute
     op = ((i) >> o->shift) & ((1 << o->bits) - 1);
     if ((o->flags & ARC_OPERAND_SIGNED) != 0
          && (op & (1 << (o->bits - 1))) != 0)
       op -= 1 << o->bits;
   (I is the instruction, O is a pointer to this structure, and OP
   is the result; this assumes twos complement arithmetic).
   
   If this field is not NULL, then simply call it with the
   instruction value.  It will return the value of the operand.  If
   the INVALID argument is not NULL, *INVALID will be set to
   non-zero if this operand type can not actually be extracted from
   this operand (i.e., the instruction does not match).  If the
   operand is valid, *INVALID will not be changed.

   INSN is a pointer to an array of two `arc_insn's.  The first element is
   the insn, the second is the limm if present.

   Operands that have a printable form like registers and suffixes have
   their struct arc_operand_value pointer stored in OPVAL.  */

  long (*extract)
    (arc_insn *insn, const struct arc_operand *operand, int mods,
     const struct arc_operand_value **opval, int *invalid);
};

/* Bits that say what version of cpu we have. These should be passed to
   arc_init_opcode_tables. At present, all there is is the cpu type.  */

/* CPU number, given value passed to `arc_init_opcode_tables'.  */
#define ARC_HAVE_CPU(bits) ((bits) & ARC_MACH_CPU_MASK)
/* MACH number, given value passed to `arc_init_opcode_tables'.  */
#define ARC_HAVE_MACH(bits) ((bits) & ARC_MACH_MASK)

/* Special register values:  */
#define ARC_REG_SHIMM_UPDATE 61
#define ARC_REG_SHIMM 63
#define ARC_REG_LIMM 62

/* Non-zero if REG is a constant marker.  */
#define ARC_REG_CONSTANT_P(REG) ((REG) >= 61)

/* Positions and masks of various fields:  */
#define ARC_SHIFT_REGA 21
#define ARC_SHIFT_REGB 15
#define ARC_SHIFT_REGC 9
#define ARC_MASK_REG 63

/* Delay slot types.  */
#define ARC_DELAY_NONE 0   /* no delay slot */
#define ARC_DELAY_NORMAL 1 /* delay slot in both cases */
#define ARC_DELAY_JUMP 2   /* delay slot only if branch taken */

/* Non-zero if X will fit in a signed 9 bit field.  */
#define ARC_SHIMM_CONST_P(x) ((long) (x) >= -256 && (long) (x) <= 255)

extern const struct arc_operand arc_operands[];
extern const int arc_operand_count;
extern struct arc_opcode arc_opcodes[];
extern const int arc_opcodes_count;
extern const struct arc_operand_value arc_suffixes[];
extern const int arc_suffixes_count;
extern const struct arc_operand_value arc_reg_names[];
extern const int arc_reg_names_count;
extern unsigned char arc_operand_map[];

/* Utility fns in arc-opc.c.  */
int arc_get_opcode_mach (int, int);

/* `arc_opcode_init_tables' must be called before `arc_xxx_supported'.  */
void arc_opcode_init_tables (int);
void arc_opcode_init_insert (void);
void arc_opcode_init_extract (void);
const struct arc_opcode *arc_opcode_lookup_asm (const char *);
const struct arc_opcode *arc_opcode_lookup_dis (unsigned int);
int arc_opcode_limm_p (long *);
const struct arc_operand_value *arc_opcode_lookup_suffix
  (const struct arc_operand *type, int value);
int arc_opcode_supported (const struct arc_opcode *);
int arc_opval_supported (const struct arc_operand_value *);
int arc_limm_fixup_adjust (arc_insn);
int arc_insn_is_j (arc_insn);
int arc_insn_not_jl (arc_insn);
int arc_operand_type (int);
struct arc_operand_value *get_ext_suffix (char *);
int arc_get_noshortcut_flag (void);
