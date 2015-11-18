/*
 * comedi/drivers/adv_pci_dio.c
 *
 * Author: Michal Dobes <dobes@tesnet.cz>
 *
 *  Hardware driver for Advantech PCI DIO cards.
 */

/*
 * Driver: adv_pci_dio
 * Description: Advantech PCI-1730, PCI-1733, PCI-1734, PCI-1735U,
 *   PCI-1736UP, PCI-1739U, PCI-1750, PCI-1751, PCI-1752,
 *   PCI-1753/E, PCI-1754, PCI-1756, PCI-1762
 * Devices: [Advantech] PCI-1730 (adv_pci_dio), PCI-1733,
 *   PCI-1734, PCI-1735U, PCI-1736UP, PCI-1739U, PCI-1750,
 *   PCI-1751, PCI-1752, PCI-1753,
 *   PCI-1753+PCI-1753E, PCI-1754, PCI-1756,
 *   PCI-1762
 * Author: Michal Dobes <dobes@tesnet.cz>
 * Updated: Mon, 09 Jan 2012 12:40:46 +0000
 * Status: untested
 *
 * Configuration Options: not applicable, uses PCI auto config
 */

#include <linux/module.h>
#include <linux/delay.h>

#include "../comedi_pci.h"

#include "8255.h"
#include "comedi_8254.h"

/* hardware types of the cards */
enum hw_cards_id {
	TYPE_PCI1730, TYPE_PCI1733, TYPE_PCI1734, TYPE_PCI1735, TYPE_PCI1736,
	TYPE_PCI1739,
	TYPE_PCI1750,
	TYPE_PCI1751,
	TYPE_PCI1752,
	TYPE_PCI1753, TYPE_PCI1753E,
	TYPE_PCI1754, TYPE_PCI1756,
	TYPE_PCI1762
};

#define MAX_DI_SUBDEVS	2	/* max number of DI subdevices per card */
#define MAX_DO_SUBDEVS	2	/* max number of DO subdevices per card */
#define MAX_DIO_SUBDEVG	2	/* max number of DIO subdevices group per
				 * card */

/* Register offset definitions */
/*  Advantech PCI-1730/3/4 */
#define PCI1730_IDI	   0	/* R:   Isolated digital input  0-15 */
#define PCI1730_IDO	   0	/* W:   Isolated digital output 0-15 */
#define PCI1730_DI	   2	/* R:   Digital input  0-15 */
#define PCI1730_DO	   2	/* W:   Digital output 0-15 */
#define PCI1733_IDI	   0	/* R:   Isolated digital input  0-31 */
#define	PCI1730_3_INT_EN	0x08	/* R/W: enable/disable interrupts */
#define	PCI1730_3_INT_RF	0x0c	/* R/W: set falling/raising edge for
					 * interrupts */
#define	PCI1730_3_INT_CLR	0x10	/* R/W: clear interrupts */
#define PCI1734_IDO	   0	/* W:   Isolated digital output 0-31 */
#define PCI173x_BOARDID	   4	/* R:   Board I/D switch for 1730/3/4 */

/* Advantech PCI-1735U */
#define PCI1735_DI	   0	/* R:   Digital input  0-31 */
#define PCI1735_DO	   0	/* W:   Digital output 0-31 */
#define PCI1735_C8254	   4	/* R/W: 8254 counter */
#define PCI1735_BOARDID	   8    /* R:   Board I/D switch for 1735U */

/*  Advantech PCI-1736UP */
#define PCI1736_IDI        0	/* R:   Isolated digital input  0-15 */
#define PCI1736_IDO        0	/* W:   Isolated digital output 0-15 */
#define PCI1736_3_INT_EN        0x08	/* R/W: enable/disable interrupts */
#define PCI1736_3_INT_RF        0x0c	/* R/W: set falling/raising edge for
					 * interrupts */
#define PCI1736_3_INT_CLR       0x10	/* R/W: clear interrupts */
#define PCI1736_BOARDID    4	/* R:   Board I/D switch for 1736UP */

/* Advantech PCI-1739U */
#define PCI1739_DIO	   0	/* R/W: begin of 8255 registers block */
#define PCI1739_ICR	  32	/* W:   Interrupt control register */
#define PCI1739_ISR	  32	/* R:   Interrupt status register */
#define PCI1739_BOARDID	   8    /* R:   Board I/D switch for 1739U */

/*  Advantech PCI-1750 */
#define PCI1750_IDI	   0	/* R:   Isolated digital input  0-15 */
#define PCI1750_IDO	   0	/* W:   Isolated digital output 0-15 */
#define PCI1750_ICR	  32	/* W:   Interrupt control register */
#define PCI1750_ISR	  32	/* R:   Interrupt status register */

/*  Advantech PCI-1751/3/3E */
#define PCI1751_DIO	   0	/* R/W: begin of 8255 registers block */
#define PCI1751_CNT	  24	/* R/W: begin of 8254 registers block */
#define PCI1751_ICR	  32	/* W:   Interrupt control register */
#define PCI1751_ISR	  32	/* R:   Interrupt status register */
#define PCI1753_DIO	   0	/* R/W: begin of 8255 registers block */
#define PCI1753_ICR0	  16	/* R/W: Interrupt control register group 0 */
#define PCI1753_ICR1	  17	/* R/W: Interrupt control register group 1 */
#define PCI1753_ICR2	  18	/* R/W: Interrupt control register group 2 */
#define PCI1753_ICR3	  19	/* R/W: Interrupt control register group 3 */
#define PCI1753E_DIO	  32	/* R/W: begin of 8255 registers block */
#define PCI1753E_ICR0	  48	/* R/W: Interrupt control register group 0 */
#define PCI1753E_ICR1	  49	/* R/W: Interrupt control register group 1 */
#define PCI1753E_ICR2	  50	/* R/W: Interrupt control register group 2 */
#define PCI1753E_ICR3	  51	/* R/W: Interrupt control register group 3 */

/*  Advantech PCI-1752/4/6 */
#define PCI1752_IDO	   0	/* R/W: Digital output  0-31 */
#define PCI1752_IDO2	   4	/* R/W: Digital output 32-63 */
#define PCI1754_IDI	   0	/* R:   Digital input   0-31 */
#define PCI1754_IDI2	   4	/* R:   Digital input  32-64 */
#define PCI1756_IDI	   0	/* R:   Digital input   0-31 */
#define PCI1756_IDO	   4	/* R/W: Digital output  0-31 */
#define PCI1754_6_ICR0	0x08	/* R/W: Interrupt control register group 0 */
#define PCI1754_6_ICR1	0x0a	/* R/W: Interrupt control register group 1 */
#define PCI1754_ICR2	0x0c	/* R/W: Interrupt control register group 2 */
#define PCI1754_ICR3	0x0e	/* R/W: Interrupt control register group 3 */
#define PCI1752_6_CFC	0x12	/* R/W: set/read channel freeze function */
#define PCI175x_BOARDID	0x10	/* R:   Board I/D switch for 1752/4/6 */

/*  Advantech PCI-1762 registers */
#define PCI1762_RO	   0	/* R/W: Relays status/output */
#define PCI1762_IDI	   2	/* R:   Isolated input status */
#define PCI1762_BOARDID	   4	/* R:   Board I/D switch */
#define PCI1762_ICR	   6	/* W:   Interrupt control register */
#define PCI1762_ISR	   6	/* R:   Interrupt status register */

struct diosubd_data {
	int chans;		/*  num of chans or 8255 devices */
	unsigned long addr;	/*  PCI address ofset */
};

struct dio_boardtype {
	const char *name;	/*  board name */
	enum hw_cards_id cardtype;
	int nsubdevs;
	struct diosubd_data sdi[MAX_DI_SUBDEVS];	/*  DI chans */
	struct diosubd_data sdo[MAX_DO_SUBDEVS];	/*  DO chans */
	struct diosubd_data sdio[MAX_DIO_SUBDEVG];	/*  DIO 8255 chans */
	struct diosubd_data boardid;	/*  card supports board ID switch */
	unsigned long timer_regbase;
	unsigned int is_16bit:1;
};

static const struct dio_boardtype boardtypes[] = {
	[TYPE_PCI1730] = {
		.name		= "pci1730",
		.cardtype	= TYPE_PCI1730,
		.nsubdevs	= 5,
		.sdi[0]		= { 16, PCI1730_DI, },
		.sdi[1]		= { 16, PCI1730_IDI, },
		.sdo[0]		= { 16, PCI1730_DO, },
		.sdo[1]		= { 16, PCI1730_IDO, },
		.boardid	= { 4, PCI173x_BOARDID, },
	},
	[TYPE_PCI1733] = {
		.name		= "pci1733",
		.cardtype	= TYPE_PCI1733,
		.nsubdevs	= 2,
		.sdi[1]		= { 32, PCI1733_IDI, },
		.boardid	= { 4, PCI173x_BOARDID, },
	},
	[TYPE_PCI1734] = {
		.name		= "pci1734",
		.cardtype	= TYPE_PCI1734,
		.nsubdevs	= 2,
		.sdo[1]		= { 32, PCI1734_IDO, },
		.boardid	= { 4, PCI173x_BOARDID, },
	},
	[TYPE_PCI1735] = {
		.name		= "pci1735",
		.cardtype	= TYPE_PCI1735,
		.nsubdevs	= 4,
		.sdi[0]		= { 32, PCI1735_DI, },
		.sdo[0]		= { 32, PCI1735_DO, },
		.boardid	= { 4, PCI1735_BOARDID, },
		.timer_regbase	= PCI1735_C8254,
	},
	[TYPE_PCI1736] = {
		.name		= "pci1736",
		.cardtype	= TYPE_PCI1736,
		.nsubdevs	= 3,
		.sdi[1]		= { 16, PCI1736_IDI, },
		.sdo[1]		= { 16, PCI1736_IDO, },
		.boardid	= { 4, PCI1736_BOARDID, },
	},
	[TYPE_PCI1739] = {
		.name		= "pci1739",
		.cardtype	= TYPE_PCI1739,
		.nsubdevs	= 2,
		.sdio[0]	= { 2, PCI1739_DIO, },
	},
	[TYPE_PCI1750] = {
		.name		= "pci1750",
		.cardtype	= TYPE_PCI1750,
		.nsubdevs	= 2,
		.sdi[1]		= { 16, PCI1750_IDI, },
		.sdo[1]		= { 16, PCI1750_IDO, },
	},
	[TYPE_PCI1751] = {
		.name		= "pci1751",
		.cardtype	= TYPE_PCI1751,
		.nsubdevs	= 3,
		.sdio[0]	= { 2, PCI1751_DIO, },
		.timer_regbase	= PCI1751_CNT,
	},
	[TYPE_PCI1752] = {
		.name		= "pci1752",
		.cardtype	= TYPE_PCI1752,
		.nsubdevs	= 3,
		.sdo[0]		= { 32, PCI1752_IDO, },
		.sdo[1]		= { 32, PCI1752_IDO2, },
		.boardid	= { 4, PCI175x_BOARDID, },
		.is_16bit	= 1,
	},
	[TYPE_PCI1753] = {
		.name		= "pci1753",
		.cardtype	= TYPE_PCI1753,
		.nsubdevs	= 4,
		.sdio[0]	= { 4, PCI1753_DIO, },
	},
	[TYPE_PCI1753E] = {
		.name		= "pci1753e",
		.cardtype	= TYPE_PCI1753E,
		.nsubdevs	= 8,
		.sdio[0]	= { 4, PCI1753_DIO, },
		.sdio[1]	= { 4, PCI1753E_DIO, },
	},
	[TYPE_PCI1754] = {
		.name		= "pci1754",
		.cardtype	= TYPE_PCI1754,
		.nsubdevs	= 3,
		.sdi[0]		= { 32, PCI1754_IDI, },
		.sdi[1]		= { 32, PCI1754_IDI2, },
		.boardid	= { 4, PCI175x_BOARDID, },
		.is_16bit	= 1,
	},
	[TYPE_PCI1756] = {
		.name		= "pci1756",
		.cardtype	= TYPE_PCI1756,
		.nsubdevs	= 3,
		.sdi[1]		= { 32, PCI1756_IDI, },
		.sdo[1]		= { 32, PCI1756_IDO, },
		.boardid	= { 4, PCI175x_BOARDID, },
		.is_16bit	= 1,
	},
	[TYPE_PCI1762] = {
		.name		= "pci1762",
		.cardtype	= TYPE_PCI1762,
		.nsubdevs	= 3,
		.sdi[1]		= { 16, PCI1762_IDI, },
		.sdo[1]		= { 16, PCI1762_RO, },
		.boardid	= { 4, PCI1762_BOARDID, },
		.is_16bit	= 1,
	},
};

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

static int pci_dio_reset(struct comedi_device *dev)
{
	const struct dio_boardtype *board = dev->board_ptr;

	switch (board->cardtype) {
	case TYPE_PCI1730:
		outb(0, dev->iobase + PCI1730_DO);	/*  clear outputs */
		outb(0, dev->iobase + PCI1730_DO + 1);
		outb(0, dev->iobase + PCI1730_IDO);
		outb(0, dev->iobase + PCI1730_IDO + 1);
		/* fallthrough */
	case TYPE_PCI1733:
		/* disable interrupts */
		outb(0, dev->iobase + PCI1730_3_INT_EN);
		/* clear interrupts */
		outb(0x0f, dev->iobase + PCI1730_3_INT_CLR);
		/* set rising edge trigger */
		outb(0, dev->iobase + PCI1730_3_INT_RF);
		break;
	case TYPE_PCI1734:
		outb(0, dev->iobase + PCI1734_IDO);	/*  clear outputs */
		outb(0, dev->iobase + PCI1734_IDO + 1);
		outb(0, dev->iobase + PCI1734_IDO + 2);
		outb(0, dev->iobase + PCI1734_IDO + 3);
		break;
	case TYPE_PCI1735:
		outb(0, dev->iobase + PCI1735_DO);	/*  clear outputs */
		outb(0, dev->iobase + PCI1735_DO + 1);
		outb(0, dev->iobase + PCI1735_DO + 2);
		outb(0, dev->iobase + PCI1735_DO + 3);
		break;

	case TYPE_PCI1736:
		outb(0, dev->iobase + PCI1736_IDO);
		outb(0, dev->iobase + PCI1736_IDO + 1);
		/* disable interrupts */
		outb(0, dev->iobase + PCI1736_3_INT_EN);
		/* clear interrupts */
		outb(0x0f, dev->iobase + PCI1736_3_INT_CLR);
		/* set rising edge trigger */
		outb(0, dev->iobase + PCI1736_3_INT_RF);
		break;

	case TYPE_PCI1739:
		/* disable & clear interrupts */
		outb(0x88, dev->iobase + PCI1739_ICR);
		break;

	case TYPE_PCI1750:
	case TYPE_PCI1751:
		/* disable & clear interrupts */
		outb(0x88, dev->iobase + PCI1750_ICR);
		break;
	case TYPE_PCI1752:
		outw(0, dev->iobase + PCI1752_6_CFC); /* disable channel freeze
						       * function */
		outw(0, dev->iobase + PCI1752_IDO);	/*  clear outputs */
		outw(0, dev->iobase + PCI1752_IDO + 2);
		outw(0, dev->iobase + PCI1752_IDO2);
		outw(0, dev->iobase + PCI1752_IDO2 + 2);
		break;
	case TYPE_PCI1753E:
		outb(0x88, dev->iobase + PCI1753E_ICR0); /* disable & clear
							  * interrupts */
		outb(0x80, dev->iobase + PCI1753E_ICR1);
		outb(0x80, dev->iobase + PCI1753E_ICR2);
		outb(0x80, dev->iobase + PCI1753E_ICR3);
		/* fallthrough */
	case TYPE_PCI1753:
		outb(0x88, dev->iobase + PCI1753_ICR0); /* disable & clear
							 * interrupts */
		outb(0x80, dev->iobase + PCI1753_ICR1);
		outb(0x80, dev->iobase + PCI1753_ICR2);
		outb(0x80, dev->iobase + PCI1753_ICR3);
		break;
	case TYPE_PCI1754:
		outw(0x08, dev->iobase + PCI1754_6_ICR0); /* disable and clear
							   * interrupts */
		outw(0x08, dev->iobase + PCI1754_6_ICR1);
		outw(0x08, dev->iobase + PCI1754_ICR2);
		outw(0x08, dev->iobase + PCI1754_ICR3);
		break;
	case TYPE_PCI1756:
		outw(0, dev->iobase + PCI1752_6_CFC); /* disable channel freeze
						       * function */
		outw(0x08, dev->iobase + PCI1754_6_ICR0); /* disable and clear
							   * interrupts */
		outw(0x08, dev->iobase + PCI1754_6_ICR1);
		outw(0, dev->iobase + PCI1756_IDO);	/*  clear outputs */
		outw(0, dev->iobase + PCI1756_IDO + 2);
		break;
	case TYPE_PCI1762:
		outw(0x0101, dev->iobase + PCI1762_ICR); /* disable & clear
							  * interrupts */
		break;
	}

	return 0;
}

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

static int pci_dio_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct dio_boardtype *board = NULL;
	const struct diosubd_data *d;
	struct comedi_subdevice *s;
	int ret, subdev, i, j;

	if (context < ARRAY_SIZE(boardtypes))
		board = &boardtypes[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	if (board->cardtype == TYPE_PCI1736)
		dev->iobase = pci_resource_start(pcidev, 0);
	else
		dev->iobase = pci_resource_start(pcidev, 2);

	ret = comedi_alloc_subdevices(dev, board->nsubdevs);
	if (ret)
		return ret;

	subdev = 0;
	for (i = 0; i < MAX_DI_SUBDEVS; i++) {
		d = &board->sdi[i];
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

	for (i = 0; i < MAX_DO_SUBDEVS; i++) {
		d = &board->sdo[i];
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
		}
	}

	for (i = 0; i < MAX_DIO_SUBDEVG; i++) {
		d = &board->sdio[i];
		for (j = 0; j < d->chans; j++) {
			s = &dev->subdevices[subdev++];
			ret = subdev_8255_init(dev, s, NULL,
					       d->addr + j * I8255_SIZE);
			if (ret)
				return ret;
		}
	}

	d = &board->boardid;
	if (d->chans) {
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE | SDF_INTERNAL;
		s->n_chan	= d->chans;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= board->is_16bit ? pci_dio_insn_bits_di_w
						  : pci_dio_insn_bits_di_b;
		s->private	= (void *)d->addr;
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

	pci_dio_reset(dev);

	return 0;
}

static void pci_dio_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		pci_dio_reset(dev);
	comedi_pci_detach(dev);
}

static struct comedi_driver adv_pci_dio_driver = {
	.driver_name	= "adv_pci_dio",
	.module		= THIS_MODULE,
	.auto_attach	= pci_dio_auto_attach,
	.detach		= pci_dio_detach,
};

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

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
