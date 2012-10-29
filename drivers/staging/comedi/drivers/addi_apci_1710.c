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

#define CONFIG_APCI_1710 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_1710"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_APCI1710.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci1710",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA_OLD,
		.i_DeviceId		= APCI1710_BOARD_DEVICE_ID,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= 8,
		.i_IorangeBase2		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.interrupt		= v_APCI1710_Interrupt,
		.reset			= i_APCI1710_Reset,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA_OLD, APCI1710_BOARD_DEVICE_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"
