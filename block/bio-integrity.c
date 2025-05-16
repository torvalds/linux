// SPDX-License-Identifier: GPL-2.0
/*
 * bio-integrity.c - bio data integrity extensions
 *
 * Copyright (C) 2007, 2008, 2009 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 */

#include <linux/blk-integrity.h>
#include "blk.h"

struct bio_integrity_alloc {
	struct bio_integrity_payload	bip;
	struct bio_vec			bvecs[];
};

/**
 * bio_integrity_free - Free bio integrity payload
 * @bio:	bio containing bip to be freed
 *
 * Description: Free the integrity portion of a bio.
 */
void bio_integrity_free(struct bio *bio)
{
	kfree(bio_integrity(bio));
	bio->bi_integrity = NULL;
	bio->bi_opf &= ~REQ_INTEGRITY;
}

void bio_integrity_init(struct bio *bio, struct bio_integrity_payload *bip,
		struct bio_vec *bvecs, unsigned int nr_vecs)
{
	memset(bip, 0, sizeof(*bip));
	bip->bip_max_vcnt = nr_vecs;
	if (nr_vecs)
		bip->bip_vec = bvecs;

	bio->bi_integrity = bip;
	bio->bi_opf |= REQ_INTEGRITY;
}

/**
 * bio_integrity_alloc - Allocate integrity payload and attach it to bio
 * @bio:	bio to attach integrity metadata to
 * @gfp_mask:	Memory allocation mask
 * @nr_vecs:	Number of integrity metadata scatter-gather elements
 *
 * Description: This function prepares a bio for attaching integrity
 * metadata.  nr_vecs specifies the maximum number of pages containing
 * integrity metadata that can be attached.
 */
struct bio_integrity_payload *bio_integrity_alloc(struct bio *bio,
						  gfp_t gfp_mask,
						  unsigned int nr_vecs)
{
	struct bio_integrity_alloc *bia;

	if (WARN_ON_ONCE(bio_has_crypt_ctx(bio)))
		return ERR_PTR(-EOPNOTSUPP);

	bia = kmalloc(struct_size(bia, bvecs, nr_vecs), gfp_mask);
	if (unlikely(!bia))
		return ERR_PTR(-ENOMEM);
	bio_integrity_init(bio, &bia->bip, bia->bvecs, nr_vecs);
	return &bia->bip;
}
EXPORT_SYMBOL(bio_integrity_alloc);

static void bio_integrity_unpin_bvec(struct bio_vec *bv, int nr_vecs)
{
	int i;

	for (i = 0; i < nr_vecs; i++)
		unpin_user_page(bv[i].bv_page);
}

static void bio_integrity_uncopy_user(struct bio_integrity_payload *bip)
{
	unsigned short orig_nr_vecs = bip->bip_max_vcnt - 1;
	struct bio_vec *orig_bvecs = &bip->bip_vec[1];
	struct bio_vec *bounce_bvec = &bip->bip_vec[0];
	size_t bytes = bounce_bvec->bv_len;
	struct iov_iter orig_iter;
	int ret;

	iov_iter_bvec(&orig_iter, ITER_DEST, orig_bvecs, orig_nr_vecs, bytes);
	ret = copy_to_iter(bvec_virt(bounce_bvec), bytes, &orig_iter);
	WARN_ON_ONCE(ret != bytes);

	bio_integrity_unpin_bvec(orig_bvecs, orig_nr_vecs);
}

/**
 * bio_integrity_unmap_user - Unmap user integrity payload
 * @bio:	bio containing bip to be unmapped
 *
 * Unmap the user mapped integrity portion of a bio.
 */
void bio_integrity_unmap_user(struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);

	if (bip->bip_flags & BIP_COPY_USER) {
		if (bio_data_dir(bio) == READ)
			bio_integrity_uncopy_user(bip);
		kfree(bvec_virt(bip->bip_vec));
		return;
	}

	bio_integrity_unpin_bvec(bip->bip_vec, bip->bip_max_vcnt);
}

/**
 * bio_integrity_add_page - Attach integrity metadata
 * @bio:	bio to update
 * @page:	page containing integrity metadata
 * @len:	number of bytes of integrity metadata in page
 * @offset:	start offset within page
 *
 * Description: Attach a page containing integrity metadata to bio.
 */
int bio_integrity_add_page(struct bio *bio, struct page *page,
			   unsigned int len, unsigned int offset)
{
	struct request_queue *q = bdev_get_queue(bio->bi_bdev);
	struct bio_integrity_payload *bip = bio_integrity(bio);

	if (bip->bip_vcnt > 0) {
		struct bio_vec *bv = &bip->bip_vec[bip->bip_vcnt - 1];
		bool same_page = false;

		if (bvec_try_merge_hw_page(q, bv, page, len, offset,
					   &same_page)) {
			bip->bip_iter.bi_size += len;
			return len;
		}

		if (bip->bip_vcnt >=
		    min(bip->bip_max_vcnt, queue_max_integrity_segments(q)))
			return 0;

		/*
		 * If the queue doesn't support SG gaps and adding this segment
		 * would create a gap, disallow it.
		 */
		if (bvec_gap_to_prev(&q->limits, bv, offset))
			return 0;
	}

	bvec_set_page(&bip->bip_vec[bip->bip_vcnt], page, len, offset);
	bip->bip_vcnt++;
	bip->bip_iter.bi_size += len;

	return len;
}
EXPORT_SYMBOL(bio_integrity_add_page);

static int bio_integrity_copy_user(struct bio *bio, struct bio_vec *bvec,
				   int nr_vecs, unsigned int len,
				   unsigned int direction)
{
	bool write = direction == ITER_SOURCE;
	struct bio_integrity_payload *bip;
	struct iov_iter iter;
	void *buf;
	int ret;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (write) {
		iov_iter_bvec(&iter, direction, bvec, nr_vecs, len);
		if (!copy_from_iter_full(buf, len, &iter)) {
			ret = -EFAULT;
			goto free_buf;
		}

		bip = bio_integrity_alloc(bio, GFP_KERNEL, 1);
	} else {
		memset(buf, 0, len);

		/*
		 * We need to preserve the original bvec and the number of vecs
		 * in it for completion handling
		 */
		bip = bio_integrity_alloc(bio, GFP_KERNEL, nr_vecs + 1);
	}

	if (IS_ERR(bip)) {
		ret = PTR_ERR(bip);
		goto free_buf;
	}

	if (write)
		bio_integrity_unpin_bvec(bvec, nr_vecs);
	else
		memcpy(&bip->bip_vec[1], bvec, nr_vecs * sizeof(*bvec));

	ret = bio_integrity_add_page(bio, virt_to_page(buf), len,
				     offset_in_page(buf));
	if (ret != len) {
		ret = -ENOMEM;
		goto free_bip;
	}

	bip->bip_flags |= BIP_COPY_USER;
	bip->bip_vcnt = nr_vecs;
	return 0;
free_bip:
	bio_integrity_free(bio);
free_buf:
	kfree(buf);
	return ret;
}

static int bio_integrity_init_user(struct bio *bio, struct bio_vec *bvec,
				   int nr_vecs, unsigned int len)
{
	struct bio_integrity_payload *bip;

	bip = bio_integrity_alloc(bio, GFP_KERNEL, nr_vecs);
	if (IS_ERR(bip))
		return PTR_ERR(bip);

	memcpy(bip->bip_vec, bvec, nr_vecs * sizeof(*bvec));
	bip->bip_iter.bi_size = len;
	bip->bip_vcnt = nr_vecs;
	return 0;
}

static unsigned int bvec_from_pages(struct bio_vec *bvec, struct page **pages,
				    int nr_vecs, ssize_t bytes, ssize_t offset)
{
	unsigned int nr_bvecs = 0;
	int i, j;

	for (i = 0; i < nr_vecs; i = j) {
		size_t size = min_t(size_t, bytes, PAGE_SIZE - offset);
		struct folio *folio = page_folio(pages[i]);

		bytes -= size;
		for (j = i + 1; j < nr_vecs; j++) {
			size_t next = min_t(size_t, PAGE_SIZE, bytes);

			if (page_folio(pages[j]) != folio ||
			    pages[j] != pages[j - 1] + 1)
				break;
			unpin_user_page(pages[j]);
			size += next;
			bytes -= next;
		}

		bvec_set_page(&bvec[nr_bvecs], pages[i], size, offset);
		offset = 0;
		nr_bvecs++;
	}

	return nr_bvecs;
}

int bio_integrity_map_user(struct bio *bio, struct iov_iter *iter)
{
	struct request_queue *q = bdev_get_queue(bio->bi_bdev);
	unsigned int align = blk_lim_dma_alignment_and_pad(&q->limits);
	struct page *stack_pages[UIO_FASTIOV], **pages = stack_pages;
	struct bio_vec stack_vec[UIO_FASTIOV], *bvec = stack_vec;
	size_t offset, bytes = iter->count;
	unsigned int direction, nr_bvecs;
	int ret, nr_vecs;
	bool copy;

	if (bio_integrity(bio))
		return -EINVAL;
	if (bytes >> SECTOR_SHIFT > queue_max_hw_sectors(q))
		return -E2BIG;

	if (bio_data_dir(bio) == READ)
		direction = ITER_DEST;
	else
		direction = ITER_SOURCE;

	nr_vecs = iov_iter_npages(iter, BIO_MAX_VECS + 1);
	if (nr_vecs > BIO_MAX_VECS)
		return -E2BIG;
	if (nr_vecs > UIO_FASTIOV) {
		bvec = kcalloc(nr_vecs, sizeof(*bvec), GFP_KERNEL);
		if (!bvec)
			return -ENOMEM;
		pages = NULL;
	}

	copy = !iov_iter_is_aligned(iter, align, align);
	ret = iov_iter_extract_pages(iter, &pages, bytes, nr_vecs, 0, &offset);
	if (unlikely(ret < 0))
		goto free_bvec;

	nr_bvecs = bvec_from_pages(bvec, pages, nr_vecs, bytes, offset);
	if (pages != stack_pages)
		kvfree(pages);
	if (nr_bvecs > queue_max_integrity_segments(q))
		copy = true;

	if (copy)
		ret = bio_integrity_copy_user(bio, bvec, nr_bvecs, bytes,
					      direction);
	else
		ret = bio_integrity_init_user(bio, bvec, nr_bvecs, bytes);
	if (ret)
		goto release_pages;
	if (bvec != stack_vec)
		kfree(bvec);

	return 0;

release_pages:
	bio_integrity_unpin_bvec(bvec, nr_bvecs);
free_bvec:
	if (bvec != stack_vec)
		kfree(bvec);
	return ret;
}

static void bio_uio_meta_to_bip(struct bio *bio, struct uio_meta *meta)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);

	if (meta->flags & IO_INTEGRITY_CHK_GUARD)
		bip->bip_flags |= BIP_CHECK_GUARD;
	if (meta->flags & IO_INTEGRITY_CHK_APPTAG)
		bip->bip_flags |= BIP_CHECK_APPTAG;
	if (meta->flags & IO_INTEGRITY_CHK_REFTAG)
		bip->bip_flags |= BIP_CHECK_REFTAG;

	bip->app_tag = meta->app_tag;
}

int bio_integrity_map_iter(struct bio *bio, struct uio_meta *meta)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);
	unsigned int integrity_bytes;
	int ret;
	struct iov_iter it;

	if (!bi)
		return -EINVAL;
	/*
	 * original meta iterator can be bigger.
	 * process integrity info corresponding to current data buffer only.
	 */
	it = meta->iter;
	integrity_bytes = bio_integrity_bytes(bi, bio_sectors(bio));
	if (it.count < integrity_bytes)
		return -EINVAL;

	/* should fit into two bytes */
	BUILD_BUG_ON(IO_INTEGRITY_VALID_FLAGS >= (1 << 16));

	if (meta->flags && (meta->flags & ~IO_INTEGRITY_VALID_FLAGS))
		return -EINVAL;

	it.count = integrity_bytes;
	ret = bio_integrity_map_user(bio, &it);
	if (!ret) {
		bio_uio_meta_to_bip(bio, meta);
		bip_set_seed(bio_integrity(bio), meta->seed);
		iov_iter_advance(&meta->iter, integrity_bytes);
		meta->seed += bio_integrity_intervals(bi, bio_sectors(bio));
	}
	return ret;
}

/**
 * bio_integrity_advance - Advance integrity vector
 * @bio:	bio whose integrity vector to update
 * @bytes_done:	number of data bytes that have been completed
 *
 * Description: This function calculates how many integrity bytes the
 * number of completed data bytes correspond to and advances the
 * integrity vector accordingly.
 */
void bio_integrity_advance(struct bio *bio, unsigned int bytes_done)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);
	unsigned bytes = bio_integrity_bytes(bi, bytes_done >> 9);

	bip->bip_iter.bi_sector += bio_integrity_intervals(bi, bytes_done >> 9);
	bvec_iter_advance(bip->bip_vec, &bip->bip_iter, bytes);
}

/**
 * bio_integrity_trim - Trim integrity vector
 * @bio:	bio whose integrity vector to update
 *
 * Description: Used to trim the integrity vector in a cloned bio.
 */
void bio_integrity_trim(struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);

	bip->bip_iter.bi_size = bio_integrity_bytes(bi, bio_sectors(bio));
}
EXPORT_SYMBOL(bio_integrity_trim);

/**
 * bio_integrity_clone - Callback for cloning bios with integrity metadata
 * @bio:	New bio
 * @bio_src:	Original bio
 * @gfp_mask:	Memory allocation mask
 *
 * Description:	Called to allocate a bip when cloning a bio
 */
int bio_integrity_clone(struct bio *bio, struct bio *bio_src,
			gfp_t gfp_mask)
{
	struct bio_integrity_payload *bip_src = bio_integrity(bio_src);
	struct bio_integrity_payload *bip;

	BUG_ON(bip_src == NULL);

	bip = bio_integrity_alloc(bio, gfp_mask, 0);
	if (IS_ERR(bip))
		return PTR_ERR(bip);

	bip->bip_vec = bip_src->bip_vec;
	bip->bip_iter = bip_src->bip_iter;
	bip->bip_flags = bip_src->bip_flags & BIP_CLONE_FLAGS;
	bip->app_tag = bip_src->app_tag;

	return 0;
}
