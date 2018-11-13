/*
 * Driver for SWIM (Sander Woz Integrated Machine) floppy controller
 *
 * Copyright (C) 2004,2008 Laurent Vivier <Laurent@lvivier.info>
 *
 * based on Alastair Bridgewater SWIM analysis, 2001
 * based on SWIM3 driver (c) Paul Mackerras, 1996
 * based on netBSD IWM driver (c) 1997, 1998 Hauke Fath.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * 2004-08-21 (lv) - Initial implementation
 * 2008-10-30 (lv) - Port to 2.6
 */

#include <linux/module.h>
#include <linux/fd.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/hdreg.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <asm/mac_via.h>

#define CARDNAME "swim"

struct sector_header {
	unsigned char side;
	unsigned char track;
	unsigned char sector;
	unsigned char size;
	unsigned char crc0;
	unsigned char crc1;
} __attribute__((packed));

#define DRIVER_VERSION "Version 0.2 (2008-10-30)"

#define REG(x)	unsigned char x, x ## _pad[0x200 - 1];

struct swim {
	REG(write_data)
	REG(write_mark)
	REG(write_CRC)
	REG(write_parameter)
	REG(write_phase)
	REG(write_setup)
	REG(write_mode0)
	REG(write_mode1)

	REG(read_data)
	REG(read_mark)
	REG(read_error)
	REG(read_parameter)
	REG(read_phase)
	REG(read_setup)
	REG(read_status)
	REG(read_handshake)
} __attribute__((packed));

#define swim_write(base, reg, v) 	out_8(&(base)->write_##reg, (v))
#define swim_read(base, reg)		in_8(&(base)->read_##reg)

/* IWM registers */

struct iwm {
	REG(ph0L)
	REG(ph0H)
	REG(ph1L)
	REG(ph1H)
	REG(ph2L)
	REG(ph2H)
	REG(ph3L)
	REG(ph3H)
	REG(mtrOff)
	REG(mtrOn)
	REG(intDrive)
	REG(extDrive)
	REG(q6L)
	REG(q6H)
	REG(q7L)
	REG(q7H)
} __attribute__((packed));

#define iwm_write(base, reg, v) 	out_8(&(base)->reg, (v))
#define iwm_read(base, reg)		in_8(&(base)->reg)

/* bits in phase register */

#define SEEK_POSITIVE	0x070
#define SEEK_NEGATIVE	0x074
#define STEP		0x071
#define MOTOR_ON	0x072
#define MOTOR_OFF	0x076
#define INDEX		0x073
#define EJECT		0x077
#define SETMFM		0x171
#define SETGCR		0x175

#define RELAX		0x033
#define LSTRB		0x008

#define CA_MASK		0x077

/* Select values for swim_select and swim_readbit */

#define READ_DATA_0	0x074
#define ONEMEG_DRIVE	0x075
#define SINGLE_SIDED	0x076
#define DRIVE_PRESENT	0x077
#define DISK_IN		0x170
#define WRITE_PROT	0x171
#define TRACK_ZERO	0x172
#define TACHO		0x173
#define READ_DATA_1	0x174
#define GCR_MODE	0x175
#define SEEK_COMPLETE	0x176
#define TWOMEG_MEDIA	0x177

/* Bits in handshake register */

#define MARK_BYTE	0x01
#define CRC_ZERO	0x02
#define RDDATA		0x04
#define SENSE		0x08
#define MOTEN		0x10
#define ERROR		0x20
#define DAT2BYTE	0x40
#define DAT1BYTE	0x80

/* bits in setup register */

#define S_INV_WDATA	0x01
#define S_3_5_SELECT	0x02
#define S_GCR		0x04
#define S_FCLK_DIV2	0x08
#define S_ERROR_CORR	0x10
#define S_IBM_DRIVE	0x20
#define S_GCR_WRITE	0x40
#define S_TIMEOUT	0x80

/* bits in mode register */

#define CLFIFO		0x01
#define ENBL1		0x02
#define ENBL2		0x04
#define ACTION		0x08
#define WRITE_MODE	0x10
#define HEDSEL		0x20
#define MOTON		0x80

/*----------------------------------------------------------------------------*/

enum drive_location {
	INTERNAL_DRIVE = 0x02,
	EXTERNAL_DRIVE = 0x04,
};

enum media_type {
	DD_MEDIA,
	HD_MEDIA,
};

struct floppy_state {

	/* physical properties */

	enum drive_location location;	/* internal or external drive */
	int		 head_number;	/* single- or double-sided drive */

	/* media */

	int		 disk_in;
	int		 ejected;
	enum media_type	 type;
	int		 write_protected;

	int		 total_secs;
	int		 secpercyl;
	int		 secpertrack;

	/* in-use information */

	int		track;
	int		ref_count;

	struct gendisk *disk;

	/* parent controller */

	struct swim_priv *swd;
};

enum motor_action {
	OFF,
	ON,
};

enum head {
	LOWER_HEAD = 0,
	UPPER_HEAD = 1,
};

#define FD_MAX_UNIT	2

struct swim_priv {
	struct swim __iomem *base;
	spinlock_t lock;
	int fdc_queue;
	int floppy_count;
	struct floppy_state unit[FD_MAX_UNIT];
};

extern int swim_read_sector_header(struct swim __iomem *base,
				   struct sector_header *header);
extern int swim_read_sector_data(struct swim __iomem *base,
				 unsigned char *data);

static DEFINE_MUTEX(swim_mutex);
static inline void set_swim_mode(struct swim __iomem *base, int enable)
{
	struct iwm __iomem *iwm_base;
	unsigned long flags;

	if (!enable) {
		swim_write(base, mode0, 0xf8);
		return;
	}

	iwm_base = (struct iwm __iomem *)base;
	local_irq_save(flags);

	iwm_read(iwm_base, q7L);
	iwm_read(iwm_base, mtrOff);
	iwm_read(iwm_base, q6H);

	iwm_write(iwm_base, q7H, 0x57);
	iwm_write(iwm_base, q7H, 0x17);
	iwm_write(iwm_base, q7H, 0x57);
	iwm_write(iwm_base, q7H, 0x57);

	local_irq_restore(flags);
}

static inline int get_swim_mode(struct swim __iomem *base)
{
	unsigned long flags;

	local_irq_save(flags);

	swim_write(base, phase, 0xf5);
	if (swim_read(base, phase) != 0xf5)
		goto is_iwm;
	swim_write(base, phase, 0xf6);
	if (swim_read(base, phase) != 0xf6)
		goto is_iwm;
	swim_write(base, phase, 0xf7);
	if (swim_read(base, phase) != 0xf7)
		goto is_iwm;
	local_irq_restore(flags);
	return 1;
is_iwm:
	local_irq_restore(flags);
	return 0;
}

static inline void swim_select(struct swim __iomem *base, int sel)
{
	swim_write(base, phase, RELAX);

	via1_set_head(sel & 0x100);

	swim_write(base, phase, sel & CA_MASK);
}

static inline void swim_action(struct swim __iomem *base, int action)
{
	unsigned long flags;

	local_irq_save(flags);

	swim_select(base, action);
	udelay(1);
	swim_write(base, phase, (LSTRB<<4) | LSTRB);
	udelay(1);
	swim_write(base, phase, (LSTRB<<4) | ((~LSTRB) & 0x0F));
	udelay(1);

	local_irq_restore(flags);
}

static inline int swim_readbit(struct swim __iomem *base, int bit)
{
	int stat;

	swim_select(base, bit);

	udelay(10);

	stat = swim_read(base, handshake);

	return (stat & SENSE) == 0;
}

static inline void swim_drive(struct swim __iomem *base,
			      enum drive_location location)
{
	if (location == INTERNAL_DRIVE) {
		swim_write(base, mode0, EXTERNAL_DRIVE); /* clear drive 1 bit */
		swim_write(base, mode1, INTERNAL_DRIVE); /* set drive 0 bit */
	} else if (location == EXTERNAL_DRIVE) {
		swim_write(base, mode0, INTERNAL_DRIVE); /* clear drive 0 bit */
		swim_write(base, mode1, EXTERNAL_DRIVE); /* set drive 1 bit */
	}
}

static inline void swim_motor(struct swim __iomem *base,
			      enum motor_action action)
{
	if (action == ON) {
		int i;

		swim_action(base, MOTOR_ON);

		for (i = 0; i < 2*HZ; i++) {
			swim_select(base, RELAX);
			if (swim_readbit(base, MOTOR_ON))
				break;
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
	} else if (action == OFF) {
		swim_action(base, MOTOR_OFF);
		swim_select(base, RELAX);
	}
}

static inline void swim_eject(struct swim __iomem *base)
{
	int i;

	swim_action(base, EJECT);

	for (i = 0; i < 2*HZ; i++) {
		swim_select(base, RELAX);
		if (!swim_readbit(base, DISK_IN))
			break;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
	}
	swim_select(base, RELAX);
}

static inline void swim_head(struct swim __iomem *base, enum head head)
{
	/* wait drive is ready */

	if (head == UPPER_HEAD)
		swim_select(base, READ_DATA_1);
	else if (head == LOWER_HEAD)
		swim_select(base, READ_DATA_0);
}

static inline int swim_step(struct swim __iomem *base)
{
	int wait;

	swim_action(base, STEP);

	for (wait = 0; wait < HZ; wait++) {

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);

		swim_select(base, RELAX);
		if (!swim_readbit(base, STEP))
			return 0;
	}
	return -1;
}

static inline int swim_track00(struct swim __iomem *base)
{
	int try;

	swim_action(base, SEEK_NEGATIVE);

	for (try = 0; try < 100; try++) {

		swim_select(base, RELAX);
		if (swim_readbit(base, TRACK_ZERO))
			break;

		if (swim_step(base))
			return -1;
	}

	if (swim_readbit(base, TRACK_ZERO))
		return 0;

	return -1;
}

static inline int swim_seek(struct swim __iomem *base, int step)
{
	if (step == 0)
		return 0;

	if (step < 0) {
		swim_action(base, SEEK_NEGATIVE);
		step = -step;
	} else
		swim_action(base, SEEK_POSITIVE);

	for ( ; step > 0; step--) {
		if (swim_step(base))
			return -1;
	}

	return 0;
}

static inline int swim_track(struct floppy_state *fs,  int track)
{
	struct swim __iomem *base = fs->swd->base;
	int ret;

	ret = swim_seek(base, track - fs->track);

	if (ret == 0)
		fs->track = track;
	else {
		swim_track00(base);
		fs->track = 0;
	}

	return ret;
}

static int floppy_eject(struct floppy_state *fs)
{
	struct swim __iomem *base = fs->swd->base;

	swim_drive(base, fs->location);
	swim_motor(base, OFF);
	swim_eject(base);

	fs->disk_in = 0;
	fs->ejected = 1;

	return 0;
}

static inline int swim_read_sector(struct floppy_state *fs,
				   int side, int track,
				   int sector, unsigned char *buffer)
{
	struct swim __iomem *base = fs->swd->base;
	unsigned long flags;
	struct sector_header header;
	int ret = -1;
	short i;

	swim_track(fs, track);

	swim_write(base, mode1, MOTON);
	swim_head(base, side);
	swim_write(base, mode0, side);

	local_irq_save(flags);
	for (i = 0; i < 36; i++) {
		ret = swim_read_sector_header(base, &header);
		if (!ret && (header.sector == sector)) {
			/* found */

			ret = swim_read_sector_data(base, buffer);
			break;
		}
	}
	local_irq_restore(flags);

	swim_write(base, mode0, MOTON);

	if ((header.side != side)  || (header.track != track) ||
	     (header.sector != sector))
		return 0;

	return ret;
}

static blk_status_t floppy_read_sectors(struct floppy_state *fs,
			       int req_sector, int sectors_nb,
			       unsigned char *buffer)
{
	struct swim __iomem *base = fs->swd->base;
	int ret;
	int side, track, sector;
	int i, try;


	swim_drive(base, fs->location);
	for (i = req_sector; i < req_sector + sectors_nb; i++) {
		int x;
		track = i / fs->secpercyl;
		x = i % fs->secpercyl;
		side = x / fs->secpertrack;
		sector = x % fs->secpertrack + 1;

		try = 5;
		do {
			ret = swim_read_sector(fs, side, track, sector,
						buffer);
			if (try-- == 0)
				return BLK_STS_IOERR;
		} while (ret != 512);

		buffer += ret;
	}

	return 0;
}

static struct request *swim_next_request(struct swim_priv *swd)
{
	struct request_queue *q;
	struct request *rq;
	int old_pos = swd->fdc_queue;

	do {
		q = swd->unit[swd->fdc_queue].disk->queue;
		if (++swd->fdc_queue == swd->floppy_count)
			swd->fdc_queue = 0;
		if (q) {
			rq = blk_fetch_request(q);
			if (rq)
				return rq;
		}
	} while (swd->fdc_queue != old_pos);

	return NULL;
}

static void do_fd_request(struct request_queue *q)
{
	struct swim_priv *swd = q->queuedata;
	struct request *req;
	struct floppy_state *fs;

	req = swim_next_request(swd);
	while (req) {
		blk_status_t err = BLK_STS_IOERR;

		fs = req->rq_disk->private_data;
		if (blk_rq_pos(req) >= fs->total_secs)
			goto done;
		if (!fs->disk_in)
			goto done;
		if (rq_data_dir(req) == WRITE && fs->write_protected)
			goto done;

		switch (rq_data_dir(req)) {
		case WRITE:
			/* NOT IMPLEMENTED */
			break;
		case READ:
			err = floppy_read_sectors(fs, blk_rq_pos(req),
						  blk_rq_cur_sectors(req),
						  bio_data(req->bio));
			break;
		}
	done:
		if (!__blk_end_request_cur(req, err))
			req = swim_next_request(swd);
	}
}

static struct floppy_struct floppy_type[4] = {
	{    0,  0, 0,  0, 0, 0x00, 0x00, 0x00, 0x00, NULL }, /* no testing   */
	{  720,  9, 1, 80, 0, 0x2A, 0x02, 0xDF, 0x50, NULL }, /* 360KB SS 3.5"*/
	{ 1440,  9, 2, 80, 0, 0x2A, 0x02, 0xDF, 0x50, NULL }, /* 720KB 3.5"   */
	{ 2880, 18, 2, 80, 0, 0x1B, 0x00, 0xCF, 0x6C, NULL }, /* 1.44MB 3.5"  */
};

static int get_floppy_geometry(struct floppy_state *fs, int type,
			       struct floppy_struct **g)
{
	if (type >= ARRAY_SIZE(floppy_type))
		return -EINVAL;

	if (type)
		*g = &floppy_type[type];
	else if (fs->type == HD_MEDIA) /* High-Density media */
		*g = &floppy_type[3];
	else if (fs->head_number == 2) /* double-sided */
		*g = &floppy_type[2];
	else
		*g = &floppy_type[1];

	return 0;
}

static void setup_medium(struct floppy_state *fs)
{
	struct swim __iomem *base = fs->swd->base;

	if (swim_readbit(base, DISK_IN)) {
		struct floppy_struct *g;
		fs->disk_in = 1;
		fs->write_protected = swim_readbit(base, WRITE_PROT);

		if (swim_track00(base))
			printk(KERN_ERR
				"SWIM: cannot move floppy head to track 0\n");

		swim_track00(base);

		fs->type = swim_readbit(base, TWOMEG_MEDIA) ?
			HD_MEDIA : DD_MEDIA;
		fs->head_number = swim_readbit(base, SINGLE_SIDED) ? 1 : 2;
		get_floppy_geometry(fs, 0, &g);
		fs->total_secs = g->size;
		fs->secpercyl = g->head * g->sect;
		fs->secpertrack = g->sect;
		fs->track = 0;
	} else {
		fs->disk_in = 0;
	}
}

static int floppy_open(struct block_device *bdev, fmode_t mode)
{
	struct floppy_state *fs = bdev->bd_disk->private_data;
	struct swim __iomem *base = fs->swd->base;
	int err;

	if (fs->ref_count == -1 || (fs->ref_count && mode & FMODE_EXCL))
		return -EBUSY;

	if (mode & FMODE_EXCL)
		fs->ref_count = -1;
	else
		fs->ref_count++;

	swim_write(base, setup, S_IBM_DRIVE  | S_FCLK_DIV2);
	udelay(10);
	swim_drive(base, fs->location);
	swim_motor(base, ON);
	swim_action(base, SETMFM);
	if (fs->ejected)
		setup_medium(fs);
	if (!fs->disk_in) {
		err = -ENXIO;
		goto out;
	}

	set_capacity(fs->disk, fs->total_secs);

	if (mode & FMODE_NDELAY)
		return 0;

	if (mode & (FMODE_READ|FMODE_WRITE)) {
		check_disk_change(bdev);
		if ((mode & FMODE_WRITE) && fs->write_protected) {
			err = -EROFS;
			goto out;
		}
	}
	return 0;
out:
	if (fs->ref_count < 0)
		fs->ref_count = 0;
	else if (fs->ref_count > 0)
		--fs->ref_count;

	if (fs->ref_count == 0)
		swim_motor(base, OFF);
	return err;
}

static int floppy_unlocked_open(struct block_device *bdev, fmode_t mode)
{
	int ret;

	mutex_lock(&swim_mutex);
	ret = floppy_open(bdev, mode);
	mutex_unlock(&swim_mutex);

	return ret;
}

static void floppy_release(struct gendisk *disk, fmode_t mode)
{
	struct floppy_state *fs = disk->private_data;
	struct swim __iomem *base = fs->swd->base;

	mutex_lock(&swim_mutex);
	if (fs->ref_count < 0)
		fs->ref_count = 0;
	else if (fs->ref_count > 0)
		--fs->ref_count;

	if (fs->ref_count == 0)
		swim_motor(base, OFF);
	mutex_unlock(&swim_mutex);
}

static int floppy_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long param)
{
	struct floppy_state *fs = bdev->bd_disk->private_data;
	int err;

	if ((cmd & 0x80) && !capable(CAP_SYS_ADMIN))
			return -EPERM;

	switch (cmd) {
	case FDEJECT:
		if (fs->ref_count != 1)
			return -EBUSY;
		mutex_lock(&swim_mutex);
		err = floppy_eject(fs);
		mutex_unlock(&swim_mutex);
		return err;

	case FDGETPRM:
		if (copy_to_user((void __user *) param, (void *) &floppy_type,
				 sizeof(struct floppy_struct)))
			return -EFAULT;
		return 0;
	}
	return -ENOTTY;
}

static int floppy_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct floppy_state *fs = bdev->bd_disk->private_data;
	struct floppy_struct *g;
	int ret;

	ret = get_floppy_geometry(fs, 0, &g);
	if (ret)
		return ret;

	geo->heads = g->head;
	geo->sectors = g->sect;
	geo->cylinders = g->track;

	return 0;
}

static unsigned int floppy_check_events(struct gendisk *disk,
					unsigned int clearing)
{
	struct floppy_state *fs = disk->private_data;

	return fs->ejected ? DISK_EVENT_MEDIA_CHANGE : 0;
}

static int floppy_revalidate(struct gendisk *disk)
{
	struct floppy_state *fs = disk->private_data;
	struct swim __iomem *base = fs->swd->base;

	swim_drive(base, fs->location);

	if (fs->ejected)
		setup_medium(fs);

	if (!fs->disk_in)
		swim_motor(base, OFF);
	else
		fs->ejected = 0;

	return !fs->disk_in;
}

static const struct block_device_operations floppy_fops = {
	.owner		 = THIS_MODULE,
	.open		 = floppy_unlocked_open,
	.release	 = floppy_release,
	.ioctl		 = floppy_ioctl,
	.getgeo		 = floppy_getgeo,
	.check_events	 = floppy_check_events,
	.revalidate_disk = floppy_revalidate,
};

static struct kobject *floppy_find(dev_t dev, int *part, void *data)
{
	struct swim_priv *swd = data;
	int drive = (*part & 3);

	if (drive >= swd->floppy_count)
		return NULL;

	*part = 0;
	return get_disk(swd->unit[drive].disk);
}

static int swim_add_floppy(struct swim_priv *swd, enum drive_location location)
{
	struct floppy_state *fs = &swd->unit[swd->floppy_count];
	struct swim __iomem *base = swd->base;

	fs->location = location;

	swim_drive(base, location);

	swim_motor(base, OFF);

	fs->type = HD_MEDIA;
	fs->head_number = 2;

	fs->ref_count = 0;
	fs->ejected = 1;

	swd->floppy_count++;

	return 0;
}

static int swim_floppy_init(struct swim_priv *swd)
{
	int err;
	int drive;
	struct swim __iomem *base = swd->base;

	/* scan floppy drives */

	swim_drive(base, INTERNAL_DRIVE);
	if (swim_readbit(base, DRIVE_PRESENT) &&
	    !swim_readbit(base, ONEMEG_DRIVE))
		swim_add_floppy(swd, INTERNAL_DRIVE);
	swim_drive(base, EXTERNAL_DRIVE);
	if (swim_readbit(base, DRIVE_PRESENT) &&
	    !swim_readbit(base, ONEMEG_DRIVE))
		swim_add_floppy(swd, EXTERNAL_DRIVE);

	/* register floppy drives */

	err = register_blkdev(FLOPPY_MAJOR, "fd");
	if (err) {
		printk(KERN_ERR "Unable to get major %d for SWIM floppy\n",
		       FLOPPY_MAJOR);
		return -EBUSY;
	}

	spin_lock_init(&swd->lock);

	for (drive = 0; drive < swd->floppy_count; drive++) {
		swd->unit[drive].disk = alloc_disk(1);
		if (swd->unit[drive].disk == NULL) {
			err = -ENOMEM;
			goto exit_put_disks;
		}
		swd->unit[drive].disk->queue = blk_init_queue(do_fd_request,
							      &swd->lock);
		if (!swd->unit[drive].disk->queue) {
			err = -ENOMEM;
			goto exit_put_disks;
		}
		blk_queue_bounce_limit(swd->unit[drive].disk->queue,
				BLK_BOUNCE_HIGH);
		swd->unit[drive].disk->queue->queuedata = swd;
		swd->unit[drive].swd = swd;
	}

	for (drive = 0; drive < swd->floppy_count; drive++) {
		swd->unit[drive].disk->flags = GENHD_FL_REMOVABLE;
		swd->unit[drive].disk->major = FLOPPY_MAJOR;
		swd->unit[drive].disk->first_minor = drive;
		sprintf(swd->unit[drive].disk->disk_name, "fd%d", drive);
		swd->unit[drive].disk->fops = &floppy_fops;
		swd->unit[drive].disk->private_data = &swd->unit[drive];
		set_capacity(swd->unit[drive].disk, 2880);
		add_disk(swd->unit[drive].disk);
	}

	blk_register_region(MKDEV(FLOPPY_MAJOR, 0), 256, THIS_MODULE,
			    floppy_find, NULL, swd);

	return 0;

exit_put_disks:
	unregister_blkdev(FLOPPY_MAJOR, "fd");
	do {
		struct gendisk *disk = swd->unit[drive].disk;

		if (disk) {
			if (disk->queue) {
				blk_cleanup_queue(disk->queue);
				disk->queue = NULL;
			}
			put_disk(disk);
		}
	} while (drive--);
	return err;
}

static int swim_probe(struct platform_device *dev)
{
	struct resource *res;
	struct swim __iomem *swim_base;
	struct swim_priv *swd;
	int ret;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto out;
	}

	if (!request_mem_region(res->start, resource_size(res), CARDNAME)) {
		ret = -EBUSY;
		goto out;
	}

	swim_base = (struct swim __iomem *)res->start;
	if (!swim_base) {
		ret = -ENOMEM;
		goto out_release_io;
	}

	/* probe device */

	set_swim_mode(swim_base, 1);
	if (!get_swim_mode(swim_base)) {
		printk(KERN_INFO "SWIM device not found !\n");
		ret = -ENODEV;
		goto out_release_io;
	}

	/* set platform driver data */

	swd = kzalloc(sizeof(struct swim_priv), GFP_KERNEL);
	if (!swd) {
		ret = -ENOMEM;
		goto out_release_io;
	}
	platform_set_drvdata(dev, swd);

	swd->base = swim_base;

	ret = swim_floppy_init(swd);
	if (ret)
		goto out_kfree;

	return 0;

out_kfree:
	kfree(swd);
out_release_io:
	release_mem_region(res->start, resource_size(res));
out:
	return ret;
}

static int swim_remove(struct platform_device *dev)
{
	struct swim_priv *swd = platform_get_drvdata(dev);
	int drive;
	struct resource *res;

	blk_unregister_region(MKDEV(FLOPPY_MAJOR, 0), 256);

	for (drive = 0; drive < swd->floppy_count; drive++) {
		del_gendisk(swd->unit[drive].disk);
		blk_cleanup_queue(swd->unit[drive].disk->queue);
		put_disk(swd->unit[drive].disk);
	}

	unregister_blkdev(FLOPPY_MAJOR, "fd");

	/* eject floppies */

	for (drive = 0; drive < swd->floppy_count; drive++)
		floppy_eject(&swd->unit[drive]);

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));

	kfree(swd);

	return 0;
}

static struct platform_driver swim_driver = {
	.probe  = swim_probe,
	.remove = swim_remove,
	.driver   = {
		.name	= CARDNAME,
	},
};

static int __init swim_init(void)
{
	printk(KERN_INFO "SWIM floppy driver %s\n", DRIVER_VERSION);

	return platform_driver_register(&swim_driver);
}
module_init(swim_init);

static void __exit swim_exit(void)
{
	platform_driver_unregister(&swim_driver);
}
module_exit(swim_exit);

MODULE_DESCRIPTION("Driver for SWIM floppy controller");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Laurent Vivier <laurent@lvivier.info>");
MODULE_ALIAS_BLOCKDEV_MAJOR(FLOPPY_MAJOR);
