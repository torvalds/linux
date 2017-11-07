// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi_8254.c
 * Generic 8254 timer/counter support
 * Copyright (C) 2014 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on 8253.h and various subdevice implementations in comedi drivers.
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

/*
 * Module: comedi_8254
 * Description: Generic 8254 timer/counter support
 * Author: H Hartley Sweeten <hsweeten@visionengravers.com>
 * Updated: Thu Jan 8 16:45:45 MST 2015
 * Status: works
 *
 * This module is not used directly by end-users. Rather, it is used by other
 * drivers to provide support for an 8254 Programmable Interval Timer. These
 * counters are typically used to generate the pacer clock used for data
 * acquisition. Some drivers also expose the counters for general purpose use.
 *
 * This module provides the following basic functions:
 *
 * comedi_8254_init() / comedi_8254_mm_init()
 *	Initializes this module to access the 8254 registers. The _mm version
 *	sets up the module for MMIO register access the other for PIO access.
 *	The pointer returned from these functions is normally stored in the
 *	comedi_device dev->pacer and will be freed by the comedi core during
 *	the driver (*detach). If a driver has multiple 8254 devices, they need
 *	to be stored in the drivers private data and freed when the driver is
 *	detached.
 *
 *	NOTE: The counters are reset by setting them to I8254_MODE0 as part of
 *	this initialization.
 *
 * comedi_8254_set_mode()
 *	Sets a counters operation mode:
 *		I8254_MODE0	Interrupt on terminal count
 *		I8254_MODE1	Hardware retriggerable one-shot
 *		I8254_MODE2	Rate generator
 *		I8254_MODE3	Square wave mode
 *		I8254_MODE4	Software triggered strobe
 *		I8254_MODE5	Hardware triggered strobe (retriggerable)
 *
 *	In addition I8254_BCD and I8254_BINARY specify the counting mode:
 *		I8254_BCD	BCD counting
 *		I8254_BINARY	Binary counting
 *
 * comedi_8254_write()
 *	Writes an initial value to a counter.
 *
 *	The largest possible initial count is 0; this is equivalent to 2^16
 *	for binary counting and 10^4 for BCD counting.
 *
 *	NOTE: The counter does not stop when it reaches zero. In Mode 0, 1, 4,
 *	and 5 the counter "wraps around" to the highest count, either 0xffff
 *	for binary counting or 9999 for BCD counting, and continues counting.
 *	Modes 2 and 3 are periodic; the counter reloads itself with the initial
 *	count and continues counting from there.
 *
 * comedi_8254_read()
 *	Reads the current value from a counter.
 *
 * comedi_8254_status()
 *	Reads the status of a counter.
 *
 * comedi_8254_load()
 *	Sets a counters operation mode and writes the initial value.
 *
 * Typically the pacer clock is created by cascading two of the 16-bit counters
 * to create a 32-bit rate generator (I8254_MODE2). These functions are
 * provided to handle the cascaded counters:
 *
 * comedi_8254_ns_to_timer()
 *	Calculates the divisor value needed for a single counter to generate
 *	ns timing.
 *
 * comedi_8254_cascade_ns_to_timer()
 *	Calculates the two divisor values needed to the generate the pacer
 *	clock (in ns).
 *
 * comedi_8254_update_divisors()
 *	Transfers the intermediate divisor values to the current divisors.
 *
 * comedi_8254_pacer_enable()
 *	Programs the mode of the cascaded counters and writes the current
 *	divisor values.
 *
 * To expose the counters as a subdevice for general purpose use the following
 * functions a provided:
 *
 * comedi_8254_subdevice_init()
 *	Initializes a comedi_subdevice to use the 8254 timer.
 *
 * comedi_8254_set_busy()
 *	Internally flags a counter as "busy". This is done to protect the
 *	counters that are used for the cascaded 32-bit pacer.
 *
 * The subdevice provides (*insn_read) and (*insn_write) operations to read
 * the current value and write an initial value to a counter. A (*insn_config)
 * operation is also provided to handle the following comedi instructions:
 *
 *	INSN_CONFIG_SET_COUNTER_MODE	calls comedi_8254_set_mode()
 *	INSN_CONFIG_8254_READ_STATUS	calls comedi_8254_status()
 *
 * The (*insn_config) member of comedi_8254 can be initialized by the external
 * driver to handle any additional instructions.
 *
 * NOTE: Gate control, clock routing, and any interrupt handling for the
 * counters is not handled by this module. These features are driver dependent.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "../comedidev.h"

#include "comedi_8254.h"

static unsigned int __i8254_read(struct comedi_8254 *i8254, unsigned int reg)
{
	unsigned int reg_offset = (reg * i8254->iosize) << i8254->regshift;
	unsigned int val;

	switch (i8254->iosize) {
	default:
	case I8254_IO8:
		if (i8254->mmio)
			val = readb(i8254->mmio + reg_offset);
		else
			val = inb(i8254->iobase + reg_offset);
		break;
	case I8254_IO16:
		if (i8254->mmio)
			val = readw(i8254->mmio + reg_offset);
		else
			val = inw(i8254->iobase + reg_offset);
		break;
	case I8254_IO32:
		if (i8254->mmio)
			val = readl(i8254->mmio + reg_offset);
		else
			val = inl(i8254->iobase + reg_offset);
		break;
	}
	return val & 0xff;
}

static void __i8254_write(struct comedi_8254 *i8254,
			  unsigned int val, unsigned int reg)
{
	unsigned int reg_offset = (reg * i8254->iosize) << i8254->regshift;

	switch (i8254->iosize) {
	default:
	case I8254_IO8:
		if (i8254->mmio)
			writeb(val, i8254->mmio + reg_offset);
		else
			outb(val, i8254->iobase + reg_offset);
		break;
	case I8254_IO16:
		if (i8254->mmio)
			writew(val, i8254->mmio + reg_offset);
		else
			outw(val, i8254->iobase + reg_offset);
		break;
	case I8254_IO32:
		if (i8254->mmio)
			writel(val, i8254->mmio + reg_offset);
		else
			outl(val, i8254->iobase + reg_offset);
		break;
	}
}

/**
 * comedi_8254_status - return the status of a counter
 * @i8254:	comedi_8254 struct for the timer
 * @counter:	the counter number
 */
unsigned int comedi_8254_status(struct comedi_8254 *i8254, unsigned int counter)
{
	unsigned int cmd;

	if (counter > 2)
		return 0;

	cmd = I8254_CTRL_READBACK_STATUS | I8254_CTRL_READBACK_SEL_CTR(counter);
	__i8254_write(i8254, cmd, I8254_CTRL_REG);

	return __i8254_read(i8254, counter);
}
EXPORT_SYMBOL_GPL(comedi_8254_status);

/**
 * comedi_8254_read - read the current counter value
 * @i8254:	comedi_8254 struct for the timer
 * @counter:	the counter number
 */
unsigned int comedi_8254_read(struct comedi_8254 *i8254, unsigned int counter)
{
	unsigned int val;

	if (counter > 2)
		return 0;

	/* latch counter */
	__i8254_write(i8254, I8254_CTRL_SEL_CTR(counter) | I8254_CTRL_LATCH,
		      I8254_CTRL_REG);

	/* read LSB then MSB */
	val = __i8254_read(i8254, counter);
	val |= (__i8254_read(i8254, counter) << 8);

	return val;
}
EXPORT_SYMBOL_GPL(comedi_8254_read);

/**
 * comedi_8254_write - load a 16-bit initial counter value
 * @i8254:	comedi_8254 struct for the timer
 * @counter:	the counter number
 * @val:	the initial value
 */
void comedi_8254_write(struct comedi_8254 *i8254,
		       unsigned int counter, unsigned int val)
{
	unsigned int byte;

	if (counter > 2)
		return;
	if (val > 0xffff)
		return;

	/* load LSB then MSB */
	byte = val & 0xff;
	__i8254_write(i8254, byte, counter);
	byte = (val >> 8) & 0xff;
	__i8254_write(i8254, byte, counter);
}
EXPORT_SYMBOL_GPL(comedi_8254_write);

/**
 * comedi_8254_set_mode - set the mode of a counter
 * @i8254:	comedi_8254 struct for the timer
 * @counter:	the counter number
 * @mode:	the I8254_MODEx and I8254_BCD|I8254_BINARY
 */
int comedi_8254_set_mode(struct comedi_8254 *i8254, unsigned int counter,
			 unsigned int mode)
{
	unsigned int byte;

	if (counter > 2)
		return -EINVAL;
	if (mode > (I8254_MODE5 | I8254_BCD))
		return -EINVAL;

	byte = I8254_CTRL_SEL_CTR(counter) |	/* select counter */
	       I8254_CTRL_LSB_MSB |		/* load LSB then MSB */
	       mode;				/* mode and BCD|binary */
	__i8254_write(i8254, byte, I8254_CTRL_REG);

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_8254_set_mode);

/**
 * comedi_8254_load - program the mode and initial count of a counter
 * @i8254:	comedi_8254 struct for the timer
 * @counter:	the counter number
 * @mode:	the I8254_MODEx and I8254_BCD|I8254_BINARY
 * @val:	the initial value
 */
int comedi_8254_load(struct comedi_8254 *i8254, unsigned int counter,
		     unsigned int val, unsigned int mode)
{
	if (counter > 2)
		return -EINVAL;
	if (val > 0xffff)
		return -EINVAL;
	if (mode > (I8254_MODE5 | I8254_BCD))
		return -EINVAL;

	comedi_8254_set_mode(i8254, counter, mode);
	comedi_8254_write(i8254, counter, val);

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_8254_load);

/**
 * comedi_8254_pacer_enable - set the mode and load the cascaded counters
 * @i8254:	comedi_8254 struct for the timer
 * @counter1:	the counter number for the first divisor
 * @counter2:	the counter number for the second divisor
 * @enable:	flag to enable (load) the counters
 */
void comedi_8254_pacer_enable(struct comedi_8254 *i8254,
			      unsigned int counter1,
			      unsigned int counter2,
			      bool enable)
{
	unsigned int mode;

	if (counter1 > 2 || counter2 > 2 || counter1 == counter2)
		return;

	if (enable)
		mode = I8254_MODE2 | I8254_BINARY;
	else
		mode = I8254_MODE0 | I8254_BINARY;

	comedi_8254_set_mode(i8254, counter1, mode);
	comedi_8254_set_mode(i8254, counter2, mode);

	if (enable) {
		/*
		 * Divisors are loaded second counter then first counter to
		 * avoid possible issues with the first counter expiring
		 * before the second counter is loaded.
		 */
		comedi_8254_write(i8254, counter2, i8254->divisor2);
		comedi_8254_write(i8254, counter1, i8254->divisor1);
	}
}
EXPORT_SYMBOL_GPL(comedi_8254_pacer_enable);

/**
 * comedi_8254_update_divisors - update the divisors for the cascaded counters
 * @i8254:	comedi_8254 struct for the timer
 */
void comedi_8254_update_divisors(struct comedi_8254 *i8254)
{
	/* masking is done since counter maps zero to 0x10000 */
	i8254->divisor = i8254->next_div & 0xffff;
	i8254->divisor1 = i8254->next_div1 & 0xffff;
	i8254->divisor2 = i8254->next_div2 & 0xffff;
}
EXPORT_SYMBOL_GPL(comedi_8254_update_divisors);

/**
 * comedi_8254_cascade_ns_to_timer - calculate the cascaded divisor values
 * @i8254:	comedi_8254 struct for the timer
 * @nanosec:	the desired ns time
 * @flags:	comedi_cmd flags
 */
void comedi_8254_cascade_ns_to_timer(struct comedi_8254 *i8254,
				     unsigned int *nanosec,
				     unsigned int flags)
{
	unsigned int d1 = i8254->next_div1 ? i8254->next_div1 : I8254_MAX_COUNT;
	unsigned int d2 = i8254->next_div2 ? i8254->next_div2 : I8254_MAX_COUNT;
	unsigned int div = d1 * d2;
	unsigned int ns_lub = 0xffffffff;
	unsigned int ns_glb = 0;
	unsigned int d1_lub = 0;
	unsigned int d1_glb = 0;
	unsigned int d2_lub = 0;
	unsigned int d2_glb = 0;
	unsigned int start;
	unsigned int ns;
	unsigned int ns_low;
	unsigned int ns_high;

	/* exit early if everything is already correct */
	if (div * i8254->osc_base == *nanosec &&
	    d1 > 1 && d1 <= I8254_MAX_COUNT &&
	    d2 > 1 && d2 <= I8254_MAX_COUNT &&
	    /* check for overflow */
	    div > d1 && div > d2 &&
	    div * i8254->osc_base > div &&
	    div * i8254->osc_base > i8254->osc_base)
		return;

	div = *nanosec / i8254->osc_base;
	d2 = I8254_MAX_COUNT;
	start = div / d2;
	if (start < 2)
		start = 2;
	for (d1 = start; d1 <= div / d1 + 1 && d1 <= I8254_MAX_COUNT; d1++) {
		for (d2 = div / d1;
		     d1 * d2 <= div + d1 + 1 && d2 <= I8254_MAX_COUNT; d2++) {
			ns = i8254->osc_base * d1 * d2;
			if (ns <= *nanosec && ns > ns_glb) {
				ns_glb = ns;
				d1_glb = d1;
				d2_glb = d2;
			}
			if (ns >= *nanosec && ns < ns_lub) {
				ns_lub = ns;
				d1_lub = d1;
				d2_lub = d2;
			}
		}
	}

	switch (flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_NEAREST:
	default:
		ns_high = d1_lub * d2_lub * i8254->osc_base;
		ns_low = d1_glb * d2_glb * i8254->osc_base;
		if (ns_high - *nanosec < *nanosec - ns_low) {
			d1 = d1_lub;
			d2 = d2_lub;
		} else {
			d1 = d1_glb;
			d2 = d2_glb;
		}
		break;
	case CMDF_ROUND_UP:
		d1 = d1_lub;
		d2 = d2_lub;
		break;
	case CMDF_ROUND_DOWN:
		d1 = d1_glb;
		d2 = d2_glb;
		break;
	}

	*nanosec = d1 * d2 * i8254->osc_base;
	i8254->next_div1 = d1;
	i8254->next_div2 = d2;
}
EXPORT_SYMBOL_GPL(comedi_8254_cascade_ns_to_timer);

/**
 * comedi_8254_ns_to_timer - calculate the divisor value for nanosec timing
 * @i8254:	comedi_8254 struct for the timer
 * @nanosec:	the desired ns time
 * @flags:	comedi_cmd flags
 */
void comedi_8254_ns_to_timer(struct comedi_8254 *i8254,
			     unsigned int *nanosec, unsigned int flags)
{
	unsigned int divisor;

	switch (flags & CMDF_ROUND_MASK) {
	default:
	case CMDF_ROUND_NEAREST:
		divisor = DIV_ROUND_CLOSEST(*nanosec, i8254->osc_base);
		break;
	case CMDF_ROUND_UP:
		divisor = DIV_ROUND_UP(*nanosec, i8254->osc_base);
		break;
	case CMDF_ROUND_DOWN:
		divisor = *nanosec / i8254->osc_base;
		break;
	}
	if (divisor < 2)
		divisor = 2;
	if (divisor > I8254_MAX_COUNT)
		divisor = I8254_MAX_COUNT;

	*nanosec = divisor * i8254->osc_base;
	i8254->next_div = divisor;
}
EXPORT_SYMBOL_GPL(comedi_8254_ns_to_timer);

/**
 * comedi_8254_set_busy - set/clear the "busy" flag for a given counter
 * @i8254:	comedi_8254 struct for the timer
 * @counter:	the counter number
 * @busy:	set/clear flag
 */
void comedi_8254_set_busy(struct comedi_8254 *i8254,
			  unsigned int counter, bool busy)
{
	if (counter < 3)
		i8254->busy[counter] = busy;
}
EXPORT_SYMBOL_GPL(comedi_8254_set_busy);

static int comedi_8254_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct comedi_8254 *i8254 = s->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	if (i8254->busy[chan])
		return -EBUSY;

	for (i = 0; i < insn->n; i++)
		data[i] = comedi_8254_read(i8254, chan);

	return insn->n;
}

static int comedi_8254_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct comedi_8254 *i8254 = s->private;
	unsigned int chan = CR_CHAN(insn->chanspec);

	if (i8254->busy[chan])
		return -EBUSY;

	if (insn->n)
		comedi_8254_write(i8254, chan, data[insn->n - 1]);

	return insn->n;
}

static int comedi_8254_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct comedi_8254 *i8254 = s->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int ret;

	if (i8254->busy[chan])
		return -EBUSY;

	switch (data[0]) {
	case INSN_CONFIG_RESET:
		ret = comedi_8254_set_mode(i8254, chan,
					   I8254_MODE0 | I8254_BINARY);
		if (ret)
			return ret;
		break;
	case INSN_CONFIG_SET_COUNTER_MODE:
		ret = comedi_8254_set_mode(i8254, chan, data[1]);
		if (ret)
			return ret;
		break;
	case INSN_CONFIG_8254_READ_STATUS:
		data[1] = comedi_8254_status(i8254, chan);
		break;
	default:
		/*
		 * If available, call the driver provided (*insn_config)
		 * to handle any driver implemented instructions.
		 */
		if (i8254->insn_config)
			return i8254->insn_config(dev, s, insn, data);

		return -EINVAL;
	}

	return insn->n;
}

/**
 * comedi_8254_subdevice_init - initialize a comedi_subdevice for the 8254 timer
 * @s:		comedi_subdevice struct
 */
void comedi_8254_subdevice_init(struct comedi_subdevice *s,
				struct comedi_8254 *i8254)
{
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 3;
	s->maxdata	= 0xffff;
	s->range_table	= &range_unknown;
	s->insn_read	= comedi_8254_insn_read;
	s->insn_write	= comedi_8254_insn_write;
	s->insn_config	= comedi_8254_insn_config;

	s->private	= i8254;
}
EXPORT_SYMBOL_GPL(comedi_8254_subdevice_init);

static struct comedi_8254 *__i8254_init(unsigned long iobase,
					void __iomem *mmio,
					unsigned int osc_base,
					unsigned int iosize,
					unsigned int regshift)
{
	struct comedi_8254 *i8254;
	int i;

	/* sanity check that the iosize is valid */
	if (!(iosize == I8254_IO8 || iosize == I8254_IO16 ||
	      iosize == I8254_IO32))
		return NULL;

	i8254 = kzalloc(sizeof(*i8254), GFP_KERNEL);
	if (!i8254)
		return NULL;

	i8254->iobase	= iobase;
	i8254->mmio	= mmio;
	i8254->iosize	= iosize;
	i8254->regshift	= regshift;

	/* default osc_base to the max speed of a generic 8254 timer */
	i8254->osc_base	= osc_base ? osc_base : I8254_OSC_BASE_10MHZ;

	/* reset all the counters by setting them to I8254_MODE0 */
	for (i = 0; i < 3; i++)
		comedi_8254_set_mode(i8254, i, I8254_MODE0 | I8254_BINARY);

	return i8254;
}

/**
 * comedi_8254_init - allocate and initialize the 8254 device for pio access
 * @mmio:	port I/O base address
 * @osc_base:	base time of the counter in ns
 *		OPTIONAL - only used by comedi_8254_cascade_ns_to_timer()
 * @iosize:	I/O register size
 * @regshift:	register gap shift
 */
struct comedi_8254 *comedi_8254_init(unsigned long iobase,
				     unsigned int osc_base,
				     unsigned int iosize,
				     unsigned int regshift)
{
	return __i8254_init(iobase, NULL, osc_base, iosize, regshift);
}
EXPORT_SYMBOL_GPL(comedi_8254_init);

/**
 * comedi_8254_mm_init - allocate and initialize the 8254 device for mmio access
 * @mmio:	memory mapped I/O base address
 * @osc_base:	base time of the counter in ns
 *		OPTIONAL - only used by comedi_8254_cascade_ns_to_timer()
 * @iosize:	I/O register size
 * @regshift:	register gap shift
 */
struct comedi_8254 *comedi_8254_mm_init(void __iomem *mmio,
					unsigned int osc_base,
					unsigned int iosize,
					unsigned int regshift)
{
	return __i8254_init(0, mmio, osc_base, iosize, regshift);
}
EXPORT_SYMBOL_GPL(comedi_8254_mm_init);

static int __init comedi_8254_module_init(void)
{
	return 0;
}
module_init(comedi_8254_module_init);

static void __exit comedi_8254_module_exit(void)
{
}
module_exit(comedi_8254_module_exit);

MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_DESCRIPTION("Comedi: Generic 8254 timer/counter support");
MODULE_LICENSE("GPL");
