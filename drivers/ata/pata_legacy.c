// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   pata-legacy.c - Legacy port PATA/SATA controller driver.
 *   Copyright 2005/2006 Red Hat, all rights reserved.
 *
 *   An ATA driver for the legacy ATA ports.
 *
 *  This driver handles legacy (that is "ISA side") IDE ports found
 *  on PC class systems. There are three hybrid devices that are exceptions:
 *  The Cyrix 5510/5520 where a pre SFF ATA device is on the bridge and
 *  the MPIIX where the tuning is PCI side but the IDE is "ISA side".
 */

#include <linux/async.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/platform_device.h>

#define DRV_NAME "pata_legacy"
#define DRV_VERSION "0.6.5"

#define NR_HOST 6

static int all;
module_param(all, int, 0444);
MODULE_PARM_DESC(all,
		 "Set to probe unclaimed pri/sec ISA port ranges even if PCI");

static int probe_all;
module_param(probe_all, int, 0);
MODULE_PARM_DESC(probe_all,
		 "Set to probe tertiary+ ISA port ranges even if PCI");

static int probe_mask = ~0;
module_param(probe_mask, int, 0);
MODULE_PARM_DESC(probe_mask, "Probe mask for legacy ISA PATA ports");

static int autospeed;
module_param(autospeed, int, 0);
MODULE_PARM_DESC(autospeed, "Chip present that snoops speed changes");

static int pio_mask = ATA_PIO4;
module_param(pio_mask, int, 0);
MODULE_PARM_DESC(pio_mask, "PIO range for autospeed devices");

static int iordy_mask = 0xFFFFFFFF;
module_param(iordy_mask, int, 0);
MODULE_PARM_DESC(iordy_mask, "Use IORDY if available");

enum controller {
	BIOS = 0,
	SNOOP = 1,

	UNKNOWN = -1
};

struct legacy_data {
	unsigned long timing;
	u8 clock[2];
	u8 last;
	int fast;
	enum controller type;
	struct platform_device *platform_dev;
};

struct legacy_probe {
	unsigned char *name;
	unsigned long port;
	unsigned int irq;
	unsigned int slot;
	enum controller type;
	unsigned long private;
};

struct legacy_controller {
	const char *name;
	struct ata_port_operations *ops;
	unsigned int pio_mask;
	unsigned int flags;
	unsigned int pflags;
	int (*setup)(struct platform_device *, struct legacy_probe *probe,
		struct legacy_data *data);
};

static int legacy_port[NR_HOST] = { 0x1f0, 0x170, 0x1e8, 0x168, 0x1e0, 0x160 };

static struct legacy_probe probe_list[NR_HOST];
static struct legacy_data legacy_data[NR_HOST];
static struct ata_host *legacy_host[NR_HOST];

/**
 *	legacy_probe_add	-	Add interface to probe list
 *	@port: Controller port
 *	@irq: IRQ number
 *	@type: Controller type
 *	@private: Controller specific info
 *
 *	Add an entry into the probe list for ATA controllers. This is used
 *	to add the default ISA slots and then to build up the table
 *	further according to other ISA/Weird device scans
 *
 *	An I/O port list is used to keep ordering stable and sane, as we
 *	don't have any good way to talk about ordering otherwise
 */

static int legacy_probe_add(unsigned long port, unsigned int irq,
				enum controller type, unsigned long private)
{
	struct legacy_probe *lp = &probe_list[0];
	int i;
	struct legacy_probe *free = NULL;

	for (i = 0; i < NR_HOST; i++) {
		if (lp->port == 0 && free == NULL)
			free = lp;
		/* Matching port, or the correct slot for ordering */
		if (lp->port == port || legacy_port[i] == port) {
			if (!(probe_mask & 1 << i))
				return -1;
			free = lp;
			break;
		}
		lp++;
	}
	if (free == NULL) {
		printk(KERN_ERR "pata_legacy: Too many interfaces.\n");
		return -1;
	}
	/* Fill in the entry for later probing */
	free->port = port;
	free->irq = irq;
	free->type = type;
	free->private = private;
	return 0;
}


/**
 *	legacy_set_mode		-	mode setting
 *	@link: IDE link
 *	@unused: Device that failed when error is returned
 *
 *	Use a non standard set_mode function. We don't want to be tuned.
 *
 *	The BIOS configured everything. Our job is not to fiddle. Just use
 *	whatever PIO the hardware is using and leave it at that. When we
 *	get some kind of nice user driven API for control then we can
 *	expand on this as per hdparm in the base kernel.
 */

static int legacy_set_mode(struct ata_link *link, struct ata_device **unused)
{
	struct ata_device *dev;

	ata_for_each_dev(dev, link, ENABLED) {
		ata_dev_info(dev, "configured for PIO\n");
		dev->pio_mode = XFER_PIO_0;
		dev->xfer_mode = XFER_PIO_0;
		dev->xfer_shift = ATA_SHIFT_PIO;
		dev->flags |= ATA_DFLAG_PIO;
	}
	return 0;
}

static const struct scsi_host_template legacy_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static const struct ata_port_operations legacy_base_port_ops = {
	.inherits	= &ata_sff_port_ops,
	.cable_detect	= ata_cable_40wire,
};

/*
 *	These ops are used if the user indicates the hardware
 *	snoops the commands to decide on the mode and handles the
 *	mode selection "magically" itself. Several legacy controllers
 *	do this. The mode range can be set if it is not 0x1F by setting
 *	pio_mask as well.
 */

static struct ata_port_operations simple_port_ops = {
	.inherits	= &legacy_base_port_ops,
	.sff_data_xfer	= ata_sff_data_xfer32,
};

static struct ata_port_operations legacy_port_ops = {
	.inherits	= &legacy_base_port_ops,
	.sff_data_xfer	= ata_sff_data_xfer32,
	.set_mode	= legacy_set_mode,
};

static struct legacy_controller controllers[] = {
	{"BIOS",	&legacy_port_ops, 	ATA_PIO4,
			ATA_FLAG_NO_IORDY,	0,			NULL },
	{"Snooping", 	&simple_port_ops, 	ATA_PIO4,
			0,			0,			NULL },
};

/**
 *	probe_chip_type		-	Discover controller
 *	@probe: Probe entry to check
 *
 *	Probe an ATA port and identify the type of controller. We don't
 *	check if the controller appears to be driveless at this point.
 */

static __init int probe_chip_type(struct legacy_probe *probe)
{
	int mask = 1 << probe->slot;

	if (autospeed & mask)
		return SNOOP;
	return BIOS;
}


/**
 *	legacy_init_one		-	attach a legacy interface
 *	@probe: probe record
 *
 *	Register an ISA bus IDE interface. Such interfaces are PIO and we
 *	assume do not support IRQ sharing.
 */

static __init int legacy_init_one(struct legacy_probe *probe)
{
	struct legacy_controller *controller = &controllers[probe->type];
	int pio_modes = controller->pio_mask;
	unsigned long io = probe->port;
	u32 mask = (1 << probe->slot);
	struct ata_port_operations *ops = controller->ops;
	struct legacy_data *ld = &legacy_data[probe->slot];
	struct ata_host *host = NULL;
	struct ata_port *ap;
	struct platform_device *pdev;
	struct ata_device *dev;
	void __iomem *io_addr, *ctrl_addr;
	u32 iordy = (iordy_mask & mask) ? 0: ATA_FLAG_NO_IORDY;
	int ret;

	iordy |= controller->flags;

	pdev = platform_device_register_simple(DRV_NAME, probe->slot, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ret = -EBUSY;
	if (devm_request_region(&pdev->dev, io, 8, "pata_legacy") == NULL ||
	    devm_request_region(&pdev->dev, io + 0x0206, 1,
							"pata_legacy") == NULL)
		goto fail;

	ret = -ENOMEM;
	io_addr = devm_ioport_map(&pdev->dev, io, 8);
	ctrl_addr = devm_ioport_map(&pdev->dev, io + 0x0206, 1);
	if (!io_addr || !ctrl_addr)
		goto fail;
	ld->type = probe->type;
	if (controller->setup)
		if (controller->setup(pdev, probe, ld) < 0)
			goto fail;
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		goto fail;
	ap = host->ports[0];

	ap->ops = ops;
	ap->pio_mask = pio_modes;
	ap->flags |= ATA_FLAG_SLAVE_POSS | iordy;
	ap->pflags |= controller->pflags;
	ap->ioaddr.cmd_addr = io_addr;
	ap->ioaddr.altstatus_addr = ctrl_addr;
	ap->ioaddr.ctl_addr = ctrl_addr;
	ata_sff_std_ports(&ap->ioaddr);
	ap->host->private_data = ld;

	ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx", io, io + 0x0206);

	ret = ata_host_activate(host, probe->irq, ata_sff_interrupt, 0,
				&legacy_sht);
	if (ret)
		goto fail;
	async_synchronize_full();
	ld->platform_dev = pdev;

	/* Nothing found means we drop the port as its probably not there */

	ret = -ENODEV;
	ata_for_each_dev(dev, &ap->link, ALL) {
		if (!ata_dev_absent(dev)) {
			legacy_host[probe->slot] = host;
			ld->platform_dev = pdev;
			return 0;
		}
	}
	ata_host_detach(host);
fail:
	platform_device_unregister(pdev);
	return ret;
}

/**
 *	legacy_check_special_cases	-	ATA special cases
 *	@p: PCI device to check
 *	@primary: set this if we find an ATA master
 *	@secondary: set this if we find an ATA secondary
 *
 *	A small number of vendors implemented early PCI ATA interfaces
 *	on bridge logic without the ATA interface being PCI visible.
 *	Where we have a matching PCI driver we must skip the relevant
 *	device here. If we don't know about it then the legacy driver
 *	is the right driver anyway.
 */

static void __init legacy_check_special_cases(struct pci_dev *p, int *primary,
								int *secondary)
{
	/* Cyrix CS5510 pre SFF MWDMA ATA on the bridge */
	if (p->vendor == 0x1078 && p->device == 0x0000) {
		*primary = *secondary = 1;
		return;
	}
	/* Cyrix CS5520 pre SFF MWDMA ATA on the bridge */
	if (p->vendor == 0x1078 && p->device == 0x0002) {
		*primary = *secondary = 1;
		return;
	}
	/* Intel MPIIX - PIO ATA on non PCI side of bridge */
	if (p->vendor == 0x8086 && p->device == 0x1234) {
		u16 r;
		pci_read_config_word(p, 0x6C, &r);
		if (r & 0x8000) {
			/* ATA port enabled */
			if (r & 0x4000)
				*secondary = 1;
			else
				*primary = 1;
		}
		return;
	}
}

/**
 *	legacy_init		-	attach legacy interfaces
 *
 *	Attach legacy IDE interfaces by scanning the usual IRQ/port suspects.
 *	Right now we do not scan the ide0 and ide1 address but should do so
 *	for non PCI systems or systems with no PCI IDE legacy mode devices.
 *	If you fix that note there are special cases to consider like CS5510/20.
 */

static __init int legacy_init(void)
{
	int i;
	int ct = 0;
	int primary = 0;
	int secondary = 0;
	int pci_present = 0;
	struct legacy_probe *pl = &probe_list[0];
	int slot = 0;

	struct pci_dev *p = NULL;

	for_each_pci_dev(p) {
		int r;
		/* Check for any overlap of the system ATA mappings. Native
		   mode controllers stuck on these addresses or some devices
		   in 'raid' mode won't be found by the storage class test */
		for (r = 0; r < 6; r++) {
			if (pci_resource_start(p, r) == 0x1f0)
				primary = 1;
			if (pci_resource_start(p, r) == 0x170)
				secondary = 1;
		}
		/* Check for special cases */
		legacy_check_special_cases(p, &primary, &secondary);

		/* If PCI bus is present then don't probe for tertiary
		   legacy ports */
		pci_present = 1;
	}

	if (primary == 0 || all)
		legacy_probe_add(0x1F0, 14, UNKNOWN, 0);
	if (secondary == 0 || all)
		legacy_probe_add(0x170, 15, UNKNOWN, 0);

	if (probe_all || !pci_present) {
		/* ISA extra ports */
		legacy_probe_add(0x1E8, 11, UNKNOWN, 0);
		legacy_probe_add(0x168, 10, UNKNOWN, 0);
		legacy_probe_add(0x1E0, 8, UNKNOWN, 0);
		legacy_probe_add(0x160, 12, UNKNOWN, 0);
	}

	for (i = 0; i < NR_HOST; i++, pl++) {
		if (pl->port == 0)
			continue;
		if (pl->type == UNKNOWN)
			pl->type = probe_chip_type(pl);
		pl->slot = slot++;
		if (legacy_init_one(pl) == 0)
			ct++;
	}
	if (ct != 0)
		return 0;
	return -ENODEV;
}

static __exit void legacy_exit(void)
{
	int i;

	for (i = 0; i < NR_HOST; i++) {
		struct legacy_data *ld = &legacy_data[i];

		if (legacy_host[i])
			ata_host_detach(legacy_host[i]);
		platform_device_unregister(ld->platform_dev);
	}
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for legacy ATA");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(legacy_init);
module_exit(legacy_exit);
