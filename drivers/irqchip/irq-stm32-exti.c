// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Maxime Coquelin 2015
 * Copyright (C) STMicroelectronics 2017
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define IRQS_PER_BANK 32

struct stm32_exti_bank {
	u32 imr_ofst;
	u32 emr_ofst;
	u32 rtsr_ofst;
	u32 ftsr_ofst;
	u32 swier_ofst;
	u32 rpr_ofst;
	u32 fpr_ofst;
};

#define UNDEF_REG ~0

struct stm32_desc_irq {
	u32 exti;
	u32 irq_parent;
};

struct stm32_exti_drv_data {
	const struct stm32_exti_bank **exti_banks;
	const struct stm32_desc_irq *desc_irqs;
	u32 bank_nr;
	u32 irq_nr;
};

struct stm32_exti_chip_data {
	struct stm32_exti_host_data *host_data;
	const struct stm32_exti_bank *reg_bank;
	struct raw_spinlock rlock;
	u32 wake_active;
	u32 mask_cache;
	u32 rtsr_cache;
	u32 ftsr_cache;
};

struct stm32_exti_host_data {
	void __iomem *base;
	struct stm32_exti_chip_data *chips_data;
	const struct stm32_exti_drv_data *drv_data;
};

static struct stm32_exti_host_data *stm32_host_data;

static const struct stm32_exti_bank stm32f4xx_exti_b1 = {
	.imr_ofst	= 0x00,
	.emr_ofst	= 0x04,
	.rtsr_ofst	= 0x08,
	.ftsr_ofst	= 0x0C,
	.swier_ofst	= 0x10,
	.rpr_ofst	= 0x14,
	.fpr_ofst	= UNDEF_REG,
};

static const struct stm32_exti_bank *stm32f4xx_exti_banks[] = {
	&stm32f4xx_exti_b1,
};

static const struct stm32_exti_drv_data stm32f4xx_drv_data = {
	.exti_banks = stm32f4xx_exti_banks,
	.bank_nr = ARRAY_SIZE(stm32f4xx_exti_banks),
};

static const struct stm32_exti_bank stm32h7xx_exti_b1 = {
	.imr_ofst	= 0x80,
	.emr_ofst	= 0x84,
	.rtsr_ofst	= 0x00,
	.ftsr_ofst	= 0x04,
	.swier_ofst	= 0x08,
	.rpr_ofst	= 0x88,
	.fpr_ofst	= UNDEF_REG,
};

static const struct stm32_exti_bank stm32h7xx_exti_b2 = {
	.imr_ofst	= 0x90,
	.emr_ofst	= 0x94,
	.rtsr_ofst	= 0x20,
	.ftsr_ofst	= 0x24,
	.swier_ofst	= 0x28,
	.rpr_ofst	= 0x98,
	.fpr_ofst	= UNDEF_REG,
};

static const struct stm32_exti_bank stm32h7xx_exti_b3 = {
	.imr_ofst	= 0xA0,
	.emr_ofst	= 0xA4,
	.rtsr_ofst	= 0x40,
	.ftsr_ofst	= 0x44,
	.swier_ofst	= 0x48,
	.rpr_ofst	= 0xA8,
	.fpr_ofst	= UNDEF_REG,
};

static const struct stm32_exti_bank *stm32h7xx_exti_banks[] = {
	&stm32h7xx_exti_b1,
	&stm32h7xx_exti_b2,
	&stm32h7xx_exti_b3,
};

static const struct stm32_exti_drv_data stm32h7xx_drv_data = {
	.exti_banks = stm32h7xx_exti_banks,
	.bank_nr = ARRAY_SIZE(stm32h7xx_exti_banks),
};

static const struct stm32_exti_bank stm32mp1_exti_b1 = {
	.imr_ofst	= 0x80,
	.emr_ofst	= 0x84,
	.rtsr_ofst	= 0x00,
	.ftsr_ofst	= 0x04,
	.swier_ofst	= 0x08,
	.rpr_ofst	= 0x0C,
	.fpr_ofst	= 0x10,
};

static const struct stm32_exti_bank stm32mp1_exti_b2 = {
	.imr_ofst	= 0x90,
	.emr_ofst	= 0x94,
	.rtsr_ofst	= 0x20,
	.ftsr_ofst	= 0x24,
	.swier_ofst	= 0x28,
	.rpr_ofst	= 0x2C,
	.fpr_ofst	= 0x30,
};

static const struct stm32_exti_bank stm32mp1_exti_b3 = {
	.imr_ofst	= 0xA0,
	.emr_ofst	= 0xA4,
	.rtsr_ofst	= 0x40,
	.ftsr_ofst	= 0x44,
	.swier_ofst	= 0x48,
	.rpr_ofst	= 0x4C,
	.fpr_ofst	= 0x50,
};

static const struct stm32_exti_bank *stm32mp1_exti_banks[] = {
	&stm32mp1_exti_b1,
	&stm32mp1_exti_b2,
	&stm32mp1_exti_b3,
};

static const struct stm32_desc_irq stm32mp1_desc_irq[] = {
	{ .exti = 0, .irq_parent = 6 },
	{ .exti = 1, .irq_parent = 7 },
	{ .exti = 2, .irq_parent = 8 },
	{ .exti = 3, .irq_parent = 9 },
	{ .exti = 4, .irq_parent = 10 },
	{ .exti = 5, .irq_parent = 23 },
	{ .exti = 6, .irq_parent = 64 },
	{ .exti = 7, .irq_parent = 65 },
	{ .exti = 8, .irq_parent = 66 },
	{ .exti = 9, .irq_parent = 67 },
	{ .exti = 10, .irq_parent = 40 },
	{ .exti = 11, .irq_parent = 42 },
	{ .exti = 12, .irq_parent = 76 },
	{ .exti = 13, .irq_parent = 77 },
	{ .exti = 14, .irq_parent = 121 },
	{ .exti = 15, .irq_parent = 127 },
	{ .exti = 16, .irq_parent = 1 },
	{ .exti = 65, .irq_parent = 144 },
	{ .exti = 68, .irq_parent = 143 },
	{ .exti = 73, .irq_parent = 129 },
};

static const struct stm32_exti_drv_data stm32mp1_drv_data = {
	.exti_banks = stm32mp1_exti_banks,
	.bank_nr = ARRAY_SIZE(stm32mp1_exti_banks),
	.desc_irqs = stm32mp1_desc_irq,
	.irq_nr = ARRAY_SIZE(stm32mp1_desc_irq),
};

static int stm32_exti_to_irq(const struct stm32_exti_drv_data *drv_data,
			     irq_hw_number_t hwirq)
{
	const struct stm32_desc_irq *desc_irq;
	int i;

	if (!drv_data->desc_irqs)
		return -EINVAL;

	for (i = 0; i < drv_data->irq_nr; i++) {
		desc_irq = &drv_data->desc_irqs[i];
		if (desc_irq->exti == hwirq)
			return desc_irq->irq_parent;
	}

	return -EINVAL;
}

static unsigned long stm32_exti_pending(struct irq_chip_generic *gc)
{
	struct stm32_exti_chip_data *chip_data = gc->private;
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;
	unsigned long pending;

	pending = irq_reg_readl(gc, stm32_bank->rpr_ofst);
	if (stm32_bank->fpr_ofst != UNDEF_REG)
		pending |= irq_reg_readl(gc, stm32_bank->fpr_ofst);

	return pending;
}

static void stm32_irq_handler(struct irq_desc *desc)
{
	struct irq_domain *domain = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int virq, nbanks = domain->gc->num_chips;
	struct irq_chip_generic *gc;
	unsigned long pending;
	int n, i, irq_base = 0;

	chained_irq_enter(chip, desc);

	for (i = 0; i < nbanks; i++, irq_base += IRQS_PER_BANK) {
		gc = irq_get_domain_generic_chip(domain, irq_base);

		while ((pending = stm32_exti_pending(gc))) {
			for_each_set_bit(n, &pending, IRQS_PER_BANK) {
				virq = irq_find_mapping(domain, irq_base + n);
				generic_handle_irq(virq);
			}
		}
	}

	chained_irq_exit(chip, desc);
}

static int stm32_exti_set_type(struct irq_data *d,
			       unsigned int type, u32 *rtsr, u32 *ftsr)
{
	u32 mask = BIT(d->hwirq % IRQS_PER_BANK);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		*rtsr |= mask;
		*ftsr &= ~mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		*rtsr &= ~mask;
		*ftsr |= mask;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		*rtsr |= mask;
		*ftsr |= mask;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int stm32_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct stm32_exti_chip_data *chip_data = gc->private;
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;
	u32 rtsr, ftsr;
	int err;

	irq_gc_lock(gc);

	rtsr = irq_reg_readl(gc, stm32_bank->rtsr_ofst);
	ftsr = irq_reg_readl(gc, stm32_bank->ftsr_ofst);

	err = stm32_exti_set_type(d, type, &rtsr, &ftsr);
	if (err) {
		irq_gc_unlock(gc);
		return err;
	}

	irq_reg_writel(gc, rtsr, stm32_bank->rtsr_ofst);
	irq_reg_writel(gc, ftsr, stm32_bank->ftsr_ofst);

	irq_gc_unlock(gc);

	return 0;
}

static void stm32_chip_suspend(struct stm32_exti_chip_data *chip_data,
			       u32 wake_active)
{
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;
	void __iomem *base = chip_data->host_data->base;

	/* save rtsr, ftsr registers */
	chip_data->rtsr_cache = readl_relaxed(base + stm32_bank->rtsr_ofst);
	chip_data->ftsr_cache = readl_relaxed(base + stm32_bank->ftsr_ofst);

	writel_relaxed(wake_active, base + stm32_bank->imr_ofst);
}

static void stm32_chip_resume(struct stm32_exti_chip_data *chip_data,
			      u32 mask_cache)
{
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;
	void __iomem *base = chip_data->host_data->base;

	/* restore rtsr, ftsr, registers */
	writel_relaxed(chip_data->rtsr_cache, base + stm32_bank->rtsr_ofst);
	writel_relaxed(chip_data->ftsr_cache, base + stm32_bank->ftsr_ofst);

	writel_relaxed(mask_cache, base + stm32_bank->imr_ofst);
}

static void stm32_irq_suspend(struct irq_chip_generic *gc)
{
	struct stm32_exti_chip_data *chip_data = gc->private;

	irq_gc_lock(gc);
	stm32_chip_suspend(chip_data, gc->wake_active);
	irq_gc_unlock(gc);
}

static void stm32_irq_resume(struct irq_chip_generic *gc)
{
	struct stm32_exti_chip_data *chip_data = gc->private;

	irq_gc_lock(gc);
	stm32_chip_resume(chip_data, gc->mask_cache);
	irq_gc_unlock(gc);
}

static int stm32_exti_alloc(struct irq_domain *d, unsigned int virq,
			    unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	irq_hw_number_t hwirq;

	hwirq = fwspec->param[0];

	irq_map_generic_chip(d, virq, hwirq);

	return 0;
}

static void stm32_exti_free(struct irq_domain *d, unsigned int virq,
			    unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(d, virq);

	irq_domain_reset_irq_data(data);
}

static const struct irq_domain_ops irq_exti_domain_ops = {
	.map	= irq_map_generic_chip,
	.alloc  = stm32_exti_alloc,
	.free	= stm32_exti_free,
};

static void stm32_irq_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct stm32_exti_chip_data *chip_data = gc->private;
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;

	irq_gc_lock(gc);

	irq_reg_writel(gc, d->mask, stm32_bank->rpr_ofst);
	if (stm32_bank->fpr_ofst != UNDEF_REG)
		irq_reg_writel(gc, d->mask, stm32_bank->fpr_ofst);

	irq_gc_unlock(gc);
}

static inline u32 stm32_exti_set_bit(struct irq_data *d, u32 reg)
{
	struct stm32_exti_chip_data *chip_data = irq_data_get_irq_chip_data(d);
	void __iomem *base = chip_data->host_data->base;
	u32 val;

	val = readl_relaxed(base + reg);
	val |= BIT(d->hwirq % IRQS_PER_BANK);
	writel_relaxed(val, base + reg);

	return val;
}

static inline u32 stm32_exti_clr_bit(struct irq_data *d, u32 reg)
{
	struct stm32_exti_chip_data *chip_data = irq_data_get_irq_chip_data(d);
	void __iomem *base = chip_data->host_data->base;
	u32 val;

	val = readl_relaxed(base + reg);
	val &= ~BIT(d->hwirq % IRQS_PER_BANK);
	writel_relaxed(val, base + reg);

	return val;
}

static void stm32_exti_h_eoi(struct irq_data *d)
{
	struct stm32_exti_chip_data *chip_data = irq_data_get_irq_chip_data(d);
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;

	raw_spin_lock(&chip_data->rlock);

	stm32_exti_set_bit(d, stm32_bank->rpr_ofst);
	if (stm32_bank->fpr_ofst != UNDEF_REG)
		stm32_exti_set_bit(d, stm32_bank->fpr_ofst);

	raw_spin_unlock(&chip_data->rlock);

	if (d->parent_data->chip)
		irq_chip_eoi_parent(d);
}

static void stm32_exti_h_mask(struct irq_data *d)
{
	struct stm32_exti_chip_data *chip_data = irq_data_get_irq_chip_data(d);
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;

	raw_spin_lock(&chip_data->rlock);
	chip_data->mask_cache = stm32_exti_clr_bit(d, stm32_bank->imr_ofst);
	raw_spin_unlock(&chip_data->rlock);

	if (d->parent_data->chip)
		irq_chip_mask_parent(d);
}

static void stm32_exti_h_unmask(struct irq_data *d)
{
	struct stm32_exti_chip_data *chip_data = irq_data_get_irq_chip_data(d);
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;

	raw_spin_lock(&chip_data->rlock);
	chip_data->mask_cache = stm32_exti_set_bit(d, stm32_bank->imr_ofst);
	raw_spin_unlock(&chip_data->rlock);

	if (d->parent_data->chip)
		irq_chip_unmask_parent(d);
}

static int stm32_exti_h_set_type(struct irq_data *d, unsigned int type)
{
	struct stm32_exti_chip_data *chip_data = irq_data_get_irq_chip_data(d);
	const struct stm32_exti_bank *stm32_bank = chip_data->reg_bank;
	void __iomem *base = chip_data->host_data->base;
	u32 rtsr, ftsr;
	int err;

	raw_spin_lock(&chip_data->rlock);
	rtsr = readl_relaxed(base + stm32_bank->rtsr_ofst);
	ftsr = readl_relaxed(base + stm32_bank->ftsr_ofst);

	err = stm32_exti_set_type(d, type, &rtsr, &ftsr);
	if (err) {
		raw_spin_unlock(&chip_data->rlock);
		return err;
	}

	writel_relaxed(rtsr, base + stm32_bank->rtsr_ofst);
	writel_relaxed(ftsr, base + stm32_bank->ftsr_ofst);
	raw_spin_unlock(&chip_data->rlock);

	return 0;
}

static int stm32_exti_h_set_wake(struct irq_data *d, unsigned int on)
{
	struct stm32_exti_chip_data *chip_data = irq_data_get_irq_chip_data(d);
	u32 mask = BIT(d->hwirq % IRQS_PER_BANK);

	raw_spin_lock(&chip_data->rlock);

	if (on)
		chip_data->wake_active |= mask;
	else
		chip_data->wake_active &= ~mask;

	raw_spin_unlock(&chip_data->rlock);

	return 0;
}

static int stm32_exti_h_set_affinity(struct irq_data *d,
				     const struct cpumask *dest, bool force)
{
	if (d->parent_data->chip)
		return irq_chip_set_affinity_parent(d, dest, force);

	return -EINVAL;
}

#ifdef CONFIG_PM
static int stm32_exti_h_suspend(void)
{
	struct stm32_exti_chip_data *chip_data;
	int i;

	for (i = 0; i < stm32_host_data->drv_data->bank_nr; i++) {
		chip_data = &stm32_host_data->chips_data[i];
		raw_spin_lock(&chip_data->rlock);
		stm32_chip_suspend(chip_data, chip_data->wake_active);
		raw_spin_unlock(&chip_data->rlock);
	}

	return 0;
}

static void stm32_exti_h_resume(void)
{
	struct stm32_exti_chip_data *chip_data;
	int i;

	for (i = 0; i < stm32_host_data->drv_data->bank_nr; i++) {
		chip_data = &stm32_host_data->chips_data[i];
		raw_spin_lock(&chip_data->rlock);
		stm32_chip_resume(chip_data, chip_data->mask_cache);
		raw_spin_unlock(&chip_data->rlock);
	}
}

static struct syscore_ops stm32_exti_h_syscore_ops = {
	.suspend	= stm32_exti_h_suspend,
	.resume		= stm32_exti_h_resume,
};

static void stm32_exti_h_syscore_init(void)
{
	register_syscore_ops(&stm32_exti_h_syscore_ops);
}
#else
static inline void stm32_exti_h_syscore_init(void) {}
#endif

static struct irq_chip stm32_exti_h_chip = {
	.name			= "stm32-exti-h",
	.irq_eoi		= stm32_exti_h_eoi,
	.irq_mask		= stm32_exti_h_mask,
	.irq_unmask		= stm32_exti_h_unmask,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_type		= stm32_exti_h_set_type,
	.irq_set_wake		= stm32_exti_h_set_wake,
	.flags			= IRQCHIP_MASK_ON_SUSPEND,
	.irq_set_affinity	= IS_ENABLED(CONFIG_SMP) ? stm32_exti_h_set_affinity : NULL,
};

static int stm32_exti_h_domain_alloc(struct irq_domain *dm,
				     unsigned int virq,
				     unsigned int nr_irqs, void *data)
{
	struct stm32_exti_host_data *host_data = dm->host_data;
	struct stm32_exti_chip_data *chip_data;
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec p_fwspec;
	irq_hw_number_t hwirq;
	int p_irq, bank;

	hwirq = fwspec->param[0];
	bank  = hwirq / IRQS_PER_BANK;
	chip_data = &host_data->chips_data[bank];

	irq_domain_set_hwirq_and_chip(dm, virq, hwirq,
				      &stm32_exti_h_chip, chip_data);

	p_irq = stm32_exti_to_irq(host_data->drv_data, hwirq);
	if (p_irq >= 0) {
		p_fwspec.fwnode = dm->parent->fwnode;
		p_fwspec.param_count = 3;
		p_fwspec.param[0] = GIC_SPI;
		p_fwspec.param[1] = p_irq;
		p_fwspec.param[2] = IRQ_TYPE_LEVEL_HIGH;

		return irq_domain_alloc_irqs_parent(dm, virq, 1, &p_fwspec);
	}

	return 0;
}

static struct
stm32_exti_host_data *stm32_exti_host_init(const struct stm32_exti_drv_data *dd,
					   struct device_node *node)
{
	struct stm32_exti_host_data *host_data;

	host_data = kzalloc(sizeof(*host_data), GFP_KERNEL);
	if (!host_data)
		return NULL;

	host_data->drv_data = dd;
	host_data->chips_data = kcalloc(dd->bank_nr,
					sizeof(struct stm32_exti_chip_data),
					GFP_KERNEL);
	if (!host_data->chips_data)
		goto free_host_data;

	host_data->base = of_iomap(node, 0);
	if (!host_data->base) {
		pr_err("%pOF: Unable to map registers\n", node);
		goto free_chips_data;
	}

	stm32_host_data = host_data;

	return host_data;

free_chips_data:
	kfree(host_data->chips_data);
free_host_data:
	kfree(host_data);

	return NULL;
}

static struct
stm32_exti_chip_data *stm32_exti_chip_init(struct stm32_exti_host_data *h_data,
					   u32 bank_idx,
					   struct device_node *node)
{
	const struct stm32_exti_bank *stm32_bank;
	struct stm32_exti_chip_data *chip_data;
	void __iomem *base = h_data->base;
	u32 irqs_mask;

	stm32_bank = h_data->drv_data->exti_banks[bank_idx];
	chip_data = &h_data->chips_data[bank_idx];
	chip_data->host_data = h_data;
	chip_data->reg_bank = stm32_bank;

	raw_spin_lock_init(&chip_data->rlock);

	/* Determine number of irqs supported */
	writel_relaxed(~0UL, base + stm32_bank->rtsr_ofst);
	irqs_mask = readl_relaxed(base + stm32_bank->rtsr_ofst);

	/*
	 * This IP has no reset, so after hot reboot we should
	 * clear registers to avoid residue
	 */
	writel_relaxed(0, base + stm32_bank->imr_ofst);
	writel_relaxed(0, base + stm32_bank->emr_ofst);
	writel_relaxed(0, base + stm32_bank->rtsr_ofst);
	writel_relaxed(0, base + stm32_bank->ftsr_ofst);
	writel_relaxed(~0UL, base + stm32_bank->rpr_ofst);
	if (stm32_bank->fpr_ofst != UNDEF_REG)
		writel_relaxed(~0UL, base + stm32_bank->fpr_ofst);

	pr_info("%s: bank%d, External IRQs available:%#x\n",
		node->full_name, bank_idx, irqs_mask);

	return chip_data;
}

static int __init stm32_exti_init(const struct stm32_exti_drv_data *drv_data,
				  struct device_node *node)
{
	struct stm32_exti_host_data *host_data;
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	int nr_irqs, ret, i;
	struct irq_chip_generic *gc;
	struct irq_domain *domain;

	host_data = stm32_exti_host_init(drv_data, node);
	if (!host_data)
		return -ENOMEM;

	domain = irq_domain_add_linear(node, drv_data->bank_nr * IRQS_PER_BANK,
				       &irq_exti_domain_ops, NULL);
	if (!domain) {
		pr_err("%s: Could not register interrupt domain.\n",
		       node->name);
		ret = -ENOMEM;
		goto out_unmap;
	}

	ret = irq_alloc_domain_generic_chips(domain, IRQS_PER_BANK, 1, "exti",
					     handle_edge_irq, clr, 0, 0);
	if (ret) {
		pr_err("%pOF: Could not allocate generic interrupt chip.\n",
		       node);
		goto out_free_domain;
	}

	for (i = 0; i < drv_data->bank_nr; i++) {
		const struct stm32_exti_bank *stm32_bank;
		struct stm32_exti_chip_data *chip_data;

		stm32_bank = drv_data->exti_banks[i];
		chip_data = stm32_exti_chip_init(host_data, i, node);

		gc = irq_get_domain_generic_chip(domain, i * IRQS_PER_BANK);

		gc->reg_base = host_data->base;
		gc->chip_types->type = IRQ_TYPE_EDGE_BOTH;
		gc->chip_types->chip.irq_ack = stm32_irq_ack;
		gc->chip_types->chip.irq_mask = irq_gc_mask_clr_bit;
		gc->chip_types->chip.irq_unmask = irq_gc_mask_set_bit;
		gc->chip_types->chip.irq_set_type = stm32_irq_set_type;
		gc->chip_types->chip.irq_set_wake = irq_gc_set_wake;
		gc->suspend = stm32_irq_suspend;
		gc->resume = stm32_irq_resume;
		gc->wake_enabled = IRQ_MSK(IRQS_PER_BANK);

		gc->chip_types->regs.mask = stm32_bank->imr_ofst;
		gc->private = (void *)chip_data;
	}

	nr_irqs = of_irq_count(node);
	for (i = 0; i < nr_irqs; i++) {
		unsigned int irq = irq_of_parse_and_map(node, i);

		irq_set_handler_data(irq, domain);
		irq_set_chained_handler(irq, stm32_irq_handler);
	}

	return 0;

out_free_domain:
	irq_domain_remove(domain);
out_unmap:
	iounmap(host_data->base);
	kfree(host_data->chips_data);
	kfree(host_data);
	return ret;
}

static const struct irq_domain_ops stm32_exti_h_domain_ops = {
	.alloc	= stm32_exti_h_domain_alloc,
	.free	= irq_domain_free_irqs_common,
};

static int
__init stm32_exti_hierarchy_init(const struct stm32_exti_drv_data *drv_data,
				 struct device_node *node,
				 struct device_node *parent)
{
	struct irq_domain *parent_domain, *domain;
	struct stm32_exti_host_data *host_data;
	int ret, i;

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("interrupt-parent not found\n");
		return -EINVAL;
	}

	host_data = stm32_exti_host_init(drv_data, node);
	if (!host_data)
		return -ENOMEM;

	for (i = 0; i < drv_data->bank_nr; i++)
		stm32_exti_chip_init(host_data, i, node);

	domain = irq_domain_add_hierarchy(parent_domain, 0,
					  drv_data->bank_nr * IRQS_PER_BANK,
					  node, &stm32_exti_h_domain_ops,
					  host_data);

	if (!domain) {
		pr_err("%s: Could not register exti domain.\n", node->name);
		ret = -ENOMEM;
		goto out_unmap;
	}

	stm32_exti_h_syscore_init();

	return 0;

out_unmap:
	iounmap(host_data->base);
	kfree(host_data->chips_data);
	kfree(host_data);
	return ret;
}

static int __init stm32f4_exti_of_init(struct device_node *np,
				       struct device_node *parent)
{
	return stm32_exti_init(&stm32f4xx_drv_data, np);
}

IRQCHIP_DECLARE(stm32f4_exti, "st,stm32-exti", stm32f4_exti_of_init);

static int __init stm32h7_exti_of_init(struct device_node *np,
				       struct device_node *parent)
{
	return stm32_exti_init(&stm32h7xx_drv_data, np);
}

IRQCHIP_DECLARE(stm32h7_exti, "st,stm32h7-exti", stm32h7_exti_of_init);

static int __init stm32mp1_exti_of_init(struct device_node *np,
					struct device_node *parent)
{
	return stm32_exti_hierarchy_init(&stm32mp1_drv_data, np, parent);
}

IRQCHIP_DECLARE(stm32mp1_exti, "st,stm32mp1-exti", stm32mp1_exti_of_init);
