/*
 *	comedi/drivers/ii_pci20kc.c
 *	Driver for Intelligent Instruments PCI-20001C carrier board
 *	and modules.
 *
 *	Copyright (C) 2000 Markus Kempf <kempf@matsci.uni-sb.de>
 *	with suggestions from David Schleef
 *			16.06.2000
 *
 *	Linux device driver for COMEDI
 *	Intelligent Instrumentation
 *	PCI-20001 C-2A Carrier Board
 *	PCI-20341 M-1A 16-Bit analog input module
 *				- differential
 *				- range (-5V - +5V)
 *				- 16 bit
 *	PCI-20006 M-2 16-Bit analog output module
 *				- ranges (-10V - +10V) (0V - +10V) (-5V - +5V)
 *				- 16 bit
 *
 *	only ONE PCI-20341 module possible
 *	only ONE PCI-20006 module possible
 *	no extern trigger implemented
 *
 *	NOT WORKING (but soon) only 4 on-board differential channels supported
 *	NOT WORKING (but soon) only ONE di-port and ONE do-port supported
 *			       instead of 4 digital ports
 *	di-port == Port 0
 *	do-port == Port 1
 *
 *	The state of this driver is only a starting point for a complete
 *	COMEDI-driver. The final driver should support all features of the
 *	carrier board and modules.
 *
 *	The test configuration:
 *
 *	kernel 2.2.14 with RTAI v1.2  and patch-2.2.14rthal2
 *	COMEDI 0.7.45
 *	COMEDILIB 0.7.9
 *
 */
/*
Driver: ii_pci20kc
Description: Intelligent Instruments PCI-20001C carrier board
Author: Markus Kempf <kempf@matsci.uni-sb.de>
Devices: [Intelligent Instrumentation] PCI-20001C (ii_pci20kc)
Status: works

Supports the PCI-20001 C-2a Carrier board, and could probably support
the other carrier boards with small modifications.  Modules supported
are:
	PCI-20006 M-2 16-bit analog output module
	PCI-20341 M-1A 16-bit analog input module

Options:
  0   Board base address
  1   IRQ
  2   first option for module 1
  3   second option for module 1
  4   first option for module 2
  5   second option for module 2
  6   first option for module 3
  7   second option for module 3

options for PCI-20006M:
  first:   Analog output channel 0 range configuration
	     0  bipolar 10  (-10V -- +10V)
	     1  unipolar 10  (0V -- +10V)
	     2  bipolar 5  (-5V -- 5V)
  second:  Analog output channel 1 range configuration

options for PCI-20341M:
  first:   Analog input gain configuration
	     0  1
	     1  10
	     2  100
	     3  200
*/

#include <linux/module.h>
#include "../comedidev.h"

/*
 * Register I/O map
 */
#define II20K_MOD_OFFSET		0x100
#define II20K_ID_REG			0x00
#define II20K_ID_MOD1_EMPTY		(1 << 7)
#define II20K_ID_MOD2_EMPTY		(1 << 6)
#define II20K_ID_MOD3_EMPTY		(1 << 5)
#define II20K_ID_MASK			0x1f
#define II20K_ID_PCI20001C_1A		0x1b	/* no on-board DIO */
#define II20K_ID_PCI20001C_2A		0x1d	/* on-board DIO */
#define II20K_MOD_STATUS_REG		0x40
#define II20K_MOD_STATUS_IRQ_MOD1	(1 << 7)
#define II20K_MOD_STATUS_IRQ_MOD2	(1 << 6)
#define II20K_MOD_STATUS_IRQ_MOD3	(1 << 5)
#define II20K_DIO0_REG			0x80
#define II20K_DIO1_REG			0x81
#define II20K_DIR_ENA_REG		0x82
#define II20K_DIR_DIO3_OUT		(1 << 7)
#define II20K_DIR_DIO2_OUT		(1 << 6)
#define II20K_BUF_DISAB_DIO3		(1 << 5)
#define II20K_BUF_DISAB_DIO2		(1 << 4)
#define II20K_DIR_DIO1_OUT		(1 << 3)
#define II20K_DIR_DIO0_OUT		(1 << 2)
#define II20K_BUF_DISAB_DIO1		(1 << 1)
#define II20K_BUF_DISAB_DIO0		(1 << 0)
#define II20K_CTRL01_REG		0x83
#define II20K_CTRL01_SET		(1 << 7)
#define II20K_CTRL01_DIO0_IN		(1 << 4)
#define II20K_CTRL01_DIO1_IN		(1 << 1)
#define II20K_DIO2_REG			0xc0
#define II20K_DIO3_REG			0xc1
#define II20K_CTRL23_REG		0xc3
#define II20K_CTRL23_SET		(1 << 7)
#define II20K_CTRL23_DIO2_IN		(1 << 4)
#define II20K_CTRL23_DIO3_IN		(1 << 1)

#define PCI20341_ID			0x77
#define PCI20006_ID			0xe3
#define PCI20xxx_EMPTY_ID		0xff

#define PCI20006_LCHAN0			0x0d
#define PCI20006_STROBE0		0x0b
#define PCI20006_LCHAN1			0x15
#define PCI20006_STROBE1		0x13

#define PCI20341_INIT			0x04
#define PCI20341_REPMODE		0x00	/* single shot mode */
#define PCI20341_PACER			0x00	/* Hardware Pacer disabled */
#define PCI20341_CONFIG_REG		0x10
#define PCI20341_MOD_STATUS		0x01
#define PCI20341_OPT_REG		0x11
#define PCI20341_SET_TIME_REG		0x15
#define PCI20341_LCHAN_ADDR_REG		0x13
#define PCI20341_CHAN_LIST		0x80
#define PCI20341_CC_RESET		0x1b
#define PCI20341_CHAN_RESET		0x19
#define PCI20341_SOFT_PACER		0x04
#define PCI20341_STATUS_REG		0x12
#define PCI20341_LDATA			0x02
#define PCI20341_DAISY_CHAIN		0x20	/* On-board inputs only */
#define PCI20341_MUX			0x04	/* Enable on-board MUX */
#define PCI20341_SCANLIST		0x80	/* Channel/Gain Scan List */

static const struct comedi_lrange range_bipolar0_5 = {
	1, {
		BIP_RANGE(0.5)
	}
};

static const struct comedi_lrange range_bipolar0_05 = {
	1, {
		BIP_RANGE(0.05)
	}
};

static const struct comedi_lrange range_bipolar0_025 = {
	1, {
		BIP_RANGE(0.025)
	}
};

static const struct comedi_lrange *ii20k_ao_ranges[] = {
	&range_bipolar10,
	&range_unipolar10,
	&range_bipolar5,
};

static const struct comedi_lrange *const ii20k_ai_ranges[] = {
	&range_bipolar5,
	&range_bipolar0_5,
	&range_bipolar0_05,
	&range_bipolar0_025,
};

static const int pci20341_timebase[] = { 0x00, 0x00, 0x00, 0x04 };
static const int pci20341_settling_time[] = { 0x58, 0x58, 0x93, 0x99 };

struct ii20k_ao_private {
	const struct comedi_lrange *ao_range_list[2];
	unsigned int last_data[2];
};

struct ii20k_ai_private {
	int timebase;
	int settling_time;
	int ai_gain;
};

struct pci20xxx_private {
	void __iomem *ioaddr;
};

static void __iomem *ii20k_module_iobase(struct comedi_device *dev,
					 struct comedi_subdevice *s)
{
	struct pci20xxx_private *devpriv = dev->private;

	return devpriv->ioaddr + (s->index + 1) * II20K_MOD_OFFSET;
}

static int ii20k_ao_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct ii20k_ao_private *ao_spriv = s->private;

	data[0] = ao_spriv->last_data[CR_CHAN(insn->chanspec)];

	return 1;
}

static int ii20k_ao_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct ii20k_ao_private *ao_spriv = s->private;
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	int hi, lo;
	unsigned int boarddata;

	ao_spriv->last_data[CR_CHAN(insn->chanspec)] = data[0];
	boarddata = (((unsigned int)data[0] + 0x8000) & 0xffff);
						/* comedi-data -> board-data */
	lo = (boarddata & 0xff);
	hi = ((boarddata >> 8) & 0xff);

	switch (CR_CHAN(insn->chanspec)) {
	case 0:
		writeb(lo, iobase + PCI20006_LCHAN0);
		writeb(hi, iobase + PCI20006_LCHAN0 + 1);
		writeb(0x00, iobase + PCI20006_STROBE0);
		break;
	case 1:
		writeb(lo, iobase + PCI20006_LCHAN1);
		writeb(hi, iobase + PCI20006_LCHAN1 + 1);
		writeb(0x00, iobase + PCI20006_STROBE1);
		break;
	default:
		dev_warn(dev->class_dev, "ao channel Error!\n");
		return -EINVAL;
	}

	return 1;
}

static int ii20k_ai_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct ii20k_ai_private *ai_spriv = s->private;
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	unsigned int i = 0, j = 0;
	int lo, hi;
	unsigned char eoc;	/* end of conversion */
	unsigned int clb;	/* channel list byte */
	unsigned int boarddata;

	/* write number of input channels */
	writeb(1, iobase + PCI20341_LCHAN_ADDR_REG);
	clb = PCI20341_DAISY_CHAIN | PCI20341_MUX | (ai_spriv->ai_gain << 3)
	    | CR_CHAN(insn->chanspec);
	writeb(clb, iobase + PCI20341_CHAN_LIST);

	/* reset settling time counter and trigger delay counter */
	writeb(0x00, iobase + PCI20341_CC_RESET);

	writeb(0x00, iobase + PCI20341_CHAN_RESET);

	/* generate Pacer */

	for (i = 0; i < insn->n; i++) {
		/* data polling isn't the niciest way to get the data, I know,
		 * but there are only 6 cycles (mean) and it is easier than
		 * the whole interrupt stuff
		 */
		j = 0;
		/* generate Pacer */
		readb(iobase + PCI20341_SOFT_PACER);

		eoc = readb(iobase + PCI20341_STATUS_REG);
		/* poll Interrupt Flag */
		while ((eoc < 0x80) && j < 100) {
			j++;
			eoc = readb(iobase + PCI20341_STATUS_REG);
		}
		if (j >= 100) {
			dev_warn(dev->class_dev,
				 "AI interrupt channel %i polling exit !\n", i);
			return -EINVAL;
		}
		lo = readb(iobase + PCI20341_LDATA);
		hi = readb(iobase + PCI20341_LDATA + 1);
		boarddata = lo + 0x100 * hi;

		/* board-data -> comedi-data */
		data[i] = (short)((boarddata + 0x8000) & 0xffff);
	}

	return i;
}

static void ii20k_ai_init(struct comedi_device *dev,
			  struct comedi_subdevice *s)
{
	struct ii20k_ai_private *ai_spriv = s->private;
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	unsigned char option;

	/* depends on gain, trigger, repetition mode */
	option = ai_spriv->timebase | PCI20341_REPMODE;

	/* initialize Module */
	writeb(PCI20341_INIT, iobase + PCI20341_CONFIG_REG);
	/* set Pacer */
	writeb(PCI20341_PACER, iobase + PCI20341_MOD_STATUS);
	/* option register */
	writeb(option, iobase + PCI20341_OPT_REG);
	/* settling time counter */
	writeb(ai_spriv->settling_time, iobase + PCI20341_SET_TIME_REG);
	/* trigger not implemented */
}

static void ii20k_dio_config(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pci20xxx_private *devpriv = dev->private;
	unsigned char ctrl01 = 0;
	unsigned char ctrl23 = 0;
	unsigned char dir_ena = 0;

	/* port 0 - channels 0-7 */
	if (s->io_bits & 0x000000ff) {
		/* output port */
		ctrl01 &= ~II20K_CTRL01_DIO0_IN;
		dir_ena &= ~II20K_BUF_DISAB_DIO0;
		dir_ena |= II20K_DIR_DIO0_OUT;
	} else {
		/* input port */
		ctrl01 |= II20K_CTRL01_DIO0_IN;
		dir_ena &= ~II20K_DIR_DIO0_OUT;
	}

	/* port 1 - channels 8-15 */
	if (s->io_bits & 0x0000ff00) {
		/* output port */
		ctrl01 &= ~II20K_CTRL01_DIO1_IN;
		dir_ena &= ~II20K_BUF_DISAB_DIO1;
		dir_ena |= II20K_DIR_DIO1_OUT;
	} else {
		/* input port */
		ctrl01 |= II20K_CTRL01_DIO1_IN;
		dir_ena &= ~II20K_DIR_DIO1_OUT;
	}

	/* port 2 - channels 16-23 */
	if (s->io_bits & 0x00ff0000) {
		/* output port */
		ctrl23 &= ~II20K_CTRL23_DIO2_IN;
		dir_ena &= ~II20K_BUF_DISAB_DIO2;
		dir_ena |= II20K_DIR_DIO2_OUT;
	} else {
		/* input port */
		ctrl23 |= II20K_CTRL23_DIO2_IN;
		dir_ena &= ~II20K_DIR_DIO2_OUT;
	}

	/* port 3 - channels 24-31 */
	if (s->io_bits & 0xff000000) {
		/* output port */
		ctrl23 &= ~II20K_CTRL23_DIO3_IN;
		dir_ena &= ~II20K_BUF_DISAB_DIO3;
		dir_ena |= II20K_DIR_DIO3_OUT;
	} else {
		/* input port */
		ctrl23 |= II20K_CTRL23_DIO3_IN;
		dir_ena &= ~II20K_DIR_DIO3_OUT;
	}

	ctrl23 |= II20K_CTRL01_SET;
	ctrl23 |= II20K_CTRL23_SET;

	/* order is important */
	writeb(ctrl01, devpriv->ioaddr + II20K_CTRL01_REG);
	writeb(ctrl23, devpriv->ioaddr + II20K_CTRL23_REG);
	writeb(dir_ena, devpriv->ioaddr + II20K_DIR_ENA_REG);
}

static int ii20k_dio_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int mask = 1 << CR_CHAN(insn->chanspec);
	unsigned int bits;

	if (mask & 0x000000ff)
		bits = 0x000000ff;
	else if (mask & 0x0000ff00)
		bits = 0x0000ff00;
	else if (mask & 0x00ff0000)
		bits = 0x00ff0000;
	else
		bits = 0xff000000;

	switch (data[0]) {
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~bits;
		break;
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= bits;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & bits) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
	default:
		return -EINVAL;
	}

	ii20k_dio_config(dev, s);

	return insn->n;
}

static int ii20k_dio_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct pci20xxx_private *devpriv = dev->private;
	unsigned int mask = data[0] & s->io_bits;	/* outputs only */
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		if (mask & 0x000000ff)
			writeb((s->state >> 0) & 0xff,
			       devpriv->ioaddr + II20K_DIO0_REG);
		if (mask & 0x0000ff00)
			writeb((s->state >> 8) & 0xff,
			       devpriv->ioaddr + II20K_DIO1_REG);
		if (mask & 0x00ff0000)
			writeb((s->state >> 16) & 0xff,
			       devpriv->ioaddr + II20K_DIO2_REG);
		if (mask & 0xff000000)
			writeb((s->state >> 24) & 0xff,
			       devpriv->ioaddr + II20K_DIO3_REG);
	}

	data[1] = readb(devpriv->ioaddr + II20K_DIO0_REG);
	data[1] |= readb(devpriv->ioaddr + II20K_DIO1_REG) << 8;
	data[1] |= readb(devpriv->ioaddr + II20K_DIO2_REG) << 16;
	data[1] |= readb(devpriv->ioaddr + II20K_DIO3_REG) << 24;

	return insn->n;
}

static int ii20k_init_module(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_devconfig *it)
{
	struct ii20k_ao_private *ao_spriv;
	struct ii20k_ai_private *ai_spriv;
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	unsigned int opt0 = it->options[(2 * s->index) + 2];
	unsigned int opt1 = it->options[(2 * s->index) + 3];
	unsigned char id;

	id = readb(iobase + II20K_ID_REG);
	switch (id) {
	case PCI20006_ID:
		if (opt0 < 0 || opt0 > 2)
			opt0 = 0;
		if (opt1 < 0 || opt1 > 2)
			opt1 = 0;

		ao_spriv = comedi_alloc_spriv(s, sizeof(*ao_spriv));
		if (!ao_spriv)
			return -ENOMEM;

		ao_spriv->ao_range_list[0] = ii20k_ao_ranges[opt0];
		ao_spriv->ao_range_list[1] = ii20k_ao_ranges[opt1];

		/* Analog Output subdevice */
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 2;
		s->maxdata	= 0xffff;
		s->range_table_list = ao_spriv->ao_range_list;
		s->insn_read	= ii20k_ao_insn_read;
		s->insn_write	= ii20k_ao_insn_write;
		break;
	case PCI20341_ID:
		if (opt0 < 0 || opt0 > 3)
			opt0 = 0;

		ai_spriv = comedi_alloc_spriv(s, sizeof(*ai_spriv));
		if (!ai_spriv)
			return -ENOMEM;

		ai_spriv->timebase = pci20341_timebase[opt0];
		ai_spriv->settling_time = pci20341_settling_time[opt0];

		/* Analog Input subdevice */
		s->type		= COMEDI_SUBD_AI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= 4;
		s->maxdata	= 0xffff;
		s->range_table	= ii20k_ai_ranges[opt0];
		s->insn_read	= ii20k_ai_insn_read;

		ii20k_ai_init(dev, s);
		break;
	case PCI20xxx_EMPTY_ID:
	default:
		s->type = COMEDI_SUBD_UNUSED;
		break;
	}

	return 0;
}

static int pci20xxx_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	struct pci20xxx_private *devpriv;
	struct comedi_subdevice *s;
	unsigned char id;
	bool has_dio;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	devpriv->ioaddr = (void __iomem *)(unsigned long)it->options[0];

	id = readb(devpriv->ioaddr + II20K_ID_REG);
	switch (id & II20K_ID_MASK) {
	case II20K_ID_PCI20001C_1A:
		break;
	case II20K_ID_PCI20001C_2A:
		has_dio = true;
		break;
	default:
		return -ENODEV;
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	if (id & II20K_ID_MOD1_EMPTY) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		ret = ii20k_init_module(dev, s, it);
		if (ret)
			return ret;
	}

	s = &dev->subdevices[1];
	if (id & II20K_ID_MOD2_EMPTY) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		ret = ii20k_init_module(dev, s, it);
		if (ret)
			return ret;
	}

	s = &dev->subdevices[2];
	if (id & II20K_ID_MOD3_EMPTY) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		ret = ii20k_init_module(dev, s, it);
		if (ret)
			return ret;
	}

	/* Digital I/O subdevice */
	s = &dev->subdevices[3];
	if (has_dio) {
		s->type		= COMEDI_SUBD_DIO;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
		s->n_chan	= 32;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= ii20k_dio_insn_bits;
		s->insn_config	= ii20k_dio_insn_config;

		/* default all channels to input */
		ii20k_dio_config(dev, s);
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	return 1;
}

static void pci20xxx_detach(struct comedi_device *dev)
{
	/* Nothing to cleanup */
}

static struct comedi_driver pci20xxx_driver = {
	.driver_name	= "ii_pci20kc",
	.module		= THIS_MODULE,
	.attach		= pci20xxx_attach,
	.detach		= pci20xxx_detach,
};
module_comedi_driver(pci20xxx_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
