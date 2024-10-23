// SPDX-License-Identifier: GPL-2.0
/*
 * PA-RISC KGDB support
 *
 * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
 * Copyright (c) 2022 Helge Deller <deller@gmx.de>
 *
 */

#include <linux/kgdb.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/kdebug.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>
#include <asm/traps.h>
#include <asm/processor.h>
#include <asm/text-patching.h>
#include <asm/cacheflush.h>

const struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr = { 0x03, 0xff, 0xa0, 0x1f }
};

static int __kgdb_notify(struct die_args *args, unsigned long cmd)
{
	struct pt_regs *regs = args->regs;

	if (kgdb_handle_exception(1, args->signr, cmd, regs))
		return NOTIFY_DONE;
	return NOTIFY_STOP;
}

static int kgdb_notify(struct notifier_block *self,
		       unsigned long cmd, void *ptr)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = __kgdb_notify(ptr, cmd);
	local_irq_restore(flags);

	return ret;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call	= kgdb_notify,
	.priority	= -INT_MAX,
};

int kgdb_arch_init(void)
{
	return register_die_notifier(&kgdb_notifier);
}

void kgdb_arch_exit(void)
{
	unregister_die_notifier(&kgdb_notifier);
}

void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	struct parisc_gdb_regs *gr = (struct parisc_gdb_regs *)gdb_regs;

	memset(gr, 0, sizeof(struct parisc_gdb_regs));

	memcpy(gr->gpr, regs->gr, sizeof(gr->gpr));
	memcpy(gr->fr, regs->fr, sizeof(gr->fr));

	gr->sr0 = regs->sr[0];
	gr->sr1 = regs->sr[1];
	gr->sr2 = regs->sr[2];
	gr->sr3 = regs->sr[3];
	gr->sr4 = regs->sr[4];
	gr->sr5 = regs->sr[5];
	gr->sr6 = regs->sr[6];
	gr->sr7 = regs->sr[7];

	gr->sar = regs->sar;
	gr->iir = regs->iir;
	gr->isr = regs->isr;
	gr->ior = regs->ior;
	gr->ipsw = regs->ipsw;
	gr->cr27 = regs->cr27;

	gr->iaoq_f = regs->iaoq[0];
	gr->iasq_f = regs->iasq[0];

	gr->iaoq_b = regs->iaoq[1];
	gr->iasq_b = regs->iasq[1];
}

void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	struct parisc_gdb_regs *gr = (struct parisc_gdb_regs *)gdb_regs;


	memcpy(regs->gr, gr->gpr, sizeof(regs->gr));
	memcpy(regs->fr, gr->fr, sizeof(regs->fr));

	regs->sr[0] = gr->sr0;
	regs->sr[1] = gr->sr1;
	regs->sr[2] = gr->sr2;
	regs->sr[3] = gr->sr3;
	regs->sr[4] = gr->sr4;
	regs->sr[5] = gr->sr5;
	regs->sr[6] = gr->sr6;
	regs->sr[7] = gr->sr7;

	regs->sar = gr->sar;
	regs->iir = gr->iir;
	regs->isr = gr->isr;
	regs->ior = gr->ior;
	regs->ipsw = gr->ipsw;
	regs->cr27 = gr->cr27;

	regs->iaoq[0] = gr->iaoq_f;
	regs->iasq[0] = gr->iasq_f;

	regs->iaoq[1] = gr->iaoq_b;
	regs->iasq[1] = gr->iasq_b;
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs,
				struct task_struct *task)
{
	struct pt_regs *regs = task_pt_regs(task);
	unsigned long gr30, iaoq;

	gr30 = regs->gr[30];
	iaoq = regs->iaoq[0];

	regs->gr[30] = regs->ksp;
	regs->iaoq[0] = regs->kpc;
	pt_regs_to_gdb_regs(gdb_regs, regs);

	regs->gr[30] = gr30;
	regs->iaoq[0] = iaoq;

}

static void step_instruction_queue(struct pt_regs *regs)
{
	regs->iaoq[0] = regs->iaoq[1];
	regs->iaoq[1] += 4;
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	regs->iaoq[0] = ip;
	regs->iaoq[1] = ip + 4;
}

int kgdb_arch_set_breakpoint(struct kgdb_bkpt *bpt)
{
	int ret = copy_from_kernel_nofault(bpt->saved_instr,
			(char *)bpt->bpt_addr, BREAK_INSTR_SIZE);
	if (ret)
		return ret;

	__patch_text((void *)bpt->bpt_addr,
			*(unsigned int *)&arch_kgdb_ops.gdb_bpt_instr);
	return ret;
}

int kgdb_arch_remove_breakpoint(struct kgdb_bkpt *bpt)
{
	__patch_text((void *)bpt->bpt_addr, *(unsigned int *)&bpt->saved_instr);
	return 0;
}

int kgdb_arch_handle_exception(int trap, int signo,
		int err_code, char *inbuf, char *outbuf,
		struct pt_regs *regs)
{
	unsigned long addr;
	char *p = inbuf + 1;

	switch (inbuf[0]) {
	case 'D':
	case 'c':
	case 'k':
		kgdb_contthread = NULL;
		kgdb_single_step = 0;

		if (kgdb_hex2long(&p, &addr))
			kgdb_arch_set_pc(regs, addr);
		else if (trap == 9 && regs->iir ==
				PARISC_KGDB_COMPILED_BREAK_INSN)
			step_instruction_queue(regs);
		return 0;
	case 's':
		kgdb_single_step = 1;
		if (kgdb_hex2long(&p, &addr)) {
			kgdb_arch_set_pc(regs, addr);
		} else if (trap == 9 && regs->iir ==
				PARISC_KGDB_COMPILED_BREAK_INSN) {
			step_instruction_queue(regs);
			mtctl(-1, 0);
		} else {
			mtctl(0, 0);
		}
		regs->gr[0] |= PSW_R;
		return 0;

	}
	return -1;
}
