#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_2016 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_2016"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci2016.c"

static const struct addi_board boardtypes[] = {
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
		.do_config		= i_APCI2016_ConfigDigitalOutput,
		.do_write		= i_APCI2016_WriteDigitalOutput,
		.do_bits		= i_APCI2016_BitsDigitalOutput,
		.timer_config		= i_APCI2016_ConfigWatchdog,
		.timer_write		= i_APCI2016_StartStopWriteWatchdog,
		.timer_read		= i_APCI2016_ReadWatchdog,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1002) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
