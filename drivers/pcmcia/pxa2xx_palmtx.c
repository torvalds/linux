/*
 * linux/drivers/pcmcia/pxa2xx_palmtx.c
 *
 * Driver for Palm T|X PCMCIA
 *
 * Copyright (C) 2007-2011 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <mach/palmtx.h>
#include "soc_common.h"

static struct gpio palmtx_pcmcia_gpios[] = {
	{ GPIO_NR_PALMTX_PCMCIA_POWER1,	GPIOF_INIT_LOW,	"PCMCIA Power 1" },
	{ GPIO_NR_PALMTX_PCMCIA_POWER2,	GPIOF_INIT_LOW,	"PCMCIA Power 2" },
	{ GPIO_NR_PALMTX_PCMCIA_RESET,	GPIOF_INIT_HIGH,"PCMCIA Reset" },
	{ GPIO_NR_PALMTX_PCMCIA_READY,	GPIOF_IN,	"PCMCIA Ready" },
};

static int palmtx_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	ret = gpio_request_array(palmtx_pcmcia_gpios,
				ARRAY_SIZE(palmtx_pcmcia_gpios));

	skt->socket.pci_irq = gpio_to_irq(GPIO_NR_PALMTX_PCMCIA_READY);

	return ret;
}

static void palmtx_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free_array(palmtx_pcmcia_gpios, ARRAY_SIZE(palmtx_pcmcia_gpios));
}

static void palmtx_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
					struct pcmcia_state *state)
{
	state->detect = 1; /* always inserted */
	state->ready  = !!gpio_get_value(GPIO_NR_PALMTX_PCMCIA_READY);
	state->bvd1   = 1;
	state->bvd2   = 1;
	state->wrprot = 0;
	state->vs_3v  = 1;
	state->vs_Xv  = 0;
}

static int
palmtx_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				const socket_state_t *state)
{
	gpio_set_value(GPIO_NR_PALMTX_PCMCIA_POWER1, 1);
	gpio_set_value(GPIO_NR_PALMTX_PCMCIA_POWER2, 1);
	gpio_set_value(GPIO_NR_PALMTX_PCMCIA_RESET,
			!!(state->flags & SS_RESET));

	return 0;
}

static struct pcmcia_low_level palmtx_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.first			= 0,
	.nr			= 1,

	.hw_init		= palmtx_pcmcia_hw_init,
	.hw_shutdown		= palmtx_pcmcia_hw_shutdown,

	.socket_state		= palmtx_pcmcia_socket_state,
	.configure_socket	= palmtx_pcmcia_configure_socket,
};

static struct platform_device *palmtx_pcmcia_device;

static int __init palmtx_pcmcia_init(void)
{
	int ret;

	if (!machine_is_palmtx())
		return -ENODEV;

	palmtx_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!palmtx_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(palmtx_pcmcia_device, &palmtx_pcmcia_ops,
					sizeof(palmtx_pcmcia_ops));

	if (!ret)
		ret = platform_device_add(palmtx_pcmcia_device);

	if (ret)
		platform_device_put(palmtx_pcmcia_device);

	return ret;
}

static void __exit palmtx_pcmcia_exit(void)
{
	platform_device_unregister(palmtx_pcmcia_device);
}

module_init(palmtx_pcmcia_init);
module_exit(palmtx_pcmcia_exit);

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("PCMCIA support for Palm T|X");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
MODULE_LICENSE("GPL");
