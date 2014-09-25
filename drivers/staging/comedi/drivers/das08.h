/*
    das08.h

    Header for das08.c and das08_cs.c

    Copyright (C) 2003 Frank Mori Hess <fmhess@users.sourceforge.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#ifndef _DAS08_H
#define _DAS08_H

/* different ways ai data is encoded in first two registers */
enum das08_ai_encoding { das08_encode12, das08_encode16, das08_pcm_encode12 };
enum das08_lrange { das08_pg_none, das08_bipolar5, das08_pgh, das08_pgl,
	das08_pgm
};

struct das08_board_struct {
	const char *name;
	bool is_jr;		/* true for 'JR' boards */
	unsigned int ai_nbits;
	enum das08_lrange ai_pg;
	enum das08_ai_encoding ai_encoding;
	unsigned int ao_nbits;
	unsigned int di_nchan;
	unsigned int do_nchan;
	unsigned int i8255_offset;
	unsigned int i8254_offset;
	unsigned int iosize;	/*  number of ioports used */
};

struct das08_private_struct {
	unsigned int do_mux_bits;	/*  bits for do/mux register on boards
					 *  without separate do register
					 */
	const unsigned int *pg_gainlist;
	unsigned int ao_readback[2];	/* assume 2 AO channels */
};

int das08_common_attach(struct comedi_device *dev, unsigned long iobase);

#endif /* _DAS08_H */
