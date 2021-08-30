// SPDX-License-Identifier: GPL-2.0
/*
 *	character device driver for reading z/VM system service records
 *
 *
 *	Copyright IBM Corp. 2004, 2009
 *	character device driver for reading z/VM system service records,
 *	Version 1.0
 *	Author(s): Xenia Tkatschow <xenia@us.ibm.com>
 *		   Stefan Weinhuber <wein@de.ibm.com>
 *
 */

#define KMSG_COMPONENT "vmlogrdr"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <asm/cpcmd.h>
#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <net/iucv/iucv.h>
#include <linux/kmod.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/string.h>

MODULE_AUTHOR
	("(C) 2004 IBM Corporation by Xenia Tkatschow (xenia@us.ibm.com)\n"
	 "                            Stefan Weinhuber (wein@de.ibm.com)");
MODULE_DESCRIPTION ("Character device driver for reading z/VM "
		    "system service records.");
MODULE_LICENSE("GPL");


/*
 * The size of the buffer for iucv data transfer is one page,
 * but in addition to the data we read from iucv we also
 * place an integer and some characters into that buffer,
 * so the maximum size for record data is a little less then
 * one page.
 */
#define NET_BUFFER_SIZE	(PAGE_SIZE - sizeof(int) - sizeof(FENCE))

/*
 * The elements that are concurrently accessed by bottom halves are
 * connection_established, iucv_path_severed, local_interrupt_buffer
 * and receive_ready. The first three can be protected by
 * priv_lock.  receive_ready is atomic, so it can be incremented and
 * decremented without holding a lock.
 * The variable dev_in_use needs to be protected by the lock, since
 * it's a flag used by open to make sure that the device is opened only
 * by one user at the same time.
 */
struct vmlogrdr_priv_t {
	char system_service[8];
	char internal_name[8];
	char recording_name[8];
	struct iucv_path *path;
	int connection_established;
	int iucv_path_severed;
	struct iucv_message local_interrupt_buffer;
	atomic_t receive_ready;
	int minor_num;
	char * buffer;
	char * current_position;
	int remaining;
	ulong residual_length;
	int buffer_free;
	int dev_in_use; /* 1: already opened, 0: not opened*/
	spinlock_t priv_lock;
	struct device  *device;
	struct device  *class_device;
	int autorecording;
	int autopurge;
};


/*
 * File operation structure for vmlogrdr devices
 */
static int vmlogrdr_open(struct inode *, struct file *);
static int vmlogrdr_release(struct inode *, struct file *);
static ssize_t vmlogrdr_read (struct file *filp, char __user *data,
			      size_t count, loff_t * ppos);

static const struct file_operations vmlogrdr_fops = {
	.owner   = THIS_MODULE,
	.open    = vmlogrdr_open,
	.release = vmlogrdr_release,
	.read    = vmlogrdr_read,
	.llseek  = no_llseek,
};


static void vmlogrdr_iucv_path_complete(struct iucv_path *, u8 *ipuser);
static void vmlogrdr_iucv_path_severed(struct iucv_path *, u8 *ipuser);
static void vmlogrdr_iucv_message_pending(struct iucv_path *,
					  struct iucv_message *);


static struct iucv_handler vmlogrdr_iucv_handler = {
	.path_complete	 = vmlogrdr_iucv_path_complete,
	.path_severed	 = vmlogrdr_iucv_path_severed,
	.message_pending = vmlogrdr_iucv_message_pending,
};


static DECLARE_WAIT_QUEUE_HEAD(conn_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(read_wait_queue);

/*
 * pointer to system service private structure
 * minor number 0 --> logrec
 * minor number 1 --> account
 * minor number 2 --> symptom
 */

static struct vmlogrdr_priv_t sys_ser[] = {
	{ .system_service = "*LOGREC ",
	  .internal_name  = "logrec",
	  .recording_name = "EREP",
	  .minor_num      = 0,
	  .buffer_free    = 1,
	  .priv_lock	  = __SPIN_LOCK_UNLOCKED(sys_ser[0].priv_lock),
	  .autorecording  = 1,
	  .autopurge      = 1,
	},
	{ .system_service = "*ACCOUNT",
	  .internal_name  = "account",
	  .recording_name = "ACCOUNT",
	  .minor_num      = 1,
	  .buffer_free    = 1,
	  .priv_lock	  = __SPIN_LOCK_UNLOCKED(sys_ser[1].priv_lock),
	  .autorecording  = 1,
	  .autopurge      = 1,
	},
	{ .system_service = "*SYMPTOM",
	  .internal_name  = "symptom",
	  .recording_name = "SYMPTOM",
	  .minor_num      = 2,
	  .buffer_free    = 1,
	  .priv_lock	  = __SPIN_LOCK_UNLOCKED(sys_ser[2].priv_lock),
	  .autorecording  = 1,
	  .autopurge      = 1,
	}
};

#define MAXMINOR  ARRAY_SIZE(sys_ser)

static char FENCE[] = {"EOR"};
static int vmlogrdr_major = 0;
static struct cdev  *vmlogrdr_cdev = NULL;
static int recording_class_AB;


static void vmlogrdr_iucv_path_complete(struct iucv_path *path, u8 *ipuser)
{
	struct vmlogrdr_priv_t * logptr = path->private;

	spin_lock(&logptr->priv_lock);
	logptr->connection_established = 1;
	spin_unlock(&logptr->priv_lock);
	wake_up(&conn_wait_queue);
}


static void vmlogrdr_iucv_path_severed(struct iucv_path *path, u8 *ipuser)
{
	struct vmlogrdr_priv_t * logptr = path->private;
	u8 reason = (u8) ipuser[8];

	pr_err("vmlogrdr: connection severed with reason %i\n", reason);

	iucv_path_sever(path, NULL);
	kfree(path);
	logptr->path = NULL;

	spin_lock(&logptr->priv_lock);
	logptr->connection_established = 0;
	logptr->iucv_path_severed = 1;
	spin_unlock(&logptr->priv_lock);

	wake_up(&conn_wait_queue);
	/* just in case we're sleeping waiting for a record */
	wake_up_interruptible(&read_wait_queue);
}


static void vmlogrdr_iucv_message_pending(struct iucv_path *path,
					  struct iucv_message *msg)
{
	struct vmlogrdr_priv_t * logptr = path->private;

	/*
	 * This function is the bottom half so it should be quick.
	 * Copy the external interrupt data into our local eib and increment
	 * the usage count
	 */
	spin_lock(&logptr->priv_lock);
	memcpy(&logptr->local_interrupt_buffer, msg, sizeof(*msg));
	atomic_inc(&logptr->receive_ready);
	spin_unlock(&logptr->priv_lock);
	wake_up_interruptible(&read_wait_queue);
}


static int vmlogrdr_get_recording_class_AB(void)
{
	static const char cp_command[] = "QUERY COMMAND RECORDING ";
	char cp_response[80];
	char *tail;
	int len,i;

	cpcmd(cp_command, cp_response, sizeof(cp_response), NULL);
	len = strnlen(cp_response,sizeof(cp_response));
	// now the parsing
	tail=strnchr(cp_response,len,'=');
	if (!tail)
		return 0;
	tail++;
	if (!strncmp("ANY",tail,3))
		return 1;
	if (!strncmp("NONE",tail,4))
		return 0;
	/*
	 * expect comma separated list of classes here, if one of them
	 * is A or B return 1 otherwise 0
	 */
        for (i=tail-cp_response; i<len; i++)
		if ( cp_response[i]=='A' || cp_response[i]=='B' )
			return 1;
	return 0;
}


static int vmlogrdr_recording(struct vmlogrdr_priv_t * logptr,
			      int action, int purge)
{

	char cp_command[80];
	char cp_response[160];
	char *onoff, *qid_string;
	int rc;

	onoff = ((action == 1) ? "ON" : "OFF");
	qid_string = ((recording_class_AB == 1) ? " QID * " : "");

	/*
	 * The recording commands needs to be called with option QID
	 * for guests that have previlege classes A or B.
	 * Purging has to be done as separate step, because recording
	 * can't be switched on as long as records are on the queue.
	 * Doing both at the same time doesn't work.
	 */
	if (purge && (action == 1)) {
		memset(cp_command, 0x00, sizeof(cp_command));
		memset(cp_response, 0x00, sizeof(cp_response));
		snprintf(cp_command, sizeof(cp_command),
			 "RECORDING %s PURGE %s",
			 logptr->recording_name,
			 qid_string);
		cpcmd(cp_command, cp_response, sizeof(cp_response), NULL);
	}

	memset(cp_command, 0x00, sizeof(cp_command));
	memset(cp_response, 0x00, sizeof(cp_response));
	snprintf(cp_command, sizeof(cp_command), "RECORDING %s %s %s",
		logptr->recording_name,
		onoff,
		qid_string);
	cpcmd(cp_command, cp_response, sizeof(cp_response), NULL);
	/* The recording command will usually answer with 'Command complete'
	 * on success, but when the specific service was never connected
	 * before then there might be an additional informational message
	 * 'HCPCRC8072I Recording entry not found' before the
	 * 'Command complete'. So I use strstr rather then the strncmp.
	 */
	if (strstr(cp_response,"Command complete"))
		rc = 0;
	else
		rc = -EIO;
	/*
	 * If we turn recording off, we have to purge any remaining records
	 * afterwards, as a large number of queued records may impact z/VM
	 * performance.
	 */
	if (purge && (action == 0)) {
		memset(cp_command, 0x00, sizeof(cp_command));
		memset(cp_response, 0x00, sizeof(cp_response));
		snprintf(cp_command, sizeof(cp_command),
			 "RECORDING %s PURGE %s",
			 logptr->recording_name,
			 qid_string);
		cpcmd(cp_command, cp_response, sizeof(cp_response), NULL);
	}

	return rc;
}


static int vmlogrdr_open (struct inode *inode, struct file *filp)
{
	int dev_num = 0;
	struct vmlogrdr_priv_t * logptr = NULL;
	int connect_rc = 0;
	int ret;

	dev_num = iminor(inode);
	if (dev_num >= MAXMINOR)
		return -ENODEV;
	logptr = &sys_ser[dev_num];

	/*
	 * only allow for blocking reads to be open
	 */
	if (filp->f_flags & O_NONBLOCK)
		return -EOPNOTSUPP;

	/* Besure this device hasn't already been opened */
	spin_lock_bh(&logptr->priv_lock);
	if (logptr->dev_in_use)	{
		spin_unlock_bh(&logptr->priv_lock);
		return -EBUSY;
	}
	logptr->dev_in_use = 1;
	logptr->connection_established = 0;
	logptr->iucv_path_severed = 0;
	atomic_set(&logptr->receive_ready, 0);
	logptr->buffer_free = 1;
	spin_unlock_bh(&logptr->priv_lock);

	/* set the file options */
	filp->private_data = logptr;

	/* start recording for this service*/
	if (logptr->autorecording) {
		ret = vmlogrdr_recording(logptr,1,logptr->autopurge);
		if (ret)
			pr_warn("vmlogrdr: failed to start recording automatically\n");
	}

	/* create connection to the system service */
	logptr->path = iucv_path_alloc(10, 0, GFP_KERNEL);
	if (!logptr->path)
		goto out_dev;
	connect_rc = iucv_path_connect(logptr->path, &vmlogrdr_iucv_handler,
				       logptr->system_service, NULL, NULL,
				       logptr);
	if (connect_rc) {
		pr_err("vmlogrdr: iucv connection to %s "
		       "failed with rc %i \n",
		       logptr->system_service, connect_rc);
		goto out_path;
	}

	/* We've issued the connect and now we must wait for a
	 * ConnectionComplete or ConnectinSevered Interrupt
	 * before we can continue to process.
	 */
	wait_event(conn_wait_queue, (logptr->connection_established)
		   || (logptr->iucv_path_severed));
	if (logptr->iucv_path_severed)
		goto out_record;
	nonseekable_open(inode, filp);
	return 0;

out_record:
	if (logptr->autorecording)
		vmlogrdr_recording(logptr,0,logptr->autopurge);
out_path:
	kfree(logptr->path);	/* kfree(NULL) is ok. */
	logptr->path = NULL;
out_dev:
	logptr->dev_in_use = 0;
	return -EIO;
}


static int vmlogrdr_release (struct inode *inode, struct file *filp)
{
	int ret;

	struct vmlogrdr_priv_t * logptr = filp->private_data;

	iucv_path_sever(logptr->path, NULL);
	kfree(logptr->path);
	logptr->path = NULL;
	if (logptr->autorecording) {
		ret = vmlogrdr_recording(logptr,0,logptr->autopurge);
		if (ret)
			pr_warn("vmlogrdr: failed to stop recording automatically\n");
	}
	logptr->dev_in_use = 0;

	return 0;
}


static int vmlogrdr_receive_data(struct vmlogrdr_priv_t *priv)
{
	int rc, *temp;
	/* we need to keep track of two data sizes here:
	 * The number of bytes we need to receive from iucv and
	 * the total number of bytes we actually write into the buffer.
	 */
	int user_data_count, iucv_data_count;
	char * buffer;

	if (atomic_read(&priv->receive_ready)) {
		spin_lock_bh(&priv->priv_lock);
		if (priv->residual_length){
			/* receive second half of a record */
			iucv_data_count = priv->residual_length;
			user_data_count = 0;
			buffer = priv->buffer;
		} else {
			/* receive a new record:
			 * We need to return the total length of the record
                         * + size of FENCE in the first 4 bytes of the buffer.
		         */
			iucv_data_count = priv->local_interrupt_buffer.length;
			user_data_count = sizeof(int);
			temp = (int*)priv->buffer;
			*temp= iucv_data_count + sizeof(FENCE);
			buffer = priv->buffer + sizeof(int);
		}
		/*
		 * If the record is bigger than our buffer, we receive only
		 * a part of it. We can get the rest later.
		 */
		if (iucv_data_count > NET_BUFFER_SIZE)
			iucv_data_count = NET_BUFFER_SIZE;
		rc = iucv_message_receive(priv->path,
					  &priv->local_interrupt_buffer,
					  0, buffer, iucv_data_count,
					  &priv->residual_length);
		spin_unlock_bh(&priv->priv_lock);
		/* An rc of 5 indicates that the record was bigger than
		 * the buffer, which is OK for us. A 9 indicates that the
		 * record was purged befor we could receive it.
		 */
		if (rc == 5)
			rc = 0;
		if (rc == 9)
			atomic_set(&priv->receive_ready, 0);
	} else {
		rc = 1;
	}
	if (!rc) {
		priv->buffer_free = 0;
 		user_data_count += iucv_data_count;
		priv->current_position = priv->buffer;
		if (priv->residual_length == 0){
			/* the whole record has been captured,
			 * now add the fence */
			atomic_dec(&priv->receive_ready);
			buffer = priv->buffer + user_data_count;
			memcpy(buffer, FENCE, sizeof(FENCE));
			user_data_count += sizeof(FENCE);
		}
		priv->remaining = user_data_count;
	}

	return rc;
}


static ssize_t vmlogrdr_read(struct file *filp, char __user *data,
			     size_t count, loff_t * ppos)
{
	int rc;
	struct vmlogrdr_priv_t * priv = filp->private_data;

	while (priv->buffer_free) {
		rc = vmlogrdr_receive_data(priv);
		if (rc) {
			rc = wait_event_interruptible(read_wait_queue,
					atomic_read(&priv->receive_ready));
			if (rc)
				return rc;
		}
	}
	/* copy only up to end of record */
	if (count > priv->remaining)
		count = priv->remaining;

	if (copy_to_user(data, priv->current_position, count))
		return -EFAULT;

	*ppos += count;
	priv->current_position += count;
	priv->remaining -= count;

	/* if all data has been transferred, set buffer free */
	if (priv->remaining == 0)
		priv->buffer_free = 1;

	return count;
}

static ssize_t vmlogrdr_autopurge_store(struct device * dev,
					struct device_attribute *attr,
					const char * buf, size_t count)
{
	struct vmlogrdr_priv_t *priv = dev_get_drvdata(dev);
	ssize_t ret = count;

	switch (buf[0]) {
	case '0':
		priv->autopurge=0;
		break;
	case '1':
		priv->autopurge=1;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}


static ssize_t vmlogrdr_autopurge_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct vmlogrdr_priv_t *priv = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", priv->autopurge);
}


static DEVICE_ATTR(autopurge, 0644, vmlogrdr_autopurge_show,
		   vmlogrdr_autopurge_store);


static ssize_t vmlogrdr_purge_store(struct device * dev,
				    struct device_attribute *attr,
				    const char * buf, size_t count)
{

	char cp_command[80];
	char cp_response[80];
	struct vmlogrdr_priv_t *priv = dev_get_drvdata(dev);

	if (buf[0] != '1')
		return -EINVAL;

	memset(cp_command, 0x00, sizeof(cp_command));
	memset(cp_response, 0x00, sizeof(cp_response));

        /*
	 * The recording command needs to be called with option QID
	 * for guests that have previlege classes A or B.
	 * Other guests will not recognize the command and we have to
	 * issue the same command without the QID parameter.
	 */

	if (recording_class_AB)
		snprintf(cp_command, sizeof(cp_command),
			 "RECORDING %s PURGE QID * ",
			 priv->recording_name);
	else
		snprintf(cp_command, sizeof(cp_command),
			 "RECORDING %s PURGE ",
			 priv->recording_name);

	cpcmd(cp_command, cp_response, sizeof(cp_response), NULL);

	return count;
}


static DEVICE_ATTR(purge, 0200, NULL, vmlogrdr_purge_store);


static ssize_t vmlogrdr_autorecording_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct vmlogrdr_priv_t *priv = dev_get_drvdata(dev);
	ssize_t ret = count;

	switch (buf[0]) {
	case '0':
		priv->autorecording=0;
		break;
	case '1':
		priv->autorecording=1;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}


static ssize_t vmlogrdr_autorecording_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct vmlogrdr_priv_t *priv = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", priv->autorecording);
}


static DEVICE_ATTR(autorecording, 0644, vmlogrdr_autorecording_show,
		   vmlogrdr_autorecording_store);


static ssize_t vmlogrdr_recording_store(struct device * dev,
					struct device_attribute *attr,
					const char * buf, size_t count)
{
	struct vmlogrdr_priv_t *priv = dev_get_drvdata(dev);
	ssize_t ret;

	switch (buf[0]) {
	case '0':
		ret = vmlogrdr_recording(priv,0,0);
		break;
	case '1':
		ret = vmlogrdr_recording(priv,1,0);
		break;
	default:
		ret = -EINVAL;
	}
	if (ret)
		return ret;
	else
		return count;

}


static DEVICE_ATTR(recording, 0200, NULL, vmlogrdr_recording_store);


static ssize_t recording_status_show(struct device_driver *driver, char *buf)
{
	static const char cp_command[] = "QUERY RECORDING ";
	int len;

	cpcmd(cp_command, buf, 4096, NULL);
	len = strlen(buf);
	return len;
}
static DRIVER_ATTR_RO(recording_status);
static struct attribute *vmlogrdr_drv_attrs[] = {
	&driver_attr_recording_status.attr,
	NULL,
};
static struct attribute_group vmlogrdr_drv_attr_group = {
	.attrs = vmlogrdr_drv_attrs,
};
static const struct attribute_group *vmlogrdr_drv_attr_groups[] = {
	&vmlogrdr_drv_attr_group,
	NULL,
};

static struct attribute *vmlogrdr_attrs[] = {
	&dev_attr_autopurge.attr,
	&dev_attr_purge.attr,
	&dev_attr_autorecording.attr,
	&dev_attr_recording.attr,
	NULL,
};
static struct attribute_group vmlogrdr_attr_group = {
	.attrs = vmlogrdr_attrs,
};
static const struct attribute_group *vmlogrdr_attr_groups[] = {
	&vmlogrdr_attr_group,
	NULL,
};

static struct class *vmlogrdr_class;
static struct device_driver vmlogrdr_driver = {
	.name = "vmlogrdr",
	.bus  = &iucv_bus,
	.groups = vmlogrdr_drv_attr_groups,
};

static int vmlogrdr_register_driver(void)
{
	int ret;

	/* Register with iucv driver */
	ret = iucv_register(&vmlogrdr_iucv_handler, 1);
	if (ret)
		goto out;

	ret = driver_register(&vmlogrdr_driver);
	if (ret)
		goto out_iucv;

	vmlogrdr_class = class_create(THIS_MODULE, "vmlogrdr");
	if (IS_ERR(vmlogrdr_class)) {
		ret = PTR_ERR(vmlogrdr_class);
		vmlogrdr_class = NULL;
		goto out_driver;
	}
	return 0;

out_driver:
	driver_unregister(&vmlogrdr_driver);
out_iucv:
	iucv_unregister(&vmlogrdr_iucv_handler, 1);
out:
	return ret;
}


static void vmlogrdr_unregister_driver(void)
{
	class_destroy(vmlogrdr_class);
	vmlogrdr_class = NULL;
	driver_unregister(&vmlogrdr_driver);
	iucv_unregister(&vmlogrdr_iucv_handler, 1);
}


static int vmlogrdr_register_device(struct vmlogrdr_priv_t *priv)
{
	struct device *dev;
	int ret;

	dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (dev) {
		dev_set_name(dev, "%s", priv->internal_name);
		dev->bus = &iucv_bus;
		dev->parent = iucv_root;
		dev->driver = &vmlogrdr_driver;
		dev->groups = vmlogrdr_attr_groups;
		dev_set_drvdata(dev, priv);
		/*
		 * The release function could be called after the
		 * module has been unloaded. It's _only_ task is to
		 * free the struct. Therefore, we specify kfree()
		 * directly here. (Probably a little bit obfuscating
		 * but legitime ...).
		 */
		dev->release = (void (*)(struct device *))kfree;
	} else
		return -ENOMEM;
	ret = device_register(dev);
	if (ret) {
		put_device(dev);
		return ret;
	}

	priv->class_device = device_create(vmlogrdr_class, dev,
					   MKDEV(vmlogrdr_major,
						 priv->minor_num),
					   priv, "%s", dev_name(dev));
	if (IS_ERR(priv->class_device)) {
		ret = PTR_ERR(priv->class_device);
		priv->class_device=NULL;
		device_unregister(dev);
		return ret;
	}
	priv->device = dev;
	return 0;
}


static int vmlogrdr_unregister_device(struct vmlogrdr_priv_t *priv)
{
	device_destroy(vmlogrdr_class, MKDEV(vmlogrdr_major, priv->minor_num));
	if (priv->device != NULL) {
		device_unregister(priv->device);
		priv->device=NULL;
	}
	return 0;
}


static int vmlogrdr_register_cdev(dev_t dev)
{
	int rc = 0;
	vmlogrdr_cdev = cdev_alloc();
	if (!vmlogrdr_cdev) {
		return -ENOMEM;
	}
	vmlogrdr_cdev->owner = THIS_MODULE;
	vmlogrdr_cdev->ops = &vmlogrdr_fops;
	rc = cdev_add(vmlogrdr_cdev, dev, MAXMINOR);
	if (!rc)
		return 0;

	// cleanup: cdev is not fully registered, no cdev_del here!
	kobject_put(&vmlogrdr_cdev->kobj);
	vmlogrdr_cdev=NULL;
	return rc;
}


static void vmlogrdr_cleanup(void)
{
        int i;

	if (vmlogrdr_cdev) {
		cdev_del(vmlogrdr_cdev);
		vmlogrdr_cdev=NULL;
	}
	for (i=0; i < MAXMINOR; ++i ) {
		vmlogrdr_unregister_device(&sys_ser[i]);
		free_page((unsigned long)sys_ser[i].buffer);
	}
	vmlogrdr_unregister_driver();
	if (vmlogrdr_major) {
		unregister_chrdev_region(MKDEV(vmlogrdr_major, 0), MAXMINOR);
		vmlogrdr_major=0;
	}
}


static int __init vmlogrdr_init(void)
{
	int rc;
	int i;
	dev_t dev;

	if (! MACHINE_IS_VM) {
		pr_err("not running under VM, driver not loaded.\n");
		return -ENODEV;
	}

        recording_class_AB = vmlogrdr_get_recording_class_AB();

	rc = alloc_chrdev_region(&dev, 0, MAXMINOR, "vmlogrdr");
	if (rc)
		return rc;
	vmlogrdr_major = MAJOR(dev);

	rc=vmlogrdr_register_driver();
	if (rc)
		goto cleanup;

	for (i=0; i < MAXMINOR; ++i ) {
		sys_ser[i].buffer = (char *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
		if (!sys_ser[i].buffer) {
			rc = -ENOMEM;
			break;
		}
		sys_ser[i].current_position = sys_ser[i].buffer;
		rc=vmlogrdr_register_device(&sys_ser[i]);
		if (rc)
			break;
	}
	if (rc)
		goto cleanup;

	rc = vmlogrdr_register_cdev(dev);
	if (rc)
		goto cleanup;
	return 0;

cleanup:
	vmlogrdr_cleanup();
	return rc;
}


static void __exit vmlogrdr_exit(void)
{
	vmlogrdr_cleanup();
	return;
}


module_init(vmlogrdr_init);
module_exit(vmlogrdr_exit);
