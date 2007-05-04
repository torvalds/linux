/*
 * pata_ali.c 	- ALI 15x3 PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
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
 *	otherwise should do atapi DMA.
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
#define DRV_VERSION "0.7.4"

/*
 *	Cable special cases
 */

static struct dmi_system_id cable_dmi_table[] = {
	{
		.ident = "HP Pavilion N5430",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_BOARD_NAME, "OmniBook N32N-736"),
		},
	},
	{ }
};

static int ali_cable_override(struct pci_dev *pdev)
{
	/* Fujitsu P2000 */
	if (pdev->subsystem_vendor == 0x10CF && pdev->subsystem_device == 0x10AF)
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
	return ata_pci_default_filter(adev, mask);
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
	if (on)
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
		t->setup = FIT(t->setup, 1, 8) & 7;
		t->act8b = FIT(t->act8b, 1, 8) & 7;
		t->rec8b = FIT(t->rec8b, 1, 16) & 15;
		t->active = FIT(t->active, 1, 8) & 7;
		t->recover = FIT(t->recover, 1, 16) & 15;

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
}

static struct scsi_host_template ali_sht = {
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

/*
 *	Port operations for PIO only ALi
 */

static struct ata_port_operations ali_early_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= ali_set_piomode,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

/*
 *	Port operations for DMA capable ALi without cable
 *	detect
 */
static struct ata_port_operations ali_20_port_ops = {
	.port_disable	= ata_port_disable,

	.set_piomode	= ali_set_piomode,
	.set_dmamode	= ali_set_dmamode,
	.mode_filter	= ali_20_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,
	.dev_config	= ali_lock_sectors,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

/*
 *	Port operations for DMA capable ALi with cable detect
 */
static struct ata_port_operations ali_c2_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= ali_set_piomode,
	.set_dmamode	= ali_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,
	.dev_config	= ali_lock_sectors,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ali_c2_cable_detect,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

/*
 *	Port operations for DMA capable ALi with cable detect and LBA48
 */
static struct ata_port_operations ali_c5_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= ali_set_piomode,
	.set_dmamode	= ali_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ali_c2_cable_detect,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
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
	u8 rev, tmp;
	struct pci_dev *north, *isa_bridge;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);

	/*
	 * The chipset revision selects the driver operations and
	 * mode data.
	 */

	if (rev >= 0x20 && rev < 0xC2) {
		/* 1543-E/F, 1543C-C, 1543C-D, 1543C-E */
		pci_read_config_byte(pdev, 0x4B, &tmp);
		/* Clear CD-ROM DMA write bit */
		tmp &= 0x7F;
		pci_write_config_byte(pdev, 0x4B, tmp);
	} else if (rev >= 0xC2) {
		/* Enable cable detection logic */
		pci_read_config_byte(pdev, 0x4B, &tmp);
		pci_write_config_byte(pdev, 0x4B, tmp | 0x08);
	}
	north = pci_get_bus_and_slot(0, PCI_DEVFN(0,0));
	isa_bridge = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL);

	if (north && north->vendor == PCI_VENDOR_ID_AL && isa_bridge) {
		/* Configure the ALi bridge logic. For non ALi rely on BIOS.
		   Set the south bridge enable bit */
		pci_read_config_byte(isa_bridge, 0x79, &tmp);
		if (rev == 0xC2)
			pci_write_config_byte(isa_bridge, 0x79, tmp | 0x04);
		else if (rev > 0xC2 && rev < 0xC5)
			pci_write_config_byte(isa_bridge, 0x79, tmp | 0x02);
	}
	if (rev >= 0x20) {
		/*
		 * CD_ROM DMA on (0x53 bit 0). Enable this even if we want
		 * to use PIO. 0x53 bit 1 (rev 20 only) - enable FIFO control
		 * via 0x54/55.
		 */
		pci_read_config_byte(pdev, 0x53, &tmp);
		if (rev <= 0x20)
			tmp &= ~0x02;
		if (rev >= 0xc7)
			tmp |= 0x03;
		else
			tmp |= 0x01;	/* CD_ROM enable for DMA */
		pci_write_config_byte(pdev, 0x53, tmp);
	}
	pci_dev_put(isa_bridge);
	pci_dev_put(north);
	ata_pci_clear_simplex(pdev);
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
		.sht = &ali_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.port_ops = &ali_early_port_ops
	};
	/* Revision 0x20 added DMA */
	static const struct ata_port_info info_20 = {
		.sht = &ali_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.port_ops = &ali_20_port_ops
	};
	/* Revision 0x20 with support logic added UDMA */
	static const struct ata_port_info info_20_udma = {
		.sht = &ali_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x07,	/* UDMA33 */
		.port_ops = &ali_20_port_ops
	};
	/* Revision 0xC2 adds UDMA66 */
	static const struct ata_port_info info_c2 = {
		.sht = &ali_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x1f,
		.port_ops = &ali_c2_port_ops
	};
	/* Revision 0xC3 is UDMA100 */
	static const struct ata_port_info info_c3 = {
		.sht = &ali_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x3f,
		.port_ops = &ali_c2_port_ops
	};
	/* Revision 0xC4 is UDMA133 */
	static const struct ata_port_info info_c4 = {
		.sht = &ali_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST | ATA_FLAG_PIO_LBA48,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x7f,
		.port_ops = &ali_c2_port_ops
	};
	/* Revision 0xC5 is UDMA133 with LBA48 DMA */
	static const struct ata_port_info info_c5 = {
		.sht = &ali_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x7f,
		.port_ops = &ali_c5_port_ops
	};

	const struct ata_port_info *ppi[] = { NULL, NULL };
	u8 rev, tmp;
	struct pci_dev *isa_bridge;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);

	/*
	 * The chipset revision selects the driver operations and
	 * mode data.
	 */

	if (rev < 0x20) {
		ppi[0] = &info_early;
	} else if (rev < 0xC2) {
        	ppi[0] = &info_20;
	} else if (rev == 0xC2) {
        	ppi[0] = &info_c2;
	} else if (rev == 0xC3) {
        	ppi[0] = &info_c3;
	} else if (rev == 0xC4) {
        	ppi[0] = &info_c4;
	} else
        	ppi[0] = &info_c5;

	ali_init_chipset(pdev);

	isa_bridge = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL);
	if (isa_bridge && rev >= 0x20 && rev < 0xC2) {
		/* Are we paired with a UDMA capable chip */
		pci_read_config_byte(isa_bridge, 0x5E, &tmp);
		if ((tmp & 0x1E) == 0x12)
	        	ppi[0] = &info_20_udma;
		pci_dev_put(isa_bridge);
	}
	return ata_pci_init_one(pdev, ppi);
}

#ifdef CONFIG_PM
static int ali_reinit_one(struct pci_dev *pdev)
{
	ali_init_chipset(pdev);
	return ata_pci_device_resume(pdev);
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
	return pci_register_driver(&ali_pci_driver);
}


static void __exit ali_exit(void)
{
	pci_unregister_driver(&ali_pci_driver);
}


MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for ALi PATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ali);
MODULE_VERSION(DRV_VERSION);

module_init(ali_init);
module_exit(ali_exit);
