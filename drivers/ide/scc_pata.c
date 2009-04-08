/*
 * Support for IDE interfaces on Celleb platform
 *
 * (C) Copyright 2006 TOSHIBA CORPORATION
 *
 * This code is based on drivers/ide/pci/siimage.c:
 * Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2003		Red Hat
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
	struct ide_host *host;	/* for removing port from system */
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

static void scc_exec_command(ide_hwif_t *hwif, u8 cmd)
{
	out_be32((void *)hwif->io_ports.command_addr, cmd);
	eieio();
	in_be32((void *)(hwif->dma_base + 0x01c));
	eieio();
}

static u8 scc_read_status(ide_hwif_t *hwif)
{
	return (u8)in_be32((void *)hwif->io_ports.status_addr);
}

static u8 scc_read_altstatus(ide_hwif_t *hwif)
{
	return (u8)in_be32((void *)hwif->io_ports.ctl_addr);
}

static u8 scc_dma_sff_read_status(ide_hwif_t *hwif)
{
	return (u8)in_be32((void *)(hwif->dma_base + 4));
}

static void scc_write_devctl(ide_hwif_t *hwif, u8 ctl)
{
	out_be32((void *)hwif->io_ports.ctl_addr, ctl);
	eieio();
	in_be32((void *)(hwif->dma_base + 0x01c));
	eieio();
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
	ide_hwif_t *hwif = drive->hwif;
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
	ide_hwif_t *hwif = drive->hwif;
	struct scc_ports *ports = ide_get_hwifdata(hwif);
	unsigned long ctl_base = ports->ctl;
	unsigned long cckctrl_port = ctl_base + 0xff0;
	unsigned long mdmact_port = ctl_base + 0x008;
	unsigned long mcrcst_port = ctl_base + 0x00c;
	unsigned long sdmact_port = ctl_base + 0x010;
	unsigned long scrcst_port = ctl_base + 0x014;
	unsigned long udenvt_port = ctl_base + 0x018;
	unsigned long tdvhsel_port   = ctl_base + 0x020;
	int is_slave = drive->dn & 1;
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

static void scc_dma_host_set(ide_drive_t *drive, int on)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 unit = drive->dn & 1;
	u8 dma_stat = scc_dma_sff_read_status(hwif);

	if (on)
		dma_stat |= (1 << (5 + unit));
	else
		dma_stat &= ~(1 << (5 + unit));

	scc_ide_outb(dma_stat, hwif->dma_base + 4);
}

/**
 *	scc_dma_setup	-	begin a DMA phase
 *	@drive: target device
 *	@cmd: command
 *
 *	Build an IDE DMA PRD (IDE speak for scatter gather table)
 *	and then set up the DMA transfer registers.
 *
 *	Returns 0 on success. If a PIO fallback is required then 1
 *	is returned.
 */

static int scc_dma_setup(ide_drive_t *drive, struct ide_cmd *cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	u32 rw = (cmd->tf_flags & IDE_TFLAG_WRITE) ? 0 : ATA_DMA_WR;
	u8 dma_stat;

	/* fall back to pio! */
	if (ide_build_dmatable(drive, cmd) == 0)
		return 1;

	/* PRD table */
	out_be32((void __iomem *)(hwif->dma_base + 8), hwif->dmatable_dma);

	/* specify r/w */
	out_be32((void __iomem *)hwif->dma_base, rw);

	/* read DMA status for INTR & ERROR flags */
	dma_stat = scc_dma_sff_read_status(hwif);

	/* clear INTR & ERROR flags */
	out_be32((void __iomem *)(hwif->dma_base + 4), dma_stat | 6);

	return 0;
}

static void scc_dma_start(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 dma_cmd = scc_ide_inb(hwif->dma_base);

	/* start DMA */
	scc_ide_outb(dma_cmd | 1, hwif->dma_base);
}

static int __scc_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u8 dma_stat, dma_cmd;

	/* get DMA command mode */
	dma_cmd = scc_ide_inb(hwif->dma_base);
	/* stop DMA */
	scc_ide_outb(dma_cmd & ~1, hwif->dma_base);
	/* get DMA status */
	dma_stat = scc_dma_sff_read_status(hwif);
	/* clear the INTR & ERROR bits */
	scc_ide_outb(dma_stat | 6, hwif->dma_base + 4);
	/* verify good DMA status */
	return (dma_stat & 7) != 4 ? (0x10 | dma_stat) : 0;
}

/**
 *	scc_dma_end	-	Stop DMA
 *	@drive: IDE drive
 *
 *	Check and clear INT Status register.
 *	Then call __scc_dma_end().
 */

static int scc_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	void __iomem *dma_base = (void __iomem *)hwif->dma_base;
	unsigned long intsts_port = hwif->dma_base + 0x014;
	u32 reg;
	int dma_stat, data_loss = 0;
	static int retry = 0;

	/* errata A308 workaround: Step5 (check data loss) */
	/* We don't check non ide_disk because it is limited to UDMA4 */
	if (!(in_be32((void __iomem *)hwif->io_ports.ctl_addr)
	      & ATA_ERR) &&
	    drive->media == ide_disk && drive->current_speed > XFER_UDMA_4) {
		reg = in_be32((void __iomem *)intsts_port);
		if (!(reg & INTSTS_ACTEINT)) {
			printk(KERN_WARNING "%s: operation failed (transfer data loss)\n",
			       drive->name);
			data_loss = 1;
			if (retry++) {
				struct request *rq = hwif->rq;
				ide_drive_t *drive;
				int i;

				/* ERROR_RESET and drive->crc_count are needed
				 * to reduce DMA transfer mode in retry process.
				 */
				if (rq)
					rq->errors |= ERROR_RESET;

				ide_port_for_each_dev(i, drive, hwif)
					drive->crc_count++;
			}
		}
	}

	while (1) {
		reg = in_be32((void __iomem *)intsts_port);

		if (reg & INTSTS_SERROR) {
			printk(KERN_WARNING "%s: SERROR\n", SCC_PATA_NAME);
			out_be32((void __iomem *)intsts_port, INTSTS_SERROR|INTSTS_BMSINT);

			out_be32(dma_base, in_be32(dma_base) & ~QCHCD_IOS_SS);
			continue;
		}

		if (reg & INTSTS_PRERR) {
			u32 maea0, maec0;
			unsigned long ctl_base = hwif->config_data;

			maea0 = in_be32((void __iomem *)(ctl_base + 0xF50));
			maec0 = in_be32((void __iomem *)(ctl_base + 0xF54));

			printk(KERN_WARNING "%s: PRERR [addr:%x cmd:%x]\n", SCC_PATA_NAME, maea0, maec0);

			out_be32((void __iomem *)intsts_port, INTSTS_PRERR|INTSTS_BMSINT);

			out_be32(dma_base, in_be32(dma_base) & ~QCHCD_IOS_SS);
			continue;
		}

		if (reg & INTSTS_RERR) {
			printk(KERN_WARNING "%s: Response Error\n", SCC_PATA_NAME);
			out_be32((void __iomem *)intsts_port, INTSTS_RERR|INTSTS_BMSINT);

			out_be32(dma_base, in_be32(dma_base) & ~QCHCD_IOS_SS);
			continue;
		}

		if (reg & INTSTS_ICERR) {
			out_be32(dma_base, in_be32(dma_base) & ~QCHCD_IOS_SS);

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

	dma_stat = __scc_dma_end(drive);
	if (data_loss)
		dma_stat |= 2; /* emulate DMA error (to retry command) */
	return dma_stat;
}

/* returns 1 if dma irq issued, 0 otherwise */
static int scc_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	u32 int_stat = in_be32((void __iomem *)hwif->dma_base + 0x014);

	/* SCC errata A252,A308 workaround: Step4 */
	if ((in_be32((void __iomem *)hwif->io_ports.ctl_addr)
	     & ATA_ERR) &&
	    (int_stat & INTSTS_INTRQ))
		return 1;

	/* SCC errata A308 workaround: Step5 (polling IOIRQS) */
	if (int_stat & INTSTS_IOIRQS)
		return 1;

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

	ctl_addr = pci_ioremap_bar(dev, 0);
	if (!ctl_addr)
		goto fail_0;

	dma_addr = pci_ioremap_bar(dev, 1);
	if (!dma_addr)
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
	struct ide_host *host;
	hw_regs_t hw, *hws[] = { &hw, NULL, NULL, NULL };
	int i, rc;

	memset(&hw, 0, sizeof(hw));
	for (i = 0; i <= 8; i++)
		hw.io_ports_array[i] = ports->dma + 0x20 + i * 4;
	hw.irq = dev->irq;
	hw.dev = &dev->dev;
	hw.chipset = ide_pci;

	rc = ide_host_add(d, hws, &host);
	if (rc)
		return rc;

	ports->host = host;

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

static void scc_tf_load(ide_drive_t *drive, struct ide_cmd *cmd)
{
	struct ide_io_ports *io_ports = &drive->hwif->io_ports;
	struct ide_taskfile *tf = &cmd->hob;
	u8 valid = cmd->valid.out.hob;

	if (valid & IDE_VALID_FEATURE)
		scc_ide_outb(tf->feature, io_ports->feature_addr);
	if (valid & IDE_VALID_NSECT)
		scc_ide_outb(tf->nsect, io_ports->nsect_addr);
	if (valid & IDE_VALID_LBAL)
		scc_ide_outb(tf->lbal, io_ports->lbal_addr);
	if (valid & IDE_VALID_LBAM)
		scc_ide_outb(tf->lbam, io_ports->lbam_addr);
	if (valid & IDE_VALID_LBAH)
		scc_ide_outb(tf->lbah, io_ports->lbah_addr);

	tf = &cmd->tf;
	valid = cmd->valid.out.tf;

	if (valid & IDE_VALID_FEATURE)
		scc_ide_outb(tf->feature, io_ports->feature_addr);
	if (valid & IDE_VALID_NSECT)
		scc_ide_outb(tf->nsect, io_ports->nsect_addr);
	if (valid & IDE_VALID_LBAL)
		scc_ide_outb(tf->lbal, io_ports->lbal_addr);
	if (valid & IDE_VALID_LBAM)
		scc_ide_outb(tf->lbam, io_ports->lbam_addr);
	if (valid & IDE_VALID_LBAH)
		scc_ide_outb(tf->lbah, io_ports->lbah_addr);
	if (valid & IDE_VALID_DEVICE)
		scc_ide_outb(tf->device, io_ports->device_addr);
}

static void scc_tf_read(ide_drive_t *drive, struct ide_cmd *cmd)
{
	struct ide_io_ports *io_ports = &drive->hwif->io_ports;
	struct ide_taskfile *tf = &cmd->tf;
	u8 valid = cmd->valid.in.tf;

	/* be sure we're looking at the low order bits */
	scc_ide_outb(ATA_DEVCTL_OBS, io_ports->ctl_addr);

	if (valid & IDE_VALID_ERROR)
		tf->error  = scc_ide_inb(io_ports->feature_addr);
	if (valid & IDE_VALID_NSECT)
		tf->nsect  = scc_ide_inb(io_ports->nsect_addr);
	if (valid & IDE_VALID_LBAL)
		tf->lbal   = scc_ide_inb(io_ports->lbal_addr);
	if (valid & IDE_VALID_LBAM)
		tf->lbam   = scc_ide_inb(io_ports->lbam_addr);
	if (valid & IDE_VALID_LBAH)
		tf->lbah   = scc_ide_inb(io_ports->lbah_addr);
	if (valid & IDE_VALID_DEVICE)
		tf->device = scc_ide_inb(io_ports->device_addr);

	if (cmd->tf_flags & IDE_TFLAG_LBA48) {
		scc_ide_outb(ATA_HOB | ATA_DEVCTL_OBS, io_ports->ctl_addr);

		tf = &cmd->hob;
		valid = cmd->valid.in.hob;

		if (valid & IDE_VALID_ERROR)
			tf->error = scc_ide_inb(io_ports->feature_addr);
		if (valid & IDE_VALID_NSECT)
			tf->nsect = scc_ide_inb(io_ports->nsect_addr);
		if (valid & IDE_VALID_LBAL)
			tf->lbal  = scc_ide_inb(io_ports->lbal_addr);
		if (valid & IDE_VALID_LBAM)
			tf->lbam  = scc_ide_inb(io_ports->lbam_addr);
		if (valid & IDE_VALID_LBAH)
			tf->lbah  = scc_ide_inb(io_ports->lbah_addr);
	}
}

static void scc_input_data(ide_drive_t *drive, struct ide_cmd *cmd,
			   void *buf, unsigned int len)
{
	unsigned long data_addr = drive->hwif->io_ports.data_addr;

	len++;

	if (drive->io_32bit) {
		scc_ide_insl(data_addr, buf, len / 4);

		if ((len & 3) >= 2)
			scc_ide_insw(data_addr, (u8 *)buf + (len & ~3), 1);
	} else
		scc_ide_insw(data_addr, buf, len / 2);
}

static void scc_output_data(ide_drive_t *drive,  struct ide_cmd *cmd,
			    void *buf, unsigned int len)
{
	unsigned long data_addr = drive->hwif->io_ports.data_addr;

	len++;

	if (drive->io_32bit) {
		scc_ide_outsl(data_addr, buf, len / 4);

		if ((len & 3) >= 2)
			scc_ide_outsw(data_addr, (u8 *)buf + (len & ~3), 1);
	} else
		scc_ide_outsw(data_addr, buf, len / 2);
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

	hwif->dma_base = dma_base;
	hwif->config_data = ports->ctl;
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

static int __devinit scc_init_dma(ide_hwif_t *hwif,
				  const struct ide_port_info *d)
{
	return ide_allocate_dma_engine(hwif);
}

static u8 scc_cable_detect(ide_hwif_t *hwif)
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
	/* PTERADD */
	out_be32((void __iomem *)(hwif->dma_base + 0x018), hwif->dmatable_dma);

	if (in_be32((void __iomem *)(hwif->config_data + 0xff0)) & CCKCTRL_ATACLKOEN)
		hwif->ultra_mask = ATA_UDMA6; /* 133MHz */
	else
		hwif->ultra_mask = ATA_UDMA5; /* 100MHz */
}

static const struct ide_tp_ops scc_tp_ops = {
	.exec_command		= scc_exec_command,
	.read_status		= scc_read_status,
	.read_altstatus		= scc_read_altstatus,
	.write_devctl		= scc_write_devctl,

	.dev_select		= ide_dev_select,
	.tf_load		= scc_tf_load,
	.tf_read		= scc_tf_read,

	.input_data		= scc_input_data,
	.output_data		= scc_output_data,
};

static const struct ide_port_ops scc_port_ops = {
	.set_pio_mode		= scc_set_pio_mode,
	.set_dma_mode		= scc_set_dma_mode,
	.udma_filter		= scc_udma_filter,
	.cable_detect		= scc_cable_detect,
};

static const struct ide_dma_ops scc_dma_ops = {
	.dma_host_set		= scc_dma_host_set,
	.dma_setup		= scc_dma_setup,
	.dma_start		= scc_dma_start,
	.dma_end		= scc_dma_end,
	.dma_test_irq		= scc_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timer_expiry	= ide_dma_sff_timer_expiry,
	.dma_sff_read_status	= scc_dma_sff_read_status,
};

static const struct ide_port_info scc_chipset __devinitdata = {
	.name		= "sccIDE",
	.init_iops	= init_iops_scc,
	.init_dma	= scc_init_dma,
	.init_hwif	= init_hwif_scc,
	.tp_ops		= &scc_tp_ops,
	.port_ops	= &scc_port_ops,
	.dma_ops	= &scc_dma_ops,
	.host_flags	= IDE_HFLAG_SINGLE,
	.irq_flags	= IRQF_SHARED,
	.pio_mask	= ATA_PIO4,
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
	return init_setup_scc(dev, &scc_chipset);
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
	struct ide_host *host = ports->host;

	ide_host_remove(host);

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

static struct pci_driver scc_pci_driver = {
	.name = "SCC IDE",
	.id_table = scc_pci_tbl,
	.probe = scc_init_one,
	.remove = __devexit_p(scc_remove),
};

static int scc_ide_init(void)
{
	return ide_pci_register_driver(&scc_pci_driver);
}

module_init(scc_ide_init);
/* -- No exit code?
static void scc_ide_exit(void)
{
	ide_pci_unregister_driver(&scc_pci_driver);
}
module_exit(scc_ide_exit);
 */


MODULE_DESCRIPTION("PCI driver module for Toshiba SCC IDE");
MODULE_LICENSE("GPL");
