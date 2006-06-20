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
#define DRV_VERSION			"0.9"

enum {
	NV_PORTS			= 2,
	NV_PIO_MASK			= 0x1f,
	NV_MWDMA_MASK			= 0x07,
	NV_UDMA_MASK			= 0x7f,
	NV_PORT0_SCR_REG_OFFSET		= 0x00,
	NV_PORT1_SCR_REG_OFFSET		= 0x40,

	/* INT_STATUS/ENABLE */
	NV_INT_STATUS			= 0x10,
	NV_INT_ENABLE			= 0x11,
	NV_INT_STATUS_CK804		= 0x440,
	NV_INT_ENABLE_CK804		= 0x441,

	/* INT_STATUS/ENABLE bits */
	NV_INT_DEV			= 0x01,
	NV_INT_PM			= 0x02,
	NV_INT_ADDED			= 0x04,
	NV_INT_REMOVED			= 0x08,

	NV_INT_PORT_SHIFT		= 4,	/* each port occupies 4 bits */

	NV_INT_ALL			= 0x0f,
	NV_INT_MASK			= NV_INT_DEV |
					  NV_INT_ADDED | NV_INT_REMOVED,

	/* INT_CONFIG */
	NV_INT_CONFIG			= 0x12,
	NV_INT_CONFIG_METHD		= 0x01, // 0 = INT, 1 = SMI

	// For PCI config register 20
	NV_MCP_SATA_CFG_20		= 0x50,
	NV_MCP_SATA_CFG_20_SATA_SPACE_EN = 0x04,
};

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void nv_ck804_host_stop(struct ata_host_set *host_set);
static irqreturn_t nv_generic_interrupt(int irq, void *dev_instance,
					struct pt_regs *regs);
static irqreturn_t nv_nf2_interrupt(int irq, void *dev_instance,
				    struct pt_regs *regs);
static irqreturn_t nv_ck804_interrupt(int irq, void *dev_instance,
				      struct pt_regs *regs);
static u32 nv_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void nv_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);

static void nv_nf2_freeze(struct ata_port *ap);
static void nv_nf2_thaw(struct ata_port *ap);
static void nv_ck804_freeze(struct ata_port *ap);
static void nv_ck804_thaw(struct ata_port *ap);
static void nv_error_handler(struct ata_port *ap);

enum nv_host_type
{
	GENERIC,
	NFORCE2,
	NFORCE3 = NFORCE2,	/* NF2 == NF3 as far as sata_nv is concerned */
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
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_SATA,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_SATA3,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_IDE<<8, 0xffff00, GENERIC },
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_RAID<<8, 0xffff00, GENERIC },
	{ 0, } /* terminate list */
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

static const struct ata_port_operations nv_generic_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= nv_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.data_xfer		= ata_pio_data_xfer,
	.irq_handler		= nv_generic_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= ata_pci_host_stop,
};

static const struct ata_port_operations nv_nf2_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.freeze			= nv_nf2_freeze,
	.thaw			= nv_nf2_thaw,
	.error_handler		= nv_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.data_xfer		= ata_pio_data_xfer,
	.irq_handler		= nv_nf2_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= ata_pci_host_stop,
};

static const struct ata_port_operations nv_ck804_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.freeze			= nv_ck804_freeze,
	.thaw			= nv_ck804_thaw,
	.error_handler		= nv_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.data_xfer		= ata_pio_data_xfer,
	.irq_handler		= nv_ck804_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= nv_ck804_host_stop,
};

static struct ata_port_info nv_port_info[] = {
	/* generic */
	{
		.sht		= &nv_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_generic_ops,
	},
	/* nforce2/3 */
	{
		.sht		= &nv_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_nf2_ops,
	},
	/* ck804 */
	{
		.sht		= &nv_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY,
		.pio_mask	= NV_PIO_MASK,
		.mwdma_mask	= NV_MWDMA_MASK,
		.udma_mask	= NV_UDMA_MASK,
		.port_ops	= &nv_ck804_ops,
	},
};

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("low-level driver for NVIDIA nForce SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, nv_pci_tbl);
MODULE_VERSION(DRV_VERSION);

static irqreturn_t nv_generic_interrupt(int irq, void *dev_instance,
					struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;

	spin_lock_irqsave(&host_set->lock, flags);

	for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap;

		ap = host_set->ports[i];
		if (ap &&
		    !(ap->flags & ATA_FLAG_DISABLED)) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)))
				handled += ata_host_intr(ap, qc);
			else
				// No request pending?  Clear interrupt status
				// anyway, in case there's one pending.
				ap->ops->check_status(ap);
		}

	}

	spin_unlock_irqrestore(&host_set->lock, flags);

	return IRQ_RETVAL(handled);
}

static int nv_host_intr(struct ata_port *ap, u8 irq_stat)
{
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->active_tag);
	int handled;

	/* freeze if hotplugged */
	if (unlikely(irq_stat & (NV_INT_ADDED | NV_INT_REMOVED))) {
		ata_port_freeze(ap);
		return 1;
	}

	/* bail out if not our interrupt */
	if (!(irq_stat & NV_INT_DEV))
		return 0;

	/* DEV interrupt w/ no active qc? */
	if (unlikely(!qc || (qc->tf.flags & ATA_TFLAG_POLLING))) {
		ata_check_status(ap);
		return 1;
	}

	/* handle interrupt */
	handled = ata_host_intr(ap, qc);
	if (unlikely(!handled)) {
		/* spurious, clear it */
		ata_check_status(ap);
	}

	return 1;
}

static irqreturn_t nv_do_interrupt(struct ata_host_set *host_set, u8 irq_stat)
{
	int i, handled = 0;

	for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap = host_set->ports[i];

		if (ap && !(ap->flags & ATA_FLAG_DISABLED))
			handled += nv_host_intr(ap, irq_stat);

		irq_stat >>= NV_INT_PORT_SHIFT;
	}

	return IRQ_RETVAL(handled);
}

static irqreturn_t nv_nf2_interrupt(int irq, void *dev_instance,
				    struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	u8 irq_stat;
	irqreturn_t ret;

	spin_lock(&host_set->lock);
	irq_stat = inb(host_set->ports[0]->ioaddr.scr_addr + NV_INT_STATUS);
	ret = nv_do_interrupt(host_set, irq_stat);
	spin_unlock(&host_set->lock);

	return ret;
}

static irqreturn_t nv_ck804_interrupt(int irq, void *dev_instance,
				      struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	u8 irq_stat;
	irqreturn_t ret;

	spin_lock(&host_set->lock);
	irq_stat = readb(host_set->mmio_base + NV_INT_STATUS_CK804);
	ret = nv_do_interrupt(host_set, irq_stat);
	spin_unlock(&host_set->lock);

	return ret;
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

static void nv_nf2_freeze(struct ata_port *ap)
{
	unsigned long scr_addr = ap->host_set->ports[0]->ioaddr.scr_addr;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	mask = inb(scr_addr + NV_INT_ENABLE);
	mask &= ~(NV_INT_ALL << shift);
	outb(mask, scr_addr + NV_INT_ENABLE);
}

static void nv_nf2_thaw(struct ata_port *ap)
{
	unsigned long scr_addr = ap->host_set->ports[0]->ioaddr.scr_addr;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	outb(NV_INT_ALL << shift, scr_addr + NV_INT_STATUS);

	mask = inb(scr_addr + NV_INT_ENABLE);
	mask |= (NV_INT_MASK << shift);
	outb(mask, scr_addr + NV_INT_ENABLE);
}

static void nv_ck804_freeze(struct ata_port *ap)
{
	void __iomem *mmio_base = ap->host_set->mmio_base;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	mask = readb(mmio_base + NV_INT_ENABLE_CK804);
	mask &= ~(NV_INT_ALL << shift);
	writeb(mask, mmio_base + NV_INT_ENABLE_CK804);
}

static void nv_ck804_thaw(struct ata_port *ap)
{
	void __iomem *mmio_base = ap->host_set->mmio_base;
	int shift = ap->port_no * NV_INT_PORT_SHIFT;
	u8 mask;

	writeb(NV_INT_ALL << shift, mmio_base + NV_INT_STATUS_CK804);

	mask = readb(mmio_base + NV_INT_ENABLE_CK804);
	mask |= (NV_INT_MASK << shift);
	writeb(mask, mmio_base + NV_INT_ENABLE_CK804);
}

static int nv_hardreset(struct ata_port *ap, unsigned int *class)
{
	unsigned int dummy;

	/* SATA hardreset fails to retrieve proper device signature on
	 * some controllers.  Don't classify on hardreset.  For more
	 * info, see http://bugme.osdl.org/show_bug.cgi?id=3352
	 */
	return sata_std_hardreset(ap, &dummy);
}

static void nv_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, ata_std_prereset, ata_std_softreset,
			   nv_hardreset, ata_std_postreset);
}

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version = 0;
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

	ppi = &nv_port_info[ent->driver_data];
	probe_ent = ata_pci_init_native_mode(pdev, &ppi, ATA_PORT_PRIMARY | ATA_PORT_SECONDARY);
	if (!probe_ent)
		goto err_out_regions;

	probe_ent->mmio_base = pci_iomap(pdev, 5, 0);
	if (!probe_ent->mmio_base) {
		rc = -EIO;
		goto err_out_free_ent;
	}

	base = (unsigned long)probe_ent->mmio_base;

	probe_ent->port[0].scr_addr = base + NV_PORT0_SCR_REG_OFFSET;
	probe_ent->port[1].scr_addr = base + NV_PORT1_SCR_REG_OFFSET;

	/* enable SATA space for CK804 */
	if (ent->driver_data == CK804) {
		u8 regval;

		pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
		regval |= NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
		pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);
	}

	pci_set_master(pdev);

	rc = ata_device_add(probe_ent);
	if (rc != NV_PORTS)
		goto err_out_iounmap;

	kfree(probe_ent);

	return 0;

err_out_iounmap:
	pci_iounmap(pdev, probe_ent->mmio_base);
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

static void nv_ck804_host_stop(struct ata_host_set *host_set)
{
	struct pci_dev *pdev = to_pci_dev(host_set->dev);
	u8 regval;

	/* disable SATA space for CK804 */
	pci_read_config_byte(pdev, NV_MCP_SATA_CFG_20, &regval);
	regval &= ~NV_MCP_SATA_CFG_20_SATA_SPACE_EN;
	pci_write_config_byte(pdev, NV_MCP_SATA_CFG_20, regval);

	ata_pci_host_stop(host_set);
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
