// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2014-2018 Nuvoton Technology corporation.

#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define DEVICE_NAME	"npcm7xx-pci-mbox"

#define NPCM7XX_MBOX_BMBXSTAT	0x0
#define NPCM7XX_MBOX_BMBXCTL	0x4
#define NPCM7XX_MBOX_BMBXCMD	0x8

#define NPCM7XX_MBOX_CIF_0	BIT(0)
#define NPCM7XX_MBOX_CIE_0	BIT(0)
#define NPCM7XX_MBOX_HIF_0	BIT(0)

#define NPCM7XX_MBOX_ALL_CIF	GENMASK(7, 0)
#define NPCM7XX_MBOX_ALL_CIE	GENMASK(7, 0)
#define NPCM7XX_MBOX_ALL_HIF	GENMASK(7, 0)

struct npcm7xx_mbox {
	struct miscdevice	miscdev;
	struct regmap		*regmap;
	void __iomem		*memory;
	wait_queue_head_t	queue;
	spinlock_t		lock;	/* mbox access mutex */
	bool			cif0;
	u32			max_buf_size;
};

static atomic_t npcm7xx_mbox_open_count = ATOMIC_INIT(0);

static struct npcm7xx_mbox *file_mbox(struct file *file)
{
	return container_of(file->private_data, struct npcm7xx_mbox, miscdev);
}

static int npcm7xx_mbox_open(struct inode *inode, struct file *file)
{
	struct npcm7xx_mbox *mbox = file_mbox(file);

	if (atomic_inc_return(&npcm7xx_mbox_open_count) == 1) {
		/* enable mailbox interrupt */
		regmap_update_bits(mbox->regmap, NPCM7XX_MBOX_BMBXCTL,
				   NPCM7XX_MBOX_ALL_CIE, NPCM7XX_MBOX_CIE_0);
		return 0;
	}

	atomic_dec(&npcm7xx_mbox_open_count);
	return -EBUSY;
}

static ssize_t npcm7xx_mbox_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct npcm7xx_mbox *mbox = file_mbox(file);
	unsigned long flags;

	if (!access_ok(buf, count))
		return -EFAULT;

	if ((*ppos + count) > mbox->max_buf_size)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK) {
		if (!mbox->cif0)
			return -EAGAIN;
	} else if (wait_event_interruptible(mbox->queue, mbox->cif0)) {
		return -ERESTARTSYS;
	}

	spin_lock_irqsave(&mbox->lock, flags);

	if (copy_to_user((void __user *)buf,
			 (const void *)(mbox->memory + *ppos), count)) {
		spin_unlock_irqrestore(&mbox->lock, flags);
		return -EFAULT;
	}

	mbox->cif0 = false;
	spin_unlock_irqrestore(&mbox->lock, flags);
	return count;
}

static ssize_t npcm7xx_mbox_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct npcm7xx_mbox *mbox = file_mbox(file);
	unsigned long flags;

	if (!access_ok(buf, count))
		return -EFAULT;

	if ((*ppos + count) > mbox->max_buf_size)
		return -EINVAL;

	spin_lock_irqsave(&mbox->lock, flags);

	if (copy_from_user((void *)(mbox->memory + *ppos),
			   (void __user *)buf, count)) {
		spin_unlock_irqrestore(&mbox->lock, flags);
		return -EFAULT;
	}

	regmap_update_bits(mbox->regmap, NPCM7XX_MBOX_BMBXCMD,
			   NPCM7XX_MBOX_ALL_HIF, NPCM7XX_MBOX_HIF_0);

	spin_unlock_irqrestore(&mbox->lock, flags);
	return count;
}

static unsigned int npcm7xx_mbox_poll(struct file *file, poll_table *wait)
{
	struct npcm7xx_mbox *mbox = file_mbox(file);
	unsigned int mask = 0;

	poll_wait(file, &mbox->queue, wait);
	if (mbox->cif0)
		mask |= POLLIN;

	return mask;
}

static int npcm7xx_mbox_release(struct inode *inode, struct file *file)
{
	atomic_dec(&npcm7xx_mbox_open_count);
	return 0;
}

static const struct file_operations npcm7xx_mbox_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_seek_end_llseek,
	.read		= npcm7xx_mbox_read,
	.write		= npcm7xx_mbox_write,
	.open		= npcm7xx_mbox_open,
	.release	= npcm7xx_mbox_release,
	.poll		= npcm7xx_mbox_poll,
};

static irqreturn_t npcm7xx_mbox_irq(int irq, void *arg)
{
	struct npcm7xx_mbox *mbox = arg;
	u32 val;

	regmap_read(mbox->regmap, NPCM7XX_MBOX_BMBXSTAT, &val);
	if ((val & NPCM7XX_MBOX_CIF_0) != NPCM7XX_MBOX_CIF_0)
		return IRQ_NONE;

	/*
	 * Leave the status bit set so that we know the data is for us,
	 * clear it once it has been read.
	 */
	mbox->cif0 = true;

	/* Mask it off, we'll clear it when we the data gets read */
	regmap_write_bits(mbox->regmap, NPCM7XX_MBOX_BMBXSTAT,
			  NPCM7XX_MBOX_ALL_CIF, NPCM7XX_MBOX_CIF_0);

	wake_up(&mbox->queue);

	return IRQ_HANDLED;
}

static int npcm7xx_mbox_config_irq(struct npcm7xx_mbox *mbox,
				   struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int rc, irq;
	u32 val;

	/* Disable all register based interrupts */
	regmap_update_bits(mbox->regmap, NPCM7XX_MBOX_BMBXCTL,
			   NPCM7XX_MBOX_ALL_CIE, 0);
/*
 * These registers are write one to clear. Clear them.
 * Per spec, cleared bits should not be re-cleared.
 * Need to read and clear needed bits only, instead of blindly clearing all.
 */
	regmap_read(mbox->regmap, NPCM7XX_MBOX_BMBXSTAT, &val);
	val &= NPCM7XX_MBOX_ALL_CIF;

	/* If any bit is set, write back to clear */
	if (val)
		regmap_write_bits(mbox->regmap, NPCM7XX_MBOX_BMBXSTAT,
				  NPCM7XX_MBOX_ALL_CIF, val);

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (!irq)
		return -ENODEV;

	rc = devm_request_irq(dev, irq, npcm7xx_mbox_irq, 0, DEVICE_NAME, mbox);
	if (rc < 0) {
		dev_err(dev, "Unable to request IRQ %d\n", irq);
		return rc;
	}

	return 0;
}

static int npcm7xx_mbox_probe(struct platform_device *pdev)
{
	struct npcm7xx_mbox *mbox;
	struct device *dev;
	struct resource *res;
	int rc;

	dev = &pdev->dev;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, mbox);

	mbox->regmap = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(mbox->regmap)) {
		dev_err(dev, "Couldn't get regmap\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	mbox->memory = devm_ioremap_resource(dev, res);
	if (IS_ERR(mbox->memory))
		return PTR_ERR(mbox->memory);
	mbox->max_buf_size = resource_size(res);

	spin_lock_init(&mbox->lock);
	init_waitqueue_head(&mbox->queue);

	mbox->miscdev.minor = MISC_DYNAMIC_MINOR;
	mbox->miscdev.name = DEVICE_NAME;
	mbox->miscdev.fops = &npcm7xx_mbox_fops;
	mbox->miscdev.parent = dev;
	mbox->cif0 = false;
	rc = misc_register(&mbox->miscdev);
	if (rc) {
		dev_err(dev, "Unable to register device\n");
		return rc;
	}

	rc = npcm7xx_mbox_config_irq(mbox, pdev);
	if (rc) {
		dev_err(dev, "Failed to configure IRQ\n");
		misc_deregister(&mbox->miscdev);
		return rc;
	}

	pr_info("NPCM7xx PCI Mailbox probed\n");

	return 0;
}

static int npcm7xx_mbox_remove(struct platform_device *pdev)
{
	struct npcm7xx_mbox *mbox = dev_get_drvdata(&pdev->dev);

	misc_deregister(&mbox->miscdev);

	return 0;
}

static const struct of_device_id npcm7xx_mbox_match[] = {
	{ .compatible = "nuvoton,npcm750-pci-mbox" },
	{ },
};

static struct platform_driver npcm7xx_mbox_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = npcm7xx_mbox_match,
	},
	.probe = npcm7xx_mbox_probe,
	.remove = npcm7xx_mbox_remove,
};

module_platform_driver(npcm7xx_mbox_driver);

MODULE_DEVICE_TABLE(of, npcm7xx_mbox_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tomer Maimon <tomer.maimon@nuvoton.com>");
MODULE_DESCRIPTION("NPCM7XX mailbox device driver");
