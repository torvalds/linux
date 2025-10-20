// SPDX-License-Identifier: GPL-2.0-only or MIT
// Copyright (C) 2025 Arm, Ltd.

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_utils.h>
#include <drm/drm_gem.h>
#include <drm/drm_accel.h>
#include <drm/ethosu_accel.h>

#include "ethosu_drv.h"
#include "ethosu_device.h"
#include "ethosu_gem.h"
#include "ethosu_job.h"

static int ethosu_ioctl_dev_query(struct drm_device *ddev, void *data,
				  struct drm_file *file)
{
	struct ethosu_device *ethosudev = to_ethosu_device(ddev);
	struct drm_ethosu_dev_query *args = data;

	if (!args->pointer) {
		switch (args->type) {
		case DRM_ETHOSU_DEV_QUERY_NPU_INFO:
			args->size = sizeof(ethosudev->npu_info);
			return 0;
		default:
			return -EINVAL;
		}
	}

	switch (args->type) {
	case DRM_ETHOSU_DEV_QUERY_NPU_INFO:
		if (args->size < offsetofend(struct drm_ethosu_npu_info, sram_size))
			return -EINVAL;
		return copy_struct_to_user(u64_to_user_ptr(args->pointer),
					   args->size,
					   &ethosudev->npu_info,
					   sizeof(ethosudev->npu_info), NULL);
	default:
		return -EINVAL;
	}
}

#define ETHOSU_BO_FLAGS		DRM_ETHOSU_BO_NO_MMAP

static int ethosu_ioctl_bo_create(struct drm_device *ddev, void *data,
				  struct drm_file *file)
{
	struct drm_ethosu_bo_create *args = data;
	int cookie, ret;

	if (!drm_dev_enter(ddev, &cookie))
		return -ENODEV;

	if (!args->size || (args->flags & ~ETHOSU_BO_FLAGS)) {
		ret = -EINVAL;
		goto out_dev_exit;
	}

	ret = ethosu_gem_create_with_handle(file, ddev, &args->size,
					    args->flags, &args->handle);

out_dev_exit:
	drm_dev_exit(cookie);
	return ret;
}

static int ethosu_ioctl_bo_wait(struct drm_device *ddev, void *data,
				struct drm_file *file)
{
	struct drm_ethosu_bo_wait *args = data;
	int cookie, ret;
	unsigned long timeout = drm_timeout_abs_to_jiffies(args->timeout_ns);

	if (args->pad)
		return -EINVAL;

	if (!drm_dev_enter(ddev, &cookie))
		return -ENODEV;

	ret = drm_gem_dma_resv_wait(file, args->handle, true, timeout);

	drm_dev_exit(cookie);
	return ret;
}

static int ethosu_ioctl_bo_mmap_offset(struct drm_device *ddev, void *data,
				       struct drm_file *file)
{
	struct drm_ethosu_bo_mmap_offset *args = data;
	struct drm_gem_object *obj;

	if (args->pad)
		return -EINVAL;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	args->offset = drm_vma_node_offset_addr(&obj->vma_node);
	drm_gem_object_put(obj);
	return 0;
}

static int ethosu_ioctl_cmdstream_bo_create(struct drm_device *ddev, void *data,
					    struct drm_file *file)
{
	struct drm_ethosu_cmdstream_bo_create *args = data;
	int cookie, ret;

	if (!drm_dev_enter(ddev, &cookie))
		return -ENODEV;

	if (!args->size || !args->data || args->pad || args->flags) {
		ret = -EINVAL;
		goto out_dev_exit;
	}

	args->flags |= DRM_ETHOSU_BO_NO_MMAP;

	ret = ethosu_gem_cmdstream_create(file, ddev, args->size, args->data,
					  args->flags, &args->handle);

out_dev_exit:
	drm_dev_exit(cookie);
	return ret;
}

static int ethosu_open(struct drm_device *ddev, struct drm_file *file)
{
	int ret = 0;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	struct ethosu_file_priv __free(kfree) *priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err_put_mod;
	}
	priv->edev = to_ethosu_device(ddev);

	ret = ethosu_job_open(priv);
	if (ret)
		goto err_put_mod;

	file->driver_priv = no_free_ptr(priv);
	return 0;

err_put_mod:
	module_put(THIS_MODULE);
	return ret;
}

static void ethosu_postclose(struct drm_device *ddev, struct drm_file *file)
{
	ethosu_job_close(file->driver_priv);
	kfree(file->driver_priv);
	module_put(THIS_MODULE);
}

static const struct drm_ioctl_desc ethosu_drm_driver_ioctls[] = {
#define ETHOSU_IOCTL(n, func, flags) \
	DRM_IOCTL_DEF_DRV(ETHOSU_##n, ethosu_ioctl_##func, flags)

	ETHOSU_IOCTL(DEV_QUERY, dev_query, 0),
	ETHOSU_IOCTL(BO_CREATE, bo_create, 0),
	ETHOSU_IOCTL(BO_WAIT, bo_wait, 0),
	ETHOSU_IOCTL(BO_MMAP_OFFSET, bo_mmap_offset, 0),
	ETHOSU_IOCTL(CMDSTREAM_BO_CREATE, cmdstream_bo_create, 0),
	ETHOSU_IOCTL(SUBMIT, submit, 0),
};

DEFINE_DRM_ACCEL_FOPS(ethosu_drm_driver_fops);

/*
 * Ethosu driver version:
 * - 1.0 - initial interface
 */
static const struct drm_driver ethosu_drm_driver = {
	.driver_features = DRIVER_COMPUTE_ACCEL | DRIVER_GEM,
	.open = ethosu_open,
	.postclose = ethosu_postclose,
	.ioctls = ethosu_drm_driver_ioctls,
	.num_ioctls = ARRAY_SIZE(ethosu_drm_driver_ioctls),
	.fops = &ethosu_drm_driver_fops,
	.name = "ethosu",
	.desc = "Arm Ethos-U Accel driver",
	.major = 1,
	.minor = 0,

	.gem_create_object = ethosu_gem_create_object,
};

#define U65_DRAM_AXI_LIMIT_CFG	0x1f3f0002
#define U65_SRAM_AXI_LIMIT_CFG	0x1f3f00b0
#define U85_AXI_EXT_CFG		0x00021f3f
#define U85_AXI_SRAM_CFG	0x00021f3f
#define U85_MEM_ATTR0_CFG	0x00000000
#define U85_MEM_ATTR2_CFG	0x000000b7

static int ethosu_reset(struct ethosu_device *ethosudev)
{
	int ret;
	u32 reg;

	writel_relaxed(RESET_PENDING_CSL, ethosudev->regs + NPU_REG_RESET);
	ret = readl_poll_timeout(ethosudev->regs + NPU_REG_STATUS, reg,
				 !FIELD_GET(STATUS_RESET_STATUS, reg),
				 USEC_PER_MSEC, USEC_PER_SEC);
	if (ret)
		return ret;

	if (!FIELD_GET(PROT_ACTIVE_CSL, readl_relaxed(ethosudev->regs + NPU_REG_PROT))) {
		dev_warn(ethosudev->base.dev, "Could not reset to non-secure mode (PROT = %x)\n",
			 readl_relaxed(ethosudev->regs + NPU_REG_PROT));
	}

	/*
	 * Assign region 2 (SRAM) to AXI M0 (AXILIMIT0),
	 * everything else to AXI M1 (AXILIMIT2)
	 */
	writel_relaxed(0x0000aa8a, ethosudev->regs + NPU_REG_REGIONCFG);
	if (ethosu_is_u65(ethosudev)) {
		writel_relaxed(U65_SRAM_AXI_LIMIT_CFG, ethosudev->regs + NPU_REG_AXILIMIT0);
		writel_relaxed(U65_DRAM_AXI_LIMIT_CFG, ethosudev->regs + NPU_REG_AXILIMIT2);
	} else {
		writel_relaxed(U85_AXI_SRAM_CFG, ethosudev->regs + NPU_REG_AXI_SRAM);
		writel_relaxed(U85_AXI_EXT_CFG, ethosudev->regs + NPU_REG_AXI_EXT);
		writel_relaxed(U85_MEM_ATTR0_CFG, ethosudev->regs + NPU_REG_MEM_ATTR0);	// SRAM
		writel_relaxed(U85_MEM_ATTR2_CFG, ethosudev->regs + NPU_REG_MEM_ATTR2);	// DRAM
	}

	if (ethosudev->sram)
		memset_io(ethosudev->sram, 0, ethosudev->npu_info.sram_size);

	return 0;
}

static int ethosu_device_resume(struct device *dev)
{
	struct ethosu_device *ethosudev = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(ethosudev->num_clks, ethosudev->clks);
	if (ret)
		return ret;

	ret = ethosu_reset(ethosudev);
	if (!ret)
		return 0;

	clk_bulk_disable_unprepare(ethosudev->num_clks, ethosudev->clks);
	return ret;
}

static int ethosu_device_suspend(struct device *dev)
{
	struct ethosu_device *ethosudev = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(ethosudev->num_clks, ethosudev->clks);
	return 0;
}

static int ethosu_sram_init(struct ethosu_device *ethosudev)
{
	ethosudev->npu_info.sram_size = 0;

	ethosudev->srampool = of_gen_pool_get(ethosudev->base.dev->of_node, "sram", 0);
	if (!ethosudev->srampool)
		return 0;

	ethosudev->npu_info.sram_size = gen_pool_size(ethosudev->srampool);

	ethosudev->sram = (void __iomem *)gen_pool_dma_alloc(ethosudev->srampool,
							     ethosudev->npu_info.sram_size,
							     &ethosudev->sramphys);
	if (!ethosudev->sram) {
		dev_err(ethosudev->base.dev, "failed to allocate from SRAM pool\n");
		return -ENOMEM;
	}

	return 0;
}

static int ethosu_init(struct ethosu_device *ethosudev)
{
	int ret;
	u32 id, config;

	ret = ethosu_device_resume(ethosudev->base.dev);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(ethosudev->base.dev, 50);
	pm_runtime_use_autosuspend(ethosudev->base.dev);
	ret = devm_pm_runtime_set_active_enabled(ethosudev->base.dev);
	if (ret)
		return ret;
	pm_runtime_get_noresume(ethosudev->base.dev);

	ethosudev->npu_info.id = id = readl_relaxed(ethosudev->regs + NPU_REG_ID);
	ethosudev->npu_info.config = config = readl_relaxed(ethosudev->regs + NPU_REG_CONFIG);

	ethosu_sram_init(ethosudev);

	dev_info(ethosudev->base.dev,
		 "Ethos-U NPU, arch v%ld.%ld.%ld, rev r%ldp%ld, cmd stream ver%ld, %d MACs, %dKB SRAM\n",
		 FIELD_GET(ID_ARCH_MAJOR_MASK, id),
		 FIELD_GET(ID_ARCH_MINOR_MASK, id),
		 FIELD_GET(ID_ARCH_PATCH_MASK, id),
		 FIELD_GET(ID_VER_MAJOR_MASK, id),
		 FIELD_GET(ID_VER_MINOR_MASK, id),
		 FIELD_GET(CONFIG_CMD_STREAM_VER_MASK, config),
		 1 << FIELD_GET(CONFIG_MACS_PER_CC_MASK, config),
		 ethosudev->npu_info.sram_size / 1024);

	return 0;
}

static int ethosu_probe(struct platform_device *pdev)
{
	int ret;
	struct ethosu_device *ethosudev;

	ethosudev = devm_drm_dev_alloc(&pdev->dev, &ethosu_drm_driver,
				       struct ethosu_device, base);
	if (IS_ERR(ethosudev))
		return -ENOMEM;
	platform_set_drvdata(pdev, ethosudev);

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));

	ethosudev->regs = devm_platform_ioremap_resource(pdev, 0);

	ethosudev->num_clks = devm_clk_bulk_get_all(&pdev->dev, &ethosudev->clks);
	if (ethosudev->num_clks < 0)
		return ethosudev->num_clks;

	ret = ethosu_job_init(ethosudev);
	if (ret)
		return ret;

	ret = ethosu_init(ethosudev);
	if (ret)
		return ret;

	ret = drm_dev_register(&ethosudev->base, 0);
	if (ret)
		pm_runtime_dont_use_autosuspend(ethosudev->base.dev);

	pm_runtime_put_autosuspend(ethosudev->base.dev);
	return ret;
}

static void ethosu_remove(struct platform_device *pdev)
{
	struct ethosu_device *ethosudev = dev_get_drvdata(&pdev->dev);

	drm_dev_unregister(&ethosudev->base);
	ethosu_job_fini(ethosudev);
	if (ethosudev->sram)
		gen_pool_free(ethosudev->srampool, (unsigned long)ethosudev->sram,
			      ethosudev->npu_info.sram_size);
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "arm,ethos-u65" },
	{ .compatible = "arm,ethos-u85" },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static DEFINE_RUNTIME_DEV_PM_OPS(ethosu_pm_ops,
				 ethosu_device_suspend,
				 ethosu_device_resume,
				 NULL);

static struct platform_driver ethosu_driver = {
	.probe = ethosu_probe,
	.remove = ethosu_remove,
	.driver = {
		.name = "ethosu",
		.pm = pm_ptr(&ethosu_pm_ops),
		.of_match_table = dt_match,
	},
};
module_platform_driver(ethosu_driver);

MODULE_AUTHOR("Rob Herring <robh@kernel.org>");
MODULE_DESCRIPTION("Arm Ethos-U Accel Driver");
MODULE_LICENSE("Dual MIT/GPL");
