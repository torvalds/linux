#include <linux/pci.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci1500.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci1500_boardtypes[] = {
	{
		.pc_DriverName		= "apci1500",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA_OLD,
		.i_DeviceId		= 0x80fc,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= APCI1500_ADDRESS_RANGE,
		.i_IorangeBase2		= 4,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrDiChannel		= 16,
		.i_NbrDoChannel		= 16,
		.i_DoMaxdata		= 0xffff,
		.i_Timer		= 1,
		.interrupt		= v_APCI1500_Interrupt,
		.reset			= i_APCI1500_Reset,
		.di_config		= i_APCI1500_ConfigDigitalInputEvent,
		.di_read		= i_APCI1500_Initialisation,
		.di_write		= i_APCI1500_StartStopInputEvent,
		.di_bits		= apci1500_di_insn_bits,
		.do_config		= i_APCI1500_ConfigDigitalOutputErrorInterrupt,
		.do_write		= i_APCI1500_WriteDigitalOutput,
		.do_bits		= i_APCI1500_ConfigureInterrupt,
		.timer_config		= i_APCI1500_ConfigCounterTimerWatchdog,
		.timer_write		= i_APCI1500_StartStopTriggerTimerCounterWatchdog,
		.timer_read		= i_APCI1500_ReadInterruptMask,
		.timer_bits		= i_APCI1500_ReadCounterTimerWatchdog,
	},
};

static struct comedi_driver apci1500_driver = {
	.driver_name	= "addi_apci_1500",
	.module		= THIS_MODULE,
	.auto_attach	= addi_auto_attach,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(apci1500_boardtypes),
	.board_name	= &apci1500_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int apci1500_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci1500_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(apci1500_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA_OLD, 0x80fc) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1500_pci_table);

static struct pci_driver apci1500_pci_driver = {
	.name		= "addi_apci_1500",
	.id_table	= apci1500_pci_table,
	.probe		= apci1500_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci1500_driver, apci1500_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
