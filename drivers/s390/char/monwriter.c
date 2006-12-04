/*
 * drivers/s390/char/monwriter.c
 *
 * Character device driver for writing z/VM *MONITOR service records.
 *
 * Copyright (C) IBM Corp. 2006
 *
 * Author(s): Melissa Howland <Melissa.Howland@us.ibm.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/ctype.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/appldata.h>
#include <asm/monwriter.h>

#define MONWRITE_MAX_DATALEN	4024

static int mon_max_bufs = 255;
static int mon_buf_count;

struct mon_buf {
	struct list_head list;
	struct monwrite_hdr hdr;
	int diag_done;
	char *data;
};

struct mon_private {
	struct list_head list;
	struct monwrite_hdr hdr;
	size_t hdr_to_read;
	size_t data_to_read;
	struct mon_buf *current_buf;
};

/*
 * helper functions
 */

static int monwrite_diag(struct monwrite_hdr *myhdr, char *buffer, int fcn)
{
	struct appldata_product_id id;
	int rc;

	strcpy(id.prod_nr, "LNXAPPL");
	id.prod_fn = myhdr->applid;
	id.record_nr = myhdr->record_num;
	id.version_nr = myhdr->version;
	id.release_nr = myhdr->release;
	id.mod_lvl = myhdr->mod_level;
	rc = appldata_asm(&id, fcn, (void *) buffer, myhdr->datalen);
	if (rc <= 0)
		return rc;
	if (rc == 5)
		return -EPERM;
	printk("DIAG X'DC' error with return code: %i\n", rc);
	return -EINVAL;
}

static inline struct mon_buf *monwrite_find_hdr(struct mon_private *monpriv,
						struct monwrite_hdr *monhdr)
{
	struct mon_buf *entry, *next;

	list_for_each_entry_safe(entry, next, &monpriv->list, list)
		if ((entry->hdr.mon_function == monhdr->mon_function ||
		     monhdr->mon_function == MONWRITE_STOP_INTERVAL) &&
		    entry->hdr.applid == monhdr->applid &&
		    entry->hdr.record_num == monhdr->record_num &&
		    entry->hdr.version == monhdr->version &&
		    entry->hdr.release == monhdr->release &&
		    entry->hdr.mod_level == monhdr->mod_level)
			return entry;

	return NULL;
}

static int monwrite_new_hdr(struct mon_private *monpriv)
{
	struct monwrite_hdr *monhdr = &monpriv->hdr;
	struct mon_buf *monbuf;
	int rc;

	if (monhdr->datalen > MONWRITE_MAX_DATALEN ||
	    monhdr->mon_function > MONWRITE_START_CONFIG ||
	    monhdr->hdrlen != sizeof(struct monwrite_hdr))
		return -EINVAL;
	monbuf = NULL;
	if (monhdr->mon_function != MONWRITE_GEN_EVENT)
		monbuf = monwrite_find_hdr(monpriv, monhdr);
	if (monbuf) {
		if (monhdr->mon_function == MONWRITE_STOP_INTERVAL) {
			monhdr->datalen = monbuf->hdr.datalen;
			rc = monwrite_diag(monhdr, monbuf->data,
					   APPLDATA_STOP_REC);
			list_del(&monbuf->list);
			mon_buf_count--;
			kfree(monbuf->data);
			kfree(monbuf);
			monbuf = NULL;
		}
	} else if (monhdr->mon_function != MONWRITE_STOP_INTERVAL) {
		if (mon_buf_count >= mon_max_bufs)
			return -ENOSPC;
		monbuf = kzalloc(sizeof(struct mon_buf), GFP_KERNEL);
		if (!monbuf)
			return -ENOMEM;
		monbuf->data = kzalloc(monhdr->datalen,
				       GFP_KERNEL | GFP_DMA);
		if (!monbuf->data) {
			kfree(monbuf);
			return -ENOMEM;
		}
		monbuf->hdr = *monhdr;
		list_add_tail(&monbuf->list, &monpriv->list);
		if (monhdr->mon_function != MONWRITE_GEN_EVENT)
			mon_buf_count++;
	}
	monpriv->current_buf = monbuf;
	return 0;
}

static int monwrite_new_data(struct mon_private *monpriv)
{
	struct monwrite_hdr *monhdr = &monpriv->hdr;
	struct mon_buf *monbuf = monpriv->current_buf;
	int rc = 0;

	switch (monhdr->mon_function) {
	case MONWRITE_START_INTERVAL:
		if (!monbuf->diag_done) {
			rc = monwrite_diag(monhdr, monbuf->data,
					   APPLDATA_START_INTERVAL_REC);
			monbuf->diag_done = 1;
		}
		break;
	case MONWRITE_START_CONFIG:
		if (!monbuf->diag_done) {
			rc = monwrite_diag(monhdr, monbuf->data,
					   APPLDATA_START_CONFIG_REC);
			monbuf->diag_done = 1;
		}
		break;
	case MONWRITE_GEN_EVENT:
		rc = monwrite_diag(monhdr, monbuf->data,
				   APPLDATA_GEN_EVENT_REC);
		list_del(&monpriv->current_buf->list);
		kfree(monpriv->current_buf->data);
		kfree(monpriv->current_buf);
		monpriv->current_buf = NULL;
		break;
	default:
		/* monhdr->mon_function is checked in monwrite_new_hdr */
		BUG();
	}
	return rc;
}

/*
 * file operations
 */

static int monwrite_open(struct inode *inode, struct file *filp)
{
	struct mon_private *monpriv;

	monpriv = kzalloc(sizeof(struct mon_private), GFP_KERNEL);
	if (!monpriv)
		return -ENOMEM;
	INIT_LIST_HEAD(&monpriv->list);
	monpriv->hdr_to_read = sizeof(monpriv->hdr);
	filp->private_data = monpriv;
	return nonseekable_open(inode, filp);
}

static int monwrite_close(struct inode *inode, struct file *filp)
{
	struct mon_private *monpriv = filp->private_data;
	struct mon_buf *entry, *next;

	list_for_each_entry_safe(entry, next, &monpriv->list, list) {
		if (entry->hdr.mon_function != MONWRITE_GEN_EVENT)
			monwrite_diag(&entry->hdr, entry->data,
				      APPLDATA_STOP_REC);
		mon_buf_count--;
		list_del(&entry->list);
		kfree(entry->data);
		kfree(entry);
	}
	kfree(monpriv);
	return 0;
}

static ssize_t monwrite_write(struct file *filp, const char __user *data,
			      size_t count, loff_t *ppos)
{
	struct mon_private *monpriv = filp->private_data;
	size_t len, written;
	void *to;
	int rc;

	for (written = 0; written < count; ) {
		if (monpriv->hdr_to_read) {
			len = min(count - written, monpriv->hdr_to_read);
			to = (char *) &monpriv->hdr +
				sizeof(monpriv->hdr) - monpriv->hdr_to_read;
			if (copy_from_user(to, data + written, len)) {
				rc = -EFAULT;
				goto out_error;
			}
			monpriv->hdr_to_read -= len;
			written += len;
			if (monpriv->hdr_to_read > 0)
				continue;
			rc = monwrite_new_hdr(monpriv);
			if (rc)
				goto out_error;
			monpriv->data_to_read = monpriv->current_buf ?
				monpriv->current_buf->hdr.datalen : 0;
		}

		if (monpriv->data_to_read) {
			len = min(count - written, monpriv->data_to_read);
			to = monpriv->current_buf->data +
				monpriv->hdr.datalen - monpriv->data_to_read;
			if (copy_from_user(to, data + written, len)) {
				rc = -EFAULT;
				goto out_error;
			}
			monpriv->data_to_read -= len;
			written += len;
			if (monpriv->data_to_read > 0)
				continue;
			rc = monwrite_new_data(monpriv);
			if (rc)
				goto out_error;
		}
		monpriv->hdr_to_read = sizeof(monpriv->hdr);
	}
	return written;

out_error:
	monpriv->data_to_read = 0;
	monpriv->hdr_to_read = sizeof(struct monwrite_hdr);
	return rc;
}

static struct file_operations monwrite_fops = {
	.owner	 = THIS_MODULE,
	.open	 = &monwrite_open,
	.release = &monwrite_close,
	.write	 = &monwrite_write,
};

static struct miscdevice mon_dev = {
	.name	= "monwriter",
	.fops	= &monwrite_fops,
	.minor	= MISC_DYNAMIC_MINOR,
};

/*
 * module init/exit
 */

static int __init mon_init(void)
{
	if (MACHINE_IS_VM)
		return misc_register(&mon_dev);
	else
		return -ENODEV;
}

static void __exit mon_exit(void)
{
	WARN_ON(misc_deregister(&mon_dev) != 0);
}

module_init(mon_init);
module_exit(mon_exit);

module_param_named(max_bufs, mon_max_bufs, int, 0644);
MODULE_PARM_DESC(max_bufs, "Maximum number of sample monitor data buffers"
		 "that can be active at one time");

MODULE_AUTHOR("Melissa Howland <Melissa.Howland@us.ibm.com>");
MODULE_DESCRIPTION("Character device driver for writing z/VM "
		   "APPLDATA monitor records.");
MODULE_LICENSE("GPL");
