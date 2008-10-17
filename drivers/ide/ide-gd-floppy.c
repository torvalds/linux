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

static DEFINE_MUTEX(idefloppy_ref_mutex);

static void idefloppy_cleanup_obj(struct kref *);

static struct ide_floppy_obj *ide_floppy_get(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = NULL;

	mutex_lock(&idefloppy_ref_mutex);
	floppy = ide_drv_g(disk, ide_floppy_obj);
	if (floppy) {
		if (ide_device_get(floppy->drive))
			floppy = NULL;
		else
			kref_get(&floppy->kref);
	}
	mutex_unlock(&idefloppy_ref_mutex);
	return floppy;
}

static void ide_floppy_put(struct ide_floppy_obj *floppy)
{
	ide_drive_t *drive = floppy->drive;

	mutex_lock(&idefloppy_ref_mutex);
	kref_put(&floppy->kref, idefloppy_cleanup_obj);
	ide_device_put(drive);
	mutex_unlock(&idefloppy_ref_mutex);
}

sector_t ide_floppy_capacity(ide_drive_t *drive)
{
	return drive->capacity64;
}

static void ide_floppy_remove(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct gendisk *g = floppy->disk;

	ide_proc_unregister_driver(drive, floppy->driver);

	del_gendisk(g);

	ide_floppy_put(floppy);
}

static void idefloppy_cleanup_obj(struct kref *kref)
{
	struct ide_floppy_obj *floppy = to_ide_drv(kref, ide_floppy_obj);
	ide_drive_t *drive = floppy->drive;
	struct gendisk *g = floppy->disk;

	drive->driver_data = NULL;
	g->private_data = NULL;
	put_disk(g);
	kfree(floppy);
}

static int ide_floppy_probe(ide_drive_t *);

static ide_driver_t idefloppy_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-floppy",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_floppy_probe,
	.remove			= ide_floppy_remove,
	.version		= IDEFLOPPY_VERSION,
	.do_request		= ide_floppy_do_request,
	.end_request		= ide_floppy_end_request,
	.error			= __ide_error,
#ifdef CONFIG_IDE_PROC_FS
	.proc			= ide_floppy_proc,
	.settings		= ide_floppy_settings,
#endif
};

static int idefloppy_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_floppy_obj *floppy;
	ide_drive_t *drive;
	int ret = 0;

	floppy = ide_floppy_get(disk);
	if (!floppy)
		return -ENXIO;

	drive = floppy->drive;

	ide_debug_log(IDE_DBG_FUNC, "Call %s\n", __func__);

	floppy->openers++;

	if (floppy->openers == 1) {
		drive->dev_flags &= ~IDE_DFLAG_FORMAT_IN_PROGRESS;
		/* Just in case */

		if (ide_do_test_unit_ready(drive, disk))
			ide_do_start_stop(drive, disk, 1);

		ret = ide_floppy_get_capacity(drive);

		set_capacity(disk, ide_floppy_capacity(drive));

		if (ret && (filp->f_flags & O_NDELAY) == 0) {
		    /*
		     * Allow O_NDELAY to open a drive without a disk, or with an
		     * unreadable disk, so that we can get the format capacity
		     * of the drive or begin the format - Sam
		     */
			ret = -EIO;
			goto out_put_floppy;
		}

		if ((drive->dev_flags & IDE_DFLAG_WP) && (filp->f_mode & 2)) {
			ret = -EROFS;
			goto out_put_floppy;
		}

		ide_set_media_lock(drive, disk, 1);
		drive->dev_flags |= IDE_DFLAG_MEDIA_CHANGED;
		check_disk_change(inode->i_bdev);
	} else if (drive->dev_flags & IDE_DFLAG_FORMAT_IN_PROGRESS) {
		ret = -EBUSY;
		goto out_put_floppy;
	}
	return 0;

out_put_floppy:
	floppy->openers--;
	ide_floppy_put(floppy);
	return ret;
}

static int idefloppy_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_floppy_obj *floppy = ide_drv_g(disk, ide_floppy_obj);
	ide_drive_t *drive = floppy->drive;

	ide_debug_log(IDE_DBG_FUNC, "Call %s\n", __func__);

	if (floppy->openers == 1) {
		ide_set_media_lock(drive, disk, 0);
		drive->dev_flags &= ~IDE_DFLAG_FORMAT_IN_PROGRESS;
	}

	floppy->openers--;

	ide_floppy_put(floppy);

	return 0;
}

static int idefloppy_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ide_floppy_obj *floppy = ide_drv_g(bdev->bd_disk,
						     ide_floppy_obj);
	ide_drive_t *drive = floppy->drive;

	geo->heads = drive->bios_head;
	geo->sectors = drive->bios_sect;
	geo->cylinders = (u16)drive->bios_cyl; /* truncate */
	return 0;
}

static int idefloppy_media_changed(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = ide_drv_g(disk, ide_floppy_obj);
	ide_drive_t *drive = floppy->drive;
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

static int idefloppy_revalidate_disk(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = ide_drv_g(disk, ide_floppy_obj);
	set_capacity(disk, ide_floppy_capacity(floppy->drive));
	return 0;
}

static struct block_device_operations idefloppy_ops = {
	.owner			= THIS_MODULE,
	.open			= idefloppy_open,
	.release		= idefloppy_release,
	.ioctl			= ide_floppy_ioctl,
	.getgeo			= idefloppy_getgeo,
	.media_changed		= idefloppy_media_changed,
	.revalidate_disk	= idefloppy_revalidate_disk
};

static int ide_floppy_probe(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy;
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
	floppy = kzalloc(sizeof(idefloppy_floppy_t), GFP_KERNEL);
	if (!floppy) {
		printk(KERN_ERR PFX "%s: Can't allocate a floppy structure\n",
		       drive->name);
		goto failed;
	}

	g = alloc_disk_node(1 << PARTN_BITS, hwif_to_node(drive->hwif));
	if (!g)
		goto out_free_floppy;

	ide_init_disk(g, drive);

	kref_init(&floppy->kref);

	floppy->drive = drive;
	floppy->driver = &idefloppy_driver;
	floppy->disk = g;

	g->private_data = &floppy->driver;

	drive->driver_data = floppy;

	drive->debug_mask = debug_mask;

	ide_floppy_setup(drive);

	set_capacity(g, ide_floppy_capacity(drive));

	g->minors = 1 << PARTN_BITS;
	g->driverfs_dev = &drive->gendev;
	if (drive->dev_flags & IDE_DFLAG_REMOVABLE)
		g->flags = GENHD_FL_REMOVABLE;
	g->fops = &idefloppy_ops;
	add_disk(g);
	return 0;

out_free_floppy:
	kfree(floppy);
failed:
	return -ENODEV;
}

static int __init idefloppy_init(void)
{
	printk(KERN_INFO DRV_NAME " driver " IDEFLOPPY_VERSION "\n");
	return driver_register(&idefloppy_driver.gen_driver);
}

static void __exit idefloppy_exit(void)
{
	driver_unregister(&idefloppy_driver.gen_driver);
}

MODULE_ALIAS("ide:*m-floppy*");
MODULE_ALIAS("ide-floppy");
module_init(idefloppy_init);
module_exit(idefloppy_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ATAPI FLOPPY Driver");
