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

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_APCI1710.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci1710_boardtypes[] = {
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

static struct comedi_driver apci1710_driver = {
	.driver_name	= "addi_apci_1710",
	.module		= THIS_MODULE,
	.attach		= i_ADDI_Attach,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(apci1710_boardtypes),
	.board_name	= &apci1710_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int __devinit apci1710_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci1710_driver);
}

static void __devexit apci1710_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci1710_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA_OLD, APCI1710_BOARD_DEVICE_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1710_pci_table);

static struct pci_driver apci1710_pci_driver = {
	.name		= "addi_apci_1710",
	.id_table	= apci1710_pci_table,
	.probe		= apci1710_pci_probe,
	.remove		= __devexit_p(apci1710_pci_remove),
};
module_comedi_pci_driver(apci1710_driver, apci1710_pci_driver);
