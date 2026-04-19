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

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>
#include <linux/poll.h>

#define DRIVER_NAME "axis_fifo"

#define READ_BUF_SIZE 128U /* read buffer length in words */

#define AXIS_FIFO_DEBUG_REG_NAME_MAX_LEN	4

#define XLLF_ISR_OFFSET		0x00 /* Interrupt Status */
#define XLLF_IER_OFFSET		0x04 /* Interrupt Enable */
#define XLLF_TDFR_OFFSET	0x08 /* Transmit Reset */
#define XLLF_TDFV_OFFSET	0x0c /* Transmit Vacancy */
#define XLLF_TDFD_OFFSET	0x10 /* Transmit Data */
#define XLLF_TLR_OFFSET		0x14 /* Transmit Length */
#define XLLF_RDFR_OFFSET	0x18 /* Receive Reset */
#define XLLF_RDFO_OFFSET	0x1c /* Receive Occupancy */
#define XLLF_RDFD_OFFSET	0x20 /* Receive Data */
#define XLLF_RLR_OFFSET		0x24 /* Receive Length */
#define XLLF_SRR_OFFSET		0x28 /* Local Link Reset */
#define XLLF_TDR_OFFSET		0x2C /* Transmit Destination */
#define XLLF_RDR_OFFSET		0x30 /* Receive Destination */

#define XLLF_RDFR_RESET_MASK	0xa5 /* Receive reset value */
#define XLLF_TDFR_RESET_MASK	0xa5 /* Transmit reset value */
#define XLLF_SRR_RESET_MASK	0xa5 /* Local Link reset value */

#define XLLF_INT_RPURE_MASK	BIT(31) /* Receive under-read */
#define XLLF_INT_RPORE_MASK	BIT(30) /* Receive over-read */
#define XLLF_INT_RPUE_MASK	BIT(29) /* Receive underrun (empty) */
#define XLLF_INT_TPOE_MASK	BIT(28) /* Transmit overrun */
#define XLLF_INT_TC_MASK	BIT(27) /* Transmit complete */
#define XLLF_INT_RC_MASK	BIT(26) /* Receive complete */
#define XLLF_INT_TSE_MASK	BIT(25) /* Transmit length mismatch */

#define XLLF_INT_CLEAR_ALL	GENMASK(31, 0)

static DEFINE_IDA(axis_fifo_ida);

struct axis_fifo {
	int id;
	void __iomem *base_addr;

	unsigned int rx_fifo_depth;
	unsigned int tx_fifo_depth;
	u32 has_rx_fifo;
	u32 has_tx_fifo;

	wait_queue_head_t read_queue;
	struct mutex read_lock; /* lock for reading */
	wait_queue_head_t write_queue;
	struct mutex write_lock; /* lock for writing */

	struct device *dt_device;
	struct miscdevice miscdev;

	struct dentry *debugfs_dir;
};

struct axis_fifo_debug_reg {
	const char * const name;
	unsigned int offset;
};

static void reset_ip_core(struct axis_fifo *fifo)
{
	iowrite32(XLLF_SRR_RESET_MASK, fifo->base_addr + XLLF_SRR_OFFSET);
	iowrite32(XLLF_TDFR_RESET_MASK, fifo->base_addr + XLLF_TDFR_OFFSET);
	iowrite32(XLLF_RDFR_RESET_MASK, fifo->base_addr + XLLF_RDFR_OFFSET);
	iowrite32(XLLF_INT_TC_MASK | XLLF_INT_RC_MASK | XLLF_INT_RPURE_MASK |
		  XLLF_INT_RPORE_MASK | XLLF_INT_RPUE_MASK |
		  XLLF_INT_TPOE_MASK | XLLF_INT_TSE_MASK,
		  fifo->base_addr + XLLF_IER_OFFSET);
	iowrite32(XLLF_INT_CLEAR_ALL, fifo->base_addr + XLLF_ISR_OFFSET);
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
	struct axis_fifo *fifo = f->private_data;
	size_t bytes_available;
	unsigned int words_available;
	unsigned int copied;
	unsigned int copy;
	unsigned int i;
	int ret;
	u32 tmp_buf[READ_BUF_SIZE];

	if (f->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&fifo->read_lock))
			return -EAGAIN;

		if (!ioread32(fifo->base_addr + XLLF_RDFO_OFFSET)) {
			ret = -EAGAIN;
			goto end_unlock;
		}
	} else {
		mutex_lock(&fifo->read_lock);

		ret = wait_event_interruptible(fifo->read_queue,
					       ioread32(fifo->base_addr + XLLF_RDFO_OFFSET));
		if (ret)
			goto end_unlock;
	}

	bytes_available = ioread32(fifo->base_addr + XLLF_RLR_OFFSET);
	words_available = bytes_available / sizeof(u32);

	if (bytes_available > len) {
		ret = -EINVAL;
		goto err_flush_rx;
	}

	if (bytes_available % sizeof(u32)) {
		/* this probably can't happen unless IP
		 * registers were previously mishandled
		 */
		dev_err(fifo->dt_device, "received a packet that isn't word-aligned\n");
		ret = -EIO;
		goto err_flush_rx;
	}

	copied = 0;
	while (words_available > 0) {
		copy = min(words_available, READ_BUF_SIZE);

		for (i = 0; i < copy; i++) {
			tmp_buf[i] = ioread32(fifo->base_addr +
					      XLLF_RDFD_OFFSET);
		}
		words_available -= copy;

		if (copy_to_user(buf + copied * sizeof(u32), tmp_buf,
				 copy * sizeof(u32))) {
			ret = -EFAULT;
			goto err_flush_rx;
		}

		copied += copy;
	}
	mutex_unlock(&fifo->read_lock);

	return bytes_available;

err_flush_rx:
	while (words_available--)
		ioread32(fifo->base_addr + XLLF_RDFD_OFFSET);

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
	struct axis_fifo *fifo = f->private_data;
	unsigned int words_to_write;
	u32 *txbuf;
	int ret;

	words_to_write = len / sizeof(u32);

	/*
	 * In 'Store-and-Forward' mode, the maximum packet that can be
	 * transmitted is limited by the size of the FIFO, which is
	 * (C_TX_FIFO_DEPTH–4)*(data interface width/8) bytes.
	 *
	 * Do not attempt to send a packet larger than 'tx_fifo_depth - 4',
	 * otherwise a 'Transmit Packet Overrun Error' interrupt will be
	 * raised, which requires a reset of the TX circuit to recover.
	 */
	if (!words_to_write || (len % sizeof(u32)) ||
	    (words_to_write > (fifo->tx_fifo_depth - 4)))
		return -EINVAL;

	if (f->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&fifo->write_lock))
			return -EAGAIN;

		if (words_to_write > ioread32(fifo->base_addr +
					      XLLF_TDFV_OFFSET)) {
			ret = -EAGAIN;
			goto end_unlock;
		}
	} else {
		mutex_lock(&fifo->write_lock);

		ret = wait_event_interruptible(fifo->write_queue,
			ioread32(fifo->base_addr + XLLF_TDFV_OFFSET) >= words_to_write);
		if (ret)
			goto end_unlock;
	}

	txbuf = vmemdup_user(buf, len);
	if (IS_ERR(txbuf)) {
		ret = PTR_ERR(txbuf);
		goto end_unlock;
	}

	for (int i = 0; i < words_to_write; ++i)
		iowrite32(txbuf[i], fifo->base_addr + XLLF_TDFD_OFFSET);

	iowrite32(len, fifo->base_addr + XLLF_TLR_OFFSET);

	ret = len;
	kvfree(txbuf);
end_unlock:
	mutex_unlock(&fifo->write_lock);

	return ret;
}

static __poll_t axis_fifo_poll(struct file *f, poll_table *wait)
{
	struct axis_fifo *fifo = f->private_data;
	__poll_t mask = 0;

	if (fifo->has_rx_fifo) {
		poll_wait(f, &fifo->read_queue, wait);

		if (ioread32(fifo->base_addr + XLLF_RDFO_OFFSET))
			mask |= EPOLLIN | EPOLLRDNORM;
	}

	if (fifo->has_tx_fifo) {
		poll_wait(f, &fifo->write_queue, wait);

		if (ioread32(fifo->base_addr + XLLF_TDFV_OFFSET))
			mask |= EPOLLOUT | EPOLLWRNORM;
	}

	return mask;
}

static irqreturn_t axis_fifo_irq(int irq, void *dw)
{
	struct axis_fifo *fifo = dw;
	u32 isr, ier, intr;

	ier = ioread32(fifo->base_addr + XLLF_IER_OFFSET);
	isr = ioread32(fifo->base_addr + XLLF_ISR_OFFSET);
	intr = ier & isr;

	if (intr & XLLF_INT_RC_MASK)
		wake_up(&fifo->read_queue);

	if (intr & XLLF_INT_TC_MASK)
		wake_up(&fifo->write_queue);

	if (intr & XLLF_INT_RPURE_MASK)
		dev_err(fifo->dt_device, "receive under-read interrupt\n");

	if (intr & XLLF_INT_RPORE_MASK)
		dev_err(fifo->dt_device, "receive over-read interrupt\n");

	if (intr & XLLF_INT_RPUE_MASK)
		dev_err(fifo->dt_device, "receive underrun error interrupt\n");

	if (intr & XLLF_INT_TPOE_MASK)
		dev_err(fifo->dt_device, "transmit overrun error interrupt\n");

	if (intr & XLLF_INT_TSE_MASK)
		dev_err(fifo->dt_device,
			"transmit length mismatch error interrupt\n");

	iowrite32(XLLF_INT_CLEAR_ALL, fifo->base_addr + XLLF_ISR_OFFSET);

	return IRQ_HANDLED;
}

static int axis_fifo_open(struct inode *inod, struct file *f)
{
	struct axis_fifo *fifo = container_of(f->private_data,
					      struct axis_fifo, miscdev);
	unsigned int flags = f->f_flags & O_ACCMODE;

	f->private_data = fifo;

	if ((flags == O_WRONLY || flags == O_RDWR) && !fifo->has_tx_fifo)
		return -EPERM;

	if ((flags == O_RDONLY || flags == O_RDWR) && !fifo->has_rx_fifo)
		return -EPERM;

	return 0;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = axis_fifo_open,
	.read = axis_fifo_read,
	.write = axis_fifo_write,
	.poll = axis_fifo_poll,
};

static int axis_fifo_debugfs_regs_show(struct seq_file *m, void *p)
{
	static const struct axis_fifo_debug_reg regs[] = {
		{"isr", XLLF_ISR_OFFSET},
		{"ier", XLLF_IER_OFFSET},
		{"tdfv", XLLF_TDFV_OFFSET},
		{"rdfo", XLLF_RDFO_OFFSET},
		{ /* Sentinel */ },
	};
	const struct axis_fifo_debug_reg *reg;
	struct axis_fifo *fifo = m->private;

	for (reg = regs; reg->name; ++reg) {
		u32 val = ioread32(fifo->base_addr + reg->offset);

		seq_printf(m, "%*s: 0x%08x\n", AXIS_FIFO_DEBUG_REG_NAME_MAX_LEN,
			   reg->name, val);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(axis_fifo_debugfs_regs);

static void axis_fifo_debugfs_init(struct axis_fifo *fifo)
{
	fifo->debugfs_dir = debugfs_create_dir(dev_name(fifo->dt_device), NULL);

	debugfs_create_file("regs", 0444, fifo->debugfs_dir, fifo,
			    &axis_fifo_debugfs_regs_fops);
}

static int axis_fifo_parse_dt(struct axis_fifo *fifo)
{
	int ret;
	unsigned int value;
	struct device_node *node = fifo->dt_device->of_node;

	ret = of_property_read_u32(node, "xlnx,axi-str-rxd-tdata-width",
				   &value);
	if (ret)
		return ret;
	if (value != 32)
		return -EINVAL;

	ret = of_property_read_u32(node, "xlnx,axi-str-txd-tdata-width",
				   &value);
	if (ret)
		return ret;
	if (value != 32)
		return -EINVAL;

	ret = of_property_read_u32(node, "xlnx,rx-fifo-depth",
				   &fifo->rx_fifo_depth);
	if (ret)
		return ret;

	ret = of_property_read_u32(node, "xlnx,tx-fifo-depth",
				   &fifo->tx_fifo_depth);
	if (ret)
		return ret;

	ret = of_property_read_u32(node, "xlnx,use-rx-data",
				   &fifo->has_rx_fifo);
	if (ret)
		return ret;

	ret = of_property_read_u32(node, "xlnx,use-tx-data",
				   &fifo->has_tx_fifo);
	if (ret)
		return ret;

	return 0;
}

static int axis_fifo_probe(struct platform_device *pdev)
{
	struct resource *r_mem;
	struct device *dev = &pdev->dev;
	struct axis_fifo *fifo = NULL;
	int rc = 0; /* error return value */
	int irq;

	fifo = devm_kzalloc(dev, sizeof(*fifo), GFP_KERNEL);
	if (!fifo)
		return -ENOMEM;

	dev_set_drvdata(dev, fifo);
	fifo->dt_device = dev;

	init_waitqueue_head(&fifo->read_queue);
	init_waitqueue_head(&fifo->write_queue);

	mutex_init(&fifo->read_lock);
	mutex_init(&fifo->write_lock);

	fifo->base_addr = devm_platform_get_and_ioremap_resource(pdev, 0, &r_mem);
	if (IS_ERR(fifo->base_addr))
		return PTR_ERR(fifo->base_addr);

	rc = axis_fifo_parse_dt(fifo);
	if (rc)
		return rc;

	reset_ip_core(fifo);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	rc = devm_request_irq(fifo->dt_device, irq, &axis_fifo_irq, 0,
			      DRIVER_NAME, fifo);
	if (rc) {
		dev_err(fifo->dt_device, "couldn't allocate interrupt %i\n",
			irq);
		return rc;
	}

	fifo->id = ida_alloc(&axis_fifo_ida, GFP_KERNEL);
	if (fifo->id < 0)
		return fifo->id;

	fifo->miscdev.fops = &fops;
	fifo->miscdev.minor = MISC_DYNAMIC_MINOR;
	fifo->miscdev.parent = dev;
	fifo->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s%d",
					    DRIVER_NAME, fifo->id);
	if (!fifo->miscdev.name) {
		ida_free(&axis_fifo_ida, fifo->id);
		return -ENOMEM;
	}

	rc = misc_register(&fifo->miscdev);
	if (rc < 0) {
		ida_free(&axis_fifo_ida, fifo->id);
		return rc;
	}

	axis_fifo_debugfs_init(fifo);

	return 0;
}

static void axis_fifo_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct axis_fifo *fifo = dev_get_drvdata(dev);

	debugfs_remove(fifo->debugfs_dir);
	misc_deregister(&fifo->miscdev);
	ida_free(&axis_fifo_ida, fifo->id);
}

static const struct of_device_id axis_fifo_of_match[] = {
	{ .compatible = "xlnx,axi-fifo-mm-s-4.1", },
	{ .compatible = "xlnx,axi-fifo-mm-s-4.2", },
	{ .compatible = "xlnx,axi-fifo-mm-s-4.3", },
	{},
};
MODULE_DEVICE_TABLE(of, axis_fifo_of_match);

static struct platform_driver axis_fifo_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table	= axis_fifo_of_match,
	},
	.probe		= axis_fifo_probe,
	.remove		= axis_fifo_remove,
};

static int __init axis_fifo_init(void)
{
	return platform_driver_register(&axis_fifo_driver);
}

module_init(axis_fifo_init);

static void __exit axis_fifo_exit(void)
{
	platform_driver_unregister(&axis_fifo_driver);
	ida_destroy(&axis_fifo_ida);
}

module_exit(axis_fifo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jacob Feder <jacobsfeder@gmail.com>");
MODULE_DESCRIPTION("Xilinx AXI-Stream FIFO IP core driver");
