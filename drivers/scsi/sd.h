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
#define SD_FLUSH_TIMEOUT	(60 * HZ)

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
	SD_LBP_FULL = 0,	/* Full logical block provisioning */
	SD_LBP_UNMAP,		/* Use UNMAP command */
	SD_LBP_WS16,		/* Use WRITE SAME(16) with UNMAP bit */
	SD_LBP_WS10,		/* Use WRITE SAME(10) with UNMAP bit */
	SD_LBP_ZERO,		/* Use WRITE SAME(10) with zero payload */
	SD_LBP_DISABLE,		/* Discard disabled due to failed cmd */
};

struct scsi_disk {
	struct scsi_driver *driver;	/* always &sd_template */
	struct scsi_device *device;
	struct device	dev;
	struct gendisk	*disk;
	atomic_t	openers;
	sector_t	capacity;	/* size in 512-byte sectors */
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
	unsigned	ATO : 1;	/* state of disk ATO bit */
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
};
#define to_scsi_disk(obj) container_of(obj,struct scsi_disk,dev)

static inline struct scsi_disk *scsi_disk(struct gendisk *disk)
{
	return container_of(disk->private_data, struct scsi_disk, driver);
}

#define sd_printk(prefix, sdsk, fmt, a...)				\
        (sdsk)->disk ?							\
	sdev_printk(prefix, (sdsk)->device, "[%s] " fmt,		\
		    (sdsk)->disk->disk_name, ##a) :			\
	sdev_printk(prefix, (sdsk)->device, fmt, ##a)

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

/*
 * A DIF-capable target device can be formatted with different
 * protection schemes.  Currently 0 through 3 are defined:
 *
 * Type 0 is regular (unprotected) I/O
 *
 * Type 1 defines the contents of the guard and reference tags
 *
 * Type 2 defines the contents of the guard and reference tags and
 * uses 32-byte commands to seed the latter
 *
 * Type 3 defines the contents of the guard tag only
 */

enum sd_dif_target_protection_types {
	SD_DIF_TYPE0_PROTECTION = 0x0,
	SD_DIF_TYPE1_PROTECTION = 0x1,
	SD_DIF_TYPE2_PROTECTION = 0x2,
	SD_DIF_TYPE3_PROTECTION = 0x3,
};

/*
 * Data Integrity Field tuple.
 */
struct sd_dif_tuple {
       __be16 guard_tag;	/* Checksum */
       __be16 app_tag;		/* Opaque storage */
       __be32 ref_tag;		/* Target LBA or indirect LBA */
};

#ifdef CONFIG_BLK_DEV_INTEGRITY

extern void sd_dif_config_host(struct scsi_disk *);
extern void sd_dif_prepare(struct request *rq, sector_t, unsigned int);
extern void sd_dif_complete(struct scsi_cmnd *, unsigned int);

#else /* CONFIG_BLK_DEV_INTEGRITY */

static inline void sd_dif_config_host(struct scsi_disk *disk)
{
}
static inline int sd_dif_prepare(struct request *rq, sector_t s, unsigned int a)
{
	return 0;
}
static inline void sd_dif_complete(struct scsi_cmnd *cmd, unsigned int a)
{
}

#endif /* CONFIG_BLK_DEV_INTEGRITY */

#endif /* _SCSI_DISK_H */
