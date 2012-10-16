#ifndef __ASM_ARCH_MACH_GPIO_H
#define __ASM_ARCH_MACH_GPIO_H

#include <asm-generic/gpio.h>

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define gpio_to_irq	__gpio_to_irq

#endif /* __ASM_ARCH_MACH_GPIO_H */
