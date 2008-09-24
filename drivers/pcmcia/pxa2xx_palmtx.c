/*
 * linux/drivers/pcmcia/pxa2xx_palmtx.c
 *
 * Driver for Palm T|X PCMCIA
 *
 * Copyright (C) 2007-2008 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>

#include <mach/gpio.h>
#include <mach/palmtx.h>

#include "soc_common.h"

static int palmtx_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	ret = gpio_request(GPIO_NR_PALMTX_PCMCIA_POWER1, "PCMCIA PWR1");
	if (ret)
		goto err1;
	ret = gpio_direction_output(GPIO_NR_PALMTX_PCMCIA_POWER1, 0);
	if (ret)
		goto err2;

	ret = gpio_request(GPIO_NR_PALMTX_PCMCIA_POWER2, "PCMCIA PWR2");
	if (ret)
		goto err2;
	ret = gpio_direction_output(GPIO_NR_PALMTX_PCMCIA_POWER2, 0);
	if (ret)
		goto err3;

	ret = gpio_request(GPIO_NR_PALMTX_PCMCIA_RESET, "PCMCIA RST");
	if (ret)
		goto err3;
	ret = gpio_direction_output(GPIO_NR_PALMTX_PCMCIA_RESET, 1);
	if (ret)
		goto err4;

	ret = gpio_request(GPIO_NR_PALMTX_PCMCIA_READY, "PCMCIA RDY");
	if (ret)
		goto err4;
	ret = gpio_direction_input(GPIO_NR_PALMTX_PCMCIA_READY);
	if (ret)
		goto err5;

	skt->irq = gpio_to_irq(GPIO_NR_PALMTX_PCMCIA_READY);
	return 0;

err5:
	gpio_free(GPIO_NR_PALMTX_PCMCIA_READY);
err4:
	gpio_free(GPIO_NR_PALMTX_PCMCIA_RESET);
err3:
	gpio_free(GPIO_NR_PALMTX_PCMCIA_POWER2);
err2:
	gpio_free(GPIO_NR_PALMTX_PCMCIA_POWER1);
err1:
	return ret;
}

static void palmtx_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free(GPIO_NR_PALMTX_PCMCIA_READY);
	gpio_free(GPIO_NR_PALMTX_PCMCIA_RESET);
	gpio_free(GPIO_NR_PALMTX_PCMCIA_POWER2);
	gpio_free(GPIO_NR_PALMTX_PCMCIA_POWER1);
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

static void palmtx_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void palmtx_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}

static struct pcmcia_low_level palmtx_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.first			= 0,
	.nr			= 1,

	.hw_init		= palmtx_pcmcia_hw_init,
	.hw_shutdown		= palmtx_pcmcia_hw_shutdown,

	.socket_state		= palmtx_pcmcia_socket_state,
	.configure_socket	= palmtx_pcmcia_configure_socket,

	.socket_init		= palmtx_pcmcia_socket_init,
	.socket_suspend		= palmtx_pcmcia_socket_suspend,
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
