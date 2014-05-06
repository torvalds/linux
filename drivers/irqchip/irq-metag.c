/*
 * Meta internal (HWSTATMETA) interrupt code.
 *
 * Copyright (C) 2011-2012 Imagination Technologies Ltd.
 *
 * This code is based on the code in SoC/common/irq.c and SoC/comet/irq.c
 * The code base could be generalised/merged as a lot of the functionality is
 * similar. Until this is done, we try to keep the code simple here.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqdomain.h>

#include <asm/irq.h>
#include <asm/hwthread.h>

#define PERF0VECINT		0x04820580
#define PERF1VECINT		0x04820588
#define PERF0TRIG_OFFSET	16
#define PERF1TRIG_OFFSET	17

/**
 * struct metag_internal_irq_priv - private meta internal interrupt data
 * @domain:		IRQ domain for all internal Meta IRQs (HWSTATMETA)
 * @unmasked:		Record of unmasked IRQs
 */
struct metag_internal_irq_priv {
	struct irq_domain	*domain;

	unsigned long		unmasked;
};

/* Private data for the one and only internal interrupt controller */
static struct metag_internal_irq_priv metag_internal_irq_priv;

static unsigned int metag_internal_irq_startup(struct irq_data *data);
static void metag_internal_irq_shutdown(struct irq_data *data);
static void metag_internal_irq_ack(struct irq_data *data);
static void metag_internal_irq_mask(struct irq_data *data);
static void metag_internal_irq_unmask(struct irq_data *data);
#ifdef CONFIG_SMP
static int metag_internal_irq_set_affinity(struct irq_data *data,
			const struct cpumask *cpumask, bool force);
#endif

static struct irq_chip internal_irq_edge_chip = {
	.name = "HWSTATMETA-IRQ",
	.irq_startup = metag_internal_irq_startup,
	.irq_shutdown = metag_internal_irq_shutdown,
	.irq_ack = metag_internal_irq_ack,
	.irq_mask = metag_internal_irq_mask,
	.irq_unmask = metag_internal_irq_unmask,
#ifdef CONFIG_SMP
	.irq_set_affinity = metag_internal_irq_set_affinity,
#endif
};

/*
 *	metag_hwvec_addr - get the address of *VECINT regs of irq
 *
 *	This function is a table of supported triggers on HWSTATMETA
 *	Could do with a structure, but better keep it simple. Changes
 *	in this code should be rare.
 */
static inline void __iomem *metag_hwvec_addr(irq_hw_number_t hw)
{
	void __iomem *addr;

	switch (hw) {
	case PERF0TRIG_OFFSET:
		addr = (void __iomem *)PERF0VECINT;
		break;
	case PERF1TRIG_OFFSET:
		addr = (void __iomem *)PERF1VECINT;
		break;
	default:
		addr = NULL;
		break;
	}
	return addr;
}

/*
 *	metag_internal_startup - setup an internal irq
 *	@irq:	the irq to startup
 *
 *	Multiplex interrupts for @irq onto TR1. Clear any pending
 *	interrupts.
 */
static unsigned int metag_internal_irq_startup(struct irq_data *data)
{
	/* Clear (toggle) the bit in HWSTATMETA for our interrupt. */
	metag_internal_irq_ack(data);

	/* Enable the interrupt by unmasking it */
	metag_internal_irq_unmask(data);

	return 0;
}

/*
 *	metag_internal_irq_shutdown - turn off the irq
 *	@irq:	the irq number to turn off
 *
 *	Mask @irq and clear any pending interrupts.
 *	Stop muxing @irq onto TR1.
 */
static void metag_internal_irq_shutdown(struct irq_data *data)
{
	/* Disable the IRQ at the core by masking it. */
	metag_internal_irq_mask(data);

	/* Clear (toggle) the bit in HWSTATMETA for our interrupt. */
	metag_internal_irq_ack(data);
}

/*
 *	metag_internal_irq_ack - acknowledge irq
 *	@irq:	the irq to ack
 */
static void metag_internal_irq_ack(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	unsigned int bit = 1 << hw;

	if (metag_in32(HWSTATMETA) & bit)
		metag_out32(bit, HWSTATMETA);
}

/**
 * metag_internal_irq_mask() - mask an internal irq by unvectoring
 * @data:	data for the internal irq to mask
 *
 * HWSTATMETA has no mask register. Instead the IRQ is unvectored from the core
 * and retriggered if necessary later.
 */
static void metag_internal_irq_mask(struct irq_data *data)
{
	struct metag_internal_irq_priv *priv = &metag_internal_irq_priv;
	irq_hw_number_t hw = data->hwirq;
	void __iomem *vec_addr = metag_hwvec_addr(hw);

	clear_bit(hw, &priv->unmasked);

	/* there is no interrupt mask, so unvector the interrupt */
	metag_out32(0, vec_addr);
}

/**
 * meta_intc_unmask_edge_irq_nomask() - unmask an edge irq by revectoring
 * @data:	data for the internal irq to unmask
 *
 * HWSTATMETA has no mask register. Instead the IRQ is revectored back to the
 * core and retriggered if necessary.
 */
static void metag_internal_irq_unmask(struct irq_data *data)
{
	struct metag_internal_irq_priv *priv = &metag_internal_irq_priv;
	irq_hw_number_t hw = data->hwirq;
	unsigned int bit = 1 << hw;
	void __iomem *vec_addr = metag_hwvec_addr(hw);
	unsigned int thread = hard_processor_id();

	set_bit(hw, &priv->unmasked);

	/* there is no interrupt mask, so revector the interrupt */
	metag_out32(TBI_TRIG_VEC(TBID_SIGNUM_TR1(thread)), vec_addr);

	/*
	 * Re-trigger interrupt
	 *
	 * Writing a 1 toggles, and a 0->1 transition triggers. We only
	 * retrigger if the status bit is already set, which means we
	 * need to clear it first. Retriggering is fundamentally racy
	 * because if the interrupt fires again after we clear it we
	 * could end up clearing it again and the interrupt handler
	 * thinking it hasn't fired. Therefore we need to keep trying to
	 * retrigger until the bit is set.
	 */
	if (metag_in32(HWSTATMETA) & bit) {
		metag_out32(bit, HWSTATMETA);
		while (!(metag_in32(HWSTATMETA) & bit))
			metag_out32(bit, HWSTATMETA);
	}
}

#ifdef CONFIG_SMP
/*
 *	metag_internal_irq_set_affinity - set the affinity for an interrupt
 */
static int metag_internal_irq_set_affinity(struct irq_data *data,
			const struct cpumask *cpumask, bool force)
{
	unsigned int cpu, thread;
	irq_hw_number_t hw = data->hwirq;
	/*
	 * Wire up this interrupt from *VECINT to the Meta core.
	 *
	 * Note that we can't wire up *VECINT to interrupt more than
	 * one cpu (the interrupt code doesn't support it), so we just
	 * pick the first cpu we find in 'cpumask'.
	 */
	cpu = cpumask_any_and(cpumask, cpu_online_mask);
	thread = cpu_2_hwthread_id[cpu];

	metag_out32(TBI_TRIG_VEC(TBID_SIGNUM_TR1(thread)),
		    metag_hwvec_addr(hw));

	return 0;
}
#endif

/*
 *	metag_internal_irq_demux - irq de-multiplexer
 *	@irq:	the interrupt number
 *	@desc:	the interrupt description structure for this irq
 *
 *	The cpu receives an interrupt on TR1 when an interrupt has
 *	occurred. It is this function's job to demux this irq and
 *	figure out exactly which trigger needs servicing.
 */
static void metag_internal_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	struct metag_internal_irq_priv *priv = irq_desc_get_handler_data(desc);
	irq_hw_number_t hw;
	unsigned int irq_no;
	u32 status;

recalculate:
	status = metag_in32(HWSTATMETA) & priv->unmasked;

	for (hw = 0; status != 0; status >>= 1, ++hw) {
		if (status & 0x1) {
			/*
			 * Map the hardware IRQ number to a virtual Linux IRQ
			 * number.
			 */
			irq_no = irq_linear_revmap(priv->domain, hw);

			/*
			 * Only fire off interrupts that are
			 * registered to be handled by the kernel.
			 * Other interrupts are probably being
			 * handled by other Meta hardware threads.
			 */
			generic_handle_irq(irq_no);

			/*
			 * The handler may have re-enabled interrupts
			 * which could have caused a nested invocation
			 * of this code and make the copy of the
			 * status register we are using invalid.
			 */
			goto recalculate;
		}
	}
}

/**
 * internal_irq_map() - Map an internal meta IRQ to a virtual IRQ number.
 * @hw:		Number of the internal IRQ. Must be in range.
 *
 * Returns:	The virtual IRQ number of the Meta internal IRQ specified by
 *		@hw.
 */
int internal_irq_map(unsigned int hw)
{
	struct metag_internal_irq_priv *priv = &metag_internal_irq_priv;
	if (!priv->domain)
		return -ENODEV;
	return irq_create_mapping(priv->domain, hw);
}

/**
 *	metag_internal_irq_init_cpu - regsister with the Meta cpu
 *	@cpu:	the CPU to register on
 *
 *	Configure @cpu's TR1 irq so that we can demux irqs.
 */
static void metag_internal_irq_init_cpu(struct metag_internal_irq_priv *priv,
					int cpu)
{
	unsigned int thread = cpu_2_hwthread_id[cpu];
	unsigned int signum = TBID_SIGNUM_TR1(thread);
	int irq = tbisig_map(signum);

	/* Register the multiplexed IRQ handler */
	irq_set_handler_data(irq, priv);
	irq_set_chained_handler(irq, metag_internal_irq_demux);
	irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);
}

/**
 * metag_internal_intc_map() - map an internal irq
 * @d:		irq domain of internal trigger block
 * @irq:	virtual irq number
 * @hw:		hardware irq number within internal trigger block
 *
 * This sets up a virtual irq for a specified hardware interrupt. The irq chip
 * and handler is configured.
 */
static int metag_internal_intc_map(struct irq_domain *d, unsigned int irq,
				   irq_hw_number_t hw)
{
	/* only register interrupt if it is mapped */
	if (!metag_hwvec_addr(hw))
		return -EINVAL;

	irq_set_chip_and_handler(irq, &internal_irq_edge_chip,
				 handle_edge_irq);
	return 0;
}

static const struct irq_domain_ops metag_internal_intc_domain_ops = {
	.map	= metag_internal_intc_map,
};

/**
 *	metag_internal_irq_register - register internal IRQs
 *
 *	Register the irq chip and handler function for all internal IRQs
 */
int __init init_internal_IRQ(void)
{
	struct metag_internal_irq_priv *priv = &metag_internal_irq_priv;
	unsigned int cpu;

	/* Set up an IRQ domain */
	priv->domain = irq_domain_add_linear(NULL, 32,
					     &metag_internal_intc_domain_ops,
					     priv);
	if (unlikely(!priv->domain)) {
		pr_err("meta-internal-intc: cannot add IRQ domain\n");
		return -ENOMEM;
	}

	/* Setup TR1 for all cpus. */
	for_each_possible_cpu(cpu)
		metag_internal_irq_init_cpu(priv, cpu);

	return 0;
};
