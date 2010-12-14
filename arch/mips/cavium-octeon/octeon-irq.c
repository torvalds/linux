/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2008, 2009, 2010 Cavium Networks
 */
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/smp.h>

#include <asm/octeon/octeon.h>

static DEFINE_RAW_SPINLOCK(octeon_irq_ciu0_lock);
static DEFINE_RAW_SPINLOCK(octeon_irq_ciu1_lock);

static int octeon_coreid_for_cpu(int cpu)
{
#ifdef CONFIG_SMP
	return cpu_logical_map(cpu);
#else
	return cvmx_get_core_num();
#endif
}

static void octeon_irq_core_ack(unsigned int irq)
{
	unsigned int bit = irq - OCTEON_IRQ_SW0;
	/*
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	clear_c0_status(0x100 << bit);
	/* The two user interrupts must be cleared manually. */
	if (bit < 2)
		clear_c0_cause(0x100 << bit);
}

static void octeon_irq_core_eoi(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned int bit = irq - OCTEON_IRQ_SW0;
	/*
	 * If an IRQ is being processed while we are disabling it the
	 * handler will attempt to unmask the interrupt after it has
	 * been disabled.
	 */
	if ((unlikely(desc->status & IRQ_DISABLED)))
		return;
	/*
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	set_c0_status(0x100 << bit);
}

static void octeon_irq_core_enable(unsigned int irq)
{
	unsigned long flags;
	unsigned int bit = irq - OCTEON_IRQ_SW0;

	/*
	 * We need to disable interrupts to make sure our updates are
	 * atomic.
	 */
	local_irq_save(flags);
	set_c0_status(0x100 << bit);
	local_irq_restore(flags);
}

static void octeon_irq_core_disable_local(unsigned int irq)
{
	unsigned long flags;
	unsigned int bit = irq - OCTEON_IRQ_SW0;
	/*
	 * We need to disable interrupts to make sure our updates are
	 * atomic.
	 */
	local_irq_save(flags);
	clear_c0_status(0x100 << bit);
	local_irq_restore(flags);
}

static void octeon_irq_core_disable(unsigned int irq)
{
#ifdef CONFIG_SMP
	on_each_cpu((void (*)(void *)) octeon_irq_core_disable_local,
		    (void *) (long) irq, 1);
#else
	octeon_irq_core_disable_local(irq);
#endif
}

static struct irq_chip octeon_irq_chip_core = {
	.name = "Core",
	.enable = octeon_irq_core_enable,
	.disable = octeon_irq_core_disable,
	.ack = octeon_irq_core_ack,
	.eoi = octeon_irq_core_eoi,
};


static void octeon_irq_ciu0_ack(unsigned int irq)
{
	switch (irq) {
	case OCTEON_IRQ_GMX_DRP0:
	case OCTEON_IRQ_GMX_DRP1:
	case OCTEON_IRQ_IPD_DRP:
	case OCTEON_IRQ_KEY_ZERO:
	case OCTEON_IRQ_TIMER0:
	case OCTEON_IRQ_TIMER1:
	case OCTEON_IRQ_TIMER2:
	case OCTEON_IRQ_TIMER3:
	{
		int index = cvmx_get_core_num() * 2;
		u64 mask = 1ull << (irq - OCTEON_IRQ_WORKQ0);
		/*
		 * CIU timer type interrupts must be acknoleged by
		 * writing a '1' bit to their sum0 bit.
		 */
		cvmx_write_csr(CVMX_CIU_INTX_SUM0(index), mask);
		break;
	}
	default:
		break;
	}

	/*
	 * In order to avoid any locking accessing the CIU, we
	 * acknowledge CIU interrupts by disabling all of them.  This
	 * way we can use a per core register and avoid any out of
	 * core locking requirements.  This has the side affect that
	 * CIU interrupts can't be processed recursively.
	 *
	 * We don't need to disable IRQs to make these atomic since
	 * they are already disabled earlier in the low level
	 * interrupt code.
	 */
	clear_c0_status(0x100 << 2);
}

static void octeon_irq_ciu0_eoi(unsigned int irq)
{
	/*
	 * Enable all CIU interrupts again.  We don't need to disable
	 * IRQs to make these atomic since they are already disabled
	 * earlier in the low level interrupt code.
	 */
	set_c0_status(0x100 << 2);
}

static int next_coreid_for_irq(struct irq_desc *desc)
{

#ifdef CONFIG_SMP
	int coreid;
	int weight = cpumask_weight(desc->affinity);

	if (weight > 1) {
		int cpu = smp_processor_id();
		for (;;) {
			cpu = cpumask_next(cpu, desc->affinity);
			if (cpu >= nr_cpu_ids) {
				cpu = -1;
				continue;
			} else if (cpumask_test_cpu(cpu, cpu_online_mask)) {
				break;
			}
		}
		coreid = octeon_coreid_for_cpu(cpu);
	} else if (weight == 1) {
		coreid = octeon_coreid_for_cpu(cpumask_first(desc->affinity));
	} else {
		coreid = cvmx_get_core_num();
	}
	return coreid;
#else
	return cvmx_get_core_num();
#endif
}

static void octeon_irq_ciu0_enable(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int coreid = next_coreid_for_irq(desc);
	unsigned long flags;
	uint64_t en0;
	int bit = irq - OCTEON_IRQ_WORKQ0;	/* Bit 0-63 of EN0 */

	raw_spin_lock_irqsave(&octeon_irq_ciu0_lock, flags);
	en0 = cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
	en0 |= 1ull << bit;
	cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), en0);
	cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
	raw_spin_unlock_irqrestore(&octeon_irq_ciu0_lock, flags);
}

static void octeon_irq_ciu0_enable_mbox(unsigned int irq)
{
	int coreid = cvmx_get_core_num();
	unsigned long flags;
	uint64_t en0;
	int bit = irq - OCTEON_IRQ_WORKQ0;	/* Bit 0-63 of EN0 */

	raw_spin_lock_irqsave(&octeon_irq_ciu0_lock, flags);
	en0 = cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
	en0 |= 1ull << bit;
	cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), en0);
	cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
	raw_spin_unlock_irqrestore(&octeon_irq_ciu0_lock, flags);
}

static void octeon_irq_ciu0_disable(unsigned int irq)
{
	int bit = irq - OCTEON_IRQ_WORKQ0;	/* Bit 0-63 of EN0 */
	unsigned long flags;
	uint64_t en0;
	int cpu;
	raw_spin_lock_irqsave(&octeon_irq_ciu0_lock, flags);
	for_each_online_cpu(cpu) {
		int coreid = octeon_coreid_for_cpu(cpu);
		en0 = cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
		en0 &= ~(1ull << bit);
		cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), en0);
	}
	/*
	 * We need to do a read after the last update to make sure all
	 * of them are done.
	 */
	cvmx_read_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num() * 2));
	raw_spin_unlock_irqrestore(&octeon_irq_ciu0_lock, flags);
}

/*
 * Enable the irq on the next core in the affinity set for chips that
 * have the EN*_W1{S,C} registers.
 */
static void octeon_irq_ciu0_enable_v2(unsigned int irq)
{
	int index;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WORKQ0);
	struct irq_desc *desc = irq_to_desc(irq);

	if ((desc->status & IRQ_DISABLED) == 0) {
		index = next_coreid_for_irq(desc) * 2;
		cvmx_write_csr(CVMX_CIU_INTX_EN0_W1S(index), mask);
	}
}

/*
 * Enable the irq on the current CPU for chips that
 * have the EN*_W1{S,C} registers.
 */
static void octeon_irq_ciu0_enable_mbox_v2(unsigned int irq)
{
	int index;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WORKQ0);

	index = cvmx_get_core_num() * 2;
	cvmx_write_csr(CVMX_CIU_INTX_EN0_W1S(index), mask);
}

/*
 * Disable the irq on the current core for chips that have the EN*_W1{S,C}
 * registers.
 */
static void octeon_irq_ciu0_ack_v2(unsigned int irq)
{
	int index = cvmx_get_core_num() * 2;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WORKQ0);

	switch (irq) {
	case OCTEON_IRQ_GMX_DRP0:
	case OCTEON_IRQ_GMX_DRP1:
	case OCTEON_IRQ_IPD_DRP:
	case OCTEON_IRQ_KEY_ZERO:
	case OCTEON_IRQ_TIMER0:
	case OCTEON_IRQ_TIMER1:
	case OCTEON_IRQ_TIMER2:
	case OCTEON_IRQ_TIMER3:
		/*
		 * CIU timer type interrupts must be acknoleged by
		 * writing a '1' bit to their sum0 bit.
		 */
		cvmx_write_csr(CVMX_CIU_INTX_SUM0(index), mask);
		break;
	default:
		break;
	}

	cvmx_write_csr(CVMX_CIU_INTX_EN0_W1C(index), mask);
}

/*
 * Enable the irq on the current core for chips that have the EN*_W1{S,C}
 * registers.
 */
static void octeon_irq_ciu0_eoi_mbox_v2(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int index = cvmx_get_core_num() * 2;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WORKQ0);

	if (likely((desc->status & IRQ_DISABLED) == 0))
		cvmx_write_csr(CVMX_CIU_INTX_EN0_W1S(index), mask);
}

/*
 * Disable the irq on the all cores for chips that have the EN*_W1{S,C}
 * registers.
 */
static void octeon_irq_ciu0_disable_all_v2(unsigned int irq)
{
	u64 mask = 1ull << (irq - OCTEON_IRQ_WORKQ0);
	int index;
	int cpu;
	for_each_online_cpu(cpu) {
		index = octeon_coreid_for_cpu(cpu) * 2;
		cvmx_write_csr(CVMX_CIU_INTX_EN0_W1C(index), mask);
	}
}

#ifdef CONFIG_SMP
static int octeon_irq_ciu0_set_affinity(unsigned int irq, const struct cpumask *dest)
{
	int cpu;
	struct irq_desc *desc = irq_to_desc(irq);
	int enable_one = (desc->status & IRQ_DISABLED) == 0;
	unsigned long flags;
	int bit = irq - OCTEON_IRQ_WORKQ0;	/* Bit 0-63 of EN0 */

	/*
	 * For non-v2 CIU, we will allow only single CPU affinity.
	 * This removes the need to do locking in the .ack/.eoi
	 * functions.
	 */
	if (cpumask_weight(dest) != 1)
		return -EINVAL;

	raw_spin_lock_irqsave(&octeon_irq_ciu0_lock, flags);
	for_each_online_cpu(cpu) {
		int coreid = octeon_coreid_for_cpu(cpu);
		uint64_t en0 =
			cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2));
		if (cpumask_test_cpu(cpu, dest) && enable_one) {
			enable_one = 0;
			en0 |= 1ull << bit;
		} else {
			en0 &= ~(1ull << bit);
		}
		cvmx_write_csr(CVMX_CIU_INTX_EN0(coreid * 2), en0);
	}
	/*
	 * We need to do a read after the last update to make sure all
	 * of them are done.
	 */
	cvmx_read_csr(CVMX_CIU_INTX_EN0(cvmx_get_core_num() * 2));
	raw_spin_unlock_irqrestore(&octeon_irq_ciu0_lock, flags);

	return 0;
}

/*
 * Set affinity for the irq for chips that have the EN*_W1{S,C}
 * registers.
 */
static int octeon_irq_ciu0_set_affinity_v2(unsigned int irq,
					   const struct cpumask *dest)
{
	int cpu;
	int index;
	struct irq_desc *desc = irq_to_desc(irq);
	int enable_one = (desc->status & IRQ_DISABLED) == 0;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WORKQ0);

	for_each_online_cpu(cpu) {
		index = octeon_coreid_for_cpu(cpu) * 2;
		if (cpumask_test_cpu(cpu, dest) && enable_one) {
			enable_one = 0;
			cvmx_write_csr(CVMX_CIU_INTX_EN0_W1S(index), mask);
		} else {
			cvmx_write_csr(CVMX_CIU_INTX_EN0_W1C(index), mask);
		}
	}
	return 0;
}
#endif

/*
 * Newer octeon chips have support for lockless CIU operation.
 */
static struct irq_chip octeon_irq_chip_ciu0_v2 = {
	.name = "CIU0",
	.enable = octeon_irq_ciu0_enable_v2,
	.disable = octeon_irq_ciu0_disable_all_v2,
	.eoi = octeon_irq_ciu0_enable_v2,
#ifdef CONFIG_SMP
	.set_affinity = octeon_irq_ciu0_set_affinity_v2,
#endif
};

static struct irq_chip octeon_irq_chip_ciu0 = {
	.name = "CIU0",
	.enable = octeon_irq_ciu0_enable,
	.disable = octeon_irq_ciu0_disable,
	.eoi = octeon_irq_ciu0_eoi,
#ifdef CONFIG_SMP
	.set_affinity = octeon_irq_ciu0_set_affinity,
#endif
};

/* The mbox versions don't do any affinity or round-robin. */
static struct irq_chip octeon_irq_chip_ciu0_mbox_v2 = {
	.name = "CIU0-M",
	.enable = octeon_irq_ciu0_enable_mbox_v2,
	.disable = octeon_irq_ciu0_disable,
	.eoi = octeon_irq_ciu0_eoi_mbox_v2,
};

static struct irq_chip octeon_irq_chip_ciu0_mbox = {
	.name = "CIU0-M",
	.enable = octeon_irq_ciu0_enable_mbox,
	.disable = octeon_irq_ciu0_disable,
	.eoi = octeon_irq_ciu0_eoi,
};

static void octeon_irq_ciu1_ack(unsigned int irq)
{
	/*
	 * In order to avoid any locking accessing the CIU, we
	 * acknowledge CIU interrupts by disabling all of them.  This
	 * way we can use a per core register and avoid any out of
	 * core locking requirements.  This has the side affect that
	 * CIU interrupts can't be processed recursively.  We don't
	 * need to disable IRQs to make these atomic since they are
	 * already disabled earlier in the low level interrupt code.
	 */
	clear_c0_status(0x100 << 3);
}

static void octeon_irq_ciu1_eoi(unsigned int irq)
{
	/*
	 * Enable all CIU interrupts again.  We don't need to disable
	 * IRQs to make these atomic since they are already disabled
	 * earlier in the low level interrupt code.
	 */
	set_c0_status(0x100 << 3);
}

static void octeon_irq_ciu1_enable(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int coreid = next_coreid_for_irq(desc);
	unsigned long flags;
	uint64_t en1;
	int bit = irq - OCTEON_IRQ_WDOG0;	/* Bit 0-63 of EN1 */

	raw_spin_lock_irqsave(&octeon_irq_ciu1_lock, flags);
	en1 = cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
	en1 |= 1ull << bit;
	cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), en1);
	cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
	raw_spin_unlock_irqrestore(&octeon_irq_ciu1_lock, flags);
}

/*
 * Watchdog interrupts are special.  They are associated with a single
 * core, so we hardwire the affinity to that core.
 */
static void octeon_irq_ciu1_wd_enable(unsigned int irq)
{
	unsigned long flags;
	uint64_t en1;
	int bit = irq - OCTEON_IRQ_WDOG0;	/* Bit 0-63 of EN1 */
	int coreid = bit;

	raw_spin_lock_irqsave(&octeon_irq_ciu1_lock, flags);
	en1 = cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
	en1 |= 1ull << bit;
	cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), en1);
	cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
	raw_spin_unlock_irqrestore(&octeon_irq_ciu1_lock, flags);
}

static void octeon_irq_ciu1_disable(unsigned int irq)
{
	int bit = irq - OCTEON_IRQ_WDOG0;	/* Bit 0-63 of EN1 */
	unsigned long flags;
	uint64_t en1;
	int cpu;
	raw_spin_lock_irqsave(&octeon_irq_ciu1_lock, flags);
	for_each_online_cpu(cpu) {
		int coreid = octeon_coreid_for_cpu(cpu);
		en1 = cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
		en1 &= ~(1ull << bit);
		cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), en1);
	}
	/*
	 * We need to do a read after the last update to make sure all
	 * of them are done.
	 */
	cvmx_read_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num() * 2 + 1));
	raw_spin_unlock_irqrestore(&octeon_irq_ciu1_lock, flags);
}

/*
 * Enable the irq on the current core for chips that have the EN*_W1{S,C}
 * registers.
 */
static void octeon_irq_ciu1_enable_v2(unsigned int irq)
{
	int index;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WDOG0);
	struct irq_desc *desc = irq_to_desc(irq);

	if ((desc->status & IRQ_DISABLED) == 0) {
		index = next_coreid_for_irq(desc) * 2 + 1;
		cvmx_write_csr(CVMX_CIU_INTX_EN1_W1S(index), mask);
	}
}

/*
 * Watchdog interrupts are special.  They are associated with a single
 * core, so we hardwire the affinity to that core.
 */
static void octeon_irq_ciu1_wd_enable_v2(unsigned int irq)
{
	int index;
	int coreid = irq - OCTEON_IRQ_WDOG0;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WDOG0);
	struct irq_desc *desc = irq_to_desc(irq);

	if ((desc->status & IRQ_DISABLED) == 0) {
		index = coreid * 2 + 1;
		cvmx_write_csr(CVMX_CIU_INTX_EN1_W1S(index), mask);
	}
}

/*
 * Disable the irq on the current core for chips that have the EN*_W1{S,C}
 * registers.
 */
static void octeon_irq_ciu1_ack_v2(unsigned int irq)
{
	int index = cvmx_get_core_num() * 2 + 1;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WDOG0);

	cvmx_write_csr(CVMX_CIU_INTX_EN1_W1C(index), mask);
}

/*
 * Disable the irq on the all cores for chips that have the EN*_W1{S,C}
 * registers.
 */
static void octeon_irq_ciu1_disable_all_v2(unsigned int irq)
{
	u64 mask = 1ull << (irq - OCTEON_IRQ_WDOG0);
	int index;
	int cpu;
	for_each_online_cpu(cpu) {
		index = octeon_coreid_for_cpu(cpu) * 2 + 1;
		cvmx_write_csr(CVMX_CIU_INTX_EN1_W1C(index), mask);
	}
}

#ifdef CONFIG_SMP
static int octeon_irq_ciu1_set_affinity(unsigned int irq,
					const struct cpumask *dest)
{
	int cpu;
	struct irq_desc *desc = irq_to_desc(irq);
	int enable_one = (desc->status & IRQ_DISABLED) == 0;
	unsigned long flags;
	int bit = irq - OCTEON_IRQ_WDOG0;	/* Bit 0-63 of EN1 */

	/*
	 * For non-v2 CIU, we will allow only single CPU affinity.
	 * This removes the need to do locking in the .ack/.eoi
	 * functions.
	 */
	if (cpumask_weight(dest) != 1)
		return -EINVAL;

	raw_spin_lock_irqsave(&octeon_irq_ciu1_lock, flags);
	for_each_online_cpu(cpu) {
		int coreid = octeon_coreid_for_cpu(cpu);
		uint64_t en1 =
			cvmx_read_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1));
		if (cpumask_test_cpu(cpu, dest) && enable_one) {
			enable_one = 0;
			en1 |= 1ull << bit;
		} else {
			en1 &= ~(1ull << bit);
		}
		cvmx_write_csr(CVMX_CIU_INTX_EN1(coreid * 2 + 1), en1);
	}
	/*
	 * We need to do a read after the last update to make sure all
	 * of them are done.
	 */
	cvmx_read_csr(CVMX_CIU_INTX_EN1(cvmx_get_core_num() * 2 + 1));
	raw_spin_unlock_irqrestore(&octeon_irq_ciu1_lock, flags);

	return 0;
}

/*
 * Set affinity for the irq for chips that have the EN*_W1{S,C}
 * registers.
 */
static int octeon_irq_ciu1_set_affinity_v2(unsigned int irq,
					   const struct cpumask *dest)
{
	int cpu;
	int index;
	struct irq_desc *desc = irq_to_desc(irq);
	int enable_one = (desc->status & IRQ_DISABLED) == 0;
	u64 mask = 1ull << (irq - OCTEON_IRQ_WDOG0);
	for_each_online_cpu(cpu) {
		index = octeon_coreid_for_cpu(cpu) * 2 + 1;
		if (cpumask_test_cpu(cpu, dest) && enable_one) {
			enable_one = 0;
			cvmx_write_csr(CVMX_CIU_INTX_EN1_W1S(index), mask);
		} else {
			cvmx_write_csr(CVMX_CIU_INTX_EN1_W1C(index), mask);
		}
	}
	return 0;
}
#endif

/*
 * Newer octeon chips have support for lockless CIU operation.
 */
static struct irq_chip octeon_irq_chip_ciu1_v2 = {
	.name = "CIU1",
	.enable = octeon_irq_ciu1_enable_v2,
	.disable = octeon_irq_ciu1_disable_all_v2,
	.eoi = octeon_irq_ciu1_enable_v2,
#ifdef CONFIG_SMP
	.set_affinity = octeon_irq_ciu1_set_affinity_v2,
#endif
};

static struct irq_chip octeon_irq_chip_ciu1 = {
	.name = "CIU1",
	.enable = octeon_irq_ciu1_enable,
	.disable = octeon_irq_ciu1_disable,
	.eoi = octeon_irq_ciu1_eoi,
#ifdef CONFIG_SMP
	.set_affinity = octeon_irq_ciu1_set_affinity,
#endif
};

static struct irq_chip octeon_irq_chip_ciu1_wd_v2 = {
	.name = "CIU1-W",
	.enable = octeon_irq_ciu1_wd_enable_v2,
	.disable = octeon_irq_ciu1_disable_all_v2,
	.eoi = octeon_irq_ciu1_wd_enable_v2,
};

static struct irq_chip octeon_irq_chip_ciu1_wd = {
	.name = "CIU1-W",
	.enable = octeon_irq_ciu1_wd_enable,
	.disable = octeon_irq_ciu1_disable,
	.eoi = octeon_irq_ciu1_eoi,
};

static void (*octeon_ciu0_ack)(unsigned int);
static void (*octeon_ciu1_ack)(unsigned int);

void __init arch_init_irq(void)
{
	unsigned int irq;
	struct irq_chip *chip0;
	struct irq_chip *chip0_mbox;
	struct irq_chip *chip1;
	struct irq_chip *chip1_wd;

#ifdef CONFIG_SMP
	/* Set the default affinity to the boot cpu. */
	cpumask_clear(irq_default_affinity);
	cpumask_set_cpu(smp_processor_id(), irq_default_affinity);
#endif

	if (NR_IRQS < OCTEON_IRQ_LAST)
		pr_err("octeon_irq_init: NR_IRQS is set too low\n");

	if (OCTEON_IS_MODEL(OCTEON_CN58XX_PASS2_X) ||
	    OCTEON_IS_MODEL(OCTEON_CN56XX_PASS2_X) ||
	    OCTEON_IS_MODEL(OCTEON_CN52XX_PASS2_X)) {
		octeon_ciu0_ack = octeon_irq_ciu0_ack_v2;
		octeon_ciu1_ack = octeon_irq_ciu1_ack_v2;
		chip0 = &octeon_irq_chip_ciu0_v2;
		chip0_mbox = &octeon_irq_chip_ciu0_mbox_v2;
		chip1 = &octeon_irq_chip_ciu1_v2;
		chip1_wd = &octeon_irq_chip_ciu1_wd_v2;
	} else {
		octeon_ciu0_ack = octeon_irq_ciu0_ack;
		octeon_ciu1_ack = octeon_irq_ciu1_ack;
		chip0 = &octeon_irq_chip_ciu0;
		chip0_mbox = &octeon_irq_chip_ciu0_mbox;
		chip1 = &octeon_irq_chip_ciu1;
		chip1_wd = &octeon_irq_chip_ciu1_wd;
	}

	/* 0 - 15 reserved for i8259 master and slave controller. */

	/* 17 - 23 Mips internal */
	for (irq = OCTEON_IRQ_SW0; irq <= OCTEON_IRQ_TIMER; irq++) {
		set_irq_chip_and_handler(irq, &octeon_irq_chip_core,
					 handle_percpu_irq);
	}

	/* 24 - 87 CIU_INT_SUM0 */
	for (irq = OCTEON_IRQ_WORKQ0; irq <= OCTEON_IRQ_BOOTDMA; irq++) {
		switch (irq) {
		case OCTEON_IRQ_MBOX0:
		case OCTEON_IRQ_MBOX1:
			set_irq_chip_and_handler(irq, chip0_mbox, handle_percpu_irq);
			break;
		default:
			set_irq_chip_and_handler(irq, chip0, handle_fasteoi_irq);
			break;
		}
	}

	/* 88 - 151 CIU_INT_SUM1 */
	for (irq = OCTEON_IRQ_WDOG0; irq <= OCTEON_IRQ_WDOG15; irq++)
		set_irq_chip_and_handler(irq, chip1_wd, handle_fasteoi_irq);

	for (irq = OCTEON_IRQ_UART2; irq <= OCTEON_IRQ_RESERVED151; irq++)
		set_irq_chip_and_handler(irq, chip1, handle_fasteoi_irq);

	set_c0_status(0x300 << 2);
}

asmlinkage void plat_irq_dispatch(void)
{
	const unsigned long core_id = cvmx_get_core_num();
	const uint64_t ciu_sum0_address = CVMX_CIU_INTX_SUM0(core_id * 2);
	const uint64_t ciu_en0_address = CVMX_CIU_INTX_EN0(core_id * 2);
	const uint64_t ciu_sum1_address = CVMX_CIU_INT_SUM1;
	const uint64_t ciu_en1_address = CVMX_CIU_INTX_EN1(core_id * 2 + 1);
	unsigned long cop0_cause;
	unsigned long cop0_status;
	uint64_t ciu_en;
	uint64_t ciu_sum;
	unsigned int irq;

	while (1) {
		cop0_cause = read_c0_cause();
		cop0_status = read_c0_status();
		cop0_cause &= cop0_status;
		cop0_cause &= ST0_IM;

		if (unlikely(cop0_cause & STATUSF_IP2)) {
			ciu_sum = cvmx_read_csr(ciu_sum0_address);
			ciu_en = cvmx_read_csr(ciu_en0_address);
			ciu_sum &= ciu_en;
			if (likely(ciu_sum)) {
				irq = fls64(ciu_sum) + OCTEON_IRQ_WORKQ0 - 1;
				octeon_ciu0_ack(irq);
				do_IRQ(irq);
			} else {
				spurious_interrupt();
			}
		} else if (unlikely(cop0_cause & STATUSF_IP3)) {
			ciu_sum = cvmx_read_csr(ciu_sum1_address);
			ciu_en = cvmx_read_csr(ciu_en1_address);
			ciu_sum &= ciu_en;
			if (likely(ciu_sum)) {
				irq = fls64(ciu_sum) + OCTEON_IRQ_WDOG0 - 1;
				octeon_ciu1_ack(irq);
				do_IRQ(irq);
			} else {
				spurious_interrupt();
			}
		} else if (likely(cop0_cause)) {
			do_IRQ(fls(cop0_cause) - 9 + MIPS_CPU_IRQ_BASE);
		} else {
			break;
		}
	}
}

#ifdef CONFIG_HOTPLUG_CPU

void fixup_irqs(void)
{
	int irq;
	struct irq_desc *desc;
	cpumask_t new_affinity;
	unsigned long flags;
	int do_set_affinity;
	int cpu;

	cpu = smp_processor_id();

	for (irq = OCTEON_IRQ_SW0; irq <= OCTEON_IRQ_TIMER; irq++)
		octeon_irq_core_disable_local(irq);

	for (irq = OCTEON_IRQ_WORKQ0; irq < OCTEON_IRQ_LAST; irq++) {
		desc = irq_to_desc(irq);
		switch (irq) {
		case OCTEON_IRQ_MBOX0:
		case OCTEON_IRQ_MBOX1:
			/* The eoi function will disable them on this CPU. */
			desc->chip->eoi(irq);
			break;
		case OCTEON_IRQ_WDOG0:
		case OCTEON_IRQ_WDOG1:
		case OCTEON_IRQ_WDOG2:
		case OCTEON_IRQ_WDOG3:
		case OCTEON_IRQ_WDOG4:
		case OCTEON_IRQ_WDOG5:
		case OCTEON_IRQ_WDOG6:
		case OCTEON_IRQ_WDOG7:
		case OCTEON_IRQ_WDOG8:
		case OCTEON_IRQ_WDOG9:
		case OCTEON_IRQ_WDOG10:
		case OCTEON_IRQ_WDOG11:
		case OCTEON_IRQ_WDOG12:
		case OCTEON_IRQ_WDOG13:
		case OCTEON_IRQ_WDOG14:
		case OCTEON_IRQ_WDOG15:
			/*
			 * These have special per CPU semantics and
			 * are handled in the watchdog driver.
			 */
			break;
		default:
			raw_spin_lock_irqsave(&desc->lock, flags);
			/*
			 * If this irq has an action, it is in use and
			 * must be migrated if it has affinity to this
			 * cpu.
			 */
			if (desc->action && cpumask_test_cpu(cpu, desc->affinity)) {
				if (cpumask_weight(desc->affinity) > 1) {
					/*
					 * It has multi CPU affinity,
					 * just remove this CPU from
					 * the affinity set.
					 */
					cpumask_copy(&new_affinity, desc->affinity);
					cpumask_clear_cpu(cpu, &new_affinity);
				} else {
					/*
					 * Otherwise, put it on lowest
					 * numbered online CPU.
					 */
					cpumask_clear(&new_affinity);
					cpumask_set_cpu(cpumask_first(cpu_online_mask), &new_affinity);
				}
				do_set_affinity = 1;
			} else {
				do_set_affinity = 0;
			}
			raw_spin_unlock_irqrestore(&desc->lock, flags);

			if (do_set_affinity)
				irq_set_affinity(irq, &new_affinity);

			break;
		}
	}
}

#endif /* CONFIG_HOTPLUG_CPU */
