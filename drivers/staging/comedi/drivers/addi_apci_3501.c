#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3501.c"

static const struct addi_board apci3501_boardtypes[] = {
	{
		.pc_DriverName		= "apci3501",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x3001,
		.i_IorangeBase0		= 64,
		.i_IorangeBase1		= APCI3501_ADDRESS_RANGE,
		.i_PCIEeprom		= ADDIDATA_EEPROM,
		.pc_EepromChip		= ADDIDATA_S5933,
	},
};

static int apci3501_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	data[1] = inl(dev->iobase + APCI3501_DIGITAL_IP) & 0x3;

	return insn->n;
}

static int apci3501_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	s->state = inl(dev->iobase + APCI3501_DIGITAL_OP);
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outl(s->state, dev->iobase + APCI3501_DIGITAL_OP);
	}

	data[1] = s->state;

	return insn->n;
}

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

static irqreturn_t apci3501_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct addi_private *devpriv = dev->private;
	unsigned int ui_Timer_AOWatchdog;
	unsigned long ul_Command1;
	int i_temp;

	/*  Disable Interrupt */
	ul_Command1 =
		inl(dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);

	ul_Command1 = (ul_Command1 & 0xFFFFF9FDul);
	outl(ul_Command1,
		dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);

	ui_Timer_AOWatchdog =
		inl(dev->iobase + APCI3501_WATCHDOG +
		APCI3501_TCW_IRQ) & 0x1;

	if ((!ui_Timer_AOWatchdog)) {
		comedi_error(dev, "IRQ from unknown source");
		return IRQ_NONE;
	}

	/* Enable Interrupt Send a signal to from kernel to user space */
	send_sig(SIGIO, devpriv->tsk_Current, 0);
	ul_Command1 =
		inl(dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);
	ul_Command1 = ((ul_Command1 & 0xFFFFF9FDul) | 1 << 1);
	outl(ul_Command1,
		dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);
	i_temp = inl(dev->iobase + APCI3501_WATCHDOG +
		APCI3501_TCW_TRIG_STATUS) & 0x1;

	return IRQ_HANDLED;
}

static int apci3501_reset(struct comedi_device *dev)
{
	int i_Count = 0, i_temp = 0;
	unsigned int ul_Command1 = 0, ul_Polarity, ul_DAC_Ready = 0;

	outl(0x0, dev->iobase + APCI3501_DIGITAL_OP);
	outl(1, dev->iobase + APCI3501_ANALOG_OUTPUT +
		APCI3501_AO_VOLT_MODE);

	ul_Polarity = 0x80000000;

	for (i_Count = 0; i_Count <= 7; i_Count++) {
		ul_DAC_Ready = inl(dev->iobase + APCI3501_ANALOG_OUTPUT);

		while (ul_DAC_Ready == 0) {
			ul_DAC_Ready =
				inl(dev->iobase + APCI3501_ANALOG_OUTPUT);
			ul_DAC_Ready = (ul_DAC_Ready >> 8) & 1;
		}

		if (ul_DAC_Ready) {
			/*  Output the Value on the output channels. */
			ul_Command1 =
				(unsigned int) ((unsigned int) (i_Count & 0xFF) |
				(unsigned int) ((i_temp << 0x8) & 0x7FFFFF00L) |
				(unsigned int) (ul_Polarity));
			outl(ul_Command1,
				dev->iobase + APCI3501_ANALOG_OUTPUT +
				APCI3501_AO_PROG);
		}
	}

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

static int apci3501_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct addi_board *this_board;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, n_subdevices;

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

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);

	/* Initialize parameters that can be overridden in EEPROM */
	devpriv->s_EeParameters.i_NbrAoChannel = this_board->i_NbrAoChannel;

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci3501_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	addi_eeprom_read_info(dev, pci_resource_start(pcidev, 0));

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
		s->maxdata = 0x3fff;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrAoChannel;
		s->range_table = &range_apci3501_ao;
		s->insn_config = i_APCI3501_ConfigAnalogOutput;
		s->insn_write = i_APCI3501_WriteAnalogOutput;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 2;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci3501_di_insn_bits;

	/* Initialize the digital output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 2;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci3501_do_insn_bits;

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;
	s->insn_write = i_APCI3501_StartStopWriteTimerCounterWatchdog;
	s->insn_read = i_APCI3501_ReadTimerCounterWatchdog;
	s->insn_config = i_APCI3501_ConfigTimerCounterWatchdog;

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

	apci3501_reset(dev);
	return 0;
}

static void apci3501_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			apci3501_reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci3501_driver = {
	.driver_name	= "addi_apci_3501",
	.module		= THIS_MODULE,
	.auto_attach	= apci3501_auto_attach,
	.detach		= apci3501_detach,
	.num_names	= ARRAY_SIZE(apci3501_boardtypes),
	.board_name	= &apci3501_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int apci3501_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci3501_driver);
}

static void apci3501_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci3501_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3001) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3501_pci_table);

static struct pci_driver apci3501_pci_driver = {
	.name		= "addi_apci_3501",
	.id_table	= apci3501_pci_table,
	.probe		= apci3501_pci_probe,
	.remove		= apci3501_pci_remove,
};
module_comedi_pci_driver(apci3501_driver, apci3501_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
