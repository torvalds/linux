// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/pcmcia/pxa2xx_mainstone.c
 *
 * Mainstone PCMCIA specific routines.
 *
 * Created:	May 12, 2004
 * Author:	Nicolas Pitre
 * Copyright:	MontaVista Software Inc.
 */
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>

#include <pcmcia/ss.h>

#include <asm/mach-types.h>

#include "soc_common.h"
#include "max1600.h"

static int mst_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	struct device *dev = skt->socket.dev.parent;
	struct max1600 *m;
	int ret;

	skt->stat[SOC_STAT_CD].name = skt->nr ? "bdetect" : "adetect";
	skt->stat[SOC_STAT_BVD1].name = skt->nr ? "bbvd1" : "abvd1";
	skt->stat[SOC_STAT_BVD2].name = skt->nr ? "bbvd2" : "abvd2";
	skt->stat[SOC_STAT_RDY].name = skt->nr ? "bready" : "aready";
	skt->stat[SOC_STAT_VS1].name = skt->nr ? "bvs1" : "avs1";
	skt->stat[SOC_STAT_VS2].name = skt->nr ? "bvs2" : "avs2";

	skt->gpio_reset = devm_gpiod_get(dev, skt->nr ? "breset" : "areset",
					 GPIOD_OUT_HIGH);
	if (IS_ERR(skt->gpio_reset))
		return PTR_ERR(skt->gpio_reset);

	ret = max1600_init(dev, &m, skt->nr ? MAX1600_CHAN_B : MAX1600_CHAN_A,
			   MAX1600_CODE_HIGH);
	if (ret)
		return ret;

	skt->driver_data = m;

	return soc_pcmcia_request_gpiods(skt);
}

static unsigned int mst_pcmcia_bvd1_status[2];

static void mst_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				    struct pcmcia_state *state)
{
	unsigned int flip = mst_pcmcia_bvd1_status[skt->nr] ^ state->bvd1;

	/*
	 * Workaround for STSCHG which can't be deasserted:
	 * We therefore disable/enable corresponding IRQs
	 * as needed to avoid IRQ locks.
	 */
	if (flip) {
		mst_pcmcia_bvd1_status[skt->nr] = state->bvd1;
		if (state->bvd1)
			enable_irq(skt->stat[SOC_STAT_BVD1].irq);
		else
			disable_irq(skt->stat[SOC_STAT_BVD2].irq);
	}
}

static int mst_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				       const socket_state_t *state)
{
	return max1600_configure(skt->driver_data, state->Vcc, state->Vpp);
}

static struct pcmcia_low_level mst_pcmcia_ops __initdata = {
	.owner			= THIS_MODULE,
	.hw_init		= mst_pcmcia_hw_init,
	.socket_state		= mst_pcmcia_socket_state,
	.configure_socket	= mst_pcmcia_configure_socket,
	.nr			= 2,
};

static struct platform_device *mst_pcmcia_device;

static int __init mst_pcmcia_init(void)
{
	int ret;

	if (!machine_is_mainstone())
		return -ENODEV;

	mst_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!mst_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(mst_pcmcia_device, &mst_pcmcia_ops,
				       sizeof(mst_pcmcia_ops));
	if (ret == 0)
		ret = platform_device_add(mst_pcmcia_device);

	if (ret)
		platform_device_put(mst_pcmcia_device);

	return ret;
}

static void __exit mst_pcmcia_exit(void)
{
	platform_device_unregister(mst_pcmcia_device);
}

fs_initcall(mst_pcmcia_init);
module_exit(mst_pcmcia_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
