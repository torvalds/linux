/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MAX1600_H
#define MAX1600_H

struct gpio_desc;

enum {
	MAX1600_GPIO_0VCC = 0,
	MAX1600_GPIO_1VCC,
	MAX1600_GPIO_0VPP,
	MAX1600_GPIO_1VPP,
	MAX1600_GPIO_MAX,

	MAX1600_CHAN_A,
	MAX1600_CHAN_B,

	MAX1600_CODE_LOW,
	MAX1600_CODE_HIGH,
};

struct max1600 {
	struct gpio_desc *gpio[MAX1600_GPIO_MAX];
	struct device *dev;
	unsigned int code;
};

int max1600_init(struct device *dev, struct max1600 **ptr,
	unsigned int channel, unsigned int code);

int max1600_configure(struct max1600 *, unsigned int vcc, unsigned int vpp);

#endif
