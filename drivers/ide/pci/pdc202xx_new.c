/*
 *  Promise TX2/TX4/TX2000/133 IDE driver
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Split from:
 *  linux/drivers/ide/pdc202xx.c	Version 0.35	Mar. 30, 2002
 *  Copyright (C) 1998-2002		Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2005-2007		MontaVista Software, Inc.
 *  Portions Copyright (C) 1999 Promise Technology, Inc.
 *  Author: Frank Tiernan (frankt@promise.com)
 *  Released under terms of General Public License
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_PPC_PMAC
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt, args...) printk("%s: " fmt, __FUNCTION__, ## args)
#else
#define DBG(fmt, args...)
#endif

static const char *pdc_quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP KA9.1",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP KX13.6",
	"QUANTUM FIREBALLP KX20.5",
	"QUANTUM FIREBALLP KX27.3",
	"QUANTUM FIREBALLP LM20.5",
	NULL
};

static u8 max_dma_rate(struct pci_dev *pdev)
{
	u8 mode;

	switch(pdev->device) {
		case PCI_DEVICE_ID_PROMISE_20277:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20271:
		case PCI_DEVICE_ID_PROMISE_20269:
			mode = 4;
			break;
		case PCI_DEVICE_ID_PROMISE_20270:
		case PCI_DEVICE_ID_PROMISE_20268:
			mode = 3;
			break;
		default:
			return 0;
	}

	return mode;
}

/**
 * get_indexed_reg - Get indexed register
 * @hwif: for the port address
 * @index: index of the indexed register
 */
static u8 get_indexed_reg(ide_hwif_t *hwif, u8 index)
{
	u8 value;

	outb(index, hwif->dma_vendor1);
	value = inb(hwif->dma_vendor3);

	DBG("index[%02X] value[%02X]\n", index, value);
	return value;
}

/**
 * set_indexed_reg - Set indexed register
 * @hwif: for the port address
 * @index: index of the indexed register
 */
static void set_indexed_reg(ide_hwif_t *hwif, u8 index, u8 value)
{
	outb(index, hwif->dma_vendor1);
	outb(value, hwif->dma_vendor3);
	DBG("index[%02X] value[%02X]\n", index, value);
}

/*
 * ATA Timing Tables based on 133 MHz PLL output clock.
 *
 * If the PLL outputs 100 MHz clock, the ASIC hardware will set
 * the timing registers automatically when "set features" command is
 * issued to the device. However, if the PLL output clock is 133 MHz,
 * the following tables must be used.
 */
static struct pio_timing {
	u8 reg0c, reg0d, reg13;
} pio_timings [] = {
	{ 0xfb, 0x2b, 0xac },	/* PIO mode 0, IORDY off, Prefetch off */
	{ 0x46, 0x29, 0xa4 },	/* PIO mode 1, IORDY off, Prefetch off */
	{ 0x23, 0x26, 0x64 },	/* PIO mode 2, IORDY off, Prefetch off */
	{ 0x27, 0x0d, 0x35 },	/* PIO mode 3, IORDY on,  Prefetch off */
	{ 0x23, 0x09, 0x25 },	/* PIO mode 4, IORDY on,  Prefetch off */
};

static struct mwdma_timing {
	u8 reg0e, reg0f;
} mwdma_timings [] = {
	{ 0xdf, 0x5f }, 	/* MWDMA mode 0 */
	{ 0x6b, 0x27 }, 	/* MWDMA mode 1 */
	{ 0x69, 0x25 }, 	/* MWDMA mode 2 */
};

static struct udma_timing {
	u8 reg10, reg11, reg12;
} udma_timings [] = {
	{ 0x4a, 0x0f, 0xd5 },	/* UDMA mode 0 */
	{ 0x3a, 0x0a, 0xd0 },	/* UDMA mode 1 */
	{ 0x2a, 0x07, 0xcd },	/* UDMA mode 2 */
	{ 0x1a, 0x05, 0xcd },	/* UDMA mode 3 */
	{ 0x1a, 0x03, 0xcd },	/* UDMA mode 4 */
	{ 0x1a, 0x02, 0xcb },	/* UDMA mode 5 */
	{ 0x1a, 0x01, 0xcb },	/* UDMA mode 6 */
};

static void pdcnew_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u8 adj			= (drive->dn & 1) ? 0x08 : 0x00;

	/*
	 * IDE core issues SETFEATURES_XFER to the drive first (thanks to
	 * IDE_HFLAG_POST_SET_MODE in ->host_flags).  PDC202xx hardware will
	 * automatically set the timing registers based on 100 MHz PLL output.
	 *
	 * As we set up the PLL to output 133 MHz for UltraDMA/133 capable
	 * chips, we must override the default register settings...
	 */
	if (max_dma_rate(dev) == 4) {
		u8 mode = speed & 0x07;

		if (speed >= XFER_UDMA_0) {
			set_indexed_reg(hwif, 0x10 + adj,
					udma_timings[mode].reg10);
			set_indexed_reg(hwif, 0x11 + adj,
					udma_timings[mode].reg11);
			set_indexed_reg(hwif, 0x12 + adj,
					udma_timings[mode].reg12);
		} else {
			set_indexed_reg(hwif, 0x0e + adj,
					mwdma_timings[mode].reg0e);
			set_indexed_reg(hwif, 0x0f + adj,
					mwdma_timings[mode].reg0f);
		}
	} else if (speed == XFER_UDMA_2) {
		/* Set tHOLD bit to 0 if using UDMA mode 2 */
		u8 tmp = get_indexed_reg(hwif, 0x10 + adj);

		set_indexed_reg(hwif, 0x10 + adj, tmp & 0x7f);
 	}
}

static void pdcnew_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	u8 adj = (drive->dn & 1) ? 0x08 : 0x00;

	if (max_dma_rate(dev) == 4) {
		set_indexed_reg(hwif, 0x0c + adj, pio_timings[pio].reg0c);
		set_indexed_reg(hwif, 0x0d + adj, pio_timings[pio].reg0d);
		set_indexed_reg(hwif, 0x13 + adj, pio_timings[pio].reg13);
	}
}

static u8 pdcnew_cable_detect(ide_hwif_t *hwif)
{
	if (get_indexed_reg(hwif, 0x0b) & 0x04)
		return ATA_CBL_PATA40;
	else
		return ATA_CBL_PATA80;
}

static void pdcnew_quirkproc(ide_drive_t *drive)
{
	const char **list, *model = drive->id->model;

	for (list = pdc_quirk_drives; *list != NULL; list++)
		if (strstr(model, *list) != NULL) {
			drive->quirk_list = 2;
			return;
		}

	drive->quirk_list = 0;
}

static void pdcnew_reset(ide_drive_t *drive)
{
	/*
	 * Deleted this because it is redundant from the caller.
	 */
	printk(KERN_WARNING "pdc202xx_new: %s channel reset.\n",
		HWIF(drive)->channel ? "Secondary" : "Primary");
}

/**
 * read_counter - Read the byte count registers
 * @dma_base: for the port address
 */
static long __devinit read_counter(u32 dma_base)
{
	u32  pri_dma_base = dma_base, sec_dma_base = dma_base + 0x08;
	u8   cnt0, cnt1, cnt2, cnt3;
	long count = 0, last;
	int  retry = 3;

	do {
		last = count;

		/* Read the current count */
		outb(0x20, pri_dma_base + 0x01);
		cnt0 = inb(pri_dma_base + 0x03);
		outb(0x21, pri_dma_base + 0x01);
		cnt1 = inb(pri_dma_base + 0x03);
		outb(0x20, sec_dma_base + 0x01);
		cnt2 = inb(sec_dma_base + 0x03);
		outb(0x21, sec_dma_base + 0x01);
		cnt3 = inb(sec_dma_base + 0x03);

		count = (cnt3 << 23) | (cnt2 << 15) | (cnt1 << 8) | cnt0;

		/*
		 * The 30-bit decrementing counter is read in 4 pieces.
		 * Incorrect value may be read when the most significant bytes
		 * are changing...
		 */
	} while (retry-- && (((last ^ count) & 0x3fff8000) || last < count));

	DBG("cnt0[%02X] cnt1[%02X] cnt2[%02X] cnt3[%02X]\n",
		  cnt0, cnt1, cnt2, cnt3);

	return count;
}

/**
 * detect_pll_input_clock - Detect the PLL input clock in Hz.
 * @dma_base: for the port address
 * E.g. 16949000 on 33 MHz PCI bus, i.e. half of the PCI clock.
 */
static long __devinit detect_pll_input_clock(unsigned long dma_base)
{
	struct timeval start_time, end_time;
	long start_count, end_count;
	long pll_input, usec_elapsed;
	u8 scr1;

	start_count = read_counter(dma_base);
	do_gettimeofday(&start_time);

	/* Start the test mode */
	outb(0x01, dma_base + 0x01);
	scr1 = inb(dma_base + 0x03);
	DBG("scr1[%02X]\n", scr1);
	outb(scr1 | 0x40, dma_base + 0x03);

	/* Let the counter run for 10 ms. */
	mdelay(10);

	end_count = read_counter(dma_base);
	do_gettimeofday(&end_time);

	/* Stop the test mode */
	outb(0x01, dma_base + 0x01);
	scr1 = inb(dma_base + 0x03);
	DBG("scr1[%02X]\n", scr1);
	outb(scr1 & ~0x40, dma_base + 0x03);

	/*
	 * Calculate the input clock in Hz
	 * (the clock counter is 30 bit wide and counts down)
	 */
	usec_elapsed = (end_time.tv_sec - start_time.tv_sec) * 1000000 +
		(end_time.tv_usec - start_time.tv_usec);
	pll_input = ((start_count - end_count) & 0x3fffffff) / 10 *
		(10000000 / usec_elapsed);

	DBG("start[%ld] end[%ld]\n", start_count, end_count);

	return pll_input;
}

#ifdef CONFIG_PPC_PMAC
static void __devinit apple_kiwi_init(struct pci_dev *pdev)
{
	struct device_node *np = pci_device_to_OF_node(pdev);
	u8 conf;

	if (np == NULL || !of_device_is_compatible(np, "kiwi-root"))
		return;

	if (pdev->revision >= 0x03) {
		/* Setup chip magic config stuff (from darwin) */
		pci_read_config_byte (pdev, 0x40, &conf);
		pci_write_config_byte(pdev, 0x40, (conf | 0x01));
	}
}
#endif /* CONFIG_PPC_PMAC */

static unsigned int __devinit init_chipset_pdcnew(struct pci_dev *dev, const char *name)
{
	unsigned long dma_base = pci_resource_start(dev, 4);
	unsigned long sec_dma_base = dma_base + 0x08;
	long pll_input, pll_output, ratio;
	int f, r;
	u8 pll_ctl0, pll_ctl1;

	if (dma_base == 0)
		return -EFAULT;

#ifdef CONFIG_PPC_PMAC
	apple_kiwi_init(dev);
#endif

	/* Calculate the required PLL output frequency */
	switch(max_dma_rate(dev)) {
		case 4: /* it's 133 MHz for Ultra133 chips */
			pll_output = 133333333;
			break;
		case 3: /* and  100 MHz for Ultra100 chips */
		default:
			pll_output = 100000000;
			break;
	}

	/*
	 * Detect PLL input clock.
	 * On some systems, where PCI bus is running at non-standard clock rate
	 * (e.g. 25 or 40 MHz), we have to adjust the cycle time.
	 * PDC20268 and newer chips employ PLL circuit to help correct timing
	 * registers setting.
	 */
	pll_input = detect_pll_input_clock(dma_base);
	printk("%s: PLL input clock is %ld kHz\n", name, pll_input / 1000);

	/* Sanity check */
	if (unlikely(pll_input < 5000000L || pll_input > 70000000L)) {
		printk(KERN_ERR "%s: Bad PLL input clock %ld Hz, giving up!\n",
		       name, pll_input);
		goto out;
	}

#ifdef DEBUG
	DBG("pll_output is %ld Hz\n", pll_output);

	/* Show the current clock value of PLL control register
	 * (maybe already configured by the BIOS)
	 */
	outb(0x02, sec_dma_base + 0x01);
	pll_ctl0 = inb(sec_dma_base + 0x03);
	outb(0x03, sec_dma_base + 0x01);
	pll_ctl1 = inb(sec_dma_base + 0x03);

	DBG("pll_ctl[%02X][%02X]\n", pll_ctl0, pll_ctl1);
#endif

	/*
	 * Calculate the ratio of F, R and NO
	 * POUT = (F + 2) / (( R + 2) * NO)
	 */
	ratio = pll_output / (pll_input / 1000);
	if (ratio < 8600L) { /* 8.6x */
		/* Using NO = 0x01, R = 0x0d */
		r = 0x0d;
	} else if (ratio < 12900L) { /* 12.9x */
		/* Using NO = 0x01, R = 0x08 */
		r = 0x08;
	} else if (ratio < 16100L) { /* 16.1x */
		/* Using NO = 0x01, R = 0x06 */
		r = 0x06;
	} else if (ratio < 64000L) { /* 64x */
		r = 0x00;
	} else {
		/* Invalid ratio */
		printk(KERN_ERR "%s: Bad ratio %ld, giving up!\n", name, ratio);
		goto out;
	}

	f = (ratio * (r + 2)) / 1000 - 2;

	DBG("F[%d] R[%d] ratio*1000[%ld]\n", f, r, ratio);

	if (unlikely(f < 0 || f > 127)) {
		/* Invalid F */
		printk(KERN_ERR "%s: F[%d] invalid!\n", name, f);
		goto out;
	}

	pll_ctl0 = (u8) f;
	pll_ctl1 = (u8) r;

	DBG("Writing pll_ctl[%02X][%02X]\n", pll_ctl0, pll_ctl1);

	outb(0x02,     sec_dma_base + 0x01);
	outb(pll_ctl0, sec_dma_base + 0x03);
	outb(0x03,     sec_dma_base + 0x01);
	outb(pll_ctl1, sec_dma_base + 0x03);

	/* Wait the PLL circuit to be stable */
	mdelay(30);

#ifdef DEBUG
	/*
	 *  Show the current clock value of PLL control register
	 */
	outb(0x02, sec_dma_base + 0x01);
	pll_ctl0 = inb(sec_dma_base + 0x03);
	outb(0x03, sec_dma_base + 0x01);
	pll_ctl1 = inb(sec_dma_base + 0x03);

	DBG("pll_ctl[%02X][%02X]\n", pll_ctl0, pll_ctl1);
#endif

 out:
	return dev->irq;
}

static void __devinit init_hwif_pdc202new(ide_hwif_t *hwif)
{
	hwif->set_pio_mode = &pdcnew_set_pio_mode;
	hwif->set_dma_mode = &pdcnew_set_dma_mode;

	hwif->quirkproc = &pdcnew_quirkproc;
	hwif->resetproc = &pdcnew_reset;

	if (hwif->dma_base == 0)
		return;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT)
		hwif->cbl = pdcnew_cable_detect(hwif);
}

static struct pci_dev * __devinit pdc20270_get_dev2(struct pci_dev *dev)
{
	struct pci_dev *dev2;

	dev2 = pci_get_slot(dev->bus, PCI_DEVFN(PCI_SLOT(dev->devfn) + 1,
						PCI_FUNC(dev->devfn)));

	if (dev2 &&
	    dev2->vendor == dev->vendor &&
	    dev2->device == dev->device) {

		if (dev2->irq != dev->irq) {
			dev2->irq = dev->irq;
			printk(KERN_INFO "PDC20270: PCI config space "
					 "interrupt fixed\n");
		}

		return dev2;
	}

	return NULL;
}

#define DECLARE_PDCNEW_DEV(name_str, udma) \
	{ \
		.name		= name_str, \
		.init_chipset	= init_chipset_pdcnew, \
		.init_hwif	= init_hwif_pdc202new, \
		.host_flags	= IDE_HFLAG_POST_SET_MODE | \
				  IDE_HFLAG_ERROR_STOPS_FIFO | \
				  IDE_HFLAG_OFF_BOARD, \
		.pio_mask	= ATA_PIO4, \
		.mwdma_mask	= ATA_MWDMA2, \
		.udma_mask	= udma, \
	}

static const struct ide_port_info pdcnew_chipsets[] __devinitdata = {
	/* 0 */ DECLARE_PDCNEW_DEV("PDC20268", ATA_UDMA5),
	/* 1 */ DECLARE_PDCNEW_DEV("PDC20269", ATA_UDMA6),
	/* 2 */ DECLARE_PDCNEW_DEV("PDC20270", ATA_UDMA5),
	/* 3 */ DECLARE_PDCNEW_DEV("PDC20271", ATA_UDMA6),
	/* 4 */ DECLARE_PDCNEW_DEV("PDC20275", ATA_UDMA6),
	/* 5 */ DECLARE_PDCNEW_DEV("PDC20276", ATA_UDMA6),
	/* 6 */ DECLARE_PDCNEW_DEV("PDC20277", ATA_UDMA6),
};

/**
 *	pdc202new_init_one	-	called when a pdc202xx is found
 *	@dev: the pdc202new device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit pdc202new_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	const struct ide_port_info *d;
	struct pci_dev *bridge = dev->bus->self;
	u8 idx = id->driver_data;

	d = &pdcnew_chipsets[idx];

	if (idx == 2 && bridge &&
	    bridge->vendor == PCI_VENDOR_ID_DEC &&
	    bridge->device == PCI_DEVICE_ID_DEC_21150) {
		struct pci_dev *dev2;

		if (PCI_SLOT(dev->devfn) & 2)
			return -ENODEV;

		dev2 = pdc20270_get_dev2(dev);

		if (dev2) {
			int ret = ide_setup_pci_devices(dev, dev2, d);
			if (ret < 0)
				pci_dev_put(dev2);
			return ret;
		}
	}

	if (idx == 5 && bridge &&
	    bridge->vendor == PCI_VENDOR_ID_INTEL &&
	    (bridge->device == PCI_DEVICE_ID_INTEL_I960 ||
	     bridge->device == PCI_DEVICE_ID_INTEL_I960RM)) {
		printk(KERN_INFO "PDC20276: attached to I2O RAID controller, "
				 "skipping\n");
		return -ENODEV;
	}

	return ide_setup_pci_device(dev, d);
}

static const struct pci_device_id pdc202new_pci_tbl[] = {
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20268), 0 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20269), 1 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20270), 2 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20271), 3 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20275), 4 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20276), 5 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20277), 6 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, pdc202new_pci_tbl);

static struct pci_driver driver = {
	.name		= "Promise_IDE",
	.id_table	= pdc202new_pci_tbl,
	.probe		= pdc202new_init_one,
};

static int __init pdc202new_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(pdc202new_ide_init);

MODULE_AUTHOR("Andre Hedrick, Frank Tiernan");
MODULE_DESCRIPTION("PCI driver module for Promise PDC20268 and higher");
MODULE_LICENSE("GPL");
