// SPDX-License-Identifier: GPL-2.0
/*
 * LoongArch specific sysrq operations.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/sysrq.h>
#include <linux/workqueue.h>

#include <asm/cpu-features.h>
#include <asm/tlb.h>

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

static struct sysrq_key_op sysrq_tlbdump_op = {
	.handler        = sysrq_handle_tlbdump,
	.help_msg       = "show-tlbs(x)",
	.action_msg     = "Show TLB entries",
	.enable_mask	= SYSRQ_ENABLE_DUMP,
};

static int __init loongarch_sysrq_init(void)
{
	return register_sysrq_key('x', &sysrq_tlbdump_op);
}
arch_initcall(loongarch_sysrq_init);
