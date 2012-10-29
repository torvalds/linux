#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_3501 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_3501"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3501.c"

static const struct addi_board boardtypes[] = {
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
		.di_bits		= i_APCI3501_ReadDigitalInput,
		.do_config		= i_APCI3501_ConfigDigitalOutput,
		.do_write		= i_APCI3501_WriteDigitalOutput,
		.do_bits		= i_APCI3501_ReadDigitalOutput,
		.timer_config		= i_APCI3501_ConfigTimerCounterWatchdog,
		.timer_write		= i_APCI3501_StartStopWriteTimerCounterWatchdog,
		.timer_read		= i_APCI3501_ReadTimerCounterWatchdog,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3001) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
