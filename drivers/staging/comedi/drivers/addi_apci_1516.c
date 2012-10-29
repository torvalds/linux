#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_1516 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_1516"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci1516.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci1516",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1001,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= APCI1516_ADDRESS_RANGE,
		.i_IorangeBase2		= 32,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrDiChannel		= 8,
		.i_NbrDoChannel		= 8,
		.i_Timer		= 1,
		.reset			= i_APCI1516_Reset,
		.di_read		= i_APCI1516_Read1DigitalInput,
		.di_bits		= i_APCI1516_ReadMoreDigitalInput,
		.do_config		= i_APCI1516_ConfigDigitalOutput,
		.do_write		= i_APCI1516_WriteDigitalOutput,
		.do_bits		= i_APCI1516_ReadDigitalOutput,
		.timer_config		= i_APCI1516_ConfigWatchdog,
		.timer_write		= i_APCI1516_StartStopWriteWatchdog,
		.timer_read		= i_APCI1516_ReadWatchdog,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1001) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
