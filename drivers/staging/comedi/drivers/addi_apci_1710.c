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
#include "addi-data/hwdrv_APCI1710.c"

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

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

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	const struct addi_board *this_board = comedi_board(dev);

	this_board->interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static int i_ADDI_Reset(struct comedi_device *dev)
{
	const struct addi_board *this_board = comedi_board(dev);

	this_board->reset(dev);
	return 0;
}

static const void *apci1710_find_boardinfo(struct comedi_device *dev,
					   struct pci_dev *pcidev)
{
	const struct addi_board *this_board;
	int i;

	for (i = 0; i < ARRAY_SIZE(apci1710_boardtypes); i++) {
		this_board = &apci1710_boardtypes[i];
		if (this_board->i_VendorId == pcidev->vendor &&
		    this_board->i_DeviceId == pcidev->device)
			return this_board;
	}
	return NULL;
}

static int apci1710_attach_pci(struct comedi_device *dev,
			       struct pci_dev *pcidev)
{
	const struct addi_board *this_board;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, pages, i, n_subdevices;
	unsigned int dw_Dummy;

	this_board = apci1710_find_boardinfo(dev, pcidev);
	if (!this_board)
		return -ENODEV;
	dev->board_ptr = this_board;
	dev->board_name = this_board->pc_DriverName;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	if (this_board->i_Dma)
		pci_set_master(pcidev);

	if (!this_board->pc_EepromChip ||
	    !strcmp(this_board->pc_EepromChip, ADDIDATA_9054)) {
		if (this_board->i_IorangeBase1)
			dev->iobase = pci_resource_start(pcidev, 1);
		else
			dev->iobase = pci_resource_start(pcidev, 0);

		devpriv->iobase = dev->iobase;
		devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);
		devpriv->i_IobaseAddon = pci_resource_start(pcidev, 2);
	} else {
		dev->iobase = pci_resource_start(pcidev, 2);
		devpriv->iobase = pci_resource_start(pcidev, 2);
		devpriv->dw_AiBase = ioremap(pci_resource_start(pcidev, 3),
					     this_board->i_IorangeBase3);
	}
	devpriv->i_IobaseReserved = pci_resource_start(pcidev, 3);

	/* Initialize parameters that can be overridden in EEPROM */
	devpriv->s_EeParameters.i_NbrAiChannel = this_board->i_NbrAiChannel;
	devpriv->s_EeParameters.i_NbrAoChannel = this_board->i_NbrAoChannel;
	devpriv->s_EeParameters.i_AiMaxdata = this_board->i_AiMaxdata;
	devpriv->s_EeParameters.i_AoMaxdata = this_board->i_AoMaxdata;
	devpriv->s_EeParameters.i_NbrDiChannel = this_board->i_NbrDiChannel;
	devpriv->s_EeParameters.i_NbrDoChannel = this_board->i_NbrDoChannel;
	devpriv->s_EeParameters.i_DoMaxdata = this_board->i_DoMaxdata;
	devpriv->s_EeParameters.i_Dma = this_board->i_Dma;
	devpriv->s_EeParameters.i_Timer = this_board->i_Timer;
	devpriv->s_EeParameters.ui_MinAcquisitiontimeNs =
		this_board->ui_MinAcquisitiontimeNs;
	devpriv->s_EeParameters.ui_MinDelaytimeNs =
		this_board->ui_MinDelaytimeNs;

	/* ## */

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	/*  Read eepeom and fill addi_board Structure */

	if (this_board->i_PCIEeprom) {
		if (!(strcmp(this_board->pc_EepromChip, "S5920"))) {
			/*  Set 3 wait stait */
			if (!(strcmp(dev->board_name, "apci035"))) {
				outl(0x80808082, devpriv->i_IobaseAmcc + 0x60);
			} else {
				outl(0x83838383, devpriv->i_IobaseAmcc + 0x60);
			}
			/*  Enable the interrupt for the controller */
			dw_Dummy = inl(devpriv->i_IobaseAmcc + 0x38);
			outl(dw_Dummy | 0x2000, devpriv->i_IobaseAmcc + 0x38);
		}
		addi_eeprom_read_info(dev, pci_resource_start(pcidev, 0));
	}

	devpriv->us_UseDma = ADDI_ENABLE;

	if (devpriv->s_EeParameters.i_Dma) {
		if (devpriv->us_UseDma == ADDI_ENABLE) {
			/*  alloc DMA buffers */
			devpriv->b_DmaDoubleBuffer = 0;
			for (i = 0; i < 2; i++) {
				for (pages = 4; pages >= 0; pages--) {
					devpriv->ul_DmaBufferVirtual[i] =
						(void *) __get_free_pages(GFP_KERNEL, pages);

					if (devpriv->ul_DmaBufferVirtual[i])
						break;
				}
				if (devpriv->ul_DmaBufferVirtual[i]) {
					devpriv->ui_DmaBufferPages[i] = pages;
					devpriv->ui_DmaBufferSize[i] =
						PAGE_SIZE * pages;
					devpriv->ui_DmaBufferSamples[i] =
						devpriv->
						ui_DmaBufferSize[i] >> 1;
					devpriv->ul_DmaBufferHw[i] =
						virt_to_bus((void *)devpriv->
						ul_DmaBufferVirtual[i]);
				}
			}
			if (!devpriv->ul_DmaBufferVirtual[0])
				devpriv->us_UseDma = ADDI_DISABLE;

			if (devpriv->ul_DmaBufferVirtual[1])
				devpriv->b_DmaDoubleBuffer = 1;
		}
	}

	i_ADDI_AttachPCI1710(dev);

	devpriv->s_BoardInfos.ui_Address = pci_resource_start(pcidev, 2);

	i_ADDI_Reset(dev);
	return 0;
}

static void apci1710_detach(struct comedi_device *dev)
{
	const struct addi_board *this_board = comedi_board(dev);
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			i_ADDI_Reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
		if ((this_board->pc_EepromChip == NULL) ||
		    (strcmp(this_board->pc_EepromChip, ADDIDATA_9054) != 0)) {
			if (devpriv->ul_DmaBufferVirtual[0]) {
				free_pages((unsigned long)devpriv->
					ul_DmaBufferVirtual[0],
					devpriv->ui_DmaBufferPages[0]);
			}
			if (devpriv->ul_DmaBufferVirtual[1]) {
				free_pages((unsigned long)devpriv->
					ul_DmaBufferVirtual[1],
					devpriv->ui_DmaBufferPages[1]);
			}
		} else {
			iounmap(devpriv->dw_AiBase);
		}
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci1710_driver = {
	.driver_name	= "addi_apci_1710",
	.module		= THIS_MODULE,
	.attach_pci	= apci1710_attach_pci,
	.detach		= apci1710_detach,
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
