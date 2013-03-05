#include <linux/pci.h>

#include <asm/i387.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

static void fpu_begin(void)
{
	kernel_fpu_begin();
}

static void fpu_end(void)
{
	kernel_fpu_end();
}

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3200.c"
#include "addi-data/addi_common.c"

enum apci3200_boardid {
	BOARD_APCI3200,
	BOARD_APCI3300,
};

static const struct addi_board apci3200_boardtypes[] = {
	[BOARD_APCI3200] = {
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
		.di_bits		= apci3200_di_insn_bits,
		.do_bits		= apci3200_do_insn_bits,
	},
	[BOARD_APCI3300] = {
		.pc_DriverName		= "apci3300",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3007,
		.i_IorangeBase0		= 128,
		.i_IorangeBase1		= 256,
		.i_IorangeBase2		= 4,
		.i_IorangeBase3		= 4,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 0x3ffff,
		.pr_AiRangelist		= &range_apci3300_ai,
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
		.di_bits		= apci3200_di_insn_bits,
		.do_bits		= apci3200_do_insn_bits,
	},
};

static int apci3200_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	const struct addi_board *board = NULL;

	if (context < ARRAY_SIZE(apci3200_boardtypes))
		board = &apci3200_boardtypes[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;

	return addi_auto_attach(dev, context);
}

static struct comedi_driver apci3200_driver = {
	.driver_name	= "addi_apci_3200",
	.module		= THIS_MODULE,
	.auto_attach	= apci3200_auto_attach,
	.detach		= i_ADDI_Detach,
};

static int apci3200_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci3200_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(apci3200_pci_table) = {
	{ PCI_VDEVICE(ADDIDATA, 0x3000), BOARD_APCI3200 },
	{ PCI_VDEVICE(ADDIDATA, 0x3007), BOARD_APCI3300 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3200_pci_table);

static struct pci_driver apci3200_pci_driver = {
	.name		= "addi_apci_3200",
	.id_table	= apci3200_pci_table,
	.probe		= apci3200_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci3200_driver, apci3200_pci_driver);
