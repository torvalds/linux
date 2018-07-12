// SPDX-License-Identifier: GPL-2.0 OR MIT

/******************************************************************************
 * privcmd-buf.c
 *
 * Mmap of hypercall buffers.
 *
 * Copyright (c) 2018 Juergen Gross
 */

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "privcmd.h"

MODULE_LICENSE("GPL");

static unsigned int limit = 64;
module_param(limit, uint, 0644);
MODULE_PARM_DESC(limit, "Maximum number of pages that may be allocated by "
			"the privcmd-buf device per open file");

struct privcmd_buf_private {
	struct mutex lock;
	struct list_head list;
	unsigned int allocated;
};

struct privcmd_buf_vma_private {
	struct privcmd_buf_private *file_priv;
	struct list_head list;
	unsigned int users;
	unsigned int n_pages;
	struct page *pages[];
};

static int privcmd_buf_open(struct inode *ino, struct file *file)
{
	struct privcmd_buf_private *file_priv;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	mutex_init(&file_priv->lock);
	INIT_LIST_HEAD(&file_priv->list);

	file->private_data = file_priv;

	return 0;
}

static void privcmd_buf_vmapriv_free(struct privcmd_buf_vma_private *vma_priv)
{
	unsigned int i;

	vma_priv->file_priv->allocated -= vma_priv->n_pages;

	list_del(&vma_priv->list);

	for (i = 0; i < vma_priv->n_pages; i++)
		if (vma_priv->pages[i])
			__free_page(vma_priv->pages[i]);

	kfree(vma_priv);
}

static int privcmd_buf_release(struct inode *ino, struct file *file)
{
	struct privcmd_buf_private *file_priv = file->private_data;
	struct privcmd_buf_vma_private *vma_priv;

	mutex_lock(&file_priv->lock);

	while (!list_empty(&file_priv->list)) {
		vma_priv = list_first_entry(&file_priv->list,
					    struct privcmd_buf_vma_private,
					    list);
		privcmd_buf_vmapriv_free(vma_priv);
	}

	mutex_unlock(&file_priv->lock);

	kfree(file_priv);

	return 0;
}

static void privcmd_buf_vma_open(struct vm_area_struct *vma)
{
	struct privcmd_buf_vma_private *vma_priv = vma->vm_private_data;

	if (!vma_priv)
		return;

	mutex_lock(&vma_priv->file_priv->lock);
	vma_priv->users++;
	mutex_unlock(&vma_priv->file_priv->lock);
}

static void privcmd_buf_vma_close(struct vm_area_struct *vma)
{
	struct privcmd_buf_vma_private *vma_priv = vma->vm_private_data;
	struct privcmd_buf_private *file_priv;

	if (!vma_priv)
		return;

	file_priv = vma_priv->file_priv;

	mutex_lock(&file_priv->lock);

	vma_priv->users--;
	if (!vma_priv->users)
		privcmd_buf_vmapriv_free(vma_priv);

	mutex_unlock(&file_priv->lock);
}

static vm_fault_t privcmd_buf_vma_fault(struct vm_fault *vmf)
{
	pr_debug("fault: vma=%p %lx-%lx, pgoff=%lx, uv=%p\n",
		 vmf->vma, vmf->vma->vm_start, vmf->vma->vm_end,
		 vmf->pgoff, (void *)vmf->address);

	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct privcmd_buf_vm_ops = {
	.open = privcmd_buf_vma_open,
	.close = privcmd_buf_vma_close,
	.fault = privcmd_buf_vma_fault,
};

static int privcmd_buf_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct privcmd_buf_private *file_priv = file->private_data;
	struct privcmd_buf_vma_private *vma_priv;
	unsigned long count = vma_pages(vma);
	unsigned int i;
	int ret = 0;

	if (!(vma->vm_flags & VM_SHARED) || count > limit ||
	    file_priv->allocated + count > limit)
		return -EINVAL;

	vma_priv = kzalloc(sizeof(*vma_priv) + count * sizeof(void *),
			   GFP_KERNEL);
	if (!vma_priv)
		return -ENOMEM;

	vma_priv->n_pages = count;
	count = 0;
	for (i = 0; i < vma_priv->n_pages; i++) {
		vma_priv->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!vma_priv->pages[i])
			break;
		count++;
	}

	mutex_lock(&file_priv->lock);

	file_priv->allocated += count;

	vma_priv->file_priv = file_priv;
	vma_priv->users = 1;

	vma->vm_flags |= VM_IO | VM_DONTEXPAND;
	vma->vm_ops = &privcmd_buf_vm_ops;
	vma->vm_private_data = vma_priv;

	list_add(&vma_priv->list, &file_priv->list);

	if (vma_priv->n_pages != count)
		ret = -ENOMEM;
	else
		for (i = 0; i < vma_priv->n_pages; i++) {
			ret = vm_insert_page(vma, vma->vm_start + i * PAGE_SIZE,
					     vma_priv->pages[i]);
			if (ret)
				break;
		}

	if (ret)
		privcmd_buf_vmapriv_free(vma_priv);

	mutex_unlock(&file_priv->lock);

	return ret;
}

const struct file_operations xen_privcmdbuf_fops = {
	.owner = THIS_MODULE,
	.open = privcmd_buf_open,
	.release = privcmd_buf_release,
	.mmap = privcmd_buf_mmap,
};
EXPORT_SYMBOL_GPL(xen_privcmdbuf_fops);

struct miscdevice xen_privcmdbuf_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xen/hypercall",
	.fops = &xen_privcmdbuf_fops,
};
