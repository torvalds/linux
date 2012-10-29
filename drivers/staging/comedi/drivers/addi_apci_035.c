#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_035 1

#define ADDIDATA_WATCHDOG 2	/*  Or shold it be something else */

#define ADDIDATA_DRIVER_NAME	"addi_apci_035"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci035.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci035",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x0300,
		.i_IorangeBase0		= 127,
		.i_IorangeBase1		= APCI035_ADDRESS_RANGE,
		.i_PCIEeprom		= 1,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xff,
		.pr_AiRangelist		= &range_apci035_ai,
		.i_Timer		= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI035_Interrupt,
		.reset			= i_APCI035_Reset,
		.ai_config		= i_APCI035_ConfigAnalogInput,
		.ai_read		= i_APCI035_ReadAnalogInput,
		.timer_config		= i_APCI035_ConfigTimerWatchdog,
		.timer_write		= i_APCI035_StartStopWriteTimerWatchdog,
		.timer_read		= i_APCI035_ReadTimerWatchdog,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA,  0x0300) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
