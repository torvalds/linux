// SPDX-License-Identifier: GPL-2.0
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
	unsigned long flags;

	spin_lock_irqsave(&show_lock, flags);

	pr_info("CPU%d:\n", smp_processor_id());
	dump_tlb_regs();
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

static void sysrq_handle_tlbdump(u8 key)
{
	sysrq_tlbdump_single(NULL);
#ifdef CONFIG_SMP
	schedule_work(&sysrq_tlbdump);
#endif
}

static const struct sysrq_key_op sysrq_tlbdump_op = {
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
