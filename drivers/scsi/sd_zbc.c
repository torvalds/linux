// SPDX-License-Identifier: GPL-2.0-only
/*
 * SCSI Zoned Block commands
 *
 * Copyright (C) 2014-2015 SUSE Linux GmbH
 * Written by: Hannes Reinecke <hare@suse.de>
 * Modified by: Damien Le Moal <damien.lemoal@hgst.com>
 * Modified by: Shaun Tancheff <shaun.tancheff@seagate.com>
 */

#include <linux/blkdev.h>
#include <linux/vmalloc.h>
#include <linux/sched/mm.h>
#include <linux/mutex.h>

#include <asm/unaligned.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include "sd.h"

static unsigned int sd_zbc_get_zone_wp_offset(struct blk_zone *zone)
{
	if (zone->type == ZBC_ZONE_TYPE_CONV)
		return 0;

	switch (zone->cond) {
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
	case BLK_ZONE_COND_CLOSED:
		return zone->wp - zone->start;
	case BLK_ZONE_COND_FULL:
		return zone->len;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_OFFLINE:
	case BLK_ZONE_COND_READONLY:
	default:
		/*
		 * Offline and read-only zones do not have a valid
		 * write pointer. Use 0 as for an empty zone.
		 */
		return 0;
	}
}

static int sd_zbc_parse_report(struct scsi_disk *sdkp, u8 *buf,
			       unsigned int idx, report_zones_cb cb, void *data)
{
	struct scsi_device *sdp = sdkp->device;
	struct blk_zone zone = { 0 };
	int ret;

	zone.type = buf[0] & 0x0f;
	zone.cond = (buf[1] >> 4) & 0xf;
	if (buf[1] & 0x01)
		zone.reset = 1;
	if (buf[1] & 0x02)
		zone.non_seq = 1;

	zone.len = logical_to_sectors(sdp, get_unaligned_be64(&buf[8]));
	zone.capacity = zone.len;
	zone.start = logical_to_sectors(sdp, get_unaligned_be64(&buf[16]));
	zone.wp = logical_to_sectors(sdp, get_unaligned_be64(&buf[24]));
	if (zone.type != ZBC_ZONE_TYPE_CONV &&
	    zone.cond == ZBC_ZONE_COND_FULL)
		zone.wp = zone.start + zone.len;

	ret = cb(&zone, idx, data);
	if (ret)
		return ret;

	if (sdkp->rev_wp_offset)
		sdkp->rev_wp_offset[idx] = sd_zbc_get_zone_wp_offset(&zone);

	return 0;
}

/**
 * sd_zbc_do_report_zones - Issue a REPORT ZONES scsi command.
 * @sdkp: The target disk
 * @buf: vmalloc-ed buffer to use for the reply
 * @buflen: the buffer size
 * @lba: Start LBA of the report
 * @partial: Do partial report
 *
 * For internal use during device validation.
 * Using partial=true can significantly speed up execution of a report zones
 * command because the disk does not have to count all possible report matching
 * zones and will only report the count of zones fitting in the command reply
 * buffer.
 */
static int sd_zbc_do_report_zones(struct scsi_disk *sdkp, unsigned char *buf,
				  unsigned int buflen, sector_t lba,
				  bool partial)
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
	if (partial)
		cmd[14] = ZBC_REPORT_ZONE_PARTIAL;

	result = scsi_execute_req(sdp, cmd, DMA_FROM_DEVICE,
				  buf, buflen, &sshdr,
				  timeout, SD_MAX_RETRIES, NULL);
	if (result) {
		sd_printk(KERN_ERR, sdkp,
			  "REPORT ZONES start lba %llu failed\n", lba);
		sd_print_result(sdkp, "REPORT ZONES", result);
		if (driver_byte(result) == DRIVER_SENSE &&
		    scsi_sense_valid(&sshdr))
			sd_print_sense_hdr(sdkp, &sshdr);
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
 * Allocate a buffer for report zones reply.
 * @sdkp: The target disk
 * @nr_zones: Maximum number of zones to report
 * @buflen: Size of the buffer allocated
 *
 * Try to allocate a reply buffer for the number of requested zones.
 * The size of the buffer allocated may be smaller than requested to
 * satify the device constraint (max_hw_sectors, max_segments, etc).
 *
 * Return the address of the allocated buffer and update @buflen with
 * the size of the allocated buffer.
 */
static void *sd_zbc_alloc_report_buffer(struct scsi_disk *sdkp,
					unsigned int nr_zones, size_t *buflen)
{
	struct request_queue *q = sdkp->disk->queue;
	size_t bufsize;
	void *buf;

	/*
	 * Report zone buffer size should be at most 64B times the number of
	 * zones requested plus the 64B reply header, but should be aligned
	 * to SECTOR_SIZE for ATA devices.
	 * Make sure that this size does not exceed the hardware capabilities.
	 * Furthermore, since the report zone command cannot be split, make
	 * sure that the allocated buffer can always be mapped by limiting the
	 * number of pages allocated to the HBA max segments limit.
	 */
	nr_zones = min(nr_zones, sdkp->nr_zones);
	bufsize = roundup((nr_zones + 1) * 64, SECTOR_SIZE);
	bufsize = min_t(size_t, bufsize,
			queue_max_hw_sectors(q) << SECTOR_SHIFT);
	bufsize = min_t(size_t, bufsize, queue_max_segments(q) << PAGE_SHIFT);

	while (bufsize >= SECTOR_SIZE) {
		buf = __vmalloc(bufsize,
				GFP_KERNEL | __GFP_ZERO | __GFP_NORETRY);
		if (buf) {
			*buflen = bufsize;
			return buf;
		}
		bufsize = rounddown(bufsize >> 1, SECTOR_SIZE);
	}

	return NULL;
}

/**
 * sd_zbc_zone_sectors - Get the device zone size in number of 512B sectors.
 * @sdkp: The target disk
 */
static inline sector_t sd_zbc_zone_sectors(struct scsi_disk *sdkp)
{
	return logical_to_sectors(sdkp->device, sdkp->zone_blocks);
}

int sd_zbc_report_zones(struct gendisk *disk, sector_t sector,
			unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct scsi_disk *sdkp = scsi_disk(disk);
	sector_t capacity = logical_to_sectors(sdkp->device, sdkp->capacity);
	unsigned int nr, i;
	unsigned char *buf;
	size_t offset, buflen = 0;
	int zone_idx = 0;
	int ret;

	if (!sd_is_zoned(sdkp))
		/* Not a zoned device */
		return -EOPNOTSUPP;

	if (!capacity)
		/* Device gone or invalid */
		return -ENODEV;

	buf = sd_zbc_alloc_report_buffer(sdkp, nr_zones, &buflen);
	if (!buf)
		return -ENOMEM;

	while (zone_idx < nr_zones && sector < capacity) {
		ret = sd_zbc_do_report_zones(sdkp, buf, buflen,
				sectors_to_logical(sdkp->device, sector), true);
		if (ret)
			goto out;

		offset = 0;
		nr = min(nr_zones, get_unaligned_be32(&buf[0]) / 64);
		if (!nr)
			break;

		for (i = 0; i < nr && zone_idx < nr_zones; i++) {
			offset += 64;
			ret = sd_zbc_parse_report(sdkp, buf + offset, zone_idx,
						  cb, data);
			if (ret)
				goto out;
			zone_idx++;
		}

		sector += sd_zbc_zone_sectors(sdkp) * i;
	}

	ret = zone_idx;
out:
	kvfree(buf);
	return ret;
}

static blk_status_t sd_zbc_cmnd_checks(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	sector_t sector = blk_rq_pos(rq);

	if (!sd_is_zoned(sdkp))
		/* Not a zoned device */
		return BLK_STS_IOERR;

	if (sdkp->device->changed)
		return BLK_STS_IOERR;

	if (sector & (sd_zbc_zone_sectors(sdkp) - 1))
		/* Unaligned request */
		return BLK_STS_IOERR;

	return BLK_STS_OK;
}

#define SD_ZBC_INVALID_WP_OFST	(~0u)
#define SD_ZBC_UPDATING_WP_OFST	(SD_ZBC_INVALID_WP_OFST - 1)

static int sd_zbc_update_wp_offset_cb(struct blk_zone *zone, unsigned int idx,
				    void *data)
{
	struct scsi_disk *sdkp = data;

	lockdep_assert_held(&sdkp->zones_wp_offset_lock);

	sdkp->zones_wp_offset[idx] = sd_zbc_get_zone_wp_offset(zone);

	return 0;
}

static void sd_zbc_update_wp_offset_workfn(struct work_struct *work)
{
	struct scsi_disk *sdkp;
	unsigned int zno;
	int ret;

	sdkp = container_of(work, struct scsi_disk, zone_wp_offset_work);

	spin_lock_bh(&sdkp->zones_wp_offset_lock);
	for (zno = 0; zno < sdkp->nr_zones; zno++) {
		if (sdkp->zones_wp_offset[zno] != SD_ZBC_UPDATING_WP_OFST)
			continue;

		spin_unlock_bh(&sdkp->zones_wp_offset_lock);
		ret = sd_zbc_do_report_zones(sdkp, sdkp->zone_wp_update_buf,
					     SD_BUF_SIZE,
					     zno * sdkp->zone_blocks, true);
		spin_lock_bh(&sdkp->zones_wp_offset_lock);
		if (!ret)
			sd_zbc_parse_report(sdkp, sdkp->zone_wp_update_buf + 64,
					    zno, sd_zbc_update_wp_offset_cb,
					    sdkp);
	}
	spin_unlock_bh(&sdkp->zones_wp_offset_lock);

	scsi_device_put(sdkp->device);
}

/**
 * sd_zbc_prepare_zone_append() - Prepare an emulated ZONE_APPEND command.
 * @cmd: the command to setup
 * @lba: the LBA to patch
 * @nr_blocks: the number of LBAs to be written
 *
 * Called from sd_setup_read_write_cmnd() for REQ_OP_ZONE_APPEND.
 * @sd_zbc_prepare_zone_append() handles the necessary zone wrote locking and
 * patching of the lba for an emulated ZONE_APPEND command.
 *
 * In case the cached write pointer offset is %SD_ZBC_INVALID_WP_OFST it will
 * schedule a REPORT ZONES command and return BLK_STS_IOERR.
 */
blk_status_t sd_zbc_prepare_zone_append(struct scsi_cmnd *cmd, sector_t *lba,
					unsigned int nr_blocks)
{
	struct request *rq = cmd->request;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	unsigned int wp_offset, zno = blk_rq_zone_no(rq);
	blk_status_t ret;

	ret = sd_zbc_cmnd_checks(cmd);
	if (ret != BLK_STS_OK)
		return ret;

	if (!blk_rq_zone_is_seq(rq))
		return BLK_STS_IOERR;

	/* Unlock of the write lock will happen in sd_zbc_complete() */
	if (!blk_req_zone_write_trylock(rq))
		return BLK_STS_ZONE_RESOURCE;

	spin_lock_bh(&sdkp->zones_wp_offset_lock);
	wp_offset = sdkp->zones_wp_offset[zno];
	switch (wp_offset) {
	case SD_ZBC_INVALID_WP_OFST:
		/*
		 * We are about to schedule work to update a zone write pointer
		 * offset, which will cause the zone append command to be
		 * requeued. So make sure that the scsi device does not go away
		 * while the work is being processed.
		 */
		if (scsi_device_get(sdkp->device)) {
			ret = BLK_STS_IOERR;
			break;
		}
		sdkp->zones_wp_offset[zno] = SD_ZBC_UPDATING_WP_OFST;
		schedule_work(&sdkp->zone_wp_offset_work);
		fallthrough;
	case SD_ZBC_UPDATING_WP_OFST:
		ret = BLK_STS_DEV_RESOURCE;
		break;
	default:
		wp_offset = sectors_to_logical(sdkp->device, wp_offset);
		if (wp_offset + nr_blocks > sdkp->zone_blocks) {
			ret = BLK_STS_IOERR;
			break;
		}

		*lba += wp_offset;
	}
	spin_unlock_bh(&sdkp->zones_wp_offset_lock);
	if (ret)
		blk_req_zone_write_unlock(rq);
	return ret;
}

/**
 * sd_zbc_setup_zone_mgmt_cmnd - Prepare a zone ZBC_OUT command. The operations
 *			can be RESET WRITE POINTER, OPEN, CLOSE or FINISH.
 * @cmd: the command to setup
 * @op: Operation to be performed
 * @all: All zones control
 *
 * Called from sd_init_command() for REQ_OP_ZONE_RESET, REQ_OP_ZONE_RESET_ALL,
 * REQ_OP_ZONE_OPEN, REQ_OP_ZONE_CLOSE or REQ_OP_ZONE_FINISH requests.
 */
blk_status_t sd_zbc_setup_zone_mgmt_cmnd(struct scsi_cmnd *cmd,
					 unsigned char op, bool all)
{
	struct request *rq = cmd->request;
	sector_t sector = blk_rq_pos(rq);
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	sector_t block = sectors_to_logical(sdkp->device, sector);
	blk_status_t ret;

	ret = sd_zbc_cmnd_checks(cmd);
	if (ret != BLK_STS_OK)
		return ret;

	cmd->cmd_len = 16;
	memset(cmd->cmnd, 0, cmd->cmd_len);
	cmd->cmnd[0] = ZBC_OUT;
	cmd->cmnd[1] = op;
	if (all)
		cmd->cmnd[14] = 0x1;
	else
		put_unaligned_be64(block, &cmd->cmnd[2]);

	rq->timeout = SD_TIMEOUT;
	cmd->sc_data_direction = DMA_NONE;
	cmd->transfersize = 0;
	cmd->allowed = 0;

	return BLK_STS_OK;
}

static bool sd_zbc_need_zone_wp_update(struct request *rq)
{
	switch (req_op(rq)) {
	case REQ_OP_ZONE_APPEND:
	case REQ_OP_ZONE_FINISH:
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_RESET_ALL:
		return true;
	case REQ_OP_WRITE:
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE_SAME:
		return blk_rq_zone_is_seq(rq);
	default:
		return false;
	}
}

/**
 * sd_zbc_zone_wp_update - Update cached zone write pointer upon cmd completion
 * @cmd: Completed command
 * @good_bytes: Command reply bytes
 *
 * Called from sd_zbc_complete() to handle the update of the cached zone write
 * pointer value in case an update is needed.
 */
static unsigned int sd_zbc_zone_wp_update(struct scsi_cmnd *cmd,
					  unsigned int good_bytes)
{
	int result = cmd->result;
	struct request *rq = cmd->request;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	unsigned int zno = blk_rq_zone_no(rq);
	enum req_opf op = req_op(rq);

	/*
	 * If we got an error for a command that needs updating the write
	 * pointer offset cache, we must mark the zone wp offset entry as
	 * invalid to force an update from disk the next time a zone append
	 * command is issued.
	 */
	spin_lock_bh(&sdkp->zones_wp_offset_lock);

	if (result && op != REQ_OP_ZONE_RESET_ALL) {
		if (op == REQ_OP_ZONE_APPEND) {
			/* Force complete completion (no retry) */
			good_bytes = 0;
			scsi_set_resid(cmd, blk_rq_bytes(rq));
		}

		/*
		 * Force an update of the zone write pointer offset on
		 * the next zone append access.
		 */
		if (sdkp->zones_wp_offset[zno] != SD_ZBC_UPDATING_WP_OFST)
			sdkp->zones_wp_offset[zno] = SD_ZBC_INVALID_WP_OFST;
		goto unlock_wp_offset;
	}

	switch (op) {
	case REQ_OP_ZONE_APPEND:
		rq->__sector += sdkp->zones_wp_offset[zno];
		fallthrough;
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE_SAME:
	case REQ_OP_WRITE:
		if (sdkp->zones_wp_offset[zno] < sd_zbc_zone_sectors(sdkp))
			sdkp->zones_wp_offset[zno] +=
						good_bytes >> SECTOR_SHIFT;
		break;
	case REQ_OP_ZONE_RESET:
		sdkp->zones_wp_offset[zno] = 0;
		break;
	case REQ_OP_ZONE_FINISH:
		sdkp->zones_wp_offset[zno] = sd_zbc_zone_sectors(sdkp);
		break;
	case REQ_OP_ZONE_RESET_ALL:
		memset(sdkp->zones_wp_offset, 0,
		       sdkp->nr_zones * sizeof(unsigned int));
		break;
	default:
		break;
	}

unlock_wp_offset:
	spin_unlock_bh(&sdkp->zones_wp_offset_lock);

	return good_bytes;
}

/**
 * sd_zbc_complete - ZBC command post processing.
 * @cmd: Completed command
 * @good_bytes: Command reply bytes
 * @sshdr: command sense header
 *
 * Called from sd_done() to handle zone commands errors and updates to the
 * device queue zone write pointer offset cahce.
 */
unsigned int sd_zbc_complete(struct scsi_cmnd *cmd, unsigned int good_bytes,
		     struct scsi_sense_hdr *sshdr)
{
	int result = cmd->result;
	struct request *rq = cmd->request;

	if (op_is_zone_mgmt(req_op(rq)) &&
	    result &&
	    sshdr->sense_key == ILLEGAL_REQUEST &&
	    sshdr->asc == 0x24) {
		/*
		 * INVALID FIELD IN CDB error: a zone management command was
		 * attempted on a conventional zone. Nothing to worry about,
		 * so be quiet about the error.
		 */
		rq->rq_flags |= RQF_QUIET;
	} else if (sd_zbc_need_zone_wp_update(rq))
		good_bytes = sd_zbc_zone_wp_update(cmd, good_bytes);

	if (req_op(rq) == REQ_OP_ZONE_APPEND)
		blk_req_zone_write_unlock(rq);

	return good_bytes;
}

/**
 * sd_zbc_check_zoned_characteristics - Check zoned block device characteristics
 * @sdkp: Target disk
 * @buf: Buffer where to store the VPD page data
 *
 * Read VPD page B6, get information and check that reads are unconstrained.
 */
static int sd_zbc_check_zoned_characteristics(struct scsi_disk *sdkp,
					      unsigned char *buf)
{

	if (scsi_get_vpd_page(sdkp->device, 0xb6, buf, 64)) {
		sd_printk(KERN_NOTICE, sdkp,
			  "Read zoned characteristics VPD page failed\n");
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

	/*
	 * Check for unconstrained reads: host-managed devices with
	 * constrained reads (drives failing read after write pointer)
	 * are not supported.
	 */
	if (!sdkp->urswrz) {
		if (sdkp->first_scan)
			sd_printk(KERN_NOTICE, sdkp,
			  "constrained reads devices are not supported\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * sd_zbc_check_capacity - Check the device capacity
 * @sdkp: Target disk
 * @buf: command buffer
 * @zblocks: zone size in number of blocks
 *
 * Get the device zone size and check that the device capacity as reported
 * by READ CAPACITY matches the max_lba value (plus one) of the report zones
 * command reply for devices with RC_BASIS == 0.
 *
 * Returns 0 upon success or an error code upon failure.
 */
static int sd_zbc_check_capacity(struct scsi_disk *sdkp, unsigned char *buf,
				 u32 *zblocks)
{
	u64 zone_blocks;
	sector_t max_lba;
	unsigned char *rec;
	int ret;

	/* Do a report zone to get max_lba and the size of the first zone */
	ret = sd_zbc_do_report_zones(sdkp, buf, SD_BUF_SIZE, 0, false);
	if (ret)
		return ret;

	if (sdkp->rc_basis == 0) {
		/* The max_lba field is the capacity of this device */
		max_lba = get_unaligned_be64(&buf[8]);
		if (sdkp->capacity != max_lba + 1) {
			if (sdkp->first_scan)
				sd_printk(KERN_WARNING, sdkp,
					"Changing capacity from %llu to max LBA+1 %llu\n",
					(unsigned long long)sdkp->capacity,
					(unsigned long long)max_lba + 1);
			sdkp->capacity = max_lba + 1;
		}
	}

	/* Get the size of the first reported zone */
	rec = buf + 64;
	zone_blocks = get_unaligned_be64(&rec[8]);
	if (logical_to_sectors(sdkp->device, zone_blocks) > UINT_MAX) {
		if (sdkp->first_scan)
			sd_printk(KERN_NOTICE, sdkp,
				  "Zone size too large\n");
		return -EFBIG;
	}

	*zblocks = zone_blocks;

	return 0;
}

static void sd_zbc_print_zones(struct scsi_disk *sdkp)
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

static int sd_zbc_init_disk(struct scsi_disk *sdkp)
{
	sdkp->zones_wp_offset = NULL;
	spin_lock_init(&sdkp->zones_wp_offset_lock);
	sdkp->rev_wp_offset = NULL;
	mutex_init(&sdkp->rev_mutex);
	INIT_WORK(&sdkp->zone_wp_offset_work, sd_zbc_update_wp_offset_workfn);
	sdkp->zone_wp_update_buf = kzalloc(SD_BUF_SIZE, GFP_KERNEL);
	if (!sdkp->zone_wp_update_buf)
		return -ENOMEM;

	return 0;
}

void sd_zbc_release_disk(struct scsi_disk *sdkp)
{
	kvfree(sdkp->zones_wp_offset);
	sdkp->zones_wp_offset = NULL;
	kfree(sdkp->zone_wp_update_buf);
	sdkp->zone_wp_update_buf = NULL;
}

static void sd_zbc_revalidate_zones_cb(struct gendisk *disk)
{
	struct scsi_disk *sdkp = scsi_disk(disk);

	swap(sdkp->zones_wp_offset, sdkp->rev_wp_offset);
}

int sd_zbc_revalidate_zones(struct scsi_disk *sdkp)
{
	struct gendisk *disk = sdkp->disk;
	struct request_queue *q = disk->queue;
	u32 zone_blocks = sdkp->rev_zone_blocks;
	unsigned int nr_zones = sdkp->rev_nr_zones;
	u32 max_append;
	int ret = 0;
	unsigned int flags;

	/*
	 * For all zoned disks, initialize zone append emulation data if not
	 * already done. This is necessary also for host-aware disks used as
	 * regular disks due to the presence of partitions as these partitions
	 * may be deleted and the disk zoned model changed back from
	 * BLK_ZONED_NONE to BLK_ZONED_HA.
	 */
	if (sd_is_zoned(sdkp) && !sdkp->zone_wp_update_buf) {
		ret = sd_zbc_init_disk(sdkp);
		if (ret)
			return ret;
	}

	/*
	 * There is nothing to do for regular disks, including host-aware disks
	 * that have partitions.
	 */
	if (!blk_queue_is_zoned(q))
		return 0;

	/*
	 * Make sure revalidate zones are serialized to ensure exclusive
	 * updates of the scsi disk data.
	 */
	mutex_lock(&sdkp->rev_mutex);

	if (sdkp->zone_blocks == zone_blocks &&
	    sdkp->nr_zones == nr_zones &&
	    disk->queue->nr_zones == nr_zones)
		goto unlock;

	flags = memalloc_noio_save();
	sdkp->zone_blocks = zone_blocks;
	sdkp->nr_zones = nr_zones;
	sdkp->rev_wp_offset = kvcalloc(nr_zones, sizeof(u32), GFP_KERNEL);
	if (!sdkp->rev_wp_offset) {
		ret = -ENOMEM;
		memalloc_noio_restore(flags);
		goto unlock;
	}

	ret = blk_revalidate_disk_zones(disk, sd_zbc_revalidate_zones_cb);

	memalloc_noio_restore(flags);
	kvfree(sdkp->rev_wp_offset);
	sdkp->rev_wp_offset = NULL;

	if (ret) {
		sdkp->zone_blocks = 0;
		sdkp->nr_zones = 0;
		sdkp->capacity = 0;
		goto unlock;
	}

	max_append = min_t(u32, logical_to_sectors(sdkp->device, zone_blocks),
			   q->limits.max_segments << (PAGE_SHIFT - 9));
	max_append = min_t(u32, max_append, queue_max_hw_sectors(q));

	blk_queue_max_zone_append_sectors(q, max_append);

	sd_zbc_print_zones(sdkp);

unlock:
	mutex_unlock(&sdkp->rev_mutex);

	return ret;
}

int sd_zbc_read_zones(struct scsi_disk *sdkp, unsigned char *buf)
{
	struct gendisk *disk = sdkp->disk;
	struct request_queue *q = disk->queue;
	unsigned int nr_zones;
	u32 zone_blocks = 0;
	int ret;

	if (!sd_is_zoned(sdkp))
		/*
		 * Device managed or normal SCSI disk,
		 * no special handling required
		 */
		return 0;

	/* Check zoned block device characteristics (unconstrained reads) */
	ret = sd_zbc_check_zoned_characteristics(sdkp, buf);
	if (ret)
		goto err;

	/* Check the device capacity reported by report zones */
	ret = sd_zbc_check_capacity(sdkp, buf, &zone_blocks);
	if (ret != 0)
		goto err;

	/* The drive satisfies the kernel restrictions: set it up */
	blk_queue_flag_set(QUEUE_FLAG_ZONE_RESETALL, q);
	blk_queue_required_elevator_features(q, ELEVATOR_F_ZBD_SEQ_WRITE);
	if (sdkp->zones_max_open == U32_MAX)
		blk_queue_max_open_zones(q, 0);
	else
		blk_queue_max_open_zones(q, sdkp->zones_max_open);
	blk_queue_max_active_zones(q, 0);
	nr_zones = round_up(sdkp->capacity, zone_blocks) >> ilog2(zone_blocks);

	/* READ16/WRITE16 is mandatory for ZBC disks */
	sdkp->device->use_16_for_rw = 1;
	sdkp->device->use_10_for_rw = 0;

	sdkp->rev_nr_zones = nr_zones;
	sdkp->rev_zone_blocks = zone_blocks;

	return 0;

err:
	sdkp->capacity = 0;

	return ret;
}
