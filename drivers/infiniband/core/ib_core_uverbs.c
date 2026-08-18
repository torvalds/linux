// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 * Copyright 2019 Marvell. All rights reserved.
 */
#include <linux/xarray.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include "uverbs.h"
#include "core_priv.h"
#include "rdma_core.h"

MODULE_IMPORT_NS("DMA_BUF");

/**
 * rdma_umap_priv_init() - Initialize the private data of a vma
 *
 * @priv: The already allocated private data
 * @vma: The vm area struct that needs private data
 * @entry: entry into the mmap_xa that needs to be linked with
 *       this vma
 *
 * Each time we map IO memory into user space this keeps track of the
 * mapping. When the device is hot-unplugged we 'zap' the mmaps in user space
 * to point to the zero page and allow the hot unplug to proceed.
 *
 * This is necessary for cases like PCI physical hot unplug as the actual BAR
 * memory may vanish after this and access to it from userspace could MCE.
 *
 * RDMA drivers supporting disassociation must have their user space designed
 * to cope in some way with their IO pages going to the zero page.
 *
 */
void rdma_umap_priv_init(struct rdma_umap_priv *priv,
			 struct vm_area_struct *vma,
			 struct rdma_user_mmap_entry *entry)
{
	struct ib_uverbs_file *ufile = vma->vm_file->private_data;

	priv->vma = vma;
	if (entry) {
		kref_get(&entry->ref);
		priv->entry = entry;
	}
	vma->vm_private_data = priv;
	/* vm_ops is setup in ib_uverbs_mmap() to avoid module dependencies */

	mutex_lock(&ufile->umap_lock);
	list_add(&priv->list, &ufile->umaps);
	mutex_unlock(&ufile->umap_lock);
}
EXPORT_SYMBOL(rdma_umap_priv_init);

/**
 * rdma_user_mmap_io() - Map IO memory into a process
 *
 * @ucontext: associated user context
 * @vma: the vma related to the current mmap call
 * @pfn: pfn to map
 * @size: size to map
 * @prot: pgprot to use in remap call
 * @entry: mmap_entry retrieved from rdma_user_mmap_entry_get(), or NULL
 *         if mmap_entry is not used by the driver
 *
 * This is to be called by drivers as part of their mmap() functions if they
 * wish to send something like PCI-E BAR memory to userspace.
 *
 * Return -EINVAL on wrong flags or size, -EAGAIN on failure to map. 0 on
 * success.
 */
int rdma_user_mmap_io(struct ib_ucontext *ucontext, struct vm_area_struct *vma,
		      unsigned long pfn, unsigned long size, pgprot_t prot,
		      struct rdma_user_mmap_entry *entry)
{
	struct ib_uverbs_file *ufile = ucontext->ufile;
	struct rdma_umap_priv *priv;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	if (vma->vm_end - vma->vm_start != size)
		return -EINVAL;

	/* Driver is using this wrong, must be called by ib_uverbs_mmap */
	if (WARN_ON(!vma->vm_file ||
		    vma->vm_file->private_data != ufile))
		return -EINVAL;
	lockdep_assert_held(&ufile->device->disassociate_srcu);

	priv = kzalloc_obj(*priv);
	if (!priv)
		return -ENOMEM;

	vma->vm_page_prot = prot;
	if (io_remap_pfn_range(vma, vma->vm_start, pfn, size, prot)) {
		kfree(priv);
		return -EAGAIN;
	}

	rdma_umap_priv_init(priv, vma, entry);
	return 0;
}
EXPORT_SYMBOL(rdma_user_mmap_io);

/**
 * rdma_user_mmap_entry_get_pgoff() - Get an entry from the mmap_xa
 *
 * @ucontext: associated user context
 * @pgoff: The mmap offset >> PAGE_SHIFT
 *
 * This function is called when a user tries to mmap with an offset (returned
 * by rdma_user_mmap_get_offset()) it initially received from the driver. The
 * rdma_user_mmap_entry was created by the function
 * rdma_user_mmap_entry_insert().  This function increases the refcnt of the
 * entry so that it won't be deleted from the xarray in the meantime.
 *
 * Return an reference to an entry if exists or NULL if there is no
 * match. rdma_user_mmap_entry_put() must be called to put the reference.
 */
struct rdma_user_mmap_entry *
rdma_user_mmap_entry_get_pgoff(struct ib_ucontext *ucontext,
			       unsigned long pgoff)
{
	struct rdma_user_mmap_entry *entry;

	if (pgoff > U32_MAX)
		return NULL;

	xa_lock(&ucontext->mmap_xa);

	entry = xa_load(&ucontext->mmap_xa, pgoff);

	/*
	 * If refcount is zero, entry is already being deleted, driver_removed
	 * indicates that the no further mmaps are possible and we waiting for
	 * the active VMAs to be closed.
	 */
	if (!entry || entry->start_pgoff != pgoff || entry->driver_removed ||
	    !kref_get_unless_zero(&entry->ref))
		goto err;

	xa_unlock(&ucontext->mmap_xa);

	ibdev_dbg(ucontext->device, "mmap: pgoff[%#lx] npages[%#zx] returned\n",
		  pgoff, entry->npages);

	return entry;

err:
	xa_unlock(&ucontext->mmap_xa);
	return NULL;
}
EXPORT_SYMBOL(rdma_user_mmap_entry_get_pgoff);

/**
 * rdma_user_mmap_entry_get() - Get an entry from the mmap_xa
 *
 * @ucontext: associated user context
 * @vma: the vma being mmap'd into
 *
 * This function is like rdma_user_mmap_entry_get_pgoff() except that it also
 * checks that the VMA is correct.
 */
struct rdma_user_mmap_entry *
rdma_user_mmap_entry_get(struct ib_ucontext *ucontext,
			 struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *entry;

	if (!(vma->vm_flags & VM_SHARED))
		return NULL;
	entry = rdma_user_mmap_entry_get_pgoff(ucontext, vma->vm_pgoff);
	if (!entry)
		return NULL;
	if (entry->npages * PAGE_SIZE != vma->vm_end - vma->vm_start) {
		rdma_user_mmap_entry_put(entry);
		return NULL;
	}
	return entry;
}
EXPORT_SYMBOL(rdma_user_mmap_entry_get);

static void rdma_user_mmap_entry_free(struct kref *kref)
{
	struct rdma_user_mmap_entry *entry =
		container_of(kref, struct rdma_user_mmap_entry, ref);
	struct ib_ucontext *ucontext = entry->ucontext;
	unsigned long i;

	/*
	 * Erase all entries occupied by this single entry, this is deferred
	 * until all VMA are closed so that the mmap offsets remain unique.
	 */
	xa_lock(&ucontext->mmap_xa);
	for (i = 0; i < entry->npages; i++)
		__xa_erase(&ucontext->mmap_xa, entry->start_pgoff + i);
	xa_unlock(&ucontext->mmap_xa);

	ibdev_dbg(ucontext->device, "mmap: pgoff[%#lx] npages[%#zx] removed\n",
		  entry->start_pgoff, entry->npages);

	if (ucontext->device->ops.mmap_free)
		ucontext->device->ops.mmap_free(entry);
}

/**
 * rdma_user_mmap_entry_put() - Drop reference to the mmap entry
 *
 * @entry: an entry in the mmap_xa
 *
 * This function is called when the mapping is closed if it was
 * an io mapping or when the driver is done with the entry for
 * some other reason.
 * Should be called after rdma_user_mmap_entry_get was called
 * and entry is no longer needed. This function will erase the
 * entry and free it if its refcnt reaches zero.
 */
void rdma_user_mmap_entry_put(struct rdma_user_mmap_entry *entry)
{
	kref_put(&entry->ref, rdma_user_mmap_entry_free);
}
EXPORT_SYMBOL(rdma_user_mmap_entry_put);

/**
 * rdma_user_mmap_entry_remove() - Drop reference to entry and
 *				   mark it as unmmapable
 *
 * @entry: the entry to insert into the mmap_xa
 *
 * Drivers can call this to prevent userspace from creating more mappings for
 * entry, however existing mmaps continue to exist and ops->mmap_free() will
 * not be called until all user mmaps are destroyed.
 */
void rdma_user_mmap_entry_remove(struct rdma_user_mmap_entry *entry)
{
	struct ib_uverbs_dmabuf_file *uverbs_dmabuf, *tmp;

	if (!entry)
		return;

	mutex_lock(&entry->dmabufs_lock);
	xa_lock(&entry->ucontext->mmap_xa);
	entry->driver_removed = true;
	xa_unlock(&entry->ucontext->mmap_xa);
	list_for_each_entry_safe(uverbs_dmabuf, tmp, &entry->dmabufs, dmabufs_elm) {
		dma_resv_lock(uverbs_dmabuf->dmabuf->resv, NULL);
		list_del(&uverbs_dmabuf->dmabufs_elm);
		uverbs_dmabuf->revoked = true;
		dma_buf_invalidate_mappings(uverbs_dmabuf->dmabuf);
		dma_resv_wait_timeout(uverbs_dmabuf->dmabuf->resv,
				      DMA_RESV_USAGE_BOOKKEEP, false,
				      MAX_SCHEDULE_TIMEOUT);
		dma_resv_unlock(uverbs_dmabuf->dmabuf->resv);
		kref_put(&uverbs_dmabuf->kref, ib_uverbs_dmabuf_done);
		wait_for_completion(&uverbs_dmabuf->comp);
	}
	mutex_unlock(&entry->dmabufs_lock);

	kref_put(&entry->ref, rdma_user_mmap_entry_free);
}
EXPORT_SYMBOL(rdma_user_mmap_entry_remove);

/**
 * rdma_user_mmap_entry_insert_range() - Insert an entry to the mmap_xa
 *					 in a given range.
 *
 * @ucontext: associated user context.
 * @entry: the entry to insert into the mmap_xa
 * @length: length of the address that will be mmapped
 * @min_pgoff: minimum pgoff to be returned
 * @max_pgoff: maximum pgoff to be returned
 *
 * This function should be called by drivers that use the rdma_user_mmap
 * interface for implementing their mmap syscall A database of mmap offsets is
 * handled in the core and helper functions are provided to insert entries
 * into the database and extract entries when the user calls mmap with the
 * given offset. The function allocates a unique page offset in a given range
 * that should be provided to user, the user will use the offset to retrieve
 * information such as address to be mapped and how.
 *
 * Return: 0 on success and -ENOMEM on failure
 */
int rdma_user_mmap_entry_insert_range(struct ib_ucontext *ucontext,
				      struct rdma_user_mmap_entry *entry,
				      size_t length, u32 min_pgoff,
				      u32 max_pgoff)
{
	struct ib_uverbs_file *ufile = ucontext->ufile;
	XA_STATE(xas, &ucontext->mmap_xa, min_pgoff);
	u32 xa_first, xa_last, npages;
	int err;
	u32 i;

	if (!entry)
		return -EINVAL;

	kref_init(&entry->ref);
	INIT_LIST_HEAD(&entry->dmabufs);
	mutex_init(&entry->dmabufs_lock);

	entry->ucontext = ucontext;

	/*
	 * We want the whole allocation to be done without interruption from a
	 * different thread. The allocation requires finding a free range and
	 * storing. During the xa_insert the lock could be released, possibly
	 * allowing another thread to choose the same range.
	 */
	mutex_lock(&ufile->umap_lock);

	xa_lock(&ucontext->mmap_xa);

	/* We want to find an empty range */
	npages = (u32)DIV_ROUND_UP(length, PAGE_SIZE);
	entry->npages = npages;
	while (true) {
		/* First find an empty index */
		xas_find_marked(&xas, max_pgoff, XA_FREE_MARK);
		if (xas.xa_node == XAS_RESTART)
			goto err_unlock;

		xa_first = xas.xa_index;

		/* Is there enough room to have the range? */
		if (check_add_overflow(xa_first, npages, &xa_last))
			goto err_unlock;

		/*
		 * Now look for the next present entry. If an entry doesn't
		 * exist, we found an empty range and can proceed.
		 */
		xas_next_entry(&xas, xa_last - 1);
		if (xas.xa_node == XAS_BOUNDS || xas.xa_index >= xa_last)
			break;
	}

	for (i = xa_first; i < xa_last; i++) {
		err = __xa_insert(&ucontext->mmap_xa, i, entry, GFP_KERNEL);
		if (err)
			goto err_undo;
	}

	/*
	 * Internally the kernel uses a page offset, in libc this is a byte
	 * offset. Drivers should not return pgoff to userspace.
	 */
	entry->start_pgoff = xa_first;
	xa_unlock(&ucontext->mmap_xa);
	mutex_unlock(&ufile->umap_lock);

	ibdev_dbg(ucontext->device, "mmap: pgoff[%#lx] npages[%#x] inserted\n",
		  entry->start_pgoff, npages);

	return 0;

err_undo:
	for (; i > xa_first; i--)
		__xa_erase(&ucontext->mmap_xa, i - 1);

err_unlock:
	xa_unlock(&ucontext->mmap_xa);
	mutex_unlock(&ufile->umap_lock);
	return -ENOMEM;
}
EXPORT_SYMBOL(rdma_user_mmap_entry_insert_range);

/**
 * rdma_user_mmap_entry_insert() - Insert an entry to the mmap_xa.
 *
 * @ucontext: associated user context.
 * @entry: the entry to insert into the mmap_xa
 * @length: length of the address that will be mmapped
 *
 * This function should be called by drivers that use the rdma_user_mmap
 * interface for handling user mmapped addresses. The database is handled in
 * the core and helper functions are provided to insert entries into the
 * database and extract entries when the user calls mmap with the given offset.
 * The function allocates a unique page offset that should be provided to user,
 * the user will use the offset to retrieve information such as address to
 * be mapped and how.
 *
 * Return: 0 on success and -ENOMEM on failure
 */
int rdma_user_mmap_entry_insert(struct ib_ucontext *ucontext,
				struct rdma_user_mmap_entry *entry,
				size_t length)
{
	return rdma_user_mmap_entry_insert_range(ucontext, entry, length, 0,
						 U32_MAX);
}
EXPORT_SYMBOL(rdma_user_mmap_entry_insert);

/**
 * rdma_udata_to_dev - Get a ib_device from a udata
 * @udata: The system calls ib_udata struct
 *
 * The struct ib_device that is handling the uverbs call. Must not be called if
 * udata is NULL. The result can be NULL.
 */
static struct ib_device *rdma_udata_to_dev(struct ib_udata *udata)
{
	struct uverbs_attr_bundle *bundle =
		rdma_udata_to_uverbs_attr_bundle(udata);

	lockdep_assert_held(&bundle->ufile->device->disassociate_srcu);

	if (bundle->context)
		return bundle->context->device;

	/*
	 * If the context hasn't been created yet use the ufile's dev, but it
	 * might be NULL if we are racing with disassociate.
	 */
	return srcu_dereference(bundle->ufile->device->ib_dev,
				&bundle->ufile->device->disassociate_srcu);
}

typedef int (*uverbs_api_ioctl_handler_fn)(struct uverbs_attr_bundle *attrs);
static uverbs_api_ioctl_handler_fn uverbs_get_handler_fn(struct ib_udata *udata)
{
	struct uverbs_attr_bundle *bundle =
		rdma_udata_to_uverbs_attr_bundle(udata);

	lockdep_assert_held(&bundle->ufile->device->disassociate_srcu);

	return srcu_dereference(bundle->method_elm->handler,
				&bundle->ufile->device->disassociate_srcu);
}

int _ib_copy_validate_udata_in(struct ib_udata *udata, void *req,
			       size_t kernel_size, size_t minimum_size)
{
	int err;

	if (udata->inlen < minimum_size) {
		ibdev_dbg(
			rdma_udata_to_dev(udata),
			"System call driver input udata too small (%zu < %zu) for ioctl %ps called by %pSR\n",
			udata->inlen, minimum_size,
			uverbs_get_handler_fn(udata),
			__builtin_return_address(0));
		return -EINVAL;
	}

	err = copy_struct_from_user(req, kernel_size, udata->inbuf,
				    udata->inlen);
	if (err) {
		if (err == -E2BIG) {
			ibdev_dbg(
				rdma_udata_to_dev(udata),
				"System call driver input udata not zero from %zu -> %zu for ioctl %ps called by %pSR\n",
				minimum_size, udata->inlen,
				uverbs_get_handler_fn(udata),
				__builtin_return_address(0));
			return -EOPNOTSUPP;
		}
		ibdev_dbg(
			rdma_udata_to_dev(udata),
			"System call driver input udata EFAULT for ioctl %ps called by %pSR\n",
			uverbs_get_handler_fn(udata),
			__builtin_return_address(0));
		return err;
	}
	return 0;
}
EXPORT_SYMBOL(_ib_copy_validate_udata_in);

int _ib_copy_validate_udata_cm_fail(struct ib_udata *udata, u64 req_cm,
				    u64 valid_cm)
{
	ibdev_dbg(
		rdma_udata_to_dev(udata),
		"System call driver input udata has unsupported comp_mask %llx & ~%llx = %llx for ioctl %ps called by %pSR\n",
		req_cm, valid_cm, req_cm & ~valid_cm,
		uverbs_get_handler_fn(udata), __builtin_return_address(0));
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(_ib_copy_validate_udata_cm_fail);

int _ib_respond_udata(struct ib_udata *udata, const void *src, size_t len)
{
	size_t copy_len;

	/* 0 length copy_len is a NOP for copy_to_user() and doesn't fail. */
	copy_len = min(len, udata->outlen);
	if (copy_to_user(udata->outbuf, src, copy_len))
		goto err_fault;
	if (copy_len < udata->outlen) {
		if (clear_user(udata->outbuf + copy_len,
			       udata->outlen - copy_len))
			goto err_fault;
	}
	return 0;
err_fault:
	ibdev_dbg(
		rdma_udata_to_dev(udata),
		"System call driver out udata has EFAULT (%zu into %zu) for ioctl %ps called by %pSR\n",
		len, udata->outlen, uverbs_get_handler_fn(udata),
		__builtin_return_address(0));
	return -EFAULT;
}
EXPORT_SYMBOL(_ib_respond_udata);

/*
 * Must be called with the ufile->device->disassociate_srcu held, and the lock
 * must be held until use of the ucontext is finished.
 */
struct ib_ucontext *ib_uverbs_get_ucontext_file(struct ib_uverbs_file *ufile)
{
	/*
	 * We do not hold the hw_destroy_rwsem lock for this flow, instead
	 * srcu is used. It does not matter if someone races this with
	 * get_context, we get NULL or valid ucontext.
	 */
	struct ib_ucontext *ucontext = smp_load_acquire(&ufile->ucontext);

	if (!srcu_dereference(ufile->device->ib_dev,
			      &ufile->device->disassociate_srcu))
		return ERR_PTR(-EIO);

	if (!ucontext)
		return ERR_PTR(-EINVAL);

	return ucontext;
}
EXPORT_SYMBOL(ib_uverbs_get_ucontext_file);

int uverbs_destroy_def_handler(struct uverbs_attr_bundle *attrs)
{
	return 0;
}
EXPORT_SYMBOL(uverbs_destroy_def_handler);

/*
 * When calling a destroy function during an error unwind we need to pass in
 * the udata that is sanitized of all user arguments. Ie from the driver
 * perspective it looks like no udata was passed.
 */
struct ib_udata *uverbs_get_cleared_udata(struct uverbs_attr_bundle *attrs)
{
	attrs->driver_udata = (struct ib_udata){};
	return &attrs->driver_udata;
}
EXPORT_SYMBOL_NS_GPL(uverbs_get_cleared_udata, "rdma_core");

/**
 * _uverbs_alloc() - Quickly allocate memory for use with a bundle
 * @bundle: The bundle
 * @size: Number of bytes to allocate
 * @flags: Allocator flags
 *
 * The bundle allocator is intended for allocations that are connected with
 * processing the system call related to the bundle. The allocated memory is
 * always freed once the system call completes, and cannot be freed any other
 * way.
 *
 * This tries to use a small pool of pre-allocated memory for performance.
 */
__malloc void *_uverbs_alloc(struct uverbs_attr_bundle *bundle, size_t size,
			     gfp_t flags)
{
	struct bundle_priv *pbundle =
		container_of(&bundle->hdr, struct bundle_priv, bundle);
	size_t new_used;
	void *res;

	if (check_add_overflow(size, pbundle->internal_used, &new_used))
		return ERR_PTR(-EOVERFLOW);

	if (new_used > pbundle->internal_avail) {
		struct bundle_alloc_head *buf;

		buf = kvmalloc_flex(*buf, data, size, flags);
		if (!buf)
			return ERR_PTR(-ENOMEM);
		buf->next = pbundle->allocated_mem;
		pbundle->allocated_mem = buf;
		return buf->data;
	}

	res = (void *)pbundle->internal_buffer + pbundle->internal_used;
	pbundle->internal_used =
		ALIGN(new_used, sizeof(*pbundle->internal_buffer));
	if (want_init_on_alloc(flags))
		memset(res, 0, size);
	return res;
}
EXPORT_SYMBOL(_uverbs_alloc);

int uverbs_copy_to(const struct uverbs_attr_bundle *bundle, size_t idx,
		   const void *from, size_t size)
{
	const struct uverbs_attr *attr = uverbs_attr_get(bundle, idx);
	size_t min_size;

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	min_size = min_t(size_t, attr->ptr_attr.len, size);
	if (copy_to_user(u64_to_user_ptr(attr->ptr_attr.data), from, min_size))
		return -EFAULT;

	return uverbs_set_output(bundle, attr);
}
EXPORT_SYMBOL(uverbs_copy_to);

int uverbs_copy_to_struct_or_zero(const struct uverbs_attr_bundle *bundle,
				  size_t idx, const void *from, size_t size)
{
	const struct uverbs_attr *attr = uverbs_attr_get(bundle, idx);

	if (IS_ERR(attr))
		return PTR_ERR(attr);

	if (size < attr->ptr_attr.len) {
		if (clear_user(u64_to_user_ptr(attr->ptr_attr.data) + size,
			       attr->ptr_attr.len - size))
			return -EFAULT;
	}
	return uverbs_copy_to(bundle, idx, from, size);
}
EXPORT_SYMBOL(uverbs_copy_to_struct_or_zero);

int _uverbs_get_const_unsigned(u64 *to,
			       const struct uverbs_attr_bundle *attrs_bundle,
			       size_t idx, u64 upper_bound, u64 *def_val)
{
	const struct uverbs_attr *attr;

	attr = uverbs_attr_get(attrs_bundle, idx);
	if (IS_ERR(attr)) {
		if ((PTR_ERR(attr) != -ENOENT) || !def_val)
			return PTR_ERR(attr);

		*to = *def_val;
	} else {
		*to = attr->ptr_attr.data;
	}

	if (*to > upper_bound)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(_uverbs_get_const_unsigned);

int _uverbs_get_const_signed(s64 *to,
			     const struct uverbs_attr_bundle *attrs_bundle,
			     size_t idx, s64 lower_bound, u64 upper_bound,
			     s64  *def_val)
{
	const struct uverbs_attr *attr;

	attr = uverbs_attr_get(attrs_bundle, idx);
	if (IS_ERR(attr)) {
		if ((PTR_ERR(attr) != -ENOENT) || !def_val)
			return PTR_ERR(attr);

		*to = *def_val;
	} else {
		*to = attr->ptr_attr.data;
	}

	if (*to < lower_bound || (*to > 0 && (u64)*to > upper_bound))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(_uverbs_get_const_signed);

int uverbs_get_flags64(u64 *to, const struct uverbs_attr_bundle *attrs_bundle,
		       size_t idx, u64 allowed_bits)
{
	const struct uverbs_attr *attr;
	u64 flags;

	attr = uverbs_attr_get(attrs_bundle, idx);
	/* Missing attribute means 0 flags */
	if (IS_ERR(attr)) {
		*to = 0;
		return 0;
	}

	/*
	 * New userspace code should use 8 bytes to pass flags, but we
	 * transparently support old userspaces that were using 4 bytes as
	 * well.
	 */
	if (attr->ptr_attr.len == 8)
		flags = attr->ptr_attr.data;
	else if (attr->ptr_attr.len == 4)
		flags = *(u32 *)&attr->ptr_attr.data;
	else
		return -EINVAL;

	if (flags & ~allowed_bits)
		return -EINVAL;

	*to = flags;
	return 0;
}
EXPORT_SYMBOL(uverbs_get_flags64);

int uverbs_get_flags32(u32 *to, const struct uverbs_attr_bundle *attrs_bundle,
		       size_t idx, u64 allowed_bits)
{
	u64 flags;
	int ret;

	ret = uverbs_get_flags64(&flags, attrs_bundle, idx, allowed_bits);
	if (ret)
		return ret;

	if (flags > U32_MAX)
		return -EINVAL;
	*to = flags;

	return 0;
}
EXPORT_SYMBOL(uverbs_get_flags32);

/**
 * uverbs_get_buffer_desc - Read a buffer descriptor from a uverbs attr.
 * @attrs_bundle: uverbs attribute bundle.
 * @attr_id:      id of an UVERBS_ATTR_UMEM-typed attribute.
 * @desc:         descriptor to fill.
 *
 * Return: 0 on success, -ENOENT if @attr_id is not set, -EINVAL on a
 * malformed descriptor, or any other negative errno propagated from
 * uverbs_copy_from() (notably -EFAULT on copy_from_user() failure).
 */
int uverbs_get_buffer_desc(const struct uverbs_attr_bundle *attrs_bundle,
			   u16 attr_id, struct ib_uverbs_buffer_desc *desc)
{
	int ret;

	ret = uverbs_copy_from(desc, attrs_bundle, attr_id);
	if (ret)
		return ret;
	if (desc->flags & ~IB_UVERBS_BUFFER_DESC_FLAGS_KNOWN_MASK)
		return -EINVAL;
	desc->optional_flags &= IB_UVERBS_BUFFER_DESC_OPTIONAL_FLAGS_KNOWN_MASK;
	return 0;
}
EXPORT_SYMBOL(uverbs_get_buffer_desc);

/* Once called an abort will call through to the type's destroy_hw() */
void uverbs_finalize_uobj_create(const struct uverbs_attr_bundle *bundle,
				 u16 idx)
{
	struct bundle_priv *pbundle =
		container_of(&bundle->hdr, struct bundle_priv, bundle);

	__set_bit(uapi_bkey_attr(uapi_key_attr(idx)),
		  pbundle->uobj_hw_obj_valid);
}
EXPORT_SYMBOL(uverbs_finalize_uobj_create);
