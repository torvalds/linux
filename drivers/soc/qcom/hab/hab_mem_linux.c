// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"
#include <linux/fdtable.h>
#include <linux/dma-buf.h>
#include "hab_grantable.h"

enum hab_page_list_type {
	HAB_PAGE_LIST_IMPORT = 0x1,
	HAB_PAGE_LIST_EXPORT
};

struct pages_list {
	struct list_head list;
	struct page **pages;
	long npages;
	void *vmapping;
	uint32_t userflags;
	int32_t export_id;
	int32_t vcid;
	struct physical_channel *pchan;
	uint32_t type;
	struct kref refcount;
};

struct importer_context {
	struct file *filp;
};

struct exp_platform_data {
	void *dmabuf;
	void *attach;
	void *sg_table;
};

static struct dma_buf_ops dma_buf_ops;

static struct pages_list *pages_list_create(
	struct export_desc *exp,
	uint32_t userflags)
{
	struct page **pages = NULL;
	struct compressed_pfns *pfn_table =
		(struct compressed_pfns *)exp->payload;
	struct pages_list *pglist = NULL;
	unsigned long pfn;
	int i, j, k = 0, size;
	unsigned long region_total_page = 0;

	if (!pfn_table)
		return ERR_PTR(-EINVAL);

	pfn = pfn_table->first_pfn;
	if (pfn_valid(pfn) == 0 || page_is_ram(pfn) == 0) {
		pr_err("imp sanity failed pfn %lx valid %d ram %d pchan %s\n",
			pfn, pfn_valid(pfn),
			page_is_ram(pfn), exp->pchan->name);
		return ERR_PTR(-EINVAL);
	}

	size = exp->payload_count * sizeof(struct page *);
	pages = vmalloc(size);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	pglist = kzalloc(sizeof(*pglist), GFP_KERNEL);
	if (!pglist) {
		vfree(pages);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < pfn_table->nregions; i++) {
		if (pfn_table->region[i].size <= 0) {
			pr_err("pfn_table->region[%d].size %d is less than 1\n",
				i, pfn_table->region[i].size);
			goto err_region_total_page;
		}

		region_total_page += pfn_table->region[i].size;
		if (region_total_page > exp->payload_count) {
			pr_err("payload_count %d but region_total_page %lu\n",
				exp->payload_count, region_total_page);
			goto err_region_total_page;
		}

		for (j = 0; j < pfn_table->region[i].size; j++) {
			pages[k] = pfn_to_page(pfn+j);
			k++;
		}
		pfn += pfn_table->region[i].size + pfn_table->region[i].space;
	}
	if (region_total_page != exp->payload_count) {
		pr_err("payload_count %d and region_total_page %lu are not equal\n",
			exp->payload_count, region_total_page);
		goto err_region_total_page;
	}

	pglist->pages = pages;
	pglist->npages = exp->payload_count;
	pglist->userflags = userflags;
	pglist->export_id = exp->export_id;
	pglist->vcid = exp->vcid_remote;
	pglist->pchan = exp->pchan;

	kref_init(&pglist->refcount);

	return pglist;

err_region_total_page:
	vfree(pages);
	kfree(pglist);
	return ERR_PTR(-EINVAL);
}

static void pages_list_add(struct pages_list *pglist)
{
	spin_lock_bh(&hab_driver.imp_lock);
	list_add_tail(&pglist->list,  &hab_driver.imp_list);
	hab_driver.imp_cnt++;
	spin_unlock_bh(&hab_driver.imp_lock);
}

static void pages_list_remove(struct pages_list *pglist)
{
	spin_lock_bh(&hab_driver.imp_lock);
	list_del(&pglist->list);
	hab_driver.imp_cnt--;
	spin_unlock_bh(&hab_driver.imp_lock);
}

static void pages_list_destroy(struct kref *refcount)
{
	struct pages_list *pglist = container_of(refcount,
				struct pages_list, refcount);

	if (pglist->vmapping) {
		vunmap(pglist->vmapping);
		pglist->vmapping = NULL;
	}

	/* the imported pages used, notify the remote */
	if (pglist->type == HAB_PAGE_LIST_IMPORT)
		pages_list_remove(pglist);

	vfree(pglist->pages);

	kfree(pglist);
}

static void pages_list_get(struct pages_list *pglist)
{
	kref_get(&pglist->refcount);
}

static int pages_list_put(struct pages_list *pglist)
{
	return kref_put(&pglist->refcount, pages_list_destroy);
}

static struct pages_list *pages_list_lookup(
		uint32_t export_id,
		struct physical_channel *pchan,
		bool get_pages_list)
{
	struct pages_list *pglist = NULL, *tmp = NULL;

	spin_lock_bh(&hab_driver.imp_lock);
	list_for_each_entry_safe(pglist, tmp, &hab_driver.imp_list, list) {
		if (pglist->export_id == export_id &&
			pglist->pchan == pchan) {
			if (get_pages_list)
				pages_list_get(pglist);
			spin_unlock_bh(&hab_driver.imp_lock);
			return pglist;
		}
	}
	spin_unlock_bh(&hab_driver.imp_lock);

	return NULL;
}

static int match_file(const void *p, struct file *file, unsigned int fd)
{
	/*
	 * We must return fd + 1 because iterate_fd stops searching on
	 * non-zero return, but 0 is a valid fd.
	 */
	return (p == file) ? (fd + 1) : 0;
}

static struct dma_buf *habmem_get_dma_buf_from_va(unsigned long address,
		int page_count,
		unsigned long *offset)
{
	struct vm_area_struct *vma = NULL;
	struct dma_buf *dmabuf = NULL;
	int rc = 0;
	int fd = -1;

	mmap_read_lock(current->mm);

	vma = find_vma(current->mm, address);
	if (!vma || !vma->vm_file) {
		pr_err("cannot find vma\n");
		rc = -EBADF;
		goto pro_end;
	}

	/* Look for the fd that matches this the vma file */
	fd = iterate_fd(current->files, 0, match_file, vma->vm_file);
	if (fd == 0) {
		pr_err("iterate_fd failed\n");
		rc = -EBADF;
		goto pro_end;
	}

	dmabuf = dma_buf_get(fd - 1);
	if (IS_ERR_OR_NULL(dmabuf)) {
		pr_err("dma_buf_get failed fd %d ret %pK\n", fd, dmabuf);
		rc = -EBADF;
		goto pro_end;
	}
	*offset = address - vma->vm_start;

pro_end:
	mmap_read_unlock(current->mm);

	return rc < 0 ? ERR_PTR(rc) : dmabuf;
}

static struct dma_buf *habmem_get_dma_buf_from_uva(unsigned long address,
		int page_count)
{
	struct page **pages = NULL;
	int i, ret = 0;
	struct dma_buf *dmabuf = NULL;
	struct pages_list *pglist = NULL;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	pages = vmalloc((page_count * sizeof(struct page *)));
	if (!pages) {
		ret = -ENOMEM;
		goto err;
	}

	pglist = kzalloc(sizeof(*pglist), GFP_KERNEL);
	if (!pglist) {
		ret = -ENOMEM;
		goto err;
	}

	mmap_read_lock(current->mm);

	ret = get_user_pages(address, page_count, 0, pages, NULL);

	mmap_read_unlock(current->mm);

	if (ret <= 0) {
		ret = -EINVAL;
		pr_err("get %d user pages failed %d\n",
			page_count, ret);
		goto err;
	}

	pglist->pages = pages;
	pglist->npages = page_count;
	pglist->type = HAB_PAGE_LIST_EXPORT;

	kref_init(&pglist->refcount);

	exp_info.ops = &dma_buf_ops;
	exp_info.size = pglist->npages << PAGE_SHIFT;
	exp_info.flags = O_RDWR;
	exp_info.priv = pglist;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		for (i = 0; i < page_count; i++)
			put_page(pages[i]);

		pr_err("export to dmabuf failed\n");
		ret = PTR_ERR(dmabuf);
		goto err;
	}
	return dmabuf;

err:
	vfree(pages);
	kfree(pglist);
	return ERR_PTR(ret);
}

static int habmem_compress_pfns(
		struct export_desc_super *exp_super,
		struct compressed_pfns *pfns,
		uint32_t *data_size)
{
	int ret = 0;
	struct exp_platform_data *platform_data =
		(struct exp_platform_data *) exp_super->platform_data;
	struct dma_buf *dmabuf =
		(struct dma_buf *) platform_data->dmabuf;
	int page_count = exp_super->exp.payload_count;
	struct pages_list *pglist = NULL;
	struct page **pages = NULL;
	int i = 0, j = 0;
	int region_size = 1;
	struct scatterlist *s = NULL;
	struct sg_table *sg_table = NULL;
	struct dma_buf_attachment *attach = NULL;
	struct page *page = NULL, *pre_page = NULL;
	unsigned long page_offset;
	uint32_t spage_size = 0;

	if (IS_ERR_OR_NULL(dmabuf) || !pfns || !data_size)
		return -EINVAL;

	pr_debug("page_count %d\n", page_count);

	/* DMA buffer from fd */
	if (dmabuf->ops != &dma_buf_ops) {
		attach = dma_buf_attach(dmabuf, hab_driver.dev);
		if (IS_ERR_OR_NULL(attach)) {
			pr_err("dma_buf_attach failed %d\n", -EBADF);
			ret = -EBADF;
			goto err;
		}

		sg_table = dma_buf_map_attachment(attach, DMA_TO_DEVICE);
		if (IS_ERR_OR_NULL(sg_table)) {
			pr_err("dma_buf_map_attachment failed %d\n", -EBADF);
			ret = -EBADF;
			goto err;
		}

		/* Restore sg table and attach of dmabuf */
		platform_data->attach = attach;
		platform_data->sg_table = sg_table;
		page_offset = exp_super->offset >> PAGE_SHIFT;

		pr_debug("page_offset %lu\n", page_offset);

		for_each_sg(sg_table->sgl, s, sg_table->nents, i) {
			spage_size = s->length >> PAGE_SHIFT;

			if (page_offset >= spage_size) {
				page_offset -= spage_size;
				continue;
			}

			page = sg_page(s);
			if (j == 0) {
				pfns->first_pfn = page_to_pfn(nth_page(page,
							page_offset));
			} else {
				pfns->region[j-1].space =
					page_to_pfn(nth_page(page, 0)) -
					page_to_pfn(pre_page) - 1;
				pr_debug("j %d, space %d, ppfn %lu, pfn %lu\n",
					j, pfns->region[j-1].space,
					page_to_pfn(pre_page),
					page_to_pfn(nth_page(page, 0)));
			}

			pfns->region[j].size = spage_size - page_offset;
			if (pfns->region[j].size >= page_count) {
				pfns->region[j].size = page_count;
				pfns->region[j].space = 0;
				break;
			}

			page_count -= pfns->region[j].size;
			pre_page = nth_page(page, pfns->region[j].size - 1);
			page_offset = 0;
			j++;
		}
		pfns->nregions = j+1;
	} else {
		pglist = dmabuf->priv;
		pages = pglist->pages;

		pfns->first_pfn = page_to_pfn(pages[0]);
		for (i = 1; i < page_count; i++) {
			if ((page_to_pfn(pages[i]) - 1) ==
					page_to_pfn(pages[i-1])) {
				region_size++;
			} else {
				pfns->region[j].size = region_size;
				pfns->region[j].space =
					page_to_pfn(pages[i]) -
					page_to_pfn(pages[i-1]) - 1;
				j++;
				region_size = 1;
			}
		}
		pfns->region[j].size = region_size;
		pfns->region[j].space = 0;
		pfns->nregions = j+1;
	}

	*data_size = sizeof(struct compressed_pfns) +
		sizeof(struct region) * pfns->nregions;

	pr_debug("first_pfn %lu, nregions %d, data_size %u\n",
			pfns->first_pfn, pfns->nregions, *data_size);
	return 0;
err:
	if (!IS_ERR_OR_NULL(attach)) {
		if (!IS_ERR_OR_NULL(sg_table))
			dma_buf_unmap_attachment(attach,
					sg_table,
					DMA_TO_DEVICE);
		dma_buf_detach(dmabuf, attach);
	}

	return ret;
}

static int habmem_add_export_compress(struct virtual_channel *vchan,
		unsigned long offset,
		int page_count,
		void *buf,
		int flags,
		int *payload_size,
		int *export_id)
{
	int ret = 0;
	struct export_desc *exp = NULL;
	struct export_desc_super *exp_super = NULL;
	struct exp_platform_data *platform_data = NULL;
	struct compressed_pfns *pfns = NULL;
	uint32_t sizebytes = sizeof(*exp_super) +
				sizeof(struct compressed_pfns) +
				page_count * sizeof(struct region);

	pr_debug("exp_desc %zu, comp_pfns %zu, region %zu, page_count %d\n",
		sizeof(struct export_desc),
		sizeof(struct compressed_pfns),
		sizeof(struct region), page_count);

	exp_super = habmem_add_export(vchan,
			sizebytes,
			flags);
	if (IS_ERR_OR_NULL(exp_super)) {
		ret = -ENOMEM;
		goto err_add_exp;
	}
	exp = &exp_super->exp;
	exp->payload_count = page_count;
	platform_data = kzalloc(
			sizeof(struct exp_platform_data),
			GFP_KERNEL);
	if (!platform_data) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	platform_data->dmabuf = buf;
	exp_super->offset = offset;
	exp_super->platform_data = (void *)platform_data;
	kref_init(&exp_super->refcount);

	pfns = (struct compressed_pfns *)&exp->payload[0];
	ret = habmem_compress_pfns(exp_super, pfns, payload_size);
	if (ret) {
		pr_err("hab compressed pfns failed %d\n", ret);
		*payload_size = 0;
		goto err_compress_pfns;
	}

	*export_id = exp->export_id;
	return 0;

err_compress_pfns:
	kfree(platform_data);
err_alloc:
	spin_lock(&vchan->pchan->expid_lock);
	idr_remove(&vchan->pchan->expid_idr, exp->export_id);
	spin_unlock(&vchan->pchan->expid_lock);
	vfree(exp_super);
err_add_exp:
	dma_buf_put((struct dma_buf *)buf);
	return ret;
}

/*
 * exporter - grant & revoke
 * degenerate sharabled page list based on CPU friendly virtual "address".
 * The result as an array is stored in ppdata to return to caller
 * page size 4KB is assumed
 */
int habmem_hyp_grant_user(struct virtual_channel *vchan,
		unsigned long address,
		int page_count,
		int flags,
		int remotedom,
		int *compressed,
		int *payload_size,
		int *export_id)
{
	int ret = 0;
	struct dma_buf *dmabuf = NULL;
	unsigned long off = 0;

	if (HABMM_EXP_MEM_TYPE_DMA & flags)
		dmabuf = habmem_get_dma_buf_from_va(address,
					page_count, &off);
	else if (HABMM_EXPIMP_FLAGS_FD & flags)
		dmabuf = dma_buf_get(address);
	else
		dmabuf = habmem_get_dma_buf_from_uva(address, page_count);

	if (IS_ERR_OR_NULL(dmabuf))
		return -EINVAL;

	ret = habmem_add_export_compress(vchan,
			off,
			page_count,
			dmabuf,
			flags,
			payload_size,
			export_id);

	return ret;
}
/*
 * exporter - grant & revoke
 * generate shareable page list based on CPU friendly virtual "address".
 * The result as an array is stored in ppdata to return to caller
 * page size 4KB is assumed
 */
int habmem_hyp_grant(struct virtual_channel *vchan,
		unsigned long address,
		int page_count,
		int flags,
		int remotedom,
		int *compressed,
		int *payload_size,
		int *export_id)
{
	int ret = 0;
	void *kva = (void *)(uintptr_t)address;
	int is_vmalloc = is_vmalloc_addr(kva);
	struct page **pages = NULL;
	int i;
	struct dma_buf *dmabuf = NULL;
	struct pages_list *pglist = NULL;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	if (HABMM_EXPIMP_FLAGS_DMABUF & flags) {
		dmabuf = (struct dma_buf *)address;
		if (dmabuf)
			get_dma_buf(dmabuf);
	} else if (HABMM_EXPIMP_FLAGS_FD & flags)
		dmabuf = dma_buf_get(address);
	else { /*Input is kva;*/
		pages = vmalloc((page_count *
				sizeof(struct page *)));
		if (!pages) {
			ret = -ENOMEM;
			goto err;
		}

		pglist = kzalloc(sizeof(*pglist), GFP_KERNEL);
		if (!pglist) {
			ret = -ENOMEM;
			goto err;
		}

		pglist->pages = pages;
		pglist->npages = page_count;
		pglist->type = HAB_PAGE_LIST_EXPORT;
		pglist->pchan = vchan->pchan;
		pglist->vcid = vchan->id;

		kref_init(&pglist->refcount);

		for (i = 0; i < page_count; i++) {
			kva = (void *)(uintptr_t)(address + i*PAGE_SIZE);
			if (is_vmalloc)
				pages[i] = vmalloc_to_page(kva);
			else
				pages[i] = virt_to_page(kva);
		}

		exp_info.ops = &dma_buf_ops;
		exp_info.size = pglist->npages << PAGE_SHIFT;
		exp_info.flags = O_RDWR;
		exp_info.priv = pglist;
		dmabuf = dma_buf_export(&exp_info);
	}

	if (IS_ERR_OR_NULL(dmabuf)) {
		pr_err("dmabuf get failed %d\n", PTR_ERR(dmabuf));
		ret = -EINVAL;
		goto err;
	}

	ret = habmem_add_export_compress(vchan,
			0,
			page_count,
			dmabuf,
			flags,
			payload_size,
			export_id);

	return ret;
err:
	vfree(pages);
	kfree(pglist);
	return ret;
}

int habmem_exp_release(struct export_desc_super *exp_super)
{
	struct exp_platform_data *platform_data =
		(struct exp_platform_data *)exp_super->platform_data;
	struct dma_buf *dmabuf =
		(struct dma_buf *) platform_data->dmabuf;
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sg_table = NULL;

	if (!IS_ERR_OR_NULL(dmabuf)) {
		attach = (struct dma_buf_attachment *) platform_data->attach;
		if (!IS_ERR_OR_NULL(attach)) {
			sg_table = (struct sg_table *) platform_data->sg_table;
			if (!IS_ERR_OR_NULL(sg_table))
				dma_buf_unmap_attachment(attach,
						sg_table,
						DMA_TO_DEVICE);
			dma_buf_detach(dmabuf, attach);
		}
		dma_buf_put(dmabuf);
	} else
		pr_debug("release failed, dmabuf is null!!!\n");

	kfree(platform_data);
	return 0;
}

int habmem_hyp_revoke(void *expdata, uint32_t count)
{
	return 0;
}

void *habmem_imp_hyp_open(void)
{
	struct importer_context *priv = NULL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	return priv;
}

void habmem_imp_hyp_close(void *imp_ctx, int kernel)
{
	struct importer_context *priv = imp_ctx;

	if (!priv)
		return;

	kfree(priv);
}

static struct sg_table *hab_mem_map_dma_buf(
	struct dma_buf_attachment *attachment,
	enum dma_data_direction direction)
{
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct pages_list *pglist = dmabuf->priv;
	struct sg_table *sgt;
	struct scatterlist *sg;
	int i;
	int ret = 0;
	struct page **pages = pglist->pages;

	sgt = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(sgt, pglist->npages, GFP_KERNEL);
	if (ret) {
		kfree(sgt);
		return ERR_PTR(-ENOMEM);
	}

	for_each_sg(sgt->sgl, sg, pglist->npages, i) {
		sg_set_page(sg, pages[i], PAGE_SIZE, 0);
	}

	return sgt;
}


static void hab_mem_unmap_dma_buf(struct dma_buf_attachment *attachment,
	struct sg_table *sgt,
	enum dma_data_direction direction)
{
	sg_free_table(sgt);
	kfree(sgt);
}

static vm_fault_t hab_map_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = NULL;
	struct pages_list *pglist = NULL;
	unsigned long offset, fault_offset;
	int page_idx;

	if (vma == NULL)
		return VM_FAULT_SIGBUS;

	offset = vma->vm_pgoff << PAGE_SHIFT;

	/* PHY address */
	fault_offset =
		(unsigned long)vmf->address - vma->vm_start + offset;
	page_idx = fault_offset>>PAGE_SHIFT;

	pglist  = vma->vm_private_data;

	if (page_idx < 0 || page_idx >= pglist->npages) {
		pr_err("Out of page array! page_idx %d, pg cnt %ld\n",
			page_idx, pglist->npages);
		return VM_FAULT_SIGBUS;
	}

	page = pglist->pages[page_idx];
	get_page(page);
	vmf->page = page;
	return 0;
}

static void hab_map_open(struct vm_area_struct *vma)
{
	struct pages_list *pglist =
	    (struct pages_list *)vma->vm_private_data;

	pages_list_get(pglist);
}

static void hab_map_close(struct vm_area_struct *vma)
{
	struct pages_list *pglist =
	    (struct pages_list *)vma->vm_private_data;

	pages_list_put(pglist);
	vma->vm_private_data = NULL;
}

static const struct vm_operations_struct habmem_vm_ops = {
	.fault = hab_map_fault,
	.open = hab_map_open,
	.close = hab_map_close,
};

static vm_fault_t hab_buffer_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct pages_list *pglist = vma->vm_private_data;
	pgoff_t page_offset;
	int ret;

	page_offset = ((unsigned long)vmf->address - vma->vm_start) >>
		PAGE_SHIFT;

	if (page_offset > pglist->npages)
		return VM_FAULT_SIGBUS;

	ret = vm_insert_page(vma, (unsigned long)vmf->address,
			     pglist->pages[page_offset]);

	switch (ret) {
	case 0:
	case -EBUSY:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	case -EFAULT:
	case -EINVAL:
		return VM_FAULT_SIGBUS;
	default:
		WARN_ON(1);
		return VM_FAULT_SIGBUS;
	}
}

static void hab_buffer_open(struct vm_area_struct *vma)
{
}

static void hab_buffer_close(struct vm_area_struct *vma)
{
}

static const struct vm_operations_struct hab_buffer_vm_ops = {
	.fault = hab_buffer_fault,
	.open = hab_buffer_open,
	.close = hab_buffer_close,
};

static int hab_mem_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct pages_list *pglist = dmabuf->priv;
	uint32_t obj_size = pglist->npages << PAGE_SHIFT;

	if (vma == NULL)
		return VM_FAULT_SIGBUS;

	/* Check for valid size. */
	if (obj_size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	vm_flags_set(vma, (VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP));
	vma->vm_ops = &hab_buffer_vm_ops;
	vma->vm_private_data = pglist;
	vm_flags_set(vma, VM_MIXEDMAP);

	if (!(pglist->userflags & HABMM_IMPORT_FLAGS_CACHED))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return 0;
}

static void hab_mem_dma_buf_release(struct dma_buf *dmabuf)
{
	struct pages_list *pglist = dmabuf->priv;

	pages_list_put(pglist);
}

static int hab_mem_dma_buf_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct pages_list *pglist = dmabuf->priv;

	if (!pglist->vmapping) {
		pglist->vmapping = vmap(pglist->pages,
			    pglist->npages,
			    VM_IOREMAP,
			    pgprot_writecombine(PAGE_KERNEL));
		if (!pglist->vmapping)
			return -ENOMEM;
	}
	iosys_map_set_vaddr(map, pglist->vmapping);
	return 0;
}

static void hab_mem_dma_buf_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct pages_list *pglist = dmabuf->priv;

	/* sanity check */
	if (map->vaddr != pglist->vmapping)
		pr_warn("vunmap pass-in %pK != at-hand %pK\n",
				map->vaddr, pglist->vmapping);

	if (pglist->vmapping) {
		vunmap(pglist->vmapping);
		pglist->vmapping = NULL;
	}
}

static struct dma_buf_ops dma_buf_ops = {
	.cache_sgt_mapping = true,
	.map_dma_buf = hab_mem_map_dma_buf,
	.unmap_dma_buf = hab_mem_unmap_dma_buf,
	.mmap = hab_mem_mmap,
	.release = hab_mem_dma_buf_release,
	.vmap = hab_mem_dma_buf_vmap,
	.vunmap = hab_mem_dma_buf_vunmap,
};

static struct dma_buf *habmem_import_to_dma_buf(
	struct physical_channel *pchan,
	struct export_desc *exp,
	uint32_t userflags)
{
	struct pages_list *pglist = NULL;
	struct dma_buf *dmabuf = NULL;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	pglist = pages_list_lookup(exp->export_id, pchan, true);
	if (pglist)
		goto buffer_ready;

	pglist = pages_list_create(exp, userflags);
	if (IS_ERR(pglist))
		return (void *)pglist;

	pages_list_add(pglist);
	pglist->type = HAB_PAGE_LIST_IMPORT;

buffer_ready:
	exp_info.ops = &dma_buf_ops;
	exp_info.size = pglist->npages << PAGE_SHIFT;
	exp_info.flags = O_RDWR;
	exp_info.priv = pglist;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		pr_err("export to dmabuf failed, exp %d, pchan %s\n",
			exp->export_id, pchan->name);
		pages_list_put(pglist);
	}

	return dmabuf;
}

int habmem_imp_hyp_map(void *imp_ctx, struct hab_import *param,
		struct export_desc *exp, int kernel)
{
	int fd = -1;
	struct dma_buf *dma_buf = NULL;
	struct physical_channel *pchan = exp->pchan;

	dma_buf = habmem_import_to_dma_buf(pchan, exp, param->flags);
	if (IS_ERR_OR_NULL(dma_buf))
		return -EINVAL;

	if (kernel) {
		param->kva = (uint64_t)dma_buf;
	} else {
		fd = dma_buf_fd(dma_buf, O_CLOEXEC);
		if (fd < 0) {
			pr_err("dma buf to fd failed\n");
			dma_buf_put(dma_buf);
			return -EINVAL;
		}
		param->kva = (uint64_t)fd;
	}
	return 0;
}

int habmm_imp_hyp_unmap(void *imp_ctx, struct export_desc *exp, int kernel)
{
	/* dma_buf is the only supported format in khab */
	if (kernel)
		dma_buf_put((struct dma_buf *)exp->kva);
	return 0;
}

int habmem_imp_hyp_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EFAULT;
}

int habmm_imp_hyp_map_check(void *imp_ctx, struct export_desc *exp)
{
	struct pages_list *pglist = NULL;
	int found = 0;

	pglist = pages_list_lookup(exp->export_id, exp->pchan, false);
	if (pglist)
		found = 1;

	return found;
}

MODULE_IMPORT_NS(DMA_BUF);
