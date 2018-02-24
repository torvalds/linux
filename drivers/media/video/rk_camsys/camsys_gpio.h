/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RKCAMSYS_GPIO_H__
#define __RKCAMSYS_GPIO_H__

#if defined(CONFIG_ARCH_ROCKCHIP)
#define RK30_PIN0_PA0 (0)
#define NUM_GROUP (32)
#define GPIO_BANKS (9)
#endif
extern unsigned int CHIP_TYPE;

static inline unsigned int camsys_gpio_group_pin(unsigned char *io_name)
{
	char *pin_char = NULL;
	int pin = -1;

	if (strstr(io_name, "PA")) {
		pin_char = strstr(io_name, "PA");
		pin_char += 2;
		pin = *pin_char - 0x30;
	} else if (strstr(io_name, "PB")) {
		pin_char = strstr(io_name, "PB");
		pin_char += 2;
		pin = *pin_char - 0x30;
		pin += 8;
	} else if (strstr(io_name, "PC")) {
		pin_char = strstr(io_name, "PC");
		pin_char += 2;
		pin = *pin_char - 0x30;
		pin += 16;
	} else if (strstr(io_name, "PD")) {
		pin_char = strstr(io_name, "PD");
		pin_char += 2;
		pin = *pin_char - 0x30;
		pin += 24;
	}
	return pin;
}

static inline unsigned int camsys_gpio_group(unsigned char *io_name)
{
	unsigned int group = 0;

	if (strstr(io_name, "PIN0"))
		group = 0;
	else if (strstr(io_name, "PIN1"))
		group = 1;
	else if (strstr(io_name, "PIN2"))
		group = 2;
	else if (strstr(io_name, "PIN3"))
		group = 3;
	else if (strstr(io_name, "PIN4"))
		group = 4;
	else if (strstr(io_name, "PIN5"))
		group = 5;
	else if (strstr(io_name, "PIN6"))
		group = 6;
	else if (strstr(io_name, "PIN7"))
		group = 7;
	else if (strstr(io_name, "PIN8"))
		group = 8;
	return group;
}

static inline unsigned int camsys_gpio_get(unsigned char *io_name)
{
	unsigned int gpio = 0;
	unsigned int group = 0;
	int group_pin = 0;
#if (defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188) ||\
		defined(CONFIG_ARCH_RK319X) || defined(CONFIG_ARCH_ROCKCHIP))
	if (strstr(io_name, "RK30_")) {
		gpio = RK30_PIN0_PA0;
		group = camsys_gpio_group(io_name);
		group_pin = camsys_gpio_group_pin(io_name);
		if (group_pin == -1)
			gpio = 0xffffffff;
		else {
			if (group >= GPIO_BANKS)
				gpio = 0xffffffff;
			else
				gpio += group * NUM_GROUP + group_pin;
		}
		/* gpio0_D is unavailable on rk3288. */
		if (!strstr(io_name, "PIN0") && 3288 == CHIP_TYPE)
			gpio -= 8;
	}
#endif
	return gpio;
}

#endif

