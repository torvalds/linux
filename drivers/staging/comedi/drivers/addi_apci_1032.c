#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci1032.c"

static const struct addi_board apci1032_boardtypes[] = {
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
		.di_config		= i_APCI1032_ConfigDigitalInput,
		.di_read		= i_APCI1032_Read1DigitalInput,
		.di_bits		= i_APCI1032_ReadMoreDigitalInput,
	},
};

static int i_ADDIDATA_InsnReadEeprom(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	const struct addi_board *this_board = comedi_board(dev);
	struct addi_private *devpriv = dev->private;
	unsigned short w_Address = CR_CHAN(insn->chanspec);
	unsigned short w_Data;

	w_Data = addi_eeprom_readw(devpriv->i_IobaseAmcc,
		this_board->pc_EepromChip, 2 * w_Address);
	data[0] = w_Data;

	return insn->n;
}

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	const struct addi_board *this_board = comedi_board(dev);

	this_board->interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static const void *addi_find_boardinfo(struct comedi_device *dev,
				       struct pci_dev *pcidev)
{
	const void *p = dev->driver->board_name;
	const struct addi_board *this_board;
	int i;

	for (i = 0; i < dev->driver->num_names; i++) {
		this_board = p;
		if (this_board->i_VendorId == pcidev->vendor &&
		    this_board->i_DeviceId == pcidev->device)
			return this_board;
		p += dev->driver->offset;
	}
	return NULL;
}

static int apci1032_attach_pci(struct comedi_device *dev,
			       struct pci_dev *pcidev)
{
	const struct addi_board *this_board;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, n_subdevices;
	unsigned int dw_Dummy;

	this_board = addi_find_boardinfo(dev, pcidev);
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

	n_subdevices = 7;
	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	if (devpriv->s_EeParameters.i_NbrAoChannel) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrAoChannel;
		s->maxdata = devpriv->s_EeParameters.i_AoMaxdata;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrAoChannel;
		s->range_table = this_board->pr_AoRangelist;
		s->insn_config = this_board->ao_config;
		s->insn_write = this_board->ao_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	if (devpriv->s_EeParameters.i_NbrDiChannel) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrDiChannel;
		s->maxdata = 1;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrDiChannel;
		s->range_table = &range_digital;
		s->io_bits = 0;	/* all bits input */
		s->insn_config = this_board->di_config;
		s->insn_read = this_board->di_read;
		s->insn_write = this_board->di_write;
		s->insn_bits = this_board->di_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise TTL */
	s = &dev->subdevices[5];
	s->type = COMEDI_SUBD_UNUSED;

	/* EEPROM */
	s = &dev->subdevices[6];
	if (this_board->i_PCIEeprom) {
		s->type = COMEDI_SUBD_MEMORY;
		s->subdev_flags = SDF_READABLE | SDF_INTERNAL;
		s->n_chan = 256;
		s->maxdata = 0xffff;
		s->insn_read = i_ADDIDATA_InsnReadEeprom;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	i_APCI1032_Reset(dev);
	return 0;
}

static void apci1032_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			i_APCI1032_Reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
		if (devpriv->dw_AiBase)
			iounmap(devpriv->dw_AiBase);
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci1032_driver = {
	.driver_name	= "addi_apci_1032",
	.module		= THIS_MODULE,
	.attach_pci	= apci1032_attach_pci,
	.detach		= apci1032_detach,
	.num_names	= ARRAY_SIZE(apci1032_boardtypes),
	.board_name	= &apci1032_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int __devinit apci1032_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci1032_driver);
}

static void __devexit apci1032_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci1032_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1003) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1032_pci_table);

static struct pci_driver apci1032_pci_driver = {
	.name		= "addi_apci_1032",
	.id_table	= apci1032_pci_table,
	.probe		= apci1032_pci_probe,
	.remove		= __devexit_p(apci1032_pci_remove),
};
module_comedi_pci_driver(apci1032_driver, apci1032_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
