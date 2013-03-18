/*
    comedi/drivers/amplc_dio.h

    Header for amplc_dio200.c, amplc_dio200_common.c and
    amplc_dio200_pci.c.

    Copyright (C) 2005-2013 MEV Ltd. <http://www.mev.co.uk/>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998,2000 David A. Schleef <ds@schleef.org>

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

#ifndef AMPLC_DIO200_H_INCLUDED
#define AMPLC_DIO200_H_INCLUDED

/* 200 series register area sizes */
#define DIO200_IO_SIZE		0x20
#define DIO200_PCIE_IO_SIZE	0x4000

/*
 * Register region.
 */
enum dio200_regtype { no_regtype = 0, io_regtype, mmio_regtype };
struct dio200_region {
	union {
		unsigned long iobase;		/* I/O base address */
		unsigned char __iomem *membase;	/* mapped MMIO base address */
	} u;
	enum dio200_regtype regtype;
};

/*
 * Subdevice types.
 */
enum dio200_sdtype { sd_none, sd_intr, sd_8255, sd_8254, sd_timer };

#define DIO200_MAX_SUBDEVS	8
#define DIO200_MAX_ISNS		6

/*
 * Board descriptions.
 */

struct dio200_layout {
	unsigned short n_subdevs;	/* number of subdevices */
	unsigned char sdtype[DIO200_MAX_SUBDEVS];	/* enum dio200_sdtype */
	unsigned char sdinfo[DIO200_MAX_SUBDEVS];	/* depends on sdtype */
	bool has_int_sce:1;		/* has interrupt enable/status reg */
	bool has_clk_gat_sce:1;		/* has clock/gate selection registers */
	bool has_enhancements:1;	/* has enhanced features */
};

enum dio200_bustype { isa_bustype, pci_bustype };

struct dio200_board {
	const char *name;
	struct dio200_layout layout;
	enum dio200_bustype bustype;
	unsigned char mainbar;
	unsigned char mainshift;
	unsigned int mainsize;
};

/*
 * Comedi device private data.
 */
struct dio200_private {
	struct dio200_region io;	/* Register region */
	int intr_sd;
};

int amplc_dio200_common_attach(struct comedi_device *dev, unsigned int irq,
			       unsigned long req_irq_flags);

void amplc_dio200_common_detach(struct comedi_device *dev);

/* Used by initialization of PCIe boards. */
void amplc_dio200_set_enhance(struct comedi_device *dev, unsigned char val);

#endif
