/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ABI_CSKY_ELF_H
#define __ABI_CSKY_ELF_H

#define ELF_CORE_COPY_REGS(pr_reg, regs) do {	\
	pr_reg[0] = regs->pc;			\
	pr_reg[1] = regs->regs[9];		\
	pr_reg[2] = regs->usp;			\
	pr_reg[3] = regs->sr;			\
	pr_reg[4] = regs->a0;			\
	pr_reg[5] = regs->a1;			\
	pr_reg[6] = regs->a2;			\
	pr_reg[7] = regs->a3;			\
	pr_reg[8] = regs->regs[0];		\
	pr_reg[9] = regs->regs[1];		\
	pr_reg[10] = regs->regs[2];		\
	pr_reg[11] = regs->regs[3];		\
	pr_reg[12] = regs->regs[4];		\
	pr_reg[13] = regs->regs[5];		\
	pr_reg[14] = regs->regs[6];		\
	pr_reg[15] = regs->regs[7];		\
	pr_reg[16] = regs->regs[8];		\
	pr_reg[17] = regs->lr;			\
} while (0);
#endif /* __ABI_CSKY_ELF_H */
