/*
 * comedi/drivers/ni_labpc.c
 * Driver for National Instruments Lab-PC series boards and compatibles
 * Copyright (C) 2001-2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: ni_labpc
 * Description: National Instruments Lab-PC (& compatibles)
 * Devices: [National Instruments] Lab-PC-1200 (lab-pc-1200),
 *   Lab-PC-1200AI (lab-pc-1200ai), Lab-PC+ (lab-pc+)
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Status: works
 *
 * Configuration options - ISA boards:
 *   [0] - I/O port base address
 *   [1] - IRQ (optional, required for timed or externally triggered
 *		conversions)
 *   [2] - DMA channel (optional)
 *
 * Tested with lab-pc-1200.  For the older Lab-PC+, not all input
 * ranges and analog references will work, the available ranges/arefs
 * will depend on how you have configured the jumpers on your board
 * (see your owner's manual).
 *
 * Kernel-level ISA plug-and-play support for the lab-pc-1200 boards
 * has not yet been added to the driver, mainly due to the fact that
 * I don't know the device id numbers. If you have one of these boards,
 * please file a bug report at http://comedi.org/ so I can get the
 * necessary information from you.
 *
 * The 1200 series boards have onboard calibration dacs for correcting
 * analog input/output offsets and gains. The proper settings for these
 * caldacs are stored on the board's eeprom. To read the caldac values
 * from the eeprom and store them into a file that can be then be used
 * by comedilib, use the comedi_calibrate program.
 *
 * The Lab-pc+ has quirky chanlist requirements when scanning multiple
 * channels. Multiple channel scan sequence must start at highest channel,
 * then decrement down to channel 0. The rest of the cards can scan down
 * like lab-pc+ or scan up from channel zero. Chanlists consisting of all
 * one channel are also legal, and allow you to pace conversions in bursts.
 *
 * NI manuals:
 * 341309a (labpc-1200 register manual)
 * 320502b (lab-pc+)
 */

#include <linux/module.h>

#include "../comedidev.h"

#include "ni_labpc.h"
#include "ni_labpc_isadma.h"

static const struct labpc_boardinfo labpc_boards[] = {
	{
		.name			= "lab-pc-1200",
		.ai_speed		= 10000,
		.ai_scan_up		= 1,
		.has_ao			= 1,
		.is_labpc1200		= 1,
	}, {
		.name			= "lab-pc-1200ai",
		.ai_speed		= 10000,
		.ai_scan_up		= 1,
		.is_labpc1200		= 1,
	}, {
		.name			= "lab-pc+",
		.ai_speed		= 12000,
		.has_ao			= 1,
	},
};

static int labpc_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	unsigned int irq = it->options[1];
	unsigned int dma_chan = it->options[2];
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x20);
	if (ret)
		return ret;

	ret = labpc_common_attach(dev, irq, 0);
	if (ret)
		return ret;

	if (dev->irq)
		labpc_init_dma_chan(dev, dma_chan);

	return 0;
}

static void labpc_detach(struct comedi_device *dev)
{
	labpc_free_dma_chan(dev);
	labpc_common_detach(dev);
	comedi_legacy_detach(dev);
}

static struct comedi_driver labpc_driver = {
	.driver_name	= "ni_labpc",
	.module		= THIS_MODULE,
	.attach		= labpc_attach,
	.detach		= labpc_detach,
	.num_names	= ARRAY_SIZE(labpc_boards),
	.board_name	= &labpc_boards[0].name,
	.offset		= sizeof(struct labpc_boardinfo),
};
module_comedi_driver(labpc_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for NI Lab-PC ISA boards");
MODULE_LICENSE("GPL");
