/* cr16.h -- Header file for CR16 opcode and register tables.
   Copyright 2007 Free Software Foundation, Inc.
   Contributed by M R Swami Reddy

   This file is part of GAS, GDB and the GNU binutils.

   GAS, GDB, and GNU binutils is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GAS, GDB, and GNU binutils are distributed in the hope that they will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _CR16_H_
#define _CR16_H_

/* CR16 core Registers :
   The enums are used as indices to CR16 registers table (cr16_regtab).
   Therefore, order MUST be preserved.  */

typedef enum
  {
    /* 16-bit general purpose registers.  */
    r0, r1, r2, r3, 
    r4, r5, r6, r7, 
    r8, r9, r10, r11, 
    r12_L = 12, r13_L = 13, ra = 14, sp_L = 15,

    /* 32-bit general purpose registers.  */
    r12 = 12, r13 = 13, r14 = 14, r15 = 15, 
    era = 14, sp = 15, RA,

    /* Not a register.  */
    nullregister,
    MAX_REG
  }
reg;

/* CR16 processor registers and special registers :
   The enums are used as indices to CR16 processor registers table
   (cr16_pregtab). Therefore, order MUST be preserved.  */

typedef enum
  {
    /* processor registers.  */
    dbs = MAX_REG, 
    dsr, dcrl, dcrh, 
    car0l, car0h, car1l, car1h, 
    cfg, psr, intbasel, intbaseh, 
    ispl, isph, uspl, usph,
    dcr =  dcrl, 
    car0 = car0l, 
    car1 = car1l, 
    intbase = intbasel, 
    isp =  ispl, 
    usp =  uspl,
    /* Not a processor register.  */
    nullpregister = usph + 1,
    MAX_PREG
  }
preg;

/* CR16 Register types. */

typedef enum
  {
    CR16_R_REGTYPE,    /* r<N>      */
    CR16_RP_REGTYPE,   /* reg pair  */
    CR16_P_REGTYPE     /* Processor register  */
  }
reg_type;

/* CR16 argument types :
   The argument types correspond to instructions operands

   Argument types :
   r - register
   rp - register pair
   c - constant
   i - immediate
   idxr - index with register
   idxrp - index with register pair
   rbase - register base
   rpbase - register pair base
   pr - processor register */

typedef enum
  {
    arg_r,
    arg_c,
    arg_cr,
    arg_crp,
    arg_ic,
    arg_icr,
    arg_idxr,
    arg_idxrp,
    arg_rbase,
    arg_rpbase,
    arg_rp,
    arg_pr,
    arg_prp,
    arg_cc,
    arg_ra,
    /* Not an argument.  */
    nullargs
  }
argtype;

/* CR16 operand types:The operand types correspond to instructions operands.*/

typedef enum
  {
    dummy,
    /* N-bit signed immediate.  */
    imm3, imm4, imm5, imm6, imm16, imm20, imm32,
    /* N-bit unsigned immediate.  */
    uimm3, uimm3_1, uimm4, uimm4_1, uimm5, uimm16, uimm20, uimm32,
    /* N-bit signed displacement.  */
    disps5, disps17, disps25,
    /* N-bit unsigned displacement.  */
    dispe9,
    /* N-bit absolute address.  */
    abs20, abs24,
    /* Register relative.  */
    rra, rbase, rbase_disps20, rbase_dispe20,
    /* Register pair relative.  */
    rpbase_disps0, rpbase_dispe4, rpbase_disps4, rpbase_disps16,
    rpbase_disps20, rpbase_dispe20,
    /* Register index.  */
    rindex7_abs20, rindex8_abs20,
    /* Register pair index.  */
    rpindex_disps0, rpindex_disps14, rpindex_disps20,
    /* register.  */
    regr, 
    /* register pair.  */
    regp, 
    /* processor register.  */
    pregr, 
    /* processor register 32 bit.  */
    pregrp, 
    /* condition code - 4 bit.  */
    cc, 
    /* Not an operand.  */
    nulloperand,
    /* Maximum supported operand.  */
    MAX_OPRD
  }
operand_type;

/* CR16 instruction types.  */

#define NO_TYPE_INS       0
#define ARITH_INS         1
#define LD_STOR_INS       2
#define BRANCH_INS        3
#define ARITH_BYTE_INS    4
#define SHIFT_INS         5
#define BRANCH_NEQ_INS    6
#define LD_STOR_INS_INC   7
#define STOR_IMM_INS      8
#define CSTBIT_INS        9

/* Maximum value supported for instruction types.  */
#define CR16_INS_MAX        (1 << 4)
/* Mask to record an instruction type.  */
#define CR16_INS_MASK       (CR16_INS_MAX - 1)
/* Return instruction type, given instruction's attributes.  */
#define CR16_INS_TYPE(attr) ((attr) & CR16_INS_MASK)

/* Indicates whether this instruction has a register list as parameter.  */
#define REG_LIST        CR16_INS_MAX

/* The operands in binary and assembly are placed in reverse order.
   load - (REVERSE_MATCH)/store - (! REVERSE_MATCH).  */
#define REVERSE_MATCH  (1 << 5)

/* Printing formats, where the instruction prefix isn't consecutive.  */
#define FMT_1          (1 << 9)    /* 0xF0F00000 */
#define FMT_2          (1 << 10)   /* 0xFFF0FF00 */
#define FMT_3          (1 << 11)   /* 0xFFF00F00 */
#define FMT_4          (1 << 12)   /* 0xFFF0F000 */
#define FMT_5          (1 << 13)   /* 0xFFF0FFF0 */
#define FMT_CR16       (FMT_1 | FMT_2 | FMT_3 | FMT_4 | FMT_5)

/* Indicates whether this instruction can be relaxed.  */
#define RELAXABLE      (1 << 14)

/* Indicates that instruction uses user registers (and not 
   general-purpose registers) as operands.  */
#define USER_REG       (1 << 15)


/* Instruction shouldn't allow 'sp' usage.  */
#define NO_SP          (1 << 17)

/* Instruction shouldn't allow to push a register which is used as a rptr.  */
#define NO_RPTR        (1 << 18)

/* Maximum operands per instruction.  */
#define MAX_OPERANDS     5
/* Maximum register name length. */
#define MAX_REGNAME_LEN  10
/* Maximum instruction length. */
#define MAX_INST_LEN     256


/* Values defined for the flags field of a struct operand_entry.  */

/* Operand must be an unsigned number.  */
#define OP_UNSIGNED   (1 << 0)
/* Operand must be a signed number.  */
#define OP_SIGNED     (1 << 1)
/* Operand must be a negative number.  */
#define OP_NEG        (1 << 2)
/* A special load/stor 4-bit unsigned displacement operand.  */
#define OP_DEC        (1 << 3)
/* Operand must be an even number.  */
#define OP_EVEN       (1 << 4)
/* Operand is shifted right.  */
#define OP_SHIFT      (1 << 5)
/* Operand is shifted right and decremented.  */
#define OP_SHIFT_DEC  (1 << 6)
/* Operand has reserved escape sequences.  */
#define OP_ESC        (1 << 7)
/* Operand must be a ABS20 number.  */
#define OP_ABS20      (1 << 8)
/* Operand must be a ABS24 number.  */
#define OP_ABS24      (1 << 9)
/* Operand has reserved escape sequences type 1.  */
#define OP_ESC1       (1 << 10)

/* Single operand description.  */

typedef struct
  {
    /* Operand type.  */
    operand_type op_type;
    /* Operand location within the opcode.  */
    unsigned int shift;
  }
operand_desc;

/* Instruction data structure used in instruction table.  */

typedef struct
  {
    /* Name.  */
    const char *mnemonic;
    /* Size (in words).  */
    unsigned int size;
    /* Constant prefix (matched by the disassembler).  */
    unsigned long match;  /* ie opcode */
    /* Match size (in bits).  */
    /* MASK: if( (i & match_bits) == match ) then match */
    int match_bits;
    /* Attributes.  */
    unsigned int flags;
    /* Operands (always last, so unreferenced operands are initialized).  */
    operand_desc operands[MAX_OPERANDS];
  }
inst;

/* Data structure for a single instruction's arguments (Operands).  */

typedef struct
  {
    /* Register or base register.  */
    reg r;
    /* Register pair register.  */
    reg rp;
    /* Index register.  */
    reg i_r;
    /* Processor register.  */
    preg pr;
    /* Processor register. 32 bit  */
    preg prp;
    /* Constant/immediate/absolute value.  */
    long constant;
    /* CC code.  */
    unsigned int cc;
    /* Scaled index mode.  */
    unsigned int scale;
    /* Argument type.  */
    argtype type;
    /* Size of the argument (in bits) required to represent.  */
    int size;
  /* The type of the expression.  */
    unsigned char X_op;
  }
argument;

/* Internal structure to hold the various entities
   corresponding to the current assembling instruction.  */

typedef struct
  {
    /* Number of arguments.  */
    int nargs;
    /* The argument data structure for storing args (operands).  */
    argument arg[MAX_OPERANDS];
/* The following fields are required only by CR16-assembler.  */
#ifdef TC_CR16
    /* Expression used for setting the fixups (if any).  */
    expressionS exp;
    bfd_reloc_code_real_type rtype;
#endif /* TC_CR16 */
    /* Instruction size (in bytes).  */
    int size;
  }
ins;

/* Structure to hold information about predefined operands.  */

typedef struct
  {
    /* Size (in bits).  */
    unsigned int bit_size;
    /* Argument type.  */
    argtype arg_type;
    /* One bit syntax flags.  */
    int flags;
  }
operand_entry;

/* Structure to hold trap handler information.  */

typedef struct
  {
    /* Trap name.  */
    char *name;
    /* Index in dispatch table.  */
    unsigned int entry;
  }
trap_entry;

/* Structure to hold information about predefined registers.  */

typedef struct
  {
    /* Name (string representation).  */
    char *name;
    /* Value (enum representation).  */
    union
    {
      /* Register.  */
      reg reg_val;
      /* processor register.  */
      preg preg_val;
    } value;
    /* Register image.  */
    int image;
    /* Register type.  */
    reg_type type;
  }
reg_entry;

/* CR16 opcode table.  */
extern const inst cr16_instruction[];
extern const unsigned int cr16_num_opcodes;
#define NUMOPCODES cr16_num_opcodes

/* CR16 operands table.  */
extern const operand_entry cr16_optab[];

/* CR16 registers table.  */
extern const reg_entry cr16_regtab[];
extern const unsigned int cr16_num_regs;
#define NUMREGS cr16_num_regs

/* CR16 register pair table.  */
extern const reg_entry cr16_regptab[];
extern const unsigned int cr16_num_regps;
#define NUMREGPS cr16_num_regps

/* CR16 processor registers table.  */
extern const reg_entry cr16_pregtab[];
extern const unsigned int cr16_num_pregs;
#define NUMPREGS cr16_num_pregs

/* CR16 processor registers - 32 bit table.  */
extern const reg_entry cr16_pregptab[];
extern const unsigned int cr16_num_pregps;
#define NUMPREGPS cr16_num_pregps

/* CR16 trap/interrupt table.  */
extern const trap_entry cr16_traps[];
extern const unsigned int cr16_num_traps;
#define NUMTRAPS cr16_num_traps

/* CR16 CC - codes bit table.  */
extern const char * cr16_b_cond_tab[];
extern const unsigned int cr16_num_cc;
#define NUMCC cr16_num_cc;


/* Table of instructions with no operands.  */
extern const char * cr16_no_op_insn[];

/* Current instruction we're assembling.  */
extern const inst *instruction;

/* A macro for representing the instruction "constant" opcode, that is,
   the FIXED part of the instruction. The "constant" opcode is represented
   as a 32-bit unsigned long, where OPC is expanded (by a left SHIFT)
   over that range.  */
#define BIN(OPC,SHIFT)        (OPC << SHIFT)

/* Is the current instruction type is TYPE ?  */
#define IS_INSN_TYPE(TYPE)              \
  (CR16_INS_TYPE (instruction->flags) == TYPE)

/* Is the current instruction mnemonic is MNEMONIC ?  */
#define IS_INSN_MNEMONIC(MNEMONIC)    \
  (strcmp (instruction->mnemonic, MNEMONIC) == 0)

/* Does the current instruction has register list ?  */
#define INST_HAS_REG_LIST              \
  (instruction->flags & REG_LIST)


/* Utility macros for string comparison.  */
#define streq(a, b)           (strcmp (a, b) == 0)
#define strneq(a, b, c)       (strncmp (a, b, c) == 0)

/* Long long type handling.  */
/* Replace all appearances of 'long long int' with LONGLONG.  */
typedef long long int LONGLONG;
typedef unsigned long long ULONGLONG;

#endif /* _CR16_H_ */
