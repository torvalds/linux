#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_3120 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_3120"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3120.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci3120",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA_OLD,
		.i_DeviceId		= 0x818D,
		.i_IorangeBase0		= AMCC_OP_REG_SIZE,
		.i_IorangeBase1		= APCI3120_ADDRESS_RANGE,
		.i_IorangeBase2		= 8,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 8,
		.i_AiMaxdata		= 0xffff,
		.i_AoMaxdata		= 0x3fff,
		.pr_AiRangelist		= &range_apci3120_ai,
		.pr_AoRangelist		= &range_apci3120_ao,
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
		.ao_write		= i_APCI3120_InsnWriteAnalogOutput,
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

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA_OLD, 0x818d) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
