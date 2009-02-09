/*
 * Copyright (C) 2006		Red Hat <evan_ko@phison.com>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  [Modify History]
 *   #0001, Evan, 2008.10.22, V0.00, New release.
 *   #0002, Evan, 2008.11.01, V0.90, Test Work In Ubuntu Linux 8.04.
 *   #0003, Evan, 2008.01.08, V0.91, Change Name "PCIE-SSD" to "E-BOX".
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/ata.h>

#define PHISON_DEBUG

#define DRV_NAME	"PHISON E-BOX" //#0003
#define DRV_VERSION     "0.91" //#0003

#define PCI_VENDOR_ID_PHISON   0x1987
#define PCI_DEVICE_ID_PS5000   0x5000

int phison_pre_reset(struct ata_link *link, unsigned long deadline)
{
	int ret;
	struct ata_port *ap = link->ap;
	ap->cbl = ATA_CBL_NONE;
	ret = ata_std_prereset(link, deadline);
	#ifdef PHISON_DEBUG
	printk("****** phison_pre_reset(), ret = %x ******\n", ret);
	#endif
	return ret;
}

void phison_error_handler(struct ata_port *ap)
{
	#ifdef PHISON_DEBUG
	printk("****** phison_error_handler() ******\n");
	#endif
	return ata_bmdma_drive_eh(ap, phison_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

struct scsi_host_template phison_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand	= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize	= LIBATA_MAX_PRD,
	.cmd_per_lun	= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering	= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary	= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy	= ata_scsi_slave_destroy,
	/* Use standard CHS mapping rules */
	.bios_param		= ata_std_bios_param,
};

const struct ata_port_operations phison_ops = {
	/* Task file is PCI ATA format, use helpers */
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler	= phison_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,

	/* BMDMA handling is PCI ATA format, use helpers */
	.bmdma_setup	= ata_bmdma_setup,
	.bmdma_start	= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	/* IRQ-related hooks */
	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,

	/* Generic PATA PCI ATA helpers */
	.port_start		= ata_port_start,
};

int phison_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ata_port_info info = {
		.sht		= &phison_sht,
		.flags	= ATA_FLAG_NO_ATAPI,

		.pio_mask	= 0x1f,
		.mwdma_mask	= 0x07,
		.udma_mask 	= ATA_UDMA5,

		.port_ops	= &phison_ops,
	};
	int ret;

	const struct ata_port_info *ppi[] = { &info, NULL };

	ret = ata_pci_init_one(pdev, ppi);

	#ifdef PHISON_DEBUG
	printk("****** phison_init_one(), ret = %x ******\n", ret);
	#endif

	return ret;

}

struct pci_device_id phison_pci_tbl[] = {
	{ PCI_VENDOR_ID_PHISON, PCI_DEVICE_ID_PS5000, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_STORAGE_IDE << 8, 0xffff00, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, phison_pci_tbl);

struct pci_driver phison_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= phison_pci_tbl,
	.probe		= phison_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM //haven't test it.
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

int phison_ide_init(void)
{
#if 0 //For Test.
	struct pci_dev *pci_dev = NULL;
	struct pci_dev *ps5k_dev = NULL;
	int i, ret;
	u16 vid, pid;
	u32 addr1,addr2,addr3, addr4,addr5,addr6;

	printk("****** phison_ide_init ******\n");

	i = 0;

	while(1)
	{
		pci_dev = pci_find_device(PCI_ANY_ID,PCI_ANY_ID, pci_dev);

		if(pci_dev!=NULL)
		{
			pci_read_config_word(pci_dev, 0, &pid);
			pci_read_config_word(pci_dev, 2, &vid);
			pci_read_config_dword(pci_dev, 16, &addr1);
			pci_read_config_dword(pci_dev, 16, &addr1);
			pci_read_config_dword(pci_dev, 16, &addr1);
			pci_read_config_dword(pci_dev, 16, &addr1);
			pci_read_config_dword(pci_dev, 20, &addr2);
			pci_read_config_dword(pci_dev, 24, &addr3);
			pci_read_config_dword(pci_dev, 28, &addr4);
			pci_read_config_dword(pci_dev, 32, &addr5);
			pci_read_config_dword(pci_dev, 36, &addr6);

			printk("****** <0x%02x>, %x, %x, %x,%x,%x,%x,%x,%x ******\n", i, pid, vid, addr1, addr2, addr3, addr4,addr5,addr6);
			i++;

			if((pid==PCI_VENDOR_ID_PHISON)&&(vid==PCI_DEVICE_ID_PS5000)) ps5k_dev = pci_dev;
		}
		else
		{
			if(i==0) printk("****** no pci device found ******\n");
			break;
		}
	}

	if(ps5k_dev!=NULL)
	{
		ret = pci_register_driver(&phison_pci_driver);
		printk("****** PS5000 found, Ret = %x ******\n", ret);
		return true;
	}

	printk("****** PS5000 not found ******\n");
	return false;
#else
	int ret;

	ret = pci_register_driver(&phison_pci_driver);

	#ifdef PHISON_DEBUG
	printk("****** phison_ide_init(), ret = %x ******\n", ret);
	#endif

	return ret;
#endif


}

void phison_ide_exit(void)
{
	#ifdef PHISON_DEBUG
	printk("****** phison_ide_exit() ******\n");
	#endif
	pci_unregister_driver(&phison_pci_driver);
}

module_init(phison_ide_init);
module_exit(phison_ide_exit);

MODULE_AUTHOR("Evan Ko");
MODULE_DESCRIPTION("PCIE driver module for PHISON PS5000 E-BOX");//#0003
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
