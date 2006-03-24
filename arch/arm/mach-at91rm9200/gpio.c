/*
 * linux/arch/arm/mach-at91rm9200/gpio.c
 *
 * Copyright (C) 2005 HP Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/arch/hardware.h>
#include <asm/arch/gpio.h>

static const u32 pio_controller_offset[4] = {
	AT91_PIOA,
	AT91_PIOB,
	AT91_PIOC,
	AT91_PIOD,
};

static inline void __iomem *pin_to_controller(unsigned pin)
{
	void __iomem *sys_base = (void __iomem *) AT91_VA_BASE_SYS;

	pin -= PIN_BASE;
	pin /= 32;
	if (likely(pin < BGA_GPIO_BANKS))
		return sys_base + pio_controller_offset[pin];

	return NULL;
}

static inline unsigned pin_to_mask(unsigned pin)
{
	pin -= PIN_BASE;
	return 1 << (pin % 32);
}


/*--------------------------------------------------------------------------*/

/* Not all hardware capabilities are exposed through these calls; they
 * only encapsulate the most common features and modes.  (So if you
 * want to change signals in groups, do it directly.)
 *
 * Bootloaders will usually handle some of the pin multiplexing setup.
 * The intent is certainly that by the time Linux is fully booted, all
 * pins should have been fully initialized.  These setup calls should
 * only be used by board setup routines, or possibly in driver probe().
 *
 * For bootloaders doing all that setup, these calls could be inlined
 * as NOPs so Linux won't duplicate any setup code
 */


/*
 * mux the pin to the "A" internal peripheral role.
 */
int __init_or_module at91_set_A_periph(unsigned pin, int use_pullup)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (!pio)
		return -EINVAL;

	__raw_writel(mask, pio + PIO_IDR);
	__raw_writel(mask, pio + (use_pullup ? PIO_PUER : PIO_PUDR));
	__raw_writel(mask, pio + PIO_ASR);
	__raw_writel(mask, pio + PIO_PDR);
	return 0;
}
EXPORT_SYMBOL(at91_set_A_periph);


/*
 * mux the pin to the "B" internal peripheral role.
 */
int __init_or_module at91_set_B_periph(unsigned pin, int use_pullup)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (!pio)
		return -EINVAL;

	__raw_writel(mask, pio + PIO_IDR);
	__raw_writel(mask, pio + (use_pullup ? PIO_PUER : PIO_PUDR));
	__raw_writel(mask, pio + PIO_BSR);
	__raw_writel(mask, pio + PIO_PDR);
	return 0;
}
EXPORT_SYMBOL(at91_set_B_periph);


/*
 * mux the pin to the gpio controller (instead of "A" or "B" peripheral), and
 * configure it for an input.
 */
int __init_or_module at91_set_gpio_input(unsigned pin, int use_pullup)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (!pio)
		return -EINVAL;

	__raw_writel(mask, pio + PIO_IDR);
	__raw_writel(mask, pio + (use_pullup ? PIO_PUER : PIO_PUDR));
	__raw_writel(mask, pio + PIO_ODR);
	__raw_writel(mask, pio + PIO_PER);
	return 0;
}
EXPORT_SYMBOL(at91_set_gpio_input);


/*
 * mux the pin to the gpio controller (instead of "A" or "B" peripheral),
 * and configure it for an output.
 */
int __init_or_module at91_set_gpio_output(unsigned pin, int value)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (!pio)
		return -EINVAL;

	__raw_writel(mask, pio + PIO_IDR);
	__raw_writel(mask, pio + PIO_PUDR);
	__raw_writel(mask, pio + (value ? PIO_SODR : PIO_CODR));
	__raw_writel(mask, pio + PIO_OER);
	__raw_writel(mask, pio + PIO_PER);
	return 0;
}
EXPORT_SYMBOL(at91_set_gpio_output);


/*
 * enable/disable the glitch filter; mostly used with IRQ handling.
 */
int __init_or_module at91_set_deglitch(unsigned pin, int is_on)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (!pio)
		return -EINVAL;
	__raw_writel(mask, pio + (is_on ? PIO_IFER : PIO_IFDR));
	return 0;
}
EXPORT_SYMBOL(at91_set_deglitch);

/*
 * enable/disable the multi-driver; This is only valid for output and
 * allows the output pin to run as an open collector output.
 */
int __init_or_module at91_set_multi_drive(unsigned pin, int is_on)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (!pio)
		return -EINVAL;

	__raw_writel(mask, pio + (is_on ? PIO_MDER : PIO_MDDR));
	return 0;
}
EXPORT_SYMBOL(at91_set_multi_drive);

/*--------------------------------------------------------------------------*/


/*
 * assuming the pin is muxed as a gpio output, set its value.
 */
int at91_set_gpio_value(unsigned pin, int value)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (!pio)
		return -EINVAL;
	__raw_writel(mask, pio + (value ? PIO_SODR : PIO_CODR));
	return 0;
}
EXPORT_SYMBOL(at91_set_gpio_value);


/*
 * read the pin's value (works even if it's not muxed as a gpio).
 */
int at91_get_gpio_value(unsigned pin)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);
	u32		pdsr;

	if (!pio)
		return -EINVAL;
	pdsr = __raw_readl(pio + PIO_PDSR);
	return (pdsr & mask) != 0;
}
EXPORT_SYMBOL(at91_get_gpio_value);

/*--------------------------------------------------------------------------*/


/* Several AIC controller irqs are dispatched through this GPIO handler.
 * To use any AT91_PIN_* as an externally triggered IRQ, first call
 * at91_set_gpio_input() then maybe enable its glitch filter.
 * Then just request_irq() with the pin ID; it works like any ARM IRQ
 * handler, though it always triggers on rising and falling edges.
 *
 * Alternatively, certain pins may be used directly as IRQ0..IRQ6 after
 * configuring them with at91_set_a_periph() or at91_set_b_periph().
 * IRQ0..IRQ6 should be configurable, e.g. level vs edge triggering.
 */

static void gpio_irq_mask(unsigned pin)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (pio)
		__raw_writel(mask, pio + PIO_IDR);
}

static void gpio_irq_unmask(unsigned pin)
{
	void __iomem	*pio = pin_to_controller(pin);
	unsigned	mask = pin_to_mask(pin);

	if (pio)
		__raw_writel(mask, pio + PIO_IER);
}

static int gpio_irq_type(unsigned pin, unsigned type)
{
	return (type == IRQT_BOTHEDGE) ? 0 : -EINVAL;
}

static struct irqchip gpio_irqchip = {
	.mask		= gpio_irq_mask,
	.unmask		= gpio_irq_unmask,
	.set_type	= gpio_irq_type,
};

static void gpio_irq_handler(unsigned irq, struct irqdesc *desc, struct pt_regs *regs)
{
	unsigned	pin;
	struct irqdesc	*gpio;
	void __iomem	*pio;
	u32		isr;

	pio = desc->base;

	/* temporarily mask (level sensitive) parent IRQ */
	desc->chip->ack(irq);
	for (;;) {
		isr = __raw_readl(pio + PIO_ISR) & __raw_readl(pio + PIO_IMR);
		if (!isr)
			break;

		pin = (unsigned) desc->data;
		gpio = &irq_desc[pin];

		while (isr) {
			if (isr & 1) {
				if (unlikely(gpio->disable_depth)) {
					/*
					 * The core ARM interrupt handler lazily disables IRQs so
					 * another IRQ must be generated before it actually gets
					 * here to be disabled on the GPIO controller.
					 */
					gpio_irq_mask(pin);
				}
				else
					gpio->handle(pin, gpio, regs);
			}
			pin++;
			gpio++;
			isr >>= 1;
		}
	}
	desc->chip->unmask(irq);
	/* now it may re-trigger */
}

/* call this from board-specific init_irq */
void __init at91_gpio_irq_setup(unsigned banks)
{
	unsigned	pioc, pin, id;

	if (banks > 4)
		banks = 4;
	for (pioc = 0, pin = PIN_BASE, id = AT91_ID_PIOA;
			pioc < banks;
			pioc++, id++) {
		void __iomem	*controller;
		unsigned	i;

		controller = (void __iomem *) AT91_VA_BASE_SYS + pio_controller_offset[pioc];
		__raw_writel(~0, controller + PIO_IDR);

		set_irq_data(id, (void *) pin);
		set_irq_chipdata(id, controller);

		for (i = 0; i < 32; i++, pin++) {
			set_irq_chip(pin, &gpio_irqchip);
			set_irq_handler(pin, do_simple_IRQ);
			set_irq_flags(pin, IRQF_VALID);
		}

		set_irq_chained_handler(id, gpio_irq_handler);

		/* enable the PIO peripheral clock */
		at91_sys_write(AT91_PMC_PCER, 1 << id);
	}
	pr_info("AT91: %d gpio irqs in %d banks\n", pin - PIN_BASE, banks);
}
