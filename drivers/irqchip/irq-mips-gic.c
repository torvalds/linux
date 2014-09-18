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

struct gic_pcpu_mask {
	DECLARE_BITMAP(pcpu_mask, GIC_MAX_INTRS);
};

struct gic_pending_regs {
	DECLARE_BITMAP(pending, GIC_MAX_INTRS);
};

struct gic_intrmask_regs {
	DECLARE_BITMAP(intrmask, GIC_MAX_INTRS);
};

static struct gic_pcpu_mask pcpu_masks[NR_CPUS];
static struct gic_pending_regs pending_regs[NR_CPUS];
static struct gic_intrmask_regs intrmask_regs[NR_CPUS];
static DEFINE_SPINLOCK(gic_lock);
static struct irq_domain *gic_irq_domain;
static int gic_shared_intrs;
static int gic_vpes;
static unsigned int gic_cpu_pin;
static struct irq_chip gic_level_irq_controller, gic_edge_irq_controller;

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

static bool gic_local_irq_is_routable(int intr)
{
	u32 vpe_ctl;

	/* All local interrupts are routable in EIC mode. */
	if (cpu_has_veic)
		return true;

	GICREAD(GIC_REG(VPE_LOCAL, GIC_VPE_CTL), vpe_ctl);
	switch (intr) {
	case GIC_LOCAL_INT_TIMER:
		return vpe_ctl & GIC_VPE_CTL_TIMER_RTBL_MSK;
	case GIC_LOCAL_INT_PERFCTR:
		return vpe_ctl & GIC_VPE_CTL_PERFCNT_RTBL_MSK;
	case GIC_LOCAL_INT_FDC:
		return vpe_ctl & GIC_VPE_CTL_FDC_RTBL_MSK;
	case GIC_LOCAL_INT_SWINT0:
	case GIC_LOCAL_INT_SWINT1:
		return vpe_ctl & GIC_VPE_CTL_SWINT_RTBL_MSK;
	default:
		return true;
	}
}

unsigned int gic_get_timer_pending(void)
{
	unsigned int vpe_pending;

	GICREAD(GIC_REG(VPE_LOCAL, GIC_VPE_PEND), vpe_pending);
	return vpe_pending & GIC_VPE_PEND_TIMER_MSK;
}

static void gic_bind_eic_interrupt(int irq, int set)
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

int gic_get_c0_compare_int(void)
{
	if (!gic_local_irq_is_routable(GIC_LOCAL_INT_TIMER))
		return MIPS_CPU_IRQ_BASE + cp0_compare_irq;
	return irq_create_mapping(gic_irq_domain,
				  GIC_LOCAL_TO_HWIRQ(GIC_LOCAL_INT_TIMER));
}

int gic_get_c0_perfcount_int(void)
{
	if (!gic_local_irq_is_routable(GIC_LOCAL_INT_PERFCTR)) {
		/* Is the erformance counter shared with the timer? */
		if (cp0_perfcount_irq < 0)
			return -1;
		return MIPS_CPU_IRQ_BASE + cp0_perfcount_irq;
	}
	return irq_create_mapping(gic_irq_domain,
				  GIC_LOCAL_TO_HWIRQ(GIC_LOCAL_INT_PERFCTR));
}

static unsigned int gic_get_int(void)
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

	for (i = 0; i < BITS_TO_LONGS(gic_shared_intrs); i++) {
		GICREAD(*pending_abs, pending[i]);
		GICREAD(*intrmask_abs, intrmask[i]);
		pending_abs++;
		intrmask_abs++;
	}

	bitmap_and(pending, pending, intrmask, gic_shared_intrs);
	bitmap_and(pending, pending, pcpu_mask, gic_shared_intrs);

	return find_first_bit(pending, gic_shared_intrs);
}

static void gic_mask_irq(struct irq_data *d)
{
	GIC_CLR_INTR_MASK(GIC_HWIRQ_TO_SHARED(d->hwirq));
}

static void gic_unmask_irq(struct irq_data *d)
{
	GIC_SET_INTR_MASK(GIC_HWIRQ_TO_SHARED(d->hwirq));
}

static void gic_ack_irq(struct irq_data *d)
{
	unsigned int irq = GIC_HWIRQ_TO_SHARED(d->hwirq);

	GICWRITE(GIC_REG(SHARED, GIC_SH_WEDGE), irq);
}

static int gic_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int irq = GIC_HWIRQ_TO_SHARED(d->hwirq);
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
		__irq_set_chip_handler_name_locked(d->irq,
						   &gic_edge_irq_controller,
						   handle_edge_irq, NULL);
	} else {
		__irq_set_chip_handler_name_locked(d->irq,
						   &gic_level_irq_controller,
						   handle_level_irq, NULL);
	}
	spin_unlock_irqrestore(&gic_lock, flags);

	return 0;
}

#ifdef CONFIG_SMP
static int gic_set_affinity(struct irq_data *d, const struct cpumask *cpumask,
			    bool force)
{
	unsigned int irq = GIC_HWIRQ_TO_SHARED(d->hwirq);
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

static struct irq_chip gic_level_irq_controller = {
	.name			=	"MIPS GIC",
	.irq_mask		=	gic_mask_irq,
	.irq_unmask		=	gic_unmask_irq,
	.irq_set_type		=	gic_set_type,
#ifdef CONFIG_SMP
	.irq_set_affinity	=	gic_set_affinity,
#endif
};

static struct irq_chip gic_edge_irq_controller = {
	.name			=	"MIPS GIC",
	.irq_ack		=	gic_ack_irq,
	.irq_mask		=	gic_mask_irq,
	.irq_unmask		=	gic_unmask_irq,
	.irq_set_type		=	gic_set_type,
#ifdef CONFIG_SMP
	.irq_set_affinity	=	gic_set_affinity,
#endif
};

static unsigned int gic_get_local_int(void)
{
	unsigned long pending, masked;

	GICREAD(GIC_REG(VPE_LOCAL, GIC_VPE_PEND), pending);
	GICREAD(GIC_REG(VPE_LOCAL, GIC_VPE_MASK), masked);

	bitmap_and(&pending, &pending, &masked, GIC_NUM_LOCAL_INTRS);

	return find_first_bit(&pending, GIC_NUM_LOCAL_INTRS);
}

static void gic_mask_local_irq(struct irq_data *d)
{
	int intr = GIC_HWIRQ_TO_LOCAL(d->hwirq);

	GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_RMASK), 1 << intr);
}

static void gic_unmask_local_irq(struct irq_data *d)
{
	int intr = GIC_HWIRQ_TO_LOCAL(d->hwirq);

	GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_SMASK), 1 << intr);
}

static struct irq_chip gic_local_irq_controller = {
	.name			=	"MIPS GIC Local",
	.irq_mask		=	gic_mask_local_irq,
	.irq_unmask		=	gic_unmask_local_irq,
};

static void gic_mask_local_irq_all_vpes(struct irq_data *d)
{
	int intr = GIC_HWIRQ_TO_LOCAL(d->hwirq);
	int i;
	unsigned long flags;

	spin_lock_irqsave(&gic_lock, flags);
	for (i = 0; i < gic_vpes; i++) {
		GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_OTHER_ADDR), i);
		GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_RMASK), 1 << intr);
	}
	spin_unlock_irqrestore(&gic_lock, flags);
}

static void gic_unmask_local_irq_all_vpes(struct irq_data *d)
{
	int intr = GIC_HWIRQ_TO_LOCAL(d->hwirq);
	int i;
	unsigned long flags;

	spin_lock_irqsave(&gic_lock, flags);
	for (i = 0; i < gic_vpes; i++) {
		GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_OTHER_ADDR), i);
		GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_SMASK), 1 << intr);
	}
	spin_unlock_irqrestore(&gic_lock, flags);
}

static struct irq_chip gic_all_vpes_local_irq_controller = {
	.name			=	"MIPS GIC Local",
	.irq_mask		=	gic_mask_local_irq_all_vpes,
	.irq_unmask		=	gic_unmask_local_irq_all_vpes,
};

static void __gic_irq_dispatch(void)
{
	unsigned int intr, virq;

	while ((intr = gic_get_local_int()) != GIC_NUM_LOCAL_INTRS) {
		virq = irq_linear_revmap(gic_irq_domain,
					 GIC_LOCAL_TO_HWIRQ(intr));
		do_IRQ(virq);
	}

	while ((intr = gic_get_int()) != gic_shared_intrs) {
		virq = irq_linear_revmap(gic_irq_domain,
					 GIC_SHARED_TO_HWIRQ(intr));
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
	int virq = irq_create_mapping(gic_irq_domain,
				      GIC_SHARED_TO_HWIRQ(intr));
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
	gic_resched_int_base = gic_shared_intrs - nr_cpu_ids;
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

static void __init gic_basic_init(void)
{
	unsigned int i;

	board_bind_eic_interrupt = &gic_bind_eic_interrupt;

	/* Setup defaults */
	for (i = 0; i < gic_shared_intrs; i++) {
		GIC_SET_POLARITY(i, GIC_POL_POS);
		GIC_SET_TRIGGER(i, GIC_TRIG_LEVEL);
		GIC_CLR_INTR_MASK(i);
	}

	for (i = 0; i < gic_vpes; i++) {
		unsigned int j;

		GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_OTHER_ADDR), i);
		for (j = 0; j < GIC_NUM_LOCAL_INTRS; j++) {
			if (!gic_local_irq_is_routable(j))
				continue;
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_RMASK), 1 << j);
		}
	}
}

static int gic_local_irq_domain_map(struct irq_domain *d, unsigned int virq,
				    irq_hw_number_t hw)
{
	int intr = GIC_HWIRQ_TO_LOCAL(hw);
	int ret = 0;
	int i;
	unsigned long flags;

	if (!gic_local_irq_is_routable(intr))
		return -EPERM;

	/*
	 * HACK: These are all really percpu interrupts, but the rest
	 * of the MIPS kernel code does not use the percpu IRQ API for
	 * the CP0 timer and performance counter interrupts.
	 */
	if (intr != GIC_LOCAL_INT_TIMER && intr != GIC_LOCAL_INT_PERFCTR) {
		irq_set_chip_and_handler(virq,
					 &gic_local_irq_controller,
					 handle_percpu_devid_irq);
		irq_set_percpu_devid(virq);
	} else {
		irq_set_chip_and_handler(virq,
					 &gic_all_vpes_local_irq_controller,
					 handle_percpu_irq);
	}

	spin_lock_irqsave(&gic_lock, flags);
	for (i = 0; i < gic_vpes; i++) {
		u32 val = GIC_MAP_TO_PIN_MSK | gic_cpu_pin;

		GICWRITE(GIC_REG(VPE_LOCAL, GIC_VPE_OTHER_ADDR), i);

		switch (intr) {
		case GIC_LOCAL_INT_WD:
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_WD_MAP), val);
			break;
		case GIC_LOCAL_INT_COMPARE:
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_COMPARE_MAP), val);
			break;
		case GIC_LOCAL_INT_TIMER:
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_TIMER_MAP), val);
			break;
		case GIC_LOCAL_INT_PERFCTR:
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_PERFCTR_MAP), val);
			break;
		case GIC_LOCAL_INT_SWINT0:
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_SWINT0_MAP), val);
			break;
		case GIC_LOCAL_INT_SWINT1:
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_SWINT1_MAP), val);
			break;
		case GIC_LOCAL_INT_FDC:
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_FDC_MAP), val);
			break;
		default:
			pr_err("Invalid local IRQ %d\n", intr);
			ret = -EINVAL;
			break;
		}
	}
	spin_unlock_irqrestore(&gic_lock, flags);

	return ret;
}

static int gic_shared_irq_domain_map(struct irq_domain *d, unsigned int virq,
				     irq_hw_number_t hw)
{
	int intr = GIC_HWIRQ_TO_SHARED(hw);
	unsigned long flags;

	irq_set_chip_and_handler(virq, &gic_level_irq_controller,
				 handle_level_irq);

	spin_lock_irqsave(&gic_lock, flags);
	GICWRITE(GIC_REG_ADDR(SHARED, GIC_SH_MAP_TO_PIN(intr)),
		 GIC_MAP_TO_PIN_MSK | gic_cpu_pin);
	/* Map to VPE 0 by default */
	GIC_SH_MAP_TO_VPE_SMASK(intr, 0);
	set_bit(intr, pcpu_masks[0].pcpu_mask);
	spin_unlock_irqrestore(&gic_lock, flags);

	return 0;
}

static int gic_irq_domain_map(struct irq_domain *d, unsigned int virq,
			      irq_hw_number_t hw)
{
	if (GIC_HWIRQ_TO_LOCAL(hw) < GIC_NUM_LOCAL_INTRS)
		return gic_local_irq_domain_map(d, virq, hw);
	return gic_shared_irq_domain_map(d, virq, hw);
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

	_gic_base = (unsigned long) ioremap_nocache(gic_base_addr,
						    gic_addrspace_size);

	GICREAD(GIC_REG(SHARED, GIC_SH_CONFIG), gicconfig);
	gic_shared_intrs = (gicconfig & GIC_SH_CONFIG_NUMINTRS_MSK) >>
		   GIC_SH_CONFIG_NUMINTRS_SHF;
	gic_shared_intrs = ((gic_shared_intrs + 1) * 8);

	gic_vpes = (gicconfig & GIC_SH_CONFIG_NUMVPES_MSK) >>
		  GIC_SH_CONFIG_NUMVPES_SHF;
	gic_vpes = gic_vpes + 1;

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

	gic_irq_domain = irq_domain_add_simple(NULL, GIC_NUM_LOCAL_INTRS +
					       gic_shared_intrs, irqbase,
					       &gic_irq_domain_ops, NULL);
	if (!gic_irq_domain)
		panic("Failed to add GIC IRQ domain");

	gic_basic_init();

	gic_ipi_init();
}
