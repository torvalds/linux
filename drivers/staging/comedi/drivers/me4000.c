/*
   comedi/drivers/me4000.c
   Source code for the Meilhaus ME-4000 board family.

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 2000 David A. Schleef <ds@schleef.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 */
/*
Driver: me4000
Description: Meilhaus ME-4000 series boards
Devices: [Meilhaus] ME-4650 (me4000), ME-4670i, ME-4680, ME-4680i, ME-4680is
Author: gg (Guenter Gebhardt <g.gebhardt@meilhaus.com>)
Updated: Mon, 18 Mar 2002 15:34:01 -0800
Status: broken (no support for loading firmware)

Supports:

    - Analog Input
    - Analog Output
    - Digital I/O
    - Counter

Configuration Options:

    [0] - PCI bus number (optional)
    [1] - PCI slot number (optional)

    If bus/slot is not specified, the first available PCI
    device will be used.

The firmware required by these boards is available in the
comedi_nonfree_firmware tarball available from
http://www.comedi.org.  However, the driver's support for
loading the firmware through comedi_config is currently
broken.

 */

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "me4000.h"
#if 0
/* file removed due to GPL incompatibility */
#include "me4000_fw.h"
#endif

#define PCI_VENDOR_ID_MEILHAUS		0x1402

#define PCI_DEVICE_ID_MEILHAUS_ME4650	0x4650
#define PCI_DEVICE_ID_MEILHAUS_ME4660	0x4660
#define PCI_DEVICE_ID_MEILHAUS_ME4660I	0x4661
#define PCI_DEVICE_ID_MEILHAUS_ME4660S	0x4662
#define PCI_DEVICE_ID_MEILHAUS_ME4660IS	0x4663
#define PCI_DEVICE_ID_MEILHAUS_ME4670	0x4670
#define PCI_DEVICE_ID_MEILHAUS_ME4670I	0x4671
#define PCI_DEVICE_ID_MEILHAUS_ME4670S	0x4672
#define PCI_DEVICE_ID_MEILHAUS_ME4670IS	0x4673
#define PCI_DEVICE_ID_MEILHAUS_ME4680	0x4680
#define PCI_DEVICE_ID_MEILHAUS_ME4680I	0x4681
#define PCI_DEVICE_ID_MEILHAUS_ME4680S	0x4682
#define PCI_DEVICE_ID_MEILHAUS_ME4680IS	0x4683

struct me4000_board {
	const char *name;
	unsigned short device_id;
	int ao_nchan;
	int ao_fifo;
	int ai_nchan;
	int ai_diff_nchan;
	int ai_sh_nchan;
	int ex_trig_analog;
	int dio_nchan;
	int has_counter;
};

static const struct me4000_board me4000_boards[] = {
	{
		.name		= "ME-4650",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4650,
		.ai_nchan	= 16,
		.dio_nchan	= 32,
	}, {
		.name		= "ME-4660",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4660,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4660i",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4660I,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4660s",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4660S,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4660is",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4660IS,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4670",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4670,
		.ao_nchan	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4670i",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4670I,
		.ao_nchan	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4670s",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4670S,
		.ao_nchan	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4670is",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4670IS,
		.ao_nchan	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4680",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4680,
		.ao_nchan	= 4,
		.ao_fifo	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4680i",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4680I,
		.ao_nchan	= 4,
		.ao_fifo	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4680s",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4680S,
		.ao_nchan	= 4,
		.ao_fifo	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	}, {
		.name		= "ME-4680is",
		.device_id	= PCI_DEVICE_ID_MEILHAUS_ME4680IS,
		.ao_nchan	= 4,
		.ao_fifo	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
};

/*-----------------------------------------------------------------------------
  Meilhaus function prototypes
  ---------------------------------------------------------------------------*/
static int init_board_info(struct comedi_device *dev,
			   struct pci_dev *pci_dev_p);
static int init_ao_context(struct comedi_device *dev);
static int xilinx_download(struct comedi_device *dev);
static int reset_board(struct comedi_device *dev);

static int ai_write_chanlist(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_cmd *cmd);

static const struct comedi_lrange me4000_ai_range = {
	4,
	{
	 UNI_RANGE(2.5),
	 UNI_RANGE(10),
	 BIP_RANGE(2.5),
	 BIP_RANGE(10),
	 }
};

static const struct comedi_lrange me4000_ao_range = {
	1,
	{
	 BIP_RANGE(10),
	 }
};

static int me4000_probe(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct pci_dev *pci_device = NULL;
	int result, i;
	const struct me4000_board *board;

	/* Allocate private memory */
	if (alloc_private(dev, sizeof(struct me4000_info)) < 0)
		return -ENOMEM;

	/*
	 * Probe the device to determine what device in the series it is.
	 */
	for_each_pci_dev(pci_device) {
		if (pci_device->vendor == PCI_VENDOR_ID_MEILHAUS) {
			for (i = 0; i < ARRAY_SIZE(me4000_boards); i++) {
				if (me4000_boards[i].device_id ==
				    pci_device->device) {
					/*
					 * Was a particular
					 * bus/slot requested?
					 */
					if ((it->options[0] != 0)
					    || (it->options[1] != 0)) {
						/*
						 * Are we on the wrong
						 * bus/slot?
						 */
						if (pci_device->bus->number !=
						    it->options[0]
						    ||
						    PCI_SLOT(pci_device->devfn)
						    != it->options[1]) {
							continue;
						}
					}
					dev->board_ptr = me4000_boards + i;
					board = comedi_board(dev);
					info->pci_dev_p = pci_device;
					goto found;
				}
			}
		}
	}
	return -ENODEV;

found:
	dev->board_name = board->name;

	result = comedi_pci_enable(pci_device, dev->board_name);
	if (result)
		return result;

	info->plx_regbase = pci_resource_start(pci_device, 1);
	if (!info->plx_regbase)
		return -ENODEV;

	dev->iobase = pci_resource_start(pci_device, 2);
	if (!dev->iobase)
		return -ENODEV;

	info->timer_regbase = pci_resource_start(pci_device, 3);
	if (!info->timer_regbase)
		return -ENODEV;

	info->program_regbase = pci_resource_start(pci_device, 5);
	if (!info->program_regbase)
		return -ENODEV;

	result = init_board_info(dev, pci_device);
	if (result)
		return result;

	result = init_ao_context(dev);
	if (result)
		return result;

	result = xilinx_download(dev);
	if (result)
		return result;

	result = reset_board(dev);
	if (result)
		return result;

	return 0;
}

static int init_board_info(struct comedi_device *dev, struct pci_dev *pci_dev_p)
{
	/* Init spin locks */
	/* spin_lock_init(&info->preload_lock); */
	/* spin_lock_init(&info->ai_ctrl_lock); */

	/* Get the vendor id */
	info->vendor_id = pci_dev_p->vendor;

	/* Get the device id */
	info->device_id = pci_dev_p->device;

	/* Get the irq assigned to the board */
	info->irq = pci_dev_p->irq;

	return 0;
}

static int init_ao_context(struct comedi_device *dev)
{
	const struct me4000_board *thisboard = comedi_board(dev);
	int i;

	for (i = 0; i < thisboard->ao_nchan; i++) {
		/* spin_lock_init(&info->ao_context[i].use_lock); */
		info->ao_context[i].irq = info->irq;

		switch (i) {
		case 0:
			info->ao_context[i].ctrl_reg =
			    dev->iobase + ME4000_AO_00_CTRL_REG;
			info->ao_context[i].status_reg =
			    dev->iobase + ME4000_AO_00_STATUS_REG;
			info->ao_context[i].fifo_reg =
			    dev->iobase + ME4000_AO_00_FIFO_REG;
			info->ao_context[i].single_reg =
			    dev->iobase + ME4000_AO_00_SINGLE_REG;
			info->ao_context[i].timer_reg =
			    dev->iobase + ME4000_AO_00_TIMER_REG;
			info->ao_context[i].irq_status_reg =
			    dev->iobase + ME4000_IRQ_STATUS_REG;
			info->ao_context[i].preload_reg =
			    dev->iobase + ME4000_AO_LOADSETREG_XX;
			break;
		case 1:
			info->ao_context[i].ctrl_reg =
			    dev->iobase + ME4000_AO_01_CTRL_REG;
			info->ao_context[i].status_reg =
			    dev->iobase + ME4000_AO_01_STATUS_REG;
			info->ao_context[i].fifo_reg =
			    dev->iobase + ME4000_AO_01_FIFO_REG;
			info->ao_context[i].single_reg =
			    dev->iobase + ME4000_AO_01_SINGLE_REG;
			info->ao_context[i].timer_reg =
			    dev->iobase + ME4000_AO_01_TIMER_REG;
			info->ao_context[i].irq_status_reg =
			    dev->iobase + ME4000_IRQ_STATUS_REG;
			info->ao_context[i].preload_reg =
			    dev->iobase + ME4000_AO_LOADSETREG_XX;
			break;
		case 2:
			info->ao_context[i].ctrl_reg =
			    dev->iobase + ME4000_AO_02_CTRL_REG;
			info->ao_context[i].status_reg =
			    dev->iobase + ME4000_AO_02_STATUS_REG;
			info->ao_context[i].fifo_reg =
			    dev->iobase + ME4000_AO_02_FIFO_REG;
			info->ao_context[i].single_reg =
			    dev->iobase + ME4000_AO_02_SINGLE_REG;
			info->ao_context[i].timer_reg =
			    dev->iobase + ME4000_AO_02_TIMER_REG;
			info->ao_context[i].irq_status_reg =
			    dev->iobase + ME4000_IRQ_STATUS_REG;
			info->ao_context[i].preload_reg =
			    dev->iobase + ME4000_AO_LOADSETREG_XX;
			break;
		case 3:
			info->ao_context[i].ctrl_reg =
			    dev->iobase + ME4000_AO_03_CTRL_REG;
			info->ao_context[i].status_reg =
			    dev->iobase + ME4000_AO_03_STATUS_REG;
			info->ao_context[i].fifo_reg =
			    dev->iobase + ME4000_AO_03_FIFO_REG;
			info->ao_context[i].single_reg =
			    dev->iobase + ME4000_AO_03_SINGLE_REG;
			info->ao_context[i].timer_reg =
			    dev->iobase + ME4000_AO_03_TIMER_REG;
			info->ao_context[i].irq_status_reg =
			    dev->iobase + ME4000_IRQ_STATUS_REG;
			info->ao_context[i].preload_reg =
			    dev->iobase + ME4000_AO_LOADSETREG_XX;
			break;
		default:
			break;
		}
	}

	return 0;
}

#define FIRMWARE_NOT_AVAILABLE 1
#if FIRMWARE_NOT_AVAILABLE
extern unsigned char *xilinx_firm;
#endif

static int xilinx_download(struct comedi_device *dev)
{
	u32 value = 0;
	wait_queue_head_t queue;
	int idx = 0;
	int size = 0;

	init_waitqueue_head(&queue);

	/*
	 * Set PLX local interrupt 2 polarity to high.
	 * Interrupt is thrown by init pin of xilinx.
	 */
	outl(0x10, info->plx_regbase + PLX_INTCSR);

	/* Set /CS and /WRITE of the Xilinx */
	value = inl(info->plx_regbase + PLX_ICR);
	value |= 0x100;
	outl(value, info->plx_regbase + PLX_ICR);

	/* Init Xilinx with CS1 */
	inb(info->program_regbase + 0xC8);

	/* Wait until /INIT pin is set */
	udelay(20);
	if (!(inl(info->plx_regbase + PLX_INTCSR) & 0x20)) {
		printk(KERN_ERR
		       "comedi%d: me4000: xilinx_download(): "
		       "Can't init Xilinx\n", dev->minor);
		return -EIO;
	}

	/* Reset /CS and /WRITE of the Xilinx */
	value = inl(info->plx_regbase + PLX_ICR);
	value &= ~0x100;
	outl(value, info->plx_regbase + PLX_ICR);
	if (FIRMWARE_NOT_AVAILABLE) {
		comedi_error(dev, "xilinx firmware unavailable "
			     "due to licensing, aborting");
		return -EIO;
	} else {
		/* Download Xilinx firmware */
		size = (xilinx_firm[0] << 24) + (xilinx_firm[1] << 16) +
		    (xilinx_firm[2] << 8) + xilinx_firm[3];
		udelay(10);

		for (idx = 0; idx < size; idx++) {
			outb(xilinx_firm[16 + idx], info->program_regbase);
			udelay(10);

			/* Check if BUSY flag is low */
			if (inl(info->plx_regbase + PLX_ICR) & 0x20) {
				printk(KERN_ERR
				       "comedi%d: me4000: xilinx_download(): "
				       "Xilinx is still busy (idx = %d)\n",
				       dev->minor, idx);
				return -EIO;
			}
		}
	}

	/* If done flag is high download was successful */
	if (inl(info->plx_regbase + PLX_ICR) & 0x4) {
	} else {
		printk(KERN_ERR
		       "comedi%d: me4000: xilinx_download(): "
		       "DONE flag is not set\n", dev->minor);
		printk(KERN_ERR
		       "comedi%d: me4000: xilinx_download(): "
		       "Download not successful\n", dev->minor);
		return -EIO;
	}

	/* Set /CS and /WRITE */
	value = inl(info->plx_regbase + PLX_ICR);
	value |= 0x100;
	outl(value, info->plx_regbase + PLX_ICR);

	return 0;
}

static int reset_board(struct comedi_device *dev)
{
	unsigned long icr;

	/* Make a hardware reset */
	icr = inl(info->plx_regbase + PLX_ICR);
	icr |= 0x40000000;
	outl(icr, info->plx_regbase + PLX_ICR);
	icr &= ~0x40000000;
	outl(icr, info->plx_regbase + PLX_ICR);

	/* 0x8000 to the DACs means an output voltage of 0V */
	outl(0x8000, dev->iobase + ME4000_AO_00_SINGLE_REG);
	outl(0x8000, dev->iobase + ME4000_AO_01_SINGLE_REG);
	outl(0x8000, dev->iobase + ME4000_AO_02_SINGLE_REG);
	outl(0x8000, dev->iobase + ME4000_AO_03_SINGLE_REG);

	/* Set both stop bits in the analog input control register */
	outl(ME4000_AI_CTRL_BIT_IMMEDIATE_STOP | ME4000_AI_CTRL_BIT_STOP,
		dev->iobase + ME4000_AI_CTRL_REG);

	/* Set both stop bits in the analog output control register */
	outl(ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP,
		dev->iobase + ME4000_AO_00_CTRL_REG);
	outl(ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP,
		dev->iobase + ME4000_AO_01_CTRL_REG);
	outl(ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP,
		dev->iobase + ME4000_AO_02_CTRL_REG);
	outl(ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP,
		dev->iobase + ME4000_AO_03_CTRL_REG);

	/* Enable interrupts on the PLX */
	outl(0x43, info->plx_regbase + PLX_INTCSR);

	/* Set the adustment register for AO demux */
	outl(ME4000_AO_DEMUX_ADJUST_VALUE,
		    dev->iobase + ME4000_AO_DEMUX_ADJUST_REG);

	/*
	 * Set digital I/O direction for port 0
	 * to output on isolated versions
	 */
	if (!(inl(dev->iobase + ME4000_DIO_DIR_REG) & 0x1))
		outl(0x1, dev->iobase + ME4000_DIO_CTRL_REG);

	return 0;
}

/*=============================================================================
  Analog input section
  ===========================================================================*/

static int me4000_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *subdevice,
			       struct comedi_insn *insn, unsigned int *data)
{
	const struct me4000_board *thisboard = comedi_board(dev);
	int chan = CR_CHAN(insn->chanspec);
	int rang = CR_RANGE(insn->chanspec);
	int aref = CR_AREF(insn->chanspec);

	unsigned long entry = 0;
	unsigned long tmp;
	long lval;

	if (insn->n == 0) {
		return 0;
	} else if (insn->n > 1) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_insn_read(): "
		       "Invalid instruction length %d\n", dev->minor, insn->n);
		return -EINVAL;
	}

	switch (rang) {
	case 0:
		entry |= ME4000_AI_LIST_RANGE_UNIPOLAR_2_5;
		break;
	case 1:
		entry |= ME4000_AI_LIST_RANGE_UNIPOLAR_10;
		break;
	case 2:
		entry |= ME4000_AI_LIST_RANGE_BIPOLAR_2_5;
		break;
	case 3:
		entry |= ME4000_AI_LIST_RANGE_BIPOLAR_10;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_insn_read(): "
		       "Invalid range specified\n", dev->minor);
		return -EINVAL;
	}

	switch (aref) {
	case AREF_GROUND:
	case AREF_COMMON:
		if (chan >= thisboard->ai_nchan) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_insn_read(): "
			       "Analog input is not available\n", dev->minor);
			return -EINVAL;
		}
		entry |= ME4000_AI_LIST_INPUT_SINGLE_ENDED | chan;
		break;

	case AREF_DIFF:
		if (rang == 0 || rang == 1) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_insn_read(): "
			       "Range must be bipolar when aref = diff\n",
			       dev->minor);
			return -EINVAL;
		}

		if (chan >= thisboard->ai_diff_nchan) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_insn_read(): "
			       "Analog input is not available\n", dev->minor);
			return -EINVAL;
		}
		entry |= ME4000_AI_LIST_INPUT_DIFFERENTIAL | chan;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_insn_read(): "
		       "Invalid aref specified\n", dev->minor);
		return -EINVAL;
	}

	entry |= ME4000_AI_LIST_LAST_ENTRY;

	/* Clear channel list, data fifo and both stop bits */
	tmp = inl(dev->iobase + ME4000_AI_CTRL_REG);
	tmp &= ~(ME4000_AI_CTRL_BIT_CHANNEL_FIFO |
		 ME4000_AI_CTRL_BIT_DATA_FIFO |
		 ME4000_AI_CTRL_BIT_STOP | ME4000_AI_CTRL_BIT_IMMEDIATE_STOP);
	outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

	/* Set the acquisition mode to single */
	tmp &= ~(ME4000_AI_CTRL_BIT_MODE_0 | ME4000_AI_CTRL_BIT_MODE_1 |
		 ME4000_AI_CTRL_BIT_MODE_2);
	outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

	/* Enable channel list and data fifo */
	tmp |= ME4000_AI_CTRL_BIT_CHANNEL_FIFO | ME4000_AI_CTRL_BIT_DATA_FIFO;
	outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

	/* Generate channel list entry */
	outl(entry, dev->iobase + ME4000_AI_CHANNEL_LIST_REG);

	/* Set the timer to maximum sample rate */
	outl(ME4000_AI_MIN_TICKS, dev->iobase + ME4000_AI_CHAN_TIMER_REG);
	outl(ME4000_AI_MIN_TICKS, dev->iobase + ME4000_AI_CHAN_PRE_TIMER_REG);

	/* Start conversion by dummy read */
	inl(dev->iobase + ME4000_AI_START_REG);

	/* Wait until ready */
	udelay(10);
	if (!(inl(dev->iobase + ME4000_AI_STATUS_REG) &
	     ME4000_AI_STATUS_BIT_EF_DATA)) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_insn_read(): "
		       "Value not available after wait\n", dev->minor);
		return -EIO;
	}

	/* Read value from data fifo */
	lval = inl(dev->iobase + ME4000_AI_DATA_REG) & 0xFFFF;
	data[0] = lval ^ 0x8000;

	return 1;
}

static int me4000_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	unsigned long tmp;

	/* Stop any running conversion */
	tmp = inl(dev->iobase + ME4000_AI_CTRL_REG);
	tmp &= ~(ME4000_AI_CTRL_BIT_STOP | ME4000_AI_CTRL_BIT_IMMEDIATE_STOP);
	outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

	/* Clear the control register */
	outl(0x0, dev->iobase + ME4000_AI_CTRL_REG);

	return 0;
}

static int ai_check_chanlist(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	const struct me4000_board *thisboard = comedi_board(dev);
	int aref;
	int i;

	/* Check whether a channel list is available */
	if (!cmd->chanlist_len) {
		printk(KERN_ERR
		       "comedi%d: me4000: ai_check_chanlist(): "
		       "No channel list available\n", dev->minor);
		return -EINVAL;
	}

	/* Check the channel list size */
	if (cmd->chanlist_len > ME4000_AI_CHANNEL_LIST_COUNT) {
		printk(KERN_ERR
		       "comedi%d: me4000: ai_check_chanlist(): "
		       "Channel list is to large\n", dev->minor);
		return -EINVAL;
	}

	/* Check the pointer */
	if (!cmd->chanlist) {
		printk(KERN_ERR
		       "comedi%d: me4000: ai_check_chanlist(): "
		       "NULL pointer to channel list\n", dev->minor);
		return -EFAULT;
	}

	/* Check whether aref is equal for all entries */
	aref = CR_AREF(cmd->chanlist[0]);
	for (i = 0; i < cmd->chanlist_len; i++) {
		if (CR_AREF(cmd->chanlist[i]) != aref) {
			printk(KERN_ERR
			       "comedi%d: me4000: ai_check_chanlist(): "
			       "Mode is not equal for all entries\n",
			       dev->minor);
			return -EINVAL;
		}
	}

	/* Check whether channels are available for this ending */
	if (aref == SDF_DIFF) {
		for (i = 0; i < cmd->chanlist_len; i++) {
			if (CR_CHAN(cmd->chanlist[i]) >=
			    thisboard->ai_diff_nchan) {
				printk(KERN_ERR
				       "comedi%d: me4000: ai_check_chanlist():"
				       " Channel number to high\n", dev->minor);
				return -EINVAL;
			}
		}
	} else {
		for (i = 0; i < cmd->chanlist_len; i++) {
			if (CR_CHAN(cmd->chanlist[i]) >= thisboard->ai_nchan) {
				printk(KERN_ERR
				       "comedi%d: me4000: ai_check_chanlist(): "
				       "Channel number to high\n", dev->minor);
				return -EINVAL;
			}
		}
	}

	/* Check if bipolar is set for all entries when in differential mode */
	if (aref == SDF_DIFF) {
		for (i = 0; i < cmd->chanlist_len; i++) {
			if (CR_RANGE(cmd->chanlist[i]) != 1 &&
			    CR_RANGE(cmd->chanlist[i]) != 2) {
				printk(KERN_ERR
				       "comedi%d: me4000: ai_check_chanlist(): "
				       "Bipolar is not selected in "
				       "differential mode\n",
				       dev->minor);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int ai_round_cmd_args(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_cmd *cmd,
			     unsigned int *init_ticks,
			     unsigned int *scan_ticks, unsigned int *chan_ticks)
{

	int rest;

	*init_ticks = 0;
	*scan_ticks = 0;
	*chan_ticks = 0;

	if (cmd->start_arg) {
		*init_ticks = (cmd->start_arg * 33) / 1000;
		rest = (cmd->start_arg * 33) % 1000;

		if ((cmd->flags & TRIG_ROUND_MASK) == TRIG_ROUND_NEAREST) {
			if (rest > 33)
				(*init_ticks)++;
		} else if ((cmd->flags & TRIG_ROUND_MASK) == TRIG_ROUND_UP) {
			if (rest)
				(*init_ticks)++;
		}
	}

	if (cmd->scan_begin_arg) {
		*scan_ticks = (cmd->scan_begin_arg * 33) / 1000;
		rest = (cmd->scan_begin_arg * 33) % 1000;

		if ((cmd->flags & TRIG_ROUND_MASK) == TRIG_ROUND_NEAREST) {
			if (rest > 33)
				(*scan_ticks)++;
		} else if ((cmd->flags & TRIG_ROUND_MASK) == TRIG_ROUND_UP) {
			if (rest)
				(*scan_ticks)++;
		}
	}

	if (cmd->convert_arg) {
		*chan_ticks = (cmd->convert_arg * 33) / 1000;
		rest = (cmd->convert_arg * 33) % 1000;

		if ((cmd->flags & TRIG_ROUND_MASK) == TRIG_ROUND_NEAREST) {
			if (rest > 33)
				(*chan_ticks)++;
		} else if ((cmd->flags & TRIG_ROUND_MASK) == TRIG_ROUND_UP) {
			if (rest)
				(*chan_ticks)++;
		}
	}

	return 0;
}

static void ai_write_timer(struct comedi_device *dev,
			   unsigned int init_ticks,
			   unsigned int scan_ticks, unsigned int chan_ticks)
{
	outl(init_ticks - 1, dev->iobase + ME4000_AI_SCAN_PRE_TIMER_LOW_REG);
	outl(0x0, dev->iobase + ME4000_AI_SCAN_PRE_TIMER_HIGH_REG);

	if (scan_ticks) {
		outl(scan_ticks - 1, dev->iobase + ME4000_AI_SCAN_TIMER_LOW_REG);
		outl(0x0, dev->iobase + ME4000_AI_SCAN_TIMER_HIGH_REG);
	}

	outl(chan_ticks - 1, dev->iobase + ME4000_AI_CHAN_PRE_TIMER_REG);
	outl(chan_ticks - 1, dev->iobase + ME4000_AI_CHAN_TIMER_REG);
}

static int ai_prepare(struct comedi_device *dev,
		      struct comedi_subdevice *s,
		      struct comedi_cmd *cmd,
		      unsigned int init_ticks,
		      unsigned int scan_ticks, unsigned int chan_ticks)
{

	unsigned long tmp = 0;

	/* Write timer arguments */
	ai_write_timer(dev, init_ticks, scan_ticks, chan_ticks);

	/* Reset control register */
	outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

	/* Start sources */
	if ((cmd->start_src == TRIG_EXT &&
	     cmd->scan_begin_src == TRIG_TIMER &&
	     cmd->convert_src == TRIG_TIMER) ||
	    (cmd->start_src == TRIG_EXT &&
	     cmd->scan_begin_src == TRIG_FOLLOW &&
	     cmd->convert_src == TRIG_TIMER)) {
		tmp = ME4000_AI_CTRL_BIT_MODE_1 |
		    ME4000_AI_CTRL_BIT_CHANNEL_FIFO |
		    ME4000_AI_CTRL_BIT_DATA_FIFO;
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_EXT &&
		   cmd->convert_src == TRIG_TIMER) {
		tmp = ME4000_AI_CTRL_BIT_MODE_2 |
		    ME4000_AI_CTRL_BIT_CHANNEL_FIFO |
		    ME4000_AI_CTRL_BIT_DATA_FIFO;
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_EXT &&
		   cmd->convert_src == TRIG_EXT) {
		tmp = ME4000_AI_CTRL_BIT_MODE_0 |
		    ME4000_AI_CTRL_BIT_MODE_1 |
		    ME4000_AI_CTRL_BIT_CHANNEL_FIFO |
		    ME4000_AI_CTRL_BIT_DATA_FIFO;
	} else {
		tmp = ME4000_AI_CTRL_BIT_MODE_0 |
		    ME4000_AI_CTRL_BIT_CHANNEL_FIFO |
		    ME4000_AI_CTRL_BIT_DATA_FIFO;
	}

	/* Stop triggers */
	if (cmd->stop_src == TRIG_COUNT) {
		outl(cmd->chanlist_len * cmd->stop_arg,
			    dev->iobase + ME4000_AI_SAMPLE_COUNTER_REG);
		tmp |= ME4000_AI_CTRL_BIT_HF_IRQ | ME4000_AI_CTRL_BIT_SC_IRQ;
	} else if (cmd->stop_src == TRIG_NONE &&
		   cmd->scan_end_src == TRIG_COUNT) {
		outl(cmd->scan_end_arg,
			    dev->iobase + ME4000_AI_SAMPLE_COUNTER_REG);
		tmp |= ME4000_AI_CTRL_BIT_HF_IRQ | ME4000_AI_CTRL_BIT_SC_IRQ;
	} else {
		tmp |= ME4000_AI_CTRL_BIT_HF_IRQ;
	}

	/* Write the setup to the control register */
	outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

	/* Write the channel list */
	ai_write_chanlist(dev, s, cmd);

	return 0;
}

static int ai_write_chanlist(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	unsigned int entry;
	unsigned int chan;
	unsigned int rang;
	unsigned int aref;
	int i;

	for (i = 0; i < cmd->chanlist_len; i++) {
		chan = CR_CHAN(cmd->chanlist[i]);
		rang = CR_RANGE(cmd->chanlist[i]);
		aref = CR_AREF(cmd->chanlist[i]);

		entry = chan;

		if (rang == 0)
			entry |= ME4000_AI_LIST_RANGE_UNIPOLAR_2_5;
		else if (rang == 1)
			entry |= ME4000_AI_LIST_RANGE_UNIPOLAR_10;
		else if (rang == 2)
			entry |= ME4000_AI_LIST_RANGE_BIPOLAR_2_5;
		else
			entry |= ME4000_AI_LIST_RANGE_BIPOLAR_10;

		if (aref == SDF_DIFF)
			entry |= ME4000_AI_LIST_INPUT_DIFFERENTIAL;
		else
			entry |= ME4000_AI_LIST_INPUT_SINGLE_ENDED;

		outl(entry, dev->iobase + ME4000_AI_CHANNEL_LIST_REG);
	}

	return 0;
}

static int me4000_ai_do_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	int err;
	unsigned int init_ticks = 0;
	unsigned int scan_ticks = 0;
	unsigned int chan_ticks = 0;
	struct comedi_cmd *cmd = &s->async->cmd;

	/* Reset the analog input */
	err = me4000_ai_cancel(dev, s);
	if (err)
		return err;

	/* Round the timer arguments */
	err = ai_round_cmd_args(dev,
				s, cmd, &init_ticks, &scan_ticks, &chan_ticks);
	if (err)
		return err;

	/* Prepare the AI for acquisition */
	err = ai_prepare(dev, s, cmd, init_ticks, scan_ticks, chan_ticks);
	if (err)
		return err;

	/* Start acquistion by dummy read */
	inl(dev->iobase + ME4000_AI_START_REG);

	return 0;
}

/*
 * me4000_ai_do_cmd_test():
 *
 * The demo cmd.c in ./comedilib/demo specifies 6 return values:
 * - success
 * - invalid source
 * - source conflict
 * - invalid argument
 * - argument conflict
 * - invalid chanlist
 * So I tried to adopt this scheme.
 */
static int me4000_ai_do_cmd_test(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_cmd *cmd)
{

	unsigned int init_ticks;
	unsigned int chan_ticks;
	unsigned int scan_ticks;
	int err = 0;

	/* Only rounding flags are implemented */
	cmd->flags &= TRIG_ROUND_NEAREST | TRIG_ROUND_UP | TRIG_ROUND_DOWN;

	/* Round the timer arguments */
	ai_round_cmd_args(dev, s, cmd, &init_ticks, &scan_ticks, &chan_ticks);

	/*
	 * Stage 1. Check if the trigger sources are generally valid.
	 */
	switch (cmd->start_src) {
	case TRIG_NOW:
	case TRIG_EXT:
		break;
	case TRIG_ANY:
		cmd->start_src &= TRIG_NOW | TRIG_EXT;
		err++;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Invalid start source\n", dev->minor);
		cmd->start_src = TRIG_NOW;
		err++;
	}
	switch (cmd->scan_begin_src) {
	case TRIG_FOLLOW:
	case TRIG_TIMER:
	case TRIG_EXT:
		break;
	case TRIG_ANY:
		cmd->scan_begin_src &= TRIG_FOLLOW | TRIG_TIMER | TRIG_EXT;
		err++;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Invalid scan begin source\n", dev->minor);
		cmd->scan_begin_src = TRIG_FOLLOW;
		err++;
	}
	switch (cmd->convert_src) {
	case TRIG_TIMER:
	case TRIG_EXT:
		break;
	case TRIG_ANY:
		cmd->convert_src &= TRIG_TIMER | TRIG_EXT;
		err++;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Invalid convert source\n", dev->minor);
		cmd->convert_src = TRIG_TIMER;
		err++;
	}
	switch (cmd->scan_end_src) {
	case TRIG_NONE:
	case TRIG_COUNT:
		break;
	case TRIG_ANY:
		cmd->scan_end_src &= TRIG_NONE | TRIG_COUNT;
		err++;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Invalid scan end source\n", dev->minor);
		cmd->scan_end_src = TRIG_NONE;
		err++;
	}
	switch (cmd->stop_src) {
	case TRIG_NONE:
	case TRIG_COUNT:
		break;
	case TRIG_ANY:
		cmd->stop_src &= TRIG_NONE | TRIG_COUNT;
		err++;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Invalid stop source\n", dev->minor);
		cmd->stop_src = TRIG_NONE;
		err++;
	}
	if (err)
		return 1;

	/*
	 * Stage 2. Check for trigger source conflicts.
	 */
	if (cmd->start_src == TRIG_NOW &&
	    cmd->scan_begin_src == TRIG_TIMER &&
	    cmd->convert_src == TRIG_TIMER) {
	} else if (cmd->start_src == TRIG_NOW &&
		   cmd->scan_begin_src == TRIG_FOLLOW &&
		   cmd->convert_src == TRIG_TIMER) {
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_TIMER &&
		   cmd->convert_src == TRIG_TIMER) {
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_FOLLOW &&
		   cmd->convert_src == TRIG_TIMER) {
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_EXT &&
		   cmd->convert_src == TRIG_TIMER) {
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_EXT &&
		   cmd->convert_src == TRIG_EXT) {
	} else {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Invalid start trigger combination\n", dev->minor);
		cmd->start_src = TRIG_NOW;
		cmd->scan_begin_src = TRIG_FOLLOW;
		cmd->convert_src = TRIG_TIMER;
		err++;
	}

	if (cmd->stop_src == TRIG_NONE && cmd->scan_end_src == TRIG_NONE) {
	} else if (cmd->stop_src == TRIG_COUNT &&
		   cmd->scan_end_src == TRIG_NONE) {
	} else if (cmd->stop_src == TRIG_NONE &&
		   cmd->scan_end_src == TRIG_COUNT) {
	} else if (cmd->stop_src == TRIG_COUNT &&
		   cmd->scan_end_src == TRIG_COUNT) {
	} else {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Invalid stop trigger combination\n", dev->minor);
		cmd->stop_src = TRIG_NONE;
		cmd->scan_end_src = TRIG_NONE;
		err++;
	}
	if (err)
		return 2;

	/*
	 * Stage 3. Check if arguments are generally valid.
	 */
	if (cmd->chanlist_len < 1) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "No channel list\n", dev->minor);
		cmd->chanlist_len = 1;
		err++;
	}
	if (init_ticks < 66) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Start arg to low\n", dev->minor);
		cmd->start_arg = 2000;
		err++;
	}
	if (scan_ticks && scan_ticks < 67) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Scan begin arg to low\n", dev->minor);
		cmd->scan_begin_arg = 2031;
		err++;
	}
	if (chan_ticks < 66) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
		       "Convert arg to low\n", dev->minor);
		cmd->convert_arg = 2000;
		err++;
	}

	if (err)
		return 3;

	/*
	 * Stage 4. Check for argument conflicts.
	 */
	if (cmd->start_src == TRIG_NOW &&
	    cmd->scan_begin_src == TRIG_TIMER &&
	    cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid start arg\n", dev->minor);
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid convert arg\n", dev->minor);
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (scan_ticks <= cmd->chanlist_len * chan_ticks) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid scan end arg\n", dev->minor);

			/*  At least one tick more */
			cmd->scan_end_arg = 2000 * cmd->chanlist_len + 31;
			err++;
		}
	} else if (cmd->start_src == TRIG_NOW &&
		   cmd->scan_begin_src == TRIG_FOLLOW &&
		   cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid start arg\n", dev->minor);
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid convert arg\n", dev->minor);
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_TIMER &&
		   cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid start arg\n", dev->minor);
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid convert arg\n", dev->minor);
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (scan_ticks <= cmd->chanlist_len * chan_ticks) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid scan end arg\n", dev->minor);

			/*  At least one tick more */
			cmd->scan_end_arg = 2000 * cmd->chanlist_len + 31;
			err++;
		}
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_FOLLOW &&
		   cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid start arg\n", dev->minor);
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid convert arg\n", dev->minor);
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_EXT &&
		   cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid start arg\n", dev->minor);
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid convert arg\n", dev->minor);
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_EXT &&
		   cmd->convert_src == TRIG_EXT) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid start arg\n", dev->minor);
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (cmd->stop_arg == 0) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid stop arg\n", dev->minor);
			cmd->stop_arg = 1;
			err++;
		}
	}
	if (cmd->scan_end_src == TRIG_COUNT) {
		if (cmd->scan_end_arg == 0) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_do_cmd_test(): "
			       "Invalid scan end arg\n", dev->minor);
			cmd->scan_end_arg = 1;
			err++;
		}
	}

	if (err)
		return 4;

	/*
	 * Stage 5. Check the channel list.
	 */
	if (ai_check_chanlist(dev, s, cmd))
		return 5;

	return 0;
}

static irqreturn_t me4000_ai_isr(int irq, void *dev_id)
{
	unsigned int tmp;
	struct comedi_device *dev = dev_id;
	struct comedi_subdevice *s = &dev->subdevices[0];
	int i;
	int c = 0;
	long lval;

	if (!dev->attached)
		return IRQ_NONE;

	/* Reset all events */
	s->async->events = 0;

	/* Check if irq number is right */
	if (irq != info->irq) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ai_isr(): "
		       "Incorrect interrupt num: %d\n", dev->minor, irq);
		return IRQ_HANDLED;
	}

	if (inl(dev->iobase + ME4000_IRQ_STATUS_REG) &
	    ME4000_IRQ_STATUS_BIT_AI_HF) {
		/* Read status register to find out what happened */
		tmp = inl(dev->iobase + ME4000_AI_CTRL_REG);

		if (!(tmp & ME4000_AI_STATUS_BIT_FF_DATA) &&
		    !(tmp & ME4000_AI_STATUS_BIT_HF_DATA) &&
		    (tmp & ME4000_AI_STATUS_BIT_EF_DATA)) {
			c = ME4000_AI_FIFO_COUNT;

			/*
			 * FIFO overflow, so stop conversion
			 * and disable all interrupts
			 */
			tmp |= ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
			tmp &= ~(ME4000_AI_CTRL_BIT_HF_IRQ |
				 ME4000_AI_CTRL_BIT_SC_IRQ);
			outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

			s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;

			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_isr(): "
			       "FIFO overflow\n", dev->minor);
		} else if ((tmp & ME4000_AI_STATUS_BIT_FF_DATA)
			   && !(tmp & ME4000_AI_STATUS_BIT_HF_DATA)
			   && (tmp & ME4000_AI_STATUS_BIT_EF_DATA)) {
			s->async->events |= COMEDI_CB_BLOCK;

			c = ME4000_AI_FIFO_COUNT / 2;
		} else {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_isr(): "
			       "Can't determine state of fifo\n", dev->minor);
			c = 0;

			/*
			 * Undefined state, so stop conversion
			 * and disable all interrupts
			 */
			tmp |= ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
			tmp &= ~(ME4000_AI_CTRL_BIT_HF_IRQ |
				 ME4000_AI_CTRL_BIT_SC_IRQ);
			outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

			s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;

			printk(KERN_ERR
			       "comedi%d: me4000: me4000_ai_isr(): "
			       "Undefined FIFO state\n", dev->minor);
		}

		for (i = 0; i < c; i++) {
			/* Read value from data fifo */
			lval = inl(dev->iobase + ME4000_AI_DATA_REG) & 0xFFFF;
			lval ^= 0x8000;

			if (!comedi_buf_put(s->async, lval)) {
				/*
				 * Buffer overflow, so stop conversion
				 * and disable all interrupts
				 */
				tmp |= ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
				tmp &= ~(ME4000_AI_CTRL_BIT_HF_IRQ |
					 ME4000_AI_CTRL_BIT_SC_IRQ);
				outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

				s->async->events |= COMEDI_CB_OVERFLOW;

				printk(KERN_ERR
				       "comedi%d: me4000: me4000_ai_isr(): "
				       "Buffer overflow\n", dev->minor);

				break;
			}
		}

		/* Work is done, so reset the interrupt */
		tmp |= ME4000_AI_CTRL_BIT_HF_IRQ_RESET;
		outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);
		tmp &= ~ME4000_AI_CTRL_BIT_HF_IRQ_RESET;
		outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);
	}

	if (inl(dev->iobase + ME4000_IRQ_STATUS_REG) &
	    ME4000_IRQ_STATUS_BIT_SC) {
		s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOA;

		/*
		 * Acquisition is complete, so stop
		 * conversion and disable all interrupts
		 */
		tmp = inl(dev->iobase + ME4000_AI_CTRL_REG);
		tmp |= ME4000_AI_CTRL_BIT_IMMEDIATE_STOP;
		tmp &= ~(ME4000_AI_CTRL_BIT_HF_IRQ | ME4000_AI_CTRL_BIT_SC_IRQ);
		outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);

		/* Poll data until fifo empty */
		while (inl(dev->iobase + ME4000_AI_CTRL_REG) &
		       ME4000_AI_STATUS_BIT_EF_DATA) {
			/* Read value from data fifo */
			lval = inl(dev->iobase + ME4000_AI_DATA_REG) & 0xFFFF;
			lval ^= 0x8000;

			if (!comedi_buf_put(s->async, lval)) {
				printk(KERN_ERR
				       "comedi%d: me4000: me4000_ai_isr(): "
				       "Buffer overflow\n", dev->minor);
				s->async->events |= COMEDI_CB_OVERFLOW;
				break;
			}
		}

		/* Work is done, so reset the interrupt */
		tmp |= ME4000_AI_CTRL_BIT_SC_IRQ_RESET;
		outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);
		tmp &= ~ME4000_AI_CTRL_BIT_SC_IRQ_RESET;
		outl(tmp, dev->iobase + ME4000_AI_CTRL_REG);
	}

	if (s->async->events)
		comedi_event(dev, s);

	return IRQ_HANDLED;
}

/*=============================================================================
  Analog output section
  ===========================================================================*/

static int me4000_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	const struct me4000_board *thisboard = comedi_board(dev);
	int chan = CR_CHAN(insn->chanspec);
	int rang = CR_RANGE(insn->chanspec);
	int aref = CR_AREF(insn->chanspec);
	unsigned long tmp;

	if (insn->n == 0) {
		return 0;
	} else if (insn->n > 1) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ao_insn_write(): "
		       "Invalid instruction length %d\n", dev->minor, insn->n);
		return -EINVAL;
	}

	if (chan >= thisboard->ao_nchan) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ao_insn_write(): "
		       "Invalid channel %d\n", dev->minor, insn->n);
		return -EINVAL;
	}

	if (rang != 0) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ao_insn_write(): "
		       "Invalid range %d\n", dev->minor, insn->n);
		return -EINVAL;
	}

	if (aref != AREF_GROUND && aref != AREF_COMMON) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_ao_insn_write(): "
		       "Invalid aref %d\n", dev->minor, insn->n);
		return -EINVAL;
	}

	/* Stop any running conversion */
	tmp = inl(info->ao_context[chan].ctrl_reg);
	tmp |= ME4000_AO_CTRL_BIT_IMMEDIATE_STOP;
	outl(tmp, info->ao_context[chan].ctrl_reg);

	/* Clear control register and set to single mode */
	outl(0x0, info->ao_context[chan].ctrl_reg);

	/* Write data value */
	outl(data[0], info->ao_context[chan].single_reg);

	/* Store in the mirror */
	info->ao_context[chan].mirror = data[0];

	return 1;
}

static int me4000_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);

	if (insn->n == 0) {
		return 0;
	} else if (insn->n > 1) {
		printk
		    ("comedi%d: me4000: me4000_ao_insn_read(): "
		     "Invalid instruction length\n", dev->minor);
		return -EINVAL;
	}

	data[0] = info->ao_context[chan].mirror;

	return 1;
}

/*=============================================================================
  Digital I/O section
  ===========================================================================*/

static int me4000_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	/*
	 * The insn data consists of a mask in data[0] and the new data
	 * in data[1]. The mask defines which bits we are concerning about.
	 * The new data must be anded with the mask.
	 * Each channel corresponds to a bit.
	 */
	if (data[0]) {
		/* Check if requested ports are configured for output */
		if ((s->io_bits & data[0]) != data[0])
			return -EIO;

		s->state &= ~data[0];
		s->state |= data[0] & data[1];

		/* Write out the new digital output lines */
		outl((s->state >> 0) & 0xFF,
			    dev->iobase + ME4000_DIO_PORT_0_REG);
		outl((s->state >> 8) & 0xFF,
			    dev->iobase + ME4000_DIO_PORT_1_REG);
		outl((s->state >> 16) & 0xFF,
			    dev->iobase + ME4000_DIO_PORT_2_REG);
		outl((s->state >> 24) & 0xFF,
			    dev->iobase + ME4000_DIO_PORT_3_REG);
	}

	/* On return, data[1] contains the value of
	   the digital input and output lines. */
	data[1] = ((inl(dev->iobase + ME4000_DIO_PORT_0_REG) & 0xFF) << 0) |
		  ((inl(dev->iobase + ME4000_DIO_PORT_1_REG) & 0xFF) << 8) |
		  ((inl(dev->iobase + ME4000_DIO_PORT_2_REG) & 0xFF) << 16) |
		  ((inl(dev->iobase + ME4000_DIO_PORT_3_REG) & 0xFF) << 24);

	return insn->n;
}

static int me4000_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	unsigned long tmp;
	int chan = CR_CHAN(insn->chanspec);

	switch (data[0]) {
	default:
		return -EINVAL;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
	case INSN_CONFIG_DIO_INPUT:
	case INSN_CONFIG_DIO_OUTPUT:
		break;
	}

	/*
	 * The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * value INSN_CONFIG_DIO_INPUT or INSN_CONFIG_DIO_OUTPUT.
	 * On the ME-4000 it is only possible to switch port wise (8 bit)
	 */

	tmp = inl(dev->iobase + ME4000_DIO_CTRL_REG);

	if (data[0] == INSN_CONFIG_DIO_OUTPUT) {
		if (chan < 8) {
			s->io_bits |= 0xFF;
			tmp &= ~(ME4000_DIO_CTRL_BIT_MODE_0 |
				 ME4000_DIO_CTRL_BIT_MODE_1);
			tmp |= ME4000_DIO_CTRL_BIT_MODE_0;
		} else if (chan < 16) {
			/*
			 * Chech for optoisolated ME-4000 version.
			 * If one the first port is a fixed output
			 * port and the second is a fixed input port.
			 */
			if (!inl(dev->iobase + ME4000_DIO_DIR_REG))
				return -ENODEV;

			s->io_bits |= 0xFF00;
			tmp &= ~(ME4000_DIO_CTRL_BIT_MODE_2 |
				 ME4000_DIO_CTRL_BIT_MODE_3);
			tmp |= ME4000_DIO_CTRL_BIT_MODE_2;
		} else if (chan < 24) {
			s->io_bits |= 0xFF0000;
			tmp &= ~(ME4000_DIO_CTRL_BIT_MODE_4 |
				 ME4000_DIO_CTRL_BIT_MODE_5);
			tmp |= ME4000_DIO_CTRL_BIT_MODE_4;
		} else if (chan < 32) {
			s->io_bits |= 0xFF000000;
			tmp &= ~(ME4000_DIO_CTRL_BIT_MODE_6 |
				 ME4000_DIO_CTRL_BIT_MODE_7);
			tmp |= ME4000_DIO_CTRL_BIT_MODE_6;
		} else {
			return -EINVAL;
		}
	} else {
		if (chan < 8) {
			/*
			 * Chech for optoisolated ME-4000 version.
			 * If one the first port is a fixed output
			 * port and the second is a fixed input port.
			 */
			if (!inl(dev->iobase + ME4000_DIO_DIR_REG))
				return -ENODEV;

			s->io_bits &= ~0xFF;
			tmp &= ~(ME4000_DIO_CTRL_BIT_MODE_0 |
				 ME4000_DIO_CTRL_BIT_MODE_1);
		} else if (chan < 16) {
			s->io_bits &= ~0xFF00;
			tmp &= ~(ME4000_DIO_CTRL_BIT_MODE_2 |
				 ME4000_DIO_CTRL_BIT_MODE_3);
		} else if (chan < 24) {
			s->io_bits &= ~0xFF0000;
			tmp &= ~(ME4000_DIO_CTRL_BIT_MODE_4 |
				 ME4000_DIO_CTRL_BIT_MODE_5);
		} else if (chan < 32) {
			s->io_bits &= ~0xFF000000;
			tmp &= ~(ME4000_DIO_CTRL_BIT_MODE_6 |
				 ME4000_DIO_CTRL_BIT_MODE_7);
		} else {
			return -EINVAL;
		}
	}

	outl(tmp, dev->iobase + ME4000_DIO_CTRL_REG);

	return 1;
}

/*=============================================================================
  Counter section
  ===========================================================================*/

static int cnt_reset(struct comedi_device *dev, unsigned int channel)
{
	switch (channel) {
	case 0:
		outb(0x30, info->timer_regbase + ME4000_CNT_CTRL_REG);
		outb(0x00, info->timer_regbase + ME4000_CNT_COUNTER_0_REG);
		outb(0x00, info->timer_regbase + ME4000_CNT_COUNTER_0_REG);
		break;
	case 1:
		outb(0x70, info->timer_regbase + ME4000_CNT_CTRL_REG);
		outb(0x00, info->timer_regbase + ME4000_CNT_COUNTER_1_REG);
		outb(0x00, info->timer_regbase + ME4000_CNT_COUNTER_1_REG);
		break;
	case 2:
		outb(0xB0, info->timer_regbase + ME4000_CNT_CTRL_REG);
		outb(0x00, info->timer_regbase + ME4000_CNT_COUNTER_2_REG);
		outb(0x00, info->timer_regbase + ME4000_CNT_COUNTER_2_REG);
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: cnt_reset(): Invalid channel\n",
		       dev->minor);
		return -EINVAL;
	}

	return 0;
}

static int cnt_config(struct comedi_device *dev, unsigned int channel,
		      unsigned int mode)
{
	int tmp = 0;

	switch (channel) {
	case 0:
		tmp |= ME4000_CNT_COUNTER_0;
		break;
	case 1:
		tmp |= ME4000_CNT_COUNTER_1;
		break;
	case 2:
		tmp |= ME4000_CNT_COUNTER_2;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: cnt_config(): Invalid channel\n",
		       dev->minor);
		return -EINVAL;
	}

	switch (mode) {
	case 0:
		tmp |= ME4000_CNT_MODE_0;
		break;
	case 1:
		tmp |= ME4000_CNT_MODE_1;
		break;
	case 2:
		tmp |= ME4000_CNT_MODE_2;
		break;
	case 3:
		tmp |= ME4000_CNT_MODE_3;
		break;
	case 4:
		tmp |= ME4000_CNT_MODE_4;
		break;
	case 5:
		tmp |= ME4000_CNT_MODE_5;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: cnt_config(): Invalid counter mode\n",
		       dev->minor);
		return -EINVAL;
	}

	/* Write the control word */
	tmp |= 0x30;
	outb(tmp, info->timer_regbase + ME4000_CNT_CTRL_REG);

	return 0;
}

static int me4000_cnt_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{

	int err;

	switch (data[0]) {
	case GPCT_RESET:
		if (insn->n != 1) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_cnt_insn_config(): "
			       "Invalid instruction length%d\n",
			       dev->minor, insn->n);
			return -EINVAL;
		}

		err = cnt_reset(dev, insn->chanspec);
		if (err)
			return err;
		break;
	case GPCT_SET_OPERATION:
		if (insn->n != 2) {
			printk(KERN_ERR
			       "comedi%d: me4000: me4000_cnt_insn_config(): "
			       "Invalid instruction length%d\n",
			       dev->minor, insn->n);
			return -EINVAL;
		}

		err = cnt_config(dev, insn->chanspec, data[1]);
		if (err)
			return err;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_cnt_insn_config(): "
		       "Invalid instruction\n", dev->minor);
		return -EINVAL;
	}

	return 2;
}

static int me4000_cnt_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{

	unsigned short tmp;

	if (insn->n == 0)
		return 0;

	if (insn->n > 1) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_cnt_insn_read(): "
		       "Invalid instruction length %d\n",
		       dev->minor, insn->n);
		return -EINVAL;
	}

	switch (insn->chanspec) {
	case 0:
		tmp = inb(info->timer_regbase + ME4000_CNT_COUNTER_0_REG);
		data[0] = tmp;
		tmp = inb(info->timer_regbase + ME4000_CNT_COUNTER_0_REG);
		data[0] |= tmp << 8;
		break;
	case 1:
		tmp = inb(info->timer_regbase + ME4000_CNT_COUNTER_1_REG);
		data[0] = tmp;
		tmp = inb(info->timer_regbase + ME4000_CNT_COUNTER_1_REG);
		data[0] |= tmp << 8;
		break;
	case 2:
		tmp = inb(info->timer_regbase + ME4000_CNT_COUNTER_2_REG);
		data[0] = tmp;
		tmp = inb(info->timer_regbase + ME4000_CNT_COUNTER_2_REG);
		data[0] |= tmp << 8;
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_cnt_insn_read(): "
		       "Invalid channel %d\n",
		       dev->minor, insn->chanspec);
		return -EINVAL;
	}

	return 1;
}

static int me4000_cnt_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{

	unsigned short tmp;

	if (insn->n == 0) {
		return 0;
	} else if (insn->n > 1) {
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_cnt_insn_write(): "
		       "Invalid instruction length %d\n",
		       dev->minor, insn->n);
		return -EINVAL;
	}

	switch (insn->chanspec) {
	case 0:
		tmp = data[0] & 0xFF;
		outb(tmp, info->timer_regbase + ME4000_CNT_COUNTER_0_REG);
		tmp = (data[0] >> 8) & 0xFF;
		outb(tmp, info->timer_regbase + ME4000_CNT_COUNTER_0_REG);
		break;
	case 1:
		tmp = data[0] & 0xFF;
		outb(tmp, info->timer_regbase + ME4000_CNT_COUNTER_1_REG);
		tmp = (data[0] >> 8) & 0xFF;
		outb(tmp, info->timer_regbase + ME4000_CNT_COUNTER_1_REG);
		break;
	case 2:
		tmp = data[0] & 0xFF;
		outb(tmp, info->timer_regbase + ME4000_CNT_COUNTER_2_REG);
		tmp = (data[0] >> 8) & 0xFF;
		outb(tmp, info->timer_regbase + ME4000_CNT_COUNTER_2_REG);
		break;
	default:
		printk(KERN_ERR
		       "comedi%d: me4000: me4000_cnt_insn_write(): "
		       "Invalid channel %d\n",
		       dev->minor, insn->chanspec);
		return -EINVAL;
	}

	return 1;
}

static int me4000_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct me4000_board *thisboard;
	struct comedi_subdevice *s;
	int result;

	result = me4000_probe(dev, it);
	if (result)
		return result;
	thisboard = comedi_board(dev);

	result = comedi_alloc_subdevices(dev, 4);
	if (result)
		return result;

    /*=========================================================================
      Analog input subdevice
      ========================================================================*/

	s = &dev->subdevices[0];

	if (thisboard->ai_nchan) {
		s->type = COMEDI_SUBD_AI;
		s->subdev_flags =
		    SDF_READABLE | SDF_COMMON | SDF_GROUND | SDF_DIFF;
		s->n_chan = thisboard->ai_nchan;
		s->maxdata = 0xFFFF;	/*  16 bit ADC */
		s->len_chanlist = ME4000_AI_CHANNEL_LIST_COUNT;
		s->range_table = &me4000_ai_range;
		s->insn_read = me4000_ai_insn_read;

		if (info->irq > 0) {
			if (request_irq(info->irq, me4000_ai_isr,
					IRQF_SHARED, "ME-4000", dev)) {
				printk
				    ("comedi%d: me4000: me4000_attach(): "
				     "Unable to allocate irq\n", dev->minor);
			} else {
				dev->read_subdev = s;
				s->subdev_flags |= SDF_CMD_READ;
				s->cancel = me4000_ai_cancel;
				s->do_cmdtest = me4000_ai_do_cmd_test;
				s->do_cmd = me4000_ai_do_cmd;
			}
		} else {
			printk(KERN_WARNING
			       "comedi%d: me4000: me4000_attach(): "
			       "No interrupt available\n", dev->minor);
		}
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

    /*=========================================================================
      Analog output subdevice
      ========================================================================*/

	s = &dev->subdevices[1];

	if (thisboard->ao_nchan) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_COMMON | SDF_GROUND;
		s->n_chan = thisboard->ao_nchan;
		s->maxdata = 0xFFFF;	/*  16 bit DAC */
		s->range_table = &me4000_ao_range;
		s->insn_write = me4000_ao_insn_write;
		s->insn_read = me4000_ao_insn_read;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

    /*=========================================================================
      Digital I/O subdevice
      ========================================================================*/

	s = &dev->subdevices[2];

	if (thisboard->dio_nchan) {
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan = thisboard->dio_nchan;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = me4000_dio_insn_bits;
		s->insn_config = me4000_dio_insn_config;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*
	 * Check for optoisolated ME-4000 version. If one the first
	 * port is a fixed output port and the second is a fixed input port.
	 */
	if (!inl(dev->iobase + ME4000_DIO_DIR_REG)) {
		s->io_bits |= 0xFF;
		outl(ME4000_DIO_CTRL_BIT_MODE_0,
			dev->iobase + ME4000_DIO_DIR_REG);
	}

    /*=========================================================================
      Counter subdevice
      ========================================================================*/

	s = &dev->subdevices[3];

	if (thisboard->has_counter) {
		s->type = COMEDI_SUBD_COUNTER;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan = 3;
		s->maxdata = 0xFFFF;	/*  16 bit counters */
		s->insn_read = me4000_cnt_insn_read;
		s->insn_write = me4000_cnt_insn_write;
		s->insn_config = me4000_cnt_insn_config;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	return 0;
}

static void me4000_detach(struct comedi_device *dev)
{
	if (info) {
		if (info->pci_dev_p) {
			reset_board(dev);
			if (info->plx_regbase)
				comedi_pci_disable(info->pci_dev_p);
			pci_dev_put(info->pci_dev_p);
		}
	}
}

static struct comedi_driver me4000_driver = {
	.driver_name	= "me4000",
	.module		= THIS_MODULE,
	.attach		= me4000_attach,
	.detach		= me4000_detach,
};

static int __devinit me4000_pci_probe(struct pci_dev *dev,
				      const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &me4000_driver);
}

static void __devexit me4000_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(me4000_pci_table) = {
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4650)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4660)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4660I)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4660S)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4660IS)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670I)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670S)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670IS)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680I)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680S)},
	{PCI_DEVICE(PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680IS)},
	{0}
};
MODULE_DEVICE_TABLE(pci, me4000_pci_table);

static struct pci_driver me4000_pci_driver = {
	.name		= "me4000",
	.id_table	= me4000_pci_table,
	.probe		= me4000_pci_probe,
	.remove		= __devexit_p(me4000_pci_remove),
};
module_comedi_pci_driver(me4000_driver, me4000_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
