#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_16XX 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_16xx"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci16xx.c"

static const struct addi_board boardtypes[] = {
	{
		.pc_DriverName		= "apci1648",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1009,
		.i_IorangeBase0		= 128,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrTTLChannel	= 48,
		.reset			= i_APCI16XX_Reset,
		.ttl_config		= i_APCI16XX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI16XX_InsnBitsReadTTLIO,
		.ttl_read		= i_APCI16XX_InsnReadTTLIOAllPortValue,
		.ttl_write		= i_APCI16XX_InsnBitsWriteTTLIO,
	}, {
		.pc_DriverName		= "apci1696",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x100A,
		.i_IorangeBase0		= 128,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.i_NbrTTLChannel	= 96,
		.reset			= i_APCI16XX_Reset,
		.ttl_config		= i_APCI16XX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI16XX_InsnBitsReadTTLIO,
		.ttl_read		= i_APCI16XX_InsnReadTTLIOAllPortValue,
		.ttl_write		= i_APCI16XX_InsnBitsWriteTTLIO,
	},
};

static DEFINE_PCI_DEVICE_TABLE(addi_apci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1009) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x100a) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, addi_apci_tbl);

#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
