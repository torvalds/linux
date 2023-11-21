// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
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
#include <linux/regmap.h>
#include <linux/of_address.h>

#ifndef FPGA_PLATFORM
#include <soc/rockchip/rockchip_iommu.h>
#endif

#include "rknpu_ioctl.h"
#include "rknpu_reset.h"
#include "rknpu_fence.h"
#include "rknpu_drv.h"
#include "rknpu_gem.h"
#include "rknpu_devfreq.h"

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

struct rknpu_irqs_data {
	const char *name;
	irqreturn_t (*irq_hdl)(int irq, void *ctx);
};

static const struct rknpu_irqs_data rknpu_irqs[] = {
	{ "npu_irq", rknpu_core0_irq_handler }
};

static const struct rknpu_irqs_data rk3588_npu_irqs[] = {
	{ "npu0_irq", rknpu_core0_irq_handler },
	{ "npu1_irq", rknpu_core1_irq_handler },
	{ "npu2_irq", rknpu_core2_irq_handler }
};

static const struct rknpu_reset_data rknpu_resets[] = { { "srst_a",
							  "srst_h" } };

static const struct rknpu_reset_data rk3588_npu_resets[] = {
	{ "srst_a0", "srst_h0" },
	{ "srst_a1", "srst_h1" },
	{ "srst_a2", "srst_h2" }
};

static const struct rknpu_config rk356x_rknpu_config = {
	.bw_priority_addr = 0xfe180008,
	.bw_priority_length = 0x10,
	.dma_mask = DMA_BIT_MASK(32),
	.pc_data_amount_scale = 1,
	.pc_task_number_bits = 12,
	.pc_task_number_mask = 0xfff,
	.pc_task_status_offset = 0x3c,
	.pc_dma_ctrl = 0,
	.bw_enable = 1,
	.irqs = rknpu_irqs,
	.resets = rknpu_resets,
	.num_irqs = ARRAY_SIZE(rknpu_irqs),
	.num_resets = ARRAY_SIZE(rknpu_resets),
	.nbuf_phyaddr = 0,
	.nbuf_size = 0,
	.max_submit_number = (1 << 12) - 1,
	.core_mask = 0x1,
};

static const struct rknpu_config rk3588_rknpu_config = {
	.bw_priority_addr = 0x0,
	.bw_priority_length = 0x0,
	.dma_mask = DMA_BIT_MASK(40),
	.pc_data_amount_scale = 2,
	.pc_task_number_bits = 12,
	.pc_task_number_mask = 0xfff,
	.pc_task_status_offset = 0x3c,
	.pc_dma_ctrl = 0,
	.bw_enable = 0,
	.irqs = rk3588_npu_irqs,
	.resets = rk3588_npu_resets,
	.num_irqs = ARRAY_SIZE(rk3588_npu_irqs),
	.num_resets = ARRAY_SIZE(rk3588_npu_resets),
	.nbuf_phyaddr = 0,
	.nbuf_size = 0,
	.max_submit_number = (1 << 12) - 1,
	.core_mask = 0x7,
};

static const struct rknpu_config rk3583_rknpu_config = {
	.bw_priority_addr = 0x0,
	.bw_priority_length = 0x0,
	.dma_mask = DMA_BIT_MASK(40),
	.pc_data_amount_scale = 2,
	.pc_task_number_bits = 12,
	.pc_task_number_mask = 0xfff,
	.pc_task_status_offset = 0x3c,
	.pc_dma_ctrl = 0,
	.bw_enable = 0,
	.irqs = rk3588_npu_irqs,
	.resets = rk3588_npu_resets,
	.num_irqs = 2,
	.num_resets = 2,
	.nbuf_phyaddr = 0,
	.nbuf_size = 0,
	.max_submit_number = (1 << 12) - 1,
	.core_mask = 0x3,
};

static const struct rknpu_config rv1106_rknpu_config = {
	.bw_priority_addr = 0x0,
	.bw_priority_length = 0x0,
	.dma_mask = DMA_BIT_MASK(32),
	.pc_data_amount_scale = 2,
	.pc_task_number_bits = 16,
	.pc_task_number_mask = 0xffff,
	.pc_task_status_offset = 0x3c,
	.pc_dma_ctrl = 0,
	.bw_enable = 1,
	.irqs = rknpu_irqs,
	.resets = rknpu_resets,
	.num_irqs = ARRAY_SIZE(rknpu_irqs),
	.num_resets = ARRAY_SIZE(rknpu_resets),
	.nbuf_phyaddr = 0,
	.nbuf_size = 0,
	.max_submit_number = (1 << 16) - 1,
	.core_mask = 0x1,
};

static const struct rknpu_config rk3562_rknpu_config = {
	.bw_priority_addr = 0x0,
	.bw_priority_length = 0x0,
	.dma_mask = DMA_BIT_MASK(40),
	.pc_data_amount_scale = 2,
	.pc_task_number_bits = 16,
	.pc_task_number_mask = 0xffff,
	.pc_task_status_offset = 0x48,
	.pc_dma_ctrl = 1,
	.bw_enable = 1,
	.irqs = rknpu_irqs,
	.resets = rknpu_resets,
	.num_irqs = ARRAY_SIZE(rknpu_irqs),
	.num_resets = ARRAY_SIZE(rknpu_resets),
	.nbuf_phyaddr = 0xfe400000,
	.nbuf_size = 256 * 1024,
	.max_submit_number = (1 << 16) - 1,
	.core_mask = 0x1,
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
	{
		.compatible = "rockchip,rk3562-rknpu",
		.data = &rk3562_rknpu_config,
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
	if (atomic_dec_if_positive(&rknpu_dev->power_refcount) == 0)
		rknpu_power_off(rknpu_dev);
	mutex_unlock(&rknpu_dev->power_lock);
}

int rknpu_power_get(struct rknpu_device *rknpu_dev)
{
	int ret = 0;

	mutex_lock(&rknpu_dev->power_lock);
	if (atomic_inc_return(&rknpu_dev->power_refcount) == 1)
		ret = rknpu_power_on(rknpu_dev);
	mutex_unlock(&rknpu_dev->power_lock);

	return ret;
}

int rknpu_power_put(struct rknpu_device *rknpu_dev)
{
	int ret = 0;

	mutex_lock(&rknpu_dev->power_lock);
	if (atomic_dec_if_positive(&rknpu_dev->power_refcount) == 0)
		ret = rknpu_power_off(rknpu_dev);
	mutex_unlock(&rknpu_dev->power_lock);

	return ret;
}

static int rknpu_power_put_delay(struct rknpu_device *rknpu_dev)
{
	if (rknpu_dev->power_put_delay == 0)
		return rknpu_power_put(rknpu_dev);

	mutex_lock(&rknpu_dev->power_lock);
	if (atomic_read(&rknpu_dev->power_refcount) == 1)
		queue_delayed_work(
			rknpu_dev->power_off_wq, &rknpu_dev->power_off_work,
			msecs_to_jiffies(rknpu_dev->power_put_delay));
	else
		atomic_dec_if_positive(&rknpu_dev->power_refcount);
	mutex_unlock(&rknpu_dev->power_lock);

	return 0;
}

static int rknpu_action(struct rknpu_device *rknpu_dev,
			struct rknpu_action *args)
{
	int ret = -EINVAL;

	switch (args->flags) {
	case RKNPU_GET_HW_VERSION:
		ret = rknpu_get_hw_version(rknpu_dev, &args->value);
		break;
	case RKNPU_GET_DRV_VERSION:
		ret = rknpu_get_drv_version(&args->value);
		break;
	case RKNPU_GET_FREQ:
#ifndef FPGA_PLATFORM
		args->value = clk_get_rate(rknpu_dev->clks[0].clk);
#endif
		ret = 0;
		break;
	case RKNPU_SET_FREQ:
		break;
	case RKNPU_GET_VOLT:
#ifndef FPGA_PLATFORM
		args->value = regulator_get_voltage(rknpu_dev->vdd);
#endif
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
	case RKNPU_GET_TOTAL_SRAM_SIZE:
		if (rknpu_dev->sram_mm)
			args->value = rknpu_dev->sram_mm->total_chunks *
				      rknpu_dev->sram_mm->chunk_size;
		else
			args->value = 0;
		ret = 0;
		break;
	case RKNPU_GET_FREE_SRAM_SIZE:
		if (rknpu_dev->sram_mm)
			args->value = rknpu_dev->sram_mm->free_chunks *
				      rknpu_dev->sram_mm->chunk_size;
		else
			args->value = 0;
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
	struct rknpu_device *rknpu_dev =
		container_of(file->private_data, struct rknpu_device, miscdev);
	struct rknpu_session *session = NULL;

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session) {
		LOG_ERROR("rknpu session alloc failed\n");
		return -ENOMEM;
	}

	session->rknpu_dev = rknpu_dev;
	INIT_LIST_HEAD(&session->list);

	file->private_data = (void *)session;

	return nonseekable_open(inode, file);
}

static int rknpu_release(struct inode *inode, struct file *file)
{
	struct rknpu_mem_object *entry;
	struct rknpu_session *session = file->private_data;
	struct rknpu_device *rknpu_dev = session->rknpu_dev;
	LIST_HEAD(local_list);

	spin_lock(&rknpu_dev->lock);
	list_replace_init(&session->list, &local_list);
	file->private_data = NULL;
	spin_unlock(&rknpu_dev->lock);

	while (!list_empty(&local_list)) {
		entry = list_first_entry(&local_list, struct rknpu_mem_object,
					 head);

		LOG_DEBUG(
			"Fd close free rknpu_obj: %#llx, rknpu_obj->dma_addr: %#llx\n",
			(__u64)(uintptr_t)entry, (__u64)entry->dma_addr);

		vunmap(entry->kv_addr);
		entry->kv_addr = NULL;

		if (!entry->owner)
			dma_buf_put(entry->dmabuf);

		list_del(&entry->head);
		kfree(entry);
	}

	kfree(session);

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
	long ret = -EINVAL;
	struct rknpu_device *rknpu_dev = NULL;

	if (!file->private_data)
		return -EINVAL;

	rknpu_dev = ((struct rknpu_session *)file->private_data)->rknpu_dev;

	rknpu_power_get(rknpu_dev);

	switch (cmd) {
	case IOCTL_RKNPU_ACTION:
		ret = rknpu_action_ioctl(rknpu_dev, arg);
		break;
	case IOCTL_RKNPU_SUBMIT:
		ret = rknpu_submit_ioctl(rknpu_dev, arg);
		break;
	case IOCTL_RKNPU_MEM_CREATE:
		ret = rknpu_mem_create_ioctl(rknpu_dev, arg, file);
		break;
	case RKNPU_MEM_MAP:
		break;
	case IOCTL_RKNPU_MEM_DESTROY:
		ret = rknpu_mem_destroy_ioctl(rknpu_dev, arg, file);
		break;
	case IOCTL_RKNPU_MEM_SYNC:
		ret = rknpu_mem_sync_ioctl(rknpu_dev, arg);
		break;
	default:
		break;
	}

	rknpu_power_put_delay(rknpu_dev);

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
#if KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE
static const struct vm_operations_struct rknpu_gem_vm_ops = {
	.fault = rknpu_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};
#endif

static int rknpu_action_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev->dev);

	return rknpu_action(rknpu_dev, (struct rknpu_action *)data);
}

#define RKNPU_IOCTL(func)                                                      \
	static int __##func(struct drm_device *dev, void *data,                \
			    struct drm_file *file_priv)                        \
	{                                                                      \
		struct rknpu_device *rknpu_dev = dev_get_drvdata(dev->dev);    \
		int ret = -EINVAL;                                             \
		rknpu_power_get(rknpu_dev);                                    \
		ret = func(dev, data, file_priv);                              \
		rknpu_power_put_delay(rknpu_dev);                              \
		return ret;                                                    \
	}

RKNPU_IOCTL(rknpu_action_ioctl);
RKNPU_IOCTL(rknpu_submit_ioctl);
RKNPU_IOCTL(rknpu_gem_create_ioctl);
RKNPU_IOCTL(rknpu_gem_map_ioctl);
RKNPU_IOCTL(rknpu_gem_destroy_ioctl);
RKNPU_IOCTL(rknpu_gem_sync_ioctl);

static const struct drm_ioctl_desc rknpu_ioctls[] = {
	DRM_IOCTL_DEF_DRV(RKNPU_ACTION, __rknpu_action_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_SUBMIT, __rknpu_submit_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_MEM_CREATE, __rknpu_gem_create_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_MEM_MAP, __rknpu_gem_map_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_MEM_DESTROY, __rknpu_gem_destroy_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RKNPU_MEM_SYNC, __rknpu_gem_sync_ioctl,
			  DRM_RENDER_ALLOW),
};

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
DEFINE_DRM_GEM_FOPS(rknpu_drm_driver_fops);
#else
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
#endif

static struct drm_driver rknpu_drm_driver = {
#if KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE
	.driver_features = DRIVER_GEM | DRIVER_RENDER,
#else
	.driver_features = DRIVER_GEM | DRIVER_PRIME | DRIVER_RENDER,
#endif
#if KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE
	.gem_free_object_unlocked = rknpu_gem_free_object,
	.gem_vm_ops = &rknpu_gem_vm_ops,
	.dumb_destroy = drm_gem_dumb_destroy,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_get_sg_table = rknpu_gem_prime_get_sg_table,
	.gem_prime_vmap = rknpu_gem_prime_vmap,
	.gem_prime_vunmap = rknpu_gem_prime_vunmap,
#endif
	.dumb_create = rknpu_gem_dumb_create,
#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
	.dumb_map_offset = rknpu_gem_dumb_map_offset,
#else
	.dumb_map_offset = drm_gem_dumb_map_offset,
#endif
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
	.gem_prime_import = rknpu_gem_prime_import,
#else
	.gem_prime_import = drm_gem_prime_import,
#endif
	.gem_prime_import_sg_table = rknpu_gem_prime_import_sg_table,
#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
	.gem_prime_mmap = drm_gem_prime_mmap,
#else
	.gem_prime_mmap = rknpu_gem_prime_mmap,
#endif
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

#endif

static enum hrtimer_restart hrtimer_handler(struct hrtimer *timer)
{
	struct rknpu_device *rknpu_dev =
		container_of(timer, struct rknpu_device, timer);
	struct rknpu_subcore_data *subcore_data = NULL;
	struct rknpu_job *job = NULL;
	ktime_t now;
	unsigned long flags;
	int i;

	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		subcore_data = &rknpu_dev->subcore_datas[i];

		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);

		job = subcore_data->job;
		if (job) {
			now = ktime_get();
			subcore_data->timer.busy_time +=
				ktime_sub(now, job->hw_recoder_time);
			job->hw_recoder_time = now;
		}

		subcore_data->timer.total_busy_time =
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
				"failed to enable vdd reg for rknpu, ret: %d\n",
				ret);
			return ret;
		}
	}

	if (rknpu_dev->mem) {
		ret = regulator_enable(rknpu_dev->mem);
		if (ret) {
			LOG_DEV_ERROR(
				dev,
				"failed to enable mem reg for rknpu, ret: %d\n",
				ret);
			return ret;
		}
	}
#endif

	ret = clk_bulk_prepare_enable(rknpu_dev->num_clks, rknpu_dev->clks);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to enable clk for rknpu, ret: %d\n",
			      ret);
		return ret;
	}

#ifndef FPGA_PLATFORM
	rknpu_devfreq_lock(rknpu_dev);
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
					"failed to get pm runtime for npu0, ret: %d\n",
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
					"failed to get pm runtime for npu1, ret: %d\n",
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
					"failed to get pm runtime for npu2, ret: %d\n",
					ret);
				goto out;
			}
		}
	}
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		LOG_DEV_ERROR(dev,
			      "failed to get pm runtime for rknpu, ret: %d\n",
			      ret);
	}

out:
#ifndef FPGA_PLATFORM
	rknpu_devfreq_unlock(rknpu_dev);
#endif

	return ret;
}

static int rknpu_power_off(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;

#ifndef FPGA_PLATFORM
	int ret;
	bool val;

	rknpu_devfreq_lock(rknpu_dev);
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
			rknpu_devfreq_unlock(rknpu_dev);
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
	rknpu_devfreq_unlock(rknpu_dev);
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

static int rknpu_find_sram_resource(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;
	struct device_node *sram_node = NULL;
	struct resource sram_res;
	uint32_t sram_size = 0;
	int ret = -EINVAL;

	/* get sram device node */
	sram_node = of_parse_phandle(dev->of_node, "rockchip,sram", 0);
	rknpu_dev->sram_size = 0;
	if (!sram_node)
		return -EINVAL;

	/* get sram start and size */
	ret = of_address_to_resource(sram_node, 0, &sram_res);
	of_node_put(sram_node);
	if (ret)
		return ret;

	/* check sram start and size is PAGE_SIZE align */
	rknpu_dev->sram_start = round_up(sram_res.start, PAGE_SIZE);
	rknpu_dev->sram_end = round_down(
		sram_res.start + resource_size(&sram_res), PAGE_SIZE);
	if (rknpu_dev->sram_end <= rknpu_dev->sram_start) {
		LOG_DEV_WARN(
			dev,
			"invalid sram resource, sram start %pa, sram end %pa\n",
			&rknpu_dev->sram_start, &rknpu_dev->sram_end);
		return -EINVAL;
	}

	sram_size = rknpu_dev->sram_end - rknpu_dev->sram_start;

	rknpu_dev->sram_base_io =
		devm_ioremap(dev, rknpu_dev->sram_start, sram_size);
	if (IS_ERR(rknpu_dev->sram_base_io)) {
		LOG_DEV_ERROR(dev, "failed to remap sram base io!\n");
		rknpu_dev->sram_base_io = NULL;
	}

	rknpu_dev->sram_size = sram_size;

	LOG_DEV_INFO(dev, "sram region: [%pa, %pa), sram size: %#x\n",
		     &rknpu_dev->sram_start, &rknpu_dev->sram_end,
		     rknpu_dev->sram_size);

	return 0;
}

static int rknpu_find_nbuf_resource(struct rknpu_device *rknpu_dev)
{
	struct device *dev = rknpu_dev->dev;

	if (rknpu_dev->config->nbuf_size == 0)
		return -EINVAL;

	rknpu_dev->nbuf_start = rknpu_dev->config->nbuf_phyaddr;
	rknpu_dev->nbuf_size = rknpu_dev->config->nbuf_size;
	rknpu_dev->nbuf_base_io =
		devm_ioremap(dev, rknpu_dev->nbuf_start, rknpu_dev->nbuf_size);
	if (IS_ERR(rknpu_dev->nbuf_base_io)) {
		LOG_DEV_ERROR(dev, "failed to remap nbuf base io!\n");
		rknpu_dev->nbuf_base_io = NULL;
	}

	rknpu_dev->nbuf_end = rknpu_dev->nbuf_start + rknpu_dev->nbuf_size;

	LOG_DEV_INFO(dev, "nbuf region: [%pa, %pa), nbuf size: %#x\n",
		     &rknpu_dev->nbuf_start, &rknpu_dev->nbuf_end,
		     rknpu_dev->nbuf_size);

	return 0;
}

static int rknpu_get_invalid_core_mask(struct device *dev)
{
	int ret = 0;
	u8 invalid_core_mask = 0;

	if (of_property_match_string(dev->of_node, "nvmem-cell-names",
				     "cores") >= 0) {
		ret = rockchip_nvmem_cell_read_u8(dev->of_node, "cores",
						  &invalid_core_mask);
		/* The default valid npu cores for RK3583 are core0 and core1 */
		invalid_core_mask |= RKNPU_CORE2_MASK;
		if (ret) {
			LOG_DEV_ERROR(
				dev,
				"failed to get specification_serial_number\n");
			return invalid_core_mask;
		}
	}

	return (int)invalid_core_mask;
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

	if (match->data == (void *)&rk3588_rknpu_config) {
		int invalid_core_mask = rknpu_get_invalid_core_mask(dev);
		/* The default valid npu cores for RK3583 are core0 and core1 */
		if (invalid_core_mask & RKNPU_CORE2_MASK) {
			if ((invalid_core_mask & RKNPU_CORE0_MASK) ||
			    (invalid_core_mask & RKNPU_CORE1_MASK)) {
				LOG_DEV_ERROR(
					dev,
					"rknpu core invalid, invalid core mask: %#x\n",
					invalid_core_mask);
				return -ENODEV;
			}
			config = &rk3583_rknpu_config;
		}
	}

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
	mutex_init(&rknpu_dev->reset_lock);
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
		goto err_remove_drv;
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
		if (config->num_irqs > 2) {
			virt_dev = dev_pm_domain_attach_by_name(dev, "npu2");
			if (!IS_ERR(virt_dev))
				rknpu_dev->genpd_dev_npu2 = virt_dev;
		}
		rknpu_dev->multiple_domains = true;
	}

	ret = rknpu_power_on(rknpu_dev);
	if (ret)
		goto err_remove_drv;

#ifndef FPGA_PLATFORM
	rknpu_devfreq_init(rknpu_dev);
#endif

	// set default power put delay to 3s
	rknpu_dev->power_put_delay = 3000;
	rknpu_dev->power_off_wq =
		create_freezable_workqueue("rknpu_power_off_wq");
	if (!rknpu_dev->power_off_wq) {
		LOG_DEV_ERROR(dev, "rknpu couldn't create power_off workqueue");
		ret = -ENOMEM;
		goto err_devfreq_remove;
	}
	INIT_DEFERRABLE_WORK(&rknpu_dev->power_off_work,
			     rknpu_power_off_delay_work);

	if (IS_ENABLED(CONFIG_NO_GKI) &&
	    IS_ENABLED(CONFIG_ROCKCHIP_RKNPU_SRAM) && rknpu_dev->iommu_en) {
		if (!rknpu_find_sram_resource(rknpu_dev)) {
			ret = rknpu_mm_create(rknpu_dev->sram_size, PAGE_SIZE,
					      &rknpu_dev->sram_mm);
			if (ret != 0)
				goto err_remove_wq;
		} else {
			LOG_DEV_WARN(dev, "could not find sram resource!\n");
		}
	}

	if (IS_ENABLED(CONFIG_NO_GKI) && rknpu_dev->iommu_en &&
	    rknpu_dev->config->nbuf_size > 0)
		rknpu_find_nbuf_resource(rknpu_dev);

	rknpu_power_off(rknpu_dev);
	atomic_set(&rknpu_dev->power_refcount, 0);
	atomic_set(&rknpu_dev->cmdline_power_refcount, 0);

	rknpu_debugger_init(rknpu_dev);
	rknpu_init_timer(rknpu_dev);

	return 0;

err_remove_wq:
	destroy_workqueue(rknpu_dev->power_off_wq);

err_devfreq_remove:
#ifndef FPGA_PLATFORM
	rknpu_devfreq_remove(rknpu_dev);
#endif

err_remove_drv:
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
	rknpu_drm_remove(rknpu_dev);
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
	misc_deregister(&(rknpu_dev->miscdev));
#endif

	return ret;
}

static int rknpu_remove(struct platform_device *pdev)
{
	struct rknpu_device *rknpu_dev = platform_get_drvdata(pdev);
	int i = 0;

	cancel_delayed_work_sync(&rknpu_dev->power_off_work);
	destroy_workqueue(rknpu_dev->power_off_wq);

	if (IS_ENABLED(CONFIG_ROCKCHIP_RKNPU_SRAM) && rknpu_dev->sram_mm)
		rknpu_mm_destroy(rknpu_dev->sram_mm);

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

#ifndef FPGA_PLATFORM
	rknpu_devfreq_remove(rknpu_dev);
#endif

	mutex_lock(&rknpu_dev->power_lock);
	if (atomic_read(&rknpu_dev->power_refcount) > 0)
		rknpu_power_off(rknpu_dev);
	mutex_unlock(&rknpu_dev->power_lock);

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
static int rknpu_runtime_suspend(struct device *dev)
{
	return rknpu_devfreq_runtime_suspend(dev);
}

static int rknpu_runtime_resume(struct device *dev)
{
	return rknpu_devfreq_runtime_resume(dev);
}

static const struct dev_pm_ops rknpu_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
		SET_RUNTIME_PM_OPS(rknpu_runtime_suspend, rknpu_runtime_resume,
				   NULL)
};
#endif

static struct platform_driver rknpu_driver = {
	.probe = rknpu_probe,
	.remove = rknpu_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "RKNPU",
#ifndef FPGA_PLATFORM
		.pm = &rknpu_pm_ops,
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
#if KERNEL_VERSION(5, 16, 0) < LINUX_VERSION_CODE
MODULE_IMPORT_NS(DMA_BUF);
#endif
