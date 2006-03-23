/*
 *  sata_nv.c - NVIDIA nForce SATA
 *
 *  Copyright 2004 NVIDIA Corp.  All rights reserved.
 *  Copyright 2004 Andrew Chew
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
 *  No hardware documentation available outside of NVIDIA.
 *  This driver programs the NVIDIA SATA controller in a similar
 *  fashion as with other PCI IDE BMDMA controllers, with a few
 *  NV-specific details such as register offsets, SATA phy location,
 *  hotplug info, etc.
 *
 */

#include <linux/config.h>
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

#define DRV_NAME			"sata_nv"
#define DRV_VERSION			"0.8"

enum {
	NV_PORTS			= 2,
	NV_PIO_MASK			= 0x1f,
	NV_MWDMA_MASK			= 0x07,
	NV_UDMA_MASK			= 0x7f,
	NV_PORT0_SCR_REG_OFFSET		= 0x00,
	NV_PORT1_SCR_REG_OFFSET		= 0x40,

	NV_INT_STATUS			= 0x10,
	NV_INT_STATUS_CK804		= 0x440,
	NV_INT_STATUS_PDEV_INT		= 0x01,
	NV_INT_STATUS_PDEV_PM		= 0x02,
	NV_INT_STATUS_PDEV_ADDED	= 0x04,
	NV_INT_STATUS_PDEV_REMOVED	= 0x08,
	NV_INT_STATUS_SDEV_INT		= 0x10,
	NV_INT_STATUS_SDEV_PM		= 0x20,
	NV_INT_STATUS_SDEV_ADDED	= 0x40,
	NV_INT_STATUS_SDEV_REMOVED	= 0x80,
	NV_INT_STATUS_PDEV_HOTPLUG	= (NV_INT_STATUS_PDEV_ADDED |
					   NV_INT_STATUS_PDEV_REMOVED),
	NV_INT_STATUS_SDEV_HOTPLUG	= (NV_INT_STATUS_SDEV_ADDED |
					   NV_INT_STATUS_SDEV_REMOVED),
	NV_INT_STATUS_HOTPLUG		= (NV_INT_STATUS_PDEV_HOTPLUG |
					   NV_INT_STATUS_SDEV_HOTPLUG),

	NV_INT_ENABLE			= 0x11,
	NV_INT_ENABLE_CK804		= 0x441,
	NV_INT_ENABLE_PDEV_MASK		= 0x01,
	NV_INT_ENABLE_PDEV_PM		= 0x02,
	NV_INT_ENABLE_PDEV_ADDED	= 0x04,
	NV_INT_ENABLE_PDEV_REMOVED	= 0x08,
	NV_INT_ENABLE_SDEV_MASK		= 0x10,
	NV_INT_ENABLE_SDEV_PM		= 0x20,
	NV_INT_ENABLE_SDEV_ADDED	= 0x40,
	NV_INT_ENABLE_SDEV_REMOVED	= 0x80,
	NV_INT_ENABLE_PDEV_HOTPLUG	= (NV_INT_ENABLE_PDEV_ADDED |
					   NV_INT_ENABLE_PDEV_REMOVED),
	NV_INT_ENABLE_SDEV_HOTPLUG	= (NV_INT_ENABLE_SDEV_ADDED |
					   NV_INT_ENABLE_SDEV_REMOVED),
	NV_INT_ENABLE_HOTPLUG		= (NV_INT_ENABLE_PDEV_HOTPLUG |
					   NV_INT_ENABLE_SDEV_HOTPLUG),

	NV_INT_CONFIG			= 0x12,
	NV_INT_CONFIG_METHD		= 0x01, // 0 = INT, 1 = SMI

	// For PCI config register 20
	NV_MCP_SATA_CFG_20		= 0x50,
	NV_MCP_SATA_CFG_20_SATA_SPACE_EN = 0x04,
};

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static irqreturn_t nv_interrupt (int irq, void *dev_instance,
				 struct pt_regs *regs);
static u32 nv_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void nv_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static void nv_host_stop (struct ata_host_set *host_set);
static void nv_enable_hotplug(struct ata_probe_ent *probe_ent);
static void nv_disable_hotplug(struct ata_host_set *host_set);
static int nv_check_hotplug(struct ata_host_set *host_set);
static void nv_enable_hotplug_ck804(struct ata_probe_ent *probe_ent);
static void nv_disable_hotplug_ck804(struct ata_host_set *host_set);
static int nv_check_hotplug_ck804(struct ata_host_set *host_set);

enum nv_host_type
{
	GENERIC,
	NFORCE2,
	NFORCE3,
	CK804
};

static const struct pci_device_id nv_pci_tbl[] = {
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, NFORCE2 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, NFORCE3 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, NFORCE3 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_SATA,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, CK804 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, CK804 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_SATA,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, CK804 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, CK804 },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_SATA,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_SATA,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_IDE<<8, 0xffff00, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_RAID<<8, 0xffff00, GENERIC },
	{ 0, } /* terminate list */
};

struct nv_host_desc
{
	enum nv_host_type	host_type;
	void			(*enable_hotplug)(struct ata_probe_ent *probe_ent);
	void			(*disable_hotplug)(struct ata_host_set *host_set);
	int			(*check_hotplug)(struct ata_host_set *host_set);

};
static struct nv_host_desc nv_device_tbl[] = {
	{
		.host_type	= GENERIC,
		.enable_hotplug	= NULL,
		.disable_hotplug= NULL,
		.check_hotplug	= NULL,
	},
	{
		.host_type	= NFORCE2,
		.enable_hotplug	= nv_enable_hotplug,
		.disable_hotplug= nv_disable_hotplug,
		.check_hotplug	= nv_check_hotplug,
	},
	{
		.host_type	= NFORCE3,
		.enable_hotplug	= nv_enable_hotplug,
		.disable_hotplug= nv_disable_hotplug,
		.check_hotplug	= nv_check_hotplug,
	},
	{	.host_type	= CK804,
		.enable_hotplug	= nv_enable_hotplug_ck804,
		.disable_hotplug= nv_disable_hotplug_ck804,
		.check_hotplug	= nv_check_hotplug_ck804,
	},
};

struct nv_host
{
	struct nv_host_desc	*host_desc;
	unsigned long		host_flags;
};

static struct pci_driver nv_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= nv_pci_tbl,
	.probe			= nv_init_one,
	.remove			= ata_pci_remove_one,
};

static struct scsi_host_template nv_sht = {
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
};

static const struct ata_port_operations nv_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.phy_reset		= sata_phy_reset,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.eng_timeout		= ata_eng_timeout,
	.irq_handler		= nv_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= nv_host_stop,
};

/* FIXME: The hardware provides the necessary SATA PHY controls
 * to support ATA_FLAG_SATA_RESET.  However, it is currently
 * necessary to disable that flag, to solve misdetection problems.
 * See http://bugme.osdl.org/show_bug.cgi?id=3352 for more info.
 *
 * This problem really needs to be investigated further.  But in the
 * meantime, we avoid ATA_FLAG_SATA_RESET to get people working.
 */
static struct ata_port_info nv_port_info = {
	.sht		= &nv_sht,
	.host_flags	= ATA_FLAG_SATA |
			  /* ATA_FLAG_SATA_RESET | */
			  ATA_FLAG_SRST |
			  ATA_FLAG_NO_LEGACY,
	.pio_mask	= NV_PIO_MASK,
	.mwdma_mask	= NV_MWDMA_MASK,
	.udma_mask	= NV_UDMA_MASK,
	.port_ops	= &nv_ops,
};

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("low-level driver for NVIDIA nForce SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, nv_pci_tbl);
MODULE_VERSION(DRV_VERSION);

static irqreturn_t nv_interrupt (int irq, void *dev_instance,
				 struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct nv_host *host = host_set->private_data;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;

	spin_lock_irqsave(&host_set->lock, flags);

	for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap;

		ap = host_set->ports[i];
		if (ap &&
		    !(ap->flags & (ATA_FLAG_PORT_DISABLED | ATA_FLAG_NOINTR))) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.ctl & ATA_NIEN)))
				handled += ata_host_intr(ap, qc);
			else
				// No request pending?  Clear interrupt status
				// anyway, in case there's one pending.
				ap->ops->check_status(ap);
		}

	}

	if (host->host_desc->check_hotplug)
		handled += host->host_desc->check_hotplug(host_set);

	spin_unlock_irqrestore(&host_set->lock, flags);

	return IRQ_RETVAL(handled);
}

static u32 nv_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;

	return ioread32((void __iomem *)ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void nv_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;

	iowrite32(val, (void __iomem *)ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void nv_host_stop (struct ata_host_set *host_set)
{
	struct nv_host *host = host_set->private_data;

	// Disable hotplug event interrupts.
	if (host->host_desc->disable_hotplug)
		host->host_desc->disable_hotplug(host_set);

	kfree(host);

	ata_pci_host_stop(host_set);
}

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version = 0;
	struct nv_host *host;
	struct ata_port_info *ppi;
	struct ata_probe_ent *probe_ent;
	int pci_dev_busy = 0;
	int rc;
	u32 bar;
	unsigned long base;

        // Make sure this is a SATA controller by counting the number of bars
        // (NVIDIA SATA controllers will always have six bars).  Otherwise,
        // it's an IDE controller and we ignore it.
	for (bar=0; bar<6; bar++)
		if (pci_resource_start(pdev, bar) == 0)
			return -ENODEV;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out_disable;
	}

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	rc = -ENOMEM;

	ppi = &nv_port_info;
	probe_ent = ata_pci_init_native_mode(pdev, &ppi, ATA_PORT_PRIMARY | ATA_PORT_SECONDARY);
	if (!probe_ent)
		goto err_out_regions;

	host = kmalloc(sizeof(struct nv_host), GFP_KERNEL);
	if (!host)
		goto err_out_free_ent;

	memset(host, 0, sizeof(struct nv_host));
	host->host_desc = &nv_device_tbl[ent->driver_data];

	probe_ent->private_data = host;

	probe_ent->mmio_base = pci_iomap(pdev, 5, 0);
	if (!probe_ent->mmio_base) {
		rc = -EIO;
		goto err_out_free_host;
	}

	base = (unsigned long)probe_ent->mmio_base;

	probe_ent->port[0].scr_addr = base + NV_PORT0_SCR_REG_OFFSET;
	probe_ent->port[1].scr_addr = base + NV_PORT1_SCR_REG_OFFSET;

	pci_set_master(pdev);

	rc = ata_device_add(probe_ent);
	if (rc != NV_PORTS)
		goto err_out_iounmap;

	// Enable hotplug event interrupts.
	if (host->host_desc->enable_hotplug)
		host->host_desc->enable_hotplug(probe_ent);

	kfree(probe_ent);

	return 0;

err_out_iounmap:
	pci_iounmap(pdev, probe_ent->mmio_base);
err_out_free_host:
	kfree(host);
err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out_disable:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
err_out:
	return rc;
}

static void nv_enable_hotplug(struct ata_probe_ent *probe_ent)
{
	u8 intr_mask;

	outb(NV_INT_STATUS_HOTPLUG,
		probe_ent->port[0].scr_addr + NV_INT_STATUS);

	intr_mask = inb(probe_ent->port[0].scr_addr + NV_INT_ENABLE);
	intr_mask |= NV_INT_ENABLE_HOTPLUG;

	outb(intr_mask, probe_ent->port[0].scr_addr + NV_INT_ENABLE);
}

static void nv_disable_hotplug(struct ata_host_set *host_set)
{
	u8 intr_mask;

	intr_mask = inb(host_set->ports[0]->ioaddr.scr_addr + NV_INT_ENABLE);

	intr_mask &= ~(NV_INT_ENABLE_HOTPLUG);

	outb(intr_mask, host_set->ports[0]->ioaddr.scr_addr + NV_INT_ENABLE);
}

static int nv_check_hotplug(struct ata_host_set *host_set)
{
	u8 intr_status;

	intr_status = inb(host_set->ports[0]->ioaddr.scr_addr + NV_INT_STATUS);

	// Clear interrupt status.
	outb(0xff, host_set->ports[0]->ioaddr.scr_addr + NV_INT_STATUS);

	if (intr_status & NV_INT_STATUS_HOTPLUG) {
		if (intr_status & NV_INT_STATUS_PDEV_ADDED)
			printk(KERN_WARNING "nv_sata: "
				"Primary device added\n");

		if (intr_status & NV_INT_STATUS_PDEV_REMOVED)
			printk(KERN_WARNING "nv_sata: "
				"Primary device removed\n");

		if (intr_status & NV_INT_STATUS_SDEV_ADDED)
			printk(KERN_WARNING "nv_sata: "
				"Secondary device added\n");

		if (intr_status & NV_INT_STATUS_SDEV_REMOVED)
			printk(KERN_WARNING "nv_sata: "
				"Secondary device removed\n");

		return 1;
	}

	return 0;
}

static void nv_enable_hotplug_ck804(struct ata_probe_ent *probe_ent)
{
	struct pci_dev *pdev = to_pci_dev(probe_ent->dev);
	u8 intr_mask;
	u8 regval;

	pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
	regval |= NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
	pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);

	writeb(NV_INT_STATUS_HOTPLUG, probe_ent->mmio_base + NV_INT_STATUS_CK804);

	intr_mask = readb(probe_ent->mmio_base + NV_INT_ENABLE_CK804);
	intr_mask |= NV_INT_ENABLE_HOTPLUG;

	writeb(intr_mask, probe_ent->mmio_base + NV_INT_ENABLE_CK804);
}

static void nv_disable_hotplug_ck804(struct ata_host_set *host_set)
{
	struct pci_dev *pdev = to_pci_dev(host_set->dev);
	u8 intr_mask;
	u8 regval;

	intr_mask = readb(host_set->mmio_base + NV_INT_ENABLE_CK804);

	intr_mask &= ~(NV_INT_ENABLE_HOTPLUG);

	writeb(intr_mask, host_set->mmio_base + NV_INT_ENABLE_CK804);

	pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
	regval &= ~NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
	pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);
}

static int nv_check_hotplug_ck804(struct ata_host_set *host_set)
{
	u8 intr_status;

	intr_status = readb(host_set->mmio_base + NV_INT_STATUS_CK804);

	// Clear interrupt status.
	writeb(0xff, host_set->mmio_base + NV_INT_STATUS_CK804);

	if (intr_status & NV_INT_STATUS_HOTPLUG) {
		if (intr_status & NV_INT_STATUS_PDEV_ADDED)
			printk(KERN_WARNING "nv_sata: "
				"Primary device added\n");

		if (intr_status & NV_INT_STATUS_PDEV_REMOVED)
			printk(KERN_WARNING "nv_sata: "
				"Primary device removed\n");

		if (intr_status & NV_INT_STATUS_SDEV_ADDED)
			printk(KERN_WARNING "nv_sata: "
				"Secondary device added\n");

		if (intr_status & NV_INT_STATUS_SDEV_REMOVED)
			printk(KERN_WARNING "nv_sata: "
				"Secondary device removed\n");

		return 1;
	}

	return 0;
}

static int __init nv_init(void)
{
	return pci_module_init(&nv_pci_driver);
}

static void __exit nv_exit(void)
{
	pci_unregister_driver(&nv_pci_driver);
}

module_init(nv_init);
module_exit(nv_exit);
