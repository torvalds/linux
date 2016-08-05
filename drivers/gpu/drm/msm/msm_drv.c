/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "msm_drv.h"
#include "msm_debugfs.h"
#include "msm_fence.h"
#include "msm_gpu.h"
#include "msm_kms.h"


/*
 * MSM driver version:
 * - 1.0.0 - initial interface
 * - 1.1.0 - adds madvise, and support for submits with > 4 cmd buffers
 */
#define MSM_VERSION_MAJOR	1
#define MSM_VERSION_MINOR	1
#define MSM_VERSION_PATCHLEVEL	0

static void msm_fb_output_poll_changed(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	if (priv->fbdev)
		drm_fb_helper_hotplug_event(priv->fbdev);
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = msm_framebuffer_create,
	.output_poll_changed = msm_fb_output_poll_changed,
	.atomic_check = msm_atomic_check,
	.atomic_commit = msm_atomic_commit,
};

int msm_register_mmu(struct drm_device *dev, struct msm_mmu *mmu)
{
	struct msm_drm_private *priv = dev->dev_private;
	int idx = priv->num_mmus++;

	if (WARN_ON(idx >= ARRAY_SIZE(priv->mmus)))
		return -EINVAL;

	priv->mmus[idx] = mmu;

	return idx;
}

#ifdef CONFIG_DRM_MSM_REGISTER_LOGGING
static bool reglog = false;
MODULE_PARM_DESC(reglog, "Enable register read/write logging");
module_param(reglog, bool, 0600);
#else
#define reglog 0
#endif

#ifdef CONFIG_DRM_FBDEV_EMULATION
static bool fbdev = true;
MODULE_PARM_DESC(fbdev, "Enable fbdev compat layer");
module_param(fbdev, bool, 0600);
#endif

static char *vram = "16m";
MODULE_PARM_DESC(vram, "Configure VRAM size (for devices without IOMMU/GPUMMU)");
module_param(vram, charp, 0);

/*
 * Util/helpers:
 */

void __iomem *msm_ioremap(struct platform_device *pdev, const char *name,
		const char *dbgname)
{
	struct resource *res;
	unsigned long size;
	void __iomem *ptr;

	if (name)
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	else
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		dev_err(&pdev->dev, "failed to get memory resource: %s\n", name);
		return ERR_PTR(-EINVAL);
	}

	size = resource_size(res);

	ptr = devm_ioremap_nocache(&pdev->dev, res->start, size);
	if (!ptr) {
		dev_err(&pdev->dev, "failed to ioremap: %s\n", name);
		return ERR_PTR(-ENOMEM);
	}

	if (reglog)
		printk(KERN_DEBUG "IO:region %s %p %08lx\n", dbgname, ptr, size);

	return ptr;
}

void msm_writel(u32 data, void __iomem *addr)
{
	if (reglog)
		printk(KERN_DEBUG "IO:W %p %08x\n", addr, data);
	writel(data, addr);
}

u32 msm_readl(const void __iomem *addr)
{
	u32 val = readl(addr);
	if (reglog)
		printk(KERN_ERR "IO:R %p %08x\n", addr, val);
	return val;
}

struct vblank_event {
	struct list_head node;
	int crtc_id;
	bool enable;
};

static void vblank_ctrl_worker(struct work_struct *work)
{
	struct msm_vblank_ctrl *vbl_ctrl = container_of(work,
						struct msm_vblank_ctrl, work);
	struct msm_drm_private *priv = container_of(vbl_ctrl,
					struct msm_drm_private, vblank_ctrl);
	struct msm_kms *kms = priv->kms;
	struct vblank_event *vbl_ev, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&vbl_ctrl->lock, flags);
	list_for_each_entry_safe(vbl_ev, tmp, &vbl_ctrl->event_list, node) {
		list_del(&vbl_ev->node);
		spin_unlock_irqrestore(&vbl_ctrl->lock, flags);

		if (vbl_ev->enable)
			kms->funcs->enable_vblank(kms,
						priv->crtcs[vbl_ev->crtc_id]);
		else
			kms->funcs->disable_vblank(kms,
						priv->crtcs[vbl_ev->crtc_id]);

		kfree(vbl_ev);

		spin_lock_irqsave(&vbl_ctrl->lock, flags);
	}

	spin_unlock_irqrestore(&vbl_ctrl->lock, flags);
}

static int vblank_ctrl_queue_work(struct msm_drm_private *priv,
					int crtc_id, bool enable)
{
	struct msm_vblank_ctrl *vbl_ctrl = &priv->vblank_ctrl;
	struct vblank_event *vbl_ev;
	unsigned long flags;

	vbl_ev = kzalloc(sizeof(*vbl_ev), GFP_ATOMIC);
	if (!vbl_ev)
		return -ENOMEM;

	vbl_ev->crtc_id = crtc_id;
	vbl_ev->enable = enable;

	spin_lock_irqsave(&vbl_ctrl->lock, flags);
	list_add_tail(&vbl_ev->node, &vbl_ctrl->event_list);
	spin_unlock_irqrestore(&vbl_ctrl->lock, flags);

	queue_work(priv->wq, &vbl_ctrl->work);

	return 0;
}

static int msm_drm_uninit(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *ddev = platform_get_drvdata(pdev);
	struct msm_drm_private *priv = ddev->dev_private;
	struct msm_kms *kms = priv->kms;
	struct msm_gpu *gpu = priv->gpu;
	struct msm_vblank_ctrl *vbl_ctrl = &priv->vblank_ctrl;
	struct vblank_event *vbl_ev, *tmp;

	/* We must cancel and cleanup any pending vblank enable/disable
	 * work before drm_irq_uninstall() to avoid work re-enabling an
	 * irq after uninstall has disabled it.
	 */
	cancel_work_sync(&vbl_ctrl->work);
	list_for_each_entry_safe(vbl_ev, tmp, &vbl_ctrl->event_list, node) {
		list_del(&vbl_ev->node);
		kfree(vbl_ev);
	}

	msm_gem_shrinker_cleanup(ddev);

	drm_kms_helper_poll_fini(ddev);

	drm_dev_unregister(ddev);

#ifdef CONFIG_DRM_FBDEV_EMULATION
	if (fbdev && priv->fbdev)
		msm_fbdev_free(ddev);
#endif
	drm_mode_config_cleanup(ddev);

	pm_runtime_get_sync(dev);
	drm_irq_uninstall(ddev);
	pm_runtime_put_sync(dev);

	flush_workqueue(priv->wq);
	destroy_workqueue(priv->wq);

	flush_workqueue(priv->atomic_wq);
	destroy_workqueue(priv->atomic_wq);

	if (kms)
		kms->funcs->destroy(kms);

	if (gpu) {
		mutex_lock(&ddev->struct_mutex);
		gpu->funcs->pm_suspend(gpu);
		mutex_unlock(&ddev->struct_mutex);
		gpu->funcs->destroy(gpu);
	}

	if (priv->vram.paddr) {
		DEFINE_DMA_ATTRS(attrs);
		dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &attrs);
		drm_mm_takedown(&priv->vram.mm);
		dma_free_attrs(dev, priv->vram.size, NULL,
			       priv->vram.paddr, &attrs);
	}

	component_unbind_all(dev, ddev);

	msm_mdss_destroy(ddev);

	ddev->dev_private = NULL;
	drm_dev_unref(ddev);

	kfree(priv);

	return 0;
}

static int get_mdp_ver(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return (int) (unsigned long) of_device_get_match_data(dev);
}

#include <linux/of_address.h>

static int msm_init_vram(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct device_node *node;
	unsigned long size = 0;
	int ret = 0;

	/* In the device-tree world, we could have a 'memory-region'
	 * phandle, which gives us a link to our "vram".  Allocating
	 * is all nicely abstracted behind the dma api, but we need
	 * to know the entire size to allocate it all in one go. There
	 * are two cases:
	 *  1) device with no IOMMU, in which case we need exclusive
	 *     access to a VRAM carveout big enough for all gpu
	 *     buffers
	 *  2) device with IOMMU, but where the bootloader puts up
	 *     a splash screen.  In this case, the VRAM carveout
	 *     need only be large enough for fbdev fb.  But we need
	 *     exclusive access to the buffer to avoid the kernel
	 *     using those pages for other purposes (which appears
	 *     as corruption on screen before we have a chance to
	 *     load and do initial modeset)
	 */

	node = of_parse_phandle(dev->dev->of_node, "memory-region", 0);
	if (node) {
		struct resource r;
		ret = of_address_to_resource(node, 0, &r);
		of_node_put(node);
		if (ret)
			return ret;
		size = r.end - r.start;
		DRM_INFO("using VRAM carveout: %lx@%pa\n", size, &r.start);

		/* if we have no IOMMU, then we need to use carveout allocator.
		 * Grab the entire CMA chunk carved out in early startup in
		 * mach-msm:
		 */
	} else if (!iommu_present(&platform_bus_type)) {
		DRM_INFO("using %s VRAM carveout\n", vram);
		size = memparse(vram, NULL);
	}

	if (size) {
		DEFINE_DMA_ATTRS(attrs);
		void *p;

		priv->vram.size = size;

		drm_mm_init(&priv->vram.mm, 0, (size >> PAGE_SHIFT) - 1);

		dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &attrs);
		dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);

		/* note that for no-kernel-mapping, the vaddr returned
		 * is bogus, but non-null if allocation succeeded:
		 */
		p = dma_alloc_attrs(dev->dev, size,
				&priv->vram.paddr, GFP_KERNEL, &attrs);
		if (!p) {
			dev_err(dev->dev, "failed to allocate VRAM\n");
			priv->vram.paddr = 0;
			return -ENOMEM;
		}

		dev_info(dev->dev, "VRAM: %08x->%08x\n",
				(uint32_t)priv->vram.paddr,
				(uint32_t)(priv->vram.paddr + size));
	}

	return ret;
}

static int msm_drm_init(struct device *dev, struct drm_driver *drv)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *ddev;
	struct msm_drm_private *priv;
	struct msm_kms *kms;
	int ret;

	ddev = drm_dev_alloc(drv, dev);
	if (!ddev) {
		dev_err(dev, "failed to allocate drm_device\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ddev);
	ddev->platformdev = pdev;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		drm_dev_unref(ddev);
		return -ENOMEM;
	}

	ddev->dev_private = priv;
	priv->dev = ddev;

	ret = msm_mdss_init(ddev);
	if (ret) {
		kfree(priv);
		drm_dev_unref(ddev);
		return ret;
	}

	priv->wq = alloc_ordered_workqueue("msm", 0);
	priv->atomic_wq = alloc_ordered_workqueue("msm:atomic", 0);
	init_waitqueue_head(&priv->pending_crtcs_event);

	INIT_LIST_HEAD(&priv->inactive_list);
	INIT_LIST_HEAD(&priv->vblank_ctrl.event_list);
	INIT_WORK(&priv->vblank_ctrl.work, vblank_ctrl_worker);
	spin_lock_init(&priv->vblank_ctrl.lock);

	drm_mode_config_init(ddev);

	/* Bind all our sub-components: */
	ret = component_bind_all(dev, ddev);
	if (ret) {
		msm_mdss_destroy(ddev);
		kfree(priv);
		drm_dev_unref(ddev);
		return ret;
	}

	ret = msm_init_vram(ddev);
	if (ret)
		goto fail;

	msm_gem_shrinker_init(ddev);

	switch (get_mdp_ver(pdev)) {
	case 4:
		kms = mdp4_kms_init(ddev);
		priv->kms = kms;
		break;
	case 5:
		kms = mdp5_kms_init(ddev);
		break;
	default:
		kms = ERR_PTR(-ENODEV);
		break;
	}

	if (IS_ERR(kms)) {
		/*
		 * NOTE: once we have GPU support, having no kms should not
		 * be considered fatal.. ideally we would still support gpu
		 * and (for example) use dmabuf/prime to share buffers with
		 * imx drm driver on iMX5
		 */
		dev_err(dev, "failed to load kms\n");
		ret = PTR_ERR(kms);
		goto fail;
	}

	if (kms) {
		ret = kms->funcs->hw_init(kms);
		if (ret) {
			dev_err(dev, "kms hw init failed: %d\n", ret);
			goto fail;
		}
	}

	ddev->mode_config.funcs = &mode_config_funcs;

	ret = drm_vblank_init(ddev, priv->num_crtcs);
	if (ret < 0) {
		dev_err(dev, "failed to initialize vblank\n");
		goto fail;
	}

	if (kms) {
		pm_runtime_get_sync(dev);
		ret = drm_irq_install(ddev, kms->irq);
		pm_runtime_put_sync(dev);
		if (ret < 0) {
			dev_err(dev, "failed to install IRQ handler\n");
			goto fail;
		}
	}

	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto fail;

	drm_mode_config_reset(ddev);

#ifdef CONFIG_DRM_FBDEV_EMULATION
	if (fbdev)
		priv->fbdev = msm_fbdev_init(ddev);
#endif

	ret = msm_debugfs_late_init(ddev);
	if (ret)
		goto fail;

	drm_kms_helper_poll_init(ddev);

	return 0;

fail:
	msm_drm_uninit(dev);
	return ret;
}

/*
 * DRM operations:
 */

static void load_gpu(struct drm_device *dev)
{
	static DEFINE_MUTEX(init_lock);
	struct msm_drm_private *priv = dev->dev_private;

	mutex_lock(&init_lock);

	if (!priv->gpu)
		priv->gpu = adreno_load_gpu(dev);

	mutex_unlock(&init_lock);
}

static int msm_open(struct drm_device *dev, struct drm_file *file)
{
	struct msm_file_private *ctx;

	/* For now, load gpu on open.. to avoid the requirement of having
	 * firmware in the initrd.
	 */
	load_gpu(dev);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file->driver_priv = ctx;

	return 0;
}

static void msm_preclose(struct drm_device *dev, struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_file_private *ctx = file->driver_priv;

	mutex_lock(&dev->struct_mutex);
	if (ctx == priv->lastctx)
		priv->lastctx = NULL;
	mutex_unlock(&dev->struct_mutex);

	kfree(ctx);
}

static void msm_lastclose(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	if (priv->fbdev)
		drm_fb_helper_restore_fbdev_mode_unlocked(priv->fbdev);
}

static irqreturn_t msm_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	BUG_ON(!kms);
	return kms->funcs->irq(kms);
}

static void msm_irq_preinstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	BUG_ON(!kms);
	kms->funcs->irq_preinstall(kms);
}

static int msm_irq_postinstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	BUG_ON(!kms);
	return kms->funcs->irq_postinstall(kms);
}

static void msm_irq_uninstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	BUG_ON(!kms);
	kms->funcs->irq_uninstall(kms);
}

static int msm_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	if (!kms)
		return -ENXIO;
	DBG("dev=%p, crtc=%u", dev, pipe);
	return vblank_ctrl_queue_work(priv, pipe, true);
}

static void msm_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	if (!kms)
		return;
	DBG("dev=%p, crtc=%u", dev, pipe);
	vblank_ctrl_queue_work(priv, pipe, false);
}

/*
 * DRM ioctls:
 */

static int msm_ioctl_get_param(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_msm_param *args = data;
	struct msm_gpu *gpu;

	/* for now, we just have 3d pipe.. eventually this would need to
	 * be more clever to dispatch to appropriate gpu module:
	 */
	if (args->pipe != MSM_PIPE_3D0)
		return -EINVAL;

	gpu = priv->gpu;

	if (!gpu)
		return -ENXIO;

	return gpu->funcs->get_param(gpu, args->param, &args->value);
}

static int msm_ioctl_gem_new(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_new *args = data;

	if (args->flags & ~MSM_BO_FLAGS) {
		DRM_ERROR("invalid flags: %08x\n", args->flags);
		return -EINVAL;
	}

	return msm_gem_new_handle(dev, file, args->size,
			args->flags, &args->handle);
}

static inline ktime_t to_ktime(struct drm_msm_timespec timeout)
{
	return ktime_set(timeout.tv_sec, timeout.tv_nsec);
}

static int msm_ioctl_gem_cpu_prep(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_cpu_prep *args = data;
	struct drm_gem_object *obj;
	ktime_t timeout = to_ktime(args->timeout);
	int ret;

	if (args->op & ~MSM_PREP_FLAGS) {
		DRM_ERROR("invalid op: %08x\n", args->op);
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	ret = msm_gem_cpu_prep(obj, args->op, &timeout);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int msm_ioctl_gem_cpu_fini(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_cpu_fini *args = data;
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	ret = msm_gem_cpu_fini(obj);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int msm_ioctl_gem_info(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_info *args = data;
	struct drm_gem_object *obj;
	int ret = 0;

	if (args->pad)
		return -EINVAL;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	args->offset = msm_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int msm_ioctl_wait_fence(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_msm_wait_fence *args = data;
	ktime_t timeout = to_ktime(args->timeout);

	if (args->pad) {
		DRM_ERROR("invalid pad: %08x\n", args->pad);
		return -EINVAL;
	}

	if (!priv->gpu)
		return 0;

	return msm_wait_fence(priv->gpu->fctx, args->fence, &timeout, true);
}

static int msm_ioctl_gem_madvise(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_madvise *args = data;
	struct drm_gem_object *obj;
	int ret;

	switch (args->madv) {
	case MSM_MADV_DONTNEED:
	case MSM_MADV_WILLNEED:
		break;
	default:
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj) {
		ret = -ENOENT;
		goto unlock;
	}

	ret = msm_gem_madvise(obj, args->madv);
	if (ret >= 0) {
		args->retained = ret;
		ret = 0;
	}

	drm_gem_object_unreference(obj);

unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static const struct drm_ioctl_desc msm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(MSM_GET_PARAM,    msm_ioctl_get_param,    DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_NEW,      msm_ioctl_gem_new,      DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_INFO,     msm_ioctl_gem_info,     DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_PREP, msm_ioctl_gem_cpu_prep, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_FINI, msm_ioctl_gem_cpu_fini, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_SUBMIT,   msm_ioctl_gem_submit,   DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_WAIT_FENCE,   msm_ioctl_wait_fence,   DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_MADVISE,  msm_ioctl_gem_madvise,  DRM_AUTH|DRM_RENDER_ALLOW),
};

static const struct vm_operations_struct vm_ops = {
	.fault = msm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations fops = {
	.owner              = THIS_MODULE,
	.open               = drm_open,
	.release            = drm_release,
	.unlocked_ioctl     = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl       = drm_compat_ioctl,
#endif
	.poll               = drm_poll,
	.read               = drm_read,
	.llseek             = no_llseek,
	.mmap               = msm_gem_mmap,
};

static struct drm_driver msm_driver = {
	.driver_features    = DRIVER_HAVE_IRQ |
				DRIVER_GEM |
				DRIVER_PRIME |
				DRIVER_RENDER |
				DRIVER_ATOMIC |
				DRIVER_MODESET,
	.open               = msm_open,
	.preclose           = msm_preclose,
	.lastclose          = msm_lastclose,
	.irq_handler        = msm_irq,
	.irq_preinstall     = msm_irq_preinstall,
	.irq_postinstall    = msm_irq_postinstall,
	.irq_uninstall      = msm_irq_uninstall,
	.get_vblank_counter = drm_vblank_no_hw_counter,
	.enable_vblank      = msm_enable_vblank,
	.disable_vblank     = msm_disable_vblank,
	.gem_free_object    = msm_gem_free_object,
	.gem_vm_ops         = &vm_ops,
	.dumb_create        = msm_gem_dumb_create,
	.dumb_map_offset    = msm_gem_dumb_map_offset,
	.dumb_destroy       = drm_gem_dumb_destroy,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export   = drm_gem_prime_export,
	.gem_prime_import   = drm_gem_prime_import,
	.gem_prime_pin      = msm_gem_prime_pin,
	.gem_prime_unpin    = msm_gem_prime_unpin,
	.gem_prime_get_sg_table = msm_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = msm_gem_prime_import_sg_table,
	.gem_prime_vmap     = msm_gem_prime_vmap,
	.gem_prime_vunmap   = msm_gem_prime_vunmap,
	.gem_prime_mmap     = msm_gem_prime_mmap,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = msm_debugfs_init,
	.debugfs_cleanup    = msm_debugfs_cleanup,
#endif
	.ioctls             = msm_ioctls,
	.num_ioctls         = DRM_MSM_NUM_IOCTLS,
	.fops               = &fops,
	.name               = "msm",
	.desc               = "MSM Snapdragon DRM",
	.date               = "20130625",
	.major              = MSM_VERSION_MAJOR,
	.minor              = MSM_VERSION_MINOR,
	.patchlevel         = MSM_VERSION_PATCHLEVEL,
};

#ifdef CONFIG_PM_SLEEP
static int msm_pm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	drm_kms_helper_poll_disable(ddev);

	return 0;
}

static int msm_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	drm_kms_helper_poll_enable(ddev);

	return 0;
}
#endif

static const struct dev_pm_ops msm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_pm_suspend, msm_pm_resume)
};

/*
 * Componentized driver support:
 */

/*
 * NOTE: duplication of the same code as exynos or imx (or probably any other).
 * so probably some room for some helpers
 */
static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

/*
 * Identify what components need to be added by parsing what remote-endpoints
 * our MDP output ports are connected to. In the case of LVDS on MDP4, there
 * is no external component that we need to add since LVDS is within MDP4
 * itself.
 */
static int add_components_mdp(struct device *mdp_dev,
			      struct component_match **matchptr)
{
	struct device_node *np = mdp_dev->of_node;
	struct device_node *ep_node;
	struct device *master_dev;

	/*
	 * on MDP4 based platforms, the MDP platform device is the component
	 * master that adds other display interface components to itself.
	 *
	 * on MDP5 based platforms, the MDSS platform device is the component
	 * master that adds MDP5 and other display interface components to
	 * itself.
	 */
	if (of_device_is_compatible(np, "qcom,mdp4"))
		master_dev = mdp_dev;
	else
		master_dev = mdp_dev->parent;

	for_each_endpoint_of_node(np, ep_node) {
		struct device_node *intf;
		struct of_endpoint ep;
		int ret;

		ret = of_graph_parse_endpoint(ep_node, &ep);
		if (ret) {
			dev_err(mdp_dev, "unable to parse port endpoint\n");
			of_node_put(ep_node);
			return ret;
		}

		/*
		 * The LCDC/LVDS port on MDP4 is a speacial case where the
		 * remote-endpoint isn't a component that we need to add
		 */
		if (of_device_is_compatible(np, "qcom,mdp4") &&
		    ep.port == 0) {
			of_node_put(ep_node);
			continue;
		}

		/*
		 * It's okay if some of the ports don't have a remote endpoint
		 * specified. It just means that the port isn't connected to
		 * any external interface.
		 */
		intf = of_graph_get_remote_port_parent(ep_node);
		if (!intf) {
			of_node_put(ep_node);
			continue;
		}

		component_match_add(master_dev, matchptr, compare_of, intf);

		of_node_put(intf);
		of_node_put(ep_node);
	}

	return 0;
}

static int compare_name_mdp(struct device *dev, void *data)
{
	return (strstr(dev_name(dev), "mdp") != NULL);
}

static int add_display_components(struct device *dev,
				  struct component_match **matchptr)
{
	struct device *mdp_dev;
	int ret;

	/*
	 * MDP5 based devices don't have a flat hierarchy. There is a top level
	 * parent: MDSS, and children: MDP5, DSI, HDMI, eDP etc. Populate the
	 * children devices, find the MDP5 node, and then add the interfaces
	 * to our components list.
	 */
	if (of_device_is_compatible(dev->of_node, "qcom,mdss")) {
		ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to populate children devices\n");
			return ret;
		}

		mdp_dev = device_find_child(dev, NULL, compare_name_mdp);
		if (!mdp_dev) {
			dev_err(dev, "failed to find MDSS MDP node\n");
			of_platform_depopulate(dev);
			return -ENODEV;
		}

		put_device(mdp_dev);

		/* add the MDP component itself */
		component_match_add(dev, matchptr, compare_of,
				    mdp_dev->of_node);
	} else {
		/* MDP4 */
		mdp_dev = dev;
	}

	ret = add_components_mdp(mdp_dev, matchptr);
	if (ret)
		of_platform_depopulate(dev);

	return ret;
}

/*
 * We don't know what's the best binding to link the gpu with the drm device.
 * Fow now, we just hunt for all the possible gpus that we support, and add them
 * as components.
 */
static const struct of_device_id msm_gpu_match[] = {
	{ .compatible = "qcom,adreno-3xx" },
	{ .compatible = "qcom,kgsl-3d0" },
	{ },
};

static int add_gpu_components(struct device *dev,
			      struct component_match **matchptr)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, msm_gpu_match);
	if (!np)
		return 0;

	component_match_add(dev, matchptr, compare_of, np);

	of_node_put(np);

	return 0;
}

static int msm_drm_bind(struct device *dev)
{
	return msm_drm_init(dev, &msm_driver);
}

static void msm_drm_unbind(struct device *dev)
{
	msm_drm_uninit(dev);
}

static const struct component_master_ops msm_drm_ops = {
	.bind = msm_drm_bind,
	.unbind = msm_drm_unbind,
};

/*
 * Platform driver:
 */

static int msm_pdev_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	int ret;

	ret = add_display_components(&pdev->dev, &match);
	if (ret)
		return ret;

	ret = add_gpu_components(&pdev->dev, &match);
	if (ret)
		return ret;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	return component_master_add_with_match(&pdev->dev, &msm_drm_ops, match);
}

static int msm_pdev_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &msm_drm_ops);
	of_platform_depopulate(&pdev->dev);

	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,mdp4", .data = (void *)4 },	/* MDP4 */
	{ .compatible = "qcom,mdss", .data = (void *)5 },	/* MDP5 MDSS */
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver msm_platform_driver = {
	.probe      = msm_pdev_probe,
	.remove     = msm_pdev_remove,
	.driver     = {
		.name   = "msm",
		.of_match_table = dt_match,
		.pm     = &msm_pm_ops,
	},
};

static int __init msm_drm_register(void)
{
	DBG("init");
	msm_mdp_register();
	msm_dsi_register();
	msm_edp_register();
	msm_hdmi_register();
	adreno_register();
	return platform_driver_register(&msm_platform_driver);
}

static void __exit msm_drm_unregister(void)
{
	DBG("fini");
	platform_driver_unregister(&msm_platform_driver);
	msm_hdmi_unregister();
	adreno_unregister();
	msm_edp_unregister();
	msm_dsi_unregister();
	msm_mdp_unregister();
}

module_init(msm_drm_register);
module_exit(msm_drm_unregister);

MODULE_AUTHOR("Rob Clark <robdclark@gmail.com");
MODULE_DESCRIPTION("MSM DRM Driver");
MODULE_LICENSE("GPL");
