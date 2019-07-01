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

/*
 * Maximum number of zones to get with one report zones command.
 */
#define SD_ZBC_REPORT_MAX_ZONES		8192U

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
	 * zones requested plus the 64B reply header, but should be at least
	 * SECTOR_SIZE for ATA devices.
	 * Make sure that this size does not exceed the hardware capabilities.
	 * Furthermore, since the report zone command cannot be split, make
	 * sure that the allocated buffer can always be mapped by limiting the
	 * number of pages allocated to the HBA max segments limit.
	 */
	nr_zones = min(nr_zones, SD_ZBC_REPORT_MAX_ZONES);
	bufsize = roundup((nr_zones + 1) * 64, 512);
	bufsize = min_t(size_t, bufsize,
			queue_max_hw_sectors(q) << SECTOR_SHIFT);
	bufsize = min_t(size_t, bufsize, queue_max_segments(q) << PAGE_SHIFT);

	buf = vzalloc(bufsize);
	if (buf)
		*buflen = bufsize;

	return buf;
}

/**
 * sd_zbc_report_zones - Disk report zones operation.
 * @disk: The target disk
 * @sector: Start 512B sector of the report
 * @zones: Array of zone descriptors
 * @nr_zones: Number of descriptors in the array
 *
 * Execute a report zones command on the target disk.
 */
int sd_zbc_report_zones(struct gendisk *disk, sector_t sector,
			struct blk_zone *zones, unsigned int *nr_zones)
{
	struct scsi_disk *sdkp = scsi_disk(disk);
	unsigned int i, nrz = *nr_zones;
	unsigned char *buf;
	size_t buflen = 0, offset = 0;
	int ret = 0;

	if (!sd_is_zoned(sdkp))
		/* Not a zoned device */
		return -EOPNOTSUPP;

	buf = sd_zbc_alloc_report_buffer(sdkp, nrz, &buflen);
	if (!buf)
		return -ENOMEM;

	ret = sd_zbc_do_report_zones(sdkp, buf, buflen,
			sectors_to_logical(sdkp->device, sector), true);
	if (ret)
		goto out;

	nrz = min(nrz, get_unaligned_be32(&buf[0]) / 64);
	for (i = 0; i < nrz; i++) {
		offset += 64;
		sd_zbc_parse_report(sdkp, buf + offset, zones);
		zones++;
	}

	*nr_zones = nrz;

out:
	kvfree(buf);

	return ret;
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
blk_status_t sd_zbc_setup_reset_cmnd(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;
	struct scsi_disk *sdkp = scsi_disk(rq->rq_disk);
	sector_t sector = blk_rq_pos(rq);
	sector_t block = sectors_to_logical(sdkp->device, sector);

	if (!sd_is_zoned(sdkp))
		/* Not a zoned device */
		return BLK_STS_IOERR;

	if (sdkp->device->changed)
		return BLK_STS_IOERR;

	if (sector & (sd_zbc_zone_sectors(sdkp) - 1))
		/* Unaligned request */
		return BLK_STS_IOERR;

	cmd->cmd_len = 16;
	memset(cmd->cmnd, 0, cmd->cmd_len);
	cmd->cmnd[0] = ZBC_OUT;
	cmd->cmnd[1] = ZO_RESET_WRITE_POINTER;
	put_unaligned_be64(block, &cmd->cmnd[2]);

	rq->timeout = SD_TIMEOUT;
	cmd->sc_data_direction = DMA_NONE;
	cmd->transfersize = 0;
	cmd->allowed = 0;

	return BLK_STS_OK;
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
		break;
	}
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
 * sd_zbc_check_zones - Check the device capacity and zone sizes
 * @sdkp: Target disk
 *
 * Check that the device capacity as reported by READ CAPACITY matches the
 * max_lba value (plus one)of the report zones command reply. Also check that
 * all zones of the device have an equal size, only allowing the last zone of
 * the disk to have a smaller size (runt zone). The zone size must also be a
 * power of two.
 *
 * Returns the zone size in number of blocks upon success or an error code
 * upon failure.
 */
static int sd_zbc_check_zones(struct scsi_disk *sdkp, u32 *zblocks)
{
	size_t bufsize, buflen;
	unsigned int noio_flag;
	u64 zone_blocks = 0;
	sector_t max_lba, block = 0;
	unsigned char *buf;
	unsigned char *rec;
	int ret;
	u8 same;

	/* Do all memory allocations as if GFP_NOIO was specified */
	noio_flag = memalloc_noio_save();

	/* Get a buffer */
	buf = sd_zbc_alloc_report_buffer(sdkp, SD_ZBC_REPORT_MAX_ZONES,
					 &bufsize);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	/* Do a report zone to get max_lba and the same field */
	ret = sd_zbc_do_report_zones(sdkp, buf, bufsize, 0, false);
	if (ret)
		goto out_free;

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

	/*
	 * Check same field: for any value other than 0, we know that all zones
	 * have the same size.
	 */
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
		buflen = min_t(size_t, get_unaligned_be32(&buf[0]) + 64,
			       bufsize);
		rec = buf + 64;

		/* Parse zone descriptors */
		while (rec < buf + buflen) {
			u64 this_zone_blocks = get_unaligned_be64(&rec[8]);

			if (zone_blocks == 0) {
				zone_blocks = this_zone_blocks;
			} else if (this_zone_blocks != zone_blocks &&
				   (block + this_zone_blocks < sdkp->capacity
				    || this_zone_blocks > zone_blocks)) {
				zone_blocks = 0;
				goto out;
			}
			block += this_zone_blocks;
			rec += 64;
		}

		if (block < sdkp->capacity) {
			ret = sd_zbc_do_report_zones(sdkp, buf, bufsize, block,
						     true);
			if (ret)
				goto out_free;
		}

	} while (block < sdkp->capacity);

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
		ret = -EFBIG;
	} else {
		*zblocks = zone_blocks;
		ret = 0;
	}

out_free:
	memalloc_noio_restore(noio_flag);
	kvfree(buf);

	return ret;
}

int sd_zbc_read_zones(struct scsi_disk *sdkp, unsigned char *buf)
{
	struct gendisk *disk = sdkp->disk;
	unsigned int nr_zones;
	u32 zone_blocks;
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

	/*
	 * Check zone size: only devices with a constant zone size (except
	 * an eventual last runt zone) that is a power of 2 are supported.
	 */
	ret = sd_zbc_check_zones(sdkp, &zone_blocks);
	if (ret != 0)
		goto err;

	/* The drive satisfies the kernel restrictions: set it up */
	blk_queue_chunk_sectors(sdkp->disk->queue,
			logical_to_sectors(sdkp->device, zone_blocks));
	nr_zones = round_up(sdkp->capacity, zone_blocks) >> ilog2(zone_blocks);

	/* READ16/WRITE16 is mandatory for ZBC disks */
	sdkp->device->use_16_for_rw = 1;
	sdkp->device->use_10_for_rw = 0;

	/*
	 * Revalidate the disk zone bitmaps once the block device capacity is
	 * set on the second revalidate execution during disk scan and if
	 * something changed when executing a normal revalidate.
	 */
	if (sdkp->first_scan) {
		sdkp->zone_blocks = zone_blocks;
		sdkp->nr_zones = nr_zones;
		return 0;
	}

	if (sdkp->zone_blocks != zone_blocks ||
	    sdkp->nr_zones != nr_zones ||
	    disk->queue->nr_zones != nr_zones) {
		ret = blk_revalidate_disk_zones(disk);
		if (ret != 0)
			goto err;
		sdkp->zone_blocks = zone_blocks;
		sdkp->nr_zones = nr_zones;
	}

	return 0;

err:
	sdkp->capacity = 0;

	return ret;
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
