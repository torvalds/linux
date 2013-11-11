/*
    comedi/drivers/8253.h
    Header file for 8253

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

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

#ifndef _8253_H
#define _8253_H

#include "../comedi.h"

#define i8253_cascade_ns_to_timer i8253_cascade_ns_to_timer_2div

static inline void i8253_cascade_ns_to_timer_2div_old(int i8253_osc_base,
						      unsigned int *d1,
						      unsigned int *d2,
						      unsigned int *nanosec,
						      int round_mode)
{
	int divider;
	int div1, div2;
	int div1_glb, div2_glb, ns_glb;
	int div1_lub, div2_lub, ns_lub;
	int ns;

	divider = (*nanosec + i8253_osc_base / 2) / i8253_osc_base;

	/* find 2 integers 1<={x,y}<=65536 such that x*y is
	   close to divider */

	div1_lub = div2_lub = 0;
	div1_glb = div2_glb = 0;

	ns_glb = 0;
	ns_lub = 0xffffffff;

	div2 = 0x10000;
	for (div1 = divider / 65536 + 1; div1 < div2; div1++) {
		div2 = divider / div1;

		ns = i8253_osc_base * div1 * div2;
		if (ns <= *nanosec && ns > ns_glb) {
			ns_glb = ns;
			div1_glb = div1;
			div2_glb = div2;
		}

		div2++;
		if (div2 <= 65536) {
			ns = i8253_osc_base * div1 * div2;
			if (ns > *nanosec && ns < ns_lub) {
				ns_lub = ns;
				div1_lub = div1;
				div2_lub = div2;
			}
		}
	}

	*nanosec = div1_lub * div2_lub * i8253_osc_base;
	*d1 = div1_lub & 0xffff;
	*d2 = div2_lub & 0xffff;
	return;
}

static inline void i8253_cascade_ns_to_timer_power(int i8253_osc_base,
						   unsigned int *d1,
						   unsigned int *d2,
						   unsigned int *nanosec,
						   int round_mode)
{
	int div1, div2;
	int base;

	for (div1 = 2; div1 <= (1 << 16); div1 <<= 1) {
		base = i8253_osc_base * div1;
		round_mode &= TRIG_ROUND_MASK;
		switch (round_mode) {
		case TRIG_ROUND_NEAREST:
		default:
			div2 = (*nanosec + base / 2) / base;
			break;
		case TRIG_ROUND_DOWN:
			div2 = (*nanosec) / base;
			break;
		case TRIG_ROUND_UP:
			div2 = (*nanosec + base - 1) / base;
			break;
		}
		if (div2 < 2)
			div2 = 2;
		if (div2 <= 65536) {
			*nanosec = div2 * base;
			*d1 = div1 & 0xffff;
			*d2 = div2 & 0xffff;
			return;
		}
	}

	/* shouldn't get here */
	div1 = 0x10000;
	div2 = 0x10000;
	*nanosec = div1 * div2 * i8253_osc_base;
	*d1 = div1 & 0xffff;
	*d2 = div2 & 0xffff;
}

static inline void i8253_cascade_ns_to_timer_2div(int i8253_osc_base,
						  unsigned int *d1,
						  unsigned int *d2,
						  unsigned int *nanosec,
						  int round_mode)
{
	unsigned int divider;
	unsigned int div1, div2;
	unsigned int div1_glb, div2_glb, ns_glb;
	unsigned int div1_lub, div2_lub, ns_lub;
	unsigned int ns;
	unsigned int start;
	unsigned int ns_low, ns_high;
	static const unsigned int max_count = 0x10000;
	/* exit early if everything is already correct (this can save time
	 * since this function may be called repeatedly during command tests
	 * and execution) */
	div1 = *d1 ? *d1 : max_count;
	div2 = *d2 ? *d2 : max_count;
	divider = div1 * div2;
	if (div1 * div2 * i8253_osc_base == *nanosec &&
	    div1 > 1 && div1 <= max_count && div2 > 1 && div2 <= max_count &&
	    /* check for overflow */
	    divider > div1 && divider > div2 &&
	    divider * i8253_osc_base > divider &&
	    divider * i8253_osc_base > i8253_osc_base) {
		return;
	}

	divider = *nanosec / i8253_osc_base;

	div1_lub = div2_lub = 0;
	div1_glb = div2_glb = 0;

	ns_glb = 0;
	ns_lub = 0xffffffff;

	div2 = max_count;
	start = divider / div2;
	if (start < 2)
		start = 2;
	for (div1 = start; div1 <= divider / div1 + 1 && div1 <= max_count;
	     div1++) {
		for (div2 = divider / div1;
		     div1 * div2 <= divider + div1 + 1 && div2 <= max_count;
		     div2++) {
			ns = i8253_osc_base * div1 * div2;
			if (ns <= *nanosec && ns > ns_glb) {
				ns_glb = ns;
				div1_glb = div1;
				div2_glb = div2;
			}
			if (ns >= *nanosec && ns < ns_lub) {
				ns_lub = ns;
				div1_lub = div1;
				div2_lub = div2;
			}
		}
	}

	round_mode &= TRIG_ROUND_MASK;
	switch (round_mode) {
	case TRIG_ROUND_NEAREST:
	default:
		ns_high = div1_lub * div2_lub * i8253_osc_base;
		ns_low = div1_glb * div2_glb * i8253_osc_base;
		if (ns_high - *nanosec < *nanosec - ns_low) {
			div1 = div1_lub;
			div2 = div2_lub;
		} else {
			div1 = div1_glb;
			div2 = div2_glb;
		}
		break;
	case TRIG_ROUND_UP:
		div1 = div1_lub;
		div2 = div2_lub;
		break;
	case TRIG_ROUND_DOWN:
		div1 = div1_glb;
		div2 = div2_glb;
		break;
	}

	*nanosec = div1 * div2 * i8253_osc_base;
	/*  masking is done since counter maps zero to 0x10000 */
	*d1 = div1 & 0xffff;
	*d2 = div2 & 0xffff;
	return;
}

#ifndef CMDTEST
/* i8254_load programs 8254 counter chip.  It should also work for the 8253.
 * base_address is the lowest io address
 * for the chip (the address of counter 0).
 * counter_number is the counter you want to load (0,1 or 2)
 * count is the number to load into the counter.
 *
 * You probably want to use mode 2.
 *
 * Use i8254_mm_load() if you board uses memory-mapped io, it is
 * the same as i8254_load() except it uses writeb() instead of outb().
 *
 * Neither i8254_load() or i8254_read() do their loading/reading
 * atomically.  The 16 bit read/writes are performed with two successive
 * 8 bit read/writes.  So if two parts of your driver do a load/read on
 * the same counter, it may be necessary to protect these functions
 * with a spinlock.
 *
 * FMH
 */

#define i8254_control_reg	3

static inline int i8254_load(unsigned long base_address, unsigned int regshift,
			     unsigned int counter_number, unsigned int count,
			     unsigned int mode)
{
	unsigned int byte;

	if (counter_number > 2)
		return -1;
	if (count > 0xffff)
		return -1;
	if (mode > 5)
		return -1;
	if ((mode == 2 || mode == 3) && count == 1)
		return -1;

	byte = counter_number << 6;
	byte |= 0x30;		/*  load low then high byte */
	byte |= (mode << 1);	/*  set counter mode */
	outb(byte, base_address + (i8254_control_reg << regshift));
	byte = count & 0xff;	/*  lsb of counter value */
	outb(byte, base_address + (counter_number << regshift));
	byte = (count >> 8) & 0xff;	/*  msb of counter value */
	outb(byte, base_address + (counter_number << regshift));

	return 0;
}

static inline int i8254_mm_load(void __iomem *base_address,
				unsigned int regshift,
				unsigned int counter_number,
				unsigned int count,
				unsigned int mode)
{
	unsigned int byte;

	if (counter_number > 2)
		return -1;
	if (count > 0xffff)
		return -1;
	if (mode > 5)
		return -1;
	if ((mode == 2 || mode == 3) && count == 1)
		return -1;

	byte = counter_number << 6;
	byte |= 0x30;		/*  load low then high byte */
	byte |= (mode << 1);	/*  set counter mode */
	writeb(byte, base_address + (i8254_control_reg << regshift));
	byte = count & 0xff;	/*  lsb of counter value */
	writeb(byte, base_address + (counter_number << regshift));
	byte = (count >> 8) & 0xff;	/*  msb of counter value */
	writeb(byte, base_address + (counter_number << regshift));

	return 0;
}

/* Returns 16 bit counter value, should work for 8253 also.*/
static inline int i8254_read(unsigned long base_address, unsigned int regshift,
			     unsigned int counter_number)
{
	unsigned int byte;
	int ret;

	if (counter_number > 2)
		return -1;

	/*  latch counter */
	byte = counter_number << 6;
	outb(byte, base_address + (i8254_control_reg << regshift));

	/*  read lsb */
	ret = inb(base_address + (counter_number << regshift));
	/*  read msb */
	ret += inb(base_address + (counter_number << regshift)) << 8;

	return ret;
}

static inline int i8254_mm_read(void __iomem *base_address,
				unsigned int regshift,
				unsigned int counter_number)
{
	unsigned int byte;
	int ret;

	if (counter_number > 2)
		return -1;

	/*  latch counter */
	byte = counter_number << 6;
	writeb(byte, base_address + (i8254_control_reg << regshift));

	/*  read lsb */
	ret = readb(base_address + (counter_number << regshift));
	/*  read msb */
	ret += readb(base_address + (counter_number << regshift)) << 8;

	return ret;
}

/* Loads 16 bit initial counter value, should work for 8253 also. */
static inline void i8254_write(unsigned long base_address,
			       unsigned int regshift,
			       unsigned int counter_number, unsigned int count)
{
	unsigned int byte;

	if (counter_number > 2)
		return;

	byte = count & 0xff;	/*  lsb of counter value */
	outb(byte, base_address + (counter_number << regshift));
	byte = (count >> 8) & 0xff;	/*  msb of counter value */
	outb(byte, base_address + (counter_number << regshift));
}

static inline void i8254_mm_write(void __iomem *base_address,
				  unsigned int regshift,
				  unsigned int counter_number,
				  unsigned int count)
{
	unsigned int byte;

	if (counter_number > 2)
		return;

	byte = count & 0xff;	/*  lsb of counter value */
	writeb(byte, base_address + (counter_number << regshift));
	byte = (count >> 8) & 0xff;	/*  msb of counter value */
	writeb(byte, base_address + (counter_number << regshift));
}

/* Set counter mode, should work for 8253 also.
 * Note: the 'mode' value is different to that for i8254_load() and comes
 * from the INSN_CONFIG_8254_SET_MODE command:
 *   I8254_MODE0, I8254_MODE1, ..., I8254_MODE5
 * OR'ed with:
 *   I8254_BCD, I8254_BINARY
 */
static inline int i8254_set_mode(unsigned long base_address,
				 unsigned int regshift,
				 unsigned int counter_number, unsigned int mode)
{
	unsigned int byte;

	if (counter_number > 2)
		return -1;
	if (mode > (I8254_MODE5 | I8254_BINARY))
		return -1;

	byte = counter_number << 6;
	byte |= 0x30;		/*  load low then high byte */
	byte |= mode;		/*  set counter mode and BCD|binary */
	outb(byte, base_address + (i8254_control_reg << regshift));

	return 0;
}

static inline int i8254_mm_set_mode(void __iomem *base_address,
				    unsigned int regshift,
				    unsigned int counter_number,
				    unsigned int mode)
{
	unsigned int byte;

	if (counter_number > 2)
		return -1;
	if (mode > (I8254_MODE5 | I8254_BINARY))
		return -1;

	byte = counter_number << 6;
	byte |= 0x30;		/*  load low then high byte */
	byte |= mode;		/*  set counter mode and BCD|binary */
	writeb(byte, base_address + (i8254_control_reg << regshift));

	return 0;
}

static inline int i8254_status(unsigned long base_address,
			       unsigned int regshift,
			       unsigned int counter_number)
{
	outb(0xE0 | (2 << counter_number),
	     base_address + (i8254_control_reg << regshift));
	return inb(base_address + (counter_number << regshift));
}

static inline int i8254_mm_status(void __iomem *base_address,
				  unsigned int regshift,
				  unsigned int counter_number)
{
	writeb(0xE0 | (2 << counter_number),
	       base_address + (i8254_control_reg << regshift));
	return readb(base_address + (counter_number << regshift));
}

#endif

#endif
