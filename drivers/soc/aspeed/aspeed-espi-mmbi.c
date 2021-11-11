// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2021 Aspeed Technology Inc.
 */
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/poll.h>

#include "aspeed-espi-ctrl.h"
#include "aspeed-espi-perif.h"

#define DEVICE_NAME "aspeed-espi-mmbi"

#define MMBI_CTRL		0x800
#define MMBI_CTRL_INST_SZ_MASK		GENMASK(10, 8)
#define MMBI_CTRL_INST_SZ_SHIFT		8
#define MMBI_CTRL_TOTAL_SZ_MASK		GENMASK(6, 4)
#define MMBI_CTRL_TOTAL_SZ_SHIFT	4
#define MMBI_CTRL_EN			BIT(0)
#define MMBI_INT_STS		0x808
#define MMBI_INT_EN		0x80c
#define MMBI_HOST_RWP(x)	(0x810 + (x << 3))

#define MMBI_INST_NUM	8

enum aspeed_espi_mmbi_inst_size {
	MMBI_INST_SIZE_8KB = 0x0,
	MMBI_INST_SIZE_16KB,
	MMBI_INST_SIZE_32KB,
	MMBI_INST_SIZE_64KB,
	MMBI_INST_SIZE_128KB,
	MMBI_INST_SIZE_256KB,
	MMBI_INST_SIZE_512KB,
	MMBI_INST_SIZE_1024KB,
	MMBI_INST_SIZE_TYPES,
};

struct aspeed_espi_mmbi_instance {
	uint32_t idx;
	dma_addr_t b2h_addr;
	dma_addr_t h2b_addr;
	struct miscdevice mdev_b2h;
	struct miscdevice mdev_h2b;
	bool host_rwp_updated;
	wait_queue_head_t wq;
	struct aspeed_espi_mmbi *mmbi;
};

struct aspeed_espi_mmbi {
	struct device *dev;
	struct regmap *map;
	int irq;

	uint32_t inst_sz;

	void *virt;
	dma_addr_t addr;
	uint32_t src_addr;

	struct aspeed_espi_mmbi_instance inst[MMBI_INST_NUM];
	struct aspeed_espi_ctrl *espi_ctrl;
};


static int aspeed_espi_mmbi_b2h_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct aspeed_espi_mmbi_instance *mmbi_inst = container_of(fp->private_data,
								   struct aspeed_espi_mmbi_instance, mdev_b2h);
	struct aspeed_espi_mmbi *espi_mmbi = mmbi_inst->mmbi;
	unsigned long vm_size = vma->vm_end - vma->vm_start;
	pgprot_t prot = vma->vm_page_prot;

	if (((vma->vm_pgoff << PAGE_SHIFT) + vm_size) > (0x1000 << espi_mmbi->inst_sz))
		return -EINVAL;

	prot = pgprot_noncached(prot);

	if (remap_pfn_range(vma, vma->vm_start,
			    (mmbi_inst->b2h_addr >> PAGE_SHIFT) + vma->vm_pgoff,
			    vm_size, prot))
		return -EAGAIN;

	return 0;
}

static int aspeed_espi_mmbi_h2b_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct aspeed_espi_mmbi_instance *mmbi_inst = container_of(fp->private_data,
								   struct aspeed_espi_mmbi_instance, mdev_h2b);
	struct aspeed_espi_mmbi *espi_mmbi = mmbi_inst->mmbi;
	unsigned long vm_size = vma->vm_end - vma->vm_start;
	pgprot_t prot = vma->vm_page_prot;

	if (((vma->vm_pgoff << PAGE_SHIFT) + vm_size) > (0x1000 << espi_mmbi->inst_sz))
		return -EINVAL;

	prot = pgprot_noncached(prot);

	if (remap_pfn_range(vma, vma->vm_start,
			    (mmbi_inst->h2b_addr >> PAGE_SHIFT) + vma->vm_pgoff,
			    vm_size, prot))
		return -EAGAIN;

	return 0;
}

static __poll_t aspeed_espi_mmbi_h2b_poll(struct file *fp, struct poll_table_struct *pt)
{
	struct aspeed_espi_mmbi_instance *mmbi_inst = container_of(fp->private_data,
			struct aspeed_espi_mmbi_instance, mdev_h2b);

	poll_wait(fp, &mmbi_inst->wq, pt);

	if (!mmbi_inst->host_rwp_updated)
		return 0;

	mmbi_inst->host_rwp_updated = false;

	return EPOLLIN;
}

static irqreturn_t aspeed_espi_mmbi_isr(int irq, void *arg)
{
	int i;
	uint32_t sts, tmp;
	struct aspeed_espi_mmbi *espi_mmbi = (struct aspeed_espi_mmbi *)arg;

	regmap_read(espi_mmbi->map, MMBI_INT_STS, &sts);

	tmp = sts;
	for (i = 0; i < MMBI_INST_NUM; ++i, tmp >>= 2) {
		if (!(tmp & 0x3))
		    continue;

		regmap_read(espi_mmbi->map, MMBI_HOST_RWP(i),
			    espi_mmbi->virt + (0x1000 << espi_mmbi->inst_sz) * (i + MMBI_INST_NUM));
		regmap_read(espi_mmbi->map, MMBI_HOST_RWP(i) + 4,
			    espi_mmbi->virt + (0x1000 << espi_mmbi->inst_sz) * (i + MMBI_INST_NUM) + 4);

		espi_mmbi->inst[i].host_rwp_updated = true;
		wake_up_interruptible(&espi_mmbi->inst[i].wq);
	}

	regmap_write(espi_mmbi->map, MMBI_INT_STS, sts);

	return IRQ_HANDLED;
}

static const struct file_operations aspeed_espi_mmbi_b2h_fops = {
	.owner = THIS_MODULE,
	.mmap = aspeed_espi_mmbi_b2h_mmap,
};

static const struct file_operations aspeed_espi_mmbi_h2b_fops = {
	.owner = THIS_MODULE,
	.mmap = aspeed_espi_mmbi_h2b_mmap,
	.poll = aspeed_espi_mmbi_h2b_poll,
};

static int aspeed_espi_mmbi_enable(struct aspeed_espi_mmbi *espi_mmbi)
{
	int i, rc;
	uint32_t reg;
	struct aspeed_espi_mmbi_instance *mmbi_inst;

	espi_mmbi->virt = dma_alloc_coherent(espi_mmbi->dev,
					     (0x2000 << espi_mmbi->inst_sz) * MMBI_INST_NUM,
					     &espi_mmbi->addr, GFP_KERNEL);
	if (!espi_mmbi->virt)
		return -ENOMEM;

	for (i = 0; i < MMBI_INST_NUM; ++i) {
		mmbi_inst = &espi_mmbi->inst[i];
		mmbi_inst->idx = i;
		mmbi_inst->b2h_addr = espi_mmbi->addr +
				      ((0x1000 << espi_mmbi->inst_sz) * i);
		mmbi_inst->h2b_addr = espi_mmbi->addr +
				      ((0x1000 << espi_mmbi->inst_sz) * (i + MMBI_INST_NUM));

		mmbi_inst->mdev_b2h.parent = espi_mmbi->dev;
		mmbi_inst->mdev_b2h.minor = MISC_DYNAMIC_MINOR;
		mmbi_inst->mdev_b2h.name = devm_kasprintf(espi_mmbi->dev, GFP_KERNEL, "%s-b2h%d", DEVICE_NAME, i);
		mmbi_inst->mdev_b2h.fops = &aspeed_espi_mmbi_b2h_fops;
		rc = misc_register(&mmbi_inst->mdev_b2h);
		if (rc) {
			dev_err(espi_mmbi->dev, "cannot register device %s\n", mmbi_inst->mdev_b2h.name);
			return rc;
		}

		mmbi_inst->mdev_h2b.parent = espi_mmbi->dev;
		mmbi_inst->mdev_h2b.minor = MISC_DYNAMIC_MINOR;
		mmbi_inst->mdev_h2b.name = devm_kasprintf(espi_mmbi->dev, GFP_KERNEL, "%s-h2b%d", DEVICE_NAME, i);
		mmbi_inst->mdev_h2b.fops = &aspeed_espi_mmbi_h2b_fops;
		rc = misc_register(&mmbi_inst->mdev_h2b);
		if (rc) {
			dev_err(espi_mmbi->dev, "cannot register device %s\n", mmbi_inst->mdev_h2b.name);
			return rc;
		}

		init_waitqueue_head(&mmbi_inst->wq);

		mmbi_inst->host_rwp_updated = false;
		mmbi_inst->mmbi = espi_mmbi;
	}

	rc = devm_request_irq(espi_mmbi->dev, espi_mmbi->irq,
			      aspeed_espi_mmbi_isr,
			      0, DEVICE_NAME, espi_mmbi);
	if (rc) {
		dev_err(espi_mmbi->dev, "failed to request IRQ\n");
		return rc;
	}

	regmap_write(espi_mmbi->map, MMBI_INT_EN, 0);
	regmap_write(espi_mmbi->map, MMBI_INT_STS, 0xffff);

	regmap_update_bits(espi_mmbi->map, ESPI_CTRL2,
			ESPI_CTRL2_MEMCYC_RD_DIS | ESPI_CTRL2_MEMCYC_WR_DIS, 0);
	regmap_write(espi_mmbi->map, ESPI_PERIF_PC_RX_MASK,
			~(((0x2000 << espi_mmbi->inst_sz) * MMBI_INST_NUM) - 1));
	regmap_write(espi_mmbi->map, ESPI_PERIF_PC_RX_SADDR, espi_mmbi->src_addr);
	regmap_write(espi_mmbi->map, ESPI_PERIF_PC_RX_TADDR, espi_mmbi->addr);

	reg = ((espi_mmbi->inst_sz << MMBI_CTRL_INST_SZ_SHIFT) & MMBI_CTRL_INST_SZ_MASK) |
	      ((espi_mmbi->inst_sz << MMBI_CTRL_TOTAL_SZ_SHIFT) & MMBI_CTRL_TOTAL_SZ_MASK) |
	      MMBI_CTRL_EN;

	regmap_write(espi_mmbi->map, MMBI_CTRL, reg);
	regmap_write(espi_mmbi->map, MMBI_INT_EN, 0xffff);

	return 0;
}

static int aspeed_espi_mmbi_disable(struct aspeed_espi_mmbi *espi_mmbi)
{
	int i;

	for (i = 0; i < MMBI_INST_NUM; ++i) {
		misc_deregister(&espi_mmbi->inst[i].mdev_b2h);
		misc_deregister(&espi_mmbi->inst[i].mdev_h2b);
	}

	if (espi_mmbi->virt)
		dma_free_coherent(espi_mmbi->dev,
				  (0x2000 << espi_mmbi->inst_sz) * MMBI_INST_NUM,
				  espi_mmbi->virt, espi_mmbi->addr);

	return 0;
}

static int aspeed_espi_mmbi_probe(struct platform_device *pdev)
{
	int rc;
	struct device *dev = &pdev->dev;
	struct aspeed_espi_mmbi *espi_mmbi;
	uint32_t reg;

	espi_mmbi = devm_kzalloc(dev, sizeof(*espi_mmbi), GFP_KERNEL);
	if (!espi_mmbi)
		return -ENOMEM;

	espi_mmbi->map = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(espi_mmbi->map)) {
		dev_err(dev, "cannot get remap\n");
		return -ENODEV;
	}

	regmap_read(espi_mmbi->map, ESPI_CTRL, &reg);
	if (!(reg & ESPI_CTRL_PERIF_SW_RDY))
		return -EPROBE_DEFER;

	espi_mmbi->irq = platform_get_irq(pdev, 0);
	if (espi_mmbi->irq < 0)
		return espi_mmbi->irq;

	rc = of_property_read_u32(dev->of_node, "host-src-addr", &espi_mmbi->src_addr);
	if (rc) {
		dev_err(dev, "cannot get Host source address\n");
		return -ENODEV;
	}

	rc = of_property_read_u32(dev->of_node, "instance-size", &espi_mmbi->inst_sz);
	if (rc) {
		dev_err(dev, "cannot get instance size\n");
		return -ENODEV;
	}

	if (espi_mmbi->inst_sz >= MMBI_INST_SIZE_TYPES) {
		dev_err(dev, "invalid MMBI instance size\n");
		return -EINVAL;
	}

	espi_mmbi->dev = dev;

	rc = aspeed_espi_mmbi_enable(espi_mmbi);
	if (rc)
	    return rc;

	dev_info(dev, "module loaded\n");

	return 0;
}

static int aspeed_espi_mmbi_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_espi_mmbi *espi_mmbi = dev_get_drvdata(dev);

	aspeed_espi_mmbi_disable(espi_mmbi);

	return 0;
}

static const struct of_device_id aspeed_espi_mmbi_of_matches[] = {
	{ .compatible = "aspeed,ast2600-espi-mmbi" },
	{ },
};

static struct platform_driver aspeed_espi_mmbi_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = aspeed_espi_mmbi_of_matches,
	},
	.probe = aspeed_espi_mmbi_probe,
	.remove = aspeed_espi_mmbi_remove,
};

module_platform_driver(aspeed_espi_mmbi_driver);

MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_DESCRIPTION("Control of Aspeed eSPI MMBI Device");
MODULE_LICENSE("GPL v2");
