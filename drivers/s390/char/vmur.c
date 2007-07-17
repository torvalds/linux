/*
 * Linux driver for System z and s390 unit record devices
 * (z/VM virtual punch, reader, printer)
 *
 * Copyright IBM Corp. 2001, 2007
 * Authors: Malcolm Beattie <beattiem@uk.ibm.com>
 *	    Michael Holzheu <holzheu@de.ibm.com>
 *	    Frank Munzert <munzert@de.ibm.com>
 */

#include <linux/cdev.h>

#include <asm/uaccess.h>
#include <asm/cio.h>
#include <asm/ccwdev.h>
#include <asm/debug.h>

#include "vmur.h"

/*
 * Driver overview
 *
 * Unit record device support is implemented as a character device driver.
 * We can fit at least 16 bits into a device minor number and use the
 * simple method of mapping a character device number with minor abcd
 * to the unit record device with devno abcd.
 * I/O to virtual unit record devices is handled as follows:
 * Reads: Diagnose code 0x14 (input spool file manipulation)
 * is used to read spool data page-wise.
 * Writes: The CCW used is WRITE_CCW_CMD (0x01). The device's record length
 * is available by reading sysfs attr reclen. Each write() to the device
 * must specify an integral multiple (maximal 511) of reclen.
 */

static char ur_banner[] = "z/VM virtual unit record device driver";

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 z/VM virtual unit record device driver");
MODULE_LICENSE("GPL");

#define PRINTK_HEADER "vmur: "

static dev_t ur_first_dev_maj_min;
static struct class *vmur_class;
static struct debug_info *vmur_dbf;

/* We put the device's record length (for writes) in the driver_info field */
static struct ccw_device_id ur_ids[] = {
	{ CCWDEV_CU_DI(READER_PUNCH_DEVTYPE, 80) },
	{ CCWDEV_CU_DI(PRINTER_DEVTYPE, 132) },
	{ /* end of list */ }
};

MODULE_DEVICE_TABLE(ccw, ur_ids);

static int ur_probe(struct ccw_device *cdev);
static void ur_remove(struct ccw_device *cdev);
static int ur_set_online(struct ccw_device *cdev);
static int ur_set_offline(struct ccw_device *cdev);

static struct ccw_driver ur_driver = {
	.name		= "vmur",
	.owner		= THIS_MODULE,
	.ids		= ur_ids,
	.probe		= ur_probe,
	.remove		= ur_remove,
	.set_online	= ur_set_online,
	.set_offline	= ur_set_offline,
};

/*
 * Allocation, freeing, getting and putting of urdev structures
 */
static struct urdev *urdev_alloc(struct ccw_device *cdev)
{
	struct urdev *urd;

	urd = kzalloc(sizeof(struct urdev), GFP_KERNEL);
	if (!urd)
		return NULL;
	urd->cdev = cdev;
	urd->reclen = cdev->id.driver_info;
	ccw_device_get_id(cdev, &urd->dev_id);
	mutex_init(&urd->io_mutex);
	mutex_init(&urd->open_mutex);
	return urd;
}

static void urdev_free(struct urdev *urd)
{
	kfree(urd);
}

/*
 * This is how the character device driver gets a reference to a
 * ur device. When this call returns successfully, a reference has
 * been taken (by get_device) on the underlying kobject. The recipient
 * of this urdev pointer must eventually drop it with urdev_put(urd)
 * which does the corresponding put_device().
 */
static struct urdev *urdev_get_from_devno(u16 devno)
{
	char bus_id[16];
	struct ccw_device *cdev;

	sprintf(bus_id, "0.0.%04x", devno);
	cdev = get_ccwdev_by_busid(&ur_driver, bus_id);
	if (!cdev)
		return NULL;

	return cdev->dev.driver_data;
}

static void urdev_put(struct urdev *urd)
{
	put_device(&urd->cdev->dev);
}

/*
 * Low-level functions to do I/O to a ur device.
 *     alloc_chan_prog
 *     do_ur_io
 *     ur_int_handler
 *
 * alloc_chan_prog allocates and builds the channel program
 *
 * do_ur_io issues the channel program to the device and blocks waiting
 * on a completion event it publishes at urd->io_done. The function
 * serialises itself on the device's mutex so that only one I/O
 * is issued at a time (and that I/O is synchronous).
 *
 * ur_int_handler catches the "I/O done" interrupt, writes the
 * subchannel status word into the scsw member of the urdev structure
 * and complete()s the io_done to wake the waiting do_ur_io.
 *
 * The caller of do_ur_io is responsible for kfree()ing the channel program
 * address pointer that alloc_chan_prog returned.
 */


/*
 * alloc_chan_prog
 * The channel program we use is write commands chained together
 * with a final NOP CCW command-chained on (which ensures that CE and DE
 * are presented together in a single interrupt instead of as separate
 * interrupts unless an incorrect length indication kicks in first). The
 * data length in each CCW is reclen. The caller must ensure that count
 * is an integral multiple of reclen.
 * The channel program pointer returned by this function must be freed
 * with kfree. The caller is responsible for checking that
 * count/reclen is not ridiculously large.
 */
static struct ccw1 *alloc_chan_prog(char *buf, size_t count, size_t reclen)
{
	size_t num_ccws;
	struct ccw1 *cpa;
	int i;

	TRACE("alloc_chan_prog(%p, %zu, %zu)\n", buf, count, reclen);

	/*
	 * We chain a NOP onto the writes to force CE+DE together.
	 * That means we allocate room for CCWs to cover count/reclen
	 * records plus a NOP.
	 */
	num_ccws = count / reclen + 1;
	cpa = kmalloc(num_ccws * sizeof(struct ccw1), GFP_KERNEL | GFP_DMA);
	if (!cpa)
		return NULL;

	for (i = 0; count; i++) {
		cpa[i].cmd_code = WRITE_CCW_CMD;
		cpa[i].flags = CCW_FLAG_CC | CCW_FLAG_SLI;
		cpa[i].count = reclen;
		cpa[i].cda = __pa(buf);
		buf += reclen;
		count -= reclen;
	}
	/* The following NOP CCW forces CE+DE to be presented together */
	cpa[i].cmd_code = CCW_CMD_NOOP;
	cpa[i].flags = 0;
	cpa[i].count = 0;
	cpa[i].cda = 0;

	return cpa;
}

static int do_ur_io(struct urdev *urd, struct ccw1 *cpa)
{
	int rc;
	struct ccw_device *cdev = urd->cdev;
	DECLARE_COMPLETION(event);

	TRACE("do_ur_io: cpa=%p\n", cpa);

	rc = mutex_lock_interruptible(&urd->io_mutex);
	if (rc)
		return rc;

	urd->io_done = &event;

	spin_lock_irq(get_ccwdev_lock(cdev));
	rc = ccw_device_start(cdev, cpa, 1, 0, 0);
	spin_unlock_irq(get_ccwdev_lock(cdev));

	TRACE("do_ur_io: ccw_device_start returned %d\n", rc);
	if (rc)
		goto out;

	wait_for_completion(&event);
	TRACE("do_ur_io: I/O complete\n");
	rc = 0;

out:
	mutex_unlock(&urd->io_mutex);
	return rc;
}

/*
 * ur interrupt handler, called from the ccw_device layer
 */
static void ur_int_handler(struct ccw_device *cdev, unsigned long intparm,
			   struct irb *irb)
{
	struct urdev *urd;

	TRACE("ur_int_handler: intparm=0x%lx cstat=%02x dstat=%02x res=%u\n",
	      intparm, irb->scsw.cstat, irb->scsw.dstat, irb->scsw.count);

	if (!intparm) {
		TRACE("ur_int_handler: unsolicited interrupt\n");
		return;
	}
	urd = cdev->dev.driver_data;
	/* On special conditions irb is an error pointer */
	if (IS_ERR(irb))
		urd->io_request_rc = PTR_ERR(irb);
	else if (irb->scsw.dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		urd->io_request_rc = 0;
	else
		urd->io_request_rc = -EIO;

	complete(urd->io_done);
}

/*
 * reclen sysfs attribute - The record length to be used for write CCWs
 */
static ssize_t ur_attr_reclen_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct urdev *urd = dev->driver_data;

	return sprintf(buf, "%zu\n", urd->reclen);
}

static DEVICE_ATTR(reclen, 0444, ur_attr_reclen_show, NULL);

static int ur_create_attributes(struct device *dev)
{
	return device_create_file(dev, &dev_attr_reclen);
}

static void ur_remove_attributes(struct device *dev)
{
	device_remove_file(dev, &dev_attr_reclen);
}

/*
 * diagnose code 0x210 - retrieve device information
 * cc=0  normal completion, we have a real device
 * cc=1  CP paging error
 * cc=2  The virtual device exists, but is not associated with a real device
 * cc=3  Invalid device address, or the virtual device does not exist
 */
static int get_urd_class(struct urdev *urd)
{
	static struct diag210 ur_diag210;
	int cc;

	ur_diag210.vrdcdvno = urd->dev_id.devno;
	ur_diag210.vrdclen = sizeof(struct diag210);

	cc = diag210(&ur_diag210);
	switch (cc) {
	case 0:
		return -ENOTSUPP;
	case 2:
		return ur_diag210.vrdcvcla; /* virtual device class */
	case 3:
		return -ENODEV;
	default:
		return -EIO;
	}
}

/*
 * Allocation and freeing of urfile structures
 */
static struct urfile *urfile_alloc(struct urdev *urd)
{
	struct urfile *urf;

	urf = kzalloc(sizeof(struct urfile), GFP_KERNEL);
	if (!urf)
		return NULL;
	urf->urd = urd;

	TRACE("urfile_alloc: urd=%p urf=%p rl=%zu\n", urd, urf,
	      urf->dev_reclen);

	return urf;
}

static void urfile_free(struct urfile *urf)
{
	TRACE("urfile_free: urf=%p urd=%p\n", urf, urf->urd);
	kfree(urf);
}

/*
 * The fops implementation of the character device driver
 */
static ssize_t do_write(struct urdev *urd, const char __user *udata,
			size_t count, size_t reclen, loff_t *ppos)
{
	struct ccw1 *cpa;
	char *buf;
	int rc;

	/* Data buffer must be under 2GB line for fmt1 CCWs: hence GFP_DMA */
	buf = kmalloc(count, GFP_KERNEL | GFP_DMA);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, udata, count)) {
		rc = -EFAULT;
		goto fail_kfree_buf;
	}

	cpa = alloc_chan_prog(buf, count, reclen);
	if (!cpa) {
		rc = -ENOMEM;
		goto fail_kfree_buf;
	}

	rc = do_ur_io(urd, cpa);
	if (rc)
		goto fail_kfree_cpa;

	if (urd->io_request_rc) {
		rc = urd->io_request_rc;
		goto fail_kfree_cpa;
	}
	*ppos += count;
	rc = count;
fail_kfree_cpa:
	kfree(cpa);
fail_kfree_buf:
	kfree(buf);
	return rc;
}

static ssize_t ur_write(struct file *file, const char __user *udata,
			size_t count, loff_t *ppos)
{
	struct urfile *urf = file->private_data;

	TRACE("ur_write: count=%zu\n", count);

	if (count == 0)
		return 0;

	if (count % urf->dev_reclen)
		return -EINVAL;	/* count must be a multiple of reclen */

	if (count > urf->dev_reclen * MAX_RECS_PER_IO)
		count = urf->dev_reclen * MAX_RECS_PER_IO;

	return do_write(urf->urd, udata, count, urf->dev_reclen, ppos);
}

static int do_diag_14(unsigned long rx, unsigned long ry1,
		      unsigned long subcode)
{
	register unsigned long _ry1 asm("2") = ry1;
	register unsigned long _ry2 asm("3") = subcode;
	int rc = 0;

	asm volatile(
#ifdef CONFIG_64BIT
		"   sam31\n"
		"   diag    %2,2,0x14\n"
		"   sam64\n"
#else
		"   diag    %2,2,0x14\n"
#endif
		"   ipm     %0\n"
		"   srl     %0,28\n"
		: "=d" (rc), "+d" (_ry2)
		: "d" (rx), "d" (_ry1)
		: "cc");

	TRACE("diag 14: subcode=0x%lx, cc=%i\n", subcode, rc);
	return rc;
}

/*
 * diagnose code 0x14 subcode 0x0028 - position spool file to designated
 *				       record
 * cc=0  normal completion
 * cc=2  no file active on the virtual reader or device not ready
 * cc=3  record specified is beyond EOF
 */
static int diag_position_to_record(int devno, int record)
{
	int cc;

	cc = do_diag_14(record, devno, 0x28);
	switch (cc) {
	case 0:
		return 0;
	case 2:
		return -ENOMEDIUM;
	case 3:
		return -ENODATA; /* position beyond end of file */
	default:
		return -EIO;
	}
}

/*
 * diagnose code 0x14 subcode 0x0000 - read next spool file buffer
 * cc=0  normal completion
 * cc=1  EOF reached
 * cc=2  no file active on the virtual reader, and no file eligible
 * cc=3  file already active on the virtual reader or specified virtual
 *	 reader does not exist or is not a reader
 */
static int diag_read_file(int devno, char *buf)
{
	int cc;

	cc = do_diag_14((unsigned long) buf, devno, 0x00);
	switch (cc) {
	case 0:
		return 0;
	case 1:
		return -ENODATA;
	case 2:
		return -ENOMEDIUM;
	default:
		return -EIO;
	}
}

static ssize_t diag14_read(struct file *file, char __user *ubuf, size_t count,
			   loff_t *offs)
{
	size_t len, copied, res;
	char *buf;
	int rc;
	u16 reclen;
	struct urdev *urd;

	urd = ((struct urfile *) file->private_data)->urd;
	reclen = ((struct urfile *) file->private_data)->file_reclen;

	rc = diag_position_to_record(urd->dev_id.devno, *offs / PAGE_SIZE + 1);
	if (rc == -ENODATA)
		return 0;
	if (rc)
		return rc;

	len = min((size_t) PAGE_SIZE, count);
	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	copied = 0;
	res = (size_t) (*offs % PAGE_SIZE);
	do {
		rc = diag_read_file(urd->dev_id.devno, buf);
		if (rc == -ENODATA) {
			break;
		}
		if (rc)
			goto fail;
		if (reclen)
			*((u16 *) &buf[FILE_RECLEN_OFFSET]) = reclen;
		len = min(count - copied, PAGE_SIZE - res);
		if (copy_to_user(ubuf + copied, buf + res, len)) {
			rc = -EFAULT;
			goto fail;
		}
		res = 0;
		copied += len;
	} while (copied != count);

	*offs += copied;
	rc = copied;
fail:
	kfree(buf);
	return rc;
}

static ssize_t ur_read(struct file *file, char __user *ubuf, size_t count,
		       loff_t *offs)
{
	struct urdev *urd;
	int rc;

	TRACE("ur_read: count=%zu ppos=%li\n", count, (unsigned long) *offs);

	if (count == 0)
		return 0;

	urd = ((struct urfile *) file->private_data)->urd;
	rc = mutex_lock_interruptible(&urd->io_mutex);
	if (rc)
		return rc;
	rc = diag14_read(file, ubuf, count, offs);
	mutex_unlock(&urd->io_mutex);
	return rc;
}

/*
 * diagnose code 0x14 subcode 0x0fff - retrieve next file descriptor
 * cc=0  normal completion
 * cc=1  no files on reader queue or no subsequent file
 * cc=2  spid specified is invalid
 */
static int diag_read_next_file_info(struct file_control_block *buf, int spid)
{
	int cc;

	cc = do_diag_14((unsigned long) buf, spid, 0xfff);
	switch (cc) {
	case 0:
		return 0;
	default:
		return -ENODATA;
	}
}

static int verify_device(struct urdev *urd)
{
	struct file_control_block fcb;
	char *buf;
	int rc;

	switch (urd->class) {
	case DEV_CLASS_UR_O:
		return 0; /* no check needed here */
	case DEV_CLASS_UR_I:
		/* check for empty reader device (beginning of chain) */
		rc = diag_read_next_file_info(&fcb, 0);
		if (rc)
			return rc;

		/* open file on virtual reader	*/
		buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		rc = diag_read_file(urd->dev_id.devno, buf);
		kfree(buf);

		if ((rc != 0) && (rc != -ENODATA)) /* EOF does not hurt */
			return rc;
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int get_file_reclen(struct urdev *urd)
{
	struct file_control_block fcb;
	int rc;

	switch (urd->class) {
	case DEV_CLASS_UR_O:
		return 0;
	case DEV_CLASS_UR_I:
		rc = diag_read_next_file_info(&fcb, 0);
		if (rc)
			return rc;
		break;
	default:
		return -ENOTSUPP;
	}
	if (fcb.file_stat & FLG_CP_DUMP)
		return 0;

	return fcb.rec_len;
}

static int ur_open(struct inode *inode, struct file *file)
{
	u16 devno;
	struct urdev *urd;
	struct urfile *urf;
	unsigned short accmode;
	int rc;

	accmode = file->f_flags & O_ACCMODE;

	if (accmode == O_RDWR)
		return -EACCES;

	/*
	 * We treat the minor number as the devno of the ur device
	 * to find in the driver tree.
	 */
	devno = MINOR(file->f_dentry->d_inode->i_rdev);

	urd = urdev_get_from_devno(devno);
	if (!urd)
		return -ENXIO;

	if (file->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&urd->open_mutex)) {
			rc = -EBUSY;
			goto fail_put;
		}
	} else {
		if (mutex_lock_interruptible(&urd->open_mutex)) {
			rc = -ERESTARTSYS;
			goto fail_put;
		}
	}

	TRACE("ur_open\n");

	if (((accmode == O_RDONLY) && (urd->class != DEV_CLASS_UR_I)) ||
	    ((accmode == O_WRONLY) && (urd->class != DEV_CLASS_UR_O))) {
		TRACE("ur_open: unsupported dev class (%d)\n", urd->class);
		rc = -EACCES;
		goto fail_unlock;
	}

	rc = verify_device(urd);
	if (rc)
		goto fail_unlock;

	urf = urfile_alloc(urd);
	if (!urf) {
		rc = -ENOMEM;
		goto fail_unlock;
	}

	urf->dev_reclen = urd->reclen;
	rc = get_file_reclen(urd);
	if (rc < 0)
		goto fail_urfile_free;
	urf->file_reclen = rc;
	file->private_data = urf;
	return 0;

fail_urfile_free:
	urfile_free(urf);
fail_unlock:
	mutex_unlock(&urd->open_mutex);
fail_put:
	urdev_put(urd);
	return rc;
}

static int ur_release(struct inode *inode, struct file *file)
{
	struct urfile *urf = file->private_data;

	TRACE("ur_release\n");
	mutex_unlock(&urf->urd->open_mutex);
	urdev_put(urf->urd);
	urfile_free(urf);
	return 0;
}

static loff_t ur_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t newpos;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		return -ESPIPE; /* seek allowed only for reader */
	if (offset % PAGE_SIZE)
		return -ESPIPE; /* only multiples of 4K allowed */
	switch (whence) {
	case 0: /* SEEK_SET */
		newpos = offset;
		break;
	case 1: /* SEEK_CUR */
		newpos = file->f_pos + offset;
		break;
	default:
		return -EINVAL;
	}
	file->f_pos = newpos;
	return newpos;
}

static struct file_operations ur_fops = {
	.owner	 = THIS_MODULE,
	.open	 = ur_open,
	.release = ur_release,
	.read	 = ur_read,
	.write	 = ur_write,
	.llseek  = ur_llseek,
};

/*
 * ccw_device infrastructure:
 *     ur_probe gets its own ref to the device (i.e. get_device),
 *     creates the struct urdev, the device attributes, sets up
 *     the interrupt handler and validates the virtual unit record device.
 *     ur_remove removes the device attributes, frees the struct urdev
 *     and drops (put_device) the ref to the device we got in ur_probe.
 */
static int ur_probe(struct ccw_device *cdev)
{
	struct urdev *urd;
	int rc;

	TRACE("ur_probe: cdev=%p state=%d\n", cdev, *(int *) cdev->private);

	if (!get_device(&cdev->dev))
		return -ENODEV;

	urd = urdev_alloc(cdev);
	if (!urd) {
		rc = -ENOMEM;
		goto fail;
	}
	rc = ur_create_attributes(&cdev->dev);
	if (rc) {
		rc = -ENOMEM;
		goto fail;
	}
	cdev->dev.driver_data = urd;
	cdev->handler = ur_int_handler;

	/* validate virtual unit record device */
	urd->class = get_urd_class(urd);
	if (urd->class < 0) {
		rc = urd->class;
		goto fail;
	}
	if ((urd->class != DEV_CLASS_UR_I) && (urd->class != DEV_CLASS_UR_O)) {
		rc = -ENOTSUPP;
		goto fail;
	}

	return 0;

fail:
	urdev_free(urd);
	put_device(&cdev->dev);
	return rc;
}

static void ur_remove(struct ccw_device *cdev)
{
	struct urdev *urd = cdev->dev.driver_data;

	TRACE("ur_remove\n");
	if (cdev->online)
		ur_set_offline(cdev);
	ur_remove_attributes(&cdev->dev);
	urdev_free(urd);
	put_device(&cdev->dev);
}

static int ur_set_online(struct ccw_device *cdev)
{
	struct urdev *urd;
	int minor, major, rc;
	char node_id[16];

	TRACE("ur_set_online: cdev=%p state=%d\n", cdev,
	      *(int *) cdev->private);

	if (!try_module_get(ur_driver.owner))
		return -EINVAL;

	urd = (struct urdev *) cdev->dev.driver_data;
	minor = urd->dev_id.devno;
	major = MAJOR(ur_first_dev_maj_min);

	urd->char_device = cdev_alloc();
	if (!urd->char_device) {
		rc = -ENOMEM;
		goto fail_module_put;
	}

	cdev_init(urd->char_device, &ur_fops);
	urd->char_device->dev = MKDEV(major, minor);
	urd->char_device->owner = ur_fops.owner;

	rc = cdev_add(urd->char_device, urd->char_device->dev, 1);
	if (rc)
		goto fail_free_cdev;
	if (urd->cdev->id.cu_type == READER_PUNCH_DEVTYPE) {
		if (urd->class == DEV_CLASS_UR_I)
			sprintf(node_id, "vmrdr-%s", cdev->dev.bus_id);
		if (urd->class == DEV_CLASS_UR_O)
			sprintf(node_id, "vmpun-%s", cdev->dev.bus_id);
	} else if (urd->cdev->id.cu_type == PRINTER_DEVTYPE) {
		sprintf(node_id, "vmprt-%s", cdev->dev.bus_id);
	} else {
		rc = -ENOTSUPP;
		goto fail_free_cdev;
	}

	urd->device = device_create(vmur_class, NULL, urd->char_device->dev,
					"%s", node_id);
	if (IS_ERR(urd->device)) {
		rc = PTR_ERR(urd->device);
		TRACE("ur_set_online: device_create rc=%d\n", rc);
		goto fail_free_cdev;
	}

	return 0;

fail_free_cdev:
	cdev_del(urd->char_device);
fail_module_put:
	module_put(ur_driver.owner);

	return rc;
}

static int ur_set_offline(struct ccw_device *cdev)
{
	struct urdev *urd;

	TRACE("ur_set_offline: cdev=%p cdev->private=%p state=%d\n",
		cdev, cdev->private, *(int *) cdev->private);
	urd = (struct urdev *) cdev->dev.driver_data;
	device_destroy(vmur_class, urd->char_device->dev);
	cdev_del(urd->char_device);
	module_put(ur_driver.owner);

	return 0;
}

/*
 * Module initialisation and cleanup
 */
static int __init ur_init(void)
{
	int rc;
	dev_t dev;

	if (!MACHINE_IS_VM) {
		PRINT_ERR("%s is only available under z/VM.\n", ur_banner);
		return -ENODEV;
	}

	vmur_dbf = debug_register("vmur", 4, 1, 4 * sizeof(long));
	if (!vmur_dbf)
		return -ENOMEM;
	rc = debug_register_view(vmur_dbf, &debug_sprintf_view);
	if (rc)
		goto fail_free_dbf;

	debug_set_level(vmur_dbf, 6);

	rc = ccw_driver_register(&ur_driver);
	if (rc)
		goto fail_free_dbf;

	rc = alloc_chrdev_region(&dev, 0, NUM_MINORS, "vmur");
	if (rc) {
		PRINT_ERR("alloc_chrdev_region failed: err = %d\n", rc);
		goto fail_unregister_driver;
	}
	ur_first_dev_maj_min = MKDEV(MAJOR(dev), 0);

	vmur_class = class_create(THIS_MODULE, "vmur");
	if (IS_ERR(vmur_class)) {
		rc = PTR_ERR(vmur_class);
		goto fail_unregister_region;
	}
	PRINT_INFO("%s loaded.\n", ur_banner);
	return 0;

fail_unregister_region:
	unregister_chrdev_region(ur_first_dev_maj_min, NUM_MINORS);
fail_unregister_driver:
	ccw_driver_unregister(&ur_driver);
fail_free_dbf:
	debug_unregister(vmur_dbf);
	return rc;
}

static void __exit ur_exit(void)
{
	class_destroy(vmur_class);
	unregister_chrdev_region(ur_first_dev_maj_min, NUM_MINORS);
	ccw_driver_unregister(&ur_driver);
	debug_unregister(vmur_dbf);
	PRINT_INFO("%s unloaded.\n", ur_banner);
}

module_init(ur_init);
module_exit(ur_exit);
