// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
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
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/dma-mapping.h>
#include <linux/sizes.h>

#define DEVICE_NAME "aspeed-lpc-pcc"

static DEFINE_IDA(aspeed_pcc_ida);

#define PCCR6	0x0c4
#define   PCCR6_DMA_CUR_ADDR		GENMASK(27, 0)
#define PCCR4	0x0d0
#define   PCCR4_DMA_ADDRL_MASK		GENMASK(31, 0)
#define   PCCR4_DMA_ADDRL_SHIFT		0
#define PCCR5	0x0d4
#define   PCCR5_DMA_ADDRH_MASK		GENMASK(27, 24)
#define   PCCR5_DMA_ADDRH_SHIFT		24
#define   PCCR5_DMA_LEN_MASK		GENMASK(23, 0)
#define   PCCR5_DMA_LEN_SHIFT		0
#define PCCR0	0x130
#define   PCCR0_EN_DMA_INT		BIT(31)
#define   PCCR0_EN_DMA_MODE		BIT(14)
#define   PCCR0_ADDR_SEL_MASK		GENMASK(13, 12)
#define   PCCR0_ADDR_SEL_SHIFT		12
#define   PCCR0_RX_TRIG_LVL_MASK	GENMASK(10, 8)
#define   PCCR0_RX_TRIG_LVL_SHIFT	8
#define   PCCR0_CLR_RX_FIFO		BIT(7)
#define   PCCR0_MODE_SEL_MASK		GENMASK(5, 4)
#define   PCCR0_MODE_SEL_SHIFT		4
#define   PCCR0_EN_RX_TMOUT_INT		BIT(2)
#define   PCCR0_EN_RX_AVAIL_INT		BIT(1)
#define   PCCR0_EN			BIT(0)
#define PCCR1	0x134
#define   PCCR1_BASE_ADDR_MASK		GENMASK(15, 0)
#define   PCCR1_BASE_ADDR_SHIFT		0
#define   PCCR1_DONT_CARE_BITS_MASK	GENMASK(21, 16)
#define   PCCR1_DONT_CARE_BITS_SHIFT	16
#define PCCR2	0x138
#define   PCCR2_DMA_DONE		BIT(4)
#define   PCCR2_DATA_RDY		PCCR2_DMA_DONE
#define   PCCR2_RX_TMOUT_INT		BIT(2)
#define   PCCR2_RX_AVAIL_INT		BIT(1)
#define PCCR3	0x13c
#define   PCCR3_FIFO_DATA_MASK		GENMASK(7, 0)

#define PCC_DMA_BUFSZ	(256 * SZ_1K)

enum pcc_fifo_threshold {
	PCC_FIFO_THR_1_BYTE,
	PCC_FIFO_THR_1_EIGHTH,
	PCC_FIFO_THR_2_EIGHTH,
	PCC_FIFO_THR_3_EIGHTH,
	PCC_FIFO_THR_4_EIGHTH,
	PCC_FIFO_THR_5_EIGHTH,
	PCC_FIFO_THR_6_EIGHTH,
	PCC_FIFO_THR_7_EIGHTH,
	PCC_FIFO_THR_8_EIGHTH,
};

enum pcc_record_mode {
	PCC_REC_1B,
	PCC_REC_2B,
	PCC_REC_4B,
	PCC_REC_FULL,
};

enum pcc_port_hbits_select {
	PCC_PORT_HBITS_SEL_NONE,
	PCC_PORT_HBITS_SEL_45,
	PCC_PORT_HBITS_SEL_67,
	PCC_PORT_HBITS_SEL_89,
};

struct aspeed_pcc_dma {
	uint32_t rptr;
	uint8_t *virt;
	dma_addr_t addr;
	uint32_t size;
};

struct aspeed_pcc {
	struct device *dev;
	struct regmap *regmap;
	int irq;
	uint32_t rec_mode;
	uint32_t port;
	uint32_t port_xbits;
	uint32_t port_hbits_select;
	uint32_t dma_mode;
	struct aspeed_pcc_dma dma;
	struct kfifo fifo;
	wait_queue_head_t wq;
	struct miscdevice mdev;
	int mdev_id;
	bool a2600_15;
};

static inline bool is_valid_rec_mode(uint32_t mode)
{
	return (mode > PCC_REC_FULL) ? false : true;
}

static inline bool is_valid_high_bits_select(uint32_t sel)
{
	return (sel > PCC_PORT_HBITS_SEL_89) ? false : true;
}

static ssize_t aspeed_pcc_file_read(struct file *file, char __user *buffer,
		size_t count, loff_t *ppos)
{
	int rc;
	unsigned int copied;
	struct aspeed_pcc *pcc = container_of(file->private_data,
					      struct aspeed_pcc,
					      mdev);

	if (kfifo_is_empty(&pcc->fifo)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		rc = wait_event_interruptible(pcc->wq,
					      !kfifo_is_empty(&pcc->fifo));
		if (rc == -ERESTARTSYS)
			return -EINTR;
	}

	rc = kfifo_to_user(&pcc->fifo, buffer, count, &copied);

	return rc ? rc : copied;
}

static __poll_t aspeed_pcc_file_poll(struct file *file,
		struct poll_table_struct *pt)
{
	struct aspeed_pcc *pcc = container_of(
			file->private_data,
			struct aspeed_pcc,
			mdev);

	poll_wait(file, &pcc->wq, pt);

	return !kfifo_is_empty(&pcc->fifo) ? POLLIN : 0;
}

static const struct file_operations pcc_fops = {
	.owner = THIS_MODULE,
	.read = aspeed_pcc_file_read,
	.poll = aspeed_pcc_file_poll,
};

static irqreturn_t aspeed_pcc_dma_isr(int irq, void *arg)
{
	uint32_t reg, rptr, wptr;
	struct aspeed_pcc *pcc = (struct aspeed_pcc*)arg;
	struct kfifo *fifo = &pcc->fifo;

	regmap_read(pcc->regmap, PCCR6, &reg);

	regmap_write_bits(pcc->regmap, PCCR2, PCCR2_DMA_DONE, PCCR2_DMA_DONE);

	wptr = (reg & PCCR6_DMA_CUR_ADDR) - (pcc->dma.addr & PCCR6_DMA_CUR_ADDR);
	rptr = pcc->dma.rptr;

	do {
		if (kfifo_is_full(fifo))
			kfifo_skip(fifo);

		kfifo_put(fifo, pcc->dma.virt[rptr]);

		rptr = (rptr + 1) % pcc->dma.size;
	} while (rptr != wptr);

	pcc->dma.rptr = rptr;

	wake_up_interruptible(&pcc->wq);

	return IRQ_HANDLED;
}

static irqreturn_t aspeed_pcc_isr(int irq, void *arg)
{
	uint32_t sts, reg;
	struct aspeed_pcc *pcc = (struct aspeed_pcc*)arg;
	struct kfifo *fifo = &pcc->fifo;

	regmap_read(pcc->regmap, PCCR2, &sts);

	if (!(sts & (PCCR2_RX_TMOUT_INT | PCCR2_RX_AVAIL_INT | PCCR2_DMA_DONE)))
		return IRQ_NONE;

	if (pcc->dma_mode)
		return aspeed_pcc_dma_isr(irq, arg);

	while (sts & PCCR2_DATA_RDY) {
		regmap_read(pcc->regmap, PCCR3, &reg);

		if (kfifo_is_full(fifo))
			kfifo_skip(fifo);

		kfifo_put(fifo, reg & PCCR3_FIFO_DATA_MASK);

		regmap_read(pcc->regmap, PCCR2, &sts);
	}

	wake_up_interruptible(&pcc->wq);

	return IRQ_HANDLED;
}

/*
 * A2600-15 AP note
 *
 * SW workaround to prevent generating Non-Fatal-Error (NFE)
 * eSPI response when PCC is used for port I/O byte snooping
 * over eSPI.
 */
#define SNPWADR	0x90
#define HICR6	0x84
#define HICRB	0x100
static int aspeed_a2600_15(struct aspeed_pcc *pcc, struct device *dev)
{
	struct device_node *np;

	/* abort if snoop is enabled */
	np = of_find_compatible_node(NULL, NULL, "aspeed,ast2600-lpc-snoop");
	if (np) {
		if (of_device_is_available(np)) {
			dev_err(dev, "A2600-15 should be applied with snoop disabled\n");
			return -EPERM;
		}
	}

	/* abort if port is not 4-bytes continuous range */
	if (pcc->port_xbits != 0x3) {
		dev_err(dev, "A2600-15 should be applied on 4-bytes continuous I/O address range\n");
		return -EINVAL;
	}

	/* set SNPWADR of snoop device */
	regmap_write(pcc->regmap, SNPWADR, pcc->port | ((pcc->port + 2) << 16));

	/* set HICRB[15:14]=11b to enable ACCEPT response for SNPWADR */
	regmap_update_bits(pcc->regmap, HICRB, BIT(14) | BIT(15),
			   BIT(14) | BIT(15));

	/* set HICR6[19] to extend SNPWADR to 2x range */
	regmap_update_bits(pcc->regmap, HICR6, BIT(19), BIT(19));

	return 0;
}

static int aspeed_pcc_enable(struct aspeed_pcc *pcc, struct device *dev)
{
	int rc;

	if (pcc->a2600_15) {
		rc = aspeed_a2600_15(pcc, dev);
		if (rc)
			return rc;
	}

	/* record mode */
	regmap_update_bits(pcc->regmap, PCCR0,
			   PCCR0_MODE_SEL_MASK,
			   pcc->rec_mode << PCCR0_MODE_SEL_SHIFT);

	/* port address */
	regmap_update_bits(pcc->regmap, PCCR1,
			   PCCR1_BASE_ADDR_MASK,
			   pcc->port << PCCR1_BASE_ADDR_SHIFT);

	/* port address high bits selection or parser control */
	regmap_update_bits(pcc->regmap, PCCR0,
			   PCCR0_ADDR_SEL_MASK,
			   pcc->port_hbits_select << PCCR0_ADDR_SEL_SHIFT);

	/* port address dont care bits */
	regmap_update_bits(pcc->regmap, PCCR1,
			   PCCR1_DONT_CARE_BITS_MASK,
			   pcc->port_xbits << PCCR1_DONT_CARE_BITS_SHIFT);

	/* set DMA ring buffer size and enable interrupts */
	if (pcc->dma_mode) {
		regmap_write(pcc->regmap, PCCR4, pcc->dma.addr & 0xffffffff);
		regmap_update_bits(pcc->regmap, PCCR5, PCCR5_DMA_ADDRH_MASK,
				   (pcc->dma.addr >> 32) << PCCR5_DMA_ADDRH_SHIFT);
		regmap_update_bits(pcc->regmap, PCCR5, PCCR5_DMA_LEN_MASK,
				   (pcc->dma.size / 4) << PCCR5_DMA_LEN_SHIFT);
		regmap_update_bits(pcc->regmap, PCCR0,
				   PCCR0_EN_DMA_INT | PCCR0_EN_DMA_MODE,
				   PCCR0_EN_DMA_INT | PCCR0_EN_DMA_MODE);
	} else {
		regmap_update_bits(pcc->regmap, PCCR0, PCCR0_RX_TRIG_LVL_MASK,
				   PCC_FIFO_THR_4_EIGHTH << PCCR0_RX_TRIG_LVL_SHIFT);
		regmap_update_bits(pcc->regmap, PCCR0,
				   PCCR0_EN_RX_TMOUT_INT | PCCR0_EN_RX_AVAIL_INT,
				   PCCR0_EN_RX_TMOUT_INT | PCCR0_EN_RX_AVAIL_INT);
	}

	regmap_update_bits(pcc->regmap, PCCR0, PCCR0_EN, PCCR0_EN);

	return 0;
}

static int aspeed_pcc_probe(struct platform_device *pdev)
{
	int rc;
	struct aspeed_pcc *pcc;
	struct device *dev = &pdev->dev;
	uint32_t fifo_size = PAGE_SIZE;

	pcc = devm_kzalloc(&pdev->dev, sizeof(*pcc), GFP_KERNEL);
	if (!pcc)
		return -ENOMEM;

	pcc->dev = dev;

	rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_err(dev, "cannot set 64-bits DMA mask\n");
		return rc;
	}

	pcc->regmap = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(pcc->regmap)) {
		dev_err(dev, "cannot map register\n");
		return -ENODEV;
	}

	/* disable PCC for safety */
	regmap_update_bits(pcc->regmap, PCCR0, PCCR0_EN, 0);

	rc = of_property_read_u32(dev->of_node, "port-addr", &pcc->port);
	if (rc) {
		dev_err(dev, "cannot get port address\n");
		return -ENODEV;
	}

	/* optional, by default: 0 -> 1-Byte mode */
	of_property_read_u32(dev->of_node, "rec-mode", &pcc->rec_mode);
	if (!is_valid_rec_mode(pcc->rec_mode)) {
		dev_err(dev, "invalid record mode: %u\n",
				pcc->rec_mode);
		return -EINVAL;
	}

	/* optional, by default: 0 -> no don't care bits */
	of_property_read_u32(dev->of_node, "port-addr-xbits", &pcc->port_xbits);

	/*
	 * optional, by default: 0 -> no high address bits
	 *
	 * Note that when record mode is set to 1-Byte, this
	 * property is ignored and the corresponding HW bits
	 * behave as read/write cycle parser control with the
	 * value set to 0b11
	 */
	if (pcc->rec_mode) {
		of_property_read_u32(dev->of_node, "port-addr-hbits-select",
				     &pcc->port_hbits_select);
		if (!is_valid_high_bits_select(pcc->port_hbits_select)) {
			dev_err(dev, "invalid high address bits selection: %u\n",
				pcc->port_hbits_select);
			return -EINVAL;
		}
	}
	else
		pcc->port_hbits_select = 0x3;

	pcc->dma_mode = of_property_read_bool(dev->of_node, "dma-mode");
	if (pcc->dma_mode) {
		pcc->dma.size = PCC_DMA_BUFSZ;
		pcc->dma.virt = dmam_alloc_coherent(dev,
						    pcc->dma.size,
						    &pcc->dma.addr,
						    GFP_KERNEL);
		if (!pcc->dma.virt) {
			dev_err(dev, "cannot allocate DMA buffer\n");
			return -ENOMEM;
		}

		fifo_size = roundup(pcc->dma.size, PAGE_SIZE);
	}

	rc = kfifo_alloc(&pcc->fifo, fifo_size, GFP_KERNEL);
	if (rc) {
		dev_err(dev, "cannot allocate kFIFO\n");
		return -ENOMEM;
	}

	/* AP note A2600-15 */
	pcc->a2600_15 = of_property_read_bool(dev->of_node, "A2600-15");
	if (pcc->a2600_15)
		dev_info(dev, "A2600-15 AP note patch is selected\n");

	pcc->irq = platform_get_irq(pdev, 0);
	if (pcc->irq < 0) {
		dev_err(dev, "cannot get IRQ\n");
		rc = -ENODEV;
		goto err_free_kfifo;
	}

	rc = devm_request_irq(dev, pcc->irq, aspeed_pcc_isr, 0, DEVICE_NAME, pcc);
	if (rc < 0) {
		dev_err(dev, "cannot request IRQ handler\n");
		goto err_free_kfifo;
	}

	init_waitqueue_head(&pcc->wq);

	pcc->mdev_id = ida_alloc(&aspeed_pcc_ida, GFP_KERNEL);
	if (pcc->mdev_id < 0) {
		dev_err(dev, "cannot allocate ID\n");
		return pcc->mdev_id;
	}

	pcc->mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s%d", DEVICE_NAME,
					pcc->mdev_id);
	pcc->mdev.parent = dev;
	pcc->mdev.fops = &pcc_fops;
	rc = misc_register(&pcc->mdev);
	if (rc) {
		dev_err(dev, "cannot register misc device\n");
		goto err_free_kfifo;
	}

	rc = aspeed_pcc_enable(pcc, dev);
	if (rc) {
		dev_err(dev, "cannot enable PCC\n");
		goto err_dereg_mdev;
	}

	dev_set_drvdata(&pdev->dev, pcc);

	dev_info(dev, "module loaded\n");

	return 0;

err_dereg_mdev:
	misc_deregister(&pcc->mdev);

err_free_kfifo:
	kfifo_free(&pcc->fifo);

	return rc;
}

static int aspeed_pcc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_pcc *pcc = dev_get_drvdata(dev);

	kfifo_free(&pcc->fifo);
	misc_deregister(&pcc->mdev);

	return 0;
}

static const struct of_device_id aspeed_pcc_table[] = {
	{ .compatible = "aspeed,ast2500-lpc-pcc" },
	{ .compatible = "aspeed,ast2600-lpc-pcc" },
	{ .compatible = "aspeed,ast2700-lpc-pcc" },
	{ },
};

static struct platform_driver aspeed_pcc_driver = {
	.driver = {
		.name = "aspeed-pcc",
		.of_match_table = aspeed_pcc_table,
	},
	.probe = aspeed_pcc_probe,
	.remove = aspeed_pcc_remove,
};

module_platform_driver(aspeed_pcc_driver);

MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for Aspeed Post Code Capture");
