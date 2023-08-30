// SPDX-License-Identifier: GPL-2.0-only
/* mac_esp.c: ESP front-end for Macintosh Quadra systems.
 *
 * Adapted from jazz_esp.c and the old mac_esp.c.
 *
 * The pseudo DMA algorithm is based on the one used in NetBSD.
 * See sys/arch/mac68k/obio/esp.c for some background information.
 *
 * Copyright (C) 2007-2008 Finn Thain
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/nubus.h>
#include <linux/slab.h>

#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/macints.h>
#include <asm/macintosh.h>
#include <asm/mac_via.h>

#include <scsi/scsi_host.h>

#include "esp_scsi.h"

#define DRV_MODULE_NAME     "mac_esp"
#define PFX                 DRV_MODULE_NAME ": "
#define DRV_VERSION         "1.000"
#define DRV_MODULE_RELDATE  "Sept 15, 2007"

#define MAC_ESP_IO_BASE          0x50F00000
#define MAC_ESP_REGS_QUADRA      (MAC_ESP_IO_BASE + 0x10000)
#define MAC_ESP_REGS_QUADRA2     (MAC_ESP_IO_BASE + 0xF000)
#define MAC_ESP_REGS_QUADRA3     (MAC_ESP_IO_BASE + 0x18000)
#define MAC_ESP_REGS_SPACING     0x402
#define MAC_ESP_PDMA_REG         0xF9800024
#define MAC_ESP_PDMA_REG_SPACING 0x4
#define MAC_ESP_PDMA_IO_OFFSET   0x100

#define esp_read8(REG)		mac_esp_read8(esp, REG)
#define esp_write8(VAL, REG)	mac_esp_write8(esp, VAL, REG)

struct mac_esp_priv {
	struct esp *esp;
	void __iomem *pdma_regs;
	void __iomem *pdma_io;
};
static struct esp *esp_chips[2];
static DEFINE_SPINLOCK(esp_chips_lock);

#define MAC_ESP_GET_PRIV(esp) ((struct mac_esp_priv *) \
			       dev_get_drvdata((esp)->dev))

static inline void mac_esp_write8(struct esp *esp, u8 val, unsigned long reg)
{
	nubus_writeb(val, esp->regs + reg * 16);
}

static inline u8 mac_esp_read8(struct esp *esp, unsigned long reg)
{
	return nubus_readb(esp->regs + reg * 16);
}

static void mac_esp_reset_dma(struct esp *esp)
{
	/* Nothing to do. */
}

static void mac_esp_dma_drain(struct esp *esp)
{
	/* Nothing to do. */
}

static void mac_esp_dma_invalidate(struct esp *esp)
{
	/* Nothing to do. */
}

static int mac_esp_dma_error(struct esp *esp)
{
	return esp->send_cmd_error;
}

static inline int mac_esp_wait_for_empty_fifo(struct esp *esp)
{
	int i = 500000;

	do {
		if (!(esp_read8(ESP_FFLAGS) & ESP_FF_FBYTES))
			return 0;

		if (esp_read8(ESP_STATUS) & ESP_STAT_INTR)
			return 1;

		udelay(2);
	} while (--i);

	printk(KERN_ERR PFX "FIFO is not empty (sreg %02x)\n",
	       esp_read8(ESP_STATUS));
	esp->send_cmd_error = 1;
	return 1;
}

static inline int mac_esp_wait_for_dreq(struct esp *esp)
{
	struct mac_esp_priv *mep = MAC_ESP_GET_PRIV(esp);
	int i = 500000;

	do {
		if (mep->pdma_regs == NULL) {
			if (via2_scsi_drq_pending())
				return 0;
		} else {
			if (nubus_readl(mep->pdma_regs) & 0x200)
				return 0;
		}

		if (esp_read8(ESP_STATUS) & ESP_STAT_INTR)
			return 1;

		udelay(2);
	} while (--i);

	printk(KERN_ERR PFX "PDMA timeout (sreg %02x)\n",
	       esp_read8(ESP_STATUS));
	esp->send_cmd_error = 1;
	return 1;
}

#define MAC_ESP_PDMA_LOOP(operands) \
	asm volatile ( \
	     "       tstw %1                   \n" \
	     "       jbeq 20f                  \n" \
	     "1:     movew " operands "        \n" \
	     "2:     movew " operands "        \n" \
	     "3:     movew " operands "        \n" \
	     "4:     movew " operands "        \n" \
	     "5:     movew " operands "        \n" \
	     "6:     movew " operands "        \n" \
	     "7:     movew " operands "        \n" \
	     "8:     movew " operands "        \n" \
	     "9:     movew " operands "        \n" \
	     "10:    movew " operands "        \n" \
	     "11:    movew " operands "        \n" \
	     "12:    movew " operands "        \n" \
	     "13:    movew " operands "        \n" \
	     "14:    movew " operands "        \n" \
	     "15:    movew " operands "        \n" \
	     "16:    movew " operands "        \n" \
	     "       subqw #1,%1               \n" \
	     "       jbne 1b                   \n" \
	     "20:    tstw %2                   \n" \
	     "       jbeq 30f                  \n" \
	     "21:    movew " operands "        \n" \
	     "       subqw #1,%2               \n" \
	     "       jbne 21b                  \n" \
	     "30:    tstw %3                   \n" \
	     "       jbeq 40f                  \n" \
	     "31:    moveb " operands "        \n" \
	     "32:    nop                       \n" \
	     "40:                              \n" \
	     "                                 \n" \
	     "       .section __ex_table,\"a\" \n" \
	     "       .align  4                 \n" \
	     "       .long   1b,40b            \n" \
	     "       .long   2b,40b            \n" \
	     "       .long   3b,40b            \n" \
	     "       .long   4b,40b            \n" \
	     "       .long   5b,40b            \n" \
	     "       .long   6b,40b            \n" \
	     "       .long   7b,40b            \n" \
	     "       .long   8b,40b            \n" \
	     "       .long   9b,40b            \n" \
	     "       .long  10b,40b            \n" \
	     "       .long  11b,40b            \n" \
	     "       .long  12b,40b            \n" \
	     "       .long  13b,40b            \n" \
	     "       .long  14b,40b            \n" \
	     "       .long  15b,40b            \n" \
	     "       .long  16b,40b            \n" \
	     "       .long  21b,40b            \n" \
	     "       .long  31b,40b            \n" \
	     "       .long  32b,40b            \n" \
	     "       .previous                 \n" \
	     : "+a" (addr), "+r" (count32), "+r" (count2) \
	     : "g" (count1), "a" (mep->pdma_io))

static void mac_esp_send_pdma_cmd(struct esp *esp, u32 addr, u32 esp_count,
				  u32 dma_count, int write, u8 cmd)
{
	struct mac_esp_priv *mep = MAC_ESP_GET_PRIV(esp);

	esp->send_cmd_error = 0;

	if (!write)
		scsi_esp_cmd(esp, ESP_CMD_FLUSH);

	esp_write8((esp_count >> 0) & 0xFF, ESP_TCLOW);
	esp_write8((esp_count >> 8) & 0xFF, ESP_TCMED);

	scsi_esp_cmd(esp, cmd);

	do {
		unsigned int count32 = esp_count >> 5;
		unsigned int count2 = (esp_count & 0x1F) >> 1;
		unsigned int count1 = esp_count & 1;
		unsigned int start_addr = addr;

		if (mac_esp_wait_for_dreq(esp))
			break;

		if (write) {
			MAC_ESP_PDMA_LOOP("%4@,%0@+");

			esp_count -= addr - start_addr;
		} else {
			unsigned int n;

			MAC_ESP_PDMA_LOOP("%0@+,%4@");

			if (mac_esp_wait_for_empty_fifo(esp))
				break;

			n = (esp_read8(ESP_TCMED) << 8) + esp_read8(ESP_TCLOW);
			addr = start_addr + esp_count - n;
			esp_count = n;
		}
	} while (esp_count);
}

static int mac_esp_irq_pending(struct esp *esp)
{
	if (esp_read8(ESP_STATUS) & ESP_STAT_INTR)
		return 1;
	return 0;
}

static u32 mac_esp_dma_length_limit(struct esp *esp, u32 dma_addr, u32 dma_len)
{
	return dma_len > 0xFFFF ? 0xFFFF : dma_len;
}

static irqreturn_t mac_scsi_esp_intr(int irq, void *dev_id)
{
	int got_intr;

	/*
	 * This is an edge triggered IRQ, so we have to be careful to
	 * avoid missing a transition when it is shared by two ESP devices.
	 */

	do {
		got_intr = 0;
		if (esp_chips[0] &&
		    (mac_esp_read8(esp_chips[0], ESP_STATUS) & ESP_STAT_INTR)) {
			(void)scsi_esp_intr(irq, esp_chips[0]);
			got_intr = 1;
		}
		if (esp_chips[1] &&
		    (mac_esp_read8(esp_chips[1], ESP_STATUS) & ESP_STAT_INTR)) {
			(void)scsi_esp_intr(irq, esp_chips[1]);
			got_intr = 1;
		}
	} while (got_intr);

	return IRQ_HANDLED;
}

static struct esp_driver_ops mac_esp_ops = {
	.esp_write8       = mac_esp_write8,
	.esp_read8        = mac_esp_read8,
	.irq_pending      = mac_esp_irq_pending,
	.dma_length_limit = mac_esp_dma_length_limit,
	.reset_dma        = mac_esp_reset_dma,
	.dma_drain        = mac_esp_dma_drain,
	.dma_invalidate   = mac_esp_dma_invalidate,
	.send_dma_cmd     = mac_esp_send_pdma_cmd,
	.dma_error        = mac_esp_dma_error,
};

static int esp_mac_probe(struct platform_device *dev)
{
	const struct scsi_host_template *tpnt = &scsi_esp_template;
	struct Scsi_Host *host;
	struct esp *esp;
	int err;
	struct mac_esp_priv *mep;

	if (!MACH_IS_MAC)
		return -ENODEV;

	if (dev->id > 1)
		return -ENODEV;

	host = scsi_host_alloc(tpnt, sizeof(struct esp));

	err = -ENOMEM;
	if (!host)
		goto fail;

	host->max_id = 8;
	host->dma_boundary = PAGE_SIZE - 1;
	esp = shost_priv(host);

	esp->host = host;
	esp->dev = &dev->dev;

	esp->command_block = kzalloc(16, GFP_KERNEL);
	if (!esp->command_block)
		goto fail_unlink;
	esp->command_block_dma = (dma_addr_t)esp->command_block;

	esp->scsi_id = 7;
	host->this_id = esp->scsi_id;
	esp->scsi_id_mask = 1 << esp->scsi_id;

	mep = kzalloc(sizeof(struct mac_esp_priv), GFP_KERNEL);
	if (!mep)
		goto fail_free_command_block;
	mep->esp = esp;
	platform_set_drvdata(dev, mep);

	switch (macintosh_config->scsi_type) {
	case MAC_SCSI_QUADRA:
		esp->cfreq     = 16500000;
		esp->regs      = (void __iomem *)MAC_ESP_REGS_QUADRA;
		mep->pdma_io   = esp->regs + MAC_ESP_PDMA_IO_OFFSET;
		mep->pdma_regs = NULL;
		break;
	case MAC_SCSI_QUADRA2:
		esp->cfreq     = 25000000;
		esp->regs      = (void __iomem *)(MAC_ESP_REGS_QUADRA2 +
				 dev->id * MAC_ESP_REGS_SPACING);
		mep->pdma_io   = esp->regs + MAC_ESP_PDMA_IO_OFFSET;
		mep->pdma_regs = (void __iomem *)(MAC_ESP_PDMA_REG +
				 dev->id * MAC_ESP_PDMA_REG_SPACING);
		nubus_writel(0x1d1, mep->pdma_regs);
		break;
	case MAC_SCSI_QUADRA3:
		/* These quadras have a real DMA controller (the PSC) but we
		 * don't know how to drive it so we must use PIO instead.
		 */
		esp->cfreq     = 25000000;
		esp->regs      = (void __iomem *)MAC_ESP_REGS_QUADRA3;
		mep->pdma_io   = NULL;
		mep->pdma_regs = NULL;
		break;
	}
	esp->fifo_reg = esp->regs + ESP_FDATA * 16;

	esp->ops = &mac_esp_ops;
	esp->flags = ESP_FLAG_NO_DMA_MAP;
	if (mep->pdma_io == NULL) {
		printk(KERN_INFO PFX "using PIO for controller %d\n", dev->id);
		esp_write8(0, ESP_TCLOW);
		esp_write8(0, ESP_TCMED);
		esp->flags |= ESP_FLAG_DISABLE_SYNC;
		mac_esp_ops.send_dma_cmd = esp_send_pio_cmd;
	} else {
		printk(KERN_INFO PFX "using PDMA for controller %d\n", dev->id);
	}

	host->irq = IRQ_MAC_SCSI;

	/* The request_irq() call is intended to succeed for the first device
	 * and fail for the second device.
	 */
	err = request_irq(host->irq, mac_scsi_esp_intr, 0, "ESP", NULL);
	spin_lock(&esp_chips_lock);
	if (err < 0 && esp_chips[!dev->id] == NULL) {
		spin_unlock(&esp_chips_lock);
		goto fail_free_priv;
	}
	esp_chips[dev->id] = esp;
	spin_unlock(&esp_chips_lock);

	err = scsi_esp_register(esp);
	if (err)
		goto fail_free_irq;

	return 0;

fail_free_irq:
	spin_lock(&esp_chips_lock);
	esp_chips[dev->id] = NULL;
	if (esp_chips[!dev->id] == NULL) {
		spin_unlock(&esp_chips_lock);
		free_irq(host->irq, NULL);
	} else
		spin_unlock(&esp_chips_lock);
fail_free_priv:
	kfree(mep);
fail_free_command_block:
	kfree(esp->command_block);
fail_unlink:
	scsi_host_put(host);
fail:
	return err;
}

static int esp_mac_remove(struct platform_device *dev)
{
	struct mac_esp_priv *mep = platform_get_drvdata(dev);
	struct esp *esp = mep->esp;
	unsigned int irq = esp->host->irq;

	scsi_esp_unregister(esp);

	spin_lock(&esp_chips_lock);
	esp_chips[dev->id] = NULL;
	if (esp_chips[!dev->id] == NULL) {
		spin_unlock(&esp_chips_lock);
		free_irq(irq, NULL);
	} else
		spin_unlock(&esp_chips_lock);

	kfree(mep);

	kfree(esp->command_block);

	scsi_host_put(esp->host);

	return 0;
}

static struct platform_driver esp_mac_driver = {
	.probe    = esp_mac_probe,
	.remove   = esp_mac_remove,
	.driver   = {
		.name	= DRV_MODULE_NAME,
	},
};
module_platform_driver(esp_mac_driver);

MODULE_DESCRIPTION("Mac ESP SCSI driver");
MODULE_AUTHOR("Finn Thain");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:" DRV_MODULE_NAME);
