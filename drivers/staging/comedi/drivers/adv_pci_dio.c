// SPDX-License-Identifier: GPL-2.0
/*
 * comedi/drivers/adv_pci_dio.c
 *
 * Author: Michal Dobes <dobes@tesnet.cz>
 *
 *  Hardware driver for Advantech PCI DIO cards.
 */

/*
 * Driver: adv_pci_dio
 * Description: Advantech Digital I/O Cards
 * Devices: [Advantech] PCI-1730 (adv_pci_dio), PCI-1733,
 *   PCI-1734, PCI-1735U, PCI-1736UP, PCI-1739U, PCI-1750,
 *   PCI-1751, PCI-1752, PCI-1753, PCI-1753+PCI-1753E,
 *   PCI-1754, PCI-1756, PCI-1761, PCI-1762
 * Author: Michal Dobes <dobes@tesnet.cz>
 * Updated: Fri, 25 Aug 2017 07:23:06 +0300
 * Status: untested
 *
 * Configuration Options: not applicable, uses PCI auto config
 */

#include <linux/module.h>
#include <linux/delay.h>

#include "../comedi_pci.h"

#include "8255.h"
#include "comedi_8254.h"

/*
 * Register offset definitions
 */

/* PCI-1730, PCI-1733, PCI-1736 interrupt control registers */
#define PCI173X_INT_EN_REG	0x0008	/* R/W: enable/disable */
#define PCI173X_INT_RF_REG	0x000c	/* R/W: falling/rising edge */
#define PCI173X_INT_FLAG_REG	0x0010	/* R: status */
#define PCI173X_INT_CLR_REG	0x0010	/* W: clear */

#define PCI173X_INT_IDI0 0x01  /* IDI0 edge occurred */
#define PCI173X_INT_IDI1 0x02  /* IDI1 edge occurred */
#define PCI173X_INT_DI0  0x04  /* DI0 edge occurred */
#define PCI173X_INT_DI1  0x08  /* DI1 edge occurred */

/* PCI-1739U, PCI-1750, PCI1751 interrupt control registers */
#define PCI1750_INT_REG		0x20	/* R/W: status/control */

/* PCI-1753, PCI-1753E interrupt control registers */
#define PCI1753_INT_REG(x)	(0x10 + (x)) /* R/W: control group 0 to 3 */
#define PCI1753E_INT_REG(x)	(0x30 + (x)) /* R/W: control group 0 to 3 */

/* PCI-1754, PCI-1756 interrupt control registers */
#define PCI1754_INT_REG(x)	(0x08 + (x) * 2) /* R/W: control group 0 to 3 */

/* PCI-1752, PCI-1756 special registers */
#define PCI1752_CFC_REG		0x12	/* R/W: channel freeze function */

/* PCI-1761 interrupt control registers */
#define PCI1761_INT_EN_REG	0x03	/* R/W: enable/disable interrupts */
#define PCI1761_INT_RF_REG	0x04	/* R/W: falling/rising edge */
#define PCI1761_INT_CLR_REG	0x05	/* R/W: clear interrupts */

/* PCI-1762 interrupt control registers */
#define PCI1762_INT_REG		0x06	/* R/W: status/control */

/* maximum number of subdevice descriptions in the boardinfo */
#define PCI_DIO_MAX_DI_SUBDEVS	2	/* 2 x 8/16/32 input channels max */
#define PCI_DIO_MAX_DO_SUBDEVS	2	/* 2 x 8/16/32 output channels max */
#define PCI_DIO_MAX_DIO_SUBDEVG	2	/* 2 x any number of 8255 devices max */
#define PCI_DIO_MAX_IRQ_SUBDEVS	4	/* 4 x 1 input IRQ channels max */

enum pci_dio_boardid {
	TYPE_PCI1730,
	TYPE_PCI1733,
	TYPE_PCI1734,
	TYPE_PCI1735,
	TYPE_PCI1736,
	TYPE_PCI1739,
	TYPE_PCI1750,
	TYPE_PCI1751,
	TYPE_PCI1752,
	TYPE_PCI1753,
	TYPE_PCI1753E,
	TYPE_PCI1754,
	TYPE_PCI1756,
	TYPE_PCI1761,
	TYPE_PCI1762
};

struct diosubd_data {
	int chans;		/*  num of chans or 8255 devices */
	unsigned long addr;	/*  PCI address offset */
};

struct dio_irq_subd_data {
	unsigned short int_en;		/* interrupt enable/status bit */
	unsigned long addr;		/* PCI address offset */
};

struct dio_boardtype {
	const char *name;	/*  board name */
	int nsubdevs;
	struct diosubd_data sdi[PCI_DIO_MAX_DI_SUBDEVS];
	struct diosubd_data sdo[PCI_DIO_MAX_DO_SUBDEVS];
	struct diosubd_data sdio[PCI_DIO_MAX_DIO_SUBDEVG];
	struct dio_irq_subd_data sdirq[PCI_DIO_MAX_IRQ_SUBDEVS];
	unsigned long id_reg;
	unsigned long timer_regbase;
	unsigned int is_16bit:1;
};

static const struct dio_boardtype boardtypes[] = {
	[TYPE_PCI1730] = {
		.name		= "pci1730",
		/* DI, IDI, DO, IDO, ID, IRQ_DI0, IRQ_DI1, IRQ_IDI0, IRQ_IDI1 */
		.nsubdevs	= 9,
		.sdi[0]		= { 16, 0x02, },	/* DI 0-15 */
		.sdi[1]		= { 16, 0x00, },	/* ISO DI 0-15 */
		.sdo[0]		= { 16, 0x02, },	/* DO 0-15 */
		.sdo[1]		= { 16, 0x00, },	/* ISO DO 0-15 */
		.id_reg		= 0x04,
		.sdirq[0]	= { PCI173X_INT_DI0,  0x02, },	/* DI 0 */
		.sdirq[1]	= { PCI173X_INT_DI1,  0x02, },	/* DI 1 */
		.sdirq[2]	= { PCI173X_INT_IDI0, 0x00, },	/* ISO DI 0 */
		.sdirq[3]	= { PCI173X_INT_IDI1, 0x00, },	/* ISO DI 1 */
	},
	[TYPE_PCI1733] = {
		.name		= "pci1733",
		.nsubdevs	= 2,
		.sdi[1]		= { 32, 0x00, },	/* ISO DI 0-31 */
		.id_reg		= 0x04,
	},
	[TYPE_PCI1734] = {
		.name		= "pci1734",
		.nsubdevs	= 2,
		.sdo[1]		= { 32, 0x00, },	/* ISO DO 0-31 */
		.id_reg		= 0x04,
	},
	[TYPE_PCI1735] = {
		.name		= "pci1735",
		.nsubdevs	= 4,
		.sdi[0]		= { 32, 0x00, },	/* DI 0-31 */
		.sdo[0]		= { 32, 0x00, },	/* DO 0-31 */
		.id_reg		= 0x08,
		.timer_regbase	= 0x04,
	},
	[TYPE_PCI1736] = {
		.name		= "pci1736",
		.nsubdevs	= 3,
		.sdi[1]		= { 16, 0x00, },	/* ISO DI 0-15 */
		.sdo[1]		= { 16, 0x00, },	/* ISO DO 0-15 */
		.id_reg		= 0x04,
	},
	[TYPE_PCI1739] = {
		.name		= "pci1739",
		.nsubdevs	= 3,
		.sdio[0]	= { 2, 0x00, },		/* 8255 DIO */
		.id_reg		= 0x08,
	},
	[TYPE_PCI1750] = {
		.name		= "pci1750",
		.nsubdevs	= 2,
		.sdi[1]		= { 16, 0x00, },	/* ISO DI 0-15 */
		.sdo[1]		= { 16, 0x00, },	/* ISO DO 0-15 */
	},
	[TYPE_PCI1751] = {
		.name		= "pci1751",
		.nsubdevs	= 3,
		.sdio[0]	= { 2, 0x00, },		/* 8255 DIO */
		.timer_regbase	= 0x18,
	},
	[TYPE_PCI1752] = {
		.name		= "pci1752",
		.nsubdevs	= 3,
		.sdo[0]		= { 32, 0x00, },	/* DO 0-31 */
		.sdo[1]		= { 32, 0x04, },	/* DO 32-63 */
		.id_reg		= 0x10,
		.is_16bit	= 1,
	},
	[TYPE_PCI1753] = {
		.name		= "pci1753",
		.nsubdevs	= 4,
		.sdio[0]	= { 4, 0x00, },		/* 8255 DIO */
	},
	[TYPE_PCI1753E] = {
		.name		= "pci1753e",
		.nsubdevs	= 8,
		.sdio[0]	= { 4, 0x00, },		/* 8255 DIO */
		.sdio[1]	= { 4, 0x20, },		/* 8255 DIO */
	},
	[TYPE_PCI1754] = {
		.name		= "pci1754",
		.nsubdevs	= 3,
		.sdi[0]		= { 32, 0x00, },	/* DI 0-31 */
		.sdi[1]		= { 32, 0x04, },	/* DI 32-63 */
		.id_reg		= 0x10,
		.is_16bit	= 1,
	},
	[TYPE_PCI1756] = {
		.name		= "pci1756",
		.nsubdevs	= 3,
		.sdi[1]		= { 32, 0x00, },	/* DI 0-31 */
		.sdo[1]		= { 32, 0x04, },	/* DO 0-31 */
		.id_reg		= 0x10,
		.is_16bit	= 1,
	},
	[TYPE_PCI1761] = {
		.name		= "pci1761",
		.nsubdevs	= 3,
		.sdi[1]		= { 8, 0x01 },		/* ISO DI 0-7 */
		.sdo[1]		= { 8, 0x00 },		/* RELAY DO 0-7 */
		.id_reg		= 0x02,
	},
	[TYPE_PCI1762] = {
		.name		= "pci1762",
		.nsubdevs	= 3,
		.sdi[1]		= { 16, 0x02, },	/* ISO DI 0-15 */
		.sdo[1]		= { 16, 0x00, },	/* ISO DO 0-15 */
		.id_reg		= 0x04,
		.is_16bit	= 1,
	},
};

struct pci_dio_dev_private_data {
	int boardtype;
	int irq_subd;
	unsigned short int_ctrl;
	unsigned short int_rf;
};

struct pci_dio_sd_private_data {
	spinlock_t subd_slock;		/* spin-lock for cmd_running */
	unsigned long port_offset;
	short int cmd_running;
};

static void process_irq(struct comedi_device *dev, unsigned int subdev,
			unsigned char irqflags)
{
	struct comedi_subdevice *s = &dev->subdevices[subdev];
	struct pci_dio_sd_private_data *sd_priv = s->private;
	unsigned long reg = sd_priv->port_offset;
	struct comedi_async *async_p = s->async;

	if (async_p) {
		unsigned short val = inw(dev->iobase + reg);

		spin_lock(&sd_priv->subd_slock);
		if (sd_priv->cmd_running)
			comedi_buf_write_samples(s, &val, 1);
		spin_unlock(&sd_priv->subd_slock);
		comedi_handle_events(dev, s);
	}
}

static irqreturn_t pci_dio_interrupt(int irq, void *p_device)
{
	struct comedi_device *dev = p_device;
	struct pci_dio_dev_private_data *dev_private = dev->private;
	const struct dio_boardtype *board = dev->board_ptr;
	unsigned long cpu_flags;
	unsigned char irqflags;
	int i;

	if (!dev->attached) {
		/* Ignore interrupt before device fully attached. */
		/* Might not even have allocated subdevices yet! */
		return IRQ_NONE;
	}

	/* Check if we are source of interrupt */
	spin_lock_irqsave(&dev->spinlock, cpu_flags);
	irqflags = inb(dev->iobase + PCI173X_INT_FLAG_REG);
	if (!(irqflags & 0x0F)) {
		spin_unlock_irqrestore(&dev->spinlock, cpu_flags);
		return IRQ_NONE;
	}

	/* clear all current interrupt flags */
	outb(irqflags, dev->iobase + PCI173X_INT_CLR_REG);
	spin_unlock_irqrestore(&dev->spinlock, cpu_flags);

	/* check irq subdevice triggers */
	for (i = 0; i < PCI_DIO_MAX_IRQ_SUBDEVS; i++) {
		if (irqflags & board->sdirq[i].int_en)
			process_irq(dev, dev_private->irq_subd + i, irqflags);
	}

	return IRQ_HANDLED;
}

static int pci_dio_asy_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	/*
	 * For scan_begin_arg, the trigger number must be 0 and the only
	 * allowed flags are CR_EDGE and CR_INVERT.  CR_EDGE is ignored,
	 * CR_INVERT sets the trigger to falling edge.
	 */
	if (cmd->scan_begin_arg & ~(CR_EDGE | CR_INVERT)) {
		cmd->scan_begin_arg &= (CR_EDGE | CR_INVERT);
		err |= -EINVAL;
	}
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	/* Step 5: check channel list if it exists */

	return 0;
}

static int pci_dio_asy_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct pci_dio_dev_private_data *dev_private = dev->private;
	struct pci_dio_sd_private_data *sd_priv = s->private;
	const struct dio_boardtype *board = dev->board_ptr;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned long cpu_flags;
	unsigned short int_en;

	int_en = board->sdirq[s->index - dev_private->irq_subd].int_en;

	spin_lock_irqsave(&dev->spinlock, cpu_flags);
	if (cmd->scan_begin_arg & CR_INVERT)
		dev_private->int_rf |= int_en;	/* falling edge */
	else
		dev_private->int_rf &= ~int_en;	/* rising edge */
	outb(dev_private->int_rf, dev->iobase + PCI173X_INT_RF_REG);
	dev_private->int_ctrl |= int_en;	/* enable interrupt source */
	outb(dev_private->int_ctrl, dev->iobase + PCI173X_INT_EN_REG);
	spin_unlock_irqrestore(&dev->spinlock, cpu_flags);

	spin_lock_irqsave(&sd_priv->subd_slock, cpu_flags);
	sd_priv->cmd_running = 1;
	spin_unlock_irqrestore(&sd_priv->subd_slock, cpu_flags);

	return 0;
}

static int pci_dio_asy_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct pci_dio_dev_private_data *dev_private = dev->private;
	struct pci_dio_sd_private_data *sd_priv = s->private;
	const struct dio_boardtype *board = dev->board_ptr;
	unsigned long cpu_flags;
	unsigned short int_en;

	spin_lock_irqsave(&sd_priv->subd_slock, cpu_flags);
	sd_priv->cmd_running = 0;
	spin_unlock_irqrestore(&sd_priv->subd_slock, cpu_flags);

	int_en = board->sdirq[s->index - dev_private->irq_subd].int_en;

	spin_lock_irqsave(&dev->spinlock, cpu_flags);
	dev_private->int_ctrl &= ~int_en;
	outb(dev_private->int_ctrl, dev->iobase + PCI173X_INT_EN_REG);
	spin_unlock_irqrestore(&dev->spinlock, cpu_flags);

	return 0;
}

/* same as _insn_bits_di_ because the IRQ-pins are the DI-ports  */
static int pci_dio_insn_bits_dirq_b(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct pci_dio_sd_private_data *sd_priv = s->private;
	unsigned long reg = (unsigned long)sd_priv->port_offset;
	unsigned long iobase = dev->iobase + reg;

	data[1] = inb(iobase);

	return insn->n;
}

static int pci_dio_insn_bits_di_b(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;
	unsigned long iobase = dev->iobase + reg;

	data[1] = inb(iobase);
	if (s->n_chan > 8)
		data[1] |= (inb(iobase + 1) << 8);
	if (s->n_chan > 16)
		data[1] |= (inb(iobase + 2) << 16);
	if (s->n_chan > 24)
		data[1] |= (inb(iobase + 3) << 24);

	return insn->n;
}

static int pci_dio_insn_bits_di_w(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;
	unsigned long iobase = dev->iobase + reg;

	data[1] = inw(iobase);
	if (s->n_chan > 16)
		data[1] |= (inw(iobase + 2) << 16);

	return insn->n;
}

static int pci_dio_insn_bits_do_b(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;
	unsigned long iobase = dev->iobase + reg;

	if (comedi_dio_update_state(s, data)) {
		outb(s->state & 0xff, iobase);
		if (s->n_chan > 8)
			outb((s->state >> 8) & 0xff, iobase + 1);
		if (s->n_chan > 16)
			outb((s->state >> 16) & 0xff, iobase + 2);
		if (s->n_chan > 24)
			outb((s->state >> 24) & 0xff, iobase + 3);
	}

	data[1] = s->state;

	return insn->n;
}

static int pci_dio_insn_bits_do_w(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;
	unsigned long iobase = dev->iobase + reg;

	if (comedi_dio_update_state(s, data)) {
		outw(s->state & 0xffff, iobase);
		if (s->n_chan > 16)
			outw((s->state >> 16) & 0xffff, iobase + 2);
	}

	data[1] = s->state;

	return insn->n;
}

static int pci_dio_reset(struct comedi_device *dev, unsigned long cardtype)
{
	struct pci_dio_dev_private_data *dev_private = dev->private;
	/* disable channel freeze function on the PCI-1752/1756 boards */
	if (cardtype == TYPE_PCI1752 || cardtype == TYPE_PCI1756)
		outw(0, dev->iobase + PCI1752_CFC_REG);

	/* disable and clear interrupts */
	switch (cardtype) {
	case TYPE_PCI1730:
	case TYPE_PCI1733:
	case TYPE_PCI1736:
		dev_private->int_ctrl = 0x00;
		outb(dev_private->int_ctrl, dev->iobase + PCI173X_INT_EN_REG);
		/* Reset all 4 Int Flags */
		outb(0x0f, dev->iobase + PCI173X_INT_CLR_REG);
		/* Rising Edge => IRQ . On all 4 Pins */
		dev_private->int_rf = 0x00;
		outb(dev_private->int_rf, dev->iobase + PCI173X_INT_RF_REG);
		break;
	case TYPE_PCI1739:
	case TYPE_PCI1750:
	case TYPE_PCI1751:
		outb(0x88, dev->iobase + PCI1750_INT_REG);
		break;
	case TYPE_PCI1753:
	case TYPE_PCI1753E:
		outb(0x88, dev->iobase + PCI1753_INT_REG(0));
		outb(0x80, dev->iobase + PCI1753_INT_REG(1));
		outb(0x80, dev->iobase + PCI1753_INT_REG(2));
		outb(0x80, dev->iobase + PCI1753_INT_REG(3));
		if (cardtype == TYPE_PCI1753E) {
			outb(0x88, dev->iobase + PCI1753E_INT_REG(0));
			outb(0x80, dev->iobase + PCI1753E_INT_REG(1));
			outb(0x80, dev->iobase + PCI1753E_INT_REG(2));
			outb(0x80, dev->iobase + PCI1753E_INT_REG(3));
		}
		break;
	case TYPE_PCI1754:
	case TYPE_PCI1756:
		outw(0x08, dev->iobase + PCI1754_INT_REG(0));
		outw(0x08, dev->iobase + PCI1754_INT_REG(1));
		if (cardtype == TYPE_PCI1754) {
			outw(0x08, dev->iobase + PCI1754_INT_REG(2));
			outw(0x08, dev->iobase + PCI1754_INT_REG(3));
		}
		break;
	case TYPE_PCI1761:
		/* disable interrupts */
		outb(0, dev->iobase + PCI1761_INT_EN_REG);
		/* clear interrupts */
		outb(0xff, dev->iobase + PCI1761_INT_CLR_REG);
		/* set rising edge trigger */
		outb(0, dev->iobase + PCI1761_INT_RF_REG);
		break;
	case TYPE_PCI1762:
		outw(0x0101, dev->iobase + PCI1762_INT_REG);
		break;
	default:
		break;
	}

	return 0;
}

static int pci_dio_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct dio_boardtype *board = NULL;
	struct comedi_subdevice *s;
	struct pci_dio_dev_private_data *dev_private;
	int ret, subdev, i, j;

	if (context < ARRAY_SIZE(boardtypes))
		board = &boardtypes[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	dev_private = comedi_alloc_devpriv(dev, sizeof(*dev_private));
	if (!dev_private)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	if (context == TYPE_PCI1736)
		dev->iobase = pci_resource_start(pcidev, 0);
	else
		dev->iobase = pci_resource_start(pcidev, 2);

	dev_private->boardtype = context;
	pci_dio_reset(dev, context);

	/* request IRQ if device has irq subdevices */
	if (board->sdirq[0].int_en && pcidev->irq) {
		ret = request_irq(pcidev->irq, pci_dio_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, board->nsubdevs);
	if (ret)
		return ret;

	subdev = 0;
	for (i = 0; i < PCI_DIO_MAX_DI_SUBDEVS; i++) {
		const struct diosubd_data *d = &board->sdi[i];

		if (d->chans) {
			s = &dev->subdevices[subdev++];
			s->type		= COMEDI_SUBD_DI;
			s->subdev_flags	= SDF_READABLE;
			s->n_chan	= d->chans;
			s->maxdata	= 1;
			s->range_table	= &range_digital;
			s->insn_bits	= board->is_16bit
						? pci_dio_insn_bits_di_w
						: pci_dio_insn_bits_di_b;
			s->private	= (void *)d->addr;

		}
	}

	for (i = 0; i < PCI_DIO_MAX_DO_SUBDEVS; i++) {
		const struct diosubd_data *d = &board->sdo[i];

		if (d->chans) {
			s = &dev->subdevices[subdev++];
			s->type		= COMEDI_SUBD_DO;
			s->subdev_flags	= SDF_WRITABLE;
			s->n_chan	= d->chans;
			s->maxdata	= 1;
			s->range_table	= &range_digital;
			s->insn_bits	= board->is_16bit
						? pci_dio_insn_bits_do_w
						: pci_dio_insn_bits_do_b;
			s->private	= (void *)d->addr;

			/* reset all outputs to 0 */
			if (board->is_16bit) {
				outw(0, dev->iobase + d->addr);
				if (s->n_chan > 16)
					outw(0, dev->iobase + d->addr + 2);
			} else {
				outb(0, dev->iobase + d->addr);
				if (s->n_chan > 8)
					outb(0, dev->iobase + d->addr + 1);
				if (s->n_chan > 16)
					outb(0, dev->iobase + d->addr + 2);
				if (s->n_chan > 24)
					outb(0, dev->iobase + d->addr + 3);
			}
		}
	}

	for (i = 0; i < PCI_DIO_MAX_DIO_SUBDEVG; i++) {
		const struct diosubd_data *d = &board->sdio[i];

		for (j = 0; j < d->chans; j++) {
			s = &dev->subdevices[subdev++];
			ret = subdev_8255_init(dev, s, NULL,
					       d->addr + j * I8255_SIZE);
			if (ret)
				return ret;
		}
	}

	if (board->id_reg) {
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE | SDF_INTERNAL;
		s->n_chan	= 4;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= board->is_16bit ? pci_dio_insn_bits_di_w
						  : pci_dio_insn_bits_di_b;
		s->private	= (void *)board->id_reg;
	}

	if (board->timer_regbase) {
		s = &dev->subdevices[subdev++];

		dev->pacer = comedi_8254_init(dev->iobase +
					      board->timer_regbase,
					      0, I8254_IO8, 0);
		if (!dev->pacer)
			return -ENOMEM;

		comedi_8254_subdevice_init(s, dev->pacer);
	}

	dev_private->irq_subd = subdev; /* first interrupt subdevice index */
	for (i = 0; i < PCI_DIO_MAX_IRQ_SUBDEVS; ++i) {
		struct pci_dio_sd_private_data *sd_priv = NULL;
		const struct dio_irq_subd_data *d = &board->sdirq[i];

		if (d->int_en) {
			s = &dev->subdevices[subdev++];
			s->type		= COMEDI_SUBD_DI;
			s->subdev_flags	= SDF_READABLE;
			s->n_chan	= 1;
			s->maxdata	= 1;
			s->range_table	= &range_digital;
			s->insn_bits	= pci_dio_insn_bits_dirq_b;
			sd_priv = comedi_alloc_spriv(s, sizeof(*sd_priv));
			if (!sd_priv)
				return -ENOMEM;

			spin_lock_init(&sd_priv->subd_slock);
			sd_priv->port_offset = d->addr;
			sd_priv->cmd_running = 0;

			if (dev->irq) {
				dev->read_subdev = s;
				s->type		= COMEDI_SUBD_DI;
				s->subdev_flags	= SDF_READABLE | SDF_CMD_READ;
				s->len_chanlist	= 1;
				s->do_cmdtest	= pci_dio_asy_cmdtest;
				s->do_cmd	= pci_dio_asy_cmd;
				s->cancel	= pci_dio_asy_cancel;
			}
		}
	}

	return 0;
}

static void pci_dio_detach(struct comedi_device *dev)
{
	struct pci_dio_dev_private_data *dev_private = dev->private;
	int boardtype = dev_private->boardtype;

	if (dev->iobase)
		pci_dio_reset(dev, boardtype);
	comedi_pci_detach(dev);
}

static struct comedi_driver adv_pci_dio_driver = {
	.driver_name	= "adv_pci_dio",
	.module		= THIS_MODULE,
	.auto_attach	= pci_dio_auto_attach,
	.detach		= pci_dio_detach,
};

static unsigned long pci_dio_override_cardtype(struct pci_dev *pcidev,
					       unsigned long cardtype)
{
	/*
	 * Change cardtype from TYPE_PCI1753 to TYPE_PCI1753E if expansion
	 * board available.  Need to enable PCI device and request the main
	 * registers PCI BAR temporarily to perform the test.
	 */
	if (cardtype != TYPE_PCI1753)
		return cardtype;
	if (pci_enable_device(pcidev) < 0)
		return cardtype;
	if (pci_request_region(pcidev, 2, "adv_pci_dio") == 0) {
		/*
		 * This test is based on Advantech's "advdaq" driver source
		 * (which declares its module licence as "GPL" although the
		 * driver source does not include a "COPYING" file).
		 */
		unsigned long reg = pci_resource_start(pcidev, 2) + 53;

		outb(0x05, reg);
		if ((inb(reg) & 0x07) == 0x02) {
			outb(0x02, reg);
			if ((inb(reg) & 0x07) == 0x05)
				cardtype = TYPE_PCI1753E;
		}
		pci_release_region(pcidev, 2);
	}
	pci_disable_device(pcidev);
	return cardtype;
}

static int adv_pci_dio_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	unsigned long cardtype;

	cardtype = pci_dio_override_cardtype(dev, id->driver_data);
	return comedi_pci_auto_config(dev, &adv_pci_dio_driver, cardtype);
}

static const struct pci_device_id adv_pci_dio_pci_table[] = {
	{ PCI_VDEVICE(ADVANTECH, 0x1730), TYPE_PCI1730 },
	{ PCI_VDEVICE(ADVANTECH, 0x1733), TYPE_PCI1733 },
	{ PCI_VDEVICE(ADVANTECH, 0x1734), TYPE_PCI1734 },
	{ PCI_VDEVICE(ADVANTECH, 0x1735), TYPE_PCI1735 },
	{ PCI_VDEVICE(ADVANTECH, 0x1736), TYPE_PCI1736 },
	{ PCI_VDEVICE(ADVANTECH, 0x1739), TYPE_PCI1739 },
	{ PCI_VDEVICE(ADVANTECH, 0x1750), TYPE_PCI1750 },
	{ PCI_VDEVICE(ADVANTECH, 0x1751), TYPE_PCI1751 },
	{ PCI_VDEVICE(ADVANTECH, 0x1752), TYPE_PCI1752 },
	{ PCI_VDEVICE(ADVANTECH, 0x1753), TYPE_PCI1753 },
	{ PCI_VDEVICE(ADVANTECH, 0x1754), TYPE_PCI1754 },
	{ PCI_VDEVICE(ADVANTECH, 0x1756), TYPE_PCI1756 },
	{ PCI_VDEVICE(ADVANTECH, 0x1761), TYPE_PCI1761 },
	{ PCI_VDEVICE(ADVANTECH, 0x1762), TYPE_PCI1762 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adv_pci_dio_pci_table);

static struct pci_driver adv_pci_dio_pci_driver = {
	.name		= "adv_pci_dio",
	.id_table	= adv_pci_dio_pci_table,
	.probe		= adv_pci_dio_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adv_pci_dio_driver, adv_pci_dio_pci_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Advantech Digital I/O Cards");
MODULE_LICENSE("GPL");
