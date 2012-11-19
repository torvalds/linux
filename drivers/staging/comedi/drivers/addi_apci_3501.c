#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3501.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci3501_boardtypes[] = {
	{
		.pc_DriverName		= "apci3501",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3001,
		.i_IorangeBase0		= 64,
		.i_IorangeBase1		= APCI3501_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5933,
		.i_AoMaxdata		= 16383,
		.pr_AoRangelist		= &range_apci3501_ao,
		.i_NbrDiChannel		= 2,
		.i_NbrDoChannel		= 2,
		.i_DoMaxdata		= 0x3,
		.i_Timer		= 1,
		.interrupt		= v_APCI3501_Interrupt,
		.reset			= i_APCI3501_Reset,
		.ao_config		= i_APCI3501_ConfigAnalogOutput,
		.ao_write		= i_APCI3501_WriteAnalogOutput,
		.di_bits		= apci3501_di_insn_bits,
		.do_bits		= apci3501_do_insn_bits,
		.timer_config		= i_APCI3501_ConfigTimerCounterWatchdog,
		.timer_write		= i_APCI3501_StartStopWriteTimerCounterWatchdog,
		.timer_read		= i_APCI3501_ReadTimerCounterWatchdog,
	},
};

static DEFINE_PCI_DEVICE_TABLE(apci3501_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3001) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3501_pci_table);

static struct comedi_driver apci3501_driver = {
	.driver_name	= "addi_apci_3501",
	.module		= THIS_MODULE,
	.auto_attach	= addi_auto_attach,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(apci3501_boardtypes),
	.board_name	= &apci3501_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int apci3501_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci3501_driver);
}

static void __devexit apci3501_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver apci3501_pci_driver = {
	.name		= "addi_apci_3501",
	.id_table	= apci3501_pci_table,
	.probe		= apci3501_pci_probe,
	.remove		= apci3501_pci_remove,
};
module_comedi_pci_driver(apci3501_driver, apci3501_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
