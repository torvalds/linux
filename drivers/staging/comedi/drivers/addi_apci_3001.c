#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3120.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci3001_boardtypes[] = {
	{
		.pc_DriverName		= "apci3001",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA_OLD,
		.i_DeviceId		= 0x828D,
		.i_IorangeBase0		= AMCC_OP_REG_SIZE,
		.i_IorangeBase1		= APCI3120_ADDRESS_RANGE,
		.i_IorangeBase2		= 8,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xfff,
		.pr_AiRangelist		= &range_apci3120_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 0x0f,
		.i_Dma			= 1,
		.i_Timer		= 1,
		.b_AvailableConvertUnit	= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI3120_Interrupt,
		.reset			= i_APCI3120_Reset,
		.ai_config		= i_APCI3120_InsnConfigAnalogInput,
		.ai_read		= i_APCI3120_InsnReadAnalogInput,
		.ai_cmdtest		= i_APCI3120_CommandTestAnalogInput,
		.ai_cmd			= i_APCI3120_CommandAnalogInput,
		.ai_cancel		= i_APCI3120_StopCyclicAcquisition,
		.di_read		= i_APCI3120_InsnReadDigitalInput,
		.di_bits		= i_APCI3120_InsnBitsDigitalInput,
		.do_config		= i_APCI3120_InsnConfigDigitalOutput,
		.do_write		= i_APCI3120_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3120_InsnBitsDigitalOutput,
		.timer_config		= i_APCI3120_InsnConfigTimer,
		.timer_write		= i_APCI3120_InsnWriteTimer,
		.timer_read		= i_APCI3120_InsnReadTimer,
	},
};

static struct comedi_driver apci3001_driver = {
	.driver_name	= "addi_apci_3001",
	.module		= THIS_MODULE,
	.attach_pci	= addi_attach_pci,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(apci3001_boardtypes),
	.board_name	= &apci3001_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int __devinit apci3001_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci3001_driver);
}

static void __devexit apci3001_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci3001_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA_OLD, 0x828d) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3001_pci_table);

static struct pci_driver apci3001_pci_driver = {
	.name		= "addi_apci_3001",
	.id_table	= apci3001_pci_table,
	.probe		= apci3001_pci_probe,
	.remove		= __devexit_p(apci3001_pci_remove),
};
module_comedi_pci_driver(apci3001_driver, apci3001_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
