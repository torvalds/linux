// SPDX-License-Identifier: GPL-2.0-only
/* jazz_esp.c: ESP front-end for MIPS JAZZ systems.
 *
 * Copyright (C) 2007 Thomas Bogendörfer (tsbogend@alpha.frankende)
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <asm/jazz.h>
#include <asm/jazzdma.h>

#include <scsi/scsi_host.h>

#include "esp_scsi.h"

#define DRV_MODULE_NAME		"jazz_esp"
#define PFX DRV_MODULE_NAME	": "
#define DRV_VERSION		"1.000"
#define DRV_MODULE_RELDATE	"May 19, 2007"

static void jazz_esp_write8(struct esp *esp, u8 val, unsigned long reg)
{
	*(volatile u8 *)(esp->regs + reg) = val;
}

static u8 jazz_esp_read8(struct esp *esp, unsigned long reg)
{
	return *(volatile u8 *)(esp->regs + reg);
}

static int jazz_esp_irq_pending(struct esp *esp)
{
	if (jazz_esp_read8(esp, ESP_STATUS) & ESP_STAT_INTR)
		return 1;
	return 0;
}

static void jazz_esp_reset_dma(struct esp *esp)
{
	vdma_disable ((int)esp->dma_regs);
}

static void jazz_esp_dma_drain(struct esp *esp)
{
	/* nothing to do */
}

static void jazz_esp_dma_invalidate(struct esp *esp)
{
	vdma_disable ((int)esp->dma_regs);
}

static void jazz_esp_send_dma_cmd(struct esp *esp, u32 addr, u32 esp_count,
				  u32 dma_count, int write, u8 cmd)
{
	BUG_ON(!(cmd & ESP_CMD_DMA));

	jazz_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	jazz_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);
	vdma_disable ((int)esp->dma_regs);
	if (write)
		vdma_set_mode ((int)esp->dma_regs, DMA_MODE_READ);
	else
		vdma_set_mode ((int)esp->dma_regs, DMA_MODE_WRITE);

	vdma_set_addr ((int)esp->dma_regs, addr);
	vdma_set_count ((int)esp->dma_regs, dma_count);
	vdma_enable ((int)esp->dma_regs);

	scsi_esp_cmd(esp, cmd);
}

static int jazz_esp_dma_error(struct esp *esp)
{
	u32 enable = vdma_get_enable((int)esp->dma_regs);

	if (enable & (R4030_MEM_INTR|R4030_ADDR_INTR))
		return 1;

	return 0;
}

static const struct esp_driver_ops jazz_esp_ops = {
	.esp_write8	=	jazz_esp_write8,
	.esp_read8	=	jazz_esp_read8,
	.irq_pending	=	jazz_esp_irq_pending,
	.reset_dma	=	jazz_esp_reset_dma,
	.dma_drain	=	jazz_esp_dma_drain,
	.dma_invalidate	=	jazz_esp_dma_invalidate,
	.send_dma_cmd	=	jazz_esp_send_dma_cmd,
	.dma_error	=	jazz_esp_dma_error,
};

static int esp_jazz_probe(struct platform_device *dev)
{
	struct scsi_host_template *tpnt = &scsi_esp_template;
	struct Scsi_Host *host;
	struct esp *esp;
	struct resource *res;
	int err;

	host = scsi_host_alloc(tpnt, sizeof(struct esp));

	err = -ENOMEM;
	if (!host)
		goto fail;

	host->max_id = 8;
	esp = shost_priv(host);

	esp->host = host;
	esp->dev = &dev->dev;
	esp->ops = &jazz_esp_ops;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		goto fail_unlink;

	esp->regs = (void __iomem *)res->start;
	if (!esp->regs)
		goto fail_unlink;

	res = platform_get_resource(dev, IORESOURCE_MEM, 1);
	if (!res)
		goto fail_unlink;

	esp->dma_regs = (void __iomem *)res->start;

	esp->command_block = dma_alloc_coherent(esp->dev, 16,
						&esp->command_block_dma,
						GFP_KERNEL);
	if (!esp->command_block)
		goto fail_unmap_regs;

	host->irq = platform_get_irq(dev, 0);
	err = request_irq(host->irq, scsi_esp_intr, IRQF_SHARED, "ESP", esp);
	if (err < 0)
		goto fail_unmap_command_block;

	esp->scsi_id = 7;
	esp->host->this_id = esp->scsi_id;
	esp->scsi_id_mask = (1 << esp->scsi_id);
	esp->cfreq = 40000000;

	dev_set_drvdata(&dev->dev, esp);

	err = scsi_esp_register(esp);
	if (err)
		goto fail_free_irq;

	return 0;

fail_free_irq:
	free_irq(host->irq, esp);
fail_unmap_command_block:
	dma_free_coherent(esp->dev, 16,
			  esp->command_block,
			  esp->command_block_dma);
fail_unmap_regs:
fail_unlink:
	scsi_host_put(host);
fail:
	return err;
}

static int esp_jazz_remove(struct platform_device *dev)
{
	struct esp *esp = dev_get_drvdata(&dev->dev);
	unsigned int irq = esp->host->irq;

	scsi_esp_unregister(esp);

	free_irq(irq, esp);
	dma_free_coherent(esp->dev, 16,
			  esp->command_block,
			  esp->command_block_dma);

	scsi_host_put(esp->host);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:jazz_esp");

static struct platform_driver esp_jazz_driver = {
	.probe		= esp_jazz_probe,
	.remove		= esp_jazz_remove,
	.driver	= {
		.name	= "jazz_esp",
	},
};

static int __init jazz_esp_init(void)
{
	return platform_driver_register(&esp_jazz_driver);
}

static void __exit jazz_esp_exit(void)
{
	platform_driver_unregister(&esp_jazz_driver);
}

MODULE_DESCRIPTION("JAZZ ESP SCSI driver");
MODULE_AUTHOR("Thomas Bogendoerfer (tsbogend@alpha.franken.de)");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(jazz_esp_init);
module_exit(jazz_esp_exit);
