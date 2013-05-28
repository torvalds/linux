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
#include "addi-data/hwdrv_APCI1710.c"

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	v_APCI1710_Interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static int apci1710_auto_attach(struct comedi_device *dev,
					  unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	devpriv->s_BoardInfos.ui_Address = pci_resource_start(pcidev, 2);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	i_ADDI_AttachPCI1710(dev);

	i_APCI1710_Reset(dev);
	return 0;
}

static void apci1710_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		i_APCI1710_Reset(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	comedi_pci_disable(dev);
}

static struct comedi_driver apci1710_driver = {
	.driver_name	= "addi_apci_1710",
	.module		= THIS_MODULE,
	.auto_attach	= apci1710_auto_attach,
	.detach		= apci1710_detach,
};

static int apci1710_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci1710_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(apci1710_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMCC, APCI1710_BOARD_DEVICE_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1710_pci_table);

static struct pci_driver apci1710_pci_driver = {
	.name		= "addi_apci_1710",
	.id_table	= apci1710_pci_table,
	.probe		= apci1710_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci1710_driver, apci1710_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
