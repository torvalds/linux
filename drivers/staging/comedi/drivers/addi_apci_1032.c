#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/hwdrv_apci1032.c"

static irqreturn_t apci1032_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct addi_private *devpriv = dev->private;
	unsigned int ctrl;

	/* disable the interrupt */
	ctrl = inl(dev->iobase + APCI1032_CTRL_REG);
	outl(ctrl & ~APCI1032_CTRL_INT_ENA, dev->iobase + APCI1032_CTRL_REG);

	ui_InterruptStatus = inl(dev->iobase + APCI1032_STATUS_REG);
	ui_InterruptStatus = ui_InterruptStatus & 0X0000FFFF;
	send_sig(SIGIO, devpriv->tsk_Current, 0);	/*  send signal to the sample */

	/* enable the interrupt */
	outl(ctrl, dev->iobase + APCI1032_CTRL_REG);

	return IRQ_HANDLED;
}

static int apci1032_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	data[1] = inl(dev->iobase + APCI1032_DI_REG);

	return insn->n;
}

static int apci1032_reset(struct comedi_device *dev)
{
	/* disable the interrupts */
	outl(0x0, dev->iobase + APCI1032_CTRL_REG);
	/* Reset the interrupt status register */
	inl(dev->iobase + APCI1032_STATUS_REG);
	/* Disable the and/or interrupt */
	outl(0x0, dev->iobase + APCI1032_MODE1_REG);
	outl(0x0, dev->iobase + APCI1032_MODE2_REG);

	return 0;
}

static int apci1032_attach_pci(struct comedi_device *dev,
			       struct pci_dev *pcidev)
{
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	dev->board_name = dev->driver->driver_name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 2);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci1032_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 32;
	s->maxdata	= 1;
	s->len_chanlist	= 32;
	s->range_table	= &range_digital;
	s->insn_config	= i_APCI1032_ConfigDigitalInput;
	s->insn_bits	= apci1032_di_insn_bits;

	apci1032_reset(dev);
	return 0;
}

static void apci1032_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			apci1032_reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
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
