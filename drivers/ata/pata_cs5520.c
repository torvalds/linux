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
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Documentation:
 *	Not publically available.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
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
 *	cs5520_enable_dma	-	turn on DMA bits
 *
 *	Turn on the DMA bits for this disk. Needed because the BIOS probably
 *	has not done the work for us. Belongs in the core SATA code.
 */

static void cs5520_enable_dma(struct ata_port *ap, struct ata_device *adev)
{
	/* Set the DMA enable/disable flag */
	u8 reg = ioread8(ap->ioaddr.bmdma_addr + 0x02);
	reg |= 1<<(adev->devno + 5);
	iowrite8(reg, ap->ioaddr.bmdma_addr + 0x02);
}

/**
 *	cs5520_set_dmamode	-	program DMA timings
 *	@ap: ATA port
 *	@adev: ATA device
 *
 *	Program the DMA mode timings for the controller according to the pio
 *	clocking table. Note that this device sets the DMA timings to PIO
 *	mode values. This may seem bizarre but the 5520 architecture talks
 *	PIO mode to the disk and DMA mode to the controller so the underlying
 *	transfers are PIO timed.
 */

static void cs5520_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static const int dma_xlate[3] = { XFER_PIO_0, XFER_PIO_3, XFER_PIO_4 };
	cs5520_set_timings(ap, adev, dma_xlate[adev->dma_mode]);
	cs5520_enable_dma(ap, adev);
}

/**
 *	cs5520_set_piomode	-	program PIO timings
 *	@ap: ATA port
 *	@adev: ATA device
 *
 *	Program the PIO mode timings for the controller according to the pio
 *	clocking table. We know pio_mode will equal dma_mode because of the
 *	CS5520 architecture. At least once we turned DMA on and wrote a
 *	mode setter.
 */

static void cs5520_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	cs5520_set_timings(ap, adev, adev->pio_mode);
}

static struct scsi_host_template cs5520_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_DUMB_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations cs5520_port_ops = {
	.set_piomode		= cs5520_set_piomode,
	.set_dmamode		= cs5520_set_dmamode,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= ata_bmdma_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= ata_cable_40wire,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_dumb_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,

	.port_start		= ata_sff_port_start,
};

static int __devinit cs5520_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const unsigned int cmd_port[] = { 0x1F0, 0x170 };
	static const unsigned int ctl_port[] = { 0x3F6, 0x376 };
	struct ata_port_info pi = {
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x1f,
		.port_ops	= &cs5520_port_ops,
	};
	const struct ata_port_info *ppi[2];
	u8 pcicfg;
	void __iomem *iomap[5];
	struct ata_host *host;
	struct ata_ioports *ioaddr;
	int i, rc;

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
		dev_printk(KERN_WARNING, &pdev->dev,
			   "DMA mode disabled. Enabling.\n");
		pci_write_config_byte(pdev, 0x60, pcicfg | 0x40);
	}

	pi.mwdma_mask = id->driver_data;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 2);
	if (!host)
		return -ENOMEM;

	/* Perform set up for DMA */
	if (pci_enable_device_bars(pdev, 1<<2)) {
		printk(KERN_ERR DRV_NAME ": unable to configure BAR2.\n");
		return -ENODEV;
	}

	if (pci_set_dma_mask(pdev, DMA_32BIT_MASK)) {
		printk(KERN_ERR DRV_NAME ": unable to configure DMA mask.\n");
		return -ENODEV;
	}
	if (pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK)) {
		printk(KERN_ERR DRV_NAME ": unable to configure consistent DMA mask.\n");
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
	ata_std_ports(ioaddr);

	ata_port_desc(host->ports[0],
		      "cmd 0x%x ctl 0x%x", cmd_port[0], ctl_port[0]);
	ata_port_pbar_desc(host->ports[0], 4, 0, "bmdma");

	ioaddr = &host->ports[1]->ioaddr;
	ioaddr->cmd_addr = iomap[2];
	ioaddr->ctl_addr = iomap[3];
	ioaddr->altstatus_addr = iomap[3];
	ioaddr->bmdma_addr = iomap[4] + 8;
	ata_std_ports(ioaddr);

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
				      ata_interrupt, 0, DRV_NAME, host);
		if (rc)
			return rc;

		ata_port_desc(ap, "irq %d", irq[i]);
	}

	return ata_host_register(host, &cs5520_sht);
}

#ifdef CONFIG_PM
/**
 *	cs5520_reinit_one	-	device resume
 *	@pdev: PCI device
 *
 *	Do any reconfiguration work needed by a resume from RAM. We need
 *	to restore DMA mode support on BIOSen which disabled it
 */

static int cs5520_reinit_one(struct pci_dev *pdev)
{
	u8 pcicfg;
	pci_read_config_byte(pdev, 0x60, &pcicfg);
	if ((pcicfg & 0x40) == 0)
		pci_write_config_byte(pdev, 0x60, pcicfg | 0x40);
	return ata_pci_device_resume(pdev);
}

/**
 *	cs5520_pci_device_suspend	-	device suspend
 *	@pdev: PCI device
 *
 *	We have to cut and waste bits from the standard method because
 *	the 5520 is a bit odd and not just a pure ATA device. As a result
 *	we must not disable it. The needed code is short and this avoids
 *	chip specific mess in the core code.
 */

static int cs5520_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc = 0;

	rc = ata_host_suspend(host, mesg);
	if (rc)
		return rc;

	pci_save_state(pdev);
	return 0;
}
#endif /* CONFIG_PM */

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
#ifdef CONFIG_PM
	.suspend	= cs5520_pci_device_suspend,
	.resume		= cs5520_reinit_one,
#endif
};

static int __init cs5520_init(void)
{
	return pci_register_driver(&cs5520_pci_driver);
}

static void __exit cs5520_exit(void)
{
	pci_unregister_driver(&cs5520_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Cyrix CS5510/5520");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pata_cs5520);
MODULE_VERSION(DRV_VERSION);

module_init(cs5520_init);
module_exit(cs5520_exit);

