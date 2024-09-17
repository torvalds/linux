// SPDX-License-Identifier: GPL-2.0+
/*
 * COMEDI driver for the ADLINK PCI-723x/743x series boards.
 * Copyright (C) 2012 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on the adl_pci7230 driver written by:
 *	David Fernandez <dfcastelao@gmail.com>
 * and the adl_pci7432 driver written by:
 *	Michel Lachaine <mike@mikelachaine.ca>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: adl_pci7x3x
 * Description: 32/64-Channel Isolated Digital I/O Boards
 * Devices: [ADLink] PCI-7230 (adl_pci7230), PCI-7233 (adl_pci7233),
 *   PCI-7234 (adl_pci7234), PCI-7432 (adl_pci7432), PCI-7433 (adl_pci7433),
 *   PCI-7434 (adl_pci7434)
 * Author: H Hartley Sweeten <hsweeten@visionengravers.com>
 * Updated: Fri, 20 Nov 2020 14:49:36 +0000
 * Status: works (tested on PCI-7230)
 *
 * One or two subdevices are setup by this driver depending on
 * the number of digital inputs and/or outputs provided by the
 * board. Each subdevice has a maximum of 32 channels.
 *
 *	PCI-7230 - 4 subdevices: 0 - 16 input, 1 - 16 output,
 *	                         2 - IRQ_IDI0, 3 - IRQ_IDI1
 *	PCI-7233 - 1 subdevice: 0 - 32 input
 *	PCI-7234 - 1 subdevice: 0 - 32 output
 *	PCI-7432 - 2 subdevices: 0 - 32 input, 1 - 32 output
 *	PCI-7433 - 2 subdevices: 0 - 32 input, 1 - 32 input
 *	PCI-7434 - 2 subdevices: 0 - 32 output, 1 - 32 output
 *
 * The PCI-7230, PCI-7432 and PCI-7433 boards also support external
 * interrupt signals on digital input channels 0 and 1. The PCI-7233
 * has dual-interrupt sources for change-of-state (COS) on any 16
 * digital input channels of LSB and for COS on any 16 digital input
 * lines of MSB.
 *
 * Currently, this driver only supports interrupts for PCI-7230.
 *
 * Configuration Options: not applicable, uses comedi PCI auto config
 */

#include <linux/module.h>
#include <linux/comedi/comedi_pci.h>

#include "plx9052.h"

/*
 * Register I/O map (32-bit access only)
 */
#define PCI7X3X_DIO_REG		0x0000	/* in the DigIO Port area */
#define PCI743X_DIO_REG		0x0004

#define ADL_PT_CLRIRQ		0x0040	/* in the DigIO Port area */

#define LINTI1_EN_ACT_IDI0 (PLX9052_INTCSR_LI1ENAB | PLX9052_INTCSR_LI1STAT)
#define LINTI2_EN_ACT_IDI1 (PLX9052_INTCSR_LI2ENAB | PLX9052_INTCSR_LI2STAT)
#define EN_PCI_LINT2H_LINT1H	\
	(PLX9052_INTCSR_PCIENAB | PLX9052_INTCSR_LI2POL | PLX9052_INTCSR_LI1POL)

enum adl_pci7x3x_boardid {
	BOARD_PCI7230,
	BOARD_PCI7233,
	BOARD_PCI7234,
	BOARD_PCI7432,
	BOARD_PCI7433,
	BOARD_PCI7434,
};

struct adl_pci7x3x_boardinfo {
	const char *name;
	int nsubdevs;
	int di_nchan;
	int do_nchan;
	int irq_nchan;
};

static const struct adl_pci7x3x_boardinfo adl_pci7x3x_boards[] = {
	[BOARD_PCI7230] = {
		.name		= "adl_pci7230",
		.nsubdevs	= 4,  /* IDI, IDO, IRQ_IDI0, IRQ_IDI1 */
		.di_nchan	= 16,
		.do_nchan	= 16,
		.irq_nchan	= 2,
	},
	[BOARD_PCI7233] = {
		.name		= "adl_pci7233",
		.nsubdevs	= 1,
		.di_nchan	= 32,
	},
	[BOARD_PCI7234] = {
		.name		= "adl_pci7234",
		.nsubdevs	= 1,
		.do_nchan	= 32,
	},
	[BOARD_PCI7432] = {
		.name		= "adl_pci7432",
		.nsubdevs	= 2,
		.di_nchan	= 32,
		.do_nchan	= 32,
	},
	[BOARD_PCI7433] = {
		.name		= "adl_pci7433",
		.nsubdevs	= 2,
		.di_nchan	= 64,
	},
	[BOARD_PCI7434] = {
		.name		= "adl_pci7434",
		.nsubdevs	= 2,
		.do_nchan	= 64,
	}
};

struct adl_pci7x3x_dev_private_data {
	unsigned long lcr_io_base;
	unsigned int int_ctrl;
};

struct adl_pci7x3x_sd_private_data {
	spinlock_t subd_slock;		/* spin-lock for cmd_running */
	unsigned long port_offset;
	short int cmd_running;
};

static void process_irq(struct comedi_device *dev, unsigned int subdev,
			unsigned short intcsr)
{
	struct comedi_subdevice *s = &dev->subdevices[subdev];
	struct adl_pci7x3x_sd_private_data *sd_priv = s->private;
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

static irqreturn_t adl_pci7x3x_interrupt(int irq, void *p_device)
{
	struct comedi_device *dev = p_device;
	struct adl_pci7x3x_dev_private_data *dev_private = dev->private;
	unsigned long cpu_flags;
	unsigned int intcsr;
	bool li1stat, li2stat;

	if (!dev->attached) {
		/* Ignore interrupt before device fully attached. */
		/* Might not even have allocated subdevices yet! */
		return IRQ_NONE;
	}

	/* Check if we are source of interrupt */
	spin_lock_irqsave(&dev->spinlock, cpu_flags);
	intcsr = inl(dev_private->lcr_io_base + PLX9052_INTCSR);
	li1stat = (intcsr & LINTI1_EN_ACT_IDI0) == LINTI1_EN_ACT_IDI0;
	li2stat = (intcsr & LINTI2_EN_ACT_IDI1) == LINTI2_EN_ACT_IDI1;
	if (li1stat || li2stat) {
		/* clear all current interrupt flags */
		/* Fixme: Reset all 2 Int Flags */
		outb(0x00, dev->iobase + ADL_PT_CLRIRQ);
	}
	spin_unlock_irqrestore(&dev->spinlock, cpu_flags);

	/* SubDev 2, 3 = Isolated DigIn , on "SCSI2" jack!*/

	if (li1stat)	/* 0x0005 LINTi1 is Enabled && IDI0 is 1 */
		process_irq(dev, 2, intcsr);

	if (li2stat)	/* 0x0028 LINTi2 is Enabled && IDI1 is 1 */
		process_irq(dev, 3, intcsr);

	return IRQ_RETVAL(li1stat || li2stat);
}

static int adl_pci7x3x_asy_cmdtest(struct comedi_device *dev,
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
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
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

static int adl_pci7x3x_asy_cmd(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct adl_pci7x3x_dev_private_data *dev_private = dev->private;
	struct adl_pci7x3x_sd_private_data *sd_priv = s->private;
	unsigned long cpu_flags;
	unsigned int int_enab;

	if (s->index == 2) {
		/* enable LINTi1 == IDI sdi[0] Ch 0 IRQ ActHigh */
		int_enab = PLX9052_INTCSR_LI1ENAB;
	} else {
		/* enable LINTi2 == IDI sdi[0] Ch 1 IRQ ActHigh */
		int_enab = PLX9052_INTCSR_LI2ENAB;
	}

	spin_lock_irqsave(&dev->spinlock, cpu_flags);
	dev_private->int_ctrl |= int_enab;
	outl(dev_private->int_ctrl, dev_private->lcr_io_base + PLX9052_INTCSR);
	spin_unlock_irqrestore(&dev->spinlock, cpu_flags);

	spin_lock_irqsave(&sd_priv->subd_slock, cpu_flags);
	sd_priv->cmd_running = 1;
	spin_unlock_irqrestore(&sd_priv->subd_slock, cpu_flags);

	return 0;
}

static int adl_pci7x3x_asy_cancel(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct adl_pci7x3x_dev_private_data *dev_private = dev->private;
	struct adl_pci7x3x_sd_private_data *sd_priv = s->private;
	unsigned long cpu_flags;
	unsigned int int_enab;

	spin_lock_irqsave(&sd_priv->subd_slock, cpu_flags);
	sd_priv->cmd_running = 0;
	spin_unlock_irqrestore(&sd_priv->subd_slock, cpu_flags);
	/* disable Interrupts */
	if (s->index == 2)
		int_enab = PLX9052_INTCSR_LI1ENAB;
	else
		int_enab = PLX9052_INTCSR_LI2ENAB;
	spin_lock_irqsave(&dev->spinlock, cpu_flags);
	dev_private->int_ctrl &= ~int_enab;
	outl(dev_private->int_ctrl, dev_private->lcr_io_base + PLX9052_INTCSR);
	spin_unlock_irqrestore(&dev->spinlock, cpu_flags);

	return 0;
}

/* same as _di_insn_bits because the IRQ-pins are the DI-ports  */
static int adl_pci7x3x_dirq_insn_bits(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	struct adl_pci7x3x_sd_private_data *sd_priv = s->private;
	unsigned long reg = (unsigned long)sd_priv->port_offset;

	data[1] = inl(dev->iobase + reg);

	return insn->n;
}

static int adl_pci7x3x_do_insn_bits(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;

	if (comedi_dio_update_state(s, data)) {
		unsigned int val = s->state;

		if (s->n_chan == 16) {
			/*
			 * It seems the PCI-7230 needs the 16-bit DO state
			 * to be shifted left by 16 bits before being written
			 * to the 32-bit register.  Set the value in both
			 * halves of the register to be sure.
			 */
			val |= val << 16;
		}
		outl(val, dev->iobase + reg);
	}

	data[1] = s->state;

	return insn->n;
}

static int adl_pci7x3x_di_insn_bits(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;

	data[1] = inl(dev->iobase + reg);

	return insn->n;
}

static int adl_pci7x3x_reset(struct comedi_device *dev)
{
	struct adl_pci7x3x_dev_private_data *dev_private = dev->private;

	/* disable Interrupts */
	dev_private->int_ctrl = 0x00;  /* Disable PCI + LINTi2 + LINTi1 */
	outl(dev_private->int_ctrl, dev_private->lcr_io_base + PLX9052_INTCSR);

	return 0;
}

static int adl_pci7x3x_auto_attach(struct comedi_device *dev,
				   unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct adl_pci7x3x_boardinfo *board = NULL;
	struct comedi_subdevice *s;
	struct adl_pci7x3x_dev_private_data *dev_private;
	int subdev;
	int nchan;
	int ret;
	int ic;

	if (context < ARRAY_SIZE(adl_pci7x3x_boards))
		board = &adl_pci7x3x_boards[context];
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
	dev->iobase = pci_resource_start(pcidev, 2);
	dev_private->lcr_io_base = pci_resource_start(pcidev, 1);

	adl_pci7x3x_reset(dev);

	if (board->irq_nchan) {
		/* discard all evtl. old IRQs */
		outb(0x00, dev->iobase + ADL_PT_CLRIRQ);

		if (pcidev->irq) {
			ret = request_irq(pcidev->irq, adl_pci7x3x_interrupt,
					  IRQF_SHARED, dev->board_name, dev);
			if (ret == 0) {
				dev->irq = pcidev->irq;
				/* 0x52 PCI + IDI Ch 1 Ch 0 IRQ Off ActHigh */
				dev_private->int_ctrl = EN_PCI_LINT2H_LINT1H;
				outl(dev_private->int_ctrl,
				     dev_private->lcr_io_base + PLX9052_INTCSR);
			}
		}
	}

	ret = comedi_alloc_subdevices(dev, board->nsubdevs);
	if (ret)
		return ret;

	subdev = 0;

	if (board->di_nchan) {
		nchan = min(board->di_nchan, 32);

		s = &dev->subdevices[subdev];
		/* Isolated digital inputs 0 to 15/31 */
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= nchan;
		s->maxdata	= 1;
		s->insn_bits	= adl_pci7x3x_di_insn_bits;
		s->range_table	= &range_digital;

		s->private	= (void *)PCI7X3X_DIO_REG;

		subdev++;

		nchan = board->di_nchan - nchan;
		if (nchan) {
			s = &dev->subdevices[subdev];
			/* Isolated digital inputs 32 to 63 */
			s->type		= COMEDI_SUBD_DI;
			s->subdev_flags	= SDF_READABLE;
			s->n_chan	= nchan;
			s->maxdata	= 1;
			s->insn_bits	= adl_pci7x3x_di_insn_bits;
			s->range_table	= &range_digital;

			s->private	= (void *)PCI743X_DIO_REG;

			subdev++;
		}
	}

	if (board->do_nchan) {
		nchan = min(board->do_nchan, 32);

		s = &dev->subdevices[subdev];
		/* Isolated digital outputs 0 to 15/31 */
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= nchan;
		s->maxdata	= 1;
		s->insn_bits	= adl_pci7x3x_do_insn_bits;
		s->range_table	= &range_digital;

		s->private	= (void *)PCI7X3X_DIO_REG;

		subdev++;

		nchan = board->do_nchan - nchan;
		if (nchan) {
			s = &dev->subdevices[subdev];
			/* Isolated digital outputs 32 to 63 */
			s->type		= COMEDI_SUBD_DO;
			s->subdev_flags	= SDF_WRITABLE;
			s->n_chan	= nchan;
			s->maxdata	= 1;
			s->insn_bits	= adl_pci7x3x_do_insn_bits;
			s->range_table	= &range_digital;

			s->private	= (void *)PCI743X_DIO_REG;

			subdev++;
		}
	}

	for (ic = 0; ic < board->irq_nchan; ++ic) {
		struct adl_pci7x3x_sd_private_data *sd_priv;

		nchan = 1;

		s = &dev->subdevices[subdev];
		/* Isolated digital inputs 0 or 1 */
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= nchan;
		s->maxdata	= 1;
		s->insn_bits	= adl_pci7x3x_dirq_insn_bits;
		s->range_table	= &range_digital;

		sd_priv = comedi_alloc_spriv(s, sizeof(*sd_priv));
		if (!sd_priv)
			return -ENOMEM;

		spin_lock_init(&sd_priv->subd_slock);
		sd_priv->port_offset = PCI7X3X_DIO_REG;
		sd_priv->cmd_running = 0;

		if (dev->irq) {
			dev->read_subdev = s;
			s->type		= COMEDI_SUBD_DI;
			s->subdev_flags	= SDF_READABLE | SDF_CMD_READ;
			s->len_chanlist	= 1;
			s->do_cmdtest	= adl_pci7x3x_asy_cmdtest;
			s->do_cmd	= adl_pci7x3x_asy_cmd;
			s->cancel	= adl_pci7x3x_asy_cancel;
		}

		subdev++;
	}

	return 0;
}

static void adl_pci7x3x_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		adl_pci7x3x_reset(dev);
	comedi_pci_detach(dev);
}

static struct comedi_driver adl_pci7x3x_driver = {
	.driver_name	= "adl_pci7x3x",
	.module		= THIS_MODULE,
	.auto_attach	= adl_pci7x3x_auto_attach,
	.detach		= adl_pci7x3x_detach,
};

static int adl_pci7x3x_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adl_pci7x3x_driver,
				      id->driver_data);
}

static const struct pci_device_id adl_pci7x3x_pci_table[] = {
	{ PCI_VDEVICE(ADLINK, 0x7230), BOARD_PCI7230 },
	{ PCI_VDEVICE(ADLINK, 0x7233), BOARD_PCI7233 },
	{ PCI_VDEVICE(ADLINK, 0x7234), BOARD_PCI7234 },
	{ PCI_VDEVICE(ADLINK, 0x7432), BOARD_PCI7432 },
	{ PCI_VDEVICE(ADLINK, 0x7433), BOARD_PCI7433 },
	{ PCI_VDEVICE(ADLINK, 0x7434), BOARD_PCI7434 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adl_pci7x3x_pci_table);

static struct pci_driver adl_pci7x3x_pci_driver = {
	.name		= "adl_pci7x3x",
	.id_table	= adl_pci7x3x_pci_table,
	.probe		= adl_pci7x3x_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adl_pci7x3x_driver, adl_pci7x3x_pci_driver);

MODULE_DESCRIPTION("ADLINK PCI-723x/743x Isolated Digital I/O boards");
MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_LICENSE("GPL");
