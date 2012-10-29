#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_2200 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_2200"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci2200.c"

static const struct addi_board boardtypes[] = {
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

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1005) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
