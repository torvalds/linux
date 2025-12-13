// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

/**
 * hl_mmap_mem_buf_get - increase the buffer refcount and return a pointer to
 *                        the buffer descriptor.
 *
 * @mmg: parent unified memory manager
 * @handle: requested buffer handle
 *
 * Find the buffer in the store and return a pointer to its descriptor.
 * Increase buffer refcount. If not found - return NULL.
 */
struct hl_mmap_mem_buf *hl_mmap_mem_buf_get(struct hl_mem_mgr *mmg, u64 handle)
{
	struct hl_mmap_mem_buf *buf;

	spin_lock(&mmg->lock);
	buf = idr_find(&mmg->handles, lower_32_bits(handle >> PAGE_SHIFT));
	if (!buf) {
		spin_unlock(&mmg->lock);
		dev_dbg(mmg->dev, "Buff get failed, no match to handle %#llx\n", handle);
		return NULL;
	}
	kref_get(&buf->refcount);
	spin_unlock(&mmg->lock);
	return buf;
}

/**
 * hl_mmap_mem_buf_destroy - destroy the unused buffer
 *
 * @buf: memory manager buffer descriptor
 *
 * Internal function, used as a final step of buffer release. Shall be invoked
 * only when the buffer is no longer in use (removed from idr). Will call the
 * release callback (if applicable), and free the memory.
 */
static void hl_mmap_mem_buf_destroy(struct hl_mmap_mem_buf *buf)
{
	if (buf->behavior->release)
		buf->behavior->release(buf);

	kfree(buf);
}

/**
 * hl_mmap_mem_buf_release - release buffer
 *
 * @kref: kref that reached 0.
 *
 * Internal function, used as a kref release callback, when the last user of
 * the buffer is released. Shall be called from an interrupt context.
 */
static void hl_mmap_mem_buf_release(struct kref *kref)
{
	struct hl_mmap_mem_buf *buf =
		container_of(kref, struct hl_mmap_mem_buf, refcount);

	spin_lock(&buf->mmg->lock);
	idr_remove(&buf->mmg->handles, lower_32_bits(buf->handle >> PAGE_SHIFT));
	spin_unlock(&buf->mmg->lock);

	hl_mmap_mem_buf_destroy(buf);
}

/**
 * hl_mmap_mem_buf_remove_idr_locked - remove handle from idr
 *
 * @kref: kref that reached 0.
 *
 * Internal function, used for kref put by handle. Assumes mmg lock is taken.
 * Will remove the buffer from idr, without destroying it.
 */
static void hl_mmap_mem_buf_remove_idr_locked(struct kref *kref)
{
	struct hl_mmap_mem_buf *buf =
		container_of(kref, struct hl_mmap_mem_buf, refcount);

	idr_remove(&buf->mmg->handles, lower_32_bits(buf->handle >> PAGE_SHIFT));
}

/**
 * hl_mmap_mem_buf_put - decrease the reference to the buffer
 *
 * @buf: memory manager buffer descriptor
 *
 * Decrease the reference to the buffer, and release it if it was the last one.
 * Shall be called from an interrupt context.
 */
int hl_mmap_mem_buf_put(struct hl_mmap_mem_buf *buf)
{
	return kref_put(&buf->refcount, hl_mmap_mem_buf_release);
}

/**
 * hl_mmap_mem_buf_put_handle - decrease the reference to the buffer with the
 *                              given handle.
 *
 * @mmg: parent unified memory manager
 * @handle: requested buffer handle
 *
 * Decrease the reference to the buffer, and release it if it was the last one.
 * Shall not be called from an interrupt context. Return -EINVAL if handle was
 * not found, else return the put outcome (0 or 1).
 */
int hl_mmap_mem_buf_put_handle(struct hl_mem_mgr *mmg, u64 handle)
{
	struct hl_mmap_mem_buf *buf;

	spin_lock(&mmg->lock);
	buf = idr_find(&mmg->handles, lower_32_bits(handle >> PAGE_SHIFT));
	if (!buf) {
		spin_unlock(&mmg->lock);
		dev_dbg(mmg->dev,
			 "Buff put failed, no match to handle %#llx\n", handle);
		return -EINVAL;
	}

	if (kref_put(&buf->refcount, hl_mmap_mem_buf_remove_idr_locked)) {
		spin_unlock(&mmg->lock);
		hl_mmap_mem_buf_destroy(buf);
		return 1;
	}

	spin_unlock(&mmg->lock);
	return 0;
}

/**
 * hl_mmap_mem_buf_alloc - allocate a new mappable buffer
 *
 * @mmg: parent unified memory manager
 * @behavior: behavior object describing this buffer polymorphic behavior
 * @gfp: gfp flags to use for the memory allocations
 * @args: additional args passed to behavior->alloc
 *
 * Allocate and register a new memory buffer inside the give memory manager.
 * Return the pointer to the new buffer on success or NULL on failure.
 */
struct hl_mmap_mem_buf *
hl_mmap_mem_buf_alloc(struct hl_mem_mgr *mmg,
		      struct hl_mmap_mem_buf_behavior *behavior, gfp_t gfp,
		      void *args)
{
	struct hl_mmap_mem_buf *buf;
	int rc;

	buf = kzalloc(sizeof(*buf), gfp);
	if (!buf)
		return NULL;

	spin_lock(&mmg->lock);
	rc = idr_alloc(&mmg->handles, buf, 1, 0, GFP_ATOMIC);
	spin_unlock(&mmg->lock);
	if (rc < 0) {
		dev_err(mmg->dev,
			"%s: Failed to allocate IDR for a new buffer, rc=%d\n",
			behavior->topic, rc);
		goto free_buf;
	}

	buf->mmg = mmg;
	buf->behavior = behavior;
	buf->handle = (((u64)rc | buf->behavior->mem_id) << PAGE_SHIFT);
	kref_init(&buf->refcount);

	rc = buf->behavior->alloc(buf, gfp, args);
	if (rc) {
		dev_err(mmg->dev, "%s: Failure in buffer alloc callback %d\n",
			behavior->topic, rc);
		goto remove_idr;
	}

	return buf;

remove_idr:
	spin_lock(&mmg->lock);
	idr_remove(&mmg->handles, lower_32_bits(buf->handle >> PAGE_SHIFT));
	spin_unlock(&mmg->lock);
free_buf:
	kfree(buf);
	return NULL;
}

/**
 * hl_mmap_mem_buf_vm_close - handle mmap close
 *
 * @vma: the vma object for which mmap was closed.
 *
 * Put the memory buffer if it is no longer mapped.
 */
static void hl_mmap_mem_buf_vm_close(struct vm_area_struct *vma)
{
	struct hl_mmap_mem_buf *buf =
		(struct hl_mmap_mem_buf *)vma->vm_private_data;
	long new_mmap_size;

	new_mmap_size = buf->real_mapped_size - (vma->vm_end - vma->vm_start);

	if (new_mmap_size > 0) {
		buf->real_mapped_size = new_mmap_size;
		return;
	}

	atomic_set(&buf->mmap, 0);
	hl_mmap_mem_buf_put(buf);
	vma->vm_private_data = NULL;
}

static const struct vm_operations_struct hl_mmap_mem_buf_vm_ops = {
	.close = hl_mmap_mem_buf_vm_close
};

/**
 * hl_mem_mgr_mmap - map the given buffer to the user
 *
 * @mmg: unified memory manager
 * @vma: the vma object for which mmap was closed.
 * @args: additional args passed to behavior->mmap
 *
 * Map the buffer specified by the vma->vm_pgoff to the given vma.
 */
int hl_mem_mgr_mmap(struct hl_mem_mgr *mmg, struct vm_area_struct *vma,
		    void *args)
{
	struct hl_mmap_mem_buf *buf;
	u64 user_mem_size;
	u64 handle;
	int rc;

	/* We use the page offset to hold the idr and thus we need to clear
	 * it before doing the mmap itself
	 */
	handle = vma->vm_pgoff << PAGE_SHIFT;
	vma->vm_pgoff = 0;

	/* Reference was taken here */
	buf = hl_mmap_mem_buf_get(mmg, handle);
	if (!buf) {
		dev_err(mmg->dev,
			"Memory mmap failed, no match to handle %#llx\n", handle);
		return -EINVAL;
	}

	/* Validation check */
	user_mem_size = vma->vm_end - vma->vm_start;
	if (user_mem_size != ALIGN(buf->mappable_size, PAGE_SIZE)) {
		dev_err(mmg->dev,
			"%s: Memory mmap failed, mmap VM size 0x%llx != 0x%llx allocated physical mem size\n",
			buf->behavior->topic, user_mem_size, buf->mappable_size);
		rc = -EINVAL;
		goto put_mem;
	}

	if (!access_ok((void __user *)(uintptr_t)vma->vm_start,
		       user_mem_size)) {
		dev_err(mmg->dev, "%s: User pointer is invalid - 0x%lx\n",
			buf->behavior->topic, vma->vm_start);

		rc = -EINVAL;
		goto put_mem;
	}

	if (atomic_cmpxchg(&buf->mmap, 0, 1)) {
		dev_err(mmg->dev,
			"%s, Memory mmap failed, already mapped to user\n",
			buf->behavior->topic);
		rc = -EINVAL;
		goto put_mem;
	}

	vma->vm_ops = &hl_mmap_mem_buf_vm_ops;

	/* Note: We're transferring the memory reference to vma->vm_private_data here. */

	vma->vm_private_data = buf;

	rc = buf->behavior->mmap(buf, vma, args);
	if (rc) {
		atomic_set(&buf->mmap, 0);
		goto put_mem;
	}

	buf->real_mapped_size = buf->mappable_size;
	vma->vm_pgoff = handle >> PAGE_SHIFT;

	return 0;

put_mem:
	hl_mmap_mem_buf_put(buf);
	return rc;
}

/**
 * hl_mem_mgr_init - initialize unified memory manager
 *
 * @dev: owner device pointer
 * @mmg: structure to initialize
 *
 * Initialize an instance of unified memory manager
 */
void hl_mem_mgr_init(struct device *dev, struct hl_mem_mgr *mmg)
{
	mmg->dev = dev;
	spin_lock_init(&mmg->lock);
	idr_init(&mmg->handles);
}

static void hl_mem_mgr_fini_stats_reset(struct hl_mem_mgr_fini_stats *stats)
{
	if (!stats)
		return;

	memset(stats, 0, sizeof(*stats));
}

static void hl_mem_mgr_fini_stats_inc(u64 mem_id, struct hl_mem_mgr_fini_stats *stats)
{
	if (!stats)
		return;

	switch (mem_id) {
	case HL_MMAP_TYPE_CB:
		++stats->n_busy_cb;
		break;
	case HL_MMAP_TYPE_TS_BUFF:
		++stats->n_busy_ts;
		break;
	default:
		/* we currently store only CB/TS so this shouldn't happen */
		++stats->n_busy_other;
	}
}

/**
 * hl_mem_mgr_fini - release unified memory manager
 *
 * @mmg: parent unified memory manager
 * @stats: if non-NULL, will return some counters for handles that could not be removed.
 *
 * Release the unified memory manager. Shall be called from an interrupt context.
 */
void hl_mem_mgr_fini(struct hl_mem_mgr *mmg, struct hl_mem_mgr_fini_stats *stats)
{
	struct hl_mmap_mem_buf *buf;
	struct idr *idp;
	const char *topic;
	u64 mem_id;
	u32 id;

	hl_mem_mgr_fini_stats_reset(stats);

	idp = &mmg->handles;

	idr_for_each_entry(idp, buf, id) {
		topic = buf->behavior->topic;
		mem_id = buf->behavior->mem_id;
		if (hl_mmap_mem_buf_put(buf) != 1) {
			dev_err(mmg->dev,
				"%s: Buff handle %u for CTX is still alive\n",
				topic, id);
			hl_mem_mgr_fini_stats_inc(mem_id, stats);
		}
	}
}

/**
 * hl_mem_mgr_idr_destroy() - destroy memory manager IDR.
 * @mmg: parent unified memory manager
 *
 * Destroy the memory manager IDR.
 * Shall be called when IDR is empty and no memory buffers are in use.
 */
void hl_mem_mgr_idr_destroy(struct hl_mem_mgr *mmg)
{
	if (!idr_is_empty(&mmg->handles))
		dev_crit(mmg->dev, "memory manager IDR is destroyed while it is not empty!\n");

	idr_destroy(&mmg->handles);
}
