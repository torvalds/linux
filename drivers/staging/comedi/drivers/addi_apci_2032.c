#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_2032 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_2032"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci2032.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci2032",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1004,
		.i_IorangeBase0		= 4,
		.i_IorangeBase1		= APCI2032_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDoChannel		= 32,
		.i_DoMaxdata		= 0xffffffff,
		.i_Timer		= 1,
		.interrupt		= v_APCI2032_Interrupt,
		.reset			= i_APCI2032_Reset,
		.do_config		= i_APCI2032_ConfigDigitalOutput,
		.do_write		= i_APCI2032_WriteDigitalOutput,
		.do_bits		= i_APCI2032_ReadDigitalOutput,
		.do_read		= i_APCI2032_ReadInterruptStatus,
		.timer_config		= i_APCI2032_ConfigWatchdog,
		.timer_write		= i_APCI2032_StartStopWriteWatchdog,
		.timer_read		= i_APCI2032_ReadWatchdog,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1004) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
