// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/interconnect.h>
#include <linux/io.h>

#include "msm_drv.h"

/*
 * Util/helpers:
 */

struct clk *msm_clk_bulk_get_clock(struct clk_bulk_data *bulk, int count,
		const char *name)
{
	int i;
	char n[32];

	snprintf(n, sizeof(n), "%s_clk", name);

	for (i = 0; bulk && i < count; i++) {
		if (!strcmp(bulk[i].id, name) || !strcmp(bulk[i].id, n))
			return bulk[i].clk;
	}


	return NULL;
}

struct clk *msm_clk_get(struct platform_device *pdev, const char *name)
{
	struct clk *clk;
	char name2[32];

	clk = devm_clk_get(&pdev->dev, name);
	if (!IS_ERR(clk) || PTR_ERR(clk) == -EPROBE_DEFER)
		return clk;

	snprintf(name2, sizeof(name2), "%s_clk", name);

	clk = devm_clk_get(&pdev->dev, name2);
	if (!IS_ERR(clk))
		dev_warn(&pdev->dev, "Using legacy clk name binding.  Use "
				"\"%s\" instead of \"%s\"\n", name, name2);

	return clk;
}

void __iomem *msm_ioremap_mdss(struct platform_device *mdss_pdev,
			       struct platform_device *pdev,
			       const char *name)
{
	struct resource *res;

	res = platform_get_resource_byname(mdss_pdev, IORESOURCE_MEM, name);
	if (!res)
		return ERR_PTR(-EINVAL);

	return devm_ioremap_resource(&pdev->dev, res);
}

static void __iomem *_msm_ioremap(struct platform_device *pdev, const char *name,
				  bool quiet, phys_addr_t *psize)
{
	struct resource *res;
	unsigned long size;
	void __iomem *ptr;

	if (name)
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	else
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		if (!quiet)
			DRM_DEV_ERROR(&pdev->dev, "failed to get memory resource: %s\n", name);
		return ERR_PTR(-EINVAL);
	}

	size = resource_size(res);

	ptr = devm_ioremap(&pdev->dev, res->start, size);
	if (!ptr) {
		if (!quiet)
			DRM_DEV_ERROR(&pdev->dev, "failed to ioremap: %s\n", name);
		return ERR_PTR(-ENOMEM);
	}

	if (psize)
		*psize = size;

	return ptr;
}

void __iomem *msm_ioremap(struct platform_device *pdev, const char *name)
{
	return _msm_ioremap(pdev, name, false, NULL);
}

void __iomem *msm_ioremap_quiet(struct platform_device *pdev, const char *name)
{
	return _msm_ioremap(pdev, name, true, NULL);
}

void __iomem *msm_ioremap_size(struct platform_device *pdev, const char *name,
			  phys_addr_t *psize)
{
	return _msm_ioremap(pdev, name, false, psize);
}

static enum hrtimer_restart msm_hrtimer_worktimer(struct hrtimer *t)
{
	struct msm_hrtimer_work *work = container_of(t,
			struct msm_hrtimer_work, timer);

	kthread_queue_work(work->worker, &work->work);

	return HRTIMER_NORESTART;
}

void msm_hrtimer_queue_work(struct msm_hrtimer_work *work,
			    ktime_t wakeup_time,
			    enum hrtimer_mode mode)
{
	hrtimer_start(&work->timer, wakeup_time, mode);
}

void msm_hrtimer_work_init(struct msm_hrtimer_work *work,
			   struct kthread_worker *worker,
			   kthread_work_func_t fn,
			   clockid_t clock_id,
			   enum hrtimer_mode mode)
{
	hrtimer_init(&work->timer, clock_id, mode);
	work->timer.function = msm_hrtimer_worktimer;
	work->worker = worker;
	kthread_init_work(&work->work, fn);
}

struct icc_path *msm_icc_get(struct device *dev, const char *name)
{
	struct device *mdss_dev = dev->parent;
	struct icc_path *path;

	path = of_icc_get(dev, name);
	if (path)
		return path;

	/*
	 * If there are no interconnects attached to the corresponding device
	 * node, of_icc_get() will return NULL.
	 *
	 * If the MDP5/DPU device node doesn't have interconnects, lookup the
	 * path in the parent (MDSS) device.
	 */
	return of_icc_get(mdss_dev, name);

}
