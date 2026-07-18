/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2020 Intel Corporation. All rights reserved.
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

#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/count_zeros.h>
#include <rdma/ib_umem_odp.h>

#include "uverbs.h"

#define RESCHED_LOOP_CNT_THRESHOLD 0x1000

static void __ib_umem_release(struct ib_device *dev, struct ib_umem *umem, int dirty)
{
	bool make_dirty = umem->writable && dirty;
	struct scatterlist *sg;
	unsigned int i;

	if (dirty)
		ib_dma_unmap_sgtable_attrs(dev, &umem->sgt_append.sgt,
					   DMA_BIDIRECTIONAL, umem->dma_attrs);

	for_each_sgtable_sg(&umem->sgt_append.sgt, sg, i) {
		unpin_user_page_range_dirty_lock(sg_page(sg),
			DIV_ROUND_UP(sg->length, PAGE_SIZE), make_dirty);

		if (i && !(i % RESCHED_LOOP_CNT_THRESHOLD))
			cond_resched();
	}

	sg_free_append_table(&umem->sgt_append);
}

/**
 * ib_umem_find_best_pgsz - Find best HW page size to use for this MR
 *
 * @umem: umem struct
 * @pgsz_bitmap: bitmap of HW supported page sizes
 * @virt: IOVA
 *
 * This helper is intended for HW that support multiple page
 * sizes but can do only a single page size in an MR.
 *
 * Returns 0 if the umem requires page sizes not supported by
 * the driver to be mapped. Drivers always supporting PAGE_SIZE
 * or smaller will never see a 0 result.
 */
unsigned long ib_umem_find_best_pgsz(struct ib_umem *umem,
				     unsigned long pgsz_bitmap,
				     u64 virt)
{
	unsigned long curr_len = 0;
	dma_addr_t curr_base = ~0;
	unsigned long pgoff;
	struct scatterlist *sg;
	unsigned long mask = 0;
	unsigned int bits;
	dma_addr_t end;
	u64 last_va;
	u64 va;
	int i;

	umem->iova = va = virt;

	if (umem->is_odp) {
		unsigned int page_size = BIT(to_ib_umem_odp(umem)->page_shift);

		/* ODP must always be self consistent. */
		if (!(pgsz_bitmap & page_size))
			return 0;
		return page_size;
	}

	/* The best result is the smallest page size that results in the minimum
	 * number of required pages. Compute the largest page size that could
	 * work based on VA address bits that don't change.
	 */
	if (check_add_overflow(umem->length - 1, virt, &last_va))
		return 0;
	bits = bits_per(virt ^ last_va);
	if (bits < BITS_PER_LONG)
		mask = pgsz_bitmap & GENMASK(BITS_PER_LONG - 1, bits);

	/* offset into first SGL */
	pgoff = umem->address & ~PAGE_MASK;

	for_each_sgtable_dma_sg(&umem->sgt_append.sgt, sg, i) {
		/* If the current entry is physically contiguous with the previous
		 * one, no need to take its start addresses into consideration.
		 */
		if (check_add_overflow(curr_base, curr_len, &end) ||
		    end != sg_dma_address(sg)) {

			curr_base = sg_dma_address(sg);
			curr_len = 0;

			/* Reduce max page size if VA/PA bits differ */
			mask |= (curr_base + pgoff) ^ va;

			/* The alignment of any VA matching a discontinuity point
			* in the physical memory sets the maximum possible page
			* size as this must be a starting point of a new page that
			* needs to be aligned.
			*/
			if (i != 0)
				mask |= va;
		}

		curr_len += sg_dma_len(sg);
		va += sg_dma_len(sg) - pgoff;

		pgoff = 0;
	}

	/* The mask accumulates 1's in each position where the VA and physical
	 * address differ, thus the length of trailing 0 is the largest page
	 * size that can pass the VA through to the physical.
	 */
	if (mask)
		pgsz_bitmap &= GENMASK(count_trailing_zeros(mask), 0);
	return pgsz_bitmap ? rounddown_pow_of_two(pgsz_bitmap) : 0;
}
EXPORT_SYMBOL(ib_umem_find_best_pgsz);

static struct ib_umem *__ib_umem_get_va(struct ib_device *device,
					unsigned long addr, size_t size,
					int access)
{
	struct ib_umem *umem;
	struct page **page_list;
	unsigned long lock_limit;
	unsigned long new_pinned;
	unsigned long cur_base;
	struct mm_struct *mm;
	unsigned long npages;
	int pinned, ret;
	unsigned int gup_flags = FOLL_LONGTERM;

	if (device->cc_dma_bounce)
		return ERR_PTR(-EOPNOTSUPP);

	/*
	 * If the combination of the addr and size requested for this memory
	 * region causes an integer overflow, return error.
	 */
	if (((addr + size) < addr) ||
	    PAGE_ALIGN(addr + size) < (addr + size))
		return ERR_PTR(-EINVAL);

	if (!can_do_mlock())
		return ERR_PTR(-EPERM);

	if (access & IB_ACCESS_ON_DEMAND)
		return ERR_PTR(-EOPNOTSUPP);

	umem = kzalloc_obj(*umem);
	if (!umem)
		return ERR_PTR(-ENOMEM);
	umem->ibdev      = device;
	umem->length     = size;
	umem->address    = addr;
	/*
	 * Drivers should call ib_umem_find_best_pgsz() to set the iova
	 * correctly.
	 */
	umem->iova = addr;
	umem->writable   = ib_access_writable(access);
	umem->owning_mm = mm = current->mm;
	umem->dma_attrs = DMA_ATTR_REQUIRE_COHERENT;
	if (access & IB_ACCESS_RELAXED_ORDERING)
		umem->dma_attrs |= DMA_ATTR_WEAK_ORDERING;

	mmgrab(mm);

	page_list = (struct page **) __get_free_page(GFP_KERNEL);
	if (!page_list) {
		ret = -ENOMEM;
		goto umem_kfree;
	}

	npages = ib_umem_num_pages(umem);
	if (npages == 0 || npages > UINT_MAX) {
		ret = -EINVAL;
		goto out;
	}

	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	new_pinned = atomic64_add_return(npages, &mm->pinned_vm);
	if (new_pinned > lock_limit && !capable(CAP_IPC_LOCK)) {
		atomic64_sub(npages, &mm->pinned_vm);
		ret = -ENOMEM;
		goto out;
	}

	cur_base = addr & PAGE_MASK;

	if (umem->writable)
		gup_flags |= FOLL_WRITE;

	while (npages) {
		cond_resched();
		pinned = pin_user_pages_fast(cur_base,
					  min_t(unsigned long, npages,
						PAGE_SIZE /
						sizeof(struct page *)),
					  gup_flags, page_list);
		if (pinned < 0) {
			ret = pinned;
			goto umem_release;
		}

		cur_base += pinned * PAGE_SIZE;
		npages -= pinned;
		ret = sg_alloc_append_table_from_pages(
			&umem->sgt_append, page_list, pinned, 0,
			pinned << PAGE_SHIFT, ib_dma_max_seg_size(device),
			npages, GFP_KERNEL);
		if (ret) {
			unpin_user_pages_dirty_lock(page_list, pinned, 0);
			goto umem_release;
		}
	}

	ret = ib_dma_map_sgtable_attrs(device, &umem->sgt_append.sgt,
				       DMA_BIDIRECTIONAL, umem->dma_attrs);
	if (ret)
		goto umem_release;
	goto out;

umem_release:
	__ib_umem_release(device, umem, 0);
	atomic64_sub(ib_umem_num_pages(umem), &mm->pinned_vm);
out:
	free_page((unsigned long) page_list);
umem_kfree:
	if (ret) {
		mmdrop(umem->owning_mm);
		kfree(umem);
	}
	return ret ? ERR_PTR(ret) : umem;
}

/**
 * ib_umem_get_desc - Pin a umem from a buffer descriptor.
 * @device: IB device.
 * @desc:   buffer descriptor (VA or DMABUF).
 * @access: IB access flags.
 *
 * Return: caller-owned umem on success, ERR_PTR(...) on error.
 */
struct ib_umem *ib_umem_get_desc(struct ib_device *device,
				 const struct ib_uverbs_buffer_desc *desc,
				 int access)
{
	struct ib_umem_dmabuf *umem_dmabuf;

	if (desc->flags & ~IB_UVERBS_BUFFER_DESC_FLAGS_KNOWN_MASK)
		return ERR_PTR(-EINVAL);

	if (overflows_type(desc->addr, unsigned long) ||
	    overflows_type(desc->length, size_t))
		return ERR_PTR(-EOVERFLOW);

	switch (desc->type) {
	case IB_UVERBS_BUFFER_TYPE_DMABUF:
		umem_dmabuf = ib_umem_dmabuf_get_pinned(device, desc->addr,
							desc->length, desc->fd,
							access);
		if (IS_ERR(umem_dmabuf))
			return ERR_CAST(umem_dmabuf);
		return &umem_dmabuf->umem;
	case IB_UVERBS_BUFFER_TYPE_VA:
		return __ib_umem_get_va(device, desc->addr, desc->length,
					access);
	default:
		return ERR_PTR(-EINVAL);
	}
}
EXPORT_SYMBOL(ib_umem_get_desc);

/*
 * Per-command legacy buffer-desc filler.
 * Returns 0 on success (desc filled), -ENODATA if no legacy attrs apply,
 * negative errno on validation failure.
 */
typedef int (*ib_umem_buf_desc_filler_t)(const struct uverbs_attr_bundle *attrs,
					 struct ib_uverbs_buffer_desc *desc);

/*
 * ib_umem_resolve_desc - Resolve a buffer descriptor from a per-command UMEM
 *                        attribute and/or a legacy attr filler.
 *
 * Return:
 *    0       @desc filled.
 *   -ENOENT  no source produced a buffer.
 *   -EINVAL  both the UMEM attribute and the legacy filler produced a buffer.
 *   -errno   propagated from attr read / filler validation.
 */
static int ib_umem_resolve_desc(const struct uverbs_attr_bundle *attrs,
				u16 attr_id,
				ib_umem_buf_desc_filler_t legacy_filler,
				struct ib_uverbs_buffer_desc *desc)
{
	bool have_desc = false;
	int ret;

	if (!attrs)
		return -ENOENT;

	ret = uverbs_get_buffer_desc(attrs, attr_id, desc);
	if (!ret)
		have_desc = true;
	else if (ret != -ENOENT)
		return ret;

	if (legacy_filler) {
		struct ib_uverbs_buffer_desc legacy_desc = {};

		ret = legacy_filler(attrs, &legacy_desc);
		if (!ret) {
			if (have_desc)
				return -EINVAL;
			*desc = legacy_desc;
			have_desc = true;
		} else if (ret != -ENODATA) {
			return ret;
		}
	}

	return have_desc ? 0 : -ENOENT;
}

/*
 * ib_umem_get_desc_check - Pin a umem from @desc and verify it meets
 *                          @min_size.
 */
static struct ib_umem *
ib_umem_get_desc_check(struct ib_device *device,
		       const struct ib_uverbs_buffer_desc *desc,
		       size_t min_size, int access)
{
	struct ib_umem *umem;

	umem = ib_umem_get_desc(device, desc, access);
	if (IS_ERR(umem))
		return umem;
	if (umem->length < min_size) {
		ib_umem_release(umem);
		return ERR_PTR(-EINVAL);
	}
	return umem;
}

/*
 * ib_umem_get_from_attrs - Pin a umem from a per-command UMEM attribute
 *                          and/or a legacy attr filler.
 *
 * Return: caller-owned umem on success; NULL when no source supplied a
 * buffer; ERR_PTR(...) on error.
 */
static struct ib_umem *
ib_umem_get_from_attrs(struct ib_device *device,
		       const struct uverbs_attr_bundle *attrs,
		       u16 attr_id, ib_umem_buf_desc_filler_t legacy_filler,
		       size_t size, int access)
{
	struct ib_uverbs_buffer_desc desc = {};
	int ret;

	ret = ib_umem_resolve_desc(attrs, attr_id, legacy_filler, &desc);
	if (ret == -ENOENT)
		return NULL;
	if (ret)
		return ERR_PTR(ret);
	return ib_umem_get_desc_check(device, &desc, size, access);
}

/*
 * ib_umem_get_from_attrs_or_va - Pin a umem from a per-command UMEM
 *                                attribute and/or a legacy attr filler,
 *                                falling back to a UHW VA when no source
 *                                matched.
 *
 * @size is always consumed: it is the length to pin on the VA fallback
 * path AND the post-pin minimum-length check on the attr / legacy paths.
 * Callers must always pass a meaningful, validated value.
 *
 * Return: caller-owned umem on success, ERR_PTR(...) on error.
 */
static struct ib_umem *
ib_umem_get_from_attrs_or_va(struct ib_device *device,
			     const struct uverbs_attr_bundle *attrs,
			     u16 attr_id,
			     ib_umem_buf_desc_filler_t legacy_filler,
			     u64 addr, size_t size, int access)
{
	struct ib_uverbs_buffer_desc desc = {};
	int ret;

	ret = ib_umem_resolve_desc(attrs, attr_id, legacy_filler, &desc);
	if (ret == -ENOENT)
		desc = (struct ib_uverbs_buffer_desc){
			.type	= IB_UVERBS_BUFFER_TYPE_VA,
			.addr	= addr,
			.length	= size,
		};
	else if (ret)
		return ERR_PTR(ret);
	return ib_umem_get_desc_check(device, &desc, size, access);
}

/**
 * ib_umem_get_attr - Pin a umem from a per-command UMEM attribute.
 * @device:  IB device.
 * @attrs:   uverbs attribute bundle (may be NULL).
 * @attr_id: per-command UMEM attribute id.
 * @size:    minimum required umem length.
 * @access:  IB access flags.
 *
 * Return: caller-owned umem on success; NULL when no source supplied
 * a buffer; ERR_PTR(...) on error.
 */
struct ib_umem *ib_umem_get_attr(struct ib_device *device,
				 const struct uverbs_attr_bundle *attrs,
				 u16 attr_id, size_t size, int access)
{
	return ib_umem_get_from_attrs(device, attrs, attr_id, NULL, size,
				      access);
}
EXPORT_SYMBOL(ib_umem_get_attr);

/**
 * ib_umem_get_attr_or_va - Pin a umem from a per-command UMEM attribute,
 *                          falling back to a UHW VA.
 * @device:  IB device.
 * @attrs:   uverbs attribute bundle (may be NULL).
 * @attr_id: per-command UMEM attribute id.
 * @addr:    UHW user VA used when no per-command attribute matched.
 * @size:    on the attr / legacy paths, the minimum required umem length
 *           validated post-pin; on the VA fallback path, the length to pin.
 * @access:  IB access flags.
 *
 * Like ib_umem_get_attr(), but pins @addr/@size when no per-command
 * UMEM attribute is supplied.
 *
 * IMPORTANT: @size is always consumed. On the attr / legacy paths it is
 * used as the post-pin minimum-length check; on the VA fallback path it
 * is the length to pin. Callers MUST pass a meaningful, validated value
 * even when they expect an attribute-supplied buffer to be used.
 *
 * Every in-tree caller passes the same value for the two roles of @size
 * because no driver today distinguishes a user-passed buffer length from
 * a driver-computed minimum. Drivers that currently accept a user-supplied
 * length without cross-checking it against a driver minimum (vmw_pvrdma
 * CQ/QP/SRQ, qedr CQ/QP/SRQ, mana WQ/QP, ionic CQ/QP), once tightened to
 * compute and check a real minimum, will want to introduce a separate
 * helper that passes these as distinct values.
 *
 * Return: caller-owned umem on success, ERR_PTR(...) on error.
 */
struct ib_umem *ib_umem_get_attr_or_va(struct ib_device *device,
				       const struct uverbs_attr_bundle *attrs,
				       u16 attr_id, u64 addr, size_t size,
				       int access)
{
	return ib_umem_get_from_attrs_or_va(device, attrs, attr_id, NULL, addr,
					    size, access);
}
EXPORT_SYMBOL(ib_umem_get_attr_or_va);

static int uverbs_create_cq_get_buffer_desc(const struct uverbs_attr_bundle *attrs,
					    struct ib_uverbs_buffer_desc *desc)
{
	struct ib_device *ib_dev = attrs->context->device;
	int ret;

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_VA)) {
		ret = uverbs_copy_from(&desc->addr, attrs,
				       UVERBS_ATTR_CREATE_CQ_BUFFER_VA);
		if (ret)
			return ret;
		ret = uverbs_copy_from(&desc->length, attrs,
				       UVERBS_ATTR_CREATE_CQ_BUFFER_LENGTH);
		if (ret)
			return ret;
		if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_FD) ||
		    uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_OFFSET) ||
		    !ib_dev->ops.create_user_cq)
			return -EINVAL;
		desc->type = IB_UVERBS_BUFFER_TYPE_VA;
		return 0;
	}

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_FD)) {
		ret = uverbs_get_raw_fd(&desc->fd, attrs,
					UVERBS_ATTR_CREATE_CQ_BUFFER_FD);
		if (ret)
			return ret;

		ret = uverbs_copy_from(&desc->addr, attrs,
				       UVERBS_ATTR_CREATE_CQ_BUFFER_OFFSET);
		if (ret)
			return ret;
		ret = uverbs_copy_from(&desc->length, attrs,
				       UVERBS_ATTR_CREATE_CQ_BUFFER_LENGTH);
		if (ret)
			return ret;
		if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_VA) ||
		    !ib_dev->ops.create_user_cq)
			return -EINVAL;
		desc->type = IB_UVERBS_BUFFER_TYPE_DMABUF;
		return 0;
	}

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_OFFSET) ||
	    uverbs_attr_is_valid(attrs, UVERBS_ATTR_CREATE_CQ_BUFFER_LENGTH))
		return -EINVAL;
	return -ENODATA;
}

/**
 * ib_umem_get_cq_buf - Pin a CQ buffer umem from per-command attributes.
 * @device:  IB device.
 * @attrs:   uverbs attribute bundle (may be NULL).
 * @size:    minimum required CQ buffer length.
 * @access:  IB access flags.
 *
 * Resolves the CQ buffer from the new UMEM attribute or the legacy
 * CQ buffer attributes. There is no UHW VA fallback, so the caller
 * must arrange its own backing (typically an in-kernel allocation)
 * when no source is available.
 *
 * Return: caller-owned umem on success; NULL when no source supplied
 * a buffer; ERR_PTR(...) on error.
 */
struct ib_umem *ib_umem_get_cq_buf(struct ib_device *device,
				   const struct uverbs_attr_bundle *attrs,
				   size_t size, int access)
{
	return ib_umem_get_from_attrs(device, attrs,
				      UVERBS_ATTR_CREATE_CQ_BUF_UMEM,
				      uverbs_create_cq_get_buffer_desc,
				      size, access);
}
EXPORT_SYMBOL(ib_umem_get_cq_buf);

/**
 * ib_umem_get_cq_buf_or_va - Pin a CQ buffer umem with UHW VA fallback.
 * @device:  IB device.
 * @attrs:   uverbs attribute bundle (may be NULL).
 * @addr:    UHW user VA used when no per-command attribute matched.
 * @size:    on the attr / legacy paths, the minimum required umem length
 *           validated post-pin; on the VA fallback path, the length to pin.
 * @access:  IB access flags.
 *
 * Like ib_umem_get_cq_buf(), but pins @addr/@size when neither the
 * UMEM attribute nor the legacy CQ buffer attributes are supplied.
 *
 * See ib_umem_get_attr_or_va() for the note on @size's dual role and
 * the migration path for drivers that would distinguish a user-supplied
 * length from a driver-computed minimum.
 *
 * Return: caller-owned umem on success, ERR_PTR(...) on error.
 */
struct ib_umem *ib_umem_get_cq_buf_or_va(struct ib_device *device,
					 const struct uverbs_attr_bundle *attrs,
					 u64 addr, size_t size, int access)
{
	return ib_umem_get_from_attrs_or_va(device, attrs,
					    UVERBS_ATTR_CREATE_CQ_BUF_UMEM,
					    uverbs_create_cq_get_buffer_desc,
					    addr, size, access);
}
EXPORT_SYMBOL(ib_umem_get_cq_buf_or_va);

/**
 * ib_umem_release - release pinned memory
 * @umem: umem struct to release
 */
void ib_umem_release(struct ib_umem *umem)
{
	if (IS_ERR_OR_NULL(umem))
		return;
	if (umem->is_dmabuf)
		return ib_umem_dmabuf_release(to_ib_umem_dmabuf(umem));
	if (umem->is_odp)
		return ib_umem_odp_release(to_ib_umem_odp(umem));

	__ib_umem_release(umem->ibdev, umem, 1);

	atomic64_sub(ib_umem_num_pages(umem), &umem->owning_mm->pinned_vm);
	mmdrop(umem->owning_mm);
	kfree(umem);
}
EXPORT_SYMBOL(ib_umem_release);

/*
 * Copy from the given ib_umem's pages to the given buffer.
 *
 * umem - the umem to copy from
 * offset - offset to start copying from
 * dst - destination buffer
 * length - buffer length
 *
 * Returns 0 on success, or an error code.
 */
int ib_umem_copy_from(void *dst, struct ib_umem *umem, size_t offset,
		      size_t length)
{
	size_t end = offset + length;
	int ret;

	if (offset > umem->length || length > umem->length - offset) {
		pr_err("%s not in range. offset: %zd umem length: %zd end: %zd\n",
		       __func__, offset, umem->length, end);
		return -EINVAL;
	}

	ret = sg_pcopy_to_buffer(umem->sgt_append.sgt.sgl,
				 umem->sgt_append.sgt.orig_nents, dst, length,
				 offset + ib_umem_offset(umem));

	if (ret < 0)
		return ret;
	else if (ret != length)
		return -EINVAL;
	else
		return 0;
}
EXPORT_SYMBOL(ib_umem_copy_from);

/*
 * Called during rereg mr if the driver is able to re-use a umem for
 * IB_MR_REREG_ACCESS.
 */
int ib_umem_check_rereg(struct ib_umem *umem, int flags, int new_access_flags)
{
	if (!umem)
		return 0;

	if ((flags & IB_MR_REREG_ACCESS) && !(flags & IB_MR_REREG_TRANS))
		if (ib_access_writable(new_access_flags) && !umem->writable)
			return -EACCES;
	return 0;
}
EXPORT_SYMBOL(ib_umem_check_rereg);
