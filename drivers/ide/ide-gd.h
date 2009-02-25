#ifndef __IDE_GD_H
#define __IDE_GD_H

#define DRV_NAME "ide-gd"
#define PFX DRV_NAME ": "

/* define to see debug info */
#define IDE_GD_DEBUG_LOG	0

#if IDE_GD_DEBUG_LOG
#define ide_debug_log(lvl, fmt, args...) __ide_debug_log(lvl, fmt, args)
#else
#define ide_debug_log(lvl, fmt, args...) do {} while (0)
#endif

struct ide_disk_obj {
	ide_drive_t		*drive;
	struct ide_driver	*driver;
	struct gendisk		*disk;
	struct device		dev;
	unsigned int		openers;	/* protected by BKL for now */

	/* Last failed packet command */
	struct ide_atapi_pc *failed_pc;
	/* used for blk_{fs,pc}_request() requests */
	struct ide_atapi_pc queued_pc;

	/* Last error information */
	u8 sense_key, asc, ascq;

	int progress_indication;

	/* Device information */
	/* Current format */
	int blocks, block_size, bs_factor;
	/* Last format capacity descriptor */
	u8 cap_desc[8];
	/* Copy of the flexible disk page */
	u8 flexible_disk_page[32];
};

sector_t ide_gd_capacity(ide_drive_t *);

#endif /* __IDE_GD_H */
