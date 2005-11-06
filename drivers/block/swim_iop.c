/*
 * Driver for the SWIM (Super Woz Integrated Machine) IOP
 * floppy controller on the Macintosh IIfx and Quadra 900/950
 *
 * Written by Joshua M. Thompson (funaho@jurai.org)
 * based on the SWIM3 driver (c) 1996 by Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * 1999-06-12 (jmt) - Initial implementation.
 */

/*
 * -------------------
 * Theory of Operation
 * -------------------
 *
 * Since the SWIM IOP is message-driven we implement a simple request queue
 * system.  One outstanding request may be queued at any given time (this is
 * an IOP limitation); only when that request has completed can a new request
 * be sent.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/fd.h>
#include <linux/ioctl.h>
#include <linux/blkdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mac_iop.h>
#include <asm/swim_iop.h>

#define DRIVER_VERSION "Version 0.1 (1999-06-12)"

#define MAX_FLOPPIES	4

enum swim_state {
	idle,
	available,
	revalidating,
	transferring,
	ejecting
};

struct floppy_state {
	enum swim_state state;
	int	drive_num;	/* device number */
	int	secpercyl;	/* disk geometry information */
	int	secpertrack;
	int	total_secs;
	int	write_prot;	/* 1 if write-protected, 0 if not, -1 dunno */
	int	ref_count;
	struct timer_list timeout;
	int	ejected;
	struct wait_queue *wait;
	int	wanted;
	int	timeout_pending;
};

struct swim_iop_req {
	int	sent;
	int	complete;
	__u8	command[32];
	struct floppy_state *fs;
	void	(*done)(struct swim_iop_req *);
};

static struct swim_iop_req *current_req;
static int floppy_count;

static struct floppy_state floppy_states[MAX_FLOPPIES];
static DEFINE_SPINLOCK(swim_iop_lock);

#define CURRENT elv_next_request(swim_queue)

static char *drive_names[7] = {
	"not installed",	/* DRV_NONE    */
	"unknown (1)",		/* DRV_UNKNOWN */
	"a 400K drive",		/* DRV_400K    */
	"an 800K drive"		/* DRV_800K    */
	"unknown (4)",		/* ????        */
	"an FDHD",		/* DRV_FDHD    */
	"unknown (6)",		/* ????        */
	"an Apple HD20"		/* DRV_HD20    */
};

int swimiop_init(void);
static void swimiop_init_request(struct swim_iop_req *);
static int swimiop_send_request(struct swim_iop_req *);
static void swimiop_receive(struct iop_msg *, struct pt_regs *);
static void swimiop_status_update(int, struct swim_drvstatus *);
static int swimiop_eject(struct floppy_state *fs);

static int floppy_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long param);
static int floppy_open(struct inode *inode, struct file *filp);
static int floppy_release(struct inode *inode, struct file *filp);
static int floppy_check_change(struct gendisk *disk);
static int floppy_revalidate(struct gendisk *disk);
static int grab_drive(struct floppy_state *fs, enum swim_state state,
		      int interruptible);
static void release_drive(struct floppy_state *fs);
static void set_timeout(struct floppy_state *fs, int nticks,
			void (*proc)(unsigned long));
static void fd_request_timeout(unsigned long);
static void do_fd_request(request_queue_t * q);
static void start_request(struct floppy_state *fs);

static struct block_device_operations floppy_fops = {
	.open		= floppy_open,
	.release	= floppy_release,
	.ioctl		= floppy_ioctl,
	.media_changed	= floppy_check_change,
	.revalidate_disk= floppy_revalidate,
};

static struct request_queue *swim_queue;
/*
 * SWIM IOP initialization
 */

int swimiop_init(void)
{
	volatile struct swim_iop_req req;
	struct swimcmd_status *cmd = (struct swimcmd_status *) &req.command[0];
	struct swim_drvstatus *ds = &cmd->status;
	struct floppy_state *fs;
	int i;

	current_req = NULL;
	floppy_count = 0;

	if (!iop_ism_present)
		return -ENODEV;

	if (register_blkdev(FLOPPY_MAJOR, "fd"))
		return -EBUSY;

	swim_queue = blk_init_queue(do_fd_request, &swim_iop_lock);
	if (!swim_queue) {
		unregister_blkdev(FLOPPY_MAJOR, "fd");
		return -ENOMEM;
	}

	printk("SWIM-IOP: %s by Joshua M. Thompson (funaho@jurai.org)\n",
		DRIVER_VERSION);

	if (iop_listen(SWIM_IOP, SWIM_CHAN, swimiop_receive, "SWIM") != 0) {
		printk(KERN_ERR "SWIM-IOP: IOP channel already in use; can't initialize.\n");
		unregister_blkdev(FLOPPY_MAJOR, "fd");
		blk_cleanup_queue(swim_queue);
		return -EBUSY;
	}

	printk(KERN_ERR "SWIM_IOP: probing for installed drives.\n");

	for (i = 0 ; i < MAX_FLOPPIES ; i++) {
		memset(&floppy_states[i], 0, sizeof(struct floppy_state));
		fs = &floppy_states[floppy_count];

		swimiop_init_request(&req);
		cmd->code = CMD_STATUS;
		cmd->drive_num = i + 1;
		if (swimiop_send_request(&req) != 0) continue;
		while (!req.complete);
		if (cmd->error != 0) {
			printk(KERN_ERR "SWIM-IOP: probe on drive %d returned error %d\n", i, (uint) cmd->error);
			continue;
		}
		if (ds->installed != 0x01) continue;
		printk("SWIM-IOP: drive %d is %s (%s, %s, %s, %s)\n", i,
			drive_names[ds->info.type],
			ds->info.external? "ext" : "int",
			ds->info.scsi? "scsi" : "floppy",
			ds->info.fixed? "fixed" : "removable",
			ds->info.secondary? "secondary" : "primary");
		swimiop_status_update(floppy_count, ds);
		fs->state = idle;

		init_timer(&fs->timeout);
		floppy_count++;
	}
	printk("SWIM-IOP: detected %d installed drives.\n", floppy_count);

	for (i = 0; i < floppy_count; i++) {
		struct gendisk *disk = alloc_disk(1);
		if (!disk)
			continue;
		disk->major = FLOPPY_MAJOR;
		disk->first_minor = i;
		disk->fops = &floppy_fops;
		sprintf(disk->disk_name, "fd%d", i);
		disk->private_data = &floppy_states[i];
		disk->queue = swim_queue;
		set_capacity(disk, 2880 * 2);
		add_disk(disk);
	}

	return 0;
}

static void swimiop_init_request(struct swim_iop_req *req)
{
	req->sent = 0;
	req->complete = 0;
	req->done = NULL;
}

static int swimiop_send_request(struct swim_iop_req *req)
{
	unsigned long flags;
	int err;

	/* It's doubtful an interrupt routine would try to send */
	/* a SWIM request, but I'd rather play it safe here.    */

	local_irq_save(flags);

	if (current_req != NULL) {
		local_irq_restore(flags);
		return -ENOMEM;
	}

	current_req = req;

	/* Interrupts should be back on for iop_send_message() */

	local_irq_restore(flags);

	err = iop_send_message(SWIM_IOP, SWIM_CHAN, (void *) req,
				sizeof(req->command), (__u8 *) &req->command[0],
				swimiop_receive);

	/* No race condition here; we own current_req at this point */

	if (err) {
		current_req = NULL;
	} else {
		req->sent = 1;
	}
	return err;
}

/*
 * Receive a SWIM message from the IOP.
 *
 * This will be called in two cases:
 *
 * 1. A message has been successfully sent to the IOP.
 * 2. An unsolicited message was received from the IOP.
 */

void swimiop_receive(struct iop_msg *msg, struct pt_regs *regs)
{
	struct swim_iop_req *req;
	struct swimmsg_status *sm;
	struct swim_drvstatus *ds;

	req = current_req;

	switch(msg->status) {
		case IOP_MSGSTATUS_COMPLETE:
			memcpy(&req->command[0], &msg->reply[0], sizeof(req->command));
			req->complete = 1;
			if (req->done) (*req->done)(req);
			current_req = NULL;
			break;
		case IOP_MSGSTATUS_UNSOL:
			sm = (struct swimmsg_status *) &msg->message[0];
			ds = &sm->status;
			swimiop_status_update(sm->drive_num, ds);
			iop_complete_message(msg);
			break;
	}
}

static void swimiop_status_update(int drive_num, struct swim_drvstatus *ds)
{
	struct floppy_state *fs = &floppy_states[drive_num];

	fs->write_prot = (ds->write_prot == 0x80);
	if ((ds->disk_in_drive != 0x01) && (ds->disk_in_drive != 0x02)) {
		fs->ejected = 1;
	} else {
		fs->ejected = 0;
	}
	switch(ds->info.type) {
		case DRV_400K:
			fs->secpercyl = 10;
			fs->secpertrack = 10;
			fs->total_secs = 800;
			break;
		case DRV_800K:
			fs->secpercyl = 20;
			fs->secpertrack = 10;
			fs->total_secs = 1600;
			break;
		case DRV_FDHD:
			fs->secpercyl = 36;
			fs->secpertrack = 18;
			fs->total_secs = 2880;
			break;
		default:
			fs->secpercyl = 0;
			fs->secpertrack = 0;
			fs->total_secs = 0;
			break;
	}
}

static int swimiop_eject(struct floppy_state *fs)
{
	int err, n;
	struct swim_iop_req req;
	struct swimcmd_eject *cmd = (struct swimcmd_eject *) &req.command[0];

	err = grab_drive(fs, ejecting, 1);
	if (err) return err;

	swimiop_init_request(&req);
	cmd->code = CMD_EJECT;
	cmd->drive_num = fs->drive_num;
	err = swimiop_send_request(&req);
	if (err) {
		release_drive(fs);
		return err;
	}
	for (n = 2*HZ; n > 0; --n) {
		if (req.complete) break;
		if (signal_pending(current)) {
			err = -EINTR;
			break;
		}
		schedule_timeout_interruptible(1);
	}
	release_drive(fs);
	return cmd->error;
}

static struct floppy_struct floppy_type =
	{ 2880,18,2,80,0,0x1B,0x00,0xCF,0x6C,NULL };	/*  7 1.44MB 3.5"   */

static int floppy_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long param)
{
	struct floppy_state *fs = inode->i_bdev->bd_disk->private_data;
	int err;

	if ((cmd & 0x80) && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case FDEJECT:
		if (fs->ref_count != 1)
			return -EBUSY;
		err = swimiop_eject(fs);
		return err;
	case FDGETPRM:
	        if (copy_to_user((void *) param, (void *) &floppy_type,
				 sizeof(struct floppy_struct)))
			return -EFAULT;
		return 0;
	}
	return -ENOTTY;
}

static int floppy_open(struct inode *inode, struct file *filp)
{
	struct floppy_state *fs = inode->i_bdev->bd_disk->private_data;

	if (fs->ref_count == -1 || filp->f_flags & O_EXCL)
		return -EBUSY;

	if ((filp->f_flags & O_NDELAY) == 0 && (filp->f_mode & 3)) {
		check_disk_change(inode->i_bdev);
		if (fs->ejected)
			return -ENXIO;
	}

	if ((filp->f_mode & 2) && fs->write_prot)
		return -EROFS;

	if (filp->f_flags & O_EXCL)
		fs->ref_count = -1;
	else
		++fs->ref_count;

	return 0;
}

static int floppy_release(struct inode *inode, struct file *filp)
{
	struct floppy_state *fs = inode->i_bdev->bd_disk->private_data;
	if (fs->ref_count > 0)
		fs->ref_count--;
	return 0;
}

static int floppy_check_change(struct gendisk *disk)
{
	struct floppy_state *fs = disk->private_data;
	return fs->ejected;
}

static int floppy_revalidate(struct gendisk *disk)
{
	struct floppy_state *fs = disk->private_data;
	grab_drive(fs, revalidating, 0);
	/* yadda, yadda */
	release_drive(fs);
	return 0;
}

static void floppy_off(unsigned int nr)
{
}

static int grab_drive(struct floppy_state *fs, enum swim_state state,
		      int interruptible)
{
	unsigned long flags;

	local_irq_save(flags);
	if (fs->state != idle) {
		++fs->wanted;
		while (fs->state != available) {
			if (interruptible && signal_pending(current)) {
				--fs->wanted;
				local_irq_restore(flags);
				return -EINTR;
			}
			interruptible_sleep_on(&fs->wait);
		}
		--fs->wanted;
	}
	fs->state = state;
	local_irq_restore(flags);
	return 0;
}

static void release_drive(struct floppy_state *fs)
{
	unsigned long flags;

	local_irq_save(flags);
	fs->state = idle;
	start_request(fs);
	local_irq_restore(flags);
}

static void set_timeout(struct floppy_state *fs, int nticks,
			void (*proc)(unsigned long))
{
	unsigned long flags;

	local_irq_save(flags);
	if (fs->timeout_pending)
		del_timer(&fs->timeout);
	init_timer(&fs->timeout);
	fs->timeout.expires = jiffies + nticks;
	fs->timeout.function = proc;
	fs->timeout.data = (unsigned long) fs;
	add_timer(&fs->timeout);
	fs->timeout_pending = 1;
	local_irq_restore(flags);
}

static void do_fd_request(request_queue_t * q)
{
	int i;

	for (i = 0 ; i < floppy_count ; i++) {
		start_request(&floppy_states[i]);
	}
}

static void fd_request_complete(struct swim_iop_req *req)
{
	struct floppy_state *fs = req->fs;
	struct swimcmd_rw *cmd = (struct swimcmd_rw *) &req->command[0];

	del_timer(&fs->timeout);
	fs->timeout_pending = 0;
	fs->state = idle;
	if (cmd->error) {
		printk(KERN_ERR "SWIM-IOP: error %d on read/write request.\n", cmd->error);
		end_request(CURRENT, 0);
	} else {
		CURRENT->sector += cmd->num_blocks;
		CURRENT->current_nr_sectors -= cmd->num_blocks;
		if (CURRENT->current_nr_sectors <= 0) {
			end_request(CURRENT, 1);
			return;
		}
	}
	start_request(fs);
}

static void fd_request_timeout(unsigned long data)
{
	struct floppy_state *fs = (struct floppy_state *) data;

	fs->timeout_pending = 0;
	end_request(CURRENT, 0);
	fs->state = idle;
}

static void start_request(struct floppy_state *fs)
{
	volatile struct swim_iop_req req;
	struct swimcmd_rw *cmd = (struct swimcmd_rw *) &req.command[0];

	if (fs->state == idle && fs->wanted) {
		fs->state = available;
		wake_up(&fs->wait);
		return;
	}
	while (CURRENT && fs->state == idle) {
		if (CURRENT->bh && !buffer_locked(CURRENT->bh))
			panic("floppy: block not locked");
#if 0
		printk("do_fd_req: dev=%s cmd=%d sec=%ld nr_sec=%ld buf=%p\n",
		       CURRENT->rq_disk->disk_name, CURRENT->cmd,
		       CURRENT->sector, CURRENT->nr_sectors, CURRENT->buffer);
		printk("           rq_status=%d errors=%d current_nr_sectors=%ld\n",
		       CURRENT->rq_status, CURRENT->errors, CURRENT->current_nr_sectors);
#endif

		if (CURRENT->sector < 0 || CURRENT->sector >= fs->total_secs) {
			end_request(CURRENT, 0);
			continue;
		}
		if (CURRENT->current_nr_sectors == 0) {
			end_request(CURRENT, 1);
			continue;
		}
		if (fs->ejected) {
			end_request(CURRENT, 0);
			continue;
		}

		swimiop_init_request(&req);
		req.fs = fs;
		req.done = fd_request_complete;

		if (CURRENT->cmd == WRITE) {
			if (fs->write_prot) {
				end_request(CURRENT, 0);
				continue;
			}
			cmd->code = CMD_WRITE;
		} else {
			cmd->code = CMD_READ;

		}
		cmd->drive_num = fs->drive_num;
		cmd->buffer = CURRENT->buffer;
		cmd->first_block = CURRENT->sector;
		cmd->num_blocks = CURRENT->current_nr_sectors;

		if (swimiop_send_request(&req)) {
			end_request(CURRENT, 0);
			continue;
		}

		set_timeout(fs, HZ*CURRENT->current_nr_sectors,
				fd_request_timeout);

		fs->state = transferring;
	}
}
