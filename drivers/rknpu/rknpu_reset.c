// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/iommu.h>

#include "rknpu_reset.h"

#ifndef FPGA_PLATFORM
static inline struct reset_control *rknpu_reset_control_get(struct device *dev,
							    const char *name)
{
	struct reset_control *rst = NULL;

	rst = devm_reset_control_get(dev, name);
	if (IS_ERR(rst))
		LOG_DEV_ERROR(dev,
			      "failed to get rknpu reset control: %s, %ld\n",
			      name, PTR_ERR(rst));

	return rst;
}
#endif

int rknpu_reset_get(struct rknpu_device *rknpu_dev)
{
#ifndef FPGA_PLATFORM
	struct reset_control *srst_a = NULL;
	struct reset_control *srst_h = NULL;
	int i = 0;

	for (i = 0; i < rknpu_dev->config->num_resets; i++) {
		srst_a = rknpu_reset_control_get(
			rknpu_dev->dev,
			rknpu_dev->config->resets[i].srst_a_name);
		if (IS_ERR(srst_a))
			return PTR_ERR(srst_a);

		rknpu_dev->srst_a[i] = srst_a;

		srst_h = rknpu_reset_control_get(
			rknpu_dev->dev,
			rknpu_dev->config->resets[i].srst_h_name);
		if (IS_ERR(srst_h))
			return PTR_ERR(srst_h);

		rknpu_dev->srst_h[i] = srst_h;
	}
#endif

	return 0;
}

#ifndef FPGA_PLATFORM
static int rknpu_reset_assert(struct reset_control *rst)
{
	int ret = -EINVAL;

	if (!rst)
		return -EINVAL;

	ret = reset_control_assert(rst);
	if (ret < 0) {
		LOG_ERROR("failed to assert rknpu reset: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rknpu_reset_deassert(struct reset_control *rst)
{
	int ret = -EINVAL;

	if (!rst)
		return -EINVAL;

	ret = reset_control_deassert(rst);
	if (ret < 0) {
		LOG_ERROR("failed to deassert rknpu reset: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

int rknpu_soft_reset(struct rknpu_device *rknpu_dev)
{
#ifndef FPGA_PLATFORM
	struct iommu_domain *domain = NULL;
	struct rknpu_subcore_data *subcore_data = NULL;
	int ret = -EINVAL, i = 0;

	if (rknpu_dev->bypass_soft_reset) {
		LOG_WARN("bypass soft reset\n");
		return 0;
	}

	if (!mutex_trylock(&rknpu_dev->reset_lock))
		return 0;

	rknpu_dev->soft_reseting = true;

	msleep(100);

	for (i = 0; i < rknpu_dev->config->num_irqs; ++i) {
		subcore_data = &rknpu_dev->subcore_datas[i];
		wake_up(&subcore_data->job_done_wq);
	}

	LOG_INFO("soft reset\n");

	for (i = 0; i < rknpu_dev->config->num_resets; i++) {
		ret = rknpu_reset_assert(rknpu_dev->srst_a[i]);
		ret |= rknpu_reset_assert(rknpu_dev->srst_h[i]);

		udelay(10);

		ret |= rknpu_reset_deassert(rknpu_dev->srst_a[i]);
		ret |= rknpu_reset_deassert(rknpu_dev->srst_h[i]);
	}

	if (ret) {
		LOG_DEV_ERROR(rknpu_dev->dev,
			      "failed to soft reset for rknpu: %d\n", ret);
		mutex_unlock(&rknpu_dev->reset_lock);
		return ret;
	}

	if (rknpu_dev->iommu_en)
		domain = iommu_get_domain_for_dev(rknpu_dev->dev);

	if (domain) {
		iommu_detach_device(domain, rknpu_dev->dev);
		iommu_attach_device(domain, rknpu_dev->dev);
	}

	rknpu_dev->soft_reseting = false;

	mutex_unlock(&rknpu_dev->reset_lock);
#endif

	return 0;
}
