/*
 * drivers/pcmcia/sa1100_cerf.c
 *
 * PCMCIA implementation routines for CerfBoard
 * Based off the Assabet.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <mach/cerf.h>
#include "sa1100_generic.h"

#define CERF_SOCKET	1

static int cerf_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	ret = gpio_request_one(CERF_GPIO_CF_RESET, GPIOF_OUT_INIT_LOW, "CF_RESET");
	if (ret)
		return ret;

	skt->stat[SOC_STAT_CD].gpio = CERF_GPIO_CF_CD;
	skt->stat[SOC_STAT_CD].name = "CF_CD";
	skt->stat[SOC_STAT_BVD1].gpio = CERF_GPIO_CF_BVD1;
	skt->stat[SOC_STAT_BVD1].name = "CF_BVD1";
	skt->stat[SOC_STAT_BVD2].gpio = CERF_GPIO_CF_BVD2;
	skt->stat[SOC_STAT_BVD2].name = "CF_BVD2";
	skt->stat[SOC_STAT_RDY].gpio = CERF_GPIO_CF_IRQ;
	skt->stat[SOC_STAT_RDY].name = "CF_IRQ";

	return 0;
}

static void cerf_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free(CERF_GPIO_CF_RESET);
}

static void
cerf_pcmcia_socket_state(struct soc_pcmcia_socket *skt, struct pcmcia_state *state)
{
	state->vs_3v	= 1;
	state->vs_Xv	= 0;
}

static int
cerf_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
			     const socket_state_t *state)
{
	switch (state->Vcc) {
	case 0:
	case 50:
	case 33:
		break;

	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n",
			__func__, state->Vcc);
		return -1;
	}

	gpio_set_value(CERF_GPIO_CF_RESET, !!(state->flags & SS_RESET));

	return 0;
}

static struct pcmcia_low_level cerf_pcmcia_ops = { 
	.owner			= THIS_MODULE,
	.hw_init		= cerf_pcmcia_hw_init,
	.hw_shutdown		= cerf_pcmcia_hw_shutdown,
	.socket_state		= cerf_pcmcia_socket_state,
	.configure_socket	= cerf_pcmcia_configure_socket,
};

int pcmcia_cerf_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_cerf())
		ret = sa11xx_drv_pcmcia_probe(dev, &cerf_pcmcia_ops, CERF_SOCKET, 1);

	return ret;
}
