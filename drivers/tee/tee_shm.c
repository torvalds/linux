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
#include <linux/uio.h>
#include "tee_private.h"

static void release_registered_pages(struct tee_shm *shm)
{
	if (shm->pages) {
		if (shm->flags & TEE_SHM_USER_MAPPED) {
			unpin_user_pages(shm->pages, shm->num_pages);
		} else {
			size_t n;

			for (n = 0; n < shm->num_pages; n++)
				put_page(shm->pages[n]);
		}

		kfree(shm->pages);
	}
}

static void tee_shm_release(struct tee_device *teedev, struct tee_shm *shm)
{
	if (shm->flags & TEE_SHM_POOL) {
		teedev->pool->ops->free(teedev->pool, shm);
	} else if (shm->flags & TEE_SHM_REGISTER) {
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

struct tee_shm *tee_shm_alloc(struct tee_context *ctx, size_t size, u32 flags)
{
	struct tee_device *teedev = ctx->teedev;
	struct tee_shm *shm;
	size_t align;
	void *ret;
	int rc;

	if (!(flags & TEE_SHM_MAPPED)) {
		dev_err(teedev->dev.parent,
			"only mapped allocations supported\n");
		return ERR_PTR(-EINVAL);
	}

	if ((flags & ~(TEE_SHM_MAPPED | TEE_SHM_DMA_BUF | TEE_SHM_PRIV))) {
		dev_err(teedev->dev.parent, "invalid shm flags 0x%x", flags);
		return ERR_PTR(-EINVAL);
	}

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
	shm->flags = flags | TEE_SHM_POOL;
	shm->ctx = ctx;
	if (flags & TEE_SHM_DMA_BUF) {
		align = PAGE_SIZE;
		/*
		 * Request to register the shm in the pool allocator below
		 * if supported.
		 */
		shm->flags |= TEE_SHM_REGISTER;
	} else {
		align = 2 * sizeof(long);
	}

	rc = teedev->pool->ops->alloc(teedev->pool, shm, size, align);
	if (rc) {
		ret = ERR_PTR(rc);
		goto err_kfree;
	}

	if (flags & TEE_SHM_DMA_BUF) {
		mutex_lock(&teedev->mutex);
		shm->id = idr_alloc(&teedev->idr, shm, 1, 0, GFP_KERNEL);
		mutex_unlock(&teedev->mutex);
		if (shm->id < 0) {
			ret = ERR_PTR(shm->id);
			goto err_pool_free;
		}
	}

	teedev_ctx_get(ctx);

	return shm;
err_pool_free:
	teedev->pool->ops->free(teedev->pool, shm);
err_kfree:
	kfree(shm);
err_dev_put:
	tee_device_put(teedev);
	return ret;
}
EXPORT_SYMBOL_GPL(tee_shm_alloc);

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
	return tee_shm_alloc(ctx, size, TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
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
	return tee_shm_alloc(ctx, size, TEE_SHM_MAPPED);
}
EXPORT_SYMBOL_GPL(tee_shm_alloc_kernel_buf);

struct tee_shm *tee_shm_register(struct tee_context *ctx, unsigned long addr,
				 size_t length, u32 flags)
{
	struct tee_device *teedev = ctx->teedev;
	const u32 req_user_flags = TEE_SHM_DMA_BUF | TEE_SHM_USER_MAPPED;
	const u32 req_kernel_flags = TEE_SHM_DMA_BUF | TEE_SHM_KERNEL_MAPPED;
	struct tee_shm *shm;
	void *ret;
	int rc;
	int num_pages;
	unsigned long start;

	if (flags != req_user_flags && flags != req_kernel_flags)
		return ERR_PTR(-ENOTSUPP);

	if (!tee_device_get(teedev))
		return ERR_PTR(-EINVAL);

	if (!teedev->desc->ops->shm_register ||
	    !teedev->desc->ops->shm_unregister) {
		tee_device_put(teedev);
		return ERR_PTR(-ENOTSUPP);
	}

	teedev_ctx_get(ctx);

	shm = kzalloc(sizeof(*shm), GFP_KERNEL);
	if (!shm) {
		ret = ERR_PTR(-ENOMEM);
		goto err;
	}

	refcount_set(&shm->refcount, 1);
	shm->flags = flags | TEE_SHM_REGISTER;
	shm->ctx = ctx;
	shm->id = -1;
	addr = untagged_addr(addr);
	start = rounddown(addr, PAGE_SIZE);
	shm->offset = addr - start;
	shm->size = length;
	num_pages = (roundup(addr + length, PAGE_SIZE) - start) / PAGE_SIZE;
	shm->pages = kcalloc(num_pages, sizeof(*shm->pages), GFP_KERNEL);
	if (!shm->pages) {
		ret = ERR_PTR(-ENOMEM);
		goto err;
	}

	if (flags & TEE_SHM_USER_MAPPED) {
		rc = pin_user_pages_fast(start, num_pages, FOLL_WRITE,
					 shm->pages);
	} else {
		struct kvec *kiov;
		int i;

		kiov = kcalloc(num_pages, sizeof(*kiov), GFP_KERNEL);
		if (!kiov) {
			ret = ERR_PTR(-ENOMEM);
			goto err;
		}

		for (i = 0; i < num_pages; i++) {
			kiov[i].iov_base = (void *)(start + i * PAGE_SIZE);
			kiov[i].iov_len = PAGE_SIZE;
		}

		rc = get_kernel_pages(kiov, num_pages, 0, shm->pages);
		kfree(kiov);
	}
	if (rc > 0)
		shm->num_pages = rc;
	if (rc != num_pages) {
		if (rc >= 0)
			rc = -ENOMEM;
		ret = ERR_PTR(rc);
		goto err;
	}

	mutex_lock(&teedev->mutex);
	shm->id = idr_alloc(&teedev->idr, shm, 1, 0, GFP_KERNEL);
	mutex_unlock(&teedev->mutex);

	if (shm->id < 0) {
		ret = ERR_PTR(shm->id);
		goto err;
	}

	rc = teedev->desc->ops->shm_register(ctx, shm, shm->pages,
					     shm->num_pages, start);
	if (rc) {
		ret = ERR_PTR(rc);
		goto err;
	}

	return shm;
err:
	if (shm) {
		if (shm->id >= 0) {
			mutex_lock(&teedev->mutex);
			idr_remove(&teedev->idr, shm->id);
			mutex_unlock(&teedev->mutex);
		}
		release_registered_pages(shm);
	}
	kfree(shm);
	teedev_ctx_put(ctx);
	tee_device_put(teedev);
	return ret;
}
EXPORT_SYMBOL_GPL(tee_shm_register);

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

	if (!(shm->flags & TEE_SHM_DMA_BUF))
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
 * tee_shm_va2pa() - Get physical address of a virtual address
 * @shm:	Shared memory handle
 * @va:		Virtual address to tranlsate
 * @pa:		Returned physical address
 * @returns 0 on success and < 0 on failure
 */
int tee_shm_va2pa(struct tee_shm *shm, void *va, phys_addr_t *pa)
{
	if (!(shm->flags & TEE_SHM_MAPPED))
		return -EINVAL;
	/* Check that we're in the range of the shm */
	if ((char *)va < (char *)shm->kaddr)
		return -EINVAL;
	if ((char *)va >= ((char *)shm->kaddr + shm->size))
		return -EINVAL;

	return tee_shm_get_pa(
			shm, (unsigned long)va - (unsigned long)shm->kaddr, pa);
}
EXPORT_SYMBOL_GPL(tee_shm_va2pa);

/**
 * tee_shm_pa2va() - Get virtual address of a physical address
 * @shm:	Shared memory handle
 * @pa:		Physical address to tranlsate
 * @va:		Returned virtual address
 * @returns 0 on success and < 0 on failure
 */
int tee_shm_pa2va(struct tee_shm *shm, phys_addr_t pa, void **va)
{
	if (!(shm->flags & TEE_SHM_MAPPED))
		return -EINVAL;
	/* Check that we're in the range of the shm */
	if (pa < shm->paddr)
		return -EINVAL;
	if (pa >= (shm->paddr + shm->size))
		return -EINVAL;

	if (va) {
		void *v = tee_shm_get_va(shm, pa - shm->paddr);

		if (IS_ERR(v))
			return PTR_ERR(v);
		*va = v;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(tee_shm_pa2va);

/**
 * tee_shm_get_va() - Get virtual address of a shared memory plus an offset
 * @shm:	Shared memory handle
 * @offs:	Offset from start of this shared memory
 * @returns virtual address of the shared memory + offs if offs is within
 *	the bounds of this shared memory, else an ERR_PTR
 */
void *tee_shm_get_va(struct tee_shm *shm, size_t offs)
{
	if (!(shm->flags & TEE_SHM_MAPPED))
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
		if (shm->flags & TEE_SHM_DMA_BUF)
			idr_remove(&teedev->idr, shm->id);
		do_release = true;
	}
	mutex_unlock(&teedev->mutex);

	if (do_release)
		tee_shm_release(teedev, shm);
}
EXPORT_SYMBOL_GPL(tee_shm_put);
