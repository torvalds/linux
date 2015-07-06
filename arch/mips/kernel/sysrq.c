/*
 * MIPS specific sysrq operations.
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/sysrq.h>
#include <linux/workqueue.h>

#include <asm/cpu-features.h>
#include <asm/mipsregs.h>
#include <asm/tlbdebug.h>

/*
 * Dump TLB entries on all CPUs.
 */

static DEFINE_SPINLOCK(show_lock);

static void sysrq_tlbdump_single(void *dummy)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned long flags;

	spin_lock_irqsave(&show_lock, flags);

	pr_info("CPU%d:\n", smp_processor_id());
	pr_info("Index	: %0x\n", read_c0_index());
	pr_info("Pagemask: %0x\n", read_c0_pagemask());
	pr_info("EntryHi : %0*lx\n", field, read_c0_entryhi());
	pr_info("EntryLo0: %0*lx\n", field, read_c0_entrylo0());
	pr_info("EntryLo1: %0*lx\n", field, read_c0_entrylo1());
	pr_info("Wired   : %0x\n", read_c0_wired());
	pr_info("Pagegrain: %0x\n", read_c0_pagegrain());
	if (cpu_has_htw) {
		pr_info("PWField : %0*lx\n", field, read_c0_pwfield());
		pr_info("PWSize  : %0*lx\n", field, read_c0_pwsize());
		pr_info("PWCtl   : %0x\n", read_c0_pwctl());
	}
	pr_info("\n");
	dump_tlb_all();
	pr_info("\n");

	spin_unlock_irqrestore(&show_lock, flags);
}

#ifdef CONFIG_SMP
static void sysrq_tlbdump_othercpus(struct work_struct *dummy)
{
	smp_call_function(sysrq_tlbdump_single, NULL, 0);
}

static DECLARE_WORK(sysrq_tlbdump, sysrq_tlbdump_othercpus);
#endif

static void sysrq_handle_tlbdump(int key)
{
	sysrq_tlbdump_single(NULL);
#ifdef CONFIG_SMP
	schedule_work(&sysrq_tlbdump);
#endif
}

static struct sysrq_key_op sysrq_tlbdump_op = {
	.handler        = sysrq_handle_tlbdump,
	.help_msg       = "show-tlbs(x)",
	.action_msg     = "Show TLB entries",
	.enable_mask	= SYSRQ_ENABLE_DUMP,
};

static int __init mips_sysrq_init(void)
{
	return register_sysrq_key('x', &sysrq_tlbdump_op);
}
arch_initcall(mips_sysrq_init);
