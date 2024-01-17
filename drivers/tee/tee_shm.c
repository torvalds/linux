// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, 2019-2021 Linaro Limited
 */
#include <linux/anon_inodes.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/highmem.h>
#include "tee_private.h"

static void shm_put_kernel_pages(struct page **pages, size_t page_count)
{
	size_t n;

	for (n = 0; n < page_count; n++)
		put_page(pages[n]);
}

static void shm_get_kernel_pages(struct page **pages, size_t page_count)
{
	size_t n;

	for (n = 0; n < page_count; n++)
		get_page(pages[n]);
}

static void release_registered_pages(struct tee_shm *shm)
{
	if (shm->pages) {
		if (shm->flags & TEE_SHM_USER_MAPPED)
			unpin_user_pages(shm->pages, shm->num_pages);
		else
			shm_put_kernel_pages(shm->pages, shm->num_pages);

		kfree(shm->pages);
	}
}

static void tee_shm_release(struct tee_device *teedev, struct tee_shm *shm)
{
	if (shm->flags & TEE_SHM_POOL) {
		teedev->pool->ops->free(teedev->pool, shm);
	} else if (shm->flags & TEE_SHM_DYNAMIC) {
		int rc = teedev->desc->ops->shm_unregister(shm->ctx, shm);

		if (rc)
			dev_err(teedev->dev.parent,
				"unregister shm %p failed: %d", shm, rc);

		release_registered_pages(shm);
	}

	teedev_ctx_put(shm->ctx);

	kfree(shm);

	tee_device_put(teedev);
}

static struct tee_shm *shm_alloc_helper(struct tee_context *ctx, size_t size,
					size_t align, u32 flags, int id)
{
	struct tee_device *teedev = ctx->teedev;
	struct tee_shm *shm;
	void *ret;
	int rc;

	if (!tee_device_get(teedev))
		return ERR_PTR(-EINVAL);

	if (!teedev->pool) {
		/* teedev has been detached from driver */
		ret = ERR_PTR(-EINVAL);
		goto err_dev_put;
	}

	shm = kzalloc(sizeof(*shm), GFP_KERNEL);
	if (!shm) {
		ret = ERR_PTR(-ENOMEM);
		goto err_dev_put;
	}

	refcount_set(&shm->refcount, 1);
	shm->flags = flags;
	shm->id = id;

	/*
	 * We're assigning this as it is needed if the shm is to be
	 * registered. If this function returns OK then the caller expected
	 * to call teedev_ctx_get() or clear shm->ctx in case it's not
	 * needed any longer.
	 */
	shm->ctx = ctx;

	rc = teedev->pool->ops->alloc(teedev->pool, shm, size, align);
	if (rc) {
		ret = ERR_PTR(rc);
		goto err_kfree;
	}

	teedev_ctx_get(ctx);
	return shm;
err_kfree:
	kfree(shm);
err_dev_put:
	tee_device_put(teedev);
	return ret;
}

/**
 * tee_shm_alloc_user_buf() - Allocate shared memory for user space
 * @ctx:	Context that allocates the shared memory
 * @size:	Requested size of shared memory
 *
 * Memory allocated as user space shared memory is automatically freed when
 * the TEE file pointer is closed. The primary usage of this function is
 * when the TEE driver doesn't support registering ordinary user space
 * memory.
 *
 * @returns a pointer to 'struct tee_shm'
 */
struct tee_shm *tee_shm_alloc_user_buf(struct tee_context *ctx, size_t size)
{
	u32 flags = TEE_SHM_DYNAMIC | TEE_SHM_POOL;
	struct tee_device *teedev = ctx->teedev;
	struct tee_shm *shm;
	void *ret;
	int id;

	mutex_lock(&teedev->mutex);
	id = idr_alloc(&teedev->idr, NULL, 1, 0, GFP_KERNEL);
	mutex_unlock(&teedev->mutex);
	if (id < 0)
		return ERR_PTR(id);

	shm = shm_alloc_helper(ctx, size, PAGE_SIZE, flags, id);
	if (IS_ERR(shm)) {
		mutex_lock(&teedev->mutex);
		idr_remove(&teedev->idr, id);
		mutex_unlock(&teedev->mutex);
		return shm;
	}

	mutex_lock(&teedev->mutex);
	ret = idr_replace(&teedev->idr, shm, id);
	mutex_unlock(&teedev->mutex);
	if (IS_ERR(ret)) {
		tee_shm_free(shm);
		return ret;
	}

	return shm;
}

/**
 * tee_shm_alloc_kernel_buf() - Allocate shared memory for kernel buffer
 * @ctx:	Context that allocates the shared memory
 * @size:	Requested size of shared memory
 *
 * The returned memory registered in secure world and is suitable to be
 * passed as a memory buffer in parameter argument to
 * tee_client_invoke_func(). The memory allocated is later freed with a
 * call to tee_shm_free().
 *
 * @returns a pointer to 'struct tee_shm'
 */
struct tee_shm *tee_shm_alloc_kernel_buf(struct tee_context *ctx, size_t size)
{
	u32 flags = TEE_SHM_DYNAMIC | TEE_SHM_POOL;

	return shm_alloc_helper(ctx, size, PAGE_SIZE, flags, -1);
}
EXPORT_SYMBOL_GPL(tee_shm_alloc_kernel_buf);

/**
 * tee_shm_alloc_priv_buf() - Allocate shared memory for a privately shared
 *			      kernel buffer
 * @ctx:	Context that allocates the shared memory
 * @size:	Requested size of shared memory
 *
 * This function returns similar shared memory as
 * tee_shm_alloc_kernel_buf(), but with the difference that the memory
 * might not be registered in secure world in case the driver supports
 * passing memory not registered in advance.
 *
 * This function should normally only be used internally in the TEE
 * drivers.
 *
 * @returns a pointer to 'struct tee_shm'
 */
struct tee_shm *tee_shm_alloc_priv_buf(struct tee_context *ctx, size_t size)
{
	u32 flags = TEE_SHM_PRIV | TEE_SHM_POOL;

	return shm_alloc_helper(ctx, size, sizeof(long) * 2, flags, -1);
}
EXPORT_SYMBOL_GPL(tee_shm_alloc_priv_buf);

static struct tee_shm *
register_shm_helper(struct tee_context *ctx, struct iov_iter *iter, u32 flags,
		    int id)
{
	struct tee_device *teedev = ctx->teedev;
	struct tee_shm *shm;
	unsigned long start, addr;
	size_t num_pages, off;
	ssize_t len;
	void *ret;
	int rc;

	if (!tee_device_get(teedev))
		return ERR_PTR(-EINVAL);

	if (!teedev->desc->ops->shm_register ||
	    !teedev->desc->ops->shm_unregister) {
		ret = ERR_PTR(-ENOTSUPP);
		goto err_dev_put;
	}

	teedev_ctx_get(ctx);

	shm = kzalloc(sizeof(*shm), GFP_KERNEL);
	if (!shm) {
		ret = ERR_PTR(-ENOMEM);
		goto err_ctx_put;
	}

	refcount_set(&shm->refcount, 1);
	shm->flags = flags;
	shm->ctx = ctx;
	shm->id = id;
	addr = untagged_addr((unsigned long)iter_iov_addr(iter));
	start = rounddown(addr, PAGE_SIZE);
	num_pages = iov_iter_npages(iter, INT_MAX);
	if (!num_pages) {
		ret = ERR_PTR(-ENOMEM);
		goto err_ctx_put;
	}

	shm->pages = kcalloc(num_pages, sizeof(*shm->pages), GFP_KERNEL);
	if (!shm->pages) {
		ret = ERR_PTR(-ENOMEM);
		goto err_free_shm;
	}

	len = iov_iter_extract_pages(iter, &shm->pages, LONG_MAX, num_pages, 0,
				     &off);
	if (unlikely(len <= 0)) {
		ret = len ? ERR_PTR(len) : ERR_PTR(-ENOMEM);
		goto err_free_shm_pages;
	}

	/*
	 * iov_iter_extract_kvec_pages does not get reference on the pages,
	 * get a reference on them.
	 */
	if (iov_iter_is_kvec(iter))
		shm_get_kernel_pages(shm->pages, num_pages);

	shm->offset = off;
	shm->size = len;
	shm->num_pages = num_pages;

	rc = teedev->desc->ops->shm_register(ctx, shm, shm->pages,
					     shm->num_pages, start);
	if (rc) {
		ret = ERR_PTR(rc);
		goto err_put_shm_pages;
	}

	return shm;
err_put_shm_pages:
	if (!iov_iter_is_kvec(iter))
		unpin_user_pages(shm->pages, shm->num_pages);
	else
		shm_put_kernel_pages(shm->pages, shm->num_pages);
err_free_shm_pages:
	kfree(shm->pages);
err_free_shm:
	kfree(shm);
err_ctx_put:
	teedev_ctx_put(ctx);
err_dev_put:
	tee_device_put(teedev);
	return ret;
}

/**
 * tee_shm_register_user_buf() - Register a userspace shared memory buffer
 * @ctx:	Context that registers the shared memory
 * @addr:	The userspace address of the shared buffer
 * @length:	Length of the shared buffer
 *
 * @returns a pointer to 'struct tee_shm'
 */
struct tee_shm *tee_shm_register_user_buf(struct tee_context *ctx,
					  unsigned long addr, size_t length)
{
	u32 flags = TEE_SHM_USER_MAPPED | TEE_SHM_DYNAMIC;
	struct tee_device *teedev = ctx->teedev;
	struct tee_shm *shm;
	struct iov_iter iter;
	void *ret;
	int id;

	if (!access_ok((void __user *)addr, length))
		return ERR_PTR(-EFAULT);

	mutex_lock(&teedev->mutex);
	id = idr_alloc(&teedev->idr, NULL, 1, 0, GFP_KERNEL);
	mutex_unlock(&teedev->mutex);
	if (id < 0)
		return ERR_PTR(id);

	iov_iter_ubuf(&iter, ITER_DEST,  (void __user *)addr, length);
	shm = register_shm_helper(ctx, &iter, flags, id);
	if (IS_ERR(shm)) {
		mutex_lock(&teedev->mutex);
		idr_remove(&teedev->idr, id);
		mutex_unlock(&teedev->mutex);
		return shm;
	}

	mutex_lock(&teedev->mutex);
	ret = idr_replace(&teedev->idr, shm, id);
	mutex_unlock(&teedev->mutex);
	if (IS_ERR(ret)) {
		tee_shm_free(shm);
		return ret;
	}

	return shm;
}

/**
 * tee_shm_register_kernel_buf() - Register kernel memory to be shared with
 *				   secure world
 * @ctx:	Context that registers the shared memory
 * @addr:	The buffer
 * @length:	Length of the buffer
 *
 * @returns a pointer to 'struct tee_shm'
 */

struct tee_shm *tee_shm_register_kernel_buf(struct tee_context *ctx,
					    void *addr, size_t length)
{
	u32 flags = TEE_SHM_DYNAMIC;
	struct kvec kvec;
	struct iov_iter iter;

	kvec.iov_base = addr;
	kvec.iov_len = length;
	iov_iter_kvec(&iter, ITER_DEST, &kvec, 1, length);

	return register_shm_helper(ctx, &iter, flags, -1);
}
EXPORT_SYMBOL_GPL(tee_shm_register_kernel_buf);

static int tee_shm_fop_release(struct inode *inode, struct file *filp)
{
	tee_shm_put(filp->private_data);
	return 0;
}

static int tee_shm_fop_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct tee_shm *shm = filp->private_data;
	size_t size = vma->vm_end - vma->vm_start;

	/* Refuse sharing shared memory provided by application */
	if (shm->flags & TEE_SHM_USER_MAPPED)
		return -EINVAL;

	/* check for overflowing the buffer's size */
	if (vma->vm_pgoff + vma_pages(vma) > shm->size >> PAGE_SHIFT)
		return -EINVAL;

	return remap_pfn_range(vma, vma->vm_start, shm->paddr >> PAGE_SHIFT,
			       size, vma->vm_page_prot);
}

static const struct file_operations tee_shm_fops = {
	.owner = THIS_MODULE,
	.release = tee_shm_fop_release,
	.mmap = tee_shm_fop_mmap,
};

/**
 * tee_shm_get_fd() - Increase reference count and return file descriptor
 * @shm:	Shared memory handle
 * @returns user space file descriptor to shared memory
 */
int tee_shm_get_fd(struct tee_shm *shm)
{
	int fd;

	if (shm->id < 0)
		return -EINVAL;

	/* matched by tee_shm_put() in tee_shm_op_release() */
	refcount_inc(&shm->refcount);
	fd = anon_inode_getfd("tee_shm", &tee_shm_fops, shm, O_RDWR);
	if (fd < 0)
		tee_shm_put(shm);
	return fd;
}

/**
 * tee_shm_free() - Free shared memory
 * @shm:	Handle to shared memory to free
 */
void tee_shm_free(struct tee_shm *shm)
{
	tee_shm_put(shm);
}
EXPORT_SYMBOL_GPL(tee_shm_free);

/**
 * tee_shm_get_va() - Get virtual address of a shared memory plus an offset
 * @shm:	Shared memory handle
 * @offs:	Offset from start of this shared memory
 * @returns virtual address of the shared memory + offs if offs is within
 *	the bounds of this shared memory, else an ERR_PTR
 */
void *tee_shm_get_va(struct tee_shm *shm, size_t offs)
{
	if (!shm->kaddr)
		return ERR_PTR(-EINVAL);
	if (offs >= shm->size)
		return ERR_PTR(-EINVAL);
	return (char *)shm->kaddr + offs;
}
EXPORT_SYMBOL_GPL(tee_shm_get_va);

/**
 * tee_shm_get_pa() - Get physical address of a shared memory plus an offset
 * @shm:	Shared memory handle
 * @offs:	Offset from start of this shared memory
 * @pa:		Physical address to return
 * @returns 0 if offs is within the bounds of this shared memory, else an
 *	error code.
 */
int tee_shm_get_pa(struct tee_shm *shm, size_t offs, phys_addr_t *pa)
{
	if (offs >= shm->size)
		return -EINVAL;
	if (pa)
		*pa = shm->paddr + offs;
	return 0;
}
EXPORT_SYMBOL_GPL(tee_shm_get_pa);

/**
 * tee_shm_get_from_id() - Find shared memory object and increase reference
 * count
 * @ctx:	Context owning the shared memory
 * @id:		Id of shared memory object
 * @returns a pointer to 'struct tee_shm' on success or an ERR_PTR on failure
 */
struct tee_shm *tee_shm_get_from_id(struct tee_context *ctx, int id)
{
	struct tee_device *teedev;
	struct tee_shm *shm;

	if (!ctx)
		return ERR_PTR(-EINVAL);

	teedev = ctx->teedev;
	mutex_lock(&teedev->mutex);
	shm = idr_find(&teedev->idr, id);
	/*
	 * If the tee_shm was found in the IDR it must have a refcount
	 * larger than 0 due to the guarantee in tee_shm_put() below. So
	 * it's safe to use refcount_inc().
	 */
	if (!shm || shm->ctx != ctx)
		shm = ERR_PTR(-EINVAL);
	else
		refcount_inc(&shm->refcount);
	mutex_unlock(&teedev->mutex);
	return shm;
}
EXPORT_SYMBOL_GPL(tee_shm_get_from_id);

/**
 * tee_shm_put() - Decrease reference count on a shared memory handle
 * @shm:	Shared memory handle
 */
void tee_shm_put(struct tee_shm *shm)
{
	struct tee_device *teedev = shm->ctx->teedev;
	bool do_release = false;

	mutex_lock(&teedev->mutex);
	if (refcount_dec_and_test(&shm->refcount)) {
		/*
		 * refcount has reached 0, we must now remove it from the
		 * IDR before releasing the mutex. This will guarantee that
		 * the refcount_inc() in tee_shm_get_from_id() never starts
		 * from 0.
		 */
		if (shm->id >= 0)
			idr_remove(&teedev->idr, shm->id);
		do_release = true;
	}
	mutex_unlock(&teedev->mutex);

	if (do_release)
		tee_shm_release(teedev, shm);
}
EXPORT_SYMBOL_GPL(tee_shm_put);
