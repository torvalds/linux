#ifndef _ASM_SCORE_EXTABLE_H
#define _ASM_SCORE_EXTABLE_H

struct exception_table_entry {
	unsigned long insn;
	unsigned long fixup;
};

struct pt_regs;
extern int fixup_exception(struct pt_regs *regs);
#endif
