// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/drivers/amplc_pc236.c
 * Driver for Amplicon PC36AT DIO boards.
 *
 * Copyright (C) 2002 MEV Ltd. <http://www.mev.co.uk/>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */
/*
 * Driver: amplc_pc236
 * Description: Amplicon PC36AT
 * Author: Ian Abbott <abbotti@mev.co.uk>
 * Devices: [Amplicon] PC36AT (pc36at)
 * Updated: Fri, 25 Jul 2014 15:32:40 +0000
 * Status: works
 *
 * Configuration options - PC36AT:
 *   [0] - I/O port base address
 *   [1] - IRQ (optional)
 *
 * The PC36AT board has a single 8255 appearing as subdevice 0.
 *
 * Subdevice 1 pretends to be a digital input device, but it always returns
 * 0 when read. However, if you run a command with scan_begin_src=TRIG_EXT,
 * a rising edge on port C bit 3 acts as an external trigger, which can be
 * used to wake up tasks.  This is like the comedi_parport device, but the
 * only way to physically disable the interrupt on the PC36AT is to remove
 * the IRQ jumper.  If no interrupt is connected, then subdevice 1 is
 * unused.
 */

#include <linux/module.h>

#include "../comedidev.h"

#include "amplc_pc236.h"

static int pc236_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct pc236_private *devpriv;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x4);
	if (ret)
		return ret;

	return amplc_pc236_common_attach(dev, dev->iobase, it->options[1], 0);
}

static const struct pc236_board pc236_boards[] = {
	{
		.name = "pc36at",
	},
};

static struct comedi_driver amplc_pc236_driver = {
	.driver_name = "amplc_pc236",
	.module = THIS_MODULE,
	.attach = pc236_attach,
	.detach = comedi_legacy_detach,
	.board_name = &pc236_boards[0].name,
	.offset = sizeof(struct pc236_board),
	.num_names = ARRAY_SIZE(pc236_boards),
};

module_comedi_driver(amplc_pc236_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Amplicon PC36AT DIO boards");
MODULE_LICENSE("GPL");
