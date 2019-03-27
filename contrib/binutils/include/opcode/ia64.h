/* ia64.h -- Header file for ia64 opcode table
   Copyright (C) 1998, 1999, 2000, 2002, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by David Mosberger-Tang <davidm@hpl.hp.com> */

#ifndef opcode_ia64_h
#define opcode_ia64_h

#include <sys/types.h>

#include "bfd.h"


typedef BFD_HOST_U_64_BIT ia64_insn;

enum ia64_insn_type
  {
    IA64_TYPE_NIL = 0,	/* illegal type */
    IA64_TYPE_A,	/* integer alu (I- or M-unit) */
    IA64_TYPE_I,	/* non-alu integer (I-unit) */
    IA64_TYPE_M,	/* memory (M-unit) */
    IA64_TYPE_B,	/* branch (B-unit) */
    IA64_TYPE_F,	/* floating-point (F-unit) */
    IA64_TYPE_X,	/* long encoding (X-unit) */
    IA64_TYPE_DYN,	/* Dynamic opcode */
    IA64_NUM_TYPES
  };

enum ia64_unit
  {
    IA64_UNIT_NIL = 0,	/* illegal unit */
    IA64_UNIT_I,	/* integer unit */
    IA64_UNIT_M,	/* memory unit */
    IA64_UNIT_B,	/* branching unit */
    IA64_UNIT_F,	/* floating-point unit */
    IA64_UNIT_L,	/* long "unit" */
    IA64_UNIT_X,	/* may be integer or branch unit */
    IA64_NUM_UNITS
  };

/* Changes to this enumeration must be propagated to the operand table in
   bfd/cpu-ia64-opc.c
 */
enum ia64_opnd
  {
    IA64_OPND_NIL,	/* no operand---MUST BE FIRST!*/

    /* constants */
    IA64_OPND_AR_CSD,	/* application register csd (ar.csd) */
    IA64_OPND_AR_CCV,	/* application register ccv (ar.ccv) */
    IA64_OPND_AR_PFS,	/* application register pfs (ar.pfs) */
    IA64_OPND_C1,	/* the constant 1 */
    IA64_OPND_C8,	/* the constant 8 */
    IA64_OPND_C16,	/* the constant 16 */
    IA64_OPND_GR0,	/* gr0 */
    IA64_OPND_IP,	/* instruction pointer (ip) */
    IA64_OPND_PR,	/* predicate register (pr) */
    IA64_OPND_PR_ROT,	/* rotating predicate register (pr.rot) */
    IA64_OPND_PSR,	/* processor status register (psr) */
    IA64_OPND_PSR_L,	/* processor status register L (psr.l) */
    IA64_OPND_PSR_UM,	/* processor status register UM (psr.um) */

    /* register operands: */
    IA64_OPND_AR3,	/* third application register # (bits 20-26) */
    IA64_OPND_B1,	/* branch register # (bits 6-8) */
    IA64_OPND_B2,	/* branch register # (bits 13-15) */
    IA64_OPND_CR3,	/* third control register # (bits 20-26) */
    IA64_OPND_F1,	/* first floating-point register # */
    IA64_OPND_F2,	/* second floating-point register # */
    IA64_OPND_F3,	/* third floating-point register # */
    IA64_OPND_F4,	/* fourth floating-point register # */
    IA64_OPND_P1,	/* first predicate # */
    IA64_OPND_P2,	/* second predicate # */
    IA64_OPND_R1,	/* first register # */
    IA64_OPND_R2,	/* second register # */
    IA64_OPND_R3,	/* third register # */
    IA64_OPND_R3_2,	/* third register # (limited to gr0-gr3) */

    /* memory operands: */
    IA64_OPND_MR3,	/* memory at addr of third register # */

    /* indirect operands: */
    IA64_OPND_CPUID_R3,	/* cpuid[reg] */
    IA64_OPND_DBR_R3,	/* dbr[reg] */
    IA64_OPND_DTR_R3,	/* dtr[reg] */
    IA64_OPND_ITR_R3,	/* itr[reg] */
    IA64_OPND_IBR_R3,	/* ibr[reg] */
    IA64_OPND_MSR_R3,	/* msr[reg] */
    IA64_OPND_PKR_R3,	/* pkr[reg] */
    IA64_OPND_PMC_R3,	/* pmc[reg] */
    IA64_OPND_PMD_R3,	/* pmd[reg] */
    IA64_OPND_RR_R3,	/* rr[reg] */

    /* immediate operands: */
    IA64_OPND_CCNT5,	/* 5-bit count (31 - bits 20-24) */
    IA64_OPND_CNT2a,	/* 2-bit count (1 + bits 27-28) */
    IA64_OPND_CNT2b,	/* 2-bit count (bits 27-28): 1, 2, 3 */
    IA64_OPND_CNT2c,	/* 2-bit count (bits 30-31): 0, 7, 15, or 16 */
    IA64_OPND_CNT5,	/* 5-bit count (bits 14-18) */
    IA64_OPND_CNT6,	/* 6-bit count (bits 27-32) */
    IA64_OPND_CPOS6a,	/* 6-bit count (63 - bits 20-25) */
    IA64_OPND_CPOS6b,	/* 6-bit count (63 - bits 14-19) */
    IA64_OPND_CPOS6c,	/* 6-bit count (63 - bits 31-36) */
    IA64_OPND_IMM1,	/* signed 1-bit immediate (bit 36) */
    IA64_OPND_IMMU2,	/* unsigned 2-bit immediate (bits 13-14) */
    IA64_OPND_IMMU5b,	/* unsigned 5-bit immediate (32 + bits 14-18) */
    IA64_OPND_IMMU7a,	/* unsigned 7-bit immediate (bits 13-19) */
    IA64_OPND_IMMU7b,	/* unsigned 7-bit immediate (bits 20-26) */
    IA64_OPND_SOF,	/* 8-bit stack frame size */
    IA64_OPND_SOL,	/* 8-bit size of locals */
    IA64_OPND_SOR,	/* 6-bit number of rotating registers (scaled by 8) */
    IA64_OPND_IMM8,	/* signed 8-bit immediate (bits 13-19 & 36) */
    IA64_OPND_IMM8U4,	/* cmp4*u signed 8-bit immediate (bits 13-19 & 36) */
    IA64_OPND_IMM8M1,	/* signed 8-bit immediate -1 (bits 13-19 & 36) */
    IA64_OPND_IMM8M1U4,	/* cmp4*u signed 8-bit immediate -1 (bits 13-19 & 36)*/
    IA64_OPND_IMM8M1U8,	/* cmp*u signed 8-bit immediate -1 (bits 13-19 & 36) */
    IA64_OPND_IMMU9,	/* unsigned 9-bit immediate (bits 33-34, 20-26) */
    IA64_OPND_IMM9a,	/* signed 9-bit immediate (bits 6-12, 27, 36) */
    IA64_OPND_IMM9b,	/* signed 9-bit immediate (bits 13-19, 27, 36) */
    IA64_OPND_IMM14,	/* signed 14-bit immediate (bits 13-19, 27-32, 36) */
    IA64_OPND_IMM17,	/* signed 17-bit immediate (2*bits 6-12, 24-31, 36) */
    IA64_OPND_IMMU21,	/* unsigned 21-bit immediate (bits 6-25, 36) */
    IA64_OPND_IMM22,	/* signed 22-bit immediate (bits 13-19, 22-36) */
    IA64_OPND_IMMU24,	/* unsigned 24-bit immediate (bits 6-26, 31-32, 36) */
    IA64_OPND_IMM44,	/* signed 44-bit immediate (2^16*bits 6-32, 36) */
    IA64_OPND_IMMU62,	/* unsigned 62-bit immediate */
    IA64_OPND_IMMU64,	/* unsigned 64-bit immediate (lotsa bits...) */
    IA64_OPND_INC3,	/* signed 3-bit (bits 13-15): +/-1, 4, 8, 16 */
    IA64_OPND_LEN4,	/* 4-bit count (bits 27-30 + 1) */
    IA64_OPND_LEN6,	/* 6-bit count (bits 27-32 + 1) */
    IA64_OPND_MBTYPE4,	/* 4-bit mux type (bits 20-23) */
    IA64_OPND_MHTYPE8,	/* 8-bit mux type (bits 20-27) */
    IA64_OPND_POS6,	/* 6-bit count (bits 14-19) */
    IA64_OPND_TAG13,	/* signed 13-bit tag (ip + 16*bits 6-12, 33-34) */
    IA64_OPND_TAG13b,	/* signed 13-bit tag (ip + 16*bits 24-32) */
    IA64_OPND_TGT25,	/* signed 25-bit (ip + 16*bits 6-25, 36) */
    IA64_OPND_TGT25b,	/* signed 25-bit (ip + 16*bits 6-12, 20-32, 36) */
    IA64_OPND_TGT25c,	/* signed 25-bit (ip + 16*bits 13-32, 36) */
    IA64_OPND_TGT64,    /* 64-bit (ip + 16*bits 13-32, 36, 2-40(L)) */
    IA64_OPND_LDXMOV,	/* any symbol, generates R_IA64_LDXMOV.  */

    IA64_OPND_COUNT	/* # of operand types (MUST BE LAST!) */
  };

enum ia64_dependency_mode
{
  IA64_DV_RAW,
  IA64_DV_WAW,
  IA64_DV_WAR,
};

enum ia64_dependency_semantics
{
  IA64_DVS_NONE,
  IA64_DVS_IMPLIED,
  IA64_DVS_IMPLIEDF,
  IA64_DVS_DATA,
  IA64_DVS_INSTR,
  IA64_DVS_SPECIFIC,
  IA64_DVS_STOP,
  IA64_DVS_OTHER,
};

enum ia64_resource_specifier
{
  IA64_RS_ANY,
  IA64_RS_AR_K,
  IA64_RS_AR_UNAT,
  IA64_RS_AR, /* 8-15, 20, 22-23, 31, 33-35, 37-39, 41-43, 45-47, 67-111 */
  IA64_RS_ARb, /* 48-63, 112-127 */
  IA64_RS_BR,
  IA64_RS_CFM,
  IA64_RS_CPUID,
  IA64_RS_CR_IRR,
  IA64_RS_CR_LRR,
  IA64_RS_CR, /* 3-7,10-15,18,26-63,75-79,82-127 */
  IA64_RS_DBR,
  IA64_RS_FR,
  IA64_RS_FRb,
  IA64_RS_GR0,
  IA64_RS_GR,
  IA64_RS_IBR,
  IA64_RS_INSERVICE, /* CR[EOI] or CR[IVR] */
  IA64_RS_MSR,
  IA64_RS_PKR,
  IA64_RS_PMC,
  IA64_RS_PMD,
  IA64_RS_PR,  /* non-rotating, 1-15 */
  IA64_RS_PRr, /* rotating, 16-62 */
  IA64_RS_PR63,
  IA64_RS_RR,

  IA64_RS_ARX, /* ARs not in RS_AR or RS_ARb */
  IA64_RS_CRX, /* CRs not in RS_CR */
  IA64_RS_PSR, /* PSR bits */
  IA64_RS_RSE, /* implementation-specific RSE resources */
  IA64_RS_AR_FPSR,
};

enum ia64_rse_resource
{
  IA64_RSE_N_STACKED_PHYS,
  IA64_RSE_BOF,
  IA64_RSE_STORE_REG,
  IA64_RSE_LOAD_REG,
  IA64_RSE_BSPLOAD,
  IA64_RSE_RNATBITINDEX,
  IA64_RSE_CFLE,
  IA64_RSE_NDIRTY,
};

/* Information about a given resource dependency */
struct ia64_dependency
{
  /* Name of the resource */
  const char *name;
  /* Does this dependency need further specification? */
  enum ia64_resource_specifier specifier;
  /* Mode of dependency */
  enum ia64_dependency_mode mode;
  /* Dependency semantics */
  enum ia64_dependency_semantics semantics;
  /* Register index, if applicable (distinguishes AR, CR, and PSR deps) */
#define REG_NONE (-1)
  int regindex;
  /* Special info on semantics */
  const char *info;
};

/* Two arrays of indexes into the ia64_dependency table.
   chks are dependencies to check for conflicts when an opcode is
   encountered; regs are dependencies to register (mark as used) when an
   opcode is used.  chks correspond to readers (RAW) or writers (WAW or
   WAR) of a resource, while regs correspond to writers (RAW or WAW) and
   readers (WAR) of a resource.  */
struct ia64_opcode_dependency
{
  int nchks;
  const unsigned short *chks;
  int nregs;
  const unsigned short *regs;
};

/* encode/extract the note/index for a dependency */
#define RDEP(N,X) (((N)<<11)|(X))
#define NOTE(X) (((X)>>11)&0x1F)
#define DEP(X) ((X)&0x7FF)

/* A template descriptor describes the execution units that are active
   for each of the three slots.  It also specifies the location of
   instruction group boundaries that may be present between two slots.  */
struct ia64_templ_desc
  {
    int group_boundary;	/* 0=no boundary, 1=between slot 0 & 1, etc. */
    enum ia64_unit exec_unit[3];
    const char *name;
  };

/* The opcode table is an array of struct ia64_opcode.  */

struct ia64_opcode
  {
    /* The opcode name.  */
    const char *name;

    /* The type of the instruction: */
    enum ia64_insn_type type;

    /* Number of output operands: */
    int num_outputs;

    /* The opcode itself.  Those bits which will be filled in with
       operands are zeroes.  */
    ia64_insn opcode;

    /* The opcode mask.  This is used by the disassembler.  This is a
       mask containing ones indicating those bits which must match the
       opcode field, and zeroes indicating those bits which need not
       match (and are presumably filled in by operands).  */
    ia64_insn mask;

    /* An array of operand codes.  Each code is an index into the
       operand table.  They appear in the order which the operands must
       appear in assembly code, and are terminated by a zero.  */
    enum ia64_opnd operands[5];

    /* One bit flags for the opcode.  These are primarily used to
       indicate specific processors and environments support the
       instructions.  The defined values are listed below. */
    unsigned int flags;

    /* Used by ia64_find_next_opcode (). */
    short ent_index;

    /* Opcode dependencies. */
    const struct ia64_opcode_dependency *dependencies;
  };

/* Values defined for the flags field of a struct ia64_opcode.  */

#define IA64_OPCODE_FIRST	(1<<0)	/* must be first in an insn group */
#define IA64_OPCODE_X_IN_MLX	(1<<1)	/* insn is allowed in X slot of MLX */
#define IA64_OPCODE_LAST	(1<<2)	/* must be last in an insn group */
#define IA64_OPCODE_PRIV	(1<<3)	/* privileged instruct */
#define IA64_OPCODE_SLOT2	(1<<4)	/* insn allowed in slot 2 only */
#define IA64_OPCODE_NO_PRED	(1<<5)	/* insn cannot be predicated */
#define IA64_OPCODE_PSEUDO	(1<<6)	/* insn is a pseudo-op */
#define IA64_OPCODE_F2_EQ_F3	(1<<7)	/* constraint: F2 == F3 */
#define IA64_OPCODE_LEN_EQ_64MCNT	(1<<8)	/* constraint: LEN == 64-CNT */
#define IA64_OPCODE_MOD_RRBS    (1<<9)	/* modifies all rrbs in CFM */
#define IA64_OPCODE_POSTINC	(1<<10)	/* postincrement MR3 operand */

/* A macro to extract the major opcode from an instruction.  */
#define IA64_OP(i)	(((i) >> 37) & 0xf)

enum ia64_operand_class
  {
    IA64_OPND_CLASS_CST,	/* constant */
    IA64_OPND_CLASS_REG,	/* register */
    IA64_OPND_CLASS_IND,	/* indirect register */
    IA64_OPND_CLASS_ABS,	/* absolute value */
    IA64_OPND_CLASS_REL,	/* IP-relative value */
  };

/* The operands table is an array of struct ia64_operand.  */

struct ia64_operand
{
  enum ia64_operand_class class;

  /* Set VALUE as the operand bits for the operand of type SELF in the
     instruction pointed to by CODE.  If an error occurs, *CODE is not
     modified and the returned string describes the cause of the
     error.  If no error occurs, NULL is returned.  */
  const char *(*insert) (const struct ia64_operand *self, ia64_insn value,
			 ia64_insn *code);

  /* Extract the operand bits for an operand of type SELF from
     instruction CODE store them in *VALUE.  If an error occurs, the
     cause of the error is described by the string returned.  If no
     error occurs, NULL is returned.  */
  const char *(*extract) (const struct ia64_operand *self, ia64_insn code,
			  ia64_insn *value);

  /* A string whose meaning depends on the operand class.  */

  const char *str;

  struct bit_field
    {
      /* The number of bits in the operand.  */
      int bits;

      /* How far the operand is left shifted in the instruction.  */
      int shift;
    }
  field[4];		/* no operand has more than this many bit-fields */

  unsigned int flags;

  const char *desc;	/* brief description */
};

/* Values defined for the flags field of a struct ia64_operand.  */

/* Disassemble as signed decimal (instead of hex): */
#define IA64_OPND_FLAG_DECIMAL_SIGNED	(1<<0)
/* Disassemble as unsigned decimal (instead of hex): */
#define IA64_OPND_FLAG_DECIMAL_UNSIGNED	(1<<1)

extern const struct ia64_templ_desc ia64_templ_desc[16];

/* The tables are sorted by major opcode number and are otherwise in
   the order in which the disassembler should consider instructions.  */
extern struct ia64_opcode ia64_opcodes_a[];
extern struct ia64_opcode ia64_opcodes_i[];
extern struct ia64_opcode ia64_opcodes_m[];
extern struct ia64_opcode ia64_opcodes_b[];
extern struct ia64_opcode ia64_opcodes_f[];
extern struct ia64_opcode ia64_opcodes_d[];


extern struct ia64_opcode *ia64_find_opcode (const char *name);
extern struct ia64_opcode *ia64_find_next_opcode (struct ia64_opcode *ent);

extern struct ia64_opcode *ia64_dis_opcode (ia64_insn insn,
					    enum ia64_insn_type type);

extern void ia64_free_opcode (struct ia64_opcode *ent);
extern const struct ia64_dependency *ia64_find_dependency (int index);

/* To avoid circular library dependencies, this array is implemented
   in bfd/cpu-ia64-opc.c: */
extern const struct ia64_operand elf64_ia64_operands[IA64_OPND_COUNT];

#endif /* opcode_ia64_h */
