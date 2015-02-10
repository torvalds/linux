/*
 *  das08_isa.c
 *  comedi driver for DAS08 ISA/PC-104 boards
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 *  Copyright (C) 2001,2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 *  Copyright (C) 2004 Salvador E. Tropea <set@users.sf.net> <set@ieee.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * Driver: das08_isa
 * Description: DAS-08 ISA/PC-104 compatible boards
 * Devices: (Keithley Metrabyte) DAS08 [isa-das08],
 *	    (ComputerBoards) DAS08 [isa-das08]
 *	    (ComputerBoards) DAS08-PGM [das08-pgm]
 *	    (ComputerBoards) DAS08-PGH [das08-pgh]
 *	    (ComputerBoards) DAS08-PGL [das08-pgl]
 *	    (ComputerBoards) DAS08-AOH [das08-aoh]
 *	    (ComputerBoards) DAS08-AOL [das08-aol]
 *	    (ComputerBoards) DAS08-AOM [das08-aom]
 *	    (ComputerBoards) DAS08/JR-AO [das08/jr-ao]
 *	    (ComputerBoards) DAS08/JR-16-AO [das08jr-16-ao]
 *	    (ComputerBoards) PC104-DAS08 [pc104-das08]
 *	    (ComputerBoards) DAS08/JR/16 [das08jr/16]
 * Author: Warren Jasper, ds, Frank Hess
 * Updated: Fri, 31 Aug 2012 19:19:06 +0100
 * Status: works
 *
 * This is the ISA/PC-104-specific support split off from the das08 driver.
 *
 * Configuration Options:
 *	[0] - base io address
 */

#include <linux/module.h>
#include "../comedidev.h"

#include "das08.h"

static const struct das08_board_struct das08_isa_boards[] = {
	{
		/* cio-das08.pdf */
		.name		= "isa-das08",
		.ai_nbits	= 12,
		.ai_pg		= das08_pg_none,
		.ai_encoding	= das08_encode12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8255_offset	= 8,
		.i8254_offset	= 4,
		.iosize		= 16,		/* unchecked */
	}, {
		/* cio-das08pgx.pdf */
		.name		= "das08-pgm",
		.ai_nbits	= 12,
		.ai_pg		= das08_pgm,
		.ai_encoding	= das08_encode12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8255_offset	= 0,
		.i8254_offset	= 0x04,
		.iosize		= 16,		/* unchecked */
	}, {
		/* cio-das08pgx.pdf */
		.name		= "das08-pgh",
		.ai_nbits	= 12,
		.ai_pg		= das08_pgh,
		.ai_encoding	= das08_encode12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8254_offset	= 0x04,
		.iosize		= 16,		/* unchecked */
	}, {
		/* cio-das08pgx.pdf */
		.name		= "das08-pgl",
		.ai_nbits	= 12,
		.ai_pg		= das08_pgl,
		.ai_encoding	= das08_encode12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8254_offset	= 0x04,
		.iosize		= 16,		/* unchecked */
	}, {
		/* cio-das08_aox.pdf */
		.name		= "das08-aoh",
		.ai_nbits	= 12,
		.ai_pg		= das08_pgh,
		.ai_encoding	= das08_encode12,
		.ao_nbits	= 12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8255_offset	= 0x0c,
		.i8254_offset	= 0x04,
		.iosize		= 16,		/* unchecked */
	}, {
		/* cio-das08_aox.pdf */
		.name		= "das08-aol",
		.ai_nbits	= 12,
		.ai_pg		= das08_pgl,
		.ai_encoding	= das08_encode12,
		.ao_nbits	= 12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8255_offset	= 0x0c,
		.i8254_offset	= 0x04,
		.iosize		= 16,		/* unchecked */
	}, {
		/* cio-das08_aox.pdf */
		.name		= "das08-aom",
		.ai_nbits	= 12,
		.ai_pg		= das08_pgm,
		.ai_encoding	= das08_encode12,
		.ao_nbits	= 12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8255_offset	= 0x0c,
		.i8254_offset	= 0x04,
		.iosize		= 16,		/* unchecked */
	}, {
		/* cio-das08-jr-ao.pdf */
		.name		= "das08/jr-ao",
		.is_jr		= true,
		.ai_nbits	= 12,
		.ai_pg		= das08_pg_none,
		.ai_encoding	= das08_encode12,
		.ao_nbits	= 12,
		.di_nchan	= 8,
		.do_nchan	= 8,
		.iosize		= 16,		/* unchecked */
	}, {
		/* cio-das08jr-16-ao.pdf */
		.name		= "das08jr-16-ao",
		.is_jr		= true,
		.ai_nbits	= 16,
		.ai_pg		= das08_pg_none,
		.ai_encoding	= das08_encode16,
		.ao_nbits	= 16,
		.di_nchan	= 8,
		.do_nchan	= 8,
		.i8254_offset	= 0x04,
		.iosize		= 16,		/* unchecked */
	}, {
		.name		= "pc104-das08",
		.ai_nbits	= 12,
		.ai_pg		= das08_pg_none,
		.ai_encoding	= das08_encode12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8254_offset	= 4,
		.iosize		= 16,		/* unchecked */
	}, {
		.name		= "das08jr/16",
		.is_jr		= true,
		.ai_nbits	= 16,
		.ai_pg		= das08_pg_none,
		.ai_encoding	= das08_encode16,
		.di_nchan	= 8,
		.do_nchan	= 8,
		.iosize		= 16,		/* unchecked */
	},
};

static int das08_isa_attach(struct comedi_device *dev,
			    struct comedi_devconfig *it)
{
	const struct das08_board_struct *thisboard = dev->board_ptr;
	struct das08_private_struct *devpriv;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], thisboard->iosize);
	if (ret)
		return ret;

	return das08_common_attach(dev, dev->iobase);
}

static struct comedi_driver das08_isa_driver = {
	.driver_name	= "isa-das08",
	.module		= THIS_MODULE,
	.attach		= das08_isa_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &das08_isa_boards[0].name,
	.num_names	= ARRAY_SIZE(das08_isa_boards),
	.offset		= sizeof(das08_isa_boards[0]),
};
module_comedi_driver(das08_isa_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
