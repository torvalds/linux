/*
 * ii_pci20kc.c
 * Driver for Intelligent Instruments PCI-20001C carrier board and modules.
 *
 * Copyright (C) 2000 Markus Kempf <kempf@matsci.uni-sb.de>
 * with suggestions from David Schleef		16.06.2000
 */

/*
 * Driver: ii_pci20kc
 * Description: Intelligent Instruments PCI-20001C carrier board
 * Devices: (Intelligent Instrumentation) PCI-20001C [ii_pci20kc]
 * Author: Markus Kempf <kempf@matsci.uni-sb.de>
 * Status: works
 *
 * Supports the PCI-20001C-1a and PCI-20001C-2a carrier boards. The
 * -2a version has 32 on-board DIO channels. Three add-on modules
 * can be added to the carrier board for additional functionality.
 *
 * Supported add-on modules:
 *	PCI-20006M-1   1 channel, 16-bit analog output module
 *	PCI-20006M-2   2 channel, 16-bit analog output module
 *	PCI-20341M-1A  4 channel, 16-bit analog input module
 *
 * Options:
 *   0   Board base address
 *   1   IRQ (not-used)
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

#define II20K_ID_PCI20006M_1		0xe2	/* 1 AO channels */
#define II20K_ID_PCI20006M_2		0xe3	/* 2 AO channels */
#define II20K_AO_STRB_REG(x)		(0x0b + ((x) * 0x08))
#define II20K_AO_LSB_REG(x)		(0x0d + ((x) * 0x08))
#define II20K_AO_MSB_REG(x)		(0x0e + ((x) * 0x08))
#define II20K_AO_STRB_BOTH_REG		0x1b

#define II20K_ID_PCI20341M_1		0x77	/* 4 AI channels */
#define II20K_AI_STATUS_CMD_REG		0x01
#define II20K_AI_STATUS_CMD_BUSY	(1 << 7)
#define II20K_AI_STATUS_CMD_HW_ENA	(1 << 1)
#define II20K_AI_STATUS_CMD_EXT_START	(1 << 0)
#define II20K_AI_LSB_REG		0x02
#define II20K_AI_MSB_REG		0x03
#define II20K_AI_PACER_RESET_REG	0x04
#define II20K_AI_16BIT_DATA_REG		0x06
#define II20K_AI_CONF_REG		0x10
#define II20K_AI_CONF_ENA		(1 << 2)
#define II20K_AI_OPT_REG		0x11
#define II20K_AI_OPT_TRIG_ENA		(1 << 5)
#define II20K_AI_OPT_TRIG_INV		(1 << 4)
#define II20K_AI_OPT_TIMEBASE(x)	(((x) & 0x3) << 1)
#define II20K_AI_OPT_BURST_MODE		(1 << 0)
#define II20K_AI_STATUS_REG		0x12
#define II20K_AI_STATUS_INT		(1 << 7)
#define II20K_AI_STATUS_TRIG		(1 << 6)
#define II20K_AI_STATUS_TRIG_ENA	(1 << 5)
#define II20K_AI_STATUS_PACER_ERR	(1 << 2)
#define II20K_AI_STATUS_DATA_ERR	(1 << 1)
#define II20K_AI_STATUS_SET_TIME_ERR	(1 << 0)
#define II20K_AI_LAST_CHAN_ADDR_REG	0x13
#define II20K_AI_CUR_ADDR_REG		0x14
#define II20K_AI_SET_TIME_REG		0x15
#define II20K_AI_DELAY_LSB_REG		0x16
#define II20K_AI_DELAY_MSB_REG		0x17
#define II20K_AI_CHAN_ADV_REG		0x18
#define II20K_AI_CHAN_RESET_REG		0x19
#define II20K_AI_START_TRIG_REG		0x1a
#define II20K_AI_COUNT_RESET_REG	0x1b
#define II20K_AI_CHANLIST_REG		0x80
#define II20K_AI_CHANLIST_ONBOARD_ONLY	(1 << 5)
#define II20K_AI_CHANLIST_GAIN(x)	(((x) & 0x3) << 3)
#define II20K_AI_CHANLIST_MUX_ENA	(1 << 2)
#define II20K_AI_CHANLIST_CHAN(x)	(((x) & 0x3) << 0)
#define II20K_AI_CHANLIST_LEN		0x80

/* the AO range is set by jumpers on the 20006M module */
static const struct comedi_lrange ii20k_ao_ranges = {
	3, {
		BIP_RANGE(5),	/* Chan 0 - W1/W3 in   Chan 1 - W2/W4 in  */
		UNI_RANGE(10),	/* Chan 0 - W1/W3 out  Chan 1 - W2/W4 in  */
		BIP_RANGE(10)	/* Chan 0 - W1/W3 in   Chan 1 - W2/W4 out */
	}
};

static const struct comedi_lrange ii20k_ai_ranges = {
	4, {
		BIP_RANGE(5),		/* gain 1 */
		BIP_RANGE(0.5),		/* gain 10 */
		BIP_RANGE(0.05),	/* gain 100 */
		BIP_RANGE(0.025)	/* gain 200 */
	},
};

struct ii20k_ao_private {
	unsigned int last_data[2];
};

struct ii20k_private {
	void __iomem *ioaddr;
};

static void __iomem *ii20k_module_iobase(struct comedi_device *dev,
					 struct comedi_subdevice *s)
{
	struct ii20k_private *devpriv = dev->private;

	return devpriv->ioaddr + (s->index + 1) * II20K_MOD_OFFSET;
}

static int ii20k_ao_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct ii20k_ao_private *ao_spriv = s->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = ao_spriv->last_data[chan];

	return insn->n;
}

static int ii20k_ao_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct ii20k_ao_private *ao_spriv = s->private;
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = ao_spriv->last_data[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];

		/* munge data */
		val += ((s->maxdata + 1) >> 1);
		val &= s->maxdata;

		writeb(val & 0xff, iobase + II20K_AO_LSB_REG(chan));
		writeb((val >> 8) & 0xff, iobase + II20K_AO_MSB_REG(chan));
		writeb(0x00, iobase + II20K_AO_STRB_REG(chan));
	}

	ao_spriv->last_data[chan] = val;

	return insn->n;
}

static int ii20k_ai_wait_eoc(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     int timeout)
{
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	unsigned char status;

	do {
		status = readb(iobase + II20K_AI_STATUS_REG);
		if ((status & II20K_AI_STATUS_INT) == 0)
			return 0;
	} while (timeout--);

	return -ETIME;
}

static void ii20k_ai_setup(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   unsigned int chanspec)
{
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);
	unsigned char val;

	/* initialize module */
	writeb(II20K_AI_CONF_ENA, iobase + II20K_AI_CONF_REG);

	/* software conversion */
	writeb(0, iobase + II20K_AI_STATUS_CMD_REG);

	/* set the time base for the settling time counter based on the gain */
	val = (range < 3) ? II20K_AI_OPT_TIMEBASE(0) : II20K_AI_OPT_TIMEBASE(2);
	writeb(val, iobase + II20K_AI_OPT_REG);

	/* set the settling time counter based on the gain */
	val = (range < 2) ? 0x58 : (range < 3) ? 0x93 : 0x99;
	writeb(val, iobase + II20K_AI_SET_TIME_REG);

	/* set number of input channels */
	writeb(1, iobase + II20K_AI_LAST_CHAN_ADDR_REG);

	/* set the channel list byte */
	val = II20K_AI_CHANLIST_ONBOARD_ONLY |
	      II20K_AI_CHANLIST_MUX_ENA |
	      II20K_AI_CHANLIST_GAIN(range) |
	      II20K_AI_CHANLIST_CHAN(chan);
	writeb(val, iobase + II20K_AI_CHANLIST_REG);

	/* reset settling time counter and trigger delay counter */
	writeb(0, iobase + II20K_AI_COUNT_RESET_REG);

	/* reset channel scanner */
	writeb(0, iobase + II20K_AI_CHAN_RESET_REG);
}

static int ii20k_ai_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	int ret;
	int i;

	ii20k_ai_setup(dev, s, insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		unsigned int val;

		/* generate a software start convert signal */
		readb(iobase + II20K_AI_PACER_RESET_REG);

		ret = ii20k_ai_wait_eoc(dev, s, 100);
		if (ret)
			return ret;

		val = readb(iobase + II20K_AI_LSB_REG);
		val |= (readb(iobase + II20K_AI_MSB_REG) << 8);

		/* munge two's complement data */
		val += ((s->maxdata + 1) >> 1);
		val &= s->maxdata;

		data[i] = val;
	}

	return insn->n;
}

static void ii20k_dio_config(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct ii20k_private *devpriv = dev->private;
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
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 8)
		mask = 0x000000ff;
	else if (chan < 16)
		mask = 0x0000ff00;
	else if (chan < 24)
		mask = 0x00ff0000;
	else
		mask = 0xff000000;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	ii20k_dio_config(dev, s);

	return insn->n;
}

static int ii20k_dio_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct ii20k_private *devpriv = dev->private;
	unsigned int mask;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
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
			     struct comedi_subdevice *s)
{
	struct ii20k_ao_private *ao_spriv;
	void __iomem *iobase = ii20k_module_iobase(dev, s);
	unsigned char id;

	id = readb(iobase + II20K_ID_REG);
	switch (id) {
	case II20K_ID_PCI20006M_1:
	case II20K_ID_PCI20006M_2:
		ao_spriv = comedi_alloc_spriv(s, sizeof(*ao_spriv));
		if (!ao_spriv)
			return -ENOMEM;

		/* Analog Output subdevice */
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= (id == II20K_ID_PCI20006M_2) ? 2 : 1;
		s->maxdata	= 0xffff;
		s->range_table	= &ii20k_ao_ranges;
		s->insn_read	= ii20k_ao_insn_read;
		s->insn_write	= ii20k_ao_insn_write;
		break;
	case II20K_ID_PCI20341M_1:
		/* Analog Input subdevice */
		s->type		= COMEDI_SUBD_AI;
		s->subdev_flags	= SDF_READABLE | SDF_DIFF;
		s->n_chan	= 4;
		s->maxdata	= 0xffff;
		s->range_table	= &ii20k_ai_ranges;
		s->insn_read	= ii20k_ai_insn_read;
		break;
	default:
		s->type = COMEDI_SUBD_UNUSED;
		break;
	}

	return 0;
}

static int ii20k_attach(struct comedi_device *dev,
			struct comedi_devconfig *it)
{
	struct ii20k_private *devpriv;
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
		ret = ii20k_init_module(dev, s);
		if (ret)
			return ret;
	}

	s = &dev->subdevices[1];
	if (id & II20K_ID_MOD2_EMPTY) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		ret = ii20k_init_module(dev, s);
		if (ret)
			return ret;
	}

	s = &dev->subdevices[2];
	if (id & II20K_ID_MOD3_EMPTY) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		ret = ii20k_init_module(dev, s);
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

	return 0;
}

static struct comedi_driver ii20k_driver = {
	.driver_name	= "ii_pci20kc",
	.module		= THIS_MODULE,
	.attach		= ii20k_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(ii20k_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
