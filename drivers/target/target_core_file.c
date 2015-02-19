/*******************************************************************************
 * Filename:  target_core_file.c
 *
 * This file contains the Storage Engine <-> FILEIO transport specific functions
 *
 * (c) Copyright 2005-2013 Datera, Inc.
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
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/falloc.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <asm/unaligned.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_backend_configfs.h>

#include "target_core_file.h"

static inline struct fd_dev *FD_DEV(struct se_device *dev)
{
	return container_of(dev, struct fd_dev, dev);
}

/*	fd_attach_hba(): (Part of se_subsystem_api_t template)
 *
 *
 */
static int fd_attach_hba(struct se_hba *hba, u32 host_id)
{
	struct fd_host *fd_host;

	fd_host = kzalloc(sizeof(struct fd_host), GFP_KERNEL);
	if (!fd_host) {
		pr_err("Unable to allocate memory for struct fd_host\n");
		return -ENOMEM;
	}

	fd_host->fd_host_id = host_id;

	hba->hba_ptr = fd_host;

	pr_debug("CORE_HBA[%d] - TCM FILEIO HBA Driver %s on Generic"
		" Target Core Stack %s\n", hba->hba_id, FD_VERSION,
		TARGET_CORE_MOD_VERSION);
	pr_debug("CORE_HBA[%d] - Attached FILEIO HBA: %u to Generic\n",
		hba->hba_id, fd_host->fd_host_id);

	return 0;
}

static void fd_detach_hba(struct se_hba *hba)
{
	struct fd_host *fd_host = hba->hba_ptr;

	pr_debug("CORE_HBA[%d] - Detached FILEIO HBA: %u from Generic"
		" Target Core\n", hba->hba_id, fd_host->fd_host_id);

	kfree(fd_host);
	hba->hba_ptr = NULL;
}

static struct se_device *fd_alloc_device(struct se_hba *hba, const char *name)
{
	struct fd_dev *fd_dev;
	struct fd_host *fd_host = hba->hba_ptr;

	fd_dev = kzalloc(sizeof(struct fd_dev), GFP_KERNEL);
	if (!fd_dev) {
		pr_err("Unable to allocate memory for struct fd_dev\n");
		return NULL;
	}

	fd_dev->fd_host = fd_host;

	pr_debug("FILEIO: Allocated fd_dev for %p\n", name);

	return &fd_dev->dev;
}

static int fd_configure_device(struct se_device *dev)
{
	struct fd_dev *fd_dev = FD_DEV(dev);
	struct fd_host *fd_host = dev->se_hba->hba_ptr;
	struct file *file;
	struct inode *inode = NULL;
	int flags, ret = -EINVAL;

	if (!(fd_dev->fbd_flags & FBDF_HAS_PATH)) {
		pr_err("Missing fd_dev_name=\n");
		return -EINVAL;
	}

	/*
	 * Use O_DSYNC by default instead of O_SYNC to forgo syncing
	 * of pure timestamp updates.
	 */
	flags = O_RDWR | O_CREAT | O_LARGEFILE | O_DSYNC;

	/*
	 * Optionally allow fd_buffered_io=1 to be enabled for people
	 * who want use the fs buffer cache as an WriteCache mechanism.
	 *
	 * This means that in event of a hard failure, there is a risk
	 * of silent data-loss if the SCSI client has *not* performed a
	 * forced unit access (FUA) write, or issued SYNCHRONIZE_CACHE
	 * to write-out the entire device cache.
	 */
	if (fd_dev->fbd_flags & FDBD_HAS_BUFFERED_IO_WCE) {
		pr_debug("FILEIO: Disabling O_DSYNC, using buffered FILEIO\n");
		flags &= ~O_DSYNC;
	}

	file = filp_open(fd_dev->fd_dev_name, flags, 0600);
	if (IS_ERR(file)) {
		pr_err("filp_open(%s) failed\n", fd_dev->fd_dev_name);
		ret = PTR_ERR(file);
		goto fail;
	}
	fd_dev->fd_file = file;
	/*
	 * If using a block backend with this struct file, we extract
	 * fd_dev->fd_[block,dev]_size from struct block_device.
	 *
	 * Otherwise, we use the passed fd_size= from configfs
	 */
	inode = file->f_mapping->host;
	if (S_ISBLK(inode->i_mode)) {
		struct request_queue *q = bdev_get_queue(inode->i_bdev);
		unsigned long long dev_size;

		fd_dev->fd_block_size = bdev_logical_block_size(inode->i_bdev);
		/*
		 * Determine the number of bytes from i_size_read() minus
		 * one (1) logical sector from underlying struct block_device
		 */
		dev_size = (i_size_read(file->f_mapping->host) -
				       fd_dev->fd_block_size);

		pr_debug("FILEIO: Using size: %llu bytes from struct"
			" block_device blocks: %llu logical_block_size: %d\n",
			dev_size, div_u64(dev_size, fd_dev->fd_block_size),
			fd_dev->fd_block_size);
		/*
		 * Check if the underlying struct block_device request_queue supports
		 * the QUEUE_FLAG_DISCARD bit for UNMAP/WRITE_SAME in SCSI + TRIM
		 * in ATA and we need to set TPE=1
		 */
		if (blk_queue_discard(q)) {
			dev->dev_attrib.max_unmap_lba_count =
				q->limits.max_discard_sectors;
			/*
			 * Currently hardcoded to 1 in Linux/SCSI code..
			 */
			dev->dev_attrib.max_unmap_block_desc_count = 1;
			dev->dev_attrib.unmap_granularity =
				q->limits.discard_granularity >> 9;
			dev->dev_attrib.unmap_granularity_alignment =
				q->limits.discard_alignment;
			pr_debug("IFILE: BLOCK Discard support available,"
					" disabled by default\n");
		}
		/*
		 * Enable write same emulation for IBLOCK and use 0xFFFF as
		 * the smaller WRITE_SAME(10) only has a two-byte block count.
		 */
		dev->dev_attrib.max_write_same_len = 0xFFFF;

		if (blk_queue_nonrot(q))
			dev->dev_attrib.is_nonrot = 1;
	} else {
		if (!(fd_dev->fbd_flags & FBDF_HAS_SIZE)) {
			pr_err("FILEIO: Missing fd_dev_size="
				" parameter, and no backing struct"
				" block_device\n");
			goto fail;
		}

		fd_dev->fd_block_size = FD_BLOCKSIZE;
		/*
		 * Limit UNMAP emulation to 8k Number of LBAs (NoLB)
		 */
		dev->dev_attrib.max_unmap_lba_count = 0x2000;
		/*
		 * Currently hardcoded to 1 in Linux/SCSI code..
		 */
		dev->dev_attrib.max_unmap_block_desc_count = 1;
		dev->dev_attrib.unmap_granularity = 1;
		dev->dev_attrib.unmap_granularity_alignment = 0;

		/*
		 * Limit WRITE_SAME w/ UNMAP=0 emulation to 8k Number of LBAs (NoLB)
		 * based upon struct iovec limit for vfs_writev()
		 */
		dev->dev_attrib.max_write_same_len = 0x1000;
	}

	dev->dev_attrib.hw_block_size = fd_dev->fd_block_size;
	dev->dev_attrib.max_bytes_per_io = FD_MAX_BYTES;
	dev->dev_attrib.hw_max_sectors = FD_MAX_BYTES / fd_dev->fd_block_size;
	dev->dev_attrib.hw_queue_depth = FD_MAX_DEVICE_QUEUE_DEPTH;

	if (fd_dev->fbd_flags & FDBD_HAS_BUFFERED_IO_WCE) {
		pr_debug("FILEIO: Forcing setting of emulate_write_cache=1"
			" with FDBD_HAS_BUFFERED_IO_WCE\n");
		dev->dev_attrib.emulate_write_cache = 1;
	}

	fd_dev->fd_dev_id = fd_host->fd_host_dev_id_count++;
	fd_dev->fd_queue_depth = dev->queue_depth;

	pr_debug("CORE_FILE[%u] - Added TCM FILEIO Device ID: %u at %s,"
		" %llu total bytes\n", fd_host->fd_host_id, fd_dev->fd_dev_id,
			fd_dev->fd_dev_name, fd_dev->fd_dev_size);

	return 0;
fail:
	if (fd_dev->fd_file) {
		filp_close(fd_dev->fd_file, NULL);
		fd_dev->fd_file = NULL;
	}
	return ret;
}

static void fd_free_device(struct se_device *dev)
{
	struct fd_dev *fd_dev = FD_DEV(dev);

	if (fd_dev->fd_file) {
		filp_close(fd_dev->fd_file, NULL);
		fd_dev->fd_file = NULL;
	}

	kfree(fd_dev);
}

static int fd_do_prot_rw(struct se_cmd *cmd, struct fd_prot *fd_prot,
			 int is_write)
{
	struct se_device *se_dev = cmd->se_dev;
	struct fd_dev *dev = FD_DEV(se_dev);
	struct file *prot_fd = dev->fd_prot_file;
	struct scatterlist *sg;
	loff_t pos = (cmd->t_task_lba * se_dev->prot_length);
	unsigned char *buf;
	u32 prot_size, len, size;
	int rc, ret = 1, i;

	prot_size = (cmd->data_length / se_dev->dev_attrib.block_size) *
		     se_dev->prot_length;

	if (!is_write) {
		fd_prot->prot_buf = vzalloc(prot_size);
		if (!fd_prot->prot_buf) {
			pr_err("Unable to allocate fd_prot->prot_buf\n");
			return -ENOMEM;
		}
		buf = fd_prot->prot_buf;

		fd_prot->prot_sg_nents = cmd->t_prot_nents;
		fd_prot->prot_sg = kzalloc(sizeof(struct scatterlist) *
					   fd_prot->prot_sg_nents, GFP_KERNEL);
		if (!fd_prot->prot_sg) {
			pr_err("Unable to allocate fd_prot->prot_sg\n");
			vfree(fd_prot->prot_buf);
			return -ENOMEM;
		}
		size = prot_size;

		for_each_sg(fd_prot->prot_sg, sg, fd_prot->prot_sg_nents, i) {

			len = min_t(u32, PAGE_SIZE, size);
			sg_set_buf(sg, buf, len);
			size -= len;
			buf += len;
		}
	}

	if (is_write) {
		rc = kernel_write(prot_fd, fd_prot->prot_buf, prot_size, pos);
		if (rc < 0 || prot_size != rc) {
			pr_err("kernel_write() for fd_do_prot_rw failed:"
			       " %d\n", rc);
			ret = -EINVAL;
		}
	} else {
		rc = kernel_read(prot_fd, pos, fd_prot->prot_buf, prot_size);
		if (rc < 0) {
			pr_err("kernel_read() for fd_do_prot_rw failed:"
			       " %d\n", rc);
			ret = -EINVAL;
		}
	}

	if (is_write || ret < 0) {
		kfree(fd_prot->prot_sg);
		vfree(fd_prot->prot_buf);
	}

	return ret;
}

static int fd_do_rw(struct se_cmd *cmd, struct scatterlist *sgl,
		u32 sgl_nents, int is_write)
{
	struct se_device *se_dev = cmd->se_dev;
	struct fd_dev *dev = FD_DEV(se_dev);
	struct file *fd = dev->fd_file;
	struct scatterlist *sg;
	struct iovec *iov;
	mm_segment_t old_fs;
	loff_t pos = (cmd->t_task_lba * se_dev->dev_attrib.block_size);
	int ret = 0, i;

	iov = kzalloc(sizeof(struct iovec) * sgl_nents, GFP_KERNEL);
	if (!iov) {
		pr_err("Unable to allocate fd_do_readv iov[]\n");
		return -ENOMEM;
	}

	for_each_sg(sgl, sg, sgl_nents, i) {
		iov[i].iov_len = sg->length;
		iov[i].iov_base = kmap(sg_page(sg)) + sg->offset;
	}

	old_fs = get_fs();
	set_fs(get_ds());

	if (is_write)
		ret = vfs_writev(fd, &iov[0], sgl_nents, &pos);
	else
		ret = vfs_readv(fd, &iov[0], sgl_nents, &pos);

	set_fs(old_fs);

	for_each_sg(sgl, sg, sgl_nents, i)
		kunmap(sg_page(sg));

	kfree(iov);

	if (is_write) {
		if (ret < 0 || ret != cmd->data_length) {
			pr_err("%s() write returned %d\n", __func__, ret);
			return (ret < 0 ? ret : -EINVAL);
		}
	} else {
		/*
		 * Return zeros and GOOD status even if the READ did not return
		 * the expected virt_size for struct file w/o a backing struct
		 * block_device.
		 */
		if (S_ISBLK(file_inode(fd)->i_mode)) {
			if (ret < 0 || ret != cmd->data_length) {
				pr_err("%s() returned %d, expecting %u for "
						"S_ISBLK\n", __func__, ret,
						cmd->data_length);
				return (ret < 0 ? ret : -EINVAL);
			}
		} else {
			if (ret < 0) {
				pr_err("%s() returned %d for non S_ISBLK\n",
						__func__, ret);
				return ret;
			}
		}
	}
	return 1;
}

static sense_reason_t
fd_execute_sync_cache(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct fd_dev *fd_dev = FD_DEV(dev);
	int immed = (cmd->t_task_cdb[1] & 0x2);
	loff_t start, end;
	int ret;

	/*
	 * If the Immediate bit is set, queue up the GOOD response
	 * for this SYNCHRONIZE_CACHE op
	 */
	if (immed)
		target_complete_cmd(cmd, SAM_STAT_GOOD);

	/*
	 * Determine if we will be flushing the entire device.
	 */
	if (cmd->t_task_lba == 0 && cmd->data_length == 0) {
		start = 0;
		end = LLONG_MAX;
	} else {
		start = cmd->t_task_lba * dev->dev_attrib.block_size;
		if (cmd->data_length)
			end = start + cmd->data_length - 1;
		else
			end = LLONG_MAX;
	}

	ret = vfs_fsync_range(fd_dev->fd_file, start, end, 1);
	if (ret != 0)
		pr_err("FILEIO: vfs_fsync_range() failed: %d\n", ret);

	if (immed)
		return 0;

	if (ret)
		target_complete_cmd(cmd, SAM_STAT_CHECK_CONDITION);
	else
		target_complete_cmd(cmd, SAM_STAT_GOOD);

	return 0;
}

static unsigned char *
fd_setup_write_same_buf(struct se_cmd *cmd, struct scatterlist *sg,
		    unsigned int len)
{
	struct se_device *se_dev = cmd->se_dev;
	unsigned int block_size = se_dev->dev_attrib.block_size;
	unsigned int i = 0, end;
	unsigned char *buf, *p, *kmap_buf;

	buf = kzalloc(min_t(unsigned int, len, PAGE_SIZE), GFP_KERNEL);
	if (!buf) {
		pr_err("Unable to allocate fd_execute_write_same buf\n");
		return NULL;
	}

	kmap_buf = kmap(sg_page(sg)) + sg->offset;
	if (!kmap_buf) {
		pr_err("kmap() failed in fd_setup_write_same\n");
		kfree(buf);
		return NULL;
	}
	/*
	 * Fill local *buf to contain multiple WRITE_SAME blocks up to
	 * min(len, PAGE_SIZE)
	 */
	p = buf;
	end = min_t(unsigned int, len, PAGE_SIZE);

	while (i < end) {
		memcpy(p, kmap_buf, block_size);

		i += block_size;
		p += block_size;
	}
	kunmap(sg_page(sg));

	return buf;
}

static sense_reason_t
fd_execute_write_same(struct se_cmd *cmd)
{
	struct se_device *se_dev = cmd->se_dev;
	struct fd_dev *fd_dev = FD_DEV(se_dev);
	struct file *f = fd_dev->fd_file;
	struct scatterlist *sg;
	struct iovec *iov;
	mm_segment_t old_fs;
	sector_t nolb = sbc_get_write_same_sectors(cmd);
	loff_t pos = cmd->t_task_lba * se_dev->dev_attrib.block_size;
	unsigned int len, len_tmp, iov_num;
	int i, rc;
	unsigned char *buf;

	if (!nolb) {
		target_complete_cmd(cmd, SAM_STAT_GOOD);
		return 0;
	}
	sg = &cmd->t_data_sg[0];

	if (cmd->t_data_nents > 1 ||
	    sg->length != cmd->se_dev->dev_attrib.block_size) {
		pr_err("WRITE_SAME: Illegal SGL t_data_nents: %u length: %u"
			" block_size: %u\n", cmd->t_data_nents, sg->length,
			cmd->se_dev->dev_attrib.block_size);
		return TCM_INVALID_CDB_FIELD;
	}

	len = len_tmp = nolb * se_dev->dev_attrib.block_size;
	iov_num = DIV_ROUND_UP(len, PAGE_SIZE);

	buf = fd_setup_write_same_buf(cmd, sg, len);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	iov = vzalloc(sizeof(struct iovec) * iov_num);
	if (!iov) {
		pr_err("Unable to allocate fd_execute_write_same iovecs\n");
		kfree(buf);
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * Map the single fabric received scatterlist block now populated
	 * in *buf into each iovec for I/O submission.
	 */
	for (i = 0; i < iov_num; i++) {
		iov[i].iov_base = buf;
		iov[i].iov_len = min_t(unsigned int, len_tmp, PAGE_SIZE);
		len_tmp -= iov[i].iov_len;
	}

	old_fs = get_fs();
	set_fs(get_ds());
	rc = vfs_writev(f, &iov[0], iov_num, &pos);
	set_fs(old_fs);

	vfree(iov);
	kfree(buf);

	if (rc < 0 || rc != len) {
		pr_err("vfs_writev() returned %d for write same\n", rc);
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	target_complete_cmd(cmd, SAM_STAT_GOOD);
	return 0;
}

static sense_reason_t
fd_do_unmap(struct se_cmd *cmd, void *priv, sector_t lba, sector_t nolb)
{
	struct file *file = priv;
	struct inode *inode = file->f_mapping->host;
	int ret;

	if (S_ISBLK(inode->i_mode)) {
		/* The backend is block device, use discard */
		struct block_device *bdev = inode->i_bdev;

		ret = blkdev_issue_discard(bdev, lba,
				nolb, GFP_KERNEL, 0);
		if (ret < 0) {
			pr_warn("FILEIO: blkdev_issue_discard() failed: %d\n",
				ret);
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		}
	} else {
		/* The backend is normal file, use fallocate */
		struct se_device *se_dev = cmd->se_dev;
		loff_t pos = lba * se_dev->dev_attrib.block_size;
		unsigned int len = nolb * se_dev->dev_attrib.block_size;
		int mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;

		if (!file->f_op->fallocate)
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

		ret = file->f_op->fallocate(file, mode, pos, len);
		if (ret < 0) {
			pr_warn("FILEIO: fallocate() failed: %d\n", ret);
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		}
	}

	return 0;
}

static sense_reason_t
fd_execute_write_same_unmap(struct se_cmd *cmd)
{
	struct se_device *se_dev = cmd->se_dev;
	struct fd_dev *fd_dev = FD_DEV(se_dev);
	struct file *file = fd_dev->fd_file;
	sector_t lba = cmd->t_task_lba;
	sector_t nolb = sbc_get_write_same_sectors(cmd);
	int ret;

	if (!nolb) {
		target_complete_cmd(cmd, SAM_STAT_GOOD);
		return 0;
	}

	ret = fd_do_unmap(cmd, file, lba, nolb);
	if (ret)
		return ret;

	target_complete_cmd(cmd, GOOD);
	return 0;
}

static sense_reason_t
fd_execute_unmap(struct se_cmd *cmd)
{
	struct file *file = FD_DEV(cmd->se_dev)->fd_file;

	return sbc_execute_unmap(cmd, fd_do_unmap, file);
}

static sense_reason_t
fd_execute_rw(struct se_cmd *cmd, struct scatterlist *sgl, u32 sgl_nents,
	      enum dma_data_direction data_direction)
{
	struct se_device *dev = cmd->se_dev;
	struct fd_prot fd_prot;
	sense_reason_t rc;
	int ret = 0;
	/*
	 * We are currently limited by the number of iovecs (2048) per
	 * single vfs_[writev,readv] call.
	 */
	if (cmd->data_length > FD_MAX_BYTES) {
		pr_err("FILEIO: Not able to process I/O of %u bytes due to"
		       "FD_MAX_BYTES: %u iovec count limitiation\n",
			cmd->data_length, FD_MAX_BYTES);
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * Call vectorized fileio functions to map struct scatterlist
	 * physical memory addresses to struct iovec virtual memory.
	 */
	if (data_direction == DMA_FROM_DEVICE) {
		memset(&fd_prot, 0, sizeof(struct fd_prot));

		if (cmd->prot_type) {
			ret = fd_do_prot_rw(cmd, &fd_prot, false);
			if (ret < 0)
				return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		}

		ret = fd_do_rw(cmd, sgl, sgl_nents, 0);

		if (ret > 0 && cmd->prot_type) {
			u32 sectors = cmd->data_length / dev->dev_attrib.block_size;

			rc = sbc_dif_verify_read(cmd, cmd->t_task_lba, sectors,
						 0, fd_prot.prot_sg, 0);
			if (rc) {
				kfree(fd_prot.prot_sg);
				vfree(fd_prot.prot_buf);
				return rc;
			}
			kfree(fd_prot.prot_sg);
			vfree(fd_prot.prot_buf);
		}
	} else {
		memset(&fd_prot, 0, sizeof(struct fd_prot));

		if (cmd->prot_type) {
			u32 sectors = cmd->data_length / dev->dev_attrib.block_size;

			ret = fd_do_prot_rw(cmd, &fd_prot, false);
			if (ret < 0)
				return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

			rc = sbc_dif_verify_write(cmd, cmd->t_task_lba, sectors,
						  0, fd_prot.prot_sg, 0);
			if (rc) {
				kfree(fd_prot.prot_sg);
				vfree(fd_prot.prot_buf);
				return rc;
			}
		}

		ret = fd_do_rw(cmd, sgl, sgl_nents, 1);
		/*
		 * Perform implicit vfs_fsync_range() for fd_do_writev() ops
		 * for SCSI WRITEs with Forced Unit Access (FUA) set.
		 * Allow this to happen independent of WCE=0 setting.
		 */
		if (ret > 0 &&
		    dev->dev_attrib.emulate_fua_write > 0 &&
		    (cmd->se_cmd_flags & SCF_FUA)) {
			struct fd_dev *fd_dev = FD_DEV(dev);
			loff_t start = cmd->t_task_lba *
				dev->dev_attrib.block_size;
			loff_t end;

			if (cmd->data_length)
				end = start + cmd->data_length - 1;
			else
				end = LLONG_MAX;

			vfs_fsync_range(fd_dev->fd_file, start, end, 1);
		}

		if (ret > 0 && cmd->prot_type) {
			ret = fd_do_prot_rw(cmd, &fd_prot, true);
			if (ret < 0)
				return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		}
	}

	if (ret < 0) {
		kfree(fd_prot.prot_sg);
		vfree(fd_prot.prot_buf);
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (ret)
		target_complete_cmd(cmd, SAM_STAT_GOOD);
	return 0;
}

enum {
	Opt_fd_dev_name, Opt_fd_dev_size, Opt_fd_buffered_io, Opt_err
};

static match_table_t tokens = {
	{Opt_fd_dev_name, "fd_dev_name=%s"},
	{Opt_fd_dev_size, "fd_dev_size=%s"},
	{Opt_fd_buffered_io, "fd_buffered_io=%d"},
	{Opt_err, NULL}
};

static ssize_t fd_set_configfs_dev_params(struct se_device *dev,
		const char *page, ssize_t count)
{
	struct fd_dev *fd_dev = FD_DEV(dev);
	char *orig, *ptr, *arg_p, *opts;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, arg, token;

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;

	while ((ptr = strsep(&opts, ",\n")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_fd_dev_name:
			if (match_strlcpy(fd_dev->fd_dev_name, &args[0],
				FD_MAX_DEV_NAME) == 0) {
				ret = -EINVAL;
				break;
			}
			pr_debug("FILEIO: Referencing Path: %s\n",
					fd_dev->fd_dev_name);
			fd_dev->fbd_flags |= FBDF_HAS_PATH;
			break;
		case Opt_fd_dev_size:
			arg_p = match_strdup(&args[0]);
			if (!arg_p) {
				ret = -ENOMEM;
				break;
			}
			ret = kstrtoull(arg_p, 0, &fd_dev->fd_dev_size);
			kfree(arg_p);
			if (ret < 0) {
				pr_err("kstrtoull() failed for"
						" fd_dev_size=\n");
				goto out;
			}
			pr_debug("FILEIO: Referencing Size: %llu"
					" bytes\n", fd_dev->fd_dev_size);
			fd_dev->fbd_flags |= FBDF_HAS_SIZE;
			break;
		case Opt_fd_buffered_io:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			if (arg != 1) {
				pr_err("bogus fd_buffered_io=%d value\n", arg);
				ret = -EINVAL;
				goto out;
			}

			pr_debug("FILEIO: Using buffered I/O"
				" operations for struct fd_dev\n");

			fd_dev->fbd_flags |= FDBD_HAS_BUFFERED_IO_WCE;
			break;
		default:
			break;
		}
	}

out:
	kfree(orig);
	return (!ret) ? count : ret;
}

static ssize_t fd_show_configfs_dev_params(struct se_device *dev, char *b)
{
	struct fd_dev *fd_dev = FD_DEV(dev);
	ssize_t bl = 0;

	bl = sprintf(b + bl, "TCM FILEIO ID: %u", fd_dev->fd_dev_id);
	bl += sprintf(b + bl, "        File: %s  Size: %llu  Mode: %s\n",
		fd_dev->fd_dev_name, fd_dev->fd_dev_size,
		(fd_dev->fbd_flags & FDBD_HAS_BUFFERED_IO_WCE) ?
		"Buffered-WCE" : "O_DSYNC");
	return bl;
}

static sector_t fd_get_blocks(struct se_device *dev)
{
	struct fd_dev *fd_dev = FD_DEV(dev);
	struct file *f = fd_dev->fd_file;
	struct inode *i = f->f_mapping->host;
	unsigned long long dev_size;
	/*
	 * When using a file that references an underlying struct block_device,
	 * ensure dev_size is always based on the current inode size in order
	 * to handle underlying block_device resize operations.
	 */
	if (S_ISBLK(i->i_mode))
		dev_size = i_size_read(i);
	else
		dev_size = fd_dev->fd_dev_size;

	return div_u64(dev_size - dev->dev_attrib.block_size,
		       dev->dev_attrib.block_size);
}

static int fd_init_prot(struct se_device *dev)
{
	struct fd_dev *fd_dev = FD_DEV(dev);
	struct file *prot_file, *file = fd_dev->fd_file;
	struct inode *inode;
	int ret, flags = O_RDWR | O_CREAT | O_LARGEFILE | O_DSYNC;
	char buf[FD_MAX_DEV_PROT_NAME];

	if (!file) {
		pr_err("Unable to locate fd_dev->fd_file\n");
		return -ENODEV;
	}

	inode = file->f_mapping->host;
	if (S_ISBLK(inode->i_mode)) {
		pr_err("FILEIO Protection emulation only supported on"
		       " !S_ISBLK\n");
		return -ENOSYS;
	}

	if (fd_dev->fbd_flags & FDBD_HAS_BUFFERED_IO_WCE)
		flags &= ~O_DSYNC;

	snprintf(buf, FD_MAX_DEV_PROT_NAME, "%s.protection",
		 fd_dev->fd_dev_name);

	prot_file = filp_open(buf, flags, 0600);
	if (IS_ERR(prot_file)) {
		pr_err("filp_open(%s) failed\n", buf);
		ret = PTR_ERR(prot_file);
		return ret;
	}
	fd_dev->fd_prot_file = prot_file;

	return 0;
}

static int fd_format_prot(struct se_device *dev)
{
	struct fd_dev *fd_dev = FD_DEV(dev);
	struct file *prot_fd = fd_dev->fd_prot_file;
	sector_t prot_length, prot;
	unsigned char *buf;
	loff_t pos = 0;
	int unit_size = FDBD_FORMAT_UNIT_SIZE * dev->dev_attrib.block_size;
	int rc, ret = 0, size, len;

	if (!dev->dev_attrib.pi_prot_type) {
		pr_err("Unable to format_prot while pi_prot_type == 0\n");
		return -ENODEV;
	}
	if (!prot_fd) {
		pr_err("Unable to locate fd_dev->fd_prot_file\n");
		return -ENODEV;
	}

	buf = vzalloc(unit_size);
	if (!buf) {
		pr_err("Unable to allocate FILEIO prot buf\n");
		return -ENOMEM;
	}
	prot_length = (dev->transport->get_blocks(dev) + 1) * dev->prot_length;
	size = prot_length;

	pr_debug("Using FILEIO prot_length: %llu\n",
		 (unsigned long long)prot_length);

	memset(buf, 0xff, unit_size);
	for (prot = 0; prot < prot_length; prot += unit_size) {
		len = min(unit_size, size);
		rc = kernel_write(prot_fd, buf, len, pos);
		if (rc != len) {
			pr_err("vfs_write to prot file failed: %d\n", rc);
			ret = -ENODEV;
			goto out;
		}
		pos += len;
		size -= len;
	}

out:
	vfree(buf);
	return ret;
}

static void fd_free_prot(struct se_device *dev)
{
	struct fd_dev *fd_dev = FD_DEV(dev);

	if (!fd_dev->fd_prot_file)
		return;

	filp_close(fd_dev->fd_prot_file, NULL);
	fd_dev->fd_prot_file = NULL;
}

static struct sbc_ops fd_sbc_ops = {
	.execute_rw		= fd_execute_rw,
	.execute_sync_cache	= fd_execute_sync_cache,
	.execute_write_same	= fd_execute_write_same,
	.execute_write_same_unmap = fd_execute_write_same_unmap,
	.execute_unmap		= fd_execute_unmap,
};

static sense_reason_t
fd_parse_cdb(struct se_cmd *cmd)
{
	return sbc_parse_cdb(cmd, &fd_sbc_ops);
}

DEF_TB_DEFAULT_ATTRIBS(fileio);

static struct configfs_attribute *fileio_backend_dev_attrs[] = {
	&fileio_dev_attrib_emulate_model_alias.attr,
	&fileio_dev_attrib_emulate_dpo.attr,
	&fileio_dev_attrib_emulate_fua_write.attr,
	&fileio_dev_attrib_emulate_fua_read.attr,
	&fileio_dev_attrib_emulate_write_cache.attr,
	&fileio_dev_attrib_emulate_ua_intlck_ctrl.attr,
	&fileio_dev_attrib_emulate_tas.attr,
	&fileio_dev_attrib_emulate_tpu.attr,
	&fileio_dev_attrib_emulate_tpws.attr,
	&fileio_dev_attrib_emulate_caw.attr,
	&fileio_dev_attrib_emulate_3pc.attr,
	&fileio_dev_attrib_pi_prot_type.attr,
	&fileio_dev_attrib_hw_pi_prot_type.attr,
	&fileio_dev_attrib_pi_prot_format.attr,
	&fileio_dev_attrib_enforce_pr_isids.attr,
	&fileio_dev_attrib_is_nonrot.attr,
	&fileio_dev_attrib_emulate_rest_reord.attr,
	&fileio_dev_attrib_force_pr_aptpl.attr,
	&fileio_dev_attrib_hw_block_size.attr,
	&fileio_dev_attrib_block_size.attr,
	&fileio_dev_attrib_hw_max_sectors.attr,
	&fileio_dev_attrib_optimal_sectors.attr,
	&fileio_dev_attrib_hw_queue_depth.attr,
	&fileio_dev_attrib_queue_depth.attr,
	&fileio_dev_attrib_max_unmap_lba_count.attr,
	&fileio_dev_attrib_max_unmap_block_desc_count.attr,
	&fileio_dev_attrib_unmap_granularity.attr,
	&fileio_dev_attrib_unmap_granularity_alignment.attr,
	&fileio_dev_attrib_max_write_same_len.attr,
	NULL,
};

static struct se_subsystem_api fileio_template = {
	.name			= "fileio",
	.inquiry_prod		= "FILEIO",
	.inquiry_rev		= FD_VERSION,
	.owner			= THIS_MODULE,
	.transport_type		= TRANSPORT_PLUGIN_VHBA_PDEV,
	.attach_hba		= fd_attach_hba,
	.detach_hba		= fd_detach_hba,
	.alloc_device		= fd_alloc_device,
	.configure_device	= fd_configure_device,
	.free_device		= fd_free_device,
	.parse_cdb		= fd_parse_cdb,
	.set_configfs_dev_params = fd_set_configfs_dev_params,
	.show_configfs_dev_params = fd_show_configfs_dev_params,
	.get_device_type	= sbc_get_device_type,
	.get_blocks		= fd_get_blocks,
	.init_prot		= fd_init_prot,
	.format_prot		= fd_format_prot,
	.free_prot		= fd_free_prot,
};

static int __init fileio_module_init(void)
{
	struct target_backend_cits *tbc = &fileio_template.tb_cits;

	target_core_setup_sub_cits(&fileio_template);
	tbc->tb_dev_attrib_cit.ct_attrs = fileio_backend_dev_attrs;

	return transport_subsystem_register(&fileio_template);
}

static void __exit fileio_module_exit(void)
{
	transport_subsystem_release(&fileio_template);
}

MODULE_DESCRIPTION("TCM FILEIO subsystem plugin");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(fileio_module_init);
module_exit(fileio_module_exit);
