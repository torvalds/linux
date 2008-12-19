#ifndef _AU1XXX_GPIO_H_
#define _AU1XXX_GPIO_H_

#include <linux/types.h>

#define AU1XXX_GPIO_BASE	200

struct au1x00_gpio2 {
	u32	dir;
	u32	reserved;
	u32	output;
	u32	pinstate;
	u32	inten;
	u32	enable;
};

extern int au1xxx_gpio_get_value(unsigned gpio);
extern void au1xxx_gpio_set_value(unsigned gpio, int value);
extern int au1xxx_gpio_direction_input(unsigned gpio);
extern int au1xxx_gpio_direction_output(unsigned gpio, int value);


/* Wrappers for the arch-neutral GPIO API */

static inline int gpio_request(unsigned gpio, const char *label)
{
	/* Not yet implemented */
	return 0;
}

static inline void gpio_free(unsigned gpio)
{
	/* Not yet implemented */
}

static inline int gpio_direction_input(unsigned gpio)
{
	return au1xxx_gpio_direction_input(gpio);
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	return au1xxx_gpio_direction_output(gpio, value);
}

static inline int gpio_get_value(unsigned gpio)
{
	return au1xxx_gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	au1xxx_gpio_set_value(gpio, value);
}

static inline int gpio_to_irq(unsigned gpio)
{
	return gpio;
}

static inline int irq_to_gpio(unsigned irq)
{
	return irq;
}

/* For cansleep */
#include <asm-generic/gpio.h>

#endif /* _AU1XXX_GPIO_H_ */
