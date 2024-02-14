// SPDX-License-Identifier: GPL-2.0-or-later
/*******************************************************************************
 * Filename:  target_core_iblock.c
 *
 * This file contains the Storage Engine  <-> Linux BlockIO transport
 * specific functions.
 *
 * (c) Copyright 2003-2013 Datera, Inc.
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 ******************************************************************************/

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-integrity.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/bio.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/pr.h>
#include <scsi/scsi_proto.h>
#include <scsi/scsi_common.h>
#include <asm/unaligned.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>

#include "target_core_iblock.h"
#include "target_core_pr.h"

#define IBLOCK_MAX_BIO_PER_TASK	 32	/* max # of bios to submit at a time */
#define IBLOCK_BIO_POOL_SIZE	128

static inline struct iblock_dev *IBLOCK_DEV(struct se_device *dev)
{
	return container_of(dev, struct iblock_dev, dev);
}


static int iblock_attach_hba(struct se_hba *hba, u32 host_id)
{
	pr_debug("CORE_HBA[%d] - TCM iBlock HBA Driver %s on"
		" Generic Target Core Stack %s\n", hba->hba_id,
		IBLOCK_VERSION, TARGET_CORE_VERSION);
	return 0;
}

static void iblock_detach_hba(struct se_hba *hba)
{
}

static struct se_device *iblock_alloc_device(struct se_hba *hba, const char *name)
{
	struct iblock_dev *ib_dev = NULL;

	ib_dev = kzalloc(sizeof(struct iblock_dev), GFP_KERNEL);
	if (!ib_dev) {
		pr_err("Unable to allocate struct iblock_dev\n");
		return NULL;
	}

	ib_dev->ibd_plug = kcalloc(nr_cpu_ids, sizeof(*ib_dev->ibd_plug),
				   GFP_KERNEL);
	if (!ib_dev->ibd_plug)
		goto free_dev;

	pr_debug( "IBLOCK: Allocated ib_dev for %s\n", name);

	return &ib_dev->dev;

free_dev:
	kfree(ib_dev);
	return NULL;
}

static bool iblock_configure_unmap(struct se_device *dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);

	return target_configure_unmap_from_queue(&dev->dev_attrib,
						 ib_dev->ibd_bd);
}

static int iblock_configure_device(struct se_device *dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct request_queue *q;
	struct bdev_handle *bdev_handle;
	struct block_device *bd;
	struct blk_integrity *bi;
	blk_mode_t mode = BLK_OPEN_READ;
	unsigned int max_write_zeroes_sectors;
	int ret;

	if (!(ib_dev->ibd_flags & IBDF_HAS_UDEV_PATH)) {
		pr_err("Missing udev_path= parameters for IBLOCK\n");
		return -EINVAL;
	}

	ret = bioset_init(&ib_dev->ibd_bio_set, IBLOCK_BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (ret) {
		pr_err("IBLOCK: Unable to create bioset\n");
		goto out;
	}

	pr_debug( "IBLOCK: Claiming struct block_device: %s\n",
			ib_dev->ibd_udev_path);

	if (!ib_dev->ibd_readonly)
		mode |= BLK_OPEN_WRITE;
	else
		dev->dev_flags |= DF_READ_ONLY;

	bdev_handle = bdev_open_by_path(ib_dev->ibd_udev_path, mode, ib_dev,
					NULL);
	if (IS_ERR(bdev_handle)) {
		ret = PTR_ERR(bdev_handle);
		goto out_free_bioset;
	}
	ib_dev->ibd_bdev_handle = bdev_handle;
	ib_dev->ibd_bd = bd = bdev_handle->bdev;

	q = bdev_get_queue(bd);

	dev->dev_attrib.hw_block_size = bdev_logical_block_size(bd);
	dev->dev_attrib.hw_max_sectors = mult_frac(queue_max_hw_sectors(q),
			SECTOR_SIZE,
			dev->dev_attrib.hw_block_size);
	dev->dev_attrib.hw_queue_depth = q->nr_requests;

	/*
	 * Enable write same emulation for IBLOCK and use 0xFFFF as
	 * the smaller WRITE_SAME(10) only has a two-byte block count.
	 */
	max_write_zeroes_sectors = bdev_write_zeroes_sectors(bd);
	if (max_write_zeroes_sectors)
		dev->dev_attrib.max_write_same_len = max_write_zeroes_sectors;
	else
		dev->dev_attrib.max_write_same_len = 0xFFFF;

	if (bdev_nonrot(bd))
		dev->dev_attrib.is_nonrot = 1;

	bi = bdev_get_integrity(bd);
	if (bi) {
		struct bio_set *bs = &ib_dev->ibd_bio_set;

		if (!strcmp(bi->profile->name, "T10-DIF-TYPE3-IP") ||
		    !strcmp(bi->profile->name, "T10-DIF-TYPE1-IP")) {
			pr_err("IBLOCK export of blk_integrity: %s not"
			       " supported\n", bi->profile->name);
			ret = -ENOSYS;
			goto out_blkdev_put;
		}

		if (!strcmp(bi->profile->name, "T10-DIF-TYPE3-CRC")) {
			dev->dev_attrib.pi_prot_type = TARGET_DIF_TYPE3_PROT;
		} else if (!strcmp(bi->profile->name, "T10-DIF-TYPE1-CRC")) {
			dev->dev_attrib.pi_prot_type = TARGET_DIF_TYPE1_PROT;
		}

		if (dev->dev_attrib.pi_prot_type) {
			if (bioset_integrity_create(bs, IBLOCK_BIO_POOL_SIZE) < 0) {
				pr_err("Unable to allocate bioset for PI\n");
				ret = -ENOMEM;
				goto out_blkdev_put;
			}
			pr_debug("IBLOCK setup BIP bs->bio_integrity_pool: %p\n",
				 &bs->bio_integrity_pool);
		}
		dev->dev_attrib.hw_pi_prot_type = dev->dev_attrib.pi_prot_type;
	}

	return 0;

out_blkdev_put:
	bdev_release(ib_dev->ibd_bdev_handle);
out_free_bioset:
	bioset_exit(&ib_dev->ibd_bio_set);
out:
	return ret;
}

static void iblock_dev_call_rcu(struct rcu_head *p)
{
	struct se_device *dev = container_of(p, struct se_device, rcu_head);
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);

	kfree(ib_dev->ibd_plug);
	kfree(ib_dev);
}

static void iblock_free_device(struct se_device *dev)
{
	call_rcu(&dev->rcu_head, iblock_dev_call_rcu);
}

static void iblock_destroy_device(struct se_device *dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);

	if (ib_dev->ibd_bdev_handle)
		bdev_release(ib_dev->ibd_bdev_handle);
	bioset_exit(&ib_dev->ibd_bio_set);
}

static struct se_dev_plug *iblock_plug_device(struct se_device *se_dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(se_dev);
	struct iblock_dev_plug *ib_dev_plug;

	/*
	 * Each se_device has a per cpu work this can be run from. We
	 * shouldn't have multiple threads on the same cpu calling this
	 * at the same time.
	 */
	ib_dev_plug = &ib_dev->ibd_plug[raw_smp_processor_id()];
	if (test_and_set_bit(IBD_PLUGF_PLUGGED, &ib_dev_plug->flags))
		return NULL;

	blk_start_plug(&ib_dev_plug->blk_plug);
	return &ib_dev_plug->se_plug;
}

static void iblock_unplug_device(struct se_dev_plug *se_plug)
{
	struct iblock_dev_plug *ib_dev_plug = container_of(se_plug,
					struct iblock_dev_plug, se_plug);

	blk_finish_plug(&ib_dev_plug->blk_plug);
	clear_bit(IBD_PLUGF_PLUGGED, &ib_dev_plug->flags);
}

static sector_t iblock_get_blocks(struct se_device *dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	u32 block_size = bdev_logical_block_size(ib_dev->ibd_bd);
	unsigned long long blocks_long =
		div_u64(bdev_nr_bytes(ib_dev->ibd_bd), block_size) - 1;

	if (block_size == dev->dev_attrib.block_size)
		return blocks_long;

	switch (block_size) {
	case 4096:
		switch (dev->dev_attrib.block_size) {
		case 2048:
			blocks_long <<= 1;
			break;
		case 1024:
			blocks_long <<= 2;
			break;
		case 512:
			blocks_long <<= 3;
			break;
		default:
			break;
		}
		break;
	case 2048:
		switch (dev->dev_attrib.block_size) {
		case 4096:
			blocks_long >>= 1;
			break;
		case 1024:
			blocks_long <<= 1;
			break;
		case 512:
			blocks_long <<= 2;
			break;
		default:
			break;
		}
		break;
	case 1024:
		switch (dev->dev_attrib.block_size) {
		case 4096:
			blocks_long >>= 2;
			break;
		case 2048:
			blocks_long >>= 1;
			break;
		case 512:
			blocks_long <<= 1;
			break;
		default:
			break;
		}
		break;
	case 512:
		switch (dev->dev_attrib.block_size) {
		case 4096:
			blocks_long >>= 3;
			break;
		case 2048:
			blocks_long >>= 2;
			break;
		case 1024:
			blocks_long >>= 1;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return blocks_long;
}

static void iblock_complete_cmd(struct se_cmd *cmd, blk_status_t blk_status)
{
	struct iblock_req *ibr = cmd->priv;
	u8 status;

	if (!refcount_dec_and_test(&ibr->pending))
		return;

	if (blk_status == BLK_STS_RESV_CONFLICT)
		status = SAM_STAT_RESERVATION_CONFLICT;
	else if (atomic_read(&ibr->ib_bio_err_cnt))
		status = SAM_STAT_CHECK_CONDITION;
	else
		status = SAM_STAT_GOOD;

	target_complete_cmd(cmd, status);
	kfree(ibr);
}

static void iblock_bio_done(struct bio *bio)
{
	struct se_cmd *cmd = bio->bi_private;
	struct iblock_req *ibr = cmd->priv;
	blk_status_t blk_status = bio->bi_status;

	if (bio->bi_status) {
		pr_err("bio error: %p,  err: %d\n", bio, bio->bi_status);
		/*
		 * Bump the ib_bio_err_cnt and release bio.
		 */
		atomic_inc(&ibr->ib_bio_err_cnt);
		smp_mb__after_atomic();
	}

	bio_put(bio);

	iblock_complete_cmd(cmd, blk_status);
}

static struct bio *iblock_get_bio(struct se_cmd *cmd, sector_t lba, u32 sg_num,
				  blk_opf_t opf)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(cmd->se_dev);
	struct bio *bio;

	/*
	 * Only allocate as many vector entries as the bio code allows us to,
	 * we'll loop later on until we have handled the whole request.
	 */
	bio = bio_alloc_bioset(ib_dev->ibd_bd, bio_max_segs(sg_num), opf,
			       GFP_NOIO, &ib_dev->ibd_bio_set);
	if (!bio) {
		pr_err("Unable to allocate memory for bio\n");
		return NULL;
	}

	bio->bi_private = cmd;
	bio->bi_end_io = &iblock_bio_done;
	bio->bi_iter.bi_sector = lba;

	return bio;
}

static void iblock_submit_bios(struct bio_list *list)
{
	struct blk_plug plug;
	struct bio *bio;
	/*
	 * The block layer handles nested plugs, so just plug/unplug to handle
	 * fabric drivers that didn't support batching and multi bio cmds.
	 */
	blk_start_plug(&plug);
	while ((bio = bio_list_pop(list)))
		submit_bio(bio);
	blk_finish_plug(&plug);
}

static void iblock_end_io_flush(struct bio *bio)
{
	struct se_cmd *cmd = bio->bi_private;

	if (bio->bi_status)
		pr_err("IBLOCK: cache flush failed: %d\n", bio->bi_status);

	if (cmd) {
		if (bio->bi_status)
			target_complete_cmd(cmd, SAM_STAT_CHECK_CONDITION);
		else
			target_complete_cmd(cmd, SAM_STAT_GOOD);
	}

	bio_put(bio);
}

/*
 * Implement SYCHRONIZE CACHE.  Note that we can't handle lba ranges and must
 * always flush the whole cache.
 */
static sense_reason_t
iblock_execute_sync_cache(struct se_cmd *cmd)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(cmd->se_dev);
	int immed = (cmd->t_task_cdb[1] & 0x2);
	struct bio *bio;

	/*
	 * If the Immediate bit is set, queue up the GOOD response
	 * for this SYNCHRONIZE_CACHE op.
	 */
	if (immed)
		target_complete_cmd(cmd, SAM_STAT_GOOD);

	bio = bio_alloc(ib_dev->ibd_bd, 0, REQ_OP_WRITE | REQ_PREFLUSH,
			GFP_KERNEL);
	bio->bi_end_io = iblock_end_io_flush;
	if (!immed)
		bio->bi_private = cmd;
	submit_bio(bio);
	return 0;
}

static sense_reason_t
iblock_execute_unmap(struct se_cmd *cmd, sector_t lba, sector_t nolb)
{
	struct block_device *bdev = IBLOCK_DEV(cmd->se_dev)->ibd_bd;
	struct se_device *dev = cmd->se_dev;
	int ret;

	ret = blkdev_issue_discard(bdev,
				   target_to_linux_sector(dev, lba),
				   target_to_linux_sector(dev,  nolb),
				   GFP_KERNEL);
	if (ret < 0) {
		pr_err("blkdev_issue_discard() failed: %d\n", ret);
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	return 0;
}

static sense_reason_t
iblock_execute_zero_out(struct block_device *bdev, struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct scatterlist *sg = &cmd->t_data_sg[0];
	unsigned char *buf, *not_zero;
	int ret;

	buf = kmap(sg_page(sg)) + sg->offset;
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	/*
	 * Fall back to block_execute_write_same() slow-path if
	 * incoming WRITE_SAME payload does not contain zeros.
	 */
	not_zero = memchr_inv(buf, 0x00, cmd->data_length);
	kunmap(sg_page(sg));

	if (not_zero)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	ret = blkdev_issue_zeroout(bdev,
				target_to_linux_sector(dev, cmd->t_task_lba),
				target_to_linux_sector(dev,
					sbc_get_write_same_sectors(cmd)),
				GFP_KERNEL, BLKDEV_ZERO_NOUNMAP);
	if (ret)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	target_complete_cmd(cmd, SAM_STAT_GOOD);
	return 0;
}

static sense_reason_t
iblock_execute_write_same(struct se_cmd *cmd)
{
	struct block_device *bdev = IBLOCK_DEV(cmd->se_dev)->ibd_bd;
	struct iblock_req *ibr;
	struct scatterlist *sg;
	struct bio *bio;
	struct bio_list list;
	struct se_device *dev = cmd->se_dev;
	sector_t block_lba = target_to_linux_sector(dev, cmd->t_task_lba);
	sector_t sectors = target_to_linux_sector(dev,
					sbc_get_write_same_sectors(cmd));

	if (cmd->prot_op) {
		pr_err("WRITE_SAME: Protection information with IBLOCK"
		       " backends not supported\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (!cmd->t_data_nents)
		return TCM_INVALID_CDB_FIELD;

	sg = &cmd->t_data_sg[0];

	if (cmd->t_data_nents > 1 ||
	    sg->length != cmd->se_dev->dev_attrib.block_size) {
		pr_err("WRITE_SAME: Illegal SGL t_data_nents: %u length: %u"
			" block_size: %u\n", cmd->t_data_nents, sg->length,
			cmd->se_dev->dev_attrib.block_size);
		return TCM_INVALID_CDB_FIELD;
	}

	if (bdev_write_zeroes_sectors(bdev)) {
		if (!iblock_execute_zero_out(bdev, cmd))
			return 0;
	}

	ibr = kzalloc(sizeof(struct iblock_req), GFP_KERNEL);
	if (!ibr)
		goto fail;
	cmd->priv = ibr;

	bio = iblock_get_bio(cmd, block_lba, 1, REQ_OP_WRITE);
	if (!bio)
		goto fail_free_ibr;

	bio_list_init(&list);
	bio_list_add(&list, bio);

	refcount_set(&ibr->pending, 1);

	while (sectors) {
		while (bio_add_page(bio, sg_page(sg), sg->length, sg->offset)
				!= sg->length) {

			bio = iblock_get_bio(cmd, block_lba, 1, REQ_OP_WRITE);
			if (!bio)
				goto fail_put_bios;

			refcount_inc(&ibr->pending);
			bio_list_add(&list, bio);
		}

		/* Always in 512 byte units for Linux/Block */
		block_lba += sg->length >> SECTOR_SHIFT;
		sectors -= sg->length >> SECTOR_SHIFT;
	}

	iblock_submit_bios(&list);
	return 0;

fail_put_bios:
	while ((bio = bio_list_pop(&list)))
		bio_put(bio);
fail_free_ibr:
	kfree(ibr);
fail:
	return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
}

enum {
	Opt_udev_path, Opt_readonly, Opt_force, Opt_err
};

static match_table_t tokens = {
	{Opt_udev_path, "udev_path=%s"},
	{Opt_readonly, "readonly=%d"},
	{Opt_force, "force=%d"},
	{Opt_err, NULL}
};

static ssize_t iblock_set_configfs_dev_params(struct se_device *dev,
		const char *page, ssize_t count)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	char *orig, *ptr, *arg_p, *opts;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, token;
	unsigned long tmp_readonly;

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;

	while ((ptr = strsep(&opts, ",\n")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_udev_path:
			if (ib_dev->ibd_bd) {
				pr_err("Unable to set udev_path= while"
					" ib_dev->ibd_bd exists\n");
				ret = -EEXIST;
				goto out;
			}
			if (match_strlcpy(ib_dev->ibd_udev_path, &args[0],
				SE_UDEV_PATH_LEN) == 0) {
				ret = -EINVAL;
				break;
			}
			pr_debug("IBLOCK: Referencing UDEV path: %s\n",
					ib_dev->ibd_udev_path);
			ib_dev->ibd_flags |= IBDF_HAS_UDEV_PATH;
			break;
		case Opt_readonly:
			arg_p = match_strdup(&args[0]);
			if (!arg_p) {
				ret = -ENOMEM;
				break;
			}
			ret = kstrtoul(arg_p, 0, &tmp_readonly);
			kfree(arg_p);
			if (ret < 0) {
				pr_err("kstrtoul() failed for"
						" readonly=\n");
				goto out;
			}
			ib_dev->ibd_readonly = tmp_readonly;
			pr_debug("IBLOCK: readonly: %d\n", ib_dev->ibd_readonly);
			break;
		case Opt_force:
			break;
		default:
			break;
		}
	}

out:
	kfree(orig);
	return (!ret) ? count : ret;
}

static ssize_t iblock_show_configfs_dev_params(struct se_device *dev, char *b)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct block_device *bd = ib_dev->ibd_bd;
	ssize_t bl = 0;

	if (bd)
		bl += sprintf(b + bl, "iBlock device: %pg", bd);
	if (ib_dev->ibd_flags & IBDF_HAS_UDEV_PATH)
		bl += sprintf(b + bl, "  UDEV PATH: %s",
				ib_dev->ibd_udev_path);
	bl += sprintf(b + bl, "  readonly: %d\n", ib_dev->ibd_readonly);

	bl += sprintf(b + bl, "        ");
	if (bd) {
		bl += sprintf(b + bl, "Major: %d Minor: %d  %s\n",
			MAJOR(bd->bd_dev), MINOR(bd->bd_dev),
			"CLAIMED: IBLOCK");
	} else {
		bl += sprintf(b + bl, "Major: 0 Minor: 0\n");
	}

	return bl;
}

static int
iblock_alloc_bip(struct se_cmd *cmd, struct bio *bio,
		 struct sg_mapping_iter *miter)
{
	struct se_device *dev = cmd->se_dev;
	struct blk_integrity *bi;
	struct bio_integrity_payload *bip;
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	int rc;
	size_t resid, len;

	bi = bdev_get_integrity(ib_dev->ibd_bd);
	if (!bi) {
		pr_err("Unable to locate bio_integrity\n");
		return -ENODEV;
	}

	bip = bio_integrity_alloc(bio, GFP_NOIO, bio_max_segs(cmd->t_prot_nents));
	if (IS_ERR(bip)) {
		pr_err("Unable to allocate bio_integrity_payload\n");
		return PTR_ERR(bip);
	}

	/* virtual start sector must be in integrity interval units */
	bip_set_seed(bip, bio->bi_iter.bi_sector >>
				  (bi->interval_exp - SECTOR_SHIFT));

	pr_debug("IBLOCK BIP Size: %u Sector: %llu\n", bip->bip_iter.bi_size,
		 (unsigned long long)bip->bip_iter.bi_sector);

	resid = bio_integrity_bytes(bi, bio_sectors(bio));
	while (resid > 0 && sg_miter_next(miter)) {

		len = min_t(size_t, miter->length, resid);
		rc = bio_integrity_add_page(bio, miter->page, len,
					    offset_in_page(miter->addr));
		if (rc != len) {
			pr_err("bio_integrity_add_page() failed; %d\n", rc);
			sg_miter_stop(miter);
			return -ENOMEM;
		}

		pr_debug("Added bio integrity page: %p length: %zu offset: %lu\n",
			  miter->page, len, offset_in_page(miter->addr));

		resid -= len;
		if (len < miter->length)
			miter->consumed -= miter->length - len;
	}
	sg_miter_stop(miter);

	return 0;
}

static sense_reason_t
iblock_execute_rw(struct se_cmd *cmd, struct scatterlist *sgl, u32 sgl_nents,
		  enum dma_data_direction data_direction)
{
	struct se_device *dev = cmd->se_dev;
	sector_t block_lba = target_to_linux_sector(dev, cmd->t_task_lba);
	struct iblock_req *ibr;
	struct bio *bio;
	struct bio_list list;
	struct scatterlist *sg;
	u32 sg_num = sgl_nents;
	blk_opf_t opf;
	unsigned bio_cnt;
	int i, rc;
	struct sg_mapping_iter prot_miter;
	unsigned int miter_dir;

	if (data_direction == DMA_TO_DEVICE) {
		struct iblock_dev *ib_dev = IBLOCK_DEV(dev);

		/*
		 * Set bits to indicate WRITE_ODIRECT so we are not throttled
		 * by WBT.
		 */
		opf = REQ_OP_WRITE | REQ_SYNC | REQ_IDLE;
		/*
		 * Force writethrough using REQ_FUA if a volatile write cache
		 * is not enabled, or if initiator set the Force Unit Access bit.
		 */
		miter_dir = SG_MITER_TO_SG;
		if (bdev_fua(ib_dev->ibd_bd)) {
			if (cmd->se_cmd_flags & SCF_FUA)
				opf |= REQ_FUA;
			else if (!bdev_write_cache(ib_dev->ibd_bd))
				opf |= REQ_FUA;
		}
	} else {
		opf = REQ_OP_READ;
		miter_dir = SG_MITER_FROM_SG;
	}

	ibr = kzalloc(sizeof(struct iblock_req), GFP_KERNEL);
	if (!ibr)
		goto fail;
	cmd->priv = ibr;

	if (!sgl_nents) {
		refcount_set(&ibr->pending, 1);
		iblock_complete_cmd(cmd, BLK_STS_OK);
		return 0;
	}

	bio = iblock_get_bio(cmd, block_lba, sgl_nents, opf);
	if (!bio)
		goto fail_free_ibr;

	bio_list_init(&list);
	bio_list_add(&list, bio);

	refcount_set(&ibr->pending, 2);
	bio_cnt = 1;

	if (cmd->prot_type && dev->dev_attrib.pi_prot_type)
		sg_miter_start(&prot_miter, cmd->t_prot_sg, cmd->t_prot_nents,
			       miter_dir);

	for_each_sg(sgl, sg, sgl_nents, i) {
		/*
		 * XXX: if the length the device accepts is shorter than the
		 *	length of the S/G list entry this will cause and
		 *	endless loop.  Better hope no driver uses huge pages.
		 */
		while (bio_add_page(bio, sg_page(sg), sg->length, sg->offset)
				!= sg->length) {
			if (cmd->prot_type && dev->dev_attrib.pi_prot_type) {
				rc = iblock_alloc_bip(cmd, bio, &prot_miter);
				if (rc)
					goto fail_put_bios;
			}

			if (bio_cnt >= IBLOCK_MAX_BIO_PER_TASK) {
				iblock_submit_bios(&list);
				bio_cnt = 0;
			}

			bio = iblock_get_bio(cmd, block_lba, sg_num, opf);
			if (!bio)
				goto fail_put_bios;

			refcount_inc(&ibr->pending);
			bio_list_add(&list, bio);
			bio_cnt++;
		}

		/* Always in 512 byte units for Linux/Block */
		block_lba += sg->length >> SECTOR_SHIFT;
		sg_num--;
	}

	if (cmd->prot_type && dev->dev_attrib.pi_prot_type) {
		rc = iblock_alloc_bip(cmd, bio, &prot_miter);
		if (rc)
			goto fail_put_bios;
	}

	iblock_submit_bios(&list);
	iblock_complete_cmd(cmd, BLK_STS_OK);
	return 0;

fail_put_bios:
	while ((bio = bio_list_pop(&list)))
		bio_put(bio);
fail_free_ibr:
	kfree(ibr);
fail:
	return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
}

static sense_reason_t iblock_execute_pr_out(struct se_cmd *cmd, u8 sa, u64 key,
					    u64 sa_key, u8 type, bool aptpl)
{
	struct se_device *dev = cmd->se_dev;
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct block_device *bdev = ib_dev->ibd_bd;
	const struct pr_ops *ops = bdev->bd_disk->fops->pr_ops;
	int ret;

	if (!ops) {
		pr_err("Block device does not support pr_ops but iblock device has been configured for PR passthrough.\n");
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}

	switch (sa) {
	case PRO_REGISTER:
	case PRO_REGISTER_AND_IGNORE_EXISTING_KEY:
		if (!ops->pr_register) {
			pr_err("block device does not support pr_register.\n");
			return TCM_UNSUPPORTED_SCSI_OPCODE;
		}

		/* The block layer pr ops always enables aptpl */
		if (!aptpl)
			pr_info("APTPL not set by initiator, but will be used.\n");

		ret = ops->pr_register(bdev, key, sa_key,
				sa == PRO_REGISTER ? 0 : PR_FL_IGNORE_KEY);
		break;
	case PRO_RESERVE:
		if (!ops->pr_reserve) {
			pr_err("block_device does not support pr_reserve.\n");
			return TCM_UNSUPPORTED_SCSI_OPCODE;
		}

		ret = ops->pr_reserve(bdev, key, scsi_pr_type_to_block(type), 0);
		break;
	case PRO_CLEAR:
		if (!ops->pr_clear) {
			pr_err("block_device does not support pr_clear.\n");
			return TCM_UNSUPPORTED_SCSI_OPCODE;
		}

		ret = ops->pr_clear(bdev, key);
		break;
	case PRO_PREEMPT:
	case PRO_PREEMPT_AND_ABORT:
		if (!ops->pr_clear) {
			pr_err("block_device does not support pr_preempt.\n");
			return TCM_UNSUPPORTED_SCSI_OPCODE;
		}

		ret = ops->pr_preempt(bdev, key, sa_key,
				      scsi_pr_type_to_block(type),
				      sa == PRO_PREEMPT_AND_ABORT);
		break;
	case PRO_RELEASE:
		if (!ops->pr_clear) {
			pr_err("block_device does not support pr_pclear.\n");
			return TCM_UNSUPPORTED_SCSI_OPCODE;
		}

		ret = ops->pr_release(bdev, key, scsi_pr_type_to_block(type));
		break;
	default:
		pr_err("Unknown PERSISTENT_RESERVE_OUT SA: 0x%02x\n", sa);
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}

	if (!ret)
		return TCM_NO_SENSE;
	else if (ret == PR_STS_RESERVATION_CONFLICT)
		return TCM_RESERVATION_CONFLICT;
	else
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
}

static void iblock_pr_report_caps(unsigned char *param_data)
{
	u16 len = 8;

	put_unaligned_be16(len, &param_data[0]);
	/*
	 * When using the pr_ops passthrough method we only support exporting
	 * the device through one target port because from the backend module
	 * level we can't see the target port config. As a result we only
	 * support registration directly from the I_T nexus the cmd is sent
	 * through and do not set ATP_C here.
	 *
	 * The block layer pr_ops do not support passing in initiators so
	 * we don't set SIP_C here.
	 */
	/* PTPL_C: Persistence across Target Power Loss bit */
	param_data[2] |= 0x01;
	/*
	 * We are filling in the PERSISTENT RESERVATION TYPE MASK below, so
	 * set the TMV: Task Mask Valid bit.
	 */
	param_data[3] |= 0x80;
	/*
	 * Change ALLOW COMMANDs to 0x20 or 0x40 later from Table 166
	 */
	param_data[3] |= 0x10; /* ALLOW COMMANDs field 001b */
	/*
	 * PTPL_A: Persistence across Target Power Loss Active bit. The block
	 * layer pr ops always enables this so report it active.
	 */
	param_data[3] |= 0x01;
	/*
	 * Setup the PERSISTENT RESERVATION TYPE MASK from Table 212 spc4r37.
	 */
	param_data[4] |= 0x80; /* PR_TYPE_EXCLUSIVE_ACCESS_ALLREG */
	param_data[4] |= 0x40; /* PR_TYPE_EXCLUSIVE_ACCESS_REGONLY */
	param_data[4] |= 0x20; /* PR_TYPE_WRITE_EXCLUSIVE_REGONLY */
	param_data[4] |= 0x08; /* PR_TYPE_EXCLUSIVE_ACCESS */
	param_data[4] |= 0x02; /* PR_TYPE_WRITE_EXCLUSIVE */
	param_data[5] |= 0x01; /* PR_TYPE_EXCLUSIVE_ACCESS_ALLREG */
}

static sense_reason_t iblock_pr_read_keys(struct se_cmd *cmd,
					  unsigned char *param_data)
{
	struct se_device *dev = cmd->se_dev;
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct block_device *bdev = ib_dev->ibd_bd;
	const struct pr_ops *ops = bdev->bd_disk->fops->pr_ops;
	int i, len, paths, data_offset;
	struct pr_keys *keys;
	sense_reason_t ret;

	if (!ops) {
		pr_err("Block device does not support pr_ops but iblock device has been configured for PR passthrough.\n");
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}

	if (!ops->pr_read_keys) {
		pr_err("Block device does not support read_keys.\n");
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}

	/*
	 * We don't know what's under us, but dm-multipath will register every
	 * path with the same key, so start off with enough space for 16 paths.
	 * which is not a lot of memory and should normally be enough.
	 */
	paths = 16;
retry:
	len = 8 * paths;
	keys = kzalloc(sizeof(*keys) + len, GFP_KERNEL);
	if (!keys)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	keys->num_keys = paths;
	if (!ops->pr_read_keys(bdev, keys)) {
		if (keys->num_keys > paths) {
			kfree(keys);
			paths *= 2;
			goto retry;
		}
	} else {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto free_keys;
	}

	ret = TCM_NO_SENSE;

	put_unaligned_be32(keys->generation, &param_data[0]);
	if (!keys->num_keys) {
		put_unaligned_be32(0, &param_data[4]);
		goto free_keys;
	}

	put_unaligned_be32(8 * keys->num_keys, &param_data[4]);

	data_offset = 8;
	for (i = 0; i < keys->num_keys; i++) {
		if (data_offset + 8 > cmd->data_length)
			break;

		put_unaligned_be64(keys->keys[i], &param_data[data_offset]);
		data_offset += 8;
	}

free_keys:
	kfree(keys);
	return ret;
}

static sense_reason_t iblock_pr_read_reservation(struct se_cmd *cmd,
						 unsigned char *param_data)
{
	struct se_device *dev = cmd->se_dev;
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct block_device *bdev = ib_dev->ibd_bd;
	const struct pr_ops *ops = bdev->bd_disk->fops->pr_ops;
	struct pr_held_reservation rsv = { };

	if (!ops) {
		pr_err("Block device does not support pr_ops but iblock device has been configured for PR passthrough.\n");
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}

	if (!ops->pr_read_reservation) {
		pr_err("Block device does not support read_keys.\n");
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}

	if (ops->pr_read_reservation(bdev, &rsv))
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	put_unaligned_be32(rsv.generation, &param_data[0]);
	if (!block_pr_type_to_scsi(rsv.type)) {
		put_unaligned_be32(0, &param_data[4]);
		return TCM_NO_SENSE;
	}

	put_unaligned_be32(16, &param_data[4]);

	if (cmd->data_length < 16)
		return TCM_NO_SENSE;
	put_unaligned_be64(rsv.key, &param_data[8]);

	if (cmd->data_length < 22)
		return TCM_NO_SENSE;
	param_data[21] = block_pr_type_to_scsi(rsv.type);

	return TCM_NO_SENSE;
}

static sense_reason_t iblock_execute_pr_in(struct se_cmd *cmd, u8 sa,
					   unsigned char *param_data)
{
	sense_reason_t ret = TCM_NO_SENSE;

	switch (sa) {
	case PRI_REPORT_CAPABILITIES:
		iblock_pr_report_caps(param_data);
		break;
	case PRI_READ_KEYS:
		ret = iblock_pr_read_keys(cmd, param_data);
		break;
	case PRI_READ_RESERVATION:
		ret = iblock_pr_read_reservation(cmd, param_data);
		break;
	default:
		pr_err("Unknown PERSISTENT_RESERVE_IN SA: 0x%02x\n", sa);
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}

	return ret;
}

static sector_t iblock_get_alignment_offset_lbas(struct se_device *dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct block_device *bd = ib_dev->ibd_bd;
	int ret;

	ret = bdev_alignment_offset(bd);
	if (ret == -1)
		return 0;

	/* convert offset-bytes to offset-lbas */
	return ret / bdev_logical_block_size(bd);
}

static unsigned int iblock_get_lbppbe(struct se_device *dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct block_device *bd = ib_dev->ibd_bd;
	unsigned int logs_per_phys =
		bdev_physical_block_size(bd) / bdev_logical_block_size(bd);

	return ilog2(logs_per_phys);
}

static unsigned int iblock_get_io_min(struct se_device *dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct block_device *bd = ib_dev->ibd_bd;

	return bdev_io_min(bd);
}

static unsigned int iblock_get_io_opt(struct se_device *dev)
{
	struct iblock_dev *ib_dev = IBLOCK_DEV(dev);
	struct block_device *bd = ib_dev->ibd_bd;

	return bdev_io_opt(bd);
}

static struct exec_cmd_ops iblock_exec_cmd_ops = {
	.execute_rw		= iblock_execute_rw,
	.execute_sync_cache	= iblock_execute_sync_cache,
	.execute_write_same	= iblock_execute_write_same,
	.execute_unmap		= iblock_execute_unmap,
	.execute_pr_out		= iblock_execute_pr_out,
	.execute_pr_in		= iblock_execute_pr_in,
};

static sense_reason_t
iblock_parse_cdb(struct se_cmd *cmd)
{
	return sbc_parse_cdb(cmd, &iblock_exec_cmd_ops);
}

static bool iblock_get_write_cache(struct se_device *dev)
{
	return bdev_write_cache(IBLOCK_DEV(dev)->ibd_bd);
}

static const struct target_backend_ops iblock_ops = {
	.name			= "iblock",
	.inquiry_prod		= "IBLOCK",
	.transport_flags_changeable = TRANSPORT_FLAG_PASSTHROUGH_PGR,
	.inquiry_rev		= IBLOCK_VERSION,
	.owner			= THIS_MODULE,
	.attach_hba		= iblock_attach_hba,
	.detach_hba		= iblock_detach_hba,
	.alloc_device		= iblock_alloc_device,
	.configure_device	= iblock_configure_device,
	.destroy_device		= iblock_destroy_device,
	.free_device		= iblock_free_device,
	.configure_unmap	= iblock_configure_unmap,
	.plug_device		= iblock_plug_device,
	.unplug_device		= iblock_unplug_device,
	.parse_cdb		= iblock_parse_cdb,
	.set_configfs_dev_params = iblock_set_configfs_dev_params,
	.show_configfs_dev_params = iblock_show_configfs_dev_params,
	.get_device_type	= sbc_get_device_type,
	.get_blocks		= iblock_get_blocks,
	.get_alignment_offset_lbas = iblock_get_alignment_offset_lbas,
	.get_lbppbe		= iblock_get_lbppbe,
	.get_io_min		= iblock_get_io_min,
	.get_io_opt		= iblock_get_io_opt,
	.get_write_cache	= iblock_get_write_cache,
	.tb_dev_attrib_attrs	= sbc_attrib_attrs,
};

static int __init iblock_module_init(void)
{
	return transport_backend_register(&iblock_ops);
}

static void __exit iblock_module_exit(void)
{
	target_backend_unregister(&iblock_ops);
}

MODULE_DESCRIPTION("TCM IBLOCK subsystem plugin");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(iblock_module_init);
module_exit(iblock_module_exit);
