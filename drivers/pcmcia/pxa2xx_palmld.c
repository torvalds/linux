/*
 * linux/drivers/pcmcia/pxa2xx_palmld.c
 *
 * Driver for Palm LifeDrive PCMCIA
 *
 * Copyright (C) 2006 Alex Osborne <ato@meshy.org>
 * Copyright (C) 2007-2008 Marek Vasut <marek.vasut@gmail.com>
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
#include <mach/palmld.h>
#include "soc_common.h"

static int palmld_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	ret = gpio_request(GPIO_NR_PALMLD_PCMCIA_POWER, "PCMCIA PWR");
	if (ret)
		goto err1;
	ret = gpio_direction_output(GPIO_NR_PALMLD_PCMCIA_POWER, 0);
	if (ret)
		goto err2;

	ret = gpio_request(GPIO_NR_PALMLD_PCMCIA_RESET, "PCMCIA RST");
	if (ret)
		goto err2;
	ret = gpio_direction_output(GPIO_NR_PALMLD_PCMCIA_RESET, 1);
	if (ret)
		goto err3;

	ret = gpio_request(GPIO_NR_PALMLD_PCMCIA_READY, "PCMCIA RDY");
	if (ret)
		goto err3;
	ret = gpio_direction_input(GPIO_NR_PALMLD_PCMCIA_READY);
	if (ret)
		goto err4;

	skt->irq = IRQ_GPIO(GPIO_NR_PALMLD_PCMCIA_READY);
	return 0;

err4:
	gpio_free(GPIO_NR_PALMLD_PCMCIA_READY);
err3:
	gpio_free(GPIO_NR_PALMLD_PCMCIA_RESET);
err2:
	gpio_free(GPIO_NR_PALMLD_PCMCIA_POWER);
err1:
	return ret;
}

static void palmld_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free(GPIO_NR_PALMLD_PCMCIA_READY);
	gpio_free(GPIO_NR_PALMLD_PCMCIA_RESET);
	gpio_free(GPIO_NR_PALMLD_PCMCIA_POWER);
}

static void palmld_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
					struct pcmcia_state *state)
{
	state->detect = 1; /* always inserted */
	state->ready  = !!gpio_get_value(GPIO_NR_PALMLD_PCMCIA_READY);
	state->bvd1   = 1;
	state->bvd2   = 1;
	state->wrprot = 0;
	state->vs_3v  = 1;
	state->vs_Xv  = 0;
}

static int palmld_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
					const socket_state_t *state)
{
	gpio_set_value(GPIO_NR_PALMLD_PCMCIA_POWER, 1);
	gpio_set_value(GPIO_NR_PALMLD_PCMCIA_RESET,
			!!(state->flags & SS_RESET));

	return 0;
}

static void palmld_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void palmld_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}

static struct pcmcia_low_level palmld_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.first			= 1,
	.nr			= 1,

	.hw_init		= palmld_pcmcia_hw_init,
	.hw_shutdown		= palmld_pcmcia_hw_shutdown,

	.socket_state		= palmld_pcmcia_socket_state,
	.configure_socket	= palmld_pcmcia_configure_socket,

	.socket_init		= palmld_pcmcia_socket_init,
	.socket_suspend		= palmld_pcmcia_socket_suspend,
};

static struct platform_device *palmld_pcmcia_device;

static int __init palmld_pcmcia_init(void)
{
	int ret;

	if (!machine_is_palmld())
		return -ENODEV;

	palmld_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!palmld_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(palmld_pcmcia_device, &palmld_pcmcia_ops,
					sizeof(palmld_pcmcia_ops));

	if (!ret)
		ret = platform_device_add(palmld_pcmcia_device);

	if (ret)
		platform_device_put(palmld_pcmcia_device);

	return ret;
}

static void __exit palmld_pcmcia_exit(void)
{
	platform_device_unregister(palmld_pcmcia_device);
}

module_init(palmld_pcmcia_init);
module_exit(palmld_pcmcia_exit);

MODULE_AUTHOR("Alex Osborne <ato@meshy.org>,"
	    " Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("PCMCIA support for Palm LifeDrive");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
MODULE_LICENSE("GPL");
