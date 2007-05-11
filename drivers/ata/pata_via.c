/*
 * pata_via.c 	- VIA PATA for new ATA layer
 *			  (C) 2005-2006 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 *  Documentation
 *	Most chipset documentation available under NDA only
 *
 *  VIA version guide
 *	VIA VT82C561	-	early design, uses ata_generic currently
 *	VIA VT82C576	-	MWDMA, 33Mhz
 *	VIA VT82C586	-	MWDMA, 33Mhz
 *	VIA VT82C586a	-	Added UDMA to 33Mhz
 *	VIA VT82C586b	-	UDMA33
 *	VIA VT82C596a	-	Nonfunctional UDMA66
 *	VIA VT82C596b	-	Working UDMA66
 *	VIA VT82C686	-	Nonfunctional UDMA66
 *	VIA VT82C686a	-	Working UDMA66
 *	VIA VT82C686b	-	Updated to UDMA100
 *	VIA VT8231	-	UDMA100
 *	VIA VT8233	-	UDMA100
 *	VIA VT8233a	-	UDMA133
 *	VIA VT8233c	-	UDMA100
 *	VIA VT8235	-	UDMA133
 *	VIA VT8237	-	UDMA133
 *	VIA VT8237S	-	UDMA133
 *	VIA VT8251	-	UDMA133
 *
 *	Most registers remain compatible across chips. Others start reserved
 *	and acquire sensible semantics if set to 1 (eg cable detect). A few
 *	exceptions exist, notably around the FIFO settings.
 *
 *	One additional quirk of the VIA design is that like ALi they use few
 *	PCI IDs for a lot of chips.
 *
 *	Based heavily on:
 *
 * Version 3.38
 *
 * VIA IDE driver for Linux. Supported southbridges:
 *
 *   vt82c576, vt82c586, vt82c586a, vt82c586b, vt82c596a, vt82c596b,
 *   vt82c686, vt82c686a, vt82c686b, vt8231, vt8233, vt8233c, vt8233a,
 *   vt8235, vt8237
 *
 * Copyright (c) 2000-2002 Vojtech Pavlik
 *
 * Based on the work of:
 *	Michel Aubry
 *	Jeff Garzik
 *	Andre Hedrick

 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_via"
#define DRV_VERSION "0.3.1"

/*
 *	The following comes directly from Vojtech Pavlik's ide/pci/via82cxxx
 *	driver.
 */

enum {
	VIA_UDMA	= 0x007,
	VIA_UDMA_NONE	= 0x000,
	VIA_UDMA_33	= 0x001,
	VIA_UDMA_66	= 0x002,
	VIA_UDMA_100	= 0x003,
	VIA_UDMA_133	= 0x004,
	VIA_BAD_PREQ	= 0x010, /* Crashes if PREQ# till DDACK# set */
	VIA_BAD_CLK66	= 0x020, /* 66 MHz clock doesn't work correctly */
	VIA_SET_FIFO	= 0x040, /* Needs to have FIFO split set */
	VIA_NO_UNMASK	= 0x080, /* Doesn't work with IRQ unmasking on */
	VIA_BAD_ID	= 0x100, /* Has wrong vendor ID (0x1107) */
	VIA_BAD_AST	= 0x200, /* Don't touch Address Setup Timing */
	VIA_NO_ENABLES	= 0x400, /* Has no enablebits */
};

/*
 * VIA SouthBridge chips.
 */

static const struct via_isa_bridge {
	const char *name;
	u16 id;
	u8 rev_min;
	u8 rev_max;
	u16 flags;
} via_isa_bridges[] = {
	{ "vt8237s",	PCI_DEVICE_ID_VIA_8237S,    0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8251",	PCI_DEVICE_ID_VIA_8251,     0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "cx700",	PCI_DEVICE_ID_VIA_CX700,    0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt6410",	PCI_DEVICE_ID_VIA_6410,     0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST | VIA_NO_ENABLES},
	{ "vt8237a",	PCI_DEVICE_ID_VIA_8237A,    0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8237",	PCI_DEVICE_ID_VIA_8237,     0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8235",	PCI_DEVICE_ID_VIA_8235,     0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8233a",	PCI_DEVICE_ID_VIA_8233A,    0x00, 0x2f, VIA_UDMA_133 | VIA_BAD_AST },
	{ "vt8233c",	PCI_DEVICE_ID_VIA_8233C_0,  0x00, 0x2f, VIA_UDMA_100 },
	{ "vt8233",	PCI_DEVICE_ID_VIA_8233_0,   0x00, 0x2f, VIA_UDMA_100 },
	{ "vt8231",	PCI_DEVICE_ID_VIA_8231,     0x00, 0x2f, VIA_UDMA_100 },
	{ "vt82c686b",	PCI_DEVICE_ID_VIA_82C686,   0x40, 0x4f, VIA_UDMA_100 },
	{ "vt82c686a",	PCI_DEVICE_ID_VIA_82C686,   0x10, 0x2f, VIA_UDMA_66 },
	{ "vt82c686",	PCI_DEVICE_ID_VIA_82C686,   0x00, 0x0f, VIA_UDMA_33 | VIA_BAD_CLK66 },
	{ "vt82c596b",	PCI_DEVICE_ID_VIA_82C596,   0x10, 0x2f, VIA_UDMA_66 },
	{ "vt82c596a",	PCI_DEVICE_ID_VIA_82C596,   0x00, 0x0f, VIA_UDMA_33 | VIA_BAD_CLK66 },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x47, 0x4f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x40, 0x46, VIA_UDMA_33 | VIA_SET_FIFO | VIA_BAD_PREQ },
	{ "vt82c586b",	PCI_DEVICE_ID_VIA_82C586_0, 0x30, 0x3f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586a",	PCI_DEVICE_ID_VIA_82C586_0, 0x20, 0x2f, VIA_UDMA_33 | VIA_SET_FIFO },
	{ "vt82c586",	PCI_DEVICE_ID_VIA_82C586_0, 0x00, 0x0f, VIA_UDMA_NONE | VIA_SET_FIFO },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f, VIA_UDMA_NONE | VIA_SET_FIFO | VIA_NO_UNMASK },
	{ "vt82c576",	PCI_DEVICE_ID_VIA_82C576,   0x00, 0x2f, VIA_UDMA_NONE | VIA_SET_FIFO | VIA_NO_UNMASK | VIA_BAD_ID },
	{ NULL }
};

/**
 *	via_cable_detect	-	cable detection
 *	@ap: ATA port
 *
 *	Perform cable detection. Actually for the VIA case the BIOS
 *	already did this for us. We read the values provided by the
 *	BIOS. If you are using an 8235 in a non-PC configuration you
 *	may need to update this code.
 *
 *	Hotplug also impacts on this.
 */

static int via_cable_detect(struct ata_port *ap) {
	const struct via_isa_bridge *config = ap->host->private_data;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 ata66;

	/* Early chips are 40 wire */
	if ((config->flags & VIA_UDMA) < VIA_UDMA_66)
		return ATA_CBL_PATA40;
	/* UDMA 66 chips have only drive side logic */
	else if((config->flags & VIA_UDMA) < VIA_UDMA_100)
		return ATA_CBL_PATA_UNK;
	/* UDMA 100 or later */
	pci_read_config_dword(pdev, 0x50, &ata66);
	/* Check both the drive cable reporting bits, we might not have
	   two drives */
	if (ata66 & (0x10100000 >> (16 * ap->port_no)))
		return ATA_CBL_PATA80;
	return ATA_CBL_PATA40;
}

static int via_pre_reset(struct ata_port *ap, unsigned long deadline)
{
	const struct via_isa_bridge *config = ap->host->private_data;

	if (!(config->flags & VIA_NO_ENABLES)) {
		static const struct pci_bits via_enable_bits[] = {
			{ 0x40, 1, 0x02, 0x02 },
			{ 0x40, 1, 0x01, 0x01 }
		};
		struct pci_dev *pdev = to_pci_dev(ap->host->dev);
		if (!pci_test_config_bits(pdev, &via_enable_bits[ap->port_no]))
			return -ENOENT;
	}

	return ata_std_prereset(ap, deadline);
}


/**
 *	via_error_handler		-	reset for VIA chips
 *	@ap: ATA port
 *
 *	Handle the reset callback for the later chips with cable detect
 */

static void via_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, via_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	via_do_set_mode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *	@mode: ATA mode being programmed
 *	@tdiv: Clocks per PCI clock
 *	@set_ast: Set to program address setup
 *	@udma_type: UDMA mode/format of registers
 *
 *	Program the VIA registers for DMA and PIO modes. Uses the ata timing
 *	support in order to compute modes.
 *
 *	FIXME: Hotplug will require we serialize multiple mode changes
 *	on the two channels.
 */

static void via_do_set_mode(struct ata_port *ap, struct ata_device *adev, int mode, int tdiv, int set_ast, int udma_type)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct ata_device *peer = ata_dev_pair(adev);
	struct ata_timing t, p;
	static int via_clock = 33333;	/* Bus clock in kHZ - ought to be tunable one day */
	unsigned long T =  1000000000 / via_clock;
	unsigned long UT = T/tdiv;
	int ut;
	int offset = 3 - (2*ap->port_no) - adev->devno;


	/* Calculate the timing values we require */
	ata_timing_compute(adev, mode, &t, T, UT);

	/* We share 8bit timing so we must merge the constraints */
	if (peer) {
		if (peer->pio_mode) {
			ata_timing_compute(peer, peer->pio_mode, &p, T, UT);
			ata_timing_merge(&p, &t, &t, ATA_TIMING_8BIT);
		}
	}

	/* Address setup is programmable but breaks on UDMA133 setups */
	if (set_ast) {
		u8 setup;	/* 2 bits per drive */
		int shift = 2 * offset;

		pci_read_config_byte(pdev, 0x4C, &setup);
		setup &= ~(3 << shift);
		setup |= FIT(t.setup, 1, 4) << shift;	/* 1,4 or 1,4 - 1  FIXME */
		pci_write_config_byte(pdev, 0x4C, setup);
	}

	/* Load the PIO mode bits */
	pci_write_config_byte(pdev, 0x4F - ap->port_no,
		((FIT(t.act8b, 1, 16) - 1) << 4) | (FIT(t.rec8b, 1, 16) - 1));
	pci_write_config_byte(pdev, 0x48 + offset,
		((FIT(t.active, 1, 16) - 1) << 4) | (FIT(t.recover, 1, 16) - 1));

	/* Load the UDMA bits according to type */
	switch(udma_type) {
		default:
			/* BUG() ? */
			/* fall through */
		case 33:
			ut = t.udma ? (0xe0 | (FIT(t.udma, 2, 5) - 2)) : 0x03;
			break;
		case 66:
			ut = t.udma ? (0xe8 | (FIT(t.udma, 2, 9) - 2)) : 0x0f;
			break;
		case 100:
			ut = t.udma ? (0xe0 | (FIT(t.udma, 2, 9) - 2)) : 0x07;
			break;
		case 133:
			ut = t.udma ? (0xe0 | (FIT(t.udma, 2, 9) - 2)) : 0x07;
			break;
	}
	/* Set UDMA unless device is not UDMA capable */
	if (udma_type)
		pci_write_config_byte(pdev, 0x50 + offset, ut);
}

static void via_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	const struct via_isa_bridge *config = ap->host->private_data;
	int set_ast = (config->flags & VIA_BAD_AST) ? 0 : 1;
	int mode = config->flags & VIA_UDMA;
	static u8 tclock[5] = { 1, 1, 2, 3, 4 };
	static u8 udma[5] = { 0, 33, 66, 100, 133 };

	via_do_set_mode(ap, adev, adev->pio_mode, tclock[mode], set_ast, udma[mode]);
}

static void via_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	const struct via_isa_bridge *config = ap->host->private_data;
	int set_ast = (config->flags & VIA_BAD_AST) ? 0 : 1;
	int mode = config->flags & VIA_UDMA;
	static u8 tclock[5] = { 1, 1, 2, 3, 4 };
	static u8 udma[5] = { 0, 33, 66, 100, 133 };

	via_do_set_mode(ap, adev, adev->dma_mode, tclock[mode], set_ast, udma[mode]);
}

static struct scsi_host_template via_sht = {
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
#ifdef CONFIG_PM
	.resume			= ata_scsi_device_resume,
	.suspend		= ata_scsi_device_suspend,
#endif
};

static struct ata_port_operations via_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= via_set_piomode,
	.set_dmamode	= via_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= via_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= via_cable_detect,

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

static struct ata_port_operations via_port_ops_noirq = {
	.port_disable	= ata_port_disable,
	.set_piomode	= via_set_piomode,
	.set_dmamode	= via_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= via_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= via_cable_detect,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer_noirq,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

/**
 *	via_config_fifo		-	set up the FIFO
 *	@pdev: PCI device
 *	@flags: configuration flags
 *
 *	Set the FIFO properties for this device if neccessary. Used both on
 *	set up and on and the resume path
 */

static void via_config_fifo(struct pci_dev *pdev, unsigned int flags)
{
	u8 enable;

	/* 0x40 low bits indicate enabled channels */
	pci_read_config_byte(pdev, 0x40 , &enable);
	enable &= 3;

	if (flags & VIA_SET_FIFO) {
		static const u8 fifo_setting[4] = {0x00, 0x60, 0x00, 0x20};
		u8 fifo;

		pci_read_config_byte(pdev, 0x43, &fifo);

		/* Clear PREQ# until DDACK# for errata */
		if (flags & VIA_BAD_PREQ)
			fifo &= 0x7F;
		else
			fifo &= 0x9f;
		/* Turn on FIFO for enabled channels */
		fifo |= fifo_setting[enable];
		pci_write_config_byte(pdev, 0x43, fifo);
	}
}

/**
 *	via_init_one		-	discovery callback
 *	@pdev: PCI device
 *	@id: PCI table info
 *
 *	A VIA IDE interface has been discovered. Figure out what revision
 *	and perform configuration work before handing it to the ATA layer
 */

static int via_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	/* Early VIA without UDMA support */
	static struct ata_port_info via_mwdma_info = {
		.sht = &via_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SETXFER_POLLING,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.port_ops = &via_port_ops
	};
	/* Ditto with IRQ masking required */
	static struct ata_port_info via_mwdma_info_borked = {
		.sht = &via_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SETXFER_POLLING,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.port_ops = &via_port_ops_noirq,
	};
	/* VIA UDMA 33 devices (and borked 66) */
	static struct ata_port_info via_udma33_info = {
		.sht = &via_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SETXFER_POLLING,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x7,
		.port_ops = &via_port_ops
	};
	/* VIA UDMA 66 devices */
	static struct ata_port_info via_udma66_info = {
		.sht = &via_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SETXFER_POLLING,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x1f,
		.port_ops = &via_port_ops
	};
	/* VIA UDMA 100 devices */
	static struct ata_port_info via_udma100_info = {
		.sht = &via_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SETXFER_POLLING,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x3f,
		.port_ops = &via_port_ops
	};
	/* UDMA133 with bad AST (All current 133) */
	static struct ata_port_info via_udma133_info = {
		.sht = &via_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SETXFER_POLLING,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x7f,	/* FIXME: should check north bridge */
		.port_ops = &via_port_ops
	};
	struct ata_port_info *port_info[2], *type;
	struct pci_dev *isa = NULL;
	const struct via_isa_bridge *config;
	static int printed_version;
	u8 t;
	u8 enable;
	u32 timing;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* To find out how the IDE will behave and what features we
	   actually have to look at the bridge not the IDE controller */
	for (config = via_isa_bridges; config->id; config++)
		if ((isa = pci_get_device(PCI_VENDOR_ID_VIA +
			!!(config->flags & VIA_BAD_ID),
			config->id, NULL))) {

			pci_read_config_byte(isa, PCI_REVISION_ID, &t);
			if (t >= config->rev_min &&
			    t <= config->rev_max)
				break;
			pci_dev_put(isa);
		}

	if (!config->id) {
		printk(KERN_WARNING "via: Unknown VIA SouthBridge, disabling.\n");
		return -ENODEV;
	}
	pci_dev_put(isa);

	/* 0x40 low bits indicate enabled channels */
	pci_read_config_byte(pdev, 0x40 , &enable);
	enable &= 3;
	if (enable == 0) {
		return -ENODEV;
	}

	/* Initialise the FIFO for the enabled channels. */
	via_config_fifo(pdev, config->flags);

	/* Clock set up */
	switch(config->flags & VIA_UDMA) {
		case VIA_UDMA_NONE:
			if (config->flags & VIA_NO_UNMASK)
				type = &via_mwdma_info_borked;
			else
				type = &via_mwdma_info;
			break;
		case VIA_UDMA_33:
			type = &via_udma33_info;
			break;
		case VIA_UDMA_66:
			type = &via_udma66_info;
			/* The 66 MHz devices require we enable the clock */
			pci_read_config_dword(pdev, 0x50, &timing);
			timing |= 0x80008;
			pci_write_config_dword(pdev, 0x50, timing);
			break;
		case VIA_UDMA_100:
			type = &via_udma100_info;
			break;
		case VIA_UDMA_133:
			type = &via_udma133_info;
			break;
		default:
			WARN_ON(1);
			return -ENODEV;
	}

	if (config->flags & VIA_BAD_CLK66) {
		/* Disable the 66MHz clock on problem devices */
		pci_read_config_dword(pdev, 0x50, &timing);
		timing &= ~0x80008;
		pci_write_config_dword(pdev, 0x50, timing);
	}

	/* We have established the device type, now fire it up */
	type->private_data = (void *)config;

	port_info[0] = port_info[1] = type;
	return ata_pci_init_one(pdev, port_info, 2);
}

#ifdef CONFIG_PM
/**
 *	via_reinit_one		-	reinit after resume
 *	@pdev; PCI device
 *
 *	Called when the VIA PATA device is resumed. We must then
 *	reconfigure the fifo and other setup we may have altered. In
 *	addition the kernel needs to have the resume methods on PCI
 *	quirk supported.
 */

static int via_reinit_one(struct pci_dev *pdev)
{
	u32 timing;
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	const struct via_isa_bridge *config = host->private_data;

	via_config_fifo(pdev, config->flags);

	if ((config->flags & VIA_UDMA) == VIA_UDMA_66) {
		/* The 66 MHz devices require we enable the clock */
		pci_read_config_dword(pdev, 0x50, &timing);
		timing |= 0x80008;
		pci_write_config_dword(pdev, 0x50, timing);
	}
	if (config->flags & VIA_BAD_CLK66) {
		/* Disable the 66MHz clock on problem devices */
		pci_read_config_dword(pdev, 0x50, &timing);
		timing &= ~0x80008;
		pci_write_config_dword(pdev, 0x50, timing);
	}
	return ata_pci_device_resume(pdev);
}
#endif

static const struct pci_device_id via[] = {
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_82C576_1), },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_82C586_1), },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_6410), },
	{ PCI_VDEVICE(VIA, PCI_DEVICE_ID_VIA_SATA_EIDE), },

	{ },
};

static struct pci_driver via_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= via,
	.probe 		= via_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= via_reinit_one,
#endif
};

static int __init via_init(void)
{
	return pci_register_driver(&via_pci_driver);
}

static void __exit via_exit(void)
{
	pci_unregister_driver(&via_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for VIA PATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, via);
MODULE_VERSION(DRV_VERSION);

module_init(via_init);
module_exit(via_exit);
