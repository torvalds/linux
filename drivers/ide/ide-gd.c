#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/mutex.h>
#include <linux/ide.h>
#include <linux/hdreg.h>

#if !defined(CONFIG_DEBUG_BLOCK_EXT_DEVT)
#define IDE_DISK_MINORS		(1 << PARTN_BITS)
#else
#define IDE_DISK_MINORS		0
#endif

#include "ide-disk.h"

#define IDE_GD_VERSION	"1.18"

static DEFINE_MUTEX(ide_disk_ref_mutex);

static void ide_disk_release(struct kref *);

static struct ide_disk_obj *ide_disk_get(struct gendisk *disk)
{
	struct ide_disk_obj *idkp = NULL;

	mutex_lock(&ide_disk_ref_mutex);
	idkp = ide_drv_g(disk, ide_disk_obj);
	if (idkp) {
		if (ide_device_get(idkp->drive))
			idkp = NULL;
		else
			kref_get(&idkp->kref);
	}
	mutex_unlock(&ide_disk_ref_mutex);
	return idkp;
}

static void ide_disk_put(struct ide_disk_obj *idkp)
{
	ide_drive_t *drive = idkp->drive;

	mutex_lock(&ide_disk_ref_mutex);
	kref_put(&idkp->kref, ide_disk_release);
	ide_device_put(drive);
	mutex_unlock(&ide_disk_ref_mutex);
}

sector_t ide_gd_capacity(ide_drive_t *drive)
{
	return drive->capacity64;
}

static int ide_gd_probe(ide_drive_t *);

static void ide_gd_remove(ide_drive_t *drive)
{
	struct ide_disk_obj *idkp = drive->driver_data;
	struct gendisk *g = idkp->disk;

	ide_proc_unregister_driver(drive, idkp->driver);

	del_gendisk(g);

	ide_disk_flush(drive);

	ide_disk_put(idkp);
}

static void ide_disk_release(struct kref *kref)
{
	struct ide_disk_obj *idkp = to_ide_drv(kref, ide_disk_obj);
	ide_drive_t *drive = idkp->drive;
	struct gendisk *g = idkp->disk;

	drive->driver_data = NULL;
	g->private_data = NULL;
	put_disk(g);
	kfree(idkp);
}

/*
 * On HPA drives the capacity needs to be
 * reinitilized on resume otherwise the disk
 * can not be used and a hard reset is required
 */
static void ide_gd_resume(ide_drive_t *drive)
{
	if (ata_id_hpa_enabled(drive->id))
		ide_disk_init_capacity(drive);
}

static void ide_gd_shutdown(ide_drive_t *drive)
{
#ifdef	CONFIG_ALPHA
	/* On Alpha, halt(8) doesn't actually turn the machine off,
	   it puts you into the sort of firmware monitor. Typically,
	   it's used to boot another kernel image, so it's not much
	   different from reboot(8). Therefore, we don't need to
	   spin down the disk in this case, especially since Alpha
	   firmware doesn't handle disks in standby mode properly.
	   On the other hand, it's reasonably safe to turn the power
	   off when the shutdown process reaches the firmware prompt,
	   as the firmware initialization takes rather long time -
	   at least 10 seconds, which should be sufficient for
	   the disk to expire its write cache. */
	if (system_state != SYSTEM_POWER_OFF) {
#else
	if (system_state == SYSTEM_RESTART) {
#endif
		ide_disk_flush(drive);
		return;
	}

	printk(KERN_INFO "Shutdown: %s\n", drive->name);

	drive->gendev.bus->suspend(&drive->gendev, PMSG_SUSPEND);
}

#ifdef CONFIG_IDE_PROC_FS
static ide_proc_entry_t *ide_disk_proc_entries(ide_drive_t *drive)
{
	return ide_disk_proc;
}

static const struct ide_proc_devset *ide_disk_proc_devsets(ide_drive_t *drive)
{
	return ide_disk_settings;
}
#endif

static ide_driver_t ide_gd_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-disk",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_gd_probe,
	.remove			= ide_gd_remove,
	.resume			= ide_gd_resume,
	.shutdown		= ide_gd_shutdown,
	.version		= IDE_GD_VERSION,
	.do_request		= ide_do_rw_disk,
	.end_request		= ide_end_request,
	.error			= __ide_error,
#ifdef CONFIG_IDE_PROC_FS
	.proc_entries		= ide_disk_proc_entries,
	.proc_devsets		= ide_disk_proc_devsets,
#endif
};

static int ide_gd_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_disk_obj *idkp;
	ide_drive_t *drive;

	idkp = ide_disk_get(disk);
	if (idkp == NULL)
		return -ENXIO;

	drive = idkp->drive;

	idkp->openers++;

	if ((drive->dev_flags & IDE_DFLAG_REMOVABLE) && idkp->openers == 1) {
		/*
		 * Ignore the return code from door_lock,
		 * since the open() has already succeeded,
		 * and the door_lock is irrelevant at this point.
		 */
		ide_disk_set_doorlock(drive, 1);
		drive->dev_flags |= IDE_DFLAG_MEDIA_CHANGED;
		check_disk_change(inode->i_bdev);
	}
	return 0;
}

static int ide_gd_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_disk_obj *idkp = ide_drv_g(disk, ide_disk_obj);
	ide_drive_t *drive = idkp->drive;

	if (idkp->openers == 1)
		ide_disk_flush(drive);

	if ((drive->dev_flags & IDE_DFLAG_REMOVABLE) && idkp->openers == 1)
		ide_disk_set_doorlock(drive, 0);

	idkp->openers--;

	ide_disk_put(idkp);

	return 0;
}

static int ide_gd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ide_disk_obj *idkp = ide_drv_g(bdev->bd_disk, ide_disk_obj);
	ide_drive_t *drive = idkp->drive;

	geo->heads = drive->bios_head;
	geo->sectors = drive->bios_sect;
	geo->cylinders = (u16)drive->bios_cyl; /* truncate */
	return 0;
}

static int ide_gd_media_changed(struct gendisk *disk)
{
	struct ide_disk_obj *idkp = ide_drv_g(disk, ide_disk_obj);
	ide_drive_t *drive = idkp->drive;
	int ret;

	/* do not scan partitions twice if this is a removable device */
	if (drive->dev_flags & IDE_DFLAG_ATTACH) {
		drive->dev_flags &= ~IDE_DFLAG_ATTACH;
		return 0;
	}

	ret = !!(drive->dev_flags & IDE_DFLAG_MEDIA_CHANGED);
	drive->dev_flags &= ~IDE_DFLAG_MEDIA_CHANGED;

	return ret;
}

static int ide_gd_revalidate_disk(struct gendisk *disk)
{
	struct ide_disk_obj *idkp = ide_drv_g(disk, ide_disk_obj);
	set_capacity(disk, ide_gd_capacity(idkp->drive));
	return 0;
}

static struct block_device_operations ide_gd_ops = {
	.owner			= THIS_MODULE,
	.open			= ide_gd_open,
	.release		= ide_gd_release,
	.ioctl			= ide_disk_ioctl,
	.getgeo			= ide_gd_getgeo,
	.media_changed		= ide_gd_media_changed,
	.revalidate_disk	= ide_gd_revalidate_disk
};

static int ide_gd_probe(ide_drive_t *drive)
{
	struct ide_disk_obj *idkp;
	struct gendisk *g;

	/* strstr("foo", "") is non-NULL */
	if (!strstr("ide-disk", drive->driver_req))
		goto failed;

	if (drive->media != ide_disk)
		goto failed;

	idkp = kzalloc(sizeof(*idkp), GFP_KERNEL);
	if (!idkp)
		goto failed;

	g = alloc_disk_node(IDE_DISK_MINORS, hwif_to_node(drive->hwif));
	if (!g)
		goto out_free_idkp;

	ide_init_disk(g, drive);

	kref_init(&idkp->kref);

	idkp->drive = drive;
	idkp->driver = &ide_gd_driver;
	idkp->disk = g;

	g->private_data = &idkp->driver;

	drive->driver_data = idkp;

	ide_disk_setup(drive);

	set_capacity(g, ide_gd_capacity(drive));

	g->minors = IDE_DISK_MINORS;
	g->driverfs_dev = &drive->gendev;
	g->flags |= GENHD_FL_EXT_DEVT;
	if (drive->dev_flags & IDE_DFLAG_REMOVABLE)
		g->flags = GENHD_FL_REMOVABLE;
	g->fops = &ide_gd_ops;
	add_disk(g);
	return 0;

out_free_idkp:
	kfree(idkp);
failed:
	return -ENODEV;
}

static int __init ide_gd_init(void)
{
	return driver_register(&ide_gd_driver.gen_driver);
}

static void __exit ide_gd_exit(void)
{
	driver_unregister(&ide_gd_driver.gen_driver);
}

MODULE_ALIAS("ide:*m-disk*");
MODULE_ALIAS("ide-disk");
module_init(ide_gd_init);
module_exit(ide_gd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ATA DISK Driver");
