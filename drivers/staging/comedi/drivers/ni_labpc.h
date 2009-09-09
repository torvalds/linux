/*
    ni_labpc.h

    Header for ni_labpc.c and ni_labpc_cs.c

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

#ifndef _NI_LABPC_H
#define _NI_LABPC_H

#define EEPROM_SIZE	256	/*  256 byte eeprom */
#define NUM_AO_CHAN	2	/*  boards have two analog output channels */

enum labpc_bustype { isa_bustype, pci_bustype, pcmcia_bustype };
enum labpc_register_layout { labpc_plus_layout, labpc_1200_layout };
enum transfer_type { fifo_not_empty_transfer, fifo_half_full_transfer,
		isa_dma_transfer };

struct labpc_board_struct {
	const char *name;
	int device_id;		/*  device id for pci and pcmcia boards */
	int ai_speed;		/*  maximum input speed in nanoseconds */
	enum labpc_bustype bustype;	/*  ISA/PCI/etc. */
	enum labpc_register_layout register_layout;	/*  1200 has extra registers compared to pc+ */
	int has_ao;		/*  has analog output true/false */
	const struct comedi_lrange *ai_range_table;
	const int *ai_range_code;
	const int *ai_range_is_unipolar;
	unsigned ai_scan_up:1;	/*  board can auto scan up in ai channels, not just down */
	unsigned memory_mapped_io:1;	/* uses memory mapped io instead of ioports */
};

struct labpc_private {
	struct mite_struct *mite;	/*  for mite chip on pci-1200 */
	volatile unsigned long long count;	/* number of data points left to be taken */
	unsigned int ao_value[NUM_AO_CHAN];	/*  software copy of analog output values */
	/*  software copys of bits written to command registers */
	volatile unsigned int command1_bits;
	volatile unsigned int command2_bits;
	volatile unsigned int command3_bits;
	volatile unsigned int command4_bits;
	volatile unsigned int command5_bits;
	volatile unsigned int command6_bits;
	/*  store last read of board status registers */
	volatile unsigned int status1_bits;
	volatile unsigned int status2_bits;
	unsigned int divisor_a0;	/* value to load into board's counter a0 (conversion pacing) for timed conversions */
	unsigned int divisor_b0;	/* value to load into board's counter b0 (master) for timed conversions */
	unsigned int divisor_b1;	/* value to load into board's counter b1 (scan pacing) for timed conversions */
	unsigned int dma_chan;	/*  dma channel to use */
	u16 *dma_buffer;	/*  buffer ai will dma into */
	unsigned int dma_transfer_size;	/*  transfer size in bytes for current transfer */
	enum transfer_type current_transfer;	/*  we are using dma/fifo-half-full/etc. */
	unsigned int eeprom_data[EEPROM_SIZE];	/*  stores contents of board's eeprom */
	unsigned int caldac[16];	/*  stores settings of calibration dacs */
	/*  function pointers so we can use inb/outb or readb/writeb as appropriate */
	unsigned int (*read_byte) (unsigned long address);
	void (*write_byte) (unsigned int byte, unsigned long address);
};

int labpc_common_attach(struct comedi_device *dev, unsigned long iobase,
	unsigned int irq, unsigned int dma);
int labpc_common_detach(struct comedi_device *dev);

extern const int labpc_1200_is_unipolar[];
extern const int labpc_1200_ai_gain_bits[];
extern const struct comedi_lrange range_labpc_1200_ai;

#endif /* _NI_LABPC_H */
