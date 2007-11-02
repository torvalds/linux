/*
 *  sata_vsc.c - Vitesse VSC7174 4 port DPA SATA
 *
 *  Maintained by:  Jeremy Higdon @ SGI
 * 		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2004 SGI
 *
 *  Bits from Jeff Garzik, Copyright RedHat, Inc.
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
 *  Vitesse hardware documentation presumably available under NDA.
 *  Intel 31244 (same hardware interface) documentation presumably
 *  available from http://developer.intel.com/
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"sata_vsc"
#define DRV_VERSION	"2.3"

enum {
	VSC_MMIO_BAR			= 0,

	/* Interrupt register offsets (from chip base address) */
	VSC_SATA_INT_STAT_OFFSET	= 0x00,
	VSC_SATA_INT_MASK_OFFSET	= 0x04,

	/* Taskfile registers offsets */
	VSC_SATA_TF_CMD_OFFSET		= 0x00,
	VSC_SATA_TF_DATA_OFFSET		= 0x00,
	VSC_SATA_TF_ERROR_OFFSET	= 0x04,
	VSC_SATA_TF_FEATURE_OFFSET	= 0x06,
	VSC_SATA_TF_NSECT_OFFSET	= 0x08,
	VSC_SATA_TF_LBAL_OFFSET		= 0x0c,
	VSC_SATA_TF_LBAM_OFFSET		= 0x10,
	VSC_SATA_TF_LBAH_OFFSET		= 0x14,
	VSC_SATA_TF_DEVICE_OFFSET	= 0x18,
	VSC_SATA_TF_STATUS_OFFSET	= 0x1c,
	VSC_SATA_TF_COMMAND_OFFSET	= 0x1d,
	VSC_SATA_TF_ALTSTATUS_OFFSET	= 0x28,
	VSC_SATA_TF_CTL_OFFSET		= 0x29,

	/* DMA base */
	VSC_SATA_UP_DESCRIPTOR_OFFSET	= 0x64,
	VSC_SATA_UP_DATA_BUFFER_OFFSET	= 0x6C,
	VSC_SATA_DMA_CMD_OFFSET		= 0x70,

	/* SCRs base */
	VSC_SATA_SCR_STATUS_OFFSET	= 0x100,
	VSC_SATA_SCR_ERROR_OFFSET	= 0x104,
	VSC_SATA_SCR_CONTROL_OFFSET	= 0x108,

	/* Port stride */
	VSC_SATA_PORT_OFFSET		= 0x200,

	/* Error interrupt status bit offsets */
	VSC_SATA_INT_ERROR_CRC		= 0x40,
	VSC_SATA_INT_ERROR_T		= 0x20,
	VSC_SATA_INT_ERROR_P		= 0x10,
	VSC_SATA_INT_ERROR_R		= 0x8,
	VSC_SATA_INT_ERROR_E		= 0x4,
	VSC_SATA_INT_ERROR_M		= 0x2,
	VSC_SATA_INT_PHY_CHANGE		= 0x1,
	VSC_SATA_INT_ERROR = (VSC_SATA_INT_ERROR_CRC  | VSC_SATA_INT_ERROR_T | \
			      VSC_SATA_INT_ERROR_P    | VSC_SATA_INT_ERROR_R | \
			      VSC_SATA_INT_ERROR_E    | VSC_SATA_INT_ERROR_M | \
			      VSC_SATA_INT_PHY_CHANGE),
};

static int vsc_sata_scr_read(struct ata_port *ap, unsigned int sc_reg, u32 *val)
{
	if (sc_reg > SCR_CONTROL)
		return -EINVAL;
	*val = readl(ap->ioaddr.scr_addr + (sc_reg * 4));
	return 0;
}


static int vsc_sata_scr_write(struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return -EINVAL;
	writel(val, ap->ioaddr.scr_addr + (sc_reg * 4));
	return 0;
}


static void vsc_freeze(struct ata_port *ap)
{
	void __iomem *mask_addr;

	mask_addr = ap->host->iomap[VSC_MMIO_BAR] +
		VSC_SATA_INT_MASK_OFFSET + ap->port_no;

	writeb(0, mask_addr);
}


static void vsc_thaw(struct ata_port *ap)
{
	void __iomem *mask_addr;

	mask_addr = ap->host->iomap[VSC_MMIO_BAR] +
		VSC_SATA_INT_MASK_OFFSET + ap->port_no;

	writeb(0xff, mask_addr);
}


static void vsc_intr_mask_update(struct ata_port *ap, u8 ctl)
{
	void __iomem *mask_addr;
	u8 mask;

	mask_addr = ap->host->iomap[VSC_MMIO_BAR] +
		VSC_SATA_INT_MASK_OFFSET + ap->port_no;
	mask = readb(mask_addr);
	if (ctl & ATA_NIEN)
		mask |= 0x80;
	else
		mask &= 0x7F;
	writeb(mask, mask_addr);
}


static void vsc_sata_tf_load(struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	/*
	 * The only thing the ctl register is used for is SRST.
	 * That is not enabled or disabled via tf_load.
	 * However, if ATA_NIEN is changed, then we need to change
	 * the interrupt register.
	 */
	if ((tf->ctl & ATA_NIEN) != (ap->last_ctl & ATA_NIEN)) {
		ap->last_ctl = tf->ctl;
		vsc_intr_mask_update(ap, tf->ctl & ATA_NIEN);
	}
	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		writew(tf->feature | (((u16)tf->hob_feature) << 8),
		       ioaddr->feature_addr);
		writew(tf->nsect | (((u16)tf->hob_nsect) << 8),
		       ioaddr->nsect_addr);
		writew(tf->lbal | (((u16)tf->hob_lbal) << 8),
		       ioaddr->lbal_addr);
		writew(tf->lbam | (((u16)tf->hob_lbam) << 8),
		       ioaddr->lbam_addr);
		writew(tf->lbah | (((u16)tf->hob_lbah) << 8),
		       ioaddr->lbah_addr);
	} else if (is_addr) {
		writew(tf->feature, ioaddr->feature_addr);
		writew(tf->nsect, ioaddr->nsect_addr);
		writew(tf->lbal, ioaddr->lbal_addr);
		writew(tf->lbam, ioaddr->lbam_addr);
		writew(tf->lbah, ioaddr->lbah_addr);
	}

	if (tf->flags & ATA_TFLAG_DEVICE)
		writeb(tf->device, ioaddr->device_addr);

	ata_wait_idle(ap);
}


static void vsc_sata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u16 nsect, lbal, lbam, lbah, feature;

	tf->command = ata_check_status(ap);
	tf->device = readw(ioaddr->device_addr);
	feature = readw(ioaddr->error_addr);
	nsect = readw(ioaddr->nsect_addr);
	lbal = readw(ioaddr->lbal_addr);
	lbam = readw(ioaddr->lbam_addr);
	lbah = readw(ioaddr->lbah_addr);

	tf->feature = feature;
	tf->nsect = nsect;
	tf->lbal = lbal;
	tf->lbam = lbam;
	tf->lbah = lbah;

	if (tf->flags & ATA_TFLAG_LBA48) {
		tf->hob_feature = feature >> 8;
		tf->hob_nsect = nsect >> 8;
		tf->hob_lbal = lbal >> 8;
		tf->hob_lbam = lbam >> 8;
		tf->hob_lbah = lbah >> 8;
	}
}

static inline void vsc_error_intr(u8 port_status, struct ata_port *ap)
{
	if (port_status & (VSC_SATA_INT_PHY_CHANGE | VSC_SATA_INT_ERROR_M))
		ata_port_freeze(ap);
	else
		ata_port_abort(ap);
}

static void vsc_port_intr(u8 port_status, struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	int handled = 0;

	if (unlikely(port_status & VSC_SATA_INT_ERROR)) {
		vsc_error_intr(port_status, ap);
		return;
	}

	qc = ata_qc_from_tag(ap, ap->link.active_tag);
	if (qc && likely(!(qc->tf.flags & ATA_TFLAG_POLLING)))
		handled = ata_host_intr(ap, qc);

	/* We received an interrupt during a polled command,
	 * or some other spurious condition.  Interrupt reporting
	 * with this hardware is fairly reliable so it is safe to
	 * simply clear the interrupt
	 */
	if (unlikely(!handled))
		ata_chk_status(ap);
}

/*
 * vsc_sata_interrupt
 *
 * Read the interrupt register and process for the devices that have
 * them pending.
 */
static irqreturn_t vsc_sata_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	u32 status;

	status = readl(host->iomap[VSC_MMIO_BAR] + VSC_SATA_INT_STAT_OFFSET);

	if (unlikely(status == 0xffffffff || status == 0)) {
		if (status)
			dev_printk(KERN_ERR, host->dev,
				": IRQ status == 0xffffffff, "
				"PCI fault or device removal?\n");
		goto out;
	}

	spin_lock(&host->lock);

	for (i = 0; i < host->n_ports; i++) {
		u8 port_status = (status >> (8 * i)) & 0xff;
		if (port_status) {
			struct ata_port *ap = host->ports[i];

			if (ap && !(ap->flags & ATA_FLAG_DISABLED)) {
				vsc_port_intr(port_status, ap);
				handled++;
			} else
				dev_printk(KERN_ERR, host->dev,
					"interrupt from disabled port %d\n", i);
		}
	}

	spin_unlock(&host->lock);
out:
	return IRQ_RETVAL(handled);
}


static struct scsi_host_template vsc_sata_sht = {
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


static const struct ata_port_operations vsc_sata_ops = {
	.tf_load		= vsc_sata_tf_load,
	.tf_read		= vsc_sata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.bmdma_setup            = ata_bmdma_setup,
	.bmdma_start            = ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,
	.freeze			= vsc_freeze,
	.thaw			= vsc_thaw,
	.error_handler		= ata_bmdma_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.scr_read		= vsc_sata_scr_read,
	.scr_write		= vsc_sata_scr_write,
	.port_start		= ata_port_start,
};

static void __devinit vsc_sata_setup_port(struct ata_ioports *port,
					  void __iomem *base)
{
	port->cmd_addr		= base + VSC_SATA_TF_CMD_OFFSET;
	port->data_addr		= base + VSC_SATA_TF_DATA_OFFSET;
	port->error_addr	= base + VSC_SATA_TF_ERROR_OFFSET;
	port->feature_addr	= base + VSC_SATA_TF_FEATURE_OFFSET;
	port->nsect_addr	= base + VSC_SATA_TF_NSECT_OFFSET;
	port->lbal_addr		= base + VSC_SATA_TF_LBAL_OFFSET;
	port->lbam_addr		= base + VSC_SATA_TF_LBAM_OFFSET;
	port->lbah_addr		= base + VSC_SATA_TF_LBAH_OFFSET;
	port->device_addr	= base + VSC_SATA_TF_DEVICE_OFFSET;
	port->status_addr	= base + VSC_SATA_TF_STATUS_OFFSET;
	port->command_addr	= base + VSC_SATA_TF_COMMAND_OFFSET;
	port->altstatus_addr	= base + VSC_SATA_TF_ALTSTATUS_OFFSET;
	port->ctl_addr		= base + VSC_SATA_TF_CTL_OFFSET;
	port->bmdma_addr	= base + VSC_SATA_DMA_CMD_OFFSET;
	port->scr_addr		= base + VSC_SATA_SCR_STATUS_OFFSET;
	writel(0, base + VSC_SATA_UP_DESCRIPTOR_OFFSET);
	writel(0, base + VSC_SATA_UP_DATA_BUFFER_OFFSET);
}


static int __devinit vsc_sata_init_one(struct pci_dev *pdev,
				       const struct pci_device_id *ent)
{
	static const struct ata_port_info pi = {
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO,
		.pio_mask	= 0x1f,
		.mwdma_mask	= 0x07,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &vsc_sata_ops,
	};
	const struct ata_port_info *ppi[] = { &pi, NULL };
	static int printed_version;
	struct ata_host *host;
	void __iomem *mmio_base;
	int i, rc;
	u8 cls;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* allocate host */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 4);
	if (!host)
		return -ENOMEM;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* check if we have needed resource mapped */
	if (pci_resource_len(pdev, 0) == 0)
		return -ENODEV;

	/* map IO regions and intialize host accordingly */
	rc = pcim_iomap_regions(pdev, 1 << VSC_MMIO_BAR, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);

	mmio_base = host->iomap[VSC_MMIO_BAR];

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		unsigned int offset = (i + 1) * VSC_SATA_PORT_OFFSET;

		vsc_sata_setup_port(&ap->ioaddr, mmio_base + offset);

		ata_port_pbar_desc(ap, VSC_MMIO_BAR, -1, "mmio");
		ata_port_pbar_desc(ap, VSC_MMIO_BAR, offset, "port");
	}

	/*
	 * Use 32 bit DMA mask, because 64 bit address support is poor.
	 */
	rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc)
		return rc;

	/*
	 * Due to a bug in the chip, the default cache line size can't be
	 * used (unless the default is non-zero).
	 */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &cls);
	if (cls == 0x00)
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, 0x80);

	if (pci_enable_msi(pdev) == 0)
		pci_intx(pdev, 0);

	/*
	 * Config offset 0x98 is "Extended Control and Status Register 0"
	 * Default value is (1 << 28).  All bits except bit 28 are reserved in
	 * DPA mode.  If bit 28 is set, LED 0 reflects all ports' activity.
	 * If bit 28 is clear, each port has its own LED.
	 */
	pci_write_config_dword(pdev, 0x98, 0);

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, vsc_sata_interrupt,
				 IRQF_SHARED, &vsc_sata_sht);
}

static const struct pci_device_id vsc_sata_pci_tbl[] = {
	{ PCI_VENDOR_ID_VITESSE, 0x7174,
	  PCI_ANY_ID, PCI_ANY_ID, 0x10600, 0xFFFFFF, 0 },
	{ PCI_VENDOR_ID_INTEL, 0x3200,
	  PCI_ANY_ID, PCI_ANY_ID, 0x10600, 0xFFFFFF, 0 },

	{ }	/* terminate list */
};

static struct pci_driver vsc_sata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= vsc_sata_pci_tbl,
	.probe			= vsc_sata_init_one,
	.remove			= ata_pci_remove_one,
};

static int __init vsc_sata_init(void)
{
	return pci_register_driver(&vsc_sata_pci_driver);
}

static void __exit vsc_sata_exit(void)
{
	pci_unregister_driver(&vsc_sata_pci_driver);
}

MODULE_AUTHOR("Jeremy Higdon");
MODULE_DESCRIPTION("low-level driver for Vitesse VSC7174 SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, vsc_sata_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(vsc_sata_init);
module_exit(vsc_sata_exit);
