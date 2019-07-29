/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ABI_CSKY_ELF_H
#define __ABI_CSKY_ELF_H

/* The member sort in array pr_reg[x] is defined by GDB. */
#define ELF_CORE_COPY_REGS(pr_reg, regs) do {	\
	pr_reg[0] = regs->pc;			\
	pr_reg[1] = regs->a1;			\
	pr_reg[2] = regs->a0;			\
	pr_reg[3] = regs->sr;			\
	pr_reg[4] = regs->a2;			\
	pr_reg[5] = regs->a3;			\
	pr_reg[6] = regs->regs[0];		\
	pr_reg[7] = regs->regs[1];		\
	pr_reg[8] = regs->regs[2];		\
	pr_reg[9] = regs->regs[3];		\
	pr_reg[10] = regs->regs[4];		\
	pr_reg[11] = regs->regs[5];		\
	pr_reg[12] = regs->regs[6];		\
	pr_reg[13] = regs->regs[7];		\
	pr_reg[14] = regs->regs[8];		\
	pr_reg[15] = regs->regs[9];		\
	pr_reg[16] = regs->usp;			\
	pr_reg[17] = regs->lr;			\
	pr_reg[18] = regs->exregs[0];		\
	pr_reg[19] = regs->exregs[1];		\
	pr_reg[20] = regs->exregs[2];		\
	pr_reg[21] = regs->exregs[3];		\
	pr_reg[22] = regs->exregs[4];		\
	pr_reg[23] = regs->exregs[5];		\
	pr_reg[24] = regs->exregs[6];		\
	pr_reg[25] = regs->exregs[7];		\
	pr_reg[26] = regs->exregs[8];		\
	pr_reg[27] = regs->exregs[9];		\
	pr_reg[28] = regs->exregs[10];		\
	pr_reg[29] = regs->exregs[11];		\
	pr_reg[30] = regs->exregs[12];		\
	pr_reg[31] = regs->exregs[13];		\
	pr_reg[32] = regs->exregs[14];		\
	pr_reg[33] = regs->tls;			\
} while (0);
#endif /* __ABI_CSKY_ELF_H */
