#ifndef _AU1XXX_GPIO_H_
#define _AU1XXX_GPIO_H_

#include <linux/types.h>

#define AU1XXX_GPIO_BASE	200

/* GPIO bank 1 offsets */
#define AU1000_GPIO1_TRI_OUT	0x0100
#define AU1000_GPIO1_OUT	0x0108
#define AU1000_GPIO1_ST		0x0110
#define AU1000_GPIO1_CLR	0x010C

/* GPIO bank 2 offsets */
#define AU1000_GPIO2_DIR	0x00
#define AU1000_GPIO2_RSVD	0x04
#define AU1000_GPIO2_OUT	0x08
#define AU1000_GPIO2_ST		0x0C
#define AU1000_GPIO2_INT	0x10
#define AU1000_GPIO2_EN		0x14

#define GPIO2_OUT_EN_MASK	0x00010000

#define gpio_to_irq(gpio)	NULL

#define gpio_get_value __gpio_get_value
#define gpio_set_value __gpio_set_value

#define gpio_cansleep __gpio_cansleep

#include <asm-generic/gpio.h>

#endif /* _AU1XXX_GPIO_H_ */
