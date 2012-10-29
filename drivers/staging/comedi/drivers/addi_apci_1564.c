#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_1564 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_1564"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci1564.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci1564",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1006,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= APCI1564_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDiChannel		= 32,
		.i_NbrDoChannel		= 32,
		.i_DoMaxdata		= 0xffffffff,
		.i_Timer		= 1,
		.interrupt		= v_APCI1564_Interrupt,
		.reset			= i_APCI1564_Reset,
		.di_config		= i_APCI1564_ConfigDigitalInput,
		.di_read		= i_APCI1564_Read1DigitalInput,
		.di_bits		= i_APCI1564_ReadMoreDigitalInput,
		.do_config		= i_APCI1564_ConfigDigitalOutput,
		.do_write		= i_APCI1564_WriteDigitalOutput,
		.do_bits		= i_APCI1564_ReadDigitalOutput,
		.do_read		= i_APCI1564_ReadInterruptStatus,
		.timer_config		= i_APCI1564_ConfigTimerCounterWatchdog,
		.timer_write		= i_APCI1564_StartStopWriteTimerCounterWatchdog,
		.timer_read		= i_APCI1564_ReadTimerCounterWatchdog,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1006) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
