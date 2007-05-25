/*
 *    pata_sis.c - SiS ATA driver
 *
 *	(C) 2005 Red Hat <alan@redhat.com>
 *
 *    Based upon linux/drivers/ide/pci/sis5513.c
 * Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2002		Lionel Bouton <Lionel.Bouton@inet6.fr>, Maintainer
 * Copyright (C) 2003		Vojtech Pavlik <vojtech@suse.cz>
 * SiS Taiwan		: for direct support and hardware.
 * Daniela Engert	: for initial ATA100 advices and numerous others.
 * John Fremlin, Manfred Spraul, Dave Morgan, Peter Kjellerstedt	:
 *			  for checking code correctness, providing patches.
 * Original tests and design on the SiS620 chipset.
 * ATA100 tests and design on the SiS735 chipset.
 * ATA16/33 support from specs
 * ATA133 support for SiS961/962 by L.C. Chang <lcchang@sis.com.tw>
 *
 *
 *	TODO
 *	Check MWDMA on drives that don't support MWDMA speed pio cycles ?
 *	More Testing
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/ata.h>
#include "sis.h"

#define DRV_NAME	"pata_sis"
#define DRV_VERSION	"0.5.1"

struct sis_chipset {
	u16 device;				/* PCI host ID */
	const struct ata_port_info *info;	/* Info block */
	/* Probably add family, cable detect type etc here to clean
	   up code later */
};

struct sis_laptop {
	u16 device;
	u16 subvendor;
	u16 subdevice;
};

static const struct sis_laptop sis_laptop[] = {
	/* devid, subvendor, subdev */
	{ 0x5513, 0x1043, 0x1107 },	/* ASUS A6K */
	/* end marker */
	{ 0, }
};

static int sis_short_ata40(struct pci_dev *dev)
{
	const struct sis_laptop *lap = &sis_laptop[0];

	while (lap->device) {
		if (lap->device == dev->device &&
		    lap->subvendor == dev->subsystem_vendor &&
		    lap->subdevice == dev->subsystem_device)
			return 1;
		lap++;
	}

	return 0;
}

/**
 *	sis_old_port_base		-	return PCI configuration base for dev
 *	@adev: device
 *
 *	Returns the base of the PCI configuration registers for this port
 *	number.
 */

static int sis_old_port_base(struct ata_device *adev)
{
	return  0x40 + (4 * adev->ap->port_no) +  (2 * adev->devno);
}

/**
 *	sis_133_cable_detect	-	check for 40/80 pin
 *	@ap: Port
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform cable detection for the later UDMA133 capable
 *	SiS chipset.
 */

static int sis_133_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u16 tmp;

	/* The top bit of this register is the cable detect bit */
	pci_read_config_word(pdev, 0x50 + 2 * ap->port_no, &tmp);
	if ((tmp & 0x8000) && !sis_short_ata40(pdev))
		return ATA_CBL_PATA40;
	return ATA_CBL_PATA80;
}

/**
 *	sis_66_cable_detect	-	check for 40/80 pin
 *	@ap: Port
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform cable detection on the UDMA66, UDMA100 and early UDMA133
 *	SiS IDE controllers.
 */

static int sis_66_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 tmp;

	/* Older chips keep cable detect in bits 4/5 of reg 0x48 */
	pci_read_config_byte(pdev, 0x48, &tmp);
	tmp >>= ap->port_no;
	if ((tmp & 0x10) && !sis_short_ata40(pdev))
		return ATA_CBL_PATA40;
	return ATA_CBL_PATA80;
}


/**
 *	sis_pre_reset		-	probe begin
 *	@ap: ATA port
 *	@deadline: deadline jiffies for the operation
 *
 *	Set up cable type and use generic probe init
 */

static int sis_pre_reset(struct ata_port *ap, unsigned long deadline)
{
	static const struct pci_bits sis_enable_bits[] = {
		{ 0x4aU, 1U, 0x02UL, 0x02UL },	/* port 0 */
		{ 0x4aU, 1U, 0x04UL, 0x04UL },	/* port 1 */
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (!pci_test_config_bits(pdev, &sis_enable_bits[ap->port_no]))
		return -ENOENT;

	/* Clear the FIFO settings. We can't enable the FIFO until
	   we know we are poking at a disk */
	pci_write_config_byte(pdev, 0x4B, 0);
	return ata_std_prereset(ap, deadline);
}


/**
 *	sis_error_handler - Probe specified port on PATA host controller
 *	@ap: Port to probe
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, sis_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	sis_set_fifo	-	Set RWP fifo bits for this device
 *	@ap: Port
 *	@adev: Device
 *
 *	SIS chipsets implement prefetch/postwrite bits for each device
 *	on both channels. This functionality is not ATAPI compatible and
 *	must be configured according to the class of device present
 */

static void sis_set_fifo(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 fifoctrl;
	u8 mask = 0x11;

	mask <<= (2 * ap->port_no);
	mask <<= adev->devno;

	/* This holds various bits including the FIFO control */
	pci_read_config_byte(pdev, 0x4B, &fifoctrl);
	fifoctrl &= ~mask;

	/* Enable for ATA (disk) only */
	if (adev->class == ATA_DEV_ATA)
		fifoctrl |= mask;
	pci_write_config_byte(pdev, 0x4B, fifoctrl);
}

/**
 *	sis_old_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device we are configuring for.
 *
 *	Set PIO mode for device, in host controller PCI config space. This
 *	function handles PIO set up for all chips that are pre ATA100 and
 *	also early ATA100 devices.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_old_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int port = sis_old_port_base(adev);
	u8 t1, t2;
	int speed = adev->pio_mode - XFER_PIO_0;

	const u8 active[]   = { 0x00, 0x07, 0x04, 0x03, 0x01 };
	const u8 recovery[] = { 0x00, 0x06, 0x04, 0x03, 0x03 };

	sis_set_fifo(ap, adev);

	pci_read_config_byte(pdev, port, &t1);
	pci_read_config_byte(pdev, port + 1, &t2);

	t1 &= ~0x0F;	/* Clear active/recovery timings */
	t2 &= ~0x07;

	t1 |= active[speed];
	t2 |= recovery[speed];

	pci_write_config_byte(pdev, port, t1);
	pci_write_config_byte(pdev, port + 1, t2);
}

/**
 *	sis_100_set_pioode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device we are configuring for.
 *
 *	Set PIO mode for device, in host controller PCI config space. This
 *	function handles PIO set up for ATA100 devices and early ATA133.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_100_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int port = sis_old_port_base(adev);
	int speed = adev->pio_mode - XFER_PIO_0;

	const u8 actrec[] = { 0x00, 0x67, 0x44, 0x33, 0x31 };

	sis_set_fifo(ap, adev);

	pci_write_config_byte(pdev, port, actrec[speed]);
}

/**
 *	sis_133_set_pioode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device we are configuring for.
 *
 *	Set PIO mode for device, in host controller PCI config space. This
 *	function handles PIO set up for the later ATA133 devices.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_133_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int port = 0x40;
	u32 t1;
	u32 reg54;
	int speed = adev->pio_mode - XFER_PIO_0;

	const u32 timing133[] = {
		0x28269000,	/* Recovery << 24 | Act << 16 | Ini << 12 */
		0x0C266000,
		0x04263000,
		0x0C0A3000,
		0x05093000
	};
	const u32 timing100[] = {
		0x1E1C6000,	/* Recovery << 24 | Act << 16 | Ini << 12 */
		0x091C4000,
		0x031C2000,
		0x09072000,
		0x04062000
	};

	sis_set_fifo(ap, adev);

	/* If bit 14 is set then the registers are mapped at 0x70 not 0x40 */
	pci_read_config_dword(pdev, 0x54, &reg54);
	if (reg54 & 0x40000000)
		port = 0x70;
	port += 8 * ap->port_no +  4 * adev->devno;

	pci_read_config_dword(pdev, port, &t1);
	t1 &= 0xC0C00FFF;	/* Mask out timing */

	if (t1 & 0x08)		/* 100 or 133 ? */
		t1 |= timing133[speed];
	else
		t1 |= timing100[speed];
	pci_write_config_byte(pdev, port, t1);
}

/**
 *	sis_old_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set UDMA/MWDMA mode for device, in host controller PCI config space.
 *	Handles pre UDMA and UDMA33 devices. Supports MWDMA as well unlike
 *	the old ide/pci driver.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_old_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int speed = adev->dma_mode - XFER_MW_DMA_0;
	int drive_pci = sis_old_port_base(adev);
	u16 timing;

	const u16 mwdma_bits[] = { 0x707, 0x202, 0x202 };
	const u16 udma_bits[]  = { 0xE000, 0xC000, 0xA000 };

	pci_read_config_word(pdev, drive_pci, &timing);

	if (adev->dma_mode < XFER_UDMA_0) {
		/* bits 3-0 hold recovery timing bits 8-10 active timing and
		   the higer bits are dependant on the device */
		timing &= ~ 0x870F;
		timing |= mwdma_bits[speed];
		pci_write_config_word(pdev, drive_pci, timing);
	} else {
		/* Bit 15 is UDMA on/off, bit 13-14 are cycle time */
		speed = adev->dma_mode - XFER_UDMA_0;
		timing &= ~0x6000;
		timing |= udma_bits[speed];
	}
}

/**
 *	sis_66_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set UDMA/MWDMA mode for device, in host controller PCI config space.
 *	Handles UDMA66 and early UDMA100 devices. Supports MWDMA as well unlike
 *	the old ide/pci driver.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_66_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int speed = adev->dma_mode - XFER_MW_DMA_0;
	int drive_pci = sis_old_port_base(adev);
	u16 timing;

	const u16 mwdma_bits[] = { 0x707, 0x202, 0x202 };
	const u16 udma_bits[]  = { 0xF000, 0xD000, 0xB000, 0xA000, 0x9000};

	pci_read_config_word(pdev, drive_pci, &timing);

	if (adev->dma_mode < XFER_UDMA_0) {
		/* bits 3-0 hold recovery timing bits 8-10 active timing and
		   the higer bits are dependant on the device, bit 15 udma */
		timing &= ~0x870F;
		timing |= mwdma_bits[speed];
	} else {
		/* Bit 15 is UDMA on/off, bit 12-14 are cycle time */
		speed = adev->dma_mode - XFER_UDMA_0;
		timing &= ~0xF000;
		timing |= udma_bits[speed];
	}
	pci_write_config_word(pdev, drive_pci, timing);
}

/**
 *	sis_100_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set UDMA/MWDMA mode for device, in host controller PCI config space.
 *	Handles UDMA66 and early UDMA100 devices.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_100_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int speed = adev->dma_mode - XFER_MW_DMA_0;
	int drive_pci = sis_old_port_base(adev);
	u8 timing;

	const u8 udma_bits[]  = { 0x8B, 0x87, 0x85, 0x83, 0x82, 0x81};

	pci_read_config_byte(pdev, drive_pci + 1, &timing);

	if (adev->dma_mode < XFER_UDMA_0) {
		/* NOT SUPPORTED YET: NEED DATA SHEET. DITTO IN OLD DRIVER */
	} else {
		/* Bit 7 is UDMA on/off, bit 0-3 are cycle time */
		speed = adev->dma_mode - XFER_UDMA_0;
		timing &= ~0x8F;
		timing |= udma_bits[speed];
	}
	pci_write_config_byte(pdev, drive_pci + 1, timing);
}

/**
 *	sis_133_early_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set UDMA/MWDMA mode for device, in host controller PCI config space.
 *	Handles early SiS 961 bridges. Supports MWDMA as well unlike
 *	the old ide/pci driver.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_133_early_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int speed = adev->dma_mode - XFER_MW_DMA_0;
	int drive_pci = sis_old_port_base(adev);
	u8 timing;
	/* Low 4 bits are timing */
	static const u8 udma_bits[]  = { 0x8F, 0x8A, 0x87, 0x85, 0x83, 0x82, 0x81};

	pci_read_config_byte(pdev, drive_pci + 1, &timing);

	if (adev->dma_mode < XFER_UDMA_0) {
		/* NOT SUPPORTED YET: NEED DATA SHEET. DITTO IN OLD DRIVER */
	} else {
		/* Bit 7 is UDMA on/off, bit 0-3 are cycle time */
		speed = adev->dma_mode - XFER_UDMA_0;
		timing &= ~0x8F;
		timing |= udma_bits[speed];
	}
	pci_write_config_byte(pdev, drive_pci + 1, timing);
}

/**
 *	sis_133_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set UDMA/MWDMA mode for device, in host controller PCI config space.
 *	Handles early SiS 961 bridges. Supports MWDMA as well unlike
 *	the old ide/pci driver.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void sis_133_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	int speed = adev->dma_mode - XFER_MW_DMA_0;
	int port = 0x40;
	u32 t1;
	u32 reg54;

	/* bits 4- cycle time 8 - cvs time */
	static const u32 timing_u100[] = { 0x6B0, 0x470, 0x350, 0x140, 0x120, 0x110, 0x000 };
	static const u32 timing_u133[] = { 0x9F0, 0x6A0, 0x470, 0x250, 0x230, 0x220, 0x210 };

	/* If bit 14 is set then the registers are mapped at 0x70 not 0x40 */
	pci_read_config_dword(pdev, 0x54, &reg54);
	if (reg54 & 0x40000000)
		port = 0x70;
	port += (8 * ap->port_no) +  (4 * adev->devno);

	pci_read_config_dword(pdev, port, &t1);

	if (adev->dma_mode < XFER_UDMA_0) {
		t1 &= ~0x00000004;
		/* FIXME: need data sheet to add MWDMA here. Also lacking on
		   ide/pci driver */
	} else {
		speed = adev->dma_mode - XFER_UDMA_0;
		/* if & 8 no UDMA133 - need info for ... */
		t1 &= ~0x00000FF0;
		t1 |= 0x00000004;
		if (t1 & 0x08)
			t1 |= timing_u133[speed];
		else
			t1 |= timing_u100[speed];
	}
	pci_write_config_dword(pdev, port, t1);
}

static struct scsi_host_template sis_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations sis_133_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= sis_133_set_piomode,
	.set_dmamode		= sis_133_set_dmamode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= sis_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= sis_133_cable_detect,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};

static const struct ata_port_operations sis_133_for_sata_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= sis_133_set_piomode,
	.set_dmamode		= sis_133_set_dmamode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= ata_bmdma_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= sis_133_cable_detect,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};

static const struct ata_port_operations sis_133_early_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= sis_100_set_piomode,
	.set_dmamode		= sis_133_early_set_dmamode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= sis_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= sis_66_cable_detect,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};

static const struct ata_port_operations sis_100_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= sis_100_set_piomode,
	.set_dmamode		= sis_100_set_dmamode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= sis_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= sis_66_cable_detect,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};

static const struct ata_port_operations sis_66_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= sis_old_set_piomode,
	.set_dmamode		= sis_66_set_dmamode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,
	.cable_detect		= sis_66_cable_detect,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= sis_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};

static const struct ata_port_operations sis_old_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= sis_old_set_piomode,
	.set_dmamode		= sis_old_set_dmamode,
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= sis_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= ata_cable_40wire,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,

	.port_start		= ata_port_start,
};

static const struct ata_port_info sis_info = {
	.sht		= &sis_sht,
	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= 0x1f,	/* pio0-4 */
	.mwdma_mask	= 0x07,
	.udma_mask	= 0,
	.port_ops	= &sis_old_ops,
};
static const struct ata_port_info sis_info33 = {
	.sht		= &sis_sht,
	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= 0x1f,	/* pio0-4 */
	.mwdma_mask	= 0x07,
	.udma_mask	= ATA_UDMA2,	/* UDMA 33 */
	.port_ops	= &sis_old_ops,
};
static const struct ata_port_info sis_info66 = {
	.sht		= &sis_sht,
	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= 0x1f,	/* pio0-4 */
	.udma_mask	= ATA_UDMA4,	/* UDMA 66 */
	.port_ops	= &sis_66_ops,
};
static const struct ata_port_info sis_info100 = {
	.sht		= &sis_sht,
	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= 0x1f,	/* pio0-4 */
	.udma_mask	= ATA_UDMA5,
	.port_ops	= &sis_100_ops,
};
static const struct ata_port_info sis_info100_early = {
	.sht		= &sis_sht,
	.flags		= ATA_FLAG_SLAVE_POSS,
	.udma_mask	= ATA_UDMA5,
	.pio_mask	= 0x1f,	/* pio0-4 */
	.port_ops	= &sis_66_ops,
};
static const struct ata_port_info sis_info133 = {
	.sht		= &sis_sht,
	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= 0x1f,	/* pio0-4 */
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &sis_133_ops,
};
const struct ata_port_info sis_info133_for_sata = {
	.sht		= &sis_sht,
	.flags		= ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
	.pio_mask	= 0x1f,	/* pio0-4 */
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &sis_133_for_sata_ops,
};
static const struct ata_port_info sis_info133_early = {
	.sht		= &sis_sht,
	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= 0x1f,	/* pio0-4 */
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &sis_133_early_ops,
};

/* Privately shared with the SiS180 SATA driver, not for use elsewhere */
EXPORT_SYMBOL_GPL(sis_info133_for_sata);

static void sis_fixup(struct pci_dev *pdev, struct sis_chipset *sis)
{
	u16 regw;
	u8 reg;

	if (sis->info == &sis_info133) {
		pci_read_config_word(pdev, 0x50, &regw);
		if (regw & 0x08)
			pci_write_config_word(pdev, 0x50, regw & ~0x08);
		pci_read_config_word(pdev, 0x52, &regw);
		if (regw & 0x08)
			pci_write_config_word(pdev, 0x52, regw & ~0x08);
		return;
	}

	if (sis->info == &sis_info133_early || sis->info == &sis_info100) {
		/* Fix up latency */
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x80);
		/* Set compatibility bit */
		pci_read_config_byte(pdev, 0x49, &reg);
		if (!(reg & 0x01))
			pci_write_config_byte(pdev, 0x49, reg | 0x01);
		return;
	}

	if (sis->info == &sis_info66 || sis->info == &sis_info100_early) {
		/* Fix up latency */
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x80);
		/* Set compatibility bit */
		pci_read_config_byte(pdev, 0x52, &reg);
		if (!(reg & 0x04))
			pci_write_config_byte(pdev, 0x52, reg | 0x04);
		return;
	}

	if (sis->info == &sis_info33) {
		pci_read_config_byte(pdev, PCI_CLASS_PROG, &reg);
		if (( reg & 0x0F ) != 0x00)
			pci_write_config_byte(pdev, PCI_CLASS_PROG, reg & 0xF0);
		/* Fall through to ATA16 fixup below */
	}

	if (sis->info == &sis_info || sis->info == &sis_info33) {
		/* force per drive recovery and active timings
		   needed on ATA_33 and below chips */
		pci_read_config_byte(pdev, 0x52, &reg);
		if (!(reg & 0x08))
			pci_write_config_byte(pdev, 0x52, reg|0x08);
		return;
	}

	BUG();
}

/**
 *	sis_init_one - Register SiS ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in sis_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.  We probe for combined mode (sigh),
 *	and then hand over control to libata, for it to do the rest.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int sis_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_port_info port;
	const struct ata_port_info *ppi[] = { &port, NULL };
	struct pci_dev *host = NULL;
	struct sis_chipset *chipset = NULL;
	struct sis_chipset *sets;

	static struct sis_chipset sis_chipsets[] = {

		{ 0x0968, &sis_info133 },
		{ 0x0966, &sis_info133 },
		{ 0x0965, &sis_info133 },
		{ 0x0745, &sis_info100 },
		{ 0x0735, &sis_info100 },
		{ 0x0733, &sis_info100 },
		{ 0x0635, &sis_info100 },
		{ 0x0633, &sis_info100 },

		{ 0x0730, &sis_info100_early },	/* 100 with ATA 66 layout */
		{ 0x0550, &sis_info100_early },	/* 100 with ATA 66 layout */

		{ 0x0640, &sis_info66 },
		{ 0x0630, &sis_info66 },
		{ 0x0620, &sis_info66 },
		{ 0x0540, &sis_info66 },
		{ 0x0530, &sis_info66 },

		{ 0x5600, &sis_info33 },
		{ 0x5598, &sis_info33 },
		{ 0x5597, &sis_info33 },
		{ 0x5591, &sis_info33 },
		{ 0x5582, &sis_info33 },
		{ 0x5581, &sis_info33 },

		{ 0x5596, &sis_info },
		{ 0x5571, &sis_info },
		{ 0x5517, &sis_info },
		{ 0x5511, &sis_info },

		{0}
	};
	static struct sis_chipset sis133_early = {
		0x0, &sis_info133_early
	};
	static struct sis_chipset sis133 = {
		0x0, &sis_info133
	};
	static struct sis_chipset sis100_early = {
		0x0, &sis_info100_early
	};
	static struct sis_chipset sis100 = {
		0x0, &sis_info100
	};

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	/* We have to find the bridge first */

	for (sets = &sis_chipsets[0]; sets->device; sets++) {
		host = pci_get_device(PCI_VENDOR_ID_SI, sets->device, NULL);
		if (host != NULL) {
			chipset = sets;			/* Match found */
			if (sets->device == 0x630) {	/* SIS630 */
				u8 host_rev;
				pci_read_config_byte(host, PCI_REVISION_ID, &host_rev);
				if (host_rev >= 0x30)	/* 630 ET */
					chipset = &sis100_early;
			}
			break;
		}
	}

	/* Look for concealed bridges */
	if (chipset == NULL) {
		/* Second check */
		u32 idemisc;
		u16 trueid;

		/* Disable ID masking and register remapping then
		   see what the real ID is */

		pci_read_config_dword(pdev, 0x54, &idemisc);
		pci_write_config_dword(pdev, 0x54, idemisc & 0x7fffffff);
		pci_read_config_word(pdev, PCI_DEVICE_ID, &trueid);
		pci_write_config_dword(pdev, 0x54, idemisc);

		switch(trueid) {
		case 0x5518:	/* SIS 962/963 */
			chipset = &sis133;
			if ((idemisc & 0x40000000) == 0) {
				pci_write_config_dword(pdev, 0x54, idemisc | 0x40000000);
				printk(KERN_INFO "SIS5513: Switching to 5513 register mapping\n");
			}
			break;
		case 0x0180:	/* SIS 965/965L */
			chipset =  &sis133;
			break;
		case 0x1180:	/* SIS 966/966L */
			chipset =  &sis133;
			break;
		}
	}

	/* Further check */
	if (chipset == NULL) {
		struct pci_dev *lpc_bridge;
		u16 trueid;
		u8 prefctl;
		u8 idecfg;
		u8 sbrev;

		/* Try the second unmasking technique */
		pci_read_config_byte(pdev, 0x4a, &idecfg);
		pci_write_config_byte(pdev, 0x4a, idecfg | 0x10);
		pci_read_config_word(pdev, PCI_DEVICE_ID, &trueid);
		pci_write_config_byte(pdev, 0x4a, idecfg);

		switch(trueid) {
		case 0x5517:
			lpc_bridge = pci_get_slot(pdev->bus, 0x10); /* Bus 0 Dev 2 Fn 0 */
			if (lpc_bridge == NULL)
				break;
			pci_read_config_byte(lpc_bridge, PCI_REVISION_ID, &sbrev);
			pci_read_config_byte(pdev, 0x49, &prefctl);
			pci_dev_put(lpc_bridge);

			if (sbrev == 0x10 && (prefctl & 0x80)) {
				chipset = &sis133_early;
				break;
			}
			chipset = &sis100;
			break;
		}
	}
	pci_dev_put(host);

	/* No chipset info, no support */
	if (chipset == NULL)
		return -ENODEV;

	port = *chipset->info;
	port.private_data = chipset;

	sis_fixup(pdev, chipset);

	return ata_pci_init_one(pdev, ppi);
}

static const struct pci_device_id sis_pci_tbl[] = {
	{ PCI_VDEVICE(SI, 0x5513), },	/* SiS 5513 */
	{ PCI_VDEVICE(SI, 0x5518), },	/* SiS 5518 */
	{ PCI_VDEVICE(SI, 0x1180), },	/* SiS 1180 */

	{ }
};

static struct pci_driver sis_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= sis_pci_tbl,
	.probe			= sis_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
#endif
};

static int __init sis_init(void)
{
	return pci_register_driver(&sis_pci_driver);
}

static void __exit sis_exit(void)
{
	pci_unregister_driver(&sis_pci_driver);
}

module_init(sis_init);
module_exit(sis_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for SiS ATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, sis_pci_tbl);
MODULE_VERSION(DRV_VERSION);

