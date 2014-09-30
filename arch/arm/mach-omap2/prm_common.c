/*
 * OMAP2+ common Power & Reset Management (PRM) IP block functions
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Tero Kristo <t-kristo@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * For historical purposes, the API used to configure the PRM
 * interrupt handler refers to it as the "PRCM interrupt."  The
 * underlying registers are located in the PRM on OMAP3/4.
 *
 * XXX This code should eventually be moved to a PRM driver.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>

#include "soc.h"
#include "prm2xxx_3xxx.h"
#include "prm2xxx.h"
#include "prm3xxx.h"
#include "prm44xx.h"
#include "common.h"
#include "clock.h"

/*
 * OMAP_PRCM_MAX_NR_PENDING_REG: maximum number of PRM_IRQ*_MPU regs
 * XXX this is technically not needed, since
 * omap_prcm_register_chain_handler() could allocate this based on the
 * actual amount of memory needed for the SoC
 */
#define OMAP_PRCM_MAX_NR_PENDING_REG		2

/*
 * prcm_irq_chips: an array of all of the "generic IRQ chips" in use
 * by the PRCM interrupt handler code.  There will be one 'chip' per
 * PRM_{IRQSTATUS,IRQENABLE}_MPU register pair.  (So OMAP3 will have
 * one "chip" and OMAP4 will have two.)
 */
static struct irq_chip_generic **prcm_irq_chips;

/*
 * prcm_irq_setup: the PRCM IRQ parameters for the hardware the code
 * is currently running on.  Defined and passed by initialization code
 * that calls omap_prcm_register_chain_handler().
 */
static struct omap_prcm_irq_setup *prcm_irq_setup;

/* prm_base: base virtual address of the PRM IP block */
void __iomem *prm_base;

u16 prm_features;

/*
 * prm_ll_data: function pointers to SoC-specific implementations of
 * common PRM functions
 */
static struct prm_ll_data null_prm_ll_data;
static struct prm_ll_data *prm_ll_data = &null_prm_ll_data;

/* Private functions */

/*
 * Move priority events from events to priority_events array
 */
static void omap_prcm_events_filter_priority(unsigned long *events,
	unsigned long *priority_events)
{
	int i;

	for (i = 0; i < prcm_irq_setup->nr_regs; i++) {
		priority_events[i] =
			events[i] & prcm_irq_setup->priority_mask[i];
		events[i] ^= priority_events[i];
	}
}

/*
 * PRCM Interrupt Handler
 *
 * This is a common handler for the OMAP PRCM interrupts. Pending
 * interrupts are detected by a call to prcm_pending_events and
 * dispatched accordingly. Clearing of the wakeup events should be
 * done by the SoC specific individual handlers.
 */
static void omap_prcm_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long pending[OMAP_PRCM_MAX_NR_PENDING_REG];
	unsigned long priority_pending[OMAP_PRCM_MAX_NR_PENDING_REG];
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int virtirq;
	int nr_irq = prcm_irq_setup->nr_regs * 32;

	/*
	 * If we are suspended, mask all interrupts from PRCM level,
	 * this does not ack them, and they will be pending until we
	 * re-enable the interrupts, at which point the
	 * omap_prcm_irq_handler will be executed again.  The
	 * _save_and_clear_irqen() function must ensure that the PRM
	 * write to disable all IRQs has reached the PRM before
	 * returning, or spurious PRCM interrupts may occur during
	 * suspend.
	 */
	if (prcm_irq_setup->suspended) {
		prcm_irq_setup->save_and_clear_irqen(prcm_irq_setup->saved_mask);
		prcm_irq_setup->suspend_save_flag = true;
	}

	/*
	 * Loop until all pending irqs are handled, since
	 * generic_handle_irq() can cause new irqs to come
	 */
	while (!prcm_irq_setup->suspended) {
		prcm_irq_setup->read_pending_irqs(pending);

		/* No bit set, then all IRQs are handled */
		if (find_first_bit(pending, nr_irq) >= nr_irq)
			break;

		omap_prcm_events_filter_priority(pending, priority_pending);

		/*
		 * Loop on all currently pending irqs so that new irqs
		 * cannot starve previously pending irqs
		 */

		/* Serve priority events first */
		for_each_set_bit(virtirq, priority_pending, nr_irq)
			generic_handle_irq(prcm_irq_setup->base_irq + virtirq);

		/* Serve normal events next */
		for_each_set_bit(virtirq, pending, nr_irq)
			generic_handle_irq(prcm_irq_setup->base_irq + virtirq);
	}
	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);
	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
	chip->irq_unmask(&desc->irq_data);

	prcm_irq_setup->ocp_barrier(); /* avoid spurious IRQs */
}

/* Public functions */

/**
 * omap_prcm_event_to_irq - given a PRCM event name, returns the
 * corresponding IRQ on which the handler should be registered
 * @name: name of the PRCM interrupt bit to look up - see struct omap_prcm_irq
 *
 * Returns the Linux internal IRQ ID corresponding to @name upon success,
 * or -ENOENT upon failure.
 */
int omap_prcm_event_to_irq(const char *name)
{
	int i;

	if (!prcm_irq_setup || !name)
		return -ENOENT;

	for (i = 0; i < prcm_irq_setup->nr_irqs; i++)
		if (!strcmp(prcm_irq_setup->irqs[i].name, name))
			return prcm_irq_setup->base_irq +
				prcm_irq_setup->irqs[i].offset;

	return -ENOENT;
}

/**
 * omap_prcm_irq_cleanup - reverses memory allocated and other steps
 * done by omap_prcm_register_chain_handler()
 *
 * No return value.
 */
void omap_prcm_irq_cleanup(void)
{
	int i;

	if (!prcm_irq_setup) {
		pr_err("PRCM: IRQ handler not initialized; cannot cleanup\n");
		return;
	}

	if (prcm_irq_chips) {
		for (i = 0; i < prcm_irq_setup->nr_regs; i++) {
			if (prcm_irq_chips[i])
				irq_remove_generic_chip(prcm_irq_chips[i],
					0xffffffff, 0, 0);
			prcm_irq_chips[i] = NULL;
		}
		kfree(prcm_irq_chips);
		prcm_irq_chips = NULL;
	}

	kfree(prcm_irq_setup->saved_mask);
	prcm_irq_setup->saved_mask = NULL;

	kfree(prcm_irq_setup->priority_mask);
	prcm_irq_setup->priority_mask = NULL;

	irq_set_chained_handler(prcm_irq_setup->irq, NULL);

	if (prcm_irq_setup->base_irq > 0)
		irq_free_descs(prcm_irq_setup->base_irq,
			prcm_irq_setup->nr_regs * 32);
	prcm_irq_setup->base_irq = 0;
}

void omap_prcm_irq_prepare(void)
{
	prcm_irq_setup->suspended = true;
}

void omap_prcm_irq_complete(void)
{
	prcm_irq_setup->suspended = false;

	/* If we have not saved the masks, do not attempt to restore */
	if (!prcm_irq_setup->suspend_save_flag)
		return;

	prcm_irq_setup->suspend_save_flag = false;

	/*
	 * Re-enable all masked PRCM irq sources, this causes the PRCM
	 * interrupt to fire immediately if the events were masked
	 * previously in the chain handler
	 */
	prcm_irq_setup->restore_irqen(prcm_irq_setup->saved_mask);
}

/**
 * omap_prcm_register_chain_handler - initializes the prcm chained interrupt
 * handler based on provided parameters
 * @irq_setup: hardware data about the underlying PRM/PRCM
 *
 * Set up the PRCM chained interrupt handler on the PRCM IRQ.  Sets up
 * one generic IRQ chip per PRM interrupt status/enable register pair.
 * Returns 0 upon success, -EINVAL if called twice or if invalid
 * arguments are passed, or -ENOMEM on any other error.
 */
int omap_prcm_register_chain_handler(struct omap_prcm_irq_setup *irq_setup)
{
	int nr_regs;
	u32 mask[OMAP_PRCM_MAX_NR_PENDING_REG];
	int offset, i;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	if (!irq_setup)
		return -EINVAL;

	nr_regs = irq_setup->nr_regs;

	if (prcm_irq_setup) {
		pr_err("PRCM: already initialized; won't reinitialize\n");
		return -EINVAL;
	}

	if (nr_regs > OMAP_PRCM_MAX_NR_PENDING_REG) {
		pr_err("PRCM: nr_regs too large\n");
		return -EINVAL;
	}

	prcm_irq_setup = irq_setup;

	prcm_irq_chips = kzalloc(sizeof(void *) * nr_regs, GFP_KERNEL);
	prcm_irq_setup->saved_mask = kzalloc(sizeof(u32) * nr_regs, GFP_KERNEL);
	prcm_irq_setup->priority_mask = kzalloc(sizeof(u32) * nr_regs,
		GFP_KERNEL);

	if (!prcm_irq_chips || !prcm_irq_setup->saved_mask ||
	    !prcm_irq_setup->priority_mask) {
		pr_err("PRCM: kzalloc failed\n");
		goto err;
	}

	memset(mask, 0, sizeof(mask));

	for (i = 0; i < irq_setup->nr_irqs; i++) {
		offset = irq_setup->irqs[i].offset;
		mask[offset >> 5] |= 1 << (offset & 0x1f);
		if (irq_setup->irqs[i].priority)
			irq_setup->priority_mask[offset >> 5] |=
				1 << (offset & 0x1f);
	}

	irq_set_chained_handler(irq_setup->irq, omap_prcm_irq_handler);

	irq_setup->base_irq = irq_alloc_descs(-1, 0, irq_setup->nr_regs * 32,
		0);

	if (irq_setup->base_irq < 0) {
		pr_err("PRCM: failed to allocate irq descs: %d\n",
			irq_setup->base_irq);
		goto err;
	}

	for (i = 0; i < irq_setup->nr_regs; i++) {
		gc = irq_alloc_generic_chip("PRCM", 1,
			irq_setup->base_irq + i * 32, prm_base,
			handle_level_irq);

		if (!gc) {
			pr_err("PRCM: failed to allocate generic chip\n");
			goto err;
		}
		ct = gc->chip_types;
		ct->chip.irq_ack = irq_gc_ack_set_bit;
		ct->chip.irq_mask = irq_gc_mask_clr_bit;
		ct->chip.irq_unmask = irq_gc_mask_set_bit;

		ct->regs.ack = irq_setup->ack + i * 4;
		ct->regs.mask = irq_setup->mask + i * 4;

		irq_setup_generic_chip(gc, mask[i], 0, IRQ_NOREQUEST, 0);
		prcm_irq_chips[i] = gc;
	}

	if (of_have_populated_dt()) {
		int irq = omap_prcm_event_to_irq("io");
		omap_pcs_legacy_init(irq, irq_setup->reconfigure_io_chain);
	}

	return 0;

err:
	omap_prcm_irq_cleanup();
	return -ENOMEM;
}

/**
 * omap2_set_globals_prm - set the PRM base address (for early use)
 * @prm: PRM base virtual address
 *
 * XXX Will be replaced when the PRM/CM drivers are completed.
 */
void __init omap2_set_globals_prm(void __iomem *prm)
{
	prm_base = prm;
}

/**
 * prm_read_reset_sources - return the sources of the SoC's last reset
 *
 * Return a u32 bitmask representing the reset sources that caused the
 * SoC to reset.  The low-level per-SoC functions called by this
 * function remap the SoC-specific reset source bits into an
 * OMAP-common set of reset source bits, defined in
 * arch/arm/mach-omap2/prm.h.  Returns the standardized reset source
 * u32 bitmask from the hardware upon success, or returns (1 <<
 * OMAP_UNKNOWN_RST_SRC_ID_SHIFT) if no low-level read_reset_sources()
 * function was registered.
 */
u32 prm_read_reset_sources(void)
{
	u32 ret = 1 << OMAP_UNKNOWN_RST_SRC_ID_SHIFT;

	if (prm_ll_data->read_reset_sources)
		ret = prm_ll_data->read_reset_sources();
	else
		WARN_ONCE(1, "prm: %s: no mapping function defined for reset sources\n", __func__);

	return ret;
}

/**
 * prm_was_any_context_lost_old - was device context lost? (old API)
 * @part: PRM partition ID (e.g., OMAP4430_PRM_PARTITION)
 * @inst: PRM instance offset (e.g., OMAP4430_PRM_MPU_INST)
 * @idx: CONTEXT register offset
 *
 * Return 1 if any bits were set in the *_CONTEXT_* register
 * identified by (@part, @inst, @idx), which means that some context
 * was lost for that module; otherwise, return 0.  XXX Deprecated;
 * callers need to use a less-SoC-dependent way to identify hardware
 * IP blocks.
 */
bool prm_was_any_context_lost_old(u8 part, s16 inst, u16 idx)
{
	bool ret = true;

	if (prm_ll_data->was_any_context_lost_old)
		ret = prm_ll_data->was_any_context_lost_old(part, inst, idx);
	else
		WARN_ONCE(1, "prm: %s: no mapping function defined\n",
			  __func__);

	return ret;
}

/**
 * prm_clear_context_lost_flags_old - clear context loss flags (old API)
 * @part: PRM partition ID (e.g., OMAP4430_PRM_PARTITION)
 * @inst: PRM instance offset (e.g., OMAP4430_PRM_MPU_INST)
 * @idx: CONTEXT register offset
 *
 * Clear hardware context loss bits for the module identified by
 * (@part, @inst, @idx).  No return value.  XXX Deprecated; callers
 * need to use a less-SoC-dependent way to identify hardware IP
 * blocks.
 */
void prm_clear_context_loss_flags_old(u8 part, s16 inst, u16 idx)
{
	if (prm_ll_data->clear_context_loss_flags_old)
		prm_ll_data->clear_context_loss_flags_old(part, inst, idx);
	else
		WARN_ONCE(1, "prm: %s: no mapping function defined\n",
			  __func__);
}

/**
 * prm_register - register per-SoC low-level data with the PRM
 * @pld: low-level per-SoC OMAP PRM data & function pointers to register
 *
 * Register per-SoC low-level OMAP PRM data and function pointers with
 * the OMAP PRM common interface.  The caller must keep the data
 * pointed to by @pld valid until it calls prm_unregister() and
 * it returns successfully.  Returns 0 upon success, -EINVAL if @pld
 * is NULL, or -EEXIST if prm_register() has already been called
 * without an intervening prm_unregister().
 */
int prm_register(struct prm_ll_data *pld)
{
	if (!pld)
		return -EINVAL;

	if (prm_ll_data != &null_prm_ll_data)
		return -EEXIST;

	prm_ll_data = pld;

	return 0;
}

/**
 * prm_unregister - unregister per-SoC low-level data & function pointers
 * @pld: low-level per-SoC OMAP PRM data & function pointers to unregister
 *
 * Unregister per-SoC low-level OMAP PRM data and function pointers
 * that were previously registered with prm_register().  The
 * caller may not destroy any of the data pointed to by @pld until
 * this function returns successfully.  Returns 0 upon success, or
 * -EINVAL if @pld is NULL or if @pld does not match the struct
 * prm_ll_data * previously registered by prm_register().
 */
int prm_unregister(struct prm_ll_data *pld)
{
	if (!pld || prm_ll_data != pld)
		return -EINVAL;

	prm_ll_data = &null_prm_ll_data;

	return 0;
}

static struct of_device_id omap_prcm_dt_match_table[] = {
	{ .compatible = "ti,am3-prcm" },
	{ .compatible = "ti,am3-scrm" },
	{ .compatible = "ti,am4-prcm" },
	{ .compatible = "ti,am4-scrm" },
	{ .compatible = "ti,omap2-prcm" },
	{ .compatible = "ti,omap2-scrm" },
	{ .compatible = "ti,omap3-prm" },
	{ .compatible = "ti,omap3-cm" },
	{ .compatible = "ti,omap3-scrm" },
	{ .compatible = "ti,omap4-cm1" },
	{ .compatible = "ti,omap4-prm" },
	{ .compatible = "ti,omap4-cm2" },
	{ .compatible = "ti,omap4-scrm" },
	{ .compatible = "ti,omap5-prm" },
	{ .compatible = "ti,omap5-cm-core-aon" },
	{ .compatible = "ti,omap5-scrm" },
	{ .compatible = "ti,omap5-cm-core" },
	{ .compatible = "ti,dra7-prm" },
	{ .compatible = "ti,dra7-cm-core-aon" },
	{ .compatible = "ti,dra7-cm-core" },
	{ }
};

static struct clk_hw_omap memmap_dummy_ck = {
	.flags = MEMMAP_ADDRESSING,
};

static u32 prm_clk_readl(void __iomem *reg)
{
	return omap2_clk_readl(&memmap_dummy_ck, reg);
}

static void prm_clk_writel(u32 val, void __iomem *reg)
{
	omap2_clk_writel(val, &memmap_dummy_ck, reg);
}

static struct ti_clk_ll_ops omap_clk_ll_ops = {
	.clk_readl = prm_clk_readl,
	.clk_writel = prm_clk_writel,
};

int __init of_prcm_init(void)
{
	struct device_node *np;
	void __iomem *mem;
	int memmap_index = 0;

	ti_clk_ll_ops = &omap_clk_ll_ops;

	for_each_matching_node(np, omap_prcm_dt_match_table) {
		mem = of_iomap(np, 0);
		clk_memmaps[memmap_index] = mem;
		ti_dt_clk_init_provider(np, memmap_index);
		memmap_index++;
	}

	return 0;
}

static int __init prm_late_init(void)
{
	if (prm_ll_data->late_init)
		return prm_ll_data->late_init();
	return 0;
}
subsys_initcall(prm_late_init);
