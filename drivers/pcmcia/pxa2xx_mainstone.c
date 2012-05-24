/*
 * linux/drivers/pcmcia/pxa2xx_mainstone.c
 *
 * Mainstone PCMCIA specific routines.
 *
 * Created:	May 12, 2004
 * Author:	Nicolas Pitre
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <pcmcia/ss.h>

#include <asm/mach-types.h>
#include <asm/irq.h>

#include <mach/pxa2xx-regs.h>
#include <mach/mainstone.h>

#include "soc_common.h"


static int mst_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	/*
	 * Setup default state of GPIO outputs
	 * before we enable them as outputs.
	 */
	if (skt->nr == 0) {
		skt->socket.pci_irq = MAINSTONE_S0_IRQ;
		skt->stat[SOC_STAT_CD].irq = MAINSTONE_S0_CD_IRQ;
		skt->stat[SOC_STAT_CD].name = "PCMCIA0 CD";
		skt->stat[SOC_STAT_BVD1].irq = MAINSTONE_S0_STSCHG_IRQ;
		skt->stat[SOC_STAT_BVD1].name = "PCMCIA0 STSCHG";
	} else {
		skt->socket.pci_irq = MAINSTONE_S1_IRQ;
		skt->stat[SOC_STAT_CD].irq = MAINSTONE_S1_CD_IRQ;
		skt->stat[SOC_STAT_CD].name = "PCMCIA1 CD";
		skt->stat[SOC_STAT_BVD1].irq = MAINSTONE_S1_STSCHG_IRQ;
		skt->stat[SOC_STAT_BVD1].name = "PCMCIA1 STSCHG";
	}
	return 0;
}

static unsigned long mst_pcmcia_status[2];

static void mst_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				    struct pcmcia_state *state)
{
	unsigned long status, flip;

	status = (skt->nr == 0) ? MST_PCMCIA0 : MST_PCMCIA1;
	flip = (status ^ mst_pcmcia_status[skt->nr]) & MST_PCMCIA_nSTSCHG_BVD1;

	/*
	 * Workaround for STSCHG which can't be deasserted:
	 * We therefore disable/enable corresponding IRQs
	 * as needed to avoid IRQ locks.
	 */
	if (flip) {
		mst_pcmcia_status[skt->nr] = status;
		if (status & MST_PCMCIA_nSTSCHG_BVD1)
			enable_irq( (skt->nr == 0) ? MAINSTONE_S0_STSCHG_IRQ
						   : MAINSTONE_S1_STSCHG_IRQ );
		else
			disable_irq( (skt->nr == 0) ? MAINSTONE_S0_STSCHG_IRQ
						    : MAINSTONE_S1_STSCHG_IRQ );
	}

	state->detect = (status & MST_PCMCIA_nCD) ? 0 : 1;
	state->ready  = (status & MST_PCMCIA_nIRQ) ? 1 : 0;
	state->bvd1   = (status & MST_PCMCIA_nSTSCHG_BVD1) ? 1 : 0;
	state->bvd2   = (status & MST_PCMCIA_nSPKR_BVD2) ? 1 : 0;
	state->vs_3v  = (status & MST_PCMCIA_nVS1) ? 0 : 1;
	state->vs_Xv  = (status & MST_PCMCIA_nVS2) ? 0 : 1;
}

static int mst_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				       const socket_state_t *state)
{
	unsigned long power = 0;
	int ret = 0;

	switch (state->Vcc) {
	case 0:  power |= MST_PCMCIA_PWR_VCC_0;  break;
	case 33: power |= MST_PCMCIA_PWR_VCC_33; break;
	case 50: power |= MST_PCMCIA_PWR_VCC_50; break;
	default:
		 printk(KERN_ERR "%s(): bad Vcc %u\n",
				 __func__, state->Vcc);
		 ret = -1;
	}

	switch (state->Vpp) {
	case 0:   power |= MST_PCMCIA_PWR_VPP_0;   break;
	case 120: power |= MST_PCMCIA_PWR_VPP_120; break;
	default:
		  if(state->Vpp == state->Vcc) {
			  power |= MST_PCMCIA_PWR_VPP_VCC;
		  } else {
			  printk(KERN_ERR "%s(): bad Vpp %u\n",
					  __func__, state->Vpp);
			  ret = -1;
		  }
	}

	if (state->flags & SS_RESET)
	       power |= MST_PCMCIA_RESET;

	switch (skt->nr) {
	case 0:  MST_PCMCIA0 = power; break;
	case 1:  MST_PCMCIA1 = power; break;
	default: ret = -1;
	}

	return ret;
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
