// SPDX-License-Identifier: GPL-2.0
/*
 * ESP front-end for Amiga ZORRO SCSI systems.
 *
 * Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 *
 * Copyright (C) 2011,2018 Michael Schmitz (schmitz@debian.org) for
 *               migration to ESP SCSI core
 *
 * Copyright (C) 2013 Tuomas Vainikka (tuomas.vainikka@aalto.fi) for
 *               Blizzard 1230 DMA and probe function fixes
 *
 * Copyright (C) 2017 Finn Thain for PIO code from Mac ESP driver adapted here
 */
/*
 * ZORRO bus code from:
 */
/*
 * Detection routine for the NCR53c710 based Amiga SCSI Controllers for Linux.
 *		Amiga MacroSystemUS WarpEngine SCSI controller.
 *		Amiga Technologies/DKB A4091 SCSI controller.
 *
 * Written 1997 by Alan Hourihane <alanh@fairlite.demon.co.uk>
 * plus modifications of the 53c7xx.c driver to support the Amiga.
 *
 * Rewritten to use 53c700.c by Kars de Jong <jongk@linux-m68k.org>
 */

#define pr_fmt(fmt)        KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/zorro.h>
#include <linux/slab.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>

#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_spi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>

#include "esp_scsi.h"

MODULE_AUTHOR("Michael Schmitz <schmitz@debian.org>");
MODULE_DESCRIPTION("Amiga Zorro NCR5C9x (ESP) driver");
MODULE_LICENSE("GPL");

/* per-board register layout definitions */

/* Blizzard 1230 DMA interface */

struct blz1230_dma_registers {
	unsigned char dma_addr;		/* DMA address      [0x0000] */
	unsigned char dmapad2[0x7fff];
	unsigned char dma_latch;	/* DMA latch        [0x8000] */
};

/* Blizzard 1230II DMA interface */

struct blz1230II_dma_registers {
	unsigned char dma_addr;		/* DMA address      [0x0000] */
	unsigned char dmapad2[0xf];
	unsigned char dma_latch;	/* DMA latch        [0x0010] */
};

/* Blizzard 2060 DMA interface */

struct blz2060_dma_registers {
	unsigned char dma_led_ctrl;	/* DMA led control   [0x000] */
	unsigned char dmapad1[0x0f];
	unsigned char dma_addr0;	/* DMA address (MSB) [0x010] */
	unsigned char dmapad2[0x03];
	unsigned char dma_addr1;	/* DMA address       [0x014] */
	unsigned char dmapad3[0x03];
	unsigned char dma_addr2;	/* DMA address       [0x018] */
	unsigned char dmapad4[0x03];
	unsigned char dma_addr3;	/* DMA address (LSB) [0x01c] */
};

/* DMA control bits */
#define DMA_WRITE 0x80000000

/* Cyberstorm DMA interface */

struct cyber_dma_registers {
	unsigned char dma_addr0;	/* DMA address (MSB) [0x000] */
	unsigned char dmapad1[1];
	unsigned char dma_addr1;	/* DMA address       [0x002] */
	unsigned char dmapad2[1];
	unsigned char dma_addr2;	/* DMA address       [0x004] */
	unsigned char dmapad3[1];
	unsigned char dma_addr3;	/* DMA address (LSB) [0x006] */
	unsigned char dmapad4[0x3fb];
	unsigned char cond_reg;		/* DMA cond    (ro)  [0x402] */
#define ctrl_reg  cond_reg		/* DMA control (wo)  [0x402] */
};

/* DMA control bits */
#define CYBER_DMA_WRITE  0x40	/* DMA direction. 1 = write */
#define CYBER_DMA_Z3     0x20	/* 16 (Z2) or 32 (CHIP/Z3) bit DMA transfer */

/* DMA status bits */
#define CYBER_DMA_HNDL_INTR 0x80	/* DMA IRQ pending? */

/* The CyberStorm II DMA interface */
struct cyberII_dma_registers {
	unsigned char cond_reg;		/* DMA cond    (ro)  [0x000] */
#define ctrl_reg  cond_reg		/* DMA control (wo)  [0x000] */
	unsigned char dmapad4[0x3f];
	unsigned char dma_addr0;	/* DMA address (MSB) [0x040] */
	unsigned char dmapad1[3];
	unsigned char dma_addr1;	/* DMA address       [0x044] */
	unsigned char dmapad2[3];
	unsigned char dma_addr2;	/* DMA address       [0x048] */
	unsigned char dmapad3[3];
	unsigned char dma_addr3;	/* DMA address (LSB) [0x04c] */
};

/* Fastlane DMA interface */

struct fastlane_dma_registers {
	unsigned char cond_reg;		/* DMA status  (ro) [0x0000] */
#define ctrl_reg  cond_reg		/* DMA control (wo) [0x0000] */
	char dmapad1[0x3f];
	unsigned char clear_strobe;	/* DMA clear   (wo) [0x0040] */
};

/*
 * The controller registers can be found in the Z2 config area at these
 * offsets:
 */
#define FASTLANE_ESP_ADDR	0x1000001

/* DMA status bits */
#define FASTLANE_DMA_MINT	0x80
#define FASTLANE_DMA_IACT	0x40
#define FASTLANE_DMA_CREQ	0x20

/* DMA control bits */
#define FASTLANE_DMA_FCODE	0xa0
#define FASTLANE_DMA_MASK	0xf3
#define FASTLANE_DMA_WRITE	0x08	/* 1 = write */
#define FASTLANE_DMA_ENABLE	0x04	/* Enable DMA */
#define FASTLANE_DMA_EDI	0x02	/* Enable DMA IRQ ? */
#define FASTLANE_DMA_ESI	0x01	/* Enable SCSI IRQ */

/*
 * private data used for driver
 */
struct zorro_esp_priv {
	struct esp *esp;		/* our ESP instance - for Scsi_host* */
	void __iomem *board_base;	/* virtual address (Zorro III board) */
	int error;			/* PIO error flag */
	int zorro3;			/* board is Zorro III */
	unsigned char ctrl_data;	/* shadow copy of ctrl_reg */
};

/*
 * On all implementations except for the Oktagon, padding between ESP
 * registers is three bytes.
 * On Oktagon, it is one byte - use a different accessor there.
 *
 * Oktagon needs PDMA - currently unsupported!
 */

static void zorro_esp_write8(struct esp *esp, u8 val, unsigned long reg)
{
	writeb(val, esp->regs + (reg * 4UL));
}

static u8 zorro_esp_read8(struct esp *esp, unsigned long reg)
{
	return readb(esp->regs + (reg * 4UL));
}

static dma_addr_t zorro_esp_map_single(struct esp *esp, void *buf,
				      size_t sz, int dir)
{
	return dma_map_single(esp->dev, buf, sz, dir);
}

static int zorro_esp_map_sg(struct esp *esp, struct scatterlist *sg,
				  int num_sg, int dir)
{
	return dma_map_sg(esp->dev, sg, num_sg, dir);
}

static void zorro_esp_unmap_single(struct esp *esp, dma_addr_t addr,
				  size_t sz, int dir)
{
	dma_unmap_single(esp->dev, addr, sz, dir);
}

static void zorro_esp_unmap_sg(struct esp *esp, struct scatterlist *sg,
			      int num_sg, int dir)
{
	dma_unmap_sg(esp->dev, sg, num_sg, dir);
}

static int zorro_esp_irq_pending(struct esp *esp)
{
	/* check ESP status register; DMA has no status reg. */
	if (zorro_esp_read8(esp, ESP_STATUS) & ESP_STAT_INTR)
		return 1;

	return 0;
}

static int cyber_esp_irq_pending(struct esp *esp)
{
	struct cyber_dma_registers __iomem *dregs = esp->dma_regs;
	unsigned char dma_status = readb(&dregs->cond_reg);

	/* It's important to check the DMA IRQ bit in the correct way! */
	return ((zorro_esp_read8(esp, ESP_STATUS) & ESP_STAT_INTR) &&
		(dma_status & CYBER_DMA_HNDL_INTR));
}

static int fastlane_esp_irq_pending(struct esp *esp)
{
	struct fastlane_dma_registers __iomem *dregs = esp->dma_regs;
	unsigned char dma_status;

	dma_status = readb(&dregs->cond_reg);

	if (dma_status & FASTLANE_DMA_IACT)
		return 0;	/* not our IRQ */

	/* Return non-zero if ESP requested IRQ */
	return (
	   (dma_status & FASTLANE_DMA_CREQ) &&
	   (!(dma_status & FASTLANE_DMA_MINT)) &&
	   (zorro_esp_read8(esp, ESP_STATUS) & ESP_STAT_INTR));
}

static u32 zorro_esp_dma_length_limit(struct esp *esp, u32 dma_addr,
					u32 dma_len)
{
	return dma_len > (1U << 16) ? (1U << 16) : dma_len;
}

static u32 fastlane_esp_dma_length_limit(struct esp *esp, u32 dma_addr,
					u32 dma_len)
{
	/* The old driver used 0xfffc as limit, so do that here too */
	return dma_len > 0xfffc ? 0xfffc : dma_len;
}

static void zorro_esp_reset_dma(struct esp *esp)
{
	/* nothing to do here */
}

static void zorro_esp_dma_drain(struct esp *esp)
{
	/* nothing to do here */
}

static void zorro_esp_dma_invalidate(struct esp *esp)
{
	/* nothing to do here */
}

static void fastlane_esp_dma_invalidate(struct esp *esp)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	struct fastlane_dma_registers __iomem *dregs = esp->dma_regs;
	unsigned char *ctrl_data = &zep->ctrl_data;

	*ctrl_data = (*ctrl_data & FASTLANE_DMA_MASK);
	writeb(0, &dregs->clear_strobe);
	z_writel(0, zep->board_base);
}

/*
 * Programmed IO routines follow.
 */

static inline unsigned int zorro_esp_wait_for_fifo(struct esp *esp)
{
	int i = 500000;

	do {
		unsigned int fbytes = zorro_esp_read8(esp, ESP_FFLAGS)
							& ESP_FF_FBYTES;

		if (fbytes)
			return fbytes;

		udelay(2);
	} while (--i);

	pr_err("FIFO is empty (sreg %02x)\n",
	       zorro_esp_read8(esp, ESP_STATUS));
	return 0;
}

static inline int zorro_esp_wait_for_intr(struct esp *esp)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	int i = 500000;

	do {
		esp->sreg = zorro_esp_read8(esp, ESP_STATUS);
		if (esp->sreg & ESP_STAT_INTR)
			return 0;

		udelay(2);
	} while (--i);

	pr_err("IRQ timeout (sreg %02x)\n", esp->sreg);
	zep->error = 1;
	return 1;
}

/*
 * PIO macros as used in mac_esp.c.
 * Note that addr and fifo arguments are local-scope variables declared
 * in zorro_esp_send_pio_cmd(), the macros are only used in that function,
 * and addr and fifo are referenced in each use of the macros so there
 * is no need to pass them as macro parameters.
 */
#define ZORRO_ESP_PIO_LOOP(operands, reg1) \
	asm volatile ( \
	     "1:     moveb " operands "\n" \
	     "       subqw #1,%1       \n" \
	     "       jbne 1b           \n" \
	     : "+a" (addr), "+r" (reg1) \
	     : "a" (fifo));

#define ZORRO_ESP_PIO_FILL(operands, reg1) \
	asm volatile ( \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       moveb " operands "\n" \
	     "       subqw #8,%1       \n" \
	     "       subqw #8,%1       \n" \
	     : "+a" (addr), "+r" (reg1) \
	     : "a" (fifo));

#define ZORRO_ESP_FIFO_SIZE 16

static void zorro_esp_send_pio_cmd(struct esp *esp, u32 addr, u32 esp_count,
				 u32 dma_count, int write, u8 cmd)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	u8 __iomem *fifo = esp->regs + ESP_FDATA * 16;
	u8 phase = esp->sreg & ESP_STAT_PMASK;

	cmd &= ~ESP_CMD_DMA;

	if (write) {
		u8 *dst = (u8 *)addr;
		u8 mask = ~(phase == ESP_MIP ? ESP_INTR_FDONE : ESP_INTR_BSERV);

		scsi_esp_cmd(esp, cmd);

		while (1) {
			if (!zorro_esp_wait_for_fifo(esp))
				break;

			*dst++ = zorro_esp_read8(esp, ESP_FDATA);
			--esp_count;

			if (!esp_count)
				break;

			if (zorro_esp_wait_for_intr(esp))
				break;

			if ((esp->sreg & ESP_STAT_PMASK) != phase)
				break;

			esp->ireg = zorro_esp_read8(esp, ESP_INTRPT);
			if (esp->ireg & mask) {
				zep->error = 1;
				break;
			}

			if (phase == ESP_MIP)
				scsi_esp_cmd(esp, ESP_CMD_MOK);

			scsi_esp_cmd(esp, ESP_CMD_TI);
		}
	} else {	/* unused, as long as we only handle MIP here */
		scsi_esp_cmd(esp, ESP_CMD_FLUSH);

		if (esp_count >= ZORRO_ESP_FIFO_SIZE)
			ZORRO_ESP_PIO_FILL("%0@+,%2@", esp_count)
		else
			ZORRO_ESP_PIO_LOOP("%0@+,%2@", esp_count)

		scsi_esp_cmd(esp, cmd);

		while (esp_count) {
			unsigned int n;

			if (zorro_esp_wait_for_intr(esp))
				break;

			if ((esp->sreg & ESP_STAT_PMASK) != phase)
				break;

			esp->ireg = zorro_esp_read8(esp, ESP_INTRPT);
			if (esp->ireg & ~ESP_INTR_BSERV) {
				zep->error = 1;
				break;
			}

			n = ZORRO_ESP_FIFO_SIZE -
			    (zorro_esp_read8(esp, ESP_FFLAGS) & ESP_FF_FBYTES);
			if (n > esp_count)
				n = esp_count;

			if (n == ZORRO_ESP_FIFO_SIZE)
				ZORRO_ESP_PIO_FILL("%0@+,%2@", esp_count)
			else {
				esp_count -= n;
				ZORRO_ESP_PIO_LOOP("%0@+,%2@", n)
			}

			scsi_esp_cmd(esp, ESP_CMD_TI);
		}
	}
}

/* Blizzard 1230/60 SCSI-IV DMA */

static void zorro_esp_send_blz1230_dma_cmd(struct esp *esp, u32 addr,
			u32 esp_count, u32 dma_count, int write, u8 cmd)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	struct blz1230_dma_registers __iomem *dregs = esp->dma_regs;
	u8 phase = esp->sreg & ESP_STAT_PMASK;

	zep->error = 0;
	/*
	 * Use PIO if transferring message bytes to esp->command_block_dma.
	 * PIO requires a virtual address, so substitute esp->command_block
	 * for addr.
	 */
	if (phase == ESP_MIP && addr == esp->command_block_dma) {
		zorro_esp_send_pio_cmd(esp, (u32) esp->command_block,
					esp_count, dma_count, write, cmd);
		return;
	}

	if (write)
		/* DMA receive */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_FROM_DEVICE);
	else
		/* DMA send */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_TO_DEVICE);

	addr >>= 1;
	if (write)
		addr &= ~(DMA_WRITE);
	else
		addr |= DMA_WRITE;

	writeb((addr >> 24) & 0xff, &dregs->dma_latch);
	writeb((addr >> 24) & 0xff, &dregs->dma_addr);
	writeb((addr >> 16) & 0xff, &dregs->dma_addr);
	writeb((addr >>  8) & 0xff, &dregs->dma_addr);
	writeb(addr & 0xff, &dregs->dma_addr);

	scsi_esp_cmd(esp, ESP_CMD_DMA);
	zorro_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	zorro_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);

	scsi_esp_cmd(esp, cmd);
}

/* Blizzard 1230-II DMA */

static void zorro_esp_send_blz1230II_dma_cmd(struct esp *esp, u32 addr,
			u32 esp_count, u32 dma_count, int write, u8 cmd)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	struct blz1230II_dma_registers __iomem *dregs = esp->dma_regs;
	u8 phase = esp->sreg & ESP_STAT_PMASK;

	zep->error = 0;
	/* Use PIO if transferring message bytes to esp->command_block_dma */
	if (phase == ESP_MIP && addr == esp->command_block_dma) {
		zorro_esp_send_pio_cmd(esp, (u32) esp->command_block,
					esp_count, dma_count, write, cmd);
		return;
	}

	if (write)
		/* DMA receive */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_FROM_DEVICE);
	else
		/* DMA send */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_TO_DEVICE);

	addr >>= 1;
	if (write)
		addr &= ~(DMA_WRITE);
	else
		addr |= DMA_WRITE;

	writeb((addr >> 24) & 0xff, &dregs->dma_latch);
	writeb((addr >> 16) & 0xff, &dregs->dma_addr);
	writeb((addr >>  8) & 0xff, &dregs->dma_addr);
	writeb(addr & 0xff, &dregs->dma_addr);

	scsi_esp_cmd(esp, ESP_CMD_DMA);
	zorro_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	zorro_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);

	scsi_esp_cmd(esp, cmd);
}

/* Blizzard 2060 DMA */

static void zorro_esp_send_blz2060_dma_cmd(struct esp *esp, u32 addr,
			u32 esp_count, u32 dma_count, int write, u8 cmd)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	struct blz2060_dma_registers __iomem *dregs = esp->dma_regs;
	u8 phase = esp->sreg & ESP_STAT_PMASK;

	zep->error = 0;
	/* Use PIO if transferring message bytes to esp->command_block_dma */
	if (phase == ESP_MIP && addr == esp->command_block_dma) {
		zorro_esp_send_pio_cmd(esp, (u32) esp->command_block,
					esp_count, dma_count, write, cmd);
		return;
	}

	if (write)
		/* DMA receive */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_FROM_DEVICE);
	else
		/* DMA send */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_TO_DEVICE);

	addr >>= 1;
	if (write)
		addr &= ~(DMA_WRITE);
	else
		addr |= DMA_WRITE;

	writeb(addr & 0xff, &dregs->dma_addr3);
	writeb((addr >>  8) & 0xff, &dregs->dma_addr2);
	writeb((addr >> 16) & 0xff, &dregs->dma_addr1);
	writeb((addr >> 24) & 0xff, &dregs->dma_addr0);

	scsi_esp_cmd(esp, ESP_CMD_DMA);
	zorro_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	zorro_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);

	scsi_esp_cmd(esp, cmd);
}

/* Cyberstorm I DMA */

static void zorro_esp_send_cyber_dma_cmd(struct esp *esp, u32 addr,
			u32 esp_count, u32 dma_count, int write, u8 cmd)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	struct cyber_dma_registers __iomem *dregs = esp->dma_regs;
	u8 phase = esp->sreg & ESP_STAT_PMASK;
	unsigned char *ctrl_data = &zep->ctrl_data;

	zep->error = 0;
	/* Use PIO if transferring message bytes to esp->command_block_dma */
	if (phase == ESP_MIP && addr == esp->command_block_dma) {
		zorro_esp_send_pio_cmd(esp, (u32) esp->command_block,
					esp_count, dma_count, write, cmd);
		return;
	}

	zorro_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	zorro_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);

	if (write) {
		/* DMA receive */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_FROM_DEVICE);
		addr &= ~(1);
	} else {
		/* DMA send */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_TO_DEVICE);
		addr |= 1;
	}

	writeb((addr >> 24) & 0xff, &dregs->dma_addr0);
	writeb((addr >> 16) & 0xff, &dregs->dma_addr1);
	writeb((addr >>  8) & 0xff, &dregs->dma_addr2);
	writeb(addr & 0xff, &dregs->dma_addr3);

	if (write)
		*ctrl_data &= ~(CYBER_DMA_WRITE);
	else
		*ctrl_data |= CYBER_DMA_WRITE;

	*ctrl_data &= ~(CYBER_DMA_Z3);	/* Z2, do 16 bit DMA */

	writeb(*ctrl_data, &dregs->ctrl_reg);

	scsi_esp_cmd(esp, cmd);
}

/* Cyberstorm II DMA */

static void zorro_esp_send_cyberII_dma_cmd(struct esp *esp, u32 addr,
			u32 esp_count, u32 dma_count, int write, u8 cmd)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	struct cyberII_dma_registers __iomem *dregs = esp->dma_regs;
	u8 phase = esp->sreg & ESP_STAT_PMASK;

	zep->error = 0;
	/* Use PIO if transferring message bytes to esp->command_block_dma */
	if (phase == ESP_MIP && addr == esp->command_block_dma) {
		zorro_esp_send_pio_cmd(esp, (u32) esp->command_block,
					esp_count, dma_count, write, cmd);
		return;
	}

	zorro_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	zorro_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);

	if (write) {
		/* DMA receive */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_FROM_DEVICE);
		addr &= ~(1);
	} else {
		/* DMA send */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_TO_DEVICE);
		addr |= 1;
	}

	writeb((addr >> 24) & 0xff, &dregs->dma_addr0);
	writeb((addr >> 16) & 0xff, &dregs->dma_addr1);
	writeb((addr >>  8) & 0xff, &dregs->dma_addr2);
	writeb(addr & 0xff, &dregs->dma_addr3);

	scsi_esp_cmd(esp, cmd);
}

/* Fastlane DMA */

static void zorro_esp_send_fastlane_dma_cmd(struct esp *esp, u32 addr,
			u32 esp_count, u32 dma_count, int write, u8 cmd)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);
	struct fastlane_dma_registers __iomem *dregs = esp->dma_regs;
	u8 phase = esp->sreg & ESP_STAT_PMASK;
	unsigned char *ctrl_data = &zep->ctrl_data;

	zep->error = 0;
	/* Use PIO if transferring message bytes to esp->command_block_dma */
	if (phase == ESP_MIP && addr == esp->command_block_dma) {
		zorro_esp_send_pio_cmd(esp, (u32) esp->command_block,
					esp_count, dma_count, write, cmd);
		return;
	}

	zorro_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	zorro_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);

	if (write) {
		/* DMA receive */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_FROM_DEVICE);
		addr &= ~(1);
	} else {
		/* DMA send */
		dma_sync_single_for_device(esp->dev, addr, esp_count,
				DMA_TO_DEVICE);
		addr |= 1;
	}

	writeb(0, &dregs->clear_strobe);
	z_writel(addr, ((addr & 0x00ffffff) + zep->board_base));

	if (write) {
		*ctrl_data = (*ctrl_data & FASTLANE_DMA_MASK) |
				FASTLANE_DMA_ENABLE;
	} else {
		*ctrl_data = ((*ctrl_data & FASTLANE_DMA_MASK) |
				FASTLANE_DMA_ENABLE |
				FASTLANE_DMA_WRITE);
	}

	writeb(*ctrl_data, &dregs->ctrl_reg);

	scsi_esp_cmd(esp, cmd);
}

static int zorro_esp_dma_error(struct esp *esp)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(esp->dev);

	/* check for error in case we've been doing PIO */
	if (zep->error == 1)
		return 1;

	/* do nothing - there seems to be no way to check for DMA errors */
	return 0;
}

/* per-board ESP driver ops */

static const struct esp_driver_ops blz1230_esp_ops = {
	.esp_write8		= zorro_esp_write8,
	.esp_read8		= zorro_esp_read8,
	.map_single		= zorro_esp_map_single,
	.map_sg			= zorro_esp_map_sg,
	.unmap_single		= zorro_esp_unmap_single,
	.unmap_sg		= zorro_esp_unmap_sg,
	.irq_pending		= zorro_esp_irq_pending,
	.dma_length_limit	= zorro_esp_dma_length_limit,
	.reset_dma		= zorro_esp_reset_dma,
	.dma_drain		= zorro_esp_dma_drain,
	.dma_invalidate		= zorro_esp_dma_invalidate,
	.send_dma_cmd		= zorro_esp_send_blz1230_dma_cmd,
	.dma_error		= zorro_esp_dma_error,
};

static const struct esp_driver_ops blz1230II_esp_ops = {
	.esp_write8		= zorro_esp_write8,
	.esp_read8		= zorro_esp_read8,
	.map_single		= zorro_esp_map_single,
	.map_sg			= zorro_esp_map_sg,
	.unmap_single		= zorro_esp_unmap_single,
	.unmap_sg		= zorro_esp_unmap_sg,
	.irq_pending		= zorro_esp_irq_pending,
	.dma_length_limit	= zorro_esp_dma_length_limit,
	.reset_dma		= zorro_esp_reset_dma,
	.dma_drain		= zorro_esp_dma_drain,
	.dma_invalidate		= zorro_esp_dma_invalidate,
	.send_dma_cmd		= zorro_esp_send_blz1230II_dma_cmd,
	.dma_error		= zorro_esp_dma_error,
};

static const struct esp_driver_ops blz2060_esp_ops = {
	.esp_write8		= zorro_esp_write8,
	.esp_read8		= zorro_esp_read8,
	.map_single		= zorro_esp_map_single,
	.map_sg			= zorro_esp_map_sg,
	.unmap_single		= zorro_esp_unmap_single,
	.unmap_sg		= zorro_esp_unmap_sg,
	.irq_pending		= zorro_esp_irq_pending,
	.dma_length_limit	= zorro_esp_dma_length_limit,
	.reset_dma		= zorro_esp_reset_dma,
	.dma_drain		= zorro_esp_dma_drain,
	.dma_invalidate		= zorro_esp_dma_invalidate,
	.send_dma_cmd		= zorro_esp_send_blz2060_dma_cmd,
	.dma_error		= zorro_esp_dma_error,
};

static const struct esp_driver_ops cyber_esp_ops = {
	.esp_write8		= zorro_esp_write8,
	.esp_read8		= zorro_esp_read8,
	.map_single		= zorro_esp_map_single,
	.map_sg			= zorro_esp_map_sg,
	.unmap_single		= zorro_esp_unmap_single,
	.unmap_sg		= zorro_esp_unmap_sg,
	.irq_pending		= cyber_esp_irq_pending,
	.dma_length_limit	= zorro_esp_dma_length_limit,
	.reset_dma		= zorro_esp_reset_dma,
	.dma_drain		= zorro_esp_dma_drain,
	.dma_invalidate		= zorro_esp_dma_invalidate,
	.send_dma_cmd		= zorro_esp_send_cyber_dma_cmd,
	.dma_error		= zorro_esp_dma_error,
};

static const struct esp_driver_ops cyberII_esp_ops = {
	.esp_write8		= zorro_esp_write8,
	.esp_read8		= zorro_esp_read8,
	.map_single		= zorro_esp_map_single,
	.map_sg			= zorro_esp_map_sg,
	.unmap_single		= zorro_esp_unmap_single,
	.unmap_sg		= zorro_esp_unmap_sg,
	.irq_pending		= zorro_esp_irq_pending,
	.dma_length_limit	= zorro_esp_dma_length_limit,
	.reset_dma		= zorro_esp_reset_dma,
	.dma_drain		= zorro_esp_dma_drain,
	.dma_invalidate		= zorro_esp_dma_invalidate,
	.send_dma_cmd		= zorro_esp_send_cyberII_dma_cmd,
	.dma_error		= zorro_esp_dma_error,
};

static const struct esp_driver_ops fastlane_esp_ops = {
	.esp_write8		= zorro_esp_write8,
	.esp_read8		= zorro_esp_read8,
	.map_single		= zorro_esp_map_single,
	.map_sg			= zorro_esp_map_sg,
	.unmap_single		= zorro_esp_unmap_single,
	.unmap_sg		= zorro_esp_unmap_sg,
	.irq_pending		= fastlane_esp_irq_pending,
	.dma_length_limit	= fastlane_esp_dma_length_limit,
	.reset_dma		= zorro_esp_reset_dma,
	.dma_drain		= zorro_esp_dma_drain,
	.dma_invalidate		= fastlane_esp_dma_invalidate,
	.send_dma_cmd		= zorro_esp_send_fastlane_dma_cmd,
	.dma_error		= zorro_esp_dma_error,
};

/* Zorro driver config data */

struct zorro_driver_data {
	const char *name;
	unsigned long offset;
	unsigned long dma_offset;
	int absolute;	/* offset is absolute address */
	int scsi_option;
	const struct esp_driver_ops *esp_ops;
};

/* board types */

enum {
	ZORRO_BLZ1230,
	ZORRO_BLZ1230II,
	ZORRO_BLZ2060,
	ZORRO_CYBER,
	ZORRO_CYBERII,
	ZORRO_FASTLANE,
};

/* per-board config data */

static const struct zorro_driver_data zorro_esp_boards[] = {
	[ZORRO_BLZ1230] = {
				.name		= "Blizzard 1230",
				.offset		= 0x8000,
				.dma_offset	= 0x10000,
				.scsi_option	= 1,
				.esp_ops	= &blz1230_esp_ops,
	},
	[ZORRO_BLZ1230II] = {
				.name		= "Blizzard 1230II",
				.offset		= 0x10000,
				.dma_offset	= 0x10021,
				.scsi_option	= 1,
				.esp_ops	= &blz1230II_esp_ops,
	},
	[ZORRO_BLZ2060] = {
				.name		= "Blizzard 2060",
				.offset		= 0x1ff00,
				.dma_offset	= 0x1ffe0,
				.esp_ops	= &blz2060_esp_ops,
	},
	[ZORRO_CYBER] = {
				.name		= "CyberStormI",
				.offset		= 0xf400,
				.dma_offset	= 0xf800,
				.esp_ops	= &cyber_esp_ops,
	},
	[ZORRO_CYBERII] = {
				.name		= "CyberStormII",
				.offset		= 0x1ff03,
				.dma_offset	= 0x1ff43,
				.scsi_option	= 1,
				.esp_ops	= &cyberII_esp_ops,
	},
	[ZORRO_FASTLANE] = {
				.name		= "Fastlane",
				.offset		= 0x1000001,
				.dma_offset	= 0x1000041,
				.esp_ops	= &fastlane_esp_ops,
	},
};

static const struct zorro_device_id zorro_esp_zorro_tbl[] = {
	{	/* Blizzard 1230 IV */
		.id = ZORRO_ID(PHASE5, 0x11, 0),
		.driver_data = ZORRO_BLZ1230,
	},
	{	/* Blizzard 1230 II (Zorro II) or Fastlane (Zorro III) */
		.id = ZORRO_ID(PHASE5, 0x0B, 0),
		.driver_data = ZORRO_BLZ1230II,
	},
	{	/* Blizzard 2060 */
		.id = ZORRO_ID(PHASE5, 0x18, 0),
		.driver_data = ZORRO_BLZ2060,
	},
	{	/* Cyberstorm */
		.id = ZORRO_ID(PHASE5, 0x0C, 0),
		.driver_data = ZORRO_CYBER,
	},
	{	/* Cyberstorm II */
		.id = ZORRO_ID(PHASE5, 0x19, 0),
		.driver_data = ZORRO_CYBERII,
	},
	{ 0 }
};
MODULE_DEVICE_TABLE(zorro, zorro_esp_zorro_tbl);

static int zorro_esp_probe(struct zorro_dev *z,
				       const struct zorro_device_id *ent)
{
	struct scsi_host_template *tpnt = &scsi_esp_template;
	struct Scsi_Host *host;
	struct esp *esp;
	const struct zorro_driver_data *zdd;
	struct zorro_esp_priv *zep;
	unsigned long board, ioaddr, dmaaddr;
	int err;

	board = zorro_resource_start(z);
	zdd = &zorro_esp_boards[ent->driver_data];

	pr_info("%s found at address 0x%lx.\n", zdd->name, board);

	zep = kzalloc(sizeof(*zep), GFP_KERNEL);
	if (!zep) {
		pr_err("Can't allocate device private data!\n");
		return -ENOMEM;
	}

	/* let's figure out whether we have a Zorro II or Zorro III board */
	if ((z->rom.er_Type & ERT_TYPEMASK) == ERT_ZORROIII) {
		if (board > 0xffffff)
			zep->zorro3 = 1;
	} else {
		/*
		 * Even though most of these boards identify as Zorro II,
		 * they are in fact CPU expansion slot boards and have full
		 * access to all of memory. Fix up DMA bitmask here.
		 */
		z->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	}

	/*
	 * If Zorro III and ID matches Fastlane, our device table entry
	 * contains data for the Blizzard 1230 II board which does share the
	 * same ID. Fix up device table entry here.
	 * TODO: Some Cyberstom060 boards also share this ID but would need
	 * to use the Cyberstorm I driver data ... we catch this by checking
	 * for presence of ESP chip later, but don't try to fix up yet.
	 */
	if (zep->zorro3 && ent->driver_data == ZORRO_BLZ1230II) {
		pr_info("%s at address 0x%lx is Fastlane Z3, fixing data!\n",
			zdd->name, board);
		zdd = &zorro_esp_boards[ZORRO_FASTLANE];
	}

	if (zdd->absolute) {
		ioaddr  = zdd->offset;
		dmaaddr = zdd->dma_offset;
	} else {
		ioaddr  = board + zdd->offset;
		dmaaddr = board + zdd->dma_offset;
	}

	if (!zorro_request_device(z, zdd->name)) {
		pr_err("cannot reserve region 0x%lx, abort\n",
		       board);
		err = -EBUSY;
		goto fail_free_zep;
	}

	host = scsi_host_alloc(tpnt, sizeof(struct esp));

	if (!host) {
		pr_err("No host detected; board configuration problem?\n");
		err = -ENOMEM;
		goto fail_release_device;
	}

	host->base		= ioaddr;
	host->this_id		= 7;

	esp			= shost_priv(host);
	esp->host		= host;
	esp->dev		= &z->dev;

	esp->scsi_id		= host->this_id;
	esp->scsi_id_mask	= (1 << esp->scsi_id);

	esp->cfreq = 40000000;

	zep->esp = esp;

	dev_set_drvdata(esp->dev, zep);

	/* additional setup required for Fastlane */
	if (zep->zorro3 && ent->driver_data == ZORRO_BLZ1230II) {
		/* map full address space up to ESP base for DMA */
		zep->board_base = ioremap_nocache(board,
						FASTLANE_ESP_ADDR-1);
		if (!zep->board_base) {
			pr_err("Cannot allocate board address space\n");
			err = -ENOMEM;
			goto fail_free_host;
		}
		/* initialize DMA control shadow register */
		zep->ctrl_data = (FASTLANE_DMA_FCODE |
				  FASTLANE_DMA_EDI | FASTLANE_DMA_ESI);
	}

	esp->ops = zdd->esp_ops;

	if (ioaddr > 0xffffff)
		esp->regs = ioremap_nocache(ioaddr, 0x20);
	else
		/* ZorroII address space remapped nocache by early startup */
		esp->regs = ZTWO_VADDR(ioaddr);

	if (!esp->regs) {
		err = -ENOMEM;
		goto fail_unmap_fastlane;
	}

	/* Check whether a Blizzard 12x0 or CyberstormII really has SCSI */
	if (zdd->scsi_option) {
		zorro_esp_write8(esp, (ESP_CONFIG1_PENABLE | 7), ESP_CFG1);
		if (zorro_esp_read8(esp, ESP_CFG1) != (ESP_CONFIG1_PENABLE|7)) {
			err = -ENODEV;
			goto fail_unmap_regs;
		}
	}

	if (zep->zorro3) {
		/*
		 * Only Fastlane Z3 for now - add switch for correct struct
		 * dma_registers size if adding any more
		 */
		esp->dma_regs = ioremap_nocache(dmaaddr,
				sizeof(struct fastlane_dma_registers));
	} else
		/* ZorroII address space remapped nocache by early startup */
		esp->dma_regs = ZTWO_VADDR(dmaaddr);

	if (!esp->dma_regs) {
		err = -ENOMEM;
		goto fail_unmap_regs;
	}

	esp->command_block = dma_alloc_coherent(esp->dev, 16,
						&esp->command_block_dma,
						GFP_KERNEL);

	if (!esp->command_block) {
		err = -ENOMEM;
		goto fail_unmap_dma_regs;
	}

	host->irq = IRQ_AMIGA_PORTS;
	err = request_irq(host->irq, scsi_esp_intr, IRQF_SHARED,
			  "Amiga Zorro ESP", esp);
	if (err < 0) {
		err = -ENODEV;
		goto fail_free_command_block;
	}

	/* register the chip */
	err = scsi_esp_register(esp, &z->dev);

	if (err) {
		err = -ENOMEM;
		goto fail_free_irq;
	}

	return 0;

fail_free_irq:
	free_irq(host->irq, esp);

fail_free_command_block:
	dma_free_coherent(esp->dev, 16,
			  esp->command_block,
			  esp->command_block_dma);

fail_unmap_dma_regs:
	if (zep->zorro3)
		iounmap(esp->dma_regs);

fail_unmap_regs:
	if (ioaddr > 0xffffff)
		iounmap(esp->regs);

fail_unmap_fastlane:
	if (zep->zorro3)
		iounmap(zep->board_base);

fail_free_host:
	scsi_host_put(host);

fail_release_device:
	zorro_release_device(z);

fail_free_zep:
	kfree(zep);

	return err;
}

static void zorro_esp_remove(struct zorro_dev *z)
{
	struct zorro_esp_priv *zep = dev_get_drvdata(&z->dev);
	struct esp *esp	= zep->esp;
	struct Scsi_Host *host = esp->host;

	scsi_esp_unregister(esp);

	free_irq(host->irq, esp);
	dma_free_coherent(esp->dev, 16,
			  esp->command_block,
			  esp->command_block_dma);

	if (zep->zorro3) {
		iounmap(zep->board_base);
		iounmap(esp->dma_regs);
	}

	if (host->base > 0xffffff)
		iounmap(esp->regs);

	scsi_host_put(host);

	zorro_release_device(z);

	kfree(zep);
}

static struct zorro_driver zorro_esp_driver = {
	.name	  = KBUILD_MODNAME,
	.id_table = zorro_esp_zorro_tbl,
	.probe	  = zorro_esp_probe,
	.remove	  = zorro_esp_remove,
};

static int __init zorro_esp_scsi_init(void)
{
	return zorro_register_driver(&zorro_esp_driver);
}

static void __exit zorro_esp_scsi_exit(void)
{
	zorro_unregister_driver(&zorro_esp_driver);
}

module_init(zorro_esp_scsi_init);
module_exit(zorro_esp_scsi_exit);
