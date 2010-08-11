/*
 * linux/drivers/pcmcia/pxa2xx_colibri.c
 *
 * Driver for Toradex Colibri PXA270 CF socket
 *
 * Copyright (C) 2010 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>

#include "soc_common.h"

#define	COLIBRI270_RESET_GPIO	53
#define	COLIBRI270_PPEN_GPIO	107
#define	COLIBRI270_BVD1_GPIO	83
#define	COLIBRI270_BVD2_GPIO	82
#define	COLIBRI270_DETECT_GPIO	84
#define	COLIBRI270_READY_GPIO	1

#define	COLIBRI320_RESET_GPIO	77
#define	COLIBRI320_PPEN_GPIO	57
#define	COLIBRI320_BVD1_GPIO	53
#define	COLIBRI320_BVD2_GPIO	79
#define	COLIBRI320_DETECT_GPIO	81
#define	COLIBRI320_READY_GPIO	29

static struct {
	int	reset_gpio;
	int	ppen_gpio;
	int	bvd1_gpio;
	int	bvd2_gpio;
	int	detect_gpio;
	int	ready_gpio;
} colibri_pcmcia_gpio;

static struct pcmcia_irqs colibri_irqs[] = {
	{
		.sock = 0,
		.str  = "PCMCIA CD"
	},
};

static int colibri_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	ret = gpio_request(colibri_pcmcia_gpio.detect_gpio, "DETECT");
	if (ret)
		goto err1;
	ret = gpio_direction_input(colibri_pcmcia_gpio.detect_gpio);
	if (ret)
		goto err2;

	ret = gpio_request(colibri_pcmcia_gpio.ready_gpio, "READY");
	if (ret)
		goto err2;
	ret = gpio_direction_input(colibri_pcmcia_gpio.ready_gpio);
	if (ret)
		goto err3;

	ret = gpio_request(colibri_pcmcia_gpio.bvd1_gpio, "BVD1");
	if (ret)
		goto err3;
	ret = gpio_direction_input(colibri_pcmcia_gpio.bvd1_gpio);
	if (ret)
		goto err4;

	ret = gpio_request(colibri_pcmcia_gpio.bvd2_gpio, "BVD2");
	if (ret)
		goto err4;
	ret = gpio_direction_input(colibri_pcmcia_gpio.bvd2_gpio);
	if (ret)
		goto err5;

	ret = gpio_request(colibri_pcmcia_gpio.ppen_gpio, "PPEN");
	if (ret)
		goto err5;
	ret = gpio_direction_output(colibri_pcmcia_gpio.ppen_gpio, 0);
	if (ret)
		goto err6;

	ret = gpio_request(colibri_pcmcia_gpio.reset_gpio, "RESET");
	if (ret)
		goto err6;
	ret = gpio_direction_output(colibri_pcmcia_gpio.reset_gpio, 1);
	if (ret)
		goto err7;

	colibri_irqs[0].irq = gpio_to_irq(colibri_pcmcia_gpio.detect_gpio);
	skt->socket.pci_irq = gpio_to_irq(colibri_pcmcia_gpio.ready_gpio);

	return soc_pcmcia_request_irqs(skt, colibri_irqs,
					ARRAY_SIZE(colibri_irqs));

err7:
	gpio_free(colibri_pcmcia_gpio.detect_gpio);
err6:
	gpio_free(colibri_pcmcia_gpio.ready_gpio);
err5:
	gpio_free(colibri_pcmcia_gpio.bvd1_gpio);
err4:
	gpio_free(colibri_pcmcia_gpio.bvd2_gpio);
err3:
	gpio_free(colibri_pcmcia_gpio.reset_gpio);
err2:
	gpio_free(colibri_pcmcia_gpio.ppen_gpio);
err1:
	return ret;
}

static void colibri_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free(colibri_pcmcia_gpio.detect_gpio);
	gpio_free(colibri_pcmcia_gpio.ready_gpio);
	gpio_free(colibri_pcmcia_gpio.bvd1_gpio);
	gpio_free(colibri_pcmcia_gpio.bvd2_gpio);
	gpio_free(colibri_pcmcia_gpio.reset_gpio);
	gpio_free(colibri_pcmcia_gpio.ppen_gpio);
}

static void colibri_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
					struct pcmcia_state *state)
{

	state->detect = !!gpio_get_value(colibri_pcmcia_gpio.detect_gpio);
	state->ready  = !!gpio_get_value(colibri_pcmcia_gpio.ready_gpio);
	state->bvd1   = !!gpio_get_value(colibri_pcmcia_gpio.bvd1_gpio);
	state->bvd2   = !!gpio_get_value(colibri_pcmcia_gpio.bvd2_gpio);
	state->wrprot = 0;
	state->vs_3v  = 1;
	state->vs_Xv  = 0;
}

static int
colibri_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				const socket_state_t *state)
{
	gpio_set_value(colibri_pcmcia_gpio.ppen_gpio,
			!(state->Vcc == 33 && state->Vpp < 50));
	gpio_set_value(colibri_pcmcia_gpio.reset_gpio, state->flags & SS_RESET);
	return 0;
}

static void colibri_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void colibri_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}

static struct pcmcia_low_level colibri_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.first			= 0,
	.nr			= 1,

	.hw_init		= colibri_pcmcia_hw_init,
	.hw_shutdown		= colibri_pcmcia_hw_shutdown,

	.socket_state		= colibri_pcmcia_socket_state,
	.configure_socket	= colibri_pcmcia_configure_socket,

	.socket_init		= colibri_pcmcia_socket_init,
	.socket_suspend		= colibri_pcmcia_socket_suspend,
};

static struct platform_device *colibri_pcmcia_device;

static int __init colibri_pcmcia_init(void)
{
	int ret;

	colibri_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!colibri_pcmcia_device)
		return -ENOMEM;

	/* Colibri PXA270 */
	if (machine_is_colibri()) {
		colibri_pcmcia_gpio.reset_gpio	= COLIBRI270_RESET_GPIO;
		colibri_pcmcia_gpio.ppen_gpio	= COLIBRI270_PPEN_GPIO;
		colibri_pcmcia_gpio.bvd1_gpio	= COLIBRI270_BVD1_GPIO;
		colibri_pcmcia_gpio.bvd2_gpio	= COLIBRI270_BVD2_GPIO;
		colibri_pcmcia_gpio.detect_gpio	= COLIBRI270_DETECT_GPIO;
		colibri_pcmcia_gpio.ready_gpio	= COLIBRI270_READY_GPIO;
	/* Colibri PXA320 */
	} else if (machine_is_colibri320()) {
		colibri_pcmcia_gpio.reset_gpio	= COLIBRI320_RESET_GPIO;
		colibri_pcmcia_gpio.ppen_gpio	= COLIBRI320_PPEN_GPIO;
		colibri_pcmcia_gpio.bvd1_gpio	= COLIBRI320_BVD1_GPIO;
		colibri_pcmcia_gpio.bvd2_gpio	= COLIBRI320_BVD2_GPIO;
		colibri_pcmcia_gpio.detect_gpio	= COLIBRI320_DETECT_GPIO;
		colibri_pcmcia_gpio.ready_gpio	= COLIBRI320_READY_GPIO;
	}

	ret = platform_device_add_data(colibri_pcmcia_device,
		&colibri_pcmcia_ops, sizeof(colibri_pcmcia_ops));

	if (!ret)
		ret = platform_device_add(colibri_pcmcia_device);

	if (ret)
		platform_device_put(colibri_pcmcia_device);

	return ret;
}

static void __exit colibri_pcmcia_exit(void)
{
	platform_device_unregister(colibri_pcmcia_device);
}

module_init(colibri_pcmcia_init);
module_exit(colibri_pcmcia_exit);

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("PCMCIA support for Toradex Colibri PXA270/PXA320");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
MODULE_LICENSE("GPL");
