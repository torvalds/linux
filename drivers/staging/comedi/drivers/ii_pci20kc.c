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
 * 	only ONE PCI-20006 module possible
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

/* XXX needs to use ioremap() for compatibility with 2.4 kernels.  Should also
 * check_mem_region() etc. - fmhess */

#include "../comedidev.h"

#define PCI20000_ID			0x1d
#define PCI20341_ID    			0x77
#define PCI20006_ID      		0xe3
#define PCI20xxx_EMPTY_ID		0xff

#define PCI20000_OFFSET 		0x100
#define PCI20000_MODULES		3

#define PCI20000_DIO_0			0x80
#define PCI20000_DIO_1			0x81
#define PCI20000_DIO_2			0xc0
#define PCI20000_DIO_3			0xc1
#define PCI20000_DIO_CONTROL_01		0x83	/* port 0, 1 control */
#define PCI20000_DIO_CONTROL_23		0xc3	/* port 2, 3 control */
#define PCI20000_DIO_BUFFER		0x82	/* buffer direction & enable */
#define PCI20000_DIO_EOC		0xef	/* even port, control output */
#define PCI20000_DIO_OOC		0xfd	/* odd port, control output */
#define PCI20000_DIO_EIC		0x90	/* even port, control input */
#define PCI20000_DIO_OIC		0x82	/* odd port, control input */
#define DIO_CAND			0x12	/* and bit 1 & 4 of control */
#define DIO_BE				0x01	/* buffer: port enable */
#define DIO_BO				0x04	/* buffer: output */
#define DIO_BI				0x05	/* buffer: input */
#define DIO_PS_0			0x00	/* buffer: port shift 0 */
#define DIO_PS_1			0x01	/* buffer: port shift 1 */
#define DIO_PS_2			0x04	/* buffer: port shift 2 */
#define DIO_PS_3			0x05	/* buffer: port shift 3 */

#define PCI20006_LCHAN0			0x0d
#define PCI20006_STROBE0		0x0b
#define PCI20006_LCHAN1			0x15
#define PCI20006_STROBE1		0x13

#define PCI20341_INIT			0x04
#define PCI20341_REPMODE		0x00	/* single shot mode */
#define PCI20341_PACER			0x00	/* Hardware Pacer disabled */
#define PCI20341_CHAN_NR		0x04	/* number of input channels */
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

union pci20xxx_subdev_private {
	void *iobase;
	struct {
		void *iobase;
		const struct comedi_lrange *ao_range_list[2];
					/* range of channels of ao module */
		unsigned int last_data[2];
	} pci20006;
	struct {
		void *iobase;
		int timebase;
		int settling_time;
		int ai_gain;
	} pci20341;
};

struct pci20xxx_private {

	void *ioaddr;
	union pci20xxx_subdev_private subdev_private[PCI20000_MODULES];
};

#define devpriv ((struct pci20xxx_private *)dev->private)
#define CHAN (CR_CHAN(it->chanlist[0]))

static int pci20xxx_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it);
static int pci20xxx_detach(struct comedi_device *dev);

static struct comedi_driver driver_pci20xxx = {
	.driver_name = "ii_pci20kc",
	.module = THIS_MODULE,
	.attach = pci20xxx_attach,
	.detach = pci20xxx_detach,
};

static int pci20006_init(struct comedi_device *dev, struct comedi_subdevice *s,
			 int opt0, int opt1);
static int pci20341_init(struct comedi_device *dev, struct comedi_subdevice *s,
			 int opt0, int opt1);
static int pci20xxx_dio_init(struct comedi_device *dev,
			     struct comedi_subdevice *s);

/*
  options[0]	Board base address
  options[1]	IRQ
  options[2]	first option for module 1
  options[3]	second option for module 1
  options[4]	first option for module 2
  options[5]	second option for module 2
  options[6]	first option for module 3
  options[7]	second option for module 3

  options for PCI-20341M:
  first		Analog input gain configuration
		0 == 1
		1 == 10
		2 == 100
		3 == 200

  options for PCI-20006M:
  first		Analog output channel 0 range configuration
		0 == bipolar 10  (-10V -- +10V)
		1 == unipolar 10V  (0V -- +10V)
		2 == bipolar 5V  (-5V -- +5V)
  second	Analog output channel 1 range configuration
		0 == bipolar 10  (-10V -- +10V)
		1 == unipolar 10V  (0V -- +10V)
		2 == bipolar 5V  (-5V -- +5V)
*/
static int pci20xxx_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	unsigned char i;
	int ret;
	int id;
	struct comedi_subdevice *s;
	union pci20xxx_subdev_private *sdp;

	ret = alloc_subdevices(dev, 1 + PCI20000_MODULES);
	if (ret < 0)
		return ret;

	ret = alloc_private(dev, sizeof(struct pci20xxx_private));
	if (ret < 0)
		return ret;

	devpriv->ioaddr = (void *)(unsigned long)it->options[0];
	dev->board_name = "pci20kc";

	/* Check PCI-20001 C-2A Carrier Board ID */
	if ((readb(devpriv->ioaddr) & PCI20000_ID) != PCI20000_ID) {
		printk(KERN_WARNING "comedi%d: ii_pci20kc PCI-20001"
		       " C-2A Carrier Board at base=0x%p not found !\n",
		       dev->minor, devpriv->ioaddr);
		return -EINVAL;
	}
	printk(KERN_INFO "comedi%d: ii_pci20kc: PCI-20001 C-2A at base=0x%p\n",
	       dev->minor, devpriv->ioaddr);

	for (i = 0; i < PCI20000_MODULES; i++) {
		s = dev->subdevices + i;
		id = readb(devpriv->ioaddr + (i + 1) * PCI20000_OFFSET);
		s->private = devpriv->subdev_private + i;
		sdp = s->private;
		switch (id) {
		case PCI20006_ID:
			sdp->pci20006.iobase =
			    devpriv->ioaddr + (i + 1) * PCI20000_OFFSET;
			pci20006_init(dev, s, it->options[2 * i + 2],
				      it->options[2 * i + 3]);
			printk(KERN_INFO "comedi%d: "
			       "ii_pci20kc PCI-20006 module in slot %d \n",
			       dev->minor, i + 1);
			break;
		case PCI20341_ID:
			sdp->pci20341.iobase =
			    devpriv->ioaddr + (i + 1) * PCI20000_OFFSET;
			pci20341_init(dev, s, it->options[2 * i + 2],
				      it->options[2 * i + 3]);
			printk(KERN_INFO "comedi%d: "
			       "ii_pci20kc PCI-20341 module in slot %d \n",
			       dev->minor, i + 1);
			break;
		default:
			printk(KERN_WARNING "ii_pci20kc: unknown module "
			       "code 0x%02x in slot %d: module disabled\n",
			       id, i); /* XXX this looks like a bug! i + 1 ?? */
			/* fall through */
		case PCI20xxx_EMPTY_ID:
			s->type = COMEDI_SUBD_UNUSED;
			break;
		}
	}

	/* initialize struct pci20xxx_private */
	pci20xxx_dio_init(dev, dev->subdevices + PCI20000_MODULES);

	return 1;
}

static int pci20xxx_detach(struct comedi_device *dev)
{
	printk(KERN_INFO "comedi%d: pci20xxx: remove\n", dev->minor);

	return 0;
}

/* pci20006m */

static int pci20006_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);
static int pci20006_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data);

static const struct comedi_lrange *pci20006_range_list[] = {
	&range_bipolar10,
	&range_unipolar10,
	&range_bipolar5,
};

static int pci20006_init(struct comedi_device *dev, struct comedi_subdevice *s,
			 int opt0, int opt1)
{
	union pci20xxx_subdev_private *sdp = s->private;

	if (opt0 < 0 || opt0 > 2)
		opt0 = 0;
	if (opt1 < 0 || opt1 > 2)
		opt1 = 0;

	sdp->pci20006.ao_range_list[0] = pci20006_range_list[opt0];
	sdp->pci20006.ao_range_list[1] = pci20006_range_list[opt1];

	/* ao subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->len_chanlist = 2;
	s->insn_read = pci20006_insn_read;
	s->insn_write = pci20006_insn_write;
	s->maxdata = 0xffff;
	s->range_table_list = sdp->pci20006.ao_range_list;
	return 0;
}

static int pci20006_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	union pci20xxx_subdev_private *sdp = s->private;

	data[0] = sdp->pci20006.last_data[CR_CHAN(insn->chanspec)];

	return 1;
}

static int pci20006_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	union pci20xxx_subdev_private *sdp = s->private;
	int hi, lo;
	unsigned int boarddata;

	sdp->pci20006.last_data[CR_CHAN(insn->chanspec)] = data[0];
	boarddata = (((unsigned int)data[0] + 0x8000) & 0xffff);
						/* comedi-data -> board-data */
	lo = (boarddata & 0xff);
	hi = ((boarddata >> 8) & 0xff);

	switch (CR_CHAN(insn->chanspec)) {
	case 0:
		writeb(lo, sdp->iobase + PCI20006_LCHAN0);
		writeb(hi, sdp->iobase + PCI20006_LCHAN0 + 1);
		writeb(0x00, sdp->iobase + PCI20006_STROBE0);
		break;
	case 1:
		writeb(lo, sdp->iobase + PCI20006_LCHAN1);
		writeb(hi, sdp->iobase + PCI20006_LCHAN1 + 1);
		writeb(0x00, sdp->iobase + PCI20006_STROBE1);
		break;
	default:
		printk(KERN_WARNING
		       " comedi%d: pci20xxx: ao channel Error!\n", dev->minor);
		return -EINVAL;
	}

	return 1;
}

/* PCI20341M */

static int pci20341_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);

static const int pci20341_timebase[] = { 0x00, 0x00, 0x00, 0x04 };
static const int pci20341_settling_time[] = { 0x58, 0x58, 0x93, 0x99 };

static const struct comedi_lrange range_bipolar0_5 = { 1, {BIP_RANGE(0.5)} };
static const struct comedi_lrange range_bipolar0_05 = { 1, {BIP_RANGE(0.05)} };
static const struct comedi_lrange range_bipolar0_025 = { 1, {BIP_RANGE(0.025)} };

static const struct comedi_lrange *const pci20341_ranges[] = {
	&range_bipolar5,
	&range_bipolar0_5,
	&range_bipolar0_05,
	&range_bipolar0_025,
};

static int pci20341_init(struct comedi_device *dev, struct comedi_subdevice *s,
			 int opt0, int opt1)
{
	union pci20xxx_subdev_private *sdp = s->private;
	int option;

	/* options handling */
	if (opt0 < 0 || opt0 > 3)
		opt0 = 0;
	sdp->pci20341.timebase = pci20341_timebase[opt0];
	sdp->pci20341.settling_time = pci20341_settling_time[opt0];

	/* ai subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = PCI20341_CHAN_NR;
	s->len_chanlist = PCI20341_SCANLIST;
	s->insn_read = pci20341_insn_read;
	s->maxdata = 0xffff;
	s->range_table = pci20341_ranges[opt0];

	option = sdp->pci20341.timebase | PCI20341_REPMODE;	/* depends on gain, trigger, repetition mode */

	writeb(PCI20341_INIT, sdp->iobase + PCI20341_CONFIG_REG);	/* initialize Module */
	writeb(PCI20341_PACER, sdp->iobase + PCI20341_MOD_STATUS);	/* set Pacer */
	writeb(option, sdp->iobase + PCI20341_OPT_REG);	/* option register */
	writeb(sdp->pci20341.settling_time, sdp->iobase + PCI20341_SET_TIME_REG);	/* settling time counter */
	/* trigger not implemented */
	return 0;
}

static int pci20341_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	union pci20xxx_subdev_private *sdp = s->private;
	unsigned int i = 0, j = 0;
	int lo, hi;
	unsigned char eoc;	/* end of conversion */
	unsigned int clb;	/* channel list byte */
	unsigned int boarddata;

	writeb(1, sdp->iobase + PCI20341_LCHAN_ADDR_REG);	/* write number of input channels */
	clb = PCI20341_DAISY_CHAIN | PCI20341_MUX | (sdp->pci20341.ai_gain << 3)
	    | CR_CHAN(insn->chanspec);
	writeb(clb, sdp->iobase + PCI20341_CHAN_LIST);
	writeb(0x00, sdp->iobase + PCI20341_CC_RESET);	/* reset settling time counter and trigger delay counter */
	writeb(0x00, sdp->iobase + PCI20341_CHAN_RESET);

	/* generate Pacer */

	for (i = 0; i < insn->n; i++) {
		/* data polling isn't the niciest way to get the data, I know,
		 * but there are only 6 cycles (mean) and it is easier than
		 * the whole interrupt stuff
		 */
		j = 0;
		readb(sdp->iobase + PCI20341_SOFT_PACER);	/* generate Pacer */
		eoc = readb(sdp->iobase + PCI20341_STATUS_REG);
		while ((eoc < 0x80) && j < 100) {	/* poll Interrupt Flag */
			j++;
			eoc = readb(sdp->iobase + PCI20341_STATUS_REG);
		}
		if (j >= 100) {
			printk(KERN_WARNING
			       "comedi%d:  pci20xxx: "
			       "AI interrupt channel %i polling exit !\n",
			       dev->minor, i);
			return -EINVAL;
		}
		lo = readb(sdp->iobase + PCI20341_LDATA);
		hi = readb(sdp->iobase + PCI20341_LDATA + 1);
		boarddata = lo + 0x100 * hi;
		data[i] = (short)((boarddata + 0x8000) & 0xffff);	/* board-data -> comedi-data */
	}

	return i;
}

/* native DIO */

static void pci20xxx_dio_config(struct comedi_device *dev,
				struct comedi_subdevice *s);
static int pci20xxx_dio_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data);
static int pci20xxx_dio_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data);

/* initialize struct pci20xxx_private */
static int pci20xxx_dio_init(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{

	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 32;
	s->insn_bits = pci20xxx_dio_insn_bits;
	s->insn_config = pci20xxx_dio_insn_config;
	s->maxdata = 1;
	s->len_chanlist = 32;
	s->range_table = &range_digital;
	s->io_bits = 0;

	/* digital I/O lines default to input on board reset. */
	pci20xxx_dio_config(dev, s);

	return 0;
}

static int pci20xxx_dio_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	int mask, bits;

	mask = 1 << CR_CHAN(insn->chanspec);
	if (mask & 0x000000ff)
		bits = 0x000000ff;
	else if (mask & 0x0000ff00)
		bits = 0x0000ff00;
	else if (mask & 0x00ff0000)
		bits = 0x00ff0000;
	else
		bits = 0xff000000;
	if (data[0])
		s->io_bits |= bits;
	else
		s->io_bits &= ~bits;
	pci20xxx_dio_config(dev, s);

	return 1;
}

static int pci20xxx_dio_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	unsigned int mask = data[0];

	s->state &= ~mask;
	s->state |= (mask & data[1]);

	mask &= s->io_bits;
	if (mask & 0x000000ff)
		writeb((s->state >> 0) & 0xff,
		       devpriv->ioaddr + PCI20000_DIO_0);
	if (mask & 0x0000ff00)
		writeb((s->state >> 8) & 0xff,
		       devpriv->ioaddr + PCI20000_DIO_1);
	if (mask & 0x00ff0000)
		writeb((s->state >> 16) & 0xff,
		       devpriv->ioaddr + PCI20000_DIO_2);
	if (mask & 0xff000000)
		writeb((s->state >> 24) & 0xff,
		       devpriv->ioaddr + PCI20000_DIO_3);

	data[1] = readb(devpriv->ioaddr + PCI20000_DIO_0);
	data[1] |= readb(devpriv->ioaddr + PCI20000_DIO_1) << 8;
	data[1] |= readb(devpriv->ioaddr + PCI20000_DIO_2) << 16;
	data[1] |= readb(devpriv->ioaddr + PCI20000_DIO_3) << 24;

	return 2;
}

static void pci20xxx_dio_config(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	unsigned char control_01;
	unsigned char control_23;
	unsigned char buffer;

	control_01 = readb(devpriv->ioaddr + PCI20000_DIO_CONTROL_01);
	control_23 = readb(devpriv->ioaddr + PCI20000_DIO_CONTROL_23);
	buffer = readb(devpriv->ioaddr + PCI20000_DIO_BUFFER);

	if (s->io_bits & 0x000000ff) {
		/* output port 0 */
		control_01 &= PCI20000_DIO_EOC;
		buffer = (buffer & (~(DIO_BE << DIO_PS_0))) | (DIO_BO <<
							       DIO_PS_0);
	} else {
		/* input port 0 */
		control_01 = (control_01 & DIO_CAND) | PCI20000_DIO_EIC;
		buffer = (buffer & (~(DIO_BI << DIO_PS_0)));
	}
	if (s->io_bits & 0x0000ff00) {
		/* output port 1 */
		control_01 &= PCI20000_DIO_OOC;
		buffer = (buffer & (~(DIO_BE << DIO_PS_1))) | (DIO_BO <<
							       DIO_PS_1);
	} else {
		/* input port 1 */
		control_01 = (control_01 & DIO_CAND) | PCI20000_DIO_OIC;
		buffer = (buffer & (~(DIO_BI << DIO_PS_1)));
	}
	if (s->io_bits & 0x00ff0000) {
		/* output port 2 */
		control_23 &= PCI20000_DIO_EOC;
		buffer = (buffer & (~(DIO_BE << DIO_PS_2))) | (DIO_BO <<
							       DIO_PS_2);
	} else {
		/* input port 2 */
		control_23 = (control_23 & DIO_CAND) | PCI20000_DIO_EIC;
		buffer = (buffer & (~(DIO_BI << DIO_PS_2)));
	}
	if (s->io_bits & 0xff000000) {
		/* output port 3 */
		control_23 &= PCI20000_DIO_OOC;
		buffer = (buffer & (~(DIO_BE << DIO_PS_3))) | (DIO_BO <<
							       DIO_PS_3);
	} else {
		/* input port 3 */
		control_23 = (control_23 & DIO_CAND) | PCI20000_DIO_OIC;
		buffer = (buffer & (~(DIO_BI << DIO_PS_3)));
	}
	writeb(control_01, devpriv->ioaddr + PCI20000_DIO_CONTROL_01);
	writeb(control_23, devpriv->ioaddr + PCI20000_DIO_CONTROL_23);
	writeb(buffer, devpriv->ioaddr + PCI20000_DIO_BUFFER);
}

#if 0
static void pci20xxx_do(struct comedi_device *dev, struct comedi_subdevice *s)
{
	/* XXX if the channel is configured for input, does this
	   do bad things? */
	/* XXX it would be a good idea to only update the registers
	   that _need_ to be updated.  This requires changes to
	   comedi, however. */
	writeb((s->state >> 0) & 0xff, devpriv->ioaddr + PCI20000_DIO_0);
	writeb((s->state >> 8) & 0xff, devpriv->ioaddr + PCI20000_DIO_1);
	writeb((s->state >> 16) & 0xff, devpriv->ioaddr + PCI20000_DIO_2);
	writeb((s->state >> 24) & 0xff, devpriv->ioaddr + PCI20000_DIO_3);
}

static unsigned int pci20xxx_di(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	/* XXX same note as above */
	unsigned int bits;

	bits = readb(devpriv->ioaddr + PCI20000_DIO_0);
	bits |= readb(devpriv->ioaddr + PCI20000_DIO_1) << 8;
	bits |= readb(devpriv->ioaddr + PCI20000_DIO_2) << 16;
	bits |= readb(devpriv->ioaddr + PCI20000_DIO_3) << 24;

	return bits;
}
#endif

COMEDI_INITCLEANUP(driver_pci20xxx);
