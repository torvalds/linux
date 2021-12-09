// SPDX-License-Identifier: GPL-2.0
/*
 * t10_pi.c - Functions for generating and verifying T10 Protection
 *	      Information.
 */

#include <linux/t10-pi.h>
#include <linux/blkdev.h>
#include <linux/crc-t10dif.h>
#include <linux/module.h>
#include <net/checksum.h>

typedef __be16 (csum_fn) (void *, unsigned int);

static __be16 t10_pi_crc_fn(void *data, unsigned int len)
{
	return cpu_to_be16(crc_t10dif(data, len));
}

static __be16 t10_pi_ip_fn(void *data, unsigned int len)
{
	return (__force __be16)ip_compute_csum(data, len);
}

/*
 * Type 1 and Type 2 protection use the same format: 16 bit guard tag,
 * 16 bit app tag, 32 bit reference tag. Type 3 does not define the ref
 * tag.
 */
static blk_status_t t10_pi_generate(struct blk_integrity_iter *iter,
		csum_fn *fn, enum t10_dif_type type)
{
	unsigned int i;

	for (i = 0 ; i < iter->data_size ; i += iter->interval) {
		struct t10_pi_tuple *pi = iter->prot_buf;

		pi->guard_tag = fn(iter->data_buf, iter->interval);
		pi->app_tag = 0;

		if (type == T10_PI_TYPE1_PROTECTION)
			pi->ref_tag = cpu_to_be32(lower_32_bits(iter->seed));
		else
			pi->ref_tag = 0;

		iter->data_buf += iter->interval;
		iter->prot_buf += sizeof(struct t10_pi_tuple);
		iter->seed++;
	}

	return BLK_STS_OK;
}

static blk_status_t t10_pi_verify(struct blk_integrity_iter *iter,
		csum_fn *fn, enum t10_dif_type type)
{
	unsigned int i;

	BUG_ON(type == T10_PI_TYPE0_PROTECTION);

	for (i = 0 ; i < iter->data_size ; i += iter->interval) {
		struct t10_pi_tuple *pi = iter->prot_buf;
		__be16 csum;

		if (type == T10_PI_TYPE1_PROTECTION ||
		    type == T10_PI_TYPE2_PROTECTION) {
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
		} else if (type == T10_PI_TYPE3_PROTECTION) {
			if (pi->app_tag == T10_PI_APP_ESCAPE &&
			    pi->ref_tag == T10_PI_REF_ESCAPE)
				goto next;
		}

		csum = fn(iter->data_buf, iter->interval);

		if (pi->guard_tag != csum) {
			pr_err("%s: guard tag error at sector %llu " \
			       "(rcvd %04x, want %04x)\n", iter->disk_name,
			       (unsigned long long)iter->seed,
			       be16_to_cpu(pi->guard_tag), be16_to_cpu(csum));
			return BLK_STS_PROTECTION;
		}

next:
		iter->data_buf += iter->interval;
		iter->prot_buf += sizeof(struct t10_pi_tuple);
		iter->seed++;
	}

	return BLK_STS_OK;
}

static blk_status_t t10_pi_type1_generate_crc(struct blk_integrity_iter *iter)
{
	return t10_pi_generate(iter, t10_pi_crc_fn, T10_PI_TYPE1_PROTECTION);
}

static blk_status_t t10_pi_type1_generate_ip(struct blk_integrity_iter *iter)
{
	return t10_pi_generate(iter, t10_pi_ip_fn, T10_PI_TYPE1_PROTECTION);
}

static blk_status_t t10_pi_type1_verify_crc(struct blk_integrity_iter *iter)
{
	return t10_pi_verify(iter, t10_pi_crc_fn, T10_PI_TYPE1_PROTECTION);
}

static blk_status_t t10_pi_type1_verify_ip(struct blk_integrity_iter *iter)
{
	return t10_pi_verify(iter, t10_pi_ip_fn, T10_PI_TYPE1_PROTECTION);
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
	const int tuple_sz = rq->q->integrity.tuple_size;
	u32 ref_tag = t10_pi_ref_tag(rq);
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
				struct t10_pi_tuple *pi = p;

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
	unsigned intervals = nr_bytes >> rq->q->integrity.interval_exp;
	const int tuple_sz = rq->q->integrity.tuple_size;
	u32 ref_tag = t10_pi_ref_tag(rq);
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
				struct t10_pi_tuple *pi = p;

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

static blk_status_t t10_pi_type3_generate_crc(struct blk_integrity_iter *iter)
{
	return t10_pi_generate(iter, t10_pi_crc_fn, T10_PI_TYPE3_PROTECTION);
}

static blk_status_t t10_pi_type3_generate_ip(struct blk_integrity_iter *iter)
{
	return t10_pi_generate(iter, t10_pi_ip_fn, T10_PI_TYPE3_PROTECTION);
}

static blk_status_t t10_pi_type3_verify_crc(struct blk_integrity_iter *iter)
{
	return t10_pi_verify(iter, t10_pi_crc_fn, T10_PI_TYPE3_PROTECTION);
}

static blk_status_t t10_pi_type3_verify_ip(struct blk_integrity_iter *iter)
{
	return t10_pi_verify(iter, t10_pi_ip_fn, T10_PI_TYPE3_PROTECTION);
}

/* Type 3 does not have a reference tag so no remapping is required. */
static void t10_pi_type3_prepare(struct request *rq)
{
}

/* Type 3 does not have a reference tag so no remapping is required. */
static void t10_pi_type3_complete(struct request *rq, unsigned int nr_bytes)
{
}

const struct blk_integrity_profile t10_pi_type1_crc = {
	.name			= "T10-DIF-TYPE1-CRC",
	.generate_fn		= t10_pi_type1_generate_crc,
	.verify_fn		= t10_pi_type1_verify_crc,
	.prepare_fn		= t10_pi_type1_prepare,
	.complete_fn		= t10_pi_type1_complete,
};
EXPORT_SYMBOL(t10_pi_type1_crc);

const struct blk_integrity_profile t10_pi_type1_ip = {
	.name			= "T10-DIF-TYPE1-IP",
	.generate_fn		= t10_pi_type1_generate_ip,
	.verify_fn		= t10_pi_type1_verify_ip,
	.prepare_fn		= t10_pi_type1_prepare,
	.complete_fn		= t10_pi_type1_complete,
};
EXPORT_SYMBOL(t10_pi_type1_ip);

const struct blk_integrity_profile t10_pi_type3_crc = {
	.name			= "T10-DIF-TYPE3-CRC",
	.generate_fn		= t10_pi_type3_generate_crc,
	.verify_fn		= t10_pi_type3_verify_crc,
	.prepare_fn		= t10_pi_type3_prepare,
	.complete_fn		= t10_pi_type3_complete,
};
EXPORT_SYMBOL(t10_pi_type3_crc);

const struct blk_integrity_profile t10_pi_type3_ip = {
	.name			= "T10-DIF-TYPE3-IP",
	.generate_fn		= t10_pi_type3_generate_ip,
	.verify_fn		= t10_pi_type3_verify_ip,
	.prepare_fn		= t10_pi_type3_prepare,
	.complete_fn		= t10_pi_type3_complete,
};
EXPORT_SYMBOL(t10_pi_type3_ip);

MODULE_LICENSE("GPL");
