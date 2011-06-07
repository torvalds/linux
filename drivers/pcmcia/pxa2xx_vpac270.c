/*
 * linux/drivers/pcmcia/pxa2xx_vpac270.c
 *
 * Driver for Voipac PXA270 PCMCIA and CF sockets
 *
 * Copyright (C) 2010-2011 Marek Vasut <marek.vasut@gmail.com>
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
#include <mach/vpac270.h>

#include "soc_common.h"

static struct gpio vpac270_pcmcia_gpios[] = {
	{ GPIO84_VPAC270_PCMCIA_CD,	GPIOF_IN,	"PCMCIA Card Detect" },
	{ GPIO35_VPAC270_PCMCIA_RDY,	GPIOF_IN,	"PCMCIA Ready" },
	{ GPIO107_VPAC270_PCMCIA_PPEN,	GPIOF_INIT_LOW,	"PCMCIA PPEN" },
	{ GPIO11_VPAC270_PCMCIA_RESET,	GPIOF_INIT_LOW,	"PCMCIA Reset" },
};

static struct gpio vpac270_cf_gpios[] = {
	{ GPIO17_VPAC270_CF_CD,		GPIOF_IN,	"CF Card Detect" },
	{ GPIO12_VPAC270_CF_RDY,	GPIOF_IN,	"CF Ready" },
	{ GPIO16_VPAC270_CF_RESET,	GPIOF_INIT_LOW,	"CF Reset" },
};

static struct pcmcia_irqs cd_irqs[] = {
	{
		.sock = 0,
		.irq  = IRQ_GPIO(GPIO84_VPAC270_PCMCIA_CD),
		.str  = "PCMCIA CD"
	},
	{
		.sock = 1,
		.irq  = IRQ_GPIO(GPIO17_VPAC270_CF_CD),
		.str  = "CF CD"
	},
};

static int vpac270_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	if (skt->nr == 0) {
		ret = gpio_request_array(vpac270_pcmcia_gpios,
				ARRAY_SIZE(vpac270_pcmcia_gpios));

		skt->socket.pci_irq = gpio_to_irq(GPIO35_VPAC270_PCMCIA_RDY);

		if (!ret)
			ret = soc_pcmcia_request_irqs(skt, &cd_irqs[0], 1);
	} else {
		ret = gpio_request_array(vpac270_cf_gpios,
				ARRAY_SIZE(vpac270_cf_gpios));

		skt->socket.pci_irq = gpio_to_irq(GPIO12_VPAC270_CF_RDY);

		if (!ret)
			ret = soc_pcmcia_request_irqs(skt, &cd_irqs[1], 1);
	}

	return ret;
}

static void vpac270_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	if (skt->nr == 0)
		gpio_request_array(vpac270_pcmcia_gpios,
					ARRAY_SIZE(vpac270_pcmcia_gpios));
	else
		gpio_request_array(vpac270_cf_gpios,
					ARRAY_SIZE(vpac270_cf_gpios));
}

static void vpac270_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
					struct pcmcia_state *state)
{
	if (skt->nr == 0) {
		state->detect = !gpio_get_value(GPIO84_VPAC270_PCMCIA_CD);
		state->ready  = !!gpio_get_value(GPIO35_VPAC270_PCMCIA_RDY);
	} else {
		state->detect = !gpio_get_value(GPIO17_VPAC270_CF_CD);
		state->ready  = !!gpio_get_value(GPIO12_VPAC270_CF_RDY);
	}
	state->bvd1   = 1;
	state->bvd2   = 1;
	state->wrprot = 0;
	state->vs_3v  = 1;
	state->vs_Xv  = 0;
}

static int
vpac270_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				const socket_state_t *state)
{
	if (skt->nr == 0) {
		gpio_set_value(GPIO11_VPAC270_PCMCIA_RESET,
			(state->flags & SS_RESET));
		gpio_set_value(GPIO107_VPAC270_PCMCIA_PPEN,
			!(state->Vcc == 33 || state->Vcc == 50));
	} else {
		gpio_set_value(GPIO16_VPAC270_CF_RESET,
			(state->flags & SS_RESET));
	}

	return 0;
}

static void vpac270_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void vpac270_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}

static struct pcmcia_low_level vpac270_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.first			= 0,
	.nr			= 2,

	.hw_init		= vpac270_pcmcia_hw_init,
	.hw_shutdown		= vpac270_pcmcia_hw_shutdown,

	.socket_state		= vpac270_pcmcia_socket_state,
	.configure_socket	= vpac270_pcmcia_configure_socket,

	.socket_init		= vpac270_pcmcia_socket_init,
	.socket_suspend		= vpac270_pcmcia_socket_suspend,
};

static struct platform_device *vpac270_pcmcia_device;

static int __init vpac270_pcmcia_init(void)
{
	int ret;

	if (!machine_is_vpac270())
		return -ENODEV;

	vpac270_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!vpac270_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(vpac270_pcmcia_device,
		&vpac270_pcmcia_ops, sizeof(vpac270_pcmcia_ops));

	if (!ret)
		ret = platform_device_add(vpac270_pcmcia_device);

	if (ret)
		platform_device_put(vpac270_pcmcia_device);

	return ret;
}

static void __exit vpac270_pcmcia_exit(void)
{
	platform_device_unregister(vpac270_pcmcia_device);
}

module_init(vpac270_pcmcia_init);
module_exit(vpac270_pcmcia_exit);

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("PCMCIA support for Voipac PXA270");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
MODULE_LICENSE("GPL");
