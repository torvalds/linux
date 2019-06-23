// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/pcmcia/pxa2xx_balloon3.c
 *
 * Balloon3 PCMCIA specific routines.
 *
 *  Author:	Nick Bane
 *  Created:	June, 2006
 *  Copyright:	Toby Churchill Ltd
 *  Derived from pxa2xx_mainstone.c, by Nico Pitre
 *
 * Various modification by Marek Vasut <marek.vasut@gmail.com>
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/balloon3.h>

#include <asm/mach-types.h>

#include "soc_common.h"

static int balloon3_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	uint16_t ver;

	ver = __raw_readw(BALLOON3_FPGA_VER);
	if (ver < 0x4f08)
		pr_warn("The FPGA code, version 0x%04x, is too old. "
			"PCMCIA/CF support might be broken in this version!",
			ver);

	skt->socket.pci_irq = BALLOON3_BP_CF_NRDY_IRQ;
	skt->stat[SOC_STAT_CD].gpio = BALLOON3_GPIO_S0_CD;
	skt->stat[SOC_STAT_CD].name = "PCMCIA0 CD";
	skt->stat[SOC_STAT_BVD1].irq = BALLOON3_BP_NSTSCHG_IRQ;
	skt->stat[SOC_STAT_BVD1].name = "PCMCIA0 STSCHG";

	return 0;
}

static unsigned long balloon3_pcmcia_status[2] = {
	BALLOON3_CF_nSTSCHG_BVD1,
	BALLOON3_CF_nSTSCHG_BVD1
};

static void balloon3_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				    struct pcmcia_state *state)
{
	uint16_t status;
	int flip;

	/* This actually reads the STATUS register */
	status = __raw_readw(BALLOON3_CF_STATUS_REG);
	flip = (status ^ balloon3_pcmcia_status[skt->nr])
		& BALLOON3_CF_nSTSCHG_BVD1;
	/*
	 * Workaround for STSCHG which can't be deasserted:
	 * We therefore disable/enable corresponding IRQs
	 * as needed to avoid IRQ locks.
	 */
	if (flip) {
		balloon3_pcmcia_status[skt->nr] = status;
		if (status & BALLOON3_CF_nSTSCHG_BVD1)
			enable_irq(BALLOON3_BP_NSTSCHG_IRQ);
		else
			disable_irq(BALLOON3_BP_NSTSCHG_IRQ);
	}

	state->ready	= !!(status & BALLOON3_CF_nIRQ);
	state->bvd1	= !!(status & BALLOON3_CF_nSTSCHG_BVD1);
	state->bvd2	= 0;	/* not available */
	state->vs_3v	= 1;	/* Always true its a CF card */
	state->vs_Xv	= 0;	/* not available */
}

static int balloon3_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				       const socket_state_t *state)
{
	__raw_writew(BALLOON3_CF_RESET, BALLOON3_CF_CONTROL_REG +
			((state->flags & SS_RESET) ?
			BALLOON3_FPGA_SETnCLR : 0));
	return 0;
}

static struct pcmcia_low_level balloon3_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= balloon3_pcmcia_hw_init,
	.socket_state		= balloon3_pcmcia_socket_state,
	.configure_socket	= balloon3_pcmcia_configure_socket,
	.first			= 0,
	.nr			= 1,
};

static struct platform_device *balloon3_pcmcia_device;

static int __init balloon3_pcmcia_init(void)
{
	int ret;

	if (!machine_is_balloon3())
		return -ENODEV;

	balloon3_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!balloon3_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(balloon3_pcmcia_device,
			&balloon3_pcmcia_ops, sizeof(balloon3_pcmcia_ops));

	if (!ret)
		ret = platform_device_add(balloon3_pcmcia_device);

	if (ret)
		platform_device_put(balloon3_pcmcia_device);

	return ret;
}

static void __exit balloon3_pcmcia_exit(void)
{
	platform_device_unregister(balloon3_pcmcia_device);
}

module_init(balloon3_pcmcia_init);
module_exit(balloon3_pcmcia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick Bane <nick@cecomputing.co.uk>");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
MODULE_DESCRIPTION("Balloon3 board CF/PCMCIA driver");
