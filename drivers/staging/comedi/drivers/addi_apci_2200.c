#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci2200.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci2200_boardtypes[] = {
	{
		.pc_DriverName		= "apci2200",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1005,
		.i_IorangeBase0		= 4,
		.i_IorangeBase1		= APCI2200_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDiChannel		= 8,
		.i_NbrDoChannel		= 16,
		.i_Timer		= 1,
		.reset			= i_APCI2200_Reset,
		.di_read		= i_APCI2200_Read1DigitalInput,
		.di_bits		= i_APCI2200_ReadMoreDigitalInput,
		.do_config		= i_APCI2200_ConfigDigitalOutput,
		.do_write		= i_APCI2200_WriteDigitalOutput,
		.do_bits		= i_APCI2200_ReadDigitalOutput,
		.timer_config		= i_APCI2200_ConfigWatchdog,
		.timer_write		= i_APCI2200_StartStopWriteWatchdog,
		.timer_read		= i_APCI2200_ReadWatchdog,
	},
};

static struct comedi_driver apci2200_driver = {
	.driver_name	= "addi_apci_2200",
	.module		= THIS_MODULE,
	.attach_pci	= addi_attach_pci,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(apci2200_boardtypes),
	.board_name	= &apci2200_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int __devinit apci2200_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci2200_driver);
}

static void __devexit apci2200_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci2200_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1005) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci2200_pci_table);

static struct pci_driver apci2200_pci_driver = {
	.name		= "addi_apci_2200",
	.id_table	= apci2200_pci_table,
	.probe		= apci2200_pci_probe,
	.remove		= __devexit_p(apci2200_pci_remove),
};
module_comedi_pci_driver(apci2200_driver, apci2200_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
