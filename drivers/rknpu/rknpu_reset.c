// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/iommu.h>

#include "rknpu_reset.h"

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

int rknpu_reset_get(struct rknpu_device *rknpu_dev)
{
	struct reset_control *srst_a = NULL;
	struct reset_control *srst_h = NULL;

	srst_a = rknpu_reset_control_get(rknpu_dev->dev, "srst_a");
	if (IS_ERR(srst_a))
		return PTR_ERR(srst_a);

	rknpu_dev->srst_a = srst_a;

	srst_h = devm_reset_control_get(rknpu_dev->dev, "srst_h");
	if (IS_ERR(srst_h))
		return PTR_ERR(srst_h);

	rknpu_dev->srst_h = srst_h;

	return 0;
}

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

int rknpu_soft_reset(struct rknpu_device *rknpu_dev)
{
	struct iommu_domain *domain = NULL;
	int ret = -EINVAL;

	if (rknpu_dev->bypass_soft_reset) {
		LOG_WARN("bypass soft reset\n");
		return 0;
	}

	LOG_INFO("soft reset\n");

	ret = rknpu_reset_assert(rknpu_dev->srst_a);
	ret |= rknpu_reset_assert(rknpu_dev->srst_h);

	udelay(10);

	ret |= rknpu_reset_deassert(rknpu_dev->srst_a);
	ret |= rknpu_reset_deassert(rknpu_dev->srst_h);

	if (ret) {
		LOG_DEV_ERROR(rknpu_dev->dev,
			      "failed to soft reset for rknpu: %d\n", ret);
		return ret;
	}

	if (rknpu_dev->iommu_en)
		domain = iommu_get_domain_for_dev(rknpu_dev->dev);

	if (domain) {
		iommu_detach_device(domain, rknpu_dev->dev);
		iommu_attach_device(domain, rknpu_dev->dev);
	}

	return ret;
}
