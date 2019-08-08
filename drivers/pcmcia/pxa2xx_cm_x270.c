// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/pcmcia/pxa/pxa_cm_x270.c
 *
 * Compulab Ltd., 2003, 2007, 2008
 * Mike Rapoport <mike@compulab.co.il>
 */

#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/export.h>

#include "soc_common.h"

#define GPIO_PCMCIA_S0_CD_VALID	(84)
#define GPIO_PCMCIA_S0_RDYINT	(82)
#define GPIO_PCMCIA_RESET	(53)

static int cmx270_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret = gpio_request(GPIO_PCMCIA_RESET, "PCCard reset");
	if (ret)
		return ret;
	gpio_direction_output(GPIO_PCMCIA_RESET, 0);

	skt->stat[SOC_STAT_CD].gpio = GPIO_PCMCIA_S0_CD_VALID;
	skt->stat[SOC_STAT_CD].name = "PCMCIA0 CD";
	skt->stat[SOC_STAT_RDY].gpio = GPIO_PCMCIA_S0_RDYINT;
	skt->stat[SOC_STAT_RDY].name = "PCMCIA0 RDY";

	return ret;
}

static void cmx270_pcmcia_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free(GPIO_PCMCIA_RESET);
}


static void cmx270_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				       struct pcmcia_state *state)
{
	state->vs_3v  = 0;
	state->vs_Xv  = 0;
}


static int cmx270_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
					  const socket_state_t *state)
{
	switch (skt->nr) {
	case 0:
		if (state->flags & SS_RESET) {
			gpio_set_value(GPIO_PCMCIA_RESET, 1);
			udelay(10);
			gpio_set_value(GPIO_PCMCIA_RESET, 0);
		}
		break;
	}

	return 0;
}

static struct pcmcia_low_level cmx270_pcmcia_ops __initdata = {
	.owner			= THIS_MODULE,
	.hw_init		= cmx270_pcmcia_hw_init,
	.hw_shutdown		= cmx270_pcmcia_shutdown,
	.socket_state		= cmx270_pcmcia_socket_state,
	.configure_socket	= cmx270_pcmcia_configure_socket,
	.nr			= 1,
};

static struct platform_device *cmx270_pcmcia_device;

int __init cmx270_pcmcia_init(void)
{
	int ret;

	cmx270_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);

	if (!cmx270_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(cmx270_pcmcia_device, &cmx270_pcmcia_ops,
				       sizeof(cmx270_pcmcia_ops));

	if (ret == 0) {
		printk(KERN_INFO "Registering cm-x270 PCMCIA interface.\n");
		ret = platform_device_add(cmx270_pcmcia_device);
	}

	if (ret)
		platform_device_put(cmx270_pcmcia_device);

	return ret;
}

void __exit cmx270_pcmcia_exit(void)
{
	platform_device_unregister(cmx270_pcmcia_device);
}
