/*
 * Freescale UUT driver
 *
 * Copyright 2008-2014 Freescale Semiconductor, Inc.
 * Copyright 2008-2009 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __FSL_UPDATER_H
#define __FSL_UPDATER_H

#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/ioctl.h>
/* #include <mach/hardware.h> */

static int utp_init(struct fsg_dev *fsg);
static void utp_exit(struct fsg_dev *fsg);
static ssize_t utp_file_read(struct file *file,
			     char __user *buf,
			     size_t size,
			     loff_t *off);

static ssize_t utp_file_write(struct file *file,
			      const char __user *buf,
			      size_t size,
			      loff_t *off);

static long utp_ioctl(struct file *file,
	      unsigned int cmd, unsigned long arg);
static struct utp_user_data *utp_user_data_alloc(size_t size);
static void utp_user_data_free(struct utp_user_data *uud);
static int utp_get_sense(struct fsg_dev *fsg);
static int utp_do_read(struct fsg_dev *fsg, void *data, size_t size);
static int utp_do_write(struct fsg_dev *fsg, void *data, size_t size);
static inline void utp_set_sense(struct fsg_dev *fsg, u16 code, u64 reply);
static int utp_handle_message(struct fsg_dev *fsg,
			      char *cdb_data,
			      int default_reply);

#define UTP_REPLY_PASS		0
#define UTP_REPLY_EXIT		0x8001
#define UTP_REPLY_BUSY		0x8002
#define UTP_REPLY_SIZE		0x8003
#define UTP_SENSE_KEY		9

#define UTP_MINOR		222
/* MISC_DYNAMIC_MINOR would be better, but... */

#define UTP_COMMAND_SIZE	80

#define UTP_SS_EXIT(fsg, r)	utp_set_sense(fsg, UTP_REPLY_EXIT, (u64)r)
#define UTP_SS_PASS(fsg)	utp_set_sense(fsg, UTP_REPLY_PASS, 0)
#define UTP_SS_BUSY(fsg, r)	utp_set_sense(fsg, UTP_REPLY_BUSY, (u64)r)
#define UTP_SS_SIZE(fsg, r)	utp_set_sense(fsg, UTP_REPLY_SIZE, (u64)r)

#define	UTP_IOCTL_BASE	'U'
#define	UTP_GET_CPU_ID	_IOR(UTP_IOCTL_BASE, 0, int)
/* the structure of utp message which is mapped to 16-byte SCSI CBW's CDB */
#pragma pack(1)
struct utp_msg {
	u8  f0;
	u8  utp_msg_type;
	u32 utp_msg_tag;
	union {
		struct {
			u32 param_lsb;
			u32 param_msb;
		};
		u64 param;
	};
};

enum utp_msg_type {
	UTP_POLL = 0,
	UTP_EXEC,
	UTP_GET,
	UTP_PUT,
};

static struct utp_context {
	wait_queue_head_t wq;
	wait_queue_head_t list_full_wq;
	struct mutex lock;
	struct list_head read;
	struct list_head write;
	u32 sd, sdinfo, sdinfo_h;			/* sense data */
	int processed;
	u8 *buffer;
	u32 counter;
	u64 utp_version;
} utp_context;

static const struct file_operations utp_fops = {
	.open	= nonseekable_open,
	.read	= utp_file_read,
	.write	= utp_file_write,
	/* .ioctl  = utp_ioctl, */
	.unlocked_ioctl  = utp_ioctl,
};

static struct miscdevice utp_dev = {
	.minor	= UTP_MINOR,
	.name	= "utp",
	.fops	= &utp_fops,
};

#define UTP_FLAG_COMMAND	0x00000001
#define UTP_FLAG_DATA		0x00000002
#define UTP_FLAG_STATUS		0x00000004
#define UTP_FLAG_REPORT_BUSY	0x10000000
struct utp_message {
	u32	flags;
	size_t	size;
	union {
		struct {
			u64 payload;
			char command[1];
		};
		struct {
			size_t bufsize;
			u8 data[1];
		};
		u32 status;
	};
};

struct utp_user_data {
	struct  list_head	link;
	struct  utp_message	data;
};
#pragma pack()

static inline struct utp_context *UTP_CTX(struct fsg_dev *fsg)
{
	return (struct utp_context *)fsg->utp;
}

#endif /* __FSL_UPDATER_H */

