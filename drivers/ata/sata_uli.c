/*
 *  sata_uli.c - ULi Electronics SATA
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

#define DRV_NAME	"sata_uli"
#define DRV_VERSION	"1.1"

enum {
	uli_5289		= 0,
	uli_5287		= 1,
	uli_5281		= 2,

	uli_max_ports		= 4,

	/* PCI configuration registers */
	ULI5287_BASE		= 0x90, /* sata0 phy SCR registers */
	ULI5287_OFFS		= 0x10, /* offset from sata0->sata1 phy regs */
	ULI5281_BASE		= 0x60, /* sata0 phy SCR  registers */
	ULI5281_OFFS		= 0x60, /* offset from sata0->sata1 phy regs */
};

struct uli_priv {
	unsigned int		scr_cfg_addr[uli_max_ports];
};

static int uli_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static u32 uli_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void uli_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);

static const struct pci_device_id uli_pci_tbl[] = {
	{ PCI_VDEVICE(AL, 0x5289), uli_5289 },
	{ PCI_VDEVICE(AL, 0x5287), uli_5287 },
	{ PCI_VDEVICE(AL, 0x5281), uli_5281 },

	{ }	/* terminate list */
};

static struct pci_driver uli_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= uli_pci_tbl,
	.probe			= uli_init_one,
	.remove			= ata_pci_remove_one,
};

static struct scsi_host_template uli_sht = {
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

static const struct ata_port_operations uli_ops = {
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

	.scr_read		= uli_scr_read,
	.scr_write		= uli_scr_write,

	.port_start		= ata_port_start,
};

static const struct ata_port_info uli_port_info = {
	.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
			  ATA_FLAG_IGN_SIMPLEX,
	.pio_mask       = 0x1f,		/* pio0-4 */
	.udma_mask      = 0x7f,		/* udma0-6 */
	.port_ops       = &uli_ops,
};


MODULE_AUTHOR("Peer Chen");
MODULE_DESCRIPTION("low-level driver for ULi Electronics SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, uli_pci_tbl);
MODULE_VERSION(DRV_VERSION);

static unsigned int get_scr_cfg_addr(struct ata_port *ap, unsigned int sc_reg)
{
	struct uli_priv *hpriv = ap->host->private_data;
	return hpriv->scr_cfg_addr[ap->port_no] + (4 * sc_reg);
}

static u32 uli_scr_cfg_read (struct ata_port *ap, unsigned int sc_reg)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned int cfg_addr = get_scr_cfg_addr(ap, sc_reg);
	u32 val;

	pci_read_config_dword(pdev, cfg_addr, &val);
	return val;
}

static void uli_scr_cfg_write (struct ata_port *ap, unsigned int scr, u32 val)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	unsigned int cfg_addr = get_scr_cfg_addr(ap, scr);

	pci_write_config_dword(pdev, cfg_addr, val);
}

static u32 uli_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;

	return uli_scr_cfg_read(ap, sc_reg);
}

static void uli_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)	//SCR_CONTROL=2, SCR_ERROR=1, SCR_STATUS=0
		return;

	uli_scr_cfg_write(ap, sc_reg, val);
}

static int uli_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	const struct ata_port_info *ppi[] = { &uli_port_info, NULL };
	unsigned int board_idx = (unsigned int) ent->driver_data;
	struct ata_host *host;
	struct uli_priv *hpriv;
	void __iomem * const *iomap;
	struct ata_ioports *ioaddr;
	int n_ports, rc;

	if (!printed_version++)
		dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	n_ports = 2;
	if (board_idx == uli_5287)
		n_ports = 4;

	/* allocate the host */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	if (!host)
		return -ENOMEM;

	hpriv = devm_kzalloc(&pdev->dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;
	host->private_data = hpriv;

	/* the first two ports are standard SFF */
	rc = ata_pci_init_native_host(host);
	if (rc)
		return rc;

	rc = ata_pci_init_bmdma(host);
	if (rc)
		return rc;

	iomap = host->iomap;

	switch (board_idx) {
	case uli_5287:
		/* If there are four, the last two live right after
		 * the standard SFF ports.
		 */
		hpriv->scr_cfg_addr[0] = ULI5287_BASE;
		hpriv->scr_cfg_addr[1] = ULI5287_BASE + ULI5287_OFFS;

		ioaddr = &host->ports[2]->ioaddr;
		ioaddr->cmd_addr = iomap[0] + 8;
		ioaddr->altstatus_addr =
		ioaddr->ctl_addr = (void __iomem *)
			((unsigned long)iomap[1] | ATA_PCI_CTL_OFS) + 4;
		ioaddr->bmdma_addr = iomap[4] + 16;
		hpriv->scr_cfg_addr[2] = ULI5287_BASE + ULI5287_OFFS*4;
		ata_std_ports(ioaddr);

		ioaddr = &host->ports[3]->ioaddr;
		ioaddr->cmd_addr = iomap[2] + 8;
		ioaddr->altstatus_addr =
		ioaddr->ctl_addr = (void __iomem *)
			((unsigned long)iomap[3] | ATA_PCI_CTL_OFS) + 4;
		ioaddr->bmdma_addr = iomap[4] + 24;
		hpriv->scr_cfg_addr[3] = ULI5287_BASE + ULI5287_OFFS*5;
		ata_std_ports(ioaddr);
		break;

	case uli_5289:
		hpriv->scr_cfg_addr[0] = ULI5287_BASE;
		hpriv->scr_cfg_addr[1] = ULI5287_BASE + ULI5287_OFFS;
		break;

	case uli_5281:
		hpriv->scr_cfg_addr[0] = ULI5281_BASE;
		hpriv->scr_cfg_addr[1] = ULI5281_BASE + ULI5281_OFFS;
		break;

	default:
		BUG();
		break;
	}

	pci_set_master(pdev);
	pci_intx(pdev, 1);
	return ata_host_activate(host, pdev->irq, ata_interrupt, IRQF_SHARED,
				 &uli_sht);
}

static int __init uli_init(void)
{
	return pci_register_driver(&uli_pci_driver);
}

static void __exit uli_exit(void)
{
	pci_unregister_driver(&uli_pci_driver);
}


module_init(uli_init);
module_exit(uli_exit);
