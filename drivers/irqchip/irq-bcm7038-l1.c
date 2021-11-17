// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom BCM7038 style Level 1 interrupt controller driver
 *
 * Copyright (C) 2014 Broadcom Corporation
 * Author: Kevin Cernekee
 */

#define pr_fmt(fmt)	KBUILD_MODNAME	": " fmt

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/syscore_ops.h>
#ifdef CONFIG_ARM
#include <asm/smp_plat.h>
#endif

#define IRQS_PER_WORD		32
#define REG_BYTES_PER_IRQ_WORD	(sizeof(u32) * 4)
#define MAX_WORDS		8

struct bcm7038_l1_cpu;

struct bcm7038_l1_chip {
	raw_spinlock_t		lock;
	unsigned int		n_words;
	struct irq_domain	*domain;
	struct bcm7038_l1_cpu	*cpus[NR_CPUS];
#ifdef CONFIG_PM_SLEEP
	struct list_head	list;
	u32			wake_mask[MAX_WORDS];
#endif
	u32			irq_fwd_mask[MAX_WORDS];
	u8			affinity[MAX_WORDS * IRQS_PER_WORD];
};

struct bcm7038_l1_cpu {
	void __iomem		*map_base;
	u32			mask_cache[];
};

/*
 * STATUS/MASK_STATUS/MASK_SET/MASK_CLEAR are packed one right after another:
 *
 * 7038:
 *   0x1000_1400: W0_STATUS
 *   0x1000_1404: W1_STATUS
 *   0x1000_1408: W0_MASK_STATUS
 *   0x1000_140c: W1_MASK_STATUS
 *   0x1000_1410: W0_MASK_SET
 *   0x1000_1414: W1_MASK_SET
 *   0x1000_1418: W0_MASK_CLEAR
 *   0x1000_141c: W1_MASK_CLEAR
 *
 * 7445:
 *   0xf03e_1500: W0_STATUS
 *   0xf03e_1504: W1_STATUS
 *   0xf03e_1508: W2_STATUS
 *   0xf03e_150c: W3_STATUS
 *   0xf03e_1510: W4_STATUS
 *   0xf03e_1514: W0_MASK_STATUS
 *   0xf03e_1518: W1_MASK_STATUS
 *   [...]
 */

static inline unsigned int reg_status(struct bcm7038_l1_chip *intc,
				      unsigned int word)
{
	return (0 * intc->n_words + word) * sizeof(u32);
}

static inline unsigned int reg_mask_status(struct bcm7038_l1_chip *intc,
					   unsigned int word)
{
	return (1 * intc->n_words + word) * sizeof(u32);
}

static inline unsigned int reg_mask_set(struct bcm7038_l1_chip *intc,
					unsigned int word)
{
	return (2 * intc->n_words + word) * sizeof(u32);
}

static inline unsigned int reg_mask_clr(struct bcm7038_l1_chip *intc,
					unsigned int word)
{
	return (3 * intc->n_words + word) * sizeof(u32);
}

static inline u32 l1_readl(void __iomem *reg)
{
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return ioread32be(reg);
	else
		return readl(reg);
}

static inline void l1_writel(u32 val, void __iomem *reg)
{
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		iowrite32be(val, reg);
	else
		writel(val, reg);
}

static void bcm7038_l1_irq_handle(struct irq_desc *desc)
{
	struct bcm7038_l1_chip *intc = irq_desc_get_handler_data(desc);
	struct bcm7038_l1_cpu *cpu;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int idx;

#ifdef CONFIG_SMP
	cpu = intc->cpus[cpu_logical_map(smp_processor_id())];
#else
	cpu = intc->cpus[0];
#endif

	chained_irq_enter(chip, desc);

	for (idx = 0; idx < intc->n_words; idx++) {
		int base = idx * IRQS_PER_WORD;
		unsigned long pending, flags;
		int hwirq;

		raw_spin_lock_irqsave(&intc->lock, flags);
		pending = l1_readl(cpu->map_base + reg_status(intc, idx)) &
			  ~cpu->mask_cache[idx];
		raw_spin_unlock_irqrestore(&intc->lock, flags);

		for_each_set_bit(hwirq, &pending, IRQS_PER_WORD)
			generic_handle_domain_irq(intc->domain, base + hwirq);
	}

	chained_irq_exit(chip, desc);
}

static void __bcm7038_l1_unmask(struct irq_data *d, unsigned int cpu_idx)
{
	struct bcm7038_l1_chip *intc = irq_data_get_irq_chip_data(d);
	u32 word = d->hwirq / IRQS_PER_WORD;
	u32 mask = BIT(d->hwirq % IRQS_PER_WORD);

	intc->cpus[cpu_idx]->mask_cache[word] &= ~mask;
	l1_writel(mask, intc->cpus[cpu_idx]->map_base +
			reg_mask_clr(intc, word));
}

static void __bcm7038_l1_mask(struct irq_data *d, unsigned int cpu_idx)
{
	struct bcm7038_l1_chip *intc = irq_data_get_irq_chip_data(d);
	u32 word = d->hwirq / IRQS_PER_WORD;
	u32 mask = BIT(d->hwirq % IRQS_PER_WORD);

	intc->cpus[cpu_idx]->mask_cache[word] |= mask;
	l1_writel(mask, intc->cpus[cpu_idx]->map_base +
			reg_mask_set(intc, word));
}

static void bcm7038_l1_unmask(struct irq_data *d)
{
	struct bcm7038_l1_chip *intc = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	raw_spin_lock_irqsave(&intc->lock, flags);
	__bcm7038_l1_unmask(d, intc->affinity[d->hwirq]);
	raw_spin_unlock_irqrestore(&intc->lock, flags);
}

static void bcm7038_l1_mask(struct irq_data *d)
{
	struct bcm7038_l1_chip *intc = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	raw_spin_lock_irqsave(&intc->lock, flags);
	__bcm7038_l1_mask(d, intc->affinity[d->hwirq]);
	raw_spin_unlock_irqrestore(&intc->lock, flags);
}

static int bcm7038_l1_set_affinity(struct irq_data *d,
				   const struct cpumask *dest,
				   bool force)
{
	struct bcm7038_l1_chip *intc = irq_data_get_irq_chip_data(d);
	unsigned long flags;
	irq_hw_number_t hw = d->hwirq;
	u32 word = hw / IRQS_PER_WORD;
	u32 mask = BIT(hw % IRQS_PER_WORD);
	unsigned int first_cpu = cpumask_any_and(dest, cpu_online_mask);
	bool was_disabled;

	raw_spin_lock_irqsave(&intc->lock, flags);

	was_disabled = !!(intc->cpus[intc->affinity[hw]]->mask_cache[word] &
			  mask);
	__bcm7038_l1_mask(d, intc->affinity[hw]);
	intc->affinity[hw] = first_cpu;
	if (!was_disabled)
		__bcm7038_l1_unmask(d, first_cpu);

	raw_spin_unlock_irqrestore(&intc->lock, flags);
	irq_data_update_effective_affinity(d, cpumask_of(first_cpu));

	return 0;
}

#ifdef CONFIG_SMP
static void bcm7038_l1_cpu_offline(struct irq_data *d)
{
	struct cpumask *mask = irq_data_get_affinity_mask(d);
	int cpu = smp_processor_id();
	cpumask_t new_affinity;

	/* This CPU was not on the affinity mask */
	if (!cpumask_test_cpu(cpu, mask))
		return;

	if (cpumask_weight(mask) > 1) {
		/*
		 * Multiple CPU affinity, remove this CPU from the affinity
		 * mask
		 */
		cpumask_copy(&new_affinity, mask);
		cpumask_clear_cpu(cpu, &new_affinity);
	} else {
		/* Only CPU, put on the lowest online CPU */
		cpumask_clear(&new_affinity);
		cpumask_set_cpu(cpumask_first(cpu_online_mask), &new_affinity);
	}
	irq_set_affinity_locked(d, &new_affinity, false);
}
#endif

static int __init bcm7038_l1_init_one(struct device_node *dn,
				      unsigned int idx,
				      struct bcm7038_l1_chip *intc)
{
	struct resource res;
	resource_size_t sz;
	struct bcm7038_l1_cpu *cpu;
	unsigned int i, n_words, parent_irq;
	int ret;

	if (of_address_to_resource(dn, idx, &res))
		return -EINVAL;
	sz = resource_size(&res);
	n_words = sz / REG_BYTES_PER_IRQ_WORD;

	if (n_words > MAX_WORDS)
		return -EINVAL;
	else if (!intc->n_words)
		intc->n_words = n_words;
	else if (intc->n_words != n_words)
		return -EINVAL;

	ret = of_property_read_u32_array(dn , "brcm,int-fwd-mask",
					 intc->irq_fwd_mask, n_words);
	if (ret != 0 && ret != -EINVAL) {
		/* property exists but has the wrong number of words */
		pr_err("invalid brcm,int-fwd-mask property\n");
		return -EINVAL;
	}

	cpu = intc->cpus[idx] = kzalloc(sizeof(*cpu) + n_words * sizeof(u32),
					GFP_KERNEL);
	if (!cpu)
		return -ENOMEM;

	cpu->map_base = ioremap(res.start, sz);
	if (!cpu->map_base)
		return -ENOMEM;

	for (i = 0; i < n_words; i++) {
		l1_writel(~intc->irq_fwd_mask[i],
			  cpu->map_base + reg_mask_set(intc, i));
		l1_writel(intc->irq_fwd_mask[i],
			  cpu->map_base + reg_mask_clr(intc, i));
		cpu->mask_cache[i] = ~intc->irq_fwd_mask[i];
	}

	parent_irq = irq_of_parse_and_map(dn, idx);
	if (!parent_irq) {
		pr_err("failed to map parent interrupt %d\n", parent_irq);
		return -EINVAL;
	}

	if (of_property_read_bool(dn, "brcm,irq-can-wake"))
		enable_irq_wake(parent_irq);

	irq_set_chained_handler_and_data(parent_irq, bcm7038_l1_irq_handle,
					 intc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * We keep a list of bcm7038_l1_chip used for suspend/resume. This hack is
 * used because the struct chip_type suspend/resume hooks are not called
 * unless chip_type is hooked onto a generic_chip. Since this driver does
 * not use generic_chip, we need to manually hook our resume/suspend to
 * syscore_ops.
 */
static LIST_HEAD(bcm7038_l1_intcs_list);
static DEFINE_RAW_SPINLOCK(bcm7038_l1_intcs_lock);

static int bcm7038_l1_suspend(void)
{
	struct bcm7038_l1_chip *intc;
	int boot_cpu, word;
	u32 val;

	/* Wakeup interrupt should only come from the boot cpu */
#ifdef CONFIG_SMP
	boot_cpu = cpu_logical_map(0);
#else
	boot_cpu = 0;
#endif

	list_for_each_entry(intc, &bcm7038_l1_intcs_list, list) {
		for (word = 0; word < intc->n_words; word++) {
			val = intc->wake_mask[word] | intc->irq_fwd_mask[word];
			l1_writel(~val,
				intc->cpus[boot_cpu]->map_base + reg_mask_set(intc, word));
			l1_writel(val,
				intc->cpus[boot_cpu]->map_base + reg_mask_clr(intc, word));
		}
	}

	return 0;
}

static void bcm7038_l1_resume(void)
{
	struct bcm7038_l1_chip *intc;
	int boot_cpu, word;

#ifdef CONFIG_SMP
	boot_cpu = cpu_logical_map(0);
#else
	boot_cpu = 0;
#endif

	list_for_each_entry(intc, &bcm7038_l1_intcs_list, list) {
		for (word = 0; word < intc->n_words; word++) {
			l1_writel(intc->cpus[boot_cpu]->mask_cache[word],
				intc->cpus[boot_cpu]->map_base + reg_mask_set(intc, word));
			l1_writel(~intc->cpus[boot_cpu]->mask_cache[word],
				intc->cpus[boot_cpu]->map_base + reg_mask_clr(intc, word));
		}
	}
}

static struct syscore_ops bcm7038_l1_syscore_ops = {
	.suspend	= bcm7038_l1_suspend,
	.resume		= bcm7038_l1_resume,
};

static int bcm7038_l1_set_wake(struct irq_data *d, unsigned int on)
{
	struct bcm7038_l1_chip *intc = irq_data_get_irq_chip_data(d);
	unsigned long flags;
	u32 word = d->hwirq / IRQS_PER_WORD;
	u32 mask = BIT(d->hwirq % IRQS_PER_WORD);

	raw_spin_lock_irqsave(&intc->lock, flags);
	if (on)
		intc->wake_mask[word] |= mask;
	else
		intc->wake_mask[word] &= ~mask;
	raw_spin_unlock_irqrestore(&intc->lock, flags);

	return 0;
}
#endif

static struct irq_chip bcm7038_l1_irq_chip = {
	.name			= "bcm7038-l1",
	.irq_mask		= bcm7038_l1_mask,
	.irq_unmask		= bcm7038_l1_unmask,
	.irq_set_affinity	= bcm7038_l1_set_affinity,
#ifdef CONFIG_SMP
	.irq_cpu_offline	= bcm7038_l1_cpu_offline,
#endif
#ifdef CONFIG_PM_SLEEP
	.irq_set_wake		= bcm7038_l1_set_wake,
#endif
};

static int bcm7038_l1_map(struct irq_domain *d, unsigned int virq,
			  irq_hw_number_t hw_irq)
{
	struct bcm7038_l1_chip *intc = d->host_data;
	u32 mask = BIT(hw_irq % IRQS_PER_WORD);
	u32 word = hw_irq / IRQS_PER_WORD;

	if (intc->irq_fwd_mask[word] & mask)
		return -EPERM;

	irq_set_chip_and_handler(virq, &bcm7038_l1_irq_chip, handle_level_irq);
	irq_set_chip_data(virq, d->host_data);
	irqd_set_single_target(irq_desc_get_irq_data(irq_to_desc(virq)));
	return 0;
}

static const struct irq_domain_ops bcm7038_l1_domain_ops = {
	.xlate			= irq_domain_xlate_onecell,
	.map			= bcm7038_l1_map,
};

static int __init bcm7038_l1_of_init(struct device_node *dn,
			      struct device_node *parent)
{
	struct bcm7038_l1_chip *intc;
	int idx, ret;

	intc = kzalloc(sizeof(*intc), GFP_KERNEL);
	if (!intc)
		return -ENOMEM;

	raw_spin_lock_init(&intc->lock);
	for_each_possible_cpu(idx) {
		ret = bcm7038_l1_init_one(dn, idx, intc);
		if (ret < 0) {
			if (idx)
				break;
			pr_err("failed to remap intc L1 registers\n");
			goto out_free;
		}
	}

	intc->domain = irq_domain_add_linear(dn, IRQS_PER_WORD * intc->n_words,
					     &bcm7038_l1_domain_ops,
					     intc);
	if (!intc->domain) {
		ret = -ENOMEM;
		goto out_unmap;
	}

#ifdef CONFIG_PM_SLEEP
	/* Add bcm7038_l1_chip into a list */
	raw_spin_lock(&bcm7038_l1_intcs_lock);
	list_add_tail(&intc->list, &bcm7038_l1_intcs_list);
	raw_spin_unlock(&bcm7038_l1_intcs_lock);

	if (list_is_singular(&bcm7038_l1_intcs_list))
		register_syscore_ops(&bcm7038_l1_syscore_ops);
#endif

	pr_info("registered BCM7038 L1 intc (%pOF, IRQs: %d)\n",
		dn, IRQS_PER_WORD * intc->n_words);

	return 0;

out_unmap:
	for_each_possible_cpu(idx) {
		struct bcm7038_l1_cpu *cpu = intc->cpus[idx];

		if (cpu) {
			if (cpu->map_base)
				iounmap(cpu->map_base);
			kfree(cpu);
		}
	}
out_free:
	kfree(intc);
	return ret;
}

IRQCHIP_DECLARE(bcm7038_l1, "brcm,bcm7038-l1-intc", bcm7038_l1_of_init);
