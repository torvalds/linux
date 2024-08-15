// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022, Oracle and/or its affiliates.
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */
#include <linux/iova_bitmap.h>
#include <linux/mm.h>
#include <linux/highmem.h>

#define BITS_PER_PAGE (PAGE_SIZE * BITS_PER_BYTE)

/*
 * struct iova_bitmap_map - A bitmap representing an IOVA range
 *
 * Main data structure for tracking mapped user pages of bitmap data.
 *
 * For example, for something recording dirty IOVAs, it will be provided a
 * struct iova_bitmap structure, as a general structure for iterating the
 * total IOVA range. The struct iova_bitmap_map, though, represents the
 * subset of said IOVA space that is pinned by its parent structure (struct
 * iova_bitmap).
 *
 * The user does not need to exact location of the bits in the bitmap.
 * From user perspective the only API available is iova_bitmap_set() which
 * records the IOVA *range* in the bitmap by setting the corresponding
 * bits.
 *
 * The bitmap is an array of u64 whereas each bit represents an IOVA of
 * range of (1 << pgshift). Thus formula for the bitmap data to be set is:
 *
 *   data[(iova / page_size) / 64] & (1ULL << (iova % 64))
 */
struct iova_bitmap_map {
	/* base IOVA representing bit 0 of the first page */
	unsigned long iova;

	/* page size order that each bit granules to */
	unsigned long pgshift;

	/* page offset of the first user page pinned */
	unsigned long pgoff;

	/* number of pages pinned */
	unsigned long npages;

	/* pinned pages representing the bitmap data */
	struct page **pages;
};

/*
 * struct iova_bitmap - The IOVA bitmap object
 *
 * Main data structure for iterating over the bitmap data.
 *
 * Abstracts the pinning work and iterates in IOVA ranges.
 * It uses a windowing scheme and pins the bitmap in relatively
 * big ranges e.g.
 *
 * The bitmap object uses one base page to store all the pinned pages
 * pointers related to the bitmap. For sizeof(struct page*) == 8 it stores
 * 512 struct page pointers which, if the base page size is 4K, it means
 * 2M of bitmap data is pinned at a time. If the iova_bitmap page size is
 * also 4K then the range window to iterate is 64G.
 *
 * For example iterating on a total IOVA range of 4G..128G, it will walk
 * through this set of ranges:
 *
 *    4G  -  68G-1 (64G)
 *    68G - 128G-1 (64G)
 *
 * An example of the APIs on how to use/iterate over the IOVA bitmap:
 *
 *   bitmap = iova_bitmap_alloc(iova, length, page_size, data);
 *   if (IS_ERR(bitmap))
 *       return PTR_ERR(bitmap);
 *
 *   ret = iova_bitmap_for_each(bitmap, arg, dirty_reporter_fn);
 *
 *   iova_bitmap_free(bitmap);
 *
 * Each iteration of the @dirty_reporter_fn is called with a unique @iova
 * and @length argument, indicating the current range available through the
 * iova_bitmap. The @dirty_reporter_fn uses iova_bitmap_set() to mark dirty
 * areas (@iova_length) within that provided range, as following:
 *
 *   iova_bitmap_set(bitmap, iova, iova_length);
 *
 * The internals of the object uses an index @mapped_base_index that indexes
 * which u64 word of the bitmap is mapped, up to @mapped_total_index.
 * Those keep being incremented until @mapped_total_index is reached while
 * mapping up to PAGE_SIZE / sizeof(struct page*) maximum of pages.
 *
 * The IOVA bitmap is usually located on what tracks DMA mapped ranges or
 * some form of IOVA range tracking that co-relates to the user passed
 * bitmap.
 */
struct iova_bitmap {
	/* IOVA range representing the currently mapped bitmap data */
	struct iova_bitmap_map mapped;

	/* userspace address of the bitmap */
	u8 __user *bitmap;

	/* u64 index that @mapped points to */
	unsigned long mapped_base_index;

	/* how many u64 can we walk in total */
	unsigned long mapped_total_index;

	/* base IOVA of the whole bitmap */
	unsigned long iova;

	/* length of the IOVA range for the whole bitmap */
	size_t length;
};

/*
 * Converts a relative IOVA to a bitmap index.
 * This function provides the index into the u64 array (bitmap::bitmap)
 * for a given IOVA offset.
 * Relative IOVA means relative to the bitmap::mapped base IOVA
 * (stored in mapped::iova). All computations in this file are done using
 * relative IOVAs and thus avoid an extra subtraction against mapped::iova.
 * The user API iova_bitmap_set() always uses a regular absolute IOVAs.
 */
static unsigned long iova_bitmap_offset_to_index(struct iova_bitmap *bitmap,
						 unsigned long iova)
{
	unsigned long pgsize = 1 << bitmap->mapped.pgshift;

	return iova / (BITS_PER_TYPE(*bitmap->bitmap) * pgsize);
}

/*
 * Converts a bitmap index to a *relative* IOVA.
 */
static unsigned long iova_bitmap_index_to_offset(struct iova_bitmap *bitmap,
						 unsigned long index)
{
	unsigned long pgshift = bitmap->mapped.pgshift;

	return (index * BITS_PER_TYPE(*bitmap->bitmap)) << pgshift;
}

/*
 * Returns the base IOVA of the mapped range.
 */
static unsigned long iova_bitmap_mapped_iova(struct iova_bitmap *bitmap)
{
	unsigned long skip = bitmap->mapped_base_index;

	return bitmap->iova + iova_bitmap_index_to_offset(bitmap, skip);
}

/*
 * Pins the bitmap user pages for the current range window.
 * This is internal to IOVA bitmap and called when advancing the
 * index (@mapped_base_index) or allocating the bitmap.
 */
static int iova_bitmap_get(struct iova_bitmap *bitmap)
{
	struct iova_bitmap_map *mapped = &bitmap->mapped;
	unsigned long npages;
	u8 __user *addr;
	long ret;

	/*
	 * @mapped_base_index is the index of the currently mapped u64 words
	 * that we have access. Anything before @mapped_base_index is not
	 * mapped. The range @mapped_base_index .. @mapped_total_index-1 is
	 * mapped but capped at a maximum number of pages.
	 */
	npages = DIV_ROUND_UP((bitmap->mapped_total_index -
			       bitmap->mapped_base_index) *
			       sizeof(*bitmap->bitmap), PAGE_SIZE);

	/*
	 * Bitmap address to be pinned is calculated via pointer arithmetic
	 * with bitmap u64 word index.
	 */
	addr = bitmap->bitmap + bitmap->mapped_base_index;

	/*
	 * We always cap at max number of 'struct page' a base page can fit.
	 * This is, for example, on x86 means 2M of bitmap data max.
	 */
	npages = min(npages + !!offset_in_page(addr),
		     PAGE_SIZE / sizeof(struct page *));

	ret = pin_user_pages_fast((unsigned long)addr, npages,
				  FOLL_WRITE, mapped->pages);
	if (ret <= 0)
		return -EFAULT;

	mapped->npages = (unsigned long)ret;
	/* Base IOVA where @pages point to i.e. bit 0 of the first page */
	mapped->iova = iova_bitmap_mapped_iova(bitmap);

	/*
	 * offset of the page where pinned pages bit 0 is located.
	 * This handles the case where the bitmap is not PAGE_SIZE
	 * aligned.
	 */
	mapped->pgoff = offset_in_page(addr);
	return 0;
}

/*
 * Unpins the bitmap user pages and clears @npages
 * (un)pinning is abstracted from API user and it's done when advancing
 * the index or freeing the bitmap.
 */
static void iova_bitmap_put(struct iova_bitmap *bitmap)
{
	struct iova_bitmap_map *mapped = &bitmap->mapped;

	if (mapped->npages) {
		unpin_user_pages(mapped->pages, mapped->npages);
		mapped->npages = 0;
	}
}

/**
 * iova_bitmap_alloc() - Allocates an IOVA bitmap object
 * @iova: Start address of the IOVA range
 * @length: Length of the IOVA range
 * @page_size: Page size of the IOVA bitmap. It defines what each bit
 *             granularity represents
 * @data: Userspace address of the bitmap
 *
 * Allocates an IOVA object and initializes all its fields including the
 * first user pages of @data.
 *
 * Return: A pointer to a newly allocated struct iova_bitmap
 * or ERR_PTR() on error.
 */
struct iova_bitmap *iova_bitmap_alloc(unsigned long iova, size_t length,
				      unsigned long page_size, u64 __user *data)
{
	struct iova_bitmap_map *mapped;
	struct iova_bitmap *bitmap;
	int rc;

	bitmap = kzalloc(sizeof(*bitmap), GFP_KERNEL);
	if (!bitmap)
		return ERR_PTR(-ENOMEM);

	mapped = &bitmap->mapped;
	mapped->pgshift = __ffs(page_size);
	bitmap->bitmap = (u8 __user *)data;
	bitmap->mapped_total_index =
		iova_bitmap_offset_to_index(bitmap, length - 1) + 1;
	bitmap->iova = iova;
	bitmap->length = length;
	mapped->iova = iova;
	mapped->pages = (struct page **)__get_free_page(GFP_KERNEL);
	if (!mapped->pages) {
		rc = -ENOMEM;
		goto err;
	}

	rc = iova_bitmap_get(bitmap);
	if (rc)
		goto err;
	return bitmap;

err:
	iova_bitmap_free(bitmap);
	return ERR_PTR(rc);
}

/**
 * iova_bitmap_free() - Frees an IOVA bitmap object
 * @bitmap: IOVA bitmap to free
 *
 * It unpins and releases pages array memory and clears any leftover
 * state.
 */
void iova_bitmap_free(struct iova_bitmap *bitmap)
{
	struct iova_bitmap_map *mapped = &bitmap->mapped;

	iova_bitmap_put(bitmap);

	if (mapped->pages) {
		free_page((unsigned long)mapped->pages);
		mapped->pages = NULL;
	}

	kfree(bitmap);
}

/*
 * Returns the remaining bitmap indexes from mapped_total_index to process for
 * the currently pinned bitmap pages.
 */
static unsigned long iova_bitmap_mapped_remaining(struct iova_bitmap *bitmap)
{
	unsigned long remaining, bytes;

	bytes = (bitmap->mapped.npages << PAGE_SHIFT) - bitmap->mapped.pgoff;

	remaining = bitmap->mapped_total_index - bitmap->mapped_base_index;
	remaining = min_t(unsigned long, remaining,
			  DIV_ROUND_UP(bytes, sizeof(*bitmap->bitmap)));

	return remaining;
}

/*
 * Returns the length of the mapped IOVA range.
 */
static unsigned long iova_bitmap_mapped_length(struct iova_bitmap *bitmap)
{
	unsigned long max_iova = bitmap->iova + bitmap->length - 1;
	unsigned long iova = iova_bitmap_mapped_iova(bitmap);
	unsigned long remaining;

	/*
	 * iova_bitmap_mapped_remaining() returns a number of indexes which
	 * when converted to IOVA gives us a max length that the bitmap
	 * pinned data can cover. Afterwards, that is capped to
	 * only cover the IOVA range in @bitmap::iova .. @bitmap::length.
	 */
	remaining = iova_bitmap_index_to_offset(bitmap,
			iova_bitmap_mapped_remaining(bitmap));

	if (iova + remaining - 1 > max_iova)
		remaining -= ((iova + remaining - 1) - max_iova);

	return remaining;
}

/*
 * Returns true if there's not more data to iterate.
 */
static bool iova_bitmap_done(struct iova_bitmap *bitmap)
{
	return bitmap->mapped_base_index >= bitmap->mapped_total_index;
}

/*
 * Advances to the next range, releases the current pinned
 * pages and pins the next set of bitmap pages.
 * Returns 0 on success or otherwise errno.
 */
static int iova_bitmap_advance(struct iova_bitmap *bitmap)
{
	unsigned long iova = iova_bitmap_mapped_length(bitmap) - 1;
	unsigned long count = iova_bitmap_offset_to_index(bitmap, iova) + 1;

	bitmap->mapped_base_index += count;

	iova_bitmap_put(bitmap);
	if (iova_bitmap_done(bitmap))
		return 0;

	/* When advancing the index we pin the next set of bitmap pages */
	return iova_bitmap_get(bitmap);
}

/**
 * iova_bitmap_for_each() - Iterates over the bitmap
 * @bitmap: IOVA bitmap to iterate
 * @opaque: Additional argument to pass to the callback
 * @fn: Function that gets called for each IOVA range
 *
 * Helper function to iterate over bitmap data representing a portion of IOVA
 * space. It hides the complexity of iterating bitmaps and translating the
 * mapped bitmap user pages into IOVA ranges to process.
 *
 * Return: 0 on success, and an error on failure either upon
 * iteration or when the callback returns an error.
 */
int iova_bitmap_for_each(struct iova_bitmap *bitmap, void *opaque,
			 iova_bitmap_fn_t fn)
{
	int ret = 0;

	for (; !iova_bitmap_done(bitmap) && !ret;
	     ret = iova_bitmap_advance(bitmap)) {
		ret = fn(bitmap, iova_bitmap_mapped_iova(bitmap),
			 iova_bitmap_mapped_length(bitmap), opaque);
		if (ret)
			break;
	}

	return ret;
}

/**
 * iova_bitmap_set() - Records an IOVA range in bitmap
 * @bitmap: IOVA bitmap
 * @iova: IOVA to start
 * @length: IOVA range length
 *
 * Set the bits corresponding to the range [iova .. iova+length-1] in
 * the user bitmap.
 *
 */
void iova_bitmap_set(struct iova_bitmap *bitmap,
		     unsigned long iova, size_t length)
{
	struct iova_bitmap_map *mapped = &bitmap->mapped;
	unsigned long cur_bit = ((iova - mapped->iova) >>
			mapped->pgshift) + mapped->pgoff * BITS_PER_BYTE;
	unsigned long last_bit = (((iova + length - 1) - mapped->iova) >>
			mapped->pgshift) + mapped->pgoff * BITS_PER_BYTE;
	unsigned long last_page_idx = mapped->npages - 1;

	do {
		unsigned int page_idx = cur_bit / BITS_PER_PAGE;
		unsigned int offset = cur_bit % BITS_PER_PAGE;
		unsigned int nbits = min(BITS_PER_PAGE - offset,
					 last_bit - cur_bit + 1);
		void *kaddr;

		if (unlikely(page_idx > last_page_idx))
			break;

		kaddr = kmap_local_page(mapped->pages[page_idx]);
		bitmap_set(kaddr, offset, nbits);
		kunmap_local(kaddr);
		cur_bit += nbits;
	} while (cur_bit <= last_bit);
}
EXPORT_SYMBOL_GPL(iova_bitmap_set);
