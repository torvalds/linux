// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2017 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "vmwgfx_drv.h"

#include "vmwgfx_bo.h"
#include <linux/highmem.h>

/*
 * Template that implements find_first_diff() for a generic
 * unsigned integer type. @size and return value are in bytes.
 */
#define VMW_FIND_FIRST_DIFF(_type)			 \
static size_t vmw_find_first_diff_ ## _type		 \
	(const _type * dst, const _type * src, size_t size)\
{							 \
	size_t i;					 \
							 \
	for (i = 0; i < size; i += sizeof(_type)) {	 \
		if (*dst++ != *src++)			 \
			break;				 \
	}						 \
							 \
	return i;					 \
}


/*
 * Template that implements find_last_diff() for a generic
 * unsigned integer type. Pointers point to the item following the
 * *end* of the area to be examined. @size and return value are in
 * bytes.
 */
#define VMW_FIND_LAST_DIFF(_type)					\
static ssize_t vmw_find_last_diff_ ## _type(				\
	const _type * dst, const _type * src, size_t size)		\
{									\
	while (size) {							\
		if (*--dst != *--src)					\
			break;						\
									\
		size -= sizeof(_type);					\
	}								\
	return size;							\
}


/*
 * Instantiate find diff functions for relevant unsigned integer sizes,
 * assuming that wider integers are faster (including aligning) up to the
 * architecture native width, which is assumed to be 32 bit unless
 * CONFIG_64BIT is defined.
 */
VMW_FIND_FIRST_DIFF(u8);
VMW_FIND_LAST_DIFF(u8);

VMW_FIND_FIRST_DIFF(u16);
VMW_FIND_LAST_DIFF(u16);

VMW_FIND_FIRST_DIFF(u32);
VMW_FIND_LAST_DIFF(u32);

#ifdef CONFIG_64BIT
VMW_FIND_FIRST_DIFF(u64);
VMW_FIND_LAST_DIFF(u64);
#endif


/* We use size aligned copies. This computes (addr - align(addr)) */
#define SPILL(_var, _type) ((unsigned long) _var & (sizeof(_type) - 1))


/*
 * Template to compute find_first_diff() for a certain integer type
 * including a head copy for alignment, and adjustment of parameters
 * for tail find or increased resolution find using an unsigned integer find
 * of smaller width. If finding is complete, and resolution is sufficient,
 * the macro executes a return statement. Otherwise it falls through.
 */
#define VMW_TRY_FIND_FIRST_DIFF(_type)					\
do {									\
	unsigned int spill = SPILL(dst, _type);				\
	size_t diff_offs;						\
									\
	if (spill && spill == SPILL(src, _type) &&			\
	    sizeof(_type) - spill <= size) {				\
		spill = sizeof(_type) - spill;				\
		diff_offs = vmw_find_first_diff_u8(dst, src, spill);	\
		if (diff_offs < spill)					\
			return round_down(offset + diff_offs, granularity); \
									\
		dst += spill;						\
		src += spill;						\
		size -= spill;						\
		offset += spill;					\
		spill = 0;						\
	}								\
	if (!spill && !SPILL(src, _type)) {				\
		size_t to_copy = size &	 ~(sizeof(_type) - 1);		\
									\
		diff_offs = vmw_find_first_diff_ ## _type		\
			((_type *) dst, (_type *) src, to_copy);	\
		if (diff_offs >= size || granularity == sizeof(_type))	\
			return (offset + diff_offs);			\
									\
		dst += diff_offs;					\
		src += diff_offs;					\
		size -= diff_offs;					\
		offset += diff_offs;					\
	}								\
} while (0)								\


/**
 * vmw_find_first_diff - find the first difference between dst and src
 *
 * @dst: The destination address
 * @src: The source address
 * @size: Number of bytes to compare
 * @granularity: The granularity needed for the return value in bytes.
 * return: The offset from find start where the first difference was
 * encountered in bytes. If no difference was found, the function returns
 * a value >= @size.
 */
static size_t vmw_find_first_diff(const u8 *dst, const u8 *src, size_t size,
				  size_t granularity)
{
	size_t offset = 0;

	/*
	 * Try finding with large integers if alignment allows, or we can
	 * fix it. Fall through if we need better resolution or alignment
	 * was bad.
	 */
#ifdef CONFIG_64BIT
	VMW_TRY_FIND_FIRST_DIFF(u64);
#endif
	VMW_TRY_FIND_FIRST_DIFF(u32);
	VMW_TRY_FIND_FIRST_DIFF(u16);

	return round_down(offset + vmw_find_first_diff_u8(dst, src, size),
			  granularity);
}


/*
 * Template to compute find_last_diff() for a certain integer type
 * including a tail copy for alignment, and adjustment of parameters
 * for head find or increased resolution find using an unsigned integer find
 * of smaller width. If finding is complete, and resolution is sufficient,
 * the macro executes a return statement. Otherwise it falls through.
 */
#define VMW_TRY_FIND_LAST_DIFF(_type)					\
do {									\
	unsigned int spill = SPILL(dst, _type);				\
	ssize_t location;						\
	ssize_t diff_offs;						\
									\
	if (spill && spill <= size && spill == SPILL(src, _type)) {	\
		diff_offs = vmw_find_last_diff_u8(dst, src, spill);	\
		if (diff_offs) {					\
			location = size - spill + diff_offs - 1;	\
			return round_down(location, granularity);	\
		}							\
									\
		dst -= spill;						\
		src -= spill;						\
		size -= spill;						\
		spill = 0;						\
	}								\
	if (!spill && !SPILL(src, _type)) {				\
		size_t to_copy = round_down(size, sizeof(_type));	\
									\
		diff_offs = vmw_find_last_diff_ ## _type		\
			((_type *) dst, (_type *) src, to_copy);	\
		location = size - to_copy + diff_offs - sizeof(_type);	\
		if (location < 0 || granularity == sizeof(_type))	\
			return location;				\
									\
		dst -= to_copy - diff_offs;				\
		src -= to_copy - diff_offs;				\
		size -= to_copy - diff_offs;				\
	}								\
} while (0)


/**
 * vmw_find_last_diff - find the last difference between dst and src
 *
 * @dst: The destination address
 * @src: The source address
 * @size: Number of bytes to compare
 * @granularity: The granularity needed for the return value in bytes.
 * return: The offset from find start where the last difference was
 * encountered in bytes, or a negative value if no difference was found.
 */
static ssize_t vmw_find_last_diff(const u8 *dst, const u8 *src, size_t size,
				  size_t granularity)
{
	dst += size;
	src += size;

#ifdef CONFIG_64BIT
	VMW_TRY_FIND_LAST_DIFF(u64);
#endif
	VMW_TRY_FIND_LAST_DIFF(u32);
	VMW_TRY_FIND_LAST_DIFF(u16);

	return round_down(vmw_find_last_diff_u8(dst, src, size) - 1,
			  granularity);
}


/**
 * vmw_memcpy - A wrapper around kernel memcpy with allowing to plug it into a
 * struct vmw_diff_cpy.
 *
 * @diff: The struct vmw_diff_cpy closure argument (unused).
 * @dest: The copy destination.
 * @src: The copy source.
 * @n: Number of bytes to copy.
 */
void vmw_memcpy(struct vmw_diff_cpy *diff, u8 *dest, const u8 *src, size_t n)
{
	memcpy(dest, src, n);
}


/**
 * vmw_adjust_rect - Adjust rectangle coordinates for newly found difference
 *
 * @diff: The struct vmw_diff_cpy used to track the modified bounding box.
 * @diff_offs: The offset from @diff->line_offset where the difference was
 * found.
 */
static void vmw_adjust_rect(struct vmw_diff_cpy *diff, size_t diff_offs)
{
	size_t offs = (diff_offs + diff->line_offset) / diff->cpp;
	struct drm_rect *rect = &diff->rect;

	rect->x1 = min_t(int, rect->x1, offs);
	rect->x2 = max_t(int, rect->x2, offs + 1);
	rect->y1 = min_t(int, rect->y1, diff->line);
	rect->y2 = max_t(int, rect->y2, diff->line + 1);
}

/**
 * vmw_diff_memcpy - memcpy that creates a bounding box of modified content.
 *
 * @diff: The struct vmw_diff_cpy used to track the modified bounding box.
 * @dest: The copy destination.
 * @src: The copy source.
 * @n: Number of bytes to copy.
 *
 * In order to correctly track the modified content, the field @diff->line must
 * be pre-loaded with the current line number, the field @diff->line_offset must
 * be pre-loaded with the line offset in bytes where the copy starts, and
 * finally the field @diff->cpp need to be preloaded with the number of bytes
 * per unit in the horizontal direction of the area we're examining.
 * Typically bytes per pixel.
 * This is needed to know the needed granularity of the difference computing
 * operations. A higher cpp generally leads to faster execution at the cost of
 * bounding box width precision.
 */
void vmw_diff_memcpy(struct vmw_diff_cpy *diff, u8 *dest, const u8 *src,
		     size_t n)
{
	ssize_t csize, byte_len;

	if (WARN_ON_ONCE(round_down(n, diff->cpp) != n))
		return;

	/* TODO: Possibly use a single vmw_find_first_diff per line? */
	csize = vmw_find_first_diff(dest, src, n, diff->cpp);
	if (csize < n) {
		vmw_adjust_rect(diff, csize);
		byte_len = diff->cpp;

		/*
		 * Starting from where first difference was found, find
		 * location of last difference, and then copy.
		 */
		diff->line_offset += csize;
		dest += csize;
		src += csize;
		n -= csize;
		csize = vmw_find_last_diff(dest, src, n, diff->cpp);
		if (csize >= 0) {
			byte_len += csize;
			vmw_adjust_rect(diff, csize);
		}
		memcpy(dest, src, byte_len);
	}
	diff->line_offset += n;
}

/**
 * struct vmw_bo_blit_line_data - Convenience argument to vmw_bo_cpu_blit_line
 *
 * @mapped_dst: Already mapped destination page index in @dst_pages.
 * @dst_addr: Kernel virtual address of mapped destination page.
 * @dst_pages: Array of destination bo pages.
 * @dst_num_pages: Number of destination bo pages.
 * @dst_prot: Destination bo page protection.
 * @mapped_src: Already mapped source page index in @dst_pages.
 * @src_addr: Kernel virtual address of mapped source page.
 * @src_pages: Array of source bo pages.
 * @src_num_pages: Number of source bo pages.
 * @src_prot: Source bo page protection.
 * @diff: Struct vmw_diff_cpy, in the end forwarded to the memcpy routine.
 */
struct vmw_bo_blit_line_data {
	u32 mapped_dst;
	u8 *dst_addr;
	struct page **dst_pages;
	u32 dst_num_pages;
	pgprot_t dst_prot;
	u32 mapped_src;
	u8 *src_addr;
	struct page **src_pages;
	u32 src_num_pages;
	pgprot_t src_prot;
	struct vmw_diff_cpy *diff;
};

/**
 * vmw_bo_cpu_blit_line - Blit part of a line from one bo to another.
 *
 * @d: Blit data as described above.
 * @dst_offset: Destination copy start offset from start of bo.
 * @src_offset: Source copy start offset from start of bo.
 * @bytes_to_copy: Number of bytes to copy in this line.
 */
static int vmw_bo_cpu_blit_line(struct vmw_bo_blit_line_data *d,
				u32 dst_offset,
				u32 src_offset,
				u32 bytes_to_copy)
{
	struct vmw_diff_cpy *diff = d->diff;

	while (bytes_to_copy) {
		u32 copy_size = bytes_to_copy;
		u32 dst_page = dst_offset >> PAGE_SHIFT;
		u32 src_page = src_offset >> PAGE_SHIFT;
		u32 dst_page_offset = dst_offset & ~PAGE_MASK;
		u32 src_page_offset = src_offset & ~PAGE_MASK;
		bool unmap_dst = d->dst_addr && dst_page != d->mapped_dst;
		bool unmap_src = d->src_addr && (src_page != d->mapped_src ||
						 unmap_dst);

		copy_size = min_t(u32, copy_size, PAGE_SIZE - dst_page_offset);
		copy_size = min_t(u32, copy_size, PAGE_SIZE - src_page_offset);

		if (unmap_src) {
			kunmap_atomic(d->src_addr);
			d->src_addr = NULL;
		}

		if (unmap_dst) {
			kunmap_atomic(d->dst_addr);
			d->dst_addr = NULL;
		}

		if (!d->dst_addr) {
			if (WARN_ON_ONCE(dst_page >= d->dst_num_pages))
				return -EINVAL;

			d->dst_addr =
				kmap_atomic_prot(d->dst_pages[dst_page],
						 d->dst_prot);
			if (!d->dst_addr)
				return -ENOMEM;

			d->mapped_dst = dst_page;
		}

		if (!d->src_addr) {
			if (WARN_ON_ONCE(src_page >= d->src_num_pages))
				return -EINVAL;

			d->src_addr =
				kmap_atomic_prot(d->src_pages[src_page],
						 d->src_prot);
			if (!d->src_addr)
				return -ENOMEM;

			d->mapped_src = src_page;
		}
		diff->do_cpy(diff, d->dst_addr + dst_page_offset,
			     d->src_addr + src_page_offset, copy_size);

		bytes_to_copy -= copy_size;
		dst_offset += copy_size;
		src_offset += copy_size;
	}

	return 0;
}

static void *map_external(struct vmw_bo *bo, struct iosys_map *map)
{
	struct vmw_private *vmw =
		container_of(bo->tbo.bdev, struct vmw_private, bdev);
	void *ptr = NULL;
	int ret;

	if (bo->tbo.base.import_attach) {
		ret = dma_buf_vmap(bo->tbo.base.dma_buf, map);
		if (ret) {
			drm_dbg_driver(&vmw->drm,
				       "Wasn't able to map external bo!\n");
			goto out;
		}
		ptr = map->vaddr;
	} else {
		ptr = vmw_bo_map_and_cache(bo);
	}

out:
	return ptr;
}

static void unmap_external(struct vmw_bo *bo, struct iosys_map *map)
{
	if (bo->tbo.base.import_attach)
		dma_buf_vunmap(bo->tbo.base.dma_buf, map);
	else
		vmw_bo_unmap(bo);
}

static int vmw_external_bo_copy(struct vmw_bo *dst, u32 dst_offset,
				u32 dst_stride, struct vmw_bo *src,
				u32 src_offset, u32 src_stride,
				u32 width_in_bytes, u32 height,
				struct vmw_diff_cpy *diff)
{
	struct vmw_private *vmw =
		container_of(dst->tbo.bdev, struct vmw_private, bdev);
	size_t dst_size = dst->tbo.resource->size;
	size_t src_size = src->tbo.resource->size;
	struct iosys_map dst_map = {0};
	struct iosys_map src_map = {0};
	int ret, i;
	int x_in_bytes;
	u8 *vsrc;
	u8 *vdst;

	vsrc = map_external(src, &src_map);
	if (!vsrc) {
		drm_dbg_driver(&vmw->drm, "Wasn't able to map src\n");
		ret = -ENOMEM;
		goto out;
	}

	vdst = map_external(dst, &dst_map);
	if (!vdst) {
		drm_dbg_driver(&vmw->drm, "Wasn't able to map dst\n");
		ret = -ENOMEM;
		goto out;
	}

	vsrc += src_offset;
	vdst += dst_offset;
	if (src_stride == dst_stride) {
		dst_size -= dst_offset;
		src_size -= src_offset;
		memcpy(vdst, vsrc,
		       min(dst_stride * height, min(dst_size, src_size)));
	} else {
		WARN_ON(dst_stride < width_in_bytes);
		for (i = 0; i < height; ++i) {
			memcpy(vdst, vsrc, width_in_bytes);
			vsrc += src_stride;
			vdst += dst_stride;
		}
	}

	x_in_bytes = (dst_offset % dst_stride);
	diff->rect.x1 =  x_in_bytes / diff->cpp;
	diff->rect.y1 = ((dst_offset - x_in_bytes) / dst_stride);
	diff->rect.x2 = diff->rect.x1 + width_in_bytes / diff->cpp;
	diff->rect.y2 = diff->rect.y1 + height;

	ret = 0;
out:
	unmap_external(src, &src_map);
	unmap_external(dst, &dst_map);

	return ret;
}

/**
 * vmw_bo_cpu_blit - in-kernel cpu blit.
 *
 * @vmw_dst: Destination buffer object.
 * @dst_offset: Destination offset of blit start in bytes.
 * @dst_stride: Destination stride in bytes.
 * @vmw_src: Source buffer object.
 * @src_offset: Source offset of blit start in bytes.
 * @src_stride: Source stride in bytes.
 * @w: Width of blit.
 * @h: Height of blit.
 * @diff: The struct vmw_diff_cpy used to track the modified bounding box.
 * return: Zero on success. Negative error value on failure. Will print out
 * kernel warnings on caller bugs.
 *
 * Performs a CPU blit from one buffer object to another avoiding a full
 * bo vmap which may exhaust- or fragment vmalloc space.
 * On supported architectures (x86), we're using kmap_atomic which avoids
 * cross-processor TLB- and cache flushes and may, on non-HIGHMEM systems
 * reference already set-up mappings.
 *
 * Neither of the buffer objects may be placed in PCI memory
 * (Fixed memory in TTM terminology) when using this function.
 */
int vmw_bo_cpu_blit(struct vmw_bo *vmw_dst,
		    u32 dst_offset, u32 dst_stride,
		    struct vmw_bo *vmw_src,
		    u32 src_offset, u32 src_stride,
		    u32 w, u32 h,
		    struct vmw_diff_cpy *diff)
{
	struct ttm_buffer_object *src = &vmw_src->tbo;
	struct ttm_buffer_object *dst = &vmw_dst->tbo;
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};
	u32 j, initial_line = dst_offset / dst_stride;
	struct vmw_bo_blit_line_data d = {0};
	int ret = 0;
	struct page **dst_pages = NULL;
	struct page **src_pages = NULL;
	bool src_external = (src->ttm->page_flags & TTM_TT_FLAG_EXTERNAL) != 0;
	bool dst_external = (dst->ttm->page_flags & TTM_TT_FLAG_EXTERNAL) != 0;

	if (WARN_ON(dst == src))
		return -EINVAL;

	/* Buffer objects need to be either pinned or reserved: */
	if (!(dst->pin_count))
		dma_resv_assert_held(dst->base.resv);
	if (!(src->pin_count))
		dma_resv_assert_held(src->base.resv);

	if (!ttm_tt_is_populated(dst->ttm)) {
		ret = dst->bdev->funcs->ttm_tt_populate(dst->bdev, dst->ttm, &ctx);
		if (ret)
			return ret;
	}

	if (!ttm_tt_is_populated(src->ttm)) {
		ret = src->bdev->funcs->ttm_tt_populate(src->bdev, src->ttm, &ctx);
		if (ret)
			return ret;
	}

	if (src_external || dst_external)
		return vmw_external_bo_copy(vmw_dst, dst_offset, dst_stride,
					    vmw_src, src_offset, src_stride,
					    w, h, diff);

	if (!src->ttm->pages && src->ttm->sg) {
		src_pages = kvmalloc_array(src->ttm->num_pages,
					   sizeof(struct page *), GFP_KERNEL);
		if (!src_pages)
			return -ENOMEM;
		ret = drm_prime_sg_to_page_array(src->ttm->sg, src_pages,
						 src->ttm->num_pages);
		if (ret)
			goto out;
	}
	if (!dst->ttm->pages && dst->ttm->sg) {
		dst_pages = kvmalloc_array(dst->ttm->num_pages,
					   sizeof(struct page *), GFP_KERNEL);
		if (!dst_pages) {
			ret = -ENOMEM;
			goto out;
		}
		ret = drm_prime_sg_to_page_array(dst->ttm->sg, dst_pages,
						 dst->ttm->num_pages);
		if (ret)
			goto out;
	}

	d.mapped_dst = 0;
	d.mapped_src = 0;
	d.dst_addr = NULL;
	d.src_addr = NULL;
	d.dst_pages = dst->ttm->pages ? dst->ttm->pages : dst_pages;
	d.src_pages = src->ttm->pages ? src->ttm->pages : src_pages;
	d.dst_num_pages = PFN_UP(dst->resource->size);
	d.src_num_pages = PFN_UP(src->resource->size);
	d.dst_prot = ttm_io_prot(dst, dst->resource, PAGE_KERNEL);
	d.src_prot = ttm_io_prot(src, src->resource, PAGE_KERNEL);
	d.diff = diff;

	for (j = 0; j < h; ++j) {
		diff->line = j + initial_line;
		diff->line_offset = dst_offset % dst_stride;
		ret = vmw_bo_cpu_blit_line(&d, dst_offset, src_offset, w);
		if (ret)
			goto out;

		dst_offset += dst_stride;
		src_offset += src_stride;
	}
out:
	if (d.src_addr)
		kunmap_atomic(d.src_addr);
	if (d.dst_addr)
		kunmap_atomic(d.dst_addr);
	if (src_pages)
		kvfree(src_pages);
	if (dst_pages)
		kvfree(dst_pages);

	return ret;
}
