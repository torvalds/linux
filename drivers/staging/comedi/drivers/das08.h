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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef _DAS08_H
#define _DAS08_H

enum das08_bustype { isa, pci, pcmcia, pc104 };
/* different ways ai data is encoded in first two registers */
enum das08_ai_encoding { das08_encode12, das08_encode16, das08_pcm_encode12 };
enum das08_lrange { das08_pg_none, das08_bipolar5, das08_pgh, das08_pgl,
		das08_pgm };

struct das08_board_struct {
	const char *name;
	unsigned int id;	/*  id for pci/pcmcia boards */
	enum das08_bustype bustype;
	void *ai;
	unsigned int ai_nbits;
	enum das08_lrange ai_pg;
	enum das08_ai_encoding ai_encoding;
	void *ao;
	unsigned int ao_nbits;
	void *di;
	void *do_;
	unsigned int do_nchan;
	unsigned int i8255_offset;
	unsigned int i8254_offset;
	unsigned int iosize;	/*  number of ioports used */
};

struct i8254_struct {
	int channels;		/*  available channels. Some could be used internally. */
	int logic2phys[3];	/*  to know which physical channel is. */
	int mode[3];		/*  the index is the real counter. */
	unsigned int iobase;
};

#define I8254_CNT0 0
#define I8254_CNT1 1
#define I8254_CNT2 2
#define I8254_CTRL 3

struct das08_private_struct {
	unsigned int do_mux_bits;	/*  bits for do/mux register on boards without seperate do register */
	unsigned int do_bits;	/*  bits for do register on boards with register dedicated to digital out only */
	const unsigned int *pg_gainlist;
	struct pci_dev *pdev;	/*  struct for pci-das08 */
	unsigned int pci_iobase;	/*  additional base address for pci-das08 */
	struct i8254_struct i8254;
};

#define NUM_DAS08_CS_BOARDS 2
extern struct das08_board_struct das08_cs_boards[NUM_DAS08_CS_BOARDS];

int das08_common_attach(struct comedi_device * dev, unsigned long iobase);
int das08_common_detach(struct comedi_device * dev);

#endif /* _DAS08_H */
