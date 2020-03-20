// SPDX-License-Identifier: GPL-2.0
/*
 * ION Memory Allocator
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/bitmap.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/rbtree.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "ion_private.h"

#define ION_CURRENT_ABI_VERSION  2

static struct ion_device *internal_dev;

/* Entry into ION allocator for rest of the kernel */
struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
			  unsigned int flags)
{
	return ion_dmabuf_alloc(internal_dev, len, heap_id_mask, flags);
}
EXPORT_SYMBOL_GPL(ion_alloc);

int ion_free(struct ion_buffer *buffer)
{
	return ion_buffer_destroy(internal_dev, buffer);
}
EXPORT_SYMBOL_GPL(ion_free);

static int ion_alloc_fd(size_t len, unsigned int heap_id_mask,
			unsigned int flags)
{
	int fd;
	struct dma_buf *dmabuf;

	dmabuf = ion_dmabuf_alloc(internal_dev, len, heap_id_mask, flags);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (fd < 0)
		dma_buf_put(dmabuf);

	return fd;
}

size_t ion_query_heaps_kernel(struct ion_heap_data *hdata, size_t size)
{
	struct ion_device *dev = internal_dev;
	size_t i = 0, num_heaps = 0;
	struct ion_heap *heap;

	down_read(&dev->lock);

	// If size is 0, return without updating hdata.
	if (size == 0) {
		num_heaps = dev->heap_cnt;
		goto out;
	}

	plist_for_each_entry(heap, &dev->heaps, node) {
		strncpy(hdata[i].name, heap->name, MAX_HEAP_NAME);
		hdata[i].name[MAX_HEAP_NAME - 1] = '\0';
		hdata[i].type = heap->type;
		hdata[i].heap_id = heap->id;

		i++;
		if (i >= size)
			break;
	}

	num_heaps = i;
out:
	up_read(&dev->lock);
	return num_heaps;
}
EXPORT_SYMBOL_GPL(ion_query_heaps_kernel);

static int ion_query_heaps(struct ion_heap_query *query)
{
	struct ion_device *dev = internal_dev;
	struct ion_heap_data __user *buffer = u64_to_user_ptr(query->heaps);
	int ret = -EINVAL, cnt = 0, max_cnt;
	struct ion_heap *heap;
	struct ion_heap_data hdata;

	memset(&hdata, 0, sizeof(hdata));

	down_read(&dev->lock);
	if (!buffer) {
		query->cnt = dev->heap_cnt;
		ret = 0;
		goto out;
	}

	if (query->cnt <= 0)
		goto out;

	max_cnt = query->cnt;

	plist_for_each_entry(heap, &dev->heaps, node) {
		strncpy(hdata.name, heap->name, MAX_HEAP_NAME);
		hdata.name[sizeof(hdata.name) - 1] = '\0';
		hdata.type = heap->type;
		hdata.heap_id = heap->id;

		if (copy_to_user(&buffer[cnt], &hdata, sizeof(hdata))) {
			ret = -EFAULT;
			goto out;
		}

		cnt++;
		if (cnt >= max_cnt)
			break;
	}

	query->cnt = cnt;
	ret = 0;
out:
	up_read(&dev->lock);
	return ret;
}

union ion_ioctl_arg {
	struct ion_allocation_data allocation;
	struct ion_heap_query query;
	u32 ion_abi_version;
};

static int validate_ioctl_arg(unsigned int cmd, union ion_ioctl_arg *arg)
{
	switch (cmd) {
	case ION_IOC_HEAP_QUERY:
		if (arg->query.reserved0 ||
		    arg->query.reserved1 ||
		    arg->query.reserved2)
			return -EINVAL;
		break;
	default:
		break;
	}

	return 0;
}

static long ion_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	union ion_ioctl_arg data;

	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	/*
	 * The copy_from_user is unconditional here for both read and write
	 * to do the validate. If there is no write for the ioctl, the
	 * buffer is cleared
	 */
	if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	ret = validate_ioctl_arg(cmd, &data);
	if (ret) {
		pr_warn_once("%s: ioctl validate failed\n", __func__);
		return ret;
	}

	if (!(_IOC_DIR(cmd) & _IOC_WRITE))
		memset(&data, 0, sizeof(data));

	switch (cmd) {
	case ION_IOC_ALLOC:
	{
		int fd;

		fd = ion_alloc_fd(data.allocation.len,
				  data.allocation.heap_id_mask,
				  data.allocation.flags);
		if (fd < 0)
			return fd;

		data.allocation.fd = fd;

		break;
	}
	case ION_IOC_HEAP_QUERY:
		ret = ion_query_heaps(&data.query);
		break;
	case ION_IOC_ABI_VERSION:
		data.ion_abi_version = ION_CURRENT_ABI_VERSION;
		break;
	default:
		return -ENOTTY;
	}

	if (_IOC_DIR(cmd) & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd)))
			return -EFAULT;
	}
	return ret;
}

static const struct file_operations ion_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = ion_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

static int debug_shrink_set(void *data, u64 val)
{
	struct ion_heap *heap = data;
	struct shrink_control sc;
	int objs;

	sc.gfp_mask = GFP_HIGHUSER;
	sc.nr_to_scan = val;

	if (!val) {
		objs = heap->shrinker.count_objects(&heap->shrinker, &sc);
		sc.nr_to_scan = objs;
	}

	heap->shrinker.scan_objects(&heap->shrinker, &sc);
	return 0;
}

static int debug_shrink_get(void *data, u64 *val)
{
	struct ion_heap *heap = data;
	struct shrink_control sc;
	int objs;

	sc.gfp_mask = GFP_HIGHUSER;
	sc.nr_to_scan = 0;

	objs = heap->shrinker.count_objects(&heap->shrinker, &sc);
	*val = objs;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_shrink_fops, debug_shrink_get,
			debug_shrink_set, "%llu\n");

static int ion_assign_heap_id(struct ion_heap *heap, struct ion_device *dev)
{
	int id_bit = -EINVAL;
	int start_bit = -1, end_bit = -1;

	switch (heap->type) {
	case ION_HEAP_TYPE_SYSTEM:
		id_bit = __ffs(ION_HEAP_SYSTEM);
		break;
	case ION_HEAP_TYPE_DMA:
		start_bit = __ffs(ION_HEAP_DMA_START);
		end_bit = __ffs(ION_HEAP_DMA_END);
		break;
	case ION_HEAP_TYPE_CUSTOM ... ION_HEAP_TYPE_MAX:
		start_bit = __ffs(ION_HEAP_CUSTOM_START);
		end_bit = __ffs(ION_HEAP_CUSTOM_END);
		break;
	default:
		return -EINVAL;
	}

	/* For carveout, dma & custom heaps, we first let the heaps choose their
	 * own IDs. This allows the old behaviour of knowing the heap ids
	 * of these type of heaps  in advance in user space. If a heap with
	 * that ID already exists, it is an error.
	 *
	 * If the heap hasn't picked an id by itself, then we assign it
	 * one.
	 */
	if (id_bit < 0) {
		if (heap->id) {
			id_bit = __ffs(heap->id);
			if (id_bit < start_bit || id_bit > end_bit)
				return -EINVAL;
		} else {
			id_bit = find_next_zero_bit(dev->heap_ids, end_bit + 1,
						    start_bit);
			if (id_bit > end_bit)
				return -ENOSPC;
		}
	}

	if (test_and_set_bit(id_bit, dev->heap_ids))
		return -EEXIST;
	heap->id = id_bit;
	dev->heap_cnt++;

	return 0;
}

int __ion_device_add_heap(struct ion_heap *heap, struct module *owner)
{
	struct ion_device *dev = internal_dev;
	int ret;
	struct dentry *heap_root;
	char debug_name[64];

	if (!heap || !heap->ops || !heap->ops->allocate || !heap->ops->free) {
		pr_err("%s: invalid heap or heap_ops\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	heap->owner = owner;
	spin_lock_init(&heap->free_lock);
	spin_lock_init(&heap->stat_lock);
	heap->free_list_size = 0;

	if (heap->flags & ION_HEAP_FLAG_DEFER_FREE) {
		ret = ion_heap_init_deferred_free(heap);
		if (ret)
			goto out_heap_cleanup;
	}

	if ((heap->flags & ION_HEAP_FLAG_DEFER_FREE) || heap->ops->shrink) {
		ret = ion_heap_init_shrinker(heap);
		if (ret) {
			pr_err("%s: Failed to register shrinker\n", __func__);
			goto out_heap_cleanup;
		}
	}

	heap->num_of_buffers = 0;
	heap->num_of_alloc_bytes = 0;
	heap->alloc_bytes_wm = 0;

	heap_root = debugfs_create_dir(heap->name, dev->debug_root);
	debugfs_create_u64("num_of_buffers",
			   0444, heap_root,
			   &heap->num_of_buffers);
	debugfs_create_u64("num_of_alloc_bytes",
			   0444,
			   heap_root,
			   &heap->num_of_alloc_bytes);
	debugfs_create_u64("alloc_bytes_wm",
			   0444,
			   heap_root,
			   &heap->alloc_bytes_wm);

	if (heap->shrinker.count_objects &&
	    heap->shrinker.scan_objects) {
		snprintf(debug_name, 64, "%s_shrink", heap->name);
		debugfs_create_file(debug_name,
				    0644,
				    heap_root,
				    heap,
				    &debug_shrink_fops);
	}

	heap->debugfs_dir = heap_root;
	down_write(&dev->lock);
	ret = ion_assign_heap_id(heap, dev);
	if (ret) {
		pr_err("%s: Failed to assign heap id for heap type %x\n",
		       __func__, heap->type);
		up_write(&dev->lock);
		goto out_debugfs_cleanup;
	}

	/*
	 * use negative heap->id to reverse the priority -- when traversing
	 * the list later attempt higher id numbers first
	 */
	plist_node_init(&heap->node, -heap->id);
	plist_add(&heap->node, &dev->heaps);

	up_write(&dev->lock);

	return 0;

out_debugfs_cleanup:
	debugfs_remove_recursive(heap->debugfs_dir);
out_heap_cleanup:
	ion_heap_cleanup(heap);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(__ion_device_add_heap);

void ion_device_remove_heap(struct ion_heap *heap)
{
	struct ion_device *dev = internal_dev;

	if (!heap) {
		pr_err("%s: Invalid argument\n", __func__);
		return;
	}

	// take semaphore and remove the heap from dev->heap list
	down_write(&dev->lock);
	/* So no new allocations can happen from this heap */
	plist_del(&heap->node, &dev->heaps);
	if (ion_heap_cleanup(heap) != 0) {
		pr_warn("%s: failed to cleanup heap (%s)\n",
			__func__, heap->name);
	}
	debugfs_remove_recursive(heap->debugfs_dir);
	clear_bit(heap->id, dev->heap_ids);
	dev->heap_cnt--;
	up_write(&dev->lock);
}
EXPORT_SYMBOL_GPL(ion_device_remove_heap);

static ssize_t
total_heaps_kb_show(struct kobject *kobj, struct kobj_attribute *attr,
		    char *buf)
{
	return sprintf(buf, "%llu\n",
		       div_u64(ion_get_total_heap_bytes(), 1024));
}

static ssize_t
total_pools_kb_show(struct kobject *kobj, struct kobj_attribute *attr,
		    char *buf)
{
	struct ion_device *dev = internal_dev;
	struct ion_heap *heap;
	u64 total_pages = 0;

	down_read(&dev->lock);
	plist_for_each_entry(heap, &dev->heaps, node)
		if (heap->ops->get_pool_size)
			total_pages += heap->ops->get_pool_size(heap);
	up_read(&dev->lock);

	return sprintf(buf, "%llu\n", total_pages * (PAGE_SIZE / 1024));
}

static struct kobj_attribute total_heaps_kb_attr =
	__ATTR_RO(total_heaps_kb);

static struct kobj_attribute total_pools_kb_attr =
	__ATTR_RO(total_pools_kb);

static struct attribute *ion_device_attrs[] = {
	&total_heaps_kb_attr.attr,
	&total_pools_kb_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(ion_device);

static int ion_init_sysfs(void)
{
	struct kobject *ion_kobj;
	int ret;

	ion_kobj = kobject_create_and_add("ion", kernel_kobj);
	if (!ion_kobj)
		return -ENOMEM;

	ret = sysfs_create_groups(ion_kobj, ion_device_groups);
	if (ret) {
		kobject_put(ion_kobj);
		return ret;
	}

	return 0;
}

static int ion_device_create(void)
{
	struct ion_device *idev;
	int ret;

	idev = kzalloc(sizeof(*idev), GFP_KERNEL);
	if (!idev)
		return -ENOMEM;

	idev->dev.minor = MISC_DYNAMIC_MINOR;
	idev->dev.name = "ion";
	idev->dev.fops = &ion_fops;
	idev->dev.parent = NULL;
	ret = misc_register(&idev->dev);
	if (ret) {
		pr_err("ion: failed to register misc device.\n");
		goto err_reg;
	}

	ret = ion_init_sysfs();
	if (ret) {
		pr_err("ion: failed to add sysfs attributes.\n");
		goto err_sysfs;
	}

	idev->debug_root = debugfs_create_dir("ion", NULL);
	init_rwsem(&idev->lock);
	plist_head_init(&idev->heaps);
	internal_dev = idev;
	return 0;

err_sysfs:
	misc_deregister(&idev->dev);
err_reg:
	kfree(idev);
	return ret;
}
subsys_initcall(ion_device_create);
