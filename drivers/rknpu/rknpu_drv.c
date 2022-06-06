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
#include <linux/iopoll.h>
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
#include <linux/clk-provider.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/devfreq_cooling.h>
#include <linux/regmap.h>

#ifndef FPGA_PLATFORM
#include <soc/rockchip/rockchip_iommu.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>
#include <soc/rockchip/rockchip_ipa.h>
#endif

#include "rknpu_ioctl.h"
#include "rknpu_reset.h"
#include "rknpu_fence.h"
#include "rknpu_drv.h"

#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
#include <drm/drm_device.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_file.h>
#include <drm/drm_drv.h>
#include "rknpu_gem.h"
#endif

#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
#include <linux/rk-dma-heap.h>
#include "rknpu_mem.h"
#endif

#define POWER_DOWN_FREQ 200000000
#define NPU_MMU_DISABLED_POLL_PERIOD_US 1000
#define NPU_MMU_DISABLED_POLL_TIMEOUT_US 20000

static int bypass_irq_handler;
module_param(bypass_irq_handler, int, 0644);
MODULE_PARM_DESC(bypass_irq_handler,
		 "bypass RKNPU irq handler if set it to 1, disabled by default");

static int bypass_soft_reset;
module_param(bypass_soft_reset, int, 0644);
MODULE_PARM_DESC(bypass_soft_reset,
		 "bypass RKNPU soft reset if set it to 1, disabled by default");

struct npu_irqs_data {
	const char *name;
	irqreturn_t (*irq_hdl)(int irq, void *ctx);
};

static const struct npu_irqs_data rk356x_npu_irqs[] = {
	{ "npu_irq", rknpu_core0_irq_handler }
};

static const struct npu_irqs_data rk3588_npu_irqs[] = {
	{ "npu0_irq", rknpu_core0_irq_handler },
	{ "npu1_irq", rknpu_core1_irq_handler },
	{ "npu2_irq", rknpu_core2_irq_handler }
};

static const struct npu_irqs_data rv110x_npu_irqs[] = {
	{ "npu_irq", rknpu_core0_irq_handler }
};

static const struct npu_reset_data rk356x_npu_resets[] = { { "srst_a",
							     "srst_h" } };

static const struct npu_reset_data rk3588_npu_resets[] = {
	{ "srst_a0", "srst_h0" },
	{ "srst_a1", "srst_h1" },
	{ "srst_a2", "srst_h2" }
};

static const struct npu_reset_data rv110x_npu_resets[] = { { "srst_a",
							     "srst_h" } };

static const struct rknpu_config rk356x_rknpu_config = {
	.bw_priority_addr = 0xfe180008,
	.bw_priority_length = 0x10,
	.dma_mask = DMA_BIT_MASK(32),
	.pc_data_amount_scale = 1,
	.pc_task_number_bits = 12,
	.pc_task_number_mask = 0xfff,
	.bw_enable = 1,
	.irqs = rk356x_npu_irqs,
	.resets = rk356x_npu_resets,
	.num_irqs = ARRAY_SIZE(rk356x_npu_irqs),
	.num_resets = ARRAY_SIZE(rk356x_npu_resets)
};

static const struct rknpu_config rk3588_rknpu_config = {
	.bw_priority_addr = 0x0,
	.bw_priority_length = 0x0,
	.dma_mask = DMA_BIT_MASK(40),
	.pc_data_amount_scale = 2,
	.pc_task_number_bits = 12,
	.pc_task_number_mask = 0xfff,
	.bw_enable = 0,
	.irqs = rk3588_npu_irqs,
	.resets = rk3588_npu_resets,
	.num_irqs = ARRAY_SIZE(rk3588_npu_irqs),
	.num_resets = ARRAY_SIZE(rk3588_npu_resets)
};

static const struct rknpu_config rv1106_rknpu_config = {
	.bw_priority_addr = 0x0,
	.bw_priority_length = 0x0,
	.dma_mask = DMA_BIT_MASK(32),
	.pc_data_amount_scale = 2,
	.pc_task_number_bits = 16,
	.pc_task_number_mask = 0xffff,
	.bw_enable = 1,
	.irqs = rv110x_npu_irqs,
	.resets = rv110x_npu_resets,
	.num_irqs = ARRAY_SIZE(rv110x_npu_irqs),
	.num_resets = ARRAY_SIZE(rv110x_npu_resets)
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
	{
		.compatible = "rockchip,rk3588-rknpu",
		.data = &rk3588_rknpu_config,
	},
	{
		.compatible = "rockchip,rv1106-rknpu",
		.data = &rv1106_rknpu_config,
	},
	{},
};

static int rknpu_get_drv_version(uint32_t *version)
{
	*version = RKNPU_GET_DRV_VERSION_CODE(DRIVER_MAJOR, DRIVER_MINOR,
					      DRIVER_PATCHLEVEL);
	return 0;
}

static int rknpu_power_on(struct rknpu_device *rknpu_dev);
static int rknpu_power_off(struct rknpu_device *rknpu_dev);

static void rknpu_power_off_delay_work(struct work_struct *power_off_work)
{
	struct rknpu_device *rknpu_dev =
		container_of(to_delayed_work(power_off_work),
			     struct rknpu_device, power_off_work);
	mutex_lock(&rknpu_dev->power_lock);
	if (atomic_read(&rknpu_dev->power_refcount) == 0 &&
	    rknpu_dev->is_powered) {
		rknpu_dev->is_powered = false;
		rknpu_power_off(rknpu_dev);
	}
	mutex_unlock(&rknpu_dev->power_lock);
}

int rknpu_action(struct rknpu_device *rknpu_dev, struct rknpu_action *args)
{
	int ret = -EINVAL;

	switch (args->flags) {
	case RKNPU_POWER_ON:
		atomic_inc(&rknpu_dev->power_refcount);
		mutex_lock(&rknpu_dev->power_lock);
		if (!rknpu_dev->is_powered) {
			rknpu_dev->is_powered = true;
			ret = rknpu_power_on(rknpu_dev);
		}
		mutex_unlock(&rknpu_dev->power_lock);
		break;
	case RKNPU_POWER_OFF:
		if (rknpu_dev->is_powered &&
		    atomic_dec_if_positive(&rknpu_dev->power_refcount) == 0)
			queue_delayed_work(rknpu_dev->power_off_wq,
					   &rknpu_dev->power_off_work,
					   msecs_to_jiffies(1000));
		break;
	default:
		/* default open rknpu power to compatible with librknnrt.so version before 1.2.0 */
		mutex_lock(&rknpu_dev->power_lock);
		if (!rknpu_dev->is_powered) {
			atomic_inc(&rknpu_dev->power_refcount);
			rknpu_dev->is_powered = true;
			ret = rknpu_power_on(rknpu_dev);
		}
		mutex_unlock(&rknpu_dev->power_lock);
	}

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

#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
static int rknpu_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int rknpu_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int rknpu_action_ioctl(struct rknpu_device *rknpu_dev,
			      unsigned long data)
{
	struct rknpu_action args;
	int ret = -EINVAL;

	if (unlikely(copy_from_user(&args, (struct rknpu_action *)data,
				    sizeof(struct rknpu_action)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	ret = rknpu_action(rknpu_dev, &args);

	if (unlikely(copy_to_user((struct rknpu_action *)data, &args,
				  sizeof(struct rknpu_action)))) {
		LOG_ERROR("%s: copy_to_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	return ret;
}

static long rknpu_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	long ret = 0;
	struct rknpu_device *rknpu_dev =
		container_of(file->private_data, struct rknpu_device, miscdev);
	switch (cmd) {
	case IOCTL_RKNPU_ACTION:
		ret = rknpu_action_ioctl(rknpu_dev, arg);
		break;
	case IOCTL_RKNPU_SUBMIT:
		ret = rknpu_submit_ioctl(rknpu_dev, arg);
		break;
	case IOCTL_RKNPU_MEM_CREATE:
		ret = rknpu_mem_create_ioctl(rknpu_dev, arg);
		break;
	case RKNPU_MEM_MAP:
		break;
	case IOCTL_RKNPU_MEM_DESTROY:
		ret = rknpu_mem_destroy_ioctl(rknpu_dev, arg);
		break;
	case IOCTL_RKNPU_MEM_SYNC:
		ret = rknpu_mem_sync_ioctl(rknpu_dev, arg);
		break;
	default:
		break;
	}
	return ret;
}
const struct file_operations rknpu_fops = {
	.owner = THIS_MODULE,
	.open = rknpu_open,
	.release = rknpu_release,
	.unlocked_ioctl = rknpu_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = rknpu_ioctl,
#endif
};
#endif

#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
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

static int rknpu_action_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev->dev);

	return rknpu_action(rknpu_dev, (struct rknpu_action *)data);
}

#endif

static enum hrtimer_restart hrtimer_handler(struct hrtimer *timer)
{
	struct rknpu_device *rknpu_dev =
		container_of(timer, struct rknpu_device, timer);
	struct rknpu_subcore_data *subcore_data = NULL;
	struct rknpu_job *job = NULL;
	ktime_t now = ktime_get();
	unsigned long flags;
	int i;

	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		subcore_data = &rknpu_dev->subcore_datas[i];

		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);

		job = subcore_data->job;
		if (job) {
			subcore_data->timer.busy_time +=
				ktime_us_delta(now, job->hw_recoder_time);
			job->hw_recoder_time = ktime_get();
		}

		subcore_data->timer.busy_time_record =
			subcore_data->timer.busy_time;
		subcore_data->timer.busy_time = 0;
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
	}

	hrtimer_forward_now(timer, rknpu_dev->kt);
	return HRTIMER_RESTART;
}

static void rknpu_init_timer(struct rknpu_device *rknpu_dev)
{
	rknpu_dev->kt = ktime_set(0, RKNPU_LOAD_INTERVAL);
	hrtimer_init(&rknpu_dev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	rknpu_dev->timer.function = hrtimer_handler;
	hrtimer_start(&rknpu_dev->timer, rknpu_dev->kt, HRTIMER_MODE_REL);
}

static void rknpu_cancel_timer(struct rknpu_device *rknpu_dev)
{
	hrtimer_cancel(&rknpu_dev->timer);
}

#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP

#endif

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

#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
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

	drm_dev_unregister(drm_dev);

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	drm_dev_put(drm_dev);
#else
	drm_dev_unref(drm_dev);
#endif
}
#endif

static int rknpu_power_on(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;
	int ret = -EINVAL;

#ifndef FPGA_PLATFORM
	if (rknpu_dev->vdd) {
		ret = regulator_enable(rknpu_dev->vdd);
		if (ret) {
			LOG_DEV_ERROR(
				dev,
				"failed to enable vdd reg for rknpu, ret = %d\n",
				ret);
			return ret;
		}
	}

	if (rknpu_dev->mem) {
		ret = regulator_enable(rknpu_dev->mem);
		if (ret) {
			LOG_DEV_ERROR(
				dev,
				"failed to enable mem reg for rknpu, ret = %d\n",
				ret);
			return ret;
		}
	}
#endif

	ret = clk_bulk_prepare_enable(rknpu_dev->num_clks, rknpu_dev->clks);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to enable clk for rknpu, ret = %d\n",
			      ret);
		return ret;
	}

#ifndef FPGA_PLATFORM
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rockchip_monitor_volt_adjust_lock(rknpu_dev->mdev_info);
#endif
#endif

	if (rknpu_dev->multiple_domains) {
		if (rknpu_dev->genpd_dev_npu0) {
#if KERNEL_VERSION(5, 5, 0) < LINUX_VERSION_CODE
			ret = pm_runtime_resume_and_get(
				rknpu_dev->genpd_dev_npu0);
#else
			ret = pm_runtime_get_sync(rknpu_dev->genpd_dev_npu0);
#endif
			if (ret < 0) {
				LOG_DEV_ERROR(
					dev,
					"failed to get pm runtime for npu0, ret = %d\n",
					ret);
				goto out;
			}
		}
		if (rknpu_dev->genpd_dev_npu1) {
#if KERNEL_VERSION(5, 5, 0) < LINUX_VERSION_CODE
			ret = pm_runtime_resume_and_get(
				rknpu_dev->genpd_dev_npu1);
#else
			ret = pm_runtime_get_sync(rknpu_dev->genpd_dev_npu1);
#endif
			if (ret < 0) {
				LOG_DEV_ERROR(
					dev,
					"failed to get pm runtime for npu1, ret = %d\n",
					ret);
				goto out;
			}
		}
		if (rknpu_dev->genpd_dev_npu2) {
#if KERNEL_VERSION(5, 5, 0) < LINUX_VERSION_CODE
			ret = pm_runtime_resume_and_get(
				rknpu_dev->genpd_dev_npu2);
#else
			ret = pm_runtime_get_sync(rknpu_dev->genpd_dev_npu2);
#endif
			if (ret < 0) {
				LOG_DEV_ERROR(
					dev,
					"failed to get pm runtime for npu2, ret = %d\n",
					ret);
				goto out;
			}
		}
	}
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		LOG_DEV_ERROR(dev,
			      "failed to get pm runtime for rknpu, ret = %d\n",
			      ret);
	}

out:
#ifndef FPGA_PLATFORM
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rockchip_monitor_volt_adjust_unlock(rknpu_dev->mdev_info);
#endif
#endif

	return ret;
}

static int rknpu_power_off(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;

#ifndef FPGA_PLATFORM
	int ret;
	bool val;

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rockchip_monitor_volt_adjust_lock(rknpu_dev->mdev_info);
#endif
#endif

	pm_runtime_put_sync(dev);

	if (rknpu_dev->multiple_domains) {
#ifndef FPGA_PLATFORM
		/*
		 * Because IOMMU's runtime suspend callback is asynchronous,
		 * So it may be executed after the NPU is turned off after PD/CLK/VD,
		 * and the runtime suspend callback has a register access.
		 * If the PD/VD/CLK is closed, the register access will crash.
		 * As a workaround, it's safe to close pd stuff until iommu disabled.
		 * If pm runtime framework can handle this issue in the future, remove
		 * this.
		 */
		ret = readx_poll_timeout(rockchip_iommu_is_enabled, dev, val,
					 !val, NPU_MMU_DISABLED_POLL_PERIOD_US,
					 NPU_MMU_DISABLED_POLL_TIMEOUT_US);
		if (ret) {
			LOG_DEV_ERROR(dev, "iommu still enabled\n");
			pm_runtime_get_sync(dev);
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
			rockchip_monitor_volt_adjust_unlock(
				rknpu_dev->mdev_info);
#endif
			return ret;
		}
#else
		if (rknpu_dev->iommu_en)
			msleep(20);
#endif
		if (rknpu_dev->genpd_dev_npu2)
			pm_runtime_put_sync(rknpu_dev->genpd_dev_npu2);
		if (rknpu_dev->genpd_dev_npu1)
			pm_runtime_put_sync(rknpu_dev->genpd_dev_npu1);
		if (rknpu_dev->genpd_dev_npu0)
			pm_runtime_put_sync(rknpu_dev->genpd_dev_npu0);
	}

#ifndef FPGA_PLATFORM
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rockchip_monitor_volt_adjust_unlock(rknpu_dev->mdev_info);
#endif
#endif

	clk_bulk_disable_unprepare(rknpu_dev->num_clks, rknpu_dev->clks);

#ifndef FPGA_PLATFORM
	if (rknpu_dev->vdd)
		regulator_disable(rknpu_dev->vdd);

	if (rknpu_dev->mem)
		regulator_disable(rknpu_dev->mem);
#endif

	return 0;
}

#ifndef FPGA_PLATFORM
static struct monitor_dev_profile npu_mdevp = {
	.type = MONITOR_TPYE_DEV,
	.low_temp_adjust = rockchip_monitor_dev_low_temp_adjust,
	.high_temp_adjust = rockchip_monitor_dev_high_temp_adjust,
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	.update_volt = rockchip_monitor_check_rate_volt,
#endif
};

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
static int npu_opp_helper(struct dev_pm_set_opp_data *data)
{
	struct device *dev = data->dev;
	struct dev_pm_opp_supply *old_supply_vdd = &data->old_opp.supplies[0];
	struct dev_pm_opp_supply *old_supply_mem = &data->old_opp.supplies[1];
	struct dev_pm_opp_supply *new_supply_vdd = &data->new_opp.supplies[0];
	struct dev_pm_opp_supply *new_supply_mem = &data->new_opp.supplies[1];
	struct regulator *vdd_reg = data->regulators[0];
	struct regulator *mem_reg = data->regulators[1];
	struct clk *clk = data->clk;
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev);
	struct rockchip_opp_info *opp_info = &rknpu_dev->opp_info;
	unsigned long old_freq = data->old_opp.rate;
	unsigned long new_freq = data->new_opp.rate;
	bool is_set_rm = true;
	bool is_set_clk = true;
	u32 target_rm = UINT_MAX;
	int ret = 0;

	if (!pm_runtime_active(dev)) {
		is_set_rm = false;
		if (opp_info->scmi_clk)
			is_set_clk = false;
	}

	ret = clk_bulk_prepare_enable(opp_info->num_clks, opp_info->clks);
	if (ret < 0) {
		LOG_DEV_ERROR(dev, "failed to enable opp clks\n");
		return ret;
	}
	rockchip_get_read_margin(dev, opp_info, new_supply_vdd->u_volt,
				 &target_rm);

	/* Change frequency */
	LOG_DEV_DEBUG(dev, "switching OPP: %lu Hz --> %lu Hz\n", old_freq,
		      new_freq);
	/* Scaling up? Scale voltage before frequency */
	if (new_freq >= old_freq) {
		rockchip_set_intermediate_rate(dev, opp_info, clk, old_freq,
					       new_freq, true, is_set_clk);
		ret = regulator_set_voltage(mem_reg, new_supply_mem->u_volt,
					    INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev,
				      "failed to set volt %lu uV for mem reg\n",
				      new_supply_mem->u_volt);
			goto restore_voltage;
		}
		ret = regulator_set_voltage(vdd_reg, new_supply_vdd->u_volt,
					    INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev,
				      "failed to set volt %lu uV for vdd reg\n",
				      new_supply_vdd->u_volt);
			goto restore_voltage;
		}
		rockchip_set_read_margin(dev, opp_info, target_rm, is_set_rm);
		if (is_set_clk && clk_set_rate(clk, new_freq)) {
			ret = -EINVAL;
			LOG_DEV_ERROR(dev, "failed to set clk rate\n");
			goto restore_rm;
		}
		/* Scaling down? Scale voltage after frequency */
	} else {
		rockchip_set_intermediate_rate(dev, opp_info, clk, old_freq,
					       new_freq, false, is_set_clk);
		rockchip_set_read_margin(dev, opp_info, target_rm, is_set_rm);
		if (is_set_clk && clk_set_rate(clk, new_freq)) {
			ret = -EINVAL;
			LOG_DEV_ERROR(dev, "failed to set clk rate\n");
			goto restore_rm;
		}
		ret = regulator_set_voltage(vdd_reg, new_supply_vdd->u_volt,
					    INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev,
				      "failed to set volt %lu uV for vdd reg\n",
				      new_supply_vdd->u_volt);
			goto restore_freq;
		}
		ret = regulator_set_voltage(mem_reg, new_supply_mem->u_volt,
					    INT_MAX);
		if (ret) {
			LOG_DEV_ERROR(dev,
				      "failed to set volt %lu uV for mem reg\n",
				      new_supply_mem->u_volt);
			goto restore_freq;
		}
	}

	clk_bulk_disable_unprepare(opp_info->num_clks, opp_info->clks);

	return 0;

restore_freq:
	if (is_set_clk && clk_set_rate(clk, old_freq))
		LOG_DEV_ERROR(dev, "failed to restore old-freq %lu Hz\n",
			      old_freq);
restore_rm:
	rockchip_get_read_margin(dev, opp_info, old_supply_vdd->u_volt,
				 &target_rm);
	rockchip_set_read_margin(dev, opp_info, opp_info->current_rm,
				 is_set_rm);
restore_voltage:
	regulator_set_voltage(mem_reg, old_supply_mem->u_volt, INT_MAX);
	regulator_set_voltage(vdd_reg, old_supply_vdd->u_volt, INT_MAX);
	clk_bulk_disable_unprepare(opp_info->num_clks, opp_info->clks);

	return ret;
}

static int npu_devfreq_target(struct device *dev, unsigned long *freq,
			      u32 flags)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long opp_volt;
	int ret = 0;

	if (!npu_mdevp.is_checked)
		return -EINVAL;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);
	opp_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rockchip_monitor_volt_adjust_lock(rknpu_dev->mdev_info);
#endif
	ret = dev_pm_opp_set_rate(dev, *freq);
	if (!ret) {
		rknpu_dev->current_freq = *freq;
		if (rknpu_dev->devfreq)
			rknpu_dev->devfreq->last_status.current_frequency =
				*freq;
		rknpu_dev->current_volt = opp_volt;
	}
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rockchip_monitor_volt_adjust_unlock(rknpu_dev->mdev_info);
#endif

	return ret;
}

#else

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
#endif

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

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
static int rk3588_npu_set_read_margin(struct device *dev,
				      struct rockchip_opp_info *opp_info,
				      u32 rm)
{
	u32 offset = 0, val = 0;
	int i, ret = 0;

	if (!opp_info->grf || !opp_info->volt_rm_tbl)
		return 0;

	if (rm == opp_info->current_rm || rm == UINT_MAX)
		return 0;

	LOG_DEV_DEBUG(dev, "set rm to %d\n", rm);

	for (i = 0; i < 3; i++) {
		ret = regmap_read(opp_info->grf, offset, &val);
		if (ret < 0) {
			LOG_DEV_ERROR(dev, "failed to get rm from 0x%x\n",
				      offset);
			return ret;
		}
		val &= ~0x1c;
		regmap_write(opp_info->grf, offset, val | (rm << 2));
		offset += 4;
	}
	opp_info->current_rm = rm;

	return 0;
}

static const struct rockchip_opp_data rk3588_npu_opp_data = {
	.set_read_margin = rk3588_npu_set_read_margin,
};

static const struct of_device_id rockchip_npu_of_match[] = {
	{
		.compatible = "rockchip,rk3588",
		.data = (void *)&rk3588_npu_opp_data,
	},
	{},
};

static int rknpu_devfreq_init(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;
	struct devfreq_dev_profile *dp = &npu_devfreq_profile;
	struct dev_pm_opp *opp;
	struct opp_table *reg_table = NULL;
	struct opp_table *opp_table = NULL;
	const char *const reg_names[] = { "rknpu", "mem" };
	int ret = -EINVAL;

	if (of_find_property(dev->of_node, "rknpu-supply", NULL) &&
	    of_find_property(dev->of_node, "mem-supply", NULL)) {
		reg_table = dev_pm_opp_set_regulators(dev, reg_names, 2);
		if (IS_ERR(reg_table))
			return PTR_ERR(reg_table);
		opp_table =
			dev_pm_opp_register_set_opp_helper(dev, npu_opp_helper);
		if (IS_ERR(opp_table)) {
			dev_pm_opp_put_regulators(reg_table);
			return PTR_ERR(opp_table);
		}
	} else {
		reg_table = dev_pm_opp_set_regulators(dev, reg_names, 1);
		if (IS_ERR(reg_table))
			return PTR_ERR(reg_table);
	}

	rockchip_get_opp_data(rockchip_npu_of_match, &rknpu_dev->opp_info);
	ret = rockchip_init_opp_table(dev, &rknpu_dev->opp_info, "npu_leakage",
				      "rknpu");
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to init_opp_table\n");
		return ret;
	}

	rknpu_dev->current_freq = clk_get_rate(rknpu_dev->clks[0].clk);

	opp = devfreq_recommended_opp(dev, &rknpu_dev->current_freq, 0);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		return ret;
	}
	dev_pm_opp_put(opp);
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
	npu_mdevp.opp_info = &rknpu_dev->opp_info;
	rknpu_dev->mdev_info =
		rockchip_system_monitor_register(dev, &npu_mdevp);
	if (IS_ERR(rknpu_dev->mdev_info)) {
		LOG_DEV_DEBUG(dev, "without system monitor\n");
		rknpu_dev->mdev_info = NULL;
		npu_mdevp.is_checked = true;
	}
	rknpu_dev->current_freq = clk_get_rate(rknpu_dev->clks[0].clk);
	rknpu_dev->current_volt = regulator_get_voltage(rknpu_dev->vdd);

	of_property_read_u32(dev->of_node, "dynamic-power-coefficient",
			     (u32 *)&npu_cooling_power.dyn_power_coeff);
	rknpu_dev->model_data =
		rockchip_ipa_power_model_init(dev, "npu_leakage");
	if (IS_ERR_OR_NULL(rknpu_dev->model_data)) {
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

	rknpu_dev->devfreq_cooling = of_devfreq_cooling_register_power(
		dev->of_node, rknpu_dev->devfreq, &npu_cooling_power);
	if (IS_ERR_OR_NULL(rknpu_dev->devfreq_cooling))
		LOG_DEV_ERROR(dev, "failed to register cooling device\n");

out:
	return 0;
}

#else

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
	rknpu_dev->current_freq = clk_get_rate(rknpu_dev->clks[0].clk);
	rknpu_dev->current_volt = regulator_get_voltage(rknpu_dev->vdd);

	of_property_read_u32(dev->of_node, "dynamic-power-coefficient",
			     (u32 *)&npu_cooling_power.dyn_power_coeff);
	rknpu_dev->model_data =
		rockchip_ipa_power_model_init(dev, "npu_leakage");
	if (IS_ERR_OR_NULL(rknpu_dev->model_data)) {
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

	rknpu_dev->devfreq_cooling = of_devfreq_cooling_register_power(
		dev->of_node, rknpu_dev->devfreq, &npu_cooling_power);
	if (IS_ERR_OR_NULL(rknpu_dev->devfreq_cooling))
		LOG_DEV_ERROR(dev, "failed to register cooling device\n");

out:
	return 0;
}
#endif
#endif

static int rknpu_register_irq(struct platform_device *pdev,
			      struct rknpu_device *rknpu_dev)
{
	const struct rknpu_config *config = rknpu_dev->config;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int i, ret, irq;

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
					   config->irqs[0].name);
	if (res) {
		/* there are irq names in dts */
		for (i = 0; i < config->num_irqs; i++) {
			irq = platform_get_irq_byname(pdev,
						      config->irqs[i].name);
			if (irq < 0) {
				LOG_DEV_ERROR(dev, "no npu %s in dts\n",
					      config->irqs[i].name);
				return irq;
			}

			ret = devm_request_irq(dev, irq,
					       config->irqs[i].irq_hdl,
					       IRQF_SHARED, dev_name(dev),
					       rknpu_dev);
			if (ret < 0) {
				LOG_DEV_ERROR(dev, "request %s failed: %d\n",
					      config->irqs[i].name, ret);
				return ret;
			}
		}
	} else {
		/* no irq names in dts */
		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			LOG_DEV_ERROR(dev, "no npu irq in dts\n");
			return irq;
		}

		ret = devm_request_irq(dev, irq, rknpu_core0_irq_handler,
				       IRQF_SHARED, dev_name(dev), rknpu_dev);
		if (ret < 0) {
			LOG_DEV_ERROR(dev, "request irq failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int rknpu_probe(struct platform_device *pdev)
{
	struct resource *res = NULL;
	struct rknpu_device *rknpu_dev = NULL;
	struct device *dev = &pdev->dev;
	struct device *virt_dev = NULL;
	const struct of_device_id *match = NULL;
	const struct rknpu_config *config = NULL;
	int ret = -EINVAL, i = 0;

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
#ifndef FPGA_PLATFORM
		return -ENODEV;
#endif
	}

#ifndef FPGA_PLATFORM
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	if (strstr(__clk_get_name(rknpu_dev->clks[0].clk), "scmi"))
		rknpu_dev->opp_info.scmi_clk = rknpu_dev->clks[0].clk;
#endif

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

	rknpu_dev->mem = devm_regulator_get_optional(dev, "mem");
	if (IS_ERR(rknpu_dev->mem)) {
		if (PTR_ERR(rknpu_dev->mem) != -ENODEV) {
			ret = PTR_ERR(rknpu_dev->mem);
			LOG_DEV_ERROR(
				dev,
				"failed to get mem regulator for rknpu: %d\n",
				ret);
			return ret;
		}
		rknpu_dev->mem = NULL;
	}
#endif

	spin_lock_init(&rknpu_dev->lock);
	spin_lock_init(&rknpu_dev->irq_lock);
	mutex_init(&rknpu_dev->power_lock);
	for (i = 0; i < config->num_irqs; i++) {
		INIT_LIST_HEAD(&rknpu_dev->subcore_datas[i].todo_list);
		init_waitqueue_head(&rknpu_dev->subcore_datas[i].job_done_wq);
		rknpu_dev->subcore_datas[i].task_num = 0;
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			LOG_DEV_ERROR(
				dev,
				"failed to get memory resource for rknpu\n");
			return -ENXIO;
		}

		rknpu_dev->base[i] = devm_ioremap_resource(dev, res);
		if (PTR_ERR(rknpu_dev->base[i]) == -EBUSY) {
			rknpu_dev->base[i] = devm_ioremap(dev, res->start,
							  resource_size(res));
		}

		if (IS_ERR(rknpu_dev->base[i])) {
			LOG_DEV_ERROR(dev,
				      "failed to remap register for rknpu\n");
			return PTR_ERR(rknpu_dev->base[i]);
		}
	}

	if (config->bw_priority_length > 0) {
		rknpu_dev->bw_priority_base =
			devm_ioremap(dev, config->bw_priority_addr,
				     config->bw_priority_length);
		if (IS_ERR(rknpu_dev->bw_priority_base)) {
			LOG_DEV_ERROR(
				rknpu_dev->dev,
				"failed to remap bw priority register for rknpu\n");
			rknpu_dev->bw_priority_base = NULL;
		}
	}

	if (!rknpu_dev->bypass_irq_handler) {
		ret = rknpu_register_irq(pdev, rknpu_dev);
		if (ret)
			return ret;
	} else {
		LOG_DEV_WARN(dev, "bypass irq handler!\n");
	}

#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
	ret = rknpu_drm_probe(rknpu_dev);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to probe device for rknpu\n");
		return ret;
	}
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
	rknpu_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	rknpu_dev->miscdev.name = "rknpu";
	rknpu_dev->miscdev.fops = &rknpu_fops;

	ret = misc_register(&rknpu_dev->miscdev);
	if (ret) {
		LOG_DEV_ERROR(dev, "cannot register miscdev (%d)\n", ret);
		return ret;
	}

	rknpu_dev->heap = rk_dma_heap_find("rk-dma-heap-cma");
	if (!rknpu_dev->heap) {
		LOG_DEV_ERROR(dev, "failed to find cma heap\n");
		return -ENOMEM;
	}
	rk_dma_heap_set_dev(dev);
	LOG_DEV_INFO(dev, "Initialized %s: v%d.%d.%d for %s\n", DRIVER_DESC,
		     DRIVER_MAJOR, DRIVER_MINOR, DRIVER_PATCHLEVEL,
		     DRIVER_DATE);
#endif

#ifdef CONFIG_ROCKCHIP_RKNPU_FENCE
	ret = rknpu_fence_context_alloc(rknpu_dev);
	if (ret) {
		LOG_DEV_ERROR(dev,
			      "failed to allocate fence context for rknpu\n");
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
		goto err_remove_drm;
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
		goto err_remove_misc;
#endif
	}
#endif

	platform_set_drvdata(pdev, rknpu_dev);

	pm_runtime_enable(dev);

	if (of_count_phandle_with_args(dev->of_node, "power-domains",
				       "#power-domain-cells") > 1) {
		virt_dev = dev_pm_domain_attach_by_name(dev, "npu0");
		if (!IS_ERR(virt_dev))
			rknpu_dev->genpd_dev_npu0 = virt_dev;
		virt_dev = dev_pm_domain_attach_by_name(dev, "npu1");
		if (!IS_ERR(virt_dev))
			rknpu_dev->genpd_dev_npu1 = virt_dev;
		virt_dev = dev_pm_domain_attach_by_name(dev, "npu2");
		if (!IS_ERR(virt_dev))
			rknpu_dev->genpd_dev_npu2 = virt_dev;
		rknpu_dev->multiple_domains = true;
	}

	ret = rknpu_power_on(rknpu_dev);
	if (ret) {
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
		goto err_remove_drm;
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
		goto err_remove_misc;
#endif
	}

#ifndef FPGA_PLATFORM
	rknpu_devfreq_init(rknpu_dev);
#endif

	rknpu_dev->power_off_wq =
		create_freezable_workqueue("rknpu_power_off_wq");
	if (!rknpu_dev->power_off_wq) {
		LOG_DEV_ERROR(dev, "rknpu couldn't create power_off workqueue");
		ret = -ENOMEM;
		goto err_remove_wq;
	}
	INIT_DEFERRABLE_WORK(&rknpu_dev->power_off_work,
			     rknpu_power_off_delay_work);
	rknpu_power_off(rknpu_dev);
	rknpu_dev->is_powered = false;
	atomic_set(&rknpu_dev->power_refcount, 0);
	atomic_set(&rknpu_dev->cmdline_power_refcount, 0);

	rknpu_debugger_init(rknpu_dev);
	rknpu_init_timer(rknpu_dev);

	return 0;

#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
err_remove_drm:
	rknpu_drm_remove(rknpu_dev);
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
err_remove_misc:
	misc_deregister(&(rknpu_dev->miscdev));
#endif
err_remove_wq:
	destroy_workqueue(rknpu_dev->power_off_wq);
	return ret;
}

static int rknpu_remove(struct platform_device *pdev)
{
	struct rknpu_device *rknpu_dev = platform_get_drvdata(pdev);
	int i = 0;

	cancel_delayed_work_sync(&rknpu_dev->power_off_work);
	destroy_workqueue(rknpu_dev->power_off_wq);

	rknpu_debugger_remove(rknpu_dev);
	rknpu_cancel_timer(rknpu_dev);

	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		WARN_ON(rknpu_dev->subcore_datas[i].job);
		WARN_ON(!list_empty(&rknpu_dev->subcore_datas[i].todo_list));
	}

#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
	rknpu_drm_remove(rknpu_dev);
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
	misc_deregister(&(rknpu_dev->miscdev));
#endif
	if (rknpu_dev->is_powered)
		rknpu_power_off(rknpu_dev);

	if (rknpu_dev->multiple_domains) {
		if (rknpu_dev->genpd_dev_npu0)
			dev_pm_domain_detach(rknpu_dev->genpd_dev_npu0, true);
		if (rknpu_dev->genpd_dev_npu1)
			dev_pm_domain_detach(rknpu_dev->genpd_dev_npu1, true);
		if (rknpu_dev->genpd_dev_npu2)
			dev_pm_domain_detach(rknpu_dev->genpd_dev_npu2, true);
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifndef FPGA_PLATFORM
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
static int rknpu_runtime_suspend(struct device *dev)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev);
	struct rockchip_opp_info *opp_info = &rknpu_dev->opp_info;

	if (opp_info->scmi_clk) {
		if (clk_set_rate(opp_info->scmi_clk, POWER_DOWN_FREQ))
			LOG_DEV_ERROR(dev, "failed to restore clk rate\n");
	}
	opp_info->current_rm = UINT_MAX;

	return 0;
}

static int rknpu_runtime_resume(struct device *dev)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev);
	struct rockchip_opp_info *opp_info = &rknpu_dev->opp_info;
	int ret = 0;

	if (!rknpu_dev->current_freq || !rknpu_dev->current_volt)
		return 0;

	ret = clk_bulk_prepare_enable(opp_info->num_clks, opp_info->clks);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to enable opp clks\n");
		return ret;
	}

	if (opp_info->data && opp_info->data->set_read_margin)
		opp_info->data->set_read_margin(dev, opp_info,
						opp_info->target_rm);
	if (opp_info->scmi_clk) {
		if (clk_set_rate(opp_info->scmi_clk, rknpu_dev->current_freq))
			LOG_DEV_ERROR(dev, "failed to set power down rate\n");
	}

	clk_bulk_disable_unprepare(opp_info->num_clks, opp_info->clks);

	return ret;
}

static const struct dev_pm_ops rknpu_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
		SET_RUNTIME_PM_OPS(rknpu_runtime_suspend, rknpu_runtime_resume,
				   NULL)
};
#endif
#endif

static struct platform_driver rknpu_driver = {
	.probe = rknpu_probe,
	.remove = rknpu_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "RKNPU",
#ifndef FPGA_PLATFORM
#if KERNEL_VERSION(5, 5, 0) < LINUX_VERSION_CODE
		.pm = &rknpu_pm_ops,
#endif
#endif
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
