/* sun3x_esp.c: ESP front-end for Sun3x systems.
 *
 * Copyright (C) 2007,2008 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <asm/sun3x.h>
#include <asm/dma.h>
#include <asm/dvma.h>

/* DMA controller reg offsets */
#define DMA_CSR		0x00UL	/* rw  DMA control/status register    0x00   */
#define DMA_ADDR        0x04UL	/* rw  DMA transfer address register  0x04   */
#define DMA_COUNT       0x08UL	/* rw  DMA transfer count register    0x08   */
#define DMA_TEST        0x0cUL	/* rw  DMA test/debug register        0x0c   */

#include <scsi/scsi_host.h>

#include "esp_scsi.h"

#define DRV_MODULE_NAME		"sun3x_esp"
#define PFX DRV_MODULE_NAME	": "
#define DRV_VERSION		"1.000"
#define DRV_MODULE_RELDATE	"Nov 1, 2007"

/*
 * m68k always assumes readl/writel operate on little endian
 * mmio space; this is wrong at least for Sun3x, so we
 * need to workaround this until a proper way is found
 */
#if 0
#define dma_read32(REG) \
	readl(esp->dma_regs + (REG))
#define dma_write32(VAL, REG) \
	writel((VAL), esp->dma_regs + (REG))
#else
#define dma_read32(REG) \
	*(volatile u32 *)(esp->dma_regs + (REG))
#define dma_write32(VAL, REG) \
	do { *(volatile u32 *)(esp->dma_regs + (REG)) = (VAL); } while (0)
#endif

static void sun3x_esp_write8(struct esp *esp, u8 val, unsigned long reg)
{
	writeb(val, esp->regs + (reg * 4UL));
}

static u8 sun3x_esp_read8(struct esp *esp, unsigned long reg)
{
	return readb(esp->regs + (reg * 4UL));
}

static dma_addr_t sun3x_esp_map_single(struct esp *esp, void *buf,
				      size_t sz, int dir)
{
	return dma_map_single(esp->dev, buf, sz, dir);
}

static int sun3x_esp_map_sg(struct esp *esp, struct scatterlist *sg,
				  int num_sg, int dir)
{
	return dma_map_sg(esp->dev, sg, num_sg, dir);
}

static void sun3x_esp_unmap_single(struct esp *esp, dma_addr_t addr,
				  size_t sz, int dir)
{
	dma_unmap_single(esp->dev, addr, sz, dir);
}

static void sun3x_esp_unmap_sg(struct esp *esp, struct scatterlist *sg,
			      int num_sg, int dir)
{
	dma_unmap_sg(esp->dev, sg, num_sg, dir);
}

static int sun3x_esp_irq_pending(struct esp *esp)
{
	if (dma_read32(DMA_CSR) & (DMA_HNDL_INTR | DMA_HNDL_ERROR))
		return 1;
	return 0;
}

static void sun3x_esp_reset_dma(struct esp *esp)
{
	u32 val;

	val = dma_read32(DMA_CSR);
	dma_write32(val | DMA_RST_SCSI, DMA_CSR);
	dma_write32(val & ~DMA_RST_SCSI, DMA_CSR);

	/* Enable interrupts.  */
	val = dma_read32(DMA_CSR);
	dma_write32(val | DMA_INT_ENAB, DMA_CSR);
}

static void sun3x_esp_dma_drain(struct esp *esp)
{
	u32 csr;
	int lim;

	csr = dma_read32(DMA_CSR);
	if (!(csr & DMA_FIFO_ISDRAIN))
		return;

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

static void sun3x_esp_dma_invalidate(struct esp *esp)
{
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

static void sun3x_esp_send_dma_cmd(struct esp *esp, u32 addr, u32 esp_count,
				  u32 dma_count, int write, u8 cmd)
{
	u32 csr;

	BUG_ON(!(cmd & ESP_CMD_DMA));

	sun3x_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	sun3x_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);
	csr = dma_read32(DMA_CSR);
	csr |= DMA_ENABLE;
	if (write)
		csr |= DMA_ST_WRITE;
	else
		csr &= ~DMA_ST_WRITE;
	dma_write32(csr, DMA_CSR);
	dma_write32(addr, DMA_ADDR);

	scsi_esp_cmd(esp, cmd);
}

static int sun3x_esp_dma_error(struct esp *esp)
{
	u32 csr = dma_read32(DMA_CSR);

	if (csr & DMA_HNDL_ERROR)
		return 1;

	return 0;
}

static const struct esp_driver_ops sun3x_esp_ops = {
	.esp_write8	=	sun3x_esp_write8,
	.esp_read8	=	sun3x_esp_read8,
	.map_single	=	sun3x_esp_map_single,
	.map_sg		=	sun3x_esp_map_sg,
	.unmap_single	=	sun3x_esp_unmap_single,
	.unmap_sg	=	sun3x_esp_unmap_sg,
	.irq_pending	=	sun3x_esp_irq_pending,
	.reset_dma	=	sun3x_esp_reset_dma,
	.dma_drain	=	sun3x_esp_dma_drain,
	.dma_invalidate	=	sun3x_esp_dma_invalidate,
	.send_dma_cmd	=	sun3x_esp_send_dma_cmd,
	.dma_error	=	sun3x_esp_dma_error,
};

static int esp_sun3x_probe(struct platform_device *dev)
{
	struct scsi_host_template *tpnt = &scsi_esp_template;
	struct Scsi_Host *host;
	struct esp *esp;
	struct resource *res;
	int err = -ENOMEM;

	host = scsi_host_alloc(tpnt, sizeof(struct esp));
	if (!host)
		goto fail;

	host->max_id = 8;
	esp = shost_priv(host);

	esp->host = host;
	esp->dev = dev;
	esp->ops = &sun3x_esp_ops;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res || !res->start)
		goto fail_unlink;

	esp->regs = ioremap_nocache(res->start, 0x20);
	if (!esp->regs)
		goto fail_unmap_regs;

	res = platform_get_resource(dev, IORESOURCE_MEM, 1);
	if (!res || !res->start)
		goto fail_unmap_regs;

	esp->dma_regs = ioremap_nocache(res->start, 0x10);

	esp->command_block = dma_alloc_coherent(esp->dev, 16,
						&esp->command_block_dma,
						GFP_KERNEL);
	if (!esp->command_block)
		goto fail_unmap_regs_dma;

	host->irq = platform_get_irq(dev, 0);
	err = request_irq(host->irq, scsi_esp_intr, IRQF_SHARED,
			  "SUN3X ESP", esp);
	if (err < 0)
		goto fail_unmap_command_block;

	esp->scsi_id = 7;
	esp->host->this_id = esp->scsi_id;
	esp->scsi_id_mask = (1 << esp->scsi_id);
	esp->cfreq = 20000000;

	dev_set_drvdata(&dev->dev, esp);

	err = scsi_esp_register(esp, &dev->dev);
	if (err)
		goto fail_free_irq;

	return 0;

fail_free_irq:
	free_irq(host->irq, esp);
fail_unmap_command_block:
	dma_free_coherent(esp->dev, 16,
			  esp->command_block,
			  esp->command_block_dma);
fail_unmap_regs_dma:
	iounmap(esp->dma_regs);
fail_unmap_regs:
	iounmap(esp->regs);
fail_unlink:
	scsi_host_put(host);
fail:
	return err;
}

static int esp_sun3x_remove(struct platform_device *dev)
{
	struct esp *esp = dev_get_drvdata(&dev->dev);
	unsigned int irq = esp->host->irq;
	u32 val;

	scsi_esp_unregister(esp);

	/* Disable interrupts.  */
	val = dma_read32(DMA_CSR);
	dma_write32(val & ~DMA_INT_ENAB, DMA_CSR);

	free_irq(irq, esp);
	dma_free_coherent(esp->dev, 16,
			  esp->command_block,
			  esp->command_block_dma);

	scsi_host_put(esp->host);

	return 0;
}

static struct platform_driver esp_sun3x_driver = {
	.probe          = esp_sun3x_probe,
	.remove         = esp_sun3x_remove,
	.driver = {
		.name   = "sun3x_esp",
	},
};

static int __init sun3x_esp_init(void)
{
	return platform_driver_register(&esp_sun3x_driver);
}

static void __exit sun3x_esp_exit(void)
{
	platform_driver_unregister(&esp_sun3x_driver);
}

MODULE_DESCRIPTION("Sun3x ESP SCSI driver");
MODULE_AUTHOR("Thomas Bogendoerfer (tsbogend@alpha.franken.de)");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(sun3x_esp_init);
module_exit(sun3x_esp_exit);
MODULE_ALIAS("platform:sun3x_esp");
