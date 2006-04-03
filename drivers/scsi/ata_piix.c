/*
 *    ata_piix.c - Intel PATA/SATA controllers
 *
 *    Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *    		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *
 *	Copyright 2003-2005 Red Hat Inc
 *	Copyright 2003-2005 Jeff Garzik
 *
 *
 *	Copyright header from piix.c:
 *
 *  Copyright (C) 1998-1999 Andrzej Krzysztofowicz, Author and Maintainer
 *  Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003 Red Hat Inc <alan@redhat.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *  Hardware documentation available at http://developer.intel.com/
 *
 * Documentation
 *	Publically available from Intel web site. Errata documentation
 * is also publically available. As an aide to anyone hacking on this
 * driver the list of errata that are relevant is below.going back to
 * PIIX4. Older device documentation is now a bit tricky to find.
 *
 * The chipsets all follow very much the same design. The orginal Triton
 * series chipsets do _not_ support independant device timings, but this
 * is fixed in Triton II. With the odd mobile exception the chips then
 * change little except in gaining more modes until SATA arrives. This
 * driver supports only the chips with independant timing (that is those
 * with SITRE and the 0x44 timing register). See pata_oldpiix and pata_mpiix
 * for the early chip drivers.
 *
 * Errata of note:
 *
 * Unfixable
 *	PIIX4    errata #9	- Only on ultra obscure hw
 *	ICH3	 errata #13     - Not observed to affect real hw
 *				  by Intel
 *
 * Things we must deal with
 *	PIIX4	errata #10	- BM IDE hang with non UDMA
 *				  (must stop/start dma to recover)
 *	440MX   errata #15	- As PIIX4 errata #10
 *	PIIX4	errata #15	- Must not read control registers
 * 				  during a PIO transfer
 *	440MX   errata #13	- As PIIX4 errata #15
 *	ICH2	errata #21	- DMA mode 0 doesn't work right
 *	ICH0/1  errata #55	- As ICH2 errata #21
 *	ICH2	spec c #9	- Extra operations needed to handle
 *				  drive hotswap [NOT YET SUPPORTED]
 *	ICH2    spec c #20	- IDE PRD must not cross a 64K boundary
 *				  and must be dword aligned
 *	ICH2    spec c #24	- UDMA mode 4,5 t85/86 should be 6ns not 3.3
 *
 * Should have been BIOS fixed:
 *	450NX:	errata #19	- DMA hangs on old 450NX
 *	450NX:  errata #20	- DMA hangs on old 450NX
 *	450NX:  errata #25	- Corruption with DMA on old 450NX
 *	ICH3    errata #15      - IDE deadlock under high load
 *				  (BIOS must set dev 31 fn 0 bit 23)
 *	ICH3	errata #18	- Don't use native mode
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

#define DRV_NAME	"ata_piix"
#define DRV_VERSION	"1.10"

enum {
	PIIX_IOCFG		= 0x54, /* IDE I/O configuration register */
	ICH5_PMR		= 0x90, /* port mapping register */
	ICH5_PCS		= 0x92,	/* port control and status */
	PIIX_SCC		= 0x0A, /* sub-class code register */

	PIIX_FLAG_IGNORE_PCS	= (1 << 25), /* ignore PCS present bits */
	PIIX_FLAG_SCR		= (1 << 26), /* SCR available */
	PIIX_FLAG_AHCI		= (1 << 27), /* AHCI possible */
	PIIX_FLAG_CHECKINTR	= (1 << 28), /* make sure PCI INTx enabled */
	PIIX_FLAG_COMBINED	= (1 << 29), /* combined mode possible */
	/* ICH6/7 use different scheme for map value */
	PIIX_FLAG_COMBINED_ICH6	= PIIX_FLAG_COMBINED | (1 << 30),

	/* combined mode.  if set, PATA is channel 0.
	 * if clear, PATA is channel 1.
	 */
	PIIX_PORT_ENABLED	= (1 << 0),
	PIIX_PORT_PRESENT	= (1 << 4),

	PIIX_80C_PRI		= (1 << 5) | (1 << 4),
	PIIX_80C_SEC		= (1 << 7) | (1 << 6),

	/* controller IDs */
	piix4_pata		= 0,
	ich5_pata		= 1,
	ich5_sata		= 2,
	esb_sata		= 3,
	ich6_sata		= 4,
	ich6_sata_ahci		= 5,
	ich6m_sata_ahci		= 6,

	/* constants for mapping table */
	P0			= 0,  /* port 0 */
	P1			= 1,  /* port 1 */
	P2			= 2,  /* port 2 */
	P3			= 3,  /* port 3 */
	IDE			= -1, /* IDE */
	NA			= -2, /* not avaliable */
	RV			= -3, /* reserved */

	PIIX_AHCI_DEVICE	= 6,
};

struct piix_map_db {
	const u32 mask;
	const int map[][4];
};

static int piix_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent);

static int piix_pata_probe_reset(struct ata_port *ap, unsigned int *classes);
static int piix_sata_probe_reset(struct ata_port *ap, unsigned int *classes);
static void piix_set_piomode (struct ata_port *ap, struct ata_device *adev);
static void piix_set_dmamode (struct ata_port *ap, struct ata_device *adev);

static unsigned int in_module_init = 1;

static const struct pci_device_id piix_pci_tbl[] = {
#ifdef ATA_ENABLE_PATA
	{ 0x8086, 0x7111, PCI_ANY_ID, PCI_ANY_ID, 0, 0, piix4_pata },
	{ 0x8086, 0x24db, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_pata },
	{ 0x8086, 0x25a2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_pata },
#endif

	/* NOTE: The following PCI ids must be kept in sync with the
	 * list in drivers/pci/quirks.c.
	 */

	/* 82801EB (ICH5) */
	{ 0x8086, 0x24d1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	/* 82801EB (ICH5) */
	{ 0x8086, 0x24df, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	/* 6300ESB (ICH5 variant with broken PCS present bits) */
	{ 0x8086, 0x25a3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, esb_sata },
	/* 6300ESB pretending RAID */
	{ 0x8086, 0x25b0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, esb_sata },
	/* 82801FB/FW (ICH6/ICH6W) */
	{ 0x8086, 0x2651, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata },
	/* 82801FR/FRW (ICH6R/ICH6RW) */
	{ 0x8086, 0x2652, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata_ahci },
	/* 82801FBM ICH6M (ICH6R with only port 0 and 2 implemented) */
	{ 0x8086, 0x2653, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6m_sata_ahci },
	/* 82801GB/GR/GH (ICH7, identical to ICH6) */
	{ 0x8086, 0x27c0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata_ahci },
	/* 2801GBM/GHM (ICH7M, identical to ICH6M) */
	{ 0x8086, 0x27c4, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6m_sata_ahci },
	/* Enterprise Southbridge 2 (where's the datasheet?) */
	{ 0x8086, 0x2680, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata_ahci },
	/* SATA Controller 1 IDE (ICH8, no datasheet yet) */
	{ 0x8086, 0x2820, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata_ahci },
	/* SATA Controller 2 IDE (ICH8, ditto) */
	{ 0x8086, 0x2825, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata_ahci },
	/* Mobile SATA Controller IDE (ICH8M, ditto) */
	{ 0x8086, 0x2828, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6m_sata_ahci },

	{ }	/* terminate list */
};

static struct pci_driver piix_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= piix_pci_tbl,
	.probe			= piix_init_one,
	.remove			= ata_pci_remove_one,
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
};

static struct scsi_host_template piix_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
	.resume			= ata_scsi_device_resume,
	.suspend		= ata_scsi_device_suspend,
};

static const struct ata_port_operations piix_pata_ops = {
	.port_disable		= ata_port_disable,
	.set_piomode		= piix_set_piomode,
	.set_dmamode		= piix_set_dmamode,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.probe_reset		= piix_pata_probe_reset,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,

	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= ata_host_stop,
};

static const struct ata_port_operations piix_sata_ops = {
	.port_disable		= ata_port_disable,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.probe_reset		= piix_sata_probe_reset,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,

	.eng_timeout		= ata_eng_timeout,

	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,

	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= ata_host_stop,
};

static struct piix_map_db ich5_map_db = {
	.mask = 0x7,
	.map = {
		/* PM   PS   SM   SS       MAP  */
		{  P0,  NA,  P1,  NA }, /* 000b */
		{  P1,  NA,  P0,  NA }, /* 001b */
		{  RV,  RV,  RV,  RV },
		{  RV,  RV,  RV,  RV },
		{  P0,  P1, IDE, IDE }, /* 100b */
		{  P1,  P0, IDE, IDE }, /* 101b */
		{ IDE, IDE,  P0,  P1 }, /* 110b */
		{ IDE, IDE,  P1,  P0 }, /* 111b */
	},
};

static struct piix_map_db ich6_map_db = {
	.mask = 0x3,
	.map = {
		/* PM   PS   SM   SS       MAP */
		{  P0,  P2,  P1,  P3 }, /* 00b */
		{ IDE, IDE,  P1,  P3 }, /* 01b */
		{  P0,  P2, IDE, IDE }, /* 10b */
		{  RV,  RV,  RV,  RV },
	},
};

static struct piix_map_db ich6m_map_db = {
	.mask = 0x3,
	.map = {
		/* PM   PS   SM   SS       MAP */
		{  P0,  P2,  RV,  RV }, /* 00b */
		{  RV,  RV,  RV,  RV },
		{  P0,  P2, IDE, IDE }, /* 10b */
		{  RV,  RV,  RV,  RV },
	},
};

static struct ata_port_info piix_port_info[] = {
	/* piix4_pata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x1f,	/* pio0-4 */
#if 0
		.mwdma_mask	= 0x06, /* mwdma1-2 */
#else
		.mwdma_mask	= 0x00, /* mwdma broken */
#endif
		.udma_mask	= ATA_UDMA_MASK_40C,
		.port_ops	= &piix_pata_ops,
	},

	/* ich5_pata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SLAVE_POSS | PIIX_FLAG_CHECKINTR,
		.pio_mask	= 0x1f,	/* pio0-4 */
#if 0
		.mwdma_mask	= 0x06, /* mwdma1-2 */
#else
		.mwdma_mask	= 0x00, /* mwdma broken */
#endif
		.udma_mask	= 0x3f, /* udma0-5 */
		.port_ops	= &piix_pata_ops,
	},

	/* ich5_sata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SATA | PIIX_FLAG_COMBINED |
				  PIIX_FLAG_CHECKINTR,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f,	/* udma0-6 */
		.port_ops	= &piix_sata_ops,
		.private_data	= &ich5_map_db,
	},

	/* i6300esb_sata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SATA | PIIX_FLAG_COMBINED |
				  PIIX_FLAG_CHECKINTR | PIIX_FLAG_IGNORE_PCS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f,	/* udma0-6 */
		.port_ops	= &piix_sata_ops,
		.private_data	= &ich5_map_db,
	},

	/* ich6_sata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SATA | PIIX_FLAG_COMBINED_ICH6 |
				  PIIX_FLAG_CHECKINTR | PIIX_FLAG_SCR,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f,	/* udma0-6 */
		.port_ops	= &piix_sata_ops,
		.private_data	= &ich6_map_db,
	},

	/* ich6_sata_ahci */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SATA | PIIX_FLAG_COMBINED_ICH6 |
				  PIIX_FLAG_CHECKINTR | PIIX_FLAG_SCR |
				  PIIX_FLAG_AHCI,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f,	/* udma0-6 */
		.port_ops	= &piix_sata_ops,
		.private_data	= &ich6_map_db,
	},

	/* ich6m_sata_ahci */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SATA | PIIX_FLAG_COMBINED_ICH6 |
				  PIIX_FLAG_CHECKINTR | PIIX_FLAG_SCR |
				  PIIX_FLAG_AHCI,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f,	/* udma0-6 */
		.port_ops	= &piix_sata_ops,
		.private_data	= &ich6m_map_db,
	},
};

static struct pci_bits piix_enable_bits[] = {
	{ 0x41U, 1U, 0x80UL, 0x80UL },	/* port 0 */
	{ 0x43U, 1U, 0x80UL, 0x80UL },	/* port 1 */
};

MODULE_AUTHOR("Andre Hedrick, Alan Cox, Andrzej Krzysztofowicz, Jeff Garzik");
MODULE_DESCRIPTION("SCSI low-level driver for Intel PIIX/ICH ATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, piix_pci_tbl);
MODULE_VERSION(DRV_VERSION);

/**
 *	piix_pata_cbl_detect - Probe host controller cable detect info
 *	@ap: Port for which cable detect info is desired
 *
 *	Read 80c cable indicator from ATA PCI device's PCI config
 *	register.  This register is normally set by firmware (BIOS).
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void piix_pata_cbl_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host_set->dev);
	u8 tmp, mask;

	/* no 80c support in host controller? */
	if ((ap->udma_mask & ~ATA_UDMA_MASK_40C) == 0)
		goto cbl40;

	/* check BIOS cable detect results */
	mask = ap->hard_port_no == 0 ? PIIX_80C_PRI : PIIX_80C_SEC;
	pci_read_config_byte(pdev, PIIX_IOCFG, &tmp);
	if ((tmp & mask) == 0)
		goto cbl40;

	ap->cbl = ATA_CBL_PATA80;
	return;

cbl40:
	ap->cbl = ATA_CBL_PATA40;
	ap->udma_mask &= ATA_UDMA_MASK_40C;
}

/**
 *	piix_pata_probeinit - probeinit for PATA host controller
 *	@ap: Target port
 *
 *	Probeinit including cable detection.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void piix_pata_probeinit(struct ata_port *ap)
{
	piix_pata_cbl_detect(ap);
	ata_std_probeinit(ap);
}

/**
 *	piix_pata_probe_reset - Perform reset on PATA port and classify
 *	@ap: Port to reset
 *	@classes: Resulting classes of attached devices
 *
 *	Reset PATA phy and classify attached devices.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static int piix_pata_probe_reset(struct ata_port *ap, unsigned int *classes)
{
	struct pci_dev *pdev = to_pci_dev(ap->host_set->dev);

	if (!pci_test_config_bits(pdev, &piix_enable_bits[ap->hard_port_no])) {
		printk(KERN_INFO "ata%u: port disabled. ignoring.\n", ap->id);
		return 0;
	}

	return ata_drive_probe_reset(ap, piix_pata_probeinit,
				     ata_std_softreset, NULL,
				     ata_std_postreset, classes);
}

/**
 *	piix_sata_probe - Probe PCI device for present SATA devices
 *	@ap: Port associated with the PCI device we wish to probe
 *
 *	Reads and configures SATA PCI device's PCI config register
 *	Port Configuration and Status (PCS) to determine port and
 *	device availability.
 *
 *	LOCKING:
 *	None (inherited from caller).
 *
 *	RETURNS:
 *	Mask of avaliable devices on the port.
 */
static unsigned int piix_sata_probe (struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host_set->dev);
	const unsigned int *map = ap->host_set->private_data;
	int base = 2 * ap->hard_port_no;
	unsigned int present_mask = 0;
	int port, i;
	u8 pcs;

	pci_read_config_byte(pdev, ICH5_PCS, &pcs);
	DPRINTK("ata%u: ENTER, pcs=0x%x base=%d\n", ap->id, pcs, base);

	/* enable all ports on this ap and wait for them to settle */
	for (i = 0; i < 2; i++) {
		port = map[base + i];
		if (port >= 0)
			pcs |= 1 << port;
	}

	pci_write_config_byte(pdev, ICH5_PCS, pcs);
	msleep(100);

	/* let's see which devices are present */
	pci_read_config_byte(pdev, ICH5_PCS, &pcs);

	for (i = 0; i < 2; i++) {
		port = map[base + i];
		if (port < 0)
			continue;
		if (ap->flags & PIIX_FLAG_IGNORE_PCS || pcs & 1 << (4 + port))
			present_mask |= 1 << i;
		else
			pcs &= ~(1 << port);
	}

	/* disable offline ports on non-AHCI controllers */
	if (!(ap->flags & PIIX_FLAG_AHCI))
		pci_write_config_byte(pdev, ICH5_PCS, pcs);

	DPRINTK("ata%u: LEAVE, pcs=0x%x present_mask=0x%x\n",
		ap->id, pcs, present_mask);

	return present_mask;
}

/**
 *	piix_sata_probe_reset - Perform reset on SATA port and classify
 *	@ap: Port to reset
 *	@classes: Resulting classes of attached devices
 *
 *	Reset SATA phy and classify attached devices.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static int piix_sata_probe_reset(struct ata_port *ap, unsigned int *classes)
{
	if (!piix_sata_probe(ap)) {
		printk(KERN_INFO "ata%u: SATA port has no device.\n", ap->id);
		return 0;
	}

	return ata_drive_probe_reset(ap, ata_std_probeinit,
				     ata_std_softreset, NULL,
				     ata_std_postreset, classes);
}

/**
 *	piix_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *
 *	Set PIO mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void piix_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *dev	= to_pci_dev(ap->host_set->dev);
	unsigned int is_slave	= (adev->devno != 0);
	unsigned int master_port= ap->hard_port_no ? 0x42 : 0x40;
	unsigned int slave_port	= 0x44;
	u16 master_data;
	u8 slave_data;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(dev, master_port, &master_data);
	if (is_slave) {
		master_data |= 0x4000;
		/* enable PPE, IE and TIME */
		master_data |= 0x0070;
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= (ap->hard_port_no ? 0x0f : 0xf0);
		slave_data |=
			(timings[pio][0] << 2) |
			(timings[pio][1] << (ap->hard_port_no ? 4 : 0));
	} else {
		master_data &= 0xccf8;
		/* enable PPE, IE and TIME */
		master_data |= 0x0007;
		master_data |=
			(timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
}

/**
 *	piix_set_dmamode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *	@udma: udma mode, 0 - 6
 *
 *	Set UDMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void piix_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int udma	= adev->dma_mode; /* FIXME: MWDMA too */
	struct pci_dev *dev	= to_pci_dev(ap->host_set->dev);
	u8 maslave		= ap->hard_port_no ? 0x42 : 0x40;
	u8 speed		= udma;
	unsigned int drive_dn	= (ap->hard_port_no ? 2 : 0) + adev->devno;
	int a_speed		= 3 << (drive_dn * 4);
	int u_flag		= 1 << drive_dn;
	int v_flag		= 0x01 << drive_dn;
	int w_flag		= 0x10 << drive_dn;
	int u_speed		= 0;
	int			sitre;
	u16			reg4042, reg4a;
	u8			reg48, reg54, reg55;

	pci_read_config_word(dev, maslave, &reg4042);
	DPRINTK("reg4042 = 0x%04x\n", reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_byte(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);
	pci_read_config_byte(dev, 0x54, &reg54);
	pci_read_config_byte(dev, 0x55, &reg55);

	switch(speed) {
		case XFER_UDMA_4:
		case XFER_UDMA_2:	u_speed = 2 << (drive_dn * 4); break;
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_3:
		case XFER_UDMA_1:	u_speed = 1 << (drive_dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive_dn * 4); break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:	break;
		default:
			BUG();
			return;
	}

	if (speed >= XFER_UDMA_0) {
		if (!(reg48 & u_flag))
			pci_write_config_byte(dev, 0x48, reg48 | u_flag);
		if (speed == XFER_UDMA_5) {
			pci_write_config_byte(dev, 0x55, (u8) reg55|w_flag);
		} else {
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
		}
		if ((reg4a & a_speed) != u_speed)
			pci_write_config_word(dev, 0x4a, (reg4a & ~a_speed) | u_speed);
		if (speed > XFER_UDMA_2) {
			if (!(reg54 & v_flag))
				pci_write_config_byte(dev, 0x54, reg54 | v_flag);
		} else
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
	} else {
		if (reg48 & u_flag)
			pci_write_config_byte(dev, 0x48, reg48 & ~u_flag);
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		if (reg54 & v_flag)
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
		if (reg55 & w_flag)
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
	}
}

#define AHCI_PCI_BAR 5
#define AHCI_GLOBAL_CTL 0x04
#define AHCI_ENABLE (1 << 31)
static int piix_disable_ahci(struct pci_dev *pdev)
{
	void __iomem *mmio;
	u32 tmp;
	int rc = 0;

	/* BUG: pci_enable_device has not yet been called.  This
	 * works because this device is usually set up by BIOS.
	 */

	if (!pci_resource_start(pdev, AHCI_PCI_BAR) ||
	    !pci_resource_len(pdev, AHCI_PCI_BAR))
		return 0;

	mmio = pci_iomap(pdev, AHCI_PCI_BAR, 64);
	if (!mmio)
		return -ENOMEM;

	tmp = readl(mmio + AHCI_GLOBAL_CTL);
	if (tmp & AHCI_ENABLE) {
		tmp &= ~AHCI_ENABLE;
		writel(tmp, mmio + AHCI_GLOBAL_CTL);

		tmp = readl(mmio + AHCI_GLOBAL_CTL);
		if (tmp & AHCI_ENABLE)
			rc = -EIO;
	}

	pci_iounmap(pdev, mmio);
	return rc;
}

/**
 *	piix_check_450nx_errata	-	Check for problem 450NX setup
 *	@ata_dev: the PCI device to check
 *
 *	Check for the present of 450NX errata #19 and errata #25. If
 *	they are found return an error code so we can turn off DMA
 */

static int __devinit piix_check_450nx_errata(struct pci_dev *ata_dev)
{
	struct pci_dev *pdev = NULL;
	u16 cfg;
	u8 rev;
	int no_piix_dma = 0;

	while((pdev = pci_get_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82454NX, pdev)) != NULL)
	{
		/* Look for 450NX PXB. Check for problem configurations
		   A PCI quirk checks bit 6 already */
		pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);
		pci_read_config_word(pdev, 0x41, &cfg);
		/* Only on the original revision: IDE DMA can hang */
		if(rev == 0x00)
			no_piix_dma = 1;
		/* On all revisions below 5 PXB bus lock must be disabled for IDE */
		else if(cfg & (1<<14) && rev < 5)
			no_piix_dma = 2;
	}
	if(no_piix_dma)
		dev_printk(KERN_WARNING, &ata_dev->dev, "450NX errata present, disabling IDE DMA.\n");
	if(no_piix_dma == 2)
		dev_printk(KERN_WARNING, &ata_dev->dev, "A BIOS update may resolve this.\n");
	return no_piix_dma;
}

static void __devinit piix_init_sata_map(struct pci_dev *pdev,
					 struct ata_port_info *pinfo)
{
	struct piix_map_db *map_db = pinfo[0].private_data;
	const unsigned int *map;
	int i, invalid_map = 0;
	u8 map_value;

	pci_read_config_byte(pdev, ICH5_PMR, &map_value);

	map = map_db->map[map_value & map_db->mask];

	dev_printk(KERN_INFO, &pdev->dev, "MAP [");
	for (i = 0; i < 4; i++) {
		switch (map[i]) {
		case RV:
			invalid_map = 1;
			printk(" XX");
			break;

		case NA:
			printk(" --");
			break;

		case IDE:
			WARN_ON((i & 1) || map[i + 1] != IDE);
			pinfo[i / 2] = piix_port_info[ich5_pata];
			i++;
			printk(" IDE IDE");
			break;

		default:
			printk(" P%d", map[i]);
			if (i & 1)
				pinfo[i / 2].host_flags |= ATA_FLAG_SLAVE_POSS;
			break;
		}
	}
	printk(" ]\n");

	if (invalid_map)
		dev_printk(KERN_ERR, &pdev->dev,
			   "invalid MAP value %u\n", map_value);

	pinfo[0].private_data = (void *)map;
	pinfo[1].private_data = (void *)map;
}

/**
 *	piix_init_one - Register PIIX ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in piix_pci_tbl matching with @pdev
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

static int piix_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_port_info port_info[2];
	struct ata_port_info *ppinfo[2] = { &port_info[0], &port_info[1] };
	unsigned long host_flags;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	/* no hotplugging support (FIXME) */
	if (!in_module_init)
		return -ENODEV;

	port_info[0] = piix_port_info[ent->driver_data];
	port_info[1] = piix_port_info[ent->driver_data];

	host_flags = port_info[0].host_flags;

	if (host_flags & PIIX_FLAG_AHCI) {
		u8 tmp;
		pci_read_config_byte(pdev, PIIX_SCC, &tmp);
		if (tmp == PIIX_AHCI_DEVICE) {
			int rc = piix_disable_ahci(pdev);
			if (rc)
				return rc;
		}
	}

	/* Initialize SATA map */
	if (host_flags & ATA_FLAG_SATA)
		piix_init_sata_map(pdev, port_info);

	/* On ICH5, some BIOSen disable the interrupt using the
	 * PCI_COMMAND_INTX_DISABLE bit added in PCI 2.3.
	 * On ICH6, this bit has the same effect, but only when
	 * MSI is disabled (and it is disabled, as we don't use
	 * message-signalled interrupts currently).
	 */
	if (host_flags & PIIX_FLAG_CHECKINTR)
		pci_intx(pdev, 1);

	if (piix_check_450nx_errata(pdev)) {
		/* This writes into the master table but it does not
		   really matter for this errata as we will apply it to
		   all the PIIX devices on the board */
		port_info[0].mwdma_mask = 0;
		port_info[0].udma_mask = 0;
		port_info[1].mwdma_mask = 0;
		port_info[1].udma_mask = 0;
	}
	return ata_pci_init_one(pdev, ppinfo, 2);
}

static int __init piix_init(void)
{
	int rc;

	DPRINTK("pci_module_init\n");
	rc = pci_module_init(&piix_pci_driver);
	if (rc)
		return rc;

	in_module_init = 0;

	DPRINTK("done\n");
	return 0;
}

static void __exit piix_exit(void)
{
	pci_unregister_driver(&piix_pci_driver);
}

module_init(piix_init);
module_exit(piix_exit);

