/*
 * Character device driver for writing z/VM *MONITOR service records.
 *
 * Copyright IBM Corp. 2006, 2009
 *
 * Author(s): Melissa Howland <Melissa.Howland@us.ibm.com>
 */

#define KMSG_COMPONENT "monwriter"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/ctype.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/appldata.h>
#include <asm/monwriter.h>

#define MONWRITE_MAX_DATALEN	4010

static int mon_max_bufs = 255;
static int mon_buf_count;

struct mon_buf {
	struct list_head list;
	struct monwrite_hdr hdr;
	int diag_done;
	char *data;
};

static LIST_HEAD(mon_priv_list);

struct mon_private {
	struct list_head priv_list;
	struct list_head list;
	struct monwrite_hdr hdr;
	size_t hdr_to_read;
	size_t data_to_read;
	struct mon_buf *current_buf;
	struct mutex thread_mutex;
};

/*
 * helper functions
 */

static int monwrite_diag(struct monwrite_hdr *myhdr, char *buffer, int fcn)
{
	struct appldata_product_id id;
	int rc;

	strncpy(id.prod_nr, "LNXAPPL", 7);
	id.prod_fn = myhdr->applid;
	id.record_nr = myhdr->record_num;
	id.version_nr = myhdr->version;
	id.release_nr = myhdr->release;
	id.mod_lvl = myhdr->mod_level;
	rc = appldata_asm(&id, fcn, (void *) buffer, myhdr->datalen);
	if (rc <= 0)
		return rc;
	pr_err("Writing monitor data failed with rc=%i\n", rc);
	if (rc == 5)
		return -EPERM;
	return -EINVAL;
}

static struct mon_buf *monwrite_find_hdr(struct mon_private *monpriv,
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
	int rc = 0;

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
	return rc;
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
	mutex_init(&monpriv->thread_mutex);
	filp->private_data = monpriv;
	list_add_tail(&monpriv->priv_list, &mon_priv_list);
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
	list_del(&monpriv->priv_list);
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

	mutex_lock(&monpriv->thread_mutex);
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
	mutex_unlock(&monpriv->thread_mutex);
	return written;

out_error:
	monpriv->data_to_read = 0;
	monpriv->hdr_to_read = sizeof(struct monwrite_hdr);
	mutex_unlock(&monpriv->thread_mutex);
	return rc;
}

static const struct file_operations monwrite_fops = {
	.owner	 = THIS_MODULE,
	.open	 = &monwrite_open,
	.release = &monwrite_close,
	.write	 = &monwrite_write,
	.llseek  = noop_llseek,
};

static struct miscdevice mon_dev = {
	.name	= "monwriter",
	.fops	= &monwrite_fops,
	.minor	= MISC_DYNAMIC_MINOR,
};

/*
 * suspend/resume
 */

static int monwriter_freeze(struct device *dev)
{
	struct mon_private *monpriv;
	struct mon_buf *monbuf;

	list_for_each_entry(monpriv, &mon_priv_list, priv_list) {
		list_for_each_entry(monbuf, &monpriv->list, list) {
			if (monbuf->hdr.mon_function != MONWRITE_GEN_EVENT)
				monwrite_diag(&monbuf->hdr, monbuf->data,
					      APPLDATA_STOP_REC);
		}
	}
	return 0;
}

static int monwriter_restore(struct device *dev)
{
	struct mon_private *monpriv;
	struct mon_buf *monbuf;

	list_for_each_entry(monpriv, &mon_priv_list, priv_list) {
		list_for_each_entry(monbuf, &monpriv->list, list) {
			if (monbuf->hdr.mon_function == MONWRITE_START_INTERVAL)
				monwrite_diag(&monbuf->hdr, monbuf->data,
					      APPLDATA_START_INTERVAL_REC);
			if (monbuf->hdr.mon_function == MONWRITE_START_CONFIG)
				monwrite_diag(&monbuf->hdr, monbuf->data,
					      APPLDATA_START_CONFIG_REC);
		}
	}
	return 0;
}

static int monwriter_thaw(struct device *dev)
{
	return monwriter_restore(dev);
}

static const struct dev_pm_ops monwriter_pm_ops = {
	.freeze		= monwriter_freeze,
	.thaw		= monwriter_thaw,
	.restore	= monwriter_restore,
};

static struct platform_driver monwriter_pdrv = {
	.driver = {
		.name	= "monwriter",
		.pm	= &monwriter_pm_ops,
	},
};

static struct platform_device *monwriter_pdev;

/*
 * module init/exit
 */

static int __init mon_init(void)
{
	int rc;

	if (!MACHINE_IS_VM)
		return -ENODEV;

	rc = platform_driver_register(&monwriter_pdrv);
	if (rc)
		return rc;

	monwriter_pdev = platform_device_register_simple("monwriter", -1, NULL,
							0);
	if (IS_ERR(monwriter_pdev)) {
		rc = PTR_ERR(monwriter_pdev);
		goto out_driver;
	}

	/*
	 * misc_register() has to be the last action in module_init(), because
	 * file operations will be available right after this.
	 */
	rc = misc_register(&mon_dev);
	if (rc)
		goto out_device;
	return 0;

out_device:
	platform_device_unregister(monwriter_pdev);
out_driver:
	platform_driver_unregister(&monwriter_pdrv);
	return rc;
}

static void __exit mon_exit(void)
{
	misc_deregister(&mon_dev);
	platform_device_unregister(monwriter_pdev);
	platform_driver_unregister(&monwriter_pdrv);
}

module_init(mon_init);
module_exit(mon_exit);

module_param_named(max_bufs, mon_max_bufs, int, 0644);
MODULE_PARM_DESC(max_bufs, "Maximum number of sample monitor data buffers "
		 "that can be active at one time");

MODULE_AUTHOR("Melissa Howland <Melissa.Howland@us.ibm.com>");
MODULE_DESCRIPTION("Character device driver for writing z/VM "
		   "APPLDATA monitor records.");
MODULE_LICENSE("GPL");
