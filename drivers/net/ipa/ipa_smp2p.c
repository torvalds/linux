// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */

#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>

#include <linux/soc/qcom/smem_state.h>

#include "ipa.h"
#include "ipa_smp2p.h"
#include "ipa_uc.h"

/**
 * DOC: IPA SMP2P communication with the modem
 *
 * SMP2P is a primitive communication mechanism available between the AP and
 * the modem.  The IPA driver uses this for two purposes:  to enable the modem
 * to state that the GSI hardware is ready to use; and to communicate the
 * state of IPA power in the event of a crash.
 *
 * GSI needs to have early initialization completed before it can be used.
 * This initialization is done either by Trust Zone or by the modem.  In the
 * latter case, the modem uses an SMP2P interrupt to tell the AP IPA driver
 * when the GSI is ready to use.
 *
 * The modem is also able to inquire about the current state of IPA
 * power by trigging another SMP2P interrupt to the AP.  We communicate
 * whether power is enabled using two SMP2P state bits--one to indicate
 * the power state (on or off), and a second to indicate the power state
 * bit is valid.  The modem will poll the valid bit until it is set, and
 * at that time records whether the AP has IPA power enabled.
 *
 * Finally, if the AP kernel panics, we update the SMP2P state bits even if
 * we never receive an interrupt from the modem requesting this.
 */

/**
 * struct ipa_smp2p - IPA SMP2P information
 * @ipa:		IPA pointer
 * @valid_state:	SMEM state indicating enabled state is valid
 * @enabled_state:	SMEM state to indicate power is enabled
 * @valid_bit:		Valid bit in 32-bit SMEM state mask
 * @enabled_bit:	Enabled bit in 32-bit SMEM state mask
 * @enabled_bit:	Enabled bit in 32-bit SMEM state mask
 * @clock_query_irq:	IPA interrupt triggered by modem for power query
 * @setup_ready_irq:	IPA interrupt triggered by modem to signal GSI ready
 * @power_on:		Whether IPA power is on
 * @notified:		Whether modem has been notified of power state
 * @setup_disabled:	Whether setup ready interrupt handler is disabled
 * @mutex:		Mutex protecting ready-interrupt/shutdown interlock
 * @panic_notifier:	Panic notifier structure
*/
struct ipa_smp2p {
	struct ipa *ipa;
	struct qcom_smem_state *valid_state;
	struct qcom_smem_state *enabled_state;
	u32 valid_bit;
	u32 enabled_bit;
	u32 clock_query_irq;
	u32 setup_ready_irq;
	bool power_on;
	bool notified;
	bool setup_disabled;
	struct mutex mutex;
	struct notifier_block panic_notifier;
};

/**
 * ipa_smp2p_notify() - use SMP2P to tell modem about IPA power state
 * @smp2p:	SMP2P information
 *
 * This is called either when the modem has requested it (by triggering
 * the modem power query IPA interrupt) or whenever the AP is shutting down
 * (via a panic notifier).  It sets the two SMP2P state bits--one saying
 * whether the IPA power is on, and the other indicating the first bit
 * is valid.
 */
static void ipa_smp2p_notify(struct ipa_smp2p *smp2p)
{
	u32 value;
	u32 mask;

	if (smp2p->notified)
		return;

	smp2p->power_on = pm_runtime_get_if_active(smp2p->ipa->dev) > 0;

	/* Signal whether the IPA power is enabled */
	mask = BIT(smp2p->enabled_bit);
	value = smp2p->power_on ? mask : 0;
	qcom_smem_state_update_bits(smp2p->enabled_state, mask, value);

	/* Now indicate that the enabled flag is valid */
	mask = BIT(smp2p->valid_bit);
	value = mask;
	qcom_smem_state_update_bits(smp2p->valid_state, mask, value);

	smp2p->notified = true;
}

/* Threaded IRQ handler for modem "ipa-clock-query" SMP2P interrupt */
static irqreturn_t ipa_smp2p_modem_clk_query_isr(int irq, void *dev_id)
{
	struct ipa_smp2p *smp2p = dev_id;

	ipa_smp2p_notify(smp2p);

	return IRQ_HANDLED;
}

static int ipa_smp2p_panic_notifier(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	struct ipa_smp2p *smp2p;

	smp2p = container_of(nb, struct ipa_smp2p, panic_notifier);

	ipa_smp2p_notify(smp2p);

	if (smp2p->power_on)
		ipa_uc_panic_notifier(smp2p->ipa);

	return NOTIFY_DONE;
}

static int ipa_smp2p_panic_notifier_register(struct ipa_smp2p *smp2p)
{
	/* IPA panic handler needs to run before modem shuts down */
	smp2p->panic_notifier.notifier_call = ipa_smp2p_panic_notifier;
	smp2p->panic_notifier.priority = INT_MAX;	/* Do it early */

	return atomic_notifier_chain_register(&panic_notifier_list,
					      &smp2p->panic_notifier);
}

static void ipa_smp2p_panic_notifier_unregister(struct ipa_smp2p *smp2p)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &smp2p->panic_notifier);
}

/* Threaded IRQ handler for modem "ipa-setup-ready" SMP2P interrupt */
static irqreturn_t ipa_smp2p_modem_setup_ready_isr(int irq, void *dev_id)
{
	struct ipa_smp2p *smp2p = dev_id;
	struct ipa *ipa = smp2p->ipa;
	struct device *dev;
	int ret;

	/* Ignore any (spurious) interrupts received after the first */
	if (ipa->setup_complete)
		return IRQ_HANDLED;

	/* Power needs to be active for setup */
	dev = ipa->dev;
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "error %d getting power for setup\n", ret);
		goto out_power_put;
	}

	/* An error here won't cause driver shutdown, so warn if one occurs */
	ret = ipa_setup(ipa);
	WARN(ret != 0, "error %d from ipa_setup()\n", ret);

out_power_put:
	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);

	return IRQ_HANDLED;
}

/* Initialize SMP2P interrupts */
static int ipa_smp2p_irq_init(struct ipa_smp2p *smp2p,
			      struct platform_device *pdev,
			      const char *name, irq_handler_t handler)
{
	struct device *dev = &pdev->dev;
	unsigned int irq;
	int ret;

	ret = platform_get_irq_byname(pdev, name);
	if (ret <= 0)
		return ret ? : -EINVAL;
	irq = ret;

	ret = request_threaded_irq(irq, NULL, handler, 0, name, smp2p);
	if (ret) {
		dev_err(dev, "error %d requesting \"%s\" IRQ\n", ret, name);
		return ret;
	}

	return irq;
}

static void ipa_smp2p_irq_exit(struct ipa_smp2p *smp2p, u32 irq)
{
	free_irq(irq, smp2p);
}

/* Drop the power reference if it was taken in ipa_smp2p_notify() */
static void ipa_smp2p_power_release(struct ipa *ipa)
{
	struct device *dev = ipa->dev;

	if (!ipa->smp2p->power_on)
		return;

	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);
	ipa->smp2p->power_on = false;
}

/* Initialize the IPA SMP2P subsystem */
int
ipa_smp2p_init(struct ipa *ipa, struct platform_device *pdev, bool modem_init)
{
	struct qcom_smem_state *enabled_state;
	struct device *dev = &pdev->dev;
	struct qcom_smem_state *valid_state;
	struct ipa_smp2p *smp2p;
	u32 enabled_bit;
	u32 valid_bit;
	int ret;

	valid_state = qcom_smem_state_get(dev, "ipa-clock-enabled-valid",
					  &valid_bit);
	if (IS_ERR(valid_state))
		return PTR_ERR(valid_state);
	if (valid_bit >= 32)		/* BITS_PER_U32 */
		return -EINVAL;

	enabled_state = qcom_smem_state_get(dev, "ipa-clock-enabled",
					    &enabled_bit);
	if (IS_ERR(enabled_state))
		return PTR_ERR(enabled_state);
	if (enabled_bit >= 32)		/* BITS_PER_U32 */
		return -EINVAL;

	smp2p = kzalloc(sizeof(*smp2p), GFP_KERNEL);
	if (!smp2p)
		return -ENOMEM;

	smp2p->ipa = ipa;

	/* These fields are needed by the power query interrupt
	 * handler, so initialize them now.
	 */
	mutex_init(&smp2p->mutex);
	smp2p->valid_state = valid_state;
	smp2p->valid_bit = valid_bit;
	smp2p->enabled_state = enabled_state;
	smp2p->enabled_bit = enabled_bit;

	/* We have enough information saved to handle notifications */
	ipa->smp2p = smp2p;

	ret = ipa_smp2p_irq_init(smp2p, pdev, "ipa-clock-query",
				 ipa_smp2p_modem_clk_query_isr);
	if (ret < 0)
		goto err_null_smp2p;
	smp2p->clock_query_irq = ret;

	ret = ipa_smp2p_panic_notifier_register(smp2p);
	if (ret)
		goto err_irq_exit;

	if (modem_init) {
		/* Result will be non-zero (negative for error) */
		ret = ipa_smp2p_irq_init(smp2p, pdev, "ipa-setup-ready",
					 ipa_smp2p_modem_setup_ready_isr);
		if (ret < 0)
			goto err_notifier_unregister;
		smp2p->setup_ready_irq = ret;
	}

	return 0;

err_notifier_unregister:
	ipa_smp2p_panic_notifier_unregister(smp2p);
err_irq_exit:
	ipa_smp2p_irq_exit(smp2p, smp2p->clock_query_irq);
err_null_smp2p:
	ipa->smp2p = NULL;
	mutex_destroy(&smp2p->mutex);
	kfree(smp2p);

	return ret;
}

void ipa_smp2p_exit(struct ipa *ipa)
{
	struct ipa_smp2p *smp2p = ipa->smp2p;

	if (smp2p->setup_ready_irq)
		ipa_smp2p_irq_exit(smp2p, smp2p->setup_ready_irq);
	ipa_smp2p_panic_notifier_unregister(smp2p);
	ipa_smp2p_irq_exit(smp2p, smp2p->clock_query_irq);
	/* We won't get notified any more; drop power reference (if any) */
	ipa_smp2p_power_release(ipa);
	ipa->smp2p = NULL;
	mutex_destroy(&smp2p->mutex);
	kfree(smp2p);
}

void ipa_smp2p_irq_disable_setup(struct ipa *ipa)
{
	struct ipa_smp2p *smp2p = ipa->smp2p;

	if (!smp2p->setup_ready_irq)
		return;

	mutex_lock(&smp2p->mutex);

	if (!smp2p->setup_disabled) {
		disable_irq(smp2p->setup_ready_irq);
		smp2p->setup_disabled = true;
	}

	mutex_unlock(&smp2p->mutex);
}

/* Reset state tracking whether we have notified the modem */
void ipa_smp2p_notify_reset(struct ipa *ipa)
{
	struct ipa_smp2p *smp2p = ipa->smp2p;
	u32 mask;

	if (!smp2p->notified)
		return;

	ipa_smp2p_power_release(ipa);

	/* Reset the power enabled valid flag */
	mask = BIT(smp2p->valid_bit);
	qcom_smem_state_update_bits(smp2p->valid_state, mask, 0);

	/* Mark the power disabled for good measure... */
	mask = BIT(smp2p->enabled_bit);
	qcom_smem_state_update_bits(smp2p->enabled_state, mask, 0);

	smp2p->notified = false;
}
