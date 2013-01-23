#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/hwdrv_apci3501.c"

/*
 * AMCC S5933 NVRAM
 */
#define NVRAM_USER_DATA_START	0x100

#define NVCMD_BEGIN_READ	(0x7 << 5)
#define NVCMD_LOAD_LOW		(0x4 << 5)
#define NVCMD_LOAD_HIGH		(0x5 << 5)

/*
 * Function types stored in the eeprom
 */
#define EEPROM_DIGITALINPUT		0
#define EEPROM_DIGITALOUTPUT		1
#define EEPROM_ANALOGINPUT		2
#define EEPROM_ANALOGOUTPUT		3
#define EEPROM_TIMER			4
#define EEPROM_WATCHDOG			5
#define EEPROM_TIMER_WATCHDOG_COUNTER	10

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

static void apci3501_eeprom_wait(unsigned long iobase)
{
	unsigned char val;

	do {
		val = inb(iobase + AMCC_OP_REG_MCSR_NVCMD);
	} while (val & 0x80);
}

static unsigned short apci3501_eeprom_readw(unsigned long iobase,
					    unsigned short addr)
{
	unsigned short val = 0;
	unsigned char tmp;
	unsigned char i;

	/* Add the offset to the start of the user data */
	addr += NVRAM_USER_DATA_START;

	for (i = 0; i < 2; i++) {
		/* Load the low 8 bit address */
		outb(NVCMD_LOAD_LOW, iobase + AMCC_OP_REG_MCSR_NVCMD);
		apci3501_eeprom_wait(iobase);
		outb((addr + i) & 0xff, iobase + AMCC_OP_REG_MCSR_NVDATA);
		apci3501_eeprom_wait(iobase);

		/* Load the high 8 bit address */
		outb(NVCMD_LOAD_HIGH, iobase + AMCC_OP_REG_MCSR_NVCMD);
		apci3501_eeprom_wait(iobase);
		outb(((addr + i) >> 8) & 0xff,
			iobase + AMCC_OP_REG_MCSR_NVDATA);
		apci3501_eeprom_wait(iobase);

		/* Read the eeprom data byte */
		outb(NVCMD_BEGIN_READ, iobase + AMCC_OP_REG_MCSR_NVCMD);
		apci3501_eeprom_wait(iobase);
		tmp = inb(iobase + AMCC_OP_REG_MCSR_NVDATA);
		apci3501_eeprom_wait(iobase);

		if (i == 0)
			val |= tmp;
		else
			val |= (tmp << 8);
	}

	return val;
}

static int apci3501_eeprom_get_ao_n_chan(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;
	unsigned long iobase = devpriv->i_IobaseAmcc;
	unsigned char nfuncs;
	int i;

	nfuncs = apci3501_eeprom_readw(iobase, 10) & 0xff;

	/* Read functionality details */
	for (i = 0; i < nfuncs; i++) {
		unsigned short offset = i * 4;
		unsigned short addr;
		unsigned char func;
		unsigned short val;

		func = apci3501_eeprom_readw(iobase, 12 + offset) & 0x3f;
		addr = apci3501_eeprom_readw(iobase, 14 + offset);

		if (func == EEPROM_ANALOGOUTPUT) {
			val = apci3501_eeprom_readw(iobase, addr + 10);
			return (val >> 4) & 0x3ff;
		}
	}
	return 0;
}

static int apci3501_eeprom_insn_read(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned short addr = CR_CHAN(insn->chanspec);

	data[0] = apci3501_eeprom_readw(devpriv->i_IobaseAmcc, 2 * addr);

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

static int apci3501_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ao_n_chan;
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
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);

	ao_n_chan = apci3501_eeprom_get_ao_n_chan(dev);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci3501_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	/* Initialize the analog output subdevice */
	s = &dev->subdevices[0];
	if (ao_n_chan) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan	= ao_n_chan;
		s->maxdata	= 0x3fff;
		s->range_table	= &range_apci3501_ao;
		s->insn_config	= i_APCI3501_ConfigAnalogOutput;
		s->insn_write	= i_APCI3501_WriteAnalogOutput;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Initialize the digital input subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 2;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci3501_di_insn_bits;

	/* Initialize the digital output subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 2;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci3501_do_insn_bits;

	/* Initialize the timer/watchdog subdevice */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;
	s->insn_write = i_APCI3501_StartStopWriteTimerCounterWatchdog;
	s->insn_read = i_APCI3501_ReadTimerCounterWatchdog;
	s->insn_config = i_APCI3501_ConfigTimerCounterWatchdog;

	/* Initialize the eeprom subdevice */
	s = &dev->subdevices[4];
	s->type		= COMEDI_SUBD_MEMORY;
	s->subdev_flags	= SDF_READABLE | SDF_INTERNAL;
	s->n_chan	= 256;
	s->maxdata	= 0xffff;
	s->insn_read	= apci3501_eeprom_insn_read;

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
