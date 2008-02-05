/* -*- linux-c -*-
 *  drivers/cdrom/viocd.c
 *
 *  iSeries Virtual CD Rom
 *
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *           Stephen Rothwell <sfr@au1.ibm.com>
 *
 * (C) Copyright 2000-2004 IBM Corporation
 *
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) anyu later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * This routine provides access to CD ROM drives owned and managed by an
 * OS/400 partition running on the same box as this Linux partition.
 *
 * All operations are performed by sending messages back and forth to
 * the OS/400 partition.
 */

#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>

#include <asm/vio.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_event.h>
#include <asm/iseries/vio.h>
#include <asm/firmware.h>

#define VIOCD_DEVICE			"iseries/vcd"

#define VIOCD_VERS "1.06"

#define VIOCD_KERN_WARNING		KERN_WARNING "viocd: "
#define VIOCD_KERN_INFO			KERN_INFO "viocd: "

/*
 * Should probably make this a module parameter....sigh
 */
#define VIOCD_MAX_CD	HVMAXARCHITECTEDVIRTUALCDROMS

static const struct vio_error_entry viocd_err_table[] = {
	{0x0201, EINVAL, "Invalid Range"},
	{0x0202, EINVAL, "Invalid Token"},
	{0x0203, EIO, "DMA Error"},
	{0x0204, EIO, "Use Error"},
	{0x0205, EIO, "Release Error"},
	{0x0206, EINVAL, "Invalid CD"},
	{0x020C, EROFS, "Read Only Device"},
	{0x020D, ENOMEDIUM, "Changed or Missing Volume (or Varied Off?)"},
	{0x020E, EIO, "Optical System Error (Varied Off?)"},
	{0x02FF, EIO, "Internal Error"},
	{0x3010, EIO, "Changed Volume"},
	{0xC100, EIO, "Optical System Error"},
	{0x0000, 0, NULL},
};

/*
 * This is the structure we use to exchange info between driver and interrupt
 * handler
 */
struct viocd_waitevent {
	struct completion	com;
	int			rc;
	u16			sub_result;
	int			changed;
};

/* this is a lookup table for the true capabilities of a device */
struct capability_entry {
	char	*type;
	int	capability;
};

static struct capability_entry capability_table[] __initdata = {
	{ "6330", CDC_LOCK | CDC_DVD_RAM | CDC_RAM },
	{ "6331", CDC_LOCK | CDC_DVD_RAM | CDC_RAM },
	{ "6333", CDC_LOCK | CDC_DVD_RAM | CDC_RAM },
	{ "632A", CDC_LOCK | CDC_DVD_RAM | CDC_RAM },
	{ "6321", CDC_LOCK },
	{ "632B", 0 },
	{ NULL  , CDC_LOCK },
};

/* These are our internal structures for keeping track of devices */
static int viocd_numdev;

struct disk_info {
	struct gendisk			*viocd_disk;
	struct cdrom_device_info	viocd_info;
	struct device			*dev;
	const char			*rsrcname;
	const char			*type;
	const char			*model;
};
static struct disk_info viocd_diskinfo[VIOCD_MAX_CD];

#define DEVICE_NR(di)	((di) - &viocd_diskinfo[0])

static spinlock_t viocd_reqlock;

#define MAX_CD_REQ	1

/* procfs support */
static int proc_viocd_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < viocd_numdev; i++) {
		seq_printf(m, "viocd device %d is iSeries resource %10.10s"
				"type %4.4s, model %3.3s\n",
				i, viocd_diskinfo[i].rsrcname,
				viocd_diskinfo[i].type,
				viocd_diskinfo[i].model);
	}
	return 0;
}

static int proc_viocd_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_viocd_show, NULL);
}

static const struct file_operations proc_viocd_operations = {
	.open		= proc_viocd_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int viocd_blk_open(struct inode *inode, struct file *file)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_open(&di->viocd_info, inode, file);
}

static int viocd_blk_release(struct inode *inode, struct file *file)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_release(&di->viocd_info, file);
}

static int viocd_blk_ioctl(struct inode *inode, struct file *file,
		unsigned cmd, unsigned long arg)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_ioctl(file, &di->viocd_info, inode, cmd, arg);
}

static int viocd_blk_media_changed(struct gendisk *disk)
{
	struct disk_info *di = disk->private_data;
	return cdrom_media_changed(&di->viocd_info);
}

struct block_device_operations viocd_fops = {
	.owner =		THIS_MODULE,
	.open =			viocd_blk_open,
	.release =		viocd_blk_release,
	.ioctl =		viocd_blk_ioctl,
	.media_changed =	viocd_blk_media_changed,
};

static int viocd_open(struct cdrom_device_info *cdi, int purpose)
{
        struct disk_info *diskinfo = cdi->handle;
	int device_no = DEVICE_NR(diskinfo);
	HvLpEvent_Rc hvrc;
	struct viocd_waitevent we;

	init_completion(&we.com);
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdopen,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, ((u64)device_no << 48),
			0, 0, 0);
	if (hvrc != 0) {
		printk(VIOCD_KERN_WARNING
				"bad rc on HvCallEvent_signalLpEventFast %d\n",
				(int)hvrc);
		return -EIO;
	}

	wait_for_completion(&we.com);

	if (we.rc) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viocd_err_table, we.sub_result);
		printk(VIOCD_KERN_WARNING "bad rc %d:0x%04X on open: %s\n",
				we.rc, we.sub_result, err->msg);
		return -err->errno;
	}

	return 0;
}

static void viocd_release(struct cdrom_device_info *cdi)
{
	int device_no = DEVICE_NR((struct disk_info *)cdi->handle);
	HvLpEvent_Rc hvrc;

	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdclose,
			HvLpEvent_AckInd_NoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp), 0,
			VIOVERSION << 16, ((u64)device_no << 48), 0, 0, 0);
	if (hvrc != 0)
		printk(VIOCD_KERN_WARNING
				"bad rc on HvCallEvent_signalLpEventFast %d\n",
				(int)hvrc);
}

/* Send a read or write request to OS/400 */
static int send_request(struct request *req)
{
	HvLpEvent_Rc hvrc;
	struct disk_info *diskinfo = req->rq_disk->private_data;
	u64 len;
	dma_addr_t dmaaddr;
	int direction;
	u16 cmd;
	struct scatterlist sg;

	BUG_ON(req->nr_phys_segments > 1);

	if (rq_data_dir(req) == READ) {
		direction = DMA_FROM_DEVICE;
		cmd = viomajorsubtype_cdio | viocdread;
	} else {
		direction = DMA_TO_DEVICE;
		cmd = viomajorsubtype_cdio | viocdwrite;
	}

	sg_init_table(&sg, 1);
        if (blk_rq_map_sg(req->q, req, &sg) == 0) {
		printk(VIOCD_KERN_WARNING
				"error setting up scatter/gather list\n");
		return -1;
	}

	if (dma_map_sg(diskinfo->dev, &sg, 1, direction) == 0) {
		printk(VIOCD_KERN_WARNING "error allocating sg tce\n");
		return -1;
	}
	dmaaddr = sg_dma_address(&sg);
	len = sg_dma_len(&sg);

	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo, cmd,
			HvLpEvent_AckInd_DoAck,
			HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)req, VIOVERSION << 16,
			((u64)DEVICE_NR(diskinfo) << 48) | dmaaddr,
			(u64)req->sector * 512, len, 0);
	if (hvrc != HvLpEvent_Rc_Good) {
		printk(VIOCD_KERN_WARNING "hv error on op %d\n", (int)hvrc);
		return -1;
	}

	return 0;
}

static void viocd_end_request(struct request *req, int error)
{
	int nsectors = req->hard_nr_sectors;

	/*
	 * Make sure it's fully ended, and ensure that we process
	 * at least one sector.
	 */
	if (blk_pc_request(req))
		nsectors = (req->data_len + 511) >> 9;
	if (!nsectors)
		nsectors = 1;

	if (__blk_end_request(req, error, nsectors << 9))
		BUG();
}

static int rwreq;

static void do_viocd_request(struct request_queue *q)
{
	struct request *req;

	while ((rwreq == 0) && ((req = elv_next_request(q)) != NULL)) {
		if (!blk_fs_request(req))
			viocd_end_request(req, -EIO);
		else if (send_request(req) < 0) {
			printk(VIOCD_KERN_WARNING
					"unable to send message to OS/400!");
			viocd_end_request(req, -EIO);
		} else
			rwreq++;
	}
}

static int viocd_media_changed(struct cdrom_device_info *cdi, int disc_nr)
{
	struct viocd_waitevent we;
	HvLpEvent_Rc hvrc;
	int device_no = DEVICE_NR((struct disk_info *)cdi->handle);

	init_completion(&we.com);

	/* Send the open event to OS/400 */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdcheck,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, ((u64)device_no << 48),
			0, 0, 0);
	if (hvrc != 0) {
		printk(VIOCD_KERN_WARNING "bad rc on HvCallEvent_signalLpEventFast %d\n",
				(int)hvrc);
		return -EIO;
	}

	wait_for_completion(&we.com);

	/* Check the return code.  If bad, assume no change */
	if (we.rc) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viocd_err_table, we.sub_result);
		printk(VIOCD_KERN_WARNING
				"bad rc %d:0x%04X on check_change: %s; Assuming no change\n",
				we.rc, we.sub_result, err->msg);
		return 0;
	}

	return we.changed;
}

static int viocd_lock_door(struct cdrom_device_info *cdi, int locking)
{
	HvLpEvent_Rc hvrc;
	u64 device_no = DEVICE_NR((struct disk_info *)cdi->handle);
	/* NOTE: flags is 1 or 0 so it won't overwrite the device_no */
	u64 flags = !!locking;
	struct viocd_waitevent we;

	init_completion(&we.com);

	/* Send the lockdoor event to OS/400 */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdlockdoor,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16,
			(device_no << 48) | (flags << 32), 0, 0, 0);
	if (hvrc != 0) {
		printk(VIOCD_KERN_WARNING "bad rc on HvCallEvent_signalLpEventFast %d\n",
				(int)hvrc);
		return -EIO;
	}

	wait_for_completion(&we.com);

	if (we.rc != 0)
		return -EIO;
	return 0;
}

static int viocd_packet(struct cdrom_device_info *cdi,
		struct packet_command *cgc)
{
	unsigned int buflen = cgc->buflen;
	int ret = -EIO;

	switch (cgc->cmd[0]) {
	case GPCMD_READ_DISC_INFO:
		{
			disc_information *di = (disc_information *)cgc->buffer;

			if (buflen >= 2) {
				di->disc_information_length = cpu_to_be16(1);
				ret = 0;
			}
			if (buflen >= 3)
				di->erasable =
					(cdi->ops->capability & ~cdi->mask
					 & (CDC_DVD_RAM | CDC_RAM)) != 0;
		}
		break;
	case GPCMD_GET_CONFIGURATION:
		if (cgc->cmd[3] == CDF_RWRT) {
			struct rwrt_feature_desc *rfd = (struct rwrt_feature_desc *)(cgc->buffer + sizeof(struct feature_header));

			if ((buflen >=
			     (sizeof(struct feature_header) + sizeof(*rfd))) &&
			    (cdi->ops->capability & ~cdi->mask
			     & (CDC_DVD_RAM | CDC_RAM))) {
				rfd->feature_code = cpu_to_be16(CDF_RWRT);
				rfd->curr = 1;
				ret = 0;
			}
		}
		break;
	default:
		if (cgc->sense) {
			/* indicate Unknown code */
			cgc->sense->sense_key = 0x05;
			cgc->sense->asc = 0x20;
			cgc->sense->ascq = 0x00;
		}
		break;
	}

	cgc->stat = ret;
	return ret;
}

static void restart_all_queues(int first_index)
{
	int i;

	for (i = first_index + 1; i < viocd_numdev; i++)
		if (viocd_diskinfo[i].viocd_disk)
			blk_run_queue(viocd_diskinfo[i].viocd_disk->queue);
	for (i = 0; i <= first_index; i++)
		if (viocd_diskinfo[i].viocd_disk)
			blk_run_queue(viocd_diskinfo[i].viocd_disk->queue);
}

/* This routine handles incoming CD LP events */
static void vio_handle_cd_event(struct HvLpEvent *event)
{
	struct viocdlpevent *bevent;
	struct viocd_waitevent *pwe;
	struct disk_info *di;
	unsigned long flags;
	struct request *req;


	if (event == NULL)
		/* Notification that a partition went away! */
		return;
	/* First, we should NEVER get an int here...only acks */
	if (hvlpevent_is_int(event)) {
		printk(VIOCD_KERN_WARNING
				"Yikes! got an int in viocd event handler!\n");
		if (hvlpevent_need_ack(event)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}

	bevent = (struct viocdlpevent *)event;

	switch (event->xSubtype & VIOMINOR_SUBTYPE_MASK) {
	case viocdopen:
		if (event->xRc == 0) {
			di = &viocd_diskinfo[bevent->disk];
			blk_queue_hardsect_size(di->viocd_disk->queue,
					bevent->block_size);
			set_capacity(di->viocd_disk,
					bevent->media_size *
					bevent->block_size / 512);
		}
		/* FALLTHROUGH !! */
	case viocdlockdoor:
		pwe = (struct viocd_waitevent *)event->xCorrelationToken;
return_complete:
		pwe->rc = event->xRc;
		pwe->sub_result = bevent->sub_result;
		complete(&pwe->com);
		break;

	case viocdcheck:
		pwe = (struct viocd_waitevent *)event->xCorrelationToken;
		pwe->changed = bevent->flags;
		goto return_complete;

	case viocdclose:
		break;

	case viocdwrite:
	case viocdread:
		/*
		 * Since this is running in interrupt mode, we need to
		 * make sure we're not stepping on any global I/O operations
		 */
		di = &viocd_diskinfo[bevent->disk];
		spin_lock_irqsave(&viocd_reqlock, flags);
		dma_unmap_single(di->dev, bevent->token, bevent->len,
				((event->xSubtype & VIOMINOR_SUBTYPE_MASK) == viocdread)
				?  DMA_FROM_DEVICE : DMA_TO_DEVICE);
		req = (struct request *)bevent->event.xCorrelationToken;
		rwreq--;

		if (event->xRc != HvLpEvent_Rc_Good) {
			const struct vio_error_entry *err =
				vio_lookup_rc(viocd_err_table,
						bevent->sub_result);
			printk(VIOCD_KERN_WARNING "request %p failed "
					"with rc %d:0x%04X: %s\n",
					req, event->xRc,
					bevent->sub_result, err->msg);
			viocd_end_request(req, -EIO);
		} else
			viocd_end_request(req, 0);

		/* restart handling of incoming requests */
		spin_unlock_irqrestore(&viocd_reqlock, flags);
		restart_all_queues(bevent->disk);
		break;

	default:
		printk(VIOCD_KERN_WARNING
				"message with invalid subtype %0x04X!\n",
				event->xSubtype & VIOMINOR_SUBTYPE_MASK);
		if (hvlpevent_need_ack(event)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

static struct cdrom_device_ops viocd_dops = {
	.open = viocd_open,
	.release = viocd_release,
	.media_changed = viocd_media_changed,
	.lock_door = viocd_lock_door,
	.generic_packet = viocd_packet,
	.capability = CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK | CDC_SELECT_SPEED | CDC_SELECT_DISC | CDC_MULTI_SESSION | CDC_MCN | CDC_MEDIA_CHANGED | CDC_PLAY_AUDIO | CDC_RESET | CDC_DRIVE_STATUS | CDC_GENERIC_PACKET | CDC_CD_R | CDC_CD_RW | CDC_DVD | CDC_DVD_R | CDC_DVD_RAM | CDC_RAM
};

static int find_capability(const char *type)
{
	struct capability_entry *entry;

	for(entry = capability_table; entry->type; ++entry)
		if(!strncmp(entry->type, type, 4))
			break;
	return entry->capability;
}

static int viocd_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	struct gendisk *gendisk;
	int deviceno;
	struct disk_info *d;
	struct cdrom_device_info *c;
	struct request_queue *q;
	struct device_node *node = vdev->dev.archdata.of_node;

	deviceno = vdev->unit_address;
	if (deviceno > VIOCD_MAX_CD)
		return -ENODEV;
	if (!node)
		return -ENODEV;

	if (deviceno >= viocd_numdev)
		viocd_numdev = deviceno + 1;

	d = &viocd_diskinfo[deviceno];
	d->rsrcname = of_get_property(node, "linux,vio_rsrcname", NULL);
	d->type = of_get_property(node, "linux,vio_type", NULL);
	d->model = of_get_property(node, "linux,vio_model", NULL);

	c = &d->viocd_info;

	c->ops = &viocd_dops;
	c->speed = 4;
	c->capacity = 1;
	c->handle = d;
	c->mask = ~find_capability(d->type);
	sprintf(c->name, VIOCD_DEVICE "%c", 'a' + deviceno);

	if (register_cdrom(c) != 0) {
		printk(VIOCD_KERN_WARNING "Cannot register viocd CD-ROM %s!\n",
				c->name);
		goto out;
	}
	printk(VIOCD_KERN_INFO "cd %s is iSeries resource %10.10s "
			"type %4.4s, model %3.3s\n",
			c->name, d->rsrcname, d->type, d->model);
	q = blk_init_queue(do_viocd_request, &viocd_reqlock);
	if (q == NULL) {
		printk(VIOCD_KERN_WARNING "Cannot allocate queue for %s!\n",
				c->name);
		goto out_unregister_cdrom;
	}
	gendisk = alloc_disk(1);
	if (gendisk == NULL) {
		printk(VIOCD_KERN_WARNING "Cannot create gendisk for %s!\n",
				c->name);
		goto out_cleanup_queue;
	}
	gendisk->major = VIOCD_MAJOR;
	gendisk->first_minor = deviceno;
	strncpy(gendisk->disk_name, c->name,
			sizeof(gendisk->disk_name));
	blk_queue_max_hw_segments(q, 1);
	blk_queue_max_phys_segments(q, 1);
	blk_queue_max_sectors(q, 4096 / 512);
	gendisk->queue = q;
	gendisk->fops = &viocd_fops;
	gendisk->flags = GENHD_FL_CD|GENHD_FL_REMOVABLE;
	set_capacity(gendisk, 0);
	gendisk->private_data = d;
	d->viocd_disk = gendisk;
	d->dev = &vdev->dev;
	gendisk->driverfs_dev = d->dev;
	add_disk(gendisk);
	return 0;

out_cleanup_queue:
	blk_cleanup_queue(q);
out_unregister_cdrom:
	unregister_cdrom(c);
out:
	return -ENODEV;
}

static int viocd_remove(struct vio_dev *vdev)
{
	struct disk_info *d = &viocd_diskinfo[vdev->unit_address];

	if (unregister_cdrom(&d->viocd_info) != 0)
		printk(VIOCD_KERN_WARNING
				"Cannot unregister viocd CD-ROM %s!\n",
				d->viocd_info.name);
	del_gendisk(d->viocd_disk);
	blk_cleanup_queue(d->viocd_disk->queue);
	put_disk(d->viocd_disk);
	return 0;
}

/**
 * viocd_device_table: Used by vio.c to match devices that we
 * support.
 */
static struct vio_device_id viocd_device_table[] __devinitdata = {
	{ "block", "IBM,iSeries-viocd" },
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, viocd_device_table);

static struct vio_driver viocd_driver = {
	.id_table = viocd_device_table,
	.probe = viocd_probe,
	.remove = viocd_remove,
	.driver = {
		.name = "viocd",
		.owner = THIS_MODULE,
	}
};

static int __init viocd_init(void)
{
	struct proc_dir_entry *e;
	int ret = 0;

	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		return -ENODEV;

	if (viopath_hostLp == HvLpIndexInvalid) {
		vio_set_hostlp();
		/* If we don't have a host, bail out */
		if (viopath_hostLp == HvLpIndexInvalid)
			return -ENODEV;
	}

	printk(VIOCD_KERN_INFO "vers " VIOCD_VERS ", hosting partition %d\n",
			viopath_hostLp);

	if (register_blkdev(VIOCD_MAJOR, VIOCD_DEVICE) != 0) {
		printk(VIOCD_KERN_WARNING "Unable to get major %d for %s\n",
				VIOCD_MAJOR, VIOCD_DEVICE);
		return -EIO;
	}

	ret = viopath_open(viopath_hostLp, viomajorsubtype_cdio,
			MAX_CD_REQ + 2);
	if (ret) {
		printk(VIOCD_KERN_WARNING
				"error opening path to host partition %d\n",
				viopath_hostLp);
		goto out_unregister;
	}

	/* Initialize our request handler */
	vio_setHandler(viomajorsubtype_cdio, vio_handle_cd_event);

	spin_lock_init(&viocd_reqlock);

	ret = vio_register_driver(&viocd_driver);
	if (ret)
		goto out_free_info;

	e = create_proc_entry("iSeries/viocd", S_IFREG|S_IRUGO, NULL);
	if (e) {
		e->owner = THIS_MODULE;
		e->proc_fops = &proc_viocd_operations;
	}

	return 0;

out_free_info:
	vio_clearHandler(viomajorsubtype_cdio);
	viopath_close(viopath_hostLp, viomajorsubtype_cdio, MAX_CD_REQ + 2);
out_unregister:
	unregister_blkdev(VIOCD_MAJOR, VIOCD_DEVICE);
	return ret;
}

static void __exit viocd_exit(void)
{
	remove_proc_entry("iSeries/viocd", NULL);
	vio_unregister_driver(&viocd_driver);
	viopath_close(viopath_hostLp, viomajorsubtype_cdio, MAX_CD_REQ + 2);
	vio_clearHandler(viomajorsubtype_cdio);
	unregister_blkdev(VIOCD_MAJOR, VIOCD_DEVICE);
}

module_init(viocd_init);
module_exit(viocd_exit);
MODULE_LICENSE("GPL");
