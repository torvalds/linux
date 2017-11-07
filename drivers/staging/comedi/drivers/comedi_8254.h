// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi_8254.h
 * Generic 8254 timer/counter support
 * Copyright (C) 2014 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
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

#ifndef _COMEDI_8254_H
#define _COMEDI_8254_H

#include <linux/types.h>

struct comedi_device;
struct comedi_insn;
struct comedi_subdevice;

/*
 * Common oscillator base values in nanoseconds
 */
#define I8254_OSC_BASE_10MHZ	100
#define I8254_OSC_BASE_5MHZ	200
#define I8254_OSC_BASE_4MHZ	250
#define I8254_OSC_BASE_2MHZ	500
#define I8254_OSC_BASE_1MHZ	1000
#define I8254_OSC_BASE_100KHZ	10000
#define I8254_OSC_BASE_10KHZ	100000
#define I8254_OSC_BASE_1KHZ	1000000

/*
 * I/O access size used to read/write registers
 */
#define I8254_IO8		1
#define I8254_IO16		2
#define I8254_IO32		4

/*
 * Register map for generic 8254 timer (I8254_IO8 with 0 regshift)
 */
#define I8254_COUNTER0_REG		0x00
#define I8254_COUNTER1_REG		0x01
#define I8254_COUNTER2_REG		0x02
#define I8254_CTRL_REG			0x03
#define I8254_CTRL_SEL_CTR(x)		((x) << 6)
#define I8254_CTRL_READBACK(x)		(I8254_CTRL_SEL_CTR(3) | BIT(x))
#define I8254_CTRL_READBACK_COUNT	I8254_CTRL_READBACK(4)
#define I8254_CTRL_READBACK_STATUS	I8254_CTRL_READBACK(5)
#define I8254_CTRL_READBACK_SEL_CTR(x)	(2 << (x))
#define I8254_CTRL_RW(x)		(((x) & 0x3) << 4)
#define I8254_CTRL_LATCH		I8254_CTRL_RW(0)
#define I8254_CTRL_LSB_ONLY		I8254_CTRL_RW(1)
#define I8254_CTRL_MSB_ONLY		I8254_CTRL_RW(2)
#define I8254_CTRL_LSB_MSB		I8254_CTRL_RW(3)

/* counter maps zero to 0x10000 */
#define I8254_MAX_COUNT			0x10000

/**
 * struct comedi_8254 - private data used by this module
 * @iobase:		PIO base address of the registers (in/out)
 * @mmio:		MMIO base address of the registers (read/write)
 * @iosize:		I/O size used to access the registers (b/w/l)
 * @regshift:		register gap shift
 * @osc_base:		cascaded oscillator speed in ns
 * @divisor:		divisor for single counter
 * @divisor1:		divisor loaded into first cascaded counter
 * @divisor2:		divisor loaded into second cascaded counter
 * #next_div:		next divisor for single counter
 * @next_div1:		next divisor to use for first cascaded counter
 * @next_div2:		next divisor to use for second cascaded counter
 * @clock_src;		current clock source for each counter (driver specific)
 * @gate_src;		current gate source  for each counter (driver specific)
 * @busy:		flags used to indicate that a counter is "busy"
 * @insn_config:	driver specific (*insn_config) callback
 */
struct comedi_8254 {
	unsigned long iobase;
	void __iomem *mmio;
	unsigned int iosize;
	unsigned int regshift;
	unsigned int osc_base;
	unsigned int divisor;
	unsigned int divisor1;
	unsigned int divisor2;
	unsigned int next_div;
	unsigned int next_div1;
	unsigned int next_div2;
	unsigned int clock_src[3];
	unsigned int gate_src[3];
	bool busy[3];

	int (*insn_config)(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn, unsigned int *data);
};

unsigned int comedi_8254_status(struct comedi_8254 *i8254,
				unsigned int counter);
unsigned int comedi_8254_read(struct comedi_8254 *i8254, unsigned int counter);
void comedi_8254_write(struct comedi_8254 *i8254,
		       unsigned int counter, unsigned int val);

int comedi_8254_set_mode(struct comedi_8254 *i8254,
			 unsigned int counter, unsigned int mode);
int comedi_8254_load(struct comedi_8254 *i8254,
		     unsigned int counter, unsigned int val, unsigned int mode);

void comedi_8254_pacer_enable(struct comedi_8254 *i8254,
			      unsigned int counter1, unsigned int counter2,
			      bool enable);
void comedi_8254_update_divisors(struct comedi_8254 *i8254);
void comedi_8254_cascade_ns_to_timer(struct comedi_8254 *i8254,
				     unsigned int *nanosec, unsigned int flags);
void comedi_8254_ns_to_timer(struct comedi_8254 *i8254,
			     unsigned int *nanosec, unsigned int flags);

void comedi_8254_set_busy(struct comedi_8254 *i8254,
			  unsigned int counter, bool busy);

void comedi_8254_subdevice_init(struct comedi_subdevice *s,
				struct comedi_8254 *i8254);

struct comedi_8254 *comedi_8254_init(unsigned long iobase,
				     unsigned int osc_base,
				     unsigned int iosize,
				     unsigned int regshift);
struct comedi_8254 *comedi_8254_mm_init(void __iomem *mmio,
					unsigned int osc_base,
					unsigned int iosize,
					unsigned int regshift);

#endif	/* _COMEDI_8254_H */
