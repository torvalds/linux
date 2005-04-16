/*
 *  giu.c, General-purpose I/O Unit Interrupt routines for NEC VR4100 series.
 *
 *  Copyright (C) 2002 MontaVista Software Inc.
 *    Author: Yoichi Yuasa <yyuasa@mvista.com or source@mvista.com>
 *  Copyright (C) 2003-2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *  Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4111, VR4121, VR4122 and VR4131 are supported.
 *
 *  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *  - Added support for NEC VR4133.
 *  - Removed board_irq_init.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/vr41xx/vr41xx.h>

#define GIUIOSELL_TYPE1	KSEG1ADDR(0x0b000100)
#define GIUIOSELL_TYPE2	KSEG1ADDR(0x0f000140)

#define GIUIOSELL	0x00
#define GIUIOSELH	0x02
#define GIUINTSTATL	0x08
#define GIUINTSTATH	0x0a
#define GIUINTENL	0x0c
#define GIUINTENH	0x0e
#define GIUINTTYPL	0x10
#define GIUINTTYPH	0x12
#define GIUINTALSELL	0x14
#define GIUINTALSELH	0x16
#define GIUINTHTSELL	0x18
#define GIUINTHTSELH	0x1a
#define GIUFEDGEINHL	0x20
#define GIUFEDGEINHH	0x22
#define GIUREDGEINHL	0x24
#define GIUREDGEINHH	0x26

static uint32_t giu_base;

static struct irqaction giu_cascade = {
	.handler	= no_action,
	.mask		= CPU_MASK_NONE,
	.name		= "cascade",
};

#define read_giuint(offset)		readw(giu_base + (offset))
#define write_giuint(val, offset)	writew((val), giu_base + (offset))

#define GIUINT_HIGH_OFFSET	16

static inline uint16_t set_giuint(uint8_t offset, uint16_t set)
{
	uint16_t res;

	res = read_giuint(offset);
	res |= set;
	write_giuint(res, offset);

	return res;
}

static inline uint16_t clear_giuint(uint8_t offset, uint16_t clear)
{
	uint16_t res;

	res = read_giuint(offset);
	res &= ~clear;
	write_giuint(res, offset);

	return res;
}

static unsigned int startup_giuint_low_irq(unsigned int irq)
{
	unsigned int pin;

	pin = GIU_IRQ_TO_PIN(irq);
	write_giuint((uint16_t)1 << pin, GIUINTSTATL);
	set_giuint(GIUINTENL, (uint16_t)1 << pin);

	return 0;
}

static void shutdown_giuint_low_irq(unsigned int irq)
{
	clear_giuint(GIUINTENL, (uint16_t)1 << GIU_IRQ_TO_PIN(irq));
}

static void enable_giuint_low_irq(unsigned int irq)
{
	set_giuint(GIUINTENL, (uint16_t)1 << GIU_IRQ_TO_PIN(irq));
}

#define disable_giuint_low_irq	shutdown_giuint_low_irq

static void ack_giuint_low_irq(unsigned int irq)
{
	unsigned int pin;

	pin = GIU_IRQ_TO_PIN(irq);
	clear_giuint(GIUINTENL, (uint16_t)1 << pin);
	write_giuint((uint16_t)1 << pin, GIUINTSTATL);
}

static void end_giuint_low_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		set_giuint(GIUINTENL, (uint16_t)1 << GIU_IRQ_TO_PIN(irq));
}

static struct hw_interrupt_type giuint_low_irq_type = {
	.typename	= "GIUINTL",
	.startup	= startup_giuint_low_irq,
	.shutdown	= shutdown_giuint_low_irq,
	.enable		= enable_giuint_low_irq,
	.disable	= disable_giuint_low_irq,
	.ack		= ack_giuint_low_irq,
	.end		= end_giuint_low_irq,
};

static unsigned int startup_giuint_high_irq(unsigned int irq)
{
	unsigned int pin;

	pin = GIU_IRQ_TO_PIN(irq - GIUINT_HIGH_OFFSET);
	write_giuint((uint16_t)1 << pin, GIUINTSTATH);
	set_giuint(GIUINTENH, (uint16_t)1 << pin);

	return 0;
}

static void shutdown_giuint_high_irq(unsigned int irq)
{
	clear_giuint(GIUINTENH, (uint16_t)1 << GIU_IRQ_TO_PIN(irq - GIUINT_HIGH_OFFSET));
}

static void enable_giuint_high_irq(unsigned int irq)
{
	set_giuint(GIUINTENH, (uint16_t)1 << GIU_IRQ_TO_PIN(irq - GIUINT_HIGH_OFFSET));
}

#define disable_giuint_high_irq	shutdown_giuint_high_irq

static void ack_giuint_high_irq(unsigned int irq)
{
	unsigned int pin;

	pin = GIU_IRQ_TO_PIN(irq - GIUINT_HIGH_OFFSET);
	clear_giuint(GIUINTENH, (uint16_t)1 << pin);
	write_giuint((uint16_t)1 << pin, GIUINTSTATH);
}

static void end_giuint_high_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		set_giuint(GIUINTENH, (uint16_t)1 << GIU_IRQ_TO_PIN(irq - GIUINT_HIGH_OFFSET));
}

static struct hw_interrupt_type giuint_high_irq_type = {
	.typename	= "GIUINTH",
	.startup	= startup_giuint_high_irq,
	.shutdown	= shutdown_giuint_high_irq,
	.enable		= enable_giuint_high_irq,
	.disable	= disable_giuint_high_irq,
	.ack		= ack_giuint_high_irq,
	.end		= end_giuint_high_irq,
};

void __init init_vr41xx_giuint_irq(void)
{
	int i;

	for (i = GIU_IRQ_BASE; i <= GIU_IRQ_LAST; i++) {
		if (i < (GIU_IRQ_BASE + GIUINT_HIGH_OFFSET))
			irq_desc[i].handler = &giuint_low_irq_type;
		else
			irq_desc[i].handler = &giuint_high_irq_type;
	}

	setup_irq(GIUINT_CASCADE_IRQ, &giu_cascade);
}

void vr41xx_set_irq_trigger(int pin, int trigger, int hold)
{
	uint16_t mask;

	if (pin < GIUINT_HIGH_OFFSET) {
		mask = (uint16_t)1 << pin;
		if (trigger != TRIGGER_LEVEL) {
        		set_giuint(GIUINTTYPL, mask);
			if (hold == SIGNAL_HOLD)
				set_giuint(GIUINTHTSELL, mask);
			else
				clear_giuint(GIUINTHTSELL, mask);
			if (current_cpu_data.cputype == CPU_VR4133) {
				switch (trigger) {
				case TRIGGER_EDGE_FALLING:
					set_giuint(GIUFEDGEINHL, mask);
					clear_giuint(GIUREDGEINHL, mask);
					break;
				case TRIGGER_EDGE_RISING:
					clear_giuint(GIUFEDGEINHL, mask);
					set_giuint(GIUREDGEINHL, mask);
					break;
				default:
					set_giuint(GIUFEDGEINHL, mask);
					set_giuint(GIUREDGEINHL, mask);
					break;
				}
			}
		} else {
			clear_giuint(GIUINTTYPL, mask);
			clear_giuint(GIUINTHTSELL, mask);
		}
		write_giuint(mask, GIUINTSTATL);
	} else {
		mask = (uint16_t)1 << (pin - GIUINT_HIGH_OFFSET);
		if (trigger != TRIGGER_LEVEL) {
			set_giuint(GIUINTTYPH, mask);
			if (hold == SIGNAL_HOLD)
				set_giuint(GIUINTHTSELH, mask);
			else
				clear_giuint(GIUINTHTSELH, mask);
			if (current_cpu_data.cputype == CPU_VR4133) {
				switch (trigger) {
				case TRIGGER_EDGE_FALLING:
					set_giuint(GIUFEDGEINHH, mask);
					clear_giuint(GIUREDGEINHH, mask);
					break;
				case TRIGGER_EDGE_RISING:
					clear_giuint(GIUFEDGEINHH, mask);
					set_giuint(GIUREDGEINHH, mask);
					break;
				default:
					set_giuint(GIUFEDGEINHH, mask);
					set_giuint(GIUREDGEINHH, mask);
					break;
				}
			}
		} else {
			clear_giuint(GIUINTTYPH, mask);
			clear_giuint(GIUINTHTSELH, mask);
		}
		write_giuint(mask, GIUINTSTATH);
	}
}

EXPORT_SYMBOL(vr41xx_set_irq_trigger);

void vr41xx_set_irq_level(int pin, int level)
{
	uint16_t mask;

	if (pin < GIUINT_HIGH_OFFSET) {
		mask = (uint16_t)1 << pin;
		if (level == LEVEL_HIGH)
			set_giuint(GIUINTALSELL, mask);
		else
			clear_giuint(GIUINTALSELL, mask);
		write_giuint(mask, GIUINTSTATL);
	} else {
		mask = (uint16_t)1 << (pin - GIUINT_HIGH_OFFSET);
		if (level == LEVEL_HIGH)
			set_giuint(GIUINTALSELH, mask);
		else
			clear_giuint(GIUINTALSELH, mask);
		write_giuint(mask, GIUINTSTATH);
	}
}

EXPORT_SYMBOL(vr41xx_set_irq_level);

#define GIUINT_NR_IRQS		32

enum {
	GIUINT_NO_CASCADE,
	GIUINT_CASCADE
};

struct vr41xx_giuint_cascade {
	unsigned int flag;
	int (*get_irq_number)(int irq);
};

static struct vr41xx_giuint_cascade giuint_cascade[GIUINT_NR_IRQS];

static int no_irq_number(int irq)
{
	return -EINVAL;
}

int vr41xx_cascade_irq(unsigned int irq, int (*get_irq_number)(int irq))
{
	unsigned int pin;
	int retval;

	if (irq < GIU_IRQ(0) || irq > GIU_IRQ(31))
		return -EINVAL;

	if(!get_irq_number)
		return -EINVAL;

	pin = GIU_IRQ_TO_PIN(irq);
	giuint_cascade[pin].flag = GIUINT_CASCADE;
	giuint_cascade[pin].get_irq_number = get_irq_number;

	retval = setup_irq(irq, &giu_cascade);
	if (retval != 0) {
		giuint_cascade[pin].flag = GIUINT_NO_CASCADE;
		giuint_cascade[pin].get_irq_number = no_irq_number;
	}

	return retval;
}

EXPORT_SYMBOL(vr41xx_cascade_irq);

static inline int get_irq_pin_number(void)
{
	uint16_t pendl, pendh, maskl, maskh;
	int i;

	pendl = read_giuint(GIUINTSTATL);
	pendh = read_giuint(GIUINTSTATH);
	maskl = read_giuint(GIUINTENL);
	maskh = read_giuint(GIUINTENH);

	maskl &= pendl;
	maskh &= pendh;

	if (maskl) {
		for (i = 0; i < 16; i++) {
			if (maskl & ((uint16_t)1 << i))
				return i;
		}
	} else if (maskh) {
		for (i = 0; i < 16; i++) {
			if (maskh & ((uint16_t)1 << i))
				return i + GIUINT_HIGH_OFFSET;
		}
	}

	printk(KERN_ERR "spurious GIU interrupt: %04x(%04x),%04x(%04x)\n",
	       maskl, pendl, maskh, pendh);

	atomic_inc(&irq_err_count);

	return -1;
}

static inline void ack_giuint_irq(int pin)
{
	if (pin < GIUINT_HIGH_OFFSET) {
		clear_giuint(GIUINTENL, (uint16_t)1 << pin);
		write_giuint((uint16_t)1 << pin, GIUINTSTATL);
	} else {
		pin -= GIUINT_HIGH_OFFSET;
		clear_giuint(GIUINTENH, (uint16_t)1 << pin);
		write_giuint((uint16_t)1 << pin, GIUINTSTATH);
	}
}

static inline void end_giuint_irq(int pin)
{
	if (pin < GIUINT_HIGH_OFFSET)
		set_giuint(GIUINTENL, (uint16_t)1 << pin);
	else
		set_giuint(GIUINTENH, (uint16_t)1 << (pin - GIUINT_HIGH_OFFSET));
}

void giuint_irq_dispatch(struct pt_regs *regs)
{
	struct vr41xx_giuint_cascade *cascade;
	unsigned int giuint_irq;
	int pin;

	pin = get_irq_pin_number();
	if (pin < 0)
		return;

	disable_irq(GIUINT_CASCADE_IRQ);

	cascade = &giuint_cascade[pin];
	giuint_irq = GIU_IRQ(pin);
	if (cascade->flag == GIUINT_CASCADE) {
		int irq = cascade->get_irq_number(giuint_irq);
		ack_giuint_irq(pin);
		if (irq >= 0)
			do_IRQ(irq, regs);
		end_giuint_irq(pin);
	} else {
		do_IRQ(giuint_irq, regs);
	}

	enable_irq(GIUINT_CASCADE_IRQ);
}

static int __init vr41xx_giu_init(void)
{
	int i;

	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
	case CPU_VR4121:
		giu_base = GIUIOSELL_TYPE1;
		break;
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		giu_base = GIUIOSELL_TYPE2;
		break;
	default:
		printk(KERN_ERR "GIU: Unexpected CPU of NEC VR4100 series\n");
		return -EINVAL;
	}

	for (i = 0; i < GIUINT_NR_IRQS; i++) {
		if (i < GIUINT_HIGH_OFFSET)
			clear_giuint(GIUINTENL, (uint16_t)1 << i);
		else
			clear_giuint(GIUINTENH, (uint16_t)1 << (i - GIUINT_HIGH_OFFSET));
		giuint_cascade[i].flag = GIUINT_NO_CASCADE;
		giuint_cascade[i].get_irq_number = no_irq_number;
	}

	return 0;
}

early_initcall(vr41xx_giu_init);
