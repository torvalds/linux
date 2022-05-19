// SPDX-License-Identifier: GPL-2.0
/*
 * Framework for userspace DMA-BUF allocations
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#include <linux/cma.h>
#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/xarray.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <uapi/linux/rk-dma-heap.h>

#include "rk-dma-heap.h"

#define DEVNAME "rk_dma_heap"

#define NUM_HEAP_MINORS 128

static LIST_HEAD(rk_heap_list);
static DEFINE_MUTEX(rk_heap_list_lock);
static dev_t rk_dma_heap_devt;
static struct class *rk_dma_heap_class;
static DEFINE_XARRAY_ALLOC(rk_dma_heap_minors);
struct proc_dir_entry *proc_rk_dma_heap_dir;

#define K(size) ((unsigned long)((size) >> 10))

static int rk_vmap_pfn_apply(pte_t *pte, unsigned long addr, void *private)
{
	struct rk_vmap_pfn_data *data = private;

	*pte = pte_mkspecial(pfn_pte(data->pfn++, data->prot));
	return 0;
}

void *rk_vmap_contig_pfn(unsigned long pfn, unsigned int count, pgprot_t prot)
{
	struct rk_vmap_pfn_data data = { .pfn = pfn, .prot = pgprot_nx(prot) };
	struct vm_struct *area;

	area = get_vm_area_caller(count * PAGE_SIZE, VM_MAP,
			__builtin_return_address(0));
	if (!area)
		return NULL;
	if (apply_to_page_range(&init_mm, (unsigned long)area->addr,
			count * PAGE_SIZE, rk_vmap_pfn_apply, &data)) {
		free_vm_area(area);
		return NULL;
	}
	return area->addr;
}

int rk_dma_heap_set_dev(struct device *heap_dev)
{
	int err = 0;

	if (!heap_dev)
		return -EINVAL;

	dma_coerce_mask_and_coherent(heap_dev, DMA_BIT_MASK(64));

	if (!heap_dev->dma_parms) {
		heap_dev->dma_parms = devm_kzalloc(heap_dev,
						   sizeof(*heap_dev->dma_parms),
						   GFP_KERNEL);
		if (!heap_dev->dma_parms)
			return -ENOMEM;

		err = dma_set_max_seg_size(heap_dev, (unsigned int)DMA_BIT_MASK(64));
		if (err) {
			devm_kfree(heap_dev, heap_dev->dma_parms);
			dev_err(heap_dev, "Failed to set DMA segment size, err:%d\n", err);
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rk_dma_heap_set_dev);

struct rk_dma_heap *rk_dma_heap_find(const char *name)
{
	struct rk_dma_heap *h;

	mutex_lock(&rk_heap_list_lock);
	list_for_each_entry(h, &rk_heap_list, list) {
		if (!strcmp(h->name, name)) {
			kref_get(&h->refcount);
			mutex_unlock(&rk_heap_list_lock);
			return h;
		}
	}
	mutex_unlock(&rk_heap_list_lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(rk_dma_heap_find);

void rk_dma_heap_buffer_free(struct dma_buf *dmabuf)
{
	dma_buf_put(dmabuf);
}
EXPORT_SYMBOL_GPL(rk_dma_heap_buffer_free);

struct dma_buf *rk_dma_heap_buffer_alloc(struct rk_dma_heap *heap, size_t len,
					 unsigned int fd_flags,
					 unsigned int heap_flags,
					 const char *name)
{
	struct dma_buf *dmabuf;

	if (fd_flags & ~RK_DMA_HEAP_VALID_FD_FLAGS)
		return ERR_PTR(-EINVAL);

	if (heap_flags & ~RK_DMA_HEAP_VALID_HEAP_FLAGS)
		return ERR_PTR(-EINVAL);
	/*
	 * Allocations from all heaps have to begin
	 * and end on page boundaries.
	 */
	len = PAGE_ALIGN(len);
	if (!len)
		return ERR_PTR(-EINVAL);

	dmabuf = heap->ops->allocate(heap, len, fd_flags, heap_flags, name);

	if (IS_ENABLED(CONFIG_DMABUF_RK_HEAPS_DEBUG) && !IS_ERR(dmabuf))
		dma_buf_set_name(dmabuf, name);

	return dmabuf;
}
EXPORT_SYMBOL_GPL(rk_dma_heap_buffer_alloc);

int rk_dma_heap_bufferfd_alloc(struct rk_dma_heap *heap, size_t len,
			       unsigned int fd_flags,
			       unsigned int heap_flags,
			       const char *name)
{
	struct dma_buf *dmabuf;
	int fd;

	dmabuf = rk_dma_heap_buffer_alloc(heap, len, fd_flags, heap_flags,
					  name);

	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = dma_buf_fd(dmabuf, fd_flags);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
	}

	return fd;

}
EXPORT_SYMBOL_GPL(rk_dma_heap_bufferfd_alloc);

struct page *rk_dma_heap_alloc_contig_pages(struct rk_dma_heap *heap,
					    size_t len, const char *name)
{
	if (!heap->support_cma) {
		WARN_ON(!heap->support_cma);
		return ERR_PTR(-EINVAL);
	}

	len = PAGE_ALIGN(len);
	if (!len)
		return ERR_PTR(-EINVAL);

	return heap->ops->alloc_contig_pages(heap, len, name);
}
EXPORT_SYMBOL_GPL(rk_dma_heap_alloc_contig_pages);

void rk_dma_heap_free_contig_pages(struct rk_dma_heap *heap,
				   struct page *pages, size_t len,
				   const char *name)
{
	if (!heap->support_cma) {
		WARN_ON(!heap->support_cma);
		return;
	}

	return heap->ops->free_contig_pages(heap, pages, len, name);
}
EXPORT_SYMBOL_GPL(rk_dma_heap_free_contig_pages);

void rk_dma_heap_total_inc(struct rk_dma_heap *heap, size_t len)
{
	mutex_lock(&rk_heap_list_lock);
	heap->total_size += len;
	mutex_unlock(&rk_heap_list_lock);
}

void rk_dma_heap_total_dec(struct rk_dma_heap *heap, size_t len)
{
	mutex_lock(&rk_heap_list_lock);
	if (WARN_ON(heap->total_size < len))
		heap->total_size = 0;
	else
		heap->total_size -= len;
	mutex_unlock(&rk_heap_list_lock);
}

static int rk_dma_heap_open(struct inode *inode, struct file *file)
{
	struct rk_dma_heap *heap;

	heap = xa_load(&rk_dma_heap_minors, iminor(inode));
	if (!heap) {
		pr_err("dma_heap: minor %d unknown.\n", iminor(inode));
		return -ENODEV;
	}

	/* instance data as context */
	file->private_data = heap;
	nonseekable_open(inode, file);

	return 0;
}

static long rk_dma_heap_ioctl_allocate(struct file *file, void *data)
{
	struct rk_dma_heap_allocation_data *heap_allocation = data;
	struct rk_dma_heap *heap = file->private_data;
	int fd;

	if (heap_allocation->fd)
		return -EINVAL;

	fd = rk_dma_heap_bufferfd_alloc(heap, heap_allocation->len,
					heap_allocation->fd_flags,
					heap_allocation->heap_flags, NULL);
	if (fd < 0)
		return fd;

	heap_allocation->fd = fd;

	return 0;
}

static unsigned int rk_dma_heap_ioctl_cmds[] = {
	RK_DMA_HEAP_IOCTL_ALLOC,
};

static long rk_dma_heap_ioctl(struct file *file, unsigned int ucmd,
			      unsigned long arg)
{
	char stack_kdata[128];
	char *kdata = stack_kdata;
	unsigned int kcmd;
	unsigned int in_size, out_size, drv_size, ksize;
	int nr = _IOC_NR(ucmd);
	int ret = 0;

	if (nr >= ARRAY_SIZE(rk_dma_heap_ioctl_cmds))
		return -EINVAL;

	/* Get the kernel ioctl cmd that matches */
	kcmd = rk_dma_heap_ioctl_cmds[nr];

	/* Figure out the delta between user cmd size and kernel cmd size */
	drv_size = _IOC_SIZE(kcmd);
	out_size = _IOC_SIZE(ucmd);
	in_size = out_size;
	if ((ucmd & kcmd & IOC_IN) == 0)
		in_size = 0;
	if ((ucmd & kcmd & IOC_OUT) == 0)
		out_size = 0;
	ksize = max(max(in_size, out_size), drv_size);

	/* If necessary, allocate buffer for ioctl argument */
	if (ksize > sizeof(stack_kdata)) {
		kdata = kmalloc(ksize, GFP_KERNEL);
		if (!kdata)
			return -ENOMEM;
	}

	if (copy_from_user(kdata, (void __user *)arg, in_size) != 0) {
		ret = -EFAULT;
		goto err;
	}

	/* zero out any difference between the kernel/user structure size */
	if (ksize > in_size)
		memset(kdata + in_size, 0, ksize - in_size);

	switch (kcmd) {
	case RK_DMA_HEAP_IOCTL_ALLOC:
		ret = rk_dma_heap_ioctl_allocate(file, kdata);
		break;
	default:
		ret = -ENOTTY;
		goto err;
	}

	if (copy_to_user((void __user *)arg, kdata, out_size) != 0)
		ret = -EFAULT;
err:
	if (kdata != stack_kdata)
		kfree(kdata);
	return ret;
}

static const struct file_operations rk_dma_heap_fops = {
	.owner          = THIS_MODULE,
	.open		= rk_dma_heap_open,
	.unlocked_ioctl = rk_dma_heap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= rk_dma_heap_ioctl,
#endif
};

/**
 * rk_dma_heap_get_drvdata() - get per-subdriver data for the heap
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The per-subdriver data for the heap.
 */
void *rk_dma_heap_get_drvdata(struct rk_dma_heap *heap)
{
	return heap->priv;
}

static void rk_dma_heap_release(struct kref *ref)
{
	struct rk_dma_heap *heap = container_of(ref, struct rk_dma_heap, refcount);
	int minor = MINOR(heap->heap_devt);

	/* Note, we already holding the rk_heap_list_lock here */
	list_del(&heap->list);

	device_destroy(rk_dma_heap_class, heap->heap_devt);
	cdev_del(&heap->heap_cdev);
	xa_erase(&rk_dma_heap_minors, minor);

	kfree(heap);
}

void rk_dma_heap_put(struct rk_dma_heap *h)
{
	/*
	 * Take the rk_heap_list_lock now to avoid racing with code
	 * scanning the list and then taking a kref.
	 */
	mutex_lock(&rk_heap_list_lock);
	kref_put(&h->refcount, rk_dma_heap_release);
	mutex_unlock(&rk_heap_list_lock);
}

/**
 * rk_dma_heap_get_dev() - get device struct for the heap
 * @heap: DMA-Heap to retrieve device struct from
 *
 * Returns:
 * The device struct for the heap.
 */
struct device *rk_dma_heap_get_dev(struct rk_dma_heap *heap)
{
	return heap->heap_dev;
}

/**
 * rk_dma_heap_get_name() - get heap name
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The char* for the heap name.
 */
const char *rk_dma_heap_get_name(struct rk_dma_heap *heap)
{
	return heap->name;
}

struct rk_dma_heap *rk_dma_heap_add(const struct rk_dma_heap_export_info *exp_info)
{
	struct rk_dma_heap *heap, *err_ret;
	unsigned int minor;
	int ret;

	if (!exp_info->name || !strcmp(exp_info->name, "")) {
		pr_err("rk_dma_heap: Cannot add heap without a name\n");
		return ERR_PTR(-EINVAL);
	}

	if (!exp_info->ops || !exp_info->ops->allocate) {
		pr_err("rk_dma_heap: Cannot add heap with invalid ops struct\n");
		return ERR_PTR(-EINVAL);
	}

	/* check the name is unique */
	heap = rk_dma_heap_find(exp_info->name);
	if (heap) {
		pr_err("rk_dma_heap: Already registered heap named %s\n",
		       exp_info->name);
		rk_dma_heap_put(heap);
		return ERR_PTR(-EINVAL);
	}

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);

	kref_init(&heap->refcount);
	heap->name = exp_info->name;
	heap->ops = exp_info->ops;
	heap->priv = exp_info->priv;
	heap->support_cma = exp_info->support_cma;
	INIT_LIST_HEAD(&heap->dmabuf_list);
	INIT_LIST_HEAD(&heap->contig_list);
	mutex_init(&heap->dmabuf_lock);
	mutex_init(&heap->contig_lock);

	/* Find unused minor number */
	ret = xa_alloc(&rk_dma_heap_minors, &minor, heap,
		       XA_LIMIT(0, NUM_HEAP_MINORS - 1), GFP_KERNEL);
	if (ret < 0) {
		pr_err("rk_dma_heap: Unable to get minor number for heap\n");
		err_ret = ERR_PTR(ret);
		goto err0;
	}

	/* Create device */
	heap->heap_devt = MKDEV(MAJOR(rk_dma_heap_devt), minor);

	cdev_init(&heap->heap_cdev, &rk_dma_heap_fops);
	ret = cdev_add(&heap->heap_cdev, heap->heap_devt, 1);
	if (ret < 0) {
		pr_err("dma_heap: Unable to add char device\n");
		err_ret = ERR_PTR(ret);
		goto err1;
	}

	heap->heap_dev = device_create(rk_dma_heap_class,
				       NULL,
				       heap->heap_devt,
				       NULL,
				       heap->name);
	if (IS_ERR(heap->heap_dev)) {
		pr_err("rk_dma_heap: Unable to create device\n");
		err_ret = ERR_CAST(heap->heap_dev);
		goto err2;
	}

	heap->procfs = proc_rk_dma_heap_dir;

	/* Make sure it doesn't disappear on us */
	heap->heap_dev = get_device(heap->heap_dev);

	/* Add heap to the list */
	mutex_lock(&rk_heap_list_lock);
	list_add(&heap->list, &rk_heap_list);
	mutex_unlock(&rk_heap_list_lock);

	return heap;

err2:
	cdev_del(&heap->heap_cdev);
err1:
	xa_erase(&rk_dma_heap_minors, minor);
err0:
	kfree(heap);
	return err_ret;
}

static char *rk_dma_heap_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "rk_dma_heap/%s", dev_name(dev));
}

static int rk_dma_heap_dump_dmabuf(const struct dma_buf *dmabuf, void *data)
{
	struct rk_dma_heap *heap = (struct rk_dma_heap *)data;
	struct rk_dma_heap_dmabuf *buf;
	struct dma_buf_attachment *a;
	phys_addr_t size;
	int attach_count;
	int ret;

	if (!strcmp(dmabuf->exp_name, heap->name)) {
		seq_printf(heap->s, "dma-heap:<%s> -dmabuf", heap->name);
		mutex_lock(&heap->dmabuf_lock);
		list_for_each_entry(buf, &heap->dmabuf_list, node) {
			if (buf->dmabuf->file->f_inode->i_ino ==
				dmabuf->file->f_inode->i_ino) {
				seq_printf(heap->s,
					   "\ti_ino = %ld\n",
					   dmabuf->file->f_inode->i_ino);
				size = buf->end - buf->start + 1;
				seq_printf(heap->s,
					   "\tAlloc by (%-20s)\t[%pa-%pa]\t%pa (%lu KiB)\n",
					   dmabuf->name, &buf->start,
					   &buf->end, &size, K(size));
				seq_puts(heap->s, "\t\tAttached Devices:\n");
				attach_count = 0;
				ret = dma_resv_lock_interruptible(dmabuf->resv,
								  NULL);
				if (ret)
					goto error_unlock;
				list_for_each_entry(a, &dmabuf->attachments,
						    node) {
					seq_printf(heap->s, "\t\t%s\n",
						   dev_name(a->dev));
					attach_count++;
				}
				dma_resv_unlock(dmabuf->resv);
				seq_printf(heap->s,
					   "Total %d devices attached\n\n",
					   attach_count);
			}
		}
		mutex_unlock(&heap->dmabuf_lock);
	}

	return 0;
error_unlock:
	mutex_unlock(&heap->dmabuf_lock);
	return ret;
}

static int rk_dma_heap_dump_contig(void *data)
{
	struct rk_dma_heap *heap = (struct rk_dma_heap *)data;
	struct rk_dma_heap_contig_buf *buf;
	phys_addr_t size;

	mutex_lock(&heap->contig_lock);
	list_for_each_entry(buf, &heap->contig_list, node) {
		size = buf->end - buf->start + 1;
		seq_printf(heap->s, "dma-heap:<%s> -non dmabuf\n", heap->name);
		seq_printf(heap->s, "\tAlloc by (%-20s)\t[%pa-%pa]\t%pa (%lu KiB)\n",
			   buf->orig_alloc, &buf->start, &buf->end, &size, K(size));
	}
	mutex_unlock(&heap->contig_lock);

	return 0;
}

static ssize_t rk_total_pools_kb_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	struct rk_dma_heap *heap;
	u64 total_pool_size = 0;

	mutex_lock(&rk_heap_list_lock);
	list_for_each_entry(heap, &rk_heap_list, list)
		if (heap->ops->get_pool_size)
			total_pool_size += heap->ops->get_pool_size(heap);
	mutex_unlock(&rk_heap_list_lock);

	return sysfs_emit(buf, "%llu\n", total_pool_size / 1024);
}

static struct kobj_attribute rk_total_pools_kb_attr =
	__ATTR_RO(rk_total_pools_kb);

static struct attribute *rk_dma_heap_sysfs_attrs[] = {
	&rk_total_pools_kb_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(rk_dma_heap_sysfs);

static struct kobject *rk_dma_heap_kobject;

static int rk_dma_heap_sysfs_setup(void)
{
	int ret;

	rk_dma_heap_kobject = kobject_create_and_add("rk_dma_heap",
						     kernel_kobj);
	if (!rk_dma_heap_kobject)
		return -ENOMEM;

	ret = sysfs_create_groups(rk_dma_heap_kobject,
				  rk_dma_heap_sysfs_groups);
	if (ret) {
		kobject_put(rk_dma_heap_kobject);
		return ret;
	}

	return 0;
}

static void rk_dma_heap_sysfs_teardown(void)
{
	kobject_put(rk_dma_heap_kobject);
}

#ifdef CONFIG_DEBUG_FS

static struct dentry *rk_dma_heap_debugfs_dir;

static int rk_dma_heap_debug_show(struct seq_file *s, void *unused)
{
	struct rk_dma_heap *heap;
	unsigned long total = 0;

	mutex_lock(&rk_heap_list_lock);
	list_for_each_entry(heap, &rk_heap_list, list) {
		heap->s = s;
		get_each_dmabuf(rk_dma_heap_dump_dmabuf, heap);
		rk_dma_heap_dump_contig(heap);
		total += heap->total_size;
	}
	seq_printf(s, "\nTotal : 0x%lx (%lu KiB)\n", total, K(total));
	mutex_unlock(&rk_heap_list_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rk_dma_heap_debug);

static int rk_dma_heap_init_debugfs(void)
{
	struct dentry *d;
	int err = 0;

	d = debugfs_create_dir("rk_dma_heap", NULL);
	if (IS_ERR(d))
		return PTR_ERR(d);

	rk_dma_heap_debugfs_dir = d;

	d = debugfs_create_file("dma_heap_info", 0444,
				rk_dma_heap_debugfs_dir, NULL,
				&rk_dma_heap_debug_fops);
	if (IS_ERR(d)) {
		dma_heap_print("rk_dma_heap : debugfs: failed to create node bufinfo\n");
		debugfs_remove_recursive(rk_dma_heap_debugfs_dir);
		rk_dma_heap_debugfs_dir = NULL;
		err = PTR_ERR(d);
	}

	return err;
}
#else
static inline int rk_dma_heap_init_debugfs(void)
{
	return 0;
}
#endif

static int rk_dma_heap_proc_show(struct seq_file *s, void *unused)
{
	struct rk_dma_heap *heap;
	unsigned long total = 0;

	mutex_lock(&rk_heap_list_lock);
	list_for_each_entry(heap, &rk_heap_list, list) {
		heap->s = s;
		get_each_dmabuf(rk_dma_heap_dump_dmabuf, heap);
		rk_dma_heap_dump_contig(heap);
		total += heap->total_size;
	}
	seq_printf(s, "\nTotal : 0x%lx (%lu KiB)\n", total, K(total));
	mutex_unlock(&rk_heap_list_lock);

	return 0;
}

static int rk_dma_heap_info_proc_open(struct inode *inode,
						  struct file *file)
{
	return single_open(file, rk_dma_heap_proc_show, NULL);
}

static const struct proc_ops rk_dma_heap_info_proc_fops = {
	.proc_open	= rk_dma_heap_info_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int rk_dma_heap_init_proc(void)
{
	proc_rk_dma_heap_dir = proc_mkdir("rk_dma_heap", NULL);
	if (!proc_rk_dma_heap_dir) {
		pr_err("create rk_dma_heap proc dir error\n");
		return -ENOENT;
	}

	proc_create("dma_heap_info", 0644, proc_rk_dma_heap_dir,
		    &rk_dma_heap_info_proc_fops);

	return 0;
}

static int rk_dma_heap_init(void)
{
	int ret;

	ret = rk_dma_heap_sysfs_setup();
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&rk_dma_heap_devt, 0, NUM_HEAP_MINORS,
				  DEVNAME);
	if (ret)
		goto err_chrdev;

	rk_dma_heap_class = class_create(THIS_MODULE, DEVNAME);
	if (IS_ERR(rk_dma_heap_class)) {
		ret = PTR_ERR(rk_dma_heap_class);
		goto err_class;
	}
	rk_dma_heap_class->devnode = rk_dma_heap_devnode;

	rk_dma_heap_init_debugfs();
	rk_dma_heap_init_proc();

	return 0;

err_class:
	unregister_chrdev_region(rk_dma_heap_devt, NUM_HEAP_MINORS);
err_chrdev:
	rk_dma_heap_sysfs_teardown();
	return ret;
}
subsys_initcall(rk_dma_heap_init);
