/*
 * sd_dif.c - SCSI Data Integrity Field
 *
 * Copyright (C) 2007, 2008 Oracle Corporation
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

#include <linux/blkdev.h>
#include <linux/crc-t10dif.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsicam.h>

#include <net/checksum.h>

#include "sd.h"

typedef __u16 (csum_fn) (void *, unsigned int);

static __u16 sd_dif_crc_fn(void *data, unsigned int len)
{
	return cpu_to_be16(crc_t10dif(data, len));
}

static __u16 sd_dif_ip_fn(void *data, unsigned int len)
{
	return ip_compute_csum(data, len);
}

/*
 * Type 1 and Type 2 protection use the same format: 16 bit guard tag,
 * 16 bit app tag, 32 bit reference tag.
 */
static void sd_dif_type1_generate(struct blk_integrity_iter *iter, csum_fn *fn)
{
	void *buf = iter->data_buf;
	struct sd_dif_tuple *sdt = iter->prot_buf;
	sector_t seed = iter->seed;
	unsigned int i;

	for (i = 0 ; i < iter->data_size ; i += iter->interval, sdt++) {
		sdt->guard_tag = fn(buf, iter->interval);
		sdt->ref_tag = cpu_to_be32(seed & 0xffffffff);
		sdt->app_tag = 0;

		buf += iter->interval;
		seed++;
	}
}

static int sd_dif_type1_generate_crc(struct blk_integrity_iter *iter)
{
	sd_dif_type1_generate(iter, sd_dif_crc_fn);
	return 0;
}

static int sd_dif_type1_generate_ip(struct blk_integrity_iter *iter)
{
	sd_dif_type1_generate(iter, sd_dif_ip_fn);
	return 0;
}

static int sd_dif_type1_verify(struct blk_integrity_iter *iter, csum_fn *fn)
{
	void *buf = iter->data_buf;
	struct sd_dif_tuple *sdt = iter->prot_buf;
	sector_t seed = iter->seed;
	unsigned int i;
	__u16 csum;

	for (i = 0 ; i < iter->data_size ; i += iter->interval, sdt++) {
		/* Unwritten sectors */
		if (sdt->app_tag == 0xffff)
			return 0;

		if (be32_to_cpu(sdt->ref_tag) != (seed & 0xffffffff)) {
			printk(KERN_ERR
			       "%s: ref tag error on sector %lu (rcvd %u)\n",
			       iter->disk_name, (unsigned long)seed,
			       be32_to_cpu(sdt->ref_tag));
			return -EIO;
		}

		csum = fn(buf, iter->interval);

		if (sdt->guard_tag != csum) {
			printk(KERN_ERR "%s: guard tag error on sector %lu " \
			       "(rcvd %04x, data %04x)\n", iter->disk_name,
			       (unsigned long)seed,
			       be16_to_cpu(sdt->guard_tag), be16_to_cpu(csum));
			return -EIO;
		}

		buf += iter->interval;
		seed++;
	}

	return 0;
}

static int sd_dif_type1_verify_crc(struct blk_integrity_iter *iter)
{
	return sd_dif_type1_verify(iter, sd_dif_crc_fn);
}

static int sd_dif_type1_verify_ip(struct blk_integrity_iter *iter)
{
	return sd_dif_type1_verify(iter, sd_dif_ip_fn);
}

static struct blk_integrity dif_type1_integrity_crc = {
	.name			= "T10-DIF-TYPE1-CRC",
	.generate_fn		= sd_dif_type1_generate_crc,
	.verify_fn		= sd_dif_type1_verify_crc,
	.tuple_size		= sizeof(struct sd_dif_tuple),
	.tag_size		= 0,
};

static struct blk_integrity dif_type1_integrity_ip = {
	.name			= "T10-DIF-TYPE1-IP",
	.generate_fn		= sd_dif_type1_generate_ip,
	.verify_fn		= sd_dif_type1_verify_ip,
	.tuple_size		= sizeof(struct sd_dif_tuple),
	.tag_size		= 0,
};


/*
 * Type 3 protection has a 16-bit guard tag and 16 + 32 bits of opaque
 * tag space.
 */
static void sd_dif_type3_generate(struct blk_integrity_iter *iter, csum_fn *fn)
{
	void *buf = iter->data_buf;
	struct sd_dif_tuple *sdt = iter->prot_buf;
	unsigned int i;

	for (i = 0 ; i < iter->data_size ; i += iter->interval, sdt++) {
		sdt->guard_tag = fn(buf, iter->interval);
		sdt->ref_tag = 0;
		sdt->app_tag = 0;

		buf += iter->interval;
	}
}

static int sd_dif_type3_generate_crc(struct blk_integrity_iter *iter)
{
	sd_dif_type3_generate(iter, sd_dif_crc_fn);
	return 0;
}

static int sd_dif_type3_generate_ip(struct blk_integrity_iter *iter)
{
	sd_dif_type3_generate(iter, sd_dif_ip_fn);
	return 0;
}

static int sd_dif_type3_verify(struct blk_integrity_iter *iter, csum_fn *fn)
{
	void *buf = iter->data_buf;
	struct sd_dif_tuple *sdt = iter->prot_buf;
	sector_t seed = iter->seed;
	unsigned int i;
	__u16 csum;

	for (i = 0 ; i < iter->data_size ; i += iter->interval, sdt++) {
		/* Unwritten sectors */
		if (sdt->app_tag == 0xffff && sdt->ref_tag == 0xffffffff)
			return 0;

		csum = fn(buf, iter->interval);

		if (sdt->guard_tag != csum) {
			printk(KERN_ERR "%s: guard tag error on sector %lu " \
			       "(rcvd %04x, data %04x)\n", iter->disk_name,
			       (unsigned long)seed,
			       be16_to_cpu(sdt->guard_tag), be16_to_cpu(csum));
			return -EIO;
		}

		buf += iter->interval;
		seed++;
	}

	return 0;
}

static int sd_dif_type3_verify_crc(struct blk_integrity_iter *iter)
{
	return sd_dif_type3_verify(iter, sd_dif_crc_fn);
}

static int sd_dif_type3_verify_ip(struct blk_integrity_iter *iter)
{
	return sd_dif_type3_verify(iter, sd_dif_ip_fn);
}

static struct blk_integrity dif_type3_integrity_crc = {
	.name			= "T10-DIF-TYPE3-CRC",
	.generate_fn		= sd_dif_type3_generate_crc,
	.verify_fn		= sd_dif_type3_verify_crc,
	.tuple_size		= sizeof(struct sd_dif_tuple),
	.tag_size		= 0,
};

static struct blk_integrity dif_type3_integrity_ip = {
	.name			= "T10-DIF-TYPE3-IP",
	.generate_fn		= sd_dif_type3_generate_ip,
	.verify_fn		= sd_dif_type3_verify_ip,
	.tuple_size		= sizeof(struct sd_dif_tuple),
	.tag_size		= 0,
};

/*
 * Configure exchange of protection information between OS and HBA.
 */
void sd_dif_config_host(struct scsi_disk *sdkp)
{
	struct scsi_device *sdp = sdkp->device;
	struct gendisk *disk = sdkp->disk;
	u8 type = sdkp->protection_type;
	int dif, dix;

	dif = scsi_host_dif_capable(sdp->host, type);
	dix = scsi_host_dix_capable(sdp->host, type);

	if (!dix && scsi_host_dix_capable(sdp->host, 0)) {
		dif = 0; dix = 1;
	}

	if (!dix)
		return;

	/* Enable DMA of protection information */
	if (scsi_host_get_guard(sdkp->device->host) & SHOST_DIX_GUARD_IP)
		if (type == SD_DIF_TYPE3_PROTECTION)
			blk_integrity_register(disk, &dif_type3_integrity_ip);
		else
			blk_integrity_register(disk, &dif_type1_integrity_ip);
	else
		if (type == SD_DIF_TYPE3_PROTECTION)
			blk_integrity_register(disk, &dif_type3_integrity_crc);
		else
			blk_integrity_register(disk, &dif_type1_integrity_crc);

	sd_printk(KERN_NOTICE, sdkp,
		  "Enabling DIX %s protection\n", disk->integrity->name);

	/* Signal to block layer that we support sector tagging */
	if (dif && type) {

		disk->integrity->flags |= BLK_INTEGRITY_DEVICE_CAPABLE;

		if (!sdkp)
			return;

		if (type == SD_DIF_TYPE3_PROTECTION)
			disk->integrity->tag_size = sizeof(u16) + sizeof(u32);
		else
			disk->integrity->tag_size = sizeof(u16);

		sd_printk(KERN_NOTICE, sdkp, "DIF application tag size %u\n",
			  disk->integrity->tag_size);
	}
}

/*
 * The virtual start sector is the one that was originally submitted
 * by the block layer.	Due to partitioning, MD/DM cloning, etc. the
 * actual physical start sector is likely to be different.  Remap
 * protection information to match the physical LBA.
 *
 * From a protocol perspective there's a slight difference between
 * Type 1 and 2.  The latter uses 32-byte CDBs exclusively, and the
 * reference tag is seeded in the CDB.  This gives us the potential to
 * avoid virt->phys remapping during write.  However, at read time we
 * don't know whether the virt sector is the same as when we wrote it
 * (we could be reading from real disk as opposed to MD/DM device.  So
 * we always remap Type 2 making it identical to Type 1.
 *
 * Type 3 does not have a reference tag so no remapping is required.
 */
void sd_dif_prepare(struct request *rq, sector_t hw_sector,
		    unsigned int sector_sz)
{
	const int tuple_sz = sizeof(struct sd_dif_tuple);
	struct bio *bio;
	struct scsi_disk *sdkp;
	struct sd_dif_tuple *sdt;
	u32 phys, virt;

	sdkp = rq->bio->bi_bdev->bd_disk->private_data;

	if (sdkp->protection_type == SD_DIF_TYPE3_PROTECTION)
		return;

	phys = hw_sector & 0xffffffff;

	__rq_for_each_bio(bio, rq) {
		struct bio_integrity_payload *bip = bio_integrity(bio);
		struct bio_vec iv;
		struct bvec_iter iter;
		unsigned int j;

		/* Already remapped? */
		if (bip->bip_flags & BIP_MAPPED_INTEGRITY)
			break;

		virt = bip_get_seed(bip) & 0xffffffff;

		bip_for_each_vec(iv, bip, iter) {
			sdt = kmap_atomic(iv.bv_page)
				+ iv.bv_offset;

			for (j = 0; j < iv.bv_len; j += tuple_sz, sdt++) {

				if (be32_to_cpu(sdt->ref_tag) == virt)
					sdt->ref_tag = cpu_to_be32(phys);

				virt++;
				phys++;
			}

			kunmap_atomic(sdt);
		}

		bip->bip_flags |= BIP_MAPPED_INTEGRITY;
	}
}

/*
 * Remap physical sector values in the reference tag to the virtual
 * values expected by the block layer.
 */
void sd_dif_complete(struct scsi_cmnd *scmd, unsigned int good_bytes)
{
	const int tuple_sz = sizeof(struct sd_dif_tuple);
	struct scsi_disk *sdkp;
	struct bio *bio;
	struct sd_dif_tuple *sdt;
	unsigned int j, sectors, sector_sz;
	u32 phys, virt;

	sdkp = scsi_disk(scmd->request->rq_disk);

	if (sdkp->protection_type == SD_DIF_TYPE3_PROTECTION || good_bytes == 0)
		return;

	sector_sz = scmd->device->sector_size;
	sectors = good_bytes / sector_sz;

	phys = blk_rq_pos(scmd->request) & 0xffffffff;
	if (sector_sz == 4096)
		phys >>= 3;

	__rq_for_each_bio(bio, scmd->request) {
		struct bio_integrity_payload *bip = bio_integrity(bio);
		struct bio_vec iv;
		struct bvec_iter iter;

		virt = bip_get_seed(bip) & 0xffffffff;

		bip_for_each_vec(iv, bip, iter) {
			sdt = kmap_atomic(iv.bv_page)
				+ iv.bv_offset;

			for (j = 0; j < iv.bv_len; j += tuple_sz, sdt++) {

				if (sectors == 0) {
					kunmap_atomic(sdt);
					return;
				}

				if (be32_to_cpu(sdt->ref_tag) == phys)
					sdt->ref_tag = cpu_to_be32(virt);

				virt++;
				phys++;
				sectors--;
			}

			kunmap_atomic(sdt);
		}
	}
}

