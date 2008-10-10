/*
 *  Copyright (C) 1994-1998	    Linus Torvalds & authors (see below)
 *  Copyright (C) 2003-2005, 2007   Bartlomiej Zolnierkiewicz
 */

/*
 *  Mostly written by Mark Lord  <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the multiple IDE interface driver, as evolved from hd.c.
 * It supports up to MAX_HWIFS IDE interfaces, on one or more IRQs
 *   (usually 14 & 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * ...
 *
 *  From hd.c:
 *  |
 *  | It traverses the request-list, using interrupts to jump between functions.
 *  | As nearly all functions can be called within interrupts, we may not sleep.
 *  | Special care is recommended.  Have Fun!
 *  |
 *  | modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *  |
 *  | Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  | in the early extended-partition checks and added DM partitions.
 *  |
 *  | Early work on error handling by Mika Liljeberg (liljeber@cs.Helsinki.FI).
 *  |
 *  | IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  | and general streamlining by Mark Lord (mlord@pobox.com).
 *
 *  October, 1994 -- Complete line-by-line overhaul for linux 1.1.x, by:
 *
 *	Mark Lord	(mlord@pobox.com)		(IDE Perf.Pkg)
 *	Delman Lee	(delman@ieee.org)		("Mr. atdisk2")
 *	Scott Snyder	(snyder@fnald0.fnal.gov)	(ATAPI IDE cd-rom)
 *
 *  This was a rewrite of just about everything from hd.c, though some original
 *  code is still sprinkled about.  Think of it as a major evolution, with
 *  inspiration from lots of linux users, esp.  hamish@zot.apana.org.au
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/hdreg.h>
#include <linux/completion.h>
#include <linux/device.h>


/* default maximum number of failures */
#define IDE_DEFAULT_MAX_FAILURES 	1

struct class *ide_port_class;

static const u8 ide_hwif_to_major[] = { IDE0_MAJOR, IDE1_MAJOR,
					IDE2_MAJOR, IDE3_MAJOR,
					IDE4_MAJOR, IDE5_MAJOR,
					IDE6_MAJOR, IDE7_MAJOR,
					IDE8_MAJOR, IDE9_MAJOR };

DEFINE_MUTEX(ide_cfg_mtx);

__cacheline_aligned_in_smp DEFINE_SPINLOCK(ide_lock);
EXPORT_SYMBOL(ide_lock);

static void ide_port_init_devices_data(ide_hwif_t *);

/*
 * Do not even *think* about calling this!
 */
void ide_init_port_data(ide_hwif_t *hwif, unsigned int index)
{
	/* bulk initialize hwif & drive info with zeros */
	memset(hwif, 0, sizeof(ide_hwif_t));

	/* fill in any non-zero initial values */
	hwif->index	= index;
	hwif->major	= ide_hwif_to_major[index];

	hwif->name[0]	= 'i';
	hwif->name[1]	= 'd';
	hwif->name[2]	= 'e';
	hwif->name[3]	= '0' + index;

	hwif->bus_state	= BUSSTATE_ON;

	init_completion(&hwif->gendev_rel_comp);

	hwif->tp_ops = &default_tp_ops;

	ide_port_init_devices_data(hwif);
}

static void ide_port_init_devices_data(ide_hwif_t *hwif)
{
	int unit;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		u8 j = (hwif->index * MAX_DRIVES) + unit;

		memset(drive, 0, sizeof(*drive));

		drive->media			= ide_disk;
		drive->select.all		= (unit<<4)|0xa0;
		drive->hwif			= hwif;
		drive->ready_stat		= ATA_DRDY;
		drive->bad_wstat		= BAD_W_STAT;
		drive->special.b.recalibrate	= 1;
		drive->special.b.set_geometry	= 1;
		drive->name[0]			= 'h';
		drive->name[1]			= 'd';
		drive->name[2]			= 'a' + j;
		drive->max_failures		= IDE_DEFAULT_MAX_FAILURES;

		INIT_LIST_HEAD(&drive->list);
		init_completion(&drive->gendev_rel_comp);
	}
}

/* Called with ide_lock held. */
static void __ide_port_unregister_devices(ide_hwif_t *hwif)
{
	int i;

	for (i = 0; i < MAX_DRIVES; i++) {
		ide_drive_t *drive = &hwif->drives[i];

		if (drive->present) {
			spin_unlock_irq(&ide_lock);
			device_unregister(&drive->gendev);
			wait_for_completion(&drive->gendev_rel_comp);
			spin_lock_irq(&ide_lock);
		}
	}
}

void ide_port_unregister_devices(ide_hwif_t *hwif)
{
	mutex_lock(&ide_cfg_mtx);
	spin_lock_irq(&ide_lock);
	__ide_port_unregister_devices(hwif);
	hwif->present = 0;
	ide_port_init_devices_data(hwif);
	spin_unlock_irq(&ide_lock);
	mutex_unlock(&ide_cfg_mtx);
}
EXPORT_SYMBOL_GPL(ide_port_unregister_devices);

/**
 *	ide_unregister		-	free an IDE interface
 *	@hwif: IDE interface
 *
 *	Perform the final unregister of an IDE interface. At the moment
 *	we don't refcount interfaces so this will also get split up.
 *
 *	Locking:
 *	The caller must not hold the IDE locks
 *	The drive present/vanishing is not yet properly locked
 *	Take care with the callbacks. These have been split to avoid
 *	deadlocking the IDE layer. The shutdown callback is called
 *	before we take the lock and free resources. It is up to the
 *	caller to be sure there is no pending I/O here, and that
 *	the interface will not be reopened (present/vanishing locking
 *	isn't yet done BTW). After we commit to the final kill we
 *	call the cleanup callback with the ide locks held.
 *
 *	Unregister restores the hwif structures to the default state.
 *	This is raving bonkers.
 */

void ide_unregister(ide_hwif_t *hwif)
{
	ide_hwif_t *g;
	ide_hwgroup_t *hwgroup;
	int irq_count = 0;

	BUG_ON(in_interrupt());
	BUG_ON(irqs_disabled());

	mutex_lock(&ide_cfg_mtx);

	spin_lock_irq(&ide_lock);
	if (hwif->present) {
		__ide_port_unregister_devices(hwif);
		hwif->present = 0;
	}
	spin_unlock_irq(&ide_lock);

	ide_proc_unregister_port(hwif);

	hwgroup = hwif->hwgroup;
	/*
	 * free the irq if we were the only hwif using it
	 */
	g = hwgroup->hwif;
	do {
		if (g->irq == hwif->irq)
			++irq_count;
		g = g->next;
	} while (g != hwgroup->hwif);
	if (irq_count == 1)
		free_irq(hwif->irq, hwgroup);

	ide_remove_port_from_hwgroup(hwif);

	device_unregister(hwif->portdev);
	device_unregister(&hwif->gendev);
	wait_for_completion(&hwif->gendev_rel_comp);

	/*
	 * Remove us from the kernel's knowledge
	 */
	blk_unregister_region(MKDEV(hwif->major, 0), MAX_DRIVES<<PARTN_BITS);
	kfree(hwif->sg_table);
	unregister_blkdev(hwif->major, hwif->name);

	if (hwif->dma_base)
		ide_release_dma_engine(hwif);

	mutex_unlock(&ide_cfg_mtx);
}

void ide_init_port_hw(ide_hwif_t *hwif, hw_regs_t *hw)
{
	memcpy(&hwif->io_ports, &hw->io_ports, sizeof(hwif->io_ports));
	hwif->irq = hw->irq;
	hwif->chipset = hw->chipset;
	hwif->dev = hw->dev;
	hwif->gendev.parent = hw->parent ? hw->parent : hw->dev;
	hwif->ack_intr = hw->ack_intr;
	hwif->config_data = hw->config;
}

/*
 *	Locks for IDE setting functionality
 */

DEFINE_MUTEX(ide_setting_mtx);

EXPORT_SYMBOL_GPL(ide_setting_mtx);

/**
 *	ide_spin_wait_hwgroup	-	wait for group
 *	@drive: drive in the group
 *
 *	Wait for an IDE device group to go non busy and then return
 *	holding the ide_lock which guards the hwgroup->busy status
 *	and right to use it.
 */

int ide_spin_wait_hwgroup (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned long timeout = jiffies + (3 * HZ);

	spin_lock_irq(&ide_lock);

	while (hwgroup->busy) {
		unsigned long lflags;
		spin_unlock_irq(&ide_lock);
		local_irq_set(lflags);
		if (time_after(jiffies, timeout)) {
			local_irq_restore(lflags);
			printk(KERN_ERR "%s: channel busy\n", drive->name);
			return -EBUSY;
		}
		local_irq_restore(lflags);
		spin_lock_irq(&ide_lock);
	}
	return 0;
}

EXPORT_SYMBOL(ide_spin_wait_hwgroup);

ide_devset_get(io_32bit, io_32bit);

int set_io_32bit(ide_drive_t *drive, int arg)
{
	if (drive->no_io_32bit)
		return -EPERM;

	if (arg < 0 || arg > 1 + (SUPPORT_VLB_SYNC << 1))
		return -EINVAL;

	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;

	drive->io_32bit = arg;

	spin_unlock_irq(&ide_lock);

	return 0;
}

ide_devset_get(ksettings, keep_settings);

int set_ksettings(ide_drive_t *drive, int arg)
{
	if (arg < 0 || arg > 1)
		return -EINVAL;

	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	drive->keep_settings = arg;
	spin_unlock_irq(&ide_lock);

	return 0;
}

ide_devset_get(using_dma, using_dma);

int set_using_dma(ide_drive_t *drive, int arg)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	ide_hwif_t *hwif = drive->hwif;
	int err = -EPERM;

	if (arg < 0 || arg > 1)
		return -EINVAL;

	if (ata_id_has_dma(drive->id) == 0)
		goto out;

	if (hwif->dma_ops == NULL)
		goto out;

	err = -EBUSY;
	if (ide_spin_wait_hwgroup(drive))
		goto out;
	/*
	 * set ->busy flag, unlock and let it ride
	 */
	hwif->hwgroup->busy = 1;
	spin_unlock_irq(&ide_lock);

	err = 0;

	if (arg) {
		if (ide_set_dma(drive))
			err = -EIO;
	} else
		ide_dma_off(drive);

	/*
	 * lock, clear ->busy flag and unlock before leaving
	 */
	spin_lock_irq(&ide_lock);
	hwif->hwgroup->busy = 0;
	spin_unlock_irq(&ide_lock);
out:
	return err;
#else
	if (arg < 0 || arg > 1)
		return -EINVAL;

	return -EPERM;
#endif
}

int set_pio_mode(ide_drive_t *drive, int arg)
{
	struct request *rq;
	ide_hwif_t *hwif = drive->hwif;
	const struct ide_port_ops *port_ops = hwif->port_ops;

	if (arg < 0 || arg > 255)
		return -EINVAL;

	if (port_ops == NULL || port_ops->set_pio_mode == NULL ||
	    (hwif->host_flags & IDE_HFLAG_NO_SET_MODE))
		return -ENOSYS;

	if (drive->special.b.set_tune)
		return -EBUSY;

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_ATA_TASKFILE;

	drive->tune_req = (u8) arg;
	drive->special.b.set_tune = 1;

	blk_execute_rq(drive->queue, NULL, rq, 0);
	blk_put_request(rq);

	return 0;
}

ide_devset_get(unmaskirq, unmask);

int set_unmaskirq(ide_drive_t *drive, int arg)
{
	if (drive->no_unmask)
		return -EPERM;

	if (arg < 0 || arg > 1)
		return -EINVAL;

	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	drive->unmask = arg;
	spin_unlock_irq(&ide_lock);

	return 0;
}

static int generic_ide_suspend(struct device *dev, pm_message_t mesg)
{
	ide_drive_t *drive = dev->driver_data;
	ide_hwif_t *hwif = HWIF(drive);
	struct request *rq;
	struct request_pm_state rqpm;
	ide_task_t args;
	int ret;

	/* Call ACPI _GTM only once */
	if (!(drive->dn % 2))
		ide_acpi_get_timing(hwif);

	memset(&rqpm, 0, sizeof(rqpm));
	memset(&args, 0, sizeof(args));
	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_PM_SUSPEND;
	rq->special = &args;
	rq->data = &rqpm;
	rqpm.pm_step = ide_pm_state_start_suspend;
	if (mesg.event == PM_EVENT_PRETHAW)
		mesg.event = PM_EVENT_FREEZE;
	rqpm.pm_state = mesg.event;

	ret = blk_execute_rq(drive->queue, NULL, rq, 0);
	blk_put_request(rq);
	/* only call ACPI _PS3 after both drivers are suspended */
	if (!ret && (((drive->dn % 2) && hwif->drives[0].present
		 && hwif->drives[1].present)
		 || !hwif->drives[0].present
		 || !hwif->drives[1].present))
		ide_acpi_set_state(hwif, 0);
	return ret;
}

static int generic_ide_resume(struct device *dev)
{
	ide_drive_t *drive = dev->driver_data;
	ide_hwif_t *hwif = HWIF(drive);
	struct request *rq;
	struct request_pm_state rqpm;
	ide_task_t args;
	int err;

	/* Call ACPI _STM only once */
	if (!(drive->dn % 2)) {
		ide_acpi_set_state(hwif, 1);
		ide_acpi_push_timing(hwif);
	}

	ide_acpi_exec_tfs(drive);

	memset(&rqpm, 0, sizeof(rqpm));
	memset(&args, 0, sizeof(args));
	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_PM_RESUME;
	rq->cmd_flags |= REQ_PREEMPT;
	rq->special = &args;
	rq->data = &rqpm;
	rqpm.pm_step = ide_pm_state_start_resume;
	rqpm.pm_state = PM_EVENT_ON;

	err = blk_execute_rq(drive->queue, NULL, rq, 1);
	blk_put_request(rq);

	if (err == 0 && dev->driver) {
		ide_driver_t *drv = to_ide_driver(dev->driver);

		if (drv->resume)
			drv->resume(drive);
	}

	return err;
}

static int generic_drive_reset(ide_drive_t *drive)
{
	struct request *rq;
	int ret = 0;

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd_len = 1;
	rq->cmd[0] = REQ_DRIVE_RESET;
	rq->cmd_flags |= REQ_SOFTBARRIER;
	if (blk_execute_rq(drive->queue, NULL, rq, 1))
		ret = rq->errors;
	blk_put_request(rq);
	return ret;
}

static inline void ide_id_to_hd_driveid(u16 *id)
{
#ifdef __BIG_ENDIAN
	/* accessed in struct hd_driveid as 8-bit values */
	id[ATA_ID_MAX_MULTSECT]	 = __cpu_to_le16(id[ATA_ID_MAX_MULTSECT]);
	id[ATA_ID_CAPABILITY]	 = __cpu_to_le16(id[ATA_ID_CAPABILITY]);
	id[ATA_ID_OLD_PIO_MODES] = __cpu_to_le16(id[ATA_ID_OLD_PIO_MODES]);
	id[ATA_ID_OLD_DMA_MODES] = __cpu_to_le16(id[ATA_ID_OLD_DMA_MODES]);
	id[ATA_ID_MULTSECT]	 = __cpu_to_le16(id[ATA_ID_MULTSECT]);

	/* as 32-bit values */
	*(u32 *)&id[ATA_ID_LBA_CAPACITY] = ata_id_u32(id, ATA_ID_LBA_CAPACITY);
	*(u32 *)&id[ATA_ID_SPG]		 = ata_id_u32(id, ATA_ID_SPG);

	/* as 64-bit value */
	*(u64 *)&id[ATA_ID_LBA_CAPACITY_2] =
		ata_id_u64(id, ATA_ID_LBA_CAPACITY_2);
#endif
}

static int ide_get_identity_ioctl(ide_drive_t *drive, unsigned int cmd,
				  unsigned long arg)
{
	u16 *id = NULL;
	int size = (cmd == HDIO_GET_IDENTITY) ? (ATA_ID_WORDS * 2) : 142;
	int rc = 0;

	if (drive->id_read == 0) {
		rc = -ENOMSG;
		goto out;
	}

	id = kmalloc(size, GFP_KERNEL);
	if (id == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(id, drive->id, size);
	ide_id_to_hd_driveid(id);

	if (copy_to_user((void __user *)arg, id, size))
		rc = -EFAULT;

	kfree(id);
out:
	return rc;
}

int generic_ide_ioctl(ide_drive_t *drive, struct file *file, struct block_device *bdev,
			unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	ide_driver_t *drv;
	int err = 0, (*getfunc)(ide_drive_t *), (*setfunc)(ide_drive_t *, int);

	switch (cmd) {
	case HDIO_GET_32BIT:	    getfunc = get_io_32bit;	 goto read_val;
	case HDIO_GET_KEEPSETTINGS: getfunc = get_ksettings;	 goto read_val;
	case HDIO_GET_UNMASKINTR:   getfunc = get_unmaskirq;	 goto read_val;
	case HDIO_GET_DMA:	    getfunc = get_using_dma;	 goto read_val;
	case HDIO_SET_32BIT:	    setfunc = set_io_32bit;	 goto set_val;
	case HDIO_SET_KEEPSETTINGS: setfunc = set_ksettings;	 goto set_val;
	case HDIO_SET_PIO_MODE:	    setfunc = set_pio_mode;	 goto set_val;
	case HDIO_SET_UNMASKINTR:   setfunc = set_unmaskirq;	 goto set_val;
	case HDIO_SET_DMA:	    setfunc = set_using_dma;	 goto set_val;
	}

	switch (cmd) {
		case HDIO_OBSOLETE_IDENTITY:
		case HDIO_GET_IDENTITY:
			if (bdev != bdev->bd_contains)
				return -EINVAL;
			return ide_get_identity_ioctl(drive, cmd, arg);
		case HDIO_GET_NICE:
			return put_user(drive->dsc_overlap	<<	IDE_NICE_DSC_OVERLAP	|
					drive->nice1 << IDE_NICE_1,
					(long __user *) arg);
#ifdef CONFIG_IDE_TASK_IOCTL
		case HDIO_DRIVE_TASKFILE:
		        if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
				return -EACCES;
			switch(drive->media) {
				case ide_disk:
					return ide_taskfile_ioctl(drive, cmd, arg);
				default:
					return -ENOMSG;
			}
#endif /* CONFIG_IDE_TASK_IOCTL */

		case HDIO_DRIVE_CMD:
			if (!capable(CAP_SYS_RAWIO))
				return -EACCES;
			return ide_cmd_ioctl(drive, cmd, arg);

		case HDIO_DRIVE_TASK:
			if (!capable(CAP_SYS_RAWIO))
				return -EACCES;
			return ide_task_ioctl(drive, cmd, arg);
		case HDIO_SET_NICE:
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
			if (arg != (arg & ((1 << IDE_NICE_DSC_OVERLAP) | (1 << IDE_NICE_1))))
				return -EPERM;
			drive->dsc_overlap = (arg >> IDE_NICE_DSC_OVERLAP) & 1;
			drv = *(ide_driver_t **)bdev->bd_disk->private_data;
			if (drive->dsc_overlap && !drv->supports_dsc_overlap) {
				drive->dsc_overlap = 0;
				return -EPERM;
			}
			drive->nice1 = (arg >> IDE_NICE_1) & 1;
			return 0;
		case HDIO_DRIVE_RESET:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;

			return generic_drive_reset(drive);

		case HDIO_GET_BUSSTATE:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (put_user(HWIF(drive)->bus_state, (long __user *)arg))
				return -EFAULT;
			return 0;

		case HDIO_SET_BUSSTATE:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			return -EOPNOTSUPP;
		default:
			return -EINVAL;
	}

read_val:
	mutex_lock(&ide_setting_mtx);
	spin_lock_irqsave(&ide_lock, flags);
	err = getfunc(drive);
	spin_unlock_irqrestore(&ide_lock, flags);
	mutex_unlock(&ide_setting_mtx);
	return err >= 0 ? put_user(err, (long __user *)arg) : err;

set_val:
	if (bdev != bdev->bd_contains)
		err = -EINVAL;
	else {
		if (!capable(CAP_SYS_ADMIN))
			err = -EACCES;
		else {
			mutex_lock(&ide_setting_mtx);
			err = setfunc(drive, arg);
			mutex_unlock(&ide_setting_mtx);
		}
	}
	return err;
}

EXPORT_SYMBOL(generic_ide_ioctl);

/**
 * ide_device_get	-	get an additional reference to a ide_drive_t
 * @drive:	device to get a reference to
 *
 * Gets a reference to the ide_drive_t and increments the use count of the
 * underlying LLDD module.
 */
int ide_device_get(ide_drive_t *drive)
{
	struct device *host_dev;
	struct module *module;

	if (!get_device(&drive->gendev))
		return -ENXIO;

	host_dev = drive->hwif->host->dev[0];
	module = host_dev ? host_dev->driver->owner : NULL;

	if (module && !try_module_get(module)) {
		put_device(&drive->gendev);
		return -ENXIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ide_device_get);

/**
 * ide_device_put	-	release a reference to a ide_drive_t
 * @drive:	device to release a reference on
 *
 * Release a reference to the ide_drive_t and decrements the use count of
 * the underlying LLDD module.
 */
void ide_device_put(ide_drive_t *drive)
{
#ifdef CONFIG_MODULE_UNLOAD
	struct device *host_dev = drive->hwif->host->dev[0];
	struct module *module = host_dev ? host_dev->driver->owner : NULL;

	if (module)
		module_put(module);
#endif
	put_device(&drive->gendev);
}
EXPORT_SYMBOL_GPL(ide_device_put);

static int ide_bus_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static char *media_string(ide_drive_t *drive)
{
	switch (drive->media) {
	case ide_disk:
		return "disk";
	case ide_cdrom:
		return "cdrom";
	case ide_tape:
		return "tape";
	case ide_floppy:
		return "floppy";
	case ide_optical:
		return "optical";
	default:
		return "UNKNOWN";
	}
}

static ssize_t media_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", media_string(drive));
}

static ssize_t drivename_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", drive->name);
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "ide:m-%s\n", media_string(drive));
}

static ssize_t model_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", (char *)&drive->id[ATA_ID_PROD]);
}

static ssize_t firmware_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", (char *)&drive->id[ATA_ID_FW_REV]);
}

static ssize_t serial_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ide_drive_t *drive = to_ide_device(dev);
	return sprintf(buf, "%s\n", (char *)&drive->id[ATA_ID_SERNO]);
}

static struct device_attribute ide_dev_attrs[] = {
	__ATTR_RO(media),
	__ATTR_RO(drivename),
	__ATTR_RO(modalias),
	__ATTR_RO(model),
	__ATTR_RO(firmware),
	__ATTR(serial, 0400, serial_show, NULL),
	__ATTR_NULL
};

static int ide_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	ide_drive_t *drive = to_ide_device(dev);

	add_uevent_var(env, "MEDIA=%s", media_string(drive));
	add_uevent_var(env, "DRIVENAME=%s", drive->name);
	add_uevent_var(env, "MODALIAS=ide:m-%s", media_string(drive));
	return 0;
}

static int generic_ide_probe(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	ide_driver_t *drv = to_ide_driver(dev->driver);

	return drv->probe ? drv->probe(drive) : -ENODEV;
}

static int generic_ide_remove(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	ide_driver_t *drv = to_ide_driver(dev->driver);

	if (drv->remove)
		drv->remove(drive);

	return 0;
}

static void generic_ide_shutdown(struct device *dev)
{
	ide_drive_t *drive = to_ide_device(dev);
	ide_driver_t *drv = to_ide_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(drive);
}

struct bus_type ide_bus_type = {
	.name		= "ide",
	.match		= ide_bus_match,
	.uevent		= ide_uevent,
	.probe		= generic_ide_probe,
	.remove		= generic_ide_remove,
	.shutdown	= generic_ide_shutdown,
	.dev_attrs	= ide_dev_attrs,
	.suspend	= generic_ide_suspend,
	.resume		= generic_ide_resume,
};

EXPORT_SYMBOL_GPL(ide_bus_type);

int ide_vlb_clk;
EXPORT_SYMBOL_GPL(ide_vlb_clk);

module_param_named(vlb_clock, ide_vlb_clk, int, 0);
MODULE_PARM_DESC(vlb_clock, "VLB clock frequency (in MHz)");

int ide_pci_clk;
EXPORT_SYMBOL_GPL(ide_pci_clk);

module_param_named(pci_clock, ide_pci_clk, int, 0);
MODULE_PARM_DESC(pci_clock, "PCI bus clock frequency (in MHz)");

static int ide_set_dev_param_mask(const char *s, struct kernel_param *kp)
{
	int a, b, i, j = 1;
	unsigned int *dev_param_mask = (unsigned int *)kp->arg;

	if (sscanf(s, "%d.%d:%d", &a, &b, &j) != 3 &&
	    sscanf(s, "%d.%d", &a, &b) != 2)
		return -EINVAL;

	i = a * MAX_DRIVES + b;

	if (i >= MAX_HWIFS * MAX_DRIVES || j < 0 || j > 1)
		return -EINVAL;

	if (j)
		*dev_param_mask |= (1 << i);
	else
		*dev_param_mask &= (1 << i);

	return 0;
}

static unsigned int ide_nodma;

module_param_call(nodma, ide_set_dev_param_mask, NULL, &ide_nodma, 0);
MODULE_PARM_DESC(nodma, "disallow DMA for a device");

static unsigned int ide_noflush;

module_param_call(noflush, ide_set_dev_param_mask, NULL, &ide_noflush, 0);
MODULE_PARM_DESC(noflush, "disable flush requests for a device");

static unsigned int ide_noprobe;

module_param_call(noprobe, ide_set_dev_param_mask, NULL, &ide_noprobe, 0);
MODULE_PARM_DESC(noprobe, "skip probing for a device");

static unsigned int ide_nowerr;

module_param_call(nowerr, ide_set_dev_param_mask, NULL, &ide_nowerr, 0);
MODULE_PARM_DESC(nowerr, "ignore the ATA_DF bit for a device");

static unsigned int ide_cdroms;

module_param_call(cdrom, ide_set_dev_param_mask, NULL, &ide_cdroms, 0);
MODULE_PARM_DESC(cdrom, "force device as a CD-ROM");

struct chs_geom {
	unsigned int	cyl;
	u8		head;
	u8		sect;
};

static unsigned int ide_disks;
static struct chs_geom ide_disks_chs[MAX_HWIFS * MAX_DRIVES];

static int ide_set_disk_chs(const char *str, struct kernel_param *kp)
{
	int a, b, c = 0, h = 0, s = 0, i, j = 1;

	if (sscanf(str, "%d.%d:%d,%d,%d", &a, &b, &c, &h, &s) != 5 &&
	    sscanf(str, "%d.%d:%d", &a, &b, &j) != 3)
		return -EINVAL;

	i = a * MAX_DRIVES + b;

	if (i >= MAX_HWIFS * MAX_DRIVES || j < 0 || j > 1)
		return -EINVAL;

	if (c > INT_MAX || h > 255 || s > 255)
		return -EINVAL;

	if (j)
		ide_disks |= (1 << i);
	else
		ide_disks &= (1 << i);

	ide_disks_chs[i].cyl  = c;
	ide_disks_chs[i].head = h;
	ide_disks_chs[i].sect = s;

	return 0;
}

module_param_call(chs, ide_set_disk_chs, NULL, NULL, 0);
MODULE_PARM_DESC(chs, "force device as a disk (using CHS)");

static void ide_dev_apply_params(ide_drive_t *drive)
{
	int i = drive->hwif->index * MAX_DRIVES + drive->select.b.unit;

	if (ide_nodma & (1 << i)) {
		printk(KERN_INFO "ide: disallowing DMA for %s\n", drive->name);
		drive->nodma = 1;
	}
	if (ide_noflush & (1 << i)) {
		printk(KERN_INFO "ide: disabling flush requests for %s\n",
				 drive->name);
		drive->noflush = 1;
	}
	if (ide_noprobe & (1 << i)) {
		printk(KERN_INFO "ide: skipping probe for %s\n", drive->name);
		drive->noprobe = 1;
	}
	if (ide_nowerr & (1 << i)) {
		printk(KERN_INFO "ide: ignoring the ATA_DF bit for %s\n",
				 drive->name);
		drive->bad_wstat = BAD_R_STAT;
	}
	if (ide_cdroms & (1 << i)) {
		printk(KERN_INFO "ide: forcing %s as a CD-ROM\n", drive->name);
		drive->present = 1;
		drive->media = ide_cdrom;
		/* an ATAPI device ignores DRDY */
		drive->ready_stat = 0;
	}
	if (ide_disks & (1 << i)) {
		drive->cyl  = drive->bios_cyl  = ide_disks_chs[i].cyl;
		drive->head = drive->bios_head = ide_disks_chs[i].head;
		drive->sect = drive->bios_sect = ide_disks_chs[i].sect;
		drive->forced_geom = 1;
		printk(KERN_INFO "ide: forcing %s as a disk (%d/%d/%d)\n",
				 drive->name,
				 drive->cyl, drive->head, drive->sect);
		drive->present = 1;
		drive->media = ide_disk;
		drive->ready_stat = ATA_DRDY;
	}
}

static unsigned int ide_ignore_cable;

static int ide_set_ignore_cable(const char *s, struct kernel_param *kp)
{
	int i, j = 1;

	if (sscanf(s, "%d:%d", &i, &j) != 2 && sscanf(s, "%d", &i) != 1)
		return -EINVAL;

	if (i >= MAX_HWIFS || j < 0 || j > 1)
		return -EINVAL;

	if (j)
		ide_ignore_cable |= (1 << i);
	else
		ide_ignore_cable &= (1 << i);

	return 0;
}

module_param_call(ignore_cable, ide_set_ignore_cable, NULL, NULL, 0);
MODULE_PARM_DESC(ignore_cable, "ignore cable detection");

void ide_port_apply_params(ide_hwif_t *hwif)
{
	int i;

	if (ide_ignore_cable & (1 << hwif->index)) {
		printk(KERN_INFO "ide: ignoring cable detection for %s\n",
				 hwif->name);
		hwif->cbl = ATA_CBL_PATA40_SHORT;
	}

	for (i = 0; i < MAX_DRIVES; i++)
		ide_dev_apply_params(&hwif->drives[i]);
}

/*
 * This is gets invoked once during initialization, to set *everything* up
 */
static int __init ide_init(void)
{
	int ret;

	printk(KERN_INFO "Uniform Multi-Platform E-IDE driver\n");

	ret = bus_register(&ide_bus_type);
	if (ret < 0) {
		printk(KERN_WARNING "IDE: bus_register error: %d\n", ret);
		return ret;
	}

	ide_port_class = class_create(THIS_MODULE, "ide_port");
	if (IS_ERR(ide_port_class)) {
		ret = PTR_ERR(ide_port_class);
		goto out_port_class;
	}

	proc_ide_create();

	return 0;

out_port_class:
	bus_unregister(&ide_bus_type);

	return ret;
}

static void __exit ide_exit(void)
{
	proc_ide_destroy();

	class_destroy(ide_port_class);

	bus_unregister(&ide_bus_type);
}

module_init(ide_init);
module_exit(ide_exit);

MODULE_LICENSE("GPL");
