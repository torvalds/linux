/*
 * pata_ali.c 	- ALI 15x3 PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *
 * based in part upon
 * linux/drivers/ide/pci/alim15x3.c		Version 0.17	2003/01/02
 *
 *  Copyright (C) 1998-2000 Michel Aubry, Maintainer
 *  Copyright (C) 1998-2000 Andrzej Krzysztofowicz, Maintainer
 *  Copyright (C) 1999-2000 CJ, cjtsai@ali.com.tw, Maintainer
 *
 *  Copyright (C) 1998-2000 Andre Hedrick (andre@linux-ide.org)
 *  May be copied or modified under the terms of the GNU General Public License
 *  Copyright (C) 2002 Alan Cox <alan@redhat.com>
 *  ALi (now ULi M5228) support by Clear Zhang <Clear.Zhang@ali.com.tw>
 *
 *  Documentation
 *	Chipset documentation available under NDA only
 *
 *  TODO/CHECK
 *	Cannot have ATAPI on both master & slave for rev < c2 (???) but
 *	otherwise should do atapi DMA (For now for old we do PIO only for
 *	ATAPI)
 *	Review Sunblade workaround.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/dmi.h>

#define DRV_NAME "pata_ali"
#define DRV_VERSION "0.7.8"

static int ali_atapi_dma = 0;
module_param_named(atapi_dma, ali_atapi_dma, int, 0644);
MODULE_PARM_DESC(atapi_dma, "Enable ATAPI DMA (0=disable, 1=enable)");

static struct pci_dev *isa_bridge;

/*
 *	Cable special cases
 */

static const struct dmi_system_id cable_dmi_table[] = {
	{
		.ident = "HP Pavilion N5430",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_BOARD_VERSION, "OmniBook N32N-736"),
		},
	},
	{
		.ident = "Toshiba Satelite S1800-814",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1800-814"),
		},
	},
	{ }
};

static int ali_cable_override(struct pci_dev *pdev)
{
	/* Fujitsu P2000 */
	if (pdev->subsystem_vendor == 0x10CF && pdev->subsystem_device == 0x10AF)
	   	return 1;
	/* Mitac 8317 (Winbook-A) and relatives */
	if (pdev->subsystem_vendor == 0x1071 && pdev->subsystem_device == 0x8317)
		return 1;
	/* Systems by DMI */
	if (dmi_check_system(cable_dmi_table))
		return 1;
	return 0;
}

/**
 *	ali_c2_cable_detect	-	cable detection
 *	@ap: ATA port
 *
 *	Perform cable detection for C2 and later revisions
 */

static int ali_c2_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 ata66;

	/* Certain laptops use short but suitable cables and don't
	   implement the detect logic */

	if (ali_cable_override(pdev))
		return ATA_CBL_PATA40_SHORT;

	/* Host view cable detect 0x4A bit 0 primary bit 1 secondary
	   Bit set for 40 pin */
	pci_read_config_byte(pdev, 0x4A, &ata66);
	if (ata66 & (1 << ap->port_no))
		return ATA_CBL_PATA40;
	else
		return ATA_CBL_PATA80;
}

/**
 *	ali_20_filter		-	filter for earlier ALI DMA
 *	@ap: ALi ATA port
 *	@adev: attached device
 *
 *	Ensure that we do not do DMA on CD devices. We may be able to
 *	fix that later on. Also ensure we do not do UDMA on WDC drives
 */

static unsigned long ali_20_filter(struct ata_device *adev, unsigned long mask)
{
	char model_num[ATA_ID_PROD_LEN + 1];
	/* No DMA on anything but a disk for now */
	if (adev->class != ATA_DEV_ATA)
		mask &= ~(ATA_MASK_MWDMA | ATA_MASK_UDMA);
	ata_id_c_string(adev->id, model_num, ATA_ID_PROD, sizeof(model_num));
	if (strstr(model_num, "WDC"))
		return mask &= ~ATA_MASK_UDMA;
	return ata_bmdma_mode_filter(adev, mask);
}

/**
 *	ali_fifo_control	-	FIFO manager
 *	@ap: ALi channel to control
 *	@adev: device for FIFO control
 *	@on: 0 for off 1 for on
 *
 *	Enable or disable the FIFO on a given device. Because of the way the
 *	ALi FIFO works it provides a boost on ATA disk but can be confused by
 *	ATAPI and we must therefore manage it.
 */

static void ali_fifo_control(struct ata_port *ap, struct ata_device *adev, int on)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int pio_fifo = 0x54 + ap->port_no;
	u8 fifo;
	int shift = 4 * adev->devno;

	/* ATA - FIFO on set nibble to 0x05, ATAPI - FIFO off, set nibble to
	   0x00. Not all the docs agree but the behaviour we now use is the
	   one stated in the BIOS Programming Guide */

	pci_read_config_byte(pdev, pio_fifo, &fifo);
	fifo &= ~(0x0F << shift);
	fifo |= (on << shift);
	pci_write_config_byte(pdev, pio_fifo, fifo);
}

/**
 *	ali_program_modes	-	load mode registers
 *	@ap: ALi channel to load
 *	@adev: Device the timing is for
 *	@cmd: Command timing
 *	@data: Data timing
 *	@ultra: UDMA timing or zero for off
 *
 *	Loads the timing registers for cmd/data and disable UDMA if
 *	ultra is zero. If ultra is set then load and enable the UDMA
 *	timing but do not touch the command/data timing.
 */

static void ali_program_modes(struct ata_port *ap, struct ata_device *adev, struct ata_timing *t, u8 ultra)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int cas = 0x58 + 4 * ap->port_no;	/* Command timing */
	int cbt = 0x59 + 4 * ap->port_no;	/* Command timing */
	int drwt = 0x5A + 4 * ap->port_no + adev->devno; /* R/W timing */
	int udmat = 0x56 + ap->port_no;	/* UDMA timing */
	int shift = 4 * adev->devno;
	u8 udma;

	if (t != NULL) {
		t->setup = clamp_val(t->setup, 1, 8) & 7;
		t->act8b = clamp_val(t->act8b, 1, 8) & 7;
		t->rec8b = clamp_val(t->rec8b, 1, 16) & 15;
		t->active = clamp_val(t->active, 1, 8) & 7;
		t->recover = clamp_val(t->recover, 1, 16) & 15;

		pci_write_config_byte(pdev, cas, t->setup);
		pci_write_config_byte(pdev, cbt, (t->act8b << 4) | t->rec8b);
		pci_write_config_byte(pdev, drwt, (t->active << 4) | t->recover);
	}

	/* Set up the UDMA enable */
	pci_read_config_byte(pdev, udmat, &udma);
	udma &= ~(0x0F << shift);
	udma |= ultra << shift;
	pci_write_config_byte(pdev, udmat, udma);
}

/**
 *	ali_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the ALi registers for PIO mode. FIXME: add timings for
 *	PIO5.
 */

static void ali_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct ata_device *pair = ata_dev_pair(adev);
	struct ata_timing t;
	unsigned long T =  1000000000 / 33333;	/* PCI clock based */

	ata_timing_compute(adev, adev->pio_mode, &t, T, 1);
	if (pair) {
		struct ata_timing p;
		ata_timing_compute(pair, pair->pio_mode, &p, T, 1);
		ata_timing_merge(&p, &t, &t, ATA_TIMING_SETUP|ATA_TIMING_8BIT);
		if (pair->dma_mode) {
			ata_timing_compute(pair, pair->dma_mode, &p, T, 1);
			ata_timing_merge(&p, &t, &t, ATA_TIMING_SETUP|ATA_TIMING_8BIT);
		}
	}

	/* PIO FIFO is only permitted on ATA disk */
	if (adev->class != ATA_DEV_ATA)
		ali_fifo_control(ap, adev, 0x00);
	ali_program_modes(ap, adev, &t, 0);
	if (adev->class == ATA_DEV_ATA)
		ali_fifo_control(ap, adev, 0x05);

}

/**
 *	ali_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	FIXME: MWDMA timings
 */

static void ali_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static u8 udma_timing[7] = { 0xC, 0xB, 0xA, 0x9, 0x8, 0xF, 0xD };
	struct ata_device *pair = ata_dev_pair(adev);
	struct ata_timing t;
	unsigned long T =  1000000000 / 33333;	/* PCI clock based */
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);


	if (adev->class == ATA_DEV_ATA)
		ali_fifo_control(ap, adev, 0x08);

	if (adev->dma_mode >= XFER_UDMA_0) {
		ali_program_modes(ap, adev, NULL, udma_timing[adev->dma_mode - XFER_UDMA_0]);
		if (adev->dma_mode >= XFER_UDMA_3) {
			u8 reg4b;
			pci_read_config_byte(pdev, 0x4B, &reg4b);
			reg4b |= 1;
			pci_write_config_byte(pdev, 0x4B, reg4b);
		}
	} else {
		ata_timing_compute(adev, adev->dma_mode, &t, T, 1);
		if (pair) {
			struct ata_timing p;
			ata_timing_compute(pair, pair->pio_mode, &p, T, 1);
			ata_timing_merge(&p, &t, &t, ATA_TIMING_SETUP|ATA_TIMING_8BIT);
			if (pair->dma_mode) {
				ata_timing_compute(pair, pair->dma_mode, &p, T, 1);
				ata_timing_merge(&p, &t, &t, ATA_TIMING_SETUP|ATA_TIMING_8BIT);
			}
		}
		ali_program_modes(ap, adev, &t, 0);
	}
}

/**
 *	ali_warn_atapi_dma	-	Warn about ATAPI DMA disablement
 *	@adev: Device
 *
 *	Whine about ATAPI DMA disablement if @adev is an ATAPI device.
 *	Can be used as ->dev_config.
 */

static void ali_warn_atapi_dma(struct ata_device *adev)
{
	struct ata_eh_context *ehc = &adev->link->eh_context;
	int print_info = ehc->i.flags & ATA_EHI_PRINTINFO;

	if (print_info && adev->class == ATA_DEV_ATAPI && !ali_atapi_dma) {
		ata_dev_printk(adev, KERN_WARNING,
			       "WARNING: ATAPI DMA disabled for reliablity issues.  It can be enabled\n");
		ata_dev_printk(adev, KERN_WARNING,
			       "WARNING: via pata_ali.atapi_dma modparam or corresponding sysfs node.\n");
	}
}

/**
 *	ali_lock_sectors	-	Keep older devices to 255 sector mode
 *	@adev: Device
 *
 *	Called during the bus probe for each device that is found. We use
 *	this call to lock the sector count of the device to 255 or less on
 *	older ALi controllers. If we didn't do this then large I/O's would
 *	require LBA48 commands which the older ALi requires are issued by
 *	slower PIO methods
 */

static void ali_lock_sectors(struct ata_device *adev)
{
	adev->max_sectors = 255;
	ali_warn_atapi_dma(adev);
}

/**
 *	ali_check_atapi_dma	-	DMA check for most ALi controllers
 *	@adev: Device
 *
 *	Called to decide whether commands should be sent by DMA or PIO
 */

static int ali_check_atapi_dma(struct ata_queued_cmd *qc)
{
	if (!ali_atapi_dma) {
		/* FIXME: pata_ali can't do ATAPI DMA reliably but the
		 * IDE alim15x3 driver can.  I tried lots of things
		 * but couldn't find what the actual difference was.
		 * If you got an idea, please write it to
		 * linux-ide@vger.kernel.org and cc htejun@gmail.com.
		 *
		 * Disable ATAPI DMA for now.
		 */
		return -EOPNOTSUPP;
	}

	/* If its not a media command, its not worth it */
	if (atapi_cmd_type(qc->cdb[0]) == ATAPI_MISC)
		return -EOPNOTSUPP;
	return 0;
}

static void ali_c2_c3_postreset(struct ata_link *link, unsigned int *classes)
{
	u8 r;
	int port_bit = 4 << link->ap->port_no;

	/* If our bridge is an ALI 1533 then do the extra work */
	if (isa_bridge) {
		/* Tristate and re-enable the bus signals */
		pci_read_config_byte(isa_bridge, 0x58, &r);
		r &= ~port_bit;
		pci_write_config_byte(isa_bridge, 0x58, r);
		r |= port_bit;
		pci_write_config_byte(isa_bridge, 0x58, r);
	}
	ata_sff_postreset(link, classes);
}

static struct scsi_host_template ali_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

/*
 *	Port operations for PIO only ALi
 */

static struct ata_port_operations ali_early_port_ops = {
	.inherits	= &ata_sff_port_ops,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= ali_set_piomode,
	.sff_data_xfer  = ata_sff_data_xfer32,
};

static const struct ata_port_operations ali_dma_base_ops = {
	.inherits	= &ata_bmdma32_port_ops,
	.set_piomode	= ali_set_piomode,
	.set_dmamode	= ali_set_dmamode,
};

/*
 *	Port operations for DMA capable ALi without cable
 *	detect
 */
static struct ata_port_operations ali_20_port_ops = {
	.inherits	= &ali_dma_base_ops,
	.cable_detect	= ata_cable_40wire,
	.mode_filter	= ali_20_filter,
	.check_atapi_dma = ali_check_atapi_dma,
	.dev_config	= ali_lock_sectors,
};

/*
 *	Port operations for DMA capable ALi with cable detect
 */
static struct ata_port_operations ali_c2_port_ops = {
	.inherits	= &ali_dma_base_ops,
	.check_atapi_dma = ali_check_atapi_dma,
	.cable_detect	= ali_c2_cable_detect,
	.dev_config	= ali_lock_sectors,
	.postreset	= ali_c2_c3_postreset,
};

/*
 *	Port operations for DMA capable ALi with cable detect
 */
static struct ata_port_operations ali_c4_port_ops = {
	.inherits	= &ali_dma_base_ops,
	.check_atapi_dma = ali_check_atapi_dma,
	.cable_detect	= ali_c2_cable_detect,
	.dev_config	= ali_lock_sectors,
};

/*
 *	Port operations for DMA capable ALi with cable detect and LBA48
 */
static struct ata_port_operations ali_c5_port_ops = {
	.inherits	= &ali_dma_base_ops,
	.check_atapi_dma = ali_check_atapi_dma,
	.dev_config	= ali_warn_atapi_dma,
	.cable_detect	= ali_c2_cable_detect,
};


/**
 *	ali_init_chipset	-	chip setup function
 *	@pdev: PCI device of ATA controller
 *
 *	Perform the setup on the device that must be done both at boot
 *	and at resume time.
 */

static void ali_init_chipset(struct pci_dev *pdev)
{
	u8 tmp;
	struct pci_dev *north;

	/*
	 * The chipset revision selects the driver operations and
	 * mode data.
	 */

	if (pdev->revision <= 0x20) {
		pci_read_config_byte(pdev, 0x53, &tmp);
		tmp |= 0x03;
		pci_write_config_byte(pdev, 0x53, tmp);
	} else {
		pci_read_config_byte(pdev, 0x4a, &tmp);
		pci_write_config_byte(pdev, 0x4a, tmp | 0x20);
		pci_read_config_byte(pdev, 0x4B, &tmp);
		if (pdev->revision < 0xC2)
			/* 1543-E/F, 1543C-C, 1543C-D, 1543C-E */
			/* Clear CD-ROM DMA write bit */
			tmp &= 0x7F;
		/* Cable and UDMA */
		pci_write_config_byte(pdev, 0x4B, tmp | 0x09);
		/*
		 * CD_ROM DMA on (0x53 bit 0). Enable this even if we want
		 * to use PIO. 0x53 bit 1 (rev 20 only) - enable FIFO control
		 * via 0x54/55.
		 */
		pci_read_config_byte(pdev, 0x53, &tmp);
		if (pdev->revision >= 0xc7)
			tmp |= 0x03;
		else
			tmp |= 0x01;	/* CD_ROM enable for DMA */
		pci_write_config_byte(pdev, 0x53, tmp);
	}
	north = pci_get_bus_and_slot(0, PCI_DEVFN(0,0));
	if (north && north->vendor == PCI_VENDOR_ID_AL && isa_bridge) {
		/* Configure the ALi bridge logic. For non ALi rely on BIOS.
		   Set the south bridge enable bit */
		pci_read_config_byte(isa_bridge, 0x79, &tmp);
		if (pdev->revision == 0xC2)
			pci_write_config_byte(isa_bridge, 0x79, tmp | 0x04);
		else if (pdev->revision > 0xC2 && pdev->revision < 0xC5)
			pci_write_config_byte(isa_bridge, 0x79, tmp | 0x02);
	}
	pci_dev_put(north);
	ata_pci_bmdma_clear_simplex(pdev);
}
/**
 *	ali_init_one		-	discovery callback
 *	@pdev: PCI device ID
 *	@id: PCI table info
 *
 *	An ALi IDE interface has been discovered. Figure out what revision
 *	and perform configuration work before handing it to the ATA layer
 */

static int ali_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info_early = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.port_ops = &ali_early_port_ops
	};
	/* Revision 0x20 added DMA */
	static const struct ata_port_info info_20 = {
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.port_ops = &ali_20_port_ops
	};
	/* Revision 0x20 with support logic added UDMA */
	static const struct ata_port_info info_20_udma = {
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x07,	/* UDMA33 */
		.port_ops = &ali_20_port_ops
	};
	/* Revision 0xC2 adds UDMA66 */
	static const struct ata_port_info info_c2 = {
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA4,
		.port_ops = &ali_c2_port_ops
	};
	/* Revision 0xC3 is UDMA66 for now */
	static const struct ata_port_info info_c3 = {
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA4,
		.port_ops = &ali_c2_port_ops
	};
	/* Revision 0xC4 is UDMA100 */
	static const struct ata_port_info info_c4 = {
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA5,
		.port_ops = &ali_c4_port_ops
	};
	/* Revision 0xC5 is UDMA133 with LBA48 DMA */
	static const struct ata_port_info info_c5 = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA6,
		.port_ops = &ali_c5_port_ops
	};

	const struct ata_port_info *ppi[] = { NULL, NULL };
	u8 tmp;
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/*
	 * The chipset revision selects the driver operations and
	 * mode data.
	 */

	if (pdev->revision < 0x20) {
		ppi[0] = &info_early;
	} else if (pdev->revision < 0xC2) {
        	ppi[0] = &info_20;
	} else if (pdev->revision == 0xC2) {
        	ppi[0] = &info_c2;
	} else if (pdev->revision == 0xC3) {
        	ppi[0] = &info_c3;
	} else if (pdev->revision == 0xC4) {
        	ppi[0] = &info_c4;
	} else
        	ppi[0] = &info_c5;

	ali_init_chipset(pdev);

	if (isa_bridge && pdev->revision >= 0x20 && pdev->revision < 0xC2) {
		/* Are we paired with a UDMA capable chip */
		pci_read_config_byte(isa_bridge, 0x5E, &tmp);
		if ((tmp & 0x1E) == 0x12)
	        	ppi[0] = &info_20_udma;
	}

	return ata_pci_sff_init_one(pdev, ppi, &ali_sht, NULL);
}

#ifdef CONFIG_PM
static int ali_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;
	ali_init_chipset(pdev);
	ata_host_resume(host);
	return 0;
}
#endif

static const struct pci_device_id ali[] = {
	{ PCI_VDEVICE(AL, PCI_DEVICE_ID_AL_M5228), },
	{ PCI_VDEVICE(AL, PCI_DEVICE_ID_AL_M5229), },

	{ },
};

static struct pci_driver ali_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= ali,
	.probe 		= ali_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ali_reinit_one,
#endif
};

static int __init ali_init(void)
{
	int ret;
	isa_bridge = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL);

	ret = pci_register_driver(&ali_pci_driver);
	if (ret < 0)
		pci_dev_put(isa_bridge);
	return ret;
}


static void __exit ali_exit(void)
{
	pci_unregister_driver(&ali_pci_driver);
	pci_dev_put(isa_bridge);
}


MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for ALi PATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ali);
MODULE_VERSION(DRV_VERSION);

module_init(ali_init);
module_exit(ali_exit);
