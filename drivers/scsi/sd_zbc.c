/*
 * SCSI Zoned Block commands
 *
 * Copyright (C) 2014-2015 SUSE Linux GmbH
 * Written by: Hannes Reinecke <hare@suse.de>
 * Modified by: Damien Le Moal <damien.lemoal@hgst.com>
 * Modified by: Shaun Tancheff <shaun.tancheff@seagate.com>
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

#include <asm/unaligned.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include "sd.h"

/**
 * sd_zbc_parse_report - Convert a zone descriptor to a struct blk_zone,
 * @sdkp: The disk the report originated from
 * @buf: Address of the report zone descriptor
 * @zone: the destination zone structure
 *
 * All LBA sized values are converted to 512B sectors unit.
 */
static void sd_zbc_parse_report(struct scsi_disk *sdkp, u8 *buf,
				struct blk_zone *zone)
{
	struct scsi_device *sdp = sdkp->device;

	memset(zone, 0, sizeof(struct blk_zone));

	zone->type = buf[0] & 0x0f;
	zone->cond = (buf[1] >> 4) & 0xf;
	if (buf[1] & 0x01)
		zone->reset = 1;
	if (buf[1] & 0x02)
		zone->non_seq = 1;

	zone->len = logical_to_sectors(sdp, get_unaligned_be64(&buf[8]));
	zone->start = logical_to_sectors(sdp, get_unaligned_be64(&buf[16]));
	zone->wp = logical_to_sectors(sdp, get_unaligned_be64(&buf[24]));
	if (zone->type != ZBC_ZONE_TYPE_CONV &&
	    zone->cond == ZBC_ZONE_COND_FULL)
		zone->wp = zone->start + zone->len;
}

/**
 * sd_zbc_report_zones - Issue a REPORT ZONES scsi command.
 * @sdkp: The target disk
 * @buf: Buffer to use for the reply
 * @buflen: the buffer size
 * @lba: Start LBA of the report
 *
 * For internal use during device validation.
 */
static int sd_zbc_report_zones(struct scsi_disk *sdkp, unsigned char *buf,
			       unsigned int buflen, sector_t lba)
{
	struct scsi_device *sdp = sdkp->device;
	const int timeout = sdp->request_queue->rq_timeout;
	struct scsi_sense_hdr sshdr;
	unsigned char cmd[16];
	unsigned int rep_len;
	int result;

	memset(cmd, 0, 16);
	cmd[0] = ZBC_IN;
	cmd[1] = ZI_REPORT_ZONES;
	put_unaligned_be64(lba, &cmd[2]);
	put_unaligned_be32(buflen, &cmd[10]);
	memset(buf, 0, buflen);

	result = scsi_execute_req(sdp, cmd, DMA_FROM_DEVICE,
				  buf, buflen, &sshdr,
				  timeout, SD_MAX_RETRIES, NULL);
	if (result) {
		sd_printk(KERN_ERR, sdkp,
			  "REPORT ZONES lba %llu failed with %d/%d\n",
			  (unsigned long long)lba,
			  host_byte(result), driver_byte(result));
		return -EIO;
	}

	rep_len = get_unaligned_be32(&buf[0]);
	if (rep_len < 64) {
		sd_printk(KERN_ERR, sdkp,
			  "REPORT ZONES report invalid length %u\n",
			  rep_len);
		return -EIO;
	}

	return 0;
}

/**
 * sd_zbc_setup_report_cmnd - Prepare a REPORT ZONES scsi command
 * @cmd: The command to setup
 *
 * Call in sd_init_command() for a REQ_OP_ZONE_REPORT request.
 */
int sd_zbc_setup_report_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	sector_t lba, sector = blk_rq_pos(rq);
	unsigned int nr_bytes = blk_rq_bytes(rq);
	int ret;

	WARN_ON(nr_bytes == 0);

	if (!sd_is_zoned(sdkp))
		/* Not a zoned device */
		return BLKPREP_KILL;

	ret = scsi_init_io(cmd);
	if (ret != BLKPREP_OK)
		return ret;

	cmd->cmd_len = 16;
	memset(cmd->cmnd, 0, cmd->cmd_len);
	cmd->cmnd[0] = ZBC_IN;
	cmd->cmnd[1] = ZI_REPORT_ZONES;
	lba = sectors_to_logical(sdkp->device, sector);
	put_unaligned_be64(lba, &cmd->cmnd[2]);
	put_unaligned_be32(nr_bytes, &cmd->cmnd[10]);
	/* Do partial report for speeding things up */
	cmd->cmnd[14] = ZBC_REPORT_ZONE_PARTIAL;

	cmd->sc_data_direction = DMA_FROM_DEVICE;
	cmd->sdb.length = nr_bytes;
	cmd->transfersize = sdkp->device->sector_size;
	cmd->allowed = 0;

	/*
	 * Report may return less bytes than requested. Make sure
	 * to report completion on the entire initial request.
	 */
	rq->__data_len = nr_bytes;

	return BLKPREP_OK;
}

/**
 * sd_zbc_report_zones_complete - Process a REPORT ZONES scsi command reply.
 * @scmd: The completed report zones command
 * @good_bytes: reply size in bytes
 *
 * Convert all reported zone descriptors to struct blk_zone. The conversion
 * is done in-place, directly in the request specified sg buffer.
 */
static void sd_zbc_report_zones_complete(struct scsi_cmnd *scmd,
					 unsigned int good_bytes)
{
	struct request *rq = scmd->request;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	struct sg_mapping_iter miter;
	struct blk_zone_report_hdr hdr;
	struct blk_zone zone;
	unsigned int offset, bytes = 0;
	unsigned long flags;
	u8 *buf;

	if (good_bytes < 64)
		return;

	memset(&hdr, 0, sizeof(struct blk_zone_report_hdr));

	sg_miter_start(&miter, scsi_sglist(scmd), scsi_sg_count(scmd),
		       SG_MITER_TO_SG | SG_MITER_ATOMIC);

	local_irq_save(flags);
	while (sg_miter_next(&miter) && bytes < good_bytes) {

		buf = miter.addr;
		offset = 0;

		if (bytes == 0) {
			/* Set the report header */
			hdr.nr_zones = min_t(unsigned int,
					 (good_bytes - 64) / 64,
					 get_unaligned_be32(&buf[0]) / 64);
			memcpy(buf, &hdr, sizeof(struct blk_zone_report_hdr));
			offset += 64;
			bytes += 64;
		}

		/* Parse zone descriptors */
		while (offset < miter.length && hdr.nr_zones) {
			WARN_ON(offset > miter.length);
			buf = miter.addr + offset;
			sd_zbc_parse_report(sdkp, buf, &zone);
			memcpy(buf, &zone, sizeof(struct blk_zone));
			offset += 64;
			bytes += 64;
			hdr.nr_zones--;
		}

		if (!hdr.nr_zones)
			break;

	}
	sg_miter_stop(&miter);
	local_irq_restore(flags);
}

/**
 * sd_zbc_zone_sectors - Get the device zone size in number of 512B sectors.
 * @sdkp: The target disk
 */
static inline sector_t sd_zbc_zone_sectors(struct scsi_disk *sdkp)
{
	return logical_to_sectors(sdkp->device, sdkp->zone_blocks);
}

/**
 * sd_zbc_setup_reset_cmnd - Prepare a RESET WRITE POINTER scsi command.
 * @cmd: the command to setup
 *
 * Called from sd_init_command() for a REQ_OP_ZONE_RESET request.
 */
int sd_zbc_setup_reset_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	sector_t sector = blk_rq_pos(rq);
	sector_t block = sectors_to_logical(sdkp->device, sector);

	if (!sd_is_zoned(sdkp))
		/* Not a zoned device */
		return BLKPREP_KILL;

	if (sdkp->device->changed)
		return BLKPREP_KILL;

	if (sector & (sd_zbc_zone_sectors(sdkp) - 1))
		/* Unaligned request */
		return BLKPREP_KILL;

	cmd->cmd_len = 16;
	memset(cmd->cmnd, 0, cmd->cmd_len);
	cmd->cmnd[0] = ZBC_OUT;
	cmd->cmnd[1] = ZO_RESET_WRITE_POINTER;
	put_unaligned_be64(block, &cmd->cmnd[2]);

	rq->timeout = SD_TIMEOUT;
	cmd->sc_data_direction = DMA_NONE;
	cmd->transfersize = 0;
	cmd->allowed = 0;

	return BLKPREP_OK;
}

/**
 * sd_zbc_complete - ZBC command post processing.
 * @cmd: Completed command
 * @good_bytes: Command reply bytes
 * @sshdr: command sense header
 *
 * Called from sd_done(). Process report zones reply and handle reset zone
 * and write commands errors.
 */
void sd_zbc_complete(struct scsi_cmnd *cmd, unsigned int good_bytes,
		     struct scsi_sense_hdr *sshdr)
{
	int result = cmd->result;
	struct request *rq = cmd->request;

	switch (req_op(rq)) {
	case REQ_OP_ZONE_RESET:

		if (result &&
		    sshdr->sense_key == ILLEGAL_REQUEST &&
		    sshdr->asc == 0x24)
			/*
			 * INVALID FIELD IN CDB error: reset of a conventional
			 * zone was attempted. Nothing to worry about, so be
			 * quiet about the error.
			 */
			rq->rq_flags |= RQF_QUIET;
		break;

	case REQ_OP_WRITE:
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE_SAME:

		if (result &&
		    sshdr->sense_key == ILLEGAL_REQUEST &&
		    sshdr->asc == 0x21)
			/*
			 * INVALID ADDRESS FOR WRITE error: It is unlikely that
			 * retrying write requests failed with any kind of
			 * alignement error will result in success. So don't.
			 */
			cmd->allowed = 0;
		break;

	case REQ_OP_ZONE_REPORT:

		if (!result)
			sd_zbc_report_zones_complete(cmd, good_bytes);
		break;

	}
}

/**
 * sd_zbc_read_zoned_characteristics - Read zoned block device characteristics
 * @sdkp: Target disk
 * @buf: Buffer where to store the VPD page data
 *
 * Read VPD page B6.
 */
static int sd_zbc_read_zoned_characteristics(struct scsi_disk *sdkp,
					     unsigned char *buf)
{

	if (scsi_get_vpd_page(sdkp->device, 0xb6, buf, 64)) {
		sd_printk(KERN_NOTICE, sdkp,
			  "Unconstrained-read check failed\n");
		return -ENODEV;
	}

	if (sdkp->device->type != TYPE_ZBC) {
		/* Host-aware */
		sdkp->urswrz = 1;
		sdkp->zones_optimal_open = get_unaligned_be32(&buf[8]);
		sdkp->zones_optimal_nonseq = get_unaligned_be32(&buf[12]);
		sdkp->zones_max_open = 0;
	} else {
		/* Host-managed */
		sdkp->urswrz = buf[4] & 1;
		sdkp->zones_optimal_open = 0;
		sdkp->zones_optimal_nonseq = 0;
		sdkp->zones_max_open = get_unaligned_be32(&buf[16]);
	}

	return 0;
}

/**
 * sd_zbc_check_capacity - Check reported capacity.
 * @sdkp: Target disk
 * @buf: Buffer to use for commands
 *
 * ZBC drive may report only the capacity of the first conventional zones at
 * LBA 0. This is indicated by the RC_BASIS field of the read capacity reply.
 * Check this here. If the disk reported only its conventional zones capacity,
 * get the total capacity by doing a report zones.
 */
static int sd_zbc_check_capacity(struct scsi_disk *sdkp, unsigned char *buf)
{
	sector_t lba;
	int ret;

	if (sdkp->rc_basis != 0)
		return 0;

	/* Do a report zone to get the maximum LBA to check capacity */
	ret = sd_zbc_report_zones(sdkp, buf, SD_BUF_SIZE, 0);
	if (ret)
		return ret;

	/* The max_lba field is the capacity of this device */
	lba = get_unaligned_be64(&buf[8]);
	if (lba + 1 == sdkp->capacity)
		return 0;

	if (sdkp->first_scan)
		sd_printk(KERN_WARNING, sdkp,
			  "Changing capacity from %llu to max LBA+1 %llu\n",
			  (unsigned long long)sdkp->capacity,
			  (unsigned long long)lba + 1);
	sdkp->capacity = lba + 1;

	return 0;
}

#define SD_ZBC_BUF_SIZE 131072U

/**
 * sd_zbc_check_zone_size - Check the device zone sizes
 * @sdkp: Target disk
 *
 * Check that all zones of the device are equal. The last zone can however
 * be smaller. The zone size must also be a power of two number of LBAs.
 */
static int sd_zbc_check_zone_size(struct scsi_disk *sdkp)
{
	u64 zone_blocks = 0;
	sector_t block = 0;
	unsigned char *buf;
	unsigned char *rec;
	unsigned int buf_len;
	unsigned int list_length;
	int ret;
	u8 same;

	sdkp->zone_blocks = 0;

	/* Get a buffer */
	buf = kmalloc(SD_ZBC_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Do a report zone to get the same field */
	ret = sd_zbc_report_zones(sdkp, buf, SD_ZBC_BUF_SIZE, 0);
	if (ret)
		goto out_free;

	same = buf[4] & 0x0f;
	if (same > 0) {
		rec = &buf[64];
		zone_blocks = get_unaligned_be64(&rec[8]);
		goto out;
	}

	/*
	 * Check the size of all zones: all zones must be of
	 * equal size, except the last zone which can be smaller
	 * than other zones.
	 */
	do {

		/* Parse REPORT ZONES header */
		list_length = get_unaligned_be32(&buf[0]) + 64;
		rec = buf + 64;
		buf_len = min(list_length, SD_ZBC_BUF_SIZE);

		/* Parse zone descriptors */
		while (rec < buf + buf_len) {
			zone_blocks = get_unaligned_be64(&rec[8]);
			if (sdkp->zone_blocks == 0) {
				sdkp->zone_blocks = zone_blocks;
			} else if (zone_blocks != sdkp->zone_blocks &&
				   (block + zone_blocks < sdkp->capacity
				    || zone_blocks > sdkp->zone_blocks)) {
				zone_blocks = 0;
				goto out;
			}
			block += zone_blocks;
			rec += 64;
		}

		if (block < sdkp->capacity) {
			ret = sd_zbc_report_zones(sdkp, buf,
						  SD_ZBC_BUF_SIZE, block);
			if (ret)
				goto out_free;
		}

	} while (block < sdkp->capacity);

	zone_blocks = sdkp->zone_blocks;

out:
	if (!zone_blocks) {
		if (sdkp->first_scan)
			sd_printk(KERN_NOTICE, sdkp,
				  "Devices with non constant zone "
				  "size are not supported\n");
		ret = -ENODEV;
	} else if (!is_power_of_2(zone_blocks)) {
		if (sdkp->first_scan)
			sd_printk(KERN_NOTICE, sdkp,
				  "Devices with non power of 2 zone "
				  "size are not supported\n");
		ret = -ENODEV;
	} else if (logical_to_sectors(sdkp->device, zone_blocks) > UINT_MAX) {
		if (sdkp->first_scan)
			sd_printk(KERN_NOTICE, sdkp,
				  "Zone size too large\n");
		ret = -ENODEV;
	} else {
		sdkp->zone_blocks = zone_blocks;
		sdkp->zone_shift = ilog2(zone_blocks);
	}

out_free:
	kfree(buf);

	return ret;
}

/**
 * sd_zbc_alloc_zone_bitmap - Allocate a zone bitmap (one bit per zone).
 * @sdkp: The disk of the bitmap
 */
static inline unsigned long *sd_zbc_alloc_zone_bitmap(struct scsi_disk *sdkp)
{
	struct request_queue *q = sdkp->disk->queue;

	return kzalloc_node(BITS_TO_LONGS(sdkp->nr_zones)
			    * sizeof(unsigned long),
			    GFP_KERNEL, q->node);
}

/**
 * sd_zbc_get_seq_zones - Parse report zones reply to identify sequential zones
 * @sdkp: disk used
 * @buf: report reply buffer
 * @buflen: length of @buf
 * @seq_zones_bitmap: bitmap of sequential zones to set
 *
 * Parse reported zone descriptors in @buf to identify sequential zones and
 * set the reported zone bit in @seq_zones_bitmap accordingly.
 * Since read-only and offline zones cannot be written, do not
 * mark them as sequential in the bitmap.
 * Return the LBA after the last zone reported.
 */
static sector_t sd_zbc_get_seq_zones(struct scsi_disk *sdkp, unsigned char *buf,
				     unsigned int buflen,
				     unsigned long *seq_zones_bitmap)
{
	sector_t lba, next_lba = sdkp->capacity;
	unsigned int buf_len, list_length;
	unsigned char *rec;
	u8 type, cond;

	list_length = get_unaligned_be32(&buf[0]) + 64;
	buf_len = min(list_length, buflen);
	rec = buf + 64;

	while (rec < buf + buf_len) {
		type = rec[0] & 0x0f;
		cond = (rec[1] >> 4) & 0xf;
		lba = get_unaligned_be64(&rec[16]);
		if (type != ZBC_ZONE_TYPE_CONV &&
		    cond != ZBC_ZONE_COND_READONLY &&
		    cond != ZBC_ZONE_COND_OFFLINE)
			set_bit(lba >> sdkp->zone_shift, seq_zones_bitmap);
		next_lba = lba + get_unaligned_be64(&rec[8]);
		rec += 64;
	}

	return next_lba;
}

/**
 * sd_zbc_setup_seq_zones_bitmap - Initialize the disk seq zone bitmap.
 * @sdkp: target disk
 *
 * Allocate a zone bitmap and initialize it by identifying sequential zones.
 */
static int sd_zbc_setup_seq_zones_bitmap(struct scsi_disk *sdkp)
{
	struct request_queue *q = sdkp->disk->queue;
	unsigned long *seq_zones_bitmap;
	sector_t lba = 0;
	unsigned char *buf;
	int ret = -ENOMEM;

	seq_zones_bitmap = sd_zbc_alloc_zone_bitmap(sdkp);
	if (!seq_zones_bitmap)
		return -ENOMEM;

	buf = kmalloc(SD_ZBC_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		goto out;

	while (lba < sdkp->capacity) {
		ret = sd_zbc_report_zones(sdkp, buf, SD_ZBC_BUF_SIZE, lba);
		if (ret)
			goto out;
		lba = sd_zbc_get_seq_zones(sdkp, buf, SD_ZBC_BUF_SIZE,
					   seq_zones_bitmap);
	}

	if (lba != sdkp->capacity) {
		/* Something went wrong */
		ret = -EIO;
	}

out:
	kfree(buf);
	if (ret) {
		kfree(seq_zones_bitmap);
		return ret;
	}

	q->seq_zones_bitmap = seq_zones_bitmap;

	return 0;
}

static void sd_zbc_cleanup(struct scsi_disk *sdkp)
{
	struct request_queue *q = sdkp->disk->queue;

	kfree(q->seq_zones_bitmap);
	q->seq_zones_bitmap = NULL;

	kfree(q->seq_zones_wlock);
	q->seq_zones_wlock = NULL;

	q->nr_zones = 0;
}

static int sd_zbc_setup(struct scsi_disk *sdkp)
{
	struct request_queue *q = sdkp->disk->queue;
	int ret;

	/* READ16/WRITE16 is mandatory for ZBC disks */
	sdkp->device->use_16_for_rw = 1;
	sdkp->device->use_10_for_rw = 0;

	/* chunk_sectors indicates the zone size */
	blk_queue_chunk_sectors(sdkp->disk->queue,
			logical_to_sectors(sdkp->device, sdkp->zone_blocks));
	sdkp->nr_zones =
		round_up(sdkp->capacity, sdkp->zone_blocks) >> sdkp->zone_shift;

	/*
	 * Initialize the device request queue information if the number
	 * of zones changed.
	 */
	if (sdkp->nr_zones != q->nr_zones) {

		sd_zbc_cleanup(sdkp);

		q->nr_zones = sdkp->nr_zones;
		if (sdkp->nr_zones) {
			q->seq_zones_wlock = sd_zbc_alloc_zone_bitmap(sdkp);
			if (!q->seq_zones_wlock) {
				ret = -ENOMEM;
				goto err;
			}

			ret = sd_zbc_setup_seq_zones_bitmap(sdkp);
			if (ret) {
				sd_zbc_cleanup(sdkp);
				goto err;
			}
		}

	}

	return 0;

err:
	sd_zbc_cleanup(sdkp);
	return ret;
}

int sd_zbc_read_zones(struct scsi_disk *sdkp, unsigned char *buf)
{
	int ret;

	if (!sd_is_zoned(sdkp))
		/*
		 * Device managed or normal SCSI disk,
		 * no special handling required
		 */
		return 0;

	/* Get zoned block device characteristics */
	ret = sd_zbc_read_zoned_characteristics(sdkp, buf);
	if (ret)
		goto err;

	/*
	 * Check for unconstrained reads: host-managed devices with
	 * constrained reads (drives failing read after write pointer)
	 * are not supported.
	 */
	if (!sdkp->urswrz) {
		if (sdkp->first_scan)
			sd_printk(KERN_NOTICE, sdkp,
			  "constrained reads devices are not supported\n");
		ret = -ENODEV;
		goto err;
	}

	/* Check capacity */
	ret = sd_zbc_check_capacity(sdkp, buf);
	if (ret)
		goto err;

	/*
	 * Check zone size: only devices with a constant zone size (except
	 * an eventual last runt zone) that is a power of 2 are supported.
	 */
	ret = sd_zbc_check_zone_size(sdkp);
	if (ret)
		goto err;

	/* The drive satisfies the kernel restrictions: set it up */
	ret = sd_zbc_setup(sdkp);
	if (ret)
		goto err;

	return 0;

err:
	sdkp->capacity = 0;
	sd_zbc_cleanup(sdkp);

	return ret;
}

void sd_zbc_remove(struct scsi_disk *sdkp)
{
	sd_zbc_cleanup(sdkp);
}

void sd_zbc_print_zones(struct scsi_disk *sdkp)
{
	if (!sd_is_zoned(sdkp) || !sdkp->capacity)
		return;

	if (sdkp->capacity & (sdkp->zone_blocks - 1))
		sd_printk(KERN_NOTICE, sdkp,
			  "%u zones of %u logical blocks + 1 runt zone\n",
			  sdkp->nr_zones - 1,
			  sdkp->zone_blocks);
	else
		sd_printk(KERN_NOTICE, sdkp,
			  "%u zones of %u logical blocks\n",
			  sdkp->nr_zones,
			  sdkp->zone_blocks);
}
