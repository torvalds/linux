/*
 * Meta External interrupt code.
 *
 * Copyright (C) 2005-2012 Imagination Technologies Ltd.
 *
 * External interrupts on Meta are configured at two-levels, in the CPU core and
 * in the external trigger block. Interrupts from SoC peripherals are
 * multiplexed onto a single Meta CPU "trigger" - traditionally it has always
 * been trigger 2 (TR2). For info on how de-multiplexing happens check out
 * meta_intc_irq_demux().
 */

#include <linux/interrupt.h>
#include <linux/irqchip/metag-ext.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>

#include <asm/irq.h>
#include <asm/hwthread.h>

#define HWSTAT_STRIDE 8
#define HWVEC_BLK_STRIDE 0x1000

/**
 * struct meta_intc_priv - private meta external interrupt data
 * @nr_banks:		Number of interrupt banks
 * @domain:		IRQ domain for all banks of external IRQs
 * @unmasked:		Record of unmasked IRQs
 * @levels_altered:	Record of altered level bits
 */
struct meta_intc_priv {
	unsigned int		nr_banks;
	struct irq_domain	*domain;

	unsigned long		unmasked[4];

#ifdef CONFIG_METAG_SUSPEND_MEM
	unsigned long		levels_altered[4];
#endif
};

/* Private data for the one and only external interrupt controller */
static struct meta_intc_priv meta_intc_priv;

/**
 * meta_intc_offset() - Get the offset into the bank of a hardware IRQ number
 * @hw:		Hardware IRQ number (within external trigger block)
 *
 * Returns:	Bit offset into the IRQ's bank registers
 */
static unsigned int meta_intc_offset(irq_hw_number_t hw)
{
	return hw & 0x1f;
}

/**
 * meta_intc_bank() - Get the bank number of a hardware IRQ number
 * @hw:		Hardware IRQ number (within external trigger block)
 *
 * Returns:	Bank number indicating which register the IRQ's bits are
 */
static unsigned int meta_intc_bank(irq_hw_number_t hw)
{
	return hw >> 5;
}

/**
 * meta_intc_stat_addr() - Get the address of a HWSTATEXT register
 * @hw:		Hardware IRQ number (within external trigger block)
 *
 * Returns:	Address of a HWSTATEXT register containing the status bit for
 *		the specified hardware IRQ number
 */
static void __iomem *meta_intc_stat_addr(irq_hw_number_t hw)
{
	return (void __iomem *)(HWSTATEXT +
				HWSTAT_STRIDE * meta_intc_bank(hw));
}

/**
 * meta_intc_level_addr() - Get the address of a HWLEVELEXT register
 * @hw:		Hardware IRQ number (within external trigger block)
 *
 * Returns:	Address of a HWLEVELEXT register containing the sense bit for
 *		the specified hardware IRQ number
 */
static void __iomem *meta_intc_level_addr(irq_hw_number_t hw)
{
	return (void __iomem *)(HWLEVELEXT +
				HWSTAT_STRIDE * meta_intc_bank(hw));
}

/**
 * meta_intc_mask_addr() - Get the address of a HWMASKEXT register
 * @hw:		Hardware IRQ number (within external trigger block)
 *
 * Returns:	Address of a HWMASKEXT register containing the mask bit for the
 *		specified hardware IRQ number
 */
static void __iomem *meta_intc_mask_addr(irq_hw_number_t hw)
{
	return (void __iomem *)(HWMASKEXT +
				HWSTAT_STRIDE * meta_intc_bank(hw));
}

/**
 * meta_intc_vec_addr() - Get the vector address of a hardware interrupt
 * @hw:		Hardware IRQ number (within external trigger block)
 *
 * Returns:	Address of a HWVECEXT register controlling the core trigger to
 *		vector the IRQ onto
 */
static inline void __iomem *meta_intc_vec_addr(irq_hw_number_t hw)
{
	return (void __iomem *)(HWVEC0EXT +
				HWVEC_BLK_STRIDE * meta_intc_bank(hw) +
				HWVECnEXT_STRIDE * meta_intc_offset(hw));
}

/**
 * meta_intc_startup_irq() - set up an external irq
 * @data:	data for the external irq to start up
 *
 * Multiplex interrupts for irq onto TR2. Clear any pending interrupts and
 * unmask irq, both using the appropriate callbacks.
 */
static unsigned int meta_intc_startup_irq(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	void __iomem *vec_addr = meta_intc_vec_addr(hw);
	int thread = hard_processor_id();

	/* Perform any necessary acking. */
	if (data->chip->irq_ack)
		data->chip->irq_ack(data);

	/* Wire up this interrupt to the core with HWVECxEXT. */
	metag_out32(TBI_TRIG_VEC(TBID_SIGNUM_TR2(thread)), vec_addr);

	/* Perform any necessary unmasking. */
	data->chip->irq_unmask(data);

	return 0;
}

/**
 * meta_intc_shutdown_irq() - turn off an external irq
 * @data:	data for the external irq to turn off
 *
 * Mask irq using the appropriate callback and stop muxing it onto TR2.
 */
static void meta_intc_shutdown_irq(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	void __iomem *vec_addr = meta_intc_vec_addr(hw);

	/* Mask the IRQ */
	data->chip->irq_mask(data);

	/*
	 * Disable the IRQ at the core by removing the interrupt from
	 * the HW vector mapping.
	 */
	metag_out32(0, vec_addr);
}

/**
 * meta_intc_ack_irq() - acknowledge an external irq
 * @data:	data for the external irq to ack
 *
 * Clear down an edge interrupt in the status register.
 */
static void meta_intc_ack_irq(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	unsigned int bit = 1 << meta_intc_offset(hw);
	void __iomem *stat_addr = meta_intc_stat_addr(hw);

	/* Ack the int, if it is still 'on'.
	 * NOTE - this only works for edge triggered interrupts.
	 */
	if (metag_in32(stat_addr) & bit)
		metag_out32(bit, stat_addr);
}

/**
 * record_irq_is_masked() - record the IRQ masked so it doesn't get handled
 * @data:	data for the external irq to record
 *
 * This should get called whenever an external IRQ is masked (by whichever
 * callback is used). It records the IRQ masked so that it doesn't get handled
 * if it still shows up in the status register.
 */
static void record_irq_is_masked(struct irq_data *data)
{
	struct meta_intc_priv *priv = &meta_intc_priv;
	irq_hw_number_t hw = data->hwirq;

	clear_bit(meta_intc_offset(hw), &priv->unmasked[meta_intc_bank(hw)]);
}

/**
 * record_irq_is_unmasked() - record the IRQ unmasked so it can be handled
 * @data:	data for the external irq to record
 *
 * This should get called whenever an external IRQ is unmasked (by whichever
 * callback is used). It records the IRQ unmasked so that it gets handled if it
 * shows up in the status register.
 */
static void record_irq_is_unmasked(struct irq_data *data)
{
	struct meta_intc_priv *priv = &meta_intc_priv;
	irq_hw_number_t hw = data->hwirq;

	set_bit(meta_intc_offset(hw), &priv->unmasked[meta_intc_bank(hw)]);
}

/*
 * For use by wrapper IRQ drivers
 */

/**
 * meta_intc_mask_irq_simple() - minimal mask used by wrapper IRQ drivers
 * @data:	data for the external irq being masked
 *
 * This should be called by any wrapper IRQ driver mask functions. it doesn't do
 * any masking but records the IRQ as masked so that the core code knows the
 * mask has taken place. It is the callers responsibility to ensure that the IRQ
 * won't trigger an interrupt to the core.
 */
void meta_intc_mask_irq_simple(struct irq_data *data)
{
	record_irq_is_masked(data);
}

/**
 * meta_intc_unmask_irq_simple() - minimal unmask used by wrapper IRQ drivers
 * @data:	data for the external irq being unmasked
 *
 * This should be called by any wrapper IRQ driver unmask functions. it doesn't
 * do any unmasking but records the IRQ as unmasked so that the core code knows
 * the unmask has taken place. It is the callers responsibility to ensure that
 * the IRQ can now trigger an interrupt to the core.
 */
void meta_intc_unmask_irq_simple(struct irq_data *data)
{
	record_irq_is_unmasked(data);
}


/**
 * meta_intc_mask_irq() - mask an external irq using HWMASKEXT
 * @data:	data for the external irq to mask
 *
 * This is a default implementation of a mask function which makes use of the
 * HWMASKEXT registers available in newer versions.
 *
 * Earlier versions without these registers should use SoC level IRQ masking
 * which call the meta_intc_*_simple() functions above, or if that isn't
 * available should use the fallback meta_intc_*_nomask() functions below.
 */
static void meta_intc_mask_irq(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	unsigned int bit = 1 << meta_intc_offset(hw);
	void __iomem *mask_addr = meta_intc_mask_addr(hw);
	unsigned long flags;

	record_irq_is_masked(data);

	/* update the interrupt mask */
	__global_lock2(flags);
	metag_out32(metag_in32(mask_addr) & ~bit, mask_addr);
	__global_unlock2(flags);
}

/**
 * meta_intc_unmask_irq() - unmask an external irq using HWMASKEXT
 * @data:	data for the external irq to unmask
 *
 * This is a default implementation of an unmask function which makes use of the
 * HWMASKEXT registers available on new versions. It should be paired with
 * meta_intc_mask_irq() above.
 */
static void meta_intc_unmask_irq(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	unsigned int bit = 1 << meta_intc_offset(hw);
	void __iomem *mask_addr = meta_intc_mask_addr(hw);
	unsigned long flags;

	record_irq_is_unmasked(data);

	/* update the interrupt mask */
	__global_lock2(flags);
	metag_out32(metag_in32(mask_addr) | bit, mask_addr);
	__global_unlock2(flags);
}

/**
 * meta_intc_mask_irq_nomask() - mask an external irq by unvectoring
 * @data:	data for the external irq to mask
 *
 * This is the version of the mask function for older versions which don't have
 * HWMASKEXT registers, or a SoC level means of masking IRQs. Instead the IRQ is
 * unvectored from the core and retriggered if necessary later.
 */
static void meta_intc_mask_irq_nomask(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	void __iomem *vec_addr = meta_intc_vec_addr(hw);

	record_irq_is_masked(data);

	/* there is no interrupt mask, so unvector the interrupt */
	metag_out32(0, vec_addr);
}

/**
 * meta_intc_unmask_edge_irq_nomask() - unmask an edge irq by revectoring
 * @data:	data for the external irq to unmask
 *
 * This is the version of the unmask function for older versions which don't
 * have HWMASKEXT registers, or a SoC level means of masking IRQs. Instead the
 * IRQ is revectored back to the core and retriggered if necessary.
 *
 * The retriggering done by this function is specific to edge interrupts.
 */
static void meta_intc_unmask_edge_irq_nomask(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	unsigned int bit = 1 << meta_intc_offset(hw);
	void __iomem *stat_addr = meta_intc_stat_addr(hw);
	void __iomem *vec_addr = meta_intc_vec_addr(hw);
	unsigned int thread = hard_processor_id();

	record_irq_is_unmasked(data);

	/* there is no interrupt mask, so revector the interrupt */
	metag_out32(TBI_TRIG_VEC(TBID_SIGNUM_TR2(thread)), vec_addr);

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
	if (metag_in32(stat_addr) & bit) {
		metag_out32(bit, stat_addr);
		while (!(metag_in32(stat_addr) & bit))
			metag_out32(bit, stat_addr);
	}
}

/**
 * meta_intc_unmask_level_irq_nomask() - unmask a level irq by revectoring
 * @data:	data for the external irq to unmask
 *
 * This is the version of the unmask function for older versions which don't
 * have HWMASKEXT registers, or a SoC level means of masking IRQs. Instead the
 * IRQ is revectored back to the core and retriggered if necessary.
 *
 * The retriggering done by this function is specific to level interrupts.
 */
static void meta_intc_unmask_level_irq_nomask(struct irq_data *data)
{
	irq_hw_number_t hw = data->hwirq;
	unsigned int bit = 1 << meta_intc_offset(hw);
	void __iomem *stat_addr = meta_intc_stat_addr(hw);
	void __iomem *vec_addr = meta_intc_vec_addr(hw);
	unsigned int thread = hard_processor_id();

	record_irq_is_unmasked(data);

	/* there is no interrupt mask, so revector the interrupt */
	metag_out32(TBI_TRIG_VEC(TBID_SIGNUM_TR2(thread)), vec_addr);

	/* Re-trigger interrupt */
	/* Writing a 1 triggers interrupt */
	if (metag_in32(stat_addr) & bit)
		metag_out32(bit, stat_addr);
}

/**
 * meta_intc_irq_set_type() - set the type of an external irq
 * @data:	data for the external irq to set the type of
 * @flow_type:	new irq flow type
 *
 * Set the flow type of an external interrupt. This updates the irq chip and irq
 * handler depending on whether the irq is edge or level sensitive (the polarity
 * is ignored), and also sets up the bit in HWLEVELEXT so the hardware knows
 * when to trigger.
 */
static int meta_intc_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
#ifdef CONFIG_METAG_SUSPEND_MEM
	struct meta_intc_priv *priv = &meta_intc_priv;
#endif
	irq_hw_number_t hw = data->hwirq;
	unsigned int bit = 1 << meta_intc_offset(hw);
	void __iomem *level_addr = meta_intc_level_addr(hw);
	unsigned long flags;
	unsigned int level;

	/* update the chip/handler */
	if (flow_type & IRQ_TYPE_LEVEL_MASK)
		irq_set_chip_handler_name_locked(data, &meta_intc_level_chip,
						 handle_level_irq, NULL);
	else
		irq_set_chip_handler_name_locked(data, &meta_intc_edge_chip,
						 handle_edge_irq, NULL);

	/* and clear/set the bit in HWLEVELEXT */
	__global_lock2(flags);
	level = metag_in32(level_addr);
	if (flow_type & IRQ_TYPE_LEVEL_MASK)
		level |= bit;
	else
		level &= ~bit;
	metag_out32(level, level_addr);
#ifdef CONFIG_METAG_SUSPEND_MEM
	priv->levels_altered[meta_intc_bank(hw)] |= bit;
#endif
	__global_unlock2(flags);

	return 0;
}

/**
 * meta_intc_irq_demux() - external irq de-multiplexer
 * @irq:	the virtual interrupt number
 * @desc:	the interrupt description structure for this irq
 *
 * The cpu receives an interrupt on TR2 when a SoC interrupt has occurred. It is
 * this function's job to demux this irq and figure out exactly which external
 * irq needs servicing.
 *
 * Whilst using TR2 to detect external interrupts is a software convention it is
 * (hopefully) unlikely to change.
 */
static void meta_intc_irq_demux(struct irq_desc *desc)
{
	struct meta_intc_priv *priv = &meta_intc_priv;
	irq_hw_number_t hw;
	unsigned int bank, irq_no, status;
	void __iomem *stat_addr = meta_intc_stat_addr(0);

	/*
	 * Locate which interrupt has caused our handler to run.
	 */
	for (bank = 0; bank < priv->nr_banks; ++bank) {
		/* Which interrupts are currently pending in this bank? */
recalculate:
		status = metag_in32(stat_addr) & priv->unmasked[bank];

		for (hw = bank*32; status; status >>= 1, ++hw) {
			if (status & 0x1) {
				/*
				 * Map the hardware IRQ number to a virtual
				 * Linux IRQ number.
				 */
				irq_no = irq_linear_revmap(priv->domain, hw);

				/*
				 * Only fire off external interrupts that are
				 * registered to be handled by the kernel.
				 * Other external interrupts are probably being
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
		stat_addr += HWSTAT_STRIDE;
	}
}

#ifdef CONFIG_SMP
/**
 * meta_intc_set_affinity() - set the affinity for an interrupt
 * @data:	data for the external irq to set the affinity of
 * @cpumask:	cpu mask representing cpus which can handle the interrupt
 * @force:	whether to force (ignored)
 *
 * Revector the specified external irq onto a specific cpu's TR2 trigger, so
 * that that cpu tends to be the one who handles it.
 */
static int meta_intc_set_affinity(struct irq_data *data,
				  const struct cpumask *cpumask, bool force)
{
	irq_hw_number_t hw = data->hwirq;
	void __iomem *vec_addr = meta_intc_vec_addr(hw);
	unsigned int cpu, thread;

	/*
	 * Wire up this interrupt from HWVECxEXT to the Meta core.
	 *
	 * Note that we can't wire up HWVECxEXT to interrupt more than
	 * one cpu (the interrupt code doesn't support it), so we just
	 * pick the first cpu we find in 'cpumask'.
	 */
	cpu = cpumask_any_and(cpumask, cpu_online_mask);
	thread = cpu_2_hwthread_id[cpu];

	metag_out32(TBI_TRIG_VEC(TBID_SIGNUM_TR2(thread)), vec_addr);

	return 0;
}
#else
#define meta_intc_set_affinity	NULL
#endif

#ifdef CONFIG_PM_SLEEP
#define META_INTC_CHIP_FLAGS	(IRQCHIP_MASK_ON_SUSPEND \
				| IRQCHIP_SKIP_SET_WAKE)
#else
#define META_INTC_CHIP_FLAGS	0
#endif

/* public edge/level irq chips which SoCs can override */

struct irq_chip meta_intc_edge_chip = {
	.irq_startup		= meta_intc_startup_irq,
	.irq_shutdown		= meta_intc_shutdown_irq,
	.irq_ack		= meta_intc_ack_irq,
	.irq_mask		= meta_intc_mask_irq,
	.irq_unmask		= meta_intc_unmask_irq,
	.irq_set_type		= meta_intc_irq_set_type,
	.irq_set_affinity	= meta_intc_set_affinity,
	.flags			= META_INTC_CHIP_FLAGS,
};

struct irq_chip meta_intc_level_chip = {
	.irq_startup		= meta_intc_startup_irq,
	.irq_shutdown		= meta_intc_shutdown_irq,
	.irq_set_type		= meta_intc_irq_set_type,
	.irq_mask		= meta_intc_mask_irq,
	.irq_unmask		= meta_intc_unmask_irq,
	.irq_set_affinity	= meta_intc_set_affinity,
	.flags			= META_INTC_CHIP_FLAGS,
};

/**
 * meta_intc_map() - map an external irq
 * @d:		irq domain of external trigger block
 * @irq:	virtual irq number
 * @hw:		hardware irq number within external trigger block
 *
 * This sets up a virtual irq for a specified hardware interrupt. The irq chip
 * and handler is configured, using the HWLEVELEXT registers to determine
 * edge/level flow type. These registers will have been set when the irq type is
 * set (or set to a default at init time).
 */
static int meta_intc_map(struct irq_domain *d, unsigned int irq,
			 irq_hw_number_t hw)
{
	unsigned int bit = 1 << meta_intc_offset(hw);
	void __iomem *level_addr = meta_intc_level_addr(hw);

	/* Go by the current sense in the HWLEVELEXT register */
	if (metag_in32(level_addr) & bit)
		irq_set_chip_and_handler(irq, &meta_intc_level_chip,
					 handle_level_irq);
	else
		irq_set_chip_and_handler(irq, &meta_intc_edge_chip,
					 handle_edge_irq);
	return 0;
}

static const struct irq_domain_ops meta_intc_domain_ops = {
	.map = meta_intc_map,
	.xlate = irq_domain_xlate_twocell,
};

#ifdef CONFIG_METAG_SUSPEND_MEM

/**
 * struct meta_intc_context - suspend context
 * @levels:	State of HWLEVELEXT registers
 * @masks:	State of HWMASKEXT registers
 * @vectors:	State of HWVECEXT registers
 * @txvecint:	State of TxVECINT registers
 *
 * This structure stores the IRQ state across suspend.
 */
struct meta_intc_context {
	u32 levels[4];
	u32 masks[4];
	u8 vectors[4*32];

	u8 txvecint[4][4];
};

/* suspend context */
static struct meta_intc_context *meta_intc_context;

/**
 * meta_intc_suspend() - store irq state
 *
 * To avoid interfering with other threads we only save the IRQ state of IRQs in
 * use by Linux.
 */
static int meta_intc_suspend(void)
{
	struct meta_intc_priv *priv = &meta_intc_priv;
	int i, j;
	irq_hw_number_t hw;
	unsigned int bank;
	unsigned long flags;
	struct meta_intc_context *context;
	void __iomem *level_addr, *mask_addr, *vec_addr;
	u32 mask, bit;

	context = kzalloc(sizeof(*context), GFP_ATOMIC);
	if (!context)
		return -ENOMEM;

	hw = 0;
	level_addr = meta_intc_level_addr(0);
	mask_addr = meta_intc_mask_addr(0);
	for (bank = 0; bank < priv->nr_banks; ++bank) {
		vec_addr = meta_intc_vec_addr(hw);

		/* create mask of interrupts in use */
		mask = 0;
		for (bit = 1; bit; bit <<= 1) {
			i = irq_linear_revmap(priv->domain, hw);
			/* save mapped irqs which are enabled or have actions */
			if (i && (!irqd_irq_disabled(irq_get_irq_data(i)) ||
				  irq_has_action(i))) {
				mask |= bit;

				/* save trigger vector */
				context->vectors[hw] = metag_in32(vec_addr);
			}

			++hw;
			vec_addr += HWVECnEXT_STRIDE;
		}

		/* save level state if any IRQ levels altered */
		if (priv->levels_altered[bank])
			context->levels[bank] = metag_in32(level_addr);
		/* save mask state if any IRQs in use */
		if (mask)
			context->masks[bank] = metag_in32(mask_addr);

		level_addr += HWSTAT_STRIDE;
		mask_addr += HWSTAT_STRIDE;
	}

	/* save trigger matrixing */
	__global_lock2(flags);
	for (i = 0; i < 4; ++i)
		for (j = 0; j < 4; ++j)
			context->txvecint[i][j] = metag_in32(T0VECINT_BHALT +
							     TnVECINT_STRIDE*i +
							     8*j);
	__global_unlock2(flags);

	meta_intc_context = context;
	return 0;
}

/**
 * meta_intc_resume() - restore saved irq state
 *
 * Restore the saved IRQ state and drop it.
 */
static void meta_intc_resume(void)
{
	struct meta_intc_priv *priv = &meta_intc_priv;
	int i, j;
	irq_hw_number_t hw;
	unsigned int bank;
	unsigned long flags;
	struct meta_intc_context *context = meta_intc_context;
	void __iomem *level_addr, *mask_addr, *vec_addr;
	u32 mask, bit, tmp;

	meta_intc_context = NULL;

	hw = 0;
	level_addr = meta_intc_level_addr(0);
	mask_addr = meta_intc_mask_addr(0);
	for (bank = 0; bank < priv->nr_banks; ++bank) {
		vec_addr = meta_intc_vec_addr(hw);

		/* create mask of interrupts in use */
		mask = 0;
		for (bit = 1; bit; bit <<= 1) {
			i = irq_linear_revmap(priv->domain, hw);
			/* restore mapped irqs, enabled or with actions */
			if (i && (!irqd_irq_disabled(irq_get_irq_data(i)) ||
				  irq_has_action(i))) {
				mask |= bit;

				/* restore trigger vector */
				metag_out32(context->vectors[hw], vec_addr);
			}

			++hw;
			vec_addr += HWVECnEXT_STRIDE;
		}

		if (mask) {
			/* restore mask state */
			__global_lock2(flags);
			tmp = metag_in32(mask_addr);
			tmp = (tmp & ~mask) | (context->masks[bank] & mask);
			metag_out32(tmp, mask_addr);
			__global_unlock2(flags);
		}

		mask = priv->levels_altered[bank];
		if (mask) {
			/* restore level state */
			__global_lock2(flags);
			tmp = metag_in32(level_addr);
			tmp = (tmp & ~mask) | (context->levels[bank] & mask);
			metag_out32(tmp, level_addr);
			__global_unlock2(flags);
		}

		level_addr += HWSTAT_STRIDE;
		mask_addr += HWSTAT_STRIDE;
	}

	/* restore trigger matrixing */
	__global_lock2(flags);
	for (i = 0; i < 4; ++i) {
		for (j = 0; j < 4; ++j) {
			metag_out32(context->txvecint[i][j],
				    T0VECINT_BHALT +
				    TnVECINT_STRIDE*i +
				    8*j);
		}
	}
	__global_unlock2(flags);

	kfree(context);
}

static struct syscore_ops meta_intc_syscore_ops = {
	.suspend = meta_intc_suspend,
	.resume = meta_intc_resume,
};

static void __init meta_intc_init_syscore_ops(struct meta_intc_priv *priv)
{
	register_syscore_ops(&meta_intc_syscore_ops);
}
#else
#define meta_intc_init_syscore_ops(priv) do {} while (0)
#endif

/**
 * meta_intc_init_cpu() - register with a Meta cpu
 * @priv:	private interrupt controller data
 * @cpu:	the CPU to register on
 *
 * Configure @cpu's TR2 irq so that we can demux external irqs.
 */
static void __init meta_intc_init_cpu(struct meta_intc_priv *priv, int cpu)
{
	unsigned int thread = cpu_2_hwthread_id[cpu];
	unsigned int signum = TBID_SIGNUM_TR2(thread);
	int irq = tbisig_map(signum);

	/* Register the multiplexed IRQ handler */
	irq_set_chained_handler(irq, meta_intc_irq_demux);
	irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);
}

/**
 * meta_intc_no_mask() - indicate lack of HWMASKEXT registers
 *
 * Called from SoC code (or init code below) to dynamically indicate the lack of
 * HWMASKEXT registers (for example depending on some SoC revision register).
 * This alters the irq mask and unmask callbacks to use the fallback
 * unvectoring/retriggering technique instead of using HWMASKEXT registers.
 */
void __init meta_intc_no_mask(void)
{
	meta_intc_edge_chip.irq_mask	= meta_intc_mask_irq_nomask;
	meta_intc_edge_chip.irq_unmask	= meta_intc_unmask_edge_irq_nomask;
	meta_intc_level_chip.irq_mask	= meta_intc_mask_irq_nomask;
	meta_intc_level_chip.irq_unmask	= meta_intc_unmask_level_irq_nomask;
}

/**
 * init_external_IRQ() - initialise the external irq controller
 *
 * Set up the external irq controller using device tree properties. This is
 * called from init_IRQ().
 */
int __init init_external_IRQ(void)
{
	struct meta_intc_priv *priv = &meta_intc_priv;
	struct device_node *node;
	int ret, cpu;
	u32 val;
	bool no_masks = false;

	node = of_find_compatible_node(NULL, NULL, "img,meta-intc");
	if (!node)
		return -ENOENT;

	/* Get number of banks */
	ret = of_property_read_u32(node, "num-banks", &val);
	if (ret) {
		pr_err("meta-intc: No num-banks property found\n");
		return ret;
	}
	if (val < 1 || val > 4) {
		pr_err("meta-intc: num-banks (%u) out of range\n", val);
		return -EINVAL;
	}
	priv->nr_banks = val;

	/* Are any mask registers present? */
	if (of_get_property(node, "no-mask", NULL))
		no_masks = true;

	/* No HWMASKEXT registers present? */
	if (no_masks)
		meta_intc_no_mask();

	/* Set up an IRQ domain */
	/*
	 * This is a legacy IRQ domain for now until all the platform setup code
	 * has been converted to devicetree.
	 */
	priv->domain = irq_domain_add_linear(node, priv->nr_banks*32,
					     &meta_intc_domain_ops, priv);
	if (unlikely(!priv->domain)) {
		pr_err("meta-intc: cannot add IRQ domain\n");
		return -ENOMEM;
	}

	/* Setup TR2 for all cpus. */
	for_each_possible_cpu(cpu)
		meta_intc_init_cpu(priv, cpu);

	/* Set up system suspend/resume callbacks */
	meta_intc_init_syscore_ops(priv);

	pr_info("meta-intc: External IRQ controller initialised (%u IRQs)\n",
		priv->nr_banks*32);

	return 0;
}
