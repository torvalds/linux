// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IDE tuning and bus mastering support for the CS5510/CS5520
 *	chipsets
 *
 *	The CS5510/CS5520 are slightly unusual devices. Unlike the
 *	typical IDE controllers they do bus mastering with the drive in
 *	PIO mode and smarter silicon.
 *
 *	The practical upshot of this is that we must always tune the
 *	drive for the right PIO mode. We must also ignore all the blacklists
 *	and the drive bus mastering DMA information. Also to confuse matters
 *	further we can do DMA on PIO only drives.
 *
 *	DMA on the 5510 also requires we disable_hlt() during DMA on early
 *	revisions.
 *
 *	*** This driver is strictly experimental ***
 *
 *	(c) Copyright Red Hat Inc 2002
 *
 * Documentation:
 *	Not publicly available.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"pata_cs5520"
#define DRV_VERSION	"0.6.6"

struct pio_clocks
{
	int address;
	int assert;
	int recovery;
};

static const struct pio_clocks cs5520_pio_clocks[]={
	{3, 6, 11},
	{2, 5, 6},
	{1, 4, 3},
	{1, 3, 2},
	{1, 2, 1}
};

/**
 *	cs5520_set_timings	-	program PIO timings
 *	@ap: ATA port
 *	@adev: ATA device
 *	@pio: PIO ID
 *
 *	Program the PIO mode timings for the controller according to the pio
 *	clocking table.
 */

static void cs5520_set_timings(struct ata_port *ap, struct ata_device *adev, int pio)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int slave = adev->devno;

	pio -= XFER_PIO_0;

	/* Channel command timing */
	pci_write_config_byte(pdev, 0x62 + ap->port_no,
				(cs5520_pio_clocks[pio].recovery << 4) |
				(cs5520_pio_clocks[pio].assert));
	/* FIXME: should these use address ? */
	/* Read command timing */
	pci_write_config_byte(pdev, 0x64 +  4*ap->port_no + slave,
				(cs5520_pio_clocks[pio].recovery << 4) |
				(cs5520_pio_clocks[pio].assert));
	/* Write command timing */
	pci_write_config_byte(pdev, 0x66 +  4*ap->port_no + slave,
				(cs5520_pio_clocks[pio].recovery << 4) |
				(cs5520_pio_clocks[pio].assert));
}

/**
 *	cs5520_set_piomode	-	program PIO timings
 *	@ap: ATA port
 *	@adev: ATA device
 *
 *	Program the PIO mode timings for the controller according to the pio
 *	clocking table.
 */

static void cs5520_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	cs5520_set_timings(ap, adev, adev->pio_mode);
}

static const struct scsi_host_template cs5520_sht = {
	ATA_BASE_SHT(DRV_NAME),
	.sg_tablesize		= LIBATA_DUMB_MAX_PRD,
	.dma_boundary		= ATA_DMA_BOUNDARY,
};

static struct ata_port_operations cs5520_port_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.qc_prep		= ata_bmdma_dumb_qc_prep,
	.cable_detect		= ata_cable_40wire,
	.set_piomode		= cs5520_set_piomode,
};

static int cs5520_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const unsigned int cmd_port[] = { 0x1F0, 0x170 };
	static const unsigned int ctl_port[] = { 0x3F6, 0x376 };
	struct ata_port_info pi = {
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= ATA_PIO4,
		.port_ops	= &cs5520_port_ops,
	};
	const struct ata_port_info *ppi[2];
	u8 pcicfg;
	void __iomem *iomap[5];
	struct ata_host *host;
	struct ata_ioports *ioaddr;
	int i, rc;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* IDE port enable bits */
	pci_read_config_byte(pdev, 0x60, &pcicfg);

	/* Check if the ATA ports are enabled */
	if ((pcicfg & 3) == 0)
		return -ENODEV;

	ppi[0] = ppi[1] = &ata_dummy_port_info;
	if (pcicfg & 1)
		ppi[0] = &pi;
	if (pcicfg & 2)
		ppi[1] = &pi;

	if ((pcicfg & 0x40) == 0) {
		dev_warn(&pdev->dev, "DMA mode disabled. Enabling.\n");
		pci_write_config_byte(pdev, 0x60, pcicfg | 0x40);
	}

	pi.mwdma_mask = id->driver_data;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 2);
	if (!host)
		return -ENOMEM;

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))) {
		dev_err(&pdev->dev, "unable to configure DMA mask.\n");
		return -ENODEV;
	}

	/* Map IO ports and initialize host accordingly */
	iomap[0] = devm_ioport_map(&pdev->dev, cmd_port[0], 8);
	iomap[1] = devm_ioport_map(&pdev->dev, ctl_port[0], 1);
	iomap[2] = devm_ioport_map(&pdev->dev, cmd_port[1], 8);
	iomap[3] = devm_ioport_map(&pdev->dev, ctl_port[1], 1);
	iomap[4] = pcim_iomap(pdev, 2, 0);

	if (!iomap[0] || !iomap[1] || !iomap[2] || !iomap[3] || !iomap[4])
		return -ENOMEM;

	ioaddr = &host->ports[0]->ioaddr;
	ioaddr->cmd_addr = iomap[0];
	ioaddr->ctl_addr = iomap[1];
	ioaddr->altstatus_addr = iomap[1];
	ioaddr->bmdma_addr = iomap[4];
	ata_sff_std_ports(ioaddr);

	ata_port_desc(host->ports[0],
		      "cmd 0x%x ctl 0x%x", cmd_port[0], ctl_port[0]);
	ata_port_pbar_desc(host->ports[0], 4, 0, "bmdma");

	ioaddr = &host->ports[1]->ioaddr;
	ioaddr->cmd_addr = iomap[2];
	ioaddr->ctl_addr = iomap[3];
	ioaddr->altstatus_addr = iomap[3];
	ioaddr->bmdma_addr = iomap[4] + 8;
	ata_sff_std_ports(ioaddr);

	ata_port_desc(host->ports[1],
		      "cmd 0x%x ctl 0x%x", cmd_port[1], ctl_port[1]);
	ata_port_pbar_desc(host->ports[1], 4, 8, "bmdma");

	/* activate the host */
	pci_set_master(pdev);
	rc = ata_host_start(host);
	if (rc)
		return rc;

	for (i = 0; i < 2; i++) {
		static const int irq[] = { 14, 15 };
		struct ata_port *ap = host->ports[i];

		if (ata_port_is_dummy(ap))
			continue;

		rc = devm_request_irq(&pdev->dev, irq[ap->port_no],
				      ata_bmdma_interrupt, 0, DRV_NAME, host);
		if (rc)
			return rc;

		ata_port_desc_misc(ap, irq[i]);
	}

	return ata_host_register(host, &cs5520_sht);
}

#ifdef CONFIG_PM_SLEEP
/**
 *	cs5520_reinit_one	-	device resume
 *	@pdev: PCI device
 *
 *	Do any reconfiguration work needed by a resume from RAM. We need
 *	to restore DMA mode support on BIOSen which disabled it
 */

static int cs5520_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = pci_get_drvdata(pdev);
	u8 pcicfg;
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	pci_read_config_byte(pdev, 0x60, &pcicfg);
	if ((pcicfg & 0x40) == 0)
		pci_write_config_byte(pdev, 0x60, pcicfg | 0x40);

	ata_host_resume(host);
	return 0;
}

/**
 *	cs5520_pci_device_suspend	-	device suspend
 *	@pdev: PCI device
 *	@mesg: PM event message
 *
 *	We have to cut and waste bits from the standard method because
 *	the 5520 is a bit odd and not just a pure ATA device. As a result
 *	we must not disable it. The needed code is short and this avoids
 *	chip specific mess in the core code.
 */

static int cs5520_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct ata_host *host = pci_get_drvdata(pdev);

	ata_host_suspend(host, mesg);

	pci_save_state(pdev);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

/* For now keep DMA off. We can set it for all but A rev CS5510 once the
   core ATA code can handle it */

static const struct pci_device_id pata_cs5520[] = {
	{ PCI_VDEVICE(CYRIX, PCI_DEVICE_ID_CYRIX_5510), },
	{ PCI_VDEVICE(CYRIX, PCI_DEVICE_ID_CYRIX_5520), },

	{ },
};

static struct pci_driver cs5520_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= pata_cs5520,
	.probe 		= cs5520_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM_SLEEP
	.suspend	= cs5520_pci_device_suspend,
	.resume		= cs5520_reinit_one,
#endif
};

module_pci_driver(cs5520_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Cyrix CS5510/5520");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pata_cs5520);
MODULE_VERSION(DRV_VERSION);
