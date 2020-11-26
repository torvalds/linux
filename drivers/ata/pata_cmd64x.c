// SPDX-License-Identifier: GPL-2.0-only
/*
 * pata_cmd64x.c 	- CMD64x PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@lxorguk.ukuu.org.uk>
 *			  (C) 2009-2010 Bartlomiej Zolnierkiewicz
 *			  (C) 2012 MontaVista Software, LLC <source@mvista.com>
 *
 * Based upon
 * linux/drivers/ide/pci/cmd64x.c		Version 1.30	Sept 10, 2002
 *
 * cmd64x.c: Enable interrupts at initialization time on Ultra/PCI machines.
 *           Note, this driver is not used at all on other systems because
 *           there the "BIOS" has done all of the following already.
 *           Due to massive hardware bugs, UltraDMA is only supported
 *           on the 646U2 and not on the 646U.
 *
 * Copyright (C) 1998		Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998		David S. Miller (davem@redhat.com)
 *
 * Copyright (C) 1999-2002	Andre Hedrick <andre@linux-ide.org>
 *
 * TODO
 *	Testing work
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_cmd64x"
#define DRV_VERSION "0.2.18"

/*
 * CMD64x specific registers definition.
 */

enum {
	CFR 		= 0x50,
		CFR_INTR_CH0  = 0x04,
	CNTRL		= 0x51,
		CNTRL_CH0     = 0x04,
		CNTRL_CH1     = 0x08,
	CMDTIM 		= 0x52,
	ARTTIM0 	= 0x53,
	DRWTIM0 	= 0x54,
	ARTTIM1 	= 0x55,
	DRWTIM1 	= 0x56,
	ARTTIM23 	= 0x57,
		ARTTIM23_DIS_RA2  = 0x04,
		ARTTIM23_DIS_RA3  = 0x08,
		ARTTIM23_INTR_CH1 = 0x10,
	DRWTIM2 	= 0x58,
	BRST 		= 0x59,
	DRWTIM3 	= 0x5b,
	BMIDECR0	= 0x70,
	MRDMODE		= 0x71,
		MRDMODE_INTR_CH0 = 0x04,
		MRDMODE_INTR_CH1 = 0x08,
	BMIDESR0	= 0x72,
	UDIDETCR0	= 0x73,
	DTPR0		= 0x74,
	BMIDECR1	= 0x78,
	BMIDECSR	= 0x79,
	UDIDETCR1	= 0x7B,
	DTPR1		= 0x7C
};

static int cmd648_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 r;

	/* Check cable detect bits */
	pci_read_config_byte(pdev, BMIDECSR, &r);
	if (r & (1 << ap->port_no))
		return ATA_CBL_PATA80;
	return ATA_CBL_PATA40;
}

/**
 *	cmd64x_set_timing	-	set PIO and MWDMA timing
 *	@ap: ATA interface
 *	@adev: ATA device
 *	@mode: mode
 *
 *	Called to do the PIO and MWDMA mode setup.
 */

static void cmd64x_set_timing(struct ata_port *ap, struct ata_device *adev, u8 mode)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct ata_timing t;
	const unsigned long T = 1000000 / 33;
	const u8 setup_data[] = { 0x40, 0x40, 0x40, 0x80, 0x00 };

	u8 reg;

	/* Port layout is not logical so use a table */
	const u8 arttim_port[2][2] = {
		{ ARTTIM0, ARTTIM1 },
		{ ARTTIM23, ARTTIM23 }
	};
	const u8 drwtim_port[2][2] = {
		{ DRWTIM0, DRWTIM1 },
		{ DRWTIM2, DRWTIM3 }
	};

	int arttim = arttim_port[ap->port_no][adev->devno];
	int drwtim = drwtim_port[ap->port_no][adev->devno];

	/* ata_timing_compute is smart and will produce timings for MWDMA
	   that don't violate the drives PIO capabilities. */
	if (ata_timing_compute(adev, mode, &t, T, 0) < 0) {
		printk(KERN_ERR DRV_NAME ": mode computation failed.\n");
		return;
	}
	if (ap->port_no) {
		/* Slave has shared address setup */
		struct ata_device *pair = ata_dev_pair(adev);

		if (pair) {
			struct ata_timing tp;
			ata_timing_compute(pair, pair->pio_mode, &tp, T, 0);
			ata_timing_merge(&t, &tp, &t, ATA_TIMING_SETUP);
		}
	}

	printk(KERN_DEBUG DRV_NAME ": active %d recovery %d setup %d.\n",
		t.active, t.recover, t.setup);
	if (t.recover > 16) {
		t.active += t.recover - 16;
		t.recover = 16;
	}
	if (t.active > 16)
		t.active = 16;

	/* Now convert the clocks into values we can actually stuff into
	   the chip */

	if (t.recover == 16)
		t.recover = 0;
	else if (t.recover > 1)
		t.recover--;
	else
		t.recover = 15;

	if (t.setup > 4)
		t.setup = 0xC0;
	else
		t.setup = setup_data[t.setup];

	t.active &= 0x0F;	/* 0 = 16 */

	/* Load setup timing */
	pci_read_config_byte(pdev, arttim, &reg);
	reg &= 0x3F;
	reg |= t.setup;
	pci_write_config_byte(pdev, arttim, reg);

	/* Load active/recovery */
	pci_write_config_byte(pdev, drwtim, (t.active << 4) | t.recover);
}

/**
 *	cmd64x_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Used when configuring the devices ot set the PIO timings. All the
 *	actual work is done by the PIO/MWDMA setting helper
 */

static void cmd64x_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	cmd64x_set_timing(ap, adev, adev->pio_mode);
}

/**
 *	cmd64x_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the DMA mode setup.
 */

static void cmd64x_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static const u8 udma_data[] = {
		0x30, 0x20, 0x10, 0x20, 0x10, 0x00
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 regU, regD;

	int pciU = UDIDETCR0 + 8 * ap->port_no;
	int pciD = BMIDESR0 + 8 * ap->port_no;
	int shift = 2 * adev->devno;

	pci_read_config_byte(pdev, pciD, &regD);
	pci_read_config_byte(pdev, pciU, &regU);

	/* DMA bits off */
	regD &= ~(0x20 << adev->devno);
	/* DMA control bits */
	regU &= ~(0x30 << shift);
	/* DMA timing bits */
	regU &= ~(0x05 << adev->devno);

	if (adev->dma_mode >= XFER_UDMA_0) {
		/* Merge the timing value */
		regU |= udma_data[adev->dma_mode - XFER_UDMA_0] << shift;
		/* Merge the control bits */
		regU |= 1 << adev->devno; /* UDMA on */
		if (adev->dma_mode > XFER_UDMA_2) /* 15nS timing */
			regU |= 4 << adev->devno;
	} else {
		regU &= ~ (1 << adev->devno);	/* UDMA off */
		cmd64x_set_timing(ap, adev, adev->dma_mode);
	}

	regD |= 0x20 << adev->devno;

	pci_write_config_byte(pdev, pciU, regU);
	pci_write_config_byte(pdev, pciD, regD);
}

/**
 *	cmd64x_sff_irq_check	-	check IDE interrupt
 *	@ap: ATA interface
 *
 *	Check IDE interrupt in CFR/ARTTIM23 registers.
 */

static bool cmd64x_sff_irq_check(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int irq_mask = ap->port_no ? ARTTIM23_INTR_CH1 : CFR_INTR_CH0;
	int irq_reg  = ap->port_no ? ARTTIM23 : CFR;
	u8 irq_stat;

	/* NOTE: reading the register should clear the interrupt */
	pci_read_config_byte(pdev, irq_reg, &irq_stat);

	return irq_stat & irq_mask;
}

/**
 *	cmd64x_sff_irq_clear	-	clear IDE interrupt
 *	@ap: ATA interface
 *
 *	Clear IDE interrupt in CFR/ARTTIM23 and DMA status registers.
 */

static void cmd64x_sff_irq_clear(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int irq_reg = ap->port_no ? ARTTIM23 : CFR;
	u8 irq_stat;

	ata_bmdma_irq_clear(ap);

	/* Reading the register should be enough to clear the interrupt */
	pci_read_config_byte(pdev, irq_reg, &irq_stat);
}

/**
 *	cmd648_sff_irq_check	-	check IDE interrupt
 *	@ap: ATA interface
 *
 *	Check IDE interrupt in MRDMODE register.
 */

static bool cmd648_sff_irq_check(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned long base = pci_resource_start(pdev, 4);
	int irq_mask = ap->port_no ? MRDMODE_INTR_CH1 : MRDMODE_INTR_CH0;
	u8 mrdmode = inb(base + 1);

	return mrdmode & irq_mask;
}

/**
 *	cmd648_sff_irq_clear	-	clear IDE interrupt
 *	@ap: ATA interface
 *
 *	Clear IDE interrupt in MRDMODE and DMA status registers.
 */

static void cmd648_sff_irq_clear(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned long base = pci_resource_start(pdev, 4);
	int irq_mask = ap->port_no ? MRDMODE_INTR_CH1 : MRDMODE_INTR_CH0;
	u8 mrdmode;

	ata_bmdma_irq_clear(ap);

	/* Clear this port's interrupt bit (leaving the other port alone) */
	mrdmode  = inb(base + 1);
	mrdmode &= ~(MRDMODE_INTR_CH0 | MRDMODE_INTR_CH1);
	outb(mrdmode | irq_mask, base + 1);
}

/**
 *	cmd646r1_bmdma_stop	-	DMA stop callback
 *	@qc: Command in progress
 *
 *	Stub for now while investigating the r1 quirk in the old driver.
 */

static void cmd646r1_bmdma_stop(struct ata_queued_cmd *qc)
{
	ata_bmdma_stop(qc);
}

static struct scsi_host_template cmd64x_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static const struct ata_port_operations cmd64x_base_ops = {
	.inherits	= &ata_bmdma_port_ops,
	.set_piomode	= cmd64x_set_piomode,
	.set_dmamode	= cmd64x_set_dmamode,
};

static struct ata_port_operations cmd64x_port_ops = {
	.inherits	= &cmd64x_base_ops,
	.sff_irq_check	= cmd64x_sff_irq_check,
	.sff_irq_clear	= cmd64x_sff_irq_clear,
	.cable_detect	= ata_cable_40wire,
};

static struct ata_port_operations cmd646r1_port_ops = {
	.inherits	= &cmd64x_base_ops,
	.sff_irq_check	= cmd64x_sff_irq_check,
	.sff_irq_clear	= cmd64x_sff_irq_clear,
	.bmdma_stop	= cmd646r1_bmdma_stop,
	.cable_detect	= ata_cable_40wire,
};

static struct ata_port_operations cmd646r3_port_ops = {
	.inherits	= &cmd64x_base_ops,
	.sff_irq_check	= cmd648_sff_irq_check,
	.sff_irq_clear	= cmd648_sff_irq_clear,
	.cable_detect	= ata_cable_40wire,
};

static struct ata_port_operations cmd648_port_ops = {
	.inherits	= &cmd64x_base_ops,
	.sff_irq_check	= cmd648_sff_irq_check,
	.sff_irq_clear	= cmd648_sff_irq_clear,
	.cable_detect	= cmd648_cable_detect,
};

static void cmd64x_fixup(struct pci_dev *pdev)
{
	u8 mrdmode;

	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 64);
	pci_read_config_byte(pdev, MRDMODE, &mrdmode);
	mrdmode &= ~0x30;	/* IRQ set up */
	mrdmode |= 0x02;	/* Memory read line enable */
	pci_write_config_byte(pdev, MRDMODE, mrdmode);

	/* PPC specific fixup copied from old driver */
#ifdef CONFIG_PPC
	pci_write_config_byte(pdev, UDIDETCR0, 0xF0);
#endif
}

static int cmd64x_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info cmd_info[7] = {
		{	/* CMD 643 - no UDMA */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.port_ops = &cmd64x_port_ops
		},
		{	/* CMD 646 with broken UDMA */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.port_ops = &cmd64x_port_ops
		},
		{	/* CMD 646U with broken UDMA */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.port_ops = &cmd646r3_port_ops
		},
		{	/* CMD 646U2 with working UDMA */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA2,
			.port_ops = &cmd646r3_port_ops
		},
		{	/* CMD 646 rev 1  */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.port_ops = &cmd646r1_port_ops
		},
		{	/* CMD 648 */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA4,
			.port_ops = &cmd648_port_ops
		},
		{	/* CMD 649 */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA5,
			.port_ops = &cmd648_port_ops
		}
	};
	const struct ata_port_info *ppi[] = {
		&cmd_info[id->driver_data],
		&cmd_info[id->driver_data],
		NULL
	};
	u8 reg;
	int rc;
	struct pci_dev *bridge = pdev->bus->self;
	/* mobility split bridges don't report enabled ports correctly */
	int port_ok = !(bridge && bridge->vendor ==
			PCI_VENDOR_ID_MOBILITY_ELECTRONICS);
	/* all (with exceptions below) apart from 643 have CNTRL_CH0 bit */
	int cntrl_ch0_ok = (id->driver_data != 0);

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	if (id->driver_data == 0)	/* 643 */
		ata_pci_bmdma_clear_simplex(pdev);

	if (pdev->device == PCI_DEVICE_ID_CMD_646)
		switch (pdev->revision) {
		/* UDMA works since rev 5 */
		default:
			ppi[0] = &cmd_info[3];
			ppi[1] = &cmd_info[3];
			break;
		/* Interrupts in MRDMODE since rev 3 */
		case 3:
		case 4:
			ppi[0] = &cmd_info[2];
			ppi[1] = &cmd_info[2];
			break;
		/* Rev 1 with other problems? */
		case 1:
			ppi[0] = &cmd_info[4];
			ppi[1] = &cmd_info[4];
			fallthrough;
		/* Early revs have no CNTRL_CH0 */
		case 2:
		case 0:
			cntrl_ch0_ok = 0;
			break;
		}

	cmd64x_fixup(pdev);

	/* check for enabled ports */
	pci_read_config_byte(pdev, CNTRL, &reg);
	if (!port_ok)
		dev_notice(&pdev->dev, "Mobility Bridge detected, ignoring CNTRL port enable/disable\n");
	if (port_ok && cntrl_ch0_ok && !(reg & CNTRL_CH0)) {
		dev_notice(&pdev->dev, "Primary port is disabled\n");
		ppi[0] = &ata_dummy_port_info;

	}
	if (port_ok && !(reg & CNTRL_CH1)) {
		dev_notice(&pdev->dev, "Secondary port is disabled\n");
		ppi[1] = &ata_dummy_port_info;
	}

	return ata_pci_bmdma_init_one(pdev, ppi, &cmd64x_sht, NULL, 0);
}

#ifdef CONFIG_PM_SLEEP
static int cmd64x_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = pci_get_drvdata(pdev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	cmd64x_fixup(pdev);

	ata_host_resume(host);
	return 0;
}
#endif

static const struct pci_device_id cmd64x[] = {
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_CMD_643), 0 },
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_CMD_646), 1 },
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_CMD_648), 5 },
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_CMD_649), 6 },

	{ },
};

static struct pci_driver cmd64x_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= cmd64x,
	.probe 		= cmd64x_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM_SLEEP
	.suspend	= ata_pci_device_suspend,
	.resume		= cmd64x_reinit_one,
#endif
};

module_pci_driver(cmd64x_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for CMD64x series PATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, cmd64x);
MODULE_VERSION(DRV_VERSION);
