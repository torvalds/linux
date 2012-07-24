#ifndef __MACH_GPIO_H
#define __MACH_GPIO_H

/* Pull up/down values */
enum sirfsoc_gpio_pull {
	SIRFSOC_GPIO_PULL_NONE,
	SIRFSOC_GPIO_PULL_UP,
	SIRFSOC_GPIO_PULL_DOWN,
};

void sirfsoc_gpio_set_pull(unsigned gpio, unsigned mode);

#endif
