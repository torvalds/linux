/*
 *  sata_sis.c - Silicon Integrated Systems SATA
 *
 *  Maintained by:  Uwe Koziolek
 *  		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2004 Uwe Koziolek
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
 *  Hardware documentation available under NDA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include "sis.h"

#define DRV_NAME	"sata_sis"
#define DRV_VERSION	"0.8"

enum {
	sis_180			= 0,
	SIS_SCR_PCI_BAR		= 5,

	/* PCI configuration registers */
	SIS_GENCTL		= 0x54, /* IDE General Control register */
	SIS_SCR_BASE		= 0xc0, /* sata0 phy SCR registers */
	SIS180_SATA1_OFS	= 0x10, /* offset from sata0->sata1 phy regs */
	SIS182_SATA1_OFS	= 0x20, /* offset from sata0->sata1 phy regs */
	SIS_PMR			= 0x90, /* port mapping register */
	SIS_PMR_COMBINED	= 0x30,

	/* random bits */
	SIS_FLAG_CFGSCR		= (1 << 30), /* host flag: SCRs via PCI cfg */

	GENCTL_IOMAPPED_SCR	= (1 << 26), /* if set, SCRs are in IO space */
};

static int sis_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static u32 sis_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void sis_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);

static const struct pci_device_id sis_pci_tbl[] = {
	{ PCI_VDEVICE(SI, 0x0180), sis_180 },		/* SiS 964/180 */
	{ PCI_VDEVICE(SI, 0x0181), sis_180 },		/* SiS 964/180 */
	{ PCI_VDEVICE(SI, 0x0182), sis_180 },		/* SiS 965/965L */
	{ PCI_VDEVICE(SI, 0x0183), sis_180 },		/* SiS 965/965L */
	{ PCI_VDEVICE(SI, 0x1182), sis_180 },		/* SiS 966/680 */
	{ PCI_VDEVICE(SI, 0x1183), sis_180 },		/* SiS 966/966L/968/680 */

	{ }	/* terminate list */
};

static struct pci_driver sis_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= sis_pci_tbl,
	.probe			= sis_init_one,
	.remove			= ata_pci_remove_one,
};

static struct scsi_host_template sis_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= ATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations sis_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup            = ata_bmdma_setup,
	.bmdma_start            = ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,
	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= ata_bmdma_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,
	.scr_read		= sis_scr_read,
	.scr_write		= sis_scr_write,
	.port_start		= ata_port_start,
};

static const struct ata_port_info sis_port_info = {
	.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY,
	.pio_mask	= 0x1f,
	.mwdma_mask	= 0x7,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &sis_ops,
};

MODULE_AUTHOR("Uwe Koziolek");
MODULE_DESCRIPTION("low-level driver for Silicon Integratad Systems SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, sis_pci_tbl);
MODULE_VERSION(DRV_VERSION);

static unsigned int get_scr_cfg_addr(struct ata_port *ap, unsigned int sc_reg)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned int addr = SIS_SCR_BASE + (4 * sc_reg);
	u8 pmr;

	if (ap->port_no)  {
		switch (pdev->device) {
			case 0x0180:
			case 0x0181:
				pci_read_config_byte(pdev, SIS_PMR, &pmr);
				if ((pmr & SIS_PMR_COMBINED) == 0)
					addr += SIS180_SATA1_OFS;
				break;

			case 0x0182:
			case 0x0183:
			case 0x1182:
				addr += SIS182_SATA1_OFS;
				break;
		}
	}
	return addr;
}

static u32 sis_scr_cfg_read (struct ata_port *ap, unsigned int sc_reg)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned int cfg_addr = get_scr_cfg_addr(ap, sc_reg);
	u32 val, val2 = 0;
	u8 pmr;

	if (sc_reg == SCR_ERROR) /* doesn't exist in PCI cfg space */
		return 0xffffffff;

	pci_read_config_byte(pdev, SIS_PMR, &pmr);

	pci_read_config_dword(pdev, cfg_addr, &val);

	if ((pdev->device == 0x0182) || (pdev->device == 0x0183) ||
	    (pdev->device == 0x1182) || (pmr & SIS_PMR_COMBINED))
		pci_read_config_dword(pdev, cfg_addr+0x10, &val2);

	return (val|val2) &  0xfffffffb; /* avoid problems with powerdowned ports */
}

static void sis_scr_cfg_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned int cfg_addr = get_scr_cfg_addr(ap, sc_reg);
	u8 pmr;

	if (sc_reg == SCR_ERROR) /* doesn't exist in PCI cfg space */
		return;

	pci_read_config_byte(pdev, SIS_PMR, &pmr);

	pci_write_config_dword(pdev, cfg_addr, val);

	if ((pdev->device == 0x0182) || (pdev->device == 0x0183) ||
	    (pdev->device == 0x1182) || (pmr & SIS_PMR_COMBINED))
		pci_write_config_dword(pdev, cfg_addr+0x10, val);
}

static u32 sis_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 val, val2 = 0;
	u8 pmr;

	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;

	if (ap->flags & SIS_FLAG_CFGSCR)
		return sis_scr_cfg_read(ap, sc_reg);

	pci_read_config_byte(pdev, SIS_PMR, &pmr);

	val = ioread32(ap->ioaddr.scr_addr + (sc_reg * 4));

	if ((pdev->device == 0x0182) || (pdev->device == 0x0183) ||
	    (pdev->device == 0x1182) || (pmr & SIS_PMR_COMBINED))
		val2 = ioread32(ap->ioaddr.scr_addr + (sc_reg * 4) + 0x10);

	return (val | val2) &  0xfffffffb;
}

static void sis_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 pmr;

	if (sc_reg > SCR_CONTROL)
		return;

	pci_read_config_byte(pdev, SIS_PMR, &pmr);

	if (ap->flags & SIS_FLAG_CFGSCR)
		sis_scr_cfg_write(ap, sc_reg, val);
	else {
		iowrite32(val, ap->ioaddr.scr_addr + (sc_reg * 4));
		if ((pdev->device == 0x0182) || (pdev->device == 0x0183) ||
		    (pdev->device == 0x1182) || (pmr & SIS_PMR_COMBINED))
			iowrite32(val, ap->ioaddr.scr_addr + (sc_reg * 4)+0x10);
	}
}

static int sis_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_port_info pi = sis_port_info;
	const struct ata_port_info *ppi[] = { &pi, &pi };
	struct ata_host *host;
	u32 genctl, val;
	u8 pmr;
	u8 port2_start = 0x20;
	int rc;

	if (!printed_version++)
		dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* check and see if the SCRs are in IO space or PCI cfg space */
	pci_read_config_dword(pdev, SIS_GENCTL, &genctl);
	if ((genctl & GENCTL_IOMAPPED_SCR) == 0)
		pi.flags |= SIS_FLAG_CFGSCR;

	/* if hardware thinks SCRs are in IO space, but there are
	 * no IO resources assigned, change to PCI cfg space.
	 */
	if ((!(pi.flags & SIS_FLAG_CFGSCR)) &&
	    ((pci_resource_start(pdev, SIS_SCR_PCI_BAR) == 0) ||
	     (pci_resource_len(pdev, SIS_SCR_PCI_BAR) < 128))) {
		genctl &= ~GENCTL_IOMAPPED_SCR;
		pci_write_config_dword(pdev, SIS_GENCTL, genctl);
		pi.flags |= SIS_FLAG_CFGSCR;
	}

	pci_read_config_byte(pdev, SIS_PMR, &pmr);
	switch (ent->device) {
	case 0x0180:
	case 0x0181:

		/* The PATA-handling is provided by pata_sis */
		switch (pmr & 0x30) {
		case 0x10:
			ppi[1] = &sis_info133_for_sata;
			break;

		case 0x30:
			ppi[0] = &sis_info133_for_sata;
			break;
		}
		if ((pmr & SIS_PMR_COMBINED) == 0) {
			dev_printk(KERN_INFO, &pdev->dev,
				   "Detected SiS 180/181/964 chipset in SATA mode\n");
			port2_start = 64;
		} else {
			dev_printk(KERN_INFO, &pdev->dev,
				   "Detected SiS 180/181 chipset in combined mode\n");
			port2_start=0;
			pi.flags |= ATA_FLAG_SLAVE_POSS;
		}
		break;

	case 0x0182:
	case 0x0183:
		pci_read_config_dword ( pdev, 0x6C, &val);
		if (val & (1L << 31)) {
			dev_printk(KERN_INFO, &pdev->dev, "Detected SiS 182/965 chipset\n");
			pi.flags |= ATA_FLAG_SLAVE_POSS;
		} else {
			dev_printk(KERN_INFO, &pdev->dev, "Detected SiS 182/965L chipset\n");
		}
		break;

	case 0x1182:
		dev_printk(KERN_INFO, &pdev->dev, "Detected SiS 1182/966/680 SATA controller\n");
		pi.flags |= ATA_FLAG_SLAVE_POSS;
		break;

	case 0x1183:
		dev_printk(KERN_INFO, &pdev->dev, "Detected SiS 1183/966/966L/968/680 controller in PATA mode\n");
		ppi[0] = &sis_info133_for_sata;
		ppi[1] = &sis_info133_for_sata;
		break;
	}

	rc = ata_pci_prepare_native_host(pdev, ppi, &host);
	if (rc)
		return rc;

	if (!(pi.flags & SIS_FLAG_CFGSCR)) {
		void __iomem *mmio;

		rc = pcim_iomap_regions(pdev, 1 << SIS_SCR_PCI_BAR, DRV_NAME);
		if (rc)
			return rc;
		mmio = host->iomap[SIS_SCR_PCI_BAR];

		host->ports[0]->ioaddr.scr_addr = mmio;
		host->ports[1]->ioaddr.scr_addr = mmio + port2_start;
	}

	pci_set_master(pdev);
	pci_intx(pdev, 1);
	return ata_host_activate(host, pdev->irq, ata_interrupt, IRQF_SHARED,
				 &sis_sht);
}

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
