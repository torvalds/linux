#ifndef __ASM_MACH_GPIO_H
#define __ASM_MACH_GPIO_H

#include <asm-generic/gpio.h>

#define gpio_to_irq(gpio)	(IRQ_GPIO_START + (gpio))
#define irq_to_gpio(irq)	((irq) - IRQ_GPIO_START)

#define __gpio_is_inverted(gpio)	(0)
#define __gpio_is_occupied(gpio)	(0)

#include <plat/gpio.h>
#endif /* __ASM_MACH_GPIO_H */
