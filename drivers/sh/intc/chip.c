/*
 * IRQ chip definitions for INTC IRQs.
 *
 * Copyright (C) 2007, 2008 Magnus Damm
 * Copyright (C) 2009, 2010 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/cpumask.h>
#include <linux/io.h>
#include "internals.h"

void _intc_enable(struct irq_data *data, unsigned long handle)
{
	unsigned int irq = data->irq;
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long addr;
	unsigned int cpu;

	for (cpu = 0; cpu < SMP_NR(d, _INTC_ADDR_E(handle)); cpu++) {
#ifdef CONFIG_SMP
		if (!cpumask_test_cpu(cpu, data->affinity))
			continue;
#endif
		addr = INTC_REG(d, _INTC_ADDR_E(handle), cpu);
		intc_enable_fns[_INTC_MODE(handle)](addr, handle, intc_reg_fns\
						    [_INTC_FN(handle)], irq);
	}

	intc_balancing_enable(irq);
}

static void intc_enable(struct irq_data *data)
{
	_intc_enable(data, (unsigned long)irq_data_get_irq_chip_data(data));
}

static void intc_disable(struct irq_data *data)
{
	unsigned int irq = data->irq;
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = (unsigned long)irq_data_get_irq_chip_data(data);
	unsigned long addr;
	unsigned int cpu;

	intc_balancing_disable(irq);

	for (cpu = 0; cpu < SMP_NR(d, _INTC_ADDR_D(handle)); cpu++) {
#ifdef CONFIG_SMP
		if (!cpumask_test_cpu(cpu, data->affinity))
			continue;
#endif
		addr = INTC_REG(d, _INTC_ADDR_D(handle), cpu);
		intc_disable_fns[_INTC_MODE(handle)](addr, handle,intc_reg_fns\
						     [_INTC_FN(handle)], irq);
	}
}

static int intc_set_wake(struct irq_data *data, unsigned int on)
{
	return 0; /* allow wakeup, but setup hardware in intc_suspend() */
}

#ifdef CONFIG_SMP
/*
 * This is held with the irq desc lock held, so we don't require any
 * additional locking here at the intc desc level. The affinity mask is
 * later tested in the enable/disable paths.
 */
static int intc_set_affinity(struct irq_data *data,
			     const struct cpumask *cpumask,
			     bool force)
{
	if (!cpumask_intersects(cpumask, cpu_online_mask))
		return -1;

	cpumask_copy(data->affinity, cpumask);

	return 0;
}
#endif

static void intc_mask_ack(struct irq_data *data)
{
	unsigned int irq = data->irq;
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = intc_get_ack_handle(irq);
	unsigned long addr;

	intc_disable(data);

	/* read register and write zero only to the associated bit */
	if (handle) {
		unsigned int value;

		addr = INTC_REG(d, _INTC_ADDR_D(handle), 0);
		value = intc_set_field_from_handle(0, 1, handle);

		switch (_INTC_FN(handle)) {
		case REG_FN_MODIFY_BASE + 0:	/* 8bit */
			__raw_readb(addr);
			__raw_writeb(0xff ^ value, addr);
			break;
		case REG_FN_MODIFY_BASE + 1:	/* 16bit */
			__raw_readw(addr);
			__raw_writew(0xffff ^ value, addr);
			break;
		case REG_FN_MODIFY_BASE + 3:	/* 32bit */
			__raw_readl(addr);
			__raw_writel(0xffffffff ^ value, addr);
			break;
		default:
			BUG();
			break;
		}
	}
}

static struct intc_handle_int *intc_find_irq(struct intc_handle_int *hp,
					     unsigned int nr_hp,
					     unsigned int irq)
{
	int i;

	/*
	 * this doesn't scale well, but...
	 *
	 * this function should only be used for cerain uncommon
	 * operations such as intc_set_priority() and intc_set_type()
	 * and in those rare cases performance doesn't matter that much.
	 * keeping the memory footprint low is more important.
	 *
	 * one rather simple way to speed this up and still keep the
	 * memory footprint down is to make sure the array is sorted
	 * and then perform a bisect to lookup the irq.
	 */
	for (i = 0; i < nr_hp; i++) {
		if ((hp + i)->irq != irq)
			continue;

		return hp + i;
	}

	return NULL;
}

int intc_set_priority(unsigned int irq, unsigned int prio)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	struct irq_data *data = irq_get_irq_data(irq);
	struct intc_handle_int *ihp;

	if (!intc_get_prio_level(irq) || prio <= 1)
		return -EINVAL;

	ihp = intc_find_irq(d->prio, d->nr_prio, irq);
	if (ihp) {
		if (prio >= (1 << _INTC_WIDTH(ihp->handle)))
			return -EINVAL;

		intc_set_prio_level(irq, prio);

		/*
		 * only set secondary masking method directly
		 * primary masking method is using intc_prio_level[irq]
		 * priority level will be set during next enable()
		 */
		if (_INTC_FN(ihp->handle) != REG_FN_ERR)
			_intc_enable(data, ihp->handle);
	}
	return 0;
}

#define SENSE_VALID_FLAG 0x80
#define VALID(x) (x | SENSE_VALID_FLAG)

static unsigned char intc_irq_sense_table[IRQ_TYPE_SENSE_MASK + 1] = {
	[IRQ_TYPE_EDGE_FALLING] = VALID(0),
	[IRQ_TYPE_EDGE_RISING] = VALID(1),
	[IRQ_TYPE_LEVEL_LOW] = VALID(2),
	/* SH7706, SH7707 and SH7709 do not support high level triggered */
#if !defined(CONFIG_CPU_SUBTYPE_SH7706) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7707) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7709)
	[IRQ_TYPE_LEVEL_HIGH] = VALID(3),
#endif
};

static int intc_set_type(struct irq_data *data, unsigned int type)
{
	unsigned int irq = data->irq;
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned char value = intc_irq_sense_table[type & IRQ_TYPE_SENSE_MASK];
	struct intc_handle_int *ihp;
	unsigned long addr;

	if (!value)
		return -EINVAL;

	ihp = intc_find_irq(d->sense, d->nr_sense, irq);
	if (ihp) {
		addr = INTC_REG(d, _INTC_ADDR_E(ihp->handle), 0);
		intc_reg_fns[_INTC_FN(ihp->handle)](addr, ihp->handle,
						    value & ~SENSE_VALID_FLAG);
	}

	return 0;
}

struct irq_chip intc_irq_chip	= {
	.irq_mask		= intc_disable,
	.irq_unmask		= intc_enable,
	.irq_mask_ack		= intc_mask_ack,
	.irq_enable		= intc_enable,
	.irq_disable		= intc_disable,
	.irq_shutdown		= intc_disable,
	.irq_set_type		= intc_set_type,
	.irq_set_wake		= intc_set_wake,
#ifdef CONFIG_SMP
	.irq_set_affinity	= intc_set_affinity,
#endif
};
