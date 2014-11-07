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
*/

#ifndef AMPLC_DIO200_H_INCLUDED
#define AMPLC_DIO200_H_INCLUDED

/*
 * Subdevice types.
 */
enum dio200_sdtype { sd_none, sd_intr, sd_8255, sd_8254, sd_timer };

#define DIO200_MAX_SUBDEVS	8
#define DIO200_MAX_ISNS		6

struct dio200_board {
	const char *name;
	unsigned char mainbar;
	unsigned short n_subdevs;	/* number of subdevices */
	unsigned char sdtype[DIO200_MAX_SUBDEVS];	/* enum dio200_sdtype */
	unsigned char sdinfo[DIO200_MAX_SUBDEVS];	/* depends on sdtype */
	bool has_int_sce:1;		/* has interrupt enable/status reg */
	bool has_clk_gat_sce:1;		/* has clock/gate selection registers */
	bool is_pcie:1;			/* has enhanced features */
};

int amplc_dio200_common_attach(struct comedi_device *dev, unsigned int irq,
			       unsigned long req_irq_flags);

/* Used by initialization of PCIe boards. */
void amplc_dio200_set_enhance(struct comedi_device *dev, unsigned char val);

#endif
