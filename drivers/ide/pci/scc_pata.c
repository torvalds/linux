/*
 * Support for IDE interfaces on Celleb platform
 *
 * (C) Copyright 2006 TOSHIBA CORPORATION
 *
 * This code is based on drivers/ide/pci/siimage.c:
 * Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2003		Red Hat <alan@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#define PCI_DEVICE_ID_TOSHIBA_SCC_ATA            0x01b4

#define SCC_PATA_NAME           "scc IDE"

#define TDVHSEL_MASTER          0x00000001
#define TDVHSEL_SLAVE           0x00000004

#define MODE_JCUSFEN            0x00000080

#define CCKCTRL_ATARESET        0x00040000
#define CCKCTRL_BUFCNT          0x00020000
#define CCKCTRL_CRST            0x00010000
#define CCKCTRL_OCLKEN          0x00000100
#define CCKCTRL_ATACLKOEN       0x00000002
#define CCKCTRL_LCLKEN          0x00000001

#define QCHCD_IOS_SS		0x00000001

#define QCHSD_STPDIAG		0x00020000

#define INTMASK_MSK             0xD1000012
#define INTSTS_SERROR		0x80000000
#define INTSTS_PRERR		0x40000000
#define INTSTS_RERR		0x10000000
#define INTSTS_ICERR		0x01000000
#define INTSTS_BMSINT		0x00000010
#define INTSTS_BMHE		0x00000008
#define INTSTS_IOIRQS           0x00000004
#define INTSTS_INTRQ            0x00000002
#define INTSTS_ACTEINT          0x00000001

#define ECMODE_VALUE 0x01

static struct scc_ports {
	unsigned long ctl, dma;
	ide_hwif_t *hwif;  /* for removing port from system */
} scc_ports[MAX_HWIFS];

/* PIO transfer mode  table */
/* JCHST */
static unsigned long JCHSTtbl[2][7] = {
	{0x0E, 0x05, 0x02, 0x03, 0x02, 0x00, 0x00},   /* 100MHz */
	{0x13, 0x07, 0x04, 0x04, 0x03, 0x00, 0x00}    /* 133MHz */
};

/* JCHHT */
static unsigned long JCHHTtbl[2][7] = {
	{0x0E, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00},   /* 100MHz */
	{0x13, 0x03, 0x03, 0x03, 0x03, 0x00, 0x00}    /* 133MHz */
};

/* JCHCT */
static unsigned long JCHCTtbl[2][7] = {
	{0x1D, 0x1D, 0x1C, 0x0B, 0x06, 0x00, 0x00},   /* 100MHz */
	{0x27, 0x26, 0x26, 0x0E, 0x09, 0x00, 0x00}    /* 133MHz */
};


/* DMA transfer mode  table */
/* JCHDCTM/JCHDCTS */
static unsigned long JCHDCTxtbl[2][7] = {
	{0x0A, 0x06, 0x04, 0x03, 0x01, 0x00, 0x00},   /* 100MHz */
	{0x0E, 0x09, 0x06, 0x04, 0x02, 0x01, 0x00}    /* 133MHz */
};

/* JCSTWTM/JCSTWTS  */
static unsigned long JCSTWTxtbl[2][7] = {
	{0x06, 0x04, 0x03, 0x02, 0x02, 0x02, 0x00},   /* 100MHz */
	{0x09, 0x06, 0x04, 0x02, 0x02, 0x02, 0x02}    /* 133MHz */
};

/* JCTSS */
static unsigned long JCTSStbl[2][7] = {
	{0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x00},   /* 100MHz */
	{0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05}    /* 133MHz */
};

/* JCENVT */
static unsigned long JCENVTtbl[2][7] = {
	{0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00},   /* 100MHz */
	{0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02}    /* 133MHz */
};

/* JCACTSELS/JCACTSELM */
static unsigned long JCACTSELtbl[2][7] = {
	{0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00},   /* 100MHz */
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}    /* 133MHz */
};


static u8 scc_ide_inb(unsigned long port)
{
	u32 data = in_be32((void*)port);
	return (u8)data;
}

static u16 scc_ide_inw(unsigned long port)
{
	u32 data = in_be32((void*)port);
	return (u16)data;
}

static void scc_ide_insw(unsigned long port, void *addr, u32 count)
{
	u16 *ptr = (u16 *)addr;
	while (count--) {
		*ptr++ = le16_to_cpu(in_be32((void*)port));
	}
}

static void scc_ide_insl(unsigned long port, void *addr, u32 count)
{
	u16 *ptr = (u16 *)addr;
	while (count--) {
		*ptr++ = le16_to_cpu(in_be32((void*)port));
		*ptr++ = le16_to_cpu(in_be32((void*)port));
	}
}

static void scc_ide_outb(u8 addr, unsigned long port)
{
	out_be32((void*)port, addr);
}

static void scc_ide_outw(u16 addr, unsigned long port)
{
	out_be32((void*)port, addr);
}

static void
scc_ide_outbsync(ide_drive_t * drive, u8 addr, unsigned long port)
{
	ide_hwif_t *hwif = HWIF(drive);

	out_be32((void*)port, addr);
	eieio();
	in_be32((void*)(hwif->dma_base + 0x01c));
	eieio();
}

static void
scc_ide_outsw(unsigned long port, void *addr, u32 count)
{
	u16 *ptr = (u16 *)addr;
	while (count--) {
		out_be32((void*)port, cpu_to_le16(*ptr++));
	}
}

static void
scc_ide_outsl(unsigned long port, void *addr, u32 count)
{
	u16 *ptr = (u16 *)addr;
	while (count--) {
		out_be32((void*)port, cpu_to_le16(*ptr++));
		out_be32((void*)port, cpu_to_le16(*ptr++));
	}
}

/**
 *	scc_set_pio_mode	-	set host controller for PIO mode
 *	@drive: drive
 *	@pio: PIO mode number
 *
 *	Load the timing settings for this device mode into the
 *	controller.
 */

static void scc_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct scc_ports *ports = ide_get_hwifdata(hwif);
	unsigned long ctl_base = ports->ctl;
	unsigned long cckctrl_port = ctl_base + 0xff0;
	unsigned long piosht_port = ctl_base + 0x000;
	unsigned long pioct_port = ctl_base + 0x004;
	unsigned long reg;
	int offset;

	reg = in_be32((void __iomem *)cckctrl_port);
	if (reg & CCKCTRL_ATACLKOEN) {
		offset = 1; /* 133MHz */
	} else {
		offset = 0; /* 100MHz */
	}
	reg = JCHSTtbl[offset][pio] << 16 | JCHHTtbl[offset][pio];
	out_be32((void __iomem *)piosht_port, reg);
	reg = JCHCTtbl[offset][pio];
	out_be32((void __iomem *)pioct_port, reg);
}

/**
 *	scc_set_dma_mode	-	set host controller for DMA mode
 *	@drive: drive
 *	@speed: DMA mode
 *
 *	Load the timing settings for this device mode into the
 *	controller.
 */

static void scc_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct scc_ports *ports = ide_get_hwifdata(hwif);
	unsigned long ctl_base = ports->ctl;
	unsigned long cckctrl_port = ctl_base + 0xff0;
	unsigned long mdmact_port = ctl_base + 0x008;
	unsigned long mcrcst_port = ctl_base + 0x00c;
	unsigned long sdmact_port = ctl_base + 0x010;
	unsigned long scrcst_port = ctl_base + 0x014;
	unsigned long udenvt_port = ctl_base + 0x018;
	unsigned long tdvhsel_port   = ctl_base + 0x020;
	int is_slave = (&hwif->drives[1] == drive);
	int offset, idx;
	unsigned long reg;
	unsigned long jcactsel;

	reg = in_be32((void __iomem *)cckctrl_port);
	if (reg & CCKCTRL_ATACLKOEN) {
		offset = 1; /* 133MHz */
	} else {
		offset = 0; /* 100MHz */
	}

	idx = speed - XFER_UDMA_0;

	jcactsel = JCACTSELtbl[offset][idx];
	if (is_slave) {
		out_be32((void __iomem *)sdmact_port, JCHDCTxtbl[offset][idx]);
		out_be32((void __iomem *)scrcst_port, JCSTWTxtbl[offset][idx]);
		jcactsel = jcactsel << 2;
		out_be32((void __iomem *)tdvhsel_port, (in_be32((void __iomem *)tdvhsel_port) & ~TDVHSEL_SLAVE) | jcactsel);
	} else {
		out_be32((void __iomem *)mdmact_port, JCHDCTxtbl[offset][idx]);
		out_be32((void __iomem *)mcrcst_port, JCSTWTxtbl[offset][idx]);
		out_be32((void __iomem *)tdvhsel_port, (in_be32((void __iomem *)tdvhsel_port) & ~TDVHSEL_MASTER) | jcactsel);
	}
	reg = JCTSStbl[offset][idx] << 16 | JCENVTtbl[offset][idx];
	out_be32((void __iomem *)udenvt_port, reg);
}

/**
 *	scc_ide_dma_setup	-	begin a DMA phase
 *	@drive: target device
 *
 *	Build an IDE DMA PRD (IDE speak for scatter gather table)
 *	and then set up the DMA transfer registers.
 *
 *	Returns 0 on success. If a PIO fallback is required then 1
 *	is returned.
 */

static int scc_dma_setup(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct request *rq = HWGROUP(drive)->rq;
	unsigned int reading;
	u8 dma_stat;

	if (rq_data_dir(rq))
		reading = 0;
	else
		reading = 1 << 3;

	/* fall back to pio! */
	if (!ide_build_dmatable(drive, rq)) {
		ide_map_sg(drive, rq);
		return 1;
	}

	/* PRD table */
	out_be32((void __iomem *)hwif->dma_prdtable, hwif->dmatable_dma);

	/* specify r/w */
	out_be32((void __iomem *)hwif->dma_command, reading);

	/* read dma_status for INTR & ERROR flags */
	dma_stat = in_be32((void __iomem *)hwif->dma_status);

	/* clear INTR & ERROR flags */
	out_be32((void __iomem *)hwif->dma_status, dma_stat|6);
	drive->waiting_for_dma = 1;
	return 0;
}


/**
 *	scc_dma_end	-	Stop DMA
 *	@drive: IDE drive
 *
 *	Check and clear INT Status register.
 *      Then call __ide_dma_end().
 */

static int scc_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long intsts_port = hwif->dma_base + 0x014;
	u32 reg;
	int dma_stat, data_loss = 0;
	static int retry = 0;

	/* errata A308 workaround: Step5 (check data loss) */
	/* We don't check non ide_disk because it is limited to UDMA4 */
	if (!(in_be32((void __iomem *)hwif->io_ports.ctl_addr)
	      & ERR_STAT) &&
	    drive->media == ide_disk && drive->current_speed > XFER_UDMA_4) {
		reg = in_be32((void __iomem *)intsts_port);
		if (!(reg & INTSTS_ACTEINT)) {
			printk(KERN_WARNING "%s: operation failed (transfer data loss)\n",
			       drive->name);
			data_loss = 1;
			if (retry++) {
				struct request *rq = HWGROUP(drive)->rq;
				int unit;
				/* ERROR_RESET and drive->crc_count are needed
				 * to reduce DMA transfer mode in retry process.
				 */
				if (rq)
					rq->errors |= ERROR_RESET;
				for (unit = 0; unit < MAX_DRIVES; unit++) {
					ide_drive_t *drive = &hwif->drives[unit];
					drive->crc_count++;
				}
			}
		}
	}

	while (1) {
		reg = in_be32((void __iomem *)intsts_port);

		if (reg & INTSTS_SERROR) {
			printk(KERN_WARNING "%s: SERROR\n", SCC_PATA_NAME);
			out_be32((void __iomem *)intsts_port, INTSTS_SERROR|INTSTS_BMSINT);

			out_be32((void __iomem *)hwif->dma_command, in_be32((void __iomem *)hwif->dma_command) & ~QCHCD_IOS_SS);
			continue;
		}

		if (reg & INTSTS_PRERR) {
			u32 maea0, maec0;
			unsigned long ctl_base = hwif->config_data;

			maea0 = in_be32((void __iomem *)(ctl_base + 0xF50));
			maec0 = in_be32((void __iomem *)(ctl_base + 0xF54));

			printk(KERN_WARNING "%s: PRERR [addr:%x cmd:%x]\n", SCC_PATA_NAME, maea0, maec0);

			out_be32((void __iomem *)intsts_port, INTSTS_PRERR|INTSTS_BMSINT);

			out_be32((void __iomem *)hwif->dma_command, in_be32((void __iomem *)hwif->dma_command) & ~QCHCD_IOS_SS);
			continue;
		}

		if (reg & INTSTS_RERR) {
			printk(KERN_WARNING "%s: Response Error\n", SCC_PATA_NAME);
			out_be32((void __iomem *)intsts_port, INTSTS_RERR|INTSTS_BMSINT);

			out_be32((void __iomem *)hwif->dma_command, in_be32((void __iomem *)hwif->dma_command) & ~QCHCD_IOS_SS);
			continue;
		}

		if (reg & INTSTS_ICERR) {
			out_be32((void __iomem *)hwif->dma_command, in_be32((void __iomem *)hwif->dma_command) & ~QCHCD_IOS_SS);

			printk(KERN_WARNING "%s: Illegal Configuration\n", SCC_PATA_NAME);
			out_be32((void __iomem *)intsts_port, INTSTS_ICERR|INTSTS_BMSINT);
			continue;
		}

		if (reg & INTSTS_BMSINT) {
			printk(KERN_WARNING "%s: Internal Bus Error\n", SCC_PATA_NAME);
			out_be32((void __iomem *)intsts_port, INTSTS_BMSINT);

			ide_do_reset(drive);
			continue;
		}

		if (reg & INTSTS_BMHE) {
			out_be32((void __iomem *)intsts_port, INTSTS_BMHE);
			continue;
		}

		if (reg & INTSTS_ACTEINT) {
			out_be32((void __iomem *)intsts_port, INTSTS_ACTEINT);
			continue;
		}

		if (reg & INTSTS_IOIRQS) {
			out_be32((void __iomem *)intsts_port, INTSTS_IOIRQS);
			continue;
		}
		break;
	}

	dma_stat = __ide_dma_end(drive);
	if (data_loss)
		dma_stat |= 2; /* emulate DMA error (to retry command) */
	return dma_stat;
}

/* returns 1 if dma irq issued, 0 otherwise */
static int scc_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u32 int_stat = in_be32((void __iomem *)hwif->dma_base + 0x014);

	/* SCC errata A252,A308 workaround: Step4 */
	if ((in_be32((void __iomem *)hwif->io_ports.ctl_addr)
	     & ERR_STAT) &&
	    (int_stat & INTSTS_INTRQ))
		return 1;

	/* SCC errata A308 workaround: Step5 (polling IOIRQS) */
	if (int_stat & INTSTS_IOIRQS)
		return 1;

	if (!drive->waiting_for_dma)
		printk(KERN_WARNING "%s: (%s) called while not waiting\n",
			drive->name, __func__);
	return 0;
}

static u8 scc_udma_filter(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 mask = hwif->ultra_mask;

	/* errata A308 workaround: limit non ide_disk drive to UDMA4 */
	if ((drive->media != ide_disk) && (mask & 0xE0)) {
		printk(KERN_INFO "%s: limit %s to UDMA4\n",
		       SCC_PATA_NAME, drive->name);
		mask = ATA_UDMA4;
	}

	return mask;
}

/**
 *	setup_mmio_scc	-	map CTRL/BMID region
 *	@dev: PCI device we are configuring
 *	@name: device name
 *
 */

static int setup_mmio_scc (struct pci_dev *dev, const char *name)
{
	unsigned long ctl_base = pci_resource_start(dev, 0);
	unsigned long dma_base = pci_resource_start(dev, 1);
	unsigned long ctl_size = pci_resource_len(dev, 0);
	unsigned long dma_size = pci_resource_len(dev, 1);
	void __iomem *ctl_addr;
	void __iomem *dma_addr;
	int i, ret;

	for (i = 0; i < MAX_HWIFS; i++) {
		if (scc_ports[i].ctl == 0)
			break;
	}
	if (i >= MAX_HWIFS)
		return -ENOMEM;

	ret = pci_request_selected_regions(dev, (1 << 2) - 1, name);
	if (ret < 0) {
		printk(KERN_ERR "%s: can't reserve resources\n", name);
		return ret;
	}

	if ((ctl_addr = ioremap(ctl_base, ctl_size)) == NULL)
		goto fail_0;

	if ((dma_addr = ioremap(dma_base, dma_size)) == NULL)
		goto fail_1;

	pci_set_master(dev);
	scc_ports[i].ctl = (unsigned long)ctl_addr;
	scc_ports[i].dma = (unsigned long)dma_addr;
	pci_set_drvdata(dev, (void *) &scc_ports[i]);

	return 1;

 fail_1:
	iounmap(ctl_addr);
 fail_0:
	return -ENOMEM;
}

static int scc_ide_setup_pci_device(struct pci_dev *dev,
				    const struct ide_port_info *d)
{
	struct scc_ports *ports = pci_get_drvdata(dev);
	ide_hwif_t *hwif = NULL;
	hw_regs_t hw;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };
	int i;

	hwif = ide_find_port();
	if (hwif == NULL) {
		printk(KERN_ERR "%s: too many IDE interfaces, "
				"no room in table\n", SCC_PATA_NAME);
		return -ENOMEM;
	}

	memset(&hw, 0, sizeof(hw));
	for (i = 0; i <= 8; i++)
		hw.io_ports_array[i] = ports->dma + 0x20 + i * 4;
	hw.irq = dev->irq;
	hw.dev = &dev->dev;
	hw.chipset = ide_pci;
	ide_init_port_hw(hwif, &hw);
	hwif->dev = &dev->dev;

	idx[0] = hwif->index;

	ide_device_add(idx, d);

	return 0;
}

/**
 *	init_setup_scc	-	set up an SCC PATA Controller
 *	@dev: PCI device
 *	@d: IDE port info
 *
 *	Perform the initial set up for this device.
 */

static int __devinit init_setup_scc(struct pci_dev *dev,
				    const struct ide_port_info *d)
{
	unsigned long ctl_base;
	unsigned long dma_base;
	unsigned long cckctrl_port;
	unsigned long intmask_port;
	unsigned long mode_port;
	unsigned long ecmode_port;
	unsigned long dma_status_port;
	u32 reg = 0;
	struct scc_ports *ports;
	int rc;

	rc = pci_enable_device(dev);
	if (rc)
		goto end;

	rc = setup_mmio_scc(dev, d->name);
	if (rc < 0)
		goto end;

	ports = pci_get_drvdata(dev);
	ctl_base = ports->ctl;
	dma_base = ports->dma;
	cckctrl_port = ctl_base + 0xff0;
	intmask_port = dma_base + 0x010;
	mode_port = ctl_base + 0x024;
	ecmode_port = ctl_base + 0xf00;
	dma_status_port = dma_base + 0x004;

	/* controller initialization */
	reg = 0;
	out_be32((void*)cckctrl_port, reg);
	reg |= CCKCTRL_ATACLKOEN;
	out_be32((void*)cckctrl_port, reg);
	reg |= CCKCTRL_LCLKEN | CCKCTRL_OCLKEN;
	out_be32((void*)cckctrl_port, reg);
	reg |= CCKCTRL_CRST;
	out_be32((void*)cckctrl_port, reg);

	for (;;) {
		reg = in_be32((void*)cckctrl_port);
		if (reg & CCKCTRL_CRST)
			break;
		udelay(5000);
	}

	reg |= CCKCTRL_ATARESET;
	out_be32((void*)cckctrl_port, reg);

	out_be32((void*)ecmode_port, ECMODE_VALUE);
	out_be32((void*)mode_port, MODE_JCUSFEN);
	out_be32((void*)intmask_port, INTMASK_MSK);

	rc = scc_ide_setup_pci_device(dev, d);

 end:
	return rc;
}

/**
 *	init_mmio_iops_scc	-	set up the iops for MMIO
 *	@hwif: interface to set up
 *
 */

static void __devinit init_mmio_iops_scc(ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct scc_ports *ports = pci_get_drvdata(dev);
	unsigned long dma_base = ports->dma;

	ide_set_hwifdata(hwif, ports);

	hwif->INB = scc_ide_inb;
	hwif->INW = scc_ide_inw;
	hwif->INSW = scc_ide_insw;
	hwif->INSL = scc_ide_insl;
	hwif->OUTB = scc_ide_outb;
	hwif->OUTBSYNC = scc_ide_outbsync;
	hwif->OUTW = scc_ide_outw;
	hwif->OUTSW = scc_ide_outsw;
	hwif->OUTSL = scc_ide_outsl;

	hwif->dma_base = dma_base;
	hwif->config_data = ports->ctl;
	hwif->mmio = 1;
}

/**
 *	init_iops_scc	-	set up iops
 *	@hwif: interface to set up
 *
 *	Do the basic setup for the SCC hardware interface
 *	and then do the MMIO setup.
 */

static void __devinit init_iops_scc(ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);

	hwif->hwif_data = NULL;
	if (pci_get_drvdata(dev) == NULL)
		return;
	init_mmio_iops_scc(hwif);
}

static u8 __devinit scc_cable_detect(ide_hwif_t *hwif)
{
	return ATA_CBL_PATA80;
}

/**
 *	init_hwif_scc	-	set up hwif
 *	@hwif: interface to set up
 *
 *	We do the basic set up of the interface structure. The SCC
 *	requires several custom handlers so we override the default
 *	ide DMA handlers appropriately.
 */

static void __devinit init_hwif_scc(ide_hwif_t *hwif)
{
	struct scc_ports *ports = ide_get_hwifdata(hwif);

	ports->hwif = hwif;

	hwif->dma_command = hwif->dma_base;
	hwif->dma_status = hwif->dma_base + 0x04;
	hwif->dma_prdtable = hwif->dma_base + 0x08;

	/* PTERADD */
	out_be32((void __iomem *)(hwif->dma_base + 0x018), hwif->dmatable_dma);

	if (in_be32((void __iomem *)(hwif->config_data + 0xff0)) & CCKCTRL_ATACLKOEN)
		hwif->ultra_mask = ATA_UDMA6; /* 133MHz */
	else
		hwif->ultra_mask = ATA_UDMA5; /* 100MHz */
}

static const struct ide_port_ops scc_port_ops = {
	.set_pio_mode		= scc_set_pio_mode,
	.set_dma_mode		= scc_set_dma_mode,
	.udma_filter		= scc_udma_filter,
	.cable_detect		= scc_cable_detect,
};

static const struct ide_dma_ops scc_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= scc_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= ide_dma_start,
	.dma_end		= scc_dma_end,
	.dma_test_irq		= scc_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timeout		= ide_dma_timeout,
};

#define DECLARE_SCC_DEV(name_str)			\
  {							\
      .name		= name_str,			\
      .init_iops	= init_iops_scc,		\
      .init_hwif	= init_hwif_scc,		\
      .port_ops		= &scc_port_ops,		\
      .dma_ops		= &scc_dma_ops,			\
      .host_flags	= IDE_HFLAG_SINGLE,		\
      .pio_mask		= ATA_PIO4,			\
  }

static const struct ide_port_info scc_chipsets[] __devinitdata = {
	/* 0 */ DECLARE_SCC_DEV("sccIDE"),
};

/**
 *	scc_init_one	-	pci layer discovery entry
 *	@dev: PCI device
 *	@id: ident table entry
 *
 *	Called by the PCI code when it finds an SCC PATA controller.
 *	We then use the IDE PCI generic helper to do most of the work.
 */

static int __devinit scc_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return init_setup_scc(dev, &scc_chipsets[id->driver_data]);
}

/**
 *	scc_remove	-	pci layer remove entry
 *	@dev: PCI device
 *
 *	Called by the PCI code when it removes an SCC PATA controller.
 */

static void __devexit scc_remove(struct pci_dev *dev)
{
	struct scc_ports *ports = pci_get_drvdata(dev);
	ide_hwif_t *hwif = ports->hwif;

	if (hwif->dmatable_cpu) {
		pci_free_consistent(dev, PRD_ENTRIES * PRD_BYTES,
				    hwif->dmatable_cpu, hwif->dmatable_dma);
		hwif->dmatable_cpu = NULL;
	}

	ide_unregister(hwif);

	iounmap((void*)ports->dma);
	iounmap((void*)ports->ctl);
	pci_release_selected_regions(dev, (1 << 2) - 1);
	memset(ports, 0, sizeof(*ports));
}

static const struct pci_device_id scc_pci_tbl[] = {
	{ PCI_VDEVICE(TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_SCC_ATA), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, scc_pci_tbl);

static struct pci_driver driver = {
	.name = "SCC IDE",
	.id_table = scc_pci_tbl,
	.probe = scc_init_one,
	.remove = scc_remove,
};

static int scc_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(scc_ide_init);
/* -- No exit code?
static void scc_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}
module_exit(scc_ide_exit);
 */


MODULE_DESCRIPTION("PCI driver module for Toshiba SCC IDE");
MODULE_LICENSE("GPL");
