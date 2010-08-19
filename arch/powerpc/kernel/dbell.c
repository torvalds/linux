/*
 * Author: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2009 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/percpu.h>

#include <asm/dbell.h>
#include <asm/irq_regs.h>

#ifdef CONFIG_SMP
struct doorbell_cpu_info {
	unsigned long	messages;	/* current messages bits */
	unsigned int	tag;		/* tag value */
};

static DEFINE_PER_CPU(struct doorbell_cpu_info, doorbell_cpu_info);

void doorbell_setup_this_cpu(void)
{
	struct doorbell_cpu_info *info = &__get_cpu_var(doorbell_cpu_info);

	info->messages = 0;
	info->tag = mfspr(SPRN_PIR) & 0x3fff;
}

void doorbell_message_pass(int target, int msg)
{
	struct doorbell_cpu_info *info;
	int i;

	if (target < NR_CPUS) {
		info = &per_cpu(doorbell_cpu_info, target);
		set_bit(msg, &info->messages);
		ppc_msgsnd(PPC_DBELL, 0, info->tag);
	}
	else if (target == MSG_ALL_BUT_SELF) {
		for_each_online_cpu(i) {
			if (i == smp_processor_id())
				continue;
			info = &per_cpu(doorbell_cpu_info, i);
			set_bit(msg, &info->messages);
			ppc_msgsnd(PPC_DBELL, 0, info->tag);
		}
	}
	else { /* target == MSG_ALL */
		for_each_online_cpu(i) {
			info = &per_cpu(doorbell_cpu_info, i);
			set_bit(msg, &info->messages);
		}
		ppc_msgsnd(PPC_DBELL, PPC_DBELL_MSG_BRDCAST, 0);
	}
}

void doorbell_exception(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct doorbell_cpu_info *info = &__get_cpu_var(doorbell_cpu_info);
	int msg;

	/* Warning: regs can be NULL when called from irq enable */

	if (!info->messages || (num_online_cpus() < 2))
		goto out;

	for (msg = 0; msg < 4; msg++)
		if (test_and_clear_bit(msg, &info->messages))
			smp_message_recv(msg);

out:
	set_irq_regs(old_regs);
}

void doorbell_check_self(void)
{
	struct doorbell_cpu_info *info = &__get_cpu_var(doorbell_cpu_info);

	if (!info->messages)
		return;

	ppc_msgsnd(PPC_DBELL, 0, info->tag);
}

#else /* CONFIG_SMP */
void doorbell_exception(struct pt_regs *regs)
{
	printk(KERN_WARNING "Received doorbell on non-smp system\n");
}
#endif /* CONFIG_SMP */

