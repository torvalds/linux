/*
 * pata_it8172.c 	- IT8172 PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 * Based heavily on
 *
 * BRIEF MODULE DESCRIPTION
 *      IT8172 IDE controller support
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	TODO
 *	Check for errata
 *	See if we really need to force native mode
 *	PIO timings (also lacking in original)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_it8172"
#define DRV_VERSION "0.3.1"

static int it8172_pre_reset(struct ata_port *ap)
{
	static const struct pci_bits it8172_enable_bits[] = {
		{ 0x00, 0, 0x00, 0x00 },
		{ 0x40, 1, 0x00, 0x01 }
	};

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	
	if (ap->port_no && !pci_test_config_bits(pdev, &it8172_enable_bits[ap->port_no])) {
		ata_port_disable(ap);
		printk(KERN_INFO "ata%u: port disabled. ignoring.\n", ap->id);
		return 0;
	}
	ap->cbl = ATA_CBL_PATA40;
	return ata_std_prereset(ap);
}

static void it8172_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, it8172_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	it8172_set_pio_timing	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called by both the pio and dma setup functions to set the controller
 *	timings for PIO transfers. We must load both the mode number and
 *	timing values into the controller.
 */

static void it8172_set_pio_timing(struct ata_port *ap, struct ata_device *adev, int pio)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u16 reg40;

	pci_read_config_word(pdev, 0x40, &reg40);

	/*
	 * FIX! The DIOR/DIOW pulse width and recovery times in port 0x44
	 * are being left at the default values of 8 PCI clocks (242 nsec
	 * for a 33 MHz clock). These can be safely shortened at higher
	 * PIO modes. The DIOR/DIOW pulse width and recovery times only
	 * apply to PIO modes, not to the DMA modes.
	 */

	/*
	 * Enable port 0x44. The IT8172G spec is confused; it calls
	 * this register the "Slave IDE Timing Register", but in fact,
	 * it controls timing for both master and slave drives.
	 */

	reg40 |= 0x4000;
	if (adev->devno) {
		reg40 &= 0xC006;
		if (pio > 1)
			/* Enable prefetch and IORDY sample-point */
			reg40 |= 0x0060;
	} else {
		reg40 &= 0xC060;
		if (pio > 1)
			/* Enable prefetch and IORDY sample-point */
			reg40 |= 0x0006;
	}
	/* Write back the enables */
	pci_write_config_word(pdev, 0x40, reg40);
}

/**
 *	it8172_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup. We use a shared helper for this
 *	as the DMA setup must also adjust the PIO timing information.
 */

static void it8172_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	it8172_set_pio_timing(ap, adev, adev->pio_mode - XFER_PIO_0);
}

/**
 *	it8172_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the DMA mode setup. We must tune an appropriate PIO
 *	mode to match.
 */

static void it8172_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int dn = (2 * ap->port_no) + adev->devno;
	u8 reg48, reg4a;
	int pio;

	static const int pio_map[] = { 1, 3, 4};
	/*
	 * Setting the DMA cycle time to 2 or 3 PCI clocks (60 and 91 nsec
	 * at 33 MHz PCI clock) seems to cause BadCRC errors during DMA
	 * transfers on some drives, even though both numbers meet the minimum
	 * ATAPI-4 spec of 73 and 54 nsec for UDMA 1 and 2 respectively.
	 * So the faster times are just commented out here. The good news is
	 * that the slower cycle time has very little affect on transfer
	 * performance.
	 */

	pci_read_config_byte(pdev, 0x48, &reg48);
	pci_read_config_byte(pdev, 0x4A, &reg4a);

 	reg4a &= ~(3 << (4 * dn));

	if (adev->dma_mode >= XFER_UDMA_0) {
		reg48 |= 1 << dn;
#ifdef UDMA_TIMING_SET
		reg4a |= ((adev->dma_mode - XFER_UDMA_0) << (4 * dn));
#endif
	 	pio = 4;
	} else  {
		pio = pio_map[adev->dma_mode - XFER_MW_DMA_0];
		reg48 &= ~ (1 << dn);
	}
	pci_write_config_byte(pdev, 0x48, reg48);
	pci_write_config_byte(pdev, 0x4A, reg4a);
 	it8172_set_pio_timing(ap, adev, pio);

}

static struct scsi_host_template it8172_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations it8172_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= it8172_set_piomode,
	.set_dmamode	= it8172_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= it8172_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,
	.eng_timeout	= ata_eng_timeout,
	.data_xfer	= ata_pio_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,

	.port_start	= ata_port_start,
	.port_stop	= ata_port_stop,
	.host_stop	= ata_host_stop
};

static int it8172_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static struct ata_port_info info = {
		.sht = &it8172_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x06,	/* No MWDMA0 support */
		.udma_mask = 0x7,
		.port_ops = &it8172_port_ops
	};
	static struct ata_port_info *port_info[2] = { &info, &info };

        if ((!(PCI_FUNC(dev->devfn) & 1) ||
            (!((dev->class >> 8) == PCI_CLASS_STORAGE_IDE))))
		return -ENODEV; /* IT8172 is more than an IDE controller */

	return ata_pci_init_one(dev, port_info, 2);
}

static struct pci_device_id it8172[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ITE, PCI_DEVICE_ID_ITE_IT8172G), },
	{ 0, },
};

static struct pci_driver it8172_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= it8172,
	.probe 		= it8172_init_one,
	.remove		= ata_pci_remove_one
};

static int __init it8172_init(void)
{
	return pci_register_driver(&it8172_pci_driver);
}


static void __exit it8172_exit(void)
{
	pci_unregister_driver(&it8172_pci_driver);
}


MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for ITE IT8172");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, it8172);
MODULE_VERSION(DRV_VERSION);

module_init(it8172_init);
module_exit(it8172_exit);
