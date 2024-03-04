// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2022 Linaro Ltd.
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

#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>

#include "ipa.h"
#include "ipa_reg.h"
#include "ipa_endpoint.h"
#include "ipa_power.h"
#include "ipa_uc.h"
#include "ipa_interrupt.h"

/**
 * struct ipa_interrupt - IPA interrupt information
 * @ipa:		IPA pointer
 * @irq:		Linux IRQ number used for IPA interrupts
 * @enabled:		Mask indicating which interrupts are enabled
 */
struct ipa_interrupt {
	struct ipa *ipa;
	u32 irq;
	u32 enabled;
};

/* Clear the suspend interrupt for all endpoints that signaled it */
static void ipa_interrupt_suspend_clear_all(struct ipa_interrupt *interrupt)
{
	struct ipa *ipa = interrupt->ipa;
	u32 unit_count;
	u32 unit;

	unit_count = DIV_ROUND_UP(ipa->endpoint_count, 32);
	for (unit = 0; unit < unit_count; unit++) {
		const struct reg *reg;
		u32 val;

		reg = ipa_reg(ipa, IRQ_SUSPEND_INFO);
		val = ioread32(ipa->reg_virt + reg_n_offset(reg, unit));

		/* SUSPEND interrupt status isn't cleared on IPA version 3.0 */
		if (!val || ipa->version == IPA_VERSION_3_0)
			continue;

		reg = ipa_reg(ipa, IRQ_SUSPEND_CLR);
		iowrite32(val, ipa->reg_virt + reg_n_offset(reg, unit));
	}
}

/* Process a particular interrupt type that has been received */
static void ipa_interrupt_process(struct ipa_interrupt *interrupt, u32 irq_id)
{
	struct ipa *ipa = interrupt->ipa;
	const struct reg *reg;
	u32 mask = BIT(irq_id);
	u32 offset;

	reg = ipa_reg(ipa, IPA_IRQ_CLR);
	offset = reg_offset(reg);

	switch (irq_id) {
	case IPA_IRQ_UC_0:
	case IPA_IRQ_UC_1:
		/* For microcontroller interrupts, clear the interrupt right
		 * away, "to avoid clearing unhandled interrupts."
		 */
		iowrite32(mask, ipa->reg_virt + offset);
		ipa_uc_interrupt_handler(ipa, irq_id);
		break;

	case IPA_IRQ_TX_SUSPEND:
		/* Clearing the SUSPEND_TX interrupt also clears the
		 * register that tells us which suspended endpoint(s)
		 * caused the interrupt, so defer clearing until after
		 * the handler has been called.
		 */
		ipa_interrupt_suspend_clear_all(interrupt);
		fallthrough;

	default:	/* Silently ignore (and clear) any other condition */
		iowrite32(mask, ipa->reg_virt + offset);
		break;
	}
}

/* IPA IRQ handler is threaded */
static irqreturn_t ipa_isr_thread(int irq, void *dev_id)
{
	struct ipa_interrupt *interrupt = dev_id;
	struct ipa *ipa = interrupt->ipa;
	u32 enabled = interrupt->enabled;
	struct device *dev = ipa->dev;
	const struct reg *reg;
	u32 pending;
	u32 offset;
	u32 mask;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (WARN_ON(ret < 0))
		goto out_power_put;

	/* The status register indicates which conditions are present,
	 * including conditions whose interrupt is not enabled.  Handle
	 * only the enabled ones.
	 */
	reg = ipa_reg(ipa, IPA_IRQ_STTS);
	offset = reg_offset(reg);
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
		reg = ipa_reg(ipa, IPA_IRQ_CLR);
		iowrite32(pending, ipa->reg_virt + reg_offset(reg));
	}
out_power_put:
	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);

	return IRQ_HANDLED;
}

static void ipa_interrupt_enabled_update(struct ipa *ipa)
{
	const struct reg *reg = ipa_reg(ipa, IPA_IRQ_EN);

	iowrite32(ipa->interrupt->enabled, ipa->reg_virt + reg_offset(reg));
}

/* Enable an IPA interrupt type */
void ipa_interrupt_enable(struct ipa *ipa, enum ipa_irq_id ipa_irq)
{
	/* Update the IPA interrupt mask to enable it */
	ipa->interrupt->enabled |= BIT(ipa_irq);
	ipa_interrupt_enabled_update(ipa);
}

/* Disable an IPA interrupt type */
void ipa_interrupt_disable(struct ipa *ipa, enum ipa_irq_id ipa_irq)
{
	/* Update the IPA interrupt mask to disable it */
	ipa->interrupt->enabled &= ~BIT(ipa_irq);
	ipa_interrupt_enabled_update(ipa);
}

void ipa_interrupt_irq_disable(struct ipa *ipa)
{
	disable_irq(ipa->interrupt->irq);
}

void ipa_interrupt_irq_enable(struct ipa *ipa)
{
	enable_irq(ipa->interrupt->irq);
}

/* Common function used to enable/disable TX_SUSPEND for an endpoint */
static void ipa_interrupt_suspend_control(struct ipa_interrupt *interrupt,
					  u32 endpoint_id, bool enable)
{
	struct ipa *ipa = interrupt->ipa;
	u32 mask = BIT(endpoint_id % 32);
	u32 unit = endpoint_id / 32;
	const struct reg *reg;
	u32 offset;
	u32 val;

	WARN_ON(!test_bit(endpoint_id, ipa->available));

	/* IPA version 3.0 does not support TX_SUSPEND interrupt control */
	if (ipa->version == IPA_VERSION_3_0)
		return;

	reg = ipa_reg(ipa, IRQ_SUSPEND_EN);
	offset = reg_n_offset(reg, unit);
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

/* Simulate arrival of an IPA TX_SUSPEND interrupt */
void ipa_interrupt_simulate_suspend(struct ipa_interrupt *interrupt)
{
	ipa_interrupt_process(interrupt, IPA_IRQ_TX_SUSPEND);
}

/* Configure the IPA interrupt framework */
int ipa_interrupt_config(struct ipa *ipa)
{
	struct ipa_interrupt *interrupt = ipa->interrupt;
	unsigned int irq = interrupt->irq;
	struct device *dev = ipa->dev;
	const struct reg *reg;
	int ret;

	interrupt->ipa = ipa;

	/* Disable all IPA interrupt types */
	reg = ipa_reg(ipa, IPA_IRQ_EN);
	iowrite32(0, ipa->reg_virt + reg_offset(reg));

	ret = request_threaded_irq(irq, NULL, ipa_isr_thread, IRQF_ONESHOT,
				   "ipa", interrupt);
	if (ret) {
		dev_err(dev, "error %d requesting \"ipa\" IRQ\n", ret);
		goto err_kfree;
	}

	ret = dev_pm_set_wake_irq(dev, irq);
	if (ret) {
		dev_err(dev, "error %d registering \"ipa\" IRQ as wakeirq\n",
			ret);
		goto err_free_irq;
	}

	ipa->interrupt = interrupt;

	return 0;

err_free_irq:
	free_irq(interrupt->irq, interrupt);
err_kfree:
	kfree(interrupt);

	return ret;
}

/* Inverse of ipa_interrupt_config() */
void ipa_interrupt_deconfig(struct ipa *ipa)
{
	struct ipa_interrupt *interrupt = ipa->interrupt;
	struct device *dev = ipa->dev;

	ipa->interrupt = NULL;

	dev_pm_clear_wake_irq(dev);
	free_irq(interrupt->irq, interrupt);
}

/* Initialize the IPA interrupt structure */
struct ipa_interrupt *ipa_interrupt_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ipa_interrupt *interrupt;
	int irq;

	irq = platform_get_irq_byname(pdev, "ipa");
	if (irq <= 0) {
		dev_err(dev, "DT error %d getting \"ipa\" IRQ property\n", irq);

		return ERR_PTR(irq ? : -EINVAL);
	}

	interrupt = kzalloc(sizeof(*interrupt), GFP_KERNEL);
	if (!interrupt)
		return ERR_PTR(-ENOMEM);
	interrupt->irq = irq;

	return interrupt;
}

/* Inverse of ipa_interrupt_init() */
void ipa_interrupt_exit(struct ipa_interrupt *interrupt)
{
	kfree(interrupt);
}
