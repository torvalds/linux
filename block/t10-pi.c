/*
 * t10_pi.c - Functions for generating and verifying T10 Protection
 *	      Information.
 *
 * Copyright (C) 2007, 2008, 2014 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include <linux/t10-pi.h>
#include <linux/blkdev.h>
#include <linux/crc-t10dif.h>
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
		csum_fn *fn, unsigned int type)
{
	unsigned int i;

	for (i = 0 ; i < iter->data_size ; i += iter->interval) {
		struct t10_pi_tuple *pi = iter->prot_buf;

		pi->guard_tag = fn(iter->data_buf, iter->interval);
		pi->app_tag = 0;

		if (type == 1)
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
		csum_fn *fn, unsigned int type)
{
	unsigned int i;

	for (i = 0 ; i < iter->data_size ; i += iter->interval) {
		struct t10_pi_tuple *pi = iter->prot_buf;
		__be16 csum;

		switch (type) {
		case 1:
		case 2:
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
			break;
		case 3:
			if (pi->app_tag == T10_PI_APP_ESCAPE &&
			    pi->ref_tag == T10_PI_REF_ESCAPE)
				goto next;
			break;
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
	return t10_pi_generate(iter, t10_pi_crc_fn, 1);
}

static blk_status_t t10_pi_type1_generate_ip(struct blk_integrity_iter *iter)
{
	return t10_pi_generate(iter, t10_pi_ip_fn, 1);
}

static blk_status_t t10_pi_type1_verify_crc(struct blk_integrity_iter *iter)
{
	return t10_pi_verify(iter, t10_pi_crc_fn, 1);
}

static blk_status_t t10_pi_type1_verify_ip(struct blk_integrity_iter *iter)
{
	return t10_pi_verify(iter, t10_pi_ip_fn, 1);
}

static blk_status_t t10_pi_type3_generate_crc(struct blk_integrity_iter *iter)
{
	return t10_pi_generate(iter, t10_pi_crc_fn, 3);
}

static blk_status_t t10_pi_type3_generate_ip(struct blk_integrity_iter *iter)
{
	return t10_pi_generate(iter, t10_pi_ip_fn, 3);
}

static blk_status_t t10_pi_type3_verify_crc(struct blk_integrity_iter *iter)
{
	return t10_pi_verify(iter, t10_pi_crc_fn, 3);
}

static blk_status_t t10_pi_type3_verify_ip(struct blk_integrity_iter *iter)
{
	return t10_pi_verify(iter, t10_pi_ip_fn, 3);
}

const struct blk_integrity_profile t10_pi_type1_crc = {
	.name			= "T10-DIF-TYPE1-CRC",
	.generate_fn		= t10_pi_type1_generate_crc,
	.verify_fn		= t10_pi_type1_verify_crc,
};
EXPORT_SYMBOL(t10_pi_type1_crc);

const struct blk_integrity_profile t10_pi_type1_ip = {
	.name			= "T10-DIF-TYPE1-IP",
	.generate_fn		= t10_pi_type1_generate_ip,
	.verify_fn		= t10_pi_type1_verify_ip,
};
EXPORT_SYMBOL(t10_pi_type1_ip);

const struct blk_integrity_profile t10_pi_type3_crc = {
	.name			= "T10-DIF-TYPE3-CRC",
	.generate_fn		= t10_pi_type3_generate_crc,
	.verify_fn		= t10_pi_type3_verify_crc,
};
EXPORT_SYMBOL(t10_pi_type3_crc);

const struct blk_integrity_profile t10_pi_type3_ip = {
	.name			= "T10-DIF-TYPE3-IP",
	.generate_fn		= t10_pi_type3_generate_ip,
	.verify_fn		= t10_pi_type3_verify_ip,
};
EXPORT_SYMBOL(t10_pi_type3_ip);

/**
 * t10_pi_prepare - prepare PI prior submitting request to device
 * @rq:              request with PI that should be prepared
 * @protection_type: PI type (Type 1/Type 2/Type 3)
 *
 * For Type 1/Type 2, the virtual start sector is the one that was
 * originally submitted by the block layer for the ref_tag usage. Due to
 * partitioning, MD/DM cloning, etc. the actual physical start sector is
 * likely to be different. Remap protection information to match the
 * physical LBA.
 *
 * Type 3 does not have a reference tag so no remapping is required.
 */
void t10_pi_prepare(struct request *rq, u8 protection_type)
{
	const int tuple_sz = rq->q->integrity.tuple_size;
	u32 ref_tag = t10_pi_ref_tag(rq);
	struct bio *bio;

	if (protection_type == T10_PI_TYPE3_PROTECTION)
		return;

	__rq_for_each_bio(bio, rq) {
		struct bio_integrity_payload *bip = bio_integrity(bio);
		u32 virt = bip_get_seed(bip) & 0xffffffff;
		struct bio_vec iv;
		struct bvec_iter iter;

		/* Already remapped? */
		if (bip->bip_flags & BIP_MAPPED_INTEGRITY)
			break;

		bip_for_each_vec(iv, bip, iter) {
			void *p, *pmap;
			unsigned int j;

			pmap = kmap_atomic(iv.bv_page);
			p = pmap + iv.bv_offset;
			for (j = 0; j < iv.bv_len; j += tuple_sz) {
				struct t10_pi_tuple *pi = p;

				if (be32_to_cpu(pi->ref_tag) == virt)
					pi->ref_tag = cpu_to_be32(ref_tag);
				virt++;
				ref_tag++;
				p += tuple_sz;
			}

			kunmap_atomic(pmap);
		}

		bip->bip_flags |= BIP_MAPPED_INTEGRITY;
	}
}
EXPORT_SYMBOL(t10_pi_prepare);

/**
 * t10_pi_complete - prepare PI prior returning request to the block layer
 * @rq:              request with PI that should be prepared
 * @protection_type: PI type (Type 1/Type 2/Type 3)
 * @intervals:       total elements to prepare
 *
 * For Type 1/Type 2, the virtual start sector is the one that was
 * originally submitted by the block layer for the ref_tag usage. Due to
 * partitioning, MD/DM cloning, etc. the actual physical start sector is
 * likely to be different. Since the physical start sector was submitted
 * to the device, we should remap it back to virtual values expected by the
 * block layer.
 *
 * Type 3 does not have a reference tag so no remapping is required.
 */
void t10_pi_complete(struct request *rq, u8 protection_type,
		     unsigned int intervals)
{
	const int tuple_sz = rq->q->integrity.tuple_size;
	u32 ref_tag = t10_pi_ref_tag(rq);
	struct bio *bio;

	if (protection_type == T10_PI_TYPE3_PROTECTION)
		return;

	__rq_for_each_bio(bio, rq) {
		struct bio_integrity_payload *bip = bio_integrity(bio);
		u32 virt = bip_get_seed(bip) & 0xffffffff;
		struct bio_vec iv;
		struct bvec_iter iter;

		bip_for_each_vec(iv, bip, iter) {
			void *p, *pmap;
			unsigned int j;

			pmap = kmap_atomic(iv.bv_page);
			p = pmap + iv.bv_offset;
			for (j = 0; j < iv.bv_len && intervals; j += tuple_sz) {
				struct t10_pi_tuple *pi = p;

				if (be32_to_cpu(pi->ref_tag) == ref_tag)
					pi->ref_tag = cpu_to_be32(virt);
				virt++;
				ref_tag++;
				intervals--;
				p += tuple_sz;
			}

			kunmap_atomic(pmap);
		}
	}
}
EXPORT_SYMBOL(t10_pi_complete);
