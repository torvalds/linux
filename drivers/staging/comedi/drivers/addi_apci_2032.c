#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"

#include "addi-data/hwdrv_apci2032.c"

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	v_APCI2032_Interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static int apci2032_reset(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	devpriv->b_DigitalOutputRegister = 0;
	ui_Type = 0;
	outl(0x0, dev->iobase + APCI2032_DIGITAL_OP);
	outl(0x0, dev->iobase + APCI2032_DIGITAL_OP_INTERRUPT);
	outl(0x0, dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG + APCI2032_TCW_PROG);
	outl(0x0, dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG + APCI2032_TCW_RELOAD_VALUE);

	return 0;
}

static int apci2032_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
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
	dev->iobase = pci_resource_start(pcidev, 1);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/* Initialize the digital output subdevice */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags =
		SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 32;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_config = i_APCI2032_ConfigDigitalOutput;
	s->insn_bits = apci2032_do_insn_bits;
	s->insn_read = i_APCI2032_ReadInterruptStatus;

	/* Initialize the watchdog subdevice */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;
	s->insn_write = i_APCI2032_StartStopWriteWatchdog;
	s->insn_read = i_APCI2032_ReadWatchdog;
	s->insn_config = i_APCI2032_ConfigWatchdog;

	apci2032_reset(dev);
	return 0;
}

static void apci2032_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			apci2032_reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci2032_driver = {
	.driver_name	= "addi_apci_2032",
	.module		= THIS_MODULE,
	.auto_attach	= apci2032_auto_attach,
	.detach		= apci2032_detach,
};

static int apci2032_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci2032_driver);
}

static void apci2032_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci2032_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1004) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci2032_pci_table);

static struct pci_driver apci2032_pci_driver = {
	.name		= "addi_apci_2032",
	.id_table	= apci2032_pci_table,
	.probe		= apci2032_pci_probe,
	.remove		= apci2032_pci_remove,
};
module_comedi_pci_driver(apci2032_driver, apci2032_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
