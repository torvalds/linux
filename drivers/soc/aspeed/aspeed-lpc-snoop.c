// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2017 Google Inc
 *
 * Provides a simple driver to control the ASPEED LPC syesop interface which
 * allows the BMC to listen on and save the data written by
 * the host to an arbitrary LPC I/O port.
 *
 * Typically used by the BMC to "watch" host boot progress via port
 * 0x80 writes made by the BIOS during the boot process.
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>

#define DEVICE_NAME	"aspeed-lpc-syesop"

#define NUM_SNOOP_CHANNELS 2
#define SNOOP_FIFO_SIZE 2048

#define HICR5	0x0
#define HICR5_EN_SNP0W		BIT(0)
#define HICR5_ENINT_SNP0W	BIT(1)
#define HICR5_EN_SNP1W		BIT(2)
#define HICR5_ENINT_SNP1W	BIT(3)

#define HICR6	0x4
#define HICR6_STR_SNP0W		BIT(0)
#define HICR6_STR_SNP1W		BIT(1)
#define SNPWADR	0x10
#define SNPWADR_CH0_MASK	GENMASK(15, 0)
#define SNPWADR_CH0_SHIFT	0
#define SNPWADR_CH1_MASK	GENMASK(31, 16)
#define SNPWADR_CH1_SHIFT	16
#define SNPWDR	0x14
#define SNPWDR_CH0_MASK		GENMASK(7, 0)
#define SNPWDR_CH0_SHIFT	0
#define SNPWDR_CH1_MASK		GENMASK(15, 8)
#define SNPWDR_CH1_SHIFT	8
#define HICRB	0x80
#define HICRB_ENSNP0D		BIT(14)
#define HICRB_ENSNP1D		BIT(15)

struct aspeed_lpc_syesop_model_data {
	/* The ast2400 has bits 14 and 15 as reserved, whereas the ast2500
	 * can use them.
	 */
	unsigned int has_hicrb_ensnp;
};

struct aspeed_lpc_syesop_channel {
	struct kfifo		fifo;
	wait_queue_head_t	wq;
	struct miscdevice	miscdev;
};

struct aspeed_lpc_syesop {
	struct regmap		*regmap;
	int			irq;
	struct aspeed_lpc_syesop_channel chan[NUM_SNOOP_CHANNELS];
};

static struct aspeed_lpc_syesop_channel *syesop_file_to_chan(struct file *file)
{
	return container_of(file->private_data,
			    struct aspeed_lpc_syesop_channel,
			    miscdev);
}

static ssize_t syesop_file_read(struct file *file, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct aspeed_lpc_syesop_channel *chan = syesop_file_to_chan(file);
	unsigned int copied;
	int ret = 0;

	if (kfifo_is_empty(&chan->fifo)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(chan->wq,
				!kfifo_is_empty(&chan->fifo));
		if (ret == -ERESTARTSYS)
			return -EINTR;
	}
	ret = kfifo_to_user(&chan->fifo, buffer, count, &copied);

	return ret ? ret : copied;
}

static __poll_t syesop_file_poll(struct file *file,
				    struct poll_table_struct *pt)
{
	struct aspeed_lpc_syesop_channel *chan = syesop_file_to_chan(file);

	poll_wait(file, &chan->wq, pt);
	return !kfifo_is_empty(&chan->fifo) ? EPOLLIN : 0;
}

static const struct file_operations syesop_fops = {
	.owner  = THIS_MODULE,
	.read   = syesop_file_read,
	.poll   = syesop_file_poll,
	.llseek = yesop_llseek,
};

/* Save a byte to a FIFO and discard the oldest byte if FIFO is full */
static void put_fifo_with_discard(struct aspeed_lpc_syesop_channel *chan, u8 val)
{
	if (!kfifo_initialized(&chan->fifo))
		return;
	if (kfifo_is_full(&chan->fifo))
		kfifo_skip(&chan->fifo);
	kfifo_put(&chan->fifo, val);
	wake_up_interruptible(&chan->wq);
}

static irqreturn_t aspeed_lpc_syesop_irq(int irq, void *arg)
{
	struct aspeed_lpc_syesop *lpc_syesop = arg;
	u32 reg, data;

	if (regmap_read(lpc_syesop->regmap, HICR6, &reg))
		return IRQ_NONE;

	/* Check if one of the syesop channels is interrupting */
	reg &= (HICR6_STR_SNP0W | HICR6_STR_SNP1W);
	if (!reg)
		return IRQ_NONE;

	/* Ack pending IRQs */
	regmap_write(lpc_syesop->regmap, HICR6, reg);

	/* Read and save most recent syesop'ed data byte to FIFO */
	regmap_read(lpc_syesop->regmap, SNPWDR, &data);

	if (reg & HICR6_STR_SNP0W) {
		u8 val = (data & SNPWDR_CH0_MASK) >> SNPWDR_CH0_SHIFT;

		put_fifo_with_discard(&lpc_syesop->chan[0], val);
	}
	if (reg & HICR6_STR_SNP1W) {
		u8 val = (data & SNPWDR_CH1_MASK) >> SNPWDR_CH1_SHIFT;

		put_fifo_with_discard(&lpc_syesop->chan[1], val);
	}

	return IRQ_HANDLED;
}

static int aspeed_lpc_syesop_config_irq(struct aspeed_lpc_syesop *lpc_syesop,
				       struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int rc;

	lpc_syesop->irq = platform_get_irq(pdev, 0);
	if (!lpc_syesop->irq)
		return -ENODEV;

	rc = devm_request_irq(dev, lpc_syesop->irq,
			      aspeed_lpc_syesop_irq, IRQF_SHARED,
			      DEVICE_NAME, lpc_syesop);
	if (rc < 0) {
		dev_warn(dev, "Unable to request IRQ %d\n", lpc_syesop->irq);
		lpc_syesop->irq = 0;
		return rc;
	}

	return 0;
}

static int aspeed_lpc_enable_syesop(struct aspeed_lpc_syesop *lpc_syesop,
				   struct device *dev,
				   int channel, u16 lpc_port)
{
	int rc = 0;
	u32 hicr5_en, snpwadr_mask, snpwadr_shift, hicrb_en;
	const struct aspeed_lpc_syesop_model_data *model_data =
		of_device_get_match_data(dev);

	init_waitqueue_head(&lpc_syesop->chan[channel].wq);
	/* Create FIFO datastructure */
	rc = kfifo_alloc(&lpc_syesop->chan[channel].fifo,
			 SNOOP_FIFO_SIZE, GFP_KERNEL);
	if (rc)
		return rc;

	lpc_syesop->chan[channel].miscdev.miyesr = MISC_DYNAMIC_MINOR;
	lpc_syesop->chan[channel].miscdev.name =
		devm_kasprintf(dev, GFP_KERNEL, "%s%d", DEVICE_NAME, channel);
	lpc_syesop->chan[channel].miscdev.fops = &syesop_fops;
	lpc_syesop->chan[channel].miscdev.parent = dev;
	rc = misc_register(&lpc_syesop->chan[channel].miscdev);
	if (rc)
		return rc;

	/* Enable LPC syesop channel at requested port */
	switch (channel) {
	case 0:
		hicr5_en = HICR5_EN_SNP0W | HICR5_ENINT_SNP0W;
		snpwadr_mask = SNPWADR_CH0_MASK;
		snpwadr_shift = SNPWADR_CH0_SHIFT;
		hicrb_en = HICRB_ENSNP0D;
		break;
	case 1:
		hicr5_en = HICR5_EN_SNP1W | HICR5_ENINT_SNP1W;
		snpwadr_mask = SNPWADR_CH1_MASK;
		snpwadr_shift = SNPWADR_CH1_SHIFT;
		hicrb_en = HICRB_ENSNP1D;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(lpc_syesop->regmap, HICR5, hicr5_en, hicr5_en);
	regmap_update_bits(lpc_syesop->regmap, SNPWADR, snpwadr_mask,
			   lpc_port << snpwadr_shift);
	if (model_data->has_hicrb_ensnp)
		regmap_update_bits(lpc_syesop->regmap, HICRB,
				hicrb_en, hicrb_en);

	return rc;
}

static void aspeed_lpc_disable_syesop(struct aspeed_lpc_syesop *lpc_syesop,
				     int channel)
{
	switch (channel) {
	case 0:
		regmap_update_bits(lpc_syesop->regmap, HICR5,
				   HICR5_EN_SNP0W | HICR5_ENINT_SNP0W,
				   0);
		break;
	case 1:
		regmap_update_bits(lpc_syesop->regmap, HICR5,
				   HICR5_EN_SNP1W | HICR5_ENINT_SNP1W,
				   0);
		break;
	default:
		return;
	}

	kfifo_free(&lpc_syesop->chan[channel].fifo);
	misc_deregister(&lpc_syesop->chan[channel].miscdev);
}

static int aspeed_lpc_syesop_probe(struct platform_device *pdev)
{
	struct aspeed_lpc_syesop *lpc_syesop;
	struct device *dev;
	u32 port;
	int rc;

	dev = &pdev->dev;

	lpc_syesop = devm_kzalloc(dev, sizeof(*lpc_syesop), GFP_KERNEL);
	if (!lpc_syesop)
		return -ENOMEM;

	lpc_syesop->regmap = syscon_yesde_to_regmap(
			pdev->dev.parent->of_yesde);
	if (IS_ERR(lpc_syesop->regmap)) {
		dev_err(dev, "Couldn't get regmap\n");
		return -ENODEV;
	}

	dev_set_drvdata(&pdev->dev, lpc_syesop);

	rc = of_property_read_u32_index(dev->of_yesde, "syesop-ports", 0, &port);
	if (rc) {
		dev_err(dev, "yes syesop ports configured\n");
		return -ENODEV;
	}

	rc = aspeed_lpc_syesop_config_irq(lpc_syesop, pdev);
	if (rc)
		return rc;

	rc = aspeed_lpc_enable_syesop(lpc_syesop, dev, 0, port);
	if (rc)
		return rc;

	/* Configuration of 2nd syesop channel port is optional */
	if (of_property_read_u32_index(dev->of_yesde, "syesop-ports",
				       1, &port) == 0) {
		rc = aspeed_lpc_enable_syesop(lpc_syesop, dev, 1, port);
		if (rc)
			aspeed_lpc_disable_syesop(lpc_syesop, 0);
	}

	return rc;
}

static int aspeed_lpc_syesop_remove(struct platform_device *pdev)
{
	struct aspeed_lpc_syesop *lpc_syesop = dev_get_drvdata(&pdev->dev);

	/* Disable both syesop channels */
	aspeed_lpc_disable_syesop(lpc_syesop, 0);
	aspeed_lpc_disable_syesop(lpc_syesop, 1);

	return 0;
}

static const struct aspeed_lpc_syesop_model_data ast2400_model_data = {
	.has_hicrb_ensnp = 0,
};

static const struct aspeed_lpc_syesop_model_data ast2500_model_data = {
	.has_hicrb_ensnp = 1,
};

static const struct of_device_id aspeed_lpc_syesop_match[] = {
	{ .compatible = "aspeed,ast2400-lpc-syesop",
	  .data = &ast2400_model_data },
	{ .compatible = "aspeed,ast2500-lpc-syesop",
	  .data = &ast2500_model_data },
	{ },
};

static struct platform_driver aspeed_lpc_syesop_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = aspeed_lpc_syesop_match,
	},
	.probe = aspeed_lpc_syesop_probe,
	.remove = aspeed_lpc_syesop_remove,
};

module_platform_driver(aspeed_lpc_syesop_driver);

MODULE_DEVICE_TABLE(of, aspeed_lpc_syesop_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Lippert <rlippert@google.com>");
MODULE_DESCRIPTION("Linux driver to control Aspeed LPC syesop functionality");
