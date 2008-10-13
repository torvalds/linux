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
ide_decl_devset(address);
ide_decl_devset(multcount);
ide_decl_devset(nowerr);
ide_decl_devset(wcache);
ide_decl_devset(acoustic);

/* ide-disk_ioctl.c */
int ide_disk_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

#endif /* __IDE_DISK_H */
