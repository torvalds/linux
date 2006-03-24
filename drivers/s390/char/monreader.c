/*
 * drivers/s390/char/monreader.c
 *
 * Character device driver for reading z/VM *MONITOR service records.
 *
 * Copyright (C) 2004 IBM Corporation, IBM Deutschland Entwicklung GmbH.
 *
 * Author: Gerald Schaefer <geraldsc@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/ebcdic.h>
#include <asm/extmem.h>
#include <linux/poll.h>
#include "../net/iucv.h"


//#define MON_DEBUG			/* Debug messages on/off */

#define MON_NAME "monreader"

#define P_INFO(x...)	printk(KERN_INFO MON_NAME " info: " x)
#define P_ERROR(x...)	printk(KERN_ERR MON_NAME " error: " x)
#define P_WARNING(x...)	printk(KERN_WARNING MON_NAME " warning: " x)

#ifdef MON_DEBUG
#define P_DEBUG(x...)   printk(KERN_DEBUG MON_NAME " debug: " x)
#else
#define P_DEBUG(x...)   do {} while (0)
#endif

#define MON_COLLECT_SAMPLE 0x80
#define MON_COLLECT_EVENT  0x40
#define MON_SERVICE	   "*MONITOR"
#define MON_IN_USE	   0x01
#define MON_MSGLIM	   255

static char mon_dcss_name[9] = "MONDCSS\0";

struct mon_msg {
	u32 pos;
	u32 mca_offset;
	iucv_MessagePending local_eib;
	char msglim_reached;
	char replied_msglim;
};

struct mon_private {
	u16 pathid;
	iucv_handle_t iucv_handle;
	struct mon_msg *msg_array[MON_MSGLIM];
	unsigned int   write_index;
	unsigned int   read_index;
	atomic_t msglim_count;
	atomic_t read_ready;
	atomic_t iucv_connected;
	atomic_t iucv_severed;
};

static unsigned long mon_in_use = 0;

static unsigned long mon_dcss_start;
static unsigned long mon_dcss_end;

static DECLARE_WAIT_QUEUE_HEAD(mon_read_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(mon_conn_wait_queue);

static u8 iucv_host[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static u8 user_data_connect[16] = {
	/* Version code, must be 0x01 for shared mode */
	0x01,
	/* what to collect */
	MON_COLLECT_SAMPLE | MON_COLLECT_EVENT,
	/* DCSS name in EBCDIC, 8 bytes padded with blanks */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static u8 user_data_sever[16] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};


/******************************************************************************
 *                             helper functions                               *
 *****************************************************************************/
/*
 * Create the 8 bytes EBCDIC DCSS segment name from
 * an ASCII name, incl. padding
 */
static inline void
dcss_mkname(char *ascii_name, char *ebcdic_name)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (ascii_name[i] == '\0')
			break;
		ebcdic_name[i] = toupper(ascii_name[i]);
	};
	for (; i < 8; i++)
		ebcdic_name[i] = ' ';
	ASCEBC(ebcdic_name, 8);
}

/*
 * print appropriate error message for segment_load()/segment_type()
 * return code
 */
static void
mon_segment_warn(int rc, char* seg_name)
{
	switch (rc) {
	case -ENOENT:
		P_WARNING("cannot load/query segment %s, does not exist\n",
			  seg_name);
		break;
	case -ENOSYS:
		P_WARNING("cannot load/query segment %s, not running on VM\n",
			  seg_name);
		break;
	case -EIO:
		P_WARNING("cannot load/query segment %s, hardware error\n",
			  seg_name);
		break;
	case -ENOTSUPP:
		P_WARNING("cannot load/query segment %s, is a multi-part "
			  "segment\n", seg_name);
		break;
	case -ENOSPC:
		P_WARNING("cannot load/query segment %s, overlaps with "
			  "storage\n", seg_name);
		break;
	case -EBUSY:
		P_WARNING("cannot load/query segment %s, overlaps with "
			  "already loaded dcss\n", seg_name);
		break;
	case -EPERM:
		P_WARNING("cannot load/query segment %s, already loaded in "
			  "incompatible mode\n", seg_name);
		break;
	case -ENOMEM:
		P_WARNING("cannot load/query segment %s, out of memory\n",
			  seg_name);
		break;
	case -ERANGE:
		P_WARNING("cannot load/query segment %s, exceeds kernel "
			  "mapping range\n", seg_name);
		break;
	default:
		P_WARNING("cannot load/query segment %s, return value %i\n",
			  seg_name, rc);
		break;
	}
}

static inline unsigned long
mon_mca_start(struct mon_msg *monmsg)
{
	return monmsg->local_eib.ln1msg1.iprmmsg1_u32;
}

static inline unsigned long
mon_mca_end(struct mon_msg *monmsg)
{
	return monmsg->local_eib.ln1msg2.ipbfln1f;
}

static inline u8
mon_mca_type(struct mon_msg *monmsg, u8 index)
{
	return *((u8 *) mon_mca_start(monmsg) + monmsg->mca_offset + index);
}

static inline u32
mon_mca_size(struct mon_msg *monmsg)
{
	return mon_mca_end(monmsg) - mon_mca_start(monmsg) + 1;
}

static inline u32
mon_rec_start(struct mon_msg *monmsg)
{
	return *((u32 *) (mon_mca_start(monmsg) + monmsg->mca_offset + 4));
}

static inline u32
mon_rec_end(struct mon_msg *monmsg)
{
	return *((u32 *) (mon_mca_start(monmsg) + monmsg->mca_offset + 8));
}

static inline int
mon_check_mca(struct mon_msg *monmsg)
{
	if ((mon_rec_end(monmsg) <= mon_rec_start(monmsg)) ||
	    (mon_rec_start(monmsg) < mon_dcss_start) ||
	    (mon_rec_end(monmsg) > mon_dcss_end) ||
	    (mon_mca_type(monmsg, 0) == 0) ||
	    (mon_mca_size(monmsg) % 12 != 0) ||
	    (mon_mca_end(monmsg) <= mon_mca_start(monmsg)) ||
	    (mon_mca_end(monmsg) > mon_dcss_end) ||
	    (mon_mca_start(monmsg) < mon_dcss_start) ||
	    ((mon_mca_type(monmsg, 1) == 0) && (mon_mca_type(monmsg, 2) == 0)))
	{
		P_DEBUG("READ, IGNORED INVALID MCA\n\n");
		return -EINVAL;
	}
	return 0;
}

static inline int
mon_send_reply(struct mon_msg *monmsg, struct mon_private *monpriv)
{
	u8 prmmsg[8];
	int rc;

	P_DEBUG("read, REPLY: pathid = 0x%04X, msgid = 0x%08X, trgcls = "
		"0x%08X\n\n",
		monmsg->local_eib.ippathid, monmsg->local_eib.ipmsgid,
		monmsg->local_eib.iptrgcls);
	rc = iucv_reply_prmmsg(monmsg->local_eib.ippathid,
				monmsg->local_eib.ipmsgid,
				monmsg->local_eib.iptrgcls,
				0, prmmsg);
	atomic_dec(&monpriv->msglim_count);
	if (likely(!monmsg->msglim_reached)) {
		monmsg->pos = 0;
		monmsg->mca_offset = 0;
		monpriv->read_index = (monpriv->read_index + 1) %
				      MON_MSGLIM;
		atomic_dec(&monpriv->read_ready);
	} else
		monmsg->replied_msglim = 1;
	if (rc) {
		P_ERROR("read, IUCV reply failed with rc = %i\n\n", rc);
		return -EIO;
	}
	return 0;
}

static inline struct mon_private *
mon_alloc_mem(void)
{
	int i,j;
	struct mon_private *monpriv;

	monpriv = kzalloc(sizeof(struct mon_private), GFP_KERNEL);
	if (!monpriv) {
		P_ERROR("no memory for monpriv\n");
		return NULL;
	}
	for (i = 0; i < MON_MSGLIM; i++) {
		monpriv->msg_array[i] = kzalloc(sizeof(struct mon_msg),
						    GFP_KERNEL);
		if (!monpriv->msg_array[i]) {
			P_ERROR("open, no memory for msg_array\n");
			for (j = 0; j < i; j++)
				kfree(monpriv->msg_array[j]);
			return NULL;
		}
	}
	return monpriv;
}

static inline void
mon_read_debug(struct mon_msg *monmsg, struct mon_private *monpriv)
{
#ifdef MON_DEBUG
	u8 msg_type[2], mca_type;
	unsigned long records_len;

	records_len = mon_rec_end(monmsg) - mon_rec_start(monmsg) + 1;

	memcpy(msg_type, &monmsg->local_eib.iptrgcls, 2);
	EBCASC(msg_type, 2);
	mca_type = mon_mca_type(monmsg, 0);
	EBCASC(&mca_type, 1);

	P_DEBUG("read, mon_read_index = %i, mon_write_index = %i\n",
		monpriv->read_index, monpriv->write_index);
	P_DEBUG("read, pathid = 0x%04X, msgid = 0x%08X, trgcls = 0x%08X\n",
		monmsg->local_eib.ippathid, monmsg->local_eib.ipmsgid,
		monmsg->local_eib.iptrgcls);
	P_DEBUG("read, msg_type = '%c%c', mca_type = '%c' / 0x%X / 0x%X\n",
		msg_type[0], msg_type[1], mca_type ? mca_type : 'X',
		mon_mca_type(monmsg, 1), mon_mca_type(monmsg, 2));
	P_DEBUG("read, MCA: start = 0x%lX, end = 0x%lX\n",
		mon_mca_start(monmsg), mon_mca_end(monmsg));
	P_DEBUG("read, REC: start = 0x%X, end = 0x%X, len = %lu\n\n",
		mon_rec_start(monmsg), mon_rec_end(monmsg), records_len);
	if (mon_mca_size(monmsg) > 12)
		P_DEBUG("READ, MORE THAN ONE MCA\n\n");
#endif
}

static inline void
mon_next_mca(struct mon_msg *monmsg)
{
	if (likely((mon_mca_size(monmsg) - monmsg->mca_offset) == 12))
		return;
	P_DEBUG("READ, NEXT MCA\n\n");
	monmsg->mca_offset += 12;
	monmsg->pos = 0;
}

static inline struct mon_msg *
mon_next_message(struct mon_private *monpriv)
{
	struct mon_msg *monmsg;

	if (!atomic_read(&monpriv->read_ready))
		return NULL;
	monmsg = monpriv->msg_array[monpriv->read_index];
	if (unlikely(monmsg->replied_msglim)) {
		monmsg->replied_msglim = 0;
		monmsg->msglim_reached = 0;
		monmsg->pos = 0;
		monmsg->mca_offset = 0;
		P_WARNING("read, message limit reached\n");
		monpriv->read_index = (monpriv->read_index + 1) %
				      MON_MSGLIM;
		atomic_dec(&monpriv->read_ready);
		return ERR_PTR(-EOVERFLOW);
	}
	return monmsg;
}


/******************************************************************************
 *                               IUCV handler                                 *
 *****************************************************************************/
static void
mon_iucv_ConnectionComplete(iucv_ConnectionComplete *eib, void *pgm_data)
{
	struct mon_private *monpriv = (struct mon_private *) pgm_data;

	P_DEBUG("IUCV connection completed\n");
	P_DEBUG("IUCV ACCEPT (from *MONITOR): Version = 0x%02X, Event = "
		"0x%02X, Sample = 0x%02X\n",
		eib->ipuser[0], eib->ipuser[1], eib->ipuser[2]);
	atomic_set(&monpriv->iucv_connected, 1);
	wake_up(&mon_conn_wait_queue);
}

static void
mon_iucv_ConnectionSevered(iucv_ConnectionSevered *eib, void *pgm_data)
{
	struct mon_private *monpriv = (struct mon_private *) pgm_data;

	P_ERROR("IUCV connection severed with rc = 0x%X\n",
		(u8) eib->ipuser[0]);
	atomic_set(&monpriv->iucv_severed, 1);
	wake_up(&mon_conn_wait_queue);
	wake_up_interruptible(&mon_read_wait_queue);
}

static void
mon_iucv_MessagePending(iucv_MessagePending *eib, void *pgm_data)
{
	struct mon_private *monpriv = (struct mon_private *) pgm_data;

	P_DEBUG("IUCV message pending\n");
	memcpy(&monpriv->msg_array[monpriv->write_index]->local_eib, eib,
	       sizeof(iucv_MessagePending));
	if (atomic_inc_return(&monpriv->msglim_count) == MON_MSGLIM) {
		P_WARNING("IUCV message pending, message limit (%i) reached\n",
			  MON_MSGLIM);
		monpriv->msg_array[monpriv->write_index]->msglim_reached = 1;
	}
	monpriv->write_index = (monpriv->write_index + 1) % MON_MSGLIM;
	atomic_inc(&monpriv->read_ready);
	wake_up_interruptible(&mon_read_wait_queue);
}

static iucv_interrupt_ops_t mon_iucvops = {
	.ConnectionComplete = mon_iucv_ConnectionComplete,
	.ConnectionSevered  = mon_iucv_ConnectionSevered,
	.MessagePending     = mon_iucv_MessagePending,
};

/******************************************************************************
 *                               file operations                              *
 *****************************************************************************/
static int
mon_open(struct inode *inode, struct file *filp)
{
	int rc, i;
	struct mon_private *monpriv;

	/*
	 * only one user allowed
	 */
	if (test_and_set_bit(MON_IN_USE, &mon_in_use))
		return -EBUSY;

	monpriv = mon_alloc_mem();
	if (!monpriv)
		return -ENOMEM;

	/*
	 * Register with IUCV and connect to *MONITOR service
	 */
	monpriv->iucv_handle = iucv_register_program("my_monreader    ",
							MON_SERVICE,
							NULL,
							&mon_iucvops,
							monpriv);
	if (!monpriv->iucv_handle) {
		P_ERROR("failed to register with iucv driver\n");
		rc = -EIO;
		goto out_error;
	}
	P_INFO("open, registered with IUCV\n");

	rc = iucv_connect(&monpriv->pathid, MON_MSGLIM, user_data_connect,
			  MON_SERVICE, iucv_host, IPRMDATA, NULL, NULL,
			  monpriv->iucv_handle, NULL);
	if (rc) {
		P_ERROR("iucv connection to *MONITOR failed with "
			"IPUSER SEVER code = %i\n", rc);
		rc = -EIO;
		goto out_unregister;
	}
	/*
	 * Wait for connection confirmation
	 */
	wait_event(mon_conn_wait_queue,
		   atomic_read(&monpriv->iucv_connected) ||
		   atomic_read(&monpriv->iucv_severed));
	if (atomic_read(&monpriv->iucv_severed)) {
		atomic_set(&monpriv->iucv_severed, 0);
		atomic_set(&monpriv->iucv_connected, 0);
		rc = -EIO;
		goto out_unregister;
	}
	P_INFO("open, established connection to *MONITOR service\n\n");
	filp->private_data = monpriv;
	return nonseekable_open(inode, filp);

out_unregister:
	iucv_unregister_program(monpriv->iucv_handle);
out_error:
	for (i = 0; i < MON_MSGLIM; i++)
		kfree(monpriv->msg_array[i]);
	kfree(monpriv);
	clear_bit(MON_IN_USE, &mon_in_use);
	return rc;
}

static int
mon_close(struct inode *inode, struct file *filp)
{
	int rc, i;
	struct mon_private *monpriv = filp->private_data;

	/*
	 * Close IUCV connection and unregister
	 */
	rc = iucv_sever(monpriv->pathid, user_data_sever);
	if (rc)
		P_ERROR("close, iucv_sever failed with rc = %i\n", rc);
	else
		P_INFO("close, terminated connection to *MONITOR service\n");

	rc = iucv_unregister_program(monpriv->iucv_handle);
	if (rc)
		P_ERROR("close, iucv_unregister failed with rc = %i\n", rc);
	else
		P_INFO("close, unregistered with IUCV\n");

	atomic_set(&monpriv->iucv_severed, 0);
	atomic_set(&monpriv->iucv_connected, 0);
	atomic_set(&monpriv->read_ready, 0);
	atomic_set(&monpriv->msglim_count, 0);
	monpriv->write_index  = 0;
	monpriv->read_index   = 0;

	for (i = 0; i < MON_MSGLIM; i++)
		kfree(monpriv->msg_array[i]);
	kfree(monpriv);
	clear_bit(MON_IN_USE, &mon_in_use);
	return 0;
}

static ssize_t
mon_read(struct file *filp, char __user *data, size_t count, loff_t *ppos)
{
	struct mon_private *monpriv = filp->private_data;
	struct mon_msg *monmsg;
	int ret;
	u32 mce_start;

	monmsg = mon_next_message(monpriv);
	if (IS_ERR(monmsg))
		return PTR_ERR(monmsg);

	if (!monmsg) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(mon_read_wait_queue,
					atomic_read(&monpriv->read_ready) ||
					atomic_read(&monpriv->iucv_severed));
		if (ret)
			return ret;
		if (unlikely(atomic_read(&monpriv->iucv_severed)))
			return -EIO;
		monmsg = monpriv->msg_array[monpriv->read_index];
	}

	if (!monmsg->pos) {
		monmsg->pos = mon_mca_start(monmsg) + monmsg->mca_offset;
		mon_read_debug(monmsg, monpriv);
	}
	if (mon_check_mca(monmsg))
		goto reply;

	/* read monitor control element (12 bytes) first */
	mce_start = mon_mca_start(monmsg) + monmsg->mca_offset;
	if ((monmsg->pos >= mce_start) && (monmsg->pos < mce_start + 12)) {
		count = min(count, (size_t) mce_start + 12 - monmsg->pos);
		ret = copy_to_user(data, (void *) (unsigned long) monmsg->pos,
				   count);
		if (ret)
			return -EFAULT;
		monmsg->pos += count;
		if (monmsg->pos == mce_start + 12)
			monmsg->pos = mon_rec_start(monmsg);
		goto out_copy;
	}

	/* read records */
	if (monmsg->pos <= mon_rec_end(monmsg)) {
		count = min(count, (size_t) mon_rec_end(monmsg) - monmsg->pos
					    + 1);
		ret = copy_to_user(data, (void *) (unsigned long) monmsg->pos,
				   count);
		if (ret)
			return -EFAULT;
		monmsg->pos += count;
		if (monmsg->pos > mon_rec_end(monmsg))
			mon_next_mca(monmsg);
		goto out_copy;
	}
reply:
	ret = mon_send_reply(monmsg, monpriv);
	return ret;

out_copy:
	*ppos += count;
	return count;
}

static unsigned int
mon_poll(struct file *filp, struct poll_table_struct *p)
{
	struct mon_private *monpriv = filp->private_data;

	poll_wait(filp, &mon_read_wait_queue, p);
	if (unlikely(atomic_read(&monpriv->iucv_severed)))
		return POLLERR;
	if (atomic_read(&monpriv->read_ready))
		return POLLIN | POLLRDNORM;
	return 0;
}

static struct file_operations mon_fops = {
	.owner   = THIS_MODULE,
	.open    = &mon_open,
	.release = &mon_close,
	.read    = &mon_read,
	.poll    = &mon_poll,
};

static struct miscdevice mon_dev = {
	.name       = "monreader",
	.devfs_name = "monreader",
	.fops       = &mon_fops,
	.minor      = MISC_DYNAMIC_MINOR,
};

/******************************************************************************
 *                              module init/exit                              *
 *****************************************************************************/
static int __init
mon_init(void)
{
	int rc;

	if (!MACHINE_IS_VM) {
		P_ERROR("not running under z/VM, driver not loaded\n");
		return -ENODEV;
	}

	rc = segment_type(mon_dcss_name);
	if (rc < 0) {
		mon_segment_warn(rc, mon_dcss_name);
		return rc;
	}
	if (rc != SEG_TYPE_SC) {
		P_ERROR("segment %s has unsupported type, should be SC\n",
			mon_dcss_name);
		return -EINVAL;
	}

	rc = segment_load(mon_dcss_name, SEGMENT_SHARED,
			  &mon_dcss_start, &mon_dcss_end);
	if (rc < 0) {
		mon_segment_warn(rc, mon_dcss_name);
		return -EINVAL;
	}
	dcss_mkname(mon_dcss_name, &user_data_connect[8]);

	rc = misc_register(&mon_dev);
	if (rc < 0 ) {
		P_ERROR("misc_register failed, rc = %i\n", rc);
		goto out;
	}
	P_INFO("Loaded segment %s from %p to %p, size = %lu Byte\n",
		mon_dcss_name, (void *) mon_dcss_start, (void *) mon_dcss_end,
		mon_dcss_end - mon_dcss_start + 1);
	return 0;

out:
	segment_unload(mon_dcss_name);
	return rc;
}

static void __exit
mon_exit(void)
{
	segment_unload(mon_dcss_name);
	WARN_ON(misc_deregister(&mon_dev) != 0);
	return;
}


module_init(mon_init);
module_exit(mon_exit);

module_param_string(mondcss, mon_dcss_name, 9, 0444);
MODULE_PARM_DESC(mondcss, "Name of DCSS segment to be used for *MONITOR "
		 "service, max. 8 chars. Default is MONDCSS");

MODULE_AUTHOR("Gerald Schaefer <geraldsc@de.ibm.com>");
MODULE_DESCRIPTION("Character device driver for reading z/VM "
		   "monitor service records.");
MODULE_LICENSE("GPL");
