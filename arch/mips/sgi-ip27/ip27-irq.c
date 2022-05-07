// SPDX-License-Identifier: GPL-2.0
/*
 * ip27-irq.c: Highlevel interrupt handling for IP27 architecture.
 *
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 1999 - 2001 Kanoj Sarcar
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/irq_cpu.h>
#include <asm/sn/addrs.h>
#include <asm/sn/agent.h>
#include <asm/sn/arch.h>
#include <asm/sn/intr.h>
#include <asm/sn/irq_alloc.h>

struct hub_irq_data {
	u64	*irq_mask[2];
	cpuid_t	cpu;
};

static DECLARE_BITMAP(hub_irq_map, IP27_HUB_IRQ_COUNT);

static DEFINE_PER_CPU(unsigned long [2], irq_enable_mask);

static inline int alloc_level(void)
{
	int level;

again:
	level = find_first_zero_bit(hub_irq_map, IP27_HUB_IRQ_COUNT);
	if (level >= IP27_HUB_IRQ_COUNT)
		return -ENOSPC;

	if (test_and_set_bit(level, hub_irq_map))
		goto again;

	return level;
}

static void enable_hub_irq(struct irq_data *d)
{
	struct hub_irq_data *hd = irq_data_get_irq_chip_data(d);
	unsigned long *mask = per_cpu(irq_enable_mask, hd->cpu);

	set_bit(d->hwirq, mask);
	__raw_writeq(mask[0], hd->irq_mask[0]);
	__raw_writeq(mask[1], hd->irq_mask[1]);
}

static void disable_hub_irq(struct irq_data *d)
{
	struct hub_irq_data *hd = irq_data_get_irq_chip_data(d);
	unsigned long *mask = per_cpu(irq_enable_mask, hd->cpu);

	clear_bit(d->hwirq, mask);
	__raw_writeq(mask[0], hd->irq_mask[0]);
	__raw_writeq(mask[1], hd->irq_mask[1]);
}

static void setup_hub_mask(struct hub_irq_data *hd, const struct cpumask *mask)
{
	nasid_t nasid;
	int cpu;

	cpu = cpumask_first_and(mask, cpu_online_mask);
	if (cpu >= nr_cpu_ids)
		cpu = cpumask_any(cpu_online_mask);

	nasid = cpu_to_node(cpu);
	hd->cpu = cpu;
	if (!cputoslice(cpu)) {
		hd->irq_mask[0] = REMOTE_HUB_PTR(nasid, PI_INT_MASK0_A);
		hd->irq_mask[1] = REMOTE_HUB_PTR(nasid, PI_INT_MASK1_A);
	} else {
		hd->irq_mask[0] = REMOTE_HUB_PTR(nasid, PI_INT_MASK0_B);
		hd->irq_mask[1] = REMOTE_HUB_PTR(nasid, PI_INT_MASK1_B);
	}
}

static int set_affinity_hub_irq(struct irq_data *d, const struct cpumask *mask,
				bool force)
{
	struct hub_irq_data *hd = irq_data_get_irq_chip_data(d);

	if (!hd)
		return -EINVAL;

	if (irqd_is_started(d))
		disable_hub_irq(d);

	setup_hub_mask(hd, mask);

	if (irqd_is_started(d))
		enable_hub_irq(d);

	irq_data_update_effective_affinity(d, cpumask_of(hd->cpu));

	return 0;
}

static struct irq_chip hub_irq_type = {
	.name		  = "HUB",
	.irq_mask	  = disable_hub_irq,
	.irq_unmask	  = enable_hub_irq,
	.irq_set_affinity = set_affinity_hub_irq,
};

static int hub_domain_alloc(struct irq_domain *domain, unsigned int virq,
			    unsigned int nr_irqs, void *arg)
{
	struct irq_alloc_info *info = arg;
	struct hub_irq_data *hd;
	struct hub_data *hub;
	struct irq_desc *desc;
	int swlevel;

	if (nr_irqs > 1 || !info)
		return -EINVAL;

	hd = kzalloc(sizeof(*hd), GFP_KERNEL);
	if (!hd)
		return -ENOMEM;

	swlevel = alloc_level();
	if (unlikely(swlevel < 0)) {
		kfree(hd);
		return -EAGAIN;
	}
	irq_domain_set_info(domain, virq, swlevel, &hub_irq_type, hd,
			    handle_level_irq, NULL, NULL);

	/* use CPU connected to nearest hub */
	hub = hub_data(info->nasid);
	setup_hub_mask(hd, &hub->h_cpus);
	info->nasid = cpu_to_node(hd->cpu);

	/* Make sure it's not already pending when we connect it. */
	REMOTE_HUB_CLR_INTR(info->nasid, swlevel);

	desc = irq_to_desc(virq);
	desc->irq_common_data.node = info->nasid;
	cpumask_copy(desc->irq_common_data.affinity, &hub->h_cpus);

	return 0;
}

static void hub_domain_free(struct irq_domain *domain,
			    unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *irqd;

	if (nr_irqs > 1)
		return;

	irqd = irq_domain_get_irq_data(domain, virq);
	if (irqd && irqd->chip_data)
		kfree(irqd->chip_data);
}

static const struct irq_domain_ops hub_domain_ops = {
	.alloc = hub_domain_alloc,
	.free  = hub_domain_free,
};

/*
 * This code is unnecessarily complex, because we do
 * intr enabling. Basically, once we grab the set of intrs we need
 * to service, we must mask _all_ these interrupts; firstly, to make
 * sure the same intr does not intr again, causing recursion that
 * can lead to stack overflow. Secondly, we can not just mask the
 * one intr we are do_IRQing, because the non-masked intrs in the
 * first set might intr again, causing multiple servicings of the
 * same intr. This effect is mostly seen for intercpu intrs.
 * Kanoj 05.13.00
 */

static void ip27_do_irq_mask0(struct irq_desc *desc)
{
	cpuid_t cpu = smp_processor_id();
	unsigned long *mask = per_cpu(irq_enable_mask, cpu);
	struct irq_domain *domain;
	u64 pend0;
	int irq;

	/* copied from Irix intpend0() */
	pend0 = LOCAL_HUB_L(PI_INT_PEND0);

	pend0 &= mask[0];		/* Pick intrs we should look at */
	if (!pend0)
		return;

#ifdef CONFIG_SMP
	if (pend0 & (1UL << CPU_RESCHED_A_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_A_IRQ);
		scheduler_ipi();
	} else if (pend0 & (1UL << CPU_RESCHED_B_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_B_IRQ);
		scheduler_ipi();
	} else if (pend0 & (1UL << CPU_CALL_A_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_CALL_A_IRQ);
		generic_smp_call_function_interrupt();
	} else if (pend0 & (1UL << CPU_CALL_B_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_CALL_B_IRQ);
		generic_smp_call_function_interrupt();
	} else
#endif
	{
		domain = irq_desc_get_handler_data(desc);
		irq = irq_linear_revmap(domain, __ffs(pend0));
		if (irq)
			generic_handle_irq(irq);
		else
			spurious_interrupt();
	}

	LOCAL_HUB_L(PI_INT_PEND0);
}

static void ip27_do_irq_mask1(struct irq_desc *desc)
{
	cpuid_t cpu = smp_processor_id();
	unsigned long *mask = per_cpu(irq_enable_mask, cpu);
	struct irq_domain *domain;
	u64 pend1;
	int irq;

	/* copied from Irix intpend0() */
	pend1 = LOCAL_HUB_L(PI_INT_PEND1);

	pend1 &= mask[1];		/* Pick intrs we should look at */
	if (!pend1)
		return;

	domain = irq_desc_get_handler_data(desc);
	irq = irq_linear_revmap(domain, __ffs(pend1) + 64);
	if (irq)
		generic_handle_irq(irq);
	else
		spurious_interrupt();

	LOCAL_HUB_L(PI_INT_PEND1);
}

void install_ipi(void)
{
	int cpu = smp_processor_id();
	unsigned long *mask = per_cpu(irq_enable_mask, cpu);
	int slice = LOCAL_HUB_L(PI_CPU_NUM);
	int resched, call;

	resched = CPU_RESCHED_A_IRQ + slice;
	set_bit(resched, mask);
	LOCAL_HUB_CLR_INTR(resched);

	call = CPU_CALL_A_IRQ + slice;
	set_bit(call, mask);
	LOCAL_HUB_CLR_INTR(call);

	if (slice == 0) {
		LOCAL_HUB_S(PI_INT_MASK0_A, mask[0]);
		LOCAL_HUB_S(PI_INT_MASK1_A, mask[1]);
	} else {
		LOCAL_HUB_S(PI_INT_MASK0_B, mask[0]);
		LOCAL_HUB_S(PI_INT_MASK1_B, mask[1]);
	}
}

void __init arch_init_irq(void)
{
	struct irq_domain *domain;
	struct fwnode_handle *fn;
	int i;

	mips_cpu_irq_init();

	/*
	 * Some interrupts are reserved by hardware or by software convention.
	 * Mark these as reserved right away so they won't be used accidentally
	 * later.
	 */
	for (i = 0; i <= CPU_CALL_B_IRQ; i++)
		set_bit(i, hub_irq_map);

	for (i = NI_BRDCAST_ERR_A; i <= MSC_PANIC_INTR; i++)
		set_bit(i, hub_irq_map);

	fn = irq_domain_alloc_named_fwnode("HUB");
	WARN_ON(fn == NULL);
	if (!fn)
		return;
	domain = irq_domain_create_linear(fn, IP27_HUB_IRQ_COUNT,
					  &hub_domain_ops, NULL);
	WARN_ON(domain == NULL);
	if (!domain)
		return;

	irq_set_default_host(domain);

	irq_set_percpu_devid(IP27_HUB_PEND0_IRQ);
	irq_set_chained_handler_and_data(IP27_HUB_PEND0_IRQ, ip27_do_irq_mask0,
					 domain);
	irq_set_percpu_devid(IP27_HUB_PEND1_IRQ);
	irq_set_chained_handler_and_data(IP27_HUB_PEND1_IRQ, ip27_do_irq_mask1,
					 domain);
}
