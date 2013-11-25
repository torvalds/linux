/*
 * Disassemble s390 instructions.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#ifndef __ASM_S390_DIS_H__
#define __ASM_S390_DIS_H__

/* Type of operand */
#define OPERAND_GPR	0x1	/* Operand printed as %rx */
#define OPERAND_FPR	0x2	/* Operand printed as %fx */
#define OPERAND_AR	0x4	/* Operand printed as %ax */
#define OPERAND_CR	0x8	/* Operand printed as %cx */
#define OPERAND_DISP	0x10	/* Operand printed as displacement */
#define OPERAND_BASE	0x20	/* Operand printed as base register */
#define OPERAND_INDEX	0x40	/* Operand printed as index register */
#define OPERAND_PCREL	0x80	/* Operand printed as pc-relative symbol */
#define OPERAND_SIGNED	0x100	/* Operand printed as signed value */
#define OPERAND_LENGTH	0x200	/* Operand printed as length (+1) */


struct s390_operand {
	int bits;		/* The number of bits in the operand. */
	int shift;		/* The number of bits to shift. */
	int flags;		/* One bit syntax flags. */
};

struct s390_insn {
	const char name[5];
	unsigned char opfrag;
	unsigned char format;
};


static inline int insn_length(unsigned char code)
{
	return ((((int) code + 64) >> 7) + 1) << 1;
}

void show_code(struct pt_regs *regs);
void print_fn_code(unsigned char *code, unsigned long len);
int insn_to_mnemonic(unsigned char *instruction, char *buf, unsigned int len);
struct s390_insn *find_insn(unsigned char *code);

static inline int is_known_insn(unsigned char *code)
{
	return !!find_insn(code);
}

#endif /* __ASM_S390_DIS_H__ */
