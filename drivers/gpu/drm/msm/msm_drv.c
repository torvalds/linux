// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/dma-mapping.h>
#include <linux/fault-inject.h>
#include <linux/debugfs.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_of.h>

#include "msm_drv.h"
#include "msm_debugfs.h"
#include "msm_gem.h"
#include "msm_gpu.h"
#include "msm_kms.h"

/*
 * MSM driver version:
 * - 1.0.0 - initial interface
 * - 1.1.0 - adds madvise, and support for submits with > 4 cmd buffers
 * - 1.2.0 - adds explicit fence support for submit ioctl
 * - 1.3.0 - adds GMEM_BASE + NR_RINGS params, SUBMITQUEUE_NEW +
 *           SUBMITQUEUE_CLOSE ioctls, and MSM_INFO_IOVA flag for
 *           MSM_GEM_INFO ioctl.
 * - 1.4.0 - softpin, MSM_RELOC_BO_DUMP, and GEM_INFO support to set/get
 *           GEM object's debug name
 * - 1.5.0 - Add SUBMITQUERY_QUERY ioctl
 * - 1.6.0 - Syncobj support
 * - 1.7.0 - Add MSM_PARAM_SUSPENDS to access suspend count
 * - 1.8.0 - Add MSM_BO_CACHED_COHERENT for supported GPUs (a6xx)
 * - 1.9.0 - Add MSM_SUBMIT_FENCE_SN_IN
 * - 1.10.0 - Add MSM_SUBMIT_BO_NO_IMPLICIT
 * - 1.11.0 - Add wait boost (MSM_WAIT_FENCE_BOOST, MSM_PREP_BOOST)
 * - 1.12.0 - Add MSM_INFO_SET_METADATA and MSM_INFO_GET_METADATA
 */
#define MSM_VERSION_MAJOR	1
#define MSM_VERSION_MINOR	12
#define MSM_VERSION_PATCHLEVEL	0

static void msm_deinit_vram(struct drm_device *ddev);

static char *vram = "16m";
MODULE_PARM_DESC(vram, "Configure VRAM size (for devices without IOMMU/GPUMMU)");
module_param(vram, charp, 0);

bool dumpstate;
MODULE_PARM_DESC(dumpstate, "Dump KMS state on errors");
module_param(dumpstate, bool, 0600);

static bool modeset = true;
MODULE_PARM_DESC(modeset, "Use kernel modesetting [KMS] (1=on (default), 0=disable)");
module_param(modeset, bool, 0600);

DECLARE_FAULT_ATTR(fail_gem_alloc);
DECLARE_FAULT_ATTR(fail_gem_iova);

static int msm_drm_uninit(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_drm_private *priv = platform_get_drvdata(pdev);
	struct drm_device *ddev = priv->dev;

	/*
	 * Shutdown the hw if we're far enough along where things might be on.
	 * If we run this too early, we'll end up panicking in any variety of
	 * places. Since we don't register the drm device until late in
	 * msm_drm_init, drm_dev->registered is used as an indicator that the
	 * shutdown will be successful.
	 */
	if (ddev->registered) {
		drm_dev_unregister(ddev);
		if (priv->kms)
			drm_atomic_helper_shutdown(ddev);
	}

	/* We must cancel and cleanup any pending vblank enable/disable
	 * work before msm_irq_uninstall() to avoid work re-enabling an
	 * irq after uninstall has disabled it.
	 */

	flush_workqueue(priv->wq);

	msm_gem_shrinker_cleanup(ddev);

	msm_perf_debugfs_cleanup(priv);
	msm_rd_debugfs_cleanup(priv);

	if (priv->kms)
		msm_drm_kms_uninit(dev);

	msm_deinit_vram(ddev);

	component_unbind_all(dev, ddev);

	ddev->dev_private = NULL;
	drm_dev_put(ddev);

	destroy_workqueue(priv->wq);

	return 0;
}

bool msm_use_mmu(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;

	/*
	 * a2xx comes with its own MMU
	 * On other platforms IOMMU can be declared specified either for the
	 * MDP/DPU device or for its parent, MDSS device.
	 */
	return priv->is_a2xx ||
		device_iommu_mapped(dev->dev) ||
		device_iommu_mapped(dev->dev->parent);
}

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
		size = r.end - r.start + 1;
		DRM_INFO("using VRAM carveout: %lx@%pa\n", size, &r.start);

		/* if we have no IOMMU, then we need to use carveout allocator.
		 * Grab the entire DMA chunk carved out in early startup in
		 * mach-msm:
		 */
	} else if (!msm_use_mmu(dev)) {
		DRM_INFO("using %s VRAM carveout\n", vram);
		size = memparse(vram, NULL);
	}

	if (size) {
		unsigned long attrs = 0;
		void *p;

		priv->vram.size = size;

		drm_mm_init(&priv->vram.mm, 0, (size >> PAGE_SHIFT) - 1);
		spin_lock_init(&priv->vram.lock);

		attrs |= DMA_ATTR_NO_KERNEL_MAPPING;
		attrs |= DMA_ATTR_WRITE_COMBINE;

		/* note that for no-kernel-mapping, the vaddr returned
		 * is bogus, but non-null if allocation succeeded:
		 */
		p = dma_alloc_attrs(dev->dev, size,
				&priv->vram.paddr, GFP_KERNEL, attrs);
		if (!p) {
			DRM_DEV_ERROR(dev->dev, "failed to allocate VRAM\n");
			priv->vram.paddr = 0;
			return -ENOMEM;
		}

		DRM_DEV_INFO(dev->dev, "VRAM: %08x->%08x\n",
				(uint32_t)priv->vram.paddr,
				(uint32_t)(priv->vram.paddr + size));
	}

	return ret;
}

static void msm_deinit_vram(struct drm_device *ddev)
{
	struct msm_drm_private *priv = ddev->dev_private;
	unsigned long attrs = DMA_ATTR_NO_KERNEL_MAPPING;

	if (!priv->vram.paddr)
		return;

	drm_mm_takedown(&priv->vram.mm);
	dma_free_attrs(ddev->dev, priv->vram.size, NULL, priv->vram.paddr,
			attrs);
}

static int msm_drm_init(struct device *dev, const struct drm_driver *drv)
{
	struct msm_drm_private *priv = dev_get_drvdata(dev);
	struct drm_device *ddev;
	int ret;

	if (drm_firmware_drivers_only())
		return -ENODEV;

	ddev = drm_dev_alloc(drv, dev);
	if (IS_ERR(ddev)) {
		DRM_DEV_ERROR(dev, "failed to allocate drm_device\n");
		return PTR_ERR(ddev);
	}
	ddev->dev_private = priv;
	priv->dev = ddev;

	priv->wq = alloc_ordered_workqueue("msm", 0);
	if (!priv->wq) {
		ret = -ENOMEM;
		goto err_put_dev;
	}

	INIT_LIST_HEAD(&priv->objects);
	mutex_init(&priv->obj_lock);

	/*
	 * Initialize the LRUs:
	 */
	mutex_init(&priv->lru.lock);
	drm_gem_lru_init(&priv->lru.unbacked, &priv->lru.lock);
	drm_gem_lru_init(&priv->lru.pinned,   &priv->lru.lock);
	drm_gem_lru_init(&priv->lru.willneed, &priv->lru.lock);
	drm_gem_lru_init(&priv->lru.dontneed, &priv->lru.lock);

	/* Teach lockdep about lock ordering wrt. shrinker: */
	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&priv->lru.lock);
	fs_reclaim_release(GFP_KERNEL);

	if (priv->kms_init) {
		ret = drmm_mode_config_init(ddev);
		if (ret)
			goto err_destroy_wq;
	}

	ret = msm_init_vram(ddev);
	if (ret)
		goto err_destroy_wq;

	dma_set_max_seg_size(dev, UINT_MAX);

	/* Bind all our sub-components: */
	ret = component_bind_all(dev, ddev);
	if (ret)
		goto err_deinit_vram;

	ret = msm_gem_shrinker_init(ddev);
	if (ret)
		goto err_msm_uninit;

	if (priv->kms_init) {
		ret = msm_drm_kms_init(dev, drv);
		if (ret)
			goto err_msm_uninit;
	} else {
		/* valid only for the dummy headless case, where of_node=NULL */
		WARN_ON(dev->of_node);
		ddev->driver_features &= ~DRIVER_MODESET;
		ddev->driver_features &= ~DRIVER_ATOMIC;
	}

	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto err_msm_uninit;

	ret = msm_debugfs_late_init(ddev);
	if (ret)
		goto err_msm_uninit;

	if (priv->kms_init) {
		drm_kms_helper_poll_init(ddev);
		drm_client_setup(ddev, NULL);
	}

	return 0;

err_msm_uninit:
	msm_drm_uninit(dev);

	return ret;

err_deinit_vram:
	msm_deinit_vram(ddev);
err_destroy_wq:
	destroy_workqueue(priv->wq);
err_put_dev:
	drm_dev_put(ddev);

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

static int context_init(struct drm_device *dev, struct drm_file *file)
{
	static atomic_t ident = ATOMIC_INIT(0);
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_file_private *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->submitqueues);
	rwlock_init(&ctx->queuelock);

	kref_init(&ctx->ref);
	msm_submitqueue_init(dev, ctx);

	ctx->aspace = msm_gpu_create_private_address_space(priv->gpu, current);
	file->driver_priv = ctx;

	ctx->seqno = atomic_inc_return(&ident);

	return 0;
}

static int msm_open(struct drm_device *dev, struct drm_file *file)
{
	/* For now, load gpu on open.. to avoid the requirement of having
	 * firmware in the initrd.
	 */
	load_gpu(dev);

	return context_init(dev, file);
}

static void context_close(struct msm_file_private *ctx)
{
	msm_submitqueue_close(ctx);
	msm_file_private_put(ctx);
}

static void msm_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_file_private *ctx = file->driver_priv;

	/*
	 * It is not possible to set sysprof param to non-zero if gpu
	 * is not initialized:
	 */
	if (priv->gpu)
		msm_file_private_set_sysprof(ctx, priv->gpu, 0);

	context_close(ctx);
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
	if ((args->pipe != MSM_PIPE_3D0) || (args->pad != 0))
		return -EINVAL;

	gpu = priv->gpu;

	if (!gpu)
		return -ENXIO;

	return gpu->funcs->get_param(gpu, file->driver_priv,
				     args->param, &args->value, &args->len);
}

static int msm_ioctl_set_param(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_msm_param *args = data;
	struct msm_gpu *gpu;

	if ((args->pipe != MSM_PIPE_3D0) || (args->pad != 0))
		return -EINVAL;

	gpu = priv->gpu;

	if (!gpu)
		return -ENXIO;

	return gpu->funcs->set_param(gpu, file->driver_priv,
				     args->param, args->value, args->len);
}

static int msm_ioctl_gem_new(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_new *args = data;
	uint32_t flags = args->flags;

	if (args->flags & ~MSM_BO_FLAGS) {
		DRM_ERROR("invalid flags: %08x\n", args->flags);
		return -EINVAL;
	}

	/*
	 * Uncached CPU mappings are deprecated, as of:
	 *
	 * 9ef364432db4 ("drm/msm: deprecate MSM_BO_UNCACHED (map as writecombine instead)")
	 *
	 * So promote them to WC.
	 */
	if (flags & MSM_BO_UNCACHED) {
		flags &= ~MSM_BO_CACHED;
		flags |= MSM_BO_WC;
	}

	if (should_fail(&fail_gem_alloc, args->size))
		return -ENOMEM;

	return msm_gem_new_handle(dev, file, args->size,
			args->flags, &args->handle, NULL);
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

	drm_gem_object_put(obj);

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

	drm_gem_object_put(obj);

	return ret;
}

static int msm_ioctl_gem_info_iova(struct drm_device *dev,
		struct drm_file *file, struct drm_gem_object *obj,
		uint64_t *iova)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_file_private *ctx = file->driver_priv;

	if (!priv->gpu)
		return -EINVAL;

	if (should_fail(&fail_gem_iova, obj->size))
		return -ENOMEM;

	/*
	 * Don't pin the memory here - just get an address so that userspace can
	 * be productive
	 */
	return msm_gem_get_iova(obj, ctx->aspace, iova);
}

static int msm_ioctl_gem_info_set_iova(struct drm_device *dev,
		struct drm_file *file, struct drm_gem_object *obj,
		uint64_t iova)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_file_private *ctx = file->driver_priv;

	if (!priv->gpu)
		return -EINVAL;

	/* Only supported if per-process address space is supported: */
	if (priv->gpu->aspace == ctx->aspace)
		return -EOPNOTSUPP;

	if (should_fail(&fail_gem_iova, obj->size))
		return -ENOMEM;

	return msm_gem_set_iova(obj, ctx->aspace, iova);
}

static int msm_ioctl_gem_info_set_metadata(struct drm_gem_object *obj,
					   __user void *metadata,
					   u32 metadata_size)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	void *buf;
	int ret;

	/* Impose a moderate upper bound on metadata size: */
	if (metadata_size > 128) {
		return -EOVERFLOW;
	}

	/* Use a temporary buf to keep copy_from_user() outside of gem obj lock: */
	buf = memdup_user(metadata, metadata_size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = msm_gem_lock_interruptible(obj);
	if (ret)
		goto out;

	msm_obj->metadata =
		krealloc(msm_obj->metadata, metadata_size, GFP_KERNEL);
	msm_obj->metadata_size = metadata_size;
	memcpy(msm_obj->metadata, buf, metadata_size);

	msm_gem_unlock(obj);

out:
	kfree(buf);

	return ret;
}

static int msm_ioctl_gem_info_get_metadata(struct drm_gem_object *obj,
					   __user void *metadata,
					   u32 *metadata_size)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	void *buf;
	int ret, len;

	if (!metadata) {
		/*
		 * Querying the size is inherently racey, but
		 * EXT_external_objects expects the app to confirm
		 * via device and driver UUIDs that the exporter and
		 * importer versions match.  All we can do from the
		 * kernel side is check the length under obj lock
		 * when userspace tries to retrieve the metadata
		 */
		*metadata_size = msm_obj->metadata_size;
		return 0;
	}

	ret = msm_gem_lock_interruptible(obj);
	if (ret)
		return ret;

	/* Avoid copy_to_user() under gem obj lock: */
	len = msm_obj->metadata_size;
	buf = kmemdup(msm_obj->metadata, len, GFP_KERNEL);

	msm_gem_unlock(obj);

	if (*metadata_size < len) {
		ret = -ETOOSMALL;
	} else if (copy_to_user(metadata, buf, len)) {
		ret = -EFAULT;
	} else {
		*metadata_size = len;
	}

	kfree(buf);

	return 0;
}

static int msm_ioctl_gem_info(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_info *args = data;
	struct drm_gem_object *obj;
	struct msm_gem_object *msm_obj;
	int i, ret = 0;

	if (args->pad)
		return -EINVAL;

	switch (args->info) {
	case MSM_INFO_GET_OFFSET:
	case MSM_INFO_GET_IOVA:
	case MSM_INFO_SET_IOVA:
	case MSM_INFO_GET_FLAGS:
		/* value returned as immediate, not pointer, so len==0: */
		if (args->len)
			return -EINVAL;
		break;
	case MSM_INFO_SET_NAME:
	case MSM_INFO_GET_NAME:
	case MSM_INFO_SET_METADATA:
	case MSM_INFO_GET_METADATA:
		break;
	default:
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	msm_obj = to_msm_bo(obj);

	switch (args->info) {
	case MSM_INFO_GET_OFFSET:
		args->value = msm_gem_mmap_offset(obj);
		break;
	case MSM_INFO_GET_IOVA:
		ret = msm_ioctl_gem_info_iova(dev, file, obj, &args->value);
		break;
	case MSM_INFO_SET_IOVA:
		ret = msm_ioctl_gem_info_set_iova(dev, file, obj, args->value);
		break;
	case MSM_INFO_GET_FLAGS:
		if (obj->import_attach) {
			ret = -EINVAL;
			break;
		}
		/* Hide internal kernel-only flags: */
		args->value = to_msm_bo(obj)->flags & MSM_BO_FLAGS;
		ret = 0;
		break;
	case MSM_INFO_SET_NAME:
		/* length check should leave room for terminating null: */
		if (args->len >= sizeof(msm_obj->name)) {
			ret = -EINVAL;
			break;
		}
		if (copy_from_user(msm_obj->name, u64_to_user_ptr(args->value),
				   args->len)) {
			msm_obj->name[0] = '\0';
			ret = -EFAULT;
			break;
		}
		msm_obj->name[args->len] = '\0';
		for (i = 0; i < args->len; i++) {
			if (!isprint(msm_obj->name[i])) {
				msm_obj->name[i] = '\0';
				break;
			}
		}
		break;
	case MSM_INFO_GET_NAME:
		if (args->value && (args->len < strlen(msm_obj->name))) {
			ret = -ETOOSMALL;
			break;
		}
		args->len = strlen(msm_obj->name);
		if (args->value) {
			if (copy_to_user(u64_to_user_ptr(args->value),
					 msm_obj->name, args->len))
				ret = -EFAULT;
		}
		break;
	case MSM_INFO_SET_METADATA:
		ret = msm_ioctl_gem_info_set_metadata(
			obj, u64_to_user_ptr(args->value), args->len);
		break;
	case MSM_INFO_GET_METADATA:
		ret = msm_ioctl_gem_info_get_metadata(
			obj, u64_to_user_ptr(args->value), &args->len);
		break;
	}

	drm_gem_object_put(obj);

	return ret;
}

static int wait_fence(struct msm_gpu_submitqueue *queue, uint32_t fence_id,
		      ktime_t timeout, uint32_t flags)
{
	struct dma_fence *fence;
	int ret;

	if (fence_after(fence_id, queue->last_fence)) {
		DRM_ERROR_RATELIMITED("waiting on invalid fence: %u (of %u)\n",
				      fence_id, queue->last_fence);
		return -EINVAL;
	}

	/*
	 * Map submitqueue scoped "seqno" (which is actually an idr key)
	 * back to underlying dma-fence
	 *
	 * The fence is removed from the fence_idr when the submit is
	 * retired, so if the fence is not found it means there is nothing
	 * to wait for
	 */
	spin_lock(&queue->idr_lock);
	fence = idr_find(&queue->fence_idr, fence_id);
	if (fence)
		fence = dma_fence_get_rcu(fence);
	spin_unlock(&queue->idr_lock);

	if (!fence)
		return 0;

	if (flags & MSM_WAIT_FENCE_BOOST)
		dma_fence_set_deadline(fence, ktime_get());

	ret = dma_fence_wait_timeout(fence, true, timeout_to_jiffies(&timeout));
	if (ret == 0) {
		ret = -ETIMEDOUT;
	} else if (ret != -ERESTARTSYS) {
		ret = 0;
	}

	dma_fence_put(fence);

	return ret;
}

static int msm_ioctl_wait_fence(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_msm_wait_fence *args = data;
	struct msm_gpu_submitqueue *queue;
	int ret;

	if (args->flags & ~MSM_WAIT_FENCE_FLAGS) {
		DRM_ERROR("invalid flags: %08x\n", args->flags);
		return -EINVAL;
	}

	if (!priv->gpu)
		return 0;

	queue = msm_submitqueue_get(file->driver_priv, args->queueid);
	if (!queue)
		return -ENOENT;

	ret = wait_fence(queue, args->fence, to_ktime(args->timeout), args->flags);

	msm_submitqueue_put(queue);

	return ret;
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

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj) {
		return -ENOENT;
	}

	ret = msm_gem_madvise(obj, args->madv);
	if (ret >= 0) {
		args->retained = ret;
		ret = 0;
	}

	drm_gem_object_put(obj);

	return ret;
}


static int msm_ioctl_submitqueue_new(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_submitqueue *args = data;

	if (args->flags & ~MSM_SUBMITQUEUE_FLAGS)
		return -EINVAL;

	return msm_submitqueue_create(dev, file->driver_priv, args->prio,
		args->flags, &args->id);
}

static int msm_ioctl_submitqueue_query(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	return msm_submitqueue_query(dev, file->driver_priv, data);
}

static int msm_ioctl_submitqueue_close(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	u32 id = *(u32 *) data;

	return msm_submitqueue_remove(file->driver_priv, id);
}

static const struct drm_ioctl_desc msm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(MSM_GET_PARAM,    msm_ioctl_get_param,    DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_SET_PARAM,    msm_ioctl_set_param,    DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_NEW,      msm_ioctl_gem_new,      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_INFO,     msm_ioctl_gem_info,     DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_PREP, msm_ioctl_gem_cpu_prep, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_FINI, msm_ioctl_gem_cpu_fini, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_SUBMIT,   msm_ioctl_gem_submit,   DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_WAIT_FENCE,   msm_ioctl_wait_fence,   DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_MADVISE,  msm_ioctl_gem_madvise,  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_SUBMITQUEUE_NEW,   msm_ioctl_submitqueue_new,   DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_SUBMITQUEUE_CLOSE, msm_ioctl_submitqueue_close, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_SUBMITQUEUE_QUERY, msm_ioctl_submitqueue_query, DRM_RENDER_ALLOW),
};

static void msm_show_fdinfo(struct drm_printer *p, struct drm_file *file)
{
	struct drm_device *dev = file->minor->dev;
	struct msm_drm_private *priv = dev->dev_private;

	if (!priv->gpu)
		return;

	msm_gpu_show_fdinfo(priv->gpu, file->driver_priv, p);

	drm_show_memory_stats(p, file);
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	DRM_GEM_FOPS,
	.show_fdinfo = drm_show_fdinfo,
};

static const struct drm_driver msm_driver = {
	.driver_features    = DRIVER_GEM |
				DRIVER_RENDER |
				DRIVER_ATOMIC |
				DRIVER_MODESET |
				DRIVER_SYNCOBJ,
	.open               = msm_open,
	.postclose          = msm_postclose,
	.dumb_create        = msm_gem_dumb_create,
	.dumb_map_offset    = msm_gem_dumb_map_offset,
	.gem_prime_import_sg_table = msm_gem_prime_import_sg_table,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = msm_debugfs_init,
#endif
	MSM_FBDEV_DRIVER_OPS,
	.show_fdinfo        = msm_show_fdinfo,
	.ioctls             = msm_ioctls,
	.num_ioctls         = ARRAY_SIZE(msm_ioctls),
	.fops               = &fops,
	.name               = "msm",
	.desc               = "MSM Snapdragon DRM",
	.major              = MSM_VERSION_MAJOR,
	.minor              = MSM_VERSION_MINOR,
	.patchlevel         = MSM_VERSION_PATCHLEVEL,
};

/*
 * Componentized driver support:
 */

/*
 * Identify what components need to be added by parsing what remote-endpoints
 * our MDP output ports are connected to. In the case of LVDS on MDP4, there
 * is no external component that we need to add since LVDS is within MDP4
 * itself.
 */
static int add_components_mdp(struct device *master_dev,
			      struct component_match **matchptr)
{
	struct device_node *np = master_dev->of_node;
	struct device_node *ep_node;

	for_each_endpoint_of_node(np, ep_node) {
		struct device_node *intf;
		struct of_endpoint ep;
		int ret;

		ret = of_graph_parse_endpoint(ep_node, &ep);
		if (ret) {
			DRM_DEV_ERROR(master_dev, "unable to parse port endpoint\n");
			of_node_put(ep_node);
			return ret;
		}

		/*
		 * The LCDC/LVDS port on MDP4 is a speacial case where the
		 * remote-endpoint isn't a component that we need to add
		 */
		if (of_device_is_compatible(np, "qcom,mdp4") &&
		    ep.port == 0)
			continue;

		/*
		 * It's okay if some of the ports don't have a remote endpoint
		 * specified. It just means that the port isn't connected to
		 * any external interface.
		 */
		intf = of_graph_get_remote_port_parent(ep_node);
		if (!intf)
			continue;

		if (of_device_is_available(intf))
			drm_of_component_match_add(master_dev, matchptr,
						   component_compare_of, intf);

		of_node_put(intf);
	}

	return 0;
}

#if !IS_REACHABLE(CONFIG_DRM_MSM_MDP5) || !IS_REACHABLE(CONFIG_DRM_MSM_DPU)
bool msm_disp_drv_should_bind(struct device *dev, bool dpu_driver)
{
	/* If just a single driver is enabled, use it no matter what */
	return true;
}
#else

static bool prefer_mdp5 = true;
MODULE_PARM_DESC(prefer_mdp5, "Select whether MDP5 or DPU driver should be preferred");
module_param(prefer_mdp5, bool, 0444);

/* list all platforms supported by both mdp5 and dpu drivers */
static const char *const msm_mdp5_dpu_migration[] = {
	"qcom,msm8917-mdp5",
	"qcom,msm8937-mdp5",
	"qcom,msm8953-mdp5",
	"qcom,msm8996-mdp5",
	"qcom,sdm630-mdp5",
	"qcom,sdm660-mdp5",
	NULL,
};

bool msm_disp_drv_should_bind(struct device *dev, bool dpu_driver)
{
	/* If it is not an MDP5 device, do not try MDP5 driver */
	if (!of_device_is_compatible(dev->of_node, "qcom,mdp5"))
		return dpu_driver;

	/* If it is not in the migration list, use MDP5 */
	if (!of_device_compatible_match(dev->of_node, msm_mdp5_dpu_migration))
		return !dpu_driver;

	return prefer_mdp5 ? !dpu_driver : dpu_driver;
}
#endif

/*
 * We don't know what's the best binding to link the gpu with the drm device.
 * Fow now, we just hunt for all the possible gpus that we support, and add them
 * as components.
 */
static const struct of_device_id msm_gpu_match[] = {
	{ .compatible = "qcom,adreno" },
	{ .compatible = "qcom,adreno-3xx" },
	{ .compatible = "amd,imageon" },
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

	if (of_device_is_available(np))
		drm_of_component_match_add(dev, matchptr, component_compare_of, np);

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

const struct component_master_ops msm_drm_ops = {
	.bind = msm_drm_bind,
	.unbind = msm_drm_unbind,
};

int msm_drv_probe(struct device *master_dev,
	int (*kms_init)(struct drm_device *dev),
	struct msm_kms *kms)
{
	struct msm_drm_private *priv;
	struct component_match *match = NULL;
	int ret;

	priv = devm_kzalloc(master_dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->kms = kms;
	priv->kms_init = kms_init;
	dev_set_drvdata(master_dev, priv);

	/* Add mdp components if we have KMS. */
	if (kms_init) {
		ret = add_components_mdp(master_dev, &match);
		if (ret)
			return ret;
	}

	ret = add_gpu_components(master_dev, &match);
	if (ret)
		return ret;

	/* on all devices that I am aware of, iommu's which can map
	 * any address the cpu can see are used:
	 */
	ret = dma_set_mask_and_coherent(master_dev, ~0);
	if (ret)
		return ret;

	ret = component_master_add_with_match(master_dev, &msm_drm_ops, match);
	if (ret)
		return ret;

	return 0;
}

/*
 * Platform driver:
 * Used only for headlesss GPU instances
 */

static int msm_pdev_probe(struct platform_device *pdev)
{
	return msm_drv_probe(&pdev->dev, NULL, NULL);
}

static void msm_pdev_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &msm_drm_ops);
}

static struct platform_driver msm_platform_driver = {
	.probe      = msm_pdev_probe,
	.remove     = msm_pdev_remove,
	.driver     = {
		.name   = "msm",
	},
};

static int __init msm_drm_register(void)
{
	if (!modeset)
		return -EINVAL;

	DBG("init");
	msm_mdp_register();
	msm_dpu_register();
	msm_dsi_register();
	msm_hdmi_register();
	msm_dp_register();
	adreno_register();
	msm_mdp4_register();
	msm_mdss_register();
	return platform_driver_register(&msm_platform_driver);
}

static void __exit msm_drm_unregister(void)
{
	DBG("fini");
	platform_driver_unregister(&msm_platform_driver);
	msm_mdss_unregister();
	msm_mdp4_unregister();
	msm_dp_unregister();
	msm_hdmi_unregister();
	adreno_unregister();
	msm_dsi_unregister();
	msm_mdp_unregister();
	msm_dpu_unregister();
}

module_init(msm_drm_register);
module_exit(msm_drm_unregister);

MODULE_AUTHOR("Rob Clark <robdclark@gmail.com");
MODULE_DESCRIPTION("MSM DRM Driver");
MODULE_LICENSE("GPL");
