#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci2016.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci2016_boardtypes[] = {
	{
		.pc_DriverName		= "apci2016",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1002,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= APCI2016_ADDRESS_RANGE,
		.i_IorangeBase2		= 32,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrDoChannel		= 16,
		.i_Timer		= 1,
		.reset			= i_APCI2016_Reset,
		.do_bits		= apci2016_do_insn_bits,
		.timer_config		= i_APCI2016_ConfigWatchdog,
		.timer_write		= i_APCI2016_StartStopWriteWatchdog,
		.timer_read		= i_APCI2016_ReadWatchdog,
	},
};

static struct comedi_driver apci2016_driver = {
	.driver_name	= "addi_apci_2016",
	.module		= THIS_MODULE,
	.attach_pci	= addi_attach_pci,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(apci2016_boardtypes),
	.board_name	= &apci2016_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int __devinit apci2016_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci2016_driver);
}

static void __devexit apci2016_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci2016_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1002) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci2016_pci_table);

static struct pci_driver apci2016_pci_driver = {
	.name		= "addi_apci_2016",
	.id_table	= apci2016_pci_table,
	.probe		= apci2016_pci_probe,
	.remove		= __devexit_p(apci2016_pci_remove),
};
module_comedi_pci_driver(apci2016_driver, apci2016_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
