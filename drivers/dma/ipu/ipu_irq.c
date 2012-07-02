/*
 * Copyright (C) 2008
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/module.h>

#include <mach/ipu.h>

#include "ipu_intern.h"

/*
 * Register read / write - shall be inlined by the compiler
 */
static u32 ipu_read_reg(struct ipu *ipu, unsigned long reg)
{
	return __raw_readl(ipu->reg_ipu + reg);
}

static void ipu_write_reg(struct ipu *ipu, u32 value, unsigned long reg)
{
	__raw_writel(value, ipu->reg_ipu + reg);
}


/*
 * IPU IRQ chip driver
 */

#define IPU_IRQ_NR_FN_BANKS 3
#define IPU_IRQ_NR_ERR_BANKS 2
#define IPU_IRQ_NR_BANKS (IPU_IRQ_NR_FN_BANKS + IPU_IRQ_NR_ERR_BANKS)

struct ipu_irq_bank {
	unsigned int	control;
	unsigned int	status;
	spinlock_t	lock;
	struct ipu	*ipu;
};

static struct ipu_irq_bank irq_bank[IPU_IRQ_NR_BANKS] = {
	/* 3 groups of functional interrupts */
	{
		.control	= IPU_INT_CTRL_1,
		.status		= IPU_INT_STAT_1,
	}, {
		.control	= IPU_INT_CTRL_2,
		.status		= IPU_INT_STAT_2,
	}, {
		.control	= IPU_INT_CTRL_3,
		.status		= IPU_INT_STAT_3,
	},
	/* 2 groups of error interrupts */
	{
		.control	= IPU_INT_CTRL_4,
		.status		= IPU_INT_STAT_4,
	}, {
		.control	= IPU_INT_CTRL_5,
		.status		= IPU_INT_STAT_5,
	},
};

struct ipu_irq_map {
	unsigned int		irq;
	int			source;
	struct ipu_irq_bank	*bank;
	struct ipu		*ipu;
};

static struct ipu_irq_map irq_map[CONFIG_MX3_IPU_IRQS];
/* Protects allocations from the above array of maps */
static DEFINE_MUTEX(map_lock);
/* Protects register accesses and individual mappings */
static DEFINE_RAW_SPINLOCK(bank_lock);

static struct ipu_irq_map *src2map(unsigned int src)
{
	int i;

	for (i = 0; i < CONFIG_MX3_IPU_IRQS; i++)
		if (irq_map[i].source == src)
			return irq_map + i;

	return NULL;
}

static void ipu_irq_unmask(struct irq_data *d)
{
	struct ipu_irq_map *map = irq_data_get_irq_chip_data(d);
	struct ipu_irq_bank *bank;
	uint32_t reg;
	unsigned long lock_flags;

	raw_spin_lock_irqsave(&bank_lock, lock_flags);

	bank = map->bank;
	if (!bank) {
		raw_spin_unlock_irqrestore(&bank_lock, lock_flags);
		pr_err("IPU: %s(%u) - unmapped!\n", __func__, d->irq);
		return;
	}

	reg = ipu_read_reg(bank->ipu, bank->control);
	reg |= (1UL << (map->source & 31));
	ipu_write_reg(bank->ipu, reg, bank->control);

	raw_spin_unlock_irqrestore(&bank_lock, lock_flags);
}

static void ipu_irq_mask(struct irq_data *d)
{
	struct ipu_irq_map *map = irq_data_get_irq_chip_data(d);
	struct ipu_irq_bank *bank;
	uint32_t reg;
	unsigned long lock_flags;

	raw_spin_lock_irqsave(&bank_lock, lock_flags);

	bank = map->bank;
	if (!bank) {
		raw_spin_unlock_irqrestore(&bank_lock, lock_flags);
		pr_err("IPU: %s(%u) - unmapped!\n", __func__, d->irq);
		return;
	}

	reg = ipu_read_reg(bank->ipu, bank->control);
	reg &= ~(1UL << (map->source & 31));
	ipu_write_reg(bank->ipu, reg, bank->control);

	raw_spin_unlock_irqrestore(&bank_lock, lock_flags);
}

static void ipu_irq_ack(struct irq_data *d)
{
	struct ipu_irq_map *map = irq_data_get_irq_chip_data(d);
	struct ipu_irq_bank *bank;
	unsigned long lock_flags;

	raw_spin_lock_irqsave(&bank_lock, lock_flags);

	bank = map->bank;
	if (!bank) {
		raw_spin_unlock_irqrestore(&bank_lock, lock_flags);
		pr_err("IPU: %s(%u) - unmapped!\n", __func__, d->irq);
		return;
	}

	ipu_write_reg(bank->ipu, 1UL << (map->source & 31), bank->status);
	raw_spin_unlock_irqrestore(&bank_lock, lock_flags);
}

/**
 * ipu_irq_status() - returns the current interrupt status of the specified IRQ.
 * @irq:	interrupt line to get status for.
 * @return:	true if the interrupt is pending/asserted or false if the
 *		interrupt is not pending.
 */
bool ipu_irq_status(unsigned int irq)
{
	struct ipu_irq_map *map = irq_get_chip_data(irq);
	struct ipu_irq_bank *bank;
	unsigned long lock_flags;
	bool ret;

	raw_spin_lock_irqsave(&bank_lock, lock_flags);
	bank = map->bank;
	ret = bank && ipu_read_reg(bank->ipu, bank->status) &
		(1UL << (map->source & 31));
	raw_spin_unlock_irqrestore(&bank_lock, lock_flags);

	return ret;
}

/**
 * ipu_irq_map() - map an IPU interrupt source to an IRQ number
 * @source:	interrupt source bit position (see below)
 * @return:	mapped IRQ number or negative error code
 *
 * The source parameter has to be explained further. On i.MX31 IPU has 137 IRQ
 * sources, they are broken down in 5 32-bit registers, like 32, 32, 24, 32, 17.
 * However, the source argument of this function is not the sequence number of
 * the possible IRQ, but rather its bit position. So, first interrupt in fourth
 * register has source number 96, and not 88. This makes calculations easier,
 * and also provides forward compatibility with any future IPU implementations
 * with any interrupt bit assignments.
 */
int ipu_irq_map(unsigned int source)
{
	int i, ret = -ENOMEM;
	struct ipu_irq_map *map;

	might_sleep();

	mutex_lock(&map_lock);
	map = src2map(source);
	if (map) {
		pr_err("IPU: Source %u already mapped to IRQ %u\n", source, map->irq);
		ret = -EBUSY;
		goto out;
	}

	for (i = 0; i < CONFIG_MX3_IPU_IRQS; i++) {
		if (irq_map[i].source < 0) {
			unsigned long lock_flags;

			raw_spin_lock_irqsave(&bank_lock, lock_flags);
			irq_map[i].source = source;
			irq_map[i].bank = irq_bank + source / 32;
			raw_spin_unlock_irqrestore(&bank_lock, lock_flags);

			ret = irq_map[i].irq;
			pr_debug("IPU: mapped source %u to IRQ %u\n",
				 source, ret);
			break;
		}
	}
out:
	mutex_unlock(&map_lock);

	if (ret < 0)
		pr_err("IPU: couldn't map source %u: %d\n", source, ret);

	return ret;
}

/**
 * ipu_irq_map() - map an IPU interrupt source to an IRQ number
 * @source:	interrupt source bit position (see ipu_irq_map())
 * @return:	0 or negative error code
 */
int ipu_irq_unmap(unsigned int source)
{
	int i, ret = -EINVAL;

	might_sleep();

	mutex_lock(&map_lock);
	for (i = 0; i < CONFIG_MX3_IPU_IRQS; i++) {
		if (irq_map[i].source == source) {
			unsigned long lock_flags;

			pr_debug("IPU: unmapped source %u from IRQ %u\n",
				 source, irq_map[i].irq);

			raw_spin_lock_irqsave(&bank_lock, lock_flags);
			irq_map[i].source = -EINVAL;
			irq_map[i].bank = NULL;
			raw_spin_unlock_irqrestore(&bank_lock, lock_flags);

			ret = 0;
			break;
		}
	}
	mutex_unlock(&map_lock);

	return ret;
}

/* Chained IRQ handler for IPU error interrupt */
static void ipu_irq_err(unsigned int irq, struct irq_desc *desc)
{
	struct ipu *ipu = irq_get_handler_data(irq);
	u32 status;
	int i, line;

	for (i = IPU_IRQ_NR_FN_BANKS; i < IPU_IRQ_NR_BANKS; i++) {
		struct ipu_irq_bank *bank = irq_bank + i;

		raw_spin_lock(&bank_lock);
		status = ipu_read_reg(ipu, bank->status);
		/*
		 * Don't think we have to clear all interrupts here, they will
		 * be acked by ->handle_irq() (handle_level_irq). However, we
		 * might want to clear unhandled interrupts after the loop...
		 */
		status &= ipu_read_reg(ipu, bank->control);
		raw_spin_unlock(&bank_lock);
		while ((line = ffs(status))) {
			struct ipu_irq_map *map;

			line--;
			status &= ~(1UL << line);

			raw_spin_lock(&bank_lock);
			map = src2map(32 * i + line);
			if (map)
				irq = map->irq;
			raw_spin_unlock(&bank_lock);

			if (!map) {
				pr_err("IPU: Interrupt on unmapped source %u bank %d\n",
				       line, i);
				continue;
			}
			generic_handle_irq(irq);
		}
	}
}

/* Chained IRQ handler for IPU function interrupt */
static void ipu_irq_fn(unsigned int irq, struct irq_desc *desc)
{
	struct ipu *ipu = irq_desc_get_handler_data(desc);
	u32 status;
	int i, line;

	for (i = 0; i < IPU_IRQ_NR_FN_BANKS; i++) {
		struct ipu_irq_bank *bank = irq_bank + i;

		raw_spin_lock(&bank_lock);
		status = ipu_read_reg(ipu, bank->status);
		/* Not clearing all interrupts, see above */
		status &= ipu_read_reg(ipu, bank->control);
		raw_spin_unlock(&bank_lock);
		while ((line = ffs(status))) {
			struct ipu_irq_map *map;

			line--;
			status &= ~(1UL << line);

			raw_spin_lock(&bank_lock);
			map = src2map(32 * i + line);
			if (map)
				irq = map->irq;
			raw_spin_unlock(&bank_lock);

			if (!map) {
				pr_err("IPU: Interrupt on unmapped source %u bank %d\n",
				       line, i);
				continue;
			}
			generic_handle_irq(irq);
		}
	}
}

static struct irq_chip ipu_irq_chip = {
	.name		= "ipu_irq",
	.irq_ack	= ipu_irq_ack,
	.irq_mask	= ipu_irq_mask,
	.irq_unmask	= ipu_irq_unmask,
};

/* Install the IRQ handler */
int __init ipu_irq_attach_irq(struct ipu *ipu, struct platform_device *dev)
{
	unsigned int irq, i;
	int irq_base = irq_alloc_descs(-1, 0, CONFIG_MX3_IPU_IRQS,
				       numa_node_id());

	if (irq_base < 0)
		return irq_base;

	for (i = 0; i < IPU_IRQ_NR_BANKS; i++)
		irq_bank[i].ipu = ipu;

	for (i = 0; i < CONFIG_MX3_IPU_IRQS; i++) {
		int ret;

		irq = irq_base + i;
		ret = irq_set_chip(irq, &ipu_irq_chip);
		if (ret < 0)
			return ret;
		ret = irq_set_chip_data(irq, irq_map + i);
		if (ret < 0)
			return ret;
		irq_map[i].ipu = ipu;
		irq_map[i].irq = irq;
		irq_map[i].source = -EINVAL;
		irq_set_handler(irq, handle_level_irq);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
#endif
	}

	irq_set_handler_data(ipu->irq_fn, ipu);
	irq_set_chained_handler(ipu->irq_fn, ipu_irq_fn);

	irq_set_handler_data(ipu->irq_err, ipu);
	irq_set_chained_handler(ipu->irq_err, ipu_irq_err);

	ipu->irq_base = irq_base;

	return 0;
}

void ipu_irq_detach_irq(struct ipu *ipu, struct platform_device *dev)
{
	unsigned int irq, irq_base;

	irq_base = ipu->irq_base;

	irq_set_chained_handler(ipu->irq_fn, NULL);
	irq_set_handler_data(ipu->irq_fn, NULL);

	irq_set_chained_handler(ipu->irq_err, NULL);
	irq_set_handler_data(ipu->irq_err, NULL);

	for (irq = irq_base; irq < irq_base + CONFIG_MX3_IPU_IRQS; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		irq_set_chip(irq, NULL);
		irq_set_chip_data(irq, NULL);
	}
}
