// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pata_atp867x.c - ARTOP 867X 64bit 4-channel UDMA133 ATA controller driver
 *
 *	(C) 2009 Google Inc. John(Jung-Ik) Lee <jilee@google.com>
 *
 * Per Atp867 data sheet rev 1.2, Acard.
 * Based in part on early ide code from
 *	2003-2004 by Eric Uhrhane, Google, Inc.
 *
 * TODO:
 *   1. RAID features [comparison, XOR, striping, mirroring, etc.]
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define	DRV_NAME	"pata_atp867x"
#define	DRV_VERSION	"0.7.5"

/*
 * IO Registers
 * Note that all runtime hot priv ports are cached in ap private_data
 */

enum {
	ATP867X_IO_CHANNEL_OFFSET	= 0x10,

	/*
	 * IO Register Bitfields
	 */

	ATP867X_IO_PIOSPD_ACTIVE_SHIFT	= 4,
	ATP867X_IO_PIOSPD_RECOVER_SHIFT	= 0,

	ATP867X_IO_DMAMODE_MSTR_SHIFT	= 0,
	ATP867X_IO_DMAMODE_MSTR_MASK	= 0x07,
	ATP867X_IO_DMAMODE_SLAVE_SHIFT	= 4,
	ATP867X_IO_DMAMODE_SLAVE_MASK	= 0x70,

	ATP867X_IO_DMAMODE_UDMA_6	= 0x07,
	ATP867X_IO_DMAMODE_UDMA_5	= 0x06,
	ATP867X_IO_DMAMODE_UDMA_4	= 0x05,
	ATP867X_IO_DMAMODE_UDMA_3	= 0x04,
	ATP867X_IO_DMAMODE_UDMA_2	= 0x03,
	ATP867X_IO_DMAMODE_UDMA_1	= 0x02,
	ATP867X_IO_DMAMODE_UDMA_0	= 0x01,
	ATP867X_IO_DMAMODE_DISABLE	= 0x00,

	ATP867X_IO_SYS_INFO_66MHZ	= 0x04,
	ATP867X_IO_SYS_INFO_SLOW_UDMA5	= 0x02,
	ATP867X_IO_SYS_MASK_RESERVED	= (~0xf1),

	ATP867X_IO_PORTSPD_VAL		= 0x1143,
	ATP867X_PREREAD_VAL		= 0x0200,

	ATP867X_NUM_PORTS		= 4,
	ATP867X_BAR_IOBASE		= 0,
	ATP867X_BAR_ROMBASE		= 6,
};

#define ATP867X_IOBASE(ap)		((ap)->host->iomap[0])
#define ATP867X_SYS_INFO(ap)		(0x3F + ATP867X_IOBASE(ap))

#define ATP867X_IO_PORTBASE(ap, port)	(0x00 + ATP867X_IOBASE(ap) + \
					(port) * ATP867X_IO_CHANNEL_OFFSET)
#define ATP867X_IO_DMABASE(ap, port)	(0x40 + \
					ATP867X_IO_PORTBASE((ap), (port)))

#define ATP867X_IO_STATUS(ap, port)	(0x07 + \
					ATP867X_IO_PORTBASE((ap), (port)))
#define ATP867X_IO_ALTSTATUS(ap, port)	(0x0E + \
					ATP867X_IO_PORTBASE((ap), (port)))

/*
 * hot priv ports
 */
#define ATP867X_IO_MSTRPIOSPD(ap, port)	(0x08 + \
					ATP867X_IO_DMABASE((ap), (port)))
#define ATP867X_IO_SLAVPIOSPD(ap, port)	(0x09 + \
					ATP867X_IO_DMABASE((ap), (port)))
#define ATP867X_IO_8BPIOSPD(ap, port)	(0x0A + \
					ATP867X_IO_DMABASE((ap), (port)))
#define ATP867X_IO_DMAMODE(ap, port)	(0x0B + \
					ATP867X_IO_DMABASE((ap), (port)))

#define ATP867X_IO_PORTSPD(ap, port)	(0x4A + \
					ATP867X_IO_PORTBASE((ap), (port)))
#define ATP867X_IO_PREREAD(ap, port)	(0x4C + \
					ATP867X_IO_PORTBASE((ap), (port)))

struct atp867x_priv {
	void __iomem *dma_mode;
	void __iomem *mstr_piospd;
	void __iomem *slave_piospd;
	void __iomem *eightb_piospd;
	int		pci66mhz;
};

static void atp867x_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev	= to_pci_dev(ap->host->dev);
	struct atp867x_priv *dp = ap->private_data;
	u8 speed = adev->dma_mode;
	u8 b;
	u8 mode = speed - XFER_UDMA_0 + 1;

	/*
	 * Doc 6.6.9: decrease the udma mode value by 1 for safer UDMA speed
	 * on 66MHz bus
	 *   rev-A: UDMA_1~4 (5, 6 no change)
	 *   rev-B: all UDMA modes
	 *   UDMA_0 stays not to disable UDMA
	 */
	if (dp->pci66mhz && mode > ATP867X_IO_DMAMODE_UDMA_0  &&
	   (pdev->device == PCI_DEVICE_ID_ARTOP_ATP867B ||
	    mode < ATP867X_IO_DMAMODE_UDMA_5))
		mode--;

	b = ioread8(dp->dma_mode);
	if (adev->devno & 1) {
		b = (b & ~ATP867X_IO_DMAMODE_SLAVE_MASK) |
			(mode << ATP867X_IO_DMAMODE_SLAVE_SHIFT);
	} else {
		b = (b & ~ATP867X_IO_DMAMODE_MSTR_MASK) |
			(mode << ATP867X_IO_DMAMODE_MSTR_SHIFT);
	}
	iowrite8(b, dp->dma_mode);
}

static int atp867x_get_active_clocks_shifted(struct ata_port *ap,
	unsigned int clk)
{
	struct atp867x_priv *dp = ap->private_data;
	unsigned char clocks = clk;

	/*
	 * Doc 6.6.9: increase the clock value by 1 for safer PIO speed
	 * on 66MHz bus
	 */
	if (dp->pci66mhz)
		clocks++;

	switch (clocks) {
	case 0:
		clocks = 1;
		break;
	case 1 ... 6:
		break;
	default:
		ata_port_warn(ap, "ATP867X: active %dclk is invalid. "
			"Using 12clk.\n", clk);
		fallthrough;
	case 9 ... 12:
		clocks = 7;	/* 12 clk */
		break;
	case 7:
	case 8:	/* default 8 clk */
		clocks = 0;
		goto active_clock_shift_done;
	}

active_clock_shift_done:
	return clocks << ATP867X_IO_PIOSPD_ACTIVE_SHIFT;
}

static int atp867x_get_recover_clocks_shifted(struct ata_port *ap,
					      unsigned int clk)
{
	unsigned char clocks = clk;

	switch (clocks) {
	case 0:
		clocks = 1;
		break;
	case 1 ... 11:
		break;
	case 13:
	case 14:
		--clocks;	/* by the spec */
		break;
	case 15:
		break;
	default:
		ata_port_warn(ap, "ATP867X: recover %dclk is invalid. "
			"Using default 12clk.\n", clk);
		fallthrough;
	case 12:	/* default 12 clk */
		clocks = 0;
		break;
	}

	return clocks << ATP867X_IO_PIOSPD_RECOVER_SHIFT;
}

static void atp867x_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct ata_device *peer = ata_dev_pair(adev);
	struct atp867x_priv *dp = ap->private_data;
	u8 speed = adev->pio_mode;
	struct ata_timing t, p;
	int T, UT;
	u8 b;

	T = 1000000000 / 33333;
	UT = T / 4;

	ata_timing_compute(adev, speed, &t, T, UT);
	if (peer && peer->pio_mode) {
		ata_timing_compute(peer, peer->pio_mode, &p, T, UT);
		ata_timing_merge(&p, &t, &t, ATA_TIMING_8BIT);
	}

	b = ioread8(dp->dma_mode);
	if (adev->devno & 1)
		b = (b & ~ATP867X_IO_DMAMODE_SLAVE_MASK);
	else
		b = (b & ~ATP867X_IO_DMAMODE_MSTR_MASK);
	iowrite8(b, dp->dma_mode);

	b = atp867x_get_active_clocks_shifted(ap, t.active) |
		atp867x_get_recover_clocks_shifted(ap, t.recover);

	if (adev->devno & 1)
		iowrite8(b, dp->slave_piospd);
	else
		iowrite8(b, dp->mstr_piospd);

	b = atp867x_get_active_clocks_shifted(ap, t.act8b) |
		atp867x_get_recover_clocks_shifted(ap, t.rec8b);

	iowrite8(b, dp->eightb_piospd);
}

static int atp867x_cable_override(struct pci_dev *pdev)
{
	if (pdev->subsystem_vendor == PCI_VENDOR_ID_ARTOP &&
		(pdev->subsystem_device == PCI_DEVICE_ID_ARTOP_ATP867A ||
		 pdev->subsystem_device == PCI_DEVICE_ID_ARTOP_ATP867B)) {
		return 1;
	}
	return 0;
}

static int atp867x_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (atp867x_cable_override(pdev))
		return ATA_CBL_PATA40_SHORT;

	return ATA_CBL_PATA_UNK;
}

static const struct scsi_host_template atp867x_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations atp867x_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.cable_detect		= atp867x_cable_detect,
	.set_piomode		= atp867x_set_piomode,
	.set_dmamode		= atp867x_set_dmamode,
};


static void atp867x_check_res(struct pci_dev *pdev)
{
	int i;
	unsigned long start, len;

	/* Check the PCI resources for this channel are enabled */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		start = pci_resource_start(pdev, i);
		len   = pci_resource_len(pdev, i);
		dev_dbg(&pdev->dev, "ATP867X: resource start:len=%lx:%lx\n",
			start, len);
	}
}

static void atp867x_check_ports(struct ata_port *ap, int port)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	struct atp867x_priv *dp = ap->private_data;

	ata_port_dbg(ap, "ATP867X: port[%d] addresses\n"
		"  cmd_addr	=0x%lx, 0x%lx\n"
		"  ctl_addr	=0x%lx, 0x%lx\n"
		"  bmdma_addr	=0x%lx, 0x%lx\n"
		"  data_addr	=0x%lx\n"
		"  error_addr	=0x%lx\n"
		"  feature_addr	=0x%lx\n"
		"  nsect_addr	=0x%lx\n"
		"  lbal_addr	=0x%lx\n"
		"  lbam_addr	=0x%lx\n"
		"  lbah_addr	=0x%lx\n"
		"  device_addr	=0x%lx\n"
		"  status_addr	=0x%lx\n"
		"  command_addr	=0x%lx\n"
		"  dp->dma_mode	=0x%lx\n"
		"  dp->mstr_piospd	=0x%lx\n"
		"  dp->slave_piospd	=0x%lx\n"
		"  dp->eightb_piospd	=0x%lx\n"
		"  dp->pci66mhz		=0x%lx\n",
		port,
		(unsigned long)ioaddr->cmd_addr,
		(unsigned long)ATP867X_IO_PORTBASE(ap, port),
		(unsigned long)ioaddr->ctl_addr,
		(unsigned long)ATP867X_IO_ALTSTATUS(ap, port),
		(unsigned long)ioaddr->bmdma_addr,
		(unsigned long)ATP867X_IO_DMABASE(ap, port),
		(unsigned long)ioaddr->data_addr,
		(unsigned long)ioaddr->error_addr,
		(unsigned long)ioaddr->feature_addr,
		(unsigned long)ioaddr->nsect_addr,
		(unsigned long)ioaddr->lbal_addr,
		(unsigned long)ioaddr->lbam_addr,
		(unsigned long)ioaddr->lbah_addr,
		(unsigned long)ioaddr->device_addr,
		(unsigned long)ioaddr->status_addr,
		(unsigned long)ioaddr->command_addr,
		(unsigned long)dp->dma_mode,
		(unsigned long)dp->mstr_piospd,
		(unsigned long)dp->slave_piospd,
		(unsigned long)dp->eightb_piospd,
		(unsigned long)dp->pci66mhz);
}

static int atp867x_set_priv(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct atp867x_priv *dp;
	int port = ap->port_no;

	dp = ap->private_data =
		devm_kzalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (dp == NULL)
		return -ENOMEM;

	dp->dma_mode	 = ATP867X_IO_DMAMODE(ap, port);
	dp->mstr_piospd	 = ATP867X_IO_MSTRPIOSPD(ap, port);
	dp->slave_piospd = ATP867X_IO_SLAVPIOSPD(ap, port);
	dp->eightb_piospd = ATP867X_IO_8BPIOSPD(ap, port);

	dp->pci66mhz =
		ioread8(ATP867X_SYS_INFO(ap)) & ATP867X_IO_SYS_INFO_66MHZ;

	return 0;
}

static void atp867x_fixup(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	struct ata_port *ap = host->ports[0];
	int i;
	u8 v;

	/*
	 * Broken BIOS might not set latency high enough
	 */
	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &v);
	if (v < 0x80) {
		v = 0x80;
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, v);
		dev_dbg(&pdev->dev, "ATP867X: set latency timer to %d\n", v);
	}

	/*
	 * init 8bit io ports speed(0aaarrrr) to 43h and
	 * init udma modes of master/slave to 0/0(11h)
	 */
	for (i = 0; i < ATP867X_NUM_PORTS; i++)
		iowrite16(ATP867X_IO_PORTSPD_VAL, ATP867X_IO_PORTSPD(ap, i));

	/*
	 * init PreREAD counts
	 */
	for (i = 0; i < ATP867X_NUM_PORTS; i++)
		iowrite16(ATP867X_PREREAD_VAL, ATP867X_IO_PREREAD(ap, i));

	v = ioread8(ATP867X_IOBASE(ap) + 0x28);
	v &= 0xcf;	/* Enable INTA#: bit4=0 means enable */
	v |= 0xc0;	/* Enable PCI burst, MRM & not immediate interrupts */
	iowrite8(v, ATP867X_IOBASE(ap) + 0x28);

	/*
	 * Turn off the over clocked udma5 mode, only for Rev-B
	 */
	v = ioread8(ATP867X_SYS_INFO(ap));
	v &= ATP867X_IO_SYS_MASK_RESERVED;
	if (pdev->device == PCI_DEVICE_ID_ARTOP_ATP867B)
		v |= ATP867X_IO_SYS_INFO_SLOW_UDMA5;
	iowrite8(v, ATP867X_SYS_INFO(ap));
}

static int atp867x_ata_pci_sff_init_host(struct ata_host *host)
{
	struct device *gdev = host->dev;
	struct pci_dev *pdev = to_pci_dev(gdev);
	unsigned int mask = 0;
	int i, rc;

	/*
	 * do not map rombase
	 */
	rc = pcim_iomap_regions(pdev, 1 << ATP867X_BAR_IOBASE, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);

	atp867x_check_res(pdev);

	for (i = 0; i < PCI_STD_NUM_BARS; i++)
		dev_dbg(gdev, "ATP867X: iomap[%d]=0x%p\n", i,
			host->iomap[i]);

	/*
	 * request, iomap BARs and init port addresses accordingly
	 */
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		struct ata_ioports *ioaddr = &ap->ioaddr;

		ioaddr->cmd_addr = ATP867X_IO_PORTBASE(ap, i);
		ioaddr->ctl_addr = ioaddr->altstatus_addr
				 = ATP867X_IO_ALTSTATUS(ap, i);
		ioaddr->bmdma_addr = ATP867X_IO_DMABASE(ap, i);

		ata_sff_std_ports(ioaddr);
		rc = atp867x_set_priv(ap);
		if (rc)
			return rc;

		atp867x_check_ports(ap, i);

		ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx",
			(unsigned long)ioaddr->cmd_addr,
			(unsigned long)ioaddr->ctl_addr);
		ata_port_desc(ap, "bmdma 0x%lx",
			(unsigned long)ioaddr->bmdma_addr);

		mask |= 1 << i;
	}

	if (!mask) {
		dev_err(gdev, "no available native port\n");
		return -ENODEV;
	}

	atp867x_fixup(host);

	return dma_set_mask_and_coherent(&pdev->dev, ATA_DMA_MASK);
}

static int atp867x_init_one(struct pci_dev *pdev,
	const struct pci_device_id *id)
{
	static const struct ata_port_info info_867x = {
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= ATA_PIO4,
		.udma_mask 	= ATA_UDMA6,
		.port_ops	= &atp867x_ops,
	};

	struct ata_host *host;
	const struct ata_port_info *ppi[] = { &info_867x, NULL };
	int rc;

	ata_print_version_once(&pdev->dev, DRV_VERSION);

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	dev_info(&pdev->dev, "ATP867X: ATP867 ATA UDMA133 controller (rev %02X)",
		pdev->device);

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, ATP867X_NUM_PORTS);
	if (!host) {
		dev_err(&pdev->dev, "failed to allocate ATA host\n");
		rc = -ENOMEM;
		goto err_out;
	}

	rc = atp867x_ata_pci_sff_init_host(host);
	if (rc) {
		dev_err(&pdev->dev, "failed to init host\n");
		goto err_out;
	}

	pci_set_master(pdev);

	rc = ata_host_activate(host, pdev->irq, ata_bmdma_interrupt,
				IRQF_SHARED, &atp867x_sht);
	if (rc)
		dev_err(&pdev->dev, "failed to activate host\n");

err_out:
	return rc;
}

#ifdef CONFIG_PM_SLEEP
static int atp867x_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = pci_get_drvdata(pdev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	atp867x_fixup(host);

	ata_host_resume(host);
	return 0;
}
#endif

static struct pci_device_id atp867x_pci_tbl[] = {
	{ PCI_VDEVICE(ARTOP, PCI_DEVICE_ID_ARTOP_ATP867A),	0 },
	{ PCI_VDEVICE(ARTOP, PCI_DEVICE_ID_ARTOP_ATP867B),	0 },
	{ },
};

static struct pci_driver atp867x_driver = {
	.name 		= DRV_NAME,
	.id_table 	= atp867x_pci_tbl,
	.probe 		= atp867x_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM_SLEEP
	.suspend	= ata_pci_device_suspend,
	.resume		= atp867x_reinit_one,
#endif
};

module_pci_driver(atp867x_driver);

MODULE_AUTHOR("John(Jung-Ik) Lee, Google Inc.");
MODULE_DESCRIPTION("low level driver for Artop/Acard 867x ATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, atp867x_pci_tbl);
MODULE_VERSION(DRV_VERSION);
