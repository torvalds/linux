/*******************************************************************************
 * Filename:  target_core_iblock.c
 *
 * This file contains the Storage Engine  <-> Linux BlockIO transport
 * specific functions.
 *
 * Copyright (c) 2003, 2004, 2005 PyX Technologies, Inc.
 * Copyright (c) 2005, 2006, 2007 SBE, Inc.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/bio.h>
#include <linux/genhd.h>
#include <linux/file.h>
#include <linux/module.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <asm/unaligned.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>

#include "target_core_iblock.h"

#define IBLOCK_MAX_BIO_PER_TASK	 32	/* max # of bios to submit at a time */
#define IBLOCK_BIO_POOL_SIZE	128

static struct se_subsystem_api iblock_template;

static void iblock_bio_done(struct bio *, int);

/*	iblock_attach_hba(): (Part of se_subsystem_api_t template)
 *
 *
 */
static int iblock_attach_hba(struct se_hba *hba, u32 host_id)
{
	pr_debug("CORE_HBA[%d] - TCM iBlock HBA Driver %s on"
		" Generic Target Core Stack %s\n", hba->hba_id,
		IBLOCK_VERSION, TARGET_CORE_MOD_VERSION);
	return 0;
}

static void iblock_detach_hba(struct se_hba *hba)
{
}

static void *iblock_allocate_virtdevice(struct se_hba *hba, const char *name)
{
	struct iblock_dev *ib_dev = NULL;

	ib_dev = kzalloc(sizeof(struct iblock_dev), GFP_KERNEL);
	if (!ib_dev) {
		pr_err("Unable to allocate struct iblock_dev\n");
		return NULL;
	}

	pr_debug( "IBLOCK: Allocated ib_dev for %s\n", name);

	return ib_dev;
}

static struct se_device *iblock_create_virtdevice(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	void *p)
{
	struct iblock_dev *ib_dev = p;
	struct se_device *dev;
	struct se_dev_limits dev_limits;
	struct block_device *bd = NULL;
	struct request_queue *q;
	struct queue_limits *limits;
	u32 dev_flags = 0;
	fmode_t mode;
	int ret = -EINVAL;

	if (!ib_dev) {
		pr_err("Unable to locate struct iblock_dev parameter\n");
		return ERR_PTR(ret);
	}
	memset(&dev_limits, 0, sizeof(struct se_dev_limits));

	ib_dev->ibd_bio_set = bioset_create(IBLOCK_BIO_POOL_SIZE, 0);
	if (!ib_dev->ibd_bio_set) {
		pr_err("IBLOCK: Unable to create bioset()\n");
		return ERR_PTR(-ENOMEM);
	}
	pr_debug("IBLOCK: Created bio_set()\n");
	/*
	 * iblock_check_configfs_dev_params() ensures that ib_dev->ibd_udev_path
	 * must already have been set in order for echo 1 > $HBA/$DEV/enable to run.
	 */
	pr_debug( "IBLOCK: Claiming struct block_device: %s\n",
			ib_dev->ibd_udev_path);

	mode = FMODE_READ|FMODE_EXCL;
	if (!ib_dev->ibd_readonly)
		mode |= FMODE_WRITE;

	bd = blkdev_get_by_path(ib_dev->ibd_udev_path, mode, ib_dev);
	if (IS_ERR(bd)) {
		ret = PTR_ERR(bd);
		goto failed;
	}
	/*
	 * Setup the local scope queue_limits from struct request_queue->limits
	 * to pass into transport_add_device_to_core_hba() as struct se_dev_limits.
	 */
	q = bdev_get_queue(bd);
	limits = &dev_limits.limits;
	limits->logical_block_size = bdev_logical_block_size(bd);
	limits->max_hw_sectors = UINT_MAX;
	limits->max_sectors = UINT_MAX;
	dev_limits.hw_queue_depth = q->nr_requests;
	dev_limits.queue_depth = q->nr_requests;

	ib_dev->ibd_bd = bd;

	dev = transport_add_device_to_core_hba(hba,
			&iblock_template, se_dev, dev_flags, ib_dev,
			&dev_limits, "IBLOCK", IBLOCK_VERSION);
	if (!dev)
		goto failed;

	/*
	 * Check if the underlying struct block_device request_queue supports
	 * the QUEUE_FLAG_DISCARD bit for UNMAP/WRITE_SAME in SCSI + TRIM
	 * in ATA and we need to set TPE=1
	 */
	if (blk_queue_discard(q)) {
		dev->se_sub_dev->se_dev_attrib.max_unmap_lba_count =
				q->limits.max_discard_sectors;
		/*
		 * Currently hardcoded to 1 in Linux/SCSI code..
		 */
		dev->se_sub_dev->se_dev_attrib.max_unmap_block_desc_count = 1;
		dev->se_sub_dev->se_dev_attrib.unmap_granularity =
				q->limits.discard_granularity >> 9;
		dev->se_sub_dev->se_dev_attrib.unmap_granularity_alignment =
				q->limits.discard_alignment;

		pr_debug("IBLOCK: BLOCK Discard support available,"
				" disabled by default\n");
	}

	if (blk_queue_nonrot(q))
		dev->se_sub_dev->se_dev_attrib.is_nonrot = 1;

	return dev;

failed:
	if (ib_dev->ibd_bio_set) {
		bioset_free(ib_dev->ibd_bio_set);
		ib_dev->ibd_bio_set = NULL;
	}
	ib_dev->ibd_bd = NULL;
	return ERR_PTR(ret);
}

static void iblock_free_device(void *p)
{
	struct iblock_dev *ib_dev = p;

	if (ib_dev->ibd_bd != NULL)
		blkdev_put(ib_dev->ibd_bd, FMODE_WRITE|FMODE_READ|FMODE_EXCL);
	if (ib_dev->ibd_bio_set != NULL)
		bioset_free(ib_dev->ibd_bio_set);
	kfree(ib_dev);
}

static unsigned long long iblock_emulate_read_cap_with_block_size(
	struct se_device *dev,
	struct block_device *bd,
	struct request_queue *q)
{
	unsigned long long blocks_long = (div_u64(i_size_read(bd->bd_inode),
					bdev_logical_block_size(bd)) - 1);
	u32 block_size = bdev_logical_block_size(bd);

	if (block_size == dev->se_sub_dev->se_dev_attrib.block_size)
		return blocks_long;

	switch (block_size) {
	case 4096:
		switch (dev->se_sub_dev->se_dev_attrib.block_size) {
		case 2048:
			blocks_long <<= 1;
			break;
		case 1024:
			blocks_long <<= 2;
			break;
		case 512:
			blocks_long <<= 3;
		default:
			break;
		}
		break;
	case 2048:
		switch (dev->se_sub_dev->se_dev_attrib.block_size) {
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
		switch (dev->se_sub_dev->se_dev_attrib.block_size) {
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
		switch (dev->se_sub_dev->se_dev_attrib.block_size) {
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

static void iblock_end_io_flush(struct bio *bio, int err)
{
	struct se_cmd *cmd = bio->bi_private;

	if (err)
		pr_err("IBLOCK: cache flush failed: %d\n", err);

	if (cmd) {
		if (err) {
			cmd->scsi_sense_reason =
				TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			target_complete_cmd(cmd, SAM_STAT_CHECK_CONDITION);
		} else {
			target_complete_cmd(cmd, SAM_STAT_GOOD);
		}
	}

	bio_put(bio);
}

/*
 * Implement SYCHRONIZE CACHE.  Note that we can't handle lba ranges and must
 * always flush the whole cache.
 */
static int iblock_execute_sync_cache(struct se_cmd *cmd)
{
	struct iblock_dev *ib_dev = cmd->se_dev->dev_ptr;
	int immed = (cmd->t_task_cdb[1] & 0x2);
	struct bio *bio;

	/*
	 * If the Immediate bit is set, queue up the GOOD response
	 * for this SYNCHRONIZE_CACHE op.
	 */
	if (immed)
		target_complete_cmd(cmd, SAM_STAT_GOOD);

	bio = bio_alloc(GFP_KERNEL, 0);
	bio->bi_end_io = iblock_end_io_flush;
	bio->bi_bdev = ib_dev->ibd_bd;
	if (!immed)
		bio->bi_private = cmd;
	submit_bio(WRITE_FLUSH, bio);
	return 0;
}

static int iblock_execute_unmap(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct iblock_dev *ibd = dev->dev_ptr;
	unsigned char *buf, *ptr = NULL;
	sector_t lba;
	int size = cmd->data_length;
	u32 range;
	int ret = 0;
	int dl, bd_dl;

	buf = transport_kmap_data_sg(cmd);

	dl = get_unaligned_be16(&buf[0]);
	bd_dl = get_unaligned_be16(&buf[2]);

	size = min(size - 8, bd_dl);
	if (size / 16 > dev->se_sub_dev->se_dev_attrib.max_unmap_block_desc_count) {
		cmd->scsi_sense_reason = TCM_INVALID_PARAMETER_LIST;
		ret = -EINVAL;
		goto err;
	}

	/* First UNMAP block descriptor starts at 8 byte offset */
	ptr = &buf[8];
	pr_debug("UNMAP: Sub: %s Using dl: %u bd_dl: %u size: %u"
		" ptr: %p\n", dev->transport->name, dl, bd_dl, size, ptr);

	while (size >= 16) {
		lba = get_unaligned_be64(&ptr[0]);
		range = get_unaligned_be32(&ptr[8]);
		pr_debug("UNMAP: Using lba: %llu and range: %u\n",
				 (unsigned long long)lba, range);

		if (range > dev->se_sub_dev->se_dev_attrib.max_unmap_lba_count) {
			cmd->scsi_sense_reason = TCM_INVALID_PARAMETER_LIST;
			ret = -EINVAL;
			goto err;
		}

		if (lba + range > dev->transport->get_blocks(dev) + 1) {
			cmd->scsi_sense_reason = TCM_ADDRESS_OUT_OF_RANGE;
			ret = -EINVAL;
			goto err;
		}

		ret = blkdev_issue_discard(ibd->ibd_bd, lba, range,
					   GFP_KERNEL, 0);
		if (ret < 0) {
			pr_err("blkdev_issue_discard() failed: %d\n",
					ret);
			goto err;
		}

		ptr += 16;
		size -= 16;
	}

err:
	transport_kunmap_data_sg(cmd);
	if (!ret)
		target_complete_cmd(cmd, GOOD);
	return ret;
}

static int iblock_execute_write_same(struct se_cmd *cmd)
{
	struct iblock_dev *ibd = cmd->se_dev->dev_ptr;
	int ret;

	ret = blkdev_issue_discard(ibd->ibd_bd, cmd->t_task_lba,
				   spc_get_write_same_sectors(cmd), GFP_KERNEL,
				   0);
	if (ret < 0) {
		pr_debug("blkdev_issue_discard() failed for WRITE_SAME\n");
		return ret;
	}

	target_complete_cmd(cmd, GOOD);
	return 0;
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

static ssize_t iblock_set_configfs_dev_params(struct se_hba *hba,
					       struct se_subsystem_dev *se_dev,
					       const char *page, ssize_t count)
{
	struct iblock_dev *ib_dev = se_dev->se_dev_su_ptr;
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
			arg_p = match_strdup(&args[0]);
			if (!arg_p) {
				ret = -ENOMEM;
				break;
			}
			snprintf(ib_dev->ibd_udev_path, SE_UDEV_PATH_LEN,
					"%s", arg_p);
			kfree(arg_p);
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
			ret = strict_strtoul(arg_p, 0, &tmp_readonly);
			kfree(arg_p);
			if (ret < 0) {
				pr_err("strict_strtoul() failed for"
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

static ssize_t iblock_check_configfs_dev_params(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev)
{
	struct iblock_dev *ibd = se_dev->se_dev_su_ptr;

	if (!(ibd->ibd_flags & IBDF_HAS_UDEV_PATH)) {
		pr_err("Missing udev_path= parameters for IBLOCK\n");
		return -EINVAL;
	}

	return 0;
}

static ssize_t iblock_show_configfs_dev_params(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	char *b)
{
	struct iblock_dev *ibd = se_dev->se_dev_su_ptr;
	struct block_device *bd = ibd->ibd_bd;
	char buf[BDEVNAME_SIZE];
	ssize_t bl = 0;

	if (bd)
		bl += sprintf(b + bl, "iBlock device: %s",
				bdevname(bd, buf));
	if (ibd->ibd_flags & IBDF_HAS_UDEV_PATH)
		bl += sprintf(b + bl, "  UDEV PATH: %s",
				ibd->ibd_udev_path);
	bl += sprintf(b + bl, "  readonly: %d\n", ibd->ibd_readonly);

	bl += sprintf(b + bl, "        ");
	if (bd) {
		bl += sprintf(b + bl, "Major: %d Minor: %d  %s\n",
			MAJOR(bd->bd_dev), MINOR(bd->bd_dev), (!bd->bd_contains) ?
			"" : (bd->bd_holder == ibd) ?
			"CLAIMED: IBLOCK" : "CLAIMED: OS");
	} else {
		bl += sprintf(b + bl, "Major: 0 Minor: 0\n");
	}

	return bl;
}

static void iblock_complete_cmd(struct se_cmd *cmd)
{
	struct iblock_req *ibr = cmd->priv;
	u8 status;

	if (!atomic_dec_and_test(&ibr->pending))
		return;

	if (atomic_read(&ibr->ib_bio_err_cnt))
		status = SAM_STAT_CHECK_CONDITION;
	else
		status = SAM_STAT_GOOD;

	target_complete_cmd(cmd, status);
	kfree(ibr);
}

static void iblock_bio_destructor(struct bio *bio)
{
	struct se_cmd *cmd = bio->bi_private;
	struct iblock_dev *ib_dev = cmd->se_dev->dev_ptr;

	bio_free(bio, ib_dev->ibd_bio_set);
}

static struct bio *
iblock_get_bio(struct se_cmd *cmd, sector_t lba, u32 sg_num)
{
	struct iblock_dev *ib_dev = cmd->se_dev->dev_ptr;
	struct bio *bio;

	/*
	 * Only allocate as many vector entries as the bio code allows us to,
	 * we'll loop later on until we have handled the whole request.
	 */
	if (sg_num > BIO_MAX_PAGES)
		sg_num = BIO_MAX_PAGES;

	bio = bio_alloc_bioset(GFP_NOIO, sg_num, ib_dev->ibd_bio_set);
	if (!bio) {
		pr_err("Unable to allocate memory for bio\n");
		return NULL;
	}

	bio->bi_bdev = ib_dev->ibd_bd;
	bio->bi_private = cmd;
	bio->bi_destructor = iblock_bio_destructor;
	bio->bi_end_io = &iblock_bio_done;
	bio->bi_sector = lba;
	return bio;
}

static void iblock_submit_bios(struct bio_list *list, int rw)
{
	struct blk_plug plug;
	struct bio *bio;

	blk_start_plug(&plug);
	while ((bio = bio_list_pop(list)))
		submit_bio(rw, bio);
	blk_finish_plug(&plug);
}

static int iblock_execute_rw(struct se_cmd *cmd)
{
	struct scatterlist *sgl = cmd->t_data_sg;
	u32 sgl_nents = cmd->t_data_nents;
	enum dma_data_direction data_direction = cmd->data_direction;
	struct se_device *dev = cmd->se_dev;
	struct iblock_req *ibr;
	struct bio *bio;
	struct bio_list list;
	struct scatterlist *sg;
	u32 sg_num = sgl_nents;
	sector_t block_lba;
	unsigned bio_cnt;
	int rw;
	int i;

	if (data_direction == DMA_TO_DEVICE) {
		/*
		 * Force data to disk if we pretend to not have a volatile
		 * write cache, or the initiator set the Force Unit Access bit.
		 */
		if (dev->se_sub_dev->se_dev_attrib.emulate_write_cache == 0 ||
		    (dev->se_sub_dev->se_dev_attrib.emulate_fua_write > 0 &&
		     (cmd->se_cmd_flags & SCF_FUA)))
			rw = WRITE_FUA;
		else
			rw = WRITE;
	} else {
		rw = READ;
	}

	/*
	 * Convert the blocksize advertised to the initiator to the 512 byte
	 * units unconditionally used by the Linux block layer.
	 */
	if (dev->se_sub_dev->se_dev_attrib.block_size == 4096)
		block_lba = (cmd->t_task_lba << 3);
	else if (dev->se_sub_dev->se_dev_attrib.block_size == 2048)
		block_lba = (cmd->t_task_lba << 2);
	else if (dev->se_sub_dev->se_dev_attrib.block_size == 1024)
		block_lba = (cmd->t_task_lba << 1);
	else if (dev->se_sub_dev->se_dev_attrib.block_size == 512)
		block_lba = cmd->t_task_lba;
	else {
		pr_err("Unsupported SCSI -> BLOCK LBA conversion:"
				" %u\n", dev->se_sub_dev->se_dev_attrib.block_size);
		cmd->scsi_sense_reason = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		return -ENOSYS;
	}

	ibr = kzalloc(sizeof(struct iblock_req), GFP_KERNEL);
	if (!ibr)
		goto fail;
	cmd->priv = ibr;

	bio = iblock_get_bio(cmd, block_lba, sgl_nents);
	if (!bio)
		goto fail_free_ibr;

	bio_list_init(&list);
	bio_list_add(&list, bio);

	atomic_set(&ibr->pending, 2);
	bio_cnt = 1;

	for_each_sg(sgl, sg, sgl_nents, i) {
		/*
		 * XXX: if the length the device accepts is shorter than the
		 *	length of the S/G list entry this will cause and
		 *	endless loop.  Better hope no driver uses huge pages.
		 */
		while (bio_add_page(bio, sg_page(sg), sg->length, sg->offset)
				!= sg->length) {
			if (bio_cnt >= IBLOCK_MAX_BIO_PER_TASK) {
				iblock_submit_bios(&list, rw);
				bio_cnt = 0;
			}

			bio = iblock_get_bio(cmd, block_lba, sg_num);
			if (!bio)
				goto fail_put_bios;

			atomic_inc(&ibr->pending);
			bio_list_add(&list, bio);
			bio_cnt++;
		}

		/* Always in 512 byte units for Linux/Block */
		block_lba += sg->length >> IBLOCK_LBA_SHIFT;
		sg_num--;
	}

	iblock_submit_bios(&list, rw);
	iblock_complete_cmd(cmd);
	return 0;

fail_put_bios:
	while ((bio = bio_list_pop(&list)))
		bio_put(bio);
fail_free_ibr:
	kfree(ibr);
	cmd->scsi_sense_reason = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
fail:
	return -ENOMEM;
}

static u32 iblock_get_device_rev(struct se_device *dev)
{
	return SCSI_SPC_2; /* Returns SPC-3 in Initiator Data */
}

static u32 iblock_get_device_type(struct se_device *dev)
{
	return TYPE_DISK;
}

static sector_t iblock_get_blocks(struct se_device *dev)
{
	struct iblock_dev *ibd = dev->dev_ptr;
	struct block_device *bd = ibd->ibd_bd;
	struct request_queue *q = bdev_get_queue(bd);

	return iblock_emulate_read_cap_with_block_size(dev, bd, q);
}

static void iblock_bio_done(struct bio *bio, int err)
{
	struct se_cmd *cmd = bio->bi_private;
	struct iblock_req *ibr = cmd->priv;

	/*
	 * Set -EIO if !BIO_UPTODATE and the passed is still err=0
	 */
	if (!test_bit(BIO_UPTODATE, &bio->bi_flags) && !err)
		err = -EIO;

	if (err != 0) {
		pr_err("test_bit(BIO_UPTODATE) failed for bio: %p,"
			" err: %d\n", bio, err);
		/*
		 * Bump the ib_bio_err_cnt and release bio.
		 */
		atomic_inc(&ibr->ib_bio_err_cnt);
		smp_mb__after_atomic_inc();
	}

	bio_put(bio);

	iblock_complete_cmd(cmd);
}

static struct spc_ops iblock_spc_ops = {
	.execute_rw		= iblock_execute_rw,
	.execute_sync_cache	= iblock_execute_sync_cache,
	.execute_write_same	= iblock_execute_write_same,
	.execute_unmap		= iblock_execute_unmap,
};

static int iblock_parse_cdb(struct se_cmd *cmd)
{
	return sbc_parse_cdb(cmd, &iblock_spc_ops);
}

static struct se_subsystem_api iblock_template = {
	.name			= "iblock",
	.owner			= THIS_MODULE,
	.transport_type		= TRANSPORT_PLUGIN_VHBA_PDEV,
	.write_cache_emulated	= 1,
	.fua_write_emulated	= 1,
	.attach_hba		= iblock_attach_hba,
	.detach_hba		= iblock_detach_hba,
	.allocate_virtdevice	= iblock_allocate_virtdevice,
	.create_virtdevice	= iblock_create_virtdevice,
	.free_device		= iblock_free_device,
	.parse_cdb		= iblock_parse_cdb,
	.check_configfs_dev_params = iblock_check_configfs_dev_params,
	.set_configfs_dev_params = iblock_set_configfs_dev_params,
	.show_configfs_dev_params = iblock_show_configfs_dev_params,
	.get_device_rev		= iblock_get_device_rev,
	.get_device_type	= iblock_get_device_type,
	.get_blocks		= iblock_get_blocks,
};

static int __init iblock_module_init(void)
{
	return transport_subsystem_register(&iblock_template);
}

static void iblock_module_exit(void)
{
	transport_subsystem_release(&iblock_template);
}

MODULE_DESCRIPTION("TCM IBLOCK subsystem plugin");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(iblock_module_init);
module_exit(iblock_module_exit);
