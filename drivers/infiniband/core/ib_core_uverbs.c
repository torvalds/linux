// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 * Copyright 2019 Marvell. All rights reserved.
 */
#include <linux/xarray.h>
#include "uverbs.h"
#include "core_priv.h"

/*
 * Each time we map IO memory into user space this keeps track of the mapping.
 * When the device is hot-unplugged we 'zap' the mmaps in user space to point
 * to the zero page and allow the hot unplug to proceed.
 *
 * This is necessary for cases like PCI physical hot unplug as the actual BAR
 * memory may vanish after this and access to it from userspace could MCE.
 *
 * RDMA drivers supporting disassociation must have their user space designed
 * to cope in some way with their IO pages going to the zero page.
 */
void rdma_umap_priv_init(struct rdma_umap_priv *priv,
			 struct vm_area_struct *vma)
{
	struct ib_uverbs_file *ufile = vma->vm_file->private_data;

	priv->vma = vma;
	vma->vm_private_data = priv;
	/* vm_ops is setup in ib_uverbs_mmap() to avoid module dependencies */

	mutex_lock(&ufile->umap_lock);
	list_add(&priv->list, &ufile->umaps);
	mutex_unlock(&ufile->umap_lock);
}
EXPORT_SYMBOL(rdma_umap_priv_init);

/*
 * Map IO memory into a process. This is to be called by drivers as part of
 * their mmap() functions if they wish to send something like PCI-E BAR memory
 * to userspace.
 */
int rdma_user_mmap_io(struct ib_ucontext *ucontext, struct vm_area_struct *vma,
		      unsigned long pfn, unsigned long size, pgprot_t prot)
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

	rdma_umap_priv_init(priv, vma);
	return 0;
}
EXPORT_SYMBOL(rdma_user_mmap_io);
