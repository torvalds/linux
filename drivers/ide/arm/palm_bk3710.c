/*
 * Palmchip bk3710 IDE controller
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * ----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ----------------------------------------------------------------------------
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

/* Offset of the primary interface registers */
#define IDE_PALM_ATA_PRI_REG_OFFSET 0x1F0

/* Primary Control Offset */
#define IDE_PALM_ATA_PRI_CTL_OFFSET 0x3F6

/*
 * PalmChip 3710 IDE Controller UDMA timing structure Definition
 */
struct palm_bk3710_udmatiming {
	unsigned int rptime;	/* Ready to pause time  */
	unsigned int cycletime;	/* Cycle Time           */
};

#define BK3710_BMICP		0x00
#define BK3710_BMISP		0x02
#define BK3710_BMIDTP		0x04
#define BK3710_BMICS		0x08
#define BK3710_BMISS		0x0A
#define BK3710_BMIDTS		0x0C
#define BK3710_IDETIMP		0x40
#define BK3710_IDETIMS		0x42
#define BK3710_SIDETIM		0x44
#define BK3710_SLEWCTL		0x45
#define BK3710_IDESTATUS	0x47
#define BK3710_UDMACTL		0x48
#define BK3710_UDMATIM		0x4A
#define BK3710_MISCCTL		0x50
#define BK3710_REGSTB		0x54
#define BK3710_REGRCVR		0x58
#define BK3710_DATSTB		0x5C
#define BK3710_DATRCVR		0x60
#define BK3710_DMASTB		0x64
#define BK3710_DMARCVR		0x68
#define BK3710_UDMASTB		0x6C
#define BK3710_UDMATRP		0x70
#define BK3710_UDMAENV		0x74
#define BK3710_IORDYTMP		0x78
#define BK3710_IORDYTMS		0x7C

#include "../ide-timing.h"

static long ide_palm_clk;

static const struct palm_bk3710_udmatiming palm_bk3710_udmatimings[6] = {
	{160, 240},		/* UDMA Mode 0 */
	{125, 160},		/* UDMA Mode 1 */
	{100, 120},		/* UDMA Mode 2 */
	{100, 90},		/* UDMA Mode 3 */
	{85,  60},		/* UDMA Mode 4 */
};

static struct clk *ideclkp;

static void palm_bk3710_setudmamode(void __iomem *base, unsigned int dev,
				    unsigned int mode)
{
	u8 tenv, trp, t0;
	u32 val32;
	u16 val16;

	/* DMA Data Setup */
	t0 = (palm_bk3710_udmatimings[mode].cycletime + ide_palm_clk - 1)
			/ ide_palm_clk - 1;
	tenv = (20 + ide_palm_clk - 1) / ide_palm_clk - 1;
	trp = (palm_bk3710_udmatimings[mode].rptime + ide_palm_clk - 1)
			/ ide_palm_clk - 1;

	/* udmatim Register */
	val16 = readw(base + BK3710_UDMATIM) & (dev ? 0xFF0F : 0xFFF0);
	val16 |= (mode << (dev ? 4 : 0));
	writew(val16, base + BK3710_UDMATIM);

	/* udmastb Ultra DMA Access Strobe Width */
	val32 = readl(base + BK3710_UDMASTB) & (0xFF << (dev ? 0 : 8));
	val32 |= (t0 << (dev ? 8 : 0));
	writel(val32, base + BK3710_UDMASTB);

	/* udmatrp Ultra DMA Ready to Pause Time */
	val32 = readl(base + BK3710_UDMATRP) & (0xFF << (dev ? 0 : 8));
	val32 |= (trp << (dev ? 8 : 0));
	writel(val32, base + BK3710_UDMATRP);

	/* udmaenv Ultra DMA envelop Time */
	val32 = readl(base + BK3710_UDMAENV) & (0xFF << (dev ? 0 : 8));
	val32 |= (tenv << (dev ? 8 : 0));
	writel(val32, base + BK3710_UDMAENV);

	/* Enable UDMA for Device */
	val16 = readw(base + BK3710_UDMACTL) | (1 << dev);
	writew(val16, base + BK3710_UDMACTL);
}

static void palm_bk3710_setdmamode(void __iomem *base, unsigned int dev,
				   unsigned short min_cycle,
				   unsigned int mode)
{
	u8 td, tkw, t0;
	u32 val32;
	u16 val16;
	struct ide_timing *t;
	int cycletime;

	t = ide_timing_find_mode(mode);
	cycletime = max_t(int, t->cycle, min_cycle);

	/* DMA Data Setup */
	t0 = (cycletime + ide_palm_clk - 1) / ide_palm_clk;
	td = (t->active + ide_palm_clk - 1) / ide_palm_clk;
	tkw = t0 - td - 1;
	td -= 1;

	val32 = readl(base + BK3710_DMASTB) & (0xFF << (dev ? 0 : 8));
	val32 |= (td << (dev ? 8 : 0));
	writel(val32, base + BK3710_DMASTB);

	val32 = readl(base + BK3710_DMARCVR) & (0xFF << (dev ? 0 : 8));
	val32 |= (tkw << (dev ? 8 : 0));
	writel(val32, base + BK3710_DMARCVR);

	/* Disable UDMA for Device */
	val16 = readw(base + BK3710_UDMACTL) & ~(1 << dev);
	writew(val16, base + BK3710_UDMACTL);
}

static void palm_bk3710_setpiomode(void __iomem *base, ide_drive_t *mate,
				   unsigned int dev, unsigned int cycletime,
				   unsigned int mode)
{
	u8 t2, t2i, t0;
	u32 val32;
	struct ide_timing *t;

	/* PIO Data Setup */
	t0 = (cycletime + ide_palm_clk - 1) / ide_palm_clk;
	t2 = (ide_timing_find_mode(XFER_PIO_0 + mode)->active +
	      ide_palm_clk - 1)	/ ide_palm_clk;

	t2i = t0 - t2 - 1;
	t2 -= 1;

	val32 = readl(base + BK3710_DATSTB) & (0xFF << (dev ? 0 : 8));
	val32 |= (t2 << (dev ? 8 : 0));
	writel(val32, base + BK3710_DATSTB);

	val32 = readl(base + BK3710_DATRCVR) & (0xFF << (dev ? 0 : 8));
	val32 |= (t2i << (dev ? 8 : 0));
	writel(val32, base + BK3710_DATRCVR);

	if (mate && mate->present) {
		u8 mode2 = ide_get_best_pio_mode(mate, 255, 4);

		if (mode2 < mode)
			mode = mode2;
	}

	/* TASKFILE Setup */
	t = ide_timing_find_mode(XFER_PIO_0 + mode);
	t0 = (t->cyc8b + ide_palm_clk - 1) / ide_palm_clk;
	t2 = (t->act8b + ide_palm_clk - 1) / ide_palm_clk;

	t2i = t0 - t2 - 1;
	t2 -= 1;

	val32 = readl(base + BK3710_REGSTB) & (0xFF << (dev ? 0 : 8));
	val32 |= (t2 << (dev ? 8 : 0));
	writel(val32, base + BK3710_REGSTB);

	val32 = readl(base + BK3710_REGRCVR) & (0xFF << (dev ? 0 : 8));
	val32 |= (t2i << (dev ? 8 : 0));
	writel(val32, base + BK3710_REGRCVR);
}

static void palm_bk3710_set_dma_mode(ide_drive_t *drive, u8 xferspeed)
{
	int is_slave = drive->dn & 1;
	void __iomem *base = (void *)drive->hwif->dma_base;

	if (xferspeed >= XFER_UDMA_0) {
		palm_bk3710_setudmamode(base, is_slave,
					xferspeed - XFER_UDMA_0);
	} else {
		palm_bk3710_setdmamode(base, is_slave, drive->id->eide_dma_min,
				       xferspeed);
	}
}

static void palm_bk3710_set_pio_mode(ide_drive_t *drive, u8 pio)
{
	unsigned int cycle_time;
	int is_slave = drive->dn & 1;
	ide_drive_t *mate;
	void __iomem *base = (void *)drive->hwif->dma_base;

	/*
	 * Obtain the drive PIO data for tuning the Palm Chip registers
	 */
	cycle_time = ide_pio_cycle_time(drive, pio);
	mate = ide_get_paired_drive(drive);
	palm_bk3710_setpiomode(base, mate, is_slave, cycle_time, pio);
}

static void __devinit palm_bk3710_chipinit(void __iomem *base)
{
	/*
	 * enable the reset_en of ATA controller so that when ata signals
	 * are brought out, by writing into device config. at that
	 * time por_n signal should not be 'Z' and have a stable value.
	 */
	writel(0x0300, base + BK3710_MISCCTL);

	/* wait for some time and deassert the reset of ATA Device. */
	mdelay(100);

	/* Deassert the Reset */
	writel(0x0200, base + BK3710_MISCCTL);

	/*
	 * Program the IDETIMP Register Value based on the following assumptions
	 *
	 * (ATA_IDETIMP_IDEEN		, ENABLE ) |
	 * (ATA_IDETIMP_SLVTIMEN	, DISABLE) |
	 * (ATA_IDETIMP_RDYSMPL		, 70NS)    |
	 * (ATA_IDETIMP_RDYRCVRY	, 50NS)    |
	 * (ATA_IDETIMP_DMAFTIM1	, PIOCOMP) |
	 * (ATA_IDETIMP_PREPOST1	, DISABLE) |
	 * (ATA_IDETIMP_RDYSEN1		, DISABLE) |
	 * (ATA_IDETIMP_PIOFTIM1	, DISABLE) |
	 * (ATA_IDETIMP_DMAFTIM0	, PIOCOMP) |
	 * (ATA_IDETIMP_PREPOST0	, DISABLE) |
	 * (ATA_IDETIMP_RDYSEN0		, DISABLE) |
	 * (ATA_IDETIMP_PIOFTIM0	, DISABLE)
	 */
	writew(0xB388, base + BK3710_IDETIMP);

	/*
	 * Configure  SIDETIM  Register
	 * (ATA_SIDETIM_RDYSMPS1	,120NS ) |
	 * (ATA_SIDETIM_RDYRCYS1	,120NS )
	 */
	writeb(0, base + BK3710_SIDETIM);

	/*
	 * UDMACTL Ultra-ATA DMA Control
	 * (ATA_UDMACTL_UDMAP1	, 0 ) |
	 * (ATA_UDMACTL_UDMAP0	, 0 )
	 *
	 */
	writew(0, base + BK3710_UDMACTL);

	/*
	 * MISCCTL Miscellaneous Conrol Register
	 * (ATA_MISCCTL_RSTMODEP	, 1) |
	 * (ATA_MISCCTL_RESETP		, 0) |
	 * (ATA_MISCCTL_TIMORIDE	, 1)
	 */
	writel(0x201, base + BK3710_MISCCTL);

	/*
	 * IORDYTMP IORDY Timer for Primary Register
	 * (ATA_IORDYTMP_IORDYTMP     , 0xffff  )
	 */
	writel(0xFFFF, base + BK3710_IORDYTMP);

	/*
	 * Configure BMISP Register
	 * (ATA_BMISP_DMAEN1	, DISABLE )	|
	 * (ATA_BMISP_DMAEN0	, DISABLE )	|
	 * (ATA_BMISP_IORDYINT	, CLEAR)	|
	 * (ATA_BMISP_INTRSTAT	, CLEAR)	|
	 * (ATA_BMISP_DMAERROR	, CLEAR)
	 */
	writew(0, base + BK3710_BMISP);

	palm_bk3710_setpiomode(base, NULL, 0, 600, 0);
	palm_bk3710_setpiomode(base, NULL, 1, 600, 0);
}

static u8 __devinit palm_bk3710_cable_detect(ide_hwif_t *hwif)
{
	return ATA_CBL_PATA80;
}

static void __devinit palm_bk3710_init_hwif(ide_hwif_t *hwif)
{
	hwif->set_pio_mode = palm_bk3710_set_pio_mode;
	hwif->set_dma_mode = palm_bk3710_set_dma_mode;

	hwif->cable_detect = palm_bk3710_cable_detect;
}

static const struct ide_port_info __devinitdata palm_bk3710_port_info = {
	.init_hwif		= palm_bk3710_init_hwif,
	.host_flags		= IDE_HFLAG_NO_DMA, /* hack (no PCI) */
	.pio_mask		= ATA_PIO4,
	.udma_mask		= ATA_UDMA4,	/* (input clk 99MHz) */
	.mwdma_mask		= ATA_MWDMA2,
};

static int __devinit palm_bk3710_probe(struct platform_device *pdev)
{
	struct clk *clkp;
	struct resource *mem, *irq;
	ide_hwif_t *hwif;
	void __iomem *base;
	int pribase, i;
	hw_regs_t hw;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

	clkp = clk_get(NULL, "IDECLK");
	if (IS_ERR(clkp))
		return -ENODEV;

	ideclkp = clkp;
	clk_enable(ideclkp);
	ide_palm_clk = clk_get_rate(ideclkp)/100000;
	ide_palm_clk = (10000/ide_palm_clk) + 1;
	/* Register the IDE interface with Linux ATA Interface */
	memset(&hw, 0, sizeof(hw));

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL) {
		printk(KERN_ERR "failed to get memory region resource\n");
		return -ENODEV;
	}
	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq == NULL) {
		printk(KERN_ERR "failed to get IRQ resource\n");
		return -ENODEV;
	}

	base = (void *)mem->start;

	/* Configure the Palm Chip controller */
	palm_bk3710_chipinit(base);

	pribase = mem->start + IDE_PALM_ATA_PRI_REG_OFFSET;
	for (i = 0; i < IDE_NR_PORTS - 2; i++)
		hw.io_ports[i] = pribase + i;
	hw.io_ports[IDE_CONTROL_OFFSET] = mem->start +
			IDE_PALM_ATA_PRI_CTL_OFFSET;
	hw.irq = irq->start;
	hw.chipset = ide_palm3710;

	hwif = ide_find_port(hw.io_ports[IDE_DATA_OFFSET]);
	if (hwif == NULL)
		goto out;

	i = hwif->index;

	if (hwif->present)
		ide_unregister(i);
	else
		ide_init_port_data(hwif, i);

	ide_init_port_hw(hwif, &hw);

	hwif->mmio = 1;
	default_hwif_mmiops(hwif);

	ide_setup_dma(hwif, mem->start);

	idx[0] = i;

	ide_device_add(idx, &palm_bk3710_port_info);

	if (!hwif->present)
		goto out;

	return 0;
out:
	printk(KERN_WARNING "Palm Chip BK3710 IDE Register Fail\n");
	return -ENODEV;
}

static struct platform_driver platform_bk_driver = {
	.driver = {
		.name = "palm_bk3710",
	},
	.probe = palm_bk3710_probe,
	.remove = NULL,
};

static int __init palm_bk3710_init(void)
{
	return platform_driver_register(&platform_bk_driver);
}

module_init(palm_bk3710_init);
MODULE_LICENSE("GPL");

