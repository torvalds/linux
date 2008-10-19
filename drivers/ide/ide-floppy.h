#ifndef __IDE_FLOPPY_H
#define __IDE_FLOPPY_H

/*
 * Most of our global data which we need to save even as we leave the driver
 * due to an interrupt or a timer event is stored in a variable of type
 * idefloppy_floppy_t, defined below.
 */
typedef struct ide_floppy_obj {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;
	unsigned int	openers;	/* protected by BKL for now */

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
} idefloppy_floppy_t;

/*
 * Pages of the SELECT SENSE / MODE SENSE packet commands.
 * See SFF-8070i spec.
 */
#define	IDEFLOPPY_CAPABILITIES_PAGE	0x1b
#define IDEFLOPPY_FLEXIBLE_DISK_PAGE	0x05

/* IOCTLs used in low-level formatting. */
#define	IDEFLOPPY_IOCTL_FORMAT_SUPPORTED	0x4600
#define	IDEFLOPPY_IOCTL_FORMAT_GET_CAPACITY	0x4601
#define	IDEFLOPPY_IOCTL_FORMAT_START		0x4602
#define IDEFLOPPY_IOCTL_FORMAT_GET_PROGRESS	0x4603

/* ide-floppy.c */
void ide_floppy_create_mode_sense_cmd(struct ide_atapi_pc *, u8);
void ide_floppy_create_read_capacity_cmd(struct ide_atapi_pc *);
sector_t ide_floppy_capacity(ide_drive_t *);

/* ide-floppy_ioctl.c */
int ide_floppy_ioctl(struct inode *, struct file *, unsigned, unsigned long);

#ifdef CONFIG_IDE_PROC_FS
/* ide-floppy_proc.c */
extern ide_proc_entry_t ide_floppy_proc[];
extern const struct ide_proc_devset ide_floppy_settings[];
#endif

#endif /*__IDE_FLOPPY_H */
