// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 * Copyright 2019 Marvell. All rights reserved.
 */
#include <linux/xarray.h>
#include "uverbs.h"
#include "core_priv.h"

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

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
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
	if (!entry)
		return;

	entry->driver_removed = true;
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
