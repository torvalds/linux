/*
 * s6000 gpio driver
 *
 * Copyright (c) 2009 emlix GmbH
 * Authors:	Oskar Schirmer <os@emlix.com>
 *		Johannes Weiner <jw@emlix.com>
 *		Daniel Gloeckner <dg@emlix.com>
 */
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio.h>

#include <variant/hardware.h>

#define IRQ_BASE XTENSA_NR_IRQS

#define S6_GPIO_DATA		0x000
#define S6_GPIO_IS		0x404
#define S6_GPIO_IBE		0x408
#define S6_GPIO_IEV		0x40C
#define S6_GPIO_IE		0x410
#define S6_GPIO_RIS		0x414
#define S6_GPIO_MIS		0x418
#define S6_GPIO_IC		0x41C
#define S6_GPIO_AFSEL		0x420
#define S6_GPIO_DIR		0x800
#define S6_GPIO_BANK(nr)	((nr) * 0x1000)
#define S6_GPIO_MASK(nr)	(4 << (nr))
#define S6_GPIO_OFFSET(nr) \
		(S6_GPIO_BANK((nr) >> 3) + S6_GPIO_MASK((nr) & 7))

static int direction_input(struct gpio_chip *chip, unsigned int off)
{
	writeb(0, S6_REG_GPIO + S6_GPIO_DIR + S6_GPIO_OFFSET(off));
	return 0;
}

static int get(struct gpio_chip *chip, unsigned int off)
{
	return readb(S6_REG_GPIO + S6_GPIO_DATA + S6_GPIO_OFFSET(off));
}

static int direction_output(struct gpio_chip *chip, unsigned int off, int val)
{
	unsigned rel = S6_GPIO_OFFSET(off);
	writeb(~0, S6_REG_GPIO + S6_GPIO_DIR + rel);
	writeb(val ? ~0 : 0, S6_REG_GPIO + S6_GPIO_DATA + rel);
	return 0;
}

static void set(struct gpio_chip *chip, unsigned int off, int val)
{
	writeb(val ? ~0 : 0, S6_REG_GPIO + S6_GPIO_DATA + S6_GPIO_OFFSET(off));
}

static int to_irq(struct gpio_chip *chip, unsigned offset)
{
	if (offset < 8)
		return offset + IRQ_BASE;
	return -EINVAL;
}

static struct gpio_chip gpiochip = {
	.owner = THIS_MODULE,
	.direction_input = direction_input,
	.get = get,
	.direction_output = direction_output,
	.set = set,
	.to_irq = to_irq,
	.base = 0,
	.ngpio = 24,
	.can_sleep = 0, /* no blocking io needed */
	.exported = 0, /* no exporting to userspace */
};

int s6_gpio_init(u32 afsel)
{
	writeb(afsel, S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_AFSEL);
	writeb(afsel >> 8, S6_REG_GPIO + S6_GPIO_BANK(1) + S6_GPIO_AFSEL);
	writeb(afsel >> 16, S6_REG_GPIO + S6_GPIO_BANK(2) + S6_GPIO_AFSEL);
	return gpiochip_add(&gpiochip);
}

static void ack(unsigned int irq)
{
	writeb(1 << (irq - IRQ_BASE), S6_REG_GPIO + S6_GPIO_IC);
}

static void mask(unsigned int irq)
{
	u8 r = readb(S6_REG_GPIO + S6_GPIO_IE);
	r &= ~(1 << (irq - IRQ_BASE));
	writeb(r, S6_REG_GPIO + S6_GPIO_IE);
}

static void unmask(unsigned int irq)
{
	u8 m = readb(S6_REG_GPIO + S6_GPIO_IE);
	m |= 1 << (irq - IRQ_BASE);
	writeb(m, S6_REG_GPIO + S6_GPIO_IE);
}

static int set_type(unsigned int irq, unsigned int type)
{
	const u8 m = 1 << (irq - IRQ_BASE);
	irq_flow_handler_t handler;
	struct irq_desc *desc;
	u8 reg;

	if (type == IRQ_TYPE_PROBE) {
		if ((readb(S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_AFSEL) & m)
		    || (readb(S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_IE) & m)
		    || readb(S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_DIR
			      + S6_GPIO_MASK(irq - IRQ_BASE)))
			return 0;
		type = IRQ_TYPE_EDGE_BOTH;
	}

	reg = readb(S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_IS);
	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH)) {
		reg |= m;
		handler = handle_level_irq;
	} else {
		reg &= ~m;
		handler = handle_edge_irq;
	}
	writeb(reg, S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_IS);
	desc = irq_to_desc(irq);
	desc->handle_irq = handler;

	reg = readb(S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_IEV);
	if (type & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_EDGE_RISING))
		reg |= m;
	else
		reg &= ~m;
	writeb(reg, S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_IEV);

	reg = readb(S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_IBE);
	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		reg |= m;
	else
		reg &= ~m;
	writeb(reg, S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_IBE);
	return 0;
}

static struct irq_chip gpioirqs = {
	.name = "GPIO",
	.ack = ack,
	.mask = mask,
	.unmask = unmask,
	.set_type = set_type,
};

static u8 demux_masks[4];

static void demux_irqs(unsigned int irq, struct irq_desc *desc)
{
	u8 *mask = get_irq_desc_data(desc);
	u8 pending;
	int cirq;

	desc->chip->mask(irq);
	desc->chip->ack(irq);
	pending = readb(S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_MIS) & *mask;
	cirq = IRQ_BASE - 1;
	while (pending) {
		int n = ffs(pending);
		cirq += n;
		pending >>= n;
		generic_handle_irq(cirq);
	}
	desc->chip->unmask(irq);
}

extern const signed char *platform_irq_mappings[XTENSA_NR_IRQS];

void __init variant_init_irq(void)
{
	int irq, n;
	writeb(0, S6_REG_GPIO + S6_GPIO_BANK(0) + S6_GPIO_IE);
	for (irq = n = 0; irq < XTENSA_NR_IRQS; irq++) {
		const signed char *mapping = platform_irq_mappings[irq];
		int alone = 1;
		u8 mask;
		if (!mapping)
			continue;
		for(mask = 0; *mapping != -1; mapping++)
			switch (*mapping) {
			case S6_INTC_GPIO(0):
				mask |= 1 << 0;
				break;
			case S6_INTC_GPIO(1):
				mask |= 1 << 1;
				break;
			case S6_INTC_GPIO(2):
				mask |= 1 << 2;
				break;
			case S6_INTC_GPIO(3):
				mask |= 0x1f << 3;
				break;
			default:
				alone = 0;
			}
		if (mask) {
			int cirq, i;
			if (!alone) {
				printk(KERN_ERR "chained irq chips can't share"
					" parent irq %i\n", irq);
				continue;
			}
			demux_masks[n] = mask;
			cirq = IRQ_BASE - 1;
			do {
				i = ffs(mask);
				cirq += i;
				mask >>= i;
				set_irq_chip(cirq, &gpioirqs);
				set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);
			} while (mask);
			set_irq_data(irq, demux_masks + n);
			set_irq_chained_handler(irq, demux_irqs);
			if (++n == ARRAY_SIZE(demux_masks))
				break;
		}
	}
}
