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
 * driver the list of errata that are relevant is below, going back to
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
#include <linux/dmi.h>

#define DRV_NAME	"ata_piix"
#define DRV_VERSION	"2.12"

enum {
	PIIX_IOCFG		= 0x54, /* IDE I/O configuration register */
	ICH5_PMR		= 0x90, /* port mapping register */
	ICH5_PCS		= 0x92,	/* port control and status */
	PIIX_SIDPR_BAR		= 5,
	PIIX_SIDPR_LEN		= 16,
	PIIX_SIDPR_IDX		= 0,
	PIIX_SIDPR_DATA		= 4,

	PIIX_FLAG_CHECKINTR	= (1 << 28), /* make sure PCI INTx enabled */
	PIIX_FLAG_SIDPR		= (1 << 29), /* SATA idx/data pair regs */

	PIIX_PATA_FLAGS		= ATA_FLAG_SLAVE_POSS,
	PIIX_SATA_FLAGS		= ATA_FLAG_SATA | PIIX_FLAG_CHECKINTR,

	PIIX_80C_PRI		= (1 << 5) | (1 << 4),
	PIIX_80C_SEC		= (1 << 7) | (1 << 6),

	/* constants for mapping table */
	P0			= 0,  /* port 0 */
	P1			= 1,  /* port 1 */
	P2			= 2,  /* port 2 */
	P3			= 3,  /* port 3 */
	IDE			= -1, /* IDE */
	NA			= -2, /* not avaliable */
	RV			= -3, /* reserved */

	PIIX_AHCI_DEVICE	= 6,

	/* host->flags bits */
	PIIX_HOST_BROKEN_SUSPEND = (1 << 24),
};

enum piix_controller_ids {
	/* controller IDs */
	piix_pata_mwdma,	/* PIIX3 MWDMA only */
	piix_pata_33,		/* PIIX4 at 33Mhz */
	ich_pata_33,		/* ICH up to UDMA 33 only */
	ich_pata_66,		/* ICH up to 66 Mhz */
	ich_pata_100,		/* ICH up to UDMA 100 */
	ich5_sata,
	ich6_sata,
	ich6m_sata,
	ich8_sata,
	ich8_2port_sata,
	ich8m_apple_sata,	/* locks up on second port enable */
	tolapai_sata,
	piix_pata_vmw,			/* PIIX4 for VMware, spurious DMA_ERR */
};

struct piix_map_db {
	const u32 mask;
	const u16 port_enable;
	const int map[][4];
};

struct piix_host_priv {
	const int *map;
	void __iomem *sidpr;
};

static int piix_init_one(struct pci_dev *pdev,
			 const struct pci_device_id *ent);
static int piix_pata_prereset(struct ata_link *link, unsigned long deadline);
static void piix_set_piomode(struct ata_port *ap, struct ata_device *adev);
static void piix_set_dmamode(struct ata_port *ap, struct ata_device *adev);
static void ich_set_dmamode(struct ata_port *ap, struct ata_device *adev);
static int ich_pata_cable_detect(struct ata_port *ap);
static u8 piix_vmw_bmdma_status(struct ata_port *ap);
static int piix_sidpr_scr_read(struct ata_port *ap, unsigned int reg, u32 *val);
static int piix_sidpr_scr_write(struct ata_port *ap, unsigned int reg, u32 val);
#ifdef CONFIG_PM
static int piix_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg);
static int piix_pci_device_resume(struct pci_dev *pdev);
#endif

static unsigned int in_module_init = 1;

static const struct pci_device_id piix_pci_tbl[] = {
	/* Intel PIIX3 for the 430HX etc */
	{ 0x8086, 0x7010, PCI_ANY_ID, PCI_ANY_ID, 0, 0, piix_pata_mwdma },
	/* VMware ICH4 */
	{ 0x8086, 0x7111, 0x15ad, 0x1976, 0, 0, piix_pata_vmw },
	/* Intel PIIX4 for the 430TX/440BX/MX chipset: UDMA 33 */
	/* Also PIIX4E (fn3 rev 2) and PIIX4M (fn3 rev 3) */
	{ 0x8086, 0x7111, PCI_ANY_ID, PCI_ANY_ID, 0, 0, piix_pata_33 },
	/* Intel PIIX4 */
	{ 0x8086, 0x7199, PCI_ANY_ID, PCI_ANY_ID, 0, 0, piix_pata_33 },
	/* Intel PIIX4 */
	{ 0x8086, 0x7601, PCI_ANY_ID, PCI_ANY_ID, 0, 0, piix_pata_33 },
	/* Intel PIIX */
	{ 0x8086, 0x84CA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, piix_pata_33 },
	/* Intel ICH (i810, i815, i840) UDMA 66*/
	{ 0x8086, 0x2411, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_66 },
	/* Intel ICH0 : UDMA 33*/
	{ 0x8086, 0x2421, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_33 },
	/* Intel ICH2M */
	{ 0x8086, 0x244A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* Intel ICH2 (i810E2, i845, 850, 860) UDMA 100 */
	{ 0x8086, 0x244B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/*  Intel ICH3M */
	{ 0x8086, 0x248A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* Intel ICH3 (E7500/1) UDMA 100 */
	{ 0x8086, 0x248B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* Intel ICH4 (i845GV, i845E, i852, i855) UDMA 100 */
	{ 0x8086, 0x24CA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	{ 0x8086, 0x24CB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* Intel ICH5 */
	{ 0x8086, 0x24DB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* C-ICH (i810E2) */
	{ 0x8086, 0x245B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* ESB (855GME/875P + 6300ESB) UDMA 100  */
	{ 0x8086, 0x25A2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* ICH6 (and 6) (i915) UDMA 100 */
	{ 0x8086, 0x266F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* ICH7/7-R (i945, i975) UDMA 100*/
	{ 0x8086, 0x27DF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	{ 0x8086, 0x269E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },
	/* ICH8 Mobile PATA Controller */
	{ 0x8086, 0x2850, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich_pata_100 },

	/* NOTE: The following PCI ids must be kept in sync with the
	 * list in drivers/pci/quirks.c.
	 */

	/* 82801EB (ICH5) */
	{ 0x8086, 0x24d1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	/* 82801EB (ICH5) */
	{ 0x8086, 0x24df, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	/* 6300ESB (ICH5 variant with broken PCS present bits) */
	{ 0x8086, 0x25a3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	/* 6300ESB pretending RAID */
	{ 0x8086, 0x25b0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	/* 82801FB/FW (ICH6/ICH6W) */
	{ 0x8086, 0x2651, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata },
	/* 82801FR/FRW (ICH6R/ICH6RW) */
	{ 0x8086, 0x2652, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata },
	/* 82801FBM ICH6M (ICH6R with only port 0 and 2 implemented).
	 * Attach iff the controller is in IDE mode. */
	{ 0x8086, 0x2653, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_STORAGE_IDE << 8, 0xffff00, ich6m_sata },
	/* 82801GB/GR/GH (ICH7, identical to ICH6) */
	{ 0x8086, 0x27c0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata },
	/* 2801GBM/GHM (ICH7M, identical to ICH6M) */
	{ 0x8086, 0x27c4, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6m_sata },
	/* Enterprise Southbridge 2 (631xESB/632xESB) */
	{ 0x8086, 0x2680, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich6_sata },
	/* SATA Controller 1 IDE (ICH8) */
	{ 0x8086, 0x2820, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_sata },
	/* SATA Controller 2 IDE (ICH8) */
	{ 0x8086, 0x2825, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_2port_sata },
	/* Mobile SATA Controller IDE (ICH8M) */
	{ 0x8086, 0x2828, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_sata },
	/* Mobile SATA Controller IDE (ICH8M), Apple */
	{ 0x8086, 0x2828, 0x106b, 0x00a0, 0, 0, ich8m_apple_sata },
	/* SATA Controller IDE (ICH9) */
	{ 0x8086, 0x2920, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_sata },
	/* SATA Controller IDE (ICH9) */
	{ 0x8086, 0x2921, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_2port_sata },
	/* SATA Controller IDE (ICH9) */
	{ 0x8086, 0x2926, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_2port_sata },
	/* SATA Controller IDE (ICH9M) */
	{ 0x8086, 0x2928, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_2port_sata },
	/* SATA Controller IDE (ICH9M) */
	{ 0x8086, 0x292d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_2port_sata },
	/* SATA Controller IDE (ICH9M) */
	{ 0x8086, 0x292e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_sata },
	/* SATA Controller IDE (Tolapai) */
	{ 0x8086, 0x5028, PCI_ANY_ID, PCI_ANY_ID, 0, 0, tolapai_sata },
	/* SATA Controller IDE (ICH10) */
	{ 0x8086, 0x3a00, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_sata },
	/* SATA Controller IDE (ICH10) */
	{ 0x8086, 0x3a06, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_2port_sata },
	/* SATA Controller IDE (ICH10) */
	{ 0x8086, 0x3a20, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_sata },
	/* SATA Controller IDE (ICH10) */
	{ 0x8086, 0x3a26, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich8_2port_sata },

	{ }	/* terminate list */
};

static struct pci_driver piix_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= piix_pci_tbl,
	.probe			= piix_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= piix_pci_device_suspend,
	.resume			= piix_pci_device_resume,
#endif
};

static struct scsi_host_template piix_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations piix_pata_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.cable_detect		= ata_cable_40wire,
	.set_piomode		= piix_set_piomode,
	.set_dmamode		= piix_set_dmamode,
	.prereset		= piix_pata_prereset,
};

static struct ata_port_operations piix_vmw_ops = {
	.inherits		= &piix_pata_ops,
	.bmdma_status		= piix_vmw_bmdma_status,
};

static struct ata_port_operations ich_pata_ops = {
	.inherits		= &piix_pata_ops,
	.cable_detect		= ich_pata_cable_detect,
	.set_dmamode		= ich_set_dmamode,
};

static struct ata_port_operations piix_sata_ops = {
	.inherits		= &ata_bmdma_port_ops,
};

static struct ata_port_operations piix_sidpr_sata_ops = {
	.inherits		= &piix_sata_ops,
	.hardreset		= sata_std_hardreset,
	.scr_read		= piix_sidpr_scr_read,
	.scr_write		= piix_sidpr_scr_write,
};

static const struct piix_map_db ich5_map_db = {
	.mask = 0x7,
	.port_enable = 0x3,
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

static const struct piix_map_db ich6_map_db = {
	.mask = 0x3,
	.port_enable = 0xf,
	.map = {
		/* PM   PS   SM   SS       MAP */
		{  P0,  P2,  P1,  P3 }, /* 00b */
		{ IDE, IDE,  P1,  P3 }, /* 01b */
		{  P0,  P2, IDE, IDE }, /* 10b */
		{  RV,  RV,  RV,  RV },
	},
};

static const struct piix_map_db ich6m_map_db = {
	.mask = 0x3,
	.port_enable = 0x5,

	/* Map 01b isn't specified in the doc but some notebooks use
	 * it anyway.  MAP 01b have been spotted on both ICH6M and
	 * ICH7M.
	 */
	.map = {
		/* PM   PS   SM   SS       MAP */
		{  P0,  P2,  NA,  NA }, /* 00b */
		{ IDE, IDE,  P1,  P3 }, /* 01b */
		{  P0,  P2, IDE, IDE }, /* 10b */
		{  RV,  RV,  RV,  RV },
	},
};

static const struct piix_map_db ich8_map_db = {
	.mask = 0x3,
	.port_enable = 0xf,
	.map = {
		/* PM   PS   SM   SS       MAP */
		{  P0,  P2,  P1,  P3 }, /* 00b (hardwired when in AHCI) */
		{  RV,  RV,  RV,  RV },
		{  P0,  P2, IDE, IDE }, /* 10b (IDE mode) */
		{  RV,  RV,  RV,  RV },
	},
};

static const struct piix_map_db ich8_2port_map_db = {
	.mask = 0x3,
	.port_enable = 0x3,
	.map = {
		/* PM   PS   SM   SS       MAP */
		{  P0,  NA,  P1,  NA }, /* 00b */
		{  RV,  RV,  RV,  RV }, /* 01b */
		{  RV,  RV,  RV,  RV }, /* 10b */
		{  RV,  RV,  RV,  RV },
	},
};

static const struct piix_map_db ich8m_apple_map_db = {
	.mask = 0x3,
	.port_enable = 0x1,
	.map = {
		/* PM   PS   SM   SS       MAP */
		{  P0,  NA,  NA,  NA }, /* 00b */
		{  RV,  RV,  RV,  RV },
		{  P0,  P2, IDE, IDE }, /* 10b */
		{  RV,  RV,  RV,  RV },
	},
};

static const struct piix_map_db tolapai_map_db = {
	.mask = 0x3,
	.port_enable = 0x3,
	.map = {
		/* PM   PS   SM   SS       MAP */
		{  P0,  NA,  P1,  NA }, /* 00b */
		{  RV,  RV,  RV,  RV }, /* 01b */
		{  RV,  RV,  RV,  RV }, /* 10b */
		{  RV,  RV,  RV,  RV },
	},
};

static const struct piix_map_db *piix_map_db_table[] = {
	[ich5_sata]		= &ich5_map_db,
	[ich6_sata]		= &ich6_map_db,
	[ich6m_sata]		= &ich6m_map_db,
	[ich8_sata]		= &ich8_map_db,
	[ich8_2port_sata]	= &ich8_2port_map_db,
	[ich8m_apple_sata]	= &ich8m_apple_map_db,
	[tolapai_sata]		= &tolapai_map_db,
};

static struct ata_port_info piix_port_info[] = {
	[piix_pata_mwdma] = 	/* PIIX3 MWDMA only */
	{
		.flags		= PIIX_PATA_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x06, /* mwdma1-2 ?? CHECK 0 should be ok but slow */
		.port_ops	= &piix_pata_ops,
	},

	[piix_pata_33] =	/* PIIX4 at 33MHz */
	{
		.flags		= PIIX_PATA_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x06, /* mwdma1-2 ?? CHECK 0 should be ok but slow */
		.udma_mask	= ATA_UDMA_MASK_40C,
		.port_ops	= &piix_pata_ops,
	},

	[ich_pata_33] = 	/* ICH0 - ICH at 33Mhz*/
	{
		.flags		= PIIX_PATA_FLAGS,
		.pio_mask 	= 0x1f,	/* pio 0-4 */
		.mwdma_mask	= 0x06, /* Check: maybe 0x07  */
		.udma_mask	= ATA_UDMA2, /* UDMA33 */
		.port_ops	= &ich_pata_ops,
	},

	[ich_pata_66] = 	/* ICH controllers up to 66MHz */
	{
		.flags		= PIIX_PATA_FLAGS,
		.pio_mask 	= 0x1f,	/* pio 0-4 */
		.mwdma_mask	= 0x06, /* MWDMA0 is broken on chip */
		.udma_mask	= ATA_UDMA4,
		.port_ops	= &ich_pata_ops,
	},

	[ich_pata_100] =
	{
		.flags		= PIIX_PATA_FLAGS | PIIX_FLAG_CHECKINTR,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x06, /* mwdma1-2 */
		.udma_mask	= ATA_UDMA5, /* udma0-5 */
		.port_ops	= &ich_pata_ops,
	},

	[ich5_sata] =
	{
		.flags		= PIIX_SATA_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &piix_sata_ops,
	},

	[ich6_sata] =
	{
		.flags		= PIIX_SATA_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &piix_sata_ops,
	},

	[ich6m_sata] =
	{
		.flags		= PIIX_SATA_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &piix_sata_ops,
	},

	[ich8_sata] =
	{
		.flags		= PIIX_SATA_FLAGS | PIIX_FLAG_SIDPR,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &piix_sata_ops,
	},

	[ich8_2port_sata] =
	{
		.flags		= PIIX_SATA_FLAGS | PIIX_FLAG_SIDPR,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &piix_sata_ops,
	},

	[tolapai_sata] =
	{
		.flags		= PIIX_SATA_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &piix_sata_ops,
	},

	[ich8m_apple_sata] =
	{
		.flags		= PIIX_SATA_FLAGS | PIIX_FLAG_SIDPR,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &piix_sata_ops,
	},

	[piix_pata_vmw] =
	{
		.flags		= PIIX_PATA_FLAGS,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0x06, /* mwdma1-2 ?? CHECK 0 should be ok but slow */
		.udma_mask	= ATA_UDMA_MASK_40C,
		.port_ops	= &piix_vmw_ops,
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

struct ich_laptop {
	u16 device;
	u16 subvendor;
	u16 subdevice;
};

/*
 *	List of laptops that use short cables rather than 80 wire
 */

static const struct ich_laptop ich_laptop[] = {
	/* devid, subvendor, subdev */
	{ 0x27DF, 0x0005, 0x0280 },	/* ICH7 on Acer 5602WLMi */
	{ 0x27DF, 0x1025, 0x0102 },	/* ICH7 on Acer 5602aWLMi */
	{ 0x27DF, 0x1025, 0x0110 },	/* ICH7 on Acer 3682WLMi */
	{ 0x27DF, 0x1043, 0x1267 },	/* ICH7 on Asus W5F */
	{ 0x27DF, 0x103C, 0x30A1 },	/* ICH7 on HP Compaq nc2400 */
	{ 0x24CA, 0x1025, 0x0061 },	/* ICH4 on ACER Aspire 2023WLMi */
	{ 0x2653, 0x1043, 0x82D8 },	/* ICH6M on Asus Eee 701 */
	/* end marker */
	{ 0, }
};

/**
 *	ich_pata_cable_detect - Probe host controller cable detect info
 *	@ap: Port for which cable detect info is desired
 *
 *	Read 80c cable indicator from ATA PCI device's PCI config
 *	register.  This register is normally set by firmware (BIOS).
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static int ich_pata_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	const struct ich_laptop *lap = &ich_laptop[0];
	u8 tmp, mask;

	/* Check for specials - Acer Aspire 5602WLMi */
	while (lap->device) {
		if (lap->device == pdev->device &&
		    lap->subvendor == pdev->subsystem_vendor &&
		    lap->subdevice == pdev->subsystem_device)
			return ATA_CBL_PATA40_SHORT;

		lap++;
	}

	/* check BIOS cable detect results */
	mask = ap->port_no == 0 ? PIIX_80C_PRI : PIIX_80C_SEC;
	pci_read_config_byte(pdev, PIIX_IOCFG, &tmp);
	if ((tmp & mask) == 0)
		return ATA_CBL_PATA40;
	return ATA_CBL_PATA80;
}

/**
 *	piix_pata_prereset - prereset for PATA host controller
 *	@link: Target link
 *	@deadline: deadline jiffies for the operation
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static int piix_pata_prereset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (!pci_test_config_bits(pdev, &piix_enable_bits[ap->port_no]))
		return -ENOENT;
	return ata_sff_prereset(link, deadline);
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

static void piix_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	unsigned int is_slave	= (adev->devno != 0);
	unsigned int master_port= ap->port_no ? 0x42 : 0x40;
	unsigned int slave_port	= 0x44;
	u16 master_data;
	u8 slave_data;
	u8 udma_enable;
	int control = 0;

	/*
	 *	See Intel Document 298600-004 for the timing programing rules
	 *	for ICH controllers.
	 */

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	if (pio >= 2)
		control |= 1;	/* TIME1 enable */
	if (ata_pio_need_iordy(adev))
		control |= 2;	/* IE enable */

	/* Intel specifies that the PPE functionality is for disk only */
	if (adev->class == ATA_DEV_ATA)
		control |= 4;	/* PPE enable */

	/* PIO configuration clears DTE unconditionally.  It will be
	 * programmed in set_dmamode which is guaranteed to be called
	 * after set_piomode if any DMA mode is available.
	 */
	pci_read_config_word(dev, master_port, &master_data);
	if (is_slave) {
		/* clear TIME1|IE1|PPE1|DTE1 */
		master_data &= 0xff0f;
		/* Enable SITRE (separate slave timing register) */
		master_data |= 0x4000;
		/* enable PPE1, IE1 and TIME1 as needed */
		master_data |= (control << 4);
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= (ap->port_no ? 0x0f : 0xf0);
		/* Load the timing nibble for this slave */
		slave_data |= ((timings[pio][0] << 2) | timings[pio][1])
						<< (ap->port_no ? 4 : 0);
	} else {
		/* clear ISP|RCT|TIME0|IE0|PPE0|DTE0 */
		master_data &= 0xccf0;
		/* Enable PPE, IE and TIME as appropriate */
		master_data |= control;
		/* load ISP and RCT */
		master_data |=
			(timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);

	/* Ensure the UDMA bit is off - it will be turned back on if
	   UDMA is selected */

	if (ap->udma_mask) {
		pci_read_config_byte(dev, 0x48, &udma_enable);
		udma_enable &= ~(1 << (2 * ap->port_no + adev->devno));
		pci_write_config_byte(dev, 0x48, udma_enable);
	}
}

/**
 *	do_pata_set_dmamode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Drive in question
 *	@udma: udma mode, 0 - 6
 *	@isich: set if the chip is an ICH device
 *
 *	Set UDMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void do_pata_set_dmamode(struct ata_port *ap, struct ata_device *adev, int isich)
{
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	u8 master_port		= ap->port_no ? 0x42 : 0x40;
	u16 master_data;
	u8 speed		= adev->dma_mode;
	int devid		= adev->devno + 2 * ap->port_no;
	u8 udma_enable		= 0;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(dev, master_port, &master_data);
	if (ap->udma_mask)
		pci_read_config_byte(dev, 0x48, &udma_enable);

	if (speed >= XFER_UDMA_0) {
		unsigned int udma = adev->dma_mode - XFER_UDMA_0;
		u16 udma_timing;
		u16 ideconf;
		int u_clock, u_speed;

		/*
		 * UDMA is handled by a combination of clock switching and
		 * selection of dividers
		 *
		 * Handy rule: Odd modes are UDMATIMx 01, even are 02
		 *	       except UDMA0 which is 00
		 */
		u_speed = min(2 - (udma & 1), udma);
		if (udma == 5)
			u_clock = 0x1000;	/* 100Mhz */
		else if (udma > 2)
			u_clock = 1;		/* 66Mhz */
		else
			u_clock = 0;		/* 33Mhz */

		udma_enable |= (1 << devid);

		/* Load the CT/RP selection */
		pci_read_config_word(dev, 0x4A, &udma_timing);
		udma_timing &= ~(3 << (4 * devid));
		udma_timing |= u_speed << (4 * devid);
		pci_write_config_word(dev, 0x4A, udma_timing);

		if (isich) {
			/* Select a 33/66/100Mhz clock */
			pci_read_config_word(dev, 0x54, &ideconf);
			ideconf &= ~(0x1001 << devid);
			ideconf |= u_clock << devid;
			/* For ICH or later we should set bit 10 for better
			   performance (WR_PingPong_En) */
			pci_write_config_word(dev, 0x54, ideconf);
		}
	} else {
		/*
		 * MWDMA is driven by the PIO timings. We must also enable
		 * IORDY unconditionally along with TIME1. PPE has already
		 * been set when the PIO timing was set.
		 */
		unsigned int mwdma	= adev->dma_mode - XFER_MW_DMA_0;
		unsigned int control;
		u8 slave_data;
		const unsigned int needed_pio[3] = {
			XFER_PIO_0, XFER_PIO_3, XFER_PIO_4
		};
		int pio = needed_pio[mwdma] - XFER_PIO_0;

		control = 3;	/* IORDY|TIME1 */

		/* If the drive MWDMA is faster than it can do PIO then
		   we must force PIO into PIO0 */

		if (adev->pio_mode < needed_pio[mwdma])
			/* Enable DMA timing only */
			control |= 8;	/* PIO cycles in PIO0 */

		if (adev->devno) {	/* Slave */
			master_data &= 0xFF4F;  /* Mask out IORDY|TIME1|DMAONLY */
			master_data |= control << 4;
			pci_read_config_byte(dev, 0x44, &slave_data);
			slave_data &= (ap->port_no ? 0x0f : 0xf0);
			/* Load the matching timing */
			slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) << (ap->port_no ? 4 : 0);
			pci_write_config_byte(dev, 0x44, slave_data);
		} else { 	/* Master */
			master_data &= 0xCCF4;	/* Mask out IORDY|TIME1|DMAONLY
						   and master timing bits */
			master_data |= control;
			master_data |=
				(timings[pio][0] << 12) |
				(timings[pio][1] << 8);
		}

		if (ap->udma_mask) {
			udma_enable &= ~(1 << devid);
			pci_write_config_word(dev, master_port, master_data);
		}
	}
	/* Don't scribble on 0x48 if the controller does not support UDMA */
	if (ap->udma_mask)
		pci_write_config_byte(dev, 0x48, udma_enable);
}

/**
 *	piix_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *
 *	Set MW/UDMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void piix_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	do_pata_set_dmamode(ap, adev, 0);
}

/**
 *	ich_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *
 *	Set MW/UDMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void ich_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	do_pata_set_dmamode(ap, adev, 1);
}

/*
 * Serial ATA Index/Data Pair Superset Registers access
 *
 * Beginning from ICH8, there's a sane way to access SCRs using index
 * and data register pair located at BAR5.  This creates an
 * interesting problem of mapping two SCRs to one port.
 *
 * Although they have separate SCRs, the master and slave aren't
 * independent enough to be treated as separate links - e.g. softreset
 * resets both.  Also, there's no protocol defined for hard resetting
 * singled device sharing the virtual port (no defined way to acquire
 * device signature).  This is worked around by merging the SCR values
 * into one sensible value and requesting follow-up SRST after
 * hardreset.
 *
 * SCR merging is perfomed in nibbles which is the unit contents in
 * SCRs are organized.  If two values are equal, the value is used.
 * When they differ, merge table which lists precedence of possible
 * values is consulted and the first match or the last entry when
 * nothing matches is used.  When there's no merge table for the
 * specific nibble, value from the first port is used.
 */
static const int piix_sidx_map[] = {
	[SCR_STATUS]	= 0,
	[SCR_ERROR]	= 2,
	[SCR_CONTROL]	= 1,
};

static void piix_sidpr_sel(struct ata_device *dev, unsigned int reg)
{
	struct ata_port *ap = dev->link->ap;
	struct piix_host_priv *hpriv = ap->host->private_data;

	iowrite32(((ap->port_no * 2 + dev->devno) << 8) | piix_sidx_map[reg],
		  hpriv->sidpr + PIIX_SIDPR_IDX);
}

static int piix_sidpr_read(struct ata_device *dev, unsigned int reg)
{
	struct piix_host_priv *hpriv = dev->link->ap->host->private_data;

	piix_sidpr_sel(dev, reg);
	return ioread32(hpriv->sidpr + PIIX_SIDPR_DATA);
}

static void piix_sidpr_write(struct ata_device *dev, unsigned int reg, u32 val)
{
	struct piix_host_priv *hpriv = dev->link->ap->host->private_data;

	piix_sidpr_sel(dev, reg);
	iowrite32(val, hpriv->sidpr + PIIX_SIDPR_DATA);
}

static u32 piix_merge_scr(u32 val0, u32 val1, const int * const *merge_tbl)
{
	u32 val = 0;
	int i, mi;

	for (i = 0, mi = 0; i < 32 / 4; i++) {
		u8 c0 = (val0 >> (i * 4)) & 0xf;
		u8 c1 = (val1 >> (i * 4)) & 0xf;
		u8 merged = c0;
		const int *cur;

		/* if no merge preference, assume the first value */
		cur = merge_tbl[mi];
		if (!cur)
			goto done;
		mi++;

		/* if two values equal, use it */
		if (c0 == c1)
			goto done;

		/* choose the first match or the last from the merge table */
		while (*cur != -1) {
			if (c0 == *cur || c1 == *cur)
				break;
			cur++;
		}
		if (*cur == -1)
			cur--;
		merged = *cur;
	done:
		val |= merged << (i * 4);
	}

	return val;
}

static int piix_sidpr_scr_read(struct ata_port *ap, unsigned int reg, u32 *val)
{
	const int * const sstatus_merge_tbl[] = {
		/* DET */ (const int []){ 1, 3, 0, 4, 3, -1 },
		/* SPD */ (const int []){ 2, 1, 0, -1 },
		/* IPM */ (const int []){ 6, 2, 1, 0, -1 },
		NULL,
	};
	const int * const scontrol_merge_tbl[] = {
		/* DET */ (const int []){ 1, 0, 4, 0, -1 },
		/* SPD */ (const int []){ 0, 2, 1, 0, -1 },
		/* IPM */ (const int []){ 0, 1, 2, 3, 0, -1 },
		NULL,
	};
	u32 v0, v1;

	if (reg >= ARRAY_SIZE(piix_sidx_map))
		return -EINVAL;

	if (!(ap->flags & ATA_FLAG_SLAVE_POSS)) {
		*val = piix_sidpr_read(&ap->link.device[0], reg);
		return 0;
	}

	v0 = piix_sidpr_read(&ap->link.device[0], reg);
	v1 = piix_sidpr_read(&ap->link.device[1], reg);

	switch (reg) {
	case SCR_STATUS:
		*val = piix_merge_scr(v0, v1, sstatus_merge_tbl);
		break;
	case SCR_ERROR:
		*val = v0 | v1;
		break;
	case SCR_CONTROL:
		*val = piix_merge_scr(v0, v1, scontrol_merge_tbl);
		break;
	}

	return 0;
}

static int piix_sidpr_scr_write(struct ata_port *ap, unsigned int reg, u32 val)
{
	if (reg >= ARRAY_SIZE(piix_sidx_map))
		return -EINVAL;

	piix_sidpr_write(&ap->link.device[0], reg, val);

	if (ap->flags & ATA_FLAG_SLAVE_POSS)
		piix_sidpr_write(&ap->link.device[1], reg, val);

	return 0;
}

#ifdef CONFIG_PM
static int piix_broken_suspend(void)
{
	static const struct dmi_system_id sysids[] = {
		{
			.ident = "TECRA M3",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "TECRA M3"),
			},
		},
		{
			.ident = "TECRA M3",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "Tecra M3"),
			},
		},
		{
			.ident = "TECRA M4",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "Tecra M4"),
			},
		},
		{
			.ident = "TECRA M5",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "TECRA M5"),
			},
		},
		{
			.ident = "TECRA M6",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "TECRA M6"),
			},
		},
		{
			.ident = "TECRA M7",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "TECRA M7"),
			},
		},
		{
			.ident = "TECRA A8",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "TECRA A8"),
			},
		},
		{
			.ident = "Satellite R20",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "Satellite R20"),
			},
		},
		{
			.ident = "Satellite R25",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "Satellite R25"),
			},
		},
		{
			.ident = "Satellite U200",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "Satellite U200"),
			},
		},
		{
			.ident = "Satellite U200",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "SATELLITE U200"),
			},
		},
		{
			.ident = "Satellite Pro U200",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "SATELLITE PRO U200"),
			},
		},
		{
			.ident = "Satellite U205",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "Satellite U205"),
			},
		},
		{
			.ident = "SATELLITE U205",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "SATELLITE U205"),
			},
		},
		{
			.ident = "Portege M500",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
				DMI_MATCH(DMI_PRODUCT_NAME, "PORTEGE M500"),
			},
		},

		{ }	/* terminate list */
	};
	static const char *oemstrs[] = {
		"Tecra M3,",
	};
	int i;

	if (dmi_check_system(sysids))
		return 1;

	for (i = 0; i < ARRAY_SIZE(oemstrs); i++)
		if (dmi_find_device(DMI_DEV_TYPE_OEM_STRING, oemstrs[i], NULL))
			return 1;

	return 0;
}

static int piix_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	unsigned long flags;
	int rc = 0;

	rc = ata_host_suspend(host, mesg);
	if (rc)
		return rc;

	/* Some braindamaged ACPI suspend implementations expect the
	 * controller to be awake on entry; otherwise, it burns cpu
	 * cycles and power trying to do something to the sleeping
	 * beauty.
	 */
	if (piix_broken_suspend() && (mesg.event & PM_EVENT_SLEEP)) {
		pci_save_state(pdev);

		/* mark its power state as "unknown", since we don't
		 * know if e.g. the BIOS will change its device state
		 * when we suspend.
		 */
		if (pdev->current_state == PCI_D0)
			pdev->current_state = PCI_UNKNOWN;

		/* tell resume that it's waking up from broken suspend */
		spin_lock_irqsave(&host->lock, flags);
		host->flags |= PIIX_HOST_BROKEN_SUSPEND;
		spin_unlock_irqrestore(&host->lock, flags);
	} else
		ata_pci_device_do_suspend(pdev, mesg);

	return 0;
}

static int piix_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	unsigned long flags;
	int rc;

	if (host->flags & PIIX_HOST_BROKEN_SUSPEND) {
		spin_lock_irqsave(&host->lock, flags);
		host->flags &= ~PIIX_HOST_BROKEN_SUSPEND;
		spin_unlock_irqrestore(&host->lock, flags);

		pci_set_power_state(pdev, PCI_D0);
		pci_restore_state(pdev);

		/* PCI device wasn't disabled during suspend.  Use
		 * pci_reenable_device() to avoid affecting the enable
		 * count.
		 */
		rc = pci_reenable_device(pdev);
		if (rc)
			dev_printk(KERN_ERR, &pdev->dev, "failed to enable "
				   "device after resume (%d)\n", rc);
	} else
		rc = ata_pci_device_do_resume(pdev);

	if (rc == 0)
		ata_host_resume(host);

	return rc;
}
#endif

static u8 piix_vmw_bmdma_status(struct ata_port *ap)
{
	return ata_bmdma_status(ap) & ~ATA_DMA_ERR;
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

	tmp = ioread32(mmio + AHCI_GLOBAL_CTL);
	if (tmp & AHCI_ENABLE) {
		tmp &= ~AHCI_ENABLE;
		iowrite32(tmp, mmio + AHCI_GLOBAL_CTL);

		tmp = ioread32(mmio + AHCI_GLOBAL_CTL);
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
	int no_piix_dma = 0;

	while ((pdev = pci_get_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82454NX, pdev)) != NULL) {
		/* Look for 450NX PXB. Check for problem configurations
		   A PCI quirk checks bit 6 already */
		pci_read_config_word(pdev, 0x41, &cfg);
		/* Only on the original revision: IDE DMA can hang */
		if (pdev->revision == 0x00)
			no_piix_dma = 1;
		/* On all revisions below 5 PXB bus lock must be disabled for IDE */
		else if (cfg & (1<<14) && pdev->revision < 5)
			no_piix_dma = 2;
	}
	if (no_piix_dma)
		dev_printk(KERN_WARNING, &ata_dev->dev, "450NX errata present, disabling IDE DMA.\n");
	if (no_piix_dma == 2)
		dev_printk(KERN_WARNING, &ata_dev->dev, "A BIOS update may resolve this.\n");
	return no_piix_dma;
}

static void __devinit piix_init_pcs(struct ata_host *host,
				    const struct piix_map_db *map_db)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	u16 pcs, new_pcs;

	pci_read_config_word(pdev, ICH5_PCS, &pcs);

	new_pcs = pcs | map_db->port_enable;

	if (new_pcs != pcs) {
		DPRINTK("updating PCS from 0x%x to 0x%x\n", pcs, new_pcs);
		pci_write_config_word(pdev, ICH5_PCS, new_pcs);
		msleep(150);
	}
}

static const int *__devinit piix_init_sata_map(struct pci_dev *pdev,
					       struct ata_port_info *pinfo,
					       const struct piix_map_db *map_db)
{
	const int *map;
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
			pinfo[i / 2] = piix_port_info[ich_pata_100];
			i++;
			printk(" IDE IDE");
			break;

		default:
			printk(" P%d", map[i]);
			if (i & 1)
				pinfo[i / 2].flags |= ATA_FLAG_SLAVE_POSS;
			break;
		}
	}
	printk(" ]\n");

	if (invalid_map)
		dev_printk(KERN_ERR, &pdev->dev,
			   "invalid MAP value %u\n", map_value);

	return map;
}

static void __devinit piix_init_sidpr(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	struct piix_host_priv *hpriv = host->private_data;
	int i;

	/* check for availability */
	for (i = 0; i < 4; i++)
		if (hpriv->map[i] == IDE)
			return;

	if (!(host->ports[0]->flags & PIIX_FLAG_SIDPR))
		return;

	if (pci_resource_start(pdev, PIIX_SIDPR_BAR) == 0 ||
	    pci_resource_len(pdev, PIIX_SIDPR_BAR) != PIIX_SIDPR_LEN)
		return;

	if (pcim_iomap_regions(pdev, 1 << PIIX_SIDPR_BAR, DRV_NAME))
		return;

	hpriv->sidpr = pcim_iomap_table(pdev)[PIIX_SIDPR_BAR];
	host->ports[0]->ops = &piix_sidpr_sata_ops;
	host->ports[1]->ops = &piix_sidpr_sata_ops;
}

static void piix_iocfg_bit18_quirk(struct pci_dev *pdev)
{
	static const struct dmi_system_id sysids[] = {
		{
			/* Clevo M570U sets IOCFG bit 18 if the cdrom
			 * isn't used to boot the system which
			 * disables the channel.
			 */
			.ident = "M570U",
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "Clevo Co."),
				DMI_MATCH(DMI_PRODUCT_NAME, "M570U"),
			},
		},

		{ }	/* terminate list */
	};
	u32 iocfg;

	if (!dmi_check_system(sysids))
		return;

	/* The datasheet says that bit 18 is NOOP but certain systems
	 * seem to use it to disable a channel.  Clear the bit on the
	 * affected systems.
	 */
	pci_read_config_dword(pdev, PIIX_IOCFG, &iocfg);
	if (iocfg & (1 << 18)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "applying IOCFG bit18 quirk\n");
		iocfg &= ~(1 << 18);
		pci_write_config_dword(pdev, PIIX_IOCFG, iocfg);
	}
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

static int __devinit piix_init_one(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	static int printed_version;
	struct device *dev = &pdev->dev;
	struct ata_port_info port_info[2];
	const struct ata_port_info *ppi[] = { &port_info[0], &port_info[1] };
	unsigned long port_flags;
	struct ata_host *host;
	struct piix_host_priv *hpriv;
	int rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	/* no hotplugging support (FIXME) */
	if (!in_module_init)
		return -ENODEV;

	port_info[0] = piix_port_info[ent->driver_data];
	port_info[1] = piix_port_info[ent->driver_data];

	port_flags = port_info[0].flags;

	/* enable device and prepare host */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* ICH6R may be driven by either ata_piix or ahci driver
	 * regardless of BIOS configuration.  Make sure AHCI mode is
	 * off.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_INTEL && pdev->device == 0x2652) {
		int rc = piix_disable_ahci(pdev);
		if (rc)
			return rc;
	}

	/* SATA map init can change port_info, do it before prepping host */
	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	if (port_flags & ATA_FLAG_SATA)
		hpriv->map = piix_init_sata_map(pdev, port_info,
					piix_map_db_table[ent->driver_data]);

	rc = ata_pci_sff_prepare_host(pdev, ppi, &host);
	if (rc)
		return rc;
	host->private_data = hpriv;

	/* initialize controller */
	if (port_flags & ATA_FLAG_SATA) {
		piix_init_pcs(host, piix_map_db_table[ent->driver_data]);
		piix_init_sidpr(host);
	}

	/* apply IOCFG bit18 quirk */
	piix_iocfg_bit18_quirk(pdev);

	/* On ICH5, some BIOSen disable the interrupt using the
	 * PCI_COMMAND_INTX_DISABLE bit added in PCI 2.3.
	 * On ICH6, this bit has the same effect, but only when
	 * MSI is disabled (and it is disabled, as we don't use
	 * message-signalled interrupts currently).
	 */
	if (port_flags & PIIX_FLAG_CHECKINTR)
		pci_intx(pdev, 1);

	if (piix_check_450nx_errata(pdev)) {
		/* This writes into the master table but it does not
		   really matter for this errata as we will apply it to
		   all the PIIX devices on the board */
		host->ports[0]->mwdma_mask = 0;
		host->ports[0]->udma_mask = 0;
		host->ports[1]->mwdma_mask = 0;
		host->ports[1]->udma_mask = 0;
	}

	pci_set_master(pdev);
	return ata_pci_sff_activate_host(host, ata_sff_interrupt, &piix_sht);
}

static int __init piix_init(void)
{
	int rc;

	DPRINTK("pci_register_driver\n");
	rc = pci_register_driver(&piix_pci_driver);
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
