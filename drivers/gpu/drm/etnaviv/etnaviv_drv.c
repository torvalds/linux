// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2018 Etnaviv Project
 */

#include <linux/component.h>
#include <linux/of_platform.h>
#include <drm/drm_of.h>

#include "etnaviv_cmdbuf.h"
#include "etnaviv_drv.h"
#include "etnaviv_gpu.h"
#include "etnaviv_gem.h"
#include "etnaviv_mmu.h"
#include "etnaviv_perfmon.h"

/*
 * DRM operations:
 */


static void load_gpu(struct drm_device *dev)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	unsigned int i;

	for (i = 0; i < ETNA_MAX_PIPES; i++) {
		struct etnaviv_gpu *g = priv->gpu[i];

		if (g) {
			int ret;

			ret = etnaviv_gpu_init(g);
			if (ret)
				priv->gpu[i] = NULL;
		}
	}
}

static int etnaviv_open(struct drm_device *dev, struct drm_file *file)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct etnaviv_file_private *ctx;
	int i;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	for (i = 0; i < ETNA_MAX_PIPES; i++) {
		struct etnaviv_gpu *gpu = priv->gpu[i];
		struct drm_sched_rq *rq;

		if (gpu) {
			rq = &gpu->sched.sched_rq[DRM_SCHED_PRIORITY_NORMAL];
			drm_sched_entity_init(&ctx->sched_entity[i],
					      &rq, 1, NULL);
			}
	}

	file->driver_priv = ctx;

	return 0;
}

static void etnaviv_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct etnaviv_file_private *ctx = file->driver_priv;
	unsigned int i;

	for (i = 0; i < ETNA_MAX_PIPES; i++) {
		struct etnaviv_gpu *gpu = priv->gpu[i];

		if (gpu)
			drm_sched_entity_destroy(&ctx->sched_entity[i]);
	}

	kfree(ctx);
}

/*
 * DRM debugfs:
 */

#ifdef CONFIG_DEBUG_FS
static int etnaviv_gem_show(struct drm_device *dev, struct seq_file *m)
{
	struct etnaviv_drm_private *priv = dev->dev_private;

	etnaviv_gem_describe_objects(priv, m);

	return 0;
}

static int etnaviv_mm_show(struct drm_device *dev, struct seq_file *m)
{
	struct drm_printer p = drm_seq_file_printer(m);

	read_lock(&dev->vma_offset_manager->vm_lock);
	drm_mm_print(&dev->vma_offset_manager->vm_addr_space_mm, &p);
	read_unlock(&dev->vma_offset_manager->vm_lock);

	return 0;
}

static int etnaviv_mmu_show(struct etnaviv_gpu *gpu, struct seq_file *m)
{
	struct drm_printer p = drm_seq_file_printer(m);

	seq_printf(m, "Active Objects (%s):\n", dev_name(gpu->dev));

	mutex_lock(&gpu->mmu->lock);
	drm_mm_print(&gpu->mmu->mm, &p);
	mutex_unlock(&gpu->mmu->lock);

	return 0;
}

static void etnaviv_buffer_dump(struct etnaviv_gpu *gpu, struct seq_file *m)
{
	struct etnaviv_cmdbuf *buf = &gpu->buffer;
	u32 size = buf->size;
	u32 *ptr = buf->vaddr;
	u32 i;

	seq_printf(m, "virt %p - phys 0x%llx - free 0x%08x\n",
			buf->vaddr, (u64)etnaviv_cmdbuf_get_pa(buf),
			size - buf->user_size);

	for (i = 0; i < size / 4; i++) {
		if (i && !(i % 4))
			seq_puts(m, "\n");
		if (i % 4 == 0)
			seq_printf(m, "\t0x%p: ", ptr + i);
		seq_printf(m, "%08x ", *(ptr + i));
	}
	seq_puts(m, "\n");
}

static int etnaviv_ring_show(struct etnaviv_gpu *gpu, struct seq_file *m)
{
	seq_printf(m, "Ring Buffer (%s): ", dev_name(gpu->dev));

	mutex_lock(&gpu->lock);
	etnaviv_buffer_dump(gpu, m);
	mutex_unlock(&gpu->lock);

	return 0;
}

static int show_unlocked(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	int (*show)(struct drm_device *dev, struct seq_file *m) =
			node->info_ent->data;

	return show(dev, m);
}

static int show_each_gpu(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct etnaviv_gpu *gpu;
	int (*show)(struct etnaviv_gpu *gpu, struct seq_file *m) =
			node->info_ent->data;
	unsigned int i;
	int ret = 0;

	for (i = 0; i < ETNA_MAX_PIPES; i++) {
		gpu = priv->gpu[i];
		if (!gpu)
			continue;

		ret = show(gpu, m);
		if (ret < 0)
			break;
	}

	return ret;
}

static struct drm_info_list etnaviv_debugfs_list[] = {
		{"gpu", show_each_gpu, 0, etnaviv_gpu_debugfs},
		{"gem", show_unlocked, 0, etnaviv_gem_show},
		{ "mm", show_unlocked, 0, etnaviv_mm_show },
		{"mmu", show_each_gpu, 0, etnaviv_mmu_show},
		{"ring", show_each_gpu, 0, etnaviv_ring_show},
};

static int etnaviv_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	int ret;

	ret = drm_debugfs_create_files(etnaviv_debugfs_list,
			ARRAY_SIZE(etnaviv_debugfs_list),
			minor->debugfs_root, minor);

	if (ret) {
		dev_err(dev->dev, "could not install etnaviv_debugfs_list\n");
		return ret;
	}

	return ret;
}
#endif

/*
 * DRM ioctls:
 */

static int etnaviv_ioctl_get_param(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct drm_etnaviv_param *args = data;
	struct etnaviv_gpu *gpu;

	if (args->pipe >= ETNA_MAX_PIPES)
		return -EINVAL;

	gpu = priv->gpu[args->pipe];
	if (!gpu)
		return -ENXIO;

	return etnaviv_gpu_get_param(gpu, args->param, &args->value);
}

static int etnaviv_ioctl_gem_new(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_etnaviv_gem_new *args = data;

	if (args->flags & ~(ETNA_BO_CACHED | ETNA_BO_WC | ETNA_BO_UNCACHED |
			    ETNA_BO_FORCE_MMU))
		return -EINVAL;

	return etnaviv_gem_new_handle(dev, file, args->size,
			args->flags, &args->handle);
}

#define TS(t) ((struct timespec){ \
	.tv_sec = (t).tv_sec, \
	.tv_nsec = (t).tv_nsec \
})

static int etnaviv_ioctl_gem_cpu_prep(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_etnaviv_gem_cpu_prep *args = data;
	struct drm_gem_object *obj;
	int ret;

	if (args->op & ~(ETNA_PREP_READ | ETNA_PREP_WRITE | ETNA_PREP_NOSYNC))
		return -EINVAL;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	ret = etnaviv_gem_cpu_prep(obj, args->op, &TS(args->timeout));

	drm_gem_object_put_unlocked(obj);

	return ret;
}

static int etnaviv_ioctl_gem_cpu_fini(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_etnaviv_gem_cpu_fini *args = data;
	struct drm_gem_object *obj;
	int ret;

	if (args->flags)
		return -EINVAL;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	ret = etnaviv_gem_cpu_fini(obj);

	drm_gem_object_put_unlocked(obj);

	return ret;
}

static int etnaviv_ioctl_gem_info(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_etnaviv_gem_info *args = data;
	struct drm_gem_object *obj;
	int ret;

	if (args->pad)
		return -EINVAL;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	ret = etnaviv_gem_mmap_offset(obj, &args->offset);
	drm_gem_object_put_unlocked(obj);

	return ret;
}

static int etnaviv_ioctl_wait_fence(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_etnaviv_wait_fence *args = data;
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct timespec *timeout = &TS(args->timeout);
	struct etnaviv_gpu *gpu;

	if (args->flags & ~(ETNA_WAIT_NONBLOCK))
		return -EINVAL;

	if (args->pipe >= ETNA_MAX_PIPES)
		return -EINVAL;

	gpu = priv->gpu[args->pipe];
	if (!gpu)
		return -ENXIO;

	if (args->flags & ETNA_WAIT_NONBLOCK)
		timeout = NULL;

	return etnaviv_gpu_wait_fence_interruptible(gpu, args->fence,
						    timeout);
}

static int etnaviv_ioctl_gem_userptr(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	struct drm_etnaviv_gem_userptr *args = data;

	if (args->flags & ~(ETNA_USERPTR_READ|ETNA_USERPTR_WRITE) ||
	    args->flags == 0)
		return -EINVAL;

	if (offset_in_page(args->user_ptr | args->user_size) ||
	    (uintptr_t)args->user_ptr != args->user_ptr ||
	    (u32)args->user_size != args->user_size ||
	    args->user_ptr & ~PAGE_MASK)
		return -EINVAL;

	if (!access_ok((void __user *)(unsigned long)args->user_ptr,
		       args->user_size))
		return -EFAULT;

	return etnaviv_gem_new_userptr(dev, file, args->user_ptr,
				       args->user_size, args->flags,
				       &args->handle);
}

static int etnaviv_ioctl_gem_wait(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct drm_etnaviv_gem_wait *args = data;
	struct timespec *timeout = &TS(args->timeout);
	struct drm_gem_object *obj;
	struct etnaviv_gpu *gpu;
	int ret;

	if (args->flags & ~(ETNA_WAIT_NONBLOCK))
		return -EINVAL;

	if (args->pipe >= ETNA_MAX_PIPES)
		return -EINVAL;

	gpu = priv->gpu[args->pipe];
	if (!gpu)
		return -ENXIO;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	if (args->flags & ETNA_WAIT_NONBLOCK)
		timeout = NULL;

	ret = etnaviv_gem_wait_bo(gpu, obj, timeout);

	drm_gem_object_put_unlocked(obj);

	return ret;
}

static int etnaviv_ioctl_pm_query_dom(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct drm_etnaviv_pm_domain *args = data;
	struct etnaviv_gpu *gpu;

	if (args->pipe >= ETNA_MAX_PIPES)
		return -EINVAL;

	gpu = priv->gpu[args->pipe];
	if (!gpu)
		return -ENXIO;

	return etnaviv_pm_query_dom(gpu, args);
}

static int etnaviv_ioctl_pm_query_sig(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct drm_etnaviv_pm_signal *args = data;
	struct etnaviv_gpu *gpu;

	if (args->pipe >= ETNA_MAX_PIPES)
		return -EINVAL;

	gpu = priv->gpu[args->pipe];
	if (!gpu)
		return -ENXIO;

	return etnaviv_pm_query_sig(gpu, args);
}

static const struct drm_ioctl_desc etnaviv_ioctls[] = {
#define ETNA_IOCTL(n, func, flags) \
	DRM_IOCTL_DEF_DRV(ETNAVIV_##n, etnaviv_ioctl_##func, flags)
	ETNA_IOCTL(GET_PARAM,    get_param,    DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(GEM_NEW,      gem_new,      DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(GEM_INFO,     gem_info,     DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(GEM_CPU_PREP, gem_cpu_prep, DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(GEM_CPU_FINI, gem_cpu_fini, DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(GEM_SUBMIT,   gem_submit,   DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(WAIT_FENCE,   wait_fence,   DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(GEM_USERPTR,  gem_userptr,  DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(GEM_WAIT,     gem_wait,     DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(PM_QUERY_DOM, pm_query_dom, DRM_AUTH|DRM_RENDER_ALLOW),
	ETNA_IOCTL(PM_QUERY_SIG, pm_query_sig, DRM_AUTH|DRM_RENDER_ALLOW),
};

static const struct vm_operations_struct vm_ops = {
	.fault = etnaviv_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations fops = {
	.owner              = THIS_MODULE,
	.open               = drm_open,
	.release            = drm_release,
	.unlocked_ioctl     = drm_ioctl,
	.compat_ioctl       = drm_compat_ioctl,
	.poll               = drm_poll,
	.read               = drm_read,
	.llseek             = no_llseek,
	.mmap               = etnaviv_gem_mmap,
};

static struct drm_driver etnaviv_drm_driver = {
	.driver_features    = DRIVER_GEM |
				DRIVER_PRIME |
				DRIVER_RENDER,
	.open               = etnaviv_open,
	.postclose           = etnaviv_postclose,
	.gem_free_object_unlocked = etnaviv_gem_free_object,
	.gem_vm_ops         = &vm_ops,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export   = drm_gem_prime_export,
	.gem_prime_import   = drm_gem_prime_import,
	.gem_prime_pin      = etnaviv_gem_prime_pin,
	.gem_prime_unpin    = etnaviv_gem_prime_unpin,
	.gem_prime_get_sg_table = etnaviv_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = etnaviv_gem_prime_import_sg_table,
	.gem_prime_vmap     = etnaviv_gem_prime_vmap,
	.gem_prime_vunmap   = etnaviv_gem_prime_vunmap,
	.gem_prime_mmap     = etnaviv_gem_prime_mmap,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = etnaviv_debugfs_init,
#endif
	.ioctls             = etnaviv_ioctls,
	.num_ioctls         = DRM_ETNAVIV_NUM_IOCTLS,
	.fops               = &fops,
	.name               = "etnaviv",
	.desc               = "etnaviv DRM",
	.date               = "20151214",
	.major              = 1,
	.minor              = 2,
};

/*
 * Platform driver:
 */
static int etnaviv_bind(struct device *dev)
{
	struct etnaviv_drm_private *priv;
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&etnaviv_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "failed to allocate private data\n");
		ret = -ENOMEM;
		goto out_put;
	}
	drm->dev_private = priv;

	dev->dma_parms = &priv->dma_parms;
	dma_set_max_seg_size(dev, SZ_2G);

	mutex_init(&priv->gem_lock);
	INIT_LIST_HEAD(&priv->gem_list);
	priv->num_gpus = 0;

	dev_set_drvdata(dev, drm);

	ret = component_bind_all(dev, drm);
	if (ret < 0)
		goto out_bind;

	load_gpu(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto out_register;

	return 0;

out_register:
	component_unbind_all(dev, drm);
out_bind:
	kfree(priv);
out_put:
	drm_dev_put(drm);

	return ret;
}

static void etnaviv_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct etnaviv_drm_private *priv = drm->dev_private;

	drm_dev_unregister(drm);

	component_unbind_all(dev, drm);

	dev->dma_parms = NULL;

	drm->dev_private = NULL;
	kfree(priv);

	drm_dev_put(drm);
}

static const struct component_master_ops etnaviv_master_ops = {
	.bind = etnaviv_bind,
	.unbind = etnaviv_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	struct device_node *np = data;

	return dev->of_node == np;
}

static int compare_str(struct device *dev, void *data)
{
	return !strcmp(dev_name(dev), data);
}

static int etnaviv_pdev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;

	if (!dev->platform_data) {
		struct device_node *core_node;

		for_each_compatible_node(core_node, NULL, "vivante,gc") {
			if (!of_device_is_available(core_node))
				continue;

			drm_of_component_match_add(&pdev->dev, &match,
						   compare_of, core_node);
		}
	} else {
		char **names = dev->platform_data;
		unsigned i;

		for (i = 0; names[i]; i++)
			component_match_add(dev, &match, compare_str, names[i]);
	}

	return component_master_add_with_match(dev, &etnaviv_master_ops, match);
}

static int etnaviv_pdev_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &etnaviv_master_ops);

	return 0;
}

static struct platform_driver etnaviv_platform_driver = {
	.probe      = etnaviv_pdev_probe,
	.remove     = etnaviv_pdev_remove,
	.driver     = {
		.name   = "etnaviv",
	},
};

static struct platform_device *etnaviv_drm;

static int __init etnaviv_init(void)
{
	struct platform_device *pdev;
	int ret;
	struct device_node *np;

	etnaviv_validate_init();

	ret = platform_driver_register(&etnaviv_gpu_driver);
	if (ret != 0)
		return ret;

	ret = platform_driver_register(&etnaviv_platform_driver);
	if (ret != 0)
		goto unregister_gpu_driver;

	/*
	 * If the DT contains at least one available GPU device, instantiate
	 * the DRM platform device.
	 */
	for_each_compatible_node(np, NULL, "vivante,gc") {
		if (!of_device_is_available(np))
			continue;

		pdev = platform_device_alloc("etnaviv", -1);
		if (!pdev) {
			ret = -ENOMEM;
			of_node_put(np);
			goto unregister_platform_driver;
		}
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(40);
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

		/*
		 * Apply the same DMA configuration to the virtual etnaviv
		 * device as the GPU we found. This assumes that all Vivante
		 * GPUs in the system share the same DMA constraints.
		 */
		of_dma_configure(&pdev->dev, np, true);

		ret = platform_device_add(pdev);
		if (ret) {
			platform_device_put(pdev);
			of_node_put(np);
			goto unregister_platform_driver;
		}

		etnaviv_drm = pdev;
		of_node_put(np);
		break;
	}

	return 0;

unregister_platform_driver:
	platform_driver_unregister(&etnaviv_platform_driver);
unregister_gpu_driver:
	platform_driver_unregister(&etnaviv_gpu_driver);
	return ret;
}
module_init(etnaviv_init);

static void __exit etnaviv_exit(void)
{
	platform_device_unregister(etnaviv_drm);
	platform_driver_unregister(&etnaviv_platform_driver);
	platform_driver_unregister(&etnaviv_gpu_driver);
}
module_exit(etnaviv_exit);

MODULE_AUTHOR("Christian Gmeiner <christian.gmeiner@gmail.com>");
MODULE_AUTHOR("Russell King <rmk+kernel@arm.linux.org.uk>");
MODULE_AUTHOR("Lucas Stach <l.stach@pengutronix.de>");
MODULE_DESCRIPTION("etnaviv DRM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:etnaviv");
