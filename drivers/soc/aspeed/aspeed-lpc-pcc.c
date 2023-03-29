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

#define DEVICE_NAME "aspeed-lpc-pcc"

#define LHCR5	0x0b4
#define LHCR6	0x0b8
#define PCCR6	0x0c4
#define LHCRA	0x0c8
#define   LHCRA_PAT_A_LEN_MASK		GENMASK(18, 17)
#define   LHCRA_PAT_A_LEN_SHIFT		17
#define   LHCRA_PAT_A_WRITE		BIT(16)
#define   LHCRA_PAT_A_ADDR_MASK		GENMASK(15, 0)
#define   LHCRA_PAT_A_ADDR_SHIFT	0
#define LHCRB	0x0cc
#define   LHCRB_PAT_B_LEN_MASK		GENMASK(18, 17)
#define   LHCRB_PAT_B_LEN_SHIFT		17
#define   LHCRB_PAT_B_WRITE		BIT(16)
#define   LHCRB_PAT_B_ADDR_MASK		GENMASK(15, 0)
#define   LHCRB_PAT_B_ADDR_SHIFT	0
#define PCCR4	0x0d0
#define PCCR5	0x0d4
#define PCCR0	0x130
#define   PCCR0_EN_DMA_INT		BIT(31)
#define   PCCR0_EN_PAT_B_INT		BIT(23)
#define   PCCR0_EN_PAT_B		BIT(22)
#define   PCCR0_EN_PAT_A_INT		BIT(21)
#define   PCCR0_EN_PAT_A		BIT(20)
#define   PCCR0_EN_DMA_MODE		BIT(14)
#define   PCCR0_ADDR_SEL_MASK		GENMASK(13, 12)
#define   PCCR0_ADDR_SEL_SHIFT		12
#define   PCCR0_RX_TRIG_LVL_MASK	GENMASK(10, 8)
#define   PCCR0_RX_TRIG_LVL_SHIFT	8
#define   PCCR0_CLR_RX_FIFO		BIT(7)
#define   PCCR0_MODE_SEL_MASK		GENMASK(5, 4)
#define   PCCR0_MODE_SEL_SHIFT		4
#define   PCCR0_EN_RX_OVR_INT		BIT(3)
#define   PCCR0_EN_RX_TMOUT_INT		BIT(2)
#define   PCCR0_EN_RX_AVAIL_INT		BIT(1)
#define   PCCR0_EN			BIT(0)
#define PCCR1	0x134
#define   PCCR1_BASE_ADDR_MASK		GENMASK(15, 0)
#define   PCCR1_BASE_ADDR_SHIFT		0
#define   PCCR1_DONT_CARE_BITS_MASK	GENMASK(21, 16)
#define   PCCR1_DONT_CARE_BITS_SHIFT	16
#define PCCR2	0x138
#define   PCCR2_PAT_B_RST		BIT(17)
#define   PCCR2_PAT_B_INT		BIT(16)
#define   PCCR2_PAT_A_RST		BIT(9)
#define   PCCR2_PAT_A_INT		BIT(8)
#define   PCCR2_DMA_DONE		BIT(4)
#define   PCCR2_DATA_RDY		PCCR2_DMA_DONE
#define   PCCR2_RX_OVR_INT		BIT(3)
#define   PCCR2_RX_TMOUT_INT		BIT(2)
#define   PCCR2_RX_AVAIL_INT		BIT(1)
#define PCCR3	0x13c
#define   PCCR3_FIFO_DATA_MASK		GENMASK(7, 0)

#define PCC_DMA_MAX_BUFSZ	(PAGE_SIZE)
#define PCC_MAX_PATNM		2

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

struct pcc_pattern {
	u32 enable;
	u32 pattern;
	u32 len;
	u32 write;
	u32 port;
};

struct aspeed_pcc_dma {
	u32 idx;
	u32 addr;
	u8 *virt;
	u32 size;
	u32 static_mem;
	struct tasklet_struct tasklet;
};

struct aspeed_pcc {
	struct device *dev;
	struct regmap *regmap;
	int irq;

	u32 rec_mode;

	u32 port;
	u32 port_xbits;
	u32 port_hbits_select;

	u32 dma_mode;
	struct aspeed_pcc_dma dma;

	struct pcc_pattern pat_search[PCC_MAX_PATNM];

	struct kfifo fifo;
	wait_queue_head_t wq;

	struct miscdevice misc_dev;
};

static inline bool is_pcc_enabled(struct aspeed_pcc *pcc)
{
	u32 reg;
	if (regmap_read(pcc->regmap, PCCR0, &reg))
		return false;
	return (reg & PCCR0_EN) ? true : false;
}

static inline bool is_valid_rec_mode(u32 mode)
{
	return (mode > PCC_REC_FULL) ? false : true;
}

static inline bool is_valid_high_bits_select(u32 select)
{
	return (select > PCC_PORT_HBITS_SEL_89) ? false : true;
}

static ssize_t aspeed_pcc_file_read(struct file *file, char __user *buffer,
		size_t count, loff_t *ppos)
{
	int rc;
	ssize_t copied;

	struct aspeed_pcc *pcc = container_of(
			file->private_data,
			struct aspeed_pcc,
			misc_dev);

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
			misc_dev);

	poll_wait(file, &pcc->wq, pt);
	return !kfifo_is_empty(&pcc->fifo) ? POLLIN : 0;
}

static const struct file_operations pcc_fops = {
	.owner = THIS_MODULE,
	.read = aspeed_pcc_file_read,
	.poll = aspeed_pcc_file_poll,
};

static void aspeed_pcc_dma_tasklet(unsigned long arg)
{
	u32 reg;
	u32 pre_dma_idx;
	u32 cur_dma_idx;
	struct aspeed_pcc *pcc = (struct aspeed_pcc*)arg;
	struct kfifo *fifo = &pcc->fifo;

	if (!kfifo_initialized(fifo))
		return;

	if (regmap_read(pcc->regmap, PCCR6, &reg))
		return;

	cur_dma_idx = reg & (PCC_DMA_MAX_BUFSZ - 1);
	pre_dma_idx = pcc->dma.idx;

	do {
		/* kick the oldest one if full */
		if (kfifo_is_full(fifo))
			kfifo_skip(fifo);
		kfifo_put(fifo, pcc->dma.virt[pre_dma_idx]);
		pre_dma_idx = (pre_dma_idx + 1) % PCC_DMA_MAX_BUFSZ;
	} while (pre_dma_idx != cur_dma_idx);

	pcc->dma.idx = cur_dma_idx;

	wake_up_interruptible(&pcc->wq);
}

static irqreturn_t aspeed_pcc_isr(int irq, void *arg)
{
	u32 val;
	irqreturn_t ret = IRQ_NONE;
	struct aspeed_pcc *pcc = (struct aspeed_pcc*)arg;

	if (regmap_read(pcc->regmap, PCCR2, &val))
		return ret;

	if (val & PCCR2_PAT_B_INT) {
		dev_info(pcc->dev, "pattern search B interrupt\n");
		regmap_write_bits(pcc->regmap, PCCR2,
			PCCR2_PAT_B_INT, PCCR2_PAT_B_INT);
		ret = IRQ_HANDLED;
	}

	if (val & PCCR2_PAT_A_INT) {
		dev_info(pcc->dev, "pattern search A interrupt\n");
		regmap_write_bits(pcc->regmap, PCCR2,
			PCCR2_PAT_A_INT, PCCR2_PAT_A_INT);
		ret = IRQ_HANDLED;
	}

	if (val & PCCR2_RX_OVR_INT) {
		dev_warn(pcc->dev, "RX FIFO overrun\n");
		regmap_write_bits(pcc->regmap, PCCR2,
			PCCR2_RX_OVR_INT, PCCR2_RX_OVR_INT);
		ret = IRQ_HANDLED;
	}

	if (val & (PCCR2_DMA_DONE | PCCR2_RX_TMOUT_INT | PCCR2_RX_AVAIL_INT)) {
		if (pcc->dma_mode) {
			regmap_write_bits(pcc->regmap, PCCR2,
					PCCR2_DMA_DONE, PCCR2_DMA_DONE);
			tasklet_schedule(&pcc->dma.tasklet);
		}
		else {
			do {
				if (regmap_read(pcc->regmap, PCCR3, &val))
					break;
				if (kfifo_is_full(&pcc->fifo))
					kfifo_skip(&pcc->fifo);
				kfifo_put(&pcc->fifo, val & PCCR3_FIFO_DATA_MASK);

				if (regmap_read(pcc->regmap, PCCR2, &val))
					break;
			} while (val & PCCR2_DATA_RDY);

			wake_up_interruptible(&pcc->wq);
		}
		ret = IRQ_HANDLED;
	}

	return ret;
}

static void aspeed_pcc_config(struct aspeed_pcc *pcc)
{
	struct pcc_pattern* pat_search = pcc->pat_search;

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

	/* pattern search state reset */
	regmap_write_bits(pcc->regmap, PCCR2,
			PCCR2_PAT_B_RST | PCCR2_PAT_A_RST,
			PCCR2_PAT_B_RST | PCCR2_PAT_A_RST);

	/* pattern A to search */
	regmap_write(pcc->regmap, LHCR5, pat_search[0].pattern);
	regmap_update_bits(pcc->regmap, LHCRA,
			LHCRA_PAT_A_LEN_MASK,
			(pat_search[0].len - 1) << LHCRA_PAT_A_LEN_SHIFT);
	regmap_update_bits(pcc->regmap, LHCRA,
			LHCRA_PAT_A_WRITE,
			(pat_search[0].write) ? LHCRA_PAT_A_WRITE : 0);
	regmap_update_bits(pcc->regmap, LHCRA,
			LHCRA_PAT_A_ADDR_MASK,
			pat_search[0].port << LHCRA_PAT_A_ADDR_SHIFT);
	regmap_update_bits(pcc->regmap, PCCR0,
			PCCR0_EN_PAT_A_INT | PCCR0_EN_PAT_A,
			(pat_search[0].enable) ? PCCR0_EN_PAT_A_INT | PCCR0_EN_PAT_A : 0);

	/* pattern B to search */
	regmap_write(pcc->regmap, LHCR6, pat_search[1].pattern);
	regmap_update_bits(pcc->regmap, LHCRB,
			LHCRB_PAT_B_LEN_MASK,
			(pat_search[1].len - 1) << LHCRB_PAT_B_LEN_SHIFT);
	regmap_update_bits(pcc->regmap, LHCRB,
			LHCRB_PAT_B_WRITE,
			(pat_search[1].write) ? LHCRB_PAT_B_WRITE : 0);
	regmap_update_bits(pcc->regmap, LHCRB,
			LHCRB_PAT_B_ADDR_MASK,
			pat_search[1].port << LHCRB_PAT_B_ADDR_SHIFT);
	regmap_update_bits(pcc->regmap, PCCR0,
			PCCR0_EN_PAT_B_INT | PCCR0_EN_PAT_B,
			PCCR0_EN_PAT_B_INT | PCCR0_EN_PAT_B);
	regmap_update_bits(pcc->regmap, PCCR0,
			PCCR0_EN_PAT_B_INT | PCCR0_EN_PAT_B,
			(pat_search[1].enable) ? PCCR0_EN_PAT_B_INT | PCCR0_EN_PAT_B : 0);

	/* DMA address and size (4-bytes unit) */
	if (pcc->dma_mode) {
		regmap_write(pcc->regmap, PCCR4, pcc->dma.addr);
		regmap_write(pcc->regmap, PCCR5, pcc->dma.size / 4);
	}
}

static int aspeed_pcc_enable(struct aspeed_pcc *pcc, struct device *dev)
{
	int rc;

	if (pcc->dma_mode) {
		/* map reserved memory or allocate a new one for DMA use */
		if (pcc->dma.static_mem) {
			if (pcc->dma.size > PCC_DMA_MAX_BUFSZ) {
				rc = -EINVAL;
				goto err_ret;
			}

			pcc->dma.virt = ioremap(pcc->dma.addr,
							  pcc->dma.size);
			if (pcc->dma.virt == NULL) {
				rc = -ENOMEM;
				goto err_ret;
			}
		}
		else {
			pcc->dma.size = PCC_DMA_MAX_BUFSZ;
			pcc->dma.virt = dma_alloc_coherent(dev,
					pcc->dma.size,
					&pcc->dma.addr,
					GFP_KERNEL);
			if (pcc->dma.virt == NULL) {
				rc = -ENOMEM;
				goto err_ret;
			}
		}
	}

	rc = kfifo_alloc(&pcc->fifo, PAGE_SIZE, GFP_KERNEL);
	if (rc)
		goto err_free_dma;

	pcc->misc_dev.parent = dev;
	pcc->misc_dev.name = devm_kasprintf(dev, GFP_KERNEL, "%s", DEVICE_NAME);
	pcc->misc_dev.fops = &pcc_fops;
	rc = misc_register(&pcc->misc_dev);
	if (rc)
		goto err_free_kfifo;

	aspeed_pcc_config(pcc);

	/* skip FIFO cleanup if already enabled */
	if (!is_pcc_enabled(pcc))
		regmap_write_bits(pcc->regmap, PCCR0,
				PCCR0_CLR_RX_FIFO, PCCR0_CLR_RX_FIFO);

	if (pcc->dma_mode) {
		regmap_update_bits(pcc->regmap, PCCR0,
			PCCR0_EN_DMA_INT | PCCR0_EN_DMA_MODE,
			PCCR0_EN_DMA_INT | PCCR0_EN_DMA_MODE);
	}
	else {
		regmap_update_bits(pcc->regmap, PCCR0,
			PCCR0_RX_TRIG_LVL_MASK,
			PCC_FIFO_THR_4_EIGHTH << PCCR0_RX_TRIG_LVL_SHIFT);
		regmap_update_bits(pcc->regmap, PCCR0,
			PCCR0_EN_RX_OVR_INT | PCCR0_EN_RX_TMOUT_INT | PCCR0_EN_RX_AVAIL_INT,
			PCCR0_EN_RX_OVR_INT | PCCR0_EN_RX_TMOUT_INT | PCCR0_EN_RX_AVAIL_INT);
	}

	regmap_update_bits(pcc->regmap, PCCR0, PCCR0_EN, PCCR0_EN);
	return 0;

err_free_kfifo:
	kfifo_free(&pcc->fifo);
err_free_dma:
	if (pcc->dma_mode) {
		if (pcc->dma.static_mem)
			iounmap(pcc->dma.virt);
		else
			dma_free_coherent(dev, pcc->dma.size,
					pcc->dma.virt, pcc->dma.addr);
	}
err_ret:
	return rc;
}

static int aspeed_pcc_disable(struct aspeed_pcc *pcc, struct device *dev)
{
	regmap_update_bits(pcc->regmap, PCCR0,
		PCCR0_EN_DMA_INT
		| PCCR0_EN_RX_OVR_INT
		| PCCR0_EN_RX_TMOUT_INT
		| PCCR0_EN_RX_AVAIL_INT
		| PCCR0_EN_DMA_MODE
		| PCCR0_EN,
		0);

	if (pcc->dma.static_mem)
		iounmap(pcc->dma.virt);
	else
		dma_free_coherent(dev, pcc->dma.size,
				pcc->dma.virt, pcc->dma.addr);

	misc_deregister(&pcc->misc_dev);
	kfifo_free(&pcc->fifo);

	return 0;
}

static int aspeed_pcc_probe(struct platform_device *pdev)
{
	int rc;

	struct aspeed_pcc *pcc;

	struct device *dev = &pdev->dev;
	struct device_node *node;

	struct resource res;

	pcc = devm_kzalloc(&pdev->dev, sizeof(*pcc), GFP_KERNEL);
	if (!pcc) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pcc->regmap = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(pcc->regmap)) {
		dev_err(dev, "failed to get regmap\n");
		return -ENODEV;
	}

	rc = of_property_read_u32(dev->of_node, "port-addr", &pcc->port);
	if (rc) {
		dev_err(dev, "failed to get port base address\n");
		return rc;
	}

	pcc->dma_mode = of_property_read_bool(dev->of_node, "dma-mode");
	if (pcc->dma_mode) {
		/*
		 * optional, reserved memory for the DMA buffer
		 * if not specified, the DMA buffer is allocated
		 * dynamically.
		 */
		node = of_parse_phandle(dev->of_node, "memory-region", 0);
		if (node) {
			rc = of_address_to_resource(node, 0, &res);
			if (rc) {
				dev_err(dev, "failed to get reserved memory region\n");
				return -ENOMEM;
			}
			pcc->dma.addr = res.start;
			pcc->dma.size = resource_size(&res);
			pcc->dma.static_mem = 1;
			of_node_put(node);
		}
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
		of_property_read_u32(dev->of_node, "port-addr-hbits-select", &pcc->port_hbits_select);
		if (!is_valid_high_bits_select(pcc->port_hbits_select)) {
			dev_err(dev, "invalid high address bits selection: %u\n",
				pcc->port_hbits_select);
			return -EINVAL;
		}
	}
	else
		pcc->port_hbits_select = 0x3;

	/* optional, pattern search A */
	if (of_property_read_bool(dev->of_node, "pattern-a-en")) {
		of_property_read_u32(dev->of_node, "pattern-a", &pcc->pat_search[0].pattern);
		of_property_read_u32(dev->of_node, "pattern-a-len", &pcc->pat_search[0].len);
		of_property_read_u32(dev->of_node, "pattern-a-write", &pcc->pat_search[0].write);
		of_property_read_u32(dev->of_node, "pattern-a-port", &pcc->pat_search[0].port);
		pcc->pat_search[0].enable = 1;
	}

	/* optional, pattern search B */
	if (of_property_read_bool(dev->of_node, "pattern-b-en")) {
		of_property_read_u32(dev->of_node, "pattern-b", &pcc->pat_search[1].pattern);
		of_property_read_u32(dev->of_node, "pattern-b-len", &pcc->pat_search[1].len);
		of_property_read_u32(dev->of_node, "pattern-b-write", &pcc->pat_search[1].write);
		of_property_read_u32(dev->of_node, "pattern-b-port", &pcc->pat_search[1].port);
		pcc->pat_search[1].enable = 1;
	}

	pcc->irq = platform_get_irq(pdev, 0);
	if (!pcc->irq) {
		dev_err(dev, "failed to get IRQ\n");
		return -ENODEV;
	}

	/*
	 * as PCC may have been enabled in early stages, we
	 * need to disable interrupts before requesting IRQ
	 * to prevent kernel crash
	 */
	regmap_update_bits(pcc->regmap, PCCR0,
			PCCR0_EN_DMA_INT
			| PCCR0_EN_PAT_A_INT
			| PCCR0_EN_PAT_B_INT
			| PCCR0_EN_RX_OVR_INT
			| PCCR0_EN_RX_TMOUT_INT
			| PCCR0_EN_RX_AVAIL_INT,
			0);

	rc = devm_request_irq(dev, pcc->irq, aspeed_pcc_isr,
			IRQF_SHARED, DEVICE_NAME, pcc);
	if (rc < 0) {
		dev_err(dev, "failed to request IRQ handler\n");
		return rc;
	}

	tasklet_init(&pcc->dma.tasklet, aspeed_pcc_dma_tasklet,
			(unsigned long)pcc);

	init_waitqueue_head(&pcc->wq);

	rc = aspeed_pcc_enable(pcc, dev);
	if (rc) {
		dev_err(dev, "failed to enable PCC\n");
		return rc;
	}

	pcc->dev = dev;
	dev_set_drvdata(&pdev->dev, pcc);

	dev_info(dev, "module loaded\n");

	return 0;
}

static int aspeed_pcc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_pcc *pcc = dev_get_drvdata(dev);
	aspeed_pcc_disable(pcc, dev);
	return 0;
}

static const struct of_device_id aspeed_pcc_table[] = {
	{ .compatible = "aspeed,ast2500-lpc-pcc" },
	{ .compatible = "aspeed,ast2600-lpc-pcc" },
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
