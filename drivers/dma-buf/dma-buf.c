// SPDX-License-Identifier: GPL-2.0-only
/*
 * Framework for buffer objects that can be shared across devices/subsystems.
 *
 * Copyright(C) 2011 Linaro Limited. All rights reserved.
 * Author: Sumit Semwal <sumit.semwal@ti.com>
 *
 * Many thanks to linaro-mm-sig list, and specially
 * Arnd Bergmann <arnd@arndb.de>, Rob Clark <rob@ti.com> and
 * Daniel Vetter <daniel@ffwll.ch> for their support in creation and
 * refining of this idea.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/anon_inodes.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/reservation.h>
#include <linux/mm.h>

#include <uapi/linux/dma-buf.h>

static inline int is_dma_buf_file(struct file *);

struct dma_buf_list {
	struct list_head head;
	struct mutex lock;
};

static struct dma_buf_list db_list;

static int dma_buf_release(struct inode *inode, struct file *file)
{
	struct dma_buf *dmabuf;

	if (!is_dma_buf_file(file))
		return -EINVAL;

	dmabuf = file->private_data;

	BUG_ON(dmabuf->vmapping_counter);

	/*
	 * Any fences that a dma-buf poll can wait on should be signaled
	 * before releasing dma-buf. This is the responsibility of each
	 * driver that uses the reservation objects.
	 *
	 * If you hit this BUG() it means someone dropped their ref to the
	 * dma-buf while still having pending operation to the buffer.
	 */
	BUG_ON(dmabuf->cb_shared.active || dmabuf->cb_excl.active);

	dmabuf->ops->release(dmabuf);

	mutex_lock(&db_list.lock);
	list_del(&dmabuf->list_node);
	mutex_unlock(&db_list.lock);

	if (dmabuf->resv == (struct reservation_object *)&dmabuf[1])
		reservation_object_fini(dmabuf->resv);

	module_put(dmabuf->owner);
	kfree(dmabuf);
	return 0;
}

static int dma_buf_mmap_internal(struct file *file, struct vm_area_struct *vma)
{
	struct dma_buf *dmabuf;

	if (!is_dma_buf_file(file))
		return -EINVAL;

	dmabuf = file->private_data;

	/* check for overflowing the buffer's size */
	if (vma->vm_pgoff + vma_pages(vma) >
	    dmabuf->size >> PAGE_SHIFT)
		return -EINVAL;

	return dmabuf->ops->mmap(dmabuf, vma);
}

static loff_t dma_buf_llseek(struct file *file, loff_t offset, int whence)
{
	struct dma_buf *dmabuf;
	loff_t base;

	if (!is_dma_buf_file(file))
		return -EBADF;

	dmabuf = file->private_data;

	/* only support discovering the end of the buffer,
	   but also allow SEEK_SET to maintain the idiomatic
	   SEEK_END(0), SEEK_CUR(0) pattern */
	if (whence == SEEK_END)
		base = dmabuf->size;
	else if (whence == SEEK_SET)
		base = 0;
	else
		return -EINVAL;

	if (offset != 0)
		return -EINVAL;

	return base + offset;
}

/**
 * DOC: fence polling
 *
 * To support cross-device and cross-driver synchronization of buffer access
 * implicit fences (represented internally in the kernel with &struct fence) can
 * be attached to a &dma_buf. The glue for that and a few related things are
 * provided in the &reservation_object structure.
 *
 * Userspace can query the state of these implicitly tracked fences using poll()
 * and related system calls:
 *
 * - Checking for EPOLLIN, i.e. read access, can be use to query the state of the
 *   most recent write or exclusive fence.
 *
 * - Checking for EPOLLOUT, i.e. write access, can be used to query the state of
 *   all attached fences, shared and exclusive ones.
 *
 * Note that this only signals the completion of the respective fences, i.e. the
 * DMA transfers are complete. Cache flushing and any other necessary
 * preparations before CPU access can begin still need to happen.
 */

static void dma_buf_poll_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct dma_buf_poll_cb_t *dcb = (struct dma_buf_poll_cb_t *)cb;
	unsigned long flags;

	spin_lock_irqsave(&dcb->poll->lock, flags);
	wake_up_locked_poll(dcb->poll, dcb->active);
	dcb->active = 0;
	spin_unlock_irqrestore(&dcb->poll->lock, flags);
}

static __poll_t dma_buf_poll(struct file *file, poll_table *poll)
{
	struct dma_buf *dmabuf;
	struct reservation_object *resv;
	struct reservation_object_list *fobj;
	struct dma_fence *fence_excl;
	__poll_t events;
	unsigned shared_count, seq;

	dmabuf = file->private_data;
	if (!dmabuf || !dmabuf->resv)
		return EPOLLERR;

	resv = dmabuf->resv;

	poll_wait(file, &dmabuf->poll, poll);

	events = poll_requested_events(poll) & (EPOLLIN | EPOLLOUT);
	if (!events)
		return 0;

retry:
	seq = read_seqcount_begin(&resv->seq);
	rcu_read_lock();

	fobj = rcu_dereference(resv->fence);
	if (fobj)
		shared_count = fobj->shared_count;
	else
		shared_count = 0;
	fence_excl = rcu_dereference(resv->fence_excl);
	if (read_seqcount_retry(&resv->seq, seq)) {
		rcu_read_unlock();
		goto retry;
	}

	if (fence_excl && (!(events & EPOLLOUT) || shared_count == 0)) {
		struct dma_buf_poll_cb_t *dcb = &dmabuf->cb_excl;
		__poll_t pevents = EPOLLIN;

		if (shared_count == 0)
			pevents |= EPOLLOUT;

		spin_lock_irq(&dmabuf->poll.lock);
		if (dcb->active) {
			dcb->active |= pevents;
			events &= ~pevents;
		} else
			dcb->active = pevents;
		spin_unlock_irq(&dmabuf->poll.lock);

		if (events & pevents) {
			if (!dma_fence_get_rcu(fence_excl)) {
				/* force a recheck */
				events &= ~pevents;
				dma_buf_poll_cb(NULL, &dcb->cb);
			} else if (!dma_fence_add_callback(fence_excl, &dcb->cb,
							   dma_buf_poll_cb)) {
				events &= ~pevents;
				dma_fence_put(fence_excl);
			} else {
				/*
				 * No callback queued, wake up any additional
				 * waiters.
				 */
				dma_fence_put(fence_excl);
				dma_buf_poll_cb(NULL, &dcb->cb);
			}
		}
	}

	if ((events & EPOLLOUT) && shared_count > 0) {
		struct dma_buf_poll_cb_t *dcb = &dmabuf->cb_shared;
		int i;

		/* Only queue a new callback if no event has fired yet */
		spin_lock_irq(&dmabuf->poll.lock);
		if (dcb->active)
			events &= ~EPOLLOUT;
		else
			dcb->active = EPOLLOUT;
		spin_unlock_irq(&dmabuf->poll.lock);

		if (!(events & EPOLLOUT))
			goto out;

		for (i = 0; i < shared_count; ++i) {
			struct dma_fence *fence = rcu_dereference(fobj->shared[i]);

			if (!dma_fence_get_rcu(fence)) {
				/*
				 * fence refcount dropped to zero, this means
				 * that fobj has been freed
				 *
				 * call dma_buf_poll_cb and force a recheck!
				 */
				events &= ~EPOLLOUT;
				dma_buf_poll_cb(NULL, &dcb->cb);
				break;
			}
			if (!dma_fence_add_callback(fence, &dcb->cb,
						    dma_buf_poll_cb)) {
				dma_fence_put(fence);
				events &= ~EPOLLOUT;
				break;
			}
			dma_fence_put(fence);
		}

		/* No callback queued, wake up any additional waiters. */
		if (i == shared_count)
			dma_buf_poll_cb(NULL, &dcb->cb);
	}

out:
	rcu_read_unlock();
	return events;
}

static long dma_buf_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct dma_buf *dmabuf;
	struct dma_buf_sync sync;
	enum dma_data_direction direction;
	int ret;

	dmabuf = file->private_data;

	switch (cmd) {
	case DMA_BUF_IOCTL_SYNC:
		if (copy_from_user(&sync, (void __user *) arg, sizeof(sync)))
			return -EFAULT;

		if (sync.flags & ~DMA_BUF_SYNC_VALID_FLAGS_MASK)
			return -EINVAL;

		switch (sync.flags & DMA_BUF_SYNC_RW) {
		case DMA_BUF_SYNC_READ:
			direction = DMA_FROM_DEVICE;
			break;
		case DMA_BUF_SYNC_WRITE:
			direction = DMA_TO_DEVICE;
			break;
		case DMA_BUF_SYNC_RW:
			direction = DMA_BIDIRECTIONAL;
			break;
		default:
			return -EINVAL;
		}

		if (sync.flags & DMA_BUF_SYNC_END)
			ret = dma_buf_end_cpu_access(dmabuf, direction);
		else
			ret = dma_buf_begin_cpu_access(dmabuf, direction);

		return ret;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations dma_buf_fops = {
	.release	= dma_buf_release,
	.mmap		= dma_buf_mmap_internal,
	.llseek		= dma_buf_llseek,
	.poll		= dma_buf_poll,
	.unlocked_ioctl	= dma_buf_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= dma_buf_ioctl,
#endif
};

/*
 * is_dma_buf_file - Check if struct file* is associated with dma_buf
 */
static inline int is_dma_buf_file(struct file *file)
{
	return file->f_op == &dma_buf_fops;
}

/**
 * DOC: dma buf device access
 *
 * For device DMA access to a shared DMA buffer the usual sequence of operations
 * is fairly simple:
 *
 * 1. The exporter defines his exporter instance using
 *    DEFINE_DMA_BUF_EXPORT_INFO() and calls dma_buf_export() to wrap a private
 *    buffer object into a &dma_buf. It then exports that &dma_buf to userspace
 *    as a file descriptor by calling dma_buf_fd().
 *
 * 2. Userspace passes this file-descriptors to all drivers it wants this buffer
 *    to share with: First the filedescriptor is converted to a &dma_buf using
 *    dma_buf_get(). Then the buffer is attached to the device using
 *    dma_buf_attach().
 *
 *    Up to this stage the exporter is still free to migrate or reallocate the
 *    backing storage.
 *
 * 3. Once the buffer is attached to all devices userspace can initiate DMA
 *    access to the shared buffer. In the kernel this is done by calling
 *    dma_buf_map_attachment() and dma_buf_unmap_attachment().
 *
 * 4. Once a driver is done with a shared buffer it needs to call
 *    dma_buf_detach() (after cleaning up any mappings) and then release the
 *    reference acquired with dma_buf_get by calling dma_buf_put().
 *
 * For the detailed semantics exporters are expected to implement see
 * &dma_buf_ops.
 */

/**
 * dma_buf_export - Creates a new dma_buf, and associates an anon file
 * with this buffer, so it can be exported.
 * Also connect the allocator specific data and ops to the buffer.
 * Additionally, provide a name string for exporter; useful in debugging.
 *
 * @exp_info:	[in]	holds all the export related information provided
 *			by the exporter. see &struct dma_buf_export_info
 *			for further details.
 *
 * Returns, on success, a newly created dma_buf object, which wraps the
 * supplied private data and operations for dma_buf_ops. On either missing
 * ops, or error in allocating struct dma_buf, will return negative error.
 *
 * For most cases the easiest way to create @exp_info is through the
 * %DEFINE_DMA_BUF_EXPORT_INFO macro.
 */
struct dma_buf *dma_buf_export(const struct dma_buf_export_info *exp_info)
{
	struct dma_buf *dmabuf;
	struct reservation_object *resv = exp_info->resv;
	struct file *file;
	size_t alloc_size = sizeof(struct dma_buf);
	int ret;

	if (!exp_info->resv)
		alloc_size += sizeof(struct reservation_object);
	else
		/* prevent &dma_buf[1] == dma_buf->resv */
		alloc_size += 1;

	if (WARN_ON(!exp_info->priv
			  || !exp_info->ops
			  || !exp_info->ops->map_dma_buf
			  || !exp_info->ops->unmap_dma_buf
			  || !exp_info->ops->release
			  || !exp_info->ops->mmap)) {
		return ERR_PTR(-EINVAL);
	}

	if (!try_module_get(exp_info->owner))
		return ERR_PTR(-ENOENT);

	dmabuf = kzalloc(alloc_size, GFP_KERNEL);
	if (!dmabuf) {
		ret = -ENOMEM;
		goto err_module;
	}

	dmabuf->priv = exp_info->priv;
	dmabuf->ops = exp_info->ops;
	dmabuf->size = exp_info->size;
	dmabuf->exp_name = exp_info->exp_name;
	dmabuf->owner = exp_info->owner;
	init_waitqueue_head(&dmabuf->poll);
	dmabuf->cb_excl.poll = dmabuf->cb_shared.poll = &dmabuf->poll;
	dmabuf->cb_excl.active = dmabuf->cb_shared.active = 0;

	if (!resv) {
		resv = (struct reservation_object *)&dmabuf[1];
		reservation_object_init(resv);
	}
	dmabuf->resv = resv;

	file = anon_inode_getfile("dmabuf", &dma_buf_fops, dmabuf,
					exp_info->flags);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto err_dmabuf;
	}

	file->f_mode |= FMODE_LSEEK;
	dmabuf->file = file;

	mutex_init(&dmabuf->lock);
	INIT_LIST_HEAD(&dmabuf->attachments);

	mutex_lock(&db_list.lock);
	list_add(&dmabuf->list_node, &db_list.head);
	mutex_unlock(&db_list.lock);

	return dmabuf;

err_dmabuf:
	kfree(dmabuf);
err_module:
	module_put(exp_info->owner);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dma_buf_export);

/**
 * dma_buf_fd - returns a file descriptor for the given dma_buf
 * @dmabuf:	[in]	pointer to dma_buf for which fd is required.
 * @flags:      [in]    flags to give to fd
 *
 * On success, returns an associated 'fd'. Else, returns error.
 */
int dma_buf_fd(struct dma_buf *dmabuf, int flags)
{
	int fd;

	if (!dmabuf || !dmabuf->file)
		return -EINVAL;

	fd = get_unused_fd_flags(flags);
	if (fd < 0)
		return fd;

	fd_install(fd, dmabuf->file);

	return fd;
}
EXPORT_SYMBOL_GPL(dma_buf_fd);

/**
 * dma_buf_get - returns the dma_buf structure related to an fd
 * @fd:	[in]	fd associated with the dma_buf to be returned
 *
 * On success, returns the dma_buf structure associated with an fd; uses
 * file's refcounting done by fget to increase refcount. returns ERR_PTR
 * otherwise.
 */
struct dma_buf *dma_buf_get(int fd)
{
	struct file *file;

	file = fget(fd);

	if (!file)
		return ERR_PTR(-EBADF);

	if (!is_dma_buf_file(file)) {
		fput(file);
		return ERR_PTR(-EINVAL);
	}

	return file->private_data;
}
EXPORT_SYMBOL_GPL(dma_buf_get);

/**
 * dma_buf_put - decreases refcount of the buffer
 * @dmabuf:	[in]	buffer to reduce refcount of
 *
 * Uses file's refcounting done implicitly by fput().
 *
 * If, as a result of this call, the refcount becomes 0, the 'release' file
 * operation related to this fd is called. It calls &dma_buf_ops.release vfunc
 * in turn, and frees the memory allocated for dmabuf when exported.
 */
void dma_buf_put(struct dma_buf *dmabuf)
{
	if (WARN_ON(!dmabuf || !dmabuf->file))
		return;

	fput(dmabuf->file);
}
EXPORT_SYMBOL_GPL(dma_buf_put);

/**
 * dma_buf_attach - Add the device to dma_buf's attachments list; optionally,
 * calls attach() of dma_buf_ops to allow device-specific attach functionality
 * @dmabuf:	[in]	buffer to attach device to.
 * @dev:	[in]	device to be attached.
 *
 * Returns struct dma_buf_attachment pointer for this attachment. Attachments
 * must be cleaned up by calling dma_buf_detach().
 *
 * Returns:
 *
 * A pointer to newly created &dma_buf_attachment on success, or a negative
 * error code wrapped into a pointer on failure.
 *
 * Note that this can fail if the backing storage of @dmabuf is in a place not
 * accessible to @dev, and cannot be moved to a more suitable place. This is
 * indicated with the error code -EBUSY.
 */
struct dma_buf_attachment *dma_buf_attach(struct dma_buf *dmabuf,
					  struct device *dev)
{
	struct dma_buf_attachment *attach;
	int ret;

	if (WARN_ON(!dmabuf || !dev))
		return ERR_PTR(-EINVAL);

	attach = kzalloc(sizeof(*attach), GFP_KERNEL);
	if (!attach)
		return ERR_PTR(-ENOMEM);

	attach->dev = dev;
	attach->dmabuf = dmabuf;

	mutex_lock(&dmabuf->lock);

	if (dmabuf->ops->attach) {
		ret = dmabuf->ops->attach(dmabuf, attach);
		if (ret)
			goto err_attach;
	}
	list_add(&attach->node, &dmabuf->attachments);

	mutex_unlock(&dmabuf->lock);
	return attach;

err_attach:
	kfree(attach);
	mutex_unlock(&dmabuf->lock);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dma_buf_attach);

/**
 * dma_buf_detach - Remove the given attachment from dmabuf's attachments list;
 * optionally calls detach() of dma_buf_ops for device-specific detach
 * @dmabuf:	[in]	buffer to detach from.
 * @attach:	[in]	attachment to be detached; is free'd after this call.
 *
 * Clean up a device attachment obtained by calling dma_buf_attach().
 */
void dma_buf_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *attach)
{
	if (WARN_ON(!dmabuf || !attach))
		return;

	mutex_lock(&dmabuf->lock);
	list_del(&attach->node);
	if (dmabuf->ops->detach)
		dmabuf->ops->detach(dmabuf, attach);

	mutex_unlock(&dmabuf->lock);
	kfree(attach);
}
EXPORT_SYMBOL_GPL(dma_buf_detach);

/**
 * dma_buf_map_attachment - Returns the scatterlist table of the attachment;
 * mapped into _device_ address space. Is a wrapper for map_dma_buf() of the
 * dma_buf_ops.
 * @attach:	[in]	attachment whose scatterlist is to be returned
 * @direction:	[in]	direction of DMA transfer
 *
 * Returns sg_table containing the scatterlist to be returned; returns ERR_PTR
 * on error. May return -EINTR if it is interrupted by a signal.
 *
 * A mapping must be unmapped by using dma_buf_unmap_attachment(). Note that
 * the underlying backing storage is pinned for as long as a mapping exists,
 * therefore users/importers should not hold onto a mapping for undue amounts of
 * time.
 */
struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *attach,
					enum dma_data_direction direction)
{
	struct sg_table *sg_table;

	might_sleep();

	if (WARN_ON(!attach || !attach->dmabuf))
		return ERR_PTR(-EINVAL);

	sg_table = attach->dmabuf->ops->map_dma_buf(attach, direction);
	if (!sg_table)
		sg_table = ERR_PTR(-ENOMEM);

	return sg_table;
}
EXPORT_SYMBOL_GPL(dma_buf_map_attachment);

/**
 * dma_buf_unmap_attachment - unmaps and decreases usecount of the buffer;might
 * deallocate the scatterlist associated. Is a wrapper for unmap_dma_buf() of
 * dma_buf_ops.
 * @attach:	[in]	attachment to unmap buffer from
 * @sg_table:	[in]	scatterlist info of the buffer to unmap
 * @direction:  [in]    direction of DMA transfer
 *
 * This unmaps a DMA mapping for @attached obtained by dma_buf_map_attachment().
 */
void dma_buf_unmap_attachment(struct dma_buf_attachment *attach,
				struct sg_table *sg_table,
				enum dma_data_direction direction)
{
	might_sleep();

	if (WARN_ON(!attach || !attach->dmabuf || !sg_table))
		return;

	attach->dmabuf->ops->unmap_dma_buf(attach, sg_table,
						direction);
}
EXPORT_SYMBOL_GPL(dma_buf_unmap_attachment);

/**
 * DOC: cpu access
 *
 * There are mutliple reasons for supporting CPU access to a dma buffer object:
 *
 * - Fallback operations in the kernel, for example when a device is connected
 *   over USB and the kernel needs to shuffle the data around first before
 *   sending it away. Cache coherency is handled by braketing any transactions
 *   with calls to dma_buf_begin_cpu_access() and dma_buf_end_cpu_access()
 *   access.
 *
 *   To support dma_buf objects residing in highmem cpu access is page-based
 *   using an api similar to kmap. Accessing a dma_buf is done in aligned chunks
 *   of PAGE_SIZE size. Before accessing a chunk it needs to be mapped, which
 *   returns a pointer in kernel virtual address space. Afterwards the chunk
 *   needs to be unmapped again. There is no limit on how often a given chunk
 *   can be mapped and unmapped, i.e. the importer does not need to call
 *   begin_cpu_access again before mapping the same chunk again.
 *
 *   Interfaces::
 *      void \*dma_buf_kmap(struct dma_buf \*, unsigned long);
 *      void dma_buf_kunmap(struct dma_buf \*, unsigned long, void \*);
 *
 *   Implementing the functions is optional for exporters and for importers all
 *   the restrictions of using kmap apply.
 *
 *   dma_buf kmap calls outside of the range specified in begin_cpu_access are
 *   undefined. If the range is not PAGE_SIZE aligned, kmap needs to succeed on
 *   the partial chunks at the beginning and end but may return stale or bogus
 *   data outside of the range (in these partial chunks).
 *
 *   For some cases the overhead of kmap can be too high, a vmap interface
 *   is introduced. This interface should be used very carefully, as vmalloc
 *   space is a limited resources on many architectures.
 *
 *   Interfaces::
 *      void \*dma_buf_vmap(struct dma_buf \*dmabuf)
 *      void dma_buf_vunmap(struct dma_buf \*dmabuf, void \*vaddr)
 *
 *   The vmap call can fail if there is no vmap support in the exporter, or if
 *   it runs out of vmalloc space. Fallback to kmap should be implemented. Note
 *   that the dma-buf layer keeps a reference count for all vmap access and
 *   calls down into the exporter's vmap function only when no vmapping exists,
 *   and only unmaps it once. Protection against concurrent vmap/vunmap calls is
 *   provided by taking the dma_buf->lock mutex.
 *
 * - For full compatibility on the importer side with existing userspace
 *   interfaces, which might already support mmap'ing buffers. This is needed in
 *   many processing pipelines (e.g. feeding a software rendered image into a
 *   hardware pipeline, thumbnail creation, snapshots, ...). Also, Android's ION
 *   framework already supported this and for DMA buffer file descriptors to
 *   replace ION buffers mmap support was needed.
 *
 *   There is no special interfaces, userspace simply calls mmap on the dma-buf
 *   fd. But like for CPU access there's a need to braket the actual access,
 *   which is handled by the ioctl (DMA_BUF_IOCTL_SYNC). Note that
 *   DMA_BUF_IOCTL_SYNC can fail with -EAGAIN or -EINTR, in which case it must
 *   be restarted.
 *
 *   Some systems might need some sort of cache coherency management e.g. when
 *   CPU and GPU domains are being accessed through dma-buf at the same time.
 *   To circumvent this problem there are begin/end coherency markers, that
 *   forward directly to existing dma-buf device drivers vfunc hooks. Userspace
 *   can make use of those markers through the DMA_BUF_IOCTL_SYNC ioctl. The
 *   sequence would be used like following:
 *
 *     - mmap dma-buf fd
 *     - for each drawing/upload cycle in CPU 1. SYNC_START ioctl, 2. read/write
 *       to mmap area 3. SYNC_END ioctl. This can be repeated as often as you
 *       want (with the new data being consumed by say the GPU or the scanout
 *       device)
 *     - munmap once you don't need the buffer any more
 *
 *    For correctness and optimal performance, it is always required to use
 *    SYNC_START and SYNC_END before and after, respectively, when accessing the
 *    mapped address. Userspace cannot rely on coherent access, even when there
 *    are systems where it just works without calling these ioctls.
 *
 * - And as a CPU fallback in userspace processing pipelines.
 *
 *   Similar to the motivation for kernel cpu access it is again important that
 *   the userspace code of a given importing subsystem can use the same
 *   interfaces with a imported dma-buf buffer object as with a native buffer
 *   object. This is especially important for drm where the userspace part of
 *   contemporary OpenGL, X, and other drivers is huge, and reworking them to
 *   use a different way to mmap a buffer rather invasive.
 *
 *   The assumption in the current dma-buf interfaces is that redirecting the
 *   initial mmap is all that's needed. A survey of some of the existing
 *   subsystems shows that no driver seems to do any nefarious thing like
 *   syncing up with outstanding asynchronous processing on the device or
 *   allocating special resources at fault time. So hopefully this is good
 *   enough, since adding interfaces to intercept pagefaults and allow pte
 *   shootdowns would increase the complexity quite a bit.
 *
 *   Interface::
 *      int dma_buf_mmap(struct dma_buf \*, struct vm_area_struct \*,
 *		       unsigned long);
 *
 *   If the importing subsystem simply provides a special-purpose mmap call to
 *   set up a mapping in userspace, calling do_mmap with dma_buf->file will
 *   equally achieve that for a dma-buf object.
 */

static int __dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
				      enum dma_data_direction direction)
{
	bool write = (direction == DMA_BIDIRECTIONAL ||
		      direction == DMA_TO_DEVICE);
	struct reservation_object *resv = dmabuf->resv;
	long ret;

	/* Wait on any implicit rendering fences */
	ret = reservation_object_wait_timeout_rcu(resv, write, true,
						  MAX_SCHEDULE_TIMEOUT);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * dma_buf_begin_cpu_access - Must be called before accessing a dma_buf from the
 * cpu in the kernel context. Calls begin_cpu_access to allow exporter-specific
 * preparations. Coherency is only guaranteed in the specified range for the
 * specified access direction.
 * @dmabuf:	[in]	buffer to prepare cpu access for.
 * @direction:	[in]	length of range for cpu access.
 *
 * After the cpu access is complete the caller should call
 * dma_buf_end_cpu_access(). Only when cpu access is braketed by both calls is
 * it guaranteed to be coherent with other DMA access.
 *
 * Can return negative error values, returns 0 on success.
 */
int dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
			     enum dma_data_direction direction)
{
	int ret = 0;

	if (WARN_ON(!dmabuf))
		return -EINVAL;

	if (dmabuf->ops->begin_cpu_access)
		ret = dmabuf->ops->begin_cpu_access(dmabuf, direction);

	/* Ensure that all fences are waited upon - but we first allow
	 * the native handler the chance to do so more efficiently if it
	 * chooses. A double invocation here will be reasonably cheap no-op.
	 */
	if (ret == 0)
		ret = __dma_buf_begin_cpu_access(dmabuf, direction);

	return ret;
}
EXPORT_SYMBOL_GPL(dma_buf_begin_cpu_access);

/**
 * dma_buf_end_cpu_access - Must be called after accessing a dma_buf from the
 * cpu in the kernel context. Calls end_cpu_access to allow exporter-specific
 * actions. Coherency is only guaranteed in the specified range for the
 * specified access direction.
 * @dmabuf:	[in]	buffer to complete cpu access for.
 * @direction:	[in]	length of range for cpu access.
 *
 * This terminates CPU access started with dma_buf_begin_cpu_access().
 *
 * Can return negative error values, returns 0 on success.
 */
int dma_buf_end_cpu_access(struct dma_buf *dmabuf,
			   enum dma_data_direction direction)
{
	int ret = 0;

	WARN_ON(!dmabuf);

	if (dmabuf->ops->end_cpu_access)
		ret = dmabuf->ops->end_cpu_access(dmabuf, direction);

	return ret;
}
EXPORT_SYMBOL_GPL(dma_buf_end_cpu_access);

/**
 * dma_buf_kmap - Map a page of the buffer object into kernel address space. The
 * same restrictions as for kmap and friends apply.
 * @dmabuf:	[in]	buffer to map page from.
 * @page_num:	[in]	page in PAGE_SIZE units to map.
 *
 * This call must always succeed, any necessary preparations that might fail
 * need to be done in begin_cpu_access.
 */
void *dma_buf_kmap(struct dma_buf *dmabuf, unsigned long page_num)
{
	WARN_ON(!dmabuf);

	if (!dmabuf->ops->map)
		return NULL;
	return dmabuf->ops->map(dmabuf, page_num);
}
EXPORT_SYMBOL_GPL(dma_buf_kmap);

/**
 * dma_buf_kunmap - Unmap a page obtained by dma_buf_kmap.
 * @dmabuf:	[in]	buffer to unmap page from.
 * @page_num:	[in]	page in PAGE_SIZE units to unmap.
 * @vaddr:	[in]	kernel space pointer obtained from dma_buf_kmap.
 *
 * This call must always succeed.
 */
void dma_buf_kunmap(struct dma_buf *dmabuf, unsigned long page_num,
		    void *vaddr)
{
	WARN_ON(!dmabuf);

	if (dmabuf->ops->unmap)
		dmabuf->ops->unmap(dmabuf, page_num, vaddr);
}
EXPORT_SYMBOL_GPL(dma_buf_kunmap);


/**
 * dma_buf_mmap - Setup up a userspace mmap with the given vma
 * @dmabuf:	[in]	buffer that should back the vma
 * @vma:	[in]	vma for the mmap
 * @pgoff:	[in]	offset in pages where this mmap should start within the
 *			dma-buf buffer.
 *
 * This function adjusts the passed in vma so that it points at the file of the
 * dma_buf operation. It also adjusts the starting pgoff and does bounds
 * checking on the size of the vma. Then it calls the exporters mmap function to
 * set up the mapping.
 *
 * Can return negative error values, returns 0 on success.
 */
int dma_buf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma,
		 unsigned long pgoff)
{
	struct file *oldfile;
	int ret;

	if (WARN_ON(!dmabuf || !vma))
		return -EINVAL;

	/* check for offset overflow */
	if (pgoff + vma_pages(vma) < pgoff)
		return -EOVERFLOW;

	/* check for overflowing the buffer's size */
	if (pgoff + vma_pages(vma) >
	    dmabuf->size >> PAGE_SHIFT)
		return -EINVAL;

	/* readjust the vma */
	get_file(dmabuf->file);
	oldfile = vma->vm_file;
	vma->vm_file = dmabuf->file;
	vma->vm_pgoff = pgoff;

	ret = dmabuf->ops->mmap(dmabuf, vma);
	if (ret) {
		/* restore old parameters on failure */
		vma->vm_file = oldfile;
		fput(dmabuf->file);
	} else {
		if (oldfile)
			fput(oldfile);
	}
	return ret;

}
EXPORT_SYMBOL_GPL(dma_buf_mmap);

/**
 * dma_buf_vmap - Create virtual mapping for the buffer object into kernel
 * address space. Same restrictions as for vmap and friends apply.
 * @dmabuf:	[in]	buffer to vmap
 *
 * This call may fail due to lack of virtual mapping address space.
 * These calls are optional in drivers. The intended use for them
 * is for mapping objects linear in kernel space for high use objects.
 * Please attempt to use kmap/kunmap before thinking about these interfaces.
 *
 * Returns NULL on error.
 */
void *dma_buf_vmap(struct dma_buf *dmabuf)
{
	void *ptr;

	if (WARN_ON(!dmabuf))
		return NULL;

	if (!dmabuf->ops->vmap)
		return NULL;

	mutex_lock(&dmabuf->lock);
	if (dmabuf->vmapping_counter) {
		dmabuf->vmapping_counter++;
		BUG_ON(!dmabuf->vmap_ptr);
		ptr = dmabuf->vmap_ptr;
		goto out_unlock;
	}

	BUG_ON(dmabuf->vmap_ptr);

	ptr = dmabuf->ops->vmap(dmabuf);
	if (WARN_ON_ONCE(IS_ERR(ptr)))
		ptr = NULL;
	if (!ptr)
		goto out_unlock;

	dmabuf->vmap_ptr = ptr;
	dmabuf->vmapping_counter = 1;

out_unlock:
	mutex_unlock(&dmabuf->lock);
	return ptr;
}
EXPORT_SYMBOL_GPL(dma_buf_vmap);

/**
 * dma_buf_vunmap - Unmap a vmap obtained by dma_buf_vmap.
 * @dmabuf:	[in]	buffer to vunmap
 * @vaddr:	[in]	vmap to vunmap
 */
void dma_buf_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	if (WARN_ON(!dmabuf))
		return;

	BUG_ON(!dmabuf->vmap_ptr);
	BUG_ON(dmabuf->vmapping_counter == 0);
	BUG_ON(dmabuf->vmap_ptr != vaddr);

	mutex_lock(&dmabuf->lock);
	if (--dmabuf->vmapping_counter == 0) {
		if (dmabuf->ops->vunmap)
			dmabuf->ops->vunmap(dmabuf, vaddr);
		dmabuf->vmap_ptr = NULL;
	}
	mutex_unlock(&dmabuf->lock);
}
EXPORT_SYMBOL_GPL(dma_buf_vunmap);

#ifdef CONFIG_DEBUG_FS
static int dma_buf_debug_show(struct seq_file *s, void *unused)
{
	int ret;
	struct dma_buf *buf_obj;
	struct dma_buf_attachment *attach_obj;
	struct reservation_object *robj;
	struct reservation_object_list *fobj;
	struct dma_fence *fence;
	unsigned seq;
	int count = 0, attach_count, shared_count, i;
	size_t size = 0;

	ret = mutex_lock_interruptible(&db_list.lock);

	if (ret)
		return ret;

	seq_puts(s, "\nDma-buf Objects:\n");
	seq_printf(s, "%-8s\t%-8s\t%-8s\t%-8s\texp_name\n",
		   "size", "flags", "mode", "count");

	list_for_each_entry(buf_obj, &db_list.head, list_node) {
		ret = mutex_lock_interruptible(&buf_obj->lock);

		if (ret) {
			seq_puts(s,
				 "\tERROR locking buffer object: skipping\n");
			continue;
		}

		seq_printf(s, "%08zu\t%08x\t%08x\t%08ld\t%s\n",
				buf_obj->size,
				buf_obj->file->f_flags, buf_obj->file->f_mode,
				file_count(buf_obj->file),
				buf_obj->exp_name);

		robj = buf_obj->resv;
		while (true) {
			seq = read_seqcount_begin(&robj->seq);
			rcu_read_lock();
			fobj = rcu_dereference(robj->fence);
			shared_count = fobj ? fobj->shared_count : 0;
			fence = rcu_dereference(robj->fence_excl);
			if (!read_seqcount_retry(&robj->seq, seq))
				break;
			rcu_read_unlock();
		}

		if (fence)
			seq_printf(s, "\tExclusive fence: %s %s %ssignalled\n",
				   fence->ops->get_driver_name(fence),
				   fence->ops->get_timeline_name(fence),
				   dma_fence_is_signaled(fence) ? "" : "un");
		for (i = 0; i < shared_count; i++) {
			fence = rcu_dereference(fobj->shared[i]);
			if (!dma_fence_get_rcu(fence))
				continue;
			seq_printf(s, "\tShared fence: %s %s %ssignalled\n",
				   fence->ops->get_driver_name(fence),
				   fence->ops->get_timeline_name(fence),
				   dma_fence_is_signaled(fence) ? "" : "un");
		}
		rcu_read_unlock();

		seq_puts(s, "\tAttached Devices:\n");
		attach_count = 0;

		list_for_each_entry(attach_obj, &buf_obj->attachments, node) {
			seq_printf(s, "\t%s\n", dev_name(attach_obj->dev));
			attach_count++;
		}

		seq_printf(s, "Total %d devices attached\n\n",
				attach_count);

		count++;
		size += buf_obj->size;
		mutex_unlock(&buf_obj->lock);
	}

	seq_printf(s, "\nTotal %d objects, %zu bytes\n", count, size);

	mutex_unlock(&db_list.lock);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dma_buf_debug);

static struct dentry *dma_buf_debugfs_dir;

static int dma_buf_init_debugfs(void)
{
	struct dentry *d;
	int err = 0;

	d = debugfs_create_dir("dma_buf", NULL);
	if (IS_ERR(d))
		return PTR_ERR(d);

	dma_buf_debugfs_dir = d;

	d = debugfs_create_file("bufinfo", S_IRUGO, dma_buf_debugfs_dir,
				NULL, &dma_buf_debug_fops);
	if (IS_ERR(d)) {
		pr_debug("dma_buf: debugfs: failed to create node bufinfo\n");
		debugfs_remove_recursive(dma_buf_debugfs_dir);
		dma_buf_debugfs_dir = NULL;
		err = PTR_ERR(d);
	}

	return err;
}

static void dma_buf_uninit_debugfs(void)
{
	debugfs_remove_recursive(dma_buf_debugfs_dir);
}
#else
static inline int dma_buf_init_debugfs(void)
{
	return 0;
}
static inline void dma_buf_uninit_debugfs(void)
{
}
#endif

static int __init dma_buf_init(void)
{
	mutex_init(&db_list.lock);
	INIT_LIST_HEAD(&db_list.head);
	dma_buf_init_debugfs();
	return 0;
}
subsys_initcall(dma_buf_init);

static void __exit dma_buf_deinit(void)
{
	dma_buf_uninit_debugfs();
}
__exitcall(dma_buf_deinit);
