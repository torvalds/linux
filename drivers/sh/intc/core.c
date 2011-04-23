/*
 * Shared interrupt handling code for IPR and INTC2 types of IRQs.
 *
 * Copyright (C) 2007, 2008 Magnus Damm
 * Copyright (C) 2009, 2010 Paul Mundt
 *
 * Based on intc2.c and ipr.c
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2001  David J. Mckay (david.mckay@st.com)
 * Copyright (C) 2003  Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 * Copyright (C) 2005, 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) "intc: " fmt

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/sh_intc.h>
#include <linux/sysdev.h>
#include <linux/syscore_ops.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/radix-tree.h>
#include "internals.h"

LIST_HEAD(intc_list);
DEFINE_RAW_SPINLOCK(intc_big_lock);
unsigned int nr_intc_controllers;

/*
 * Default priority level
 * - this needs to be at least 2 for 5-bit priorities on 7780
 */
static unsigned int default_prio_level = 2;	/* 2 - 16 */
static unsigned int intc_prio_level[NR_IRQS];	/* for now */

unsigned int intc_get_dfl_prio_level(void)
{
	return default_prio_level;
}

unsigned int intc_get_prio_level(unsigned int irq)
{
	return intc_prio_level[irq];
}

void intc_set_prio_level(unsigned int irq, unsigned int level)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&intc_big_lock, flags);
	intc_prio_level[irq] = level;
	raw_spin_unlock_irqrestore(&intc_big_lock, flags);
}

static void intc_redirect_irq(unsigned int irq, struct irq_desc *desc)
{
	generic_handle_irq((unsigned int)irq_get_handler_data(irq));
}

static void __init intc_register_irq(struct intc_desc *desc,
				     struct intc_desc_int *d,
				     intc_enum enum_id,
				     unsigned int irq)
{
	struct intc_handle_int *hp;
	struct irq_data *irq_data;
	unsigned int data[2], primary;
	unsigned long flags;

	/*
	 * Register the IRQ position with the global IRQ map, then insert
	 * it in to the radix tree.
	 */
	irq_reserve_irq(irq);

	raw_spin_lock_irqsave(&intc_big_lock, flags);
	radix_tree_insert(&d->tree, enum_id, intc_irq_xlate_get(irq));
	raw_spin_unlock_irqrestore(&intc_big_lock, flags);

	/*
	 * Prefer single interrupt source bitmap over other combinations:
	 *
	 * 1. bitmap, single interrupt source
	 * 2. priority, single interrupt source
	 * 3. bitmap, multiple interrupt sources (groups)
	 * 4. priority, multiple interrupt sources (groups)
	 */
	data[0] = intc_get_mask_handle(desc, d, enum_id, 0);
	data[1] = intc_get_prio_handle(desc, d, enum_id, 0);

	primary = 0;
	if (!data[0] && data[1])
		primary = 1;

	if (!data[0] && !data[1])
		pr_warning("missing unique irq mask for irq %d (vect 0x%04x)\n",
			   irq, irq2evt(irq));

	data[0] = data[0] ? data[0] : intc_get_mask_handle(desc, d, enum_id, 1);
	data[1] = data[1] ? data[1] : intc_get_prio_handle(desc, d, enum_id, 1);

	if (!data[primary])
		primary ^= 1;

	BUG_ON(!data[primary]); /* must have primary masking method */

	irq_data = irq_get_irq_data(irq);

	disable_irq_nosync(irq);
	irq_set_chip_and_handler_name(irq, &d->chip, handle_level_irq,
				      "level");
	irq_set_chip_data(irq, (void *)data[primary]);

	/*
	 * set priority level
	 */
	intc_set_prio_level(irq, intc_get_dfl_prio_level());

	/* enable secondary masking method if present */
	if (data[!primary])
		_intc_enable(irq_data, data[!primary]);

	/* add irq to d->prio list if priority is available */
	if (data[1]) {
		hp = d->prio + d->nr_prio;
		hp->irq = irq;
		hp->handle = data[1];

		if (primary) {
			/*
			 * only secondary priority should access registers, so
			 * set _INTC_FN(h) = REG_FN_ERR for intc_set_priority()
			 */
			hp->handle &= ~_INTC_MK(0x0f, 0, 0, 0, 0, 0);
			hp->handle |= _INTC_MK(REG_FN_ERR, 0, 0, 0, 0, 0);
		}
		d->nr_prio++;
	}

	/* add irq to d->sense list if sense is available */
	data[0] = intc_get_sense_handle(desc, d, enum_id);
	if (data[0]) {
		(d->sense + d->nr_sense)->irq = irq;
		(d->sense + d->nr_sense)->handle = data[0];
		d->nr_sense++;
	}

	/* irq should be disabled by default */
	d->chip.irq_mask(irq_data);

	intc_set_ack_handle(irq, desc, d, enum_id);
	intc_set_dist_handle(irq, desc, d, enum_id);

	activate_irq(irq);
}

static unsigned int __init save_reg(struct intc_desc_int *d,
				    unsigned int cnt,
				    unsigned long value,
				    unsigned int smp)
{
	if (value) {
		value = intc_phys_to_virt(d, value);

		d->reg[cnt] = value;
#ifdef CONFIG_SMP
		d->smp[cnt] = smp;
#endif
		return 1;
	}

	return 0;
}

int __init register_intc_controller(struct intc_desc *desc)
{
	unsigned int i, k, smp;
	struct intc_hw_desc *hw = &desc->hw;
	struct intc_desc_int *d;
	struct resource *res;

	pr_info("Registered controller '%s' with %u IRQs\n",
		desc->name, hw->nr_vectors);

	d = kzalloc(sizeof(*d), GFP_NOWAIT);
	if (!d)
		goto err0;

	INIT_LIST_HEAD(&d->list);
	list_add_tail(&d->list, &intc_list);

	raw_spin_lock_init(&d->lock);
	INIT_RADIX_TREE(&d->tree, GFP_ATOMIC);

	d->index = nr_intc_controllers;

	if (desc->num_resources) {
		d->nr_windows = desc->num_resources;
		d->window = kzalloc(d->nr_windows * sizeof(*d->window),
				    GFP_NOWAIT);
		if (!d->window)
			goto err1;

		for (k = 0; k < d->nr_windows; k++) {
			res = desc->resource + k;
			WARN_ON(resource_type(res) != IORESOURCE_MEM);
			d->window[k].phys = res->start;
			d->window[k].size = resource_size(res);
			d->window[k].virt = ioremap_nocache(res->start,
							 resource_size(res));
			if (!d->window[k].virt)
				goto err2;
		}
	}

	d->nr_reg = hw->mask_regs ? hw->nr_mask_regs * 2 : 0;
#ifdef CONFIG_INTC_BALANCING
	if (d->nr_reg)
		d->nr_reg += hw->nr_mask_regs;
#endif
	d->nr_reg += hw->prio_regs ? hw->nr_prio_regs * 2 : 0;
	d->nr_reg += hw->sense_regs ? hw->nr_sense_regs : 0;
	d->nr_reg += hw->ack_regs ? hw->nr_ack_regs : 0;
	d->nr_reg += hw->subgroups ? hw->nr_subgroups : 0;

	d->reg = kzalloc(d->nr_reg * sizeof(*d->reg), GFP_NOWAIT);
	if (!d->reg)
		goto err2;

#ifdef CONFIG_SMP
	d->smp = kzalloc(d->nr_reg * sizeof(*d->smp), GFP_NOWAIT);
	if (!d->smp)
		goto err3;
#endif
	k = 0;

	if (hw->mask_regs) {
		for (i = 0; i < hw->nr_mask_regs; i++) {
			smp = IS_SMP(hw->mask_regs[i]);
			k += save_reg(d, k, hw->mask_regs[i].set_reg, smp);
			k += save_reg(d, k, hw->mask_regs[i].clr_reg, smp);
#ifdef CONFIG_INTC_BALANCING
			k += save_reg(d, k, hw->mask_regs[i].dist_reg, 0);
#endif
		}
	}

	if (hw->prio_regs) {
		d->prio = kzalloc(hw->nr_vectors * sizeof(*d->prio),
				  GFP_NOWAIT);
		if (!d->prio)
			goto err4;

		for (i = 0; i < hw->nr_prio_regs; i++) {
			smp = IS_SMP(hw->prio_regs[i]);
			k += save_reg(d, k, hw->prio_regs[i].set_reg, smp);
			k += save_reg(d, k, hw->prio_regs[i].clr_reg, smp);
		}
	}

	if (hw->sense_regs) {
		d->sense = kzalloc(hw->nr_vectors * sizeof(*d->sense),
				   GFP_NOWAIT);
		if (!d->sense)
			goto err5;

		for (i = 0; i < hw->nr_sense_regs; i++)
			k += save_reg(d, k, hw->sense_regs[i].reg, 0);
	}

	if (hw->subgroups)
		for (i = 0; i < hw->nr_subgroups; i++)
			if (hw->subgroups[i].reg)
				k+= save_reg(d, k, hw->subgroups[i].reg, 0);

	memcpy(&d->chip, &intc_irq_chip, sizeof(struct irq_chip));
	d->chip.name = desc->name;

	if (hw->ack_regs)
		for (i = 0; i < hw->nr_ack_regs; i++)
			k += save_reg(d, k, hw->ack_regs[i].set_reg, 0);
	else
		d->chip.irq_mask_ack = d->chip.irq_disable;

	/* disable bits matching force_disable before registering irqs */
	if (desc->force_disable)
		intc_enable_disable_enum(desc, d, desc->force_disable, 0);

	/* disable bits matching force_enable before registering irqs */
	if (desc->force_enable)
		intc_enable_disable_enum(desc, d, desc->force_enable, 0);

	BUG_ON(k > 256); /* _INTC_ADDR_E() and _INTC_ADDR_D() are 8 bits */

	/* register the vectors one by one */
	for (i = 0; i < hw->nr_vectors; i++) {
		struct intc_vect *vect = hw->vectors + i;
		unsigned int irq = evt2irq(vect->vect);
		int res;

		if (!vect->enum_id)
			continue;

		res = irq_alloc_desc_at(irq, numa_node_id());
		if (res != irq && res != -EEXIST) {
			pr_err("can't get irq_desc for %d\n", irq);
			continue;
		}

		intc_irq_xlate_set(irq, vect->enum_id, d);
		intc_register_irq(desc, d, vect->enum_id, irq);

		for (k = i + 1; k < hw->nr_vectors; k++) {
			struct intc_vect *vect2 = hw->vectors + k;
			unsigned int irq2 = evt2irq(vect2->vect);

			if (vect->enum_id != vect2->enum_id)
				continue;

			/*
			 * In the case of multi-evt handling and sparse
			 * IRQ support, each vector still needs to have
			 * its own backing irq_desc.
			 */
			res = irq_alloc_desc_at(irq2, numa_node_id());
			if (res != irq2 && res != -EEXIST) {
				pr_err("can't get irq_desc for %d\n", irq2);
				continue;
			}

			vect2->enum_id = 0;

			/* redirect this interrupts to the first one */
			irq_set_chip(irq2, &dummy_irq_chip);
			irq_set_chained_handler(irq2, intc_redirect_irq);
			irq_set_handler_data(irq2, (void *)irq);
		}
	}

	intc_subgroup_init(desc, d);

	/* enable bits matching force_enable after registering irqs */
	if (desc->force_enable)
		intc_enable_disable_enum(desc, d, desc->force_enable, 1);

	nr_intc_controllers++;

	return 0;
err5:
	kfree(d->prio);
err4:
#ifdef CONFIG_SMP
	kfree(d->smp);
err3:
#endif
	kfree(d->reg);
err2:
	for (k = 0; k < d->nr_windows; k++)
		if (d->window[k].virt)
			iounmap(d->window[k].virt);

	kfree(d->window);
err1:
	kfree(d);
err0:
	pr_err("unable to allocate INTC memory\n");

	return -ENOMEM;
}

static int intc_suspend(void)
{
	struct intc_desc_int *d;

	list_for_each_entry(d, &intc_list, list) {
		int irq;

		/* enable wakeup irqs belonging to this intc controller */
		for_each_active_irq(irq) {
			struct irq_data *data;
			struct irq_chip *chip;

			data = irq_get_irq_data(irq);
			chip = irq_data_get_irq_chip(data);
			if (chip != &d->chip)
				continue;
			if (irqd_is_wakeup_set(data))
				chip->irq_enable(data);
		}
	}
	return 0;
}

static void intc_resume(void)
{
	struct intc_desc_int *d;

	list_for_each_entry(d, &intc_list, list) {
		int irq;

		for_each_active_irq(irq) {
			struct irq_data *data;
			struct irq_chip *chip;

			data = irq_get_irq_data(irq);
			chip = irq_data_get_irq_chip(data);
			/*
			 * This will catch the redirect and VIRQ cases
			 * due to the dummy_irq_chip being inserted.
			 */
			if (chip != &d->chip)
				continue;
			if (irqd_irq_disabled(data))
				chip->irq_disable(data);
			else
				chip->irq_enable(data);
		}
	}
}

struct syscore_ops intc_syscore_ops = {
	.suspend	= intc_suspend,
	.resume		= intc_resume,
};

struct sysdev_class intc_sysdev_class = {
	.name		= "intc",
};

static ssize_t
show_intc_name(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	struct intc_desc_int *d;

	d = container_of(dev, struct intc_desc_int, sysdev);

	return sprintf(buf, "%s\n", d->chip.name);
}

static SYSDEV_ATTR(name, S_IRUGO, show_intc_name, NULL);

static int __init register_intc_sysdevs(void)
{
	struct intc_desc_int *d;
	int error;

	register_syscore_ops(&intc_syscore_ops);

	error = sysdev_class_register(&intc_sysdev_class);
	if (!error) {
		list_for_each_entry(d, &intc_list, list) {
			d->sysdev.id = d->index;
			d->sysdev.cls = &intc_sysdev_class;
			error = sysdev_register(&d->sysdev);
			if (error == 0)
				error = sysdev_create_file(&d->sysdev,
							   &attr_name);
			if (error)
				break;
		}
	}

	if (error)
		pr_err("sysdev registration error\n");

	return error;
}
device_initcall(register_intc_sysdevs);
