// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2017 Google Inc
 * Copyright 2023 Aspeed Technology Inc
 */

#include <linux/bitops.h>
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
//#include <linux/idr.h>

#define DEVICE_NAME	"aspeed-lpc-snoop"

static DEFINE_IDA(aspeed_lpc_snoop_ida);

#define SNOOP_HW_CHANNEL_NUM	2
#define SNOOP_FIFO_SIZE			2048

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

struct aspeed_lpc_snoop_model_data {
	/*
	 * The ast2400 has bits 14 and 15 as reserved, whereas the ast2500 and later
	 * can use them. These two bits for are for eSPI interface to respond ACCEPT
	 * when snooping Host port I/O write.
	 */
	unsigned int has_hicrb_ensnp;
};

struct aspeed_lpc_snoop_channel {
	int id;
	struct miscdevice mdev;
	wait_queue_head_t wq;
	struct kfifo fifo;
};

struct aspeed_lpc_snoop {
	struct aspeed_lpc_snoop_channel chan[SNOOP_HW_CHANNEL_NUM];
	struct regmap *regmap;
	int irq;
};

static struct aspeed_lpc_snoop_channel *snoop_file_to_chan(struct file *file)
{
	return container_of(file->private_data,
			    struct aspeed_lpc_snoop_channel,
			    mdev);
}

static ssize_t snoop_file_read(struct file *file, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct aspeed_lpc_snoop_channel *chan = snoop_file_to_chan(file);
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
	if (ret)
		return ret;

	return copied;
}

static __poll_t snoop_file_poll(struct file *file,
				struct poll_table_struct *pt)
{
	struct aspeed_lpc_snoop_channel *chan = snoop_file_to_chan(file);

	poll_wait(file, &chan->wq, pt);

	return !kfifo_is_empty(&chan->fifo) ? EPOLLIN : 0;
}

static const struct file_operations snoop_fops = {
	.owner  = THIS_MODULE,
	.read   = snoop_file_read,
	.poll   = snoop_file_poll,
	.llseek = noop_llseek,
};

/* Save a byte to a FIFO and discard the oldest byte if FIFO is full */
static void put_fifo_with_discard(struct aspeed_lpc_snoop_channel *chan, u8 val)
{
	if (!kfifo_initialized(&chan->fifo))
		return;

	if (kfifo_is_full(&chan->fifo))
		kfifo_skip(&chan->fifo);

	kfifo_put(&chan->fifo, val);

	wake_up_interruptible(&chan->wq);
}

static irqreturn_t aspeed_lpc_snoop_irq(int irq, void *arg)
{
	struct aspeed_lpc_snoop *snoop = arg;
	u32 reg, data;
	u8 val;

	if (regmap_read(snoop->regmap, HICR6, &reg))
		return IRQ_NONE;

	/* Check if one of the snoop channels is interrupting */
	reg &= (HICR6_STR_SNP0W | HICR6_STR_SNP1W);
	if (!reg)
		return IRQ_NONE;

	/* Ack pending IRQs */
	regmap_write(snoop->regmap, HICR6, reg);

	/* Read and save most recent snoop'ed data byte to FIFO */
	regmap_read(snoop->regmap, SNPWDR, &data);

	if (reg & HICR6_STR_SNP0W) {
		val = (data & SNPWDR_CH0_MASK) >> SNPWDR_CH0_SHIFT;
		put_fifo_with_discard(&snoop->chan[0], val);
	}

	if (reg & HICR6_STR_SNP1W) {
		val = (data & SNPWDR_CH1_MASK) >> SNPWDR_CH1_SHIFT;
		put_fifo_with_discard(&snoop->chan[1], val);
	}

	return IRQ_HANDLED;
}

static int aspeed_lpc_enable_snoop(struct aspeed_lpc_snoop *snoop,
				   struct device *dev, int hw_channel, u16 port)
{
	const struct aspeed_lpc_snoop_model_data *model_data;
	u32 hicr5_en, snpwadr_mask, snpwadr_shift, hicrb_en;
	struct aspeed_lpc_snoop_channel *chan;
	int rc = 0;

	model_data = of_device_get_match_data(dev);

	chan = &snoop->chan[hw_channel];

	init_waitqueue_head(&chan->wq);

	/* Create FIFO datastructure */
	rc = kfifo_alloc(&chan->fifo, SNOOP_FIFO_SIZE, GFP_KERNEL);
	if (rc)
		return rc;

	chan->id = ida_alloc(&aspeed_lpc_snoop_ida, GFP_KERNEL);
	if (chan->id < 0)
		return chan->id;

	chan->mdev.parent = dev;
	chan->mdev.minor = MISC_DYNAMIC_MINOR;
	chan->mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s%d",
					 DEVICE_NAME, chan->id);
	chan->mdev.fops = &snoop_fops;
	rc = misc_register(&chan->mdev);
	if (rc)
		return rc;

	/* Enable LPC snoop channel at requested port */
	switch (hw_channel) {
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

	regmap_update_bits(snoop->regmap, HICR5, hicr5_en, hicr5_en);
	regmap_update_bits(snoop->regmap, SNPWADR, snpwadr_mask, port << snpwadr_shift);

	if (model_data->has_hicrb_ensnp)
		regmap_update_bits(snoop->regmap, HICRB, hicrb_en, hicrb_en);

	return rc;
}

static void aspeed_lpc_disable_snoop(struct aspeed_lpc_snoop *snoop,
				     int hw_channel)
{
	switch (hw_channel) {
	case 0:
		regmap_update_bits(snoop->regmap, HICR5,
				   HICR5_EN_SNP0W | HICR5_ENINT_SNP0W,
				   0);
		break;
	case 1:
		regmap_update_bits(snoop->regmap, HICR5,
				   HICR5_EN_SNP1W | HICR5_ENINT_SNP1W,
				   0);
		break;
	default:
		return;
	}

	kfifo_free(&snoop->chan[hw_channel].fifo);
	misc_deregister(&snoop->chan[hw_channel].mdev);
}

static int aspeed_lpc_snoop_probe(struct platform_device *pdev)
{
	u32 ports[SNOOP_HW_CHANNEL_NUM], port_num;
	struct aspeed_lpc_snoop *snoop;
	struct device *dev;
	int i, rc;

	dev = &pdev->dev;

	snoop = devm_kzalloc(dev, sizeof(*snoop), GFP_KERNEL);
	if (!snoop)
		return -ENOMEM;

	snoop->regmap = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(snoop->regmap)) {
		dev_err(dev, "Couldn't get regmap\n");
		return -ENODEV;
	}

	dev_set_drvdata(&pdev->dev, snoop);

	snoop->irq = platform_get_irq(pdev, 0);
	if (snoop->irq < 0) {
		dev_err(dev, "cannot get IRQ\n");
		return snoop->irq;
	}

	rc = devm_request_irq(dev, snoop->irq, aspeed_lpc_snoop_irq,
			      IRQF_SHARED, DEVICE_NAME, snoop);
	if (rc < 0) {
		dev_err(dev, "cannot request IRQ %d\n", snoop->irq);
		return rc;
	}

	port_num = of_property_read_variable_u32_array(dev->of_node,
						       "snoop-ports",
						       ports, 1, SNOOP_HW_CHANNEL_NUM);
	if (port_num < 0) {
		dev_err(dev, "no snoop ports configured\n");
		return -ENODEV;
	}

	for (i = 0; i < port_num; ++i) {
		rc = aspeed_lpc_enable_snoop(snoop, dev, i, ports[i]);
		if (rc)
			goto err;

		dev_info(dev, "Initialised channel %d to snoop IO address 0x%x\n",
			 snoop->chan[i].id, ports[i]);
	}

	return 0;

err:
	return rc;
}

static void aspeed_lpc_snoop_remove(struct platform_device *pdev)
{
	struct aspeed_lpc_snoop *snoop = dev_get_drvdata(&pdev->dev);
	int i;

	for (i = 0; i < SNOOP_HW_CHANNEL_NUM; ++i)
		aspeed_lpc_disable_snoop(snoop, i);
}

static const struct aspeed_lpc_snoop_model_data ast2400_model_data = {
	.has_hicrb_ensnp = 0,
};

static const struct aspeed_lpc_snoop_model_data ast2500_model_data = {
	.has_hicrb_ensnp = 1,
};

static const struct of_device_id aspeed_lpc_snoop_match[] = {
	{ .compatible = "aspeed,ast2400-lpc-snoop",
	  .data = &ast2400_model_data },
	{ .compatible = "aspeed,ast2500-lpc-snoop",
	  .data = &ast2500_model_data },
	{ .compatible = "aspeed,ast2600-lpc-snoop",
	  .data = &ast2500_model_data },
	{ .compatible = "aspeed,ast2700-lpc-snoop",
	  .data = &ast2500_model_data },
	{ },
};

static struct platform_driver aspeed_lpc_snoop_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = aspeed_lpc_snoop_match,
	},
	.probe = aspeed_lpc_snoop_probe,
	.remove_new = aspeed_lpc_snoop_remove,
};

module_platform_driver(aspeed_lpc_snoop_driver);

MODULE_DEVICE_TABLE(of, aspeed_lpc_snoop_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Lippert <rlippert@google.com>");
MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_DESCRIPTION("Linux driver to control Aspeed LPC snoop functionality");
