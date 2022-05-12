// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/interconnect.h>
#include <linux/of_platform.h>
#include <linux/iopoll.h>
#include "arm-smmu.h"

#define ARM_SMMU_ICC_AVG_BW		0
#define ARM_SMMU_ICC_PEAK_BW_HIGH	1000
#define ARM_SMMU_ICC_PEAK_BW_LOW	0
#define ARM_SMMU_ICC_ACTIVE_ONLY_TAG	0x3

/*
 * Theoretically, our interconnect does not guarantee the order between
 * writes to different "register blocks" even with device memory type.
 * It does guarantee that the completion of a read to a particular
 * register block implies that previously issued writes to that
 * register block have completed, with device memory type.
 *
 * In particular, we need to ensure that writes to iommu registers
 * complete before we turn off the power.
 */
static void arm_smmu_arch_write_sync(struct arm_smmu_device *smmu)
{
	u32 id;

	if (!smmu)
		return;

	/* Read to complete prior write transcations */
	id = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID0);

	/* Wait for read to complete before off */
	rmb();
}

static int arm_smmu_prepare_clocks(struct arm_smmu_power_resources *pwr)
{

	int i, ret = 0;

	for (i = 0; i < pwr->num_clocks; ++i) {
		ret = clk_prepare(pwr->clocks[i]);
		if (ret) {
			dev_err(pwr->dev, "Couldn't prepare clock #%d\n", i);
			while (i--)
				clk_unprepare(pwr->clocks[i]);
			break;
		}
	}
	return ret;
}

static void arm_smmu_unprepare_clocks(struct arm_smmu_power_resources *pwr)
{
	int i;

	for (i = pwr->num_clocks; i; --i)
		clk_unprepare(pwr->clocks[i - 1]);
}

static int arm_smmu_enable_clocks(struct arm_smmu_power_resources *pwr)
{
	int i, ret = 0;

	for (i = 0; i < pwr->num_clocks; ++i) {
		ret = clk_enable(pwr->clocks[i]);
		if (ret) {
			dev_err(pwr->dev, "Couldn't enable clock #%d\n", i);
			while (i--)
				clk_disable(pwr->clocks[i]);
			break;
		}
	}

	return ret;
}

static void arm_smmu_disable_clocks(struct arm_smmu_power_resources *pwr)
{
	int i;

	for (i = pwr->num_clocks; i; --i)
		clk_disable(pwr->clocks[i - 1]);
}

static int arm_smmu_raise_interconnect_bw(struct arm_smmu_power_resources *pwr)
{
	if (!pwr->icc_path)
		return 0;
	return icc_set_bw(pwr->icc_path, ARM_SMMU_ICC_AVG_BW,
			  ARM_SMMU_ICC_PEAK_BW_HIGH);
}

static void arm_smmu_lower_interconnect_bw(struct arm_smmu_power_resources *pwr)
{
	if (!pwr->icc_path)
		return;
	WARN_ON(icc_set_bw(pwr->icc_path, ARM_SMMU_ICC_AVG_BW,
			   ARM_SMMU_ICC_PEAK_BW_LOW));
}

static int arm_smmu_enable_regulators(struct arm_smmu_power_resources *pwr)
{
	struct regulator_bulk_data *consumers;
	int num_consumers, ret;
	int i;

	num_consumers = pwr->num_gdscs;
	consumers = pwr->gdscs;
	for (i = 0; i < num_consumers; i++) {
		ret = regulator_enable(consumers[i].consumer);
		if (ret)
			goto out;
	}
	return 0;

out:
	i -= 1;
	for (; i >= 0; i--)
		regulator_disable(consumers[i].consumer);
	return ret;
}

int arm_smmu_power_on(struct arm_smmu_power_resources *pwr)
{
	int ret;

	mutex_lock(&pwr->power_lock);
	if (pwr->power_count > 0) {
		pwr->power_count += 1;
		mutex_unlock(&pwr->power_lock);
		return 0;
	}

	ret = arm_smmu_raise_interconnect_bw(pwr);
	if (ret)
		goto out_unlock;

	ret = arm_smmu_enable_regulators(pwr);
	if (ret)
		goto out_disable_bus;

	ret = arm_smmu_prepare_clocks(pwr);
	if (ret)
		goto out_disable_regulators;

	ret = arm_smmu_enable_clocks(pwr);
	if (ret)
		goto out_unprepare_clocks;

	if (pwr->resume) {
		ret = pwr->resume(pwr);
		if (ret)
			goto out_disable_clocks;
	}

	pwr->power_count = 1;
	mutex_unlock(&pwr->power_lock);
	return 0;
out_disable_clocks:
	arm_smmu_disable_clocks(pwr);
out_unprepare_clocks:
	arm_smmu_unprepare_clocks(pwr);
out_disable_regulators:
	regulator_bulk_disable(pwr->num_gdscs, pwr->gdscs);
out_disable_bus:
	arm_smmu_lower_interconnect_bw(pwr);
out_unlock:
	mutex_unlock(&pwr->power_lock);
	return ret;
}

/*
 * Needing to pass smmu to this api for arm_smmu_arch_write_sync is awkward.
 */
void arm_smmu_power_off(struct arm_smmu_device *smmu,
			struct arm_smmu_power_resources *pwr)
{
	mutex_lock(&pwr->power_lock);
	if (pwr->power_count == 0) {
		WARN(1, "%s: Bad power count\n", dev_name(pwr->dev));
		mutex_unlock(&pwr->power_lock);
		return;

	} else if (pwr->power_count > 1) {
		pwr->power_count--;
		mutex_unlock(&pwr->power_lock);
		return;
	}

	if (pwr->suspend)
		pwr->suspend(pwr);

	arm_smmu_arch_write_sync(smmu);
	arm_smmu_disable_clocks(pwr);
	arm_smmu_unprepare_clocks(pwr);
	regulator_bulk_disable(pwr->num_gdscs, pwr->gdscs);
	arm_smmu_lower_interconnect_bw(pwr);
	pwr->power_count = 0;
	mutex_unlock(&pwr->power_lock);
}

static int arm_smmu_init_clocks(struct arm_smmu_power_resources *pwr)
{
	const char *cname;
	struct property *prop;
	int i;
	struct device *dev = pwr->dev;

	pwr->num_clocks =
		of_property_count_strings(dev->of_node, "clock-names");

	if (pwr->num_clocks < 1) {
		pwr->num_clocks = 0;
		return 0;
	}

	pwr->clocks = devm_kzalloc(
		dev, sizeof(*pwr->clocks) * pwr->num_clocks,
		GFP_KERNEL);

	if (!pwr->clocks)
		return -ENOMEM;

	i = 0;
	of_property_for_each_string(dev->of_node, "clock-names",
				prop, cname) {
		struct clk *c = devm_clk_get(dev, cname);

		if (IS_ERR(c)) {
			dev_err(dev, "Couldn't get clock: %s\n",
				cname);
			return PTR_ERR(c);
		}

		if (clk_get_rate(c) == 0) {
			long rate = clk_round_rate(c, 1000);

			clk_set_rate(c, rate);
		}

		pwr->clocks[i] = c;

		++i;
	}
	return 0;
}

static int arm_smmu_init_regulators(struct arm_smmu_power_resources *pwr)
{
	const char *cname;
	struct property *prop;
	int i;
	struct device *dev = pwr->dev;

	pwr->num_gdscs =
		of_property_count_strings(dev->of_node, "qcom,regulator-names");

	if (pwr->num_gdscs < 1) {
		pwr->num_gdscs = 0;
		return 0;
	}

	pwr->gdscs = devm_kzalloc(
			dev, sizeof(*pwr->gdscs) * pwr->num_gdscs, GFP_KERNEL);

	if (!pwr->gdscs)
		return -ENOMEM;

	i = 0;
	of_property_for_each_string(dev->of_node, "qcom,regulator-names",
				prop, cname)
		pwr->gdscs[i++].supply = cname;

	return devm_regulator_bulk_get(dev, pwr->num_gdscs, pwr->gdscs);
}

static int arm_smmu_init_interconnect(struct arm_smmu_power_resources *pwr)
{
	struct device *dev = pwr->dev;

	/* We don't want the interconnect APIs to print an error message */
	if (!of_find_property(dev->of_node, "interconnects", NULL)) {
		dev_dbg(dev, "No interconnect info\n");
		return 0;
	}

	pwr->icc_path = devm_of_icc_get(dev, NULL);
	if (IS_ERR_OR_NULL(pwr->icc_path)) {
		if (PTR_ERR(pwr->icc_path) != -EPROBE_DEFER)
			dev_err(dev, "Unable to read interconnect path from devicetree rc: %ld\n",
				PTR_ERR(pwr->icc_path));
		return pwr->icc_path ? PTR_ERR(pwr->icc_path) : -EINVAL;
	}

	if (of_property_read_bool(dev->of_node, "qcom,active-only"))
		icc_set_tag(pwr->icc_path, ARM_SMMU_ICC_ACTIVE_ONLY_TAG);

	return 0;
}

/*
 * Cleanup done by devm. Any non-devm resources must clean up themselves.
 */
struct arm_smmu_power_resources *arm_smmu_init_power_resources(
						struct device *dev)
{
	struct arm_smmu_power_resources *pwr;
	int ret;

	pwr = devm_kzalloc(dev, sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return ERR_PTR(-ENOMEM);

	pwr->dev = dev;
	mutex_init(&pwr->power_lock);

	ret = arm_smmu_init_clocks(pwr);
	if (ret)
		return ERR_PTR(ret);

	ret = arm_smmu_init_regulators(pwr);
	if (ret)
		return ERR_PTR(ret);

	ret = arm_smmu_init_interconnect(pwr);
	if (ret)
		return ERR_PTR(ret);

	return pwr;
}
