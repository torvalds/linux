/*
 * drivers/gpu/ion/ion.c
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/ion.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/miscdevice.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/dma-buf.h>

#include "ion_priv.h"

/**
 * struct ion_device - the metadata of the ion device node
 * @dev:		the actual misc device
 * @buffers:	an rb tree of all the existing buffers
 * @lock:		lock protecting the buffers & heaps trees
 * @heaps:		list of all the heaps in the system
 * @user_clients:	list of all the clients created from userspace
 */
struct ion_device {
	struct miscdevice dev;
	struct rb_root buffers;
	struct mutex lock;
	struct rb_root heaps;
	long (*custom_ioctl) (struct ion_client *client, unsigned int cmd,
			      unsigned long arg);
	struct rb_root clients;
	struct dentry *debug_root;
};

/**
 * struct ion_client - a process/hw block local address space
 * @node:		node in the tree of all clients
 * @dev:		backpointer to ion device
 * @handles:		an rb tree of all the handles in this client
 * @lock:		lock protecting the tree of handles
 * @heap_mask:		mask of all supported heaps
 * @name:		used for debugging
 * @task:		used for debugging
 *
 * A client represents a list of buffers this client may access.
 * The mutex stored here is used to protect both handles tree
 * as well as the handles themselves, and should be held while modifying either.
 */
struct ion_client {
	struct rb_node node;
	struct ion_device *dev;
	struct rb_root handles;
	struct mutex lock;
	unsigned int heap_mask;
	const char *name;
	struct task_struct *task;
	pid_t pid;
	struct dentry *debug_root;
};

/**
 * ion_handle - a client local reference to a buffer
 * @ref:		reference count
 * @client:		back pointer to the client the buffer resides in
 * @buffer:		pointer to the buffer
 * @node:		node in the client's handle rbtree
 * @kmap_cnt:		count of times this client has mapped to kernel
 * @dmap_cnt:		count of times this client has mapped for dma
 *
 * Modifications to node, map_cnt or mapping should be protected by the
 * lock in the client.  Other fields are never changed after initialization.
 */
struct ion_handle {
	struct kref ref;
	struct ion_client *client;
	struct ion_buffer *buffer;
	struct rb_node node;
	unsigned int kmap_cnt;
};

bool ion_buffer_fault_user_mappings(struct ion_buffer *buffer)
{
        return ((buffer->flags & ION_FLAG_CACHED) &&
                !(buffer->flags & ION_FLAG_CACHED_NEEDS_SYNC));
}

/* this function should only be called while dev->lock is held */
static void ion_buffer_add(struct ion_device *dev,
			   struct ion_buffer *buffer)
{
	struct rb_node **p = &dev->buffers.rb_node;
	struct rb_node *parent = NULL;
	struct ion_buffer *entry;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct ion_buffer, node);

		if (buffer < entry) {
			p = &(*p)->rb_left;
		} else if (buffer > entry) {
			p = &(*p)->rb_right;
		} else {
			pr_err("%s: buffer already found.", __func__);
			BUG();
		}
	}

	rb_link_node(&buffer->node, parent, p);
	rb_insert_color(&buffer->node, &dev->buffers);
}

static int ion_buffer_alloc_dirty(struct ion_buffer *buffer);

/* this function should only be called while dev->lock is held */
static struct ion_buffer *ion_buffer_create(struct ion_heap *heap,
				     struct ion_device *dev,
				     unsigned long len,
				     unsigned long align,
				     unsigned long flags)
{
	struct ion_buffer *buffer;
	struct sg_table *table;
	struct scatterlist *sg;
	int i, ret;

	buffer = kzalloc(sizeof(struct ion_buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->heap = heap;
	buffer->flags = flags;
	kref_init(&buffer->ref);

	ret = heap->ops->allocate(heap, buffer, len, align, flags);
	if (ret) {
		kfree(buffer);
		return ERR_PTR(ret);
	}

	buffer->dev = dev;
	buffer->size = len;

	table = heap->ops->map_dma(heap, buffer);
	if (IS_ERR_OR_NULL(table)) {
		heap->ops->free(buffer);
		kfree(buffer);
		return ERR_PTR(PTR_ERR(table));
	}
	buffer->sg_table = table;
	if (ion_buffer_fault_user_mappings(buffer)) {
		for_each_sg(buffer->sg_table->sgl, sg, buffer->sg_table->nents,
			    i) {
			if (sg_dma_len(sg) == PAGE_SIZE)
				continue;
			pr_err("%s: cached mappings that will be faulted in "
			       "must have pagewise sg_lists\n", __func__);
			ret = -EINVAL;
			goto err;
		}

		ret = ion_buffer_alloc_dirty(buffer);
		if (ret)
			goto err;
	}

	buffer->dev = dev;
	buffer->size = len;
	INIT_LIST_HEAD(&buffer->vmas);
	mutex_init(&buffer->lock);
	/* this will set up dma addresses for the sglist -- it is not
	   technically correct as per the dma api -- a specific
	   device isn't really taking ownership here.  However, in practice on
	   our systems the only dma_address space is physical addresses.
	   Additionally, we can't afford the overhead of invalidating every
	   allocation via dma_map_sg. The implicit contract here is that
	   memory comming from the heaps is ready for dma, ie if it has a
	   cached mapping that mapping has been invalidated */
	for_each_sg(buffer->sg_table->sgl, sg, buffer->sg_table->nents, i)
		sg_dma_address(sg) = sg_phys(sg);
	ion_buffer_add(dev, buffer);
	return buffer;

err:
	heap->ops->unmap_dma(heap, buffer);
	heap->ops->free(buffer);
	kfree(buffer);
	return ERR_PTR(ret);
}

static void ion_buffer_destroy(struct kref *kref)
{
	struct ion_buffer *buffer = container_of(kref, struct ion_buffer, ref);
	struct ion_device *dev = buffer->dev;

	if (WARN_ON(buffer->kmap_cnt > 0))
		buffer->heap->ops->unmap_kernel(buffer->heap, buffer);
	buffer->heap->ops->unmap_dma(buffer->heap, buffer);
	buffer->heap->ops->free(buffer);
	mutex_lock(&dev->lock);
	rb_erase(&buffer->node, &dev->buffers);
	mutex_unlock(&dev->lock);
	if (buffer->flags & ION_FLAG_CACHED)
		kfree(buffer->dirty);
	kfree(buffer);
}

static void ion_buffer_get(struct ion_buffer *buffer)
{
	kref_get(&buffer->ref);
}

static int ion_buffer_put(struct ion_buffer *buffer)
{
	return kref_put(&buffer->ref, ion_buffer_destroy);
}

static void ion_buffer_add_to_handle(struct ion_buffer *buffer)
{
	mutex_lock(&buffer->dev->lock);
	buffer->handle_count++;
	mutex_unlock(&buffer->dev->lock);
}

static void ion_buffer_remove_from_handle(struct ion_buffer *buffer)
{
	/*
	 * when a buffer is removed from a handle, if it is not in
	 * any other handles, copy the taskcomm and the pid of the
	 * process it's being removed from into the buffer.  At this
	 * point there will be no way to track what processes this buffer is
	 * being used by, it only exists as a dma_buf file descriptor.
	 * The taskcomm and pid can provide a debug hint as to where this fd
	 * is in the system
	 */
	mutex_lock(&buffer->dev->lock);
	buffer->handle_count--;
	BUG_ON(buffer->handle_count < 0);
	if (!buffer->handle_count) {
		struct task_struct *task;

		task = current->group_leader;
		get_task_comm(buffer->task_comm, task);
		buffer->pid = task_pid_nr(task);
	}
	mutex_unlock(&buffer->dev->lock);
}

static struct ion_handle *ion_handle_create(struct ion_client *client,
				     struct ion_buffer *buffer)
{
	struct ion_handle *handle;

	handle = kzalloc(sizeof(struct ion_handle), GFP_KERNEL);
	if (!handle)
		return ERR_PTR(-ENOMEM);
	kref_init(&handle->ref);
	rb_init_node(&handle->node);
	handle->client = client;
	ion_buffer_get(buffer);
	ion_buffer_add_to_handle(buffer);
	handle->buffer = buffer;

	return handle;
}

static void ion_handle_kmap_put(struct ion_handle *);

static void ion_handle_destroy(struct kref *kref)
{
	struct ion_handle *handle = container_of(kref, struct ion_handle, ref);
	struct ion_client *client = handle->client;
	struct ion_buffer *buffer = handle->buffer;

	mutex_lock(&buffer->lock);
	while (handle->kmap_cnt)
		ion_handle_kmap_put(handle);
	mutex_unlock(&buffer->lock);

	if (!RB_EMPTY_NODE(&handle->node))
		rb_erase(&handle->node, &client->handles);

	ion_buffer_remove_from_handle(buffer);
	ion_buffer_put(buffer);

	kfree(handle);
}

struct ion_buffer *ion_handle_buffer(struct ion_handle *handle)
{
	return handle->buffer;
}

static void ion_handle_get(struct ion_handle *handle)
{
	kref_get(&handle->ref);
}

static int ion_handle_put(struct ion_handle *handle)
{
	return kref_put(&handle->ref, ion_handle_destroy);
}

static struct ion_handle *ion_handle_lookup(struct ion_client *client,
					    struct ion_buffer *buffer)
{
	struct rb_node *n;

	for (n = rb_first(&client->handles); n; n = rb_next(n)) {
		struct ion_handle *handle = rb_entry(n, struct ion_handle,
						     node);
		if (handle->buffer == buffer)
			return handle;
	}
	return NULL;
}

static bool ion_handle_validate(struct ion_client *client, struct ion_handle *handle)
{
	struct rb_node *n = client->handles.rb_node;

	while (n) {
		struct ion_handle *handle_node = rb_entry(n, struct ion_handle,
							  node);
		if (handle < handle_node)
			n = n->rb_left;
		else if (handle > handle_node)
			n = n->rb_right;
		else
			return true;
	}
	return false;
}

static void ion_handle_add(struct ion_client *client, struct ion_handle *handle)
{
	struct rb_node **p = &client->handles.rb_node;
	struct rb_node *parent = NULL;
	struct ion_handle *entry;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct ion_handle, node);

		if (handle < entry)
			p = &(*p)->rb_left;
		else if (handle > entry)
			p = &(*p)->rb_right;
		else
			WARN(1, "%s: buffer already found.", __func__);
	}

	rb_link_node(&handle->node, parent, p);
	rb_insert_color(&handle->node, &client->handles);
}

struct ion_handle *ion_alloc(struct ion_client *client, size_t len,
			     size_t align, unsigned int heap_mask,
			     unsigned int flags)
{
	struct rb_node *n;
	struct ion_handle *handle;
	struct ion_device *dev = client->dev;
	struct ion_buffer *buffer = NULL;

	pr_debug("%s: len %d align %d heap_mask %u flags %x\n", __func__, len,
		 align, heap_mask, flags);
	/*
	 * traverse the list of heaps available in this system in priority
	 * order.  If the heap type is supported by the client, and matches the
	 * request of the caller allocate from it.  Repeat until allocate has
	 * succeeded or all heaps have been tried
	 */
	if (WARN_ON(!len))
		return ERR_PTR(-EINVAL);

	len = PAGE_ALIGN(len);

	mutex_lock(&dev->lock);
	for (n = rb_first(&dev->heaps); n != NULL; n = rb_next(n)) {
		struct ion_heap *heap = rb_entry(n, struct ion_heap, node);
		/* if the client doesn't support this heap type */
		if (!((1 << heap->type) & client->heap_mask))
			continue;
		/* if the caller didn't specify this heap type */
		if (!((1 << heap->id) & heap_mask))
			continue;
		buffer = ion_buffer_create(heap, dev, len, align, flags);
		if (!IS_ERR_OR_NULL(buffer))
			break;
	}
	mutex_unlock(&dev->lock);

	if (buffer == NULL)
		return ERR_PTR(-ENODEV);

	if (IS_ERR(buffer))
		return ERR_PTR(PTR_ERR(buffer));

	handle = ion_handle_create(client, buffer);

	/*
	 * ion_buffer_create will create a buffer with a ref_cnt of 1,
	 * and ion_handle_create will take a second reference, drop one here
	 */
	ion_buffer_put(buffer);

	if (!IS_ERR(handle)) {
		mutex_lock(&client->lock);
		ion_handle_add(client, handle);
		mutex_unlock(&client->lock);
	}


	return handle;
}
EXPORT_SYMBOL(ion_alloc);

void ion_free(struct ion_client *client, struct ion_handle *handle)
{
	bool valid_handle;

	BUG_ON(client != handle->client);

	mutex_lock(&client->lock);
	valid_handle = ion_handle_validate(client, handle);

	if (!valid_handle) {
		WARN(1, "%s: invalid handle passed to free.\n", __func__);
		mutex_unlock(&client->lock);
		return;
	}
	ion_handle_put(handle);
	mutex_unlock(&client->lock);
}
EXPORT_SYMBOL(ion_free);

int ion_phys(struct ion_client *client, struct ion_handle *handle,
	     ion_phys_addr_t *addr, size_t *len)
{
	struct ion_buffer *buffer;
	int ret;

	mutex_lock(&client->lock);
	if (!ion_handle_validate(client, handle)) {
		mutex_unlock(&client->lock);
		return -EINVAL;
	}

	buffer = handle->buffer;

	if (!buffer->heap->ops->phys) {
		pr_err("%s: ion_phys is not implemented by this heap.\n",
		       __func__);
		mutex_unlock(&client->lock);
		return -ENODEV;
	}
	mutex_unlock(&client->lock);
	ret = buffer->heap->ops->phys(buffer->heap, buffer, addr, len);
	return ret;
}
EXPORT_SYMBOL(ion_phys);

static void *ion_buffer_kmap_get(struct ion_buffer *buffer)
{
	void *vaddr;

	if (buffer->kmap_cnt) {
		buffer->kmap_cnt++;
		return buffer->vaddr;
	}
	vaddr = buffer->heap->ops->map_kernel(buffer->heap, buffer);
	if (IS_ERR_OR_NULL(vaddr))
		return vaddr;
	buffer->vaddr = vaddr;
	buffer->kmap_cnt++;
	return vaddr;
}

static void *ion_handle_kmap_get(struct ion_handle *handle)
{
	struct ion_buffer *buffer = handle->buffer;
	void *vaddr;

	if (handle->kmap_cnt) {
		handle->kmap_cnt++;
		return buffer->vaddr;
	}
	vaddr = ion_buffer_kmap_get(buffer);
	if (IS_ERR_OR_NULL(vaddr))
		return vaddr;
	handle->kmap_cnt++;
	return vaddr;
}

static void ion_buffer_kmap_put(struct ion_buffer *buffer)
{
	buffer->kmap_cnt--;
	if (!buffer->kmap_cnt) {
		buffer->heap->ops->unmap_kernel(buffer->heap, buffer);
		buffer->vaddr = NULL;
	}
}

static void ion_handle_kmap_put(struct ion_handle *handle)
{
	struct ion_buffer *buffer = handle->buffer;

	handle->kmap_cnt--;
	if (!handle->kmap_cnt)
		ion_buffer_kmap_put(buffer);
}

void *ion_map_kernel(struct ion_client *client, struct ion_handle *handle)
{
	struct ion_buffer *buffer;
	void *vaddr;

	mutex_lock(&client->lock);
	if (!ion_handle_validate(client, handle)) {
		pr_err("%s: invalid handle passed to map_kernel.\n",
		       __func__);
		mutex_unlock(&client->lock);
		return ERR_PTR(-EINVAL);
	}

	buffer = handle->buffer;

	if (!handle->buffer->heap->ops->map_kernel) {
		pr_err("%s: map_kernel is not implemented by this heap.\n",
		       __func__);
		mutex_unlock(&client->lock);
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&buffer->lock);
	vaddr = ion_handle_kmap_get(handle);
	mutex_unlock(&buffer->lock);
	mutex_unlock(&client->lock);
	return vaddr;
}
EXPORT_SYMBOL(ion_map_kernel);

void ion_unmap_kernel(struct ion_client *client, struct ion_handle *handle)
{
	struct ion_buffer *buffer;

	mutex_lock(&client->lock);
	buffer = handle->buffer;
	mutex_lock(&buffer->lock);
	ion_handle_kmap_put(handle);
	mutex_unlock(&buffer->lock);
	mutex_unlock(&client->lock);
}
EXPORT_SYMBOL(ion_unmap_kernel);

static int ion_debug_client_show(struct seq_file *s, void *unused)
{
	struct ion_client *client = s->private;
	struct rb_node *n;
	size_t sizes[ION_NUM_HEAPS] = {0};
	const char *names[ION_NUM_HEAPS] = {0};
	int i;

	mutex_lock(&client->lock);
	for (n = rb_first(&client->handles); n; n = rb_next(n)) {
		struct ion_handle *handle = rb_entry(n, struct ion_handle,
						     node);
		enum ion_heap_type type = handle->buffer->heap->type;

		if (!names[type])
			names[type] = handle->buffer->heap->name;
		sizes[type] += handle->buffer->size;
	}
	mutex_unlock(&client->lock);

	seq_printf(s, "%16.16s: %16.16s\n", "heap_name", "size_in_bytes");
	for (i = 0; i < ION_NUM_HEAPS; i++) {
		if (!names[i])
			continue;
		seq_printf(s, "%16.16s: %16u\n", names[i], sizes[i]);
	}
	return 0;
}

static int ion_debug_client_open(struct inode *inode, struct file *file)
{
	return single_open(file, ion_debug_client_show, inode->i_private);
}

static const struct file_operations debug_client_fops = {
	.open = ion_debug_client_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

struct ion_client *ion_client_create(struct ion_device *dev,
				     unsigned int heap_mask,
				     const char *name)
{
	struct ion_client *client;
	struct task_struct *task;
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct ion_client *entry;
	char debug_name[64];
	pid_t pid;

	get_task_struct(current->group_leader);
	task_lock(current->group_leader);
	pid = task_pid_nr(current->group_leader);
	/* don't bother to store task struct for kernel threads,
	   they can't be killed anyway */
	if (current->group_leader->flags & PF_KTHREAD) {
		put_task_struct(current->group_leader);
		task = NULL;
	} else {
		task = current->group_leader;
	}
	task_unlock(current->group_leader);

	client = kzalloc(sizeof(struct ion_client), GFP_KERNEL);
	if (!client) {
		if (task)
			put_task_struct(current->group_leader);
		return ERR_PTR(-ENOMEM);
	}

	client->dev = dev;
	client->handles = RB_ROOT;
	mutex_init(&client->lock);
	client->name = name;
	client->heap_mask = heap_mask;
	client->task = task;
	client->pid = pid;

	mutex_lock(&dev->lock);
	p = &dev->clients.rb_node;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct ion_client, node);

		if (client < entry)
			p = &(*p)->rb_left;
		else if (client > entry)
			p = &(*p)->rb_right;
	}
	rb_link_node(&client->node, parent, p);
	rb_insert_color(&client->node, &dev->clients);

	snprintf(debug_name, 64, "%u", client->pid);
	client->debug_root = debugfs_create_file(debug_name, 0664,
						 dev->debug_root, client,
						 &debug_client_fops);
	mutex_unlock(&dev->lock);

	return client;
}

void ion_client_destroy(struct ion_client *client)
{
	struct ion_device *dev = client->dev;
	struct rb_node *n;

	pr_debug("%s: %d\n", __func__, __LINE__);
	while ((n = rb_first(&client->handles))) {
		struct ion_handle *handle = rb_entry(n, struct ion_handle,
						     node);
		ion_handle_destroy(&handle->ref);
	}
	mutex_lock(&dev->lock);
	if (client->task)
		put_task_struct(client->task);
	rb_erase(&client->node, &dev->clients);
	debugfs_remove_recursive(client->debug_root);
	mutex_unlock(&dev->lock);

	kfree(client);
}
EXPORT_SYMBOL(ion_client_destroy);

struct sg_table *ion_sg_table(struct ion_client *client,
			      struct ion_handle *handle)
{
	struct ion_buffer *buffer;
	struct sg_table *table;

	mutex_lock(&client->lock);
	if (!ion_handle_validate(client, handle)) {
		pr_err("%s: invalid handle passed to map_dma.\n",
		       __func__);
		mutex_unlock(&client->lock);
		return ERR_PTR(-EINVAL);
	}
	buffer = handle->buffer;
	table = buffer->sg_table;
	mutex_unlock(&client->lock);
	return table;
}
EXPORT_SYMBOL(ion_sg_table);

static void ion_buffer_sync_for_device(struct ion_buffer *buffer,
				       struct device *dev,
				       enum dma_data_direction direction);

static struct sg_table *ion_map_dma_buf(struct dma_buf_attachment *attachment,
					enum dma_data_direction direction)
{
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct ion_buffer *buffer = dmabuf->priv;

	ion_buffer_sync_for_device(buffer, attachment->dev, direction);
	return buffer->sg_table;
}

static void ion_unmap_dma_buf(struct dma_buf_attachment *attachment,
			      struct sg_table *table,
			      enum dma_data_direction direction)
{
}

static int ion_buffer_alloc_dirty(struct ion_buffer *buffer)
{
	unsigned long pages = buffer->sg_table->nents;
	unsigned long length = (pages + BITS_PER_LONG - 1)/BITS_PER_LONG;

	buffer->dirty = kzalloc(length * sizeof(unsigned long), GFP_KERNEL);
	if (!buffer->dirty)
		return -ENOMEM;
	return 0;
}

struct ion_vma_list {
	struct list_head list;
	struct vm_area_struct *vma;
};

static void ion_buffer_sync_for_device(struct ion_buffer *buffer,
				       struct device *dev,
				       enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;
	struct ion_vma_list *vma_list;

	pr_debug("%s: syncing for device %s\n", __func__,
		 dev ? dev_name(dev) : "null");

	if (!ion_buffer_fault_user_mappings(buffer))
		return;

	mutex_lock(&buffer->lock);
	for_each_sg(buffer->sg_table->sgl, sg, buffer->sg_table->nents, i) {
		if (!test_bit(i, buffer->dirty))
			continue;
		dma_sync_sg_for_device(dev, sg, 1, dir);
		clear_bit(i, buffer->dirty);
	}
	list_for_each_entry(vma_list, &buffer->vmas, list) {
		struct vm_area_struct *vma = vma_list->vma;

		zap_page_range(vma, vma->vm_start, vma->vm_end - vma->vm_start,
			       NULL);
	}
	mutex_unlock(&buffer->lock);
}

int ion_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct ion_buffer *buffer = vma->vm_private_data;
	struct scatterlist *sg;
	int i;

	mutex_lock(&buffer->lock);
	set_bit(vmf->pgoff, buffer->dirty);

	for_each_sg(buffer->sg_table->sgl, sg, buffer->sg_table->nents, i) {
		if (i != vmf->pgoff)
			continue;
		dma_sync_sg_for_cpu(NULL, sg, 1, DMA_BIDIRECTIONAL);
		vm_insert_page(vma, (unsigned long)vmf->virtual_address,
			       sg_page(sg));
		break;
	}
	mutex_unlock(&buffer->lock);
	return VM_FAULT_NOPAGE;
}

static void ion_vm_open(struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = vma->vm_private_data;
	struct ion_vma_list *vma_list;

	vma_list = kmalloc(sizeof(struct ion_vma_list), GFP_KERNEL);
	if (!vma_list)
		return;
	vma_list->vma = vma;
	mutex_lock(&buffer->lock);
	list_add(&vma_list->list, &buffer->vmas);
	mutex_unlock(&buffer->lock);
	pr_debug("%s: adding %p\n", __func__, vma);
}

static void ion_vm_close(struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = vma->vm_private_data;
	struct ion_vma_list *vma_list, *tmp;

	pr_debug("%s\n", __func__);
	mutex_lock(&buffer->lock);
	list_for_each_entry_safe(vma_list, tmp, &buffer->vmas, list) {
		if (vma_list->vma != vma)
			continue;
		list_del(&vma_list->list);
		kfree(vma_list);
		pr_debug("%s: deleting %p\n", __func__, vma);
		break;
	}
	mutex_unlock(&buffer->lock);
}

struct vm_operations_struct ion_vma_ops = {
	.open = ion_vm_open,
	.close = ion_vm_close,
	.fault = ion_vm_fault,
};

static int ion_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = dmabuf->priv;
	int ret = 0;

	if (!buffer->heap->ops->map_user) {
		pr_err("%s: this heap does not define a method for mapping "
		       "to userspace\n", __func__);
		return -EINVAL;
	}

	if (ion_buffer_fault_user_mappings(buffer)) {
		vma->vm_private_data = buffer;
		vma->vm_ops = &ion_vma_ops;
		ion_vm_open(vma);
		return 0;
	}

	if (!(buffer->flags & ION_FLAG_CACHED))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	mutex_lock(&buffer->lock);
	/* now map it to userspace */
	ret = buffer->heap->ops->map_user(buffer->heap, buffer, vma);
	mutex_unlock(&buffer->lock);

	if (ret)
		pr_err("%s: failure mapping buffer to userspace\n",
		       __func__);

	return ret;
}

static void ion_dma_buf_release(struct dma_buf *dmabuf)
{
	struct ion_buffer *buffer = dmabuf->priv;
	ion_buffer_put(buffer);
}

static void *ion_dma_buf_kmap(struct dma_buf *dmabuf, unsigned long offset)
{
	struct ion_buffer *buffer = dmabuf->priv;
	return buffer->vaddr + offset * PAGE_SIZE;
}

static void ion_dma_buf_kunmap(struct dma_buf *dmabuf, unsigned long offset,
			       void *ptr)
{
	return;
}

static int ion_dma_buf_begin_cpu_access(struct dma_buf *dmabuf, size_t start,
					size_t len,
					enum dma_data_direction direction)
{
	struct ion_buffer *buffer = dmabuf->priv;
	void *vaddr;

	if (!buffer->heap->ops->map_kernel) {
		pr_err("%s: map kernel is not implemented by this heap.\n",
		       __func__);
		return -ENODEV;
	}

	mutex_lock(&buffer->lock);
	vaddr = ion_buffer_kmap_get(buffer);
	mutex_unlock(&buffer->lock);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);
	if (!vaddr)
		return -ENOMEM;
	return 0;
}

static void ion_dma_buf_end_cpu_access(struct dma_buf *dmabuf, size_t start,
				       size_t len,
				       enum dma_data_direction direction)
{
	struct ion_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	ion_buffer_kmap_put(buffer);
	mutex_unlock(&buffer->lock);
}

struct dma_buf_ops dma_buf_ops = {
	.map_dma_buf = ion_map_dma_buf,
	.unmap_dma_buf = ion_unmap_dma_buf,
	.mmap = ion_mmap,
	.release = ion_dma_buf_release,
	.begin_cpu_access = ion_dma_buf_begin_cpu_access,
	.end_cpu_access = ion_dma_buf_end_cpu_access,
	.kmap_atomic = ion_dma_buf_kmap,
	.kunmap_atomic = ion_dma_buf_kunmap,
	.kmap = ion_dma_buf_kmap,
	.kunmap = ion_dma_buf_kunmap,
};

int ion_share_dma_buf(struct ion_client *client, struct ion_handle *handle)
{
	struct ion_buffer *buffer;
	struct dma_buf *dmabuf;
	bool valid_handle;
	int fd;

	mutex_lock(&client->lock);
	valid_handle = ion_handle_validate(client, handle);
	mutex_unlock(&client->lock);
	if (!valid_handle) {
		WARN(1, "%s: invalid handle passed to share.\n", __func__);
		return -EINVAL;
	}

	buffer = handle->buffer;
	ion_buffer_get(buffer);
	dmabuf = dma_buf_export(buffer, &dma_buf_ops, buffer->size, O_RDWR);
	if (IS_ERR(dmabuf)) {
		ion_buffer_put(buffer);
		return PTR_ERR(dmabuf);
	}
	fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (fd < 0)
		dma_buf_put(dmabuf);

	return fd;
}
EXPORT_SYMBOL(ion_share_dma_buf);

struct ion_handle *ion_import_dma_buf(struct ion_client *client, int fd)
{
	struct dma_buf *dmabuf;
	struct ion_buffer *buffer;
	struct ion_handle *handle;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf))
		return ERR_PTR(PTR_ERR(dmabuf));
	/* if this memory came from ion */

	if (dmabuf->ops != &dma_buf_ops) {
		pr_err("%s: can not import dmabuf from another exporter\n",
		       __func__);
		dma_buf_put(dmabuf);
		return ERR_PTR(-EINVAL);
	}
	buffer = dmabuf->priv;

	mutex_lock(&client->lock);
	/* if a handle exists for this buffer just take a reference to it */
	handle = ion_handle_lookup(client, buffer);
	if (!IS_ERR_OR_NULL(handle)) {
		ion_handle_get(handle);
		goto end;
	}
	handle = ion_handle_create(client, buffer);
	if (IS_ERR_OR_NULL(handle))
		goto end;
	ion_handle_add(client, handle);
end:
	mutex_unlock(&client->lock);
	dma_buf_put(dmabuf);
	return handle;
}
EXPORT_SYMBOL(ion_import_dma_buf);

static int ion_sync_for_device(struct ion_client *client, int fd)
{
	struct dma_buf *dmabuf;
	struct ion_buffer *buffer;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf))
		return PTR_ERR(dmabuf);

	/* if this memory came from ion */
	if (dmabuf->ops != &dma_buf_ops) {
		pr_err("%s: can not sync dmabuf from another exporter\n",
		       __func__);
		dma_buf_put(dmabuf);
		return -EINVAL;
	}
	buffer = dmabuf->priv;

	dma_sync_sg_for_device(NULL, buffer->sg_table->sgl,
			       buffer->sg_table->nents, DMA_BIDIRECTIONAL);
	dma_buf_put(dmabuf);
	return 0;
}

static long ion_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ion_client *client = filp->private_data;

	switch (cmd) {
	case ION_IOC_ALLOC:
	{
		struct ion_allocation_data data;

		if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
			return -EFAULT;
		data.handle = ion_alloc(client, data.len, data.align,
					     data.heap_mask, data.flags);

		if (IS_ERR(data.handle))
			return PTR_ERR(data.handle);

		if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
			ion_free(client, data.handle);
			return -EFAULT;
		}
		break;
	}
	case ION_IOC_FREE:
	{
		struct ion_handle_data data;
		bool valid;

		if (copy_from_user(&data, (void __user *)arg,
				   sizeof(struct ion_handle_data)))
			return -EFAULT;
		mutex_lock(&client->lock);
		valid = ion_handle_validate(client, data.handle);
		mutex_unlock(&client->lock);
		if (!valid)
			return -EINVAL;
		ion_free(client, data.handle);
		break;
	}
	case ION_IOC_SHARE:
	{
		struct ion_fd_data data;

		if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
			return -EFAULT;
		data.fd = ion_share_dma_buf(client, data.handle);
		if (copy_to_user((void __user *)arg, &data, sizeof(data)))
			return -EFAULT;
		if (data.fd < 0)
			return data.fd;
		break;
	}
	case ION_IOC_IMPORT:
	{
		struct ion_fd_data data;
		int ret = 0;
		if (copy_from_user(&data, (void __user *)arg,
				   sizeof(struct ion_fd_data)))
			return -EFAULT;
		data.handle = ion_import_dma_buf(client, data.fd);
		if (IS_ERR(data.handle)) {
			ret = PTR_ERR(data.handle);
			data.handle = NULL;
		}
		if (copy_to_user((void __user *)arg, &data,
				 sizeof(struct ion_fd_data)))
			return -EFAULT;
		if (ret < 0)
			return ret;
		break;
	}
	case ION_IOC_SYNC:
	{
		struct ion_fd_data data;
		if (copy_from_user(&data, (void __user *)arg,
				   sizeof(struct ion_fd_data)))
			return -EFAULT;
		ion_sync_for_device(client, data.fd);
		break;
	}
	case ION_IOC_CUSTOM:
	{
		struct ion_device *dev = client->dev;
		struct ion_custom_data data;

		if (!dev->custom_ioctl)
			return -ENOTTY;
		if (copy_from_user(&data, (void __user *)arg,
				sizeof(struct ion_custom_data)))
			return -EFAULT;
		return dev->custom_ioctl(client, data.cmd, data.arg);
	}
	default:
		return -ENOTTY;
	}
	return 0;
}

static int ion_release(struct inode *inode, struct file *file)
{
	struct ion_client *client = file->private_data;

	pr_debug("%s: %d\n", __func__, __LINE__);
	ion_client_destroy(client);
	return 0;
}

static int ion_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct ion_device *dev = container_of(miscdev, struct ion_device, dev);
	struct ion_client *client;

	pr_debug("%s: %d\n", __func__, __LINE__);
	client = ion_client_create(dev, -1, "user");
	if (IS_ERR_OR_NULL(client))
		return PTR_ERR(client);
	file->private_data = client;

	return 0;
}

static const struct file_operations ion_fops = {
	.owner          = THIS_MODULE,
	.open           = ion_open,
	.release        = ion_release,
	.unlocked_ioctl = ion_ioctl,
};

static size_t ion_debug_heap_total(struct ion_client *client,
				   enum ion_heap_type type)
{
	size_t size = 0;
	struct rb_node *n;

	mutex_lock(&client->lock);
	for (n = rb_first(&client->handles); n; n = rb_next(n)) {
		struct ion_handle *handle = rb_entry(n,
						     struct ion_handle,
						     node);
		if (handle->buffer->heap->type == type)
			size += handle->buffer->size;
	}
	mutex_unlock(&client->lock);
	return size;
}

static int ion_debug_heap_show(struct seq_file *s, void *unused)
{
	struct ion_heap *heap = s->private;
	struct ion_device *dev = heap->dev;
	struct rb_node *n;
	size_t total_size = 0;
	size_t total_orphaned_size = 0;

	seq_printf(s, "%16.s %16.s %16.s\n", "client", "pid", "size");
	seq_printf(s, "----------------------------------------------------\n");

	for (n = rb_first(&dev->clients); n; n = rb_next(n)) {
		struct ion_client *client = rb_entry(n, struct ion_client,
						     node);
		size_t size = ion_debug_heap_total(client, heap->type);
		if (!size)
			continue;
		if (client->task) {
			char task_comm[TASK_COMM_LEN];

			get_task_comm(task_comm, client->task);
			seq_printf(s, "%16.s %16u %16u\n", task_comm,
				   client->pid, size);
		} else {
			seq_printf(s, "%16.s %16u %16u\n", client->name,
				   client->pid, size);
		}
	}
	seq_printf(s, "----------------------------------------------------\n");
	seq_printf(s, "orphaned allocations (info is from last known client):"
		   "\n");
	mutex_lock(&dev->lock);
	for (n = rb_first(&dev->buffers); n; n = rb_next(n)) {
		struct ion_buffer *buffer = rb_entry(n, struct ion_buffer,
						     node);
		if (buffer->heap->type == heap->type)
			total_size += buffer->size;
		if (!buffer->handle_count) {
			seq_printf(s, "%16.s %16u %16u\n", buffer->task_comm,
				   buffer->pid, buffer->size);
			total_orphaned_size += buffer->size;
		}
	}
	mutex_unlock(&dev->lock);
	seq_printf(s, "----------------------------------------------------\n");
	seq_printf(s, "%16.s %16u\n", "total orphaned",
		   total_orphaned_size);
	seq_printf(s, "%16.s %16u\n", "total ", total_size);

	return 0;
}

static int ion_debug_heap_open(struct inode *inode, struct file *file)
{
	return single_open(file, ion_debug_heap_show, inode->i_private);
}

static const struct file_operations debug_heap_fops = {
	.open = ion_debug_heap_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void ion_device_add_heap(struct ion_device *dev, struct ion_heap *heap)
{
	struct rb_node **p = &dev->heaps.rb_node;
	struct rb_node *parent = NULL;
	struct ion_heap *entry;

	if (!heap->ops->allocate || !heap->ops->free || !heap->ops->map_dma ||
	    !heap->ops->unmap_dma)
		pr_err("%s: can not add heap with invalid ops struct.\n",
		       __func__);

	heap->dev = dev;
	mutex_lock(&dev->lock);
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct ion_heap, node);

		if (heap->id < entry->id) {
			p = &(*p)->rb_left;
		} else if (heap->id > entry->id ) {
			p = &(*p)->rb_right;
		} else {
			pr_err("%s: can not insert multiple heaps with "
				"id %d\n", __func__, heap->id);
			goto end;
		}
	}

	rb_link_node(&heap->node, parent, p);
	rb_insert_color(&heap->node, &dev->heaps);
	debugfs_create_file(heap->name, 0664, dev->debug_root, heap,
			    &debug_heap_fops);
end:
	mutex_unlock(&dev->lock);
}

struct ion_device *ion_device_create(long (*custom_ioctl)
				     (struct ion_client *client,
				      unsigned int cmd,
				      unsigned long arg))
{
	struct ion_device *idev;
	int ret;

	idev = kzalloc(sizeof(struct ion_device), GFP_KERNEL);
	if (!idev)
		return ERR_PTR(-ENOMEM);

	idev->dev.minor = MISC_DYNAMIC_MINOR;
	idev->dev.name = "ion";
	idev->dev.fops = &ion_fops;
	idev->dev.parent = NULL;
	ret = misc_register(&idev->dev);
	if (ret) {
		pr_err("ion: failed to register misc device.\n");
		return ERR_PTR(ret);
	}

	idev->debug_root = debugfs_create_dir("ion", NULL);
	if (IS_ERR_OR_NULL(idev->debug_root))
		pr_err("ion: failed to create debug files.\n");

	idev->custom_ioctl = custom_ioctl;
	idev->buffers = RB_ROOT;
	mutex_init(&idev->lock);
	idev->heaps = RB_ROOT;
	idev->clients = RB_ROOT;
	return idev;
}

void ion_device_destroy(struct ion_device *dev)
{
	misc_deregister(&dev->dev);
	/* XXX need to free the heaps and clients ? */
	kfree(dev);
}

void __init ion_reserve(struct ion_platform_data *data)
{
	int i, ret;

	for (i = 0; i < data->nr; i++) {
		if (data->heaps[i].size == 0)
			continue;
		ret = memblock_reserve(data->heaps[i].base,
				       data->heaps[i].size);
		if (ret)
			pr_err("memblock reserve of %x@%lx failed\n",
			       data->heaps[i].size,
			       data->heaps[i].base);
	}
}
