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
	bool is_our_pages : 1;
	bool is_null_mapped : 1;
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

	if (!bmd->is_null_mapped) {
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
	return ret;
}

static int bio_copy_user_iov(struct request *rq, struct rq_map_data *map_data,
		struct iov_iter *iter, gfp_t gfp_mask)
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
		return -ENOMEM;

	/*
	 * We need to do a deep copy of the iov_iter including the iovecs.
	 * The caller provided iov might point to an on-stack or otherwise
	 * shortlived one.
	 */
	bmd->is_our_pages = !map_data;
	bmd->is_null_mapped = (map_data && map_data->null_mapped);

	nr_pages = bio_max_segs(DIV_ROUND_UP(offset + len, PAGE_SIZE));

	ret = -ENOMEM;
	bio = bio_kmalloc(nr_pages, gfp_mask);
	if (!bio)
		goto out_bmd;
	bio_init(bio, NULL, bio->bi_inline_vecs, nr_pages, req_op(rq));

	if (map_data) {
		nr_pages = 1U << map_data->page_order;
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
				goto cleanup;
			}

			page = map_data->pages[i / nr_pages];
			page += (i % nr_pages);

			i++;
		} else {
			page = alloc_page(GFP_NOIO | gfp_mask);
			if (!page) {
				ret = -ENOMEM;
				goto cleanup;
			}
		}

		if (bio_add_pc_page(rq->q, bio, page, bytes, offset) < bytes) {
			if (!map_data)
				__free_page(page);
			break;
		}

		len -= bytes;
		offset = 0;
	}

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

	ret = blk_rq_append_bio(rq, bio);
	if (ret)
		goto cleanup;
	return 0;
cleanup:
	if (!map_data)
		bio_free_pages(bio);
	bio_uninit(bio);
	kfree(bio);
out_bmd:
	kfree(bmd);
	return ret;
}

static void blk_mq_map_bio_put(struct bio *bio)
{
	if (bio->bi_opf & REQ_ALLOC_CACHE) {
		bio_put(bio);
	} else {
		bio_uninit(bio);
		kfree(bio);
	}
}

static struct bio *blk_rq_map_bio_alloc(struct request *rq,
		unsigned int nr_vecs, gfp_t gfp_mask)
{
	struct bio *bio;

	if (rq->cmd_flags & REQ_POLLED && (nr_vecs <= BIO_INLINE_VECS)) {
		blk_opf_t opf = rq->cmd_flags | REQ_ALLOC_CACHE;

		bio = bio_alloc_bioset(NULL, nr_vecs, opf, gfp_mask,
					&fs_bio_set);
		if (!bio)
			return NULL;
	} else {
		bio = bio_kmalloc(nr_vecs, gfp_mask);
		if (!bio)
			return NULL;
		bio_init(bio, NULL, bio->bi_inline_vecs, nr_vecs, req_op(rq));
	}
	return bio;
}

static int bio_map_user_iov(struct request *rq, struct iov_iter *iter,
		gfp_t gfp_mask)
{
	unsigned int max_sectors = queue_max_hw_sectors(rq->q);
	unsigned int nr_vecs = iov_iter_npages(iter, BIO_MAX_VECS);
	struct bio *bio;
	int ret;
	int j;

	if (!iov_iter_count(iter))
		return -EINVAL;

	bio = blk_rq_map_bio_alloc(rq, nr_vecs, gfp_mask);
	if (bio == NULL)
		return -ENOMEM;

	while (iov_iter_count(iter)) {
		struct page **pages, *stack_pages[UIO_FASTIOV];
		ssize_t bytes;
		size_t offs;
		int npages;

		if (nr_vecs <= ARRAY_SIZE(stack_pages)) {
			pages = stack_pages;
			bytes = iov_iter_get_pages2(iter, pages, LONG_MAX,
							nr_vecs, &offs);
		} else {
			bytes = iov_iter_get_pages_alloc2(iter, &pages,
							LONG_MAX, &offs);
		}
		if (unlikely(bytes <= 0)) {
			ret = bytes ? bytes : -EFAULT;
			goto out_unmap;
		}

		npages = DIV_ROUND_UP(offs + bytes, PAGE_SIZE);

		if (unlikely(offs & queue_dma_alignment(rq->q)))
			j = 0;
		else {
			for (j = 0; j < npages; j++) {
				struct page *page = pages[j];
				unsigned int n = PAGE_SIZE - offs;
				bool same_page = false;

				if (n > bytes)
					n = bytes;

				if (!bio_add_hw_page(rq->q, bio, page, n, offs,
						     max_sectors, &same_page)) {
					if (same_page)
						put_page(page);
					break;
				}

				bytes -= n;
				offs = 0;
			}
		}
		/*
		 * release the pages we didn't map into the bio, if any
		 */
		while (j < npages)
			put_page(pages[j++]);
		if (pages != stack_pages)
			kvfree(pages);
		/* couldn't stuff something into bio? */
		if (bytes) {
			iov_iter_revert(iter, bytes);
			break;
		}
	}

	ret = blk_rq_append_bio(rq, bio);
	if (ret)
		goto out_unmap;
	return 0;

 out_unmap:
	bio_release_pages(bio, false);
	blk_mq_map_bio_put(bio);
	return ret;
}

static void bio_invalidate_vmalloc_pages(struct bio *bio)
{
#ifdef ARCH_IMPLEMENTS_FLUSH_KERNEL_VMAP_RANGE
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
	bio_uninit(bio);
	kfree(bio);
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

	bio = bio_kmalloc(nr_pages, gfp_mask);
	if (!bio)
		return ERR_PTR(-ENOMEM);
	bio_init(bio, NULL, bio->bi_inline_vecs, nr_pages, 0);

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
			bio_uninit(bio);
			kfree(bio);
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
	bio_uninit(bio);
	kfree(bio);
}

static void bio_copy_kern_endio_read(struct bio *bio)
{
	char *p = bio->bi_private;
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		memcpy_from_bvec(p, bvec);
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
	bio = bio_kmalloc(nr_pages, gfp_mask);
	if (!bio)
		return ERR_PTR(-ENOMEM);
	bio_init(bio, NULL, bio->bi_inline_vecs, nr_pages, 0);

	while (len) {
		struct page *page;
		unsigned int bytes = PAGE_SIZE;

		if (bytes > len)
			bytes = len;

		page = alloc_page(GFP_NOIO | __GFP_ZERO | gfp_mask);
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
	bio_uninit(bio);
	kfree(bio);
	return ERR_PTR(-ENOMEM);
}

/*
 * Append a bio to a passthrough request.  Only works if the bio can be merged
 * into the request based on the driver constraints.
 */
int blk_rq_append_bio(struct request *rq, struct bio *bio)
{
	struct bvec_iter iter;
	struct bio_vec bv;
	unsigned int nr_segs = 0;

	bio_for_each_bvec(bv, bio, iter)
		nr_segs++;

	if (!rq->bio) {
		blk_rq_bio_prep(rq, bio, nr_segs);
	} else {
		if (!ll_back_merge_fn(rq, bio, nr_segs))
			return -EINVAL;
		rq->biotail->bi_next = bio;
		rq->biotail = bio;
		rq->__data_len += (bio)->bi_iter.bi_size;
		bio_crypt_free_ctx(bio);
	}

	return 0;
}
EXPORT_SYMBOL(blk_rq_append_bio);

/* Prepare bio for passthrough IO given ITER_BVEC iter */
static int blk_rq_map_user_bvec(struct request *rq, const struct iov_iter *iter)
{
	struct request_queue *q = rq->q;
	size_t nr_iter = iov_iter_count(iter);
	size_t nr_segs = iter->nr_segs;
	struct bio_vec *bvecs, *bvprvp = NULL;
	struct queue_limits *lim = &q->limits;
	unsigned int nsegs = 0, bytes = 0;
	struct bio *bio;
	size_t i;

	if (!nr_iter || (nr_iter >> SECTOR_SHIFT) > queue_max_hw_sectors(q))
		return -EINVAL;
	if (nr_segs > queue_max_segments(q))
		return -EINVAL;

	/* no iovecs to alloc, as we already have a BVEC iterator */
	bio = blk_rq_map_bio_alloc(rq, 0, GFP_KERNEL);
	if (bio == NULL)
		return -ENOMEM;

	bio_iov_bvec_set(bio, (struct iov_iter *)iter);
	blk_rq_bio_prep(rq, bio, nr_segs);

	/* loop to perform a bunch of sanity checks */
	bvecs = (struct bio_vec *)iter->bvec;
	for (i = 0; i < nr_segs; i++) {
		struct bio_vec *bv = &bvecs[i];

		/*
		 * If the queue doesn't support SG gaps and adding this
		 * offset would create a gap, fallback to copy.
		 */
		if (bvprvp && bvec_gap_to_prev(lim, bvprvp, bv->bv_offset)) {
			blk_mq_map_bio_put(bio);
			return -EREMOTEIO;
		}
		/* check full condition */
		if (nsegs >= nr_segs || bytes > UINT_MAX - bv->bv_len)
			goto put_bio;
		if (bytes + bv->bv_len > nr_iter)
			goto put_bio;
		if (bv->bv_offset + bv->bv_len > PAGE_SIZE)
			goto put_bio;

		nsegs++;
		bytes += bv->bv_len;
		bvprvp = bv;
	}
	return 0;
put_bio:
	blk_mq_map_bio_put(bio);
	return -EINVAL;
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
 */
int blk_rq_map_user_iov(struct request_queue *q, struct request *rq,
			struct rq_map_data *map_data,
			const struct iov_iter *iter, gfp_t gfp_mask)
{
	bool copy = false, map_bvec = false;
	unsigned long align = q->dma_pad_mask | queue_dma_alignment(q);
	struct bio *bio = NULL;
	struct iov_iter i;
	int ret = -EINVAL;

	if (map_data)
		copy = true;
	else if (blk_queue_may_bounce(q))
		copy = true;
	else if (iov_iter_alignment(iter) & align)
		copy = true;
	else if (iov_iter_is_bvec(iter))
		map_bvec = true;
	else if (!iter_is_iovec(iter))
		copy = true;
	else if (queue_virt_boundary(q))
		copy = queue_virt_boundary(q) & iov_iter_gap_alignment(iter);

	if (map_bvec) {
		ret = blk_rq_map_user_bvec(rq, iter);
		if (!ret)
			return 0;
		if (ret != -EREMOTEIO)
			goto fail;
		/* fall back to copying the data on limits mismatches */
		copy = true;
	}

	i = *iter;
	do {
		if (copy)
			ret = bio_copy_user_iov(rq, map_data, &i, gfp_mask);
		else
			ret = bio_map_user_iov(rq, &i, gfp_mask);
		if (ret)
			goto unmap_rq;
		if (!bio)
			bio = rq->bio;
	} while (iov_iter_count(&i));

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

int blk_rq_map_user_io(struct request *req, struct rq_map_data *map_data,
		void __user *ubuf, unsigned long buf_len, gfp_t gfp_mask,
		bool vec, int iov_count, bool check_iter_count, int rw)
{
	int ret = 0;

	if (vec) {
		struct iovec fast_iov[UIO_FASTIOV];
		struct iovec *iov = fast_iov;
		struct iov_iter iter;

		ret = import_iovec(rw, ubuf, iov_count ? iov_count : buf_len,
				UIO_FASTIOV, &iov, &iter);
		if (ret < 0)
			return ret;

		if (iov_count) {
			/* SG_IO howto says that the shorter of the two wins */
			iov_iter_truncate(&iter, buf_len);
			if (check_iter_count && !iov_iter_count(&iter)) {
				kfree(iov);
				return -EINVAL;
			}
		}

		ret = blk_rq_map_user_iov(req->q, req, map_data, &iter,
				gfp_mask);
		kfree(iov);
	} else if (buf_len) {
		ret = blk_rq_map_user(req->q, req, map_data, ubuf, buf_len,
				gfp_mask);
	}
	return ret;
}
EXPORT_SYMBOL(blk_rq_map_user_io);

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
	struct bio *next_bio;
	int ret = 0, ret2;

	while (bio) {
		if (bio->bi_private) {
			ret2 = bio_uncopy_user(bio);
			if (ret2 && !ret)
				ret = ret2;
		} else {
			bio_release_pages(bio, bio_data_dir(bio) == READ);
		}

		next_bio = bio;
		bio = bio->bi_next;
		blk_mq_map_bio_put(next_bio);
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
	struct bio *bio;
	int ret;

	if (len > (queue_max_hw_sectors(q) << 9))
		return -EINVAL;
	if (!len || !kbuf)
		return -EINVAL;

	if (!blk_rq_aligned(q, addr, len) || object_is_on_stack(kbuf) ||
	    blk_queue_may_bounce(q))
		bio = bio_copy_kern(q, kbuf, len, gfp_mask, reading);
	else
		bio = bio_map_kern(q, kbuf, len, gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_opf &= ~REQ_OP_MASK;
	bio->bi_opf |= req_op(rq);

	ret = blk_rq_append_bio(rq, bio);
	if (unlikely(ret)) {
		bio_uninit(bio);
		kfree(bio);
	}
	return ret;
}
EXPORT_SYMBOL(blk_rq_map_kern);
