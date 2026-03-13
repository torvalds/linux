// SPDX-License-Identifier: GPL-2.0
/*
 * t10_pi.c - Functions for generating and verifying T10 Protection
 *	      Information.
 */

#include <linux/t10-pi.h>
#include <linux/blk-integrity.h>
#include <linux/crc-t10dif.h>
#include <linux/crc64.h>
#include <net/checksum.h>
#include <linux/unaligned.h>
#include "blk.h"

#define APP_TAG_ESCAPE 0xffff
#define REF_TAG_ESCAPE 0xffffffff

/*
 * This union is used for onstack allocations when the pi field is split across
 * segments. blk_validate_integrity_limits() guarantees pi_tuple_size matches
 * the sizeof one of these two types.
 */
union pi_tuple {
	struct crc64_pi_tuple	crc64_pi;
	struct t10_pi_tuple	t10_pi;
};

struct blk_integrity_iter {
	struct bio			*bio;
	struct bio_integrity_payload	*bip;
	struct blk_integrity		*bi;
	struct bvec_iter		data_iter;
	struct bvec_iter		prot_iter;
	unsigned int			interval_remaining;
	u64				seed;
	u64				csum;
};

static void blk_calculate_guard(struct blk_integrity_iter *iter, void *data,
				unsigned int len)
{
	switch (iter->bi->csum_type) {
	case BLK_INTEGRITY_CSUM_CRC64:
		iter->csum = crc64_nvme(iter->csum, data, len);
		break;
	case BLK_INTEGRITY_CSUM_CRC:
		iter->csum = crc_t10dif_update(iter->csum, data, len);
		break;
	case BLK_INTEGRITY_CSUM_IP:
		iter->csum = (__force u32)csum_partial(data, len,
						(__force __wsum)iter->csum);
		break;
	default:
		WARN_ON_ONCE(1);
		iter->csum = U64_MAX;
		break;
	}
}

static void blk_integrity_csum_finish(struct blk_integrity_iter *iter)
{
	switch (iter->bi->csum_type) {
	case BLK_INTEGRITY_CSUM_IP:
		iter->csum = (__force u16)csum_fold((__force __wsum)iter->csum);
		break;
	default:
		break;
	}
}

/*
 * Update the csum for formats that have metadata padding in front of the data
 * integrity field
 */
static void blk_integrity_csum_offset(struct blk_integrity_iter *iter)
{
	unsigned int offset = iter->bi->pi_offset;
	struct bio_vec *bvec = iter->bip->bip_vec;

	while (offset > 0) {
		struct bio_vec pbv = bvec_iter_bvec(bvec, iter->prot_iter);
		unsigned int len = min(pbv.bv_len, offset);
		void *prot_buf = bvec_kmap_local(&pbv);

		blk_calculate_guard(iter, prot_buf, len);
		kunmap_local(prot_buf);
		offset -= len;
		bvec_iter_advance_single(bvec, &iter->prot_iter, len);
	}
	blk_integrity_csum_finish(iter);
}

static void blk_integrity_copy_from_tuple(struct bio_integrity_payload *bip,
					  struct bvec_iter *iter, void *tuple,
					  unsigned int tuple_size)
{
	while (tuple_size) {
		struct bio_vec pbv = bvec_iter_bvec(bip->bip_vec, *iter);
		unsigned int len = min(tuple_size, pbv.bv_len);
		void *prot_buf = bvec_kmap_local(&pbv);

		memcpy(prot_buf, tuple, len);
		kunmap_local(prot_buf);
		bvec_iter_advance_single(bip->bip_vec, iter, len);
		tuple_size -= len;
		tuple += len;
	}
}

static void blk_integrity_copy_to_tuple(struct bio_integrity_payload *bip,
					struct bvec_iter *iter, void *tuple,
					unsigned int tuple_size)
{
	while (tuple_size) {
		struct bio_vec pbv = bvec_iter_bvec(bip->bip_vec, *iter);
		unsigned int len = min(tuple_size, pbv.bv_len);
		void *prot_buf = bvec_kmap_local(&pbv);

		memcpy(tuple, prot_buf, len);
		kunmap_local(prot_buf);
		bvec_iter_advance_single(bip->bip_vec, iter, len);
		tuple_size -= len;
		tuple += len;
	}
}

static bool ext_pi_ref_escape(const u8 ref_tag[6])
{
	static const u8 ref_escape[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	return memcmp(ref_tag, ref_escape, sizeof(ref_escape)) == 0;
}

static blk_status_t blk_verify_ext_pi(struct blk_integrity_iter *iter,
				      struct crc64_pi_tuple *pi)
{
	u64 seed = lower_48_bits(iter->seed);
	u64 guard = get_unaligned_be64(&pi->guard_tag);
	u64 ref = get_unaligned_be48(pi->ref_tag);
	u16 app = get_unaligned_be16(&pi->app_tag);

	if (iter->bi->flags & BLK_INTEGRITY_REF_TAG) {
		if (app == APP_TAG_ESCAPE)
			return BLK_STS_OK;
		if (ref != seed) {
			pr_err("%s: ref tag error at location %llu (rcvd %llu)\n",
				iter->bio->bi_bdev->bd_disk->disk_name, seed,
				ref);
			return BLK_STS_PROTECTION;
		}
	} else if (app == APP_TAG_ESCAPE && ext_pi_ref_escape(pi->ref_tag)) {
		return BLK_STS_OK;
	}

	if (guard != iter->csum) {
		pr_err("%s: guard tag error at sector %llu (rcvd %016llx, want %016llx)\n",
			iter->bio->bi_bdev->bd_disk->disk_name, iter->seed,
			guard, iter->csum);
		return BLK_STS_PROTECTION;
	}

	return BLK_STS_OK;
}

static blk_status_t blk_verify_pi(struct blk_integrity_iter *iter,
				      struct t10_pi_tuple *pi, u16 guard)
{
	u32 seed = lower_32_bits(iter->seed);
	u32 ref = get_unaligned_be32(&pi->ref_tag);
	u16 app = get_unaligned_be16(&pi->app_tag);

	if (iter->bi->flags & BLK_INTEGRITY_REF_TAG) {
		if (app == APP_TAG_ESCAPE)
			return BLK_STS_OK;
		if (ref != seed) {
			pr_err("%s: ref tag error at location %u (rcvd %u)\n",
				iter->bio->bi_bdev->bd_disk->disk_name, seed,
				ref);
			return BLK_STS_PROTECTION;
		}
	} else if (app == APP_TAG_ESCAPE && ref == REF_TAG_ESCAPE) {
		return BLK_STS_OK;
	}

	if (guard != (u16)iter->csum) {
		pr_err("%s: guard tag error at sector %llu (rcvd %04x, want %04x)\n",
			iter->bio->bi_bdev->bd_disk->disk_name, iter->seed,
			guard, (u16)iter->csum);
		return BLK_STS_PROTECTION;
	}

	return BLK_STS_OK;
}

static blk_status_t blk_verify_t10_pi(struct blk_integrity_iter *iter,
				      struct t10_pi_tuple *pi)
{
	u16 guard = get_unaligned_be16(&pi->guard_tag);

	return blk_verify_pi(iter, pi, guard);
}

static blk_status_t blk_verify_ip_pi(struct blk_integrity_iter *iter,
				     struct t10_pi_tuple *pi)
{
	u16 guard = get_unaligned((u16 *)&pi->guard_tag);

	return blk_verify_pi(iter, pi, guard);
}

static blk_status_t blk_integrity_verify(struct blk_integrity_iter *iter,
					 union pi_tuple *tuple)
{
	switch (iter->bi->csum_type) {
	case BLK_INTEGRITY_CSUM_CRC64:
		return blk_verify_ext_pi(iter, &tuple->crc64_pi);
	case BLK_INTEGRITY_CSUM_CRC:
		return blk_verify_t10_pi(iter, &tuple->t10_pi);
	case BLK_INTEGRITY_CSUM_IP:
		return blk_verify_ip_pi(iter, &tuple->t10_pi);
	default:
		return BLK_STS_OK;
	}
}

static void blk_set_ext_pi(struct blk_integrity_iter *iter,
			   struct crc64_pi_tuple *pi)
{
	put_unaligned_be64(iter->csum, &pi->guard_tag);
	put_unaligned_be16(0, &pi->app_tag);
	put_unaligned_be48(iter->seed, &pi->ref_tag);
}

static void blk_set_pi(struct blk_integrity_iter *iter,
		       struct t10_pi_tuple *pi, __be16 csum)
{
	put_unaligned(csum, &pi->guard_tag);
	put_unaligned_be16(0, &pi->app_tag);
	put_unaligned_be32(iter->seed, &pi->ref_tag);
}

static void blk_set_t10_pi(struct blk_integrity_iter *iter,
			   struct t10_pi_tuple *pi)
{
	blk_set_pi(iter, pi, cpu_to_be16((u16)iter->csum));
}

static void blk_set_ip_pi(struct blk_integrity_iter *iter,
			  struct t10_pi_tuple *pi)
{
	blk_set_pi(iter, pi, (__force __be16)(u16)iter->csum);
}

static void blk_integrity_set(struct blk_integrity_iter *iter,
			      union pi_tuple *tuple)
{
	switch (iter->bi->csum_type) {
	case BLK_INTEGRITY_CSUM_CRC64:
		return blk_set_ext_pi(iter, &tuple->crc64_pi);
	case BLK_INTEGRITY_CSUM_CRC:
		return blk_set_t10_pi(iter, &tuple->t10_pi);
	case BLK_INTEGRITY_CSUM_IP:
		return blk_set_ip_pi(iter, &tuple->t10_pi);
	default:
		WARN_ON_ONCE(1);
		return;
	}
}

static blk_status_t blk_integrity_interval(struct blk_integrity_iter *iter,
					   bool verify)
{
	blk_status_t ret = BLK_STS_OK;
	union pi_tuple tuple;
	void *ptuple = &tuple;
	struct bio_vec pbv;

	blk_integrity_csum_offset(iter);
	pbv = bvec_iter_bvec(iter->bip->bip_vec, iter->prot_iter);
	if (pbv.bv_len >= iter->bi->pi_tuple_size) {
		ptuple = bvec_kmap_local(&pbv);
		bvec_iter_advance_single(iter->bip->bip_vec, &iter->prot_iter,
				iter->bi->metadata_size - iter->bi->pi_offset);
	} else if (verify) {
		blk_integrity_copy_to_tuple(iter->bip, &iter->prot_iter,
				ptuple, iter->bi->pi_tuple_size);
	}

	if (verify)
		ret = blk_integrity_verify(iter, ptuple);
	else
		blk_integrity_set(iter, ptuple);

	if (likely(ptuple != &tuple)) {
		kunmap_local(ptuple);
	} else if (!verify) {
		blk_integrity_copy_from_tuple(iter->bip, &iter->prot_iter,
				ptuple, iter->bi->pi_tuple_size);
	}

	iter->interval_remaining = 1 << iter->bi->interval_exp;
	iter->csum = 0;
	iter->seed++;
	return ret;
}

static blk_status_t blk_integrity_iterate(struct bio *bio,
					  struct bvec_iter *data_iter,
					  bool verify)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct blk_integrity_iter iter = {
		.bio = bio,
		.bip = bip,
		.bi = bi,
		.data_iter = *data_iter,
		.prot_iter = bip->bip_iter,
		.interval_remaining = 1 << bi->interval_exp,
		.seed = data_iter->bi_sector,
		.csum = 0,
	};
	blk_status_t ret = BLK_STS_OK;

	while (iter.data_iter.bi_size && ret == BLK_STS_OK) {
		struct bio_vec bv = bvec_iter_bvec(iter.bio->bi_io_vec,
						   iter.data_iter);
		void *kaddr = bvec_kmap_local(&bv);
		void *data = kaddr;
		unsigned int len;

		bvec_iter_advance_single(iter.bio->bi_io_vec, &iter.data_iter,
					 bv.bv_len);
		while (bv.bv_len && ret == BLK_STS_OK) {
			len = min(iter.interval_remaining, bv.bv_len);
			blk_calculate_guard(&iter, data, len);
			bv.bv_len -= len;
			data += len;
			iter.interval_remaining -= len;
			if (!iter.interval_remaining)
				ret = blk_integrity_interval(&iter, verify);
		}
		kunmap_local(kaddr);
	}

	return ret;
}

void bio_integrity_generate(struct bio *bio)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);

	switch (bi->csum_type) {
	case BLK_INTEGRITY_CSUM_CRC64:
	case BLK_INTEGRITY_CSUM_CRC:
	case BLK_INTEGRITY_CSUM_IP:
		blk_integrity_iterate(bio, &bio->bi_iter, false);
		break;
	default:
		break;
	}
}

blk_status_t bio_integrity_verify(struct bio *bio, struct bvec_iter *saved_iter)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);

	switch (bi->csum_type) {
	case BLK_INTEGRITY_CSUM_CRC64:
	case BLK_INTEGRITY_CSUM_CRC:
	case BLK_INTEGRITY_CSUM_IP:
		return blk_integrity_iterate(bio, saved_iter, true);
	default:
		break;
	}

	return BLK_STS_OK;
}

/*
 * Advance @iter past the protection offset for protection formats that
 * contain front padding on the metadata region.
 */
static void blk_pi_advance_offset(struct blk_integrity *bi,
				  struct bio_integrity_payload *bip,
				  struct bvec_iter *iter)
{
	unsigned int offset = bi->pi_offset;

	while (offset > 0) {
		struct bio_vec bv = mp_bvec_iter_bvec(bip->bip_vec, *iter);
		unsigned int len = min(bv.bv_len, offset);

		bvec_iter_advance_single(bip->bip_vec, iter, len);
		offset -= len;
	}
}

static void *blk_tuple_remap_begin(union pi_tuple *tuple,
				   struct blk_integrity *bi,
				   struct bio_integrity_payload *bip,
				   struct bvec_iter *iter)
{
	struct bvec_iter titer;
	struct bio_vec pbv;

	blk_pi_advance_offset(bi, bip, iter);
	pbv = bvec_iter_bvec(bip->bip_vec, *iter);
	if (likely(pbv.bv_len >= bi->pi_tuple_size))
		return bvec_kmap_local(&pbv);

	/*
	 * We need to preserve the state of the original iter for the
	 * copy_from_tuple at the end, so make a temp iter for here.
	 */
	titer = *iter;
	blk_integrity_copy_to_tuple(bip, &titer, tuple, bi->pi_tuple_size);
	return tuple;
}

static void blk_tuple_remap_end(union pi_tuple *tuple, void *ptuple,
				struct blk_integrity *bi,
				struct bio_integrity_payload *bip,
				struct bvec_iter *iter)
{
	unsigned int len = bi->metadata_size - bi->pi_offset;

	if (likely(ptuple != tuple)) {
		kunmap_local(ptuple);
	} else {
		blk_integrity_copy_from_tuple(bip, iter, ptuple,
				bi->pi_tuple_size);
		len -= bi->pi_tuple_size;
	}

	bvec_iter_advance(bip->bip_vec, iter, len);
}

static void blk_set_ext_unmap_ref(struct crc64_pi_tuple *pi, u64 virt,
				  u64 ref_tag)
{
	u64 ref = get_unaligned_be48(&pi->ref_tag);

	if (ref == lower_48_bits(ref_tag) && ref != lower_48_bits(virt))
		put_unaligned_be48(virt, pi->ref_tag);
}

static void blk_set_t10_unmap_ref(struct t10_pi_tuple *pi, u32 virt,
				  u32 ref_tag)
{
	u32 ref = get_unaligned_be32(&pi->ref_tag);

	if (ref == ref_tag && ref != virt)
		put_unaligned_be32(virt, &pi->ref_tag);
}

static void blk_reftag_remap_complete(struct blk_integrity *bi,
				      union pi_tuple *tuple, u64 virt, u64 ref)
{
	switch (bi->csum_type) {
	case BLK_INTEGRITY_CSUM_CRC64:
		blk_set_ext_unmap_ref(&tuple->crc64_pi, virt, ref);
		break;
	case BLK_INTEGRITY_CSUM_CRC:
	case BLK_INTEGRITY_CSUM_IP:
		blk_set_t10_unmap_ref(&tuple->t10_pi, virt, ref);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static void blk_set_ext_map_ref(struct crc64_pi_tuple *pi, u64 virt,
				u64 ref_tag)
{
	u64 ref = get_unaligned_be48(&pi->ref_tag);

	if (ref == lower_48_bits(virt) && ref != ref_tag)
		put_unaligned_be48(ref_tag, pi->ref_tag);
}

static void blk_set_t10_map_ref(struct t10_pi_tuple *pi, u32 virt, u32 ref_tag)
{
	u32 ref = get_unaligned_be32(&pi->ref_tag);

	if (ref == virt && ref != ref_tag)
		put_unaligned_be32(ref_tag, &pi->ref_tag);
}

static void blk_reftag_remap_prepare(struct blk_integrity *bi,
				     union pi_tuple *tuple,
				     u64 virt, u64 ref)
{
	switch (bi->csum_type) {
	case BLK_INTEGRITY_CSUM_CRC64:
		blk_set_ext_map_ref(&tuple->crc64_pi, virt, ref);
		break;
	case BLK_INTEGRITY_CSUM_CRC:
	case BLK_INTEGRITY_CSUM_IP:
		blk_set_t10_map_ref(&tuple->t10_pi, virt, ref);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static void __blk_reftag_remap(struct bio *bio, struct blk_integrity *bi,
			       unsigned *intervals, u64 *ref, bool prep)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct bvec_iter iter = bip->bip_iter;
	u64 virt = bip_get_seed(bip);
	union pi_tuple *ptuple;
	union pi_tuple tuple;

	if (prep && bip->bip_flags & BIP_MAPPED_INTEGRITY) {
		*ref += bio->bi_iter.bi_size >> bi->interval_exp;
		return;
	}

	while (iter.bi_size && *intervals) {
		ptuple = blk_tuple_remap_begin(&tuple, bi, bip, &iter);

		if (prep)
			blk_reftag_remap_prepare(bi, ptuple, virt, *ref);
		else
			blk_reftag_remap_complete(bi, ptuple, virt, *ref);

		blk_tuple_remap_end(&tuple, ptuple, bi, bip, &iter);
		(*intervals)--;
		(*ref)++;
		virt++;
	}

	if (prep)
		bip->bip_flags |= BIP_MAPPED_INTEGRITY;
}

static void blk_integrity_remap(struct request *rq, unsigned int nr_bytes,
				bool prep)
{
	struct blk_integrity *bi = &rq->q->limits.integrity;
	u64 ref = blk_rq_pos(rq) >> (bi->interval_exp - SECTOR_SHIFT);
	unsigned intervals = nr_bytes >> bi->interval_exp;
	struct bio *bio;

	if (!(bi->flags & BLK_INTEGRITY_REF_TAG))
		return;

	__rq_for_each_bio(bio, rq) {
		__blk_reftag_remap(bio, bi, &intervals, &ref, prep);
		if (!intervals)
			break;
	}
}

void blk_integrity_prepare(struct request *rq)
{
	blk_integrity_remap(rq, blk_rq_bytes(rq), true);
}

void blk_integrity_complete(struct request *rq, unsigned int nr_bytes)
{
	blk_integrity_remap(rq, nr_bytes, false);
}
