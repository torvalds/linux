/*
 *    User-space Probes (UProbes) for s390
 *
 *    Copyright IBM Corp. 2014
 *    Author(s): Jan Willeke,
 */

#ifndef _ASM_UPROBES_H
#define _ASM_UPROBES_H

#include <linux/notifier.h>

typedef u16 uprobe_opcode_t;

#define UPROBE_XOL_SLOT_BYTES	256 /* cache aligned */

#define UPROBE_SWBP_INSN	0x0002
#define UPROBE_SWBP_INSN_SIZE	2

struct arch_uprobe {
	union{
		uprobe_opcode_t insn[3];
		uprobe_opcode_t ixol[3];
	};
	unsigned int saved_per : 1;
	unsigned int saved_int_code;
};

struct arch_uprobe_task {
};

int arch_uprobe_analyze_insn(struct arch_uprobe *aup, struct mm_struct *mm,
			     unsigned long addr);
int arch_uprobe_pre_xol(struct arch_uprobe *aup, struct pt_regs *regs);
int arch_uprobe_post_xol(struct arch_uprobe *aup, struct pt_regs *regs);
bool arch_uprobe_xol_was_trapped(struct task_struct *tsk);
int arch_uprobe_exception_notify(struct notifier_block *self, unsigned long val,
				 void *data);
void arch_uprobe_abort_xol(struct arch_uprobe *ap, struct pt_regs *regs);
unsigned long arch_uretprobe_hijack_return_addr(unsigned long trampoline,
						struct pt_regs *regs);
#endif	/* _ASM_UPROBES_H */
