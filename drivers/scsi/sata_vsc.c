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
#define DRV_VERSION	"1.0"

/* Interrupt register offsets (from chip base address) */
#define VSC_SATA_INT_STAT_OFFSET	0x00
#define VSC_SATA_INT_MASK_OFFSET	0x04

/* Taskfile registers offsets */
#define VSC_SATA_TF_CMD_OFFSET		0x00
#define VSC_SATA_TF_DATA_OFFSET		0x00
#define VSC_SATA_TF_ERROR_OFFSET	0x04
#define VSC_SATA_TF_FEATURE_OFFSET	0x06
#define VSC_SATA_TF_NSECT_OFFSET	0x08
#define VSC_SATA_TF_LBAL_OFFSET		0x0c
#define VSC_SATA_TF_LBAM_OFFSET		0x10
#define VSC_SATA_TF_LBAH_OFFSET		0x14
#define VSC_SATA_TF_DEVICE_OFFSET	0x18
#define VSC_SATA_TF_STATUS_OFFSET	0x1c
#define VSC_SATA_TF_COMMAND_OFFSET	0x1d
#define VSC_SATA_TF_ALTSTATUS_OFFSET	0x28
#define VSC_SATA_TF_CTL_OFFSET		0x29

/* DMA base */
#define VSC_SATA_UP_DESCRIPTOR_OFFSET	0x64
#define VSC_SATA_UP_DATA_BUFFER_OFFSET	0x6C
#define VSC_SATA_DMA_CMD_OFFSET		0x70

/* SCRs base */
#define VSC_SATA_SCR_STATUS_OFFSET	0x100
#define VSC_SATA_SCR_ERROR_OFFSET	0x104
#define VSC_SATA_SCR_CONTROL_OFFSET	0x108

/* Port stride */
#define VSC_SATA_PORT_OFFSET		0x200


static u32 vsc_sata_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;
	return readl((void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void vsc_sata_scr_write (struct ata_port *ap, unsigned int sc_reg,
			       u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;
	writel(val, (void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void vsc_intr_mask_update(struct ata_port *ap, u8 ctl)
{
	void __iomem *mask_addr;
	u8 mask;

	mask_addr = ap->host_set->mmio_base +
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
	 * However, if ATA_NIEN is changed, then we need to change the interrupt register.
	 */
	if ((tf->ctl & ATA_NIEN) != (ap->last_ctl & ATA_NIEN)) {
		ap->last_ctl = tf->ctl;
		vsc_intr_mask_update(ap, tf->ctl & ATA_NIEN);
	}
	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		writew(tf->feature | (((u16)tf->hob_feature) << 8), ioaddr->feature_addr);
		writew(tf->nsect | (((u16)tf->hob_nsect) << 8), ioaddr->nsect_addr);
		writew(tf->lbal | (((u16)tf->hob_lbal) << 8), ioaddr->lbal_addr);
		writew(tf->lbam | (((u16)tf->hob_lbam) << 8), ioaddr->lbam_addr);
		writew(tf->lbah | (((u16)tf->hob_lbah) << 8), ioaddr->lbah_addr);
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


/*
 * vsc_sata_interrupt
 *
 * Read the interrupt register and process for the devices that have them pending.
 */
static irqreturn_t vsc_sata_interrupt (int irq, void *dev_instance,
				       struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	u32 int_status;

	spin_lock(&host_set->lock);

	int_status = readl(host_set->mmio_base + VSC_SATA_INT_STAT_OFFSET);

	for (i = 0; i < host_set->n_ports; i++) {
		if (int_status & ((u32) 0xFF << (8 * i))) {
			struct ata_port *ap;

			ap = host_set->ports[i];
			if (ap &&
			    !(ap->flags & ATA_FLAG_PORT_DISABLED)) {
				struct ata_queued_cmd *qc;

				qc = ata_qc_from_tag(ap, ap->active_tag);
				if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)))
					handled += ata_host_intr(ap, qc);
			}
		}
	}

	spin_unlock(&host_set->lock);

	return IRQ_RETVAL(handled);
}


static struct scsi_host_template vsc_sata_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
	.ordered_flush		= 1,
};


static const struct ata_port_operations vsc_sata_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= vsc_sata_tf_load,
	.tf_read		= vsc_sata_tf_read,
	.exec_command		= ata_exec_command,
	.check_status		= ata_check_status,
	.dev_select		= ata_std_dev_select,
	.phy_reset		= sata_phy_reset,
	.bmdma_setup            = ata_bmdma_setup,
	.bmdma_start            = ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.eng_timeout		= ata_eng_timeout,
	.irq_handler		= vsc_sata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= vsc_sata_scr_read,
	.scr_write		= vsc_sata_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= ata_pci_host_stop,
};

static void __devinit vsc_sata_setup_port(struct ata_ioports *port, unsigned long base)
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


static int __devinit vsc_sata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	unsigned long base;
	int pci_dev_busy = 0;
	void __iomem *mmio_base;
	int rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	/*
	 * Check if we have needed resource mapped.
	 */
	if (pci_resource_len(pdev, 0) == 0) {
		rc = -ENODEV;
		goto err_out;
	}

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	/*
	 * Use 32 bit DMA mask, because 64 bit address support is poor.
	 */
	rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc)
		goto err_out_regions;

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}
	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	mmio_base = pci_iomap(pdev, 0, 0);
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}
	base = (unsigned long) mmio_base;

	/*
	 * Due to a bug in the chip, the default cache line size can't be used
	 */
	pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, 0x80);

	probe_ent->sht = &vsc_sata_sht;
	probe_ent->host_flags = ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				ATA_FLAG_MMIO | ATA_FLAG_SATA_RESET;
	probe_ent->port_ops = &vsc_sata_ops;
	probe_ent->n_ports = 4;
	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;

	/* We don't care much about the PIO/UDMA masks, but the core won't like us
	 * if we don't fill these
	 */
	probe_ent->pio_mask = 0x1f;
	probe_ent->mwdma_mask = 0x07;
	probe_ent->udma_mask = 0x7f;

	/* We have 4 ports per PCI function */
	vsc_sata_setup_port(&probe_ent->port[0], base + 1 * VSC_SATA_PORT_OFFSET);
	vsc_sata_setup_port(&probe_ent->port[1], base + 2 * VSC_SATA_PORT_OFFSET);
	vsc_sata_setup_port(&probe_ent->port[2], base + 3 * VSC_SATA_PORT_OFFSET);
	vsc_sata_setup_port(&probe_ent->port[3], base + 4 * VSC_SATA_PORT_OFFSET);

	pci_set_master(pdev);

	/*
	 * Config offset 0x98 is "Extended Control and Status Register 0"
	 * Default value is (1 << 28).  All bits except bit 28 are reserved in
	 * DPA mode.  If bit 28 is set, LED 0 reflects all ports' activity.
	 * If bit 28 is clear, each port has its own LED.
	 */
	pci_write_config_dword(pdev, 0x98, 0);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);
	kfree(probe_ent);

	return 0;

err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
	return rc;
}


/*
 * 0x1725/0x7174 is the Vitesse VSC-7174
 * 0x8086/0x3200 is the Intel 31244, which is supposed to be identical
 * compatibility is untested as of yet
 */
static struct pci_device_id vsc_sata_pci_tbl[] = {
	{ 0x1725, 0x7174, PCI_ANY_ID, PCI_ANY_ID, 0x10600, 0xFFFFFF, 0 },
	{ 0x8086, 0x3200, PCI_ANY_ID, PCI_ANY_ID, 0x10600, 0xFFFFFF, 0 },
	{ }
};


static struct pci_driver vsc_sata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= vsc_sata_pci_tbl,
	.probe			= vsc_sata_init_one,
	.remove			= ata_pci_remove_one,
};


static int __init vsc_sata_init(void)
{
	return pci_module_init(&vsc_sata_pci_driver);
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
