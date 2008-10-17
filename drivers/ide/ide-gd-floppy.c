#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/mutex.h>
#include <linux/ide.h>
#include <linux/hdreg.h>

#include "ide-floppy.h"

#define IDEFLOPPY_VERSION "1.00"

/* module parameters */
static unsigned long debug_mask;
module_param(debug_mask, ulong, 0644);

static DEFINE_MUTEX(ide_disk_ref_mutex);

static void ide_disk_release(struct kref *);

static struct ide_floppy_obj *ide_disk_get(struct gendisk *disk)
{
	struct ide_floppy_obj *idkp = NULL;

	mutex_lock(&ide_disk_ref_mutex);
	idkp = ide_drv_g(disk, ide_floppy_obj);
	if (idkp) {
		if (ide_device_get(idkp->drive))
			idkp = NULL;
		else
			kref_get(&idkp->kref);
	}
	mutex_unlock(&ide_disk_ref_mutex);
	return idkp;
}

static void ide_disk_put(struct ide_floppy_obj *idkp)
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
	struct ide_floppy_obj *idkp = drive->driver_data;
	struct gendisk *g = idkp->disk;

	ide_proc_unregister_driver(drive, idkp->driver);

	del_gendisk(g);

	ide_disk_put(idkp);
}

static void ide_disk_release(struct kref *kref)
{
	struct ide_floppy_obj *idkp = to_ide_drv(kref, ide_floppy_obj);
	ide_drive_t *drive = idkp->drive;
	struct gendisk *g = idkp->disk;

	drive->driver_data = NULL;
	g->private_data = NULL;
	put_disk(g);
	kfree(idkp);
}

#ifdef CONFIG_IDE_PROC_FS
static ide_proc_entry_t *ide_floppy_proc_entries(ide_drive_t *drive)
{
	return ide_floppy_proc;
}

static const struct ide_proc_devset *ide_floppy_proc_devsets(ide_drive_t *drive)
{
	return ide_floppy_settings;
}
#endif

static ide_driver_t ide_gd_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-floppy",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_gd_probe,
	.remove			= ide_gd_remove,
	.version		= IDEFLOPPY_VERSION,
	.do_request		= ide_floppy_do_request,
	.end_request		= ide_floppy_end_request,
	.error			= __ide_error,
#ifdef CONFIG_IDE_PROC_FS
	.proc_entries		= ide_floppy_proc_entries,
	.proc_devsets		= ide_floppy_proc_devsets,
#endif
};

static int ide_gd_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_floppy_obj *idkp;
	ide_drive_t *drive;
	int ret = 0;

	idkp = ide_disk_get(disk);
	if (idkp == NULL)
		return -ENXIO;

	drive = idkp->drive;

	ide_debug_log(IDE_DBG_FUNC, "Call %s\n", __func__);

	idkp->openers++;

	if (idkp->openers == 1) {
		drive->dev_flags &= ~IDE_DFLAG_FORMAT_IN_PROGRESS;
		/* Just in case */

		if (ide_do_test_unit_ready(drive, disk))
			ide_do_start_stop(drive, disk, 1);

		ret = ide_floppy_get_capacity(drive);

		set_capacity(disk, ide_gd_capacity(drive));

		if (ret && (filp->f_flags & O_NDELAY) == 0) {
		    /*
		     * Allow O_NDELAY to open a drive without a disk, or with an
		     * unreadable disk, so that we can get the format capacity
		     * of the drive or begin the format - Sam
		     */
			ret = -EIO;
			goto out_put_idkp;
		}

		if ((drive->dev_flags & IDE_DFLAG_WP) && (filp->f_mode & 2)) {
			ret = -EROFS;
			goto out_put_idkp;
		}

		ide_set_media_lock(drive, disk, 1);
		drive->dev_flags |= IDE_DFLAG_MEDIA_CHANGED;
		check_disk_change(inode->i_bdev);
	} else if (drive->dev_flags & IDE_DFLAG_FORMAT_IN_PROGRESS) {
		ret = -EBUSY;
		goto out_put_idkp;
	}
	return 0;

out_put_idkp:
	idkp->openers--;
	ide_disk_put(idkp);
	return ret;
}

static int ide_gd_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_floppy_obj *idkp = ide_drv_g(disk, ide_floppy_obj);
	ide_drive_t *drive = idkp->drive;

	ide_debug_log(IDE_DBG_FUNC, "Call %s\n", __func__);

	if (idkp->openers == 1) {
		ide_set_media_lock(drive, disk, 0);
		drive->dev_flags &= ~IDE_DFLAG_FORMAT_IN_PROGRESS;
	}

	idkp->openers--;

	ide_disk_put(idkp);

	return 0;
}

static int ide_gd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ide_floppy_obj *idkp = ide_drv_g(bdev->bd_disk, ide_floppy_obj);
	ide_drive_t *drive = idkp->drive;

	geo->heads = drive->bios_head;
	geo->sectors = drive->bios_sect;
	geo->cylinders = (u16)drive->bios_cyl; /* truncate */
	return 0;
}

static int ide_gd_media_changed(struct gendisk *disk)
{
	struct ide_floppy_obj *idkp = ide_drv_g(disk, ide_floppy_obj);
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
	struct ide_floppy_obj *idkp = ide_drv_g(disk, ide_floppy_obj);
	set_capacity(disk, ide_gd_capacity(idkp->drive));
	return 0;
}

static struct block_device_operations ide_gd_ops = {
	.owner			= THIS_MODULE,
	.open			= ide_gd_open,
	.release		= ide_gd_release,
	.ioctl			= ide_floppy_ioctl,
	.getgeo			= ide_gd_getgeo,
	.media_changed		= ide_gd_media_changed,
	.revalidate_disk	= ide_gd_revalidate_disk
};

static int ide_gd_probe(ide_drive_t *drive)
{
	struct ide_floppy_obj *idkp;
	struct gendisk *g;

	if (!strstr("ide-floppy", drive->driver_req))
		goto failed;

	if (drive->media != ide_floppy)
		goto failed;

	if (!ide_check_atapi_device(drive, DRV_NAME)) {
		printk(KERN_ERR PFX "%s: not supported by this version of "
		       DRV_NAME "\n", drive->name);
		goto failed;
	}
	idkp = kzalloc(sizeof(*idkp), GFP_KERNEL);
	if (!idkp) {
		printk(KERN_ERR PFX "%s: Can't allocate a floppy structure\n",
		       drive->name);
		goto failed;
	}

	g = alloc_disk_node(1 << PARTN_BITS, hwif_to_node(drive->hwif));
	if (!g)
		goto out_free_idkp;

	ide_init_disk(g, drive);

	kref_init(&idkp->kref);

	idkp->drive = drive;
	idkp->driver = &ide_gd_driver;
	idkp->disk = g;

	g->private_data = &idkp->driver;

	drive->driver_data = idkp;

	drive->debug_mask = debug_mask;

	ide_floppy_setup(drive);

	set_capacity(g, ide_gd_capacity(drive));

	g->minors = 1 << PARTN_BITS;
	g->driverfs_dev = &drive->gendev;
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
	printk(KERN_INFO DRV_NAME " driver " IDEFLOPPY_VERSION "\n");
	return driver_register(&ide_gd_driver.gen_driver);
}

static void __exit ide_gd_exit(void)
{
	driver_unregister(&ide_gd_driver.gen_driver);
}

MODULE_ALIAS("ide:*m-floppy*");
MODULE_ALIAS("ide-floppy");
module_init(ide_gd_init);
module_exit(ide_gd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ATAPI FLOPPY Driver");
