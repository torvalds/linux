#ifndef _IMX_GPIO_H

#include <mach/imx-regs.h>

#define IMX_GPIO_ALLOC_MODE_NORMAL	0
#define IMX_GPIO_ALLOC_MODE_NO_ALLOC	1
#define IMX_GPIO_ALLOC_MODE_TRY_ALLOC	2
#define IMX_GPIO_ALLOC_MODE_ALLOC_ONLY	4
#define IMX_GPIO_ALLOC_MODE_RELEASE	8

extern int imx_gpio_request(unsigned gpio, const char *label);

extern void imx_gpio_free(unsigned gpio);

extern int imx_gpio_setup_multiple_pins(const int *pin_list, unsigned count,
					int alloc_mode, const char *label);

extern int imx_gpio_direction_input(unsigned gpio);

extern int imx_gpio_direction_output(unsigned gpio, int value);

extern void __imx_gpio_set_value(unsigned gpio, int value);

static inline int imx_gpio_get_value(unsigned gpio)
{
	return SSR(gpio >> GPIO_PORT_SHIFT) & (1 << (gpio & GPIO_PIN_MASK));
}

static inline void imx_gpio_set_value_inline(unsigned gpio, int value)
{
	unsigned long flags;

	raw_local_irq_save(flags);
	if(value)
		DR(gpio >> GPIO_PORT_SHIFT) |= (1 << (gpio & GPIO_PIN_MASK));
	else
		DR(gpio >> GPIO_PORT_SHIFT) &= ~(1 << (gpio & GPIO_PIN_MASK));
	raw_local_irq_restore(flags);
}

static inline void imx_gpio_set_value(unsigned gpio, int value)
{
	if(__builtin_constant_p(gpio))
		imx_gpio_set_value_inline(gpio, value);
	else
		__imx_gpio_set_value(gpio, value);
}

extern int imx_gpio_to_irq(unsigned gpio);

extern int imx_irq_to_gpio(unsigned irq);

/*-------------------------------------------------------------------------*/

/* Wrappers for "new style" GPIO calls. These calls i.MX specific versions
 * to allow future extension of GPIO logic.
 */

static inline int gpio_request(unsigned gpio, const char *label)
{
	return imx_gpio_request(gpio, label);
}

static inline void gpio_free(unsigned gpio)
{
	imx_gpio_free(gpio);
}

static inline  int gpio_direction_input(unsigned gpio)
{
	return imx_gpio_direction_input(gpio);
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	return imx_gpio_direction_output(gpio, value);
}

static inline int gpio_get_value(unsigned gpio)
{
	return imx_gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	imx_gpio_set_value(gpio, value);
}

#include <asm-generic/gpio.h>		/* cansleep wrappers */

static inline int gpio_to_irq(unsigned gpio)
{
	return imx_gpio_to_irq(gpio);
}

static inline int irq_to_gpio(unsigned irq)
{
	return imx_irq_to_gpio(irq);
}


#endif
