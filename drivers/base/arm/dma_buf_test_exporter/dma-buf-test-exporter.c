// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2012-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/dma-buf-test-exporter.h>
#include <linux/dma-buf.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#if (KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE)
#include <linux/dma-attrs.h>
#endif
#include <linux/dma-mapping.h>

/* Maximum size allowed in a single DMA_BUF_TE_ALLOC call */
#define DMA_BUF_TE_ALLOC_MAX_SIZE ((8ull << 30) >> PAGE_SHIFT) /* 8 GB */

/* Since kernel version 5.0 CONFIG_ARCH_NO_SG_CHAIN replaced CONFIG_ARCH_HAS_SG_CHAIN */
#if KERNEL_VERSION(5, 0, 0) > LINUX_VERSION_CODE
#if (!defined(ARCH_HAS_SG_CHAIN) && !defined(CONFIG_ARCH_HAS_SG_CHAIN))
#define NO_SG_CHAIN
#endif
#elif defined(CONFIG_ARCH_NO_SG_CHAIN)
#define NO_SG_CHAIN
#endif

struct dma_buf_te_alloc {
	/* the real alloc */
	size_t nr_pages;
	struct page **pages;

	/* the debug usage tracking */
	int nr_attached_devices;
	int nr_device_mappings;
	int nr_cpu_mappings;

	/* failure simulation */
	int fail_attach;
	int fail_map;
	int fail_mmap;

	bool contiguous;
	dma_addr_t contig_dma_addr;
	void *contig_cpu_addr;
};

struct dma_buf_te_attachment {
	struct sg_table *sg;
	bool attachment_mapped;
};

static struct miscdevice te_device;

#if (KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE)
static int dma_buf_te_attach(struct dma_buf *buf, struct device *dev, struct dma_buf_attachment *attachment)
#else
static int dma_buf_te_attach(struct dma_buf *buf, struct dma_buf_attachment *attachment)
#endif
{
	struct dma_buf_te_alloc	*alloc;
	alloc = buf->priv;

	if (alloc->fail_attach)
		return -EFAULT;

	attachment->priv = kzalloc(sizeof(struct dma_buf_te_attachment), GFP_KERNEL);
	if (!attachment->priv)
		return -ENOMEM;

	/* dma_buf is externally locked during call */
	alloc->nr_attached_devices++;
	return 0;
}

static void dma_buf_te_detach(struct dma_buf *buf, struct dma_buf_attachment *attachment)
{
	struct dma_buf_te_alloc *alloc = buf->priv;
	struct dma_buf_te_attachment *pa = attachment->priv;

	/* dma_buf is externally locked during call */

	WARN(pa->attachment_mapped, "WARNING: dma-buf-test-exporter detected detach with open device mappings");

	alloc->nr_attached_devices--;

	kfree(pa);
}

static struct sg_table *dma_buf_te_map(struct dma_buf_attachment *attachment, enum dma_data_direction direction)
{
	struct sg_table *sg;
	struct scatterlist *iter;
	struct dma_buf_te_alloc	*alloc;
	struct dma_buf_te_attachment *pa = attachment->priv;
	size_t i;
	int ret;

	alloc = attachment->dmabuf->priv;

	if (alloc->fail_map)
		return ERR_PTR(-ENOMEM);

	if (WARN(pa->attachment_mapped,
	    "WARNING: Attempted to map already mapped attachment."))
		return ERR_PTR(-EBUSY);

#ifdef NO_SG_CHAIN
	/* if the ARCH can't chain we can't have allocs larger than a single sg can hold */
	if (alloc->nr_pages > SG_MAX_SINGLE_ALLOC)
		return ERR_PTR(-EINVAL);
#endif /* NO_SG_CHAIN */

	sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	/* from here we access the allocation object, so lock the dmabuf pointing to it */
	mutex_lock(&attachment->dmabuf->lock);

	if (alloc->contiguous)
		ret = sg_alloc_table(sg, 1, GFP_KERNEL);
	else
		ret = sg_alloc_table(sg, alloc->nr_pages, GFP_KERNEL);
	if (ret) {
		mutex_unlock(&attachment->dmabuf->lock);
		kfree(sg);
		return ERR_PTR(ret);
	}

	if (alloc->contiguous) {
		sg_dma_len(sg->sgl) = alloc->nr_pages * PAGE_SIZE;
		sg_set_page(sg->sgl, pfn_to_page(PFN_DOWN(alloc->contig_dma_addr)), alloc->nr_pages * PAGE_SIZE, 0);
		sg_dma_address(sg->sgl) = alloc->contig_dma_addr;
	} else {
		for_each_sg(sg->sgl, iter, alloc->nr_pages, i)
			sg_set_page(iter, alloc->pages[i], PAGE_SIZE, 0);
	}

	if (!dma_map_sg(attachment->dev, sg->sgl, sg->nents, direction)) {
		mutex_unlock(&attachment->dmabuf->lock);
		sg_free_table(sg);
		kfree(sg);
		return ERR_PTR(-ENOMEM);
	}

	alloc->nr_device_mappings++;
	pa->attachment_mapped = true;
	pa->sg = sg;
	mutex_unlock(&attachment->dmabuf->lock);
	return sg;
}

static void dma_buf_te_unmap(struct dma_buf_attachment *attachment,
							 struct sg_table *sg, enum dma_data_direction direction)
{
	struct dma_buf_te_alloc *alloc;
	struct dma_buf_te_attachment *pa = attachment->priv;

	alloc = attachment->dmabuf->priv;

	mutex_lock(&attachment->dmabuf->lock);

	WARN(!pa->attachment_mapped, "WARNING: Unmatched unmap of attachment.");

	alloc->nr_device_mappings--;
	pa->attachment_mapped = false;
	pa->sg = NULL;
	mutex_unlock(&attachment->dmabuf->lock);

	dma_unmap_sg(attachment->dev, sg->sgl, sg->nents, direction);
	sg_free_table(sg);
	kfree(sg);
}

static void dma_buf_te_release(struct dma_buf *buf)
{
	size_t i;
	struct dma_buf_te_alloc *alloc;
	alloc = buf->priv;
	/* no need for locking */

	if (alloc->contiguous) {
#if (KERNEL_VERSION(4, 8, 0) <= LINUX_VERSION_CODE)
		dma_free_attrs(te_device.this_device,
						alloc->nr_pages * PAGE_SIZE,
						alloc->contig_cpu_addr,
						alloc->contig_dma_addr,
						DMA_ATTR_WRITE_COMBINE);
#else
		DEFINE_DMA_ATTRS(attrs);

		dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
		dma_free_attrs(te_device.this_device,
						alloc->nr_pages * PAGE_SIZE,
						alloc->contig_cpu_addr, alloc->contig_dma_addr, &attrs);
#endif
	} else {
		for (i = 0; i < alloc->nr_pages; i++)
			__free_page(alloc->pages[i]);
	}
#if (KERNEL_VERSION(4, 12, 0) <= LINUX_VERSION_CODE)
	kvfree(alloc->pages);
#else
	kfree(alloc->pages);
#endif
	kfree(alloc);
}

static int dma_buf_te_sync(struct dma_buf *dmabuf,
			enum dma_data_direction direction,
			bool start_cpu_access)
{
	struct dma_buf_attachment *attachment;

	mutex_lock(&dmabuf->lock);

	list_for_each_entry(attachment, &dmabuf->attachments, node) {
		struct dma_buf_te_attachment *pa = attachment->priv;
		struct sg_table *sg = pa->sg;
		if (!sg) {
			dev_dbg(te_device.this_device, "no mapping for device %s\n", dev_name(attachment->dev));
			continue;
		}

		if (start_cpu_access) {
			dev_dbg(te_device.this_device, "sync cpu with device %s\n", dev_name(attachment->dev));

			dma_sync_sg_for_cpu(attachment->dev, sg->sgl, sg->nents, direction);
		} else {
			dev_dbg(te_device.this_device, "sync device %s with cpu\n", dev_name(attachment->dev));

			dma_sync_sg_for_device(attachment->dev, sg->sgl, sg->nents, direction);
		}
	}

	mutex_unlock(&dmabuf->lock);
	return 0;
}

#if (KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE)
static int dma_buf_te_begin_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction direction)
#else
static int dma_buf_te_begin_cpu_access(struct dma_buf *dmabuf, size_t start,
					size_t len,
					enum dma_data_direction direction)
#endif
{
	return dma_buf_te_sync(dmabuf, direction, true);
}

#if (KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE)
static int dma_buf_te_end_cpu_access(struct dma_buf *dmabuf,
				enum dma_data_direction direction)
{
	return dma_buf_te_sync(dmabuf, direction, false);
}
#else
static void dma_buf_te_end_cpu_access(struct dma_buf *dmabuf, size_t start,
				size_t len,
				enum dma_data_direction direction)
{
	dma_buf_te_sync(dmabuf, direction, false);
}
#endif

static void dma_buf_te_mmap_open(struct vm_area_struct *vma)
{
	struct dma_buf *dma_buf;
	struct dma_buf_te_alloc *alloc;
	dma_buf = vma->vm_private_data;
	alloc = dma_buf->priv;

	mutex_lock(&dma_buf->lock);
	alloc->nr_cpu_mappings++;
	mutex_unlock(&dma_buf->lock);
}

static void dma_buf_te_mmap_close(struct vm_area_struct *vma)
{
	struct dma_buf *dma_buf;
	struct dma_buf_te_alloc *alloc;
	dma_buf = vma->vm_private_data;
	alloc = dma_buf->priv;

	BUG_ON(alloc->nr_cpu_mappings <= 0);
	mutex_lock(&dma_buf->lock);
	alloc->nr_cpu_mappings--;
	mutex_unlock(&dma_buf->lock);
}

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
static int dma_buf_te_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
#elif KERNEL_VERSION(5, 1, 0) > LINUX_VERSION_CODE
static int dma_buf_te_mmap_fault(struct vm_fault *vmf)
#else
static vm_fault_t dma_buf_te_mmap_fault(struct vm_fault *vmf)
#endif
{
	struct dma_buf_te_alloc *alloc;
	struct dma_buf *dmabuf;
	struct page *pageptr;

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	dmabuf = vma->vm_private_data;
#else
	dmabuf = vmf->vma->vm_private_data;
#endif
	alloc = dmabuf->priv;

	if (vmf->pgoff > alloc->nr_pages)
		return VM_FAULT_SIGBUS;

	pageptr = alloc->pages[vmf->pgoff];

	BUG_ON(!pageptr);

	get_page(pageptr);
	vmf->page = pageptr;

	return 0;
}

struct vm_operations_struct dma_buf_te_vm_ops = {
	.open = dma_buf_te_mmap_open,
	.close = dma_buf_te_mmap_close,
	.fault = dma_buf_te_mmap_fault
};

static int dma_buf_te_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct dma_buf_te_alloc *alloc;
	alloc = dmabuf->priv;

	if (alloc->fail_mmap)
		return -ENOMEM;

	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = &dma_buf_te_vm_ops;
	vma->vm_private_data = dmabuf;

	/*  we fault in the pages on access */

	/* call open to do the ref-counting */
	dma_buf_te_vm_ops.open(vma);

	return 0;
}

#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
static void *dma_buf_te_kmap_atomic(struct dma_buf *buf, unsigned long page_num)
{
	/* IGNORE */
	return NULL;
}
#endif

static void *dma_buf_te_kmap(struct dma_buf *buf, unsigned long page_num)
{
	struct dma_buf_te_alloc *alloc;

	alloc = buf->priv;
	if (page_num >= alloc->nr_pages)
		return NULL;

	return kmap(alloc->pages[page_num]);
}
static void dma_buf_te_kunmap(struct dma_buf *buf,
		unsigned long page_num, void *addr)
{
	struct dma_buf_te_alloc *alloc;

	alloc = buf->priv;
	if (page_num >= alloc->nr_pages)
		return;

	kunmap(alloc->pages[page_num]);
	return;
}

static struct dma_buf_ops dma_buf_te_ops = {
	/* real handlers */
	.attach = dma_buf_te_attach,
	.detach = dma_buf_te_detach,
	.map_dma_buf = dma_buf_te_map,
	.unmap_dma_buf = dma_buf_te_unmap,
	.release = dma_buf_te_release,
	.mmap = dma_buf_te_mmap,
	.begin_cpu_access = dma_buf_te_begin_cpu_access,
	.end_cpu_access = dma_buf_te_end_cpu_access,
#if KERNEL_VERSION(4, 12, 0) > LINUX_VERSION_CODE
	.kmap = dma_buf_te_kmap,
	.kunmap = dma_buf_te_kunmap,

	/* nop handlers for mandatory functions we ignore */
	.kmap_atomic = dma_buf_te_kmap_atomic
#else
#if KERNEL_VERSION(5, 6, 0) > LINUX_VERSION_CODE
	.map = dma_buf_te_kmap,
	.unmap = dma_buf_te_kunmap,
#endif

#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
	/* nop handlers for mandatory functions we ignore */
	.map_atomic = dma_buf_te_kmap_atomic
#endif
#endif
};

static int do_dma_buf_te_ioctl_version(struct dma_buf_te_ioctl_version __user *buf)
{
	struct dma_buf_te_ioctl_version v;

	if (copy_from_user(&v, buf, sizeof(v)))
		return -EFAULT;

	if (v.op != DMA_BUF_TE_ENQ)
		return -EFAULT;

	v.op = DMA_BUF_TE_ACK;
	v.major = DMA_BUF_TE_VER_MAJOR;
	v.minor = DMA_BUF_TE_VER_MINOR;

	if (copy_to_user(buf, &v, sizeof(v)))
		return -EFAULT;
	else
		return 0;
}

static int do_dma_buf_te_ioctl_alloc(struct dma_buf_te_ioctl_alloc __user *buf, bool contiguous)
{
	struct dma_buf_te_ioctl_alloc alloc_req;
	struct dma_buf_te_alloc *alloc;
	struct dma_buf *dma_buf;
	size_t i = 0;
	size_t max_nr_pages = DMA_BUF_TE_ALLOC_MAX_SIZE;
	int fd;

	if (copy_from_user(&alloc_req, buf, sizeof(alloc_req))) {
		dev_err(te_device.this_device, "%s: couldn't get user data", __func__);
		goto no_input;
	}

	if (!alloc_req.size) {
		dev_err(te_device.this_device, "%s: no size specified", __func__);
		goto invalid_size;
	}

#ifdef NO_SG_CHAIN
	/* Whilst it is possible to allocate larger buffer, we won't be able to
	 * map it during actual usage (mmap() still succeeds). We fail here so
	 * userspace code can deal with it early than having driver failure
	 * later on.
	 */
	if (max_nr_pages > SG_MAX_SINGLE_ALLOC)
		max_nr_pages = SG_MAX_SINGLE_ALLOC;
#endif /* NO_SG_CHAIN */

	if (alloc_req.size > max_nr_pages) {
		dev_err(te_device.this_device, "%s: buffer size of %llu pages exceeded the mapping limit of %zu pages",
				__func__, alloc_req.size, max_nr_pages);
		goto invalid_size;
	}

	alloc = kzalloc(sizeof(struct dma_buf_te_alloc), GFP_KERNEL);
	if (alloc == NULL) {
		dev_err(te_device.this_device, "%s: couldn't alloc object", __func__);
		goto no_alloc_object;
	}

	alloc->nr_pages = alloc_req.size;
	alloc->contiguous = contiguous;

#if (KERNEL_VERSION(4, 12, 0) <= LINUX_VERSION_CODE)
	alloc->pages = kvzalloc(sizeof(struct page *) * alloc->nr_pages, GFP_KERNEL);
#else
	alloc->pages = kzalloc(sizeof(struct page *) * alloc->nr_pages, GFP_KERNEL);
#endif

	if (!alloc->pages) {
		dev_err(te_device.this_device,
				"%s: couldn't alloc %zu page structures",
				__func__, alloc->nr_pages);
		goto free_alloc_object;
	}

	if (contiguous) {
		dma_addr_t dma_aux;

#if (KERNEL_VERSION(4, 8, 0) <= LINUX_VERSION_CODE)
		alloc->contig_cpu_addr = dma_alloc_attrs(te_device.this_device,
				alloc->nr_pages * PAGE_SIZE,
				&alloc->contig_dma_addr,
				GFP_KERNEL | __GFP_ZERO,
				DMA_ATTR_WRITE_COMBINE);
#else
		DEFINE_DMA_ATTRS(attrs);

		dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
		alloc->contig_cpu_addr = dma_alloc_attrs(te_device.this_device,
				alloc->nr_pages * PAGE_SIZE,
				&alloc->contig_dma_addr,
				GFP_KERNEL | __GFP_ZERO, &attrs);
#endif
		if (!alloc->contig_cpu_addr) {
			dev_err(te_device.this_device, "%s: couldn't alloc contiguous buffer %zu pages",
				__func__, alloc->nr_pages);
			goto free_page_struct;
		}
		dma_aux = alloc->contig_dma_addr;
		for (i = 0; i < alloc->nr_pages; i++) {
			alloc->pages[i] = pfn_to_page(PFN_DOWN(dma_aux));
			dma_aux += PAGE_SIZE;
		}
	} else {
		for (i = 0; i < alloc->nr_pages; i++) {
			alloc->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
			if (alloc->pages[i] == NULL) {
				dev_err(te_device.this_device, "%s: couldn't alloc page", __func__);
				goto no_page;
			}
		}
	}

	/* alloc ready, let's export it */
	{
		struct dma_buf_export_info export_info = {
			.exp_name = "dma_buf_te",
			.owner = THIS_MODULE,
			.ops = &dma_buf_te_ops,
			.size = alloc->nr_pages << PAGE_SHIFT,
			.flags = O_CLOEXEC | O_RDWR,
			.priv = alloc,
		};

		dma_buf = dma_buf_export(&export_info);
	}

	if (IS_ERR_OR_NULL(dma_buf)) {
		dev_err(te_device.this_device, "%s: couldn't export dma_buf", __func__);
		goto no_export;
	}

	/* get fd for buf */
	fd = dma_buf_fd(dma_buf, O_CLOEXEC);

	if (fd < 0) {
		dev_err(te_device.this_device, "%s: couldn't get fd from dma_buf", __func__);
		goto no_fd;
	}

	return fd;

no_fd:
	dma_buf_put(dma_buf);
no_export:
	/* i still valid */
no_page:
	if (contiguous) {
#if (KERNEL_VERSION(4, 8, 0) <= LINUX_VERSION_CODE)
		dma_free_attrs(te_device.this_device,
						alloc->nr_pages * PAGE_SIZE,
						alloc->contig_cpu_addr,
						alloc->contig_dma_addr,
						DMA_ATTR_WRITE_COMBINE);
#else
		DEFINE_DMA_ATTRS(attrs);

		dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);
		dma_free_attrs(te_device.this_device,
						alloc->nr_pages * PAGE_SIZE,
						alloc->contig_cpu_addr, alloc->contig_dma_addr, &attrs);
#endif
	} else {
		while (i-- > 0)
			__free_page(alloc->pages[i]);
	}
free_page_struct:
#if (KERNEL_VERSION(4, 12, 0) <= LINUX_VERSION_CODE)
	kvfree(alloc->pages);
#else
	kfree(alloc->pages);
#endif
free_alloc_object:
	kfree(alloc);
no_alloc_object:
invalid_size:
no_input:
	return -EFAULT;
}

static int do_dma_buf_te_ioctl_status(struct dma_buf_te_ioctl_status __user *arg)
{
	struct dma_buf_te_ioctl_status status;
	struct dma_buf *dmabuf;
	struct dma_buf_te_alloc *alloc;
	int res = -EINVAL;

	if (copy_from_user(&status, arg, sizeof(status)))
		return -EFAULT;

	dmabuf = dma_buf_get(status.fd);
	if (IS_ERR_OR_NULL(dmabuf))
		return -EINVAL;

	/* verify it's one of ours */
	if (dmabuf->ops != &dma_buf_te_ops)
		goto err_have_dmabuf;

	/* ours, get the current status */
	alloc = dmabuf->priv;

	/* lock while reading status to take a snapshot */
	mutex_lock(&dmabuf->lock);
	status.attached_devices = alloc->nr_attached_devices;
	status.device_mappings = alloc->nr_device_mappings;
	status.cpu_mappings = alloc->nr_cpu_mappings;
	mutex_unlock(&dmabuf->lock);

	if (copy_to_user(arg, &status, sizeof(status)))
		goto err_have_dmabuf;

	/* All OK */
	res = 0;

err_have_dmabuf:
	dma_buf_put(dmabuf);
	return res;
}

static int do_dma_buf_te_ioctl_set_failing(struct dma_buf_te_ioctl_set_failing __user *arg)
{
	struct dma_buf *dmabuf;
	struct dma_buf_te_ioctl_set_failing f;
	struct dma_buf_te_alloc *alloc;
	int res = -EINVAL;

	if (copy_from_user(&f, arg, sizeof(f)))
		return -EFAULT;

	dmabuf = dma_buf_get(f.fd);
	if (IS_ERR_OR_NULL(dmabuf))
		return -EINVAL;

	/* verify it's one of ours */
	if (dmabuf->ops != &dma_buf_te_ops)
		goto err_have_dmabuf;

	/* ours, set the fail modes */
	alloc = dmabuf->priv;
	/* lock to set the fail modes atomically */
	mutex_lock(&dmabuf->lock);
	alloc->fail_attach = f.fail_attach;
	alloc->fail_map    = f.fail_map;
	alloc->fail_mmap   = f.fail_mmap;
	mutex_unlock(&dmabuf->lock);

	/* success */
	res = 0;

err_have_dmabuf:
	dma_buf_put(dmabuf);
	return res;
}

static u32 dma_te_buf_fill(struct dma_buf *dma_buf, unsigned int value)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	struct scatterlist *sg;
	unsigned int count;
	unsigned int offset = 0;
	int ret = 0;
	size_t i;

	attachment = dma_buf_attach(dma_buf, te_device.this_device);
	if (IS_ERR_OR_NULL(attachment))
		return -EBUSY;

	sgt = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(sgt)) {
		ret = PTR_ERR(sgt);
		goto no_import;
	}

	ret = dma_buf_begin_cpu_access(dma_buf,
#if KERNEL_VERSION(4, 6, 0) > LINUX_VERSION_CODE
				       0, dma_buf->size,
#endif
				       DMA_BIDIRECTIONAL);
	if (ret)
		goto no_cpu_access;

	for_each_sg(sgt->sgl, sg, sgt->nents, count) {
		for (i = 0; i < sg_dma_len(sg); i = i + PAGE_SIZE) {
			void *addr = NULL;
#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
			addr = dma_buf_te_kmap(dma_buf, i >> PAGE_SHIFT);
#else
			addr = dma_buf_kmap(dma_buf, i >> PAGE_SHIFT);
#endif
			if (!addr) {
				ret = -EPERM;
				goto no_kmap;
			}
			memset(addr, value, PAGE_SIZE);
#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
			dma_buf_te_kunmap(dma_buf, i >> PAGE_SHIFT, addr);
#else
			dma_buf_kunmap(dma_buf, i >> PAGE_SHIFT, addr);
#endif
		}
		offset += sg_dma_len(sg);
	}

no_kmap:
	dma_buf_end_cpu_access(dma_buf,
#if KERNEL_VERSION(4, 6, 0) > LINUX_VERSION_CODE
			       0, dma_buf->size,
#endif
			       DMA_BIDIRECTIONAL);
no_cpu_access:
	dma_buf_unmap_attachment(attachment, sgt, DMA_BIDIRECTIONAL);
no_import:
	dma_buf_detach(dma_buf, attachment);
	return ret;
}

static int do_dma_buf_te_ioctl_fill(struct dma_buf_te_ioctl_fill __user *arg)
{

	struct dma_buf *dmabuf;
	struct dma_buf_te_ioctl_fill f;
	int ret;

	if (copy_from_user(&f, arg, sizeof(f)))
		return -EFAULT;

	dmabuf = dma_buf_get(f.fd);
	if (IS_ERR_OR_NULL(dmabuf))
		return -EINVAL;

	ret = dma_te_buf_fill(dmabuf, f.value);
	dma_buf_put(dmabuf);

	return ret;
}

static long dma_buf_te_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case DMA_BUF_TE_VERSION:
		return do_dma_buf_te_ioctl_version((struct dma_buf_te_ioctl_version __user *)arg);
	case DMA_BUF_TE_ALLOC:
		return do_dma_buf_te_ioctl_alloc((struct dma_buf_te_ioctl_alloc __user *)arg, false);
	case DMA_BUF_TE_ALLOC_CONT:
		return do_dma_buf_te_ioctl_alloc((struct dma_buf_te_ioctl_alloc __user *)arg, true);
	case DMA_BUF_TE_QUERY:
		return do_dma_buf_te_ioctl_status((struct dma_buf_te_ioctl_status __user *)arg);
	case DMA_BUF_TE_SET_FAILING:
		return do_dma_buf_te_ioctl_set_failing((struct dma_buf_te_ioctl_set_failing __user *)arg);
	case DMA_BUF_TE_FILL:
		return do_dma_buf_te_ioctl_fill((struct dma_buf_te_ioctl_fill __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations dma_buf_te_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dma_buf_te_ioctl,
	.compat_ioctl = dma_buf_te_ioctl,
};

static int __init dma_buf_te_init(void)
{
	int res;
	te_device.minor = MISC_DYNAMIC_MINOR;
	te_device.name = "dma_buf_te";
	te_device.fops = &dma_buf_te_fops;

	res = misc_register(&te_device);
	if (res) {
		printk(KERN_WARNING"Misc device registration failed of 'dma_buf_te'\n");
		return res;
	}
	te_device.this_device->coherent_dma_mask = DMA_BIT_MASK(32);

	dev_info(te_device.this_device, "dma_buf_te ready\n");
	return 0;

}

static void __exit dma_buf_te_exit(void)
{
	misc_deregister(&te_device);
}

module_init(dma_buf_te_init);
module_exit(dma_buf_te_exit);
MODULE_LICENSE("GPL");
