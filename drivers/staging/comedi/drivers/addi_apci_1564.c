#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci1564.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci1564_boardtypes[] = {
	{
		.pc_DriverName		= "apci1564",
		.i_IorangeBase1		= APCI1564_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_93C76,
		.i_NbrDiChannel		= 32,
		.i_NbrDoChannel		= 32,
		.i_DoMaxdata		= 0xffffffff,
		.i_Timer		= 1,
		.interrupt		= apci1564_interrupt,
		.reset			= apci1564_reset,
		.di_config		= apci1564_di_config,
		.di_bits		= apci1564_di_insn_bits,
		.do_config		= apci1564_do_config,
		.do_bits		= apci1564_do_insn_bits,
		.do_read		= apci1564_do_read,
		.timer_config		= apci1564_timer_config,
		.timer_write		= apci1564_timer_write,
		.timer_read		= apci1564_timer_read,
	},
};

static int apci1564_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	dev->board_ptr = &apci1564_boardtypes[0];

	return addi_auto_attach(dev, context);
}

static struct comedi_driver apci1564_driver = {
	.driver_name	= "addi_apci_1564",
	.module		= THIS_MODULE,
	.auto_attach	= apci1564_auto_attach,
	.detach		= i_ADDI_Detach,
};

static int apci1564_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci1564_driver, id->driver_data);
}

static const struct pci_device_id apci1564_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1006) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1564_pci_table);

static struct pci_driver apci1564_pci_driver = {
	.name		= "addi_apci_1564",
	.id_table	= apci1564_pci_table,
	.probe		= apci1564_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci1564_driver, apci1564_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
