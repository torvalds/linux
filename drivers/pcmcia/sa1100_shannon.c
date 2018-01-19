// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/pcmcia/sa1100_shannon.c
 *
 * PCMCIA implementation routines for Shannon
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <mach/shannon.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

static int shannon_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	/* All those are inputs */
	GAFR &= ~(GPIO_GPIO(SHANNON_GPIO_EJECT_0) |
		  GPIO_GPIO(SHANNON_GPIO_EJECT_1) |
		  GPIO_GPIO(SHANNON_GPIO_RDY_0) |
		  GPIO_GPIO(SHANNON_GPIO_RDY_1));

	if (skt->nr == 0) {
		skt->stat[SOC_STAT_CD].gpio = SHANNON_GPIO_EJECT_0;
		skt->stat[SOC_STAT_CD].name = "PCMCIA_CD_0";
		skt->stat[SOC_STAT_RDY].gpio = SHANNON_GPIO_RDY_0;
		skt->stat[SOC_STAT_RDY].name = "PCMCIA_RDY_0";
	} else {
		skt->stat[SOC_STAT_CD].gpio = SHANNON_GPIO_EJECT_1;
		skt->stat[SOC_STAT_CD].name = "PCMCIA_CD_1";
		skt->stat[SOC_STAT_RDY].gpio = SHANNON_GPIO_RDY_1;
		skt->stat[SOC_STAT_RDY].name = "PCMCIA_RDY_1";
	}

	return 0;
}

static void
shannon_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
			    struct pcmcia_state *state)
{
	switch (skt->nr) {
	case 0:
		state->bvd1   = 1; 
		state->bvd2   = 1; 
		state->vs_3v  = 1; /* FIXME Can only apply 3.3V on Shannon. */
		state->vs_Xv  = 0;
		break;

	case 1:
		state->bvd1   = 1; 
		state->bvd2   = 1; 
		state->vs_3v  = 1; /* FIXME Can only apply 3.3V on Shannon. */
		state->vs_Xv  = 0;
		break;
	}
}

static int
shannon_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				const socket_state_t *state)
{
	switch (state->Vcc) {
	case 0:	/* power off */
		printk(KERN_WARNING "%s(): CS asked for 0V, still applying 3.3V..\n", __func__);
		break;
	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V..\n", __func__);
	case 33:
		break;
	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n",
		       __func__, state->Vcc);
		return -1;
	}

	printk(KERN_WARNING "%s(): Warning, Can't perform reset\n", __func__);
	
	/* Silently ignore Vpp, output enable, speaker enable. */

	return 0;
}

static struct pcmcia_low_level shannon_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= shannon_pcmcia_hw_init,
	.socket_state		= shannon_pcmcia_socket_state,
	.configure_socket	= shannon_pcmcia_configure_socket,
};

int pcmcia_shannon_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_shannon())
		ret = sa11xx_drv_pcmcia_probe(dev, &shannon_pcmcia_ops, 0, 2);

	return ret;
}
