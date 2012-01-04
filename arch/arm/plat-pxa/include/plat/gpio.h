#ifndef __PLAT_GPIO_H
#define __PLAT_GPIO_H

#define __ARM_GPIOLIB_COMPLEX

/* The individual machine provides register offsets and NR_BUILTIN_GPIO */
#include <mach/gpio-pxa.h>

static inline int gpio_get_value(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && (gpio < NR_BUILTIN_GPIO))
		return GPLR(gpio) & GPIO_bit(gpio);
	else
		return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(gpio) && (gpio < NR_BUILTIN_GPIO)) {
		if (value)
			GPSR(gpio) = GPIO_bit(gpio);
		else
			GPCR(gpio) = GPIO_bit(gpio);
	} else
		__gpio_set_value(gpio, value);
}

#define gpio_cansleep		__gpio_cansleep

#endif /* __PLAT_GPIO_H */
