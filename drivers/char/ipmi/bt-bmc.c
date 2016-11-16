/*
 * Copyright (c) 2015-2016, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/atomic.h>
#include <linux/bt-bmc.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/timer.h>

/*
 * This is a BMC device used to communicate to the host
 */
#define DEVICE_NAME	"ipmi-bt-host"

#define BT_IO_BASE	0xe4
#define BT_IRQ		10

#define BT_CR0		0x0
#define   BT_CR0_IO_BASE		16
#define   BT_CR0_IRQ			12
#define   BT_CR0_EN_CLR_SLV_RDP		0x8
#define   BT_CR0_EN_CLR_SLV_WRP		0x4
#define   BT_CR0_ENABLE_IBT		0x1
#define BT_CR1		0x4
#define   BT_CR1_IRQ_H2B	0x01
#define   BT_CR1_IRQ_HBUSY	0x40
#define BT_CR2		0x8
#define   BT_CR2_IRQ_H2B	0x01
#define   BT_CR2_IRQ_HBUSY	0x40
#define BT_CR3		0xc
#define BT_CTRL		0x10
#define   BT_CTRL_B_BUSY		0x80
#define   BT_CTRL_H_BUSY		0x40
#define   BT_CTRL_OEM0			0x20
#define   BT_CTRL_SMS_ATN		0x10
#define   BT_CTRL_B2H_ATN		0x08
#define   BT_CTRL_H2B_ATN		0x04
#define   BT_CTRL_CLR_RD_PTR		0x02
#define   BT_CTRL_CLR_WR_PTR		0x01
#define BT_BMC2HOST	0x14
#define BT_INTMASK	0x18
#define   BT_INTMASK_B2H_IRQEN		0x01
#define   BT_INTMASK_B2H_IRQ		0x02
#define   BT_INTMASK_BMC_HWRST		0x80

#define BT_BMC_BUFFER_SIZE 256

struct bt_bmc {
	struct device		dev;
	struct miscdevice	miscdev;
	void __iomem		*base;
	int			irq;
	wait_queue_head_t	queue;
	struct timer_list	poll_timer;
	struct mutex		mutex;
};

static atomic_t open_count = ATOMIC_INIT(0);

static u8 bt_inb(struct bt_bmc *bt_bmc, int reg)
{
	return ioread8(bt_bmc->base + reg);
}

static void bt_outb(struct bt_bmc *bt_bmc, u8 data, int reg)
{
	iowrite8(data, bt_bmc->base + reg);
}

static void clr_rd_ptr(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_CLR_RD_PTR, BT_CTRL);
}

static void clr_wr_ptr(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_CLR_WR_PTR, BT_CTRL);
}

static void clr_h2b_atn(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_H2B_ATN, BT_CTRL);
}

static void set_b_busy(struct bt_bmc *bt_bmc)
{
	if (!(bt_inb(bt_bmc, BT_CTRL) & BT_CTRL_B_BUSY))
		bt_outb(bt_bmc, BT_CTRL_B_BUSY, BT_CTRL);
}

static void clr_b_busy(struct bt_bmc *bt_bmc)
{
	if (bt_inb(bt_bmc, BT_CTRL) & BT_CTRL_B_BUSY)
		bt_outb(bt_bmc, BT_CTRL_B_BUSY, BT_CTRL);
}

static void set_b2h_atn(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_B2H_ATN, BT_CTRL);
}

static u8 bt_read(struct bt_bmc *bt_bmc)
{
	return bt_inb(bt_bmc, BT_BMC2HOST);
}

static ssize_t bt_readn(struct bt_bmc *bt_bmc, u8 *buf, size_t n)
{
	int i;

	for (i = 0; i < n; i++)
		buf[i] = bt_read(bt_bmc);
	return n;
}

static void bt_write(struct bt_bmc *bt_bmc, u8 c)
{
	bt_outb(bt_bmc, c, BT_BMC2HOST);
}

static ssize_t bt_writen(struct bt_bmc *bt_bmc, u8 *buf, size_t n)
{
	int i;

	for (i = 0; i < n; i++)
		bt_write(bt_bmc, buf[i]);
	return n;
}

static void set_sms_atn(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_SMS_ATN, BT_CTRL);
}

static struct bt_bmc *file_bt_bmc(struct file *file)
{
	return container_of(file->private_data, struct bt_bmc, miscdev);
}

static int bt_bmc_open(struct inode *inode, struct file *file)
{
	struct bt_bmc *bt_bmc = file_bt_bmc(file);

	if (atomic_inc_return(&open_count) == 1) {
		clr_b_busy(bt_bmc);
		return 0;
	}

	atomic_dec(&open_count);
	return -EBUSY;
}

/*
 * The BT (Block Transfer) interface means that entire messages are
 * buffered by the host before a notification is sent to the BMC that
 * there is data to be read. The first byte is the length and the
 * message data follows. The read operation just tries to capture the
 * whole before returning it to userspace.
 *
 * BT Message format :
 *
 *    Byte 1  Byte 2     Byte 3  Byte 4  Byte 5:N
 *    Length  NetFn/LUN  Seq     Cmd     Data
 *
 */
static ssize_t bt_bmc_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct bt_bmc *bt_bmc = file_bt_bmc(file);
	u8 len;
	int len_byte = 1;
	u8 kbuffer[BT_BMC_BUFFER_SIZE];
	ssize_t ret = 0;
	ssize_t nread;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	WARN_ON(*ppos);

	if (wait_event_interruptible(bt_bmc->queue,
				     bt_inb(bt_bmc, BT_CTRL) & BT_CTRL_H2B_ATN))
		return -ERESTARTSYS;

	mutex_lock(&bt_bmc->mutex);

	if (unlikely(!(bt_inb(bt_bmc, BT_CTRL) & BT_CTRL_H2B_ATN))) {
		ret = -EIO;
		goto out_unlock;
	}

	set_b_busy(bt_bmc);
	clr_h2b_atn(bt_bmc);
	clr_rd_ptr(bt_bmc);

	/*
	 * The BT frames start with the message length, which does not
	 * include the length byte.
	 */
	kbuffer[0] = bt_read(bt_bmc);
	len = kbuffer[0];

	/* We pass the length back to userspace as well */
	if (len + 1 > count)
		len = count - 1;

	while (len) {
		nread = min_t(ssize_t, len, sizeof(kbuffer) - len_byte);

		bt_readn(bt_bmc, kbuffer + len_byte, nread);

		if (copy_to_user(buf, kbuffer, nread + len_byte)) {
			ret = -EFAULT;
			break;
		}
		len -= nread;
		buf += nread + len_byte;
		ret += nread + len_byte;
		len_byte = 0;
	}

	clr_b_busy(bt_bmc);

out_unlock:
	mutex_unlock(&bt_bmc->mutex);
	return ret;
}

/*
 * BT Message response format :
 *
 *    Byte 1  Byte 2     Byte 3  Byte 4  Byte 5  Byte 6:N
 *    Length  NetFn/LUN  Seq     Cmd     Code    Data
 */
static ssize_t bt_bmc_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct bt_bmc *bt_bmc = file_bt_bmc(file);
	u8 kbuffer[BT_BMC_BUFFER_SIZE];
	ssize_t ret = 0;
	ssize_t nwritten;

	/*
	 * send a minimum response size
	 */
	if (count < 5)
		return -EINVAL;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	WARN_ON(*ppos);

	/*
	 * There's no interrupt for clearing bmc busy so we have to
	 * poll
	 */
	if (wait_event_interruptible(bt_bmc->queue,
				     !(bt_inb(bt_bmc, BT_CTRL) &
				       (BT_CTRL_H_BUSY | BT_CTRL_B2H_ATN))))
		return -ERESTARTSYS;

	mutex_lock(&bt_bmc->mutex);

	if (unlikely(bt_inb(bt_bmc, BT_CTRL) &
		     (BT_CTRL_H_BUSY | BT_CTRL_B2H_ATN))) {
		ret = -EIO;
		goto out_unlock;
	}

	clr_wr_ptr(bt_bmc);

	while (count) {
		nwritten = min_t(ssize_t, count, sizeof(kbuffer));
		if (copy_from_user(&kbuffer, buf, nwritten)) {
			ret = -EFAULT;
			break;
		}

		bt_writen(bt_bmc, kbuffer, nwritten);

		count -= nwritten;
		buf += nwritten;
		ret += nwritten;
	}

	set_b2h_atn(bt_bmc);

out_unlock:
	mutex_unlock(&bt_bmc->mutex);
	return ret;
}

static long bt_bmc_ioctl(struct file *file, unsigned int cmd,
			 unsigned long param)
{
	struct bt_bmc *bt_bmc = file_bt_bmc(file);

	switch (cmd) {
	case BT_BMC_IOCTL_SMS_ATN:
		set_sms_atn(bt_bmc);
		return 0;
	}
	return -EINVAL;
}

static int bt_bmc_release(struct inode *inode, struct file *file)
{
	struct bt_bmc *bt_bmc = file_bt_bmc(file);

	atomic_dec(&open_count);
	set_b_busy(bt_bmc);
	return 0;
}

static unsigned int bt_bmc_poll(struct file *file, poll_table *wait)
{
	struct bt_bmc *bt_bmc = file_bt_bmc(file);
	unsigned int mask = 0;
	u8 ctrl;

	poll_wait(file, &bt_bmc->queue, wait);

	ctrl = bt_inb(bt_bmc, BT_CTRL);

	if (ctrl & BT_CTRL_H2B_ATN)
		mask |= POLLIN;

	if (!(ctrl & (BT_CTRL_H_BUSY | BT_CTRL_B2H_ATN)))
		mask |= POLLOUT;

	return mask;
}

static const struct file_operations bt_bmc_fops = {
	.owner		= THIS_MODULE,
	.open		= bt_bmc_open,
	.read		= bt_bmc_read,
	.write		= bt_bmc_write,
	.release	= bt_bmc_release,
	.poll		= bt_bmc_poll,
	.unlocked_ioctl	= bt_bmc_ioctl,
};

static void poll_timer(unsigned long data)
{
	struct bt_bmc *bt_bmc = (void *)data;

	bt_bmc->poll_timer.expires += msecs_to_jiffies(500);
	wake_up(&bt_bmc->queue);
	add_timer(&bt_bmc->poll_timer);
}

static irqreturn_t bt_bmc_irq(int irq, void *arg)
{
	struct bt_bmc *bt_bmc = arg;
	u32 reg;

	reg = ioread32(bt_bmc->base + BT_CR2);
	reg &= BT_CR2_IRQ_H2B | BT_CR2_IRQ_HBUSY;
	if (!reg)
		return IRQ_NONE;

	/* ack pending IRQs */
	iowrite32(reg, bt_bmc->base + BT_CR2);

	wake_up(&bt_bmc->queue);
	return IRQ_HANDLED;
}

static int bt_bmc_config_irq(struct bt_bmc *bt_bmc,
			     struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	u32 reg;
	int rc;

	bt_bmc->irq = platform_get_irq(pdev, 0);
	if (!bt_bmc->irq)
		return -ENODEV;

	rc = devm_request_irq(dev, bt_bmc->irq, bt_bmc_irq, IRQF_SHARED,
			      DEVICE_NAME, bt_bmc);
	if (rc < 0) {
		dev_warn(dev, "Unable to request IRQ %d\n", bt_bmc->irq);
		bt_bmc->irq = 0;
		return rc;
	}

	/*
	 * Configure IRQs on the bmc clearing the H2B and HBUSY bits;
	 * H2B will be asserted when the bmc has data for us; HBUSY
	 * will be cleared (along with B2H) when we can write the next
	 * message to the BT buffer
	 */
	reg = ioread32(bt_bmc->base + BT_CR1);
	reg |= BT_CR1_IRQ_H2B | BT_CR1_IRQ_HBUSY;
	iowrite32(reg, bt_bmc->base + BT_CR1);

	return 0;
}

static int bt_bmc_probe(struct platform_device *pdev)
{
	struct bt_bmc *bt_bmc;
	struct device *dev;
	struct resource *res;
	int rc;

	if (!pdev || !pdev->dev.of_node)
		return -ENODEV;

	dev = &pdev->dev;
	dev_info(dev, "Found bt bmc device\n");

	bt_bmc = devm_kzalloc(dev, sizeof(*bt_bmc), GFP_KERNEL);
	if (!bt_bmc)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, bt_bmc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bt_bmc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(bt_bmc->base))
		return PTR_ERR(bt_bmc->base);

	mutex_init(&bt_bmc->mutex);
	init_waitqueue_head(&bt_bmc->queue);

	bt_bmc->miscdev.minor	= MISC_DYNAMIC_MINOR,
		bt_bmc->miscdev.name	= DEVICE_NAME,
		bt_bmc->miscdev.fops	= &bt_bmc_fops,
		bt_bmc->miscdev.parent = dev;
	rc = misc_register(&bt_bmc->miscdev);
	if (rc) {
		dev_err(dev, "Unable to register misc device\n");
		return rc;
	}

	bt_bmc_config_irq(bt_bmc, pdev);

	if (bt_bmc->irq) {
		dev_info(dev, "Using IRQ %d\n", bt_bmc->irq);
	} else {
		dev_info(dev, "No IRQ; using timer\n");
		setup_timer(&bt_bmc->poll_timer, poll_timer,
			    (unsigned long)bt_bmc);
		bt_bmc->poll_timer.expires = jiffies + msecs_to_jiffies(10);
		add_timer(&bt_bmc->poll_timer);
	}

	iowrite32((BT_IO_BASE << BT_CR0_IO_BASE) |
		  (BT_IRQ << BT_CR0_IRQ) |
		  BT_CR0_EN_CLR_SLV_RDP |
		  BT_CR0_EN_CLR_SLV_WRP |
		  BT_CR0_ENABLE_IBT,
		  bt_bmc->base + BT_CR0);

	clr_b_busy(bt_bmc);

	return 0;
}

static int bt_bmc_remove(struct platform_device *pdev)
{
	struct bt_bmc *bt_bmc = dev_get_drvdata(&pdev->dev);

	misc_deregister(&bt_bmc->miscdev);
	if (!bt_bmc->irq)
		del_timer_sync(&bt_bmc->poll_timer);
	return 0;
}

static const struct of_device_id bt_bmc_match[] = {
	{ .compatible = "aspeed,ast2400-bt-bmc" },
	{ },
};

static struct platform_driver bt_bmc_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = bt_bmc_match,
	},
	.probe = bt_bmc_probe,
	.remove = bt_bmc_remove,
};

module_platform_driver(bt_bmc_driver);

MODULE_DEVICE_TABLE(of, bt_bmc_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alistair Popple <alistair@popple.id.au>");
MODULE_DESCRIPTION("Linux device interface to the BT interface");
