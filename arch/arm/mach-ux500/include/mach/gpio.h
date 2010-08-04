#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H

/*
 * 288 (#267 is the highest one actually hooked up) onchip GPIOs, plus enough
 * room for a couple of GPIO expanders.
 */
#define ARCH_NR_GPIOS	350

#include <plat/gpio.h>

#define __GPIO_RESOURCE(soc, block)					\
	{								\
		.start	= soc##_GPIOBANK##block##_BASE,			\
		.end	= soc##_GPIOBANK##block##_BASE + 127,		\
		.flags	= IORESOURCE_MEM,				\
	},								\
	{								\
		.start	= IRQ_GPIO##block,				\
		.end	= IRQ_GPIO##block,				\
		.flags	= IORESOURCE_IRQ,				\
	}

#define __GPIO_DEVICE(soc, block)					\
	{								\
		.name		= "gpio",				\
		.id		= block,				\
		.num_resources	= 2,					\
		.resource	= &soc##_gpio_resources[block * 2],	\
		.dev = {						\
			.platform_data = &soc##_gpio_data[block],	\
		},							\
	}

#define GPIO_DATA(_name, first)						\
	{								\
		.name		= _name,				\
		.first_gpio	= first,				\
		.first_irq	= NOMADIK_GPIO_TO_IRQ(first),		\
	}

#ifdef CONFIG_UX500_SOC_DB8500
#define GPIO_RESOURCE(block)	__GPIO_RESOURCE(U8500, block)
#define GPIO_DEVICE(block)	__GPIO_DEVICE(u8500, block)
#elif defined(CONFIG_UX500_SOC_DB5500)
#define GPIO_RESOURCE(block)	__GPIO_RESOURCE(U5500, block)
#define GPIO_DEVICE(block)	__GPIO_DEVICE(u5500, block)
#endif

#endif /* __ASM_ARCH_GPIO_H */
