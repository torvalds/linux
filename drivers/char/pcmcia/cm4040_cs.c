/*
 * A driver for the Omnikey PCMCIA smartcard reader CardMan 4040
 *
 * (c) 2000-2004 Omnikey AG (http://www.omnikey.com/)
 *
 * (C) 2005-2006 Harald Welte <laforge@gnumonks.org>
 * 	- add support for poll()
 * 	- driver cleanup
 * 	- add waitqueues
 * 	- adhere to linux kernel coding style and policies
 * 	- support 2.6.13 "new style" pcmcia interface
 * 	- add class interface for udev device creation
 *
 * The device basically is a USB CCID compliant device that has been
 * attached to an I/O-Mapped FIFO.
 *
 * All rights reserved, Dual BSD/GPL Licensed.
 */

/* #define PCMCIA_DEBUG 6 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>

#include "cm4040_cs.h"


#ifdef PCMCIA_DEBUG
#define reader_to_dev(x)	(&handle_to_dev(x->p_dev))
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0600);
#define DEBUGP(n, rdr, x, args...) do { 				\
	if (pc_debug >= (n)) 						\
		dev_printk(KERN_DEBUG, reader_to_dev(rdr), "%s:" x, 	\
			   __func__ , ##args); 			\
	} while (0)
#else
#define DEBUGP(n, rdr, x, args...)
#endif

static char *version =
"OMNIKEY CardMan 4040 v1.1.0gm5 - All bugs added by Harald Welte";

#define	CCID_DRIVER_BULK_DEFAULT_TIMEOUT  	(150*HZ)
#define	CCID_DRIVER_ASYNC_POWERUP_TIMEOUT 	(35*HZ)
#define	CCID_DRIVER_MINIMUM_TIMEOUT 		(3*HZ)
#define READ_WRITE_BUFFER_SIZE 512
#define POLL_LOOP_COUNT				1000

/* how often to poll for fifo status change */
#define POLL_PERIOD 				msecs_to_jiffies(10)

static void reader_release(struct pcmcia_device *link);

static int major;
static struct class *cmx_class;

#define		BS_READABLE	0x01
#define		BS_WRITABLE	0x02

struct reader_dev {
	struct pcmcia_device	*p_dev;
	dev_node_t		node;
	wait_queue_head_t	devq;
	wait_queue_head_t	poll_wait;
	wait_queue_head_t	read_wait;
	wait_queue_head_t	write_wait;
	unsigned long 	  	buffer_status;
	unsigned long     	timeout;
	unsigned char     	s_buf[READ_WRITE_BUFFER_SIZE];
	unsigned char     	r_buf[READ_WRITE_BUFFER_SIZE];
	struct timer_list 	poll_timer;
};

static struct pcmcia_device *dev_table[CM_MAX_DEV];

#ifndef PCMCIA_DEBUG
#define	xoutb	outb
#define	xinb	inb
#else
static inline void xoutb(unsigned char val, unsigned short port)
{
	if (pc_debug >= 7)
		printk(KERN_DEBUG "outb(val=%.2x,port=%.4x)\n", val, port);
	outb(val, port);
}

static inline unsigned char xinb(unsigned short port)
{
	unsigned char val;

	val = inb(port);
	if (pc_debug >= 7)
		printk(KERN_DEBUG "%.2x=inb(%.4x)\n", val, port);
	return val;
}
#endif

/* poll the device fifo status register.  not to be confused with
 * the poll syscall. */
static void cm4040_do_poll(unsigned long dummy)
{
	struct reader_dev *dev = (struct reader_dev *) dummy;
	unsigned int obs = xinb(dev->p_dev->io.BasePort1
				+ REG_OFFSET_BUFFER_STATUS);

	if ((obs & BSR_BULK_IN_FULL)) {
		set_bit(BS_READABLE, &dev->buffer_status);
		DEBUGP(4, dev, "waking up read_wait\n");
		wake_up_interruptible(&dev->read_wait);
	} else
		clear_bit(BS_READABLE, &dev->buffer_status);

	if (!(obs & BSR_BULK_OUT_FULL)) {
		set_bit(BS_WRITABLE, &dev->buffer_status);
		DEBUGP(4, dev, "waking up write_wait\n");
		wake_up_interruptible(&dev->write_wait);
	} else
		clear_bit(BS_WRITABLE, &dev->buffer_status);

	if (dev->buffer_status)
		wake_up_interruptible(&dev->poll_wait);

	mod_timer(&dev->poll_timer, jiffies + POLL_PERIOD);
}

static void cm4040_stop_poll(struct reader_dev *dev)
{
	del_timer_sync(&dev->poll_timer);
}

static int wait_for_bulk_out_ready(struct reader_dev *dev)
{
	int i, rc;
	int iobase = dev->p_dev->io.BasePort1;

	for (i = 0; i < POLL_LOOP_COUNT; i++) {
		if ((xinb(iobase + REG_OFFSET_BUFFER_STATUS)
		    & BSR_BULK_OUT_FULL) == 0) {
			DEBUGP(4, dev, "BulkOut empty (i=%d)\n", i);
			return 1;
		}
	}

	DEBUGP(4, dev, "wait_event_interruptible_timeout(timeout=%ld\n",
		dev->timeout);
	rc = wait_event_interruptible_timeout(dev->write_wait,
					      test_and_clear_bit(BS_WRITABLE,
						       &dev->buffer_status),
					      dev->timeout);

	if (rc > 0)
		DEBUGP(4, dev, "woke up: BulkOut empty\n");
	else if (rc == 0)
		DEBUGP(4, dev, "woke up: BulkOut full, returning 0 :(\n");
	else if (rc < 0)
		DEBUGP(4, dev, "woke up: signal arrived\n");

	return rc;
}

/* Write to Sync Control Register */
static int write_sync_reg(unsigned char val, struct reader_dev *dev)
{
	int iobase = dev->p_dev->io.BasePort1;
	int rc;

	rc = wait_for_bulk_out_ready(dev);
	if (rc <= 0)
		return rc;

	xoutb(val, iobase + REG_OFFSET_SYNC_CONTROL);
	rc = wait_for_bulk_out_ready(dev);
	if (rc <= 0)
		return rc;

	return 1;
}

static int wait_for_bulk_in_ready(struct reader_dev *dev)
{
	int i, rc;
	int iobase = dev->p_dev->io.BasePort1;

	for (i = 0; i < POLL_LOOP_COUNT; i++) {
		if ((xinb(iobase + REG_OFFSET_BUFFER_STATUS)
		    & BSR_BULK_IN_FULL) == BSR_BULK_IN_FULL) {
			DEBUGP(3, dev, "BulkIn full (i=%d)\n", i);
			return 1;
		}
	}

	DEBUGP(4, dev, "wait_event_interruptible_timeout(timeout=%ld\n",
		dev->timeout);
	rc = wait_event_interruptible_timeout(dev->read_wait,
					      test_and_clear_bit(BS_READABLE,
						 	&dev->buffer_status),
					      dev->timeout);
	if (rc > 0)
		DEBUGP(4, dev, "woke up: BulkIn full\n");
	else if (rc == 0)
		DEBUGP(4, dev, "woke up: BulkIn not full, returning 0 :(\n");
	else if (rc < 0)
		DEBUGP(4, dev, "woke up: signal arrived\n");

	return rc;
}

static ssize_t cm4040_read(struct file *filp, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct reader_dev *dev = filp->private_data;
	int iobase = dev->p_dev->io.BasePort1;
	size_t bytes_to_read;
	unsigned long i;
	size_t min_bytes_to_read;
	int rc;
	unsigned char uc;

	DEBUGP(2, dev, "-> cm4040_read(%s,%d)\n", current->comm, current->pid);

	if (count == 0)
		return 0;

	if (count < 10)
		return -EFAULT;

	if (filp->f_flags & O_NONBLOCK) {
		DEBUGP(4, dev, "filep->f_flags O_NONBLOCK set\n");
		DEBUGP(2, dev, "<- cm4040_read (failure)\n");
		return -EAGAIN;
	}

	if (!pcmcia_dev_present(dev->p_dev))
		return -ENODEV;

	for (i = 0; i < 5; i++) {
		rc = wait_for_bulk_in_ready(dev);
		if (rc <= 0) {
			DEBUGP(5, dev, "wait_for_bulk_in_ready rc=%.2x\n", rc);
			DEBUGP(2, dev, "<- cm4040_read (failed)\n");
			if (rc == -ERESTARTSYS)
				return rc;
			return -EIO;
		}
	  	dev->r_buf[i] = xinb(iobase + REG_OFFSET_BULK_IN);
#ifdef PCMCIA_DEBUG
		if (pc_debug >= 6)
			printk(KERN_DEBUG "%lu:%2x ", i, dev->r_buf[i]);
	}
	printk("\n");
#else
	}
#endif

	bytes_to_read = 5 + le32_to_cpu(*(__le32 *)&dev->r_buf[1]);

	DEBUGP(6, dev, "BytesToRead=%lu\n", bytes_to_read);

	min_bytes_to_read = min(count, bytes_to_read + 5);
	min_bytes_to_read = min_t(size_t, min_bytes_to_read, READ_WRITE_BUFFER_SIZE);

	DEBUGP(6, dev, "Min=%lu\n", min_bytes_to_read);

	for (i = 0; i < (min_bytes_to_read-5); i++) {
		rc = wait_for_bulk_in_ready(dev);
		if (rc <= 0) {
			DEBUGP(5, dev, "wait_for_bulk_in_ready rc=%.2x\n", rc);
			DEBUGP(2, dev, "<- cm4040_read (failed)\n");
			if (rc == -ERESTARTSYS)
				return rc;
			return -EIO;
		}
		dev->r_buf[i+5] = xinb(iobase + REG_OFFSET_BULK_IN);
#ifdef PCMCIA_DEBUG
		if (pc_debug >= 6)
			printk(KERN_DEBUG "%lu:%2x ", i, dev->r_buf[i]);
	}
	printk("\n");
#else
	}
#endif

	*ppos = min_bytes_to_read;
	if (copy_to_user(buf, dev->r_buf, min_bytes_to_read))
		return -EFAULT;

	rc = wait_for_bulk_in_ready(dev);
	if (rc <= 0) {
		DEBUGP(5, dev, "wait_for_bulk_in_ready rc=%.2x\n", rc);
		DEBUGP(2, dev, "<- cm4040_read (failed)\n");
		if (rc == -ERESTARTSYS)
			return rc;
		return -EIO;
	}

	rc = write_sync_reg(SCR_READER_TO_HOST_DONE, dev);
	if (rc <= 0) {
		DEBUGP(5, dev, "write_sync_reg c=%.2x\n", rc);
		DEBUGP(2, dev, "<- cm4040_read (failed)\n");
		if (rc == -ERESTARTSYS)
			return rc;
		else
			return -EIO;
	}

	uc = xinb(iobase + REG_OFFSET_BULK_IN);

	DEBUGP(2, dev, "<- cm4040_read (successfully)\n");
	return min_bytes_to_read;
}

static ssize_t cm4040_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct reader_dev *dev = filp->private_data;
	int iobase = dev->p_dev->io.BasePort1;
	ssize_t rc;
	int i;
	unsigned int bytes_to_write;

	DEBUGP(2, dev, "-> cm4040_write(%s,%d)\n", current->comm, current->pid);

	if (count == 0) {
		DEBUGP(2, dev, "<- cm4040_write empty read (successfully)\n");
		return 0;
	}

	if ((count < 5) || (count > READ_WRITE_BUFFER_SIZE)) {
		DEBUGP(2, dev, "<- cm4040_write buffersize=%Zd < 5\n", count);
		return -EIO;
	}

	if (filp->f_flags & O_NONBLOCK) {
		DEBUGP(4, dev, "filep->f_flags O_NONBLOCK set\n");
		DEBUGP(4, dev, "<- cm4040_write (failure)\n");
		return -EAGAIN;
	}

	if (!pcmcia_dev_present(dev->p_dev))
		return -ENODEV;

	bytes_to_write = count;
	if (copy_from_user(dev->s_buf, buf, bytes_to_write))
		return -EFAULT;

	switch (dev->s_buf[0]) {
		case CMD_PC_TO_RDR_XFRBLOCK:
		case CMD_PC_TO_RDR_SECURE:
		case CMD_PC_TO_RDR_TEST_SECURE:
		case CMD_PC_TO_RDR_OK_SECURE:
			dev->timeout = CCID_DRIVER_BULK_DEFAULT_TIMEOUT;
			break;

		case CMD_PC_TO_RDR_ICCPOWERON:
			dev->timeout = CCID_DRIVER_ASYNC_POWERUP_TIMEOUT;
			break;

		case CMD_PC_TO_RDR_GETSLOTSTATUS:
		case CMD_PC_TO_RDR_ICCPOWEROFF:
		case CMD_PC_TO_RDR_GETPARAMETERS:
		case CMD_PC_TO_RDR_RESETPARAMETERS:
		case CMD_PC_TO_RDR_SETPARAMETERS:
		case CMD_PC_TO_RDR_ESCAPE:
		case CMD_PC_TO_RDR_ICCCLOCK:
		default:
			dev->timeout = CCID_DRIVER_MINIMUM_TIMEOUT;
			break;
	}

	rc = write_sync_reg(SCR_HOST_TO_READER_START, dev);
	if (rc <= 0) {
		DEBUGP(5, dev, "write_sync_reg c=%.2Zx\n", rc);
		DEBUGP(2, dev, "<- cm4040_write (failed)\n");
		if (rc == -ERESTARTSYS)
			return rc;
		else
			return -EIO;
	}

	DEBUGP(4, dev, "start \n");

	for (i = 0; i < bytes_to_write; i++) {
		rc = wait_for_bulk_out_ready(dev);
		if (rc <= 0) {
			DEBUGP(5, dev, "wait_for_bulk_out_ready rc=%.2Zx\n",
			       rc);
			DEBUGP(2, dev, "<- cm4040_write (failed)\n");
			if (rc == -ERESTARTSYS)
				return rc;
			else
				return -EIO;
		}

		xoutb(dev->s_buf[i],iobase + REG_OFFSET_BULK_OUT);
	}
	DEBUGP(4, dev, "end\n");

	rc = write_sync_reg(SCR_HOST_TO_READER_DONE, dev);

	if (rc <= 0) {
		DEBUGP(5, dev, "write_sync_reg c=%.2Zx\n", rc);
		DEBUGP(2, dev, "<- cm4040_write (failed)\n");
		if (rc == -ERESTARTSYS)
			return rc;
		else
			return -EIO;
	}

	DEBUGP(2, dev, "<- cm4040_write (successfully)\n");
	return count;
}

static unsigned int cm4040_poll(struct file *filp, poll_table *wait)
{
	struct reader_dev *dev = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &dev->poll_wait, wait);

	if (test_and_clear_bit(BS_READABLE, &dev->buffer_status))
		mask |= POLLIN | POLLRDNORM;
	if (test_and_clear_bit(BS_WRITABLE, &dev->buffer_status))
		mask |= POLLOUT | POLLWRNORM;

	DEBUGP(2, dev, "<- cm4040_poll(%u)\n", mask);

	return mask;
}

static int cm4040_open(struct inode *inode, struct file *filp)
{
	struct reader_dev *dev;
	struct pcmcia_device *link;
	int minor = iminor(inode);
	int ret;

	if (minor >= CM_MAX_DEV)
		return -ENODEV;

	lock_kernel();
	link = dev_table[minor];
	if (link == NULL || !pcmcia_dev_present(link)) {
		ret = -ENODEV;
		goto out;
	}

	if (link->open) {
		ret = -EBUSY;
		goto out;
	}

	dev = link->priv;
	filp->private_data = dev;

	if (filp->f_flags & O_NONBLOCK) {
		DEBUGP(4, dev, "filep->f_flags O_NONBLOCK set\n");
		ret = -EAGAIN;
		goto out;
	}

	link->open = 1;

	dev->poll_timer.data = (unsigned long) dev;
	mod_timer(&dev->poll_timer, jiffies + POLL_PERIOD);

	DEBUGP(2, dev, "<- cm4040_open (successfully)\n");
	ret = nonseekable_open(inode, filp);
out:
	unlock_kernel();
	return ret;
}

static int cm4040_close(struct inode *inode, struct file *filp)
{
	struct reader_dev *dev = filp->private_data;
	struct pcmcia_device *link;
	int minor = iminor(inode);

	DEBUGP(2, dev, "-> cm4040_close(maj/min=%d.%d)\n", imajor(inode),
	      iminor(inode));

	if (minor >= CM_MAX_DEV)
		return -ENODEV;

	link = dev_table[minor];
	if (link == NULL)
		return -ENODEV;

	cm4040_stop_poll(dev);

	link->open = 0;
	wake_up(&dev->devq);

	DEBUGP(2, dev, "<- cm4040_close\n");
	return 0;
}

static void cm4040_reader_release(struct pcmcia_device *link)
{
	struct reader_dev *dev = link->priv;

	DEBUGP(3, dev, "-> cm4040_reader_release\n");
	while (link->open) {
		DEBUGP(3, dev, KERN_INFO MODULE_NAME ": delaying release "
		       "until process has terminated\n");
 		wait_event(dev->devq, (link->open == 0));
	}
	DEBUGP(3, dev, "<- cm4040_reader_release\n");
	return;
}

static int reader_config(struct pcmcia_device *link, int devno)
{
	struct reader_dev *dev;
	tuple_t tuple;
	cisparse_t parse;
	u_char buf[64];
	int fail_fn, fail_rc;
	int rc;

	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
 	tuple.TupleOffset = 0;

	link->io.BasePort2 = 0;
	link->io.NumPorts2 = 0;
	link->io.Attributes2 = 0;
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	for (rc = pcmcia_get_first_tuple(link, &tuple);
	     rc == CS_SUCCESS;
	     rc = pcmcia_get_next_tuple(link, &tuple)) {
		rc = pcmcia_get_tuple_data(link, &tuple);
		if (rc != CS_SUCCESS)
			continue;
		rc = pcmcia_parse_tuple(link, &tuple, &parse);
		if (rc != CS_SUCCESS)
			continue;

		link->conf.ConfigIndex = parse.cftable_entry.index;

		if (!parse.cftable_entry.io.nwin)
			continue;

		link->io.BasePort1 = parse.cftable_entry.io.win[0].base;
		link->io.NumPorts1 = parse.cftable_entry.io.win[0].len;
		link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
		if (!(parse.cftable_entry.io.flags & CISTPL_IO_8BIT))
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
		if (!(parse.cftable_entry.io.flags & CISTPL_IO_16BIT))
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
		link->io.IOAddrLines = parse.cftable_entry.io.flags
						& CISTPL_IO_LINES_MASK;
		rc = pcmcia_request_io(link, &link->io);

		dev_printk(KERN_INFO, &handle_to_dev(link), "foo");
		if (rc == CS_SUCCESS)
			break;
		else
			dev_printk(KERN_INFO, &handle_to_dev(link),
				   "pcmcia_request_io failed 0x%x\n", rc);
	}
	if (rc != CS_SUCCESS)
		goto cs_release;

	link->conf.IntType = 00000002;

	if ((fail_rc = pcmcia_request_configuration(link,&link->conf))
								!=CS_SUCCESS) {
		fail_fn = RequestConfiguration;
		dev_printk(KERN_INFO, &handle_to_dev(link),
			   "pcmcia_request_configuration failed 0x%x\n",
			   fail_rc);
		goto cs_release;
	}

	dev = link->priv;
	sprintf(dev->node.dev_name, DEVICE_NAME "%d", devno);
	dev->node.major = major;
	dev->node.minor = devno;
	dev->node.next = &dev->node;

	DEBUGP(2, dev, "device " DEVICE_NAME "%d at 0x%.4x-0x%.4x\n", devno,
	      link->io.BasePort1, link->io.BasePort1+link->io.NumPorts1);
	DEBUGP(2, dev, "<- reader_config (succ)\n");

	return 0;

cs_release:
	reader_release(link);
	return -ENODEV;
}

static void reader_release(struct pcmcia_device *link)
{
	cm4040_reader_release(link);
	pcmcia_disable_device(link);
}

static int reader_probe(struct pcmcia_device *link)
{
	struct reader_dev *dev;
	int i, ret;

	for (i = 0; i < CM_MAX_DEV; i++) {
		if (dev_table[i] == NULL)
			break;
	}

	if (i == CM_MAX_DEV)
		return -ENODEV;

	dev = kzalloc(sizeof(struct reader_dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	dev->timeout = CCID_DRIVER_MINIMUM_TIMEOUT;
	dev->buffer_status = 0;

	link->priv = dev;
	dev->p_dev = link;

	link->conf.IntType = INT_MEMORY_AND_IO;
	dev_table[i] = link;

	init_waitqueue_head(&dev->devq);
	init_waitqueue_head(&dev->poll_wait);
	init_waitqueue_head(&dev->read_wait);
	init_waitqueue_head(&dev->write_wait);
	setup_timer(&dev->poll_timer, cm4040_do_poll, 0);

	ret = reader_config(link, i);
	if (ret) {
		dev_table[i] = NULL;
		kfree(dev);
		return ret;
	}

	device_create_drvdata(cmx_class, NULL, MKDEV(major, i), NULL,
			      "cmx%d", i);

	return 0;
}

static void reader_detach(struct pcmcia_device *link)
{
	struct reader_dev *dev = link->priv;
	int devno;

	/* find device */
	for (devno = 0; devno < CM_MAX_DEV; devno++) {
		if (dev_table[devno] == link)
			break;
	}
	if (devno == CM_MAX_DEV)
		return;

	reader_release(link);

	dev_table[devno] = NULL;
	kfree(dev);

	device_destroy(cmx_class, MKDEV(major, devno));

	return;
}

static const struct file_operations reader_fops = {
	.owner		= THIS_MODULE,
	.read		= cm4040_read,
	.write		= cm4040_write,
	.open		= cm4040_open,
	.release	= cm4040_close,
	.poll		= cm4040_poll,
};

static struct pcmcia_device_id cm4040_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x0223, 0x0200),
	PCMCIA_DEVICE_PROD_ID12("OMNIKEY", "CardMan 4040",
				0xE32CDD8C, 0x8F23318B),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, cm4040_ids);

static struct pcmcia_driver reader_driver = {
  	.owner		= THIS_MODULE,
  	.drv		= {
		.name	= "cm4040_cs",
	},
	.probe		= reader_probe,
	.remove		= reader_detach,
	.id_table	= cm4040_ids,
};

static int __init cm4040_init(void)
{
	int rc;

	printk(KERN_INFO "%s\n", version);
	cmx_class = class_create(THIS_MODULE, "cardman_4040");
	if (IS_ERR(cmx_class))
		return PTR_ERR(cmx_class);

	major = register_chrdev(0, DEVICE_NAME, &reader_fops);
	if (major < 0) {
		printk(KERN_WARNING MODULE_NAME
			": could not get major number\n");
		class_destroy(cmx_class);
		return major;
	}

	rc = pcmcia_register_driver(&reader_driver);
	if (rc < 0) {
		unregister_chrdev(major, DEVICE_NAME);
		class_destroy(cmx_class);
		return rc;
	}

	return 0;
}

static void __exit cm4040_exit(void)
{
	printk(KERN_INFO MODULE_NAME ": unloading\n");
	pcmcia_unregister_driver(&reader_driver);
	unregister_chrdev(major, DEVICE_NAME);
	class_destroy(cmx_class);
}

module_init(cm4040_init);
module_exit(cm4040_exit);
MODULE_LICENSE("Dual BSD/GPL");
