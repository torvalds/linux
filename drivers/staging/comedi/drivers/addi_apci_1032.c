#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_1032 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_1032"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci1032.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci1032",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1003,
		.i_IorangeBase0		= 4,
		.i_IorangeBase1		= APCI1032_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDiChannel		= 32,
		.interrupt		= v_APCI1032_Interrupt,
		.reset			= i_APCI1032_Reset,
		.di_config		= i_APCI1032_ConfigDigitalInput,
		.di_read		= i_APCI1032_Read1DigitalInput,
		.di_bits		= i_APCI1032_ReadMoreDigitalInput,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1003) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
