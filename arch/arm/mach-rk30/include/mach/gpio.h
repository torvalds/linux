#ifndef __MACH_GPIO_H
#define __MACH_GPIO_H

#include <mach/irqs.h>

#define ARCH_NR_GPIOS	NR_GPIO_IRQS
#define PIN_BASE	NR_GIC_IRQS

#include <asm/errno.h>
#include <asm-generic/gpio.h>		/* cansleep wrappers */

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep

static inline int gpio_to_irq(unsigned gpio)
{
	return gpio - PIN_BASE + NR_GIC_IRQS;
}

static inline int irq_to_gpio(unsigned irq)
{
	return irq - NR_GIC_IRQS + PIN_BASE;
}

#endif
