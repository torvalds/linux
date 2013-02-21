/*
    comedi/drivers/ni_atmio.c
    Hardware driver for NI AT-MIO E series cards

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2001 David A. Schleef <ds@schleef.org>

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
Driver: ni_atmio
Description: National Instruments AT-MIO-E series
Author: ds
Devices: [National Instruments] AT-MIO-16E-1 (ni_atmio),
  AT-MIO-16E-2, AT-MIO-16E-10, AT-MIO-16DE-10, AT-MIO-64E-3,
  AT-MIO-16XE-50, AT-MIO-16XE-10, AT-AI-16XE-10
Status: works
Updated: Thu May  1 20:03:02 CDT 2003

The driver has 2.6 kernel isapnp support, and
will automatically probe for a supported board if the
I/O base is left unspecified with comedi_config.
However, many of
the isapnp id numbers are unknown.  If your board is not
recognized, please send the output of 'cat /proc/isapnp'
(you may need to modprobe the isa-pnp module for
/proc/isapnp to exist) so the
id numbers for your board can be added to the driver.

Otherwise, you can use the isapnptools package to configure
your board.  Use isapnp to
configure the I/O base and IRQ for the board, and then pass
the same values as
parameters in comedi_config.  A sample isapnp.conf file is included
in the etc/ directory of Comedilib.

Comedilib includes a utility to autocalibrate these boards.  The
boards seem to boot into a state where the all calibration DACs
are at one extreme of their range, thus the default calibration
is terrible.  Calibration at boot is strongly encouraged.

To use the extended digital I/O on some of the boards, enable the
8255 driver when configuring the Comedi source tree.

External triggering is supported for some events.  The channel index
(scan_begin_arg, etc.) maps to PFI0 - PFI9.

Some of the more esoteric triggering possibilities of these boards
are not supported.
*/
/*
	The real guts of the driver is in ni_mio_common.c, which is included
	both here and in ni_pcimio.c

	Interrupt support added by Truxton Fulton <trux@truxton.com>

	References for specifications:

	   340747b.pdf  Register Level Programmer Manual (obsolete)
	   340747c.pdf  Register Level Programmer Manual (new)
	   DAQ-STC reference manual

	Other possibly relevant info:

	   320517c.pdf  User manual (obsolete)
	   320517f.pdf  User manual (new)
	   320889a.pdf  delete
	   320906c.pdf  maximum signal ratings
	   321066a.pdf  about 16x
	   321791a.pdf  discontinuation of at-mio-16e-10 rev. c
	   321808a.pdf  about at-mio-16e-10 rev P
	   321837a.pdf  discontinuation of at-mio-16de-10 rev d
	   321838a.pdf  about at-mio-16de-10 rev N

	ISSUES:

	need to deal with external reference for DAC, and other DAC
	properties in board properties

	deal with at-mio-16de-10 revision D to N changes, etc.

*/

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/isapnp.h>

#include "ni_stc.h"
#include "8255.h"

#undef DEBUG

#define ATMIO 1
#undef PCIMIO

/*
 *  AT specific setup
 */

#define NI_SIZE 0x20

#define MAX_N_CALDACS 32

static const struct ni_board_struct ni_boards[] = {
	{.device_id = 44,
	 .isapnp_id = 0x0000,	/* XXX unknown */
	 .name = "at-mio-16e-1",
	 .n_adchan = 16,
	 .adbits = 12,
	 .ai_fifo_depth = 8192,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_16,
	 .ai_speed = 800,
	 .n_aochan = 2,
	 .aobits = 12,
	 .ao_fifo_depth = 2048,
	 .ao_range_table = &range_ni_E_ao_ext,
	 .ao_unipolar = 1,
	 .ao_speed = 1000,
	 .has_8255 = 0,
	 .num_p0_dio_channels = 8,
	 .caldac = {mb88341},
	 },
	{.device_id = 25,
	 .isapnp_id = 0x1900,
	 .name = "at-mio-16e-2",
	 .n_adchan = 16,
	 .adbits = 12,
	 .ai_fifo_depth = 2048,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_16,
	 .ai_speed = 2000,
	 .n_aochan = 2,
	 .aobits = 12,
	 .ao_fifo_depth = 2048,
	 .ao_range_table = &range_ni_E_ao_ext,
	 .ao_unipolar = 1,
	 .ao_speed = 1000,
	 .has_8255 = 0,
	 .num_p0_dio_channels = 8,
	 .caldac = {mb88341},
	 },
	{.device_id = 36,
	 .isapnp_id = 0x2400,
	 .name = "at-mio-16e-10",
	 .n_adchan = 16,
	 .adbits = 12,
	 .ai_fifo_depth = 512,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_16,
	 .ai_speed = 10000,
	 .n_aochan = 2,
	 .aobits = 12,
	 .ao_fifo_depth = 0,
	 .ao_range_table = &range_ni_E_ao_ext,
	 .ao_unipolar = 1,
	 .ao_speed = 10000,
	 .num_p0_dio_channels = 8,
	 .caldac = {ad8804_debug},
	 .has_8255 = 0,
	 },
	{.device_id = 37,
	 .isapnp_id = 0x2500,
	 .name = "at-mio-16de-10",
	 .n_adchan = 16,
	 .adbits = 12,
	 .ai_fifo_depth = 512,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_16,
	 .ai_speed = 10000,
	 .n_aochan = 2,
	 .aobits = 12,
	 .ao_fifo_depth = 0,
	 .ao_range_table = &range_ni_E_ao_ext,
	 .ao_unipolar = 1,
	 .ao_speed = 10000,
	 .num_p0_dio_channels = 8,
	 .caldac = {ad8804_debug},
	 .has_8255 = 1,
	 },
	{.device_id = 38,
	 .isapnp_id = 0x2600,
	 .name = "at-mio-64e-3",
	 .n_adchan = 64,
	 .adbits = 12,
	 .ai_fifo_depth = 2048,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_16,
	 .ai_speed = 2000,
	 .n_aochan = 2,
	 .aobits = 12,
	 .ao_fifo_depth = 2048,
	 .ao_range_table = &range_ni_E_ao_ext,
	 .ao_unipolar = 1,
	 .ao_speed = 1000,
	 .has_8255 = 0,
	 .num_p0_dio_channels = 8,
	 .caldac = {ad8804_debug},
	 },
	{.device_id = 39,
	 .isapnp_id = 0x2700,
	 .name = "at-mio-16xe-50",
	 .n_adchan = 16,
	 .adbits = 16,
	 .ai_fifo_depth = 512,
	 .alwaysdither = 1,
	 .gainlkup = ai_gain_8,
	 .ai_speed = 50000,
	 .n_aochan = 2,
	 .aobits = 12,
	 .ao_fifo_depth = 0,
	 .ao_range_table = &range_bipolar10,
	 .ao_unipolar = 0,
	 .ao_speed = 50000,
	 .num_p0_dio_channels = 8,
	 .caldac = {dac8800, dac8043},
	 .has_8255 = 0,
	 },
	{.device_id = 50,
	 .isapnp_id = 0x0000,	/* XXX unknown */
	 .name = "at-mio-16xe-10",
	 .n_adchan = 16,
	 .adbits = 16,
	 .ai_fifo_depth = 512,
	 .alwaysdither = 1,
	 .gainlkup = ai_gain_14,
	 .ai_speed = 10000,
	 .n_aochan = 2,
	 .aobits = 16,
	 .ao_fifo_depth = 2048,
	 .ao_range_table = &range_ni_E_ao_ext,
	 .ao_unipolar = 1,
	 .ao_speed = 1000,
	 .num_p0_dio_channels = 8,
	 .caldac = {dac8800, dac8043, ad8522},
	 .has_8255 = 0,
	 },
	{.device_id = 51,
	 .isapnp_id = 0x0000,	/* XXX unknown */
	 .name = "at-ai-16xe-10",
	 .n_adchan = 16,
	 .adbits = 16,
	 .ai_fifo_depth = 512,
	 .alwaysdither = 1,	/* unknown */
	 .gainlkup = ai_gain_14,
	 .ai_speed = 10000,
	 .n_aochan = 0,
	 .aobits = 0,
	 .ao_fifo_depth = 0,
	 .ao_unipolar = 0,
	 .num_p0_dio_channels = 8,
	 .caldac = {dac8800, dac8043, ad8522},
	 .has_8255 = 0,
	 }
};

static const int ni_irqpin[] = {
	-1, -1, -1, 0, 1, 2, -1, 3, -1, -1, 4, 5, 6, -1, -1, 7
};

#define interrupt_pin(a)	(ni_irqpin[(a)])

#define IRQ_POLARITY 0

#define NI_E_IRQ_FLAGS		0

struct ni_private {
	struct pnp_dev *isapnp_dev;
	NI_PRIVATE_COMMON

};

/* How we access registers */

#define ni_writel(a, b)		(outl((a), (b)+dev->iobase))
#define ni_readl(a)		(inl((a)+dev->iobase))
#define ni_writew(a, b)		(outw((a), (b)+dev->iobase))
#define ni_readw(a)		(inw((a)+dev->iobase))
#define ni_writeb(a, b)		(outb((a), (b)+dev->iobase))
#define ni_readb(a)		(inb((a)+dev->iobase))

/* How we access windowed registers */

/* We automatically take advantage of STC registers that can be
 * read/written directly in the I/O space of the board.  The
 * AT-MIO devices map the low 8 STC registers to iobase+addr*2. */

static void ni_atmio_win_out(struct comedi_device *dev, uint16_t data, int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	if ((addr) < 8) {
		ni_writew(data, addr * 2);
	} else {
		ni_writew(addr, Window_Address);
		ni_writew(data, Window_Data);
	}
	spin_unlock_irqrestore(&devpriv->window_lock, flags);
}

static uint16_t ni_atmio_win_in(struct comedi_device *dev, int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;
	uint16_t ret;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	if (addr < 8) {
		ret = ni_readw(addr * 2);
	} else {
		ni_writew(addr, Window_Address);
		ret = ni_readw(Window_Data);
	}
	spin_unlock_irqrestore(&devpriv->window_lock, flags);

	return ret;
}

static struct pnp_device_id device_ids[] = {
	{.id = "NIC1900", .driver_data = 0},
	{.id = "NIC2400", .driver_data = 0},
	{.id = "NIC2500", .driver_data = 0},
	{.id = "NIC2600", .driver_data = 0},
	{.id = "NIC2700", .driver_data = 0},
	{.id = ""}
};

MODULE_DEVICE_TABLE(pnp, device_ids);

#include "ni_mio_common.c"

static int ni_isapnp_find_board(struct pnp_dev **dev)
{
	struct pnp_dev *isapnp_dev = NULL;
	int i;

	for (i = 0; i < n_ni_boards; i++) {
		isapnp_dev = pnp_find_dev(NULL,
					  ISAPNP_VENDOR('N', 'I', 'C'),
					  ISAPNP_FUNCTION(ni_boards[i].
							  isapnp_id), NULL);

		if (isapnp_dev == NULL || isapnp_dev->card == NULL)
			continue;

		if (pnp_device_attach(isapnp_dev) < 0) {
			printk
			 ("ni_atmio: %s found but already active, skipping.\n",
			  ni_boards[i].name);
			continue;
		}
		if (pnp_activate_dev(isapnp_dev) < 0) {
			pnp_device_detach(isapnp_dev);
			return -EAGAIN;
		}
		if (!pnp_port_valid(isapnp_dev, 0)
		    || !pnp_irq_valid(isapnp_dev, 0)) {
			pnp_device_detach(isapnp_dev);
			printk("ni_atmio: pnp invalid port or irq, aborting\n");
			return -ENOMEM;
		}
		break;
	}
	if (i == n_ni_boards)
		return -ENODEV;
	*dev = isapnp_dev;
	return 0;
}

static int ni_getboardtype(struct comedi_device *dev)
{
	int device_id = ni_read_eeprom(dev, 511);
	int i;

	for (i = 0; i < n_ni_boards; i++) {
		if (ni_boards[i].device_id == device_id)
			return i;

	}
	if (device_id == 255)
		printk(" can't find board\n");
	 else if (device_id == 0)
		printk(" EEPROM read error (?) or device not found\n");
	 else
		printk(" unknown device ID %d -- contact author\n", device_id);

	return -1;
}

static int ni_atmio_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	struct ni_private *devpriv;
	struct pnp_dev *isapnp_dev;
	int ret;
	unsigned long iobase;
	int board;
	unsigned int irq;

	ret = ni_alloc_private(dev);
	if (ret)
		return ret;
	devpriv = dev->private;

	devpriv->stc_writew = &ni_atmio_win_out;
	devpriv->stc_readw = &ni_atmio_win_in;
	devpriv->stc_writel = &win_out2;
	devpriv->stc_readl = &win_in2;

	iobase = it->options[0];
	irq = it->options[1];
	isapnp_dev = NULL;
	if (iobase == 0) {
		ret = ni_isapnp_find_board(&isapnp_dev);
		if (ret < 0)
			return ret;

		iobase = pnp_port_start(isapnp_dev, 0);
		irq = pnp_irq(isapnp_dev, 0);
		devpriv->isapnp_dev = isapnp_dev;
	}

	/* reserve our I/O region */

	printk("comedi%d: ni_atmio: 0x%04lx", dev->minor, iobase);
	if (!request_region(iobase, NI_SIZE, "ni_atmio")) {
		printk(" I/O port conflict\n");
		return -EIO;
	}

	dev->iobase = iobase;

#ifdef DEBUG
	/* board existence sanity check */
	{
		int i;

		printk(" board fingerprint:");
		for (i = 0; i < 16; i += 2) {
			printk(" %04x %02x", inw(dev->iobase + i),
			       inb(dev->iobase + i + 1));
		}
	}
#endif

	/* get board type */

	board = ni_getboardtype(dev);
	if (board < 0)
		return -EIO;

	dev->board_ptr = ni_boards + board;

	printk(" %s", boardtype.name);
	dev->board_name = boardtype.name;

	/* irq stuff */

	if (irq != 0) {
		if (irq > 15 || ni_irqpin[irq] == -1) {
			printk(" invalid irq %u\n", irq);
			return -EINVAL;
		}
		printk(" ( irq = %u )", irq);
		ret = request_irq(irq, ni_E_interrupt, NI_E_IRQ_FLAGS,
				  "ni_atmio", dev);

		if (ret < 0) {
			printk(" irq not available\n");
			return -EINVAL;
		}
		dev->irq = irq;
	}

	/* generic E series stuff in ni_mio_common.c */

	ret = ni_E_init(dev);
	if (ret < 0)
		return ret;


	return 0;
}

static void ni_atmio_detach(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;

	mio_common_detach(dev);
	if (dev->iobase)
		release_region(dev->iobase, NI_SIZE);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv->isapnp_dev)
		pnp_device_detach(devpriv->isapnp_dev);
}

static struct comedi_driver ni_atmio_driver = {
	.driver_name	= "ni_atmio",
	.module		= THIS_MODULE,
	.attach		= ni_atmio_attach,
	.detach		= ni_atmio_detach,
};
module_comedi_driver(ni_atmio_driver);
