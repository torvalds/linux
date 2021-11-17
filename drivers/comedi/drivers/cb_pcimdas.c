// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/drivers/cb_pcimdas.c
 * Comedi driver for Computer Boards PCIM-DAS1602/16 and PCIe-DAS1602/16
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: cb_pcimdas
 * Description: Measurement Computing PCI Migration series boards
 * Devices: [ComputerBoards] PCIM-DAS1602/16 (cb_pcimdas), PCIe-DAS1602/16
 * Author: Richard Bytheway
 * Updated: Mon, 13 Oct 2014 11:57:39 +0000
 * Status: experimental
 *
 * Written to support the PCIM-DAS1602/16 and PCIe-DAS1602/16.
 *
 * Configuration Options:
 *   none
 *
 * Manual configuration of PCI(e) cards is not supported; they are configured
 * automatically.
 *
 * Developed from cb_pcidas and skel by Richard Bytheway (mocelet@sucs.org).
 * Only supports DIO, AO and simple AI in it's present form.
 * No interrupts, multi channel or FIFO AI,
 * although the card looks like it could support this.
 *
 * https://www.mccdaq.com/PDFs/Manuals/pcim-das1602-16.pdf
 * https://www.mccdaq.com/PDFs/Manuals/pcie-das1602-16.pdf
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

#include "comedi_8254.h"
#include "plx9052.h"
#include "8255.h"

/*
 * PCI Bar 1 Register map
 * see plx9052.h for register and bit defines
 */

/*
 * PCI Bar 2 Register map (devpriv->daqio)
 */
#define PCIMDAS_AI_REG			0x00
#define PCIMDAS_AI_SOFTTRIG_REG		0x00
#define PCIMDAS_AO_REG(x)		(0x02 + ((x) * 2))

/*
 * PCI Bar 3 Register map (devpriv->BADR3)
 */
#define PCIMDAS_MUX_REG			0x00
#define PCIMDAS_MUX(_lo, _hi)		((_lo) | ((_hi) << 4))
#define PCIMDAS_DI_DO_REG		0x01
#define PCIMDAS_STATUS_REG		0x02
#define PCIMDAS_STATUS_EOC		BIT(7)
#define PCIMDAS_STATUS_UB		BIT(6)
#define PCIMDAS_STATUS_MUX		BIT(5)
#define PCIMDAS_STATUS_CLK		BIT(4)
#define PCIMDAS_STATUS_TO_CURR_MUX(x)	((x) & 0xf)
#define PCIMDAS_CONV_STATUS_REG		0x03
#define PCIMDAS_CONV_STATUS_EOC		BIT(7)
#define PCIMDAS_CONV_STATUS_EOB		BIT(6)
#define PCIMDAS_CONV_STATUS_EOA		BIT(5)
#define PCIMDAS_CONV_STATUS_FNE		BIT(4)
#define PCIMDAS_CONV_STATUS_FHF		BIT(3)
#define PCIMDAS_CONV_STATUS_OVERRUN	BIT(2)
#define PCIMDAS_IRQ_REG			0x04
#define PCIMDAS_IRQ_INTE		BIT(7)
#define PCIMDAS_IRQ_INT			BIT(6)
#define PCIMDAS_IRQ_OVERRUN		BIT(4)
#define PCIMDAS_IRQ_EOA			BIT(3)
#define PCIMDAS_IRQ_EOA_INT_SEL		BIT(2)
#define PCIMDAS_IRQ_INTSEL(x)		((x) << 0)
#define PCIMDAS_IRQ_INTSEL_EOC		PCIMDAS_IRQ_INTSEL(0)
#define PCIMDAS_IRQ_INTSEL_FNE		PCIMDAS_IRQ_INTSEL(1)
#define PCIMDAS_IRQ_INTSEL_EOB		PCIMDAS_IRQ_INTSEL(2)
#define PCIMDAS_IRQ_INTSEL_FHF_EOA	PCIMDAS_IRQ_INTSEL(3)
#define PCIMDAS_PACER_REG		0x05
#define PCIMDAS_PACER_GATE_STATUS	BIT(6)
#define PCIMDAS_PACER_GATE_POL		BIT(5)
#define PCIMDAS_PACER_GATE_LATCH	BIT(4)
#define PCIMDAS_PACER_GATE_EN		BIT(3)
#define PCIMDAS_PACER_EXT_PACER_POL	BIT(2)
#define PCIMDAS_PACER_SRC(x)		((x) << 0)
#define PCIMDAS_PACER_SRC_POLLED	PCIMDAS_PACER_SRC(0)
#define PCIMDAS_PACER_SRC_EXT		PCIMDAS_PACER_SRC(2)
#define PCIMDAS_PACER_SRC_INT		PCIMDAS_PACER_SRC(3)
#define PCIMDAS_PACER_SRC_MASK		(3 << 0)
#define PCIMDAS_BURST_REG		0x06
#define PCIMDAS_BURST_BME		BIT(1)
#define PCIMDAS_BURST_CONV_EN		BIT(0)
#define PCIMDAS_GAIN_REG		0x07
#define PCIMDAS_8254_BASE		0x08
#define PCIMDAS_USER_CNTR_REG		0x0c
#define PCIMDAS_USER_CNTR_CTR1_CLK_SEL	BIT(0)
#define PCIMDAS_RESIDUE_MSB_REG		0x0d
#define PCIMDAS_RESIDUE_LSB_REG		0x0e

/*
 * PCI Bar 4 Register map (dev->iobase)
 */
#define PCIMDAS_8255_BASE		0x00

static const struct comedi_lrange cb_pcimdas_ai_bip_range = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25)
	}
};

static const struct comedi_lrange cb_pcimdas_ai_uni_range = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

/*
 * The Analog Output range is not programmable. The DAC ranges are
 * jumper-settable on the board. The settings are not software-readable.
 */
static const struct comedi_lrange cb_pcimdas_ao_range = {
	6, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		UNI_RANGE(10),
		UNI_RANGE(5),
		RANGE_ext(-1, 1),
		RANGE_ext(0, 1)
	}
};

/*
 * this structure is for data unique to this hardware driver.  If
 * several hardware drivers keep similar information in this structure,
 * feel free to suggest moving the variable to the struct comedi_device
 * struct.
 */
struct cb_pcimdas_private {
	/* base addresses */
	unsigned long daqio;
	unsigned long BADR3;
};

static int cb_pcimdas_ai_eoc(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned long context)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int status;

	status = inb(devpriv->BADR3 + PCIMDAS_STATUS_REG);
	if ((status & PCIMDAS_STATUS_EOC) == 0)
		return 0;
	return -EBUSY;
}

static int cb_pcimdas_ai_insn_read(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	int n;
	unsigned int d;
	int ret;

	/*  only support sw initiated reads from a single channel */

	/* configure for sw initiated read */
	d = inb(devpriv->BADR3 + PCIMDAS_PACER_REG);
	if ((d & PCIMDAS_PACER_SRC_MASK) != PCIMDAS_PACER_SRC_POLLED) {
		d &= ~PCIMDAS_PACER_SRC_MASK;
		d |= PCIMDAS_PACER_SRC_POLLED;
		outb(d, devpriv->BADR3 + PCIMDAS_PACER_REG);
	}

	/* set bursting off, conversions on */
	outb(PCIMDAS_BURST_CONV_EN, devpriv->BADR3 + PCIMDAS_BURST_REG);

	/* set range */
	outb(range, devpriv->BADR3 + PCIMDAS_GAIN_REG);

	/* set mux for single channel scan */
	outb(PCIMDAS_MUX(chan, chan), devpriv->BADR3 + PCIMDAS_MUX_REG);

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		outw(0, devpriv->daqio + PCIMDAS_AI_SOFTTRIG_REG);

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, cb_pcimdas_ai_eoc, 0);
		if (ret)
			return ret;

		/* read data */
		data[n] = inw(devpriv->daqio + PCIMDAS_AI_REG);
	}

	/* return the number of samples read/written */
	return n;
}

static int cb_pcimdas_ao_insn_write(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, devpriv->daqio + PCIMDAS_AO_REG(chan));
	}
	s->readback[chan] = val;

	return insn->n;
}

static int cb_pcimdas_di_insn_bits(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int val;

	val = inb(devpriv->BADR3 + PCIMDAS_DI_DO_REG);

	data[1] = val & 0x0f;

	return insn->n;
}

static int cb_pcimdas_do_insn_bits(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct cb_pcimdas_private *devpriv = dev->private;

	if (comedi_dio_update_state(s, data))
		outb(s->state, devpriv->BADR3 + PCIMDAS_DI_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static int cb_pcimdas_counter_insn_config(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int ctrl;

	switch (data[0]) {
	case INSN_CONFIG_SET_CLOCK_SRC:
		switch (data[1]) {
		case 0:	/* internal 100 kHz clock */
			ctrl = PCIMDAS_USER_CNTR_CTR1_CLK_SEL;
			break;
		case 1:	/* external clk on pin 21 */
			ctrl = 0;
			break;
		default:
			return -EINVAL;
		}
		outb(ctrl, devpriv->BADR3 + PCIMDAS_USER_CNTR_REG);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		ctrl = inb(devpriv->BADR3 + PCIMDAS_USER_CNTR_REG);
		if (ctrl & PCIMDAS_USER_CNTR_CTR1_CLK_SEL) {
			data[1] = 0;
			data[2] = I8254_OSC_BASE_100KHZ;
		} else {
			data[1] = 1;
			data[2] = 0;
		}
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static unsigned int cb_pcimdas_pacer_clk(struct comedi_device *dev)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int status;

	/* The Pacer Clock jumper selects a 10 MHz or 1 MHz clock */
	status = inb(devpriv->BADR3 + PCIMDAS_STATUS_REG);
	if (status & PCIMDAS_STATUS_CLK)
		return I8254_OSC_BASE_10MHZ;
	return I8254_OSC_BASE_1MHZ;
}

static bool cb_pcimdas_is_ai_se(struct comedi_device *dev)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int status;

	/*
	 * The number of Analog Input channels is set with the
	 * Analog Input Mode Switch on the board. The board can
	 * have 16 single-ended or 8 differential channels.
	 */
	status = inb(devpriv->BADR3 + PCIMDAS_STATUS_REG);
	return status & PCIMDAS_STATUS_MUX;
}

static bool cb_pcimdas_is_ai_uni(struct comedi_device *dev)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int status;

	/*
	 * The Analog Input range polarity is set with the
	 * Analog Input Polarity Switch on the board. The
	 * inputs can be set to Unipolar or Bipolar ranges.
	 */
	status = inb(devpriv->BADR3 + PCIMDAS_STATUS_REG);
	return status & PCIMDAS_STATUS_UB;
}

static int cb_pcimdas_auto_attach(struct comedi_device *dev,
				  unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct cb_pcimdas_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv->daqio = pci_resource_start(pcidev, 2);
	devpriv->BADR3 = pci_resource_start(pcidev, 3);
	dev->iobase = pci_resource_start(pcidev, 4);

	dev->pacer = comedi_8254_init(devpriv->BADR3 + PCIMDAS_8254_BASE,
				      cb_pcimdas_pacer_clk(dev),
				      I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 6);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE;
	if (cb_pcimdas_is_ai_se(dev)) {
		s->subdev_flags	|= SDF_GROUND;
		s->n_chan	= 16;
	} else {
		s->subdev_flags	|= SDF_DIFF;
		s->n_chan	= 8;
	}
	s->maxdata	= 0xffff;
	s->range_table	= cb_pcimdas_is_ai_uni(dev) ? &cb_pcimdas_ai_uni_range
						    : &cb_pcimdas_ai_bip_range;
	s->insn_read	= cb_pcimdas_ai_insn_read;

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 2;
	s->maxdata	= 0xfff;
	s->range_table	= &cb_pcimdas_ao_range;
	s->insn_write	= cb_pcimdas_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital I/O subdevice */
	s = &dev->subdevices[2];
	ret = subdev_8255_init(dev, s, NULL, PCIMDAS_8255_BASE);
	if (ret)
		return ret;

	/* Digital Input subdevice (main connector) */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= cb_pcimdas_di_insn_bits;

	/* Digital Output subdevice (main connector) */
	s = &dev->subdevices[4];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= cb_pcimdas_do_insn_bits;

	/* Counter subdevice (8254) */
	s = &dev->subdevices[5];
	comedi_8254_subdevice_init(s, dev->pacer);

	dev->pacer->insn_config = cb_pcimdas_counter_insn_config;

	/* counters 1 and 2 are used internally for the pacer */
	comedi_8254_set_busy(dev->pacer, 1, true);
	comedi_8254_set_busy(dev->pacer, 2, true);

	return 0;
}

static struct comedi_driver cb_pcimdas_driver = {
	.driver_name	= "cb_pcimdas",
	.module		= THIS_MODULE,
	.auto_attach	= cb_pcimdas_auto_attach,
	.detach		= comedi_pci_detach,
};

static int cb_pcimdas_pci_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &cb_pcimdas_driver,
				      id->driver_data);
}

static const struct pci_device_id cb_pcimdas_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0056) },	/* PCIM-DAS1602/16 */
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0115) },	/* PCIe-DAS1602/16 */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb_pcimdas_pci_table);

static struct pci_driver cb_pcimdas_pci_driver = {
	.name		= "cb_pcimdas",
	.id_table	= cb_pcimdas_pci_table,
	.probe		= cb_pcimdas_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(cb_pcimdas_driver, cb_pcimdas_pci_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for PCIM-DAS1602/16 and PCIe-DAS1602/16");
MODULE_LICENSE("GPL");
