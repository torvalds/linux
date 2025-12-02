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
	bmd->iter = *data;
	if (iter_is_iovec(data)) {
		memcpy(bmd->iov, iter_iov(data), sizeof(struct iovec) * data->nr_segs);
		bmd->iter.__iov = bmd->iov;
	}
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
	bio_init_inline(bio, NULL, nr_pages, req_op(rq));

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

		if (bio_add_page(bio, page, bytes, offset) < bytes) {
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
	if (iov_iter_rw(iter) == WRITE &&
	     (!map_data || !map_data->null_mapped)) {
		ret = bio_copy_from_iter(bio, iter);
		if (ret)
			goto cleanup;
	} else if (map_data && map_data->from_user) {
		struct iov_iter iter2 = *iter;

		/* This is the copy-in part of SG_DXFER_TO_FROM_DEV. */
		iter2.data_source = ITER_SOURCE;
		ret = bio_copy_from_iter(bio, &iter2);
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
	struct block_device *bdev = rq->q->disk ? rq->q->disk->part0 : NULL;
	struct bio *bio;

	if (rq->cmd_flags & REQ_ALLOC_CACHE && (nr_vecs <= BIO_INLINE_VECS)) {
		bio = bio_alloc_bioset(bdev, nr_vecs, rq->cmd_flags, gfp_mask,
					&fs_bio_set);
		if (!bio)
			return NULL;
	} else {
		bio = bio_kmalloc(nr_vecs, gfp_mask);
		if (!bio)
			return NULL;
		bio_init_inline(bio, bdev, nr_vecs, req_op(rq));
	}
	return bio;
}

static int bio_map_user_iov(struct request *rq, struct iov_iter *iter,
		gfp_t gfp_mask)
{
	unsigned int nr_vecs = iov_iter_npages(iter, BIO_MAX_VECS);
	struct bio *bio;
	int ret;

	if (!iov_iter_count(iter))
		return -EINVAL;

	bio = blk_rq_map_bio_alloc(rq, nr_vecs, gfp_mask);
	if (!bio)
		return -ENOMEM;
	/*
	 * No alignment requirements on our part to support arbitrary
	 * passthrough commands.
	 */
	ret = bio_iov_iter_get_pages(bio, iter, 0);
	if (ret)
		goto out_put;
	ret = blk_rq_append_bio(rq, bio);
	if (ret)
		goto out_release;
	return 0;

out_release:
	bio_release_pages(bio, false);
out_put:
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

static struct bio *bio_map_kern(void *data, unsigned int len, enum req_op op,
		gfp_t gfp_mask)
{
	unsigned int nr_vecs = bio_add_max_vecs(data, len);
	struct bio *bio;

	bio = bio_kmalloc(nr_vecs, gfp_mask);
	if (!bio)
		return ERR_PTR(-ENOMEM);
	bio_init_inline(bio, NULL, nr_vecs, op);
	if (is_vmalloc_addr(data)) {
		bio->bi_private = data;
		if (!bio_add_vmalloc(bio, data, len)) {
			bio_uninit(bio);
			kfree(bio);
			return ERR_PTR(-EINVAL);
		}
	} else {
		bio_add_virt_nofail(bio, data, len);
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
 *	@data: pointer to buffer to copy
 *	@len: length in bytes
 *	@op: bio/request operation
 *	@gfp_mask: allocation flags for bio and page allocation
 *
 *	copy the kernel address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
static struct bio *bio_copy_kern(void *data, unsigned int len, enum req_op op,
		gfp_t gfp_mask)
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
	bio_init_inline(bio, NULL, nr_pages, op);

	while (len) {
		struct page *page;
		unsigned int bytes = PAGE_SIZE;

		if (bytes > len)
			bytes = len;

		page = alloc_page(GFP_NOIO | __GFP_ZERO | gfp_mask);
		if (!page)
			goto cleanup;

		if (op_is_write(op))
			memcpy(page_address(page), p, bytes);

		if (bio_add_page(bio, page, bytes, 0) < bytes)
			break;

		len -= bytes;
		p += bytes;
	}

	if (op_is_write(op)) {
		bio->bi_end_io = bio_copy_kern_endio;
	} else {
		bio->bi_end_io = bio_copy_kern_endio_read;
		bio->bi_private = data;
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
	const struct queue_limits *lim = &rq->q->limits;
	unsigned int max_bytes = lim->max_hw_sectors << SECTOR_SHIFT;
	unsigned int nr_segs = 0;
	int ret;

	/* check that the data layout matches the hardware restrictions */
	ret = bio_split_io_at(bio, lim, &nr_segs, max_bytes, 0);
	if (ret) {
		/* if we would have to split the bio, copy instead */
		if (ret > 0)
			ret = -EREMOTEIO;
		return ret;
	}

	if (rq->bio) {
		if (!ll_back_merge_fn(rq, bio, nr_segs))
			return -EINVAL;
		rq->biotail->bi_next = bio;
		rq->biotail = bio;
		rq->__data_len += bio->bi_iter.bi_size;
		bio_crypt_free_ctx(bio);
		return 0;
	}

	rq->nr_phys_segments = nr_segs;
	rq->bio = rq->biotail = bio;
	rq->__data_len = bio->bi_iter.bi_size;
	return 0;
}
EXPORT_SYMBOL(blk_rq_append_bio);

/* Prepare bio for passthrough IO given ITER_BVEC iter */
static int blk_rq_map_user_bvec(struct request *rq, const struct iov_iter *iter)
{
	unsigned int max_bytes = rq->q->limits.max_hw_sectors << SECTOR_SHIFT;
	struct bio *bio;
	int ret;

	if (!iov_iter_count(iter) || iov_iter_count(iter) > max_bytes)
		return -EINVAL;

	/* reuse the bvecs from the iterator instead of allocating new ones */
	bio = blk_rq_map_bio_alloc(rq, 0, GFP_KERNEL);
	if (!bio)
		return -ENOMEM;
	bio_iov_bvec_set(bio, iter);

	ret = blk_rq_append_bio(rq, bio);
	if (ret)
		blk_mq_map_bio_put(bio);
	return ret;
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
	unsigned long align = blk_lim_dma_alignment_and_pad(&q->limits);
	struct bio *bio = NULL;
	struct iov_iter i;
	int ret = -EINVAL;

	if (map_data)
		copy = true;
	else if (iov_iter_alignment(iter) & align)
		copy = true;
	else if (iov_iter_is_bvec(iter))
		map_bvec = true;
	else if (!user_backed_iter(iter))
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
		if (ret) {
			if (ret == -EREMOTEIO)
				ret = -EINVAL;
			goto unmap_rq;
		}
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
	struct iov_iter i;
	int ret = import_ubuf(rq_data_dir(rq), ubuf, len, &i);

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

		if (bio_integrity(bio))
			bio_integrity_unmap_user(bio);

		next_bio = bio;
		bio = bio->bi_next;
		blk_mq_map_bio_put(next_bio);
	}

	return ret;
}
EXPORT_SYMBOL(blk_rq_unmap_user);

/**
 * blk_rq_map_kern - map kernel data to a request, for passthrough requests
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
int blk_rq_map_kern(struct request *rq, void *kbuf, unsigned int len,
		gfp_t gfp_mask)
{
	unsigned long addr = (unsigned long) kbuf;
	struct bio *bio;
	int ret;

	if (len > (queue_max_hw_sectors(rq->q) << SECTOR_SHIFT))
		return -EINVAL;
	if (!len || !kbuf)
		return -EINVAL;

	if (!blk_rq_aligned(rq->q, addr, len) || object_is_on_stack(kbuf))
		bio = bio_copy_kern(kbuf, len, req_op(rq), gfp_mask);
	else
		bio = bio_map_kern(kbuf, len, req_op(rq), gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	ret = blk_rq_append_bio(rq, bio);
	if (unlikely(ret)) {
		bio_uninit(bio);
		kfree(bio);
	}
	return ret;
}
EXPORT_SYMBOL(blk_rq_map_kern);
