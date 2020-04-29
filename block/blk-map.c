// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to mapping data to requests
 */
#include <linux/kernel.h>
#include <linux/sched/task_stack.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/uio.h>

#include "blk.h"

struct bio_map_data {
	int is_our_pages;
	struct iov_iter iter;
	struct iovec iov[];
};

static struct bio_map_data *bio_alloc_map_data(struct iov_iter *data,
					       gfp_t gfp_mask)
{
	struct bio_map_data *bmd;

	if (data->nr_segs > UIO_MAXIOV)
		return NULL;

	bmd = kmalloc(struct_size(bmd, iov, data->nr_segs), gfp_mask);
	if (!bmd)
		return NULL;
	memcpy(bmd->iov, data->iov, sizeof(struct iovec) * data->nr_segs);
	bmd->iter = *data;
	bmd->iter.iov = bmd->iov;
	return bmd;
}

/**
 * bio_copy_from_iter - copy all pages from iov_iter to bio
 * @bio: The &struct bio which describes the I/O as destination
 * @iter: iov_iter as source
 *
 * Copy all pages from iov_iter to bio.
 * Returns 0 on success, or error on failure.
 */
static int bio_copy_from_iter(struct bio *bio, struct iov_iter *iter)
{
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		ssize_t ret;

		ret = copy_page_from_iter(bvec->bv_page,
					  bvec->bv_offset,
					  bvec->bv_len,
					  iter);

		if (!iov_iter_count(iter))
			break;

		if (ret < bvec->bv_len)
			return -EFAULT;
	}

	return 0;
}

/**
 * bio_copy_to_iter - copy all pages from bio to iov_iter
 * @bio: The &struct bio which describes the I/O as source
 * @iter: iov_iter as destination
 *
 * Copy all pages from bio to iov_iter.
 * Returns 0 on success, or error on failure.
 */
static int bio_copy_to_iter(struct bio *bio, struct iov_iter iter)
{
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		ssize_t ret;

		ret = copy_page_to_iter(bvec->bv_page,
					bvec->bv_offset,
					bvec->bv_len,
					&iter);

		if (!iov_iter_count(&iter))
			break;

		if (ret < bvec->bv_len)
			return -EFAULT;
	}

	return 0;
}

/**
 *	bio_uncopy_user	-	finish previously mapped bio
 *	@bio: bio being terminated
 *
 *	Free pages allocated from bio_copy_user_iov() and write back data
 *	to user space in case of a read.
 */
static int bio_uncopy_user(struct bio *bio)
{
	struct bio_map_data *bmd = bio->bi_private;
	int ret = 0;

	if (!bio_flagged(bio, BIO_NULL_MAPPED)) {
		/*
		 * if we're in a workqueue, the request is orphaned, so
		 * don't copy into a random user address space, just free
		 * and return -EINTR so user space doesn't expect any data.
		 */
		if (!current->mm)
			ret = -EINTR;
		else if (bio_data_dir(bio) == READ)
			ret = bio_copy_to_iter(bio, bmd->iter);
		if (bmd->is_our_pages)
			bio_free_pages(bio);
	}
	kfree(bmd);
	bio_put(bio);
	return ret;
}

/**
 *	bio_copy_user_iov	-	copy user data to bio
 *	@q:		destination block queue
 *	@map_data:	pointer to the rq_map_data holding pages (if necessary)
 *	@iter:		iovec iterator
 *	@gfp_mask:	memory allocation flags
 *
 *	Prepares and returns a bio for indirect user io, bouncing data
 *	to/from kernel pages as necessary. Must be paired with
 *	call bio_uncopy_user() on io completion.
 */
static struct bio *bio_copy_user_iov(struct request_queue *q,
		struct rq_map_data *map_data, struct iov_iter *iter,
		gfp_t gfp_mask)
{
	struct bio_map_data *bmd;
	struct page *page;
	struct bio *bio;
	int i = 0, ret;
	int nr_pages;
	unsigned int len = iter->count;
	unsigned int offset = map_data ? offset_in_page(map_data->offset) : 0;

	bmd = bio_alloc_map_data(iter, gfp_mask);
	if (!bmd)
		return ERR_PTR(-ENOMEM);

	/*
	 * We need to do a deep copy of the iov_iter including the iovecs.
	 * The caller provided iov might point to an on-stack or otherwise
	 * shortlived one.
	 */
	bmd->is_our_pages = map_data ? 0 : 1;

	nr_pages = DIV_ROUND_UP(offset + len, PAGE_SIZE);
	if (nr_pages > BIO_MAX_PAGES)
		nr_pages = BIO_MAX_PAGES;

	ret = -ENOMEM;
	bio = bio_kmalloc(gfp_mask, nr_pages);
	if (!bio)
		goto out_bmd;

	ret = 0;

	if (map_data) {
		nr_pages = 1 << map_data->page_order;
		i = map_data->offset / PAGE_SIZE;
	}
	while (len) {
		unsigned int bytes = PAGE_SIZE;

		bytes -= offset;

		if (bytes > len)
			bytes = len;

		if (map_data) {
			if (i == map_data->nr_entries * nr_pages) {
				ret = -ENOMEM;
				break;
			}

			page = map_data->pages[i / nr_pages];
			page += (i % nr_pages);

			i++;
		} else {
			page = alloc_page(q->bounce_gfp | gfp_mask);
			if (!page) {
				ret = -ENOMEM;
				break;
			}
		}

		if (bio_add_pc_page(q, bio, page, bytes, offset) < bytes) {
			if (!map_data)
				__free_page(page);
			break;
		}

		len -= bytes;
		offset = 0;
	}

	if (ret)
		goto cleanup;

	if (map_data)
		map_data->offset += bio->bi_iter.bi_size;

	/*
	 * success
	 */
	if ((iov_iter_rw(iter) == WRITE &&
	     (!map_data || !map_data->null_mapped)) ||
	    (map_data && map_data->from_user)) {
		ret = bio_copy_from_iter(bio, iter);
		if (ret)
			goto cleanup;
	} else {
		if (bmd->is_our_pages)
			zero_fill_bio(bio);
		iov_iter_advance(iter, bio->bi_iter.bi_size);
	}

	bio->bi_private = bmd;
	if (map_data && map_data->null_mapped)
		bio_set_flag(bio, BIO_NULL_MAPPED);
	return bio;
cleanup:
	if (!map_data)
		bio_free_pages(bio);
	bio_put(bio);
out_bmd:
	kfree(bmd);
	return ERR_PTR(ret);
}

/**
 *	bio_map_user_iov - map user iovec into bio
 *	@q:		the struct request_queue for the bio
 *	@iter:		iovec iterator
 *	@gfp_mask:	memory allocation flags
 *
 *	Map the user space address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
static struct bio *bio_map_user_iov(struct request_queue *q,
		struct iov_iter *iter, gfp_t gfp_mask)
{
	int j;
	struct bio *bio;
	int ret;

	if (!iov_iter_count(iter))
		return ERR_PTR(-EINVAL);

	bio = bio_kmalloc(gfp_mask, iov_iter_npages(iter, BIO_MAX_PAGES));
	if (!bio)
		return ERR_PTR(-ENOMEM);

	while (iov_iter_count(iter)) {
		struct page **pages;
		ssize_t bytes;
		size_t offs, added = 0;
		int npages;

		bytes = iov_iter_get_pages_alloc(iter, &pages, LONG_MAX, &offs);
		if (unlikely(bytes <= 0)) {
			ret = bytes ? bytes : -EFAULT;
			goto out_unmap;
		}

		npages = DIV_ROUND_UP(offs + bytes, PAGE_SIZE);

		if (unlikely(offs & queue_dma_alignment(q))) {
			ret = -EINVAL;
			j = 0;
		} else {
			for (j = 0; j < npages; j++) {
				struct page *page = pages[j];
				unsigned int n = PAGE_SIZE - offs;
				bool same_page = false;

				if (n > bytes)
					n = bytes;

				if (!__bio_add_pc_page(q, bio, page, n, offs,
						&same_page)) {
					if (same_page)
						put_page(page);
					break;
				}

				added += n;
				bytes -= n;
				offs = 0;
			}
			iov_iter_advance(iter, added);
		}
		/*
		 * release the pages we didn't map into the bio, if any
		 */
		while (j < npages)
			put_page(pages[j++]);
		kvfree(pages);
		/* couldn't stuff something into bio? */
		if (bytes)
			break;
	}

	bio_set_flag(bio, BIO_USER_MAPPED);

	/*
	 * subtle -- if bio_map_user_iov() ended up bouncing a bio,
	 * it would normally disappear when its bi_end_io is run.
	 * however, we need it for the unmap, so grab an extra
	 * reference to it
	 */
	bio_get(bio);
	return bio;

 out_unmap:
	bio_release_pages(bio, false);
	bio_put(bio);
	return ERR_PTR(ret);
}

/**
 *	bio_unmap_user	-	unmap a bio
 *	@bio:		the bio being unmapped
 *
 *	Unmap a bio previously mapped by bio_map_user_iov(). Must be called from
 *	process context.
 *
 *	bio_unmap_user() may sleep.
 */
static void bio_unmap_user(struct bio *bio)
{
	bio_release_pages(bio, bio_data_dir(bio) == READ);
	bio_put(bio);
	bio_put(bio);
}

static void bio_invalidate_vmalloc_pages(struct bio *bio)
{
#ifdef ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
	if (bio->bi_private && !op_is_write(bio_op(bio))) {
		unsigned long i, len = 0;

		for (i = 0; i < bio->bi_vcnt; i++)
			len += bio->bi_io_vec[i].bv_len;
		invalidate_kernel_vmap_range(bio->bi_private, len);
	}
#endif
}

static void bio_map_kern_endio(struct bio *bio)
{
	bio_invalidate_vmalloc_pages(bio);
	bio_put(bio);
}

/**
 *	bio_map_kern	-	map kernel address into bio
 *	@q: the struct request_queue for the bio
 *	@data: pointer to buffer to map
 *	@len: length in bytes
 *	@gfp_mask: allocation flags for bio allocation
 *
 *	Map the kernel address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
static struct bio *bio_map_kern(struct request_queue *q, void *data,
		unsigned int len, gfp_t gfp_mask)
{
	unsigned long kaddr = (unsigned long)data;
	unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = kaddr >> PAGE_SHIFT;
	const int nr_pages = end - start;
	bool is_vmalloc = is_vmalloc_addr(data);
	struct page *page;
	int offset, i;
	struct bio *bio;

	bio = bio_kmalloc(gfp_mask, nr_pages);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	if (is_vmalloc) {
		flush_kernel_vmap_range(data, len);
		bio->bi_private = data;
	}

	offset = offset_in_page(kaddr);
	for (i = 0; i < nr_pages; i++) {
		unsigned int bytes = PAGE_SIZE - offset;

		if (len <= 0)
			break;

		if (bytes > len)
			bytes = len;

		if (!is_vmalloc)
			page = virt_to_page(data);
		else
			page = vmalloc_to_page(data);
		if (bio_add_pc_page(q, bio, page, bytes,
				    offset) < bytes) {
			/* we don't support partial mappings */
			bio_put(bio);
			return ERR_PTR(-EINVAL);
		}

		data += bytes;
		len -= bytes;
		offset = 0;
	}

	bio->bi_end_io = bio_map_kern_endio;
	return bio;
}

static void bio_copy_kern_endio(struct bio *bio)
{
	bio_free_pages(bio);
	bio_put(bio);
}

static void bio_copy_kern_endio_read(struct bio *bio)
{
	char *p = bio->bi_private;
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		memcpy(p, page_address(bvec->bv_page), bvec->bv_len);
		p += bvec->bv_len;
	}

	bio_copy_kern_endio(bio);
}

/**
 *	bio_copy_kern	-	copy kernel address into bio
 *	@q: the struct request_queue for the bio
 *	@data: pointer to buffer to copy
 *	@len: length in bytes
 *	@gfp_mask: allocation flags for bio and page allocation
 *	@reading: data direction is READ
 *
 *	copy the kernel address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
static struct bio *bio_copy_kern(struct request_queue *q, void *data,
		unsigned int len, gfp_t gfp_mask, int reading)
{
	unsigned long kaddr = (unsigned long)data;
	unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = kaddr >> PAGE_SHIFT;
	struct bio *bio;
	void *p = data;
	int nr_pages = 0;

	/*
	 * Overflow, abort
	 */
	if (end < start)
		return ERR_PTR(-EINVAL);

	nr_pages = end - start;
	bio = bio_kmalloc(gfp_mask, nr_pages);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	while (len) {
		struct page *page;
		unsigned int bytes = PAGE_SIZE;

		if (bytes > len)
			bytes = len;

		page = alloc_page(q->bounce_gfp | gfp_mask);
		if (!page)
			goto cleanup;

		if (!reading)
			memcpy(page_address(page), p, bytes);

		if (bio_add_pc_page(q, bio, page, bytes, 0) < bytes)
			break;

		len -= bytes;
		p += bytes;
	}

	if (reading) {
		bio->bi_end_io = bio_copy_kern_endio_read;
		bio->bi_private = data;
	} else {
		bio->bi_end_io = bio_copy_kern_endio;
	}

	return bio;

cleanup:
	bio_free_pages(bio);
	bio_put(bio);
	return ERR_PTR(-ENOMEM);
}

/*
 * Append a bio to a passthrough request.  Only works if the bio can be merged
 * into the request based on the driver constraints.
 */
int blk_rq_append_bio(struct request *rq, struct bio **bio)
{
	struct bio *orig_bio = *bio;
	struct bvec_iter iter;
	struct bio_vec bv;
	unsigned int nr_segs = 0;

	blk_queue_bounce(rq->q, bio);

	bio_for_each_bvec(bv, *bio, iter)
		nr_segs++;

	if (!rq->bio) {
		blk_rq_bio_prep(rq, *bio, nr_segs);
	} else {
		if (!ll_back_merge_fn(rq, *bio, nr_segs)) {
			if (orig_bio != *bio) {
				bio_put(*bio);
				*bio = orig_bio;
			}
			return -EINVAL;
		}

		rq->biotail->bi_next = *bio;
		rq->biotail = *bio;
		rq->__data_len += (*bio)->bi_iter.bi_size;
	}

	return 0;
}
EXPORT_SYMBOL(blk_rq_append_bio);

static int __blk_rq_unmap_user(struct bio *bio)
{
	int ret = 0;

	if (bio) {
		if (bio_flagged(bio, BIO_USER_MAPPED))
			bio_unmap_user(bio);
		else
			ret = bio_uncopy_user(bio);
	}

	return ret;
}

static int __blk_rq_map_user_iov(struct request *rq,
		struct rq_map_data *map_data, struct iov_iter *iter,
		gfp_t gfp_mask, bool copy)
{
	struct request_queue *q = rq->q;
	struct bio *bio, *orig_bio;
	int ret;

	if (copy)
		bio = bio_copy_user_iov(q, map_data, iter, gfp_mask);
	else
		bio = bio_map_user_iov(q, iter, gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_opf &= ~REQ_OP_MASK;
	bio->bi_opf |= req_op(rq);

	orig_bio = bio;

	/*
	 * We link the bounce buffer in and could have to traverse it
	 * later so we have to get a ref to prevent it from being freed
	 */
	ret = blk_rq_append_bio(rq, &bio);
	if (ret) {
		__blk_rq_unmap_user(orig_bio);
		return ret;
	}
	bio_get(bio);

	return 0;
}

/**
 * blk_rq_map_user_iov - map user data to a request, for passthrough requests
 * @q:		request queue where request should be inserted
 * @rq:		request to map data to
 * @map_data:   pointer to the rq_map_data holding pages (if necessary)
 * @iter:	iovec iterator
 * @gfp_mask:	memory allocation flags
 *
 * Description:
 *    Data will be mapped directly for zero copy I/O, if possible. Otherwise
 *    a kernel bounce buffer is used.
 *
 *    A matching blk_rq_unmap_user() must be issued at the end of I/O, while
 *    still in process context.
 *
 *    Note: The mapped bio may need to be bounced through blk_queue_bounce()
 *    before being submitted to the device, as pages mapped may be out of
 *    reach. It's the callers responsibility to make sure this happens. The
 *    original bio must be passed back in to blk_rq_unmap_user() for proper
 *    unmapping.
 */
int blk_rq_map_user_iov(struct request_queue *q, struct request *rq,
			struct rq_map_data *map_data,
			const struct iov_iter *iter, gfp_t gfp_mask)
{
	bool copy = false;
	unsigned long align = q->dma_pad_mask | queue_dma_alignment(q);
	struct bio *bio = NULL;
	struct iov_iter i;
	int ret = -EINVAL;

	if (!iter_is_iovec(iter))
		goto fail;

	if (map_data)
		copy = true;
	else if (iov_iter_alignment(iter) & align)
		copy = true;
	else if (queue_virt_boundary(q))
		copy = queue_virt_boundary(q) & iov_iter_gap_alignment(iter);

	i = *iter;
	do {
		ret =__blk_rq_map_user_iov(rq, map_data, &i, gfp_mask, copy);
		if (ret)
			goto unmap_rq;
		if (!bio)
			bio = rq->bio;
	} while (iov_iter_count(&i));

	if (!bio_flagged(bio, BIO_USER_MAPPED))
		rq->rq_flags |= RQF_COPY_USER;
	return 0;

unmap_rq:
	blk_rq_unmap_user(bio);
fail:
	rq->bio = NULL;
	return ret;
}
EXPORT_SYMBOL(blk_rq_map_user_iov);

int blk_rq_map_user(struct request_queue *q, struct request *rq,
		    struct rq_map_data *map_data, void __user *ubuf,
		    unsigned long len, gfp_t gfp_mask)
{
	struct iovec iov;
	struct iov_iter i;
	int ret = import_single_range(rq_data_dir(rq), ubuf, len, &iov, &i);

	if (unlikely(ret < 0))
		return ret;

	return blk_rq_map_user_iov(q, rq, map_data, &i, gfp_mask);
}
EXPORT_SYMBOL(blk_rq_map_user);

/**
 * blk_rq_unmap_user - unmap a request with user data
 * @bio:	       start of bio list
 *
 * Description:
 *    Unmap a rq previously mapped by blk_rq_map_user(). The caller must
 *    supply the original rq->bio from the blk_rq_map_user() return, since
 *    the I/O completion may have changed rq->bio.
 */
int blk_rq_unmap_user(struct bio *bio)
{
	struct bio *mapped_bio;
	int ret = 0, ret2;

	while (bio) {
		mapped_bio = bio;
		if (unlikely(bio_flagged(bio, BIO_BOUNCED)))
			mapped_bio = bio->bi_private;

		ret2 = __blk_rq_unmap_user(mapped_bio);
		if (ret2 && !ret)
			ret = ret2;

		mapped_bio = bio;
		bio = bio->bi_next;
		bio_put(mapped_bio);
	}

	return ret;
}
EXPORT_SYMBOL(blk_rq_unmap_user);

/**
 * blk_rq_map_kern - map kernel data to a request, for passthrough requests
 * @q:		request queue where request should be inserted
 * @rq:		request to fill
 * @kbuf:	the kernel buffer
 * @len:	length of user data
 * @gfp_mask:	memory allocation flags
 *
 * Description:
 *    Data will be mapped directly if possible. Otherwise a bounce
 *    buffer is used. Can be called multiple times to append multiple
 *    buffers.
 */
int blk_rq_map_kern(struct request_queue *q, struct request *rq, void *kbuf,
		    unsigned int len, gfp_t gfp_mask)
{
	int reading = rq_data_dir(rq) == READ;
	unsigned long addr = (unsigned long) kbuf;
	int do_copy = 0;
	struct bio *bio, *orig_bio;
	int ret;

	if (len > (queue_max_hw_sectors(q) << 9))
		return -EINVAL;
	if (!len || !kbuf)
		return -EINVAL;

	do_copy = !blk_rq_aligned(q, addr, len) || object_is_on_stack(kbuf);
	if (do_copy)
		bio = bio_copy_kern(q, kbuf, len, gfp_mask, reading);
	else
		bio = bio_map_kern(q, kbuf, len, gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_opf &= ~REQ_OP_MASK;
	bio->bi_opf |= req_op(rq);

	if (do_copy)
		rq->rq_flags |= RQF_COPY_USER;

	orig_bio = bio;
	ret = blk_rq_append_bio(rq, &bio);
	if (unlikely(ret)) {
		/* request is too big */
		bio_put(orig_bio);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(blk_rq_map_kern);
