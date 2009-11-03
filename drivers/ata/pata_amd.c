/*
 * pata_amd.c 	- AMD PATA for new ATA layer
 *			  (C) 2005-2006 Red Hat Inc
 *
 *  Based on pata-sil680. Errata information is taken from data sheets
 *  and the amd74xx.c driver by Vojtech Pavlik. Nvidia SATA devices are
 *  claimed by sata-nv.c.
 *
 *  TODO:
 *	Variable system clock when/if it makes sense
 *	Power management on ports
 *
 *
 *  Documentation publically available.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_amd"
#define DRV_VERSION "0.4.1"

/**
 *	timing_setup		-	shared timing computation and load
 *	@ap: ATA port being set up
 *	@adev: drive being configured
 *	@offset: port offset
 *	@speed: target speed
 *	@clock: clock multiplier (number of times 33MHz for this part)
 *
 *	Perform the actual timing set up for Nvidia or AMD PATA devices.
 *	The actual devices vary so they all call into this helper function
 *	providing the clock multipler and offset (because AMD and Nvidia put
 *	the ports at different locations).
 */

static void timing_setup(struct ata_port *ap, struct ata_device *adev, int offset, int speed, int clock)
{
	static const unsigned char amd_cyc2udma[] = {
		6, 6, 5, 4, 0, 1, 1, 2, 2, 3, 3, 3, 3, 3, 3, 7
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct ata_device *peer = ata_dev_pair(adev);
	int dn = ap->port_no * 2 + adev->devno;
	struct ata_timing at, apeer;
	int T, UT;
	const int amd_clock = 33333;	/* KHz. */
	u8 t;

	T = 1000000000 / amd_clock;
	UT = T;
	if (clock >= 2)
		UT = T / 2;

	if (ata_timing_compute(adev, speed, &at, T, UT) < 0) {
		dev_printk(KERN_ERR, &pdev->dev, "unknown mode %d.\n", speed);
		return;
	}

	if (peer) {
		/* This may be over conservative */
		if (peer->dma_mode) {
			ata_timing_compute(peer, peer->dma_mode, &apeer, T, UT);
			ata_timing_merge(&apeer, &at, &at, ATA_TIMING_8BIT);
		}
		ata_timing_compute(peer, peer->pio_mode, &apeer, T, UT);
		ata_timing_merge(&apeer, &at, &at, ATA_TIMING_8BIT);
	}

	if (speed == XFER_UDMA_5 && amd_clock <= 33333) at.udma = 1;
	if (speed == XFER_UDMA_6 && amd_clock <= 33333) at.udma = 15;

	/*
	 *	Now do the setup work
	 */

	/* Configure the address set up timing */
	pci_read_config_byte(pdev, offset + 0x0C, &t);
	t = (t & ~(3 << ((3 - dn) << 1))) | ((clamp_val(at.setup, 1, 4) - 1) << ((3 - dn) << 1));
	pci_write_config_byte(pdev, offset + 0x0C , t);

	/* Configure the 8bit I/O timing */
	pci_write_config_byte(pdev, offset + 0x0E + (1 - (dn >> 1)),
		((clamp_val(at.act8b, 1, 16) - 1) << 4) | (clamp_val(at.rec8b, 1, 16) - 1));

	/* Drive timing */
	pci_write_config_byte(pdev, offset + 0x08 + (3 - dn),
		((clamp_val(at.active, 1, 16) - 1) << 4) | (clamp_val(at.recover, 1, 16) - 1));

	switch (clock) {
		case 1:
		t = at.udma ? (0xc0 | (clamp_val(at.udma, 2, 5) - 2)) : 0x03;
		break;

		case 2:
		t = at.udma ? (0xc0 | amd_cyc2udma[clamp_val(at.udma, 2, 10)]) : 0x03;
		break;

		case 3:
		t = at.udma ? (0xc0 | amd_cyc2udma[clamp_val(at.udma, 1, 10)]) : 0x03;
		break;

		case 4:
		t = at.udma ? (0xc0 | amd_cyc2udma[clamp_val(at.udma, 1, 15)]) : 0x03;
		break;

		default:
			return;
	}

	/* UDMA timing */
	if (at.udma)
		pci_write_config_byte(pdev, offset + 0x10 + (3 - dn), t);
}

/**
 *	amd_pre_reset		-	perform reset handling
 *	@link: ATA link
 *	@deadline: deadline jiffies for the operation
 *
 *	Reset sequence checking enable bits to see which ports are
 *	active.
 */

static int amd_pre_reset(struct ata_link *link, unsigned long deadline)
{
	static const struct pci_bits amd_enable_bits[] = {
		{ 0x40, 1, 0x02, 0x02 },
		{ 0x40, 1, 0x01, 0x01 }
	};

	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (!pci_test_config_bits(pdev, &amd_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_sff_prereset(link, deadline);
}

/**
 *	amd_cable_detect	-	report cable type
 *	@ap: port
 *
 *	AMD controller/BIOS setups record the cable type in word 0x42
 */

static int amd_cable_detect(struct ata_port *ap)
{
	static const u32 bitmask[2] = {0x03, 0x0C};
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 ata66;

	pci_read_config_byte(pdev, 0x42, &ata66);
	if (ata66 & bitmask[ap->port_no])
		return ATA_CBL_PATA80;
	return ATA_CBL_PATA40;
}

/**
 *	amd_fifo_setup		-	set the PIO FIFO for ATA/ATAPI
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Set the PCI fifo for this device according to the devices present
 *	on the bus at this point in time. We need to turn the post write buffer
 *	off for ATAPI devices as we may need to issue a word sized write to the
 *	device as the final I/O
 */

static void amd_fifo_setup(struct ata_port *ap)
{
	struct ata_device *adev;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const u8 fifobit[2] = { 0xC0, 0x30};
	u8 fifo = fifobit[ap->port_no];
	u8 r;


	ata_for_each_dev(adev, &ap->link, ENABLED) {
		if (adev->class == ATA_DEV_ATAPI)
			fifo = 0;
	}
	if (pdev->device == PCI_DEVICE_ID_AMD_VIPER_7411) /* FIFO is broken */
		fifo = 0;

	/* On the later chips the read prefetch bits become no-op bits */
	pci_read_config_byte(pdev, 0x41, &r);
	r &= ~fifobit[ap->port_no];
	r |= fifo;
	pci_write_config_byte(pdev, 0x41, r);
}

/**
 *	amd33_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the AMD registers for PIO mode.
 */

static void amd33_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	amd_fifo_setup(ap);
	timing_setup(ap, adev, 0x40, adev->pio_mode, 1);
}

static void amd66_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	amd_fifo_setup(ap);
	timing_setup(ap, adev, 0x40, adev->pio_mode, 2);
}

static void amd100_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	amd_fifo_setup(ap);
	timing_setup(ap, adev, 0x40, adev->pio_mode, 3);
}

static void amd133_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	amd_fifo_setup(ap);
	timing_setup(ap, adev, 0x40, adev->pio_mode, 4);
}

/**
 *	amd33_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the MWDMA/UDMA modes for the AMD and Nvidia
 *	chipset.
 */

static void amd33_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x40, adev->dma_mode, 1);
}

static void amd66_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x40, adev->dma_mode, 2);
}

static void amd100_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x40, adev->dma_mode, 3);
}

static void amd133_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x40, adev->dma_mode, 4);
}

/* Both host-side and drive-side detection results are worthless on NV
 * PATAs.  Ignore them and just follow what BIOS configured.  Both the
 * current configuration in PCI config reg and ACPI GTM result are
 * cached during driver attach and are consulted to select transfer
 * mode.
 */
static unsigned long nv_mode_filter(struct ata_device *dev,
				    unsigned long xfer_mask)
{
	static const unsigned int udma_mask_map[] =
		{ ATA_UDMA2, ATA_UDMA1, ATA_UDMA0, 0,
		  ATA_UDMA3, ATA_UDMA4, ATA_UDMA5, ATA_UDMA6 };
	struct ata_port *ap = dev->link->ap;
	char acpi_str[32] = "";
	u32 saved_udma, udma;
	const struct ata_acpi_gtm *gtm;
	unsigned long bios_limit = 0, acpi_limit = 0, limit;

	/* find out what BIOS configured */
	udma = saved_udma = (unsigned long)ap->host->private_data;

	if (ap->port_no == 0)
		udma >>= 16;
	if (dev->devno == 0)
		udma >>= 8;

	if ((udma & 0xc0) == 0xc0)
		bios_limit = ata_pack_xfermask(0, 0, udma_mask_map[udma & 0x7]);

	/* consult ACPI GTM too */
	gtm = ata_acpi_init_gtm(ap);
	if (gtm) {
		acpi_limit = ata_acpi_gtm_xfermask(dev, gtm);

		snprintf(acpi_str, sizeof(acpi_str), " (%u:%u:0x%x)",
			 gtm->drive[0].dma, gtm->drive[1].dma, gtm->flags);
	}

	/* be optimistic, EH can take care of things if something goes wrong */
	limit = bios_limit | acpi_limit;

	/* If PIO or DMA isn't configured at all, don't limit.  Let EH
	 * handle it.
	 */
	if (!(limit & ATA_MASK_PIO))
		limit |= ATA_MASK_PIO;
	if (!(limit & (ATA_MASK_MWDMA | ATA_MASK_UDMA)))
		limit |= ATA_MASK_MWDMA | ATA_MASK_UDMA;
	/* PIO4, MWDMA2, UDMA2 should always be supported regardless of
	   cable detection result */
	limit |= ata_pack_xfermask(ATA_PIO4, ATA_MWDMA2, ATA_UDMA2);

	ata_port_printk(ap, KERN_DEBUG, "nv_mode_filter: 0x%lx&0x%lx->0x%lx, "
			"BIOS=0x%lx (0x%x) ACPI=0x%lx%s\n",
			xfer_mask, limit, xfer_mask & limit, bios_limit,
			saved_udma, acpi_limit, acpi_str);

	return xfer_mask & limit;
}

/**
 *	nv_probe_init	-	cable detection
 *	@lin: ATA link
 *
 *	Perform cable detection. The BIOS stores this in PCI config
 *	space for us.
 */

static int nv_pre_reset(struct ata_link *link, unsigned long deadline)
{
	static const struct pci_bits nv_enable_bits[] = {
		{ 0x50, 1, 0x02, 0x02 },
		{ 0x50, 1, 0x01, 0x01 }
	};

	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (!pci_test_config_bits(pdev, &nv_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_sff_prereset(link, deadline);
}

/**
 *	nv100_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the AMD registers for PIO mode.
 */

static void nv100_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x50, adev->pio_mode, 3);
}

static void nv133_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x50, adev->pio_mode, 4);
}

/**
 *	nv100_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the MWDMA/UDMA modes for the AMD and Nvidia
 *	chipset.
 */

static void nv100_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x50, adev->dma_mode, 3);
}

static void nv133_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x50, adev->dma_mode, 4);
}

static void nv_host_stop(struct ata_host *host)
{
	u32 udma = (unsigned long)host->private_data;

	/* restore PCI config register 0x60 */
	pci_write_config_dword(to_pci_dev(host->dev), 0x60, udma);
}

static struct scsi_host_template amd_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static const struct ata_port_operations amd_base_port_ops = {
	.inherits	= &ata_bmdma32_port_ops,
	.prereset	= amd_pre_reset,
};

static struct ata_port_operations amd33_port_ops = {
	.inherits	= &amd_base_port_ops,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= amd33_set_piomode,
	.set_dmamode	= amd33_set_dmamode,
};

static struct ata_port_operations amd66_port_ops = {
	.inherits	= &amd_base_port_ops,
	.cable_detect	= ata_cable_unknown,
	.set_piomode	= amd66_set_piomode,
	.set_dmamode	= amd66_set_dmamode,
};

static struct ata_port_operations amd100_port_ops = {
	.inherits	= &amd_base_port_ops,
	.cable_detect	= ata_cable_unknown,
	.set_piomode	= amd100_set_piomode,
	.set_dmamode	= amd100_set_dmamode,
};

static struct ata_port_operations amd133_port_ops = {
	.inherits	= &amd_base_port_ops,
	.cable_detect	= amd_cable_detect,
	.set_piomode	= amd133_set_piomode,
	.set_dmamode	= amd133_set_dmamode,
};

static const struct ata_port_operations nv_base_port_ops = {
	.inherits	= &ata_bmdma_port_ops,
	.cable_detect	= ata_cable_ignore,
	.mode_filter	= nv_mode_filter,
	.prereset	= nv_pre_reset,
	.host_stop	= nv_host_stop,
};

static struct ata_port_operations nv100_port_ops = {
	.inherits	= &nv_base_port_ops,
	.set_piomode	= nv100_set_piomode,
	.set_dmamode	= nv100_set_dmamode,
};

static struct ata_port_operations nv133_port_ops = {
	.inherits	= &nv_base_port_ops,
	.set_piomode	= nv133_set_piomode,
	.set_dmamode	= nv133_set_dmamode,
};

static void amd_clear_fifo(struct pci_dev *pdev)
{
	u8 fifo;
	/* Disable the FIFO, the FIFO logic will re-enable it as
	   appropriate */
	pci_read_config_byte(pdev, 0x41, &fifo);
	fifo &= 0x0F;
	pci_write_config_byte(pdev, 0x41, fifo);
}

static int amd_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info[10] = {
		{	/* 0: AMD 7401 - no swdma */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA2,
			.port_ops = &amd33_port_ops
		},
		{	/* 1: Early AMD7409 - no swdma */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA4,
			.port_ops = &amd66_port_ops
		},
		{	/* 2: AMD 7409 */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA4,
			.port_ops = &amd66_port_ops
		},
		{	/* 3: AMD 7411 */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA5,
			.port_ops = &amd100_port_ops
		},
		{	/* 4: AMD 7441 */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA5,
			.port_ops = &amd100_port_ops
		},
		{	/* 5: AMD 8111 - no swdma */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA6,
			.port_ops = &amd133_port_ops
		},
		{	/* 6: AMD 8111 UDMA 100 (Serenade) - no swdma */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA5,
			.port_ops = &amd133_port_ops
		},
		{	/* 7: Nvidia Nforce */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA5,
			.port_ops = &nv100_port_ops
		},
		{	/* 8: Nvidia Nforce2 and later - no swdma */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA6,
			.port_ops = &nv133_port_ops
		},
		{	/* 9: AMD CS5536 (Geode companion) */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = ATA_PIO4,
			.mwdma_mask = ATA_MWDMA2,
			.udma_mask = ATA_UDMA5,
			.port_ops = &amd100_port_ops
		}
	};
	const struct ata_port_info *ppi[] = { NULL, NULL };
	static int printed_version;
	int type = id->driver_data;
	void *hpriv = NULL;
	u8 fifo;
	int rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	pci_read_config_byte(pdev, 0x41, &fifo);

	/* Check for AMD7409 without swdma errata and if found adjust type */
	if (type == 1 && pdev->revision > 0x7)
		type = 2;

	/* Serenade ? */
	if (type == 5 && pdev->subsystem_vendor == PCI_VENDOR_ID_AMD &&
			 pdev->subsystem_device == PCI_DEVICE_ID_AMD_SERENADE)
		type = 6;	/* UDMA 100 only */

	/*
	 * Okay, type is determined now.  Apply type-specific workarounds.
	 */
	ppi[0] = &info[type];

	if (type < 3)
		ata_pci_bmdma_clear_simplex(pdev);
	if (pdev->vendor == PCI_VENDOR_ID_AMD)
		amd_clear_fifo(pdev);
	/* Cable detection on Nvidia chips doesn't work too well,
	 * cache BIOS programmed UDMA mode.
	 */
	if (type == 7 || type == 8) {
		u32 udma;

		pci_read_config_dword(pdev, 0x60, &udma);
		hpriv = (void *)(unsigned long)udma;
	}

	/* And fire it up */
	return ata_pci_sff_init_one(pdev, ppi, &amd_sht, hpriv);
}

#ifdef CONFIG_PM
static int amd_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	if (pdev->vendor == PCI_VENDOR_ID_AMD) {
		amd_clear_fifo(pdev);
		if (pdev->device == PCI_DEVICE_ID_AMD_VIPER_7409 ||
		    pdev->device == PCI_DEVICE_ID_AMD_COBRA_7401)
			ata_pci_bmdma_clear_simplex(pdev);
	}
	ata_host_resume(host);
	return 0;
}
#endif

static const struct pci_device_id amd[] = {
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_COBRA_7401),		0 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_VIPER_7409),		1 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_VIPER_7411),		3 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_OPUS_7441),		4 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_8111_IDE),		5 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_IDE),	7 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE2_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE2S_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE3S_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP51_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP55_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP61_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP65_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP67_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP73_IDE),	8 },
	{ PCI_VDEVICE(NVIDIA,	PCI_DEVICE_ID_NVIDIA_NFORCE_MCP77_IDE),	8 },
	{ PCI_VDEVICE(AMD,	PCI_DEVICE_ID_AMD_CS5536_IDE),		9 },

	{ },
};

static struct pci_driver amd_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= amd,
	.probe 		= amd_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= amd_reinit_one,
#endif
};

static int __init amd_init(void)
{
	return pci_register_driver(&amd_pci_driver);
}

static void __exit amd_exit(void)
{
	pci_unregister_driver(&amd_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for AMD and Nvidia PATA IDE");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, amd);
MODULE_VERSION(DRV_VERSION);

module_init(amd_init);
module_exit(amd_exit);
