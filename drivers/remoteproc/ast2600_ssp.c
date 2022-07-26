// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2022 Aspeed Technology Inc.
 */
#include <linux/io.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/virtio_ids.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-mapping.h>

#include "remoteproc_internal.h"

/* SCU SSP register offsets */
#define SSP_CTRL		0xa00
#define   SSP_CTRL_RST		BIT(1)
#define   SSP_CTRL_EN		BIT(0)
#define SSP_MEM_BASE		0xa04
#define SSP_IMEM_LIMIT		0xa08
#define SSP_DMEM_LIMIT		0xa0c
#define SSP_CACHE_RGN		0xa40
#define   SSP_CACHE_RGN_16MB(x)	BIT(x)
#define SSP_CACHE_INVALID	0xa44
#define SSP_CACHE_CTRL		0xa48
#define   SSP_CACHE_CTRL_ICLR	BIT(2)
#define   SSP_CACHE_CTRL_DCLR	BIT(2)
#define   SSP_CACHE_CTRL_EN	BIT(0)

/* SWVIC register offsets */
#define CA7_SWINT	0x18
#define CA7_SWINT_CLR	0x1c
#define CM3_SWINT	0x28
#define	CM3_SWINT_CLR	0x2c

#define SSP_IMG		"ast2600_ssp.bin"
#define SSP_IRQ_NUM	15

#define SSP_MEM_RGN_SIZE	(16 * 1024 * 1024)

#ifndef VIRTIO_RPMSG_F_NS
#define VIRTIO_RPMSG_F_NS	0
#endif

/* SSP memory region */
enum ast2600_mem_region {
	SSP_MEM_ALL,
	SSP_MEM_FW,
	SSP_MEM_SHM,
	SSP_MEM_RSC_TBL,
	SSP_MEM_VDEV0VRING0,
	SSP_MEM_VDEV0VRING1,
	SSP_MEM_VDEV0BUFFER,

	SSP_MEM_REGIONS,
};

struct ast2600_ssp_mem {
	void *va;
	dma_addr_t pa;
	size_t sz;
};

struct ast2600_ssp {
	struct rproc *rproc;
	struct device *dev;

	void __iomem *scu;
	void __iomem *cvic;

	int irq[SSP_IRQ_NUM];
	struct work_struct work;

	struct ast2600_ssp_mem mem[SSP_MEM_REGIONS];
};

/* vq callback uses mutex_xx and should be executed in process context */
static irqreturn_t ast2600_ssp_isr_bh(int irq, void *arg)
{
	int i;
	struct ast2600_ssp *ssp = (struct ast2600_ssp *)arg;

	for (i = 0; i < SSP_IRQ_NUM; ++i)
		if (ssp->irq[i] == irq)
			return rproc_vq_interrupt(ssp->rproc, i);

	return IRQ_NONE;
}

static irqreturn_t ast2600_ssp_isr(int irq, void *arg)
{
	int i;
	struct ast2600_ssp *ssp = (struct ast2600_ssp *)arg;

	for (i = 0; i < SSP_IRQ_NUM; ++i) {
		if (ssp->irq[i] == irq) {
			writel(BIT(i), ssp->cvic + CA7_SWINT_CLR);
			return IRQ_WAKE_THREAD;
		}
	}

	return IRQ_NONE;
}

static int ast2600_ssp_gen_rsc_tbl(struct rproc *rproc, const struct firmware *fw)
{
	int i;
	struct fw_rsc_hdr *ssp_fw_rsc_hdr;
	struct fw_rsc_vdev *ssp_fw_rsc_vdev;
	struct fw_rsc_vdev_vring *ssp_fw_rsc_vdev_vring;
	struct ast2600_ssp *ssp = (struct ast2600_ssp *)rproc->priv;
	struct resource_table *ssp_rsc_tbl = (struct resource_table *)ssp->mem[SSP_MEM_RSC_TBL].va;

	/*
	 * compose resource table for the compatibility
	 * with raw firmware binary and adapt to virtio
	 * based service
	 */
	ssp_rsc_tbl->num = 1;
	ssp_rsc_tbl->offset[0] = sizeof(*ssp_rsc_tbl) + ssp_rsc_tbl->num * sizeof(u32);

	/* entry header */
	ssp_fw_rsc_hdr = (void *)ssp_rsc_tbl + ssp_rsc_tbl->offset[0];
	ssp_fw_rsc_hdr->type = RSC_VDEV;

	/* vdev entry */
	ssp_fw_rsc_vdev = (void *)&ssp_fw_rsc_hdr[1];
	ssp_fw_rsc_vdev->id = VIRTIO_ID_RPMSG;
	ssp_fw_rsc_vdev->dfeatures = BIT(VIRTIO_RPMSG_F_NS);
	ssp_fw_rsc_vdev->config_len = 2 * sizeof(*ssp_fw_rsc_vdev_vring);
	ssp_fw_rsc_vdev->num_of_vrings = 2;

	/* vdev vring entry */
	for (i = 0; i < ssp_fw_rsc_vdev->num_of_vrings; ++i) {
		ssp_fw_rsc_vdev_vring = (void *)&ssp_fw_rsc_vdev->vring[i];
		ssp_fw_rsc_vdev_vring->align = 16;
		ssp_fw_rsc_vdev_vring->num = 8;
		ssp_fw_rsc_vdev_vring->da = FW_RSC_ADDR_ANY;
	}

	rproc->table_ptr = ssp_rsc_tbl;
	rproc->table_sz = ssp->mem[SSP_MEM_RSC_TBL].sz;

	return 0;
}

static int ast2600_ssp_mem_alloc(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	/* do nothing as we already allocated */
	return 0;
}

static int ast2600_ssp_mem_release(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	/* do nothing as DMA memory is managed */
	return 0;
}

static int ast2600_ssp_prepare(struct rproc *rproc)
{
	struct rproc_mem_entry *mem;
	struct ast2600_ssp *ssp = (struct ast2600_ssp *)rproc->priv;
	struct ast2600_ssp_mem *vdev0vring0 = &ssp->mem[SSP_MEM_VDEV0VRING0];
	struct ast2600_ssp_mem *vdev0vring1 = &ssp->mem[SSP_MEM_VDEV0VRING1];
	struct ast2600_ssp_mem *vdev0buffer = &ssp->mem[SSP_MEM_VDEV0BUFFER];
	struct device *dev = ssp->dev;

	mem = rproc_mem_entry_init(dev,
				   vdev0vring0->va,
				   vdev0vring0->pa,
				   vdev0vring0->sz,
				   vdev0vring0->pa,
				   ast2600_ssp_mem_alloc,
				   ast2600_ssp_mem_release,
				   "vdev0vring0");
	if (!mem) {
		dev_err(dev, "cannot allocate memory entry for vdev0vring0");
		return -ENOMEM;
	}

	dev_dbg(dev, "vdev0vring0 va %pK, pa 0x%08x, len 0x%08x\n",
		 vdev0vring0->va,
		 vdev0vring0->pa,
		 vdev0vring0->sz);
	rproc_add_carveout(rproc, mem);

	mem = rproc_mem_entry_init(dev,
				   vdev0vring1->va,
				   vdev0vring1->pa,
				   vdev0vring1->sz,
				   vdev0vring1->pa,
				   ast2600_ssp_mem_alloc,
				   ast2600_ssp_mem_release,
				   "vdev0vring1");
	if (!mem) {
		dev_err(dev, "cannot allocate memory entry for vdev0vring1");
		return -ENOMEM;
	}

	dev_dbg(dev, "vdev0vring1 va %pK, pa 0x%08x, len 0x%08x\n",
		 vdev0vring1->va,
		 vdev0vring1->pa,
		 vdev0vring1->sz);
	rproc_add_carveout(rproc, mem);

	mem = rproc_mem_entry_init(dev,
				   vdev0buffer->va,
				   vdev0buffer->pa,
				   vdev0buffer->sz,
				   vdev0buffer->pa,
				   ast2600_ssp_mem_alloc,
				   ast2600_ssp_mem_release,
				   "vdev0buffer");
	if (!mem) {
		dev_err(dev, "cannot allocate memory entry for vdev0buffer");
		return -ENOMEM;
	}

	dev_dbg(dev, "vdev0buffer va %pK, pa 0x%08x, len 0x%08x\n",
		 vdev0buffer->va,
		 vdev0buffer->pa,
		 vdev0buffer->sz);
	rproc_add_carveout(rproc, mem);

	return 0;
}

static int ast2600_ssp_start(struct rproc *rproc)
{
	struct ast2600_ssp *ssp = rproc->priv;
	struct device *dev = ssp->dev;
	struct ast2600_ssp_mem *mem = &ssp->mem[SSP_MEM_ALL];

	regmap_write(ssp->scu, SSP_CTRL, 0);
	mdelay(1);

	regmap_write(ssp->scu, SSP_MEM_BASE, mem->pa);

	/*
	 * by default make instruction memory cacheable
	 * the data memory cachability is controlled by
	 * SSP itself
	 */
	regmap_write(ssp->scu, SSP_IMEM_LIMIT, mem->pa + SSP_MEM_RGN_SIZE);
	regmap_write(ssp->scu, SSP_DMEM_LIMIT, mem->pa + mem->sz);
	regmap_write(ssp->scu, SSP_CACHE_RGN, SSP_CACHE_RGN_16MB(0));

	regmap_write(ssp->scu, SSP_CTRL, SSP_CTRL_RST);
	mdelay(1);

	regmap_write(ssp->scu, SSP_CTRL, 0);
	mdelay(1);

	regmap_write(ssp->scu, SSP_CTRL, SSP_CTRL_EN);

	dev_info(dev, "SSP started\n");

	return 0;
}

static int ast2600_ssp_stop(struct rproc *rproc)
{
	struct ast2600_ssp *ssp = rproc->priv;
	struct device *dev = ssp->dev;

	regmap_write(ssp->scu, SSP_CTRL, 0);
	mdelay(1);

	dev_info(dev, "SSP stopped\n");

	return 0;
}

static int ast2600_ssp_load(struct rproc *rproc, const struct firmware *fw)
{
	struct ast2600_ssp *ssp = rproc->priv;

	if (fw->size > ssp->mem[SSP_MEM_FW].sz)
		return -EINVAL;

	memcpy(ssp->mem[SSP_MEM_FW].va, fw->data, fw->size);

	return 0;
}

static void ast2600_ssp_kick(struct rproc *rproc, int vqid)
{
	struct ast2600_ssp *ssp = rproc->priv;

	writel(BIT(vqid), ssp->cvic + CM3_SWINT);
}

static const struct rproc_ops ast2600_ssp_ops = {
	.prepare = ast2600_ssp_prepare,
	.start = ast2600_ssp_start,
	.stop = ast2600_ssp_stop,
	.load = ast2600_ssp_load,
	.kick = ast2600_ssp_kick,
	.parse_fw = ast2600_ssp_gen_rsc_tbl,
};

static int ast2600_ssp_mem_init(struct ast2600_ssp *ssp)
{
	struct device *dev = ssp->dev;
	struct ast2600_ssp_mem *mem = &ssp->mem[SSP_MEM_ALL];
	struct ast2600_ssp_mem *fw = &ssp->mem[SSP_MEM_FW];
	struct ast2600_ssp_mem *shm = &ssp->mem[SSP_MEM_SHM];
	struct ast2600_ssp_mem *rsc_tbl = &ssp->mem[SSP_MEM_RSC_TBL];
	struct ast2600_ssp_mem *vdev0vring0 = &ssp->mem[SSP_MEM_VDEV0VRING0];
	struct ast2600_ssp_mem *vdev0vring1 = &ssp->mem[SSP_MEM_VDEV0VRING1];
	struct ast2600_ssp_mem *vdev0buffer = &ssp->mem[SSP_MEM_VDEV0BUFFER];

	mem->va = dmam_alloc_coherent(dev, mem->sz, &mem->pa, GFP_KERNEL);
	if (!mem->va)
		return -ENOMEM;

	fw->va = mem->va;
	fw->pa = mem->pa;
	fw->sz = mem->sz - shm->sz;

	shm->va = fw->va + shm->sz;
	shm->pa = fw->pa + shm->sz;

	rsc_tbl->va = shm->va;
	rsc_tbl->pa = shm->pa;

	vdev0vring0->va = rsc_tbl->va + rsc_tbl->sz;
	vdev0vring0->pa = rsc_tbl->pa + rsc_tbl->sz;

	vdev0vring1->va = vdev0vring0->va + vdev0vring0->sz;
	vdev0vring1->pa = vdev0vring0->pa + vdev0vring0->sz;

	vdev0buffer->va = vdev0vring1->va + vdev0vring1->sz;
	vdev0buffer->pa = vdev0vring1->pa + vdev0vring1->sz;

	return 0;
}

static int ast2600_ssp_probe(struct platform_device *pdev)
{
	int i, rc;
	struct rproc *rproc;
	struct ast2600_ssp *ssp;
	struct device_node *np;
	struct device *dev = &pdev->dev;

	rproc = devm_rproc_alloc(dev,
				 dev->of_node->name,
				 &ast2600_ssp_ops,
				 SSP_IMG,
				 sizeof(*ssp));
	if (!rproc) {
		dev_err(dev, "cannot allocate rproc\n");
		return -ENOMEM;
	}

	rproc->auto_boot = false;

	ssp = rproc->priv;

	ssp->rproc = rproc;
	ssp->dev = dev;

	ssp->scu = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,scu");
	if (IS_ERR(ssp->scu)) {
		dev_err(dev, "cannot map SCU\n");
		rc = -ENODEV;
	}

	np = of_parse_phandle(dev->of_node, "aspeed,cvic", 0);
	if (!np) {
		dev_err(dev, "cannot find SWVIC device node\n");
		return -ENODEV;
	}

	ssp->cvic = devm_of_iomap(dev, np, 0, NULL);
	if (IS_ERR(ssp->cvic)) {
		dev_err(dev, "cannot map SWVIC\n");
		return -EIO;
	}

	rc = of_reserved_mem_device_init(dev);
	if (rc) {
		dev_err(dev, "cannot assign reserved memory\n");
		return rc;
	}

	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np) {
		dev_err(dev, "cannot get SSP reserved memory region\n");
		return -ENODEV;
	}

	rc = of_property_read_u32(np, "size", &ssp->mem[SSP_MEM_ALL].sz);
	if (rc) {
		dev_err(dev, "cannot get SSP memory size\n");
		return rc;
	};

	rc = of_property_read_u32(np, "shm-size", &ssp->mem[SSP_MEM_SHM].sz);
	if (rc) {
		dev_err(dev, "cannot get SSP shared memory size\n");
		return rc;
	}

	rc = of_property_read_u32(np, "vdev0buffer-size",
				  &ssp->mem[SSP_MEM_VDEV0BUFFER].sz);
	if (rc) {
		dev_err(dev, "cannot get SSP vdev0buffer size\n");
		return rc;
	}

	rc = of_property_read_u32(np, "vdev0vring0-size",
				  &ssp->mem[SSP_MEM_VDEV0VRING0].sz);
	if (rc) {
		dev_err(dev, "cannot get SSP vdev0vring0 size\n");
		return rc;
	}

	rc = of_property_read_u32(np, "vdev0vring1-size",
				  &ssp->mem[SSP_MEM_VDEV0VRING1].sz);
	if (rc) {
		dev_err(dev, "cannot get SSP vdev0vring1 size\n");
		return rc;
	}

	rc = of_property_read_u32(np, "rsc-table-size",
				  &ssp->mem[SSP_MEM_RSC_TBL].sz);
	if (rc) {
		dev_err(dev, "cannot get SSP resource table size\n");
		return rc;
	}

	rc = ast2600_ssp_mem_init(ssp);
	if (rc) {
		dev_err(dev, "cannot initialize SSP memory\n");
		return rc;
	};

	for (i = 0; i < SSP_IRQ_NUM; ++i) {
		ssp->irq[i] = platform_get_irq(pdev, i);
		if (ssp->irq[i] < 0) {
			dev_err(dev, "cannot get IRQ number\n");
			return -EIO;
		}

		rc = devm_request_threaded_irq(dev, ssp->irq[i], ast2600_ssp_isr, ast2600_ssp_isr_bh, 0, "ssp-sw-irq", ssp);
		if (rc) {
			dev_err(dev, "cannot request IRQ\n");
			return rc;
		}
	}

	rc = devm_rproc_add(dev, rproc);
	if (rc) {
		dev_err(dev, "cannot add rproc\n");
		return rc;
	}

	platform_set_drvdata(pdev, ssp);

	dev_info(dev, "driver probed\n");

	return 0;
}

static int ast2600_ssp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	/* leave all to managed APIs */
	dev_info(dev, "driver removed\n");

	return 0;
}

static const struct of_device_id ast2600_ssp_of_matches[] = {
	{ .compatible = "aspeed,ast2600-ssp" },
	{ },
};

static struct platform_driver ast2600_ssp_driver = {
	.probe = ast2600_ssp_probe,
	.remove = ast2600_ssp_remove,
	.driver = {
		.name = "ast2600-ssp",
		.of_match_table = ast2600_ssp_of_matches,
	},
};

module_platform_driver(ast2600_ssp_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Aspeed AST2600 SSP control driver");
MODULE_AUTHOR("Dylan Hung <dylan_hung@aspeedtech.com>");
MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
