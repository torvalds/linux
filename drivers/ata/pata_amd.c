/*
 * pata_amd.c 	- AMD PATA for new ATA layer
 *			  (C) 2005-2006 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
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
#define DRV_VERSION "0.3.8"

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
	UT = T / min_t(int, max_t(int, clock, 1), 2);

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
	t = (t & ~(3 << ((3 - dn) << 1))) | ((FIT(at.setup, 1, 4) - 1) << ((3 - dn) << 1));
	pci_write_config_byte(pdev, offset + 0x0C , t);

	/* Configure the 8bit I/O timing */
	pci_write_config_byte(pdev, offset + 0x0E + (1 - (dn >> 1)),
		((FIT(at.act8b, 1, 16) - 1) << 4) | (FIT(at.rec8b, 1, 16) - 1));

	/* Drive timing */
	pci_write_config_byte(pdev, offset + 0x08 + (3 - dn),
		((FIT(at.active, 1, 16) - 1) << 4) | (FIT(at.recover, 1, 16) - 1));

	switch (clock) {
		case 1:
		t = at.udma ? (0xc0 | (FIT(at.udma, 2, 5) - 2)) : 0x03;
		break;

		case 2:
		t = at.udma ? (0xc0 | amd_cyc2udma[FIT(at.udma, 2, 10)]) : 0x03;
		break;

		case 3:
		t = at.udma ? (0xc0 | amd_cyc2udma[FIT(at.udma, 1, 10)]) : 0x03;
		break;

		case 4:
		t = at.udma ? (0xc0 | amd_cyc2udma[FIT(at.udma, 1, 15)]) : 0x03;
		break;

		default:
			return;
	}

	/* UDMA timing */
	pci_write_config_byte(pdev, offset + 0x10 + (3 - dn), t);
}

/**
 *	amd_probe_init		-	perform reset handling
 *	@ap: ATA port
 *	@deadline: deadline jiffies for the operation
 *
 *	Reset sequence checking enable bits to see which ports are
 *	active.
 */

static int amd_pre_reset(struct ata_port *ap, unsigned long deadline)
{
	static const struct pci_bits amd_enable_bits[] = {
		{ 0x40, 1, 0x02, 0x02 },
		{ 0x40, 1, 0x01, 0x01 }
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (!pci_test_config_bits(pdev, &amd_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_std_prereset(ap, deadline);
}

static void amd_error_handler(struct ata_port *ap)
{
	return ata_bmdma_drive_eh(ap, amd_pre_reset,
				      ata_std_softreset, NULL,
				      ata_std_postreset);
}

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
 *	amd33_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the AMD registers for PIO mode.
 */

static void amd33_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x40, adev->pio_mode, 1);
}

static void amd66_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x40, adev->pio_mode, 2);
}

static void amd100_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	timing_setup(ap, adev, 0x40, adev->pio_mode, 3);
}

static void amd133_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
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


/**
 *	nv_probe_init	-	cable detection
 *	@ap: ATA port
 *
 *	Perform cable detection. The BIOS stores this in PCI config
 *	space for us.
 */

static int nv_pre_reset(struct ata_port *ap, unsigned long deadline)
{
	static const struct pci_bits nv_enable_bits[] = {
		{ 0x50, 1, 0x02, 0x02 },
		{ 0x50, 1, 0x01, 0x01 }
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (!pci_test_config_bits(pdev, &nv_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_std_prereset(ap, deadline);
}

static void nv_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, nv_pre_reset,
			       ata_std_softreset, NULL,
			       ata_std_postreset);
}

static int nv_cable_detect(struct ata_port *ap)
{
	static const u8 bitmask[2] = {0x03, 0x0C};
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 ata66;
	u16 udma;
	int cbl;

	pci_read_config_byte(pdev, 0x52, &ata66);
	if (ata66 & bitmask[ap->port_no])
		cbl = ATA_CBL_PATA80;
	else
		cbl = ATA_CBL_PATA40;

 	/* We now have to double check because the Nvidia boxes BIOS
 	   doesn't always set the cable bits but does set mode bits */
 	pci_read_config_word(pdev, 0x62 - 2 * ap->port_no, &udma);
 	if ((udma & 0xC4) == 0xC4 || (udma & 0xC400) == 0xC400)
		cbl = ATA_CBL_PATA80;
	return cbl;
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

static struct scsi_host_template amd_sht = {
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

static struct ata_port_operations amd33_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= amd33_set_piomode,
	.set_dmamode	= amd33_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= amd_error_handler,
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

static struct ata_port_operations amd66_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= amd66_set_piomode,
	.set_dmamode	= amd66_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= amd_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_unknown,

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

static struct ata_port_operations amd100_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= amd100_set_piomode,
	.set_dmamode	= amd100_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= amd_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_unknown,

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

static struct ata_port_operations amd133_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= amd133_set_piomode,
	.set_dmamode	= amd133_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= amd_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= amd_cable_detect,

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

static struct ata_port_operations nv100_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= nv100_set_piomode,
	.set_dmamode	= nv100_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= nv_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= nv_cable_detect,

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

static struct ata_port_operations nv133_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= nv133_set_piomode,
	.set_dmamode	= nv133_set_dmamode,
	.mode_filter	= ata_pci_default_filter,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= nv_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= nv_cable_detect,

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

static int amd_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info[10] = {
		{	/* 0: AMD 7401 */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,	/* No SWDMA */
			.udma_mask = 0x07,	/* UDMA 33 */
			.port_ops = &amd33_port_ops
		},
		{	/* 1: Early AMD7409 - no swdma */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x1f,	/* UDMA 66 */
			.port_ops = &amd66_port_ops
		},
		{	/* 2: AMD 7409, no swdma errata */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x1f,	/* UDMA 66 */
			.port_ops = &amd66_port_ops
		},
		{	/* 3: AMD 7411 */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x3f,	/* UDMA 100 */
			.port_ops = &amd100_port_ops
		},
		{	/* 4: AMD 7441 */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x3f,	/* UDMA 100 */
			.port_ops = &amd100_port_ops
		},
		{	/* 5: AMD 8111*/
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x7f,	/* UDMA 133, no swdma */
			.port_ops = &amd133_port_ops
		},
		{	/* 6: AMD 8111 UDMA 100 (Serenade) */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x3f,	/* UDMA 100, no swdma */
			.port_ops = &amd133_port_ops
		},
		{	/* 7: Nvidia Nforce */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x3f,	/* UDMA 100 */
			.port_ops = &nv100_port_ops
		},
		{	/* 8: Nvidia Nforce2 and later */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x7f,	/* UDMA 133, no swdma */
			.port_ops = &nv133_port_ops
		},
		{	/* 9: AMD CS5536 (Geode companion) */
			.sht = &amd_sht,
			.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x3f,	/* UDMA 100 */
			.port_ops = &amd100_port_ops
		}
	};
	const struct ata_port_info *ppi[] = { NULL, NULL };
	static int printed_version;
	int type = id->driver_data;
	u8 rev;
	u8 fifo;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);
	pci_read_config_byte(pdev, 0x41, &fifo);

	/* Check for AMD7409 without swdma errata and if found adjust type */
	if (type == 1 && rev > 0x7)
		type = 2;

	/* Check for AMD7411 */
	if (type == 3)
		/* FIFO is broken */
		pci_write_config_byte(pdev, 0x41, fifo & 0x0F);
	else
		pci_write_config_byte(pdev, 0x41, fifo | 0xF0);

	/* Serenade ? */
	if (type == 5 && pdev->subsystem_vendor == PCI_VENDOR_ID_AMD &&
			 pdev->subsystem_device == PCI_DEVICE_ID_AMD_SERENADE)
		type = 6;	/* UDMA 100 only */

	if (type < 3)
		ata_pci_clear_simplex(pdev);

	/* And fire it up */
	ppi[0] = &info[type];
	return ata_pci_init_one(pdev, ppi);
}

#ifdef CONFIG_PM
static int amd_reinit_one(struct pci_dev *pdev)
{
	if (pdev->vendor == PCI_VENDOR_ID_AMD) {
		u8 fifo;
		pci_read_config_byte(pdev, 0x41, &fifo);
		if (pdev->device == PCI_DEVICE_ID_AMD_VIPER_7411)
			/* FIFO is broken */
			pci_write_config_byte(pdev, 0x41, fifo & 0x0F);
		else
			pci_write_config_byte(pdev, 0x41, fifo | 0xF0);
		if (pdev->device == PCI_DEVICE_ID_AMD_VIPER_7409 ||
		    pdev->device == PCI_DEVICE_ID_AMD_COBRA_7401)
		    	ata_pci_clear_simplex(pdev);
	}
	return ata_pci_device_resume(pdev);
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
MODULE_DESCRIPTION("low-level driver for AMD PATA IDE");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, amd);
MODULE_VERSION(DRV_VERSION);

module_init(amd_init);
module_exit(amd_exit);
