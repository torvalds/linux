// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include "linux/soc/qcom/qcom_aoss.h"

#include "ipa.h"
#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_interrupt.h"
#include "ipa_modem.h"
#include "ipa_power.h"

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
 * struct ipa_power - IPA power management information
 * @dev:		IPA device pointer
 * @core:		IPA core clock
 * @qmp:		QMP handle for AOSS communication
 * @interconnect_count:	Number of elements in interconnect[]
 * @interconnect:	Interconnect array
 */
struct ipa_power {
	struct device *dev;
	struct clk *core;
	struct qmp *qmp;
	u32 interconnect_count;
	struct icc_bulk_data interconnect[] __counted_by(interconnect_count);
};

/* Initialize interconnects required for IPA operation */
static int ipa_interconnect_init(struct ipa_power *power,
				 const struct ipa_interconnect_data *data)
{
	struct icc_bulk_data *interconnect;
	int ret;
	u32 i;

	/* Initialize our interconnect data array for bulk operations */
	interconnect = &power->interconnect[0];
	for (i = 0; i < power->interconnect_count; i++) {
		/* interconnect->path is filled in by of_icc_bulk_get() */
		interconnect->name = data->name;
		interconnect->avg_bw = data->average_bandwidth;
		interconnect->peak_bw = data->peak_bandwidth;
		data++;
		interconnect++;
	}

	ret = of_icc_bulk_get(power->dev, power->interconnect_count,
			      power->interconnect);
	if (ret)
		return ret;

	/* All interconnects are initially disabled */
	icc_bulk_disable(power->interconnect_count, power->interconnect);

	/* Set the bandwidth values to be used when enabled */
	ret = icc_bulk_set_bw(power->interconnect_count, power->interconnect);
	if (ret)
		icc_bulk_put(power->interconnect_count, power->interconnect);

	return ret;
}

/* Inverse of ipa_interconnect_init() */
static void ipa_interconnect_exit(struct ipa_power *power)
{
	icc_bulk_put(power->interconnect_count, power->interconnect);
}

/* Enable IPA power, enabling interconnects and the core clock */
static int ipa_power_enable(struct ipa *ipa)
{
	struct ipa_power *power = ipa->power;
	int ret;

	ret = icc_bulk_enable(power->interconnect_count, power->interconnect);
	if (ret)
		return ret;

	ret = clk_prepare_enable(power->core);
	if (ret) {
		dev_err(power->dev, "error %d enabling core clock\n", ret);
		icc_bulk_disable(power->interconnect_count,
				 power->interconnect);
	}

	return ret;
}

/* Inverse of ipa_power_enable() */
static void ipa_power_disable(struct ipa *ipa)
{
	struct ipa_power *power = ipa->power;

	clk_disable_unprepare(power->core);

	icc_bulk_disable(power->interconnect_count, power->interconnect);
}

static int ipa_runtime_suspend(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	/* Endpoints aren't usable until setup is complete */
	if (ipa->setup_complete) {
		ipa_endpoint_suspend(ipa);
		gsi_suspend(&ipa->gsi);
	}

	ipa_power_disable(ipa);

	return 0;
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

	/* Increment the disable depth to ensure that the IRQ won't
	 * be re-enabled until the matching _enable call in
	 * ipa_resume(). We do this to ensure that the interrupt
	 * handler won't run whilst PM runtime is disabled.
	 *
	 * Note that disabling the IRQ is NOT the same as disabling
	 * irq wake. If wakeup is enabled for the IPA then the IRQ
	 * will still cause the system to wake up, see irq_set_irq_wake().
	 */
	ipa_interrupt_irq_disable(ipa);

	return pm_runtime_force_suspend(dev);
}

static int ipa_resume(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);

	/* Now that PM runtime is enabled again it's safe
	 * to turn the IRQ back on and process any data
	 * that was received during suspend.
	 */
	ipa_interrupt_irq_enable(ipa);

	return ret;
}

/* Return the current IPA core clock rate */
u32 ipa_core_clock_rate(struct ipa *ipa)
{
	return ipa->power ? (u32)clk_get_rate(ipa->power->core) : 0;
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
	int ret;

	if (!power->qmp)
		return;		/* Not needed on this platform */

	ret = qmp_send(power->qmp, fmt, enable ? '1' : '0');
	if (ret)
		dev_err(power->dev, "error %d sending QMP %sable request\n",
			ret, enable ? "en" : "dis");
}

/* Initialize IPA power management */
struct ipa_power *
ipa_power_init(struct device *dev, const struct ipa_power_data *data)
{
	struct ipa_power *power;
	struct clk *clk;
	size_t size;
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

	size = struct_size(power, interconnect, data->interconnect_count);
	power = kzalloc(size, GFP_KERNEL);
	if (!power) {
		ret = -ENOMEM;
		goto err_clk_put;
	}
	power->dev = dev;
	power->core = clk;
	power->interconnect_count = data->interconnect_count;

	ret = ipa_interconnect_init(power, data->interconnect_data);
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
