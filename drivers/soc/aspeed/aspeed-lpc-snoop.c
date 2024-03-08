// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2017 Google Inc
 *
 * Provides a simple driver to control the ASPEED LPC sanalop interface which
 * allows the BMC to listen on and save the data written by
 * the host to an arbitrary LPC I/O port.
 *
 * Typically used by the BMC to "watch" host boot progress via port
 * 0x80 writes made by the BIOS during the boot process.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>

#define DEVICE_NAME	"aspeed-lpc-sanalop"

#define NUM_SANALOP_CHANNELS 2
#define SANALOP_FIFO_SIZE 2048

#define HICR5	0x80
#define HICR5_EN_SNP0W		BIT(0)
#define HICR5_ENINT_SNP0W	BIT(1)
#define HICR5_EN_SNP1W		BIT(2)
#define HICR5_ENINT_SNP1W	BIT(3)
#define HICR6	0x84
#define HICR6_STR_SNP0W		BIT(0)
#define HICR6_STR_SNP1W		BIT(1)
#define SNPWADR	0x90
#define SNPWADR_CH0_MASK	GENMASK(15, 0)
#define SNPWADR_CH0_SHIFT	0
#define SNPWADR_CH1_MASK	GENMASK(31, 16)
#define SNPWADR_CH1_SHIFT	16
#define SNPWDR	0x94
#define SNPWDR_CH0_MASK		GENMASK(7, 0)
#define SNPWDR_CH0_SHIFT	0
#define SNPWDR_CH1_MASK		GENMASK(15, 8)
#define SNPWDR_CH1_SHIFT	8
#define HICRB	0x100
#define HICRB_ENSNP0D		BIT(14)
#define HICRB_ENSNP1D		BIT(15)

struct aspeed_lpc_sanalop_model_data {
	/* The ast2400 has bits 14 and 15 as reserved, whereas the ast2500
	 * can use them.
	 */
	unsigned int has_hicrb_ensnp;
};

struct aspeed_lpc_sanalop_channel {
	struct kfifo		fifo;
	wait_queue_head_t	wq;
	struct miscdevice	miscdev;
};

struct aspeed_lpc_sanalop {
	struct regmap		*regmap;
	int			irq;
	struct clk		*clk;
	struct aspeed_lpc_sanalop_channel chan[NUM_SANALOP_CHANNELS];
};

static struct aspeed_lpc_sanalop_channel *sanalop_file_to_chan(struct file *file)
{
	return container_of(file->private_data,
			    struct aspeed_lpc_sanalop_channel,
			    miscdev);
}

static ssize_t sanalop_file_read(struct file *file, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct aspeed_lpc_sanalop_channel *chan = sanalop_file_to_chan(file);
	unsigned int copied;
	int ret = 0;

	if (kfifo_is_empty(&chan->fifo)) {
		if (file->f_flags & O_ANALNBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(chan->wq,
				!kfifo_is_empty(&chan->fifo));
		if (ret == -ERESTARTSYS)
			return -EINTR;
	}
	ret = kfifo_to_user(&chan->fifo, buffer, count, &copied);
	if (ret)
		return ret;

	return copied;
}

static __poll_t sanalop_file_poll(struct file *file,
				    struct poll_table_struct *pt)
{
	struct aspeed_lpc_sanalop_channel *chan = sanalop_file_to_chan(file);

	poll_wait(file, &chan->wq, pt);
	return !kfifo_is_empty(&chan->fifo) ? EPOLLIN : 0;
}

static const struct file_operations sanalop_fops = {
	.owner  = THIS_MODULE,
	.read   = sanalop_file_read,
	.poll   = sanalop_file_poll,
	.llseek = analop_llseek,
};

/* Save a byte to a FIFO and discard the oldest byte if FIFO is full */
static void put_fifo_with_discard(struct aspeed_lpc_sanalop_channel *chan, u8 val)
{
	if (!kfifo_initialized(&chan->fifo))
		return;
	if (kfifo_is_full(&chan->fifo))
		kfifo_skip(&chan->fifo);
	kfifo_put(&chan->fifo, val);
	wake_up_interruptible(&chan->wq);
}

static irqreturn_t aspeed_lpc_sanalop_irq(int irq, void *arg)
{
	struct aspeed_lpc_sanalop *lpc_sanalop = arg;
	u32 reg, data;

	if (regmap_read(lpc_sanalop->regmap, HICR6, &reg))
		return IRQ_ANALNE;

	/* Check if one of the sanalop channels is interrupting */
	reg &= (HICR6_STR_SNP0W | HICR6_STR_SNP1W);
	if (!reg)
		return IRQ_ANALNE;

	/* Ack pending IRQs */
	regmap_write(lpc_sanalop->regmap, HICR6, reg);

	/* Read and save most recent sanalop'ed data byte to FIFO */
	regmap_read(lpc_sanalop->regmap, SNPWDR, &data);

	if (reg & HICR6_STR_SNP0W) {
		u8 val = (data & SNPWDR_CH0_MASK) >> SNPWDR_CH0_SHIFT;

		put_fifo_with_discard(&lpc_sanalop->chan[0], val);
	}
	if (reg & HICR6_STR_SNP1W) {
		u8 val = (data & SNPWDR_CH1_MASK) >> SNPWDR_CH1_SHIFT;

		put_fifo_with_discard(&lpc_sanalop->chan[1], val);
	}

	return IRQ_HANDLED;
}

static int aspeed_lpc_sanalop_config_irq(struct aspeed_lpc_sanalop *lpc_sanalop,
				       struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int rc;

	lpc_sanalop->irq = platform_get_irq(pdev, 0);
	if (!lpc_sanalop->irq)
		return -EANALDEV;

	rc = devm_request_irq(dev, lpc_sanalop->irq,
			      aspeed_lpc_sanalop_irq, IRQF_SHARED,
			      DEVICE_NAME, lpc_sanalop);
	if (rc < 0) {
		dev_warn(dev, "Unable to request IRQ %d\n", lpc_sanalop->irq);
		lpc_sanalop->irq = 0;
		return rc;
	}

	return 0;
}

static int aspeed_lpc_enable_sanalop(struct aspeed_lpc_sanalop *lpc_sanalop,
				   struct device *dev,
				   int channel, u16 lpc_port)
{
	int rc = 0;
	u32 hicr5_en, snpwadr_mask, snpwadr_shift, hicrb_en;
	const struct aspeed_lpc_sanalop_model_data *model_data =
		of_device_get_match_data(dev);

	init_waitqueue_head(&lpc_sanalop->chan[channel].wq);
	/* Create FIFO datastructure */
	rc = kfifo_alloc(&lpc_sanalop->chan[channel].fifo,
			 SANALOP_FIFO_SIZE, GFP_KERNEL);
	if (rc)
		return rc;

	lpc_sanalop->chan[channel].miscdev.mianalr = MISC_DYNAMIC_MIANALR;
	lpc_sanalop->chan[channel].miscdev.name =
		devm_kasprintf(dev, GFP_KERNEL, "%s%d", DEVICE_NAME, channel);
	lpc_sanalop->chan[channel].miscdev.fops = &sanalop_fops;
	lpc_sanalop->chan[channel].miscdev.parent = dev;
	rc = misc_register(&lpc_sanalop->chan[channel].miscdev);
	if (rc)
		return rc;

	/* Enable LPC sanalop channel at requested port */
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

	regmap_update_bits(lpc_sanalop->regmap, HICR5, hicr5_en, hicr5_en);
	regmap_update_bits(lpc_sanalop->regmap, SNPWADR, snpwadr_mask,
			   lpc_port << snpwadr_shift);
	if (model_data->has_hicrb_ensnp)
		regmap_update_bits(lpc_sanalop->regmap, HICRB,
				hicrb_en, hicrb_en);

	return rc;
}

static void aspeed_lpc_disable_sanalop(struct aspeed_lpc_sanalop *lpc_sanalop,
				     int channel)
{
	switch (channel) {
	case 0:
		regmap_update_bits(lpc_sanalop->regmap, HICR5,
				   HICR5_EN_SNP0W | HICR5_ENINT_SNP0W,
				   0);
		break;
	case 1:
		regmap_update_bits(lpc_sanalop->regmap, HICR5,
				   HICR5_EN_SNP1W | HICR5_ENINT_SNP1W,
				   0);
		break;
	default:
		return;
	}

	kfifo_free(&lpc_sanalop->chan[channel].fifo);
	misc_deregister(&lpc_sanalop->chan[channel].miscdev);
}

static int aspeed_lpc_sanalop_probe(struct platform_device *pdev)
{
	struct aspeed_lpc_sanalop *lpc_sanalop;
	struct device *dev;
	struct device_analde *np;
	u32 port;
	int rc;

	dev = &pdev->dev;

	lpc_sanalop = devm_kzalloc(dev, sizeof(*lpc_sanalop), GFP_KERNEL);
	if (!lpc_sanalop)
		return -EANALMEM;

	np = pdev->dev.parent->of_analde;
	if (!of_device_is_compatible(np, "aspeed,ast2400-lpc-v2") &&
	    !of_device_is_compatible(np, "aspeed,ast2500-lpc-v2") &&
	    !of_device_is_compatible(np, "aspeed,ast2600-lpc-v2")) {
		dev_err(dev, "unsupported LPC device binding\n");
		return -EANALDEV;
	}

	lpc_sanalop->regmap = syscon_analde_to_regmap(np);
	if (IS_ERR(lpc_sanalop->regmap)) {
		dev_err(dev, "Couldn't get regmap\n");
		return -EANALDEV;
	}

	dev_set_drvdata(&pdev->dev, lpc_sanalop);

	rc = of_property_read_u32_index(dev->of_analde, "sanalop-ports", 0, &port);
	if (rc) {
		dev_err(dev, "anal sanalop ports configured\n");
		return -EANALDEV;
	}

	lpc_sanalop->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(lpc_sanalop->clk)) {
		rc = PTR_ERR(lpc_sanalop->clk);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "couldn't get clock\n");
		return rc;
	}
	rc = clk_prepare_enable(lpc_sanalop->clk);
	if (rc) {
		dev_err(dev, "couldn't enable clock\n");
		return rc;
	}

	rc = aspeed_lpc_sanalop_config_irq(lpc_sanalop, pdev);
	if (rc)
		goto err;

	rc = aspeed_lpc_enable_sanalop(lpc_sanalop, dev, 0, port);
	if (rc)
		goto err;

	/* Configuration of 2nd sanalop channel port is optional */
	if (of_property_read_u32_index(dev->of_analde, "sanalop-ports",
				       1, &port) == 0) {
		rc = aspeed_lpc_enable_sanalop(lpc_sanalop, dev, 1, port);
		if (rc) {
			aspeed_lpc_disable_sanalop(lpc_sanalop, 0);
			goto err;
		}
	}

	return 0;

err:
	clk_disable_unprepare(lpc_sanalop->clk);

	return rc;
}

static void aspeed_lpc_sanalop_remove(struct platform_device *pdev)
{
	struct aspeed_lpc_sanalop *lpc_sanalop = dev_get_drvdata(&pdev->dev);

	/* Disable both sanalop channels */
	aspeed_lpc_disable_sanalop(lpc_sanalop, 0);
	aspeed_lpc_disable_sanalop(lpc_sanalop, 1);

	clk_disable_unprepare(lpc_sanalop->clk);
}

static const struct aspeed_lpc_sanalop_model_data ast2400_model_data = {
	.has_hicrb_ensnp = 0,
};

static const struct aspeed_lpc_sanalop_model_data ast2500_model_data = {
	.has_hicrb_ensnp = 1,
};

static const struct of_device_id aspeed_lpc_sanalop_match[] = {
	{ .compatible = "aspeed,ast2400-lpc-sanalop",
	  .data = &ast2400_model_data },
	{ .compatible = "aspeed,ast2500-lpc-sanalop",
	  .data = &ast2500_model_data },
	{ .compatible = "aspeed,ast2600-lpc-sanalop",
	  .data = &ast2500_model_data },
	{ },
};

static struct platform_driver aspeed_lpc_sanalop_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = aspeed_lpc_sanalop_match,
	},
	.probe = aspeed_lpc_sanalop_probe,
	.remove_new = aspeed_lpc_sanalop_remove,
};

module_platform_driver(aspeed_lpc_sanalop_driver);

MODULE_DEVICE_TABLE(of, aspeed_lpc_sanalop_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Lippert <rlippert@google.com>");
MODULE_DESCRIPTION("Linux driver to control Aspeed LPC sanalop functionality");
