#include <asm/i387.h>

#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

static void fpu_begin(void)
{
	kernel_fpu_begin();
}

static void fpu_end(void)
{
	kernel_fpu_end();
}

#define CONFIG_APCI_3200 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_3200"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3200.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci3200",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3000,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 4,
		.i_IorangeBase3		= 4,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0x3ffff,
		.pr_AiRangelist		= &range_apci3200_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI3200_Interrupt,
		.reset			= i_APCI3200_Reset,
		.ai_config		= i_APCI3200_ConfigAnalogInput,
		.ai_read		= i_APCI3200_ReadAnalogInput,
		.ai_write		= i_APCI3200_InsnWriteReleaseAnalogInput,
		.ai_bits		= i_APCI3200_InsnBits_AnalogInput_Test,
		.ai_cmdtest		= i_APCI3200_CommandTestAnalogInput,
		.ai_cmd			= i_APCI3200_CommandAnalogInput,
		.ai_cancel		= i_APCI3200_StopCyclicAcquisition,
		.di_bits		= i_APCI3200_ReadDigitalInput,
		.do_config		= i_APCI3200_ConfigDigitalOutput,
		.do_write		= i_APCI3200_WriteDigitalOutput,
		.do_bits		= i_APCI3200_ReadDigitalOutput,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3000) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"
