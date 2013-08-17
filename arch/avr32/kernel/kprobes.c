/*
 *  Kernel Probes (KProbes)
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * Based on arch/ppc64/kernel/kprobes.c
 *  Copyright (C) IBM Corporation, 2002, 2004
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kprobes.h>
#include <linux/ptrace.h>

#include <asm/cacheflush.h>
#include <linux/kdebug.h>
#include <asm/ocd.h>

DEFINE_PER_CPU(struct kprobe *, current_kprobe);
static unsigned long kprobe_status;
static struct pt_regs jprobe_saved_regs;

struct kretprobe_blackpoint kretprobe_blacklist[] = {{NULL, NULL}};

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	int ret = 0;

	if ((unsigned long)p->addr & 0x01) {
		printk("Attempt to register kprobe at an unaligned address\n");
		ret = -EINVAL;
	}

	/* XXX: Might be a good idea to check if p->addr is a valid
	 * kernel address as well... */

	if (!ret) {
		pr_debug("copy kprobe at %p\n", p->addr);
		memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE * sizeof(kprobe_opcode_t));
		p->opcode = *p->addr;
	}

	return ret;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	pr_debug("arming kprobe at %p\n", p->addr);
	ocd_enable(NULL);
	*p->addr = BREAKPOINT_INSTRUCTION;
	flush_icache_range((unsigned long)p->addr,
			   (unsigned long)p->addr + sizeof(kprobe_opcode_t));
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	pr_debug("disarming kprobe at %p\n", p->addr);
	ocd_disable(NULL);
	*p->addr = p->opcode;
	flush_icache_range((unsigned long)p->addr,
			   (unsigned long)p->addr + sizeof(kprobe_opcode_t));
}

static void __kprobes prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long dc;

	pr_debug("preparing to singlestep over %p (PC=%08lx)\n",
		 p->addr, regs->pc);

	BUG_ON(!(sysreg_read(SR) & SYSREG_BIT(SR_D)));

	dc = ocd_read(DC);
	dc |= 1 << OCD_DC_SS_BIT;
	ocd_write(DC, dc);

	/*
	 * We must run the instruction from its original location
	 * since it may actually reference PC.
	 *
	 * TODO: Do the instruction replacement directly in icache.
	 */
	*p->addr = p->opcode;
	flush_icache_range((unsigned long)p->addr,
			   (unsigned long)p->addr + sizeof(kprobe_opcode_t));
}

static void __kprobes resume_execution(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long dc;

	pr_debug("resuming execution at PC=%08lx\n", regs->pc);

	dc = ocd_read(DC);
	dc &= ~(1 << OCD_DC_SS_BIT);
	ocd_write(DC, dc);

	*p->addr = BREAKPOINT_INSTRUCTION;
	flush_icache_range((unsigned long)p->addr,
			   (unsigned long)p->addr + sizeof(kprobe_opcode_t));
}

static void __kprobes set_current_kprobe(struct kprobe *p)
{
	__get_cpu_var(current_kprobe) = p;
}

static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	void *addr = (void *)regs->pc;
	int ret = 0;

	pr_debug("kprobe_handler: kprobe_running=%p\n",
		 kprobe_running());

	/*
	 * We don't want to be preempted for the entire
	 * duration of kprobe processing
	 */
	preempt_disable();

	/* Check that we're not recursing */
	if (kprobe_running()) {
		p = get_kprobe(addr);
		if (p) {
			if (kprobe_status == KPROBE_HIT_SS) {
				printk("FIXME: kprobe hit while single-stepping!\n");
				goto no_kprobe;
			}

			printk("FIXME: kprobe hit while handling another kprobe\n");
			goto no_kprobe;
		} else {
			p = kprobe_running();
			if (p->break_handler && p->break_handler(p, regs))
				goto ss_probe;
		}
		/* If it's not ours, can't be delete race, (we hold lock). */
		goto no_kprobe;
	}

	p = get_kprobe(addr);
	if (!p)
		goto no_kprobe;

	kprobe_status = KPROBE_HIT_ACTIVE;
	set_current_kprobe(p);
	if (p->pre_handler && p->pre_handler(p, regs))
		/* handler has already set things up, so skip ss setup */
		return 1;

ss_probe:
	prepare_singlestep(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

static int __kprobes post_kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();

	pr_debug("post_kprobe_handler, cur=%p\n", cur);

	if (!cur)
		return 0;

	if (cur->post_handler) {
		kprobe_status = KPROBE_HIT_SSDONE;
		cur->post_handler(cur, regs, 0);
	}

	resume_execution(cur, regs);
	reset_current_kprobe();
	preempt_enable_no_resched();

	return 1;
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();

	pr_debug("kprobe_fault_handler: trapnr=%d\n", trapnr);

	if (cur->fault_handler && cur->fault_handler(cur, regs, trapnr))
		return 1;

	if (kprobe_status & KPROBE_HIT_SS) {
		resume_execution(cur, regs);
		preempt_enable_no_resched();
	}
	return 0;
}

/*
 * Wrapper routine to for handling exceptions.
 */
int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;

	pr_debug("kprobe_exceptions_notify: val=%lu, data=%p\n",
		 val, data);

	switch (val) {
	case DIE_BREAKPOINT:
		if (kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_SSTEP:
		if (post_kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	default:
		break;
	}

	return ret;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	memcpy(&jprobe_saved_regs, regs, sizeof(struct pt_regs));

	/*
	 * TODO: We should probably save some of the stack here as
	 * well, since gcc may pass arguments on the stack for certain
	 * functions (lots of arguments, large aggregates, varargs)
	 */

	/* setup return addr to the jprobe handler routine */
	regs->pc = (unsigned long)jp->entry;
	return 1;
}

void __kprobes jprobe_return(void)
{
	asm volatile("breakpoint" ::: "memory");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	/*
	 * FIXME - we should ideally be validating that we got here 'cos
	 * of the "trap" in jprobe_return() above, before restoring the
	 * saved regs...
	 */
	memcpy(regs, &jprobe_saved_regs, sizeof(struct pt_regs));
	return 1;
}

int __init arch_init_kprobes(void)
{
	/* TODO: Register kretprobe trampoline */
	return 0;
}
