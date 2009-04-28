#undef DEBUG

#include <linux/bitmap.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/gic.h>
#include <asm/gcmpregs.h>
#include <asm/mips-boards/maltaint.h>
#include <asm/irq.h>
#include <linux/hardirq.h>
#include <asm-generic/bitops/find.h>


static unsigned long _gic_base;
static unsigned int _irqbase, _mapsize, numvpes, numintrs;
static struct gic_intr_map *_intrmap;

static struct gic_pcpu_mask pcpu_masks[NR_CPUS];
static struct gic_pending_regs pending_regs[NR_CPUS];
static struct gic_intrmask_regs intrmask_regs[NR_CPUS];

#define gic_wedgeb2bok 0	/*
				 * Can GIC handle b2b writes to wedge register?
				 */
#if gic_wedgeb2bok == 0
static DEFINE_SPINLOCK(gic_wedgeb2b_lock);
#endif

void gic_send_ipi(unsigned int intr)
{
#if gic_wedgeb2bok == 0
	unsigned long flags;
#endif
	pr_debug("CPU%d: %s status %08x\n", smp_processor_id(), __func__,
		 read_c0_status());
	if (!gic_wedgeb2bok)
		spin_lock_irqsave(&gic_wedgeb2b_lock, flags);
	GICWRITE(GIC_REG(SHARED, GIC_SH_WEDGE), 0x80000000 | intr);
	if (!gic_wedgeb2bok) {
		(void) GIC_REG(SHARED, GIC_SH_CONFIG);
		spin_unlock_irqrestore(&gic_wedgeb2b_lock, flags);
	}
}

/* This is Malta specific and needs to be exported */
static void vpe_local_setup(unsigned int numvpes)
{
	int i;
	unsigned long timer_interrupt = 5, perf_interrupt = 5;
	unsigned int vpe_ctl;

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
				 GIC_MAP_TO_PIN_MSK | timer_interrupt);

		if (vpe_ctl & GIC_VPE_CTL_PERFCNT_RTBL_MSK)
			GICWRITE(GIC_REG(VPE_OTHER, GIC_VPE_PERFCTR_MAP),
				 GIC_MAP_TO_PIN_MSK | perf_interrupt);
	}
}

unsigned int gic_get_int(void)
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

	i = find_first_bit(pending, GIC_NUM_INTRS);

	pr_debug("CPU%d: %s pend=%d\n", smp_processor_id(), __func__, i);

	return i;
}

static unsigned int gic_irq_startup(unsigned int irq)
{
	pr_debug("CPU%d: %s: irq%d\n", smp_processor_id(), __func__, irq);
	irq -= _irqbase;
	/* FIXME: this is wrong for !GICISWORDLITTLEENDIAN */
	GICWRITE(GIC_REG_ADDR(SHARED, (GIC_SH_SMASK_31_0_OFS + (irq / 32))),
		 1 << (irq % 32));
	return 0;
}

static void gic_irq_ack(unsigned int irq)
{
#if gic_wedgeb2bok == 0
	unsigned long flags;
#endif
	pr_debug("CPU%d: %s: irq%d\n", smp_processor_id(), __func__, irq);
	irq -= _irqbase;
	GICWRITE(GIC_REG_ADDR(SHARED, (GIC_SH_RMASK_31_0_OFS + (irq / 32))),
		 1 << (irq % 32));

	if (_intrmap[irq].trigtype == GIC_TRIG_EDGE) {
		if (!gic_wedgeb2bok)
			spin_lock_irqsave(&gic_wedgeb2b_lock, flags);
		GICWRITE(GIC_REG(SHARED, GIC_SH_WEDGE), irq);
		if (!gic_wedgeb2bok) {
			(void) GIC_REG(SHARED, GIC_SH_CONFIG);
			spin_unlock_irqrestore(&gic_wedgeb2b_lock, flags);
		}
	}
}

static void gic_mask_irq(unsigned int irq)
{
	pr_debug("CPU%d: %s: irq%d\n", smp_processor_id(), __func__, irq);
	irq -= _irqbase;
	/* FIXME: this is wrong for !GICISWORDLITTLEENDIAN */
	GICWRITE(GIC_REG_ADDR(SHARED, (GIC_SH_RMASK_31_0_OFS + (irq / 32))),
		 1 << (irq % 32));
}

static void gic_unmask_irq(unsigned int irq)
{
	pr_debug("CPU%d: %s: irq%d\n", smp_processor_id(), __func__, irq);
	irq -= _irqbase;
	/* FIXME: this is wrong for !GICISWORDLITTLEENDIAN */
	GICWRITE(GIC_REG_ADDR(SHARED, (GIC_SH_SMASK_31_0_OFS + (irq / 32))),
		 1 << (irq % 32));
}

#ifdef CONFIG_SMP

static DEFINE_SPINLOCK(gic_lock);

static int gic_set_affinity(unsigned int irq, const struct cpumask *cpumask)
{
	cpumask_t	tmp = CPU_MASK_NONE;
	unsigned long	flags;
	int		i;

	pr_debug(KERN_DEBUG "%s called\n", __func__);
	irq -= _irqbase;

	cpumask_and(&tmp, cpumask, cpu_online_mask);
	if (cpus_empty(tmp))
		return -1;

	/* Assumption : cpumask refers to a single CPU */
	spin_lock_irqsave(&gic_lock, flags);
	for (;;) {
		/* Re-route this IRQ */
		GIC_SH_MAP_TO_VPE_SMASK(irq, first_cpu(tmp));

		/*
		 * FIXME: assumption that _intrmap is ordered and has no holes
		 */

		/* Update the intr_map */
		_intrmap[irq].cpunum = first_cpu(tmp);

		/* Update the pcpu_masks */
		for (i = 0; i < NR_CPUS; i++)
			clear_bit(irq, pcpu_masks[i].pcpu_mask);
		set_bit(irq, pcpu_masks[first_cpu(tmp)].pcpu_mask);

	}
	cpumask_copy(irq_desc[irq].affinity, cpumask);
	spin_unlock_irqrestore(&gic_lock, flags);

	return 0;
}
#endif

static struct irq_chip gic_irq_controller = {
	.name		=	"MIPS GIC",
	.startup	=	gic_irq_startup,
	.ack		=	gic_irq_ack,
	.mask		=	gic_mask_irq,
	.mask_ack	=	gic_mask_irq,
	.unmask		=	gic_unmask_irq,
	.eoi		=	gic_unmask_irq,
#ifdef CONFIG_SMP
	.set_affinity	=	gic_set_affinity,
#endif
};

static void __init setup_intr(unsigned int intr, unsigned int cpu,
	unsigned int pin, unsigned int polarity, unsigned int trigtype)
{
	/* Setup Intr to Pin mapping */
	if (pin & GIC_MAP_TO_NMI_MSK) {
		GICWRITE(GIC_REG_ADDR(SHARED, GIC_SH_MAP_TO_PIN(intr)), pin);
		/* FIXME: hack to route NMI to all cpu's */
		for (cpu = 0; cpu < NR_CPUS; cpu += 32) {
			GICWRITE(GIC_REG_ADDR(SHARED,
					  GIC_SH_MAP_TO_VPE_REG_OFF(intr, cpu)),
				 0xffffffff);
		}
	} else {
		GICWRITE(GIC_REG_ADDR(SHARED, GIC_SH_MAP_TO_PIN(intr)),
			 GIC_MAP_TO_PIN_MSK | pin);
		/* Setup Intr to CPU mapping */
		GIC_SH_MAP_TO_VPE_SMASK(intr, cpu);
	}

	/* Setup Intr Polarity */
	GIC_SET_POLARITY(intr, polarity);

	/* Setup Intr Trigger Type */
	GIC_SET_TRIGGER(intr, trigtype);

	/* Init Intr Masks */
	GIC_SET_INTR_MASK(intr, 0);
}

static void __init gic_basic_init(void)
{
	unsigned int i, cpu;

	/* Setup defaults */
	for (i = 0; i < GIC_NUM_INTRS; i++) {
		GIC_SET_POLARITY(i, GIC_POL_POS);
		GIC_SET_TRIGGER(i, GIC_TRIG_LEVEL);
		GIC_SET_INTR_MASK(i, 0);
	}

	/* Setup specifics */
	for (i = 0; i < _mapsize; i++) {
		cpu = _intrmap[i].cpunum;
		if (cpu == X)
			continue;

		setup_intr(_intrmap[i].intrnum,
				_intrmap[i].cpunum,
				_intrmap[i].pin,
				_intrmap[i].polarity,
				_intrmap[i].trigtype);
		/* Initialise per-cpu Interrupt software masks */
		if (_intrmap[i].ipiflag)
			set_bit(_intrmap[i].intrnum, pcpu_masks[cpu].pcpu_mask);
	}

	vpe_local_setup(numvpes);

	for (i = _irqbase; i < (_irqbase + numintrs); i++)
		set_irq_chip(i, &gic_irq_controller);
}

void __init gic_init(unsigned long gic_base_addr,
		     unsigned long gic_addrspace_size,
		     struct gic_intr_map *intr_map, unsigned int intr_map_size,
		     unsigned int irqbase)
{
	unsigned int gicconfig;

	_gic_base = (unsigned long) ioremap_nocache(gic_base_addr,
						    gic_addrspace_size);
	_irqbase = irqbase;
	_intrmap = intr_map;
	_mapsize = intr_map_size;

	GICREAD(GIC_REG(SHARED, GIC_SH_CONFIG), gicconfig);
	numintrs = (gicconfig & GIC_SH_CONFIG_NUMINTRS_MSK) >>
		   GIC_SH_CONFIG_NUMINTRS_SHF;
	numintrs = ((numintrs + 1) * 8);

	numvpes = (gicconfig & GIC_SH_CONFIG_NUMVPES_MSK) >>
		  GIC_SH_CONFIG_NUMVPES_SHF;

	pr_debug("%s called\n", __func__);

	gic_basic_init();
}
