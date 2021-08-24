// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/devfreq_cooling.h>

#include <drm/drm_device.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_file.h>
#include <drm/drm_drv.h>

#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>
#include <soc/rockchip/rockchip_ipa.h>

#include "rknpu_ioctl.h"
#include "rknpu_reset.h"
#include "rknpu_gem.h"
#include "rknpu_fence.h"
#include "rknpu_drv.h"

static int bypass_irq_handler;
module_param(bypass_irq_handler, int, 0644);
MODULE_PARM_DESC(bypass_irq_handler,
		 "bypass RKNPU irq handler if set it to 1, disabled by default");

static int bypass_soft_reset;
module_param(bypass_soft_reset, int, 0644);
MODULE_PARM_DESC(bypass_soft_reset,
		 "bypass RKNPU soft reset if set it to 1, disabled by default");

static const struct rknpu_config rk356x_rknpu_config = {
	.bw_priority_addr = 0xfe180008,
	.bw_priority_length = 0x10,
	.dma_mask = DMA_BIT_MASK(32),
};

/* driver probe and init */
static const struct of_device_id rknpu_of_match[] = {
	{
		.compatible = "rockchip,rknpu",
		.data = &rk356x_rknpu_config,
	},
	{
		.compatible = "rockchip,rk3568-rknpu",
		.data = &rk356x_rknpu_config,
	},
	{},
};

static const struct vm_operations_struct rknpu_gem_vm_ops = {
	.fault = rknpu_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static int rknpu_action_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);

static const struct drm_ioctl_desc rknpu_ioctls[] = {
	DRM_IOCTL_DEF_DRV(RKNPU_ACTION, rknpu_action_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_SUBMIT, rknpu_submit_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_MEM_CREATE, rknpu_gem_create_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_MEM_MAP, rknpu_gem_map_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_MEM_DESTROY, rknpu_gem_destroy_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_MEM_SYNC, rknpu_gem_sync_ioctl,
			  DRM_RENDER_ALLOW),
};

static const struct file_operations rknpu_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = rknpu_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.release = drm_release,
	.llseek = noop_llseek,
};

static struct drm_driver rknpu_drm_driver = {
#if KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE
	.driver_features = DRIVER_GEM | DRIVER_RENDER,
#else
	.driver_features = DRIVER_GEM | DRIVER_PRIME | DRIVER_RENDER,
#endif
	.gem_free_object_unlocked = rknpu_gem_free_object,
	.gem_vm_ops = &rknpu_gem_vm_ops,
	.dumb_create = rknpu_gem_dumb_create,
#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
	.dumb_map_offset = rknpu_gem_dumb_map_offset,
#else
	.dumb_map_offset = drm_gem_dumb_map_offset,
#endif
	.dumb_destroy = drm_gem_dumb_destroy,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
	.gem_prime_import = rknpu_gem_prime_import,
#else
	.gem_prime_import = drm_gem_prime_import,
#endif
	.gem_prime_get_sg_table = rknpu_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = rknpu_gem_prime_import_sg_table,
	.gem_prime_vmap = rknpu_gem_prime_vmap,
	.gem_prime_vunmap = rknpu_gem_prime_vunmap,
	.gem_prime_mmap = rknpu_gem_prime_mmap,
	.ioctls = rknpu_ioctls,
	.num_ioctls = ARRAY_SIZE(rknpu_ioctls),
	.fops = &rknpu_drm_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int rknpu_get_drv_version(uint32_t *version)
{
	*version = RKNPU_GET_DRV_VERSION_CODE(DRIVER_MAJOR, DRIVER_MINOR,
					      DRIVER_PATCHLEVEL);
	return 0;
}

static int rknpu_action_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev->dev);
	struct rknpu_action *args = data;
	int ret = -EINVAL;

	switch (args->flags) {
	case RKNPU_GET_HW_VERSION:
		ret = rknpu_get_hw_version(rknpu_dev, &args->value);
		break;
	case RKNPU_GET_DRV_VERSION:
		ret = rknpu_get_drv_version(&args->value);
		break;
	case RKNPU_GET_FREQ:
		args->value = rknpu_dev->current_freq;
		ret = 0;
		break;
	case RKNPU_SET_FREQ:
		break;
	case RKNPU_GET_VOLT:
		args->value = rknpu_dev->current_volt;
		ret = 0;
		break;
	case RKNPU_SET_VOLT:
		break;
	case RKNPU_ACT_RESET:
		ret = rknpu_soft_reset(rknpu_dev);
		break;
	case RKNPU_GET_BW_PRIORITY:
		ret = rknpu_get_bw_priority(rknpu_dev, &args->value, NULL,
					    NULL);
		break;
	case RKNPU_SET_BW_PRIORITY:
		ret = rknpu_set_bw_priority(rknpu_dev, args->value, 0, 0);
		break;
	case RKNPU_GET_BW_EXPECT:
		ret = rknpu_get_bw_priority(rknpu_dev, NULL, &args->value,
					    NULL);
		break;
	case RKNPU_SET_BW_EXPECT:
		ret = rknpu_set_bw_priority(rknpu_dev, 0, args->value, 0);
		break;
	case RKNPU_GET_BW_TW:
		ret = rknpu_get_bw_priority(rknpu_dev, NULL, NULL,
					    &args->value);
		break;
	case RKNPU_SET_BW_TW:
		ret = rknpu_set_bw_priority(rknpu_dev, 0, 0, args->value);
		break;
	case RKNPU_ACT_CLR_TOTAL_RW_AMOUNT:
		ret = rknpu_clear_rw_amount(rknpu_dev);
		break;
	case RKNPU_GET_DT_WR_AMOUNT:
		ret = rknpu_get_rw_amount(rknpu_dev, &args->value, NULL, NULL);
		break;
	case RKNPU_GET_DT_RD_AMOUNT:
		ret = rknpu_get_rw_amount(rknpu_dev, NULL, &args->value, NULL);
		break;
	case RKNPU_GET_WT_RD_AMOUNT:
		ret = rknpu_get_rw_amount(rknpu_dev, NULL, NULL, &args->value);
		break;
	case RKNPU_GET_TOTAL_RW_AMOUNT:
		ret = rknpu_get_total_rw_amount(rknpu_dev, &args->value);
		break;
	case RKNPU_GET_IOMMU_EN:
		args->value = rknpu_dev->iommu_en;
		ret = 0;
		break;
	case RKNPU_SET_PROC_NICE:
		set_user_nice(current, *(int32_t *)&args->value);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static bool rknpu_is_iommu_enable(struct device *dev)
{
	struct device_node *iommu = NULL;

	iommu = of_parse_phandle(dev->of_node, "iommus", 0);
	if (!iommu) {
		LOG_DEV_INFO(
			dev,
			"rknpu iommu device-tree entry not found!, using non-iommu mode\n");
		return false;
	}

	if (!of_device_is_available(iommu)) {
		LOG_DEV_INFO(dev,
			     "rknpu iommu is disabled, using non-iommu mode\n");
		of_node_put(iommu);
		return false;
	}
	of_node_put(iommu);

	LOG_DEV_INFO(dev, "rknpu iommu is enabled, using iommu mode\n");

	return true;
}

static int drm_fake_dev_register(struct rknpu_device *rknpu_dev)
{
	const struct platform_device_info rknpu_dev_info = {
		.name = "rknpu_dev",
		.id = PLATFORM_DEVID_AUTO,
		.dma_mask = rknpu_dev->config->dma_mask,
	};
	struct platform_device *pdev = NULL;
	int ret = -EINVAL;

	pdev = platform_device_register_full(&rknpu_dev_info);
	if (pdev) {
		ret = of_dma_configure(&pdev->dev, NULL, true);
		if (ret) {
			platform_device_unregister(pdev);
			pdev = NULL;
		}
	}

	rknpu_dev->fake_dev = pdev ? &pdev->dev : NULL;

	return ret;
}

static void drm_fake_dev_unregister(struct rknpu_device *rknpu_dev)
{
	struct platform_device *pdev = NULL;

	if (!rknpu_dev->fake_dev)
		return;

	pdev = to_platform_device(rknpu_dev->fake_dev);

	platform_device_unregister(pdev);
}

static int rknpu_drm_probe(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;
	struct drm_device *drm_dev = NULL;
	int ret = -EINVAL;

	drm_dev = drm_dev_alloc(&rknpu_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	/* register the DRM device */
	ret = drm_dev_register(drm_dev, 0);
	if (ret < 0)
		goto err_free_drm;

	drm_dev->dev_private = rknpu_dev;
	rknpu_dev->drm_dev = drm_dev;

	drm_fake_dev_register(rknpu_dev);

	return 0;

err_free_drm:
#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	drm_dev_put(drm_dev);
#else
	drm_dev_unref(drm_dev);
#endif

	return ret;
}

static void rknpu_drm_remove(struct rknpu_device *rknpu_dev)
{
	struct drm_device *drm_dev = rknpu_dev->drm_dev;

	drm_fake_dev_unregister(rknpu_dev);

	drm_dev_unregister(drm_dev);

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	drm_dev_put(drm_dev);
#else
	drm_dev_unref(drm_dev);
#endif
}

static int rknpu_power_on(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;
	int ret = -EINVAL;

	ret = regulator_enable(rknpu_dev->vdd);
	if (ret) {
		LOG_DEV_ERROR(
			dev, "failed to enable regulator for rknpu, ret = %d\n",
			ret);
		return ret;
	}

	ret = clk_bulk_prepare_enable(rknpu_dev->num_clks, rknpu_dev->clks);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to enable clk for rknpu, ret = %d\n",
			      ret);
		return ret;
	}

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		LOG_DEV_ERROR(dev,
			      "failed to get pm runtime for rknpu, ret = %d\n",
			      ret);
		return ret;
	}

	return ret;
}

static int rknpu_power_off(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;

	pm_runtime_put_sync(dev);

	clk_bulk_disable_unprepare(rknpu_dev->num_clks, rknpu_dev->clks);

	regulator_disable(rknpu_dev->vdd);

	return 0;
}

static struct monitor_dev_profile npu_mdevp = {
	.type = MONITOR_TPYE_DEV,
	.low_temp_adjust = rockchip_monitor_dev_low_temp_adjust,
	.high_temp_adjust = rockchip_monitor_dev_high_temp_adjust,
};

static int npu_devfreq_target(struct device *dev, unsigned long *target_freq,
			      u32 flags)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev);
	struct dev_pm_opp *opp = NULL;
	unsigned long freq = *target_freq;
	unsigned long old_freq = rknpu_dev->current_freq;
	unsigned long volt, old_volt = rknpu_dev->current_volt;
	int ret = -EINVAL;

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_lock();
#endif

	opp = devfreq_recommended_opp(dev, &freq, flags);
	if (IS_ERR(opp)) {
#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
		rcu_read_unlock();
#endif
		LOG_DEV_ERROR(dev, "failed to get opp (%ld)\n", PTR_ERR(opp));
		return PTR_ERR(opp);
	}
	volt = dev_pm_opp_get_voltage(opp);
#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_unlock();
#endif

	/*
	 * Only update if there is a change of frequency
	 */
	if (old_freq == freq) {
		*target_freq = freq;
		if (old_volt == volt)
			return 0;
		ret = regulator_set_voltage(rknpu_dev->vdd, volt, INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev, "failed to set volt %lu\n", volt);
			return ret;
		}
		rknpu_dev->current_volt = volt;
		return 0;
	}

	if (rknpu_dev->vdd && old_volt != volt && old_freq < freq) {
		ret = regulator_set_voltage(rknpu_dev->vdd, volt, INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev, "failed to increase volt %lu\n",
				      volt);
			return ret;
		}
	}
	LOG_DEV_DEBUG(dev, "%luHz %luuV -> %luHz %luuV\n", old_freq, old_volt,
		      freq, volt);
	ret = clk_set_rate(rknpu_dev->clks[0].clk, freq);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to set clock %lu\n", freq);
		return ret;
	}
	*target_freq = freq;
	rknpu_dev->current_freq = freq;

	if (rknpu_dev->devfreq)
		rknpu_dev->devfreq->last_status.current_frequency = freq;

	if (rknpu_dev->vdd && old_volt != volt && old_freq > freq) {
		ret = regulator_set_voltage(rknpu_dev->vdd, volt, INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev, "failed to decrease volt %lu\n",
				      volt);
			return ret;
		}
	}
	rknpu_dev->current_volt = volt;

	return ret;
}

static int npu_devfreq_get_dev_status(struct device *dev,
				      struct devfreq_dev_status *stat)
{
	return 0;
}

static int npu_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev);

	*freq = rknpu_dev->current_freq;

	return 0;
}

static struct devfreq_dev_profile npu_devfreq_profile = {
	.polling_ms = 50,
	.target = npu_devfreq_target,
	.get_dev_status = npu_devfreq_get_dev_status,
	.get_cur_freq = npu_devfreq_get_cur_freq,
};

static unsigned long npu_get_static_power(struct devfreq *devfreq,
					  unsigned long voltage)
{
	struct device *dev = devfreq->dev.parent;
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev);

	if (!rknpu_dev->model_data)
		return 0;

	return rockchip_ipa_get_static_power(rknpu_dev->model_data, voltage);
}

static struct devfreq_cooling_power npu_cooling_power = {
	.get_static_power = &npu_get_static_power,
};

static int npu_devfreq_adjust_current_freq_volt(struct device *dev,
						struct rknpu_device *rknpu_dev)
{
	unsigned long volt, old_freq, freq;
	struct dev_pm_opp *opp = NULL;
	int ret = -EINVAL;

	old_freq = clk_get_rate(rknpu_dev->clks[0].clk);
	freq = old_freq;

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_lock();
#endif

	opp = devfreq_recommended_opp(dev, &freq, 0);
	volt = dev_pm_opp_get_voltage(opp);

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_unlock();
#endif

	if (freq >= old_freq && rknpu_dev->vdd) {
		ret = regulator_set_voltage(rknpu_dev->vdd, volt, INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev, "failed to set volt %lu\n", volt);
			return ret;
		}
	}
	LOG_DEV_DEBUG(dev, "adjust current freq=%luHz, volt=%luuV\n", freq,
		      volt);
	ret = clk_set_rate(rknpu_dev->clks[0].clk, freq);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to set clock %lu\n", freq);
		return ret;
	}
	if (freq < old_freq && rknpu_dev->vdd) {
		ret = regulator_set_voltage(rknpu_dev->vdd, volt, INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev, "failed to set volt %lu\n", volt);
			return ret;
		}
	}
	rknpu_dev->current_freq = freq;
	rknpu_dev->current_volt = volt;

	return 0;
}

static int rknpu_devfreq_init(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;
	struct devfreq_dev_profile *dp = &npu_devfreq_profile;
	int ret = -EINVAL;

	ret = rockchip_init_opp_table(dev, NULL, "npu_leakage", "rknpu");

	if (ret) {
		LOG_DEV_ERROR(dev, "failed to init_opp_table\n");
		return ret;
	}

	ret = npu_devfreq_adjust_current_freq_volt(dev, rknpu_dev);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to adjust current freq volt\n");
		return ret;
	}
	dp->initial_freq = rknpu_dev->current_freq;

	rknpu_dev->devfreq =
		devm_devfreq_add_device(dev, dp, "userspace", NULL);
	if (IS_ERR(rknpu_dev->devfreq)) {
		LOG_DEV_ERROR(dev, "failed to add devfreq\n");
		return PTR_ERR(rknpu_dev->devfreq);
	}
	devm_devfreq_register_opp_notifier(dev, rknpu_dev->devfreq);

	rknpu_dev->devfreq->last_status.current_frequency = dp->initial_freq;
	rknpu_dev->devfreq->last_status.total_time = 1;
	rknpu_dev->devfreq->last_status.busy_time = 1;

	npu_mdevp.data = rknpu_dev->devfreq;
	rknpu_dev->mdev_info =
		rockchip_system_monitor_register(dev, &npu_mdevp);
	if (IS_ERR(rknpu_dev->mdev_info)) {
		LOG_DEV_DEBUG(dev, "without system monitor\n");
		rknpu_dev->mdev_info = NULL;
	}

	of_property_read_u32(dev->of_node, "dynamic-power-coefficient",
			     (u32 *)&npu_cooling_power.dyn_power_coeff);

#if KERNEL_VERSION(4, 4, 179) <= LINUX_VERSION_CODE
	rknpu_dev->model_data =
		rockchip_ipa_power_model_init(dev, "npu_leakage");
	if (IS_ERR_OR_NULL(rknpu_dev->model_data)) {
#else
	ret = rockchip_ipa_power_model_init(dev, &rknpu_dev->model_data);
	if (ret) {
#endif
		rknpu_dev->model_data = NULL;
		LOG_DEV_ERROR(dev, "failed to initialize power model\n");
	} else if (rknpu_dev->model_data->dynamic_coefficient) {
		npu_cooling_power.dyn_power_coeff =
			rknpu_dev->model_data->dynamic_coefficient;
	}

	if (!npu_cooling_power.dyn_power_coeff) {
		LOG_DEV_ERROR(dev, "failed to get dynamic-coefficient\n");
		goto out;
	}

#if KERNEL_VERSION(4, 4, 179) > LINUX_VERSION_CODE
	rockchip_of_get_leakage(dev, "npu_leakage",
				&rknpu_dev->model_data->leakage);
#endif
	rknpu_dev->devfreq_cooling = of_devfreq_cooling_register_power(
		dev->of_node, rknpu_dev->devfreq, &npu_cooling_power);
	if (IS_ERR_OR_NULL(rknpu_dev->devfreq_cooling))
		LOG_DEV_ERROR(dev, "failed to register cooling device\n");

out:
	return 0;
}

static int rknpu_probe(struct platform_device *pdev)
{
	struct resource *res = NULL;
	struct rknpu_device *rknpu_dev = NULL;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match = NULL;
	const struct rknpu_config *config = NULL;
	int ret = -EINVAL;

	if (!pdev->dev.of_node) {
		LOG_DEV_ERROR(dev, "rknpu device-tree data is missing!\n");
		return -ENODEV;
	}

	match = of_match_device(rknpu_of_match, dev);
	if (!match) {
		LOG_DEV_ERROR(dev, "rknpu device-tree entry is missing!\n");
		return -ENODEV;
	}

	rknpu_dev = devm_kzalloc(dev, sizeof(*rknpu_dev), GFP_KERNEL);
	if (!rknpu_dev) {
		LOG_DEV_ERROR(dev, "failed to allocate rknpu device!\n");
		return -ENOMEM;
	}

	config = of_device_get_match_data(dev);
	if (!config)
		return -EINVAL;

	rknpu_dev->config = config;
	rknpu_dev->dev = dev;

	rknpu_dev->iommu_en = rknpu_is_iommu_enable(dev);
	if (!rknpu_dev->iommu_en) {
		/* Initialize reserved memory resources */
		ret = of_reserved_mem_device_init(dev);
		if (!ret) {
			LOG_DEV_INFO(
				dev,
				"initialize reserved memory for rknpu device!\n");
		}
	}

	rknpu_dev->bypass_irq_handler = bypass_irq_handler;
	rknpu_dev->bypass_soft_reset = bypass_soft_reset;

	rknpu_reset_get(rknpu_dev);

	rknpu_dev->num_clks = devm_clk_bulk_get_all(dev, &rknpu_dev->clks);
	if (rknpu_dev->num_clks < 1) {
		LOG_DEV_ERROR(dev, "failed to get clk source for rknpu\n");
		return -ENODEV;
	}

	rknpu_dev->vdd = devm_regulator_get_optional(dev, "rknpu");
	if (IS_ERR(rknpu_dev->vdd)) {
		if (PTR_ERR(rknpu_dev->vdd) != -ENODEV) {
			ret = PTR_ERR(rknpu_dev->vdd);
			LOG_DEV_ERROR(
				dev,
				"failed to get vdd regulator for rknpu: %d\n",
				ret);
			return ret;
		}
		rknpu_dev->vdd = NULL;
	}

	spin_lock_init(&rknpu_dev->lock);
	spin_lock_init(&rknpu_dev->irq_lock);
	INIT_LIST_HEAD(&rknpu_dev->todo_list);
	init_waitqueue_head(&rknpu_dev->job_done_wq);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		LOG_DEV_ERROR(dev, "failed to get memory resource for rknpu\n");
		return -ENXIO;
	}

	rknpu_dev->base = devm_ioremap_resource(dev, res);
	if (PTR_ERR(rknpu_dev->base) == -EBUSY) {
		rknpu_dev->base =
			devm_ioremap(dev, res->start, resource_size(res));
	}

	if (IS_ERR(rknpu_dev->base)) {
		LOG_DEV_ERROR(dev, "failed to remap register for rknpu\n");
		return PTR_ERR(rknpu_dev->base);
	}

	rknpu_dev->bw_priority_base = devm_ioremap(
		dev, config->bw_priority_addr, config->bw_priority_length);
	if (IS_ERR(rknpu_dev->bw_priority_base)) {
		LOG_DEV_ERROR(
			rknpu_dev->dev,
			"failed to remap bw priority register for rknpu\n");
		rknpu_dev->bw_priority_base = NULL;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		LOG_DEV_ERROR(dev,
			      "failed to get interrupt resource for rknpu\n");
		return -ENXIO;
	}

	if (!rknpu_dev->bypass_irq_handler) {
		ret = devm_request_irq(dev, res->start, rknpu_irq_handler,
				       IRQF_SHARED, dev_name(dev), rknpu_dev);
		if (ret) {
			LOG_DEV_ERROR(dev, "failed to request irq for rknpu\n");
			return ret;
		}
	} else {
		LOG_DEV_WARN(dev, "bypass irq handler!\n");
	}

	ret = rknpu_drm_probe(rknpu_dev);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to probe device for rknpu\n");
		return ret;
	}

	rknpu_dev->fence_ctx = rknpu_fence_context_alloc();
	if (IS_ERR(rknpu_dev->fence_ctx)) {
		LOG_DEV_ERROR(dev,
			      "failed to allocate fence context for rknpu\n");
		ret = PTR_ERR(rknpu_dev->fence_ctx);
		goto err_remove_drm;
	}

	platform_set_drvdata(pdev, rknpu_dev);

	rknpu_devfreq_init(rknpu_dev);

	pm_runtime_enable(dev);

	ret = rknpu_power_on(rknpu_dev);
	if (ret)
		goto err_free_fence_context;

	return 0;

err_free_fence_context:
	rknpu_fence_context_free(rknpu_dev->fence_ctx);
err_remove_drm:
	rknpu_drm_remove(rknpu_dev);

	return ret;
}

static int rknpu_remove(struct platform_device *pdev)
{
	struct rknpu_device *rknpu_dev = platform_get_drvdata(pdev);

	WARN_ON(rknpu_dev->job);
	WARN_ON(!list_empty(&rknpu_dev->todo_list));

	rknpu_drm_remove(rknpu_dev);

	rknpu_fence_context_free(rknpu_dev->fence_ctx);

	rknpu_power_off(rknpu_dev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver rknpu_driver = {
	.probe = rknpu_probe,
	.remove = rknpu_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "RKNPU",
		.of_match_table = of_match_ptr(rknpu_of_match),
	},
};

static int rknpu_init(void)
{
	return platform_driver_register(&rknpu_driver);
}

static void rknpu_exit(void)
{
	platform_driver_unregister(&rknpu_driver);
}

late_initcall(rknpu_init);
module_exit(rknpu_exit);

MODULE_DESCRIPTION("RKNPU driver");
MODULE_AUTHOR("Felix Zeng <felix.zeng@rock-chips.com>");
MODULE_ALIAS("rockchip-rknpu");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(RKNPU_GET_DRV_VERSION_STRING(DRIVER_MAJOR, DRIVER_MINOR,
					    DRIVER_PATCHLEVEL));
