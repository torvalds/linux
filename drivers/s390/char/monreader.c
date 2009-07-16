/*
 * Character device driver for reading z/VM *MONITOR service records.
 *
 * Copyright IBM Corp. 2004, 2009
 *
 * Author: Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#define KMSG_COMPONENT "monreader"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <net/iucv/iucv.h>
#include <asm/uaccess.h>
#include <asm/ebcdic.h>
#include <asm/extmem.h>


#define MON_COLLECT_SAMPLE 0x80
#define MON_COLLECT_EVENT  0x40
#define MON_SERVICE	   "*MONITOR"
#define MON_IN_USE	   0x01
#define MON_MSGLIM	   255

static char mon_dcss_name[9] = "MONDCSS\0";

struct mon_msg {
	u32 pos;
	u32 mca_offset;
	struct iucv_message msg;
	char msglim_reached;
	char replied_msglim;
};

struct mon_private {
	struct iucv_path *path;
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

static struct device *monreader_device;

/******************************************************************************
 *                             helper functions                               *
 *****************************************************************************/
/*
 * Create the 8 bytes EBCDIC DCSS segment name from
 * an ASCII name, incl. padding
 */
static void dcss_mkname(char *ascii_name, char *ebcdic_name)
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

static inline unsigned long mon_mca_start(struct mon_msg *monmsg)
{
	return *(u32 *) &monmsg->msg.rmmsg;
}

static inline unsigned long mon_mca_end(struct mon_msg *monmsg)
{
	return *(u32 *) &monmsg->msg.rmmsg[4];
}

static inline u8 mon_mca_type(struct mon_msg *monmsg, u8 index)
{
	return *((u8 *) mon_mca_start(monmsg) + monmsg->mca_offset + index);
}

static inline u32 mon_mca_size(struct mon_msg *monmsg)
{
	return mon_mca_end(monmsg) - mon_mca_start(monmsg) + 1;
}

static inline u32 mon_rec_start(struct mon_msg *monmsg)
{
	return *((u32 *) (mon_mca_start(monmsg) + monmsg->mca_offset + 4));
}

static inline u32 mon_rec_end(struct mon_msg *monmsg)
{
	return *((u32 *) (mon_mca_start(monmsg) + monmsg->mca_offset + 8));
}

static int mon_check_mca(struct mon_msg *monmsg)
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
		return -EINVAL;
	return 0;
}

static int mon_send_reply(struct mon_msg *monmsg,
			  struct mon_private *monpriv)
{
	int rc;

	rc = iucv_message_reply(monpriv->path, &monmsg->msg,
				IUCV_IPRMDATA, NULL, 0);
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
		pr_err("Reading monitor data failed with rc=%i\n", rc);
		return -EIO;
	}
	return 0;
}

static void mon_free_mem(struct mon_private *monpriv)
{
	int i;

	for (i = 0; i < MON_MSGLIM; i++)
		if (monpriv->msg_array[i])
			kfree(monpriv->msg_array[i]);
	kfree(monpriv);
}

static struct mon_private *mon_alloc_mem(void)
{
	int i;
	struct mon_private *monpriv;

	monpriv = kzalloc(sizeof(struct mon_private), GFP_KERNEL);
	if (!monpriv)
		return NULL;
	for (i = 0; i < MON_MSGLIM; i++) {
		monpriv->msg_array[i] = kzalloc(sizeof(struct mon_msg),
						    GFP_KERNEL);
		if (!monpriv->msg_array[i]) {
			mon_free_mem(monpriv);
			return NULL;
		}
	}
	return monpriv;
}

static inline void mon_next_mca(struct mon_msg *monmsg)
{
	if (likely((mon_mca_size(monmsg) - monmsg->mca_offset) == 12))
		return;
	monmsg->mca_offset += 12;
	monmsg->pos = 0;
}

static struct mon_msg *mon_next_message(struct mon_private *monpriv)
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
static void mon_iucv_path_complete(struct iucv_path *path, u8 ipuser[16])
{
	struct mon_private *monpriv = path->private;

	atomic_set(&monpriv->iucv_connected, 1);
	wake_up(&mon_conn_wait_queue);
}

static void mon_iucv_path_severed(struct iucv_path *path, u8 ipuser[16])
{
	struct mon_private *monpriv = path->private;

	pr_err("z/VM *MONITOR system service disconnected with rc=%i\n",
	       ipuser[0]);
	iucv_path_sever(path, NULL);
	atomic_set(&monpriv->iucv_severed, 1);
	wake_up(&mon_conn_wait_queue);
	wake_up_interruptible(&mon_read_wait_queue);
}

static void mon_iucv_message_pending(struct iucv_path *path,
				     struct iucv_message *msg)
{
	struct mon_private *monpriv = path->private;

	memcpy(&monpriv->msg_array[monpriv->write_index]->msg,
	       msg, sizeof(*msg));
	if (atomic_inc_return(&monpriv->msglim_count) == MON_MSGLIM) {
		pr_warning("The read queue for monitor data is full\n");
		monpriv->msg_array[monpriv->write_index]->msglim_reached = 1;
	}
	monpriv->write_index = (monpriv->write_index + 1) % MON_MSGLIM;
	atomic_inc(&monpriv->read_ready);
	wake_up_interruptible(&mon_read_wait_queue);
}

static struct iucv_handler monreader_iucv_handler = {
	.path_complete	 = mon_iucv_path_complete,
	.path_severed	 = mon_iucv_path_severed,
	.message_pending = mon_iucv_message_pending,
};

/******************************************************************************
 *                               file operations                              *
 *****************************************************************************/
static int mon_open(struct inode *inode, struct file *filp)
{
	struct mon_private *monpriv;
	int rc;

	/*
	 * only one user allowed
	 */
	lock_kernel();
	rc = -EBUSY;
	if (test_and_set_bit(MON_IN_USE, &mon_in_use))
		goto out;

	rc = -ENOMEM;
	monpriv = mon_alloc_mem();
	if (!monpriv)
		goto out_use;

	/*
	 * Connect to *MONITOR service
	 */
	monpriv->path = iucv_path_alloc(MON_MSGLIM, IUCV_IPRMDATA, GFP_KERNEL);
	if (!monpriv->path)
		goto out_priv;
	rc = iucv_path_connect(monpriv->path, &monreader_iucv_handler,
			       MON_SERVICE, NULL, user_data_connect, monpriv);
	if (rc) {
		pr_err("Connecting to the z/VM *MONITOR system service "
		       "failed with rc=%i\n", rc);
		rc = -EIO;
		goto out_path;
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
		goto out_path;
	}
	filp->private_data = monpriv;
	dev_set_drvdata(monreader_device, monpriv);
	unlock_kernel();
	return nonseekable_open(inode, filp);

out_path:
	iucv_path_free(monpriv->path);
out_priv:
	mon_free_mem(monpriv);
out_use:
	clear_bit(MON_IN_USE, &mon_in_use);
out:
	unlock_kernel();
	return rc;
}

static int mon_close(struct inode *inode, struct file *filp)
{
	int rc, i;
	struct mon_private *monpriv = filp->private_data;

	/*
	 * Close IUCV connection and unregister
	 */
	if (monpriv->path) {
		rc = iucv_path_sever(monpriv->path, user_data_sever);
		if (rc)
			pr_warning("Disconnecting the z/VM *MONITOR system "
				   "service failed with rc=%i\n", rc);
		iucv_path_free(monpriv->path);
	}

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

static ssize_t mon_read(struct file *filp, char __user *data,
			size_t count, loff_t *ppos)
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

	if (!monmsg->pos)
		monmsg->pos = mon_mca_start(monmsg) + monmsg->mca_offset;
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

static unsigned int mon_poll(struct file *filp, struct poll_table_struct *p)
{
	struct mon_private *monpriv = filp->private_data;

	poll_wait(filp, &mon_read_wait_queue, p);
	if (unlikely(atomic_read(&monpriv->iucv_severed)))
		return POLLERR;
	if (atomic_read(&monpriv->read_ready))
		return POLLIN | POLLRDNORM;
	return 0;
}

static const struct file_operations mon_fops = {
	.owner   = THIS_MODULE,
	.open    = &mon_open,
	.release = &mon_close,
	.read    = &mon_read,
	.poll    = &mon_poll,
};

static struct miscdevice mon_dev = {
	.name       = "monreader",
	.fops       = &mon_fops,
	.minor      = MISC_DYNAMIC_MINOR,
};


/******************************************************************************
 *				suspend / resume			      *
 *****************************************************************************/
static int monreader_freeze(struct device *dev)
{
	struct mon_private *monpriv = dev_get_drvdata(dev);
	int rc;

	if (!monpriv)
		return 0;
	if (monpriv->path) {
		rc = iucv_path_sever(monpriv->path, user_data_sever);
		if (rc)
			pr_warning("Disconnecting the z/VM *MONITOR system "
				   "service failed with rc=%i\n", rc);
		iucv_path_free(monpriv->path);
	}
	atomic_set(&monpriv->iucv_severed, 0);
	atomic_set(&monpriv->iucv_connected, 0);
	atomic_set(&monpriv->read_ready, 0);
	atomic_set(&monpriv->msglim_count, 0);
	monpriv->write_index  = 0;
	monpriv->read_index   = 0;
	monpriv->path = NULL;
	return 0;
}

static int monreader_thaw(struct device *dev)
{
	struct mon_private *monpriv = dev_get_drvdata(dev);
	int rc;

	if (!monpriv)
		return 0;
	rc = -ENOMEM;
	monpriv->path = iucv_path_alloc(MON_MSGLIM, IUCV_IPRMDATA, GFP_KERNEL);
	if (!monpriv->path)
		goto out;
	rc = iucv_path_connect(monpriv->path, &monreader_iucv_handler,
			       MON_SERVICE, NULL, user_data_connect, monpriv);
	if (rc) {
		pr_err("Connecting to the z/VM *MONITOR system service "
		       "failed with rc=%i\n", rc);
		goto out_path;
	}
	wait_event(mon_conn_wait_queue,
		   atomic_read(&monpriv->iucv_connected) ||
		   atomic_read(&monpriv->iucv_severed));
	if (atomic_read(&monpriv->iucv_severed))
		goto out_path;
	return 0;
out_path:
	rc = -EIO;
	iucv_path_free(monpriv->path);
	monpriv->path = NULL;
out:
	atomic_set(&monpriv->iucv_severed, 1);
	return rc;
}

static int monreader_restore(struct device *dev)
{
	int rc;

	segment_unload(mon_dcss_name);
	rc = segment_load(mon_dcss_name, SEGMENT_SHARED,
			  &mon_dcss_start, &mon_dcss_end);
	if (rc < 0) {
		segment_warning(rc, mon_dcss_name);
		panic("fatal monreader resume error: no monitor dcss\n");
	}
	return monreader_thaw(dev);
}

static struct dev_pm_ops monreader_pm_ops = {
	.freeze  = monreader_freeze,
	.thaw	 = monreader_thaw,
	.restore = monreader_restore,
};

static struct device_driver monreader_driver = {
	.name = "monreader",
	.bus  = &iucv_bus,
	.pm   = &monreader_pm_ops,
};


/******************************************************************************
 *                              module init/exit                              *
 *****************************************************************************/
static int __init mon_init(void)
{
	int rc;

	if (!MACHINE_IS_VM) {
		pr_err("The z/VM *MONITOR record device driver cannot be "
		       "loaded without z/VM\n");
		return -ENODEV;
	}

	/*
	 * Register with IUCV and connect to *MONITOR service
	 */
	rc = iucv_register(&monreader_iucv_handler, 1);
	if (rc) {
		pr_err("The z/VM *MONITOR record device driver failed to "
		       "register with IUCV\n");
		return rc;
	}

	rc = driver_register(&monreader_driver);
	if (rc)
		goto out_iucv;
	monreader_device = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!monreader_device)
		goto out_driver;
	dev_set_name(monreader_device, "monreader-dev");
	monreader_device->bus = &iucv_bus;
	monreader_device->parent = iucv_root;
	monreader_device->driver = &monreader_driver;
	monreader_device->release = (void (*)(struct device *))kfree;
	rc = device_register(monreader_device);
	if (rc) {
		kfree(monreader_device);
		goto out_driver;
	}

	rc = segment_type(mon_dcss_name);
	if (rc < 0) {
		segment_warning(rc, mon_dcss_name);
		goto out_device;
	}
	if (rc != SEG_TYPE_SC) {
		pr_err("The specified *MONITOR DCSS %s does not have the "
		       "required type SC\n", mon_dcss_name);
		rc = -EINVAL;
		goto out_device;
	}

	rc = segment_load(mon_dcss_name, SEGMENT_SHARED,
			  &mon_dcss_start, &mon_dcss_end);
	if (rc < 0) {
		segment_warning(rc, mon_dcss_name);
		rc = -EINVAL;
		goto out_device;
	}
	dcss_mkname(mon_dcss_name, &user_data_connect[8]);

	rc = misc_register(&mon_dev);
	if (rc < 0 )
		goto out;
	return 0;

out:
	segment_unload(mon_dcss_name);
out_device:
	device_unregister(monreader_device);
out_driver:
	driver_unregister(&monreader_driver);
out_iucv:
	iucv_unregister(&monreader_iucv_handler, 1);
	return rc;
}

static void __exit mon_exit(void)
{
	segment_unload(mon_dcss_name);
	WARN_ON(misc_deregister(&mon_dev) != 0);
	device_unregister(monreader_device);
	driver_unregister(&monreader_driver);
	iucv_unregister(&monreader_iucv_handler, 1);
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
