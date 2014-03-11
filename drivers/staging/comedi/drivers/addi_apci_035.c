#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#define ADDIDATA_WATCHDOG 2	/*  Or shold it be something else */

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci035.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci035_boardtypes[] = {
	{
		.pc_DriverName		= "apci035",
		.i_IorangeBase1		= APCI035_ADDRESS_RANGE,
		.i_PCIEeprom		= 1,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xff,
		.pr_AiRangelist		= &range_apci035_ai,
		.i_Timer		= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= apci035_interrupt,
		.reset			= apci035_reset,
		.ai_config		= apci035_ai_config,
		.ai_read		= apci035_ai_read,
		.timer_config		= apci035_timer_config,
		.timer_write		= apci035_timer_write,
		.timer_read		= apci035_timer_read,
	},
};

static int apci035_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	dev->board_ptr = &apci035_boardtypes[0];

	return addi_auto_attach(dev, context);
}

static struct comedi_driver apci035_driver = {
	.driver_name	= "addi_apci_035",
	.module		= THIS_MODULE,
	.auto_attach	= apci035_auto_attach,
	.detach		= i_ADDI_Detach,
};

static int apci035_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci035_driver, id->driver_data);
}

static const struct pci_device_id apci035_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA,  0x0300) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci035_pci_table);

static struct pci_driver apci035_pci_driver = {
	.name		= "addi_apci_035",
	.id_table	= apci035_pci_table,
	.probe		= apci035_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci035_driver, apci035_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
