/*
 *  pata_rdc		-	Driver for later RDC PATA controllers
 *
 *  This is actually a driver for hardware meeting
 *  INCITS 370-2004 (1510D): ATA Host Adapter Standards
 *
 *  Based on ata_piix.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/dmi.h>

#define DRV_NAME	"pata_rdc"
#define DRV_VERSION	"0.01"

struct rdc_host_priv {
	u32 saved_iocfg;
};

/**
 *	rdc_pata_cable_detect - Probe host controller cable detect info
 *	@ap: Port for which cable detect info is desired
 *
 *	Read 80c cable indicator from ATA PCI device's PCI config
 *	register.  This register is normally set by firmware (BIOS).
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static int rdc_pata_cable_detect(struct ata_port *ap)
{
	struct rdc_host_priv *hpriv = ap->host->private_data;
	u8 mask;

	/* check BIOS cable detect results */
	mask = 0x30 << (2 * ap->port_no);
	if ((hpriv->saved_iocfg & mask) == 0)
		return ATA_CBL_PATA40;
	return ATA_CBL_PATA80;
}

/**
 *	rdc_pata_prereset - prereset for PATA host controller
 *	@link: Target link
 *	@deadline: deadline jiffies for the operation
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static int rdc_pata_prereset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	static const struct pci_bits rdc_enable_bits[] = {
		{ 0x41U, 1U, 0x80UL, 0x80UL },	/* port 0 */
		{ 0x43U, 1U, 0x80UL, 0x80UL },	/* port 1 */
	};

	if (!pci_test_config_bits(pdev, &rdc_enable_bits[ap->port_no]))
		return -ENOENT;
	return ata_sff_prereset(link, deadline);
}

/**
 *	rdc_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *
 *	Set PIO mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void rdc_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	unsigned int is_slave	= (adev->devno != 0);
	unsigned int master_port= ap->port_no ? 0x42 : 0x40;
	unsigned int slave_port	= 0x44;
	u16 master_data;
	u8 slave_data;
	u8 udma_enable;
	int control = 0;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	if (pio >= 2)
		control |= 1;	/* TIME1 enable */
	if (ata_pio_need_iordy(adev))
		control |= 2;	/* IE enable */

	if (adev->class == ATA_DEV_ATA)
		control |= 4;	/* PPE enable */

	/* PIO configuration clears DTE unconditionally.  It will be
	 * programmed in set_dmamode which is guaranteed to be called
	 * after set_piomode if any DMA mode is available.
	 */
	pci_read_config_word(dev, master_port, &master_data);
	if (is_slave) {
		/* clear TIME1|IE1|PPE1|DTE1 */
		master_data &= 0xff0f;
		/* Enable SITRE (separate slave timing register) */
		master_data |= 0x4000;
		/* enable PPE1, IE1 and TIME1 as needed */
		master_data |= (control << 4);
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= (ap->port_no ? 0x0f : 0xf0);
		/* Load the timing nibble for this slave */
		slave_data |= ((timings[pio][0] << 2) | timings[pio][1])
						<< (ap->port_no ? 4 : 0);
	} else {
		/* clear ISP|RCT|TIME0|IE0|PPE0|DTE0 */
		master_data &= 0xccf0;
		/* Enable PPE, IE and TIME as appropriate */
		master_data |= control;
		/* load ISP and RCT */
		master_data |=
			(timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);

	/* Ensure the UDMA bit is off - it will be turned back on if
	   UDMA is selected */

	pci_read_config_byte(dev, 0x48, &udma_enable);
	udma_enable &= ~(1 << (2 * ap->port_no + adev->devno));
	pci_write_config_byte(dev, 0x48, udma_enable);
}

/**
 *	rdc_set_dmamode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Drive in question
 *
 *	Set UDMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void rdc_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	u8 master_port		= ap->port_no ? 0x42 : 0x40;
	u16 master_data;
	u8 speed		= adev->dma_mode;
	int devid		= adev->devno + 2 * ap->port_no;
	u8 udma_enable		= 0;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(dev, master_port, &master_data);
	pci_read_config_byte(dev, 0x48, &udma_enable);

	if (speed >= XFER_UDMA_0) {
		unsigned int udma = adev->dma_mode - XFER_UDMA_0;
		u16 udma_timing;
		u16 ideconf;
		int u_clock, u_speed;

		/*
		 * UDMA is handled by a combination of clock switching and
		 * selection of dividers
		 *
		 * Handy rule: Odd modes are UDMATIMx 01, even are 02
		 *	       except UDMA0 which is 00
		 */
		u_speed = min(2 - (udma & 1), udma);
		if (udma == 5)
			u_clock = 0x1000;	/* 100Mhz */
		else if (udma > 2)
			u_clock = 1;		/* 66Mhz */
		else
			u_clock = 0;		/* 33Mhz */

		udma_enable |= (1 << devid);

		/* Load the CT/RP selection */
		pci_read_config_word(dev, 0x4A, &udma_timing);
		udma_timing &= ~(3 << (4 * devid));
		udma_timing |= u_speed << (4 * devid);
		pci_write_config_word(dev, 0x4A, udma_timing);

		/* Select a 33/66/100Mhz clock */
		pci_read_config_word(dev, 0x54, &ideconf);
		ideconf &= ~(0x1001 << devid);
		ideconf |= u_clock << devid;
		pci_write_config_word(dev, 0x54, ideconf);
	} else {
		/*
		 * MWDMA is driven by the PIO timings. We must also enable
		 * IORDY unconditionally along with TIME1. PPE has already
		 * been set when the PIO timing was set.
		 */
		unsigned int mwdma	= adev->dma_mode - XFER_MW_DMA_0;
		unsigned int control;
		u8 slave_data;
		const unsigned int needed_pio[3] = {
			XFER_PIO_0, XFER_PIO_3, XFER_PIO_4
		};
		int pio = needed_pio[mwdma] - XFER_PIO_0;

		control = 3;	/* IORDY|TIME1 */

		/* If the drive MWDMA is faster than it can do PIO then
		   we must force PIO into PIO0 */

		if (adev->pio_mode < needed_pio[mwdma])
			/* Enable DMA timing only */
			control |= 8;	/* PIO cycles in PIO0 */

		if (adev->devno) {	/* Slave */
			master_data &= 0xFF4F;  /* Mask out IORDY|TIME1|DMAONLY */
			master_data |= control << 4;
			pci_read_config_byte(dev, 0x44, &slave_data);
			slave_data &= (ap->port_no ? 0x0f : 0xf0);
			/* Load the matching timing */
			slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) << (ap->port_no ? 4 : 0);
			pci_write_config_byte(dev, 0x44, slave_data);
		} else { 	/* Master */
			master_data &= 0xCCF4;	/* Mask out IORDY|TIME1|DMAONLY
						   and master timing bits */
			master_data |= control;
			master_data |=
				(timings[pio][0] << 12) |
				(timings[pio][1] << 8);
		}

		udma_enable &= ~(1 << devid);
		pci_write_config_word(dev, master_port, master_data);
	}
	pci_write_config_byte(dev, 0x48, udma_enable);
}

static struct ata_port_operations rdc_pata_ops = {
	.inherits		= &ata_bmdma32_port_ops,
	.cable_detect		= rdc_pata_cable_detect,
	.set_piomode		= rdc_set_piomode,
	.set_dmamode		= rdc_set_dmamode,
	.prereset		= rdc_pata_prereset,
};

static struct ata_port_info rdc_port_info = {

	.flags		= ATA_FLAG_SLAVE_POSS,
	.pio_mask	= ATA_PIO4,
	.mwdma_mask	= ATA_MWDMA12_ONLY,
	.udma_mask	= ATA_UDMA5,
	.port_ops	= &rdc_pata_ops,
};

static struct scsi_host_template rdc_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

/**
 *	rdc_init_one - Register PIIX ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in rdc_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.  We probe for combined mode (sigh),
 *	and then hand over control to libata, for it to do the rest.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int __devinit rdc_init_one(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	static int printed_version;
	struct device *dev = &pdev->dev;
	struct ata_port_info port_info[2];
	const struct ata_port_info *ppi[] = { &port_info[0], &port_info[1] };
	unsigned long port_flags;
	struct ata_host *host;
	struct rdc_host_priv *hpriv;
	int rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	port_info[0] = rdc_port_info;
	port_info[1] = rdc_port_info;

	port_flags = port_info[0].flags;

	/* enable device and prepare host */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	/* Save IOCFG, this will be used for cable detection, quirk
	 * detection and restoration on detach.
	 */
	pci_read_config_dword(pdev, 0x54, &hpriv->saved_iocfg);

	rc = ata_pci_bmdma_prepare_host(pdev, ppi, &host);
	if (rc)
		return rc;
	host->private_data = hpriv;

	pci_intx(pdev, 1);

	host->flags |= ATA_HOST_PARALLEL_SCAN;

	pci_set_master(pdev);
	return ata_pci_sff_activate_host(host, ata_bmdma_interrupt, &rdc_sht);
}

static void rdc_remove_one(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	struct rdc_host_priv *hpriv = host->private_data;

	pci_write_config_dword(pdev, 0x54, hpriv->saved_iocfg);

	ata_pci_remove_one(pdev);
}

static const struct pci_device_id rdc_pci_tbl[] = {
	{ PCI_DEVICE(0x17F3, 0x1011), },
	{ PCI_DEVICE(0x17F3, 0x1012), },
	{ }	/* terminate list */
};

static struct pci_driver rdc_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= rdc_pci_tbl,
	.probe			= rdc_init_one,
	.remove			= rdc_remove_one,
};


static int __init rdc_init(void)
{
	return pci_register_driver(&rdc_pci_driver);
}

static void __exit rdc_exit(void)
{
	pci_unregister_driver(&rdc_pci_driver);
}

module_init(rdc_init);
module_exit(rdc_exit);

MODULE_AUTHOR("Alan Cox (based on ata_piix)");
MODULE_DESCRIPTION("SCSI low-level driver for RDC PATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, rdc_pci_tbl);
MODULE_VERSION(DRV_VERSION);
