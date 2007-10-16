/*
 * pata_cs5536.c	- CS5536 PATA for new ATA layer
 *			  (C) 2007 Martin K. Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307	 USA
 *
 * Documentation:
 *	Available from AMD web site.
 *
 * The IDE timing registers for the CS5536 live in the Geode Machine
 * Specific Register file and not PCI config space.  Most BIOSes
 * virtualize the PCI registers so the chip looks like a standard IDE
 * controller.	Unfortunately not all implementations get this right.
 * In particular some have problems with unaligned accesses to the
 * virtualized PCI registers.  This driver always does full dword
 * writes to work around the issue.  Also, in case of a bad BIOS this
 * driver can be loaded with the "msr=1" parameter which forces using
 * the Machine Specific Registers to configure the device.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/libata.h>
#include <scsi/scsi_host.h>
#include <asm/msr.h>

#define DRV_NAME	"pata_cs5536"
#define DRV_VERSION	"0.0.6"

enum {
	CFG			= 0,
	DTC			= 1,
	CAST			= 2,
	ETC			= 3,

	MSR_IDE_BASE		= 0x51300000,
	MSR_IDE_CFG		= (MSR_IDE_BASE + 0x10),
	MSR_IDE_DTC		= (MSR_IDE_BASE + 0x12),
	MSR_IDE_CAST		= (MSR_IDE_BASE + 0x13),
	MSR_IDE_ETC		= (MSR_IDE_BASE + 0x14),

	PCI_IDE_CFG		= 0x40,
	PCI_IDE_DTC		= 0x48,
	PCI_IDE_CAST		= 0x4c,
	PCI_IDE_ETC		= 0x50,

	IDE_CFG_CHANEN		= 0x2,
	IDE_CFG_CABLE		= 0x10000,

	IDE_D0_SHIFT		= 24,
	IDE_D1_SHIFT		= 16,
	IDE_DRV_MASK		= 0xff,

	IDE_CAST_D0_SHIFT	= 6,
	IDE_CAST_D1_SHIFT	= 4,
	IDE_CAST_DRV_MASK	= 0x3,
	IDE_CAST_CMD_MASK	= 0xff,
	IDE_CAST_CMD_SHIFT	= 24,

	IDE_ETC_NODMA		= 0x03,
};

static int use_msr;

static const u32 msr_reg[4] = {
	MSR_IDE_CFG, MSR_IDE_DTC, MSR_IDE_CAST, MSR_IDE_ETC,
};

static const u8 pci_reg[4] = {
	PCI_IDE_CFG, PCI_IDE_DTC, PCI_IDE_CAST, PCI_IDE_ETC,
};

static inline int cs5536_read(struct pci_dev *pdev, int reg, int *val)
{
	if (unlikely(use_msr)) {
		u32 dummy;

		rdmsr(msr_reg[reg], *val, dummy);
		return 0;
	}

	return pci_read_config_dword(pdev, pci_reg[reg], val);
}

static inline int cs5536_write(struct pci_dev *pdev, int reg, int val)
{
	if (unlikely(use_msr)) {
		wrmsr(msr_reg[reg], val, 0);
		return 0;
	}

	return pci_write_config_dword(pdev, pci_reg[reg], val);
}

/**
 *	cs5536_cable_detect	-	detect cable type
 *	@ap: Port to detect on
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform cable detection for ATA66 capable cable. Return a libata
 *	cable type.
 */

static int cs5536_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 cfg;

	cs5536_read(pdev, CFG, &cfg);

	if (cfg & (IDE_CFG_CABLE << ap->port_no))
		return ATA_CBL_PATA80;
	else
		return ATA_CBL_PATA40;
}

/**
 *	cs5536_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 */

static void cs5536_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	static const u8 drv_timings[5] = {
		0x98, 0x55, 0x32, 0x21, 0x20,
	};

	static const u8 addr_timings[5] = {
		0x2, 0x1, 0x0, 0x0, 0x0,
	};

	static const u8 cmd_timings[5] = {
		0x99, 0x92, 0x90, 0x22, 0x20,
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct ata_device *pair = ata_dev_pair(adev);
	int mode = adev->pio_mode - XFER_PIO_0;
	int cmdmode = mode;
	int dshift = ap->port_no ? IDE_D1_SHIFT : IDE_D0_SHIFT;
	int cshift = ap->port_no ? IDE_CAST_D1_SHIFT : IDE_CAST_D0_SHIFT;
	u32 dtc, cast, etc;

	if (pair)
		cmdmode = min(mode, pair->pio_mode - XFER_PIO_0);

	cs5536_read(pdev, DTC, &dtc);
	cs5536_read(pdev, CAST, &cast);
	cs5536_read(pdev, ETC, &etc);

	dtc &= ~(IDE_DRV_MASK << dshift);
	dtc |= drv_timings[mode] << dshift;

	cast &= ~(IDE_CAST_DRV_MASK << cshift);
	cast |= addr_timings[mode] << cshift;

	cast &= ~(IDE_CAST_CMD_MASK << IDE_CAST_CMD_SHIFT);
	cast |= cmd_timings[cmdmode] << IDE_CAST_CMD_SHIFT;

	etc &= ~(IDE_DRV_MASK << dshift);
	etc |= IDE_ETC_NODMA << dshift;

	cs5536_write(pdev, DTC, dtc);
	cs5536_write(pdev, CAST, cast);
	cs5536_write(pdev, ETC, etc);
}

/**
 *	cs5536_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 */

static void cs5536_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static const u8 udma_timings[6] = {
		0xc2, 0xc1, 0xc0, 0xc4, 0xc5, 0xc6,
	};

	static const u8 mwdma_timings[3] = {
		0x67, 0x21, 0x20,
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 dtc, etc;
	int mode = adev->dma_mode;
	int dshift = ap->port_no ? IDE_D1_SHIFT : IDE_D0_SHIFT;

	if (mode >= XFER_UDMA_0) {
		cs5536_read(pdev, ETC, &etc);

		etc &= ~(IDE_DRV_MASK << dshift);
		etc |= udma_timings[mode - XFER_UDMA_0] << dshift;

		cs5536_write(pdev, ETC, etc);
	} else { /* MWDMA */
		cs5536_read(pdev, DTC, &dtc);

		dtc &= ~(IDE_DRV_MASK << dshift);
		dtc |= mwdma_timings[mode - XFER_MW_DMA_0] << dshift;

		cs5536_write(pdev, DTC, dtc);
	}
}

static struct scsi_host_template cs5536_sht = {
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

static struct ata_port_operations cs5536_port_ops = {
	.set_piomode		= cs5536_set_piomode,
	.set_dmamode		= cs5536_set_dmamode,
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
	.cable_detect		= cs5536_cable_detect,

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

	.port_start		= ata_port_start,
};

/**
 *	cs5536_init_one
 *	@dev: PCI device
 *	@id: Entry in match table
 *
 */

static int cs5536_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.sht = &cs5536_sht,
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA5,
		.port_ops = &cs5536_port_ops,
	};

	const struct ata_port_info *ppi[] = { &info, &ata_dummy_port_info };
	u32 cfg;

	if (use_msr)
		printk(KERN_ERR DRV_NAME ": Using MSR regs instead of PCI\n");

	cs5536_read(dev, CFG, &cfg);

	if ((cfg & IDE_CFG_CHANEN) == 0) {
		printk(KERN_ERR DRV_NAME ": disabled by BIOS\n");
		return -ENODEV;
	}

	return ata_pci_init_one(dev, ppi);
}

static const struct pci_device_id cs5536[] = {
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_CS5536_IDE), },
	{ },
};

static struct pci_driver cs5536_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= cs5536,
	.probe		= cs5536_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

static int __init cs5536_init(void)
{
	return pci_register_driver(&cs5536_pci_driver);
}

static void __exit cs5536_exit(void)
{
	pci_unregister_driver(&cs5536_pci_driver);
}

MODULE_AUTHOR("Martin K. Petersen");
MODULE_DESCRIPTION("low-level driver for the CS5536 IDE controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, cs5536);
MODULE_VERSION(DRV_VERSION);
module_param_named(msr, use_msr, int, 0644);
MODULE_PARM_DESC(msr, "Force using MSR to configure IDE function (Default: 0)");

module_init(cs5536_init);
module_exit(cs5536_exit);
