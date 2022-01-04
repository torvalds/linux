// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */

/* DOC: IPA Interrupts
 *
 * The IPA has an interrupt line distinct from the interrupt used by the GSI
 * code.  Whereas GSI interrupts are generally related to channel events (like
 * transfer completions), IPA interrupts are related to other events related
 * to the IPA.  Some of the IPA interrupts come from a microcontroller
 * embedded in the IPA.  Each IPA interrupt type can be both masked and
 * acknowledged independent of the others.
 *
 * Two of the IPA interrupts are initiated by the microcontroller.  A third
 * can be generated to signal the need for a wakeup/resume when an IPA
 * endpoint has been suspended.  There are other IPA events, but at this
 * time only these three are supported.
 */

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>

#include "ipa.h"
#include "ipa_reg.h"
#include "ipa_endpoint.h"
#include "ipa_interrupt.h"

/**
 * struct ipa_interrupt - IPA interrupt information
 * @ipa:		IPA pointer
 * @irq:		Linux IRQ number used for IPA interrupts
 * @enabled:		Mask indicating which interrupts are enabled
 * @handler:		Array of handlers indexed by IPA interrupt ID
 */
struct ipa_interrupt {
	struct ipa *ipa;
	u32 irq;
	u32 enabled;
	ipa_irq_handler_t handler[IPA_IRQ_COUNT];
};

/* Returns true if the interrupt type is associated with the microcontroller */
static bool ipa_interrupt_uc(struct ipa_interrupt *interrupt, u32 irq_id)
{
	return irq_id == IPA_IRQ_UC_0 || irq_id == IPA_IRQ_UC_1;
}

/* Process a particular interrupt type that has been received */
static void ipa_interrupt_process(struct ipa_interrupt *interrupt, u32 irq_id)
{
	bool uc_irq = ipa_interrupt_uc(interrupt, irq_id);
	struct ipa *ipa = interrupt->ipa;
	u32 mask = BIT(irq_id);
	u32 offset;

	/* For microcontroller interrupts, clear the interrupt right away,
	 * "to avoid clearing unhandled interrupts."
	 */
	offset = ipa_reg_irq_clr_offset(ipa->version);
	if (uc_irq)
		iowrite32(mask, ipa->reg_virt + offset);

	if (irq_id < IPA_IRQ_COUNT && interrupt->handler[irq_id])
		interrupt->handler[irq_id](interrupt->ipa, irq_id);

	/* Clearing the SUSPEND_TX interrupt also clears the register
	 * that tells us which suspended endpoint(s) caused the interrupt,
	 * so defer clearing until after the handler has been called.
	 */
	if (!uc_irq)
		iowrite32(mask, ipa->reg_virt + offset);
}

/* IPA IRQ handler is threaded */
static irqreturn_t ipa_isr_thread(int irq, void *dev_id)
{
	struct ipa_interrupt *interrupt = dev_id;
	struct ipa *ipa = interrupt->ipa;
	u32 enabled = interrupt->enabled;
	struct device *dev;
	u32 pending;
	u32 offset;
	u32 mask;
	int ret;

	dev = &ipa->pdev->dev;
	ret = pm_runtime_get_sync(dev);
	if (WARN_ON(ret < 0))
		goto out_power_put;

	/* The status register indicates which conditions are present,
	 * including conditions whose interrupt is not enabled.  Handle
	 * only the enabled ones.
	 */
	offset = ipa_reg_irq_stts_offset(ipa->version);
	pending = ioread32(ipa->reg_virt + offset);
	while ((mask = pending & enabled)) {
		do {
			u32 irq_id = __ffs(mask);

			mask ^= BIT(irq_id);

			ipa_interrupt_process(interrupt, irq_id);
		} while (mask);
		pending = ioread32(ipa->reg_virt + offset);
	}

	/* If any disabled interrupts are pending, clear them */
	if (pending) {
		dev_dbg(dev, "clearing disabled IPA interrupts 0x%08x\n",
			pending);
		offset = ipa_reg_irq_clr_offset(ipa->version);
		iowrite32(pending, ipa->reg_virt + offset);
	}
out_power_put:
	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);

	return IRQ_HANDLED;
}

/* Common function used to enable/disable TX_SUSPEND for an endpoint */
static void ipa_interrupt_suspend_control(struct ipa_interrupt *interrupt,
					  u32 endpoint_id, bool enable)
{
	struct ipa *ipa = interrupt->ipa;
	u32 mask = BIT(endpoint_id);
	u32 offset;
	u32 val;

	WARN_ON(!(mask & ipa->available));

	/* IPA version 3.0 does not support TX_SUSPEND interrupt control */
	if (ipa->version == IPA_VERSION_3_0)
		return;

	offset = ipa_reg_irq_suspend_en_offset(ipa->version);
	val = ioread32(ipa->reg_virt + offset);
	if (enable)
		val |= mask;
	else
		val &= ~mask;
	iowrite32(val, ipa->reg_virt + offset);
}

/* Enable TX_SUSPEND for an endpoint */
void
ipa_interrupt_suspend_enable(struct ipa_interrupt *interrupt, u32 endpoint_id)
{
	ipa_interrupt_suspend_control(interrupt, endpoint_id, true);
}

/* Disable TX_SUSPEND for an endpoint */
void
ipa_interrupt_suspend_disable(struct ipa_interrupt *interrupt, u32 endpoint_id)
{
	ipa_interrupt_suspend_control(interrupt, endpoint_id, false);
}

/* Clear the suspend interrupt for all endpoints that signaled it */
void ipa_interrupt_suspend_clear_all(struct ipa_interrupt *interrupt)
{
	struct ipa *ipa = interrupt->ipa;
	u32 offset;
	u32 val;

	offset = ipa_reg_irq_suspend_info_offset(ipa->version);
	val = ioread32(ipa->reg_virt + offset);

	/* SUSPEND interrupt status isn't cleared on IPA version 3.0 */
	if (ipa->version == IPA_VERSION_3_0)
		return;

	offset = ipa_reg_irq_suspend_clr_offset(ipa->version);
	iowrite32(val, ipa->reg_virt + offset);
}

/* Simulate arrival of an IPA TX_SUSPEND interrupt */
void ipa_interrupt_simulate_suspend(struct ipa_interrupt *interrupt)
{
	ipa_interrupt_process(interrupt, IPA_IRQ_TX_SUSPEND);
}

/* Add a handler for an IPA interrupt */
void ipa_interrupt_add(struct ipa_interrupt *interrupt,
		       enum ipa_irq_id ipa_irq, ipa_irq_handler_t handler)
{
	struct ipa *ipa = interrupt->ipa;
	u32 offset;

	WARN_ON(ipa_irq >= IPA_IRQ_COUNT);

	interrupt->handler[ipa_irq] = handler;

	/* Update the IPA interrupt mask to enable it */
	interrupt->enabled |= BIT(ipa_irq);
	offset = ipa_reg_irq_en_offset(ipa->version);
	iowrite32(interrupt->enabled, ipa->reg_virt + offset);
}

/* Remove the handler for an IPA interrupt type */
void
ipa_interrupt_remove(struct ipa_interrupt *interrupt, enum ipa_irq_id ipa_irq)
{
	struct ipa *ipa = interrupt->ipa;
	u32 offset;

	WARN_ON(ipa_irq >= IPA_IRQ_COUNT);

	/* Update the IPA interrupt mask to disable it */
	interrupt->enabled &= ~BIT(ipa_irq);
	offset = ipa_reg_irq_en_offset(ipa->version);
	iowrite32(interrupt->enabled, ipa->reg_virt + offset);

	interrupt->handler[ipa_irq] = NULL;
}

/* Configure the IPA interrupt framework */
struct ipa_interrupt *ipa_interrupt_config(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	struct ipa_interrupt *interrupt;
	unsigned int irq;
	u32 offset;
	int ret;

	ret = platform_get_irq_byname(ipa->pdev, "ipa");
	if (ret <= 0) {
		dev_err(dev, "DT error %d getting \"ipa\" IRQ property\n",
			ret);
		return ERR_PTR(ret ? : -EINVAL);
	}
	irq = ret;

	interrupt = kzalloc(sizeof(*interrupt), GFP_KERNEL);
	if (!interrupt)
		return ERR_PTR(-ENOMEM);
	interrupt->ipa = ipa;
	interrupt->irq = irq;

	/* Start with all IPA interrupts disabled */
	offset = ipa_reg_irq_en_offset(ipa->version);
	iowrite32(0, ipa->reg_virt + offset);

	ret = request_threaded_irq(irq, NULL, ipa_isr_thread, IRQF_ONESHOT,
				   "ipa", interrupt);
	if (ret) {
		dev_err(dev, "error %d requesting \"ipa\" IRQ\n", ret);
		goto err_kfree;
	}

	ret = enable_irq_wake(irq);
	if (ret) {
		dev_err(dev, "error %d enabling wakeup for \"ipa\" IRQ\n", ret);
		goto err_free_irq;
	}

	return interrupt;

err_free_irq:
	free_irq(interrupt->irq, interrupt);
err_kfree:
	kfree(interrupt);

	return ERR_PTR(ret);
}

/* Inverse of ipa_interrupt_config() */
void ipa_interrupt_deconfig(struct ipa_interrupt *interrupt)
{
	struct device *dev = &interrupt->ipa->pdev->dev;
	int ret;

	ret = disable_irq_wake(interrupt->irq);
	if (ret)
		dev_err(dev, "error %d disabling \"ipa\" IRQ wakeup\n", ret);
	free_irq(interrupt->irq, interrupt);
	kfree(interrupt);
}
