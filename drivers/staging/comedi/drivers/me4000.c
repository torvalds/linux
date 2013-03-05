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

Configuration Options: not applicable, uses PCI auto config

The firmware required by these boards is available in the
comedi_nonfree_firmware tarball available from
http://www.comedi.org.  However, the driver's support for
loading the firmware through comedi_config is currently
broken.

 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "8253.h"

#if 0
/* file removed due to GPL incompatibility */
#include "me4000_fw.h"
#endif

/*
 * ME4000 Register map and bit defines
 */
#define ME4000_AO_CHAN(x)			((x) * 0x18)

#define ME4000_AO_CTRL_REG(x)			(0x00 + ME4000_AO_CHAN(x))
#define ME4000_AO_CTRL_BIT_MODE_0		(1 << 0)
#define ME4000_AO_CTRL_BIT_MODE_1		(1 << 1)
#define ME4000_AO_CTRL_MASK_MODE		(3 << 0)
#define ME4000_AO_CTRL_BIT_STOP			(1 << 2)
#define ME4000_AO_CTRL_BIT_ENABLE_FIFO		(1 << 3)
#define ME4000_AO_CTRL_BIT_ENABLE_EX_TRIG	(1 << 4)
#define ME4000_AO_CTRL_BIT_EX_TRIG_EDGE		(1 << 5)
#define ME4000_AO_CTRL_BIT_IMMEDIATE_STOP	(1 << 7)
#define ME4000_AO_CTRL_BIT_ENABLE_DO		(1 << 8)
#define ME4000_AO_CTRL_BIT_ENABLE_IRQ		(1 << 9)
#define ME4000_AO_CTRL_BIT_RESET_IRQ		(1 << 10)
#define ME4000_AO_STATUS_REG(x)			(0x04 + ME4000_AO_CHAN(x))
#define ME4000_AO_STATUS_BIT_FSM		(1 << 0)
#define ME4000_AO_STATUS_BIT_FF			(1 << 1)
#define ME4000_AO_STATUS_BIT_HF			(1 << 2)
#define ME4000_AO_STATUS_BIT_EF			(1 << 3)
#define ME4000_AO_FIFO_REG(x)			(0x08 + ME4000_AO_CHAN(x))
#define ME4000_AO_SINGLE_REG(x)			(0x0c + ME4000_AO_CHAN(x))
#define ME4000_AO_TIMER_REG(x)			(0x10 + ME4000_AO_CHAN(x))
#define ME4000_AI_CTRL_REG			0x74
#define ME4000_AI_STATUS_REG			0x74
#define ME4000_AI_CTRL_BIT_MODE_0		(1 << 0)
#define ME4000_AI_CTRL_BIT_MODE_1		(1 << 1)
#define ME4000_AI_CTRL_BIT_MODE_2		(1 << 2)
#define ME4000_AI_CTRL_BIT_SAMPLE_HOLD		(1 << 3)
#define ME4000_AI_CTRL_BIT_IMMEDIATE_STOP	(1 << 4)
#define ME4000_AI_CTRL_BIT_STOP			(1 << 5)
#define ME4000_AI_CTRL_BIT_CHANNEL_FIFO		(1 << 6)
#define ME4000_AI_CTRL_BIT_DATA_FIFO		(1 << 7)
#define ME4000_AI_CTRL_BIT_FULLSCALE		(1 << 8)
#define ME4000_AI_CTRL_BIT_OFFSET		(1 << 9)
#define ME4000_AI_CTRL_BIT_EX_TRIG_ANALOG	(1 << 10)
#define ME4000_AI_CTRL_BIT_EX_TRIG		(1 << 11)
#define ME4000_AI_CTRL_BIT_EX_TRIG_FALLING	(1 << 12)
#define ME4000_AI_CTRL_BIT_EX_IRQ		(1 << 13)
#define ME4000_AI_CTRL_BIT_EX_IRQ_RESET		(1 << 14)
#define ME4000_AI_CTRL_BIT_LE_IRQ		(1 << 15)
#define ME4000_AI_CTRL_BIT_LE_IRQ_RESET		(1 << 16)
#define ME4000_AI_CTRL_BIT_HF_IRQ		(1 << 17)
#define ME4000_AI_CTRL_BIT_HF_IRQ_RESET		(1 << 18)
#define ME4000_AI_CTRL_BIT_SC_IRQ		(1 << 19)
#define ME4000_AI_CTRL_BIT_SC_IRQ_RESET		(1 << 20)
#define ME4000_AI_CTRL_BIT_SC_RELOAD		(1 << 21)
#define ME4000_AI_STATUS_BIT_EF_CHANNEL		(1 << 22)
#define ME4000_AI_STATUS_BIT_HF_CHANNEL		(1 << 23)
#define ME4000_AI_STATUS_BIT_FF_CHANNEL		(1 << 24)
#define ME4000_AI_STATUS_BIT_EF_DATA		(1 << 25)
#define ME4000_AI_STATUS_BIT_HF_DATA		(1 << 26)
#define ME4000_AI_STATUS_BIT_FF_DATA		(1 << 27)
#define ME4000_AI_STATUS_BIT_LE			(1 << 28)
#define ME4000_AI_STATUS_BIT_FSM		(1 << 29)
#define ME4000_AI_CTRL_BIT_EX_TRIG_BOTH		(1 << 31)
#define ME4000_AI_CHANNEL_LIST_REG		0x78
#define ME4000_AI_LIST_INPUT_SINGLE_ENDED	(0 << 5)
#define ME4000_AI_LIST_INPUT_DIFFERENTIAL	(1 << 5)
#define ME4000_AI_LIST_RANGE_BIPOLAR_10		(0 << 6)
#define ME4000_AI_LIST_RANGE_BIPOLAR_2_5	(1 << 6)
#define ME4000_AI_LIST_RANGE_UNIPOLAR_10	(2 << 6)
#define ME4000_AI_LIST_RANGE_UNIPOLAR_2_5	(3 << 6)
#define ME4000_AI_LIST_LAST_ENTRY		(1 << 8)
#define ME4000_AI_DATA_REG			0x7c
#define ME4000_AI_CHAN_TIMER_REG		0x80
#define ME4000_AI_CHAN_PRE_TIMER_REG		0x84
#define ME4000_AI_SCAN_TIMER_LOW_REG		0x88
#define ME4000_AI_SCAN_TIMER_HIGH_REG		0x8c
#define ME4000_AI_SCAN_PRE_TIMER_LOW_REG	0x90
#define ME4000_AI_SCAN_PRE_TIMER_HIGH_REG	0x94
#define ME4000_AI_START_REG			0x98
#define ME4000_IRQ_STATUS_REG			0x9c
#define ME4000_IRQ_STATUS_BIT_EX		(1 << 0)
#define ME4000_IRQ_STATUS_BIT_LE		(1 << 1)
#define ME4000_IRQ_STATUS_BIT_AI_HF		(1 << 2)
#define ME4000_IRQ_STATUS_BIT_AO_0_HF		(1 << 3)
#define ME4000_IRQ_STATUS_BIT_AO_1_HF		(1 << 4)
#define ME4000_IRQ_STATUS_BIT_AO_2_HF		(1 << 5)
#define ME4000_IRQ_STATUS_BIT_AO_3_HF		(1 << 6)
#define ME4000_IRQ_STATUS_BIT_SC		(1 << 7)
#define ME4000_DIO_PORT_0_REG			0xa0
#define ME4000_DIO_PORT_1_REG			0xa4
#define ME4000_DIO_PORT_2_REG			0xa8
#define ME4000_DIO_PORT_3_REG			0xac
#define ME4000_DIO_DIR_REG			0xb0
#define ME4000_AO_LOADSETREG_XX			0xb4
#define ME4000_DIO_CTRL_REG			0xb8
#define ME4000_DIO_CTRL_BIT_MODE_0		(1 << 0)
#define ME4000_DIO_CTRL_BIT_MODE_1		(1 << 1)
#define ME4000_DIO_CTRL_BIT_MODE_2		(1 << 2)
#define ME4000_DIO_CTRL_BIT_MODE_3		(1 << 3)
#define ME4000_DIO_CTRL_BIT_MODE_4		(1 << 4)
#define ME4000_DIO_CTRL_BIT_MODE_5		(1 << 5)
#define ME4000_DIO_CTRL_BIT_MODE_6		(1 << 6)
#define ME4000_DIO_CTRL_BIT_MODE_7		(1 << 7)
#define ME4000_DIO_CTRL_BIT_FUNCTION_0		(1 << 8)
#define ME4000_DIO_CTRL_BIT_FUNCTION_1		(1 << 9)
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_0		(1 << 10)
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_1		(1 << 11)
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_2		(1 << 12)
#define ME4000_DIO_CTRL_BIT_FIFO_HIGH_3		(1 << 13)
#define ME4000_AO_DEMUX_ADJUST_REG		0xbc
#define ME4000_AO_DEMUX_ADJUST_VALUE		0x4c
#define ME4000_AI_SAMPLE_COUNTER_REG		0xc0

/*
 * PLX Register map and bit defines
 */
#define PLX_INTCSR				0x4c
#define PLX_INTCSR_LOCAL_INT1_EN		(1 << 0)
#define PLX_INTCSR_LOCAL_INT1_POL		(1 << 1)
#define PLX_INTCSR_LOCAL_INT1_STATE		(1 << 2)
#define PLX_INTCSR_LOCAL_INT2_EN		(1 << 3)
#define PLX_INTCSR_LOCAL_INT2_POL		(1 << 4)
#define PLX_INTCSR_LOCAL_INT2_STATE		(1 << 5)
#define PLX_INTCSR_PCI_INT_EN			(1 << 6)
#define PLX_INTCSR_SOFT_INT			(1 << 7)
#define PLX_ICR					0x50
#define PLX_ICR_BIT_EEPROM_CLOCK_SET		(1 << 24)
#define PLX_ICR_BIT_EEPROM_CHIP_SELECT		(1 << 25)
#define PLX_ICR_BIT_EEPROM_WRITE		(1 << 26)
#define PLX_ICR_BIT_EEPROM_READ			(1 << 27)
#define PLX_ICR_BIT_EEPROM_VALID		(1 << 28)
#define PLX_ICR_MASK_EEPROM			(0x1f << 24)

#define EEPROM_DELAY				1

#define ME4000_AI_FIFO_COUNT			2048

#define ME4000_AI_MIN_TICKS			66
#define ME4000_AI_MIN_SAMPLE_TIME		2000
#define ME4000_AI_BASE_FREQUENCY		(unsigned int) 33E6

#define ME4000_AI_CHANNEL_LIST_COUNT		1024

struct me4000_info {
	unsigned long plx_regbase;
	unsigned long timer_regbase;

	unsigned int ao_readback[4];
};

enum me4000_boardid {
	BOARD_ME4650,
	BOARD_ME4660,
	BOARD_ME4660I,
	BOARD_ME4660S,
	BOARD_ME4660IS,
	BOARD_ME4670,
	BOARD_ME4670I,
	BOARD_ME4670S,
	BOARD_ME4670IS,
	BOARD_ME4680,
	BOARD_ME4680I,
	BOARD_ME4680S,
	BOARD_ME4680IS,
};

struct me4000_board {
	const char *name;
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
	[BOARD_ME4650] = {
		.name		= "ME-4650",
		.ai_nchan	= 16,
		.dio_nchan	= 32,
	},
	[BOARD_ME4660] = {
		.name		= "ME-4660",
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4660I] = {
		.name		= "ME-4660i",
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4660S] = {
		.name		= "ME-4660s",
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4660IS] = {
		.name		= "ME-4660is",
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4670] = {
		.name		= "ME-4670",
		.ao_nchan	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4670I] = {
		.name		= "ME-4670i",
		.ao_nchan	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4670S] = {
		.name		= "ME-4670s",
		.ao_nchan	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4670IS] = {
		.name		= "ME-4670is",
		.ao_nchan	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4680] = {
		.name		= "ME-4680",
		.ao_nchan	= 4,
		.ao_fifo	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4680I] = {
		.name		= "ME-4680i",
		.ao_nchan	= 4,
		.ao_fifo	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4680S] = {
		.name		= "ME-4680s",
		.ao_nchan	= 4,
		.ao_fifo	= 4,
		.ai_nchan	= 32,
		.ai_diff_nchan	= 16,
		.ai_sh_nchan	= 8,
		.ex_trig_analog	= 1,
		.dio_nchan	= 32,
		.has_counter	= 1,
	},
	[BOARD_ME4680IS] = {
		.name		= "ME-4680is",
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

static const struct comedi_lrange me4000_ai_range = {
	4,
	{
	 UNI_RANGE(2.5),
	 UNI_RANGE(10),
	 BIP_RANGE(2.5),
	 BIP_RANGE(10),
	 }
};

#define FIRMWARE_NOT_AVAILABLE 1
#if FIRMWARE_NOT_AVAILABLE
extern unsigned char *xilinx_firm;
#endif

static int xilinx_download(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct me4000_info *info = dev->private;
	unsigned long xilinx_iobase = pci_resource_start(pcidev, 5);
	u32 value = 0;
	wait_queue_head_t queue;
	int idx = 0;
	int size = 0;

	if (!xilinx_iobase)
		return -ENODEV;

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
	inb(xilinx_iobase + 0xC8);

	/* Wait until /INIT pin is set */
	udelay(20);
	if (!(inl(info->plx_regbase + PLX_INTCSR) & 0x20)) {
		dev_err(dev->class_dev, "Can't init Xilinx\n");
		return -EIO;
	}

	/* Reset /CS and /WRITE of the Xilinx */
	value = inl(info->plx_regbase + PLX_ICR);
	value &= ~0x100;
	outl(value, info->plx_regbase + PLX_ICR);
	if (FIRMWARE_NOT_AVAILABLE) {
		dev_err(dev->class_dev,
			"xilinx firmware unavailable due to licensing, aborting");
		return -EIO;
	} else {
		/* Download Xilinx firmware */
		size = (xilinx_firm[0] << 24) + (xilinx_firm[1] << 16) +
		    (xilinx_firm[2] << 8) + xilinx_firm[3];
		udelay(10);

		for (idx = 0; idx < size; idx++) {
			outb(xilinx_firm[16 + idx], xilinx_iobase);
			udelay(10);

			/* Check if BUSY flag is low */
			if (inl(info->plx_regbase + PLX_ICR) & 0x20) {
				dev_err(dev->class_dev,
					"Xilinx is still busy (idx = %d)\n",
					idx);
				return -EIO;
			}
		}
	}

	/* If done flag is high download was successful */
	if (inl(info->plx_regbase + PLX_ICR) & 0x4) {
	} else {
		dev_err(dev->class_dev, "DONE flag is not set\n");
		dev_err(dev->class_dev, "Download not successful\n");
		return -EIO;
	}

	/* Set /CS and /WRITE */
	value = inl(info->plx_regbase + PLX_ICR);
	value |= 0x100;
	outl(value, info->plx_regbase + PLX_ICR);

	return 0;
}

static void me4000_reset(struct comedi_device *dev)
{
	struct me4000_info *info = dev->private;
	unsigned long val;
	int chan;

	/* Make a hardware reset */
	val = inl(info->plx_regbase + PLX_ICR);
	val |= 0x40000000;
	outl(val, info->plx_regbase + PLX_ICR);
	val &= ~0x40000000;
	outl(val , info->plx_regbase + PLX_ICR);

	/* 0x8000 to the DACs means an output voltage of 0V */
	for (chan = 0; chan < 4; chan++)
		outl(0x8000, dev->iobase + ME4000_AO_SINGLE_REG(chan));

	/* Set both stop bits in the analog input control register */
	outl(ME4000_AI_CTRL_BIT_IMMEDIATE_STOP | ME4000_AI_CTRL_BIT_STOP,
		dev->iobase + ME4000_AI_CTRL_REG);

	/* Set both stop bits in the analog output control register */
	val = ME4000_AO_CTRL_BIT_IMMEDIATE_STOP | ME4000_AO_CTRL_BIT_STOP;
	for (chan = 0; chan < 4; chan++)
		outl(val, dev->iobase + ME4000_AO_CTRL_REG(chan));

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
		dev_err(dev->class_dev, "Invalid instruction length %d\n",
			insn->n);
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
		dev_err(dev->class_dev, "Invalid range specified\n");
		return -EINVAL;
	}

	switch (aref) {
	case AREF_GROUND:
	case AREF_COMMON:
		if (chan >= thisboard->ai_nchan) {
			dev_err(dev->class_dev,
				"Analog input is not available\n");
			return -EINVAL;
		}
		entry |= ME4000_AI_LIST_INPUT_SINGLE_ENDED | chan;
		break;

	case AREF_DIFF:
		if (rang == 0 || rang == 1) {
			dev_err(dev->class_dev,
				"Range must be bipolar when aref = diff\n");
			return -EINVAL;
		}

		if (chan >= thisboard->ai_diff_nchan) {
			dev_err(dev->class_dev,
				"Analog input is not available\n");
			return -EINVAL;
		}
		entry |= ME4000_AI_LIST_INPUT_DIFFERENTIAL | chan;
		break;
	default:
		dev_err(dev->class_dev, "Invalid aref specified\n");
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
		dev_err(dev->class_dev, "Value not available after wait\n");
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
		dev_err(dev->class_dev, "No channel list available\n");
		return -EINVAL;
	}

	/* Check the channel list size */
	if (cmd->chanlist_len > ME4000_AI_CHANNEL_LIST_COUNT) {
		dev_err(dev->class_dev, "Channel list is to large\n");
		return -EINVAL;
	}

	/* Check the pointer */
	if (!cmd->chanlist) {
		dev_err(dev->class_dev, "NULL pointer to channel list\n");
		return -EFAULT;
	}

	/* Check whether aref is equal for all entries */
	aref = CR_AREF(cmd->chanlist[0]);
	for (i = 0; i < cmd->chanlist_len; i++) {
		if (CR_AREF(cmd->chanlist[i]) != aref) {
			dev_err(dev->class_dev,
				"Mode is not equal for all entries\n");
			return -EINVAL;
		}
	}

	/* Check whether channels are available for this ending */
	if (aref == SDF_DIFF) {
		for (i = 0; i < cmd->chanlist_len; i++) {
			if (CR_CHAN(cmd->chanlist[i]) >=
			    thisboard->ai_diff_nchan) {
				dev_err(dev->class_dev,
					"Channel number to high\n");
				return -EINVAL;
			}
		}
	} else {
		for (i = 0; i < cmd->chanlist_len; i++) {
			if (CR_CHAN(cmd->chanlist[i]) >= thisboard->ai_nchan) {
				dev_err(dev->class_dev,
					"Channel number to high\n");
				return -EINVAL;
			}
		}
	}

	/* Check if bipolar is set for all entries when in differential mode */
	if (aref == SDF_DIFF) {
		for (i = 0; i < cmd->chanlist_len; i++) {
			if (CR_RANGE(cmd->chanlist[i]) != 1 &&
			    CR_RANGE(cmd->chanlist[i]) != 2) {
				dev_err(dev->class_dev,
				       "Bipolar is not selected in differential mode\n");
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

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_FOLLOW | TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_end_src,
					TRIG_NONE | TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_NONE | TRIG_COUNT);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_end_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

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
		err |= -EINVAL;
	}

	if (cmd->stop_src == TRIG_NONE && cmd->scan_end_src == TRIG_NONE) {
	} else if (cmd->stop_src == TRIG_COUNT &&
		   cmd->scan_end_src == TRIG_NONE) {
	} else if (cmd->stop_src == TRIG_NONE &&
		   cmd->scan_end_src == TRIG_COUNT) {
	} else if (cmd->stop_src == TRIG_COUNT &&
		   cmd->scan_end_src == TRIG_COUNT) {
	} else {
		err |= -EINVAL;
	}

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	if (cmd->chanlist_len < 1) {
		cmd->chanlist_len = 1;
		err |= -EINVAL;
	}
	if (init_ticks < 66) {
		cmd->start_arg = 2000;
		err |= -EINVAL;
	}
	if (scan_ticks && scan_ticks < 67) {
		cmd->scan_begin_arg = 2031;
		err |= -EINVAL;
	}
	if (chan_ticks < 66) {
		cmd->convert_arg = 2000;
		err |= -EINVAL;
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
			dev_err(dev->class_dev, "Invalid start arg\n");
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid convert arg\n");
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (scan_ticks <= cmd->chanlist_len * chan_ticks) {
			dev_err(dev->class_dev, "Invalid scan end arg\n");

			/*  At least one tick more */
			cmd->scan_end_arg = 2000 * cmd->chanlist_len + 31;
			err++;
		}
	} else if (cmd->start_src == TRIG_NOW &&
		   cmd->scan_begin_src == TRIG_FOLLOW &&
		   cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid start arg\n");
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid convert arg\n");
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_TIMER &&
		   cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid start arg\n");
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid convert arg\n");
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (scan_ticks <= cmd->chanlist_len * chan_ticks) {
			dev_err(dev->class_dev, "Invalid scan end arg\n");

			/*  At least one tick more */
			cmd->scan_end_arg = 2000 * cmd->chanlist_len + 31;
			err++;
		}
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_FOLLOW &&
		   cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid start arg\n");
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid convert arg\n");
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_EXT &&
		   cmd->convert_src == TRIG_TIMER) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid start arg\n");
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
		if (chan_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid convert arg\n");
			cmd->convert_arg = 2000;	/*  66 ticks at least */
			err++;
		}
	} else if (cmd->start_src == TRIG_EXT &&
		   cmd->scan_begin_src == TRIG_EXT &&
		   cmd->convert_src == TRIG_EXT) {

		/* Check timer arguments */
		if (init_ticks < ME4000_AI_MIN_TICKS) {
			dev_err(dev->class_dev, "Invalid start arg\n");
			cmd->start_arg = 2000;	/*  66 ticks at least */
			err++;
		}
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (cmd->stop_arg == 0) {
			dev_err(dev->class_dev, "Invalid stop arg\n");
			cmd->stop_arg = 1;
			err++;
		}
	}
	if (cmd->scan_end_src == TRIG_COUNT) {
		if (cmd->scan_end_arg == 0) {
			dev_err(dev->class_dev, "Invalid scan end arg\n");
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
	if (irq != dev->irq) {
		dev_err(dev->class_dev, "Incorrect interrupt num: %d\n", irq);
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

			dev_err(dev->class_dev, "FIFO overflow\n");
		} else if ((tmp & ME4000_AI_STATUS_BIT_FF_DATA)
			   && !(tmp & ME4000_AI_STATUS_BIT_HF_DATA)
			   && (tmp & ME4000_AI_STATUS_BIT_EF_DATA)) {
			s->async->events |= COMEDI_CB_BLOCK;

			c = ME4000_AI_FIFO_COUNT / 2;
		} else {
			dev_err(dev->class_dev,
				"Can't determine state of fifo\n");
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

			dev_err(dev->class_dev, "Undefined FIFO state\n");
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

				dev_err(dev->class_dev, "Buffer overflow\n");

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
				dev_err(dev->class_dev, "Buffer overflow\n");
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
	struct me4000_info *info = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	int rang = CR_RANGE(insn->chanspec);
	int aref = CR_AREF(insn->chanspec);
	unsigned long tmp;

	if (insn->n == 0) {
		return 0;
	} else if (insn->n > 1) {
		dev_err(dev->class_dev, "Invalid instruction length %d\n",
			insn->n);
		return -EINVAL;
	}

	if (chan >= thisboard->ao_nchan) {
		dev_err(dev->class_dev, "Invalid channel %d\n", insn->n);
		return -EINVAL;
	}

	if (rang != 0) {
		dev_err(dev->class_dev, "Invalid range %d\n", insn->n);
		return -EINVAL;
	}

	if (aref != AREF_GROUND && aref != AREF_COMMON) {
		dev_err(dev->class_dev, "Invalid aref %d\n", insn->n);
		return -EINVAL;
	}

	/* Stop any running conversion */
	tmp = inl(dev->iobase + ME4000_AO_CTRL_REG(chan));
	tmp |= ME4000_AO_CTRL_BIT_IMMEDIATE_STOP;
	outl(tmp, dev->iobase + ME4000_AO_CTRL_REG(chan));

	/* Clear control register and set to single mode */
	outl(0x0, dev->iobase + ME4000_AO_CTRL_REG(chan));

	/* Write data value */
	outl(data[0], dev->iobase + ME4000_AO_SINGLE_REG(chan));

	/* Store in the mirror */
	info->ao_readback[chan] = data[0];

	return 1;
}

static int me4000_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct me4000_info *info = dev->private;
	int chan = CR_CHAN(insn->chanspec);

	if (insn->n == 0) {
		return 0;
	} else if (insn->n > 1) {
		dev_err(dev->class_dev, "Invalid instruction length\n");
		return -EINVAL;
	}

	data[0] = info->ao_readback[chan];

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

static int me4000_cnt_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct me4000_info *info = dev->private;
	int err;

	switch (data[0]) {
	case GPCT_RESET:
		if (insn->n != 1)
			return -EINVAL;

		err = i8254_load(info->timer_regbase, 0, insn->chanspec, 0,
				I8254_MODE0 | I8254_BINARY);
		if (err)
			return err;
		break;
	case GPCT_SET_OPERATION:
		if (insn->n != 2)
			return -EINVAL;

		err = i8254_set_mode(info->timer_regbase, 0, insn->chanspec,
				(data[1] << 1) | I8254_BINARY);
		if (err)
			return err;
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static int me4000_cnt_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	struct me4000_info *info = dev->private;

	if (insn->n == 0)
		return 0;

	if (insn->n > 1) {
		dev_err(dev->class_dev, "Invalid instruction length %d\n",
			insn->n);
		return -EINVAL;
	}

	data[0] = i8254_read(info->timer_regbase, 0, insn->chanspec);

	return 1;
}

static int me4000_cnt_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	struct me4000_info *info = dev->private;

	if (insn->n == 0) {
		return 0;
	} else if (insn->n > 1) {
		dev_err(dev->class_dev, "Invalid instruction length %d\n",
			insn->n);
		return -EINVAL;
	}

	i8254_write(info->timer_regbase, 0, insn->chanspec, data[0]);

	return 1;
}

static int me4000_auto_attach(struct comedi_device *dev,
			      unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct me4000_board *thisboard = NULL;
	struct me4000_info *info;
	struct comedi_subdevice *s;
	int result;

	if (context < ARRAY_SIZE(me4000_boards))
		thisboard = &me4000_boards[context];
	if (!thisboard)
		return -ENODEV;
	dev->board_ptr = thisboard;
	dev->board_name = thisboard->name;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	dev->private = info;

	result = comedi_pci_enable(pcidev, dev->board_name);
	if (result)
		return result;

	info->plx_regbase = pci_resource_start(pcidev, 1);
	dev->iobase = pci_resource_start(pcidev, 2);
	info->timer_regbase = pci_resource_start(pcidev, 3);
	if (!info->plx_regbase || !dev->iobase || !info->timer_regbase)
		return -ENODEV;

	result = xilinx_download(dev);
	if (result)
		return result;

	me4000_reset(dev);

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

		if (pcidev->irq > 0) {
			if (request_irq(pcidev->irq, me4000_ai_isr,
					IRQF_SHARED, dev->board_name, dev)) {
				dev_warn(dev->class_dev,
					"request_irq failed\n");
			} else {
				dev->read_subdev = s;
				s->subdev_flags |= SDF_CMD_READ;
				s->cancel = me4000_ai_cancel;
				s->do_cmdtest = me4000_ai_do_cmd_test;
				s->do_cmd = me4000_ai_do_cmd;

				dev->irq = pcidev->irq;
			}
		} else {
			dev_warn(dev->class_dev, "No interrupt available\n");
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
		s->range_table = &range_bipolar10;
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
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->irq)
		free_irq(dev->irq, dev);
	if (pcidev) {
		if (dev->iobase) {
			me4000_reset(dev);
			comedi_pci_disable(pcidev);
		}
	}
}

static struct comedi_driver me4000_driver = {
	.driver_name	= "me4000",
	.module		= THIS_MODULE,
	.auto_attach	= me4000_auto_attach,
	.detach		= me4000_detach,
};

static int me4000_pci_probe(struct pci_dev *dev,
			    const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &me4000_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(me4000_pci_table) = {
	{ PCI_VDEVICE(MEILHAUS, 0x4650), BOARD_ME4650 },
	{ PCI_VDEVICE(MEILHAUS, 0x4660), BOARD_ME4660 },
	{ PCI_VDEVICE(MEILHAUS, 0x4661), BOARD_ME4660I },
	{ PCI_VDEVICE(MEILHAUS, 0x4662), BOARD_ME4660S },
	{ PCI_VDEVICE(MEILHAUS, 0x4663), BOARD_ME4660IS },
	{ PCI_VDEVICE(MEILHAUS, 0x4670), BOARD_ME4670 },
	{ PCI_VDEVICE(MEILHAUS, 0x4671), BOARD_ME4670I },
	{ PCI_VDEVICE(MEILHAUS, 0x4672), BOARD_ME4670S },
	{ PCI_VDEVICE(MEILHAUS, 0x4673), BOARD_ME4670IS },
	{ PCI_VDEVICE(MEILHAUS, 0x4680), BOARD_ME4680 },
	{ PCI_VDEVICE(MEILHAUS, 0x4681), BOARD_ME4680I },
	{ PCI_VDEVICE(MEILHAUS, 0x4682), BOARD_ME4680S },
	{ PCI_VDEVICE(MEILHAUS, 0x4683), BOARD_ME4680IS },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, me4000_pci_table);

static struct pci_driver me4000_pci_driver = {
	.name		= "me4000",
	.id_table	= me4000_pci_table,
	.probe		= me4000_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(me4000_driver, me4000_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
