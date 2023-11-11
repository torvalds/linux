/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * das08.h
 *
 * Header for common DAS08 support (used by ISA/PCI/PCMCIA drivers)
 *
 * Copyright (C) 2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 */

#ifndef _DAS08_H
#define _DAS08_H

#include <linux/types.h>

struct comedi_device;

/* different ways ai data is encoded in first two registers */
enum das08_ai_encoding { das08_encode12, das08_encode16, das08_pcm_encode12 };
/* types of ai range table used by different boards */
enum das08_lrange {
	das08_pg_none, das08_bipolar5, das08_pgh, das08_pgl, das08_pgm
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
	unsigned int iosize;	/* number of ioports used */
};

struct das08_private_struct {
	/* bits for do/mux register on boards without separate do register */
	unsigned int do_mux_bits;
	const unsigned int *pg_gainlist;
};

int das08_common_attach(struct comedi_device *dev, unsigned long iobase);

#endif /* _DAS08_H */
