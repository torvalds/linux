// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>

#include "ipa_smp2p.h"
#include "ipa.h"
#include "ipa_uc.h"
#include "ipa_clock.h"

/**
 * DOC: IPA SMP2P communication with the modem
 *
 * SMP2P is a primitive communication mechanism available between the AP and
 * the modem.  The IPA driver uses this for two purposes:  to enable the modem
 * to state that the GSI hardware is ready to use; and to communicate the
 * state of the IPA clock in the event of a crash.
 *
 * GSI needs to have early initialization completed before it can be used.
 * This initialization is done either by Trust Zone or by the modem.  In the
 * latter case, the modem uses an SMP2P interrupt to tell the AP IPA driver
 * when the GSI is ready to use.
 *
 * The modem is also able to inquire about the current state of the IPA
 * clock by trigging another SMP2P interrupt to the AP.  We communicate
 * whether the clock is enabled using two SMP2P state bits--one to
 * indicate the clock state (on or off), and a second to indicate the
 * clock state bit is valid.  The modem will poll the valid bit until it
 * is set, and at that time records whether the AP has the IPA clock enabled.
 *
 * Finally, if the AP kernel panics, we update the SMP2P state bits even if
 * we never receive an interrupt from the modem requesting this.
 */

/**
 * struct ipa_smp2p - IPA SMP2P information
 * @ipa:		IPA pointer
 * @valid_state:	SMEM state indicating enabled state is valid
 * @enabled_state:	SMEM state to indicate clock is enabled
 * @valid_bit:		Valid bit in 32-bit SMEM state mask
 * @enabled_bit:	Enabled bit in 32-bit SMEM state mask
 * @enabled_bit:	Enabled bit in 32-bit SMEM state mask
 * @clock_query_irq:	IPA interrupt triggered by modem for clock query
 * @setup_ready_irq:	IPA interrupt triggered by modem to signal GSI ready
 * @clock_on:		Whether IPA clock is on
 * @notified:		Whether modem has been notified of clock state
 * @disabled:		Whether setup ready interrupt handling is disabled
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
	bool clock_on;
	bool notified;
	bool disabled;
	struct mutex mutex;
	struct notifier_block panic_notifier;
};

/**
 * ipa_smp2p_notify() - use SMP2P to tell modem about IPA clock state
 * @smp2p:	SMP2P information
 *
 * This is called either when the modem has requested it (by triggering
 * the modem clock query IPA interrupt) or whenever the AP is shutting down
 * (via a panic notifier).  It sets the two SMP2P state bits--one saying
 * whether the IPA clock is running, and the other indicating the first bit
 * is valid.
 */
static void ipa_smp2p_notify(struct ipa_smp2p *smp2p)
{
	u32 value;
	u32 mask;

	if (smp2p->notified)
		return;

	smp2p->clock_on = ipa_clock_get_additional(smp2p->ipa);

	/* Signal whether the clock is enabled */
	mask = BIT(smp2p->enabled_bit);
	value = smp2p->clock_on ? mask : 0;
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

	if (smp2p->clock_on)
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

	mutex_lock(&smp2p->mutex);

	if (!smp2p->disabled) {
		int ret;

		ret = ipa_setup(smp2p->ipa);
		if (ret)
			dev_err(&smp2p->ipa->pdev->dev,
				"error %d from ipa_setup()\n", ret);
		smp2p->disabled = true;
	}

	mutex_unlock(&smp2p->mutex);

	return IRQ_HANDLED;
}

/* Initialize SMP2P interrupts */
static int ipa_smp2p_irq_init(struct ipa_smp2p *smp2p, const char *name,
			      irq_handler_t handler)
{
	struct device *dev = &smp2p->ipa->pdev->dev;
	unsigned int irq;
	int ret;

	ret = platform_get_irq_byname(smp2p->ipa->pdev, name);
	if (ret <= 0) {
		dev_err(dev, "DT error %d getting \"%s\" IRQ property\n",
			ret, name);
		return ret ? : -EINVAL;
	}
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

/* Drop the clock reference if it was taken in ipa_smp2p_notify() */
static void ipa_smp2p_clock_release(struct ipa *ipa)
{
	if (!ipa->smp2p->clock_on)
		return;

	ipa_clock_put(ipa);
	ipa->smp2p->clock_on = false;
}

/* Initialize the IPA SMP2P subsystem */
int ipa_smp2p_init(struct ipa *ipa, bool modem_init)
{
	struct qcom_smem_state *enabled_state;
	struct device *dev = &ipa->pdev->dev;
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

	/* These fields are needed by the clock query interrupt
	 * handler, so initialize them now.
	 */
	mutex_init(&smp2p->mutex);
	smp2p->valid_state = valid_state;
	smp2p->valid_bit = valid_bit;
	smp2p->enabled_state = enabled_state;
	smp2p->enabled_bit = enabled_bit;

	/* We have enough information saved to handle notifications */
	ipa->smp2p = smp2p;

	ret = ipa_smp2p_irq_init(smp2p, "ipa-clock-query",
				 ipa_smp2p_modem_clk_query_isr);
	if (ret < 0)
		goto err_null_smp2p;
	smp2p->clock_query_irq = ret;

	ret = ipa_smp2p_panic_notifier_register(smp2p);
	if (ret)
		goto err_irq_exit;

	if (modem_init) {
		/* Result will be non-zero (negative for error) */
		ret = ipa_smp2p_irq_init(smp2p, "ipa-setup-ready",
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
	/* We won't get notified any more; drop clock reference (if any) */
	ipa_smp2p_clock_release(ipa);
	ipa->smp2p = NULL;
	mutex_destroy(&smp2p->mutex);
	kfree(smp2p);
}

void ipa_smp2p_disable(struct ipa *ipa)
{
	struct ipa_smp2p *smp2p = ipa->smp2p;

	if (!smp2p->setup_ready_irq)
		return;

	mutex_lock(&smp2p->mutex);

	smp2p->disabled = true;

	mutex_unlock(&smp2p->mutex);
}

/* Reset state tracking whether we have notified the modem */
void ipa_smp2p_notify_reset(struct ipa *ipa)
{
	struct ipa_smp2p *smp2p = ipa->smp2p;
	u32 mask;

	if (!smp2p->notified)
		return;

	ipa_smp2p_clock_release(ipa);

	/* Reset the clock enabled valid flag */
	mask = BIT(smp2p->valid_bit);
	qcom_smem_state_update_bits(smp2p->valid_state, mask, 0);

	/* Mark the clock disabled for good measure... */
	mask = BIT(smp2p->enabled_bit);
	qcom_smem_state_update_bits(smp2p->enabled_state, mask, 0);

	smp2p->notified = false;
}
