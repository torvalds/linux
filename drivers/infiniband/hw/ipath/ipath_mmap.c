/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <asm/pgtable.h>

#include "ipath_verbs.h"

/**
 * ipath_release_mmap_info - free mmap info structure
 * @ref: a pointer to the kref within struct ipath_mmap_info
 */
void ipath_release_mmap_info(struct kref *ref)
{
	struct ipath_mmap_info *ip =
		container_of(ref, struct ipath_mmap_info, ref);

	vfree(ip->obj);
	kfree(ip);
}

/*
 * open and close keep track of how many times the CQ is mapped,
 * to avoid releasing it.
 */
static void ipath_vma_open(struct vm_area_struct *vma)
{
	struct ipath_mmap_info *ip = vma->vm_private_data;

	kref_get(&ip->ref);
	ip->mmap_cnt++;
}

static void ipath_vma_close(struct vm_area_struct *vma)
{
	struct ipath_mmap_info *ip = vma->vm_private_data;

	ip->mmap_cnt--;
	kref_put(&ip->ref, ipath_release_mmap_info);
}

static struct vm_operations_struct ipath_vm_ops = {
	.open =     ipath_vma_open,
	.close =    ipath_vma_close,
};

/**
 * ipath_mmap - create a new mmap region
 * @context: the IB user context of the process making the mmap() call
 * @vma: the VMA to be initialized
 * Return zero if the mmap is OK. Otherwise, return an errno.
 */
int ipath_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct ipath_ibdev *dev = to_idev(context->device);
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long size = vma->vm_end - vma->vm_start;
	struct ipath_mmap_info *ip, **pp;
	int ret = -EINVAL;

	/*
	 * Search the device's list of objects waiting for a mmap call.
	 * Normally, this list is very short since a call to create a
	 * CQ, QP, or SRQ is soon followed by a call to mmap().
	 */
	spin_lock_irq(&dev->pending_lock);
	for (pp = &dev->pending_mmaps; (ip = *pp); pp = &ip->next) {
		/* Only the creator is allowed to mmap the object */
		if (context != ip->context || (void *) offset != ip->obj)
			continue;
		/* Don't allow a mmap larger than the object. */
		if (size > ip->size)
			break;

		*pp = ip->next;
		spin_unlock_irq(&dev->pending_lock);

		ret = remap_vmalloc_range(vma, ip->obj, 0);
		if (ret)
			goto done;
		vma->vm_ops = &ipath_vm_ops;
		vma->vm_private_data = ip;
		ipath_vma_open(vma);
		goto done;
	}
	spin_unlock_irq(&dev->pending_lock);
done:
	return ret;
}
