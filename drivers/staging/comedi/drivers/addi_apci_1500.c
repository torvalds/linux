#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_1500 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_1500"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci1500.c"

static const struct addi_board boardtypes[] = {
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
		.di_bits		= i_APCI1500_ReadMoreDigitalInput,
		.do_config		= i_APCI1500_ConfigDigitalOutputErrorInterrupt,
		.do_write		= i_APCI1500_WriteDigitalOutput,
		.do_bits		= i_APCI1500_ConfigureInterrupt,
		.timer_config		= i_APCI1500_ConfigCounterTimerWatchdog,
		.timer_write		= i_APCI1500_StartStopTriggerTimerCounterWatchdog,
		.timer_read		= i_APCI1500_ReadInterruptMask,
		.timer_bits		= i_APCI1500_ReadCounterTimerWatchdog,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA_OLD, 0x80fc) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
