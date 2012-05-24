/*
 * drivers/pcmcia/sa1100_nanoengine.c
 *
 * PCMCIA implementation routines for BSI nanoEngine.
 *
 * In order to have a fully functional pcmcia subsystem in a BSE nanoEngine
 * board you should carefully read this:
 * http://cambuca.ldhs.cetuc.puc-rio.br/nanoengine/
 *
 * Copyright (C) 2010 Marcelo Roberto Jimenez <mroberto@cpti.cetuc.puc-rio.br>
 *
 * Based on original work for kernel 2.4 by
 * Miguel Freitas <miguel@cpti.cetuc.puc-rio.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/signal.h>

#include <asm/mach-types.h>
#include <asm/irq.h>

#include <mach/hardware.h>
#include <mach/nanoengine.h>

#include "sa1100_generic.h"

struct nanoengine_pins {
	unsigned output_pins;
	unsigned clear_outputs;
	int gpio_rst;
	int gpio_cd;
	int gpio_rdy;
};

static struct nanoengine_pins nano_skts[] = {
	{
		.gpio_rst		= GPIO_PC_RESET0,
		.gpio_cd		= GPIO_PC_CD0,
		.gpio_rdy		= GPIO_PC_READY0,
	}, {
		.gpio_rst		= GPIO_PC_RESET1,
		.gpio_cd		= GPIO_PC_CD1,
		.gpio_rdy		= GPIO_PC_READY1,
	}
};

unsigned num_nano_pcmcia_sockets = ARRAY_SIZE(nano_skts);

static int nanoengine_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	unsigned i = skt->nr;
	int ret;

	if (i >= num_nano_pcmcia_sockets)
		return -ENXIO;

	ret = gpio_request_one(nano_skts[i].gpio_rst, GPIOF_OUT_INIT_LOW,
		i ? "PC RST1" : "PC RST0");
	if (ret)
		return ret;

	skt->stat[SOC_STAT_CD].gpio = nano_skts[i].gpio_cd;
	skt->stat[SOC_STAT_CD].name = i ? "PC CD1" : "PC CD0";
	skt->stat[SOC_STAT_RDY].gpio = nano_skts[i].gpio_rdy;
	skt->stat[SOC_STAT_RDY].name = i ? "PC RDY1" : "PC RDY0";

	return 0;
}

static void nanoengine_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free(nano_skts[skt->nr].gpio_rst);
}

static int nanoengine_pcmcia_configure_socket(
	struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
	unsigned i = skt->nr;

	if (i >= num_nano_pcmcia_sockets)
		return -ENXIO;

	gpio_set_value(nano_skts[skt->nr].gpio_rst, !!(state->flags & SS_RESET));

	return 0;
}

static void nanoengine_pcmcia_socket_state(
	struct soc_pcmcia_socket *skt, struct pcmcia_state *state)
{
	unsigned i = skt->nr;

	if (i >= num_nano_pcmcia_sockets)
		return;

	state->bvd1 = 1;
	state->bvd2 = 1;
	state->vs_3v = 1; /* Can only apply 3.3V */
	state->vs_Xv = 0;
}

static struct pcmcia_low_level nanoengine_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.hw_init		= nanoengine_pcmcia_hw_init,
	.hw_shutdown		= nanoengine_pcmcia_hw_shutdown,

	.configure_socket	= nanoengine_pcmcia_configure_socket,
	.socket_state		= nanoengine_pcmcia_socket_state,
};

int pcmcia_nanoengine_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_nanoengine())
		ret = sa11xx_drv_pcmcia_probe(
			dev, &nanoengine_pcmcia_ops, 0, 2);

	return ret;
}

