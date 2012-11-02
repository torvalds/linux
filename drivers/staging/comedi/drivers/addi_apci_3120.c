#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/hwdrv_apci3120.c"

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

static const struct addi_board apci3120_boardtypes[] = {
	{
		.pc_DriverName		= "apci3120",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA_OLD,
		.i_DeviceId		= 0x818D,
		.i_IorangeBase0		= AMCC_OP_REG_SIZE,
		.i_IorangeBase1		= APCI3120_ADDRESS_RANGE,
		.i_IorangeBase2		= 8,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 8,
		.i_AiMaxdata		= 0xffff,
		.i_AoMaxdata		= 0x3fff,
		.pr_AiRangelist		= &range_apci3120_ai,
		.pr_AoRangelist		= &range_apci3120_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 0x0f,
		.b_AvailableConvertUnit	= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI3120_Interrupt,
		.reset			= i_APCI3120_Reset,
		.ai_config		= i_APCI3120_InsnConfigAnalogInput,
		.ai_read		= i_APCI3120_InsnReadAnalogInput,
		.ai_cmdtest		= i_APCI3120_CommandTestAnalogInput,
		.ai_cmd			= i_APCI3120_CommandAnalogInput,
		.ai_cancel		= i_APCI3120_StopCyclicAcquisition,
		.ao_write		= i_APCI3120_InsnWriteAnalogOutput,
		.di_read		= i_APCI3120_InsnReadDigitalInput,
		.di_bits		= i_APCI3120_InsnBitsDigitalInput,
		.do_config		= i_APCI3120_InsnConfigDigitalOutput,
		.do_write		= i_APCI3120_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3120_InsnBitsDigitalOutput,
		.timer_config		= i_APCI3120_InsnConfigTimer,
		.timer_write		= i_APCI3120_InsnWriteTimer,
		.timer_read		= i_APCI3120_InsnReadTimer,
	}, {
		.pc_DriverName		= "apci3001",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA_OLD,
		.i_DeviceId		= 0x828D,
		.i_IorangeBase0		= AMCC_OP_REG_SIZE,
		.i_IorangeBase1		= APCI3120_ADDRESS_RANGE,
		.i_IorangeBase2		= 8,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xfff,
		.pr_AiRangelist		= &range_apci3120_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 0x0f,
		.b_AvailableConvertUnit	= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI3120_Interrupt,
		.reset			= i_APCI3120_Reset,
		.ai_config		= i_APCI3120_InsnConfigAnalogInput,
		.ai_read		= i_APCI3120_InsnReadAnalogInput,
		.ai_cmdtest		= i_APCI3120_CommandTestAnalogInput,
		.ai_cmd			= i_APCI3120_CommandAnalogInput,
		.ai_cancel		= i_APCI3120_StopCyclicAcquisition,
		.di_read		= i_APCI3120_InsnReadDigitalInput,
		.di_bits		= i_APCI3120_InsnBitsDigitalInput,
		.do_config		= i_APCI3120_InsnConfigDigitalOutput,
		.do_write		= i_APCI3120_InsnWriteDigitalOutput,
		.do_bits		= i_APCI3120_InsnBitsDigitalOutput,
		.timer_config		= i_APCI3120_InsnConfigTimer,
		.timer_write		= i_APCI3120_InsnWriteTimer,
		.timer_read		= i_APCI3120_InsnReadTimer,
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

static int apci3120_attach_pci(struct comedi_device *dev,
			       struct pci_dev *pcidev)
{
	const struct addi_board *this_board;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, pages, i, n_subdevices;

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
	pci_set_master(pcidev);

	if (this_board->i_IorangeBase1)
		dev->iobase = pci_resource_start(pcidev, 1);
	else
		dev->iobase = pci_resource_start(pcidev, 0);

	devpriv->iobase = dev->iobase;
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);
	devpriv->i_IobaseAddon = pci_resource_start(pcidev, 2);
	devpriv->i_IobaseReserved = pci_resource_start(pcidev, 3);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	devpriv->us_UseDma = ADDI_ENABLE;

	/* Allocate DMA buffers */
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
			devpriv->ui_DmaBufferSize[i] = PAGE_SIZE * pages;
			devpriv->ui_DmaBufferSamples[i] =
				devpriv->ui_DmaBufferSize[i] >> 1;
			devpriv->ul_DmaBufferHw[i] =
				virt_to_bus((void *)devpriv->
				ul_DmaBufferVirtual[i]);
		}
	}
	if (!devpriv->ul_DmaBufferVirtual[0])
		devpriv->us_UseDma = ADDI_DISABLE;

	if (devpriv->ul_DmaBufferVirtual[1])
		devpriv->b_DmaDoubleBuffer = 1;

	n_subdevices = 7;
	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags =
		SDF_READABLE | SDF_COMMON | SDF_GROUND
		| SDF_DIFF;
	if (this_board->i_NbrAiChannel) {
		s->n_chan = this_board->i_NbrAiChannel;
		devpriv->b_SingelDiff = 0;
	} else {
		s->n_chan = this_board->i_NbrAiChannelDiff;
		devpriv->b_SingelDiff = 1;
	}
	s->maxdata = this_board->i_AiMaxdata;
	s->len_chanlist = this_board->i_AiChannelList;
	s->range_table = this_board->pr_AiRangelist;

	/* Set the initialisation flag */
	devpriv->b_AiInitialisation = 1;

	s->insn_config = this_board->ai_config;
	s->insn_read = this_board->ai_read;
	s->insn_write = this_board->ai_write;
	s->insn_bits = this_board->ai_bits;
	s->do_cmdtest = this_board->ai_cmdtest;
	s->do_cmd = this_board->ai_cmd;
	s->cancel = this_board->ai_cancel;

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	if (this_board->i_NbrAoChannel) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->i_NbrAoChannel;
		s->maxdata = this_board->i_AoMaxdata;
		s->len_chanlist = this_board->i_NbrAoChannel;
		s->range_table = this_board->pr_AoRangelist;
		s->insn_config = this_board->ao_config;
		s->insn_write = this_board->ao_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = this_board->i_NbrDiChannel;
	s->maxdata = 1;
	s->len_chanlist = this_board->i_NbrDiChannel;
	s->range_table = &range_digital;
	s->io_bits = 0;	/* all bits input */
	s->insn_config = this_board->di_config;
	s->insn_read = this_board->di_read;
	s->insn_write = this_board->di_write;
	s->insn_bits = this_board->di_bits;

	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags =
		SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = this_board->i_NbrDoChannel;
	s->maxdata = this_board->i_DoMaxdata;
	s->len_chanlist = this_board->i_NbrDoChannel;
	s->range_table = &range_digital;
	s->io_bits = 0xf;	/* all bits output */

	/* insn_config - for digital output memory */
	s->insn_config = this_board->do_config;
	s->insn_write = this_board->do_write;
	s->insn_bits = this_board->do_bits;
	s->insn_read = this_board->do_read;

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;

	s->insn_write = this_board->timer_write;
	s->insn_read = this_board->timer_read;
	s->insn_config = this_board->timer_config;
	s->insn_bits = this_board->timer_bits;

	/*  Allocate and Initialise TTL */
	s = &dev->subdevices[5];
	s->type = COMEDI_SUBD_UNUSED;

	/* EEPROM */
	s = &dev->subdevices[6];
	s->type = COMEDI_SUBD_UNUSED;

	i_ADDI_Reset(dev);
	return 0;
}

static void apci3120_detach(struct comedi_device *dev)
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

static struct comedi_driver apci3120_driver = {
	.driver_name	= "addi_apci_3120",
	.module		= THIS_MODULE,
	.attach_pci	= apci3120_attach_pci,
	.detach		= apci3120_detach,
	.num_names	= ARRAY_SIZE(apci3120_boardtypes),
	.board_name	= &apci3120_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int __devinit apci3120_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci3120_driver);
}

static void __devexit apci3120_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci3120_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA_OLD, 0x818d) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA_OLD, 0x828d) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3120_pci_table);

static struct pci_driver apci3120_pci_driver = {
	.name		= "addi_apci_3120",
	.id_table	= apci3120_pci_table,
	.probe		= apci3120_pci_probe,
	.remove		= __devexit_p(apci3120_pci_remove),
};
module_comedi_pci_driver(apci3120_driver, apci3120_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
