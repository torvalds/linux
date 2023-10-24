// SPDX-License-Identifier: GPL-2.0-only
/* sun_esp.c: ESP front-end for Sparc SBUS systems.
 *
 * Copyright (C) 2007, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/gfp.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <scsi/scsi_host.h>

#include "esp_scsi.h"

#define DRV_MODULE_NAME		"sun_esp"
#define PFX DRV_MODULE_NAME	": "
#define DRV_VERSION		"1.100"
#define DRV_MODULE_RELDATE	"August 27, 2008"

#define dma_read32(REG) \
	sbus_readl(esp->dma_regs + (REG))
#define dma_write32(VAL, REG) \
	sbus_writel((VAL), esp->dma_regs + (REG))

/* DVMA chip revisions */
enum dvma_rev {
	dvmarev0,
	dvmaesc1,
	dvmarev1,
	dvmarev2,
	dvmarev3,
	dvmarevplus,
	dvmahme
};

static int esp_sbus_setup_dma(struct esp *esp, struct platform_device *dma_of)
{
	esp->dma = dma_of;

	esp->dma_regs = of_ioremap(&dma_of->resource[0], 0,
				   resource_size(&dma_of->resource[0]),
				   "espdma");
	if (!esp->dma_regs)
		return -ENOMEM;

	switch (dma_read32(DMA_CSR) & DMA_DEVICE_ID) {
	case DMA_VERS0:
		esp->dmarev = dvmarev0;
		break;
	case DMA_ESCV1:
		esp->dmarev = dvmaesc1;
		break;
	case DMA_VERS1:
		esp->dmarev = dvmarev1;
		break;
	case DMA_VERS2:
		esp->dmarev = dvmarev2;
		break;
	case DMA_VERHME:
		esp->dmarev = dvmahme;
		break;
	case DMA_VERSPLUS:
		esp->dmarev = dvmarevplus;
		break;
	}

	return 0;

}

static int esp_sbus_map_regs(struct esp *esp, int hme)
{
	struct platform_device *op = to_platform_device(esp->dev);
	struct resource *res;

	/* On HME, two reg sets exist, first is DVMA,
	 * second is ESP registers.
	 */
	if (hme)
		res = &op->resource[1];
	else
		res = &op->resource[0];

	esp->regs = of_ioremap(res, 0, SBUS_ESP_REG_SIZE, "ESP");
	if (!esp->regs)
		return -ENOMEM;

	return 0;
}

static int esp_sbus_map_command_block(struct esp *esp)
{
	esp->command_block = dma_alloc_coherent(esp->dev, 16,
						&esp->command_block_dma,
						GFP_KERNEL);
	if (!esp->command_block)
		return -ENOMEM;
	return 0;
}

static int esp_sbus_register_irq(struct esp *esp)
{
	struct Scsi_Host *host = esp->host;
	struct platform_device *op = to_platform_device(esp->dev);

	host->irq = op->archdata.irqs[0];
	return request_irq(host->irq, scsi_esp_intr, IRQF_SHARED, "ESP", esp);
}

static void esp_get_scsi_id(struct esp *esp, struct platform_device *espdma)
{
	struct platform_device *op = to_platform_device(esp->dev);
	struct device_node *dp;

	dp = op->dev.of_node;
	esp->scsi_id = of_getintprop_default(dp, "initiator-id", 0xff);
	if (esp->scsi_id != 0xff)
		goto done;

	esp->scsi_id = of_getintprop_default(dp, "scsi-initiator-id", 0xff);
	if (esp->scsi_id != 0xff)
		goto done;

	esp->scsi_id = of_getintprop_default(espdma->dev.of_node,
					     "scsi-initiator-id", 7);

done:
	esp->host->this_id = esp->scsi_id;
	esp->scsi_id_mask = (1 << esp->scsi_id);
}

static void esp_get_differential(struct esp *esp)
{
	struct platform_device *op = to_platform_device(esp->dev);
	struct device_node *dp;

	dp = op->dev.of_node;
	if (of_property_read_bool(dp, "differential"))
		esp->flags |= ESP_FLAG_DIFFERENTIAL;
	else
		esp->flags &= ~ESP_FLAG_DIFFERENTIAL;
}

static void esp_get_clock_params(struct esp *esp)
{
	struct platform_device *op = to_platform_device(esp->dev);
	struct device_node *bus_dp, *dp;
	int fmhz;

	dp = op->dev.of_node;
	bus_dp = dp->parent;

	fmhz = of_getintprop_default(dp, "clock-frequency", 0);
	if (fmhz == 0)
		fmhz = of_getintprop_default(bus_dp, "clock-frequency", 0);

	esp->cfreq = fmhz;
}

static void esp_get_bursts(struct esp *esp, struct platform_device *dma_of)
{
	struct device_node *dma_dp = dma_of->dev.of_node;
	struct platform_device *op = to_platform_device(esp->dev);
	struct device_node *dp;
	u8 bursts, val;

	dp = op->dev.of_node;
	bursts = of_getintprop_default(dp, "burst-sizes", 0xff);
	val = of_getintprop_default(dma_dp, "burst-sizes", 0xff);
	if (val != 0xff)
		bursts &= val;

	val = of_getintprop_default(dma_dp->parent, "burst-sizes", 0xff);
	if (val != 0xff)
		bursts &= val;

	if (bursts == 0xff ||
	    (bursts & DMA_BURST16) == 0 ||
	    (bursts & DMA_BURST32) == 0)
		bursts = (DMA_BURST32 - 1);

	esp->bursts = bursts;
}

static void esp_sbus_get_props(struct esp *esp, struct platform_device *espdma)
{
	esp_get_scsi_id(esp, espdma);
	esp_get_differential(esp);
	esp_get_clock_params(esp);
	esp_get_bursts(esp, espdma);
}

static void sbus_esp_write8(struct esp *esp, u8 val, unsigned long reg)
{
	sbus_writeb(val, esp->regs + (reg * 4UL));
}

static u8 sbus_esp_read8(struct esp *esp, unsigned long reg)
{
	return sbus_readb(esp->regs + (reg * 4UL));
}

static int sbus_esp_irq_pending(struct esp *esp)
{
	if (dma_read32(DMA_CSR) & (DMA_HNDL_INTR | DMA_HNDL_ERROR))
		return 1;
	return 0;
}

static void sbus_esp_reset_dma(struct esp *esp)
{
	int can_do_burst16, can_do_burst32, can_do_burst64;
	int can_do_sbus64, lim;
	struct platform_device *op = to_platform_device(esp->dev);
	u32 val;

	can_do_burst16 = (esp->bursts & DMA_BURST16) != 0;
	can_do_burst32 = (esp->bursts & DMA_BURST32) != 0;
	can_do_burst64 = 0;
	can_do_sbus64 = 0;
	if (sbus_can_dma_64bit())
		can_do_sbus64 = 1;
	if (sbus_can_burst64())
		can_do_burst64 = (esp->bursts & DMA_BURST64) != 0;

	/* Put the DVMA into a known state. */
	if (esp->dmarev != dvmahme) {
		val = dma_read32(DMA_CSR);
		dma_write32(val | DMA_RST_SCSI, DMA_CSR);
		dma_write32(val & ~DMA_RST_SCSI, DMA_CSR);
	}
	switch (esp->dmarev) {
	case dvmahme:
		dma_write32(DMA_RESET_FAS366, DMA_CSR);
		dma_write32(DMA_RST_SCSI, DMA_CSR);

		esp->prev_hme_dmacsr = (DMA_PARITY_OFF | DMA_2CLKS |
					DMA_SCSI_DISAB | DMA_INT_ENAB);

		esp->prev_hme_dmacsr &= ~(DMA_ENABLE | DMA_ST_WRITE |
					  DMA_BRST_SZ);

		if (can_do_burst64)
			esp->prev_hme_dmacsr |= DMA_BRST64;
		else if (can_do_burst32)
			esp->prev_hme_dmacsr |= DMA_BRST32;

		if (can_do_sbus64) {
			esp->prev_hme_dmacsr |= DMA_SCSI_SBUS64;
			sbus_set_sbus64(&op->dev, esp->bursts);
		}

		lim = 1000;
		while (dma_read32(DMA_CSR) & DMA_PEND_READ) {
			if (--lim == 0) {
				printk(KERN_ALERT PFX "esp%d: DMA_PEND_READ "
				       "will not clear!\n",
				       esp->host->unique_id);
				break;
			}
			udelay(1);
		}

		dma_write32(0, DMA_CSR);
		dma_write32(esp->prev_hme_dmacsr, DMA_CSR);

		dma_write32(0, DMA_ADDR);
		break;

	case dvmarev2:
		if (esp->rev != ESP100) {
			val = dma_read32(DMA_CSR);
			dma_write32(val | DMA_3CLKS, DMA_CSR);
		}
		break;

	case dvmarev3:
		val = dma_read32(DMA_CSR);
		val &= ~DMA_3CLKS;
		val |= DMA_2CLKS;
		if (can_do_burst32) {
			val &= ~DMA_BRST_SZ;
			val |= DMA_BRST32;
		}
		dma_write32(val, DMA_CSR);
		break;

	case dvmaesc1:
		val = dma_read32(DMA_CSR);
		val |= DMA_ADD_ENABLE;
		val &= ~DMA_BCNT_ENAB;
		if (!can_do_burst32 && can_do_burst16) {
			val |= DMA_ESC_BURST;
		} else {
			val &= ~(DMA_ESC_BURST);
		}
		dma_write32(val, DMA_CSR);
		break;

	default:
		break;
	}

	/* Enable interrupts.  */
	val = dma_read32(DMA_CSR);
	dma_write32(val | DMA_INT_ENAB, DMA_CSR);
}

static void sbus_esp_dma_drain(struct esp *esp)
{
	u32 csr;
	int lim;

	if (esp->dmarev == dvmahme)
		return;

	csr = dma_read32(DMA_CSR);
	if (!(csr & DMA_FIFO_ISDRAIN))
		return;

	if (esp->dmarev != dvmarev3 && esp->dmarev != dvmaesc1)
		dma_write32(csr | DMA_FIFO_STDRAIN, DMA_CSR);

	lim = 1000;
	while (dma_read32(DMA_CSR) & DMA_FIFO_ISDRAIN) {
		if (--lim == 0) {
			printk(KERN_ALERT PFX "esp%d: DMA will not drain!\n",
			       esp->host->unique_id);
			break;
		}
		udelay(1);
	}
}

static void sbus_esp_dma_invalidate(struct esp *esp)
{
	if (esp->dmarev == dvmahme) {
		dma_write32(DMA_RST_SCSI, DMA_CSR);

		esp->prev_hme_dmacsr = ((esp->prev_hme_dmacsr |
					 (DMA_PARITY_OFF | DMA_2CLKS |
					  DMA_SCSI_DISAB | DMA_INT_ENAB)) &
					~(DMA_ST_WRITE | DMA_ENABLE));

		dma_write32(0, DMA_CSR);
		dma_write32(esp->prev_hme_dmacsr, DMA_CSR);

		/* This is necessary to avoid having the SCSI channel
		 * engine lock up on us.
		 */
		dma_write32(0, DMA_ADDR);
	} else {
		u32 val;
		int lim;

		lim = 1000;
		while ((val = dma_read32(DMA_CSR)) & DMA_PEND_READ) {
			if (--lim == 0) {
				printk(KERN_ALERT PFX "esp%d: DMA will not "
				       "invalidate!\n", esp->host->unique_id);
				break;
			}
			udelay(1);
		}

		val &= ~(DMA_ENABLE | DMA_ST_WRITE | DMA_BCNT_ENAB);
		val |= DMA_FIFO_INV;
		dma_write32(val, DMA_CSR);
		val &= ~DMA_FIFO_INV;
		dma_write32(val, DMA_CSR);
	}
}

static void sbus_esp_send_dma_cmd(struct esp *esp, u32 addr, u32 esp_count,
				  u32 dma_count, int write, u8 cmd)
{
	u32 csr;

	BUG_ON(!(cmd & ESP_CMD_DMA));

	sbus_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	sbus_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);
	if (esp->rev == FASHME) {
		sbus_esp_write8(esp, (esp_count >> 16) & 0xff, FAS_RLO);
		sbus_esp_write8(esp, 0, FAS_RHI);

		scsi_esp_cmd(esp, cmd);

		csr = esp->prev_hme_dmacsr;
		csr |= DMA_SCSI_DISAB | DMA_ENABLE;
		if (write)
			csr |= DMA_ST_WRITE;
		else
			csr &= ~DMA_ST_WRITE;
		esp->prev_hme_dmacsr = csr;

		dma_write32(dma_count, DMA_COUNT);
		dma_write32(addr, DMA_ADDR);
		dma_write32(csr, DMA_CSR);
	} else {
		csr = dma_read32(DMA_CSR);
		csr |= DMA_ENABLE;
		if (write)
			csr |= DMA_ST_WRITE;
		else
			csr &= ~DMA_ST_WRITE;
		dma_write32(csr, DMA_CSR);
		if (esp->dmarev == dvmaesc1) {
			u32 end = PAGE_ALIGN(addr + dma_count + 16U);
			dma_write32(end - addr, DMA_COUNT);
		}
		dma_write32(addr, DMA_ADDR);

		scsi_esp_cmd(esp, cmd);
	}

}

static int sbus_esp_dma_error(struct esp *esp)
{
	u32 csr = dma_read32(DMA_CSR);

	if (csr & DMA_HNDL_ERROR)
		return 1;

	return 0;
}

static const struct esp_driver_ops sbus_esp_ops = {
	.esp_write8	=	sbus_esp_write8,
	.esp_read8	=	sbus_esp_read8,
	.irq_pending	=	sbus_esp_irq_pending,
	.reset_dma	=	sbus_esp_reset_dma,
	.dma_drain	=	sbus_esp_dma_drain,
	.dma_invalidate	=	sbus_esp_dma_invalidate,
	.send_dma_cmd	=	sbus_esp_send_dma_cmd,
	.dma_error	=	sbus_esp_dma_error,
};

static int esp_sbus_probe_one(struct platform_device *op,
			      struct platform_device *espdma, int hme)
{
	const struct scsi_host_template *tpnt = &scsi_esp_template;
	struct Scsi_Host *host;
	struct esp *esp;
	int err;

	host = scsi_host_alloc(tpnt, sizeof(struct esp));

	err = -ENOMEM;
	if (!host)
		goto fail;

	host->max_id = (hme ? 16 : 8);
	esp = shost_priv(host);

	esp->host = host;
	esp->dev = &op->dev;
	esp->ops = &sbus_esp_ops;

	if (hme)
		esp->flags |= ESP_FLAG_WIDE_CAPABLE;

	err = esp_sbus_setup_dma(esp, espdma);
	if (err < 0)
		goto fail_unlink;

	err = esp_sbus_map_regs(esp, hme);
	if (err < 0)
		goto fail_unlink;

	err = esp_sbus_map_command_block(esp);
	if (err < 0)
		goto fail_unmap_regs;

	err = esp_sbus_register_irq(esp);
	if (err < 0)
		goto fail_unmap_command_block;

	esp_sbus_get_props(esp, espdma);

	/* Before we try to touch the ESP chip, ESC1 dma can
	 * come up with the reset bit set, so make sure that
	 * is clear first.
	 */
	if (esp->dmarev == dvmaesc1) {
		u32 val = dma_read32(DMA_CSR);

		dma_write32(val & ~DMA_RST_SCSI, DMA_CSR);
	}

	dev_set_drvdata(&op->dev, esp);

	err = scsi_esp_register(esp);
	if (err)
		goto fail_free_irq;

	return 0;

fail_free_irq:
	free_irq(host->irq, esp);
fail_unmap_command_block:
	dma_free_coherent(&op->dev, 16,
			  esp->command_block,
			  esp->command_block_dma);
fail_unmap_regs:
	of_iounmap(&op->resource[(hme ? 1 : 0)], esp->regs, SBUS_ESP_REG_SIZE);
fail_unlink:
	scsi_host_put(host);
fail:
	return err;
}

static int esp_sbus_probe(struct platform_device *op)
{
	struct device_node *dma_node = NULL;
	struct device_node *dp = op->dev.of_node;
	struct platform_device *dma_of = NULL;
	int hme = 0;
	int ret;

	if (of_node_name_eq(dp->parent, "espdma") ||
	    of_node_name_eq(dp->parent, "dma"))
		dma_node = dp->parent;
	else if (of_node_name_eq(dp, "SUNW,fas")) {
		dma_node = op->dev.of_node;
		hme = 1;
	}
	if (dma_node)
		dma_of = of_find_device_by_node(dma_node);
	if (!dma_of)
		return -ENODEV;

	ret = esp_sbus_probe_one(op, dma_of, hme);
	if (ret)
		put_device(&dma_of->dev);

	return ret;
}

static int esp_sbus_remove(struct platform_device *op)
{
	struct esp *esp = dev_get_drvdata(&op->dev);
	struct platform_device *dma_of = esp->dma;
	unsigned int irq = esp->host->irq;
	bool is_hme;
	u32 val;

	scsi_esp_unregister(esp);

	/* Disable interrupts.  */
	val = dma_read32(DMA_CSR);
	dma_write32(val & ~DMA_INT_ENAB, DMA_CSR);

	free_irq(irq, esp);

	is_hme = (esp->dmarev == dvmahme);

	dma_free_coherent(&op->dev, 16,
			  esp->command_block,
			  esp->command_block_dma);
	of_iounmap(&op->resource[(is_hme ? 1 : 0)], esp->regs,
		   SBUS_ESP_REG_SIZE);
	of_iounmap(&dma_of->resource[0], esp->dma_regs,
		   resource_size(&dma_of->resource[0]));

	scsi_host_put(esp->host);

	dev_set_drvdata(&op->dev, NULL);

	put_device(&dma_of->dev);

	return 0;
}

static const struct of_device_id esp_match[] = {
	{
		.name = "SUNW,esp",
	},
	{
		.name = "SUNW,fas",
	},
	{
		.name = "esp",
	},
	{},
};
MODULE_DEVICE_TABLE(of, esp_match);

static struct platform_driver esp_sbus_driver = {
	.driver = {
		.name = "esp",
		.of_match_table = esp_match,
	},
	.probe		= esp_sbus_probe,
	.remove		= esp_sbus_remove,
};
module_platform_driver(esp_sbus_driver);

MODULE_DESCRIPTION("Sun ESP SCSI driver");
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
