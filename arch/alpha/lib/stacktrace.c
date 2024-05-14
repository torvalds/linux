// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>

typedef unsigned int instr;

#define MAJOR_OP	0xfc000000
#define LDA_OP		0x20000000
#define STQ_OP		0xb4000000
#define BR_OP		0xc0000000

#define STK_ALLOC_1	0x23de8000 /* lda $30,-X($30) */
#define STK_ALLOC_1M	0xffff8000
#define STK_ALLOC_2	0x43c0153e /* subq $30,X,$30 */
#define STK_ALLOC_2M	0xffe01fff

#define MEM_REG		0x03e00000
#define MEM_BASE	0x001f0000
#define MEM_OFF		0x0000ffff
#define MEM_OFF_SIGN	0x00008000
#define	BASE_SP		0x001e0000

#define STK_ALLOC_MATCH(INSTR)			\
  (((INSTR) & STK_ALLOC_1M) == STK_ALLOC_1	\
   || ((INSTR) & STK_ALLOC_2M) == STK_ALLOC_2)
#define STK_PUSH_MATCH(INSTR) \
  (((INSTR) & (MAJOR_OP | MEM_BASE | MEM_OFF_SIGN)) == (STQ_OP | BASE_SP))
#define MEM_OP_OFFSET(INSTR) \
  (((long)((INSTR) & MEM_OFF) << 48) >> 48)
#define MEM_OP_REG(INSTR) \
  (((INSTR) & MEM_REG) >> 22)

/* Branches, jumps, PAL calls, and illegal opcodes end a basic block. */
#define BB_END(INSTR)						\
  (((instr)(INSTR) >= BR_OP) | ((instr)(INSTR) < LDA_OP) |	\
   ((((instr)(INSTR) ^ 0x60000000) < 0x20000000) &		\
    (((instr)(INSTR) & 0x0c000000) != 0)))

#define IS_KERNEL_TEXT(PC) ((unsigned long)(PC) > START_ADDR)

static char reg_name[][4] = {
	"v0 ", "t0 ", "t1 ", "t2 ", "t3 ", "t4 ", "t5 ", "t6 ", "t7 ",
	"s0 ", "s1 ", "s2 ", "s3 ", "s4 ", "s5 ", "s6 ", "a0 ", "a1 ",
	"a2 ", "a3 ", "a4 ", "a5 ", "t8 ", "t9 ", "t10", "t11", "ra ",
	"pv ", "at ", "gp ", "sp ", "0"
};


static instr *
display_stored_regs(instr * pro_pc, unsigned char * sp)
{
	instr * ret_pc = 0;
	int reg;
	unsigned long value;

	printk("Prologue [<%p>], Frame %p:\n", pro_pc, sp);
	while (!BB_END(*pro_pc))
		if (STK_PUSH_MATCH(*pro_pc)) {
			reg = (*pro_pc & MEM_REG) >> 21;
			value = *(unsigned long *)(sp + (*pro_pc & MEM_OFF));
			if (reg == 26)
				ret_pc = (instr *)value;
			printk("\t\t%s / 0x%016lx\n", reg_name[reg], value);
		}
	return ret_pc;
}

static instr *
seek_prologue(instr * pc)
{
	while (!STK_ALLOC_MATCH(*pc))
		--pc;
	while (!BB_END(*(pc - 1)))
		--pc;
	return pc;
}

static long
stack_increment(instr * prologue_pc)
{
	while (!STK_ALLOC_MATCH(*prologue_pc))
		++prologue_pc;

	/* Count the bytes allocated. */
	if ((*prologue_pc & STK_ALLOC_1M) == STK_ALLOC_1M)
		return -(((long)(*prologue_pc) << 48) >> 48);
	else
		return (*prologue_pc >> 13) & 0xff;
}

void
stacktrace(void)
{
	instr * ret_pc;
	instr * prologue = (instr *)stacktrace;
	register unsigned char * sp __asm__ ("$30");

	printk("\tstack trace:\n");
	do {
		ret_pc = display_stored_regs(prologue, sp);
		sp += stack_increment(prologue);
		prologue = seek_prologue(ret_pc);
	} while (IS_KERNEL_TEXT(ret_pc));
}
