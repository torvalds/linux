#ifndef __ASM_MIPS_MACH_LANTIQ_GPIO_H
#define __ASM_MIPS_MACH_LANTIQ_GPIO_H

static inline int gpio_to_irq(unsigned int gpio)
{
	return -1;
}

#define gpio_get_value __gpio_get_value
#define gpio_set_value __gpio_set_value

#define gpio_cansleep __gpio_cansleep

#include <asm-generic/gpio.h>

#endif
