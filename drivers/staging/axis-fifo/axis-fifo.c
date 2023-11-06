// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AXIS FIFO: interface to the Xilinx AXI-Stream FIFO IP core
 *
 * Copyright (C) 2018 Jacob Feder
 *
 * Authors:  Jacob Feder <jacobsfeder@gmail.com>
 *
 * See Xilinx PG080 document for IP details
 */

/* ----------------------------
 *           includes
 * ----------------------------
 */

#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/param.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>

#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

/* ----------------------------
 *       driver parameters
 * ----------------------------
 */

#define DRIVER_NAME "axis_fifo"

#define READ_BUF_SIZE 128U /* read buffer length in words */
#define WRITE_BUF_SIZE 128U /* write buffer length in words */

/* ----------------------------
 *     IP register offsets
 * ----------------------------
 */

#define XLLF_ISR_OFFSET  0x00000000  /* Interrupt Status */
#define XLLF_IER_OFFSET  0x00000004  /* Interrupt Enable */

#define XLLF_TDFR_OFFSET 0x00000008  /* Transmit Reset */
#define XLLF_TDFV_OFFSET 0x0000000c  /* Transmit Vacancy */
#define XLLF_TDFD_OFFSET 0x00000010  /* Transmit Data */
#define XLLF_TLR_OFFSET  0x00000014  /* Transmit Length */

#define XLLF_RDFR_OFFSET 0x00000018  /* Receive Reset */
#define XLLF_RDFO_OFFSET 0x0000001c  /* Receive Occupancy */
#define XLLF_RDFD_OFFSET 0x00000020  /* Receive Data */
#define XLLF_RLR_OFFSET  0x00000024  /* Receive Length */
#define XLLF_SRR_OFFSET  0x00000028  /* Local Link Reset */
#define XLLF_TDR_OFFSET  0x0000002C  /* Transmit Destination */
#define XLLF_RDR_OFFSET  0x00000030  /* Receive Destination */

/* ----------------------------
 *     reset register masks
 * ----------------------------
 */

#define XLLF_RDFR_RESET_MASK        0x000000a5 /* receive reset value */
#define XLLF_TDFR_RESET_MASK        0x000000a5 /* Transmit reset value */
#define XLLF_SRR_RESET_MASK         0x000000a5 /* Local Link reset value */

/* ----------------------------
 *       interrupt masks
 * ----------------------------
 */

#define XLLF_INT_RPURE_MASK       0x80000000 /* Receive under-read */
#define XLLF_INT_RPORE_MASK       0x40000000 /* Receive over-read */
#define XLLF_INT_RPUE_MASK        0x20000000 /* Receive underrun (empty) */
#define XLLF_INT_TPOE_MASK        0x10000000 /* Transmit overrun */
#define XLLF_INT_TC_MASK          0x08000000 /* Transmit complete */
#define XLLF_INT_RC_MASK          0x04000000 /* Receive complete */
#define XLLF_INT_TSE_MASK         0x02000000 /* Transmit length mismatch */
#define XLLF_INT_TRC_MASK         0x01000000 /* Transmit reset complete */
#define XLLF_INT_RRC_MASK         0x00800000 /* Receive reset complete */
#define XLLF_INT_TFPF_MASK        0x00400000 /* Tx FIFO Programmable Full */
#define XLLF_INT_TFPE_MASK        0x00200000 /* Tx FIFO Programmable Empty */
#define XLLF_INT_RFPF_MASK        0x00100000 /* Rx FIFO Programmable Full */
#define XLLF_INT_RFPE_MASK        0x00080000 /* Rx FIFO Programmable Empty */
#define XLLF_INT_ALL_MASK         0xfff80000 /* All the ints */
#define XLLF_INT_ERROR_MASK       0xf2000000 /* Error status ints */
#define XLLF_INT_RXERROR_MASK     0xe0000000 /* Receive Error status ints */
#define XLLF_INT_TXERROR_MASK     0x12000000 /* Transmit Error status ints */

/* ----------------------------
 *           globals
 * ----------------------------
 */
static long read_timeout = 1000; /* ms to wait before read() times out */
static long write_timeout = 1000; /* ms to wait before write() times out */

/* ----------------------------
 * module command-line arguments
 * ----------------------------
 */

module_param(read_timeout, long, 0444);
MODULE_PARM_DESC(read_timeout, "ms to wait before blocking read() timing out; set to -1 for no timeout");
module_param(write_timeout, long, 0444);
MODULE_PARM_DESC(write_timeout, "ms to wait before blocking write() timing out; set to -1 for no timeout");

/* ----------------------------
 *            types
 * ----------------------------
 */

struct axis_fifo {
	int irq; /* interrupt */
	void __iomem *base_addr; /* kernel space memory */

	unsigned int rx_fifo_depth; /* max words in the receive fifo */
	unsigned int tx_fifo_depth; /* max words in the transmit fifo */
	int has_rx_fifo; /* whether the IP has the rx fifo enabled */
	int has_tx_fifo; /* whether the IP has the tx fifo enabled */

	wait_queue_head_t read_queue; /* wait queue for asynchronos read */
	struct mutex read_lock; /* lock for reading */
	wait_queue_head_t write_queue; /* wait queue for asynchronos write */
	struct mutex write_lock; /* lock for writing */
	unsigned int write_flags; /* write file flags */
	unsigned int read_flags; /* read file flags */

	struct device *dt_device; /* device created from the device tree */
	struct miscdevice miscdev;
};

/* ----------------------------
 *         sysfs entries
 * ----------------------------
 */

static ssize_t sysfs_write(struct device *dev, const char *buf,
			   size_t count, unsigned int addr_offset)
{
	struct axis_fifo *fifo = dev_get_drvdata(dev);
	unsigned long tmp;
	int rc;

	rc = kstrtoul(buf, 0, &tmp);
	if (rc < 0)
		return rc;

	iowrite32(tmp, fifo->base_addr + addr_offset);

	return count;
}

static ssize_t sysfs_read(struct device *dev, char *buf,
			  unsigned int addr_offset)
{
	struct axis_fifo *fifo = dev_get_drvdata(dev);
	unsigned int read_val;
	unsigned int len;
	char tmp[32];

	read_val = ioread32(fifo->base_addr + addr_offset);
	len =  snprintf(tmp, sizeof(tmp), "0x%x\n", read_val);
	memcpy(buf, tmp, len);

	return len;
}

static ssize_t isr_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_write(dev, buf, count, XLLF_ISR_OFFSET);
}

static ssize_t isr_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sysfs_read(dev, buf, XLLF_ISR_OFFSET);
}

static DEVICE_ATTR_RW(isr);

static ssize_t ier_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_write(dev, buf, count, XLLF_IER_OFFSET);
}

static ssize_t ier_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sysfs_read(dev, buf, XLLF_IER_OFFSET);
}

static DEVICE_ATTR_RW(ier);

static ssize_t tdfr_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	return sysfs_write(dev, buf, count, XLLF_TDFR_OFFSET);
}

static DEVICE_ATTR_WO(tdfr);

static ssize_t tdfv_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sysfs_read(dev, buf, XLLF_TDFV_OFFSET);
}

static DEVICE_ATTR_RO(tdfv);

static ssize_t tdfd_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	return sysfs_write(dev, buf, count, XLLF_TDFD_OFFSET);
}

static DEVICE_ATTR_WO(tdfd);

static ssize_t tlr_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_write(dev, buf, count, XLLF_TLR_OFFSET);
}

static DEVICE_ATTR_WO(tlr);

static ssize_t rdfr_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	return sysfs_write(dev, buf, count, XLLF_RDFR_OFFSET);
}

static DEVICE_ATTR_WO(rdfr);

static ssize_t rdfo_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sysfs_read(dev, buf, XLLF_RDFO_OFFSET);
}

static DEVICE_ATTR_RO(rdfo);

static ssize_t rdfd_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sysfs_read(dev, buf, XLLF_RDFD_OFFSET);
}

static DEVICE_ATTR_RO(rdfd);

static ssize_t rlr_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sysfs_read(dev, buf, XLLF_RLR_OFFSET);
}

static DEVICE_ATTR_RO(rlr);

static ssize_t srr_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_write(dev, buf, count, XLLF_SRR_OFFSET);
}

static DEVICE_ATTR_WO(srr);

static ssize_t tdr_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_write(dev, buf, count, XLLF_TDR_OFFSET);
}

static DEVICE_ATTR_WO(tdr);

static ssize_t rdr_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sysfs_read(dev, buf, XLLF_RDR_OFFSET);
}

static DEVICE_ATTR_RO(rdr);

static struct attribute *axis_fifo_attrs[] = {
	&dev_attr_isr.attr,
	&dev_attr_ier.attr,
	&dev_attr_tdfr.attr,
	&dev_attr_tdfv.attr,
	&dev_attr_tdfd.attr,
	&dev_attr_tlr.attr,
	&dev_attr_rdfr.attr,
	&dev_attr_rdfo.attr,
	&dev_attr_rdfd.attr,
	&dev_attr_rlr.attr,
	&dev_attr_srr.attr,
	&dev_attr_tdr.attr,
	&dev_attr_rdr.attr,
	NULL,
};

static const struct attribute_group axis_fifo_attrs_group = {
	.name = "ip_registers",
	.attrs = axis_fifo_attrs,
};

static const struct attribute_group *axis_fifo_attrs_groups[] = {
	&axis_fifo_attrs_group,
	NULL,
};

/* ----------------------------
 *        implementation
 * ----------------------------
 */

static void reset_ip_core(struct axis_fifo *fifo)
{
	iowrite32(XLLF_SRR_RESET_MASK, fifo->base_addr + XLLF_SRR_OFFSET);
	iowrite32(XLLF_TDFR_RESET_MASK, fifo->base_addr + XLLF_TDFR_OFFSET);
	iowrite32(XLLF_RDFR_RESET_MASK, fifo->base_addr + XLLF_RDFR_OFFSET);
	iowrite32(XLLF_INT_TC_MASK | XLLF_INT_RC_MASK | XLLF_INT_RPURE_MASK |
		  XLLF_INT_RPORE_MASK | XLLF_INT_RPUE_MASK |
		  XLLF_INT_TPOE_MASK | XLLF_INT_TSE_MASK,
		  fifo->base_addr + XLLF_IER_OFFSET);
	iowrite32(XLLF_INT_ALL_MASK, fifo->base_addr + XLLF_ISR_OFFSET);
}

/**
 * axis_fifo_read() - Read a packet from AXIS-FIFO character device.
 * @f: Open file.
 * @buf: User space buffer to read to.
 * @len: User space buffer length.
 * @off: Buffer offset.
 *
 * As defined by the device's documentation, we need to check the device's
 * occupancy before reading the length register and then the data. All these
 * operations must be executed atomically, in order and one after the other
 * without missing any.
 *
 * Returns the number of bytes read from the device or negative error code
 *	on failure.
 */
static ssize_t axis_fifo_read(struct file *f, char __user *buf,
			      size_t len, loff_t *off)
{
	struct axis_fifo *fifo = (struct axis_fifo *)f->private_data;
	size_t bytes_available;
	unsigned int words_available;
	unsigned int copied;
	unsigned int copy;
	unsigned int i;
	int ret;
	u32 tmp_buf[READ_BUF_SIZE];

	if (fifo->read_flags & O_NONBLOCK) {
		/*
		 * Device opened in non-blocking mode. Try to lock it and then
		 * check if any packet is available.
		 */
		if (!mutex_trylock(&fifo->read_lock))
			return -EAGAIN;

		if (!ioread32(fifo->base_addr + XLLF_RDFO_OFFSET)) {
			ret = -EAGAIN;
			goto end_unlock;
		}
	} else {
		/* opened in blocking mode
		 * wait for a packet available interrupt (or timeout)
		 * if nothing is currently available
		 */
		mutex_lock(&fifo->read_lock);
		ret = wait_event_interruptible_timeout(fifo->read_queue,
			ioread32(fifo->base_addr + XLLF_RDFO_OFFSET),
			read_timeout);

		if (ret <= 0) {
			if (ret == 0) {
				ret = -EAGAIN;
			} else if (ret != -ERESTARTSYS) {
				dev_err(fifo->dt_device, "wait_event_interruptible_timeout() error in read (ret=%i)\n",
					ret);
			}

			goto end_unlock;
		}
	}

	bytes_available = ioread32(fifo->base_addr + XLLF_RLR_OFFSET);
	if (!bytes_available) {
		dev_err(fifo->dt_device, "received a packet of length 0 - fifo core will be reset\n");
		reset_ip_core(fifo);
		ret = -EIO;
		goto end_unlock;
	}

	if (bytes_available > len) {
		dev_err(fifo->dt_device, "user read buffer too small (available bytes=%zu user buffer bytes=%zu) - fifo core will be reset\n",
			bytes_available, len);
		reset_ip_core(fifo);
		ret = -EINVAL;
		goto end_unlock;
	}

	if (bytes_available % sizeof(u32)) {
		/* this probably can't happen unless IP
		 * registers were previously mishandled
		 */
		dev_err(fifo->dt_device, "received a packet that isn't word-aligned - fifo core will be reset\n");
		reset_ip_core(fifo);
		ret = -EIO;
		goto end_unlock;
	}

	words_available = bytes_available / sizeof(u32);

	/* read data into an intermediate buffer, copying the contents
	 * to userspace when the buffer is full
	 */
	copied = 0;
	while (words_available > 0) {
		copy = min(words_available, READ_BUF_SIZE);

		for (i = 0; i < copy; i++) {
			tmp_buf[i] = ioread32(fifo->base_addr +
					      XLLF_RDFD_OFFSET);
		}

		if (copy_to_user(buf + copied * sizeof(u32), tmp_buf,
				 copy * sizeof(u32))) {
			reset_ip_core(fifo);
			ret = -EFAULT;
			goto end_unlock;
		}

		copied += copy;
		words_available -= copy;
	}

	ret = bytes_available;

end_unlock:
	mutex_unlock(&fifo->read_lock);

	return ret;
}

/**
 * axis_fifo_write() - Write buffer to AXIS-FIFO character device.
 * @f: Open file.
 * @buf: User space buffer to write to the device.
 * @len: User space buffer length.
 * @off: Buffer offset.
 *
 * As defined by the device's documentation, we need to write to the device's
 * data buffer then to the device's packet length register atomically. Also,
 * we need to lock before checking if the device has available space to avoid
 * any concurrency issue.
 *
 * Returns the number of bytes written to the device or negative error code
 *	on failure.
 */
static ssize_t axis_fifo_write(struct file *f, const char __user *buf,
			       size_t len, loff_t *off)
{
	struct axis_fifo *fifo = (struct axis_fifo *)f->private_data;
	unsigned int words_to_write;
	unsigned int copied;
	unsigned int copy;
	unsigned int i;
	int ret;
	u32 tmp_buf[WRITE_BUF_SIZE];

	if (len % sizeof(u32)) {
		dev_err(fifo->dt_device,
			"tried to send a packet that isn't word-aligned\n");
		return -EINVAL;
	}

	words_to_write = len / sizeof(u32);

	if (!words_to_write) {
		dev_err(fifo->dt_device,
			"tried to send a packet of length 0\n");
		return -EINVAL;
	}

	if (words_to_write > fifo->tx_fifo_depth) {
		dev_err(fifo->dt_device, "tried to write more words [%u] than slots in the fifo buffer [%u]\n",
			words_to_write, fifo->tx_fifo_depth);
		return -EINVAL;
	}

	if (fifo->write_flags & O_NONBLOCK) {
		/*
		 * Device opened in non-blocking mode. Try to lock it and then
		 * check if there is any room to write the given buffer.
		 */
		if (!mutex_trylock(&fifo->write_lock))
			return -EAGAIN;

		if (words_to_write > ioread32(fifo->base_addr +
					      XLLF_TDFV_OFFSET)) {
			ret = -EAGAIN;
			goto end_unlock;
		}
	} else {
		/* opened in blocking mode */

		/* wait for an interrupt (or timeout) if there isn't
		 * currently enough room in the fifo
		 */
		mutex_lock(&fifo->write_lock);
		ret = wait_event_interruptible_timeout(fifo->write_queue,
			ioread32(fifo->base_addr + XLLF_TDFV_OFFSET)
				 >= words_to_write,
			write_timeout);

		if (ret <= 0) {
			if (ret == 0) {
				ret = -EAGAIN;
			} else if (ret != -ERESTARTSYS) {
				dev_err(fifo->dt_device, "wait_event_interruptible_timeout() error in write (ret=%i)\n",
					ret);
			}

			goto end_unlock;
		}
	}

	/* write data from an intermediate buffer into the fifo IP, refilling
	 * the buffer with userspace data as needed
	 */
	copied = 0;
	while (words_to_write > 0) {
		copy = min(words_to_write, WRITE_BUF_SIZE);

		if (copy_from_user(tmp_buf, buf + copied * sizeof(u32),
				   copy * sizeof(u32))) {
			reset_ip_core(fifo);
			ret = -EFAULT;
			goto end_unlock;
		}

		for (i = 0; i < copy; i++)
			iowrite32(tmp_buf[i], fifo->base_addr +
				  XLLF_TDFD_OFFSET);

		copied += copy;
		words_to_write -= copy;
	}

	ret = copied * sizeof(u32);

	/* write packet size to fifo */
	iowrite32(ret, fifo->base_addr + XLLF_TLR_OFFSET);

end_unlock:
	mutex_unlock(&fifo->write_lock);

	return ret;
}

static irqreturn_t axis_fifo_irq(int irq, void *dw)
{
	struct axis_fifo *fifo = (struct axis_fifo *)dw;
	unsigned int pending_interrupts;

	do {
		pending_interrupts = ioread32(fifo->base_addr +
					      XLLF_IER_OFFSET) &
					      ioread32(fifo->base_addr
					      + XLLF_ISR_OFFSET);
		if (pending_interrupts & XLLF_INT_RC_MASK) {
			/* packet received */

			/* wake the reader process if it is waiting */
			wake_up(&fifo->read_queue);

			/* clear interrupt */
			iowrite32(XLLF_INT_RC_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_TC_MASK) {
			/* packet sent */

			/* wake the writer process if it is waiting */
			wake_up(&fifo->write_queue);

			iowrite32(XLLF_INT_TC_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_TFPF_MASK) {
			/* transmit fifo programmable full */

			iowrite32(XLLF_INT_TFPF_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_TFPE_MASK) {
			/* transmit fifo programmable empty */

			iowrite32(XLLF_INT_TFPE_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_RFPF_MASK) {
			/* receive fifo programmable full */

			iowrite32(XLLF_INT_RFPF_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_RFPE_MASK) {
			/* receive fifo programmable empty */

			iowrite32(XLLF_INT_RFPE_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_TRC_MASK) {
			/* transmit reset complete interrupt */

			iowrite32(XLLF_INT_TRC_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_RRC_MASK) {
			/* receive reset complete interrupt */

			iowrite32(XLLF_INT_RRC_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_RPURE_MASK) {
			/* receive fifo under-read error interrupt */
			dev_err(fifo->dt_device,
				"receive under-read interrupt\n");

			iowrite32(XLLF_INT_RPURE_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_RPORE_MASK) {
			/* receive over-read error interrupt */
			dev_err(fifo->dt_device,
				"receive over-read interrupt\n");

			iowrite32(XLLF_INT_RPORE_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_RPUE_MASK) {
			/* receive underrun error interrupt */
			dev_err(fifo->dt_device,
				"receive underrun error interrupt\n");

			iowrite32(XLLF_INT_RPUE_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_TPOE_MASK) {
			/* transmit overrun error interrupt */
			dev_err(fifo->dt_device,
				"transmit overrun error interrupt\n");

			iowrite32(XLLF_INT_TPOE_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts & XLLF_INT_TSE_MASK) {
			/* transmit length mismatch error interrupt */
			dev_err(fifo->dt_device,
				"transmit length mismatch error interrupt\n");

			iowrite32(XLLF_INT_TSE_MASK & XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		} else if (pending_interrupts) {
			/* unknown interrupt type */
			dev_err(fifo->dt_device,
				"unknown interrupt(s) 0x%x\n",
				pending_interrupts);

			iowrite32(XLLF_INT_ALL_MASK,
				  fifo->base_addr + XLLF_ISR_OFFSET);
		}
	} while (pending_interrupts);

	return IRQ_HANDLED;
}

static int axis_fifo_open(struct inode *inod, struct file *f)
{
	struct axis_fifo *fifo = container_of(f->private_data,
					      struct axis_fifo, miscdev);
	f->private_data = fifo;

	if (((f->f_flags & O_ACCMODE) == O_WRONLY) ||
	    ((f->f_flags & O_ACCMODE) == O_RDWR)) {
		if (fifo->has_tx_fifo) {
			fifo->write_flags = f->f_flags;
		} else {
			dev_err(fifo->dt_device, "tried to open device for write but the transmit fifo is disabled\n");
			return -EPERM;
		}
	}

	if (((f->f_flags & O_ACCMODE) == O_RDONLY) ||
	    ((f->f_flags & O_ACCMODE) == O_RDWR)) {
		if (fifo->has_rx_fifo) {
			fifo->read_flags = f->f_flags;
		} else {
			dev_err(fifo->dt_device, "tried to open device for read but the receive fifo is disabled\n");
			return -EPERM;
		}
	}

	return 0;
}

static int axis_fifo_close(struct inode *inod, struct file *f)
{
	f->private_data = NULL;

	return 0;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = axis_fifo_open,
	.release = axis_fifo_close,
	.read = axis_fifo_read,
	.write = axis_fifo_write
};

/* read named property from the device tree */
static int get_dts_property(struct axis_fifo *fifo,
			    char *name, unsigned int *var)
{
	int rc;

	rc = of_property_read_u32(fifo->dt_device->of_node, name, var);
	if (rc) {
		dev_err(fifo->dt_device, "couldn't read IP dts property '%s'",
			name);
		return rc;
	}
	dev_dbg(fifo->dt_device, "dts property '%s' = %u\n",
		name, *var);

	return 0;
}

static int axis_fifo_parse_dt(struct axis_fifo *fifo)
{
	int ret;
	unsigned int value;

	ret = get_dts_property(fifo, "xlnx,axi-str-rxd-tdata-width", &value);
	if (ret) {
		dev_err(fifo->dt_device, "missing xlnx,axi-str-rxd-tdata-width property\n");
		goto end;
	} else if (value != 32) {
		dev_err(fifo->dt_device, "xlnx,axi-str-rxd-tdata-width only supports 32 bits\n");
		ret = -EIO;
		goto end;
	}

	ret = get_dts_property(fifo, "xlnx,axi-str-txd-tdata-width", &value);
	if (ret) {
		dev_err(fifo->dt_device, "missing xlnx,axi-str-txd-tdata-width property\n");
		goto end;
	} else if (value != 32) {
		dev_err(fifo->dt_device, "xlnx,axi-str-txd-tdata-width only supports 32 bits\n");
		ret = -EIO;
		goto end;
	}

	ret = get_dts_property(fifo, "xlnx,rx-fifo-depth",
			       &fifo->rx_fifo_depth);
	if (ret) {
		dev_err(fifo->dt_device, "missing xlnx,rx-fifo-depth property\n");
		ret = -EIO;
		goto end;
	}

	ret = get_dts_property(fifo, "xlnx,tx-fifo-depth",
			       &fifo->tx_fifo_depth);
	if (ret) {
		dev_err(fifo->dt_device, "missing xlnx,tx-fifo-depth property\n");
		ret = -EIO;
		goto end;
	}

	/* IP sets TDFV to fifo depth - 4 so we will do the same */
	fifo->tx_fifo_depth -= 4;

	ret = get_dts_property(fifo, "xlnx,use-rx-data", &fifo->has_rx_fifo);
	if (ret) {
		dev_err(fifo->dt_device, "missing xlnx,use-rx-data property\n");
		ret = -EIO;
		goto end;
	}

	ret = get_dts_property(fifo, "xlnx,use-tx-data", &fifo->has_tx_fifo);
	if (ret) {
		dev_err(fifo->dt_device, "missing xlnx,use-tx-data property\n");
		ret = -EIO;
		goto end;
	}

end:
	return ret;
}

static int axis_fifo_probe(struct platform_device *pdev)
{
	struct resource *r_mem; /* IO mem resources */
	struct device *dev = &pdev->dev; /* OS device (from device tree) */
	struct axis_fifo *fifo = NULL;
	char *device_name;
	int rc = 0; /* error return value */

	/* ----------------------------
	 *     init wrapper device
	 * ----------------------------
	 */

	device_name = devm_kzalloc(dev, 32, GFP_KERNEL);
	if (!device_name)
		return -ENOMEM;

	/* allocate device wrapper memory */
	fifo = devm_kzalloc(dev, sizeof(*fifo), GFP_KERNEL);
	if (!fifo)
		return -ENOMEM;

	dev_set_drvdata(dev, fifo);
	fifo->dt_device = dev;

	init_waitqueue_head(&fifo->read_queue);
	init_waitqueue_head(&fifo->write_queue);

	mutex_init(&fifo->read_lock);
	mutex_init(&fifo->write_lock);

	/* ----------------------------
	 *   init device memory space
	 * ----------------------------
	 */

	/* get iospace for the device */
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		dev_err(fifo->dt_device, "invalid address\n");
		rc = -ENODEV;
		goto err_initial;
	}

	/* request physical memory */
	fifo->base_addr = devm_ioremap_resource(fifo->dt_device, r_mem);
	if (IS_ERR(fifo->base_addr)) {
		rc = PTR_ERR(fifo->base_addr);
		goto err_initial;
	}

	dev_dbg(fifo->dt_device, "remapped memory to 0x%p\n", fifo->base_addr);

	/* create unique device name */
	snprintf(device_name, 32, "%s_%pa", DRIVER_NAME, &r_mem->start);
	dev_dbg(fifo->dt_device, "device name [%s]\n", device_name);

	/* ----------------------------
	 *          init IP
	 * ----------------------------
	 */

	rc = axis_fifo_parse_dt(fifo);
	if (rc)
		goto err_initial;

	reset_ip_core(fifo);

	/* ----------------------------
	 *    init device interrupts
	 * ----------------------------
	 */

	/* get IRQ resource */
	rc = platform_get_irq(pdev, 0);
	if (rc < 0)
		goto err_initial;

	/* request IRQ */
	fifo->irq = rc;
	rc = devm_request_irq(fifo->dt_device, fifo->irq, &axis_fifo_irq, 0,
			      DRIVER_NAME, fifo);
	if (rc) {
		dev_err(fifo->dt_device, "couldn't allocate interrupt %i\n",
			fifo->irq);
		goto err_initial;
	}

	/* ----------------------------
	 *      init char device
	 * ----------------------------
	 */

	/* create character device */
	fifo->miscdev.fops = &fops;
	fifo->miscdev.minor = MISC_DYNAMIC_MINOR;
	fifo->miscdev.name = device_name;
	fifo->miscdev.groups = axis_fifo_attrs_groups;
	fifo->miscdev.parent = dev;
	rc = misc_register(&fifo->miscdev);
	if (rc < 0)
		goto err_initial;

	return 0;

err_initial:
	dev_set_drvdata(dev, NULL);
	return rc;
}

static void axis_fifo_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct axis_fifo *fifo = dev_get_drvdata(dev);

	misc_deregister(&fifo->miscdev);
	dev_set_drvdata(dev, NULL);
}

static const struct of_device_id axis_fifo_of_match[] = {
	{ .compatible = "xlnx,axi-fifo-mm-s-4.1", },
	{},
};
MODULE_DEVICE_TABLE(of, axis_fifo_of_match);

static struct platform_driver axis_fifo_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table	= axis_fifo_of_match,
	},
	.probe		= axis_fifo_probe,
	.remove_new	= axis_fifo_remove,
};

static int __init axis_fifo_init(void)
{
	if (read_timeout >= 0)
		read_timeout = msecs_to_jiffies(read_timeout);
	else
		read_timeout = MAX_SCHEDULE_TIMEOUT;

	if (write_timeout >= 0)
		write_timeout = msecs_to_jiffies(write_timeout);
	else
		write_timeout = MAX_SCHEDULE_TIMEOUT;

	pr_info("axis-fifo driver loaded with parameters read_timeout = %li, write_timeout = %li\n",
		read_timeout, write_timeout);
	return platform_driver_register(&axis_fifo_driver);
}

module_init(axis_fifo_init);

static void __exit axis_fifo_exit(void)
{
	platform_driver_unregister(&axis_fifo_driver);
}

module_exit(axis_fifo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jacob Feder <jacobsfeder@gmail.com>");
MODULE_DESCRIPTION("Xilinx AXI-Stream FIFO v4.1 IP core driver");
