/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/bitmap.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/irq.h>
#include <linux/clocksource.h>

#include <asm/io.h>
#include <asm/gic.h>
#include <asm/setup.h>
#include <asm/traps.h>
#include <linux/hardirq.h>
#include <asm-generic/bitops/find.h>

unsigned int gic_frequency;
unsigned int gic_present;
unsigned long _gic_base;
unsigned int gic_irq_flags[GIC_NUM_INTRS];
unsigned int gic_cpu_pin;

struct gic_pcpu_mask {
	DECLARE_BITMAP(pcpu_mask, GIC_NUM_INTRS);
};

struct gic_pending_regs {
	DECLARE_BITMAP(pending, GIC_NUM_INTRS);
};

struct gic_intrmask_regs {
	DECLARE_BITMAP(intrmask, GIC_NUM_INTRS);
};

static struct gic_pcpu_mask pcpu_masks[NR_CPUS];
static struct gic_pending_regs pending_regs[NR_CPUS];
static struct gic_intrmask_regs intrmask_regs[NR_CPUS];
static DEFINE_SPINLOCK(gic_lock);
static struct irq_domain *gic_irq_domain;

static void __gic_irq_dispatch(void);

#if defined(CONFIG_CSRC_GIC) || defined(CONFIG_CEVT_GIC)
cycle_t gic_read_count(void)
{
	unsigned int hi, hi2, lo;

	do {
		GICREAD(GIC_REG(SHARED, GIC_SH_COUNTER_63_32), hi);
		GICREAD(GIC_REG(SHARED, GIC_SH_COUNTER_31_00), lo);
		GICREAD(GIC_REG(SHARED, GIC_SH_COUNTER_63_32), hi2);
	} while (hi2 != hi);

	return (((cycle_t) hi) << 32) + lo;
}

void gic_write_compare(cycle_t cnt)
{
	GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_COMPARE_HI),
				(int)(cnt >> 32));
	GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_COMPARE_LO),
				(int)(cnt & 0xffffffff));
}

void gic_write_cpu_compare(cycle_t cnt, int cpu)
{
	unsigned long flags;

	local_irq_save(flags);

	GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_OTHER_ADDR), cpu);
	GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_COMPARE_HI),
				(int)(cnt >> 32));
	GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_COMPARE_LO),
				(int)(cnt & 0xffffffff));

	local_irq_restore(flags);
}

cycle_t gic_read_compare(void)
{
	unsigned int hi, lo;

	GICREAD(GIC_REG(VPE_LOCAL, GIC_VPE_COMPARE_HI), hi);
	GICREAD(GIC_REG(VPE_LOCAL, GIC_VPE_COMPARE_LO), lo);

	return (((cycle_t) hi) << 32) + lo;
}
#endif

unsigned int gic_get_timer_pending(void)
{
	unsigned int vpe_pending;

	GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_OTHER_ADDR), 0);
	GICREAD(GIC_REG(VPE_OTHER, GIC_VPE_PEND), vpe_pending);
	return vpe_pending & GIC_VPE_PEND_TIMER_MSK;
}

void gic_bind_eic_interrupt(int irq, int set)
{
	/* Convert irq vector # to hw int # */
	irq -= GIC_PIN_TO_VEC_OFFSET;

	/* Set irq to use shadow set */
	GICWRITE(GIC_REG_ADDR(VPE_LOCAL, GIC_VPE_EIC_SS(irq)), set);
}

void gic_send_ipi(unsigned int intr)
{
	GICWRITE(GIC_REG(SHARED, GIC_SH_WEDGE), 0x80000000 | intr);
}

static void __init vpe_local_setup(unsigned int numvpes)
{
	unsigned long timer_intr = GIC_INT_TMR;
	unsigned long perf_intr = GIC_INT_PERFCTR;
	unsigned int vpe_ctl;
	int i;

	if (cpu_has_veic) {
		/*
		 * GIC timer interrupt -> CPU HW Int X (vector X+2) ->
		 * map to pin X+2-1 (since GIC adds 1)
		 */
		timer_intr += (GIC_CPU_TO_VEC_OFFSET - GIC_PIN_TO_VEC_OFFSET);
		/*
		 * GIC perfcnt interrupt -> CPU HW Int X (vector X+2) ->
		 * map to pin X+2-1 (since GIC adds 1)
		 */
		perf_intr += (GIC_CPU_TO_VEC_OFFSET - GIC_PIN_TO_VEC_OFFSET);
	}

	/*
	 * Setup the default performance counter timer interrupts
	 * for all VPEs
	 */
	for (i = 0; i < numvpes; i++) {
		GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_OTHER_ADDR), i);

		/* Are Interrupts locally routable? */
		GICREAD(GIC_REG(VPE_OTHER, GIC_VPE_CTL), vpe_ctl);
		if (vpe_ctl & GIC_VPE_CTL_TIMER_RTBL_MSK)
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_TIMER_MAP),
				 GIC_MAP_TO_PIN_MSK | timer_intr);
		if (cpu_has_veic) {
			set_vi_handler(timer_intr + GIC_PIN_TO_VEC_OFFSET,
				       __gic_irq_dispatch);
		}

		if (vpe_ctl & GIC_VPE_CTL_PERFCNT_RTBL_MSK)
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_PERFCTR_MAP),
				 GIC_MAP_TO_PIN_MSK | perf_intr);
		if (cpu_has_veic) {
			set_vi_handler(perf_intr + GIC_PIN_TO_VEC_OFFSET,
				       __gic_irq_dispatch);
		}
	}
}

unsigned int gic_compare_int(void)
{
	unsigned int pending;

	GICREAD(GIC_REG(VPE_LOCAL, GIC_VPE_PEND), pending);
	if (pending & GIC_VPE_PEND_CMP_MSK)
		return 1;
	else
		return 0;
}

void gic_get_int_mask(unsigned long *dst, const unsigned long *src)
{
	unsigned int i;
	unsigned long *pending, *intrmask, *pcpu_mask;
	unsigned long *pending_abs, *intrmask_abs;

	/* Get per-cpu bitmaps */
	pending = pending_regs[smp_processor_id()].pending;
	intrmask = intrmask_regs[smp_processor_id()].intrmask;
	pcpu_mask = pcpu_masks[smp_processor_id()].pcpu_mask;

	pending_abs = (unsigned long *) GIC_REG_ABS_ADDR(SHARED,
							 GIC_SH_PEND_31_0_OFS);
	intrmask_abs = (unsigned long *) GIC_REG_ABS_ADDR(SHARED,
							  GIC_SH_MASK_31_0_OFS);

	for (i = 0; i < BITS_TO_LONGS(GIC_NUM_INTRS); i++) {
		GICREAD(*pending_abs, pending[i]);
		GICREAD(*intrmask_abs, intrmask[i]);
		pending_abs++;
		intrmask_abs++;
	}

	bitmap_and(pending, pending, intrmask, GIC_NUM_INTRS);
	bitmap_and(pending, pending, pcpu_mask, GIC_NUM_INTRS);
	bitmap_and(dst, src, pending, GIC_NUM_INTRS);
}

unsigned int gic_get_int(void)
{
	DECLARE_BITMAP(interrupts, GIC_NUM_INTRS);

	bitmap_fill(interrupts, GIC_NUM_INTRS);
	gic_get_int_mask(interrupts, interrupts);

	return find_first_bit(interrupts, GIC_NUM_INTRS);
}

static void gic_mask_irq(struct irq_data *d)
{
	GIC_CLR_INTR_MASK(d->hwirq);
}

static void gic_unmask_irq(struct irq_data *d)
{
	GIC_SET_INTR_MASK(d->hwirq);
}

static void gic_ack_irq(struct irq_data *d)
{
	unsigned int irq = d->hwirq;

	/* Clear edge detector */
	if (gic_irq_flags[irq] & GIC_TRIG_EDGE)
		GICWRITE(GIC_REG(SHARED, GIC_SH_WEDGE), irq);
}

static int gic_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int irq = d->hwirq;
	unsigned long flags;
	bool is_edge;

	spin_lock_irqsave(&gic_lock, flags);
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_FALLING:
		GIC_SET_POLARITY(irq, GIC_POL_NEG);
		GIC_SET_TRIGGER(irq, GIC_TRIG_EDGE);
		GIC_SET_DUAL(irq, GIC_TRIG_DUAL_DISABLE);
		is_edge = true;
		break;
	case IRQ_TYPE_EDGE_RISING:
		GIC_SET_POLARITY(irq, GIC_POL_POS);
		GIC_SET_TRIGGER(irq, GIC_TRIG_EDGE);
		GIC_SET_DUAL(irq, GIC_TRIG_DUAL_DISABLE);
		is_edge = true;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		/* polarity is irrelevant in this case */
		GIC_SET_TRIGGER(irq, GIC_TRIG_EDGE);
		GIC_SET_DUAL(irq, GIC_TRIG_DUAL_ENABLE);
		is_edge = true;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		GIC_SET_POLARITY(irq, GIC_POL_NEG);
		GIC_SET_TRIGGER(irq, GIC_TRIG_LEVEL);
		GIC_SET_DUAL(irq, GIC_TRIG_DUAL_DISABLE);
		is_edge = false;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
	default:
		GIC_SET_POLARITY(irq, GIC_POL_POS);
		GIC_SET_TRIGGER(irq, GIC_TRIG_LEVEL);
		GIC_SET_DUAL(irq, GIC_TRIG_DUAL_DISABLE);
		is_edge = false;
		break;
	}

	if (is_edge) {
		gic_irq_flags[irq] |= GIC_TRIG_EDGE;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
	} else {
		gic_irq_flags[irq] &= ~GIC_TRIG_EDGE;
		__irq_set_handler_locked(d->irq, handle_level_irq);
	}
	spin_unlock_irqrestore(&gic_lock, flags);

	return 0;
}

#ifdef CONFIG_SMP
static int gic_set_affinity(struct irq_data *d, const struct cpumask *cpumask,
			    bool force)
{
	unsigned int irq = d->hwirq;
	cpumask_t	tmp = CPU_MASK_NONE;
	unsigned long	flags;
	int		i;

	cpumask_and(&tmp, cpumask, cpu_online_mask);
	if (cpus_empty(tmp))
		return -EINVAL;

	/* Assumption : cpumask refers to a single CPU */
	spin_lock_irqsave(&gic_lock, flags);

	/* Re-route this IRQ */
	GIC_SH_MAP_TO_VPE_SMASK(irq, first_cpu(tmp));

	/* Update the pcpu_masks */
	for (i = 0; i < NR_CPUS; i++)
		clear_bit(irq, pcpu_masks[i].pcpu_mask);
	set_bit(irq, pcpu_masks[first_cpu(tmp)].pcpu_mask);

	cpumask_copy(d->affinity, cpumask);
	spin_unlock_irqrestore(&gic_lock, flags);

	return IRQ_SET_MASK_OK_NOCOPY;
}
#endif

static struct irq_chip gic_irq_controller = {
	.name			=	"MIPS GIC",
	.irq_ack		=	gic_ack_irq,
	.irq_mask		=	gic_mask_irq,
	.irq_unmask		=	gic_unmask_irq,
	.irq_set_type		=	gic_set_type,
#ifdef CONFIG_SMP
	.irq_set_affinity	=	gic_set_affinity,
#endif
};

static void __gic_irq_dispatch(void)
{
	unsigned int intr, virq;

	while ((intr = gic_get_int()) != GIC_NUM_INTRS) {
		virq = irq_linear_revmap(gic_irq_domain, intr);
		do_IRQ(virq);
	}
}

static void gic_irq_dispatch(unsigned int irq, struct irq_desc *desc)
{
	__gic_irq_dispatch();
}

#ifdef CONFIG_MIPS_GIC_IPI
static int gic_resched_int_base;
static int gic_call_int_base;

unsigned int plat_ipi_resched_int_xlate(unsigned int cpu)
{
	return gic_resched_int_base + cpu;
}

unsigned int plat_ipi_call_int_xlate(unsigned int cpu)
{
	return gic_call_int_base + cpu;
}

static irqreturn_t ipi_resched_interrupt(int irq, void *dev_id)
{
	scheduler_ipi();

	return IRQ_HANDLED;
}

static irqreturn_t ipi_call_interrupt(int irq, void *dev_id)
{
	smp_call_function_interrupt();

	return IRQ_HANDLED;
}

static struct irqaction irq_resched = {
	.handler	= ipi_resched_interrupt,
	.flags		= IRQF_PERCPU,
	.name		= "IPI resched"
};

static struct irqaction irq_call = {
	.handler	= ipi_call_interrupt,
	.flags		= IRQF_PERCPU,
	.name		= "IPI call"
};

static __init void gic_ipi_init_one(unsigned int intr, int cpu,
				    struct irqaction *action)
{
	int virq = irq_create_mapping(gic_irq_domain, intr);
	int i;

	GIC_SH_MAP_TO_VPE_SMASK(intr, cpu);
	for (i = 0; i < NR_CPUS; i++)
		clear_bit(intr, pcpu_masks[i].pcpu_mask);
	set_bit(intr, pcpu_masks[cpu].pcpu_mask);

	irq_set_irq_type(virq, IRQ_TYPE_EDGE_RISING);

	irq_set_handler(virq, handle_percpu_irq);
	setup_irq(virq, action);
}

static __init void gic_ipi_init(void)
{
	int i;

	/* Use last 2 * NR_CPUS interrupts as IPIs */
	gic_resched_int_base = GIC_NUM_INTRS - nr_cpu_ids;
	gic_call_int_base = gic_resched_int_base - nr_cpu_ids;

	for (i = 0; i < nr_cpu_ids; i++) {
		gic_ipi_init_one(gic_call_int_base + i, i, &irq_call);
		gic_ipi_init_one(gic_resched_int_base + i, i, &irq_resched);
	}
}
#else
static inline void gic_ipi_init(void)
{
}
#endif

static void __init gic_basic_init(int numintrs, int numvpes)
{
	unsigned int i;

	board_bind_eic_interrupt = &gic_bind_eic_interrupt;

	/* Setup defaults */
	for (i = 0; i < numintrs; i++) {
		GIC_SET_POLARITY(i, GIC_POL_POS);
		GIC_SET_TRIGGER(i, GIC_TRIG_LEVEL);
		GIC_CLR_INTR_MASK(i);
		if (i < GIC_NUM_INTRS)
			gic_irq_flags[i] = 0;
	}

	vpe_local_setup(numvpes);
}

static int gic_irq_domain_map(struct irq_domain *d, unsigned int virq,
			      irq_hw_number_t hw)
{
	unsigned long flags;

	irq_set_chip_and_handler(virq, &gic_irq_controller, handle_level_irq);

	spin_lock_irqsave(&gic_lock, flags);
	GICWRITE(GIC_REG_ADDR(SHARED, GIC_SH_MAP_TO_PIN(hw)),
		 GIC_MAP_TO_PIN_MSK | gic_cpu_pin);
	/* Map to VPE 0 by default */
	GIC_SH_MAP_TO_VPE_SMASK(hw, 0);
	set_bit(hw, pcpu_masks[0].pcpu_mask);
	spin_unlock_irqrestore(&gic_lock, flags);

	return 0;
}

static struct irq_domain_ops gic_irq_domain_ops = {
	.map = gic_irq_domain_map,
	.xlate = irq_domain_xlate_twocell,
};

void __init gic_init(unsigned long gic_base_addr,
		     unsigned long gic_addrspace_size, unsigned int cpu_vec,
		     unsigned int irqbase)
{
	unsigned int gicconfig;
	int numvpes, numintrs;

	_gic_base = (unsigned long) ioremap_nocache(gic_base_addr,
						    gic_addrspace_size);

	GICREAD(GIC_REG(SHARED, GIC_SH_CONFIG), gicconfig);
	numintrs = (gicconfig & GIC_SH_CONFIG_NUMINTRS_MSK) >>
		   GIC_SH_CONFIG_NUMINTRS_SHF;
	numintrs = ((numintrs + 1) * 8);

	numvpes = (gicconfig & GIC_SH_CONFIG_NUMVPES_MSK) >>
		  GIC_SH_CONFIG_NUMVPES_SHF;
	numvpes = numvpes + 1;

	if (cpu_has_veic) {
		/* Always use vector 1 in EIC mode */
		gic_cpu_pin = 0;
		set_vi_handler(gic_cpu_pin + GIC_PIN_TO_VEC_OFFSET,
			       __gic_irq_dispatch);
	} else {
		gic_cpu_pin = cpu_vec - GIC_CPU_PIN_OFFSET;
		irq_set_chained_handler(MIPS_CPU_IRQ_BASE + cpu_vec,
					gic_irq_dispatch);
	}

	gic_irq_domain = irq_domain_add_simple(NULL, GIC_NUM_INTRS, irqbase,
					       &gic_irq_domain_ops, NULL);
	if (!gic_irq_domain)
		panic("Failed to add GIC IRQ domain");

	gic_basic_init(numintrs, numvpes);

	gic_ipi_init();
}
