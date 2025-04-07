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

struct blk_integrity_iter {
	void			*prot_buf;
	void			*data_buf;
	sector_t		seed;
	unsigned int		data_size;
	unsigned short		interval;
	const char		*disk_name;
};

static __be16 t10_pi_csum(__be16 csum, void *data, unsigned int len,
		unsigned char csum_type)
{
	if (csum_type == BLK_INTEGRITY_CSUM_IP)
		return (__force __be16)ip_compute_csum(data, len);
	return cpu_to_be16(crc_t10dif_update(be16_to_cpu(csum), data, len));
}

/*
 * Type 1 and Type 2 protection use the same format: 16 bit guard tag,
 * 16 bit app tag, 32 bit reference tag. Type 3 does not define the ref
 * tag.
 */
static void t10_pi_generate(struct blk_integrity_iter *iter,
		struct blk_integrity *bi)
{
	u8 offset = bi->pi_offset;
	unsigned int i;

	for (i = 0 ; i < iter->data_size ; i += iter->interval) {
		struct t10_pi_tuple *pi = iter->prot_buf + offset;

		pi->guard_tag = t10_pi_csum(0, iter->data_buf, iter->interval,
				bi->csum_type);
		if (offset)
			pi->guard_tag = t10_pi_csum(pi->guard_tag,
					iter->prot_buf, offset, bi->csum_type);
		pi->app_tag = 0;

		if (bi->flags & BLK_INTEGRITY_REF_TAG)
			pi->ref_tag = cpu_to_be32(lower_32_bits(iter->seed));
		else
			pi->ref_tag = 0;

		iter->data_buf += iter->interval;
		iter->prot_buf += bi->tuple_size;
		iter->seed++;
	}
}

static blk_status_t t10_pi_verify(struct blk_integrity_iter *iter,
		struct blk_integrity *bi)
{
	u8 offset = bi->pi_offset;
	unsigned int i;

	for (i = 0 ; i < iter->data_size ; i += iter->interval) {
		struct t10_pi_tuple *pi = iter->prot_buf + offset;
		__be16 csum;

		if (bi->flags & BLK_INTEGRITY_REF_TAG) {
			if (pi->app_tag == T10_PI_APP_ESCAPE)
				goto next;

			if (be32_to_cpu(pi->ref_tag) !=
			    lower_32_bits(iter->seed)) {
				pr_err("%s: ref tag error at location %llu " \
				       "(rcvd %u)\n", iter->disk_name,
				       (unsigned long long)
				       iter->seed, be32_to_cpu(pi->ref_tag));
				return BLK_STS_PROTECTION;
			}
		} else {
			if (pi->app_tag == T10_PI_APP_ESCAPE &&
			    pi->ref_tag == T10_PI_REF_ESCAPE)
				goto next;
		}

		csum = t10_pi_csum(0, iter->data_buf, iter->interval,
				bi->csum_type);
		if (offset)
			csum = t10_pi_csum(csum, iter->prot_buf, offset,
					bi->csum_type);

		if (pi->guard_tag != csum) {
			pr_err("%s: guard tag error at sector %llu " \
			       "(rcvd %04x, want %04x)\n", iter->disk_name,
			       (unsigned long long)iter->seed,
			       be16_to_cpu(pi->guard_tag), be16_to_cpu(csum));
			return BLK_STS_PROTECTION;
		}

next:
		iter->data_buf += iter->interval;
		iter->prot_buf += bi->tuple_size;
		iter->seed++;
	}

	return BLK_STS_OK;
}

/**
 * t10_pi_type1_prepare - prepare PI prior submitting request to device
 * @rq:              request with PI that should be prepared
 *
 * For Type 1/Type 2, the virtual start sector is the one that was
 * originally submitted by the block layer for the ref_tag usage. Due to
 * partitioning, MD/DM cloning, etc. the actual physical start sector is
 * likely to be different. Remap protection information to match the
 * physical LBA.
 */
static void t10_pi_type1_prepare(struct request *rq)
{
	struct blk_integrity *bi = &rq->q->limits.integrity;
	const int tuple_sz = bi->tuple_size;
	u32 ref_tag = t10_pi_ref_tag(rq);
	u8 offset = bi->pi_offset;
	struct bio *bio;

	__rq_for_each_bio(bio, rq) {
		struct bio_integrity_payload *bip = bio_integrity(bio);
		u32 virt = bip_get_seed(bip) & 0xffffffff;
		struct bio_vec iv;
		struct bvec_iter iter;

		/* Already remapped? */
		if (bip->bip_flags & BIP_MAPPED_INTEGRITY)
			break;

		bip_for_each_vec(iv, bip, iter) {
			unsigned int j;
			void *p;

			p = bvec_kmap_local(&iv);
			for (j = 0; j < iv.bv_len; j += tuple_sz) {
				struct t10_pi_tuple *pi = p + offset;

				if (be32_to_cpu(pi->ref_tag) == virt)
					pi->ref_tag = cpu_to_be32(ref_tag);
				virt++;
				ref_tag++;
				p += tuple_sz;
			}
			kunmap_local(p);
		}

		bip->bip_flags |= BIP_MAPPED_INTEGRITY;
	}
}

/**
 * t10_pi_type1_complete - prepare PI prior returning request to the blk layer
 * @rq:              request with PI that should be prepared
 * @nr_bytes:        total bytes to prepare
 *
 * For Type 1/Type 2, the virtual start sector is the one that was
 * originally submitted by the block layer for the ref_tag usage. Due to
 * partitioning, MD/DM cloning, etc. the actual physical start sector is
 * likely to be different. Since the physical start sector was submitted
 * to the device, we should remap it back to virtual values expected by the
 * block layer.
 */
static void t10_pi_type1_complete(struct request *rq, unsigned int nr_bytes)
{
	struct blk_integrity *bi = &rq->q->limits.integrity;
	unsigned intervals = nr_bytes >> bi->interval_exp;
	const int tuple_sz = bi->tuple_size;
	u32 ref_tag = t10_pi_ref_tag(rq);
	u8 offset = bi->pi_offset;
	struct bio *bio;

	__rq_for_each_bio(bio, rq) {
		struct bio_integrity_payload *bip = bio_integrity(bio);
		u32 virt = bip_get_seed(bip) & 0xffffffff;
		struct bio_vec iv;
		struct bvec_iter iter;

		bip_for_each_vec(iv, bip, iter) {
			unsigned int j;
			void *p;

			p = bvec_kmap_local(&iv);
			for (j = 0; j < iv.bv_len && intervals; j += tuple_sz) {
				struct t10_pi_tuple *pi = p + offset;

				if (be32_to_cpu(pi->ref_tag) == ref_tag)
					pi->ref_tag = cpu_to_be32(virt);
				virt++;
				ref_tag++;
				intervals--;
				p += tuple_sz;
			}
			kunmap_local(p);
		}
	}
}

static __be64 ext_pi_crc64(u64 crc, void *data, unsigned int len)
{
	return cpu_to_be64(crc64_nvme(crc, data, len));
}

static void ext_pi_crc64_generate(struct blk_integrity_iter *iter,
		struct blk_integrity *bi)
{
	u8 offset = bi->pi_offset;
	unsigned int i;

	for (i = 0 ; i < iter->data_size ; i += iter->interval) {
		struct crc64_pi_tuple *pi = iter->prot_buf + offset;

		pi->guard_tag = ext_pi_crc64(0, iter->data_buf, iter->interval);
		if (offset)
			pi->guard_tag = ext_pi_crc64(be64_to_cpu(pi->guard_tag),
					iter->prot_buf, offset);
		pi->app_tag = 0;

		if (bi->flags & BLK_INTEGRITY_REF_TAG)
			put_unaligned_be48(iter->seed, pi->ref_tag);
		else
			put_unaligned_be48(0ULL, pi->ref_tag);

		iter->data_buf += iter->interval;
		iter->prot_buf += bi->tuple_size;
		iter->seed++;
	}
}

static bool ext_pi_ref_escape(const u8 ref_tag[6])
{
	static const u8 ref_escape[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	return memcmp(ref_tag, ref_escape, sizeof(ref_escape)) == 0;
}

static blk_status_t ext_pi_crc64_verify(struct blk_integrity_iter *iter,
		struct blk_integrity *bi)
{
	u8 offset = bi->pi_offset;
	unsigned int i;

	for (i = 0; i < iter->data_size; i += iter->interval) {
		struct crc64_pi_tuple *pi = iter->prot_buf + offset;
		u64 ref, seed;
		__be64 csum;

		if (bi->flags & BLK_INTEGRITY_REF_TAG) {
			if (pi->app_tag == T10_PI_APP_ESCAPE)
				goto next;

			ref = get_unaligned_be48(pi->ref_tag);
			seed = lower_48_bits(iter->seed);
			if (ref != seed) {
				pr_err("%s: ref tag error at location %llu (rcvd %llu)\n",
					iter->disk_name, seed, ref);
				return BLK_STS_PROTECTION;
			}
		} else {
			if (pi->app_tag == T10_PI_APP_ESCAPE &&
			    ext_pi_ref_escape(pi->ref_tag))
				goto next;
		}

		csum = ext_pi_crc64(0, iter->data_buf, iter->interval);
		if (offset)
			csum = ext_pi_crc64(be64_to_cpu(csum), iter->prot_buf,
					    offset);

		if (pi->guard_tag != csum) {
			pr_err("%s: guard tag error at sector %llu " \
			       "(rcvd %016llx, want %016llx)\n",
				iter->disk_name, (unsigned long long)iter->seed,
				be64_to_cpu(pi->guard_tag), be64_to_cpu(csum));
			return BLK_STS_PROTECTION;
		}

next:
		iter->data_buf += iter->interval;
		iter->prot_buf += bi->tuple_size;
		iter->seed++;
	}

	return BLK_STS_OK;
}

static void ext_pi_type1_prepare(struct request *rq)
{
	struct blk_integrity *bi = &rq->q->limits.integrity;
	const int tuple_sz = bi->tuple_size;
	u64 ref_tag = ext_pi_ref_tag(rq);
	u8 offset = bi->pi_offset;
	struct bio *bio;

	__rq_for_each_bio(bio, rq) {
		struct bio_integrity_payload *bip = bio_integrity(bio);
		u64 virt = lower_48_bits(bip_get_seed(bip));
		struct bio_vec iv;
		struct bvec_iter iter;

		/* Already remapped? */
		if (bip->bip_flags & BIP_MAPPED_INTEGRITY)
			break;

		bip_for_each_vec(iv, bip, iter) {
			unsigned int j;
			void *p;

			p = bvec_kmap_local(&iv);
			for (j = 0; j < iv.bv_len; j += tuple_sz) {
				struct crc64_pi_tuple *pi = p +  offset;
				u64 ref = get_unaligned_be48(pi->ref_tag);

				if (ref == virt)
					put_unaligned_be48(ref_tag, pi->ref_tag);
				virt++;
				ref_tag++;
				p += tuple_sz;
			}
			kunmap_local(p);
		}

		bip->bip_flags |= BIP_MAPPED_INTEGRITY;
	}
}

static void ext_pi_type1_complete(struct request *rq, unsigned int nr_bytes)
{
	struct blk_integrity *bi = &rq->q->limits.integrity;
	unsigned intervals = nr_bytes >> bi->interval_exp;
	const int tuple_sz = bi->tuple_size;
	u64 ref_tag = ext_pi_ref_tag(rq);
	u8 offset = bi->pi_offset;
	struct bio *bio;

	__rq_for_each_bio(bio, rq) {
		struct bio_integrity_payload *bip = bio_integrity(bio);
		u64 virt = lower_48_bits(bip_get_seed(bip));
		struct bio_vec iv;
		struct bvec_iter iter;

		bip_for_each_vec(iv, bip, iter) {
			unsigned int j;
			void *p;

			p = bvec_kmap_local(&iv);
			for (j = 0; j < iv.bv_len && intervals; j += tuple_sz) {
				struct crc64_pi_tuple *pi = p + offset;
				u64 ref = get_unaligned_be48(pi->ref_tag);

				if (ref == ref_tag)
					put_unaligned_be48(virt, pi->ref_tag);
				virt++;
				ref_tag++;
				intervals--;
				p += tuple_sz;
			}
			kunmap_local(p);
		}
	}
}

void blk_integrity_generate(struct bio *bio)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct blk_integrity_iter iter;
	struct bvec_iter bviter;
	struct bio_vec bv;

	iter.disk_name = bio->bi_bdev->bd_disk->disk_name;
	iter.interval = 1 << bi->interval_exp;
	iter.seed = bio->bi_iter.bi_sector;
	iter.prot_buf = bvec_virt(bip->bip_vec);
	bio_for_each_segment(bv, bio, bviter) {
		void *kaddr = bvec_kmap_local(&bv);

		iter.data_buf = kaddr;
		iter.data_size = bv.bv_len;
		switch (bi->csum_type) {
		case BLK_INTEGRITY_CSUM_CRC64:
			ext_pi_crc64_generate(&iter, bi);
			break;
		case BLK_INTEGRITY_CSUM_CRC:
		case BLK_INTEGRITY_CSUM_IP:
			t10_pi_generate(&iter, bi);
			break;
		default:
			break;
		}
		kunmap_local(kaddr);
	}
}

void blk_integrity_verify_iter(struct bio *bio, struct bvec_iter *saved_iter)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct blk_integrity_iter iter;
	struct bvec_iter bviter;
	struct bio_vec bv;

	/*
	 * At the moment verify is called bi_iter has been advanced during split
	 * and completion, so use the copy created during submission here.
	 */
	iter.disk_name = bio->bi_bdev->bd_disk->disk_name;
	iter.interval = 1 << bi->interval_exp;
	iter.seed = saved_iter->bi_sector;
	iter.prot_buf = bvec_virt(bip->bip_vec);
	__bio_for_each_segment(bv, bio, bviter, *saved_iter) {
		void *kaddr = bvec_kmap_local(&bv);
		blk_status_t ret = BLK_STS_OK;

		iter.data_buf = kaddr;
		iter.data_size = bv.bv_len;
		switch (bi->csum_type) {
		case BLK_INTEGRITY_CSUM_CRC64:
			ret = ext_pi_crc64_verify(&iter, bi);
			break;
		case BLK_INTEGRITY_CSUM_CRC:
		case BLK_INTEGRITY_CSUM_IP:
			ret = t10_pi_verify(&iter, bi);
			break;
		default:
			break;
		}
		kunmap_local(kaddr);

		if (ret) {
			bio->bi_status = ret;
			return;
		}
	}
}

void blk_integrity_prepare(struct request *rq)
{
	struct blk_integrity *bi = &rq->q->limits.integrity;

	if (!(bi->flags & BLK_INTEGRITY_REF_TAG))
		return;

	if (bi->csum_type == BLK_INTEGRITY_CSUM_CRC64)
		ext_pi_type1_prepare(rq);
	else
		t10_pi_type1_prepare(rq);
}

void blk_integrity_complete(struct request *rq, unsigned int nr_bytes)
{
	struct blk_integrity *bi = &rq->q->limits.integrity;

	if (!(bi->flags & BLK_INTEGRITY_REF_TAG))
		return;

	if (bi->csum_type == BLK_INTEGRITY_CSUM_CRC64)
		ext_pi_type1_complete(rq, nr_bytes);
	else
		t10_pi_type1_complete(rq, nr_bytes);
}
