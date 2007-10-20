/*
 * pata-cs5535.c 	- CS5535 PATA for new ATA layer
 *			  (C) 2005-2006 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 * based upon cs5535.c from AMD <Jens.Altmann@amd.com> as cleaned up and
 * made readable and Linux style by Wolfgang Zuleger <wolfgang.zuleger@gmx.de
 * and Alexander Kiausch <alex.kiausch@t-online.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Loosely based on the piix & svwks drivers.
 *
 * Documentation:
 *	Available from AMD web site.
 * TODO
 *	Review errata to see if serializing is necessary
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <asm/msr.h>

#define DRV_NAME	"cs5535"
#define DRV_VERSION	"0.2.12"

/*
 *	The Geode (Aka Athlon GX now) uses an internal MSR based
 *	bus system for control. Demented but there you go.
 */

#define MSR_ATAC_BASE    	0x51300000
#define ATAC_GLD_MSR_CAP 	(MSR_ATAC_BASE+0)
#define ATAC_GLD_MSR_CONFIG    (MSR_ATAC_BASE+0x01)
#define ATAC_GLD_MSR_SMI       (MSR_ATAC_BASE+0x02)
#define ATAC_GLD_MSR_ERROR     (MSR_ATAC_BASE+0x03)
#define ATAC_GLD_MSR_PM        (MSR_ATAC_BASE+0x04)
#define ATAC_GLD_MSR_DIAG      (MSR_ATAC_BASE+0x05)
#define ATAC_IO_BAR            (MSR_ATAC_BASE+0x08)
#define ATAC_RESET             (MSR_ATAC_BASE+0x10)
#define ATAC_CH0D0_PIO         (MSR_ATAC_BASE+0x20)
#define ATAC_CH0D0_DMA         (MSR_ATAC_BASE+0x21)
#define ATAC_CH0D1_PIO         (MSR_ATAC_BASE+0x22)
#define ATAC_CH0D1_DMA         (MSR_ATAC_BASE+0x23)
#define ATAC_PCI_ABRTERR       (MSR_ATAC_BASE+0x24)

#define ATAC_BM0_CMD_PRIM      0x00
#define ATAC_BM0_STS_PRIM      0x02
#define ATAC_BM0_PRD           0x04

#define CS5535_CABLE_DETECT    0x48

#define CS5535_BAD_PIO(timings) ( (timings&~0x80000000UL)==0x00009172 )

/**
 *	cs5535_cable_detect	-	detect cable type
 *	@ap: Port to detect on
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform cable detection for ATA66 capable cable. Return a libata
 *	cable type.
 */

static int cs5535_cable_detect(struct ata_port *ap)
{
	u8 cable;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	pci_read_config_byte(pdev, CS5535_CABLE_DETECT, &cable);
	if (cable & 1)
		return ATA_CBL_PATA80;
	else
		return ATA_CBL_PATA40;
}

/**
 *	cs5535_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Set our PIO requirements. The CS5535 is pretty clean about all this
 */

static void cs5535_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	static const u16 pio_timings[5] = {
		0xF7F4, 0x53F3, 0x13F1, 0x5131, 0x1131
	};
	static const u16 pio_cmd_timings[5] = {
		0xF7F4, 0x53F3, 0x13F1, 0x5131, 0x1131
	};
	u32 reg, dummy;
	struct ata_device *pair = ata_dev_pair(adev);

	int mode = adev->pio_mode - XFER_PIO_0;
	int cmdmode = mode;

	/* Command timing has to be for the lowest of the pair of devices */
	if (pair) {
		int pairmode = pair->pio_mode - XFER_PIO_0;
		cmdmode = min(mode, pairmode);
		/* Write the other drive timing register if it changed */
		if (cmdmode < pairmode)
			wrmsr(ATAC_CH0D0_PIO + 2 * pair->devno,
				pio_cmd_timings[cmdmode] << 16 | pio_timings[pairmode], 0);
	}
	/* Write the drive timing register */
	wrmsr(ATAC_CH0D0_PIO + 2 * adev->devno,
		pio_cmd_timings[cmdmode] << 16 | pio_timings[mode], 0);

	/* Set the PIO "format 1" bit in the DMA timing register */
	rdmsr(ATAC_CH0D0_DMA + 2 * adev->devno, reg, dummy);
	wrmsr(ATAC_CH0D0_DMA + 2 * adev->devno, reg | 0x80000000UL, 0);
}

/**
 *	cs5535_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 */

static void cs5535_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static const u32 udma_timings[5] = {
		0x7F7436A1, 0x7F733481, 0x7F723261, 0x7F713161, 0x7F703061
	};
	static const u32 mwdma_timings[3] = {
		0x7F0FFFF3, 0x7F035352, 0x7F024241
	};
	u32 reg, dummy;
	int mode = adev->dma_mode;

	rdmsr(ATAC_CH0D0_DMA + 2 * adev->devno, reg, dummy);
	reg &= 0x80000000UL;
	if (mode >= XFER_UDMA_0)
		reg |= udma_timings[mode - XFER_UDMA_0];
	else
		reg |= mwdma_timings[mode - XFER_MW_DMA_0];
	wrmsr(ATAC_CH0D0_DMA + 2 * adev->devno, reg, 0);
}

static struct scsi_host_template cs5535_sht = {
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

static struct ata_port_operations cs5535_port_ops = {
	.set_piomode	= cs5535_set_piomode,
	.set_dmamode	= cs5535_set_dmamode,
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
	.cable_detect	= cs5535_cable_detect,

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

	.port_start	= ata_sff_port_start,
};

/**
 *	cs5535_init_one		-	Initialise a CS5530
 *	@dev: PCI device
 *	@id: Entry in match table
 *
 *	Install a driver for the newly found CS5530 companion chip. Most of
 *	this is just housekeeping. We have to set the chip up correctly and
 *	turn off various bits of emulation magic.
 */

static int cs5535_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.sht = &cs5535_sht,
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = ATA_UDMA4,
		.port_ops = &cs5535_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, &ata_dummy_port_info };

	u32 timings, dummy;

	/* Check the BIOS set the initial timing clock. If not set the
	   timings for PIO0 */
	rdmsr(ATAC_CH0D0_PIO, timings, dummy);
	if (CS5535_BAD_PIO(timings))
		wrmsr(ATAC_CH0D0_PIO, 0xF7F4F7F4UL, 0);
	rdmsr(ATAC_CH0D1_PIO, timings, dummy);
	if (CS5535_BAD_PIO(timings))
		wrmsr(ATAC_CH0D1_PIO, 0xF7F4F7F4UL, 0);
	return ata_pci_init_one(dev, ppi);
}

static const struct pci_device_id cs5535[] = {
	{ PCI_VDEVICE(NS, 0x002D), },

	{ },
};

static struct pci_driver cs5535_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= cs5535,
	.probe 		= cs5535_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

static int __init cs5535_init(void)
{
	return pci_register_driver(&cs5535_pci_driver);
}

static void __exit cs5535_exit(void)
{
	pci_unregister_driver(&cs5535_pci_driver);
}

MODULE_AUTHOR("Alan Cox, Jens Altmann, Wolfgan Zuleger, Alexander Kiausch");
MODULE_DESCRIPTION("low-level driver for the NS/AMD 5530");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, cs5535);
MODULE_VERSION(DRV_VERSION);

module_init(cs5535_init);
module_exit(cs5535_exit);
