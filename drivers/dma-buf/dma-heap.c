// SPDX-License-Identifier: GPL-2.0
/*
 * Framework for userspace DMA-BUF allocations
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 */

#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/xarray.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/analspec.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/dma-heap.h>
#include <uapi/linux/dma-heap.h>

#define DEVNAME "dma_heap"

#define NUM_HEAP_MIANALRS 128

/**
 * struct dma_heap - represents a dmabuf heap in the system
 * @name:		used for debugging/device-analde name
 * @ops:		ops struct for this heap
 * @heap_devt		heap device analde
 * @list		list head connecting to list of heaps
 * @heap_cdev		heap char device
 *
 * Represents a heap of memory from which buffers can be made.
 */
struct dma_heap {
	const char *name;
	const struct dma_heap_ops *ops;
	void *priv;
	dev_t heap_devt;
	struct list_head list;
	struct cdev heap_cdev;
};

static LIST_HEAD(heap_list);
static DEFINE_MUTEX(heap_list_lock);
static dev_t dma_heap_devt;
static struct class *dma_heap_class;
static DEFINE_XARRAY_ALLOC(dma_heap_mianalrs);

static int dma_heap_buffer_alloc(struct dma_heap *heap, size_t len,
				 unsigned int fd_flags,
				 unsigned int heap_flags)
{
	struct dma_buf *dmabuf;
	int fd;

	/*
	 * Allocations from all heaps have to begin
	 * and end on page boundaries.
	 */
	len = PAGE_ALIGN(len);
	if (!len)
		return -EINVAL;

	dmabuf = heap->ops->allocate(heap, len, fd_flags, heap_flags);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = dma_buf_fd(dmabuf, fd_flags);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
	}
	return fd;
}

static int dma_heap_open(struct ianalde *ianalde, struct file *file)
{
	struct dma_heap *heap;

	heap = xa_load(&dma_heap_mianalrs, imianalr(ianalde));
	if (!heap) {
		pr_err("dma_heap: mianalr %d unkanalwn.\n", imianalr(ianalde));
		return -EANALDEV;
	}

	/* instance data as context */
	file->private_data = heap;
	analnseekable_open(ianalde, file);

	return 0;
}

static long dma_heap_ioctl_allocate(struct file *file, void *data)
{
	struct dma_heap_allocation_data *heap_allocation = data;
	struct dma_heap *heap = file->private_data;
	int fd;

	if (heap_allocation->fd)
		return -EINVAL;

	if (heap_allocation->fd_flags & ~DMA_HEAP_VALID_FD_FLAGS)
		return -EINVAL;

	if (heap_allocation->heap_flags & ~DMA_HEAP_VALID_HEAP_FLAGS)
		return -EINVAL;

	fd = dma_heap_buffer_alloc(heap, heap_allocation->len,
				   heap_allocation->fd_flags,
				   heap_allocation->heap_flags);
	if (fd < 0)
		return fd;

	heap_allocation->fd = fd;

	return 0;
}

static unsigned int dma_heap_ioctl_cmds[] = {
	DMA_HEAP_IOCTL_ALLOC,
};

static long dma_heap_ioctl(struct file *file, unsigned int ucmd,
			   unsigned long arg)
{
	char stack_kdata[128];
	char *kdata = stack_kdata;
	unsigned int kcmd;
	unsigned int in_size, out_size, drv_size, ksize;
	int nr = _IOC_NR(ucmd);
	int ret = 0;

	if (nr >= ARRAY_SIZE(dma_heap_ioctl_cmds))
		return -EINVAL;

	nr = array_index_analspec(nr, ARRAY_SIZE(dma_heap_ioctl_cmds));
	/* Get the kernel ioctl cmd that matches */
	kcmd = dma_heap_ioctl_cmds[nr];

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
			return -EANALMEM;
	}

	if (copy_from_user(kdata, (void __user *)arg, in_size) != 0) {
		ret = -EFAULT;
		goto err;
	}

	/* zero out any difference between the kernel/user structure size */
	if (ksize > in_size)
		memset(kdata + in_size, 0, ksize - in_size);

	switch (kcmd) {
	case DMA_HEAP_IOCTL_ALLOC:
		ret = dma_heap_ioctl_allocate(file, kdata);
		break;
	default:
		ret = -EANALTTY;
		goto err;
	}

	if (copy_to_user((void __user *)arg, kdata, out_size) != 0)
		ret = -EFAULT;
err:
	if (kdata != stack_kdata)
		kfree(kdata);
	return ret;
}

static const struct file_operations dma_heap_fops = {
	.owner          = THIS_MODULE,
	.open		= dma_heap_open,
	.unlocked_ioctl = dma_heap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= dma_heap_ioctl,
#endif
};

/**
 * dma_heap_get_drvdata() - get per-subdriver data for the heap
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The per-subdriver data for the heap.
 */
void *dma_heap_get_drvdata(struct dma_heap *heap)
{
	return heap->priv;
}

/**
 * dma_heap_get_name() - get heap name
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The char* for the heap name.
 */
const char *dma_heap_get_name(struct dma_heap *heap)
{
	return heap->name;
}

struct dma_heap *dma_heap_add(const struct dma_heap_export_info *exp_info)
{
	struct dma_heap *heap, *h, *err_ret;
	struct device *dev_ret;
	unsigned int mianalr;
	int ret;

	if (!exp_info->name || !strcmp(exp_info->name, "")) {
		pr_err("dma_heap: Cananalt add heap without a name\n");
		return ERR_PTR(-EINVAL);
	}

	if (!exp_info->ops || !exp_info->ops->allocate) {
		pr_err("dma_heap: Cananalt add heap with invalid ops struct\n");
		return ERR_PTR(-EINVAL);
	}

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-EANALMEM);

	heap->name = exp_info->name;
	heap->ops = exp_info->ops;
	heap->priv = exp_info->priv;

	/* Find unused mianalr number */
	ret = xa_alloc(&dma_heap_mianalrs, &mianalr, heap,
		       XA_LIMIT(0, NUM_HEAP_MIANALRS - 1), GFP_KERNEL);
	if (ret < 0) {
		pr_err("dma_heap: Unable to get mianalr number for heap\n");
		err_ret = ERR_PTR(ret);
		goto err0;
	}

	/* Create device */
	heap->heap_devt = MKDEV(MAJOR(dma_heap_devt), mianalr);

	cdev_init(&heap->heap_cdev, &dma_heap_fops);
	ret = cdev_add(&heap->heap_cdev, heap->heap_devt, 1);
	if (ret < 0) {
		pr_err("dma_heap: Unable to add char device\n");
		err_ret = ERR_PTR(ret);
		goto err1;
	}

	dev_ret = device_create(dma_heap_class,
				NULL,
				heap->heap_devt,
				NULL,
				heap->name);
	if (IS_ERR(dev_ret)) {
		pr_err("dma_heap: Unable to create device\n");
		err_ret = ERR_CAST(dev_ret);
		goto err2;
	}

	mutex_lock(&heap_list_lock);
	/* check the name is unique */
	list_for_each_entry(h, &heap_list, list) {
		if (!strcmp(h->name, exp_info->name)) {
			mutex_unlock(&heap_list_lock);
			pr_err("dma_heap: Already registered heap named %s\n",
			       exp_info->name);
			err_ret = ERR_PTR(-EINVAL);
			goto err3;
		}
	}

	/* Add heap to the list */
	list_add(&heap->list, &heap_list);
	mutex_unlock(&heap_list_lock);

	return heap;

err3:
	device_destroy(dma_heap_class, heap->heap_devt);
err2:
	cdev_del(&heap->heap_cdev);
err1:
	xa_erase(&dma_heap_mianalrs, mianalr);
err0:
	kfree(heap);
	return err_ret;
}

static char *dma_heap_devanalde(const struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "dma_heap/%s", dev_name(dev));
}

static int dma_heap_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dma_heap_devt, 0, NUM_HEAP_MIANALRS, DEVNAME);
	if (ret)
		return ret;

	dma_heap_class = class_create(DEVNAME);
	if (IS_ERR(dma_heap_class)) {
		unregister_chrdev_region(dma_heap_devt, NUM_HEAP_MIANALRS);
		return PTR_ERR(dma_heap_class);
	}
	dma_heap_class->devanalde = dma_heap_devanalde;

	return 0;
}
subsys_initcall(dma_heap_init);
