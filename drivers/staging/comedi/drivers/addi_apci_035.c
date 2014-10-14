#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

struct apci035_private {
	int iobase;
	int i_IobaseAmcc;
	int i_IobaseAddon;
	int i_IobaseReserved;
	unsigned char b_TimerSelectMode;
	struct task_struct *tsk_Current;
};

#define ADDIDATA_WATCHDOG 2	/*  Or shold it be something else */

#include "addi-data/hwdrv_apci035.c"

static int apci035_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct apci035_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int dw_Dummy;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->iobase = dev->iobase;
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);
	devpriv->i_IobaseAddon = pci_resource_start(pcidev, 2);
	devpriv->i_IobaseReserved = pci_resource_start(pcidev, 3);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci035_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	/*  Set 3 wait stait */
	outl(0x80808082, devpriv->i_IobaseAmcc + 0x60);

	/*  Enable the interrupt for the controller */
	dw_Dummy = inl(devpriv->i_IobaseAmcc + 0x38);
	outl(dw_Dummy | 0x2000, devpriv->i_IobaseAmcc + 0x38);

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_COMMON | SDF_GROUND | SDF_DIFF;
	s->n_chan = 16;
	s->maxdata = 0xff;
	s->len_chanlist = s->n_chan;
	s->range_table = &range_apci035_ai;
	s->insn_config = apci035_ai_config;
	s->insn_read = apci035_ai_read;

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;
	s->insn_write = apci035_timer_write;
	s->insn_read = apci035_timer_read;
	s->insn_config = apci035_timer_config;

	apci035_reset(dev);

	return 0;
}

static void apci035_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		apci035_reset(dev);
	comedi_pci_detach(dev);
}

static struct comedi_driver apci035_driver = {
	.driver_name	= "addi_apci_035",
	.module		= THIS_MODULE,
	.auto_attach	= apci035_auto_attach,
	.detach		= apci035_detach,
};

static int apci035_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci035_driver, id->driver_data);
}

static const struct pci_device_id apci035_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA,  0x0300) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci035_pci_table);

static struct pci_driver apci035_pci_driver = {
	.name		= "addi_apci_035",
	.id_table	= apci035_pci_table,
	.probe		= apci035_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci035_driver, apci035_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
