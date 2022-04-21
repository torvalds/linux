/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_DISK_H
#define _SCSI_DISK_H

/*
 * More than enough for everybody ;)  The huge number of majors
 * is a leftover from 16bit dev_t days, we don't really need that
 * much numberspace.
 */
#define SD_MAJORS	16

/*
 * Time out in seconds for disks and Magneto-opticals (which are slower).
 */
#define SD_TIMEOUT		(30 * HZ)
#define SD_MOD_TIMEOUT		(75 * HZ)
/*
 * Flush timeout is a multiplier over the standard device timeout which is
 * user modifiable via sysfs but initially set to SD_TIMEOUT
 */
#define SD_FLUSH_TIMEOUT_MULTIPLIER	2
#define SD_WRITE_SAME_TIMEOUT	(120 * HZ)

/*
 * Number of allowed retries
 */
#define SD_MAX_RETRIES		5
#define SD_PASSTHROUGH_RETRIES	1
#define SD_MAX_MEDIUM_TIMEOUTS	2

/*
 * Size of the initial data buffer for mode and read capacity data
 */
#define SD_BUF_SIZE		512

/*
 * Number of sectors at the end of the device to avoid multi-sector
 * accesses to in the case of last_sector_bug
 */
#define SD_LAST_BUGGY_SECTORS	8

enum {
	SD_EXT_CDB_SIZE = 32,	/* Extended CDB size */
	SD_MEMPOOL_SIZE = 2,	/* CDB pool size */
};

enum {
	SD_DEF_XFER_BLOCKS = 0xffff,
	SD_MAX_XFER_BLOCKS = 0xffffffff,
	SD_MAX_WS10_BLOCKS = 0xffff,
	SD_MAX_WS16_BLOCKS = 0x7fffff,
};

enum {
	SD_LBP_FULL = 0,	/* Full logical block provisioning */
	SD_LBP_UNMAP,		/* Use UNMAP command */
	SD_LBP_WS16,		/* Use WRITE SAME(16) with UNMAP bit */
	SD_LBP_WS10,		/* Use WRITE SAME(10) with UNMAP bit */
	SD_LBP_ZERO,		/* Use WRITE SAME(10) with zero payload */
	SD_LBP_DISABLE,		/* Discard disabled due to failed cmd */
};

enum {
	SD_ZERO_WRITE = 0,	/* Use WRITE(10/16) command */
	SD_ZERO_WS,		/* Use WRITE SAME(10/16) command */
	SD_ZERO_WS16_UNMAP,	/* Use WRITE SAME(16) with UNMAP */
	SD_ZERO_WS10_UNMAP,	/* Use WRITE SAME(10) with UNMAP */
};

struct scsi_disk {
	struct scsi_device *device;

	/*
	 * disk_dev is used to show attributes in /sys/class/scsi_disk/,
	 * but otherwise not really needed.  Do not use for refcounting.
	 */
	struct device	disk_dev;
	struct gendisk	*disk;
	struct opal_dev *opal_dev;
#ifdef CONFIG_BLK_DEV_ZONED
	u32		nr_zones;
	u32		rev_nr_zones;
	u32		zone_blocks;
	u32		rev_zone_blocks;
	u32		zones_optimal_open;
	u32		zones_optimal_nonseq;
	u32		zones_max_open;
	u32		*zones_wp_offset;
	spinlock_t	zones_wp_offset_lock;
	u32		*rev_wp_offset;
	struct mutex	rev_mutex;
	struct work_struct zone_wp_offset_work;
	char		*zone_wp_update_buf;
#endif
	atomic_t	openers;
	sector_t	capacity;	/* size in logical blocks */
	int		max_retries;
	u32		max_xfer_blocks;
	u32		opt_xfer_blocks;
	u32		max_ws_blocks;
	u32		max_unmap_blocks;
	u32		unmap_granularity;
	u32		unmap_alignment;
	u32		index;
	unsigned int	physical_block_size;
	unsigned int	max_medium_access_timeouts;
	unsigned int	medium_access_timed_out;
	u8		media_present;
	u8		write_prot;
	u8		protection_type;/* Data Integrity Field */
	u8		provisioning_mode;
	u8		zeroing_mode;
	u8		nr_actuators;		/* Number of actuators */
	unsigned	ATO : 1;	/* state of disk ATO bit */
	unsigned	cache_override : 1; /* temp override of WCE,RCD */
	unsigned	WCE : 1;	/* state of disk WCE bit */
	unsigned	RCD : 1;	/* state of disk RCD bit, unused */
	unsigned	DPOFUA : 1;	/* state of disk DPOFUA bit */
	unsigned	first_scan : 1;
	unsigned	lbpme : 1;
	unsigned	lbprz : 1;
	unsigned	lbpu : 1;
	unsigned	lbpws : 1;
	unsigned	lbpws10 : 1;
	unsigned	lbpvpd : 1;
	unsigned	ws10 : 1;
	unsigned	ws16 : 1;
	unsigned	rc_basis: 2;
	unsigned	zoned: 2;
	unsigned	urswrz : 1;
	unsigned	security : 1;
	unsigned	ignore_medium_access_errors : 1;
};
#define to_scsi_disk(obj) container_of(obj, struct scsi_disk, disk_dev)

static inline struct scsi_disk *scsi_disk(struct gendisk *disk)
{
	return disk->private_data;
}

#define sd_printk(prefix, sdsk, fmt, a...)				\
        (sdsk)->disk ?							\
	      sdev_prefix_printk(prefix, (sdsk)->device,		\
				 (sdsk)->disk->disk_name, fmt, ##a) :	\
	      sdev_printk(prefix, (sdsk)->device, fmt, ##a)

#define sd_first_printk(prefix, sdsk, fmt, a...)			\
	do {								\
		if ((sdsk)->first_scan)					\
			sd_printk(prefix, sdsk, fmt, ##a);		\
	} while (0)

static inline int scsi_medium_access_command(struct scsi_cmnd *scmd)
{
	switch (scmd->cmnd[0]) {
	case READ_6:
	case READ_10:
	case READ_12:
	case READ_16:
	case SYNCHRONIZE_CACHE:
	case VERIFY:
	case VERIFY_12:
	case VERIFY_16:
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
	case WRITE_SAME:
	case WRITE_SAME_16:
	case UNMAP:
		return 1;
	case VARIABLE_LENGTH_CMD:
		switch (scmd->cmnd[9]) {
		case READ_32:
		case VERIFY_32:
		case WRITE_32:
		case WRITE_SAME_32:
			return 1;
		}
	}

	return 0;
}

static inline sector_t logical_to_sectors(struct scsi_device *sdev, sector_t blocks)
{
	return blocks << (ilog2(sdev->sector_size) - 9);
}

static inline unsigned int logical_to_bytes(struct scsi_device *sdev, sector_t blocks)
{
	return blocks * sdev->sector_size;
}

static inline sector_t bytes_to_logical(struct scsi_device *sdev, unsigned int bytes)
{
	return bytes >> ilog2(sdev->sector_size);
}

static inline sector_t sectors_to_logical(struct scsi_device *sdev, sector_t sector)
{
	return sector >> (ilog2(sdev->sector_size) - 9);
}

#ifdef CONFIG_BLK_DEV_INTEGRITY

extern void sd_dif_config_host(struct scsi_disk *);

#else /* CONFIG_BLK_DEV_INTEGRITY */

static inline void sd_dif_config_host(struct scsi_disk *disk)
{
}

#endif /* CONFIG_BLK_DEV_INTEGRITY */

static inline int sd_is_zoned(struct scsi_disk *sdkp)
{
	return sdkp->zoned == 1 || sdkp->device->type == TYPE_ZBC;
}

#ifdef CONFIG_BLK_DEV_ZONED

void sd_zbc_release_disk(struct scsi_disk *sdkp);
int sd_zbc_read_zones(struct scsi_disk *sdkp, unsigned char *buffer);
int sd_zbc_revalidate_zones(struct scsi_disk *sdkp);
blk_status_t sd_zbc_setup_zone_mgmt_cmnd(struct scsi_cmnd *cmd,
					 unsigned char op, bool all);
unsigned int sd_zbc_complete(struct scsi_cmnd *cmd, unsigned int good_bytes,
			     struct scsi_sense_hdr *sshdr);
int sd_zbc_report_zones(struct gendisk *disk, sector_t sector,
		unsigned int nr_zones, report_zones_cb cb, void *data);

blk_status_t sd_zbc_prepare_zone_append(struct scsi_cmnd *cmd, sector_t *lba,
				        unsigned int nr_blocks);

#else /* CONFIG_BLK_DEV_ZONED */

static inline void sd_zbc_release_disk(struct scsi_disk *sdkp) {}

static inline int sd_zbc_read_zones(struct scsi_disk *sdkp,
				    unsigned char *buf)
{
	return 0;
}

static inline int sd_zbc_revalidate_zones(struct scsi_disk *sdkp)
{
	return 0;
}

static inline blk_status_t sd_zbc_setup_zone_mgmt_cmnd(struct scsi_cmnd *cmd,
						       unsigned char op,
						       bool all)
{
	return BLK_STS_TARGET;
}

static inline unsigned int sd_zbc_complete(struct scsi_cmnd *cmd,
			unsigned int good_bytes, struct scsi_sense_hdr *sshdr)
{
	return good_bytes;
}

static inline blk_status_t sd_zbc_prepare_zone_append(struct scsi_cmnd *cmd,
						      sector_t *lba,
						      unsigned int nr_blocks)
{
	return BLK_STS_TARGET;
}

#define sd_zbc_report_zones NULL

#endif /* CONFIG_BLK_DEV_ZONED */

void sd_print_sense_hdr(struct scsi_disk *sdkp, struct scsi_sense_hdr *sshdr);
void sd_print_result(const struct scsi_disk *sdkp, const char *msg, int result);

#endif /* _SCSI_DISK_H */
