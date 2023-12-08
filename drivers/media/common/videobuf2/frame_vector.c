// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/sched.h>

#include <media/frame_vector.h>

/**
 * get_vaddr_frames() - map virtual addresses to pfns
 * @start:	starting user address
 * @nr_frames:	number of pages / pfns from start to map
 * @vec:	structure which receives pages / pfns of the addresses mapped.
 *		It should have space for at least nr_frames entries.
 *
 * This function maps virtual addresses from @start and fills @vec structure
 * with page frame numbers or page pointers to corresponding pages (choice
 * depends on the type of the vma underlying the virtual address). If @start
 * belongs to a normal vma, the function grabs reference to each of the pages
 * to pin them in memory. If @start belongs to VM_IO | VM_PFNMAP vma, we don't
 * touch page structures and the caller must make sure pfns aren't reused for
 * anything else while he is using them.
 *
 * The function returns number of pages mapped which may be less than
 * @nr_frames. In particular we stop mapping if there are more vmas of
 * different type underlying the specified range of virtual addresses.
 * When the function isn't able to map a single page, it returns error.
 *
 * Note that get_vaddr_frames() cannot follow VM_IO mappings. It used
 * to be able to do that, but that could (racily) return non-refcounted
 * pfns.
 *
 * This function takes care of grabbing mmap_lock as necessary.
 */
int get_vaddr_frames(unsigned long start, unsigned int nr_frames,
		     struct frame_vector *vec)
{
	int ret;

	if (nr_frames == 0)
		return 0;

	if (WARN_ON_ONCE(nr_frames > vec->nr_allocated))
		nr_frames = vec->nr_allocated;

	start = untagged_addr(start);

	ret = pin_user_pages_fast(start, nr_frames,
				  FOLL_FORCE | FOLL_WRITE | FOLL_LONGTERM,
				  (struct page **)(vec->ptrs));
	vec->got_ref = true;
	vec->is_pfns = false;
	vec->nr_frames = ret;

	if (likely(ret > 0))
		return ret;

	vec->nr_frames = 0;
	return ret ? ret : -EFAULT;
}
EXPORT_SYMBOL(get_vaddr_frames);

/**
 * put_vaddr_frames() - drop references to pages if get_vaddr_frames() acquired
 *			them
 * @vec:	frame vector to put
 *
 * Drop references to pages if get_vaddr_frames() acquired them. We also
 * invalidate the frame vector so that it is prepared for the next call into
 * get_vaddr_frames().
 */
void put_vaddr_frames(struct frame_vector *vec)
{
	struct page **pages;

	if (!vec->got_ref)
		goto out;
	pages = frame_vector_pages(vec);
	/*
	 * frame_vector_pages() might needed to do a conversion when
	 * get_vaddr_frames() got pages but vec was later converted to pfns.
	 * But it shouldn't really fail to convert pfns back...
	 */
	if (WARN_ON(IS_ERR(pages)))
		goto out;

	unpin_user_pages(pages, vec->nr_frames);
	vec->got_ref = false;
out:
	vec->nr_frames = 0;
}
EXPORT_SYMBOL(put_vaddr_frames);

/**
 * frame_vector_to_pages - convert frame vector to contain page pointers
 * @vec:	frame vector to convert
 *
 * Convert @vec to contain array of page pointers.  If the conversion is
 * successful, return 0. Otherwise return an error. Note that we do not grab
 * page references for the page structures.
 */
int frame_vector_to_pages(struct frame_vector *vec)
{
	int i;
	unsigned long *nums;
	struct page **pages;

	if (!vec->is_pfns)
		return 0;
	nums = frame_vector_pfns(vec);
	for (i = 0; i < vec->nr_frames; i++)
		if (!pfn_valid(nums[i]))
			return -EINVAL;
	pages = (struct page **)nums;
	for (i = 0; i < vec->nr_frames; i++)
		pages[i] = pfn_to_page(nums[i]);
	vec->is_pfns = false;
	return 0;
}
EXPORT_SYMBOL(frame_vector_to_pages);

/**
 * frame_vector_to_pfns - convert frame vector to contain pfns
 * @vec:	frame vector to convert
 *
 * Convert @vec to contain array of pfns.
 */
void frame_vector_to_pfns(struct frame_vector *vec)
{
	int i;
	unsigned long *nums;
	struct page **pages;

	if (vec->is_pfns)
		return;
	pages = (struct page **)(vec->ptrs);
	nums = (unsigned long *)pages;
	for (i = 0; i < vec->nr_frames; i++)
		nums[i] = page_to_pfn(pages[i]);
	vec->is_pfns = true;
}
EXPORT_SYMBOL(frame_vector_to_pfns);

/**
 * frame_vector_create() - allocate & initialize structure for pinned pfns
 * @nr_frames:	number of pfns slots we should reserve
 *
 * Allocate and initialize struct pinned_pfns to be able to hold @nr_pfns
 * pfns.
 */
struct frame_vector *frame_vector_create(unsigned int nr_frames)
{
	struct frame_vector *vec;
	int size = sizeof(struct frame_vector) + sizeof(void *) * nr_frames;

	if (WARN_ON_ONCE(nr_frames == 0))
		return NULL;
	/*
	 * This is absurdly high. It's here just to avoid strange effects when
	 * arithmetics overflows.
	 */
	if (WARN_ON_ONCE(nr_frames > INT_MAX / sizeof(void *) / 2))
		return NULL;
	/*
	 * Avoid higher order allocations, use vmalloc instead. It should
	 * be rare anyway.
	 */
	vec = kvmalloc(size, GFP_KERNEL);
	if (!vec)
		return NULL;
	vec->nr_allocated = nr_frames;
	vec->nr_frames = 0;
	return vec;
}
EXPORT_SYMBOL(frame_vector_create);

/**
 * frame_vector_destroy() - free memory allocated to carry frame vector
 * @vec:	Frame vector to free
 *
 * Free structure allocated by frame_vector_create() to carry frames.
 */
void frame_vector_destroy(struct frame_vector *vec)
{
	/* Make sure put_vaddr_frames() got called properly... */
	VM_BUG_ON(vec->nr_frames > 0);
	kvfree(vec);
}
EXPORT_SYMBOL(frame_vector_destroy);
