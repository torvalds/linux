/* linux/drivers/media/video/samsung/tvout/s5p_tvout_common_lib.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Common library file for SAMSUNG TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <linux/pm_runtime.h>

#include "s5p_tvout_common_lib.h"

#ifdef CONFIG_VCM
#include <plat/s5p-vcm.h>
#endif

#ifdef CONFIG_VCM
static atomic_t		s5p_tvout_vcm_usage = ATOMIC_INIT(0);

static void tvout_tlb_invalidator(enum vcm_dev_id id)
{
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	if (atomic_read(&s5p_tvout_vcm_usage) == 0) {
		return (void)0;
	}
#endif
}

static void tvout_pgd_base_specifier(enum vcm_dev_id id, unsigned long base)
{

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	if (atomic_read(&s5p_tvout_vcm_usage) == 0) {
		return (void)0;
	}
#endif
}

static struct s5p_vcm_driver s5ptv_vcm_driver = {
	.tlb_invalidator = &tvout_tlb_invalidator,
	.pgd_base_specifier = &tvout_pgd_base_specifier,
	.phys_alloc = NULL,
	.phys_free = NULL,
};

#endif


#ifdef CONFIG_VCM
static struct vcm	*s5p_vcm;

int s5p_tvout_vcm_create_unified(void)
{
	s5p_vcm = vcm_create_unified((SZ_64M), VCM_DEV_TV,
						&s5ptv_vcm_driver);

	if (IS_ERR(s5p_vcm))
		return PTR_ERR(s5p_vcm);

	return 0;
}

int s5p_tvout_vcm_init(void)
{
	if (vcm_activate(s5p_vcm) < 0)
		return -1;

	return 0;
}

void s5p_tvout_vcm_activate(void)
{
	vcm_set_pgtable_base(VCM_DEV_TV);
}

void s5p_tvout_vcm_deactivate(void)
{
}


#endif
int s5p_tvout_map_resource_mem(
		struct platform_device *pdev, char *name,
		void __iomem **base, struct resource **res)
{
	size_t		size;
	void __iomem	*tmp_base;
	struct resource	*tmp_res;

	tmp_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);

	if (!tmp_res)
		goto not_found;

	size = (tmp_res->end - tmp_res->start) + 1;

	tmp_res = request_mem_region(tmp_res->start, size, tmp_res->name);

	if (!tmp_res) {
		tvout_err("%s: fail to get memory region\n", __func__);
		goto err_on_request_mem_region;
	}

	tmp_base = ioremap(tmp_res->start, size);

	if (!tmp_base) {
		tvout_err("%s: fail to ioremap address region\n", __func__);
		goto err_on_ioremap;
	}

	*res = tmp_res;
	*base = tmp_base;
	return 0;

err_on_ioremap:
	release_resource(tmp_res);
	kfree(tmp_res);

err_on_request_mem_region:
	return -ENXIO;

not_found:
	tvout_err("%s: fail to get IORESOURCE_MEM for %s\n", __func__, name);
	return -ENODEV;
}

void s5p_tvout_unmap_resource_mem(
		void __iomem *base, struct resource *res)
{
	if (base)
		iounmap(base);

	if (res) {
		release_resource(res);
		kfree(res);
	}
}

/* Libraries for runtime PM */
static struct device *s5p_tvout_dev;

void s5p_tvout_pm_runtime_enable(struct device *dev)
{
	pm_runtime_enable(dev);

	s5p_tvout_dev = dev;
}

void s5p_tvout_pm_runtime_disable(struct device *dev)
{
	pm_runtime_disable(dev);
}

void s5p_tvout_pm_runtime_get(void)
{
	pm_runtime_get_sync(s5p_tvout_dev);

#ifdef CONFIG_VCM
	atomic_inc(&s5p_tvout_vcm_usage);
	if (atomic_read(&s5p_tvout_vcm_usage) == 1)
		s5p_tvout_vcm_activate();
#endif
}

void s5p_tvout_pm_runtime_put(void)
{
#ifdef CONFIG_VCM
	if (atomic_read(&s5p_tvout_vcm_usage) == 1)
		s5p_tvout_vcm_deactivate();

	atomic_dec(&s5p_tvout_vcm_usage);
#endif

	pm_runtime_put_sync(s5p_tvout_dev);
}
