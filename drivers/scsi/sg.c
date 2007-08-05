/*
 *  History:
 *  Started: Aug 9 by Lawrence Foard (entropy@world.std.com),
 *           to allow user process control of SCSI devices.
 *  Development Sponsored by Killy Corp. NY NY
 *
 * Original driver (sg.c):
 *        Copyright (C) 1992 Lawrence Foard
 * Version 2 and 3 extensions to driver:
 *        Copyright (C) 1998 - 2005 Douglas Gilbert
 *
 *  Modified  19-JAN-1998  Richard Gooch <rgooch@atnf.csiro.au>  Devfs support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 */

static int sg_version_num = 30534;	/* 2 digits for each component */
#define SG_VERSION_STR "3.5.34"

/*
 *  D. P. Gilbert (dgilbert@interlog.com, dougg@triode.net.au), notes:
 *      - scsi logging is available via SCSI_LOG_TIMEOUT macros. First
 *        the kernel/module needs to be built with CONFIG_SCSI_LOGGING
 *        (otherwise the macros compile to empty statements).
 *
 */
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/mtio.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>

#include "scsi.h"
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>

#include "scsi_logging.h"

#ifdef CONFIG_SCSI_PROC_FS
#include <linux/proc_fs.h>
static char *sg_version_date = "20061027";

static int sg_proc_init(void);
static void sg_proc_cleanup(void);
#endif

#define SG_ALLOW_DIO_DEF 0
#define SG_ALLOW_DIO_CODE /* compile out by commenting this define */

#define SG_MAX_DEVS 32768

/*
 * Suppose you want to calculate the formula muldiv(x,m,d)=int(x * m / d)
 * Then when using 32 bit integers x * m may overflow during the calculation.
 * Replacing muldiv(x) by muldiv(x)=((x % d) * m) / d + int(x / d) * m
 * calculates the same, but prevents the overflow when both m and d
 * are "small" numbers (like HZ and USER_HZ).
 * Of course an overflow is inavoidable if the result of muldiv doesn't fit
 * in 32 bits.
 */
#define MULDIV(X,MUL,DIV) ((((X % DIV) * MUL) / DIV) + ((X / DIV) * MUL))

#define SG_DEFAULT_TIMEOUT MULDIV(SG_DEFAULT_TIMEOUT_USER, HZ, USER_HZ)

int sg_big_buff = SG_DEF_RESERVED_SIZE;
/* N.B. This variable is readable and writeable via
   /proc/scsi/sg/def_reserved_size . Each time sg_open() is called a buffer
   of this size (or less if there is not enough memory) will be reserved
   for use by this file descriptor. [Deprecated usage: this variable is also
   readable via /proc/sys/kernel/sg-big-buff if the sg driver is built into
   the kernel (i.e. it is not a module).] */
static int def_reserved_size = -1;	/* picks up init parameter */
static int sg_allow_dio = SG_ALLOW_DIO_DEF;

static int scatter_elem_sz = SG_SCATTER_SZ;
static int scatter_elem_sz_prev = SG_SCATTER_SZ;

#define SG_SECTOR_SZ 512
#define SG_SECTOR_MSK (SG_SECTOR_SZ - 1)

static int sg_add(struct class_device *, struct class_interface *);
static void sg_remove(struct class_device *, struct class_interface *);

static DEFINE_IDR(sg_index_idr);
static DEFINE_RWLOCK(sg_index_lock);	/* Also used to lock
							   file descriptor list for device */

static struct class_interface sg_interface = {
	.add		= sg_add,
	.remove		= sg_remove,
};

typedef struct sg_scatter_hold { /* holding area for scsi scatter gather info */
	unsigned short k_use_sg; /* Count of kernel scatter-gather pieces */
	unsigned sglist_len; /* size of malloc'd scatter-gather list ++ */
	unsigned bufflen;	/* Size of (aggregate) data buffer */
	unsigned b_malloc_len;	/* actual len malloc'ed in buffer */
	struct scatterlist *buffer;/* scatter list */
	char dio_in_use;	/* 0->indirect IO (or mmap), 1->dio */
	unsigned char cmd_opcode; /* first byte of command */
} Sg_scatter_hold;

struct sg_device;		/* forward declarations */
struct sg_fd;

typedef struct sg_request {	/* SG_MAX_QUEUE requests outstanding per file */
	struct sg_request *nextrp;	/* NULL -> tail request (slist) */
	struct sg_fd *parentfp;	/* NULL -> not in use */
	Sg_scatter_hold data;	/* hold buffer, perhaps scatter list */
	sg_io_hdr_t header;	/* scsi command+info, see <scsi/sg.h> */
	unsigned char sense_b[SCSI_SENSE_BUFFERSIZE];
	char res_used;		/* 1 -> using reserve buffer, 0 -> not ... */
	char orphan;		/* 1 -> drop on sight, 0 -> normal */
	char sg_io_owned;	/* 1 -> packet belongs to SG_IO */
	volatile char done;	/* 0->before bh, 1->before read, 2->read */
} Sg_request;

typedef struct sg_fd {		/* holds the state of a file descriptor */
	struct sg_fd *nextfp;	/* NULL when last opened fd on this device */
	struct sg_device *parentdp;	/* owning device */
	wait_queue_head_t read_wait;	/* queue read until command done */
	rwlock_t rq_list_lock;	/* protect access to list in req_arr */
	int timeout;		/* defaults to SG_DEFAULT_TIMEOUT      */
	int timeout_user;	/* defaults to SG_DEFAULT_TIMEOUT_USER */
	Sg_scatter_hold reserve;	/* buffer held for this file descriptor */
	unsigned save_scat_len;	/* original length of trunc. scat. element */
	Sg_request *headrp;	/* head of request slist, NULL->empty */
	struct fasync_struct *async_qp;	/* used by asynchronous notification */
	Sg_request req_arr[SG_MAX_QUEUE];	/* used as singly-linked list */
	char low_dma;		/* as in parent but possibly overridden to 1 */
	char force_packid;	/* 1 -> pack_id input to read(), 0 -> ignored */
	volatile char closed;	/* 1 -> fd closed but request(s) outstanding */
	char cmd_q;		/* 1 -> allow command queuing, 0 -> don't */
	char next_cmd_len;	/* 0 -> automatic (def), >0 -> use on next write() */
	char keep_orphan;	/* 0 -> drop orphan (def), 1 -> keep for read() */
	char mmap_called;	/* 0 -> mmap() never called on this fd */
} Sg_fd;

typedef struct sg_device { /* holds the state of each scsi generic device */
	struct scsi_device *device;
	wait_queue_head_t o_excl_wait;	/* queue open() when O_EXCL in use */
	int sg_tablesize;	/* adapter's max scatter-gather table size */
	u32 index;		/* device index number */
	Sg_fd *headfp;		/* first open fd belonging to this device */
	volatile char detached;	/* 0->attached, 1->detached pending removal */
	volatile char exclude;	/* opened for exclusive access */
	char sgdebug;		/* 0->off, 1->sense, 9->dump dev, 10-> all devs */
	struct gendisk *disk;
	struct cdev * cdev;	/* char_dev [sysfs: /sys/cdev/major/sg<n>] */
} Sg_device;

static int sg_fasync(int fd, struct file *filp, int mode);
/* tasklet or soft irq callback */
static void sg_cmd_done(void *data, char *sense, int result, int resid);
static int sg_start_req(Sg_request * srp);
static void sg_finish_rem_req(Sg_request * srp);
static int sg_build_indirect(Sg_scatter_hold * schp, Sg_fd * sfp, int buff_size);
static int sg_build_sgat(Sg_scatter_hold * schp, const Sg_fd * sfp,
			 int tablesize);
static ssize_t sg_new_read(Sg_fd * sfp, char __user *buf, size_t count,
			   Sg_request * srp);
static ssize_t sg_new_write(Sg_fd * sfp, const char __user *buf, size_t count,
			    int blocking, int read_only, Sg_request ** o_srp);
static int sg_common_write(Sg_fd * sfp, Sg_request * srp,
			   unsigned char *cmnd, int timeout, int blocking);
static int sg_u_iovec(sg_io_hdr_t * hp, int sg_num, int ind,
		      int wr_xf, int *countp, unsigned char __user **up);
static int sg_write_xfer(Sg_request * srp);
static int sg_read_xfer(Sg_request * srp);
static int sg_read_oxfer(Sg_request * srp, char __user *outp, int num_read_xfer);
static void sg_remove_scat(Sg_scatter_hold * schp);
static void sg_build_reserve(Sg_fd * sfp, int req_size);
static void sg_link_reserve(Sg_fd * sfp, Sg_request * srp, int size);
static void sg_unlink_reserve(Sg_fd * sfp, Sg_request * srp);
static struct page *sg_page_malloc(int rqSz, int lowDma, int *retSzp);
static void sg_page_free(struct page *page, int size);
static Sg_fd *sg_add_sfp(Sg_device * sdp, int dev);
static int sg_remove_sfp(Sg_device * sdp, Sg_fd * sfp);
static void __sg_remove_sfp(Sg_device * sdp, Sg_fd * sfp);
static Sg_request *sg_get_rq_mark(Sg_fd * sfp, int pack_id);
static Sg_request *sg_add_request(Sg_fd * sfp);
static int sg_remove_request(Sg_fd * sfp, Sg_request * srp);
static int sg_res_in_use(Sg_fd * sfp);
static int sg_allow_access(unsigned char opcode, char dev_type);
static int sg_build_direct(Sg_request * srp, Sg_fd * sfp, int dxfer_len);
static Sg_device *sg_get_dev(int dev);
#ifdef CONFIG_SCSI_PROC_FS
static int sg_last_dev(void);
#endif

#define SZ_SG_HEADER sizeof(struct sg_header)
#define SZ_SG_IO_HDR sizeof(sg_io_hdr_t)
#define SZ_SG_IOVEC sizeof(sg_iovec_t)
#define SZ_SG_REQ_INFO sizeof(sg_req_info_t)

static int
sg_open(struct inode *inode, struct file *filp)
{
	int dev = iminor(inode);
	int flags = filp->f_flags;
	struct request_queue *q;
	Sg_device *sdp;
	Sg_fd *sfp;
	int res;
	int retval;

	nonseekable_open(inode, filp);
	SCSI_LOG_TIMEOUT(3, printk("sg_open: dev=%d, flags=0x%x\n", dev, flags));
	sdp = sg_get_dev(dev);
	if ((!sdp) || (!sdp->device))
		return -ENXIO;
	if (sdp->detached)
		return -ENODEV;

	/* This driver's module count bumped by fops_get in <linux/fs.h> */
	/* Prevent the device driver from vanishing while we sleep */
	retval = scsi_device_get(sdp->device);
	if (retval)
		return retval;

	if (!((flags & O_NONBLOCK) ||
	      scsi_block_when_processing_errors(sdp->device))) {
		retval = -ENXIO;
		/* we are in error recovery for this device */
		goto error_out;
	}

	if (flags & O_EXCL) {
		if (O_RDONLY == (flags & O_ACCMODE)) {
			retval = -EPERM; /* Can't lock it with read only access */
			goto error_out;
		}
		if (sdp->headfp && (flags & O_NONBLOCK)) {
			retval = -EBUSY;
			goto error_out;
		}
		res = 0;
		__wait_event_interruptible(sdp->o_excl_wait,
			((sdp->headfp || sdp->exclude) ? 0 : (sdp->exclude = 1)), res);
		if (res) {
			retval = res;	/* -ERESTARTSYS because signal hit process */
			goto error_out;
		}
	} else if (sdp->exclude) {	/* some other fd has an exclusive lock on dev */
		if (flags & O_NONBLOCK) {
			retval = -EBUSY;
			goto error_out;
		}
		res = 0;
		__wait_event_interruptible(sdp->o_excl_wait, (!sdp->exclude),
					   res);
		if (res) {
			retval = res;	/* -ERESTARTSYS because signal hit process */
			goto error_out;
		}
	}
	if (sdp->detached) {
		retval = -ENODEV;
		goto error_out;
	}
	if (!sdp->headfp) {	/* no existing opens on this device */
		sdp->sgdebug = 0;
		q = sdp->device->request_queue;
		sdp->sg_tablesize = min(q->max_hw_segments,
					q->max_phys_segments);
	}
	if ((sfp = sg_add_sfp(sdp, dev)))
		filp->private_data = sfp;
	else {
		if (flags & O_EXCL)
			sdp->exclude = 0;	/* undo if error */
		retval = -ENOMEM;
		goto error_out;
	}
	return 0;

      error_out:
	scsi_device_put(sdp->device);
	return retval;
}

/* Following function was formerly called 'sg_close' */
static int
sg_release(struct inode *inode, struct file *filp)
{
	Sg_device *sdp;
	Sg_fd *sfp;

	if ((!(sfp = (Sg_fd *) filp->private_data)) || (!(sdp = sfp->parentdp)))
		return -ENXIO;
	SCSI_LOG_TIMEOUT(3, printk("sg_release: %s\n", sdp->disk->disk_name));
	sg_fasync(-1, filp, 0);	/* remove filp from async notification list */
	if (0 == sg_remove_sfp(sdp, sfp)) {	/* Returns 1 when sdp gone */
		if (!sdp->detached) {
			scsi_device_put(sdp->device);
		}
		sdp->exclude = 0;
		wake_up_interruptible(&sdp->o_excl_wait);
	}
	return 0;
}

static ssize_t
sg_read(struct file *filp, char __user *buf, size_t count, loff_t * ppos)
{
	Sg_device *sdp;
	Sg_fd *sfp;
	Sg_request *srp;
	int req_pack_id = -1;
	sg_io_hdr_t *hp;
	struct sg_header *old_hdr = NULL;
	int retval = 0;

	if ((!(sfp = (Sg_fd *) filp->private_data)) || (!(sdp = sfp->parentdp)))
		return -ENXIO;
	SCSI_LOG_TIMEOUT(3, printk("sg_read: %s, count=%d\n",
				   sdp->disk->disk_name, (int) count));

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;
	if (sfp->force_packid && (count >= SZ_SG_HEADER)) {
		old_hdr = kmalloc(SZ_SG_HEADER, GFP_KERNEL);
		if (!old_hdr)
			return -ENOMEM;
		if (__copy_from_user(old_hdr, buf, SZ_SG_HEADER)) {
			retval = -EFAULT;
			goto free_old_hdr;
		}
		if (old_hdr->reply_len < 0) {
			if (count >= SZ_SG_IO_HDR) {
				sg_io_hdr_t *new_hdr;
				new_hdr = kmalloc(SZ_SG_IO_HDR, GFP_KERNEL);
				if (!new_hdr) {
					retval = -ENOMEM;
					goto free_old_hdr;
				}
				retval =__copy_from_user
				    (new_hdr, buf, SZ_SG_IO_HDR);
				req_pack_id = new_hdr->pack_id;
				kfree(new_hdr);
				if (retval) {
					retval = -EFAULT;
					goto free_old_hdr;
				}
			}
		} else
			req_pack_id = old_hdr->pack_id;
	}
	srp = sg_get_rq_mark(sfp, req_pack_id);
	if (!srp) {		/* now wait on packet to arrive */
		if (sdp->detached) {
			retval = -ENODEV;
			goto free_old_hdr;
		}
		if (filp->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto free_old_hdr;
		}
		while (1) {
			retval = 0; /* following macro beats race condition */
			__wait_event_interruptible(sfp->read_wait,
				(sdp->detached ||
				(srp = sg_get_rq_mark(sfp, req_pack_id))), 
				retval);
			if (sdp->detached) {
				retval = -ENODEV;
				goto free_old_hdr;
			}
			if (0 == retval)
				break;

			/* -ERESTARTSYS as signal hit process */
			goto free_old_hdr;
		}
	}
	if (srp->header.interface_id != '\0') {
		retval = sg_new_read(sfp, buf, count, srp);
		goto free_old_hdr;
	}

	hp = &srp->header;
	if (old_hdr == NULL) {
		old_hdr = kmalloc(SZ_SG_HEADER, GFP_KERNEL);
		if (! old_hdr) {
			retval = -ENOMEM;
			goto free_old_hdr;
		}
	}
	memset(old_hdr, 0, SZ_SG_HEADER);
	old_hdr->reply_len = (int) hp->timeout;
	old_hdr->pack_len = old_hdr->reply_len; /* old, strange behaviour */
	old_hdr->pack_id = hp->pack_id;
	old_hdr->twelve_byte =
	    ((srp->data.cmd_opcode >= 0xc0) && (12 == hp->cmd_len)) ? 1 : 0;
	old_hdr->target_status = hp->masked_status;
	old_hdr->host_status = hp->host_status;
	old_hdr->driver_status = hp->driver_status;
	if ((CHECK_CONDITION & hp->masked_status) ||
	    (DRIVER_SENSE & hp->driver_status))
		memcpy(old_hdr->sense_buffer, srp->sense_b,
		       sizeof (old_hdr->sense_buffer));
	switch (hp->host_status) {
	/* This setup of 'result' is for backward compatibility and is best
	   ignored by the user who should use target, host + driver status */
	case DID_OK:
	case DID_PASSTHROUGH:
	case DID_SOFT_ERROR:
		old_hdr->result = 0;
		break;
	case DID_NO_CONNECT:
	case DID_BUS_BUSY:
	case DID_TIME_OUT:
		old_hdr->result = EBUSY;
		break;
	case DID_BAD_TARGET:
	case DID_ABORT:
	case DID_PARITY:
	case DID_RESET:
	case DID_BAD_INTR:
		old_hdr->result = EIO;
		break;
	case DID_ERROR:
		old_hdr->result = (srp->sense_b[0] == 0 && 
				  hp->masked_status == GOOD) ? 0 : EIO;
		break;
	default:
		old_hdr->result = EIO;
		break;
	}

	/* Now copy the result back to the user buffer.  */
	if (count >= SZ_SG_HEADER) {
		if (__copy_to_user(buf, old_hdr, SZ_SG_HEADER)) {
			retval = -EFAULT;
			goto free_old_hdr;
		}
		buf += SZ_SG_HEADER;
		if (count > old_hdr->reply_len)
			count = old_hdr->reply_len;
		if (count > SZ_SG_HEADER) {
			if (sg_read_oxfer(srp, buf, count - SZ_SG_HEADER)) {
				retval = -EFAULT;
				goto free_old_hdr;
			}
		}
	} else
		count = (old_hdr->result == 0) ? 0 : -EIO;
	sg_finish_rem_req(srp);
	retval = count;
free_old_hdr:
	kfree(old_hdr);
	return retval;
}

static ssize_t
sg_new_read(Sg_fd * sfp, char __user *buf, size_t count, Sg_request * srp)
{
	sg_io_hdr_t *hp = &srp->header;
	int err = 0;
	int len;

	if (count < SZ_SG_IO_HDR) {
		err = -EINVAL;
		goto err_out;
	}
	hp->sb_len_wr = 0;
	if ((hp->mx_sb_len > 0) && hp->sbp) {
		if ((CHECK_CONDITION & hp->masked_status) ||
		    (DRIVER_SENSE & hp->driver_status)) {
			int sb_len = SCSI_SENSE_BUFFERSIZE;
			sb_len = (hp->mx_sb_len > sb_len) ? sb_len : hp->mx_sb_len;
			len = 8 + (int) srp->sense_b[7];	/* Additional sense length field */
			len = (len > sb_len) ? sb_len : len;
			if (copy_to_user(hp->sbp, srp->sense_b, len)) {
				err = -EFAULT;
				goto err_out;
			}
			hp->sb_len_wr = len;
		}
	}
	if (hp->masked_status || hp->host_status || hp->driver_status)
		hp->info |= SG_INFO_CHECK;
	if (copy_to_user(buf, hp, SZ_SG_IO_HDR)) {
		err = -EFAULT;
		goto err_out;
	}
	err = sg_read_xfer(srp);
      err_out:
	sg_finish_rem_req(srp);
	return (0 == err) ? count : err;
}

static ssize_t
sg_write(struct file *filp, const char __user *buf, size_t count, loff_t * ppos)
{
	int mxsize, cmd_size, k;
	int input_size, blocking;
	unsigned char opcode;
	Sg_device *sdp;
	Sg_fd *sfp;
	Sg_request *srp;
	struct sg_header old_hdr;
	sg_io_hdr_t *hp;
	unsigned char cmnd[MAX_COMMAND_SIZE];

	if ((!(sfp = (Sg_fd *) filp->private_data)) || (!(sdp = sfp->parentdp)))
		return -ENXIO;
	SCSI_LOG_TIMEOUT(3, printk("sg_write: %s, count=%d\n",
				   sdp->disk->disk_name, (int) count));
	if (sdp->detached)
		return -ENODEV;
	if (!((filp->f_flags & O_NONBLOCK) ||
	      scsi_block_when_processing_errors(sdp->device)))
		return -ENXIO;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;	/* protects following copy_from_user()s + get_user()s */
	if (count < SZ_SG_HEADER)
		return -EIO;
	if (__copy_from_user(&old_hdr, buf, SZ_SG_HEADER))
		return -EFAULT;
	blocking = !(filp->f_flags & O_NONBLOCK);
	if (old_hdr.reply_len < 0)
		return sg_new_write(sfp, buf, count, blocking, 0, NULL);
	if (count < (SZ_SG_HEADER + 6))
		return -EIO;	/* The minimum scsi command length is 6 bytes. */

	if (!(srp = sg_add_request(sfp))) {
		SCSI_LOG_TIMEOUT(1, printk("sg_write: queue full\n"));
		return -EDOM;
	}
	buf += SZ_SG_HEADER;
	__get_user(opcode, buf);
	if (sfp->next_cmd_len > 0) {
		if (sfp->next_cmd_len > MAX_COMMAND_SIZE) {
			SCSI_LOG_TIMEOUT(1, printk("sg_write: command length too long\n"));
			sfp->next_cmd_len = 0;
			sg_remove_request(sfp, srp);
			return -EIO;
		}
		cmd_size = sfp->next_cmd_len;
		sfp->next_cmd_len = 0;	/* reset so only this write() effected */
	} else {
		cmd_size = COMMAND_SIZE(opcode);	/* based on SCSI command group */
		if ((opcode >= 0xc0) && old_hdr.twelve_byte)
			cmd_size = 12;
	}
	SCSI_LOG_TIMEOUT(4, printk(
		"sg_write:   scsi opcode=0x%02x, cmd_size=%d\n", (int) opcode, cmd_size));
/* Determine buffer size.  */
	input_size = count - cmd_size;
	mxsize = (input_size > old_hdr.reply_len) ? input_size : old_hdr.reply_len;
	mxsize -= SZ_SG_HEADER;
	input_size -= SZ_SG_HEADER;
	if (input_size < 0) {
		sg_remove_request(sfp, srp);
		return -EIO;	/* User did not pass enough bytes for this command. */
	}
	hp = &srp->header;
	hp->interface_id = '\0';	/* indicator of old interface tunnelled */
	hp->cmd_len = (unsigned char) cmd_size;
	hp->iovec_count = 0;
	hp->mx_sb_len = 0;
	if (input_size > 0)
		hp->dxfer_direction = (old_hdr.reply_len > SZ_SG_HEADER) ?
		    SG_DXFER_TO_FROM_DEV : SG_DXFER_TO_DEV;
	else
		hp->dxfer_direction = (mxsize > 0) ? SG_DXFER_FROM_DEV : SG_DXFER_NONE;
	hp->dxfer_len = mxsize;
	hp->dxferp = (char __user *)buf + cmd_size;
	hp->sbp = NULL;
	hp->timeout = old_hdr.reply_len;	/* structure abuse ... */
	hp->flags = input_size;	/* structure abuse ... */
	hp->pack_id = old_hdr.pack_id;
	hp->usr_ptr = NULL;
	if (__copy_from_user(cmnd, buf, cmd_size))
		return -EFAULT;
	/*
	 * SG_DXFER_TO_FROM_DEV is functionally equivalent to SG_DXFER_FROM_DEV,
	 * but is is possible that the app intended SG_DXFER_TO_DEV, because there
	 * is a non-zero input_size, so emit a warning.
	 */
	if (hp->dxfer_direction == SG_DXFER_TO_FROM_DEV)
		if (printk_ratelimit())
			printk(KERN_WARNING
			       "sg_write: data in/out %d/%d bytes for SCSI command 0x%x--"
			       "guessing data in;\n" KERN_WARNING "   "
			       "program %s not setting count and/or reply_len properly\n",
			       old_hdr.reply_len - (int)SZ_SG_HEADER,
			       input_size, (unsigned int) cmnd[0],
			       current->comm);
	k = sg_common_write(sfp, srp, cmnd, sfp->timeout, blocking);
	return (k < 0) ? k : count;
}

static ssize_t
sg_new_write(Sg_fd * sfp, const char __user *buf, size_t count,
	     int blocking, int read_only, Sg_request ** o_srp)
{
	int k;
	Sg_request *srp;
	sg_io_hdr_t *hp;
	unsigned char cmnd[MAX_COMMAND_SIZE];
	int timeout;
	unsigned long ul_timeout;

	if (count < SZ_SG_IO_HDR)
		return -EINVAL;
	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT; /* protects following copy_from_user()s + get_user()s */

	sfp->cmd_q = 1;	/* when sg_io_hdr seen, set command queuing on */
	if (!(srp = sg_add_request(sfp))) {
		SCSI_LOG_TIMEOUT(1, printk("sg_new_write: queue full\n"));
		return -EDOM;
	}
	hp = &srp->header;
	if (__copy_from_user(hp, buf, SZ_SG_IO_HDR)) {
		sg_remove_request(sfp, srp);
		return -EFAULT;
	}
	if (hp->interface_id != 'S') {
		sg_remove_request(sfp, srp);
		return -ENOSYS;
	}
	if (hp->flags & SG_FLAG_MMAP_IO) {
		if (hp->dxfer_len > sfp->reserve.bufflen) {
			sg_remove_request(sfp, srp);
			return -ENOMEM;	/* MMAP_IO size must fit in reserve buffer */
		}
		if (hp->flags & SG_FLAG_DIRECT_IO) {
			sg_remove_request(sfp, srp);
			return -EINVAL;	/* either MMAP_IO or DIRECT_IO (not both) */
		}
		if (sg_res_in_use(sfp)) {
			sg_remove_request(sfp, srp);
			return -EBUSY;	/* reserve buffer already being used */
		}
	}
	ul_timeout = msecs_to_jiffies(srp->header.timeout);
	timeout = (ul_timeout < INT_MAX) ? ul_timeout : INT_MAX;
	if ((!hp->cmdp) || (hp->cmd_len < 6) || (hp->cmd_len > sizeof (cmnd))) {
		sg_remove_request(sfp, srp);
		return -EMSGSIZE;
	}
	if (!access_ok(VERIFY_READ, hp->cmdp, hp->cmd_len)) {
		sg_remove_request(sfp, srp);
		return -EFAULT;	/* protects following copy_from_user()s + get_user()s */
	}
	if (__copy_from_user(cmnd, hp->cmdp, hp->cmd_len)) {
		sg_remove_request(sfp, srp);
		return -EFAULT;
	}
	if (read_only &&
	    (!sg_allow_access(cmnd[0], sfp->parentdp->device->type))) {
		sg_remove_request(sfp, srp);
		return -EPERM;
	}
	k = sg_common_write(sfp, srp, cmnd, timeout, blocking);
	if (k < 0)
		return k;
	if (o_srp)
		*o_srp = srp;
	return count;
}

static int
sg_common_write(Sg_fd * sfp, Sg_request * srp,
		unsigned char *cmnd, int timeout, int blocking)
{
	int k, data_dir;
	Sg_device *sdp = sfp->parentdp;
	sg_io_hdr_t *hp = &srp->header;

	srp->data.cmd_opcode = cmnd[0];	/* hold opcode of command */
	hp->status = 0;
	hp->masked_status = 0;
	hp->msg_status = 0;
	hp->info = 0;
	hp->host_status = 0;
	hp->driver_status = 0;
	hp->resid = 0;
	SCSI_LOG_TIMEOUT(4, printk("sg_common_write:  scsi opcode=0x%02x, cmd_size=%d\n",
			  (int) cmnd[0], (int) hp->cmd_len));

	if ((k = sg_start_req(srp))) {
		SCSI_LOG_TIMEOUT(1, printk("sg_common_write: start_req err=%d\n", k));
		sg_finish_rem_req(srp);
		return k;	/* probably out of space --> ENOMEM */
	}
	if ((k = sg_write_xfer(srp))) {
		SCSI_LOG_TIMEOUT(1, printk("sg_common_write: write_xfer, bad address\n"));
		sg_finish_rem_req(srp);
		return k;
	}
	if (sdp->detached) {
		sg_finish_rem_req(srp);
		return -ENODEV;
	}

	switch (hp->dxfer_direction) {
	case SG_DXFER_TO_FROM_DEV:
	case SG_DXFER_FROM_DEV:
		data_dir = DMA_FROM_DEVICE;
		break;
	case SG_DXFER_TO_DEV:
		data_dir = DMA_TO_DEVICE;
		break;
	case SG_DXFER_UNKNOWN:
		data_dir = DMA_BIDIRECTIONAL;
		break;
	default:
		data_dir = DMA_NONE;
		break;
	}
	hp->duration = jiffies_to_msecs(jiffies);
/* Now send everything of to mid-level. The next time we hear about this
   packet is when sg_cmd_done() is called (i.e. a callback). */
	if (scsi_execute_async(sdp->device, cmnd, hp->cmd_len, data_dir, srp->data.buffer,
				hp->dxfer_len, srp->data.k_use_sg, timeout,
				SG_DEFAULT_RETRIES, srp, sg_cmd_done,
				GFP_ATOMIC)) {
		SCSI_LOG_TIMEOUT(1, printk("sg_common_write: scsi_execute_async failed\n"));
		/*
		 * most likely out of mem, but could also be a bad map
		 */
		sg_finish_rem_req(srp);
		return -ENOMEM;
	} else
		return 0;
}

static int
sg_srp_done(Sg_request *srp, Sg_fd *sfp)
{
	unsigned long iflags;
	int done;

	read_lock_irqsave(&sfp->rq_list_lock, iflags);
	done = srp->done;
	read_unlock_irqrestore(&sfp->rq_list_lock, iflags);
	return done;
}

static int
sg_ioctl(struct inode *inode, struct file *filp,
	 unsigned int cmd_in, unsigned long arg)
{
	void __user *p = (void __user *)arg;
	int __user *ip = p;
	int result, val, read_only;
	Sg_device *sdp;
	Sg_fd *sfp;
	Sg_request *srp;
	unsigned long iflags;

	if ((!(sfp = (Sg_fd *) filp->private_data)) || (!(sdp = sfp->parentdp)))
		return -ENXIO;
	SCSI_LOG_TIMEOUT(3, printk("sg_ioctl: %s, cmd=0x%x\n",
				   sdp->disk->disk_name, (int) cmd_in));
	read_only = (O_RDWR != (filp->f_flags & O_ACCMODE));

	switch (cmd_in) {
	case SG_IO:
		{
			int blocking = 1;	/* ignore O_NONBLOCK flag */

			if (sdp->detached)
				return -ENODEV;
			if (!scsi_block_when_processing_errors(sdp->device))
				return -ENXIO;
			if (!access_ok(VERIFY_WRITE, p, SZ_SG_IO_HDR))
				return -EFAULT;
			result =
			    sg_new_write(sfp, p, SZ_SG_IO_HDR,
					 blocking, read_only, &srp);
			if (result < 0)
				return result;
			srp->sg_io_owned = 1;
			while (1) {
				result = 0;	/* following macro to beat race condition */
				__wait_event_interruptible(sfp->read_wait,
					(sdp->detached || sfp->closed || sg_srp_done(srp, sfp)),
							   result);
				if (sdp->detached)
					return -ENODEV;
				if (sfp->closed)
					return 0;	/* request packet dropped already */
				if (0 == result)
					break;
				srp->orphan = 1;
				return result;	/* -ERESTARTSYS because signal hit process */
			}
			write_lock_irqsave(&sfp->rq_list_lock, iflags);
			srp->done = 2;
			write_unlock_irqrestore(&sfp->rq_list_lock, iflags);
			result = sg_new_read(sfp, p, SZ_SG_IO_HDR, srp);
			return (result < 0) ? result : 0;
		}
	case SG_SET_TIMEOUT:
		result = get_user(val, ip);
		if (result)
			return result;
		if (val < 0)
			return -EIO;
		if (val >= MULDIV (INT_MAX, USER_HZ, HZ))
		    val = MULDIV (INT_MAX, USER_HZ, HZ);
		sfp->timeout_user = val;
		sfp->timeout = MULDIV (val, HZ, USER_HZ);

		return 0;
	case SG_GET_TIMEOUT:	/* N.B. User receives timeout as return value */
				/* strange ..., for backward compatibility */
		return sfp->timeout_user;
	case SG_SET_FORCE_LOW_DMA:
		result = get_user(val, ip);
		if (result)
			return result;
		if (val) {
			sfp->low_dma = 1;
			if ((0 == sfp->low_dma) && (0 == sg_res_in_use(sfp))) {
				val = (int) sfp->reserve.bufflen;
				sg_remove_scat(&sfp->reserve);
				sg_build_reserve(sfp, val);
			}
		} else {
			if (sdp->detached)
				return -ENODEV;
			sfp->low_dma = sdp->device->host->unchecked_isa_dma;
		}
		return 0;
	case SG_GET_LOW_DMA:
		return put_user((int) sfp->low_dma, ip);
	case SG_GET_SCSI_ID:
		if (!access_ok(VERIFY_WRITE, p, sizeof (sg_scsi_id_t)))
			return -EFAULT;
		else {
			sg_scsi_id_t __user *sg_idp = p;

			if (sdp->detached)
				return -ENODEV;
			__put_user((int) sdp->device->host->host_no,
				   &sg_idp->host_no);
			__put_user((int) sdp->device->channel,
				   &sg_idp->channel);
			__put_user((int) sdp->device->id, &sg_idp->scsi_id);
			__put_user((int) sdp->device->lun, &sg_idp->lun);
			__put_user((int) sdp->device->type, &sg_idp->scsi_type);
			__put_user((short) sdp->device->host->cmd_per_lun,
				   &sg_idp->h_cmd_per_lun);
			__put_user((short) sdp->device->queue_depth,
				   &sg_idp->d_queue_depth);
			__put_user(0, &sg_idp->unused[0]);
			__put_user(0, &sg_idp->unused[1]);
			return 0;
		}
	case SG_SET_FORCE_PACK_ID:
		result = get_user(val, ip);
		if (result)
			return result;
		sfp->force_packid = val ? 1 : 0;
		return 0;
	case SG_GET_PACK_ID:
		if (!access_ok(VERIFY_WRITE, ip, sizeof (int)))
			return -EFAULT;
		read_lock_irqsave(&sfp->rq_list_lock, iflags);
		for (srp = sfp->headrp; srp; srp = srp->nextrp) {
			if ((1 == srp->done) && (!srp->sg_io_owned)) {
				read_unlock_irqrestore(&sfp->rq_list_lock,
						       iflags);
				__put_user(srp->header.pack_id, ip);
				return 0;
			}
		}
		read_unlock_irqrestore(&sfp->rq_list_lock, iflags);
		__put_user(-1, ip);
		return 0;
	case SG_GET_NUM_WAITING:
		read_lock_irqsave(&sfp->rq_list_lock, iflags);
		for (val = 0, srp = sfp->headrp; srp; srp = srp->nextrp) {
			if ((1 == srp->done) && (!srp->sg_io_owned))
				++val;
		}
		read_unlock_irqrestore(&sfp->rq_list_lock, iflags);
		return put_user(val, ip);
	case SG_GET_SG_TABLESIZE:
		return put_user(sdp->sg_tablesize, ip);
	case SG_SET_RESERVED_SIZE:
		result = get_user(val, ip);
		if (result)
			return result;
                if (val < 0)
                        return -EINVAL;
		val = min_t(int, val,
				sdp->device->request_queue->max_sectors * 512);
		if (val != sfp->reserve.bufflen) {
			if (sg_res_in_use(sfp) || sfp->mmap_called)
				return -EBUSY;
			sg_remove_scat(&sfp->reserve);
			sg_build_reserve(sfp, val);
		}
		return 0;
	case SG_GET_RESERVED_SIZE:
		val = min_t(int, sfp->reserve.bufflen,
				sdp->device->request_queue->max_sectors * 512);
		return put_user(val, ip);
	case SG_SET_COMMAND_Q:
		result = get_user(val, ip);
		if (result)
			return result;
		sfp->cmd_q = val ? 1 : 0;
		return 0;
	case SG_GET_COMMAND_Q:
		return put_user((int) sfp->cmd_q, ip);
	case SG_SET_KEEP_ORPHAN:
		result = get_user(val, ip);
		if (result)
			return result;
		sfp->keep_orphan = val;
		return 0;
	case SG_GET_KEEP_ORPHAN:
		return put_user((int) sfp->keep_orphan, ip);
	case SG_NEXT_CMD_LEN:
		result = get_user(val, ip);
		if (result)
			return result;
		sfp->next_cmd_len = (val > 0) ? val : 0;
		return 0;
	case SG_GET_VERSION_NUM:
		return put_user(sg_version_num, ip);
	case SG_GET_ACCESS_COUNT:
		/* faked - we don't have a real access count anymore */
		val = (sdp->device ? 1 : 0);
		return put_user(val, ip);
	case SG_GET_REQUEST_TABLE:
		if (!access_ok(VERIFY_WRITE, p, SZ_SG_REQ_INFO * SG_MAX_QUEUE))
			return -EFAULT;
		else {
			sg_req_info_t *rinfo;
			unsigned int ms;

			rinfo = kmalloc(SZ_SG_REQ_INFO * SG_MAX_QUEUE,
								GFP_KERNEL);
			if (!rinfo)
				return -ENOMEM;
			read_lock_irqsave(&sfp->rq_list_lock, iflags);
			for (srp = sfp->headrp, val = 0; val < SG_MAX_QUEUE;
			     ++val, srp = srp ? srp->nextrp : srp) {
				memset(&rinfo[val], 0, SZ_SG_REQ_INFO);
				if (srp) {
					rinfo[val].req_state = srp->done + 1;
					rinfo[val].problem =
					    srp->header.masked_status & 
					    srp->header.host_status & 
					    srp->header.driver_status;
					if (srp->done)
						rinfo[val].duration =
							srp->header.duration;
					else {
						ms = jiffies_to_msecs(jiffies);
						rinfo[val].duration =
						    (ms > srp->header.duration) ?
						    (ms - srp->header.duration) : 0;
					}
					rinfo[val].orphan = srp->orphan;
					rinfo[val].sg_io_owned =
							srp->sg_io_owned;
					rinfo[val].pack_id =
							srp->header.pack_id;
					rinfo[val].usr_ptr =
							srp->header.usr_ptr;
				}
			}
			read_unlock_irqrestore(&sfp->rq_list_lock, iflags);
			result = __copy_to_user(p, rinfo, 
						SZ_SG_REQ_INFO * SG_MAX_QUEUE);
			result = result ? -EFAULT : 0;
			kfree(rinfo);
			return result;
		}
	case SG_EMULATED_HOST:
		if (sdp->detached)
			return -ENODEV;
		return put_user(sdp->device->host->hostt->emulated, ip);
	case SG_SCSI_RESET:
		if (sdp->detached)
			return -ENODEV;
		if (filp->f_flags & O_NONBLOCK) {
			if (scsi_host_in_recovery(sdp->device->host))
				return -EBUSY;
		} else if (!scsi_block_when_processing_errors(sdp->device))
			return -EBUSY;
		result = get_user(val, ip);
		if (result)
			return result;
		if (SG_SCSI_RESET_NOTHING == val)
			return 0;
		switch (val) {
		case SG_SCSI_RESET_DEVICE:
			val = SCSI_TRY_RESET_DEVICE;
			break;
		case SG_SCSI_RESET_BUS:
			val = SCSI_TRY_RESET_BUS;
			break;
		case SG_SCSI_RESET_HOST:
			val = SCSI_TRY_RESET_HOST;
			break;
		default:
			return -EINVAL;
		}
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		return (scsi_reset_provider(sdp->device, val) ==
			SUCCESS) ? 0 : -EIO;
	case SCSI_IOCTL_SEND_COMMAND:
		if (sdp->detached)
			return -ENODEV;
		if (read_only) {
			unsigned char opcode = WRITE_6;
			Scsi_Ioctl_Command __user *siocp = p;

			if (copy_from_user(&opcode, siocp->data, 1))
				return -EFAULT;
			if (!sg_allow_access(opcode, sdp->device->type))
				return -EPERM;
		}
		return sg_scsi_ioctl(filp, sdp->device->request_queue, NULL, p);
	case SG_SET_DEBUG:
		result = get_user(val, ip);
		if (result)
			return result;
		sdp->sgdebug = (char) val;
		return 0;
	case SCSI_IOCTL_GET_IDLUN:
	case SCSI_IOCTL_GET_BUS_NUMBER:
	case SCSI_IOCTL_PROBE_HOST:
	case SG_GET_TRANSFORM:
		if (sdp->detached)
			return -ENODEV;
		return scsi_ioctl(sdp->device, cmd_in, p);
	case BLKSECTGET:
		return put_user(sdp->device->request_queue->max_sectors * 512,
				ip);
	default:
		if (read_only)
			return -EPERM;	/* don't know so take safe approach */
		return scsi_ioctl(sdp->device, cmd_in, p);
	}
}

#ifdef CONFIG_COMPAT
static long sg_compat_ioctl(struct file *filp, unsigned int cmd_in, unsigned long arg)
{
	Sg_device *sdp;
	Sg_fd *sfp;
	struct scsi_device *sdev;

	if ((!(sfp = (Sg_fd *) filp->private_data)) || (!(sdp = sfp->parentdp)))
		return -ENXIO;

	sdev = sdp->device;
	if (sdev->host->hostt->compat_ioctl) { 
		int ret;

		ret = sdev->host->hostt->compat_ioctl(sdev, cmd_in, (void __user *)arg);

		return ret;
	}
	
	return -ENOIOCTLCMD;
}
#endif

static unsigned int
sg_poll(struct file *filp, poll_table * wait)
{
	unsigned int res = 0;
	Sg_device *sdp;
	Sg_fd *sfp;
	Sg_request *srp;
	int count = 0;
	unsigned long iflags;

	if ((!(sfp = (Sg_fd *) filp->private_data)) || (!(sdp = sfp->parentdp))
	    || sfp->closed)
		return POLLERR;
	poll_wait(filp, &sfp->read_wait, wait);
	read_lock_irqsave(&sfp->rq_list_lock, iflags);
	for (srp = sfp->headrp; srp; srp = srp->nextrp) {
		/* if any read waiting, flag it */
		if ((0 == res) && (1 == srp->done) && (!srp->sg_io_owned))
			res = POLLIN | POLLRDNORM;
		++count;
	}
	read_unlock_irqrestore(&sfp->rq_list_lock, iflags);

	if (sdp->detached)
		res |= POLLHUP;
	else if (!sfp->cmd_q) {
		if (0 == count)
			res |= POLLOUT | POLLWRNORM;
	} else if (count < SG_MAX_QUEUE)
		res |= POLLOUT | POLLWRNORM;
	SCSI_LOG_TIMEOUT(3, printk("sg_poll: %s, res=0x%x\n",
				   sdp->disk->disk_name, (int) res));
	return res;
}

static int
sg_fasync(int fd, struct file *filp, int mode)
{
	int retval;
	Sg_device *sdp;
	Sg_fd *sfp;

	if ((!(sfp = (Sg_fd *) filp->private_data)) || (!(sdp = sfp->parentdp)))
		return -ENXIO;
	SCSI_LOG_TIMEOUT(3, printk("sg_fasync: %s, mode=%d\n",
				   sdp->disk->disk_name, mode));

	retval = fasync_helper(fd, filp, mode, &sfp->async_qp);
	return (retval < 0) ? retval : 0;
}

static struct page *
sg_vma_nopage(struct vm_area_struct *vma, unsigned long addr, int *type)
{
	Sg_fd *sfp;
	struct page *page = NOPAGE_SIGBUS;
	unsigned long offset, len, sa;
	Sg_scatter_hold *rsv_schp;
	struct scatterlist *sg;
	int k;

	if ((NULL == vma) || (!(sfp = (Sg_fd *) vma->vm_private_data)))
		return page;
	rsv_schp = &sfp->reserve;
	offset = addr - vma->vm_start;
	if (offset >= rsv_schp->bufflen)
		return page;
	SCSI_LOG_TIMEOUT(3, printk("sg_vma_nopage: offset=%lu, scatg=%d\n",
				   offset, rsv_schp->k_use_sg));
	sg = rsv_schp->buffer;
	sa = vma->vm_start;
	for (k = 0; (k < rsv_schp->k_use_sg) && (sa < vma->vm_end);
	     ++k, ++sg) {
		len = vma->vm_end - sa;
		len = (len < sg->length) ? len : sg->length;
		if (offset < len) {
			page = virt_to_page(page_address(sg->page) + offset);
			get_page(page);	/* increment page count */
			break;
		}
		sa += len;
		offset -= len;
	}

	if (type)
		*type = VM_FAULT_MINOR;
	return page;
}

static struct vm_operations_struct sg_mmap_vm_ops = {
	.nopage = sg_vma_nopage,
};

static int
sg_mmap(struct file *filp, struct vm_area_struct *vma)
{
	Sg_fd *sfp;
	unsigned long req_sz, len, sa;
	Sg_scatter_hold *rsv_schp;
	int k;
	struct scatterlist *sg;

	if ((!filp) || (!vma) || (!(sfp = (Sg_fd *) filp->private_data)))
		return -ENXIO;
	req_sz = vma->vm_end - vma->vm_start;
	SCSI_LOG_TIMEOUT(3, printk("sg_mmap starting, vm_start=%p, len=%d\n",
				   (void *) vma->vm_start, (int) req_sz));
	if (vma->vm_pgoff)
		return -EINVAL;	/* want no offset */
	rsv_schp = &sfp->reserve;
	if (req_sz > rsv_schp->bufflen)
		return -ENOMEM;	/* cannot map more than reserved buffer */

	sa = vma->vm_start;
	sg = rsv_schp->buffer;
	for (k = 0; (k < rsv_schp->k_use_sg) && (sa < vma->vm_end);
	     ++k, ++sg) {
		len = vma->vm_end - sa;
		len = (len < sg->length) ? len : sg->length;
		sa += len;
	}

	sfp->mmap_called = 1;
	vma->vm_flags |= VM_RESERVED;
	vma->vm_private_data = sfp;
	vma->vm_ops = &sg_mmap_vm_ops;
	return 0;
}

/* This function is a "bottom half" handler that is called by the
 * mid level when a command is completed (or has failed). */
static void
sg_cmd_done(void *data, char *sense, int result, int resid)
{
	Sg_request *srp = data;
	Sg_device *sdp = NULL;
	Sg_fd *sfp;
	unsigned long iflags;
	unsigned int ms;

	if (NULL == srp) {
		printk(KERN_ERR "sg_cmd_done: NULL request\n");
		return;
	}
	sfp = srp->parentfp;
	if (sfp)
		sdp = sfp->parentdp;
	if ((NULL == sdp) || sdp->detached) {
		printk(KERN_INFO "sg_cmd_done: device detached\n");
		return;
	}


	SCSI_LOG_TIMEOUT(4, printk("sg_cmd_done: %s, pack_id=%d, res=0x%x\n",
		sdp->disk->disk_name, srp->header.pack_id, result));
	srp->header.resid = resid;
	ms = jiffies_to_msecs(jiffies);
	srp->header.duration = (ms > srp->header.duration) ?
				(ms - srp->header.duration) : 0;
	if (0 != result) {
		struct scsi_sense_hdr sshdr;

		memcpy(srp->sense_b, sense, sizeof (srp->sense_b));
		srp->header.status = 0xff & result;
		srp->header.masked_status = status_byte(result);
		srp->header.msg_status = msg_byte(result);
		srp->header.host_status = host_byte(result);
		srp->header.driver_status = driver_byte(result);
		if ((sdp->sgdebug > 0) &&
		    ((CHECK_CONDITION == srp->header.masked_status) ||
		     (COMMAND_TERMINATED == srp->header.masked_status)))
			__scsi_print_sense("sg_cmd_done", sense,
					   SCSI_SENSE_BUFFERSIZE);

		/* Following if statement is a patch supplied by Eric Youngdale */
		if (driver_byte(result) != 0
		    && scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE, &sshdr)
		    && !scsi_sense_is_deferred(&sshdr)
		    && sshdr.sense_key == UNIT_ATTENTION
		    && sdp->device->removable) {
			/* Detected possible disc change. Set the bit - this */
			/* may be used if there are filesystems using this device */
			sdp->device->changed = 1;
		}
	}
	/* Rely on write phase to clean out srp status values, so no "else" */

	if (sfp->closed) {	/* whoops this fd already released, cleanup */
		SCSI_LOG_TIMEOUT(1, printk("sg_cmd_done: already closed, freeing ...\n"));
		sg_finish_rem_req(srp);
		srp = NULL;
		if (NULL == sfp->headrp) {
			SCSI_LOG_TIMEOUT(1, printk("sg_cmd_done: already closed, final cleanup\n"));
			if (0 == sg_remove_sfp(sdp, sfp)) {	/* device still present */
				scsi_device_put(sdp->device);
			}
			sfp = NULL;
		}
	} else if (srp && srp->orphan) {
		if (sfp->keep_orphan)
			srp->sg_io_owned = 0;
		else {
			sg_finish_rem_req(srp);
			srp = NULL;
		}
	}
	if (sfp && srp) {
		/* Now wake up any sg_read() that is waiting for this packet. */
		kill_fasync(&sfp->async_qp, SIGPOLL, POLL_IN);
		write_lock_irqsave(&sfp->rq_list_lock, iflags);
		srp->done = 1;
		wake_up_interruptible(&sfp->read_wait);
		write_unlock_irqrestore(&sfp->rq_list_lock, iflags);
	}
}

static struct file_operations sg_fops = {
	.owner = THIS_MODULE,
	.read = sg_read,
	.write = sg_write,
	.poll = sg_poll,
	.ioctl = sg_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = sg_compat_ioctl,
#endif
	.open = sg_open,
	.mmap = sg_mmap,
	.release = sg_release,
	.fasync = sg_fasync,
};

static struct class *sg_sysfs_class;

static int sg_sysfs_valid = 0;

static Sg_device *sg_alloc(struct gendisk *disk, struct scsi_device *scsidp)
{
	struct request_queue *q = scsidp->request_queue;
	Sg_device *sdp;
	unsigned long iflags;
	int error;
	u32 k;

	sdp = kzalloc(sizeof(Sg_device), GFP_KERNEL);
	if (!sdp) {
		printk(KERN_WARNING "kmalloc Sg_device failure\n");
		return ERR_PTR(-ENOMEM);
	}
	error = -ENOMEM;
	if (!idr_pre_get(&sg_index_idr, GFP_KERNEL)) {
		printk(KERN_WARNING "idr expansion Sg_device failure\n");
		goto out;
	}

	write_lock_irqsave(&sg_index_lock, iflags);
	error = idr_get_new(&sg_index_idr, sdp, &k);
	write_unlock_irqrestore(&sg_index_lock, iflags);

	if (error) {
		printk(KERN_WARNING "idr allocation Sg_device failure: %d\n",
		       error);
		goto out;
	}

	if (unlikely(k >= SG_MAX_DEVS))
		goto overflow;

	SCSI_LOG_TIMEOUT(3, printk("sg_alloc: dev=%d \n", k));
	sprintf(disk->disk_name, "sg%d", k);
	disk->first_minor = k;
	sdp->disk = disk;
	sdp->device = scsidp;
	init_waitqueue_head(&sdp->o_excl_wait);
	sdp->sg_tablesize = min(q->max_hw_segments, q->max_phys_segments);
	sdp->index = k;

	error = 0;
 out:
	if (error) {
		kfree(sdp);
		return ERR_PTR(error);
	}
	return sdp;

 overflow:
	sdev_printk(KERN_WARNING, scsidp,
		    "Unable to attach sg device type=%d, minor "
		    "number exceeds %d\n", scsidp->type, SG_MAX_DEVS - 1);
	error = -ENODEV;
	goto out;
}

static int
sg_add(struct class_device *cl_dev, struct class_interface *cl_intf)
{
	struct scsi_device *scsidp = to_scsi_device(cl_dev->dev);
	struct gendisk *disk;
	Sg_device *sdp = NULL;
	struct cdev * cdev = NULL;
	int error;
	unsigned long iflags;

	disk = alloc_disk(1);
	if (!disk) {
		printk(KERN_WARNING "alloc_disk failed\n");
		return -ENOMEM;
	}
	disk->major = SCSI_GENERIC_MAJOR;

	error = -ENOMEM;
	cdev = cdev_alloc();
	if (!cdev) {
		printk(KERN_WARNING "cdev_alloc failed\n");
		goto out;
	}
	cdev->owner = THIS_MODULE;
	cdev->ops = &sg_fops;

	sdp = sg_alloc(disk, scsidp);
	if (IS_ERR(sdp)) {
		printk(KERN_WARNING "sg_alloc failed\n");
		error = PTR_ERR(sdp);
		goto out;
	}

	class_set_devdata(cl_dev, sdp);
	error = cdev_add(cdev, MKDEV(SCSI_GENERIC_MAJOR, sdp->index), 1);
	if (error)
		goto cdev_add_err;

	sdp->cdev = cdev;
	if (sg_sysfs_valid) {
		struct class_device * sg_class_member;

		sg_class_member = class_device_create(sg_sysfs_class, NULL,
				MKDEV(SCSI_GENERIC_MAJOR, sdp->index),
				cl_dev->dev, "%s",
				disk->disk_name);
		if (IS_ERR(sg_class_member))
			printk(KERN_WARNING "sg_add: "
				"class_device_create failed\n");
		class_set_devdata(sg_class_member, sdp);
		error = sysfs_create_link(&scsidp->sdev_gendev.kobj, 
					  &sg_class_member->kobj, "generic");
		if (error)
			printk(KERN_ERR "sg_add: unable to make symlink "
					"'generic' back to sg%d\n", sdp->index);
	} else
		printk(KERN_WARNING "sg_add: sg_sys Invalid\n");

	sdev_printk(KERN_NOTICE, scsidp,
		    "Attached scsi generic sg%d type %d\n", sdp->index,
		    scsidp->type);

	return 0;

cdev_add_err:
	write_lock_irqsave(&sg_index_lock, iflags);
	idr_remove(&sg_index_idr, sdp->index);
	write_unlock_irqrestore(&sg_index_lock, iflags);
	kfree(sdp);

out:
	put_disk(disk);
	if (cdev)
		cdev_del(cdev);
	return error;
}

static void
sg_remove(struct class_device *cl_dev, struct class_interface *cl_intf)
{
	struct scsi_device *scsidp = to_scsi_device(cl_dev->dev);
	Sg_device *sdp = class_get_devdata(cl_dev);
	unsigned long iflags;
	Sg_fd *sfp;
	Sg_fd *tsfp;
	Sg_request *srp;
	Sg_request *tsrp;
	int delay;

	if (!sdp)
		return;

	delay = 0;
	write_lock_irqsave(&sg_index_lock, iflags);
	if (sdp->headfp) {
		sdp->detached = 1;
		for (sfp = sdp->headfp; sfp; sfp = tsfp) {
			tsfp = sfp->nextfp;
			for (srp = sfp->headrp; srp; srp = tsrp) {
				tsrp = srp->nextrp;
				if (sfp->closed || (0 == sg_srp_done(srp, sfp)))
					sg_finish_rem_req(srp);
			}
			if (sfp->closed) {
				scsi_device_put(sdp->device);
				__sg_remove_sfp(sdp, sfp);
			} else {
				delay = 1;
				wake_up_interruptible(&sfp->read_wait);
				kill_fasync(&sfp->async_qp, SIGPOLL,
					    POLL_HUP);
			}
		}
		SCSI_LOG_TIMEOUT(3, printk("sg_remove: dev=%d, dirty\n", sdp->index));
		if (NULL == sdp->headfp) {
			idr_remove(&sg_index_idr, sdp->index);
		}
	} else {	/* nothing active, simple case */
		SCSI_LOG_TIMEOUT(3, printk("sg_remove: dev=%d\n", sdp->index));
		idr_remove(&sg_index_idr, sdp->index);
	}
	write_unlock_irqrestore(&sg_index_lock, iflags);

	sysfs_remove_link(&scsidp->sdev_gendev.kobj, "generic");
	class_device_destroy(sg_sysfs_class, MKDEV(SCSI_GENERIC_MAJOR, sdp->index));
	cdev_del(sdp->cdev);
	sdp->cdev = NULL;
	put_disk(sdp->disk);
	sdp->disk = NULL;
	if (NULL == sdp->headfp)
		kfree(sdp);

	if (delay)
		msleep(10);	/* dirty detach so delay device destruction */
}

module_param_named(scatter_elem_sz, scatter_elem_sz, int, S_IRUGO | S_IWUSR);
module_param_named(def_reserved_size, def_reserved_size, int,
		   S_IRUGO | S_IWUSR);
module_param_named(allow_dio, sg_allow_dio, int, S_IRUGO | S_IWUSR);

MODULE_AUTHOR("Douglas Gilbert");
MODULE_DESCRIPTION("SCSI generic (sg) driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(SG_VERSION_STR);
MODULE_ALIAS_CHARDEV_MAJOR(SCSI_GENERIC_MAJOR);

MODULE_PARM_DESC(scatter_elem_sz, "scatter gather element "
                "size (default: max(SG_SCATTER_SZ, PAGE_SIZE))");
MODULE_PARM_DESC(def_reserved_size, "size of buffer reserved for each fd");
MODULE_PARM_DESC(allow_dio, "allow direct I/O (default: 0 (disallow))");

static int __init
init_sg(void)
{
	int rc;

	if (scatter_elem_sz < PAGE_SIZE) {
		scatter_elem_sz = PAGE_SIZE;
		scatter_elem_sz_prev = scatter_elem_sz;
	}
	if (def_reserved_size >= 0)
		sg_big_buff = def_reserved_size;
	else
		def_reserved_size = sg_big_buff;

	rc = register_chrdev_region(MKDEV(SCSI_GENERIC_MAJOR, 0), 
				    SG_MAX_DEVS, "sg");
	if (rc)
		return rc;
        sg_sysfs_class = class_create(THIS_MODULE, "scsi_generic");
        if ( IS_ERR(sg_sysfs_class) ) {
		rc = PTR_ERR(sg_sysfs_class);
		goto err_out;
        }
	sg_sysfs_valid = 1;
	rc = scsi_register_interface(&sg_interface);
	if (0 == rc) {
#ifdef CONFIG_SCSI_PROC_FS
		sg_proc_init();
#endif				/* CONFIG_SCSI_PROC_FS */
		return 0;
	}
	class_destroy(sg_sysfs_class);
err_out:
	unregister_chrdev_region(MKDEV(SCSI_GENERIC_MAJOR, 0), SG_MAX_DEVS);
	return rc;
}

static void __exit
exit_sg(void)
{
#ifdef CONFIG_SCSI_PROC_FS
	sg_proc_cleanup();
#endif				/* CONFIG_SCSI_PROC_FS */
	scsi_unregister_interface(&sg_interface);
	class_destroy(sg_sysfs_class);
	sg_sysfs_valid = 0;
	unregister_chrdev_region(MKDEV(SCSI_GENERIC_MAJOR, 0),
				 SG_MAX_DEVS);
	idr_destroy(&sg_index_idr);
}

static int
sg_start_req(Sg_request * srp)
{
	int res;
	Sg_fd *sfp = srp->parentfp;
	sg_io_hdr_t *hp = &srp->header;
	int dxfer_len = (int) hp->dxfer_len;
	int dxfer_dir = hp->dxfer_direction;
	Sg_scatter_hold *req_schp = &srp->data;
	Sg_scatter_hold *rsv_schp = &sfp->reserve;

	SCSI_LOG_TIMEOUT(4, printk("sg_start_req: dxfer_len=%d\n", dxfer_len));
	if ((dxfer_len <= 0) || (dxfer_dir == SG_DXFER_NONE))
		return 0;
	if (sg_allow_dio && (hp->flags & SG_FLAG_DIRECT_IO) &&
	    (dxfer_dir != SG_DXFER_UNKNOWN) && (0 == hp->iovec_count) &&
	    (!sfp->parentdp->device->host->unchecked_isa_dma)) {
		res = sg_build_direct(srp, sfp, dxfer_len);
		if (res <= 0)	/* -ve -> error, 0 -> done, 1 -> try indirect */
			return res;
	}
	if ((!sg_res_in_use(sfp)) && (dxfer_len <= rsv_schp->bufflen))
		sg_link_reserve(sfp, srp, dxfer_len);
	else {
		res = sg_build_indirect(req_schp, sfp, dxfer_len);
		if (res) {
			sg_remove_scat(req_schp);
			return res;
		}
	}
	return 0;
}

static void
sg_finish_rem_req(Sg_request * srp)
{
	Sg_fd *sfp = srp->parentfp;
	Sg_scatter_hold *req_schp = &srp->data;

	SCSI_LOG_TIMEOUT(4, printk("sg_finish_rem_req: res_used=%d\n", (int) srp->res_used));
	if (srp->res_used)
		sg_unlink_reserve(sfp, srp);
	else
		sg_remove_scat(req_schp);
	sg_remove_request(sfp, srp);
}

static int
sg_build_sgat(Sg_scatter_hold * schp, const Sg_fd * sfp, int tablesize)
{
	int sg_bufflen = tablesize * sizeof(struct scatterlist);
	gfp_t gfp_flags = GFP_ATOMIC | __GFP_NOWARN;

	/*
	 * TODO: test without low_dma, we should not need it since
	 * the block layer will bounce the buffer for us
	 *
	 * XXX(hch): we shouldn't need GFP_DMA for the actual S/G list.
	 */
	if (sfp->low_dma)
		 gfp_flags |= GFP_DMA;
	schp->buffer = kzalloc(sg_bufflen, gfp_flags);
	if (!schp->buffer)
		return -ENOMEM;
	schp->sglist_len = sg_bufflen;
	return tablesize;	/* number of scat_gath elements allocated */
}

#ifdef SG_ALLOW_DIO_CODE
/* vvvvvvvv  following code borrowed from st driver's direct IO vvvvvvvvv */
	/* TODO: hopefully we can use the generic block layer code */

/* Pin down user pages and put them into a scatter gather list. Returns <= 0 if
   - mapping of all pages not successful
   (i.e., either completely successful or fails)
*/
static int 
st_map_user_pages(struct scatterlist *sgl, const unsigned int max_pages, 
	          unsigned long uaddr, size_t count, int rw)
{
	unsigned long end = (uaddr + count + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = uaddr >> PAGE_SHIFT;
	const int nr_pages = end - start;
	int res, i, j;
	struct page **pages;

	/* User attempted Overflow! */
	if ((uaddr + count) < uaddr)
		return -EINVAL;

	/* Too big */
        if (nr_pages > max_pages)
		return -ENOMEM;

	/* Hmm? */
	if (count == 0)
		return 0;

	if ((pages = kmalloc(max_pages * sizeof(*pages), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

        /* Try to fault in all of the necessary pages */
	down_read(&current->mm->mmap_sem);
        /* rw==READ means read from drive, write into memory area */
	res = get_user_pages(
		current,
		current->mm,
		uaddr,
		nr_pages,
		rw == READ,
		0, /* don't force */
		pages,
		NULL);
	up_read(&current->mm->mmap_sem);

	/* Errors and no page mapped should return here */
	if (res < nr_pages)
		goto out_unmap;

        for (i=0; i < nr_pages; i++) {
                /* FIXME: flush superflous for rw==READ,
                 * probably wrong function for rw==WRITE
                 */
		flush_dcache_page(pages[i]);
		/* ?? Is locking needed? I don't think so */
		/* if (TestSetPageLocked(pages[i]))
		   goto out_unlock; */
        }

	sgl[0].page = pages[0];
	sgl[0].offset = uaddr & ~PAGE_MASK;
	if (nr_pages > 1) {
		sgl[0].length = PAGE_SIZE - sgl[0].offset;
		count -= sgl[0].length;
		for (i=1; i < nr_pages ; i++) {
			sgl[i].page = pages[i]; 
			sgl[i].length = count < PAGE_SIZE ? count : PAGE_SIZE;
			count -= PAGE_SIZE;
		}
	}
	else {
		sgl[0].length = count;
	}

	kfree(pages);
	return nr_pages;

 out_unmap:
	if (res > 0) {
		for (j=0; j < res; j++)
			page_cache_release(pages[j]);
		res = 0;
	}
	kfree(pages);
	return res;
}


/* And unmap them... */
static int 
st_unmap_user_pages(struct scatterlist *sgl, const unsigned int nr_pages,
		    int dirtied)
{
	int i;

	for (i=0; i < nr_pages; i++) {
		struct page *page = sgl[i].page;

		if (dirtied)
			SetPageDirty(page);
		/* unlock_page(page); */
		/* FIXME: cache flush missing for rw==READ
		 * FIXME: call the correct reference counting function
		 */
		page_cache_release(page);
	}

	return 0;
}

/* ^^^^^^^^  above code borrowed from st driver's direct IO ^^^^^^^^^ */
#endif


/* Returns: -ve -> error, 0 -> done, 1 -> try indirect */
static int
sg_build_direct(Sg_request * srp, Sg_fd * sfp, int dxfer_len)
{
#ifdef SG_ALLOW_DIO_CODE
	sg_io_hdr_t *hp = &srp->header;
	Sg_scatter_hold *schp = &srp->data;
	int sg_tablesize = sfp->parentdp->sg_tablesize;
	int mx_sc_elems, res;
	struct scsi_device *sdev = sfp->parentdp->device;

	if (((unsigned long)hp->dxferp &
			queue_dma_alignment(sdev->request_queue)) != 0)
		return 1;

	mx_sc_elems = sg_build_sgat(schp, sfp, sg_tablesize);
        if (mx_sc_elems <= 0) {
                return 1;
        }
	res = st_map_user_pages(schp->buffer, mx_sc_elems,
				(unsigned long)hp->dxferp, dxfer_len, 
				(SG_DXFER_TO_DEV == hp->dxfer_direction) ? 1 : 0);
	if (res <= 0) {
		sg_remove_scat(schp);
		return 1;
	}
	schp->k_use_sg = res;
	schp->dio_in_use = 1;
	hp->info |= SG_INFO_DIRECT_IO;
	return 0;
#else
	return 1;
#endif
}

static int
sg_build_indirect(Sg_scatter_hold * schp, Sg_fd * sfp, int buff_size)
{
	struct scatterlist *sg;
	int ret_sz = 0, k, rem_sz, num, mx_sc_elems;
	int sg_tablesize = sfp->parentdp->sg_tablesize;
	int blk_size = buff_size;
	struct page *p = NULL;

	if (blk_size < 0)
		return -EFAULT;
	if (0 == blk_size)
		++blk_size;	/* don't know why */
/* round request up to next highest SG_SECTOR_SZ byte boundary */
	blk_size = (blk_size + SG_SECTOR_MSK) & (~SG_SECTOR_MSK);
	SCSI_LOG_TIMEOUT(4, printk("sg_build_indirect: buff_size=%d, blk_size=%d\n",
				   buff_size, blk_size));

	/* N.B. ret_sz carried into this block ... */
	mx_sc_elems = sg_build_sgat(schp, sfp, sg_tablesize);
	if (mx_sc_elems < 0)
		return mx_sc_elems;	/* most likely -ENOMEM */

	num = scatter_elem_sz;
	if (unlikely(num != scatter_elem_sz_prev)) {
		if (num < PAGE_SIZE) {
			scatter_elem_sz = PAGE_SIZE;
			scatter_elem_sz_prev = PAGE_SIZE;
		} else
			scatter_elem_sz_prev = num;
	}
	for (k = 0, sg = schp->buffer, rem_sz = blk_size;
	     (rem_sz > 0) && (k < mx_sc_elems);
	     ++k, rem_sz -= ret_sz, ++sg) {
		
		num = (rem_sz > scatter_elem_sz_prev) ?
		      scatter_elem_sz_prev : rem_sz;
		p = sg_page_malloc(num, sfp->low_dma, &ret_sz);
		if (!p)
			return -ENOMEM;

		if (num == scatter_elem_sz_prev) {
			if (unlikely(ret_sz > scatter_elem_sz_prev)) {
				scatter_elem_sz = ret_sz;
				scatter_elem_sz_prev = ret_sz;
			}
		}
		sg->page = p;
		sg->length = (ret_sz > num) ? num : ret_sz;

		SCSI_LOG_TIMEOUT(5, printk("sg_build_indirect: k=%d, num=%d, "
				 "ret_sz=%d\n", k, num, ret_sz));
	}		/* end of for loop */

	schp->k_use_sg = k;
	SCSI_LOG_TIMEOUT(5, printk("sg_build_indirect: k_use_sg=%d, "
			 "rem_sz=%d\n", k, rem_sz));

	schp->bufflen = blk_size;
	if (rem_sz > 0)	/* must have failed */
		return -ENOMEM;

	return 0;
}

static int
sg_write_xfer(Sg_request * srp)
{
	sg_io_hdr_t *hp = &srp->header;
	Sg_scatter_hold *schp = &srp->data;
	struct scatterlist *sg = schp->buffer;
	int num_xfer = 0;
	int j, k, onum, usglen, ksglen, res;
	int iovec_count = (int) hp->iovec_count;
	int dxfer_dir = hp->dxfer_direction;
	unsigned char *p;
	unsigned char __user *up;
	int new_interface = ('\0' == hp->interface_id) ? 0 : 1;

	if ((SG_DXFER_UNKNOWN == dxfer_dir) || (SG_DXFER_TO_DEV == dxfer_dir) ||
	    (SG_DXFER_TO_FROM_DEV == dxfer_dir)) {
		num_xfer = (int) (new_interface ? hp->dxfer_len : hp->flags);
		if (schp->bufflen < num_xfer)
			num_xfer = schp->bufflen;
	}
	if ((num_xfer <= 0) || (schp->dio_in_use) ||
	    (new_interface
	     && ((SG_FLAG_NO_DXFER | SG_FLAG_MMAP_IO) & hp->flags)))
		return 0;

	SCSI_LOG_TIMEOUT(4, printk("sg_write_xfer: num_xfer=%d, iovec_count=%d, k_use_sg=%d\n",
			  num_xfer, iovec_count, schp->k_use_sg));
	if (iovec_count) {
		onum = iovec_count;
		if (!access_ok(VERIFY_READ, hp->dxferp, SZ_SG_IOVEC * onum))
			return -EFAULT;
	} else
		onum = 1;

	ksglen = sg->length;
	p = page_address(sg->page);
	for (j = 0, k = 0; j < onum; ++j) {
		res = sg_u_iovec(hp, iovec_count, j, 1, &usglen, &up);
		if (res)
			return res;

		for (; p; ++sg, ksglen = sg->length,
		     p = page_address(sg->page)) {
			if (usglen <= 0)
				break;
			if (ksglen > usglen) {
				if (usglen >= num_xfer) {
					if (__copy_from_user(p, up, num_xfer))
						return -EFAULT;
					return 0;
				}
				if (__copy_from_user(p, up, usglen))
					return -EFAULT;
				p += usglen;
				ksglen -= usglen;
				break;
			} else {
				if (ksglen >= num_xfer) {
					if (__copy_from_user(p, up, num_xfer))
						return -EFAULT;
					return 0;
				}
				if (__copy_from_user(p, up, ksglen))
					return -EFAULT;
				up += ksglen;
				usglen -= ksglen;
			}
			++k;
			if (k >= schp->k_use_sg)
				return 0;
		}
	}

	return 0;
}

static int
sg_u_iovec(sg_io_hdr_t * hp, int sg_num, int ind,
	   int wr_xf, int *countp, unsigned char __user **up)
{
	int num_xfer = (int) hp->dxfer_len;
	unsigned char __user *p = hp->dxferp;
	int count;

	if (0 == sg_num) {
		if (wr_xf && ('\0' == hp->interface_id))
			count = (int) hp->flags;	/* holds "old" input_size */
		else
			count = num_xfer;
	} else {
		sg_iovec_t iovec;
		if (__copy_from_user(&iovec, p + ind*SZ_SG_IOVEC, SZ_SG_IOVEC))
			return -EFAULT;
		p = iovec.iov_base;
		count = (int) iovec.iov_len;
	}
	if (!access_ok(wr_xf ? VERIFY_READ : VERIFY_WRITE, p, count))
		return -EFAULT;
	if (up)
		*up = p;
	if (countp)
		*countp = count;
	return 0;
}

static void
sg_remove_scat(Sg_scatter_hold * schp)
{
	SCSI_LOG_TIMEOUT(4, printk("sg_remove_scat: k_use_sg=%d\n", schp->k_use_sg));
	if (schp->buffer && (schp->sglist_len > 0)) {
		struct scatterlist *sg = schp->buffer;

		if (schp->dio_in_use) {
#ifdef SG_ALLOW_DIO_CODE
			st_unmap_user_pages(sg, schp->k_use_sg, TRUE);
#endif
		} else {
			int k;

			for (k = 0; (k < schp->k_use_sg) && sg->page;
			     ++k, ++sg) {
				SCSI_LOG_TIMEOUT(5, printk(
				    "sg_remove_scat: k=%d, pg=0x%p, len=%d\n",
				    k, sg->page, sg->length));
				sg_page_free(sg->page, sg->length);
			}
		}
		kfree(schp->buffer);
	}
	memset(schp, 0, sizeof (*schp));
}

static int
sg_read_xfer(Sg_request * srp)
{
	sg_io_hdr_t *hp = &srp->header;
	Sg_scatter_hold *schp = &srp->data;
	struct scatterlist *sg = schp->buffer;
	int num_xfer = 0;
	int j, k, onum, usglen, ksglen, res;
	int iovec_count = (int) hp->iovec_count;
	int dxfer_dir = hp->dxfer_direction;
	unsigned char *p;
	unsigned char __user *up;
	int new_interface = ('\0' == hp->interface_id) ? 0 : 1;

	if ((SG_DXFER_UNKNOWN == dxfer_dir) || (SG_DXFER_FROM_DEV == dxfer_dir)
	    || (SG_DXFER_TO_FROM_DEV == dxfer_dir)) {
		num_xfer = hp->dxfer_len;
		if (schp->bufflen < num_xfer)
			num_xfer = schp->bufflen;
	}
	if ((num_xfer <= 0) || (schp->dio_in_use) ||
	    (new_interface
	     && ((SG_FLAG_NO_DXFER | SG_FLAG_MMAP_IO) & hp->flags)))
		return 0;

	SCSI_LOG_TIMEOUT(4, printk("sg_read_xfer: num_xfer=%d, iovec_count=%d, k_use_sg=%d\n",
			  num_xfer, iovec_count, schp->k_use_sg));
	if (iovec_count) {
		onum = iovec_count;
		if (!access_ok(VERIFY_READ, hp->dxferp, SZ_SG_IOVEC * onum))
			return -EFAULT;
	} else
		onum = 1;

	p = page_address(sg->page);
	ksglen = sg->length;
	for (j = 0, k = 0; j < onum; ++j) {
		res = sg_u_iovec(hp, iovec_count, j, 0, &usglen, &up);
		if (res)
			return res;

		for (; p; ++sg, ksglen = sg->length,
		     p = page_address(sg->page)) {
			if (usglen <= 0)
				break;
			if (ksglen > usglen) {
				if (usglen >= num_xfer) {
					if (__copy_to_user(up, p, num_xfer))
						return -EFAULT;
					return 0;
				}
				if (__copy_to_user(up, p, usglen))
					return -EFAULT;
				p += usglen;
				ksglen -= usglen;
				break;
			} else {
				if (ksglen >= num_xfer) {
					if (__copy_to_user(up, p, num_xfer))
						return -EFAULT;
					return 0;
				}
				if (__copy_to_user(up, p, ksglen))
					return -EFAULT;
				up += ksglen;
				usglen -= ksglen;
			}
			++k;
			if (k >= schp->k_use_sg)
				return 0;
		}
	}

	return 0;
}

static int
sg_read_oxfer(Sg_request * srp, char __user *outp, int num_read_xfer)
{
	Sg_scatter_hold *schp = &srp->data;
	struct scatterlist *sg = schp->buffer;
	int k, num;

	SCSI_LOG_TIMEOUT(4, printk("sg_read_oxfer: num_read_xfer=%d\n",
				   num_read_xfer));
	if ((!outp) || (num_read_xfer <= 0))
		return 0;

	for (k = 0; (k < schp->k_use_sg) && sg->page; ++k, ++sg) {
		num = sg->length;
		if (num > num_read_xfer) {
			if (__copy_to_user(outp, page_address(sg->page),
					   num_read_xfer))
				return -EFAULT;
			break;
		} else {
			if (__copy_to_user(outp, page_address(sg->page),
					   num))
				return -EFAULT;
			num_read_xfer -= num;
			if (num_read_xfer <= 0)
				break;
			outp += num;
		}
	}

	return 0;
}

static void
sg_build_reserve(Sg_fd * sfp, int req_size)
{
	Sg_scatter_hold *schp = &sfp->reserve;

	SCSI_LOG_TIMEOUT(4, printk("sg_build_reserve: req_size=%d\n", req_size));
	do {
		if (req_size < PAGE_SIZE)
			req_size = PAGE_SIZE;
		if (0 == sg_build_indirect(schp, sfp, req_size))
			return;
		else
			sg_remove_scat(schp);
		req_size >>= 1;	/* divide by 2 */
	} while (req_size > (PAGE_SIZE / 2));
}

static void
sg_link_reserve(Sg_fd * sfp, Sg_request * srp, int size)
{
	Sg_scatter_hold *req_schp = &srp->data;
	Sg_scatter_hold *rsv_schp = &sfp->reserve;
	struct scatterlist *sg = rsv_schp->buffer;
	int k, num, rem;

	srp->res_used = 1;
	SCSI_LOG_TIMEOUT(4, printk("sg_link_reserve: size=%d\n", size));
	rem = size;

	for (k = 0; k < rsv_schp->k_use_sg; ++k, ++sg) {
		num = sg->length;
		if (rem <= num) {
			sfp->save_scat_len = num;
			sg->length = rem;
			req_schp->k_use_sg = k + 1;
			req_schp->sglist_len = rsv_schp->sglist_len;
			req_schp->buffer = rsv_schp->buffer;

			req_schp->bufflen = size;
			req_schp->b_malloc_len = rsv_schp->b_malloc_len;
			break;
		} else
			rem -= num;
	}

	if (k >= rsv_schp->k_use_sg)
		SCSI_LOG_TIMEOUT(1, printk("sg_link_reserve: BAD size\n"));
}

static void
sg_unlink_reserve(Sg_fd * sfp, Sg_request * srp)
{
	Sg_scatter_hold *req_schp = &srp->data;
	Sg_scatter_hold *rsv_schp = &sfp->reserve;

	SCSI_LOG_TIMEOUT(4, printk("sg_unlink_reserve: req->k_use_sg=%d\n",
				   (int) req_schp->k_use_sg));
	if ((rsv_schp->k_use_sg > 0) && (req_schp->k_use_sg > 0)) {
		struct scatterlist *sg = rsv_schp->buffer;

		if (sfp->save_scat_len > 0)
			(sg + (req_schp->k_use_sg - 1))->length =
			    (unsigned) sfp->save_scat_len;
		else
			SCSI_LOG_TIMEOUT(1, printk ("sg_unlink_reserve: BAD save_scat_len\n"));
	}
	req_schp->k_use_sg = 0;
	req_schp->bufflen = 0;
	req_schp->buffer = NULL;
	req_schp->sglist_len = 0;
	sfp->save_scat_len = 0;
	srp->res_used = 0;
}

static Sg_request *
sg_get_rq_mark(Sg_fd * sfp, int pack_id)
{
	Sg_request *resp;
	unsigned long iflags;

	write_lock_irqsave(&sfp->rq_list_lock, iflags);
	for (resp = sfp->headrp; resp; resp = resp->nextrp) {
		/* look for requests that are ready + not SG_IO owned */
		if ((1 == resp->done) && (!resp->sg_io_owned) &&
		    ((-1 == pack_id) || (resp->header.pack_id == pack_id))) {
			resp->done = 2;	/* guard against other readers */
			break;
		}
	}
	write_unlock_irqrestore(&sfp->rq_list_lock, iflags);
	return resp;
}

#ifdef CONFIG_SCSI_PROC_FS
static Sg_request *
sg_get_nth_request(Sg_fd * sfp, int nth)
{
	Sg_request *resp;
	unsigned long iflags;
	int k;

	read_lock_irqsave(&sfp->rq_list_lock, iflags);
	for (k = 0, resp = sfp->headrp; resp && (k < nth);
	     ++k, resp = resp->nextrp) ;
	read_unlock_irqrestore(&sfp->rq_list_lock, iflags);
	return resp;
}
#endif

/* always adds to end of list */
static Sg_request *
sg_add_request(Sg_fd * sfp)
{
	int k;
	unsigned long iflags;
	Sg_request *resp;
	Sg_request *rp = sfp->req_arr;

	write_lock_irqsave(&sfp->rq_list_lock, iflags);
	resp = sfp->headrp;
	if (!resp) {
		memset(rp, 0, sizeof (Sg_request));
		rp->parentfp = sfp;
		resp = rp;
		sfp->headrp = resp;
	} else {
		if (0 == sfp->cmd_q)
			resp = NULL;	/* command queuing disallowed */
		else {
			for (k = 0; k < SG_MAX_QUEUE; ++k, ++rp) {
				if (!rp->parentfp)
					break;
			}
			if (k < SG_MAX_QUEUE) {
				memset(rp, 0, sizeof (Sg_request));
				rp->parentfp = sfp;
				while (resp->nextrp)
					resp = resp->nextrp;
				resp->nextrp = rp;
				resp = rp;
			} else
				resp = NULL;
		}
	}
	if (resp) {
		resp->nextrp = NULL;
		resp->header.duration = jiffies_to_msecs(jiffies);
	}
	write_unlock_irqrestore(&sfp->rq_list_lock, iflags);
	return resp;
}

/* Return of 1 for found; 0 for not found */
static int
sg_remove_request(Sg_fd * sfp, Sg_request * srp)
{
	Sg_request *prev_rp;
	Sg_request *rp;
	unsigned long iflags;
	int res = 0;

	if ((!sfp) || (!srp) || (!sfp->headrp))
		return res;
	write_lock_irqsave(&sfp->rq_list_lock, iflags);
	prev_rp = sfp->headrp;
	if (srp == prev_rp) {
		sfp->headrp = prev_rp->nextrp;
		prev_rp->parentfp = NULL;
		res = 1;
	} else {
		while ((rp = prev_rp->nextrp)) {
			if (srp == rp) {
				prev_rp->nextrp = rp->nextrp;
				rp->parentfp = NULL;
				res = 1;
				break;
			}
			prev_rp = rp;
		}
	}
	write_unlock_irqrestore(&sfp->rq_list_lock, iflags);
	return res;
}

#ifdef CONFIG_SCSI_PROC_FS
static Sg_fd *
sg_get_nth_sfp(Sg_device * sdp, int nth)
{
	Sg_fd *resp;
	unsigned long iflags;
	int k;

	read_lock_irqsave(&sg_index_lock, iflags);
	for (k = 0, resp = sdp->headfp; resp && (k < nth);
	     ++k, resp = resp->nextfp) ;
	read_unlock_irqrestore(&sg_index_lock, iflags);
	return resp;
}
#endif

static Sg_fd *
sg_add_sfp(Sg_device * sdp, int dev)
{
	Sg_fd *sfp;
	unsigned long iflags;
	int bufflen;

	sfp = kzalloc(sizeof(*sfp), GFP_ATOMIC | __GFP_NOWARN);
	if (!sfp)
		return NULL;

	init_waitqueue_head(&sfp->read_wait);
	rwlock_init(&sfp->rq_list_lock);

	sfp->timeout = SG_DEFAULT_TIMEOUT;
	sfp->timeout_user = SG_DEFAULT_TIMEOUT_USER;
	sfp->force_packid = SG_DEF_FORCE_PACK_ID;
	sfp->low_dma = (SG_DEF_FORCE_LOW_DMA == 0) ?
	    sdp->device->host->unchecked_isa_dma : 1;
	sfp->cmd_q = SG_DEF_COMMAND_Q;
	sfp->keep_orphan = SG_DEF_KEEP_ORPHAN;
	sfp->parentdp = sdp;
	write_lock_irqsave(&sg_index_lock, iflags);
	if (!sdp->headfp)
		sdp->headfp = sfp;
	else {			/* add to tail of existing list */
		Sg_fd *pfp = sdp->headfp;
		while (pfp->nextfp)
			pfp = pfp->nextfp;
		pfp->nextfp = sfp;
	}
	write_unlock_irqrestore(&sg_index_lock, iflags);
	SCSI_LOG_TIMEOUT(3, printk("sg_add_sfp: sfp=0x%p\n", sfp));
	if (unlikely(sg_big_buff != def_reserved_size))
		sg_big_buff = def_reserved_size;

	bufflen = min_t(int, sg_big_buff,
			sdp->device->request_queue->max_sectors * 512);
	sg_build_reserve(sfp, bufflen);
	SCSI_LOG_TIMEOUT(3, printk("sg_add_sfp:   bufflen=%d, k_use_sg=%d\n",
			   sfp->reserve.bufflen, sfp->reserve.k_use_sg));
	return sfp;
}

static void
__sg_remove_sfp(Sg_device * sdp, Sg_fd * sfp)
{
	Sg_fd *fp;
	Sg_fd *prev_fp;

	prev_fp = sdp->headfp;
	if (sfp == prev_fp)
		sdp->headfp = prev_fp->nextfp;
	else {
		while ((fp = prev_fp->nextfp)) {
			if (sfp == fp) {
				prev_fp->nextfp = fp->nextfp;
				break;
			}
			prev_fp = fp;
		}
	}
	if (sfp->reserve.bufflen > 0) {
		SCSI_LOG_TIMEOUT(6, 
			printk("__sg_remove_sfp:    bufflen=%d, k_use_sg=%d\n",
			(int) sfp->reserve.bufflen, (int) sfp->reserve.k_use_sg));
		sg_remove_scat(&sfp->reserve);
	}
	sfp->parentdp = NULL;
	SCSI_LOG_TIMEOUT(6, printk("__sg_remove_sfp:    sfp=0x%p\n", sfp));
	kfree(sfp);
}

/* Returns 0 in normal case, 1 when detached and sdp object removed */
static int
sg_remove_sfp(Sg_device * sdp, Sg_fd * sfp)
{
	Sg_request *srp;
	Sg_request *tsrp;
	int dirty = 0;
	int res = 0;

	for (srp = sfp->headrp; srp; srp = tsrp) {
		tsrp = srp->nextrp;
		if (sg_srp_done(srp, sfp))
			sg_finish_rem_req(srp);
		else
			++dirty;
	}
	if (0 == dirty) {
		unsigned long iflags;

		write_lock_irqsave(&sg_index_lock, iflags);
		__sg_remove_sfp(sdp, sfp);
		if (sdp->detached && (NULL == sdp->headfp)) {
			idr_remove(&sg_index_idr, sdp->index);
			kfree(sdp);
			res = 1;
		}
		write_unlock_irqrestore(&sg_index_lock, iflags);
	} else {
		/* MOD_INC's to inhibit unloading sg and associated adapter driver */
		/* only bump the access_count if we actually succeeded in
		 * throwing another counter on the host module */
		scsi_device_get(sdp->device);	/* XXX: retval ignored? */	
		sfp->closed = 1;	/* flag dirty state on this fd */
		SCSI_LOG_TIMEOUT(1, printk("sg_remove_sfp: worrisome, %d writes pending\n",
				  dirty));
	}
	return res;
}

static int
sg_res_in_use(Sg_fd * sfp)
{
	const Sg_request *srp;
	unsigned long iflags;

	read_lock_irqsave(&sfp->rq_list_lock, iflags);
	for (srp = sfp->headrp; srp; srp = srp->nextrp)
		if (srp->res_used)
			break;
	read_unlock_irqrestore(&sfp->rq_list_lock, iflags);
	return srp ? 1 : 0;
}

/* The size fetched (value output via retSzp) set when non-NULL return */
static struct page *
sg_page_malloc(int rqSz, int lowDma, int *retSzp)
{
	struct page *resp = NULL;
	gfp_t page_mask;
	int order, a_size;
	int resSz;

	if ((rqSz <= 0) || (NULL == retSzp))
		return resp;

	if (lowDma)
		page_mask = GFP_ATOMIC | GFP_DMA | __GFP_COMP | __GFP_NOWARN;
	else
		page_mask = GFP_ATOMIC | __GFP_COMP | __GFP_NOWARN;

	for (order = 0, a_size = PAGE_SIZE; a_size < rqSz;
	     order++, a_size <<= 1) ;
	resSz = a_size;		/* rounded up if necessary */
	resp = alloc_pages(page_mask, order);
	while ((!resp) && order) {
		--order;
		a_size >>= 1;	/* divide by 2, until PAGE_SIZE */
		resp =  alloc_pages(page_mask, order);	/* try half */
		resSz = a_size;
	}
	if (resp) {
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			memset(page_address(resp), 0, resSz);
		*retSzp = resSz;
	}
	return resp;
}

static void
sg_page_free(struct page *page, int size)
{
	int order, a_size;

	if (!page)
		return;
	for (order = 0, a_size = PAGE_SIZE; a_size < size;
	     order++, a_size <<= 1) ;
	__free_pages(page, order);
}

#ifndef MAINTENANCE_IN_CMD
#define MAINTENANCE_IN_CMD 0xa3
#endif

static unsigned char allow_ops[] = { TEST_UNIT_READY, REQUEST_SENSE,
	INQUIRY, READ_CAPACITY, READ_BUFFER, READ_6, READ_10, READ_12,
	READ_16, MODE_SENSE, MODE_SENSE_10, LOG_SENSE, REPORT_LUNS,
	SERVICE_ACTION_IN, RECEIVE_DIAGNOSTIC, READ_LONG, MAINTENANCE_IN_CMD
};

static int
sg_allow_access(unsigned char opcode, char dev_type)
{
	int k;

	if (TYPE_SCANNER == dev_type)	/* TYPE_ROM maybe burner */
		return 1;
	for (k = 0; k < sizeof (allow_ops); ++k) {
		if (opcode == allow_ops[k])
			return 1;
	}
	return 0;
}

#ifdef CONFIG_SCSI_PROC_FS
static int
sg_idr_max_id(int id, void *p, void *data)
{
	int *k = data;

	if (*k < id)
		*k = id;

	return 0;
}

static int
sg_last_dev(void)
{
	int k = 0;
	unsigned long iflags;

	read_lock_irqsave(&sg_index_lock, iflags);
	idr_for_each(&sg_index_idr, sg_idr_max_id, &k);
	read_unlock_irqrestore(&sg_index_lock, iflags);
	return k + 1;		/* origin 1 */
}
#endif

static Sg_device *
sg_get_dev(int dev)
{
	Sg_device *sdp;
	unsigned long iflags;

	read_lock_irqsave(&sg_index_lock, iflags);
	sdp = idr_find(&sg_index_idr, dev);
	read_unlock_irqrestore(&sg_index_lock, iflags);

	return sdp;
}

#ifdef CONFIG_SCSI_PROC_FS

static struct proc_dir_entry *sg_proc_sgp = NULL;

static char sg_proc_sg_dirname[] = "scsi/sg";

static int sg_proc_seq_show_int(struct seq_file *s, void *v);

static int sg_proc_single_open_adio(struct inode *inode, struct file *file);
static ssize_t sg_proc_write_adio(struct file *filp, const char __user *buffer,
			          size_t count, loff_t *off);
static struct file_operations adio_fops = {
	/* .owner, .read and .llseek added in sg_proc_init() */
	.open = sg_proc_single_open_adio,
	.write = sg_proc_write_adio,
	.release = single_release,
};

static int sg_proc_single_open_dressz(struct inode *inode, struct file *file);
static ssize_t sg_proc_write_dressz(struct file *filp, 
		const char __user *buffer, size_t count, loff_t *off);
static struct file_operations dressz_fops = {
	.open = sg_proc_single_open_dressz,
	.write = sg_proc_write_dressz,
	.release = single_release,
};

static int sg_proc_seq_show_version(struct seq_file *s, void *v);
static int sg_proc_single_open_version(struct inode *inode, struct file *file);
static struct file_operations version_fops = {
	.open = sg_proc_single_open_version,
	.release = single_release,
};

static int sg_proc_seq_show_devhdr(struct seq_file *s, void *v);
static int sg_proc_single_open_devhdr(struct inode *inode, struct file *file);
static struct file_operations devhdr_fops = {
	.open = sg_proc_single_open_devhdr,
	.release = single_release,
};

static int sg_proc_seq_show_dev(struct seq_file *s, void *v);
static int sg_proc_open_dev(struct inode *inode, struct file *file);
static void * dev_seq_start(struct seq_file *s, loff_t *pos);
static void * dev_seq_next(struct seq_file *s, void *v, loff_t *pos);
static void dev_seq_stop(struct seq_file *s, void *v);
static struct file_operations dev_fops = {
	.open = sg_proc_open_dev,
	.release = seq_release,
};
static struct seq_operations dev_seq_ops = {
	.start = dev_seq_start,
	.next  = dev_seq_next,
	.stop  = dev_seq_stop,
	.show  = sg_proc_seq_show_dev,
};

static int sg_proc_seq_show_devstrs(struct seq_file *s, void *v);
static int sg_proc_open_devstrs(struct inode *inode, struct file *file);
static struct file_operations devstrs_fops = {
	.open = sg_proc_open_devstrs,
	.release = seq_release,
};
static struct seq_operations devstrs_seq_ops = {
	.start = dev_seq_start,
	.next  = dev_seq_next,
	.stop  = dev_seq_stop,
	.show  = sg_proc_seq_show_devstrs,
};

static int sg_proc_seq_show_debug(struct seq_file *s, void *v);
static int sg_proc_open_debug(struct inode *inode, struct file *file);
static struct file_operations debug_fops = {
	.open = sg_proc_open_debug,
	.release = seq_release,
};
static struct seq_operations debug_seq_ops = {
	.start = dev_seq_start,
	.next  = dev_seq_next,
	.stop  = dev_seq_stop,
	.show  = sg_proc_seq_show_debug,
};


struct sg_proc_leaf {
	const char * name;
	struct file_operations * fops;
};

static struct sg_proc_leaf sg_proc_leaf_arr[] = {
	{"allow_dio", &adio_fops},
	{"debug", &debug_fops},
	{"def_reserved_size", &dressz_fops},
	{"device_hdr", &devhdr_fops},
	{"devices", &dev_fops},
	{"device_strs", &devstrs_fops},
	{"version", &version_fops}
};

static int
sg_proc_init(void)
{
	int k, mask;
	int num_leaves = ARRAY_SIZE(sg_proc_leaf_arr);
	struct proc_dir_entry *pdep;
	struct sg_proc_leaf * leaf;

	sg_proc_sgp = proc_mkdir(sg_proc_sg_dirname, NULL);
	if (!sg_proc_sgp)
		return 1;
	for (k = 0; k < num_leaves; ++k) {
		leaf = &sg_proc_leaf_arr[k];
		mask = leaf->fops->write ? S_IRUGO | S_IWUSR : S_IRUGO;
		pdep = create_proc_entry(leaf->name, mask, sg_proc_sgp);
		if (pdep) {
			leaf->fops->owner = THIS_MODULE,
			leaf->fops->read = seq_read,
			leaf->fops->llseek = seq_lseek,
			pdep->proc_fops = leaf->fops;
		}
	}
	return 0;
}

static void
sg_proc_cleanup(void)
{
	int k;
	int num_leaves = ARRAY_SIZE(sg_proc_leaf_arr);

	if (!sg_proc_sgp)
		return;
	for (k = 0; k < num_leaves; ++k)
		remove_proc_entry(sg_proc_leaf_arr[k].name, sg_proc_sgp);
	remove_proc_entry(sg_proc_sg_dirname, NULL);
}


static int sg_proc_seq_show_int(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n", *((int *)s->private));
	return 0;
}

static int sg_proc_single_open_adio(struct inode *inode, struct file *file)
{
	return single_open(file, sg_proc_seq_show_int, &sg_allow_dio);
}

static ssize_t 
sg_proc_write_adio(struct file *filp, const char __user *buffer,
		   size_t count, loff_t *off)
{
	int num;
	char buff[11];

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
		return -EACCES;
	num = (count < 10) ? count : 10;
	if (copy_from_user(buff, buffer, num))
		return -EFAULT;
	buff[num] = '\0';
	sg_allow_dio = simple_strtoul(buff, NULL, 10) ? 1 : 0;
	return count;
}

static int sg_proc_single_open_dressz(struct inode *inode, struct file *file)
{
	return single_open(file, sg_proc_seq_show_int, &sg_big_buff);
}

static ssize_t 
sg_proc_write_dressz(struct file *filp, const char __user *buffer,
		     size_t count, loff_t *off)
{
	int num;
	unsigned long k = ULONG_MAX;
	char buff[11];

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
		return -EACCES;
	num = (count < 10) ? count : 10;
	if (copy_from_user(buff, buffer, num))
		return -EFAULT;
	buff[num] = '\0';
	k = simple_strtoul(buff, NULL, 10);
	if (k <= 1048576) {	/* limit "big buff" to 1 MB */
		sg_big_buff = k;
		return count;
	}
	return -ERANGE;
}

static int sg_proc_seq_show_version(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\t%s [%s]\n", sg_version_num, SG_VERSION_STR,
		   sg_version_date);
	return 0;
}

static int sg_proc_single_open_version(struct inode *inode, struct file *file)
{
	return single_open(file, sg_proc_seq_show_version, NULL);
}

static int sg_proc_seq_show_devhdr(struct seq_file *s, void *v)
{
	seq_printf(s, "host\tchan\tid\tlun\ttype\topens\tqdepth\tbusy\t"
		   "online\n");
	return 0;
}

static int sg_proc_single_open_devhdr(struct inode *inode, struct file *file)
{
	return single_open(file, sg_proc_seq_show_devhdr, NULL);
}

struct sg_proc_deviter {
	loff_t	index;
	size_t	max;
};

static void * dev_seq_start(struct seq_file *s, loff_t *pos)
{
	struct sg_proc_deviter * it = kmalloc(sizeof(*it), GFP_KERNEL);

	s->private = it;
	if (! it)
		return NULL;

	it->index = *pos;
	it->max = sg_last_dev();
	if (it->index >= it->max)
		return NULL;
	return it;
}

static void * dev_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct sg_proc_deviter * it = s->private;

	*pos = ++it->index;
	return (it->index < it->max) ? it : NULL;
}

static void dev_seq_stop(struct seq_file *s, void *v)
{
	kfree(s->private);
}

static int sg_proc_open_dev(struct inode *inode, struct file *file)
{
        return seq_open(file, &dev_seq_ops);
}

static int sg_proc_seq_show_dev(struct seq_file *s, void *v)
{
	struct sg_proc_deviter * it = (struct sg_proc_deviter *) v;
	Sg_device *sdp;
	struct scsi_device *scsidp;

	sdp = it ? sg_get_dev(it->index) : NULL;
	if (sdp && (scsidp = sdp->device) && (!sdp->detached))
		seq_printf(s, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
			      scsidp->host->host_no, scsidp->channel,
			      scsidp->id, scsidp->lun, (int) scsidp->type,
			      1,
			      (int) scsidp->queue_depth,
			      (int) scsidp->device_busy,
			      (int) scsi_device_online(scsidp));
	else
		seq_printf(s, "-1\t-1\t-1\t-1\t-1\t-1\t-1\t-1\t-1\n");
	return 0;
}

static int sg_proc_open_devstrs(struct inode *inode, struct file *file)
{
        return seq_open(file, &devstrs_seq_ops);
}

static int sg_proc_seq_show_devstrs(struct seq_file *s, void *v)
{
	struct sg_proc_deviter * it = (struct sg_proc_deviter *) v;
	Sg_device *sdp;
	struct scsi_device *scsidp;

	sdp = it ? sg_get_dev(it->index) : NULL;
	if (sdp && (scsidp = sdp->device) && (!sdp->detached))
		seq_printf(s, "%8.8s\t%16.16s\t%4.4s\n",
			   scsidp->vendor, scsidp->model, scsidp->rev);
	else
		seq_printf(s, "<no active device>\n");
	return 0;
}

static void sg_proc_debug_helper(struct seq_file *s, Sg_device * sdp)
{
	int k, m, new_interface, blen, usg;
	Sg_request *srp;
	Sg_fd *fp;
	const sg_io_hdr_t *hp;
	const char * cp;
	unsigned int ms;

	for (k = 0; (fp = sg_get_nth_sfp(sdp, k)); ++k) {
		seq_printf(s, "   FD(%d): timeout=%dms bufflen=%d "
			   "(res)sgat=%d low_dma=%d\n", k + 1,
			   jiffies_to_msecs(fp->timeout),
			   fp->reserve.bufflen,
			   (int) fp->reserve.k_use_sg,
			   (int) fp->low_dma);
		seq_printf(s, "   cmd_q=%d f_packid=%d k_orphan=%d closed=%d\n",
			   (int) fp->cmd_q, (int) fp->force_packid,
			   (int) fp->keep_orphan, (int) fp->closed);
		for (m = 0; (srp = sg_get_nth_request(fp, m)); ++m) {
			hp = &srp->header;
			new_interface = (hp->interface_id == '\0') ? 0 : 1;
			if (srp->res_used) {
				if (new_interface && 
				    (SG_FLAG_MMAP_IO & hp->flags))
					cp = "     mmap>> ";
				else
					cp = "     rb>> ";
			} else {
				if (SG_INFO_DIRECT_IO_MASK & hp->info)
					cp = "     dio>> ";
				else
					cp = "     ";
			}
			seq_printf(s, cp);
			blen = srp->data.bufflen;
			usg = srp->data.k_use_sg;
			seq_printf(s, srp->done ? 
				   ((1 == srp->done) ?  "rcv:" : "fin:")
				   : "act:");
			seq_printf(s, " id=%d blen=%d",
				   srp->header.pack_id, blen);
			if (srp->done)
				seq_printf(s, " dur=%d", hp->duration);
			else {
				ms = jiffies_to_msecs(jiffies);
				seq_printf(s, " t_o/elap=%d/%d",
					(new_interface ? hp->timeout :
						  jiffies_to_msecs(fp->timeout)),
					(ms > hp->duration ? ms - hp->duration : 0));
			}
			seq_printf(s, "ms sgat=%d op=0x%02x\n", usg,
				   (int) srp->data.cmd_opcode);
		}
		if (0 == m)
			seq_printf(s, "     No requests active\n");
	}
}

static int sg_proc_open_debug(struct inode *inode, struct file *file)
{
        return seq_open(file, &debug_seq_ops);
}

static int sg_proc_seq_show_debug(struct seq_file *s, void *v)
{
	struct sg_proc_deviter * it = (struct sg_proc_deviter *) v;
	Sg_device *sdp;

	if (it && (0 == it->index)) {
		seq_printf(s, "max_active_device=%d(origin 1)\n",
			   (int)it->max);
		seq_printf(s, " def_reserved_size=%d\n", sg_big_buff);
	}
	sdp = it ? sg_get_dev(it->index) : NULL;
	if (sdp) {
		struct scsi_device *scsidp = sdp->device;

		if (NULL == scsidp) {
			seq_printf(s, "device %d detached ??\n", 
				   (int)it->index);
			return 0;
		}

		if (sg_get_nth_sfp(sdp, 0)) {
			seq_printf(s, " >>> device=%s ",
				sdp->disk->disk_name);
			if (sdp->detached)
				seq_printf(s, "detached pending close ");
			else
				seq_printf
				    (s, "scsi%d chan=%d id=%d lun=%d   em=%d",
				     scsidp->host->host_no,
				     scsidp->channel, scsidp->id,
				     scsidp->lun,
				     scsidp->host->hostt->emulated);
			seq_printf(s, " sg_tablesize=%d excl=%d\n",
				   sdp->sg_tablesize, sdp->exclude);
		}
		sg_proc_debug_helper(s, sdp);
	}
	return 0;
}

#endif				/* CONFIG_SCSI_PROC_FS */

module_init(init_sg);
module_exit(exit_sg);
