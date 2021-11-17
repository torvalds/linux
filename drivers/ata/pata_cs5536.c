// SPDX-License-Identifier: GPL-2.0-only
/*
 * pata_cs5536.c	- CS5536 PATA for new ATA layer
 *			  (C) 2007 Martin K. Petersen <mkp@mkp.net>
 *			  (C) 2011 Bartlomiej Zolnierkiewicz
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
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/libata.h>
#include <scsi/scsi_host.h>
#include <linux/dmi.h>

#ifdef CONFIG_X86_32
#include <asm/msr.h>
static int use_msr;
module_param_named(msr, use_msr, int, 0644);
MODULE_PARM_DESC(msr, "Force using MSR to configure IDE function (Default: 0)");
#else
#undef rdmsr	/* avoid accidental MSR usage on, e.g. x86-64 */
#undef wrmsr
#define rdmsr(x, y, z) do { } while (0)
#define wrmsr(x, y, z) do { } while (0)
#define use_msr 0
#endif

#define DRV_NAME	"pata_cs5536"
#define DRV_VERSION	"0.0.8"

enum {
	MSR_IDE_CFG		= 0x51300010,
	PCI_IDE_CFG		= 0x40,

	CFG			= 0,
	DTC			= 2,
	CAST			= 3,
	ETC			= 4,

	IDE_CFG_CHANEN		= (1 << 1),
	IDE_CFG_CABLE		= (1 << 17) | (1 << 16),

	IDE_D0_SHIFT		= 24,
	IDE_D1_SHIFT		= 16,
	IDE_DRV_MASK		= 0xff,

	IDE_CAST_D0_SHIFT	= 6,
	IDE_CAST_D1_SHIFT	= 4,
	IDE_CAST_DRV_MASK	= 0x3,
	IDE_CAST_CMD_MASK	= 0xff,
	IDE_CAST_CMD_SHIFT	= 24,

	IDE_ETC_UDMA_MASK	= 0xc0,
};

/* Some Bachmann OT200 devices have a non working UDMA support due a
 * missing resistor.
 */
static const struct dmi_system_id udma_quirk_dmi_table[] = {
	{
		.ident = "Bachmann electronic OT200",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Bachmann electronic"),
			DMI_MATCH(DMI_PRODUCT_NAME, "OT200"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "1")
		},
	},
	{ }
};

static int cs5536_read(struct pci_dev *pdev, int reg, u32 *val)
{
	if (unlikely(use_msr)) {
		u32 dummy __maybe_unused;

		rdmsr(MSR_IDE_CFG + reg, *val, dummy);
		return 0;
	}

	return pci_read_config_dword(pdev, PCI_IDE_CFG + reg * 4, val);
}

static int cs5536_write(struct pci_dev *pdev, int reg, int val)
{
	if (unlikely(use_msr)) {
		wrmsr(MSR_IDE_CFG + reg, val, 0);
		return 0;
	}

	return pci_write_config_dword(pdev, PCI_IDE_CFG + reg * 4, val);
}

static void cs5536_program_dtc(struct ata_device *adev, u8 tim)
{
	struct pci_dev *pdev = to_pci_dev(adev->link->ap->host->dev);
	int dshift = adev->devno ? IDE_D1_SHIFT : IDE_D0_SHIFT;
	u32 dtc;

	cs5536_read(pdev, DTC, &dtc);
	dtc &= ~(IDE_DRV_MASK << dshift);
	dtc |= tim << dshift;
	cs5536_write(pdev, DTC, dtc);
}

/**
 *	cs5536_cable_detect	-	detect cable type
 *	@ap: Port to detect on
 *
 *	Perform cable detection for ATA66 capable cable.
 *
 *	Returns a cable type.
 */

static int cs5536_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 cfg;

	cs5536_read(pdev, CFG, &cfg);

	if (cfg & IDE_CFG_CABLE)
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
	int cshift = adev->devno ? IDE_CAST_D1_SHIFT : IDE_CAST_D0_SHIFT;
	u32 cast;

	if (pair)
		cmdmode = min(mode, pair->pio_mode - XFER_PIO_0);

	cs5536_program_dtc(adev, drv_timings[mode]);

	cs5536_read(pdev, CAST, &cast);

	cast &= ~(IDE_CAST_DRV_MASK << cshift);
	cast |= addr_timings[mode] << cshift;

	cast &= ~(IDE_CAST_CMD_MASK << IDE_CAST_CMD_SHIFT);
	cast |= cmd_timings[cmdmode] << IDE_CAST_CMD_SHIFT;

	cs5536_write(pdev, CAST, cast);
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
	u32 etc;
	int mode = adev->dma_mode;
	int dshift = adev->devno ? IDE_D1_SHIFT : IDE_D0_SHIFT;

	cs5536_read(pdev, ETC, &etc);

	if (mode >= XFER_UDMA_0) {
		etc &= ~(IDE_DRV_MASK << dshift);
		etc |= udma_timings[mode - XFER_UDMA_0] << dshift;
	} else { /* MWDMA */
		etc &= ~(IDE_ETC_UDMA_MASK << dshift);
		cs5536_program_dtc(adev, mwdma_timings[mode - XFER_MW_DMA_0]);
	}

	cs5536_write(pdev, ETC, etc);
}

static struct scsi_host_template cs5536_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations cs5536_port_ops = {
	.inherits		= &ata_bmdma32_port_ops,
	.cable_detect		= cs5536_cable_detect,
	.set_piomode		= cs5536_set_piomode,
	.set_dmamode		= cs5536_set_dmamode,
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
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA5,
		.port_ops = &cs5536_port_ops,
	};

	static const struct ata_port_info no_udma_info = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.port_ops = &cs5536_port_ops,
	};


	const struct ata_port_info *ppi[2];
	u32 cfg;

	if (dmi_check_system(udma_quirk_dmi_table))
		ppi[0] = &no_udma_info;
	else
		ppi[0] = &info;

	ppi[1] = &ata_dummy_port_info;

	if (use_msr)
		printk(KERN_ERR DRV_NAME ": Using MSR regs instead of PCI\n");

	cs5536_read(dev, CFG, &cfg);

	if ((cfg & IDE_CFG_CHANEN) == 0) {
		printk(KERN_ERR DRV_NAME ": disabled by BIOS\n");
		return -ENODEV;
	}

	return ata_pci_bmdma_init_one(dev, ppi, &cs5536_sht, NULL, 0);
}

static const struct pci_device_id cs5536[] = {
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_CS5536_IDE), },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_CS5536_DEV_IDE), },
	{ },
};

static struct pci_driver cs5536_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= cs5536,
	.probe		= cs5536_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM_SLEEP
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

module_pci_driver(cs5536_pci_driver);

MODULE_AUTHOR("Martin K. Petersen");
MODULE_DESCRIPTION("low-level driver for the CS5536 IDE controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, cs5536);
MODULE_VERSION(DRV_VERSION);
