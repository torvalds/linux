// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2021 Linaro Ltd.
 */

#include <linux/refcount.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/bitops.h>

#include "ipa.h"
#include "ipa_clock.h"
#include "ipa_endpoint.h"
#include "ipa_modem.h"
#include "ipa_data.h"

/**
 * DOC: IPA Clocking
 *
 * The "IPA Clock" manages both the IPA core clock and the interconnects
 * (buses) the IPA depends on as a single logical entity.  A reference count
 * is incremented by "get" operations and decremented by "put" operations.
 * Transitions of that count from 0 to 1 result in the clock and interconnects
 * being enabled, and transitions of the count from 1 to 0 cause them to be
 * disabled.  We currently operate the core clock at a fixed clock rate, and
 * all buses at a fixed average and peak bandwidth.  As more advanced IPA
 * features are enabled, we can make better use of clock and bus scaling.
 *
 * An IPA clock reference must be held for any access to IPA hardware.
 */

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
 * @IPA_POWER_FLAG_COUNT:	Number of defined power flags
 */
enum ipa_power_flag {
	IPA_POWER_FLAG_RESUMED,
	IPA_POWER_FLAG_COUNT,		/* Last; not a flag */
};

/**
 * struct ipa_clock - IPA clocking information
 * @count:		Clocking reference count
 * @mutex:		Protects clock enable/disable
 * @core:		IPA core clock
 * @flags:		Boolean state flags
 * @interconnect_count:	Number of elements in interconnect[]
 * @interconnect:	Interconnect array
 */
struct ipa_clock {
	refcount_t count;
	struct mutex mutex; /* protects clock enable/disable */
	struct clk *core;
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
static int ipa_interconnect_init(struct ipa_clock *clock, struct device *dev,
				 const struct ipa_interconnect_data *data)
{
	struct ipa_interconnect *interconnect;
	u32 count;
	int ret;

	count = clock->interconnect_count;
	interconnect = kcalloc(count, sizeof(*interconnect), GFP_KERNEL);
	if (!interconnect)
		return -ENOMEM;
	clock->interconnect = interconnect;

	while (count--) {
		ret = ipa_interconnect_init_one(dev, interconnect, data++);
		if (ret)
			goto out_unwind;
		interconnect++;
	}

	return 0;

out_unwind:
	while (interconnect-- > clock->interconnect)
		ipa_interconnect_exit_one(interconnect);
	kfree(clock->interconnect);
	clock->interconnect = NULL;

	return ret;
}

/* Inverse of ipa_interconnect_init() */
static void ipa_interconnect_exit(struct ipa_clock *clock)
{
	struct ipa_interconnect *interconnect;

	interconnect = clock->interconnect + clock->interconnect_count;
	while (interconnect-- > clock->interconnect)
		ipa_interconnect_exit_one(interconnect);
	kfree(clock->interconnect);
	clock->interconnect = NULL;
}

/* Currently we only use one bandwidth level, so just "enable" interconnects */
static int ipa_interconnect_enable(struct ipa *ipa)
{
	struct ipa_interconnect *interconnect;
	struct ipa_clock *clock = ipa->clock;
	int ret;
	u32 i;

	interconnect = clock->interconnect;
	for (i = 0; i < clock->interconnect_count; i++) {
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
	while (interconnect-- > clock->interconnect)
		(void)icc_set_bw(interconnect->path, 0, 0);

	return ret;
}

/* To disable an interconnect, we just its bandwidth to 0 */
static int ipa_interconnect_disable(struct ipa *ipa)
{
	struct ipa_interconnect *interconnect;
	struct ipa_clock *clock = ipa->clock;
	struct device *dev = &ipa->pdev->dev;
	int result = 0;
	u32 count;
	int ret;

	count = clock->interconnect_count;
	interconnect = clock->interconnect + count;
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

/* Turn on IPA clocks, including interconnects */
static int ipa_clock_enable(struct ipa *ipa)
{
	int ret;

	ret = ipa_interconnect_enable(ipa);
	if (ret)
		return ret;

	ret = clk_prepare_enable(ipa->clock->core);
	if (ret) {
		dev_err(&ipa->pdev->dev, "error %d enabling core clock\n", ret);
		(void)ipa_interconnect_disable(ipa);
	}

	return ret;
}

/* Inverse of ipa_clock_enable() */
static int ipa_clock_disable(struct ipa *ipa)
{
	clk_disable_unprepare(ipa->clock->core);

	return ipa_interconnect_disable(ipa);
}

static int ipa_runtime_suspend(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	/* Endpoints aren't usable until setup is complete */
	if (ipa->setup_complete) {
		__clear_bit(IPA_POWER_FLAG_RESUMED, ipa->clock->flags);
		ipa_endpoint_suspend(ipa);
		gsi_suspend(&ipa->gsi);
	}

	return ipa_clock_disable(ipa);
}

static int ipa_runtime_resume(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);
	int ret;

	ret = ipa_clock_enable(ipa);
	if (WARN_ON(ret < 0))
		return ret;

	/* Endpoints aren't usable until setup is complete */
	if (ipa->setup_complete) {
		gsi_resume(&ipa->gsi);
		ipa_endpoint_resume(ipa);
	}

	return 0;
}

/* Get an IPA clock reference, but only if the reference count is
 * already non-zero.  Returns true if the additional reference was
 * added successfully, or false otherwise.
 */
bool ipa_clock_get_additional(struct ipa *ipa)
{
	return refcount_inc_not_zero(&ipa->clock->count);
}

/* Get an IPA clock reference.  If the reference count is non-zero, it is
 * incremented and return is immediate.  Otherwise it is checked again
 * under protection of the mutex, and if appropriate the IPA clock
 * is enabled.
 *
 * Incrementing the reference count is intentionally deferred until
 * after the clock is running and endpoints are resumed.
 */
int ipa_clock_get(struct ipa *ipa)
{
	struct ipa_clock *clock = ipa->clock;
	int ret;

	/* If the clock is running, just bump the reference count */
	if (ipa_clock_get_additional(ipa))
		return 1;

	/* Otherwise get the mutex and check again */
	mutex_lock(&clock->mutex);

	/* A reference might have been added before we got the mutex. */
	if (ipa_clock_get_additional(ipa)) {
		ret = 1;
		goto out_mutex_unlock;
	}

	ret = ipa_runtime_resume(&ipa->pdev->dev);

	refcount_set(&clock->count, 1);

out_mutex_unlock:
	mutex_unlock(&clock->mutex);

	return ret;
}

/* Attempt to remove an IPA clock reference.  If this represents the
 * last reference, disable the IPA clock under protection of the mutex.
 */
int ipa_clock_put(struct ipa *ipa)
{
	struct ipa_clock *clock = ipa->clock;
	int ret;

	/* If this is not the last reference there's nothing more to do */
	if (!refcount_dec_and_mutex_lock(&clock->count, &clock->mutex))
		return 0;

	ret = ipa_runtime_suspend(&ipa->pdev->dev);

	mutex_unlock(&clock->mutex);

	return ret;
}

/* Return the current IPA core clock rate */
u32 ipa_clock_rate(struct ipa *ipa)
{
	return ipa->clock ? (u32)clk_get_rate(ipa->clock->core) : 0;
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
	/* Just report the event, and let system resume handle the rest.
	 * More than one endpoint could signal this; if so, ignore
	 * all but the first.
	 */
	if (!test_and_set_bit(IPA_POWER_FLAG_RESUMED, ipa->clock->flags))
		pm_wakeup_dev_event(&ipa->pdev->dev, 0, true);

	/* Acknowledge/clear the suspend interrupt on all endpoints */
	ipa_interrupt_suspend_clear_all(ipa->interrupt);
}

void ipa_power_setup(struct ipa *ipa)
{
	ipa_interrupt_add(ipa->interrupt, IPA_IRQ_TX_SUSPEND,
			  ipa_suspend_handler);
}

void ipa_power_teardown(struct ipa *ipa)
{
	ipa_interrupt_remove(ipa->interrupt, IPA_IRQ_TX_SUSPEND);
}

/* Initialize IPA clocking */
struct ipa_clock *
ipa_clock_init(struct device *dev, const struct ipa_clock_data *data)
{
	struct ipa_clock *clock;
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

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock) {
		ret = -ENOMEM;
		goto err_clk_put;
	}
	clock->core = clk;
	clock->interconnect_count = data->interconnect_count;

	ret = ipa_interconnect_init(clock, dev, data->interconnect_data);
	if (ret)
		goto err_kfree;

	mutex_init(&clock->mutex);
	refcount_set(&clock->count, 0);

	return clock;

err_kfree:
	kfree(clock);
err_clk_put:
	clk_put(clk);

	return ERR_PTR(ret);
}

/* Inverse of ipa_clock_init() */
void ipa_clock_exit(struct ipa_clock *clock)
{
	struct clk *clk = clock->core;

	WARN_ON(refcount_read(&clock->count) != 0);
	mutex_destroy(&clock->mutex);
	ipa_interconnect_exit(clock);
	kfree(clock);
	clk_put(clk);
}

/**
 * ipa_suspend() - Power management system suspend callback
 * @dev:	IPA device structure
 *
 * Return:	0 on success, or a negative error code
 *
 * Called by the PM framework when a system suspend operation is invoked.
 * Suspends endpoints and releases the clock reference held to keep
 * the IPA clock running until this point.
 */
static int ipa_suspend(struct device *dev)
{
	return ipa_runtime_suspend(dev);
}

/**
 * ipa_resume() - Power management system resume callback
 * @dev:	IPA device structure
 *
 * Return:	0 on success, or a negative error code
 *
 * Called by the PM framework when a system resume operation is invoked.
 * Takes an IPA clock reference to keep the clock running until suspend,
 * and resumes endpoints.
 */
static int ipa_resume(struct device *dev)
{
	return ipa_runtime_resume(dev);
}

const struct dev_pm_ops ipa_pm_ops = {
	.suspend	= ipa_suspend,
	.resume		= ipa_resume,
};
