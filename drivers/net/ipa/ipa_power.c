// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2021 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/bitops.h>

#include "linux/soc/qcom/qcom_aoss.h"

#include "ipa.h"
#include "ipa_power.h"
#include "ipa_endpoint.h"
#include "ipa_modem.h"
#include "ipa_data.h"

/**
 * DOC: IPA Power Management
 *
 * The IPA hardware is enabled when the IPA core clock and all the
 * interconnects (buses) it depends on are enabled.  Runtime power
 * management is used to determine whether the core clock and
 * interconnects are enabled, and if not in use to be suspended
 * automatically.
 *
 * The core clock currently runs at a fixed clock rate when enabled,
 * an all interconnects use a fixed average and peak bandwidth.
 */

#define IPA_AUTOSUSPEND_DELAY	500	/* milliseconds */

/**
 * struct ipa_interconnect - IPA interconnect information
 * @path:		Interconnect path
 * @average_bandwidth:	Average interconnect bandwidth (KB/second)
 * @peak_bandwidth:	Peak interconnect bandwidth (KB/second)
 */
struct ipa_interconnect {
	struct icc_path *path;
	u32 average_bandwidth;
	u32 peak_bandwidth;
};

/**
 * enum ipa_power_flag - IPA power flags
 * @IPA_POWER_FLAG_RESUMED:	Whether resume from suspend has been signaled
 * @IPA_POWER_FLAG_SYSTEM:	Hardware is system (not runtime) suspended
 * @IPA_POWER_FLAG_STOPPED:	Modem TX is disabled by ipa_start_xmit()
 * @IPA_POWER_FLAG_STARTED:	Modem TX was enabled by ipa_runtime_resume()
 * @IPA_POWER_FLAG_COUNT:	Number of defined power flags
 */
enum ipa_power_flag {
	IPA_POWER_FLAG_RESUMED,
	IPA_POWER_FLAG_SYSTEM,
	IPA_POWER_FLAG_STOPPED,
	IPA_POWER_FLAG_STARTED,
	IPA_POWER_FLAG_COUNT,		/* Last; not a flag */
};

/**
 * struct ipa_power - IPA power management information
 * @dev:		IPA device pointer
 * @core:		IPA core clock
 * @qmp:		QMP handle for AOSS communication
 * @spinlock:		Protects modem TX queue enable/disable
 * @flags:		Boolean state flags
 * @interconnect_count:	Number of elements in interconnect[]
 * @interconnect:	Interconnect array
 */
struct ipa_power {
	struct device *dev;
	struct clk *core;
	struct qmp *qmp;
	spinlock_t spinlock;	/* used with STOPPED/STARTED power flags */
	DECLARE_BITMAP(flags, IPA_POWER_FLAG_COUNT);
	u32 interconnect_count;
	struct ipa_interconnect *interconnect;
};

static int ipa_interconnect_init_one(struct device *dev,
				     struct ipa_interconnect *interconnect,
				     const struct ipa_interconnect_data *data)
{
	struct icc_path *path;

	path = of_icc_get(dev, data->name);
	if (IS_ERR(path)) {
		int ret = PTR_ERR(path);

		dev_err_probe(dev, ret, "error getting %s interconnect\n",
			      data->name);

		return ret;
	}

	interconnect->path = path;
	interconnect->average_bandwidth = data->average_bandwidth;
	interconnect->peak_bandwidth = data->peak_bandwidth;

	return 0;
}

static void ipa_interconnect_exit_one(struct ipa_interconnect *interconnect)
{
	icc_put(interconnect->path);
	memset(interconnect, 0, sizeof(*interconnect));
}

/* Initialize interconnects required for IPA operation */
static int ipa_interconnect_init(struct ipa_power *power, struct device *dev,
				 const struct ipa_interconnect_data *data)
{
	struct ipa_interconnect *interconnect;
	u32 count;
	int ret;

	count = power->interconnect_count;
	interconnect = kcalloc(count, sizeof(*interconnect), GFP_KERNEL);
	if (!interconnect)
		return -ENOMEM;
	power->interconnect = interconnect;

	while (count--) {
		ret = ipa_interconnect_init_one(dev, interconnect, data++);
		if (ret)
			goto out_unwind;
		interconnect++;
	}

	return 0;

out_unwind:
	while (interconnect-- > power->interconnect)
		ipa_interconnect_exit_one(interconnect);
	kfree(power->interconnect);
	power->interconnect = NULL;

	return ret;
}

/* Inverse of ipa_interconnect_init() */
static void ipa_interconnect_exit(struct ipa_power *power)
{
	struct ipa_interconnect *interconnect;

	interconnect = power->interconnect + power->interconnect_count;
	while (interconnect-- > power->interconnect)
		ipa_interconnect_exit_one(interconnect);
	kfree(power->interconnect);
	power->interconnect = NULL;
}

/* Currently we only use one bandwidth level, so just "enable" interconnects */
static int ipa_interconnect_enable(struct ipa *ipa)
{
	struct ipa_interconnect *interconnect;
	struct ipa_power *power = ipa->power;
	int ret;
	u32 i;

	interconnect = power->interconnect;
	for (i = 0; i < power->interconnect_count; i++) {
		ret = icc_set_bw(interconnect->path,
				 interconnect->average_bandwidth,
				 interconnect->peak_bandwidth);
		if (ret) {
			dev_err(&ipa->pdev->dev,
				"error %d enabling %s interconnect\n",
				ret, icc_get_name(interconnect->path));
			goto out_unwind;
		}
		interconnect++;
	}

	return 0;

out_unwind:
	while (interconnect-- > power->interconnect)
		(void)icc_set_bw(interconnect->path, 0, 0);

	return ret;
}

/* To disable an interconnect, we just its bandwidth to 0 */
static int ipa_interconnect_disable(struct ipa *ipa)
{
	struct ipa_interconnect *interconnect;
	struct ipa_power *power = ipa->power;
	struct device *dev = &ipa->pdev->dev;
	int result = 0;
	u32 count;
	int ret;

	count = power->interconnect_count;
	interconnect = power->interconnect + count;
	while (count--) {
		interconnect--;
		ret = icc_set_bw(interconnect->path, 0, 0);
		if (ret) {
			dev_err(dev, "error %d disabling %s interconnect\n",
				ret, icc_get_name(interconnect->path));
			/* Try to disable all; record only the first error */
			if (!result)
				result = ret;
		}
	}

	return result;
}

/* Enable IPA power, enabling interconnects and the core clock */
static int ipa_power_enable(struct ipa *ipa)
{
	int ret;

	ret = ipa_interconnect_enable(ipa);
	if (ret)
		return ret;

	ret = clk_prepare_enable(ipa->power->core);
	if (ret) {
		dev_err(&ipa->pdev->dev, "error %d enabling core clock\n", ret);
		(void)ipa_interconnect_disable(ipa);
	}

	return ret;
}

/* Inverse of ipa_power_enable() */
static int ipa_power_disable(struct ipa *ipa)
{
	clk_disable_unprepare(ipa->power->core);

	return ipa_interconnect_disable(ipa);
}

static int ipa_runtime_suspend(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	/* Endpoints aren't usable until setup is complete */
	if (ipa->setup_complete) {
		__clear_bit(IPA_POWER_FLAG_RESUMED, ipa->power->flags);
		ipa_endpoint_suspend(ipa);
		gsi_suspend(&ipa->gsi);
	}

	return ipa_power_disable(ipa);
}

static int ipa_runtime_resume(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);
	int ret;

	ret = ipa_power_enable(ipa);
	if (WARN_ON(ret < 0))
		return ret;

	/* Endpoints aren't usable until setup is complete */
	if (ipa->setup_complete) {
		gsi_resume(&ipa->gsi);
		ipa_endpoint_resume(ipa);
	}

	return 0;
}

static int ipa_suspend(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	__set_bit(IPA_POWER_FLAG_SYSTEM, ipa->power->flags);

	return pm_runtime_force_suspend(dev);
}

static int ipa_resume(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);

	__clear_bit(IPA_POWER_FLAG_SYSTEM, ipa->power->flags);

	return ret;
}

/* Return the current IPA core clock rate */
u32 ipa_core_clock_rate(struct ipa *ipa)
{
	return ipa->power ? (u32)clk_get_rate(ipa->power->core) : 0;
}

/**
 * ipa_suspend_handler() - Handle the suspend IPA interrupt
 * @ipa:	IPA pointer
 * @irq_id:	IPA interrupt type (unused)
 *
 * If an RX endpoint is suspended, and the IPA has a packet destined for
 * that endpoint, the IPA generates a SUSPEND interrupt to inform the AP
 * that it should resume the endpoint.  If we get one of these interrupts
 * we just wake up the system.
 */
static void ipa_suspend_handler(struct ipa *ipa, enum ipa_irq_id irq_id)
{
	/* To handle an IPA interrupt we will have resumed the hardware
	 * just to handle the interrupt, so we're done.  If we are in a
	 * system suspend, trigger a system resume.
	 */
	if (!__test_and_set_bit(IPA_POWER_FLAG_RESUMED, ipa->power->flags))
		if (test_bit(IPA_POWER_FLAG_SYSTEM, ipa->power->flags))
			pm_wakeup_dev_event(&ipa->pdev->dev, 0, true);

	/* Acknowledge/clear the suspend interrupt on all endpoints */
	ipa_interrupt_suspend_clear_all(ipa->interrupt);
}

/* The next few functions coordinate stopping and starting the modem
 * network device transmit queue.
 *
 * Transmit can be running concurrent with power resume, and there's a
 * chance the resume completes before the transmit path stops the queue,
 * leaving the queue in a stopped state.  The next two functions are used
 * to avoid this: ipa_power_modem_queue_stop() is used by ipa_start_xmit()
 * to conditionally stop the TX queue; and ipa_power_modem_queue_start()
 * is used by ipa_runtime_resume() to conditionally restart it.
 *
 * Two flags and a spinlock are used.  If the queue is stopped, the STOPPED
 * power flag is set.  And if the queue is started, the STARTED flag is set.
 * The queue is only started on resume if the STOPPED flag is set.  And the
 * queue is only started in ipa_start_xmit() if the STARTED flag is *not*
 * set.  As a result, the queue remains operational if the two activites
 * happen concurrently regardless of the order they complete.  The spinlock
 * ensures the flag and TX queue operations are done atomically.
 *
 * The first function stops the modem netdev transmit queue, but only if
 * the STARTED flag is *not* set.  That flag is cleared if it was set.
 * If the queue is stopped, the STOPPED flag is set.  This is called only
 * from the power ->runtime_resume operation.
 */
void ipa_power_modem_queue_stop(struct ipa *ipa)
{
	struct ipa_power *power = ipa->power;
	unsigned long flags;

	spin_lock_irqsave(&power->spinlock, flags);

	if (!__test_and_clear_bit(IPA_POWER_FLAG_STARTED, power->flags)) {
		netif_stop_queue(ipa->modem_netdev);
		__set_bit(IPA_POWER_FLAG_STOPPED, power->flags);
	}

	spin_unlock_irqrestore(&power->spinlock, flags);
}

/* This function starts the modem netdev transmit queue, but only if the
 * STOPPED flag is set.  That flag is cleared if it was set.  If the queue
 * was restarted, the STARTED flag is set; this allows ipa_start_xmit()
 * to skip stopping the queue in the event of a race.
 */
void ipa_power_modem_queue_wake(struct ipa *ipa)
{
	struct ipa_power *power = ipa->power;
	unsigned long flags;

	spin_lock_irqsave(&power->spinlock, flags);

	if (__test_and_clear_bit(IPA_POWER_FLAG_STOPPED, power->flags)) {
		__set_bit(IPA_POWER_FLAG_STARTED, power->flags);
		netif_wake_queue(ipa->modem_netdev);
	}

	spin_unlock_irqrestore(&power->spinlock, flags);
}

/* This function clears the STARTED flag once the TX queue is operating */
void ipa_power_modem_queue_active(struct ipa *ipa)
{
	clear_bit(IPA_POWER_FLAG_STARTED, ipa->power->flags);
}

static int ipa_power_retention_init(struct ipa_power *power)
{
	struct qmp *qmp = qmp_get(power->dev);

	if (IS_ERR(qmp)) {
		if (PTR_ERR(qmp) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		/* We assume any other error means it's not defined/needed */
		qmp = NULL;
	}
	power->qmp = qmp;

	return 0;
}

static void ipa_power_retention_exit(struct ipa_power *power)
{
	qmp_put(power->qmp);
	power->qmp = NULL;
}

/* Control register retention on power collapse */
void ipa_power_retention(struct ipa *ipa, bool enable)
{
	static const char fmt[] = "{ class: bcm, res: ipa_pc, val: %c }";
	struct ipa_power *power = ipa->power;
	char buf[36];	/* Exactly enough for fmt[]; size a multiple of 4 */
	int ret;

	if (!power->qmp)
		return;		/* Not needed on this platform */

	(void)snprintf(buf, sizeof(buf), fmt, enable ? '1' : '0');

	ret = qmp_send(power->qmp, buf, sizeof(buf));
	if (ret)
		dev_err(power->dev, "error %d sending QMP %sable request\n",
			ret, enable ? "en" : "dis");
}

int ipa_power_setup(struct ipa *ipa)
{
	int ret;

	ipa_interrupt_add(ipa->interrupt, IPA_IRQ_TX_SUSPEND,
			  ipa_suspend_handler);

	ret = device_init_wakeup(&ipa->pdev->dev, true);
	if (ret)
		ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_TX_SUSPEND);

	return ret;
}

void ipa_power_teardown(struct ipa *ipa)
{
	(void)device_init_wakeup(&ipa->pdev->dev, false);
	ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_TX_SUSPEND);
}

/* Initialize IPA power management */
struct ipa_power *
ipa_power_init(struct device *dev, const struct ipa_power_data *data)
{
	struct ipa_power *power;
	struct clk *clk;
	int ret;

	clk = clk_get(dev, "core");
	if (IS_ERR(clk)) {
		dev_err_probe(dev, PTR_ERR(clk), "error getting core clock\n");

		return ERR_CAST(clk);
	}

	ret = clk_set_rate(clk, data->core_clock_rate);
	if (ret) {
		dev_err(dev, "error %d setting core clock rate to %u\n",
			ret, data->core_clock_rate);
		goto err_clk_put;
	}

	power = kzalloc(sizeof(*power), GFP_KERNEL);
	if (!power) {
		ret = -ENOMEM;
		goto err_clk_put;
	}
	power->dev = dev;
	power->core = clk;
	spin_lock_init(&power->spinlock);
	power->interconnect_count = data->interconnect_count;

	ret = ipa_interconnect_init(power, dev, data->interconnect_data);
	if (ret)
		goto err_kfree;

	ret = ipa_power_retention_init(power);
	if (ret)
		goto err_interconnect_exit;

	pm_runtime_set_autosuspend_delay(dev, IPA_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	return power;

err_interconnect_exit:
	ipa_interconnect_exit(power);
err_kfree:
	kfree(power);
err_clk_put:
	clk_put(clk);

	return ERR_PTR(ret);
}

/* Inverse of ipa_power_init() */
void ipa_power_exit(struct ipa_power *power)
{
	struct device *dev = power->dev;
	struct clk *clk = power->core;

	pm_runtime_disable(dev);
	pm_runtime_dont_use_autosuspend(dev);
	ipa_power_retention_exit(power);
	ipa_interconnect_exit(power);
	kfree(power);
	clk_put(clk);
}

const struct dev_pm_ops ipa_pm_ops = {
	.suspend		= ipa_suspend,
	.resume			= ipa_resume,
	.runtime_suspend	= ipa_runtime_suspend,
	.runtime_resume		= ipa_runtime_resume,
};
