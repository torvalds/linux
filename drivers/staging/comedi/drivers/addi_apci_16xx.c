#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci16xx.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci16xx_boardtypes[] = {
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

static struct comedi_driver apci16xx_driver = {
	.driver_name	= "addi_apci_16xx",
	.module		= THIS_MODULE,
	.auto_attach	= addi_auto_attach,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(apci16xx_boardtypes),
	.board_name	= &apci16xx_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int apci16xx_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci16xx_driver);
}

static void apci16xx_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci16xx_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1009) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x100a) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci16xx_pci_table);

static struct pci_driver apci16xx_pci_driver = {
	.name		= "addi_apci_16xx",
	.id_table	= apci16xx_pci_table,
	.probe		= apci16xx_pci_probe,
	.remove		= apci16xx_pci_remove,
};
module_comedi_pci_driver(apci16xx_driver, apci16xx_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
