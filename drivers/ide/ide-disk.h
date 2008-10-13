#ifndef __IDE_DISK_H
#define __IDE_DISK_H

struct ide_disk_obj {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;
	unsigned int	openers;	/* protected by BKL for now */
};

#define ide_disk_g(disk) \
	container_of((disk)->private_data, struct ide_disk_obj, driver)

/* ide-disk.c */
sector_t ide_disk_capacity(ide_drive_t *);
ide_decl_devset(address);
ide_decl_devset(multcount);
ide_decl_devset(nowerr);
ide_decl_devset(wcache);
ide_decl_devset(acoustic);

/* ide-disk_ioctl.c */
int ide_disk_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

#ifdef CONFIG_IDE_PROC_FS
/* ide-disk_proc.c */
extern ide_proc_entry_t ide_disk_proc[];
extern const struct ide_proc_devset ide_disk_settings[];
#endif

#endif /* __IDE_DISK_H */
