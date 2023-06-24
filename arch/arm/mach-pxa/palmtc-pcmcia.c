// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/pcmcia/pxa2xx_palmtc.c
 *
 * Driver for Palm Tungsten|C PCMCIA
 *
 * Copyright (C) 2008 Alex Osborne <ato@meshy.org>
 * Copyright (C) 2009-2011 Marek Vasut <marek.vasut@gmail.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <asm/mach-types.h>
#include "palmtc.h"
#include <pcmcia/soc_common.h>

static struct gpio palmtc_pcmcia_gpios[] = {
	{ GPIO_NR_PALMTC_PCMCIA_POWER1,	GPIOF_INIT_LOW,	"PCMCIA Power 1" },
	{ GPIO_NR_PALMTC_PCMCIA_POWER2,	GPIOF_INIT_LOW,	"PCMCIA Power 2" },
	{ GPIO_NR_PALMTC_PCMCIA_POWER3,	GPIOF_INIT_LOW,	"PCMCIA Power 3" },
	{ GPIO_NR_PALMTC_PCMCIA_RESET,	GPIOF_INIT_HIGH,"PCMCIA Reset" },
	{ GPIO_NR_PALMTC_PCMCIA_PWRREADY, GPIOF_IN,	"PCMCIA Power Ready" },
};

static int palmtc_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	ret = gpio_request_array(palmtc_pcmcia_gpios,
				ARRAY_SIZE(palmtc_pcmcia_gpios));

	skt->stat[SOC_STAT_RDY].gpio = GPIO_NR_PALMTC_PCMCIA_READY;
	skt->stat[SOC_STAT_RDY].name = "PCMCIA Ready";

	return ret;
}

static void palmtc_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free_array(palmtc_pcmcia_gpios, ARRAY_SIZE(palmtc_pcmcia_gpios));
}

static void palmtc_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
					struct pcmcia_state *state)
{
	state->detect = 1; /* always inserted */
	state->vs_3v  = 1;
	state->vs_Xv  = 0;
}

static int palmtc_wifi_powerdown(void)
{
	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_RESET, 1);
	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_POWER2, 0);
	mdelay(40);
	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_POWER1, 0);
	return 0;
}

static int palmtc_wifi_powerup(void)
{
	int timeout = 50;

	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_POWER3, 1);
	mdelay(50);

	/* Power up the card, 1.8V first, after a while 3.3V */
	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_POWER1, 1);
	mdelay(100);
	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_POWER2, 1);

	/* Wait till the card is ready */
	while (!gpio_get_value(GPIO_NR_PALMTC_PCMCIA_PWRREADY) &&
		timeout) {
		mdelay(1);
		timeout--;
	}

	/* Power down the WiFi in case of error */
	if (!timeout) {
		palmtc_wifi_powerdown();
		return 1;
	}

	/* Reset the card */
	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_RESET, 1);
	mdelay(20);
	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_RESET, 0);
	mdelay(25);

	gpio_set_value(GPIO_NR_PALMTC_PCMCIA_POWER3, 0);

	return 0;
}

static int palmtc_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
					const socket_state_t *state)
{
	int ret = 1;

	if (state->Vcc == 0)
		ret = palmtc_wifi_powerdown();
	else if (state->Vcc == 33)
		ret = palmtc_wifi_powerup();

	return ret;
}

static struct pcmcia_low_level palmtc_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.first			= 0,
	.nr			= 1,

	.hw_init		= palmtc_pcmcia_hw_init,
	.hw_shutdown		= palmtc_pcmcia_hw_shutdown,

	.socket_state		= palmtc_pcmcia_socket_state,
	.configure_socket	= palmtc_pcmcia_configure_socket,
};

static struct platform_device *palmtc_pcmcia_device;

static int __init palmtc_pcmcia_init(void)
{
	int ret;

	if (!machine_is_palmtc())
		return -ENODEV;

	palmtc_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!palmtc_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(palmtc_pcmcia_device, &palmtc_pcmcia_ops,
					sizeof(palmtc_pcmcia_ops));

	if (!ret)
		ret = platform_device_add(palmtc_pcmcia_device);

	if (ret)
		platform_device_put(palmtc_pcmcia_device);

	return ret;
}

static void __exit palmtc_pcmcia_exit(void)
{
	platform_device_unregister(palmtc_pcmcia_device);
}

module_init(palmtc_pcmcia_init);
module_exit(palmtc_pcmcia_exit);

MODULE_AUTHOR("Alex Osborne <ato@meshy.org>,"
	    " Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("PCMCIA support for Palm Tungsten|C");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
MODULE_LICENSE("GPL");
