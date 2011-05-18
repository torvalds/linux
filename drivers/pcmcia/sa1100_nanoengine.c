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

static struct pcmcia_irqs irqs_skt0[] = {
	/* socket, IRQ, name */
	{ 0, NANOENGINE_IRQ_GPIO_PC_CD0, "PC CD0" },
};

static struct pcmcia_irqs irqs_skt1[] = {
	/* socket, IRQ, name */
	{ 1, NANOENGINE_IRQ_GPIO_PC_CD1, "PC CD1" },
};

struct nanoengine_pins {
	unsigned input_pins;
	unsigned output_pins;
	unsigned clear_outputs;
	unsigned transition_pins;
	unsigned pci_irq;
	struct pcmcia_irqs *pcmcia_irqs;
	unsigned pcmcia_irqs_size;
};

static struct nanoengine_pins nano_skts[] = {
	{
		.input_pins		= GPIO_PC_READY0 | GPIO_PC_CD0,
		.output_pins		= GPIO_PC_RESET0,
		.clear_outputs		= GPIO_PC_RESET0,
		.transition_pins	= NANOENGINE_IRQ_GPIO_PC_CD0,
		.pci_irq		= NANOENGINE_IRQ_GPIO_PC_READY0,
		.pcmcia_irqs		= irqs_skt0,
		.pcmcia_irqs_size	= ARRAY_SIZE(irqs_skt0)
	}, {
		.input_pins		= GPIO_PC_READY1 | GPIO_PC_CD1,
		.output_pins		= GPIO_PC_RESET1,
		.clear_outputs		= GPIO_PC_RESET1,
		.transition_pins	= NANOENGINE_IRQ_GPIO_PC_CD1,
		.pci_irq		= NANOENGINE_IRQ_GPIO_PC_READY1,
		.pcmcia_irqs		= irqs_skt1,
		.pcmcia_irqs_size	= ARRAY_SIZE(irqs_skt1)
	}
};

unsigned num_nano_pcmcia_sockets = ARRAY_SIZE(nano_skts);

static int nanoengine_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	unsigned i = skt->nr;

	if (i >= num_nano_pcmcia_sockets)
		return -ENXIO;

	GPDR &= ~nano_skts[i].input_pins;
	GPDR |= nano_skts[i].output_pins;
	GPCR = nano_skts[i].clear_outputs;
	irq_set_irq_type(nano_skts[i].transition_pins, IRQ_TYPE_EDGE_BOTH);
	skt->socket.pci_irq = nano_skts[i].pci_irq;

	return soc_pcmcia_request_irqs(skt,
		nano_skts[i].pcmcia_irqs, nano_skts[i].pcmcia_irqs_size);
}

/*
 * Release all resources.
 */
static void nanoengine_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	unsigned i = skt->nr;

	if (i >= num_nano_pcmcia_sockets)
		return;

	soc_pcmcia_free_irqs(skt,
		nano_skts[i].pcmcia_irqs, nano_skts[i].pcmcia_irqs_size);
}

static int nanoengine_pcmcia_configure_socket(
	struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
	unsigned reset;
	unsigned i = skt->nr;

	if (i >= num_nano_pcmcia_sockets)
		return -ENXIO;

	switch (i) {
	case 0:
		reset = GPIO_PC_RESET0;
		break;
	case 1:
		reset = GPIO_PC_RESET1;
		break;
	default:
		return -ENXIO;
	}

	if (state->flags & SS_RESET)
		GPSR = reset;
	else
		GPCR = reset;

	return 0;
}

static void nanoengine_pcmcia_socket_state(
	struct soc_pcmcia_socket *skt, struct pcmcia_state *state)
{
	unsigned long levels = GPLR;
	unsigned i = skt->nr;

	if (i >= num_nano_pcmcia_sockets)
		return;

	memset(state, 0, sizeof(struct pcmcia_state));
	switch (i) {
	case 0:
		state->ready = (levels & GPIO_PC_READY0) ? 1 : 0;
		state->detect = !(levels & GPIO_PC_CD0) ? 1 : 0;
		break;
	case 1:
		state->ready = (levels & GPIO_PC_READY1) ? 1 : 0;
		state->detect = !(levels & GPIO_PC_CD1) ? 1 : 0;
		break;
	default:
		return;
	}
	state->bvd1 = 1;
	state->bvd2 = 1;
	state->wrprot = 0; /* Not available */
	state->vs_3v = 1; /* Can only apply 3.3V */
	state->vs_Xv = 0;
}

/*
 * Enable card status IRQs on (re-)initialisation.  This can
 * be called at initialisation, power management event, or
 * pcmcia event.
 */
static void nanoengine_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
	unsigned i = skt->nr;

	if (i >= num_nano_pcmcia_sockets)
		return;

	soc_pcmcia_enable_irqs(skt,
		nano_skts[i].pcmcia_irqs, nano_skts[i].pcmcia_irqs_size);
}

/*
 * Disable card status IRQs on suspend.
 */
static void nanoengine_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	unsigned i = skt->nr;

	if (i >= num_nano_pcmcia_sockets)
		return;

	soc_pcmcia_disable_irqs(skt,
		nano_skts[i].pcmcia_irqs, nano_skts[i].pcmcia_irqs_size);
}

static struct pcmcia_low_level nanoengine_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.hw_init		= nanoengine_pcmcia_hw_init,
	.hw_shutdown		= nanoengine_pcmcia_hw_shutdown,

	.configure_socket	= nanoengine_pcmcia_configure_socket,
	.socket_state		= nanoengine_pcmcia_socket_state,
	.socket_init		= nanoengine_pcmcia_socket_init,
	.socket_suspend		= nanoengine_pcmcia_socket_suspend,
};

int pcmcia_nanoengine_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_nanoengine())
		ret = sa11xx_drv_pcmcia_probe(
			dev, &nanoengine_pcmcia_ops, 0, 2);

	return ret;
}

