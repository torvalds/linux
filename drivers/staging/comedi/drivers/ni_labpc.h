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
*/

#ifndef _NI_LABPC_H
#define _NI_LABPC_H

#define EEPROM_SIZE	256	/*  256 byte eeprom */
#define NUM_AO_CHAN	2	/*  boards have two analog output channels */

enum transfer_type { fifo_not_empty_transfer, fifo_half_full_transfer,
	isa_dma_transfer
};

struct labpc_boardinfo {
	const char *name;
	int ai_speed;			/* maximum input speed in ns */
	unsigned ai_scan_up:1;		/* can auto scan up in ai channels */
	unsigned has_ao:1;		/* has analog outputs */
	unsigned is_labpc1200:1;	/* has extra regs compared to pc+ */
	unsigned has_mmio:1;		/* uses memory mapped io */
};

struct labpc_private {
	/*  number of data points left to be taken */
	unsigned long long count;
	/*  software copy of analog output values */
	unsigned int ao_value[NUM_AO_CHAN];
	/*  software copys of bits written to command registers */
	unsigned int cmd1;
	unsigned int cmd2;
	unsigned int cmd3;
	unsigned int cmd4;
	unsigned int cmd5;
	unsigned int cmd6;
	/*  store last read of board status registers */
	unsigned int stat1;
	unsigned int stat2;
	/*
	 * value to load into board's counter a0 (conversion pacing) for timed
	 * conversions
	 */
	unsigned int divisor_a0;
	/*
	 * value to load into board's counter b0 (master) for timed conversions
	 */
	unsigned int divisor_b0;
	/*
	 * value to load into board's counter b1 (scan pacing) for timed
	 * conversions
	 */
	unsigned int divisor_b1;
	unsigned int dma_chan;	/*  dma channel to use */
	u16 *dma_buffer;	/*  buffer ai will dma into */
	phys_addr_t dma_addr;
	/* transfer size in bytes for current transfer */
	unsigned int dma_transfer_size;
	/* we are using dma/fifo-half-full/etc. */
	enum transfer_type current_transfer;
	/* stores contents of board's eeprom */
	unsigned int eeprom_data[EEPROM_SIZE];
	/* stores settings of calibration dacs */
	unsigned int caldac[16];
	/*
	 * function pointers so we can use inb/outb or readb/writeb as
	 * appropriate
	 */
	unsigned int (*read_byte) (unsigned long address);
	void (*write_byte) (unsigned int byte, unsigned long address);
};

int labpc_common_attach(struct comedi_device *dev,
			unsigned int irq, unsigned long isr_flags);

#endif /* _NI_LABPC_H */
